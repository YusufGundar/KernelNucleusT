# knst_window — User Guide (English)

**Aims to be a comprehensive, fully-supported window management library for Windows, Linux, and Android, offering as many features as possible.**

**Features:**
- **`force inline` is used in critical, frequently-repeated hot paths during runtime**
- **Enables the user to write clean code using modern C++ features**
- **Captures and lets you handle events in a manner similar to the OS's own event model**
- **Security** — Has been tested successfully across a limited but meaningful set of tests

---

**Library Architecture**

To use the library, you must first call `KnstWindowSources::Init()`. `KnstWindowSources` holds the structures that need to remain global throughout the library's lifetime; you initialize them via `Init()`. This call also internally invokes `knst_display::refresh_screens()`, which fetches the currently connected screens and their information — there is no need to call it again elsewhere in your program unless you specifically need to refresh monitor info; the call has already been made, and the `knst_monitor` struct already holds monitor/screen properties.

Once you've initialized the library, you're ready to create windows and process events.

#### Creating a window: ####
```cpp
knst_window window('width', 'height', 'title', 'x position on the primary screen', 'y position on the primary screen', 'monitor');
```

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| width | int | Window width (pixels) | 800 |
| height | int | Window height (pixels) | 600 |
| title | std::u16string | Window title (UTF-16) | u"Triangle Test" |
| x position on the primary screen | int | x position (pixels) | 200 |
| y position on the primary screen | int | y position (pixels) | 200 |
| monitor | knst_monitor | knst_monitor object | knst_display::get_primary_monitor() |

---

⚠️: On Linux (Wayland), the compositor — not us — decides where the window will appear. Even if you supply a position on the primary screen, it **will be ignored**.

#### Next step: 'Creation and Showing' :

- creation()
- show()
- creation_and_show()

The constructor only stores the information you give it — the actual window-creation work happens inside `creation()`. This is the stage at which the window is introduced to the operating system and resources are initialized. Afterward you can call `show()`. `creation_and_show()` internally calls `creation()` first, then `show()`. You can either call `creation()` and then `show()` yourself, or call both in one line via `creation_and_show()`. To avoid flicker issues, I deliberately did **not** put `show()` inside `creation()` — if you have settings/configuration to apply, do so after `creation()` and then call `show()`, and your window will open already reflecting those settings.

- If the window's position on the primary screen doesn't matter to you, you can pass the `KNST_DEFAULT` macro to let the operating system decide. This isn't limited to creation — you can pass `KNST_DEFAULT` for any positional value you don't want to explicitly control.

#### Next step: 'Event Handling and the Event Loop' :

**Event Handling:**
#### knst_window has 3 fundamental event-handling mechanisms: ###

- block_pool_event ---> returns once an event arrives
- non_block_pool_event ---> returns immediately, consuming an event if one is available, or returning right away if not

📌 **NOTE:** These are found as public, static members inside `knst_window_event_system` — you simply call them. In multi-window applications, events are correctly routed to the appropriate window.

- If an event object's `type` is `KNST_UNKNOWN`, the loop failed to determine its type.


# Knst Event System — User Guide

This section explains how the application should use the `knst_window` / `knst_window_event_system` event system. You do **not** need to know the platform-specific implementation details (X11/Win32/Wayland/Android) — all you need to do is use this API; the library handles everything behind the scenes.

---

## 1. Setup and Main Loop Skeleton

```cpp
int main() {
    KnstWindowSources::Init();

    knst_window window(1280, 720, "Title");
    window.creation();

    // ... render backend init (Vulkan/OpenGL) ...

    window.set_user_data(&renderState);
    window.set_redraw_callback(my_render_frame);

    window.show();

    while (true) {
        knst_window_event_system::non_block_pool_event(); // 1) pull native events
        window.call_redraw_callback();                     // 2) render/update

        // 3) read this frame's events (see below)

        if (window.is_should_close()) break;

        window.clear_temporary_events();                   // 4) clear buffers
    }

    // ... destroy ...
    KnstWindowSources::CleanUp();
}
```

These 4 steps should run **in this exact order**, every frame. If you skip step 4, the ring buffers will keep overflowing (old events get overwritten, no data is lost, but it leads to logical confusion) — if you want a genuinely consistent event mechanism, I recommend following this pattern.

---

## 2. Poll Functions: `non_block_pool_event()` vs `block_pool_event()`

| Function | When to use |
|---|---|
| `non_block_pool_event()` | Game/render loop — drains the native queue every frame, returns immediately if there's nothing. |
| `block_pool_event()` | When the window is minimized/backgrounded and you want to avoid burning CPU — waits until at least 1 event arrives. |

Both do the same underlying work: drain the native platform queue, route each event to the correct window, and run `check_key_repeat()` for all windows. This design was chosen specifically to guarantee cross-platform consistency.

---

## 3. Event Categories and How to Read Them

The event system automatically splits incoming events into two storage styles based on type:

### 3.1 Single-slot categories
Only the **most recent value** matters — it gets overwritten. Even if resize fires 50 times in the same frame, only the last one is valid.

```cpp
const auto& resize = window.get_resize_event();
if (resize.type == KNST_WINDOW_RESIZE) {
    // resize.window_width / resize.window_height
}

const auto& focus = window.get_focus_event();
const auto& winState = window.get_window_state_event(); // maximize/minimize/restore/fullscreen
const auto& expose = window.get_expose_event();
const auto& move = window.get_move_event();
const auto& misc = window.get_misc_event();
const auto& lifecycle = window.get_lifecycle_event(); // Android only
```

> ⚠️ The `type` field is reset every frame by `clear_temporary_events()` (`0` = `KNST_UNKNOWN`). So always check `type` before reading — if no event occurred, no stale data lingers; `type` will simply be `0`.

### 3.2 Ring buffer categories
These can arrive multiple times, back-to-back, within the same frame (e.g. W pressed and D released within one frame). They must be read in order, without loss:

```cpp
// Keyboard
for (size_t i = 0; i < window.get_keyboard_event_count(); i++) {
    const auto& ev = window.get_keyboard_event(i);
    // ev.key_code, ev.key_action (PRESS/RELEASE/REPEAT), ev.scancode, ev.mods
}

// Mouse
for (size_t i = 0; i < window.get_mouse_event_count(); i++) {
    const auto& ev = window.get_mouse_event(i);
    // ev.mouse_action, ev.mouse_button, ev.mouse_x/y, ev.mouse_scroll_delta
}

// File drag & drop
for (size_t i = 0; i < window.get_filedrop_event_count(); i++) {
    const auto& ev = window.get_filedrop_event(i);
    // ev.drop_files, ev.drop_count
}

// Touch (Android only)
for (size_t i = 0; i < window.get_touch_event_count(); i++) {
    const auto& ev = window.get_touch_event(i);
    // ev.pointer_x[i], ev.pointer_y[i], ev.touch_action
}
```

Ring buffers are fixed-size (e.g. `KNST_KEYBOARD_EVENT_SLOTS` = 8) and never allocate. If more events arrive in a single frame than there are slots, the oldest event gets overwritten — in practice, hitting this limit for keyboard/mouse input is virtually impossible. You can also change this slot count via macros.

---

## 4. Tracking Key State (Is It Held?) Yourself

**Important:** The event system only tells you "what happened this frame" (`PRESS`/`RELEASE`/`REPEAT`) — it does **not** tell you "is this key currently held down." That internal state (`m_key_held`, etc.) is private.

If you want continuous movement (rotating/walking with WASD, etc.), maintain your own `bool` flags and update them on PRESS/RELEASE:

```cpp
struct RenderState {
    bool keyW = false, keyA = false, keyS = false, keyD = false;
    // ...
};

// inside the main loop:
for (size_t i = 0; i < window.get_keyboard_event_count(); i++) {
    const auto& ev = window.get_keyboard_event(i);
    bool isDown = (ev.key_action == KNST_KEY_PRESS);
    bool isUp   = (ev.key_action == KNST_KEY_RELEASE);

    if (isDown || isUp) {
        if (ev.key_code == KNST_KEY_D) rs.keyD = isDown;
        else if (ev.key_code == KNST_KEY_A) rs.keyA = isDown;
        else if (ev.key_code == KNST_KEY_W) rs.keyW = isDown;
        else if (ev.key_code == KNST_KEY_S) rs.keyS = isDown;
    }
}
```

Don't use the `KNST_KEY_REPEAT` action for this kind of logic — it's unnecessary for continuous movement and only useful where "character repeat" is actually needed, such as text input fields or a console.

---

## 5. Making Continuous Movement Frame-Rate Independent (Delta-Time Based)

Update key state (`keyW`, etc.) in the event loop, but apply the actual movement **in the render callback, based on elapsed time (delta time)** — not on event/repeat frequency:

```cpp
void render_frame(knst_window& window, void* user_data) {
    RenderState* rs = static_cast<RenderState*>(user_data);

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - rs->lastFrameTime).count();
    rs->lastFrameTime = now;
    dt = std::min(dt, 0.05f); // guards against sudden spikes (alt-tab, freezes, etc.)

    const float ROT_SPEED = 2.0f; // radians/second

    if (rs->keyD) rs->rotb += ROT_SPEED * dt;
    if (rs->keyA) rs->rotb -= ROT_SPEED * dt;
    if (rs->keyW) rs->rota += ROT_SPEED * dt;
    if (rs->keyS) rs->rota -= ROT_SPEED * dt;

    mesh.SetRotation(rs->rota, rs->rotb, rs->rotc);
    // ... rest of rendering ...
}
```

This approach behaves at the same real-world speed whether you're at 60fps or 144fps, and is completely independent of OS autorepeat timing (delay/interval).

---

## 6. Key Repeat Settings (For Text Input)

For scenarios where character repetition is actually needed (input boxes, a console), the shared constants in `knst_window_core.hpp` apply:

```cpp
static constexpr uint32_t KEY_REPEAT_DELAY = 150;    // wait before first repeat (ms)
static constexpr uint32_t KEY_REPEAT_INTERVAL = 5;   // interval between subsequent repeats (ms)
```

These settings are controlled from **a single shared location for all platforms** (`check_key_repeat()`) — there's no need for separate per-platform configuration.

> Note: Lowering `INTERVAL` below the frame duration (e.g. ~16ms at 60fps) provides no practical benefit — repeat checking already runs once per frame (inside `non_block_pool_event()`).

---

## 7. End-of-Frame Cleanup

```cpp
window.clear_temporary_events();
```

This call:
- Resets single-slot events (`type = 0`)
- Resets the `count`/`head` values of ring buffers (data isn't erased — the next `push` simply overwrites it)

**If you forget this call:** functions like `get_keyboard_event_count()` will keep returning previous frames' events over and over (since the ring buffer's "counter" never resets) — this causes input to be processed as if it were "pressed twice."

---

## 8. Quick Reference — Typical Usage Template

```cpp
while (true) {
    knst_window_event_system::non_block_pool_event();
    window.call_redraw_callback();

    // Update keyboard state (PRESS/RELEASE)
    for (size_t i = 0; i < window.get_keyboard_event_count(); i++) {
        const auto& ev = window.get_keyboard_event(i);
        // ... rs.keyX = (ev.key_action == KNST_KEY_PRESS) ...
    }

    // Resize check
    const auto& resize = window.get_resize_event();
    if (resize.type == KNST_WINDOW_RESIZE) {
        // update the swapchain
    }

    // File dropping
    for (size_t i = 0; i < window.get_filedrop_event_count(); i++) {
        // ...
    }

    if (window.is_should_close()) break;

    window.clear_temporary_events();
}
```

⚠️: One more warning specifically for Linux (Wayland). Global values inside the window's event object such as `window_root_y`, `window_root_x`, `mouse_root_x`, `mouse_root_y` will **never** be populated. You can look into the details yourself — the Wayland compositor blocks most of this for security reasons.

⚠️: On the Android side, mouse action / maximized states etc. are also defaults — you cannot control them. Additionally, you'll receive touch events instead of mouse events.

### Window Customization: ###

- If you want to use your window without the automatic callback, I recommend defining the `KNST_DISABLE_REDRAW_ON_EVENT_MANAGER` macro via `#define`. However, if you define this macro, you may run into white-screen/non-rendering issues on Windows during resize — for cross-platform consistency, you should assign a callback by default.

- My recommendation is that you **do not** define `KNST_DISABLE_REDRAW_ON_EVENT_MANAGER` — but you're free to define it if you want to take full manual control of rendering. That's your choice. Also, if you're worried about performance overhead, all of these calls are executed via `KNST_FORCE_INLINE`, meaning they're pasted directly with no actual function-call overhead.

___

- If you want to disable the operating system's default title bar for your window, simply define the `KNST_DISABLE_TITLE_BAR` macro via `#define`. Unlike setting `set_attribute(KNST_WINDOW_ATTRIB_DECORATED, false)` on `knst_window`, this macro gives you the same resizable window but with just the title bar removed. Normally, using `set_attribute` this way prevents you from resizing the window (commonly used by game developers), but defining this macro removes only the title bar while keeping resizing intact.

### OpenGL and Vulkan Support: ###

- To use `opengl` or `vulkan` in your window, you first need to create the content object for the graphics API you want and attach it to your window.

#### OpenGL: ####
- Create a content object with `knst_window_opengl_content content`
- `content.Init(knst_window, vsync state)` — pass your window's address with `&` to `knst_window`. The vsync state defaults to `false`, but you can set whatever value you want.

⚠️: If you plan to draw your own title bar, I recommend setting the vsync state to `false` — especially on Wayland.

- I have created special title-bar themes specifically for OpenGL. To fully use them in your window, first define the `KNST_DISABLE_TITLE_BAR` macro via `#define`, then define the macro for whichever theme you want.

```cpp
#define KNST_WINDOW_USING_KNST_TITLE_BAR_WHITE_MODERN
#define KNST_WINDOW_USING_KNST_TITLE_BAR_BLUE_MODERN
#define KNST_WINDOW_USING_KNST_TITLE_BAR_FUTURISTIC
#define KNST_WINDOW_USING_KNST_TITLE_BAR_SUNSET_GLOW
```
- You can define whichever of these macros you like — and if you have a theme suggestion, feel free to reach out; I can add it to this list.

- This title-bar drawing happens inside the OpenGL content's `SwapBuffers` function (not applicable to Android). You can either define a theme macro and use it normally with SwapBuffers, or simply disable the title bar via macro and draw/swap your own title bar at runtime however you like.

```cpp
DrawKnstTitleBarBlueModern()
DrawKnstTitleBarWhiteModern()
DrawKnstTitleBarFuturistic()
DrawKnstTitleBarSunsetGlow()
```
- You can use functions like these.

⚠️: On Linux (Wayland), most distributions do not provide their own title bar, so we're required to draw it ourselves. Of course, some distributions do support SSD (Server-Side Decoration) — e.g. KDE. But since I designed this library to work consistently across every Wayland distribution, by default I draw the `DrawKnstTitleBarWhiteModern()` title bar myself. You can, of course, change this via macro. Also, if you start with the content's `BeginFrame` function, the title bar and content area will be set up correctly.

___

- Here's my recommended usage pattern for the OpenGL content: if you're using it with a callback (as in the `package_tests/knst_window` example), pass the content via user data, then begin drawing your frame by calling the OpenGL content's `BeginFrame()` function. This correctly sets up both the title bar area and the content area in a cross-platform-consistent way, and also ensures every frame is drawn cleanly.

- You must close your OpenGL content at the end with `Shutdown()`.

#### Vulkan: ####

- For Vulkan support, we currently only provide the necessary resources. I would like to add custom title-bar drawing/themes eventually, but for now, in this version of the library, we only provide the underlying resources.

- Create a content object with `knst_window_vulkan_content content`
- `content.Init(knst_window)` — pass your window to bind it to your content.

- You must close your Vulkan content at the end with `Destroy()`.

- This part is meant to be used together with the library's `knst_gui_framework`, though you can also use it standalone if you prefer.

## Android ##

- Since `knst_window` is a cross-platform library, it tries to minimize how much this platform difference is felt by you — however, things get a bit different when it comes to Android.

- To build a basic application, the only difference on the Android side is that your app begins with `void android_main(struct android_app* app)` instead of `int main`, and the only other difference is that `KnstWindowSources::Init()` expects a `struct android_app* app` parameter. All you need to do differently is call `KnstWindowSources::Init(app)`. This maintains cross-platform consistency, but naturally, on Android you'll need to control a lot more — for example: what happens when the app is backgrounded, what happens when the virtual keyboard opens, how the app-closing flow behaves, etc. I'll cover these below.

- To open the virtual keyboard on Android, I designed the `knst_mobile_keyboard` class. Its methods:

```cpp
    static bool hide(); // hides the keyboard
    static void toggle(); // toggles the keyboard's visibility
    static bool is_visible(); // returns whether the keyboard is currently open or closed
```
- You can interact with the mobile keyboard using these methods. You don't need to handle Init/Shutdown yourself — `KnstWindowSources` handles that internally. A detailed example is also available under `package_tests/knst_window/android`.

- I should also note: on Android, when the app is being closed, you don't always get `KNST_CLOSE_WINDOW` / `KNST_DISCONNECT` events — sometimes the OS forcibly kills the app and cleans up resources itself. In this case, all you need to do is save anything you need to save inside the `KNST_SAVE_STATE` event, before the app is closed. The `KNST_SAVE_STATE` event is guaranteed to fire before the app closes.

- Also, the parameters you pass to `creation()` are meaningless on Android — we can't control them there. Some required information should instead be defined in your `AndroidManifest.xml`.

#### Helper Structures ####

### knst_display ---> Stores information about the system's screens

- `refresh_screens()` ---> fetches the currently connected screens' information and stores it in `knst_monitor` objects inside a `knst_vector`
- `KnstWindowSources::Init()` already calls `refresh_screens()` internally

- `get_monitor_list()` ---> returns the current `knst_vector<knst_monitor>` object; you can iterate over the screens inside it with a loop

- `get_primary_monitor()` ---> gives you the `knst_monitor` object of the currently active primary monitor/screen

Example:
```cpp
    for (size_t i = 0; i < knst_display::get_monitor_list().size(); i++) {
        const auto& mon = knst_display::get_monitor_list()[i];
        std::cout << "\n--- Monitor " << (i + 1) << " ---" << std::endl;
        std::cout << "Name: " << mon.name << std::endl;
        std::cout << "Primary: " << (mon.is_primary ? "Yes" : "No") << std::endl;
        std::cout << "Position: (" << mon.root_x << ", " << mon.root_y << ")" << std::endl;
        std::cout << "Resolution: " << mon.width << "x" << mon.height << std::endl;
        std::cout << "Physical size: " << mon.physical_width << "x" << mon.physical_height << " mm" << std::endl;
        std::cout << "Refresh rate: " << mon.refresh_rate << " Hz" << std::endl;
        std::cout << "DPI: " << mon.dpi_scale << std::endl;
    }
```
___

### knst_image_loader ---> Can read image files from a given path (currently only bmp and png)

- Real example from the code:
```cpp
    #define KNST_BITMAP_16_16       (1 << 0)
    #define KNST_BITMAP_24_24       (1 << 1)
    #define KNST_BITMAP_32_32       (1 << 2)
    #define KNST_BITMAP_48_48       (1 << 3)
    #define KNST_BITMAP_64_64       (1 << 4)
    #define KNST_BITMAP_96_96       (1 << 5)
    #define KNST_BITMAP_128_128     (1 << 6)
    #define KNST_BITMAP_256_256     (1 << 7)


    #define KNST_BITMAP_OUTPUT_RGB   (1 << 8)   
    #define KNST_BITMAP_OUTPUT_RGBA  (1 << 9)   
    #define KNST_BITMAP_OUTPUT_BGR   (1 << 10)  
    #define KNST_BITMAP_OUTPUT_BGRA  (1 << 11)  


    #define KNST_BITMAP_GET_SIZE(flags)     ((flags) & 0xFF)
    #define KNST_BITMAP_GET_FORMAT(flags)   ((flags) & 0xFF00)
```

- You can combine these macros with `|`, e.g. `KNST_BITMAP_96_96 | KNST_BITMAP_OUTPUT_BGRA`.

```cpp
    int width,height;

    // this example works across all supported platforms (except Android) — tested
    knst_byte_string bmp_data = knst_image_loader::load_bmp("/home/knst_user/Desktop/KernelNucleusT/icon_example/cpp_logo.bmp",&width,&height,KNST_BITMAP_64_64 | KNST_BITMAP_OUTPUT_RGBA);

    // Used inside the library for purposes such as:
    window.set_bmp_cursor(bmp_data,width,height);
    window.apply_bmp_icon(bmp_data,width,height)
    // these kinds of things
    // naturally, these settings should be applied after 'creation()'
```

## knst_window Methods and Macro Features


## PLATFORM AND CONTENT DETECTION MACROS
```cpp
#define KNST_LINUX_PLATFORM_WAYLAND // if you're using Linux Wayland
#define KNST_LINUX_PLATFORM_X11 // if you're using Linux X11
#define KNST_USING_PLATFORM_ANDROID // if you're using Android


// if you're using Android, you must specify whether you're using OpenGL or Vulkan
#define KNST_PLATFORM_ANDROID_OPENGL
#define KNST_PLATFORM_ANDROID_VULKAN


// if you're using Linux X11 with OpenGL, you have two options and MUST specify one
// EGL / GLX
#define KNST_OPENGL_USING_EGL
#define KNST_OPENGL_USING_GLX
```

___
### Methods ###

```cpp

inline void knst_window::creation() noexcept; // creates the window, introduces it to the OS
inline void knst_window::show() noexcept; // shows the window
KNST_FORCE_INLINE void creation_and_show()noexcept; // first creates your window, then shows it
inline void knst_window::destroy() noexcept; // destroys the window's resources
inline void knst_window::set_title(const knst_c16string& title) noexcept // sets the window title
inline void knst_window::move(int root_x, int root_y, const knst_monitor& monitor) noexcept // moves the window to the given position; as mentioned earlier, you can pass the KNST_DEFAULT macro for a coordinate you don't want to change — e.g. if only root_x should change and root_y should stay fixed, pass KNST_DEFAULT for root_y. Important warning: this does not work on Wayland for security reasons — see the Wayland behavior section of the docs.
inline void knst_window::move(int root_x, int root_y) noexcept; // moves the window to the given position
inline void knst_window::toggle_fullscreen(bool fullscreen) noexcept; // puts your window into fullscreen state
inline void knst_window::set_minimized() noexcept; // puts your window into a minimized state
inline void knst_window::set_maximized() noexcept; // puts your window into a maximized state
inline void knst_window::restore() noexcept; // restores your window to its previous state
inline void knst_window::hide() noexcept; // hides your window
inline void knst_window::focus() noexcept; // brings your window to the front and gives it focus

inline void knst_window::set_cursor(uint16_t cursor_type) noexcept; // lets you use the operating system's built-in cursors
#define KNST_CURSOR_ARROW          
#define KNST_CURSOR_IBEAM          
#define KNST_CURSOR_CROSSHAIR      
#define KNST_CURSOR_HAND           
#define KNST_CURSOR_HRESIZE       
#define KNST_CURSOR_VRESIZE        
#define KNST_CURSOR_MOVE           
#define KNST_CURSOR_WAIT           
#define KNST_CURSOR_HELP           
#define KNST_CURSOR_NOT_ALLOWED    
// you can pass macros like these as parameters

inline void knst_window::apply_bmp_icon(const knst_byte_string& bytes, int icon_width, int icon_height) noexcept; // lets you assign your application's icon! On Wayland you'll need to do this via a .desktop file.

inline void knst_window::set_bmp_cursor(const knst_byte_string& data,int width,int height,int hot_x, int hot_y) noexcept; // turns your application's cursor into the given bmp file

inline void knst_window::reset_cursor() noexcept; // resets the cursor to its default state

inline void knst_window::resize(int width, int height) noexcept; // changes the window's size

inline void knst_window::set_cursor_mode(int mode) noexcept; // sets the cursor's state
#define KNST_CURSOR_NORMAL
#define KNST_CURSOR_HIDDEN
#define KNST_CURSOR_DISABLED
// you can set the cursor's state using macros like these

inline void knst_window::set_cursor_pos_on_window(int x, int y) noexcept; // sets the cursor's position within the window

inline void knst_window::set_cursor_pos_global(int root_x, int root_y) noexcept; // sets the cursor's position relative to the entire screen


inline void knst_window::set_clipboard(const knst_c16string& text) noexcept; // writes the given value to the operating system's clipboard

inline void knst_window::request_clipboard() noexcept; // fetches the data from the OS's clipboard and stores it in your knst_window object's clipboard_text field; you can then retrieve it with get_clipboard()

inline void knst_window::set_drag_drop_status(bool enabled) noexcept; // lets you enable/disable drag & drop support in your application

inline void knst_window::set_opacity(float opacity) noexcept; // sets your application's opacity! On Wayland this only affects the title bar's opacity — you'll need to handle the content's opacity yourself.


inline void knst_window::set_attribute(int attribute, bool value) noexcept; // lets you assign a property to the window; first parameter is the macro, second is whether that property should be on or off
#define KNST_WINDOW_ATTRIB_DECORATED 
#define KNST_WINDOW_ATTRIB_RESIZABLE
#define KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP
#define KNST_WINDOW_ATTRIB_TRANSPARENT

inline bool knst_window::get_attribute(int attribute) const noexcept; // takes one of the macros above as a parameter and returns the current state of that property on your window

inline void knst_window::set_minimum_size(int width, int height) noexcept; // sets the minimum size the window is allowed to be

inline void knst_window::set_maximum_size(int width, int height) noexcept; // sets the maximum size the window is allowed to be


KNST_FORCE_INLINE const knst_c16string& get_title() const noexcept; // returns the window's title
KNST_FORCE_INLINE void set_user_data(void* data)noexcept; // lets you attach your own custom data to the window
KNST_FORCE_INLINE const void* get_user_data()const noexcept; // lets you retrieve your custom data from the window
KNST_FORCE_INLINE const bool& is_should_close()const noexcept; // indicates whether your window should be closed
KNST_FORCE_INLINE void should_close() noexcept; // tells the window it should close
KNST_FORCE_INLINE void clear_temporary_events() noexcept; // clears the events that need to be reset every loop iteration
KNST_FORCE_INLINE const knst_window_event& get_window_event_handle() const noexcept; // returns the event object inside your window

template<typename Callback>
KNST_FORCE_INLINE void set_redraw_callback(Callback&& callback) noexcept; // lets you assign a callback
KNST_FORCE_INLINE void set_redraw_callback(void (*callback)(knst_window&, void*)) noexcept; // lets you assign a callback
KNST_FORCE_INLINE void call_redraw_callback() noexcept; // invokes the callback
KNST_FORCE_INLINE const knst_c16string& get_clipboard() const noexcept; // retrieves the data copied to the clipboard, stored in the window object
KNST_FORCE_INLINE void clear_clipboard() noexcept; // clears the clipboard data stored in the window object
KNST_FORCE_INLINE const float& get_opacity() const noexcept; // returns the opacity value
KNST_FORCE_INLINE void set_title_bar_height(int height) noexcept; // sets the title bar's height
KNST_FORCE_INLINE int get_title_bar_height() const noexcept; // gets the title bar's height

// platform-specific objects, real example:
// you can also retrieve globally stored objects from within KnstWindowSources

#if KNST_USING_PLATFORM_WINDOWS
        
    KNST_FORCE_INLINE const HWND& get_windows_window_handle() const noexcept{
        return m_window;
    }

#elif KNST_USING_LINUX_PLATFORM_X11

    KNST_FORCE_INLINE const xcb_window_t& get_x11_window_handle() const noexcept{
        return m_window;
    }

#elif KNST_USING_LINUX_PLATFORM_WAYLAND

        KNST_FORCE_INLINE const wl_surface * get_wayland_surface_handle() const noexcept{
            return m_surface;
        }

#endif


```

## 1. MOUSE BUTTONS

| Macro | Description |
|-------|-------------|
| KNST_MOUSE_BUTTON_LEFT | Left mouse button |
| KNST_MOUSE_BUTTON_MIDDLE | Middle mouse button |
| KNST_MOUSE_BUTTON_RIGHT | Right mouse button |
| KNST_MOUSE_SCROLL_UP | Wheel scrolled up |
| KNST_MOUSE_SCROLL_DOWN | Wheel scrolled down |

___

## 2. MOUSE EVENTS

| Macro | Description |
|-------|-------------|
| KNST_MOUSE_BUTTON_PRESS | A mouse button was pressed |
| KNST_MOUSE_BUTTON_RELEASE | A mouse button was released |
| KNST_MOUSE_SCROLL | The wheel moved |
| KNST_MOUSE_EVENT | A mouse event occurred |
___

## 3. Keyboard Keys

## Letter Keys (A-Z)

| Macro | Description |
|-------|-------------|
| KNST_KEY_A | A key |
| KNST_KEY_B | B key |
| KNST_KEY_C | C key |
| KNST_KEY_D | D key |
| KNST_KEY_E | E key |
| KNST_KEY_F | F key |
| KNST_KEY_G | G key |
| KNST_KEY_H | H key |
| KNST_KEY_I | I key |
| KNST_KEY_J | J key |
| KNST_KEY_K | K key |
| KNST_KEY_L | L key |
| KNST_KEY_M | M key |
| KNST_KEY_N | N key |
| KNST_KEY_O | O key |
| KNST_KEY_P | P key |
| KNST_KEY_Q | Q key |
| KNST_KEY_R | R key |
| KNST_KEY_S | S key |
| KNST_KEY_T | T key |
| KNST_KEY_U | U key |
| KNST_KEY_V | V key |
| KNST_KEY_W | W key |
| KNST_KEY_X | X key |
| KNST_KEY_Y | Y key |
| KNST_KEY_Z | Z key |

## Turkish Characters

| Macro | Description |
|-------|-------------|
| KNST_KEY_C_CEDILLA | Ç letter |
| KNST_KEY_G_BREVE | Ğ letter |
| KNST_KEY_I_DOTLESS | ı letter |
| KNST_KEY_O_DIAERESIS | Ö letter |
| KNST_KEY_S_CEDILLA | Ş letter |
| KNST_KEY_U_DIAERESIS | Ü letter |

## Numbers (0-9)

| Macro | Description |
|-------|-------------|
| KNST_KEY_0 | 0 key |
| KNST_KEY_1 | 1 key |
| KNST_KEY_2 | 2 key |
| KNST_KEY_3 | 3 key |
| KNST_KEY_4 | 4 key |
| KNST_KEY_5 | 5 key |
| KNST_KEY_6 | 6 key |
| KNST_KEY_7 | 7 key |
| KNST_KEY_8 | 8 key |
| KNST_KEY_9 | 9 key |

## Function Keys (F1-F12)

| Macro | Description |
|-------|-------------|
| KNST_KEY_F1 | F1 key |
| KNST_KEY_F2 | F2 key |
| KNST_KEY_F3 | F3 key |
| KNST_KEY_F4 | F4 key |
| KNST_KEY_F5 | F5 key |
| KNST_KEY_F6 | F6 key |
| KNST_KEY_F7 | F7 key |
| KNST_KEY_F8 | F8 key |
| KNST_KEY_F9 | F9 key |
| KNST_KEY_F10 | F10 key |
| KNST_KEY_F11 | F11 key |
| KNST_KEY_F12 | F12 key |

## Control Keys

| Macro | Description |
|-------|-------------|
| KNST_KEY_ESCAPE | ESC key |
| KNST_KEY_ENTER | Enter key |
| KNST_KEY_SPACE | Space key |
| KNST_KEY_BACKSPACE | Backspace key |
| KNST_KEY_TAB | Tab key |
| KNST_KEY_CAPS_LOCK | Caps Lock key |
| KNST_KEY_NUM_LOCK | Num Lock key |
| KNST_KEY_SCROLL_LOCK | Scroll Lock key |
| KNST_KEY_INSERT | Insert key |
| KNST_KEY_DELETE | Delete key |
| KNST_KEY_PRINT | Print Screen key |
| KNST_KEY_PAUSE | Pause key |
| KNST_KEY_BREAK | Break key |

## Modifier Keys

| Macro | Description |
|-------|-------------|
| KNST_KEY_SHIFT | Shift key |
| KNST_KEY_CONTROL | Ctrl key |
| KNST_KEY_ALT | Alt key |
| KNST_KEY_SUPER | Windows/Command key |
| KNST_KEY_MENU | Menu key |

## Arrow Keys

| Macro | Description |
|-------|-------------|
| KNST_KEY_LEFT | Left arrow key |
| KNST_KEY_RIGHT | Right arrow key |
| KNST_KEY_UP | Up arrow key |
| KNST_KEY_DOWN | Down arrow key |
| KNST_KEY_HOME | Home key |
| KNST_KEY_END | End key |
| KNST_KEY_PAGE_UP | Page Up key |
| KNST_KEY_PAGE_DOWN | Page Down key |

## Numpad Keys

| Macro | Description |
|-------|-------------|
| KNST_KEY_NUMPAD_0 | Numpad 0 |
| KNST_KEY_NUMPAD_1 | Numpad 1 |
| KNST_KEY_NUMPAD_2 | Numpad 2 |
| KNST_KEY_NUMPAD_3 | Numpad 3 |
| KNST_KEY_NUMPAD_4 | Numpad 4 |
| KNST_KEY_NUMPAD_5 | Numpad 5 |
| KNST_KEY_NUMPAD_6 | Numpad 6 |
| KNST_KEY_NUMPAD_7 | Numpad 7 |
| KNST_KEY_NUMPAD_8 | Numpad 8 |
| KNST_KEY_NUMPAD_9 | Numpad 9 |
| KNST_KEY_NUMPAD_ADD | Numpad addition (+) |
| KNST_KEY_NUMPAD_SUBTRACT | Numpad subtraction (-) |
| KNST_KEY_NUMPAD_MULTIPLY | Numpad multiplication (*) |
| KNST_KEY_NUMPAD_DIVIDE | Numpad division (/) |
| KNST_KEY_NUMPAD_DECIMAL | Numpad decimal (.) |
| KNST_KEY_NUMPAD_ENTER | Numpad enter |

## Punctuation Marks

| Macro | Description |
|-------|-------------|
| KNST_KEY_SEMICOLON | ; semicolon |
| KNST_KEY_SLASH | / slash |
| KNST_KEY_GRAVE | ` grave accent |
| KNST_KEY_LEFT_BRACKET | [ left square bracket |
| KNST_KEY_BACKSLASH | \ backslash |
| KNST_KEY_RIGHT_BRACKET | ] right square bracket |
| KNST_KEY_APOSTROPHE | ' apostrophe |
| KNST_KEY_PERIOD | . period |
| KNST_KEY_COMMA | , comma |
| KNST_KEY_MINUS | - minus |
| KNST_KEY_PLUS | + plus |
| KNST_KEY_EQUALS | = equals |
| KNST_KEY_QUOTE | " quote |
| KNST_KEY_COLON | : colon |
| KNST_KEY_TILDE | ~ tilde |
| KNST_KEY_LESS | < less than |
| KNST_KEY_GREATER | > greater than |
| KNST_KEY_QUESTION | ? question mark |
| KNST_KEY_PIPE | | pipe |
| KNST_KEY_EXCLAM | ! exclamation mark |
| KNST_KEY_AT | @ at sign |
| KNST_KEY_HASH | # hash |
| KNST_KEY_DOLLAR | $ dollar |
| KNST_KEY_PERCENT | % percent |
| KNST_KEY_CIRCUMFLEX | ^ circumflex |
| KNST_KEY_AMPERSAND | & ampersand |
| KNST_KEY_ASTERISK | * asterisk |
| KNST_KEY_LEFT_PAREN | ( left parenthesis |
| KNST_KEY_RIGHT_PAREN | ) right parenthesis |
| KNST_KEY_UNDERSCORE | _ underscore |
| KNST_KEY_LEFT_BRACE | { left brace |
| KNST_KEY_RIGHT_BRACE | } right brace |

## Media Keys

| Macro | Description |
|-------|-------------|
| KNST_KEY_VOLUME_UP | Volume up |
| KNST_KEY_VOLUME_DOWN | Volume down |
| KNST_KEY_VOLUME_MUTE | Mute |
| KNST_KEY_MEDIA_PLAY | Play |
| KNST_KEY_MEDIA_STOP | Stop |
| KNST_KEY_MEDIA_NEXT | Next track |
| KNST_KEY_MEDIA_PREV | Previous track |
| KNST_KEY_MEDIA_PAUSE | Pause |

## Browser Keys

| Macro | Description |
|-------|-------------|
| KNST_KEY_BROWSER_HOME | Browser home |
| KNST_KEY_BROWSER_BACK | Browser back |
| KNST_KEY_BROWSER_FORWARD | Browser forward |
| KNST_KEY_BROWSER_REFRESH | Browser refresh |
| KNST_KEY_BROWSER_SEARCH | Browser search |
| KNST_KEY_BROWSER_FAVORITES | Browser favorites |

## Modifier Mask Keys

| Macro | Description |
|-------|-------------|
| KNST_MOD_SHIFT | Shift key mask |
| KNST_MOD_CONTROL | Ctrl key mask |
| KNST_MOD_ALT | Alt key mask |
| KNST_MOD_SUPER | Super/Windows key mask |
| KNST_MOD_CAPS_LOCK | Caps Lock mask |
| KNST_MOD_NUM_LOCK | Num Lock mask |


___

## 4. KEYBOARD EVENTS

| Macro | Description |
|-------|-------------|
| KNST_KEY_PRESS | A key was pressed |
| KNST_KEY_RELEASE | A key was released |
| KNST_KEY_REPEAT | A key repeated |
| KNST_KEYBOARD_EVENT | A keyboard event occurred |

___

## 5. WINDOW EVENTS

| Macro | Description |
|-------|-------------|
| KNST_WINDOW_FULL_SCREEN | Fullscreen |
| KNST_WINDOW_RESTORE | Restored to previous state |
| KNST_MOTION_NOTIFY | Mouse moved |
| KNST_WINDOW_RESIZE | Window was resized |
| KNST_WINDOW_MOVE | Window was moved |
| KNST_CLOSE_WINDOW | Window is being closed |
| KNST_WINDOW_MAXIMIZE | Maximized |
| KNST_WINDOW_MINIMIZE | Minimized |

## 6. FOCUS EVENTS

| Macro | Description |
|-------|-------------|
| KNST_FOCUS_IN | Focus gained |
| KNST_FOCUS_OUT | Focus lost |
| KNST_ENTER_NOTIFY | Mouse entered the window |
| KNST_LEAVE_NOTIFY | Mouse left the window |


## 7. Other Events

| Macro | Description |
|-------|-------------|
| KNST_UNKNOWN | Unknown event |
| KNST_EXPOSE | Redraw required |
| KNST_DISCONNECT | Connection lost |

## 8. CURSOR

| Macro | Description |
|-------|-------------|
| KNST_CURSOR_NORMAL | Normal cursor |
| KNST_CURSOR_HIDDEN | Hidden cursor |
| KNST_CURSOR_DISABLED | Disabled cursor |

## 9. FILE DRAG & DROP

| Macro | Description |
|-------|-------------|
| KNST_FILE_DROP_ENTER | A file entered the window |
| KNST_FILE_DROP_MOVE | A file moved |
| KNST_FILE_DROP_LEAVE | A file left the window |
| KNST_FILE_DROP | A file was dropped |


## 10. WINDOW ATTRIBUTES

| Macro | Description |
|-------|-------------|
| KNST_WINDOW_ATTRIB_DECORATED | Has a title bar |
| KNST_WINDOW_ATTRIB_RESIZABLE | Resizable |
| KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP | Always on top |
| KNST_WINDOW_ATTRIB_TRANSPARENT | Transparent window |



## 11. MOBILE APP STATE

| Macro | Description |
|-------|-------------|
| KNST_WINDOW_LOST | Window was lost |
| KNST_LOW_MEMORY | Low memory |
| KNST_APP_STARTED | App started |
| KNST_APP_RESUMED | App resumed |
| KNST_APP_PAUSED | App paused |
| KNST_SAVE_STATE | State should be saved |
| KNST_CONTENT_RECT_CHANGED | Content area changed |
| KNST_CONFIG_CHANGED | Configuration changed |
| KNST_INPUT_CHANGED | Input method changed |
| KNST_APP_STOPPED | App stopped |


## 12. MOBILE TOUCH

| Macro | Description |
|-------|-------------|
| KNST_MOBILE_TOUCH_EVENT | A touch event occurred |


## 13. MOBILE SYSTEM KEYS

| Macro | Description |
|-------|-------------|
| KNST_MOBILE_BACK_PRESS | Back key |
| KNST_MOBILE_HOME_PRESS | Home key |
| KNST_MOBILE_MENU_PRESS | Menu key |
| KNST_MOBILE_SEARCH_PRESS | Search key |
| KNST_MOBILE_VOLUME_UP | Volume up |
| KNST_MOBILE_VOLUME_DOWN | Volume down |
| KNST_MOBILE_APP_SWITCH | App switch |
| KNST_MOBILE_RECENT_APPS | Recent apps |
| KNST_MOBILE_VOLUME_MUTE | Mute |
| KNST_MOBILE_POWER | Power key |
| KNST_MOBILE_CAMERA | Camera key |
| KNST_MOBILE_HELP | Help key |
| KNST_MOBILE_SETTINGS | Settings key |
| KNST_MOBILE_SLEEP | Sleep mode |
| KNST_MOBILE_WAKEUP | Wake up |


## 14. MOBILE MEDIA KEYS

| Macro | Description |
|-------|-------------|
| KNST_MOBILE_MEDIA_PLAY_PAUSE | Play/Pause |
| KNST_MOBILE_MEDIA_STOP | Stop |
| KNST_MOBILE_MEDIA_NEXT | Next |
| KNST_MOBILE_MEDIA_PREVIOUS | Previous |
| KNST_MOBILE_MEDIA_REWIND | Rewind |
| KNST_MOBILE_MEDIA_FAST_FORWARD | Fast forward |
| KNST_MOBILE_MEDIA_RECORD | Record |
| KNST_MOBILE_MEDIA_CLOSE | Close |
| KNST_MOBILE_MEDIA_EJECT | Eject |
| KNST_MOBILE_MEDIA_PAUSE | Pause |

## 15. MOBILE SYSTEM ACTIONS

| Macro | Description |
|-------|-------------|
| KNST_MOBILE_NOTIFICATION | Notifications |
| KNST_MOBILE_ASSIST | Assistant |
| KNST_MOBILE_VOICE_ASSIST | Voice assistant |
| KNST_MOBILE_BOOKMARK | Bookmark |
| KNST_MOBILE_CALCULATOR | Calculator |
| KNST_MOBILE_CALENDAR | Calendar |
| KNST_MOBILE_CONTACTS | Contacts |
| KNST_MOBILE_EXPLORER | File explorer |
| KNST_MOBILE_MUSIC | Music player |

## 16. MOBILE NUMPAD KEYS

| Macro | Description |
|-------|-------------|
| KNST_KEY_NUMPAD_COMMA | Numpad comma |
| KNST_KEY_NUMPAD_EQUALS | Numpad equals |
| KNST_KEY_NUMPAD_LEFT_PAREN | Numpad left parenthesis |
| KNST_KEY_NUMPAD_RIGHT_PAREN | Numpad right parenthesis |

## 17. MOBILE TOUCH ACTIONS

| Macro | Description |
|-------|-------------|
| KNST_MOBILE_TOUCH_ACTION_PRESS | Finger pressed down |
| KNST_MOBILE_TOUCH_ACTION_RELEASE | Finger lifted |
| KNST_MOBILE_TOUCH_ACTION_MOVE | Finger moved |
| KNST_MOBILE_TOUCH_ACTION_CANCEL | Event was cancelled |
| KNST_MOBILE_TOUCH_ACTION_OUTSIDE | Tapped outside the window |
| KNST_MOBILE_TOUCH_ACTION_POINTER_PRESS | A secondary finger pressed down |
| KNST_MOBILE_TOUCH_ACTION_POINTER_RELEASE | A secondary finger lifted |


## 18. MOBILE SCREEN ORIENTATION

| Macro | Description |
|-------|-------------|
| KNST_MOBILE_ORIENTATION_UNDEFINED | Undefined |
| KNST_MOBILE_ORIENTATION_PORTRAIT | Portrait mode |
| KNST_MOBILE_ORIENTATION_LANDSCAPE | Landscape mode |
| KNST_MOBILE_ORIENTATION_SQUARE | Square mode |

// May vary depending on the Android version
## 19. MOBILE NIGHT MODE

| Macro | Description |
|-------|-------------|
| KNST_MOBILE_NIGHT_MODE_OFF | Night mode off |
| KNST_MOBILE_NIGHT_MODE_ON | Night mode on |

## 20. DEFAULT

| Macro | Description |
|-------|-------------|
| KNST_DEFAULT | Default / unassigned value state |