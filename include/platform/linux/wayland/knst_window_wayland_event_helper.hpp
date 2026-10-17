// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_wayland_event_helper.hpp
----------------------------

    Wayland event handling — all the listener bodies live here.

*/

#pragma once
#include <linux/input-event-codes.h>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/mman.h>

#if KNST_USING_LINUX_PLATFORM_WAYLAND

inline void knst_window_wayland_funcs::XdgSurfaceConfigure(
    void* data, xdg_surface* surface, uint32_t serial)
{
    auto* window = static_cast<knst_window*>(data);

    if (!window) {
        xdg_surface_ack_configure(surface, serial);
        return;
    }

    // Remember the serial, ack it right before the next swap. Acking
    // the newest serial implicitly covers all earlier ones.
    window->m_pending_ack_serial = serial;
    window->m_has_pending_ack = true;

    if (window->m_has_pending_configure) {
        window->m_has_pending_configure = false;

        if (window->m_pending_w > 0 && window->m_pending_h > 0) {
            window->m_knst_event.begin(KNST_WINDOW_EVENT_RESIZE);
            window->m_knst_event.window_width = window->m_pending_w;
            window->m_knst_event.window_height = window->m_pending_h;
            window->dispatch_current_event();
        }
        // Ack is deferred — sent from SwapBuffers.
    } else {
        // No size change (state-only configure) — ack immediately.
        window->wayland_ack_pending_configure();
    }
}

inline const xdg_surface_listener
knst_window_wayland_funcs::xdgSurfaceListener =
{
    XdgSurfaceConfigure
};

inline void knst_window_wayland_funcs::XdgToplevelConfigure(
    void* data,
    xdg_toplevel* toplevel,
    int32_t w,
    int32_t h,
    wl_array* states)
{
    (void)toplevel;

    auto* window = static_cast<knst_window*>(data);
    if (!window) return;

    // Save pending size — no dispatch, no commit here.
    if (w > 0 && h > 0) {
        window->m_pending_w = w;
        window->m_pending_h = h;
        window->m_has_pending_configure = true;
    }

    // Viewport destination = logical size (compositor scales the
    // buffer by the fractional-scale factor).
    if (w > 0 && h > 0 && window->m_viewport) {
        wp_viewport_set_destination(window->m_viewport, w, h);
    }

    // State changes: maximized / fullscreen / focus.
    // xdg_toplevel.suspended is optional and not implemented by
    // GNOME/Mutter as of 2024. Deliberately ignore it so we don't get
    // stuck in a "never draw again" state.
    bool maximized = false;
    bool activated = false;
    bool fullscreen = false;

    if (states && states->size > 0) {
        uint32_t* s = (uint32_t*)states->data;
        size_t count = states->size / sizeof(uint32_t);
        for (size_t i = 0; i < count; i++) {
            if (s[i] == XDG_TOPLEVEL_STATE_MAXIMIZED) maximized = true;
            if (s[i] == XDG_TOPLEVEL_STATE_ACTIVATED) activated = true;
            if (s[i] == XDG_TOPLEVEL_STATE_FULLSCREEN) fullscreen = true;
        }
    }

    if (fullscreen != window->m_knst_event.is_full_screen) {
        window->m_knst_event.begin(fullscreen ? KNST_WINDOW_EVENT_FULLSCREEN : KNST_WINDOW_EVENT_RESTORE);
        window->m_knst_event.is_full_screen = fullscreen;
        window->dispatch_current_event();
    }

    if (maximized != window->m_knst_event.is_maximized) {
        window->m_knst_event.begin(maximized ? KNST_WINDOW_EVENT_MAXIMIZE : KNST_WINDOW_EVENT_RESTORE);
        window->m_knst_event.is_maximized = maximized;
        window->dispatch_current_event();
    }

    if (activated != window->m_knst_event.is_focused) {
        window->m_knst_event.begin(activated ? KNST_WINDOW_EVENT_FOCUS_IN : KNST_WINDOW_EVENT_FOCUS_OUT);
        window->m_knst_event.is_focused = activated;
        window->dispatch_current_event();
    }

    // No wl_surface_commit here — that was the old bug.
}

inline void knst_window_wayland_funcs::XdgToplevelClose(
    void* data,
    xdg_toplevel* toplevel)
{
    (void)toplevel;
    auto* window = static_cast<knst_window*>(data);
    if (!window) return;

    window->m_knst_event.begin(KNST_WINDOW_EVENT_CLOSE);
    window->should_close();
    window->dispatch_current_event();
}

inline void knst_window_wayland_funcs::XdgToplevelConfigureBounds(
    void* data,
    xdg_toplevel* toplevel,
    int32_t width,
    int32_t height)
{
    (void)data; (void)toplevel; (void)width; (void)height;
}

inline void knst_window_wayland_funcs::XdgToplevelWmCapabilities(
    void* data,
    xdg_toplevel* toplevel,
    wl_array* capabilities)
{
    (void)data; (void)toplevel; (void)capabilities;
}

inline const xdg_toplevel_listener
knst_window_wayland_funcs::xdgToplevelListener =
{
    XdgToplevelConfigure,
    XdgToplevelClose,
    XdgToplevelConfigureBounds,
    XdgToplevelWmCapabilities
};

// ─── xdg-decoration listener ───────────────────────────────────────────────
inline void knst_window_wayland_funcs::XdgDecorationConfigure(
    void* data,
    struct zxdg_toplevel_decoration_v1* deco,
    uint32_t mode)
{
    (void)deco;
    auto* window = static_cast<knst_window*>(data);
    if (!window) return;

    // The compositor told us which mode it prefers. CSD on → we want
    // CLIENT_SIDE; otherwise SERVER_SIDE. No automatic layout — the
    // user decides.
    const bool csd_mode = (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    window->m_draw_custom_title_bar = csd_mode;

    // SSD = server-side decoration is active. The edges belong to the
    // WM in that case; skip auto-resize / hit-test.
    window->m_has_ssd = (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

inline const zxdg_toplevel_decoration_v1_listener
knst_window_wayland_funcs::xdgDecorationListener =
{
    XdgDecorationConfigure
};

// ─── fractional-scale listener ─────────────────────────────────────────────
inline void knst_window_wayland_funcs::FractionalScalePreferred(
    void* data,
    struct wp_fractional_scale_v1* fs,
    uint32_t scale)
{
    (void)fs;
    auto* window = static_cast<knst_window*>(data);
    if (!window) return;

    // Clamp absurd values.
    if (scale < 60)  scale = 60; // 0.5x
    if (scale > 480) scale = 480; // 4.0x

    const int old_scale = window->m_fractional_scale_120;
    window->m_fractional_scale_120 = (int)scale;

    // Update the global field too — some paths still read it (static
    // accessors). Deprecated but kept working.
    KnstWindowSources::fractional_scale_120 = (int)scale;

    // Viewport destination = logical size (compositor scales the buffer).
    if (window->m_viewport) {
        int logical_w = window->m_knst_event.window_width;
        int logical_h = window->m_knst_event.window_height;
        wp_viewport_set_destination(window->m_viewport, logical_w, logical_h);
    }

    // Scale changed — notify the app. It should recompute the
    // framebuffer size and update glViewport + the EGL window, or the
    // window stays blurry.
    if (old_scale != (int)scale) {
        window->m_knst_event.begin(KNST_WINDOW_EVENT_SCALE_CHANGED);
        window->dispatch_current_event();
    }
}

inline const wp_fractional_scale_v1_listener
knst_window_wayland_funcs::fractionalScaleListener =
{
    FractionalScalePreferred
};

// ─── xdg-activation token listener ─────────────────────────────────────────
inline void knst_window_wayland_funcs::ActivationTokenDone(
    void* data,
    struct xdg_activation_token_v1* token,
    const char* token_str)
{
    auto* window = static_cast<knst_window*>(data);
    if (window && window->toplevel && token_str && *token_str) {
        xdg_activation_v1_activate(
            KnstWindowSources::activation,
            token_str,
            window->m_surface);
    }
    xdg_activation_token_v1_destroy(token);
}

inline const xdg_activation_token_v1_listener
knst_window_wayland_funcs::activationTokenListener =
{
    ActivationTokenDone
};

// ─── Frame callback gating ─────────────────────────────────────────────────
// When the compositor signals a new frame, m_frame_ready flips true.
inline void knst_window_wayland_funcs::FrameCallbackDone(
    void* data, struct wl_callback* cb, uint32_t /*time*/)
{
    auto* window = static_cast<knst_window*>(data);
    wl_callback_destroy(cb);
    if (window) {
        window->m_frame_cb = nullptr;
        window->m_frame_ready = true;
        window->m_frame_request_time  = 0;
    }
}

inline const wl_callback_listener
knst_window_wayland_funcs::frameCallbackListener =
{
    FrameCallbackDone
};

inline void knst_window_wayland_funcs::UpdateCursor(
    wl_pointer* pointer,
    uint32_t serial,
    resize_edge edge)
{
    if (!pointer) return;

    // Prefer cursor-shape-v1 when available.
    if (KnstWindowSources::cursor_shape_manager) {
        if (!KnstWindowSources::cursor_shape_device) {
            KnstWindowSources::cursor_shape_device =
                wp_cursor_shape_manager_v1_get_pointer(
                    KnstWindowSources::cursor_shape_manager,
                    pointer);
        }
        if (KnstWindowSources::cursor_shape_device) {
            uint32_t shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
            switch (edge) {
                case resize_edge::left:
                case resize_edge::right: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE; break;
                case resize_edge::top:
                case resize_edge::bottom: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE; break;
                case resize_edge::top_left:
                case resize_edge::bottom_right: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE; break;
                case resize_edge::top_right:
                case resize_edge::bottom_left: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE; break;
                case resize_edge::none:
                default:shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT; break;
            }
            wp_cursor_shape_device_v1_set_shape( KnstWindowSources::cursor_shape_device, serial, shape);
               
            return;
        }
    }

    // Fallback to the legacy wl_cursor_theme.
    wl_cursor* cursor = nullptr;

    switch (edge) {
        case resize_edge::left:
        case resize_edge::right:
            cursor = KnstWindowSources::cursor_ew; break;
        case resize_edge::top:
        case resize_edge::bottom:
            cursor = KnstWindowSources::cursor_ns; break;
        case resize_edge::top_left:
        case resize_edge::bottom_right:
            cursor = KnstWindowSources::cursor_nwse; break;
        case resize_edge::top_right:
        case resize_edge::bottom_left:
            cursor = KnstWindowSources::cursor_nesw; break;
        case resize_edge::none:
        default:
            cursor = KnstWindowSources::cursor_default; break;
    }

    if (!cursor || !cursor->images || !cursor->images[0] || !KnstWindowSources::cursor_surface)
        return;

    wl_cursor_image* image = cursor->images[0];
    wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) return;

    wl_surface_attach(KnstWindowSources::cursor_surface, buffer, 0, 0);
    wl_surface_damage(KnstWindowSources::cursor_surface, 0, 0, image->width, image->height);
    wl_surface_commit(KnstWindowSources::cursor_surface);

    wl_pointer_set_cursor(pointer, serial, KnstWindowSources::cursor_surface, image->hotspot_x, image->hotspot_y);
}

inline void knst_window_wayland_funcs::PointerEnter(
    void* data,
    wl_pointer* pointer,
    uint32_t serial,
    wl_surface* surface,
    wl_fixed_t sx,
    wl_fixed_t sy)
{
    (void)data;

    knst_window* window = knst_window::find_from_surface(surface);
    if (!window) return;

    window->m_pointer_serial = serial;
    window->m_mouse_x = wl_fixed_to_double(sx);
    window->m_mouse_y = wl_fixed_to_double(sy);
    window->m_resize_edge = window->get_resize_edge(window->m_mouse_x, window->m_mouse_y);

    KnstWindowSources::active_window = window;

    auto& ev = window->m_knst_event;
    ev.begin(KNST_WINDOW_EVENT_ENTER);
    ev.mouse_on_window = true;
    ev.mouse_x = wl_fixed_to_int(sx);
    ev.mouse_y = wl_fixed_to_int(sy);

    if (window->is_csd_enabled()) {
        window->refresh_csd_cursor(wl_fixed_to_int(sx), wl_fixed_to_int(sy));
    } else if (window->m_resize_edge != resize_edge::none) {
        UpdateCursor(pointer, serial, window->m_resize_edge);
    } else if (window->m_using_custom_cursor &&
               !window->m_pending_cursor_data.empty()) {
        window->apply_bmp_cursor_now(
            window->m_pending_cursor_data,
            window->m_pending_cursor_w,
            window->m_pending_cursor_h,
            window->m_pending_cursor_hot_x,
            window->m_pending_cursor_hot_y);
    } else {
        UpdateCursor(pointer, serial, resize_edge::none);
    }

    window->m_has_pending_cursor = false;
    window->dispatch_current_event();
}

inline void knst_window_wayland_funcs::PointerLeave(
    void* data,
    wl_pointer* pointer,
    uint32_t serial,
    wl_surface* surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;

    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    window->m_resize_edge = resize_edge::none;
    window->m_pointer_pressed = false;

    auto& ev = window->m_knst_event;
    ev.begin(KNST_WINDOW_EVENT_LEAVE);
    ev.mouse_on_window = false;
    window->dispatch_current_event();

    KnstWindowSources::active_window = nullptr;
}

inline void knst_window_wayland_funcs::PointerMotion(
    void* data,
    wl_pointer* pointer,
    uint32_t time,
    wl_fixed_t sx,
    wl_fixed_t sy)
{
    (void)data; (void)time;

    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    window->m_mouse_x = wl_fixed_to_double(sx);
    window->m_mouse_y = wl_fixed_to_double(sy);

    // CSD callback takes priority when enabled.
    if (window->is_csd_enabled()) {
        window->refresh_csd_cursor(wl_fixed_to_int(sx), wl_fixed_to_int(sy));
    } else if (!window->m_has_ssd) {
        // CSD off but WM SSD is also off — undecorated. Do the edge
        // resize hit-test ourselves.
        resize_edge old_edge = window->m_resize_edge;
        resize_edge new_edge = window->get_resize_edge(
            window->m_mouse_x, window->m_mouse_y);
        window->m_resize_edge = new_edge;

        if (old_edge != new_edge) {
            if (new_edge != resize_edge::none) {
                UpdateCursor(pointer, window->m_pointer_serial, new_edge);
            } else if (window->m_using_custom_cursor &&!window->m_pending_cursor_data.empty()) {
                window->apply_bmp_cursor_now(
                    window->m_pending_cursor_data,
                    window->m_pending_cursor_w,
                    window->m_pending_cursor_h,
                    window->m_pending_cursor_hot_x,
                    window->m_pending_cursor_hot_y);
            } else {
                UpdateCursor(pointer, window->m_pointer_serial, new_edge);
            }
        }
    }
    // else: SSD — the WM owns the cursor, leave it alone.

    auto& ev = window->m_knst_event;
    ev.begin(KNST_WINDOW_EVENT_MOTION);
    ev.mouse_x = wl_fixed_to_int(sx);
    ev.mouse_y = wl_fixed_to_int(sy);
    window->dispatch_current_event();
}

inline void knst_window_wayland_funcs::PointerButton(
    void* data,
    wl_pointer* pointer,
    uint32_t serial,
    uint32_t time,
    uint32_t button,
    uint32_t state)
{
    (void)data; (void)pointer; (void)time;

    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    // Remember the latest input serial (used by xdg-activation,
    // clipboard, primary-selection, and drag-and-drop).
    KnstWindowSources::activation_last_serial = serial;

    auto& ev = window->m_knst_event;

    int original_mx = (int)window->m_mouse_x;
    int original_my = (int)window->m_mouse_y;

    ev.begin(KNST_WINDOW_EVENT_MOUSE);
    ev.mouse_x = original_mx;
    ev.mouse_y = original_my;

    switch (button) {
        case BTN_LEFT: ev.mouse_button = KNST_WINDOW_MOUSE_BUTTON_LEFT; break;
        case BTN_MIDDLE: ev.mouse_button = KNST_WINDOW_MOUSE_BUTTON_MIDDLE; break;
        case BTN_RIGHT: ev.mouse_button = KNST_WINDOW_MOUSE_BUTTON_RIGHT; break;
        // Side buttons arrive as 275/276 in evdev. Report them as
        // BACK/FORWARD for parity with Windows/X11.
        case BTN_SIDE:
        case BTN_BACK: ev.mouse_button = KNST_WINDOW_MOUSE_BUTTON_BACK; break;
        case BTN_EXTRA:
        case BTN_FORWARD: ev.mouse_button = KNST_WINDOW_MOUSE_BUTTON_FORWARD; break;
        default: ev.mouse_button = button; break;
    }

    ev.mouse_action = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? KNST_WINDOW_MOUSE_ACTION_PRESS : KNST_WINDOW_MOUSE_ACTION_RELEASE;




    // Left click PRESS — initiate drag / resize.
    if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        // 1. CSD callback if installed.
        if (window->is_csd_enabled()) {
            knst_window::csd_hit hit = window->hit_test_csd(original_mx, original_my);

            // Double-click on the CSD title bar → maximize / restore.
            if (hit == knst_window::csd_hit::draggable) {
                const uint32_t now = KnstWindowSources::get_current_time_ms();
                static thread_local uint32_t s_last_click = 0;
                static thread_local int      s_last_x = -1;
                static thread_local int      s_last_y = -1;
                constexpr uint32_t DBL_CLICK_MS = 400;
                constexpr int      DBL_CLICK_DIST = 4;

                const bool near_last =
                    (s_last_x >= 0) &&
                    (std::abs(original_mx - s_last_x) <= DBL_CLICK_DIST) &&
                    (std::abs(original_my - s_last_y) <= DBL_CLICK_DIST);
                if ((now - s_last_click) <= DBL_CLICK_MS && near_last) {
                    if (window->m_knst_event.is_maximized) window->restore();
                    else window->set_maximized();
                    s_last_click = 0;
                    s_last_x = -1;
                    s_last_y = -1;
                    return;
                }
                s_last_click = now;
                s_last_x = original_mx;
                s_last_y = original_my;
            }

            if (hit != knst_window::csd_hit::normal) {
                window->csd_handle_hit(hit, original_mx, original_my, serial);
                return;
            }
        }

        // 2. Automatic edge detection — only on undecorated windows.
        // SSD = WM owns the edges; our resize would fight scrollbars
        // and inner widgets. CSD already handled via hit_test above.
        const bool should_auto_resize =
            !window->is_csd_enabled() && !window->m_has_ssd;

        resize_edge edge = should_auto_resize
            ? window->get_resize_edge((double)original_mx, (double)original_my)
            : resize_edge::none;
        if (edge != resize_edge::none) {
            knst_window::csd_hit hit = knst_window::csd_hit::normal;
            switch (edge) {
                case resize_edge::top: hit = knst_window::csd_hit::resize_top; break;
                case resize_edge::bottom: hit = knst_window::csd_hit::resize_bottom; break;
                case resize_edge::left: hit = knst_window::csd_hit::resize_left; break;
                case resize_edge::right: hit = knst_window::csd_hit::resize_right; break;
                case resize_edge::top_left: hit = knst_window::csd_hit::resize_top_left; break;
                case resize_edge::top_right: hit = knst_window::csd_hit::resize_top_right; break;
                case resize_edge::bottom_left: hit = knst_window::csd_hit::resize_bottom_left; break;
                case resize_edge::bottom_right: hit = knst_window::csd_hit::resize_bottom_right; break;
                default: break;
            }
            if (hit != knst_window::csd_hit::normal) {
                window->csd_handle_hit(hit, original_mx, original_my, serial);
                return;
            }
        }
    }

    window->dispatch_current_event();
}

inline void knst_window_wayland_funcs::PointerAxis(
    void* data,
    wl_pointer* pointer,
    uint32_t time,
    uint32_t axis,
    wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time;

    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL &&
        axis != WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        return;
    }

    // Wheel-mouse compositors send both axis and axis_discrete.
    // axis_discrete already gives the notch (1.0 each). Handling both
    // double-counts the scroll. If axis_discrete arrived, a flag says
    // to ignore the axis event.
    //
    // Continuous devices like touchpads don't send discrete, so
    // they're unaffected.
    if (window->m_discard_axis_this_frame) {
        return;
    }

    // Wayland reports ~10.0 per wheel notch, positive = DOWN.
    // X11/Windows use ±1, positive = UP. Negate and divide by 10.
    double norm = -wl_fixed_to_double(value) / 10.0;

    int notches = 0;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        window->m_scroll_accum_v += norm;
        notches = (int)window->m_scroll_accum_v;
        if (notches != 0) window->m_scroll_accum_v -= notches;
    } else {
        window->m_scroll_accum_h += norm;
        notches = (int)window->m_scroll_accum_h;
        if (notches != 0) window->m_scroll_accum_h -= notches;
    }

    if (notches == 0) return;

    auto& ev = window->m_knst_event;
    ev.begin(KNST_WINDOW_EVENT_MOUSE);
    ev.mouse_action = KNST_WINDOW_MOUSE_ACTION_SCROLL;
    ev.mouse_scroll_delta = notches;
    ev.mouse_button = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) ? 4 : 6;
    window->dispatch_current_event();
}

inline void knst_window_wayland_funcs::PointerFrame(void* data, wl_pointer* pointer) {
    (void)data; (void)pointer;
    // Frame is done — reset the axis_discrete flag so the next frame
    // can process axis events again.
    auto* window = KnstWindowSources::active_window;
    if (window) {
        window->m_discard_axis_this_frame = false;
    }
}

inline void knst_window_wayland_funcs::PointerAxisSource(
    void* data, wl_pointer* pointer, uint32_t axis_source)
{
    (void)data; (void)pointer; (void)axis_source;
}

inline void knst_window_wayland_funcs::PointerAxisStop(
    void* data, wl_pointer* pointer, uint32_t time, uint32_t axis)
{
    (void)data; (void)pointer; (void)time; (void)axis;
}

inline void knst_window_wayland_funcs::PointerAxisDiscrete(
    void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete)
{
    (void)data; (void)pointer;

    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL &&
        axis != WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        return;
    }

    // axis_discrete already carries the notch. The axis event will
    // arrive too — don't count it twice. One-frame flag.
    window->m_discard_axis_this_frame = true;

    int notches = -discrete;
    if (notches == 0) return;

    auto& ev = window->m_knst_event;
    ev.begin(KNST_WINDOW_EVENT_MOUSE);
    ev.mouse_action = KNST_WINDOW_MOUSE_ACTION_SCROLL;
    ev.mouse_scroll_delta = notches;
    ev.mouse_button = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) ? 4 : 6;
    window->dispatch_current_event();
}

inline const wl_pointer_listener
knst_window_wayland_funcs::pointerListener =
{
    PointerEnter,
    PointerLeave,
    PointerMotion,
    PointerButton,
    PointerAxis,
    PointerFrame,
    PointerAxisSource,
    PointerAxisStop,
    PointerAxisDiscrete
};

inline void knst_window_wayland_funcs::SeatCapabilities(
    void* data,
    wl_seat* seat,
    uint32_t capabilities)
{
    (void)data;

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!KnstWindowSources::pointer) {
            KnstWindowSources::pointer = wl_seat_get_pointer(seat);
            if (KnstWindowSources::pointer) {
                wl_pointer_add_listener(KnstWindowSources::pointer, &pointerListener, nullptr);

                // Create the relative-pointer as soon as the pointer is
                // ready. Otherwise raw mouse deltas never arrive on
                // Wayland.
                if (KnstWindowSources::relative_pointer_manager &&
                    !KnstWindowSources::relative_pointer) {
                    KnstWindowSources::relative_pointer =
                        zwp_relative_pointer_manager_v1_get_relative_pointer(
                            KnstWindowSources::relative_pointer_manager,
                            KnstWindowSources::pointer);
                    if (KnstWindowSources::relative_pointer) {
                        zwp_relative_pointer_v1_add_listener(
                            KnstWindowSources::relative_pointer,
                            &relativePointerListener,
                            nullptr);
                    }
                }
            }
        }
    } else {
        if (KnstWindowSources::relative_pointer) {
            zwp_relative_pointer_v1_destroy(KnstWindowSources::relative_pointer);
            KnstWindowSources::relative_pointer = nullptr;
        }
        // Destroy the cursor_shape_device bound to the pointer first.
        if (KnstWindowSources::cursor_shape_device) {
            wp_cursor_shape_device_v1_destroy(KnstWindowSources::cursor_shape_device);
            KnstWindowSources::cursor_shape_device = nullptr;
        }
        if (KnstWindowSources::pointer) {
            wl_pointer_release(KnstWindowSources::pointer);
            KnstWindowSources::pointer = nullptr;
            KnstWindowSources::active_window = nullptr;
        }
    }

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (!KnstWindowSources::keyboard) {
            KnstWindowSources::keyboard = wl_seat_get_keyboard(seat);
            if (KnstWindowSources::keyboard)
                wl_keyboard_add_listener(KnstWindowSources::keyboard, &keyboardListener, nullptr);
        }
    } else {
        if (KnstWindowSources::keyboard) {
            wl_keyboard_release(KnstWindowSources::keyboard);
            KnstWindowSources::keyboard = nullptr;
            KnstWindowSources::keyboard_focus_window = nullptr;
        }
    }

    // Touch screen.
    if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
        if (!KnstWindowSources::touch) {
            KnstWindowSources::touch = wl_seat_get_touch(seat);
            if (KnstWindowSources::touch) {
                wl_touch_add_listener(
                    KnstWindowSources::touch, &touchListener, nullptr);
            }
        }
    } else {
        if (KnstWindowSources::touch) {
            wl_touch_release(KnstWindowSources::touch);
            KnstWindowSources::touch = nullptr;
            KnstWindowSources::touch_surface = nullptr;
        }
    }
}

inline void knst_window_wayland_funcs::SeatName(
    void* data,
    wl_seat* seat,
    const char* name)
{
    (void)data; (void)seat; (void)name;
}

inline const wl_seat_listener
knst_window_wayland_funcs::seatListener =
{
    SeatCapabilities,
    SeatName
};

inline void knst_window_wayland_funcs::KeyboardEnter(
    void* data, wl_keyboard* keyboard, uint32_t serial,
    wl_surface* surface, wl_array* keys)
{
    (void)data; (void)keyboard; (void)keys;
    KnstWindowSources::keyboard_serial = serial;

    knst_window* window = knst_window::find_from_surface(surface);
    if (!window) return;

    KnstWindowSources::keyboard_focus_window = window;

    // XdgToplevelConfigure ACTIVATED also fires FOCUS_IN — check first
    // so we don't emit the event twice.
    if (!window->m_knst_event.is_focused) {
        window->m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_IN);
        window->m_knst_event.is_focused = true;
        window->dispatch_current_event();
    }
}

inline void knst_window_wayland_funcs::KeyboardLeave(
    void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface)
{
    (void)data; (void)keyboard; (void)serial; (void)surface;

    auto* window = KnstWindowSources::keyboard_focus_window;
    if (!window) return;

    // Always clear held keys — release events never arrive during
    // Alt+Tab.
    window->clear_held_keys();

    if (window->m_knst_event.is_focused) {
        window->m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_OUT);
        window->m_knst_event.is_focused = false;
        window->dispatch_current_event();
    }
    KnstWindowSources::keyboard_focus_window = nullptr;
}

inline void knst_window_wayland_funcs::KeyboardKeymap(
    void* data,
    wl_keyboard* keyboard,
    uint32_t format,
    int fd,
    uint32_t size)
{
    (void)data; (void)keyboard;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char* map_str = (char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    if (KnstWindowSources::xkb_map)
        xkb_keymap_unref(KnstWindowSources::xkb_map);

    KnstWindowSources::xkb_map = xkb_keymap_new_from_string(
        KnstWindowSources::xkb_ctx,
        map_str,
        XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );

    munmap(map_str, size);
    close(fd);

    if (KnstWindowSources::xkb_st)
        xkb_state_unref(KnstWindowSources::xkb_st);

    if (KnstWindowSources::xkb_map)
        KnstWindowSources::xkb_st = xkb_state_new(KnstWindowSources::xkb_map);
}

inline void knst_window_wayland_funcs::KeyboardKey(
    void* data, wl_keyboard* keyboard, uint32_t serial,
    uint32_t time, uint32_t key, uint32_t state)
{
    (void)data; (void)keyboard; (void)time;

    // Remember the latest input serial for xdg-activation.
    KnstWindowSources::activation_last_serial = serial;

    auto* window = KnstWindowSources::keyboard_focus_window;
    if (!window) return;

    const bool pressed  = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
    const uint32_t xkb_code = key + 8;      // wl_keyboard key → xkb keycode
    const uint32_t scancode = key;
    const uint32_t now      = KnstWindowSources::get_current_time_ms();

    uint32_t keycode = xkb_code;
    char utf8[32] = {0};
    int n = 0;

    if (KnstWindowSources::xkb_st) {
        // Do NOT call xkb_state_update_key here. On Wayland the keyboard
        // state is only updated via wl_keyboard.modifiers. Calling
        // update_key here would double-update with the modifiers event
        // and corrupt the mod state.
        keycode = xkb_state_key_get_one_sym(KnstWindowSources::xkb_st, xkb_code);

        // Turkish keyboard normalization (same as X11).
        if (keycode >= XKB_KEY_a && keycode <= XKB_KEY_z)      keycode -= 32;
        else if (keycode == XKB_KEY_udiaeresis) keycode = XKB_KEY_Udiaeresis;
        else if (keycode == XKB_KEY_odiaeresis) keycode = XKB_KEY_Odiaeresis;
        else if (keycode == XKB_KEY_ccedilla)   keycode = XKB_KEY_Ccedilla;
        else if (keycode == XKB_KEY_scedilla)   keycode = XKB_KEY_Scedilla;
        else if (keycode == XKB_KEY_gbreve)     keycode = XKB_KEY_Gbreve;
        else if (keycode == XKB_KEY_idotless)   keycode = XKB_KEY_I;

        if (pressed) {
            n = xkb_state_key_get_utf8(
                KnstWindowSources::xkb_st, xkb_code,
                utf8, sizeof(utf8) - 1);
        }
    }

    // Don't send control characters (Enter, Tab, Esc, Backspace) as
    // TEXT_INPUT. Those go through the KEYBOARD event path.
    const bool printable = n > 0 &&
        !(n == 1 && ((unsigned char)utf8[0] < 0x20 || (unsigned char)utf8[0] == 0x7F));

    if (printable) {
        window->m_knst_event.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
        window->m_knst_event.text_input = knst_c16string(utf8, (uint32_t)n);
        window->dispatch_current_event();
    }

    if (pressed) {
        if (window->find_held_by_scancode(scancode)) {
            // Already held — emit a repeat event.
            window->m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_REPEAT;
            window->m_knst_event.key_code   = keycode;
            window->m_knst_event.scancode   = scancode;
            window->dispatch_current_event();
            return;
        }

        auto* hk = window->add_held_key(keycode, scancode, now);
        // Store the UTF-8 character in the held key — check_key_repeat
        // re-emits TEXT_INPUT on auto-repeat.
        if (hk && printable && n < (int)sizeof(hk->utf8)) {
            memcpy(hk->utf8, utf8, (size_t)n);
            hk->utf8_len = (uint8_t)n;
        }

        window->m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
        window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
        window->m_knst_event.key_code   = keycode;
        window->m_knst_event.scancode   = scancode;
        window->dispatch_current_event();
    } else {
        // Release: use whatever keycode was set on press. If Shift is
        // released first, the keysym changes — wrong key_code.
        if (auto* hk = window->find_held_by_scancode(scancode)) {
            keycode = (uint32_t)hk->key_code;
        }
        window->remove_held_key(scancode);

        window->m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
        window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_RELEASE;
        window->m_knst_event.key_code   = keycode;
        window->m_knst_event.scancode   = scancode;
        window->dispatch_current_event();
    }
}

inline void knst_window_wayland_funcs::KeyboardModifiers(
    void* data, wl_keyboard* keyboard, uint32_t serial,
    uint32_t mods_depressed, uint32_t mods_latched,
    uint32_t mods_locked, uint32_t group)
{
    (void)data; (void)keyboard; (void)serial;

    if (!KnstWindowSources::xkb_st) return;

    xkb_state_update_mask(
        KnstWindowSources::xkb_st,
        mods_depressed, mods_latched, mods_locked,
        0, 0, group);

    auto* window = KnstWindowSources::keyboard_focus_window;
    if (!window) return;

    // xkb_state_mod_name_is_active reads the real mod state. The old
    // code used a raw bit mask (0x01, 0x04, 0x08, 0x40...) which is
    // keymap-dependent, not fixed.
    auto active = [&](const char* name) {
        return xkb_state_mod_name_is_active(
            KnstWindowSources::xkb_st, name,
            XKB_STATE_MODS_EFFECTIVE) > 0;
    };

    int mods = 0;
    if (active(XKB_MOD_NAME_SHIFT)) mods |= KNST_WINDOW_MOD_SHIFT;
    if (active(XKB_MOD_NAME_CTRL))  mods |= KNST_WINDOW_MOD_CONTROL;
    if (active(XKB_MOD_NAME_ALT))   mods |= KNST_WINDOW_MOD_ALT;
    if (active(XKB_MOD_NAME_LOGO))  mods |= KNST_WINDOW_MOD_SUPER;
    if (active(XKB_MOD_NAME_CAPS))  mods |= KNST_WINDOW_MOD_CAPS_LOCK;
    if (active(XKB_MOD_NAME_NUM))   mods |= KNST_WINDOW_MOD_NUM_LOCK;
    window->m_knst_event.mods = mods;
}

inline void knst_window_wayland_funcs::KeyboardRepeatInfo(
    void* data,
    wl_keyboard* keyboard,
    int32_t rate,
    int32_t delay)
{
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

inline const wl_keyboard_listener
knst_window_wayland_funcs::keyboardListener =
{
    KeyboardKeymap,
    KeyboardEnter,
    KeyboardLeave,
    KeyboardKey,
    KeyboardModifiers,
    KeyboardRepeatInfo
};

inline void knst_window_wayland_funcs::WmBasePing(
    void* data, struct xdg_wm_base* base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(base, serial);
}

inline const xdg_wm_base_listener knst_window_wayland_funcs::wmBaseListener =
{
    WmBasePing
};

inline void knst_window_wayland_funcs::DataSourceTarget(
    void*, struct wl_data_source*, const char*) {}

inline void knst_window_wayland_funcs::DataSourceSend(
    void*, struct wl_data_source*, const char* mime_type, int32_t fd)
{
    // SIGPIPE is already ignored in Init. Still, harden the
    // EINTR/EAGAIN loop.
    const bool is_text = (strcmp(mime_type, "text/plain;charset=utf-8") == 0) ||
                         (strcmp(mime_type, "text/plain") == 0) ||
                         (strcmp(mime_type, "UTF8_STRING") == 0) ||
                         (strcmp(mime_type, "TEXT") == 0) ||
                         (strcmp(mime_type, "STRING") == 0);

    if (is_text) {
        knst_byte_string data(KnstWindowSources::clipboard_pending_text);
        const uint8_t* buf = data.data();
        size_t len = data.length();
        size_t total = 0;

        while (total < len) {
            ssize_t n = write(fd, buf + total, len - total);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;   // EPIPE, EAGAIN, etc. — give up
            }
            total += (size_t)n;
        }
    }
    close(fd);
}

inline void knst_window_wayland_funcs::DataSourceCancelled(
    void*, struct wl_data_source* source)
{
    if (source == KnstWindowSources::clipboard_source) {
        wl_data_source_destroy(source);
        KnstWindowSources::clipboard_source = nullptr;
    }
}

inline void knst_window_wayland_funcs::DataSourceDndDropPerformed(
    void*, struct wl_data_source*) {}
inline void knst_window_wayland_funcs::DataSourceDndFinished(
    void*, struct wl_data_source*) {}
inline void knst_window_wayland_funcs::DataSourceAction(
    void*, struct wl_data_source*, uint32_t) {}

inline const wl_data_source_listener knst_window_wayland_funcs::dataSourceListener = {
    DataSourceTarget,
    DataSourceSend,
    DataSourceCancelled,
    DataSourceDndDropPerformed,
    DataSourceDndFinished,
    DataSourceAction
};

inline void knst_window_wayland_funcs::DataOfferOffer(
    void* data, struct wl_data_offer* offer, const char* mime_type)
{
    if (offer == KnstWindowSources::pending_offer) {
        if (strcmp(mime_type, "text/uri-list") == 0) {
            KnstWindowSources::pending_offer_has_uri_list = true;
        }
    }
}

inline void knst_window_wayland_funcs::DataOfferSourceActions(
    void*, struct wl_data_offer*, uint32_t) {}
inline void knst_window_wayland_funcs::DataOfferAction(
    void*, struct wl_data_offer*, uint32_t) {}

inline const wl_data_offer_listener knst_window_wayland_funcs::dataOfferListener = {
    DataOfferOffer,
    DataOfferSourceActions,
    DataOfferAction
};

inline void knst_window_wayland_funcs::DataDeviceDataOffer(
    void*, struct wl_data_device*, struct wl_data_offer* offer)
{
    if (offer) {
        wl_data_offer_add_listener(offer, &dataOfferListener, nullptr);

        KnstWindowSources::pending_offer = offer;
        KnstWindowSources::pending_offer_has_uri_list = false;
    }
}

inline void knst_window_wayland_funcs::DataDeviceEnter(
    void*, struct wl_data_device*, uint32_t serial,
    struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
    struct wl_data_offer* offer)
{
    if (KnstWindowSources::drag_offer &&
        KnstWindowSources::drag_offer != offer) {
        wl_data_offer_destroy(KnstWindowSources::drag_offer);
        KnstWindowSources::drag_offer = nullptr;
    }
    if (KnstWindowSources::drag_target_window) {
        KnstWindowSources::drag_target_window->m_knst_event.type = KNST_WINDOW_EVENT_FILE_DROP_LEAVE;
        KnstWindowSources::drag_target_window->m_knst_event.drop_files = nullptr;
        KnstWindowSources::drag_target_window->m_knst_event.drop_count = 0;
        KnstWindowSources::drag_target_window->dispatch_current_event();
    }
    KnstWindowSources::drag_target_window = nullptr;
    KnstWindowSources::drag_offer_has_uri_list = false;
    knst_window* window = knst_window::find_from_surface(surface);
    if (!window) return;

    if (!window->m_drag_drop_enabled) {
        // Tell the source "we don't accept" via accept(serial, nullptr).
        // Just destroying the offer leaves the source app hanging.
        if (offer) {
            wl_data_offer_accept(offer, serial, nullptr);
            wl_data_offer_destroy(offer);
        }

        if (offer == KnstWindowSources::pending_offer) {
            KnstWindowSources::pending_offer = nullptr;
            KnstWindowSources::pending_offer_has_uri_list = false;
        }
        return;
    }

    if (KnstWindowSources::drag_offer && KnstWindowSources::drag_offer != offer) {
        wl_data_offer_destroy(KnstWindowSources::drag_offer);
    }

    KnstWindowSources::drag_offer = offer;

    if (offer == KnstWindowSources::pending_offer) {
        KnstWindowSources::drag_offer_has_uri_list = KnstWindowSources::pending_offer_has_uri_list;
    } else {
        KnstWindowSources::drag_offer_has_uri_list = false;
    }

    KnstWindowSources::drag_target_window = window;
    KnstWindowSources::drag_enter_serial = serial;

    if (offer) {
        wl_data_offer_set_actions(offer,
            WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
            WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        // No uri-list → accept(nullptr) means the source won't apply
        // copy. The old code always requested uri-list and then called
        // finish in Drop, which is a protocol error risk.
        if (KnstWindowSources::drag_offer_has_uri_list) {
            wl_data_offer_accept(offer, serial, "text/uri-list");
        } else {
            wl_data_offer_accept(offer, serial, nullptr);
        }
    }

    // Use begin() — the old code only set `type` and left other fields
    // (mouse_x, drop_files, etc.) from the previous event.
    window->m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_ENTER);
    window->m_knst_event.drop_files = nullptr;
    window->m_knst_event.drop_count = 0;
    window->m_knst_event.mouse_x = wl_fixed_to_int(x);
    window->m_knst_event.mouse_y = wl_fixed_to_int(y);
    window->dispatch_current_event();
}

inline void knst_window_wayland_funcs::DataDeviceLeave(
    void*, struct wl_data_device*)
{
    auto* window = KnstWindowSources::drag_target_window;
    if (window) {
        window->m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_LEAVE);
        window->m_knst_event.drop_files = nullptr;
        window->m_knst_event.drop_count = 0;
        window->dispatch_current_event();
    }
    if (KnstWindowSources::drag_offer) {
        wl_data_offer_destroy(KnstWindowSources::drag_offer);
        KnstWindowSources::drag_offer = nullptr;
    }
    KnstWindowSources::drag_offer_has_uri_list = false;
    KnstWindowSources::drag_target_window = nullptr;
}

inline void knst_window_wayland_funcs::DataDeviceMotion(
    void*, struct wl_data_device*, uint32_t, wl_fixed_t x, wl_fixed_t y)
{
    auto* window = KnstWindowSources::drag_target_window;
    if (!window) return;

    window->m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP_MOVE);
    window->m_knst_event.mouse_x = wl_fixed_to_int(x);
    window->m_knst_event.mouse_y = wl_fixed_to_int(y);
    window->dispatch_current_event();
}

inline void knst_window_wayland_funcs::DataDeviceDrop(
    void*, struct wl_data_device*)
{
    auto* window = KnstWindowSources::drag_target_window;
    if (!window) {
        return;
    }

    struct wl_data_offer* local_offer = KnstWindowSources::drag_offer;
    if (!local_offer) {
        return;
    }

    bool has_uri_list = KnstWindowSources::drag_offer_has_uri_list;

    KnstWindowSources::drag_offer = nullptr;
    KnstWindowSources::drag_offer_has_uri_list = false;

    if (!has_uri_list) {
        // Do NOT call wl_data_offer_finish here. In DataDeviceEnter we
        // called accept(serial, nullptr) because there was no uri-list.
        // The Wayland protocol says: wl_data_offer::finish MUST NOT be
        // sent after an accept with a NULL mime type — it triggers an
        // invalid_finish protocol error and the compositor disconnects
        // us. Just destroy the offer.
        wl_data_offer_destroy(local_offer);
        KnstWindowSources::drag_target_window = nullptr;
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        wl_data_offer_destroy(local_offer);
        return;
    }

    wl_data_offer_receive(local_offer, "text/uri-list", pipefd[1]);
    close(pipefd[1]);

    wl_display_roundtrip(KnstWindowSources::wayland_display);

    // Blocking read would freeze the UI. Non-block + poll + timeout.
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    knst_byte_string result;
    char buf[4096];
    auto t_start = std::chrono::steady_clock::now();
    bool got_eof = false;

    while (!got_eof) {
        while (wl_display_prepare_read(KnstWindowSources::wayland_display) != 0) {
            wl_display_dispatch_pending(KnstWindowSources::wayland_display);
        }
        wl_display_flush(KnstWindowSources::wayland_display);

        struct pollfd pfd;
        pfd.fd = wl_display_get_fd(KnstWindowSources::wayland_display);
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
            std::chrono::steady_clock::now() - t_start).count();
        if (elapsed > 5000) break;   // 5-second safety limit
    }
    close(pipefd[0]);

    knst_vector<knst_c16string> files;
    knst_parse_uri_list(result, files);

    window->m_knst_event.begin(KNST_WINDOW_EVENT_FILE_DROP);
    window->m_knst_event.drop_files =
        std::make_shared<knst_vector<knst_c16string>>(std::move(files));
    window->m_knst_event.drop_count = (uint32_t)window->m_knst_event.drop_files->size();
    window->dispatch_current_event();

    wl_data_offer_finish(local_offer);
    wl_data_offer_destroy(local_offer);

    KnstWindowSources::drag_target_window = nullptr;

}


inline void knst_window_wayland_funcs::DataDeviceSelection(
    void*, struct wl_data_device*, struct wl_data_offer* offer)
{
    if (KnstWindowSources::selection_offer) {
        wl_data_offer_destroy(KnstWindowSources::selection_offer);
    }
    KnstWindowSources::selection_offer = offer;
}

inline const wl_data_device_listener knst_window_wayland_funcs::dataDeviceListener = {
    DataDeviceDataOffer,
    DataDeviceEnter,
    DataDeviceLeave,
    DataDeviceMotion,
    DataDeviceDrop,
    DataDeviceSelection
};


// ─── relative-pointer (raw motion) ─────────────────────────────────────────
inline void knst_window_wayland_funcs::RelativePointerMotion(
    void* data, struct zwp_relative_pointer_v1* rp,
    uint32_t /*time_hi*/, uint32_t /*time_lo*/,
    wl_fixed_t dx, wl_fixed_t dy,
    wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel)
{
    (void)data; (void)rp;
    auto* window = KnstWindowSources::active_window;
    if (!window) return;

    // Prefer unaccelerated deltas — FPS cameras want raw device
    // motion, not the compositor's pointer acceleration. Fall back to
    // accelerated values when unaccel is zero (some compositors never
    // send unaccel).
    double udx = wl_fixed_to_double(dx_unaccel);
    double udy = wl_fixed_to_double(dy_unaccel);
    if (udx == 0.0 && udy == 0.0) {
        udx = wl_fixed_to_double(dx);
        udy = wl_fixed_to_double(dy);
    }

    // wl_fixed_to_int truncates toward zero — a slow 0.4 px motion
    // would be lost forever. Accumulate the fractional part per window.
    window->m_raw_accum_x += udx;
    window->m_raw_accum_y += udy;

    int32_t ix = (int32_t)window->m_raw_accum_x;
    int32_t iy = (int32_t)window->m_raw_accum_y;
    if (ix == 0 && iy == 0) return;   // nothing to emit yet

    window->m_raw_accum_x -= (double)ix;
    window->m_raw_accum_y -= (double)iy;

    window->m_knst_event.begin(KNST_WINDOW_EVENT_MOTION);
    window->m_knst_event.is_raw = true;
    window->m_knst_event.raw_delta_x = ix;
    window->m_knst_event.raw_delta_y = iy;
    window->dispatch_current_event();
}

inline const zwp_relative_pointer_v1_listener
knst_window_wayland_funcs::relativePointerListener =
{
    RelativePointerMotion
};

// ─── Primary selection listeners (X11 middle-click parity) ─────────────────
inline void knst_window_wayland_funcs::PrimarySourceSend(
    void*, struct zwp_primary_selection_source_v1*,
    const char* mime_type, int32_t fd)
{
    const bool is_text = (strcmp(mime_type, "text/plain;charset=utf-8") == 0) || (strcmp(mime_type, "text/plain") == 0);
    if (is_text) {
        knst_byte_string data(KnstWindowSources::primary_pending_text);
        const uint8_t* buf = data.data();
        size_t len = data.length();
        size_t total = 0;

        while (total < len) {
            ssize_t n = write(fd, buf + total, len - total);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            total += (size_t)n;
        }
    }
    close(fd);
}

inline void knst_window_wayland_funcs::PrimarySourceCancelled(
    void*, struct zwp_primary_selection_source_v1* source)
{
    if (source == KnstWindowSources::primary_source) {
        zwp_primary_selection_source_v1_destroy(source);
        KnstWindowSources::primary_source = nullptr;
    }
}

inline const zwp_primary_selection_source_v1_listener
knst_window_wayland_funcs::primarySourceListener =
{
    PrimarySourceSend,
    PrimarySourceCancelled
};

inline void knst_window_wayland_funcs::PrimaryOfferOffer(
    void* data, struct zwp_primary_selection_offer_v1* offer,
    const char* mime_type)
{
    (void)data; (void)offer; (void)mime_type;
}

inline const zwp_primary_selection_offer_v1_listener
knst_window_wayland_funcs::primaryOfferListener =
{
    PrimaryOfferOffer
};

inline void knst_window_wayland_funcs::PrimaryDeviceDataOffer(
    void*, struct zwp_primary_selection_device_v1*,
    struct zwp_primary_selection_offer_v1* offer)
{
    // data_offer: a new offer is being introduced. Just add the
    // listener — don't change the active offer here. That happens in
    // the selection event.
    if (offer) {
        zwp_primary_selection_offer_v1_add_listener(
            offer, &primaryOfferListener, nullptr);
    }
}

inline void knst_window_wayland_funcs::PrimaryDeviceSelection(
    void*, struct zwp_primary_selection_device_v1*,
    struct zwp_primary_selection_offer_v1* offer)
{
    // The same offer can arrive twice (some compositors). Without a
    // guard this would be a double destroy.
    if (KnstWindowSources::primary_offer == offer) return;

    if (KnstWindowSources::primary_offer) {
        zwp_primary_selection_offer_v1_destroy(KnstWindowSources::primary_offer);
    }
    KnstWindowSources::primary_offer = offer;
    // Listener was already added in data_offer — don't add it here.
}

inline const zwp_primary_selection_device_v1_listener
knst_window_wayland_funcs::primarySelectionDeviceListener =
{
    PrimaryDeviceDataOffer,
    PrimaryDeviceSelection
};

// ─── text-input-v3 (IME) ───────────────────────────────────────────────────
// CJK preedit + commit + delete_surrounding.
//
// Protocol order:
//   1. preedit_string (0..N times — composition preview update)
//   2. commit_string (0..1 times — final text)
//   3. delete_surrounding_text (0..1 times — delete previous text)
//   4. done (1 time — apply everything)
//
// The app must NOT change state until `done` arrives (protocol rule).
// Most compositors send done immediately; we batch events at done time.

inline void knst_window_wayland_funcs::TextInputEnter(
    void* data, struct zwp_text_input_v3* ti, struct wl_surface* surface)
{
    auto* window = static_cast<knst_window*>(data);
    if (!window || !ti) return;

    // Only enable IME when OUR surface gets focus. Otherwise two
    // windows share the same IME and text goes to the wrong one.
    if (surface != window->m_surface) return;

    zwp_text_input_v3_enable(ti);

    // Hint: text editor / terminal-like surface.
    zwp_text_input_v3_set_content_type(
        ti,
        ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
        ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);

    // Cursor rectangle — the IME popup opens near this box.
    zwp_text_input_v3_set_cursor_rectangle(
        ti, 0, 0,
        window->m_knst_event.window_width,
        window->m_knst_event.window_height);

    zwp_text_input_v3_commit(ti);
}

inline void knst_window_wayland_funcs::TextInputLeave(
    void* data, struct zwp_text_input_v3* ti, struct wl_surface* surface)
{
    auto* window = static_cast<knst_window*>(data);
    if (!ti || !window) return;
    if (surface != window->m_surface) return;

    zwp_text_input_v3_disable(ti);
    zwp_text_input_v3_commit(ti);
}

// State buffers — accumulate until done, then dispatch events.
namespace knst_text_input_state {
    struct state {
        knst_c16string preedit;
        int32_t preedit_begin = -1;
        int32_t preedit_end = -1;
        bool had_preedit = false;  // did the previous done have a preedit?

        knst_c16string commit;
        bool has_commit = false;
        bool has_delete = false;
        int32_t delete_before = 0;
        int32_t delete_after = 0;
    };
    // thread_local — Wayland is single-threaded, but safe either way.
    inline state& get() noexcept {
        static thread_local state s;
        return s;
    }
}

inline void knst_window_wayland_funcs::TextInputPreeditString(
    void* data, struct zwp_text_input_v3* ti,
    const char* text, int32_t cursor_begin, int32_t cursor_end)
{
    (void)data; (void)ti;

    auto& s = knst_text_input_state::get();

    // text == null → preedit is being cleared (IME cancelled).
    if (!text) {
        s.preedit = u"";
        s.preedit_begin = -1;
        s.preedit_end = -1;
        return;
    }

    s.preedit = knst_c16string(text);
    s.preedit_begin = cursor_begin;
    s.preedit_end = cursor_end;
}

inline void knst_window_wayland_funcs::TextInputCommitString(
    void* data, struct zwp_text_input_v3* ti, const char* text)
{
    (void)data; (void)ti;
    auto& s = knst_text_input_state::get();

    if (text && *text) {
        s.commit = knst_c16string(text);
        s.has_commit = true;
    } else {
        s.commit = u"";
        s.has_commit = false;
    }

    // Commit arrived — preedit is now invalid; clear it.
    s.preedit = u"";
    s.preedit_begin = -1;
    s.preedit_end = -1;
}

inline void knst_window_wayland_funcs::TextInputDeleteSurroundingText(
    void* data, struct zwp_text_input_v3* ti,
    uint32_t before_length, uint32_t after_length)
{
    (void)data; (void)ti;
    auto& s = knst_text_input_state::get();

    s.has_delete = true;
    s.delete_before = (int32_t)before_length;
    s.delete_after = (int32_t)after_length;
}

inline void knst_window_wayland_funcs::TextInputDone(
    void* data, struct zwp_text_input_v3* ti, uint32_t serial)
{
    (void)ti; (void)serial;
    auto* window = static_cast<knst_window*>(data);
    if (!window) return;

    auto& s = knst_text_input_state::get();

    // Order matters: delete → preedit → commit. The app deletes text,
    // shows the preedit indicator, then appends committed text.

    if (s.has_delete) {
        window->m_knst_event.begin(KNST_WINDOW_EVENT_TEXT_DELETE_SURROUNDING);
        window->m_knst_event.text_delete_before = s.delete_before;
        window->m_knst_event.text_delete_after  = s.delete_after;
        window->dispatch_current_event();
        s.has_delete = false;
        s.delete_before = 0;
        s.delete_after  = 0;
    }

    // Avoid preedit spam: only send an empty preedit if the previous
    // done had one. Otherwise the app receives a pointless "clear"
    // event on every done.
    if (s.preedit.length() > 0 || s.had_preedit) {
        window->m_knst_event.begin(KNST_WINDOW_EVENT_TEXT_PREEDIT);
        window->m_knst_event.text_preedit = s.preedit;
        window->m_knst_event.text_preedit_cursor_begin = s.preedit_begin;
        window->m_knst_event.text_preedit_cursor_end   = s.preedit_end;
        window->dispatch_current_event();
    }
    s.had_preedit = s.preedit.length() > 0;

    // Commit text.
    if (s.has_commit) {
        window->m_knst_event.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
        window->m_knst_event.text_input = s.commit;
        window->dispatch_current_event();
        s.has_commit = false;
        s.commit = u"";
    }

    // Reset preedit cursor (for the next preedit).
    s.preedit = u"";
    s.preedit_begin = -1;
    s.preedit_end = -1;
}

inline const zwp_text_input_v3_listener
knst_window_wayland_funcs::textInputListener =
{
    TextInputEnter,
    TextInputLeave,
    TextInputPreeditString,
    TextInputCommitString,
    TextInputDeleteSurroundingText,
    TextInputDone
};

// ─── Touch screen (wl_touch) ───────────────────────────────────────────────
// Multi-touch architecture:
//   The Wayland protocol has two phases:
//     1) down / up / motion events update STATE as they arrive.
//     2) wl_touch.frame flushes all accumulated points as ONE event.
//
//   The old code emitted each event separately. Since begin() resets
//   pointer_count, multi-finger was LOST — pinch/zoom didn't work.

inline void knst_window_wayland_funcs::TouchDown(
    void* data, struct wl_touch* /*touch*/, uint32_t /*serial*/,
    uint32_t /*time*/, struct wl_surface* surface,
    int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    (void)data;
    knst_window* window = knst_window::find_from_surface(surface);
    if (!window || !window->m_touch_enabled) return;

    // Remember the active surface — TouchMotion / TouchUp / TouchFrame
    // find the window through it. Touch-only devices may never get
    // pointer enter.
    KnstWindowSources::touch_surface = surface;

    // Find an empty slot.
    int slot = -1;
    for (int i = 0; i < KNST_MAX_TOUCH_POINTS; i++) {
        if (window->m_active_touches[i].id == -1) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return;   // full — swallow quietly

    window->m_active_touches[slot].id = id;
    window->m_active_touches[slot].x = (float)wl_fixed_to_double(x);
    window->m_active_touches[slot].y = (float)wl_fixed_to_double(y);

    int active = 0;
    for (int i = 0; i < KNST_MAX_TOUCH_POINTS; i++) {
        if (window->m_active_touches[i].id >= 0) ++active;
    }

    window->m_touch_pending_action = (active == 1)
        ? KNST_WINDOW_TOUCH_ACTION_PRESS
        : KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS;
    window->m_touch_frame_dirty = true;
}

inline void knst_window_wayland_funcs::TouchUp(
    void* data, struct wl_touch* /*touch*/,
    uint32_t /*serial*/, uint32_t /*time*/, int32_t id)
{
    (void)data;
    knst_window* window = nullptr;
    if (KnstWindowSources::touch_surface) {
        window = knst_window::find_from_surface(KnstWindowSources::touch_surface);
    }
    if (!window || !window->m_touch_enabled) return;

    for (int i = 0; i < KNST_MAX_TOUCH_POINTS; i++) {
        if (window->m_active_touches[i].id == id) {
            window->m_active_touches[i].id = -1;
            window->m_active_touches[i].x = 0.0f;
            window->m_active_touches[i].y = 0.0f;
            break;
        }
    }

    int active = 0;
    for (int i = 0; i < KNST_MAX_TOUCH_POINTS; i++) {
        if (window->m_active_touches[i].id >= 0) ++active;
    }

    window->m_touch_pending_action = (active == 0)
        ? KNST_WINDOW_TOUCH_ACTION_RELEASE
        : KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE;
    window->m_touch_frame_dirty = true;
}

inline void knst_window_wayland_funcs::TouchMotion(
    void* data, struct wl_touch* /*touch*/, uint32_t /*time*/,
    int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    (void)data;
    knst_window* window = nullptr;
    if (KnstWindowSources::touch_surface) {
        window = knst_window::find_from_surface(KnstWindowSources::touch_surface);
    }
    if (!window || !window->m_touch_enabled) return;

    for (int i = 0; i < KNST_MAX_TOUCH_POINTS; i++) {
        if (window->m_active_touches[i].id == id) {
            window->m_active_touches[i].x = (float)wl_fixed_to_double(x);
            window->m_active_touches[i].y = (float)wl_fixed_to_double(y);
            break;
        }
    }

    window->m_touch_pending_action = KNST_WINDOW_TOUCH_ACTION_MOVE;
    window->m_touch_frame_dirty = true;
}

inline void knst_window_wayland_funcs::TouchFrame(
    void* data, struct wl_touch* /*touch*/)
{
    (void)data;
    knst_window* window = nullptr;
    if (KnstWindowSources::touch_surface) {
        window = knst_window::find_from_surface(KnstWindowSources::touch_surface);
    }
    if (!window || !window->m_touch_enabled) return;
    if (!window->m_touch_frame_dirty) return;

    // Frame: emit all accumulated points as ONE event.
    auto& ev = window->m_knst_event;
    ev.begin(KNST_WINDOW_EVENT_TOUCH);

    int idx = 0;
    for (int i = 0; i < KNST_MAX_TOUCH_POINTS && idx < KNST_MAX_TOUCH_POINTS; i++) {
        if (window->m_active_touches[i].id >= 0) {
            ev.pointer_id[idx] = window->m_active_touches[i].id;
            ev.pointer_x[idx]  = window->m_active_touches[i].x;
            ev.pointer_y[idx]  = window->m_active_touches[i].y;
            ++idx;
        }
    }
    ev.pointer_count = idx;
    ev.touch_action  = window->m_touch_pending_action;

    window->dispatch_current_event();

    window->m_touch_frame_dirty = false;
}

inline void knst_window_wayland_funcs::TouchCancel(
    void* data, struct wl_touch* /*touch*/)
{
    (void)data;
    knst_window* window = nullptr;
    if (KnstWindowSources::touch_surface) {
        window = knst_window::find_from_surface(KnstWindowSources::touch_surface);
    }
    if (!window) return;

    // Clear every active point.
    for (int i = 0; i < KNST_MAX_TOUCH_POINTS; i++) {
        window->m_active_touches[i].id = -1;
    }
    window->m_touch_frame_dirty = false;

    if (!window->m_touch_enabled) return;

    window->m_knst_event.begin(KNST_WINDOW_EVENT_TOUCH);
    window->m_knst_event.touch_action = KNST_WINDOW_TOUCH_ACTION_CANCEL;
    window->dispatch_current_event();
}

inline const wl_touch_listener
knst_window_wayland_funcs::touchListener =
{
    TouchDown,
    TouchUp,
    TouchMotion,
    TouchFrame,
    TouchCancel
};

#endif