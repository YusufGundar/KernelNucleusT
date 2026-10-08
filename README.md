# KernelNucleusT (*Beta)

#### A high-performance, customizable, cross-platform library package written in C++20.

# knst_window — Supported Platforms

| Platform | Status |
|----------|:------:|
| **Windows** | ✅ |
| **Linux X11** | ✅ |
| **Linux Wayland** | ✅ |
| **Android** | ✅ |

Window creation, event handling, keyboard/mouse input, clipboard, and lifecycle have been tested on the platforms above.

## Contents

### Complex Structures

- knst_window
- knst_vgui <---------> (Coming soon)

### Simple Structures

- knst_byte_array
- knst_c16string
- knst_device
- knst_file
- knst_function
- knst_image_loader
- knst_process
- knst_thread
- knst_thread_pool
- knst_vector

#### The library bundles all of the above. Complex structures are built on top of the simple ones. Most simple structures depend on each other; complex structures depend on the simple ones.

## Philosophy

KernelNucleusT is built on an **optimal balance between performance and safety**. Core principles:

- **`noexcept` + `bool` return** — Most functions return `bool` instead of throwing exceptions. This improves performance and leaves full error control in the developer's hands.

- **Force Inline** — By default, some performance-critical functions are compiled with `force_inline`. There is no call overhead; the code is inlined directly at the call site. Result: faster execution, at the cost of a slightly larger binary — your choice.

- **Adjustable Binary Size** — If `KNST_SMALL_SIZE_CLASS` is defined, standard `inline` is used instead of `force_inline`. The compiler decides, and in most cases it will use a `call` to the function. The binary becomes smaller, but speed may drop. Your choice. Overall, the library can be customized however you like — macros and their descriptions are available in `include/knst_settings.hpp`.

- **Situational Flexibility** — For frequently used critical functions, extra optimizations can be made at the cost of binary size. This is not a bug, it is a deliberate choice. Our goal is to blend flexibility and performance for you in the best way possible, and the macros give you the ability to shape the library as you wish.

This philosophy applies to all current and future structures in the package.

---

## For Detailed Documentation

#### Inside the `docs/` directory of the project, you will find documentation for simple and complex structures under the `türkçe` (Turkish) and `english` folders. Additionally, under `docs/vscode_setting` you will find ready-to-use settings for each platform.

For example: with `CTRL + SHIFT + P`, open `C/C++: Edit Configurations` and apply the setting.

---

# KernelNucleusT – Installation Guide

This section walks you through building the KernelNucleusT library for **Linux (X11 / Wayland)**, **Windows**, and **Android**, along with macro details, step by step.

---

## 1. There are essentially 2 choices

| Choice | What it does |
|---|---|
| **Platform** | Detected automatically. On Linux you additionally need to choose between **X11** and **Wayland**. |
| **Graphics mode** | Choose **Vulkan** (`-DKNST_ENABLE_VULKAN=ON`) or **Headless** (`-DKNST_ENABLE_VULKAN=OFF`, the default). |

---

## 2. All CMake options

| Option | Default | Description |
|---|---|---|
| `KNST_ENABLE_VULKAN` | `OFF` | `ON` = build with Vulkan, `OFF` = headless |
| `KNST_LINUX_PLATFORM` | `X11` | Linux only: `X11` or `Wayland` |
| `KNST_BUILD_EXAMPLES` | `OFF` | Also build the examples under `examples/` |
| `KNST_APP_SOURCE` | Empty | Build your own `.cpp` file as `knst_app` |
| `KNST_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UBSan (for debugging) |
| `KNST_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as errors |
| `KNST_STATIC_MSVC_RUNTIME` | `ON` | MSVC only: static runtime (`/MT`) |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |

---

## 3. Which macros are defined?

These macros are automatically propagated to every project that consumes the library. You do not need to define them manually.

| Situation | Defined macros |
|---|---|
| Linux + X11 | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11` |
| Linux + Wayland | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_WAYLAND` |
| Windows | `KNST_USING_PLATFORM_WINDOWS`, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `UNICODE`, `_UNICODE` |
| Android | `KNST_USING_PLATFORM_ANDROID`, `KNST_USING_PLATFORM_LINUX` |
| Vulkan on | `KNST_USING_VULKAN` (also `KNST_PLATFORM_ANDROID_VULKAN` on Android) |
| Headless | `KNST_HEADLESS_MODE` is defined; the Vulkan macro is **not** defined |

During configuration, CMake prints a summary at the end; you can see which macros were defined there.

---

## 4. Quick start (small example)

```bash
git clone https://github.com/YusufGundar/KernelNucleusT
cd KernelNucleusT

# Configure (Linux X11 + Vulkan, build your own .cpp file)
cmake -S . -B build \
  -DKNST_ENABLE_VULKAN=ON \
  -DKNST_APP_SOURCE=examples/simple_structures/knst_vector/knst_vector_basic.cpp

# Build
cmake --build build -j

# Run (example)
./build/bin/knst_app
```

### If you also want to build the library examples

```bash
cmake -S . -B build -DKNST_ENABLE_VULKAN=ON -DKNST_BUILD_EXAMPLES=ON

cmake --build build -j

./build/bin/knst_window_basic
```

💡 Note: In both cases the outputs land in `build/bin/`. In example mode, each example becomes its own separate program with its own name (e.g. `knst_window_basic`, `knst_vector_basic`).

---

## 5. Linux

### 5.1 Required tools

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config
```

**Fedora**
```bash
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config
```

**Arch**
```bash
sudo pacman -S base-devel cmake ninja pkgconf
```

### 5.2 Packages for X11

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libx11-dev libx11-xcb-dev libxcb1-dev libxcb-randr0-dev \
  libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-util-dev libxcb-sync-dev \
  libxext-dev libxfixes-dev libxi-dev libxrandr-dev libxcursor-dev
```

**Fedora**
```bash
sudo dnf install libX11-devel libxcb-devel xcb-util-devel xcb-util-keysyms-devel \
  xcb-util-wm-devel libXext-devel libXfixes-devel libXi-devel libXrandr-devel libXcursor-devel
```

**Arch**
```bash
sudo pacman -S libx11 libxcb xcb-util xcb-util-keysyms xcb-util-wm \
  libxext libxfixes libxi libxrandr libxcursor
```

Build:
```bash
cmake -S . -B build -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=ON
cmake --build build -j
```

### 5.3 Packages for Wayland

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libwayland-dev libxkbcommon-dev
```

**Fedora**
```bash
sudo dnf install wayland-devel libxkbcommon-devel
```

**Arch**
```bash
sudo pacman -S wayland libxkbcommon
```

Build:
```bash
cmake -S . -B build -DKNST_LINUX_PLATFORM=Wayland -DKNST_ENABLE_VULKAN=ON
cmake --build build -j
```

> Wayland protocol files (the `.c` files) ship ready-made under `include/platform/linux/wayland/protocol_files/`. CMake compiles them for you — you do not need to do anything.

### 5.4 Packages for Vulkan (Linux)

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libvulkan-dev vulkan-tools
# Optional (debug layers):
sudo apt install vulkan-validationlayers
```

**Fedora**
```bash
sudo dnf install vulkan-loader-devel vulkan-headers vulkan-tools
```

**Arch**
```bash
sudo pacman -S vulkan-headers vulkan-icd-loader vulkan-tools
```

Check: `vulkaninfo --summary` should show your GPU. Up-to-date drivers must be installed for NVIDIA / AMD / Intel.

### 5.5 Headless on Linux

No GPU or Vulkan package required:
```bash
cmake -S . -B build -DKNST_ENABLE_VULKAN=OFF
cmake --build build -j
```

---

## 6. Windows

### 6.1 Option A: Visual Studio (MSVC) – recommended

Requirements:
- **Visual Studio 2022** (with the "Desktop development with C++" workload)
- **CMake** (comes bundled with Visual Studio)
- If you use Vulkan, install the **Vulkan SDK**: https://vulkan.lunarg.com/ (close and reopen the terminal after installation; the `VULKAN_SDK` environment variable is set automatically)

In "x64 Native Tools Command Prompt" or PowerShell:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_ENABLE_VULKAN=ON"
cmake --build build --config Release
```
Output: `build\bin\Release\`

For headless, use `-DKNST_ENABLE_VULKAN=OFF` — the Vulkan SDK is not required.

### 6.2 Option B: MSYS2 (MinGW)

1. Install MSYS2 from https://www.msys2.org/.
2. Open the **MSYS2 UCRT64** terminal and install:

Extra: `pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`

```bash
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja
# For Vulkan:
pacman -S mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader
```
3. Build:
```bash
cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=ON
cmake --build build
```
Output: `build/bin/`

---

## 7. Android

### 7.1 Requirements
- **Android NDK** (r26 or newer recommended). Install via Android Studio → SDK Manager → SDK Tools → "NDK (Side by side)".
- **CMake** and **Ninja**
- Environment variable (adjust the path to your installation):
```bash
export ANDROID_NDK=$HOME/Android/Sdk/ndk/<version>
```
Windows PowerShell: `$env:ANDROID_NDK="C:\Users\<username>\AppData\Local\Android\Sdk\ndk\<version>"`

### 7.2 Build (with Vulkan)

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DKNST_ENABLE_VULKAN=ON \
  -DKNST_BUILD_EXAMPLES=ON
cmake --build build-android
```

Output: `build-android/libknst_app.so`

Important notes:
- **Vulkan requires at least `android-24`.** Lower values will cause CMake to error.
- `ANDROID_ABI` values: `arm64-v8a` (most phones), `armeabi-v7a`, `x86_64` (emulator).
- For headless, use `-DKNST_ENABLE_VULKAN=OFF`.
- To use your own code: `-DKNST_APP_SOURCE=path/to/your.cpp`

Ready-made `build-android` scripts are provided. Once you have the environment set up, you can just run those scripts to establish the connection.

## 8. Using in your own project

The library is header-only. The easiest way is `add_subdirectory`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApplication CXX)

set(KNST_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
set(KNST_LINUX_PLATFORM "X11" CACHE STRING "" FORCE)   # only meaningful on Linux

add_subdirectory(KernelNucleusT)

add_executable(my_application main.cpp)
target_link_libraries(my_application PRIVATE KernelNucleusT::KernelNucleusT)
```

That one line is enough: include directories, platform macros, `KNST_USING_VULKAN`, and X11/Wayland/Vulkan libraries are all wired up automatically. In your code:

```cpp
#include "KernelNucleusT.hpp"

#if defined(KNST_USING_VULKAN)
    // Vulkan mode
#else
    // Headless mode
#endif
```

---

## 9. Common issues

| Problem | Solution |
|---|---|
| `Missing X11 development packages` | Install the packages in section 5.2, then `rm -rf build` and reconfigure. |
| `KNST_ENABLE_VULKAN=ON but Vulkan was not found` | Install the Vulkan package / SDK (section 5.4 or 6), or use `-DKNST_ENABLE_VULKAN=OFF`. |
| `Wayland protocol source not found` | Make sure the `.c` files under `include/platform/linux/wayland/protocol_files/` have not been deleted. |
| `Android NDK not found` | Pass `-DCMAKE_TOOLCHAIN_FILE=.../android.toolchain.cmake`. |
| `Vulkan requires Android API level 24` | Use `-DANDROID_PLATFORM=android-24` (or newer). |
| Vulkan not found on Windows | Install the Vulkan SDK, close and reopen the terminal (so `VULKAN_SDK` is loaded). |
| Changed a setting but nothing happens | Clear the cache: `rm -rf build` (Windows: `rmdir /s /q build`) and reconfigure. |
| Which macros are defined? | Check the **Definitions** line in the summary printed at the end of the CMake output. |
| `ninja: command not found` (Windows) | Use the MSYS2 UCRT64 terminal, not normal PowerShell. Or: `pacman -S mingw-w64-ucrt-x86_64-ninja` |
| `cmake: command not found` (Windows) | In MSYS2 UCRT64: `pacman -S mingw-w64-ucrt-x86_64-cmake` |
| PowerShell splits `-D` argument at the `.cpp` | Wrap the argument in quotes: `"-DKNST_APP_SOURCE=path/file.cpp"` |
| `cl.exe not found` (MSVC) | Open "x64 Native Tools Command Prompt for VS", not regular PowerShell |
| I use Visual Studio 2026 | Generator name: `-G "Visual Studio 18 2026"` |

---

## 10. Ready-made command summary

```bash

#-DKNST_BUILD_EXAMPLES=ON to build all examples
# ------------------------------------X11--------------------------------------------
  # Vulkan
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app

  # Headless
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=OFF -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app

  # Build examples too (X11 + Vulkan)
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=ON -DKNST_BUILD_EXAMPLES=ON  
  cmake --build build -j
  ./build/bin/knst_window_basic

  # Build examples too (X11 + Headless)
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=OFF -DKNST_BUILD_EXAMPLES=ON
  cmake --build build -j
  ./build/bin/knst_vector_basic
# ------------------------------------------------------------------------------------

# ------------------------------------Wayland-----------------------------------------
  # Vulkan 
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_ENABLE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app

  # Headless
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_ENABLE_VULKAN=OFF
  cmake --build build -j
  ./build/bin/knst_app
# ------------------------------------------------------------------------------------

# ------------------------------------Windows MSVC------------------------------------
# NOTE: Run inside "x64 Native Tools Command Prompt for VS"
# NOTE: Wrap -D arguments in quotes (PowerShell trap)

  # Vulkan
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_ENABLE_VULKAN=ON"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

  # Headless
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_ENABLE_VULKAN=OFF"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

  # With your own .cpp file
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
    "-DKNST_ENABLE_VULKAN=ON" ^
    "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe
# ------------------------------------------------------------------------------------

# --------------------------------Windows (MSYS2 / MinGW)-----------------------------
# NOTE: Not regular PowerShell — use the "MSYS2 UCRT64" terminal
# NOTE: No quotes needed in Bash, -D arguments are not split

  # Vulkan
  cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build
  ./build/bin/knst_app.exe

  # Headless
  cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=OFF -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build
  ./build/bin/knst_app.exe
# -----------------------------------------------------------------------------------

# ------------------------------------Android----------------------------------------
  # Environment variable (Linux / macOS)
  export ANDROID_NDK=$HOME/Android/Sdk/ndk/<version>

  # Environment variable (Windows PowerShell)
  $env:ANDROID_NDK="C:\Users\<username>\AppData\Local\Android\Sdk\ndk\<version>"

  # Vulkan
  cmake -S . -B build-android -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DKNST_ENABLE_VULKAN=ON
  cmake --build build-android

  # Headless
  cmake -S . -B build-android -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DKNST_ENABLE_VULKAN=OFF
  cmake --build build-android
# -----------------------------------------------------------------------------------

# Build your own .cpp file
  cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=ON \
    -DKNST_APP_SOURCE=path/your_file.cpp
  cmake --build build
# -----------------------------------------------------------------------------------

# Build all examples
  cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=ON -DKNST_BUILD_EXAMPLES=ON
  cmake --build build
# -----------------------------------------------------------------------------------
```

## 🤝 Contributing

#### Use the **Issues** page for bug reports and feature requests.

#### If you would like to support the project directly

  [![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-ffdd00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/developeryk)

---

## 📄 License

- MPL 2.0