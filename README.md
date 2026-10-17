# KernelNucleusT

#### A high-performance, customizable, cross-platform library package written in C++20.

**Stable release is ready.** I've been working on this for months. Leak tests (ASan, UBSan, Valgrind), multi-platform tests, specific checks, real hardware tests... All passed successfully.

---

## Philosophy

KernelNucleusT is built on an **optimal balance between performance and safety**. Core principles:

- **noexcept + bool return** — Most functions return `bool` instead of throwing exceptions. This both improves performance and leaves error handling entirely in the developer's hands.

- **Force Inline** — By default, some performance-critical functions are compiled with `force_inline`. No call overhead, code is copied directly to the call site. Result: faster execution, but a slightly larger binary — your choice.

- **Adjustable Binary Size** — If `KNST_SMALL_SIZE_CLASS` is defined, standard `inline` is used instead of `force_inline`. The compiler decides for itself, and in most cases a `call` goes to the function. Binary size shrinks, speed may decrease. Your choice. Additionally, in general it offers the ability to customize the libraries however you want; the macros and their descriptions are in `include/knst_settings.hpp`.

- **Flexibility Per Situation** — Extra optimizations can be made on frequently-used critical functions at the cost of binary size. This isn't a bug — it's a deliberate choice. Our goal is to blend flexibility and performance for you in the best possible way, and the macros give you the ability to shape the library however you want.

This philosophy applies to all present and future structures in the package.

---

## For Detailed Documentation

In the project directory, under the `docs/` path, there are detailed documentation files under the `türkçe` (Turkish) and `english` folders for both simple and complex structures.

Additionally, under `docs/` there are `vscode_setting` files with ready-to-use configurations for each platform. For example: you can add the relevant setting via `CTRL + SHIFT + P` → `C/C++: Edit Configurations`.

---

## Supported Platforms

| Platform | Status | Tested |
|----------|:-----:|-------------|
| **Windows** (Win32/Win64) | ✅ | ✅ |
| **Linux X11** | ✅ | ✅ |
| **Linux Wayland** | ✅ | ✅ |
| **Linux Dual (X11 + Wayland, single binary)** | ✅ | ✅ |
| **Android** | ✅ | ✅ |

---

## Highlighted Features

- **Single library, many platforms** — Windows, Linux (X11/Wayland/Dual), Android.

- **Automatic Android Build Scripts** — SDK/NDK/device detection, build, APK packaging, installation and live logcat all in one command. `.sh` (Linux) and `.bat` (Windows) come ready.

- **Header-only** — All library files are .hpp
- **Modern C++20** — Effective in places like custom allocator support
- **Exception-free** — Most critical APIs return `bool`, `noexcept`. Error handling is entirely up to you.
- **Force Inline** — Critical functions are compiled with `KNST_FORCE_INLINE`. No overhead.
- **Adjustable Binary Size** — Shrink the binary with `KNST_SMALL_SIZE_CLASS`.
- **Dual-mode (X11 + Wayland)** — Two Linux backends in a single binary. Automatic selection at runtime.
- **CSD (Custom Title Bar)** — SDL3-style hit-test callback. Draw the title bar however you want.
- **Touch Screen** — Windows, X11, Wayland, Android — support is available on all of them.
- **IME (Input Method)** — XIM (X11), text-input-v3 (Wayland), JNI bridge (Android), TSF (Windows).
- **Modern Wayland** — `cursor-shape-v1`, `fractional-scale`, `viewporter`, `xdg-activation`, `primary-selection`, `idle-inhibit`, `text-input-v3`, `xdg-toplevel-icon`, `tablet-v2`.
- **Flicker-free X11 resize** — No flickering during window resize thanks to `_NET_WM_SYNC_REQUEST`.
- **Wayland frame callback gating** — Doesn't draw if the compositor isn't ready.
- **Windows Per-Monitor V2 DPI** — Sharp image on HiDPI screens.
- **OpenGL + Vulkan** — Two backends can be used independently or together. GLAD 3.3 Core comes with the library.
- **Correct UTF-16** — Emoji, CJK, Arabic, Turkish characters don't get mangled.

---

## What's Inside

### Complex Structures

- **knst_window** — window + event system
  - **OpenGL Core Backend** — `KNST_WINDOW_USE_OPENGL`
  - **Vulkan Backend** — `KNST_WINDOW_USE_VULKAN`
  - **Embedded (X11 + Wayland, single binary)** — `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND`
- **knst_vgui** <---------> (Coming very soon)

### Simple Structures

- knst_byte_string
- knst_c16string
- knst_devices
- knst_file
- knst_function
- knst_image_loader
- knst_process
- knst_thread
- knst_thread_pool
- knst_vector

Complex structures are built using the basic structures. Most simple structures depend on each other; complex structures depend on the simple structures.

---

## Window Backends

`knst_window` supports optional GPU backends. The two are **independent** and can be used **together**.

| Backend | CMake Option | Macro | Status |
|---------|--------------|-------|-------|
| **OpenGL** | `-DKNST_WINDOW_USE_OPENGL=ON` | `KNST_WINDOW_USE_OPENGL` | ✅ |
| **Vulkan** | `-DKNST_WINDOW_USE_VULKAN=ON` | `KNST_WINDOW_USE_VULKAN` | ✅ |

| What you want | Flags |
|-----------|-------|
| Basic window (no GPU backend) | (nothing) |
| OpenGL only | `-DKNST_WINDOW_USE_OPENGL=ON` |
| Vulkan only | `-DKNST_WINDOW_USE_VULKAN=ON` |
| Both | `-DKNST_WINDOW_USE_OPENGL=ON -DKNST_WINDOW_USE_VULKAN=ON` |

---

## Embedded Mode (X11 + Wayland, single binary)

One of the **most powerful features** of the library: compile **both X11 and Wayland** backends into the same binary, and at startup it automatically detects which one is active **at runtime**.

**Why?** The end user may not know which desktop session they're on. An X11-only binary doesn't run on Wayland; a Wayland-only binary doesn't run on X11. **Embedded mode** works seamlessly on both sessions with a single binary.

**Activation:**

```bash
cmake -S . -B build -G Ninja -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON
```

**Runtime selection order:**

1. Environment variable override — `KNST_WINDOW_FORCE_X11=1` or `KNST_WINDOW_FORCE_WAYLAND=1`
2. Wayland probe — is `WAYLAND_DISPLAY` + `wl_compositor` + `xdg_wm_base` present?
3. X11 probe — is `DISPLAY` set and can a connection be made?
4. None — `knst_backend::none`

**Force selection:**

```bash
KNST_WINDOW_FORCE_X11=1 ./build/bin/knst_app
KNST_WINDOW_FORCE_WAYLAND=1 ./build/bin/knst_app
```

**Features automatically selected in dual-mode:**

| Feature | X11 | Wayland |
|---------|:---:|:---:|
| Open window | ✅ | ✅ |
| Keyboard / mouse / wheel | ✅ | ✅ |
| CSD (Custom Title Bar) | ✅ | ✅ |
| Clipboard | ✅ | ✅ |
| Primary selection | ✅ | ✅ |
| Drag & Drop (Files) | ✅ | ✅ |
| OpenGL (EGL) | ✅ | ✅ |
| Vulkan | ✅ | ✅ |
| Touch Screen | ✅ (XInput2) | ✅ (wl_touch) |
| IME (Input Method) | ✅ (XIM) | ✅ (text-input-v3) |
| Raw mouse | ✅ (XInput2) | ✅ (relative-pointer) |
| Cursor shape | ✅ | ✅ (cursor-shape-v1) |
| Fractional scale | — | ✅ (fractional-scale) |
| Idle inhibit | — | ✅ (idle-inhibit) |

The user doesn't have to write `#if` — the library automatically picks the right backend in the background.

---

## Touch Screen

`knst_window` offers touch support on **all platforms**. Whether the device is physically touch-capable is detected **at runtime**.

| Platform | Mechanism
|----------|-----------
| **Windows** | `WM_TOUCH` + `RegisterTouchWindow`
| **Linux X11** | XInput2 `XI_TouchBegin/Update/End`
| **Linux Wayland** | `wl_touch` protocol
| **Android** | `AInputEvent` (built-in)

**Key features:**
- Runtime hardware detection (auto-enabled if present)
- Multi-finger support (10 points by default, adjustable with `KNST_MAX_TOUCH_POINTS`)
- Pressure, position, ID info
- Automatic mouse fallback when touch isn't active
- On Wayland, collected as a single event via `wl_touch.frame` — pinch/zoom works
- On X11, snapshot logic — multi-finger comes with stable IDs

See `docs/` for detailed API and event structure.

---

## CSD (Custom Title Bar)

The library **never draws the title bar** — that's your job. `knst_window` only does this:

1. You install a callback with `set_csd_hit_test()`.
2. On every left click the library calls the callback.
3. If callback returns `draggable` → the OS drags the window.
4. If `resize_*` → the OS resizes the window.
5. If `normal` → goes to your app as a normal mouse event (your buttons).

Every platform's native equivalent is chosen automatically:
- X11: `_NET_WM_MOVERESIZE`
- Wayland: `xdg_toplevel_move` / `xdg_toplevel_resize`
- Windows: `WM_NCHITTEST` → `HT*` codes

**A one-to-one equivalent of SDL3's `SDL_SetWindowHitTest()` API.** SDL knows its business :)

```cpp
window.set_title_bar_height(40);
window.set_csd_hit_test([](const knst_window& w, int x, int y) {
    if (y < 8) return knst_window::csd_hit::resize_top;
    if (y < 40) return knst_window::csd_hit::draggable;
    return knst_window::csd_hit::normal;
});
```

---



# KernelNucleusT – Installation Guide

This section explains how to build the KernelNucleusT library for **Linux (X11 / Wayland / Dual)**, **Windows** and **Android**, along with macro details.

---

## 1. Platform Selection

| Choice | What it does |
|---|---|
| **Platform** | Detected automatically. On Linux you must additionally choose whether **X11** or **Wayland** (or **both**) is used. |

---

## 2. All CMake Options

| Option | Default | Description |
|---|---|---|
| `KNST_LINUX_PLATFORM` | `X11` | Linux only: `X11` or `Wayland` |
| `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` | `OFF` | Linux: both backends in the same binary (runtime-selected) |
| `KNST_BUILD_EXAMPLES` | `OFF` | Also build the examples in the `examples/` folder |
| `KNST_APP_SOURCE` | Empty | Build your own `.cpp` file as `knst_app` |
| `KNST_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UBSan (for debugging) |
| `KNST_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as errors |
| `KNST_STATIC_MSVC_RUNTIME` | `ON` | MSVC only: static runtime (`/MT`) |
| `KNST_WINDOW_USE_OPENGL` | `OFF` | OpenGL 3.3 Core backend (GLAD + GL libs) |
| `KNST_WINDOW_USE_VULKAN` | `OFF` | Vulkan backend |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |

---

## 3. Which Macros Are Defined?

These macros are passed **automatically** to every project that uses the library. You don't need to define them manually.

| Situation | Defined macros |
|---|---|
| Linux + X11 | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11` |
| Linux + Wayland | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_WAYLAND` |
| Linux + Dual | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11`, `KNST_USING_LINUX_PLATFORM_WAYLAND`, `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` |
| Windows | `KNST_USING_PLATFORM_WINDOWS`, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `UNICODE`, `_UNICODE` |
| Android | `KNST_USING_PLATFORM_ANDROID`, `KNST_USING_PLATFORM_LINUX` |
| OpenGL enabled | `KNST_WINDOW_USE_OPENGL` |
| Vulkan enabled | `KNST_WINDOW_USE_VULKAN` |

Backend macros are **independent** — all can be defined together. At the end of the build, CMake prints a summary; you can see which macros are defined there.

---

## 4. Quick Start

```bash
git clone https://github.com/YusufGundar/KernelNucleusT
cd KernelNucleusT

# Configure (Linux X11, build your own .cpp)
cmake -S . -B build \
  -DKNST_APP_SOURCE=examples/simple_structures/knst_vector/knst_vector_basic.cpp

# Build
cmake --build build -j

# Run
./build/bin/knst_app
```

If you also want to build the library examples:

```bash
cmake -S . -B build -DKNST_BUILD_EXAMPLES=ON
cmake --build build -j
./build/bin/knst_window_basic
```

💡 In both cases, outputs land in the `build/bin/` folder. In example mode, each example becomes its own separate program with its own name (`knst_window_basic`, `knst_vector_basic`, etc.).

---

## 5. Linux

### 5.1 Required Tools

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
  libxext-dev libxfixes-dev libxi-dev libxrandr-dev libxcursor-dev libxcb-xinput-dev
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
cmake -S . -B build -DKNST_LINUX_PLATFORM=X11
cmake --build build -j
```

### 5.3 Packages for Wayland

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libwayland-dev libwayland-bin libxkbcommon-dev
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
cmake -S . -B build -DKNST_LINUX_PLATFORM=Wayland
cmake --build build -j
```

> Wayland protocol files (`.c` files) come ready in `include/platform/linux/wayland/protocol_files/`. CMake builds them itself. You don't really need to do anything.

### 5.4 Extra Packages for Embedded (X11 + Wayland)

You need **all** of the X11 **and** Wayland packages above. Additionally:

```bash
sudo apt install libwayland-bin wayland-protocols
```

Build:
```bash
cmake -S . -B build -G Ninja -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON
cmake --build build -j
```

Produces a single binary; picks Wayland on Wayland sessions, X11 on X11 sessions.

### 5.5 Extra Packages for OpenGL Backend

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev
```

**Fedora**
```bash
sudo dnf install mesa-libGL-devel mesa-libEGL-devel mesa-libGLES-devel
```

**Arch**
```bash
sudo pacman -S mesa
```

### 5.6 Extra Packages for Vulkan Backend

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libvulkan-dev vulkan-tools
```

**Fedora**
```bash
sudo dnf install vulkan-loader-devel vulkan-headers vulkan-tools
```

**Arch**
```bash
sudo pacman -S vulkan-headers vulkan-icd-loader vulkan-tools
```

### 5.7 GLAD (OpenGL Loader)

The library comes with **GLAD 3.3 Core** included. By default it's already in:

```
include/platform/knst_window/opengl_content/glad_3_3/
├── include/
│   ├── glad/glad.h
│   └── KHR/khrplatform.h
└── src/glad.c
```

When you pass `KNST_WINDOW_USE_OPENGL=ON`, CMake compiles these files automatically.

**If you want to use a different version / profile** (e.g. GL 4.6 Core, or GLES):

1. Pick the configuration you want at https://glad.dav1d.de/.
2. Replace the `include/` and `src/` folders in the downloaded zip into the same-named folders under `include/platform/knst_window/opengl_content/glad_3_3/`, additionally you can add your new download in place of the old one in `KernelNucleusT.hpp`.
3. Reconfigure with `rm -rf build && cmake -S . -B build ...`.

---

## 6. Windows

### 6.1 Option A: Visual Studio (MSVC)

Requirements:
- **Visual Studio 2022** (with the "Desktop development with C++" workload)
- **CMake** (comes with Visual Studio)

In "x64 Native Tools Command Prompt" or PowerShell:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Output: `build\bin\Release\`

### 6.2 Option B: MSYS2 (MinGW)

1. Install MSYS2 from https://www.msys2.org/.
2. Open the **MSYS2 UCRT64** terminal and install:

```bash
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja
```

3. Build:
```bash
cmake -S . -B build -G Ninja
cmake --build build
```
Output: `build/bin/`

### 6.3 OpenGL Backend (Windows)

No extra setup needed — `opengl32.dll` comes with Windows. GLAD comes with the library.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DKNST_WINDOW_USE_OPENGL=ON
cmake --build build --config Release
```

### 6.4 Vulkan Backend (Windows)

Requires the **Vulkan SDK** → https://vulkan.lunarg.com/

**Restart the terminal after installing** (for the `VULKAN_SDK` env var).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DKNST_WINDOW_USE_VULKAN=ON
cmake --build build --config Release
```

Vulkan in MSYS2:
```bash
pacman -S mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader
```

---

## 7. Android

### 7.1 Requirements

- **Android NDK** (r26 or newer recommended). Install via Android Studio → SDK Manager → SDK Tools → "NDK (Side by side)".
- **CMake** and **Ninja**
- Environment variable:
```bash
export ANDROID_NDK=$HOME/Android/Sdk/ndk/<version>
```
Windows PowerShell: `$env:ANDROID_NDK="C:\Users\<user>\AppData\Local\Android\Sdk\ndk\<version>"`

### 7.2 Build

Two paths: **ready-made scripts** (recommended) or **manual CMake**.

#### 7.2.1 Ready-made Scripts (Easiest Way)

There are 4 scripts under `examples/complex_structures/knst_window/android/`. They do the following, in order:

1. Auto-detect SDK + NDK + build-tools
2. Start the ADB server
3. Find the device via USB / mDNS / Wi-Fi / cache
4. CMake configure (skips if nothing changed)
5. Native build (Ninja parallel)
6. Package the APK (aapt2 + javac + d8 + zipalign + apksigner)
7. Install the APK on the device + launch the app
8. Start filtered live logcat (until Ctrl+C)

| Script | Backend | Platform |
|--------|---------|----------|
| `build-android-opengl.sh` | OpenGL ES | Linux |
| `build-android-opengl.bat` | OpenGL ES | Windows |
| `build-android-vulkan.sh` | Vulkan | Linux |
| `build-android-vulkan.bat` | Vulkan | Windows |

Usage:

```bash
# Linux / macOS — OpenGL
cd examples/complex_structures/knst_window/android
./build-android-opengl.sh

# Linux / macOS — Vulkan
./build-android-vulkan.sh
```

```bat
:: Windows — OpenGL
cd examples\complex_structures\knst_window\android
build-android-opengl.bat

:: Windows — Vulkan
build-android-vulkan.bat
```

If the script can't find the SDK, you can set the env vars beforehand:

```bash
export ANDROID_HOME=$HOME/Android/Sdk
export ANDROID_NDK=$ANDROID_HOME/ndk/26.1.10909125   # optional; newest is picked otherwise
```

```bat
set ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk
set ANDROID_NDK=%ANDROID_HOME%\ndk\26.1.10909125
```

If no device is connected, the script walks you through how to connect step by step (USB / VSCode extension / manual `adb pair`).

> 💡 The scripts keep their build dirs separate (`build-android-opengl/` or `build-android-vulkan/`). So you can install the OpenGL and Vulkan versions side by side on the same device — package names are different too (`com.knst.test.gl` vs `com.knst.test.vk`).

#### 7.2.2 Manual CMake

If you don't want to use the scripts, you can build the classic way:

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DKNST_BUILD_EXAMPLES=ON
cmake --build build-android
```

Output: `build-android/libknst_app.so`

Key points:
- `ANDROID_ABI` values: `arm64-v8a` (most phones), `armeabi-v7a`, `x86_64` (emulator).
- To use your own code: `-DKNST_APP_SOURCE=path/to/file.cpp`

> This path only produces the `.so` file. To package an APK, install it on a device and run it, either use the script or manually run the `aapt2` + `d8` + `zipalign` + `apksigner` chain.

### 7.3 OpenGL Backend (Android)

`libEGL.so` and `libGLESv2.so` come ready in the NDK, no extra setup. GLAD comes with the library.

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DKNST_WINDOW_USE_OPENGL=ON \
  -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_opengl_test.cpp
cmake --build build-android
```

> Note: **GLES 3.x** is used by default on Android (`opts.client_api = ClientAPI::OpenGLES`).

### 7.4 Vulkan Backend (Android)

Android API **24+** required (`libvulkan.so` comes with the NDK).

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DKNST_WINDOW_USE_VULKAN=ON \
  -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_vulkan_test.cpp
cmake --build build-android
```

> CMake errors out if `ANDROID_PLATFORM` is below `android-24`.

---

## 8. Using in Your Own Project

The library is header-only. The easiest path is `add_subdirectory`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApplication CXX)

# Meaningful on Linux
set(KNST_LINUX_PLATFORM "X11" CACHE STRING "" FORCE)

# Backend selection (optional)
set(KNST_WINDOW_USE_OPENGL ON  CACHE BOOL "" FORCE)
# set(KNST_WINDOW_USE_VULKAN ON CACHE BOOL "" FORCE)

add_subdirectory(KernelNucleusT)

add_executable(my_application main.cpp)
target_link_libraries(my_application PRIVATE KernelNucleusT::KernelNucleusT)
```

That one line is enough: include folders, platform macros, X11/Wayland libraries and backend dependencies are all wired up automatically.

---

## 9. Common Issues

| Issue | Solution |
|---|---|
| `Missing X11 development packages` | Install the packages from section 5.2, then `rm -rf build` and reconfigure. |
| `Wayland protocol source not found` | Make sure the `.c` files in `include/platform/linux/wayland/protocol_files/` haven't been deleted. |
| `Android NDK not found` | Pass `-DCMAKE_TOOLCHAIN_FILE=.../android.toolchain.cmake`. |
| `Vulkan was not found` | Install the Vulkan SDK (Windows) or `libvulkan-dev` (Linux). Restart the terminal. |
| OpenGL example doesn't open / segfault | Pay attention to init order: `Init → MakeCurrent → gladLoadGLLoader → gl*` calls. |
| Changed a setting but no effect | Clear the cache: `rm -rf build` (Windows: `rmdir /s /q build`) and reconfigure from scratch. |
| Which macros are defined? | Look at the **Definitions** row in the summary table printed by `cmake` at the end. |
| `ninja: command not found` (Windows) | Use the MSYS2 UCRT64 terminal, not plain PowerShell. |
| `cmake: command not found` (Windows) | In MSYS2 UCRT64: `pacman -S mingw-w64-ucrt-x86_64-cmake` |
| PowerShell splits a `-D` argument at `.cpp` | Wrap the argument in quotes: `"-DKNST_APP_SOURCE=path/file.cpp"` |
| `cl.exe not found` (MSVC) | Open "x64 Native Tools Command Prompt for VS", not plain PowerShell |
| I'm using Visual Studio 2026 | Generator name: `-G "Visual Studio 18 2026"` |

---

## 10. Ready-made Command Summary

> **If you use PowerShell:** wrap every argument that starts with `-D` and contains a `.` (dot) in double quotes.
> Otherwise PowerShell splits the argument at the `.cpp` part and CMake prints `Ignoring extra path from command line: ".cpp"`.

---

### 🔹 MSYS2 UCRT64 / PowerShell (Windows)

```powershell
#-DKNST_BUILD_EXAMPLES=ON to build all examples

# ------------------------------Basic Window-----------------------------------------
  cmake -S . -B build -G Ninja "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp"
  cmake --build build
  ./build/bin/knst_app.exe
# -----------------------------------------------------------------------------------

# ------------------------------OpenGL Backend---------------------------------------
  cmake -S . -B build -G Ninja "-DKNST_WINDOW_USE_OPENGL=ON" "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_opengl_basic.cpp"
  cmake --build build
  ./build/bin/knst_app.exe
# -----------------------------------------------------------------------------------

# ------------------------------Vulkan Backend---------------------------------------
  cmake -S . -B build -G Ninja "-DKNST_WINDOW_USE_VULKAN=ON" "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_vulkan_basic.cpp"
  cmake --build build
  ./build/bin/knst_app.exe
# -----------------------------------------------------------------------------------

# ------------------------------Build All Examples-----------------------------------
  cmake -S . -B build -G Ninja "-DKNST_BUILD_EXAMPLES=ON"
  cmake --build build
# -----------------------------------------------------------------------------------

# ------------------------------Build Your Own .cpp----------------------------------
  cmake -S . -B build -G Ninja "-DKNST_APP_SOURCE=path/to/file.cpp"
  cmake --build build
# -----------------------------------------------------------------------------------

# ------------------------------Reset the Build Folder-------------------------------
  Remove-Item -Recurse -Force build
# -----------------------------------------------------------------------------------
```

---

### 🔹 Windows MSVC (Visual Studio)

> **NOTE:** Run inside `"x64 Native Tools Command Prompt for VS"`. Not plain PowerShell.

```powershell
# Basic
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

# Your own .cpp
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

# OpenGL backend
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_WINDOW_USE_OPENGL=ON" "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_opengl_basic.cpp"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

# Vulkan backend
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_WINDOW_USE_VULKAN=ON" "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_vulkan_basic.cpp"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe
```

---

### 🔹 Linux / macOS (Bash)

```bash
# ------------------------------------X11--------------------------------------------
  # Build your own .cpp
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app

  # Build examples too
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_BUILD_EXAMPLES=ON
  cmake --build build -j
  ./build/bin/knst_window_basic
# -----------------------------------------------------------------------------------

# ------------------------------------Wayland-----------------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp # In Wayland, a window does not appear because Wayland must perform drawing operations for the window to become visible.
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------Dual (X11 + Wayland)----------------------------
  cmake -S . -B build -G Ninja -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp # In Wayland, a window does not appear because Wayland must perform drawing operations for the window to become visible.
  cmake --build build -j
  ./build/bin/knst_app              # Automatic selection
  KNST_WINDOW_FORCE_X11=1 ./build/bin/knst_app       # Force X11
  KNST_WINDOW_FORCE_WAYLAND=1 ./build/bin/knst_app   # Force Wayland
# -----------------------------------------------------------------------------------

# ------------------------------------OpenGL (X11)------------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_WINDOW_USE_OPENGL=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_opengl_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------OpenGL (Wayland)--------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_WINDOW_USE_OPENGL=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_opengl_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------Vulkan (X11)------------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_WINDOW_USE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_vulkan_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------Vulkan (Wayland)--------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_WINDOW_USE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_vulkan_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# Build All Examples
  cmake -S . -B build -G Ninja -DKNST_BUILD_EXAMPLES=ON
  cmake --build build
# -----------------------------------------------------------------------------------
```

---

### 🔹 Android

> **Easiest path:** run the ready-made script. SDK/NDK/device detection, build, APK, install and logcat all in one command.

```bash
# Ready-made script — Linux / macOS
  cd examples/complex_structures/knst_window/android
  ./build-android-opengl.sh     # OpenGL ES
  ./build-android-vulkan.sh     # Vulkan
```

```powershell
# Ready-made script — Windows
  cd examples\complex_structures\knst_window\android
  build-android-opengl.bat      # OpenGL ES
  build-android-vulkan.bat      # Vulkan
```

Manual CMake (only produces `.so` — no APK / install / logcat):

```bash
# Env var (Linux / macOS)
  export ANDROID_NDK=$HOME/Android/Sdk/ndk/<version>

# Env var (Windows PowerShell)
  $env:ANDROID_NDK="C:\Users\<user>\AppData\Local\Android\Sdk\ndk\<version>"

# Basic build
  cmake -S . -B build-android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
  cmake --build build-android

# OpenGL (GLES) backend
  cmake -S . -B build-android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DKNST_WINDOW_USE_OPENGL=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_opengl_test.cpp
  cmake --build build-android

# Vulkan backend
  cmake -S . -B build-android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DKNST_WINDOW_USE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_vulkan_test.cpp
  cmake --build build-android
```

---

### 📌 Rule Summary

| Shell | `-D...=.cpp` argument |
|-------|-----------------------------|
| **Bash (Linux/macOS)** | Works unquoted |
| **cmd.exe (Windows)** | Works unquoted |
| **PowerShell** | **Quoting required** |
| **MSYS2 UCRT64** | Works either way; quoting is guaranteed |

> **Why?** PowerShell sometimes treats the `.` character inside the path after `=` as a separate argument. Solution: wrap the whole `-D...=...` argument in `"..."`.

---

# Changelog

## v1.0.0 — Stable Release

The beta process is over. This is the **first stable** release of the library. Tested on all platforms (Windows, Linux X11, Linux Wayland, Linux Dual, Android) through leak tests and other tests, verified on real hardware. Many technical changes were made since the beta version. I've done my best, and along the way I realized how important comment lines and code layout really are, and how crucial it is to run every feature test and leak test on every platform.

Right now, on my machine, I have 2 different operating systems, an Android development environment, Linux (lots of desktop environments for X11/Wayland, specifically testing GNOME's title bar, etc.) — so comprehensive tests were done on every platform under every condition, fixed until the results were exactly what I wanted, and now here's `knst_window` — ready for you.


---

## 🤝 Contributing

For bug reports and feature requests, please use the **Issues** page. You can also reach out to me directly for requests, suggestions, and similar.


---

## 📄 License

- MPL 2.0