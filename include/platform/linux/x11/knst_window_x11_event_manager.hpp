// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
knst_window_x11_event_manager.hpp
----------------------------

    Event handling for the X11 backend

*/


#pragma once

#if KNST_USING_LINUX_PLATFORM_X11

KNST_FORCE_INLINE void load_native_to_knst_event(knst_window& window, xcb_generic_event_t* ev) noexcept {

    uint8_t event_type = ev->response_type & ~0x80;

    switch (event_type) {
        case XCB_BUTTON_PRESS:
        case XCB_BUTTON_RELEASE: {
            window.m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            xcb_button_press_event_t* btn = (xcb_button_press_event_t*)ev;

            const bool is_press = (event_type == XCB_BUTTON_PRESS);

            // Translate native X11 button state to unified modifier bits
            {
                int mods = 0;
                if (btn->state & ShiftMask)   mods |= KNST_WINDOW_MOD_SHIFT;
                if (btn->state & ControlMask) mods |= KNST_WINDOW_MOD_CONTROL;
                if (btn->state & Mod1Mask)    mods |= KNST_WINDOW_MOD_ALT;
                if (btn->state & Mod4Mask)    mods |= KNST_WINDOW_MOD_SUPER;
                if (btn->state & LockMask)    mods |= KNST_WINDOW_MOD_CAPS_LOCK;
                if (btn->state & Mod2Mask)    mods |= KNST_WINDOW_MOD_NUM_LOCK;
                window.m_knst_event.mods = mods;
            }

            // ── SCROLL (button 4 and 5) ────────────────────────────────
            if (btn->detail == 4 || btn->detail == 5) {
                if (!is_press) {
                    free(ev);
                    return;
                }

                window.m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_SCROLL;
                window.m_knst_event.mouse_scroll_delta = (btn->detail == 4) ? 1 : -1;
                window.m_knst_event.mouse_button = btn->detail;
                window.m_knst_event.mouse_x = btn->event_x;
                window.m_knst_event.mouse_y = btn->event_y;
                window.dispatch_current_event();
                break;
            }

            // ── NORMAL BUTTON (1, 2, 3, ...) ─────────────────────────── yeaa
            window.m_knst_event.mouse_action = is_press
                ? KNST_WINDOW_MOUSE_ACTION_PRESS
                : KNST_WINDOW_MOUSE_ACTION_RELEASE;


            window.m_knst_event.mouse_button = btn->detail;
            window.m_knst_event.mouse_x = btn->event_x;
            window.m_knst_event.mouse_y = btn->event_y;
            window.m_knst_event.mouse_root_x = btn->root_x;
            window.m_knst_event.mouse_root_y = btn->root_y;

            #ifdef KNST_DISABLE_TITLE_BAR
            if (is_press && btn->detail == 1) {
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
                        window.m_knst_event.type = KNST_WINDOW_EVENT_CLOSE;
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

            window.dispatch_current_event();
            break;
        }

        case XCB_MOTION_NOTIFY: {
            window.m_knst_event.begin(KNST_WINDOW_EVENT_MOTION);
            xcb_motion_notify_event_t* motion = (xcb_motion_notify_event_t*)ev;
            window.m_knst_event.mouse_x = motion->event_x;
            window.m_knst_event.mouse_y = motion->event_y;
            window.m_knst_event.mouse_root_x = motion->root_x;
            window.m_knst_event.mouse_root_y = motion->root_y;

            {
                int mods = 0;
                if (motion->state & ShiftMask)   mods |= KNST_WINDOW_MOD_SHIFT;
                if (motion->state & ControlMask) mods |= KNST_WINDOW_MOD_CONTROL;
                if (motion->state & Mod1Mask)    mods |= KNST_WINDOW_MOD_ALT;
                if (motion->state & Mod4Mask)    mods |= KNST_WINDOW_MOD_SUPER;
                if (motion->state & LockMask)    mods |= KNST_WINDOW_MOD_CAPS_LOCK;
                if (motion->state & Mod2Mask)    mods |= KNST_WINDOW_MOD_NUM_LOCK;
                window.m_knst_event.mods = mods;
            }

            #ifdef KNST_DISABLE_TITLE_BAR
                window.update_edge_cursor(motion->event_x, motion->event_y);
            #endif

            window.dispatch_current_event();
            break;
        }

        case XCB_CONFIGURE_NOTIFY: {
            xcb_configure_notify_event_t* config = (xcb_configure_notify_event_t*)ev;

            int new_width = config->width;
            int new_height = config->height;

            int old_width = window.m_knst_event.window_width;
            int old_height = window.m_knst_event.window_height;

            bool size_changed = (new_width != old_width) || (new_height != old_height);
            bool pos_changed  = (config->x != window.m_knst_event.window_root_x) ||
                                (config->y != window.m_knst_event.window_root_y);

            if (size_changed || pos_changed) {
                if (pos_changed) {
                    window.m_knst_event.window_root_x = config->x;
                    window.m_knst_event.window_root_y = config->y;
                }

                if (size_changed) {
                    window.m_knst_event.begin(KNST_WINDOW_EVENT_RESIZE);
                    window.m_knst_event.window_width = new_width;
                    window.m_knst_event.window_height = new_height;
                } else {
                    window.m_knst_event.begin(KNST_WINDOW_EVENT_MOVE);
                }

                window.dispatch_current_event();
                free(ev);
                return;
            } else {
                window.m_knst_event.type = KNST_WINDOW_EVENT_UNKNOWN;
                free(ev);
                return;
            }
        }

        case XCB_CLIENT_MESSAGE: {
            xcb_client_message_event_t* msg = (xcb_client_message_event_t*)ev;

            if (msg->data.data32[0] == KnstWindowSources::m_wmDelete) {
                window.m_knst_event.begin(KNST_WINDOW_EVENT_CLOSE);
                window.should_close();
                window.dispatch_current_event();
            }
            else if (msg->data.data32[0] == KnstWindowSources::m_wmSyncRequest) {
                window.m_syncPendingValue.lo = msg->data.data32[2];
                window.m_syncPendingValue.hi = (int32_t)msg->data.data32[3];
                window.m_syncHasPendingValue = true;
                window.m_syncRequestReceived = true;
            }
            else if (msg->type == KnstWindowSources::m_XdndEnter) {
                window.m_xdnd_source = msg->data.data32[0];
                window.m_xdnd_version = msg->data.data32[1] >> 24;
                window.m_xdnd_selected_type = KnstWindowSources::m_textUriList;

                window.m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_ENTER);
                window.dispatch_current_event();
            }
            else if (msg->type == KnstWindowSources::m_XdndPosition) {
                xcb_window_t source = msg->data.data32[0];

                uint32_t x_pos = (msg->data.data32[2] >> 16) & 0xFFFF;
                uint32_t y_pos = msg->data.data32[2] & 0xFFFF;

                window.m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_MOVE);
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
                window.dispatch_current_event();
            }
            else if (msg->type == KnstWindowSources::m_XdndLeave) {
                window.m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_LEAVE);
                window.dispatch_current_event();
            }
            else if (msg->type == KnstWindowSources::m_XdndDrop) {
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

                window.m_knst_event.type = KNST_WINDOW_EVENT_UNKNOWN;
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
                        knst_byte_string uriList(data, (uint32_t)len);

                        window.m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP);

                        knst_vector<knst_c16string> files;

                        uint32_t pos = 0;
                        while (pos < uriList.length()) {
                            uint32_t end = pos;
                            while (end < uriList.length() && uriList[end] != '\n') end++;

                            std::string uri(reinterpret_cast<const char*>(uriList.data() + pos), end - pos);

                            if (!uri.empty() && uri.back() == '\r') uri.pop_back();

                            if (uri.find("file://") == 0) {
                                std::string path = uri.substr(7);

                                for (size_t i = 0; i < path.length(); i++) {
                                    if (path[i] == '%' && i + 2 < path.length()) {
                                        unsigned int hex = 0;
                                        sscanf(path.substr(i+1, 2).c_str(), "%x", &hex);
                                        path.replace(i, 3, 1, (char)hex);
                                    }
                                }
                                files.push_back(knst_c16string(path.c_str()));
                            }
                            pos = end + 1;
                        }

                        window.m_knst_event.drop_files =
                            std::make_shared<knst_vector<knst_c16string>>(std::move(files));
                        window.m_knst_event.drop_count =
                            (uint32_t)window.m_knst_event.drop_files->size();

                        window.dispatch_current_event();

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
            window.m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_IN);
            window.m_knst_event.is_focused = true;
            window.dispatch_current_event();
            break;
        }

        case XCB_FOCUS_OUT: {
            window.m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_OUT);
            window.m_knst_event.is_focused = false;
            window.dispatch_current_event();
            break;
        }

        case XCB_ENTER_NOTIFY: {
            window.m_knst_event.begin(KNST_WINDOW_EVENT_ENTER);
            window.m_knst_event.mouse_on_window = true;
            window.dispatch_current_event();
            break;
        }

        case XCB_LEAVE_NOTIFY: {
            window.m_knst_event.begin(KNST_WINDOW_EVENT_LEAVE);
            window.m_knst_event.mouse_on_window = false;
            window.dispatch_current_event();
            break;
        }

        case XCB_EXPOSE: {
            xcb_expose_event_t* expose = (xcb_expose_event_t*)ev;
            if (expose->count == 0) {
                window.m_knst_event.begin(KNST_WINDOW_EVENT_EXPOSE);
                window.dispatch_current_event();
            }
            break;
        }

        case XCB_KEY_PRESS: {
            xcb_key_press_event_t* key = (xcb_key_press_event_t*)ev;
            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(KnstWindowSources::m_keysyms, key->detail, 0);

            if (keysym >= XK_a && keysym <= XK_z) keysym -= 32;
            else if (keysym == XK_ccedilla)   keysym = XK_Ccedilla;
            else if (keysym == XK_scedilla)   keysym = XK_Scedilla;
            else if (keysym == XK_gbreve)     keysym = XK_Gbreve;
            else if (keysym == XK_idotless)   keysym = XK_I;
            else if (keysym == XK_odiaeresis) keysym = XK_Odiaeresis;
            else if (keysym == XK_udiaeresis) keysym = XK_Udiaeresis;

            if (window.find_held_by_scancode(key->detail)) break;

            window.m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window.add_held_key(keysym, key->detail, KnstWindowSources::get_current_time_ms());

            window.m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
            window.m_knst_event.key_code = keysym;
            window.m_knst_event.scancode = key->detail;

            {
                int mods = 0;
                if (key->state & ShiftMask)   mods |= KNST_WINDOW_MOD_SHIFT;
                if (key->state & ControlMask) mods |= KNST_WINDOW_MOD_CONTROL;
                if (key->state & Mod1Mask)    mods |= KNST_WINDOW_MOD_ALT;
                if (key->state & Mod4Mask)    mods |= KNST_WINDOW_MOD_SUPER;
                if (key->state & LockMask)    mods |= KNST_WINDOW_MOD_CAPS_LOCK;
                if (key->state & Mod2Mask)    mods |= KNST_WINDOW_MOD_NUM_LOCK;
                window.m_knst_event.mods = mods;
            }
            window.dispatch_current_event();
            break;
        }

        case XCB_KEY_RELEASE: {
            xcb_key_release_event_t* key = (xcb_key_release_event_t*)ev;
            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(KnstWindowSources::m_keysyms, key->detail, 0);

            if (keysym >= XK_a && keysym <= XK_z) keysym -= 32;
            else if (keysym == XK_ccedilla)   keysym = XK_Ccedilla;
            else if (keysym == XK_scedilla)   keysym = XK_Scedilla;
            else if (keysym == XK_gbreve)     keysym = XK_Gbreve;
            else if (keysym == XK_idotless)   keysym = XK_I;
            else if (keysym == XK_odiaeresis) keysym = XK_Odiaeresis;
            else if (keysym == XK_udiaeresis) keysym = XK_Udiaeresis;

            window.m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window.m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_RELEASE;
            window.m_knst_event.key_code = keysym;
            window.m_knst_event.scancode = key->detail;

            {
                int mods = 0;
                if (key->state & ShiftMask)   mods |= KNST_WINDOW_MOD_SHIFT;
                if (key->state & ControlMask) mods |= KNST_WINDOW_MOD_CONTROL;
                if (key->state & Mod1Mask)    mods |= KNST_WINDOW_MOD_ALT;
                if (key->state & Mod4Mask)    mods |= KNST_WINDOW_MOD_SUPER;
                if (key->state & LockMask)    mods |= KNST_WINDOW_MOD_CAPS_LOCK;
                if (key->state & Mod2Mask)    mods |= KNST_WINDOW_MOD_NUM_LOCK;
                window.m_knst_event.mods = mods;
            }
            window.remove_held_key(key->detail);
            window.dispatch_current_event();
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
                            window.m_knst_event.begin(KNST_WINDOW_EVENT_FULLSCREEN);
                        } else if (final_minimized) {
                            window.m_knst_event.begin(KNST_WINDOW_EVENT_MINIMIZE);
                        } else if (final_maximized) {
                            window.m_knst_event.begin(KNST_WINDOW_EVENT_MAXIMIZE);
                        } else {
                            window.m_knst_event.begin(KNST_WINDOW_EVENT_RESTORE);
                        }
                    } else {
                        window.m_knst_event.begin(KNST_WINDOW_EVENT_UNKNOWN);
                    }

                    window.m_knst_event.is_full_screen = final_fullscreen;
                    window.m_knst_event.is_maximized    = final_maximized;
                    window.m_knst_event.is_minimized    = final_minimized;
                    window.dispatch_current_event();
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
            window.m_knst_event.type = KNST_WINDOW_EVENT_UNKNOWN;
            break;
    }
    free(ev);
}

#endif