/*
----------------------------
KernelNucleusT.hpp
----------------------------

    It is the file where all the library's core and bundled structures are included together

*/




#pragma once

#define KNST_NETWORK_ENABLE_JSON

#if defined(_WIN32) || defined(_WIN64)
    #ifndef KNST_DISABLE_CONSOLE_UTF8
        extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int);
        extern "C" __declspec(dllimport) int __stdcall SetConsoleCP(unsigned int);
        namespace knst_detail {
            struct knst_console_utf8_initializer {
                knst_console_utf8_initializer() noexcept {
                    ::SetConsoleOutputCP(65001);  // CP_UTF8
                    ::SetConsoleCP(65001);
                }
            };
            inline knst_console_utf8_initializer g_console_utf8_init;
        }
    #endif
#endif

#if defined(_WIN32) || defined(KNST_USING_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

// structural
#if defined(KNST_USING_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include "knst_android_context.hpp"
#endif
#include "knst_global_functions.hpp"
#include "knst_c16string.hpp"
#include "knst_byte_string.hpp"
#include "knst_vector.hpp"
#include "knst_json.hpp"
#include "knst_file.hpp"



#include "knst_function.hpp"
#include "knst_thread.hpp"
#include "knst_thread_pool.hpp"

#include "knst_process.hpp"
#include "knst_devices.hpp"

#include "knst_audio.hpp"
#include "knst_microphone.hpp"

#include "knst_bluetooth.hpp"



#include "knst_sensors.hpp"
#include "knst_storage.hpp"
#include "knst_usb.hpp"

#include "knst_image_loader.hpp"
#include "knst_video.hpp"
#include "knst_camera.hpp"

#include "knst_converter.hpp"
// -----------------------------------------------------------------------------
//  Network stack
//
//  knst_network.hpp    : always included — POSIX/Winsock only when TLS is off,
//                        OpenSSL headers guarded by KNST_NETWORK_ENABLE_TLS
//  knst_websocket.hpp  : always included — depends only on knst_network.hpp
//  knst_network_h2.hpp : only when TLS is enabled (needs nghttp2 + OpenSSL)
//  knst_network_h3.hpp : only when TLS + H3 are enabled (needs ngtcp2 + nghttp3
//                        + quictls). The header itself #errors if TLS is off.
// -----------------------------------------------------------------------------
#include "knst_network.hpp"
#include "knst_websocket.hpp"

#if defined(KNST_NETWORK_ENABLE_H2)
    #include "knst_network_h2.hpp"
#endif

#if defined(KNST_NETWORK_ENABLE_H3)
    #include "knst_network_h3.hpp"
#endif
#include "knst_printer.hpp"


// _end structural




// knst_window

#include "knst_window.hpp"

// _end knst_window

#if defined(KNST_WINDOW_USE_OPENGL)

    #include "platform/knst_window/opengl_content/glad_3_3/include/glad/glad.h" // default 3.3 opengl core

    #include "platform/knst_window/opengl_content/knst_window_opengl_content.hpp" // for opengl content
    
#endif

#if defined(KNST_WINDOW_USE_VULKAN)


    #include "platform/knst_window/vulkan_content/knst_window_vulkan_content.hpp" // for opengl content
    
#endif


#if !defined(KNST_KEEP_X11_NONE_MACRO) 
    #ifdef None // for knst_file none parameter
        #undef None 
    #endif
#endif


// =============================================================================
//  knst_android::context::configure_modules implementation
//  ----------------------------------------------------------------------------
//  Defined HERE (not in knst_android_context.hpp) so it can call every
//  module's legacy set_*_context() without circular dependencies.
//
//  When the user calls knst_android::context::instance().init(vm, clazz),
//  this fans out to:
//    • knst_devices::detail_android::set_java_vm      (devices, bluetooth, file paths)
//    • knst_net_detail::android_jni::set_context      (system proxy)
//    • knst_prn::detail::set_jni_context              (print / scan / mDNS)
//
//  Backward compatibility: users can still call the three set_* functions
//  directly — nothing here changes their behavior.
// =============================================================================
#if defined(KNST_USING_PLATFORM_ANDROID)
namespace knst_android {
inline void context::configure_modules(JavaVM* vm, jobject activity) noexcept {
    if (!vm || !activity) return;

    // knst_devices — also triggers KnstDevicesHelper/KnstBluetoothHelper
    // Java context setup and knst_file's Android paths.
    knst_devices::detail_android::set_java_vm(vm, activity);

    // knst_network — system proxy settings via Java System.getProperty.
    knst_net_detail::android_jni::set_context(vm, activity);

    // knst_printer — PrintManager / NsdManager / ML Kit helper context.
    knst_prn::detail::set_jni_context(vm, activity);

    // knst_file — extract temp/home/external dirs from the Activity so that
    // get_temp_dir()/get_home_dir() work on Android. Called last so the
    // JNI context is already initialized by set_java_vm above.
    {
        JNIEnv* env = nullptr;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) env = nullptr;
        }
        if (env && activity) {
            auto jstr = [env](jstring js) -> knst_c16string {
                if (!js) return knst_c16string();
                const char* c = env->GetStringUTFChars(js, nullptr);
                if (!c) { env->ExceptionClear(); return knst_c16string(); }
                knst_c16string r(c, static_cast<uint32_t>(env->GetStringUTFLength(js)));
                env->ReleaseStringUTFChars(js, c);
                return r;
            };
            auto file_abs = [env, activity, &jstr](const char* method,
                                                    const char* sig,
                                                    bool pass_null) -> knst_c16string {
                jclass ac = env->GetObjectClass(activity);
                if (!ac) return knst_c16string();
                jmethodID fn = env->GetMethodID(ac, method, sig);
                env->DeleteLocalRef(ac);
                if (!fn) { env->ExceptionClear(); return knst_c16string(); }
                jobject f = pass_null
                    ? env->CallObjectMethod(activity, fn, static_cast<jstring>(nullptr))
                    : env->CallObjectMethod(activity, fn);
                if (env->ExceptionCheck() || !f) {
                    env->ExceptionClear();
                    return knst_c16string();
                }
                knst_c16string out;
                jclass fc = env->FindClass("java/io/File");
                if (fc) {
                    jmethodID ap = env->GetMethodID(fc, "getAbsolutePath",
                                                    "()Ljava/lang/String;");
                    env->DeleteLocalRef(fc);
                    if (ap) {
                        jstring js = static_cast<jstring>(env->CallObjectMethod(f, ap));
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        else if (js) { out = jstr(js); env->DeleteLocalRef(js); }
                    }
                }
                env->DeleteLocalRef(f);
                return out;
            };
            auto files_parent = [env, activity, &jstr]() -> knst_c16string {
                jclass ac = env->GetObjectClass(activity);
                if (!ac) return knst_c16string();
                jmethodID fn = env->GetMethodID(ac, "getFilesDir", "()Ljava/io/File;");
                env->DeleteLocalRef(ac);
                if (!fn) { env->ExceptionClear(); return knst_c16string(); }
                jobject f = env->CallObjectMethod(activity, fn);
                if (env->ExceptionCheck() || !f) {
                    env->ExceptionClear();
                    return knst_c16string();
                }
                knst_c16string out;
                jclass fc = env->FindClass("java/io/File");
                if (fc) {
                    jmethodID pp = env->GetMethodID(fc, "getParent",
                                                    "()Ljava/lang/String;");
                    env->DeleteLocalRef(fc);
                    if (pp) {
                        jstring js = static_cast<jstring>(env->CallObjectMethod(f, pp));
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        else if (js) { out = jstr(js); env->DeleteLocalRef(js); }
                    }
                }
                env->DeleteLocalRef(f);
                return out;
            };

            knst_file::set_android_dirs(
                file_abs("getCacheDir", "()Ljava/io/File;", false),
                files_parent(),
                file_abs("getExternalFilesDir",
                         "(Ljava/lang/String;)Ljava/io/File;", true));
        }
    }
}
} // namespace knst_android
#endif // KNST_USING_PLATFORM_ANDROID