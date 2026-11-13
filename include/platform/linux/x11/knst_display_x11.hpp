// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_display_x11.hpp
----------------------------

    Monitor information for the X11 backend.

*/

#pragma once

#if KNST_USING_LINUX_PLATFORM_X11

inline void knst_display::refresh_screens_x11() noexcept {

    // After CleanUp or a failed Init, m_connection can be null — any
    // xcb_* call would crash.
    if (KnstWindowSources::m_connection == nullptr ||
        xcb_connection_has_error(KnstWindowSources::m_connection)) {
        return;
    }

    // ====================================================================
    //  HiDPI overrides
    //
    //  Priority (highest first):
    //    1. Xft.dpi        → user's explicit X resource
    //    2. GDK_SCALE      → integer scale from env var
    //    3. physical calc  → computed from monitor dimensions
    //
    //  Qt/GTK set Xft.dpi from their own scale settings, so honoring
    //  it keeps us consistent with the toolkit.
    // ====================================================================
    float xft_dpi_override = 0.0f;
    if (KnstWindowSources::m_xlib_display) {
        char* xrdb = XResourceManagerString(KnstWindowSources::m_xlib_display);
        if (xrdb) {
            char* dpi_str = strstr(xrdb, "Xft.dpi:");
            if (dpi_str) {
                float parsed = 0.0f;
                if (sscanf(dpi_str + 8, "%f", &parsed) == 1 && parsed > 0.0f) {
                    xft_dpi_override = parsed;
                }
            }
        }
    }

    float gdk_scale = 0.0f;
    if (const char* env = getenv("GDK_SCALE")) {
        int v = atoi(env);
        if (v > 0) gdk_scale = (float)v;
    }

    xcb_randr_query_version_cookie_t version_cookie =  xcb_randr_query_version(KnstWindowSources::m_connection, 1, 5);
    xcb_randr_query_version_reply_t* version_reply = xcb_randr_query_version_reply(KnstWindowSources::m_connection, version_cookie, nullptr);

    if (!version_reply) return;

    // xcb_randr_get_monitors only works on RandR 1.5+. Older servers
    // are rare — exit quietly.
    const uint32_t r_major = version_reply->major_version;
    const uint32_t r_minor = version_reply->minor_version;
    free(version_reply);

    if (r_major < 1 || (r_major == 1 && r_minor < 5)) {
        m_monitor_list.clear();
        auto fallback = std::make_unique<knst_monitor>();
        fallback->width = DefaultScreenOfDisplay(KnstWindowSources::m_xlib_display)->width;
        fallback->height = DefaultScreenOfDisplay(KnstWindowSources::m_xlib_display)->height;
        fallback->root_x = 0;
        fallback->root_y = 0;
        fallback->is_primary = true;
        fallback->name = u"Default";
        m_monitor_list.push_back(std::move(fallback));
        return;
    }

    xcb_randr_get_screen_resources_current_cookie_t res_cookie =
        xcb_randr_get_screen_resources_current(
            KnstWindowSources::m_connection,
            KnstWindowSources::m_root
        );

    xcb_randr_get_screen_resources_current_reply_t* res_reply =
        xcb_randr_get_screen_resources_current_reply(
            KnstWindowSources::m_connection,
            res_cookie,
            nullptr
        );

    xcb_randr_get_monitors_cookie_t monitors_cookie =
        xcb_randr_get_monitors(
            KnstWindowSources::m_connection,
            KnstWindowSources::m_root,
            1
        );

    xcb_randr_get_monitors_reply_t* monitors_reply =
        xcb_randr_get_monitors_reply(
            KnstWindowSources::m_connection,
            monitors_cookie,
            nullptr
        );

    if (!monitors_reply) {
        if (res_reply) free(res_reply);
        return;
    }

    m_monitor_list.clear();

    xcb_randr_monitor_info_iterator_t iter =
        xcb_randr_get_monitors_monitors_iterator(monitors_reply);

    while (iter.rem) {
        xcb_randr_monitor_info_t* info = iter.data;
        knst_monitor monitor;

        monitor.root_x = info->x;
        monitor.root_y = info->y;
        monitor.width = info->width;
        monitor.height = info->height;
        monitor.is_primary = info->primary;
        monitor.physical_width = info->width_in_millimeters;
        monitor.physical_height = info->height_in_millimeters;

        if (info->name != XCB_ATOM_NONE) {
            xcb_get_atom_name_cookie_t name_cookie = xcb_get_atom_name(KnstWindowSources::m_connection, info->name);
            xcb_get_atom_name_reply_t* name_reply = xcb_get_atom_name_reply(KnstWindowSources::m_connection, name_cookie, nullptr);

            if (name_reply) {
                const char* name_ptr = xcb_get_atom_name_name(name_reply);
                int name_len = xcb_get_atom_name_name_length(name_reply);
                monitor.name = knst_c16string(std::string(name_ptr, name_len).c_str());
                free(name_reply);
            } else {
                monitor.name = knst_c16string("Unknown");
            }
        } else {
            monitor.name = knst_c16string("Unknown");
        }

        xcb_randr_output_t* outputs = xcb_randr_monitor_info_outputs(info);
        int num_outputs = xcb_randr_monitor_info_outputs_length(info);

        if (num_outputs > 0) {
            monitor.output_id = outputs[0];
        }

        monitor.refresh_rate = 60.0f;

        if (num_outputs > 0 && res_reply) {
            xcb_randr_get_output_info_cookie_t out_cookie =
                xcb_randr_get_output_info(
                    KnstWindowSources::m_connection,
                    outputs[0],
                    XCB_CURRENT_TIME
                );

            xcb_randr_get_output_info_reply_t* out_reply =
                xcb_randr_get_output_info_reply(
                    KnstWindowSources::m_connection,
                    out_cookie,
                    nullptr
                );

            if (out_reply && out_reply->crtc != XCB_NONE) {
                xcb_randr_get_crtc_info_cookie_t crtc_cookie =
                    xcb_randr_get_crtc_info(
                        KnstWindowSources::m_connection,
                        out_reply->crtc,
                        XCB_CURRENT_TIME
                    );

                xcb_randr_get_crtc_info_reply_t* crtc_reply =
                    xcb_randr_get_crtc_info_reply(
                        KnstWindowSources::m_connection,
                        crtc_cookie,
                        nullptr
                    );

                if (crtc_reply && crtc_reply->mode != XCB_NONE) {
                    xcb_randr_mode_info_iterator_t mode_iter =
                        xcb_randr_get_screen_resources_current_modes_iterator(res_reply);

                    while (mode_iter.rem) {
                        xcb_randr_mode_info_t* mode = mode_iter.data;

                        if (mode->id == crtc_reply->mode) {
                            if (mode->htotal > 0 && mode->vtotal > 0) {
                                monitor.refresh_rate =
                                    (float)mode->dot_clock / (mode->htotal * mode->vtotal);
                            }
                            break;
                        }
                        xcb_randr_mode_info_next(&mode_iter);
                    }
                }
                if (crtc_reply) free(crtc_reply);
            }
            if (out_reply) free(out_reply);
        }

        // ─── Compute DPI (with override hierarchy) ──────────────────
        monitor.dpi_scale = 96.0f;

        if (xft_dpi_override > 0.0f) {
            // 1) User's explicit Xft.dpi setting wins
            monitor.dpi_scale = xft_dpi_override;
        } else if (gdk_scale > 0.0f) {
            // 2) GDK_SCALE env var (e.g. "2")
            monitor.dpi_scale = 96.0f * gdk_scale;
        } else if (monitor.physical_width > 0 && monitor.physical_height > 0) {
            // 3) Compute from physical dimensions
            double diagonal_px = std::sqrt(
                (double)(monitor.width * monitor.width) +
                (double)(monitor.height * monitor.height)
            );
            double diagonal_mm = std::sqrt(
                (double)(monitor.physical_width * monitor.physical_width) +
                (double)(monitor.physical_height * monitor.physical_height)
            );
            double diagonal_inch = diagonal_mm / 25.4;

            if (diagonal_inch > 0.0) {
                monitor.dpi_scale = static_cast<float>(
                    std::round(diagonal_px / diagonal_inch * 1000.0) / 1000.0
                );
            }
        }

        m_monitor_list.push_back(
            std::make_unique<knst_monitor>(std::move(monitor)));
        xcb_randr_monitor_info_next(&iter);
    }

    free(monitors_reply);
    if (res_reply) free(res_reply);

    // X11 monitors don't use listener user-data (output_id is an
    // integer), so sorting is safe. unique_ptrs move but the objects
    // stay at fixed heap addresses.
    std::sort(m_monitor_list.begin(), m_monitor_list.end(),
        [](const std::unique_ptr<knst_monitor>& a,
           const std::unique_ptr<knst_monitor>& b) {
            if (!a || !b) return false;
            if (a->root_y != b->root_y) return a->root_y < b->root_y;
            return a->root_x < b->root_x;
        });

    bool has_primary = false;
    for (const auto& m : m_monitor_list) {
        if (m && m->is_primary) { has_primary = true; break; }
    }
    if (!has_primary && !m_monitor_list.empty() && m_monitor_list[0]) {
        m_monitor_list[0]->is_primary = true;
    }
}

#endif