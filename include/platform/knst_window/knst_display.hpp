// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_display.hpp
----------------------------

    Monitor information classes live here.

*/

#pragma once

#include <cmath>
#include <algorithm>
#include <cstring>
#include <memory>

#if KNST_USING_PLATFORM_WINDOWS
    #include <ShellScalingApi.h>
#elif KNST_USING_LINUX_PLATFORM_X11
    #include <xcb/randr.h>
#endif


class knst_display;

class knst_monitor {
private:
    friend class knst_display;

#if KNST_USING_PLATFORM_WINDOWS
    HMONITOR handle = nullptr;
#endif
#if KNST_USING_LINUX_PLATFORM_X11
    xcb_randr_output_t output_id = 0;
#endif
#if KNST_USING_LINUX_PLATFORM_WAYLAND
    friend class KnstWindowSources;
    struct wl_output* output = nullptr;
    uint32_t wl_output_id = 0;
    // wl_output.scale gives an integer factor (1, 2, 3). The old code
    // multiplied it into dpi_scale, which produced wrong values. Store
    // the factor separately and use it in done().
    int wl_scale_factor = 1;
#endif

public:
    int root_x = 0;
    int root_y = 0;
    int width = 0;
    int height = 0;
    int physical_width = 0;
    int physical_height = 0;
    float refresh_rate = 60;
    float dpi_scale = 96.0f;
    bool is_primary = false;
    knst_c16string name = u"Undefined";
};


class knst_display {
private:
    // unique_ptr keeps monitor objects at a fixed heap address. The
    // wl_output_listener user-data pointers are registered as
    // &m_monitor_list[i]; if the vector grows and reallocates, those
    // pointers dangle → use-after-free. unique_ptr avoids the move.
    static inline knst_vector<std::unique_ptr<knst_monitor>> m_monitor_list;
    friend class KnstWindowSources;

#if KNST_USING_LINUX_PLATFORM_WAYLAND

    static inline void wayland_output_geometry(
        void* data, wl_output*,
        int32_t x, int32_t y,
        int32_t pw, int32_t ph,
        int32_t, const char* make, const char* model, int32_t)
    {
        auto* m = (knst_monitor*)data;
        if (!m) return;

        m->root_x = x;
        m->root_y = y;
        m->physical_width = pw;
        m->physical_height = ph;

        knst_byte_string name;
        if (make && make[0] != '\0') {
            name.append(reinterpret_cast<const unsigned char*>(make), (uint32_t)strlen(make));
        }
        if (model && model[0] != '\0') {
            if (!name.empty()) name.append(" ");
            name.append(reinterpret_cast<const unsigned char*>(model), (uint32_t)strlen(model));
        }
        if (!name.empty()) {
            m->name = knst_c16string(reinterpret_cast<const char*>(name.data()));
        }
    }

    static inline void wayland_output_mode(
        void* data, wl_output*,
        uint32_t flags, int32_t w, int32_t h, int32_t refresh)
    {
        auto* m = (knst_monitor*)data;
        if (!m) return;

        if (flags & WL_OUTPUT_MODE_CURRENT) {
            m->width = w;
            m->height = h;
            m->refresh_rate = refresh / 1000.0f;
        }

        // PREFERRED is not the same as primary — several monitors can
        // be PREFERRED. On Wayland, "primary" comes from xdg-output.
        // We don't set is_primary here; refresh_screens_wayland() marks
        // the first monitor as primary at the end.
    }

    static inline void wayland_output_done(void* data, wl_output*) {
        auto* m = (knst_monitor*)data;
        if (!m) return;

        // Priority order:
        //   1. wl_output.scale (integer factor) — GNOME/KDE integer scaling
        //   2. Physical DPI computation — last resort
        //
        // Fractional-scale-v1 is per-surface, so it isn't accessible at
        // the monitor level here.
        if (m->wl_scale_factor > 1) {
            m->dpi_scale = 96.0f * (float)m->wl_scale_factor;
            return;
        }

        if (m->physical_width > 0 && m->physical_height > 0 &&
            m->width > 0 && m->height > 0) {

            double dp = std::sqrt((double)(m->width  * m->width) +  (double)(m->height * m->height));
            double dm = std::sqrt((double)(m->physical_width  * m->physical_width) +  (double)(m->physical_height * m->physical_height));
            double di = dm / 25.4;

            if (di > 0.0) {
                float computed = (float)(dp / di);
                // Clamp extreme values (buggy EDID).
                if (computed < 48.0f)  computed = 48.0f;
                if (computed > 480.0f) computed = 480.0f;
                m->dpi_scale = computed;
            }
        }
        // else: keep the 96.0f default.
    }

    static inline void wayland_output_scale(void* data, wl_output*, int32_t factor) {
        auto* m = (knst_monitor*)data;
        if (!m) return;

        // Wayland guarantees factor >= 1. Clamp absurd values.
        if (factor < 1) factor = 1;
        if (factor > 8) factor = 8;

        m->wl_scale_factor = factor;
    }

    static inline const wl_output_listener wayland_output_listener = {
        .geometry = wayland_output_geometry,
        .mode = wayland_output_mode,
        .done = wayland_output_done,
        .scale = wayland_output_scale,
    };

#endif

public:

    // ── Backend-specific implementations ───────────────────────────
    static void refresh_screens_x11() noexcept;
    static void refresh_screens_wayland() noexcept;
    static void refresh_screens_win32() noexcept;
    static void refresh_screens_android() noexcept;

    // ── Public dispatcher (defined in knst_window_core.hpp) ────────
    static void refresh_screens() noexcept;

    // Phase 6E switched the container to unique_ptr, so the return
    // type follows.
    static const knst_vector<std::unique_ptr<knst_monitor>>&
    get_monitor_list() noexcept {
        return m_monitor_list;
    }

    static const knst_monitor* get_primary_monitor() noexcept {
        if (m_monitor_list.empty()) return nullptr;

        for (const auto& mon : m_monitor_list) {
            if (mon && mon->is_primary) return mon.get();
        }
        return m_monitor_list[0] ? m_monitor_list[0].get() : nullptr;
    }

    // Numeric access — how many monitors were found.
    static size_t get_monitor_count() noexcept {
        return m_monitor_list.size();
    }

    // Indexed access — use this instead of `[i]`.
    static const knst_monitor* get_monitor(size_t index) noexcept {
        if (index >= m_monitor_list.size()) return nullptr;
        return m_monitor_list[index].get();
    }
};