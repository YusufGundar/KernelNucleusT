# KernelNucleusT - Build Configuration

cmake_minimum_required(VERSION 3.15)

project(KernelNucleusT VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

#*_____________________________________________________________
# User Options
#*_____________________________________________________________
option(ENABLE_OPENGL "Enable OpenGL backend" OFF)
option(ENABLE_VULKAN "Enable Vulkan backend" OFF)

if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(LINUX_PLATFORM "" CACHE STRING "Display server protocol (X11 or Wayland) [REQUIRED]")
    set_property(CACHE LINUX_PLATFORM PROPERTY STRINGS "X11" "Wayland")
endif()

#*_____________________________________________________________
# Compiler Detection
#*_____________________________________________________________
if(MSVC)
    add_compile_options(/W4 /utf-8)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|IntelLLVM")
    add_compile_options(-Wall -Wextra)
endif()

#*_____________________________________________________________
# Platform: Android
#*_____________________________________________________________
if(ANDROID)
    add_definitions(-DKNST_USING_PLATFORM_ANDROID)
    add_definitions(-DKNST_USING_PLATFORM_LINUX)
    set(PLATFORM_TYPE "Android")
    set(PLATFORM_INCLUDES)
    set(PROTOCOL_SOURCES)
    set(EXTRA_LIBS)
    set(ANDROID_SOURCES package_tests/knst_window/android/knst_window_android_test.cpp)

    set(NATIVE_APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
    
    set(PLATFORM_LIBS android log)
    list(APPEND ANDROID_SOURCES ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c)
    list(APPEND PLATFORM_INCLUDES ${NATIVE_APP_GLUE_DIR})

    # ============================================================
    # OpenGL ES
    # ============================================================
    if(ENABLE_OPENGL)
        add_definitions(-DKNST_USING_OPENGL)
        add_definitions(-DKNST_OPENGL_USING_EGL)
        add_definitions(-DKNST_PLATFORM_ANDROID_OPENGL)      # ✅ Android OpenGL makrosu
        set(OPENGL_LIBS EGL GLESv3 GLESv2)
        set(EXTRA_LIBS ${EXTRA_LIBS} EGL GLESv3 GLESv2)
        set(OPENGL_INCLUDES)
        set(GLAD_SOURCES)
        set(GLAD_INCLUDES)
        message(STATUS " Android OpenGL: ENABLED")
    else()
        message(STATUS " Android OpenGL: DISABLED")
    endif()
    # ============================================================

    # ============================================================
    # Vulkan
    # ============================================================
    if(ENABLE_VULKAN)
        add_definitions(-DKNST_USING_VULKAN)
        add_definitions(-DKNST_PLATFORM_ANDROID_VULKAN)      # ✅ Android Vulkan makrosu
        set(VULKAN_LIBS vulkan)
        set(EXTRA_LIBS ${EXTRA_LIBS} vulkan)
        set(VULKAN_INCLUDES)
        message(STATUS " Android Vulkan: ENABLED")
    else()
        message(STATUS " Android Vulkan: DISABLED")
    endif()
    # ============================================================

    # ============================================================
    # None - Her ikisi de kapalı
    # ============================================================
    if(NOT ENABLE_OPENGL AND NOT ENABLE_VULKAN)
        message(STATUS " Android Graphics: NONE (No OpenGL/Vulkan)")
    endif()

#*_____________________________________________________________
# Platform: Windows
#*_____________________________________________________________
elseif(WIN32)
    add_definitions(-DKNST_USING_PLATFORM_WINDOWS)
    set(PLATFORM_TYPE "Windows")
    set(PLATFORM_EXTRA_LIBS)
    set(PROTOCOL_SOURCES)
    set(EXTRA_LIBS)

    set(PLATFORM_LIBS gdi32 user32 shell32 shcore ole32 oleaut32 uuid)
    set(PLATFORM_INCLUDES)

    # --- MSYS2 Detection ---
    set(MSYS2_ROOTS "C:/msys64/ucrt64" "C:/msys64/mingw64" "C:/msys64/clang64")
    foreach(ROOT ${MSYS2_ROOTS})
        if(EXISTS "${ROOT}")
            set(MSYS2_PATH "${ROOT}")
            list(APPEND PLATFORM_INCLUDES "${MSYS2_PATH}/include")
            link_directories("${MSYS2_PATH}/lib")
            break()
        endif()
    endforeach()

    # --- Vulkan SDK Detection ---
    set(VULKAN_SDK_SEARCH
        "$ENV{VULKAN_SDK}"
        "C:/VulkanSDK"
        "${MSYS2_PATH}"
    )

    foreach(PATH ${VULKAN_SDK_SEARCH})
        if(EXISTS "${PATH}/Include/vulkan/vulkan.h")
            set(VULKAN_SDK_PATH "${PATH}")
            set(VULKAN_INCLUDE_DIR "${PATH}/Include")
            set(VULKAN_LIB_DIR "${PATH}/Lib")
            break()
        elseif(EXISTS "${PATH}/include/vulkan/vulkan.h")
            set(VULKAN_SDK_PATH "${PATH}")
            set(VULKAN_INCLUDE_DIR "${PATH}/include")
            set(VULKAN_LIB_DIR "${PATH}/lib")
            break()
        endif()
    endforeach()

    if(VULKAN_SDK_PATH AND VULKAN_INCLUDE_DIR MATCHES "VulkanSDK")
        file(GLOB VERSIONS RELATIVE "${VULKAN_SDK_PATH}" "${VULKAN_SDK_PATH}/*")
        list(FILTER VERSIONS INCLUDE REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
        if(VERSIONS)
            list(SORT VERSIONS ORDER DESCENDING)
            list(GET VERSIONS 0 VER)
            set(VULKAN_INCLUDE_DIR "${VULKAN_SDK_PATH}/${VER}/Include")
            set(VULKAN_LIB_DIR "${VULKAN_SDK_PATH}/${VER}/Lib")
        endif()
    endif()

#*_____________________________________________________________
# Platform: Linux
#*_____________________________________________________________
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
    add_definitions(-DKNST_USING_PLATFORM_LINUX)

    if(LINUX_PLATFORM STREQUAL "Wayland")
        add_definitions(-DKNST_USING_LINUX_PLATFORM_WAYLAND)
        set(PLATFORM_TYPE "Wayland")

        find_package(PkgConfig REQUIRED)
        pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-cursor wayland-egl xkbcommon)

        set(PLATFORM_LIBS ${WAYLAND_LIBRARIES})
        set(PLATFORM_INCLUDES ${WAYLAND_INCLUDE_DIRS})

        set(PROTOCOL_SOURCES
            include/platform/linux/wayland/protocol_files/xdg-shell-protocol.c
            include/platform/linux/wayland/protocol_files/xdg-output-protocol.c
            include/platform/linux/wayland/protocol_files/xdg-decoration-protocol.c
            include/platform/linux/wayland/protocol_files/pointer-constraints-unstable-v1-protocol.c
            include/platform/linux/wayland/protocol_files/relative-pointer-unstable-v1-protocol.c
        )

        set(EXTRA_LIBS wayland-client wayland-cursor wayland-egl xkbcommon)

    else()
        add_definitions(-DKNST_USING_LINUX_PLATFORM_X11)
        set(PLATFORM_TYPE "X11")

        find_package(X11 REQUIRED)
        set(PLATFORM_LIBS ${X11_LIBRARIES})
        set(PLATFORM_INCLUDES ${X11_INCLUDE_DIR})

        find_package(PkgConfig REQUIRED)
        foreach(LIB x11-xcb xcb xcb-randr xcb-keysyms xcb-icccm xcb-util xext xfixes xi xrandr xcursor)
            string(REPLACE "-" "_" VAR "${LIB}")
            string(TOUPPER "${VAR}" VAR_UPPER)
            pkg_check_modules(PKG_${VAR_UPPER} ${LIB})
            if(PKG_${VAR_UPPER}_FOUND)
                list(APPEND PLATFORM_LIBS ${PKG_${VAR_UPPER}_LIBRARIES})
                list(APPEND PLATFORM_INCLUDES ${PKG_${VAR_UPPER}_INCLUDE_DIRS})
            else()
                list(APPEND PLATFORM_LIBS ${LIB})
            endif()
        endforeach()

        set(PROTOCOL_SOURCES)
        set(EXTRA_LIBS)
    endif()

else()
    message(FATAL_ERROR "Unsupported platform. Only Windows, Linux, and Android are supported.")
endif()

#*_____________________________________________________________
# OpenGL Backend (X11 only)
#*_____________________________________________________________
if(ENABLE_OPENGL AND PLATFORM_TYPE STREQUAL "X11")
    set(OPENGL_BACKEND "" CACHE STRING "OpenGL binding for X11 (GLX or EGL) [REQUIRED]")
    set_property(CACHE OPENGL_BACKEND PROPERTY STRINGS "GLX" "EGL")

    if(NOT OPENGL_BACKEND MATCHES "^(GLX|EGL)$")
        message(FATAL_ERROR
            "OPENGL_BACKEND must be 'GLX' or 'EGL' when using OpenGL on X11.\n"
            "Pass -DOPENGL_BACKEND=GLX or -DOPENGL_BACKEND=EGL")
    endif()
endif()

#*_____________________________________________________________
# OpenGL Configuration (Masaüstü)
#*_____________________________________________________________
if(NOT ANDROID)
    set(OPENGL_LIBS)
    set(OPENGL_INCLUDES)
    set(GLAD_SOURCES)
    set(GLAD_INCLUDES)
endif()

if(ENABLE_OPENGL AND NOT ANDROID)
    add_definitions(-DKNST_USING_OPENGL)

    if(WIN32)
        set(OPENGL_LIBS opengl32 glu32)

    elseif(UNIX AND NOT APPLE AND NOT ANDROID)
        find_package(OpenGL REQUIRED)
        set(OPENGL_LIBS ${OPENGL_LIBRARIES})
        set(OPENGL_INCLUDES ${OPENGL_INCLUDE_DIR})
        find_package(PkgConfig REQUIRED)

        if(PLATFORM_TYPE STREQUAL "X11" AND OPENGL_BACKEND STREQUAL "EGL")
            add_definitions(-DKNST_OPENGL_USING_EGL)
            pkg_check_modules(EGL REQUIRED egl)
            list(APPEND OPENGL_LIBS ${EGL_LIBRARIES})
            list(APPEND OPENGL_INCLUDES ${EGL_INCLUDE_DIRS})
        elseif(PLATFORM_TYPE STREQUAL "X11")
            add_definitions(-DKNST_OPENGL_USING_GLX)
            pkg_check_modules(GLX REQUIRED glx)
            list(APPEND OPENGL_INCLUDES ${GLX_INCLUDE_DIRS})
        elseif(PLATFORM_TYPE STREQUAL "Wayland")
            add_definitions(-DKNST_OPENGL_USING_EGL)
            pkg_check_modules(EGL REQUIRED egl)
            pkg_check_modules(WAYLAND_EGL REQUIRED wayland-egl)
            pkg_check_modules(GLESV2 REQUIRED glesv2)
            list(APPEND OPENGL_LIBS ${EGL_LIBRARIES} ${WAYLAND_EGL_LIBRARIES} ${GLESV2_LIBRARIES})
            list(APPEND OPENGL_INCLUDES ${EGL_INCLUDE_DIRS} ${WAYLAND_EGL_INCLUDE_DIRS} ${GLESV2_INCLUDE_DIRS})
        endif()
    endif()

    set(GLAD_SOURCES glad_3_3/src/glad.c)
    set(GLAD_INCLUDES glad_3_3/include)

    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${GLAD_SOURCES}")
        message(FATAL_ERROR
            "glad.c not found at '${CMAKE_SOURCE_DIR}/${GLAD_SOURCES}'.\n"
            "Make sure the glad_3_3/ folder is present at the project root.")
    endif()
endif()

#*_____________________________________________________________
# Vulkan Configuration (Masaüstü)
#*_____________________________________________________________
if(NOT ANDROID)
    set(VULKAN_LIBS)
    set(VULKAN_INCLUDES)
endif()

if(ENABLE_VULKAN AND NOT ANDROID)
    add_definitions(-DKNST_USING_VULKAN)

    if(WIN32)
        if(NOT VULKAN_SDK_PATH)
            message(FATAL_ERROR "*--* Vulkan SDK not found. Install from https://vulkan.lunarg.com/ *--*")
        endif()
        set(VULKAN_LIBS vulkan-1)
        set(VULKAN_INCLUDES ${VULKAN_INCLUDE_DIR})

    elseif(UNIX AND NOT APPLE AND NOT ANDROID)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(VULKAN vulkan)

        if(VULKAN_FOUND)
            set(VULKAN_LIBS ${VULKAN_LIBRARIES})
            set(VULKAN_INCLUDES ${VULKAN_INCLUDE_DIRS})
        else()
            find_path(VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h)
            find_library(VULKAN_LIBRARY NAMES vulkan libvulkan)

            if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY)
                message(FATAL_ERROR "Vulkan not found. Install: vulkan-sdk")
            endif()

            set(VULKAN_LIBS ${VULKAN_LIBRARY})
            set(VULKAN_INCLUDES ${VULKAN_INCLUDE_DIR})
        endif()
    endif()
endif()

# ************************************************************
# Targets
# ************************************************************
if(ANDROID)
    add_library(knst_app SHARED
        ${ANDROID_SOURCES}
        ${PROTOCOL_SOURCES}
        ${GLAD_SOURCES}
    )
else()
    set(SOURCES
        package_tests/knst_window/knst_window_test.cpp
        ${PROTOCOL_SOURCES}
        ${GLAD_SOURCES}
    )
    add_executable(knst_app ${SOURCES})
endif()

if(GLAD_SOURCES)
    set_source_files_properties(${GLAD_SOURCES} PROPERTIES LANGUAGE C)
endif()
if(PROTOCOL_SOURCES)
    set_source_files_properties(${PROTOCOL_SOURCES} PROPERTIES LANGUAGE C)
endif()

target_include_directories(knst_app PRIVATE
    include
    ${PLATFORM_INCLUDES}
    ${GLAD_INCLUDES}
    ${VULKAN_INCLUDES}
    ${OPENGL_INCLUDES}
)

if(VULKAN_LIB_DIR)
    target_link_directories(knst_app PRIVATE ${VULKAN_LIB_DIR})
endif()

target_link_libraries(knst_app PRIVATE
    ${PLATFORM_LIBS}
    ${OPENGL_LIBS}
    ${VULKAN_LIBS}
    ${EXTRA_LIBS}
)

if(ANDROID)
  
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(knst_app PRIVATE dl pthread m)
endif()

if(WIN32 AND NOT MSVC)
    target_link_libraries(knst_app PRIVATE mingw32)
endif()

#*_____________________________________________________________
# Summary
#*_____________________________________________________________
message(STATUS "")
message(STATUS "========================================")
message(STATUS " KernelNucleusT v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS " Platform:     ${PLATFORM_TYPE}")
message(STATUS " Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

if(MSYS2_PATH)
    message(STATUS " MSYS2:        ${MSYS2_PATH}")
endif()
if(VULKAN_SDK_PATH)
    message(STATUS " Vulkan SDK:   ${VULKAN_SDK_PATH}")
endif()

message(STATUS " OpenGL:       ${ENABLE_OPENGL}")
message(STATUS " Vulkan:       ${ENABLE_VULKAN}")

if(ENABLE_OPENGL)
    if(PLATFORM_TYPE STREQUAL "X11")
        message(STATUS " GL Backend:   ${OPENGL_BACKEND}")
    elseif(PLATFORM_TYPE STREQUAL "Wayland")
        message(STATUS " GL Backend:   EGL (mandatory)")
    elseif(PLATFORM_TYPE STREQUAL "Android")
        message(STATUS " GL Backend:   EGL + GLES (mandatory)")
    endif()
    message(STATUS " GLAD source:  ${GLAD_SOURCES}")
endif()

message(STATUS "----------------------------------------")
message(STATUS " Build:        ${CMAKE_BINARY_DIR}")
message(STATUS " Source:       ${CMAKE_SOURCE_DIR}")
message(STATUS "========================================")
message(STATUS "")