#ifndef KNST_WINDOW_ANDROID_MANAGER_HPP
#define KNST_WINDOW_ANDROID_MANAGER_HPP
#pragma once


#if defined(KNST_USING_PLATFORM_ANDROID)




inline void knst_window::creation() noexcept {
   
    m_knst_event.window_width = ANativeWindow_getWidth(KnstWindowSources::m_app->window);
    m_knst_event.window_height = ANativeWindow_getHeight(KnstWindowSources::m_app->window);
    m_knst_event.window_root_x = 0;
    m_knst_event.window_root_y = 0;
    m_knst_event.is_focused = true;
    m_knst_event.mouse_on_window = true;
   
   
    KnstWindowSources::m_app->userData = this;
    KnstWindowSources::m_app->onAppCmd = cmd_callback;
    KnstWindowSources::m_app->onInputEvent = input_callback;
   
    
    
    knst_window_event_system::register_window(this);
    
}



inline void knst_window::show() noexcept {
    // We don't need to do anything it's already ready.
    m_knst_event.type = KNST_EXPOSE;
    
}


inline void knst_window::destroy() noexcept {

    knst_window_event_system::unregister_window(this);


}




inline void knst_window::toggle_fullscreen(bool fullscreen) noexcept {
   // The Android side does not support it.
    m_knst_event.is_full_screen = fullscreen;
}

inline void knst_window::set_minimized() noexcept {
    // The Android side does not support it.
    m_knst_event.is_minimized = true;
}

inline void knst_window::set_maximized() noexcept {
    // The Android side does not support it.
    m_knst_event.is_maximized = true;
}

inline void knst_window::restore() noexcept {
    m_knst_event.is_minimized = false;
    m_knst_event.is_maximized = false;
    m_knst_event.is_full_screen = false;
}



inline void knst_window::move(int root_x, int root_y, const knst_monitor& monitor) noexcept {
   // The Android side does not support it.
    (void)root_x; (void)root_y; (void)monitor;
}

inline void knst_window::move(int root_x, int root_y) noexcept {
    (void)root_x; (void)root_y;
}

inline void knst_window::resize(int width, int height) noexcept {
    // The Android side does not support it.
    (void)width; (void)height;
}

inline void knst_window::hide() noexcept {
   // The Android side does not support it.
}

inline void knst_window::focus() noexcept {
    // The Android side does not support it.
}


inline void knst_window::set_title(const knst_c16string& title) noexcept {
    // The Android side does not support it.
    m_title = title;
}

inline void knst_window::set_opacity(float opacity) noexcept {
    //null for now
}

inline void knst_window::set_attribute(int attribute, bool value) noexcept {
    // The Android side does not support it.
    (void)attribute; (void)value;
}

inline bool knst_window::get_attribute(int attribute) const noexcept {
    (void)attribute;
    return false;
}



inline void knst_window::set_cursor(uint16_t cursor_type) noexcept {
    // The Android side does not support it.
    (void)cursor_type;
}

inline void knst_window::set_bmp_cursor(const knst_byte_string& data, int width, int height, int hot_x, int hot_y) noexcept {
    (void)data; (void)width; (void)height; (void)hot_x; (void)hot_y;
}

inline void knst_window::reset_cursor() noexcept {
   // The Android side does not support it.
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
    
    jobject activity = app->activity->clazz;
    
    
    jclass contextClass = env->GetObjectClass(activity);
    jfieldID clipboardServiceField = env->GetStaticFieldID(
        contextClass,
        "CLIPBOARD_SERVICE",
        "Ljava/lang/String;"
    );
    jstring clipboardServiceStr = (jstring)env->GetStaticObjectField(
        contextClass,
        clipboardServiceField
    );
    
    jmethodID getSystemServiceMethod = env->GetMethodID(
        contextClass,
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;"
    );
    jobject clipboardManager = env->CallObjectMethod(
        activity,
        getSystemServiceMethod,
        clipboardServiceStr
    );
    
    if (clipboardManager == nullptr) {
        if (attached) vm->DetachCurrentThread();
        return;
    }
    
    
    jclass clipboardClass = env->GetObjectClass(clipboardManager);
    
   
    jmethodID setPrimaryClipMethod = env->GetMethodID(
        clipboardClass,
        "setPrimaryClip",
        "(Landroid/content/ClipData;)V"
    );
    
   
    jclass clipDataClass = env->FindClass("android/content/ClipData");
    jmethodID newPlainTextMethod = env->GetStaticMethodID(
        clipDataClass,
        "newPlainText",
        "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;"
    );
    
    
    knst_byte_string utf8Text(text);
    jstring jText = env->NewStringUTF((const char*)utf8Text.data());
    
    jobject clipData = env->CallStaticObjectMethod(
        clipDataClass,
        newPlainTextMethod,
        jText,  // label
        jText   // text
    );
    
    env->CallVoidMethod(clipboardManager, setPrimaryClipMethod, clipData);
    
    
    env->DeleteLocalRef(jText);
    env->DeleteLocalRef(clipData);
    env->DeleteLocalRef(clipboardManager);
    env->DeleteLocalRef(clipboardClass);
    env->DeleteLocalRef(clipDataClass);
    
    if (attached) {
        vm->DetachCurrentThread();
    }
}

inline void knst_window::request_clipboard() noexcept {
    android_app* app = KnstWindowSources::m_app;
    if (app == nullptr || app->activity == nullptr) return;
    
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
    
    jobject activity = app->activity->clazz;
    
   
    jclass contextClass = env->GetObjectClass(activity);
    jfieldID clipboardServiceField = env->GetStaticFieldID(
        contextClass,
        "CLIPBOARD_SERVICE",
        "Ljava/lang/String;"
    );
    jstring clipboardServiceStr = (jstring)env->GetStaticObjectField(
        contextClass,
        clipboardServiceField
    );
    
    jmethodID getSystemServiceMethod = env->GetMethodID(
        contextClass,
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;"
    );
    jobject clipboardManager = env->CallObjectMethod(
        activity,
        getSystemServiceMethod,
        clipboardServiceStr
    );
    
    if (clipboardManager == nullptr) {
        if (attached) vm->DetachCurrentThread();
        return;
    }
    jclass clipboardClass = env->GetObjectClass(clipboardManager);
    jmethodID getPrimaryClipMethod = env->GetMethodID(
        clipboardClass,
        "getPrimaryClip",
        "()Landroid/content/ClipData;"
    );
    jobject clipData = env->CallObjectMethod(clipboardManager, getPrimaryClipMethod);
    
    if (clipData != nullptr) {
        
        jclass clipDataClass = env->GetObjectClass(clipData);
        jmethodID getItemCountMethod = env->GetMethodID(
            clipDataClass,
            "getItemCount",
            "()I"
        );
        jint count = env->CallIntMethod(clipData, getItemCountMethod);
        
        if (count > 0) {
            jmethodID getItemAtMethod = env->GetMethodID(
                clipDataClass,
                "getItemAt",
                "(I)Landroid/content/ClipData$Item;"
            );
            jobject clipItem = env->CallObjectMethod(clipData, getItemAtMethod, 0);
            
            if (clipItem != nullptr) {
                jclass clipItemClass = env->GetObjectClass(clipItem);
                jmethodID getTextMethod = env->GetMethodID(
                    clipItemClass,
                    "getText",
                    "()Ljava/lang/CharSequence;"
                );
                jobject text = env->CallObjectMethod(clipItem, getTextMethod);
                
                if (text != nullptr) {
                   
                    jclass charSequenceClass = env->GetObjectClass(text);
                    jmethodID toStringMethod = env->GetMethodID(
                        charSequenceClass,
                        "toString",
                        "()Ljava/lang/String;"
                    );
                    jstring jText = (jstring)env->CallObjectMethod(text, toStringMethod);
                    
                   
                    const char* utf8 = env->GetStringUTFChars(jText, nullptr);
                    clipboard_text = knst_c16string(utf8);
                    env->ReleaseStringUTFChars(jText, utf8);
                    
                    env->DeleteLocalRef(jText);
                }
                env->DeleteLocalRef(clipItem);
            }
        }
        env->DeleteLocalRef(clipData);
    }
    
  
    env->DeleteLocalRef(clipboardManager);
    env->DeleteLocalRef(clipboardClass);
    
    if (attached) {
        vm->DetachCurrentThread();
    }
}



inline void knst_window::set_drag_drop_status(bool enabled) noexcept {
    // The Android side does not support it.
    (void)enabled;
}



inline void knst_window::apply_bmp_icon(const knst_byte_string& data, int width, int height) noexcept {
    // The Android side does not support it.
    (void)data; (void)width; (void)height;
}



inline void knst_window::set_minimum_size(int width, int height) noexcept {
   // The Android side does not support it.
    (void)width; (void)height;
}

inline void knst_window::set_maximum_size(int width, int height) noexcept {
    // The Android side does not support it.
    (void)width; (void)height;
}





knst_mobile_keyboard::jni_scope::jni_scope(JavaVM* vm) : vm(vm) {
    if (vm == nullptr) return;
    
    int result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) == 0) {
            attached = true;
        }
    }
}

knst_mobile_keyboard::jni_scope::~jni_scope() {
    if (attached && vm != nullptr) {
        vm->DetachCurrentThread();
    }
}


void knst_mobile_keyboard::Init(struct android_app* app) {
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


bool knst_mobile_keyboard::show() {
    std::lock_guard<std::mutex> lock(s_mutex);
    
    if (!s_initialized) return false;
    if (s_javaVM == nullptr) return false;
    if (s_visible) return true;

    jni_scope scope(s_javaVM);
    if (!scope.valid()) return false;

    JNIEnv* env = scope.get();

    jstring serviceName = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(s_activity, s_getSystemService, serviceName);
    env->DeleteLocalRef(serviceName);

    if (imm == nullptr) return false;

    jobject window = env->CallObjectMethod(s_activity, s_getWindow);
    if (window == nullptr) {
        env->DeleteLocalRef(imm);
        return false;
    }

    jobject decorView = env->CallObjectMethod(window, s_getDecorView);
    env->DeleteLocalRef(window);

    if (decorView == nullptr) {
        env->DeleteLocalRef(imm);
        return false;
    }

    jboolean result = env->CallBooleanMethod(imm, s_showSoftInput, decorView, 0);
    
    env->DeleteLocalRef(imm);
    env->DeleteLocalRef(decorView);

    if (result) {
        s_visible = true;
    }
    return result;
}


bool knst_mobile_keyboard::hide() {
    std::lock_guard<std::mutex> lock(s_mutex);
    
    if (!s_initialized) return false;
    if (s_javaVM == nullptr) return false;
    if (!s_visible) return true;

    jni_scope scope(s_javaVM);
    if (!scope.valid()) return false;

    JNIEnv* env = scope.get();

    jstring serviceName = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(s_activity, s_getSystemService, serviceName);
    env->DeleteLocalRef(serviceName);

    if (imm == nullptr) return false;

    jobject window = env->CallObjectMethod(s_activity, s_getWindow);
    if (window == nullptr) {
        env->DeleteLocalRef(imm);
        return false;
    }

    jobject decorView = env->CallObjectMethod(window, s_getDecorView);
    env->DeleteLocalRef(window);

    if (decorView == nullptr) {
        env->DeleteLocalRef(imm);
        return false;
    }

    jobject windowToken = env->CallObjectMethod(decorView, s_getWindowToken);
    env->DeleteLocalRef(decorView);

    if (windowToken == nullptr) {
        env->DeleteLocalRef(imm);
        return false;
    }

    jboolean result = env->CallBooleanMethod(imm, s_hideSoftInput, windowToken, 0);
    
    env->DeleteLocalRef(imm);
    env->DeleteLocalRef(windowToken);

    if (result) {
        s_visible = false;
    } else {
        s_visible = false;
    }
    return true;
}


void knst_mobile_keyboard::toggle() {
  
    static uint64_t lastToggleTime = 0;
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    
    if (now - lastToggleTime < 300) {
        return;
    }
    lastToggleTime = now;
    
   
    if (s_visible) {
        return;
    }
    

    show();
}


bool knst_mobile_keyboard::is_visible() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_visible;
}


void knst_mobile_keyboard::shutdown() {
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




#endif
#endif // KNST_WINDOW_ANDROID_MANAGER_HPP