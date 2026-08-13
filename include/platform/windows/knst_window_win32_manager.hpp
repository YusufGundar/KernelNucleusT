#ifndef KNST_WINDOW_WIN32_MANAGER_HPP
#define KNST_WINDOW_WIN32_MANAGER_HPP
#pragma once

#if KNST_USING_PLATFORM_WINDOWS

    #include <ole2.h>
    #include <shlobj.h>




class knst_drop_target : public IDropTarget { // for drag drop system
private:
    knst_window* m_window;
    LONG m_ref_count = 1;
    bool m_is_hovering = false;

    void set_event(int type) {
        if (!m_window) return;
        
        if (m_window->m_knst_event.type == type) return;
        m_window->m_knst_event.type = type;
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
            m_window->m_knst_event.drop_files.clear();

            POINT client_pt = { pt.x, pt.y };
            ScreenToClient(m_window->m_window, &client_pt);
            m_window->m_knst_event.mouse_x = client_pt.x;
            m_window->m_knst_event.mouse_y = client_pt.y;

            set_event(KNST_FILE_DROP_ENTER);
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
        m_window->m_knst_event.mouse_x = client_pt.x;
        m_window->m_knst_event.mouse_y = client_pt.y;

        set_event(KNST_FILE_DROP_MOVE);
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    STDMETHODIMP DragLeave() override {
        if (!m_is_hovering) return S_OK;

        m_is_hovering = false;

        if (m_window) {
            set_event(KNST_FILE_DROP_LEAVE);
            m_window->m_knst_event.drop_files.clear();
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
            m_window->m_knst_event.drop_files.clear();

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
                        m_window->m_knst_event.drop_files.push_back(knst_c16string(buffer));
                        delete[] buffer;
                    }
                    GlobalUnlock(stg.hGlobal);
                }
                ReleaseStgMedium(&stg);
            }

            POINT client_pt = { pt.x, pt.y };
            ScreenToClient(m_window->m_window, &client_pt);
            m_window->m_knst_event.mouse_x = client_pt.x;
            m_window->m_knst_event.mouse_y = client_pt.y;

            set_event(KNST_FILE_DROP);
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

    #ifdef KNST_DISABLE_TITLE_BAR

        m_window = CreateWindowExW(
            0,
            L"KnstWindowClass",
            reinterpret_cast<const wchar_t*>(m_title.data()),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            m_knst_event.window_width,
            m_knst_event.window_height,
            nullptr,
            nullptr,
            KnstWindowSources::m_hInstance,
            this
        );
        m_draw_custom_title_bar = true;

    #else

        m_window = CreateWindowExW(
            0,
            L"KnstWindowClass",
            reinterpret_cast<const wchar_t*>(m_title.data()),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            m_knst_event.window_width,
            m_knst_event.window_height,
            nullptr,
            nullptr,
            KnstWindowSources::m_hInstance,
            this
        );

    #endif

    if (m_window) {
        SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        AddClipboardFormatListener(m_window);

        #ifdef KNST_DISABLE_TITLE_BAR
            SetWindowPos(m_window, nullptr, 0, 0, 0, 0,SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        #endif

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
    if (m_window) {
        RemoveClipboardFormatListener(m_window);
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

    SetWindowPos(m_window, nullptr,root_x, root_y,0, 0,SWP_NOSIZE | SWP_NOZORDER);

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
        m_knst_event.is_maximized = false;
        m_knst_event.is_minimized = false;
        
    } else if (!fullscreen && m_knst_event.is_full_screen) {
       
        SetWindowLongW(m_window, GWL_STYLE, m_prevStyle);
        
       
        SetWindowPlacement(m_window, &m_prevPlacement);
        
       
        SetWindowPos(m_window, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        
       
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
    
    m_knst_event.is_maximized = true;
    m_knst_event.is_minimized = false;
    
    ShowWindow(m_window, SW_MAXIMIZE);
  
    RECT rect;
    GetClientRect(m_window, &rect);
    m_knst_event.window_width = rect.right - rect.left;
    m_knst_event.window_height = rect.bottom - rect.top;
    
   m_redraw_callback(*this, const_cast<void*>(get_user_data()));

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
    m_knst_event.window_width = rect.right - rect.left;
    m_knst_event.window_height = rect.bottom - rect.top;
    
    m_redraw_callback(*this, const_cast<void*>(get_user_data()));

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
    
    HCURSOR hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(cursor_type));
    if (hCursor) {
        SetClassLongPtrW(m_window, GCLP_HCURSOR, (LONG_PTR)hCursor);
        SetCursor(hCursor);
    }
}

inline void knst_window::resize(int width, int height) noexcept {
if (!m_window) return;
    
   
    if (width < 0) width = m_knst_event.window_width;
    if (height < 0) height = m_knst_event.window_height;
    
    SetWindowPos(
        m_window, nullptr,
        0, 0,
        width, height,
        SWP_NOMOVE | SWP_NOZORDER
    );
    
    m_knst_event.window_width = width;
    m_knst_event.window_height = height;
}

inline void knst_window::set_cursor_mode(int mode) noexcept {
    if (!m_window) return;
    
    if (mode == KNST_CURSOR_NORMAL) {
        ShowCursor(TRUE);
        ClipCursor(nullptr);
    } else if (mode == KNST_CURSOR_HIDDEN) {
        ShowCursor(FALSE);
    } else if (mode == KNST_CURSOR_DISABLED) {
        ShowCursor(FALSE);
        
        RECT rect;
        GetWindowRect(m_window, &rect);
        ClipCursor(&rect);
        
        int cx = (rect.right - rect.left) / 2;
        int cy = (rect.bottom - rect.top) / 2;
        POINT pt = { rect.left + cx, rect.top + cy };
        ClientToScreen(m_window, &pt);
        SetCursorPos(pt.x, pt.y);
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
    
  
    if (root_x < 0) root_x = m_knst_event.window_root_x;
    if (root_y < 0) root_y = m_knst_event.window_root_y;
    
    SetCursorPos(root_x, root_y);
}

inline void knst_window::set_clipboard(const knst_c16string& text) noexcept {
    if (!OpenClipboard(m_window)) return;
    EmptyClipboard();
    
    SIZE_T size = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    
    if (hMem) {
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        memcpy(pMem, text.data(), size);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
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
}


inline void knst_window::set_drag_drop_status(bool enabled) noexcept {
    if (!m_window) return;
    m_drag_drop_enabled = enabled;
    
    if (enabled && !m_drop_target) {
        OleInitialize(nullptr);
        m_drop_target = new knst_drop_target(this);
        RegisterDragDrop(m_window, m_drop_target);
    } else if (!enabled && m_drop_target) {
        RevokeDragDrop(m_window);
        m_drop_target->Release();
        m_drop_target = nullptr;
        OleUninitialize();
    }
}
inline void knst_window::set_opacity(float opacity) noexcept {
    if (!m_window) return;
    
    m_opacity = opacity;
    SetWindowLongW(m_window, GWL_EXSTYLE, GetWindowLongW(m_window, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(m_window, 0, (BYTE)(opacity * 255.0f), LWA_ALPHA);
}

inline void knst_window::set_attribute(int attribute, bool value) noexcept {
    if (!m_window) return;

    switch (attribute) {
        case KNST_WINDOW_ATTRIB_DECORATED: {
            DWORD style = GetWindowLongPtrW(m_window, GWL_STYLE);
            style = value ? WS_OVERLAPPEDWINDOW : (WS_POPUP | WS_VISIBLE);
            SetWindowLongPtrW(m_window, GWL_STYLE, style);
            SetWindowPos(m_window, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            ShowWindow(m_window, SW_SHOW);
            m_draw_custom_title_bar = !value;
            break;
        }
        case KNST_WINDOW_ATTRIB_RESIZABLE: {
            DWORD style = GetWindowLongPtrW(m_window, GWL_STYLE);
            if (value) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
            else       style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
            SetWindowLongPtrW(m_window, GWL_STYLE, style);
            SetWindowPos(m_window, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            break;
        }
        case KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP:
            SetWindowPos(m_window, value ? HWND_TOPMOST : HWND_NOTOPMOST,
                0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            break;

       
        case KNST_WINDOW_ATTRIB_TRANSPARENT: {
            if (value) {
               
                LONG_PTR exStyle = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
                SetWindowLongPtrW(m_window, GWL_EXSTYLE, 
                    exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED);
                SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA);
            } else {
               
                LONG_PTR exStyle = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
                SetWindowLongPtrW(m_window, GWL_EXSTYLE, 
                    exStyle & ~WS_EX_TRANSPARENT);
            }
            SetWindowPos(m_window, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            m_input_transparent = value;
            break;
        }
    }
}

inline bool knst_window::get_attribute(int attribute) const noexcept {
    if (!m_window) return false;

    switch (attribute) {
        case KNST_WINDOW_ATTRIB_DECORATED: {
            if (m_draw_custom_title_bar) return false;
            DWORD style = GetWindowLongPtrW(m_window, GWL_STYLE);
            return (style & WS_CAPTION) != 0;
        }
        case KNST_WINDOW_ATTRIB_RESIZABLE: {
            DWORD style = GetWindowLongPtrW(m_window, GWL_STYLE);
            return (style & WS_THICKFRAME) != 0;
        }
        case KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP:
            return (GetWindowLongPtrW(m_window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        case KNST_WINDOW_ATTRIB_TRANSPARENT:
            return m_input_transparent;
    }
    return false;
}



inline void knst_window::apply_bmp_icon(const knst_byte_string& data, int width, int height) noexcept {
    if (!m_window || data.empty() || width <= 0 || height <= 0) return;
    
   
   
    const uint8_t* pixel_data = (const uint8_t*)data.data();
    bool is_rgba = true;
    
 
    uint32_t and_mask_size = ((width + 7) / 8) * height;
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
            SendMessageW(m_window, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(m_window, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
    }
  
    else if (data.length() == (size_t)width * height * 3) {
        std::vector<uint8_t> bgra_data(width * height * 4);
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
        // RGB <----->>> BGRA (alpha=255)
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
    
 
    uint32_t and_mask_size = ((width + 7) / 8) * height;
    std::vector<uint8_t> and_mask(and_mask_size, 0x00);
    
  
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            uint8_t alpha = alpha_data[idx];
            
          
            if (alpha < 128) {
                int bit_idx = y * width + x;
                and_mask[bit_idx / 8] |= (1 << (bit_idx % 8));
            }
        }
    }
    

    if (hot_x < 0 || hot_x >= width) hot_x = width / 2;
    if (hot_y < 0 || hot_y >= height) hot_y = height / 2;
    
 
    HCURSOR hCursor = nullptr;
    
  
    HBITMAP hbmMask = CreateBitmap(width, height, 1, 1, and_mask.data());
    HBITMAP hbmColor = CreateBitmap(width, height, 1, 32, bgra_data.data());
    
    if (hbmMask && hbmColor) {
        ICONINFO iconInfo = {};
        iconInfo.fIcon = FALSE;          
        iconInfo.xHotspot = hot_x;       
        iconInfo.yHotspot = hot_y;        
        iconInfo.hbmMask = hbmMask;
        iconInfo.hbmColor = hbmColor;
        
        hCursor = CreateIconIndirect(&iconInfo);
    }
    
    if (hbmMask) DeleteObject(hbmMask);
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
    
 
    if (m_cursor) {
        DestroyCursor(m_cursor);
        m_cursor = nullptr;
    }
    
    m_cursor = hCursor;
    SetClassLongPtrW(m_window, GCLP_HCURSOR, (LONG_PTR)hCursor);
    SetCursor(hCursor);
    
}


inline void knst_window::reset_cursor() noexcept {
    
    if (!m_window) return;
    
   
    HCURSOR hDefault = LoadCursor(nullptr, IDC_ARROW);
    SetClassLongPtrW(m_window, GCLP_HCURSOR, (LONG_PTR)hDefault);
    SetCursor(hDefault);
    
    if (m_cursor) {
        DestroyCursor(m_cursor);
        m_cursor = nullptr;
    }
   



}





inline void knst_window::set_minimum_size(int width, int height) noexcept {
    if (!m_window) return;
    
    if (width == KNST_DEFAULT) {
        width = 300; 
    }
    if (height == KNST_DEFAULT) {
        height = 250;
    }
    
  
    LONG_PTR style = GetWindowLongPtrW(m_window, GWL_STYLE);
    
    if (!(style & WS_THICKFRAME)) {
        style |= WS_THICKFRAME;
        SetWindowLongPtrW(m_window, GWL_STYLE, style);
        SetWindowPos(m_window, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    
    MINMAXINFO mmi = {};
    mmi.ptMinTrackSize.x = width;
    mmi.ptMinTrackSize.y = height;
    
    
    SetPropW(m_window, L"KnstMinSize", (HANDLE)new POINT{width, height});
    
    RECT rect;
    GetWindowRect(m_window, &rect);
    int currentWidth = rect.right - rect.left;
    int currentHeight = rect.bottom - rect.top;
    
    if (currentWidth < width || currentHeight < height) {
        int newWidth = std::max(currentWidth, width);
        int newHeight = std::max(currentHeight, height);
        SetWindowPos(m_window, nullptr, 0, 0, newWidth, newHeight,
            SWP_NOMOVE | SWP_NOZORDER);
    }
}

inline void knst_window::set_maximum_size(int width, int height) noexcept {
    if (!m_window) return;
    
    if (width == KNST_DEFAULT && height == KNST_DEFAULT) {
        return;
    }
    
    LONG_PTR style = GetWindowLongPtrW(m_window, GWL_STYLE);
    
    
    if (!(style & WS_THICKFRAME)) {
        style |= WS_THICKFRAME;
        SetWindowLongPtrW(m_window, GWL_STYLE, style);
        SetWindowPos(m_window, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    
    if (width != KNST_DEFAULT && height != KNST_DEFAULT) {
        SetPropW(m_window, L"KnstMaxSize", (HANDLE)new POINT{width, height});
    } else if (width != KNST_DEFAULT) {
        SetPropW(m_window, L"KnstMaxSizeWidth", (HANDLE)new int{width});
        RemovePropW(m_window, L"KnstMaxSizeHeight");
    } else if (height != KNST_DEFAULT) {
        SetPropW(m_window, L"KnstMaxSizeHeight", (HANDLE)new int{height});
        RemovePropW(m_window, L"KnstMaxSizeWidth");
    }
}




#endif
#endif // KNST_WINDOW_WIN32_MANAGER_HPP