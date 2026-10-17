{
    // ========================================================================
    //  KernelNucleusT — VS Code C/C++ IntelliSense configuration
    //
    //  This file only affects IntelliSense (code completion, error squiggles,
    //  go-to-definition). It does NOT drive the actual build — CMakeLists.txt
    //  does. Treat these profiles as "reference setups" that mirror the CMake
    //  options so the editor matches the compiler.
    //
    //  Quick tips:
    //    - Ctrl+Shift+P -> "C/C++: Select IntelliSense Configuration"
    //      picks which profile below is active.
    //    - If something is red but compiles fine, switch to the
    //      "CMake (compile_commands.json)" profile — it just reads what
    //      CMake already produced and can't drift out of sync.
    //    - NDK / MSVC / VULKAN_SDK paths are placeholders. Adjust them to
    //      match your local installs if they differ.
    // ========================================================================

    "configurations": [
        {
            // ------------------------------------------------------------------
            //  Recommended — reads the generated compile_commands.json.
            //  Run CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON to produce it.
            //  Everything else in this file is a fallback for when that
            //  file doesn't exist yet.
            // ------------------------------------------------------------------
            "name": "CMake (compile_commands.json - recommended)",
            "compileCommands": "${workspaceFolder}/build/compile_commands.json",
            "includePath": [
                "${workspaceFolder}/include"
            ],
            "defines": [
                "KNST_WINDOW_USE_OPENGL",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "cStandard": "c11",
            "cppStandard": "c++20"
        },

        // ==================================================================
        //  Linux — X11
        // ==================================================================
        {
            // X11 base. No GPU backend — pure window/event testing.
            "name": "Linux X11 (Basic)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_X11"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            // X11 + OpenGL. Adds GLAD, EGL, GL, GLES headers.
            "name": "Linux X11 (OpenGL)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/EGL",
                "/usr/include/GL",
                "/usr/include/GLES2",
                "/usr/include/GLES3",
                "/usr/include/KHR"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_WINDOW_USE_OPENGL"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            // X11 + Vulkan. Vulkan loader headers come from the system
            // (libvulkan-dev / vulkan-headers).
            "name": "Linux X11 (Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/vulkan"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            // X11 + both backends at once.
            "name": "Linux X11 (OpenGL + Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/EGL",
                "/usr/include/GL",
                "/usr/include/GLES2",
                "/usr/include/GLES3",
                "/usr/include/KHR",
                "/usr/include/vulkan"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_WINDOW_USE_OPENGL",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },

        // ==================================================================
        //  Linux — Wayland
        //  The protocol_files/ folder contains wayland-scanner generated
        //  headers (xdg-shell, viewporter, cursor-shape, ...). IntelliSense
        //  needs it on the include path or the .hpp files stay red.
        // ==================================================================
        {
            "name": "Linux Wayland (Basic)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/linux/wayland/protocol_files",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_WAYLAND"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux Wayland (OpenGL)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "${workspaceFolder}/include/platform/linux/wayland/protocol_files",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/EGL",
                "/usr/include/GL",
                "/usr/include/GLES2",
                "/usr/include/GLES3",
                "/usr/include/KHR"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_WINDOW_USE_OPENGL"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux Wayland (Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/linux/wayland/protocol_files",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/vulkan"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux Wayland (OpenGL + Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "${workspaceFolder}/include/platform/linux/wayland/protocol_files",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/EGL",
                "/usr/include/GL",
                "/usr/include/GLES2",
                "/usr/include/GLES3",
                "/usr/include/KHR",
                "/usr/include/vulkan"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_WINDOW_USE_OPENGL",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },

        // ==================================================================
        //  Linux — Dual (X11 + Wayland)
        //  Both backends compile in. Runtime picks one via
        //  KnstWindowSources::detect_backend().
        // ==================================================================
        {
            "name": "Linux Dual (X11 + Wayland, Basic)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/linux/wayland/protocol_files",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux Dual (X11 + Wayland, OpenGL)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "${workspaceFolder}/include/platform/linux/wayland/protocol_files",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/EGL",
                "/usr/include/GL",
                "/usr/include/GLES2",
                "/usr/include/GLES3",
                "/usr/include/KHR"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND",
                "KNST_WINDOW_USE_OPENGL"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux Dual (X11 + Wayland, Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/linux/wayland/protocol_files",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/vulkan"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux Dual (X11 + Wayland, OpenGL + Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "${workspaceFolder}/include/platform/linux/wayland/protocol_files",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/EGL",
                "/usr/include/GL",
                "/usr/include/GLES2",
                "/usr/include/GLES3",
                "/usr/include/KHR",
                "/usr/include/vulkan"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND",
                "KNST_WINDOW_USE_OPENGL",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },

        // ==================================================================
        //  Android (NDK)
        //  Paths assume the NDK lives at /opt/android-ndk. Adjust if your
        //  install is elsewhere — most distro packages put it under
        //  $ANDROID_HOME/ndk/<version>.
        // ==================================================================
        {
            // Base NDK headers only. Enough for the plain window/event code.
            "name": "Android (Basic)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android",
                "/opt/android-ndk/sources/android/native_app_glue"
            ],
            "defines": [
                "KNST_USING_PLATFORM_ANDROID",
                "KNST_USING_PLATFORM_LINUX"
            ],
            "compilerPath": "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-clang-arm64"
        },
        {
            // NDK + GLAD + GLES3 headers.
            "name": "Android (OpenGL)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/EGL",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES2",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES3",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/KHR",
                "/opt/android-ndk/sources/android/native_app_glue"
            ],
            "defines": [
                "KNST_USING_PLATFORM_ANDROID",
                "KNST_USING_PLATFORM_LINUX",
                "KNST_WINDOW_USE_OPENGL"
            ],
            "compilerPath": "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-clang-arm64"
        },
        {
            // NDK + Vulkan headers.
            "name": "Android (Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/vulkan",
                "/opt/android-ndk/sources/android/native_app_glue"
            ],
            "defines": [
                "KNST_USING_PLATFORM_ANDROID",
                "KNST_USING_PLATFORM_LINUX",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-clang-arm64"
        },
        {
            // NDK + both backends.
            "name": "Android (OpenGL + Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/EGL",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES2",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES3",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/KHR",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/vulkan",
                "/opt/android-ndk/sources/android/native_app_glue"
            ],
            "defines": [
                "KNST_USING_PLATFORM_ANDROID",
                "KNST_USING_PLATFORM_LINUX",
                "KNST_WINDOW_USE_OPENGL",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-clang-arm64"
        },

        // ==================================================================
        //  Windows — MSVC (Visual Studio)
        //  Uses env: glob patterns so it works across VS versions.
        // ==================================================================
        {
            "name": "Windows (MSVC Basic)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${env:ProgramFiles(x86)}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/um",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/shared",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/winrt"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "NOMINMAX",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE"
            ],
            "compilerPath": "cl.exe",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "windows-msvc-x64"
        },
        {
            "name": "Windows (MSVC OpenGL)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "${env:ProgramFiles(x86)}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/um",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/shared",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/winrt",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/um/gl",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/shared/KHR"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "NOMINMAX",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE",
                "KNST_WINDOW_USE_OPENGL"
            ],
            "compilerPath": "cl.exe",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "windows-msvc-x64"
        },
        {
            // Requires VULKAN_SDK env var (set by the Vulkan SDK installer).
            "name": "Windows (MSVC Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${env:ProgramFiles(x86)}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/um",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/shared",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/winrt",
                "${env:VULKAN_SDK}/Include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "NOMINMAX",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "cl.exe",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "windows-msvc-x64"
        },
        {
            "name": "Windows (MSVC OpenGL + Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "${env:ProgramFiles(x86)}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/um",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/shared",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/winrt",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/um/gl",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/shared/KHR",
                "${env:VULKAN_SDK}/Include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "NOMINMAX",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE",
                "KNST_WINDOW_USE_OPENGL",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "cl.exe",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "windows-msvc-x64"
        },

        // ==================================================================
        //  Windows — MinGW / MSYS2
        //  Paths assume MSYS2 is installed at C:/msys64. Change if needed.
        // ==================================================================
        {
            "name": "Windows (MinGW / MSYS2 Basic)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "C:/msys64/ucrt64/include",
                "C:/msys64/mingw64/include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "NOMINMAX",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE"
            ],
            "compilerPath": "g++.exe",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "windows-gcc-x64"
        },
        {
            "name": "Windows (MinGW / MSYS2 OpenGL)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "C:/msys64/ucrt64/include",
                "C:/msys64/ucrt64/include/GL",
                "C:/msys64/ucrt64/include/KHR",
                "C:/msys64/mingw64/include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "NOMINMAX",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE",
                "KNST_WINDOW_USE_OPENGL"
            ],
            "compilerPath": "g++.exe",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "windows-gcc-x64"
        },
        {
            "name": "Windows (MinGW / MSYS2 Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "C:/msys64/ucrt64/include",
                "C:/msys64/ucrt64/include/vulkan",
                "C:/msys64/mingw64/include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "NOMINMAX",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "g++.exe",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "windows-gcc-x64"
        },
        {
            "name": "Windows (MinGW / MSYS2 OpenGL + Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/knst_window/opengl_content/glad_3_3/include",
                "C:/msys64/ucrt64/include",
                "C:/msys64/ucrt64/include/GL",
                "C:/msys64/ucrt64/include/KHR",
                "C:/msys64/ucrt64/include/vulkan",
                "C:/msys64/mingw64/include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "NOMINMAX",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE",
                "KNST_WINDOW_USE_OPENGL",
                "KNST_WINDOW_USE_VULKAN"
            ],
            "compilerPath": "g++.exe",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "windows-gcc-x64"
        }
    ],
    "version": 4
}