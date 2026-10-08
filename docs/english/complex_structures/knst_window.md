# knst_window — Documentation

> Cross-platform window library.
> Windows · Linux (X11 + Wayland) · Android

---

## Table of Contents

1. [Overview](#overview)
2. [Installation and First Window](#installation-and-first-window)
3. [Classes and Structures](#classes-and-structures)
   - [knst_window](#knst_window)
   - [knst_window_event](#knst_window_event)
   - [knst_monitor](#knst_monitor)
   - [knst_display](#knst_display)
   - [KnstWindowSources](#knstwwindowsources)
   - [knst_window_event_system](#knst_window_event_system)
   - [knst_mobile_keyboard](#knst_mobile_keyboard-android)
4. [Constructors](#constructors)
5. [Window Functions](#window-functions)
6. [Event Functions](#event-functions)
7. [Cursor Functions](#cursor-functions)
8. [Clipboard Functions](#clipboard-functions)
9. [Window Properties](#window-properties)
10. [Monitor Functions](#monitor-functions)
11. [Macro Reference](#macro-reference)
    - [Event Types](#1-event-types-evtype)
    - [Key Actions](#2-key-actions-evkey_action)
    - [Mouse Actions](#3-mouse-actions-evmouse_action)
    - [Mouse Buttons](#4-mouse-buttons-evmouse_button)
    - [Modifier Flags](#5-modifier-flags-evmods)
    - [Key Codes](#6-key-codes-evkey_code)
    - [Cursor Types](#7-cursor-types)
    - [Window Attribute Macros](#8-window-attribute-macros)
    - [Android Specific](#9-android-specific-macros)
12. [Platform Notes](#platform-notes)
    - [Windows](#windows)
    - [Linux X11](#linux-x11)
    - [Linux Wayland](#linux-wayland)
    - [Android](#android)
13. [Frequently Asked Questions](#frequently-asked-questions)
14. [Tips and Patterns](#tips-and-patterns)

---

## Overview

`knst_window` lets you open a window on 4 platforms with a single `#include "KernelNucleusT.hpp"`. The library is **header-only** — there is no binary to link against.

### Philosophy

- **Events are yours, drawing is yours.** The library collects events and manages the window; it is not responsible for drawing.
- **`noexcept` + `bool` return.** It does not throw exceptions; error handling is in your hands.
- **Zero-cost abstraction.** Written with modern C++ (`if constexpr`, `force_inline`, ring buffer). The goal is maximum performance and flexibility.

### Library Skeleton

```
KnstWindowSources::Init()        ← Prepare platform resources
        ↓
window.creation_and_show()       ← Create and show the window
        ↓
    ┌─────────────────────┐
    │  while (!closing)   │       ← Main loop
    │    poll_events()    │
    │    redraw_callback()│
    └─────────────────────┘
        ↓
window.destroy()
KnstWindowSources::CleanUp()     ← Free resources
```

---

## Installation and First Window

### Build

Since it is header-only, you can simply add the `include/` folder to your project — however, due to the Wayland protocol files, it cannot be considered truly full header-only. Use the build commands from `README.md` to compile:

```cmake
add_subdirectory(KernelNucleusT)
target_link_libraries(my_application PRIVATE KernelNucleusT::KernelNucleusT)
```

CMake automatically defines the platform macros (`KNST_USING_PLATFORM_WINDOWS`, `KNST_USING_LINUX_PLATFORM_X11`, etc.) and links the required libraries (GDI, xcb, wayland-client, etc.).

### Simplest Example

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

KNST_FORCE_INLINE static void on_frame(knst_window& w, void*) {
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
    knst_window window(800, 600, "Hello World");
    window.set_redraw_callback(on_frame);
    window.creation_and_show();

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
    }

    window.destroy();
    KnstWindowSources::CleanUp();
}

// Info note: the call_redraw_callback pattern was designed to fix the "rendering stops during resize" issue on the Windows side. It is not mandatory, but if you want full cross-platform consistency, follow the example as shown — it produces the same result on every platform.
```

---

## Classes and Structures

### `knst_window`

The main window class. Each instance is **non-copyable** but **movable** (move-only).

**Responsibilities:**
- Create, show, and destroy the window
- Manage the event queue
- Invoke the render callback
- Manage clipboard, cursor, and window properties

### `knst_window_event`

Represents a single event. All event fields live inside this struct.

```cpp
struct knst_window_event {
    uint32_t type;              // KNST_WINDOW_EVENT_*
    uint32_t timestamp_ms;      // Time in ms

    int32_t  mods;              // Modifier bits (shift/ctrl/...)
    int32_t  mouse_x;           // Mouse X (window-local)
    int32_t  mouse_y;           // Mouse Y (window-local)
    int32_t  mouse_root_x;      // Mouse X (screen)
    int32_t  mouse_root_y;      // Mouse Y (screen)
    int32_t  window_width;      // Window width
    int32_t  window_height;     // Window height
    int32_t  window_root_x;     // Window position X (screen)
    int32_t  window_root_y;     // Window position Y (screen)

    bool is_focused;
    bool mouse_on_window;
    bool is_full_screen;
    bool is_maximized;
    bool is_minimized;

    // Union: mouse OR keyboard fields
    union {
        struct { int32_t mouse_button; int32_t mouse_action; int32_t mouse_scroll_delta; };
        struct { int32_t key_code; int32_t scancode; int32_t key_action; };
    };

    // File drag-and-drop
    std::shared_ptr<knst_vector<knst_c16string>> drop_files;
    uint32_t drop_count;

    #if defined(KNST_USING_PLATFORM_ANDROID)
        // Android-specific fields
        int pointer_count;
        float pointer_x[10];      // Up to 10 fingers
        float pointer_y[10];
        int pointer_id[10];
        int touch_action;
        int content_left, content_top, content_right, content_bottom;
        int orientation;
        char language[4];
        char country[4];
        bool is_night_mode;
        bool is_low_memory;
        float density;
        int screen_width_dp, screen_height_dp;
        void* saved_state;
        size_t saved_state_size;
    #endif
};
```

### `knst_monitor`

Represents a single monitor.

```cpp
class knst_monitor {
public:
    int root_x = 0;              // Screen position X
    int root_y = 0;              // Screen position Y
    int width = 0;               // Pixel width
    int height = 0;              // Pixel height
    int physical_width = 0;      // Physical width in mm
    int physical_height = 0;     // Physical height in mm
    float refresh_rate = 60.0f;  // Hz
    float dpi_scale = 96.0f;     // DPI
    bool is_primary = false;     // Is it the primary monitor?
    knst_c16string name;         // Monitor name
};
```

### `knst_display`

Static class that manages all monitors.

```cpp
class knst_display {
public:
    // Rescan the monitor list
    static void refresh_screens() noexcept;

    // Get all monitors
    static const knst_vector<knst_monitor>& get_monitor_list() noexcept;

    // Get the primary monitor
    static const knst_monitor* get_primary_monitor() noexcept;
};
```

**Notes:**
- `refresh_screens()` is expensive (makes system calls). Call it once at window creation.
- It does not detect monitor changes automatically; call it periodically if you need to.

### `KnstWindowSources`

Manages platform resources. **Do not use directly** — only `Init()` / `CleanUp()` / `get_current_time_ms()`.

```cpp
class KnstWindowSources {
public:
    // ─── Mandatory ───────────────────────────────────────
    static void Init() noexcept;             // Windows/Linux
    static void Init(android_app* app);      // Android
    static void CleanUp() noexcept;

    static uint32_t get_current_time_ms() noexcept;

    // ─── Platform Handle Access ──────────────────────────
    #if KNST_USING_PLATFORM_WINDOWS
        static HINSTANCE& get_windows_native_instance_handle() noexcept;
    #elif KNST_USING_LINUX_PLATFORM_X11
        static xcb_window_t& get_native_x11_root_handle() noexcept;
        static xcb_connection_t* get_native_x11_connection_handle() noexcept;
        static Display* get_native_x11_display() noexcept;
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        static wl_display* wayland_display;  // public
    #elif defined(KNST_USING_PLATFORM_ANDROID)
        static android_app* get_android_app() noexcept;
    #endif

    // Wayland-specific resources (public)
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        static wl_registry*    registery;       // Registry object
        static wl_compositor*  compositor;      // Surface creator
        static xdg_wm_base*    wmBase;          // Window manager
        static wl_shm*         shm;             // Shared memory
        static wl_seat*        seat;            // Input devices
        static wl_pointer*     pointer;         // Mouse
        static wl_keyboard*    keyboard;        // Keyboard
        static wl_cursor_theme* cursor_theme;   // Cursor theme
        static wl_surface*     cursor_surface;  // Cursor surface
        // ...
    #endif
};
```

### `knst_window_event_system`

Manages the event queue globally.

```cpp
struct knst_window_event_system {
    // Wait for and process events (blocking)
    static void block_pool_event() noexcept;

    // Process events instantly (non-blocking, use in games)
    static void non_block_pool_event() noexcept;

    // Number of registered windows
    static size_t get_window_count() noexcept;
};
```

**`block_pool_event()` vs `non_block_pool_event()`:**

| Function | Behavior | Use case |
|----------|----------|----------|
| `block_pool_event()` | Waits until an event arrives | Simple CLI tools |
| `non_block_pool_event()` | Returns immediately | Game loops, animations |

On Linux (Wayland), drawing is mandatory by default. Since the compositor usually does not provide a title bar, events arrive in every situation.

### `knst_mobile_keyboard` (Android)

Controls the Android soft keyboard.

```cpp
class knst_mobile_keyboard {
public:
    static bool show();        // Open the keyboard → true on success
    static bool hide();        // Close the keyboard
    static void toggle();      // Toggle open/close
    static bool is_visible();  // Is it visible?
};
```

**Notes:**
- `show()` has double-tap protection (300ms). If called twice within a short window, the second call is ignored.
- It contains JNI calls, so it is **not compiled** on other platforms.

---

## Constructors

### `knst_window` (Desktop)

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

**Parameters:**

| Parameter | Description |
|-----------|-------------|
| `width`, `height` | Window size (pixels) |
| `title` | Window title (UTF-16) |
| `root_x`, `root_y` | Position relative to the monitor. If `KNST_WINDOW_DEFAULT` is passed, the window is centered on the monitor |
| `monitor` | Which monitor to open on. Default: empty (primary) |

**Examples:**

```cpp
// Simple window
knst_window w1(800, 600, u"Hello");

// At a specific position
knst_window w2(800, 600, u"Window", 100, 200);

// On a secondary monitor
knst_display::refresh_screens();
auto monitors = knst_display::get_monitor_list();
if (monitors.size() > 1) {
    knst_window w3(800, 600, u"Secondary", KNST_WINDOW_DEFAULT, KNST_WINDOW_DEFAULT, monitors[1]);
}
```

### `knst_window` (Android)

```cpp
knst_window(
    int width = KNST_WINDOW_DEFAULT,
    int height = KNST_WINDOW_DEFAULT,
    knst_c16string title = u"Knst_Window",
    int root_x = KNST_WINDOW_DEFAULT,
    int root_y = KNST_WINDOW_DEFAULT,
    const knst_monitor& monitor = knst_monitor()
);
```

**Note:** On Android, the size and position parameters are **ignored** — the Android window already opens at full screen size.

### Copy / Move

```cpp
knst_window(const knst_window&) = delete;             // Cannot be copied
knst_window& operator=(const knst_window&) = delete;

knst_window(knst_window&&) noexcept;                  // Movable
knst_window& operator=(knst_window&&) noexcept;
```

**Why move-only?** The window holds platform resources (HWND, xcb_window_t, wl_surface). Copying would lead to double-free of these resources. Move safely transfers them and updates callbacks.

---

## Window Functions

### Lifecycle

#### `void creation() noexcept`

Creates the window but does not show it on screen. Allocates the platform resource and registers with the event system.

```cpp
knst_window w;
w.creation();     // Window exists, not on screen
// ... adjustments ...
w.show();         // Now visible
```

#### `void show() noexcept`

Shows the window on screen.

#### `void creation_and_show() noexcept`

Shorthand for `creation()` + `show()`. Most commonly used in examples.

#### `void destroy() noexcept`

Destroys the window and frees its resources. The destructor (`~knst_window`) already calls it, but you may want to call it explicitly.

#### `void should_close() noexcept`

Sends a close signal. The main loop sees `is_should_close()` → `true` on the next iteration and exits.

#### `bool is_should_close() const noexcept`

Has a close signal arrived? Use in the main loop condition:

```cpp
while (!window.is_should_close()) { /* ... */ }
```

#### `bool is_disconnected() const noexcept`

Has the connection dropped? (Becomes `true` when the Wayland compositor shuts down.) Behavior varies by compositor.

### Title

```cpp
void set_title(const knst_c16string& title) noexcept;
const knst_c16string& get_title() const noexcept;
```

Uses UTF-16 strings. On Windows it is native UTF-16; on Linux/Android it is converted to UTF-8.

### Position and Size

```cpp
void move(int root_x, int root_y) noexcept;
void move(int root_x, int root_y, const knst_monitor& monitor) noexcept;
void resize(int width, int height) noexcept;
void set_minimum_size(int width, int height) noexcept;
void set_maximum_size(int width, int height) noexcept;
```

**Example:**

```cpp
window.move(100, 100);                         // To screen point (100,100)
window.resize(1280, 720);                      // Resize to 1280x720
window.set_minimum_size(400, 300);             // Minimum size limit
window.set_maximum_size(1920, 1080);           // Maximum size limit
window.set_maximum_size(KNST_WINDOW_DEFAULT);  // Remove the limit
```

### Window State

```cpp
void toggle_fullscreen(bool fullscreen) noexcept;
void set_minimized() noexcept;
void set_maximized() noexcept;
void restore() noexcept;
void hide() noexcept;
void focus() noexcept;
void set_opacity(float opacity) noexcept;
float get_opacity() const noexcept;
```

**Example:**

```cpp
window.set_minimized();          // Minimize to taskbar
window.set_maximized();          // Maximize
window.toggle_fullscreen(true);  // True fullscreen (like F11)
window.restore();                // Restore to normal size
window.set_opacity(0.5f);        // Semi-transparent
```

**Notes:**
- On **Android**, most of these are **ignored** — Android manages these window controls itself.
- On **Wayland**, `move()` and `focus()` do not work (compositor security restrictions).

### Window Attributes

```cpp
void set_attribute(int attribute, bool value) noexcept;
bool get_attribute(int attribute) const noexcept;
```

**Available attributes:**

| Attribute | Description |
|-----------|-------------|
| `KNST_WINDOW_ATTR_DECORATED` | Show system frame? |
| `KNST_WINDOW_ATTR_RESIZABLE` | Can the user resize it? |
| `KNST_WINDOW_ATTR_ALWAYS_ON_TOP` | Stay above other windows? |
| `KNST_WINDOW_ATTR_TRANSPARENT` | Let mouse clicks pass through? |

**Example:**

```cpp
// Borderless, fixed-size, always on top
window.set_attribute(KNST_WINDOW_ATTR_DECORATED, false);
window.set_attribute(KNST_WINDOW_ATTR_RESIZABLE, false);
window.set_attribute(KNST_WINDOW_ATTR_ALWAYS_ON_TOP, true);

// Overlay that lets clicks pass through
window.set_attribute(KNST_WINDOW_ATTR_TRANSPARENT, true);
```

**Platform notes:**
- **Windows:** applied via `WS_POPUP` vs `WS_OVERLAPPEDWINDOW`, `WS_EX_TOPMOST`, `WS_EX_TRANSPARENT`.
- **X11:** `_MOTIF_WM_HINTS`, `_NET_WM_STATE_ABOVE`, `XFixesSetWindowShapeRegion`.
- **Wayland:** `xdg-decoration`, `xdg_toplevel_set_min/max_size`, `wl_surface_set_input_region`.

### Title Bar (Custom Drawing)

```cpp
void set_title_bar_height(int height) noexcept;
int get_title_bar_height() const noexcept;
```

By default the library **automatically computes** the title bar height based on DPI (96 DPI base, 34 pixels).

To override manually:

```cpp
window.set_title_bar_height(48);  // Fixed 48 pixels
```

---

## Event Functions

### Queue Management

```cpp
size_t event_count() const noexcept;
const knst_window_event& get_window_event_handle(size_t i) const noexcept;
void clear_events() noexcept;
void dispatch_event(const knst_window_event& ev) noexcept;
void dispatch_current_event() noexcept;
```

**Most common usage:**

```cpp
for (size_t i = 0; i < w.event_count(); ++i) {
    const auto& ev = w.get_window_event_handle(i);
    // ... do something with ev
}
w.clear_events();  // ← THIS IS VERY IMPORTANT — prevents leftover events from previous frames
```

`dispatch_event()` and `dispatch_current_event()` are advanced — for injecting your own events. Not needed except for tests/simulation.

### Key State Querying

```cpp
bool is_key_held(int key_code) const noexcept;
bool is_caps_lock_on() const noexcept;
bool is_num_lock_on() const noexcept;
```

Queries the current key state without waiting for an event. For text editors / games:

```cpp
if (window.is_key_held(KNST_WINDOW_KEY_CODE_W)) {
    player.y += speed * dt;  // W held → move forward
}
```

### Internal Key Tracking (Advanced)

```cpp
knst_window_event::knst_held_key* find_held_by_scancode(int sc) noexcept;
knst_window_event::knst_held_key* add_held_key(int kc, int sc, uint32_t now) noexcept;
void remove_held_key(int sc) noexcept;
void clear_held_keys() noexcept;
```

These are typically called by the **event manager**. You do not usually need to call them directly.

### User Data

```cpp
void set_user_data(void* data) noexcept;
const void* get_user_data() const noexcept;
```

For accessing your state inside the callback:

```cpp
struct AppState { int counter; };
AppState state;

window.set_user_data(&state);

// Inside on_frame:
static void on_frame(knst_window& w, void* user_data) {
    AppState* s = static_cast<AppState*>(user_data);
    s->counter++;
}
```

**Note:** `set_redraw_callback` already passes `m_user_data` to the callback. Alternatively, you can use a capturing lambda:

```cpp
window.set_redraw_callback([&state](knst_window& w, void*) {
    state.counter++;
});
```

### Redraw Callback

```cpp
template<typename Callback>
void set_redraw_callback(Callback&& callback) noexcept;

void call_redraw_callback() noexcept;
```

Callback signature: `void(knst_window&, void* user_data)`

**Example:**

```cpp
window.set_redraw_callback([](knst_window& w, void* data) {
    // Draw this frame
    for (size_t i = 0; i < w.event_count(); ++i) {
        // Process events
    }
    w.clear_events();
});

// In the main loop:
while (!window.is_should_close()) {
    knst_window_event_system::non_block_pool_event();
    window.call_redraw_callback();
}
```

---

## Cursor Functions

### System Cursors

```cpp
void set_cursor(uint16_t cursor_type) noexcept;
```

**Available types:**

| Macro | Appearance |
|-------|------------|
| `KNST_WINDOW_CURSOR_ARROW` | Standard arrow |
| `KNST_WINDOW_CURSOR_IBEAM` | Text selection |
| `KNST_WINDOW_CURSOR_CROSSHAIR` | Cross |
| `KNST_WINDOW_CURSOR_HAND` | Hand (over links) |
| `KNST_WINDOW_CURSOR_HRESIZE` | Horizontal resize |
| `KNST_WINDOW_CURSOR_VRESIZE` | Vertical resize |
| `KNST_WINDOW_CURSOR_MOVE` | Move |
| `KNST_WINDOW_CURSOR_WAIT` | Wait (clock) |
| `KNST_WINDOW_CURSOR_HELP` | Help |
| `KNST_WINDOW_CURSOR_NOT_ALLOWED` | Forbidden |

**Example:**

```cpp
window.set_cursor(KNST_WINDOW_CURSOR_HAND);  // Hand on hover over a button
```

### Custom Cursor

```cpp
void set_bmp_cursor(
    const knst_byte_string& data,
    int width,
    int height,
    int hot_x = -1,
    int hot_y = -1
) noexcept;
```

**Input format:** RGBA8888 — each pixel is 4 bytes, ordered `R, G, B, A`.

**Parameters:**
- `data`: Raw RGBA bytes
- `width`, `height`: Dimensions
- `hot_x`, `hot_y`: Click point (`-1` → auto center)

**Example:**

```cpp
knst_byte_string cursor_data;

// Prepare an RGBA8888 array
// cursor_data = ...  (32*32*4 = 4096 bytes)

window.set_bmp_cursor(cursor_data, 32, 32, 0, 0);   // Hotspot top-left
window.set_bmp_cursor(cursor_data, 32, 32, 16, 16); // Hotspot center
```

**Note:** The library converts RGBA to native format on each platform:
- **Windows:** `CreateIconIndirect` — BGRA + AND mask
- **X11:** `XcursorImageLoadCursor` — ARGB
- **Wayland:** `wl_shm` buffer — ARGB8888

### Reset

```cpp
void reset_cursor() noexcept;
```

Returns to the default arrow cursor.

### Cursor Mode

```cpp
void set_cursor_mode(int mode) noexcept;
```

| Mode | Effect |
|------|--------|
| `KNST_WINDOW_CURSOR_ARROW` | Normal |
| `KNST_WINDOW_CURSOR_HIDDEN` | Invisible (still moves) |
| `KNST_WINDOW_CURSOR_DISABLED` | Invisible + locked (held at center) |

**Example — FPS look control:**

```cpp
window.set_cursor_mode(KNST_WINDOW_CURSOR_DISABLED);
// Now mouse movements are relative; the cursor never leaves the screen area
```

**Platform notes:**
- **Windows:** `ShowCursor(FALSE)` + `ClipCursor(rect)`
- **X11:** `xcb_grab_pointer` + `warp_pointer`
- **Wayland:** `zwp_locked_pointer_v1` + `zwp_confined_pointer_v1`

### Cursor Position

```cpp
void set_cursor_pos_on_window(int x, int y) noexcept;
void set_cursor_pos_global(int root_x, int root_y) noexcept;
```

**Note:** On Wayland it **does not work** due to compositor security. It works on X11 and Windows.

---

## Clipboard Functions

### Write to Clipboard

```cpp
void set_clipboard(const knst_c16string& text) noexcept;
```

**Example:**

```cpp
window.set_clipboard(u"Hello world!");
```

### Read from Clipboard

```cpp
void request_clipboard() noexcept;
const knst_c16string& get_clipboard() const noexcept;
void clear_clipboard() noexcept;
```

**Usage:**

```cpp
window.request_clipboard();
// On Wayland it is asynchronous — content arrives on the next frame
// On X11/Windows it is synchronous

// Next frame:
const auto& text = window.get_clipboard();
std::wcout << text.data() << L"\n";
```

**Platform notes:**
- **Windows:** `OpenClipboard`, `SetClipboardData`, `CF_UNICODETEXT`
- **X11:** `xcb_set_selection_owner`, `XCB_SELECTION_NOTIFY`
- **Wayland:** `wl_data_source` + `wl_data_offer`
- **Android:** via JNI, `ClipboardManager`

---

## Window Properties

### `void set_drag_drop_status(bool enabled) noexcept`

Enables/disables file drag-and-drop.

```cpp
window.set_drag_drop_status(true);   // Enable
window.set_drag_drop_status(false);  // Disable
```

**Note:** Ignored on Android.

### `void apply_bmp_icon(const knst_byte_string& data, int width, int height) noexcept`

Sets the window icon. `data` is again RGBA8888.

```cpp
knst_byte_string icon = load_icon();
window.apply_bmp_icon(icon, 64, 64);
```

**Platform notes:**
- **Windows:** `CreateIcon` + `WM_SETICON`
- **X11:** `_NET_WM_ICON` property
- **Wayland:** Not supported (depends on compositor)
- **Android:** Ignored

---

## Monitor Functions

### Scan Monitors

```cpp
knst_display::refresh_screens();
```

Lists all monitors on the system. This is an **expensive operation** — call it once before opening a window.

### Monitor List

```cpp
const auto& monitors = knst_display::get_monitor_list();
for (const auto& mon : monitors) {
    std::cout << "Name:      " << mon.name << "\n";
    std::cout << "Resolution:" << mon.width << "x" << mon.height << "\n";
    std::cout << "Position:  (" << mon.root_x << ", " << mon.root_y << ")\n";
    std::cout << "DPI:       " << mon.dpi_scale << "\n";
    std::cout << "Refresh:   " << mon.refresh_rate << " Hz\n";
    std::cout << "Physical:  " << mon.physical_width << "x" << mon.physical_height << " mm\n";
    std::cout << "Primary:   " << (mon.is_primary ? "yes" : "no") << "\n";
}
```

### Primary Monitor

```cpp
const knst_monitor* primary = knst_display::get_primary_monitor();
if (primary) {
    std::cout << "Primary: " << primary->width << "x" << primary->height << "\n";
}
```

**Platform notes:**
- **Windows:** `EnumDisplayDevicesW`, `GetDpiForMonitor`
- **X11:** `xcb_randr_get_monitors`, `xcb_randr_get_crtc_info`
- **Wayland:** `wl_output` protocol
- **Android:** `ANativeWindow_getWidth/Height` + `AConfiguration`

---

## Macro Reference

All macros are defined in `knst_window_identifiers.hpp`.

### 1. Event Types — `ev.type`

The event ranges do not overlap:

| Range | Category |
|-------|----------|
| `0` | `UNKNOWN` — never dispatched |
| `100..199` | Mouse events |
| `200..299` | Keyboard events |
| `300..399` | Window events |
| `400..499` | Focus events |
| `500..599` | File drop |
| `600..699` | Mobile |
| `700..799` | App lifecycle |

#### Mouse (100-199)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_MOUSE` | 100 |
| `KNST_WINDOW_EVENT_MOTION` | 101 |
| `KNST_WINDOW_EVENT_ENTER` | 102 |
| `KNST_WINDOW_EVENT_LEAVE` | 103 |

#### Keyboard (200-299)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_KEYBOARD` | 200 |

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

#### Focus (400-499)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_FOCUS_IN` | 400 |
| `KNST_WINDOW_EVENT_FOCUS_OUT` | 401 |

#### File Drop (500-599)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_FILE_DROP` | 500 |
| `KNST_WINDOW_EVENT_FILE_DROP_ENTER` | 501 |
| `KNST_WINDOW_EVENT_FILE_DROP_MOVE` | 502 |
| `KNST_WINDOW_EVENT_FILE_DROP_LEAVE` | 503 |

#### Mobile (600-699)

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_EVENT_MOBILE_TOUCH` | 600 |
| `KNST_WINDOW_EVENT_MOBILE_BACK` | 601 |
| `KNST_WINDOW_EVENT_MOBILE_HOME` | 602 |
| `KNST_WINDOW_EVENT_MOBILE_MENU` | 603 |
| `KNST_WINDOW_EVENT_MOBILE_SEARCH` | 604 |
| `KNST_WINDOW_EVENT_MOBILE_APP_SWITCH` | 605 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_UP` | 607 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_DOWN` | 608 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_MUTE` | 609 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PLAY_PAUSE` | 623 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_NEXT` | 625 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PREVIOUS` | 626 |
| (more in `identifiers.hpp`) | ... |

#### App Lifecycle (700-799)

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

### 2. Key Actions — `ev.key_action`

| Macro | Value | Meaning |
|-------|-------|---------|
| `KNST_WINDOW_KEY_ACTION_PRESS` | 1 | Key pressed |
| `KNST_WINDOW_KEY_ACTION_RELEASE` | 2 | Key released |
| `KNST_WINDOW_KEY_ACTION_REPEAT` | 3 | Key repeating |

**Keyboard repeat** is configured in `knst_settings.hpp`:

```cpp
#define KNST_WINDOW_KEY_REPEAT_DELAY     100  // Initial repeat delay (ms)
#define KNST_WINDOW_KEY_REPEAT_INTERVAL   23  // Repeat interval (ms)
```

### 3. Mouse Actions — `ev.mouse_action`

| Macro | Value |
|-------|-------|
| `KNST_WINDOW_MOUSE_ACTION_PRESS` | 1 |
| `KNST_WINDOW_MOUSE_ACTION_RELEASE` | 2 |
| `KNST_WINDOW_MOUSE_ACTION_SCROLL` | 3 |

### 4. Mouse Buttons — `ev.mouse_button`

Follows the X11/POSIX standard:

| Macro | Value | Physical |
|-------|-------|----------|
| `KNST_WINDOW_MOUSE_BUTTON_LEFT` | 1 | Left click |
| `KNST_WINDOW_MOUSE_BUTTON_MIDDLE` | 2 | Middle click |
| `KNST_WINDOW_MOUSE_BUTTON_RIGHT` | 3 | Right click |
| `KNST_WINDOW_MOUSE_BUTTON_BACK` | 8 | Side "back" |
| `KNST_WINDOW_MOUSE_BUTTON_FORWARD` | 9 | Side "forward" |

### 5. Modifier Flags — `ev.mods`

Combined with the bitwise `|` operator:

| Macro | Value | Meaning |
|-------|-------|---------|
| `KNST_WINDOW_MOD_SHIFT` | 1 | Shift |
| `KNST_WINDOW_MOD_CONTROL` | 2 | Ctrl |
| `KNST_WINDOW_MOD_ALT` | 4 | Alt |
| `KNST_WINDOW_MOD_SUPER` | 8 | Windows/Cmd key |
| `KNST_WINDOW_MOD_CAPS_LOCK` | 16 | Caps Lock active |
| `KNST_WINDOW_MOD_NUM_LOCK` | 32 | Num Lock active |

**Example:**

```cpp
bool ctrl_shift_s = (ev.mods & KNST_WINDOW_MOD_CONTROL) &&
                    (ev.mods & KNST_WINDOW_MOD_SHIFT) &&
                    ev.key_code == KNST_WINDOW_KEY_CODE_S;
```

### 6. Key Codes — `ev.key_code`

Same name on every platform — native value differs. See `identifiers.hpp` for platform details.

#### Letters

```cpp
KNST_WINDOW_KEY_CODE_A ... KNST_WINDOW_KEY_CODE_Z
```

**Turkish characters:**

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
KNST_WINDOW_KEY_CODE_NUMPAD_0 ... KNST_WINDOW_KEY_CODE_NUMPAD_9
```

#### Function Keys

```cpp
KNST_WINDOW_KEY_CODE_F1 ... KNST_WINDOW_KEY_CODE_F12
```

#### Special Keys

```cpp
KNST_WINDOW_KEY_CODE_ESCAPE
KNST_WINDOW_KEY_CODE_ENTER
KNST_WINDOW_KEY_CODE_SPACE
KNST_WINDOW_KEY_CODE_BACKSPACE
KNST_WINDOW_KEY_CODE_TAB
KNST_WINDOW_KEY_CODE_CAPS_LOCK
KNST_WINDOW_KEY_CODE_NUM_LOCK
KNST_WINDOW_KEY_CODE_SCROLL_LOCK
```

#### Modifier Keys

```cpp
KNST_WINDOW_KEY_CODE_SHIFT
KNST_WINDOW_KEY_CODE_CONTROL
KNST_WINDOW_KEY_CODE_ALT
KNST_WINDOW_KEY_CODE_SUPER
KNST_WINDOW_KEY_CODE_MENU
```

#### Arrow Keys

```cpp
KNST_WINDOW_KEY_CODE_LEFT
KNST_WINDOW_KEY_CODE_RIGHT
KNST_WINDOW_KEY_CODE_UP
KNST_WINDOW_KEY_CODE_DOWN
```

#### Navigation

```cpp
KNST_WINDOW_KEY_CODE_HOME
KNST_WINDOW_KEY_CODE_END
KNST_WINDOW_KEY_CODE_PAGE_UP
KNST_WINDOW_KEY_CODE_PAGE_DOWN
KNST_WINDOW_KEY_CODE_INSERT
KNST_WINDOW_KEY_CODE_DELETE
```

#### Symbols

```cpp
KNST_WINDOW_KEY_CODE_SEMICOLON
KNST_WINDOW_KEY_CODE_SLASH
KNST_WINDOW_KEY_CODE_GRAVE
KNST_WINDOW_KEY_CODE_LEFT_BRACKET
KNST_WINDOW_KEY_CODE_BACKSLASH
KNST_WINDOW_KEY_CODE_RIGHT_BRACKET
KNST_WINDOW_KEY_CODE_APOSTROPHE
KNST_WINDOW_KEY_CODE_PERIOD
KNST_WINDOW_KEY_CODE_COMMA
KNST_WINDOW_KEY_CODE_MINUS
KNST_WINDOW_KEY_CODE_PLUS
KNST_WINDOW_KEY_CODE_EQUAL
```

#### Media Keys

```cpp
KNST_WINDOW_KEY_CODE_VOLUME_UP
KNST_WINDOW_KEY_CODE_VOLUME_DOWN
KNST_WINDOW_KEY_CODE_VOLUME_MUTE
KNST_WINDOW_KEY_CODE_MEDIA_PLAY
KNST_WINDOW_KEY_CODE_MEDIA_STOP
KNST_WINDOW_KEY_CODE_MEDIA_NEXT
KNST_WINDOW_KEY_CODE_MEDIA_PREV
KNST_WINDOW_KEY_CODE_MEDIA_PAUSE
```

#### Browser Keys

```cpp
KNST_WINDOW_KEY_CODE_BROWSER_HOME
KNST_WINDOW_KEY_CODE_BROWSER_BACK
KNST_WINDOW_KEY_CODE_BROWSER_FORWARD
KNST_WINDOW_KEY_CODE_BROWSER_REFRESH
KNST_WINDOW_KEY_CODE_BROWSER_SEARCH
KNST_WINDOW_KEY_CODE_BROWSER_FAVORITES
```

### 7. Cursor Types

Passed as a parameter to `set_cursor()`.

**Note:** Values are platform-specific. Same name, different value.

| Macro | Windows | X11 | Wayland |
|-------|---------|-----|---------|
| `KNST_WINDOW_CURSOR_ARROW` | `OCR_NORMAL` | `XC_left_ptr` | `"left_ptr"` |
| `KNST_WINDOW_CURSOR_IBEAM` | `OCR_IBEAM` | `XC_xterm` | `"xterm"` |
| `KNST_WINDOW_CURSOR_HAND` | `OCR_HAND` | `XC_hand2` | `"hand2"` |
| `KNST_WINDOW_CURSOR_HRESIZE` | `OCR_SIZEWE` | `XC_sb_h_double_arrow` | `"sb_h_double_arrow"` |
| `KNST_WINDOW_CURSOR_VRESIZE` | `OCR_SIZENS` | `XC_sb_v_double_arrow` | `"sb_v_double_arrow"` |
| `KNST_WINDOW_CURSOR_MOVE` | `OCR_SIZEALL` | `XC_fleur` | `"fleur"` |

Additionally for `set_cursor_mode()`:

```cpp
KNST_WINDOW_CURSOR_HIDDEN     // Invisible
KNST_WINDOW_CURSOR_DISABLED   // Locked + invisible
```

### 8. Window Attribute Macros

For `set_attribute()` / `get_attribute()`:

| Macro | Value | Meaning |
|-------|-------|---------|
| `KNST_WINDOW_ATTR_DECORATED` | 1 | System frame |
| `KNST_WINDOW_ATTR_RESIZABLE` | 2 | Resizable |
| `KNST_WINDOW_ATTR_ALWAYS_ON_TOP` | 3 | Always on top |
| `KNST_WINDOW_ATTR_TRANSPARENT` | 4 | Input-transparent |

### 9. Android-Specific Macros

#### Touch Actions

```cpp
KNST_WINDOW_TOUCH_ACTION_PRESS           // 0 — Finger placed
KNST_WINDOW_TOUCH_ACTION_RELEASE         // 1 — Finger lifted
KNST_WINDOW_TOUCH_ACTION_MOVE            // 2 — Dragged
KNST_WINDOW_TOUCH_ACTION_CANCEL          // 3 — Cancelled
KNST_WINDOW_TOUCH_ACTION_OUTSIDE         // 4 — Outside bounds
KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS   // 5 — Additional finger placed
KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE // 6 — Additional finger lifted
```

#### Screen Orientations

```cpp
KNST_WINDOW_ORIENTATION_UNDEFINED   // 0
KNST_WINDOW_ORIENTATION_PORTRAIT    // 1
KNST_WINDOW_ORIENTATION_LANDSCAPE   // 2
KNST_WINDOW_ORIENTATION_SQUARE      // 3
```

#### Other Constants

```cpp
KNST_WINDOW_DEFAULT  // -10000 — Sentinel for "automatic" values
```

---

## Platform Notes

### Windows

#### Native Handle Access

```cpp
HWND hwnd = window.get_windows_window_handle();
```

#### Notes

- **DPI awareness** is automatic. Text/metrics scale correctly on high-DPI displays.
- **Unicode** window titles are natively supported as UTF-16.
- **File drag-and-drop** works two ways: legacy `WM_DROPFILES` and modern `IDropTarget` (COM). Both are active at the same time.
- **Custom frame** (`KNST_DISABLE_TITLE_BAR`) — drag, resize, and close buttons are handled automatically via `WM_NCHITTEST`. You only draw.
- **Transparency** (`set_opacity`) is applied via `WS_EX_LAYERED` + `SetLayeredWindowAttributes`. 0.0 = fully invisible, 1.0 = opaque.

#### Unsupported

- `move()` may be blocked by the user if composition is disabled (`WM_WINDOWPOSCHANGING`).

### Linux X11

#### Native Handle Access

```cpp
xcb_window_t win = window.get_x11_window_handle();
xcb_connection_t* c = KnstWindowSources::get_native_x11_connection_handle();
Display* dpy = KnstWindowSources::get_native_x11_display();
```

#### Notes

- **Key repeat** is managed by the X server, but the library runs its own repeat logic via `XkbSetDetectableAutoRepeat(True)`. Result: same repeat speed on every platform.
- **`_NET_WM_STATE`** protocol fully supported: `_NET_WM_STATE_FULLSCREEN`, `_MAXIMIZED_HORZ`, `_MAXIMIZED_VERT`, `_HIDDEN`, `_ABOVE`.
- **`_NET_WM_SYNC_REQUEST`** frame synchronization — no tearing.
- **Xdnd** protocol fully supported.
- **Clipboard** is synchronous. `request_clipboard()` yields the content on the next frame.
- **Compared to Wayland**, window management is much more flexible: `move()`, `focus()`, `set_position` all work.

#### Unsupported

- When running under Wayland via XWayland, some features (transparency) may be limited.

### Linux Wayland

#### Native Handle Access

```cpp
const wl_surface* surface = window.get_wayland_surface_handle();
wl_display* dpy = KnstWindowSources::wayland_display;
wl_compositor* comp = KnstWindowSources::compositor;
wl_shm* shm = KnstWindowSources::shm;
```

#### Notes

- **⚠️ Drawing is mandatory.** On Wayland, you must attach a buffer every frame to display the window. If you skip it, the window will not show.
- **`move()` and `focus()`** do not work due to compositor security restrictions.
- **`set_cursor_pos_*`** also does not work.
- **Clipboard is asynchronous** — `request_clipboard()` will be ready on the next frame.
- **Cursor locking** via `zwp_pointer_constraints_v1`: `zwp_locked_pointer_v1` (keep at center) and `zwp_confined_pointer_v1` (keep inside window).
- **File drag-and-drop** works through the `wl_data_device` protocol.

#### Software Render Example on Wayland

```cpp
static void on_frame(knst_window& w, void*) {
    // ... process events ...

    struct wl_surface* s = const_cast<struct wl_surface*>(w.get_wayland_surface_handle());
    if (!s || !KnstWindowSources::shm) return;

    // Create buffer
    int W = w.m_knst_event.window_width;
    int H = w.m_knst_event.window_height;
    int stride = W * 4;

    int fd = memfd_create("buf", MFD_CLOEXEC);
    ftruncate(fd, stride * H);
    uint32_t* px = (uint32_t*)mmap(nullptr, stride * H, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    // Fill pixels (BGRA / ARGB8888)
    for (int i = 0; i < W * H; ++i) px[i] = 0xFF202060;  // dark blue

    struct wl_shm_pool* pool = wl_shm_create_pool(KnstWindowSources::shm, fd, stride * H);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);

    wl_surface_attach(s, buf, 0, 0);
    wl_surface_damage_buffer(s, 0, 0, W, H);
    wl_surface_commit(s);
    wl_display_flush(KnstWindowSources::wayland_display);

    // NOTE: the buffer must be destroyed later.
    wl_shm_pool_destroy(pool);
    munmap(px, stride * H);
    close(fd);
}
```

### Android

#### Native Handle Access

```cpp
struct android_app* app = KnstWindowSources::get_android_app();
ANativeWindow* win = app->window;
```

#### Notes

- **`android_main`** is the entry point — **not** the usual `main`.
- **`app->window`** is `NULL` at start. Wait until the `APP_CMD_INIT_WINDOW` event arrives.
- **Soft keyboard** is controlled via `knst_mobile_keyboard`.
- **Touch** supports multiple pointers (`pointer_x[10]`).
- **Lifecycle** events (pause/resume/stop/config-changed) are fully supported.
- **Clipboard** is bound to the system clipboard via JNI.
- **`move()` / `resize()` / `focus()`** are ignored (do not exist on Android).

#### Android `Init` Difference

```cpp
// Windows / Linux
KnstWindowSources::Init();

// Android
void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);
    // ...
}
```

#### Android `app->window` NULL Check

```cpp
while (app->window == nullptr) {
    int events;
    struct android_poll_source* source = nullptr;
    while (ALooper_pollAll(-1, nullptr, &events, (void**)&source) >= 0) {
        if (source) source->process(app, source);
        if (app->destroyRequested != 0) return;
    }
}
```

#### Software Render Example (Android)

```cpp
ANativeWindow_Buffer buf;
if (ANativeWindow_lock(app->window, &buf, nullptr) != 0) return;

uint32_t* px = (uint32_t*)buf.bits;
for (int y = 0; y < buf.height; ++y)
    for (int x = 0; x < buf.width; ++x)
        px[y * buf.stride + x] = 0xFF202060;

ANativeWindow_unlockAndPost(app->window);
```

Ready-made `build-android` scripts are provided for both Linux and Windows to build the APK and run it on the phone. Once the environment is set up, the rest is easy. The Android part is actually similar to the PC part, but it holds an application handle like `struct android_app* app`. After passing it to `KnstWindowSources::Init(app);` you will not need to deal with it again. In the sample I provided, since there is currently no Vulkan backend or similar, I did software rendering (Android-specific). A comprehensive drawing backend will be added later.

---

## Frequently Asked Questions

### 1. Window opens but nothing is visible

- **Wayland:** You did not draw. Every frame needs `wl_surface_attach` + `commit`.
- **Android:** You drew while `app->window == nullptr`. Add a check.
- **Others:** The `on_frame` callback may not be set via `set_redraw_callback`.

### 2. The same event is processed over and over

`clear_events()` was not called. You must call it at the **end** of every frame.

### 3. Key repeats are not arriving

You are looking for `KNST_WINDOW_KEY_ACTION_PRESS`, not `REPEAT`. Catch both:

```cpp
if (ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS ||
    ev.key_action == KNST_WINDOW_KEY_ACTION_REPEAT) {
    // ...
}
```

### 4. Clipboard is empty (Wayland)

It is asynchronous. Call `request_clipboard()` and read `get_clipboard()` on the next frame. Or wait a few frames.

### 5. `move()` does not work

- **Wayland:** The compositor does not allow it. Expected behavior.
- **X11/Windows:** The window manager (WM) rules may block it. For example, GNOME may not allow moving the window without user interaction.

### 6. `set_opacity()` does not work on Android

Window opacity is **ignored** on Android. It is managed by the system.

### 7. I opened two windows — which one receives events?

Each window has its **own event queue**. `non_block_pool_event()` feeds them all at once, and each window receives its own events.

### 8. Difference between `user_data` and callback

- `set_user_data(void*)` — Stores an arbitrary pointer. Passed to the callback as the second parameter.
- `set_redraw_callback(fn)` — The function invoked per frame. It also receives `user_data`.

They are used together:

```cpp
struct MyState { int x; };
MyState s;

window.set_user_data(&s);
window.set_redraw_callback([](knst_window& w, void* data) {
    MyState* st = (MyState*)data;
    st->x++;
});
```

### 9. I want to draw my own title bar

Define the `KNST_DISABLE_TITLE_BAR` macro before compilation:

```cpp
#define KNST_DISABLE_TITLE_BAR
#include "KernelNucleusT.hpp"
```

Then read the height via `get_title_bar_height()` and draw inside `on_frame`.

On **Windows**, dragging/closing works automatically. On **X11**, `_NET_WM_MOVERESIZE` is handled by the library. On **Wayland**, `xdg_toplevel_move/resize` is used.

### 10. How do I hook up my own OpenGL context?

```cpp
#if KNST_USING_PLATFORM_WINDOWS
    HWND hwnd = window.get_windows_window_handle();
    HDC hdc = GetDC(hwnd);
    // wglCreateContext(hdc) ...
#elif KNST_USING_LINUX_PLATFORM_X11
    xcb_window_t win = window.get_x11_window_handle();
    Display* dpy = KnstWindowSources::get_native_x11_display();
    // glXCreateContext(dpy, ...) ...
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    wl_surface* s = const_cast<wl_surface*>(window.get_wayland_surface_handle());
    wl_egl_window* eglWin = wl_egl_window_create(s, W, H);
    // eglCreateWindowSurface(...)
#elif defined(KNST_USING_PLATFORM_ANDROID)
    ANativeWindow* win = KnstWindowSources::get_android_app()->window;
    // eglCreateWindowSurface(..., win, ...)
#endif
```

Take the library's native handle and pass it to your own GL layer.
If you look at older versions of the library, they provided Vulkan and OpenGL content classes — however, for now I am considering dropping support for them, at least for the OpenGL side, and focusing on Vulkan.

---

## Tips and Patterns

### Pattern 1 — Game Loop

```cpp
struct Game {
    float player_x = 400, player_y = 300;
    bool keys[6] = {};  // W, A, S, D, Space, Shift

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
                    case KNST_WINDOW_KEY_CODE_SPACE:  keys[4] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_ESCAPE:
                        w.should_close(); break;
                }
            }
        }
        w.clear_events();
    }

    void update(float dt) {
        const float speed = 300.f;
        if (keys[0]) player_y -= speed * dt;
        if (keys[2]) player_y += speed * dt;
        if (keys[1]) player_x -= speed * dt;
        if (keys[3]) player_x += speed * dt;
    }

    void render() { /* draw */ }
};

int main() {
    KnstWindowSources::Init();

    Game game;
    knst_window window(800, 600, "Game");

    window.set_redraw_callback([&game](knst_window& w, void*) {
        game.handle(w);
        game.update(1.0f / 60.0f);
        game.render();
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

### Pattern 2 — UI Editor

```cpp
// Drag-and-drop with the mouse
struct Editor {
    bool dragging = false;
    int drag_offset_x = 0, drag_offset_y = 0;
    int box_x = 100, box_y = 100, box_w = 200, box_h = 100;

    void on_event(const knst_window_event& ev) {
        if (ev.type == KNST_WINDOW_EVENT_MOUSE) {
            if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS &&
                ev.mouse_button == KNST_WINDOW_MOUSE_BUTTON_LEFT) {
                // Inside the box?
                if (ev.mouse_x >= box_x && ev.mouse_x < box_x + box_w &&
                    ev.mouse_y >= box_y && ev.mouse_y < box_y + box_h) {
                    dragging = true;
                    drag_offset_x = ev.mouse_x - box_x;
                    drag_offset_y = ev.mouse_y - box_y;
                }
            }
            else if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_RELEASE) {
                dragging = false;
            }
        }
        else if (ev.type == KNST_WINDOW_EVENT_MOTION) {
            if (dragging) {
                box_x = ev.mouse_x - drag_offset_x;
                box_y = ev.mouse_y - drag_offset_y;
            }
        }
    }
};
```

### Pattern 3 — Multiple Windows + Different Callbacks

```cpp
knst_window editor(800, 600, "Editor");
knst_window preview(400, 300, "Preview");

editor.set_redraw_callback([](knst_window& w, void*) { /* draw editor */ });
preview.set_redraw_callback([](knst_window& w, void*) { /* draw preview */ });

editor.creation_and_show();
preview.creation_and_show();

while (!editor.is_should_close() || !preview.is_should_close()) {
    knst_window_event_system::non_block_pool_event();

    if (!editor.is_should_close())  editor.call_redraw_callback();
    if (!preview.is_should_close()) preview.call_redraw_callback();
}
```

### Pattern 4 — Shortcut Handling

```cpp
if (ev.type == KNST_WINDOW_EVENT_KEYBOARD &&
    ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS) {

    const bool ctrl  = ev.mods & KNST_WINDOW_MOD_CONTROL;
    const bool shift = ev.mods & KNST_WINDOW_MOD_SHIFT;

    if (ctrl && !shift && ev.key_code == KNST_WINDOW_KEY_CODE_S) {
        save();
    }
    else if (ctrl && shift && ev.key_code == KNST_WINDOW_KEY_CODE_S) {
        save_as();
    }
    else if (ctrl && ev.key_code == KNST_WINDOW_KEY_CODE_Z) {
        undo();
    }
}
```

I tried to write this documentation in a way that everyone can understand, acting as a user manual as much as possible. In some places I provided pattern examples; I hope they are useful. However, if you still have questions or want to get in touch in any way, my contact details are on my GitHub profile.

I hope this library is useful to everyone. Have a good day :)