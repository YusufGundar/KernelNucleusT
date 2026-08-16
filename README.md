# KernelNucleusT

A modern C++17 library package. A high-performance, customizable, cross-platform library package

## Included Components

### Packaged Structures

- **knst_window** — Provides support for Window / Linux (X11 / Wayland) / Android

---

### Core Structures

- **knst_c16string**
- **knst_byte_array**
- **knst_vector**
- **knst_memory**
- **knst_image_loader** — Currently only `BMP` format is supported

These classes are included within the package.

## Philosophy

KernelNucleusT is built upon the principle of **optimal balance between performance and security**. Core principles:

- **noexcept + bool return** — Most functions return `bool` instead of throwing exceptions. This increases performance and gives the developer full control over error handling.

- **Force Inline** — Functions are compiled with `force_inline` by default. There is no call overhead; code is copied directly to the call site. Result: faster execution, but larger binary size.

- **Configurable Binary Size** — If `KNST_SMALL_SIZE_CLASS` is defined, `force_inline` is replaced with standard `inline`. The compiler decides when to inline, usually using `call` to invoke functions. Binary size decreases, speed may decrease. The choice is yours. Additionally, the library offers extensive customization options, and `knst_window` provides custom title bar themes for OpenGL content.

- **Contextual Flexibility** — Extra optimizations may be applied to frequently used critical functions at the cost of binary size. This is not a bug, but a conscious design choice. Our goal is to harmonize flexibility and performance in the best way possible. Macros also provide the ability to shape the library according to your needs.

This philosophy applies to all current and future libraries within the package.

## 🚀 Libraries

## knst_window (Beta)

**A comprehensive window management library with full support for Windows, Linux, and Android, aiming to provide as many features as possible.**

**Features:**
- **`force inline` is used in most critical and frequently called sections during application runtime**
- **Provides a clean coding experience with modern C++ features**
- **Event handling mechanism similar to the operating system's event model**
- **Security — Has successfully passed a limited but rigorous set of tests**

---

## knst_c16string

A high-performance `char16_t`-based string class.
**By default, it stores up to 22 bytes (10 characters) on the stack; beyond that, it moves to the heap.**

**Features:**
- **Multi-Character Support** — Directly accepts `char16_t`, `char`, `wchar_t`, `char32_t` types in constructors and functions.
- **STL Compatible** — Works seamlessly with `std::string`, `std::wstring`, `std::u16string`, `std::u32string`, and their views.
- **Macro Customization:**
  - `KNST_C16STRING_DEACTIVE_COW` — Disables Copy-On-Write
  - `KNST_C16_STRING_USING_ATOMIC_COW` — Makes COW counter thread-safe
  - `KNST_C16STRING_ALIGN_64` / `KNST_C16STRING_ALIGN_32` — Changes class alignment and SSO capacity
- **Security** — Successfully passed an extensive test suite

---

## knst_byte_string

An `unsigned char`-based alternative to `knst_c16string` for storing binary data and UTF-8 values. Future features for network communication will be added.

- **UTF-8 Support:** Stores as raw bytes
- **Security** — Successfully passed tests
- **Binary Safe:** Can contain `\0` (null) bytes

---

## knst_vector

Currently provides only the methods required by the library's internal needs; methods are limited. New methods for user-facing usage will be added.

- **Security** — Successfully passed tests
- **Memory** — Designed to work with `knst_memory`

---

## knst_memory

An instance-based memory pool allocator. Thread-safe option is available. All future libraries will also support `knst_memory`.

---

## Goals

I plan to design a GUI framework library in the future, along with regex-supported structures and cross-platform file I/O primitives.

---

## 📦 Installation


### WINDOWS (MSVC)

```bash
# OpenGL
- cmake -B build -DENABLE_OPENGL=ON
- cmake --build build --config Release

# Vulkan
cmake -B build -DENABLE_VULKAN=ON
cmake --build build --config Release

# OpenGL + Vulkan
cmake -B build -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build --config Release

# Run
build/knst_app.exe
```


### WINDOWS (MinGW / MSYS2)

```bash
# OpenGL
cmake -B build -G "MinGW Makefiles" -DENABLE_OPENGL=ON
cmake --build build

# Vulkan
cmake -B build -G "MinGW Makefiles" -DENABLE_VULKAN=ON
cmake --build build

# OpenGL + Vulkan
cmake -B build -G "MinGW Makefiles" -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build

# Run
build/knst_app.exe
```



### WINDOWS (Ninja / MSYS2)

```bash
# OpenGL
cmake -B build -G Ninja -DENABLE_OPENGL=ON
cmake --build build

# Vulkan
cmake -B build -G Ninja -DENABLE_VULKAN=ON
cmake --build build

# OpenGL + Vulkan
cmake -B build -G Ninja -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build

# Run
build/knst_app.exe
```



### LINUX / X11

```bash
# X11 + OpenGL (GLX)
cmake -B build -DLINUX_PLATFORM=X11 -DOPENGL_BACKEND=GLX -DENABLE_OPENGL=ON
cmake --build build

# X11 + OpenGL (EGL)
cmake -B build -DLINUX_PLATFORM=X11 -DOPENGL_BACKEND=EGL -DENABLE_OPENGL=ON
cmake --build build

# X11 + Vulkan
cmake -B build -DLINUX_PLATFORM=X11 -DENABLE_VULKAN=ON
cmake --build build

# X11 + OpenGL (GLX) + Vulkan
cmake -B build -DLINUX_PLATFORM=X11 -DOPENGL_BACKEND=GLX -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build

# X11 + OpenGL (EGL) + Vulkan
cmake -B build -DLINUX_PLATFORM=X11 -DOPENGL_BACKEND=EGL -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build

# Run
build/./knst_app
```

### LINUX / WAYLAND
```bash
# Wayland + OpenGL (EGL)
cmake -B build -DLINUX_PLATFORM=Wayland -DENABLE_OPENGL=ON
cmake --build build

# Wayland + Vulkan
cmake -B build -DLINUX_PLATFORM=Wayland -DENABLE_VULKAN=ON
cmake --build build

# Wayland + OpenGL (EGL) + Vulkan
cmake -B build -DLINUX_PLATFORM=Wayland -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build

# Run
build/./knst_app
```

### ANDROID

```bash
# Linux

    package_tests/knst_window/android/./build_android.sh  # Builds the APK
    package_tests/knst_window/android/./run_android.sh    # Runs the APK on the device

# Windows

    package_tests/knst_window/android/./build_android.bat # Builds the APK
    package_tests/knst_window/android/./run_android.bat   # Runs the APK on the device
```


 📌 **NOTE:** For Android: You must download the Android SDK and Android NDK and configure them with your compiler. Example VS Code JSON configuration:

```json
{
    "configurations": [
        {
            "name": "Android",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/EGL",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES3",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES2",
                "/opt/android-ndk/sources/android/native_app_glue"
            ],
            "defines": [
                "KNST_USING_PLATFORM_ANDROID",
                "KNST_PLATFORM_ANDROID_OPENGL"
            ],
            "compilerPath": "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-clang-arm64"
        },
        {
            "name": "Windows (MSVC)",
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
                "KNST_USING_OPENGL",
                "KNST_OPENGL_USING_WGL",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE"
            ],
            "compilerPath": "cl.exe",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "windows-msvc-x64"
        },
        {
            "name": "Windows (MinGW)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "C:/msys64/mingw64/include",
                "C:/msys64/mingw64/include/SDL2",
                "C:/msys64/mingw64/include/GL",
                "C:/mingw-w64/x86_64-8.1.0-posix-seh-rt_v6-rev0/mingw64/include",
                "C:/mingw64/include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "KNST_USING_OPENGL"
            ],
            "compilerPath": "g++.exe",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "windows-gcc-x64"
        },
        {
            "name": "Linux (X11)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/lib/gcc/x86_64-linux-gnu/*/include",
                "${workspaceFolder}/include/linux/x11"
            ],
            "defines": [
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_USING_OPENGL",
                "KNST_OPENGL_USING_GLX"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux (Wayland)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "${workspaceFolder}/include/linux/wayland",
                "/usr/include/wayland-client"
            ],
            "defines": [
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_USING_OPENGL",
                "KNST_OPENGL_USING_EGL"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux (Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/vulkan"
            ],
            "defines": [
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_USING_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-gcc-x64"
        }
    ],
    "version": 4
}
```
📌 **NOTE:** You can use the following JSON configuration as an example:

---
---
```cpp
#include "../include/KernelNucleusT.hpp" // That's all
```

- You can directly include the `KernelNucleusT.hpp` header, or alternatively, you can use only the specific structures you need in a separate project.

```cpp

// All core structures depend on `knst_global_functions.hpp`

#include "knst_global_functions.hpp"

#include "knst_c16string.hpp"
#include "knst_byte_string.hpp"
#include "knst_vector.hpp"
#include "knst_image_loader.hpp"
```

```cpp
#include "knst_window.hpp" // Package libraries depend on the core structures
```

## 🤝 Contributing

- Use the **Issues** page for bug reports and feature requests.
- For any special requests or if you'd like to get in touch with me directly, you can reach me via email.
- If you would like to help me
- [![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-ffdd00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/developeryk)


## 📄 License

- MIT License — Use freely, develop, and share.
