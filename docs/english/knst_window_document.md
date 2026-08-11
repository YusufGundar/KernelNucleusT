# knst_window — User Guide (English)

**A comprehensive cross-platform window management library for Windows and Linux.**

**Features:**
- **Performance and Size Balance — Optimized for both speed and binary size**
- **Modern C++ — Clean, readable, and modern code structure**
- **Event-Driven Architecture — Handle window events similar to operating system event loops**
- **Beta Stage — Security and stability are continuously improving**


    **Usage and Important Notes**
    — First, you must call KnstWindowSources::Init() at the beginning of your program to initialize the library. This prepares and initializes the static resources defined for the library.
    Additionally, this call internally retrieves the list of monitors based on the operating system, meaning it automatically calls knst_display::refresh_monitors() internally. Therefore, you don't need to call ::refresh_monitors() separately to read monitor information from knst_display. However, you can call it again if you need to refresh the list (e.g., after connecting a new monitor).


**About Event Handling**
  — Events that come to your window are stored in the event structure inside the window object. You can access it as const using the get_window_event_handle() function and check the structures within using switch-case and if-else statements.

  — event type'ları
    KNST_KEYBOARD_EVENT	   ==    Keyboard events
    KNST_MOUSE_EVENT	   ==    Mouse events
    KNST_WINDOW_RESIZE	   ==    Window resized
    KNST_WINDOW_MOVE	   ==    Window moved
    KNST_WINDOW_FULL_SCREEN	   ==    Fullscreen mode
    KNST_WINDOW_RESTORE	   ==    Window restored to previous state
    KNST_WINDOW_MAXIMIZE	   ==    Window maximized
    KNST_WINDOW_MINIMIZE	   ==    Window minimized
    KNST_FOCUS_IN / OUT	   ==    Window focus changed
    KNST_ENTER_NOTIFY / LEAVE_NOTIFY 	  ==     Mouse entered/left the window
    KNST_EXPOSE	   ==    Window needs to be redrawn
    KNST_FILE_DROP_...	   ==    Drag-and-drop events
    KNST_CLOSE_WINDOW	   ==    Window is closing
    KNST_DISCONNECT	   ==    Connection lost
    KNST_UNKNOWN	   ==    Undefined event

  — For example, for any event related to the W key:

  if(window.get_window_event_handle().type == KNST_KEYBOARD_EVENT){
  
    if(window.get_window_event_handle().key_code == KNST_KEY_W){
      
        // If events like PRESS, RELEASE, REPEAT come for the W key, 
        // this block is triggered. You can also check the action field 
        // of the event object to understand whether it's a press, 
        // release, or repeat.

    }

  }
  

**Loops**
    — There are 3 main loop types available in knst_window_event_system:
    — block_pool_event == triggers only when an event arrives
    — non_block_pool_event == processes the event if available, returns immediately if not
    — timeout_pool_event == waits for the specified duration, processes the event if available, returns immediately if not



**Functions**
  
void creation() noexcept Creates the native window resources (handle, surface, etc.).

__________________________________________________________________________________________
void show() noexcept Makes the created window visible.

__________________________________________________________________________________________
void creation_and_show() noexcept Calls creation() and show() in a single call.

__________________________________________________________________________________________
void destroy() noexcept Cleans up the window and all its resources.

__________________________________________________________________________________________
void should_close() noexcept Marks the window as ready to close.

__________________________________________________________________________________________
const bool& is_should_close() const noexcept Returns the window's close status.

__________________________________________________________________________________________
void set_title(const knst_c16string& title) noexcept Changes the window title.

__________________________________________________________________________________________
const knst_c16string& get_title() const noexcept Returns the current window title.

__________________________________________________________________________________________
void resize(int width = KNST_DEFAULT, int height = KNST_DEFAULT) noexcept Changes the window size.

__________________________________________________________________________________________
void move(int root_x, int root_y) noexcept Moves the window to the specified screen coordinates.

__________________________________________________________________________________________
void move(int root_x, int root_y, const knst_monitor& monitor) noexcept Moves the window according to the specified monitor.

__________________________________________________________________________________________
void toggle_fullscreen(bool fullscreen) noexcept Toggles fullscreen mode on/off.

__________________________________________________________________________________________
void set_minimized() noexcept Minimizes the window to the taskbar.

__________________________________________________________________________________________
void set_maximized() noexcept Maximizes the window to fill the screen.

__________________________________________________________________________________________
void restore() noexcept Restores the window to its previous (normal) state.

__________________________________________________________________________________________
void hide() noexcept Hides the window (makes it invisible).

__________________________________________________________________________________________
void focus() noexcept Attempts to focus the window.

__________________________________________________________________________________________
void set_opacity(float opacity) noexcept Sets the window opacity (0.0 - 1.0).

__________________________________________________________________________________________
const float& get_opacity() const noexcept Returns the current opacity value.

__________________________________________________________________________________________
void set_minimum_size(int width = KNST_DEFAULT, int height = KNST_DEFAULT) noexcept Sets the minimum window size.

__________________________________________________________________________________________
void set_maximum_size(int width = KNST_DEFAULT, int height = KNST_DEFAULT) noexcept Sets the maximum window size.

__________________________________________________________________________________________
void set_attribute(int attribute, bool value) noexcept Sets window attributes.

__________________________________________________________________________________________
bool get_attribute(int attribute) const noexcept Queries window attributes.

__________________________________________________________________________________________
void set_title_bar_height(int height) noexcept Sets the custom title bar height.

__________________________________________________________________________________________
int get_title_bar_height() const noexcept Returns the title bar height.

__________________________________________________________________________________________
void set_cursor(uint16_t cursor_type) noexcept Changes the mouse cursor (e.g., KNST_CURSOR_HAND).

__________________________________________________________________________________________
void set_bmp_cursor(const knst_byte_string& data, int width, int height, int hot_x = -1, int hot_y = -1) noexcept Creates a custom cursor from BMP data.

__________________________________________________________________________________________
void reset_cursor() noexcept Resets the cursor to its default state.

__________________________________________________________________________________________
void set_cursor_mode(int mode) noexcept Sets the cursor mode (NORMAL, HIDDEN, DISABLED).

__________________________________________________________________________________________
void set_cursor_pos_on_window(int x = KNST_DEFAULT, int y = KNST_DEFAULT) noexcept Moves the cursor to the specified position within the window.

__________________________________________________________________________________________
void set_cursor_pos_global(int root_x = KNST_DEFAULT, int root_y = KNST_DEFAULT) noexcept Moves the cursor to the specified screen coordinates.

__________________________________________________________________________________________
void set_clipboard(const knst_c16string& text) noexcept Copies text to the clipboard.

__________________________________________________________________________________________
void request_clipboard() noexcept Reads text from the clipboard and stores it in the clipboard_text variable.

__________________________________________________________________________________________
const knst_c16string& get_clipboard() const noexcept Returns the last read clipboard text.

__________________________________________________________________________________________
void clear_clipboard() noexcept Clears the clipboard text.

__________________________________________________________________________________________
void set_drag_drop_status(bool enabled) noexcept Enables/disables drag-and-drop functionality.

__________________________________________________________________________________________
void set_redraw_callback(void (*callback)(knst_window&, void*)) noexcept Sets the redraw callback (C function).

__________________________________________________________________________________________
void call_redraw_callback() noexcept Manually triggers the set callback.

__________________________________________________________________________________________
const knst_window_event& get_window_event_handle() const noexcept Returns a const reference to the event structure.

__________________________________________________________________________________________
void clear_temporary_events() noexcept Clears temporary events (e.g., KEY_REPEAT).

__________________________________________________________________________________________
void set_user_data(void* data) noexcept Sets user data.

__________________________________________________________________________________________
const void* get_user_data() const noexcept Returns the set user data.

__________________________________________________________________________________________


**Special Macros**

Window Feature Macros



KNST_DEFAULT--------------------------------------------------------->Default value (e.g. -10000)
KNST_DISABLE_TITLE_BAR--------------------------------------------------------->Disables title bar, enables custom drawing
KNST_DISABLE_REDRAW_ON_EVENT_MANAGER--------------------------------------------------------->Disables automatic redraw in event management

Window Attributes -- Passed as parameters to set_attribute()

KNST_WINDOW_ATTRIB_DECORATED
KNST_WINDOW_ATTRIB_RESIZABLE
KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP
KNST_WINDOW_ATTRIB_TRANSPARENT

Cursor Modes (Cursor Mode)



KNST_CURSOR_NORMAL--------------------------------------------------------->Normal cursor
KNST_CURSOR_HIDDEN--------------------------------------------------------->Hidden cursor
KNST_CURSOR_DISABLED --------------------------------------------------------->Disabled cursor (locked)

Cursor Types (Cursor Type)


KNST_CURSOR_ARROW --------------------------------------------------------->Arrow (default)
KNST_CURSOR_IBEAM--------------------------------------------------------->Text cursor (I-beam)
KNST_CURSOR_CROSSHAIR--------------------------------------------------------->Crosshair cursor
KNST_CURSOR_HAND--------------------------------------------------------->Hand cursor (clickable)
KNST_CURSOR_HRESIZE--------------------------------------------------------->Horizontal resize cursor
KNST_CURSOR_VRESIZE--------------------------------------------------------->Vertical resize cursor
KNST_CURSOR_MOVE--------------------------------------------------------->Move cursor
KNST_CURSOR_WAIT--------------------------------------------------------->Wait cursor
KNST_CURSOR_HELP--------------------------------------------------------->Help cursor
KNST_CURSOR_NOT_ALLOWED--------------------------------------------------------->Not allowed cursor

Keyboard Keys


KNST_KEY_A ... KNST_KEY_Z--------------------------------------------------------->Alphabet keys
KNST_KEY_0 ... KNST_KEY_9--------------------------------------------------------->Number keys
KNST_KEY_F1 ... KNST_KEY_F12--------------------------------------------------------->Function keys (F1-F12)
KNST_KEY_ESCAPE--------------------------------------------------------->Escape key
KNST_KEY_ENTER--------------------------------------------------------->Enter key
KNST_KEY_SPACE--------------------------------------------------------->Space key
KNST_KEY_BACKSPACE--------------------------------------------------------->Backspace key
KNST_KEY_TAB--------------------------------------------------------->Tab key
KNST_KEY_LEFT/RIGHT/UP/DOWN--------------------------------------------------------->Arrow keys
KNST_KEY_HOME/END/PAGE_UP/PAGE_DOWN --------------------------------------------------------->Navigation keys
KNST_KEY_INSERT/DELETE--------------------------------------------------------->Insert/Delete keys
KNST_KEY_SHIFT/CONTROL/ALT/SUPER--------------------------------------------------------->Modifier keys
KNST_KEY_CAPS_LOCK/NUM_LOCK/SCROLL_LOCK--------------------------------------------------------->Lock keys
KNST_KEY_VOLUME_*--------------------------------------------------------->Volume control keys
KNST_KEY_MEDIA_*--------------------------------------------------------->Media control keys
KNST_KEY_BROWSER_*--------------------------------------------------------->Browser keys
KNST_KEY_C_CEDILLA--------------------------------------------------------->Ç (Turkish)
KNST_KEY_G_BREVE--------------------------------------------------------->Ğ (Turkish)
KNST_KEY_I_DOTLESS--------------------------------------------------------->ı (Turkish)
KNST_KEY_O_DIAERESIS--------------------------------------------------------->Ö (Turkish)
KNST_KEY_S_CEDILLA--------------------------------------------------------->Ş (Turkish)
KNST_KEY_U_DIAERESIS--------------------------------------------------------->Ü (Turkish)

Modifier Keys (Mods)


KNST_MOD_SHIFT--------------------------------------------------------->Shift key pressed
KNST_MOD_CONTROL--------------------------------------------------------->Control key pressed
KNST_MOD_ALT Alt--------------------------------------------------------->key pressed
KNST_MOD_SUPER--------------------------------------------------------->Windows/Super key pressed
KNST_MOD_CAPS_LOCK--------------------------------------------------------->Caps Lock active
KNST_MOD_NUM_LOCK--------------------------------------------------------->Num Lock active
