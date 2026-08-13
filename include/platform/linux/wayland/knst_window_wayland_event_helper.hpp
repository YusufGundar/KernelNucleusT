#ifndef KNST_WINDOW_WAYLAND_EVENT_HELPER_HPP
#define KNST_WINDOW_WAYLAND_EVENT_HELPER_HPP

#pragma once
#include <linux/input-event-codes.h>



#if KNST_USING_LINUX_PLATFORM_WAYLAND


inline void knst_window_wayland_funcs::XdgSurfaceConfigure(
    void* data, xdg_surface* surface, uint32_t serial)
{
    xdg_surface_ack_configure(surface, serial);
}
inline const xdg_surface_listener
knst_window_wayland_funcs::xdgSurfaceListener =
{
    XdgSurfaceConfigure
};


inline void knst_window_wayland_funcs::XdgToplevelConfigure(
    void* data,
    xdg_toplevel* toplevel,
    int32_t w,
    int32_t h,
    wl_array* states)
{
    (void)toplevel;
    (void)states;

    auto* window = static_cast<knst_window*>(data);
    if (!window) return;

    if (w > 0 && h > 0) {
        window->m_knst_event.window_width = w;
        window->m_knst_event.window_height = h;
        window->m_knst_event.type = KNST_WINDOW_RESIZE;
    }

    bool maximized = false;
    bool activated = false;
    
    if (states && states->size > 0) {
        uint32_t* s = (uint32_t*)states->data;
        size_t count = states->size / sizeof(uint32_t);
        for (size_t i = 0; i < count; i++) {
            if (s[i] == XDG_TOPLEVEL_STATE_MAXIMIZED) maximized = true;
            if (s[i] == XDG_TOPLEVEL_STATE_ACTIVATED)  activated = true;
        }
    }

    if (maximized != window->m_knst_event.is_maximized) {
        window->m_knst_event.is_maximized = maximized;
        window->m_knst_event.type = maximized ? KNST_WINDOW_MAXIMIZE : KNST_WINDOW_RESTORE;
    }

    if (activated != window->m_knst_event.is_focused) {
        window->m_knst_event.is_focused = activated;
        window->m_knst_event.type = activated ? KNST_FOCUS_IN : KNST_FOCUS_OUT;
    }

    if (window->m_surface) {
        wl_surface_commit(window->m_surface);
    }
}

inline void knst_window_wayland_funcs::XdgToplevelClose(
    void* data,
    xdg_toplevel* toplevel)
{
    (void)toplevel;
    auto* window = static_cast<knst_window*>(data);
    if (!window) return;
    window->m_knst_event.type = KNST_CLOSE_WINDOW;     
}

inline void knst_window_wayland_funcs::XdgToplevelConfigureBounds(
    void* data,
    xdg_toplevel* toplevel,
    int32_t width,
    int32_t height)
{
    (void)data; (void)toplevel; (void)width; (void)height;
}

inline void knst_window_wayland_funcs::XdgToplevelWmCapabilities(
    void* data,
    xdg_toplevel* toplevel,
    wl_array* capabilities)
{
    (void)data; (void)toplevel; (void)capabilities;
}

inline const xdg_toplevel_listener
knst_window_wayland_funcs::xdgToplevelListener =
{
    XdgToplevelConfigure,
    XdgToplevelClose,
    XdgToplevelConfigureBounds,
    XdgToplevelWmCapabilities
};


inline void knst_window_wayland_funcs::UpdateCursor(
    wl_pointer* pointer,
    uint32_t serial,
    resize_edge edge)
{
    if (!pointer) return;

    wl_cursor* cursor = nullptr;

    switch (edge)
    {
        case resize_edge::left:
        case resize_edge::right:
            cursor = KnstWindowSources::cursor_ew; break;
        case resize_edge::top:
        case resize_edge::bottom:
            cursor = KnstWindowSources::cursor_ns; break;
        case resize_edge::top_left:
        case resize_edge::bottom_right:
            cursor = KnstWindowSources::cursor_nwse; break;
        case resize_edge::top_right:
        case resize_edge::bottom_left:
            cursor = KnstWindowSources::cursor_nesw; break;
        case resize_edge::none:
        default:
            cursor = KnstWindowSources::cursor_default; break;
    }

    if (!cursor || !cursor->images || !cursor->images[0] || !KnstWindowSources::cursor_surface)
        return;

    wl_cursor_image* image = cursor->images[0];
    wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) return;

    wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
    wl_surface_damage(KnstWindowSources::cursor_surface, 0, 0, image->width, image->height);
    wl_surface_commit(KnstWindowSources::cursor_surface);

    wl_pointer_set_cursor(pointer, serial, KnstWindowSources::cursor_surface, image->hotspot_x, image->hotspot_y);
}


inline void knst_window_wayland_funcs::PointerEnter(
    void* data,
    wl_pointer* pointer,
    uint32_t serial,
    wl_surface* surface,
    wl_fixed_t sx,
    wl_fixed_t sy)
{
    (void)data;

    knst_window* window = knst_window::find_from_surface(surface);
    if (!window) return;

    window->m_pointer_serial = serial;
    window->m_mouse_x = wl_fixed_to_double(sx);
    window->m_mouse_y = wl_fixed_to_double(sy);
    window->m_resize_edge = window->get_resize_edge(window->m_mouse_x, window->m_mouse_y);

    KnstWindowSources::active_window = window;

    auto& ev = window->m_knst_event;
    ev.type = KNST_ENTER_NOTIFY;
    ev.mouse_on_window = true;
    ev.mouse_x = wl_fixed_to_int(sx);

    #ifndef KNST_DISABLE_TITLE_BAR
        int raw_y = wl_fixed_to_int(sy);
        int titleBarH = window->get_title_bar_height();
        ev.mouse_y = (raw_y < titleBarH) ? 0 : (raw_y - titleBarH);
    #else
        ev.mouse_y = wl_fixed_to_int(sy);
    #endif

    if (window->m_resize_edge != resize_edge::none) {
        UpdateCursor(pointer, serial, window->m_resize_edge);
    }
    else if (window->m_using_custom_cursor && !window->m_pending_cursor_data.empty()) {
        window->apply_bmp_cursor_now(
            window->m_pending_cursor_data,
            window->m_pending_cursor_w,
            window->m_pending_cursor_h,
            window->m_pending_cursor_hot_x,
            window->m_pending_cursor_hot_y
        );
    }
    else {
        UpdateCursor(pointer, serial, resize_edge::none);
    }

    window->m_has_pending_cursor = false;
}

inline void knst_window_wayland_funcs::PointerLeave(
    void* data,
    wl_pointer* pointer,
    uint32_t serial,
    wl_surface* surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;

    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    window->m_resize_edge = resize_edge::none;
    window->m_pointer_pressed = false;

    auto& ev = window->m_knst_event;
    ev.type = KNST_LEAVE_NOTIFY;   
    ev.mouse_on_window = false;

    KnstWindowSources::active_window = nullptr;
}

inline void knst_window_wayland_funcs::PointerMotion(
    void* data,
    wl_pointer* pointer,
    uint32_t time,
    wl_fixed_t sx,
    wl_fixed_t sy)
{
    (void)data; (void)time;

    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    window->m_mouse_x = wl_fixed_to_double(sx);
    window->m_mouse_y = wl_fixed_to_double(sy);

    resize_edge old_edge = window->m_resize_edge;
    resize_edge new_edge = window->get_resize_edge(window->m_mouse_x, window->m_mouse_y);
    window->m_resize_edge = new_edge;

    if (old_edge != new_edge) {
        if (new_edge != resize_edge::none) {

            UpdateCursor(pointer, window->m_pointer_serial, new_edge);
        }
        else if (window->m_using_custom_cursor && !window->m_pending_cursor_data.empty()) {
           
            window->apply_bmp_cursor_now(
                window->m_pending_cursor_data,
                window->m_pending_cursor_w,
                window->m_pending_cursor_h,
                window->m_pending_cursor_hot_x,
                window->m_pending_cursor_hot_y
            );
        }
        else {
            UpdateCursor(pointer, window->m_pointer_serial, new_edge);
        }
    }

    auto& ev = window->m_knst_event;
    ev.type = KNST_MOTION_NOTIFY;
    ev.mouse_x = wl_fixed_to_int(sx);

    #ifndef KNST_DISABLE_TITLE_BAR
        int raw_y = wl_fixed_to_int(sy);
        int titleBarH = window->get_title_bar_height();
        ev.mouse_y = (raw_y < titleBarH) ? 0 : (raw_y - titleBarH);
    #else
        ev.mouse_y = wl_fixed_to_int(sy);
    #endif
}

inline void knst_window_wayland_funcs::PointerButton(
    void* data,
    wl_pointer* pointer,
    uint32_t serial,
    uint32_t time,
    uint32_t button,
    uint32_t state)
{
    (void)data; (void)pointer; (void)time;

    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    auto& ev = window->m_knst_event;

    int original_mx = (int)window->m_mouse_x;
    int original_my = (int)window->m_mouse_y;

    ev.type = KNST_MOUSE_EVENT;
    ev.mouse_x = original_mx;
    
    #ifndef KNST_DISABLE_TITLE_BAR
        int titleBarH = window->get_title_bar_height();
        ev.mouse_y = (original_my < titleBarH) ? 0 : (original_my - titleBarH);
    #else
        ev.mouse_y = original_my;
    #endif

    switch (button) {
        case BTN_LEFT: ev.mouse_button = KNST_MOUSE_BUTTON_LEFT; break;
        case BTN_MIDDLE: ev.mouse_button = KNST_MOUSE_BUTTON_MIDDLE; break;
        case BTN_RIGHT: ev.mouse_button = KNST_MOUSE_BUTTON_RIGHT; break;
        default: ev.mouse_button = button; break;
    }

    ev.mouse_action = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? 
                       KNST_MOUSE_BUTTON_PRESS : 
                       KNST_MOUSE_BUTTON_RELEASE;
                      
    if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        int mx = original_mx;
        int my = original_my;
        int width = ev.window_width;
        int titleBarH = window->get_title_bar_height();
        const int BUTTON_WIDTH = 48;
        const int CORNER_SIZE = 8;   

        if (my <= titleBarH && my >= 0) {

            if (mx >= width - BUTTON_WIDTH) {
                ev.type = KNST_CLOSE_WINDOW;
                window->should_close();
                return;
            }
            else if (mx >= width - BUTTON_WIDTH * 2) {
                if (ev.is_maximized) {
                    xdg_toplevel_unset_maximized(window->toplevel);
                    ev.is_maximized = false;
                    ev.type = KNST_WINDOW_RESTORE;
                } else {
                    xdg_toplevel_set_maximized(window->toplevel);
                    ev.is_maximized = true;
                    ev.type = KNST_WINDOW_MAXIMIZE;
                }
                return;
            }
            else if (mx >= width - BUTTON_WIDTH * 3) {
                ev.type = KNST_WINDOW_MINIMIZE;
                xdg_toplevel_set_minimized(window->toplevel);
                return;
            }
            else if (mx < CORNER_SIZE) {
                xdg_toplevel_resize(window->toplevel, KnstWindowSources::seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT);
                return;
            }
            else if (mx >= width - CORNER_SIZE) {
                xdg_toplevel_resize(window->toplevel, KnstWindowSources::seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT);
                return;
            }
            else {
                xdg_toplevel_move(window->toplevel, KnstWindowSources::seat, serial);
                return;
            }
        }

        resize_edge edge = window->m_resize_edge;
        if (edge != resize_edge::none) {
            xdg_toplevel_resize_edge wayland_edge;
            switch (edge) {
                case resize_edge::top: wayland_edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP; break;
                case resize_edge::bottom: wayland_edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM; break;
                case resize_edge::left: wayland_edge = XDG_TOPLEVEL_RESIZE_EDGE_LEFT; break;
                case resize_edge::right: wayland_edge = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT; break;
                case resize_edge::top_left: wayland_edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT; break;
                case resize_edge::top_right: wayland_edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT; break;
                case resize_edge::bottom_left: wayland_edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT; break;
                case resize_edge::bottom_right: wayland_edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT; break;
                default: return;
            }
            if (window->toplevel && KnstWindowSources::seat) {
                xdg_toplevel_resize(window->toplevel, KnstWindowSources::seat, serial, wayland_edge);
            }
            return;
        }
    }
}

inline void knst_window_wayland_funcs::PointerAxis(
    void* data,
    wl_pointer* pointer,
    uint32_t time,
    uint32_t axis,
    wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time;

    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
    {
        auto& ev = window->m_knst_event;
        ev.type = KNST_MOUSE_SCROLL;   
        ev.mouse_scroll_delta = wl_fixed_to_int(value);
    }
}

inline void knst_window_wayland_funcs::PointerFrame(void* data, wl_pointer* pointer) {
    (void)data; (void)pointer;
}
inline void knst_window_wayland_funcs::PointerAxisSource(void* data, wl_pointer* pointer, uint32_t axis_source) {
    (void)data; (void)pointer; (void)axis_source;
}
inline void knst_window_wayland_funcs::PointerAxisStop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis) {
    (void)data; (void)pointer; (void)time; (void)axis;
}
inline void knst_window_wayland_funcs::PointerAxisDiscrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete) {
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

inline const wl_pointer_listener
knst_window_wayland_funcs::pointerListener =
{
    PointerEnter,
    PointerLeave,
    PointerMotion,
    PointerButton,
    PointerAxis,
    PointerFrame,
    PointerAxisSource,
    PointerAxisStop,
    PointerAxisDiscrete
};


inline void knst_window_wayland_funcs::SeatCapabilities(
    void* data,
    wl_seat* seat,
    uint32_t capabilities)
{
    (void)data;

    if (capabilities & WL_SEAT_CAPABILITY_POINTER)
    {
        if (!KnstWindowSources::pointer)
        {
            KnstWindowSources::pointer = wl_seat_get_pointer(seat);
            if (KnstWindowSources::pointer)
                wl_pointer_add_listener(KnstWindowSources::pointer, &pointerListener, nullptr);
        }
    }
    else
    {
        if (KnstWindowSources::pointer)
        {
            wl_pointer_destroy(KnstWindowSources::pointer);
            KnstWindowSources::pointer = nullptr;
            KnstWindowSources::active_window = nullptr;
        }
    }

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
    {
        if (!KnstWindowSources::keyboard)
        {
            KnstWindowSources::keyboard = wl_seat_get_keyboard(seat);
            if (KnstWindowSources::keyboard)
                wl_keyboard_add_listener(KnstWindowSources::keyboard, &keyboardListener, nullptr);
        }
    }
    else
    {
        if (KnstWindowSources::keyboard)
        {
            wl_keyboard_destroy(KnstWindowSources::keyboard);
            KnstWindowSources::keyboard = nullptr;
            KnstWindowSources::keyboard_focus_window = nullptr;
        }
    }
}

inline void knst_window_wayland_funcs::SeatName(
    void* data,
    wl_seat* seat,
    const char* name)
{
    (void)data; (void)seat; (void)name;
}

inline const wl_seat_listener
knst_window_wayland_funcs::seatListener =
{
    SeatCapabilities,
    SeatName
};


inline void knst_window_wayland_funcs::KeyboardEnter(
    void* data,
    wl_keyboard* keyboard,
    uint32_t serial,
    wl_surface* surface,
    wl_array* keys)
{
    (void)data; (void)keyboard; (void)serial; (void)keys;

    KnstWindowSources::keyboard_serial = serial;

    knst_window* window = knst_window::find_from_surface(surface);
    if (!window) return;

    window->m_knst_event.type = KNST_FOCUS_IN;          
    window->m_knst_event.is_focused = true;
    KnstWindowSources::keyboard_focus_window = window;
}

inline void knst_window_wayland_funcs::KeyboardLeave(
    void* data,
    wl_keyboard* keyboard,
    uint32_t serial,
    wl_surface* surface)
{
    (void)data; (void)keyboard; (void)serial; (void)surface;

    auto* window = KnstWindowSources::keyboard_focus_window;
    if (!window) return;

    window->m_knst_event.type = KNST_FOCUS_OUT;         
    window->m_knst_event.is_focused = false;
    KnstWindowSources::keyboard_focus_window = nullptr;
}

inline void knst_window_wayland_funcs::KeyboardKeymap(
    void* data,
    wl_keyboard* keyboard,
    uint32_t format,
    int fd,
    uint32_t size)
{
    (void)data; (void)keyboard;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
        close(fd);
        return;
    }

    char* map_str = (char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_str == MAP_FAILED)
    {
        close(fd);
        return;
    }

    if (KnstWindowSources::xkb_map)
        xkb_keymap_unref(KnstWindowSources::xkb_map);

    KnstWindowSources::xkb_map = xkb_keymap_new_from_string(
        KnstWindowSources::xkb_ctx,
        map_str,
        XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );

    munmap(map_str, size);
    close(fd);

    if (KnstWindowSources::xkb_st)
        xkb_state_unref(KnstWindowSources::xkb_st);

    if (KnstWindowSources::xkb_map)
        KnstWindowSources::xkb_st = xkb_state_new(KnstWindowSources::xkb_map);
}

inline void knst_window_wayland_funcs::KeyboardKey(
    void* data,
    wl_keyboard* keyboard,
    uint32_t serial,
    uint32_t time,
    uint32_t key,
    uint32_t state)
{
    (void)data; (void)keyboard; (void)serial;

    auto* window = KnstWindowSources::keyboard_focus_window;
    if (!window) return;

    uint32_t keycode = key + 8;
    uint32_t scancode = key;
    uint32_t current_time = KnstWindowSources::get_current_time_ms();

    if (KnstWindowSources::xkb_st)
    {
        xkb_state_update_key(
            KnstWindowSources::xkb_st,
            keycode,
            (state == WL_KEYBOARD_KEY_STATE_PRESSED) ? XKB_KEY_DOWN : XKB_KEY_UP
        );

        keycode = xkb_state_key_get_one_sym(KnstWindowSources::xkb_st, keycode);
        
        if (keycode >= XKB_KEY_a && keycode <= XKB_KEY_z) {
            keycode -= 32; 
        }
        else if (keycode == XKB_KEY_udiaeresis) {
            keycode = XKB_KEY_Udiaeresis;
        }
        else if (keycode == XKB_KEY_odiaeresis) {
            keycode = XKB_KEY_Odiaeresis;
        }
        else if (keycode == XKB_KEY_ccedilla) {
            keycode = XKB_KEY_Ccedilla;
        }
        else if (keycode == XKB_KEY_scedilla) {
            keycode = XKB_KEY_Scedilla;
        }
        else if (keycode == XKB_KEY_gbreve) {
            keycode = XKB_KEY_Gbreve;
        }
        else if (keycode == XKB_KEY_idotless) {
            keycode = XKB_KEY_I;
        }
    }

    auto& ev = window->m_knst_event;
    
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        if (ev.m_last_scancode == scancode && ev.m_key_held) {
            ev.key_action = KNST_KEY_REPEAT;
            ev.m_last_repeat_time = current_time;
        } 
        else {
            ev.key_action = KNST_KEY_PRESS;
            ev.m_last_key = keycode;
            ev.m_last_scancode = scancode;
            ev.m_key_held = true;
            ev.m_last_key_time = current_time;
            ev.m_last_repeat_time = current_time;
            ev.m_repeat_initialized = false;
        }
        
        ev.type = KNST_KEYBOARD_EVENT;
        ev.key_code = keycode;
        ev.scancode = scancode;
    }
    else
    {
        if (ev.m_last_scancode == scancode) {
            ev.type = KNST_KEYBOARD_EVENT;
            ev.key_code = keycode;
            ev.scancode = scancode;
            ev.key_action = KNST_KEY_RELEASE;
            
            ev.m_key_held = false;
            ev.m_last_key = 0;
            ev.m_last_scancode = 0;
            ev.m_last_key_time = 0;
            ev.m_last_repeat_time = 0;
            ev.m_repeat_initialized = false;
        }
    }
}

inline void knst_window_wayland_funcs::KeyboardModifiers(
    void* data,
    wl_keyboard* keyboard,
    uint32_t serial,
    uint32_t mods_depressed,
    uint32_t mods_latched,
    uint32_t mods_locked,
    uint32_t group)
{
    (void)data; (void)keyboard; (void)serial;

    auto* window = KnstWindowSources::keyboard_focus_window;

    int mods = 0;
    if (mods_depressed & 1)  mods |= KNST_MOD_SHIFT;
    if (mods_depressed & 4)  mods |= KNST_MOD_CONTROL;
    if (mods_depressed & 8)  mods |= KNST_MOD_ALT;
    if (mods_depressed & 64) mods |= KNST_MOD_SUPER;
    if (mods_locked & 2)     mods |= KNST_MOD_CAPS_LOCK;
    if (mods_locked & 16)    mods |= KNST_MOD_NUM_LOCK;

    if (window)
        window->m_knst_event.mods = mods;

    if (KnstWindowSources::xkb_st)
        xkb_state_update_mask(KnstWindowSources::xkb_st, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

inline void knst_window_wayland_funcs::KeyboardRepeatInfo(
    void* data,
    wl_keyboard* keyboard,
    int32_t rate,
    int32_t delay)
{
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

inline const wl_keyboard_listener
knst_window_wayland_funcs::keyboardListener =
{
    KeyboardKeymap,
    KeyboardEnter,
    KeyboardLeave,
    KeyboardKey,
    KeyboardModifiers,
    KeyboardRepeatInfo
};


inline void knst_window_wayland_funcs::WmBasePing(void* data, struct xdg_wm_base* base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(base, serial);
}

inline const xdg_wm_base_listener knst_window_wayland_funcs::wmBaseListener =
{
    WmBasePing
};


inline void knst_window_wayland_funcs::DataSourceTarget(void*, struct wl_data_source*, const char*) {}

inline void knst_window_wayland_funcs::DataSourceSend(void*, struct wl_data_source*, const char* mime_type, int32_t fd) {
    if (strcmp(mime_type, "text/plain;charset=utf-8") == 0 || strcmp(mime_type, "text/plain") == 0) {
        knst_byte_string data(KnstWindowSources::clipboard_pending_text);
        ssize_t written = 0;
        const uint8_t* buf = data.data();
        size_t len = data.length();
        while (written < (ssize_t)len) {
            ssize_t n = write(fd, buf + written, len - written);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            written += n;
        }
    }
    close(fd);
}

inline void knst_window_wayland_funcs::DataSourceCancelled(void*, struct wl_data_source* source) {
    if (source == KnstWindowSources::clipboard_source) {
        wl_data_source_destroy(source);
        KnstWindowSources::clipboard_source = nullptr;
    }
}

inline void knst_window_wayland_funcs::DataSourceDndDropPerformed(void*, struct wl_data_source*) {}
inline void knst_window_wayland_funcs::DataSourceDndFinished(void*, struct wl_data_source*) {}
inline void knst_window_wayland_funcs::DataSourceAction(void*, struct wl_data_source*, uint32_t) {}

const wl_data_source_listener knst_window_wayland_funcs::dataSourceListener = {
    DataSourceTarget,
    DataSourceSend,
    DataSourceCancelled,
    DataSourceDndDropPerformed,
    DataSourceDndFinished,
    DataSourceAction
};

inline void knst_window_wayland_funcs::DataOfferOffer(void* data, struct wl_data_offer* offer, const char* mime_type) {
    if (offer == KnstWindowSources::pending_offer) { 
        if (strcmp(mime_type, "text/uri-list") == 0) {
            KnstWindowSources::pending_offer_has_uri_list = true;
        }
    }
}
inline void knst_window_wayland_funcs::DataOfferSourceActions(void*, struct wl_data_offer*, uint32_t) {}
inline void knst_window_wayland_funcs::DataOfferAction(void*, struct wl_data_offer*, uint32_t) {}

const wl_data_offer_listener knst_window_wayland_funcs::dataOfferListener = {
    DataOfferOffer,
    DataOfferSourceActions,
    DataOfferAction
};


static void knst_parse_uri_list_wayland(const std::string& uriList, knst_vector<knst_c16string>& out) {
    size_t pos = 0;
    while (pos < uriList.length()) {
        size_t end = uriList.find('\n', pos);
        if (end == std::string::npos) end = uriList.length();

        std::string uri = uriList.substr(pos, end - pos);
        if (!uri.empty() && uri.back() == '\r') uri.pop_back();

        if (uri.find("file://") == 0) {
            std::string path = uri.substr(7);
            for (size_t i = 0; i < path.length(); i++) {
                if (path[i] == '%' && i + 2 < path.length()) {
                    int hex;
                    sscanf(path.substr(i + 1, 2).c_str(), "%x", &hex);
                    path.replace(i, 3, 1, (char)hex);
                }
            }
            out.push_back(knst_c16string(path.c_str()));
        }
        pos = end + 1;
    }
}


inline void knst_window_wayland_funcs::DataDeviceDataOffer(void*, struct wl_data_device*, struct wl_data_offer* offer) {
    if (offer) {
        wl_data_offer_add_listener(offer, &dataOfferListener, nullptr);
    
        KnstWindowSources::pending_offer = offer;
        KnstWindowSources::pending_offer_has_uri_list = false;
    }
}

inline void knst_window_wayland_funcs::DataDeviceEnter(
    void*, struct wl_data_device*, uint32_t serial,
    struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y, 
    struct wl_data_offer* offer)
{
   
    if (KnstWindowSources::drag_offer && 
        KnstWindowSources::drag_offer != offer) 
    {
        wl_data_offer_destroy(KnstWindowSources::drag_offer);
        KnstWindowSources::drag_offer = nullptr;
    }
    if (KnstWindowSources::drag_target_window) {
        KnstWindowSources::drag_target_window->m_knst_event.type = KNST_FILE_DROP_LEAVE;
        KnstWindowSources::drag_target_window->m_knst_event.drop_files.clear();
        KnstWindowSources::drag_target_window->m_knst_event.drop_count = 0;
    }
    KnstWindowSources::drag_target_window = nullptr;
    KnstWindowSources::drag_offer_has_uri_list = false;
    knst_window* window = knst_window::find_from_surface(surface);
    if (!window) return;

    if (!window->m_drag_drop_enabled) {
       
        if (offer) {
            wl_data_offer_destroy(offer);
        }
       
        if (offer == KnstWindowSources::pending_offer) {
            KnstWindowSources::pending_offer = nullptr;
            KnstWindowSources::pending_offer_has_uri_list = false;
        }
        return;
    }

    if (KnstWindowSources::drag_offer && KnstWindowSources::drag_offer != offer) {
        wl_data_offer_destroy(KnstWindowSources::drag_offer);
    }

    KnstWindowSources::drag_offer = offer;

    if (offer == KnstWindowSources::pending_offer) {
        KnstWindowSources::drag_offer_has_uri_list = KnstWindowSources::pending_offer_has_uri_list;
    } else {
        KnstWindowSources::drag_offer_has_uri_list = false;
    }

    KnstWindowSources::drag_target_window = window;
    KnstWindowSources::drag_enter_serial = serial;

    if (offer) {
        wl_data_offer_set_actions(offer,
            WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
            WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        wl_data_offer_accept(offer, serial, "text/uri-list");
    }

    window->m_knst_event.type = KNST_FILE_DROP_ENTER;
    window->m_knst_event.drop_files.clear();
    window->m_knst_event.drop_count = 0;
    window->m_knst_event.mouse_x = wl_fixed_to_int(x);
    window->m_knst_event.mouse_y = wl_fixed_to_int(y);
}

inline void knst_window_wayland_funcs::DataDeviceLeave(void*, struct wl_data_device*) {
   
    
    auto* window = KnstWindowSources::drag_target_window;
    if (window) {
        window->m_knst_event.type = KNST_FILE_DROP_LEAVE;
        window->m_knst_event.drop_files.clear();
        window->m_knst_event.drop_count = 0;
    }
    if (KnstWindowSources::drag_offer) { 
        wl_data_offer_destroy(KnstWindowSources::drag_offer);
        KnstWindowSources::drag_offer = nullptr;
    }
    KnstWindowSources::drag_offer_has_uri_list = false;
    KnstWindowSources::drag_target_window = nullptr; 
}



inline void knst_window_wayland_funcs::DataDeviceMotion(
    void*, struct wl_data_device*, uint32_t, wl_fixed_t x, wl_fixed_t y) 
{
    auto* window = KnstWindowSources::drag_target_window;
    if (!window) return;
    
    window->m_knst_event.type = KNST_FILE_DROP_MOVE;
    window->m_knst_event.mouse_x = wl_fixed_to_int(x);
    window->m_knst_event.mouse_y = wl_fixed_to_int(y);
}

inline void knst_window_wayland_funcs::DataDeviceDrop(void*, struct wl_data_device*) {
   
    
    auto* window = KnstWindowSources::drag_target_window;
    if (!window) {
        return;
    }
    
    
    struct wl_data_offer* local_offer = KnstWindowSources::drag_offer;
    if (!local_offer) {
        return;
    }

    bool has_uri_list = KnstWindowSources::drag_offer_has_uri_list;

    
    KnstWindowSources::drag_offer = nullptr;
    KnstWindowSources::drag_offer_has_uri_list = false;

    if (!has_uri_list) {
        
        wl_data_offer_finish(local_offer);
        wl_data_offer_destroy(local_offer);
        KnstWindowSources::drag_target_window = nullptr;
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
       
        wl_data_offer_destroy(local_offer);
        return;
    }

   
    wl_data_offer_receive(local_offer, "text/uri-list", pipefd[1]);
    close(pipefd[1]);

  
    wl_display_roundtrip(KnstWindowSources::wayland_display);

    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        result.append(buf, n);
      
    }
    close(pipefd[0]);

   

    knst_vector<knst_c16string> files;
    knst_parse_uri_list_wayland(result, files);
  

    window->m_knst_event.drop_files = files;
    window->m_knst_event.drop_count = files.size();
    window->m_knst_event.type = KNST_FILE_DROP;
   

  
    wl_data_offer_finish(local_offer);
    wl_data_offer_destroy(local_offer);
    
    KnstWindowSources::drag_target_window = nullptr;
}

inline void knst_window_wayland_funcs::DataDeviceSelection(void*, struct wl_data_device*, struct wl_data_offer* offer) {
    
    if (KnstWindowSources::selection_offer) {
        wl_data_offer_destroy(KnstWindowSources::selection_offer);
    }
    KnstWindowSources::selection_offer = offer;
}

const wl_data_device_listener knst_window_wayland_funcs::dataDeviceListener = {
    DataDeviceDataOffer,
    DataDeviceEnter,
    DataDeviceLeave,
    DataDeviceMotion,
    DataDeviceDrop,
    DataDeviceSelection
};




#endif 
#endif // KNST_WINDOW_WAYLAND_EVENT_HELPER_HPP