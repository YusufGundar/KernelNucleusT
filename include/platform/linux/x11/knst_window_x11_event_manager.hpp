// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_x11_event_manager.hpp
----------------------------

    Event handling for the X11 backend.

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

            // Translate native X11 button state to unified modifier bits.
            {
                int mods = 0;
                if (btn->state & ShiftMask) mods |= KNST_WINDOW_MOD_SHIFT;
                if (btn->state & ControlMask) mods |= KNST_WINDOW_MOD_CONTROL;
                if (btn->state & Mod1Mask) mods |= KNST_WINDOW_MOD_ALT;
                if (btn->state & Mod4Mask) mods |= KNST_WINDOW_MOD_SUPER;
                if (btn->state & LockMask) mods |= KNST_WINDOW_MOD_CAPS_LOCK;
                if (btn->state & Mod2Mask) mods |= KNST_WINDOW_MOD_NUM_LOCK;
                window.m_knst_event.mods = mods;
            }

            // Scroll: button 4/5 vertical, 6/7 horizontal.
            // The old code only handled 4/5; 6/7 went through as normal
            // buttons. Wayland/Windows use mouse_button = 6 for
            // horizontal scroll — match that.
            if (btn->detail == 4 || btn->detail == 5 ||
                btn->detail == 6 || btn->detail == 7) {
                if (!is_press) {
                    free(ev);
                    return;
                }

                window.m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_SCROLL;
                window.m_knst_event.mouse_x = btn->event_x;
                window.m_knst_event.mouse_y = btn->event_y;

                if (btn->detail == 4) {
                    // Vertical up.
                    window.m_knst_event.mouse_scroll_delta = 1;
                    window.m_knst_event.mouse_button = 4;
                } else if (btn->detail == 5) {
                    // Vertical down.
                    window.m_knst_event.mouse_scroll_delta = -1;
                    window.m_knst_event.mouse_button = 4;
                } else if (btn->detail == 6) {
                    // Horizontal left.
                    window.m_knst_event.mouse_scroll_delta = -1;
                    window.m_knst_event.mouse_button = 6;
                } else {  // 7
                    // Horizontal right.
                    window.m_knst_event.mouse_scroll_delta = 1;
                    window.m_knst_event.mouse_button = 6;
                }
                window.dispatch_current_event();
                break;
            }

            // Normal button (1, 2, 3, ...).
            window.m_knst_event.mouse_action = is_press
                ? KNST_WINDOW_MOUSE_ACTION_PRESS
                : KNST_WINDOW_MOUSE_ACTION_RELEASE;

            window.m_knst_event.mouse_button = btn->detail;
            window.m_knst_event.mouse_x = btn->event_x;
            window.m_knst_event.mouse_y = btn->event_y;
            window.m_knst_event.mouse_root_x = btn->root_x;
            window.m_knst_event.mouse_root_y = btn->root_y;

            // CSD — user-configured title bar.
            if (is_press && btn->detail == 1 && window.is_csd_enabled()) {
                const int mx = btn->event_x;
                const int my = btn->event_y;

                knst_window::csd_hit hit = window.hit_test_csd(mx, my);

                // Double-click on the CSD title bar — maximize/restore.
                // The X11 WM doesn't do this itself; the library does.
                const uint32_t now = KnstWindowSources::get_current_time_ms();
                static thread_local uint32_t s_last_click = 0;
                static thread_local int s_last_x = -1;
                static thread_local int s_last_y = -1;
                constexpr uint32_t DBL_CLICK_MS = 400;
                constexpr int DBL_CLICK_DIST = 4;

                if (hit == knst_window::csd_hit::draggable) {
                    const bool near_last =
                        (s_last_x >= 0) &&
                        (std::abs(mx - s_last_x) <= DBL_CLICK_DIST) &&
                        (std::abs(my - s_last_y) <= DBL_CLICK_DIST);
                    if ((now - s_last_click) <= DBL_CLICK_MS && near_last) {
                        // Double-click — maximize/restore.
                        if (window.m_knst_event.is_maximized) window.restore();
                        else window.set_maximized();
                        s_last_click = 0;
                        s_last_x = -1;
                        s_last_y = -1;
                        free(ev);
                        return;
                    }
                    s_last_click = now;
                    s_last_x = mx;
                    s_last_y = my;
                }

                if (hit != knst_window::csd_hit::normal) {
                    // Hand off to the OS — drag or resize.
                    window.csd_handle_hit(hit, mx, my, 0);
                    free(ev);
                    return;
                }
            }

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
                if (motion->state & ShiftMask) mods |= KNST_WINDOW_MOD_SHIFT;
                if (motion->state & ControlMask) mods |= KNST_WINDOW_MOD_CONTROL;
                if (motion->state & Mod1Mask) mods |= KNST_WINDOW_MOD_ALT;
                if (motion->state & Mod4Mask) mods |= KNST_WINDOW_MOD_SUPER;
                if (motion->state & LockMask) mods |= KNST_WINDOW_MOD_CAPS_LOCK;
                if (motion->state & Mod2Mask) mods |= KNST_WINDOW_MOD_NUM_LOCK;
                window.m_knst_event.mods = mods;
            }

            if (window.is_csd_enabled()) {
                window.refresh_csd_cursor(motion->event_x, motion->event_y);
            }

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

            // X11 has two kinds of ConfigureNotify:
            //   - synthetic (from the WM, root-relative coords) — correct
            //   - real (from the X server, parent-relative) — wrong
            // Only update position from the synthetic event.
            const bool synthetic = (ev->response_type & 0x80) != 0;
            bool pos_changed = synthetic &&
                               ((config->x != window.m_knst_event.window_root_x) ||
                                (config->y != window.m_knst_event.window_root_y));

            if (size_changed || pos_changed) {
                // We're handling a WM configure. Clear the flag so the
                // next present_post() sends its sync counter.
                window.m_in_configure = true;

                if (pos_changed) {
                    window.m_knst_event.window_root_x = config->x;
                    window.m_knst_event.window_root_y = config->y;
                }

                if (size_changed) {
                    window.m_syncConfigured = true;    // pending sync frame
                                                       // refreshed by this
                                                       // configure
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

            // The sync fallback (100 ms) now lives inside present_post().
            // It checks m_syncConfigured or a timeout to ack. Doing it
            // here would ack before the new-size frame is drawn and the
            // resize would look broken.

            // Must verify msg->type == WM_PROTOCOLS before treating
            // data32[0] as an atom. XDND messages also use data32[0]
            // (window ID), so a spurious match was possible. The atom
            // is cached in m_wmProtocols — no per-event round-trip.
            const bool is_wm_protocols =
                (KnstWindowSources::m_wmProtocols != 0) &&
                (msg->type == KnstWindowSources::m_wmProtocols);

            if (is_wm_protocols && msg->data.data32[0] == KnstWindowSources::m_wmDelete) {
                window.m_knst_event.begin(KNST_WINDOW_EVENT_CLOSE);
                window.should_close();
                window.dispatch_current_event();
            }
            else if (is_wm_protocols && msg->data.data32[0] == KnstWindowSources::m_wmSyncRequest) {
                window.m_syncPendingValue.lo = msg->data.data32[2];
                window.m_syncPendingValue.hi = (int32_t)msg->data.data32[3];
                window.m_syncHasPendingValue = true;
                window.m_syncRequestReceived = true;
                window.m_sync_received_at_ms = KnstWindowSources::get_current_time_ms();
            }
            else if (msg->type == KnstWindowSources::m_XdndEnter) {
                if (!window.m_drag_drop_enabled) break;

                window.m_xdnd_source = msg->data.data32[0];
                window.m_xdnd_version = (msg->data.data32[1] >> 24) & 0xFF;

                // data32[1] bit 0: does data32[2..4] hold more than 3
                // types? If yes, the actual list is in the XdndTypeList
                // property.
                const bool more_than_three = (msg->data.data32[1] & 0x1) != 0;

                bool has_uri_list = false;

                if (more_than_three) {
                    // Read XdndTypeList and look for text/uri-list.
                    xcb_get_property_cookie_t tc = xcb_get_property(
                        KnstWindowSources::m_connection,
                        0,
                        window.m_xdnd_source,
                        KnstWindowSources::m_XdndTypeList,
                        XCB_ATOM_ATOM,
                        0, 64);
                    xcb_get_property_reply_t* tr = xcb_get_property_reply(
                        KnstWindowSources::m_connection, tc, nullptr);
                    if (tr) {
                        int len = xcb_get_property_value_length(tr) / sizeof(xcb_atom_t);
                        xcb_atom_t* atoms = (xcb_atom_t*)xcb_get_property_value(tr);
                        for (int i = 0; i < len; ++i) {
                            if (atoms[i] == KnstWindowSources::m_textUriList) {
                                has_uri_list = true;
                                break;
                            }
                        }
                        free(tr);
                    }
                } else {
                    for (int i = 2; i <= 4; ++i) {
                        if (msg->data.data32[i] == KnstWindowSources::m_textUriList) {
                            has_uri_list = true;
                            break;
                        }
                    }
                }

                if (!has_uri_list) {
                    // The type we want isn't there — reject this drag
                    // by not sending XdndStatus.
                    break;
                }

                window.m_xdnd_selected_type = KnstWindowSources::m_textUriList;
                window.m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_ENTER);
                window.dispatch_current_event();
            }
            else if (msg->type == KnstWindowSources::m_XdndPosition) {
                if (!window.m_drag_drop_enabled) break;
                xcb_window_t source = msg->data.data32[0];

                // XdndPosition coords are ROOT-relative. Translate to
                // window-relative (matches Wayland/Windows).
                //
                // Coordinates are 16-bit SIGNED. Reading as unsigned
                // breaks negative-coordinate monitors.
                int32_t root_x = (int32_t)(int16_t)((msg->data.data32[2] >> 16) & 0xFFFF);
                int32_t root_y = (int32_t)(int16_t)(msg->data.data32[2] & 0xFFFF);

                xcb_translate_coordinates_cookie_t tc =
                    xcb_translate_coordinates(
                        KnstWindowSources::m_connection,
                        KnstWindowSources::m_root,
                        window.m_window,
                        root_x, root_y);
                xcb_translate_coordinates_reply_t* tr =
                    xcb_translate_coordinates_reply(
                        KnstWindowSources::m_connection, tc, nullptr);

                int32_t win_x = root_x;
                int32_t win_y = root_y;
                if (tr) {
                    win_x = tr->dst_x;
                    win_y = tr->dst_y;
                    free(tr);
                }

                window.m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_MOVE);
                window.m_knst_event.mouse_x = win_x;
                window.m_knst_event.mouse_y = win_y;

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
                // Reset the selected drag type. Otherwise a subsequent
                // uri-list-less drag would still match the previous
                // m_xdnd_selected_type and accept it, sending a bogus
                // DROP event.
                window.m_xdnd_selected_type = 0;
                window.m_xdnd_source = 0;

                window.m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_LEAVE);
                window.dispatch_current_event();
            }
            else if (msg->type == KnstWindowSources::m_XdndDrop) {
                if (!window.m_drag_drop_enabled) break;
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

            // If the source can't provide the data, notify XdndFinished
            // anyway so the source app doesn't hang waiting for us.
            if (notify->property == XCB_NONE &&
                notify->selection == KnstWindowSources::m_XdndSelection) {

                if (window.m_xdnd_source) {
                    xcb_client_message_event_t finished_ev{};
                    finished_ev.response_type = XCB_CLIENT_MESSAGE;
                    finished_ev.format = 32;
                    finished_ev.window = window.m_xdnd_source;
                    finished_ev.type = KnstWindowSources::m_XdndFinished;
                    finished_ev.data.data32[0] = window.m_window;
                    finished_ev.data.data32[1] = 0;   // not accepted
                    finished_ev.data.data32[2] = 0;

                    xcb_send_event(
                        KnstWindowSources::m_connection,
                        0,
                        window.m_xdnd_source,
                        XCB_EVENT_MASK_NO_EVENT,
                        (const char*)&finished_ev);
                    xcb_flush(KnstWindowSources::m_connection);
                }
                break;
            }

            // The source refused (property = NONE) — still emit the
            // event. Otherwise an app that called request_clipboard()
            // and is waiting would hang forever.
            if (notify->property == XCB_NONE &&
                (notify->selection == KnstWindowSources::m_CLIPBOARD ||
                 notify->selection == KnstWindowSources::m_PRIMARY)) {
                window.clipboard_text = u"";
                window.m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
                window.dispatch_current_event();
                break;
            }

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
                        knst_parse_uri_list(uriList, files);

                        window.m_knst_event.drop_files =  std::make_shared<knst_vector<knst_c16string>>(std::move(files));
                        window.m_knst_event.drop_count = (uint32_t)window.m_knst_event.drop_files->size();
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
                    else if (notify->selection == KnstWindowSources::m_CLIPBOARD ||
                             notify->selection == KnstWindowSources::m_PRIMARY) {
                        // Normal clipboard data arrived. Emit the event
                        // so the user knows it's ready.
                        window.clipboard_text = knst_c16string(data, len);

                        window.m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
                        window.dispatch_current_event();
                    }

                    free(reply);
                }
            }
            break;
        }

        case XCB_FOCUS_IN: {
            // Focus events caused by grab / ungrab are not real focus
            // changes. Alt+Tab or Super-opening-Overview would fire
            // spurious events.
            {
                auto* fe = (xcb_focus_in_event_t*)ev;
                if (fe->mode == XCB_NOTIFY_MODE_GRAB ||
                    fe->mode == XCB_NOTIFY_MODE_UNGRAB ||
                    fe->detail == XCB_NOTIFY_DETAIL_POINTER) {
                    break;
                }
            }
            if (window.m_xic) {
                XSetICFocus(window.m_xic);
            }
            window.m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_IN);
            window.m_knst_event.is_focused = true;
            window.dispatch_current_event();
            break;
        }

        case XCB_FOCUS_OUT: {
            {
                auto* fe = (xcb_focus_out_event_t*)ev;
                if (fe->mode == XCB_NOTIFY_MODE_GRAB ||
                    fe->mode == XCB_NOTIFY_MODE_UNGRAB ||
                    fe->detail == XCB_NOTIFY_DETAIL_POINTER) {
                    break;
                }
            }
            if (window.m_xic) {
                XUnsetICFocus(window.m_xic);
            }
            window.m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_OUT);
            window.m_knst_event.is_focused = false;
            // Alt+Tab: release events never arrive, the held-key table
            // would emit REPEAT forever. Clear on focus loss.
            window.clear_held_keys();
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

            // TEXT_INPUT via Xutf8LookupString. Handles AltGr, dead
            // keys, IME, Shift+digits, and any non-Latin layout.
            if (window.m_xic && KnstWindowSources::m_xlib_display) {
                XKeyEvent xev = {};
                xev.type = KeyPress;
                xev.display = KnstWindowSources::m_xlib_display;
                xev.window = window.m_window;
                xev.root = KnstWindowSources::m_root;
                xev.subwindow = 0;
                xev.time = key->time;
                xev.x = key->event_x;
                xev.y = key->event_y;
                xev.x_root = key->root_x;
                xev.y_root = key->root_y;
                xev.same_screen = True;
                xev.keycode = key->detail;
                xev.state = key->state;

                // Route through XIM (returns True if consumed by IME).
                if (!XFilterEvent((XEvent*)&xev, window.m_window)) {
                    char buf[64] = {0};
                    KeySym ks_ignored = 0;
                    Status status = 0;
                    int n = Xutf8LookupString(
                        window.m_xic,
                        &xev,
                        buf,
                        sizeof(buf) - 1,
                        &ks_ignored,
                        &status);

                    if (n > 0) {
                        const unsigned char c0 = (unsigned char)buf[0];
                        const bool is_control = (n == 1 && c0 < 0x20);
                        if (!is_control) {
                            window.m_knst_event.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
                            window.m_knst_event.text_input = knst_c16string(buf, (uint32_t)n);
                            window.dispatch_current_event();
                        }
                    }
                }
            }

            // The old code only used col=0/1. With NumLock on, keypad
            // sends XK_KP_End etc., which didn't match NUMPAD_1.
            // Correct: pick the column based on active modifiers.
            const bool shift_down = (key->state & ShiftMask) != 0;
            const bool caps_lock = (key->state & LockMask)  != 0;
            const bool num_lock = (key->state & Mod2Mask)  != 0;

            // CapsLock only affects letters. Numbers, punctuation,
            // function keys use shift_down directly. Grab the base
            // keysym and check whether it's a letter.
            xcb_keysym_t base_sym = xcb_key_symbols_get_keysym(KnstWindowSources::m_keysyms, key->detail, 0);

            const bool is_letter =
                (base_sym >= XK_a && base_sym <= XK_z) ||
                (base_sym >= XK_A && base_sym <= XK_Z);

            // XOR: shift+caps -> lowercase, caps only -> uppercase.
            bool shifted = is_letter
                            ? (shift_down != caps_lock)
                            : shift_down;

            const int col = shifted ? 1 : 0;
            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(KnstWindowSources::m_keysyms, key->detail, col);

            // NumLock: convert keypad KP_* to normal keys so pressing
            // NUMPAD_1 matches KNST_WINDOW_KEY_CODE_NUMPAD_1 (otherwise
            // you get XK_KP_End).
            if (num_lock) {
                switch (keysym) {
                    case XK_KP_Insert:   keysym = XK_KP_0; break;
                    case XK_KP_End:      keysym = XK_KP_1; break;
                    case XK_KP_Down:     keysym = XK_KP_2; break;
                    case XK_KP_Next:     keysym = XK_KP_3; break;
                    case XK_KP_Left:     keysym = XK_KP_4; break;
                    case XK_KP_Begin:    keysym = XK_KP_5; break;
                    case XK_KP_Right:    keysym = XK_KP_6; break;
                    case XK_KP_Home:     keysym = XK_KP_7; break;
                    case XK_KP_Up:       keysym = XK_KP_8; break;
                    case XK_KP_Prior:    keysym = XK_KP_9; break;
                    case XK_KP_Delete:   keysym = XK_KP_Decimal; break;
                    default: break;
                }
            }

            // NoSymbol (0) on a release event — check the held-key
            // table. If not there, emit nothing.
            if (keysym == 0 && !window.find_held_by_scancode(key->detail)) {
                break;
            }

            if (keysym >= XK_a && keysym <= XK_z) keysym -= 32;
            else if (keysym == XK_ccedilla)keysym = XK_Ccedilla;
            else if (keysym == XK_scedilla) keysym = XK_Scedilla;
            else if (keysym == XK_gbreve) keysym = XK_Gbreve;
            // Turkish dotless-i gets a distinct key code.
            else if (keysym == XK_idotless) keysym = KNST_WINDOW_KEY_CODE_I_DOTLESS;
            else if (keysym == XK_odiaeresis) keysym = XK_Odiaeresis;
            else if (keysym == XK_udiaeresis) keysym = XK_Udiaeresis;

            // NoSymbol (0) — Fn keys, dead keys. Don't add to the
            // held-key table; would emit infinite repeats.
            if (keysym == 0) break;

            if (window.find_held_by_scancode(key->detail)) break;

            window.m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window.add_held_key(keysym, key->detail, KnstWindowSources::get_current_time_ms());

            window.m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
            window.m_knst_event.key_code = keysym;
            window.m_knst_event.scancode = key->detail;

            {
                int mods = 0;
                if (key->state & ShiftMask) mods |= KNST_WINDOW_MOD_SHIFT;
                if (key->state & ControlMask) mods |= KNST_WINDOW_MOD_CONTROL;
                if (key->state & Mod1Mask) mods |= KNST_WINDOW_MOD_ALT;
                if (key->state & Mod4Mask) mods |= KNST_WINDOW_MOD_SUPER;
                if (key->state & LockMask) mods |= KNST_WINDOW_MOD_CAPS_LOCK;
                if (key->state & Mod2Mask) mods |= KNST_WINDOW_MOD_NUM_LOCK;
                window.m_knst_event.mods = mods;
            }
            window.dispatch_current_event();
            break;
        }

        case XCB_KEY_RELEASE: {
            xcb_key_release_event_t* key = (xcb_key_release_event_t*)ev;

            const bool shift_down = (key->state & ShiftMask) != 0;
            const bool caps_lock  = (key->state & LockMask)  != 0;
            const bool num_lock   = (key->state & Mod2Mask)  != 0;

            xcb_keysym_t base_sym = xcb_key_symbols_get_keysym(
                KnstWindowSources::m_keysyms, key->detail, 0);

            const bool is_letter =
                (base_sym >= XK_a && base_sym <= XK_z) ||
                (base_sym >= XK_A && base_sym <= XK_Z);

            bool shifted = is_letter ? (shift_down != caps_lock): shift_down;

            const int col = shifted ? 1 : 0;
            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(
                KnstWindowSources::m_keysyms, key->detail, col);

            if (num_lock) {
                switch (keysym) {
                    case XK_KP_Insert: keysym = XK_KP_0; break;
                    case XK_KP_End: keysym = XK_KP_1; break;
                    case XK_KP_Down: keysym = XK_KP_2; break;
                    case XK_KP_Next: keysym = XK_KP_3; break;
                    case XK_KP_Left: keysym = XK_KP_4; break;
                    case XK_KP_Begin: keysym = XK_KP_5; break;
                    case XK_KP_Right: keysym = XK_KP_6; break;
                    case XK_KP_Home: keysym = XK_KP_7; break;
                    case XK_KP_Up: keysym = XK_KP_8; break;
                    case XK_KP_Prior: keysym = XK_KP_9; break;
                    case XK_KP_Delete: keysym = XK_KP_Decimal; break;
                    default: break;
                }
            }

            if (keysym >= XK_a && keysym <= XK_z) keysym -= 32;
            else if (keysym == XK_ccedilla) keysym = XK_Ccedilla;
            else if (keysym == XK_scedilla) keysym = XK_Scedilla;
            else if (keysym == XK_gbreve) keysym = XK_Gbreve;
            // Turkish dotless-i gets a distinct key code.
            else if (keysym == XK_idotless) keysym = KNST_WINDOW_KEY_CODE_I_DOTLESS;
            else if (keysym == XK_odiaeresis) keysym = XK_Odiaeresis;
            else if (keysym == XK_udiaeresis) keysym = XK_Udiaeresis;

            // Use whatever keysym was set on press. If Shift is released
            // first, the keysym changes — wrong key_code.
            if (auto* hk = window.find_held_by_scancode(key->detail)) {
                keysym = (xcb_keysym_t)hk->key_code;
            }

            window.m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window.m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_RELEASE;
            window.m_knst_event.key_code = keysym;
            window.m_knst_event.scancode = key->detail;

            {
                int mods = 0;
                if (key->state & ShiftMask) mods |= KNST_WINDOW_MOD_SHIFT;
                if (key->state & ControlMask) mods |= KNST_WINDOW_MOD_CONTROL;
                if (key->state & Mod1Mask) mods |= KNST_WINDOW_MOD_ALT;
                if (key->state & Mod4Mask) mods |= KNST_WINDOW_MOD_SUPER;
                if (key->state & LockMask) mods |= KNST_WINDOW_MOD_CAPS_LOCK;
                if (key->state & Mod2Mask) mods |= KNST_WINDOW_MOD_NUM_LOCK;
                window.m_knst_event.mods = mods;
            }
            window.remove_held_key(key->detail);
            window.dispatch_current_event();
            break;
        }

        case XCB_GE_GENERIC: {
            xcb_ge_generic_event_t* ge = (xcb_ge_generic_event_t*)ev;

            // Extension check — is this an XI2 event or something else?
            if (KnstWindowSources::m_xi_opcode < 0 ||
                ge->extension != (uint8_t)KnstWindowSources::m_xi_opcode) {
                free(ev);
                return;
            }

            const uint16_t xi_ev = ge->event_type;

            // Touch events — XCB wire format.
            //
            // The old code cast to (XIDeviceEvent*), but that's an
            // XLIB struct. XCB has a different layout — the cast was
            // UB and read garbage for event_x / event_y.
            //
            // Correct: xcb_input_touch_begin_event_t and accessors.
            if (xi_ev == XCB_INPUT_TOUCH_BEGIN ||
                xi_ev == XCB_INPUT_TOUCH_UPDATE ||
                xi_ev == XCB_INPUT_TOUCH_END)
            {
                xcb_input_touch_begin_event_t* tbe = (xcb_input_touch_begin_event_t*)ev;

                auto fp1616 = [](xcb_input_fp1616_t v) -> double {
                    return (double)v / 65536.0;
                };

                const double tx = fp1616(tbe->event_x);
                const double ty = fp1616(tbe->event_y);
                const uint32_t touch_id = tbe->detail;

                if (window.m_touch_enabled) {
                    // Maintain multi-touch state. X11 has no "frame"
                    // event like Wayland, so we dispatch a full snapshot
                    // on every event — all currently active touches are
                    // reported together. Makes pinch/zoom work: the app
                    // sees 2 points with stable IDs.

                    int slot = -1;
                    for (int s = 0; s < KNST_MAX_TOUCH_POINTS; ++s) {
                        if (window.m_active_touches[s].id == (int32_t)touch_id) {
                            slot = s;
                            break;
                        }
                    }

                    if (xi_ev == XCB_INPUT_TOUCH_BEGIN) {
                        if (slot < 0) {
                            // New finger — find empty slot.
                            for (int s = 0; s < KNST_MAX_TOUCH_POINTS; ++s) {
                                if (window.m_active_touches[s].id < 0) {
                                    slot = s;
                                    break;
                                }
                            }
                        }
                        if (slot >= 0) {
                            window.m_active_touches[slot].id = (int32_t)touch_id;
                            window.m_active_touches[slot].x  = (float)tx;
                            window.m_active_touches[slot].y  = (float)ty;
                        }
                    } else if (xi_ev == XCB_INPUT_TOUCH_UPDATE) {
                        if (slot >= 0) {
                            window.m_active_touches[slot].x = (float)tx;
                            window.m_active_touches[slot].y = (float)ty;
                        }
                    } else {   // XCB_INPUT_TOUCH_END
                        if (slot >= 0) {
                            window.m_active_touches[slot].id = -1;
                            window.m_active_touches[slot].x  = 0.0f;
                            window.m_active_touches[slot].y  = 0.0f;
                        }
                    }

                    // Snapshot all active touches.
                    window.m_knst_event.begin(KNST_WINDOW_EVENT_TOUCH);
                    int out_idx = 0;
                    for (int s = 0; s < KNST_MAX_TOUCH_POINTS; ++s) {
                        if (window.m_active_touches[s].id >= 0) {
                            window.m_knst_event.pointer_id[out_idx] = window.m_active_touches[s].id;
                            window.m_knst_event.pointer_x[out_idx] = window.m_active_touches[s].x;
                            window.m_knst_event.pointer_y[out_idx] = window.m_active_touches[s].y;
                            ++out_idx;
                        }
                    }
                    window.m_knst_event.pointer_count = out_idx;

                    // Determine action.
                    const int active_count = out_idx;
                    if (xi_ev == XCB_INPUT_TOUCH_BEGIN) {
                        window.m_knst_event.touch_action = (active_count == 1)
                            ? KNST_WINDOW_TOUCH_ACTION_PRESS
                            : KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS;
                    } else if (xi_ev == XCB_INPUT_TOUCH_UPDATE) {
                        window.m_knst_event.touch_action =
                            KNST_WINDOW_TOUCH_ACTION_MOVE;
                    } else {
                        window.m_knst_event.touch_action = (active_count == 0)
                            ? KNST_WINDOW_TOUCH_ACTION_RELEASE
                            : KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE;
                    }

                    window.dispatch_current_event();
                } else {
                    // Touch is off — mouse fallback.
                    if (xi_ev == XCB_INPUT_TOUCH_BEGIN) {
                        window.m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
                        window.m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_PRESS;
                        window.m_knst_event.mouse_button = KNST_WINDOW_MOUSE_BUTTON_LEFT;


                        window.m_knst_event.mouse_x = (int32_t)tx;

                        window.m_knst_event.mouse_y = (int32_t)ty;



                    } else if (xi_ev == XCB_INPUT_TOUCH_UPDATE) {
                        window.m_knst_event.begin(KNST_WINDOW_EVENT_MOTION);
                        window.m_knst_event.mouse_x = (int32_t)tx;
                        window.m_knst_event.mouse_y = (int32_t)ty;
                    } else {
                        window.m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
                        window.m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_RELEASE;
                            
                        window.m_knst_event.mouse_button = KNST_WINDOW_MOUSE_BUTTON_LEFT;
                            
                        window.m_knst_event.mouse_x = (int32_t)tx;
                        window.m_knst_event.mouse_y = (int32_t)ty;
                    }
                    window.dispatch_current_event();
                }

                free(ev);
                return;
            }

            // Raw motion — for FPS-style cameras.
            //
            // xcb_input_raw_motion_event_axisvalues() doesn't exist in
            // this xcb-xinput version. Read the struct directly.
            //
            // xcb-input.xml RawMotion wire layout (after the struct):
            //   FP3232 valuators[valuators_len]   (8 bytes each)
            //   FP1616 axisvalues[...]            (4 bytes each)
            //   FP1616 axisvalues_raw[...]        (4 bytes each)
            //
            // We only need the first two valuators (X and Y).
            if (xi_ev == XCB_INPUT_RAW_MOTION && window.m_raw_mouse_enabled) {
                xcb_input_raw_motion_event_t* rm =
                    (xcb_input_raw_motion_event_t*)ev;

                double dx = 0.0, dy = 0.0;
                {
                    const uint32_t* vmask =
                        (const uint32_t*)(rm + 1);

                    int set_count = 0;
                    for (uint32_t w = 0; w < rm->valuators_len; ++w) {
                        set_count += __builtin_popcount(vmask[w]);
                    }

                    const xcb_input_fp3232_t* axis =
                        (const xcb_input_fp3232_t*)(vmask + rm->valuators_len);

                    // axisvalues_raw starts after axisvalues.
                    const xcb_input_fp3232_t* raw_vals = axis + set_count;

                    auto fp3232 = [](xcb_input_fp3232_t v) -> double {
                        return (double)v.integral + (double)v.frac / 4294967296.0;
                    };

                    int k = 0;
                    for (uint32_t bit = 0;
                         bit < rm->valuators_len * 32 && bit < 2;
                         ++bit) {
                        if (!(vmask[bit / 32] & (1u << (bit % 32)))) continue;
                        const double v = fp3232(raw_vals[k++]);
                        if (bit == 0) dx = v;
                        else dy = v;
                    }
                }

                // Don't squash fractional deltas into a 32-bit int —
                // small movements would get lost. Use accumulators.
                static thread_local double s_accum_x = 0.0;
                static thread_local double s_accum_y = 0.0;
                s_accum_x += dx;
                s_accum_y += dy;

                const int32_t dxi = (int32_t)s_accum_x;
                const int32_t dyi = (int32_t)s_accum_y;
                s_accum_x -= dxi;
                s_accum_y -= dyi;

                if (dxi != 0 || dyi != 0) {
                    window.m_knst_event.begin(KNST_WINDOW_EVENT_MOTION);
                    window.m_knst_event.is_raw = true;
                    window.m_knst_event.raw_delta_x = dxi;
                    window.m_knst_event.raw_delta_y = dyi;
                    window.dispatch_current_event();
                }

                free(ev);
                return;
            }

            // Unknown XI event — swallow.
            free(ev);
            return;
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

                    // We do NOT force configure_window here — the WM
                    // applies its own restore size. Otherwise the async
                    // configure would clash with ours and one frame
                    // would look empty (white flash).

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
                    window.m_knst_event.is_maximized = final_maximized;
                    window.m_knst_event.is_minimized = final_minimized;
                    window.dispatch_current_event();
                }
            }
            break;
        }

        case XCB_SELECTION_REQUEST: {
            xcb_selection_request_event_t* req = (xcb_selection_request_event_t*)ev;

            if (req->selection != KnstWindowSources::m_CLIPBOARD &&
                req->selection != KnstWindowSources::m_PRIMARY) {
                break;
            }

            const knst_c16string& src =
                (req->selection == KnstWindowSources::m_PRIMARY)
                    ? window.m_primary_owned
                    : window.m_clipboard_owned;
            knst_byte_string utf8_data(src);

            bool handled = false;
            xcb_atom_t reply_property = req->property;

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
                handled = true;
            }
            else if (req->target == KnstWindowSources::m_UTF8_STRING) {
                // X11 property size limit ~ 32 KB - 24 bytes header.
                // Without INCR support, anything bigger is silently
                // truncated. Warn once so the user knows.
                constexpr uint32_t X11_PROP_MAX = 32 * 1024 - 24;
                uint32_t len_to_send = utf8_data.length();
                if (len_to_send > X11_PROP_MAX) {
                    static bool s_warned = false;
                    if (!s_warned) {
                        s_warned = true;
                        fprintf(stderr,
                            "[KNST] X11 clipboard payload %u B exceeds the "
                            "%u B property limit. INCR not implemented — "
                            "data will be truncated. Copy smaller text or "
                            "use file drag-and-drop for large content.\n",
                            (unsigned)len_to_send, (unsigned)X11_PROP_MAX);
                    }
                    len_to_send = X11_PROP_MAX;
                }

                xcb_change_property(
                    KnstWindowSources::m_connection,
                    XCB_PROP_MODE_REPLACE,
                    req->requestor,
                    req->property,
                    req->target,
                    8,
                    len_to_send,
                    utf8_data.data()
                );
                handled = true;
            }
            else if (req->target == XCB_ATOM_STRING) {
                xcb_change_property(
                    KnstWindowSources::m_connection,
                    XCB_PROP_MODE_REPLACE,
                    req->requestor,
                    req->property,
                    XCB_ATOM_STRING,
                    8,
                    utf8_data.length(),
                    utf8_data.data()
                );
                handled = true;
            }
            else {
                reply_property = XCB_NONE;
                handled = false;
            }

            xcb_selection_notify_event_t notify = {};
            notify.response_type = XCB_SELECTION_NOTIFY;
            notify.requestor = req->requestor;
            notify.selection = req->selection;
            notify.target = req->target;
            notify.property = reply_property;
            notify.time = req->time;

            xcb_send_event(
                KnstWindowSources::m_connection,
                0, req->requestor,
                XCB_EVENT_MASK_NO_EVENT,
                (const char*)&notify
            );
            xcb_flush(KnstWindowSources::m_connection);
            (void)handled;
            break;
        }

        default:
            window.m_knst_event.type = KNST_WINDOW_EVENT_UNKNOWN;
            break;
    }
    free(ev);
}

#endif