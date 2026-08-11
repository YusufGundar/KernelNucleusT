#ifndef KNST_WINDOW_EVENT_SYSTEM_HPP
#define KNST_WINDOW_EVENT_SYSTEM_HPP
#pragma once

#if KNST_USING_PLATFORM_WINDOWS
    #include "../windows/knst_window_win32_event_manager.hpp"
#elif KNST_USING_LINUX_PLATFORM_X11
    #include "../linux/x11/knst_window_x11_event_manager.hpp"
    #include <poll.h>
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    #include <poll.h>
    #include <unistd.h>
    #include <errno.h>

#endif

struct knst_window_event_system {


        #if KNST_USING_LINUX_PLATFORM_WAYLAND
        KNST_FORCE_INLINE static uint32_t get_current_time_ms() noexcept {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        }

        KNST_FORCE_INLINE static void check_key_repeat(knst_window& window) noexcept {
            auto& ev = window.m_knst_event;
            
            if (!ev.m_key_held || ev.m_last_scancode == 0) {
                return;
            }
            
            uint32_t current_time = get_current_time_ms();
            
            if (!ev.m_repeat_initialized) {
                if (current_time - ev.m_last_key_time >= ev.KEY_REPEAT_DELAY) {
                    ev.type = KNST_KEYBOARD_EVENT;
                    ev.key_action = KNST_KEY_REPEAT;
                    ev.key_code = ev.m_last_key;
                    ev.scancode = ev.m_last_scancode;
                    
                    ev.m_repeat_initialized = true;
                    ev.m_last_repeat_time = current_time;
                }
            }
            else {
                if (current_time - ev.m_last_repeat_time >= ev.KEY_REPEAT_INTERVAL) {
                    ev.type = KNST_KEYBOARD_EVENT;
                    ev.key_action = KNST_KEY_REPEAT;
                    ev.key_code = ev.m_last_key;
                    ev.scancode = ev.m_last_scancode;
                    
                    ev.m_last_repeat_time = current_time;
                }
            }
        }
    #endif



    KNST_FORCE_INLINE static void block_pool_event(knst_window& window) noexcept {
        #if KNST_USING_PLATFORM_WINDOWS
            MSG msg;
            if (GetMessageW(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        #elif KNST_USING_LINUX_PLATFORM_X11
            load_native_to_knst_event(window, xcb_wait_for_event(KnstWindowSources::m_connection));

        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
             window.m_knst_event.type = KNST_UNKNOWN;
            wl_display_dispatch(KnstWindowSources::wayland_display);
            check_key_repeat(window);
        #endif

        #ifndef KNST_DISABLE_REDRAW_ON_EVENT_MANAGER
            window.call_redraw_callback();
        #endif
    }

    KNST_FORCE_INLINE static void non_block_pool_event(knst_window& window) noexcept {
        #if KNST_USING_PLATFORM_WINDOWS
            MSG msg;
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            
        #elif KNST_USING_LINUX_PLATFORM_X11
            xcb_generic_event_t* ev = xcb_poll_for_event(KnstWindowSources::m_connection);
            if (ev) {
                load_native_to_knst_event(window, ev);
            } else if (xcb_connection_has_error(KnstWindowSources::m_connection)) {
                window.m_knst_event.type = KNST_DISCONNECT;
                return;
            }

        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
           window.m_knst_event.type = KNST_UNKNOWN;
            
            while (wl_display_prepare_read(KnstWindowSources::wayland_display) != 0) {
                wl_display_dispatch_pending(KnstWindowSources::wayland_display);
            }
            
            errno = 0;
            if (wl_display_flush(KnstWindowSources::wayland_display) == -1 && errno == EPIPE) {
                wl_display_cancel_read(KnstWindowSources::wayland_display);
                window.m_knst_event.type = KNST_DISCONNECT;
                return;
            }
         
            struct pollfd pfd;
            pfd.fd = wl_display_get_fd(KnstWindowSources::wayland_display);
            pfd.events = POLLIN;
            pfd.revents = 0;
            
            int ret = poll(&pfd, 1, 0);
            
            if (ret > 0) {
                if (pfd.revents & (POLLHUP | POLLERR)) {
                    wl_display_cancel_read(KnstWindowSources::wayland_display);
                    window.m_knst_event.type = KNST_DISCONNECT;
                    return;
                }
                
                if (pfd.revents & POLLIN) {
                    wl_display_read_events(KnstWindowSources::wayland_display);
                    wl_display_dispatch_pending(KnstWindowSources::wayland_display);
                }
            } else {
                wl_display_cancel_read(KnstWindowSources::wayland_display);
            }
            
            check_key_repeat(window);
        #endif

        #ifndef KNST_DISABLE_REDRAW_ON_EVENT_MANAGER
            window.call_redraw_callback();
        #endif
    }

    KNST_FORCE_INLINE static void timeout_pool_event(knst_window& window, int timeout_ms = 16) noexcept {
        #if KNST_USING_PLATFORM_WINDOWS
            if (MsgWaitForMultipleObjects(0, nullptr, FALSE, timeout_ms, QS_ALLINPUT) == WAIT_OBJECT_0) {
                MSG msg;
                if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            
        #elif KNST_USING_LINUX_PLATFORM_X11
            if (xcb_connection_has_error(KnstWindowSources::m_connection)) {
                window.m_knst_event.type = KNST_DISCONNECT;
                return;
            }
            
            int xcb_fd = xcb_get_file_descriptor(KnstWindowSources::m_connection);
            struct pollfd pfd;
            pfd.fd = xcb_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            
            if (poll(&pfd, 1, timeout_ms) == 0) {
                return;
            }
            
            if (pfd.revents & POLLIN) {
                xcb_generic_event_t* ev = xcb_poll_for_event(KnstWindowSources::m_connection);
                if (ev) {
                    load_native_to_knst_event(window, ev);
                }
            }

        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            window.m_knst_event.type = KNST_UNKNOWN;
            
            while (wl_display_prepare_read(KnstWindowSources::wayland_display) != 0) {
                wl_display_dispatch_pending(KnstWindowSources::wayland_display);
            }
            
            errno = 0;
            if (wl_display_flush(KnstWindowSources::wayland_display) == -1 && errno == EPIPE) {
                wl_display_cancel_read(KnstWindowSources::wayland_display);
                window.m_knst_event.type = KNST_DISCONNECT;
                return;
            }
       
            struct pollfd pfd;
            pfd.fd = wl_display_get_fd(KnstWindowSources::wayland_display);
            pfd.events = POLLIN;
            pfd.revents = 0;
            
            int ret = poll(&pfd, 1, timeout_ms);
            
            if (ret > 0) {
                if (pfd.revents & (POLLHUP | POLLERR)) {
                    wl_display_cancel_read(KnstWindowSources::wayland_display);
                    window.m_knst_event.type = KNST_DISCONNECT;
                    return;
                }
                
                if (pfd.revents & POLLIN) {
                    wl_display_read_events(KnstWindowSources::wayland_display);
                    wl_display_dispatch_pending(KnstWindowSources::wayland_display);
                }
            } else {
                if (ret == 0) {
                    wl_display_cancel_read(KnstWindowSources::wayland_display);
                } else {
                    wl_display_cancel_read(KnstWindowSources::wayland_display);
                }
                wl_display_dispatch_pending(KnstWindowSources::wayland_display);
            }
            
            check_key_repeat(window);
        #endif
        
        #ifndef KNST_DISABLE_REDRAW_ON_EVENT_MANAGER
            window.call_redraw_callback();
        #endif
    }
};

#endif // KNST_WINDOW_EVENT_SYSTEM_HPP