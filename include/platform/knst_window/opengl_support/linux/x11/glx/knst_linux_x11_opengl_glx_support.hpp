#ifndef KNST_LINUX_X11_OPENGL_GLX_SUPPORT_HPP
#define KNST_LINUX_X11_OPENGL_GLX_SUPPORT_HPP
#pragma once

#if KNST_USING_LINUX_PLATFORM_X11 && defined(KNST_USING_OPENGL) && defined(KNST_OPENGL_USING_GLX)

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

    if (s_glx_display == nullptr) {
        s_glx_display = KnstWindowSources::get_native_x11_display();
        if (!s_glx_display) {
            return false;
        }
    }

    m_window = window->get_x11_window_handle();
    if (!m_window) {
        return false;
    }

   
    xcb_get_window_attributes_cookie_t attr_cookie = 
        xcb_get_window_attributes(m_connection, m_window);
    xcb_get_window_attributes_reply_t* attr_reply = 
        xcb_get_window_attributes_reply(m_connection, attr_cookie, nullptr);
    
    if (!attr_reply) {
        return false;
    }
    
    xcb_visualid_t window_visual_id = attr_reply->visual;
    free(attr_reply);
    
    
    XVisualInfo template_vi;
    template_vi.visualid = window_visual_id;
    template_vi.screen = DefaultScreen(s_glx_display);
    
    int num_visuals;
    XVisualInfo* visual_info = XGetVisualInfo(s_glx_display, 
                                               VisualIDMask | VisualScreenMask, 
                                               &template_vi, &num_visuals);
    
    if (!visual_info || num_visuals == 0) {
        return false;
    }

    
    int fbCount = 0;
    GLXFBConfig* fbConfigs = glXGetFBConfigs(s_glx_display, DefaultScreen(s_glx_display), &fbCount);

    if (!fbConfigs || fbCount == 0) {
        XFree(visual_info);
        return false;
    }

    
    m_glx_fbConfig = nullptr;
    for (int i = 0; i < fbCount; i++) {
        XVisualInfo* vi = glXGetVisualFromFBConfig(s_glx_display, fbConfigs[i]);
        if (vi) {
            if (vi->visualid == window_visual_id) {
                m_glx_fbConfig = fbConfigs[i];
                XFree(vi);
                break;
            }
            XFree(vi);
        }
    }

    if (!m_glx_fbConfig) {
        XFree(visual_info);
        XFree(fbConfigs);
        return false;
    }
    
    XFree(visual_info);

    
    typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    glXCreateContextAttribsARBProc glXCreateContextAttribsARB =
        (glXCreateContextAttribsARBProc)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");

    if (glXCreateContextAttribsARB) {
        int contextAttribs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, KNST_OPENGL_MAJOR,
            GLX_CONTEXT_MINOR_VERSION_ARB, KNST_OPENGL_MINOR,
            GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
            None
        };
        m_glx_context = glXCreateContextAttribsARB(s_glx_display, m_glx_fbConfig, nullptr, True, contextAttribs);
    }

    if (!m_glx_context) {
        m_glx_context = glXCreateNewContext(s_glx_display, m_glx_fbConfig, GLX_RGBA_TYPE, nullptr, True);
    }

    if (!m_glx_context) {
        XFree(fbConfigs);
        return false;
    }
    XFree(fbConfigs);

    m_glx_drawable = (GLXDrawable)m_window;

    if (!glXMakeCurrent(s_glx_display, m_glx_drawable, m_glx_context)) {
        glXDestroyContext(s_glx_display, m_glx_context);
        m_glx_context = nullptr;
        return false;
    }

   
    typedef void (*glXSwapIntervalEXTProc)(Display*, GLXDrawable, int);
    glXSwapIntervalEXTProc glXSwapIntervalEXT =
        (glXSwapIntervalEXTProc)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
    if (glXSwapIntervalEXT) {
        glXSwapIntervalEXT(s_glx_display, m_glx_drawable, vsync_status ? 1 : 0);
    }
    m_vsync_enabled = vsync_status;

    s_gl_functions_loaded = true;
    m_initialized = true;
    s_initialized = true;
    s_ref_count++;

    return true;
}

#endif
#endif // KNST_LINUX_X11_OPENGL_GLX_SUPPORT_HPP