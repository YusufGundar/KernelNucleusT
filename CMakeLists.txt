# KernelNucleusT - Build Configuration


cmake_minimum_required(VERSION 3.20)

project(KernelNucleusT VERSION 1.0.0 LANGUAGES C CXX)

#*_____________________________________________________________
# Global Settings
#*_____________________________________________________________
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable compile commands for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#*_____________________________________________________________
# User Options
#*_____________________________________________________________
option(ENABLE_OPENGL "Enable OpenGL backend" OFF)
option(ENABLE_VULKAN "Enable Vulkan backend" OFF)
option(BUILD_TESTS "Build test applications" ON)
option(ENABLE_SANITIZERS "Enable sanitizers for debugging" OFF)

# Validate that at least one graphics backend is selected
if(NOT ENABLE_OPENGL AND NOT ENABLE_VULKAN)
    message(WARNING "No graphics backend selected. Application will have no rendering capability.")
endif()

# Platform-specific requirements
if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(LINUX_PLATFORM "X11" CACHE STRING "Display server protocol (X11 or Wayland)")
    set_property(CACHE LINUX_PLATFORM PROPERTY STRINGS "X11" "Wayland")
    
    # Validate Linux platform selection
    if(NOT LINUX_PLATFORM MATCHES "^(X11|Wayland)$")
        message(FATAL_ERROR "LINUX_PLATFORM must be 'X11' or 'Wayland'. Got: '${LINUX_PLATFORM}'")
    endif()
endif()

#*_____________________________________________________________
# Compiler Detection and Configuration
#*_____________________________________________________________
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    # MSVC-specific definitions
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(NOMINMAX)
    
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang|IntelLLVM")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    
    # Add sanitizers if requested
    if(ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

#*_____________________________________________________________
# Helper Functions
#*_____________________________________________________________
function(knst_add_platform_definition PLATFORM_NAME)
    add_compile_definitions(${PLATFORM_NAME})
    message(STATUS "Added platform definition: ${PLATFORM_NAME}")
endfunction()

function(knst_check_pkg_config PKG_NAME)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(${PKG_NAME} QUIET ${ARGN})
        if(${PKG_NAME}_FOUND)
            set(${PKG_NAME}_FOUND TRUE PARENT_SCOPE)
            set(${PKG_NAME}_LIBRARIES ${${PKG_NAME}_LIBRARIES} PARENT_SCOPE)
            set(${PKG_NAME}_INCLUDE_DIRS ${${PKG_NAME}_INCLUDE_DIRS} PARENT_SCOPE)
            return()
        endif()
    endif()
    set(${PKG_NAME}_FOUND FALSE PARENT_SCOPE)
endfunction()

#*_____________________________________________________________
# Platform: Android
#*_____________________________________________________________
if(ANDROID)
    knst_add_platform_definition(KNST_USING_PLATFORM_ANDROID)
    knst_add_platform_definition(KNST_USING_PLATFORM_LINUX)
    
    set(PLATFORM_TYPE "Android")
    set(PLATFORM_INCLUDES)
    set(PROTOCOL_SOURCES)
    set(EXTRA_LIBS)
    
    # Android NDK validation
    if(NOT ANDROID_NDK)
        message(FATAL_ERROR "ANDROID_NDK not set. Required for Android builds.")
    endif()
    
    set(ANDROID_SOURCES package_tests/knst_window/android/knst_window_android_test.cpp)
    
    # Native App Glue
    set(NATIVE_APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
    if(NOT EXISTS "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
        message(FATAL_ERROR "Android native_app_glue not found at: ${NATIVE_APP_GLUE_DIR}")
    endif()
    
    set(PLATFORM_LIBS android log)
    list(APPEND ANDROID_SOURCES ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c)
    list(APPEND PLATFORM_INCLUDES ${NATIVE_APP_GLUE_DIR})
    
    # Android API level check
    if(ANDROID_NATIVE_API_LEVEL LESS 24)
        message(WARNING "Android API level ${ANDROID_NATIVE_API_LEVEL} is below 24. Some features may not work.")
    endif()

    # ============================================================
    # OpenGL ES for Android
    # ============================================================
    if(ENABLE_OPENGL)
        knst_add_platform_definition(KNST_USING_OPENGL)
        knst_add_platform_definition(KNST_OPENGL_USING_EGL)
        knst_add_platform_definition(KNST_PLATFORM_ANDROID_OPENGL)
        
        set(OPENGL_LIBS EGL GLESv3 GLESv2)
        list(APPEND EXTRA_LIBS EGL GLESv3 GLESv2)
        set(OPENGL_INCLUDES)
        set(GLAD_SOURCES)
        set(GLAD_INCLUDES)
        message(STATUS "✓ Android OpenGL: ENABLED")
    else()
        message(STATUS "✗ Android OpenGL: DISABLED")
    endif()
    # ============================================================

    # ============================================================
    # Vulkan for Android
    # ============================================================
    if(ENABLE_VULKAN)
        knst_add_platform_definition(KNST_USING_VULKAN)
        knst_add_platform_definition(KNST_PLATFORM_ANDROID_VULKAN)
        
        # Check if Vulkan is available in NDK
        if(EXISTS "${ANDROID_NDK}/sources/third_party/vulkan")
            set(VULKAN_LIBS vulkan)
            list(APPEND EXTRA_LIBS vulkan)
            set(VULKAN_INCLUDES)
            message(STATUS "✓ Android Vulkan: ENABLED")
        else()
            message(FATAL_ERROR "Vulkan not found in NDK. Please use NDK r24+ or install Vulkan support.")
        endif()
    else()
        message(STATUS "✗ Android Vulkan: DISABLED")
    endif()
    # ============================================================

    # ============================================================
    # No graphics backend
    # ============================================================
    if(NOT ENABLE_OPENGL AND NOT ENABLE_VULKAN)
        message(WARNING "⚠ Android Graphics: NONE (No OpenGL/Vulkan)")
    endif()

#*_____________________________________________________________
# Platform: Windows
#*_____________________________________________________________
elseif(WIN32)
    knst_add_platform_definition(KNST_USING_PLATFORM_WINDOWS)
    
    set(PLATFORM_TYPE "Windows")
    set(PLATFORM_EXTRA_LIBS)
    set(PROTOCOL_SOURCES)
    set(EXTRA_LIBS)
    
    set(PLATFORM_LIBS gdi32 user32 shell32 shcore ole32 oleaut32 uuid)
    set(PLATFORM_INCLUDES)
    
    # --- MSYS2 Detection ---
    set(MSYS2_ROOTS 
        "C:/msys64/ucrt64" 
        "C:/msys64/mingw64" 
        "C:/msys64/clang64"
        "$ENV{MSYS2_PATH}"
    )
    
    set(MSYS2_PATH "")
    foreach(ROOT ${MSYS2_ROOTS})
        if(ROOT AND EXISTS "${ROOT}/include")
            set(MSYS2_PATH "${ROOT}")
            list(APPEND PLATFORM_INCLUDES "${MSYS2_PATH}/include")
            link_directories("${MSYS2_PATH}/lib")
            message(STATUS "✓ MSYS2 found: ${MSYS2_PATH}")
            break()
        endif()
    endforeach()
    
    if(NOT MSYS2_PATH AND NOT MSVC)
        message(WARNING "MSYS2 not found. Some dependencies may not be available.")
    endif()

    # --- Vulkan SDK Detection ---
    set(VULKAN_SDK_SEARCH
        "$ENV{VULKAN_SDK}"
        "C:/VulkanSDK"
        "${MSYS2_PATH}"
    )
    
    set(VULKAN_SDK_PATH "")
    set(VULKAN_INCLUDE_DIR "")
    set(VULKAN_LIB_DIR "")
    
    foreach(PATH ${VULKAN_SDK_SEARCH})
        if(PATH AND EXISTS "${PATH}/Include/vulkan/vulkan.h")
            set(VULKAN_SDK_PATH "${PATH}")
            set(VULKAN_INCLUDE_DIR "${PATH}/Include")
            set(VULKAN_LIB_DIR "${PATH}/Lib")
            break()
        elseif(PATH AND EXISTS "${PATH}/include/vulkan/vulkan.h")
            set(VULKAN_SDK_PATH "${PATH}")
            set(VULKAN_INCLUDE_DIR "${PATH}/include")
            set(VULKAN_LIB_DIR "${PATH}/lib")
            break()
        endif()
    endforeach()
    
    # Handle VulkanSDK versioned directories
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
    
    if(VULKAN_SDK_PATH)
        message(STATUS "✓ Vulkan SDK found: ${VULKAN_SDK_PATH}")
    endif()

#*_____________________________________________________________
# Platform: Linux
#*_____________________________________________________________
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
    knst_add_platform_definition(KNST_USING_PLATFORM_LINUX)
    
    if(LINUX_PLATFORM STREQUAL "Wayland")
        knst_add_platform_definition(KNST_USING_LINUX_PLATFORM_WAYLAND)
        set(PLATFORM_TYPE "Wayland")
        
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(WAYLAND REQUIRED 
            wayland-client 
            wayland-cursor 
            wayland-egl 
            xkbcommon
        )
        
        set(PLATFORM_LIBS ${WAYLAND_LIBRARIES})
        set(PLATFORM_INCLUDES ${WAYLAND_INCLUDE_DIRS})
        
        # Wayland protocol sources
        set(PROTOCOL_SOURCES
            include/platform/linux/wayland/protocol_files/xdg-shell-protocol.c
            include/platform/linux/wayland/protocol_files/xdg-output-protocol.c
            include/platform/linux/wayland/protocol_files/xdg-decoration-protocol.c
            include/platform/linux/wayland/protocol_files/pointer-constraints-unstable-v1-protocol.c
            include/platform/linux/wayland/protocol_files/relative-pointer-unstable-v1-protocol.c
        )
        
        # Validate protocol sources exist
        foreach(PROTO_SRC ${PROTOCOL_SOURCES})
            if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${PROTO_SRC}")
                message(FATAL_ERROR "Wayland protocol source not found: ${PROTO_SRC}")
            endif()
        endforeach()
        
        set(EXTRA_LIBS wayland-client wayland-cursor wayland-egl xkbcommon)
        message(STATUS "✓ Wayland backend configured")
        
    else() # X11
        knst_add_platform_definition(KNST_USING_LINUX_PLATFORM_X11)
        set(PLATFORM_TYPE "X11")
        
        find_package(X11 REQUIRED)
        set(PLATFORM_LIBS ${X11_LIBRARIES})
        set(PLATFORM_INCLUDES ${X11_INCLUDE_DIR})
        
        # Additional X11 libraries
        find_package(PkgConfig REQUIRED)
        set(XCB_LIBS_TO_CHECK 
            x11-xcb 
            xcb 
            xcb-randr 
            xcb-keysyms 
            xcb-icccm 
            xcb-util 
            xext 
            xfixes 
            xi 
            xrandr 
            xcursor
        )
        
        foreach(LIB ${XCB_LIBS_TO_CHECK})
            string(REPLACE "-" "_" VAR "${LIB}")
            string(TOUPPER "${VAR}" VAR_UPPER)
            pkg_check_modules(PKG_${VAR_UPPER} QUIET ${LIB})
            
            if(PKG_${VAR_UPPER}_FOUND)
                list(APPEND PLATFORM_LIBS ${PKG_${VAR_UPPER}_LIBRARIES})
                list(APPEND PLATFORM_INCLUDES ${PKG_${VAR_UPPER}_INCLUDE_DIRS})
            else()
                # Try fallback: just link against the library name
                list(APPEND PLATFORM_LIBS ${LIB})
                message(WARNING "pkg-config for ${LIB} not found, attempting direct linking")
            endif()
        endforeach()
        
        set(PROTOCOL_SOURCES)
        set(EXTRA_LIBS)
        message(STATUS "✓ X11 backend configured")
    endif()

elseif(APPLE)
    message(FATAL_ERROR "macOS is not currently supported. Supported platforms: Windows, Linux, Android.")
else()
    message(FATAL_ERROR "Unsupported platform detected.")
endif()

#*_____________________________________________________________
# OpenGL Backend Configuration
#*_____________________________________________________________
# X11-specific OpenGL backend selection
if(ENABLE_OPENGL AND PLATFORM_TYPE STREQUAL "X11")
    set(OPENGL_BACKEND "GLX" CACHE STRING "OpenGL binding for X11 (GLX or EGL)")
    set_property(CACHE OPENGL_BACKEND PROPERTY STRINGS "GLX" "EGL")
    
    if(NOT OPENGL_BACKEND MATCHES "^(GLX|EGL)$")
        message(FATAL_ERROR
            "OPENGL_BACKEND must be 'GLX' or 'EGL' when using OpenGL on X11.\n"
            "Current value: '${OPENGL_BACKEND}'\n"
            "Pass -DOPENGL_BACKEND=GLX or -DOPENGL_BACKEND=EGL"
        )
    endif()
endif()

# Initialize OpenGL variables for non-Android platforms
if(NOT ANDROID)
    set(OPENGL_LIBS)
    set(OPENGL_INCLUDES)
    set(GLAD_SOURCES)
    set(GLAD_INCLUDES)
endif()

# OpenGL Configuration (Desktop)
if(ENABLE_OPENGL AND NOT ANDROID)
    knst_add_platform_definition(KNST_USING_OPENGL)
    
    if(WIN32)
        set(OPENGL_LIBS opengl32 glu32)
        message(STATUS "✓ OpenGL: Using native Windows OpenGL")
        
    elseif(UNIX AND NOT APPLE AND NOT ANDROID)
        find_package(OpenGL REQUIRED)
        set(OPENGL_LIBS ${OPENGL_LIBRARIES})
        set(OPENGL_INCLUDES ${OPENGL_INCLUDE_DIR})
        
        if(PLATFORM_TYPE STREQUAL "X11" AND OPENGL_BACKEND STREQUAL "EGL")
            knst_add_platform_definition(KNST_OPENGL_USING_EGL)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(EGL REQUIRED egl)
            list(APPEND OPENGL_LIBS ${EGL_LIBRARIES})
            list(APPEND OPENGL_INCLUDES ${EGL_INCLUDE_DIRS})
            message(STATUS "✓ OpenGL: Using EGL backend")
            
        elseif(PLATFORM_TYPE STREQUAL "X11")
            knst_add_platform_definition(KNST_OPENGL_USING_GLX)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(GLX REQUIRED glx)
            list(APPEND OPENGL_INCLUDES ${GLX_INCLUDE_DIRS})
            message(STATUS "✓ OpenGL: Using GLX backend")
            
        elseif(PLATFORM_TYPE STREQUAL "Wayland")
            knst_add_platform_definition(KNST_OPENGL_USING_EGL)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(EGL REQUIRED egl)
            pkg_check_modules(WAYLAND_EGL REQUIRED wayland-egl)
            pkg_check_modules(GLESV2 REQUIRED glesv2)
            
            list(APPEND OPENGL_LIBS ${EGL_LIBRARIES} ${WAYLAND_EGL_LIBRARIES} ${GLESV2_LIBRARIES})
            list(APPEND OPENGL_INCLUDES ${EGL_INCLUDE_DIRS} ${WAYLAND_EGL_INCLUDE_DIRS} ${GLESV2_INCLUDE_DIRS})
            message(STATUS "✓ OpenGL: Using EGL backend with Wayland")
        endif()
    endif()
    
    # GLAD setup
    set(GLAD_SOURCES glad_3_3/src/glad.c)
    set(GLAD_INCLUDES glad_3_3/include)
    
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${GLAD_SOURCES}")
        message(FATAL_ERROR
            "glad.c not found at '${CMAKE_SOURCE_DIR}/${GLAD_SOURCES}'.\n"
            "Make sure the glad_3_3/ folder is present at the project root.\n"
            "You can generate GLAD files from https://glad.dav1d.de/"
        )
    endif()
    message(STATUS "✓ GLAD loader: ${GLAD_SOURCES}")
endif()

#*_____________________________________________________________
# Vulkan Configuration
#*_____________________________________________________________
# Initialize Vulkan variables for non-Android platforms
if(NOT ANDROID)
    set(VULKAN_LIBS)
    set(VULKAN_INCLUDES)
endif()

if(ENABLE_VULKAN AND NOT ANDROID)
    knst_add_platform_definition(KNST_USING_VULKAN)
    
    if(WIN32)
        if(NOT VULKAN_SDK_PATH)
            message(FATAL_ERROR 
                "Vulkan SDK not found!\n"
                "Please install it from https://vulkan.lunarg.com/\n"
                "Or set VULKAN_SDK environment variable."
            )
        endif()
        
        set(VULKAN_LIBS vulkan-1)
        set(VULKAN_INCLUDES ${VULKAN_INCLUDE_DIR})
        message(STATUS "✓ Vulkan: ${VULKAN_SDK_PATH}")
        
    elseif(UNIX AND NOT APPLE AND NOT ANDROID)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(VULKAN QUIET vulkan)
        
        if(VULKAN_FOUND)
            set(VULKAN_LIBS ${VULKAN_LIBRARIES})
            set(VULKAN_INCLUDES ${VULKAN_INCLUDE_DIRS})
            message(STATUS "✓ Vulkan: Found via pkg-config")
        else()
            # Manual detection
            find_path(VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h)
            find_library(VULKAN_LIBRARY NAMES vulkan libvulkan)
            
            if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY)
                message(FATAL_ERROR 
                    "Vulkan not found!\n"
                    "Install vulkan-sdk or libvulkan-dev package."
                )
            endif()
            
            set(VULKAN_LIBS ${VULKAN_LIBRARY})
            set(VULKAN_INCLUDES ${VULKAN_INCLUDE_DIR})
            message(STATUS "✓ Vulkan: Found at ${VULKAN_INCLUDE_DIR}")
        endif()
    endif()
endif()

#*_____________________________________________________________
# Target Creation
#*_____________________________________________________________
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
    
    # Validate main source exists
    foreach(SRC ${SOURCES})
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${SRC}")
            message(FATAL_ERROR "Source file not found: ${SRC}")
        endif()
    endforeach()
    
    add_executable(knst_app ${SOURCES})
endif()

#*_____________________________________________________________
# Target Configuration
#*_____________________________________________________________
# Set language for C sources
if(GLAD_SOURCES)
    set_source_files_properties(${GLAD_SOURCES} PROPERTIES LANGUAGE C)
endif()

if(PROTOCOL_SOURCES)
    set_source_files_properties(${PROTOCOL_SOURCES} PROPERTIES LANGUAGE C)
endif()

# Include directories
target_include_directories(knst_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${PLATFORM_INCLUDES}
    ${GLAD_INCLUDES}
    ${VULKAN_INCLUDES}
    ${OPENGL_INCLUDES}
)

# Remove duplicate include directories
get_target_property(_INCLUDE_DIRS knst_app INCLUDE_DIRECTORIES)
list(REMOVE_DUPLICATES _INCLUDE_DIRS)
set_target_properties(knst_app PROPERTIES INCLUDE_DIRECTORIES "${_INCLUDE_DIRS}")

# Link directories
if(VULKAN_LIB_DIR)
    target_link_directories(knst_app PRIVATE ${VULKAN_LIB_DIR})
endif()

# Link libraries
target_link_libraries(knst_app PRIVATE
    ${PLATFORM_LIBS}
    ${OPENGL_LIBS}
    ${VULKAN_LIBS}
    ${EXTRA_LIBS}
)

# Platform-specific libraries
if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(knst_app PRIVATE dl pthread m)
endif()

if(WIN32 AND NOT MSVC)
    target_link_libraries(knst_app PRIVATE mingw32)
endif()

# Compiler-specific flags
if(MSVC)
    target_compile_options(knst_app PRIVATE /W4 /utf-8)
else()
    target_compile_options(knst_app PRIVATE -Wall -Wextra)
endif()

#*_____________________________________________________________
# Installation
#*_____________________________________________________________
if(NOT ANDROID)
    install(TARGETS knst_app
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
endif()

#*_____________________________________________________________
# Summary
#*_____________________________________________________________
message(STATUS "")
message(STATUS "========================================")
message(STATUS " KernelNucleusT v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS " Platform:      ${PLATFORM_TYPE}")
message(STATUS " Compiler:      ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS " Build Type:    ${CMAKE_BUILD_TYPE}")
message(STATUS " C++ Standard:  C++${CMAKE_CXX_STANDARD}")
message(STATUS " C Standard:    C${CMAKE_C_STANDARD}")

if(MSYS2_PATH)
    message(STATUS " MSYS2:         ${MSYS2_PATH}")
endif()

if(VULKAN_SDK_PATH)
    message(STATUS " Vulkan SDK:    ${VULKAN_SDK_PATH}")
endif()

message(STATUS "----------------------------------------")
message(STATUS " OpenGL:        ${ENABLE_OPENGL}")
message(STATUS " Vulkan:        ${ENABLE_VULKAN}")

if(ENABLE_OPENGL)
    if(PLATFORM_TYPE STREQUAL "X11")
        message(STATUS " GL Backend:    ${OPENGL_BACKEND}")
    elseif(PLATFORM_TYPE STREQUAL "Wayland")
        message(STATUS " GL Backend:    EGL (mandatory)")
    elseif(PLATFORM_TYPE STREQUAL "Android")
        message(STATUS " GL Backend:    EGL + GLES (mandatory)")
    endif()
    
    if(GLAD_SOURCES)
        message(STATUS " GLAD source:   ${GLAD_SOURCES}")
    endif()
endif()

if(ENABLE_SANITIZERS)
    message(STATUS "----------------------------------------")
    message(STATUS " Sanitizers:    ENABLED (ASan + UBSan)")
endif()

message(STATUS "----------------------------------------")
message(STATUS " Build:         ${CMAKE_BINARY_DIR}")
message(STATUS " Source:        ${CMAKE_SOURCE_DIR}")
message(STATUS "========================================")
message(STATUS "")