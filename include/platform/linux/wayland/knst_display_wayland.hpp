// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_display_wayland.hpp
----------------------------

    Monitor information for the Wayland backend.

*/

#pragma once

#if KNST_USING_LINUX_PLATFORM_WAYLAND

inline void knst_display::refresh_screens_wayland() noexcept {
    if (!KnstWindowSources::wayland_display ||
        !KnstWindowSources::registery) return;

    // Roundtrip so any pending wl_output events get processed.
    wl_display_roundtrip(KnstWindowSources::wayland_display);
    wl_display_roundtrip(KnstWindowSources::wayland_display);

    // DO NOT SORT!
    //
    //   std::sort used to be called here — but each monitor's
    //   wl_output_listener user-data holds &m_monitor_list[i].
    //   Re-sorting the vector makes that address point at a
    //   different monitor, so the next geometry / mode / done event
    //   is written to the wrong one. That class of bug is basically
    //   a UAF in disguise.
    //
    //   Monitors already arrive in the compositor's order; if you
    //   need spatial sorting, sort a copy in your app.

    bool has_primary = false;
    for (const auto& m : m_monitor_list) {
        if (m && m->is_primary) { has_primary = true; break; }
    }
    if (!has_primary && !m_monitor_list.empty() && m_monitor_list[0]) {
        m_monitor_list[0]->is_primary = true;
    }
}

#endif