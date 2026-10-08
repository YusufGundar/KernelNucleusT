// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
knst_window_android_event_manager.hpp
----------------------------

    Event handling for the Android backend

*/


#pragma once

#if defined(KNST_USING_PLATFORM_ANDROID)
#include <thread>
#include <chrono>
KNST_FORCE_INLINE void cmd_callback(struct android_app* app, int32_t cmd) {
    knst_window* window = reinterpret_cast<knst_window*>(app->userData);
    if (window == nullptr) return;
    
    switch (cmd) {
                case APP_CMD_INPUT_CHANGED:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_INPUT_CHANGED);
            window->dispatch_current_event();
            break;
            
                case APP_CMD_INIT_WINDOW:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_EXPOSE);
            window->m_knst_event.window_width  = ANativeWindow_getWidth(app->window);
            window->m_knst_event.window_height = ANativeWindow_getHeight(app->window);
            window->dispatch_current_event();
            break;
            
                case APP_CMD_TERM_WINDOW:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_WINDOW_LOST);
            window->m_knst_event.window_width = 0;
            window->m_knst_event.window_height = 0;
            window->dispatch_current_event();
            break;
            
                case APP_CMD_WINDOW_RESIZED:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_RESIZE);
            window->m_knst_event.window_width = ANativeWindow_getWidth(app->window);
            window->m_knst_event.window_height = ANativeWindow_getHeight(app->window);
            window->dispatch_current_event();
            break;
            
                case APP_CMD_WINDOW_REDRAW_NEEDED:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_EXPOSE);
            window->dispatch_current_event();
            break;
            
                case APP_CMD_CONTENT_RECT_CHANGED:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_CONTENT_RECT);
            window->m_knst_event.content_left = app->contentRect.left;
            window->m_knst_event.content_top = app->contentRect.top;
            window->m_knst_event.content_right = app->contentRect.right;
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
            window->dispatch_current_event();
            break;
            
        case APP_CMD_CONFIG_CHANGED: {
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_CONFIG_CHANGED);
            AConfiguration* config = app->config;
            
            
            window->m_knst_event.orientation = AConfiguration_getOrientation(config);
            
            
            AConfiguration_getLanguage(config, window->m_knst_event.language);
            AConfiguration_getCountry(config, window->m_knst_event.country);
            
           
            #ifdef AConfiguration_getUiMode
                int uiMode = AConfiguration_getUiMode(config);
                window->m_knst_event.is_night_mode = (uiMode & ACONFIGURATION_UI_MODE_NIGHT_MASK) == ACONFIGURATION_UI_MODE_NIGHT_YES;
            #else
                window->m_knst_event.is_night_mode = false;
            #endif
            
            
            int density = AConfiguration_getDensity(config);
            window->m_knst_event.density = density > 0 ? (float)density / 160.0f : 1.0f;
            
            
            window->m_knst_event.screen_width_dp = AConfiguration_getScreenWidthDp(config);
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
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_SAVE_STATE);
            
           
            if (app->savedState != nullptr && app->savedStateSize > 0) {
                window->m_knst_event.saved_state = malloc(app->savedStateSize);
                if (window->m_knst_event.saved_state != nullptr) {
                    memcpy(window->m_knst_event.saved_state, app->savedState, app->savedStateSize);
                    window->m_knst_event.saved_state_size = app->savedStateSize;
                }
            }
            window->dispatch_current_event();
            break;
            
                    case APP_CMD_PAUSE:
            window->m_knst_event.begin(KNST_WINDOW_EVENT_APP_PAUSED);
            window->m_knst_event.is_focused = false;
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

KNST_FORCE_INLINE int32_t input_callback(struct android_app* app, AInputEvent* event) {
    KNST_LOG_INFO("[INPUT] callback FIRED  type=%d", AInputEvent_getType(event));
    knst_window* window = reinterpret_cast<knst_window*>(app->userData);
    if (window == nullptr) {
        KNST_LOG_INFO("[INPUT] window NULL");
        return 0;
    }
    
    int32_t type = AInputEvent_getType(event);
    
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        int pointerCount = AMotionEvent_getPointerCount(event);
        
        window->m_knst_event.begin(KNST_WINDOW_EVENT_MOBILE_TOUCH);
        window->m_knst_event.pointer_count = pointerCount;
        
        for (int i = 0; i < pointerCount && i < 10; i++) {
            window->m_knst_event.pointer_x[i] = AMotionEvent_getX(event, i);
            window->m_knst_event.pointer_y[i] = AMotionEvent_getY(event, i);
            window->m_knst_event.pointer_id[i] = AMotionEvent_getPointerId(event, i);
        }
        
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
                window->m_knst_event.touch_action = 0;
                break;
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
        
        int scancode = AKeyEvent_getScanCode(event);

        // Android AKEYCODE_* → ASCII normalize
        int32_t originalKeyCode = keyCode;
        if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
            keyCode = 'A' + (keyCode - AKEYCODE_A);
        }
        else if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
            keyCode = '0' + (keyCode - AKEYCODE_0);
        }

        if (action == AKEY_EVENT_ACTION_DOWN) {
            if (window->find_held_by_scancode(scancode)) {
                return 1;
            }
            window->add_held_key(keyCode, scancode,
                KnstWindowSources::get_current_time_ms());
            window->m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
            window->m_knst_event.key_code   = keyCode;
            window->m_knst_event.scancode   = scancode;
        } else if (action == AKEY_EVENT_ACTION_UP) {
            window->remove_held_key(scancode);
            window->m_knst_event.begin(KNST_WINDOW_EVENT_KEYBOARD);
            window->m_knst_event.key_action = KNST_WINDOW_KEY_ACTION_RELEASE;
            window->m_knst_event.key_code   = keyCode;
            window->m_knst_event.scancode   = scancode;
        } else {
            return 1;
        }
        
        window->m_knst_event.mods = 0;
        if (metaState & AMETA_SHIFT_ON) window->m_knst_event.mods |= KNST_WINDOW_MOD_SHIFT;
        if (metaState & AMETA_CTRL_ON) window->m_knst_event.mods |= KNST_WINDOW_MOD_CONTROL;
        if (metaState & AMETA_ALT_ON) window->m_knst_event.mods |= KNST_WINDOW_MOD_ALT;
        if (metaState & AMETA_META_ON) window->m_knst_event.mods |= KNST_WINDOW_MOD_SUPER;
        if (metaState & AMETA_CAPS_LOCK_ON) window->m_knst_event.mods |= KNST_WINDOW_MOD_CAPS_LOCK;
        if (metaState & AMETA_NUM_LOCK_ON) window->m_knst_event.mods |= KNST_WINDOW_MOD_NUM_LOCK;
        
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