// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_wayland_manager.hpp
----------------------------

    Function definitions for the Wayland backend.

*/

#pragma once

#include <poll.h>
#include <fcntl.h>
#include <sys/mman.h>

// ─── Anonymous shm fd ──────────────────────────────────────────────────────
// memfd_create when available, otherwise a PID-based unique name. A fixed
// "/knst-cursor" name used to clash between multiple instances and
// cluttered /dev/shm.
inline int knst_create_anon_shm_fd(const char* name) noexcept {
#if defined(MFD_CLOEXEC)

    int fd = memfd_create(name, MFD_CLOEXEC);
    if (fd >= 0) return fd;
#endif

    char path[64];
    snprintf(path, sizeof(path), "/%s-%d", name, (int)getpid());
    int fd2 = shm_open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd2 >= 0) shm_unlink(path);   // remove the name right away — anonymous
    return fd2;
}


#if KNST_USING_LINUX_PLATFORM_WAYLAND

inline void knst_window::KNST_WAYLAND_METHOD(creation)() noexcept
{
    m_surface = wl_compositor_create_surface(KnstWindowSources::compositor);
    s_windows.push_back(this);
    xdgSurface = xdg_wm_base_get_xdg_surface(KnstWindowSources::wmBase, m_surface);
    toplevel = xdg_surface_get_toplevel(xdgSurface);

    knst_byte_string title(m_title);
    xdg_toplevel_set_title(toplevel, reinterpret_cast<const char*>(title.data()));
    xdg_toplevel_set_app_id(toplevel, "KernelNucleusT");

    // xdg-decoration — runtime mode selection.
    if (KnstWindowSources::decoration_manager) {
        m_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(KnstWindowSources::decoration_manager, toplevel);

        // Request CLIENT_SIDE when CSD is on, otherwise SERVER_SIDE.
        if (is_csd_enabled()) {
            zxdg_toplevel_decoration_v1_set_mode( m_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
            m_draw_custom_title_bar = true;
        } else {
            zxdg_toplevel_decoration_v1_set_mode( m_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }

        zxdg_toplevel_decoration_v1_add_listener(
            m_decoration, &knst_window_wayland_funcs::xdgDecorationListener, this);
    } else {
        // No xdg-decoration (GNOME/Mutter and similar): the window
        // opens without a server-side title bar. Install a CSD callback
        // before creation if you need one.
    }

    // fractional-scale (HiDPI).
    //
    // fractional-scale requires viewporter to actually apply the
    // scale. Without wp_viewport, the compositor can't scale the buffer
    // — buffer sized at logical x scale but displayed at logical -> wrong
    // size on HiDPI.
    if (KnstWindowSources::fractional_scale_manager &&
        KnstWindowSources::viewporter) {
        m_fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(KnstWindowSources::fractional_scale_manager, m_surface);
           
        wp_fractional_scale_v1_add_listener(m_fractional_scale, &knst_window_wayland_funcs::fractionalScaleListener, this);
        // Start at 1.0x; the compositor updates via
        // fractional_scale.preferred_scale.
        m_fractional_scale_120 = 120;
    }

    // text-input-v3 (IME).
    if (KnstWindowSources::text_input_manager && KnstWindowSources::seat) {
        m_text_input = zwp_text_input_manager_v3_get_text_input(
            KnstWindowSources::text_input_manager,
            KnstWindowSources::seat);
        if (m_text_input) {
            zwp_text_input_v3_add_listener( m_text_input, &knst_window_wayland_funcs::textInputListener, this);
               
        }
    }

    if (KnstWindowSources::viewporter) {
        m_viewport = wp_viewporter_get_viewport(KnstWindowSources::viewporter, m_surface);
    }

    // cursor-shape-v1 — device is created on demand in set_cursor
    // (needs wl_pointer, which comes later).
    if (KnstWindowSources::cursor_shape_manager && KnstWindowSources::pointer) {
    }

    xdg_surface_add_listener(xdgSurface, &knst_window_wayland_funcs::xdgSurfaceListener, this);
    xdg_toplevel_add_listener(toplevel, &knst_window_wayland_funcs::xdgToplevelListener, this);
    wl_surface_commit(m_surface);

    // Wayland protocol: xdg_surface.ack_configure must be called before
    // the first buffer is attached. Roundtrip to grab the first configure
    // right away — otherwise drawing before calling show() gives
    // "must ack the initial configure".
    wl_display_roundtrip(KnstWindowSources::wayland_display);

#if defined(KNST_WINDOW_ENABLE_TOUCH_SCREEN)
    // Touch screen detection. The seat's capabilities listener
    // (SeatCapabilities) actually enables wl_touch. Here we just set
    // the runtime state.
    if (KnstWindowSources::touch) {
        m_touch_supported  = true;
        m_max_touch_points = 10;
        m_touch_enabled    = true;
    }
#endif

    knst_window_event_system::register_window(this);
}

inline void knst_window::KNST_WAYLAND_METHOD(enable_touch_screen)(bool enable) noexcept {
#if defined(KNST_WINDOW_ENABLE_TOUCH_SCREEN)
    if (!m_touch_supported) return;
    m_touch_enabled = enable;
#endif
}

inline void knst_window::KNST_WAYLAND_METHOD(show)() noexcept
{
    if (!m_surface) return;

    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);

    // Wait for the first xdg_surface.configure before any buffer attach.
    if (xdgSurface) {
        wl_display_roundtrip(KnstWindowSources::wayland_display);
    }

    // hide() attaches a null buffer, which unmaps the toplevel. show()
    // has to kick off the configure cycle again.
    if (m_has_pending_configure) {
        // Configure still pending — do nothing; ack happens in present_pre.
    } else {
        // One more roundtrip to see if a configure arrived.
        wl_display_roundtrip(KnstWindowSources::wayland_display);
    }

    // Best-effort activation.
    focus();
}

inline void knst_window::KNST_WAYLAND_METHOD(destroy)() noexcept
{
    knst_window_event_system::unregister_window(this);

    // Clear the surface client registry.
    notify_window_destroyed();

    if (KnstWindowSources::active_window == this) KnstWindowSources::active_window = nullptr;
    if (KnstWindowSources::keyboard_focus_window == this) KnstWindowSources::keyboard_focus_window = nullptr;
    if (KnstWindowSources::drag_target_window == this) KnstWindowSources::drag_target_window = nullptr;

    // If the Wayland connection is gone, destroying proxies can crash.
    // Skip the proxy destroys after CleanUp — just null the pointers.
    if (KnstWindowSources::wayland_display == nullptr) {
        m_surface = nullptr;
        xdgSurface = nullptr;
        toplevel = nullptr;
        m_viewport = nullptr;
        m_fractional_scale = nullptr;
        m_decoration = nullptr;
        m_text_input = nullptr;
        m_toplevel_icon = nullptr;
        m_frame_cb = nullptr;
        s_windows.erase_value(this);
        return;
    }

    // Drop the global idle inhibitor if this window owns it.
    if (KnstWindowSources::idle_inhibit_owner == this) {
        set_idle_inhibit(false);
    }

    // Drop lock / confine if this window owns them.
    if (KnstWindowSources::locked_pointer &&
        (KnstWindowSources::keyboard_focus_window == this ||
         KnstWindowSources::active_window == this)) {
        zwp_locked_pointer_v1_destroy(KnstWindowSources::locked_pointer);
        KnstWindowSources::locked_pointer = nullptr;
    }
    if (KnstWindowSources::confined_pointer &&
        (KnstWindowSources::keyboard_focus_window == this ||
         KnstWindowSources::active_window == this)) {
        zwp_confined_pointer_v1_destroy(KnstWindowSources::confined_pointer);
        KnstWindowSources::confined_pointer = nullptr;
    }

    // Frame callback cleanup.
    if (m_frame_cb) {
        wl_callback_destroy(m_frame_cb);
        m_frame_cb = nullptr;
    }
    m_frame_ready        = true;
    m_frame_request_time = 0;

    // Text input cleanup.
    if (m_text_input) {
        zwp_text_input_v3_destroy(m_text_input);
        m_text_input = nullptr;
    }

    // Toplevel icon cleanup.
    if (m_toplevel_icon) {
        xdg_toplevel_icon_v1_destroy(m_toplevel_icon);
        m_toplevel_icon = nullptr;
    }

    // Extended protocol objects.
    if (m_viewport) { wp_viewport_destroy(m_viewport); m_viewport = nullptr; }
    if (m_fractional_scale) { wp_fractional_scale_v1_destroy(m_fractional_scale); m_fractional_scale = nullptr; }
    if (m_decoration) { zxdg_toplevel_decoration_v1_destroy(m_decoration); m_decoration = nullptr; }
    // Reset the per-window scale.
    m_fractional_scale_120 = 120;

    if (toplevel) { xdg_toplevel_destroy(toplevel); toplevel   = nullptr; }
    if (xdgSurface) { xdg_surface_destroy(xdgSurface); xdgSurface = nullptr; }
    if (m_surface) { wl_surface_destroy(m_surface); m_surface  = nullptr; }
    s_windows.erase_value(this);
}

inline knst_window* knst_window::find_from_surface(wl_surface* surface) noexcept
{
    for (knst_window* window : s_windows)
        if (window && window->m_surface == surface) return window;
    return nullptr;
}

inline resize_edge knst_window::get_resize_edge(double x, double y) const noexcept
{
    // SSD means the edges belong to the WM — don't hit-test them.
    if (m_has_ssd) return resize_edge::none;

    // Slightly larger hit zone than typical (8-10 px) so corners are
    // easier to grab, especially with high-DPI pointers.
    constexpr double border = 14.0;
    const double width  = static_cast<double>(m_knst_event.window_width);
    const double height = static_cast<double>(m_knst_event.window_height);
    const bool left   = x <= border;
    const bool right  = x >= width  - border;
    const bool top    = y <= border;
    const bool bottom = y >= height - border;
    if (left  && top) return resize_edge::top_left;
    if (right && top) return resize_edge::top_right;
    if (left  && bottom) return resize_edge::bottom_left;
    if (right && bottom) return resize_edge::bottom_right;
    if (left) return resize_edge::left;
    if (right) return resize_edge::right;
    if (top) return resize_edge::top;
    if (bottom) return resize_edge::bottom;
    return resize_edge::none;
}

inline void knst_window::KNST_WAYLAND_METHOD(set_title)(const knst_c16string& title) noexcept
{
    if (!toplevel) return;
    knst_byte_string new_title(title);
    xdg_toplevel_set_title(toplevel, reinterpret_cast<const char*>(new_title.data()));
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_title = title;
}

inline void knst_window::KNST_WAYLAND_METHOD(move)(int root_x, int root_y, const knst_monitor& monitor) noexcept {
    (void)root_x; (void)root_y; (void)monitor;
}
inline void knst_window::KNST_WAYLAND_METHOD(move)(int root_x, int root_y) noexcept {
    (void)root_x; (void)root_y;
}

inline void knst_window::KNST_WAYLAND_METHOD(toggle_fullscreen)(bool fullscreen) noexcept
{
    if (!toplevel) return;

    if (fullscreen) {
        // Save the user's last max size, then clear the hint while
        // fullscreen is active.
        m_saved_max_w = m_user_max_w;
        m_saved_max_h = m_user_max_h;
        m_have_saved_max = true;
        xdg_toplevel_set_max_size(toplevel, 0, 0);
        xdg_toplevel_set_fullscreen(toplevel, nullptr);
    } else {
        xdg_toplevel_unset_fullscreen(toplevel);

        // Restore the user's max size on exit.
        if (m_have_saved_max) {
            xdg_toplevel_set_max_size(toplevel, m_saved_max_w, m_saved_max_h);
            m_have_saved_max = false;
        }
    }

    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_knst_event.is_full_screen = fullscreen;
}

inline void knst_window::KNST_WAYLAND_METHOD(set_minimized)() noexcept
{
    if (!toplevel) return;
    xdg_toplevel_set_minimized(toplevel);
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_knst_event.is_minimized = true;
}

inline void knst_window::KNST_WAYLAND_METHOD(set_maximized)() noexcept
{
    if (!toplevel) return;
    xdg_toplevel_set_maximized(toplevel);
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_knst_event.is_maximized = true;
}

inline void knst_window::KNST_WAYLAND_METHOD(restore)() noexcept
{
    if (!toplevel) return;
    if (m_knst_event.is_maximized)   xdg_toplevel_unset_maximized(toplevel);
    if (m_knst_event.is_full_screen) xdg_toplevel_unset_fullscreen(toplevel);
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
    m_knst_event.is_maximized   = false;
    m_knst_event.is_full_screen = false;
    m_knst_event.is_minimized   = false;
}

inline void knst_window::KNST_WAYLAND_METHOD(hide)() noexcept
{
    if (!m_surface) return;
    wl_surface_attach(m_surface, nullptr, 0, 0);
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::KNST_WAYLAND_METHOD(focus)() noexcept
{
    // xdg-activation-v1 (best-effort).
    //
    // Priority:
    //   1. XDG_ACTIVATION_TOKEN env var (set by launchers)
    //   2. Recent user input serial
    //   3. Surface-only hint
    //
    // Compositors with strict focus-stealing prevention (COSMIC,
    // Hyprland, etc.) may ignore requests without a launcher-provided
    // token.
    if (!toplevel || !m_surface) return;

    // Launcher-provided token.
    const char* env_token = getenv("XDG_ACTIVATION_TOKEN");
    if (env_token && env_token[0] != '\0') {
        if (KnstWindowSources::activation) {
            xdg_activation_v1_activate(
                KnstWindowSources::activation, env_token, m_surface);
            wl_display_flush(KnstWindowSources::wayland_display);
        }
        unsetenv("XDG_ACTIVATION_TOKEN");
        return;
    }

    // Request a token from the compositor.
    if (!KnstWindowSources::activation) return;

    struct xdg_activation_token_v1* token =
        xdg_activation_v1_get_activation_token(KnstWindowSources::activation);
    if (!token) return;

    if (KnstWindowSources::activation_last_serial != 0) {
        xdg_activation_token_v1_set_serial(
            token, KnstWindowSources::activation_last_serial,
            KnstWindowSources::seat);
    }
    xdg_activation_token_v1_set_surface(token, m_surface);
    xdg_activation_token_v1_add_listener(
        token, &knst_window_wayland_funcs::activationTokenListener, this);
    xdg_activation_token_v1_commit(token);
    wl_display_flush(KnstWindowSources::wayland_display);
    // Token is destroyed by the listener.
}

inline void knst_window::KNST_WAYLAND_METHOD(set_cursor)(uint16_t cursor_type) noexcept
{
    if (!KnstWindowSources::pointer) return;

    // Release any pointer lock left over from DISABLED mode. Without
    // this, C/V/X/Z after J leaves the cursor stuck.
    if (KnstWindowSources::locked_pointer) {
        zwp_locked_pointer_v1_destroy(KnstWindowSources::locked_pointer);
        KnstWindowSources::locked_pointer = nullptr;
    }
    if (KnstWindowSources::confined_pointer) {
        zwp_confined_pointer_v1_destroy(KnstWindowSources::confined_pointer);
        KnstWindowSources::confined_pointer = nullptr;
    }

    // Prefer cursor-shape-v1 (uses the compositor's theme). Fall back
    // to legacy wl_cursor_theme if unavailable.
    if (KnstWindowSources::cursor_shape_manager) {
        if (!KnstWindowSources::cursor_shape_device) {
            KnstWindowSources::cursor_shape_device =
                wp_cursor_shape_manager_v1_get_pointer(
                    KnstWindowSources::cursor_shape_manager,
                    KnstWindowSources::pointer);
        }
        if (KnstWindowSources::cursor_shape_device) {
            uint32_t shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
            switch (cursor_type) {
                case KNST_WINDOW_CURSOR_ARROW: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT; break;
                case KNST_WINDOW_CURSOR_IBEAM: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT; break;
                case KNST_WINDOW_CURSOR_CROSSHAIR: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR; break;
                case KNST_WINDOW_CURSOR_HAND: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER; break;
                case KNST_WINDOW_CURSOR_HRESIZE: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE; break;
                case KNST_WINDOW_CURSOR_VRESIZE: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE; break;
                case KNST_WINDOW_CURSOR_NWSE_RESIZE: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE; break;
                case KNST_WINDOW_CURSOR_NESW_RESIZE: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE; break;
                case KNST_WINDOW_CURSOR_MOVE: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE; break;
                case KNST_WINDOW_CURSOR_WAIT: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT; break;
                case KNST_WINDOW_CURSOR_HELP: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_HELP; break;
                case KNST_WINDOW_CURSOR_NOT_ALLOWED: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED; break;
                default: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT; break;
            }
            wp_cursor_shape_device_v1_set_shape(
                KnstWindowSources::cursor_shape_device,
                m_pointer_serial,
                shape);
            m_using_custom_cursor = false;
            return;
        }
    }

    // Legacy fallback via wl_cursor_theme.
    if (!KnstWindowSources::cursor_surface || !KnstWindowSources::cursor_theme) return;

    const char* cursor_name = "left_ptr";
    switch (cursor_type) {
        case KNST_WINDOW_CURSOR_ARROW: cursor_name = "left_ptr"; break;
        case KNST_WINDOW_CURSOR_IBEAM: cursor_name = "xterm"; break;
        case KNST_WINDOW_CURSOR_CROSSHAIR: cursor_name = "crosshair"; break;
        case KNST_WINDOW_CURSOR_HAND: cursor_name = "hand2"; break;
        case KNST_WINDOW_CURSOR_HRESIZE: cursor_name = "sb_h_double_arrow"; break;
        case KNST_WINDOW_CURSOR_VRESIZE: cursor_name = "sb_v_double_arrow"; break;
        case KNST_WINDOW_CURSOR_NWSE_RESIZE: cursor_name = "nwse-resize"; break;
        case KNST_WINDOW_CURSOR_NESW_RESIZE: cursor_name = "nesw-resize"; break;
        case KNST_WINDOW_CURSOR_MOVE: cursor_name = "fleur"; break;
        case KNST_WINDOW_CURSOR_WAIT: cursor_name = "watch"; break;
        case KNST_WINDOW_CURSOR_HELP: cursor_name = "question_arrow"; break;
        case KNST_WINDOW_CURSOR_NOT_ALLOWED: cursor_name = "X_cursor"; break;
        default: cursor_name = "left_ptr"; break;
    }

    struct wl_cursor* cursor = wl_cursor_theme_get_cursor(KnstWindowSources::cursor_theme, cursor_name);
    if (!cursor || cursor->image_count == 0)
        cursor = wl_cursor_theme_get_cursor(KnstWindowSources::cursor_theme, "left_ptr");
    if (!cursor || cursor->image_count == 0) return;

    struct wl_cursor_image* image = cursor->images[0];
    struct wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) return;

    wl_pointer_set_cursor(KnstWindowSources::pointer, m_pointer_serial, KnstWindowSources::cursor_surface, image->hotspot_x, image->hotspot_y);
    wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
    wl_surface_damage_buffer(KnstWindowSources::cursor_surface, 0, 0, image->width, image->height);
    wl_surface_commit(KnstWindowSources::cursor_surface);
    m_using_custom_cursor = false;
}

inline void knst_window::KNST_WAYLAND_METHOD(reset_cursor)() noexcept
{
    // Coming out of DISABLED mode — drop the locked_pointer.
    if (KnstWindowSources::locked_pointer) {
        zwp_locked_pointer_v1_destroy(KnstWindowSources::locked_pointer);
        KnstWindowSources::locked_pointer = nullptr;
    }
    if (KnstWindowSources::confined_pointer) {
        zwp_confined_pointer_v1_destroy(KnstWindowSources::confined_pointer);
        KnstWindowSources::confined_pointer = nullptr;
    }

    if (KnstWindowSources::pointer && KnstWindowSources::cursor_surface &&
        KnstWindowSources::cursor_default) {
        wl_cursor_image* image = KnstWindowSources::cursor_default->images[0];
        wl_buffer* buffer = wl_cursor_image_get_buffer(image);
        if (buffer) {
            wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
            wl_surface_damage(KnstWindowSources::cursor_surface, 0, 0, image->width, image->height);
            wl_surface_commit(KnstWindowSources::cursor_surface);
            wl_pointer_set_cursor(KnstWindowSources::pointer, m_pointer_serial,
                                  KnstWindowSources::cursor_surface,
                                  image->hotspot_x, image->hotspot_y);
        }
    }

    m_using_custom_cursor = false;
}

inline void knst_window::KNST_WAYLAND_METHOD(set_bmp_cursor)(
    const knst_byte_string& data,
    int width,
    int height,
    int hot_x,
    int hot_y
) noexcept {
    if (!KnstWindowSources::shm || !KnstWindowSources::cursor_surface) {
        return;
    }

    if (data.empty() || width <= 0 || height <= 0) {
        return;
    }

    if (hot_x < 0 || hot_x >= width)  hot_x = width  / 2;
    if (hot_y < 0 || hot_y >= height) hot_y = height / 2;

    const size_t expected_size = (size_t)width * (size_t)height * 4;
    if (data.length() < expected_size) {
        return;
    }

    // Convert RGBA -> BGRA for Wayland's ARGB8888 format.
    uint8_t* converted = new uint8_t[expected_size];
    const uint8_t* src = (const uint8_t*)data.data();

    for (size_t i = 0; i < (size_t)width * height; i++) {
        uint8_t r = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t b = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];

        converted[i * 4 + 0] = b;  // B
        converted[i * 4 + 1] = g;  // G
        converted[i * 4 + 2] = r;  // R
        converted[i * 4 + 3] = a;  // A
    }

    knst_byte_string converted_data = knst_byte_string::take_ownership(converted, expected_size);

    m_pending_cursor_data  = converted_data;
    m_pending_cursor_w     = width;
    m_pending_cursor_h     = height;
    m_pending_cursor_hot_x = hot_x;
    m_pending_cursor_hot_y = hot_y;

    m_using_custom_cursor = true;

    if (!KnstWindowSources::pointer || m_pointer_serial == 0) {
        m_has_pending_cursor = true;
        return;
    }

    apply_bmp_cursor_now(converted_data, width, height, hot_x, hot_y);
}

inline void knst_window::apply_bmp_cursor_now(const knst_byte_string& data, int width, int height, int hot_x, int hot_y) noexcept
{
    int stride = width * 4;
    int size = stride * height;

    int fd = knst_create_anon_shm_fd("knst-cursor");
    if (fd < 0) {
        return;
    }

    if (ftruncate(fd, size) != 0) {
        close(fd);
        return;
    }

    void* shm_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_data == MAP_FAILED) {
        close(fd);
        return;
    }

    const uint8_t* src = data.data();
    uint8_t* dst = (uint8_t*)shm_data;

    for (int i = 0; i < width * height; i++) {
        uint8_t b = src[i*4 + 0];
        uint8_t g = src[i*4 + 1];
        uint8_t r = src[i*4 + 2];
        uint8_t a = src[i*4 + 3];
        dst[i*4 + 0] = (uint8_t)((b * a) / 255);
        dst[i*4 + 1] = (uint8_t)((g * a) / 255);
        dst[i*4 + 2] = (uint8_t)((r * a) / 255);
        dst[i*4 + 3] = a;
    }

    munmap(shm_data, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(KnstWindowSources::shm, fd, size);
    if (!pool) {
        close(fd);
        return;
    }

    struct wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);

    if (!buffer) {
        wl_shm_pool_destroy(pool);
        close(fd);
        return;
    }

    wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
    wl_surface_damage(KnstWindowSources::cursor_surface, 0, 0, width, height);
    wl_surface_commit(KnstWindowSources::cursor_surface);

    wl_pointer_set_cursor(
        KnstWindowSources::pointer,
        m_pointer_serial,
        KnstWindowSources::cursor_surface,
        hot_x,
        hot_y
    );

    wl_display_flush(KnstWindowSources::wayland_display);

    wl_buffer_destroy(buffer);
    wl_shm_pool_destroy(pool);
    close(fd);

    m_using_custom_cursor = true;
}

inline void knst_window::KNST_WAYLAND_METHOD(resize)(int width, int height) noexcept
{
    if (!toplevel || !m_surface) return;
    if (width  <= 0) width  = m_knst_event.window_width;
    if (height <= 0) height = m_knst_event.window_height;

    bool changed = (width  != m_knst_event.window_width) ||
                   (height != m_knst_event.window_height);
    if (!changed) return;

    m_knst_event.window_width  = width;
    m_knst_event.window_height = height;

    // At opacity 1.0 the opaque region was still the old size — update
    // it or the new area is treated as transparent and the compositor
    // doesn't cut the background (ghosting).
    if (m_opacity >= 0.999f && KnstWindowSources::compositor) {
        struct wl_region* region = wl_compositor_create_region(
            KnstWindowSources::compositor);
        if (region) {
            wl_region_add(region, 0, 0, width, height);
            wl_surface_set_opaque_region(m_surface, region);
            wl_region_destroy(region);
        }
    }

    m_knst_event.begin(KNST_WINDOW_EVENT_RESIZE);
    m_knst_event.window_width  = width;
    m_knst_event.window_height = height;
    dispatch_current_event();

    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::KNST_WAYLAND_METHOD(set_cursor_mode)(int mode) noexcept
{
    if (!KnstWindowSources::pointer || !KnstWindowSources::cursor_surface ||
        !KnstWindowSources::cursor_theme) return;

    // Creating two lock_pointers on the same surface triggers
    // "already_constrained" and disconnects us. Two-step approach:
    //   1) If we're already in the desired state, no-op
    //   2) Roundtrip between destroy and create

    const bool want_locked = (mode == KNST_WINDOW_CURSOR_DISABLED);
    const bool is_locked = (KnstWindowSources::locked_pointer   != nullptr);
    const bool is_confined = (KnstWindowSources::confined_pointer != nullptr);

    // Already locked and requesting lock again — no-op.
    if (want_locked && is_locked) {
        return;
    }

    // Release existing constraints.
    if (is_locked) {
        zwp_locked_pointer_v1_destroy(KnstWindowSources::locked_pointer);
        KnstWindowSources::locked_pointer = nullptr;
    }
    if (is_confined) {
        zwp_confined_pointer_v1_destroy(KnstWindowSources::confined_pointer);
        KnstWindowSources::confined_pointer = nullptr;
    }

    // Make sure the compositor processed the destroy. Without the
    // roundtrip the lock_pointer below hits "already_constrained".
    if (is_locked || is_confined) {
        wl_display_roundtrip(KnstWindowSources::wayland_display);
    }

    // DISABLED has its own distinct marker (0x10000) so the comparisons
    // work correctly regardless of order.
    if (mode == KNST_WINDOW_CURSOR_ARROW) {
        struct wl_cursor* cursor = wl_cursor_theme_get_cursor(
            KnstWindowSources::cursor_theme, "left_ptr");
        if (cursor && cursor->image_count > 0) {
            struct wl_cursor_image* image = cursor->images[0];
            struct wl_buffer* buffer = wl_cursor_image_get_buffer(image);
            if (buffer) {
                wl_pointer_set_cursor(KnstWindowSources::pointer, m_pointer_serial,KnstWindowSources::cursor_surface,image->hotspot_x, image->hotspot_y);
                wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
                wl_surface_damage_buffer(KnstWindowSources::cursor_surface, 0, 0,image->width, image->height);
                wl_surface_commit(KnstWindowSources::cursor_surface);
            }
        }
    } else if (mode == KNST_WINDOW_CURSOR_HIDDEN) {
        wl_surface_attach(KnstWindowSources::cursor_surface, nullptr, 0, 0);
        wl_surface_commit(KnstWindowSources::cursor_surface);
        wl_pointer_set_cursor(KnstWindowSources::pointer, m_pointer_serial,KnstWindowSources::cursor_surface, 0, 0);
                              
    } else if (mode == KNST_WINDOW_CURSOR_DISABLED) {
        wl_surface_attach(KnstWindowSources::cursor_surface, nullptr, 0, 0);
        wl_surface_commit(KnstWindowSources::cursor_surface);

        // Wayland protocol: only ONE pointer constraint per surface.
        // Requesting both lock + confine triggers already_constrained
        // and disconnects us. Use lock only.
        if (KnstWindowSources::pointer_constraints && m_surface &&
            !KnstWindowSources::locked_pointer) {
            KnstWindowSources::locked_pointer =
                zwp_pointer_constraints_v1_lock_pointer(
                    KnstWindowSources::pointer_constraints,
                    m_surface,
                    KnstWindowSources::pointer,
                    nullptr,
                    ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        }
    }
}

inline void knst_window::KNST_WAYLAND_METHOD(set_cursor_pos_on_window)(int x, int y) noexcept {
    (void)x; (void)y;
}
inline void knst_window::KNST_WAYLAND_METHOD(set_cursor_pos_global)(int root_x, int root_y) noexcept {
    (void)root_x; (void)root_y;
}

inline void knst_window::KNST_WAYLAND_METHOD(set_primary_selection)(const knst_c16string& text) noexcept {
    if (!KnstWindowSources::primary_selection_manager ||
        !KnstWindowSources::primary_selection_device) return;

    KnstWindowSources::primary_pending_text = text;

    if (KnstWindowSources::primary_source) {
        zwp_primary_selection_source_v1_destroy(KnstWindowSources::primary_source);
    }

    KnstWindowSources::primary_source =
        zwp_primary_selection_device_manager_v1_create_source(
            KnstWindowSources::primary_selection_manager);

    zwp_primary_selection_source_v1_add_listener(
        KnstWindowSources::primary_source,
        &knst_window_wayland_funcs::primarySourceListener,
        nullptr);

    zwp_primary_selection_source_v1_offer(
        KnstWindowSources::primary_source, "text/plain;charset=utf-8");
    zwp_primary_selection_source_v1_offer(
        KnstWindowSources::primary_source, "text/plain");

    // Order matters: the most recent INPUT serial wins.
    uint32_t serial = KnstWindowSources::activation_last_serial;
    if (serial == 0) serial = KnstWindowSources::keyboard_serial;
    if (serial == 0) serial = m_pointer_serial;

    zwp_primary_selection_device_v1_set_selection(
        KnstWindowSources::primary_selection_device,
        KnstWindowSources::primary_source,
        serial);

    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::KNST_WAYLAND_METHOD(request_primary_selection)() noexcept {
    // If we own the primary selection, return our own copy. Reading
    // from our own source would deadlock on the same thread.
    if (!m_primary_owned.empty()) {
        clipboard_text = m_primary_owned;
        m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
        dispatch_current_event();
        return;
    }

    if (!KnstWindowSources::primary_offer) {
        clipboard_text = u"";
        // No source — still emit the event so the app doesn't wait.
        m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
        dispatch_current_event();
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) return;

    zwp_primary_selection_offer_v1_receive(
        KnstWindowSources::primary_offer,
        "text/plain;charset=utf-8",
        pipefd[1]);
    close(pipefd[1]);
    wl_display_flush(KnstWindowSources::wayland_display);

    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    knst_byte_string result;
    char buf[4096];
    bool got_eof = false;
    auto start = std::chrono::steady_clock::now();

    while (!got_eof) {
        while (wl_display_prepare_read(KnstWindowSources::wayland_display) != 0) {
            wl_display_dispatch_pending(KnstWindowSources::wayland_display);
        }
        wl_display_flush(KnstWindowSources::wayland_display);

        struct pollfd pfd;
        pfd.fd     = wl_display_get_fd(KnstWindowSources::wayland_display);
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 10);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            wl_display_read_events(KnstWindowSources::wayland_display);
            wl_display_dispatch_pending(KnstWindowSources::wayland_display);
        } else {
            wl_display_cancel_read(KnstWindowSources::wayland_display);
        }

        while (true) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                result.append(reinterpret_cast<const unsigned char*>(buf),
                              (uint32_t)n);
            } else if (n == 0) {
                got_eof = true;
                break;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                got_eof = true;
                break;
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > 1000) break;
    }
    close(pipefd[0]);

    if (!result.empty()) {
        clipboard_text = knst_c16string(
            reinterpret_cast<const char*>(result.data()), result.length());
    } else {
        clipboard_text = u"";
    }

    // Emit the event — parity with request_clipboard.
    m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
    dispatch_current_event();
}

inline void knst_window::KNST_WAYLAND_METHOD(set_clipboard)(const knst_c16string& text) noexcept {
    if (!KnstWindowSources::data_device_manager || !KnstWindowSources::data_device) return;

    KnstWindowSources::clipboard_pending_text = text;
    // Also track the text we own (symmetric with X11).
    m_clipboard_owned = text;

    if (KnstWindowSources::clipboard_source) {
        wl_data_source_destroy(KnstWindowSources::clipboard_source);
        KnstWindowSources::clipboard_source = nullptr;
    }

    KnstWindowSources::clipboard_source =
        wl_data_device_manager_create_data_source(KnstWindowSources::data_device_manager);

    wl_data_source_add_listener(KnstWindowSources::clipboard_source,
        &knst_window_wayland_funcs::dataSourceListener, nullptr);

    // Offer five mime types — DataSourceSend must handle all of them
    // (XWayland apps ask for UTF8_STRING).
    wl_data_source_offer(KnstWindowSources::clipboard_source, "text/plain;charset=utf-8");
    wl_data_source_offer(KnstWindowSources::clipboard_source, "text/plain");
    wl_data_source_offer(KnstWindowSources::clipboard_source, "UTF8_STRING");
    wl_data_source_offer(KnstWindowSources::clipboard_source, "TEXT");
    wl_data_source_offer(KnstWindowSources::clipboard_source, "STRING");

    // Serial: the most recent INPUT serial, not the enter serial.
    // Mutter/KWin may reject the enter serial; clipboard set_selection
    // requires user interaction.
    uint32_t serial = KnstWindowSources::activation_last_serial;
    if (serial == 0) serial = KnstWindowSources::keyboard_serial;
    if (serial == 0) serial = m_pointer_serial;

    wl_data_device_set_selection(KnstWindowSources::data_device,
        KnstWindowSources::clipboard_source, serial);

    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::KNST_WAYLAND_METHOD(request_clipboard)() noexcept {
    // If we're the current clipboard owner, return our own text.
    // Reading from our own source via wl_data_offer_receive triggers
    // DataSourceSend on the SAME thread — the write() blocks waiting
    // for us to read the pipe we haven't read yet -> deadlock.
    if (!m_clipboard_owned.empty()) {
        clipboard_text = m_clipboard_owned;
        m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
        dispatch_current_event();
        return;
    }

    if (!KnstWindowSources::selection_offer) {
        clipboard_text = u"";
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) return;

    wl_data_offer_receive(KnstWindowSources::selection_offer, "text/plain;charset=utf-8", pipefd[1]);
    close(pipefd[1]);

    wl_display_flush(KnstWindowSources::wayland_display);

    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    knst_byte_string result;
    char buf[4096];
    bool got_eof = false;

    auto start_time = std::chrono::steady_clock::now();

    // The old code set got_data=true after the first chunk and exited —
    // text longer than 4095 bytes was truncated and the source got
    // SIGPIPE. Correct: read until EOF (n==0). Timeout is 1000 ms since
    // clipboard content can be large.
    while (!got_eof) {
        while (wl_display_prepare_read(KnstWindowSources::wayland_display) != 0) {
            wl_display_dispatch_pending(KnstWindowSources::wayland_display);
        }
        wl_display_flush(KnstWindowSources::wayland_display);

        struct pollfd pfd;
        pfd.fd     = wl_display_get_fd(KnstWindowSources::wayland_display);
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 10);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            wl_display_read_events(KnstWindowSources::wayland_display);
            wl_display_dispatch_pending(KnstWindowSources::wayland_display);
        } else {
            wl_display_cancel_read(KnstWindowSources::wayland_display);
        }

        // Read multiple times in a row (pipe may be full).
        while (true) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                result.append(reinterpret_cast<const unsigned char*>(buf),
                              (uint32_t)n);
            } else if (n == 0) {
                got_eof = true;   // done
                break;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;            // no data right now, go back to poll
            } else {
                got_eof = true;   // error -> exit
                break;
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        if (elapsed > 1000) break;   // 100 ms -> 1000 ms
    }

    close(pipefd[0]);

    if (!result.empty()) {
        clipboard_text = knst_c16string(
            reinterpret_cast<const char*>(result.data()), result.length());
    } else {
        clipboard_text = u"";
    }

    // Emit the event.
    m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
    dispatch_current_event();
}

inline void knst_window::KNST_WAYLAND_METHOD(set_drag_drop_status)(bool enabled) noexcept {
    m_drag_drop_enabled = enabled;
}

inline void knst_window::KNST_WAYLAND_METHOD(set_raw_mouse_mode)(bool enable) noexcept {
    // Wayland: raw motion is delivered by relative-pointer when the
    // pointer is LOCKED via zwp_locked_pointer_v1. Enable both together.
    if (enable) {
        if (!KnstWindowSources::pointer_constraints || !m_surface) return;
        if (!KnstWindowSources::locked_pointer) {
            KnstWindowSources::locked_pointer =
                zwp_pointer_constraints_v1_lock_pointer(
                    KnstWindowSources::pointer_constraints,
                    m_surface,
                    KnstWindowSources::pointer,
                    nullptr,
                    ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        }
        // Hide the cursor.
        wl_surface_attach(KnstWindowSources::cursor_surface, nullptr, 0, 0);
        wl_surface_commit(KnstWindowSources::cursor_surface);
    } else {
        if (KnstWindowSources::locked_pointer) {
            zwp_locked_pointer_v1_destroy(KnstWindowSources::locked_pointer);
            KnstWindowSources::locked_pointer = nullptr;
        }
    }
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::KNST_WAYLAND_METHOD(set_idle_inhibit)(bool enable) noexcept {
    if (enable) {
        if (KnstWindowSources::idle_inhibitor) return;
        if (!KnstWindowSources::idle_inhibit_manager) return;
        if (!m_surface) return;

        KnstWindowSources::idle_inhibitor =
            zwp_idle_inhibit_manager_v1_create_inhibitor(
                KnstWindowSources::idle_inhibit_manager, m_surface);
        KnstWindowSources::idle_inhibit_owner = this;
    } else {
        if (KnstWindowSources::idle_inhibitor) {
            zwp_idle_inhibitor_v1_destroy(KnstWindowSources::idle_inhibitor);
            KnstWindowSources::idle_inhibitor = nullptr;
        }
        KnstWindowSources::idle_inhibit_owner = nullptr;
    }
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::KNST_WAYLAND_METHOD(set_opacity)(float opacity) noexcept
{
    if (!m_surface || !KnstWindowSources::compositor) return;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;

    // Without wp_alpha_modifier_v1 (Sept 2023), per-window visual
    // opacity isn't possible at the protocol level on Wayland. The old
    // code only set the opaque region — that's not transparency, it
    // means "this region is fully opaque, cut the background here".
    //
    // m_opacity is still stored; the app can apply its own opacity via
    // premultiplied alpha in a fragment shader.
    m_opacity = opacity;

    // Opaque region only makes sense at 1.0.
    if (opacity >= 0.999f) {
        struct wl_region* region = wl_compositor_create_region(
            KnstWindowSources::compositor);
        if (region) {
            wl_region_add(region, 0, 0,
                          m_knst_event.window_width,
                          m_knst_event.window_height);
            wl_surface_set_opaque_region(m_surface, region);
            wl_region_destroy(region);
        }
    } else {
        wl_surface_set_opaque_region(m_surface, nullptr);
    }
    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::KNST_WAYLAND_METHOD(set_attribute)(int attribute, bool value) noexcept
{
    if (!toplevel) return;
    switch (attribute) {
        case KNST_WINDOW_ATTR_DECORATED: {
            m_draw_custom_title_bar = !value;
            m_has_ssd = value;

            // Tell the compositor — without this, runtime decoration
            // changes don't take effect.
            if (m_decoration) {
                zxdg_toplevel_decoration_v1_set_mode(
                    m_decoration,
                    value ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                          : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
                wl_surface_commit(m_surface);
                wl_display_flush(KnstWindowSources::wayland_display);
            }
            break;
        }

        case KNST_WINDOW_ATTR_RESIZABLE:
            if (!value) {
                xdg_toplevel_set_min_size(toplevel, m_knst_event.window_width, m_knst_event.window_height);
                xdg_toplevel_set_max_size(toplevel, m_knst_event.window_width, m_knst_event.window_height);
            } else {
                xdg_toplevel_set_min_size(toplevel, 300, 250);
                xdg_toplevel_set_max_size(toplevel, 0, 0);
            }
            wl_surface_commit(m_surface);
            wl_display_flush(KnstWindowSources::wayland_display);
            break;

        case KNST_WINDOW_ATTR_ALWAYS_ON_TOP:
            (void)value;
            break;

        case KNST_WINDOW_ATTR_TRANSPARENT:
            if (value) {
                if (m_surface && KnstWindowSources::compositor) {
                    struct wl_region* region = wl_compositor_create_region(KnstWindowSources::compositor);
                    if (region) {
                        #ifndef KNST_DISABLE_TITLE_BAR
                            int titleBarH = get_title_bar_height();
                            wl_region_add(region, 0, 0,
                                          m_knst_event.window_width, titleBarH);
                        #else
                            wl_region_add(region, 0, 0,
                                          m_knst_event.window_width, 5);
                            wl_region_add(region, 0, 0,
                                          5, m_knst_event.window_height);
                            wl_region_add(region, m_knst_event.window_width - 5, 0,
                                          5, m_knst_event.window_height);
                            wl_region_add(region, 0, m_knst_event.window_height - 5,
                                          m_knst_event.window_width, 5);
                        #endif

                        wl_surface_set_input_region(m_surface, region);
                        wl_region_destroy(region);
                        wl_surface_commit(m_surface);
                        wl_display_flush(KnstWindowSources::wayland_display);
                    }
                }
                m_input_transparent = true;
            } else {
                if (m_surface) {
                    wl_surface_set_input_region(m_surface, nullptr);
                    wl_surface_commit(m_surface);
                    wl_display_flush(KnstWindowSources::wayland_display);
                }
                m_input_transparent = false;
            }
            break;
    }
}

inline bool knst_window::KNST_WAYLAND_METHOD(get_attribute)(int attribute) const noexcept
{
    if (!toplevel) return false;
    switch (attribute) {
        case KNST_WINDOW_ATTR_DECORATED:
            return !m_draw_custom_title_bar;
        case KNST_WINDOW_ATTR_RESIZABLE:
            return true;
        case KNST_WINDOW_ATTR_ALWAYS_ON_TOP:
            return false;
        case KNST_WINDOW_ATTR_TRANSPARENT:
            return m_input_transparent;
        default:
            return false;
    }
}

inline void knst_window::KNST_WAYLAND_METHOD(apply_bmp_icon)(const knst_byte_string& bytes, int icon_width, int icon_height) noexcept
{
    // xdg-toplevel-icon-v1. Silent return if unsupported. Image format:
    // RGBA 8888 (unlike set_bmp_cursor which is BGRA — because the
    // protocol uses WL_SHM_FORMAT_ARGB8888, which on little-endian is
    // BGRA).
    if (!KnstWindowSources::toplevel_icon_manager) return;
    if (!toplevel) return;
    if (!KnstWindowSources::shm) return;
    if (bytes.empty() || icon_width <= 0 || icon_height <= 0) return;

    const size_t expected = (size_t)icon_width * icon_height * 4;
    if (bytes.length() < expected) return;

    // Create a wl_shm buffer with the icon pixels.
    const int stride = icon_width * 4;
    const int size   = stride * icon_height;

    int fd = knst_create_anon_shm_fd("knst-icon");
    if (fd < 0) return;

    if (ftruncate(fd, size) != 0) { close(fd); return; }

    void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) { close(fd); return; }

    // Premultiply alpha (Wayland expects premultiplied ARGB8888).
    const uint8_t* src = bytes.data();
    uint8_t* dst = (uint8_t*)mem;
    for (int i = 0; i < icon_width * icon_height; i++) {
        uint8_t r = src[i*4 + 0];
        uint8_t g = src[i*4 + 1];
        uint8_t b = src[i*4 + 2];
        uint8_t a = src[i*4 + 3];
        // WL_SHM_FORMAT_ARGB8888 in little-endian = BGRA byte order.
        dst[i*4 + 0] = (uint8_t)((b * a) / 255);
        dst[i*4 + 1] = (uint8_t)((g * a) / 255);
        dst[i*4 + 2] = (uint8_t)((r * a) / 255);
        dst[i*4 + 3] = a;
    }
    munmap(mem, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(KnstWindowSources::shm, fd, size);
    if (!pool) { close(fd); return; }

    struct wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool, 0, icon_width, icon_height, stride, WL_SHM_FORMAT_ARGB8888);
    if (!buffer) {
        wl_shm_pool_destroy(pool);
        close(fd);
        return;
    }

    // Create or reuse the icon object.
    if (m_toplevel_icon) {
        xdg_toplevel_icon_v1_destroy(m_toplevel_icon);
        m_toplevel_icon = nullptr;
    }

    m_toplevel_icon = xdg_toplevel_icon_manager_v1_create_icon(
        KnstWindowSources::toplevel_icon_manager);

    if (m_toplevel_icon) {
        xdg_toplevel_icon_v1_add_buffer(m_toplevel_icon, buffer, 1);
        xdg_toplevel_icon_manager_v1_set_icon(
            KnstWindowSources::toplevel_icon_manager,
            toplevel,
            m_toplevel_icon);
    }

    wl_buffer_destroy(buffer);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::KNST_WAYLAND_METHOD(set_minimum_size)(int width, int height) noexcept {
    if (!toplevel || !m_surface) return;

    if (width == KNST_WINDOW_DEFAULT) {
        width = 300;
    }
    if (height == KNST_WINDOW_DEFAULT) {
        height = 250;
    }

    xdg_toplevel_set_min_size(toplevel, width, height);

    if (m_knst_event.window_width < width || m_knst_event.window_height < height) {
        int newWidth  = m_knst_event.window_width  < width  ? width  : m_knst_event.window_width;
        int newHeight = m_knst_event.window_height < height ? height : m_knst_event.window_height;
        resize(newWidth, newHeight);
    }

    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

inline void knst_window::KNST_WAYLAND_METHOD(set_maximum_size)(int width, int height) noexcept {
    if (!toplevel || !m_surface) return;

    if (width == KNST_WINDOW_DEFAULT && height == KNST_WINDOW_DEFAULT) {
        m_user_max_w = 0;
        m_user_max_h = 0;
        xdg_toplevel_set_max_size(toplevel, 0, 0);
        wl_surface_commit(m_surface);
        wl_display_flush(KnstWindowSources::wayland_display);
        return;
    }

    int maxWidth = (width  == KNST_WINDOW_DEFAULT) ? 0 : width;
    int maxHeight = (height == KNST_WINDOW_DEFAULT) ? 0 : height;

    // Remember the user's max so fullscreen exit can restore it.
    m_user_max_w = maxWidth;
    m_user_max_h = maxHeight;

    xdg_toplevel_set_max_size(toplevel, maxWidth, maxHeight);

    if (maxWidth > 0 && m_knst_event.window_width > maxWidth) {
        resize(maxWidth, m_knst_event.window_height);
    }
    if (maxHeight > 0 && m_knst_event.window_height > maxHeight) {
        resize(m_knst_event.window_width, maxHeight);
    }

    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

// ─── CSD hit -> Wayland protocol action ────────────────────────────────────
inline void knst_window::KNST_WAYLAND_METHOD(csd_handle_hit)(csd_hit hit, int x, int y, uint32_t input_serial) noexcept {
    (void)x; (void)y;

    if (!toplevel || !KnstWindowSources::seat) return;

    switch (hit) {
        case csd_hit::draggable:
            xdg_toplevel_move(toplevel, KnstWindowSources::seat, input_serial);
            break;
        case csd_hit::resize_top:
            xdg_toplevel_resize(toplevel, KnstWindowSources::seat, input_serial, XDG_TOPLEVEL_RESIZE_EDGE_TOP);
            break;
        case csd_hit::resize_bottom:
            xdg_toplevel_resize(toplevel, KnstWindowSources::seat, input_serial, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM);
            break;
        case csd_hit::resize_left:
            xdg_toplevel_resize(toplevel, KnstWindowSources::seat, input_serial, XDG_TOPLEVEL_RESIZE_EDGE_LEFT);
            break;
        case csd_hit::resize_right:
            xdg_toplevel_resize(toplevel, KnstWindowSources::seat, input_serial, XDG_TOPLEVEL_RESIZE_EDGE_RIGHT);
            break;
        case csd_hit::resize_top_left:
            xdg_toplevel_resize(toplevel, KnstWindowSources::seat, input_serial, XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT);
            break;
        case csd_hit::resize_top_right:
            xdg_toplevel_resize(toplevel, KnstWindowSources::seat, input_serial, XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT);
            break;
        case csd_hit::resize_bottom_left:
            xdg_toplevel_resize(toplevel, KnstWindowSources::seat, input_serial, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT);
            break;
        case csd_hit::resize_bottom_right:
            xdg_toplevel_resize(toplevel, KnstWindowSources::seat, input_serial, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
            break;
        case csd_hit::normal:
        default:
            return;
    }

    wl_surface_commit(m_surface);
    wl_display_flush(KnstWindowSources::wayland_display);
}

#endif