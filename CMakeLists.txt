# =============================================================================
#  KernelNucleusT — CMake build configuration
# =============================================================================
#
#  Overview
#  --------
#   Header-only C++20 window / GUI framework with optional GPU backends.
#   Every compile definition, include path and system library is propagated
#   through the INTERFACE target `KernelNucleusT::KernelNucleusT`.
#
#  Supported platforms
#  -------------------
#   Windows  — Win32 / Win64 (MSVC, MinGW, Clang)
#   Linux    — X11 / Wayland / Dual (runtime-selected)
#   Android  — NDK (API 24+)
#
#  GPU backends (optional, can be combined)
#  ----------------------------------------
#   -DKNST_WINDOW_USE_OPENGL=ON   GLAD + EGL/WGL
#   -DKNST_WINDOW_USE_VULKAN=ON   Vulkan loader
#
#  Quick start
#  -----------
#   Linux X11:      cmake -B build -DKNST_LINUX_PLATFORM=X11 -DKNST_WINDOW_USE_OPENGL=ON
#   Linux Wayland:  cmake -B build -DKNST_LINUX_PLATFORM=Wayland -DKNST_WINDOW_USE_OPENGL=ON
#   Linux Dual:     cmake -B build -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON
#   Windows:        cmake -B build -DKNST_WINDOW_USE_OPENGL=ON
#   Android:        cmake -B build \
#                     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#                     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
#
#  Consuming from another project
#  ------------------------------
#   find_package(KernelNucleusT REQUIRED)
#   target_link_libraries(my_app PRIVATE KernelNucleusT::KernelNucleusT)
# =============================================================================

cmake_minimum_required(VERSION 3.20)

project(KernelNucleusT
    VERSION     1.0.0
    DESCRIPTION "KernelNucleusT cross-platform window / GUI framework"
    LANGUAGES   C CXX
)

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
option(KNST_BUILD_EXAMPLES      "Build the example applications"                 OFF)
option(KNST_ENABLE_SANITIZERS   "Enable AddressSanitizer + UBSan (debug builds)" OFF)
option(KNST_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors"              OFF)
option(KNST_STATIC_MSVC_RUNTIME "Use the static MSVC runtime (/MT, /MTd)"        ON)

# GPU backends — independent, may be combined
option(KNST_WINDOW_USE_OPENGL "Enable the OpenGL backend (GLAD + EGL/WGL)" OFF)
option(KNST_WINDOW_USE_VULKAN "Enable the Vulkan backend"                  OFF)

# Linux dual-backend mode (X11 + Wayland in a single binary, runtime-selected)
option(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND
       "Compile both X11 and Wayland backends; select one at runtime" OFF)

set(KNST_APP_SOURCE "" CACHE FILEPATH
    "Optional: absolute or relative path to a .cpp file built as 'knst_app'")

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

# Accumulators — applied to the INTERFACE target at the very end
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

    set(KNST_PLATFORM_DETAIL "Android (API ${KNST_ANDROID_API}, ABI ${ANDROID_ABI})")

# =============================================================================
#  Platform: Windows
# =============================================================================
elseif(KNST_PLATFORM STREQUAL "Windows")
    list(APPEND KNST_DEFINITIONS
        KNST_USING_PLATFORM_WINDOWS
        NOMINMAX
        OEMRESOURCE
        _CRT_SECURE_NO_WARNINGS)

    list(APPEND KNST_LINK_ITEMS
        gdi32 user32 shell32 shcore ole32 oleaut32 uuid
        hid bthprops setupapi strmiids iphlpapi ws2_32 wbemuuid
        psapi dxgi pdh advapi32)

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

    # ─────────────────────────────────────────────────────────────────────────
    #  Helper: knst_setup_wayland_protocols()
    #
    #  Generates all required Wayland protocol sources (.c) and headers (.h)
    #  at build time using wayland-scanner. XML files are located
    #  automatically under stable / staging / unstable directories so the
    #  script works across different wayland-protocols versions.
    #
    #  Required variables in caller scope:
    #    KNST_WAYLAND_SCANNER      — path to the wayland-scanner executable
    #    KNST_WP_DIR               — wayland-protocols XML root directory
    #    KNST_WAYLAND_PROTOCOL_DIR — destination for generated .c / .h files
    #    PkgConfig::KNST_WAYLAND   — imported wayland-client target
    #
    #  Creates: knst_wayland_protocols (STATIC library)
    # ─────────────────────────────────────────────────────────────────────────
    function(knst_setup_wayland_protocols)
        # Snapshot every XML once — much faster than resolving per protocol
        file(GLOB_RECURSE _all_xmls "${KNST_WP_DIR}/*.xml")

        # Resolve a single XML basename to its full path
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

        # Protocol list: "<xml-basename>|<output-prefix>"
        #
        # NOTE: cursor-shape-v1.xml references zwp_tablet_tool_v2_interface
        #       (used for stylus-aware cursors), so tablet-v2 must be
        #       generated alongside it.
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
            "tablet-v2.xml|tablet-v2"
        )

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
                "No Wayland protocols found.\n"
                "Install the wayland-protocols package:\n"
                "  Debian/Ubuntu: sudo apt install wayland-protocols\n"
                "  Fedora       : sudo dnf install wayland-protocols-devel\n"
                "  Arch         : sudo pacman -S wayland-protocols")
        endif()

        # The .h files are listed as sources so CMake knows they are
        # produced as a side-effect of the same custom commands.
        add_library(knst_wayland_protocols STATIC ${_sources} ${_headers})

        set_target_properties(knst_wayland_protocols PROPERTIES
            LINKER_LANGUAGE          C
            C_STANDARD               11
            C_STANDARD_REQUIRED      ON
            POSITION_INDEPENDENT_CODE ON)

        target_include_directories(knst_wayland_protocols PUBLIC
            $<BUILD_INTERFACE:${KNST_WAYLAND_PROTOCOL_DIR}>
            $<INSTALL_INTERFACE:include/platform/linux/wayland/protocol_files>)

        target_link_libraries(knst_wayland_protocols PUBLIC PkgConfig::KNST_WAYLAND)

        message(STATUS "Wayland protocols: ${_generated_count} will be generated")
    endfunction()

    # ─────────────────────────────────────────────────────────────────────────
    #  Linux / Dual-backend mode
    # ─────────────────────────────────────────────────────────────────────────
    if(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
        message(STATUS "Linux window backend: DUAL (X11 + Wayland, runtime-selected)")
        set(KNST_PLATFORM_DETAIL "Linux (X11 + Wayland, runtime-selected)")

        list(APPEND KNST_DEFINITIONS
            KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND
            KNST_USING_LINUX_PLATFORM_X11
            KNST_USING_LINUX_PLATFORM_WAYLAND)

        # ---- X11 dependencies ---------------------------------------------
        set(KNST_X11_MODULES
            x11 x11-xcb xcb xcb-randr xcb-keysyms xcb-icccm xcb-util
            xext xfixes xcb-sync xi xrandr xcursor xcb-xinput)
        foreach(_mod IN LISTS KNST_X11_MODULES)
            string(MAKE_C_IDENTIFIER "KNST_PC_${_mod}" _prefix)
            pkg_check_modules(${_prefix} REQUIRED IMPORTED_TARGET GLOBAL ${_mod})
            list(APPEND KNST_LINK_ITEMS PkgConfig::${_prefix})
        endforeach()

        # ---- Wayland dependencies -----------------------------------------
        pkg_check_modules(KNST_WAYLAND REQUIRED IMPORTED_TARGET GLOBAL
            wayland-client wayland-cursor xkbcommon)
        list(APPEND KNST_LINK_ITEMS PkgConfig::KNST_WAYLAND)

        # wayland-egl is mandatory for the OpenGL backend, optional otherwise
        if(KNST_WINDOW_USE_OPENGL)
            pkg_check_modules(KNST_WAYLAND_EGL REQUIRED IMPORTED_TARGET GLOBAL wayland-egl)
        else()
            pkg_check_modules(KNST_WAYLAND_EGL QUIET    IMPORTED_TARGET GLOBAL wayland-egl)
        endif()
        if(TARGET PkgConfig::KNST_WAYLAND_EGL)
            list(APPEND KNST_LINK_ITEMS PkgConfig::KNST_WAYLAND_EGL)
        endif()

        # ---- wayland-scanner ----------------------------------------------
        find_program(KNST_WAYLAND_SCANNER NAMES wayland-scanner
                     HINTS /usr/bin /usr/local/bin)
        if(NOT KNST_WAYLAND_SCANNER)
            message(FATAL_ERROR
                "wayland-scanner not found.\n"
                "  Debian/Ubuntu: sudo apt install libwayland-bin")
        endif()

        # ---- wayland-protocols XML root -----------------------------------
        pkg_check_modules(KNST_WAYLAND_PROTOCOLS QUIET wayland-protocols)
        if(KNST_WAYLAND_PROTOCOLS_FOUND)
            pkg_get_variable(KNST_WP_DIR wayland-protocols pkgdatadir)
        endif()
        if(NOT KNST_WP_DIR)
            set(KNST_WP_DIR "/usr/share/wayland-protocols")
        endif()
        message(STATUS "wayland-protocols directory: ${KNST_WP_DIR}")

        # ---- Generate protocols -------------------------------------------
        set(KNST_WAYLAND_PROTOCOL_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/linux/wayland/protocol_files")
        file(MAKE_DIRECTORY "${KNST_WAYLAND_PROTOCOL_DIR}")

        knst_setup_wayland_protocols()

        # ⚠️ Mark generated files as build artifacts. They are listed in
        #    .gitignore so `git status` stays clean. The source tree is
        #    still used as the output location because the C++ side uses
        #    relative includes (`../linux/wayland/protocol_files/...`)
        #    which only resolve inside the source tree.
        file(GLOB _knst_generated_protos CONFIGURE_DEPENDS
            "${KNST_WAYLAND_PROTOCOL_DIR}/*-protocol.c"
            "${KNST_WAYLAND_PROTOCOL_DIR}/*-client-protocol.h")
        if(_knst_generated_protos)
            set_source_files_properties(${_knst_generated_protos}
                PROPERTIES GENERATED TRUE)
        endif()

        list(APPEND KNST_LINK_ITEMS knst_wayland_protocols)

    else()
        # ─────────────────────────────────────────────────────────────────
        #  Single-backend mode — X11 or Wayland
        # ─────────────────────────────────────────────────────────────────
        string(TOUPPER "${KNST_LINUX_PLATFORM}" KNST_LINUX_PLATFORM_UC)
        if(NOT KNST_LINUX_PLATFORM_UC MATCHES "^(X11|WAYLAND)$")
            message(FATAL_ERROR
                "KNST_LINUX_PLATFORM must be 'X11' or 'Wayland'. "
                "Got: '${KNST_LINUX_PLATFORM}'")
        endif()

        # ---- Single / Wayland ---------------------------------------------
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

            find_program(KNST_WAYLAND_SCANNER NAMES wayland-scanner
                         HINTS /usr/bin /usr/local/bin)
            if(NOT KNST_WAYLAND_SCANNER)
                message(FATAL_ERROR
                    "wayland-scanner not found.\n"
                    "  Debian/Ubuntu: sudo apt install libwayland-bin")
            endif()

            pkg_check_modules(KNST_WAYLAND_PROTOCOLS QUIET wayland-protocols)
            if(KNST_WAYLAND_PROTOCOLS_FOUND)
                pkg_get_variable(KNST_WP_DIR wayland-protocols pkgdatadir)
            endif()
            if(NOT KNST_WP_DIR)
                set(KNST_WP_DIR "/usr/share/wayland-protocols")
            endif()
            message(STATUS "wayland-protocols directory: ${KNST_WP_DIR}")

            set(KNST_WAYLAND_PROTOCOL_DIR
                "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/linux/wayland/protocol_files")
            file(MAKE_DIRECTORY "${KNST_WAYLAND_PROTOCOL_DIR}")

            knst_setup_wayland_protocols()

            list(APPEND KNST_LINK_ITEMS knst_wayland_protocols)
            if(TARGET PkgConfig::KNST_WAYLAND_EGL)
                list(APPEND KNST_LINK_ITEMS PkgConfig::KNST_WAYLAND_EGL)
            endif()

        # ---- Single / X11 -------------------------------------------------
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
                    "Missing X11 development packages: ${_missing_str}\n"
                    "Debian/Ubuntu/Pop!_OS:\n"
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

# ─────────────────────────────────────────────────────────────────────────────
#  OpenGL backend  (GLAD + platform EGL/WGL)
# ─────────────────────────────────────────────────────────────────────────────
if(KNST_WINDOW_USE_OPENGL)
    list(APPEND KNST_DEFINITIONS KNST_WINDOW_USE_OPENGL)

    set(KNST_GLAD_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/knst_window/opengl_content/glad_3_3")
    set(KNST_GLAD_C    "${KNST_GLAD_ROOT}/src/glad.c")
    set(KNST_GLAD_INC  "${KNST_GLAD_ROOT}/include")

    if(NOT EXISTS "${KNST_GLAD_C}")
        message(FATAL_ERROR
            "KNST_WINDOW_USE_OPENGL=ON but GLAD source is missing.\n"
            "Expected: ${KNST_GLAD_C}\n"
            "Download GLAD 3.3 Core from https://glad.dav1d.de/ and place it under:\n"
            "  include/platform/knst_window/opengl_content/glad_3_3/")
    endif()

    add_library(knst_glad STATIC "${KNST_GLAD_C}")

    # Absolute paths must be wrapped so install(EXPORT) can rewrite them
    target_include_directories(knst_glad PUBLIC
        $<BUILD_INTERFACE:${KNST_GLAD_INC}>
        $<INSTALL_INTERFACE:include/platform/knst_window/opengl_content/glad_3_3/include>)

    set_target_properties(knst_glad PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # glad.c is third-party code — sanitizer instrumentation isn't needed.
    target_compile_options(knst_glad PRIVATE
        -fno-sanitize=address
        -fno-sanitize=undefined)

    if(ANDROID)
        # The NDK ships EGL and GLESv2 — no extra discovery required
        target_link_libraries(knst_glad PUBLIC EGL GLESv2)
        message(STATUS "OpenGL backend: enabled (Android, EGL + GLESv2)")

    elseif(KNST_PLATFORM STREQUAL "Windows")
        # WGL lives in opengl32.dll
        target_link_libraries(knst_glad PUBLIC opengl32)
        message(STATUS "OpenGL backend: enabled (Windows, opengl32)")

    else()
        # Linux (X11 / Wayland): GL + EGL + GLESv2 via pkg-config
        pkg_check_modules(KNST_PC_gl     REQUIRED IMPORTED_TARGET GLOBAL gl)
        pkg_check_modules(KNST_PC_egl    REQUIRED IMPORTED_TARGET GLOBAL egl)
        pkg_check_modules(KNST_PC_glesv2 REQUIRED IMPORTED_TARGET GLOBAL glesv2)

        target_link_libraries(knst_glad PUBLIC
            PkgConfig::KNST_PC_gl
            PkgConfig::KNST_PC_egl
            PkgConfig::KNST_PC_glesv2)

        message(STATUS "OpenGL backend: enabled (Linux, gl + egl + glesv2)")
    endif()

    list(APPEND KNST_LINK_ITEMS knst_glad)
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Vulkan backend
# ─────────────────────────────────────────────────────────────────────────────
if(KNST_WINDOW_USE_VULKAN)
    list(APPEND KNST_DEFINITIONS KNST_WINDOW_USE_VULKAN)

    if(ANDROID)
        if(KNST_ANDROID_API GREATER 0 AND KNST_ANDROID_API LESS 24)
            message(FATAL_ERROR
                "Vulkan requires Android API 24+. Current: ${KNST_ANDROID_API}.\n"
                "Use -DANDROID_PLATFORM=android-24 or newer.")
        endif()
        list(APPEND KNST_LINK_ITEMS vulkan)
        message(STATUS "Vulkan backend: enabled (Android NDK)")

    else()
        find_package(Vulkan QUIET)
        if(TARGET Vulkan::Vulkan)
            list(APPEND KNST_LINK_ITEMS Vulkan::Vulkan)
            message(STATUS "Vulkan backend: enabled (${Vulkan_LIBRARY})")
        else()
            if(WIN32)
                set(_vk_hint
                    "Install the Vulkan SDK from https://vulkan.lunarg.com/ and "
                    "make sure VULKAN_SDK is set (restart the terminal).")
            else()
                set(_vk_hint
                    "Debian/Ubuntu: sudo apt install libvulkan-dev vulkan-tools\n"
                    "Fedora       : sudo dnf install vulkan-loader-devel vulkan-headers\n"
                    "Arch         : sudo pacman -S vulkan-headers vulkan-icd-loader")
            endif()
            message(FATAL_ERROR
                "KNST_WINDOW_USE_VULKAN=ON but Vulkan was not found.\n${_vk_hint}")
        endif()
    endif()
endif()

if(NOT KNST_WINDOW_USE_OPENGL AND NOT KNST_WINDOW_USE_VULKAN)
    message(STATUS "Window backends: none (software rendering only)")
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Apply accumulated flags to the INTERFACE target
# ─────────────────────────────────────────────────────────────────────────────
target_compile_definitions(KernelNucleusT INTERFACE ${KNST_DEFINITIONS})
target_link_libraries(KernelNucleusT INTERFACE ${KNST_LINK_ITEMS})

# ─────────────────────────────────────────────────────────────────────────────
#  Per-target warning / sanitizer configuration
#
#  Applied only to targets built by this project (apps and examples),
#  not forced onto downstream consumers.
# ─────────────────────────────────────────────────────────────────────────────
function(knst_configure_target TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /utf-8 /permissive-)
        if(KNST_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET_NAME} PRIVATE /WX)
        endif()
        if(KNST_ENABLE_SANITIZERS)
            target_compile_options(${TARGET_NAME} PRIVATE /fsanitize=address)
        endif()
        return()
    endif()

    # =====================================================================
    #  Warning policy
    #
    #  -Wundef is enabled to catch `#if KNST_FOO` when KNST_FOO is not
    #  defined — a common source of silent misconfiguration.
    #
    #  Deliberately NOT disabled:
    #    -Warray-bounds, -Wstringop-overflow, -Wstringop-overread
    #  These catch real out-of-bounds / buffer-overrun issues.
    # =====================================================================
    target_compile_options(${TARGET_NAME} PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Wundef
        -Wno-pedantic
        -Wno-conversion
        -Wno-float-conversion
        -Wno-unused-result
        -Wno-unused-parameter
        $<$<CXX_COMPILER_ID:GNU>:-Wno-format-truncation>
        -Wno-missing-field-initializers
        -Wno-unknown-pragmas
        -Wno-cast-function-type
        -Wno-sign-compare
        -Wno-delete-non-virtual-dtor
        -Wno-mismatched-tags)

    if(KNST_WARNINGS_AS_ERRORS)
        target_compile_options(${TARGET_NAME} PRIVATE -Werror)
    endif()

    if(KNST_ENABLE_SANITIZERS)
        if(MINGW)
            message(WARNING "MinGW does not support sanitizers.")
        else()
            target_compile_options(${TARGET_NAME} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${TARGET_NAME} PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

# ─────────────────────────────────────────────────────────────────────────────
#  Optional custom application source
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
#  Applications
# =============================================================================

# ─────────────────────────────────────────────────────────────────────────────
#  Android — shared library
# ─────────────────────────────────────────────────────────────────────────────
if(ANDROID AND (KNST_BUILD_EXAMPLES OR KNST_APP_SOURCE_RESOLVED))

    if(KNST_APP_SOURCE_RESOLVED)
        set(KNST_ANDROID_APP_SOURCE "${KNST_APP_SOURCE_RESOLVED}")
    elseif(KNST_WINDOW_USE_VULKAN AND NOT KNST_WINDOW_USE_OPENGL)
        set(KNST_ANDROID_APP_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/complex_structures/knst_window/android/knst_window_android_vulkan_test.cpp")
    else()
        set(KNST_ANDROID_APP_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/complex_structures/knst_window/android/knst_window_android_opengl_test.cpp")
    endif()

    if(NOT EXISTS "${KNST_ANDROID_APP_SOURCE}")
        message(FATAL_ERROR "Android application source not found: ${KNST_ANDROID_APP_SOURCE}")
    endif()

    if(DEFINED ANDROID_NDK)
        set(KNST_NDK_ROOT "${ANDROID_NDK}")
    elseif(DEFINED CMAKE_ANDROID_NDK)
        set(KNST_NDK_ROOT "${CMAKE_ANDROID_NDK}")
    elseif(DEFINED ENV{ANDROID_NDK_HOME})
        set(KNST_NDK_ROOT "$ENV{ANDROID_NDK_HOME}")
    else()
        message(FATAL_ERROR
            "Android NDK not found. Pass the NDK toolchain file:\n"
            "  -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake")
    endif()

    set(KNST_NATIVE_APP_GLUE_DIR "${KNST_NDK_ROOT}/sources/android/native_app_glue")
    if(NOT EXISTS "${KNST_NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
        message(FATAL_ERROR
            "android_native_app_glue.c not found at: ${KNST_NATIVE_APP_GLUE_DIR}")
    endif()

    add_library(knst_native_app_glue STATIC
        "${KNST_NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
    target_include_directories(knst_native_app_glue PUBLIC "${KNST_NATIVE_APP_GLUE_DIR}")
    set_target_properties(knst_native_app_glue PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # The NDK's native_app_glue.c is incompatible with ASan.
    # ANativeActivity_onCreate is called before the ASan runtime loads
    # → SEGV. Google's own samples also exempt this file from ASan.
    target_compile_options(knst_native_app_glue PRIVATE
        -fno-sanitize=address
        -fno-sanitize=undefined)

    add_library(knst_app SHARED "${KNST_ANDROID_APP_SOURCE}")
    target_link_libraries(knst_app PRIVATE KernelNucleusT knst_native_app_glue)
    knst_configure_target(knst_app)

    target_link_options(knst_app PRIVATE
        -u ANativeActivity_onCreate
        -Wl,-z,max-page-size=16384)

# ─────────────────────────────────────────────────────────────────────────────
#  Desktop — executables (Windows / Linux)
# ─────────────────────────────────────────────────────────────────────────────
elseif(NOT ANDROID)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    if(KNST_APP_SOURCE_RESOLVED)
        add_executable(knst_app "${KNST_APP_SOURCE_RESOLVED}")
        target_link_libraries(knst_app PRIVATE KernelNucleusT)
        knst_configure_target(knst_app)
    endif()

    if(KNST_BUILD_EXAMPLES)
        # ⚠️ FIX: filter on RELATIVE paths so an absolute path like
        #    `/home/user/opengl-project/...` doesn't trip the regex.
        file(GLOB_RECURSE KNST_EXAMPLE_SOURCES CONFIGURE_DEPENDS
            RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/examples"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")

        # Match the substring at the START of the relative path only.
        list(FILTER KNST_EXAMPLE_SOURCES EXCLUDE REGEX "^android/")
        list(FILTER KNST_EXAMPLE_SOURCES EXCLUDE REGEX "^.*/android/")

        # Skip examples whose backend isn't built.
        if(NOT KNST_WINDOW_USE_OPENGL)
            list(FILTER KNST_EXAMPLE_SOURCES EXCLUDE REGEX "opengl")
            list(FILTER KNST_EXAMPLE_SOURCES EXCLUDE REGEX "touch_screen")
        endif()
        if(NOT KNST_WINDOW_USE_VULKAN)
            list(FILTER KNST_EXAMPLE_SOURCES EXCLUDE REGEX "vulkan")
        endif()

        # Re-prepend the examples root to get absolute paths back.
        set(_knst_ex_abs "")
        foreach(_rel IN LISTS KNST_EXAMPLE_SOURCES)
            list(APPEND _knst_ex_abs
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/${_rel}")
        endforeach()
        set(KNST_EXAMPLE_SOURCES "${_knst_ex_abs}")

        foreach(_src IN LISTS KNST_EXAMPLE_SOURCES)
            get_filename_component(_name "${_src}" NAME_WE)
            if(TARGET ${_name})
                message(WARNING
                    "Skipping example with duplicate target name '${_name}': ${_src}")
                continue()
            endif()
            add_executable(${_name} "${_src}")
            target_link_libraries(${_name} PRIVATE KernelNucleusT)
            knst_configure_target(${_name})
            list(APPEND KNST_EXAMPLE_TARGETS ${_name})
        endforeach()
    endif()
endif()

# =============================================================================
#  Installation + package export
# =============================================================================
if(KNST_IS_TOP_LEVEL)
    include(CMakePackageConfigHelpers)
    include(GNUInstallDirs)

    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
            PATTERN "*.hpp"
            PATTERN "*.h"
            PATTERN "*.c")

    install(TARGETS KernelNucleusT
        EXPORT   KernelNucleusTTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    if(TARGET knst_glad)
        install(TARGETS knst_glad
            EXPORT  KernelNucleusTTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()

    if(TARGET knst_wayland_protocols)
        install(TARGETS knst_wayland_protocols
            EXPORT  KernelNucleusTTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()

    install(EXPORT KernelNucleusTTargets
        FILE      KernelNucleusTTargets.cmake
        NAMESPACE KernelNucleusT::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/KernelNucleusT)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/KernelNucleusTConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/KernelNucleusTConfig.cmake"
"include(CMakeFindDependencyMacro)

# Threads — required on Linux, harmless on Windows
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

    if(NOT ANDROID)
        set(_install_targets "")
        if(TARGET knst_app)
            list(APPEND _install_targets knst_app)
        endif()
        if(KNST_EXAMPLE_TARGETS)
            list(APPEND _install_targets ${KNST_EXAMPLE_TARGETS})
        endif()
        if(_install_targets)
            install(TARGETS ${_install_targets}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
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
    message(STATUS " Examples:       ${KNST_BUILD_EXAMPLES}")
    message(STATUS " OpenGL:         ${KNST_WINDOW_USE_OPENGL}")
    message(STATUS " Vulkan:         ${KNST_WINDOW_USE_VULKAN}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
        message(STATUS " Embedded:       ${KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND}")
    endif()
    if(KNST_APP_SOURCE_RESOLVED)
        message(STATUS " App source:     ${KNST_APP_SOURCE_RESOLVED}")
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