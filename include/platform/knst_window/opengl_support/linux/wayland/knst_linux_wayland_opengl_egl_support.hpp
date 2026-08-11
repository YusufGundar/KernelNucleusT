#ifndef KNST_LINUX_WAYLAND_OPENGL_EGL_SUPPORT_HPP
#define KNST_LINUX_WAYLAND_OPENGL_EGL_SUPPORT_HPP
#pragma once

#if KNST_USING_LINUX_PLATFORM_WAYLAND && defined(KNST_USING_OPENGL)

inline bool knst_window_opengl_content::Init(knst_window* window_p, bool vsync_status) {
    if (m_initialized) {
        return true;
    }

    window = window_p;

    struct wl_display* display = KnstWindowSources::wayland_display;
    struct wl_surface* surface = window->m_surface;

    if (!display || !surface) {
        return false;
    }

    int width = window->get_window_event_handle().window_width;
    int height = window->get_window_event_handle().window_height;
    
    m_wl_egl_window = wl_egl_window_create(surface, width, height);
    
    if (!m_wl_egl_window) {
        return false;
    }

    if (s_egl_display == EGL_NO_DISPLAY) {
        s_egl_display = eglGetDisplay((EGLNativeDisplayType)display);
        
        if (s_egl_display == EGL_NO_DISPLAY) {
            s_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        }

        if (s_egl_display == EGL_NO_DISPLAY) {
            wl_egl_window_destroy(m_wl_egl_window);
            m_wl_egl_window = nullptr;
            return false;
        }

        EGLint major, minor;
        if (!eglInitialize(s_egl_display, &major, &minor)) {
            wl_egl_window_destroy(m_wl_egl_window);
            m_wl_egl_window = nullptr;
            s_egl_display = EGL_NO_DISPLAY;
            return false;
        }
    }

    if (m_egl_config == nullptr) {
        EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, KNST_OPENGL_DEPTH_SIZE,
            EGL_STENCIL_SIZE, KNST_OPENGL_STENCIL_SIZE,
            EGL_SAMPLE_BUFFERS, KNST_OPENGL_SAMPLES > 0 ? 1 : 0,
            EGL_SAMPLES, KNST_OPENGL_SAMPLES,
            EGL_NONE
        };

        EGLint numConfigs;
        EGLConfig configs[20];
        if (!eglChooseConfig(s_egl_display, configAttribs, configs, 20, &numConfigs) || numConfigs == 0) {
            wl_egl_window_destroy(m_wl_egl_window);
            m_wl_egl_window = nullptr;
            return false;
        }

        bool found = false;
        for (int i = 0; i < numConfigs; i++) {
            EGLint r, g, b, a;
            eglGetConfigAttrib(s_egl_display, configs[i], EGL_RED_SIZE, &r);
            eglGetConfigAttrib(s_egl_display, configs[i], EGL_GREEN_SIZE, &g);
            eglGetConfigAttrib(s_egl_display, configs[i], EGL_BLUE_SIZE, &b);
            eglGetConfigAttrib(s_egl_display, configs[i], EGL_ALPHA_SIZE, &a);
            
            if (a >= 8) {
                m_egl_config = configs[i];
                found = true;
                break;
            }
        }
        
        if (!found) {
            m_egl_config = configs[0];
        }
    }

    eglBindAPI(EGL_OPENGL_API);
    
    if (m_egl_context == EGL_NO_CONTEXT) {
        EGLint contextAttribs[] = {
            EGL_CONTEXT_MAJOR_VERSION, KNST_OPENGL_MAJOR,
            EGL_CONTEXT_MINOR_VERSION, KNST_OPENGL_MINOR,
            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
            EGL_NONE
        };

        m_egl_context = eglCreateContext(s_egl_display, m_egl_config, EGL_NO_CONTEXT, contextAttribs);
        
        if (m_egl_context == EGL_NO_CONTEXT) {
            EGLint fallbackAttribs[] = {
                EGL_CONTEXT_MAJOR_VERSION, 3,
                EGL_CONTEXT_MINOR_VERSION, 0,
                EGL_NONE
            };
            m_egl_context = eglCreateContext(s_egl_display, m_egl_config, EGL_NO_CONTEXT, fallbackAttribs);
        }

        if (m_egl_context == EGL_NO_CONTEXT) {
            wl_egl_window_destroy(m_wl_egl_window);
            m_wl_egl_window = nullptr;
            return false;
        }
    }

    if (m_egl_surface == EGL_NO_SURFACE) {
        m_egl_surface = eglCreateWindowSurface(s_egl_display, m_egl_config, (EGLNativeWindowType)m_wl_egl_window, nullptr);
                                                
        if (m_egl_surface == EGL_NO_SURFACE) {
            eglDestroyContext(s_egl_display, m_egl_context);
            m_egl_context = EGL_NO_CONTEXT;
            wl_egl_window_destroy(m_wl_egl_window);
            m_wl_egl_window = nullptr;
            return false;
        }
    }

    if (!eglMakeCurrent(s_egl_display, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroySurface(s_egl_display, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        eglDestroyContext(s_egl_display, m_egl_context);
        m_egl_context = EGL_NO_CONTEXT;
        wl_egl_window_destroy(m_wl_egl_window);
        m_wl_egl_window = nullptr;
        return false;
    }
    
    eglSwapInterval(s_egl_display, vsync_status ? 1 : 0);
    m_vsync_enabled = vsync_status;

    m_initialized = true;
    s_initialized = true;
    s_ref_count++;

    return true;
}

#endif
#endif //KNST_LINUX_WAYLAND_OPENGL_EGL_SUPPORT_HPP