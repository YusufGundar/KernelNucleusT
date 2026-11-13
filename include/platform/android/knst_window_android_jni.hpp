// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_window_android_jni.hpp
----------------------------

    JNI bridge between KnstActivity.java and the native library.

    Java InputConnection callbacks (commitText, setComposing,
    deleteSurrounding) get translated into unified knst_window_events,
    so user code never sees platform-specific behavior.
*/

#pragma once

#if defined(KNST_USING_PLATFORM_ANDROID)

#include <jni.h>
#include <android/log.h>
#include <atomic>
#include <mutex>


// Why both `inline` AND `used`:
//
//   JNI functions are only reachable from Java via reflection. The C++
//   compiler can't see that, so without help it drops them:
//
//     extern "C"             — no name mangling (JNI rule)
//     JNIEXPORT              — force default visibility
//     __attribute__((used))  — tell the linker: don't strip this
//     inline                 — allow multiple TUs without ODR clashes
//
//   inline alone → dropped as dead code → symbol missing at runtime
//   used alone   → multiple definition error
//   both         → safe and functional
#define KNST_JNI_FUNC extern "C" JNIEXPORT __attribute__((used)) inline

// Real definitions live in knst_window_core.hpp, which is included
// before this header.
class knst_window;
struct knst_window_event;


namespace knst_android_jni {

// ─── Event queue ───────────────────────────────────────────────────────────
// JNI callbacks fire on the UI thread; the render loop lives on the
// native_app_glue thread. Post into this thread-safe queue and drain it
// at the top of every frame — no races on knst_window::m_events.
//
// Ordering matters: s_queue and s_queue_mutex must exist before
// set_active_window, which clears the queue on teardown.

inline std::mutex s_queue_mutex;
inline std::vector<knst_window_event> s_queue;

// True when the Java InputConnection is installed. The AKeyEvent text
// path checks this so we don't emit the same character twice.
//
// atomic<bool>: UI thread writes, app thread reads. A plain bool would
// be a data race (UB).

inline std::atomic<bool> s_input_connection_active{false};

inline bool input_connection_active() noexcept {
    return s_input_connection_active.load(std::memory_order_relaxed);
}

// ─── Active window ─────────────────────────────────────────────────────────
// Set from knst_window::creation(), cleared on destroy.

inline knst_window* s_active_window = nullptr;
inline std::mutex s_active_mutex;

inline void set_active_window(knst_window* w) noexcept {
    std::lock_guard<std::mutex> lk(s_active_mutex);
    s_active_window = w;

    // Drop stale IME events when the window goes away. Otherwise a
    // second activity instance receives the previous session's
    // commitText / preedit.
    if (w == nullptr) {
        std::lock_guard<std::mutex> qlk(s_queue_mutex);
        s_queue.clear();

        // Reset the flag too — otherwise the getUnicodeChar path stays
        // disabled forever and the second launch has no text input.
        s_input_connection_active.store(false, std::memory_order_relaxed);
                                        
    }
}

inline knst_window* get_active_window() noexcept {
    std::lock_guard<std::mutex> lk(s_active_mutex);
    return s_active_window;
}

} // namespace knst_android_jni


// ─── JNI callbacks ─────────────────────────────────────────────────────────
// Defined inline here. This header is included exactly ONCE per .so
// (from knst_window_android_manager.hpp), so no ODR issue.

KNST_JNI_FUNC void JNICALL
Java_com_knst_window_KnstActivity_nativeInputConnectionAttached(
    JNIEnv*, jclass)
{
    knst_android_jni::s_input_connection_active.store(true, std::memory_order_relaxed);
        
}

// InputConnection detached (soft keyboard closed) → drop the flag.
//
// Without this, once the keyboard opens and closes, the flag stays
// true and getUnicodeChar is skipped for physical keyboards — meaning
// Bluetooth / USB keyboard text is lost.
KNST_JNI_FUNC void JNICALL
Java_com_knst_window_KnstActivity_nativeInputConnectionDetached(
    JNIEnv*, jclass)
{
    knst_android_jni::s_input_connection_active.store(false, std::memory_order_relaxed);
       
}

// GetStringUTFChars returns MODIFIED UTF-8 (CESU-8), not real UTF-8.
// An emoji (surrogate pair) arrives as two 3-byte sequences and breaks
// anything expecting proper 4-byte UTF-8.
//
// Correct approach: GetStringChars (UTF-16) and write straight into the
// UTF-16 string. No conversion, no loss.
KNST_JNI_FUNC void JNICALL
Java_com_knst_window_KnstActivity_nativeCommitText(
    JNIEnv* env, jclass, jstring js)
{
    if (!js) return;

    knst_window* w = knst_android_jni::get_active_window();
    if (!w) return;

    const jsize n = env->GetStringLength(js);
    if (n <= 0) return;

    const jchar* ch = env->GetStringChars(js, nullptr);
    if (!ch) return;

    knst_window_event ev;
    ev.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
    for (jsize i = 0; i < n; ++i) {
        ev.text_input += static_cast<char16_t>(ch[i]);
    }

    env->ReleaseStringChars(js, ch);

    {
        std::lock_guard<std::mutex> lk(knst_android_jni::s_queue_mutex);
        knst_android_jni::s_queue.push_back(std::move(ev));
    }
}

KNST_JNI_FUNC void JNICALL
Java_com_knst_window_KnstActivity_nativeSetComposing(
    JNIEnv* env, jclass, jstring js)
{
    if (!js) return;

    knst_window* w = knst_android_jni::get_active_window();
    if (!w) return;

    const jsize n = env->GetStringLength(js);

    // n <= 0 means "preedit cleared". The old code silently swallowed
    // this, so when the IME cancelled, the app kept showing stale
    // preedit text.
    if (n <= 0) {
        knst_window_event ev;
        ev.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
        ev.text_preedit = u"";
        std::lock_guard<std::mutex> lk(knst_android_jni::s_queue_mutex);
        knst_android_jni::s_queue.push_back(std::move(ev));
        return;
    }

    const jchar* ch = env->GetStringChars(js, nullptr);
    if (!ch) return;

    knst_window_event ev;
    ev.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
    for (jsize i = 0; i < n; ++i) {
        ev.text_preedit += static_cast<char16_t>(ch[i]);
    }

    env->ReleaseStringChars(js, ch);


    {
        std::lock_guard<std::mutex> lk(knst_android_jni::s_queue_mutex);
        knst_android_jni::s_queue.push_back(std::move(ev));
    }
}

KNST_JNI_FUNC void JNICALL
Java_com_knst_window_KnstActivity_nativeFinishComposing(
    JNIEnv*, jclass)
{
    knst_window* w = knst_android_jni::get_active_window();
    if (!w) return;

    knst_window_event ev;
    ev.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
    ev.text_preedit = u"";

    {
        std::lock_guard<std::mutex> lk(knst_android_jni::s_queue_mutex);
        knst_android_jni::s_queue.push_back(std::move(ev));
    }

}

KNST_JNI_FUNC void JNICALL
Java_com_knst_window_KnstActivity_nativeDeleteSurrounding(
    JNIEnv*, jclass, jint before, jint after)
{
    knst_window* w = knst_android_jni::get_active_window();
    if (!w) return;

    // IMEs sometimes send huge `before` values (hundreds, thousands).
    // Two events per unit — memory explodes. 64 is a reasonable upper
    // bound for a UI delete.
    int n = (before > 0) ? (int)before : 0;
    if (n > 64) n = 64;

    for (int i = 0; i < n; ++i) {
        knst_window_event ev1;
        ev1.begin(KNST_WINDOW_EVENT_KEYBOARD);
        ev1.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
        ev1.key_code = KNST_WINDOW_KEY_CODE_BACKSPACE;
        ev1.scancode = KNST_WINDOW_KEY_CODE_BACKSPACE;

        knst_window_event ev2;
        ev2.begin(KNST_WINDOW_EVENT_KEYBOARD);
        ev2.key_action = KNST_WINDOW_KEY_ACTION_RELEASE;
        ev2.key_code = KNST_WINDOW_KEY_CODE_BACKSPACE;
        ev2.scancode = KNST_WINDOW_KEY_CODE_BACKSPACE;

        std::lock_guard<std::mutex> lk(knst_android_jni::s_queue_mutex);
        knst_android_jni::s_queue.push_back(std::move(ev1));
        knst_android_jni::s_queue.push_back(std::move(ev2));
    }

    (void)after;
}

// Android 13+ gesture navigation doesn't send AKEYCODE_BACK — it calls
// onBackPressed() instead, which lands here.
//
// Returns jboolean: when there's no active window (native thread
// already gone) we return false so Java can call super.onBackPressed()
// and let the framework close the app. Otherwise Back is silently
// swallowed and the user can't close the app.
KNST_JNI_FUNC jboolean JNICALL
Java_com_knst_window_KnstActivity_nativeBackPressed(JNIEnv*, jclass)
{
    knst_window* w = knst_android_jni::get_active_window();
    if (!w) return JNI_FALSE;

    knst_window_event ev;
    ev.begin(KNST_WINDOW_EVENT_MOBILE_BACK);

    std::lock_guard<std::mutex> lk(knst_android_jni::s_queue_mutex);
    knst_android_jni::s_queue.push_back(std::move(ev));

    return JNI_TRUE;
}

KNST_JNI_FUNC void JNICALL
Java_com_knst_window_KnstActivity_nativeEditorAction(
    JNIEnv*, jclass, jint /*actionCode*/)
{
    knst_window* w = knst_android_jni::get_active_window();
    if (!w) return;

    knst_window_event ev1;
    ev1.begin(KNST_WINDOW_EVENT_KEYBOARD);
    ev1.key_action = KNST_WINDOW_KEY_ACTION_PRESS;
    ev1.key_code = KNST_WINDOW_KEY_CODE_ENTER;
    ev1.scancode = KNST_WINDOW_KEY_CODE_ENTER;

    knst_window_event ev2;
    ev2.begin(KNST_WINDOW_EVENT_KEYBOARD);
    ev2.key_action = KNST_WINDOW_KEY_ACTION_RELEASE;
    ev2.key_code = KNST_WINDOW_KEY_CODE_ENTER;
    ev2.scancode = KNST_WINDOW_KEY_CODE_ENTER;

    std::lock_guard<std::mutex> lk(knst_android_jni::s_queue_mutex);
    knst_android_jni::s_queue.push_back(std::move(ev1));
    knst_android_jni::s_queue.push_back(std::move(ev2));
}


// ─── Drain ─────────────────────────────────────────────────────────────────
// Called by the render loop from the app thread.
namespace knst_android_jni {

inline void drain(knst_window* w) {
    if (!w) return;

    std::vector<knst_window_event> local;
    {
        std::lock_guard<std::mutex> lk(s_queue_mutex);
        if (s_queue.empty()) return;
        local.swap(s_queue);
    }
    for (auto& ev : local) {
        w->dispatch_event(ev);
    }
}

} // namespace knst_android_jni


#else   // PC / Windows / Linux — no-op shims
        // No-op versions so user code can call these without #if guards.

class knst_window;

namespace knst_android_jni {

// Drain the JNI IME queue — no queue on PC, so this does nothing.
inline void drain(knst_window*) noexcept {}

// True only when the InputConnection is installed. Always false on PC.
inline bool input_connection_active() noexcept { return false; }

// Close the Android Activity — no-op on PC.
inline void finish_activity() noexcept {}

} // namespace knst_android_jni

#endif // KNST_USING_PLATFORM_ANDROID