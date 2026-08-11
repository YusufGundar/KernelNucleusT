#ifndef KNST_LINUX_X11_EVENT_MANAGER_HPP
#define KNST_LINUX_X11_EVENT_MANAGER_HPP
#pragma once

#if KNST_USING_LINUX_PLATFORM_X11



KNST_FORCE_INLINE void load_native_to_knst_event(knst_window& window,xcb_generic_event_t* ev) noexcept {
   
    
    
    uint8_t event_type = ev->response_type & ~0x80;

    switch (event_type) {
        case XCB_BUTTON_PRESS: {
            xcb_button_press_event_t* btn = (xcb_button_press_event_t*)ev;
            
            if (btn->detail == 4) {
                window.m_knst_event.type = KNST_MOUSE_EVENT;
                window.m_knst_event.mouse_action = KNST_MOUSE_SCROLL;
                window.m_knst_event.mouse_scroll_delta = 1; 
                window.m_knst_event.mouse_button = btn->detail;
                window.m_knst_event.mouse_x = btn->event_x;
                window.m_knst_event.mouse_y = btn->event_y;
                window.m_knst_event.mods = btn->state;
            }
            else if (btn->detail == 5) {
                window.m_knst_event.type = KNST_MOUSE_EVENT;
                window.m_knst_event.mouse_action = KNST_MOUSE_SCROLL;
                window.m_knst_event.mouse_scroll_delta = -1;  
                window.m_knst_event.mouse_button = btn->detail;
                window.m_knst_event.mouse_x = btn->event_x;
                window.m_knst_event.mouse_y = btn->event_y;
                window.m_knst_event.mods = btn->state;
            }
            else {
                window.m_knst_event.type = KNST_MOUSE_EVENT;
                window.m_knst_event.mouse_action = KNST_MOUSE_BUTTON_PRESS;
                window.m_knst_event.mouse_button = btn->detail;
                window.m_knst_event.mouse_x = btn->event_x;
                window.m_knst_event.mouse_y = btn->event_y;
                window.m_knst_event.mouse_root_x = btn->root_x;
                window.m_knst_event.mouse_root_y = btn->root_y;
                window.m_knst_event.mods = btn->state;

            #ifdef KNST_DISABLE_TITLE_BAR
                if (btn->detail == 1) { 
                    int mx = btn->event_x;
                    int my = btn->event_y;
                    int w = window.m_knst_event.window_width;
                    int titlebar_h = window.get_title_bar_height();
                    const int BUTTON_WIDTH = 48;
                    const int CORNER_SIZE = 12;

                    if (my >= 0 && my <= titlebar_h) {
                        
                        if (mx < CORNER_SIZE && my < CORNER_SIZE) {
                            window.start_move_or_resize(btn->root_x, btn->root_y, 0);
                        }
                    
                        else if (mx >= w - CORNER_SIZE && mx < w - BUTTON_WIDTH * 3 && my < CORNER_SIZE) {
                            window.start_move_or_resize(btn->root_x, btn->root_y, 2); 
                        }
                    
                        else if (mx >= w - BUTTON_WIDTH) {
                            window.should_close();
                            window.m_knst_event.type = KNST_CLOSE_WINDOW;
                        }
                        
                        else if (mx >= w - BUTTON_WIDTH * 2) {
                            if (window.m_knst_event.is_maximized) window.restore();
                            else window.set_maximized();
                        }
                    
                        else if (mx >= w - BUTTON_WIDTH * 3) {
                            window.set_minimized();
                        }
                        
                        else if (my < CORNER_SIZE) {
                            window.start_move_or_resize(btn->root_x, btn->root_y, 1); 
                        }
                    
                        else {
                            window.start_move_or_resize(btn->root_x, btn->root_y, 8);
                        }
                    }
                    else {
                    
                        auto edge = window.detect_edge_zone(mx, my);
                        if (edge != knst_window::ZONE_NONE) {
                            window.start_move_or_resize(
                                btn->root_x, btn->root_y,
                                window.edge_to_moveresize_direction(edge)
                            );
                        }
                    }
                }
                #endif
            }
            break;
        }

        case XCB_MOTION_NOTIFY: {
            xcb_motion_notify_event_t* motion = (xcb_motion_notify_event_t*)ev;
            window.m_knst_event.type = KNST_MOTION_NOTIFY;
            window.m_knst_event.mouse_x = motion->event_x;
            window.m_knst_event.mouse_y = motion->event_y;
            window.m_knst_event.mouse_root_x = motion->root_x;
            window.m_knst_event.mouse_root_y = motion->root_y;
            window.m_knst_event.mods = motion->state;

            #ifdef KNST_DISABLE_TITLE_BAR
                window.update_edge_cursor(motion->event_x, motion->event_y);
            #endif

            break;
        }

        case XCB_CONFIGURE_NOTIFY: {
            xcb_configure_notify_event_t* config = (xcb_configure_notify_event_t*)ev;
            window.m_knst_event.window_width = config->width;
            window.m_knst_event.window_height = config->height;
            window.m_knst_event.window_root_x = config->x;
            window.m_knst_event.window_root_y = config->y;
            window.m_knst_event.type = KNST_WINDOW_RESIZE;

          
            
           
            break;
        }

        case XCB_CLIENT_MESSAGE: {
            xcb_client_message_event_t* msg = (xcb_client_message_event_t*)ev;

            if (msg->data.data32[0] == KnstWindowSources::m_wmDelete) {
                window.m_knst_event.type = KNST_CLOSE_WINDOW;
            }
           
            else if (msg->type == KnstWindowSources::m_XdndEnter) {
               
                window.m_xdnd_source = msg->data.data32[0];
                window.m_xdnd_version = msg->data.data32[1] >> 24;
                
                
                window.m_xdnd_selected_type = KnstWindowSources::m_textUriList;
                
               
                window.m_knst_event.type = KNST_FILE_DROP_ENTER;
                window.m_knst_event.drop_files.clear(); 
                window.m_knst_event.drop_count = 0;
            }
            else if (msg->type == KnstWindowSources::m_XdndPosition) {
               
                xcb_window_t source = msg->data.data32[0];
                
              
                uint32_t x_pos = (msg->data.data32[2] >> 16) & 0xFFFF;
                uint32_t y_pos = msg->data.data32[2] & 0xFFFF;
                
              
                window.m_knst_event.type = KNST_FILE_DROP_MOVE;
                window.m_knst_event.mouse_x = x_pos;
                window.m_knst_event.mouse_y = y_pos;
                
               
                xcb_client_message_event_t status_ev{};
                status_ev.response_type = XCB_CLIENT_MESSAGE;
                status_ev.format = 32;
                status_ev.window = source;
                status_ev.type = KnstWindowSources::m_XdndStatus;
                status_ev.data.data32[0] = window.m_window;
                status_ev.data.data32[1] = 1;                 
                status_ev.data.data32[2] = 0;
                status_ev.data.data32[3] = 0;
                status_ev.data.data32[4] = KnstWindowSources::m_XdndActionCopy;

                xcb_send_event(
                    KnstWindowSources::m_connection,
                    0, source,
                    XCB_EVENT_MASK_NO_EVENT,
                    (const char*)&status_ev
                );
                xcb_flush(KnstWindowSources::m_connection);
            }
            else if (msg->type == KnstWindowSources::m_XdndLeave) {
              
                window.m_knst_event.type = KNST_FILE_DROP_LEAVE;
                
               
                window.m_knst_event.drop_files.clear();
                window.m_knst_event.drop_count = 0;
            }
            else if (msg->type == KnstWindowSources::m_XdndDrop) {
              
                xcb_window_t source = msg->data.data32[0];
                xcb_timestamp_t time = msg->data.data32[2];
                
             
                xcb_convert_selection(
                    KnstWindowSources::m_connection,
                    window.m_window,
                    KnstWindowSources::m_XdndSelection,
                    window.m_xdnd_selected_type,
                    KnstWindowSources::m_XdndSelection,
                    time
                );
                xcb_flush(KnstWindowSources::m_connection);
                
                
                window.m_knst_event.type = KNST_UNKNOWN;
            }
            break;
        }

         case XCB_SELECTION_NOTIFY: {
            xcb_selection_notify_event_t* notify = (xcb_selection_notify_event_t*)ev;

            if (notify->property != XCB_NONE) {
                xcb_get_property_cookie_t cookie = xcb_get_property(
                    KnstWindowSources::m_connection,
                    0,
                    window.m_window,
                    notify->property,
                    XCB_ATOM_ANY,
                    0,
                    1024 * 1024
                );
                xcb_get_property_reply_t* reply = xcb_get_property_reply(
                    KnstWindowSources::m_connection, cookie, nullptr
                );

                if (reply) {
                    const char* data = (const char*)xcb_get_property_value(reply);
                    int len = xcb_get_property_value_length(reply);

                    if (notify->selection == KnstWindowSources::m_XdndSelection) {
                        
                        std::string uriList(data, len);
                        
                        
                        window.m_knst_event.drop_files.clear();
                        window.m_knst_event.drop_count = 0;
                        
                       
                        knst_vector<knst_c16string> files;
                        
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
                                        sscanf(path.substr(i+1, 2).c_str(), "%x", &hex);
                                        path.replace(i, 3, 1, (char)hex);
                                    }
                                }
                                files.push_back(knst_c16string(path.c_str()));
                            }
                            pos = end + 1;
                        }
                        
                       
                        if (!files.empty()) {
                            window.m_knst_event.drop_files = files; 
                            window.m_knst_event.drop_count = files.size();
                        }
                        
                        
                        window.m_knst_event.type = KNST_FILE_DROP;
                        
                        
                        xcb_client_message_event_t finished_ev{};
                        finished_ev.response_type = XCB_CLIENT_MESSAGE;
                        finished_ev.format = 32;
                        finished_ev.window = window.m_xdnd_source;
                        finished_ev.type = KnstWindowSources::m_XdndFinished;
                        finished_ev.data.data32[0] = window.m_window;
                        finished_ev.data.data32[1] = 1; 
                        finished_ev.data.data32[2] = KnstWindowSources::m_XdndActionCopy;

                        xcb_send_event(
                            KnstWindowSources::m_connection,
                            0, 
                            window.m_xdnd_source,
                            XCB_EVENT_MASK_NO_EVENT,
                            (const char*)&finished_ev
                        );
                        xcb_flush(KnstWindowSources::m_connection);
                        

                    }
                    else {
                        window.clipboard_text = knst_c16string(data, len);
                    }

                    free(reply);
                }
            }
            break;
        }

        case XCB_FOCUS_IN: {
            window.m_knst_event.type = KNST_FOCUS_IN;
            window.m_knst_event.is_focused = true;
            break;
        }

        case XCB_FOCUS_OUT: {
            window.m_knst_event.type = KNST_FOCUS_OUT;
            window.m_knst_event.is_focused = false;
            break;
        }

        case XCB_ENTER_NOTIFY: {
            window.m_knst_event.type = KNST_ENTER_NOTIFY;
            window.m_knst_event.mouse_on_window = true;
            break;
        }

        case XCB_LEAVE_NOTIFY: {
            window.m_knst_event.type = KNST_LEAVE_NOTIFY;
            window.m_knst_event.mouse_on_window = false;
            break;
        }

        case XCB_EXPOSE: {
            xcb_expose_event_t* expose = (xcb_expose_event_t*)ev;
            if (expose->count == 0) {
                window.m_knst_event.type = KNST_EXPOSE;
                
            } else {
                window.m_knst_event.type = KNST_UNKNOWN;
            }
            break;
        }

        case XCB_KEY_PRESS: {
            xcb_key_press_event_t* key = (xcb_key_press_event_t*)ev;

            xcb_key_symbols_t* keysyms = KnstWindowSources::m_keysyms;
            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, key->detail, 0);

            if (keysym == window.m_knst_event.m_last_key && window.m_knst_event.m_key_held) {
                window.m_knst_event.key_action = KNST_KEY_REPEAT;
            } else {
                window.m_knst_event.key_action = KNST_KEY_PRESS;
                window.m_knst_event.m_last_key = keysym;
                window.m_knst_event.m_key_held = true;
            }

            window.m_knst_event.type = KNST_KEYBOARD_EVENT;
            window.m_knst_event.key_code = keysym;
            window.m_knst_event.mods = key->state;
            window.m_knst_event.scancode = key->detail;
            break;
        }

        case XCB_KEY_RELEASE: {
            xcb_key_release_event_t* key = (xcb_key_release_event_t*)ev;

            xcb_key_symbols_t* keysyms = KnstWindowSources::m_keysyms;
            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, key->detail, 0);

            window.m_knst_event.type = KNST_KEYBOARD_EVENT;
            window.m_knst_event.key_action = KNST_KEY_RELEASE;
            window.m_knst_event.key_code = keysym;
            window.m_knst_event.mods = key->state;
            window.m_knst_event.scancode = key->detail;

            if (keysym == window.m_knst_event.m_last_key) {
                window.m_knst_event.m_key_held = false;
                window.m_knst_event.m_last_key = 0;
            }
            break;
        }

        case XCB_PROPERTY_NOTIFY: {
            xcb_property_notify_event_t* prop = (xcb_property_notify_event_t*)ev;
            if (prop->atom == KnstWindowSources::m_NET_WM_STATE) {
                xcb_get_property_cookie_t cookie = xcb_get_property(
                    KnstWindowSources::m_connection,
                    0,
                    window.m_window,
                    KnstWindowSources::m_NET_WM_STATE,
                    XCB_ATOM_ATOM,
                    0,
                    1024
                );
                xcb_get_property_reply_t* reply = xcb_get_property_reply(
                    KnstWindowSources::m_connection, cookie, nullptr
                );
                if (reply) {
                    bool fullscreen = false;
                    bool max_h = false;
                    bool max_v = false;
                    bool hidden = false;
                    int len = xcb_get_property_value_length(reply) / sizeof(xcb_atom_t);
                    xcb_atom_t* atoms = (xcb_atom_t*)xcb_get_property_value(reply);
                    for (int i = 0; i < len; ++i) {
                        if (atoms[i] == KnstWindowSources::m_NET_WM_STATE_FULLSCREEN) {
                            fullscreen = true;
                        } else if (atoms[i] == KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_HORZ) {
                            max_h = true;
                        } else if (atoms[i] == KnstWindowSources::m_NET_WM_STATE_MAXIMIZED_VERT) {
                            max_v = true;
                        } else if (atoms[i] == KnstWindowSources::m_NET_WM_STATE_HIDDEN) {
                            hidden = true;
                        }
                    }
                    free(reply);

                    bool raw_maximized = (max_h && max_v);
                    bool final_fullscreen = fullscreen;
                    bool final_minimized  = !fullscreen && hidden;
                    bool final_maximized  = !fullscreen && !hidden && raw_maximized;

                    bool changed =
                        (final_fullscreen != window.m_knst_event.is_full_screen) ||
                        (final_maximized  != window.m_knst_event.is_maximized)   ||
                        (final_minimized  != window.m_knst_event.is_minimized);

                    if (changed) {
                        if (final_fullscreen) {
                            window.m_knst_event.type = KNST_WINDOW_FULL_SCREEN;
                        } else if (final_minimized) {
                            window.m_knst_event.type = KNST_WINDOW_MINIMIZE;
                        } else if (final_maximized) {
                            window.m_knst_event.type = KNST_WINDOW_MAXIMIZE;
                        } else {
                            window.m_knst_event.type = KNST_WINDOW_RESTORE;
                        }
                    } else {
                        window.m_knst_event.type = KNST_UNKNOWN;
                    }

                    window.m_knst_event.is_full_screen = final_fullscreen;
                    window.m_knst_event.is_maximized    = final_maximized;
                    window.m_knst_event.is_minimized    = final_minimized;
                    
                   
                }
            }
            break;
        }

        case XCB_SELECTION_REQUEST: {
            xcb_selection_request_event_t* req = (xcb_selection_request_event_t*)ev;
            
            if (req->selection == KnstWindowSources::m_CLIPBOARD) {
                if (req->target == KnstWindowSources::m_TARGETS) {
                    xcb_atom_t targets[] = {
                        KnstWindowSources::m_UTF8_STRING,
                        XCB_ATOM_STRING
                    };
                    xcb_change_property(
                        KnstWindowSources::m_connection,
                        XCB_PROP_MODE_REPLACE,
                        req->requestor,
                        req->property,
                        KnstWindowSources::m_ATOM,
                        32, 2, targets
                    );
                } else {
                    knst_byte_string utf8_data(window.clipboard_text);
                    
                    xcb_change_property(
                        KnstWindowSources::m_connection,
                        XCB_PROP_MODE_REPLACE,
                        req->requestor,
                        req->property,
                        req->target,
                        8,
                        utf8_data.length(),
                        utf8_data.data()
                    );
                }
                
                xcb_selection_notify_event_t notify;
                notify.response_type = XCB_SELECTION_NOTIFY;
                notify.requestor = req->requestor;
                notify.selection = req->selection;
                notify.target = req->target;
                notify.property = req->property;
                notify.time = req->time;
                
                xcb_send_event(
                    KnstWindowSources::m_connection,
                    0, req->requestor,
                    XCB_EVENT_MASK_NO_EVENT,
                    (const char*)&notify
                );
                xcb_flush(KnstWindowSources::m_connection);
            }
            break;
        }

        default:
            window.m_knst_event.type = KNST_UNKNOWN;
            break;
    }
    free(ev); 
}

#endif

#endif //KNST_LINUX_X11_EVENT_MANAGER_HPP