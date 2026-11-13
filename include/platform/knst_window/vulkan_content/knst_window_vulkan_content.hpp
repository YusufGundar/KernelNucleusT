/*
----------------------------
knst_window_vulkan_content.hpp
----------------------------

    Per-window Vulkan surface management.

*/

#pragma once

// ─── Enable platform-specific Vulkan surface functions ────────────────
#if KNST_USING_PLATFORM_WINDOWS
    #ifndef VK_USE_PLATFORM_WIN32_KHR
        #define VK_USE_PLATFORM_WIN32_KHR
    #endif
#endif

#if KNST_USING_LINUX_PLATFORM_X11
    #ifndef VK_USE_PLATFORM_XLIB_KHR
        #define VK_USE_PLATFORM_XLIB_KHR
    #endif
    #ifndef VK_USE_PLATFORM_XCB_KHR
        #define VK_USE_PLATFORM_XCB_KHR
    #endif
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    #ifndef VK_USE_PLATFORM_WAYLAND_KHR
        #define VK_USE_PLATFORM_WAYLAND_KHR
    #endif
#endif

#if defined(KNST_USING_PLATFORM_ANDROID)
    #ifndef VK_USE_PLATFORM_ANDROID_KHR
        #define VK_USE_PLATFORM_ANDROID_KHR
    #endif
#endif

#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <utility>


// ============================================================================
//  knst_window_vulkan_content — per-window Vulkan surface
// ============================================================================
class knst_window_vulkan_content : public knst_surface_client {
public:
    knst_window_vulkan_content() noexcept = default;
    ~knst_window_vulkan_content() noexcept { Shutdown(); }

    knst_window_vulkan_content(const knst_window_vulkan_content&) = delete;
    knst_window_vulkan_content& operator=(const knst_window_vulkan_content&) = delete;

    knst_window_vulkan_content(knst_window_vulkan_content&& other) noexcept {
        *this = std::move(other);
    }
    knst_window_vulkan_content& operator=(knst_window_vulkan_content&& other) noexcept;

    // ====================================================================
    //  knst_surface_client implementation
    // ====================================================================
    void on_surface_release() noexcept override {
        ReleaseSurface();
    }

    bool on_surface_recreate() noexcept override {
        return RecreateSurface();
    }

    // Called while the window is being destroyed. We do NOT destroy the
    // VkSurfaceKHR here — the user may still have a live swapchain, and
    // destroying the surface first would make vkDestroySwapchainKHR a
    // use-after-free. The correct order is: swapchain → content → window.
    // This method only nulls m_window so content's own destroy() won't
    // touch a dangling pointer.
    void on_window_destroyed() noexcept override { m_window = nullptr; }

    // ====================================================================
    //  Surface lifecycle (Android TERM_WINDOW / INIT_WINDOW)
    //
    //  ReleaseSurface() is called when the VkSurfaceKHR becomes invalid.
    //  RecreateSurface() builds a new one when a fresh ANativeWindow
    //  arrives. VkDevice / VkInstance survive — only the surface is
    //  rebuilt.
    // ====================================================================
    void ReleaseSurface() noexcept;
    bool RecreateSurface() noexcept;

    // ====================================================================
    //  Static helpers — mirrors GLFW's glfwGetRequiredInstanceExtensions
    //  and glfwGetPhysicalDevicePresentationSupport
    // ====================================================================

    // Returns the VkInstance extensions required for the current
    // platform. Always "VK_KHR_surface" plus the platform-specific
    // surface extension. The list is NOT null-terminated — the count
    // comes back separately.
    //
    // Usage:
    //   uint32_t count = 0;
    //   const char** exts = knst_window_vulkan_content::RequiredInstanceExtensions(&count);
    //   VkInstanceCreateInfo ci = {};
    //   ci.enabledExtensionCount   = count;
    //   ci.ppEnabledExtensionNames = exts;
    static const char** RequiredInstanceExtensions(uint32_t* out_count) noexcept;

    // Checks whether the given physical device, at the given queue
    // family, can present to a surface created for this platform.
    // Equivalent to GLFW's glfwGetPhysicalDevicePresentationSupport.
    //
    // NOTE: this only gives a platform-specific hint. For a definitive
    //       answer the user should still call
    //       vkGetPhysicalDeviceSurfaceSupportKHR with a VkSurfaceKHR.
    static bool GetPhysicalDevicePresentationSupport(VkPhysicalDevice device , uint32_t queue_family) noexcept;
                                                   

    // ====================================================================
    //  Per-window surface
    // ====================================================================

    // Creates a VkSurfaceKHR from the window. VkInstance must already
    // exist and include the extensions from RequiredInstanceExtensions().
    bool Init(knst_window* window, VkInstance instance) noexcept;

    void Shutdown() noexcept;

    // ─── State ─────────────────────────────────────────────────────────
    bool IsValid() const noexcept { return m_valid; }
    VkSurfaceKHR GetSurface()const noexcept { return m_surface; }
    VkInstance   GetInstance()const noexcept { return m_instance; }
    knst_window* GetWindow() const noexcept { return m_window; }

    // On Android, IsValid() stays true between TERM_WINDOW and
    // INIT_WINDOW, but the surface becomes VK_NULL_HANDLE. The user
    // should check this before building a swapchain.
    bool HasSurface() const noexcept { return m_surface != VK_NULL_HANDLE; }

private:
    knst_window* m_window   = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface  = VK_NULL_HANDLE;
    bool m_valid    = false;
};


// ============================================================================
//  IMPLEMENTATION
// ============================================================================

inline knst_window_vulkan_content&
knst_window_vulkan_content::operator=(knst_window_vulkan_content&& other) noexcept {
    if (this == &other) return *this;

    // Unregister from other's old window FIRST.
    if (other.m_window) {
        other.m_window->unregister_surface_client(&other);
    }

    if (m_window) {
        m_window->unregister_surface_client(this);
    }

    Shutdown();

    m_window = other.m_window;
    m_instance = other.m_instance;
    m_surface = other.m_surface;
    m_valid = other.m_valid;

    other.m_window = nullptr;
    other.m_instance = VK_NULL_HANDLE;
    other.m_surface = VK_NULL_HANDLE;
    other.m_valid = false;

    // Register with the new window.
    if (m_window && m_valid) {
        m_window->register_surface_client(this);
    }

    return *this;
}


inline const char**
knst_window_vulkan_content::RequiredInstanceExtensions(uint32_t* out_count) noexcept {
#if KNST_USING_PLATFORM_WINDOWS
    static const char* exts[] = {
        "VK_KHR_surface",
        "VK_KHR_win32_surface"
    };
    if (out_count) *out_count = 2;
    return exts;

#elif defined(KNST_USING_PLATFORM_ANDROID)
    static const char* exts[] = {
        "VK_KHR_surface",
        "VK_KHR_android_surface"
    };
    if (out_count) *out_count = 2;
    return exts;

#elif defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
    // Dual mode: runtime dispatch based on the detected backend.
    static const char* exts_x11[] = {
        "VK_KHR_surface",
        "VK_KHR_xlib_surface"
    };
    static const char* exts_wayland[] = {
        "VK_KHR_surface",
        "VK_KHR_wayland_surface"
    };
    if (KnstWindowSources::s_active_backend == knst_backend::wayland) {
        if (out_count) *out_count = 2;
        return exts_wayland;
    }
    if (out_count) *out_count = 2;
    return exts_x11;

#elif KNST_USING_LINUX_PLATFORM_X11
    static const char* exts[] = {
        "VK_KHR_surface",
        "VK_KHR_xlib_surface"
    };
    if (out_count) *out_count = 2;
    return exts;

#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    static const char* exts[] = {
        "VK_KHR_surface",
        "VK_KHR_wayland_surface"
    };
    if (out_count) *out_count = 2;
    return exts;

#else
    if (out_count) *out_count = 0;
    return nullptr;
#endif
}


inline bool
knst_window_vulkan_content::GetPhysicalDevicePresentationSupport(
    VkPhysicalDevice device, uint32_t queue_family) noexcept {

#if KNST_USING_PLATFORM_WINDOWS
    return vkGetPhysicalDeviceWin32PresentationSupportKHR(device, queue_family) == VK_TRUE;

#elif defined(KNST_USING_PLATFORM_ANDROID)
    (void)device; (void)queue_family;
    return true;

#elif defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
    // Dual mode: dispatch based on the active backend.
    if (KnstWindowSources::s_active_backend == knst_backend::wayland) {
        // Wayland: no precise check possible without a VkSurfaceKHR.
        (void)device; (void)queue_family;
        return true;
    }
    // X11
    {
        Display* dpy = KnstWindowSources::get_native_x11_display();
        if (!dpy) return true;
        VisualID vid = XVisualIDFromVisual(
            DefaultVisual(dpy, DefaultScreen(dpy)));
        return vkGetPhysicalDeviceXlibPresentationSupportKHR(
            device, queue_family, dpy, vid) == VK_TRUE;
    }

#elif KNST_USING_LINUX_PLATFORM_X11
    {
        Display* dpy = KnstWindowSources::get_native_x11_display();
        if (!dpy) return true;
        VisualID vid = XVisualIDFromVisual(
            DefaultVisual(dpy, DefaultScreen(dpy)));
        return vkGetPhysicalDeviceXlibPresentationSupportKHR(
            device, queue_family, dpy, vid) == VK_TRUE;
    }

#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    (void)device; (void)queue_family;
    return true;

#else
    (void)device; (void)queue_family;
    return false;
#endif
}


inline bool
knst_window_vulkan_content::Init(knst_window* window, VkInstance instance) noexcept {
    if (!window)   return false;
    if (instance == VK_NULL_HANDLE) return false;

    // The window must have been created before we touch platform handles.
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
    // Dual mode: pick the right handle for the active backend.
    if (KnstWindowSources::s_active_backend == knst_backend::wayland) {
        if (!window->m_surface) return false;
    } else {
        if (!window->m_window) return false;
    }
#elif KNST_USING_PLATFORM_WINDOWS || KNST_USING_LINUX_PLATFORM_X11
    if (!window->m_window) return false;
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    if (!window->m_surface) return false;
#elif defined(KNST_USING_PLATFORM_ANDROID)
    if (!KnstWindowSources::get_android_app() ||
        !KnstWindowSources::get_android_app()->window) return false;
#endif
    if (m_valid) Shutdown();

    m_window   = window;
    m_instance = instance;

    // ====================================================================
    //  Platform-specific surface creation
    // ====================================================================
#if KNST_USING_PLATFORM_WINDOWS
    VkWin32SurfaceCreateInfoKHR ci = {};
    ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = KnstWindowSources::get_windows_native_instance_handle();
    ci.hwnd      = window->get_windows_window_handle();
    if (vkCreateWin32SurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS) {
        m_instance = VK_NULL_HANDLE; m_window = nullptr; return false;
    }

#elif defined(KNST_USING_PLATFORM_ANDROID)
    VkAndroidSurfaceCreateInfoKHR ci = {};
    ci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = KnstWindowSources::get_android_app()->window;
    if (vkCreateAndroidSurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS) {
        m_instance = VK_NULL_HANDLE; m_window = nullptr; return false;
    }

#elif defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
    // ── Dual mode: runtime dispatch ────────────────────────────────────
    if (KnstWindowSources::s_active_backend == knst_backend::wayland) {
        VkWaylandSurfaceCreateInfoKHR ci = {};
        ci.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        ci.display = KnstWindowSources::wayland_display;
        ci.surface = const_cast<wl_surface*>(window->get_wayland_surface_handle());
        if (vkCreateWaylandSurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS) {
            m_instance = VK_NULL_HANDLE; m_window = nullptr; return false;
        }
    } else {
        // X11 — Xlib only. RequiredInstanceExtensions() does not enable
        // the XCB extension, so calling vkCreateXcbSurfaceKHR would
        // dereference a null pointer (crash). Mesa and NVIDIA both
        // support Xlib.
        Display* dpy = KnstWindowSources::get_native_x11_display();
        if (!dpy) {
            m_instance = VK_NULL_HANDLE; m_window = nullptr; return false;
        }

        VkXlibSurfaceCreateInfoKHR ci = {};
        ci.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        ci.dpy    = dpy;
        ci.window = (Window)window->get_x11_window_handle();
        if (vkCreateXlibSurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS) {
            m_instance = VK_NULL_HANDLE; m_window = nullptr; return false;
        }
    }

#elif KNST_USING_LINUX_PLATFORM_X11
    // X11 — Xlib only. The XCB fallback was removed because
    // RequiredInstanceExtensions() only enables VK_KHR_xlib_surface;
    // without the XCB extension, vkCreateXcbSurfaceKHR would call a
    // null pointer.
    {
        Display* dpy = KnstWindowSources::get_native_x11_display();
        if (!dpy) {
            m_instance = VK_NULL_HANDLE; m_window = nullptr; return false;
        }
        VkXlibSurfaceCreateInfoKHR ci = {};
        ci.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        ci.dpy    = dpy;
        ci.window = (Window)window->get_x11_window_handle();
        if (vkCreateXlibSurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS) {
            m_instance = VK_NULL_HANDLE; m_window = nullptr; return false;
        }
    }

#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    VkWaylandSurfaceCreateInfoKHR ci = {};
    ci.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    ci.display = KnstWindowSources::wayland_display;
    ci.surface = const_cast<wl_surface*>(window->get_wayland_surface_handle());
    if (vkCreateWaylandSurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS) {
        m_instance = VK_NULL_HANDLE; m_window = nullptr; return false;
    }

#else
    return false;
#endif

    m_valid = true;

    // Register with the window — on Android TERM_WINDOW/INIT_WINDOW
    // events the library notifies us automatically.
    if (m_window) {
        m_window->register_surface_client(this);
    }

    return true;
}


// ============================================================================
//  ReleaseSurface — drop the VkSurfaceKHR, keep the VkInstance
//
//  Called after APP_CMD_TERM_WINDOW on Android. Otherwise VkSurface
//  still points at the old ANativeWindow.
// ============================================================================
inline void knst_window_vulkan_content::ReleaseSurface() noexcept {
    if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    // m_instance, m_window and m_valid are preserved.
}


// ============================================================================
//  RecreateSurface — build a VkSurfaceKHR for the new native window
// ============================================================================
inline bool knst_window_vulkan_content::RecreateSurface() noexcept {
    if (!m_valid || !m_window) return false;
    if (m_instance == VK_NULL_HANDLE) return false;

#if KNST_USING_PLATFORM_WINDOWS
    // Windows doesn't recreate windows — no-op.
    return true;

#elif defined(KNST_USING_PLATFORM_ANDROID)
    android_app* app = KnstWindowSources::get_android_app();
    if (!app || !app->window) return false;

    // Clear the old surface first.
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    VkAndroidSurfaceCreateInfoKHR ci = {};
    ci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = app->window;
    if (vkCreateAndroidSurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS) {
        m_surface = VK_NULL_HANDLE;
        return false;
    }
    return true;

#else
    // X11 / Wayland: the native window normally stays the same — no-op.
    return true;
#endif
}


inline void knst_window_vulkan_content::Shutdown() noexcept {
    // Unregister FIRST.
    if (m_window) {
        m_window->unregister_surface_client(this);
    }

    if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    m_surface = VK_NULL_HANDLE;
    m_instance = VK_NULL_HANDLE;
    m_window = nullptr;
    m_valid = false;
}