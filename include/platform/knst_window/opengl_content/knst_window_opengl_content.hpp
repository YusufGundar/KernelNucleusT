/*
----------------------------
knst_window_opengl_content.hpp
----------------------------

    OpenGL context manager.

    Platform backends (fixed, not selectable):
      Windows -> WGL (opengl32.dll)
      X11     -> EGL (X11 platform, eglGetPlatformDisplay)
      Wayland -> EGL (Wayland platform, eglGetPlatformDisplay)
      Android -> EGL (native)

    Two layers:
      knst_opengl_global         -> shared resources (display, config)
      knst_window_opengl_content -> per-window context + surface

    Covers:
      - GL 3.0-4.6 core + GLES 2.0-3.2
      - Forward-compat, debug, robust contexts
      - Context sharing across windows
      - MSAA, sRGB, stereo
      - Resize handling on every platform
      - Per-window vsync (auto-off during resize)
      - Query API (actual version, vendor, renderer) — GLAD-safe
      - Windows WGL extension loader (dummy window trick)
      - GLAD / GLEW / custom-loader entry point (LoadGLProc)
      - X11 _NET_WM_SYNC_REQUEST (flicker-free resize)
      - Exception-free (bool return)

    GLAD LOAD ORDER
    ---------------
    This header works even though Init() runs BEFORE gladLoadGLLoader().
    All GL calls inside it (only glGetString) go through the platform
    loader directly, bypassing GLAD's macro redirections.

    The library provides GLAD by default; if you wish to change this, you can make modifications in the 'Kernel Nucleus.cpp' file.

    VSYNC BEHAVIOR
    --------------
    Default is OFF (options.vsync = false). Change anytime with
    SetSwapInterval(0|1).

    During resize, vsync is disabled so dragging stays responsive. Once
    the resize settles (no event for RESIZE_SETTLE_MS), the user's
    preference comes back.

    Usage:
        KnstWindowSources::Init();

        knst_opengl_global::Options opts;
        opts.gl_major = 3;
        opts.gl_minor = 3;
        opts.core_profile = true;
        opts.msaa = true;
        opts.vsync = false; // default off

        knst_opengl_global::Init(opts);

        knst_window w(1280, 720, u"Hello GL");
        w.creation_and_show();

        knst_window_opengl_content gl;
        gl.Init(&w); // creates context — no GLAD needed yet

        gl.MakeCurrent();
        gladLoadGLLoader((GLADloadproc)knst_window_opengl_content::LoadGLProc);
        // ^ gl* calls are safe from here on

        while (!w.is_should_close()) {
            // gather events, handle input...
            gl.MakeCurrent();
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            // draw...
            gl.SwapBuffers(); // presents + auto vsync
        }

        gl.Shutdown();
        knst_opengl_global::CleanUp();
        w.destroy();
        KnstWindowSources::CleanUp();
*/

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>


// ─── Platform headers ───────────────────────────────────────────────────────

#if KNST_USING_PLATFORM_WINDOWS

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <GL/gl.h>

    // WGL extension typedefs.
    typedef HGLRC (WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
    typedef BOOL  (WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
    typedef BOOL  (WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
    typedef BOOL  (WINAPI* PFNWGLGETPIXELFORMATATTRIBIVARBPROC)(HDC, int, int, UINT, const int*, int*);

    // WGL constants (from wglext.h).
    #define WGL_DRAW_TO_WINDOW_ARB           0x2001
    #define WGL_SUPPORT_OPENGL_ARB           0x2010
    #define WGL_DOUBLE_BUFFER_ARB            0x2011
    #define WGL_STEREO_ARB                   0x2012
    #define WGL_PIXEL_TYPE_ARB               0x2013
    #define WGL_RED_BITS_ARB                 0x2015
    #define WGL_GREEN_BITS_ARB               0x2017
    #define WGL_BLUE_BITS_ARB                0x2019
    #define WGL_ALPHA_BITS_ARB               0x201B
    #define WGL_DEPTH_BITS_ARB               0x2022
    #define WGL_STENCIL_BITS_ARB             0x2023
    #define WGL_ACCUM_RED_BITS_ARB           0x201E
    #define WGL_ACCUM_GREEN_BITS_ARB         0x201F
    #define WGL_ACCUM_BLUE_BITS_ARB          0x2020
    #define WGL_ACCUM_ALPHA_BITS_ARB         0x2021
    #define WGL_AUX_BUFFERS_ARB              0x2024
    #define WGL_SAMPLE_BUFFERS_ARB           0x2041
    #define WGL_SAMPLES_ARB                  0x2042
    #define WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB 0x20A9
    #define WGL_TYPE_RGBA_ARB                0x202B
    #define WGL_ACCELERATION_ARB             0x2003
    #define WGL_FULL_ACCELERATION_ARB        0x2027
    #define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
    #define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
    #define WGL_CONTEXT_FLAGS_ARB            0x2094
    #define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
    #define WGL_CONTEXT_DEBUG_BIT_ARB              0x00000001
    #define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
    #define WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB      0x00000004
    #define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
    #define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
    #define WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB 0x8256
    #define WGL_NO_RESET_NOTIFICATION_ARB    0x8261
    #define WGL_LOSE_CONTEXT_ON_RESET_ARB    0x8252
    #define WGL_CONTEXT_RELEASE_BEHAVIOR_ARB       0x2097
    #define WGL_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB  0x0000
    #define WGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_ARB 0x2098

#else // --- everything else uses EGL ---

    #if KNST_USING_LINUX_PLATFORM_X11
        #include <X11/Xlib.h>
    #endif

    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        #include <wayland-client.h>
        #include <wayland-egl.h>
    #endif

    #if !KNST_USING_PLATFORM_WINDOWS
        #include <EGL/egl.h>
        #include <EGL/eglext.h>
    #endif

    #if KNST_USING_LINUX_PLATFORM_X11 || KNST_USING_LINUX_PLATFORM_WAYLAND
        #include <GL/gl.h>
    #endif

    #if defined(KNST_USING_PLATFORM_ANDROID)
        // Skip system GLES header if GLAD is already in play.
        #if !defined(GLAD_H) && !defined(__glad_h_)
            #include <GLES3/gl3.h>
        #endif
        #include <android/native_window.h>
        #include <android/log.h>
    #endif
#endif


// ─── EGL fallback constants ────────────────────────────────────────────────
// Some headers don't define these; fill them in if missing.

#if !KNST_USING_PLATFORM_WINDOWS
    #ifndef EGL_CONTEXT_MAJOR_VERSION
        #define EGL_CONTEXT_MAJOR_VERSION         0x3098
        #define EGL_CONTEXT_MINOR_VERSION         0x30FB
    #endif
    #ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
        #define EGL_CONTEXT_OPENGL_PROFILE_MASK              0x30FD
        #define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT          0x00000001
        #define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x00000002
    #endif
    #ifndef EGL_CONTEXT_OPENGL_DEBUG
        #define EGL_CONTEXT_OPENGL_DEBUG             0x31B0
    #endif
    #ifndef EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE
        #define EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE 0x31B1
    #endif
    #ifndef EGL_CONTEXT_OPENGL_ROBUST_ACCESS
        #define EGL_CONTEXT_OPENGL_ROBUST_ACCESS     0x31B2
    #endif
    #ifndef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY
        #define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY 0x31BD
        #define EGL_NO_RESET_NOTIFICATION             0x31BE
        #define EGL_LOSE_CONTEXT_ON_RESET             0x31BF
    #endif
    #ifndef EGL_CONTEXT_CLIENT_VERSION
        #define EGL_CONTEXT_CLIENT_VERSION            0x3098
    #endif
    #ifndef EGL_PLATFORM_X11_KHR
        #define EGL_PLATFORM_X11_KHR                  0x31D5
    #endif
    #ifndef EGL_PLATFORM_WAYLAND_KHR
        #define EGL_PLATFORM_WAYLAND_KHR              0x31D8
    #endif

    // Some older EGL headers don't define the ES3 bit.
    #ifndef EGL_OPENGL_ES3_BIT
        #define EGL_OPENGL_ES3_BIT                    0x00000040
    #endif

    typedef EGLDisplay (EGLAPIENTRYP PFNEGLGETPLATFORMDISPLAYEXTPROC)(
        EGLenum platform, void* native_display, const EGLint* attrib_list);
#endif


// ─── WGL extension function pointers (Windows only) ────────────────────────

#if KNST_USING_PLATFORM_WINDOWS
    namespace knst_gl_detail {
        inline PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
        inline PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = nullptr;
        inline PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr;
    }
#endif


// ─── knst_opengl_global — shared state ─────────────────────────────────────

class knst_opengl_global {
public:
    enum class ClientAPI : uint8_t {
        OpenGL,
        OpenGLES
    };

    // "None" is avoided because X11 defines it as a macro (0L).
    enum class Robustness : uint8_t {
        Off,
        NoReset,
        Reset
    };

    // Same reason: "NoFlush" instead of "None".
    enum class ReleaseBehavior : uint8_t {
        Flush,
        NoFlush
    };

    struct Options {
        ClientAPI client_api =
        #if defined(KNST_USING_PLATFORM_ANDROID)
            ClientAPI::OpenGLES;
        #else
            ClientAPI::OpenGL;
        #endif

        int gl_major = 3;
        int gl_minor = 3;

        bool core_profile = true;
        bool forward_compat = false;

        bool debug_context = false;
        bool robust_access = false;
        Robustness robustness = Robustness::Off;
        ReleaseBehavior release_behavior = ReleaseBehavior::Flush;

        int red_bits = 8;
        int green_bits = 8;
        int blue_bits = 8;
        int alpha_bits = 8;

        int depth_bits = 24;
        int stencil_bits = 8;

        int accum_red_bits = 0;
        int accum_green_bits = 0;
        int accum_blue_bits = 0;
        int accum_alpha_bits = 0;
        int aux_buffers = 0;

        bool double_buffer = true;
        bool stereo = false;
        bool srgb = false;

        bool msaa = false;
        int  msaa_samples = 4;

        // Vsync preference. Default OFF. Resize always forces off
        // temporarily for responsiveness, regardless of this.
        bool vsync = false;
    };

    static bool Init() noexcept { return Init(Options{}); }
    static bool Init(const Options& opts) noexcept;
    static void CleanUp() noexcept;

#if KNST_USING_LINUX_PLATFORM_X11
    static bool Init_x11(const Options& opts) noexcept;
    static void CleanUp_x11() noexcept;
#endif
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    static bool Init_wayland(const Options& opts) noexcept;
    static void CleanUp_wayland() noexcept;
#endif

    static bool IsInitialized() noexcept { return s_initialized; }
    static const Options& GetOptions() noexcept { return s_options; }

#if KNST_USING_PLATFORM_WINDOWS
    static HINSTANCE GetHInstance() noexcept { return s_hinstance; }
#else
    static EGLDisplay GetEGLDisplay() noexcept { return s_egl_display; }
    static EGLConfig GetEGLConfig()  noexcept { return s_egl_config;  }
#endif

private:
    knst_opengl_global() = delete;

    static Options s_options;
    static bool s_initialized;

#if KNST_USING_PLATFORM_WINDOWS
    static HINSTANCE s_hinstance;
#else
    static EGLDisplay s_egl_display;
    static EGLConfig s_egl_config;
#endif
};


inline knst_opengl_global::Options knst_opengl_global::s_options{};
inline bool knst_opengl_global::s_initialized = false;

#if KNST_USING_PLATFORM_WINDOWS
    inline HINSTANCE knst_opengl_global::s_hinstance = nullptr;
#else
    inline EGLDisplay knst_opengl_global::s_egl_display = EGL_NO_DISPLAY;
    inline EGLConfig  knst_opengl_global::s_egl_config  = nullptr;
#endif


// ─── knst_window_opengl_content — per-window context + surface ─────────────

class knst_window_opengl_content : public knst_surface_client {
public:
    // How long to wait after the last resize event before restoring
    // the user's preferred swap interval.
    //
    // 5 ms was too short — resize events arrive every 8-16 ms, so
    // every frame triggered restore -> off -> restore. 50 ms lets the
    // drag settle once and then restore cleanly.
    static constexpr uint32_t DEFAULT_RESIZE_SETTLE_MS = 50;

    knst_window_opengl_content() noexcept = default;
    ~knst_window_opengl_content() noexcept { Shutdown(); }

    knst_window_opengl_content(const knst_window_opengl_content&) = delete;
    knst_window_opengl_content& operator=(const knst_window_opengl_content&) = delete;

    knst_window_opengl_content(knst_window_opengl_content&& other) noexcept {
        *this = std::move(other);
    }
    knst_window_opengl_content& operator=(knst_window_opengl_content&& other) noexcept;

    // ─── knst_surface_client implementation ────────────────────────────
    // Called by the library on window lifecycle events. You never need
    // to call ReleaseSurface / RecreateSurface yourself.

    void on_surface_release() noexcept override {
        ReleaseSurface();
    }

    bool on_surface_recreate() noexcept override {
        return RecreateSurface();
    }

    // Window destruction: native window is still alive at this point.
    // Full Shutdown() here is safe — inside window::destroy() the
    // native window gets destroyed AFTER notify_window_destroyed().
    void on_window_destroyed() noexcept override { Shutdown(); }

    // ─── Setup ─────────────────────────────────────────────────────────
    bool Init(knst_window* window, bool reuse_old = false) noexcept;
    bool InitShared(knst_window* window, const knst_window_opengl_content& share) noexcept;
    void Shutdown() noexcept;

    // ─── GL operations ─────────────────────────────────────────────────
    void MakeCurrent() noexcept;
    void SwapBuffers() noexcept;

    // Vsync preference. 0 = off, 1 = on, -1 = adaptive. Remembered and
    // restored after resize activity.
    void SetSwapInterval(int interval) noexcept;

    // Returns the user's preference (not the currently applied one).
    int GetSwapInterval() const noexcept { return m_user_vsync; }

    // ─── Resize ────────────────────────────────────────────────────────
    void OnResize(int new_width, int new_height) noexcept;

    // Framebuffer scale change (Wayland fractional-scale). Logical size
    // may stay the same but the buffer is resized. Call after
    // KNST_WINDOW_EVENT_SCALE_CHANGED.
    void OnScaleChanged() noexcept;

    // Dynamic window recreation (Android):
    //   APP_CMD_TERM_WINDOW -> ReleaseSurface()
    //   APP_CMD_INIT_WINDOW -> RecreateSurface()
    // Only the surface is rebuilt; the context stays alive. If the
    // context is lost too (rare), use Shutdown() + Init().
    bool RecreateSurface() noexcept;
    void ReleaseSurface() noexcept;

    // ─── State ─────────────────────────────────────────────────────────
    bool IsValid() const noexcept { return m_valid; }
    knst_window* GetWindow() const noexcept { return m_window; }

    // is_frame_ready() only checks app->window != nullptr. But the
    // surface may still be invalid if recreate hasn't happened yet.
    // Calling GL in that state would crash.
    bool HasSurface() const noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        // Windows is WGL — no EGL surface. HGLRC is the GL context.
        return m_valid && m_hglrc != nullptr;
    #else
        return m_valid && m_egl_surface != EGL_NO_SURFACE;
    #endif
    }

    int GetWidth()  const noexcept { return m_width;  }
    int GetHeight() const noexcept { return m_height; }

    // Framebuffer (physical) size. On Wayland with fractional scale
    // this differs from the logical size. glViewport should use these.
    // On other platforms it matches GetWidth / GetHeight.
    int GetFramebufferWidth() const noexcept {
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        if (m_window &&
            KnstWindowSources::s_active_backend == knst_backend::wayland) {
            return m_window->wayland_physical_width(m_width);
        }
    #endif
        return m_width;
    }

    int GetFramebufferHeight() const noexcept {
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        if (m_window &&
            KnstWindowSources::s_active_backend == knst_backend::wayland) {
            return m_window->wayland_physical_height(m_height);
        }
    #endif
        return m_height;
    }

    const char* GetVendorString() const noexcept { return m_vendor;}
    const char* GetRendererString() const noexcept { return m_renderer;}
    const char* GetVersionString() const noexcept { return m_version;}
    const char* GetGLSLVersionString() const noexcept { return m_glsl_version; }

    int GetActualMajorVersion() const noexcept {return m_actual_major; }
    int GetActualMinorVersion() const noexcept {return m_actual_minor; }

    knst_opengl_global::ClientAPI GetClientAPI() const noexcept { return m_client_api; }

    // ─── GLAD / GLEW / custom-loader entry point ───────────────────────
    // Pass this to gladLoadGLLoader() with a current context. Also used
    // internally so Init() works before GLAD is loaded.
    static void* LoadGLProc(const char* name) noexcept {
        if (!name) return nullptr;

    #if KNST_USING_PLATFORM_WINDOWS
        void* p = (void*)wglGetProcAddress(name);
        if (!p || p == (void*)0x1 || p == (void*)0x2 ||
            p == (void*)0x3 || p == (void*)-1) {
            HMODULE mod = GetModuleHandleA("opengl32.dll");
            p = (void*)::GetProcAddress(mod, name);
        }
        return p;
    #else
        return (void*)eglGetProcAddress(name);
    #endif
    }

private:
    // Send the interval to the platform driver (low-level).
    void apply_swap_interval_raw(int interval) noexcept;

    // Restore the user's vsync preference once resize settles.
    void maybe_restore_vsync() noexcept;

#if !KNST_USING_PLATFORM_WINDOWS
    // Rebuild the EGL context after EGL_CONTEXT_LOST. Destroys the old
    // context, creates a new one with the same EGLConfig, makes it
    // current. Returns false if unrecoverable — the app needs a restart.
    //
    // On success the window sees is_gl_context_lost() == true and the
    // app rebuilds its GL resources.
    bool recover_context() noexcept;
#endif

    knst_window* m_window = nullptr;
    bool m_valid = false;
    int m_width = 0;
    int m_height = 0;

    const char* m_vendor = "unknown";
    const char* m_renderer = "unknown";
    const char* m_version = "unknown";
    const char* m_glsl_version = "unknown";
    int m_actual_major = 0;
    int m_actual_minor = 0;

    knst_opengl_global::ClientAPI m_client_api = knst_opengl_global::ClientAPI::OpenGL;

    int m_user_vsync = 0;
    int m_current_swap_interval = 0;
    uint32_t m_last_resize_time = 0;
    uint32_t m_resize_settle_ms = DEFAULT_RESIZE_SETTLE_MS;

#if KNST_USING_PLATFORM_WINDOWS
    HDC m_hdc = nullptr;
    HGLRC m_hglrc = nullptr;
    int m_pixel_format = 0;
    HWND m_hwnd = nullptr;
#endif

#if !KNST_USING_PLATFORM_WINDOWS
    EGLSurface m_egl_surface = EGL_NO_SURFACE;
    EGLContext m_egl_context = EGL_NO_CONTEXT;
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    struct wl_egl_window* m_egl_window = nullptr;
#endif
};


// ============================================================================
// IMPLEMENTATION — knst_opengl_global
// ============================================================================

// ─── Windows (WGL) ─────────────────────────────────────────────────────────

#if KNST_USING_PLATFORM_WINDOWS

namespace knst_gl_detail {

inline bool load_wgl_extensions() noexcept {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"KnstGLDummyClass";

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }

    HWND dummy = CreateWindowExW(
        0, L"KnstGLDummyClass", L"", WS_OVERLAPPEDWINDOW,
        0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (!dummy) return false;



    HDC dc = GetDC(dummy);


    if (!dc) { DestroyWindow(dummy); return false; }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cAlphaBits = 8;

    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) {
        ReleaseDC(dummy, dc); DestroyWindow(dummy); return false;
    }

    HGLRC rc = wglCreateContext(dc);
    if (!rc) { ReleaseDC(dummy, dc); DestroyWindow(dummy); return false; }

    if (!wglMakeCurrent(dc, rc)) {
        wglDeleteContext(rc); ReleaseDC(dummy, dc);
        DestroyWindow(dummy); return false;
    }

    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)
        wglGetProcAddress("wglCreateContextAttribsARB");
    wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)
        wglGetProcAddress("wglChoosePixelFormatARB");
    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)
        wglGetProcAddress("wglSwapIntervalEXT");



    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(dummy, dc);
    DestroyWindow(dummy);

    return wglCreateContextAttribsARB != nullptr;
}

} // namespace knst_gl_detail

inline bool knst_opengl_global::Init(const Options& opts) noexcept {
    if (s_initialized) return true;

    s_hinstance = KnstWindowSources::get_windows_native_instance_handle();
    if (!s_hinstance) return false;

    s_options = opts;

    if (!knst_gl_detail::load_wgl_extensions()) {
        // Extensions not available — fall back to GL 2.1.
        s_options.core_profile = false;
        s_options.forward_compat = false;
        s_options.debug_context = false;
        s_options.robust_access = false;
        if (s_options.gl_major > 2) {
            s_options.gl_major = 2;
            s_options.gl_minor = 1;
        }
    }

    
    s_initialized = true;
    
    return true;
}

inline void knst_opengl_global::CleanUp() noexcept {
    s_initialized = false;
    s_hinstance   = nullptr;
}

#endif // Windows


// ─── X11 (EGL) ─────────────────────────────────────────────────────────────

#if KNST_USING_LINUX_PLATFORM_X11

namespace knst_gl_detail {

// Prefer eglGetPlatformDisplayEXT; fall back to eglGetDisplay.
inline EGLDisplay create_egl_display_x11(Display* x11_dpy) noexcept {
    auto get_platform = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (get_platform) {
        EGLDisplay d = get_platform(EGL_PLATFORM_X11_KHR, (void*)x11_dpy, nullptr);
        if (d != EGL_NO_DISPLAY) return d;
    }

    return eglGetDisplay((EGLNativeDisplayType)x11_dpy);
}

} // namespace knst_gl_detail

inline bool knst_opengl_global::Init_x11(const Options& opts) noexcept {
    if (s_initialized) return true;

    Display* x11_dpy = KnstWindowSources::get_native_x11_display();
    if (!x11_dpy) return false;

    s_options = opts;

    s_egl_display = knst_gl_detail::create_egl_display_x11(x11_dpy);
    if (s_egl_display == EGL_NO_DISPLAY) return false;

    EGLint major = 0, minor = 0;
    if (!eglInitialize(s_egl_display, &major, &minor)) {
        s_egl_display = EGL_NO_DISPLAY;
        return false;
    }

    EGLint api = (opts.client_api == ClientAPI::OpenGLES)
                    ? EGL_OPENGL_ES_API
                    : EGL_OPENGL_API;
    eglBindAPI(api);

    // If ES3 requested, filter configs to ES3.
    EGLint renderable = (opts.client_api == ClientAPI::OpenGLES)
        ? (opts.gl_major >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT)
        : EGL_OPENGL_BIT;

    EGLint attribs[32];
    int i = 0;
    attribs[i++] = EGL_SURFACE_TYPE; attribs[i++] = EGL_WINDOW_BIT;
    attribs[i++] = EGL_RENDERABLE_TYPE; attribs[i++] = renderable;
    attribs[i++] = EGL_RED_SIZE; attribs[i++] = opts.red_bits;
    attribs[i++] = EGL_GREEN_SIZE; attribs[i++] = opts.green_bits;
    attribs[i++] = EGL_BLUE_SIZE; attribs[i++] = opts.blue_bits;
    // X11 windows are typically 24-bit (no alpha). Asking for 8 alpha
    // narrows the search and can pick an incompatible config ->
    // eglCreateWindowSurface fails with EGL_BAD_MATCH.
    attribs[i++] = EGL_ALPHA_SIZE; attribs[i++] = 0;
    attribs[i++] = EGL_DEPTH_SIZE; attribs[i++] = opts.depth_bits;
    attribs[i++] = EGL_STENCIL_SIZE; attribs[i++] = opts.stencil_bits;
    if (opts.msaa) {
        attribs[i++] = EGL_SAMPLE_BUFFERS; attribs[i++] = 1;
        attribs[i++] = EGL_SAMPLES; attribs[i++] = opts.msaa_samples;
    }
    attribs[i++] = EGL_NONE;

    // Try multiple configs so we can pick one matching the window's
    // visual ID (avoids BadMatch with proprietary NVIDIA drivers).
    EGLint num_configs = 0;
    EGLConfig candidates[64];
    if (!eglChooseConfig(s_egl_display, attribs, candidates, 64, &num_configs)
        || num_configs == 0) {

        // Retry without MSAA. Some drivers (Intel iGPU, virtual GPUs,
        // Mesa llvmpipe in WSLg) don't expose an MSAA config with the
        // exact bit depths we asked for.

        for (int k = 0; k + 1 < i; ) {
            if (attribs[k] == EGL_SAMPLE_BUFFERS) {
                attribs[k + 1] = 0;
                k += 2;
            } else if (attribs[k] == EGL_SAMPLES) {
                attribs[k + 1] = 0;
                k += 2;
            } else {
                k += 2;
            }
        }

        num_configs = 0;
        if (!eglChooseConfig(s_egl_display, attribs, candidates, 64, &num_configs)
            || num_configs == 0) {
            eglTerminate(s_egl_display);
            s_egl_display = EGL_NO_DISPLAY;
            return false;
        }
    }

    // Prefer a config whose native visual matches the X11 root visual.
    s_egl_config = candidates[0];
    Visual* x11_visual = DefaultVisual(x11_dpy, DefaultScreen(x11_dpy));
    for (EGLint c = 0; c < num_configs; ++c) {
        EGLint vid = 0;
        if (eglGetConfigAttrib(s_egl_display, candidates[c], EGL_NATIVE_VISUAL_ID, &vid)) {
            if ((VisualID)vid == XVisualIDFromVisual(x11_visual)) {
                s_egl_config = candidates[c];
                break;
            }
        }
    }

    s_initialized = true;
    return true;
}

inline void knst_opengl_global::CleanUp_x11() noexcept {
    if (s_egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglTerminate(s_egl_display);
        s_egl_display = EGL_NO_DISPLAY;
        s_egl_config  = nullptr;
    }
    s_initialized = false;
}

#endif // X11


// ─── Wayland (EGL) ─────────────────────────────────────────────────────────

#if KNST_USING_LINUX_PLATFORM_WAYLAND

namespace knst_gl_detail {

inline EGLDisplay create_egl_display_wayland(wl_display* wl_disp) noexcept {
    auto get_platform = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (get_platform) {
        EGLDisplay d = get_platform(EGL_PLATFORM_WAYLAND_KHR, (void*)wl_disp, nullptr);
        if (d != EGL_NO_DISPLAY) return d;
    }

    return eglGetDisplay((EGLNativeDisplayType)wl_disp);
}

} // namespace knst_gl_detail

inline bool knst_opengl_global::Init_wayland(const Options& opts) noexcept {
    if (s_initialized) return true;

    wl_display* wl_disp = KnstWindowSources::wayland_display;
    if (!wl_disp) return false;

    s_egl_display = knst_gl_detail::create_egl_display_wayland(wl_disp);
    if (s_egl_display == EGL_NO_DISPLAY) return false;

    EGLint major = 0, minor = 0;
    if (!eglInitialize(s_egl_display, &major, &minor)) {
        s_egl_display = EGL_NO_DISPLAY;
        return false;
    }

    EGLint api = (opts.client_api == ClientAPI::OpenGLES)
                    ? EGL_OPENGL_ES_API
                    : EGL_OPENGL_API;
    eglBindAPI(api);

    EGLint renderable = (opts.client_api == ClientAPI::OpenGLES)
        ? (opts.gl_major >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT)
        : EGL_OPENGL_BIT;

    EGLint attribs[32];
    int i = 0;
    attribs[i++] = EGL_SURFACE_TYPE; attribs[i++] = EGL_WINDOW_BIT;
    attribs[i++] = EGL_RENDERABLE_TYPE; attribs[i++] = renderable;
    attribs[i++] = EGL_RED_SIZE;        attribs[i++] = opts.red_bits;
    attribs[i++] = EGL_GREEN_SIZE;      attribs[i++] = opts.green_bits;
    attribs[i++] = EGL_BLUE_SIZE;       attribs[i++] = opts.blue_bits;
    attribs[i++] = EGL_ALPHA_SIZE;      attribs[i++] = opts.alpha_bits;
    attribs[i++] = EGL_DEPTH_SIZE;      attribs[i++] = opts.depth_bits;
    attribs[i++] = EGL_STENCIL_SIZE;    attribs[i++] = opts.stencil_bits;
    if (opts.msaa) {
        attribs[i++] = EGL_SAMPLE_BUFFERS; attribs[i++] = 1;
        attribs[i++] = EGL_SAMPLES;        attribs[i++] = opts.msaa_samples;
    }
    attribs[i++] = EGL_NONE;

    EGLint num_configs = 0;
    if (!eglChooseConfig(s_egl_display, attribs, &s_egl_config, 1, &num_configs)
        || num_configs == 0) {

        // MSAA fallback (same as X11 branch).
        for (int k = 0; k + 1 < i; ) {
            if (attribs[k] == EGL_SAMPLE_BUFFERS) {
                attribs[k + 1] = 0;
                k += 2;
            } else if (attribs[k] == EGL_SAMPLES) {
                attribs[k + 1] = 0;
                k += 2;
            } else {
                k += 2;
            }
        }

        num_configs = 0;
        if (!eglChooseConfig(s_egl_display, attribs, &s_egl_config, 1, &num_configs)
            || num_configs == 0) {
            eglTerminate(s_egl_display);
            s_egl_display = EGL_NO_DISPLAY;
            return false;
        }
    }

    s_options     = opts;
    s_initialized = true;
    return true;
}

inline void knst_opengl_global::CleanUp_wayland() noexcept {
    if (s_egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglTerminate(s_egl_display);
        s_egl_display = EGL_NO_DISPLAY;
        s_egl_config  = nullptr;
    }
    s_initialized = false;
}

#endif // Wayland


// ─── Linux (X11 + Wayland) — dispatcher ────────────────────────────────────
// Dual mode picks at runtime; single mode forwards directly.

#if KNST_USING_LINUX_PLATFORM_X11 || KNST_USING_LINUX_PLATFORM_WAYLAND

inline bool knst_opengl_global::Init(const Options& opts) noexcept {

#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return Init_x11(opts);
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return Init_wayland(opts);
    return false;
#elif KNST_USING_LINUX_PLATFORM_X11
    return Init_x11(opts);
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    return Init_wayland(opts);
#endif
}

inline void knst_opengl_global::CleanUp() noexcept {
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return CleanUp_x11();
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return CleanUp_wayland();
    return;
#elif KNST_USING_LINUX_PLATFORM_X11
    return CleanUp_x11();
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    return CleanUp_wayland();
#endif
}

#endif // KNST_USING_LINUX_PLATFORM_X11 || KNST_USING_LINUX_PLATFORM_WAYLAND


// ─── Android (EGL) ─────────────────────────────────────────────────────────

#if defined(KNST_USING_PLATFORM_ANDROID)

inline bool knst_opengl_global::Init(const Options& opts) noexcept {
    if (s_initialized) return true;

    s_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_egl_display == EGL_NO_DISPLAY) return false;

    EGLint major = 0, minor = 0;
    if (!eglInitialize(s_egl_display, &major, &minor)) {
        s_egl_display = EGL_NO_DISPLAY;
        return false;
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint attribs[32];
    int i = 0;
    attribs[i++] = EGL_SURFACE_TYPE;    attribs[i++] = EGL_WINDOW_BIT;
    attribs[i++] = EGL_RENDERABLE_TYPE;
    attribs[i++] = (opts.gl_major >= 3)
                        ? EGL_OPENGL_ES3_BIT
                        : EGL_OPENGL_ES2_BIT;
    attribs[i++] = EGL_RED_SIZE;        attribs[i++] = opts.red_bits;
    attribs[i++] = EGL_GREEN_SIZE;      attribs[i++] = opts.green_bits;
    attribs[i++] = EGL_BLUE_SIZE;       attribs[i++] = opts.blue_bits;
    // Android's default framebuffer is RGBX (no alpha). Requesting
    // alpha=8 gives "FBO incomplete" on Adreno.
    attribs[i++] = EGL_ALPHA_SIZE;      attribs[i++] = 0;
    attribs[i++] = EGL_DEPTH_SIZE;      attribs[i++] = opts.depth_bits;
    attribs[i++] = EGL_STENCIL_SIZE;    attribs[i++] = opts.stencil_bits;
    if (opts.msaa) {
        attribs[i++] = EGL_SAMPLE_BUFFERS; attribs[i++] = 1;
        attribs[i++] = EGL_SAMPLES; attribs[i++] = opts.msaa_samples;
    }
    attribs[i++] = EGL_NONE;

    EGLint num_configs = 0;
    if (!eglChooseConfig(s_egl_display, attribs, &s_egl_config, 1, &num_configs)
        || num_configs == 0) {
        eglTerminate(s_egl_display);
        s_egl_display = EGL_NO_DISPLAY;
        return false;
    }

    s_options     = opts;
    s_initialized = true;
    return true;
}

inline void knst_opengl_global::CleanUp() noexcept {
    if (s_egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglTerminate(s_egl_display);
        s_egl_display = EGL_NO_DISPLAY;
        s_egl_config  = nullptr;
    }
    s_initialized = false;
}

#endif // Android


namespace knst_gl_detail {

// build_egl_context_attribs — single source of truth for EGL context
// attributes. Previously Init_x11 / Init_wayland defaulted to CORE when
// core_profile == false, because the profile mask was only set in the
// core path. Now all callers use this helper, behaviour is identical.
//
// Only exists on EGL platforms — Windows uses WGL and has no EGL
// headers, so the compiler would choke on EGL_CONTEXT_* / EGL_TRUE here.
#if !KNST_USING_PLATFORM_WINDOWS
inline int build_egl_context_attribs(
    const knst_opengl_global::Options& opts,
    EGLint* out) noexcept
{
    int i = 0;

    if (opts.client_api == knst_opengl_global::ClientAPI::OpenGLES) {
        out[i++] = EGL_CONTEXT_CLIENT_VERSION;
        out[i++] = (opts.gl_major >= 3) ? 3 : 2;
    } else {
        out[i++] = EGL_CONTEXT_MAJOR_VERSION; out[i++] = opts.gl_major;
        out[i++] = EGL_CONTEXT_MINOR_VERSION; out[i++] = opts.gl_minor;

        // Always specify the profile mask. Compat must be explicit.
        out[i++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
        out[i++] = opts.core_profile
                    ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
                    : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT;

        if (opts.forward_compat) {
            out[i++] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
            out[i++] = EGL_TRUE;
        }
        if (opts.debug_context) {
            out[i++] = EGL_CONTEXT_OPENGL_DEBUG;
            out[i++] = EGL_TRUE;
        }
        if (opts.robust_access) {
            out[i++] = EGL_CONTEXT_OPENGL_ROBUST_ACCESS;
            out[i++] = EGL_TRUE;
        }
        if (opts.robustness != knst_opengl_global::Robustness::Off) {
            out[i++] = EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY;
            out[i++] = (opts.robustness == knst_opengl_global::Robustness::Reset)
                        ? EGL_LOSE_CONTEXT_ON_RESET
                        : EGL_NO_RESET_NOTIFICATION;
        }
    }
    out[i++] = EGL_NONE;
    return i;
}
#endif  // !KNST_USING_PLATFORM_WINDOWS

// GLAD-safe glGetString.
//
// glad.h does `#define glGetString glad_glGetString` before this header
// is included. glad_glGetString is NULL until gladLoadGLLoader() runs.
// Since Init() runs BEFORE that, we can't call glGetString directly —
// resolve it via the platform loader instead.
inline const char* gl_query_string(unsigned int gl_enum) noexcept {
    typedef const unsigned char* (*PFN_GLGETSTRING)(unsigned int);

    PFN_GLGETSTRING fn = (PFN_GLGETSTRING)
        knst_window_opengl_content::LoadGLProc("glGetString");

    if (!fn) return "unknown";

    const unsigned char* s = fn(gl_enum);
    return s ? reinterpret_cast<const char*>(s) : "unknown";
}

inline void read_context_info(
    const char*& vendor, const char*& renderer,
    const char*& version, const char*& glsl_version,
    int& out_major, int& out_minor,
    const knst_opengl_global::Options& opts) noexcept {

    constexpr unsigned int GL_VENDOR_ENUM = 0x1F00;
    constexpr unsigned int GL_RENDERER_ENUM  = 0x1F01;
    constexpr unsigned int GL_VERSION_ENUM = 0x1F02;
    constexpr unsigned int GL_SHADING_LANGUAGE_VERSION_ENUM = 0x8B8C;

    vendor = gl_query_string(GL_VENDOR_ENUM);
    renderer = gl_query_string(GL_RENDERER_ENUM);
    version = gl_query_string(GL_VERSION_ENUM);
    glsl_version = gl_query_string(GL_SHADING_LANGUAGE_VERSION_ENUM);

    int a = 0, b = 0;
    if (std::sscanf(version, "%d.%d", &a, &b) == 2) {
        out_major = a;
        out_minor = b;
    }
    if (out_major == 0) {
        out_major = opts.gl_major;
        out_minor = opts.gl_minor;
    }
}

} // namespace knst_gl_detail


// ─── Move assignment ───────────────────────────────────────────────────────

inline knst_window_opengl_content&
knst_window_opengl_content::operator=(knst_window_opengl_content&& other) noexcept {
    if (this == &other) return *this;

    // Unregister from other's old window FIRST — otherwise that window
    // holds a dangling pointer after the move.
    if (other.m_window) {
        other.m_window->unregister_surface_client(&other);
    }

    // Then drop our own old registration.
    if (m_window) {
        m_window->unregister_surface_client(this);
    }

    Shutdown();

    m_window = other.m_window;
    m_valid = other.m_valid;
    m_width = other.m_width;
    m_height = other.m_height;
    m_vendor = other.m_vendor;
    m_renderer = other.m_renderer;
    m_version = other.m_version;
    m_glsl_version = other.m_glsl_version;
    m_actual_major = other.m_actual_major;
    m_actual_minor = other.m_actual_minor;
    m_client_api = other.m_client_api;

    m_user_vsync  = other.m_user_vsync;
    m_current_swap_interval = other.m_current_swap_interval;
    m_last_resize_time = other.m_last_resize_time;
    m_resize_settle_ms = other.m_resize_settle_ms;

#if KNST_USING_PLATFORM_WINDOWS
    m_hdc = other.m_hdc;
    m_hglrc = other.m_hglrc;
    m_pixel_format = other.m_pixel_format;
    m_hwnd = other.m_hwnd;
    other.m_hdc = nullptr;
    other.m_hglrc = nullptr;
    other.m_pixel_format = 0;
    other.m_hwnd = nullptr;
#endif

// Must be an independent #if (not #elif) — otherwise the Wayland members
// aren't moved when X11 is the active compile-time backend.
#if KNST_USING_LINUX_PLATFORM_X11 || KNST_USING_LINUX_PLATFORM_WAYLAND \
    || defined(KNST_USING_PLATFORM_ANDROID)
    m_egl_surface = other.m_egl_surface;
    m_egl_context = other.m_egl_context;
    other.m_egl_surface = EGL_NO_SURFACE;
    other.m_egl_context = EGL_NO_CONTEXT;
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    m_egl_window = other.m_egl_window;
    other.m_egl_window = nullptr;
#endif

    other.m_window = nullptr;
    other.m_valid  = false;

    // Register with the new window (after Shutdown()).
    if (m_window && m_valid) {
        m_window->register_surface_client(this);
    }

    return *this;
}


// ─── apply_swap_interval_raw ───────────────────────────────────────────────
// Low-level: sends the interval to the platform driver.
inline void knst_window_opengl_content::apply_swap_interval_raw(int interval) noexcept {
    if (!m_valid) return;

    // eglSwapInterval / wglSwapIntervalEXT act on the currently current
    // context/surface. Make sure ours is current.
    MakeCurrent();

#if KNST_USING_PLATFORM_WINDOWS
    if (knst_gl_detail::wglSwapIntervalEXT) {
        knst_gl_detail::wglSwapIntervalEXT(interval);
    }
#else
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    if (dpy != EGL_NO_DISPLAY) {
        // EGL doesn't support adaptive vsync (-1) — clamp to 0.
        if (interval < 0) interval = 0;
        eglSwapInterval(dpy, interval);
    }
#endif

    m_current_swap_interval = interval;
}


// ─── SetSwapInterval ───────────────────────────────────────────────────────
inline void knst_window_opengl_content::SetSwapInterval(int interval) noexcept {
    // Remember the preference so we can restore it after resize.
    m_user_vsync = interval;

    // During a resize burst, don't apply now — SwapBuffers() will
    // restore the new value once resize settles.
    if (m_last_resize_time != 0 &&
        KnstWindowSources::get_current_time_ms() - m_last_resize_time
            < m_resize_settle_ms) {
        return;
    }

    apply_swap_interval_raw(interval);
}


// ─── maybe_restore_vsync ───────────────────────────────────────────────────
// Called from SwapBuffers(). Restores the user's preference once resize
// has settled.
inline void knst_window_opengl_content::maybe_restore_vsync() noexcept {
    if (m_last_resize_time == 0) return;
    if (m_current_swap_interval == m_user_vsync) return;

    uint32_t now = KnstWindowSources::get_current_time_ms();
    if (now - m_last_resize_time >= m_resize_settle_ms) {
        apply_swap_interval_raw(m_user_vsync);
        m_last_resize_time = 0;
    }
}


// ─── Init ──────────────────────────────────────────────────────────────────

inline bool knst_window_opengl_content::Init(
    knst_window* window, bool reuse_old) noexcept {

    if (!window) return false;
    if (!knst_opengl_global::IsInitialized()) return false;

    if (m_valid && m_window == window && reuse_old) return true;
    if (m_valid) Shutdown();

    // eglBindAPI is thread-local. If this Init runs on a different
    // thread from knst_opengl_global::Init, the API binding is lost and
    // the default (ES) is used -> wrong context profile.
#if !KNST_USING_PLATFORM_WINDOWS
    {
        EGLDisplay _dpy = knst_opengl_global::GetEGLDisplay();
        if (_dpy != EGL_NO_DISPLAY) {
            EGLint _api = (knst_opengl_global::GetOptions().client_api ==
                           knst_opengl_global::ClientAPI::OpenGLES)
                            ? EGL_OPENGL_ES_API
                            : EGL_OPENGL_API;
            eglBindAPI(_api);
        }
    }
#endif

    // m_window is set ONLY after everything succeeds. If we set it here
    // and a later `return false` fires, the window's dtor would call
    // unregister_surface_client on a window we never registered with —
    // potential UAF.
    m_width = window->m_knst_event.window_width;
    m_height = window->m_knst_event.window_height;
    if (m_width  <= 0) m_width  = 800;
    if (m_height <= 0) m_height = 600;

    const auto& opts = knst_opengl_global::GetOptions();
    m_client_api = opts.client_api;

    // Vsync preference from options (default off).
    m_user_vsync = opts.vsync ? 1 : 0;
    m_current_swap_interval = m_user_vsync;

    // Reset resize tracking (in case of re-init).
    m_last_resize_time = 0;
    m_resize_settle_ms = DEFAULT_RESIZE_SETTLE_MS;

// ─────────────────────────────────────────────────────────────────────────
// Windows — WGL
// ─────────────────────────────────────────────────────────────────────────
#if KNST_USING_PLATFORM_WINDOWS

    m_hwnd = window->get_windows_window_handle();
    if (!m_hwnd) return false;

    m_hdc = GetDC(m_hwnd);
    if (!m_hdc) return false;

    if (knst_gl_detail::wglChoosePixelFormatARB) {
        // 64 ints are plenty for accum + aux + srgb + msaa + core +
        // terminator. The old code used 40 and overflowed.
        int pixel_attribs[64];
        int i = 0;
        pixel_attribs[i++] = WGL_DRAW_TO_WINDOW_ARB; pixel_attribs[i++] = GL_TRUE;
        pixel_attribs[i++] = WGL_SUPPORT_OPENGL_ARB; pixel_attribs[i++] = GL_TRUE;
        pixel_attribs[i++] = WGL_DOUBLE_BUFFER_ARB; pixel_attribs[i++] = opts.double_buffer ? GL_TRUE : GL_FALSE;
        pixel_attribs[i++] = WGL_STEREO_ARB; pixel_attribs[i++] = opts.stereo ? GL_TRUE : GL_FALSE;
        pixel_attribs[i++] = WGL_PIXEL_TYPE_ARB; pixel_attribs[i++] = WGL_TYPE_RGBA_ARB;
        pixel_attribs[i++] = WGL_ACCELERATION_ARB; pixel_attribs[i++] = WGL_FULL_ACCELERATION_ARB;
        pixel_attribs[i++] = WGL_RED_BITS_ARB; pixel_attribs[i++] = opts.red_bits;
        pixel_attribs[i++] = WGL_GREEN_BITS_ARB; pixel_attribs[i++] = opts.green_bits;
        pixel_attribs[i++] = WGL_BLUE_BITS_ARB; pixel_attribs[i++] = opts.blue_bits;
        pixel_attribs[i++] = WGL_ALPHA_BITS_ARB; pixel_attribs[i++] = opts.alpha_bits;
        pixel_attribs[i++] = WGL_DEPTH_BITS_ARB; pixel_attribs[i++] = opts.depth_bits;
        pixel_attribs[i++] = WGL_STENCIL_BITS_ARB; pixel_attribs[i++] = opts.stencil_bits;

        if (opts.accum_red_bits > 0) {
            pixel_attribs[i++] = WGL_ACCUM_RED_BITS_ARB;   pixel_attribs[i++] = opts.accum_red_bits;
            pixel_attribs[i++] = WGL_ACCUM_GREEN_BITS_ARB; pixel_attribs[i++] = opts.accum_green_bits;
            pixel_attribs[i++] = WGL_ACCUM_BLUE_BITS_ARB;  pixel_attribs[i++] = opts.accum_blue_bits;
            pixel_attribs[i++] = WGL_ACCUM_ALPHA_BITS_ARB; pixel_attribs[i++] = opts.accum_alpha_bits;
        }
        if (opts.aux_buffers > 0) {
            pixel_attribs[i++] = WGL_AUX_BUFFERS_ARB; pixel_attribs[i++] = opts.aux_buffers;
        }
        if (opts.srgb) {
            pixel_attribs[i++] = WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB; pixel_attribs[i++] = GL_TRUE;
        }

        pixel_attribs[i++] = WGL_SAMPLE_BUFFERS_ARB;
        pixel_attribs[i++] = opts.msaa ? 1 : 0;
        pixel_attribs[i++] = WGL_SAMPLES_ARB;
        pixel_attribs[i++] = opts.msaa ? opts.msaa_samples : 0;

        pixel_attribs[i++] = 0;

        UINT num_formats = 0;
        if (!knst_gl_detail::wglChoosePixelFormatARB(
                m_hdc, pixel_attribs, nullptr, 1, &m_pixel_format, &num_formats)
            || num_formats == 0) {
            // Retry without MSAA/sRGB. Many Intel/AMD iGPUs can't do
            // 4x MSAA + sRGB at the exact bit depths we asked for.

            for (int k = 0; k + 1 < i; ) {
                if (pixel_attribs[k] == WGL_SAMPLE_BUFFERS_ARB) {
                    pixel_attribs[k + 1] = 0;
                    k += 2;
                } else if (pixel_attribs[k] == WGL_SAMPLES_ARB) {
                    pixel_attribs[k + 1] = 0;
                    k += 2;
                } else if (pixel_attribs[k] == WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB) {
                    pixel_attribs[k + 1] = GL_FALSE;
                    k += 2;
                } else {
                    k += 2;
                }
            }

            num_formats = 0;
            if (!knst_gl_detail::wglChoosePixelFormatARB(  m_hdc, pixel_attribs, nullptr, 1, &m_pixel_format, &num_formats)  || num_formats == 0) {
                   
                ReleaseDC(m_hwnd, m_hdc);
                m_hdc = nullptr;
                return false;
            }
        }
    } else {
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize      = sizeof(pfd);
        pfd.nVersion   = 1;
        pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL
                       | (opts.double_buffer ? PFD_DOUBLEBUFFER : 0)
                       | (opts.stereo ? PFD_STEREO : 0);
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = opts.red_bits + opts.green_bits + opts.blue_bits;
        pfd.cAlphaBits = opts.alpha_bits;
        pfd.cDepthBits = opts.depth_bits;
        pfd.cStencilBits = opts.stencil_bits;

        m_pixel_format = ChoosePixelFormat(m_hdc, &pfd);
        if (!m_pixel_format) {
            ReleaseDC(m_hwnd, m_hdc);
            m_hdc = nullptr;
            return false;
        }
    }

    PIXELFORMATDESCRIPTOR pfd_check = {};
    DescribePixelFormat(m_hdc, m_pixel_format, sizeof(pfd_check), &pfd_check);

    // CS_OWNDC pins the pixel format to the window permanently. A second
    // Init() on the same window would fail at SetPixelFormat
    // (ERROR_PIXELFORMAT_ALREADY_SET). Reuse it if already set.
    const int already_set = GetPixelFormat(m_hdc);
    if (already_set != 0) {
        m_pixel_format = already_set;
    } else if (!SetPixelFormat(m_hdc, m_pixel_format, &pfd_check)) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    if (knst_gl_detail::wglCreateContextAttribsARB) {
        int ctx_attribs[32];
        int i = 0;
        ctx_attribs[i++] = WGL_CONTEXT_MAJOR_VERSION_ARB; ctx_attribs[i++] = opts.gl_major;
        ctx_attribs[i++] = WGL_CONTEXT_MINOR_VERSION_ARB; ctx_attribs[i++] = opts.gl_minor;

        int profile = opts.core_profile ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
        ctx_attribs[i++] = WGL_CONTEXT_PROFILE_MASK_ARB; ctx_attribs[i++] = profile;

        int flags = 0;
        if (opts.debug_context)  flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
        if (opts.forward_compat) flags |= WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
        if (opts.robust_access)  flags |= WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB;
        if (flags) {
            ctx_attribs[i++] = WGL_CONTEXT_FLAGS_ARB; ctx_attribs[i++] = flags;
        }

        if (opts.robustness != knst_opengl_global::Robustness::Off) {
            int v = (opts.robustness == knst_opengl_global::Robustness::Reset)
                        ? WGL_LOSE_CONTEXT_ON_RESET_ARB
                        : WGL_NO_RESET_NOTIFICATION_ARB;
            ctx_attribs[i++] = WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB;
            ctx_attribs[i++] = v;
        }

        if (opts.release_behavior == knst_opengl_global::ReleaseBehavior::NoFlush) {
            ctx_attribs[i++] = WGL_CONTEXT_RELEASE_BEHAVIOR_ARB;
            ctx_attribs[i++] = WGL_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB;
        }

        ctx_attribs[i++] = 0;

        m_hglrc = knst_gl_detail::wglCreateContextAttribsARB(m_hdc, nullptr, ctx_attribs);

        if (!m_hglrc) {
            int fallback[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 2,
                WGL_CONTEXT_MINOR_VERSION_ARB, 1,
                WGL_CONTEXT_PROFILE_MASK_ARB,
                WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                0
            };
            m_hglrc = knst_gl_detail::wglCreateContextAttribsARB(m_hdc, nullptr, fallback);
        }
    } else {
        m_hglrc = wglCreateContext(m_hdc);
    }

    if (!m_hglrc) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        wglDeleteContext(m_hglrc);
        ReleaseDC(m_hwnd, m_hdc);
        m_hglrc = nullptr;
        m_hdc = nullptr;
        return false;
    }

    m_valid = true;

    // Apply the user's vsync preference.
    if (knst_gl_detail::wglSwapIntervalEXT) {
        knst_gl_detail::wglSwapIntervalEXT(m_user_vsync);
        m_current_swap_interval = m_user_vsync;
    }

// ─────────────────────────────────────────────────────────────────────────
// X11 — EGL
// ─────────────────────────────────────────────────────────────────────────
#elif KNST_USING_LINUX_PLATFORM_X11
#if KNST_IS_DUAL_BACKEND
    if (KnstWindowSources::s_active_backend != knst_backend::x11) goto knst_skip_x11_gl_init;
#endif
    {
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    EGLConfig  cfg = knst_opengl_global::GetEGLConfig();
    if (dpy == EGL_NO_DISPLAY || !cfg) return false;

    xcb_window_t xcb_win = window->get_x11_window_handle();
    if (!xcb_win) return false;

    EGLNativeWindowType native_win = (EGLNativeWindowType)(uintptr_t)xcb_win;

    m_egl_surface = eglCreateWindowSurface(dpy, cfg, native_win, nullptr);
    if (m_egl_surface == EGL_NO_SURFACE) return false;

    EGLint ctx_attribs[32];
    knst_gl_detail::build_egl_context_attribs(opts, ctx_attribs);

    m_egl_context = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (m_egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        return false;
    }

    if (!eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroyContext(dpy, m_egl_context);
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_context = EGL_NO_CONTEXT;
        m_egl_surface = EGL_NO_SURFACE;
        return false;
    }

    eglSwapInterval(dpy, m_user_vsync);
    m_current_swap_interval = m_user_vsync;
    m_valid = true;
    }
#if KNST_IS_DUAL_BACKEND
    knst_skip_x11_gl_init:;
#endif
#endif   // X11 block ends here

// ─────────────────────────────────────────────────────────────────────────
// Wayland — EGL
// ─────────────────────────────────────────────────────────────────────────
#if KNST_USING_LINUX_PLATFORM_WAYLAND
#if KNST_IS_DUAL_BACKEND
    if (KnstWindowSources::s_active_backend != knst_backend::wayland) goto knst_skip_wayland_gl_init;
#endif
    {
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    EGLConfig  cfg = knst_opengl_global::GetEGLConfig();
    if (dpy == EGL_NO_DISPLAY || !cfg) return false;

    wl_surface* wl_surf = const_cast<wl_surface*>(window->get_wayland_surface_handle());
    if (!wl_surf) return false;

    // Physical size = logical * fractional scale.
    const int phys_w = window->wayland_physical_width(m_width);
    const int phys_h = window->wayland_physical_height(m_height);

    m_egl_window = wl_egl_window_create(wl_surf, phys_w, phys_h);
    if (!m_egl_window) return false;

    EGLint surf_attribs[4];
    int si = 0;
    if (opts.srgb) {
        surf_attribs[si++] = EGL_GL_COLORSPACE;
        surf_attribs[si++] = EGL_GL_COLORSPACE_SRGB;
    }
    surf_attribs[si++] = EGL_NONE;

    m_egl_surface = eglCreateWindowSurface(
        dpy, cfg, (EGLNativeWindowType)m_egl_window,
        si > 1 ? surf_attribs : nullptr);
    if (m_egl_surface == EGL_NO_SURFACE) {
        m_egl_surface = eglCreateWindowSurface(
            dpy, cfg, (EGLNativeWindowType)m_egl_window, nullptr);
        if (m_egl_surface == EGL_NO_SURFACE) {
            wl_egl_window_destroy(m_egl_window);
            m_egl_window = nullptr;
            return false;
        }
    }

    EGLint ctx_attribs[32];
    int i = 0;
    if (opts.client_api == knst_opengl_global::ClientAPI::OpenGLES) {
        ctx_attribs[i++] = EGL_CONTEXT_CLIENT_VERSION;
        ctx_attribs[i++] = (opts.gl_major >= 3) ? 3 : 2;
    } else {
        ctx_attribs[i++] = EGL_CONTEXT_MAJOR_VERSION; ctx_attribs[i++] = opts.gl_major;
        ctx_attribs[i++] = EGL_CONTEXT_MINOR_VERSION; ctx_attribs[i++] = opts.gl_minor;
        if (opts.core_profile) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
        }
        if (opts.forward_compat) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
            ctx_attribs[i++] = EGL_TRUE;
        }
        if (opts.debug_context) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_DEBUG;
            ctx_attribs[i++] = EGL_TRUE;
        }
        if (opts.robust_access) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_ROBUST_ACCESS;
            ctx_attribs[i++] = EGL_TRUE;
        }
        if (opts.robustness != knst_opengl_global::Robustness::Off) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY;
            ctx_attribs[i++] = (opts.robustness == knst_opengl_global::Robustness::Reset)
                                    ? EGL_LOSE_CONTEXT_ON_RESET
                                    : EGL_NO_RESET_NOTIFICATION;
        }
    }
    ctx_attribs[i++] = EGL_NONE;

    m_egl_context = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);

    if (m_egl_context == EGL_NO_CONTEXT) {
        EGLint fallback[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        m_egl_context = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, fallback);
    }

    if (m_egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(dpy, m_egl_surface);
        wl_egl_window_destroy(m_egl_window);
        m_egl_surface = EGL_NO_SURFACE;
        m_egl_window = nullptr;
        return false;
    }

    if (!eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroyContext(dpy, m_egl_context);
        eglDestroySurface(dpy, m_egl_surface);
        wl_egl_window_destroy(m_egl_window);
        m_egl_context = EGL_NO_CONTEXT;
        m_egl_surface = EGL_NO_SURFACE;
        m_egl_window = nullptr;
        return false;
    }

    eglSwapInterval(dpy, m_user_vsync);
    m_current_swap_interval = m_user_vsync;
    m_valid = true;
    }
#if KNST_IS_DUAL_BACKEND
    knst_skip_wayland_gl_init:;
#endif
#endif   // Wayland block ends here

// ─────────────────────────────────────────────────────────────────────────
// Android — EGL
// ─────────────────────────────────────────────────────────────────────────
#if defined(KNST_USING_PLATFORM_ANDROID)

    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    EGLConfig  cfg = knst_opengl_global::GetEGLConfig();
    if (dpy == EGL_NO_DISPLAY || !cfg) return false;

    android_app* android_app_p = KnstWindowSources::get_android_app();
    if (!android_app_p || !android_app_p->window) return false;

    m_egl_surface = eglCreateWindowSurface(
        dpy, cfg, (EGLNativeWindowType)android_app_p->window, nullptr);
    if (m_egl_surface == EGL_NO_SURFACE) return false;

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, (opts.gl_major >= 3) ? 3 : 2,
        EGL_NONE
    };

    m_egl_context = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (m_egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        return false;
    }

    if (!eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroyContext(dpy, m_egl_context);
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_context = EGL_NO_CONTEXT;
        m_egl_surface = EGL_NO_SURFACE;
        return false;
    }

    eglSwapInterval(dpy, m_user_vsync);
    m_current_swap_interval = m_user_vsync;
    m_valid = true;

#endif

    // Read context info — GLAD-safe (uses the platform loader).
    if (m_valid) {
        knst_gl_detail::read_context_info(
            m_vendor, m_renderer, m_version, m_glsl_version,
            m_actual_major, m_actual_minor, opts);

        // Only now set m_window — see comment at the top of Init.
        m_window = window;
        window->register_surface_client(this);
    }

    return m_valid;
}


// ─── InitShared ────────────────────────────────────────────────────────────

inline bool knst_window_opengl_content::InitShared(
    knst_window* window, const knst_window_opengl_content& share) noexcept {

    if (!window || !share.IsValid()) return false;
    if (!knst_opengl_global::IsInitialized()) return false;

    if (m_valid) Shutdown();

    // eglBindAPI is thread-local (see Init).
#if !KNST_USING_PLATFORM_WINDOWS
    {
        EGLDisplay _dpy = knst_opengl_global::GetEGLDisplay();
        if (_dpy != EGL_NO_DISPLAY) {
            EGLint _api = (knst_opengl_global::GetOptions().client_api ==
                           knst_opengl_global::ClientAPI::OpenGLES)
                            ? EGL_OPENGL_ES_API
                            : EGL_OPENGL_API;
            eglBindAPI(_api);
        }
    }
#endif

    // Deferred m_window assignment — see Init().
    m_width = window->m_knst_event.window_width;
    m_height = window->m_knst_event.window_height;
    if (m_width  <= 0) m_width  = 800;
    if (m_height <= 0) m_height = 600;

    const auto& opts = knst_opengl_global::GetOptions();
    m_client_api = opts.client_api;

    // Inherit the vsync preference from options, not from share.
    m_user_vsync = opts.vsync ? 1 : 0;
    m_current_swap_interval = m_user_vsync;

    m_last_resize_time = 0;
    m_resize_settle_ms = DEFAULT_RESIZE_SETTLE_MS;

#if KNST_USING_PLATFORM_WINDOWS
    m_hwnd = window->get_windows_window_handle();
    if (!m_hwnd) return false;
    m_hdc = GetDC(m_hwnd);
    if (!m_hdc) return false;

    m_pixel_format = share.m_pixel_format;
    PIXELFORMATDESCRIPTOR pfd = {};
    DescribePixelFormat(m_hdc, m_pixel_format, sizeof(pfd), &pfd);

    // CS_OWNDC: a second SetPixelFormat fails.
    const int already_set = GetPixelFormat(m_hdc);
    if (already_set != 0) {
        m_pixel_format = already_set;
    } else if (!SetPixelFormat(m_hdc, m_pixel_format, &pfd)) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    if (knst_gl_detail::wglCreateContextAttribsARB) {
        int ctx_attribs[24];
        int i = 0;
        ctx_attribs[i++] = WGL_CONTEXT_MAJOR_VERSION_ARB; ctx_attribs[i++] = opts.gl_major;
        ctx_attribs[i++] = WGL_CONTEXT_MINOR_VERSION_ARB; ctx_attribs[i++] = opts.gl_minor;
        ctx_attribs[i++] = WGL_CONTEXT_PROFILE_MASK_ARB;
        ctx_attribs[i++] = opts.core_profile
                            ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB
                            : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;

        // Same flags as the shared context.
        int flags = 0;
        if (opts.debug_context)  flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
        if (opts.forward_compat) flags |= WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
        if (opts.robust_access)  flags |= WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB;
        if (flags) {
            ctx_attribs[i++] = WGL_CONTEXT_FLAGS_ARB;
            ctx_attribs[i++] = flags;
        }
        ctx_attribs[i++] = 0;

        m_hglrc = knst_gl_detail::wglCreateContextAttribsARB(
            m_hdc, share.m_hglrc, ctx_attribs);
    } else {
        m_hglrc = wglCreateContext(m_hdc);
        if (m_hglrc) wglShareLists(share.m_hglrc, m_hglrc);
    }

    if (!m_hglrc) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
        return false;
    }

    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        wglDeleteContext(m_hglrc);
        ReleaseDC(m_hwnd, m_hdc);
        m_hglrc = nullptr; m_hdc = nullptr;
        return false;
    }

    if (knst_gl_detail::wglSwapIntervalEXT) {
        knst_gl_detail::wglSwapIntervalEXT(m_user_vsync);
    }
    m_current_swap_interval = m_user_vsync;
    m_valid = true;

#elif KNST_USING_LINUX_PLATFORM_X11
#if KNST_IS_DUAL_BACKEND
    if (KnstWindowSources::s_active_backend != knst_backend::x11) goto knst_skip_x11_gl_shared;
#endif
    {
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    EGLConfig  cfg = knst_opengl_global::GetEGLConfig();
    if (dpy == EGL_NO_DISPLAY || !cfg) return false;

    xcb_window_t xcb_win = window->get_x11_window_handle();
    if (!xcb_win) return false;

    m_egl_surface = eglCreateWindowSurface(
        dpy, cfg, (EGLNativeWindowType)(uintptr_t)xcb_win, nullptr);
    if (m_egl_surface == EGL_NO_SURFACE) return false;

    EGLint ctx_attribs[32];
    int i = 0;
    if (opts.client_api == knst_opengl_global::ClientAPI::OpenGLES) {
        ctx_attribs[i++] = EGL_CONTEXT_CLIENT_VERSION;
        ctx_attribs[i++] = (opts.gl_major >= 3) ? 3 : 2;
    } else {
        ctx_attribs[i++] = EGL_CONTEXT_MAJOR_VERSION; ctx_attribs[i++] = opts.gl_major;
        ctx_attribs[i++] = EGL_CONTEXT_MINOR_VERSION; ctx_attribs[i++] = opts.gl_minor;
        // EGL defaults to core. Compat must be explicit.
        ctx_attribs[i++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
        ctx_attribs[i++] = opts.core_profile
                                ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
                                : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT;
        if (opts.forward_compat) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
            ctx_attribs[i++] = EGL_TRUE;
        }
        // Matching flags are required by some drivers (Mesa, NVIDIA)
        // for context sharing.
        if (opts.debug_context) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_DEBUG;
            ctx_attribs[i++] = EGL_TRUE;
        }
        if (opts.robust_access) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_ROBUST_ACCESS;
            ctx_attribs[i++] = EGL_TRUE;
        }
    }
    ctx_attribs[i++] = EGL_NONE;

    m_egl_context = eglCreateContext(dpy, cfg, share.m_egl_context, ctx_attribs);

    if (m_egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        return false;
    }

    if (!eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroyContext(dpy, m_egl_context);
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_context = EGL_NO_CONTEXT;
        m_egl_surface = EGL_NO_SURFACE;
        return false;
    }

    eglSwapInterval(dpy, m_user_vsync);
    m_current_swap_interval = m_user_vsync;
    m_valid = true;
    }
#if KNST_IS_DUAL_BACKEND
    knst_skip_x11_gl_shared:;
#endif
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
#if KNST_IS_DUAL_BACKEND
    if (KnstWindowSources::s_active_backend != knst_backend::wayland) goto knst_skip_wayland_gl_shared;
#endif
    {
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    EGLConfig  cfg = knst_opengl_global::GetEGLConfig();
    if (dpy == EGL_NO_DISPLAY || !cfg) return false;

    wl_surface* wl_surf = const_cast<wl_surface*>(window->get_wayland_surface_handle());
    if (!wl_surf) return false;

    const int phys_w = window->wayland_physical_width(m_width);
    const int phys_h = window->wayland_physical_height(m_height);

    m_egl_window = wl_egl_window_create(wl_surf, phys_w, phys_h);
    if (!m_egl_window) return false;

    m_egl_surface = eglCreateWindowSurface(
        dpy, cfg, (EGLNativeWindowType)m_egl_window, nullptr);
    if (m_egl_surface == EGL_NO_SURFACE) {
        wl_egl_window_destroy(m_egl_window);
        m_egl_window = nullptr;
        return false;
    }

    EGLint ctx_attribs[32];
    int i = 0;
    if (opts.client_api == knst_opengl_global::ClientAPI::OpenGLES) {
        ctx_attribs[i++] = EGL_CONTEXT_CLIENT_VERSION;
        ctx_attribs[i++] = (opts.gl_major >= 3) ? 3 : 2;
    } else {
        ctx_attribs[i++] = EGL_CONTEXT_MAJOR_VERSION; ctx_attribs[i++] = opts.gl_major;
        ctx_attribs[i++] = EGL_CONTEXT_MINOR_VERSION; ctx_attribs[i++] = opts.gl_minor;
        ctx_attribs[i++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
        ctx_attribs[i++] = opts.core_profile
                                ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
                                : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT;
        if (opts.forward_compat) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
            ctx_attribs[i++] = EGL_TRUE;
        }
        // Matching flags are required by some drivers for context sharing.
        if (opts.debug_context) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_DEBUG;
            ctx_attribs[i++] = EGL_TRUE;
        }
        if (opts.robust_access) {
            ctx_attribs[i++] = EGL_CONTEXT_OPENGL_ROBUST_ACCESS;
            ctx_attribs[i++] = EGL_TRUE;
        }
    }
    ctx_attribs[i++] = EGL_NONE;

    m_egl_context = eglCreateContext(dpy, cfg, share.m_egl_context, ctx_attribs);

    if (m_egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(dpy, m_egl_surface);
        wl_egl_window_destroy(m_egl_window);
        m_egl_surface = EGL_NO_SURFACE;
        m_egl_window = nullptr;
        return false;
    }

    if (!eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroyContext(dpy, m_egl_context);
        eglDestroySurface(dpy, m_egl_surface);
        wl_egl_window_destroy(m_egl_window);
        m_egl_context = EGL_NO_CONTEXT;
        m_egl_surface = EGL_NO_SURFACE;
        m_egl_window = nullptr;
        return false;
    }

    eglSwapInterval(dpy, m_user_vsync);
    m_current_swap_interval = m_user_vsync;
    m_valid = true;
    }
#if KNST_IS_DUAL_BACKEND
    knst_skip_wayland_gl_shared:;
#endif
#endif

#if defined(KNST_USING_PLATFORM_ANDROID)
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    EGLConfig  cfg = knst_opengl_global::GetEGLConfig();
    if (dpy == EGL_NO_DISPLAY || !cfg) return false;

    android_app* android_app_p = KnstWindowSources::get_android_app();
    if (!android_app_p || !android_app_p->window) return false;

    m_egl_surface = eglCreateWindowSurface(
        dpy, cfg, (EGLNativeWindowType)android_app_p->window, nullptr);
    if (m_egl_surface == EGL_NO_SURFACE) return false;

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, (opts.gl_major >= 3) ? 3 : 2,
        EGL_NONE
    };
    m_egl_context = eglCreateContext(dpy, cfg, share.m_egl_context, ctx_attribs);

    if (m_egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        return false;
    }

    if (!eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroyContext(dpy, m_egl_context);
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_context = EGL_NO_CONTEXT;
        m_egl_surface = EGL_NO_SURFACE;
        return false;
    }

    eglSwapInterval(dpy, m_user_vsync);
    m_current_swap_interval = m_user_vsync;
    m_valid = true;
#endif

    if (m_valid) {
        knst_gl_detail::read_context_info(
            m_vendor, m_renderer, m_version, m_glsl_version,
            m_actual_major, m_actual_minor, opts);

        // Only now set m_window — see Init().
        m_window = window;
        window->register_surface_client(this);
    }

    return m_valid;
}


// ─── Shutdown ──────────────────────────────────────────────────────────────

inline void knst_window_opengl_content::Shutdown() noexcept {
    // Unregister FIRST — the window may be shutting down or we may be
    // switching to another context. Otherwise a dangling pointer stays.
    if (m_window) {
        m_window->unregister_surface_client(this);
    }

    if (!m_valid) {
        m_window = nullptr;
        return;
    }

#if KNST_USING_PLATFORM_WINDOWS
    if (m_hglrc) {
        // Only clear the current context if OURS is current. Otherwise
        // we'd drop another window's active context mid-frame.
        if (wglGetCurrentContext() == m_hglrc) {
            wglMakeCurrent(nullptr, nullptr);
        }
        wglDeleteContext(m_hglrc);
        m_hglrc = nullptr;
    }
    if (m_hdc && m_hwnd) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
    }
    m_hwnd = nullptr;
    m_pixel_format = 0;
#endif

#if KNST_USING_LINUX_PLATFORM_X11 || KNST_USING_LINUX_PLATFORM_WAYLAND \
    || defined(KNST_USING_PLATFORM_ANDROID)
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    if (dpy != EGL_NO_DISPLAY) {
        // Only clear the current context if OURS is current.
        EGLContext current = eglGetCurrentContext();
        if (current == m_egl_context && m_egl_context != EGL_NO_CONTEXT) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (m_egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(dpy, m_egl_context);
            m_egl_context = EGL_NO_CONTEXT;
        }
        if (m_egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(dpy, m_egl_surface);
            m_egl_surface = EGL_NO_SURFACE;
        }
    }
    // The viewport is owned by knst_window and destroyed there.
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    // m_egl_window is Wayland-only. Must be a separate #if — otherwise
    // X11-only builds try to reference wl_egl_window_destroy() which
    // isn't linked.
    if (m_egl_window) {
        wl_egl_window_destroy(m_egl_window);
        m_egl_window = nullptr;
    }
#endif
    m_valid = false;
    m_window = nullptr;
    m_width = 0;
    m_height = 0;
    m_vendor = "unknown";
    m_renderer = "unknown";
    m_version = "unknown";
    m_glsl_version = "unknown";
    m_actual_major = 0;
    m_actual_minor = 0;
}


// ─── MakeCurrent ───────────────────────────────────────────────────────────

inline void knst_window_opengl_content::MakeCurrent() noexcept {
    if (!m_valid) return;

    // Connection lost means the context is already invalid -> no-op.
    if (m_window && m_window->is_disconnected()) {
        return;
    }

#if KNST_USING_PLATFORM_WINDOWS
    if (wglGetCurrentContext() != m_hglrc) {
        wglMakeCurrent(m_hdc, m_hglrc);
    }
#else
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    if (dpy == EGL_NO_DISPLAY) return;

    // Skip if this context is already current (avoids driver overhead).
    if (eglGetCurrentContext() == m_egl_context &&
        eglGetCurrentSurface(EGL_DRAW) == m_egl_surface) {
        return;
    }
    eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context);
#endif
}


// ─── SwapBuffers ───────────────────────────────────────────────────────────

inline void knst_window_opengl_content::SwapBuffers() noexcept {
    if (!m_valid) return;

    // Skip swap if the connection is gone.
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    if (m_window && m_window->is_disconnected()) {
        return;
    }
#endif
    if (m_window && m_window->is_disconnected()) {
        return;
    }

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    if (m_window) {
        // Request a frame callback before SwapBuffers. Without this,
        // eglSwapBuffers blocks forever on NVIDIA Wayland when the
        // window is covered -> connection drops with ECONNRESET.
        // Even if the user forgot to call request_frame_callback(),
        // this preserves correct behavior.
        if (m_window->is_frame_ready()) {
            m_window->request_frame_callback();
        }

        // Wayland requires ack_configure before attaching a buffer.
        m_window->present_pre();
    }
#endif

#if KNST_USING_PLATFORM_WINDOWS
    ::SwapBuffers(m_hdc);
#else
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    if (dpy == EGL_NO_DISPLAY || m_egl_surface == EGL_NO_SURFACE) return;

    // EGL_CONTEXT_LOST: some drivers (especially Adreno) lose the
    // context after a power event or GPU reset, and eglSwapBuffers
    // returns EGL_FALSE.
    //
    // When this happens:
    //   1) Create a new EGLContext (same config)
    //   2) Make it current
    //   3) Set a flag on the window — the user checks
    //      is_gl_context_lost() next frame and rebuilds GL resources.
    EGLBoolean ok = eglSwapBuffers(dpy, m_egl_surface);
    if (ok == EGL_FALSE) {
        const EGLint err = eglGetError();
        if (err == EGL_CONTEXT_LOST) {
            #if defined(KNST_USING_PLATFORM_ANDROID)
                __android_log_print(ANDROID_LOG_ERROR, "KNST",
                    "[EGL] CONTEXT LOST — recovering");
            #else
                std::fprintf(stderr,
                    "[EGL] CONTEXT LOST — recovering\n");
            #endif

            if (recover_context() && m_window) {
                m_window->notify_gl_context_lost();
                #if defined(KNST_USING_PLATFORM_ANDROID)
                    __android_log_print(ANDROID_LOG_INFO, "KNST",
                        "[EGL] recovered — app must rebuild GL resources");
                #else
                    std::fprintf(stderr,
                        "[EGL] recovered — app must rebuild GL resources\n");
                #endif
            }
        }
    }
#endif

    // X11 _NET_WM_SYNC_REQUEST: the WM needs the counter bumped AFTER
    // the new-size frame hits the screen. Without this, resize flickers
    // or freezes when the draw loop bypasses call_redraw_callback().
    // present_post() is idempotent — safe even when
    // call_redraw_callback() already ran it.
#if KNST_USING_LINUX_PLATFORM_X11
    if (m_window) m_window->present_post();
#endif

    // Restore vsync once resize has settled.
    maybe_restore_vsync();
}


// ─── OnResize ──────────────────────────────────────────────────────────────
// Called from the event loop on every resize event.
//
// X11 / Windows / Android: force vsync OFF during resize (WM isn't
// paced, so lower latency = smoother drag).
//
// Wayland: force vsync ON during resize. Compositor pacing via frame
// callbacks; interval 0 lets Mesa commit faster than the compositor can
// keep up -> right-edge sliding.
//
// maybe_restore_vsync() restores the user's preference after the drag
// settles.

inline void knst_window_opengl_content::OnResize(int new_width, int new_height) noexcept {
    if (!m_valid) return;
    if (new_width <= 0 || new_height <= 0) return;
    if (new_width == m_width && new_height == m_height) return;

    m_width  = new_width;
    m_height = new_height;

    // Target swap interval: X11/Windows/Android = 0 (low latency),
    // Wayland = 1 (compositor pacing).
    int resize_interval = 0;

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    // Dual-mode compatibility: only run when Wayland is active.
    if (m_window &&
        KnstWindowSources::s_active_backend == knst_backend::wayland) {

        // EGL window resize first, then viewport.
        if (m_egl_window) {
            const int pw = m_window->wayland_physical_width(new_width);
            const int ph = m_window->wayland_physical_height(new_height);

            MakeCurrent();
            wl_egl_window_resize(m_egl_window, pw, ph, 0, 0);
        }

        m_window->apply_wayland_viewport_resize(new_width, new_height);
        resize_interval = 1;
    }
#endif

    if (m_current_swap_interval != resize_interval) {
        apply_swap_interval_raw(resize_interval);
    }

    m_last_resize_time = KnstWindowSources::get_current_time_ms();
}


// ─── ReleaseSurface ────────────────────────────────────────────────────────
// Drop the surface, keep the context.
//
// On Android, called after APP_CMD_TERM_WINDOW. Otherwise EGL still
// holds a pointer to the dead ANativeWindow.

inline void knst_window_opengl_content::ReleaseSurface() noexcept {
    if (!m_valid) return;

#if KNST_USING_PLATFORM_WINDOWS
    // Windows doesn't recreate windows — no-op.
#else
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    if (dpy != EGL_NO_DISPLAY && m_egl_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
    }
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    if (m_egl_window) {
        wl_egl_window_destroy(m_egl_window);
        m_egl_window = nullptr;
    }
#endif
}


// ─── RecreateSurface ───────────────────────────────────────────────────────
// Create an EGL surface for the new native window. Context is preserved;
// only the surface is rebuilt. Called after APP_CMD_INIT_WINDOW on
// Android.

inline bool knst_window_opengl_content::RecreateSurface() noexcept {
    if (!m_valid || !m_window) return false;

#if KNST_USING_PLATFORM_WINDOWS
    // Windows doesn't recreate windows — no-op.
    return true;
#elif defined(KNST_USING_PLATFORM_ANDROID)
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    EGLConfig  cfg = knst_opengl_global::GetEGLConfig();
    if (dpy == EGL_NO_DISPLAY || !cfg) return false;

    android_app* app = KnstWindowSources::get_android_app();
    if (!app || !app->window) return false;

    // Drop the old surface first to avoid a leak.
    if (m_egl_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
    }

    m_egl_surface = eglCreateWindowSurface(
        dpy, cfg, (EGLNativeWindowType)app->window, nullptr);
    if (m_egl_surface == EGL_NO_SURFACE) return false;

    if (!eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroySurface(dpy, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        return false;
    }

    // Read the new size.
    m_width  = ANativeWindow_getWidth(app->window);
    m_height = ANativeWindow_getHeight(app->window);
    if (m_width  <= 0) m_width  = 800;
    if (m_height <= 0) m_height = 600;

    eglSwapInterval(dpy, m_user_vsync);
    m_current_swap_interval = m_user_vsync;
    return true;

#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    EGLConfig  cfg = knst_opengl_global::GetEGLConfig();
    if (dpy == EGL_NO_DISPLAY || !cfg) return false;

    wl_surface* wl_surf = const_cast<wl_surface*>(
        m_window->get_wayland_surface_handle());
    if (!wl_surf) return false;

    const int phys_w = m_window->wayland_physical_width(m_width);
    const int phys_h = m_window->wayland_physical_height(m_height);
    m_egl_window = wl_egl_window_create(wl_surf, phys_w, phys_h);
    if (!m_egl_window) return false;

    m_egl_surface = eglCreateWindowSurface(
        dpy, cfg, (EGLNativeWindowType)m_egl_window, nullptr);
    if (m_egl_surface == EGL_NO_SURFACE) {
        wl_egl_window_destroy(m_egl_window);
        m_egl_window = nullptr;
        return false;
    }

    if (!eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroySurface(dpy, m_egl_surface);
        wl_egl_window_destroy(m_egl_window);
        m_egl_surface = EGL_NO_SURFACE;
        m_egl_window = nullptr;
        return false;
    }

    eglSwapInterval(dpy, m_user_vsync);
    m_current_swap_interval = m_user_vsync;
    return true;

#else
    // X11: the native window normally stays the same — no-op.
    return true;
#endif
}


// ─── recover_context ───────────────────────────────────────────────────────
// Rebuild the EGL context after EGL_CONTEXT_LOST.
//
// Called from SwapBuffers() only when EGL_CONTEXT_LOST is caught.
// Steps: drop the dead context -> create a new one with the same config
// -> make it current -> set a flag on the window.
//
// Every GL object from the old context (program, VAO, VBO, texture,
// FBO...) is now invalid. The user checks is_gl_context_lost() in the
// window and rebuilds them.
//
// Only compiled for EGL platforms (Android/Linux).
#if !KNST_USING_PLATFORM_WINDOWS
inline bool knst_window_opengl_content::recover_context() noexcept {
    if (!m_valid) return false;

    EGLDisplay dpy = knst_opengl_global::GetEGLDisplay();
    EGLConfig  cfg = knst_opengl_global::GetEGLConfig();
    if (dpy == EGL_NO_DISPLAY || !cfg) return false;
    if (m_egl_surface == EGL_NO_SURFACE) return false;

    // eglBindAPI is thread-local — recover_context can run on a
    // different thread than the original Init.
    {
        EGLint _api = (knst_opengl_global::GetOptions().client_api ==
                       knst_opengl_global::ClientAPI::OpenGLES)
                        ? EGL_OPENGL_ES_API
                        : EGL_OPENGL_API;
        eglBindAPI(_api);
    }

    // 1) Drop the old context.
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (m_egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(dpy, m_egl_context);
        m_egl_context = EGL_NO_CONTEXT;
    }

    // 2) Create a new context with the same config.
    const auto& opts = knst_opengl_global::GetOptions();

    // Use the unified helper — the old recover_context dropped
    // forward_compat / debug / robust attributes on recovery.
    EGLint ctx_attribs[32];
    knst_gl_detail::build_egl_context_attribs(opts, ctx_attribs);

    m_egl_context = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (m_egl_context == EGL_NO_CONTEXT) {
        return false;
    }

    // 3) Make the new context current.
    if (!eglMakeCurrent(dpy, m_egl_surface, m_egl_surface, m_egl_context)) {
        eglDestroyContext(dpy, m_egl_context);
        m_egl_context = EGL_NO_CONTEXT;
        return false;
    }

    // 4) Re-apply the vsync preference.
    eglSwapInterval(dpy, m_user_vsync);
    m_current_swap_interval = m_user_vsync;

    return true;
}
#endif // !KNST_USING_PLATFORM_WINDOWS


// ─── OnScaleChanged ────────────────────────────────────────────────────────
// Wayland fractional-scale change.
//
// Resizes wl_egl_window with the new scale even if the logical size
// stayed the same. The app should also update glViewport and the
// projection matrix to the new framebuffer size.

inline void knst_window_opengl_content::OnScaleChanged() noexcept {
    if (!m_valid) return;

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    // Dual-mode compatibility: do nothing if Wayland isn't active.
    if (m_window &&
        KnstWindowSources::s_active_backend == knst_backend::wayland) {

        if (m_egl_window) {
            const int pw = m_window->wayland_physical_width(m_width);
            const int ph = m_window->wayland_physical_height(m_height);
            MakeCurrent();
            wl_egl_window_resize(m_egl_window, pw, ph, 0, 0);
        }
        m_window->apply_wayland_viewport_resize(m_width, m_height);

        if (m_current_swap_interval != 1) {
            apply_swap_interval_raw(1);
        }
        m_last_resize_time = KnstWindowSources::get_current_time_ms();
    }
#endif
}