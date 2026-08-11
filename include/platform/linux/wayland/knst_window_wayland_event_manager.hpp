#ifndef KNST_LINUX_WAYLAND_EVENT_MANAGER_HPP
#define KNST_LINUX_WAYLAND_EVENT_MANAGER_HPP

#pragma once

#include <xkbcommon/xkbcommon.h>
#include <wayland-client.h>

#if KNST_USING_LINUX_PLATFORM_WAYLAND

struct wl_surface;


class knst_window_wayland_funcs {
public:
   

    static void XdgSurfaceConfigure(void* data, xdg_surface* surface, uint32_t serial);
    static const xdg_surface_listener xdgSurfaceListener;

   
    static void XdgToplevelConfigure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, wl_array* states);
    static void XdgToplevelClose(void* data, xdg_toplevel* toplevel);
    static void XdgToplevelConfigureBounds(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height);
    static void XdgToplevelWmCapabilities(void* data, xdg_toplevel* toplevel, wl_array* capabilities);
    static const xdg_toplevel_listener xdgToplevelListener;
    static void UpdateCursor(wl_pointer* pointer, uint32_t serial, resize_edge edge);

   
    static void PointerEnter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy);
    static void PointerLeave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
    static void PointerMotion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
    static void PointerButton(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
    static void PointerAxis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
    static void PointerFrame(void* data, wl_pointer* pointer);
    static void PointerAxisSource(void* data, wl_pointer* pointer, uint32_t axis_source);
    static void PointerAxisStop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis);
    static void PointerAxisDiscrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete);
    static const wl_pointer_listener pointerListener;

  
    static void KeyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys);
    static void KeyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface);
    static void KeyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int fd, uint32_t size);
    static void KeyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    static void KeyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group);
    static void KeyboardRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay);
    static const wl_keyboard_listener keyboardListener;

  
    static void SeatCapabilities(void* data, wl_seat* seat, uint32_t capabilities);
    static void SeatName(void* data, wl_seat* seat, const char* name);
    static const wl_seat_listener seatListener;

    static void WmBasePing(void* data, struct xdg_wm_base* base, uint32_t serial);
    static const xdg_wm_base_listener wmBaseListener;

  
    static void DataSourceTarget(void* data, struct wl_data_source* source, const char* mime_type);
    static void DataSourceSend(void* data, struct wl_data_source* source, const char* mime_type, int32_t fd);
    static void DataSourceCancelled(void* data, struct wl_data_source* source);
    static void DataSourceDndDropPerformed(void* data, struct wl_data_source* source);
    static void DataSourceDndFinished(void* data, struct wl_data_source* source);
    static void DataSourceAction(void* data, struct wl_data_source* source, uint32_t dnd_action);
    static const wl_data_source_listener dataSourceListener;


    static void DataOfferOffer(void* data, struct wl_data_offer* offer, const char* mime_type);
    static void DataOfferSourceActions(void* data, struct wl_data_offer* offer, uint32_t source_actions);
    static void DataOfferAction(void* data, struct wl_data_offer* offer, uint32_t dnd_action);
    static const wl_data_offer_listener dataOfferListener;

  
    static void DataDeviceDataOffer(void* data, struct wl_data_device* dev, struct wl_data_offer* offer);
    static void DataDeviceEnter(void* data, struct wl_data_device* dev, uint32_t serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* offer);
    static void DataDeviceLeave(void* data, struct wl_data_device* dev);
    static void DataDeviceMotion(void* data, struct wl_data_device* dev, uint32_t time, wl_fixed_t x, wl_fixed_t y);
    static void DataDeviceDrop(void* data, struct wl_data_device* dev);
    static void DataDeviceSelection(void* data, struct wl_data_device* dev, struct wl_data_offer* offer);
    static const wl_data_device_listener dataDeviceListener;
};

#endif
#endif // KNST_LINUX_WAYLAND_EVENT_MANAGER_HPP