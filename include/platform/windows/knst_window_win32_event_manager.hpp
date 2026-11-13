// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_win32_event_manager.hpp
----------------------------

    Event handling for the Windows backend.

*/

#pragma once

#if KNST_USING_PLATFORM_WINDOWS

#include <windowsx.h>

#define KNST_RESIZE_BORDER 8
#define KNST_CORNER_SIZE   12
#define KNST_BUTTON_WIDTH  48
#define KNST_RESIZE_TIMER_ID 0xA5F1

KNST_FORCE_INLINE LRESULT CALLBACK load_native_to_knst_event(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {

    // WM_NCCREATE arrives BEFORE WM_NCCALCSIZE. If we don't set user
    // data here, the first WM_NCCALCSIZE sees a null window and starts
    // with the default frame even when CSD is enabled.
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        knst_window* self = reinterpret_cast<knst_window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) {
            self->m_window = hwnd;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    knst_window* window = reinterpret_cast<knst_window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (window) {
        switch (msg) {

        case WM_NCHITTEST: {
            if (window->m_draw_custom_title_bar) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);

                const int x = pt.x;
                const int y = pt.y;
                const int w = window->m_knst_event.window_width;
                const int h = window->m_knst_event.window_height;

                // RESIZABLE=false — eat every HT* resize code, return
                // HTCLIENT. Even with CSD on, the window edges can't
                // be dragged.
                const bool resizable =
                    window->get_attribute(KNST_WINDOW_ATTR_RESIZABLE);

                // CSD callback (user installed via set_csd_hit_test).
                if (window->is_csd_enabled()) {
                    const knst_window::csd_hit hit =
                        window->hit_test_csd(x, y);

                    switch (hit) {
                        case knst_window::csd_hit::draggable:
                            return HTCAPTION;
                        case knst_window::csd_hit::resize_top_left:
                            return resizable ? HTTOPLEFT : HTCLIENT;
                        case knst_window::csd_hit::resize_top:
                            return resizable ? HTTOP : HTCLIENT;
                        case knst_window::csd_hit::resize_top_right:
                            return resizable ? HTTOPRIGHT : HTCLIENT;
                        case knst_window::csd_hit::resize_right:
                            return resizable ? HTRIGHT : HTCLIENT;
                        case knst_window::csd_hit::resize_bottom_right:
                            return resizable ? HTBOTTOMRIGHT : HTCLIENT;
                        case knst_window::csd_hit::resize_bottom:
                            return resizable ? HTBOTTOM : HTCLIENT;
                        case knst_window::csd_hit::resize_bottom_left:
                            return resizable ? HTBOTTOMLEFT : HTCLIENT;
                        case knst_window::csd_hit::resize_left:
                            return resizable ? HTLEFT : HTCLIENT;
                        case knst_window::csd_hit::normal:
                        default:
                            return HTCLIENT;
                    }
                }

                // CSD off but a custom title bar is drawn — do simple
                // automatic edge detection. Only when RESIZABLE=true.
                if (resizable) {
                    bool near_left = x < KNST_RESIZE_BORDER;
                    bool near_right = x >= w - KNST_RESIZE_BORDER;
                    bool near_top = y < KNST_RESIZE_BORDER;
                    bool near_bottom = y >= h - KNST_RESIZE_BORDER;

                    if (near_top && near_left) return HTTOPLEFT;
                    if (near_top && near_right) return HTTOPRIGHT;
                    if (near_bottom && near_left) return HTBOTTOMLEFT;
                    if (near_bottom && near_right) return HTBOTTOMRIGHT;
                    if (near_top) return HTTOP;
                    if (near_bottom) return HTBOTTOM;
                    if (near_left) return HTLEFT;
                    if (near_right) return HTRIGHT;
                }

                const int titlebar_h = window->get_title_bar_height();
                if (y >= 0 && y <= titlebar_h) return HTCAPTION;
                return HTCLIENT;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_NCCALCSIZE: {
            if (window->m_draw_custom_title_bar && wParam == TRUE) {
                NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                if (IsZoomed(hwnd)) {
                    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
                    if (hMon) {
                        MONITORINFO mi = { sizeof(MONITORINFO) };
                        if (GetMonitorInfoW(hMon, &mi)) {
                            params->rgrc[0] = mi.rcWork;
                        }
                    }
                }
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);

            // Skip the user's max-size hint while maximized or
            // fullscreen. Windows factors that value into the
            // SW_MAXIMIZE calculation, so a small hint breaks
            // maximize — the window grows to hint size instead of
            // monitor size.
            const bool skip_max_hint =
                window->m_knst_event.is_full_screen ||
                window->m_knst_event.is_maximized ||
                IsZoomed(hwnd);

            auto to_outer = [&](LONG cw, LONG ch) -> POINT {
                RECT r = { 0, 0, cw, ch };
                if (!window->m_draw_custom_title_bar) {
                    AdjustWindowRectEx(&r,
                        (DWORD)GetWindowLongW(hwnd, GWL_STYLE),
                        FALSE,
                        (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE));
                }
                return POINT{ r.right - r.left, r.bottom - r.top };
            };

            // Min-size always applies (harmless in maximize/fullscreen
            // — just a lower bound).
            if (POINT* mn = (POINT*)GetPropW(hwnd, L"KnstMinSize")) {
                mmi->ptMinTrackSize = to_outer(mn->x, mn->y);
            }

            // Max-size only applies in "normal" mode.
            if (!skip_max_hint) {
                if (POINT* mx = (POINT*)GetPropW(hwnd, L"KnstMaxSize")) {
                    mmi->ptMaxTrackSize = to_outer(mx->x, mx->y);
                } else {
                    int* maxW = (int*)GetPropW(hwnd, L"KnstMaxSizeWidth");
                    int* maxH = (int*)GetPropW(hwnd, L"KnstMaxSizeHeight");
                    if (maxW) mmi->ptMaxTrackSize.x = to_outer(*maxW, 0).x;
                    if (maxH) mmi->ptMaxTrackSize.y = to_outer(0, *maxH).y;
                }
            }
            return 0;
        }

        case WM_NCLBUTTONDBLCLK: {
            // Route native titlebar double-click to our set_maximized().
            // If we let DefWindowProcW handle it, the WM fires
            // SW_MAXIMIZE on its own — and since is_maximized isn't set
            // until WM_WINDOWPOSCHANGED, the max-size hint gets applied
            // and clips the maximize.
            if (wParam == HTCAPTION) {
                if (window->m_knst_event.is_maximized) window->restore();
                else window->set_maximized();
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);   // capture the mouse until button release
            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_PRESS;
            window->m_knst_event.mouse_button = 1;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            window->m_knst_event.mods = 0;
            if (wParam & MK_CONTROL) window->m_knst_event.mods |= KNST_WINDOW_MOD_CONTROL;
            if (wParam & MK_SHIFT)   window->m_knst_event.mods |= KNST_WINDOW_MOD_SHIFT;
            if (wParam & MK_ALT)     window->m_knst_event.mods |= KNST_WINDOW_MOD_ALT;

            // No hardcoded button detection. With CSD active the user's
            // callback decides what each pixel does; we send it as a
            // normal event. The app handles its own title-bar buttons.
            window->dispatch_current_event();
            return 0;
        }

        case WM_LBUTTONUP:
            // Don't release capture in DISABLED cursor mode — that
            // would undo the SetCapture from set_cursor_mode and let
            // the mouse escape the window.
            if (!window->m_cursor_disabled &&
                !(wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) {
                ReleaseCapture();
            }
            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_RELEASE;
            window->m_knst_event.mouse_button = 1;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            window->dispatch_current_event();
            return 0;

        case WM_RBUTTONDOWN:
            SetCapture(hwnd);
            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_PRESS;
            window->m_knst_event.mouse_button = 3;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            window->dispatch_current_event();
            return 0;

        case WM_RBUTTONUP:
            if (!window->m_cursor_disabled &&
                !(wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) {
                ReleaseCapture();
            }
            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_RELEASE;
            window->m_knst_event.mouse_button = 3;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);

            {

                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }


            window->dispatch_current_event();
            return 0;


        case WM_MBUTTONDOWN:
            SetCapture(hwnd);
            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_PRESS;
            window->m_knst_event.mouse_button = 2;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);

            {

                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }


            window->dispatch_current_event();
            return 0;

        case WM_MBUTTONUP:
            if (!window->m_cursor_disabled &&
                !(wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) {
                ReleaseCapture();
            }


            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_RELEASE;
            window->m_knst_event.mouse_button = 2;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }

            window->dispatch_current_event();
            return 0;

        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            const WORD xbtn = GET_XBUTTON_WPARAM(wParam);
            const int btn = (xbtn == XBUTTON1) ? KNST_WINDOW_MOUSE_BUTTON_BACK : KNST_WINDOW_MOUSE_BUTTON_FORWARD;


            const bool is_press = (msg == WM_XBUTTONDOWN);


            // WM_XBUTTON* lParam is already in CLIENT coordinates —
            // ScreenToClient would be wrong. Also: SetCapture only on
            // DOWN, ReleaseCapture on UP.
            if (is_press) SetCapture(hwnd);
            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            window->m_knst_event.mouse_action = is_press
                ? KNST_WINDOW_MOUSE_ACTION_PRESS
                : KNST_WINDOW_MOUSE_ACTION_RELEASE;
            window->m_knst_event.mouse_button = btn;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            if (!is_press && !window->m_cursor_disabled &&
                !(wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) {
                ReleaseCapture();
            }
            window->dispatch_current_event();
            return TRUE;   // must return TRUE for XBUTTON messages
        }

        case WM_MOUSEWHEEL: {
            // Accumulate raw delta to support high-precision wheels.
            const int raw = GET_WHEEL_DELTA_WPARAM(wParam);
            window->m_wheel_accum += raw;

            const int notches = window->m_wheel_accum / WHEEL_DELTA;
            if (notches == 0) return 0;   // wait for more
            window->m_wheel_accum -= notches * WHEEL_DELTA;

            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_SCROLL;
            window->m_knst_event.mouse_scroll_delta = notches;
            // X11/Wayland/Android parity: vertical scroll button = 4
            window->m_knst_event.mouse_button = 4;
            POINT wheel_pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &wheel_pt);
            window->m_knst_event.mouse_x = wheel_pt.x;
            window->m_knst_event.mouse_y = wheel_pt.y;
            window->dispatch_current_event();
            return 0;
        }

        case WM_MOUSEHWHEEL: {
            // Accumulate like the vertical wheel. High-precision
            // horizontal wheels send small deltas; dividing one by
            // itself loses the movement.
            window->m_hwheel_accum += GET_WHEEL_DELTA_WPARAM(wParam);
            const int notches = window->m_hwheel_accum / WHEEL_DELTA;
            if (notches == 0) return 0;
            window->m_hwheel_accum -= notches * WHEEL_DELTA;

            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
            window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_SCROLL;
            window->m_knst_event.mouse_scroll_delta = notches;
            window->m_knst_event.mouse_button = 6;   // horizontal scroll marker
            POINT hwheel_pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &hwheel_pt);
            window->m_knst_event.mouse_x = hwheel_pt.x;
            window->m_knst_event.mouse_y = hwheel_pt.y;
            window->dispatch_current_event();
            return 0;
        }


        case WM_MOUSEMOVE: {
            if (!window->m_knst_event.mouse_on_window) {
                window->m_knst_event.begin(KNST_WINDOW_EVENT_ENTER);
                window->m_knst_event.mouse_on_window = true;
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            } else {
                window->m_knst_event.begin(KNST_WINDOW_EVENT_MOTION);
            }
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            // Use GetKeyState for reliability (MK_ALT isn't always set).
            int mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= KNST_WINDOW_MOD_CONTROL;
            if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= KNST_WINDOW_MOD_SHIFT;
            if (GetKeyState(VK_MENU)    & 0x8000) mods |= KNST_WINDOW_MOD_ALT;
            if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
                mods |= KNST_WINDOW_MOD_SUPER;
            if (GetKeyState(VK_CAPITAL) & 0x0001) mods |= KNST_WINDOW_MOD_CAPS_LOCK;
            if (GetKeyState(VK_NUMLOCK) & 0x0001) mods |= KNST_WINDOW_MOD_NUM_LOCK;
            window->m_knst_event.mods = mods;
            window->dispatch_current_event();
            return 0;
        }

        case WM_MOUSELEAVE:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_LEAVE);
            window->m_knst_event.mouse_on_window = false;
            window->dispatch_current_event();
            return 0;

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (window->m_cursor) { SetCursor(window->m_cursor); return TRUE; }
                if (window->m_system_cursor) { SetCursor(window->m_system_cursor); return TRUE; }
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_ENTERSIZEMOVE:
            SetTimer(hwnd, KNST_RESIZE_TIMER_ID, 4, nullptr);
            return 0;

        case WM_EXITSIZEMOVE: {
            KillTimer(hwnd, KNST_RESIZE_TIMER_ID);

            // Store the final geometry; the RESIZE event already fired
            // from WM_WINDOWPOSCHANGED.
            RECT rc;
            GetWindowRect(hwnd, &rc);
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            window->m_knst_event.window_root_x = rc.left;
            window->m_knst_event.window_root_y = rc.top;
            window->m_knst_event.window_width = clientRect.right - clientRect.left;
            window->m_knst_event.window_height = clientRect.bottom - clientRect.top;
            window->m_knst_event.is_maximized  = IsZoomed(hwnd);

            window->call_redraw_callback();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_TIMER: {
            if (wParam == KNST_RESIZE_TIMER_ID) {
                // During the modal size/move loop the main app loop is
                // paused — no redraw happens while the user drags the
                // window, so the content freezes. WM_PAINT also doesn't
                // call the redraw callback (by design). Fire it here.
                // m_in_redraw guards against re-entrancy.
                window->call_redraw_callback();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_SIZING:
            // Let the default sizing algorithm run. RESIZE events come
            // from WM_WINDOWPOSCHANGED.
            return TRUE;

        case WM_SIZE:
            // All resize/minimize/maximize logic lives in
            // WM_WINDOWPOSCHANGED. Delegate so DefWindowProc can do
            // its thing.
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_MOVE:
            // MOVE logic lives in WM_WINDOWPOSCHANGED.
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_WINDOWPOSCHANGED: {
            WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam);
            if (!wp) return 0;

            auto& ev = window->m_knst_event;

            // MOVE (position changed).
            if (!(wp->flags & SWP_NOMOVE)) {
                RECT r;
                GetWindowRect(hwnd, &r);
                if (r.left != ev.window_root_x || r.top != ev.window_root_y) {
                    ev.window_root_x = r.left;
                    ev.window_root_y = r.top;
                    ev.begin(KNST_WINDOW_EVENT_MOVE);
                    window->dispatch_current_event();
                }
            }

            // Minimize / Maximize / Restore.
            const bool minimized = IsIconic(hwnd) != 0;
            const bool maximized = IsZoomed(hwnd) != 0;

            if (minimized != ev.is_minimized) {
                ev.is_minimized = minimized;
                ev.begin(minimized ? KNST_WINDOW_EVENT_MINIMIZE : KNST_WINDOW_EVENT_RESTORE);
                window->dispatch_current_event();
            }
            if (!minimized && maximized != ev.is_maximized) {
                ev.is_maximized = maximized;
                ev.begin(maximized ? KNST_WINDOW_EVENT_MAXIMIZE : KNST_WINDOW_EVENT_RESTORE);
                window->dispatch_current_event();
            }

            // Fullscreen detection.
            if (wp->cx > 0 && wp->cy > 0) {
                HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = {};
                mi.cbSize = sizeof(MONITORINFO);
                GetMonitorInfoW(hMon, &mi);

                bool coversFullScreen =
                    wp->x <= mi.rcMonitor.left &&
                    wp->y <= mi.rcMonitor.top &&
                    wp->x + wp->cx >= mi.rcMonitor.right &&
                    wp->y + wp->cy >= mi.rcMonitor.bottom;

                if (coversFullScreen && !ev.is_full_screen) {
                    ev.is_full_screen = true;
                    ev.begin(KNST_WINDOW_EVENT_FULLSCREEN);
                    window->dispatch_current_event();
                } else if (!coversFullScreen && ev.is_full_screen) {
                    ev.is_full_screen = false;
                    ev.begin(KNST_WINDOW_EVENT_RESTORE);
                    window->dispatch_current_event();
                }
            }

            // RESIZE (client size changed).
            if (!minimized && !(wp->flags & SWP_NOSIZE)) {
                RECT c;
                GetClientRect(hwnd, &c);
                const int w = c.right - c.left;
                const int h = c.bottom - c.top;
                if (w > 0 && h > 0 &&
                    (w != ev.window_width || h != ev.window_height)) {
                    ev.window_width  = w;
                    ev.window_height = h;
                    ev.begin(KNST_WINDOW_EVENT_RESIZE);
                    window->dispatch_current_event();
                    window->call_redraw_callback();
                }
            }

            return 0;
        }

        case WM_DPICHANGED: {
            // lParam is a suggested rect for the new DPI.
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr,
                    suggested->left, suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }

            // Refresh monitor list and notify the app — parity with
            // X11/Wayland. The app should update glViewport and the
            // projection matrix for the new DPI.
            knst_display::refresh_screens();
            window->m_knst_event.begin(KNST_WINDOW_EVENT_SCALE_CHANGED);
            window->dispatch_current_event();
            return 0;
        }

        // WM_CHAR — text input.
        //
        // Windows sends WM_CHAR for every character the user typed,
        // including AltGr, dead keys, IME, and Shift+digits. This is
        // the ONLY correct way to build text on Windows.
        //
        // Code units are UTF-16. Surrogate pairs (emoji, CJK ext) come
        // as two consecutive WM_CHAR messages.
        case WM_CHAR: {
            const wchar_t ch = (wchar_t)wParam;

            // Windows sends Backspace/Enter/Tab/Esc/Del as WM_CHAR too.
            // X11/Wayland filter them. Filter here too for parity.
            //   Backspace=0x08, Tab=0x09, Enter=0x0D, Esc=0x1B, Del=0x7F
            if (ch < 0x20 || ch == 0x7F) {
                return 0;
            }

            // High surrogate — save and wait for the low surrogate.
            if (ch >= 0xD800 && ch <= 0xDBFF) {
                window->m_text_high_surrogate = ch;
                return 0;
            }

            wchar_t buf[3] = { 0, 0, 0 };
            int n = 0;

            if (window->m_text_high_surrogate != 0 &&
                ch >= 0xDC00 && ch <= 0xDFFF) {
                buf[0] = window->m_text_high_surrogate;
                buf[1] = ch;
                n = 2;
                window->m_text_high_surrogate = 0;
            } else {
                buf[0] = ch;
                n = 1;
                window->m_text_high_surrogate = 0;
            }

            window->m_knst_event.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
            window->m_knst_event.text_input = knst_c16string(buf, (uint32_t)n);
            window->dispatch_current_event();
            return 0;
        }

        case WM_SYSCHAR:
            // Alt+Space: Windows system menu. Let DefWindowProc handle
            // it — otherwise Windows beeps.
            if (wParam == VK_SPACE) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
            // Swallow other WM_SYSCHARs. Alt+F4 and other system
            // shortcuts are already handled in WM_SYSKEYDOWN.
            return 0;

        case WM_SETFOCUS:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_IN);
            window->m_knst_event.is_focused = true;
            window->dispatch_current_event();

            // Re-apply cursor lock if we were in DISABLED mode before
            // losing focus.
            if (window->m_cursor_disabled) {
                SetCapture(hwnd);
                RECT rect;
                GetWindowRect(hwnd, &rect);
                ClipCursor(&rect);
            }
            return 0;

        case WM_KILLFOCUS:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_OUT);
            window->m_knst_event.is_focused = false;
            window->clear_held_keys();
            window->dispatch_current_event();

            // Windows auto-releases the clip on focus loss. Release
            // capture too so the desktop stays usable.
            // m_cursor_disabled stays true so FOCUS_IN re-applies.
            if (window->m_cursor_disabled) {
                ReleaseCapture();
                ClipCursor(nullptr);
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            // Alt+F4 and other system keys must go through
            // DefWindowProcW so Windows can handle them (close, menu,
            // etc.).
            if (msg == WM_SYSKEYDOWN) {
                if (wParam == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) {
                    return DefWindowProcW(hwnd, msg, wParam, lParam);
                }
            }

            int vk = (int)wParam;
            int scancode = (lParam >> 16) & 0xFF;
            // Extended key bit (bit 24) — distinguishes right Ctrl/Alt,
            // arrows from numpad, etc.
            if (lParam & (1 << 24)) scancode |= 0x100;

            if (window->find_held_by_scancode(scancode)) {
                return 0;
            }

            window->add_held_key(vk, scancode, KnstWindowSources::get_current_time_ms());

            window->m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
            window->m_knst_event.key_code = vk;
            window->m_knst_event.scancode = scancode;

            int mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= KNST_WINDOW_MOD_CONTROL;
            if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= KNST_WINDOW_MOD_SHIFT;
            if (GetKeyState(VK_MENU)    & 0x8000) mods |= KNST_WINDOW_MOD_ALT;
            if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
                mods |= KNST_WINDOW_MOD_SUPER;
            if (GetKeyState(VK_CAPITAL) & 0x0001) mods |= KNST_WINDOW_MOD_CAPS_LOCK;
            if (GetKeyState(VK_NUMLOCK) & 0x0001) mods |= KNST_WINDOW_MOD_NUM_LOCK;
            window->m_knst_event.mods = mods;

            window->dispatch_current_event();
            return 0;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            int scancode = (lParam >> 16) & 0xFF;
            if (lParam & (1 << 24)) scancode |= 0x100;

            window->m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_RELEASE;
            window->m_knst_event.key_code = (int)wParam;
            window->m_knst_event.scancode = scancode;
            window->m_knst_event.mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) window->m_knst_event.mods |= KNST_WINDOW_MOD_CONTROL;
            if (GetKeyState(VK_SHIFT) & 0x8000) window->m_knst_event.mods |= KNST_WINDOW_MOD_SHIFT;
            if (GetKeyState(VK_MENU) & 0x8000) window->m_knst_event.mods |= KNST_WINDOW_MOD_ALT;
            window->remove_held_key(scancode);
            window->dispatch_current_event();
            return 0;
        }

        case WM_NCACTIVATE: {
            if (window->m_draw_custom_title_bar) {
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_PAINT: {
            window->m_knst_event.begin(KNST_WINDOW_EVENT_EXPOSE);
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            window->dispatch_current_event();

            // Do NOT call call_redraw_callback() here.
            //
            // On X11/Wayland rendering belongs entirely to the main
            // loop: expose events queue up, the user's loop calls
            // call_redraw_callback(). On Windows WM_PAINT arrives
            // synchronously (during ShowWindow / UpdateWindow), so the
            // callback would fire before the user has created their
            // OpenGL/Vulkan context — crash.
            //
            // The main loop already calls call_redraw_callback() on
            // every iteration, so this is redundant. Leaving it out
            // makes all three platforms behave the same.
            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;

            window->m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP);
            if (!window->m_knst_event.drop_files) {
                window->m_knst_event.drop_files = std::make_shared<knst_vector<knst_c16string>>();
            } else {
                window->m_knst_event.drop_files->clear();
            }

            POINT pt;
            DragQueryPoint(hDrop, &pt);
            ScreenToClient(hwnd, &pt);
            window->m_knst_event.mouse_x = pt.x;
            window->m_knst_event.mouse_y = pt.y;

            UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < fileCount; i++) {
                UINT pathLen = DragQueryFileW(hDrop, i, nullptr, 0);
                wchar_t* buffer = new wchar_t[pathLen + 1];
                DragQueryFileW(hDrop, i, buffer, pathLen + 1);
                window->m_knst_event.drop_files->push_back(knst_c16string(buffer));
                delete[] buffer;
            }

            DragFinish(hDrop);
            window->dispatch_current_event();
            return 0;
        }

        case WM_CLOSE:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_CLOSE);
            window->should_close();
            window->dispatch_current_event();
            return 0;

        case WM_INPUT: {
            // Only process when raw input is enabled.
            if (!window->m_raw_mouse_enabled) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            UINT size = 0;
            GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
                            
            if (size == 0 || size > 256) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            BYTE buf[256];
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            RAWINPUT* raw = (RAWINPUT*)buf;
            if (raw->header.dwType != RIM_TYPEMOUSE) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
            if (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOTION);
            window->m_knst_event.is_raw = true;
            window->m_knst_event.raw_delta_x = raw->data.mouse.lLastX;
            window->m_knst_event.raw_delta_y = raw->data.mouse.lLastY;
            window->dispatch_current_event();
            // WM_INPUT must call DefWindowProc afterwards (per docs).
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        #if defined(KNST_WINDOW_ENABLE_TOUCH_SCREEN)
        case WM_TOUCH: {
            if (!window->m_touch_enabled) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            UINT cInputs = LOWORD(wParam);
            HTOUCHINPUT hTouch = (HTOUCHINPUT)lParam;

            if (cInputs > KNST_MAX_TOUCH_POINTS) cInputs = KNST_MAX_TOUCH_POINTS;

            TOUCHINPUT inputs[KNST_MAX_TOUCH_POINTS];
            if (!GetTouchInputInfo(hTouch, cInputs, inputs, sizeof(TOUCHINPUT))) {
                return 0;
            }

            auto& ev = window->m_knst_event;
            ev.begin(KNST_WINDOW_EVENT_TOUCH);

            int idx = 0;
            int primary_action = KNST_WINDOW_TOUCH_ACTION_MOVE;

            for (UINT i = 0; i < cInputs; ++i) {
                // TOUCHINPUT coords are in hundredths of a pixel.
                float x = (float)inputs[i].x / 100.0f;
                float y = (float)inputs[i].y / 100.0f;

                POINT pt = { (LONG)x, (LONG)y };
                ScreenToClient(hwnd, &pt);

                ev.pointer_x[idx] = (float)pt.x;
                ev.pointer_y[idx] = (float)pt.y;
                ev.pointer_id[idx] = (int)inputs[i].dwID;

                DWORD flags = inputs[i].dwFlags;
                if (flags & TOUCHEVENTF_DOWN) {
                    primary_action = (idx == 0)
                        ? KNST_WINDOW_TOUCH_ACTION_PRESS
                        : KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS;
                } else if (flags & TOUCHEVENTF_UP) {
                    primary_action = (idx == 0)
                        ? KNST_WINDOW_TOUCH_ACTION_RELEASE
                        : KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE;
                }
                idx++;
            }
            ev.pointer_count = idx;
            ev.touch_action  = primary_action;

            CloseTouchInputHandle(hTouch);
            window->dispatch_current_event();
            return 0;
        }
        #endif

        case WM_DISPLAYCHANGE: {
            // Monitor configuration changed (plug / unplug / res).
            knst_display::refresh_screens();
            return 0;
        }

        case WM_CLIPBOARDUPDATE: {
            // Clipboard contents changed. Notify the app.
            window->m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_UPDATE);
            window->dispatch_current_event();
            return 0;
        }

        case WM_DESTROY: {
            // The window was already unregistered — `<=1` was an
            // off-by-one. PostQuitMessage was being called while one
            // window remained. Must be `== 0`.
            if (knst_window_event_system::get_window_count() == 0) {
                PostQuitMessage(0);
            }
            return 0;
        }

        default:
            // Unknown messages must not touch the event state.
            // Otherwise some WM_* messages during motion corrupt
            // m_knst_event.
            break;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

#endif