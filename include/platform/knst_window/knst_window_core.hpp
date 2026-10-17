// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_core.hpp
----------------------------

    The core of the window library: structures, classes, and friends.

*/

#pragma once

// ============================================================================
//  Android forward declarations — before any use
// ============================================================================
#if defined(KNST_USING_PLATFORM_ANDROID)
    struct android_app;
    struct AInputEvent;
    typedef struct AInputEvent AInputEvent;

    // ANativeActivity_finish needs the real type definition, not just
    // a forward declaration.
    #include <android/native_activity.h>

    // Forward declaration: should_close() calls this, but the actual
    // definition lives in knst_window_android_jni.hpp, which is included
    // at the bottom of this file (it depends on the KnstWindowSources
    // class). Declaring it here breaks the circular dependency.
    namespace knst_android_jni {
        inline void finish_activity() noexcept;
    }
#endif

// ============================================================================
//  KNST_GLSL_HEADER — cross-platform shader prelude
//
//  User code writes:
//      static const char* VS = KNST_GLSL_HEADER R"(
//          layout(location = 0) in vec2 a_position;
//          ...
//      )";
//
//  The same source compiles for desktop GL 3.3 core and Android GLES 3.00.
// ============================================================================
#if defined(KNST_USING_PLATFORM_ANDROID)
    #define KNST_GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
    #define KNST_GLSL_HEADER "#version 330 core\n"
#endif

// Used by both knst_window_event and knst_window — must be defined up
// front, or the knst_window class errors with "not declared".
#ifndef KNST_MAX_TOUCH_POINTS
    #define KNST_MAX_TOUCH_POINTS 10
#endif

// ============================================================================
//  System headers — order matters
// ============================================================================
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#if !KNST_USING_PLATFORM_WINDOWS
#include <signal.h>
#endif

// ── X11 (Xlib.h MUST come before XInput2.h and Xfixes.h) ─────────────
#if KNST_USING_LINUX_PLATFORM_X11
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/Xatom.h>
    #include <X11/keysym.h>
    #include <X11/XKBlib.h>
    #include <X11/Xlocale.h>       // for XSetLocaleModifiers
    #include <X11/extensions/Xfixes.h>
    #include <X11/extensions/XInput2.h>
    #include <xcb/sync.h>
    #include <xcb/xcb_icccm.h>     // for xcb_size_hints_t (max-size save)
    // Needed to receive XI2 events through XCB. The Xlib XInput2 API
    // is incompatible with the XCB event pump — different wire format.
    // xcb-xinput gives the real struct layout.
    #include <xcb/xinput.h>
    #include <clocale>
#endif

// ── Wayland ──────────────────────────────────────────────────────────
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    #include <wayland-client.h>
    #include <wayland-cursor.h>
    #include <wayland-egl.h>
    #include <xkbcommon/xkbcommon.h>
#endif

#include "knst_display.hpp"


// ============================================================================
//  Runtime backend selection (dual mode only)
// ============================================================================
enum class knst_backend : uint8_t {
    none = 0,
    x11 = 1,
    wayland = 2,
    android = 3,
    windows = 4
};

// ─── Method rename macros for dual mode ──────────────────────────────
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
    #define KNST_X11_METHOD(name)      name##_x11
    #define KNST_WAYLAND_METHOD(name)  name##_wayland
    #define KNST_IS_DUAL_BACKEND 1
#else
    #define KNST_X11_METHOD(name)      name
    #define KNST_WAYLAND_METHOD(name)  name
    #define KNST_IS_DUAL_BACKEND 0
#endif


#if defined(KNST_USING_PLATFORM_ANDROID)

    class KnstWindowSources;
    #include <mutex>

    class knst_mobile_keyboard {
    private:
        friend KnstWindowSources;
        static inline JavaVM* s_javaVM = nullptr;
        static inline jobject s_activity = nullptr;
        static inline jclass s_activityClass = nullptr;
        static inline jclass s_immClass = nullptr;
        static inline jmethodID s_getSystemService = nullptr;
        static inline jmethodID s_showSoftInput = nullptr;
        static inline jmethodID s_hideSoftInput = nullptr;
        static inline jmethodID s_getWindow = nullptr;
        static inline jmethodID s_getDecorView = nullptr;
        static inline jmethodID s_getWindowToken = nullptr;
        static inline bool s_initialized = false;
        static inline bool s_visible = false;
        static inline std::mutex s_mutex;

        struct jni_scope {
            JNIEnv* env = nullptr;
            bool attached = false;
            JavaVM* vm = nullptr;

            jni_scope(JavaVM* vm);
            ~jni_scope();

            JNIEnv* get() const { return env; }
            bool valid() const { return env != nullptr; }
        };
        static void Init(struct android_app* app);
        static void shutdown();

    public:
        static bool show();
        static bool hide();
        static void toggle();
        static bool is_visible();
    };

#else   // PC / Windows / Linux — knst_mobile_keyboard no-op shim
        // No-op version of the Android-only soft-keyboard API, so user
        // code can call it without #if guards.

    class knst_mobile_keyboard {
    public:
        static bool show() noexcept {return false; }
        static bool hide() noexcept { return false; }
        static void toggle() noexcept {}
        static bool is_visible() noexcept { return false; }
        static void shutdown()noexcept {}
    };

#endif


#if KNST_USING_LINUX_PLATFORM_WAYLAND
    enum class resize_edge
    {
        none,
        left, right, top, bottom,
        top_left, top_right, bottom_left, bottom_right
    };
#endif


// ============================================================================
//  knst_surface_client — implement this to own a native surface (EGL,
//  VkSurface, …)
//
//  Used by:
//    - knst_window_opengl_content  → EGL surface
//    - knst_window_vulkan_content  → VkSurfaceKHR
//    - (future) software renderer, D3D, Metal, …
//
//  The library calls these methods on window lifecycle events
//  (TERM_WINDOW / INIT_WINDOW on Android, window destroy on X11, …).
//
//  Unregister is mandatory: when the content is destroyed or the
//  window changes, the window must be notified — otherwise a dangling
//  pointer remains.
// ============================================================================
class knst_surface_client {
public:
    virtual ~knst_surface_client() = default;

    // Native surface became invalid — release every surface-owned
    // resource you have. Context/device survive.
    virtual void on_surface_release() noexcept = 0;

    // Native surface became valid again — recreate. Return false on
    // failure (the library logs it).
    virtual bool on_surface_recreate() noexcept = 0;

    // Called while the window is being destroyed (native window not
    // yet torn down). Resources like EGL / wl_egl_window must be
    // released here in the right order.
    virtual void on_window_destroyed() noexcept {}
};

class knst_window;

#if KNST_USING_LINUX_PLATFORM_X11
    // Forward declaration: used inside CleanUp(), defined in
    // knst_window_x11_manager.hpp (included after this header).
    namespace knst_x11_cursor_cache {
        void clear_all() noexcept;
    }
#endif

// Wayland listener class must be declared up top because:
//   - KnstWindowSources::Init() uses knst_window_wayland_funcs::*
//     static listener references
//   - knst_window::request_frame_callback() does the same
// The bodies (_event_helper.hpp) are still included at the bottom.
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    #include "../linux/wayland/knst_window_wayland_event_manager.hpp"
#endif

// ============================================================================
//  Android: std::cout / std::cerr / printf / fprintf → logcat
//
//  On Android, stdout and stderr go to /dev/null — they don't show up
//  in logcat. The helper below opens a pipe + thread and redirects
//  stdio to logcat under the "KNST" tag. So a user's std::cout << "..."
//  shows up in logcat without any code change.
// ============================================================================
#if defined(KNST_USING_PLATFORM_ANDROID)
    #include <android/log.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <cstdio>

    inline int g_knst_log_pipe[2] = {-1, -1};

    // Line-buffered log pipe thread.
    //  The old version logged each read() chunk as its own line, which
    //  had two problems:
    //    - A single read with multiple lines got glued into one message
    //    - fprintf with two write()s split the line ("[TOUCH] ..." and
    //      "[id=0 ...]" showed as separate log lines)
    //  This version reads byte by byte and flushes on '\n'.
    inline void* knst_log_pipe_thread(void*) {
        char chunk[512];
        std::string line;
        line.reserve(512);

        ssize_t n;
        while ((n = ::read(g_knst_log_pipe[0], chunk, sizeof(chunk))) > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                if (chunk[i] == '\n') {
                    __android_log_write(ANDROID_LOG_INFO, "KNST",  line.c_str());
                    line.clear();
                } else {
                    line.push_back(chunk[i]);
                }
            }

            // Safety: don't let the buffer grow forever on a long line.
            if (line.size() > 2048) {
                __android_log_write(ANDROID_LOG_INFO, "KNST", line.c_str());
                line.clear();
            }
        }

        // Flush whatever's left (last line without a trailing newline).
        if (!line.empty()) {
            __android_log_write(ANDROID_LOG_INFO, "KNST",  line.c_str());
        }

        return nullptr;
    }

    inline void knst_redirect_stdio_to_logcat() {
        static bool done = false;
        if (done) return;
        done = true;

        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);

        if (::pipe(g_knst_log_pipe) != 0) return;
        ::dup2(g_knst_log_pipe[1], 1);
        ::dup2(g_knst_log_pipe[1], 2);

        pthread_t t;
        pthread_create(&t, nullptr, knst_log_pipe_thread, nullptr);
        pthread_detach(t);
    }
#endif


// ============================================================================
//  Shared URI list parser (for X11 + Wayland DnD)
//
//  Takes file:// URIs, percent-decodes, and appends them to a
//  knst_c16string vector. Used by the X11 and Wayland event managers.
//
//  The old X11 code had signed/unsigned confusion and a missing
//  unsigned init during percent-decode → UB. This version is safe.
// ============================================================================
#include <string>

inline void knst_parse_uri_list(const knst_byte_string& uriList, knst_vector<knst_c16string>& out) {
    uint32_t pos = 0;
    while (pos < uriList.length()) {
        uint32_t end = pos;
        while (end < uriList.length() && uriList[end] != '\n') end++;

        std::string uri(reinterpret_cast<const char*>(uriList.data() + pos),
                        end - pos);
        if (!uri.empty() && uri.back() == '\r') uri.pop_back();

        if (uri.find("file://") == 0) {
            std::string path = uri.substr(7);
            if (path.find("localhost/") == 0) path = path.substr(9);

            std::string decoded;
            decoded.reserve(path.size());
            for (size_t i = 0; i < path.length(); i++) {
                if (path[i] == '%' && i + 2 < path.length()) {
                    unsigned int hex = 0;
                    if (sscanf(path.substr(i + 1, 2).c_str(), "%x", &hex) == 1) {
                        decoded.push_back((char)hex);
                        i += 2;
                        continue;
                    }
                }
                decoded.push_back(path[i]);
            }
            out.push_back(knst_c16string(decoded.c_str()));
        }
        pos = end + 1;
    }
}


class KnstWindowSources {

public:
    friend struct knst_window_event_system;
    friend class knst_window;

    // ====================================================================
    //  Runtime backend state (dual mode)
    // ====================================================================
    static inline knst_backend s_active_backend = knst_backend::none;
    static knst_backend detect_backend() noexcept;
    KNST_FORCE_INLINE static knst_backend active_backend() noexcept {
        return s_active_backend;
    }
    KNST_FORCE_INLINE static bool is_x11_active() noexcept {
        return s_active_backend == knst_backend::x11;
    }
    KNST_FORCE_INLINE static bool is_wayland_active() noexcept {
        return s_active_backend == knst_backend::wayland;
    }
    KNST_FORCE_INLINE static bool is_windows_active() noexcept {
        return s_active_backend == knst_backend::windows;
    }

#if KNST_USING_PLATFORM_WINDOWS
    static inline HINSTANCE  m_hInstance = nullptr;
    static inline WNDCLASSEXW m_wc = {0};
#endif

#if KNST_USING_LINUX_PLATFORM_X11
    friend KNST_FORCE_INLINE void load_native_to_knst_event(
        knst_window& window, xcb_generic_event_t* ev) noexcept;

    // Xfixes.h / XInput2.h are included at the top of this file. Don't
    // include them here inside the class body — it works thanks to
    // include guards, but it reads confusingly.
    friend class knst_display;
    static inline xcb_connection_t* m_connection   = nullptr;
    static inline Display*m_xlib_display = nullptr;

    static inline xcb_window_t m_root = 0;
    static inline xcb_atom_t m_wmDelete = 0;
    static inline xcb_key_symbols_t* m_keysyms = nullptr;
    static inline xcb_atom_t m_netMoveResizeWindow = 0;

    // XIM (X Input Method) is opened ONCE per process. Each window
    // derives its own XIC from this XIM. The old code opened XOpenIM
    // per window — leaked handles and IME didn't respect the default
    // locale.
    static inline XIM m_xim = nullptr;

    static inline xcb_atom_t m_NET_WM_STATE = 0;
    static inline xcb_atom_t m_NET_WM_STATE_FULLSCREEN = 0;
    static inline xcb_atom_t m_NET_WM_STATE_MAXIMIZED_VERT = 0;
    static inline xcb_atom_t m_NET_WM_STATE_MAXIMIZED_HORZ = 0;
    static inline xcb_atom_t m_NET_WM_STATE_HIDDEN = 0;
    static inline xcb_atom_t m_wmChangeState = 0;
    static inline xcb_atom_t m_NET_ACTIVE_WINDOW = 0;
    static inline xcb_atom_t m_wmProtocols = 0;

    static inline xcb_atom_t m_CLIPBOARD = 0;
    static inline xcb_atom_t m_UTF8_STRING = 0;
    static inline xcb_atom_t m_TARGETS = 0;
    static inline xcb_atom_t m_ATOM = 0;
    static inline xcb_atom_t m_PRIMARY = 0;
    static inline xcb_atom_t m_XdndAware = 0;

    // ⚠️ BUG FIX: CLIPBOARD ve PRIMARY selection'ları AYNI property atomuna
    //    yazıyordu (m_PRIMARY). Eşzamanlı bir clipboard okuma ve primary
    //    okuma birbirini eziyordu; X11 SELECTION_NOTIFY her iki selection
    //    için de m_PRIMARY property'sini bildirdiğinden yanlış içerik
    //    dönebiliyordu. Her selection kendi property atomunu kullanmalı.
    static inline xcb_atom_t m_clipboard_property = 0;   // "_KNST_CLIPBOARD_DATA"
    static inline xcb_atom_t m_primary_property = 0;   // "_KNST_PRIMARY_DATA"

    // ── 6C: XInput2 (raw mouse) ─────────────────────────────────────
    static inline int m_xi_opcode = -1;
    static inline int m_xi_event_base = 0;
    static inline int m_xi_error_base = 0;

    // ── 6D: XRandR (monitor change) ─────────────────────────────────
    static inline int m_randr_event_base = -1;

    // ====================================================================
    //  "Global" events that don't belong to a specific window. The
    //  event pump asks here first; a true return means the event was
    //  consumed.
    //
    //  XCB_RANDR_SCREEN_CHANGE_NOTIFY is defined as 0, so
    //  `case XCB_RANDR_SCREEN_CHANGE_NOTIFY:` never matches directly.
    //  The correct approach: query the extension's first_event at
    //  runtime and use it as an offset.
    // ====================================================================
    KNST_FORCE_INLINE static bool x11_try_global_event(
        uint8_t event_type, xcb_generic_event_t*) noexcept {
        if (m_randr_event_base >= 0 &&
            event_type == (uint8_t)(m_randr_event_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY)) {
            knst_display::refresh_screens();
            return true;
        }
        return false;
    }

    static inline xcb_atom_t m_XdndEnter = 0;
    static inline xcb_atom_t m_XdndPosition = 0;
    static inline xcb_atom_t m_XdndStatus = 0;
    static inline xcb_atom_t m_XdndDrop = 0;
    static inline xcb_atom_t m_XdndFinished = 0;
    static inline xcb_atom_t m_XdndSelection = 0;
    static inline xcb_atom_t m_XdndActionCopy = 0;
    static inline xcb_atom_t m_XdndTypeList = 0;
    static inline xcb_atom_t m_textUriList = 0;
    static inline xcb_atom_t m_XdndLeave = 0;
    static inline xcb_atom_t m_MOTIF_WM_HINTS = 0;
    static inline xcb_atom_t m_NET_WM_MOVERESIZE = 0;

    static inline xcb_atom_t m_wmSyncRequest = XCB_NONE;
    static inline xcb_atom_t m_wmSyncRequestCounter = XCB_NONE;
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    static inline uint32_t drag_enter_serial = 0;
    static inline struct wl_data_device_manager* data_device_manager = nullptr;
    static inline struct wl_data_device* data_device = nullptr;

    static inline struct wl_data_source* clipboard_source = nullptr;
    static inline knst_c16string clipboard_pending_text;

    static inline struct wl_data_offer* selection_offer = nullptr;

    static inline struct wl_data_offer* drag_offer = nullptr;
    static inline bool drag_offer_has_uri_list = false;
    static inline struct wl_data_offer* pending_offer = nullptr;
    static inline bool pending_offer_has_uri_list = false;
    static inline knst_window* drag_target_window = nullptr;
    static inline uint32_t keyboard_serial = 0;

    friend class knst_window_wayland_funcs;
    friend inline void knst_display::refresh_screens() noexcept;

    static inline wl_registry* registery  = nullptr;
    static inline wl_compositor* compositor = nullptr;
    static inline struct xdg_wm_base* wmBase = nullptr;
    static inline struct wl_shm* shm = nullptr;
    static inline struct wl_seat* seat = nullptr;
    static inline wl_pointer* pointer = nullptr;
    static inline knst_window* active_window = nullptr;
    static inline wl_keyboard* keyboard = nullptr;
    static inline knst_window* keyboard_focus_window = nullptr;
    static inline xkb_context* xkb_ctx = nullptr;
    static inline xkb_keymap* xkb_map = nullptr;
    static inline xkb_state* xkb_st = nullptr;

    static inline struct zwp_pointer_constraints_v1* pointer_constraints = nullptr;
    static inline struct zwp_relative_pointer_manager_v1* relative_pointer_manager = nullptr;
    static inline struct zwp_relative_pointer_v1* relative_pointer = nullptr;
    static inline struct zwp_locked_pointer_v1* locked_pointer = nullptr;
    static inline struct zwp_confined_pointer_v1* confined_pointer = nullptr;

    // ── 5D.1: xdg-decoration ────────────────────────────────────────
    static inline struct zxdg_decoration_manager_v1* decoration_manager = nullptr;

    // ── 5D.2: cursor-shape-v1 ───────────────────────────────────────
    static inline struct wp_cursor_shape_manager_v1* cursor_shape_manager = nullptr;
    static inline struct wp_cursor_shape_device_v1* cursor_shape_device  = nullptr;

    // ── 5D.3: fractional-scale + viewporter ─────────────────────────
    static inline struct wp_fractional_scale_manager_v1* fractional_scale_manager = nullptr;
    static inline struct wp_viewporter* viewporter = nullptr;
    // Global fallback. New code uses the per-window value
    // (window->m_fractional_scale_120). This global is only for
    // backward compatibility and the "no window yet" case.
    static inline int fractional_scale_120 = 120;   // 120 = 1.0x, 180 = 1.5x

    // ── 5D.4: xdg-activation ────────────────────────────────────────
    static inline struct xdg_activation_v1* activation = nullptr;
    static inline uint32_t activation_last_serial = 0;

    // ── 5D.5: primary-selection ─────────────────────────────────────
    static inline struct zwp_primary_selection_device_manager_v1* primary_selection_manager = nullptr;
    static inline struct zwp_primary_selection_device_v1* primary_selection_device = nullptr;
    static inline struct zwp_primary_selection_offer_v1* primary_offer = nullptr;
    static inline struct zwp_primary_selection_source_v1* primary_source = nullptr;


    static inline knst_c16string  primary_pending_text;

    // ── 5D.6: idle-inhibit ──────────────────────────────────────────
    static inline struct zwp_idle_inhibit_manager_v1* idle_inhibit_manager = nullptr;
    static inline struct zwp_idle_inhibitor_v1* idle_inhibitor = nullptr;
    // Which window owns the inhibitor? So destroy() doesn't affect the
    // wrong window.
    static inline knst_window* idle_inhibit_owner = nullptr;

    // ── 6A: text-input-v3 (IME) ─────────────────────────────────────
    static inline struct zwp_text_input_manager_v3* text_input_manager = nullptr;

    // ── Touch screen (wl_touch) ─────────────────────────────────────
    static inline struct wl_touch* touch = nullptr;
    // Active touch surface — TouchMotion / TouchUp / TouchFrame find
    // the window through this surface. On touch-only devices pointer
    // enter may not fire, so we can't rely on pointer activity.
    static inline struct wl_surface* touch_surface = nullptr;

    // ── 6B: xdg-toplevel-icon ───────────────────────────────────────
    static inline struct xdg_toplevel_icon_manager_v1* toplevel_icon_manager = nullptr;

    static inline wl_cursor_theme* cursor_theme = nullptr;

    static inline wl_surface* cursor_surface = nullptr;
    static inline wl_cursor* cursor_default = nullptr;
    static inline wl_cursor* cursor_ew = nullptr;
    static inline wl_cursor* cursor_ns = nullptr;
    static inline wl_cursor* cursor_nwse = nullptr;
    static inline wl_cursor* cursor_nesw  = nullptr;

    static inline void RegistryAdd(void* data, wl_registry* registry, uint32_t name, const char* interface,uint32_t version) {
        (void)data;

        if (strcmp(interface, wl_compositor_interface.name) == 0) {
            compositor = (wl_compositor*)wl_registry_bind(
                registry, name, &wl_compositor_interface,
                (version < 4 ? version : 4));
        }
        else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
            wmBase = (xdg_wm_base*)wl_registry_bind(
                registry, name, &xdg_wm_base_interface, 1);
        }
        else if (strcmp(interface, wl_shm_interface.name) == 0) {
            shm = (wl_shm*)wl_registry_bind(
                registry, name, &wl_shm_interface, 1);
        }
        else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
            pointer_constraints = (struct zwp_pointer_constraints_v1*)wl_registry_bind(
                registry, name, &zwp_pointer_constraints_v1_interface, 1);
        }
        else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
            relative_pointer_manager = (struct zwp_relative_pointer_manager_v1*)wl_registry_bind(
                registry, name, &zwp_relative_pointer_manager_v1_interface, 1);
        }
        // ── 5D.1: xdg-decoration ────────────────────────────────────
        else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
            decoration_manager = (struct zxdg_decoration_manager_v1*)wl_registry_bind(
                registry, name, &zxdg_decoration_manager_v1_interface, 1);
        }
        // ── 5D.2: cursor-shape-v1 ───────────────────────────────────
        else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
            cursor_shape_manager = (struct wp_cursor_shape_manager_v1*)wl_registry_bind(
                registry, name, &wp_cursor_shape_manager_v1_interface, 1);
        }
        // ── 5D.3: fractional-scale ──────────────────────────────────
        else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
            fractional_scale_manager = (struct wp_fractional_scale_manager_v1*)wl_registry_bind(
                registry, name, &wp_fractional_scale_manager_v1_interface, 1);
        }
        else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
            viewporter = (struct wp_viewporter*)wl_registry_bind(
                registry, name, &wp_viewporter_interface, 1);
        }
        // ── 5D.4: xdg-activation ────────────────────────────────────
        else if (strcmp(interface, xdg_activation_v1_interface.name) == 0) {
            activation = (struct xdg_activation_v1*)wl_registry_bind(
                registry, name, &xdg_activation_v1_interface, 1);
        }
        // ── 5D.5: primary-selection ─────────────────────────────────
        else if (strcmp(interface, zwp_primary_selection_device_manager_v1_interface.name) == 0) {
            primary_selection_manager =
                (struct zwp_primary_selection_device_manager_v1*)wl_registry_bind(
                    registry, name, &zwp_primary_selection_device_manager_v1_interface, 1);
        }
        // ── 5D.6: idle-inhibit ──────────────────────────────────────
        else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
            idle_inhibit_manager = (struct zwp_idle_inhibit_manager_v1*)wl_registry_bind(
                registry, name, &zwp_idle_inhibit_manager_v1_interface, 1);
        }
        // ── 6A: text-input-v3 (IME) ─────────────────────────────────
        else if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
            text_input_manager = (struct zwp_text_input_manager_v3*)wl_registry_bind(
                registry, name, &zwp_text_input_manager_v3_interface, 1);
        }
        // ── 6B: xdg-toplevel-icon ───────────────────────────────────
        else if (strcmp(interface, xdg_toplevel_icon_manager_v1_interface.name) == 0) {
            toplevel_icon_manager = (struct xdg_toplevel_icon_manager_v1*)wl_registry_bind(
                registry, name, &xdg_toplevel_icon_manager_v1_interface, 1);
        }
        else if (strcmp(interface, wl_seat_interface.name) == 0) {
            uint32_t bind_ver = (version > 7) ? 7 : version;
            seat = (wl_seat*)wl_registry_bind(
                registry, name, &wl_seat_interface, bind_ver);
        }
        else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
            data_device_manager = (struct wl_data_device_manager*)wl_registry_bind(
                registry, name, &wl_data_device_manager_interface,
                (version < 3 ? version : 3));
        }
        else if (strcmp(interface, wl_output_interface.name) == 0) {
            auto mon = std::make_unique<knst_monitor>();
            mon->wl_output_id = name;

            uint32_t bind_ver = (version > 3) ? 3 : version;
            mon->output = (wl_output*)wl_registry_bind(
                registry, name, &wl_output_interface, bind_ver);

            // unique_ptr: monitor objects stay at a fixed heap address.
            // Even if the vector grows, the listener user-data pointer
            // (&*mon) stays valid — no UAF.
            knst_monitor* raw_ptr = mon.get();
            knst_display::m_monitor_list.push_back(std::move(mon));

            if (raw_ptr->output) {
                wl_output_add_listener(
                    raw_ptr->output,
                    &knst_display::wayland_output_listener,
                    raw_ptr   // stable address
                );
            }
        }
    }

    static inline void RegistryRemove(void*, wl_registry*, uint32_t name) noexcept {
        // When a monitor is removed (laptop dock, screen off) the
        // compositor sends this event. If we don't remove it from the
        // list, a dangling wl_output pointer remains → UAF.
        auto& list = knst_display::m_monitor_list;
        for (size_t i = 0; i < list.size(); ++i) {
            if (!list[i] || list[i]->wl_output_id != name) continue;

            if (list[i]->output) {
                if (wl_output_get_version(list[i]->output) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
                    wl_output_release(list[i]->output);
                else
                    wl_output_destroy(list[i]->output);
            }
            const bool was_primary = list[i]->is_primary;
            list[i] = std::move(list.back());
            list.pop_back();
            if (was_primary && !list.empty() && list[0]) {
                list[0]->is_primary = true;
            }
            return;
        }
    }

    static inline const wl_registry_listener registryListener = {
        RegistryAdd,
        RegistryRemove
    };
#endif

#if defined(KNST_USING_PLATFORM_ANDROID)
    friend KNST_FORCE_INLINE void cmd_callback(struct android_app* app,  int32_t cmd);
                                               
    friend KNST_FORCE_INLINE int32_t input_callback(struct android_app* app,  AInputEvent* event);

    friend class knst_display;
    static inline struct android_app* m_app = nullptr;
#endif

    KNST_FORCE_INLINE static uint32_t get_current_time_ms() noexcept {
        using namespace std::chrono;
        return (uint32_t)duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count();
    }

#if KNST_USING_PLATFORM_WINDOWS
    KNST_FORCE_INLINE static HINSTANCE& get_windows_native_instance_handle() noexcept {
        return m_hInstance;
    }
#elif KNST_USING_LINUX_PLATFORM_X11
    KNST_FORCE_INLINE static xcb_window_t& get_native_x11_root_handle() noexcept {
        return m_root;
    }
    KNST_FORCE_INLINE static xcb_connection_t* get_native_x11_connection_handle() noexcept {
        return m_connection;
    }
    KNST_FORCE_INLINE static Display* get_native_x11_display() noexcept {
        return m_xlib_display;
    }
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    static inline wl_display* wayland_display = nullptr;
#endif

#if defined(KNST_USING_PLATFORM_ANDROID)
    KNST_FORCE_INLINE static android_app* get_android_app() noexcept {
        return m_app;
    }
#endif

#if defined(KNST_USING_PLATFORM_ANDROID)
    KNST_FORCE_INLINE static void Init(struct android_app* app) noexcept {
        knst_redirect_stdio_to_logcat();   // std::cout / stderr → logcat
#else
    KNST_FORCE_INLINE static void Init() noexcept {
#endif

        // Xlib thread-safety: must run BEFORE XOpenDisplay. GL/Vulkan
        // drivers (especially NVIDIA) call Xlib from another thread;
        // without XInitThreads this races.
#if KNST_USING_LINUX_PLATFORM_X11
        XInitThreads();
#endif

        // ─── Runtime detection (dual mode only) ─────────────────────
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
        s_active_backend = detect_backend();
        if (s_active_backend == knst_backend::none) return;
#elif KNST_USING_PLATFORM_WINDOWS
        s_active_backend = knst_backend::windows;
#elif KNST_USING_LINUX_PLATFORM_X11
        s_active_backend = knst_backend::x11;
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
        s_active_backend = knst_backend::wayland;
#elif defined(KNST_USING_PLATFORM_ANDROID)
        // The `app->destroyRequested = 0` line was REMOVED. native_app_glue
        // freshly callocs the android_app struct in ANativeActivity_onCreate
        // for every Activity instance, so the flag is already 0. Forcing
        // it to 0 would erase a real destroy and the Init loop would wait
        // forever.
        s_active_backend = knst_backend::android;
#endif

        // No CSD auto-init — SDL3-style. CSD activates when the user
        // calls set_csd_hit_test(). Otherwise the OS title bar is used.

#if KNST_USING_PLATFORM_WINDOWS
        // Enable Per-Monitor V2 DPI awareness BEFORE creating any window.
        // Otherwise Windows bitmap-scales the window on HiDPI displays
        // (blurry) and reports wrong client sizes.
        {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32) {
                typedef BOOL (WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE);
                auto fn = (PFN_SetProcessDpiAwarenessContext)
                    GetProcAddress(user32, "SetProcessDpiAwarenessContext");
                if (fn) {
                    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4
                    fn((HANDLE)-4);
                }
            }
        }

        m_hInstance = GetModuleHandleW(nullptr);
        m_wc.cbSize = sizeof(WNDCLASSEXW);
        m_wc.lpfnWndProc = load_native_to_knst_event;
        m_wc.hInstance = m_hInstance;
        m_wc.lpszClassName = L"KnstWindowClass";
        m_wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        m_wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        m_wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_OWNDC;
        RegisterClassExW(&m_wc);

#elif KNST_USING_LINUX_PLATFORM_X11
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
        if (s_active_backend != knst_backend::x11) {
            goto knst_skip_x11_init;
        }
#endif
        {
        // Ignore SIGPIPE — Wayland clipboard / DnD pipe writes must
        // not kill the process if the reader closes. Process-global,
        // idempotent.
        signal(SIGPIPE, SIG_IGN);

        // Locale setup is REQUIRED for XIM (X Input Method). Without
        // it XOpenIM returns null → IME (Turkish, Japanese, …) doesn't
        // work. Process-global, only needed once.
        setlocale(LC_ALL, "");
        XSetLocaleModifiers("");

        m_xlib_display = XOpenDisplay(nullptr);
        if (!m_xlib_display) {
            s_active_backend = knst_backend::none;
            return;
        }

        Bool supported = False;
        XkbSetDetectableAutoRepeat(m_xlib_display, True, &supported);

        XSetEventQueueOwner(m_xlib_display, XCBOwnsEventQueue);
        m_connection = XGetXCBConnection(m_xlib_display);
        if (!m_connection || xcb_connection_has_error(m_connection)) {
            XCloseDisplay(m_xlib_display);
            m_xlib_display = nullptr;
            m_connection = nullptr;
            s_active_backend = knst_backend::none;
            return;
        }

        // Open the global XIM once. Continue even if it's null — the
        // basic keyboard still works, only IME is disabled.
        m_xim = XOpenIM(m_xlib_display, nullptr, nullptr, nullptr);

        // Initialize XSync (needed before using any XSync atom/counter).
        int xsync_ev = 0, xsync_err = 0;
        XSyncQueryExtension(m_xlib_display, &xsync_ev, &xsync_err);
        XSyncInitialize(m_xlib_display, &xsync_ev, &xsync_err);

        // ── 6C: XInput2 (raw mouse + touch) ─────────────────────────
        // XI 2.2 is required: touch events (TouchBegin/Update/End)
        // arrived in that version. The old code asked for 2.0, so
        // touch never worked.
        m_xi_opcode = -1;
        if (XQueryExtension(m_xlib_display, "XInputExtension",
                            &m_xi_opcode, &m_xi_event_base, &m_xi_error_base)) {
            int xi_major = 2, xi_minor = 2;
            if (XIQueryVersion(m_xlib_display, &xi_major, &xi_minor) != Success) {
                // Fall back to 2.0 (raw mouse still works, touch doesn't).
                xi_major = 2; xi_minor = 0;
                if (XIQueryVersion(m_xlib_display, &xi_major, &xi_minor) != Success) {
                    m_xi_opcode = -1;
                }
            }
        }

        const xcb_setup_t* setup = xcb_get_setup(m_connection);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
        xcb_screen_t* screen = iter.data;
        m_root = screen->root;

        // ── 6D: XRandR monitor change events ────────────────────────
        xcb_randr_select_input(
            m_connection, screen->root,
            XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);

        {
            const xcb_query_extension_reply_t* rr =
                xcb_get_extension_data(m_connection, &xcb_randr_id);
            m_randr_event_base = (rr && rr->present)
                                    ? (int)rr->first_event
                                    : -1;
        }

        auto c1 = xcb_intern_atom(m_connection, 0, strlen("_NET_WM_SYNC_REQUEST"), "_NET_WM_SYNC_REQUEST");
        auto r1 = xcb_intern_atom_reply(m_connection, c1, nullptr);
        if (r1) { m_wmSyncRequest = r1->atom; free(r1); }

        auto c2 = xcb_intern_atom(m_connection, 0, strlen("_NET_WM_SYNC_REQUEST_COUNTER"), "_NET_WM_SYNC_REQUEST_COUNTER");
        auto r2 = xcb_intern_atom_reply(m_connection, c2, nullptr);
        if (r2) { m_wmSyncRequestCounter = r2->atom; free(r2); }

        // Batch intern: send all cookies first, then read replies.
        // Cuts startup round-trips from ~30 to ~2.
        struct knst_atom_req { const char* name; xcb_atom_t* target; };
        knst_atom_req atom_reqs[] = {
            { "WM_DELETE_WINDOW",              &m_wmDelete               },
            { "_NET_MOVERESIZE_WINDOW",        &m_netMoveResizeWindow    },
            { "_NET_WM_STATE",                 &m_NET_WM_STATE           },
            { "_NET_WM_STATE_FULLSCREEN",      &m_NET_WM_STATE_FULLSCREEN},
            { "_NET_WM_STATE_MAXIMIZED_VERT",  &m_NET_WM_STATE_MAXIMIZED_VERT },
            { "_NET_WM_STATE_MAXIMIZED_HORZ",  &m_NET_WM_STATE_MAXIMIZED_HORZ },
            { "_NET_WM_STATE_HIDDEN",          &m_NET_WM_STATE_HIDDEN    },
            { "WM_CHANGE_STATE",               &m_wmChangeState          },
            { "_NET_ACTIVE_WINDOW",            &m_NET_ACTIVE_WINDOW      },
            { "WM_PROTOCOLS",                  &m_wmProtocols            },
            { "_MOTIF_WM_HINTS",               &m_MOTIF_WM_HINTS         },
            { "CLIPBOARD",                     &m_CLIPBOARD              },
            { "UTF8_STRING",                   &m_UTF8_STRING            },
            { "TARGETS",                       &m_TARGETS                },
            { "ATOM",                          &m_ATOM                   },
            { "PRIMARY",                       &m_PRIMARY                },
            { "_KNST_CLIPBOARD_DATA",          &m_clipboard_property     },
            { "_KNST_PRIMARY_DATA",            &m_primary_property       },
            { "_NET_WM_MOVERESIZE",            &m_NET_WM_MOVERESIZE      },
            { "XdndAware",                     &m_XdndAware              },
            { "XdndEnter",                     &m_XdndEnter              },
            { "XdndPosition",                  &m_XdndPosition           },
            { "XdndStatus",                    &m_XdndStatus             },
            { "XdndDrop",                      &m_XdndDrop               },
            { "XdndFinished",                  &m_XdndFinished           },
            { "XdndSelection",                 &m_XdndSelection          },
            { "XdndActionCopy",                &m_XdndActionCopy         },
            { "XdndTypeList",                  &m_XdndTypeList           },
            { "text/uri-list",                 &m_textUriList            },
            { "XdndLeave",                     &m_XdndLeave              },
        };

        constexpr int atom_count =
            sizeof(atom_reqs) / sizeof(atom_reqs[0]);

        xcb_intern_atom_cookie_t atom_cookies[atom_count];
        for (int ai = 0; ai < atom_count; ++ai) {
            atom_cookies[ai] = xcb_intern_atom(
                m_connection, 0,
                (uint16_t)strlen(atom_reqs[ai].name),
                atom_reqs[ai].name);
        }
        for (int ai = 0; ai < atom_count; ++ai) {
            auto* r = xcb_intern_atom_reply(
                m_connection, atom_cookies[ai], nullptr);
            if (r) {
                *atom_reqs[ai].target = r->atom;
                free(r);
            }
        }

        m_keysyms = xcb_key_symbols_alloc(m_connection);
        }   // ← end of X11 init block
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
        knst_skip_x11_init:;
#endif
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
        if (s_active_backend != knst_backend::wayland) {
            goto knst_skip_wayland_init;
        }
#endif
        {
        // Ignore SIGPIPE (idempotent).
        signal(SIGPIPE, SIG_IGN);

        wayland_display = wl_display_connect(nullptr);
        if (!wayland_display) {
            s_active_backend = knst_backend::none;
            return;
        }

        registery = wl_display_get_registry(wayland_display);
        if (!registery) {
            wl_display_disconnect(wayland_display);
            wayland_display = nullptr;
            s_active_backend = knst_backend::none;
            return;
        }

        wl_registry_add_listener(
            registery,
            &registryListener,
            nullptr
        );

        wl_display_roundtrip(wayland_display);

        if (!compositor || !wmBase) {
            // The old code just returned, which leaked the connection
            // and the registry and left s_active_backend = wayland.
            // Full cleanup:
            if (registery) {
                wl_registry_destroy(registery);
                registery = nullptr;
            }
            wl_display_disconnect(wayland_display);
            wayland_display = nullptr;
            s_active_backend = knst_backend::none;
            return;
        }

        xdg_wm_base_add_listener(wmBase, &knst_window_wayland_funcs::wmBaseListener, nullptr);

        if (seat) {
            wl_seat_add_listener(
                seat,
                &knst_window_wayland_funcs::seatListener,
                nullptr
            );
        }

        xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

        wl_display_roundtrip(wayland_display);

        if (data_device_manager && seat) {
            data_device = wl_data_device_manager_get_data_device(data_device_manager, seat);
            if (data_device) {
                wl_data_device_add_listener(data_device, &knst_window_wayland_funcs::dataDeviceListener, nullptr);
            }
        }

        // 5D.5: primary-selection device (middle-click paste)
        if (primary_selection_manager && seat) {
            primary_selection_device =
                zwp_primary_selection_device_manager_v1_get_device(
                    primary_selection_manager, seat);
            if (primary_selection_device) {
                zwp_primary_selection_device_v1_add_listener(
                    primary_selection_device,
                    &knst_window_wayland_funcs::primarySelectionDeviceListener,
                    nullptr);
            }
        }

        wl_display_roundtrip(wayland_display);
        wl_display_roundtrip(wayland_display);

        if (shm) {
            cursor_theme = wl_cursor_theme_load(nullptr, 24, shm);
        }

        if (cursor_theme) {
            cursor_default = wl_cursor_theme_get_cursor(cursor_theme, "default");
            cursor_ew = wl_cursor_theme_get_cursor(cursor_theme, "ew-resize");
            cursor_ns = wl_cursor_theme_get_cursor(cursor_theme, "ns-resize");
            cursor_nwse = wl_cursor_theme_get_cursor(cursor_theme, "nwse-resize");
            cursor_nesw = wl_cursor_theme_get_cursor(cursor_theme, "nesw-resize");
            cursor_surface = wl_compositor_create_surface(compositor);
        }
        }   // ← end of Wayland init block
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
        knst_skip_wayland_init:;
#endif
#endif

#if defined(KNST_USING_PLATFORM_ANDROID)
        while (app->window == nullptr) {
            int events;
            struct android_poll_source* source = nullptr;

            // The old code used ALooper_pollOnce(0, ...) — timeout 0
            // burned 100% CPU until the window arrived. Use -1 (block)
            // or a 10 ms timeout.
            int pollResult = ALooper_pollOnce(-1, nullptr, &events, (void**)&source);
                                              
            if (pollResult >= 0) {
                if (source) {
                    source->process(app, source);
                }
            }

            if (app->destroyRequested != 0) {
                // App closed before we even got a window. Null m_app
                // and reset the backend.
                s_active_backend = knst_backend::none;
                return;
            }
        }

        m_app = app;
        // Window size is set inside `knst_window::creation()`
        // (knst_window_android_manager.hpp) — that code has access to
        // `m_knst_event`. Here we only remember the app pointer.
        // The (void) calls are just a reminder; the UI thread prepares
        // the window.
        (void)ANativeWindow_getWidth(app->window);
        (void)ANativeWindow_getHeight(app->window);
        knst_mobile_keyboard::Init(app);
#endif

        knst_display::refresh_screens();
    }

    KNST_FORCE_INLINE static void CleanUp() noexcept {

#if KNST_USING_PLATFORM_WINDOWS
        if (m_hInstance) {
            m_hInstance = nullptr;
        }

#elif KNST_USING_LINUX_PLATFORM_X11
        if (m_keysyms) {
            xcb_key_symbols_free(m_keysyms);
            m_keysyms = nullptr;
        }

        // Clear the cursor cache BEFORE XCloseDisplay — otherwise the
        // cached Cursor handles dangle.
        knst_x11_cursor_cache::clear_all();

        // Close the global XIM. Must be before XCloseDisplay, otherwise
        // the XIM dangles.
        if (m_xim) {
            XCloseIM(m_xim);
            m_xim = nullptr;
        }

        if (m_xlib_display) {
            XCloseDisplay(m_xlib_display);
            m_xlib_display = nullptr;
        }

        m_connection = nullptr;

        m_root                        = 0;
        m_wmDelete                    = 0;
        m_netMoveResizeWindow         = 0;
        m_NET_WM_STATE                = 0;
        m_NET_WM_STATE_FULLSCREEN     = 0;
        m_NET_WM_STATE_MAXIMIZED_VERT = 0;
        m_NET_WM_STATE_MAXIMIZED_HORZ = 0;
        m_NET_WM_STATE_HIDDEN         = 0;
        m_wmChangeState               = 0;
        m_NET_ACTIVE_WINDOW           = 0;
        m_wmProtocols                 = 0;
        m_CLIPBOARD          = 0;
        m_UTF8_STRING        = 0;
        m_TARGETS            = 0;
        m_ATOM               = 0;
        m_PRIMARY            = 0;
        m_clipboard_property = 0;
        m_primary_property   = 0;
        m_NET_WM_MOVERESIZE = 0;
        m_XdndAware         = 0;
        m_XdndEnter         = 0;
        m_XdndPosition      = 0;
        m_XdndStatus        = 0;
        m_XdndDrop          = 0;
        m_XdndFinished      = 0;
        m_XdndSelection     = 0;
        m_XdndActionCopy    = 0;
        m_XdndTypeList      = 0;
        m_textUriList       = 0;
        m_XdndLeave         = 0;

        // The old code didn't reset these — after CleanUp, a new Init
        // would start with the old values.

        m_xi_opcode             = -1;
        m_xi_event_base         = 0;
        m_xi_error_base         = 0;
        m_NET_WM_MOVERESIZE     = 0;
        m_wmSyncRequest         = XCB_NONE;
        m_wmSyncRequestCounter  = XCB_NONE;
        m_randr_event_base      = -1;
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
        // =============================================================
        //  Cleanup order (Wayland protocol correctness):
        //  1. Per-window children (destroyed in window.destroy())
        //  2. Directly created objects (surfaces, sources, offers)
        //  3. Seat children (pointer, keyboard)
        //  4. Registry-bound singletons (all of them)
        //  5. Registry itself
        //  6. Display (last)
        //
        //  CRITICAL: proxy children MUST be destroyed BEFORE their
        //  parent (registry). Otherwise the child's internal parent
        //  link dangles and destroy() segfaults.
        // =============================================================

        active_window = nullptr;
        keyboard_focus_window = nullptr;
        drag_target_window = nullptr;

        // ── 1. Monitors (bound from registry) ───────────────────────
        for (auto& mon : knst_display::m_monitor_list) {
            if (mon && mon->output) {
                // Version 3+ prefers wl_output_release (gentler destroy —
                // clears pending events).
                if (wl_output_get_version(mon->output) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
                    wl_output_release(mon->output);
                else
                    wl_output_destroy(mon->output);
                mon->output = nullptr;
            }
        }
        knst_display::m_monitor_list.clear();

        // ── 2. Seat children ────────────────────────────────────────
        if (touch) {
            wl_touch_release(touch);
            touch = nullptr;
        }
        // Also clear touch_surface — no dangling.
        touch_surface = nullptr;
        if (pointer) {
            wl_pointer_release(pointer);
            pointer = nullptr;
        }
        if (keyboard) {
            wl_keyboard_release(keyboard);
            keyboard = nullptr;
        }

        // ── 3. XKB state ────────────────────────────────────────────
        if (xkb_st)  { xkb_state_unref(xkb_st);    xkb_st  = nullptr; }
        if (xkb_map) { xkb_keymap_unref(xkb_map);  xkb_map = nullptr; }
        if (xkb_ctx) { xkb_context_unref(xkb_ctx); xkb_ctx = nullptr; }

        // ── 4. Pointer constraints & relative pointer ──────────────
        if (locked_pointer) {
            zwp_locked_pointer_v1_destroy(locked_pointer);
            locked_pointer = nullptr;
        }
        if (confined_pointer) {
            zwp_confined_pointer_v1_destroy(confined_pointer);
            confined_pointer = nullptr;
        }
        if (pointer_constraints) {
            zwp_pointer_constraints_v1_destroy(pointer_constraints);
            pointer_constraints = nullptr;
        }
        if (relative_pointer) {
            zwp_relative_pointer_v1_destroy(relative_pointer);
            relative_pointer = nullptr;
        }
        if (relative_pointer_manager) {
            zwp_relative_pointer_manager_v1_destroy(relative_pointer_manager);
            relative_pointer_manager = nullptr;
        }

        // ── 5. Cursor surface + theme ───────────────────────────────
        if (cursor_surface) {
            wl_surface_destroy(cursor_surface);
            cursor_surface = nullptr;
        }
        if (cursor_theme) {
            wl_cursor_theme_destroy(cursor_theme);
            cursor_theme = nullptr;
        }
        cursor_default = nullptr;
        cursor_ew = nullptr;
        cursor_ns = nullptr;
        cursor_nwse = nullptr;
        cursor_nesw = nullptr;

        if (clipboard_source) { wl_data_source_destroy(clipboard_source); clipboard_source = nullptr; }
        if (selection_offer) { wl_data_offer_destroy(selection_offer); selection_offer = nullptr; }
        if (drag_offer) { wl_data_offer_destroy(drag_offer); drag_offer = nullptr; }
        if (data_device) { wl_data_device_release(data_device); data_device = nullptr; }

        pending_offer = nullptr;

        // ── 7. Primary selection (5D.5) ─────────────────────────────
        // Don't destroy primary_offer manually — the device does it.
        // Manual destroy → double-free.
        if (primary_source) {
            zwp_primary_selection_source_v1_destroy(primary_source);
            primary_source = nullptr;
        }
        primary_offer = nullptr;

        if (primary_selection_device) {
            zwp_primary_selection_device_v1_destroy(primary_selection_device);
            primary_selection_device = nullptr;
        }

        // ── 8. Idle-inhibit (5D.6) ─────────────────────────────────
        if (idle_inhibitor) { zwp_idle_inhibitor_v1_destroy(idle_inhibitor); idle_inhibitor = nullptr; }

        // ── 8b. text-input-v3 manager (6A) ─────────────────────────
        if (text_input_manager) {
            zwp_text_input_manager_v3_destroy(text_input_manager);
            text_input_manager = nullptr;
        }

        // ── 8c. toplevel-icon manager (6B) ─────────────────────────
        if (toplevel_icon_manager) {
            xdg_toplevel_icon_manager_v1_destroy(toplevel_icon_manager);
            toplevel_icon_manager = nullptr;
        }

        // ── 9. Cursor shape (5D.2) ─────────────────────────────────
        if (cursor_shape_device) {
            wp_cursor_shape_device_v1_destroy(cursor_shape_device);
            cursor_shape_device = nullptr;
        }

        // ── 10. Registry-bound managers (children of registry) ─────
        if (primary_selection_manager) {
            zwp_primary_selection_device_manager_v1_destroy(primary_selection_manager);
            primary_selection_manager = nullptr;
        }
        if (idle_inhibit_manager) {
            zwp_idle_inhibit_manager_v1_destroy(idle_inhibit_manager);
            idle_inhibit_manager = nullptr;
        }
        if (decoration_manager) {
            zxdg_decoration_manager_v1_destroy(decoration_manager);
            decoration_manager = nullptr;
        }
        if (cursor_shape_manager) {
            wp_cursor_shape_manager_v1_destroy(cursor_shape_manager);
            cursor_shape_manager = nullptr;
        }
        if (fractional_scale_manager) {
            wp_fractional_scale_manager_v1_destroy(fractional_scale_manager);
            fractional_scale_manager = nullptr;
        }
        if (viewporter) {
            wp_viewporter_destroy(viewporter);
            viewporter = nullptr;
        }
        if (activation) {
            xdg_activation_v1_destroy(activation);
            activation = nullptr;
        }
        if (data_device_manager) {
            wl_data_device_manager_destroy(data_device_manager);
            data_device_manager = nullptr;
        }
        if (seat) {
            wl_seat_release(seat);
            seat = nullptr;
        }
        if (shm) {
            wl_shm_destroy(shm);
            shm = nullptr;
        }
        if (wmBase) {
            xdg_wm_base_destroy(wmBase);
            wmBase = nullptr;
        }
        if (compositor) {
            wl_compositor_destroy(compositor);
            compositor = nullptr;
        }

        // ── 11. Registry (parent of everything above) ──────────────
        if (registery) {
            wl_registry_destroy(registery);
            registery = nullptr;
        }

        // ── 12. Display (last) ─────────────────────────────────────
        if (wayland_display) {
            // Events read from the socket but not dispatched can stay
            // in the queue → 96-byte leak. wl_display_roundtrip()
            // dispatches all pending events and drains the queue.
            //
            // Two rounds: the first reads + dispatches from the socket,
            // the second handles anything that arrived meanwhile.
            for (int i = 0; i < 2; ++i) {
                if (wl_display_roundtrip(wayland_display) < 0) break;
            }

            wl_display_flush(wayland_display);
            wl_display_disconnect(wayland_display);
            wayland_display = nullptr;
        }
#endif



#if defined(KNST_USING_PLATFORM_ANDROID)
        // Block that fixes a native_app_glue deadlock:
        //
        //   The UI thread synchronizes lifecycle transitions (onPause,
        //   onStop, onDestroy, onNativeWindowDestroyed) with the
        //   native thread's APP_CMD_* processing:
        //
        //      UI thread:  android_app_set_activity_state(...)
        //                     ↓
        //                   pthread_cond_wait     ← BLOCKED
        //                     ↓
        //      native:      ALooper_pollOnce → APP_CMD_PAUSE
        //                     ↓
        //                   android_app_set_activity_state done
        //                     ↓
        //                   pthread_cond_signal   ← UI wakes up
        //
        //   If we return from android_main, the native thread stops
        //   polling the looper → the UI thread waits forever in onPause
        //   → ANR → the phone freezes.
        //
        //   Correct pattern:
        //     1) Call ANativeActivity_finish() (async — just posts a
        //        message to the UI thread, doesn't block)
        //     2) Pump the looper until destroyRequested arrives (even
        //        with null callbacks, native_app_glue updates its own
        //        state — activityState, window, destroyRequested)
        //     3) Then return from android_main

        knst_mobile_keyboard::shutdown();

        if (m_app != nullptr) {
            android_app* app = m_app;

            if (app->destroyRequested == 0 && app->activity != nullptr) {
                ANativeActivity_finish(app->activity);
            }

            // The UI thread is waiting on us — we must process the
            // onPause, TERM_WINDOW, DESTROY commands so the UI thread
            // unblocks.
            //
            // Timeout: 10 seconds for slow devices + transition anims
            // (the previous 3 s could be too short).
            const uint32_t t0 = get_current_time_ms();
            while (app->destroyRequested == 0) {
                int events = 0;
                struct android_poll_source* source = nullptr;

                const int r = ALooper_pollOnce(
                    100, nullptr, &events, (void**)&source);

                if (r >= 0 && source != nullptr) {
                    source->process(app, source);
                }

                if (get_current_time_ms() - t0 > 10000) {
                    __android_log_print(ANDROID_LOG_WARN, "KNST",
                        "[CLEANUP] destroyRequested timeout — "
                        "UI thread may be stuck");
                    break;
                }
            }
        }

        m_app = nullptr;
#endif

        // Reset the backend. Otherwise the user sees is_x11_active() as
        // true after CleanUp, calls destroy(), and hits
        // xcb_destroy_window with m_connection == nullptr.
        s_active_backend = knst_backend::none;
    }
};


#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)

// ============================================================================
//  Runtime backend detection
// ============================================================================
inline knst_backend KnstWindowSources::detect_backend() noexcept {
    // ─── Force overrides ─────────────────────────────────────────────
    if (const char* f = getenv("KNST_WINDOW_FORCE_X11")) {
        if (atoi(f) != 0) return knst_backend::x11;
    }
    if (const char* f = getenv("KNST_WINDOW_FORCE_WAYLAND")) {
        if (atoi(f) != 0) return knst_backend::wayland;
    }

    // ─── Step 1: probe Wayland ──────────────────────────────────────
    {
        const bool wl_session =
            (getenv("WAYLAND_DISPLAY") != nullptr) ||
            (getenv("XDG_SESSION_TYPE") &&
             strcmp(getenv("XDG_SESSION_TYPE"), "wayland") == 0);

        if (wl_session) {
            wl_display* probe = wl_display_connect(nullptr);
            if (probe) {
                // Query required globals (wl_compositor + xdg_wm_base)
                wl_registry* reg = wl_display_get_registry(probe);

                struct Probe {
                    bool has_compositor = false;
                    bool has_xdg_wm     = false;
                } s;

                static const wl_registry_listener lsn = {
                    [](void* data, wl_registry*, uint32_t,
                       const char* iface, uint32_t) {
                        auto* p = static_cast<Probe*>(data);
                        if (!strcmp(iface, "wl_compositor")) p->has_compositor = true;
                        if (!strcmp(iface, "xdg_wm_base"))   p->has_xdg_wm = true;
                    },
                    nullptr
                };
                wl_registry_add_listener(reg, &lsn, &s);
                wl_display_roundtrip(probe);

                // Destroy the registry BEFORE disconnect — otherwise
                // the proxy leaks (96 bytes).
                if (reg) {
                    wl_registry_destroy(reg);
                    reg = nullptr;
                }

                wl_display_disconnect(probe);

                if (s.has_compositor && s.has_xdg_wm) {
                    return knst_backend::wayland;
                }
            }
            // Wayland probe failed → fall back to XWayland.
        }
    }

    // ─── Step 2: probe X11 ──────────────────────────────────────────
    {
        if (getenv("DISPLAY")) {
            Display* d = XOpenDisplay(nullptr);
            if (d) { XCloseDisplay(d); return knst_backend::x11; }
        }
    }

    // ─── Step 3: last-resort X11 ────────────────────────────────────
    {
        Display* d = XOpenDisplay(nullptr);
        if (d) { XCloseDisplay(d); return knst_backend::x11; }
    }

    return knst_backend::none;
}

#endif   // KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND


// ============================================================================
//  knst_display::refresh_screens() — dual backend dispatcher
//  (defined here because it depends on KnstWindowSources)
// ============================================================================
inline void knst_display::refresh_screens() noexcept {
    #if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
        if (KnstWindowSources::s_active_backend == knst_backend::x11)
            return refresh_screens_x11();
        if (KnstWindowSources::s_active_backend == knst_backend::wayland)
            return refresh_screens_wayland();
    #elif KNST_USING_PLATFORM_WINDOWS
        return refresh_screens_win32();
    #elif KNST_USING_LINUX_PLATFORM_X11
        return refresh_screens_x11();
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        return refresh_screens_wayland();
    #elif defined(KNST_USING_PLATFORM_ANDROID)
        return refresh_screens_android();
    #endif
}


struct knst_window_event {

    #ifndef KNST_MAX_HELD_KEYS
    #define KNST_MAX_HELD_KEYS 8
    #endif

    struct knst_held_key {
        int32_t key_code = 0;
        int32_t scancode  = 0;
        uint32_t last_key_time = 0;
        uint32_t last_repeat_time = 0;
        bool repeat_initialized = false;
        bool active = false;

        // UTF-8 character produced by the held key (max 4 bytes + NUL).
        // Kept so auto-repeat can re-emit TEXT_INPUT.
        // On X11 the server already sends repeats → utf8_len stays 0.
        char    utf8[8] = {0};
        uint8_t utf8_len = 0;
    };

    #if KNST_USING_PLATFORM_WINDOWS
        friend KNST_FORCE_INLINE LRESULT CALLBACK load_native_to_knst_event(
            HWND, UINT, WPARAM, LPARAM) noexcept;
    #elif KNST_USING_LINUX_PLATFORM_X11
        friend KNST_FORCE_INLINE void load_native_to_knst_event(
            knst_window&, xcb_generic_event_t*) noexcept;
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        friend class knst_window_wayland_funcs;
        friend class knst_window;
    #elif defined(KNST_USING_PLATFORM_ANDROID)
        friend KNST_FORCE_INLINE int32_t input_callback(
            struct android_app*, AInputEvent*);
    #endif

    friend class knst_window_event_system;
    friend class knst_window;

    #ifndef KNST_WINDOW_KEY_REPEAT_DELAY
        #define KNST_WINDOW_KEY_REPEAT_DELAY 200
    #endif
    #ifndef KNST_WINDOW_KEY_REPEAT_INTERVAL
        #define KNST_WINDOW_KEY_REPEAT_INTERVAL 20
    #endif

    static constexpr uint32_t KEY_REPEAT_DELAY = KNST_WINDOW_KEY_REPEAT_DELAY;
    static constexpr uint32_t KEY_REPEAT_INTERVAL = KNST_WINDOW_KEY_REPEAT_INTERVAL;

    uint32_t type = 0;
    uint32_t timestamp_ms = 0;

    int32_t mods = 0;
    int32_t mouse_x = 0;
    int32_t mouse_y = 0;
    int32_t mouse_root_x = 0;
    int32_t mouse_root_y = 0;
    int32_t window_width = 0;
    int32_t window_height = 0;
    int32_t window_root_x = 0;
    int32_t window_root_y = 0;
    bool is_focused = false;
    bool mouse_on_window = false;
    bool is_full_screen = false;
    bool is_maximized = false;
    bool is_minimized = false;

    union {
        struct {

            int32_t mouse_button;
            int32_t mouse_action;
            int32_t mouse_scroll_delta;
        };
        struct {

            int32_t key_code;
            int32_t scancode;
            int32_t key_action;
        };
    };

    std::shared_ptr<knst_vector<knst_c16string>> drop_files;
    uint32_t drop_count = 0;

    // Touch state accumulators moved into knst_window — Wayland
    // helpers access window->m_active_touches.

    // ── Text input (KNST_WINDOW_EVENT_TEXT_INPUT) ────────────────────
    // UTF-16 text produced by the OS input method (XIM, TSF, xkb).
    // Populated when the platform text-input handler fires.
    // Handles AltGr, dead keys, IME, Shift+digits correctly — which
    // the raw key_code field does NOT.
    knst_c16string text_input;

    // IME preedit (early text). Populated from the Wayland
    // text-input-v3 preedit_string event. An empty string clears the
    // previous preedit (IME cancelled or moved to commit). The app
    // shows this temporarily in its text buffer.
    knst_c16string text_preedit;

    // IME preedit cursor (character index, not byte).
    // -1 means no cursor should be shown (or it's the start).
    int32_t text_preedit_cursor_begin = -1;
    int32_t text_preedit_cursor_end   = -1;

    // TEXT_DELETE_SURROUNDING: how many characters to delete before/after.
    // (int) -1 means invalid / no bound.
    int32_t text_delete_before = 0;
    int32_t text_delete_after  = 0;

    // ── Raw mouse delta (Windows WM_INPUT, Wayland relative-pointer) ─
    int32_t raw_delta_x = 0;
    int32_t raw_delta_y = 0;
    bool is_raw = false;

    // ====================================================================
    //  Touch input (KNST_WINDOW_EVENT_TOUCH) — ALL platforms
    //  Same layout as Android's historical event so code is portable.
    // ====================================================================
    // KNST_MAX_TOUCH_POINTS is now defined at the top of the file.

    int pointer_count = 0; // number of active points
    float pointer_x[KNST_MAX_TOUCH_POINTS] = {0};
    float pointer_y[KNST_MAX_TOUCH_POINTS] = {0};
    int pointer_id[KNST_MAX_TOUCH_POINTS] = {0}; // per-point identifier
    int touch_action = 0; // KNST_WINDOW_TOUCH_ACTION_*

    #if defined(KNST_USING_PLATFORM_ANDROID)
        int content_left = 0;
        int content_top = 0;
        int content_right = 0;
        int content_bottom = 0;
        int orientation = 0;

        char language[4] = {0};
        char country[4] = {0};
        bool is_night_mode = false;
        bool is_low_memory = false;
        float density = 1.0f;
        int screen_width_dp  = 0;
        int screen_height_dp = 0;
        void* saved_state = nullptr;
        size_t saved_state_size = 0;
    #endif

    KNST_FORCE_INLINE void begin(uint32_t ev_type) noexcept {
        type = ev_type;
        timestamp_ms = KnstWindowSources::get_current_time_ms();

        mouse_button = 0;
        mouse_action = 0;
        mouse_scroll_delta = 0;

        key_code = 0;
        scancode = 0;
        key_action = 0;
        drop_files.reset();
        drop_count = 0;
        text_input = u"";
        text_preedit = u"";
        text_preedit_cursor_begin = -1;
        text_preedit_cursor_end   = -1;
        text_delete_before = 0;
        text_delete_after  = 0;

        is_raw = false;
        raw_delta_x = 0;
        raw_delta_y = 0;

        pointer_count = 0;
        touch_action = 0;

    #if defined(KNST_USING_PLATFORM_ANDROID)
        // saved_state is user-managed; the library nulls it but does
        // not free it. Null it here to avoid leaks.
        saved_state = nullptr;
        saved_state_size = 0;

        // Reset the other Android fields too — otherwise old values
        // carry over into the next event.
        content_left = 0;
        content_top = 0;
        content_right = 0;
        content_bottom = 0;
        orientation = 0;
        is_night_mode = false;
        is_low_memory = false;
        // density = 1.0f;   // DO NOT — this one must carry (consistency
        //                     across events)
        // screen_width_dp / screen_height_dp also carry.
        language[0] = '\0';
        country[0]  = '\0';
    #endif
    }
};


template<size_t N>
struct knst_event_ring {
    static_assert((N & (N - 1)) == 0,"KNST event ring size must be a power of two");

    knst_window_event data[N];
    size_t count = 0;
    size_t head = 0;

    KNST_FORCE_INLINE void push(const knst_window_event& ev) noexcept {
        data[head] = ev;
        head = (head + 1) & (N - 1);
        if (count < N) ++count;
    }

    KNST_FORCE_INLINE size_t size() const noexcept { return count; }

    KNST_FORCE_INLINE const knst_window_event& at(size_t i) const noexcept {
        size_t start = (head + N - count) & (N - 1);
        return data[(start + i) & (N - 1)];
    }

    KNST_FORCE_INLINE void clear() noexcept {
        count = 0;
        head  = 0;
    }
};

#if KNST_USING_PLATFORM_WINDOWS
    #include "../windows/knst_display_win32.hpp"
#endif

#if KNST_USING_LINUX_PLATFORM_X11
    #include "../linux/x11/knst_display_x11.hpp"
    #include <xcb/randr.h>
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    #include "../linux/wayland/knst_display_wayland.hpp"
#endif


// ============================================================================
//  knst_window_capabilities — query what the current platform supports
//
//  Use this to write portable code:
//      auto caps = knst_window::capabilities();
//      if (caps.move) window.move(100, 100);
// ============================================================================
struct knst_window_capabilities {
    bool move; // client can move the window
    bool resize_by_user; // user can drag-resize
    bool force_focus; // client can raise & focus
    bool cursor_position; // client can warp cursor
    bool opacity; // per-window alpha
    bool always_on_top; // Z-order control
    bool set_icon; // window icon
    bool input_transparent; // click-through
    bool drag_drop; // OS-level file DnD
    bool text_input; // typed characters (not key codes)
    bool ime; // input method editor (CJK, dead keys)
    bool raw_mouse; // relative motion, no bounds
    bool clipboard; // system clipboard
    bool primary_selection; // X11 middle-click paste
    bool fractional_scale; // 1.25x, 1.5x, ...
    bool vsync_control; // SetSwapInterval is honored
    bool multiple_windows; // >1 window at once
    bool touch_screen; // runtime-detected touch input
    bool idle_inhibit; // client can prevent screen sleep
};

class knst_window {

public:
    // ====================================================================
    //  CSD — Client-Side Decoration (SDL3-style)
    //
    //  The library does NOT draw the title bar. The user draws it
    //  (GL / Vulkan / software).
    //  The user installs a callback with set_csd_hit_test().
    //
    //  The library calls the callback on every left click:
    //    - normal     → regular mouse event (app handles it)
    //    - draggable  → OS moves the window
    //    - resize_*   → OS resizes the window
    //
    //  SDL3 equivalent: SDL_SetWindowHitTest() + SDL_HITTEST_* enum
    // ====================================================================
    enum class csd_hit : uint8_t {
        normal = 0, // app handles it (default)
        draggable,  // OS moves the window
        resize_top_left,
        resize_top,
        resize_top_right,
        resize_right,
        resize_bottom_right,
        resize_bottom,
        resize_bottom_left,
        resize_left,
    };

private:
    // ── Touch screen state ──────────────────────────────────────────
    bool m_touch_supported   = false;   // hardware supports touch
    bool m_touch_enabled     = false;   // user wants touch events
    int  m_max_touch_points  = 0;       // detected maximum contacts

    #if defined(KNST_WINDOW_USE_OPENGL)
        friend class knst_window_opengl_content;
    #endif

    #if defined(KNST_USING_PLATFORM_ANDROID)
        friend KNST_FORCE_INLINE void cmd_callback(struct android_app* app, int32_t cmd);
        friend KNST_FORCE_INLINE int32_t input_callback(struct android_app* app, AInputEvent* event);
    #endif




    [[maybe_unused]] bool m_input_transparent = false;

    knst_window_event m_knst_event;

    knst_window_event::knst_held_key m_held_keys[KNST_MAX_HELD_KEYS];

    knst_event_ring<256> m_events;

    knst_c16string m_title;

    bool m_should_close;

    bool m_disconnected = false;

    // Redraw re-entrancy guard. WM_WINDOWPOSCHANGED / restore() /
    // set_maximized() synchronously call call_redraw_callback().
    // Without a guard, on_frame re-enters, the event loop restarts,
    // and the same key event is processed again → infinite loop.
    bool m_in_redraw = false;




    // GL context lost flag. Set by knst_window_opengl_content on
    // EGL_CONTEXT_LOST. The user reads it via is_gl_context_lost()
    // (reading resets the flag). Atomic — SwapBuffers and the user
    // loop may run on different threads (rare but possible).
    std::atomic<bool> m_gl_context_lost{false};

    float m_opacity;

    void* m_user_data = nullptr;

    knst_function m_redraw_callback;

    // Surface client registry — the list of native surface owners.
    // On TERM_WINDOW / INIT_WINDOW (Android), on destroy (X11), and on
    // surface change (Wayland), the library notifies these clients.
    // The user never has to call ReleaseSurface / RecreateSurface.
    knst_vector<knst_surface_client*> m_surface_clients;

    // clipboard_text was used for both "text we own" and "text we
    // queried", so copying-then-pasting mixed up the content. Two
    // separate fields now:
    knst_c16string clipboard_text;         // last fetched (user queries)
    knst_c16string m_clipboard_owned;      // we own it, WM asks us
    knst_c16string m_primary_owned;        // X11/Wayland primary owner

    [[maybe_unused]] bool m_drag_drop_enabled = false;

    int m_custom_title_bar_height; // -1 default

    // Per-window DPI cache (for get_title_bar_height above).
    mutable float m_cached_dpi     = 0.0f;
    mutable int   m_cached_title_h = 0;

    [[maybe_unused]] bool m_draw_custom_title_bar;

#if defined(KNST_USING_PLATFORM_ANDROID)
    // Android mouse state — scroll accumulator and press/release pairing
    double m_scroll_accum_v = 0.0;
    double m_scroll_accum_h = 0.0;
    int m_last_mouse_button = 0;

    // destroy() double-call guard. The example explicitly calls
    // window.destroy(), then ~knst_window calls destroy() again.
    // The second call would re-trigger unregister_window and
    // notify_window_destroyed.
    bool m_destroyed = false;
#endif

// X11 also needs multi-touch state for pinch — moved out of the
// Wayland-only block.
#if KNST_USING_LINUX_PLATFORM_WAYLAND || KNST_USING_LINUX_PLATFORM_X11
    struct knst_touch_point_state {
        int32_t id = -1;
        float x = 0.0f;
        float y = 0.0f;
    };
    knst_touch_point_state m_active_touches[KNST_MAX_TOUCH_POINTS];
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    int  m_touch_pending_action = 0;
    bool m_touch_frame_dirty = false;
#endif

public:
    double m_mouse_x = 0.0;
    double m_mouse_y = 0.0;

    // knst_window_event_system accesses m_knst_event, m_held_keys and
    // m_events on every platform. Friendship must be unconditional —
    // otherwise Android would deny access.
    friend struct knst_window_event_system;

#if KNST_USING_PLATFORM_WINDOWS
    friend KNST_FORCE_INLINE LRESULT CALLBACK load_native_to_knst_event(
        HWND, UINT, WPARAM, LPARAM) noexcept;

    friend class knst_window_event_system;

    HWND m_window = nullptr;
    HCURSOR m_cursor = nullptr;
    HCURSOR m_system_cursor = nullptr;
    HICON m_icon_big = nullptr;
    wchar_t m_text_high_surrogate = 0;   // for WM_CHAR pairs

    // 5E.1: raw input (FPS-style relative motion)
    bool m_raw_mouse_enabled = false;
    int32_t m_raw_delta_x = 0;
    int32_t m_raw_delta_y = 0;

    // ShowCursor is a COUNTER. Every TRUE/FALSE call changes it.
    // Track the real visibility separately for idempotent cursor modes.
    bool m_cursor_visible = true;
    bool m_cursor_disabled = false;   // ← YENİ: SetCapture/ClipCursor state

    WINDOWPLACEMENT m_prevPlacement = {};

    LONG m_prevStyle = 0;
    IDropTarget* m_drop_target = nullptr;


    // Fullscreen max-size override (PER-WINDOW).
    POINT m_saved_max_size = {};
    bool  m_have_saved_max = false;

    
    int m_wheel_accum  = 0; // per-window high-precision vertical wheel
    int m_hwheel_accum = 0; // per-window high-precision horizontal wheel
    friend class knst_drop_target;

#elif KNST_USING_LINUX_PLATFORM_X11 || KNST_USING_LINUX_PLATFORM_WAYLAND

    #if KNST_USING_LINUX_PLATFORM_X11
        friend KNST_FORCE_INLINE void load_native_to_knst_event(knst_window& window, xcb_generic_event_t* ev) noexcept;

        friend struct knst_window_event_system;
        xcb_window_t m_window = 0;
        xcb_window_t m_xdnd_source = 0;
        xcb_atom_t m_xdnd_selected_type = 0;
        int m_xdnd_version = 5;

        XIC m_xic = nullptr;
        bool m_raw_mouse_enabled = false;
    #endif

    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        friend class knst_window_wayland_funcs;
        friend struct knst_window_event_system;

        wl_surface* m_surface = nullptr;
        xdg_surface* xdgSurface = nullptr;
        xdg_toplevel* toplevel = nullptr;

        static inline knst_vector<knst_window*> s_windows;

        static knst_window* find_from_surface(wl_surface* surface) noexcept;

        resize_edge m_resize_edge = resize_edge::none;
        int32_t m_pending_w = 0;
        int32_t m_pending_h = 0;
        bool m_has_pending_configure = false;

        uint32_t m_pending_ack_serial = 0;
        bool m_has_pending_ack = false;

        uint32_t m_pointer_serial = 0;
        bool m_pointer_pressed = false;
        double m_scroll_accum_v = 0.0;   // per-window high-precision scroll
        double m_scroll_accum_h = 0.0;

        // Raw-motion accumulator. RelativePointerMotion used to call
        // wl_fixed_to_int() which truncates — a slow 0.4 px motion
        // was lost forever. This accumulates fractional values.
        double m_raw_accum_x = 0.0;
        double m_raw_accum_y = 0.0;
        // Axis + axis_discrete double-count guard.
        bool m_discard_axis_this_frame = false;

        knst_byte_string m_pending_cursor_data;
        int m_pending_cursor_w = 0;
        int m_pending_cursor_h  = 0;
        int m_pending_cursor_hot_x = -1;
        int m_pending_cursor_hot_y = -1;
        bool m_has_pending_cursor = false;
        bool m_using_custom_cursor = false;
        void apply_bmp_cursor_now(const knst_byte_string& data, int width, int height,int hot_x, int hot_y) noexcept;

        resize_edge get_resize_edge(double x, double y) const noexcept;

        // Server-side decoration active? true → don't do resize hit
        // test on edges, leave it to the WM.
        bool m_has_ssd = false;

        // ── 5D: Modern Wayland protocol objects ─────────────────────
        struct zxdg_toplevel_decoration_v1* m_decoration = nullptr;
        struct wp_fractional_scale_v1* m_fractional_scale = nullptr;
        struct wp_viewport* m_viewport = nullptr;

        // Per-window fractional scale. With multiple monitors each
        // window can have a different scale. Keeping this global would
        // use the wrong scale when the window moves between monitors.
        int m_fractional_scale_120 = 120;

        // Fullscreen max-size override (PER-WINDOW).
                    // Last user-set max size — used to restore after fullscreen.
        int m_user_max_w = 0;
        int m_user_max_h = 0;


        int m_saved_max_w = 0;
        int m_saved_max_h = 0;
        bool m_have_saved_max = false;

        // ── 6A: text-input-v3 (IME) ─────────────────────────────────
        struct zwp_text_input_v3* m_text_input = nullptr;

        // ── 6B: xdg-toplevel-icon ───────────────────────────────────
        struct xdg_toplevel_icon_v1* m_toplevel_icon = nullptr;

        // ── 5F: Frame callback gating ───────────────────────────────
        // Don't draw when the compositor isn't ready — this keeps
        // eglSwapBuffers from blocking (NVIDIA Wayland issue).
        struct wl_callback* m_frame_cb    = nullptr;
        bool                m_frame_ready = true;
        // Timeout: if the compositor never sends the callback (buggy
        // WM, just before disconnect), don't lock the window —
        // remember when the last request was made.
        uint32_t            m_frame_request_time = 0;
    #endif   // ← closes the X11||Wayland chain

    #if KNST_USING_LINUX_PLATFORM_X11 // sync ack, used inside opengl content
        xcb_sync_counter_t m_syncCounter = XCB_NONE;
        xcb_sync_int64_t m_syncPendingValue{0, 0};
        bool m_syncHasPendingValue = false;
        bool m_syncRequestReceived = false;
        uint32_t m_sync_received_at_ms = 0;   // per-window fallback timer
        bool m_syncConfigured      = false;
        bool m_in_configure        = false;

        // Fullscreen max-size override: when set_maximum_size() was
        // called and the user toggles fullscreen, the WM would clamp
        // the window to the max. We save the user's hints here, strip
        // P_MAX_SIZE while fullscreen, and restore on exit. PER-WINDOW
        // — a static would break multi-window apps.
        xcb_size_hints_t m_saved_normal_hints = {};
        bool m_have_saved_normal_hints = false;
    #endif

#endif   // closes the outer #if WINDOWS / #elif X11||WAYLAND

    // ====================================================================
    //  CSD — Client-Side Decoration state
    //
    //  knst_function only supports void(). Our callback is
    //  csd_hit(const knst_window&, int, int). So we use a "probe
    //  pattern":
    //    1) Library writes coordinates into m_csd_probe_x/y
    //    2) m_csd_hit_invoke() runs (the user callback runs inside)
    //    3) The wrapper lambda writes the result into m_csd_probe_result
    //    4) Library reads m_csd_probe_result
    // ====================================================================
    bool m_csd_enabled = false;
    knst_function m_csd_hit_invoke;         // void() wrapper
    int m_csd_probe_x = 0;         // pending args for callback
    int m_csd_probe_y = 0;
    csd_hit  m_csd_probe_result = csd_hit::normal; // callback result

    // m_title_bar_height is no longer used — merged into
    // m_custom_title_bar_height above. Removed.

    // CSD auto-cursor
    bool m_csd_auto_cursor = true;  // auto cursor on motion event
    csd_hit m_last_csd_hit    = csd_hit::normal;  // cache (avoids redundant
    // set_cursor calls)

    KNST_FORCE_INLINE const knst_c16string& get_clipboard() const noexcept {
        return clipboard_text;
    }

    KNST_FORCE_INLINE void clear_clipboard() noexcept {
        clipboard_text = "";
        m_clipboard_owned = "";
    }

    KNST_FORCE_INLINE const float& get_opacity() const noexcept {
        return m_opacity;
    }

    KNST_FORCE_INLINE void set_title_bar_height(int height) noexcept {
        m_custom_title_bar_height = height;
    }

    KNST_FORCE_INLINE int get_title_bar_height() const noexcept {
        if (m_custom_title_bar_height > 0) {
            return m_custom_title_bar_height;
        }

        constexpr float base_height = 34.0f;
        constexpr float base_dpi    = 96.0f;

        // Wayland: coordinates are already logical. The compositor
        // scales them. Applying DPI would give 34 → 68 on a 2x screen
        // (double scale).
#if KNST_USING_LINUX_PLATFORM_WAYLAND
        if (KnstWindowSources::s_active_backend == knst_backend::wayland) {
            return (int)base_height;
        }
#endif

        const knst_monitor* mon = knst_display::get_primary_monitor();
        if (!mon || mon->dpi_scale <= 0.0f) {
            return (int)base_height;
        }

        // Per-window cache. Recompute if DPI changes.
        if (m_cached_dpi == mon->dpi_scale && m_cached_title_h > 0) {
            return m_cached_title_h;
        }
        m_cached_dpi = mon->dpi_scale;
        const float ratio = mon->dpi_scale / base_dpi;
        int h = (int)(base_height * ratio);
        if (h < 20) h = 20;    // lower bound
        m_cached_title_h = h;
        return h;
    }

#if KNST_USING_PLATFORM_WINDOWS
    KNST_FORCE_INLINE const HWND& get_windows_window_handle() const noexcept {
        return m_window;
    }
#endif

#if KNST_USING_LINUX_PLATFORM_X11
    KNST_FORCE_INLINE const xcb_window_t& get_x11_window_handle() const noexcept {
        return m_window;
    }
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    KNST_FORCE_INLINE const wl_surface* get_wayland_surface_handle() const noexcept {
        return m_surface;
    }
#endif

    template<typename Callback>
    KNST_FORCE_INLINE void set_redraw_callback(Callback&& callback) noexcept {
        auto* self = this;

        m_redraw_callback = [self, cb = std::forward<Callback>(callback)]() {
            cb(*self, self->m_user_data);
        };
    }

    // ====================================================================
    //  call_redraw_callback()
    //
    //  The central point of the frame loop. Correct ordering:
    //
    //    present_pre()          ← Wayland: xdg_surface.ack_configure
    //    user draw (if any)     ← SwapBuffers, eglSwapBuffers, etc.
    //    present_post()         ← X11: _NET_WM_SYNC_REQUEST counter
    //
    //  This way the user calls call_redraw_callback() and the backend
    //  sync happens automatically. GL-less examples also keep X11
    //  resize smooth (present_post always runs).
    //
    //  present_pre() and present_post() are idempotent — multiple
    //  calls in the same frame do work only once.
    // ====================================================================
    KNST_FORCE_INLINE void call_redraw_callback() noexcept {
        if (m_in_redraw) return;
        m_in_redraw = true;
        present_pre();
        if (m_redraw_callback) m_redraw_callback();
        present_post();
        m_in_redraw = false;
    }

    KNST_FORCE_INLINE ~knst_window() noexcept {
        destroy();
    }

#if !defined(KNST_USING_PLATFORM_ANDROID)
    KNST_FORCE_INLINE knst_window(
        int width = 800, int height = 800,
        knst_c16string title = u"Knst_Window",
        int root_x = KNST_WINDOW_DEFAULT, int root_y = KNST_WINDOW_DEFAULT,
        const knst_monitor& monitor = knst_monitor()) noexcept
        : m_title(title),
          m_should_close(false),
          m_opacity(1.0f),
          m_redraw_callback(),
          m_custom_title_bar_height(-1),
          m_draw_custom_title_bar(false) {

        if (monitor.physical_height > 0 && monitor.physical_width > 0) {
            if (root_x < 0 && root_y < 0) {
                root_x = monitor.root_x + (monitor.width  - width)  / 2;
                root_y = monitor.root_y + (monitor.height - height) / 2;
            } else {
                root_x = monitor.root_x + root_x;
                root_y = monitor.root_y + root_y;
            }
        }

        m_knst_event.window_width  = width;
        m_knst_event.window_height = height;
        m_knst_event.window_root_x = root_x;
        m_knst_event.window_root_y = root_y;
    }
#else
    KNST_FORCE_INLINE knst_window(
        int width = KNST_WINDOW_DEFAULT, int height = KNST_WINDOW_DEFAULT,
        knst_c16string title = u"Knst_Window",
        int root_x = KNST_WINDOW_DEFAULT, int root_y = KNST_WINDOW_DEFAULT,
        const knst_monitor& monitor = knst_monitor())
        : m_title(title),
          m_should_close(false),
          m_opacity(1.0f),
          m_user_data(nullptr),
          m_custom_title_bar_height(-1),
          m_draw_custom_title_bar(false) {
        (void)width; (void)height;
        (void)root_x; (void)root_y; (void)monitor;
    }
#endif

    // ====================================================================
    //  Non-copyable AND non-movable.
    //
    //  Why not movable:
    //    - Platform resources (XIC, HGLRC, wl_egl_window, IDropTarget,
    //      viewport, fractional_scale, text_input, frame_cb, …) are
    //      dozens of members — each needs its own rebind.
    //    - Listener user-data pointers (xdg_surface, xdg_toplevel,
    //      wl_callback) point at the old `this`; forgetting a rebind
    //      is a UAF.
    //    - CSD/redraw callback lambdas capture `this`.
    //    - knst_window_event_system, active_window,
    //      keyboard_focus_window, drag_target_window are all raw
    //      pointers.
    //
    //  Safe usage:
    //    knst_window w(...);                              // stack
    //    auto w = std::make_unique<knst_window>(...);     // heap
    // ====================================================================
    knst_window(const knst_window&) = delete;
    knst_window& operator=(const knst_window&) = delete;
    knst_window(knst_window&&) = delete;
    knst_window& operator=(knst_window&&) = delete;

    KNST_FORCE_INLINE size_t event_count() const noexcept {
        return m_events.size();
    }

    KNST_FORCE_INLINE const knst_window_event&
    get_window_event_handle(size_t i) const noexcept {
        return m_events.at(i);
    }

    KNST_FORCE_INLINE void clear_events() noexcept {
        m_events.clear();
    }

    KNST_FORCE_INLINE knst_window_event::knst_held_key*
    find_held_by_scancode(int sc) noexcept {
        for (auto& hk : m_held_keys)
            if (hk.active && hk.scancode == sc) return &hk;
        return nullptr;
    }

    KNST_FORCE_INLINE knst_window_event::knst_held_key*
    add_held_key(int kc, int sc, uint32_t now) noexcept {
        for (auto& hk : m_held_keys) {
            if (!hk.active) {
                hk.key_code           = kc;
                hk.scancode           = sc;
                hk.last_key_time      = now;
                hk.last_repeat_time   = now;
                hk.repeat_initialized = false;
                hk.active             = true;
                hk.utf8[0]            = '\0';
                hk.utf8_len           = 0;
                return &hk;
            }
        }
        return nullptr;
    }

    KNST_FORCE_INLINE void remove_held_key(int sc) noexcept {
        for (auto& hk : m_held_keys)
            if (hk.active && hk.scancode == sc) hk.active = false;
    }

    KNST_FORCE_INLINE void clear_held_keys() noexcept {
        for (auto& hk : m_held_keys) hk.active = false;
    }

    KNST_FORCE_INLINE bool is_key_held(int kc) const noexcept {
        for (const auto& hk : m_held_keys)
            if (hk.active && hk.key_code == kc) return true;
        return false;
    }

    KNST_FORCE_INLINE bool is_caps_lock_on() const noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    #elif KNST_USING_LINUX_PLATFORM_X11
        XkbStateRec state;
        if (XkbGetState(KnstWindowSources::m_xlib_display,
                        XkbUseCoreKbd, &state) == Success) {
            return (state.locked_mods & LockMask) != 0;
        }
        return false;
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        if (!KnstWindowSources::xkb_st || !KnstWindowSources::xkb_map) return false;
        xkb_mod_index_t idx = xkb_keymap_mod_get_index(
            KnstWindowSources::xkb_map, "Lock");
        if (idx == XKB_MOD_INVALID) return false;
        xkb_mod_mask_t locked = xkb_state_serialize_mods(
            KnstWindowSources::xkb_st, XKB_STATE_MODS_LOCKED);
        return (locked & (1u << idx)) != 0;
    #else
        return false;
    #endif
    }

    KNST_FORCE_INLINE bool is_num_lock_on() const noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        return (GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
    #elif KNST_USING_LINUX_PLATFORM_X11
        XkbStateRec state;
        if (XkbGetState(KnstWindowSources::m_xlib_display,
                        XkbUseCoreKbd, &state) == Success) {
            return (state.locked_mods & Mod2Mask) != 0;
        }
        return false;
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        if (!KnstWindowSources::xkb_st || !KnstWindowSources::xkb_map) return false;
        xkb_mod_index_t idx = xkb_keymap_mod_get_index(
            KnstWindowSources::xkb_map, "Mod2");
        if (idx == XKB_MOD_INVALID) return false;
        xkb_mod_mask_t locked = xkb_state_serialize_mods(
            KnstWindowSources::xkb_st, XKB_STATE_MODS_LOCKED);
        return (locked & (1u << idx)) != 0;
    #else
        return false;
    #endif
    }

    KNST_FORCE_INLINE void dispatch_event(const knst_window_event& ev) noexcept {
        if (ev.type == KNST_WINDOW_EVENT_UNKNOWN) return;

        // ── Coalesce consecutive MOTION / RESIZE events ─────────────
        //    Keeps the ring from overflowing during rapid resize or
        //    drag. The ring is 256 entries; a slow frame with 60+
        //    events/s easily fills it and loses important CLOSE /
        //    FOCUS events.
        if (ev.type == KNST_WINDOW_EVENT_MOTION ||
            ev.type == KNST_WINDOW_EVENT_RESIZE ||
            ev.type == KNST_WINDOW_EVENT_EXPOSE) {

            const size_t n = m_events.size();
            if (n > 0) {
                auto& newest = const_cast<knst_window_event&>(
                    m_events.at(n - 1));
                // Raw mouse deltas SUM, they don't overwrite. Otherwise
                // an FPS camera that receives two motions between
                // frames loses the first one → motion under-computed.
                if (newest.type == ev.type && newest.is_raw == ev.is_raw) {
                    if (ev.is_raw) {
                        const int32_t dx = newest.raw_delta_x + ev.raw_delta_x;
                        const int32_t dy = newest.raw_delta_y + ev.raw_delta_y;
                        newest = ev;
                        newest.raw_delta_x = dx;
                        newest.raw_delta_y = dy;
                    } else {
                        newest = ev;
                    }
                    return;
                }
            }
        }

        m_events.push(ev);
    }

    KNST_FORCE_INLINE void dispatch_current_event() noexcept {
        dispatch_event(m_knst_event);
    }

    // ── Wayland: ack the latest pending configure ───────────────────
    // Called from SwapBuffers right before eglSwapBuffers. Acking the
    // newest serial implicitly covers all earlier ones.
    KNST_FORCE_INLINE void wayland_ack_pending_configure() noexcept {
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        if (m_has_pending_ack && xdgSurface) {
            xdg_surface_ack_configure(xdgSurface, m_pending_ack_serial);
            m_has_pending_ack = false;
        }
    #endif
    }

    // ====================================================================
    //  Presentation hooks — renderer-agnostic
    //
    //  Called by the renderer (OpenGL / Vulkan / software) around the
    //  actual present:
    //
    //      window.present_pre();
    //      eglSwapBuffers(...);   // or vkQueuePresentKHR(...)
    //      window.present_post();
    //
    //  This unifies:
    //    - Wayland xdg_surface.ack_configure   (present_pre)
    //    - X11 _NET_WM_SYNC_REQUEST counter    (present_post)
    //  so OpenGL and Vulkan share the same windowing behavior.
    // ====================================================================
    KNST_FORCE_INLINE void present_pre() noexcept {
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        wayland_ack_pending_configure();
    #endif
    }

    // ====================================================================
    //  Wayland viewport / fractional-scale management
    //
    //  These are window/protocol-level — OpenGL, Vulkan, and software
    //  renderers all use them.
    // ====================================================================

    // Wayland: set the viewport destination to the logical size.
    // No-op on other platforms.
    KNST_FORCE_INLINE void apply_wayland_viewport_resize(
        int logical_w, int logical_h) noexcept {
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        if (m_viewport && logical_w > 0 && logical_h > 0) {
            wp_viewport_set_destination(m_viewport, logical_w, logical_h);
        }
    #else
        (void)logical_w; (void)logical_h;
    #endif
    }

    // Physical (buffer) equivalent of a logical width.
    KNST_FORCE_INLINE int wayland_physical_width(int logical_w) const noexcept {
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        // Use the per-window scale. Not the global — a multi-monitor
        // setup can have different scales per window.
        // Protocol rounding: (w * scale + 60) / 120 (round-half-up).
        const int fs = m_fractional_scale_120 > 0 ? m_fractional_scale_120 : 120;
        return (logical_w * fs + 60) / 120;
    #else
        return logical_w;
    #endif
    }

    KNST_FORCE_INLINE int wayland_physical_height(int logical_h) const noexcept {
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        const int fs = m_fractional_scale_120 > 0 ? m_fractional_scale_120 : 120;
        return (logical_h * fs + 60) / 120;
    #else
        return logical_h;
    #endif
    }

    // ====================================================================
    //  Frame callback gating — Wayland only
    //
    //  A new frame is drawn when the compositor is ready. On a covered
    //  window no callback arrives → no drawing → eglSwapBuffers doesn't
    //  block.
    //
    //  Usage (in the main loop):
    //      if (!w.is_frame_ready()) return;
    //      ... draw ...
    //      w.request_frame_callback();
    //      gl.SwapBuffers();
    // ====================================================================
    KNST_FORCE_INLINE bool is_frame_ready() const noexcept {
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        // Skip frame gating during an active resize.
        if (m_has_pending_configure) return true;
        if (m_frame_ready) return true;

        // Timeout: if the compositor doesn't send a callback within
        // 500 ms, consider it "ready". Otherwise a buggy WM / broken
        // connection freezes the app.
        if (m_frame_request_time != 0) {
            const uint32_t now = KnstWindowSources::get_current_time_ms();
            if (now - m_frame_request_time > 500) {
                return true;
            }
        }
        return false;
    #elif defined(KNST_USING_PLATFORM_ANDROID)
        // No native window while backgrounded — don't draw.
        android_app* a = KnstWindowSources::get_android_app();
        return a != nullptr && a->window != nullptr;
    #else
        return true;
    #endif
    }

    KNST_FORCE_INLINE void request_frame_callback() noexcept {
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        if (!m_surface) return;

        if (m_frame_cb) {
            // Already waiting for a callback: refresh the timeout
            // timer, otherwise every frame becomes "ready" after
            // 500 ms.
            m_frame_ready        = false;
            m_frame_request_time = KnstWindowSources::get_current_time_ms();
            return;
        }

        m_frame_ready        = false;
        m_frame_request_time = KnstWindowSources::get_current_time_ms();
        m_frame_cb           = wl_surface_frame(m_surface);

        wl_callback_add_listener(
            m_frame_cb,
            &knst_window_wayland_funcs::frameCallbackListener,
            this);
        // NOTE: commit is done by eglSwapBuffers.
    #endif
    }

    // ====================================================================
    //  capabilities — what the current platform supports
    // ====================================================================
    static knst_window_capabilities capabilities() noexcept {
        knst_window_capabilities c = {};

#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
        // Dual mode: return values based on the runtime backend.
        // The compile-time #elif chain picked the X11 branch, which
        // reported wrong values when Wayland was active at runtime.
        if (KnstWindowSources::s_active_backend == knst_backend::wayland) {
            c.move = false;
            c.resize_by_user = true;
            c.force_focus = false;
            c.cursor_position = false;
            c.opacity = false;
            c.always_on_top = false;
            c.set_icon = (KnstWindowSources::toplevel_icon_manager != nullptr);
            c.input_transparent = true;
            c.drag_drop = true;
            c.text_input = true;
            c.ime = (KnstWindowSources::text_input_manager != nullptr);
            c.raw_mouse = (KnstWindowSources::relative_pointer_manager != nullptr);
            c.clipboard = true;
            c.primary_selection = (KnstWindowSources::primary_selection_manager != nullptr);
            c.fractional_scale = (KnstWindowSources::fractional_scale_manager != nullptr);
            c.vsync_control = true;
            c.multiple_windows = true;
            c.touch_screen = (KnstWindowSources::touch != nullptr);
            c.idle_inhibit = (KnstWindowSources::idle_inhibit_manager != nullptr);
            return c;
        }
        // X11 branch (default)
        c.move = true;
        c.resize_by_user = true;
        c.force_focus = true;
        c.cursor_position = true;
        c.opacity = true;
        c.always_on_top = true;
        c.set_icon = true;
        c.input_transparent = true;
        c.drag_drop = true;
        c.text_input  = true;
        c.ime = true;
        c.raw_mouse = (KnstWindowSources::m_xi_opcode >= 0);
        c.touch_screen = (KnstWindowSources::m_xi_opcode >= 0);
        c.clipboard = true;
        c.primary_selection = true;
        c.fractional_scale = false;
        c.vsync_control = true;
        c.multiple_windows = true;
        c.idle_inhibit = false;
        return c;

#elif KNST_USING_PLATFORM_WINDOWS
        c.move = true;
        c.resize_by_user = true;
        c.force_focus  = true;
        c.cursor_position = true;
        c.opacity = true;
        c.always_on_top = true;
        c.set_icon = true;
        c.input_transparent = true;
        c.drag_drop = true;
        c.text_input  = true;
        c.ime  = true;
        c.raw_mouse = true;
        c.clipboard  = true;
        c.primary_selection = false;
        c.fractional_scale  = true;
        c.vsync_control = true;
        c.multiple_windows  = true;
        c.touch_screen = (GetSystemMetrics(SM_DIGITIZER) & 0x80) != 0;
        c.idle_inhibit = true;   // SetThreadExecutionState

#elif KNST_USING_LINUX_PLATFORM_X11
        c.move = true;
        c.resize_by_user = true;
        c.force_focus = true;
        c.cursor_position = true;
        c.opacity = true;
        c.always_on_top = true;
        c.set_icon  = true;
        c.input_transparent = true;
        c.drag_drop = true;
        c.text_input  = true;
        c.ime = true;
        // Runtime: without XI2, raw mouse and touch don't work.
        c.raw_mouse = (KnstWindowSources::m_xi_opcode >= 0);
        c.touch_screen = (KnstWindowSources::m_xi_opcode >= 0);
        c.clipboard = true;
        c.primary_selection = true;
        c.fractional_scale  = false;  // X11 has no real fractional (integer only)
        c.vsync_control = true;
        c.multiple_windows = true;
        c.idle_inhibit  = false;  // no X11 standard

#elif KNST_USING_LINUX_PLATFORM_WAYLAND
        c.move = false;  // protocol forbids
        c.resize_by_user = true;
        c.force_focus = false;  // compositor decides
        c.cursor_position = false;  // protocol forbids
        c.opacity = false;  // no wp_alpha_modifier yet
        c.always_on_top = false;
        // Runtime: works if the toplevel-icon manager was bound.
        c.set_icon = (KnstWindowSources::toplevel_icon_manager != nullptr);
        c.input_transparent = true;   // region-based
        c.drag_drop = true;
        c.text_input = true;   // via xkb
        // Runtime: works if the text-input-v3 manager was bound.
        c.ime = (KnstWindowSources::text_input_manager != nullptr);
        // Runtime: works if the relative-pointer manager was bound.
        c.raw_mouse = (KnstWindowSources::relative_pointer_manager != nullptr);
        c.clipboard = true;
        // Runtime: works if the primary-selection manager was bound.
        c.primary_selection = (KnstWindowSources::primary_selection_manager != nullptr);
        // Runtime: works if the fractional-scale manager was bound.
        c.fractional_scale  = (KnstWindowSources::fractional_scale_manager != nullptr);
        c.vsync_control = true;   // eglSwapInterval
        c.multiple_windows = true;
        c.touch_screen = (KnstWindowSources::touch != nullptr);
        c.idle_inhibit = (KnstWindowSources::idle_inhibit_manager != nullptr);

#elif defined(KNST_USING_PLATFORM_ANDROID)
        c.move = false;
        c.resize_by_user = false;
        c.force_focus = false;
        c.cursor_position = false;
        c.opacity = false;
        c.always_on_top = false;
        c.set_icon = false;
        c.input_transparent = false;
        c.drag_drop = false;
        c.text_input = true;
        c.ime = true;
        c.raw_mouse = false;
        c.clipboard = true;
        c.primary_selection = false;
        c.fractional_scale = true;
        c.vsync_control = true;
        c.multiple_windows = false;
        c.touch_screen = true;
        c.idle_inhibit = false;  // FLAG_KEEP_SCREEN_ON on the Java side
#endif

        return c;
    }

    KNST_FORCE_INLINE void present_post() noexcept {
#if KNST_USING_LINUX_PLATFORM_X11
        if (m_syncRequestReceived && m_syncHasPendingValue &&
            m_syncCounter != XCB_NONE) {

            const uint32_t now = KnstWindowSources::get_current_time_ms();
            // 5 ms was too short: the WM acked before the resized
            // frame was drawn → resize flicker came back. 100 ms is a
            // safe upper bound for how long the WM can wait.
            const bool timed_out = (now - m_sync_received_at_ms) > 100;

            // Only send the counter after the new-size frame is drawn.
            // For sync requests that don't change size (move etc.),
            // still ack after 50 ms — otherwise the WM waits.
            if (m_syncConfigured || timed_out) {
                xcb_sync_set_counter(
                    KnstWindowSources::m_connection,
                    m_syncCounter,
                    m_syncPendingValue);
                xcb_flush(KnstWindowSources::m_connection);
                m_syncHasPendingValue = false;
                m_syncRequestReceived = false;
                m_syncConfigured      = false;
            }
        }
        m_in_configure = false;
#endif
    }

    // GL context lost flag — the user reads it.
    //
    //    Usage:
    //        if (window.is_gl_context_lost()) {
    //            // Rebuild every GL resource (shader, VAO, VBO, texture)
    //        }
    //
    //    Reading resets the flag — it won't re-fire next frame.
    //
    //    Only triggers on Android/Linux EGL. Always false on Windows.
    KNST_FORCE_INLINE bool is_gl_context_lost() noexcept {
        return m_gl_context_lost.exchange(false,  std::memory_order_relaxed);
    }

    // Library-internal — called by knst_window_opengl_content when
    // EGL_CONTEXT_LOST is caught. Not part of the public API.
    KNST_FORCE_INLINE void notify_gl_context_lost() noexcept {
        m_gl_context_lost.store(true, std::memory_order_relaxed);
    }

    KNST_FORCE_INLINE void should_close() noexcept {
        // Just set the flag. No JNI/lifecycle call here because:
        //
        //    1) should_close() is called from the native thread.
        //    2) AttachCurrentThread from the native thread uses the
        //       same lock as the UI thread → potential deadlock → ANR
        //       → the phone freezes.
        //    3) Calling ANativeActivity_finish early collides with the
        //       UI thread's onPause/onStop/onDestroy teardown.
        //
        //    The right place is KnstWindowSources::CleanUp() — the
        //    very last teardown step.
        if (m_should_close) return;
        m_should_close = true;
    }

    KNST_FORCE_INLINE bool is_should_close() const noexcept {
        if (m_should_close) return true;

    #if defined(KNST_USING_PLATFORM_ANDROID)
        // On Android the OS can close the app on its own:
        //      - user swipes from Recents
        //      - low-memory killer
        //      - system force-stop
        //
        //    In that case app->destroyRequested != 0 and the main loop
        //    must exit. This check is baked in here so user code
        //    doesn't have to write `&& app->destroyRequested == 0`.
        //
        //    Cross-platform: PC doesn't compile this block.
        android_app* app = KnstWindowSources::m_app;
        if (app != nullptr && app->destroyRequested != 0) {
            return true;
        }
    #endif

        return false;
    }

    KNST_FORCE_INLINE const bool& is_disconnected() const noexcept {
        return m_disconnected;
    }

    KNST_FORCE_INLINE void set_user_data(void* data) noexcept {
        m_user_data = data;
    }

    KNST_FORCE_INLINE const void* get_user_data() const noexcept {
        return m_user_data;
    }


    // ─── Surface client register / unregister (internal) ────────────
    //  knst_window_opengl_content and knst_window_vulkan_content call
    //  these during Init. User code never needs to know about them.
    KNST_FORCE_INLINE void register_surface_client(
        knst_surface_client* client) noexcept {
        if (!client) return;
        for (auto* c : m_surface_clients) {
            if (c == client) return;   // already registered
        }
        m_surface_clients.push_back(client);
    }

    KNST_FORCE_INLINE void unregister_surface_client(
        knst_surface_client* client) noexcept {
        if (!client) return;
        const size_t n = m_surface_clients.size();
        for (size_t i = 0; i < n; ++i) {
            if (m_surface_clients[i] == client) {
                m_surface_clients[i] = m_surface_clients.back();
                m_surface_clients.pop_back();
                return;
            }
        }
    }


    // Library-internal: the window is being destroyed, notify all
    // surface clients and clear the list. If the user already
    // destroyed their content before the window, this is nearly a
    // no-op.
    KNST_FORCE_INLINE void notify_window_destroyed() noexcept {
        while (!m_surface_clients.empty()) {
            knst_surface_client* c = m_surface_clients.back();
            m_surface_clients.pop_back();
            if (c) c->on_window_destroyed();
        }
    }


    // Library-internal: notify all surface clients of release/recreate.
    // Called by the Android event manager.
    KNST_FORCE_INLINE void notify_surface_release() noexcept {
        for (auto* c : m_surface_clients) {
            if (c) c->on_surface_release();
        }
    }

    KNST_FORCE_INLINE void notify_surface_recreate() noexcept {
        for (auto* c : m_surface_clients) {
            if (c) c->on_surface_recreate();
        }
    }

    KNST_FORCE_INLINE const knst_c16string& get_title() const noexcept {
        return m_title;
    }

    // ====================================================================
    //  Size accessors — useful inside CSD callbacks
    //  (window sizes tracked from events)
    // ====================================================================
    KNST_FORCE_INLINE int get_width()  const noexcept { return m_knst_event.window_width;  }
    KNST_FORCE_INLINE int get_height() const noexcept { return m_knst_event.window_height; }
    KNST_FORCE_INLINE int get_root_x() const noexcept { return m_knst_event.window_root_x; }
    KNST_FORCE_INLINE int get_root_y() const noexcept { return m_knst_event.window_root_y; }



    // Waiting for a configure? (flash prevention)
    KNST_FORCE_INLINE bool in_configure() const noexcept {
    #if KNST_USING_LINUX_PLATFORM_X11
        return m_in_configure;
    #else
        return false;
    #endif
    }



    // Window state queries — real values from the runtime.
    KNST_FORCE_INLINE bool is_maximized()  const noexcept { return m_knst_event.is_maximized; }
    KNST_FORCE_INLINE bool is_minimized()  const noexcept { return m_knst_event.is_minimized; }
    KNST_FORCE_INLINE bool is_fullscreen() const noexcept { return m_knst_event.is_full_screen; }
    KNST_FORCE_INLINE bool is_focused()    const noexcept { return m_knst_event.is_focused; }



    // ====================================================================
    //  Dual-mode: extra backend-specific method declarations
    // ====================================================================
#if KNST_IS_DUAL_BACKEND
    // X11 backend
    void creation_x11() noexcept;
    void show_x11() noexcept;
    void destroy_x11() noexcept;
    void set_title_x11(const knst_c16string& title) noexcept;
    void move_x11(int root_x, int root_y, const knst_monitor& monitor) noexcept;
    void move_x11(int root_x, int root_y) noexcept;
    void toggle_fullscreen_x11(bool fullscreen) noexcept;
    void set_minimized_x11() noexcept;
    void set_maximized_x11() noexcept;
    void restore_x11() noexcept;
    void hide_x11() noexcept;
    void focus_x11() noexcept;
    void set_cursor_x11(uint16_t cursor_type) noexcept;
    void set_bmp_cursor_x11(const knst_byte_string& data, int width, int height, int hot_x, int hot_y) noexcept;
    void reset_cursor_x11() noexcept;
    void resize_x11(int width, int height) noexcept;
    void set_cursor_mode_x11(int mode) noexcept;
    void set_cursor_pos_on_window_x11(int x, int y) noexcept;
    void set_cursor_pos_global_x11(int root_x, int root_y) noexcept;
    void set_clipboard_x11(const knst_c16string& text) noexcept;
    void request_clipboard_x11() noexcept;
    void set_drag_drop_status_x11(bool enabled) noexcept;
    void set_opacity_x11(float opacity) noexcept;
    void set_attribute_x11(int attribute, bool value) noexcept;
    bool get_attribute_x11(int attribute) const noexcept;
    void apply_bmp_icon_x11(const knst_byte_string& data, int w, int h) noexcept;
    void set_minimum_size_x11(int width, int height) noexcept;
    void set_maximum_size_x11(int width, int height) noexcept;
    void set_raw_mouse_mode_x11(bool enable) noexcept;
    void enable_touch_screen_x11(bool enable) noexcept;
    void set_primary_selection_x11(const knst_c16string& text) noexcept;
    void request_primary_selection_x11() noexcept;
    void set_idle_inhibit_x11(bool enable) noexcept;
    // Note: set_display_affinity is Windows-only — no-op on Linux.

    // Wayland backend
    void creation_wayland() noexcept;
    void show_wayland() noexcept;
    void destroy_wayland() noexcept;
    void set_title_wayland(const knst_c16string& title) noexcept;
    void move_wayland(int root_x, int root_y, const knst_monitor& monitor) noexcept;
    void move_wayland(int root_x, int root_y) noexcept;
    void toggle_fullscreen_wayland(bool fullscreen) noexcept;
    void set_minimized_wayland() noexcept;
    void set_maximized_wayland() noexcept;
    void restore_wayland() noexcept;
    void hide_wayland() noexcept;
    void focus_wayland() noexcept;
    void set_cursor_wayland(uint16_t cursor_type) noexcept;
    void set_bmp_cursor_wayland(const knst_byte_string& data, int width, int height, int hot_x, int hot_y) noexcept;
    void reset_cursor_wayland() noexcept;
    void resize_wayland(int width, int height) noexcept;
    void set_cursor_mode_wayland(int mode) noexcept;
    void set_cursor_pos_on_window_wayland(int x, int y) noexcept;
    void set_cursor_pos_global_wayland(int root_x, int root_y) noexcept;
    void set_clipboard_wayland(const knst_c16string& text) noexcept;
    void request_clipboard_wayland() noexcept;
    void set_drag_drop_status_wayland(bool enabled) noexcept;
    void set_opacity_wayland(float opacity) noexcept;
    void set_attribute_wayland(int attribute, bool value) noexcept;
    bool get_attribute_wayland(int attribute) const noexcept;
    void apply_bmp_icon_wayland(const knst_byte_string& data, int w, int h) noexcept;
    void set_minimum_size_wayland(int width, int height) noexcept;
    void set_maximum_size_wayland(int width, int height) noexcept;
    void set_raw_mouse_mode_wayland(bool enable) noexcept;
    void enable_touch_screen_wayland(bool enable) noexcept;
    void set_idle_inhibit_wayland(bool enable) noexcept;
    void set_primary_selection_wayland(const knst_c16string& text) noexcept;
    void request_primary_selection_wayland() noexcept;

    // CSD — backend-specific implementations
    void csd_handle_hit_x11(csd_hit hit, int x, int y, uint32_t input_serial) noexcept;
    void csd_handle_hit_wayland(csd_hit hit, int x, int y, uint32_t input_serial) noexcept;


#endif

    // ====================================================================
    //  Public API (unchanged names — dispatch to the right backend)
    // ====================================================================


    void creation() noexcept;
    void show() noexcept;
    void destroy() noexcept;
    void set_title(const knst_c16string& title) noexcept;
    void move(int root_x, int root_y, const knst_monitor& monitor) noexcept;
    void move(int root_x, int root_y) noexcept;
    void toggle_fullscreen(bool fullscreen) noexcept;
    void set_minimized() noexcept;
    void set_maximized() noexcept;
    void restore() noexcept;
    void hide() noexcept;
    void focus() noexcept;
    void set_cursor(uint16_t cursor_type) noexcept;
    void set_bmp_cursor(const knst_byte_string& data, int width, int height, int hot_x = -1, int hot_y = -1) noexcept;
    void set_minimum_size(int width = KNST_WINDOW_DEFAULT, int height = KNST_WINDOW_DEFAULT) noexcept;
    void set_maximum_size(int width = KNST_WINDOW_DEFAULT, int height = KNST_WINDOW_DEFAULT) noexcept;
    void reset_cursor() noexcept;
    void resize(int width = KNST_WINDOW_DEFAULT, int height = KNST_WINDOW_DEFAULT) noexcept;
    void set_cursor_mode(int mode) noexcept;
    void set_cursor_pos_on_window(int x = KNST_WINDOW_DEFAULT, int y = KNST_WINDOW_DEFAULT) noexcept;
    void set_cursor_pos_global(int root_x = KNST_WINDOW_DEFAULT, int root_y = KNST_WINDOW_DEFAULT) noexcept;
    void set_clipboard(const knst_c16string& text) noexcept;
    void request_clipboard() noexcept;
    void set_primary_selection(const knst_c16string& text) noexcept;
    void request_primary_selection() noexcept;
    void set_idle_inhibit(bool enable) noexcept;
    void set_drag_drop_status(bool enabled) noexcept;
    void set_opacity(float opacity) noexcept;
    void set_raw_mouse_mode(bool enable) noexcept;
    void set_display_affinity(bool protect) noexcept;



    // ====================================================================
    //  CSD — Client-Side Decoration API (SDL3-style, knst_function-based)
    //
    //  The user installs a callback with set_csd_hit_test().
    //  The library calls it on every left click and triggers the
    //  action. The library NEVER draws a pixel — the drawing belongs
    //  to the app.
    // ====================================================================

    // ─── Callback setup ─────────────────────────────────────────────
    //
    //  Template: accepts any callable (lambda, functor)
    //  Signature: csd_hit(const knst_window&, int x, int y)
    //  SDL3 equivalent: SDL_SetWindowHitTest(win, cb, NULL)
    //
    //  Installing the callback activates CSD automatically.
    //  knst_function only supports void(), so a wrapper lambda is used
    //  inside.
    template<typename F>
    KNST_FORCE_INLINE void set_csd_hit_test(F&& cb) noexcept {
        auto* self = this;
        m_csd_hit_invoke = [self, cb = std::forward<F>(cb)]() mutable {
            self->m_csd_probe_result = cb(
                static_cast<const knst_window&>(*self),
                self->m_csd_probe_x,
                self->m_csd_probe_y);
        };
        m_csd_enabled = true;
    }


    // Remove the callback — CSD turns off, back to the OS title bar.
    KNST_FORCE_INLINE void clear_csd_hit_test() noexcept {
        m_csd_hit_invoke = knst_function{};
        m_csd_enabled = false;
    }


    KNST_FORCE_INLINE bool is_csd_enabled() const noexcept { return m_csd_enabled; }

    // Automatic cursor management (default: on)
    KNST_FORCE_INLINE void set_csd_auto_cursor(bool enable) noexcept {
        m_csd_auto_cursor = enable;
    }


    KNST_FORCE_INLINE bool is_csd_auto_cursor() const noexcept { return m_csd_auto_cursor; }

    // The library calls this on motion events — sets the appropriate
    // cursor if the hit changed. Public in case you want to call it
    // manually.
    void refresh_csd_cursor(int x, int y) noexcept;


    // ─── Hit-test + action ──────────────────────────────────────────

    // Called by the library. No callback → normal.
    csd_hit hit_test_csd(int x, int y) const noexcept;


    // CSD hit → protocol action
    // X11: _NET_WM_MOVERESIZE
    // Wayland: xdg_toplevel_move / xdg_toplevel_resize
    void csd_handle_hit(csd_hit hit, int x, int y, uint32_t input_serial = 0) noexcept;

    // ─── Geometry queries ───────────────────────────────────────────
    // Previously m_title_bar_height (always 0) was read. The correct
    // one is get_title_bar_height() — returns the user's custom height
    // or a value computed from DPI.
    KNST_FORCE_INLINE int get_content_y_offset() const noexcept {
        return get_title_bar_height();
    }
    KNST_FORCE_INLINE int get_content_height() const noexcept {
        int h = m_knst_event.window_height - get_title_bar_height();
        return h > 0 ? h : 0;
    }

    // ====================================================================
    //  Touch screen API (runtime)
    // ====================================================================
    KNST_FORCE_INLINE bool supports_touch() const noexcept {
        return m_touch_supported;
    }
    KNST_FORCE_INLINE bool is_touch_enabled() const noexcept {
        return m_touch_enabled;
    }
    KNST_FORCE_INLINE int max_touch_points() const noexcept {
        return m_max_touch_points;
    }
    void enable_touch_screen(bool enable) noexcept;
    void set_attribute(int attribute, bool value) noexcept;
    bool get_attribute(int attribute) const noexcept;

    void apply_bmp_icon(const knst_byte_string& data, int width, int height) noexcept;

    KNST_FORCE_INLINE void creation_and_show() noexcept {
        creation();
        show();
    }
};


// ============================================================================
//  CSD — Hit-test (called by the library, SDL3-style)
//
//  knst_function only supports void(), so:
//    1) Write the coordinates into m_csd_probe_x/y
//    2) Call m_csd_hit_invoke() → the wrapper runs the user callback
//    3) Read m_csd_probe_result
//
//  const_cast: knst_function::operator() isn't const, but we call it
//  from a const method. The probe members are a mutable "scratchpad".
// ============================================================================
inline knst_window::csd_hit knst_window::hit_test_csd(int x, int y) const noexcept {
    if (!m_csd_enabled || m_csd_hit_invoke.empty()) return csd_hit::normal;

    auto* self = const_cast<knst_window*>(this);

    // Probe args
    self->m_csd_probe_x = x;
    self->m_csd_probe_y = y;
    self->m_csd_probe_result   = csd_hit::normal;

    // Run the user callback
    self->m_csd_hit_invoke();

    // Read the result
    return self->m_csd_probe_result;
}

// ============================================================================
//  CSD — Automatic cursor management
//
//  Called on motion events. Sets the right cursor if the hit_test_csd
//  result changed. No-op if m_csd_auto_cursor is off.
// ============================================================================
inline void knst_window::refresh_csd_cursor(int x, int y) noexcept {
    if (!m_csd_enabled || !m_csd_auto_cursor) return;

    const csd_hit hit = hit_test_csd(x, y);
    if (hit == m_last_csd_hit) return;
    m_last_csd_hit = hit;

    // csd_hit → cursor type
    // Corner diagonals use HRESIZE / VRESIZE. Diagonal values can be
    // added to KNST_WINDOW_CURSOR_* later.
    uint16_t cursor = KNST_WINDOW_CURSOR_ARROW;
    switch (hit) {
        case csd_hit::resize_left:
        case csd_hit::resize_right:
            cursor = KNST_WINDOW_CURSOR_HRESIZE;   // ↔
            break;
        case csd_hit::resize_top:
        case csd_hit::resize_bottom:
            cursor = KNST_WINDOW_CURSOR_VRESIZE;   // ↕
            break;
        // NWSE = ↖↘ diagonal (top-left ↔ bottom-right)
        case csd_hit::resize_top_left:
        case csd_hit::resize_bottom_right:
            cursor = KNST_WINDOW_CURSOR_NWSE_RESIZE;   // ↖↘
            break;
        case csd_hit::resize_top_right:
        case csd_hit::resize_bottom_left:
            cursor = KNST_WINDOW_CURSOR_NESW_RESIZE;   // ↗↙
            break;
        default:
            cursor = KNST_WINDOW_CURSOR_ARROW;
            break;
    }
    set_cursor(cursor);
}


// ============================================================================
//  Linux single-backend: set_display_affinity is Windows-only → no-op
// ============================================================================
#if (KNST_USING_LINUX_PLATFORM_X11 || KNST_USING_LINUX_PLATFORM_WAYLAND) && !KNST_IS_DUAL_BACKEND
inline void knst_window::set_display_affinity(bool protect) noexcept {
    (void)protect;   // Windows-only feature; harmless no-op on Linux.
}
#endif


// ============================================================================
//  DISPATCHERS — dual mode routes to _x11 / _wayland impls
// ============================================================================
#if KNST_IS_DUAL_BACKEND

// Dispatchers — written manually (macro-parameterized functions didn't
// work correctly).

#define KNST_ACTIVE_IS_X11 (KnstWindowSources::s_active_backend == knst_backend::x11)
#define KNST_ACTIVE_IS_WL (KnstWindowSources::s_active_backend == knst_backend::wayland)

// ── No-parameter versions ───────────────────────────────────────────
inline void knst_window::creation() noexcept {
    if (KNST_ACTIVE_IS_X11) return creation_x11();
    if (KNST_ACTIVE_IS_WL)  return creation_wayland();
}
inline void knst_window::show() noexcept {
    if (KNST_ACTIVE_IS_X11) return show_x11();
    if (KNST_ACTIVE_IS_WL)  return show_wayland();
}
inline void knst_window::destroy() noexcept {
    if (KNST_ACTIVE_IS_X11) return destroy_x11();
    if (KNST_ACTIVE_IS_WL)  return destroy_wayland();
}
inline void knst_window::set_minimized() noexcept {
    if (KNST_ACTIVE_IS_X11) return set_minimized_x11();
    if (KNST_ACTIVE_IS_WL)  return set_minimized_wayland();
}
inline void knst_window::set_maximized() noexcept {
    if (KNST_ACTIVE_IS_X11) return set_maximized_x11();
    if (KNST_ACTIVE_IS_WL)  return set_maximized_wayland();
}
inline void knst_window::restore() noexcept {
    if (KNST_ACTIVE_IS_X11) return restore_x11();
    if (KNST_ACTIVE_IS_WL)  return restore_wayland();
}
inline void knst_window::hide() noexcept {
    if (KNST_ACTIVE_IS_X11) return hide_x11();
    if (KNST_ACTIVE_IS_WL)  return hide_wayland();
}
inline void knst_window::focus() noexcept {
    if (KNST_ACTIVE_IS_X11) return focus_x11();
    if (KNST_ACTIVE_IS_WL)  return focus_wayland();
}
inline void knst_window::reset_cursor() noexcept {
    if (KNST_ACTIVE_IS_X11) return reset_cursor_x11();
    if (KNST_ACTIVE_IS_WL)  return reset_cursor_wayland();
}
inline void knst_window::request_clipboard() noexcept {
    if (KNST_ACTIVE_IS_X11) return request_clipboard_x11();
    if (KNST_ACTIVE_IS_WL)  return request_clipboard_wayland();
}

// ── Parameterized versions ─────────────────────────────────────────
inline void knst_window::set_title(const knst_c16string& t) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_title_x11(t);
    if (KNST_ACTIVE_IS_WL)  return set_title_wayland(t);
}
inline void knst_window::toggle_fullscreen(bool fs) noexcept {
    if (KNST_ACTIVE_IS_X11) return toggle_fullscreen_x11(fs);
    if (KNST_ACTIVE_IS_WL)  return toggle_fullscreen_wayland(fs);
}
inline void knst_window::set_cursor(uint16_t c) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_cursor_x11(c);
    if (KNST_ACTIVE_IS_WL)  return set_cursor_wayland(c);
}
inline void knst_window::set_bmp_cursor(const knst_byte_string& d, int w, int h, int hx, int hy) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_bmp_cursor_x11(d, w, h, hx, hy);
    if (KNST_ACTIVE_IS_WL)  return set_bmp_cursor_wayland(d, w, h, hx, hy);
}
inline void knst_window::set_cursor_mode(int m) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_cursor_mode_x11(m);
    if (KNST_ACTIVE_IS_WL)  return set_cursor_mode_wayland(m);
}
inline void knst_window::set_cursor_pos_on_window(int x, int y) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_cursor_pos_on_window_x11(x, y);
    if (KNST_ACTIVE_IS_WL)  return set_cursor_pos_on_window_wayland(x, y);
}
inline void knst_window::set_cursor_pos_global(int x, int y) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_cursor_pos_global_x11(x, y);
    if (KNST_ACTIVE_IS_WL)  return set_cursor_pos_global_wayland(x, y);
}
inline void knst_window::set_clipboard(const knst_c16string& t) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_clipboard_x11(t);
    if (KNST_ACTIVE_IS_WL)  return set_clipboard_wayland(t);
}
inline void knst_window::set_drag_drop_status(bool e) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_drag_drop_status_x11(e);
    if (KNST_ACTIVE_IS_WL)  return set_drag_drop_status_wayland(e);
}
inline void knst_window::set_opacity(float o) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_opacity_x11(o);
    if (KNST_ACTIVE_IS_WL)  return set_opacity_wayland(o);
}
inline void knst_window::set_raw_mouse_mode(bool e) noexcept {
    if (KNST_ACTIVE_IS_X11) return set_raw_mouse_mode_x11(e);
    if (KNST_ACTIVE_IS_WL)  return set_raw_mouse_mode_wayland(e);
}
inline void knst_window::enable_touch_screen(bool e) noexcept {
    if (KNST_ACTIVE_IS_X11) return enable_touch_screen_x11(e);
    if (KNST_ACTIVE_IS_WL)  return enable_touch_screen_wayland(e);
}
inline void knst_window::csd_handle_hit(csd_hit hit, int x, int y, uint32_t input_serial) noexcept {
    if (KNST_ACTIVE_IS_X11) return csd_handle_hit_x11(hit, x, y, input_serial);
    if (KNST_ACTIVE_IS_WL)  return csd_handle_hit_wayland(hit, x, y, input_serial);
}
inline void knst_window::set_display_affinity(bool p) noexcept {
    (void)p;   // Linux: no-op (Windows-only feature)
}


#undef KNST_ACTIVE_IS_X11
#undef KNST_ACTIVE_IS_WL


inline void knst_window::move(int root_x, int root_y, const knst_monitor& monitor) noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return move_x11(root_x, root_y, monitor);
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return move_wayland(root_x, root_y, monitor);
}

inline void knst_window::move(int root_x, int root_y) noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return move_x11(root_x, root_y);
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return move_wayland(root_x, root_y);
}

inline void knst_window::resize(int width, int height) noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return resize_x11(width, height);
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return resize_wayland(width, height);
}

inline void knst_window::set_minimum_size(int width, int height) noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return set_minimum_size_x11(width, height);
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return set_minimum_size_wayland(width, height);
}

inline void knst_window::set_maximum_size(int width, int height) noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return set_maximum_size_x11(width, height);
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return set_maximum_size_wayland(width, height);
}

inline void knst_window::set_attribute(int attribute, bool value) noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return set_attribute_x11(attribute, value);
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return set_attribute_wayland(attribute, value);
}

inline bool knst_window::get_attribute(int attribute) const noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return get_attribute_x11(attribute);
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return get_attribute_wayland(attribute);
    return false;
}

inline void knst_window::apply_bmp_icon(const knst_byte_string& data, int w, int h) noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return apply_bmp_icon_x11(data, w, h);
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return apply_bmp_icon_wayland(data, w, h);
}

inline void knst_window::set_primary_selection(const knst_c16string& text) noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return set_primary_selection_x11(text);
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return set_primary_selection_wayland(text);
#endif
    (void)text;
}

inline void knst_window::request_primary_selection() noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return request_primary_selection_x11();
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return request_primary_selection_wayland();
#endif
}

inline void knst_window::set_idle_inhibit(bool enable) noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return set_idle_inhibit_x11(enable);
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return set_idle_inhibit_wayland(enable);
#endif
    (void)enable;
}



#endif   // KNST_IS_DUAL_BACKEND





#include "knst_window_event_system.hpp"




#if KNST_USING_PLATFORM_WINDOWS
    #include "../windows/knst_window_win32_manager.hpp"
#endif

#if KNST_USING_LINUX_PLATFORM_X11
    #include "../linux/x11/knst_window_x11_event_manager.hpp"
    #include "../linux/x11/knst_window_x11_manager.hpp"
#endif

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    #include "../linux/wayland/knst_window_wayland_event_helper.hpp"
    #include "../linux/wayland/knst_window_wayland_manager.hpp"
#endif

// JNI bridge — real on Android, no-op elsewhere. So user code can call
// knst_android_jni::drain(&window) without #if guards.
#include "../android/knst_window_android_jni.hpp"

#if defined(KNST_USING_PLATFORM_ANDROID)
    #include "../android/knst_window_android_manager.hpp"
    #include "../android/knst_display_android.hpp"
#endif