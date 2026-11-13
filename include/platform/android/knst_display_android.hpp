// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_display_android.hpp
----------------------------

    Monitor information for the Android backend.

*/

#pragma once

#if defined(KNST_USING_PLATFORM_ANDROID)

inline void knst_display::refresh_screens_android() noexcept {
    m_monitor_list.clear();

    knst_monitor monitor;

    // Pull the current window size if we have one; otherwise fall back
    // to a generic phone resolution so the caller still gets something.
    if (KnstWindowSources::m_app != nullptr &&
        KnstWindowSources::m_app->window != nullptr) {
        monitor.width = ANativeWindow_getWidth (KnstWindowSources::m_app->window);
        monitor.height = ANativeWindow_getHeight(KnstWindowSources::m_app->window);
    } else {
        monitor.width = 1080;
        monitor.height = 2400;
    }

    // 160 dpi = 1.0x (mdpi baseline in Android).
    monitor.dpi_scale = 160.0f;

    // Read the real density from the activity's AConfiguration.
    if (KnstWindowSources::m_app != nullptr &&
        KnstWindowSources::m_app->activity != nullptr) {
        AConfiguration*config = AConfiguration_new();
        if (config != nullptr) {
            AConfiguration_fromAssetManager(
                config, KnstWindowSources::m_app->activity->assetManager);
            int32_t dpi = AConfiguration_getDensity(config);
            AConfiguration_delete(config);

            // DENSITY_ANY (0xFFFE) and DENSITY_NONE (0xFFFF) are special
            // sentinels, and 0 means DEFAULT — all three are invalid here.
            // Using them raw would give dpi_scale = 65534 (giant UI).
            if (dpi > 0 &&
                dpi != ACONFIGURATION_DENSITY_ANY &&
                dpi != ACONFIGURATION_DENSITY_NONE) {
                monitor.dpi_scale = (float)dpi;
            }
        }
    }

    // Physical size in millimeters, derived from pixels and density.
    float dpi = monitor.dpi_scale;
    if (dpi > 0) {
        monitor.physical_width  = (int)((float)monitor.width  / dpi * 25.4f);
        monitor.physical_height = (int)((float)monitor.height / dpi * 25.4f);
    } else {
        monitor.physical_width  = 0;
        monitor.physical_height = 0;
    }

    // Android always presents a single full-screen display, so the
    // monitor is at origin and marked primary.
    monitor.root_x = 0;
    monitor.root_y = 0;
    monitor.is_primary = true;
    monitor.refresh_rate = 60.0f;
    monitor.name = u"Android Screen";

    m_monitor_list.push_back(
        std::make_unique<knst_monitor>(std::move(monitor)));
}

#endif