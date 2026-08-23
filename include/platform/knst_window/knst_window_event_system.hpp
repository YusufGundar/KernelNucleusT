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
#elif defined(KNST_USING_PLATFORM_ANDROID)

    #include "../android/knst_window_android_event_manager.hpp"

#endif

struct knst_window_event_system {

    private:
        friend class knst_window;
        static inline knst_vector<knst_window*> windows;
        
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




    public:

        
            

       KNST_FORCE_INLINE static void check_key_repeat(knst_window& window) noexcept {
    auto& ev = window.m_knst_event;
    uint32_t now = KnstWindowSources::get_current_time_ms();

    for (auto& hk : ev.m_held_keys) {
        if (!hk.active) continue;

        bool should_fire = false;
        if (!hk.repeat_initialized) {
            if (now - hk.last_key_time >= knst_window_event::KEY_REPEAT_DELAY) {
                hk.repeat_initialized = true;
                hk.last_repeat_time = now;
                should_fire = true;
            }
        } else {
            if (now - hk.last_repeat_time >= knst_window_event::KEY_REPEAT_INTERVAL) {
                hk.last_repeat_time = now;
                should_fire = true;
            }
        }

        if (should_fire) {
            knst_window_event repeat_ev = ev;
            repeat_ev.type = KNST_KEYBOARD_EVENT;
            repeat_ev.key_action = KNST_KEY_REPEAT;
            repeat_ev.key_code = hk.key_code;
            repeat_ev.scancode = hk.scancode;
            window.push_keyboard_event(repeat_ev);
        }
    }
}

  
    KNST_FORCE_INLINE static void block_pool_event() noexcept {
        #if KNST_USING_PLATFORM_WINDOWS
            MSG msg;
            // İlk mesajı bloklayarak bekle (bu fonksiyonun "block" sözleşmesi bu).
            if (GetMessageW(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            // Kuyrukta o an bekleyen TÜM diğer mesajları da bu frame'de boşalt.
            // Tek mesajla yetinmek (eski davranış) mesajların kuyrukta birikip
            // frame başına bir tane sızmasına, dolayısıyla girdi/çizimde
            // "takıla takıla" hissine yol açıyordu — X11/Wayland zaten kuyruğu
            // tamamen boşaltıyor, Windows'u da aynı ritme getiriyoruz.
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
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
            

        #elif defined(KNST_USING_PLATFORM_ANDROID)
                   
                
                    int events;
                    struct android_poll_source* source = nullptr;
                    
                    
                    int result = ALooper_pollOnce(-1, nullptr, &events, (void**)&source);
                    
                    if (result < 0) {
                        
                        return;
                    }
                    
                    if (source != nullptr && source->process != nullptr) {
                        source->process(KnstWindowSources::m_app, source);
                    }
                    

        #endif
            for (size_t i = 0; i < windows.size(); i++) {
                check_key_repeat(*windows[i]);
            }
        
    }


  
    KNST_FORCE_INLINE static void non_block_pool_event() noexcept {
        #if KNST_USING_PLATFORM_WINDOWS
            MSG msg;
            // Kuyrukta o an bekleyen TÜM mesajları bu frame'de boşalt.
            // Önceki hâlde sadece 1 mesaj işleniyordu (if yerine while
            // olmalıydı) — bu da yoğun girdi/çizim trafiğinde mesajların
            // kuyrukta birikip frame başına tek tek sızmasına, dolayısıyla
            // gözle görülür "takılma"ya sebep oluyordu.
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            
            for (size_t i = 0; i < windows.size(); i++) {
                check_key_repeat(*windows[i]);
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

            for (size_t i = 0; i < windows.size(); i++) {
                check_key_repeat(*windows[i]);
            }
            
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            
            
        
      
        wl_display_dispatch_pending(KnstWindowSources::wayland_display);
        
       
        if (wl_display_prepare_read(KnstWindowSources::wayland_display) != 0) {

            wl_display_dispatch_pending(KnstWindowSources::wayland_display);

            for (size_t i = 0; i < windows.size(); i++) {
                check_key_repeat(*windows[i]);
            }
            return;
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
        
        if (ret > 0 && (pfd.revents & POLLIN)) {
            if (pfd.revents & (POLLHUP | POLLERR)) {
                wl_display_cancel_read(KnstWindowSources::wayland_display);
                for (size_t i = 0; i < windows.size(); i++) {
                    windows[i]->m_knst_event.type = KNST_DISCONNECT;
                }
                return;
            }
            
            wl_display_read_events(KnstWindowSources::wayland_display);
            wl_display_dispatch_pending(KnstWindowSources::wayland_display);
        } else {
            wl_display_cancel_read(KnstWindowSources::wayland_display);
        }

        for (size_t i = 0; i < windows.size(); i++) {
            check_key_repeat(*windows[i]);
        }
   


        #elif defined(KNST_USING_PLATFORM_ANDROID)
            
        
            int events;
            struct android_poll_source* source = nullptr;
            
            
            int result = ALooper_pollOnce(0, nullptr, &events, (void**)&source);
            
            if (result < 0) {
                
                return;
            }
            
            if (source != nullptr && source->process != nullptr) {
                source->process(KnstWindowSources::m_app, source);
            }

            for (size_t i = 0; i < windows.size(); i++) {
                check_key_repeat(*windows[i]);
            }

        #endif

            
    }

};





#endif // KNST_WINDOW_EVENT_SYSTEM_HPP