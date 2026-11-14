# KernelNucleusT

<p align="center">
  <!-- Core -->
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white" alt="C++20"/>
  <img src="https://img.shields.io/badge/Header--Only-yes-success" alt="Header-Only"/>
  <img src="https://img.shields.io/badge/License-MPL_2.0-orange" alt="License"/>
  <img src="https://img.shields.io/badge/Version-2.0.0--alpha.1-success" alt="Version 2.0.0-alpha.1"/>
</p>

<p align="center">
  <!-- Platforms -->
  <img src="https://img.shields.io/badge/Windows-tested-3DDC84?logo=windows&logoColor=white" alt="Windows"/>
  <img src="https://img.shields.io/badge/Linux_X11-tested-3DDC84?logo=linux&logoColor=white" alt="Linux X11"/>
  <img src="https://img.shields.io/badge/Linux_Wayland-tested-3DDC84?logo=linux&logoColor=white" alt="Linux Wayland"/>
  <img src="https://img.shields.io/badge/Android-tested-3DDC84?logo=android&logoColor=white" alt="Android"/>
</p>

<p align="center">
  <!-- Graphics -->
  <img src="https://img.shields.io/badge/OpenGL-3.3_Core-5586A4?logo=opengl&logoColor=white" alt="OpenGL"/>
  <img src="https://img.shields.io/badge/Vulkan-supported-success?logo=vulkan&logoColor=white" alt="Vulkan"/>
</p>

<p align="center">
  <!-- Networking -->
  <img src="https://img.shields.io/badge/TLS-1.2_%2F_1.3-success" alt="TLS"/>
  <img src="https://img.shields.io/badge/HTTP-1.1_%7C_2_%7C_3-brightgreen" alt="HTTP"/>
  <img src="https://img.shields.io/badge/WebSocket-RFC_6455-green" alt="WebSocket"/>
</p>

<p align="center">
  <!-- Quality -->
  <img src="https://img.shields.io/badge/ASan-clean-success" alt="ASan clean"/>
  <img src="https://img.shields.io/badge/UBSan-clean-success" alt="UBSan clean"/>
  <img src="https://img.shields.io/badge/Valgrind-clean-success" alt="Valgrind clean"/>
</p>

#### A high-performance, customizable, cross-platform library package written in C++20.

**Stable release ready.** I've been working on this for months. Leak tests (ASan, UBSan, Valgrind), multi-platform tests, custom checks, real hardware tests... All passed successfully.

---

## Philosophy

KernelNucleusT is built on the **optimum balance between performance and safety**. Core principles:

- **noexcept + bool return** — Most functions return `bool` instead of throwing exceptions. This way both performance improves and error control remains entirely in the developer's hands.

- **Force Inline** — By default, some performance-critical functions are compiled with `force_inline`. No call overhead, code is inlined directly at the call site. Result: faster execution, but a slightly larger binary — your choice.

- **Adjustable Binary Size** — If `KNST_SMALL_SIZE_CLASS` is defined, standard `inline` is used instead of `force_inline`. The compiler decides on its own, and in most cases it emits a `call` to the function. Binary size shrinks, speed may decrease. Your choice. In general, the library offers extensive customization options; macros and their descriptions live in `include/knst_settings.hpp`.

- **Flexibility by Situation** — Additional optimizations can be applied to frequently-used critical functions at the cost of binary size. This is not a bug, it's a deliberate trade-off. Our goal is to blend flexibility and performance for you in the best possible way, and macros let you shape the library however you want.

This philosophy applies to all current and future structures in the package.

---

## For Detailed Documentation

In the `docs/` directory of the project, detailed documentation for both simple and complex structures is available under `turkish` and `english` folders.

Additionally, in `docs/` under `vscode_setting`, platform-specific ready-made settings are available. For example: you can add the relevant setting via `CTRL + SHIFT + P` → `C/C++: Edit Configurations`.

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

## Highlights

- **One library, many platforms** — Windows, Linux (X11/Wayland/Dual), Android.

- **Automatic Android Build Scripts** — SDK/NDK/device detection, build, APK packaging, install, and live logcat in one command. `.sh` (Linux) and `.bat` (Windows) ship ready to use.

- **Header-only** — All library files are `.hpp`
- **Modern C++20** — Leveraged effectively in places like custom allocator support
- **Exception-free** — Most critical APIs return `bool` and are `noexcept`. Error control is entirely yours.
- **Force Inline** — Critical functions compile with `KNST_FORCE_INLINE`. Zero overhead.
- **Adjustable Binary Size** — Shrink the binary with `KNST_SMALL_SIZE_CLASS`.
- **Dual-mode (X11 + Wayland)** — Two Linux backends in one binary. Automatic selection at runtime.
- **CSD (Custom Title Bar)** — SDL3-style hit-test callback. Draw the title bar however you like.
- **Touch Screen** — Windows, X11, Wayland, Android — supported everywhere.
- **IME (Input Method)** — XIM (X11), text-input-v3 (Wayland), JNI bridge (Android), TSF (Windows).
- **Modern Wayland** — `cursor-shape-v1`, `fractional-scale`, `viewporter`, `xdg-activation`, `primary-selection`, `idle-inhibit`, `text-input-v3`, `xdg-toplevel-icon`, `tablet-v2`.
- **Flicker-free X11 resize** — No flickering while resizing thanks to `_NET_WM_SYNC_REQUEST`.
- **Wayland frame callback gating** — Doesn't draw when the compositor isn't ready.
- **Windows Per-Monitor V2 DPI** — Sharp image on HiDPI screens.
- **OpenGL + Vulkan** — Two backends usable independently or together. GLAD 3.3 Core ships with the library.
- **UTF-16 correct** — Emoji, CJK, Arabic, Turkish characters don't get corrupted.

- **Networking** — HTTP/1.1, HTTP/2, HTTP/3 (QUIC), WebSocket (`ws://` + `wss://`), TLS 1.2/1.3, DoH (DNS over HTTPS), thread-safe DNS cache, HTTP CONNECT + SOCKS5/SOCKS5h proxy, cookie jar, gzip/deflate decoding, SSRF guard, `bind_local_ip`.

- **Auto-detection** — `knst_network` layers (TLS / H2 / H3) are **detected automatically at CMake configure time**. You don't pass `-D..._ENABLE_*` flags manually. Verify via the `Network: TLS=TRUE H2=TRUE H3=FALSE` line at the end of CMake output.

- **Self-contained TLS** — On Linux/macOS the system CA bundle loads automatically, on Windows the WinCertStore (ROOT store) is imported, on Android `/apex/com.android.conscrypt/cacerts` + `/system/etc/security/cacerts` are read. mTLS (`knst_tls_set_client_cert`) and custom CA support (`knst_tls_set_ca_file` / `knst_tls_set_ca_path`) are ready.

- **Automatic audio codec detection** — If `libfaad2` is present, AAC decode activates; if `libvorbis` + `libvorbisenc` + `libogg` are present, native OGG Vorbis encode activates. CMake finds them automatically.

---

## What's Inside

### Complex Structures

- **knst_window** — window + event system
  - **OpenGL Core Backend** — `KNST_WINDOW_USE_OPENGL`
  - **Vulkan Backend** — `KNST_WINDOW_USE_VULKAN`
  - **Embedded (X11 + Wayland, single binary)** — `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND`
- **knst_vgui** <---------> (Coming very soon)

### Simple Structures

- knst_byte_string.hpp
- knst_c16string.hpp
- knst_devices.hpp
- knst_file.hpp
- knst_function.hpp
- knst_image_loader.hpp
- knst_process.hpp
- knst_thread.hpp
- knst_thread_pool.hpp
- knst_vector.hpp
- knst_json.hpp
- knst_video.hpp
- knst_camera.hpp
- knst_audio.hpp
- knst_microphone.hpp
- knst_converter.hpp
- knst_bluetooth.hpp
- knst_usb.hpp
- knst_storage.hpp
- knst_sensors.hpp
- knst_printer.hpp
- knst_network.hpp       — HTTP/1.1 client, DNS cache, DoH, proxy (HTTP CONNECT + SOCKS5), TLS
- knst_websocket.hpp     — WebSocket (ws:// + wss://), full RFC 6455, fragmented messages
- knst_network_h2.hpp    — HTTP/2 (nghttp2-based, h2 ALPN, automatic session pool)
- knst_network_h3.hpp    — HTTP/3 (QUIC, ngtcp2 + nghttp3, TLS 1.3 mandatory)


Complex structures are built on top of simple structures. Most simple structures depend on each other; complex structures depend on simple ones.

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

One of the library's **most powerful features**: compile **both X11 and Wayland** backends into the same binary and have it **automatically detect at runtime** which one is active when the app starts.

**Why?** The end user may not know which desktop session they're in. An X11-only binary won't run on Wayland; a Wayland-only binary won't run on X11. **Embedded mode** works seamlessly in both sessions with a single binary.

**Activation:**

```bash
cmake -S . -B build -G Ninja -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON
```

**Runtime selection order:**

1. Environment variable override — `KNST_WINDOW_FORCE_X11=1` or `KNST_WINDOW_FORCE_WAYLAND=1`
2. Wayland probe — `WAYLAND_DISPLAY` + `wl_compositor` + `xdg_wm_base` present?
3. X11 probe — `DISPLAY` set and connection establishable?
4. None — `knst_backend::none`

**Forced selection:**

```bash
KNST_WINDOW_FORCE_X11=1 ./build/bin/knst_app
KNST_WINDOW_FORCE_WAYLAND=1 ./build/bin/knst_app
```

**Features auto-selected in dual-mode:**

| Feature | X11 | Wayland |
|---------|:---:|:---:|
| Window opening | ✅ | ✅ |
| Keyboard / mouse / wheel | ✅ | ✅ |
| CSD (Custom Title Bar) | ✅ | ✅ |
| Clipboard | ✅ | ✅ |
| Primary selection | ✅ | ✅ |
| Drag & Drop (File) | ✅ | ✅ |
| OpenGL (EGL) | ✅ | ✅ |
| Vulkan | ✅ | ✅ |
| Touch Screen | ✅ (XInput2) | ✅ (wl_touch) |
| IME (Input Method) | ✅ (XIM) | ✅ (text-input-v3) |
| Raw mouse | ✅ (XInput2) | ✅ (relative-pointer) |
| Cursor shape | ✅ | ✅ (cursor-shape-v1) |
| Fractional scale | — | ✅ (fractional-scale) |
| Idle inhibit | — | ✅ (idle-inhibit) |

The user doesn't have to write `#if` — the library automatically picks the correct backend behind the scenes.

---

## Touch Screen

`knst_window` provides **touch support on all platforms**. Whether the device is physically touch-capable is **detected at runtime**.

| Platform | Mechanism
|----------|-----------
| **Windows** | `WM_TOUCH` + `RegisterTouchWindow`
| **Linux X11** | XInput2 `XI_TouchBegin/Update/End`
| **Linux Wayland** | `wl_touch` protocol
| **Android** | `AInputEvent` (built-in)

**Key features:**
- Hardware detection at runtime (auto-active if present)
- Multi-finger support (default 10 points, configurable via `KNST_MAX_TOUCH_POINTS`)
- Pressure, position, ID info
- Automatic mouse fallback when touch isn't active
- On Wayland, `wl_touch.frame` batches events into one — pinch/zoom works
- On X11, snapshot logic — multi-finger arrives with stable IDs

See the `docs/` folder for detailed API and event structures.

---

## CSD (Custom Title Bar)

The library **never draws the title bar** — that's your job. `knst_window` only does this:

1. You install a callback via `set_csd_hit_test()`.
2. On every left click, the library calls your callback.
3. If the callback returns `draggable` → the OS drags the window.
4. If it returns `resize_*` → the OS resizes the window.
5. If it returns `normal` → it goes to your app as a normal mouse event (your buttons).

The native counterpart is chosen automatically on each platform:
- X11: `_NET_WM_MOVERESIZE`
- Wayland: `xdg_toplevel_move` / `xdg_toplevel_resize`
- Windows: `WM_NCHITTEST` → `HT*` codes

**A one-to-one equivalent of SDL3's `SDL_SetWindowHitTest()` API.** SDL knows what it's doing :)

```cpp
window.set_title_bar_height(40);
window.set_csd_hit_test([](const knst_window& w, int x, int y) {
    if (y < 8) return knst_window::csd_hit::resize_top;
    if (y < 40) return knst_window::csd_hit::draggable;
    return knst_window::csd_hit::normal;
});
```

---



# KernelNucleusT – Installation Information

This section explains how to build the KernelNucleusT library for **Linux (X11 / Wayland / Dual)**, **Windows**, and **Android**, along with macro details.

---

## 1. Platform Selection

| Selection | What it does |
|---|---|
| **Platform** | Detected automatically. On Linux you also need to choose whether you want **X11**, **Wayland**, or **both**. |

---

## 2. All CMake Options

| Option | Default | Description |
|---|---|---|
| `KNST_LINUX_PLATFORM` | `X11` | Linux only: `X11` or `Wayland` |
| `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` | `OFF` | Linux: both backends in same binary (runtime-selected) |
| `KNST_APP_SOURCE` | Empty | Compile your own `.cpp` file as `knst_app` |
| `KNST_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UBSan (for debugging) |
| `KNST_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as errors |
| `KNST_STATIC_MSVC_RUNTIME` | `ON` | MSVC only: static runtime (`/MT`) |
| `KNST_WINDOW_USE_OPENGL` | `OFF` | OpenGL 3.3 Core backend (GLAD + GL libs) |
| `KNST_WINDOW_USE_VULKAN` | `OFF` | Vulkan backend |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |

### 2.1 Network Options (optional — auto-detected)

> **Important:** `KNST_NETWORK_ENABLE_TLS/H2/H3` are **NOT user-facing CMake flags**. They're **internal** macros on the header side. CMakeLists automatically adds them to `target_compile_definitions` when it finds the corresponding libraries.

| Option | Default | Description |
|---|---|---|
| `KNST_NETWORK_DISABLE_TLS` | `OFF` | Disable TLS entirely even if OpenSSL is installed (H2 + H3 also close) |
| `KNST_NETWORK_DISABLE_H2` | `OFF` | Disable HTTP/2 even if nghttp2 is installed |
| `KNST_NETWORK_DISABLE_H3` | `OFF` | Disable HTTP/3 even if quictls+ngtcp2+nghttp3 are installed |
| `KNST_QUICTLS_ROOT` | (auto) | quictls prefix override (for H3) |
| `KNST_NGTCP2_ROOT` | (auto) | ngtcp2 prefix override |
| `KNST_NGHTTP3_ROOT` | (auto) | nghttp3 prefix override |
| `KNST_NGHTTP2_ROOT` | (auto) | nghttp2 prefix override |

**Auto-detection order** (for each library):
1. `find_package(<name> CONFIG)` — vcpkg / Conan / cmake-installed
2. `pkg_check_modules(<pc-name>)` — Linux / pkg-config
3. `find_library` + `find_path` — last resort

**Tier ladder** (highest to lowest):
- **H3** → quictls + nghttp2 + ngtcp2 + nghttp3 (TLS + H2 + H3)
- **H2** → system OpenSSL + nghttp2 (TLS + H2)
- **H1** → system OpenSSL (TLS + H1)
- **off** → none (no network TLS)

Appears at the end of CMake output after configure:
```
Network:        TLS=TRUE H2=TRUE H3=FALSE
```

> **⚠️ H3 + OpenSSL ABI warning:** When H3 is active, CMakeLists **deliberately links only quictls** and **disables system OpenSSL**. Two OpenSSLs never coexist in the same binary — ABI clash is prevented. At runtime, RPATH prepends the quictls directory.

---

## 3. Which Macros Are Defined?

These macros **automatically** propagate to every project that uses the library. No manual definition needed.

| Situation | Defined macros |
|---|---|
| Linux + X11 | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11` |
| Linux + Wayland | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_WAYLAND` |
| Linux + Dual | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11`, `KNST_USING_LINUX_PLATFORM_WAYLAND`, `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` |
| Windows | `KNST_USING_PLATFORM_WINDOWS`, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `UNICODE`, `_UNICODE` |
| Android | `KNST_USING_PLATFORM_ANDROID`, `KNST_USING_PLATFORM_LINUX` |
| OpenGL enabled | `KNST_WINDOW_USE_OPENGL` |
| Vulkan enabled | `KNST_WINDOW_USE_VULKAN` |
| OpenSSL found | `KNST_NETWORK_ENABLE_TLS` |
| nghttp2 found | `KNST_NETWORK_ENABLE_H2` |
| quictls + ngtcp2 + nghttp3 found | `KNST_NETWORK_ENABLE_H3` |
| FAAD2 found | `KNST_NETWORK_HAVE_FAAD2` (AAC decode) |
| Vorbis found | `KNST_NETWORK_HAVE_VORBIS` (OGG encode) |

Backend and network macros are **independent** — all can be defined together. During build, CMake prints a summary at the end; you can see which macros are defined there.

> **Note:** All network macros are **auto-detected**. Users don't pass them via `-D`. Verify via the `Network: TLS=TRUE H2=TRUE H3=FALSE` line in CMake configure output.

---

## 4. Quick Start

```bash
git clone https://github.com/YusufGundar/KernelNucleusT
cd KernelNucleusT

# Configure (Linux X11, compile your own .cpp file)
cmake -S . -B build \
  -DKNST_APP_SOURCE=examples/simple_structures/knst_vector/knst_vector_basic.cpp

# Build
cmake --build build -j

# Run
./build/bin/knst_app
```
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

> Wayland protocol files (`.c` files) come ready in `include/platform/linux/wayland/protocol_files/`. CMake compiles them itself. You don't need to do much.

### 5.4 Extra Packages for Embedded (X11 + Wayland)

You need **all** the X11 **and** Wayland packages above. Additionally:

```bash
sudo apt install libwayland-bin wayland-protocols
```

Build:
```bash
cmake -S . -B build -G Ninja -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON
cmake --build build -j
```

One binary comes out; it picks Wayland on Wayland sessions and X11 on X11 sessions.

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

The library ships with **GLAD 3.3 Core**. It's ready by default in this directory:

```
include/platform/knst_window/opengl_content/glad_3_3/
├── include/
│   ├── glad/glad.h
│   └── KHR/khrplatform.h
└── src/glad.c
```

When you pass `KNST_WINDOW_USE_OPENGL=ON`, CMake compiles these files automatically.

**If you want a different version / profile** (e.g. GL 4.6 Core, or GLES):

1. Pick the configuration you want at https://glad.dav1d.de/.
2. Replace the `include/` and `src/` folders inside the downloaded zip with the same-named folders under `include/platform/knst_window/opengl_content/glad_3_3/`. Additionally, in `KernelNucleusT.hpp`, add your new file instead of the old one.
3. Reconfigure with `rm -rf build && cmake -S . -B build ...`.

### 5.8 Internet Libraries (Linux)

> **Note:** The libraries in this section are **optional**. Only install the layer you need. CMake **auto-detects** — there is **no** flag like `-DKNST_NETWORK_ENABLE_TLS=ON`.

#### 5.8.1 TLS (HTTPS / WSS)

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libssl-dev
```

**Fedora**
```bash
sudo dnf install openssl-devel
```

**Arch**
```bash
sudo pacman -S openssl
```

CMake detects automatically after you install:
```
Network:        TLS=TRUE H2=FALSE H3=FALSE
```

If you **don't** want TLS (rare):
```bash
cmake -S . -B build -DKNST_NETWORK_DISABLE_TLS=ON ...
```

#### 5.8.2 HTTP/2 (`knst_network_h2.hpp`)

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libnghttp2-dev
```

**Fedora**
```bash
sudo dnf install nghttp2-devel
```

**Arch**
```bash
sudo pacman -S nghttp2
```

TLS + H2 activate automatically after install:
```
Network:        TLS=TRUE H2=TRUE H3=FALSE
```

Include the header in your project: `#include "knst_network_h2.hpp"` (or `KernelNucleusT.hpp`).

#### 5.8.3 HTTP/3 (`knst_network_h3.hpp`) — The hardest part

HTTP/3 requires **4 libraries**: `quictls` + `ngtcp2` + `ngtcp2_crypto_quictls` + `nghttp3`. **Stock OpenSSL 3.0 is not enough** — it lacks QUIC support, so the `quictls` fork is required.

**Ubuntu 24.04+ / Fedora 39+ / Arch:**
```bash
# Ubuntu 24.04+
sudo apt install libngtcp2-dev libngtcp2-crypto-quictls-dev libnghttp3-dev

# Fedora 39+
sudo dnf install ngtcp2-devel nghttp3-devel

# Arch
sudo pacman -S ngtcp2 nghttp3
```

**Ubuntu 22.04 / 20.04 / Pop!_OS (not in stock repos):**

You have two options:

**Option 1 — vcpkg (recommended):**
```bash
git clone https://github.com/microsoft/vcpkg
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install quictls nghttp2 ngtcp2 nghttp3
```

Then CMake configure:
```bash
cmake -S . -B build -G Ninja \
    -DKNST_QUICTLS_ROOT=$PWD/vcpkg/installed/x64-linux \
    ...
```

**Option 2 — build from source:**
```bash
# 1) quictls (OpenSSL fork, QUIC-capable)
git clone https://github.com/quictls/openssl.git /tmp/quictls
cd /tmp/quictls
git checkout openssl-3.1.5+quic
./config --prefix=/usr/local/quictls --openssldir=/usr/local/quictls/ssl shared
make -j$(nproc)
sudo make install_sw

# 2) nghttp3
git clone --recursive https://github.com/ngtcp2/nghttp3.git /tmp/nghttp3
cd /tmp/nghttp3
autoreconf -i
./configure --prefix=/usr/local PKG_CONFIG_PATH=/usr/local/quictls/lib/pkgconfig
make -j$(nproc)
sudo make install

# 3) ngtcp2
git clone --recursive https://github.com/ngtcp2/ngtcp2.git /tmp/ngtcp2
cd /tmp/ngtcp2
autoreconf -i
./configure --prefix=/usr/local --with-openssl=/usr/local/quictls \
    PKG_CONFIG_PATH=/usr/local/quictls/lib/pkgconfig:/usr/local/lib/pkgconfig
make -j$(nproc)
sudo make install

# 4) Library path
echo '/usr/local/lib' | sudo tee /etc/ld.so.conf.d/knst-quic.conf
sudo ldconfig
```

Configure:
```bash
cmake -S . -B build -G Ninja -DKNST_QUICTLS_ROOT=/usr/local/quictls ...
```

**Option 3 — skip HTTP/3:** TLS + H2 alone may suffice. `knst_http_auto_client` automatically falls back h3 → h2 → h1; works fine without h3.

If CMake finds quictls, it **links only quictls** (system OpenSSL **is not linked** — ABI clash prevention):
```
Network:        TLS=TRUE H2=TRUE H3=TRUE
```

> **⚠️ ABI warning:** When H3 is active, CMakeLists **deliberately disables system OpenSSL** and links only quictls. Two OpenSSLs never coexist in the same binary.

#### 5.8.4 Audio Codecs (optional — auto-detected)

**AAC decode — FAAD2:**
```bash
sudo apt install libfaad-dev          # Ubuntu / Debian
sudo dnf install faad2-devel          # Fedora
sudo pacman -S faad2                  # Arch
```

If found, CMake automatically defines `KNST_NETWORK_HAVE_FAAD2` → AAC files become decodable.

**OGG Vorbis encode:**
```bash
sudo apt install libvorbis-dev libogg-dev    # Ubuntu / Debian
sudo dnf install libvorbis-devel libogg-devel # Fedora
sudo pacman -S libvorbis libogg               # Arch
```

If found, CMake automatically defines `KNST_NETWORK_HAVE_VORBIS` → `knst_aud::encode_file("...ogg")` uses native Vorbis.

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

No extra installation needed — `opengl32.dll` ships with Windows. GLAD comes bundled with the library.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DKNST_WINDOW_USE_OPENGL=ON
cmake --build build --config Release
```

### 6.4 Vulkan Backend (Windows)

The **Vulkan SDK** is required → https://vulkan.lunarg.com/

After installing, **restart the terminal** (for the `VULKAN_SDK` env var).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DKNST_WINDOW_USE_VULKAN=ON
cmake --build build --config Release
```

Vulkan in MSYS2:
```bash
pacman -S mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader
```

### 6.5 Internet Libraries (Windows)

> **Note:** On Windows flags like `KNST_NETWORK_DISABLE_TLS=ON` are for **turning off auto-detection**. `-DKNST_NETWORK_ENABLE_TLS=ON` **doesn't exist**.

#### 6.5.1 With vcpkg (Recommended)

```powershell
# One-time
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# TLS + HTTP/2
C:\vcpkg\vcpkg install openssl nghttp2 --triplet x64-windows

# For HTTP/3 (quictls + ngtcp2 + nghttp3)
C:\vcpkg\vcpkg install quictls nghttp2 ngtcp2 nghttp3 --triplet x64-windows
```

CMake configure:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

CMake auto-detect result:
```
Network:        TLS=TRUE H2=TRUE H3=TRUE
```

vcpkg **copies runtime DLLs** (`libssl-3-x64.dll`, `libcrypto-3-x64.dll`, `nghttp2.dll`, etc.) to `build\bin\Release\` **automatically**. No manual DLL copy needed.

#### 6.5.2 MSYS2 UCRT64

```bash
pacman -S mingw-w64-ucrt-x86_64-openssl \
          mingw-w64-ucrt-x86_64-nghttp2
```

For HTTP/3 there's **no ready package** in MSYS2 — use vcpkg.

#### 6.5.3 Manual quictls (if you don't want vcpkg)

[quictls/openssl releases](https://github.com/quictls/openssl/releases) → download the Win64 binary → extract to a folder (`C:\quictls` for example). Then:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DKNST_QUICTLS_ROOT=C:/quictls"
```

#### 6.5.4 Windows WinCertStore Note

If you **don't** define `KNST_NETWORK_NO_WIN_CERTSTORE`, when TLS opens for the first time the **Windows ROOT certificate store** is imported automatically. Corporate CAs (company proxies, etc.) work without issues. `crypt32.lib` linking is automatic.

To disable (rare):
```powershell
cmake -S . -B build "-DCMAKE_CXX_FLAGS=-DKNST_NETWORK_NO_WIN_CERTSTORE"
```

#### 6.5.5 Audio Codecs (optional)

```powershell
# FAAD2 (AAC decode)
C:\vcpkg\vcpkg install faad2 --triplet x64-windows

# Vorbis (OGG encode)
C:\vcpkg\vcpkg install libvorbis libogg --triplet x64-windows
```

If found, CMake automatically defines `KNST_NETWORK_HAVE_FAAD2` / `KNST_NETWORK_HAVE_VORBIS` macros.

---

## 7. Android

### 7.1 Requirements

- **Android NDK** (r26 or newer recommended). Android Studio → SDK Manager → SDK Tools → "NDK (Side by side)".
- **CMake** and **Ninja**
- Environment variable:
```bash
export ANDROID_NDK=$HOME/Android/Sdk/ndk/<version>
```
Windows PowerShell: `$env:ANDROID_NDK="C:\Users\<user>\AppData\Local\Android\Sdk\ndk\<version>"`

### 7.2 Building

Two ways: **ready-made scripts** (recommended) or **manual CMake**.

#### 7.2.1 Ready Scripts (Easiest Way)

There are 4 scripts under `examples/complex_structures/knst_window/android/`. They do the following in order:

1. Auto-find SDK + NDK + build-tools
2. Start ADB server
3. Find device via USB / mDNS / Wi-Fi / cache
4. CMake configure (skips if no change)
5. Native build (Ninja parallel)
6. Package APK (aapt2 + javac + d8 + zipalign + apksigner)
7. Install APK to device + launch app
8. Start filtered live logcat (until Ctrl+C)

| Script | Backend | Platform |
|--------|---------|----------|
| `build-android-opengl.sh` | OpenGL ES | Linux |
| `build-android-opengl.bat` | OpenGL ES | Windows |
| `build-android-vulkan.sh` | Vulkan | Linux |
| `build-android-vulkan.bat` | Vulkan | Windows |

Usage:

```bash
# Linux — OpenGL
cd examples/complex_structures/knst_window/android
./build-android-opengl.sh

# Linux — Vulkan
./build-android-vulkan.sh
```

```bat
:: Windows — OpenGL
cd examples\complex_structures\knst_window\android
build-android-opengl.bat

:: Windows — Vulkan
build-android-vulkan.bat
```

If the script can't find the SDK, set environment variables beforehand:

```bash
export ANDROID_HOME=$HOME/Android/Sdk
export ANDROID_NDK=$ANDROID_HOME/ndk/26.1.10909125   # optional, newest picked if unset
```

```bat
set ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk
set ANDROID_NDK=%ANDROID_HOME%\ndk\26.1.10909125
```

If no device is connected, the script walks you through step by step (USB / VSCode extension / manual `adb pair`).

> 💡 The scripts keep their build directories separate (`build-android-opengl/` or `build-android-vulkan/`). So you can install OpenGL and Vulkan side-by-side on the same device — package names differ too (`com.knst.test.gl` vs `com.knst.test.vk`).

#### 7.2.2 Manual CMake

If you don't want to use the scripts, you can also build the classic way:

Output: `build-android/libknst_app.so`

Key points:
- `ANDROID_ABI` values: `arm64-v8a` (most phones), `armeabi-v7a`, `x86_64` (emulator).
- To build your own code: `-DKNST_APP_SOURCE=path/to/file.cpp`

> This path only produces the `.so` file. To package the APK, install to device, and run, either use the script or manually run the `aapt2` + `d8` + `zipalign` + `apksigner` chain.

### 7.3 OpenGL Backend (Android)

`libEGL.so` and `libGLESv2.so` ship with the NDK, no extra install. GLAD comes bundled with the library.

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DKNST_WINDOW_USE_OPENGL=ON \
  -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_opengl_test.cpp
cmake --build build-android
```

> Note: On Android, **GLES 3.x** is used by default (`opts.client_api = ClientAPI::OpenGLES`).

### 7.4 Vulkan Backend (Android)

Requires Android API **24+** (`libvulkan.so` ships with NDK).

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DKNST_WINDOW_USE_VULKAN=ON \
  -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_vulkan_test.cpp
cmake --build build-android
```

> CMake errors out if `ANDROID_PLATFORM=android-24` is below.

### 7.5 Internet Libraries (Android)

> **Warning:** Android NDK **doesn't ship** `openssl`, `nghttp2`, `ngtcp2`, `nghttp3`. CMakeLists **builds them automatically** via **FetchContent** (first configure takes 10-15 minutes).

#### 7.5.1 Automatic Build (CMakeLists handles it)

When `KNST_NETWORK_DISABLE_TLS=OFF` (default), CMakeLists:

1. **quictls** — clones from GitHub, cross-compiles with NDK toolchain
2. **nghttp2** — builds via FetchContent
3. **nghttp3 + ngtcp2** — builds via FetchContent
4. Everything **statically links** into `libknst_app.so`

Result: **no extra .so** in the APK — all in the main library.

**Cache:** After first build, artifacts are stored in `$HOME/.knst-cache/quictls-android-<ABI>/`. Subsequent builds are fast (no rebuild).

**Time estimate:**
- First configure + build: 10-15 min (quictls + nghttp2 + ngtcp2 + nghttp3)
- Subsequent builds: 1-2 min

**To disable:**
```bash
cmake ... -DKNST_NETWORK_DISABLE_TLS=ON   # disable everything
cmake ... -DKNST_NETWORK_DISABLE_H3=ON    # only disable H3 (H2 stays)
```

#### 7.5.2 Tier Order

| Flag combination | Result |
|---|---|
| (default) | TLS + H2 + H3 (auto-build) |
| `-DKNST_NETWORK_DISABLE_H3=ON` | TLS + H2 |
| `-DKNST_NETWORK_DISABLE_H3=ON -DKNST_NETWORK_DISABLE_H2=ON` | TLS only |
| `-DKNST_NETWORK_DISABLE_TLS=ON` | None (plain TCP/UDP) |

After configure, CMake output shows:
```
Network:        TLS=TRUE H2=TRUE H3=TRUE
```

#### 7.5.3 Android System CA Store

When TLS is enabled, `knst_network.hpp` automatically tries these paths:
- `/apex/com.android.conscrypt/cacerts` (Android 14+)
- `/system/etc/security/cacerts` (older Android)

No extra CA bundle needed — it uses Android system certificates.

#### 7.5.4 If You Only Need Local Operations

If you're only doing device-local operations (file, camera, audio), **don't include the internet headers**. `KernelNucleusT.hpp` doesn't include everything — only the headers you need. Build time and binary size shrink.

---

## 8. Using in Your Own Project

The library is header-only. The easiest way is `add_subdirectory`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApp CXX)

# Meaningful on Linux
set(KNST_LINUX_PLATFORM "X11" CACHE STRING "" FORCE)

# Backend selection (optional)
set(KNST_WINDOW_USE_OPENGL ON  CACHE BOOL "" FORCE)
# set(KNST_WINDOW_USE_VULKAN ON CACHE BOOL "" FORCE)

add_subdirectory(KernelNucleusT)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE KernelNucleusT::KernelNucleusT)
```

That single line is enough: include folders, platform macros, X11/Wayland libraries, and backend dependencies link automatically.

**Network layers** (TLS / H2 / H3) are **auto-detected** after `add_subdirectory` — if the required libraries exist on the system. To verify, check the CMake output:

```
Network:        TLS=TRUE H2=TRUE H3=FALSE
```

To **force-disable** a particular layer:
```cmake
set(KNST_NETWORK_DISABLE_H3 ON CACHE BOOL "" FORCE)   # disable H3, keep H2
add_subdirectory(KernelNucleusT)
```

**JSON integration** (optional):
```cmake
# Make sure to include knst_json.hpp before KernelNucleusT.hpp
target_compile_definitions(my_app PRIVATE KNST_NETWORK_ENABLE_JSON)
```

---

## 9. Common Issues

| Issue | Solution |
|---|---|
| `Missing X11 development packages` | Install the packages in Section 5.2, then `rm -rf build` and reconfigure. |
| `Wayland protocol source not found` | Make sure the `.c` files in `include/platform/linux/wayland/protocol_files/` aren't deleted. |
| `Android NDK not found` | Pass `-DCMAKE_TOOLCHAIN_FILE=.../android.toolchain.cmake`. |
| `Vulkan was not found` | Install the Vulkan SDK (Windows) or `libvulkan-dev` (Linux). Refresh the terminal. |
| OpenGL example doesn't open / segfault | Pay attention to init order: `Init → MakeCurrent → gladLoadGLLoader → gl*` calls. |
| I changed a setting but nothing changed | Clear the cache: `rm -rf build` (Windows: `rmdir /s /q build`) and reconfigure from scratch. |
| Which macros are defined? | Check the **Definitions** line in the CMake summary at the end. |
| `ninja: command not found` (Windows) | Use the MSYS2 UCRT64 terminal, not normal PowerShell. |
| `cmake: command not found` (Windows) | In MSYS2 UCRT64: `pacman -S mingw-w64-ucrt-x86_64-cmake` |
| PowerShell splits the `-D` argument at `.cpp` | Wrap the argument in quotes: `"-DKNST_APP_SOURCE=path/file.cpp"` |
| `cl.exe not found` (MSVC) | Open "x64 Native Tools Command Prompt for VS", not normal PowerShell |
| I'm using Visual Studio 2026 | Generator name: `-G "Visual Studio 18 2026"` |
| CMake output shows `Network: TLS=FALSE` | OpenSSL not installed. Install `libssl-dev` (Linux) / vcpkg `openssl` (Windows) |
| CMake output shows `Network: H2=FALSE` | `libnghttp2-dev` not installed, or you passed `-DKNST_NETWORK_DISABLE_H2=ON` |
| CMake output shows `Network: H3=FALSE` | quictls + ngtcp2 + nghttp3 missing. Pass `-DKNST_QUICTLS_ROOT=...` or `apt install libngtcp2-dev libnghttp3-dev` (Ubuntu 24.04+) |
| `fatal error: openssl/ssl.h: No such file` | `libssl-dev` missing. CMake can't find OpenSSL, so TLS stays off |
| `#error "knst_network_h3.hpp requires KNST_NETWORK_ENABLE_TLS"` | H3 requires TLS. Don't pass `KNST_NETWORK_DISABLE_TLS=ON` |
| `#error "knst_network_h3.hpp: ngtcp2 >= 1.0 required"` | Old ngtcp2. `vcpkg upgrade ngtcp2` or update manually |
| HTTP/3 requests all Timeout | Network blocks UDP/443. `knst_http_auto_client` falls back to H2/H1 automatically; manual `knst_http3_client` doesn't fall back |
| `libssl.so.3: cannot open shared object` (runtime) | Because you're not static-linking, OpenSSL 3 is needed on the system. Either `apt install libssl3` or build statically |
| Windows TLS handshake fails, certificate error | If `KNST_NETWORK_NO_WIN_CERTSTORE` is defined, WinCertStore is skipped. Remove the definition |
| Proxy CONNECT fails (`ProxyFailed`) | `options::proxy_url` format must be `http://user:pass@proxy:8080`. Check whether `no_proxy` bypasses it |
| `libngtcp2.so.16: cannot open` (Linux runtime) | If not static-linking, ngtcp2 runtime needed on the system. Verify with `ldd ./app` |
| Android build takes 15 min the first time | Normal — CMakeLists builds quictls + nghttp2 + ngtcp2 + nghttp3 from source. Subsequent builds hit the cache |
| `Network: TLS=TRUE H2=TRUE H3=TRUE` but binary doesn't run | Static linking problem. On Linux, use `ldd ./build/bin/knst_app` to check the quictls library path |

---

## 10. Ready Command Summary

> **If you're using PowerShell:** Wrap every argument that starts with `-D` and contains a `.` (dot) in double quotes.
> Otherwise PowerShell splits the argument at `.cpp` and CMake warns: `Ignoring extra path from command line: ".cpp"`.

---

### 🔹 MSYS2 UCRT64 / PowerShell (Windows)

```powershell

# ------------------------------Basic Window----------------------------------------
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

# ------------------------------Build Your Own .cpp---------------------------------
  cmake -S . -B build -G Ninja "-DKNST_APP_SOURCE=path/to/your_file.cpp"
  cmake --build build
# -----------------------------------------------------------------------------------

# ------------------------------Reset Build Folder----------------------------------
  Remove-Item -Recurse -Force build
# -----------------------------------------------------------------------------------
```

---

### 🔹 Windows MSVC (Visual Studio)

> **NOTE:** Run inside `"x64 Native Tools Command Prompt for VS"`. Not normal PowerShell.

```powershell
# Basic
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

# Your own .cpp file
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

### 🔹 Linux (Bash)

```bash
# ------------------------------------X11--------------------------------------------
  # Build your own .cpp file
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------Wayland-----------------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp # window won't show on wayland because wayland requires drawing to make window visible.
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------Dual (X11 + Wayland)----------------------------
  cmake -S . -B build -G Ninja -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp # window won't show on wayland because wayland requires drawing to make window visible.
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
```

---

### 🔹 Android

> **Easiest way:** run the ready script. SDK/NDK/device detection, build, APK, install, and logcat all in one command.

```bash
# Ready script — Linux
  cd examples/complex_structures/knst_window/android
  ./build-android-opengl.sh     # OpenGL ES
  ./build-android-vulkan.sh     # Vulkan
```

```powershell
# Ready script — Windows
  cd examples\complex_structures\knst_window\android
  build-android-opengl.bat      # OpenGL ES
  build-android-vulkan.bat      # Vulkan
```

Manual CMake (produces only `.so` — no APK / install / logcat):

```bash
# Environment variable (Linux)
  export ANDROID_NDK=$HOME/Android/Sdk/ndk/<version>

# Environment variable (Windows PowerShell)
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

### 🔹 Internet (TLS / H2 / H3) Example Commands

> **Important:** There's no flag like `-DKNST_NETWORK_ENABLE_TLS=ON`. CMake auto-detects. To disable, use `KNST_NETWORK_DISABLE_*`.

**Linux (Bash):**
```bash
# TLS + H2 + H3 (if available) — automatic
# Just having "OpenSSL + nghttp2 + quictls + ngtcp2 + nghttp3" installed is enough
cmake -S . -B build -G Ninja \
  -DKNST_LINUX_PLATFORM=X11 \
  -DKNST_APP_SOURCE=path/to/network_demo.cpp
cmake --build build -j
./build/bin/knst_app

# At the end of the output: Network: TLS=TRUE H2=TRUE H3=TRUE
```

**Windows (with vcpkg):**
```powershell
# vcpkg must be installed: C:\vcpkg
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DKNST_APP_SOURCE=path/to/network_demo.cpp"
cmake --build build --config Release
build\bin\Release\knst_app.exe
```

**Android (FetchContent auto-build):**
```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DKNST_APP_SOURCE=path/to/network_demo.cpp
cmake --build build-android
# 10-15 min the first time (quictls built from source), fast afterwards
```

**TLS only (disable H2/H3):**
```bash
cmake -S . -B build -DKNST_NETWORK_DISABLE_H2=ON -DKNST_NETWORK_DISABLE_H3=ON ...
```

**Network completely disabled:**
```bash
cmake -S . -B build -DKNST_NETWORK_DISABLE_TLS=ON ...
```

---

### 📌 Rule Summary

| Shell | Argument with `-D...=.cpp` |
|-------|-----------------------------|
| **Bash (Linux)** | Works without quotes |
| **cmd.exe (Windows)** | Works without quotes |
| **PowerShell** | **Quotes required** |
| **MSYS2 UCRT64** | Works with or without quotes; use quotes for safety |

> **Why?** PowerShell sometimes treats the `.` character in the path after `=` as a separate argument. Solution: wrap the whole `-D...=...` argument in `"..."`.

---

## 🤝 Contributing

Use the **Issues** page for bug reports and feature requests. You can also reach out to me with requests, suggestions, or anything similar.


---

## 📄 License

- MPL 2.0