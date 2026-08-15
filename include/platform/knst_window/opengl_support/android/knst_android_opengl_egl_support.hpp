#ifndef KNST_ANDROID_OPENGL_EGL_SUPPORT_HPP
#define KNST_ANDROID_OPENGL_EGL_SUPPORT_HPP
#pragma once

#if defined(KNST_USING_PLATFORM_ANDROID) && defined(KNST_PLATFORM_ANDROID_OPENGL)

#include <android/log.h>



inline bool knst_window_opengl_content::Init(knst_window* window_p, bool vsync) {
  
    
    if (m_initialized) {
        return true;
    }
    
    if (!window_p) {
        return false;
    }
    
   
    struct android_app* app = KnstWindowSources::m_app;
    
    if (!app) {
        return false;
    }
    
    if (app->window == nullptr) {
        return false;
    }

    
    window = window_p;
    m_vsync_enabled = vsync;

    
   
    if (s_egl_display == EGL_NO_DISPLAY) {
        s_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s_egl_display == EGL_NO_DISPLAY) {
            return false;
        }

        EGLint major, minor;
        if (!eglInitialize(s_egl_display, &major, &minor)) {
            s_egl_display = EGL_NO_DISPLAY;
            return false;
        }
       
    }

    
    const EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, KNST_OPENGL_DEPTH_SIZE,
        EGL_STENCIL_SIZE, KNST_OPENGL_STENCIL_SIZE,
        EGL_NONE
    };

    EGLint num_configs;
    if (!eglChooseConfig(s_egl_display, config_attribs, &m_egl_config, 1, &num_configs) || num_configs == 0) {
        
        eglTerminate(s_egl_display);
        s_egl_display = EGL_NO_DISPLAY;
        return false;
    }
  

   
    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    m_egl_context = eglCreateContext(s_egl_display, m_egl_config, EGL_NO_CONTEXT, context_attribs);
    if (m_egl_context == EGL_NO_CONTEXT) {
        
        return false;
    }
   


    
    m_egl_surface = eglCreateWindowSurface(
        s_egl_display, 
        m_egl_config, 
        (EGLNativeWindowType)app->window,
        nullptr
    );
    
    if (m_egl_surface == EGL_NO_SURFACE) {
        
        eglDestroyContext(s_egl_display, m_egl_context);
        m_egl_context = EGL_NO_CONTEXT;
        eglTerminate(s_egl_display);
        s_egl_display = EGL_NO_DISPLAY;
        return false;
    }
    

   
    if (!eglMakeCurrent(s_egl_display, m_egl_surface, m_egl_surface, m_egl_context)) {
       
        eglDestroySurface(s_egl_display, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        eglDestroyContext(s_egl_display, m_egl_context);
        m_egl_context = EGL_NO_CONTEXT;
        eglTerminate(s_egl_display);
        s_egl_display = EGL_NO_DISPLAY;
        return false;
    }
  

  
    eglSwapInterval(s_egl_display, vsync ? 1 : 0);


   
    m_last_width = ANativeWindow_getWidth(app->window);
    m_last_height = ANativeWindow_getHeight(app->window);
    
    window->m_knst_event.window_width = m_last_width;
    window->m_knst_event.window_height = m_last_height;


   
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    if (extensions) {
        s_extensions.clear();
        std::string ext_str(extensions);
        size_t pos = 0;
        while (pos < ext_str.length()) {
            size_t space = ext_str.find(' ', pos);
            if (space == std::string::npos) {
                s_extensions.push_back(knst_byte_string(ext_str.substr(pos).c_str()));
                break;
            }
            s_extensions.push_back(knst_byte_string(ext_str.substr(pos, space - pos).c_str()));
            pos = space + 1;
        }

    }

    s_gl_functions_loaded = true;
    m_initialized = true;
    s_initialized = true;
    s_ref_count++;

   

    return true;
}

#endif 
#endif // KNST_ANDROID_OPENGL_EGL_SUPPORT_HPP