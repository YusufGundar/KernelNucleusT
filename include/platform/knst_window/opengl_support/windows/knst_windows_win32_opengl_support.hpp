#ifndef KNST_WINDOWS_WIN32_OPENGL_SUPPORT_HPP
#define KNST_WINDOWS_WIN32_OPENGL_SUPPORT_HPP
#pragma once

#if KNST_USING_PLATFORM_WINDOWS && defined(KNST_USING_OPENGL)

inline bool knst_window_opengl_content::Init(knst_window* window_p, bool vsync_status) {
    if (m_initialized) {
        return true;
    }

    window = window_p;

    m_hwnd = window->get_windows_window_handle();
    if (!m_hwnd) {
        return false;
    }

    m_hdc = GetDC(m_hwnd);
    if (!m_hdc) {
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(m_hdc, &pfd);
    if (!pixelFormat) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    if (!SetPixelFormat(m_hdc, pixelFormat, &pfd)) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    HGLRC tempContext = wglCreateContext(m_hdc);
    if (!tempContext) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    if (!wglMakeCurrent(m_hdc, tempContext)) {
        wglDeleteContext(tempContext);
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    typedef BOOL(WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
    typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);

    PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB =
        (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    bool use_arb = false;
    if (wglChoosePixelFormatARB && wglCreateContextAttribsARB) {
        use_arb = true;
        
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(tempContext);
        ReleaseDC(m_hwnd, m_hdc);

        m_hdc = GetDC(m_hwnd);

        const int pixelAttribs[] = {
            0x2001, 1,       // WGL_DRAW_TO_WINDOW_ARB
            0x2002, 1,       // WGL_SUPPORT_OPENGL_ARB
            0x2010, 1,       // WGL_DOUBLE_BUFFER_ARB
            0x2011, 32,      // WGL_PIXEL_TYPE_ARB = RGBA
            0x2013, 32,      // WGL_COLOR_BITS_ARB
            0x2022, KNST_OPENGL_DEPTH_SIZE,    // WGL_DEPTH_BITS_ARB
            0x2023, KNST_OPENGL_STENCIL_SIZE,  // WGL_STENCIL_BITS_ARB
            0x2041, KNST_OPENGL_SAMPLES > 0 ? 1 : 0,  // WGL_SAMPLE_BUFFERS_ARB
            0x2042, KNST_OPENGL_SAMPLES,       // WGL_SAMPLES_ARB
            0, 0
        };

        UINT numFormats;
        int pixelFormatARB;
        if (!wglChoosePixelFormatARB(m_hdc, pixelAttribs, NULL, 1, &pixelFormatARB, &numFormats)) {
            use_arb = false;
        } else {
            SetPixelFormat(m_hdc, pixelFormatARB, &pfd);

            int contextAttribs[] = {
                0x2091, KNST_OPENGL_MAJOR,      // WGL_CONTEXT_MAJOR_VERSION_ARB
                0x2092, KNST_OPENGL_MINOR,      // WGL_CONTEXT_MINOR_VERSION_ARB
                0x2094, 0,                      // WGL_CONTEXT_FLAGS_ARB
                0x9126, 1,                      // WGL_CONTEXT_PROFILE_MASK_ARB = CORE
                0, 0
            };

            m_hglrc = wglCreateContextAttribsARB(m_hdc, NULL, contextAttribs);
        }
    }

    if (!m_hglrc) {
        if (use_arb) {
            tempContext = wglCreateContext(m_hdc);
            m_hglrc = tempContext;
        } else {
            m_hglrc = tempContext;
        }
    } else {
        if (tempContext) {
            wglDeleteContext(tempContext);
        }
    }

    if (!m_hglrc) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
        m_hwnd = nullptr;
        return false;
    }

    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        wglDeleteContext(m_hglrc);
        ReleaseDC(m_hwnd, m_hdc);
        m_hglrc = nullptr;
        m_hdc = nullptr;
        m_hwnd = nullptr;
        return false;
    }

    if (wglSwapIntervalEXT == nullptr) {
        wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    }
    
    if (wglSwapIntervalEXT) {
        wglSwapIntervalEXT(vsync_status ? 1 : 0);
    }

  

    m_vsync_enabled = vsync_status;
    s_gl_functions_loaded = true;
    m_initialized = true;
    s_initialized = true;
    s_ref_count++;

    return true;
}

#endif
#endif // KNST_WINDOWS_WIN32_OPENGL_SUPPORT_HPP