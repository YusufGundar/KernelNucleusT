#ifndef KNST_WINDOW_WIN32_EVENT_MANAGER_HPP
#define KNST_WINDOW_WIN32_EVENT_MANAGER_HPP
#pragma once

#if KNST_USING_PLATFORM_WINDOWS

#include <windowsx.h>

#define KNST_RESIZE_BORDER 8
#define KNST_CORNER_SIZE   12
#define KNST_BUTTON_WIDTH  48

KNST_FORCE_INLINE LRESULT CALLBACK load_native_to_knst_event(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept{
    knst_window* window = reinterpret_cast<knst_window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (window) {
        switch (msg) {

            case WM_NCHITTEST: {
            #ifdef KNST_DISABLE_TITLE_BAR
            if (window->m_draw_custom_title_bar) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);

                int x = pt.x;
                int y = pt.y;
                int w = window->m_knst_event.window_width;
                int h = window->m_knst_event.window_height;
                int titlebar_h = window->get_title_bar_height();

                if (IsZoomed(hwnd)) {
                    if (y >= 0 && y <= titlebar_h) {
                        if (x >= w - KNST_BUTTON_WIDTH) return HTCLIENT;
                        if (x >= w - KNST_BUTTON_WIDTH * 2) return HTCLIENT;
                        if (x >= w - KNST_BUTTON_WIDTH * 3) return HTCLIENT;
                        return HTCAPTION;
                    }
                    return HTCLIENT;
                }

                bool near_left   = x < KNST_RESIZE_BORDER;
                bool near_right  = x >= w - KNST_RESIZE_BORDER;
                bool near_top    = y < KNST_RESIZE_BORDER;
                bool near_bottom = y >= h - KNST_RESIZE_BORDER;

                if (near_top && near_left) return HTTOPLEFT;
                if (near_top && near_right) return HTTOPRIGHT;
                if (near_bottom && near_left) return HTBOTTOMLEFT;
                if (near_bottom && near_right) return HTBOTTOMRIGHT;
                if (near_top)  return HTTOP;
                if (near_bottom) return HTBOTTOM;
                if (near_left) return HTLEFT;
                if (near_right) return HTRIGHT;

                if (y >= 0 && y <= titlebar_h) {
                    if (x >= w - KNST_BUTTON_WIDTH) return HTCLIENT;
                    if (x >= w - KNST_BUTTON_WIDTH * 2)  return HTCLIENT;
                    if (x >= w - KNST_BUTTON_WIDTH * 3)  return HTCLIENT;
                    return HTCAPTION;
                }
                return HTCLIENT;
            }
            #endif
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

        case WM_GETMINMAXINFO:
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_NCLBUTTONDOWN:
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_NCLBUTTONDBLCLK: {
            #ifdef KNST_DISABLE_TITLE_BAR
            if (window->m_draw_custom_title_bar && wParam == HTCAPTION) {
                if (window->m_knst_event.is_maximized) window->restore();
                else window->set_maximized();
                return 0;
            }
            #endif
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_LBUTTONDOWN: {
            window->m_knst_event.type = KNST_MOUSE_EVENT;
            window->m_knst_event.mouse_action = KNST_MOUSE_BUTTON_PRESS;
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
            if (wParam & MK_CONTROL) window->m_knst_event.mods |= KNST_MOD_CONTROL;
            if (wParam & MK_SHIFT)   window->m_knst_event.mods |= KNST_MOD_SHIFT;
            if (wParam & MK_ALT)     window->m_knst_event.mods |= KNST_MOD_ALT;

            #ifdef KNST_DISABLE_TITLE_BAR
            if (window->m_draw_custom_title_bar) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                int w = window->m_knst_event.window_width;
                int titlebar_h = window->get_title_bar_height();

                if (my >= 0 && my <= titlebar_h) {
                    if (mx >= w - KNST_BUTTON_WIDTH) {
                        window->should_close();
                        window->m_knst_event.type = KNST_CLOSE_WINDOW;
                        return 0;
                    }
                    else if (mx >= w - KNST_BUTTON_WIDTH * 2 && mx < w - KNST_BUTTON_WIDTH) {
                        if (window->m_knst_event.is_maximized) window->restore();
                        else window->set_maximized();
                        return 0;
                    }
                    else if (mx >= w - KNST_BUTTON_WIDTH * 3 && mx < w - KNST_BUTTON_WIDTH * 2) {
                        window->set_minimized();
                        return 0;
                    }
                }
            }
            #endif
            return 0;
        }

        case WM_LBUTTONUP:
            window->m_knst_event.type = KNST_MOUSE_EVENT;
            window->m_knst_event.mouse_action = KNST_MOUSE_BUTTON_RELEASE;
            window->m_knst_event.mouse_button = 1;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            return 0;

        case WM_RBUTTONDOWN:
            window->m_knst_event.type = KNST_MOUSE_EVENT;
            window->m_knst_event.mouse_action = KNST_MOUSE_BUTTON_PRESS;
            window->m_knst_event.mouse_button = 3;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            return 0;

        case WM_RBUTTONUP:
            window->m_knst_event.type = KNST_MOUSE_EVENT;
            window->m_knst_event.mouse_action = KNST_MOUSE_BUTTON_RELEASE;
            window->m_knst_event.mouse_button = 3;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            return 0;

        case WM_MBUTTONDOWN:
            window->m_knst_event.type = KNST_MOUSE_EVENT;
            window->m_knst_event.mouse_action = KNST_MOUSE_BUTTON_PRESS;
            window->m_knst_event.mouse_button = 2;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            return 0;

        case WM_MBUTTONUP:
            window->m_knst_event.type = KNST_MOUSE_EVENT;
            window->m_knst_event.mouse_action = KNST_MOUSE_BUTTON_RELEASE;
            window->m_knst_event.mouse_button = 2;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            return 0;

        case WM_MOUSEWHEEL:
            window->m_knst_event.type = KNST_MOUSE_EVENT;
            window->m_knst_event.mouse_action = KNST_MOUSE_SCROLL;
            window->m_knst_event.mouse_scroll_delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            return 0;

        case WM_MOUSEMOVE:
            if (!window->m_knst_event.mouse_on_window) {
                window->m_knst_event.type = KNST_ENTER_NOTIFY;
                window->m_knst_event.mouse_on_window = true;
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            } else {
                window->m_knst_event.type = KNST_MOTION_NOTIFY;
            }
            window->m_knst_event.mouse_x = GET_X_LPARAM(lParam);
            window->m_knst_event.mouse_y = GET_Y_LPARAM(lParam);
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                window->m_knst_event.mouse_root_x = pt.x;
                window->m_knst_event.mouse_root_y = pt.y;
            }
            window->m_knst_event.mods = 0;
            if (wParam & MK_CONTROL) window->m_knst_event.mods |= KNST_MOD_CONTROL;
            if (wParam & MK_SHIFT)   window->m_knst_event.mods |= KNST_MOD_SHIFT;
            if (wParam & MK_ALT)     window->m_knst_event.mods |= KNST_MOD_ALT;
            return 0;

        case WM_MOUSELEAVE:
            window->m_knst_event.type = KNST_LEAVE_NOTIFY;
            window->m_knst_event.mouse_on_window = false;
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_ENTERSIZEMOVE:
        
            return 0;

        case WM_EXITSIZEMOVE: {
            RECT rc;
            GetWindowRect(hwnd, &rc);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            
            window->m_knst_event.window_root_x = rc.left;
            window->m_knst_event.window_root_y = rc.top;
            window->m_knst_event.window_width  = clientRect.right - clientRect.left;
            window->m_knst_event.window_height = clientRect.bottom - clientRect.top;

            window->m_knst_event.is_maximized = IsZoomed(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_TIMER:
            return 0;

        case WM_SIZING: {
            RECT* rect = reinterpret_cast<RECT*>(lParam);
            int w = rect->right - rect->left;
            int h = rect->bottom - rect->top;
            if (w > 0 && h > 0) {
            
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                
                window->m_knst_event.window_width  = clientRect.right - clientRect.left;
                window->m_knst_event.window_height = clientRect.bottom - clientRect.top;
                window->m_knst_event.window_root_x = rect->left;
                window->m_knst_event.window_root_y = rect->top;
                window->m_knst_event.type = KNST_WINDOW_RESIZE;

                window->m_knst_event.is_maximized = IsZoomed(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return TRUE;
        }

        case WM_SIZE: {

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            
            window->m_knst_event.window_width  = clientRect.right - clientRect.left;
            window->m_knst_event.window_height = clientRect.bottom - clientRect.top;

            bool now_maximized = IsZoomed(hwnd);
            window->m_knst_event.is_maximized = now_maximized;
            window->m_knst_event.is_minimized = (wParam == SIZE_MINIMIZED);
            window->m_knst_event.is_full_screen = false;

            if (wParam == SIZE_MINIMIZED) {
                window->m_knst_event.type = KNST_WINDOW_MINIMIZE;
            } else if (wParam == SIZE_MAXIMIZED) {
                window->m_knst_event.type = KNST_WINDOW_MAXIMIZE;
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (wParam == SIZE_RESTORED) {
                window->m_knst_event.type = KNST_WINDOW_RESTORE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOVE: {
            RECT rect;
            GetWindowRect(hwnd, &rect);
            window->m_knst_event.window_root_x = rect.left;
            window->m_knst_event.window_root_y = rect.top;


            window->m_knst_event.is_maximized = IsZoomed(hwnd);
            return 0;
        }

        case WM_WINDOWPOSCHANGED: {
            WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam);
            if (wp->cx > 0 && wp->cy > 0) {
            
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                window->m_knst_event.window_width = clientRect.right - clientRect.left;
                window->m_knst_event.window_height = clientRect.bottom - clientRect.top;
            
                HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi;
                mi.cbSize = sizeof(MONITORINFO);
                GetMonitorInfoW(hMon, &mi);

                bool coversFullScreen =
                    wp->x <= mi.rcMonitor.left &&
                    wp->y <= mi.rcMonitor.top &&
                    wp->x + wp->cx >= mi.rcMonitor.right &&
                    wp->y + wp->cy >= mi.rcMonitor.bottom;

                if (coversFullScreen && !window->m_knst_event.is_full_screen) {
                    window->m_knst_event.is_full_screen = true;
                    window->m_knst_event.type = KNST_WINDOW_FULL_SCREEN;
                }

                window->m_knst_event.is_maximized = IsZoomed(hwnd);
            }
            return 0;
        }
        case WM_SETFOCUS:
            window->m_knst_event.type = KNST_FOCUS_IN;
            window->m_knst_event.is_focused = true;
            return 0;

        case WM_KILLFOCUS:
            window->m_knst_event.type = KNST_FOCUS_OUT;
            window->m_knst_event.is_focused = false;
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            window->m_knst_event.type = KNST_KEYBOARD_EVENT;
            if ((lParam & (1 << 30)) != 0) {
                window->m_knst_event.key_action = KNST_KEY_REPEAT;
            } else {
                window->m_knst_event.key_action = KNST_KEY_PRESS;
                window->m_knst_event.m_last_key = (int)wParam;
                window->m_knst_event.m_key_held = true;
            }
            window->m_knst_event.key_code = (int)wParam;
            window->m_knst_event.scancode = (lParam >> 16) & 0xFF;
            window->m_knst_event.mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) window->m_knst_event.mods |= KNST_MOD_CONTROL;
            if (GetKeyState(VK_SHIFT)   & 0x8000) window->m_knst_event.mods |= KNST_MOD_SHIFT;
            if (GetKeyState(VK_MENU)    & 0x8000) window->m_knst_event.mods |= KNST_MOD_ALT;
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            window->m_knst_event.type = KNST_KEYBOARD_EVENT;
            window->m_knst_event.key_action = KNST_KEY_RELEASE;
            window->m_knst_event.key_code = (int)wParam;
            window->m_knst_event.scancode = (lParam >> 16) & 0xFF;
            if ((int)wParam == window->m_knst_event.m_last_key) {
                window->m_knst_event.m_key_held = false;
                window->m_knst_event.m_last_key = 0;
            }
            return 0;

        case WM_NCACTIVATE: {
            #ifdef KNST_DISABLE_TITLE_BAR
            if (window->m_draw_custom_title_bar) {
                return TRUE;
            }
            #endif
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_PAINT: {
            window->m_knst_event.type = KNST_EXPOSE;
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            #ifndef KNST_DISABLE_REDRAW_ON_EVENT_MANAGER
               window->m_redraw_callback(*window, const_cast<void*>(window->get_user_data()));
            #endif
            

            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            
            window->m_knst_event.type = KNST_FILE_DROP;
            window->m_knst_event.drop_files.clear();
            
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
                window->m_knst_event.drop_files.push_back(knst_c16string(buffer));
                delete[] buffer;
            }
            
            DragFinish(hDrop);
            return 0;
        }

        case WM_CLOSE:
            window->m_knst_event.type = KNST_CLOSE_WINDOW;
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            window->m_knst_event.type = KNST_UNKNOWN;
            break;
                }
            }

            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

#endif
#endif // KNST_WINDOW_WIN32_EVENT_MANAGER_HPP
