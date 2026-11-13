// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_android_context.hpp
----------------------------
    Process-wide Android JNI context: JavaVM + Activity + cached ClassLoader.

    Problem solved
    --------------
    Before this header, every module held its own copy of the JNI state:

        knst_devices::detail_android::set_java_vm(vm, activity);
        knst_net_detail::android_jni::set_context(vm, activity);
        knst_prn::detail::set_jni_context(vm, activity);

    Three calls, three global refs, three classloader lookups.
    This class stores them ONCE. The three legacy init functions still
    work — the central init just calls them internally.

    Design
    ------
    • One JavaVM per process (JVM guarantee) — safe to store as a singleton.
    • Activity is stored as a NewGlobalRef (survives across the calling thread).
    • ClassLoader is cached — `findClass()` from a native thread needs it,
      because the system classloader cannot see app classes on Android.
    • update_activity() handles rotation / theme change (recreate) — refreshes
      the cached classloader (it can change).
    • Thread-safe reads (`vm()`, `activity()`, `env()`) from any thread.
    • Explicit init() / shutdown() — no hidden state, no magic.

    Typical use
    -----------
        void android_main(android_app* app) {
            knst_android::context::instance().init(app->activity->vm,
                                                   app->activity->clazz);
            // ... everything else works, no per-module init needed
        }

        void on_app_cmd(android_app* app, int32_t cmd) {
            if (cmd == APP_CMD_INIT_WINDOW)
                knst_android::context::instance().update_activity(app->activity->clazz);
            else if (cmd == APP_CMD_TERM_WINDOW)
                knst_android::context::instance().shutdown();
        }
*/

#pragma once
#if defined(KNST_USING_PLATFORM_ANDROID)

#include <jni.h>
#include <atomic>
#include <cstdio>

// `struct android_app` lives in native_app_glue.h. The session RAII wrapper
// needs it; other parts of the context do not. Guard so the library still
// compiles for Android targets that don't link native_app_glue.
#if __has_include(<android_native_app_glue.h>)
    #include <android_native_app_glue.h>
    #define KNST_HAVE_ANDROID_APP_STRUCT 1
#elif __has_include(<android/native_app_glue.h>)
    #include <android/native_app_glue.h>
    #define KNST_HAVE_ANDROID_APP_STRUCT 1
#elif __has_include(<native_app_glue.h>)
    #include <native_app_glue.h>
    #define KNST_HAVE_ANDROID_APP_STRUCT 1
#else
    #define KNST_HAVE_ANDROID_APP_STRUCT 0
#endif

namespace knst_android {

class context {
public:
    static context& instance() noexcept {
        static context c;
        return c;
    }

    // Store vm + activity (NewGlobalRef), cache the class loader,
    // then fan out to each module's set_*_context (defined in KernelNucleusT.hpp
    // once all module headers are available).
    //
    // Idempotent: calling with the same (vm, activity) is a no-op.
    // Returns true if the context is now valid.
    bool init(JavaVM* vm, jobject activity) noexcept {
        if (!vm || !activity) return false;

                JNIEnv* e = nullptr;
        if (vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_EDETACHED)
            vm->AttachCurrentThread(&e, nullptr);
        if (!e) return false;

        // Fast path: same Java OBJECT (survives update_activity() with the same Activity).
        // Comparing jobject pointers directly would fail — the local ref in `activity`
        // and the global ref stored in `activity_` point at different handle-table slots
        // even when they reference the same Java object. IsSameObject is authoritative.
        jobject cur = activity_.load(std::memory_order_acquire);
        if (vm_.load(std::memory_order_acquire) == vm && cur && e->IsSameObject(cur, activity))
            return true;

        jobject new_activity = e->NewGlobalRef(activity);
        if (!new_activity) return false;

        // Cache class loader (expensive: one JNI round trip, done once)
        jobject new_loader = nullptr;
        jclass act_cls = e->GetObjectClass(activity);
        if (act_cls) {
            jmethodID get_cl = e->GetMethodID(act_cls, "getClassLoader",
                                               "()Ljava/lang/ClassLoader;");
            if (get_cl) {
                jobject local = e->CallObjectMethod(activity, get_cl);
                if (local) {
                    new_loader = e->NewGlobalRef(local);
                    e->DeleteLocalRef(local);
                }
            }
            e->DeleteLocalRef(act_cls);
        }
        if (e->ExceptionCheck()) e->ExceptionClear();

        if (jobject old = activity_.exchange(new_activity, std::memory_order_acq_rel))
            e->DeleteGlobalRef(old);
        if (jobject old = class_loader_.exchange(new_loader, std::memory_order_acq_rel))
            e->DeleteGlobalRef(old);
        vm_.store(vm, std::memory_order_release);

        configure_modules(vm, activity);
        return true;
    }

    // Activity recreated (rotation, theme change). Same as init(vm, new_activity)
    // but keeps the stored vm.
    bool update_activity(jobject new_activity) noexcept {
        JavaVM* vm = vm_.load(std::memory_order_acquire);
        if (!vm || !new_activity) return false;
        return init(vm, new_activity);
    }

        // Release global refs. Safe to call multiple times.
    // DetachCurrentThread runs only if THIS call attached the thread —
    // a caller thread already attached before shutdown() is left untouched.
    void shutdown() noexcept {
        JavaVM* jvm = vm_.load(std::memory_order_acquire);
        if (!jvm) return;
        JNIEnv* e = nullptr;
        bool attached_here = false;
        if (jvm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_EDETACHED) {
            if (jvm->AttachCurrentThread(&e, nullptr) == JNI_OK) attached_here = true;
        }
        if (e) {
            if (jobject a = activity_.exchange(nullptr, std::memory_order_acq_rel))
                e->DeleteGlobalRef(a);
            if (jobject l = class_loader_.exchange(nullptr, std::memory_order_acq_rel))
                e->DeleteGlobalRef(l);
        }
        vm_.store(nullptr, std::memory_order_release);
        if (attached_here) jvm->DetachCurrentThread();
    }
    JavaVM* vm()           const noexcept { return vm_.load(std::memory_order_acquire); }
    jobject activity()     const noexcept { return activity_.load(std::memory_order_acquire); }
    jobject class_loader() const noexcept { return class_loader_.load(std::memory_order_acquire); }
    bool    valid()        const noexcept {
        return vm_.load(std::memory_order_acquire) &&
               activity_.load(std::memory_order_acquire);
    }

    // JNIEnv for the *calling* thread. Attaches if necessary.
    // Returns nullptr if the context is not initialized.
    JNIEnv* env() noexcept {
        JavaVM* jvm = vm_.load(std::memory_order_acquire);
        if (!jvm) return nullptr;
        JNIEnv* e = nullptr;
        jint rc = jvm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6);
        if (rc == JNI_EDETACHED) {
            if (jvm->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
        } else if (rc != JNI_OK) {
            return nullptr;
        }
        return e;
    }

    // Find a class via the cached class loader. Works from any native thread.
    // Returns a *local* reference — caller must DeleteLocalRef().
    jclass find_class(const char* dotted_name) noexcept {
        if (!dotted_name) return nullptr;
        JNIEnv* e = env();
        if (!e) return nullptr;
        jobject loader = class_loader_.load(std::memory_order_acquire);
        if (!loader) return nullptr;

        jclass loader_cls = e->GetObjectClass(loader);
        if (!loader_cls) return nullptr;
        jmethodID load = e->GetMethodID(loader_cls, "loadClass",
                                         "(Ljava/lang/String;)Ljava/lang/Class;");
        jclass result = nullptr;
        if (load) {
            jstring jn = e->NewStringUTF(dotted_name);
            if (jn) {
                result = static_cast<jclass>(e->CallObjectMethod(loader, load, jn));
                e->DeleteLocalRef(jn);
                if (e->ExceptionCheck()) { e->ExceptionClear(); result = nullptr; }
            }
        }
        e->DeleteLocalRef(loader_cls);
        return result;
    }

private:
    context() noexcept = default;
    ~context() { shutdown(); }
    context(const context&) = delete;
    context& operator=(const context&) = delete;

    // Defined in KernelNucleusT.hpp *after* all module headers are included,
    // so it can call each module's legacy set_*_context without circular deps.
    void configure_modules(JavaVM* vm, jobject activity) noexcept;

    std::atomic<JavaVM*> vm_{nullptr};
    std::atomic<jobject> activity_{nullptr};
    std::atomic<jobject> class_loader_{nullptr};
};

// =============================================================================
//  session — RAII wrapper that hides ALL Android/JNI details from the caller.
//  ----------------------------------------------------------------------------
//  The user's android_main() should look like:
//
//      void android_main(struct android_app* app) {
//          knst_android::session session(app);   // ← only line that knows `app`
//          test_cpu();
//          test_memory();
//          // ... every test, zero JNI, zero vm/clazz/env
//      }
//
//  Constructing the session:
//    • stores vm + activity in the shared context
//    • wires KnstDevicesHelper / KnstBluetoothHelper / KnstPrintDiscoveryHelper
//    • installs a default onAppCmd that updates the context on rotation,
//      chained to any previously installed handler (or one set later).
//
//  Destroying the session:
//    • releases the JNI global refs
//    • calls ANativeActivity_finish() so the OS tears down the app
//
//  Advanced access (only if you really need JNI):
//      knst_android::session::env()         -> JNIEnv*
//      knst_android::session::activity()    -> jobject
//      knst_android::session::find_class()  -> jclass
//
//  Requires  <android/native_app_glue.h>  to be in the include path
//  (any target that has `android_main` already does).
// =============================================================================
#if KNST_HAVE_ANDROID_APP_STRUCT

class session {
public:
    explicit session(struct android_app* app) noexcept {
        if (!app || !app->activity) return;
        app_ = app;

        // 1. Store vm + activity in the shared context (fans out to modules).
        if (!context::instance().init(app->activity->vm, app->activity->clazz)) {
            app_ = nullptr;
            return;
        }

        // 2. Chain any pre-existing onAppCmd (the user may have set one).
        user_cmd_ = app->onAppCmd;
        app->onAppCmd = &session::on_app_cmd_static_;
        g_current_ = this;

        // 3. Bind app-specific Java helpers (com.knst.print.*).
        //    These live in the APK's classes.dex, not in the library.
        bind_helpers_();
    }

    ~session() noexcept {
        if (!app_) return;
        g_current_ = nullptr;
        if (app_->onAppCmd == &session::on_app_cmd_static_)
            app_->onAppCmd = user_cmd_;
        context::instance().shutdown();               // release JNI globals
        ANativeActivity_finish(app_->activity);       // ask OS to close the app
        app_ = nullptr;
    }

    session(const session&) = delete;
    session& operator=(const session&) = delete;
    session(session&&) = delete;
    session& operator=(session&&) = delete;

    // ---- Advanced accessors (rarely needed) ----
    static jobject activity()  noexcept { return context::instance().activity(); }
    static JNIEnv* env()       noexcept { return context::instance().env(); }
    static jclass  find_class(const char* dotted_name) noexcept {
        return context::instance().find_class(dotted_name);
    }

private:
    struct android_app* app_ = nullptr;
    void (*user_cmd_)(struct android_app*, int32_t) = nullptr;

    static inline session* g_current_ = nullptr;
    static void on_app_cmd_static_(struct android_app* app, int32_t cmd) noexcept {
        // Keep the shared context in sync with the live activity on rotation.
        if (cmd == APP_CMD_INIT_WINDOW || cmd == APP_CMD_CONFIG_CHANGED) {
            if (app && app->activity && app->activity->clazz)
                context::instance().update_activity(app->activity->clazz);
        }
        // Chain to the user's own handler if any.
        if (g_current_ && g_current_->user_cmd_)
            g_current_->user_cmd_(app, cmd);
    }

    void bind_helpers_() noexcept {
        JNIEnv* env = context::instance().env();
        jobject activity = context::instance().activity();
        if (!env || !activity) return;

        auto bind = [&](const char* cls, const char* method, const char* sig, bool required) {
            jclass h = context::instance().find_class(cls);
            if (!h) {
                                ::fprintf(stderr,
                    "[KNST] %s helper: %s\n",
                    required ? "missing required" : "optional not linked",
                    cls);
                return;
            }
            jmethodID m = env->GetStaticMethodID(h, method, sig);
            if (m) env->CallStaticVoidMethod(h, m, activity);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(h);
        };
        bind("com.knst.print.KnstPrintDiscoveryHelper",
             "setContext", "(Landroid/content/Context;)V", /*required*/ true);
        bind("com.knst.print.KnstMlKitScannerHelper",
             "setActivity", "(Landroid/app/Activity;)V", /*required*/ false);
    }
};
#endif // KNST_HAVE_ANDROID_APP_STRUCT

} // namespace knst_android

#endif // KNST_USING_PLATFORM_ANDROID