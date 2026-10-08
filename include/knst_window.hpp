// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0



/*
----------------------------
knst_window.hpp
----------------------------

   It is the unifying HPP file.

*/


#pragma once


#include "platform/knst_window/knst_window_identifiers.hpp"
#include <chrono>
#if KNST_USING_PLATFORM_WINDOWS

    KNST_FORCE_INLINE LRESULT CALLBACK load_native_to_knst_event(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept; 

#elif KNST_USING_LINUX_PLATFORM_X11

    #include <xcb/xcb_keysyms.h>
    #include <X11/Xlib-xcb.h>

#elif KNST_USING_LINUX_PLATFORM_WAYLAND

    #include <wayland-client.h>
    #include <wayland-cursor.h>
    

#endif



#include "platform/knst_window/knst_window_core.hpp"



