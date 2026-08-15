#ifndef KNST_WINDOW_OPENGL_MANAGER_HPP
#define KNST_WINDOW_OPENGL_MANAGER_HPP
#pragma once

#ifdef KNST_USING_OPENGL

#ifndef KNST_OPENGL_MAJOR
    #define KNST_OPENGL_MAJOR 3
#endif
#ifndef KNST_OPENGL_MINOR
    #define KNST_OPENGL_MINOR 3
#endif
#ifndef KNST_OPENGL_DEPTH_SIZE
    #define KNST_OPENGL_DEPTH_SIZE 24
#endif
#ifndef KNST_OPENGL_STENCIL_SIZE
    #define KNST_OPENGL_STENCIL_SIZE 8
#endif
#ifndef KNST_OPENGL_SAMPLES
    #define KNST_OPENGL_SAMPLES 0
#endif

#if KNST_USING_PLATFORM_WINDOWS
    #include <GL/gl.h>
    typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int);
#elif KNST_USING_LINUX_PLATFORM_X11
    #ifdef KNST_OPENGL_USING_EGL
        #include <EGL/egl.h>
    #elif defined(KNST_OPENGL_USING_GLX)
        #include <GL/glx.h>
    #endif
    #include <GL/gl.h>
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    #include <EGL/egl.h>
    #include <wayland-egl.h>
    #include <GL/gl.h>
    
#elif defined(KNST_USING_PLATFORM_ANDROID)
    #ifdef KNST_PLATFORM_ANDROID_OPENGL
        #include <EGL/egl.h>
        #include <GLES3/gl3.h>
        #include <GLES2/gl2ext.h>
    #endif
#endif

class knst_window_opengl_content {
private:
    static inline bool s_initialized = false;
    static inline int s_ref_count = 0;
    static inline bool s_gl_functions_loaded = false;
    static inline knst_vector<knst_byte_string> s_extensions;

    bool m_initialized = false;
    bool m_vsync_enabled = true;

    int m_last_width  = 0;
    int m_last_height = 0;

#if KNST_USING_PLATFORM_WINDOWS
    static inline PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr;
    HGLRC m_hglrc = nullptr;
    HDC   m_hdc   = nullptr;
    HWND  m_hwnd  = nullptr;

#elif KNST_USING_LINUX_PLATFORM_X11
    #ifdef KNST_OPENGL_USING_EGL
        static inline EGLDisplay s_egl_display = EGL_NO_DISPLAY;
        EGLContext m_egl_context = EGL_NO_CONTEXT;
        EGLSurface m_egl_surface = EGL_NO_SURFACE;
        EGLConfig  m_egl_config  = nullptr;
    #endif
    #ifdef KNST_OPENGL_USING_GLX
        static inline Display* s_glx_display = nullptr;
        GLXContext  m_glx_context = nullptr;
        GLXDrawable m_glx_drawable = 0;
        GLXFBConfig m_glx_fbConfig = nullptr;
    #endif
    xcb_window_t m_window = 0;
    xcb_connection_t* m_connection = nullptr;

#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    static inline EGLDisplay s_egl_display = EGL_NO_DISPLAY;
    EGLContext m_egl_context = EGL_NO_CONTEXT;
    EGLSurface m_egl_surface = EGL_NO_SURFACE;
    EGLConfig  m_egl_config  = nullptr;
    wl_egl_window* m_wl_egl_window = nullptr;

#elif defined(KNST_USING_PLATFORM_ANDROID)
    #ifdef KNST_PLATFORM_ANDROID_OPENGL
        static inline EGLDisplay s_egl_display = EGL_NO_DISPLAY;
        EGLContext m_egl_context = EGL_NO_CONTEXT;
        EGLSurface m_egl_surface = EGL_NO_SURFACE;
        EGLConfig  m_egl_config  = nullptr;
    #endif
#endif



    knst_window* window = nullptr;

public:

    bool Init(knst_window* window_p , bool vsync = false);

   
    
    void Shutdown() {
        if (!m_initialized) return;

        #if KNST_USING_PLATFORM_WINDOWS
            if (m_hglrc) {
                if (wglGetCurrentContext() == m_hglrc) wglMakeCurrent(NULL, NULL);
                wglDeleteContext(m_hglrc);
                m_hglrc = nullptr;
            }
            if (m_hdc && m_hwnd) { ReleaseDC(m_hwnd, m_hdc); m_hdc = nullptr; }
            m_hwnd = nullptr;

        #elif KNST_USING_LINUX_PLATFORM_X11
            #ifdef KNST_OPENGL_USING_EGL
                if (m_egl_surface != EGL_NO_SURFACE) {
                    eglDestroySurface(s_egl_display, m_egl_surface);
                    m_egl_surface = EGL_NO_SURFACE;
                }
                if (m_egl_context != EGL_NO_CONTEXT) {
                    if (eglGetCurrentContext() == m_egl_context)
                        eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                    eglDestroyContext(s_egl_display, m_egl_context);
                    m_egl_context = EGL_NO_CONTEXT;
                }
                m_egl_config = nullptr;
                m_window = 0;
                m_connection = nullptr;
            #elif defined(KNST_OPENGL_USING_GLX)
                if (m_glx_context) {
                    if (glXGetCurrentContext() == m_glx_context)
                        glXMakeCurrent(s_glx_display, None, nullptr);
                    glXDestroyContext(s_glx_display, m_glx_context);
                    m_glx_context = nullptr;
                }
                m_glx_drawable = 0;
                m_glx_fbConfig = nullptr;
                m_window = 0;
                m_connection = nullptr;
            #endif

        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            if (m_egl_surface != EGL_NO_SURFACE) {
                eglDestroySurface(s_egl_display, m_egl_surface);
                m_egl_surface = EGL_NO_SURFACE;
            }
            if (m_wl_egl_window) {
                wl_egl_window_destroy(m_wl_egl_window);
                m_wl_egl_window = nullptr;
            }
            if (m_egl_context != EGL_NO_CONTEXT) {
                if (eglGetCurrentContext() == m_egl_context)
                    eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                eglDestroyContext(s_egl_display, m_egl_context);
                m_egl_context = EGL_NO_CONTEXT;
            }
            m_egl_config = nullptr;
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            #ifdef KNST_PLATFORM_ANDROID_OPENGL
                if (m_egl_surface != EGL_NO_SURFACE) {
                    eglDestroySurface(s_egl_display, m_egl_surface);
                    m_egl_surface = EGL_NO_SURFACE;
                }
                if (m_egl_context != EGL_NO_CONTEXT) {
                    if (eglGetCurrentContext() == m_egl_context)
                        eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                    eglDestroyContext(s_egl_display, m_egl_context);
                    m_egl_context = EGL_NO_CONTEXT;
                }
                m_egl_config = nullptr;
                
            #endif
        #endif

        m_initialized = false;
        s_ref_count--;

        if (s_ref_count <= 0) {
            #if KNST_USING_LINUX_PLATFORM_X11 && defined(KNST_OPENGL_USING_EGL)
                if (s_egl_display != EGL_NO_DISPLAY) {
                    eglTerminate(s_egl_display);
                    s_egl_display = EGL_NO_DISPLAY;
                }
            #elif KNST_USING_LINUX_PLATFORM_WAYLAND
                if (s_egl_display != EGL_NO_DISPLAY) {
                    eglTerminate(s_egl_display);
                    s_egl_display = EGL_NO_DISPLAY;
                }
            #elif defined(KNST_USING_PLATFORM_ANDROID) && defined(KNST_PLATFORM_ANDROID_OPENGL)
                if (s_egl_display != EGL_NO_DISPLAY) {
                    eglTerminate(s_egl_display);
                    s_egl_display = EGL_NO_DISPLAY;
                }
            #elif KNST_USING_LINUX_PLATFORM_X11 && defined(KNST_OPENGL_USING_GLX)
                s_glx_display = nullptr;
            #endif

            s_initialized = false;
            s_ref_count = 0;
            s_gl_functions_loaded = false;
            s_extensions.clear();
        }
    }
    
    KNST_FORCE_INLINE void SwapBuffers() {



            GLint old_viewport[4];
            GLint old_scissor[4];
            GLfloat old_clear_color[4];
            GLboolean scissor_was_enabled = glIsEnabled(GL_SCISSOR_TEST);

            glGetIntegerv(GL_VIEWPORT, old_viewport);
            glGetIntegerv(GL_SCISSOR_BOX, old_scissor);
            glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear_color);

            
            int w = window->get_window_event_handle().window_width;
            int h = window->get_window_event_handle().window_height;
            glViewport(0, 0, w, h);

            #if !defined(KNST_USING_PLATFORM_ANDROID)

                #ifdef KNST_WINDOW_USING_KNST_TITLE_BAR_BLUE_MODERN
                    DrawKnstTitleBarBlueModern();
                #elif defined(KNST_WINDOW_USING_KNST_TITLE_BAR_WHITE_MODERN)
                    DrawKnstTitleBarWhiteModern();
                #elif defined(KNST_WINDOW_USING_KNST_TITLE_BAR_FUTURISTIC)
                    DrawKnstTitleBarFuturistic();
                #elif defined(KNST_WINDOW_USING_KNST_TITLE_BAR_SUNSET_GLOW)
                    DrawKnstTitleBarSunsetGlow();
                #elif KNST_USING_LINUX_PLATFORM_WAYLAND && !defined(KNST_DISABLE_TITLE_BAR)
                    DrawKnstTitleBarWhiteModern();
                #endif

            #endif
            

        
            glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
            glScissor(old_scissor[0], old_scissor[1], old_scissor[2], old_scissor[3]);
            glClearColor(old_clear_color[0], old_clear_color[1], old_clear_color[2], old_clear_color[3]);

            if (scissor_was_enabled) {
                glEnable(GL_SCISSOR_TEST);
            } else {
                glDisable(GL_SCISSOR_TEST);
            }


            #if KNST_USING_PLATFORM_WINDOWS
                ::SwapBuffers(m_hdc);
            #elif KNST_USING_LINUX_PLATFORM_X11
                #ifdef KNST_OPENGL_USING_EGL
                    eglSwapBuffers(s_egl_display, m_egl_surface);
                #elif defined(KNST_OPENGL_USING_GLX)
                    glXSwapBuffers(s_glx_display, m_glx_drawable);
                #endif
            #elif KNST_USING_LINUX_PLATFORM_WAYLAND
                eglSwapBuffers(s_egl_display, m_egl_surface);
            #elif defined(KNST_USING_PLATFORM_ANDROID)
                eglSwapBuffers(s_egl_display, m_egl_surface);
            #endif
        
        

    
        
    }



KNST_FORCE_INLINE void BeginFrame() {
    #ifndef KNST_USING_PLATFORM_ANDROID
    if (window->get_window_event_handle().window_width != m_last_width || 
        window->get_window_event_handle().window_height != m_last_height) {
        
        m_last_width = window->get_window_event_handle().window_width;
        m_last_height = window->get_window_event_handle().window_height;
        
        
            int ch = window->get_window_event_handle().window_height; 
            if (window->m_draw_custom_title_bar) {
                ch -= window->get_title_bar_height();   
            }
        
      
        glViewport(0, 0, window->get_window_event_handle().window_width, ch);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, window->get_window_event_handle().window_width, ch);

       
        #if KNST_USING_LINUX_PLATFORM_WAYLAND
            if (m_wl_egl_window) {
                wl_egl_window_resize(
                    m_wl_egl_window, 
                    m_last_width, 
                    m_last_height, 
                    0, 0
                );
            }
        #endif
    }
    #endif
}
#ifndef KNST_USING_PLATFORM_ANDROID

    KNST_FORCE_INLINE void DrawKnstTitleBarBlueModern()
    {
        constexpr int BW = 48;
        const int TH = window->get_title_bar_height();
        const int w = window->get_window_event_handle().window_width;
        const int h = window->get_window_event_handle().window_height;
        const int gy = h - TH;
        

        const int mx = (int)window->m_mouse_x;
        const int my = (int)window->m_mouse_y;
        
        const bool focused = window->get_window_event_handle().is_focused;
        
    
        const bool in_titlebar = (my >= 0 && my <= TH);
        
    
        const bool hover_close = in_titlebar && (mx >= w - BW) && (mx < w);
        const bool hover_max = in_titlebar && (mx >= w - BW * 2) && (mx < w - BW);
        const bool hover_min = in_titlebar && (mx >= w - BW * 3) && (mx < w - BW * 2);

        glEnable(GL_SCISSOR_TEST);

    
        glScissor(0, gy, w, TH);
        glClearColor(
            focused ? 0.155f : 0.135f,
            focused ? 0.160f : 0.140f,
            focused ? 0.170f : 0.150f,
            1.0f
        );
        glClear(GL_COLOR_BUFFER_BIT);

        
        glScissor(0, gy, w, 2);
        glClearColor(
            focused ? 0.24f : 0.10f,
            focused ? 0.50f : 0.20f,
            focused ? 0.78f : 0.35f,
            1.0f
        );
        glClear(GL_COLOR_BUFFER_BIT);

        
        constexpr int HP = 4;
        if (hover_close) {
            glScissor(w - BW + 1, gy + HP, BW - 2, TH - HP * 2);
            glClearColor(0.88f, 0.18f, 0.22f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (hover_max) {
            glScissor(w - BW * 2 + 1, gy + HP, BW - 2, TH - HP * 2);
            glClearColor(0.20f, 0.21f, 0.23f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (hover_min) {
            glScissor(w - BW * 3 + 1, gy + HP, BW - 2, TH - HP * 2);
            glClearColor(0.20f, 0.21f, 0.23f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

    
        glClearColor(0.28f, 0.29f, 0.30f, 0.35f);
        glScissor(w - BW, gy + 11, 1, TH - 22);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(w - BW * 2, gy + 11, 1, TH - 22);
        glClear(GL_COLOR_BUFFER_BIT);

    
        const int cy = gy + TH / 2;
        
    
        glClearColor(
            hover_min ? 1.0f : 0.84f,
            hover_min ? 0.97f : 0.85f,
            hover_min ? 0.97f : 0.88f,
            1.0f
        );
        int cx = w - BW * 5 / 2;
        glScissor(cx - 5, cy, 10, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy - 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 4, cy - 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        
        cx = w - BW * 3 / 2;
        if (!window->get_window_event_handle().is_maximized) {
            glClearColor(
                hover_max ? 1.0f : 0.84f,
                hover_max ? 0.97f : 0.85f,
                hover_max ? 0.97f : 0.88f,
                1.0f
            );
            glScissor(cx - 4, cy - 5, 8, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 5, cy - 4, 1, 8);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 4, cy - 4, 1, 8);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 4, cy + 4, 8, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 3, cy - 6, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 2, cy - 6, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 3, cy + 5, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 2, cy + 5, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            glClearColor(0.52f, 0.54f, 0.58f, 1.0f);
            glScissor(cx - 1, cy - 5, 6, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 4, cy - 4, 1, 6);
            glClear(GL_COLOR_BUFFER_BIT);
            
            glClearColor(
                hover_max ? 1.0f : 0.84f,
                hover_max ? 0.97f : 0.85f,
                hover_max ? 0.97f : 0.88f,
                1.0f
            );
            glScissor(cx - 5, cy - 2, 7, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 5, cy - 1, 1, 6);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 4, cy + 4, 6, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        
        cx = w - BW / 2;
        glClearColor(
            hover_close ? 1.0f : 0.84f,
            hover_close ? 0.97f : 0.85f,
            hover_close ? 0.97f : 0.88f,
            1.0f
        );

        
        glScissor(cx - 5, cy - 5, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy - 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 3, cy - 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 2, cy - 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 1, cy - 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx, cy, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 1, cy + 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 2, cy + 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 3, cy + 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 4, cy + 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        
        glScissor(cx + 4, cy - 5, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 3, cy - 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 2, cy - 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 1, cy - 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx, cy - 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 1, cy, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 2, cy + 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 3, cy + 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy + 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 5, cy + 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
    }


    KNST_FORCE_INLINE void DrawKnstTitleBarSunsetGlow()
    {
        constexpr int BW = 46;
        const int TH = window->get_title_bar_height();
        const int w = window->get_window_event_handle().window_width;
        const int h = window->get_window_event_handle().window_height;
        const int gy = h - TH;
        
        
        const int mx = (int)window->m_mouse_x;
        const int my = (int)window->m_mouse_y;
        
        const bool focused = window->get_window_event_handle().is_focused;
        
        
        const bool in_titlebar = (my >= 0 && my <= TH);
        
    
        const bool hover_close = in_titlebar && (mx >= w - BW) && (mx < w);
        const bool hover_max= in_titlebar && (mx >= w - BW * 2) && (mx < w - BW);
        const bool hover_min  =   in_titlebar && (mx >= w - BW * 3) && (mx < w - BW * 2);

        glEnable(GL_SCISSOR_TEST);

    
        glScissor(0, gy, w, TH);
        glClearColor(focused ? 0.90f : 0.85f, focused ? 0.78f : 0.74f, focused ? 0.68f : 0.64f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

    
        glScissor(0, gy, w, 2);
        glClearColor(focused ? 1.00f : 0.70f, focused ? 0.45f : 0.38f, focused ? 0.15f : 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

    
        constexpr int HP = 4;
        if (hover_close) {
            glScissor(w - BW + 1, gy + HP, BW - 2, TH - HP*2);
            glClearColor(0.88f, 0.32f, 0.22f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (hover_max) {
            glScissor(w - BW*2 + 1, gy + HP, BW - 2, TH - HP*2);
            glClearColor(0.82f, 0.68f, 0.55f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (hover_min) {
            glScissor(w - BW*3 + 1, gy + HP, BW - 2, TH - HP*2);
            glClearColor(0.82f, 0.68f, 0.55f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        
        glClearColor(0.60f, 0.40f, 0.22f, 0.35f);
        glScissor(w - BW, gy + 11, 1, TH - 22);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(w - BW*2, gy + 11, 1, TH - 22);
        glClear(GL_COLOR_BUFFER_BIT);

    
        const int cy = gy + TH/2;
        
    
        glClearColor(0.30f, 0.12f, 0.04f, 1.0f);
        int cx = w - BW*5/2;
        glScissor(cx - 5, cy, 10, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy - 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 4, cy - 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        
        cx = w - BW*3/2;
        if (!window->get_window_event_handle().is_maximized) {
            glClearColor(0.30f, 0.12f, 0.04f, 1.0f);
            glScissor(cx - 4, cy - 5, 8, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 5, cy - 4, 1, 8);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 4, cy - 4, 1, 8);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 4, cy + 4, 8, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 3, cy - 6, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 2, cy - 6, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 3, cy + 5, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 2, cy + 5, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            glClearColor(0.55f, 0.32f, 0.15f, 1.0f);
            glScissor(cx - 1, cy - 5, 6, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 4, cy - 4, 1, 6);
            glClear(GL_COLOR_BUFFER_BIT);
            glClearColor(0.30f, 0.12f, 0.04f, 1.0f);
            glScissor(cx - 5, cy - 2, 7, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 5, cy - 1, 1, 6);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 4, cy + 4, 6, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }

    
        cx = w - BW / 2;
        glClearColor(
            hover_close ? 1.0f : 0.30f,
            hover_close ? 0.95f : 0.12f,
            hover_close ? 0.90f : 0.04f,
            1.0f
        );


        glScissor(cx - 5, cy - 5, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy - 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 3, cy - 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 2, cy - 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 1, cy - 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx, cy, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 1, cy + 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 2, cy + 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 3, cy + 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 4, cy + 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        
        glScissor(cx + 4, cy - 5, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 3, cy - 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 2, cy - 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 1, cy - 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx, cy - 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 1, cy, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 2, cy + 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 3, cy + 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy + 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 5, cy + 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
    }



    KNST_FORCE_INLINE void DrawKnstTitleBarWhiteModern()
    {
        constexpr int BW = 46;
        const int TH = window->get_title_bar_height();
        const int w = window->get_window_event_handle().window_width;
        const int h = window->get_window_event_handle().window_height;
        const int gy = h - TH;
        
        
        const int mx = (int)window->m_mouse_x;
        const int my = (int)window->m_mouse_y;
        
        const bool focused = window->get_window_event_handle().is_focused;
        
    
        const bool in_titlebar = (my >= 0 && my <= TH);
        
        
        const bool hover_close = in_titlebar && (mx >= w - BW) && (mx < w);
        const bool hover_max   = in_titlebar && (mx >= w - BW * 2) && (mx < w - BW);
        const bool hover_min   = in_titlebar && (mx >= w - BW * 3) && (mx < w - BW * 2);

        glEnable(GL_SCISSOR_TEST);

        
        glScissor(0, gy, w, TH);
        glClearColor(focused ? 0.95f : 0.88f, focused ? 0.95f : 0.88f, focused ? 0.97f : 0.90f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

    
        glScissor(0, gy, w, 2);
        glClearColor(focused ? 0.68f : 0.52f, focused ? 0.68f : 0.52f, focused ? 0.72f : 0.55f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        
        constexpr int HP = 4;
        if (hover_close) {
            glScissor(w - BW + 1, gy + HP, BW - 2, TH - HP*2);
            glClearColor(0.88f, 0.20f, 0.24f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (hover_max) {
            glScissor(w - BW*2 + 1, gy + HP, BW - 2, TH - HP*2);
            glClearColor(0.78f, 0.78f, 0.82f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (hover_min) {
            glScissor(w - BW*3 + 1, gy + HP, BW - 2, TH - HP*2);
            glClearColor(0.78f, 0.78f, 0.82f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        
        glClearColor(0.58f, 0.58f, 0.61f, 0.35f);
        glScissor(w - BW, gy + 11, 1, TH - 22);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(w - BW*2, gy + 11, 1, TH - 22);
        glClear(GL_COLOR_BUFFER_BIT);

        
        const int cy = gy + TH/2;
        
        
        glClearColor(0.22f, 0.22f, 0.25f, 1.0f);
        int cx = w - BW*5/2;
        glScissor(cx - 5, cy, 10, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy - 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 4, cy - 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        
        cx = w - BW*3/2;
        if (!window->get_window_event_handle().is_maximized) {
            glClearColor(0.22f, 0.22f, 0.25f, 1.0f);
            glScissor(cx - 4, cy - 5, 8, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 5, cy - 4, 1, 8);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 4, cy - 4, 1, 8);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 4, cy + 4, 8, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 3, cy - 6, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 2, cy - 6, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 3, cy + 5, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 2, cy + 5, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            glClearColor(0.52f, 0.52f, 0.55f, 1.0f);
            glScissor(cx - 1, cy - 5, 6, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 4, cy - 4, 1, 6);
            glClear(GL_COLOR_BUFFER_BIT);
            glClearColor(0.22f, 0.22f, 0.25f, 1.0f);
            glScissor(cx - 5, cy - 2, 7, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 5, cy - 1, 1, 6);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 4, cy + 4, 6, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        
        cx = w - BW / 2;
        glClearColor(
            hover_close ? 1.0f : 0.22f,
            hover_close ? 1.0f : 0.22f,
            hover_close ? 1.0f : 0.25f,
            1.0f
        );

        
        glScissor(cx - 5, cy - 5, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy - 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 3, cy - 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 2, cy - 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 1, cy - 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx, cy, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 1, cy + 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 2, cy + 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 3, cy + 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 4, cy + 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);

    
        glScissor(cx + 4, cy - 5, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 3, cy - 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 2, cy - 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 1, cy - 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx, cy - 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 1, cy, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 2, cy + 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 3, cy + 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy + 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 5, cy + 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
    }




    KNST_FORCE_INLINE void DrawKnstTitleBarFuturistic()
    {
        constexpr int BW = 44;
        const int TH = window->get_title_bar_height();
        const int w = window->get_window_event_handle().window_width;
        const int h = window->get_window_event_handle().window_height;
        const int gy = h - TH;
        
        
        const int mx = (int)window->m_mouse_x;
        const int my = (int)window->m_mouse_y;
        
        const bool focused = window->get_window_event_handle().is_focused;
        
        
        const bool in_titlebar = (my >= 0 && my <= TH);
        
        
        const bool hover_close = in_titlebar && (mx >= w - BW) && (mx < w);
        const bool hover_max   = in_titlebar && (mx >= w - BW * 2) && (mx < w - BW);
        const bool hover_min   = in_titlebar && (mx >= w - BW * 3) && (mx < w - BW * 2);

        glEnable(GL_SCISSOR_TEST);

        
        glScissor(0, gy, w, TH);
        glClearColor(focused ? 0.10f : 0.07f, focused ? 0.12f : 0.08f, focused ? 0.18f : 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        
        glScissor(0, gy, w, 2);
        glClearColor(focused ? 0.0f : 0.0f, focused ? 0.85f : 0.35f, focused ? 0.85f : 0.35f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        
        constexpr int HP = 4;
        if (hover_close) {
            glScissor(w - BW + 1, gy + HP, BW - 2, TH - HP*2);
            glClearColor(0.82f, 0.14f, 0.24f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (hover_max) {
            glScissor(w - BW*2 + 1, gy + HP, BW - 2, TH - HP*2);
            glClearColor(0.14f, 0.17f, 0.24f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (hover_min) {
            glScissor(w - BW*3 + 1, gy + HP, BW - 2, TH - HP*2);
            glClearColor(0.14f, 0.17f, 0.24f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        
        glClearColor(0.0f, 0.32f, 0.32f, 0.40f);
        glScissor(w - BW, gy + 11, 1, TH - 22);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(w - BW*2, gy + 11, 1, TH - 22);
        glClear(GL_COLOR_BUFFER_BIT);

    
        const int cy = gy + TH/2;
        
        
        glClearColor(0.0f, 0.90f, 0.90f, 1.0f);
        int cx = w - BW*5/2;
        glScissor(cx - 5, cy, 10, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy - 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 4, cy - 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        
        cx = w - BW*3/2;
        if (!window->get_window_event_handle().is_maximized) {
            glClearColor(0.0f, 0.90f, 0.90f, 1.0f);
            glScissor(cx - 4, cy - 5, 8, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 5, cy - 4, 1, 8);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 4, cy - 4, 1, 8);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 4, cy + 4, 8, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 3, cy - 6, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 2, cy - 6, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 3, cy + 5, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 2, cy + 5, 2, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            glClearColor(0.0f, 0.48f, 0.48f, 1.0f);
            glScissor(cx - 1, cy - 5, 6, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx + 4, cy - 4, 1, 6);
            glClear(GL_COLOR_BUFFER_BIT);
            glClearColor(0.0f, 0.90f, 0.90f, 1.0f);
            glScissor(cx - 5, cy - 2, 7, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 5, cy - 1, 1, 6);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(cx - 4, cy + 4, 6, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }

    
        cx = w - BW / 2;
        glClearColor(
            hover_close ? 1.0f : 0.0f,
            hover_close ? 0.95f : 0.90f,
            hover_close ? 0.95f : 0.90f,
            1.0f
        );

    
        glScissor(cx - 5, cy - 5, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy - 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 3, cy - 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 2, cy - 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 1, cy - 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx, cy, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 1, cy + 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 2, cy + 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 3, cy + 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 4, cy + 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        
        glScissor(cx + 4, cy - 5, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 3, cy - 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 2, cy - 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx + 1, cy - 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx, cy - 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 1, cy, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 2, cy + 1, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 3, cy + 2, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 4, cy + 3, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(cx - 5, cy + 4, 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
    }
#endif

    KNST_FORCE_INLINE void MakeCurrent() {
        #if KNST_USING_PLATFORM_WINDOWS
            wglMakeCurrent(m_hdc, m_hglrc);
        #elif KNST_USING_LINUX_PLATFORM_X11
            #ifdef KNST_OPENGL_USING_EGL
                eglMakeCurrent(s_egl_display, m_egl_surface, m_egl_surface, m_egl_context);
            #elif defined(KNST_OPENGL_USING_GLX)
                glXMakeCurrent(s_glx_display, m_glx_drawable, m_glx_context);
            #endif
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            eglMakeCurrent(s_egl_display, m_egl_surface, m_egl_surface, m_egl_context);
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            #ifdef KNST_PLATFORM_ANDROID_OPENGL
                eglMakeCurrent(s_egl_display, m_egl_surface, m_egl_surface, m_egl_context);
            #endif
        #endif
        
    }

    KNST_FORCE_INLINE void ClearCurrent() {
        #if KNST_USING_PLATFORM_WINDOWS
            wglMakeCurrent(NULL, NULL);
        #elif KNST_USING_LINUX_PLATFORM_X11
            #ifdef KNST_OPENGL_USING_EGL
                eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            #elif defined(KNST_OPENGL_USING_GLX)
                glXMakeCurrent(s_glx_display, None, nullptr);
            #endif
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            #ifdef KNST_PLATFORM_ANDROID_OPENGL
                eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            #endif
        #endif
        
    }

    KNST_FORCE_INLINE bool IsCurrent() const {
        #if KNST_USING_PLATFORM_WINDOWS
            return wglGetCurrentContext() == m_hglrc;
        #elif KNST_USING_LINUX_PLATFORM_X11
            #ifdef KNST_OPENGL_USING_EGL
                return eglGetCurrentContext() == m_egl_context;
            #elif defined(KNST_OPENGL_USING_GLX)
                return glXGetCurrentContext() == m_glx_context;
            #endif
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            return eglGetCurrentContext() == m_egl_context;
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            #ifdef KNST_PLATFORM_ANDROID_OPENGL
                return eglGetCurrentContext() == m_egl_context;
            #endif
        #endif
        
    }

    KNST_FORCE_INLINE void SetVSync(bool enabled) {
        m_vsync_enabled = enabled;
        #if KNST_USING_PLATFORM_WINDOWS
            if (wglSwapIntervalEXT) wglSwapIntervalEXT(enabled ? 1 : 0);
        #elif KNST_USING_LINUX_PLATFORM_X11
            #ifdef KNST_OPENGL_USING_EGL
                eglSwapInterval(s_egl_display, enabled ? 1 : 0);
            #elif defined(KNST_OPENGL_USING_GLX)
                typedef void (*glXSwapIntervalEXT)(Display*, GLXDrawable, int);
                glXSwapIntervalEXT swapInterval = (glXSwapIntervalEXT)glXGetProcAddress((const GLubyte*)"glXSwapIntervalEXT");
                if (swapInterval) swapInterval(s_glx_display, m_glx_drawable, enabled ? 1 : 0);
            #endif
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            #ifdef KNST_PLATFORM_ANDROID_OPENGL
                eglSwapInterval(s_egl_display, enabled ? 1 : 0);
            #endif
        #endif
    }

    KNST_FORCE_INLINE bool GetVSync() const { return m_vsync_enabled; }

    static void* GLADLoaderWrapper(const char* name) {
        #if KNST_USING_PLATFORM_WINDOWS
            void* proc = (void*)wglGetProcAddress(name);
            if (!proc) {
                HMODULE module = GetModuleHandleA("opengl32.dll");
                proc = (void*)::GetProcAddress(module, name);
            }
            return proc;
        #elif KNST_USING_LINUX_PLATFORM_X11
            #ifdef KNST_OPENGL_USING_EGL
                return (void*)eglGetProcAddress(name);
            #elif defined(KNST_OPENGL_USING_GLX)
                return (void*)glXGetProcAddress((const GLubyte*)name);
            #else
                return nullptr;
            #endif
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            return (void*)eglGetProcAddress(name);
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            #ifdef KNST_PLATFORM_ANDROID_OPENGL
                return (void*)eglGetProcAddress(name);
            #endif
        #else
            return nullptr;
        #endif
    }

    KNST_FORCE_INLINE void Resize(int width, int height) {
        glViewport(0, 0, width, height);
        #if KNST_USING_LINUX_PLATFORM_WAYLAND
            if (m_wl_egl_window) wl_egl_window_resize(m_wl_egl_window, width, height, 0, 0);
        #endif
    }

    KNST_FORCE_INLINE void Resize() {
        const auto& event = window->get_window_event_handle();
        glViewport(0, 0, event.window_width, event.window_height);
        #if KNST_USING_LINUX_PLATFORM_WAYLAND
            if (m_wl_egl_window) wl_egl_window_resize(m_wl_egl_window, event.window_width, event.window_height, 0, 0);
        #endif
    }

    KNST_FORCE_INLINE bool HasExtension(const char* name) const {
        for (const auto& ext : s_extensions) if (ext == name) return true;
        return false;
    }

    KNST_FORCE_INLINE const knst_vector<knst_byte_string>& GetExtensions() const { return s_extensions; }
    KNST_FORCE_INLINE const char* GetRenderer() const { return (const char*)glGetString(GL_RENDERER); }
    KNST_FORCE_INLINE const char* GetVendor() const { return (const char*)glGetString(GL_VENDOR); }
    KNST_FORCE_INLINE const char* GetVersion() const { return (const char*)glGetString(GL_VERSION); }
    KNST_FORCE_INLINE const char* GetGLSLVersion() const { return (const char*)glGetString(0x8B8C); }
    KNST_FORCE_INLINE bool IsInitialized() const { return m_initialized; }
    KNST_FORCE_INLINE static bool IsInitialized_Static() { return s_initialized; }
    KNST_FORCE_INLINE static int GetRefCount() { return s_ref_count; }

    KNST_FORCE_INLINE void* GetNativeContext() {
        #if KNST_USING_PLATFORM_WINDOWS
            return (void*)m_hglrc;
        #elif KNST_USING_LINUX_PLATFORM_X11
            #ifdef KNST_OPENGL_USING_EGL
                return (void*)m_egl_context;
            #elif defined(KNST_OPENGL_USING_GLX)
                return (void*)m_glx_context;
            #endif
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            return (void*)m_egl_context;
        #elif defined(KNST_USING_PLATFORM_ANDROID)
            #ifdef KNST_PLATFORM_ANDROID_OPENGL
                return (void*)m_egl_context;
            #endif
        #endif
        
    }

    KNST_FORCE_INLINE void* GetNativeDisplay() {
        #if KNST_USING_PLATFORM_WINDOWS
            return (void*)m_hdc;
        #elif KNST_USING_LINUX_PLATFORM_X11
            #ifdef KNST_OPENGL_USING_EGL
                return (void*)s_egl_display;
            #elif defined(KNST_OPENGL_USING_GLX)
                return (void*)s_glx_display;
            #endif
        #elif KNST_USING_LINUX_PLATFORM_WAYLAND
            return (void*)s_egl_display;
        #endif
    }
};

#if KNST_USING_PLATFORM_WINDOWS
    #include "windows/knst_windows_win32_opengl_support.hpp"
#elif KNST_USING_LINUX_PLATFORM_X11
    #ifdef KNST_OPENGL_USING_EGL
        #include "linux/x11/egl/knst_linux_x11_opengl_egl_support.hpp"
    #elif defined(KNST_OPENGL_USING_GLX)
        #include "linux/x11/glx/knst_linux_x11_opengl_glx_support.hpp"
    #endif
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    #include "linux/wayland/knst_linux_wayland_opengl_egl_support.hpp"
#elif defined(KNST_USING_PLATFORM_ANDROID)
    #ifdef KNST_PLATFORM_ANDROID_OPENGL
        #include "android/knst_android_opengl_egl_support.hpp"
    #endif

#endif

#endif
#endif // KNST_WINDOW_OPENGL_MANAGER_HPP