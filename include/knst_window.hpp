#ifndef KNST_WINDOW_HPP
#define KNST_WINDOW_HPP
#pragma once


#include "platform/knst_window/knst_window_identifiers.hpp"

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




#ifdef KNST_USING_OPENGL

    #include "platform/knst_window/opengl_support/knst_window_opengl_manager.hpp"

#endif



#ifdef KNST_USING_VULKAN

    #include "platform/knst_window/vulkan_support/knst_window_vulkan_manager.hpp"

#endif






#endif //KNST_WINDOW_HPP