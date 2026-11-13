# knst_window — Complete Documentation

> Cross-platform window library.
> **Windows** · **Linux (X11 + Wayland + Dual)** · **Android**

---

## Table of Contents

- [1. Overview](#1-overview)
- [2. Architecture and Internal Flow](#2-architecture-and-internal-flow)
- [3. Requirements and Build](#3-requirements-and-build)
- [4. Quick Start](#4-quick-start)
- [5. Class Reference](#5-class-reference)
- [6. Function Reference](#6-function-reference)
- [7. All Functions — One Line Each (`//` commented)](#7-all-functions--one-line-each--commented)
- [8. Macro Reference](#8-macro-reference)
- [9. Platform Notes](#9-platform-notes)
- [10. Internal Design, Tactics, and Background](#10-internal-design-tactics-and-background)
- [11. Configuration (`knst_settings.hpp`)](#11-configuration-knst_settingshpp)
- [12. Patterns and Examples](#12-patterns-and-examples)
- [13. FAQ and Traps](#13-faq-and-traps)
- [14. License](#14-license)

---

## 1. Overview

`knst_window` lets you open a window on four platforms (Windows, Linux X11, Linux Wayland, Linux Dual) and Android with **a single `#include "KernelNucleusT.hpp"`**. The library is designed as fully **header-only** except on Linux (Wayland) — just copy the `include/` folder, compile, run. On Wayland, the protocol `.c`/`.h` files are compiled by CMake via `wayland-scanner` (or `wayland-protocols`). Protocol files ship with the project by default; you can delete them and let CMake regenerate them if you prefer.

### Core Principles

- **Events are yours, drawing is yours.** The library collects events, manages the window, drives the cursor, holds the clipboard. Drawing is not its job.
- **`noexcept` + `bool`.** No exceptions thrown. Error handling is entirely in your hands. `set_clipboard()` won't crash even if it fails.
- **Zero-cost abstraction.** Modern C++ (`if constexpr`, `force_inline`, ring buffer, `atomic`, `unique_ptr`) throughout. The cost of an unused feature on binary size is minimal.
- **Header-only (except Wayland).** No extra `.c`/`.cpp` to compile. On Windows and X11 you can use the `include/` folder directly.
- **Zero VM/GC.** Everything is known at compile time; allocation happens only at your request.

### Library Skeleton

```
KnstWindowSources::Init()          ← Initialize platform resources
        ↓
window.creation_and_show()         ← Create and show the window
        ↓
    ┌─────────────────────────────────────────┐
    │  while (!window.is_should_close())      │   ← Main loop
    │    non_block_pool_event()               │
    │    call_redraw_callback()               │
    └─────────────────────────────────────────┘
        ↓
window.destroy()
KnstWindowSources::CleanUp()       ← Clean up resources
```

### Event Flow Summary

```
[OS] → [Event Manager] → [Ring Buffer] → [You read] → [Handle]
                            ↑
                    (dispatch_event)
```

Each window has its own **256-slot ring buffer**. Thanks to coalescing, rapid `MOTION` / `RESIZE` / `EXPOSE` events are merged — the buffer doesn't overflow. Raw mouse deltas are **summed** (not overwritten), so FPS cameras never lose motion.

---

## 2. Architecture and Internal Flow

`knst_window` has a two-tier architecture:

### 2.1 Layers

```
┌────────────────────────────────────────────────────────────┐
│  User Code (game loop / event handler / renderer)          │
└────────────────────────────────────────────────────────────┘
                            ↕
┌────────────────────────────────────────────────────────────┐
│  knst_window                                               │
│  ├── Event ring buffer (256 slots)                         │
│  ├── Held-key table (8 slots)                              │
│  ├── CSD state (callback, hit-test)                        │
│  ├── Surface client registry                               │
│  └── Per-platform state (HWND / xcb_window_t / wl_surface) │
└────────────────────────────────────────────────────────────┘
                            ↕
┌────────────────────────────────────────────────────────────┐
│  knst_window_event_system (global event pump)              │
│  └── Registers all windows → OS events → dispatch          │
└────────────────────────────────────────────────────────────┘
                            ↕
┌────────────────────────────────────────────────────────────┐
│  KnstWindowSources (platform lifecycle, handles, display)  │
│  ├── Init / CleanUp                                        │
│  ├── Active backend (x11 / wayland / windows / android)    │
│  └── Native handles (HINSTANCE, xcb_connection_t, wl_*)    │
└────────────────────────────────────────────────────────────┘
                            ↕
┌────────────────────────────────────────────────────────────┐
│  OS APIs (Win32, XCB, Wayland, Android NDK)                │
└────────────────────────────────────────────────────────────┘
```

### 2.2 Data Flow

1. **An OS event arrives** → call `non_block_pool_event()` or `block_pool_event()`
2. **Event manager** converts the OS event into a `knst_window_event` struct
3. **Push to ring buffer** — coalescing applied (MOTION/RESIZE/EXPOSE merged, raw deltas summed)
4. **You** read via `event_count()` / `get_window_event_handle(i)`
5. **`clear_events()`** empties the buffer

### 2.3 Backend Selection (Dual Mode)

```
KnstWindowSources::Init()
        ↓
KNST_WINDOW_FORCE_X11=1 set?      → yes: X11
KNST_WINDOW_FORCE_WAYLAND=1 set?  → yes: Wayland
        ↓
WAYLAND_DISPLAY / XDG_SESSION_TYPE=wayland set?
        ↓
wl_display_connect() probe → wl_compositor + xdg_wm_base present?
        ↓
X11 probe: DISPLAY set? XOpenDisplay works?
        ↓
None → knst_backend::none (Init fails)
```

### 2.4 Feature Layers

| Layer | Purpose | Example |
|-------|---------|---------|
| **Core** | Window + event + input | `knst_window`, `knst_window_event_system` |
| **Display** | Monitor management | `knst_display`, `knst_monitor` |
| **Surface** | GPU context integration | `knst_surface_client`, `knst_window_opengl_content` |
| **Platform** | Resource lifecycle | `KnstWindowSources` |
| **Mobile** | Android-specific | `knst_mobile_keyboard`, `knst_android_jni` |

---

## 3. Requirements and Build

### 3.1 System Requirements

| Platform | Minimum | Recommended |
|----------|---------|-------------|
| **Windows** | Windows 7+ | Windows 10/11 |
| **Linux X11** | X.Org 1.17+ | X.Org 21+ |
| **Linux Wayland** | wayland-client 1.15+ | 1.22+ |
| **Android** | API 24 (Android 7.0) | API 30+ |
| **C++** | C++20 | C++20 |
| **CMake** | 3.20 | 3.25+ |

### 3.2 Header-Only Note

For Windows and X11 it is **fully header-only**. Copy `include/` into your project, `#include "KernelNucleusT.hpp"`, compile. On Wayland, `.c` and `.h` files under `include/platform/linux/wayland/protocol_files/` must be compiled — CMake handles this automatically.

### 3.3 CMake Integration

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApp CXX)

# Meaningful on Linux (ignored on Windows)
set(KNST_LINUX_PLATFORM "X11" CACHE STRING "" FORCE)
# set(KNST_LINUX_PLATFORM "Wayland" CACHE STRING "" FORCE)
# set(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND ON CACHE BOOL "" FORCE)

# Backend selection (optional)
set(KNST_WINDOW_USE_OPENGL ON  CACHE BOOL "" FORCE)
# set(KNST_WINDOW_USE_VULKAN ON CACHE BOOL "" FORCE)

add_subdirectory(KernelNucleusT)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE KernelNucleusT::KernelNucleusT)
```

These 4 lines:
- Add the `include/` path
- Define platform macros (`KNST_USING_PLATFORM_WINDOWS`, `KNST_USING_LINUX_PLATFORM_X11`, etc.)
- Link X11/Wayland/GDI libraries
- Apply the backend selection

### 3.4 All CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `KNST_LINUX_PLATFORM` | `X11` | Linux: `X11` or `Wayland` |
| `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` | `OFF` | Linux: both backends in one binary (runtime-selected) |
| `KNST_BUILD_EXAMPLES` | `OFF` | Build examples in `examples/` |
| `KNST_APP_SOURCE` | Empty | Build your own `.cpp` as `knst_app` |
| `KNST_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UBSan |
| `KNST_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as errors |
| `KNST_STATIC_MSVC_RUNTIME` | `ON` | MSVC: static runtime (`/MT`) |
| `KNST_WINDOW_USE_OPENGL` | `OFF` | OpenGL 3.3 Core backend |
| `KNST_WINDOW_USE_VULKAN` | `OFF` | Vulkan backend |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |

### 3.5 Automatically Defined Macros

| Situation | Macros defined |
|-----------|----------------|
| Linux + X11 | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11` |
| Linux + Wayland | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_WAYLAND` |
| Linux + Dual | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11`, `KNST_USING_LINUX_PLATFORM_WAYLAND`, `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` |
| Windows | `KNST_USING_PLATFORM_WINDOWS`, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `UNICODE`, `_UNICODE` |
| Android | `KNST_USING_PLATFORM_ANDROID`, `KNST_USING_PLATFORM_LINUX` |
| OpenGL on | `KNST_WINDOW_USE_OPENGL` |
| Vulkan on | `KNST_WINDOW_USE_VULKAN` |

### 3.6 Manual Build

**Required macros:**
- Windows: `-DKNST_USING_PLATFORM_WINDOWS -DNOMINMAX -DUNICODE -D_UNICODE`
- X11: `-DKNST_USING_PLATFORM_LINUX -DKNST_USING_LINUX_PLATFORM_X11`
- Wayland: `-DKNST_USING_PLATFORM_LINUX -DKNST_USING_LINUX_PLATFORM_WAYLAND`
- Android: `-DKNST_USING_PLATFORM_ANDROID -DKNST_USING_PLATFORM_LINUX`

**Required libraries:**
- Windows: `gdi32 user32 shell32 shcore ole32 oleaut32 uuid hid bthprops setupapi iphlpapi ws2_32`
- X11: `xcb xcb-randr xcb-keysyms xcb-icccm x11-xcb x11 xext xfixes xi xrandr xcursor xcb-xinput xcb-sync`
- Wayland: `wayland-client wayland-cursor wayland-egl xkbcommon`

### 3.7 Linux Package Installation

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install build-essential cmake ninja-build pkg-config
sudo apt install libx11-dev libx11-xcb-dev libxcb1-dev libxcb-randr0-dev \
  libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-util-dev libxcb-sync-dev \
  libxext-dev libxfixes-dev libxi-dev libxrandr-dev libxcursor-dev libxcb-xinput-dev
sudo apt install libwayland-dev libwayland-bin libxkbcommon-dev
sudo apt install libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev
sudo apt install libvulkan-dev vulkan-tools
```

**Fedora**
```bash
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config
sudo dnf install libX11-devel libxcb-devel xcb-util-devel xcb-util-keysyms-devel \
  xcb-util-wm-devel libXext-devel libXfixes-devel libXi-devel libXrandr-devel libXcursor-devel
sudo dnf install wayland-devel libxkbcommon-devel
sudo dnf install mesa-libGL-devel mesa-libEGL-devel mesa-libGLES-devel
sudo dnf install vulkan-loader-devel vulkan-headers vulkan-tools
```

**Arch**
```bash
sudo pacman -S base-devel cmake ninja pkgconf
sudo pacman -S libx11 libxcb xcb-util xcb-util-keysyms xcb-util-wm \
  libxext libxfixes libxi libxrandr libxcursor
sudo pacman -S wayland libxkbcommon
sudo pacman -S mesa vulkan-headers vulkan-icd-loader vulkan-tools
```

### 3.8 Android

**NDK r26+** recommended. Android Studio → SDK Manager → SDK Tools → "NDK (Side by side)".

**Ready-made scripts** (`examples/complex_structures/knst_window/android/`):
- `build-android-opengl.sh` / `.bat`
- `build-android-vulkan.sh` / `.bat`

Script pipeline:
1. **Environment** — detect SDK/NDK/build-tools
2. **ADB server** — start or reuse
3. **Device discovery** — USB → mDNS → cache → Wi-Fi → wait
4. **CMake configure** — with cache (skip if nothing changed)
5. **Native build** — Ninja parallel
6. **APK packaging** — aapt2 + javac + d8 + zipalign + apksigner
7. **Deploy** — uninstall → install → launch
8. **Live logcat** — until `Ctrl+C`

**For Vulkan, `minSdkVersion >= 24`** is mandatory — the script exits early if lower.

### 3.9 GLAD (OpenGL Loader)

The library ships with **GLAD 3.3 Core** under `include/platform/knst_window/opengl_content/glad_3_3/`. When `KNST_WINDOW_USE_OPENGL=ON` is set, CMake compiles it automatically.

If you want a different version: get a fresh configuration from https://glad.dav1d.de/, replace the `include/` and `src/` folders with the same directory, `rm -rf build` + reconfigure. You can also include your own GLAD in `KernelNucleusT.hpp` instead of the library's.

---

## 4. Quick Start

### 4.1 Simplest Example

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

static void on_frame(knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);

        if (ev.type == KNST_WINDOW_EVENT_KEYBOARD &&
            ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS &&
            ev.key_code == KNST_WINDOW_KEY_CODE_ESCAPE) {
            w.should_close();
        }
    }
    w.clear_events();
}

int main() {
    KnstWindowSources::Init();

    knst_window window(800, 600, u"Hello World");
    window.set_redraw_callback(on_frame);
    window.creation_and_show();

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
    }

    window.destroy();
    KnstWindowSources::CleanUp();
}
```

> **Important note:** `call_redraw_callback()` is **not mandatory** — but on Windows it prevents render stalling during `WM_WINDOWPOSCHANGED`. To ensure full cross-platform behavior, I recommend using this pattern.

### 4.2 Android Example

```cpp
#include "KernelNucleusT.hpp"

static void on_frame(knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);
        if (ev.type == KNST_WINDOW_EVENT_MOBILE_BACK) {
            w.should_close();
        }
    }
    w.clear_events();

    android_app* app = KnstWindowSources::get_android_app();
    if (app && app->window) {
        ANativeWindow_Buffer buf;
        if (ANativeWindow_lock(app->window, &buf, nullptr) == 0) {
            uint32_t* px = (uint32_t*)buf.bits;
            for (int y = 0; y < buf.height; ++y)
                for (int x = 0; x < buf.width; ++x)
                    px[y * buf.stride + x] = 0xFF202060;
            ANativeWindow_unlockAndPost(app->window);
        }
    }
}

void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);

    knst_window window;
    window.set_redraw_callback(on_frame);
    window.creation_and_show();

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
    }

    window.destroy();
    KnstWindowSources::CleanUp();
}
```

**Android differences:**
- Entry point is `android_main(struct android_app*)`, not `main()`
- `KnstWindowSources::Init(app)` takes a parameter
- `std::cout` is redirected to `logcat` (tag: `KNST`)
- `app->window` starts as `NULL` — `Init()` waits for it

### 4.3 OpenGL Setup Order

If you use OpenGL, the order is **critical**:

```cpp
// 1. Platform layer
KnstWindowSources::Init();

// 2. Global GL state (EGL display + config)
knst_opengl_global::Options opts;
opts.gl_major = 3;
opts.gl_minor = 3;
opts.core_profile = true;
knst_opengl_global::Init(opts);

// 3. Create window without showing it
knst_window window(800, 600, u"GL");
window.set_redraw_callback(on_frame);
window.creation();

// 4. Per-window GL context
knst_window_opengl_content gl;
gl.Init(&window);

// 5. MakeCurrent + GLAD load (order matters)
gl.MakeCurrent();
gladLoadGLLoader((GLADloadproc)knst_window_opengl_content::LoadGLProc);

// 6. Read GPU info
std::cout << "GPU: " << gl.GetRendererString() << "\n";
std::cout << "GL : " << gl.GetVersionString() << "\n";

// 7. First frame (prevents black flash)
gl.MakeCurrent();
render_frame();
gl.SwapBuffers();

// 8. Now show
window.show();

// 9. Main loop
while (!window.is_should_close()) {
    knst_window_event_system::non_block_pool_event();
    window.call_redraw_callback();
}

// 10. Clean up in reverse order
gl.MakeCurrent();
// delete GL resources
gl.Shutdown();
knst_opengl_global::CleanUp();
window.destroy();
KnstWindowSources::CleanUp();
```

> **Order matters:** `gl.Shutdown()` → `window.destroy()` → `KnstWindowSources::CleanUp()`. The reverse order means **crash** or **UB**. Be careful.

---

## 5. Class Reference

### 5.1 `knst_window`

Main window class. **Non-copyable** and **non-movable**.

```cpp
knst_window(const knst_window&) = delete;
knst_window& operator=(const knst_window&) = delete;
knst_window(knst_window&&) = delete;
knst_window& operator=(knst_window&&) = delete;
```

**Usage:** On the stack or on the heap with `std::make_unique<knst_window>(...)`.

#### Constructor

```cpp
knst_window(
    int width = 800,
    int height = 800,
    knst_c16string title = u"Knst_Window",
    int root_x = KNST_WINDOW_DEFAULT,
    int root_y = KNST_WINDOW_DEFAULT,
    const knst_monitor& monitor = knst_monitor()
) noexcept;
```

| Parameter | Description |
|-----------|-------------|
| `width`, `height` | Window size (pixels). Ignored on Android. |
| `title` | UTF-16 title. |
| `root_x`, `root_y` | Position **relative to** the monitor. `KNST_WINDOW_DEFAULT` → center. |
| `monitor` | Which monitor to open on. Empty → default behavior. |

**Note:** If `monitor.physical_width > 0 && monitor.physical_height > 0`, the constructor auto-centers. Otherwise `root_x/root_y` are used as-is.

#### Non-movable / Non-copyable — Why?

- **Platform resources:** HWND, `xcb_window_t`, `wl_surface`, XIC, HGLRC, `IDropTarget` — all tied to `this`.
- **Listener user-data:** Wayland `xdg_surface`, `xdg_toplevel`, `wl_callback` — all point to `this`. A move would leave dangling pointers → UAF.
- **Callback lambdas:** CSD hit-test, redraw callback — all capture `this`.
- **Global registry:** `knst_window_event_system::windows`, `active_window`, `keyboard_focus_window`, `drag_target_window` — all raw pointers.

**Correct usage:**
```cpp
knst_window w(800, 600, u"Title");   // stack
auto w = std::make_unique<knst_window>(...);    // heap
```

**Incorrect usage:**
```cpp
knst_window make_window() { return knst_window(800, 600, u"X"); }  // won't compile — no move
```

---

### 5.2 `knst_window_event`

Represents a single event. **Uses a union** — you must know which member to read based on the `type` field.

```cpp
struct knst_window_event {
    uint32_t type; // KNST_WINDOW_EVENT_* — which fields are valid?
    uint32_t timestamp_ms; // Time the event was created (ms)

    int32_t mods; // Modifier bit mask
    int32_t mouse_x; // Window-local coordinate
    int32_t mouse_y;
    int32_t mouse_root_x; // Screen coordinate
    int32_t mouse_root_y;
    int32_t window_width; // Window width at event time
    int32_t window_height;
    int32_t window_root_x; // Window position (screen)
    int32_t window_root_y;

    bool is_focused;
    bool mouse_on_window;
    bool is_full_screen;
    bool is_maximized;
    bool is_minimized;

    // ⚠️ UNION: Only ONE is valid (based on `type`)
    union {
        struct { int32_t mouse_button, mouse_action, mouse_scroll_delta; };
        struct { int32_t key_code, scancode, key_action; };
    };

    // File drop
    std::shared_ptr<knst_vector<knst_c16string>> drop_files;
    uint32_t drop_count;

    // Text / IME
    knst_c16string text_input;
    knst_c16string text_preedit;
    int32_t text_preedit_cursor_begin = -1;
    int32_t text_preedit_cursor_end   = -1;
    int32_t text_delete_before = 0;
    int32_t text_delete_after  = 0;

    // Raw mouse
    int32_t raw_delta_x = 0;
    int32_t raw_delta_y = 0;
    bool is_raw = false;

    // Touch (up to 10 points)
    int pointer_count;
    float pointer_x[10];
    float pointer_y[10];
    int pointer_id[10];
    int touch_action;

#if defined(KNST_USING_PLATFORM_ANDROID)
    int content_left, content_top, content_right, content_bottom;
    int orientation;
    char language[4];
    char country[4];
    bool is_night_mode;
    bool is_low_memory;
    float density;
    int screen_width_dp;
    int screen_height_dp;
    void* saved_state;
    size_t saved_state_size;
#endif

    void begin(uint32_t ev_type) noexcept;
};
```

#### ⚠️ Union Warning

`mouse_button` and `key_code` **share the same memory**. Read the correct one based on `type`:

```cpp
if (ev.type == KNST_WINDOW_EVENT_MOUSE) {
    std::cout << ev.mouse_button;   // ✓ correct
    // std::cout << ev.key_code;    // ✗ wrong — those are mouse_button's bytes
}
```

#### `begin()` Behavior

`begin()` **zeroes everything** and sets `timestamp_ms`. Strings like `text_input`, `text_preedit` are cleared. **Only** `density`, `screen_width_dp`, `screen_height_dp` — on Android, for consistency — are intentionally preserved.

---

### 5.3 `knst_monitor`

Represents a monitor. Filled by `knst_display`.

```cpp
class knst_monitor {
public:
    int root_x = 0; // X position in the virtual desktop
    int root_y = 0;
    int width = 0; // Pixel width
    int height = 0;
    int physical_width = 0; // mm (from EDID)
    int physical_height = 0;
    float refresh_rate = 60.0f;  // Hz (from driver on X11/Windows)
    float dpi_scale = 96.0f;  // 96 = 1.0x, 144 = 1.5x
    bool is_primary = false;  // Primary monitor?
    knst_c16string name;  // Monitor name (e.g. "DELL U2720Q")
};
```

#### How DPI Is Computed

| Platform | Source |
|----------|--------|
| **Windows** | `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` |
| **X11** | `Xft.dpi` → `GDK_SCALE` → physical calculation |
| **Wayland** | `wl_output.scale` or DPI estimate |
| **Android** | `AConfiguration_getDensity()` / 160 |

**X11 priority order:**
1. `Xft.dpi` X resource (user's setting) — Qt/GTK writes this
2. `GDK_SCALE` env var (2 → 192 DPI)
3. Physical calculation: `sqrt(w²+h²) / (sqrt(pw²+ph²) / 25.4)`

---

### 5.4 `knst_display`

Static class managing all monitors.

```cpp
class knst_display {
public:
    static void refresh_screens() noexcept;
    static const knst_vector<std::unique_ptr<knst_monitor>>& get_monitor_list() noexcept;
    static const knst_monitor* get_primary_monitor() noexcept;
    static size_t get_monitor_count() noexcept;
    static const knst_monitor* get_monitor(size_t index) noexcept;
};
```

**Note:** `refresh_screens()` is expensive (RandR query on X11, `EnumDisplayDevices` on Windows). Call it **once** at startup. On Wayland the compositor reports monitor changes automatically; only call it manually for the first read.

---

### 5.5 `KnstWindowSources`

Manages platform resources. You generally **don't use it directly** — only `Init()` / `CleanUp()` / `get_current_time_ms()`.

```cpp
class KnstWindowSources {
public:
    static void Init() noexcept; // Windows / Linux
    static void Init(android_app* app); // Android
    static void CleanUp() noexcept;

    static uint32_t get_current_time_ms() noexcept;

    static knst_backend active_backend() noexcept;
    static bool is_x11_active() noexcept;
    static bool is_wayland_active() noexcept;
    static bool is_windows_active() noexcept;

#if KNST_USING_PLATFORM_WINDOWS
    static HINSTANCE& get_windows_native_instance_handle() noexcept;
#elif KNST_USING_LINUX_PLATFORM_X11
    static xcb_window_t& get_native_x11_root_handle() noexcept;
    static xcb_connection_t* get_native_x11_connection_handle() noexcept;
    static Display* get_native_x11_display() noexcept;
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    static wl_display* wayland_display;
#elif defined(KNST_USING_PLATFORM_ANDROID)
    static android_app* get_android_app() noexcept;
#endif
};
```

#### `knst_backend` Enum

```cpp
enum class knst_backend : uint8_t {
    none    = 0,
    x11     = 1,
    wayland = 2,
    android = 3,
    windows = 4
};
```

---

### 5.6 `knst_window_event_system`

Global event manager. **Static** — handles all windows from one place.

```cpp
struct knst_window_event_system {
    static void block_pool_event() noexcept; // Blocks until an event arrives
    static void non_block_pool_event() noexcept; // Returns immediately
    static size_t get_window_count() noexcept; // Number of registered windows
};
```

| Function | Behavior | Use case |
|----------|----------|----------|
| `block_pool_event()` | Blocks until an event arrives | Simple CLI tools, GUI apps |
| `non_block_pool_event()` | Returns immediately | Game loops, animations |

**Wayland warning:** On Wayland drawing is mandatory. If you use `block_pool_event()` the window is never updated because the compositor waits for a `frame` event. **Always use `non_block_pool_event()`**.

---

### 5.7 `knst_mobile_keyboard` (Android)

Controls the Android soft keyboard.

```cpp
class knst_mobile_keyboard {
public:
    static bool show();
    static bool hide();
    static void toggle();
    static bool is_visible();
};
```

**No-op on PC:** Same API on PC, returns `false`. No `#ifdef` needed.

**Notes:**
- `show()` retries 3 times (200ms apart) — required for Android focus settle.
- `toggle()` has a 300ms debounce — rapid double calls are ignored.
- `is_visible()` is thread-safe (mutex-protected).

---

### 5.8 `knst_window_capabilities`

Runtime capability query. Returned by the static `knst_window::capabilities()`.

```cpp
struct knst_window_capabilities {
    bool move; // Window can be moved
    bool resize_by_user; // User can resize
    bool force_focus; // Can be focused programmatically
    bool cursor_position; // Cursor position can be set
    bool opacity; // Window opacity
    bool always_on_top; // Z-order control
    bool set_icon; // Window icon
    bool input_transparent; // Click-through
    bool drag_drop; // File drag-and-drop
    bool text_input; // Character input
    bool ime; // IME (CJK, dead keys)
    bool raw_mouse; // Relative mouse motion
    bool clipboard; // System clipboard
    bool primary_selection; // X11 middle-click paste
    bool fractional_scale; // 1.25x, 1.5x etc.
    bool vsync_control; // SwapInterval
    bool multiple_windows; // Multiple window support
    bool touch_screen; // Runtime-detected touch
    bool idle_inhibit; // Screen sleep inhibitor
};
```

```cpp
auto caps = knst_window::capabilities();
if (caps.move) window.move(100, 100);
if (caps.touch_screen) window.enable_touch_screen(true);
```

---

### 5.9 `knst_surface_client`

Interface for OpenGL/Vulkan contexts. **You don't implement this** — `knst_window_opengl_content` / `knst_window_vulkan_content` implement it internally.

```cpp
class knst_surface_client {
public:
    virtual void on_surface_release() noexcept = 0;
    virtual bool on_surface_recreate() noexcept = 0;
    virtual void on_window_destroyed() noexcept {}
};
```

**When it's called:**
- Android `APP_CMD_TERM_WINDOW` → `on_surface_release`
- Android `APP_CMD_INIT_WINDOW` → `on_surface_recreate`
- Windows/X11 `destroy()` → `on_window_destroyed`
- Wayland surface destruction → `on_surface_release`

If you write your own renderer, implement this interface and register with `window.register_surface_client(this)`.

---

### 5.10 `knst_window_opengl_content`

OpenGL context manager. One per window. Implements `knst_surface_client`.

```cpp
class knst_window_opengl_content : public knst_surface_client {
public:
    bool Init(knst_window* window, bool reuse_old = false) noexcept;
    bool InitShared(knst_window* window, const knst_window_opengl_content& share) noexcept;
    void Shutdown() noexcept;

    void MakeCurrent() noexcept;
    void SwapBuffers() noexcept;

    void SetSwapInterval(int interval) noexcept;
    int GetSwapInterval() const noexcept;

    void OnResize(int new_width, int new_height) noexcept;
    void OnScaleChanged() noexcept;

    bool RecreateSurface() noexcept;
    void ReleaseSurface() noexcept;

    bool IsValid() const noexcept;
    bool HasSurface() const noexcept;
    knst_window* GetWindow() const noexcept;

    int GetWidth() const noexcept;
    int GetHeight() const noexcept;
    int GetFramebufferWidth() const noexcept;
    int GetFramebufferHeight() const noexcept;

    const char* GetVendorString() const noexcept;
    const char* GetRendererString() const noexcept;
    const char* GetVersionString() const noexcept;
    const char* GetGLSLVersionString() const noexcept;
    int GetActualMajorVersion() const noexcept;
    int GetActualMinorVersion() const noexcept;
    knst_opengl_global::ClientAPI GetClientAPI() const noexcept;

    static void* LoadGLProc(const char* name) noexcept;
};
```

---

### 5.11 `knst_opengl_global`

Shared GL state. One per process.

```cpp
class knst_opengl_global {
public:
    enum class ClientAPI : uint8_t { OpenGL, OpenGLES };
    enum class Robustness : uint8_t { Off, NoReset, Reset };
    enum class ReleaseBehavior : uint8_t { Flush, NoFlush };

    struct Options {
        ClientAPI client_api = ClientAPI::OpenGL; // Android default: OpenGLES
        int gl_major = 3;
        int gl_minor = 3;
        bool core_profile = true;
        bool forward_compat = false;
        bool debug_context = false;
        bool robust_access = false;
        Robustness robustness = Robustness::Off;
        ReleaseBehavior release_behavior = ReleaseBehavior::Flush;
        int red_bits = 8, green_bits = 8, blue_bits = 8, alpha_bits = 8;
        int depth_bits = 24, stencil_bits = 8;
        int accum_red_bits = 0, accum_green_bits = 0;
        int accum_blue_bits = 0, accum_alpha_bits = 0;
        int aux_buffers = 0;
        bool double_buffer = true;
        bool stereo = false;
        bool srgb = false;
        bool msaa = false;
        int  msaa_samples = 4;
        bool vsync = false;
    };

    static bool Init() noexcept;
    static bool Init(const Options& opts) noexcept;
    static void CleanUp() noexcept;
    static bool IsInitialized() noexcept;
    static const Options& GetOptions() noexcept;

#if KNST_USING_PLATFORM_WINDOWS
    static HINSTANCE GetHInstance() noexcept;
#else
    static EGLDisplay GetEGLDisplay() noexcept;
    static EGLConfig GetEGLConfig() noexcept;
#endif
};
```

---

### 5.12 `knst_window_vulkan_content`

Vulkan surface manager. One per window. Implements `knst_surface_client`.

```cpp
class knst_window_vulkan_content : public knst_surface_client {
public:
    void ReleaseSurface() noexcept;
    bool RecreateSurface() noexcept;

    static const char** RequiredInstanceExtensions(uint32_t* out_count) noexcept;
    static bool GetPhysicalDevicePresentationSupport(
        VkPhysicalDevice device, uint32_t queue_family) noexcept;

    bool Init(knst_window* window, VkInstance instance) noexcept;
    void Shutdown() noexcept;

    bool IsValid() const noexcept;
    bool HasSurface() const noexcept;
    VkSurfaceKHR GetSurface() const noexcept;
    VkInstance GetInstance() const noexcept;
    knst_window* GetWindow() const noexcept;
};
```

---

## 6. Function Reference

### 6.1 Lifecycle

#### `void creation() noexcept`
**Creates** the window but doesn't show it. Platform resource is created, the window is registered with the event system.

**When to use:**
- When you want to configure before it's visible
- To show the first frame without a black flash
- To set up a custom rendering (OpenGL/Vulkan) context first

#### `void show() noexcept`
Shows the window on screen.

#### `void creation_and_show() noexcept`
Shortcut for `creation()` + `show()`. Most commonly used.

#### `void destroy() noexcept`
Destroys the window, releases resources. The destructor calls it automatically, but calling it **explicitly** is recommended — for orderly cleanup.

**Order matters:**
```
gl.Shutdown()  →  window.destroy()  →  KnstWindowSources::CleanUp()
```

#### `void should_close() noexcept`
Signals to close. `is_should_close()` returns `true` on the next call.

**Critical:** `should_close()` **only sets a flag**, nothing else. Even on Android it doesn't call lifecycle methods — that's `KnstWindowSources::CleanUp()`'s job.

#### `bool is_should_close() const noexcept`
Main loop condition.

**Android special:** Auto-returns `true` if `app->destroyRequested != 0` — works when the user closes the app from Recents.

#### `bool is_disconnected() const noexcept`
Returns `true` when the Wayland compositor shuts down. Also set on X11/Windows when the connection drops.

### 6.2 Title and Meta

#### `void set_title(const knst_c16string&) noexcept`
#### `const knst_c16string& get_title() const noexcept`

UTF-16. Native UTF-16 on Windows (W API), converted to UTF-8 on Linux/Android.

### 6.3 Position and Size

#### `void move(int root_x, int root_y) noexcept`
#### `void move(int root_x, int root_y, const knst_monitor& monitor) noexcept`

Moves to screen coordinates. The second overload moves **relative to** the monitor.

**Wayland:** Doesn't work (compositor forbids).
**X11/Windows:** WM restrictions may apply.

#### `void resize(int width, int height) noexcept`
Resizes the window.

**Wayland:** Compositor reports new size in the next `configure` event.
**Windows:** `AdjustWindowRectEx` accounts for the frame.

#### `void set_minimum_size(int width, int height) noexcept`
#### `void set_maximum_size(int width, int height) noexcept`

If `KNST_WINDOW_DEFAULT` is passed, the limit is **removed**.

### 6.4 Window State

#### `void toggle_fullscreen(bool fullscreen) noexcept`
Fullscreen. `xdg_toplevel_set_fullscreen` on Wayland.

**Note:** Maximize != Fullscreen.
- Maximize → working area of the screen (taskbar visible)
- Fullscreen → the entire screen (taskbar hidden)

#### `void set_minimized() noexcept`
#### `void set_maximized() noexcept`
#### `void restore() noexcept`
`restore()` exits minimize, maximize, and fullscreen.

#### `void hide() noexcept`
#### `void focus() noexcept`

#### `void set_opacity(float opacity) noexcept`
#### `float get_opacity() const noexcept`
0.0 = invisible, 1.0 = opaque.

**Platform:**
- Windows: `WS_EX_LAYERED` + `SetLayeredWindowAttributes`
- X11: `_NET_WM_WINDOW_OPACITY` property
- Wayland: no `wp_alpha_modifier_v1` → only the opaque region changes
- Android: Ignored

### 6.5 Window Attributes

#### `void set_attribute(int attribute, bool value) noexcept`
#### `bool get_attribute(int attribute) const noexcept`

| Attribute | Meaning |
|-----------|---------|
| `KNST_WINDOW_ATTR_DECORATED` | Show system frame |
| `KNST_WINDOW_ATTR_RESIZABLE` | User can resize |
| `KNST_WINDOW_ATTR_ALWAYS_ON_TOP` | Above other windows |
| `KNST_WINDOW_ATTR_CLICK_THROUGH` | Mouse clicks pass through |
| `KNST_WINDOW_ATTR_TRANSPARENT` | **Legacy name**, same thing |

### 6.6 Title Bar (CSD)

#### `void set_title_bar_height(int height) noexcept`
#### `int get_title_bar_height() const noexcept`
`-1` → auto DPI calculation (default 34px @ 96 DPI).

**Auto calculation:**
- Wayland: 34px (logical, compositor scales)
- X11/Windows: 34 * (dpi / 96), minimum 20px

#### `template<typename F> void set_csd_hit_test(F&& cb) noexcept`
Installs a custom hit-test callback. **Activates CSD mode.**

**Callback signature:**
```cpp
knst_window::csd_hit cb(const knst_window& w, int x, int y);
```

**Return values:**

| Value | Effect |
|-------|--------|
| `csd_hit::normal` | Event goes to the app |
| `csd_hit::draggable` | OS drags the window |
| `csd_hit::resize_top_left` | Top-left corner resize |
| `csd_hit::resize_top` | Top edge |
| `csd_hit::resize_top_right` | Top-right corner |
| `csd_hit::resize_right` | Right edge |
| `csd_hit::resize_bottom_right` | Bottom-right corner |
| `csd_hit::resize_bottom` | Bottom edge |
| `csd_hit::resize_bottom_left` | Bottom-left corner |
| `csd_hit::resize_left` | Left edge |

#### `void clear_csd_hit_test() noexcept`
Exits CSD mode, reverts to the OS frame.

#### `bool is_csd_enabled() const noexcept`
Is CSD mode on?

#### `void set_csd_auto_cursor(bool enable) noexcept`
#### `bool is_csd_auto_cursor() const noexcept`
Automatically change the cursor on mouse motion? (default: `true`)

#### `csd_hit hit_test_csd(int x, int y) const noexcept`
Manual hit-test.

#### `void refresh_csd_cursor(int x, int y) noexcept`
Manually refresh the cursor.

#### `void csd_handle_hit(csd_hit hit, int x, int y, uint32_t input_serial = 0) noexcept`
CSD hit → OS action (drag/resize). Called automatically by the library.

#### `int get_content_y_offset() const noexcept`
#### `int get_content_height() const noexcept`
Content area Y offset and height.

### 6.7 Cursor

#### `void set_cursor(uint16_t cursor_type) noexcept`
System cursor.

#### `void set_bmp_cursor(const knst_byte_string& data, int w, int h, int hot_x = -1, int hot_y = -1) noexcept`
Custom cursor in **RGBA8888**. `-1` → auto-center.

#### `void reset_cursor() noexcept`
Returns to the default arrow.

#### `void set_cursor_mode(int mode) noexcept`

| Mode | Effect |
|------|--------|
| `KNST_WINDOW_CURSOR_ARROW` | Normal |
| `KNST_WINDOW_CURSOR_HIDDEN` | Invisible but still moves |
| `KNST_WINDOW_CURSOR_DISABLED` | Invisible + locked to center |

#### `void set_cursor_pos_on_window(int x, int y) noexcept`
#### `void set_cursor_pos_global(int root_x, int root_y) noexcept`
**Wayland:** Doesn't work.

### 6.8 Clipboard

#### `void set_clipboard(const knst_c16string& text) noexcept`
Writes to clipboard. UTF-16.

#### `void request_clipboard() noexcept`
#### `const knst_c16string& get_clipboard() const noexcept`
#### `void clear_clipboard() noexcept`

**Asynchronous read (Wayland):**
```cpp
window.request_clipboard();
// 1-2 frames later:
const auto& text = window.get_clipboard();
```

#### `void set_primary_selection(const knst_c16string& text) noexcept`
#### `void request_primary_selection() noexcept`
X11/Wayland middle-click paste. **No-op** on Windows/Android.

### 6.9 Drag & Drop

#### `void set_drag_drop_status(bool enabled) noexcept`
Enable/disable file drag-and-drop. **Ignored on Android.**

**Events:**
- `FILE_DROP_ENTER` — mouse entered
- `FILE_DROP_MOVE` — dragging
- `FILE_DROP_LEAVE` — left
- `FILE_DROP` — dropped, `drop_files` is set

### 6.10 Icon

#### `void apply_bmp_icon(const knst_byte_string& data, int w, int h) noexcept`
RGBA8888. Works on **Windows** and **X11**. No-op on Wayland/Android.

### 6.11 Raw Mouse

#### `void set_raw_mouse_mode(bool enable) noexcept`
Relative mouse motion. For FPS cameras.

**Event:** `MOTION` with `is_raw = true`, `raw_delta_x/y` filled.

**Platform:**
- Windows: `RegisterRawInputDevices` + `WM_INPUT`
- X11: XInput2 `RawMotion` (select on root window)
- Wayland: `zwp_relative_pointer_v1` (requires pointer lock)
- Android: Ignored

### 6.12 Idle Inhibit

#### `void set_idle_inhibit(bool enable) noexcept`
Prevents the screen from sleeping.

- **Windows:** `SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)`
- **Wayland:** `zwp_idle_inhibit_manager_v1`
- **X11:** No-op (no standard)

### 6.13 Display Affinity

#### `void set_display_affinity(bool protect) noexcept`
**Windows only.** `WDA_EXCLUDEFROMCAPTURE` — hides the window from screen capture (for DRM).

### 6.14 Touch Screen

#### `void enable_touch_screen(bool enable) noexcept`
#### `bool supports_touch() const noexcept`
#### `bool is_touch_enabled() const noexcept`
#### `int max_touch_points() const noexcept`

**Three layers:**

| Layer | Function | When |
|-------|----------|------|
| Compile | `KNST_WINDOW_ENABLE_TOUCH_SCREEN` | Build |
| Hardware | `supports_touch()` | Runtime |
| User | `is_touch_enabled()` | Runtime |

**Android:** Always on, macro has no effect.

**Fallback:** If touch is off → converted to mouse events. If no hardware → mouse events come normally.

### 6.15 Event Queue

#### `size_t event_count() const noexcept`
#### `const knst_window_event& get_window_event_handle(size_t i) const noexcept`
#### `void clear_events() noexcept`

Ring buffer read. **Call `clear_events()` at the end of every frame.**

```cpp
for (size_t i = 0; i < w.event_count(); ++i) {
    const auto& ev = w.get_window_event_handle(i);
    // ...
}
w.clear_events();  // ← MANDATORY
```

**Ring size:** 256 slots. MOTION/RESIZE/EXPOSE coalesced.

#### `void dispatch_event(const knst_window_event& ev) noexcept`
#### `void dispatch_current_event() noexcept`
Injects an event. Used internally.

### 6.16 Key State

#### `bool is_key_held(int key_code) const noexcept`
#### `bool is_caps_lock_on() const noexcept`
#### `bool is_num_lock_on() const noexcept`

Instant key state without waiting for events. **Ideal for game movement controls.**

### 6.17 User Data

#### `void set_user_data(void* data) noexcept`
#### `const void* get_user_data() const noexcept`
Passed as the second parameter to the callback.

### 6.18 Callback

#### `template<typename F> void set_redraw_callback(F&& cb) noexcept`
Callback signature: `void(knst_window&, void*)`

#### `void call_redraw_callback() noexcept`
**Manually** invokes the callback. Wraps `present_pre()` + callback + `present_post()`.

### 6.19 Frame Gating

#### `bool is_frame_ready() const noexcept`
#### `void request_frame_callback() noexcept`

**Wayland:** The compositor sends a callback when ready to draw. Not drawing when `false` avoids stalls.

**Windows/X11/Android:** Always `true` (on Android, checks `app->window != nullptr`).

### 6.20 Platform Handles

```cpp
HWND hwnd = window.get_windows_window_handle(); // Windows
xcb_window_t win = window.get_x11_window_handle(); // X11
const wl_surface* surf = window.get_wayland_surface_handle(); // Wayland
```

### 6.21 Queries

```cpp
int get_width() const noexcept;
int get_height() const noexcept;
int get_root_x() const noexcept;
int get_root_y() const noexcept;
bool is_maximized() const noexcept;
bool is_minimized() const noexcept;
bool is_fullscreen() const noexcept;
bool is_focused() const noexcept;
```

### 6.22 GL Context Lost

#### `bool is_gl_context_lost() noexcept`
Returns `true` when **EGL_CONTEXT_LOST** occurs. Reading resets the flag.

```cpp
if (window.is_gl_context_lost()) {
    rebuild_shaders();
    rebuild_vaos();
    rebuild_textures();
}
```

Only triggers on **Android/Linux EGL**. Always `false` on Windows.

---

## 7. All Functions -- Summary

> This section is for quick reference. Each function is on a single line, with a `//` comment explaining its purpose. I added it because I think it's great for general learning.

### 7.1 `knst_window` — Lifecycle

```cpp
knst_window(int width, int height, knst_c16string title, int root_x, int root_y, const knst_monitor& monitor);
            
// Create window object (native window not created yet — call creation())

void creation() noexcept; // Create native window but don't show it (for pre-config)
void show() noexcept; // Show the window on screen
void creation_and_show() noexcept; // Shortcut for creation() + show()
void destroy() noexcept; // Destroy the window, release resources
void should_close() noexcept; // Send close signal (sets flag, doesn't destroy)
bool is_should_close() const noexcept; // Main loop condition (may auto-true on Android)
bool is_disconnected() const noexcept; // Wayland compositor / connection lost?
```

### 7.2 `knst_window` — Title & Meta

```cpp
void set_title(const knst_c16string& title) noexcept; // Change window title (UTF-16)
const knst_c16string& get_title() const noexcept; // Read the current title
```

### 7.3 `knst_window` — Position & Size

```cpp
void move(int root_x, int root_y) noexcept; // Move to screen coordinates (Wayland: no-op)
void move(int root_x, int root_y, const knst_monitor& monitor) noexcept; // Move relative to a monitor
void resize(int width, int height) noexcept; // Resize window (asynchronous)
void set_minimum_size(int width, int height) noexcept; // Minimum size limit (DEFAULT → remove)
void set_maximum_size(int width, int height) noexcept; // Maximum size limit (DEFAULT → remove)
```

### 7.4 `knst_window` — Window State

```cpp
void toggle_fullscreen(bool fullscreen) noexcept; // Toggle fullscreen (different from maximize)
void set_minimized() noexcept; // Minimize to icon
void set_maximized() noexcept; // Maximize to screen work area (taskbar visible)
void restore() noexcept; // Exit minimize / maximize / fullscreen
void hide() noexcept;  // Hide window (not destroy)
void focus() noexcept;  // Bring to front and focus
```

### 7.5 `knst_window` — Visibility

```cpp
void set_opacity(float opacity) noexcept; // Window opacity 0.0-1.0 (Wayland/Android: limited)
float get_opacity() const noexcept; // Read the current opacity
```

### 7.6 `knst_window` — Window Attributes

```cpp
void set_attribute(int attribute, bool value) noexcept; // Toggle ATTR_* attribute
bool get_attribute(int attribute) const noexcept; // Read current ATTR_* value
```

### 7.7 `knst_window` — CSD (Custom Title Bar)

```cpp
void set_title_bar_height(int height) noexcept; // CSD title bar height (-1 = auto DPI)
int get_title_bar_height() const noexcept; // Current title bar height
template<typename F> void set_csd_hit_test(F&& cb) noexcept; // Install CSD hit-test callback (activates CSD)
void clear_csd_hit_test() noexcept; // Disable CSD, revert to OS frame
bool is_csd_enabled() const noexcept; // Is CSD active?
void set_csd_auto_cursor(bool enable) noexcept; // Auto cursor change near edges?
bool is_csd_auto_cursor() const noexcept; // Is auto-cursor active?
csd_hit hit_test_csd(int x, int y) const noexcept; // CSD hit-test result at point
void refresh_csd_cursor(int x, int y) noexcept; // Manually trigger auto-cursor
void csd_handle_hit(csd_hit hit, int x, int y, uint32_t serial = 0) noexcept; // CSD hit → OS drag/resize
int get_content_y_offset() const noexcept; // Title bar height (content start Y)
int get_content_height() const noexcept;  // Content height excluding title bar
```

### 7.8 `knst_window` — Cursor

```cpp
void set_cursor(uint16_t cursor_type) noexcept;  // System cursor
void set_bmp_cursor(const knst_byte_string& data, int w, int h, int hot_x = -1, int hot_y = -1) noexcept; // RGBA8888 bitmap cursor
void reset_cursor() noexcept; // Return to default arrow
void set_cursor_mode(int mode) noexcept; // ARROW / HIDDEN / DISABLED
void set_cursor_pos_on_window(int x, int y) noexcept; // Move cursor in-window (Wayland: no-op)
void set_cursor_pos_global(int root_x, int root_y) noexcept; // Move cursor on screen (Wayland: no-op)
```

### 7.9 `knst_window` — Clipboard

```cpp
void set_clipboard(const knst_c16string& text) noexcept; // Write to clipboard (UTF-16)
void request_clipboard() noexcept; // Read from clipboard (async — CLIPBOARD_DATA event fires)
const knst_c16string& get_clipboard() const noexcept; // Last-read clipboard contents
void clear_clipboard() noexcept; // Clear clipboard contents (local cache only)
void set_primary_selection(const knst_c16string& text) noexcept; // X11/Wayland middle-click paste write
void request_primary_selection() noexcept; // X11/Wayland middle-click paste read
```

### 7.10 `knst_window` — Drag & Drop

```cpp
void set_drag_drop_status(bool enabled) noexcept; // Enable/disable file drag-and-drop (Android: no-op)
```

### 7.11 `knst_window` — Icon

```cpp
void apply_bmp_icon(const knst_byte_string& data, int width, int height) noexcept;  // RGBA8888 window icon
```

### 7.12 `knst_window` — Input Modes

```cpp
void set_raw_mouse_mode(bool enable) noexcept; // FPS-style relative mouse motion (Android: no-op)
void set_idle_inhibit(bool enable) noexcept; // Prevent screen sleep (Win/Wayland: works, X11: no-op)
void set_display_affinity(bool protect) noexcept; // Hide from screen capture (Windows only)
```

### 7.13 `knst_window` — Touch Screen

```cpp
void enable_touch_screen(bool enable) noexcept; // Enable/disable touch events (runtime)
bool supports_touch() const noexcept; // Does the hardware support touch?
bool is_touch_enabled() const noexcept; // Did the user enable touch?
int max_touch_points() const noexcept; // Maximum simultaneous finger count
```

### 7.14 `knst_window` — Event Queue

```cpp
size_t event_count() const noexcept; // Number of events in this frame
const knst_window_event& get_window_event_handle(size_t i) const noexcept; // Read event i
void clear_events() noexcept; // Clear events at end of frame (MANDATORY)
void dispatch_event(const knst_window_event& ev) noexcept; // Manually inject event
void dispatch_current_event() noexcept;  // Push m_knst_event to the queue
```

### 7.15 `knst_window` — Key State

```cpp
bool is_key_held(int key_code) const noexcept; // Is the key currently held? (no event needed)
bool is_caps_lock_on() const noexcept; // Is CapsLock on?
bool is_num_lock_on() const noexcept; // Is NumLock on?
```

### 7.16 `knst_window` — User Data & Callback

```cpp
void set_user_data(void* data) noexcept; // User data passed to callback
const void* get_user_data() const noexcept; // Read the stored user data
template<typename F> void set_redraw_callback(F&& cb) noexcept; // Callback invoked every frame
void call_redraw_callback() noexcept; // Manually invoke callback (wraps present_pre+cb+present_post)
```

### 7.17 `knst_window` — Frame Gating

```cpp
bool is_frame_ready() const noexcept; // Wayland: compositor ready? (others: true)
void request_frame_callback() noexcept; // Wayland: request the next frame callback
```

### 7.18 `knst_window` — Queries

```cpp
int get_width() const noexcept; // Window content width (logical)
int get_height() const noexcept; // Window content height (logical)
int get_root_x() const noexcept; // Window X position (screen)
int get_root_y() const noexcept; // Window Y position (screen)
bool is_maximized() const noexcept; // Maximized?
bool is_minimized() const noexcept; // Minimized?
bool is_fullscreen() const noexcept; // Fullscreen?
bool is_focused() const noexcept; // Focused?
```

### 7.19 `knst_window` — GL Context & Wayland

```cpp
bool is_gl_context_lost() noexcept; // EGL_CONTEXT_LOST detected? (reading resets the flag)
void notify_gl_context_lost() noexcept; // Internal — set context-lost flag

void apply_wayland_viewport_resize(int logical_w, int logical_h) noexcept; // Resize Wayland viewport
int wayland_physical_width(int logical_w) const noexcept; // Wayland: physical (buffer) width
int wayland_physical_height(int logical_h) const noexcept; // Wayland: physical (buffer) height
```

### 7.20 `knst_window` — Surface Client & Static

```cpp
void register_surface_client(knst_surface_client* client) noexcept; // Register surface client (internal)
void unregister_surface_client(knst_surface_client* client) noexcept; // Unregister surface client (internal)
void notify_window_destroyed() noexcept; // Notify all surface clients of destroy
void notify_surface_release() noexcept; // Notify all surface clients of release
void notify_surface_recreate() noexcept; // Notify all surface clients of recreate

static knst_window_capabilities capabilities() noexcept; // Platform capability matrix (runtime)
```

### 7.21 `KnstWindowSources` — Platform Resources

```cpp
static void Init() noexcept; // Windows/Linux: initialize platform resources
static void Init(android_app* app); // Android: initialize with android_app
static void CleanUp() noexcept; // Clean up platform resources (Android: pumps looper)
static uint32_t get_current_time_ms() noexcept; // Monotonic time (ms) — for event timestamps

static knst_backend active_backend() noexcept; // Active backend
static bool is_x11_active() noexcept; // Is active backend X11?
static bool is_wayland_active() noexcept; // Is active backend Wayland?
static bool is_windows_active() noexcept; // Is active backend Windows?

#if KNST_USING_PLATFORM_WINDOWS
static HINSTANCE& get_windows_native_instance_handle() noexcept; // Windows HINSTANCE
#elif KNST_USING_LINUX_PLATFORM_X11
static xcb_window_t& get_native_x11_root_handle() noexcept; // X11 root window
static xcb_connection_t* get_native_x11_connection_handle() noexcept; // X11 XCB connection
static Display* get_native_x11_display() noexcept; // X11 Display (Xlib)
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
static wl_display* wayland_display; // Wayland wl_display (public)
#elif defined(KNST_USING_PLATFORM_ANDROID)
static android_app* get_android_app() noexcept; // Android app pointer
#endif
```

### 7.22 `knst_window_event_system` — Global Event Pump

```cpp
static void block_pool_event() noexcept; // Block until an event (Wayland: don't use)
static void non_block_pool_event() noexcept; // Immediately pull and dispatch events (every frame)
static size_t get_window_count() noexcept; // Number of registered windows
```

### 7.23 `knst_display` — Monitor Management

```cpp
static void refresh_screens() noexcept; // Refresh monitor list
static const knst_vector<std::unique_ptr<knst_monitor>>& get_monitor_list() noexcept; // All monitors
static const knst_monitor* get_primary_monitor() noexcept; // Primary monitor
static size_t get_monitor_count() noexcept; // Monitor count
static const knst_monitor* get_monitor(size_t index) noexcept; // Monitor i
```

### 7.24 `knst_mobile_keyboard` — Android Soft Keyboard

```cpp
static bool show() noexcept; // Show Android soft keyboard (PC: returns false, no-op)
static bool hide() noexcept; // Hide Android soft keyboard (PC: returns false, no-op)
static void toggle() noexcept; // Toggle (300ms debounce)
static bool is_visible() noexcept;  // Is keyboard visible? (mutex-protected)
```

### 7.25 `knst_opengl_global` — GL Context Init

```cpp
static bool Init() noexcept; // GL global init with default Options
static bool Init(const Options& opts) noexcept; // GL global init with given Options
static void CleanUp() noexcept; // GL global cleanup
static bool IsInitialized() noexcept; // Is GL global ready?
static const Options& GetOptions() noexcept; // Read current Options
static HINSTANCE GetHInstance() noexcept; // Windows: HINSTANCE
static EGLDisplay GetEGLDisplay() noexcept; // EGL display (non-Windows)
static EGLConfig GetEGLConfig() noexcept; // EGL config (non-Windows)
```

### 7.26 `knst_window_opengl_content` — Per-Window GL Context

```cpp
bool Init(knst_window* window, bool reuse_old = false) noexcept; // Attach GL context to window
bool InitShared(knst_window* window, const knst_window_opengl_content& share) noexcept; // Shared context
void Shutdown() noexcept; // Close GL context

void MakeCurrent() noexcept; // Make this context current
void SwapBuffers() noexcept; // Buffer swap (presents per vsync)

void SetSwapInterval(int interval) noexcept; // 0=off, 1=on, -1=adaptive
int GetSwapInterval() const noexcept; // User preference

void OnResize(int new_width, int new_height) noexcept; // Window resized
void OnScaleChanged() noexcept; // Wayland fractional-scale changed

bool RecreateSurface() noexcept; // Recreate surface (Android INIT_WINDOW)
void ReleaseSurface() noexcept; // Release surface (Android TERM_WINDOW)

bool IsValid() const noexcept; // Is context valid?
bool HasSurface() const noexcept; // Is surface valid?
knst_window* GetWindow() const noexcept; // Attached window

int GetWidth() const noexcept; // Logical width
int GetHeight() const noexcept; // Logical height
int GetFramebufferWidth() const noexcept;  // Physical width
int GetFramebufferHeight() const noexcept; // Physical height

const char* GetVendorString() const noexcept; // GPU vendor
const char* GetRendererString() const noexcept; // GPU renderer
const char* GetVersionString() const noexcept; // GL version string
const char* GetGLSLVersionString() const noexcept; // GLSL version string
int GetActualMajorVersion() const noexcept; // Actual GL major
int GetActualMinorVersion() const noexcept; // Actual GL minor
knst_opengl_global::ClientAPI GetClientAPI() const noexcept; // OpenGL or OpenGLES?

static void* LoadGLProc(const char* name) noexcept;  // GLAD/GLEW loader entry
```

### 7.27 `knst_window_vulkan_content` — Per-Window Vulkan Surface

```cpp
void ReleaseSurface() noexcept;   // Release VkSurfaceKHR
bool RecreateSurface() noexcept;  // Recreate VkSurfaceKHR

static const char** RequiredInstanceExtensions(uint32_t* out_count) noexcept; // Required VkInstance extensions
static bool GetPhysicalDevicePresentationSupport(
    VkPhysicalDevice device, uint32_t queue_family) noexcept; // Does physical device support present?

bool Init(knst_window* window, VkInstance instance) noexcept; // Create VkSurfaceKHR
void Shutdown() noexcept; // Clean up VkSurfaceKHR

bool IsValid() const noexcept; // Is surface valid?
bool HasSurface() const noexcept; // Is VkSurfaceKHR != VK_NULL_HANDLE?
VkSurfaceKHR GetSurface() const noexcept; // VkSurfaceKHR handle
VkInstance GetInstance() const noexcept; // VkInstance handle
knst_window* GetWindow() const noexcept; // Attached window
```

### 7.28 `knst_window_event` — Event Fields

```cpp
void begin(uint32_t ev_type) noexcept;   // Reset event + set type & timestamp (called internally)
```

---

## 8. Macro Reference

### 8.1 Event Types — `ev.type`

#### Ranges

| Range | Category |
|-------|----------|
| 0 | `UNKNOWN` — not dispatched |
| 100-103 | Mouse |
| 200-201 | Keyboard / text |
| 300-309 | Window state |
| 400-401 | Focus |
| 450-453 | Clipboard / IME |
| 500-503 | File drop |
| 600 | Touch |
| 603-632 | Mobile (Android) |
| 700-709 | Application lifecycle |

#### Mouse (100-199)

| Macro | Value | When? |
|-------|-------|-------|
| `KNST_WINDOW_EVENT_MOUSE` | 100 | Press / release / scroll |
| `KNST_WINDOW_EVENT_MOTION` | 101 | Motion (normal or raw) |
| `KNST_WINDOW_EVENT_ENTER` | 102 | Mouse entered the window |
| `KNST_WINDOW_EVENT_LEAVE` | 103 | Mouse left |

#### Keyboard (200-299)

| Macro | Value | When? |
|-------|-------|-------|
| `KNST_WINDOW_EVENT_KEYBOARD` | 200 | Press / release / repeat |
| `KNST_WINDOW_EVENT_TEXT_INPUT` | 201 | Character input (IME, shift+digit, AltGr) |

**Important:** The `KEYBOARD` event gives `key_code` (physical key). The `TEXT_INPUT` event gives `text_input` (produced character).

**Example difference:**
- `Shift+1` → `KEYBOARD` `key_code = '1'` + `mods = SHIFT`
- `Shift+1` → `TEXT_INPUT` `text_input = "!"`

#### Window (300-399)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_RESIZE` | 300 |
| `KNST_WINDOW_EVENT_MOVE` | 301 |
| `KNST_WINDOW_EVENT_MAXIMIZE` | 302 |
| `KNST_WINDOW_EVENT_MINIMIZE` | 303 |
| `KNST_WINDOW_EVENT_RESTORE` | 304 |
| `KNST_WINDOW_EVENT_FULLSCREEN` | 305 |
| `KNST_WINDOW_EVENT_EXPOSE` | 306 |
| `KNST_WINDOW_EVENT_CLOSE` | 307 |
| `KNST_WINDOW_EVENT_DISCONNECT` | 308 |
| `KNST_WINDOW_EVENT_SCALE_CHANGED` | 309 |

#### Focus (400-499)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_FOCUS_IN` | 400 |
| `KNST_WINDOW_EVENT_FOCUS_OUT` | 401 |

#### Clipboard / IME (450-453)

| Macro | Value | When? |
|-------|-------|-------|
| `KNST_WINDOW_EVENT_CLIPBOARD_UPDATE` | 450 | Clipboard changed (Windows only) |
| `KNST_WINDOW_EVENT_CLIPBOARD_DATA` | 451 | `request_clipboard()` result ready |
| `KNST_WINDOW_EVENT_TEXT_PREEDIT` | 452 | IME preedit updated |
| `KNST_WINDOW_EVENT_TEXT_DELETE_SURROUNDING` | 453 | IME wants to delete text |

#### File Drop (500-599)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_FILE_DROP` | 500 |
| `KNST_WINDOW_EVENT_FILE_DROP_ENTER` | 501 |
| `KNST_WINDOW_EVENT_FILE_DROP_MOVE` | 502 |
| `KNST_WINDOW_EVENT_FILE_DROP_LEAVE` | 503 |

#### Touch (600)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_TOUCH` | 600 |
| `KNST_WINDOW_EVENT_MOBILE_TOUCH` | 600 (alias) |

#### Mobile (603-632)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_MOBILE_MENU` | 603 |
| `KNST_WINDOW_EVENT_MOBILE_SEARCH` | 604 |
| `KNST_WINDOW_EVENT_MOBILE_APP_SWITCH` | 605 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_UP` | 607 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_DOWN` | 608 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_MUTE` | 609 |
| `KNST_WINDOW_EVENT_MOBILE_POWER` | 610 |
| `KNST_WINDOW_EVENT_MOBILE_CAMERA` | 611 |
| `KNST_WINDOW_EVENT_MOBILE_HELP` | 612 |
| `KNST_WINDOW_EVENT_MOBILE_SETTINGS` | 613 |
| `KNST_WINDOW_EVENT_MOBILE_SLEEP` | 614 |
| `KNST_WINDOW_EVENT_MOBILE_WAKEUP` | 615 |
| `KNST_WINDOW_EVENT_MOBILE_ASSIST` | 616 |
| `KNST_WINDOW_EVENT_MOBILE_BOOKMARK` | 617 |
| `KNST_WINDOW_EVENT_MOBILE_CALCULATOR` | 618 |
| `KNST_WINDOW_EVENT_MOBILE_CALENDAR` | 619 |
| `KNST_WINDOW_EVENT_MOBILE_CONTACTS` | 620 |
| `KNST_WINDOW_EVENT_MOBILE_EXPLORER` | 621 |
| `KNST_WINDOW_EVENT_MOBILE_MUSIC` | 622 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PLAY_PAUSE` | 623 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_STOP` | 624 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_NEXT` | 625 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PREVIOUS` | 626 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_REWIND` | 627 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_FORWARD` | 628 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_RECORD` | 629 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PAUSE` | 630 |
| `KNST_WINDOW_EVENT_MOBILE_BACK` | 631 |
| `KNST_WINDOW_EVENT_MOBILE_HOME` | 632 |

#### Application Lifecycle (700-799)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_APP_STARTED` | 700 |
| `KNST_WINDOW_EVENT_APP_RESUMED` | 701 |
| `KNST_WINDOW_EVENT_APP_PAUSED` | 702 |
| `KNST_WINDOW_EVENT_APP_STOPPED` | 703 |
| `KNST_WINDOW_EVENT_APP_SAVE_STATE` | 704 |
| `KNST_WINDOW_EVENT_APP_LOW_MEMORY` | 705 |
| `KNST_WINDOW_EVENT_APP_CONFIG_CHANGED` | 706 |
| `KNST_WINDOW_EVENT_APP_INPUT_CHANGED` | 707 |
| `KNST_WINDOW_EVENT_APP_CONTENT_RECT` | 708 |
| `KNST_WINDOW_EVENT_APP_WINDOW_LOST` | 709 |

### 8.2 Key Actions — `ev.key_action`

| Macro | Value | Meaning |
|-------|-------|---------|
| `KNST_WINDOW_KEY_ACTION_PRESS` | 1 | Pressed (initial) |
| `KNST_WINDOW_KEY_ACTION_RELEASE` | 2 | Released |
| `KNST_WINDOW_KEY_ACTION_REPEAT` | 3 | Held (auto-repeat) |

### 8.3 Mouse Actions — `ev.mouse_action`

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_MOUSE_ACTION_PRESS` | 1 |
| `KNST_WINDOW_MOUSE_ACTION_RELEASE` | 2 |
| `KNST_WINDOW_MOUSE_ACTION_SCROLL` | 3 |

### 8.4 Mouse Buttons — `ev.mouse_button`

| Macro | Value | Physical |
|-------|-------|----------|
| `KNST_WINDOW_MOUSE_BUTTON_LEFT` | 1 | Left |
| `KNST_WINDOW_MOUSE_BUTTON_MIDDLE` | 2 | Middle |
| `KNST_WINDOW_MOUSE_BUTTON_RIGHT` | 3 | Right |
| `KNST_WINDOW_MOUSE_BUTTON_BACK` | 8 | Side back |
| `KNST_WINDOW_MOUSE_BUTTON_FORWARD` | 9 | Side forward |

**In scroll events:**
- Vertical scroll → `mouse_button = 4`
- Horizontal scroll → `mouse_button = 6`
- `mouse_scroll_delta` sign indicates direction (+1 up, -1 down)

### 8.5 Modifier Bits — `ev.mods`

| Macro | Value | Bit |
|-------|-------|-----|
| `KNST_WINDOW_MOD_SHIFT` | 1 | 0 |
| `KNST_WINDOW_MOD_CONTROL` | 2 | 1 |
| `KNST_WINDOW_MOD_ALT` | 4 | 2 |
| `KNST_WINDOW_MOD_SUPER` | 8 | 3 |
| `KNST_WINDOW_MOD_CAPS_LOCK` | 16 | 4 |
| `KNST_WINDOW_MOD_NUM_LOCK` | 32 | 5 |

```cpp
const bool ctrl = (ev.mods & KNST_WINDOW_MOD_CONTROL) != 0;
const bool shift = (ev.mods & KNST_WINDOW_MOD_SHIFT) != 0;
```

### 8.6 Key Codes — `ev.key_code`

#### Letters

```cpp
KNST_WINDOW_KEY_CODE_A ... KNST_WINDOW_KEY_CODE_Z
```

Value: ASCII `'A'` - `'Z'` (normalized on every platform).

#### Turkish Characters

```cpp
KNST_WINDOW_KEY_CODE_C_CEDILLA    // Ç
KNST_WINDOW_KEY_CODE_G_BREVE      // Ğ
KNST_WINDOW_KEY_CODE_I_DOTLESS    // ı
KNST_WINDOW_KEY_CODE_O_DIAERESIS  // Ö
KNST_WINDOW_KEY_CODE_S_CEDILLA    // Ş
KNST_WINDOW_KEY_CODE_U_DIAERESIS  // Ü
```

#### Digits

```cpp
KNST_WINDOW_KEY_CODE_0 ... KNST_WINDOW_KEY_CODE_9
```

#### Numpad

```cpp
KNST_WINDOW_KEY_CODE_NUMPAD_0 ... KNST_WINDOW_KEY_CODE_NUMPAD_9
KNST_WINDOW_KEY_CODE_NUMPAD_ADD / SUBTRACT / MULTIPLY / DIVIDE
KNST_WINDOW_KEY_CODE_NUMPAD_DECIMAL / ENTER
```

#### Function Keys

```cpp
KNST_WINDOW_KEY_CODE_F1 ... KNST_WINDOW_KEY_CODE_F12
```

#### Special

```cpp
KNST_WINDOW_KEY_CODE_ESCAPE / ENTER / SPACE / BACKSPACE / TAB
KNST_WINDOW_KEY_CODE_CAPS_LOCK / NUM_LOCK / SCROLL_LOCK
KNST_WINDOW_KEY_CODE_DELETE / INSERT
```

#### Modifiers

```cpp
KNST_WINDOW_KEY_CODE_SHIFT / CONTROL / ALT / SUPER / MENU
```

#### Arrows

```cpp
KNST_WINDOW_KEY_CODE_LEFT / RIGHT / UP / DOWN
```

#### Navigation

```cpp
KNST_WINDOW_KEY_CODE_HOME / END / PAGE_UP / PAGE_DOWN
KNST_WINDOW_KEY_CODE_PRINT / PAUSE / BREAK
```

#### Symbols

```cpp
KNST_WINDOW_KEY_CODE_SEMICOLON / SLASH / GRAVE
KNST_WINDOW_KEY_CODE_LEFT_BRACKET / BACKSLASH / RIGHT_BRACKET
KNST_WINDOW_KEY_CODE_APOSTROPHE / PERIOD / COMMA
KNST_WINDOW_KEY_CODE_MINUS / PLUS / EQUAL / QUOTE / COLON
KNST_WINDOW_KEY_CODE_TILDE / LESS / GREATER / QUESTION
KNST_WINDOW_KEY_CODE_PIPE / LEFT_BRACE / RIGHT_BRACE / UNDERSCORE
```

#### Media

```cpp
KNST_WINDOW_KEY_CODE_VOLUME_UP / DOWN / MUTE
KNST_WINDOW_KEY_CODE_MEDIA_PLAY / STOP / NEXT / PREV / PAUSE
```

#### Browser

```cpp
KNST_WINDOW_KEY_CODE_BROWSER_HOME / BACK / FORWARD / REFRESH / SEARCH / FAVORITES
```

### 8.7 Cursor Types

#### System Cursors (set_cursor)

| Macro | Windows | X11 | Wayland |
|-------|---------|-----|---------|
| `KNST_WINDOW_CURSOR_ARROW` | `OCR_NORMAL` | `XC_left_ptr` | `left_ptr` |
| `KNST_WINDOW_CURSOR_IBEAM` | `OCR_IBEAM` | `XC_xterm` | `xterm` |
| `KNST_WINDOW_CURSOR_CROSSHAIR` | `OCR_CROSS` | `XC_crosshair` | `crosshair` |
| `KNST_WINDOW_CURSOR_HAND` | `OCR_HAND` | `XC_hand2` | `hand2` |
| `KNST_WINDOW_CURSOR_HRESIZE` | `OCR_SIZEWE` | `XC_sb_h_double_arrow` | `sb_h_double_arrow` |
| `KNST_WINDOW_CURSOR_VRESIZE` | `OCR_SIZENS` | `XC_sb_v_double_arrow` | `sb_v_double_arrow` |
| `KNST_WINDOW_CURSOR_MOVE` | `OCR_SIZEALL` | `XC_fleur` | `fleur` |
| `KNST_WINDOW_CURSOR_WAIT` | `OCR_WAIT` | `XC_watch` | `watch` |
| `KNST_WINDOW_CURSOR_HELP` | `OCR_HELP` | `XC_question_arrow` | `question_arrow` |
| `KNST_WINDOW_CURSOR_NOT_ALLOWED` | `OCR_NO` | `XC_X_cursor` | `X_cursor` |
| `KNST_WINDOW_CURSOR_NWSE_RESIZE` | `OCR_SIZENWSE` | `nwse-resize` | `nwse-resize` |
| `KNST_WINDOW_CURSOR_NESW_RESIZE` | `OCR_SIZENESW` | `nesw-resize` | `nesw-resize` |

#### Mode Cursors (set_cursor_mode)

| Macro | Meaning |
|-------|---------|
| `KNST_WINDOW_CURSOR_ARROW` | Normal |
| `KNST_WINDOW_CURSOR_HIDDEN` | Invisible |
| `KNST_WINDOW_CURSOR_DISABLED` | Locked |

### 8.8 Window Attributes

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_ATTR_DECORATED` | 1 |
| `KNST_WINDOW_ATTR_RESIZABLE` | 2 |
| `KNST_WINDOW_ATTR_ALWAYS_ON_TOP` | 3 |
| `KNST_WINDOW_ATTR_CLICK_THROUGH` | 4 |
| `KNST_WINDOW_ATTR_TRANSPARENT` | 4 (legacy name) |

### 8.9 Touch Actions

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_TOUCH_ACTION_PRESS` | 0 |
| `KNST_WINDOW_TOUCH_ACTION_RELEASE` | 1 |
| `KNST_WINDOW_TOUCH_ACTION_MOVE` | 2 |
| `KNST_WINDOW_TOUCH_ACTION_CANCEL` | 3 |
| `KNST_WINDOW_TOUCH_ACTION_OUTSIDE` | 4 |
| `KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS` | 5 |
| `KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE` | 6 |

### 8.10 Android Screen Orientation

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_ORIENTATION_UNDEFINED` | 0 |
| `KNST_WINDOW_ORIENTATION_PORTRAIT` | 1 |
| `KNST_WINDOW_ORIENTATION_LANDSCAPE` | 2 |
| `KNST_WINDOW_ORIENTATION_SQUARE` | 3 |

### 8.11 Other Constants

| Macro | Value | Meaning |
|-------|-------|---------|
| `KNST_WINDOW_DEFAULT` | -10000 | Auto-value marker |
| `KNST_MAX_TOUCH_POINTS` | 10 | Default touch point limit |
| `KNST_MAX_HELD_KEYS` | 8 | Held-key table size |
| `KNST_WINDOW_KEY_REPEAT_DELAY` | 200 | Initial repeat (ms) |
| `KNST_WINDOW_KEY_REPEAT_INTERVAL` | 20 | Repeat interval (ms) |
| `KNST_ANDROID_KEY_OFFSET` | 0x10000 | Android keycode offset |

---

## 9. Platform Notes

### 9.1 Windows

#### Native Handle
```cpp
HWND hwnd = window.get_windows_window_handle();
```

#### Feature Matrix

| Feature | Support |
|---------|:-------:|
| move | ✅ |
| resize | ✅ |
| fullscreen | ✅ |
| minimize | ✅ |
| maximize | ✅ |
| focus | ✅ |
| opacity | ✅ |
| always_on_top | ✅ |
| click_through | ✅ |
| CSD | ✅ |
| touch | ✅ (WM_TOUCH) |
| clipboard | ✅ |
| drag_drop | ✅ (IDropTarget) |
| raw_mouse | ✅ |
| idle_inhibit | ✅ |
| display_affinity | ✅ |
| cursor_position | ✅ |
| set_icon | ✅ |
| fractional_scale | ✅ |
| multiple_windows | ✅ |

#### Automatic DPI

`SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` is called **before the first window opens**. Text and sizes are correctly scaled on high-DPI screens.

#### CSD

- `WM_NCCALCSIZE` → frame removed (flag set before first `CreateWindowExW`)
- `WM_NCHITTEST` → hit-test callback invoked, mapped to `HT*` codes
- `WM_GETMINMAXINFO` → max-size hint (temporarily removed during maximize)

#### Raw Mouse

`RegisterRawInputDevices` + `WM_INPUT`. `MOUSE_MOVE_ABSOLUTE` flag checked — only relative motion processed.

---

### 9.2 Linux X11

#### Native Handle
```cpp
xcb_window_t win = window.get_x11_window_handle();
xcb_connection_t* c = KnstWindowSources::get_native_x11_connection_handle();
Display* dpy = KnstWindowSources::get_native_x11_display();
```

#### Feature Matrix

| Feature | Support |
|---------|:-------:|
| move | ✅ |
| resize | ✅ |
| fullscreen | ✅ |
| minimize | ✅ |
| maximize | ✅ |
| focus | ✅ |
| opacity | ✅ |
| always_on_top | ✅ |
| click_through | ✅ |
| CSD | ✅ |
| touch | ✅ (XInput2) |
| clipboard | ✅ |
| primary_selection | ✅ |
| drag_drop | ✅ (Xdnd) |
| raw_mouse | ✅ (XInput2) |
| idle_inhibit | ⚠️ (none) |
| cursor_position | ✅ |
| set_icon | ✅ |
| fractional_scale | ❌ |
| multiple_windows | ✅ |

#### `_NET_WM_SYNC_REQUEST`

For flicker-free resize, `xcb_sync_counter` is used. The WM waits on a counter after each resize — the library increments it only after the new-size frame has been drawn.

---

### 9.3 Linux Wayland

#### Native Handle
```cpp
const wl_surface* surf = window.get_wayland_surface_handle();
wl_display* dpy = KnstWindowSources::wayland_display;
wl_compositor* comp = KnstWindowSources::compositor;
wl_shm* shm = KnstWindowSources::shm;
```

#### Feature Matrix

| Feature | Support |
|---------|:-------:|
| move | ❌ (protocol) |
| resize | ✅ |
| fullscreen | ✅ |
| minimize | ✅ |
| maximize | ✅ |
| focus | ❌ (compositor) |
| opacity | ❌ |
| always_on_top | ❌ |
| click_through | ✅ |
| CSD | ✅ |
| touch | ✅ (wl_touch) |
| clipboard | ✅ |
| primary_selection | ✅ |
| drag_drop | ✅ |
| raw_mouse | ✅ (relative-pointer) |
| idle_inhibit | ✅ |
| cursor_position | ❌ |
| set_icon | ✅ (xdg-toplevel-icon) |
| fractional_scale | ✅ |
| multiple_windows | ✅ |

#### ⚠️ Drawing Is Mandatory

To display a window on Wayland you must call `wl_surface_attach` + `wl_surface_commit` **every frame**. If you don't, the window doesn't appear.

**Software rendering:**

```cpp
void draw_sw(knst_window& w) {
    wl_surface* s = const_cast<wl_surface*>(w.get_wayland_surface_handle());
    if (!s || !KnstWindowSources::shm) return;

    int W = w.get_width(), H = w.get_height();
    int stride = W * 4;

    int fd = memfd_create("buf", MFD_CLOEXEC);
    ftruncate(fd, stride * H);
    uint32_t* px = (uint32_t*)mmap(nullptr, stride * H, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    for (int i = 0; i < W * H; ++i) px[i] = 0xFF202060;

    wl_shm_pool* pool = wl_shm_create_pool(KnstWindowSources::shm, fd, stride * H);
    wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_surface_attach(s, buf, 0, 0);
    wl_surface_damage_buffer(s, 0, 0, W, H);
    wl_surface_commit(s);
    wl_display_flush(KnstWindowSources::wayland_display);

    wl_buffer_destroy(buf);
    wl_shm_pool_destroy(pool);
    munmap(px, stride * H);
    close(fd);
}
```

#### Frame Callback Gating

`is_frame_ready()` returns `false` until the compositor is ready:

```cpp
if (!window.is_frame_ready()) return;
// ... draw ...
window.request_frame_callback();
gl.SwapBuffers();
```

---

### 9.4 Android

#### Native Handle
```cpp
android_app* app = KnstWindowSources::get_android_app();
ANativeWindow* win = app->window;
```

#### Feature Matrix

| Feature | Support |
|---------|:-------:|
| move | ❌ |
| resize | ❌ |
| fullscreen | ❌ |
| minimize | ❌ |
| maximize | ❌ |
| focus | ❌ |
| opacity | ❌ |
| always_on_top | ❌ |
| click_through | ❌ |
| CSD | ❌ |
| touch | ✅ (built-in) |
| clipboard | ✅ (JNI) |
| primary_selection | ❌ |
| drag_drop | ❌ |
| raw_mouse | ❌ |
| idle_inhibit | ❌ |
| cursor_position | ❌ |
| set_icon | ❌ |
| fractional_scale | ✅ |
| multiple_windows | ❌ |

#### `android_main`

```cpp
void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);
    // ...
}
```

`app->window` starts as `NULL` — `Init()` waits for it.

---

## 10. Internal Design, Tactics, and Background

> This section explains the library's **internal design**, **why certain decisions were made**, and **critical tactics**. You don't need to know this as a user, but it's gold for understanding the project. Feel free to read.

### 10.1 Event Ring Buffer Design

```cpp
template<size_t N>
struct knst_event_ring {
    static_assert((N & (N - 1)) == 0, "must be power of two");

    knst_window_event data[N];
    size_t count = 0;
    size_t head  = 0;

    void push(const knst_window_event& ev) noexcept {
        data[head] = ev;
        head = (head + 1) & (N - 1);   // bitmask — instead of modulo
        if (count < N) ++count;
    }

    const knst_window_event& at(size_t i) const noexcept {
        size_t start = (head + N - count) & (N - 1);
        return data[(start + i) & (N - 1)];
    }
};
```

**Why power-of-two?** So we can use `& (N - 1)` instead of `% N`. Modulo is expensive on the CPU (especially ARM); bitmask is a single cycle.

**Why fixed size?** Dynamic growth means allocation → allocation shouldn't happen during events. 256 slots is plenty for 60 fps × ~4 events/frame average. If it overflows, the oldest event is **dropped** (ring buffer wrap-around behavior).

**Coalescing:** In `dispatch_event()` MOTION/RESIZE/EXPOSE are merged:

```cpp
if (ev.type == KNST_WINDOW_EVENT_MOTION ||
    ev.type == KNST_WINDOW_EVENT_RESIZE ||
    ev.type == KNST_WINDOW_EVENT_EXPOSE) {

    const size_t n = m_events.size();
    if (n > 0) {
        auto& newest = const_cast<knst_window_event&>(m_events.at(n - 1));
        if (newest.type == ev.type && newest.is_raw == ev.is_raw) {
            if (ev.is_raw) {
                // Raw delta is SUMMED (for FPS camera)
                const int32_t dx = newest.raw_delta_x + ev.raw_delta_x;
                const int32_t dy = newest.raw_delta_y + ev.raw_delta_y;
                newest = ev;
                newest.raw_delta_x = dx;
                newest.raw_delta_y = dy;
            } else {
                // Normal motion overwritten (last position wins)
                newest = ev;
            }
            return;
        }
    }
}
```

**Why raw sum, normal overwrite?** An FPS camera needs to see **all** motion — deltas between two motions must be summed. But in normal motion only the last position matters; intermediate positions can be lost.

### 10.2 Held Key Table & Auto-Repeat

```cpp
struct knst_held_key {
    int32_t key_code = 0;
    int32_t scancode = 0;
    uint32_t last_key_time = 0;
    uint32_t last_repeat_time = 0;
    bool repeat_initialized = false;
    bool active = false;
    char utf8[8] = {0};     // UTF-8 char (for repeated TEXT_INPUT)
    uint8_t utf8_len = 0;
};

knst_window_event::knst_held_key m_held_keys[KNST_MAX_HELD_KEYS]; // 8 slots
```

**How it works:**

1. `add_held_key()` — add on key down. `last_key_time = now`.
2. `non_block_pool_event()` calls `check_key_repeat()` every time.
3. Once `DELAY` (400ms) has passed, the first repeat event fires.
4. After that, fires every `INTERVAL` (30ms).
5. `remove_held_key()` — remove on key up.

**Why simulate in the library?** Every OS behaves differently:
- X11 does `XkbSetDetectableAutoRepeat(True)` and applies its own repeat.
- Wayland's `keyboard.repeat_info` event gives rate/delay — but not fixed.
- Windows `WM_KEYDOWN` sends auto-repeat as a separate message but behaves oddly with modifiers.
- Android `KeyEvent.getRepeatCount()` gives it, but the API is simple.

**Solution:** The library normalizes them. The user only sees `PRESS` / `RELEASE` / `REPEAT`. Fixed tempo via `KNST_WINDOW_KEY_REPEAT_DELAY/INTERVAL`.

**Why UTF-8 cache?** On Wayland, `xkb_state_key_get_utf8()` is expensive on repeat events — cached at the initial press instead of called repeatedly.

**Modifier filter:** Modifier keys (Shift/Ctrl/Alt) don't **repeat**. The `is_modifier()` check undoes the `KNST_ANDROID_KEY_OFFSET` on Android — so offset codes don't collide with ASCII.

### 10.3 CSD "Probe Pattern"

**Problem:** The `knst_function` class only supports `void()` signatures (to avoid std::function overhead + SSO buffer). But the `set_csd_hit_test()` callback has this signature:

```cpp
knst_window::csd_hit cb(const knst_window& w, int x, int y);
```

**Solution — probe pattern:**

```cpp
// Member variables
knst_function m_csd_hit_invoke; // void() wrapper
int m_csd_probe_x = 0; // Input arg 1
int m_csd_probe_y = 0; // Input arg 2
csd_hit m_csd_probe_result; // Output value

// set_csd_hit_test implementation
template<typename F>
void set_csd_hit_test(F&& cb) noexcept {
    auto* self = this;
    m_csd_hit_invoke = [self, cb = std::forward<F>(cb)]() mutable {
        self->m_csd_probe_result = cb(
            static_cast<const knst_window&>(*self),
            self->m_csd_probe_x,
            self->m_csd_probe_y);
    };
    m_csd_enabled = true;
}

// hit_test_csd implementation
csd_hit hit_test_csd(int x, int y) const noexcept {
    if (!m_csd_enabled || m_csd_hit_invoke.empty()) return csd_hit::normal;
    auto* self = const_cast<knst_window*>(this);
    self->m_csd_probe_x = x;
    self->m_csd_probe_y = y;
    self->m_csd_probe_result = csd_hit::normal;
    self->m_csd_hit_invoke();   // void() runs, writes result to m_csd_probe_result
    return self->m_csd_probe_result;
}
```

**Why const_cast?** `knst_function::operator()` isn't const but `hit_test_csd` is. The probe variables behave like "mutable scratchpad" — the CSD callback is called on one thread (event handler), no race.

**Result:** The user writes a normal lambda, the library wraps it with a `void()` signature, and reads the result from probe variables.

### 10.4 Surface Client Pattern

```cpp
class knst_surface_client {
    virtual void on_surface_release() noexcept = 0;
    virtual bool on_surface_recreate() noexcept = 0;
    virtual void on_window_destroyed() noexcept {}
};
```

**What it does:** OpenGL/Vulkan contexts are tied to the native surface. On Android, when `TERM_WINDOW` arrives the surface becomes invalid — you need to release just the surface, not the context. This pattern allows "surface invalid" / "surface ready" notifications to be sent without depending on `knst_window`.

**How it works:**

1. `gl.Init(&window)` internally calls `window.register_surface_client(this)`.
2. When Android `TERM_WINDOW` arrives → `notify_surface_release()` → each client's `on_surface_release()` → `gl` releases the EGL surface (context stays).
3. `INIT_WINDOW` arrives → `notify_surface_recreate()` → `on_surface_recreate()` → `gl` creates a new EGL surface.
4. On window destroy → `notify_window_destroyed()` → `on_window_destroyed()` → `gl` fully shuts down.

**Critical:** The surface client must be destroyed **before** `knst_window`. `gl.Shutdown()` → `window.destroy()` → `KnstWindowSources::CleanUp()`. Reverse order → dangling pointer.

### 10.5 `present_pre()` / `present_post()` — Renderer-Agnostic Sync

```cpp
void call_redraw_callback() noexcept {
    if (m_in_redraw) return; // Re-entrancy guard
    m_in_redraw = true;
    present_pre(); // Wayland: ack_configure
    if (m_redraw_callback) m_redraw_callback();
    present_post(); // X11: _NET_WM_SYNC_REQUEST counter
    m_in_redraw = false;
}
```

**Why two separate hooks?**

- **`present_pre`** — On Wayland, `xdg_surface.ack_configure` must be called. Before the buffer is attached.
- **`present_post`** — On X11, `_NET_WM_SYNC_REQUEST` counter must be incremented. After the new-size frame is on screen.

These are renderer-agnostic. If you use OpenGL, they're inside `SwapBuffers`; if you use Vulkan, around `vkQueuePresentKHR`. The library supports both.

**`m_in_redraw` guard:** On Windows, `WM_WINDOWPOSCHANGED` sometimes calls `restore()` or `set_maximized()` — these **synchronously** call `call_redraw_callback()`. Without the guard, infinite recursion → stack overflow.

### 10.6 Wayland Frame Callback Gating

```cpp
bool is_frame_ready() const noexcept {
    if (m_has_pending_configure) return true;  // Bypass during resize
    if (m_frame_ready) return true;

    // Timeout: if callback doesn't arrive within 500ms, consider ready
    if (m_frame_request_time != 0) {
        const uint32_t now = KnstWindowSources::get_current_time_ms();
        if (now - m_frame_request_time > 500) return true;
    }
    return false;
}
```

**Why this pattern?**

On Wayland, the presentation flow is asynchronous:

1. `wl_surface_commit()` → tells the compositor "new buffer ready"
2. Compositor sends a `frame` event (when the window becomes visible)
3. `wl_callback.done` arrives → next frame is ready

If you call `eglSwapBuffers()` continuously and the compositor is slow, `eglSwapBuffers()` **blocks**. On a bad compositor (especially **NVIDIA Wayland**) this becomes a freeze → `ECONNRESET`.

**Solution:** Don't **draw** until the compositor is ready. But don't wait forever (buggy WM) — timeout at 500ms, then say "ready".

**Bypass during resize:** During resize the `configure` event arrives — that already means "compositor ready". Waiting for a frame callback would be meaningless.

### 10.7 `wl_output` User-Data Stability

```cpp
// Monitor list holds unique_ptr<knst_monitor>
auto mon = std::make_unique<knst_monitor>();
knst_monitor* raw_ptr = mon.get();
knst_display::m_monitor_list.push_back(std::move(mon));

// Give raw_ptr as listener user-data
wl_output_add_listener(raw_ptr->output, &listener, raw_ptr);
```

**Why unique_ptr?**

If `knst_monitor` were stored directly in the vector (`knst_vector<knst_monitor>`) and the vector resized → `data()` address changes → `raw_ptr` dangles → UAF on the next `geometry` event.

`unique_ptr` heap-allocates → the object address stays stable. Even if the vector grows, `raw_ptr` remains valid.

**Extra protection:** `knst_display::refresh_screens_wayland()` does **not** sort. Sorting would reorder the vector, but since it's `unique_ptr` the object address is stable — sorting still isn't needed, the compositor sends them in order.

### 10.8 Dual-Mode Macro System

```cpp
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
    #define KNST_X11_METHOD(name)      name##_x11
    #define KNST_WAYLAND_METHOD(name)  name##_wayland
    #define KNST_IS_DUAL_BACKEND 1
#else
    #define KNST_X11_METHOD(name)      name
    #define KNST_WAYLAND_METHOD(name)  name
    #define KNST_IS_DUAL_BACKEND 0
#endif
```

**What it does?**

- **Single-mode X11:** `KNST_X11_METHOD(creation)` → `creation`. Appears as `void creation() noexcept;` in the header.
- **Single-mode Wayland:** `KNST_WAYLAND_METHOD(creation)` → `creation`. Same.
- **Dual-mode:** `creation_x11()` and `creation_wayland()` become **two separate functions**. `creation()` selects them at runtime.

```cpp
#if KNST_IS_DUAL_BACKEND
inline void knst_window::creation() noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return creation_x11();
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return creation_wayland();
}
#endif
```

**Advantage:** Two backends in one binary. The user can force it via `KNST_WINDOW_FORCE_X11=1` or `KNST_WINDOW_FORCE_WAYLAND=1`. Otherwise runtime detection.

**Disadvantage:** Binary size ~2x (code for both backends). But far easier than maintaining two binaries.

### 10.9 Android JNI Bridge — Thread-Safe Event Queue

On Android, IME callbacks like `commitText` and `deleteSurrounding` arrive **from the UI thread**. But the render loop runs on `native_app_glue`'s **native thread**.

```cpp
namespace knst_android_jni {
    inline std::mutex s_queue_mutex;
    inline std::vector<knst_window_event> s_queue;
}

// UI thread (Java → JNI):
KNST_JNI_FUNC void JNICALL
Java_com_knst_window_KnstActivity_nativeCommitText(JNIEnv* env, jclass, jstring js) {
    knst_window_event ev;
    ev.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
    // ... js → ev.text_input ...

    std::lock_guard<std::mutex> lk(s_queue_mutex);
    s_queue.push_back(std::move(ev));
}

// Native thread (top of render loop):
inline void drain(knst_window* w) {
    std::vector<knst_window_event> local;
    {
        std::lock_guard<std::mutex> lk(s_queue_mutex);
        if (s_queue.empty()) return;
        local.swap(s_queue);   // swap so dispatch happens outside the lock
    }
    for (auto& ev : local) {
        w->dispatch_event(ev);
    }
}
```

**Tactic:** **`swap`** — only a pointer swap under the lock, dispatch **outside**. This lets the UI thread push events even during drain. Lock contention is minimal.

**`inline` + `__attribute__((used))`:** JNI functions are called via reflection, so the linker can eliminate them as dead code. `__attribute__((used))` prevents this.

### 10.10 UTF-16 Preservation

Android JNI has two APIs:

- **`GetStringUTFChars`** — returns **Modified UTF-8** (CESU-8). An emoji (`U+1F600` = surrogate pair) arrives as **two separate 3-byte sequences** → standard UTF-8 decoding breaks.
- **`GetStringChars`** — returns **UTF-16**. Correct.

```cpp
// CORRECT: UTF-16
const jchar* ch = env->GetStringChars(js, nullptr);
knst_window_event ev;
for (jsize i = 0; i < n; ++i) {
    ev.text_input += static_cast<char16_t>(ch[i]);
}
env->ReleaseStringChars(js, ch);

// INCORRECT: CESU-8 (emoji break)
const char* u8 = env->GetStringUTFChars(js, nullptr);
```

Similarly for clipboard read:

```cpp
// NewStringUTF expects modified UTF-8 — emoji aborts under CheckJNI
jstring jText = env->NewString(
    reinterpret_cast<const jchar*>(text.data()),
    (jsize)text.length());
```

### 10.11 Fractional Delta Accumulators

```cpp
// Wayland: wl_fixed_t = 24.8 fixed point (256 subdivisions)
// X11: xcb_input_fp1616_t = 16.16 fixed point
// Windows: int delta

// Truncating fractional raw motion loses slow movement
double m_raw_accum_x = 0.0;

// Each event:
m_raw_accum_x += wl_fixed_to_double(dx);
int32_t ix = (int32_t)m_raw_accum_x;
if (ix == 0) return;   // not enough movement yet
m_raw_accum_x -= (double)ix;

// Now use ix (integer delta)
```

**Why?** `wl_fixed_to_int(0.4) = 0`. Slow mouse movement (precision aim in FPS) would continually lose half-pixels. The accumulator **sums** them; emits when integer.

**Same pattern applies to scroll:**

```cpp
// Windows WM_MOUSEWHEEL can send 40 unit deltas on high-precision wheels
// 120 units = 1 notch (WHEEL_DELTA)
int m_wheel_accum = 0;

m_wheel_accum += raw_delta;
int notches = m_wheel_accum / 120;
if (notches == 0) return;   // continue accumulating
m_wheel_accum -= notches * 120;
```

### 10.12 DPI Caching (Per-Window)

```cpp
mutable float m_cached_dpi     = 0.0f;
mutable int   m_cached_title_h = 0;

int get_title_bar_height() const noexcept {
    // ...
    const knst_monitor* mon = knst_display::get_primary_monitor();
    if (m_cached_dpi == mon->dpi_scale && m_cached_title_h > 0) {
        return m_cached_title_h;   // Cache hit — skip expensive calc
    }
    // ... calculate ...
}
```

**Why `mutable`?** `get_title_bar_height()` is `const` but wants to update the cache. `mutable` gives permission.

**Why per-window?** In multi-monitor setups each window may be on a different monitor. A global cache would return the wrong value. But in practice DPI changes are rare — cache hit rate is high.

### 10.13 Backend Detection Algorithm

```cpp
knst_backend KnstWindowSources::detect_backend() noexcept {
    // 1. Env override
    if (getenv("KNST_WINDOW_FORCE_X11"))       return knst_backend::x11;
    if (getenv("KNST_WINDOW_FORCE_WAYLAND"))   return knst_backend::wayland;

    // 2. Wayland probe
    if (WAYLAND_DISPLAY || XDG_SESSION_TYPE=wayland) {
        wl_display* probe = wl_display_connect(nullptr);
        if (probe) {
            // check wl_compositor + xdg_wm_base
            // if missing: disconnect + fallback
        }
    }

    // 3. X11 probe
    if (DISPLAY set) {
        Display* d = XOpenDisplay(nullptr);
        if (d) { XCloseDisplay(d); return knst_backend::x11; }
    }

    // 4. Last chance: X11 default connect
    Display* d = XOpenDisplay(nullptr);
    if (d) { XCloseDisplay(d); return knst_backend::x11; }

    return knst_backend::none;
}
```

**Tactic:** If the Wayland probe fails (compositor isn't running), it falls back to XWayland. This provides compatibility with older apps in a Wayland session.

### 10.14 Platform Parity — No-Op Approach

```cpp
// No primary selection on Windows
inline void knst_window::set_primary_selection(const knst_c16string&) noexcept {
    // no-op
}

// No CSD hit-test on Android
inline void knst_window::csd_handle_hit(csd_hit, int, int, uint32_t) noexcept {
    // no-op
}
```

**Why?** We don't want `#ifdef` in user code:

```cpp
// BAD:
#if defined(KNST_USING_LINUX_PLATFORM_X11)
    window.set_primary_selection(u"Merhaba");
#endif

// GOOD:
window.set_primary_selection(u"Merhaba");   // no-op on Windows, no crash
```

**`capabilities()`** is more accurate for runtime checks:

```cpp
auto caps = knst_window::capabilities();
if (caps.primary_selection) {
    window.set_primary_selection(u"Merhaba");
}
```

### 10.15 Cleanup Order — Critical

**Wayland proxy hierarchy:**

```
wl_display
  └── wl_registry
        ├── wl_compositor
        │     └── wl_surface (per-window)
        │           └── xdg_surface (per-window)
        │                 └── xdg_toplevel (per-window)
        ├── wl_seat
        │     ├── wl_pointer
        │     └── wl_keyboard
        ├── wl_shm
        ├── wl_output (per-monitor)
        └── ... (other managers)
```

**Rule:** Child first, parent last. If you destroy `wl_compositor` before `wl_surface` → parent link dangles → **segfault**.

```cpp
// Correct order:
1. Per-window (destroy() each window in s_windows list)
2. Singletons bound from registry (cursor_theme, wl_pointer, etc.)
3. Registry-bound managers (compositor, shm, seat)
4. wl_registry
5. wl_display
```

**Android looper pump:** `CleanUp()` pumps the looper until `destroyRequested` arrives. Otherwise the UI thread blocks inside `onPause`'s `pthread_cond_wait` → ANR.

### 10.16 Windows OLE Refcount

On Windows `OleInitialize` is refcounted per process. Every `OleInitialize` must be paired with one `OleUninitialize`.

```cpp
namespace knst_ole_detail {
    inline int& ole_refcount() noexcept {
        static int s = 0;
        return s;
    }
    inline void add_ref() noexcept {
        if (ole_refcount()++ == 0) OleInitialize(nullptr);
    }
    inline void release_ref() noexcept {
        if (--ole_refcount() == 0) OleUninitialize();
    }
}
```

**Why?** If each window called `OleInitialize` when enabling drag-drop → many windows → excessive calls. Refcount solves this.

### 10.17 X11 Cursor Cache

`XCreateFontCursor` and `XcursorLibraryLoadCursor` are **expensive** (they read theme files). Instead of creating a new handle on every `set_cursor()` call, cache:

```cpp
namespace knst_x11_cursor_cache {
    struct entry { Display* dpy; unsigned int shape; char name[64]; Cursor cur; };
    inline knst_vector<entry>& store() { static knst_vector<entry> s; return s; }

    inline Cursor get_font(Display* dpy, unsigned int shape) noexcept {
        for (auto& e : store()) {
            if (e.dpy == dpy && e.shape == shape && e.name[0] == '\0') return e.cur;
        }
        // miss → create + cache
    }
    // get_named similar
}
```

**Critical:** `clear_all()` must be called **before `XCloseDisplay`**. Otherwise cached Cursor handles dangle → UB.

### 10.18 Wayland Clipboard Self-Read Deadlock

**Scenario:** You own the clipboard (called `set_clipboard`). If you call `request_clipboard()`, in the Wayland protocol this **triggers your own `wl_data_source.send()`**. On the same thread, `write()` → pipe → own `read()` → **deadlock**.

**Solution:**

```cpp
void request_clipboard() noexcept {
    if (!m_clipboard_owned.empty()) {
        // We own it → return our own copy, don't go through protocol
        clipboard_text = m_clipboard_owned;
        m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
        dispatch_current_event();
        return;
    }
    // Normal path
}
```

Same pattern applies to primary selection.

### 10.19 Wayland `wl_data_offer_finish` Rule

In the `DataDeviceDrop` handler, if **`accept(serial, nullptr)`** was called (drag rejected), calling `finish` is a **protocol error** (`invalid_finish`). The compositor disconnects you.

```cpp
if (!has_uri_list) {
    // finish is FORBIDDEN if accept(serial, nullptr) was called
    wl_data_offer_destroy(local_offer);
    return;
}
// finish is OK only if uri-list was accepted
wl_data_offer_finish(local_offer);
```

### 10.20 Wayland Event Dispatch — `prepare_read` Pattern

```cpp
// At the top of each frame:
if (wl_display_prepare_read(d) != 0) {
    // Another thread is reading — dispatch pending
    wl_display_dispatch_pending(d);
    return;
}
// ...
if (wl_display_flush(d) == -1 && errno != EAGAIN) {
    wl_display_cancel_read(d);
    return;
}
// poll
if (ret > 0 && (pfd.revents & POLLIN)) {
    wl_display_read_events(d);
    wl_display_dispatch_pending(d);
} else {
    wl_display_cancel_read(d);
}
```

**Critical:** The `POLLHUP` / `POLLERR` check must be **outside** the `POLLIN` check. If inside, that code is unreachable — disconnects never detected.

### 10.21 X11 Scroll Delta Sign

In Wayland's `axis` event, **positive = down**; X11, **positive = up**; Windows `WHEEL_DELTA` **positive = up**. The library normalizes all to `+1 = up`.

**Tactic:** On Wayland we use `-value / 10.0`. 10.0 because Wayland's `~10.0 = 1 notch`.

### 10.22 X11 NumLock Special Case

With NumLock on, X11 keypad keys send symbols like `XK_KP_End` instead of `XK_KP_1`. The library converts them to `XK_KP_1`.

```cpp
if (num_lock) {
    switch (keysym) {
        case XK_KP_End:    keysym = XK_KP_1; break;
        case XK_KP_Down:   keysym = XK_KP_2; break;
        case XK_KP_Next:   keysym = XK_KP_3; break;
        // ... others
    }
}
```

**Extra:** Shift+CapsLock together make letters **lowercase** (`XOR` logic). For digits/punctuation Shift works directly (CapsLock has no effect).

```cpp
bool is_letter = (base_sym >= XK_a && base_sym <= XK_z) ||
                 (base_sym >= XK_A && base_sym <= XK_Z);
bool shifted = is_letter ? (shift_down != caps_lock) : shift_down;
```

### 10.23 Windows ShowCursor Counter

`ShowCursor()` works on a **counter**, not a boolean. Every `ShowCursor(TRUE)` +1, every `ShowCursor(FALSE)` -1. When the counter is negative, the cursor is hidden.

**Problem:** Not idempotent. Two `ShowCursor(FALSE)` → counter -2 → requires two `ShowCursor(TRUE)`.

**Solution:**

```cpp
bool m_cursor_visible = true;   // Real state

if (!m_cursor_visible) {
    while (ShowCursor(TRUE) < 0) {}   // increment until 0
    m_cursor_visible = true;
}
```

### 10.24 Android `drain()` No-Op Pattern

Nothing Android-specific on PC. Functions like `drain()` become **no-op shims**:

```cpp
#if !defined(KNST_USING_PLATFORM_ANDROID)
namespace knst_android_jni {
    inline void drain(knst_window*) noexcept {}
    inline bool input_connection_active() noexcept { return false; }
}
#endif
```

The user can call without `#ifdef`:

```cpp
void on_frame(knst_window& w, void*) {
    knst_android_jni::drain(&w);   // Android: dispatch IME events; PC: no-op
    // ...
}
```

### 10.25 Android `KeyEvent.getUnicodeChar` via JNI

Android `AKeyEvent` doesn't give a character directly — only keycode. For characters, `android.view.KeyEvent.getUnicodeChar()` must be called. The library does this via JNI.

```cpp
// Create Java KeyEvent from JNI
jclass cls = env->FindClass("android/view/KeyEvent");
jmethodID ctor = env->GetMethodID(cls, "<init>", "(II)V");
jmethodID getU = env->GetMethodID(cls, "getUnicodeChar", "(I)I");
jobject ev = env->NewObject(cls, ctor, (jint)0, (jint)keyCode);
int32_t result = env->CallIntMethod(ev, getU, (jint)metaState);
```

This correctly resolves **Shift+letter, punctuation, Turkish characters, dead keys**.

**Tactic:** If `InputConnection` is active (soft keyboard open), `getUnicodeChar` is **skipped** — `commitText` will already send the character. Otherwise duplicate characters.

### 10.26 Android `AKEYCODE` — ASCII Collision

`AKEYCODE_ENTER = 66 = 'B'`, `AKEYCODE_DEL = 67 = 'C'`, `AKEYCODE_ESCAPE = 111 = 'o'`... dozens of collisions.

**Solution:**
- `A-Z` → ASCII (`'A'` - `'Z'`)
- `0-9` → ASCII (`'0'` - `'9'`)
- **Everything else** → `AKEYCODE_X + 0x10000`

```cpp
if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
    keyCode = 'A' + (keyCode - AKEYCODE_A);
} else if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
    keyCode = '0' + (keyCode - AKEYCODE_0);
} else {
    keyCode = keyCode + KNST_ANDROID_KEY_OFFSET;  // 0x10000
}
```

`KNST_WINDOW_KEY_CODE_*` macros give the correct value on every platform.

### 10.27 Multi-Touch Snapshot Pattern

**Wayland:** `wl_touch` events come piecemeal (`down`, `motion`, `up`), then `frame`. In a single event all currently active points are packaged as a **snapshot**.

```cpp
// Update state on down/motion/up
window->m_active_touches[slot] = {...};

// When frame arrives, package all state into one event
ev.begin(KNST_WINDOW_EVENT_TOUCH);
int idx = 0;
for (int i = 0; i < KNST_MAX_TOUCH_POINTS; i++) {
    if (window->m_active_touches[i].id >= 0) {
        ev.pointer_id[idx] = ...;
        ev.pointer_x[idx]  = ...;
        ev.pointer_y[idx]  = ...;
        ++idx;
    }
}
ev.pointer_count = idx;
```

**X11:** `XI_TouchBegin/Update/End` events arrive one at a time but the library takes a **snapshot** — every event reports all active points. Pinch/zoom works because the app sees both points in the same event.

**Android:** `AMotionEvent` **already** carries all pointers. But on `ACTION_POINTER_UP` the **lifting finger is still in the snapshot** → ghost finger. The library finds it via `ACTION_POINTER_INDEX_MASK` and skips it.

### 10.28 UTF-16 String COW (Copy-On-Write)

`knst_c16string` uses copy-on-write. `s1 = s2` doesn't copy data, only bumps a refcount. On **write**, a copy is created.

**Advantage:** Copying `text_input` in events is cheap.

**Disadvantage:** Thread safety. `KNST_C16_STRING_USING_ATOMIC_COW` enables atomic refcount but slows down.

**Default:** Non-atomic COW. Fast single-threaded; **be careful** with multiple threads.

### 10.29 X11 `_NET_WM_SYNC_REQUEST` Flow

```
1. When the window is created, set "WM_PROTOCOLS = [WM_DELETE_WINDOW, _NET_WM_SYNC_REQUEST]" for the WM
2. Set an xcb_sync_counter as the _NET_WM_SYNC_REQUEST_COUNTER property for the WM
3. User resizes the window:
   a. WM sends us _NET_WM_SYNC_REQUEST on our window (ClientMessage)
   b. We draw the new-size frame
   c. After drawing, we call xcb_sync_set_counter(counter, pending_value)
   d. WM shows the new size only after the counter ack
```

**Result:** Flicker-free resize. Without the counter ack the WM shows the old frame.

**Tactic:** The counter is set in `present_post()`. Either via `m_syncConfigured` or a 100ms timeout. 100ms because the WM can't wait forever.

### 10.30 Wayland Fractional Scale & Viewporter

`wp_fractional_scale_v1` **alone isn't enough**. `wp_viewporter` is required alongside. Reason: fractional scale only tells you the compositor's preferred scale. Pixel-level application requires the viewport.

```cpp
// 1. Get fractional_scale_v1 from the fractional_scale manager
m_fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(manager, m_surface);
wp_fractional_scale_v1_add_listener(m_fractional_scale, &listener, this);

// 2. Get viewport from viewporter
m_viewport = wp_viewporter_get_viewport(viewporter, m_surface);

// 3. When the compositor sends preferred_scale (120 = 1.0, 180 = 1.5):
void FractionalScalePreferred(...) {
    m_fractional_scale_120 = scale;
    // Viewport destination = logical size
    wp_viewport_set_destination(m_viewport, logical_w, logical_h);
}

// 4. Resize EGL window to PHYSICAL size:
wl_egl_window_resize(egl_win,
    wayland_physical_width(logical_w),   // = (logical_w * 180 + 60) / 120
    wayland_physical_height(logical_h),
    0, 0);
```

**Rounding:** `(logical * scale + 60) / 120` — 60 is half of 120 for round-half-up. The protocol recommends this.

### 10.31 Android `native_app_glue` Deadlock

`native_app_glue` UI thread lifecycle synchronization:

```
UI thread:  android_app_set_activity_state(...)
                ↓
              pthread_cond_wait     ← BLOCKED
                ↓
Native:     ALooper_pollOnce → APP_CMD_PAUSE
                ↓
            android_app_set_activity_state done
                ↓
            pthread_cond_signal   ← UI wakes up
```

**Problem:** If you return early from `android_main`, the native thread stops polling the looper → the UI thread waits **forever** inside `onPause` → **ANR** → phone freezes.

**Solution — inside `CleanUp()`:**

```cpp
if (m_app != nullptr) {
    if (m_app->destroyRequested == 0 && m_app->activity != nullptr) {
        ANativeActivity_finish(m_app->activity);   // Async, just posts a message
    }

    // Pump the looper until destroyRequested arrives
    const uint32_t t0 = get_current_time_ms();
    while (m_app->destroyRequested == 0) {
        int events;
        struct android_poll_source* source = nullptr;
        const int r = ALooper_pollOnce(100, nullptr, &events, (void**)&source);
        if (r >= 0 && source != nullptr) {
            source->process(m_app, source);
        }
        if (get_current_time_ms() - t0 > 10000) break;  // 10s timeout
    }
}
```

**10s timeout** — for slow devices + transition animations. Previously 3s was too short.

### 10.32 `m_destroyed` Double-Destroy Guard

If the user calls `window.destroy()` explicitly, then `~knst_window()` calls `destroy()` again. If `notify_window_destroyed()` is called twice, surface clients receive `on_window_destroyed()` twice.

**Solution:**

```cpp
inline void knst_window::destroy() noexcept {
    if (m_destroyed) return;
    m_destroyed = true;
    // ... actual destroy ...
}
```

**Critical:** `creation()` resets `m_destroyed = false` — for reuse.

---

## 11. Configuration (`knst_settings.hpp`)

The library can be customized via **user macros** in `include/knst_settings.hpp` before compilation.

### 11.1 Platform Selection

```cpp
// Usually set automatically by CMake
// #define KNST_USING_PLATFORM_WINDOWS
// #define KNST_USING_PLATFORM_LINUX
// #define KNST_USING_PLATFORM_ANDROID
// #define KNST_USING_LINUX_PLATFORM_X11
// #define KNST_USING_LINUX_PLATFORM_WAYLAND
// #define KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND
```

### 11.2 GPU Backends

```cpp
// #define KNST_WINDOW_USE_OPENGL
// #define KNST_WINDOW_USE_VULKAN
```

### 11.3 String

```cpp
// #define KNST_C16STRING_DEACTIVE_COW       // COW off (safer, slower)
// #define KNST_C16_STRING_USING_ATOMIC_COW  // Atomic COW (thread-safe)
// #define KNST_C16STRING_ALIGN_64           // SSO 30 char, alignas(64)
// #define KNST_C16STRING_ALIGN_32           // SSO 14 char, alignas(32)
```

### 11.4 Function

```cpp
// #define KNST_FUNCTION_INLINE_SIZE 64      // SSO buffer size (bytes)
```

### 11.5 Memory Pool

```cpp
// #define KNST_MEMORY_POOL_USE_MUTEX        // Pool thread-safe (mutex)
// #define KNST_SMALL_SIZE_CLASS             // Inline instead of force-inline
```

### 11.6 Thread Pool

```cpp
// #define KNST_OVERFLOW_MAX 32              // Max threads if queue overflows
// #define KNST_QUEUE_MAX 1024               // Queue capacity
// #define KNST_DEFAULT_WORKER_COUNT 0       // 0 = number of CPU cores
// #define KNST_WORKER_BATCH_SIZE 8          // Jobs per worker pull
```

### 11.7 Image Loader

```cpp
// #define KNST_IMAGE_MAX_DIMENSION 16384    // Max width/height
// #define KNST_IMAGE_MAX_CUSTOM_DECODERS 16 // Max custom decoders
```

### 11.8 Window

```cpp
// #define KNST_WINDOW_KEY_REPEAT_DELAY 200      // Initial repeat (ms)
// #define KNST_WINDOW_KEY_REPEAT_INTERVAL 20    // Repeat interval (ms)
// #define KNST_DISABLE_TITLE_BAR                // Custom title bar
// #define KNST_MAX_HELD_KEYS 8                  // Max held keys
```

**`KNST_WINDOW_KEY_REPEAT_DELAY/INTERVAL`:**

- Default `400/30` → desktop feel
- Fast `250/16` → FPS, action games
- Slow `500/50` → text editor

### 11.9 Touch

```cpp
// #define KNST_WINDOW_ENABLE_TOUCH_SCREEN
// #define KNST_MAX_TOUCH_POINTS 10
```

**`KNST_WINDOW_ENABLE_TOUCH_SCREEN`:** Compiles touch code.

- Windows: `WM_TOUCH` handler + `RegisterTouchWindow`
- X11: XInput2 touch event selection
- Wayland: `m_touch_supported = true`
- **Android:** Always on (macro has no effect)

**If undefined:** On Windows/X11 touch code is **not compiled**. Binary shrinks. On Wayland the listener is registered but `m_touch_enabled = false`.

---

## 12. Patterns and Examples

### 12.1 Game Loop

```cpp
struct Game {
    float px = 400, py = 300;
    bool keys[4] = {};

    void handle(knst_window& w) {
        for (size_t i = 0; i < w.event_count(); ++i) {
            const auto& ev = w.get_window_event_handle(i);
            if (ev.type == KNST_WINDOW_EVENT_KEYBOARD) {
                bool pressed = (ev.key_action != KNST_WINDOW_KEY_ACTION_RELEASE);
                switch (ev.key_code) {
                    case KNST_WINDOW_KEY_CODE_W: keys[0] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_A: keys[1] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_S: keys[2] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_D: keys[3] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_ESCAPE: w.should_close(); break;
                }
            }
        }
        w.clear_events();
    }

    void update(float dt) {
        float speed = 300.0f;
        if (keys[0]) py -= speed * dt;
        if (keys[2]) py += speed * dt;
        if (keys[1]) px -= speed * dt;
        if (keys[3]) px += speed * dt;
    }
};
```

### 12.2 Editor (Drag and Drop)

```cpp
struct Editor {
    bool dragging = false;
    int drag_dx = 0, drag_dy = 0;
    int box_x = 100, box_y = 100, box_w = 200, box_h = 100;

    void on_event(const knst_window_event& ev) {
        if (ev.type == KNST_WINDOW_EVENT_MOUSE) {
            if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS &&
                ev.mouse_button == KNST_WINDOW_MOUSE_BUTTON_LEFT) {
                if (ev.mouse_x >= box_x && ev.mouse_x < box_x + box_w &&
                    ev.mouse_y >= box_y && ev.mouse_y < box_y + box_h) {
                    dragging = true;
                    drag_dx = ev.mouse_x - box_x;
                    drag_dy = ev.mouse_y - box_y;
                }
            } else if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_RELEASE) {
                dragging = false;
            }
        } else if (ev.type == KNST_WINDOW_EVENT_MOTION) {
            if (dragging) {
                box_x = ev.mouse_x - drag_dx;
                box_y = ev.mouse_y - drag_dy;
            }
        }
    }
};
```

### 12.3 Multiple Windows

```cpp
knst_window editor(800, 600, u"Editor");
knst_window preview(400, 300, u"Preview");

editor.set_redraw_callback([](knst_window& w, void*) { /* editor */ });
preview.set_redraw_callback([](knst_window& w, void*) { /* preview */ });

editor.creation_and_show();
preview.creation_and_show();

while (!editor.is_should_close() || !preview.is_should_close()) {
    knst_window_event_system::non_block_pool_event();

    if (!editor.is_should_close())  editor.call_redraw_callback();
    if (!preview.is_should_close()) preview.call_redraw_callback();
}
```

### 12.4 Shortcut Handling

```cpp
if (ev.type == KNST_WINDOW_EVENT_KEYBOARD &&
    ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS) {

    const bool ctrl  = (ev.mods & KNST_WINDOW_MOD_CONTROL) != 0;
    const bool shift = (ev.mods & KNST_WINDOW_MOD_SHIFT)   != 0;

    if (ctrl && !shift && ev.key_code == KNST_WINDOW_KEY_CODE_S) save();
    else if (ctrl && shift && ev.key_code == KNST_WINDOW_KEY_CODE_S) save_as();
    else if (ctrl && ev.key_code == KNST_WINDOW_KEY_CODE_Z) undo();
    else if (ctrl && ev.key_code == KNST_WINDOW_KEY_CODE_Y) redo();
}
```

### 12.5 Custom Title Bar with CSD

```cpp
constexpr int TITLE_H = 40;
constexpr int BTN_W   = 46;

window.set_title_bar_height(TITLE_H);

window.set_csd_hit_test([](const knst_window& w, int x, int y) {
    using CH = knst_window::csd_hit;
    const int W = w.get_width();
    const int H = w.get_height();

    constexpr int B = 12;
    if (y < B && x < B) return CH::resize_top_left;
    if (y < B && x >= W-B) return CH::resize_top_right;
    if (y >= H-B && x < B) return CH::resize_bottom_left;
    if (y >= H-B && x >= W-B) return CH::resize_bottom_right;
    if (y < B) return CH::resize_top;
    if (y >= H-B) return CH::resize_bottom;
    if (x < B) return CH::resize_left;
    if (x >= W-B) return CH::resize_right;

    if (y < TITLE_H) {
        if (x >= W - BTN_W*3) return CH::normal;
        return CH::draggable;
    }

    return CH::normal;
});
```

### 12.6 Wayland Frame Gating + Vulkan

```cpp
while (!window.is_should_close()) {
    knst_window_event_system::non_block_pool_event();
    window.call_redraw_callback();

    if (!window.is_frame_ready()) continue;
    if (!vk.HasSurface())        continue;
    if (window.is_gl_context_lost()) continue;

    window.request_frame_callback();
    // ... vkAcquireNextImageKHR, draw, present ...
}
```

### 12.7 Waiting for Clipboard (Wayland)

```cpp
enum class ClipState { Idle, Requesting };
ClipState clip_state = ClipState::Idle;

void on_frame(knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);

        if (ev.type == KNST_WINDOW_EVENT_KEYBOARD &&
            ev.key_code == KNST_WINDOW_KEY_CODE_V &&
            ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS &&
            (ev.mods & KNST_WINDOW_MOD_CONTROL)) {

            w.request_clipboard();
            clip_state = ClipState::Requesting;
        }
        else if (ev.type == KNST_WINDOW_EVENT_CLIPBOARD_DATA) {
            if (clip_state == ClipState::Requesting) {
                clip_state = ClipState::Idle;
                std::wcout << L"From clipboard: " << w.get_clipboard().data() << L"\n";
            }
        }
    }
    w.clear_events();
}
```

### 12.8 High DPI / Scale

```cpp
window.set_redraw_callback([](knst_window& w, void*) {
    int logical_w = w.get_width();
    int logical_h = w.get_height();
    int fb_w = logical_w, fb_h = logical_h;

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    fb_w = w.wayland_physical_width(logical_w);
    fb_h = w.wayland_physical_height(logical_h);
#endif

    glViewport(0, 0, fb_w, fb_h);
    draw_ui(logical_w, logical_h);
});
```

### 12.9 FPS Camera (Raw Mouse)

```cpp
window.set_raw_mouse_mode(true);
window.set_cursor_mode(KNST_WINDOW_CURSOR_DISABLED);

float yaw = 0, pitch = 0;
constexpr float SENSITIVITY = 0.1f;

window.set_redraw_callback([&](knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);
        if (ev.type == KNST_WINDOW_EVENT_MOTION && ev.is_raw) {
            yaw   += ev.raw_delta_x * SENSITIVITY;
            pitch -= ev.raw_delta_y * SENSITIVITY;
            if (pitch >  89.0f) pitch =  89.0f;
            if (pitch < -89.0f) pitch = -89.0f;
        }
    }
    w.clear_events();
});
```

### 12.10 Touch Screen Multi-Finger

```cpp
window.enable_touch_screen(true);

struct TouchDot {
    int   id;
    float x, y;
    float hue_r, hue_g, hue_b;
};
TouchDot touches[KNST_MAX_TOUCH_POINTS];
int touch_count = 0;

window.set_redraw_callback([&](knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);
        if (ev.type == KNST_WINDOW_EVENT_TOUCH) {
            touch_count = ev.pointer_count;
            for (int j = 0; j < touch_count; ++j) {
                touches[j].id = ev.pointer_id[j];
                touches[j].x  = ev.pointer_x[j];
                touches[j].y  = ev.pointer_y[j];
                // Color: hash the id
            }
        }
    }
    w.clear_events();
});
```

### 12.11 Android IME Bridge

```cpp
void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);

    knst_window window;
    window.set_redraw_callback([](knst_window& w, void*) {
        knst_android_jni::drain(&w);   // Dispatch IME events

        for (size_t i = 0; i < w.event_count(); ++i) {
            const auto& ev = w.get_window_event_handle(i);

            if (ev.type == KNST_WINDOW_EVENT_TEXT_INPUT && ev.text_input.length() > 0) {
                // Typed text — emoji, Turkish, CJK all correct
                g_text += ev.text_input;
            }
            else if (ev.type == KNST_WINDOW_EVENT_TEXT_PREEDIT) {
                // IME preedit (not yet committed)
                g_preedit = ev.text_preedit;
            }
        }
        w.clear_events();
    });
    window.creation_and_show();

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
    }

    window.destroy();
    KnstWindowSources::CleanUp();
}
```

---

## 13. FAQ and Traps

### 13.1 Window Opens But Is Empty

**Wayland:** You didn't draw. Every frame needs `wl_surface_attach` + `commit`.

**Android:** You drew while `app->window == nullptr`. Wait for the `INIT_WINDOW` event first.

**Other:** `set_redraw_callback` might not be called.

### 13.2 Same Event Being Handled Repeatedly

`clear_events()` isn't called. **Mandatory** at the end of every frame.

### 13.3 Keyboard Repeats Not Firing

You listen to `PRESS` but not `REPEAT`:

```cpp
if (ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS ||
    ev.key_action == KNST_WINDOW_KEY_ACTION_REPEAT) { /* ... */ }
```

### 13.4 Clipboard Empty (Wayland)

Asynchronous. `request_clipboard()` is ready in the next frame. Wait for the `CLIPBOARD_DATA` event.

### 13.5 `move()` Doesn't Work

**Wayland:** Compositor forbids — expected.
**X11/Windows:** WM restrictions (GNOME won't move without user interaction).

### 13.6 `set_opacity()` Android/Wayland

Android: Ignored.
Wayland: no `wp_alpha_modifier_v1` → only the opaque region changes, no real transparency.

### 13.7 `KNST_WINDOW_EVENT_MOUSE` vs `KEY_CODE`

Union! Read the correct member based on `type`:

```cpp
if (ev.type == KNST_WINDOW_EVENT_MOUSE) {
    // ev.mouse_button, ev.mouse_action ✓
    // ev.key_code ✗ (same memory)
}
```

### 13.8 IME Double Character (Android)

The library fixes this via the `s_input_connection_active` flag. But if you write your own `commitText`, you double it.

### 13.9 `block_pool_event()` on Wayland

Don't use it. The compositor waits for a frame — if you block, the window freezes. **Always `non_block_pool_event()`.**

### 13.10 `g_ui_scale = 1.0` on Android

The `APP_CONFIG_CHANGED` event arrives before things start. `creation()` emits it manually — read the `event.density` field.

### 13.11 Dragging Doesn't Work in CSD

The `set_csd_hit_test` callback might not return `draggable`. Check:

```cpp
if (y < TITLE_H) return knst_window::csd_hit::draggable;  // ✓
```

### 13.12 Multi-Touch Only 1 Point

You're using `ev.mouse_x` instead of `pointer_count`. For touch:

```cpp
if (ev.type == KNST_WINDOW_EVENT_TOUCH) {
    for (int i = 0; i < ev.pointer_count; ++i) {
        draw_circle(ev.pointer_x[i], ev.pointer_y[i]);
    }
}
```

### 13.13 `is_should_close()` Auto-True on Android

Expected behavior. When the user closes from Recents, `app->destroyRequested != 0`. Even if you don't call `should_close()`, it returns `true`.

### 13.14 `static` Window

```cpp
static knst_window w;  // ✗ WRONG — the destructor may run after the platform CleanUp already did
```

Use stack or `unique_ptr` instead of global/static.

### 13.15 Keyboard Modifier Missing in `ev.mods`

On Wayland the `modifiers` event can arrive before keydown (race). The library handles it, but if `ev.mods` is empty, rely on runtime queries like `is_caps_lock_on()`.

### 13.16 X11 Keyboard Repeat

The library calls `XkbSetDetectableAutoRepeat(True)` and applies its own repeat. Handle the `key_action == REPEAT` scenario.

### 13.17 Android `AConfiguration` NULL

`Init()` accesses `app->config` immediately. In very rare cases it can be `NULL` — the library has a guard.

### 13.18 Wayland HiDPI Wrong Size

`wl_egl_window_resize` takes **physical** size. Use `wayland_physical_width(logical)`:

```cpp
wl_egl_window_resize(eglWin,
    window.wayland_physical_width(logical_w),
    window.wayland_physical_height(logical_h),
    0, 0);
```

### 13.19 `drop_files` Disappears After the Event

`begin()` calls `drop_files.reset()` on every event. Only `FILE_DROP` populates it — copy it:

```cpp
if (ev.type == KNST_WINDOW_EVENT_FILE_DROP) {
    auto files = ev.drop_files;   // copy shared_ptr
    // Handle later
}
```

### 13.20 `ev.text_input` Is UTF-16

`std::cout << ev.text_input` works (operator<< exists, converts to UTF-8). But `printf("%s")` doesn't.

### 13.21 FAQ — General

**Q: Does `set_clipboard()` work on Android?**
Yes, writes to `ClipboardManager` via JNI. `get_clipboard()` also works.

**Q: Does `set_opacity` work on Wayland?**
No. No `wp_alpha_modifier_v1`. Only the opaque region changes. Use premultiplied alpha in your own fragment shader.

**Q: Why doesn't `window.resize()` take effect immediately?**
Wayland/X11 are asynchronous. The old size returns until `CONFIGURE_NOTIFY` arrives.

**Q: Is the callback called after `should_close()`?**
Yes, that frame's callback finishes. The loop exits on the next iteration.

**Q: What is `event_count()` after `clear_events()`?**
0. But if new events arrive in the same frame, `event_count()` grows.

**Q: Which backend is active in dual mode?**
`KnstWindowSources::active_backend()` → `x11` / `wayland` / `windows` / `none`.

**Q: What if I return from `android_main`?**
**Don't.** Wait until `CleanUp()` finishes. Otherwise native_app_glue deadlocks → ANR.

**Q: I'm hooking up my own renderer (Vulkan), not using `knst_window_opengl_content`. Is that OK?**
Yes. `knst_window` only manages events + window. You hook up the renderer.

**Q: Why can't the window be moved/copied?**
Platform resources, listener pointers, callback lambdas all tied to `this`. Moving would cause UAF. Use stack or `unique_ptr`.

**Q: When should `capabilities()` be called?**
Any time after `Init()`. The platform is fixed; calling it multiple times is harmless (static).

**Q: What if I call `knst_opengl_global::Init` twice?**
The second call sees `s_initialized` → returns `true`. Idempotent.

**Q: Can I reuse the window after destroy?** (creation after destroy)
Yes, `creation()` resets `m_destroyed = false`. But platform resources are recreated — state may be lost.

**Q: Why is `capabilities().fractional_scale` false on X11?**
X11 has no native fractional scale in the protocol. It can be approximated via `Xft.dpi` but that's not true fractional scale.

**Q: Why doesn't `move()` work on Wayland?**
Protocol rule. Without permission from the compositor, the window can't be moved. `xdg_toplevel_move` requires user interaction (drag).

**Q: Why doesn't the window icon appear on X11?**
Some WMs ignore `_NET_WM_ICON`. GNOME/KDE generally work. Sway/Hyprland use their own icon system.

> **Note:** This document was written at the end of the beta period. All platforms (Windows, Linux X11, Wayland, Dual, Android) passed leak tests (ASan, UBSan, Valgrind) and were tested on real hardware.

> **Questions / Contributions / Suggestions:** [Issues](https://github.com/YusufGundar/KernelNucleusT/issues)

> Hope this helps. Good luck :)