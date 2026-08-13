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

    private:

        static knst_vector<knst_window*> windows;
        
        #if KNST_USING_LINUX_PLATFORM_X11
        KNST_FORCE_INLINE static knst_window* find_window(xcb_window_t id) noexcept {
            for (size_t i = 0; i < windows.size(); i++) {
                if (windows[i]->get_x11_window_handle() == id) {
                    return windows[i];
                }
            }
            return nullptr;
        }
        #endif


    public:

        KNST_FORCE_INLINE static void register_window(knst_window* window) noexcept {
            windows.push_back(window);
        }
        
        KNST_FORCE_INLINE static void unregister_window(knst_window* window) noexcept {
            for (size_t i = 0; i < windows.size(); i++) {
                if (windows[i] == window) {
                    windows[i] = windows.back();
                    windows.pop_back();
                    return;
                }
            }
        }
            

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



    KNST_FORCE_INLINE static void block_pool_event() noexcept {
        #if KNST_USING_PLATFORM_WINDOWS
            MSG msg;
            if (GetMessageW(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        #elif KNST_USING_LINUX_PLATFORM_X11
           
            while (true) {
                xcb_generic_event_t* ev = xcb_wait_for_event(KnstWindowSources::m_connection);
                if (!ev) {

                    for (size_t i = 0; i < windows.size(); i++) {
                        windows[i]->m_knst_event.type = KNST_DISCONNECT;
                    }
                    return;
                }
                
               
                xcb_window_t target = XCB_NONE;
                uint8_t event_type = ev->response_type & ~0x80;
                
                                switch (event_type) {
                    case XCB_KEY_PRESS:
                    case XCB_KEY_RELEASE:
                    case XCB_BUTTON_PRESS:
                    case XCB_BUTTON_RELEASE:
                    case XCB_MOTION_NOTIFY:
                    case XCB_ENTER_NOTIFY:
                    case XCB_LEAVE_NOTIFY:
                    case XCB_FOCUS_IN:
                    case XCB_FOCUS_OUT:
                        target = ((xcb_key_press_event_t*)ev)->event;
                        break;

                    case XCB_CONFIGURE_NOTIFY:
                        target = ((xcb_configure_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_EXPOSE:
                        target = ((xcb_expose_event_t*)ev)->window;
                        break;
                        
                    case XCB_CLIENT_MESSAGE:
                        target = ((xcb_client_message_event_t*)ev)->window; 
                        break;
                        
                    case XCB_PROPERTY_NOTIFY:
                        target = ((xcb_property_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_VISIBILITY_NOTIFY:
                        target = ((xcb_visibility_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_MAP_NOTIFY:
                        target = ((xcb_map_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_UNMAP_NOTIFY:
                        target = ((xcb_unmap_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_DESTROY_NOTIFY:
                        target = ((xcb_destroy_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_SELECTION_NOTIFY:
                        target = ((xcb_selection_notify_event_t*)ev)->requestor;
                        break;
                        
                    case XCB_SELECTION_REQUEST:
                        target = ((xcb_selection_request_event_t*)ev)->owner;
                        break;
                        
                    default:
                        target = XCB_NONE;
                        break;
                }
                
                
                if (target == XCB_NONE) {
                 
                    if (!windows.empty()) {
                        load_native_to_knst_event(*windows[0], ev);
                    } else {
                        free(ev);
                    }
                    break;
                } else {
                    knst_window* target_window = find_window(target);
                    if (target_window) {
                        load_native_to_knst_event(*target_window, ev);
                        break; 
                    } else {
                        free(ev);
                        
                    }
                }
            }
            
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            wl_display_dispatch(KnstWindowSources::wayland_display);
            for (size_t i = 0; i < windows.size(); i++) {
                check_key_repeat(*windows[i]);
            }
        #endif

        #ifndef KNST_DISABLE_REDRAW_ON_EVENT_MANAGER
            for (size_t i = 0; i < windows.size(); i++) {
                windows[i]->call_redraw_callback();
            };
        #endif
    }

    KNST_FORCE_INLINE static void non_block_pool_event() noexcept {
        #if KNST_USING_PLATFORM_WINDOWS
            MSG msg;
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            
        #elif KNST_USING_LINUX_PLATFORM_X11
           
            while (true) {
                xcb_generic_event_t* ev = xcb_poll_for_event(KnstWindowSources::m_connection);
                if (!ev) {
                    break;
                }
                
              
                xcb_window_t target = XCB_NONE;
                uint8_t event_type = ev->response_type & ~0x80;
                
                switch (event_type) {
                    case XCB_KEY_PRESS:
                    case XCB_KEY_RELEASE:
                    case XCB_BUTTON_PRESS:
                    case XCB_BUTTON_RELEASE:
                    case XCB_MOTION_NOTIFY:
                    case XCB_ENTER_NOTIFY:
                    case XCB_LEAVE_NOTIFY:
                    case XCB_FOCUS_IN:
                    case XCB_FOCUS_OUT:
                        target = ((xcb_key_press_event_t*)ev)->event;
                        break;

                    case XCB_CONFIGURE_NOTIFY:
                        target = ((xcb_configure_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_EXPOSE:
                        target = ((xcb_expose_event_t*)ev)->window;
                        break;
                        
                    case XCB_CLIENT_MESSAGE:
                        target = ((xcb_client_message_event_t*)ev)->window; 
                        break;
                        
                    case XCB_PROPERTY_NOTIFY:
                        target = ((xcb_property_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_VISIBILITY_NOTIFY:
                        target = ((xcb_visibility_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_MAP_NOTIFY:
                        target = ((xcb_map_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_UNMAP_NOTIFY:
                        target = ((xcb_unmap_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_DESTROY_NOTIFY:
                        target = ((xcb_destroy_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_SELECTION_NOTIFY:
                        target = ((xcb_selection_notify_event_t*)ev)->requestor;
                        break;
                        
                    case XCB_SELECTION_REQUEST:
                        target = ((xcb_selection_request_event_t*)ev)->owner;
                        break;
                        
                    default:
                        target = XCB_NONE;
                        break;
                }
                
                
                if (target == XCB_NONE) {
                    
                    if (!windows.empty()) {
                        load_native_to_knst_event(*windows[0], ev);
                    } else {
                        free(ev);
                    }
                } else {
                    knst_window* target_window = find_window(target);
                    if (target_window) {
                        load_native_to_knst_event(*target_window, ev);
                    } else {
                        free(ev);
                    }
                }
            }
            
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            
            
            while (wl_display_prepare_read(KnstWindowSources::wayland_display) != 0) {
                wl_display_dispatch_pending(KnstWindowSources::wayland_display);
            }
            
            errno = 0;
            if (wl_display_flush(KnstWindowSources::wayland_display) == -1 && errno == EPIPE) {
                wl_display_cancel_read(KnstWindowSources::wayland_display);
                for (size_t i = 0; i < windows.size(); i++) {
                    windows[i]->m_knst_event.type = KNST_DISCONNECT;
                }
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
                    for (size_t i = 0; i < windows.size(); i++) {
                        windows[i]->m_knst_event.type = KNST_DISCONNECT;
                    }
                    return;
                }
                
                if (pfd.revents & POLLIN) {
                    wl_display_read_events(KnstWindowSources::wayland_display);
                    wl_display_dispatch_pending(KnstWindowSources::wayland_display);
                }
            } else {
                wl_display_cancel_read(KnstWindowSources::wayland_display);
            }
      
            for (size_t i = 0; i < windows.size(); i++) {
                check_key_repeat(*windows[i]);
            }
        #endif

        #ifndef KNST_DISABLE_REDRAW_ON_EVENT_MANAGER
            for (size_t i = 0; i < windows.size(); i++) {
                windows[i]->call_redraw_callback();
            }
        #endif
    }

    KNST_FORCE_INLINE static void timeout_pool_event(int timeout_ms = 16) noexcept {
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
                for (size_t i = 0; i < windows.size(); i++) {
                    windows[i]->m_knst_event.type = KNST_DISCONNECT;
                }
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
              
                while (true) {
                    xcb_generic_event_t* ev = xcb_poll_for_event(KnstWindowSources::m_connection);
                    if (!ev) {
                        break;
                    }
                    
                   
                    xcb_window_t target = XCB_NONE;
                    uint8_t event_type = ev->response_type & ~0x80;
                    
                                    switch (event_type) {
                    case XCB_KEY_PRESS:
                    case XCB_KEY_RELEASE:
                    case XCB_BUTTON_PRESS:
                    case XCB_BUTTON_RELEASE:
                    case XCB_MOTION_NOTIFY:
                    case XCB_ENTER_NOTIFY:
                    case XCB_LEAVE_NOTIFY:
                    case XCB_FOCUS_IN:
                    case XCB_FOCUS_OUT:
                        target = ((xcb_key_press_event_t*)ev)->event;
                        break;

                    case XCB_CONFIGURE_NOTIFY:
                        target = ((xcb_configure_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_EXPOSE:
                        target = ((xcb_expose_event_t*)ev)->window;
                        break;
                        
                    case XCB_CLIENT_MESSAGE:
                        target = ((xcb_client_message_event_t*)ev)->window; 
                        break;
                        
                    case XCB_PROPERTY_NOTIFY:
                        target = ((xcb_property_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_VISIBILITY_NOTIFY:
                        target = ((xcb_visibility_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_MAP_NOTIFY:
                        target = ((xcb_map_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_UNMAP_NOTIFY:
                        target = ((xcb_unmap_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_DESTROY_NOTIFY:
                        target = ((xcb_destroy_notify_event_t*)ev)->window;
                        break;
                        
                    case XCB_SELECTION_NOTIFY:
                        target = ((xcb_selection_notify_event_t*)ev)->requestor;
                        break;
                        
                    case XCB_SELECTION_REQUEST:
                        target = ((xcb_selection_request_event_t*)ev)->owner;
                        break;
                        
                    default:
                        target = XCB_NONE;
                        break;
                }
                    
                    
                    if (target == XCB_NONE) {
                        if (!windows.empty()) {
                            load_native_to_knst_event(*windows[0], ev);
                        } else {
                            free(ev);
                        }
                    } else {
                        knst_window* target_window = find_window(target);
                        if (target_window) {
                            load_native_to_knst_event(*target_window, ev);
                        } else {
                            free(ev);
                        }
                    }
                }
            }

        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            while (wl_display_prepare_read(KnstWindowSources::wayland_display) != 0) {
                wl_display_dispatch_pending(KnstWindowSources::wayland_display);
            }
            
            errno = 0;
            if (wl_display_flush(KnstWindowSources::wayland_display) == -1 && errno == EPIPE) {
                wl_display_cancel_read(KnstWindowSources::wayland_display);
                for (size_t i = 0; i < windows.size(); i++) {
                    windows[i]->m_knst_event.type = KNST_DISCONNECT;
                }
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
                    for (size_t i = 0; i < windows.size(); i++) {
                        windows[i]->m_knst_event.type = KNST_DISCONNECT;
                    }
                    return;
                }
                
                if (pfd.revents & POLLIN) {
                    wl_display_read_events(KnstWindowSources::wayland_display);
                    wl_display_dispatch_pending(KnstWindowSources::wayland_display);
                }
            } else {
                wl_display_cancel_read(KnstWindowSources::wayland_display);
                wl_display_dispatch_pending(KnstWindowSources::wayland_display);
            }
            
            
            for (size_t i = 0; i < windows.size(); i++) {
                check_key_repeat(*windows[i]);
            }
        #endif
        
        #ifndef KNST_DISABLE_REDRAW_ON_EVENT_MANAGER
            for (size_t i = 0; i < windows.size(); i++) {
                windows[i]->call_redraw_callback();
            }
        #endif
        }
};


knst_vector<knst_window*> knst_window_event_system::windows;

#endif // KNST_WINDOW_EVENT_SYSTEM_HPP
