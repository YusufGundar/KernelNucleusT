// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_wayland_event_manager.hpp
----------------------------

    Event handling function declarations for the Wayland backend.

*/

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

    // ── 5D.1: xdg-decoration ────────────────────────────────────────
    static void XdgDecorationConfigure(void*, struct zxdg_toplevel_decoration_v1*, uint32_t);
    static const zxdg_toplevel_decoration_v1_listener xdgDecorationListener;

    // ── 5D.3: fractional-scale ──────────────────────────────────────
    static void FractionalScalePreferred(void*, struct wp_fractional_scale_v1*, uint32_t);
    static const wp_fractional_scale_v1_listener fractionalScaleListener;

    // ── 5D.4: xdg-activation ────────────────────────────────────────
    static void ActivationTokenDone(void*, struct xdg_activation_token_v1*, const char*);
    static const xdg_activation_token_v1_listener activationTokenListener;

    // ── 5F: Frame callback gating ───────────────────────────────────
    static void FrameCallbackDone(void* data, struct wl_callback* cb, uint32_t time);
    static const wl_callback_listener frameCallbackListener;

    // ── Touch screen (wl_touch) ─────────────────────────────────────
    static void TouchDown  (void*, struct wl_touch*, uint32_t, uint32_t,
                            struct wl_surface*, int32_t, wl_fixed_t, wl_fixed_t);
    static void TouchUp    (void*, struct wl_touch*, uint32_t, uint32_t, int32_t);
    static void TouchMotion(void*, struct wl_touch*, uint32_t, int32_t,
                            wl_fixed_t, wl_fixed_t);
    static void TouchFrame (void*, struct wl_touch*);
    static void TouchCancel(void*, struct wl_touch*);
    
    static const wl_touch_listener touchListener;

    // ── 6A: text-input-v3 ───────────────────────────────────────────
    static void TextInputEnter(void*, struct zwp_text_input_v3*, struct wl_surface*);
    static void TextInputLeave(void*, struct zwp_text_input_v3*, struct wl_surface*);
    static void TextInputPreeditString(void*, struct zwp_text_input_v3*, const char*, int32_t, int32_t);
    static void TextInputCommitString(void*, struct zwp_text_input_v3*, const char*);
    static void TextInputDeleteSurroundingText(void*, struct zwp_text_input_v3*, uint32_t, uint32_t);
    static void TextInputDone(void*, struct zwp_text_input_v3*, uint32_t);
    static const zwp_text_input_v3_listener textInputListener;

    // ── 5D.5: primary-selection ─────────────────────────────────────
    static void PrimarySourceSend(void*, struct zwp_primary_selection_source_v1*, const char*, int32_t);
    static void PrimarySourceCancelled(void*, struct zwp_primary_selection_source_v1*);
    static const zwp_primary_selection_source_v1_listener primarySourceListener;

    static void PrimaryOfferOffer(void*, struct zwp_primary_selection_offer_v1*, const char*);
    static const zwp_primary_selection_offer_v1_listener primaryOfferListener;

    static void PrimaryDeviceDataOffer(void*, struct zwp_primary_selection_device_v1*, struct zwp_primary_selection_offer_v1*);
    static void PrimaryDeviceSelection(void*, struct zwp_primary_selection_device_v1*, struct zwp_primary_selection_offer_v1*);
    static const zwp_primary_selection_device_v1_listener primarySelectionDeviceListener;

    // ── 5E.1: relative-pointer (raw motion on Wayland) ──────────────
    static void RelativePointerMotion(void*, struct zwp_relative_pointer_v1*,
                                      uint32_t, uint32_t, wl_fixed_t, wl_fixed_t,
                                      wl_fixed_t, wl_fixed_t);


    static const zwp_relative_pointer_v1_listener relativePointerListener;
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