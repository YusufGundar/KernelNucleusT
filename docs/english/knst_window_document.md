# knst_window — Usage Guide (English)

**Aims to be a highly comprehensive window management library with full support for Windows, Linux, and Android, and to offer you as many features as possible**

**Features:**
- **`force inline` is used in most critical places that repeat continuously while the application is running**
- **Provides users with clean code writing possibilities via modern C++ features**
- **Captures and processes events in a manner similar to the operating system's event logic**
- **Security** — Has successfully passed a limited number of tests

---

**Library Architecture**

To use the library, you first need to call `KnstWindowSources::Init()`. `KnstWindowSources` contains the structures that need to be kept global throughout the library; you initialize it with the `Init()` function. It also makes a call to `knst_display::refresh_screens()` internally to retrieve the current screens and their information — there is no point in calling this again within the program unless necessary, as the call has already been made; the `knst_monitor` structure already holds the monitor/screen properties.

After initializing the library, you are now ready to create windows and process events.

#### Window creation: ####
```cpp
knst_window window('width', 'height', 'title', 'x position on main screen', 'y position on main screen', 'monitor');
```


| Parameter | Type | Description | Example |
|-----------|-----|----------|-------|
| width | int | Window width (pixels) | 800 |
| height | int | Window height (pixels) | 600 |
| title | std::u16string | Window title (UTF-16) | u"Triangle Test" |
| x position on main screen | int | x position (pixel) | u"200" |
| y position on main screen | int | y position (pixel) | u"200" |
| monitor | knst_monitor | knst_monitor object | knst_display::get_primary_monitor() |
---

⚠️: On Linux (Wayland), it is the compositor — not us — that decides where the window will appear, meaning even if you provide a position on the main screen, it 'will not be taken into account'

#### The next step is 'Creation and Showing': 

- creation()
- show()
- creation_and_show()

The constructor only takes and stores the information; the actual window creation process happens inside the `creation()` function — at this stage the window is introduced to the operating system and resources are initialized, this is what the `creation()` function does. Afterwards you can show it with `show()`. The `creation_and_show()` function calls `creation()` first and then `show()` internally — if you want, you can do `creation()` then `show()` separately, or if you want, you can create and show it in a single line with `creation_and_show()`. So that there are no flicker issues, I did not put show inside `creation()` — if you have settings/adjustments to make, you do them after `creation()`, then call `show()`, and your window opens according to those settings

- If the window's position on the main screen doesn't matter to you, you can let the operating system decide by using the `KNST_DEFAULT` macro — this is not only valid for creation; you can pass the `KNST_DEFAULT` macro for any position values you don't want to change

#### Next step: 'Event Handling and the Event Loop': 

**Event Handling:**
#### There are 3 basic event handling types in knst_window ###

- block_pool_event ---> returns when an event arrives
- non_block_pool_event ---> takes the event if there is one and returns, otherwise returns immediately
- timeout_pool_event ---> waits for the duration you give, then takes the event if there is one afterward, otherwise returns immediately

📌 **NOTE:** These structures are found publicly and statically inside `knst_window_event_system`; you only need to call them — in multi-window applications, the event will be correctly delivered to the appropriate window

- If the `type` of the event object is KNST_UNKNOWN, its type could not be determined during the loop

- After these 3 loops, the event object fills the contents of the `m_knst_event (private)` object inside the `knst_window` object you created; you can access this m_knst_event object as const via `window.get_window_event_handle()` and check the values inside it. A real example of this structure is as follows:

```cpp
    #if defined(KNST_USING_PLATFORM_ANDROID)

        int pointer_count = 0; // number of fingers touching the screen simultaneously (multi-touch)
        float pointer_x[10] = {0}; // X coordinate of each finger (in pixels)
        float pointer_y[10] = {0}; // Y coordinate of each finger (in pixels)
        int pointer_id[10] = {0}; // unique ID of each finger (for finger tracking)
        int touch_action = 0; // type of touch event: pressed, released, moved, cancelled, etc.
        int content_left = 0; // left edge of the window content area
        int content_top = 0; // top edge of the window content area
        int content_right = 0; // right edge of the window content area
        int content_bottom = 0; // bottom edge of the window content area
        int orientation = 0; // screen orientation: portrait = 0, landscape = 1, etc.
        char language[4] = {0}; // system language
        char country[4] = {0}; // system country code
        bool is_night_mode = false; // night mode status
        bool is_low_memory = false; // device's memory status
        float density = 1.0f; // dpi ratio
        int screen_width_dp = 0; // screen width (in dp)
        int screen_height_dp = 0; // screen height (in dp)
        void* saved_state = nullptr; // data block saved when the application closes
        size_t saved_state_size = 0; // size of the saved data block (in bytes)

    #endif

        int type = 0; // type of the incoming event
        int key_code = 0; // which key it is       
        int scancode = 0; // the actual hardware code
        int key_action = 0; // key status: press, release, repeat information
        int mods = 0; // information about which modifier key was held down together with the pressed key
        bool is_focused = 0; // window's focus status
        bool mouse_on_window = 0; // whether the mouse is inside the window
        int mouse_x = 0; // mouse's x position within the window
        int mouse_y = 0; // mouse's y position within the window
        int mouse_root_x = 0; // mouse's x position on the monitor
        int mouse_root_y = 0; // mouse's y position on the monitor
        int mouse_button = 0; // mouse button status: left, right
        int mouse_action = 0; // mouse status: press, release
        int mouse_scroll_delta = 0; // mouse wheel status: 'positive means up', 'negative means down'
        int window_width = 0; // window's width
        int window_height = 0; // window's height
        int window_root_x = 0; // window's x position on the monitor
        int window_root_y = 0; // window's y position on the monitor
        bool is_full_screen = false; // window's full_screen status
        bool is_minimized = false; // window's minimize status
        bool is_maximized = false; // window's maximize status

        knst_vector<knst_c16string>drop_files; // if drag-and-drop is enabled, file paths are stored here
        uint32_t drop_count = 0; // how many files were dropped via drag-and-drop

        // You call this from the window object via 'clear_temporary_events()'
        KNST_FORCE_INLINE void clear()noexcept{
            key_code = 0;
            scancode = 0;
            key_action = 0;
            mouse_button = 0;
            mouse_action = 0;
            mouse_scroll_delta = 0;
            type = 0;        
        }

```

⚠️: Another warning, again for Linux (Wayland). Global values in the event object inside the window such as `window_root_y`, `window_root_x`, `mouse_root_x`, `mouse_root_y` will never be filled — you can research the details, Wayland's compositor blocks most of these things for security reasons

⚠️: On the Android side as well, mouse action, maximized statuses, etc. are default — meaning you cannot determine them, and also touch events come in instead of mouse events

#### My recommendation is as follows: ####
- Check the condition of the while loop with `!window.is_should_close()` — this way, when a closing or connection issue occurs within the window, you can, for example, call `window.should_close()` to close the window, i.e., exit the while loop

- Also, another important piece of information: the `KNST_CLOSE_WINDOW` macro arrives when the window's close button is pressed or it needs to close, while `KNST_DISCONNECT` arrives when retrieving the event object fails on Linux (X11); also another important note:

⚠️: The `KNST_DISCONNECT` event arrives on Linux (Wayland) when your application is open and the computer is put into sleep mode and then reopened — your application receives the `KNST_DISCONNECT` event. If you don't want your application to close in this situation, you need to check this `KNST_DISCONNECT` event separately and reinitialize your structures

#### If I were to give a simple application example: ####

```cpp
#include <iostream>
#include "../../include/KernelNucleusT.hpp"

int main(){

    KnstWindowSources::Init(); // Library initialization

    knst_window window(800,600,u"Triangle Test"); // constructor is called

    window.creation_and_show(); // window is actually created

    while(!window.is_should_close()){ // window's closing status is queried

        knst_window_event_system::block_pool_event(); // only triggers when an event arrives

        if(window.get_window_event_handle().type == KNST_CLOSE_WINDOW || window.get_window_event_handle().type == KNST_DISCONNECT){ // closes itself when it detects closing or connection loss

            window.destroy();
            window.should_close();
              
        }        
        
        window.clear_temporary_events(); // temporary events are cleared

    }

    KnstWindowSources::CleanUp(); // global resources are cleaned up

    std::cout << "Cleaning all sources..." << std::endl;
    return 0;
}
```

- A simple example looks like this


### Window Customization: ###

- If you want to use your window without callbacks, I recommend defining the `KNST_DISABLE_REDRAW_ON_EVENT_MANAGER` macro with #define — however, if you define this macro, you may encounter white screen and non-loading issues during resizing on Windows; to remain cross-platform compatible, you need to assign a callback by default

- My recommendation is that you do NOT define the `KNST_DISABLE_REDRAW_ON_EVENT_MANAGER` macro — however, if you wish, you can define it to fully take control of the render state yourself; of course the choice is yours; also, if you suspect there will be slowdowns, all these call operations are already done by being pasted directly without a call, via `KNST_FORCE_INLINE`

___

- If you want to close the default title bar given by the operating system in your window, it will be enough to define the `KNST_DISABLE_TITLE_BAR` macro with #define — unlike using `set_attribute(KNST_WINDOW_ATTRIB_DECORATED , false)` on knst_window, this gives you the same window with only the title bar removed, while still allowing resizing. Normally if you do it via `set_attribute`, you cannot maximize/minimize the window — it's generally used by game developers — but by defining this macro you only remove the title bar


### OpenGL and Vulkan support: ###

- To use `opengl` or `vulkan` in your window, you first need to create the content of the graphics API you want and introduce your window to your content

#### OpenGL: ####
- You can create content with `knst_window_opengl_content content`
- content.Init(`knst_window`, `vsync status`) — you should give the address of your window with & to `knst_window`; the vsync status is false by default, but you can give whatever value you want

⚠️: If you're planning to draw the title bar yourself, I recommend giving the vsync status as false, especially for Wayland

- There are title bar themes I've specially made for OpenGL; to use them fully in your window, first define the `KNST_DISABLE_TITLE_BAR` macro with #define, then you can define the macro of the theme you want with #define

```cpp
#define KNST_WINDOW_USING_KNST_TITLE_BAR_WHITE_MODERN
#define KNST_WINDOW_USING_KNST_TITLE_BAR_BLUE_MODERN
#define KNST_WINDOW_USING_KNST_TITLE_BAR_FUTURISTIC
#define KNST_WINDOW_USING_KNST_TITLE_BAR_SUNSET_GLOW
```
- You can define whichever of these macros you want — in fact, if you have a theme suggestion, you can reach out to me, I can add it among these macros


- This title bar drawing process happens inside the OpenGL content's function called `SwapBuffers` — 'not applicable for Android'. If you want, you can define the theme macros and use it normally with swap buffers, or if you want, you can just disable the title bar with the macro and change the title bar however you like at runtime

```cpp
DrawKnstTitleBarBlueModern()
DrawKnstTitleBarWhiteModern()
DrawKnstTitleBarFuturistic()
DrawKnstTitleBarSunsetGlow()
```
- You can use functions like these


⚠️: Again for Linux (Wayland), most Linux distributions don't provide a special title bar, so we definitely need to draw it ourselves; of course there are distributions with SSD (Server-Side Decoration) support, meaning the operating system provides a special title bar, like KDE — however, while designing the library, so that it's compatible with every distribution's Wayland, I draw the `DrawKnstTitleBarWhiteModern()` title bar myself by default. Of course you can change this with the macro — also, if you start with the `BeginFrame` function inside the content, this title bar and content area will be set up correctly

___

- I recommend using the OpenGL content in the following way: if you're going to use it with a callback, then like in the `package_tests/knst_window` example, after passing the content via userdata, I recommend starting with the `BeginFrame()` function inside the OpenGL content to begin drawing the image — this both sets up the title bar area and content area in a cross-platform compatible way, and ensures every image is drawn cleanly

- You need to close your OpenGL content at the very end with `Shutdown()`


#### Vulkan: ####

- In Vulkan support, we only provide the necessary resources — of course I would like to do special title bar drawing operations, themes, etc., but for now in this version of the library we're only providing the resources


- You can create content with `knst_window_vulkan_content content`
- With content.Init(`knst_window`), you give the address of your window with & to `knst_window` and connect your window to your content


- You need to close your Vulkan content at the very end with `Destroy()`


## Android ##

- Since `knst_window` is a cross-platform library, it tries to make you feel this platform difference at the lowest level possible — however, when it comes to Android, things change a bit

- To make a basic application, for the Android side the only difference is that your application starts with `void android_main(struct android_app* app)` instead of `int main`, and unlike the desktop version, the only difference here is that the `KnstWindowSources::Init()` function expects a `struct android_app* app` parameter — the only thing you need to do differently is `KnstWindowSources::Init(app)`, that's it. This ensures cross-platform compatibility, but of course, as you'd expect, on Android you need to control a lot more things — for example, what happens when the application is put into the background, what happens when the virtual keyboard opens, how the application closing situation is handled, etc. — I'll talk about these in the next section anyway

- For opening the virtual keyboard on Android, I designed the `knst_mobile_keyboard` class — within this class, the

```cpp
    static bool hide(); // hides the keyboard
    static void toggle(); // makes the keyboard visible
    static bool is_visible(); // returns whether the keyboard is open or closed
```
- methods let you communicate with the mobile keyboard. Doing Init and Shutdown is not your responsibility — KnstWindowSources handles these internally on its own — also a detailed example is available in `package_tests/knst_window/android`

- Also I'd like to note: on the Android side, when the application is being closed, the `KNST_CLOSE_WINDOW` / `KNST_DISCONNECT` events don't always arrive — sometimes the OS forcibly drops the application and can delete the resources itself — in this case, the only thing you need to do is, in the `KNST_SAVE_STATE` event, if there's something you need to save, save it before your application closes; the `KNST_SAVE_STATE` event definitely arrives before the application closes

- Also, the parameters you give to the `creation()` function are invalid — we cannot determine them; you can define some of the necessary information from within `AndroidManifest.xml`



#### Helper Structures ####

### knst_display ---> Stores the screens on the system and information about the screens

- `refresh_screens()` ---> retrieves the current information of the monitors currently plugged into the system and saves it into `knst_monitor` objects inside a `knst_vector` 
- `refresh_screens()` is already called inside `KnstWindowSources::Init()`

- `get_monitor_list()` ---> this function returns the current knst_vector<knst_monitor> object, and you can retrieve the screens inside it with the help of a loop

- `get_primary_monitor()` ---> with this function you get the `knst_monitor` object of the actual active monitor / screen

For example:
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

### knst_image_loader ---> Can read the image file at the path you specify (currently only .bmp)

- A real example from the code
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

- You can create combined definitions with `|`, such as `KNST_BITMAP_96_96 | KNST_BITMAP_OUTPUT_BGRA`, using these macros

```cpp
    int width,height;

    // this example works on all cross-platform targets (except android) tested
    knst_byte_string bmp_data = knst_image_loader::load_bmp("/home/knst_user/Desktop/KernelNucleusT/icon_example/cpp_logo.bmp",&width,&height,KNST_BITMAP_64_64 | KNST_BITMAP_OUTPUT_RGBA);

    // Used within the library for purposes such as
    window.set_bmp_cursor(bmp_data,width,height);
    window.apply_bmp_icon(bmp_data,width,height)
    // of course this setup should be done after the window is created, i.e. after 'creation()'
```

## knst_window Methods and Macro Properties


## PLATFORM AND CONTENT DETECTION MACROS
```cpp
#define KNST_LINUX_PLATFORM_WAYLAND // If you're using Linux Wayland
#define KNST_LINUX_PLATFORM_X11 // If you're using Linux X11
#define KNST_USING_PLATFORM_ANDROID // If you're using Android


// if you're using Android, you need to specify whether you're using OpenGL or Vulkan
#define KNST_PLATFORM_ANDROID_OPENGL
#define KNST_PLATFORM_ANDROID_VULKAN


// if you're using Linux X11, you have two options for opengl and you must specify one
// EGL / GLX
#define KNST_OPENGL_USING_EGL
#define KNST_OPENGL_USING_GLX
```

___
### Methods ###

```cpp

inline void knst_window::creation() noexcept; // creates the window, introduces it to the operating system
inline void knst_window::show() noexcept; // used to show the window
KNST_FORCE_INLINE void creation_and_show()noexcept; // first creates your window, then shows it
inline void knst_window::destroy() noexcept; // used to destroy the window's resources
inline void knst_window::set_title(const knst_c16string& title) noexcept // sets the window title
inline void knst_window::move(int root_x, int root_y, const knst_monitor& monitor) noexcept // moves the window to the position you want; as mentioned before, you can give the KNST_DEFAULT parameter for any coordinate you don't want to change — for example, if only root_x should change and root_y should stay fixed, you can give the KNST_DEFAULT macro instead of root_y. !! important warning: does not work on Wayland due to security reasons, you can check Wayland's behavior in the documentation
inline void knst_window::move(int root_x, int root_y) noexcept; // moves the window to the position you want
inline void knst_window::toggle_fullscreen(bool fullscreen) noexcept; // puts your window into full screen state
inline void knst_window::set_minimized() noexcept; // puts your window into minimized state
inline void knst_window::set_maximized() noexcept; // puts your window into maximized state
inline void knst_window::restore() noexcept; // restores your window's state to its previous state
inline void knst_window::hide() noexcept; // hides your window
inline void knst_window::focus() noexcept; // brings your window to the front and gives it focus

inline void knst_window::set_cursor(uint16_t cursor_type) noexcept; // you can use the operating system's cursors
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
// you can give macros like these as a parameter

inline void knst_window::apply_bmp_icon(const knst_byte_string& bytes, int icon_width, int icon_height) noexcept; // allows you to assign your application's logo ! on Wayland you can do this via .desktop

inline void knst_window::set_bmp_cursor(const knst_byte_string& data,int width,int height,int hot_x, int hot_y) noexcept; // turns your application's cursor into the bmp file you want

inline void knst_window::reset_cursor() noexcept; // restores the cursor to its previous state

inline void knst_window::resize(int width, int height) noexcept; // changes the size of the window

inline void knst_window::set_cursor_mode(int mode) noexcept; // sets the state of the cursor
#define KNST_CURSOR_NORMAL
#define KNST_CURSOR_HIDDEN
#define KNST_CURSOR_DISABLED
// you can set the cursor's state with macros like these

inline void knst_window::set_cursor_pos_on_window(int x, int y) noexcept; // sets the position of the cursor within the window 

inline void knst_window::set_cursor_pos_global(int root_x, int root_y) noexcept; // sets the cursor's position relative to the entire screen


inline void knst_window::set_clipboard(const knst_c16string& text) noexcept; // pastes the value you want into the operating system's clipboard

inline void knst_window::request_clipboard() noexcept; // retrieves the data from the operating system's clipboard and assigns it to the clipboard_text variable inside your knst_window object, and you can retrieve the clipboard data with get_clipboard()

inline void knst_window::set_drag_drop_status(bool enabled) noexcept; // allows you to set the drag-and-drop status within your application

inline void knst_window::set_opacity(float opacity) noexcept; // sets the opacity of your application ! on Wayland it only sets the opacity of the title bar — you need to set the content's opacity yourself


inline void knst_window::set_attribute(int attribute, bool value) noexcept; // you can assign properties to the window — the first parameter is the macro, the second is whether that value will be on or off
#define KNST_WINDOW_ATTRIB_DECORATED 
#define KNST_WINDOW_ATTRIB_RESIZABLE
#define KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP
#define KNST_WINDOW_ATTRIB_TRANSPARENT

inline bool knst_window::get_attribute(int attribute) const noexcept; // takes the macros above as a parameter and returns the state of the property you gave in your window

inline void knst_window::set_minimum_size(int width, int height) noexcept; // you specify how small the window size can be at minimum

inline void knst_window::set_maximum_size(int width, int height) noexcept; // you specify how large the window size can be at maximum


KNST_FORCE_INLINE const knst_c16string& get_title() const noexcept; // gives you the window's title
KNST_FORCE_INLINE void set_user_data(void* data)noexcept; // allows you to assign your own custom data to the window
KNST_FORCE_INLINE const void* get_user_data()const noexcept; // allows you to retrieve your custom data from the window
KNST_FORCE_INLINE const bool& is_should_close()const noexcept; // indicates that your window needs to be closed
KNST_FORCE_INLINE void should_close() noexcept; // tells the window to close
KNST_FORCE_INLINE void clear_temporary_events() noexcept; // resets the events that need to be reset every loop
KNST_FORCE_INLINE const knst_window_event& get_window_event_handle() const noexcept; // returns the event object inside your window

template<typename Callback>
KNST_FORCE_INLINE void set_redraw_callback(Callback&& callback) noexcept; // allows you to assign a callback
KNST_FORCE_INLINE void set_redraw_callback(void (*callback)(knst_window&, void*)) noexcept; // allows you to assign a callback
KNST_FORCE_INLINE void call_redraw_callback() noexcept; // used to call the callback
KNST_FORCE_INLINE const knst_c16string& get_clipboard() const noexcept; // you retrieve the data copied into the clipboard from the window object
KNST_FORCE_INLINE void clear_clipboard() noexcept; // resets the clipboard data inside the window object
KNST_FORCE_INLINE const float& get_opacity() const noexcept; // returns the opacity value
KNST_FORCE_INLINE void set_title_bar_height(int height) noexcept; // used to set the title bar height
KNST_FORCE_INLINE int get_title_bar_height() const noexcept; // used to get the title bar height

// platform-specific objects, real example:
// you can also retrieve objects stored globally from within KnstWindowSources

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
|-------|----------|
| KNST_MOUSE_BUTTON_LEFT | Left mouse button |
| KNST_MOUSE_BUTTON_MIDDLE | Middle mouse button |
| KNST_MOUSE_BUTTON_RIGHT | Right mouse button |
| KNST_MOUSE_SCROLL_UP | Wheel up |
| KNST_MOUSE_SCROLL_DOWN | Wheel down |

___

## 2. MOUSE EVENTS

| Macro | Description |
|-------|----------|
| KNST_MOUSE_BUTTON_PRESS | Mouse button was pressed |
| KNST_MOUSE_BUTTON_RELEASE | Mouse button was released |
| KNST_MOUSE_SCROLL | Wheel moved |
| KNST_MOUSE_EVENT | Mouse event |
___

## 3. Keyboard Keys

## Keys (A-Z)

| Macro | Description |
|-------|----------|
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
|-------|----------|
| KNST_KEY_C_CEDILLA | Ç letter |
| KNST_KEY_G_BREVE | Ğ letter |
| KNST_KEY_I_DOTLESS | ı letter |
| KNST_KEY_O_DIAERESIS | Ö letter |
| KNST_KEY_S_CEDILLA | Ş letter |
| KNST_KEY_U_DIAERESIS | Ü letter |

## Numbers (0-9)

| Macro | Description |
|-------|----------|
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
|-------|----------|
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
|-------|----------|
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
|-------|----------|
| KNST_KEY_SHIFT | Shift key |
| KNST_KEY_CONTROL | Ctrl key |
| KNST_KEY_ALT | Alt key |
| KNST_KEY_SUPER | Windows/Command key |
| KNST_KEY_MENU | Menu key |

## Arrow Keys

| Macro | Description |
|-------|----------|
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
|-------|----------|
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
|-------|----------|
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
| KNST_KEY_LEFT_BRACE | { left curly brace |
| KNST_KEY_RIGHT_BRACE | } right curly brace |

## Media Keys

| Macro | Description |
|-------|----------|
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
|-------|----------|
| KNST_KEY_BROWSER_HOME | Browser home page |
| KNST_KEY_BROWSER_BACK | Browser back |
| KNST_KEY_BROWSER_FORWARD | Browser forward |
| KNST_KEY_BROWSER_REFRESH | Browser refresh |
| KNST_KEY_BROWSER_SEARCH | Browser search |
| KNST_KEY_BROWSER_FAVORITES | Browser favorites |

## Modifier Mask Keys

| Macro | Description |
|-------|----------|
| KNST_MOD_SHIFT | Shift key mask |
| KNST_MOD_CONTROL | Ctrl key mask |
| KNST_MOD_ALT | Alt key mask |
| KNST_MOD_SUPER | Super/Windows key mask |
| KNST_MOD_CAPS_LOCK | Caps Lock mask |
| KNST_MOD_NUM_LOCK | Num Lock mask |


___

## 4. KEYBOARD EVENTS

| Macro | Description |
|-------|----------|
| KNST_KEY_PRESS | Key was pressed |
| KNST_KEY_RELEASE | Key was released |
| KNST_KEY_REPEAT | Key was repeated |
| KNST_KEYBOARD_EVENT | Keyboard event |

___

## 5. WINDOW EVENTS

| Macro | Description |
|-------|----------|
| KNST_WINDOW_FULL_SCREEN | Full screen |
| KNST_WINDOW_RESTORE | Restore to previous state |
| KNST_MOTION_NOTIFY | Mouse moved |
| KNST_WINDOW_RESIZE | Window was resized |
| KNST_WINDOW_MOVE | Window was moved |
| KNST_CLOSE_WINDOW | Window is closing |
| KNST_WINDOW_MAXIMIZE | Maximize |
| KNST_WINDOW_MINIMIZE | Minimize |

## 6. FOCUS EVENTS

| Macro | Description |
|-------|----------|
| KNST_FOCUS_IN | Focus gained |
| KNST_FOCUS_OUT | Focus lost |
| KNST_ENTER_NOTIFY | Mouse entered the window |
| KNST_LEAVE_NOTIFY | Mouse left the window |


## 7. Other Events

| Macro | Description |
|-------|----------|
| KNST_UNKNOWN | Unknown event |
| KNST_EXPOSE | Redraw required |
| KNST_DISCONNECT | Connection lost |

## 8. CURSOR

| Macro | Description |
|-------|----------|
| KNST_CURSOR_NORMAL | Normal cursor |
| KNST_CURSOR_HIDDEN | Hidden cursor |
| KNST_CURSOR_DISABLED | Disabled cursor |

## 9. FILE DRAG AND DROP

| Macro | Description |
|-------|----------|
| KNST_FILE_DROP_ENTER | File entered the window |
| KNST_FILE_DROP_MOVE | File moved |
| KNST_FILE_DROP_LEAVE | File left the window |
| KNST_FILE_DROP | File was dropped |


## 10. WINDOW PROPERTIES

| Macro | Description |
|-------|----------|
| KNST_WINDOW_ATTRIB_DECORATED | Has a title bar |
| KNST_WINDOW_ATTRIB_RESIZABLE | Resizable |
| KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP | Always on top |
| KNST_WINDOW_ATTRIB_TRANSPARENT | Transparent window |



## 11. MOBILE APP STATUS

| Macro | Description |
|-------|----------|
| KNST_WINDOW_LOST | Window was lost |
| KNST_LOW_MEMORY | Low memory |
| KNST_APP_STARTED | Application started |
| KNST_APP_RESUMED | Application is resuming |
| KNST_APP_PAUSED | Application was paused |
| KNST_SAVE_STATE | State should be saved |
| KNST_CONTENT_RECT_CHANGED | Content area changed |
| KNST_CONFIG_CHANGED | Configuration changed |
| KNST_INPUT_CHANGED | Input method changed |
| KNST_APP_STOPPED | Application was stopped |


## 12. MOBILE TOUCH

| Macro | Description |
|-------|----------|
| KNST_MOBILE_TOUCH_EVENT | Touch event |


## 13. MOBILE SYSTEM KEYS

| Macro | Description |
|-------|----------|
| KNST_MOBILE_BACK_PRESS | Back key |
| KNST_MOBILE_HOME_PRESS | Home key |
| KNST_MOBILE_MENU_PRESS | Menu key |
| KNST_MOBILE_SEARCH_PRESS | Search key |
| KNST_MOBILE_VOLUME_UP | Volume up |
| KNST_MOBILE_VOLUME_DOWN | Volume down |
| KNST_MOBILE_APP_SWITCH | App switch |
| KNST_MOBILE_RECENT_APPS | Recent apps |
| KNST_MOBILE_VOLUME_MUTE | Silent mode |
| KNST_MOBILE_POWER | Power key |
| KNST_MOBILE_CAMERA | Camera key |
| KNST_MOBILE_HELP | Help key |
| KNST_MOBILE_SETTINGS | Settings key |
| KNST_MOBILE_SLEEP | Sleep mode |
| KNST_MOBILE_WAKEUP | Wake up |


## 14. MOBILE MEDIA KEYS

| Macro | Description |
|-------|----------|
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
|-------|----------|
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
|-------|----------|
| KNST_KEY_NUMPAD_COMMA | Numpad comma |
| KNST_KEY_NUMPAD_EQUALS | Numpad equals |
| KNST_KEY_NUMPAD_LEFT_PAREN | Numpad left parenthesis |
| KNST_KEY_NUMPAD_RIGHT_PAREN | Numpad right parenthesis |

## 17. MOBILE TOUCH ACTIONS

| Macro | Description |
|-------|----------|
| KNST_MOBILE_TOUCH_ACTION_PRESS | Finger pressed |
| KNST_MOBILE_TOUCH_ACTION_RELEASE | Finger released |
| KNST_MOBILE_TOUCH_ACTION_MOVE | Finger moved |
| KNST_MOBILE_TOUCH_ACTION_CANCEL | Event was cancelled |
| KNST_MOBILE_TOUCH_ACTION_OUTSIDE | Tapped outside the window |
| KNST_MOBILE_TOUCH_ACTION_POINTER_PRESS | Secondary finger pressed |
| KNST_MOBILE_TOUCH_ACTION_POINTER_RELEASE | Secondary finger released 


## 18. MOBILE SCREEN ORIENTATION

| Macro | Description |
|-------|----------|
| KNST_MOBILE_ORIENTATION_UNDEFINED | Undefined |
| KNST_MOBILE_ORIENTATION_PORTRAIT | Portrait mode |
| KNST_MOBILE_ORIENTATION_LANDSCAPE | Landscape mode |
| KNST_MOBILE_ORIENTATION_SQUARE | Square mode |

// May vary depending on the Android version 
## 19. MOBILE NIGHT MODE

| Macro | Description |
|-------|----------|
| KNST_MOBILE_NIGHT_MODE_OFF | Night mode off |
| KNST_MOBILE_NIGHT_MODE_ON | Night mode on |

## 20. DEFAULT

| Macro | Description |
|-------|----------|
| KNST_DEFAULT | Default/unassigned value state |