// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_android_manager.hpp
----------------------------

    Function definitions for the Android backend.

*/

#pragma once

#if defined(KNST_USING_PLATFORM_ANDROID)

// Include the JNI bridge — this is what compiles every extern "C"
// JNIEXPORT function into the .so. Skip it and Java throws
// UnsatisfiedLinkError at runtime.
#include "knst_window_android_jni.hpp"


inline void knst_window::creation() noexcept {

    // Reset the destroy() double-call guard so a fresh window can be
    // torn down normally again.
    m_destroyed = false;

    // Init might have failed, or CleanUp might have run before us.
    // Without this check, every m_app-> access below would crash.
    if (KnstWindowSources::m_app == nullptr) {
        return;
    }

    // Touch is always available on Android.
    m_touch_supported  = true;
    m_max_touch_points = 10;
    #if defined(KNST_WINDOW_ENABLE_TOUCH_SCREEN)
        m_touch_enabled = true;
    #else
        m_touch_enabled = true;   // fundamental on Android, always on
    #endif

    // Between Init() and creation() the app might have been
    // backgrounded — m_app or m_app->window could be NULL.
    if (KnstWindowSources::m_app && KnstWindowSources::m_app->window) {
        m_knst_event.window_width = ANativeWindow_getWidth(KnstWindowSources::m_app->window);
        m_knst_event.window_height = ANativeWindow_getHeight(KnstWindowSources::m_app->window);
    } else {
        m_knst_event.window_width  = 0;
        m_knst_event.window_height = 0;
    }
    m_knst_event.window_root_x   = 0;
    m_knst_event.window_root_y   = 0;
    m_knst_event.is_focused      = true;
    m_knst_event.mouse_on_window = true;

    KnstWindowSources::m_app->userData     = this;
    KnstWindowSources::m_app->onAppCmd     = cmd_callback;
    KnstWindowSources::m_app->onInputEvent = input_callback;

    knst_window_event_system::register_window(this);

    // Register with the JNI bridge so IME text events land in this
    // window's queue.
    knst_android_jni::set_active_window(this);

    // Fire the density event manually here. Why not wait for
    // APP_CMD_INIT_WINDOW? Because that event fires during
    // KnstWindowSources::Init() — before onAppCmd is even installed
    // (creation() hasn't run yet). So the callback is never invoked
    // and g_ui_scale stays 1.0 → tiny UI on high-DPI phones.
    //
    // Fix: emit it ourselves now that onAppCmd is wired up.
    if (KnstWindowSources::m_app != nullptr &&
        KnstWindowSources::m_app->config != nullptr) {

        int density = AConfiguration_getDensity(
            KnstWindowSources::m_app->config);

        // DENSITY_ANY / NONE are sentinels — fall back to 1.0x.
        if (density == ACONFIGURATION_DENSITY_ANY ||
            density == ACONFIGURATION_DENSITY_NONE ||
            density <= 0) {
            density = 160;
        }

        m_knst_event.begin(KNST_WINDOW_EVENT_APP_CONFIG_CHANGED);
        m_knst_event.density = (float)density / 160.0f;
        dispatch_current_event();
    }
}

inline void knst_window::show() noexcept {
    // Nothing to do — the activity's surface is already visible.
    m_knst_event.type = KNST_WINDOW_EVENT_EXPOSE;
}


inline void knst_window::destroy() noexcept {

    // Double-call guard: the example calls destroy() explicitly and
    // then the destructor calls it again.
    if (m_destroyed) return;
    m_destroyed = true;

    knst_window_event_system::unregister_window(this);

    // Drop the JNI registration so IME events stop reaching us.
    if (knst_android_jni::get_active_window() == this) {
        knst_android_jni::set_active_window(nullptr);
    }

    notify_window_destroyed();

    // Null out app->userData if it still points to us. The callbacks
    // (onAppCmd / onInputEvent) stay installed — they check userData
    // and return early when it's null. Keeping them avoids fighting
    // native_app_glue during reset / cleanup.
    if (KnstWindowSources::m_app &&
        KnstWindowSources::m_app->userData == this) {
        KnstWindowSources::m_app->userData = nullptr;
    }
}


inline void knst_window::toggle_fullscreen(bool fullscreen) noexcept {
    // Android does not support this.
    m_knst_event.is_full_screen = fullscreen;
}

inline void knst_window::set_minimized() noexcept {
    // Android does not support this.
    m_knst_event.is_minimized = true;
}

inline void knst_window::set_maximized() noexcept {
    // Android does not support this.
    m_knst_event.is_maximized = true;
}

inline void knst_window::restore() noexcept {
    m_knst_event.is_minimized = false;
    m_knst_event.is_maximized = false;
    m_knst_event.is_full_screen = false;
}


inline void knst_window::move(int root_x, int root_y, const knst_monitor& monitor) noexcept {
    (void)root_x; (void)root_y; (void)monitor;
}

inline void knst_window::move(int root_x, int root_y) noexcept {
    (void)root_x; (void)root_y;
}

inline void knst_window::resize(int width, int height) noexcept {
    (void)width; (void)height;
}

inline void knst_window::hide() noexcept {
    // Android does not support this.
}

inline void knst_window::focus() noexcept {
    // Android does not support this.
}

inline void knst_window::set_title(const knst_c16string& title) noexcept {
    // Android does not support this.
    m_title = title;
}

inline void knst_window::set_opacity(float opacity) noexcept {
    // null for now
}

inline void knst_window::set_attribute(int attribute, bool value) noexcept {
    (void)attribute; (void)value;
}

inline bool knst_window::get_attribute(int attribute) const noexcept {
    (void)attribute;
    return false;
}


inline void knst_window::set_cursor(uint16_t cursor_type) noexcept {
    (void)cursor_type;
}

inline void knst_window::set_bmp_cursor(const knst_byte_string& data, int width, int height, int hot_x, int hot_y) noexcept {
    (void)data; (void)width; (void)height; (void)hot_x; (void)hot_y;
}

inline void knst_window::reset_cursor() noexcept {
    // Android does not support this.
}

inline void knst_window::set_cursor_mode(int mode) noexcept {
    (void)mode;
}

inline void knst_window::set_cursor_pos_on_window(int x, int y) noexcept {
    (void)x; (void)y;
}

inline void knst_window::set_cursor_pos_global(int root_x, int root_y) noexcept {
    (void)root_x; (void)root_y;
}


inline void knst_window::set_clipboard(const knst_c16string& text) noexcept {
    android_app* app = KnstWindowSources::m_app;
    if (app == nullptr || app->activity == nullptr) return;

    clipboard_text = text;

    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;

    int result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != 0) return;
        attached = true;
    }

    if (env == nullptr) {
        if (attached) vm->DetachCurrentThread();
        return;
    }

    // PushLocalFrame auto-frees every local ref on PopLocalFrame, even
    // on error paths. Keeps us under the 512 local ref JNI limit
    // without manual DeleteLocalRef everywhere.
    if (env->PushLocalFrame(16) != 0) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (attached) vm->DetachCurrentThread();
        return;
    }

    jobject activity = app->activity->clazz;

    jclass contextClass = env->GetObjectClass(activity);
    if (contextClass != nullptr) {
        jfieldID clipboardServiceField = env->GetStaticFieldID(
            contextClass, "CLIPBOARD_SERVICE", "Ljava/lang/String;");

        jstring clipboardServiceStr = nullptr;
        if (clipboardServiceField) {
            clipboardServiceStr = (jstring)env->GetStaticObjectField(
                contextClass, clipboardServiceField);
        }

        jobject clipboardManager = nullptr;
        if (clipboardServiceStr) {
            jmethodID getSystemServiceMethod = env->GetMethodID(
                contextClass, "getSystemService",
                "(Ljava/lang/String;)Ljava/lang/Object;");
            if (getSystemServiceMethod) {
                clipboardManager = env->CallObjectMethod(
                    activity, getSystemServiceMethod, clipboardServiceStr);
            }
        }

        if (clipboardManager != nullptr) {
            jclass clipboardClass = env->GetObjectClass(clipboardManager);
            jmethodID setPrimaryClipMethod = clipboardClass
                ? env->GetMethodID(clipboardClass, "setPrimaryClip",
                    "(Landroid/content/ClipData;)V")
                : nullptr;

            jclass clipDataClass = env->FindClass("android/content/ClipData");
            jmethodID newPlainTextMethod = clipDataClass
                ? env->GetStaticMethodID(clipDataClass, "newPlainText",
                    "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)"
                    "Landroid/content/ClipData;")
                : nullptr;

            if (setPrimaryClipMethod && newPlainTextMethod) {
                // NewStringUTF expects modified UTF-8 — a 4-byte emoji
                // would abort under CheckJNI or corrupt the text. Use
                // the UTF-16 API. knst_c16string is already UTF-16, so
                // it goes straight through.
                jstring jText = env->NewString(
                    reinterpret_cast<const jchar*>(text.data()),
                    (jsize)text.length());
                if (jText) {
                    jobject clipData = env->CallStaticObjectMethod(
                        clipDataClass, newPlainTextMethod, jText, jText);
                    if (clipData) {
                        env->CallVoidMethod(clipboardManager,setPrimaryClipMethod, clipData);
                    }
                }
            }
        }
    }

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    env->PopLocalFrame(nullptr);

    if (attached) vm->DetachCurrentThread();
}

inline void knst_window::request_clipboard() noexcept {
    android_app* app = KnstWindowSources::m_app;
    if (app == nullptr || app->activity == nullptr) {
        m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
        dispatch_current_event();
        return;
    }

    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;

    int result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != 0) {
            m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
            dispatch_current_event();
            return;
        }
        attached = true;
    }

    if (env == nullptr) {
        if (attached) vm->DetachCurrentThread();
        m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
        dispatch_current_event();
        return;
    }

    // PushLocalFrame — every local ref is freed on PopLocalFrame.
    if (env->PushLocalFrame(16) == 0) {
        jobject activity = app->activity->clazz;

        jclass contextClass = env->GetObjectClass(activity);
        jobject clipboardManager = nullptr;

        if (contextClass) {
            jfieldID clipboardServiceField = env->GetStaticFieldID(
                contextClass, "CLIPBOARD_SERVICE", "Ljava/lang/String;");
            jstring clipboardServiceStr = clipboardServiceField
                ? (jstring)env->GetStaticObjectField(
                    contextClass, clipboardServiceField)
                : nullptr;

            jmethodID getSystemServiceMethod = env->GetMethodID(
                contextClass, "getSystemService",
                "(Ljava/lang/String;)Ljava/lang/Object;");
            if (clipboardServiceStr && getSystemServiceMethod) {
                clipboardManager = env->CallObjectMethod(
                    activity, getSystemServiceMethod, clipboardServiceStr);
            }
        }

        if (clipboardManager) {
            jclass clipboardClass = env->GetObjectClass(clipboardManager);
            jmethodID getPrimaryClipMethod = clipboardClass
                ? env->GetMethodID(clipboardClass, "getPrimaryClip",
                    "()Landroid/content/ClipData;")
                : nullptr;
            jobject clipData = getPrimaryClipMethod
                ? env->CallObjectMethod(clipboardManager, getPrimaryClipMethod)
                : nullptr;

            if (clipData) {
                jclass clipDataClass = env->GetObjectClass(clipData);
                jmethodID getItemCountMethod = clipDataClass
                    ? env->GetMethodID(clipDataClass, "getItemCount", "()I")
                    : nullptr;
                jint count = getItemCountMethod
                    ? env->CallIntMethod(clipData, getItemCountMethod)
                    : 0;

                if (count > 0) {
                    jmethodID getItemAtMethod = env->GetMethodID(
                        clipDataClass, "getItemAt",
                        "(I)Landroid/content/ClipData$Item;");
                    jobject clipItem = getItemAtMethod
                        ? env->CallObjectMethod(clipData, getItemAtMethod, 0)
                        : nullptr;

                    if (clipItem) {
                        jclass clipItemClass = env->GetObjectClass(clipItem);
                        jmethodID getTextMethod = clipItemClass
                            ? env->GetMethodID(clipItemClass, "getText",
                                "()Ljava/lang/CharSequence;")
                            : nullptr;
                        jobject text = getTextMethod
                            ? env->CallObjectMethod(clipItem, getTextMethod)
                            : nullptr;

                        if (text) {
                            jclass charSequenceClass = env->GetObjectClass(text);
                            jmethodID toStringMethod = charSequenceClass
                                ? env->GetMethodID(charSequenceClass,
                                    "toString", "()Ljava/lang/String;")
                                : nullptr;
                            jstring jText = toStringMethod
                                ? (jstring)env->CallObjectMethod(
                                    text, toStringMethod)
                                : nullptr;

                            if (jText) {
                                // GetStringUTFChars returns modified UTF-8
                                // — emoji get corrupted or CheckJNI aborts.
                                // Use the UTF-16 API.
                                const jchar* ch = env->GetStringChars(
                                    jText, nullptr);
                                if (ch) {
                                    const jsize n = env->GetStringLength(jText);
                                    knst_c16string tmp;
                                    for (jsize k = 0; k < n; ++k) {
                                        tmp += static_cast<char16_t>(ch[k]);
                                    }
                                    clipboard_text = tmp;
                                    env->ReleaseStringChars(jText, ch);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        env->PopLocalFrame(nullptr);
    }

    if (attached) vm->DetachCurrentThread();

    m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
    dispatch_current_event();
}


inline void knst_window::set_drag_drop_status(bool enabled) noexcept {
    // No OS-level DnD on Android. We still store the flag so
    // get_attribute() stays consistent with other platforms.
    m_drag_drop_enabled = enabled;
}

inline void knst_window::enable_touch_screen(bool enable) noexcept {
    // Android always reports touch. The flag only controls whether the
    // app receives KNST_WINDOW_EVENT_TOUCH events.
    m_touch_enabled = enable;
}


inline void knst_window::apply_bmp_icon(const knst_byte_string& data, int width, int height) noexcept {
    (void)data; (void)width; (void)height;
}


inline void knst_window::set_minimum_size(int width, int height) noexcept {
    (void)width; (void)height;
}

inline void knst_window::set_maximum_size(int width, int height) noexcept {
    (void)width; (void)height;
}


// ─── knst_mobile_keyboard — Android soft keyboard bridge ───────────────────

inline knst_mobile_keyboard::jni_scope::jni_scope(JavaVM* vm) : vm(vm) {
    if (vm == nullptr) return;

    int result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) == 0) {
            attached = true;
        }
    }
}

inline knst_mobile_keyboard::jni_scope::~jni_scope() {
    if (attached && vm != nullptr) {
        vm->DetachCurrentThread();
    }
}


inline void knst_mobile_keyboard::Init(struct android_app* app) {
    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_initialized) return;
    if (app == nullptr || app->activity == nullptr) return;

    s_javaVM = app->activity->vm;
    if (s_javaVM == nullptr) return;

    jni_scope scope(s_javaVM);
    if (!scope.valid()) return;

    JNIEnv* env = scope.get();
    jobject activity = app->activity->clazz;

    s_activityClass = (jclass)env->NewGlobalRef(env->GetObjectClass(activity));
    s_activity = env->NewGlobalRef(activity);

    s_getSystemService = env->GetMethodID(
        s_activityClass,
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;"
    );

    s_getWindow = env->GetMethodID(
        s_activityClass,
        "getWindow",
        "()Landroid/view/Window;"
    );

    jclass immClass = env->FindClass("android/view/inputmethod/InputMethodManager");
    if (immClass != nullptr) {
        s_immClass = (jclass)env->NewGlobalRef(immClass);
        env->DeleteLocalRef(immClass);

        s_showSoftInput = env->GetMethodID(
            s_immClass,
            "showSoftInput",
            "(Landroid/view/View;I)Z"
        );

        s_hideSoftInput = env->GetMethodID(
            s_immClass,
            "hideSoftInputFromWindow",
            "(Landroid/os/IBinder;I)Z"
        );
    }

    jclass windowClass = env->FindClass("android/view/Window");
    if (windowClass != nullptr) {
        s_getDecorView = env->GetMethodID(
            windowClass,
            "getDecorView",
            "()Landroid/view/View;"
        );
        env->DeleteLocalRef(windowClass);
    }

    jclass viewClass = env->FindClass("android/view/View");
    if (viewClass != nullptr) {
        s_getWindowToken = env->GetMethodID(
            viewClass,
            "getWindowToken",
            "()Landroid/os/IBinder;"
        );
        env->DeleteLocalRef(viewClass);
    }

    s_initialized = true;
}


inline bool knst_mobile_keyboard::show() {
    std::lock_guard<std::mutex> lock(s_mutex);

    if (!s_initialized) return false;
    if (s_javaVM == nullptr) return false;
    if (s_activityClass == nullptr) return false;

    jni_scope scope(s_javaVM);
    if (!scope.valid()) return false;

    JNIEnv* env = scope.get();

    // Do NOT FindClass("com/knst/window/KnstActivity") from the native
    // thread — app classes only resolve through the UI thread's
    // ClassLoader, so FindClass returns null here.
    //
    // s_activityClass (cached in Init) already points at KnstActivity.
    // GetStaticMethodID searches parents, so showKeyboard resolves.
    jmethodID method = env->GetStaticMethodID(s_activityClass, "showKeyboard", "()Z");
    if (method == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }

    jboolean ok = env->CallStaticBooleanMethod(s_activityClass, method);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return false;
    }

    if (ok) s_visible = true;
    return ok;
}

inline bool knst_mobile_keyboard::hide() {
    std::lock_guard<std::mutex> lock(s_mutex);

    if (!s_initialized)  return false;
    if (s_javaVM == nullptr) return false;
    if (s_activityClass == nullptr) return false;

    jni_scope scope(s_javaVM);
    if (!scope.valid()) return false;

    JNIEnv* env = scope.get();

    jmethodID method = env->GetStaticMethodID(
        s_activityClass, "hideKeyboard", "()Z");
    if (method == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        s_visible = false;
        return false;
    }

    env->CallStaticBooleanMethod(s_activityClass, method);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        s_visible = false;
        return false;
    }

    s_visible = false;
    return true;
}


inline void knst_mobile_keyboard::toggle() {
    // Old code was `if (s_visible) return;` — never hid the keyboard,
    // and reading s_visible without the mutex was a data race. Now:
    // read via is_visible() (mutex-protected), then act.
    //
    // Debounce filters out rapid duplicate calls (double-tap on a key,
    // event flooding during animations).
    static uint64_t lastToggleTime = 0;
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();


    if (now - lastToggleTime < 300) {
        return;
    }

    lastToggleTime = now;

    if (is_visible()) {
        hide();
    } else {
        show();
    }
}


inline bool knst_mobile_keyboard::is_visible() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_visible;
}


inline void knst_mobile_keyboard::shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);

    if (!s_initialized) return;


    jni_scope scope(s_javaVM);
    if (scope.valid()) {
        JNIEnv* env = scope.get();

        if (s_activity != nullptr) {
            env->DeleteGlobalRef(s_activity);
            s_activity = nullptr;
        }
        if (s_activityClass != nullptr) {
            env->DeleteGlobalRef(s_activityClass);
            s_activityClass = nullptr;
        }
        if (s_immClass != nullptr) {
            env->DeleteGlobalRef(s_immClass);
            s_immClass = nullptr;
        }
    }

    s_javaVM = nullptr;
    s_getSystemService = nullptr;
    s_showSoftInput = nullptr;
    s_hideSoftInput = nullptr;
    s_getWindow = nullptr;
    s_getDecorView = nullptr;
    s_getWindowToken = nullptr;
    s_initialized = false;
    s_visible = false;
}


// ─── Android — platform-parity no-ops ──────────────────────────────────────
// These exist in the API surface and do real work on other platforms.
// Android has no hardware/protocol support, so they must be defined as
// no-ops or the linker complains.

inline void knst_window::set_raw_mouse_mode(bool enable) noexcept {
    (void)enable;
}

inline void knst_window::set_display_affinity(bool protect) noexcept {
    (void)protect;
}

inline void knst_window::set_primary_selection(const knst_c16string& text) noexcept {
    (void)text;
}

inline void knst_window::request_primary_selection() noexcept {
}

inline void knst_window::set_idle_inhibit(bool enable) noexcept {
    (void)enable;
}

inline void knst_window::csd_handle_hit(csd_hit hit, int x, int y, uint32_t input_serial) noexcept {
    (void)hit; (void)x; (void)y; (void)input_serial;
}

#endif