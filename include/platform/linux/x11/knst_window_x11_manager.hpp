// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_x11_manager.hpp
----------------------------

    Function definitions for the X11 backend.

*/

#pragma once

#if KNST_USING_LINUX_PLATFORM_X11

#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XInput2.h>

#include <unistd.h>   // for getpid()

#include <X11/Xutil.h>
#include <xcb/xcb_icccm.h>
#include <xcb/randr.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>


// ─── Cursor cache ──────────────────────────────────────────────────────────
// XCreateFontCursor is expensive (reads theme files).
// XcursorLibraryLoadCursor loads themed cursors and isn't free either.
// Cache both by (dpy, shape) and (dpy, name) for the process lifetime.
namespace knst_x11_cursor_cache {
    struct entry {
        Display* dpy;
        unsigned int shape;  // 0 when name-based
        char name[64];   // empty when shape-based
        Cursor cur;
    };

    inline knst_vector<entry>& store() noexcept {
        static knst_vector<entry> s;
        return s;
    }

    inline Cursor get_font(Display* dpy, unsigned int shape) noexcept {
        for (auto& e : store()) {
            if (e.dpy == dpy && e.shape == shape && e.name[0] == '\0')
                return e.cur;
        }
        Cursor c = XCreateFontCursor(dpy, shape);
        if (c) {
            entry e{};
            e.dpy   = dpy;
            e.shape = shape;
            e.name[0] = '\0';
            e.cur   = c;
            store().push_back(e);
        }
        return c;
    }

    inline Cursor get_named(Display* dpy, const char* name) noexcept {
        for (auto& e : store()) {
            if (e.dpy == dpy && strcmp(e.name, name) == 0)
                return e.cur;
        }
        Cursor c = XcursorLibraryLoadCursor(dpy, name);
        if (c) {
            entry e{};
            e.dpy   = dpy;
            e.shape = 0;
            strncpy(e.name, name, sizeof(e.name) - 1);
            e.name[sizeof(e.name) - 1] = '\0';   // always NUL-terminated
            e.cur   = c;
            store().push_back(e);
        }
        return c;
    }

    // Release every cached cursor BEFORE XCloseDisplay. Otherwise the
    // cached Cursor handles dangle after the display closes and cause UB
    // on the next Init cycle.
    inline void clear_all() noexcept {
        for (auto& e : store()) {
            if (e.cur && e.dpy) {
                XFreeCursor(e.dpy, e.cur);
            }
        }
        store().clear();
    }
}   // namespace knst_x11_cursor_cache


inline void knst_window::KNST_X11_METHOD(creation)() noexcept {

    const xcb_setup_t* setup = xcb_get_setup(KnstWindowSources::m_connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    xcb_screen_t* screen = iter.data;

    m_window = xcb_generate_id(KnstWindowSources::m_connection);

    uint32_t mask = XCB_CW_BACK_PIXMAP | XCB_CW_EVENT_MASK;
    uint32_t values[2] = {
        XCB_BACK_PIXMAP_NONE,
        XCB_EVENT_MASK_KEY_PRESS |
        XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_LEAVE_WINDOW |
        XCB_EVENT_MASK_EXPOSURE |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_FOCUS_CHANGE |
        XCB_EVENT_MASK_PROPERTY_CHANGE |
        XCB_EVENT_MASK_VISIBILITY_CHANGE
    };

    // Create the window. All WM properties must be set AFTER the
    // window exists — before that, the X server sees an invalid ID and
    // silently rejects the property calls.
    //
    // Don't pass KNST_WINDOW_DEFAULT (-10000) straight through. Without
    // a WM, the window would end up off-screen.
    const int init_x = (m_knst_event.window_root_x == KNST_WINDOW_DEFAULT) ? 0 : m_knst_event.window_root_x;
                        
    const int init_y = (m_knst_event.window_root_y == KNST_WINDOW_DEFAULT) ? 0 : m_knst_event.window_root_y;
                        

    xcb_create_window(
        KnstWindowSources::m_connection,
        XCB_COPY_FROM_PARENT,
        m_window,
        screen->root,
        (int16_t)init_x, (int16_t)init_y,
        m_knst_event.window_width, m_knst_event.window_height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        mask,
        values
    );

    // WM_CLASS — required for task-bar grouping.
    // Format: "instance\0class\0" (two null-terminated strings).
    {
        static const char wm_class[] = "KernelNucleusT\0KnstWindow\0";
        xcb_change_property(
            KnstWindowSources::m_connection,
            XCB_PROP_MODE_REPLACE,
            m_window,
            XCB_ATOM_WM_CLASS,
            XCB_ATOM_STRING,
            8, sizeof(wm_class) - 1, wm_class
        );
    }

    // _NET_WM_PID — lets the task bar match window to process.
    {
        // "_NET_WM_PID" is 11 chars. Passing 12 includes the NUL — wrong
        // atom.
        xcb_intern_atom_cookie_t pid_cookie = xcb_intern_atom(
            KnstWindowSources::m_connection, 0, 11, "_NET_WM_PID");
        xcb_intern_atom_reply_t* pid_reply = xcb_intern_atom_reply(
            KnstWindowSources::m_connection, pid_cookie, nullptr);
        if (pid_reply) {
            uint32_t pid = (uint32_t)getpid();
            xcb_change_property(
                KnstWindowSources::m_connection,
                XCB_PROP_MODE_REPLACE,
                m_window,
                pid_reply->atom,
                XCB_ATOM_CARDINAL,
                32, 1, &pid
            );
            free(pid_reply);
        }
    }

    // WM_NAME (legacy) + _NET_WM_NAME (modern UTF-8).
    // WM_NAME must use XA_STRING, _NET_WM_NAME must use UTF8_STRING.
    knst_byte_string title_utf8(m_title);

    xcb_change_property(
        KnstWindowSources::m_connection,
        XCB_PROP_MODE_REPLACE,
        m_window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8, title_utf8.length(), title_utf8.data()
    );

    {
        xcb_intern_atom_cookie_t name_cookie = xcb_intern_atom(
            KnstWindowSources::m_connection, 0, 12, "_NET_WM_NAME");
        xcb_intern_atom_reply_t* name_reply = xcb_intern_atom_reply(
            KnstWindowSources::m_connection, name_cookie, nullptr);
        if (name_reply) {
            xcb_change_property(
                KnstWindowSources::m_connection,
                XCB_PROP_MODE_REPLACE,
                m_window,
                name_reply->atom,
                KnstWindowSources::m_UTF8_STRING,
                8, title_utf8.length(), title_utf8.data()
            );
            free(name_reply);
        }
    }

    // _NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_NORMAL.
    // Tells the WM this is a normal application window (not a popup).
    {
        xcb_intern_atom_cookie_t type_cookie = xcb_intern_atom(KnstWindowSources::m_connection, 0, 19, "_NET_WM_WINDOW_TYPE");
            
        xcb_intern_atom_reply_t* type_reply = xcb_intern_atom_reply(KnstWindowSources::m_connection, type_cookie, nullptr);
            

        xcb_intern_atom_cookie_t normal_cookie = xcb_intern_atom(KnstWindowSources::m_connection, 0, 26, "_NET_WM_WINDOW_TYPE_NORMAL");
            
        xcb_intern_atom_reply_t* normal_reply = xcb_intern_atom_reply(KnstWindowSources::m_connection, normal_cookie, nullptr);
            

        if (type_reply && normal_reply) {
            xcb_change_property(
                KnstWindowSources::m_connection,
                XCB_PROP_MODE_REPLACE,
                m_window,
                type_reply->atom,
                XCB_ATOM_ATOM,
                32, 1, &normal_reply->atom
            );
        }
        if (type_reply) free(type_reply);
        if (normal_reply) free(normal_reply);
    }

    // WM_PROTOCOLS — WM_DELETE_WINDOW + _NET_WM_SYNC_REQUEST.
    {
        xcb_intern_atom_cookie_t protocols_cookie = xcb_intern_atom(KnstWindowSources::m_connection, 0, 12, "WM_PROTOCOLS");
        xcb_intern_atom_reply_t* protocols_reply = xcb_intern_atom_reply(KnstWindowSources::m_connection, protocols_cookie, nullptr);
        if (protocols_reply) {
            xcb_atom_t protocols[2] = {
                KnstWindowSources::m_wmDelete,
                KnstWindowSources::m_wmSyncRequest
            };
            xcb_change_property(
                KnstWindowSources::m_connection,
                XCB_PROP_MODE_REPLACE,
                m_window,
                protocols_reply->atom,
                XCB_ATOM_ATOM,
                32, 2, protocols
            );
            free(protocols_reply);
        }
    }

    // WM_NORMAL_HINTS — some compositors use this when restoring.
    {
        xcb_size_hints_t sh = {};
        xcb_icccm_size_hints_set_size(
            &sh, 0,
            m_knst_event.window_width,
            m_knst_event.window_height);
        xcb_icccm_set_wm_normal_hints(
            KnstWindowSources::m_connection,
            m_window,
            &sh);
    }

    // Custom title bar — _MOTIF_WM_HINTS (runtime).
    // CSD on → strip the WM decoration.
    if (is_csd_enabled()) {
        struct KnstMotifWmHints {
            uint32_t flags;
            uint32_t functions;
            uint32_t decorations;
            int32_t input_mode;
            uint32_t status;
        };
        KnstMotifWmHints motif_hints{};
        motif_hints.flags = 2;
        motif_hints.decorations = 0;

        xcb_change_property(
            KnstWindowSources::m_connection,
            XCB_PROP_MODE_REPLACE,
            m_window,
            KnstWindowSources::m_MOTIF_WM_HINTS,
            KnstWindowSources::m_MOTIF_WM_HINTS,
            32, 5, &motif_hints
        );
        m_draw_custom_title_bar = true;
    }

    {
        xcb_sync_int64_t initial{0, 0};
        m_syncCounter = xcb_generate_id(KnstWindowSources::m_connection);
        xcb_sync_create_counter(KnstWindowSources::m_connection, m_syncCounter, initial);

        xcb_change_property(
            KnstWindowSources::m_connection,
            XCB_PROP_MODE_REPLACE,
            m_window,
            KnstWindowSources::m_wmSyncRequestCounter,
            XCB_ATOM_CARDINAL,
            32, 1, &m_syncCounter
        );
    }

    // X Input Context for text input (Xutf8LookupString).
    // XIM is global (opened during Init); only the XIC is per-window.
    // Opening a new XIM per window leaked handles and IME ignored the
    // default locale.
    if (KnstWindowSources::m_xim && KnstWindowSources::m_xlib_display) {
        m_xic = XCreateIC(
            KnstWindowSources::m_xim,
            XNInputStyle, XIMStatusNothing | XIMPreeditNothing,
            XNClientWindow, m_window,
            XNFocusWindow, m_window,
            nullptr);
    }

    // XInput2 — raw mouse + touch (XCB wire format).
    //
    // Why XCB: the X server delivers XI2 events as XCB_GE_GENERIC
    // (response_type 35 + first_event). The Xlib APIs (XISelectEvents,
    // XIQueryDevice) work for selection but not for receiving — the
    // event pump is XCB. Selecting via Xlib without a case to catch
    // XCB_GE_GENERIC means XI2 is dead code.
    //
    // RawMotion arrives at the ROOT window (no window field), touch
    // arrives at the specific window. The event pump routes accordingly.
    if (KnstWindowSources::m_xi_opcode >= 0) {
        // Touch detection (Xlib query works here).
        int ndevices = 0;
        XIDeviceInfo* devices = XIQueryDevice(
            KnstWindowSources::m_xlib_display, XIAllDevices, &ndevices);

        if (devices) {
            for (int i = 0; i < ndevices; i++) {
                for (int c = 0; c < devices[i].num_classes; c++) {
                    if (devices[i].classes[c]->type == XITouchClass) {
                        XITouchClassInfo* tc =
                            (XITouchClassInfo*)devices[i].classes[c];
                        // Only real touchscreens. XIDependentTouch is for
                        // touchpads — don't count it as "touchscreen", or
                        // touchpad fingers get reported as touch.
                        if (tc->mode != XIDirectTouch) continue;
                        m_touch_supported = true;
                        if (tc->num_touches > m_max_touch_points) {
                            m_max_touch_points = tc->num_touches;
                        }
                    }
                }
            }
            XIFreeDeviceInfo(devices);
        }

        // XI2 event selection via XCB. Touch events on THIS window,
        // RawMotion on the root window.
        {
            const uint32_t xi_mask_words =
                (XCB_INPUT_TOUCH_END + 8 + 31) / 32;   // up to XI_TouchEnd
            const uint32_t xi_mask_bits = 4 * xi_mask_words;

            struct {
                xcb_input_event_mask_t head;
                uint32_t mask[8];
            } xcb_mask = {};

            xcb_mask.head.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
            xcb_mask.head.mask_len = xi_mask_words;

            #define KNST_SET_MASK(arr, ev) \
                (arr[(ev) / 32] |= (1u << ((ev) % 32)))

            KNST_SET_MASK(xcb_mask.mask, XCB_INPUT_TOUCH_BEGIN);
            KNST_SET_MASK(xcb_mask.mask, XCB_INPUT_TOUCH_UPDATE);
            KNST_SET_MASK(xcb_mask.mask, XCB_INPUT_TOUCH_END);

            #undef KNST_SET_MASK

            #if defined(KNST_WINDOW_ENABLE_TOUCH_SCREEN)
            if (m_touch_supported) {
                xcb_input_xi_select_events(
                    KnstWindowSources::m_connection,
                    m_window,
                    1,
                    (xcb_input_event_mask_t*)&xcb_mask);
                m_touch_enabled = true;
            }
            #else
            (void)xcb_mask;
            #endif
            (void)xi_mask_bits;
        }

        // RawMotion on the root window. Raw motion events have no
        // window field — they arrive at the root.
        {
            struct {
                xcb_input_event_mask_t head;
                uint32_t mask[8];
            } raw_mask = {};

            raw_mask.head.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
            raw_mask.head.mask_len = (XCB_INPUT_RAW_MOTION + 8 + 31) / 32;

            #define KNST_SET_MASK(arr, ev) \
                (arr[(ev) / 32] |= (1u << ((ev) % 32)))

            KNST_SET_MASK(raw_mask.mask, XCB_INPUT_RAW_MOTION);

            #undef KNST_SET_MASK

            xcb_input_xi_select_events(
                KnstWindowSources::m_connection,
                KnstWindowSources::m_root,
                1,
                (xcb_input_event_mask_t*)&raw_mask);
        }

        xcb_flush(KnstWindowSources::m_connection);
    }

    knst_window_event_system::register_window(this);
}

inline void knst_window::KNST_X11_METHOD(enable_touch_screen)(bool enable) noexcept {
#if defined(KNST_WINDOW_ENABLE_TOUCH_SCREEN)
    if (!m_touch_supported || !m_window) return;
    m_touch_enabled = enable;
#endif
}


inline void knst_window::KNST_X11_METHOD(show)() noexcept {

    if (m_window) {
        xcb_map_window(KnstWindowSources::m_connection, m_window);

        if (m_knst_event.window_root_x > 0 || m_knst_event.window_root_y > 0) {
            move(m_knst_event.window_root_x, m_knst_event.window_root_y);
        }
        xcb_flush(KnstWindowSources::m_connection);

        // Request focus. On X11 this sends _NET_ACTIVE_WINDOW to the WM,
        // which decides whether to honor it. Prevents the "grey, inactive"
        // state on WMs that don't auto-focus new toplevels.
        focus();
    }
}

inline void knst_window::KNST_X11_METHOD(destroy)() noexcept {
    knst_window_event_system::unregister_window(this);

    // Notify surface clients so they unregister — otherwise a dangling
    // pointer remains. Normally the content is destroyed before the
    // window, but just in case.
    notify_window_destroyed();

    if (m_window == 0) return;
    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) {
        m_window = 0;
        return;
    }
    if (xcb_connection_has_error(conn)) {
        m_window = 0;
        return;
    }

    // Free the XSync counter (was leaked before).
    if (m_syncCounter != XCB_NONE) {
        xcb_sync_destroy_counter(conn, m_syncCounter);
        m_syncCounter = XCB_NONE;
    }

    // Destroy the X Input Context (was leaked before).
    if (m_xic) {
        XDestroyIC(m_xic);
        m_xic = nullptr;
    }

    xcb_destroy_window(conn, m_window);
    xcb_flush(conn);
    m_window = 0;
}


inline void knst_window::KNST_X11_METHOD(set_title)(const knst_c16string& title) noexcept {
    if (!m_window) return;

    knst_byte_string new_title(title);

    // Legacy WM_NAME (XA_STRING) — old WMs.
    xcb_change_property(
        KnstWindowSources::m_connection,
        XCB_PROP_MODE_REPLACE,
        m_window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8, new_title.length(), new_title.data()
    );

    // Modern _NET_WM_NAME (UTF8_STRING) — GNOME/KDE/Sway/Wayfire.
    xcb_intern_atom_cookie_t net_cookie = xcb_intern_atom(KnstWindowSources::m_connection, 0, 12, "_NET_WM_NAME");
    xcb_intern_atom_reply_t* net_reply = xcb_intern_atom_reply(KnstWindowSources::m_connection, net_cookie, nullptr);

    if (net_reply) {
        xcb_change_property(
            KnstWindowSources::m_connection,
            XCB_PROP_MODE_REPLACE,
            m_window,
            net_reply->atom,
            KnstWindowSources::m_UTF8_STRING,
            8, new_title.length(), new_title.data()
        );
        free(net_reply);
    }

    xcb_flush(KnstWindowSources::m_connection);
    m_title = title;
}


inline void knst_window::KNST_X11_METHOD(move)(int root_x, int root_y, const knst_monitor& monitor) noexcept {

    if (!m_window) return;
    // Negative coordinates are valid (monitor to the left of primary).
    // Only catch the KNST_WINDOW_DEFAULT sentinel.
    if (root_x == KNST_WINDOW_DEFAULT) {
        root_x = m_knst_event.window_root_x;
    }
    if (root_y == KNST_WINDOW_DEFAULT) {
        root_y = m_knst_event.window_root_y;
    }

    const int absolute_x = monitor.root_x + root_x;
    const int absolute_y = monitor.root_y + root_y;

    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_netMoveResizeWindow;

    uint32_t gravity_flags = XCB_GRAVITY_STATIC | (1 << 8) | (1 << 9) | (2 << 12);

    ev.data.data32[0] = gravity_flags;
    ev.data.data32[1] = static_cast<uint32_t>(absolute_x);
    ev.data.data32[2] = static_cast<uint32_t>(absolute_y);
    ev.data.data32[3] = 0;
    ev.data.data32[4] = 0;

    xcb_send_event(
        KnstWindowSources::m_connection,
        0,
        KnstWindowSources::m_root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        (const char*)&ev
    );

    xcb_flush(KnstWindowSources::m_connection);
}


inline void knst_window::KNST_X11_METHOD(move)(int root_x, int root_y) noexcept {

    if (!m_window) return;
    if (root_x == KNST_WINDOW_DEFAULT) {
        root_x = m_knst_event.window_root_x;
    }
    if (root_y == KNST_WINDOW_DEFAULT) {
        root_y = m_knst_event.window_root_y;
    }

    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_netMoveResizeWindow;

    uint32_t gravity_flags = XCB_GRAVITY_STATIC | (1 << 8) | (1 << 9) | (2 << 12);

    ev.data.data32[0] = gravity_flags;
    ev.data.data32[1] = (uint32_t)root_x;
    ev.data.data32[2] = (uint32_t)root_y;
    ev.data.data32[3] = 0;
    ev.data.data32[4] = 0;

    xcb_send_event(
        KnstWindowSources::m_connection,
        0,
        KnstWindowSources::m_root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        (const char*)&ev
    );

    xcb_flush(KnstWindowSources::m_connection);
}


inline void knst_window::KNST_X11_METHOD(toggle_fullscreen)(bool fullscreen) noexcept {
    if (!m_window) return;

    // Fullscreen override: some WMs (Mutter, KWin) strictly honor
    // WM_NORMAL_HINTS max size. If the user set a max via
    // set_maximum_size(), the WM refuses to grow to fullscreen. Strip
    // the max-size hint while fullscreen, restore on exit.
    //
    // Saved hints are stored per-window, not static — a static would be
    // shared across windows and break multi-window apps.
    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;

    auto get_hints_atom = [&]() -> xcb_atom_t {
        xcb_intern_atom_cookie_t c = xcb_intern_atom(conn, 0, 15, "WM_NORMAL_HINTS");
        xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(conn, c, nullptr);
        xcb_atom_t a = r ? r->atom : 0;
        if (r) free(r);
        return a;
    };

    if (fullscreen && !m_knst_event.is_full_screen) {
        // Save current hints, then strip P_MAX_SIZE.
        xcb_atom_t atom = get_hints_atom();
        if (atom) {
            xcb_get_property_cookie_t pc = xcb_get_property(
                conn, 0, m_window, atom, XCB_ATOM_WM_SIZE_HINTS, 0, 18);
            xcb_get_property_reply_t* pr = xcb_get_property_reply(conn, pc, nullptr);
            if (pr) {
                int len = xcb_get_property_value_length(pr);
                if (len >= (int)sizeof(xcb_size_hints_t)) {
                    m_saved_normal_hints = *reinterpret_cast<const xcb_size_hints_t*>(
                        xcb_get_property_value(pr));
                    m_have_saved_normal_hints = true;
                }
                free(pr);
            }

            if (m_have_saved_normal_hints) {
                xcb_size_hints_t stripped = m_saved_normal_hints;
                stripped.flags &= ~XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
                xcb_change_property(conn, XCB_PROP_MODE_REPLACE, m_window,
                    atom, XCB_ATOM_WM_SIZE_HINTS, 32, 18, &stripped);
            }
        }
    }

    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_NET_WM_STATE;
    ev.data.data32[0] = fullscreen ? 1 : 0;
    ev.data.data32[1] = KnstWindowSources::m_NET_WM_STATE_FULLSCREEN;
    ev.data.data32[3] = 1;

    xcb_send_event(conn, 0, KnstWindowSources::m_root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        (const char*)&ev);
    xcb_flush(conn);

    if (!fullscreen && m_knst_event.is_full_screen && m_have_saved_normal_hints) {
        xcb_atom_t atom = get_hints_atom();
        if (atom) {
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, m_window,
                atom, XCB_ATOM_WM_SIZE_HINTS, 32, 18, &m_saved_normal_hints);
        }
        m_have_saved_normal_hints = false;
    }
}


inline void knst_window::KNST_X11_METHOD(set_maximized)() noexcept {
    if (!m_window || m_knst_event.is_maximized) return;

    if (m_knst_event.is_minimized)   focus();
    if (m_knst_event.is_full_screen) toggle_fullscreen(false);

    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_NET_WM_STATE;
    ev.data.data32[0] = 1;   // ADD
    ev.data.data32[1] = KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_HORZ;
    ev.data.data32[2] = KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_VERT;
    ev.data.data32[3] = 1;

    xcb_send_event(KnstWindowSources::m_connection, 0, KnstWindowSources::m_root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        (const char*)&ev);
    xcb_flush(KnstWindowSources::m_connection);
}

inline void knst_window::KNST_X11_METHOD(set_minimized)() noexcept {

    if (!m_window || m_knst_event.is_minimized) return;

    // The old code called restore() first, which lost the maximized
    // state on minimize. The WM already preserves max when restoring
    // from icon.
    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_wmChangeState;
    ev.data.data32[0] = 3;   // IconicState

    xcb_send_event(KnstWindowSources::m_connection, 0, KnstWindowSources::m_root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        (const char*)&ev);
    xcb_flush(KnstWindowSources::m_connection);
}

inline void knst_window::KNST_X11_METHOD(restore)() noexcept {

    if (!m_window) return;

    if (m_knst_event.is_full_screen) { toggle_fullscreen(false); return; }
    if (m_knst_event.is_minimized)   { focus(); return; }

    if (m_knst_event.is_maximized) {
        xcb_client_message_event_t ev{};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 32;
        ev.window = m_window;
        ev.type = KnstWindowSources::m_NET_WM_STATE;
        ev.data.data32[0] = 0;   // REMOVE
        ev.data.data32[1] = KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_HORZ;
        ev.data.data32[2] = KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_VERT;
        ev.data.data32[3] = 1;

        xcb_send_event(KnstWindowSources::m_connection, 0, KnstWindowSources::m_root,
            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
            (const char*)&ev);
        xcb_flush(KnstWindowSources::m_connection);
        // No forced xcb_configure_window. The WM gives back the size;
        // ConfigureNotify -> RESIZE event -> GL OnResize works.
    }
}


inline void knst_window::KNST_X11_METHOD(hide)() noexcept {
    if (!m_window) return;

    xcb_unmap_window(KnstWindowSources::m_connection, m_window);
    xcb_flush(KnstWindowSources::m_connection);
}

inline void knst_window::KNST_X11_METHOD(focus)() noexcept {
    if (!m_window) return;

    xcb_client_message_event_t state_ev{};
    state_ev.response_type = XCB_CLIENT_MESSAGE;
    state_ev.format = 32;
    state_ev.window = m_window;
    state_ev.type = KnstWindowSources::m_wmChangeState;
    state_ev.data.data32[0] = 1;
    state_ev.data.data32[1] = 0;
    state_ev.data.data32[2] = 0;
    state_ev.data.data32[3] = 0;
    state_ev.data.data32[4] = 0;

    xcb_send_event(
        KnstWindowSources::m_connection,
        0,
        KnstWindowSources::m_root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        (const char*)&state_ev
    );
    xcb_flush(KnstWindowSources::m_connection);

    uint32_t stack_mode = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(
        KnstWindowSources::m_connection,
        m_window,
        XCB_CONFIG_WINDOW_STACK_MODE,
        &stack_mode
    );
    xcb_flush(KnstWindowSources::m_connection);

    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_NET_ACTIVE_WINDOW;
    ev.data.data32[0] = 2;
    ev.data.data32[1] = XCB_CURRENT_TIME;
    ev.data.data32[2] = 0;
    ev.data.data32[3] = 0;
    ev.data.data32[4] = 0;

    xcb_send_event(
        KnstWindowSources::m_connection,
        0,
        KnstWindowSources::m_root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        (const char*)&ev
    );
    xcb_flush(KnstWindowSources::m_connection);

    xcb_set_input_focus(
        KnstWindowSources::m_connection,
        XCB_INPUT_FOCUS_PARENT,
        m_window,
        XCB_CURRENT_TIME
    );
    xcb_flush(KnstWindowSources::m_connection);
}


inline void knst_window::KNST_X11_METHOD(set_cursor)(uint16_t cursor_type) noexcept {
    if (!m_window) return;

    Display* display = KnstWindowSources::m_xlib_display;
    if (!display) return;

    Cursor cursor = 0;

    // Abstract IDs for diagonal resize (no XC_ font equivalent).
    if (cursor_type == KNST_WINDOW_CURSOR_NWSE_RESIZE) {
        cursor = knst_x11_cursor_cache::get_named(display, "nwse-resize");
    } else if (cursor_type == KNST_WINDOW_CURSOR_NESW_RESIZE) {
        cursor = knst_x11_cursor_cache::get_named(display, "nesw-resize");
    } else if (cursor_type == KNST_WINDOW_CURSOR_ARROW) {
        cursor = knst_x11_cursor_cache::get_named(display, "left_ptr");
    } else if (cursor_type == KNST_WINDOW_CURSOR_HAND) {
        cursor = knst_x11_cursor_cache::get_named(display, "hand2");
    } else if (cursor_type == KNST_WINDOW_CURSOR_IBEAM) {
        cursor = knst_x11_cursor_cache::get_named(display, "xterm");
    } else if (cursor_type == KNST_WINDOW_CURSOR_CROSSHAIR) {
        cursor = knst_x11_cursor_cache::get_named(display, "crosshair");
    } else if (cursor_type == KNST_WINDOW_CURSOR_HRESIZE) {
        cursor = knst_x11_cursor_cache::get_named(display, "sb_h_double_arrow");
    } else if (cursor_type == KNST_WINDOW_CURSOR_VRESIZE) {
        cursor = knst_x11_cursor_cache::get_named(display, "sb_v_double_arrow");
    } else if (cursor_type == KNST_WINDOW_CURSOR_MOVE) {
        cursor = knst_x11_cursor_cache::get_named(display, "fleur");
    } else if (cursor_type == KNST_WINDOW_CURSOR_WAIT) {
        cursor = knst_x11_cursor_cache::get_named(display, "watch");
    } else if (cursor_type == KNST_WINDOW_CURSOR_HELP) {
        cursor = knst_x11_cursor_cache::get_named(display, "question_arrow");
    } else if (cursor_type == KNST_WINDOW_CURSOR_NOT_ALLOWED) {
        cursor = knst_x11_cursor_cache::get_named(display, "X_cursor");
    } else {
        // Fallback: font cursor.
        cursor = knst_x11_cursor_cache::get_font(display, cursor_type);
    }

    if (!cursor) return;

    // set_cursor_mode(DISABLED) may have grabbed the pointer — release
    // it. Otherwise the mouse stays locked even if the user picks a
    // different cursor.
    xcb_ungrab_pointer(KnstWindowSources::m_connection, XCB_CURRENT_TIME);

    xcb_change_window_attributes(
        KnstWindowSources::m_connection,
        m_window,
        XCB_CW_CURSOR,
        &cursor
    );
    xcb_flush(KnstWindowSources::m_connection);
    // The cache owns the cursor — do NOT XFreeCursor.
}

inline void knst_window::KNST_X11_METHOD(apply_bmp_icon)(const knst_byte_string& bytes, int icon_width, int icon_height) noexcept {

    if (!m_window || bytes.empty()) return;
    if (icon_width <= 0 || icon_height <= 0) return;

    // Verify buffer length before reading. The caller can pass any
    // dimensions; without this check we read past the buffer when the
    // data is smaller than width*height*4.
    const uint32_t expected = (uint32_t)icon_width * (uint32_t)icon_height * 4u;
    if (bytes.length() < expected) return;

    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(KnstWindowSources::m_connection, 0, 12, "_NET_WM_ICON");

    xcb_intern_atom_reply_t* reply =
        xcb_intern_atom_reply(KnstWindowSources::m_connection, cookie, nullptr);

    if (!reply) return;

    uint32_t pixel_count = icon_width * icon_height;
    uint32_t* argb = new uint32_t[2 + pixel_count];
    argb[0] = icon_width;
    argb[1] = icon_height;

    const unsigned char* rgba = bytes.data();
    for (uint32_t i = 0; i < pixel_count; i++) {
        // uint8_t is promoted to int — 255 << 24 is signed overflow (UB).
        argb[2 + i] = ((uint32_t)rgba[i*4+3] << 24) |
                      ((uint32_t)rgba[i*4+0] << 16) |
                      ((uint32_t)rgba[i*4+1] <<  8) |
                       (uint32_t)rgba[i*4+2];
    }

    xcb_change_property(
        KnstWindowSources::m_connection,
        XCB_PROP_MODE_REPLACE,
        m_window,
        reply->atom,
        XCB_ATOM_CARDINAL,
        32,
        2 + pixel_count,
        argb
    );

    xcb_flush(KnstWindowSources::m_connection);
    free(reply);
    delete[] argb;
}


inline void knst_window::KNST_X11_METHOD(set_bmp_cursor)(
    const knst_byte_string& data,
    int width,
    int height,
    int hot_x,
    int hot_y
) noexcept {

    if (!m_window || data.empty() || width <= 0 || height <= 0) return;

    Display* display = KnstWindowSources::m_xlib_display;
    if (!display) return;

    const uint8_t* pixel_data = (const uint8_t*)data.data();
    uint32_t data_len = data.length();
    uint32_t expected_4ch = (uint32_t)(width * height * 4);
    uint32_t expected_3ch = (uint32_t)(width * height * 3);

    XcursorImage* cursor_image = XcursorImageCreate(width, height);
    if (!cursor_image) return;

    cursor_image->xhot = (hot_x >= 0 && hot_x < width)  ? hot_x  : width  / 2;
    cursor_image->yhot = (hot_y >= 0 && hot_y < height) ? hot_y  : height / 2;

    uint32_t* pixels = cursor_image->pixels;

    if (data_len == expected_4ch) {
        for (int i = 0; i < width * height; i++) {
            uint8_t r = pixel_data[i * 4 + 0];
            uint8_t g = pixel_data[i * 4 + 1];
            uint8_t b = pixel_data[i * 4 + 2];
            uint8_t a = pixel_data[i * 4 + 3];
            pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g <<  8) |  (uint32_t)b;
        }
    } else if (data_len == expected_3ch) {
        for (int i = 0; i < width * height; i++) {
            uint8_t r = pixel_data[i * 3 + 0];
            uint8_t g = pixel_data[i * 3 + 1];
            uint8_t b = pixel_data[i * 3 + 2];
            pixels[i] = ((uint32_t)0xFF << 24) | ((uint32_t)r << 16) | ((uint32_t)g    <<  8) |  (uint32_t)b;
                        
        }
    } else {
        XcursorImageDestroy(cursor_image);
        return;
    }

    Cursor cursor = XcursorImageLoadCursor(display, cursor_image);
    XcursorImageDestroy(cursor_image);

    if (!cursor) return;

    xcb_change_window_attributes(
        KnstWindowSources::m_connection,
        m_window,
        XCB_CW_CURSOR,
        &cursor
    );
    xcb_flush(KnstWindowSources::m_connection);

    XFreeCursor(display, cursor);
}


inline void knst_window::KNST_X11_METHOD(reset_cursor)() noexcept {
    if (!m_window) return;

    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;

    Display* display = KnstWindowSources::m_xlib_display;
    if (!display) return;

    // Coming out of DISABLED mode — release the pointer grab. Otherwise
    // the mouse stays locked to the window.
    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);

    Cursor default_cursor = XCreateFontCursor(display, XC_left_ptr);
    if (!default_cursor) return;

    xcb_change_window_attributes(
        conn,
        m_window,
        XCB_CW_CURSOR,
        &default_cursor
    );
    xcb_flush(conn);

    XFreeCursor(display, default_cursor);
}


inline void knst_window::KNST_X11_METHOD(set_raw_mouse_mode)(bool enable) noexcept {
    // XInput2 — raw relative motion.
    //
    // The event pump uses XCB, so SELECTION must go through XCB too.
    // Xlib's XISelectEvents can't translate the event into the XCB wire
    // format — silently does nothing.
    //
    // RawMotion is selected on the ROOT window (no window field).
    // Selecting on m_window means no events ever arrive. Root selection
    // delivers events for ALL windows; the event pump routes them to
    // the focused window.
    if (!m_window || KnstWindowSources::m_xi_opcode < 0) return;

    // This flag is per-window; the event pump uses it when routing.
    m_raw_mouse_enabled = enable;
}

inline void knst_window::KNST_X11_METHOD(resize)(int width, int height) noexcept {

    if (!m_window) return;

    if (width  < 0) width  = m_knst_event.window_width;
    if (height < 0) height = m_knst_event.window_height;

    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;

    uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    uint32_t values[2] = { (uint32_t)width, (uint32_t)height };

    xcb_configure_window(
        conn,
        m_window,
        mask,
        values
    );
    xcb_flush(conn);

    // Do NOT update m_knst_event size here. The X server sends
    // ConfigureNotify — load_native_to_knst_event produces the RESIZE
    // event and updates m_knst_event. If we update here, size_changed
    // becomes false and GL OnResize never fires.
    //
    // get_width() returns the old value until ConfigureNotify arrives —
    // that's the correct behavior (X11 is async).
}

inline void knst_window::KNST_X11_METHOD(set_cursor_mode)(int mode) noexcept {
    if (!m_window) return;

    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;

    Display* display = KnstWindowSources::m_xlib_display;
    if (!display) return;

    if (mode == KNST_WINDOW_CURSOR_ARROW) {

        uint32_t cursor_value = XCB_NONE;
        xcb_change_window_attributes(
            conn,
            m_window,
            XCB_CW_CURSOR,
            &cursor_value
        );

        xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
        xcb_flush(conn);

    } else if (mode == KNST_WINDOW_CURSOR_HIDDEN) {

        // The old code created a 1x1 pixmap whose content is undefined
        // (X server returns garbage). Some drivers render a visible dot.
        // XcursorImage gives a guaranteed-transparent cursor.
        Cursor hidden_cursor = 0;
        {
            XcursorImage* img = XcursorImageCreate(1, 1);
            if (img) {
                img->xhot = 0;
                img->yhot = 0;
                img->pixels[0] = 0;   // fully transparent ARGB
                hidden_cursor = XcursorImageLoadCursor(display, img);
                XcursorImageDestroy(img);
            }
        }

        if (hidden_cursor) {
            xcb_change_window_attributes(
                conn,
                m_window,
                XCB_CW_CURSOR,
                &hidden_cursor
            );
            XFreeCursor(display, hidden_cursor);
        }
        xcb_flush(conn);

    } else if (mode == KNST_WINDOW_CURSOR_DISABLED) {

        // Same transparent-cursor approach.
        Cursor hidden_cursor = 0;
        {
            XcursorImage* img = XcursorImageCreate(1, 1);
            if (img) {
                img->xhot = 0;
                img->yhot = 0;
                img->pixels[0] = 0;
                hidden_cursor = XcursorImageLoadCursor(display, img);
                XcursorImageDestroy(img);
            }
        }

        if (hidden_cursor) {
            xcb_change_window_attributes(
                conn,
                m_window,
                XCB_CW_CURSOR,
                &hidden_cursor
            );
            XFreeCursor(display, hidden_cursor);
        }

        xcb_grab_pointer(
            conn,
            0,
            m_window,
            XCB_EVENT_MASK_POINTER_MOTION |
            XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE,
            XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC,
            m_window,
            XCB_NONE,
            XCB_CURRENT_TIME
        );

        int center_x = m_knst_event.window_width  / 2;
        int center_y = m_knst_event.window_height / 2;

        xcb_warp_pointer(
            conn,
            XCB_NONE,
            m_window,
            0, 0, 0, 0,
            center_x,
            center_y
        );

        xcb_flush(conn);
    }
}

inline void knst_window::KNST_X11_METHOD(set_cursor_pos_on_window)(int x, int y) noexcept {
    if (!m_window) return;

    if (x < 0) x = m_knst_event.mouse_x;
    if (y < 0) y = m_knst_event.mouse_y;

    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;

    xcb_warp_pointer(
        conn,
        XCB_NONE,
        m_window,
        0, 0, 0, 0,
        (int16_t)x,
        (int16_t)y
    );
    xcb_flush(conn);
}

inline void knst_window::KNST_X11_METHOD(set_cursor_pos_global)(int root_x, int root_y) noexcept {
    if (!m_window) return;

    if (root_x == KNST_WINDOW_DEFAULT) root_x = m_knst_event.window_root_x;
    if (root_y == KNST_WINDOW_DEFAULT) root_y = m_knst_event.window_root_y;

    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;

    xcb_warp_pointer(
        conn,
        XCB_NONE,
        XCB_NONE,
        0, 0, 0, 0,
        (int16_t)root_x,
        (int16_t)root_y
    );
    xcb_flush(conn);
}

inline void knst_window::KNST_X11_METHOD(set_clipboard)(const knst_c16string& text) noexcept {
    // The old code wrote into clipboard_text, so pasting would overwrite
    // the fetched data. Track the owned text separately now.
    m_clipboard_owned = text;

    if (KnstWindowSources::m_CLIPBOARD) {
        xcb_set_selection_owner(
            KnstWindowSources::m_connection,
            m_window,
            KnstWindowSources::m_CLIPBOARD,
            XCB_CURRENT_TIME
        );
        xcb_flush(KnstWindowSources::m_connection);
    }
}

inline void knst_window::KNST_X11_METHOD(request_clipboard)() noexcept {
    if (!m_window || !KnstWindowSources::m_CLIPBOARD) return;

    // If we own the clipboard, return our own text directly. Reading via
    // xcb_convert_selection when we're the owner triggers
    // SELECTION_REQUEST -> we write the property — works but wastes a
    // round-trip and is fragile. Returning the owned text is instant.
    if (!m_clipboard_owned.empty()) {
        clipboard_text = m_clipboard_owned;
        m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
        dispatch_current_event();
        return;
    }

    // X11 clipboard is async. The request goes out, the reply arrives
    // as XCB_SELECTION_NOTIFY in the event loop. The old code only sent
    // the request and never produced an event — the user had no way to
    // know data arrived.
    //
    // Now: the event loop catches SELECTION_NOTIFY and fires
    // CLIPBOARD_DATA. The old behavior (sync assumption) is preserved
    // by clearing clipboard_text; the event fills it in.
    clipboard_text = u"";

    // Use the dedicated clipboard property atom. Using m_PRIMARY here
    // clashed with the primary-selection request below.
    xcb_convert_selection(
        KnstWindowSources::m_connection,
        m_window,
        KnstWindowSources::m_CLIPBOARD,
        KnstWindowSources::m_UTF8_STRING,
        KnstWindowSources::m_clipboard_property,
        XCB_CURRENT_TIME
    );
    xcb_flush(KnstWindowSources::m_connection);
}

inline void knst_window::KNST_X11_METHOD(set_drag_drop_status)(bool enabled) noexcept {
    m_drag_drop_enabled = enabled;
    if (enabled) {
        uint32_t xdnd_version = 5;
        xcb_change_property(
            KnstWindowSources::m_connection,
            XCB_PROP_MODE_REPLACE,
            m_window,
            KnstWindowSources::m_XdndAware,
            XCB_ATOM_ATOM,
            32,
            1,
            &xdnd_version
        );
    } else {
        xcb_delete_property(
            KnstWindowSources::m_connection,
            m_window,
            KnstWindowSources::m_XdndAware
        );
    }
    xcb_flush(KnstWindowSources::m_connection);
}

inline void knst_window::KNST_X11_METHOD(set_opacity)(float opacity) noexcept {

    if (!m_window) return;

    if (opacity < 0.0f) opacity = 0.0f;

    if (opacity > 1.0f) opacity = 1.0f;

    uint32_t value = (uint32_t)(opacity * (double)0xFFFFFFFF);
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(KnstWindowSources::m_connection, 0, 22, "_NET_WM_WINDOW_OPACITY");
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(KnstWindowSources::m_connection, cookie, nullptr);

    if (!reply) return;

    xcb_change_property(
        KnstWindowSources::m_connection,
        XCB_PROP_MODE_REPLACE,
        m_window,
        reply->atom,
        XCB_ATOM_CARDINAL,
        32,
        1,
        &value
    );

    xcb_flush(KnstWindowSources::m_connection);
    free(reply);
    m_opacity = opacity;
}


inline void knst_window::KNST_X11_METHOD(set_attribute)(int attribute, bool value) noexcept {
    if (!m_window) return;

    switch (attribute) {
        case KNST_WINDOW_ATTR_DECORATED: {
            xcb_atom_t motif_wm_hints = KnstWindowSources::m_MOTIF_WM_HINTS;

            struct {
                uint32_t flags;
                uint32_t functions;
                uint32_t decorations;
                int32_t  input_mode;
                uint32_t status;
            } hints = {};

            hints.flags = 2;
            hints.decorations = value ? 1 : 0;

            xcb_change_property(
                KnstWindowSources::m_connection,
                XCB_PROP_MODE_REPLACE,
                m_window,
                motif_wm_hints,
                motif_wm_hints,
                32, 5, &hints
            );

            xcb_unmap_window(KnstWindowSources::m_connection, m_window);
            xcb_map_window(KnstWindowSources::m_connection, m_window);
            xcb_flush(KnstWindowSources::m_connection);

            m_draw_custom_title_bar = !value;
            break;
        }

        case KNST_WINDOW_ATTR_RESIZABLE: {
            xcb_atom_t wm_normal_hints;
            const char* name = "WM_NORMAL_HINTS";
            xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
                KnstWindowSources::m_connection, 0, (uint16_t)strlen(name), name);
            xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
                KnstWindowSources::m_connection, cookie, nullptr);
            if (reply) {
                wm_normal_hints = reply->atom;
                free(reply);

                // Read and preserve existing hints. Otherwise the user's
                // set_minimum_size() and other WM-written fields are
                // wiped.
                xcb_size_hints_t hints = {};
                {
                    auto pc = xcb_get_property(
                        KnstWindowSources::m_connection, 0, m_window,
                        wm_normal_hints, XCB_ATOM_WM_SIZE_HINTS, 0, 18);
                    auto* pr = xcb_get_property_reply(
                        KnstWindowSources::m_connection, pc, nullptr);
                    if (pr) {
                        const int len = xcb_get_property_value_length(pr);
                        if (len >= (int)sizeof(xcb_size_hints_t)) {
                            hints = *reinterpret_cast<const xcb_size_hints_t*>(
                                xcb_get_property_value(pr));
                        }
                        free(pr);
                    }
                }

                if (!value) {
                    // RESIZABLE=false — set current size as both min and
                    // max.
                    hints.flags |= XCB_ICCCM_SIZE_HINT_P_MIN_SIZE |
                                   XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
                    hints.min_width  = hints.max_width  = m_knst_event.window_width;
                    hints.min_height = hints.max_height = m_knst_event.window_height;
                } else {
                    // Do NOT clear P_MIN_SIZE / P_MAX_SIZE. The user may
                    // have set them separately via set_minimum_size() /
                    // set_maximum_size() — clearing would silently undo
                    // those settings.
                    //
                    // The WM applies whatever hints are present. Since
                    // we just re-READ and don't touch min/max widths,
                    // they remain in force. There's no WM_NORMAL_HINTS
                    // lock in ICCCM, so nothing else needs changing.
                }

                xcb_change_property(
                    KnstWindowSources::m_connection,
                    XCB_PROP_MODE_REPLACE,
                    m_window,
                    wm_normal_hints,
                    XCB_ATOM_WM_SIZE_HINTS,
                    32, 18, &hints
                );
                xcb_flush(KnstWindowSources::m_connection);
            }
            break;
        }

        case KNST_WINDOW_ATTR_ALWAYS_ON_TOP: {
            const char* name = "_NET_WM_STATE_ABOVE";
            xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
                KnstWindowSources::m_connection, 0, (uint16_t)strlen(name), name);
            xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
                KnstWindowSources::m_connection, cookie, nullptr);
            if (reply) {
                xcb_client_message_event_t ev{};
                ev.response_type = XCB_CLIENT_MESSAGE;
                ev.format = 32;
                ev.window = m_window;
                ev.type = KnstWindowSources::m_NET_WM_STATE;
                ev.data.data32[0] = value ? 1 : 0;
                ev.data.data32[1] = reply->atom;
                ev.data.data32[2] = 0;
                ev.data.data32[3] = 0;
                ev.data.data32[4] = 0;

                xcb_send_event(
                    KnstWindowSources::m_connection,
                    0, KnstWindowSources::m_root,
                    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                    (const char*)&ev
                );
                free(reply);
            }
            break;
        }

        case KNST_WINDOW_ATTR_TRANSPARENT: {
            if (value) {
                XserverRegion region = XFixesCreateRegion(
                    KnstWindowSources::m_xlib_display, nullptr, 0);
                XFixesSetWindowShapeRegion(
                    KnstWindowSources::m_xlib_display,
                    m_window,
                    ShapeInput,
                    0, 0,
                    region);
                XFixesDestroyRegion(KnstWindowSources::m_xlib_display, region);
            } else {
                XFixesSetWindowShapeRegion(
                    KnstWindowSources::m_xlib_display,
                    m_window,
                    ShapeInput,
                    0, 0,
                    0);
            }
            XFlush(KnstWindowSources::m_xlib_display);
            m_input_transparent = value;
            break;
        }
    }

    xcb_flush(KnstWindowSources::m_connection);
}


inline bool knst_window::KNST_X11_METHOD(get_attribute)(int attribute) const noexcept {
    if (!m_window) return false;

    switch (attribute) {
        case KNST_WINDOW_ATTR_DECORATED: {
            if (m_draw_custom_title_bar) return false;

            xcb_atom_t motif_wm_hints = KnstWindowSources::m_MOTIF_WM_HINTS;

            xcb_get_property_cookie_t prop_cookie = xcb_get_property(
                KnstWindowSources::m_connection, 0, m_window,
                motif_wm_hints, motif_wm_hints, 0, 5);
            xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(
                KnstWindowSources::m_connection, prop_cookie, nullptr);

            if (prop_reply) {
                int len = xcb_get_property_value_length(prop_reply);
                uint32_t* data = (uint32_t*)xcb_get_property_value(prop_reply);
                // MOTIF_WM_HINTS has 5 fields; data[2] = decorations.
                // Guard against short/empty property — avoid OOB read.
                bool decorated = true;
                if (data && len >= 3 * (int)sizeof(uint32_t)) {
                    decorated = (data[2] != 0);
                }
                free(prop_reply);
                return decorated;
            }
            return true;
        }

        case KNST_WINDOW_ATTR_RESIZABLE:
            return true;

        case KNST_WINDOW_ATTR_ALWAYS_ON_TOP:
            return false;

        case KNST_WINDOW_ATTR_TRANSPARENT:
            return m_input_transparent;
    }
    return false;
}


inline void knst_window::KNST_X11_METHOD(set_minimum_size)(int width, int height) noexcept {
    if (!m_window) return;

    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;

    const char* atom_name = "WM_NORMAL_HINTS";
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
        conn, 0, (uint16_t)strlen(atom_name), atom_name);
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(conn, cookie, nullptr);

    if (!reply) return;

    xcb_atom_t wm_normal_hints = reply->atom;
    free(reply);

    xcb_get_property_cookie_t prop_cookie = xcb_get_property(conn, 0, m_window, wm_normal_hints, XCB_ATOM_WM_SIZE_HINTS, 0, 18);
    xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(conn, prop_cookie, nullptr);

    xcb_size_hints_t hints = {};

    if (prop_reply) {
        // xcb_get_property_value() returns non-null even for an empty
        // property. Checking `if (existing)` isn't enough — OOB read
        // (72 bytes). Length check first.
        const int value_len = xcb_get_property_value_length(prop_reply);
        if (value_len >= (int)sizeof(xcb_size_hints_t)) {
            const auto* existing =
                (const xcb_size_hints_t*)xcb_get_property_value(prop_reply);
            if (existing) hints = *existing;
        }
        free(prop_reply);
    }

    // DEFAULT/DEFAULT — remove the min-size limit entirely. The old
    // behavior set 300x250 even when the user said "reset", so the min
    // stayed active.
    if (width == KNST_WINDOW_DEFAULT && height == KNST_WINDOW_DEFAULT) {
        hints.flags &= ~XCB_ICCCM_SIZE_HINT_P_MIN_SIZE;
        xcb_change_property(
            conn, XCB_PROP_MODE_REPLACE, m_window,
            wm_normal_hints, XCB_ATOM_WM_SIZE_HINTS, 32, 18, &hints);
        xcb_flush(conn);
        return;
    }

    if (width == KNST_WINDOW_DEFAULT) width  = 300;
    if (height == KNST_WINDOW_DEFAULT) height = 250;

    hints.flags |= XCB_ICCCM_SIZE_HINT_P_MIN_SIZE;
    hints.min_width  = width;
    hints.min_height = height;

    xcb_change_property(
        conn,
        XCB_PROP_MODE_REPLACE,
        m_window,
        wm_normal_hints,
        XCB_ATOM_WM_SIZE_HINTS,
        32, 18, &hints
    );

    xcb_flush(conn);
}

inline void knst_window::KNST_X11_METHOD(set_maximum_size)(int width, int height) noexcept {
    if (!m_window) return;

    // DEFAULT/DEFAULT — clear the maximum size limit entirely.
    if (width == KNST_WINDOW_DEFAULT && height == KNST_WINDOW_DEFAULT) {
        const char* atom_name = "WM_NORMAL_HINTS";
        xcb_intern_atom_cookie_t cookie = xcb_intern_atom(KnstWindowSources::m_connection, 0, (uint16_t)strlen(atom_name), atom_name);
            
        xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply( KnstWindowSources::m_connection, cookie, nullptr);
           
        if (!reply) return;

        xcb_atom_t wm_normal_hints = reply->atom;
        free(reply);

        xcb_get_property_cookie_t prop_cookie = xcb_get_property(
            KnstWindowSources::m_connection, 0, m_window,
            wm_normal_hints, XCB_ATOM_WM_SIZE_HINTS, 0, 18);
        xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(
            KnstWindowSources::m_connection, prop_cookie, nullptr);

        xcb_size_hints_t hints = {};
        if (prop_reply) {
            const int value_len = xcb_get_property_value_length(prop_reply);
            if (value_len >= (int)sizeof(xcb_size_hints_t)) {
                const auto* existing =
                    (const xcb_size_hints_t*)xcb_get_property_value(prop_reply);
                if (existing) hints = *existing;
            }
            free(prop_reply);
        }
        hints.flags &= ~XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;

        xcb_change_property(
            KnstWindowSources::m_connection, XCB_PROP_MODE_REPLACE, m_window,
            wm_normal_hints, XCB_ATOM_WM_SIZE_HINTS, 32, 18, &hints);
        xcb_flush(KnstWindowSources::m_connection);
        return;
    }
    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;


    const char* atom_name = "WM_NORMAL_HINTS";
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, (uint16_t)strlen(atom_name), atom_name);
    



    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(conn, cookie, nullptr);

    if (!reply) return;

    xcb_atom_t wm_normal_hints = reply->atom;
    free(reply);

    xcb_get_property_cookie_t prop_cookie = xcb_get_property(conn, 0, m_window, wm_normal_hints, XCB_ATOM_WM_SIZE_HINTS, 0, 18);
    

    xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(conn, prop_cookie, nullptr);

    xcb_size_hints_t hints = {};

    if (prop_reply) {
        const int value_len = xcb_get_property_value_length(prop_reply);
        if (value_len >= (int)sizeof(xcb_size_hints_t)) {
            const auto* existing =
                (const xcb_size_hints_t*)xcb_get_property_value(prop_reply);
            if (existing) hints = *existing;
        }
        free(prop_reply);
    }

    hints.flags |= XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
    hints.max_width = (width  == KNST_WINDOW_DEFAULT) ? 0 : (uint32_t)width;
    hints.max_height = (height == KNST_WINDOW_DEFAULT) ? 0 : (uint32_t)height;

    xcb_change_property(
        conn,
        XCB_PROP_MODE_REPLACE,
        m_window,
        wm_normal_hints,
        XCB_ATOM_WM_SIZE_HINTS,
        32, 18, &hints
    );

    xcb_flush(conn);
}

// ─── CSD hit -> X11 protocol action ────────────────────────────────────────
inline void knst_window::KNST_X11_METHOD(csd_handle_hit)(csd_hit hit, int x, int y, uint32_t input_serial) noexcept {

    (void)input_serial;   // X11 _NET_WM_MOVERESIZE doesn't use a serial

    if (!m_window) return;

    // Translate to root coordinates.
    int root_x = x;
    int root_y = y;
    {
        xcb_translate_coordinates_cookie_t tc = xcb_translate_coordinates(
            KnstWindowSources::m_connection,
            m_window, KnstWindowSources::m_root,
            (int16_t)x, (int16_t)y);
        xcb_translate_coordinates_reply_t* tr = xcb_translate_coordinates_reply(
            KnstWindowSources::m_connection, tc, nullptr);
        if (tr) {
            root_x = tr->dst_x;
            root_y = tr->dst_y;
            free(tr);
        }
    }

    auto send_moveresize = [&](uint32_t direction) {
        xcb_client_message_event_t ev{};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 32;
        ev.window = m_window;
        ev.type   = KnstWindowSources::m_NET_WM_MOVERESIZE;
        ev.data.data32[0] = (uint32_t)root_x;
        ev.data.data32[1] = (uint32_t)root_y;
        ev.data.data32[2] = direction;
        ev.data.data32[3] = 1;    // button (left)
        ev.data.data32[4] = 1;    // source (application)

        xcb_ungrab_pointer(KnstWindowSources::m_connection, XCB_CURRENT_TIME);
        xcb_send_event(
            KnstWindowSources::m_connection,
            0,
            KnstWindowSources::m_root,
            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
            (const char*)&ev);
        xcb_flush(KnstWindowSources::m_connection);
    };

    switch (hit) {
        case csd_hit::draggable: {
            // Leave state cleanup to the WM. When the WM receives
            // _NET_WM_MOVERESIZE it removes maximize/fullscreen itself
            // and computes the mouse offset correctly. Doing un-max
            // ourselves broke that offset.
            send_moveresize(8);
            break;
        }
        case csd_hit::resize_top_left: send_moveresize(0); break;
        case csd_hit::resize_top: send_moveresize(1); break;
        case csd_hit::resize_top_right: send_moveresize(2); break;
        case csd_hit::resize_right: send_moveresize(3); break;
        case csd_hit::resize_bottom_right: send_moveresize(4); break;
        case csd_hit::resize_bottom: send_moveresize(5); break;
        case csd_hit::resize_bottom_left: send_moveresize(6); break;
        case csd_hit::resize_left: send_moveresize(7); break;
        case csd_hit::normal:
        default:
            break;
    }
}

// ─── Primary selection (X11 middle-click paste) ────────────────────────────
inline void knst_window::KNST_X11_METHOD(set_primary_selection)(const knst_c16string& text) noexcept {
    if (!m_window || !KnstWindowSources::m_PRIMARY) return;
    m_primary_owned = text;
    xcb_set_selection_owner(
        KnstWindowSources::m_connection,
        m_window,
        KnstWindowSources::m_PRIMARY,
        XCB_CURRENT_TIME);
    xcb_flush(KnstWindowSources::m_connection);
}

inline void knst_window::KNST_X11_METHOD(request_primary_selection)() noexcept {
    if (!m_window || !KnstWindowSources::m_PRIMARY) return;
    clipboard_text = u"";
    // Use the dedicated primary property atom.
    xcb_convert_selection(
        KnstWindowSources::m_connection,
        m_window,
        KnstWindowSources::m_PRIMARY,
        KnstWindowSources::m_UTF8_STRING,
        KnstWindowSources::m_primary_property,
        XCB_CURRENT_TIME);
    xcb_flush(KnstWindowSources::m_connection);
}

inline void knst_window::KNST_X11_METHOD(set_idle_inhibit)(bool /*enable*/) noexcept {
    // There's no standard idle-inhibit protocol on X11. Solutions are
    // the XScreenSaver extension or DBus (org.freedesktop.ScreenSaver).
    // Deliberately a no-op; capabilities() reports idle_inhibit = false
    // on X11.
}

#endif