# =============================================================================
#  KernelNucleusT — CMake build configuration
# =============================================================================
#
#  Overview
#  --------
#   Header-only C++20 framework with optional window/GPU/network backends.
#   Library-only target: KernelNucleusT::KernelNucleusT (INTERFACE).
#
#   This configuration builds EXACTLY ONE executable from the source you
#   pass via -DKNST_APP_SOURCE=... — nothing else. If KNST_APP_SOURCE is
#   empty, only the header target is configured (installable, usable from
#   other projects via find_package(KernelNucleusT)).
#
#  Supported platforms
#  -------------------
#   Windows  — Win32 / Win64 (MSVC, MinGW, Clang)
#   Linux    — X11 / Wayland / Dual (runtime-selected)
#   Android  — NDK (API 24+)
#
#  GPU backends (optional, may be combined)
#  ----------------------------------------
#   -DKNST_WINDOW_USE_OPENGL=ON   GLAD + EGL/WGL
#   -DKNST_WINDOW_USE_VULKAN=ON   Vulkan loader
#
#  Network stack (optional, auto-detected)
#  ---------------------------------------
#   Detection order per component:
#     1. find_package(<name> CONFIG)     — vcpkg / Conan / cmake-installed
#     2. pkg_check_modules(<pc-name>)    — Linux / pkg-config
#     3. find_library + find_path         — last resort
#
#   Opt-out flags:
#     -DKNST_NETWORK_DISABLE_TLS=ON
#     -DKNST_NETWORK_DISABLE_H2=ON
#     -DKNST_NETWORK_DISABLE_H3=ON
#
#   Tiers (highest wins):
#     H3  →  quictls + nghttp2 + ngtcp2 + nghttp3   (TLS + H2 + H3)
#     H2  →  system OpenSSL + nghttp2               (TLS + H2)
#     H1  →  system OpenSSL                         (TLS + H1)
#     off →  none                                   (no network TLS at all)
#
#  quictls discovery (H3 only)
#  ---------------------------
#   Search order:
#     1. -DKNST_QUICTLS_ROOT=/path/to/quictls   (cache override)
#     2. $ENV{QUICTLS_ROOT}                     (environment variable)
#     3. Common prefixes: /usr/local/quictls, /opt/quictls, $HOME/quictls
#
#   If quictls is not found, H3 is silently disabled and the build
#   continues with H2 or H1.
#
#  IMPORTANT: when H3 is enabled, this build links ONLY quictls
#  (libssl + libcrypto from the quictls prefix). System OpenSSL is
#  NOT linked in the same binary, which avoids an ABI clash. At
#  runtime, RPATH prepends the quictls lib directory so libssl.so.3
#  resolves to the QUIC-capable build.
#
#  Building an executable
#  ----------------------
#   cmake -S . -B build \
#     -DCMAKE_BUILD_TYPE=Release \
#     -DKNST_APP_SOURCE=examples/simple_structures/knst_json/knst_json_basic.cpp
#   cmake --build build -j
#   ./build/bin/knst_app
#
#  Consuming from another project
#  ------------------------------
#   find_package(KernelNucleusT REQUIRED)
#   target_link_libraries(my_app PRIVATE KernelNucleusT::KernelNucleusT)
# =============================================================================

cmake_minimum_required(VERSION 3.20)

project(KernelNucleusT
    VERSION     2.0.0
    DESCRIPTION "KernelNucleusT cross-platform C++20 framework"
    LANGUAGES   C CXX
)

# Pre-release suffix — not part of CMake's numeric VERSION.
# Printed in the summary; tags use v2.0.0-alpha.1.
set(KNST_VERSION_SUFFIX "-alpha.1" CACHE STRING "Version suffix for display")

# ─────────────────────────────────────────────────────────────────────────────
#  Top-level project detection
# ─────────────────────────────────────────────────────────────────────────────
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(KNST_IS_TOP_LEVEL ON)
else()
    set(KNST_IS_TOP_LEVEL OFF)
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  User options
# ─────────────────────────────────────────────────────────────────────────────
option(KNST_ENABLE_SANITIZERS   "Enable AddressSanitizer + UBSan (debug builds)" OFF)
option(KNST_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors"              OFF)
option(KNST_STATIC_MSVC_RUNTIME "Use the static MSVC runtime (/MT, /MTd)"        ON)

option(KNST_WINDOW_USE_OPENGL "Enable the OpenGL backend (GLAD + EGL/WGL)" OFF)
option(KNST_WINDOW_USE_VULKAN "Enable the Vulkan backend"                  OFF)

option(KNST_NETWORK_DISABLE_TLS "Disable TLS entirely (no OpenSSL, no wss://, no h2/h3)" OFF)
option(KNST_NETWORK_DISABLE_H2  "Disable HTTP/2 even if nghttp2 is installed"            OFF)
option(KNST_NETWORK_DISABLE_H3  "Disable HTTP/3 even if ngtcp2/nghttp3/quictls are installed" OFF)

option(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND
       "Compile both X11 and Wayland backends; select one at runtime" OFF)

set(KNST_QUICTLS_ROOT "" CACHE PATH
    "Path to quictls install prefix (QUIC-capable OpenSSL). Empty = auto-detect.")
set(KNST_NGTCP2_ROOT "" CACHE PATH
    "Path to ngtcp2 install prefix. Empty = auto-detect via find_package/pkg-config.")
set(KNST_NGHTTP3_ROOT "" CACHE PATH
    "Path to nghttp3 install prefix. Empty = auto-detect via find_package/pkg-config.")
set(KNST_NGHTTP2_ROOT "" CACHE PATH
    "Path to nghttp2 install prefix. Empty = auto-detect via find_package/pkg-config.")

# The ONLY source that is built. Absolute or relative to this CMakeLists.
# Empty = headers only (library target still exported / installable).
set(KNST_APP_SOURCE "" CACHE FILEPATH
    "Path to a .cpp file to build as 'knst_app'. Empty = headers only.")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    set(KNST_LINUX_PLATFORM "X11" CACHE STRING
        "Linux display server protocol (X11 or Wayland)")
    set_property(CACHE KNST_LINUX_PLATFORM PROPERTY STRINGS "X11" "Wayland")
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Global language settings
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_C_EXTENSIONS          OFF)

if(KNST_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
    endif()

    if(MSVC AND KNST_STATIC_MSVC_RUNTIME)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Platform detection
# ─────────────────────────────────────────────────────────────────────────────
if(ANDROID)
    set(KNST_PLATFORM "Android")
elseif(WIN32)
    set(KNST_PLATFORM "Windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(KNST_PLATFORM "Linux")
elseif(APPLE)
    message(FATAL_ERROR
        "macOS is not supported. "
        "Supported platforms: Windows, Linux (X11/Wayland), Android.")
else()
    message(FATAL_ERROR
        "Unsupported platform '${CMAKE_SYSTEM_NAME}'. "
        "Supported platforms: Windows, Linux (X11/Wayland), Android.")
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Primary INTERFACE target (header-only)
# ─────────────────────────────────────────────────────────────────────────────
add_library(KernelNucleusT INTERFACE)
add_library(KernelNucleusT::KernelNucleusT ALIAS KernelNucleusT)

target_include_directories(KernelNucleusT INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(KernelNucleusT INTERFACE cxx_std_20)

set(KNST_DEFINITIONS    "")
set(KNST_LINK_ITEMS     "")
set(KNST_PLATFORM_DETAIL "${KNST_PLATFORM}")

# ─────────────────────────────────────────────────────────────────────────────
#  Threads + common system libs (non-Windows)
# ─────────────────────────────────────────────────────────────────────────────
if(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    list(APPEND KNST_LINK_ITEMS Threads::Threads ${CMAKE_DL_LIBS})
endif()

# =============================================================================
#  Platform: Android
# =============================================================================
if(KNST_PLATFORM STREQUAL "Android")
    list(APPEND KNST_DEFINITIONS
        KNST_USING_PLATFORM_ANDROID
        KNST_USING_PLATFORM_LINUX)
            list(APPEND KNST_LINK_ITEMS android log)

    if(DEFINED ANDROID_PLATFORM_LEVEL)
        set(KNST_ANDROID_API "${ANDROID_PLATFORM_LEVEL}")
    elseif(DEFINED ANDROID_NATIVE_API_LEVEL)
        set(KNST_ANDROID_API "${ANDROID_NATIVE_API_LEVEL}")
    else()
        set(KNST_ANDROID_API 0)
    endif()

    if(KNST_ANDROID_API GREATER 0 AND KNST_ANDROID_API LESS 24)
        message(WARNING
            "Android API level ${KNST_ANDROID_API} is below 24. "
            "Some features may not work.")
    endif()

    # ---- quictls (Android NDK cross-compile) ----
    if(NOT KNST_NETWORK_DISABLE_TLS)
        set(QUICTLS_ROOT "$ENV{HOME}/.knst-cache/quictls-android-${ANDROID_ABI}")
        set(_quictls_src "$ENV{HOME}/.knst-cache/quictls-src-${ANDROID_ABI}")

        if(ANDROID_ABI STREQUAL "arm64-v8a")
            set(_quictls_target "android-arm64")
            set(_quictls_triple "aarch64-linux-android")
        elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
            set(_quictls_target "android-arm")
            set(_quictls_triple "armv7a-linux-androideabi")
        elseif(ANDROID_ABI STREQUAL "x86_64")
            set(_quictls_target "android-x86_64")
            set(_quictls_triple "x86_64-linux-android")
        elseif(ANDROID_ABI STREQUAL "x86")
            set(_quictls_target "android-x86")
            set(_quictls_triple "i686-linux-android")
        else()
            message(FATAL_ERROR "Android TLS: unsupported ABI '${ANDROID_ABI}'")
        endif()

        set(_quictls_ndk_bin "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin")
        set(_quictls_api "${KNST_ANDROID_API}")

        if(NOT EXISTS "${QUICTLS_ROOT}/lib/libssl.a")
            message(STATUS "Android quictls: downloading + building (10-15 min first time)...")
            include(FetchContent)
            FetchContent_Declare(quictls_src
                GIT_REPOSITORY https://github.com/quictls/openssl.git
                GIT_TAG        openssl-3.1.5+quic
                GIT_SHALLOW    TRUE
                SOURCE_DIR     "${_quictls_src}")
            FetchContent_GetProperties(quictls_src)
            if(NOT quictls_src_POPULATED)
                FetchContent_Populate(quictls_src)
            endif()
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E env
                    "ANDROID_NDK_ROOT=${ANDROID_NDK}"
                    "ANDROID_NDK_HOME=${ANDROID_NDK}"
                    "PATH=${_quictls_ndk_bin}:$ENV{PATH}"
                    "CC=${_quictls_ndk_bin}/${_quictls_triple}${_quictls_api}-clang"
                    "CXX=${_quictls_ndk_bin}/${_quictls_triple}${_quictls_api}-clang++"
                    "AR=${_quictls_ndk_bin}/llvm-ar"
                    "RANLIB=${_quictls_ndk_bin}/llvm-ranlib"
                    "STRIP=${_quictls_ndk_bin}/llvm-strip"
                    "${_quictls_src}/Configure"
                        "${_quictls_target}"
                        "-D__ANDROID_API__=${_quictls_api}"
                        no-shared
                        "--prefix=${QUICTLS_ROOT}"
                        "--openssldir=${QUICTLS_ROOT}/ssl"
                WORKING_DIRECTORY "${_quictls_src}"
                RESULT_VARIABLE _quictls_cfg_rc)
            if(NOT _quictls_cfg_rc EQUAL 0)
                message(FATAL_ERROR "quictls Configure failed (rc=${_quictls_cfg_rc})")
            endif()
            set(_quictls_jobs "4")
            if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
                set(_quictls_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
            endif()
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E env "PATH=${_quictls_ndk_bin}:$ENV{PATH}"
                    make "-j${_quictls_jobs}"
                WORKING_DIRECTORY "${_quictls_src}"
                RESULT_VARIABLE _quictls_build_rc)
            if(NOT _quictls_build_rc EQUAL 0)
                message(FATAL_ERROR "quictls make failed (rc=${_quictls_build_rc})")
            endif()
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E env "PATH=${_quictls_ndk_bin}:$ENV{PATH}"
                    make install_sw
                WORKING_DIRECTORY "${_quictls_src}"
                RESULT_VARIABLE _quictls_install_rc)
            if(NOT _quictls_install_rc EQUAL 0)
                message(FATAL_ERROR "quictls install_sw failed (rc=${_quictls_install_rc})")
            endif()
        else()
            message(STATUS "Android quictls: ready (${QUICTLS_ROOT})")
        endif()

        target_include_directories(KernelNucleusT INTERFACE "${QUICTLS_ROOT}/include")
        target_link_libraries(KernelNucleusT INTERFACE
            "${QUICTLS_ROOT}/lib/libssl.a"
            "${QUICTLS_ROOT}/lib/libcrypto.a"
            dl)
        list(APPEND KNST_DEFINITIONS KNST_NETWORK_ENABLE_TLS)

        set(OPENSSL_ROOT_DIR    "${QUICTLS_ROOT}"              CACHE PATH     "" FORCE)
        set(OPENSSL_INCLUDE_DIR "${QUICTLS_ROOT}/include"     CACHE PATH     "" FORCE)
        set(OPENSSL_SSL_LIBRARY "${QUICTLS_ROOT}/lib/libssl.a" CACHE FILEPATH "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY "${QUICTLS_ROOT}/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
        set(OPENSSL_FOUND TRUE CACHE BOOL "" FORCE)
    endif()

    # ---- nghttp2 (Android, FetchContent) ----
    if(NOT KNST_NETWORK_DISABLE_TLS AND NOT KNST_NETWORK_DISABLE_H2)
        include(FetchContent)
        set(ENABLE_LIB_ONLY        ON  CACHE BOOL "" FORCE)
        set(ENABLE_STATIC_LIB      ON  CACHE BOOL "" FORCE)
        set(ENABLE_SHARED_LIB      OFF CACHE BOOL "" FORCE)
        set(ENABLE_APP             OFF CACHE BOOL "" FORCE)
        set(ENABLE_HPACK_TOOLS     OFF CACHE BOOL "" FORCE)
        set(ENABLE_EXAMPLES        OFF CACHE BOOL "" FORCE)
        set(ENABLE_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
        set(ENABLE_FAILALL         OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(nghttp2
            GIT_REPOSITORY https://github.com/nghttp2/nghttp2.git
            GIT_TAG        v1.58.0)
        FetchContent_MakeAvailable(nghttp2)
        list(APPEND KNST_DEFINITIONS KNST_NETWORK_ENABLE_H2)
    endif()

    # ---- nghttp3 (Android, FetchContent) ----
    if(NOT KNST_NETWORK_DISABLE_TLS AND NOT KNST_NETWORK_DISABLE_H3)
        include(FetchContent)
        set(ENABLE_LIB_ONLY    ON  CACHE BOOL "" FORCE)
        set(ENABLE_STATIC_LIB  ON  CACHE BOOL "" FORCE)
        set(ENABLE_SHARED_LIB  OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING      OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
        set(ENABLE_FAILALL     OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(nghttp3
            GIT_REPOSITORY https://github.com/ngtcp2/nghttp3.git
            GIT_TAG        v1.6.0)
        FetchContent_GetProperties(nghttp3)
        if(NOT nghttp3_POPULATED)
            FetchContent_Populate(nghttp3)
            set(_nghttp3_cml "${nghttp3_SOURCE_DIR}/CMakeLists.txt")
            if(EXISTS "${_nghttp3_cml}")
                file(READ "${_nghttp3_cml}" _nghttp3_content)
                string(REPLACE "add_custom_target(check" "# add_custom_target(check"
                       _nghttp3_content "${_nghttp3_content}")
                file(WRITE "${_nghttp3_cml}" "${_nghttp3_content}")
            endif()
            add_subdirectory(${nghttp3_SOURCE_DIR} ${nghttp3_BINARY_DIR})
        endif()
    endif()

    # ---- ngtcp2 (Android, FetchContent) ----
    if(NOT KNST_NETWORK_DISABLE_TLS AND NOT KNST_NETWORK_DISABLE_H3)
        include(FetchContent)
        set(ENABLE_LIB_ONLY    ON  CACHE BOOL "" FORCE)
        set(ENABLE_STATIC_LIB  ON  CACHE BOOL "" FORCE)
        set(ENABLE_SHARED_LIB  OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING      OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
        set(ENABLE_OPENSSL     ON  CACHE BOOL "" FORCE)
        FetchContent_Declare(ngtcp2
            GIT_REPOSITORY https://github.com/ngtcp2/ngtcp2.git
            GIT_TAG        v1.6.0)
        FetchContent_GetProperties(ngtcp2)
        if(NOT ngtcp2_POPULATED)
            FetchContent_Populate(ngtcp2)
            set(_ngtcp2_cml "${ngtcp2_SOURCE_DIR}/CMakeLists.txt")
            if(EXISTS "${_ngtcp2_cml}")
                file(READ "${_ngtcp2_cml}" _ngtcp2_content)
                string(REPLACE "add_custom_target(check" "# add_custom_target(check"
                       _ngtcp2_content "${_ngtcp2_content}")
                file(WRITE "${_ngtcp2_cml}" "${_ngtcp2_content}")
            endif()
            add_subdirectory(${ngtcp2_SOURCE_DIR} ${ngtcp2_BINARY_DIR})
        endif()
        list(APPEND KNST_DEFINITIONS KNST_NETWORK_ENABLE_H3)
    endif()

    set(KNST_PLATFORM_DETAIL "Android (API ${KNST_ANDROID_API}, ABI ${ANDROID_ABI})")

# =============================================================================
#  Platform: Windows
# =============================================================================
elseif(KNST_PLATFORM STREQUAL "Windows")
list(APPEND KNST_DEFINITIONS
    KNST_USING_PLATFORM_WINDOWS
    WIN32_LEAN_AND_MEAN        # winsock.h (v1) -> winsock2.h clash prevention
    NOMINMAX
    OEMRESOURCE
    _CRT_SECURE_NO_WARNINGS
    _WIN32_WINNT=0x0A00        # Win10: MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING
    WINVER=0x0A00)             # Win8+ required for above; target is Win10 so 0x0A00

        list(APPEND KNST_LINK_ITEMS
        gdi32 user32 shell32 shcore ole32 oleaut32 uuid
        hid bthprops setupapi strmiids iphlpapi ws2_32 wbemuuid
        psapi dxgi pdh advapi32
        winmm wlanapi crypt32 bcrypt userenv
        mfplat mf mfuuid mfreadwrite ksuser
        winspool wiaguid)

    if(MSVC)
        set(KNST_PLATFORM_DETAIL "Windows (MSVC)")
    else()
        set(KNST_PLATFORM_DETAIL "Windows (${CMAKE_CXX_COMPILER_ID} / MinGW)")
    endif()

# =============================================================================
#  Platform: Linux (X11 / Wayland / Dual)
# =============================================================================
elseif(KNST_PLATFORM STREQUAL "Linux")
    list(APPEND KNST_DEFINITIONS KNST_USING_PLATFORM_LINUX)
    list(APPEND KNST_LINK_ITEMS m)

    find_package(PkgConfig REQUIRED)

    function(knst_setup_wayland_protocols)
        file(GLOB_RECURSE _all_xmls "${KNST_WP_DIR}/*.xml")

        function(_knst_resolve_xml xml_basename out_var)
            foreach(_xml IN LISTS _all_xmls)
                get_filename_component(_base "${_xml}" NAME)
                if(_base STREQUAL "${xml_basename}")
                    set(${out_var} "${_xml}" PARENT_SCOPE)
                    return()
                endif()
            endforeach()
            set(${out_var} "" PARENT_SCOPE)
        endfunction()

        set(_protos
            "xdg-shell.xml|xdg-shell"
            "xdg-decoration-unstable-v1.xml|xdg-decoration"
            "pointer-constraints-unstable-v1.xml|pointer-constraints-unstable-v1"
            "relative-pointer-unstable-v1.xml|relative-pointer-unstable-v1"
            "cursor-shape-v1.xml|cursor-shape"
            "fractional-scale-v1.xml|fractional-scale"
            "viewporter.xml|viewporter"
            "xdg-activation-v1.xml|xdg-activation"
            "primary-selection-unstable-v1.xml|primary-selection"
            "idle-inhibit-unstable-v1.xml|idle-inhibit"
            "text-input-unstable-v3.xml|text-input-v3"
            "xdg-toplevel-icon-v1.xml|xdg-toplevel-icon"
            "tablet-v2.xml|tablet-v2")

        set(_sources "")
        set(_headers "")
        set(_generated_count 0)

        foreach(_entry IN LISTS _protos)
            string(REPLACE "|" ";" _parts "${_entry}")
            list(GET _parts 0 _xml_name)
            list(GET _parts 1 _proto_name)

            _knst_resolve_xml("${_xml_name}" _xml_path)
            if(NOT _xml_path)
                message(STATUS "Wayland protocol skipped (XML not found): ${_xml_name}")
                continue()
            endif()

            set(_hdr "${KNST_WAYLAND_PROTOCOL_DIR}/${_proto_name}-client-protocol.h")
            set(_src "${KNST_WAYLAND_PROTOCOL_DIR}/${_proto_name}-protocol.c")

            add_custom_command(
                OUTPUT  "${_hdr}"
                COMMAND "${KNST_WAYLAND_SCANNER}" client-header "${_xml_path}" "${_hdr}"
                DEPENDS "${_xml_path}"
                COMMENT "wayland-scanner: ${_proto_name} header"
                VERBATIM)
            add_custom_command(
                OUTPUT  "${_src}"
                COMMAND "${KNST_WAYLAND_SCANNER}" private-code "${_xml_path}" "${_src}"
                DEPENDS "${_xml_path}"
                COMMENT "wayland-scanner: ${_proto_name} source"
                VERBATIM)

            list(APPEND _sources "${_src}")
            list(APPEND _headers "${_hdr}")
            math(EXPR _generated_count "${_generated_count} + 1")
        endforeach()

        if(NOT _sources)
            message(FATAL_ERROR
                "No Wayland protocols found. "
                "Install: sudo apt install wayland-protocols")
        endif()

        add_library(knst_wayland_protocols STATIC ${_sources} ${_headers})
        set_target_properties(knst_wayland_protocols PROPERTIES
            LINKER_LANGUAGE C
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON)
        target_include_directories(knst_wayland_protocols PUBLIC
            $<BUILD_INTERFACE:${KNST_WAYLAND_PROTOCOL_DIR}>
            $<INSTALL_INTERFACE:include/platform/linux/wayland/protocol_files>)
        target_link_libraries(knst_wayland_protocols PUBLIC PkgConfig::KNST_WAYLAND)
    endfunction()

    if(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
        message(STATUS "Linux window backend: DUAL (X11 + Wayland)")
        set(KNST_PLATFORM_DETAIL "Linux (X11 + Wayland, runtime-selected)")
        list(APPEND KNST_DEFINITIONS
            KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND
            KNST_USING_LINUX_PLATFORM_X11
            KNST_USING_LINUX_PLATFORM_WAYLAND)

        set(KNST_X11_MODULES
            x11 x11-xcb xcb xcb-randr xcb-keysyms xcb-icccm xcb-util
            xext xfixes xcb-sync xi xrandr xcursor xcb-xinput)
        foreach(_mod IN LISTS KNST_X11_MODULES)
            string(MAKE_C_IDENTIFIER "KNST_PC_${_mod}" _prefix)
            pkg_check_modules(${_prefix} REQUIRED IMPORTED_TARGET GLOBAL ${_mod})
            list(APPEND KNST_LINK_ITEMS PkgConfig::${_prefix})
        endforeach()

        pkg_check_modules(KNST_WAYLAND REQUIRED IMPORTED_TARGET GLOBAL
            wayland-client wayland-cursor xkbcommon)
        list(APPEND KNST_LINK_ITEMS PkgConfig::KNST_WAYLAND)

        if(KNST_WINDOW_USE_OPENGL)
            pkg_check_modules(KNST_WAYLAND_EGL REQUIRED IMPORTED_TARGET GLOBAL wayland-egl)
        else()
            pkg_check_modules(KNST_WAYLAND_EGL QUIET    IMPORTED_TARGET GLOBAL wayland-egl)
        endif()
        if(TARGET PkgConfig::KNST_WAYLAND_EGL)
            list(APPEND KNST_LINK_ITEMS PkgConfig::KNST_WAYLAND_EGL)
        endif()

        find_program(KNST_WAYLAND_SCANNER NAMES wayland-scanner HINTS /usr/bin /usr/local/bin)
        if(NOT KNST_WAYLAND_SCANNER)
            message(FATAL_ERROR "wayland-scanner not found. Install: sudo apt install libwayland-bin")
        endif()

        pkg_check_modules(KNST_WAYLAND_PROTOCOLS QUIET wayland-protocols)
        if(KNST_WAYLAND_PROTOCOLS_FOUND)
            pkg_get_variable(KNST_WP_DIR wayland-protocols pkgdatadir)
        endif()
        if(NOT KNST_WP_DIR)
            set(KNST_WP_DIR "/usr/share/wayland-protocols")
        endif()

        set(KNST_WAYLAND_PROTOCOL_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/linux/wayland/protocol_files")
        file(MAKE_DIRECTORY "${KNST_WAYLAND_PROTOCOL_DIR}")
        knst_setup_wayland_protocols()

        file(GLOB _knst_generated_protos CONFIGURE_DEPENDS
            "${KNST_WAYLAND_PROTOCOL_DIR}/*-protocol.c"
            "${KNST_WAYLAND_PROTOCOL_DIR}/*-client-protocol.h")
        if(_knst_generated_protos)
            set_source_files_properties(${_knst_generated_protos} PROPERTIES GENERATED TRUE)
        endif()
        list(APPEND KNST_LINK_ITEMS knst_wayland_protocols)

    else()
        string(TOUPPER "${KNST_LINUX_PLATFORM}" KNST_LINUX_PLATFORM_UC)
        if(NOT KNST_LINUX_PLATFORM_UC MATCHES "^(X11|WAYLAND)$")
            message(FATAL_ERROR "KNST_LINUX_PLATFORM must be X11 or Wayland. Got: '${KNST_LINUX_PLATFORM}'")
        endif()

        if(KNST_LINUX_PLATFORM_UC STREQUAL "WAYLAND")
            list(APPEND KNST_DEFINITIONS KNST_USING_LINUX_PLATFORM_WAYLAND)
            set(KNST_PLATFORM_DETAIL "Linux (Wayland)")

            pkg_check_modules(KNST_WAYLAND REQUIRED IMPORTED_TARGET GLOBAL
                wayland-client wayland-cursor xkbcommon)
            if(KNST_WINDOW_USE_OPENGL)
                pkg_check_modules(KNST_WAYLAND_EGL REQUIRED IMPORTED_TARGET GLOBAL wayland-egl)
            else()
                pkg_check_modules(KNST_WAYLAND_EGL QUIET    IMPORTED_TARGET GLOBAL wayland-egl)
            endif()

            find_program(KNST_WAYLAND_SCANNER NAMES wayland-scanner HINTS /usr/bin /usr/local/bin)
            if(NOT KNST_WAYLAND_SCANNER)
                message(FATAL_ERROR "wayland-scanner not found. Install: sudo apt install libwayland-bin")
            endif()

            pkg_check_modules(KNST_WAYLAND_PROTOCOLS QUIET wayland-protocols)
            if(KNST_WAYLAND_PROTOCOLS_FOUND)
                pkg_get_variable(KNST_WP_DIR wayland-protocols pkgdatadir)
            endif()
            if(NOT KNST_WP_DIR)
                set(KNST_WP_DIR "/usr/share/wayland-protocols")
            endif()

            set(KNST_WAYLAND_PROTOCOL_DIR
                "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/linux/wayland/protocol_files")
            file(MAKE_DIRECTORY "${KNST_WAYLAND_PROTOCOL_DIR}")
            knst_setup_wayland_protocols()

            list(APPEND KNST_LINK_ITEMS knst_wayland_protocols)
            if(TARGET PkgConfig::KNST_WAYLAND_EGL)
                list(APPEND KNST_LINK_ITEMS PkgConfig::KNST_WAYLAND_EGL)
            endif()

        else()
            list(APPEND KNST_DEFINITIONS KNST_USING_LINUX_PLATFORM_X11)
            set(KNST_PLATFORM_DETAIL "Linux (X11)")

            set(KNST_X11_MODULES
                x11 x11-xcb xcb xcb-randr xcb-keysyms xcb-icccm xcb-util
                xext xfixes xcb-sync xi xrandr xcursor xcb-xinput)

            set(KNST_X11_MISSING "")
            foreach(_mod IN LISTS KNST_X11_MODULES)
                string(MAKE_C_IDENTIFIER "KNST_PC_${_mod}" _prefix)
                pkg_check_modules(${_prefix} QUIET IMPORTED_TARGET GLOBAL ${_mod})
                if(${_prefix}_FOUND)
                    list(APPEND KNST_LINK_ITEMS PkgConfig::${_prefix})
                else()
                    list(APPEND KNST_X11_MISSING ${_mod})
                endif()
            endforeach()

            if(KNST_X11_MISSING)
                list(JOIN KNST_X11_MISSING ", " _missing_str)
                message(FATAL_ERROR
                    "Missing X11 dev packages: ${_missing_str}\n"
                    "  sudo apt install libx11-dev libx11-xcb-dev libxcb1-dev "
                    "libxcb-randr0-dev libxcb-keysyms1-dev libxcb-icccm4-dev "
                    "libxcb-util-dev libxcb-sync-dev libxext-dev libxfixes-dev "
                    "libxi-dev libxrandr-dev libxcursor-dev libxcb-xinput-dev")
            endif()
        endif()
    endif()
endif()

# =============================================================================
#  GPU backends
# =============================================================================
if(KNST_WINDOW_USE_OPENGL)
    list(APPEND KNST_DEFINITIONS KNST_WINDOW_USE_OPENGL)

    set(KNST_GLAD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/knst_window/opengl_content/glad_3_3")
    set(KNST_GLAD_C    "${KNST_GLAD_ROOT}/src/glad.c")
    set(KNST_GLAD_INC  "${KNST_GLAD_ROOT}/include")

    if(NOT EXISTS "${KNST_GLAD_C}")
        message(FATAL_ERROR
            "KNST_WINDOW_USE_OPENGL=ON but GLAD source is missing at:\n  ${KNST_GLAD_C}")
    endif()

    add_library(knst_glad STATIC "${KNST_GLAD_C}")
    target_include_directories(knst_glad PUBLIC
        $<BUILD_INTERFACE:${KNST_GLAD_INC}>
        $<INSTALL_INTERFACE:include/platform/knst_window/opengl_content/glad_3_3/include>)
    set_target_properties(knst_glad PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(knst_glad PRIVATE
        -fno-sanitize=address
        -fno-sanitize=undefined)

    if(ANDROID)
        target_link_libraries(knst_glad PUBLIC EGL GLESv2)
    elseif(WIN32)
        target_link_libraries(knst_glad PUBLIC opengl32)
    else()
        pkg_check_modules(KNST_PC_gl     REQUIRED IMPORTED_TARGET GLOBAL gl)
        pkg_check_modules(KNST_PC_egl    REQUIRED IMPORTED_TARGET GLOBAL egl)
        pkg_check_modules(KNST_PC_glesv2 REQUIRED IMPORTED_TARGET GLOBAL glesv2)
        target_link_libraries(knst_glad PUBLIC
            PkgConfig::KNST_PC_gl PkgConfig::KNST_PC_egl PkgConfig::KNST_PC_glesv2)
    endif()

    list(APPEND KNST_LINK_ITEMS knst_glad)
endif()

if(KNST_WINDOW_USE_VULKAN)
    list(APPEND KNST_DEFINITIONS KNST_WINDOW_USE_VULKAN)
    if(ANDROID)
        list(APPEND KNST_LINK_ITEMS vulkan)
    else()
        find_package(Vulkan QUIET)
        if(TARGET Vulkan::Vulkan)
            list(APPEND KNST_LINK_ITEMS Vulkan::Vulkan)
        else()
            message(FATAL_ERROR
                "KNST_WINDOW_USE_VULKAN=ON but Vulkan not found.\n"
                "  Debian/Ubuntu: sudo apt install libvulkan-dev")
        endif()
    endif()
endif()

if(NOT KNST_WINDOW_USE_OPENGL AND NOT KNST_WINDOW_USE_VULKAN)
    message(STATUS "Window backends: none (software rendering only)")
endif()

# =============================================================================
#  Network tier resolution (desktop only)
# =============================================================================
if(NOT ANDROID)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    if(NOT DEFINED PKG_CONFIG_FOUND AND NOT PkgConfig_FOUND)
        find_package(PkgConfig QUIET)
    endif()

    set(KNST_NET_TLS_OK FALSE)
    set(KNST_NET_H2_OK  FALSE)
    set(KNST_NET_H3_OK  FALSE)
    set(KNST_NET_DEFS   "")
    set(KNST_NET_LINK   "")
    set(KNST_NET_RPATH  "")
    set(KNST_QUICTLS_INCLUDE_DIR "")
    set(_openssl_link_items "")

    # ---- OpenSSL (system) ----
    if(NOT KNST_NETWORK_DISABLE_TLS)
        find_package(OpenSSL QUIET)
        if(OpenSSL_FOUND)
            set(KNST_NET_TLS_OK TRUE)
            list(APPEND KNST_NET_DEFS KNST_NETWORK_ENABLE_TLS)
            set(_openssl_link_items OpenSSL::SSL OpenSSL::Crypto)
            message(STATUS "  OpenSSL found: ${OPENSSL_VERSION}")
        else()
            message(STATUS "  OpenSSL not found — TLS disabled")
        endif()
    endif()

    # ---- nghttp2 (H2) ----
    if(KNST_NET_TLS_OK AND NOT KNST_NETWORK_DISABLE_H2)
        set(_nghttp2_target "")

        find_package(nghttp2 CONFIG QUIET HINTS "${KNST_NGHTTP2_ROOT}")
        if(TARGET nghttp2::nghttp2)
            set(_nghttp2_target nghttp2::nghttp2)
        endif()

        if(NOT _nghttp2_target AND PkgConfig_FOUND)
            pkg_check_modules(KNST_PC_NGHTTP2 QUIET IMPORTED_TARGET GLOBAL libnghttp2)
            if(TARGET PkgConfig::KNST_PC_NGHTTP2)
                set(_nghttp2_target PkgConfig::KNST_PC_NGHTTP2)
            endif()
        endif()

        if(NOT _nghttp2_target)
            find_path(KNST_NGHTTP2_INC NAMES nghttp2/nghttp2.h HINTS "${KNST_NGHTTP2_ROOT}")
            find_library(KNST_NGHTTP2_LIB NAMES nghttp2 HINTS "${KNST_NGHTTP2_ROOT}")
            if(KNST_NGHTTP2_INC AND KNST_NGHTTP2_LIB)
                if(NOT TARGET knst::nghttp2)
                    add_library(knst::nghttp2 UNKNOWN IMPORTED)
                    set_target_properties(knst::nghttp2 PROPERTIES
                        IMPORTED_LOCATION "${KNST_NGHTTP2_LIB}"
                        INTERFACE_INCLUDE_DIRECTORIES "${KNST_NGHTTP2_INC}")
                endif()
                set(_nghttp2_target knst::nghttp2)
            endif()
        endif()

                if(_nghttp2_target)
            set(KNST_NET_H2_OK TRUE)
            list(APPEND KNST_NET_DEFS KNST_NETWORK_ENABLE_H2)
            if(WIN32 AND KNST_NGHTTP2_ROOT)
                target_include_directories(KernelNucleusT INTERFACE
                    "${KNST_NGHTTP2_ROOT}/include")
                list(APPEND KNST_NET_LINK "${KNST_NGHTTP2_ROOT}/lib/libnghttp2.a")
            else()
                list(APPEND KNST_NET_LINK ${_nghttp2_target})
            endif()
        else()
            message(STATUS "  nghttp2 not found — HTTP/2 disabled "
                           "(sudo apt install libnghttp2-dev)")
        endif()
    endif()

        # ---- FAAD2 (AAC decode, optional) ----
    set(_faad2_target "")
    if(PkgConfig_FOUND)
        pkg_check_modules(KNST_PC_FAAD2 QUIET IMPORTED_TARGET GLOBAL faad2)
        if(TARGET PkgConfig::KNST_PC_FAAD2)
            set(_faad2_target PkgConfig::KNST_PC_FAAD2)
        endif()
    endif()
    if(NOT _faad2_target)
        find_path(KNST_FAAD2_INC NAMES neaacdec.h PATH_SUFFIXES faad2 include/faad2)
        find_library(KNST_FAAD2_LIB NAMES faad faad2)
        if(KNST_FAAD2_INC AND KNST_FAAD2_LIB)
            if(NOT TARGET knst::faad2)
                add_library(knst::faad2 UNKNOWN IMPORTED)
                set_target_properties(knst::faad2 PROPERTIES
                    IMPORTED_LOCATION "${KNST_FAAD2_LIB}"
                    INTERFACE_INCLUDE_DIRECTORIES "${KNST_FAAD2_INC}")
            endif()
            set(_faad2_target knst::faad2)
        endif()
    endif()
    if(_faad2_target)
        list(APPEND KNST_DEFINITIONS KNST_NETWORK_HAVE_FAAD2)
        list(APPEND KNST_LINK_ITEMS ${_faad2_target})
        message(STATUS "  FAAD2 found — AAC decode enabled")
    else()
        message(STATUS "  FAAD2 not found — AAC decode disabled "
                       "(apt install libfaad-dev / vcpkg install faad2)")
    endif()

    # ---- Vorbis (OGG Vorbis encode, optional) ----
    set(_vorbis_ok FALSE)
    set(_vorbis_link "")
    if(PkgConfig_FOUND)
        pkg_check_modules(KNST_PC_VORBIS QUIET IMPORTED_TARGET GLOBAL vorbis vorbisenc ogg)
        if(TARGET PkgConfig::KNST_PC_VORBIS)
            set(_vorbis_ok TRUE)
            set(_vorbis_link PkgConfig::KNST_PC_VORBIS)
        endif()
    endif()
    if(NOT _vorbis_ok)
        find_path(KNST_VORBIS_INC NAMES vorbis/vorbisenc.h)
        find_library(KNST_VORBIS_LIB NAMES vorbis)
        find_library(KNST_VORBISENC_LIB NAMES vorbisenc)
        find_library(KNST_OGG_LIB NAMES ogg)
        if(KNST_VORBIS_INC AND KNST_VORBIS_LIB AND KNST_VORBISENC_LIB AND KNST_OGG_LIB)
            set(_vorbis_ok TRUE)
            target_include_directories(KernelNucleusT INTERFACE "${KNST_VORBIS_INC}")
            set(_vorbis_link "${KNST_VORBIS_LIB}" "${KNST_VORBISENC_LIB}" "${KNST_OGG_LIB}")
        endif()
    endif()
    if(_vorbis_ok)
        list(APPEND KNST_DEFINITIONS KNST_NETWORK_HAVE_VORBIS)
        list(APPEND KNST_LINK_ITEMS ${_vorbis_link})
        message(STATUS "  Vorbis found — native OGG encode enabled")
    else()
        message(STATUS "  Vorbis not found — OGG encode relies on libsndfile "
                       "(apt install libvorbis-dev libogg-dev)")
    endif()

    # ---- H3 (quictls + ngtcp2 + nghttp3) ----
    if(KNST_NET_H2_OK AND NOT KNST_NETWORK_DISABLE_H3)

        set(_qroot "${KNST_QUICTLS_ROOT}")
        if(NOT _qroot AND DEFINED ENV{QUICTLS_ROOT})
            set(_qroot "$ENV{QUICTLS_ROOT}")
        endif()
        if(NOT _qroot)
            foreach(_cand /usr/local/quictls /opt/quictls "$ENV{HOME}/quictls")
                if(EXISTS "${_cand}/include/openssl/ssl.h")
                    set(_qroot "${_cand}")
                    break()
                endif()
            endforeach()
        endif()

        set(_quictls_ssl    "")
        set(_quictls_crypto "")
        set(_quictls_inc    "")
        if(_qroot AND EXISTS "${_qroot}/include/openssl/ssl.h")
            set(_quictls_inc "${_qroot}/include")
                        if(WIN32)
                # MSVC layouts: ssl.lib / libssl.lib under lib, lib/x64, lib/win64, lib/x86.
                # MinGW (MSYS2) quictls installs: lib64/libssl.a and lib64/libcrypto.a,
                # so also probe lib64 and accept the ".a" extension.
                foreach(_dir lib lib64 lib/x64 lib/win64 lib/x86)
                    if(EXISTS "${_qroot}/${_dir}/ssl.lib")
                        set(_quictls_ssl    "${_qroot}/${_dir}/ssl.lib")
                        set(_quictls_crypto "${_qroot}/${_dir}/libcrypto.lib")
                        break()
                    endif()
                    if(EXISTS "${_qroot}/${_dir}/libssl.lib")
                        set(_quictls_ssl    "${_qroot}/${_dir}/libssl.lib")
                        set(_quictls_crypto "${_qroot}/${_dir}/libcrypto.lib")
                        break()
                    endif()
                    if(EXISTS "${_qroot}/${_dir}/libssl.a")
                        set(_quictls_ssl    "${_qroot}/${_dir}/libssl.a")
                        set(_quictls_crypto "${_qroot}/${_dir}/libcrypto.a")
                        break()
                    endif()
                endforeach()
            else()
                foreach(_dir lib64 lib)
                    if(NOT _quictls_ssl AND EXISTS "${_qroot}/${_dir}/libssl.so")
                        set(_quictls_ssl    "${_qroot}/${_dir}/libssl.so")
                        set(_quictls_crypto "${_qroot}/${_dir}/libcrypto.so")
                    endif()
                    if(NOT _quictls_ssl AND EXISTS "${_qroot}/${_dir}/libssl.a")
                        set(_quictls_ssl    "${_qroot}/${_dir}/libssl.a")
                        set(_quictls_crypto "${_qroot}/${_dir}/libcrypto.a")
                    endif()
                endforeach()
            endif()
            if(NOT _quictls_ssl OR NOT _quictls_crypto)
                message(STATUS "  WARNING: quictls headers found at ${_qroot} but libssl/libcrypto missing")
                set(_quictls_inc "")
            endif()
        endif()

        set(_ngtcp2_target "")
        set(_ngtcp2_crypto_target "")
        set(_nghttp3_target "")

        find_package(ngtcp2 CONFIG QUIET HINTS "${KNST_NGTCP2_ROOT}")
        if(TARGET ngtcp2::ngtcp2)
            set(_ngtcp2_target ngtcp2::ngtcp2)
        endif()
        if(NOT _ngtcp2_target AND PkgConfig_FOUND)
            pkg_check_modules(KNST_PC_NGTCP2 QUIET IMPORTED_TARGET GLOBAL libngtcp2)
            if(TARGET PkgConfig::KNST_PC_NGTCP2)
                set(_ngtcp2_target PkgConfig::KNST_PC_NGTCP2)
            endif()
        endif()

        foreach(_t ngtcp2_crypto_quictls::ngtcp2_crypto_quictls
                  ngtcp2::ngtcp2_crypto_quictls
                  ngtcp2_crypto_quictls)
            if(TARGET ${_t})
                set(_ngtcp2_crypto_target ${_t})
                break()
            endif()
        endforeach()
        if(NOT _ngtcp2_crypto_target AND PkgConfig_FOUND)
            pkg_check_modules(KNST_PC_NGTCP2_Q QUIET IMPORTED_TARGET GLOBAL libngtcp2_crypto_quictls)
            if(TARGET PkgConfig::KNST_PC_NGTCP2_Q)
                set(_ngtcp2_crypto_target PkgConfig::KNST_PC_NGTCP2_Q)
            endif()
        endif()

        find_package(nghttp3 CONFIG QUIET HINTS "${KNST_NGHTTP3_ROOT}")
        if(TARGET nghttp3::nghttp3)
            set(_nghttp3_target nghttp3::nghttp3)
        endif()
        if(NOT _nghttp3_target AND PkgConfig_FOUND)
            pkg_check_modules(KNST_PC_NGHTTP3 QUIET IMPORTED_TARGET GLOBAL libnghttp3)
            if(TARGET PkgConfig::KNST_PC_NGHTTP3)
                set(_nghttp3_target PkgConfig::KNST_PC_NGHTTP3)
            endif()
        endif()

        if(_quictls_ssl AND _quictls_crypto AND _quictls_inc AND
           _ngtcp2_target AND _ngtcp2_crypto_target AND _nghttp3_target)
            set(KNST_NET_H3_OK TRUE)
            set(KNST_QUICTLS_INCLUDE_DIR "${_quictls_inc}")
            list(APPEND KNST_NET_DEFS KNST_NETWORK_ENABLE_H3)
            if(WIN32 AND KNST_NGTCP2_ROOT AND KNST_NGHTTP2_ROOT AND KNST_NGHTTP3_ROOT)
                # Static .a paths bypass the imported targets, so add their
                # include directories explicitly — otherwise ngtcp2/ngtcp2.h
                # and ngtcp2/ngtcp2_crypto_quictls.h are not found.
                target_include_directories(KernelNucleusT INTERFACE
                    "${KNST_NGTCP2_ROOT}/include"
                    "${KNST_NGHTTP2_ROOT}/include"
                    "${KNST_NGHTTP3_ROOT}/include")
                # Static linking: suppress __declspec(dllimport) in headers.
                # Without these, every call becomes `__imp_*` and the linker
                # cannot resolve it against the plain static symbols.
                target_compile_definitions(KernelNucleusT INTERFACE
                    NGTCP2_STATICLIB
                    NGHTTP3_STATICLIB
                    NGHTTP2_STATICLIB)
                list(APPEND KNST_NET_LINK
                    "${KNST_NGTCP2_ROOT}/lib/libngtcp2.a"
                    "${KNST_NGTCP2_ROOT}/lib/libngtcp2_crypto_quictls.a"
                    "${KNST_NGHTTP3_ROOT}/lib/libnghttp3.a"
                    "${KNST_NGHTTP2_ROOT}/lib/libnghttp2.a"
                    "${_quictls_ssl}" "${_quictls_crypto}")
            else()
                list(APPEND KNST_NET_LINK
                    ${_ngtcp2_target} ${_ngtcp2_crypto_target} ${_nghttp3_target}
                    "${_quictls_ssl}" "${_quictls_crypto}")
            endif()
            if(NOT WIN32)
                list(APPEND KNST_NET_RPATH "${_qroot}/lib" "${_qroot}/lib64")
            endif()
            message(STATUS "  quictls found: ${_qroot}")
        else()
            if(NOT _qroot)
                message(STATUS "  quictls not found — HTTP/3 disabled.\n"
                    "    Hint: -DKNST_QUICTLS_ROOT=/path/to/quictls")
            else()
                message(STATUS "  H3 disabled — missing dependency "
                    "(ngtcp2=${_ngtcp2_target} "
                    "ngtcp2_crypto=${_ngtcp2_crypto_target} "
                    "nghttp3=${_nghttp3_target})")
            endif()
        endif()
    endif()

    # NEVER link both system OpenSSL and quictls — ABI clash.
    if(NOT KNST_NET_H3_OK)
        list(APPEND KNST_NET_LINK ${_openssl_link_items})
    endif()

    if(KNST_NET_H3_OK)
        message(STATUS "Network tier: TLS + H2 + H3  (quictls + nghttp2 + ngtcp2 + nghttp3)")
    elseif(KNST_NET_H2_OK)
        message(STATUS "Network tier: TLS + H2       (system OpenSSL + nghttp2)")
    elseif(KNST_NET_TLS_OK)
        message(STATUS "Network tier: TLS + H1       (system OpenSSL)")
    else()
        message(STATUS "Network tier: disabled       (no OpenSSL / H2 / H3)")
    endif()

        list(APPEND KNST_DEFINITIONS ${KNST_NET_DEFS})
    list(APPEND KNST_LINK_ITEMS  ${KNST_NET_LINK})

    # Static link order fix (MinGW + quictls):
    # libcrypto.a/libssl.a reference Winsock / crypt32 / bcrypt symbols, so
    # those libraries must appear AFTER quictls on the link line. Unix-style
    # linkers resolve symbols left-to-right only. Re-append here.
    if(WIN32 AND KNST_NET_H3_OK)
        list(APPEND KNST_LINK_ITEMS
            ws2_32 crypt32 bcrypt userenv advapi32 iphlpapi)
    endif()

    if(KNST_QUICTLS_INCLUDE_DIR)
        target_include_directories(KernelNucleusT INTERFACE "${KNST_QUICTLS_INCLUDE_DIR}")
    endif()

    if(KNST_NET_RPATH AND NOT WIN32)
        list(APPEND CMAKE_BUILD_RPATH ${KNST_NET_RPATH})
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Per-target warning / sanitizer configuration
# ─────────────────────────────────────────────────────────────────────────────
function(knst_configure_target TARGET_NAME)
if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE
        /W4 /utf-8 /permissive-
        /wd4267   # size_t -> uint32_t
        /wd4244   # type narrowing conversion
        /wd4100   # unreferenced parameter
        /wd4068   # unknown pragma (GCC pragmas)
        /wd4996   # deprecated POSIX names
        /wd4099   # struct/class forward declaration mismatch
        /wd4310   # cast truncates constant value
        /wd4091   # unused typedef
        /wd4005   # macro redefinition
        /wd4200   # zero-sized array (flexible array member, C99 pattern)
    )
        if(KNST_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET_NAME} PRIVATE /WX)
        endif()
        if(KNST_ENABLE_SANITIZERS)
            target_compile_options(${TARGET_NAME} PRIVATE /fsanitize=address)
        endif()
        return()
    endif()

    target_compile_options(${TARGET_NAME} PRIVATE
        -Wall -Wextra -Wshadow -Wno-undef
        -Wno-pedantic -Wno-conversion -Wno-float-conversion
        -Wno-unused-result -Wno-unused-parameter
        $<$<CXX_COMPILER_ID:GNU>:-Wno-format-truncation>
        -Wno-missing-field-initializers -Wno-unknown-pragmas
        -Wno-cast-function-type -Wno-sign-compare
        -Wno-delete-non-virtual-dtor -Wno-mismatched-tags
                -Wno-misleading-indentation
        -Wno-array-bounds
        $<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overflow>
        $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>)

    if(KNST_WARNINGS_AS_ERRORS)
        target_compile_options(${TARGET_NAME} PRIVATE -Werror)
    endif()

    if(KNST_ENABLE_SANITIZERS AND NOT MINGW)
        target_compile_options(${TARGET_NAME} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${TARGET_NAME} PRIVATE
            -fsanitize=address,undefined)
    endif()

    if(MINGW)
        target_link_options(${TARGET_NAME} PRIVATE
            -static
            -static-libgcc
            -static-libstdc++)
    endif()
endfunction()

# ─────────────────────────────────────────────────────────────────────────────
#  Resolve KNST_APP_SOURCE (relative paths are resolved to this CMakeLists)
# ─────────────────────────────────────────────────────────────────────────────
set(KNST_APP_SOURCE_RESOLVED "")
if(KNST_APP_SOURCE)
    if(IS_ABSOLUTE "${KNST_APP_SOURCE}")
        set(KNST_APP_SOURCE_RESOLVED "${KNST_APP_SOURCE}")
    else()
        set(KNST_APP_SOURCE_RESOLVED "${CMAKE_CURRENT_SOURCE_DIR}/${KNST_APP_SOURCE}")
    endif()
    if(NOT EXISTS "${KNST_APP_SOURCE_RESOLVED}")
        message(FATAL_ERROR "KNST_APP_SOURCE not found: ${KNST_APP_SOURCE_RESOLVED}")
    endif()
endif()

# =============================================================================
#  The single executable — built ONLY from KNST_APP_SOURCE
# =============================================================================
if(KNST_APP_SOURCE_RESOLVED)

    if(ANDROID)
        # --- Android shared library ---
        if(DEFINED ANDROID_NDK)
            set(KNST_NDK_ROOT "${ANDROID_NDK}")
        elseif(DEFINED CMAKE_ANDROID_NDK)
            set(KNST_NDK_ROOT "${CMAKE_ANDROID_NDK}")
        elseif(DEFINED ENV{ANDROID_NDK_HOME})
            set(KNST_NDK_ROOT "$ENV{ANDROID_NDK_HOME}")
        else()
            message(FATAL_ERROR "Android NDK not found.")
        endif()

        set(KNST_NATIVE_APP_GLUE_DIR "${KNST_NDK_ROOT}/sources/android/native_app_glue")
        add_library(knst_native_app_glue STATIC
            "${KNST_NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
        target_include_directories(knst_native_app_glue PUBLIC "${KNST_NATIVE_APP_GLUE_DIR}")
        set_target_properties(knst_native_app_glue PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_compile_options(knst_native_app_glue PRIVATE
            -fno-sanitize=address -fno-sanitize=undefined)

        add_library(knst_app SHARED "${KNST_APP_SOURCE_RESOLVED}")
        target_link_libraries(knst_app PRIVATE
            KernelNucleusT knst_native_app_glue camera2ndk mediandk)

        if(TARGET nghttp2_static)
            target_link_libraries(knst_app PRIVATE nghttp2_static)
            # FetchContent + NDK: nghttp2_static's INTERFACE_INCLUDE_DIRECTORIES
            # don't propagate to consumers. Add them explicitly so the
            # `#include <nghttp2/nghttp2.h>` in knst_network_h2.hpp resolves.
            if(DEFINED nghttp2_SOURCE_DIR)
                target_include_directories(knst_app PRIVATE
                    "${nghttp2_SOURCE_DIR}/lib/includes"
                    "${nghttp2_BINARY_DIR}/lib/includes")
            endif()
        elseif(TARGET nghttp2::nghttp2)
            target_link_libraries(knst_app PRIVATE nghttp2::nghttp2)
            if(DEFINED nghttp2_SOURCE_DIR)
                target_include_directories(knst_app PRIVATE
                    "${nghttp2_SOURCE_DIR}/lib/includes")
            endif()
        endif()
                if(TARGET nghttp3_static)
            target_link_libraries(knst_app PRIVATE nghttp3_static)
            # FetchContent + NDK: nghttp3_static's INTERFACE_INCLUDE_DIRECTORIES
            # don't propagate on Android. Add them explicitly.
            if(DEFINED nghttp3_SOURCE_DIR)
                target_include_directories(knst_app PRIVATE
                    "${nghttp3_SOURCE_DIR}/lib/includes"
                    "${nghttp3_BINARY_DIR}/lib/includes")
            else()
                target_include_directories(knst_app PRIVATE
                    "${CMAKE_BINARY_DIR}/_deps/nghttp3-src/lib/includes"
                    "${CMAKE_BINARY_DIR}/_deps/nghttp3-build/lib/includes")
            endif()
        elseif(TARGET nghttp3::nghttp3)
            target_link_libraries(knst_app PRIVATE nghttp3::nghttp3)
            if(DEFINED nghttp3_SOURCE_DIR)
                target_include_directories(knst_app PRIVATE
                    "${nghttp3_SOURCE_DIR}/lib/includes"
                    "${nghttp3_BINARY_DIR}/lib/includes")
            endif()
        endif()

        if(TARGET ngtcp2_static)
            target_link_libraries(knst_app PRIVATE ngtcp2_static)
            if(DEFINED ngtcp2_SOURCE_DIR)
                target_include_directories(knst_app PRIVATE
                    "${ngtcp2_SOURCE_DIR}/lib/includes"
                    "${ngtcp2_BINARY_DIR}/lib/includes")
            endif()
        elseif(TARGET ngtcp2::ngtcp2)
            target_link_libraries(knst_app PRIVATE ngtcp2::ngtcp2)
            if(DEFINED ngtcp2_SOURCE_DIR)
                target_include_directories(knst_app PRIVATE
                    "${ngtcp2_SOURCE_DIR}/lib/includes"
                    "${ngtcp2_BINARY_DIR}/lib/includes")
            endif()
        endif()

        if(TARGET ngtcp2_crypto_quictls_static)
            target_link_libraries(knst_app PRIVATE ngtcp2_crypto_quictls_static)
            if(DEFINED ngtcp2_SOURCE_DIR)
                target_include_directories(knst_app PRIVATE
                    "${ngtcp2_SOURCE_DIR}/crypto/includes"
                    "${ngtcp2_BINARY_DIR}/crypto/includes")
            endif()
        elseif(TARGET ngtcp2_crypto_quictls::ngtcp2_crypto_quictls)
            target_link_libraries(knst_app PRIVATE ngtcp2_crypto_quictls::ngtcp2_crypto_quictls)
            if(DEFINED ngtcp2_SOURCE_DIR)
                target_include_directories(knst_app PRIVATE
                    "${ngtcp2_SOURCE_DIR}/crypto/includes"
                    "${ngtcp2_BINARY_DIR}/crypto/includes")
            endif()
        endif()

        knst_configure_target(knst_app)
        target_link_options(knst_app PRIVATE
            -u ANativeActivity_onCreate
            -Wl,-z,max-page-size=16384)
    else()
        # --- Desktop executable ---
        add_executable(knst_app "${KNST_APP_SOURCE_RESOLVED}")
        target_link_libraries(knst_app PRIVATE KernelNucleusT)
        knst_configure_target(knst_app)
    endif()

else()
    message(STATUS "No KNST_APP_SOURCE provided — headers only (no executable will be built).")
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Dedupe + apply accumulated flags to the INTERFACE target
# ─────────────────────────────────────────────────────────────────────────────
list(REMOVE_DUPLICATES KNST_DEFINITIONS)
# Do NOT dedupe KNST_LINK_ITEMS — static linking on MinGW requires that
# winsock/crypt32/bcrypt/userenv appear AFTER quictls's libcrypto.a,
# so the second copy at the end of the list must survive.
target_compile_definitions(KernelNucleusT INTERFACE ${KNST_DEFINITIONS})
target_link_libraries(KernelNucleusT INTERFACE ${KNST_LINK_ITEMS})

# =============================================================================
#  Installation + package export
# =============================================================================
if(KNST_IS_TOP_LEVEL)
    include(CMakePackageConfigHelpers)
    include(GNUInstallDirs)

    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.c")

    install(TARGETS KernelNucleusT
        EXPORT KernelNucleusTTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    if(TARGET knst_glad)
        install(TARGETS knst_glad
            EXPORT KernelNucleusTTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
    if(TARGET knst_wayland_protocols)
        install(TARGETS knst_wayland_protocols
            EXPORT KernelNucleusTTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()

    install(EXPORT KernelNucleusTTargets
        FILE KernelNucleusTTargets.cmake
        NAMESPACE KernelNucleusT::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/KernelNucleusT)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/KernelNucleusTConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/KernelNucleusTConfig.cmake"
"include(CMakeFindDependencyMacro)
if(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_dependency(Threads)
endif()
include(\"\${CMAKE_CURRENT_LIST_DIR}/KernelNucleusTTargets.cmake\")
")

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/KernelNucleusTConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/KernelNucleusTConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/KernelNucleusT)

    if(TARGET knst_app)
        install(TARGETS knst_app
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endif()

# =============================================================================
#  Summary
# =============================================================================
if(KNST_IS_TOP_LEVEL)
    list(JOIN KNST_DEFINITIONS "\n                  " KNST_DEFINITIONS_STR)

    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " KernelNucleusT v${PROJECT_VERSION}")
    message(STATUS "========================================")
    message(STATUS " Platform:       ${KNST_PLATFORM_DETAIL}")
    message(STATUS " Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    if(CMAKE_BUILD_TYPE)
        message(STATUS " Build type:     ${CMAKE_BUILD_TYPE}")
    endif()
    message(STATUS " C++ standard:   C++${CMAKE_CXX_STANDARD}")
    message(STATUS " OpenGL:         ${KNST_WINDOW_USE_OPENGL}")
    message(STATUS " Vulkan:         ${KNST_WINDOW_USE_VULKAN}")
    message(STATUS " Network:        TLS=${KNST_NET_TLS_OK} H2=${KNST_NET_H2_OK} H3=${KNST_NET_H3_OK}")
    if(KNST_APP_SOURCE_RESOLVED)
        message(STATUS " App source:     ${KNST_APP_SOURCE_RESOLVED}")
    else()
        message(STATUS " App source:     (none — headers only)")
    endif()
    message(STATUS " Sanitizers:     ${KNST_ENABLE_SANITIZERS}")
    message(STATUS "----------------------------------------")
    message(STATUS " Definitions:    ${KNST_DEFINITIONS_STR}")
    message(STATUS "----------------------------------------")
    message(STATUS " Source:         ${CMAKE_SOURCE_DIR}")
    message(STATUS " Build:          ${CMAKE_BINARY_DIR}")
    message(STATUS "========================================")
    message(STATUS "")
endif()