```json
{
    "configurations": [
        {
            "name": "CMake (compile_commands.json - recommended)",
            "compileCommands": "${workspaceFolder}/build/compile_commands.json",
            "includePath": [
                "${workspaceFolder}/include"
            ],
            "cStandard": "c11",
            "cppStandard": "c++20"
        },
        {
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
                "KNST_PLATFORM_ANDROID_VULKAN",
                "KNST_USING_VULKAN"
            ],
            "compilerPath": "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-clang-arm64"
        },
        {
            "name": "Android (Headless - No Graphics)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android",
                "/opt/android-ndk/sources/android/native_app_glue"
            ],
            "defines": [
                "KNST_USING_PLATFORM_ANDROID",
                "KNST_USING_PLATFORM_LINUX",
                "KNST_HEADLESS_MODE"
            ],
            "compilerPath": "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-clang-arm64"
        },
        {
            "name": "Windows (MSVC - Vulkan)",
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
                "KNST_USING_VULKAN",
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
            "name": "Windows (MSVC - Headless)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${env:ProgramFiles(x86)}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/um",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/shared"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "KNST_HEADLESS_MODE",
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
            "name": "Windows (MinGW / MSYS2 - Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "C:/msys64/ucrt64/include",
                "C:/msys64/mingw64/include",
                "${env:VULKAN_SDK}/Include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "KNST_USING_VULKAN",
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
            "name": "Windows (MinGW / MSYS2 - Headless)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "C:/msys64/ucrt64/include",
                "C:/msys64/mingw64/include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "KNST_HEADLESS_MODE",
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
                "KNST_USING_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux X11 (Headless)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_HEADLESS_MODE"
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
                "KNST_USING_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux Wayland (Headless)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${workspaceFolder}/include/platform/linux/wayland/protocol_files",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu"
            ],
            "defines": [
                "KNST_USING_PLATFORM_LINUX",
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_HEADLESS_MODE"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++20",
            "intelliSenseMode": "linux-gcc-x64"
        }
    ],
    "version": 4
}
```