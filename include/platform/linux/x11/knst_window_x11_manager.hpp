#ifndef KNST_WINDOW_X11_MANAGER_HPP
#define KNST_WINDOW_X11_MANAGER_HPP
#pragma once


#if KNST_USING_LINUX_PLATFORM_X11


#include <X11/Xutil.h>
#include <xcb/xcb_icccm.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>


inline void knst_window::creation() noexcept{

    const xcb_setup_t* setup = xcb_get_setup(KnstWindowSources::m_connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    xcb_screen_t* screen = iter.data;

    m_window = xcb_generate_id(KnstWindowSources::m_connection);
    
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[2] = {
        screen->white_pixel,
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

    #ifdef KNST_DISABLE_TITLE_BAR
        xcb_create_window(
            KnstWindowSources::m_connection,
            0,                          
            m_window,
            screen->root,
            m_knst_event.window_root_x, m_knst_event.window_root_y,                   
            m_knst_event.window_width, m_knst_event.window_height,                  
            0,                          
            XCB_WINDOW_CLASS_INPUT_OUTPUT,
            screen->root_visual,
            mask,
            values
        );

     
        struct KnstMotifWmHints {
            uint32_t flags;
            uint32_t functions;
            uint32_t decorations;
            int32_t  input_mode;
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
    #else
        xcb_intern_atom_cookie_t type_cookie = xcb_intern_atom(KnstWindowSources::m_connection, 0, strlen("_NET_WM_WINDOW_TYPE"), "_NET_WM_WINDOW_TYPE");
        xcb_intern_atom_reply_t* type_reply = xcb_intern_atom_reply(KnstWindowSources::m_connection, type_cookie, nullptr);

        xcb_intern_atom_cookie_t normal_cookie = xcb_intern_atom(KnstWindowSources::m_connection, 0, strlen("_NET_WM_WINDOW_TYPE_NORMAL"), "_NET_WM_WINDOW_TYPE_NORMAL");
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
            free(type_reply);
            free(normal_reply);
        }

        xcb_create_window(
            KnstWindowSources::m_connection,
            0,                          
            m_window,
            screen->root,
            m_knst_event.window_root_x, m_knst_event.window_root_y,                   
            m_knst_event.window_width, m_knst_event.window_height,                  
            0,                          
            XCB_WINDOW_CLASS_INPUT_OUTPUT,
            screen->root_visual,
            mask,
            values
        );
    #endif


    knst_byte_string title(m_title);
    xcb_change_property(
        KnstWindowSources::m_connection,
        XCB_PROP_MODE_REPLACE,
        m_window,
        XCB_ATOM_WM_NAME,
        KnstWindowSources::m_UTF8_STRING,
        8, title.length(), title.data()
    );

    xcb_intern_atom_cookie_t protocols_cookie = xcb_intern_atom(KnstWindowSources::m_connection, 0, 12, "WM_PROTOCOLS");
    xcb_intern_atom_reply_t* protocols_reply = xcb_intern_atom_reply(KnstWindowSources::m_connection, protocols_cookie, nullptr);
    if (protocols_reply) {
        xcb_change_property(
            KnstWindowSources::m_connection,
            XCB_PROP_MODE_REPLACE,
            m_window,
            protocols_reply->atom,   
            XCB_ATOM_ATOM,           
            32, 1, &KnstWindowSources::m_wmDelete
        );
        free(protocols_reply);
    }

   
    knst_window_event_system::register_window(this);
}


inline void knst_window::show() noexcept{

    if(m_window){

        xcb_map_window(KnstWindowSources::m_connection, m_window);
       
        if(m_knst_event.window_root_x > 0 || m_knst_event.window_root_y > 0){
            move(m_knst_event.window_root_x,m_knst_event.window_root_y);
        }
        xcb_flush(KnstWindowSources::m_connection);

    }


}

inline void knst_window::destroy() noexcept {
    knst_window_event_system::unregister_window(this);
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
    xcb_destroy_window(conn, m_window);
    xcb_flush(conn);
    m_window = 0;
}



inline void knst_window::set_title(const knst_c16string& title) noexcept {
    
        if (!m_window) return;
        
        knst_byte_string new_title(title);
        
        xcb_intern_atom_cookie_t net_cookie =  xcb_intern_atom(KnstWindowSources::m_connection, 0, 12, "_NET_WM_NAME");
           
        xcb_intern_atom_reply_t* net_reply =  xcb_intern_atom_reply(KnstWindowSources::m_connection, net_cookie, nullptr);
           
        
        if (net_reply) {
            xcb_change_property(
                KnstWindowSources::m_connection,
                XCB_PROP_MODE_REPLACE,
                m_window,
                net_reply->atom,     
                KnstWindowSources::m_UTF8_STRING,       
                8,
                new_title.length(),
                new_title.data()
            );
        }
        
        if (net_reply) free(net_reply);
        xcb_flush(KnstWindowSources::m_connection);
        m_title = title;
   
}


inline void knst_window::move(int root_x, int root_y, const knst_monitor& monitor) noexcept {
        
        if (!m_window) return;
        if (root_x < 0) {
            root_x = m_knst_event.window_root_x;
        }
        if (root_y < 0) {
            root_y = m_knst_event.window_root_y;
        }

        int absolute_x = monitor.root_x + root_x;
        int absolute_y = monitor.root_y + root_y;

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


inline void knst_window::move(int root_x, int root_y) noexcept {
        
        if (!m_window) return;
        if (root_x < 0) {
            root_x = m_knst_event.window_root_x;
        }
        if (root_y < 0) {
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


inline void knst_window::toggle_fullscreen(bool fullscreen) noexcept {
    if (!m_window) return;
    
    
    
    
    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_NET_WM_STATE;
    ev.data.data32[0] = fullscreen ? 1 : 0;
    ev.data.data32[1] = KnstWindowSources::m_NET_WM_STATE_FULLSCREEN;
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
    
    
    m_knst_event.is_full_screen = fullscreen;
}

inline void knst_window::set_minimized() noexcept {
    if (!m_window) return;

    if (m_knst_event.is_minimized) return;

    if (m_knst_event.is_maximized) {
        restore();
    }
    if (m_knst_event.is_full_screen) {
        toggle_fullscreen(false);
    }

    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_wmChangeState;
    ev.data.data32[0] = 3; 
    ev.data.data32[1] = 0;
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
}

inline void knst_window::set_maximized() noexcept {
    if (!m_window) return;
    
   
    
    
    if (m_knst_event.is_maximized) return;
    
   
    if (m_knst_event.is_minimized) {
        restore();
    }
    
   
    if (m_knst_event.is_full_screen) {
        toggle_fullscreen(false);
    }
    
    
    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_NET_WM_STATE;
    ev.data.data32[0] = 1; 
    ev.data.data32[1] = KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_HORZ;
    ev.data.data32[2] = KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_VERT;
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

inline void knst_window::restore() noexcept {
    if (!m_window) return;
    
    
    
    
    if (m_knst_event.is_full_screen) {
        toggle_fullscreen(false);
        return;
    }
    
  
    if (m_knst_event.is_minimized || m_knst_event.is_maximized) {
       
        xcb_client_message_event_t ev{};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 32;
        ev.window = m_window;
        ev.type = KnstWindowSources::m_NET_WM_STATE;
        ev.data.data32[0] = 0; 
        ev.data.data32[1] = KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_HORZ;
        ev.data.data32[2] = KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_VERT;
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
}




inline void knst_window::hide() noexcept {
    if (!m_window) return;
    
   
  
    xcb_unmap_window(KnstWindowSources::m_connection, m_window);
    xcb_flush(KnstWindowSources::m_connection);
}

inline void knst_window::focus() noexcept {
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




inline void knst_window::set_cursor(uint16_t cursor_type) noexcept {
    if (!m_window) return;
    
    Display* display = KnstWindowSources::m_xlib_display;
    if (!display) return;
    
   
    Cursor cursor = XCreateFontCursor(display, cursor_type);
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

inline void knst_window::apply_bmp_icon(const knst_byte_string& bytes, int icon_width, int icon_height) noexcept {
                                      
    
        if (!m_window || bytes.empty()) return;
        
       
        xcb_intern_atom_cookie_t cookie = 
            xcb_intern_atom(KnstWindowSources::m_connection,0, 12, "_NET_WM_ICON");
                            
        xcb_intern_atom_reply_t* reply = 
            xcb_intern_atom_reply(KnstWindowSources::m_connection,cookie, nullptr);
                                  
        
        if (!reply) return;
        
       
        uint32_t pixel_count = icon_width * icon_height;
        uint32_t* argb = new uint32_t[2 + pixel_count];
        argb[0] = icon_width;
        argb[1] = icon_height;
        
        const unsigned char* rgba = bytes.data();
        for (uint32_t i = 0; i < pixel_count; i++) {
            argb[2 + i] = (rgba[i*4+3] << 24) |
                          (rgba[i*4+0] << 16) |
                          (rgba[i*4+1] << 8)  |
                          (rgba[i*4+2]);
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


inline void knst_window::set_bmp_cursor(
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
    
    cursor_image->xhot = (hot_x >= 0 && hot_x < width) ? hot_x : width / 2;
    cursor_image->yhot = (hot_y >= 0 && hot_y < height) ? hot_y : height / 2;
    
    uint32_t* pixels = cursor_image->pixels;
    
    if (data_len == expected_4ch) {
        for (int i = 0; i < width * height; i++) {
            uint8_t r = pixel_data[i * 4 + 0];
            uint8_t g = pixel_data[i * 4 + 1];
            uint8_t b = pixel_data[i * 4 + 2];
            uint8_t a = pixel_data[i * 4 + 3];
            pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    } else if (data_len == expected_3ch) {
        for (int i = 0; i < width * height; i++) {
            uint8_t r = pixel_data[i * 3 + 0];
            uint8_t g = pixel_data[i * 3 + 1];
            uint8_t b = pixel_data[i * 3 + 2];
            pixels[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
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
}



inline void knst_window::reset_cursor() noexcept {
    if (!m_window) return;
    
    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;
    
  
    Display* display = KnstWindowSources::m_xlib_display;
    if (!display) return;
    
    
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


inline void knst_window::resize(int width, int height) noexcept {
    if (!m_window) return;
    
    
    if (width < 0) width = m_knst_event.window_width;
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
    
    m_knst_event.window_width = width;
    m_knst_event.window_height = height;
}

inline void knst_window::set_cursor_mode(int mode) noexcept {
    if (!m_window) return;
    
    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;
    
    Display* display = KnstWindowSources::m_xlib_display;
    if (!display) return;
    
    if (mode == KNST_CURSOR_NORMAL) {
        
        xcb_change_window_attributes(
            conn,
            m_window,
            XCB_CW_CURSOR,
            nullptr  
        );
        
        
        xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
        xcb_flush(conn);
        
    } else if (mode == KNST_CURSOR_HIDDEN) {
       
        Pixmap blank_pixmap = XCreatePixmap(display, DefaultRootWindow(display), 1, 1, 1);
        XColor dummy_color;
        dummy_color.red = 0;
        dummy_color.green = 0;
        dummy_color.blue = 0;
        
        Cursor hidden_cursor = XCreatePixmapCursor(
            display,
            blank_pixmap,
            blank_pixmap,
            &dummy_color,
            &dummy_color,
            0, 0
        );
        
        XFreePixmap(display, blank_pixmap);
        
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
        
    } else if (mode == KNST_CURSOR_DISABLED) {
       
        Pixmap blank_pixmap = XCreatePixmap(display, DefaultRootWindow(display), 1, 1, 1);
        XColor dummy_color;
        dummy_color.red = 0;
        dummy_color.green = 0;
        dummy_color.blue = 0;
        
        Cursor hidden_cursor = XCreatePixmapCursor(
            display,
            blank_pixmap,
            blank_pixmap,
            &dummy_color,
            &dummy_color,
            0, 0
        );
        
        XFreePixmap(display, blank_pixmap);
        
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
        
      
        int center_x = m_knst_event.window_width / 2;
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

inline void knst_window::set_cursor_pos_on_window(int x, int y) noexcept {
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

inline void knst_window::set_cursor_pos_global(int root_x, int root_y) noexcept {
    if (!m_window) return;
    
  
    if (root_x < 0) root_x = m_knst_event.window_root_x;
    if (root_y < 0) root_y = m_knst_event.window_root_y;
    
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

inline void knst_window::set_clipboard(const knst_c16string& text) noexcept {
   
        clipboard_text = text;
        
       
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

inline void knst_window::request_clipboard() noexcept {
   
        if (!m_window || !KnstWindowSources::m_CLIPBOARD) return;
        
        xcb_convert_selection(
            KnstWindowSources::m_connection,
            m_window,
            KnstWindowSources::m_CLIPBOARD,
            KnstWindowSources::m_UTF8_STRING,
            KnstWindowSources::m_PRIMARY,
            XCB_CURRENT_TIME
        );
        xcb_flush(KnstWindowSources::m_connection);
    
}

inline void knst_window::set_drag_drop_status(bool enabled) noexcept {
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

inline void knst_window::set_opacity(float opacity) noexcept {
        
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


inline void knst_window::set_attribute(int attribute, bool value) noexcept {
    if (!m_window) return;

    switch (attribute) {
        case KNST_WINDOW_ATTRIB_DECORATED: {
            xcb_atom_t motif_wm_hints = KnstWindowSources::m_MOTIF_WM_HINTS;

            struct {
                uint32_t flags;
                uint32_t functions;
                uint32_t decorations;
                int32_t input_mode;
                uint32_t status;
            } hints = {0};

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

        case KNST_WINDOW_ATTRIB_RESIZABLE: {
            xcb_atom_t wm_normal_hints;
            const char* name = "WM_NORMAL_HINTS";
            xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
                KnstWindowSources::m_connection, 0, (uint16_t)strlen(name), name);
            xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
                KnstWindowSources::m_connection, cookie, nullptr);
            if (reply) {
                wm_normal_hints = reply->atom;
                free(reply);

                xcb_size_hints_t hints = {};
                if (!value) {
                    hints.flags = XCB_ICCCM_SIZE_HINT_P_MIN_SIZE |
                                  XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
                    hints.min_width = m_knst_event.window_width;
                    hints.min_height = m_knst_event.window_height;
                    hints.max_width = m_knst_event.window_width;
                    hints.max_height = m_knst_event.window_height;
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

        case KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP: {
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

        case KNST_WINDOW_ATTRIB_TRANSPARENT: {
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


inline bool knst_window::get_attribute(int attribute) const noexcept {
    if (!m_window) return false;

    switch (attribute) {
        case KNST_WINDOW_ATTRIB_DECORATED: {
            if (m_draw_custom_title_bar) return false;

            xcb_atom_t motif_wm_hints = KnstWindowSources::m_MOTIF_WM_HINTS;

            xcb_get_property_cookie_t prop_cookie = xcb_get_property(
                KnstWindowSources::m_connection, 0, m_window,
                motif_wm_hints, motif_wm_hints, 0, 5);
            xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(
                KnstWindowSources::m_connection, prop_cookie, nullptr);

            if (prop_reply) {
                uint32_t* data = (uint32_t*)xcb_get_property_value(prop_reply);
                bool decorated = data[2] != 0;
                free(prop_reply);
                return decorated;
            }
            return true;
        }

        case KNST_WINDOW_ATTRIB_RESIZABLE:
            return true;

        case KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP:
            return false;

        case KNST_WINDOW_ATTRIB_TRANSPARENT:
            return m_input_transparent;
    }
    return false;
}





inline knst_window::knst_edge_zone knst_window::detect_edge_zone(int x, int y) const noexcept {
    if (!m_draw_custom_title_bar) return ZONE_NONE;

    int w = m_knst_event.window_width;
    int h = m_knst_event.window_height;
    int titlebar_h = get_title_bar_height();

    bool near_left = x < RESIZE_BORDER;
    bool near_right = x >= w - RESIZE_BORDER;
    bool near_top = y < RESIZE_BORDER;
    bool near_bottom = y >= h - RESIZE_BORDER;

    
    if (near_top && near_left && y <= titlebar_h) return ZONE_TOP_LEFT;
    if (near_top && near_right && y <= titlebar_h)return ZONE_TOP_RIGHT;
    
    
    if (near_bottom && near_left) return ZONE_BOTTOM_LEFT;
    if (near_bottom && near_right) return ZONE_BOTTOM_RIGHT;
    
   
    if (near_top && y <= titlebar_h)return ZONE_TOP;
    if (near_bottom) return ZONE_BOTTOM;
    if (near_left) return ZONE_LEFT;
    if (near_right)  return ZONE_RIGHT;
    
    return ZONE_NONE;
}


inline uint32_t knst_window::edge_to_moveresize_direction(knst_edge_zone zone) const noexcept {
    switch (zone) {
        case ZONE_TOP_LEFT: return 0;
        case ZONE_TOP: return 1;
        case ZONE_TOP_RIGHT:return 2;
        case ZONE_RIGHT: return 3;
        case ZONE_BOTTOM_RIGHT:return 4;
        case ZONE_BOTTOM: return 5;
        case ZONE_BOTTOM_LEFT: return 6;
        case ZONE_LEFT: return 7;
        default: return 8; // move
    }
}

inline unsigned int knst_window::edge_to_x11_cursor_shape(knst_edge_zone zone) const noexcept {
    switch (zone) {
        case ZONE_TOP:return XC_top_side;
        case ZONE_BOTTOM: return XC_bottom_side;
        case ZONE_LEFT:return XC_left_side;
        case ZONE_RIGHT: return XC_right_side;
        case ZONE_TOP_LEFT: return XC_top_left_corner;
        case ZONE_TOP_RIGHT: return XC_top_right_corner;
        case ZONE_BOTTOM_LEFT: return XC_bottom_left_corner;
        case ZONE_BOTTOM_RIGHT: return XC_bottom_right_corner;
        default: return XC_left_ptr;
    }
}

inline void knst_window::update_edge_cursor(int x, int y) noexcept {
    knst_edge_zone new_edge = detect_edge_zone(x, y);
    if (new_edge == m_hovered_edge) return;
    m_hovered_edge = new_edge;

    Display* display = KnstWindowSources::m_xlib_display;
    if (!display) return;

    unsigned int shape = edge_to_x11_cursor_shape(new_edge);
    Cursor cursor = XCreateFontCursor(display, shape);
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

inline void knst_window::start_move_or_resize(int root_x, int root_y, uint32_t direction) noexcept {
    if (!m_window) return;

    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = m_window;
    ev.type = KnstWindowSources::m_NET_WM_MOVERESIZE;
    ev.data.data32[0] = root_x;
    ev.data.data32[1] = root_y;
    ev.data.data32[2] = direction;
    ev.data.data32[3] = 1;
    ev.data.data32[4] = 1; 

    xcb_ungrab_pointer(KnstWindowSources::m_connection, XCB_CURRENT_TIME);

    xcb_send_event(
        KnstWindowSources::m_connection,
        0,
        KnstWindowSources::m_root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        (const char*)&ev
    );
    xcb_flush(KnstWindowSources::m_connection);
}


inline void knst_window::set_minimum_size(int width, int height) noexcept {
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
    
    
    xcb_get_property_cookie_t prop_cookie = xcb_get_property(
        conn, 0, m_window, wm_normal_hints, XCB_ATOM_WM_SIZE_HINTS, 0, 18);
    xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(conn, prop_cookie, nullptr);
    
    xcb_size_hints_t hints = {};
    
    if (prop_reply) {
       
        xcb_size_hints_t* existing = (xcb_size_hints_t*)xcb_get_property_value(prop_reply);
        if (existing) {
            hints = *existing;
        }
        free(prop_reply);
    }
    
    
    if (width == KNST_DEFAULT) {
        width = 300;  
    }
    if (height == KNST_DEFAULT) {
        height = 250; 
    }
    
    hints.flags |= XCB_ICCCM_SIZE_HINT_P_MIN_SIZE;
    hints.min_width = width;
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

inline void knst_window::set_maximum_size(int width, int height) noexcept {
    if (!m_window) return;
    
   
    if (width == KNST_DEFAULT && height == KNST_DEFAULT) {
        return;
    }
    
    xcb_connection_t* conn = KnstWindowSources::m_connection;
    if (!conn) return;
    
    
    const char* atom_name = "WM_NORMAL_HINTS";
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
        conn, 0, (uint16_t)strlen(atom_name), atom_name);
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(conn, cookie, nullptr);
    
    if (!reply) return;
    
    xcb_atom_t wm_normal_hints = reply->atom;
    free(reply);
    
  
    xcb_get_property_cookie_t prop_cookie = xcb_get_property(
        conn, 0, m_window, wm_normal_hints, XCB_ATOM_WM_SIZE_HINTS, 0, 18);
    xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(conn, prop_cookie, nullptr);
    
    xcb_size_hints_t hints = {};
    
    if (prop_reply) {
      
        xcb_size_hints_t* existing = (xcb_size_hints_t*)xcb_get_property_value(prop_reply);
        if (existing) {
            hints = *existing;
        }
        free(prop_reply);
    }
    
    
    if (width != KNST_DEFAULT) {
        hints.flags |= XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
        hints.max_width = width;
    }
    
    if (height != KNST_DEFAULT) {
        hints.flags |= XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
        hints.max_height = height;
    }
    
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
    



#endif
#endif // KNST_WINDOW_X11_MANAGER_HPP