// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_android_event_manager.hpp
----------------------------

    Event handling for the Android backend.

*/

#pragma once

#if defined(KNST_USING_PLATFORM_ANDROID)
#include <thread>
#include <chrono>
#include "knst_window_android_jni.hpp"


// ============================================================================
//  JNI getUnicodeChar helpers
//
//  Android's KeyEvent.getUnicodeChar() correctly resolves Shift+letter,
//  punctuation, Turkish characters and IME composition. An older version
//  hand-mapped only A-Z and 0-9, so "Shift+a" worked but "!" or "?" went
//  through wrong.
// ============================================================================
inline int knst_cp_to_utf8(uint32_t cp, char* o) {
    if (cp < 0x80)  { o[0] = (char)cp; return 1; }
    if (cp < 0x800)  { o[0] = (char)(0xC0 | (cp >> 6));
                        o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12));
                        o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        o[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (cp >> 18));
    o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

inline int32_t knst_android_unicode_char(android_app* app, int32_t keyCode, int32_t metaState) {
    if (!app || !app->activity || !app->activity->vm) return 0;

    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;

    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != 0) return 0;
        attached = true;
    }

    int32_t result = 0;
    if (jclass cls = env->FindClass("android/view/KeyEvent")) {
        jmethodID ctor = env->GetMethodID(cls, "<init>", "(II)V");
        jmethodID getU = env->GetMethodID(cls, "getUnicodeChar", "(I)I");
        if (ctor && getU) {
            // ACTION_DOWN = 0
            if (jobject ev = env->NewObject(cls, ctor, (jint)0, (jint)keyCode)) {
                result = env->CallIntMethod(ev, getU, (jint)metaState);
                env->DeleteLocalRef(ev);
            }
        }
        env->DeleteLocalRef(cls);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) vm->DetachCurrentThread();
    return result;
}


KNST_FORCE_INLINE void cmd_callback(struct android_app* app, int32_t cmd) {
    knst_window* window = reinterpret_cast<knst_window*>(app->userData);
    if (window == nullptr) return;

    switch (cmd) {
        case APP_CMD_INPUT_CHANGED:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_INPUT_CHANGED);
            window->dispatch_current_event();
            break;

        case APP_CMD_INIT_WINDOW:
            if (app->window == nullptr) break;
            window->notify_surface_recreate();

            window->m_knst_event.begin(KNST_WINDOW_EVENT_EXPOSE);
            window->m_knst_event.window_width  = ANativeWindow_getWidth(app->window);
            window->m_knst_event.window_height = ANativeWindow_getHeight(app->window);
            window->dispatch_current_event();

            // Fire the density event on first open.
            //
            //  APP_CMD_CONFIG_CHANGED only triggers when config actually
            //  changes — the first open isn't a change. So without this
            //  block, the density event never fires on launch and
            //  g_ui_scale stays 1.0 (tiny circles).
            //
            //  Read the density from app->config and emit it through
            //  APP_CONFIG_CHANGED. PC never compiles this block.
            if (app->config != nullptr) {
                int density = AConfiguration_getDensity(app->config);

                // DENSITY_ANY / NONE are sentinels — fall back to 1.0x.
                if (density == ACONFIGURATION_DENSITY_ANY ||
                    density == ACONFIGURATION_DENSITY_NONE ||
                    density <= 0) {
                    density = 160;
                }

                window->m_knst_event.begin(
                    KNST_WINDOW_EVENT_APP_CONFIG_CHANGED);
                window->m_knst_event.density = (float)density / 160.0f;
                window->dispatch_current_event();
            }
            break;

        case APP_CMD_TERM_WINDOW:
            // Order matters for Vulkan spec compliance:
            //
            //   1) Emit APP_WINDOW_LOST first — the app drops its
            //      swapchain. Vulkan requires the swapchain to be
            //      destroyed BEFORE VkSurfaceKHR, or you get validation
            //      errors and crashes.
            //
            //   2) Then notify_surface_release() destroys VkSurfaceKHR
            //      or EGLSurface.
            //
            //  The old order (release → event) violated the spec.
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_WINDOW_LOST);
            window->m_knst_event.window_width  = 0;
            window->m_knst_event.window_height = 0;
            window->dispatch_current_event();

            window->notify_surface_release();
            break;

        case APP_CMD_WINDOW_RESIZED:
            if (app->window == nullptr) break;

            // No notify_surface_recreate() here — Android resizes the
            // EGL surface itself. Calling recreate manually leaked on
            // every resize.
            window->m_knst_event.begin(KNST_WINDOW_EVENT_RESIZE);
            window->m_knst_event.window_width  = ANativeWindow_getWidth(app->window);
            window->m_knst_event.window_height = ANativeWindow_getHeight(app->window);
            window->dispatch_current_event();
            break;

        case APP_CMD_WINDOW_REDRAW_NEEDED:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_EXPOSE);
            window->dispatch_current_event();
            break;

        case APP_CMD_CONTENT_RECT_CHANGED:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_CONTENT_RECT);
            window->m_knst_event.content_left   = app->contentRect.left;
            window->m_knst_event.content_top    = app->contentRect.top;
            window->m_knst_event.content_right  = app->contentRect.right;
            window->m_knst_event.content_bottom = app->contentRect.bottom;
            window->dispatch_current_event();
            break;

        case APP_CMD_GAINED_FOCUS:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_IN);
            window->m_knst_event.is_focused = true;
            window->dispatch_current_event();
            break;

        case APP_CMD_LOST_FOCUS:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_FOCUS_OUT);
            window->m_knst_event.is_focused = false;
            // Release events may never arrive after focus loss
            // (Alt+Tab, app switch). Clear held keys like other platforms.
            window->clear_held_keys();
            window->dispatch_current_event();
            break;

        case APP_CMD_CONFIG_CHANGED: {
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_CONFIG_CHANGED);
            AConfiguration* config = app->config;

            window->m_knst_event.orientation = AConfiguration_getOrientation(config);

            AConfiguration_getLanguage(config, window->m_knst_event.language);
            AConfiguration_getCountry (config, window->m_knst_event.country);

            // AConfiguration_getUiMode was removed in NDK 26. No public
            // night-mode API in current NDK — the app can query it via
            // JNI from Java if needed. Report false for now.
            window->m_knst_event.is_night_mode = false;

            int density = AConfiguration_getDensity(config);
            // DENSITY_ANY (0xFFFE) and DENSITY_NONE (0xFFFF) are special
            // values; using them raw makes everything look gigantic.
            if (density == ACONFIGURATION_DENSITY_ANY ||
                density == ACONFIGURATION_DENSITY_NONE ||
                density <= 0) {
                density = 160;   // 1.0x fallback
            }
            window->m_knst_event.density = (float)density / 160.0f;

            window->m_knst_event.screen_width_dp  =  AConfiguration_getScreenWidthDp(config);
            window->m_knst_event.screen_height_dp = AConfiguration_getScreenHeightDp(config);
            window->dispatch_current_event();
            break;
        }

        case APP_CMD_LOW_MEMORY:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_LOW_MEMORY);
            window->m_knst_event.is_low_memory = true;
            window->dispatch_current_event();
            break;

        case APP_CMD_START:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_STARTED);
            window->dispatch_current_event();
            break;

        case APP_CMD_RESUME:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_RESUMED);
            window->m_knst_event.is_focused = true;
            window->dispatch_current_event();
            break;

        case APP_CMD_SAVE_STATE:
            // The previous version copied a malloc'd buffer into the
            // event ring along with its pointer — dangling on the ring.
            // It also never assigned to app->savedState. Correct
            // approach: just notify the user, let them allocate their
            // own buffer if they want. Most games don't need SAVE_STATE.
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_SAVE_STATE);
            window->m_knst_event.saved_state = nullptr;
            window->m_knst_event.saved_state_size = 0;
            window->dispatch_current_event();
            break;

        case APP_CMD_PAUSE:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_PAUSED);
            window->m_knst_event.is_focused = false;
            window->clear_held_keys();
            window->dispatch_current_event();
            break;

        case APP_CMD_STOP:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_STOPPED);
            window->dispatch_current_event();
            break;

        case APP_CMD_DESTROY:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_CLOSE);
            window->should_close();
            window->dispatch_current_event();
            break;

        default:
            break;
    }
}


KNST_FORCE_INLINE int32_t input_callback(struct android_app* app,  AInputEvent* event) {
    knst_window* window = reinterpret_cast<knst_window*>(app->userData);
    if (window == nullptr) {
        return 0;
    }

    int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        int pointerCount = AMotionEvent_getPointerCount(event);
        int32_t source = AInputEvent_getSource(event);

        // Touch vs mouse — distinguish by source.
        //
        //  AINPUT_SOURCE_MOUSE = 0x2002 and AINPUT_SOURCE_TOUCHSCREEN =
        //  0x1002 both share the CLASS_POINTER (0x2) bit. A naive
        //  `(source & X) != 0` test is true for both — a mouse would be
        //  counted as touch, and joysticks / trackballs / touchpads
        //  would fall into the touch path.
        //
        //  Correct test: full mask equality.
        const bool is_mouse =
            (source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE;
        const bool is_touch =
            (source & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN ||
            (source & AINPUT_SOURCE_STYLUS)      == AINPUT_SOURCE_STYLUS;

        if (is_mouse) {
            // ── Physical mouse event ──────────────────────────────────
            //  Android sends both ACTION_DOWN and ACTION_BUTTON_PRESS
            //  for a mouse click. Handling both means a doubled event.
            //  ACTION_BUTTON_* is preferred (more detailed).
            //  On release, getButtonState() can be zero — remember the
            //  button from the press.

            int32_t btn_state = AMotionEvent_getButtonState(event);
            int btn = 0;
            if (btn_state & AMOTION_EVENT_BUTTON_PRIMARY)   btn = KNST_WINDOW_MOUSE_BUTTON_LEFT;
            if (btn_state & AMOTION_EVENT_BUTTON_SECONDARY) btn = KNST_WINDOW_MOUSE_BUTTON_RIGHT;
            if (btn_state & AMOTION_EVENT_BUTTON_TERTIARY)  btn = KNST_WINDOW_MOUSE_BUTTON_MIDDLE;

            float mx = AMotionEvent_getX(event, 0);
            float my = AMotionEvent_getY(event, 0);

            const bool is_button_press   = (action == AMOTION_EVENT_ACTION_BUTTON_PRESS);
            const bool is_button_release = (action == AMOTION_EVENT_ACTION_BUTTON_RELEASE);
            // On Android mouse, DOWN is paired with BUTTON_PRESS → don't
            // process DOWN to avoid double counting. Touch fallback lives
            // in its own block.
            const bool is_down = false;
            const bool is_up   = false;

            if (is_button_press || is_down) {
                int use_btn = btn;
                if (use_btn == 0) use_btn = KNST_WINDOW_MOUSE_BUTTON_LEFT;
                window->m_last_mouse_button = use_btn;   // remember for release

                window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
                window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_PRESS;
                window->m_knst_event.mouse_button = use_btn;
                window->m_knst_event.mouse_x = (int32_t)mx;
                window->m_knst_event.mouse_y = (int32_t)my;
                window->dispatch_current_event();
                return 1;
            }
            else if (is_button_release || is_up) {
                int use_btn = window->m_last_mouse_button;
                if (use_btn == 0) use_btn = KNST_WINDOW_MOUSE_BUTTON_LEFT;

                window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
                window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_RELEASE;
                window->m_knst_event.mouse_button = use_btn;
                window->m_knst_event.mouse_x = (int32_t)mx;
                window->m_knst_event.mouse_y = (int32_t)my;
                window->dispatch_current_event();
                window->m_last_mouse_button = 0;
                return 1;
            }
            else if (action == AMOTION_EVENT_ACTION_HOVER_MOVE ||
                     action == AMOTION_EVENT_ACTION_MOVE ||
                     action == AMOTION_EVENT_ACTION_HOVER_ENTER ||
                     action == AMOTION_EVENT_ACTION_HOVER_EXIT ||
                     action == AMOTION_EVENT_ACTION_SCROLL) {

                // Scroll events arrive as MOTION on Android, carrying
                // AXIS_VSCROLL / HSCROLL. This check used to live in the
                // touch block — unreachable code.
                float vscroll = AMotionEvent_getAxisValue(
                    event, AMOTION_EVENT_AXIS_VSCROLL, 0);
                float hscroll = AMotionEvent_getAxisValue(
                    event, AMOTION_EVENT_AXIS_HSCROLL, 0);

                if (vscroll != 0.0f || hscroll != 0.0f) {
                    // Fractional deltas accumulate. (int)(-vscroll) of 0.5
                    // → 0 would lose the scroll. Use an accumulator.
                    // Android AXIS_VSCROLL positive = up, matching other
                    // platforms — negating would flip the direction.
                    window->m_scroll_accum_v += (double)(vscroll);
                    window->m_scroll_accum_h += (double)(hscroll);

                    int notches_v = (int)window->m_scroll_accum_v;
                    int notches_h = (int)window->m_scroll_accum_h;
                    if (notches_v != 0) window->m_scroll_accum_v -= notches_v;
                    if (notches_h != 0) window->m_scroll_accum_h -= notches_h;

                    if (notches_v != 0 || notches_h != 0) {
                        window->m_knst_event.begin(KNST_WINDOW_EVENT_MOUSE);
                        window->m_knst_event.mouse_action = KNST_WINDOW_MOUSE_ACTION_SCROLL;
                            
                        window->m_knst_event.mouse_scroll_delta = notches_v != 0 ? notches_v : notches_h;
                           
                        window->m_knst_event.mouse_button = notches_v != 0 ? 4 : 6;
                           
                        window->m_knst_event.mouse_x = (int32_t)mx;
                        window->m_knst_event.mouse_y = (int32_t)my;
                        window->dispatch_current_event();
                    }
                    return 1;
                }

                window->m_knst_event.begin(KNST_WINDOW_EVENT_MOTION);
                window->m_knst_event.mouse_x = (int32_t)mx;
                window->m_knst_event.mouse_y = (int32_t)my;
                window->dispatch_current_event();
                return 1;
            }

            return 1;   // consume other mouse events
        }

        // Joystick, trackball, touchpad → not a touch. They report axis
        // values (-1..1) via AMotionEvent_getX; treating them as touch
        // draws a fake circle at the corner.
        if (!is_touch) {
            return 0;
        }

        // ── Touch event ──────────────────────────────────────────────
        // If touch is disabled, don't emit anything —
        // enable_touch_screen(false) should actually take effect.
        if (!window->m_touch_enabled) {
            return 1;
        }

        // pointerCount can exceed KNST_MAX_TOUCH_POINTS. Clamp to avoid
        // OOB reads.
        int safe_count = pointerCount;
        if (safe_count > KNST_MAX_TOUCH_POINTS) safe_count = KNST_MAX_TOUCH_POINTS;
        if (safe_count < 0)                     safe_count = 0;

        // Ghost finger fix: Android includes the lifting finger in the
        // snapshot on ACTION_POINTER_UP. If we leave it in, the user's
        // released finger keeps drawing a ghost circle. Look up the
        // lifting finger's index via ACTION_POINTER_INDEX_MASK and skip
        // it in the loop.
        const int32_t raw_action2 = AMotionEvent_getAction(event);
        const int up_idx =
            (raw_action2 & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
            >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        const bool is_pointer_up = (action == AMOTION_EVENT_ACTION_POINTER_UP);

        window->m_knst_event.begin(KNST_WINDOW_EVENT_TOUCH);

        int out = 0;
        for (int i = 0; i < safe_count; ++i) {
            if (is_pointer_up && i == up_idx) continue;   // skip lifting finger
            window->m_knst_event.pointer_x[out]  = AMotionEvent_getX(event, i);
            window->m_knst_event.pointer_y[out]  = AMotionEvent_getY(event, i);
            window->m_knst_event.pointer_id[out] = AMotionEvent_getPointerId(event, i);
            ++out;
        }
        window->m_knst_event.pointer_count = out;

        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
                window->m_knst_event.touch_action = KNST_WINDOW_TOUCH_ACTION_PRESS;
                break;
            case AMOTION_EVENT_ACTION_UP:
                window->m_knst_event.touch_action = KNST_WINDOW_TOUCH_ACTION_RELEASE;
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                window->m_knst_event.touch_action = KNST_WINDOW_TOUCH_ACTION_MOVE;
                break;
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                window->m_knst_event.touch_action = KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS;
                break;
            case AMOTION_EVENT_ACTION_POINTER_UP:
                window->m_knst_event.touch_action = KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE;
                break;
            case AMOTION_EVENT_ACTION_CANCEL:
                window->m_knst_event.touch_action = KNST_WINDOW_TOUCH_ACTION_CANCEL;
                break;
            case AMOTION_EVENT_ACTION_OUTSIDE:
                window->m_knst_event.touch_action = KNST_WINDOW_TOUCH_ACTION_OUTSIDE;
                break;
            default:
                // Unknown action (stylus hover etc.) → emit nothing,
                // otherwise touch_action = 0 reports as PRESS.
                return 1;
        }

        window->dispatch_current_event();
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t keyCode = AKeyEvent_getKeyCode(event);
        int32_t action = AKeyEvent_getAction(event);
        int32_t metaState = AKeyEvent_getMetaState(event);

        if (keyCode == AKEYCODE_VOLUME_UP && action == AKEY_EVENT_ACTION_DOWN) {
            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOBILE_VOLUME_UP);
            window->m_knst_event.key_code = keyCode;
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
            window->dispatch_current_event();
            return 0;
        }
        else if (keyCode == AKEYCODE_VOLUME_DOWN && action == AKEY_EVENT_ACTION_DOWN) {
            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOBILE_VOLUME_DOWN);
            window->m_knst_event.key_code = keyCode;
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
            window->dispatch_current_event();
            return 0;
        }
        else if (keyCode == AKEYCODE_VOLUME_MUTE && action == AKEY_EVENT_ACTION_DOWN) {
            window->m_knst_event.begin(KNST_WINDOW_EVENT_MOBILE_VOLUME_MUTE);
            window->m_knst_event.key_code = keyCode;
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
            window->dispatch_current_event();
            return 0;
        }

        // Android scancodes are unreliable across keyboards (soft vs
        // physical report different values for the same key). Use
        // keyCode as scancode for consistent held-key tracking.
        int scancode = keyCode;

        // ═══════════════════════════════════════════════════════════════
        //  Android AKEYCODE_* → KNST_WINDOW_KEY_CODE_* conversion
        //
        //  AKEYCODE_* values overlap with ASCII letters/digits:
        //    ENTER=66='B', DEL=67='C', ESCAPE=111='o', MENU=82='R',
        //    COMMA=55='7', PERIOD=56='8', ALT_L=57='9', and many more.
        //
        //  Fix: A-Z and 0-9 map to ASCII, everything else gets a
        //  +0x10000 offset. The KNST_WINDOW_KEY_CODE_* macros in
        //  identifiers.hpp apply the same offset. User code is
        //  unaffected.
        // ═══════════════════════════════════════════════════════════════
        int32_t originalKeyCode = keyCode;
        int32_t ascii_char = 0;   // for TEXT_INPUT (0 = not printable)

        if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
            ascii_char = 'A' + (keyCode - AKEYCODE_A);
            keyCode = ascii_char;
        }
        else if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
            ascii_char = '0' + (keyCode - AKEYCODE_0);
            keyCode = ascii_char;
        }
        else if (keyCode == AKEYCODE_SPACE) {
            ascii_char = ' ';
            keyCode = KNST_WINDOW_KEY_CODE_SPACE;
        }
        else {
            keyCode = keyCode + KNST_ANDROID_KEY_OFFSET;
        }

        if (action == AKEY_EVENT_ACTION_DOWN) {
            if (window->find_held_by_scancode(scancode)) {
                return 1;
            }
            window->add_held_key(keyCode, scancode,
                KnstWindowSources::get_current_time_ms());

            // TEXT_INPUT via JNI getUnicodeChar — handles Shift+letter,
            // punctuation, Turkish characters, and IME composition
            // correctly. The old hand-mapped ascii_char is gone.
            //
            // Skip this path when the InputConnection is active — both
            // commitText and KeyEvent would send the same character
            // twice. Long-press popup characters (u → ü) never reach
            // here anyway; only commitText catches them.
            (void)ascii_char;
            if (!knst_android_jni::input_connection_active()) {
                int32_t cp = knst_android_unicode_char(app, originalKeyCode,  metaState);
                // Dead key or control char → ignore. getUnicodeChar sets
                // the high bit for dead keys.
                if (((uint32_t)cp & 0x80000000u) != 0 ||
                    cp < 0x20 || cp == 0x7F) {
                    cp = 0;
                }
                if (cp != 0) {
                    char u8[5] = {0};
                    const int un = knst_cp_to_utf8((uint32_t)cp, u8);

                    window->m_knst_event.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
                    window->m_knst_event.text_input = knst_c16string(u8, (uint32_t)un);
                    window->dispatch_current_event();

                    // Store the UTF-8 in the held-key entry so auto-repeat
                    // can produce TEXT_INPUT later.
                    if (auto* hk = window->find_held_by_scancode(scancode)) {
                        if (un < (int)sizeof(hk->utf8)) {
                            memcpy(hk->utf8, u8, (size_t)un);
                            hk->utf8_len = (uint8_t)un;
                        }
                    }
                }
            }

            window->m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
            window->m_knst_event.key_code = keyCode;
            window->m_knst_event.scancode = scancode;
        } else if (action == AKEY_EVENT_ACTION_UP) {
            window->remove_held_key(scancode);
            window->m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_RELEASE;
            window->m_knst_event.key_code = keyCode;
            window->m_knst_event.scancode = scancode;
        } else {
            return 1;
        }

        window->m_knst_event.mods = 0;
        if (metaState & AMETA_SHIFT_ON) window->m_knst_event.mods |= KNST_WINDOW_MOD_SHIFT;
        if (metaState & AMETA_CTRL_ON)window->m_knst_event.mods |= KNST_WINDOW_MOD_CONTROL;
        if (metaState & AMETA_ALT_ON) window->m_knst_event.mods |= KNST_WINDOW_MOD_ALT;
        if (metaState & AMETA_META_ON)window->m_knst_event.mods |= KNST_WINDOW_MOD_SUPER;
        if (metaState & AMETA_CAPS_LOCK_ON) window->m_knst_event.mods |= KNST_WINDOW_MOD_CAPS_LOCK;
        if (metaState & AMETA_NUM_LOCK_ON)  window->m_knst_event.mods |= KNST_WINDOW_MOD_NUM_LOCK;

        if (action == AKEY_EVENT_ACTION_DOWN) {
            switch (originalKeyCode) {
                case AKEYCODE_BACK:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_BACK;
                    break;
                case AKEYCODE_HOME:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_HOME;
                    break;
                case AKEYCODE_MENU:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MENU;
                    break;
                case AKEYCODE_SEARCH:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_SEARCH;
                    break;
                case AKEYCODE_APP_SWITCH:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_APP_SWITCH;
                    break;
                case AKEYCODE_MEDIA_PLAY_PAUSE:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MEDIA_PLAY_PAUSE;
                    break;
                case AKEYCODE_MEDIA_STOP:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MEDIA_STOP;
                    break;
                case AKEYCODE_MEDIA_NEXT:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MEDIA_NEXT;
                    break;
                case AKEYCODE_MEDIA_PREVIOUS:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MEDIA_PREVIOUS;
                    break;
                case AKEYCODE_MEDIA_REWIND:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MEDIA_REWIND;
                    break;
                case AKEYCODE_MEDIA_FAST_FORWARD:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MEDIA_FORWARD;
                    break;
                case AKEYCODE_MEDIA_RECORD:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MEDIA_RECORD;
                    break;
                case AKEYCODE_MEDIA_PAUSE:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MEDIA_PAUSE;
                    break;
                case AKEYCODE_POWER:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_POWER;
                    break;
                case AKEYCODE_CAMERA:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_CAMERA;
                    break;
                case AKEYCODE_HELP:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_HELP;
                    break;
                case AKEYCODE_SETTINGS:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_SETTINGS;
                    break;
                case AKEYCODE_SLEEP:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_SLEEP;
                    break;
                case AKEYCODE_WAKEUP:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_WAKEUP;
                    break;
                case AKEYCODE_ASSIST:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_ASSIST;
                    break;
                case AKEYCODE_BOOKMARK:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_BOOKMARK;
                    break;
                case AKEYCODE_CALCULATOR:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_CALCULATOR;
                    break;
                case AKEYCODE_CALENDAR:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_CALENDAR;
                    break;
                case AKEYCODE_CONTACTS:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_CONTACTS;
                    break;
                case AKEYCODE_EXPLORER:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_EXPLORER;
                    break;
                case AKEYCODE_MUSIC:
                    window->m_knst_event.type = KNST_WINDOW_EVENT_MOBILE_MUSIC;
                    break;
                default:
                    break;
            }
        }

        window->dispatch_current_event();
        return 1;
    }

    return 0;
}

#endif