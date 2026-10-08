// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

// ═══════════════════════════════════════════════════════════════════════════
//  knst_window_android_test.cpp
//
//  Purpose: A clean, readable example for someone seeing the library
//           for the first time.
//
//  Controls:
//    Volume Up     → show soft keyboard
//    Volume Down   → hide soft keyboard
//    Single tap    → change background color
//    3-finger tap  → clipboard roundtrip test
//
//  What this teaches:
//    1. KnstWindowSources::Init        → initialize the library
//    2. Wait for window                → app->window is NULL at first
//    3. knst_window + creation + show  → create the window
//    4. set_redraw_callback            → function called every frame
//    5. event_count/get_event/clear    → read and clear events
//    6. ANativeWindow_lock/unlockAndPost → software rendering
//    7. destroy + CleanUp              → shutdown
//
//   On Android, just like in Wayland, drawing operations are required to open a window; naturally, you can use OpenGL or Vulkan—though Vulkan support is coming in the future, the basic architecture currently follows this approach.
//
// ═══════════════════════════════════════════════════════════════════════════

#include "../../../../include/KernelNucleusT.hpp"

#if !defined(KNST_USING_PLATFORM_ANDROID)
    #error "This example is Android only"
#endif


// ═══════════════════════════════════════════════════════════════════════════
//  Application state
// ═══════════════════════════════════════════════════════════════════════════
struct state {
    uint32_t background = 0xFF202060;   // ARGB dark blue
    uint32_t skip_frames = 3;            // skip N frames after new window

    // Keyboard auto-hide guard (run only once)
    bool keyboard_hidden = false;

    // Statistics
    uint64_t frame_count = 0;
    uint64_t touch_count = 0;

    // Lifecycle
    bool window_ready = false;
};

static state g;


static inline uint32_t color(uint8_t r, uint8_t gr, uint8_t b, uint8_t a = 0xFF) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)gr << 8) | r;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Rendering — paint the whole screen with a single color
//
//  Software rendering via ANativeWindow.
//  Replace this function if you want OpenGL / Vulkan instead.
// ═══════════════════════════════════════════════════════════════════════════
KNST_FORCE_INLINE static void paint_screen(struct android_app* app) {
    if (!app || !app->window) return;

    int win_w = ANativeWindow_getWidth(app->window);
    int win_h = ANativeWindow_getHeight(app->window);
    if (win_w <= 0 || win_h <= 0) return;

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(app->window, &buf, nullptr) != 0) return;

    if (!buf.bits || buf.width <= 0 || buf.height <= 0 ||
        buf.stride < buf.width ||
        buf.width != win_w || buf.height != win_h) {
        ANativeWindow_unlockAndPost(app->window);
        return;
    }

    uint32_t* px = (uint32_t*)buf.bits;
    for (int y = 0; y < buf.height; ++y) {
        for (int x = 0; x < buf.width; ++x) {
            px[y * buf.stride + x] = g.background;
        }
    }

    ANativeWindow_unlockAndPost(app->window);
}


// ═══════════════════════════════════════════════════════════════════════════
//  Event handling
// ═══════════════════════════════════════════════════════════════════════════
KNST_FORCE_INLINE static void handle_events(knst_window& window) {

    for (size_t i = 0; i < window.event_count(); ++i) {
        const knst_window_event& ev = window.get_window_event_handle(i);

        switch (ev.type) {

        // ─── TOUCH ──────────────────────────────────────────────────────
        // Only changes background color — NOT related to keyboard.
        case KNST_WINDOW_EVENT_MOBILE_TOUCH:
            if (ev.touch_action == KNST_WINDOW_TOUCH_ACTION_PRESS) {
                g.touch_count++;
                g.background = color(255, 40 + (g.touch_count * 30) % 200, 40);
                KNST_LOG_INFO("Touch #%llu  (%.0f, %.0f)",(unsigned long long)g.touch_count,ev.pointer_x[0], ev.pointer_y[0]);
                // 3-finger tap → clipboard test
                if (ev.pointer_count >= 3) {
                    KNST_LOG_INFO("Clipboard test...");
                    window.set_clipboard(u"Hello KNST!");
                    window.request_clipboard();
                    KNST_LOG_INFO("Read length: %u",(unsigned)window.get_clipboard().length());
                }
            }
            break;

        // ─── KEYBOARD ───────────────────────────────────────────────────
        case KNST_WINDOW_EVENT_KEYBOARD:
            if (ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS) {
                if (ev.key_code >= 32 && ev.key_code < 127)
                    KNST_LOG_INFO("Key '%c'  (code=%d)", (char)ev.key_code, ev.key_code);
                else
                    KNST_LOG_INFO("Key code=%d", ev.key_code);
            }
            break;

        // ─── VOLUME BUTTONS → KEYBOARD CONTROL ──────────────────────────
        case KNST_WINDOW_EVENT_MOBILE_VOLUME_UP:
            knst_mobile_keyboard::show();
            KNST_LOG_INFO("Volume+ → keyboard SHOW");
            break;

        case KNST_WINDOW_EVENT_MOBILE_VOLUME_DOWN:
            knst_mobile_keyboard::hide();
            KNST_LOG_INFO("Volume- → keyboard HIDE");
            break;

        // ─── LIFECYCLE ──────────────────────────────────────────────────
        case KNST_WINDOW_EVENT_EXPOSE:
            KNST_LOG_INFO("Window exposed: %dx%d",
                          ev.window_width, ev.window_height);
            // New window → reset buffer format
            if (KnstWindowSources::m_app && KnstWindowSources::m_app->window) {
                ANativeWindow_setBuffersGeometry(KnstWindowSources::m_app->window,0, 0, WINDOW_FORMAT_RGBA_8888);
                                                 
            }
            g.window_ready = true;
            g.skip_frames = 3;
            break;

        case KNST_WINDOW_EVENT_APP_WINDOW_LOST:
            KNST_LOG_INFO("Window lost (backgrounded)");
            g.window_ready = false;
            g.keyboard_hidden = false;
            break;

        case KNST_WINDOW_EVENT_RESIZE:
            KNST_LOG_INFO("Resized: %dx%d", ev.window_width, ev.window_height);
            g.skip_frames = 3;
            break;

        case KNST_WINDOW_EVENT_APP_STARTED:  KNST_LOG_INFO("APP started");  break;
        case KNST_WINDOW_EVENT_APP_RESUMED:  KNST_LOG_INFO("APP resumed");  break;
        case KNST_WINDOW_EVENT_APP_PAUSED:   KNST_LOG_INFO("APP paused");   break;
        case KNST_WINDOW_EVENT_APP_STOPPED:  KNST_LOG_INFO("APP stopped");  break;

        case KNST_WINDOW_EVENT_APP_CONFIG_CHANGED:
            KNST_LOG_INFO("Config changed (rotation, etc.)");
            g.skip_frames = 3;
            break;

        case KNST_WINDOW_EVENT_APP_CONTENT_RECT:
            // Keyboard is open → screen shrinks → auto-hide
            // (only once, not on every content rect event)
            if (!g.keyboard_hidden &&
                knst_mobile_keyboard::is_visible() &&
                ev.window_height > 0 &&
                (ev.content_bottom - ev.content_top) >= ev.window_height * 0.7f) {
                knst_mobile_keyboard::hide();
                g.keyboard_hidden = true;
                KNST_LOG_INFO("Keyboard auto-hidden");
            }
            // Keyboard re-opened → reset flag
            if (knst_mobile_keyboard::is_visible()) {
                g.keyboard_hidden = false;
            }
            break;

        case KNST_WINDOW_EVENT_CLOSE:
            KNST_LOG_INFO("Close requested");
            window.should_close();
            break;

        default:
            break;
        }
    }

    window.clear_events();
}


// ═══════════════════════════════════════════════════════════════════════════
//  Frame callback — called once per frame
// ═══════════════════════════════════════════════════════════════════════════
KNST_FORCE_INLINE static void frame_callback(knst_window& window, void* /*user_data*/) {

    handle_events(window);

    if (g.skip_frames > 0) {
        g.skip_frames--;
        return;
    }

    if (g.window_ready) {
        paint_screen(KnstWindowSources::m_app);
    }

    g.frame_count++;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Entry point
// ═══════════════════════════════════════════════════════════════════════════
void android_main(struct android_app* app) {

    KNST_LOG_INFO("=== KNST Android Simple Example ===");

    // ─── Step 1: Initialize the library ─────────────────────────────────
    KnstWindowSources::Init(app);

    // ─── Step 2: Wait for the window to be ready ────────────────────────
    // On Android, app->window is NULL at first. It's filled when the OS
    // is ready to give us a surface.
    KNST_LOG_INFO("Waiting for window...");
    while (app->window == nullptr) {
        int events;
        struct android_poll_source* source = nullptr;
        while (ALooper_pollAll(-1, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested != 0) return;
        }
    }
    KNST_LOG_INFO("Window ready: %dx%d",ANativeWindow_getWidth(app->window),ANativeWindow_getHeight(app->window));
                  
                  

    // ─── Step 3: Create the window ──────────────────────────────────────
    knst_window window;
    window.set_redraw_callback(frame_callback); // what to do each frame
    window.creation(); // register with library
    window.show(); // show on screen

    // Buffer format for software rendering
    ANativeWindow_setBuffersGeometry(app->window, 0, 0, WINDOW_FORMAT_RGBA_8888);

    g.window_ready = true;

    KNST_LOG_INFO("Controls:");
    KNST_LOG_INFO("Volume Up -> keyboard SHOW");
    KNST_LOG_INFO("Volume Down -> keyboard HIDE");
    KNST_LOG_INFO("Single tap -> change color");
    KNST_LOG_INFO("3-finger tap -> clipboard test");

    // ─── Step 4: Main loop ──────────────────────────────────────────────
    while (!window.is_should_close()) {

        // Poll system events (~60 FPS)
        int events;
        struct android_poll_source* source = nullptr;
        while (ALooper_pollAll(16, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested != 0) {
                window.should_close();
                break;
            }
        }

        // Process library event queue + draw to screen
        window.call_redraw_callback();
    }

    // ─── Step 5: Cleanup ────────────────────────────────────────────────
    KNST_LOG_INFO("Closing... frames=%llu  touches=%llu",(unsigned long long)g.frame_count,(unsigned long long)g.touch_count);

    window.destroy();
    KnstWindowSources::CleanUp();
    KNST_LOG_INFO("Cleanup complete");
}
