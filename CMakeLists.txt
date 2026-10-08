# =============================================================================
# KernelNucleusT - Build Configuration
# =============================================================================
#
# Graphics modes (exactly one is active):
#   * Vulkan   : -DKNST_ENABLE_VULKAN=ON   -> defines KNST_USING_VULKAN
#   * Headless : -DKNST_ENABLE_VULKAN=OFF  -> no Vulkan macro, no Vulkan linkage
#
# Platforms (auto-detected from the toolchain):
#   * Android  : KNST_USING_PLATFORM_ANDROID  + KNST_USING_PLATFORM_LINUX
#   * Windows  : KNST_USING_PLATFORM_WINDOWS
#   * Linux    : KNST_USING_PLATFORM_LINUX +
#                  KNST_USING_LINUX_PLATFORM_X11      (-DKNST_LINUX_PLATFORM=X11)
#                  KNST_USING_LINUX_PLATFORM_WAYLAND  (-DKNST_LINUX_PLATFORM=Wayland)
#
# The library is header-only and exposed as the INTERFACE target
#   KernelNucleusT::KernelNucleusT
# Every macro, include path and system library is propagated to consumers.
# =============================================================================

cmake_minimum_required(VERSION 3.20)

project(KernelNucleusT
    VERSION 1.0.0
    DESCRIPTION "KernelNucleusT cross-platform window / GUI framework"
    LANGUAGES C CXX
)

# -----------------------------------------------------------------------------
# Top-level detection (so the project behaves well when used via add_subdirectory)
# -----------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(KNST_IS_TOP_LEVEL ON)
else()
    set(KNST_IS_TOP_LEVEL OFF)
endif()

# -----------------------------------------------------------------------------
# User options
# -----------------------------------------------------------------------------
option(KNST_ENABLE_VULKAN      "Build with the Vulkan backend (OFF = headless)"        OFF)
option(KNST_BUILD_EXAMPLES     "Build the example applications"                        OFF)
option(KNST_ENABLE_SANITIZERS  "Enable AddressSanitizer / UBSan (debug use)"           OFF)
option(KNST_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"                     OFF)
option(KNST_STATIC_MSVC_RUNTIME "Use the static MSVC runtime (/MT, /MTd)"              ON)

set(KNST_APP_SOURCE "" CACHE FILEPATH
    "Optional: path to a single .cpp file built as the 'knst_app' target")

# Linux-only: display server selection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    set(KNST_LINUX_PLATFORM "X11" CACHE STRING "Linux display server protocol (X11 or Wayland)")
    set_property(CACHE KNST_LINUX_PLATFORM PROPERTY STRINGS "X11" "Wayland")
endif()

# -----------------------------------------------------------------------------
# Global settings (only when this is the top-level project)
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

# -----------------------------------------------------------------------------
# Platform detection
# -----------------------------------------------------------------------------
if(ANDROID)
    set(KNST_PLATFORM "Android")
elseif(WIN32)
    set(KNST_PLATFORM "Windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(KNST_PLATFORM "Linux")
elseif(APPLE)
    message(FATAL_ERROR
        "macOS is not supported. Supported platforms: Windows, Linux (X11/Wayland), Android.")
else()
    message(FATAL_ERROR
        "Unsupported platform '${CMAKE_SYSTEM_NAME}'. "
        "Supported platforms: Windows, Linux (X11/Wayland), Android.")
endif()

# -----------------------------------------------------------------------------
# The library target (header-only INTERFACE)
# -----------------------------------------------------------------------------
add_library(KernelNucleusT INTERFACE)
add_library(KernelNucleusT::KernelNucleusT ALIAS KernelNucleusT)

target_include_directories(KernelNucleusT INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(KernelNucleusT INTERFACE cxx_std_20)

# Accumulated compile definitions / link items for the INTERFACE target
set(KNST_DEFINITIONS "")
set(KNST_LINK_ITEMS  "")
set(KNST_PLATFORM_DETAIL "${KNST_PLATFORM}")

# -----------------------------------------------------------------------------
# Threads / common system libraries
# -----------------------------------------------------------------------------
if(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    list(APPEND KNST_LINK_ITEMS Threads::Threads ${CMAKE_DL_LIBS})
endif()

# -----------------------------------------------------------------------------
# Platform: Android
# -----------------------------------------------------------------------------
if(KNST_PLATFORM STREQUAL "Android")
    list(APPEND KNST_DEFINITIONS
        KNST_USING_PLATFORM_ANDROID
        KNST_USING_PLATFORM_LINUX
    )
    list(APPEND KNST_LINK_ITEMS android log)

    # Resolve the Android API level
    if(DEFINED ANDROID_PLATFORM_LEVEL)
        set(KNST_ANDROID_API "${ANDROID_PLATFORM_LEVEL}")
    elseif(DEFINED ANDROID_NATIVE_API_LEVEL)
        set(KNST_ANDROID_API "${ANDROID_NATIVE_API_LEVEL}")
    else()
        set(KNST_ANDROID_API 0)
    endif()

    if(KNST_ANDROID_API GREATER 0 AND KNST_ANDROID_API LESS 24)
        message(WARNING
            "Android API level ${KNST_ANDROID_API} is below 24. Some features may not work.")
    endif()

    set(KNST_PLATFORM_DETAIL "Android (API ${KNST_ANDROID_API}, ABI ${ANDROID_ABI})")

# -----------------------------------------------------------------------------
# Platform: Windows
# -----------------------------------------------------------------------------
elseif(KNST_PLATFORM STREQUAL "Windows")
    list(APPEND KNST_DEFINITIONS
        KNST_USING_PLATFORM_WINDOWS
        NOMINMAX
        OEMRESOURCE
        _CRT_SECURE_NO_WARNINGS
    )

    list(APPEND KNST_LINK_ITEMS
        gdi32 user32 shell32 shcore ole32 oleaut32 uuid
    )

   
        list(APPEND KNST_LINK_ITEMS
        hid  
        bthprops
        setupapi
        strmiids
        iphlpapi
        ws2_32
        wbemuuid  
        psapi
        oleaut32
        dxgi
        pdh
        advapi32
    )

    if(MSVC)
        set(KNST_PLATFORM_DETAIL "Windows (MSVC)")
    else()
        set(KNST_PLATFORM_DETAIL "Windows (${CMAKE_CXX_COMPILER_ID} / MinGW)")
    endif()

# -----------------------------------------------------------------------------
# Platform: Linux (X11 or Wayland)
# -----------------------------------------------------------------------------
elseif(KNST_PLATFORM STREQUAL "Linux")
    list(APPEND KNST_DEFINITIONS KNST_USING_PLATFORM_LINUX)
    list(APPEND KNST_LINK_ITEMS m)

    string(TOUPPER "${KNST_LINUX_PLATFORM}" KNST_LINUX_PLATFORM_UC)
    if(NOT KNST_LINUX_PLATFORM_UC MATCHES "^(X11|WAYLAND)$")
        message(FATAL_ERROR
            "KNST_LINUX_PLATFORM must be 'X11' or 'Wayland'. Got: '${KNST_LINUX_PLATFORM}'")
    endif()

    find_package(PkgConfig REQUIRED)

    # ---------------------------- Wayland ------------------------------------
    if(KNST_LINUX_PLATFORM_UC STREQUAL "WAYLAND")
        list(APPEND KNST_DEFINITIONS KNST_USING_LINUX_PLATFORM_WAYLAND)
        set(KNST_PLATFORM_DETAIL "Linux (Wayland)")

        pkg_check_modules(KNST_WAYLAND REQUIRED IMPORTED_TARGET GLOBAL
            wayland-client
            wayland-cursor
            xkbcommon
        )

        # Optional: wayland-egl (only linked if present on the system)
        pkg_check_modules(KNST_WAYLAND_EGL QUIET IMPORTED_TARGET GLOBAL wayland-egl)

        set(KNST_WAYLAND_PROTOCOL_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/linux/wayland/protocol_files")

        set(KNST_WAYLAND_PROTOCOL_SOURCES
            "${KNST_WAYLAND_PROTOCOL_DIR}/xdg-shell-protocol.c"
            "${KNST_WAYLAND_PROTOCOL_DIR}/xdg-output-protocol.c"
            "${KNST_WAYLAND_PROTOCOL_DIR}/xdg-decoration-protocol.c"
            "${KNST_WAYLAND_PROTOCOL_DIR}/pointer-constraints-unstable-v1-protocol.c"
            "${KNST_WAYLAND_PROTOCOL_DIR}/relative-pointer-unstable-v1-protocol.c"
        )

        foreach(_proto_src IN LISTS KNST_WAYLAND_PROTOCOL_SOURCES)
            if(NOT EXISTS "${_proto_src}")
                message(FATAL_ERROR "Wayland protocol source not found: ${_proto_src}")
            endif()
        endforeach()

        # Generated protocol glue code is compiled once as a static C library.
        add_library(knst_wayland_protocols STATIC ${KNST_WAYLAND_PROTOCOL_SOURCES})
        set_target_properties(knst_wayland_protocols PROPERTIES
            LINKER_LANGUAGE C
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON
        )
        target_include_directories(knst_wayland_protocols PUBLIC
            "${KNST_WAYLAND_PROTOCOL_DIR}")
        target_link_libraries(knst_wayland_protocols PUBLIC PkgConfig::KNST_WAYLAND)

        list(APPEND KNST_LINK_ITEMS knst_wayland_protocols PkgConfig::KNST_WAYLAND)
        if(TARGET PkgConfig::KNST_WAYLAND_EGL)
            list(APPEND KNST_LINK_ITEMS PkgConfig::KNST_WAYLAND_EGL)
        endif()

    # ------------------------------ X11 --------------------------------------
    else()
        list(APPEND KNST_DEFINITIONS KNST_USING_LINUX_PLATFORM_X11)
        set(KNST_PLATFORM_DETAIL "Linux (X11)")

        set(KNST_X11_MODULES
            x11 x11-xcb xcb xcb-randr xcb-keysyms xcb-icccm xcb-util
            xext xfixes xcb-sync xi xrandr xcursor
        )

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
                "Missing X11 development packages (pkg-config modules): ${_missing_str}\n"
                "Debian/Ubuntu/Pop!_OS:\n"
                "  sudo apt install libx11-dev libx11-xcb-dev libxcb1-dev libxcb-randr0-dev "
                "libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-util-dev libxcb-sync-dev "
                "libxext-dev libxfixes-dev libxi-dev libxrandr-dev libxcursor-dev\n"
                "See the setup guide (KURULUM.md) for Fedora and Arch."
            )
        endif()
    endif()
endif()

# -----------------------------------------------------------------------------
# Graphics mode: Vulkan or headless
# -----------------------------------------------------------------------------
if(KNST_ENABLE_VULKAN)
    find_package(Vulkan QUIET)

    if(TARGET Vulkan::Vulkan)
        list(APPEND KNST_LINK_ITEMS Vulkan::Vulkan)
    elseif(ANDROID)
        # The NDK ships libvulkan.so in its sysroot (API 24+).
        if(KNST_ANDROID_API GREATER 0 AND KNST_ANDROID_API LESS 24)
            message(FATAL_ERROR
                "Vulkan requires Android API level 24 or higher (current: ${KNST_ANDROID_API}). "
                "Use -DANDROID_PLATFORM=android-24 or newer.")
        endif()
        list(APPEND KNST_LINK_ITEMS vulkan)
    else()
        if(WIN32)
            set(_vk_hint
                "Install the Vulkan SDK from https://vulkan.lunarg.com/ and make sure the "
                "VULKAN_SDK environment variable is set (restart the terminal afterwards). "
                "For MSYS2: pacman -S mingw-w64-ucrt-x86_64-vulkan-headers "
                "mingw-w64-ucrt-x86_64-vulkan-loader")
        else()
            set(_vk_hint
                "Debian/Ubuntu: sudo apt install libvulkan-dev vulkan-tools | "
                "Fedora: sudo dnf install vulkan-loader-devel vulkan-headers | "
                "Arch: sudo pacman -S vulkan-headers vulkan-icd-loader")
        endif()
        message(FATAL_ERROR
            "KNST_ENABLE_VULKAN=ON but Vulkan was not found.\n${_vk_hint}\n"
            "Alternatively build in headless mode with -DKNST_ENABLE_VULKAN=OFF.")
    endif()

    list(APPEND KNST_DEFINITIONS KNST_USING_VULKAN)
    if(ANDROID)
        list(APPEND KNST_DEFINITIONS KNST_PLATFORM_ANDROID_VULKAN)
    endif()

    set(KNST_GRAPHICS_MODE "Vulkan")
else()
    set(KNST_GRAPHICS_MODE "Headless (no Vulkan)")
endif()

# -----------------------------------------------------------------------------
# Apply accumulated definitions and link items to the INTERFACE target
# -----------------------------------------------------------------------------
target_compile_definitions(KernelNucleusT INTERFACE ${KNST_DEFINITIONS})
target_link_libraries(KernelNucleusT INTERFACE ${KNST_LINK_ITEMS})

# -----------------------------------------------------------------------------
# Per-target warning / sanitizer configuration (not forced onto consumers)
# -----------------------------------------------------------------------------
function(knst_configure_target TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /utf-8 /permissive-)
        if(KNST_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET_NAME} PRIVATE /WX)
        endif()
        if(KNST_ENABLE_SANITIZERS)
            target_compile_options(${TARGET_NAME} PRIVATE /fsanitize=address)
        endif()
    else()
                target_compile_options(${TARGET_NAME} PRIVATE
            -Wall -Wextra -Wshadow
                        -Wno-pedantic
            -Wno-conversion
            -Wno-float-conversion
            -Wno-unused-result
            -Wno-unused-parameter
            -Wno-unused-variable
            -Wno-unused-but-set-variable
            $<$<CXX_COMPILER_ID:GNU>:-Wno-format-truncation>
            $<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overread>
            $<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overflow>
            -Wno-misleading-indentation
            -Wno-missing-field-initializers
            -Wno-unknown-pragmas
            -Wno-cast-function-type
            -Wno-sign-compare
            -Wno-delete-non-virtual-dtor
            -Wno-array-bounds
            -Wno-mismatched-tags
            $<$<CXX_COMPILER_ID:GNU>:-Wno-format-truncation>
        )
        if(KNST_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET_NAME} PRIVATE -Werror)
        endif()
        if(KNST_ENABLE_SANITIZERS)
            if(MINGW)
                message(WARNING "MinGW sanitizer desteklemiyor.")
            else()
                target_compile_options(${TARGET_NAME} PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(${TARGET_NAME} PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Resolve optional custom application source
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# Applications: Android (shared library)
# -----------------------------------------------------------------------------
if(ANDROID AND (KNST_BUILD_EXAMPLES OR KNST_APP_SOURCE_RESOLVED))

    if(KNST_APP_SOURCE_RESOLVED)
        set(KNST_ANDROID_APP_SOURCE "${KNST_APP_SOURCE_RESOLVED}")
    else()
        set(KNST_ANDROID_APP_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/complex_structures/knst_window/android/knst_window_android_test.cpp")
    endif()

    if(NOT EXISTS "${KNST_ANDROID_APP_SOURCE}")
        message(FATAL_ERROR "Android application source not found: ${KNST_ANDROID_APP_SOURCE}")
    endif()

    # Locate the NDK and its native_app_glue
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

    add_library(knst_app SHARED "${KNST_ANDROID_APP_SOURCE}")
    target_link_libraries(knst_app PRIVATE KernelNucleusT knst_native_app_glue)
    knst_configure_target(knst_app)

    # Keep ANativeActivity_onCreate from being stripped; support 16 KB page devices.
    target_link_options(knst_app PRIVATE
        -u ANativeActivity_onCreate
        -Wl,-z,max-page-size=16384
    )

# -----------------------------------------------------------------------------
# Applications: Desktop (Windows / Linux)
# -----------------------------------------------------------------------------
elseif(NOT ANDROID)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Optional single custom application
    if(KNST_APP_SOURCE_RESOLVED)
        add_executable(knst_app "${KNST_APP_SOURCE_RESOLVED}")
        target_link_libraries(knst_app PRIVATE KernelNucleusT)
        knst_configure_target(knst_app)
    endif()

    # One executable per example source (Android examples are excluded)
    if(KNST_BUILD_EXAMPLES)
        file(GLOB_RECURSE KNST_EXAMPLE_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
        list(FILTER KNST_EXAMPLE_SOURCES EXCLUDE REGEX "/android/")

        foreach(_src IN LISTS KNST_EXAMPLE_SOURCES)
            get_filename_component(_name "${_src}" NAME_WE)
            if(TARGET ${_name})
                message(WARNING "Skipping example with duplicate target name '${_name}': ${_src}")
                continue()
            endif()
            add_executable(${_name} "${_src}")
            target_link_libraries(${_name} PRIVATE KernelNucleusT)
            knst_configure_target(${_name})
            list(APPEND KNST_EXAMPLE_TARGETS ${_name})
        endforeach()
    endif()
endif()

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------
if(KNST_IS_TOP_LEVEL)
    install(DIRECTORY include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
    )

    if(NOT ANDROID)
        set(_install_targets "")
        if(TARGET knst_app)
            list(APPEND _install_targets knst_app)
        endif()
        if(KNST_EXAMPLE_TARGETS)
            list(APPEND _install_targets ${KNST_EXAMPLE_TARGETS})
        endif()
        if(_install_targets)
            install(TARGETS ${_install_targets} RUNTIME DESTINATION bin)
        endif()
    endif()
endif()

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
if(KNST_IS_TOP_LEVEL)
    list(JOIN KNST_DEFINITIONS "\n                  " KNST_DEFINITIONS_STR)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " KernelNucleusT v${PROJECT_VERSION}")
    message(STATUS "========================================")
    message(STATUS " Platform:       ${KNST_PLATFORM_DETAIL}")
    message(STATUS " Graphics:       ${KNST_GRAPHICS_MODE}")
    message(STATUS " Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    if(CMAKE_BUILD_TYPE)
        message(STATUS " Build type:     ${CMAKE_BUILD_TYPE}")
    endif()
    message(STATUS " C++ standard:   C++${CMAKE_CXX_STANDARD}")
    message(STATUS " Examples:       ${KNST_BUILD_EXAMPLES}")
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


# -----------------------------------------------------------------------------
# Example commands
# -----------------------------------------------------------------------------
#
# Linux — X11 (bash):
#   cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 ^
#     "-DKNST_APP_SOURCE=examples/simple_structures/knst_vector/knst_vector_basic.cpp"
#   cmake --build build
#   ./build/bin/knst_app
#
# Linux — X11 + Vulkan (bash):
#   cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=ON ^
#     "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp"
#   cmake --build build
#   ./build/bin/knst_app
#
# Linux — X11 + Headless (bash):
#   cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=OFF ^
#     "-DKNST_APP_SOURCE=examples/simple_structures/knst_devices/knst_devices_basic.cpp"
#   cmake --build build
#   ./build/bin/knst_app
#
# Linux — X11 tüm örnekleri derle (bash):
#   cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=ON -DKNST_BUILD_EXAMPLES=ON
#   cmake --build build
#   ./build/bin/knst_window_basic
#
# Linux — X11 tüm örnekleri derle (Headless):
#   cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=OFF -DKNST_BUILD_EXAMPLES=ON
#   cmake --build build
#   ./build/bin/knst_vector_basic
#
# Linux / MSYS2 UCRT64 (bash):
#   cmake -S . -B build -G Ninja "-DKNST_APP_SOURCE=examples/simple_structures/knst_vector/knst_vector_basic.cpp"
#   cmake --build build
#   ./build/bin/knst_app
#
# Windows MSVC (run inside "x64 Native Tools Command Prompt for VS"):
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
#     "-DKNST_APP_SOURCE=examples/simple_structures/knst_vector/knst_vector_basic.cpp"
#   cmake --build build --config Release
#   build\bin\Release\knst_app.exe
#
# Windows PowerShell (quotes required around every -D argument):
#   cmake -S . -B build -G Ninja `
#     "-DKNST_APP_SOURCE=examples/simple_structures/knst_vector/knst_vector_basic.cpp"
#   cmake --build build
#   .\build\bin\knst_app.exe
#
# Android (NDK toolchain required; run inside MSYS2 UCRT64, Linux or macOS):
#   export ANDROID_NDK=$HOME/Android/Sdk/ndk/<version>
#   cmake -S . -B build-android -G Ninja ^
#     -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK%/build/cmake/android.toolchain.cmake ^
#     -DANDROID_ABI=arm64-v8a ^
#     -DANDROID_PLATFORM=android-24 ^
#     -DKNST_ENABLE_VULKAN=ON
#   cmake --build build-android
#   # Output: build-android/libknst_app.so
#
# Android (bash version):
#   export ANDROID_NDK=$HOME/Android/Sdk/ndk/<version>
#   cmake -S . -B build-android -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a \
#     -DANDROID_PLATFORM=android-24 \
#     -DKNST_ENABLE_VULKAN=ON
#   cmake --build build-android
#   # Output: build-android/libknst_app.so
#
# Build all examples:
#   cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=ON -DKNST_BUILD_EXAMPLES=ON
#   cmake --build build
#   # Each example becomes a separate executable under build/bin/
#   # e.g. build/bin/knst_window_basic, build/bin/knst_vector_basic
# -----------------------------------------------------------------------------