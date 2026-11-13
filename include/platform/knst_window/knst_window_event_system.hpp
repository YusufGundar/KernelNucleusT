// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_event_system.hpp
----------------------------

    Event processing types live here.

    Four copies of the X11 event routing used to exist — this file
    collapses them into one dispatcher. Fix a bug once, done.

*/

#pragma once


#if KNST_USING_PLATFORM_WINDOWS
    #include <windowsx.h>
    KNST_FORCE_INLINE LRESULT CALLBACK load_native_to_knst_event(HWND, UINT, WPARAM, LPARAM) noexcept;
#endif
#if KNST_USING_LINUX_PLATFORM_X11
    #include <poll.h>
    KNST_FORCE_INLINE void load_native_to_knst_event(knst_window& window, xcb_generic_event_t* ev) noexcept;
#endif
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    #include <poll.h>
    #include <unistd.h>
    #include <errno.h>
#endif


struct knst_window_event_system {

private:
    friend class knst_window;
    static inline knst_vector<knst_window*> windows;

    KNST_FORCE_INLINE static void register_window(knst_window* window) noexcept {
        windows.push_back(window);
    }

    KNST_FORCE_INLINE static void unregister_window(knst_window* window) noexcept {
        const uint32_t n = (uint32_t)windows.size();
        for (uint32_t i = 0; i < n; ++i) {
            if (windows[i] == window) {
                windows[i] = windows.back();
                windows.pop_back();
                return;
            }
        }
    }

    // ─── Connection lost -> DISCONNECT event on every window ──────────
    static void notify_disconnect_all() noexcept {
        const uint32_t n = (uint32_t)windows.size();
        for (uint32_t i = 0; i < n; ++i) {
            auto* w = windows[i];
            if (!w) continue;
            w->m_disconnected = true;
            w->m_knst_event.begin(KNST_WINDOW_EVENT_DISCONNECT);
            // should_close() is NOT called here — the app keeps running
            // when the connection drops. User checks is_disconnected()
            // and decides what to do (restart, show error, ...).
            w->dispatch_current_event();
        }
    }

    static void check_key_repeat_all() noexcept {
        const uint32_t n = (uint32_t)windows.size();
        for (uint32_t i = 0; i < n; ++i) {
            check_key_repeat(*windows[i]);
        }
    }

#if KNST_USING_LINUX_PLATFORM_X11
    static knst_window* find_window(xcb_window_t id) noexcept {
        const uint32_t n = (uint32_t)windows.size();
        for (uint32_t i = 0; i < n; ++i) {
            if (windows[i]->get_x11_window_handle() == id) return windows[i];
        }
        return nullptr;
    }

    // ─── Route a single XCB event to the right window ─────────────────
    //   true  -> delivered (load_native_to_knst_event freed it)
    //   false -> dropped / swallowed (freed here)
    static bool x11_dispatch_one(xcb_generic_event_t* ev) noexcept {
        constexpr xcb_window_t TARGET_FOCUSED = 0xFFFFFFFFu;   // RawMotion

        const uint8_t event_type = ev->response_type & ~0x80;

        // RandR and other global events first.
        if (KnstWindowSources::x11_try_global_event(event_type, ev)) {
            free(ev);
            return false;
        }

        xcb_window_t target = XCB_NONE;

        switch (event_type) {
            case XCB_KEY_PRESS:
            case XCB_KEY_RELEASE:
            case XCB_BUTTON_PRESS:
            case XCB_BUTTON_RELEASE:
            case XCB_MOTION_NOTIFY:
            case XCB_ENTER_NOTIFY:
            case XCB_LEAVE_NOTIFY:
                target = ((xcb_key_press_event_t*)ev)->event; break;
            case XCB_FOCUS_IN:
            case XCB_FOCUS_OUT:
                target = ((xcb_focus_in_event_t*)ev)->event; break;
            case XCB_CONFIGURE_NOTIFY:
                target = ((xcb_configure_notify_event_t*)ev)->window; break;
            case XCB_EXPOSE:
                target = ((xcb_expose_event_t*)ev)->window; break;
            case XCB_CLIENT_MESSAGE:
                target = ((xcb_client_message_event_t*)ev)->window; break;
            case XCB_PROPERTY_NOTIFY:
                target = ((xcb_property_notify_event_t*)ev)->window; break;
            case XCB_VISIBILITY_NOTIFY:
                target = ((xcb_visibility_notify_event_t*)ev)->window; break;
            case XCB_MAP_NOTIFY:
                target = ((xcb_map_notify_event_t*)ev)->window; break;
            case XCB_UNMAP_NOTIFY:
                target = ((xcb_unmap_notify_event_t*)ev)->window; break;
            case XCB_DESTROY_NOTIFY:
                target = ((xcb_destroy_notify_event_t*)ev)->window; break;
            case XCB_SELECTION_NOTIFY:
                target = ((xcb_selection_notify_event_t*)ev)->requestor; break;
            case XCB_SELECTION_REQUEST:
                target = ((xcb_selection_request_event_t*)ev)->owner; break;

                
            case XCB_GE_GENERIC: {
                auto* ge = (xcb_ge_generic_event_t*)ev;
                if (KnstWindowSources::m_xi_opcode < 0 ||
                    ge->extension != (uint8_t)KnstWindowSources::m_xi_opcode) break;
                const uint16_t xi_ev = ge->event_type;
                if (xi_ev == XCB_INPUT_TOUCH_BEGIN ||
                    xi_ev == XCB_INPUT_TOUCH_UPDATE ||
                    xi_ev == XCB_INPUT_TOUCH_END) {
                    target = ((xcb_input_touch_begin_event_t*)ev)->event;
                } else if (xi_ev == XCB_INPUT_RAW_MOTION) {
                    target = TARGET_FOCUSED;
                }
                break;
            }

            case XCB_MAPPING_NOTIFY:
                xcb_refresh_keyboard_mapping(KnstWindowSources::m_keysyms,(xcb_mapping_notify_event_t*)ev);
                free(ev);
                return false;

            default: break;
        }

        knst_window* tw = nullptr;
        if (target == TARGET_FOCUSED) {
            // RawMotion has no window field — route to the focused one.
            for (auto* w : windows) {
                if (w && w->m_knst_event.is_focused) { tw = w; break; }
            }
            if (!tw && !windows.empty()) tw = windows[0];
        } else if (target != XCB_NONE) {
            tw = find_window(target);
        }

        if (!tw) { free(ev); return false; }
        load_native_to_knst_event(*tw, ev);
        return true;
    }

    static void block_pool_x11() noexcept {
        xcb_connection_t* conn = KnstWindowSources::m_connection;
        if (!conn) return;
        while (true) {
            xcb_generic_event_t* ev = xcb_wait_for_event(conn);
            if (!ev) { notify_disconnect_all(); return; }
            if (x11_dispatch_one(ev)) break;  // block mode: one event
        }
        xcb_flush(conn);
        check_key_repeat_all();
    }

    static void non_block_pool_x11() noexcept {
        xcb_connection_t* conn = KnstWindowSources::m_connection;
        if (!conn) return;

        constexpr int MAX_EVENTS_PER_CALL = 64;
        for (int processed = 0; processed < MAX_EVENTS_PER_CALL; ++processed) {
            xcb_generic_event_t* ev = xcb_poll_for_event(conn);
            if (!ev) {
                // null means "no event right now". A real disconnect
                // shows up through has_error.
                if (xcb_connection_has_error(conn)) notify_disconnect_all();
                break; // NOT return — flush + key repeat must still run
            }
            x11_dispatch_one(ev);
        }
        xcb_flush(conn);
        check_key_repeat_all();
    }
#endif   // KNST_USING_LINUX_PLATFORM_X11

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    static void wayland_fatal(const char* where) noexcept {
        // One-shot log + dispatch. Otherwise every frame emits a new
        // DISCONNECT event -> the ring overflows.
        static bool s_already_fatal = false;
        if (s_already_fatal) return;
        s_already_fatal = true;

        KNST_LOG_ERROR("[knst] wayland connection lost (%s) errno=%d proto_err=%d\n",where, errno,  wl_display_get_error(KnstWindowSources::wayland_display));
        notify_disconnect_all();
    }

    static void block_pool_wayland() noexcept {
        wl_display* d = KnstWindowSources::wayland_display;
        if (!d) return;
        if (wl_display_dispatch(d) < 0) { wayland_fatal("dispatch"); return; }
        check_key_repeat_all();
    }

    static void non_block_pool_wayland() noexcept {
        wl_display* d = KnstWindowSources::wayland_display;
        if (!d) return;

        // If the connection is already dead, do nothing.
        if (wl_display_get_error(d) != 0) return;

        if (wl_display_dispatch_pending(d) < 0) {
            wayland_fatal("dispatch_pending"); return;
        }

        if (wl_display_prepare_read(d) != 0) {
            if (wl_display_dispatch_pending(d) < 0) {
                wayland_fatal("dispatch_pending(2)"); return;
            }
            check_key_repeat_all();
            return;
        }

        errno = 0;
        if (wl_display_flush(d) == -1 && errno != EAGAIN) {
            wl_display_cancel_read(d);
            wayland_fatal("flush");
            return;
        }

        struct pollfd pfd{};
        pfd.fd = wl_display_get_fd(d);
        pfd.events = POLLIN;
        const int ret = poll(&pfd, 1, 0);

        // HUP/ERR check must be OUTSIDE the POLLIN check — the old code
        // had it inside, so it was unreachable.
        if (ret > 0 && (pfd.revents & (POLLHUP | POLLERR))) {
            wl_display_cancel_read(d);
            wayland_fatal("poll");
            return;
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            if (wl_display_read_events(d) < 0)      { wayland_fatal("read_events"); return; }
            if (wl_display_dispatch_pending(d) < 0) { wayland_fatal("dispatch_pending(3)"); return; }
        } else {
            wl_display_cancel_read(d);
        }
        check_key_repeat_all();
    }
#endif   // KNST_USING_LINUX_PLATFORM_WAYLAND

public:

    KNST_FORCE_INLINE static size_t get_window_count() noexcept {
        return windows.size();
    }

    KNST_FORCE_INLINE static void check_key_repeat(knst_window& window) noexcept {
        auto& cur    = window.m_knst_event;
        uint32_t now = KnstWindowSources::get_current_time_ms();

        // Modifier keys must not produce repeat events.
        auto is_modifier = [](int kc) noexcept -> bool {
        #if KNST_USING_PLATFORM_WINDOWS
            return kc == VK_SHIFT   || kc == VK_LSHIFT  || kc == VK_RSHIFT
                || kc == VK_CONTROL || kc == VK_LCONTROL|| kc == VK_RCONTROL
                || kc == VK_MENU    || kc == VK_LMENU   || kc == VK_RMENU
                || kc == VK_LWIN    || kc == VK_RWIN
                || kc == VK_CAPITAL || kc == VK_NUMLOCK || kc == VK_SCROLL;
        #elif KNST_USING_LINUX_PLATFORM_X11
            return kc == XK_Shift_L   || kc == XK_Shift_R
                || kc == XK_Control_L || kc == XK_Control_R
                || kc == XK_Alt_L     || kc == XK_Alt_R
                || kc == XK_Super_L   || kc == XK_Super_R
                || kc == XK_Caps_Lock || kc == XK_Num_Lock || kc == XK_Scroll_Lock;
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            return kc == XKB_KEY_Shift_L   || kc == XKB_KEY_Shift_R
                || kc == XKB_KEY_Control_L || kc == XKB_KEY_Control_R
                || kc == XKB_KEY_Alt_L     || kc == XKB_KEY_Alt_R
                || kc == XKB_KEY_Super_L   || kc == XKB_KEY_Super_R
                || kc == XKB_KEY_Caps_Lock || kc == XKB_KEY_Num_Lock || kc == XKB_KEY_Scroll_Lock;
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            // Android keycodes are offset (KNST_ANDROID_KEY_OFFSET in
            // input_callback) to dodge ASCII clashes. Modifiers come in
            // with the same offset — undo it before comparing.
            const int b = kc - KNST_ANDROID_KEY_OFFSET;
            return b == AKEYCODE_SHIFT_LEFT  || b == AKEYCODE_SHIFT_RIGHT
                || b == AKEYCODE_CTRL_LEFT   || b == AKEYCODE_CTRL_RIGHT
                || b == AKEYCODE_ALT_LEFT    || b == AKEYCODE_ALT_RIGHT
                || b == AKEYCODE_META_LEFT   || b == AKEYCODE_META_RIGHT
                || b == AKEYCODE_CAPS_LOCK   || b == AKEYCODE_NUM_LOCK
                || b == AKEYCODE_SCROLL_LOCK;
        #else
            (void)kc;
            return false;
        #endif
        };

        for (auto& hk : window.m_held_keys) {
            if (!hk.active) continue;
            if (is_modifier(hk.key_code)) continue;

            bool should_fire = false;

            if (!hk.repeat_initialized) {
                if (now - hk.last_key_time >= knst_window_event::KEY_REPEAT_DELAY) {
                    hk.repeat_initialized = true;
                    hk.last_repeat_time   = now;
                    should_fire = true;
                }
            } else {
                if (now - hk.last_repeat_time >= knst_window_event::KEY_REPEAT_INTERVAL) {
                    hk.last_repeat_time = now;
                    should_fire = true;
                }
            }

            if (!should_fire) continue;

            // TEXT_INPUT repeat (Wayland + Android). X11 already sends
            // repeats from the server, so utf8_len stays 0 there.
            if (hk.utf8_len > 0) {
                knst_window_event te = cur;
                te.type = KNST_WINDOW_EVENT_TEXT_INPUT;
                te.timestamp_ms = now;
                te.text_input = knst_c16string(hk.utf8, (uint32_t)hk.utf8_len);
                window.m_events.push(te);
            }

            knst_window_event repeat_ev = cur;
            repeat_ev.type = KNST_WINDOW_EVENT_KEYBOARD;
            repeat_ev.key_action = KNST_WINDOW_KEY_ACTION_REPEAT;
            repeat_ev.key_code = hk.key_code;
            repeat_ev.scancode  = hk.scancode;
            repeat_ev.timestamp_ms = now;
            window.m_events.push(repeat_ev);
        }
    }

    static void block_pool_event() noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        MSG msg;
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        // -1 = error, 0 = WM_QUIT, >0 = message available.
        // The old code used `if (GetMessageW(...))`, so -1 was truthy
        // and DispatchMessageW got called with garbage.
        if (r > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else if (r == 0) {
            const uint32_t n = (uint32_t)windows.size();
            for (uint32_t i = 0; i < n; ++i) windows[i]->should_close();
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        check_key_repeat_all();

    #elif KNST_USING_LINUX_PLATFORM_X11 || KNST_USING_LINUX_PLATFORM_WAYLAND
        #if KNST_IS_DUAL_BACKEND
            if (KnstWindowSources::s_active_backend == knst_backend::x11) return block_pool_x11();
            if (KnstWindowSources::s_active_backend == knst_backend::wayland) return block_pool_wayland();
        #elif KNST_USING_LINUX_PLATFORM_X11
            return block_pool_x11();
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            return block_pool_wayland();
        #endif

    #elif defined(KNST_USING_PLATFORM_ANDROID)
        int events;
        struct android_poll_source* source = nullptr;
        int result = ALooper_pollOnce(-1, nullptr, &events, (void**)&source);
        if (result >= 0 && source != nullptr && source->process != nullptr) {
            source->process(KnstWindowSources::m_app, source);
        }
        check_key_repeat_all();
    #endif
    }

    static void non_block_pool_event() noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        check_key_repeat_all();

    #elif KNST_USING_LINUX_PLATFORM_X11 || KNST_USING_LINUX_PLATFORM_WAYLAND
        #if KNST_IS_DUAL_BACKEND
            if (KnstWindowSources::s_active_backend == knst_backend::x11) return non_block_pool_x11();
            if (KnstWindowSources::s_active_backend == knst_backend::wayland) return non_block_pool_wayland();
        #elif KNST_USING_LINUX_PLATFORM_X11
            return non_block_pool_x11();
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            return non_block_pool_wayland();
        #endif

    #elif defined(KNST_USING_PLATFORM_ANDROID)
        const bool has_window = KnstWindowSources::m_app && KnstWindowSources::m_app->window;

        // Block the looper when backgrounded, spin when we have a
        // window. Prevents 100% CPU while invisible.
        int timeout = has_window ? 0 : -1;

        for (int k = 0; k < 64; ++k) {
            int events;
            struct android_poll_source* source = nullptr;

            if (ALooper_pollOnce(timeout, nullptr, &events,(void**)&source) < 0) {
                break;
            }
            if (source != nullptr && source->process != nullptr) {
                source->process(KnstWindowSources::m_app, source);
            }
            timeout = 0;
        }

        check_key_repeat_all();
    #endif
    }
};


#if KNST_USING_PLATFORM_WINDOWS
    #include "../windows/knst_window_win32_event_manager.hpp"
#endif
#if KNST_USING_LINUX_PLATFORM_X11
    #include "../linux/x11/knst_window_x11_event_manager.hpp"
#endif
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    #include "../linux/wayland/knst_window_wayland_event_manager.hpp"
#endif
#if defined(KNST_USING_PLATFORM_ANDROID)
    #include "../android/knst_window_android_event_manager.hpp"
#endif