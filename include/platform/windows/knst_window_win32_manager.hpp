// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_win32_manager.hpp
----------------------------

    Function definitions for the Windows backend.

*/

#pragma once

#if KNST_USING_PLATFORM_WINDOWS

#include <ole2.h>
#include <shlobj.h>


class knst_drop_target : public IDropTarget { // for drag-and-drop
private:
    knst_window* m_window;
    LONG m_ref_count = 1;
    bool m_is_hovering = false;

    // The old set_event blocked same-type events, so FILE_DROP_MOVE
    // only went through the first time. Now we use begin() and queue
    // every event.
    void emit_event(int type) {
        if (!m_window) return;
        m_window->m_knst_event.begin(type);
        m_window->dispatch_current_event();
    }

public:
    knst_drop_target(knst_window* window) : m_window(window) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_ref_count);
    }

    STDMETHODIMP_(ULONG) Release() override {
        LONG count = InterlockedDecrement(&m_ref_count);
        if (count == 0) {
            delete this;
            return 0;
        }
        return count;
    }

    STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        if (!is_files_dropping(pDataObj)) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }

        m_is_hovering = true;

        if (m_window) {
            POINT client_pt = { pt.x, pt.y };
            ScreenToClient(m_window->m_window, &client_pt);

            if (!m_window->m_knst_event.drop_files) {
                m_window->m_knst_event.drop_files =
                    std::make_shared<knst_vector<knst_c16string>>();
            }
            m_window->m_knst_event.drop_files->clear();

            m_window->m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_ENTER);
            m_window->m_knst_event.mouse_x = client_pt.x;
            m_window->m_knst_event.mouse_y = client_pt.y;
            m_window->dispatch_current_event();
        }

        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        if (!m_is_hovering || !m_window) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }

        POINT client_pt = { pt.x, pt.y };
        ScreenToClient(m_window->m_window, &client_pt);

        m_window->m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_MOVE);
        m_window->m_knst_event.mouse_x = client_pt.x;
        m_window->m_knst_event.mouse_y = client_pt.y;
        m_window->dispatch_current_event();
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    STDMETHODIMP DragLeave() override {
        if (!m_is_hovering) return S_OK;

        m_is_hovering = false;

        if (m_window) {
            // Don't clear the shared vector — just send the event.
            m_window->m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_LEAVE);
            m_window->m_knst_event.drop_files = nullptr;
            m_window->m_knst_event.drop_count  = 0;
            m_window->dispatch_current_event();
        }

        return S_OK;
    }

    STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        if (!is_files_dropping(pDataObj)) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }

        m_is_hovering = false;

        if (m_window) {
            // Fresh vector every drop — a shared one would corrupt
            // queued events.
            knst_vector<knst_c16string> files;

            FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            STGMEDIUM stg;
            if (SUCCEEDED(pDataObj->GetData(&fmt, &stg))) {
                HDROP hDrop = (HDROP)GlobalLock(stg.hGlobal);
                if (hDrop) {
                    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
                    for (UINT i = 0; i < fileCount; i++) {
                        UINT pathLen = DragQueryFileW(hDrop, i, nullptr, 0);
                        wchar_t* buffer = new wchar_t[pathLen + 1];
                        DragQueryFileW(hDrop, i, buffer, pathLen + 1);
                        files.push_back(knst_c16string(buffer));
                        delete[] buffer;
                    }
                    GlobalUnlock(stg.hGlobal);
                }
                ReleaseStgMedium(&stg);
            }

            POINT client_pt = { pt.x, pt.y };
            ScreenToClient(m_window->m_window, &client_pt);

            // begin() clears drop_files. Assign the new shared_ptr
            // after, then set mouse_x/y.
            m_window->m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP);
            m_window->m_knst_event.drop_files = std::make_shared<knst_vector<knst_c16string>>(std::move(files));
               
            m_window->m_knst_event.drop_count = (uint32_t)m_window->m_knst_event.drop_files->size();
                
            m_window->m_knst_event.mouse_x = client_pt.x;
            m_window->m_knst_event.mouse_y = client_pt.y;
            m_window->dispatch_current_event();
        }

        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

private:
    bool is_files_dropping(IDataObject* pDataObj) {
        FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return pDataObj->QueryGetData(&fmt) == S_OK;
    }
};


inline void knst_window::creation() noexcept {

    // Runtime CSD detection: the user may have called
    // set_csd_hit_test() before creation. If so, drop the native chrome
    // by marking the window as custom-decorated; WM_NCCALCSIZE then
    // removes the non-client area entirely.
    bool use_csd = is_csd_enabled();
#ifdef KNST_DISABLE_TITLE_BAR
    use_csd = true;   // legacy compile-time override still wins
#endif

    // Set the CSD flag BEFORE CreateWindowExW. Otherwise the window's
    // first WM_NCCALCSIZE is computed with the native frame; even if
    // SWP_FRAMECHANGED later fixes the client area, an invisible strip
    // remains at the top. Maximize/restore forces a re-layout so it
    // cleans up then.
    if (use_csd) {
        m_draw_custom_title_bar = true;
    }

    DWORD win_style = WS_OVERLAPPEDWINDOW;

    // Do NOT call AdjustWindowRectEx when CSD is on. That function adds
    // the native frame (caption + border) size; with CSD there's no
    // native frame, so it would make the window unnecessarily large and
    // shift the content offset.
    int outer_width  = m_knst_event.window_width;
    int outer_height = m_knst_event.window_height;

    if (!use_csd) {
        RECT adjust_rect = { 0, 0, m_knst_event.window_width, m_knst_event.window_height };
        AdjustWindowRectEx(&adjust_rect, win_style, FALSE, 0);
        outer_width  = adjust_rect.right  - adjust_rect.left;
        outer_height = adjust_rect.bottom - adjust_rect.top;
    }

    // Honor the position passed to the constructor. The old code always
    // used CW_USEDEFAULT and ignored window_root_x/y, so
    // knst_window(w, h, title, x, y) placed the window wherever Windows
    // decided.
    //
    // The sentinel KNST_WINDOW_DEFAULT (-10000) means "let Windows
    // pick". Anything else is an absolute screen coordinate.
    int init_x = m_knst_event.window_root_x;
    int init_y = m_knst_event.window_root_y;
    if (init_x == KNST_WINDOW_DEFAULT || init_x < 0) init_x = CW_USEDEFAULT;
    if (init_y == KNST_WINDOW_DEFAULT || init_y < 0) init_y = CW_USEDEFAULT;

    m_window = CreateWindowExW(
        0,
        L"KnstWindowClass",
        reinterpret_cast<const wchar_t*>(m_title.data()),
        win_style,
        init_x,
        init_y,
        outer_width,
        outer_height,
        nullptr,
        nullptr,
        KnstWindowSources::m_hInstance,
        this
    );
    if (m_window) {
        SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        AddClipboardFormatListener(m_window);

#if defined(KNST_WINDOW_ENABLE_TOUCH_SCREEN)
        // Touch screen detection + registration.
        int digitizer = GetSystemMetrics(SM_DIGITIZER);
        if (digitizer & 0x80) {   // NID_INTEGRATED_TOUCH
            m_touch_supported = true;
            m_max_touch_points = GetSystemMetrics(SM_MAXIMUMTOUCHES);
            if (m_max_touch_points <= 0) m_max_touch_points = 10;
            if (RegisterTouchWindow(m_window, 0)) {
                m_touch_enabled = true;
            } else {
                m_touch_supported = false;
            }
        }
#endif

        // Refresh the frame if use_csd OR KNST_DISABLE_TITLE_BAR is on.
        if (use_csd) {
            SetWindowPos(m_window, nullptr, 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                SWP_NOZORDER | SWP_NOACTIVATE);
        }

        RECT rect;
        GetWindowRect(m_window, &rect);
        m_knst_event.window_root_x = rect.left;
        m_knst_event.window_root_y = rect.top;
    }

    knst_window_event_system::register_window(this);
}

inline void knst_window::show() noexcept {
    if (m_window) {
        ShowWindow(m_window, SW_SHOW);
        UpdateWindow(m_window);
    }
}

inline void knst_window::destroy() noexcept {
    knst_window_event_system::unregister_window(this);

    notify_window_destroyed();

    if (m_drop_target) {
        set_drag_drop_status(false);
    }
    if (m_icon_big) {
        DestroyIcon(m_icon_big);
        m_icon_big = nullptr;
    }
    if (m_cursor) {
        DestroyCursor(m_cursor);
        m_cursor = nullptr;
    }
    m_system_cursor = nullptr;
    if (m_window) {
        POINT* min_prop = (POINT*)GetPropW(m_window, L"KnstMinSize");
        if (min_prop) { delete min_prop; RemovePropW(m_window, L"KnstMinSize"); }
        POINT* max_prop = (POINT*)GetPropW(m_window, L"KnstMaxSize");
        if (max_prop) { delete max_prop; RemovePropW(m_window, L"KnstMaxSize"); }
        int* max_w = (int*)GetPropW(m_window, L"KnstMaxSizeWidth");
        if (max_w) { delete max_w; RemovePropW(m_window, L"KnstMaxSizeWidth"); }
        int* max_h = (int*)GetPropW(m_window, L"KnstMaxSizeHeight");
        if (max_h) { delete max_h; RemovePropW(m_window, L"KnstMaxSizeHeight"); }

        RemoveClipboardFormatListener(m_window);
        // Clear user data before DestroyWindow: the pump no longer
        // sends WM_* to this window, but just in case.
        SetWindowLongPtrW(m_window, GWLP_USERDATA, 0);
        DestroyWindow(m_window);
        m_window = nullptr;
    }
}

inline void knst_window::set_title(const knst_c16string& title) noexcept {
    if (!m_window) return;
    SetWindowTextW(m_window, reinterpret_cast<LPCWSTR>(title.data()));
    m_title = title;
}

inline void knst_window::move(int root_x, int root_y, const knst_monitor& monitor) noexcept {
    if (!m_window) return;

    if (root_x < 0) root_x = m_knst_event.window_root_x;
    if (root_y < 0) root_y = m_knst_event.window_root_y;

    int absolute_x = monitor.root_x + root_x;
    int absolute_y = monitor.root_y + root_y;

    SetWindowPos(
        m_window, nullptr,
        absolute_x, absolute_y,
        0, 0,
        SWP_NOSIZE | SWP_NOZORDER
    );

    RECT rect;
    GetWindowRect(m_window, &rect);
    m_knst_event.window_root_x = rect.left;
    m_knst_event.window_root_y = rect.top;
}

inline void knst_window::move(int root_x, int root_y) noexcept {
    if (!m_window) return;

    if (root_x < 0) root_x = m_knst_event.window_root_x;
    if (root_y < 0) root_y = m_knst_event.window_root_y;

    SetWindowPos(m_window, nullptr, root_x, root_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    RECT rect;
    GetWindowRect(m_window, &rect);
    m_knst_event.window_root_x = rect.left;
    m_knst_event.window_root_y = rect.top;
}

inline void knst_window::toggle_fullscreen(bool fullscreen) noexcept {
    if (!m_window) return;

    if (fullscreen && !m_knst_event.is_full_screen) {

        if (m_knst_event.is_minimized || m_knst_event.is_maximized) {
            ShowWindow(m_window, SW_RESTORE);
            m_knst_event.is_minimized = false;
            m_knst_event.is_maximized = false;
        }

        m_prevPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_window, &m_prevPlacement);
        m_prevStyle = GetWindowLongW(m_window, GWL_STYLE);

        SetWindowLongW(m_window, GWL_STYLE, m_prevStyle & ~WS_OVERLAPPEDWINDOW);

        // Fullscreen override: Windows clamps SetWindowPos to
        // ptMaxTrackSize from WM_GETMINMAXINFO. If the user set a max
        // size, fullscreen gets clamped. Strip the max-size hint while
        // fullscreen, restore on exit. Per-window — a static would
        // break multi-window apps.
        if (POINT* mx = (POINT*)GetPropW(m_window, L"KnstMaxSize")) {
            m_saved_max_size = *mx;
            m_have_saved_max = true;
            RemovePropW(m_window, L"KnstMaxSize");
        } else {
            m_have_saved_max = false;
        }

        HMONITOR hMonitor = MonitorFromWindow(m_window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfoW(hMonitor, &mi);
        SetWindowPos(
            m_window, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW
        );

        m_knst_event.is_full_screen = true;
        m_knst_event.is_maximized   = false;
        m_knst_event.is_minimized   = false;

    } else if (!fullscreen && m_knst_event.is_full_screen) {

        SetWindowLongW(m_window, GWL_STYLE, m_prevStyle);

        SetWindowPlacement(m_window, &m_prevPlacement);

        SetWindowPos(m_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            

        // Restore the max-size property we stripped.
        if (m_have_saved_max) {
            SetPropW(m_window, L"KnstMaxSize", (HANDLE)new POINT{m_saved_max_size.x, m_saved_max_size.y});
            m_have_saved_max = false;
        }

        m_knst_event.is_full_screen = false;
    }
}

inline void knst_window::set_minimized() noexcept {
    if (!m_window) return;

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(m_window, &placement);

    if (placement.showCmd == SW_SHOWMINIMIZED) return;

    if (m_knst_event.is_maximized) {
        ShowWindow(m_window, SW_RESTORE);
        m_knst_event.is_maximized = false;
    }

    if (m_knst_event.is_full_screen) {
        toggle_fullscreen(false);
    }

    ShowWindow(m_window, SW_MINIMIZE);
}

inline void knst_window::set_maximized() noexcept {
    if (!m_window) return;

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(m_window, &placement);

    if (placement.showCmd == SW_SHOWMAXIMIZED) return;

    if (!m_knst_event.is_maximized && !m_knst_event.is_minimized) {
        m_prevPlacement = placement;
    }

    if (m_knst_event.is_minimized) {
        ShowWindow(m_window, SW_RESTORE);
        m_knst_event.is_minimized = false;
    }

    if (m_knst_event.is_full_screen) {
        toggle_fullscreen(false);
    }

    // Strip the max-size hint props before maximizing. SW_MAXIMIZE
    // reads ptMaxTrackSize from these props via WM_GETMINMAXINFO; without
    // stripping them the window grows to hint size instead of monitor
    // size.
    POINT saved_max = {};
    bool have_saved = false;
    if (POINT* mx = (POINT*)GetPropW(m_window, L"KnstMaxSize")) {
        saved_max = *mx;
        have_saved = true;
        RemovePropW(m_window, L"KnstMaxSize");
    }
    int saved_maxW = 0, saved_maxH = 0;
    bool have_saved_w = false, have_saved_h = false;
    if (int* mw = (int*)GetPropW(m_window, L"KnstMaxSizeWidth")) {
        saved_maxW = *mw; have_saved_w = true;
        RemovePropW(m_window, L"KnstMaxSizeWidth");
    }
    if (int* mh = (int*)GetPropW(m_window, L"KnstMaxSizeHeight")) {
        saved_maxH = *mh; have_saved_h = true;
        RemovePropW(m_window, L"KnstMaxSizeHeight");
    }

    m_knst_event.is_maximized = true;
    m_knst_event.is_minimized = false;

    ShowWindow(m_window, SW_MAXIMIZE);

    // Put the props back.
    if (have_saved) {
        SetPropW(m_window, L"KnstMaxSize", (HANDLE)new POINT{saved_max.x, saved_max.y});
    }
    if (have_saved_w) {
        SetPropW(m_window, L"KnstMaxSizeWidth", (HANDLE)new int{saved_maxW});
    }
    if (have_saved_h) {
        SetPropW(m_window, L"KnstMaxSizeHeight", (HANDLE)new int{saved_maxH});
    }

    RECT rect;
    GetClientRect(m_window, &rect);
    m_knst_event.window_width  = rect.right - rect.left;
    m_knst_event.window_height = rect.bottom - rect.top;

    call_redraw_callback();
}

inline void knst_window::restore() noexcept {
    if (!m_window) return;

    if (m_knst_event.is_full_screen) {
        toggle_fullscreen(false);
        return;
    }

    m_knst_event.is_maximized = false;
    m_knst_event.is_minimized = false;

    if (m_prevPlacement.length == sizeof(WINDOWPLACEMENT)) {
        SetWindowPlacement(m_window, &m_prevPlacement);
    } else {
        ShowWindow(m_window, SW_RESTORE);
    }

    RECT rect;
    GetClientRect(m_window, &rect);
    m_knst_event.window_width  = rect.right - rect.left;
    m_knst_event.window_height = rect.bottom - rect.top;

    call_redraw_callback();
}

inline void knst_window::hide() noexcept {
    if (!m_window) return;
    ShowWindow(m_window, SW_HIDE);
}

inline void knst_window::focus() noexcept {
    if (!m_window) return;

    SetForegroundWindow(m_window);

    SetFocus(m_window);

    if (IsIconic(m_window)) {
        ShowWindow(m_window, SW_RESTORE);
    }

    SetWindowPos(
        m_window,
        HWND_TOP,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW
    );
}

inline void knst_window::set_cursor(uint16_t cursor_type) noexcept {
    if (!m_window) return;

    // Coming out of DISABLED mode — release capture + clip.
    if (m_cursor_disabled) {
        ReleaseCapture();
        m_cursor_disabled = false;
    }
    ClipCursor(nullptr);

    // We may be coming out of HIDDEN/DISABLED/raw-mouse mode. If the
    // ShowCursor counter is negative the mouse stays invisible.
    // SetCursor doesn't reset the counter — need ShowCursor(TRUE).
    if (!m_cursor_visible) {
        while (ShowCursor(TRUE) < 0) {}
        m_cursor_visible = true;
    }

    // Destroy any custom (bitmap) cursor, otherwise WM_SETCURSOR keeps
    // using the old one.
    if (m_cursor) {
        DestroyCursor(m_cursor);
        m_cursor = nullptr;
    }
    HCURSOR hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(cursor_type));
    if (hCursor) {
        m_system_cursor = hCursor;
        SetCursor(hCursor);
    }
}

inline void knst_window::resize(int width, int height) noexcept {
    if (!m_window) return;

    if (width  < 0) width  = m_knst_event.window_width;
    if (height < 0) height = m_knst_event.window_height;

    DWORD style = (DWORD)GetWindowLongPtrW(m_window, GWL_STYLE);
    int outer_width  = width;
    int outer_height = height;
    if (!m_draw_custom_title_bar) {
        RECT adjust_rect = { 0, 0, width, height };
        AdjustWindowRectEx(&adjust_rect, style, FALSE, 0);
        outer_width  = adjust_rect.right  - adjust_rect.left;
        outer_height = adjust_rect.bottom - adjust_rect.top;
    }

    SetWindowPos(
        m_window, nullptr,
        0, 0,
        outer_width, outer_height,
        SWP_NOMOVE | SWP_NOZORDER
    );

    m_knst_event.window_width  = width;
    m_knst_event.window_height = height;
}

inline void knst_window::set_cursor_mode(int mode) noexcept {
    if (!m_window) return;

    const bool want_visible = (mode == KNST_WINDOW_CURSOR_ARROW);

    if (m_cursor_visible != want_visible) {
        if (want_visible) {
            while (ShowCursor(TRUE) < 0) {}
        } else {
            while (ShowCursor(FALSE) >= 0) {}
        }
        m_cursor_visible = want_visible;
    }

    if (mode == KNST_WINDOW_CURSOR_ARROW) {
        if (m_cursor_disabled) {
            ReleaseCapture();
            m_cursor_disabled = false;
        }
        ClipCursor(nullptr);
    } else if (mode == KNST_WINDOW_CURSOR_DISABLED) {
        // SetCapture must accompany ClipCursor. Without it WM_MOUSEMOVE
        // stops once the cursor nears the window edge and the OS lets
        // it escape to the desktop.
        SetCapture(m_window);
        m_cursor_disabled = true;

        RECT rect;
        GetWindowRect(m_window, &rect);
        ClipCursor(&rect);

        int cx = (rect.right - rect.left) / 2;
        int cy = (rect.bottom - rect.top) / 2;
        SetCursorPos(rect.left + cx, rect.top + cy);
    } else {
        // HIDDEN/other: no lingering clip or capture.
        if (m_cursor_disabled) {
            ReleaseCapture();
            m_cursor_disabled = false;
            ClipCursor(nullptr);
        }
    }
}
inline void knst_window::set_cursor_pos_on_window(int x, int y) noexcept {
    if (!m_window) return;

    if (x < 0) x = m_knst_event.mouse_x;
    if (y < 0) y = m_knst_event.mouse_y;

    POINT pt = { x, y };
    ClientToScreen(m_window, &pt);
    SetCursorPos(pt.x, pt.y);
}

inline void knst_window::set_cursor_pos_global(int root_x, int root_y) noexcept {
    if (!m_window) return;

    if (root_x == KNST_WINDOW_DEFAULT) root_x = m_knst_event.window_root_x;
    if (root_y == KNST_WINDOW_DEFAULT) root_y = m_knst_event.window_root_y;

    SetCursorPos(root_x, root_y);
}

inline void knst_window::set_clipboard(const knst_c16string& text) noexcept {
    if (!OpenClipboard(m_window)) return;
    EmptyClipboard();

    const SIZE_T chars = text.length();
    const SIZE_T size  = (chars + 1) * sizeof(wchar_t);   // +1 = NUL
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hMem) {
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        if (pMem) {
            // data() doesn't guarantee NUL — write it manually.
            memcpy(pMem, text.data(), chars * sizeof(wchar_t));
            pMem[chars] = L'\0';
            GlobalUnlock(hMem);

            // If SetClipboardData fails we must free the HGLOBAL,
            // otherwise it leaks.
            if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
                GlobalFree(hMem);
            }
        } else {
            GlobalFree(hMem);
        }
    }

    CloseClipboard();
}

inline void knst_window::request_clipboard() noexcept {
    if (!m_window) return;

    if (!OpenClipboard(m_window)) return;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        wchar_t* pData = (wchar_t*)GlobalLock(hData);
        if (pData) {
            clipboard_text = knst_c16string(pData);
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();

    // Parity with X11/Wayland: always emit CLIPBOARD_DATA.
    m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
    dispatch_current_event();
}


namespace knst_ole_detail {
    // OleInitialize must be refcounted per process. Every
    // OleInitialize pairs with exactly one OleUninitialize.
    inline int& ole_refcount() noexcept {
        static int s = 0;
        return s;
    }
    inline void add_ref() noexcept {
        if (ole_refcount()++ == 0) OleInitialize(nullptr);
    }
    inline void release_ref() noexcept {
        if (--ole_refcount() == 0) OleUninitialize();
    }
}

inline void knst_window::set_drag_drop_status(bool enabled) noexcept {
    if (!m_window) return;
    m_drag_drop_enabled = enabled;

    if (enabled && !m_drop_target) {
        knst_ole_detail::add_ref();
        m_drop_target = new knst_drop_target(this);
        RegisterDragDrop(m_window, m_drop_target);
    } else if (!enabled && m_drop_target) {
        RevokeDragDrop(m_window);
        m_drop_target->Release();
        m_drop_target = nullptr;
        knst_ole_detail::release_ref();
    }
}

// Raw mouse input (FPS-style).
inline void knst_window::enable_touch_screen(bool enable) noexcept {
    if (!m_touch_supported || !m_window) return;

#if defined(KNST_WINDOW_ENABLE_TOUCH_SCREEN)
    if (enable && !m_touch_enabled) {
        if (RegisterTouchWindow(m_window, 0)) m_touch_enabled = true;
    } else if (!enable && m_touch_enabled) {
        UnregisterTouchWindow(m_window);
        m_touch_enabled = false;
    }
#endif
}

inline void knst_window::set_display_affinity(bool protect) noexcept {
    if (!m_window) return;

    // WDA_NONE = 0, WDA_MONITOR = 1, WDA_EXCLUDEFROMCAPTURE = 0x11
    DWORD affinity = protect ? 0x11 /* WDA_EXCLUDEFROMCAPTURE */ : 0x00 /* WDA_NONE */;
                             

    typedef BOOL (WINAPI* PFN_SetWindowDisplayAffinity)(HWND, DWORD);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = (PFN_SetWindowDisplayAffinity)
            GetProcAddress(user32, "SetWindowDisplayAffinity");
        if (fn) {
            fn(m_window, affinity);
        }
    }
}

inline void knst_window::set_raw_mouse_mode(bool enable) noexcept {
    if (!m_window) return;

    if (enable && !m_raw_mouse_enabled) {
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = 0x01;   // Generic Desktop Controls
        rid.usUsage = 0x02;   // Mouse
        rid.dwFlags = 0;
        rid.hwndTarget = m_window;

        if (RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            m_raw_mouse_enabled = true;
            // ShowCursor is a counter — not idempotent. Track real
            // state via m_cursor_visible.
            if (m_cursor_visible) {
                while (ShowCursor(FALSE) >= 0) {}
                m_cursor_visible = false;
            }
        }
    } else if (!enable && m_raw_mouse_enabled) {
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = nullptr;

        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        m_raw_mouse_enabled = false;
        if (!m_cursor_visible) {
            while (ShowCursor(TRUE) < 0) {}
            m_cursor_visible = true;
        }
    }
}

inline void knst_window::set_opacity(float opacity) noexcept {
    if (!m_window) return;

    // Parity with other platforms: clamp [0, 1].
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;

    m_opacity = opacity;
    SetWindowLongW(m_window, GWL_EXSTYLE, GetWindowLongW(m_window, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(m_window, 0, (BYTE)(opacity * 255.0f), LWA_ALPHA);
                               
}

inline void knst_window::set_attribute(int attribute, bool value) noexcept {
    if (!m_window) return;

    switch (attribute) {
        case KNST_WINDOW_ATTR_DECORATED: {
            DWORD style = GetWindowLongPtrW(m_window, GWL_STYLE);
            style = value ? WS_OVERLAPPEDWINDOW : (WS_POPUP | WS_VISIBLE);
            SetWindowLongPtrW(m_window, GWL_STYLE, style);
            SetWindowPos(m_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                
            ShowWindow(m_window, SW_SHOW);
            m_draw_custom_title_bar = !value;
            break;
        }
        case KNST_WINDOW_ATTR_RESIZABLE: {
            DWORD style = GetWindowLongPtrW(m_window, GWL_STYLE);
            if (value) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
            else style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
            SetWindowLongPtrW(m_window, GWL_STYLE, style);
            SetWindowPos(m_window, nullptr, 0, 0, 0, 0,  SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            break;
        }
        case KNST_WINDOW_ATTR_ALWAYS_ON_TOP:
            SetWindowPos(m_window, value ? HWND_TOPMOST : HWND_NOTOPMOST,  0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            break;

        case KNST_WINDOW_ATTR_TRANSPARENT: {
            if (value) {
                LONG_PTR exStyle = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
                SetWindowLongPtrW(m_window, GWL_EXSTYLE,  exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED);
                   
                SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA);
            } else {
                LONG_PTR exStyle = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
                SetWindowLongPtrW(m_window, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
                    
            }
            SetWindowPos(m_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            m_input_transparent = value;
            break;
        }
    }
}

inline bool knst_window::get_attribute(int attribute) const noexcept {
    if (!m_window) return false;

    switch (attribute) {
        case KNST_WINDOW_ATTR_DECORATED: {
            if (m_draw_custom_title_bar) return false;
            DWORD style = GetWindowLongPtrW(m_window, GWL_STYLE);
            return (style & WS_CAPTION) != 0;
        }
        case KNST_WINDOW_ATTR_RESIZABLE: {
            DWORD style = GetWindowLongPtrW(m_window, GWL_STYLE);
            return (style & WS_THICKFRAME) != 0;
        }
        case KNST_WINDOW_ATTR_ALWAYS_ON_TOP:
            return (GetWindowLongPtrW(m_window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        case KNST_WINDOW_ATTR_TRANSPARENT:
            return m_input_transparent;
    }
    return false;
}

inline void knst_window::apply_bmp_icon(const knst_byte_string& data, int width, int height) noexcept {
    if (!m_window || data.empty() || width <= 0 || height <= 0) return;

    const uint8_t* pixel_data = (const uint8_t*)data.data();

    // Windows bitmap AND masks are 16-bit (word) aligned per scanline.
    // The old formula ((width + 7) / 8) underallocates when width isn't
    // a multiple of 16 — buffer overread on CreateIcon / CreateBitmap.
    // Compute the correct stride here.
    const uint32_t and_stride = ((width + 15) / 16) * 2;   // bytes / row
    const uint32_t and_mask_size = and_stride * (uint32_t)height;
    knst_vector<uint8_t> and_mask(and_mask_size, 0);

    if (data.length() == (size_t)width * height * 4) {

        knst_vector<uint8_t> bgra_data(width * height * 4);
        for (int i = 0; i < width * height; i++) {
            bgra_data[i * 4 + 0] = pixel_data[i * 4 + 2];  // B
            bgra_data[i * 4 + 1] = pixel_data[i * 4 + 1];  // G
            bgra_data[i * 4 + 2] = pixel_data[i * 4 + 0];  // R
            bgra_data[i * 4 + 3] = pixel_data[i * 4 + 3];  // A
        }

        HICON hIcon = CreateIcon(
            GetModuleHandleW(nullptr),
            width, height,
            1, 32,
            and_mask.data(),
            (BYTE*)bgra_data.data()
        );

        if (hIcon) {
            if (m_icon_big) DestroyIcon(m_icon_big);
            m_icon_big = hIcon;
            SendMessageW(m_window, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(m_window, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
    }
    else if (data.length() == (size_t)width * height * 3) {
        knst_vector<uint8_t> bgra_data(width * height * 4);
        for (int i = 0; i < width * height; i++) {
            bgra_data[i * 4 + 0] = pixel_data[i * 3 + 2];  // B
            bgra_data[i * 4 + 1] = pixel_data[i * 3 + 1];  // G
            bgra_data[i * 4 + 2] = pixel_data[i * 3 + 0];  // R
            bgra_data[i * 4 + 3] = 255;                    // A
        }

        HICON hIcon = CreateIcon(
            GetModuleHandleW(nullptr),
            width, height,
            1, 32,
            and_mask.data(),
            (BYTE*)bgra_data.data()
        );

        if (hIcon) {
            if (m_icon_big) DestroyIcon(m_icon_big);
            m_icon_big = hIcon;
            SendMessageW(m_window, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(m_window, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
    }
}


inline void knst_window::set_bmp_cursor(
    const knst_byte_string& data,
    int width,
    int height,
    int hot_x,
    int hot_y
) noexcept {

    if (!m_window || data.empty() || width <= 0 || height <= 0) return;

    // Coming out of DISABLED mode — release capture + clip.
    if (m_cursor_disabled) {
        ReleaseCapture();
        m_cursor_disabled = false;
    }
    ClipCursor(nullptr);

    const uint8_t* pixel_data = (const uint8_t*)data.data();
    uint32_t data_len = data.length();
    uint32_t expected_4ch = (uint32_t)(width * height * 4);
    uint32_t expected_3ch = (uint32_t)(width * height * 3);

    knst_vector<uint8_t> bgra_data;
    knst_vector<uint8_t> alpha_data;

    if (data_len == expected_4ch) {
        bgra_data.resize(width * height * 4);
        alpha_data.resize(width * height);

        for (int i = 0; i < width * height; i++) {
            bgra_data[i * 4 + 0] = pixel_data[i * 4 + 2];  // B
            bgra_data[i * 4 + 1] = pixel_data[i * 4 + 1];  // G
            bgra_data[i * 4 + 2] = pixel_data[i * 4 + 0];  // R
            bgra_data[i * 4 + 3] = pixel_data[i * 4 + 3];  // A
            alpha_data[i] = pixel_data[i * 4 + 3];
        }
    } else if (data_len == expected_3ch) {
        // RGB -> BGRA (alpha = 255).
        bgra_data.resize(width * height * 4);
        alpha_data.resize(width * height, 255);

        for (int i = 0; i < width * height; i++) {
            bgra_data[i * 4 + 0] = pixel_data[i * 3 + 2];
            bgra_data[i * 4 + 1] = pixel_data[i * 3 + 1];
            bgra_data[i * 4 + 2] = pixel_data[i * 3 + 0];
            bgra_data[i * 4 + 3] = 255;
        }
    } else {
        return; // unknown format
    }

    // 16-bit (word) aligned scanlines, MSB-first bit order — Windows
    // GDI requires both.
    const uint32_t and_stride = ((width + 15) / 16) * 2;
    const uint32_t and_mask_size = and_stride * (uint32_t)height;
    std::vector<uint8_t> and_mask(and_mask_size, 0x00);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            uint8_t alpha = alpha_data[idx];

            if (alpha < 128) {
                // MSB-first: bit 7 of the byte is leftmost pixel.
                const uint32_t byte_off = (uint32_t)y * and_stride + (uint32_t)(x / 8);
                const uint8_t  bit_mask = (uint8_t)(0x80 >> (x % 8));
                and_mask[byte_off] |= bit_mask;
            }
        }
    }
    if (hot_x < 0 || hot_x >= width)  hot_x = width  / 2;
    if (hot_y < 0 || hot_y >= height) hot_y = height / 2;

    HCURSOR hCursor = nullptr;

    HBITMAP hbmMask = CreateBitmap(width, height, 1, 1, and_mask.data());
    HBITMAP hbmColor = CreateBitmap(width, height, 1, 32, bgra_data.data());

    if (hbmMask && hbmColor) {
        ICONINFO iconInfo = {};
        iconInfo.fIcon    = FALSE;
        iconInfo.xHotspot = hot_x;
        iconInfo.yHotspot = hot_y;
        iconInfo.hbmMask  = hbmMask;
        iconInfo.hbmColor = hbmColor;

        hCursor = CreateIconIndirect(&iconInfo);
    }

    if (hbmMask)  DeleteObject(hbmMask);
    if (hbmColor) DeleteObject(hbmColor);

    if (!hCursor) {
        hCursor = CreateCursor(
            GetModuleHandleW(nullptr),
            hot_x, hot_y,
            width, height,
            and_mask.data(),
            bgra_data.data()
        );
    }

    if (!hCursor) return;

    // We may be coming out of HIDDEN/DISABLED mode.
    if (!m_cursor_visible) {
        while (ShowCursor(TRUE) < 0) {}
        m_cursor_visible = true;
    }

    if (m_cursor) {
        DestroyCursor(m_cursor);
        m_cursor = nullptr;
    }

    m_cursor = hCursor;
    m_system_cursor = nullptr;
    SetCursor(hCursor);
}

inline void knst_window::reset_cursor() noexcept {

    if (!m_window) return;

    // Coming out of DISABLED mode — release capture + clip.
    if (m_cursor_disabled) {
        ReleaseCapture();
        m_cursor_disabled = false;
    }
    ClipCursor(nullptr);

    // We may be coming out of HIDDEN/DISABLED/raw-mouse mode.
    if (!m_cursor_visible) {
        while (ShowCursor(TRUE) < 0) {}
        m_cursor_visible = true;
    }

    m_system_cursor = nullptr;
    if (m_cursor) {
        DestroyCursor(m_cursor);
        m_cursor = nullptr;
    }

    SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

inline void knst_window::set_minimum_size(int width, int height) noexcept {
    if (!m_window) return;

    // DEFAULT/DEFAULT — remove the min-size limit entirely.
    if (width == KNST_WINDOW_DEFAULT && height == KNST_WINDOW_DEFAULT) {
        POINT* min_prop = (POINT*)GetPropW(m_window, L"KnstMinSize");
        if (min_prop) { delete min_prop; RemovePropW(m_window, L"KnstMinSize"); }

        return;
    }

    if (width  == KNST_WINDOW_DEFAULT) width  = 300;
    if (height == KNST_WINDOW_DEFAULT) height = 250;
    LONG_PTR style = GetWindowLongPtrW(m_window, GWL_STYLE);

    if (!(style & WS_THICKFRAME)) {
        style |= WS_THICKFRAME;
        SetWindowLongPtrW(m_window, GWL_STYLE, style);
        SetWindowPos(m_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    POINT* old_min = (POINT*)GetPropW(m_window, L"KnstMinSize");
    if (old_min) delete old_min;
    SetPropW(m_window, L"KnstMinSize", (HANDLE)new POINT{width, height});

    RECT rect;
    GetWindowRect(m_window, &rect);
    int currentWidth  = rect.right - rect.left;
    int currentHeight = rect.bottom - rect.top;

    if (currentWidth < width || currentHeight < height) {
        int newWidth  = std::max(currentWidth, width);
        int newHeight = std::max(currentHeight, height);
        SetWindowPos(m_window, nullptr, 0, 0, newWidth, newHeight, SWP_NOMOVE | SWP_NOZORDER);
    }
}

inline void knst_window::set_maximum_size(int width, int height) noexcept {
    if (!m_window) return;

    if (width == KNST_WINDOW_DEFAULT && height == KNST_WINDOW_DEFAULT) {
        // Just returning isn't enough — clear the properties.
        POINT* old_max = (POINT*)GetPropW(m_window, L"KnstMaxSize");
        if (old_max) { delete old_max; RemovePropW(m_window, L"KnstMaxSize"); }
        int* old_w = (int*)GetPropW(m_window, L"KnstMaxSizeWidth");
        if (old_w) { delete old_w; RemovePropW(m_window, L"KnstMaxSizeWidth"); }
        int* old_h = (int*)GetPropW(m_window, L"KnstMaxSizeHeight");
        if (old_h) { delete old_h; RemovePropW(m_window, L"KnstMaxSizeHeight"); }
        return;
    }

    LONG_PTR style = GetWindowLongPtrW(m_window, GWL_STYLE);

    if (!(style & WS_THICKFRAME)) {
        style |= WS_THICKFRAME;
        SetWindowLongPtrW(m_window, GWL_STYLE, style);
        SetWindowPos(m_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            
    }

    if (width != KNST_WINDOW_DEFAULT && height != KNST_WINDOW_DEFAULT) {

        POINT* old_max = (POINT*)GetPropW(m_window, L"KnstMaxSize");
        if (old_max) delete old_max;
        int* old_w = (int*)GetPropW(m_window, L"KnstMaxSizeWidth");
        if (old_w) { delete old_w; RemovePropW(m_window, L"KnstMaxSizeWidth"); }
        int* old_h = (int*)GetPropW(m_window, L"KnstMaxSizeHeight");
        if (old_h) { delete old_h; RemovePropW(m_window, L"KnstMaxSizeHeight"); }
        SetPropW(m_window, L"KnstMaxSize", (HANDLE)new POINT{width, height});

    } else if (width != KNST_WINDOW_DEFAULT) {

        int* old_w = (int*)GetPropW(m_window, L"KnstMaxSizeWidth");
        if (old_w) delete old_w;
        SetPropW(m_window, L"KnstMaxSizeWidth", (HANDLE)new int{width});
        POINT* old_max = (POINT*)GetPropW(m_window, L"KnstMaxSize");
        if (old_max) { delete old_max; RemovePropW(m_window, L"KnstMaxSize"); }
        RemovePropW(m_window, L"KnstMaxSizeHeight");
    } else if (height != KNST_WINDOW_DEFAULT) {

        int* old_h = (int*)GetPropW(m_window, L"KnstMaxSizeHeight");
        if (old_h) delete old_h;
        SetPropW(m_window, L"KnstMaxSizeHeight", (HANDLE)new int{height});
        POINT* old_max = (POINT*)GetPropW(m_window, L"KnstMaxSize");
        if (old_max) { delete old_max; RemovePropW(m_window, L"KnstMaxSize"); }
        RemovePropW(m_window, L"KnstMaxSizeWidth");
    }
}


// Windows — platform-parity no-ops.
//
// These APIs do real work on X11/Wayland. Windows has no protocol
// support, so they must be defined as no-ops — otherwise the linker
// complains.

inline void knst_window::set_primary_selection(const knst_c16string& /*text*/) noexcept {
    // Windows has no primary-selection (X11 middle-click paste).
}

inline void knst_window::request_primary_selection() noexcept {
    // no-op
}

inline void knst_window::set_idle_inhibit(bool enable) noexcept {
    // On Windows SetThreadExecutionState is per-thread but the effect
    // is system-wide. Set once with ES_CONTINUOUS.
    SetThreadExecutionState(
        enable
            ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)
            : ES_CONTINUOUS);
}

inline void knst_window::csd_handle_hit(csd_hit /*hit*/, int /*x*/, int /*y*/, uint32_t /*input_serial*/) noexcept {
    // On Windows WM_NCHITTEST already returns HT* codes. CSD hit-test
    // translates directly to a native HT* code — nothing to do here.
}

#endif