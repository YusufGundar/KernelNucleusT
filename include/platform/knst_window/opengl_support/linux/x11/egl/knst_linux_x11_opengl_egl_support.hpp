#ifndef KNST_LINUX_X11_OPENGL_EGL_SUPPORT_HPP
#define KNST_LINUX_X11_OPENGL_EGL_SUPPORT_HPP
#pragma once

#if KNST_USING_LINUX_PLATFORM_X11 && defined(KNST_USING_OPENGL) && defined(KNST_OPENGL_USING_EGL)

inline bool knst_window_opengl_content::Init(knst_window* window_p, bool vsync_status) {
    if (m_initialized) {
        return true;
    }

    if (!window_p) {
        return false;
    }
    window = window_p;

    m_connection = KnstWindowSources::get_native_x11_connection_handle();
    if (!m_connection) {
        return false;
    }

    m_window = window->get_x11_window_handle();
    if (!m_window) {
        return false;
    }

    if (s_egl_display == EGL_NO_DISPLAY) {
        s_egl_display = eglGetDisplay((EGLNativeDisplayType)m_connection);
        
        if (s_egl_display == EGL_NO_DISPLAY) {
            s_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        }

        if (s_egl_display == EGL_NO_DISPLAY) {
            return false;
        }

        EGLint major, minor;
        if (!eglInitialize(s_egl_display, &major, &minor)) {
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
        if (!eglChooseConfig(s_egl_display, configAttribs, &m_egl_config, 1, &numConfigs) || numConfigs == 0) {
            return false;
        }
    }

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
            return false;
        }
    }

    if (m_egl_surface == EGL_NO_SURFACE) {
        EGLNativeWindowType nativeWindow = (EGLNativeWindowType)m_window;
        
        m_egl_surface = eglCreateWindowSurface(s_egl_display, m_egl_config, nativeWindow, nullptr);
        if (m_egl_surface == EGL_NO_SURFACE) {
            eglDestroyContext(s_egl_display, m_egl_context);
            m_egl_context = EGL_NO_CONTEXT;
            return false;
        }
    }

    if (!eglMakeCurrent(s_egl_display, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroySurface(s_egl_display, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        eglDestroyContext(s_egl_display, m_egl_context);
        m_egl_context = EGL_NO_CONTEXT;
        return false;
    }

    eglSwapInterval(s_egl_display, vsync_status ? 1 : 0);
    m_vsync_enabled = vsync_status;

    s_gl_functions_loaded = true;
    m_initialized = true;
    s_initialized = true;
    s_ref_count++;

    return true;
}

#endif 
#endif // KNST_LINUX_X11_OPENGL_EGL_SUPPORT_HPP