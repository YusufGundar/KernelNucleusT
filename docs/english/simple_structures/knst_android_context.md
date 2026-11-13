# knst_android_context — Android JNI Context Management

Hey! 👋 `knst_android_context.hpp` is KernelNucleusT's **Android JNI context** module. All in one header: **process-wide JavaVM + Activity + ClassLoader** storage, **automatic rotation handling**, **fanout to modules**, and an **RAII `session`** that frees you from every JNI detail.

**In short:** **Android-only**, header-only, exception-free, no RTTI. Only exists under `#if defined(KNST_USING_PLATFORM_ANDROID)` — on other platforms the file is empty (no-op). In `android_main()` a **single line** `knst_android::session session(app);` is enough — JavaVM, Activity, ClassLoader, and module contexts are all handled for you. **Thread-safe reads** (`vm()`, `activity()`, `env()`). `env()` **automatically attaches the calling thread** if needed (detaching is not your responsibility — that's how Android works).

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

### Platform matrix

| Feature | Android | Other Platforms |
|---|---|---|
| **Header contents** | ✅ | ❌ Completely empty (guarded) |
| **JavaVM storage** | ✅ | — |
| **Activity storage (GlobalRef)** | ✅ | — |
| **ClassLoader cache** | ✅ | — |
| **`init()` / `shutdown()`** | ✅ | — |
| **Rotation handling (`update_activity`)** | ✅ | — |
| **Thread-safe reads** | ✅ | — |
| **Automatic AttachCurrentThread** | ✅ | — |
| **`find_class()` from native thread** | ✅ | — |
| **`session` RAII wrapper** | ✅ (if native_app_glue present) | — |
| **Automatic rotation (onAppCmd chain)** | ✅ | — |
| **Automatic Java helper bind** | ✅ | — |
| **`ANativeActivity_finish()`** | ✅ | — |
| **Legacy `set_*_context` compatibility** | ✅ | — |

**Note:** The header **only compiles on Android builds**. Under `#if !defined(KNST_USING_PLATFORM_ANDROID)` it's completely empty — safe to include on other platforms.

### Dependencies

| Dependency | For | Mandatory? |
|---|---|---|
| **`<jni.h>`** | JNI types | ✅ |
| **`<atomic>`** | Thread-safe state | ✅ |
| **`<cstdio>`** | `fprintf` (helper warning) | ✅ |
| **`<android/native_app_glue.h>`** | `session` RAII | ⚠️ Only for `session` |
| **`KernelNucleusT.hpp`** | `configure_modules()` definition | ✅ (afterwards) |

**If you don't use `session`**, `native_app_glue` is not required — `context` alone is enough. The `KNST_HAVE_ANDROID_APP_STRUCT` macro guard handles this automatically (`__has_include` tries 3 different paths).

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **One init, all modules** | `set_java_vm` not called separately in 3 modules |
| **Singleton context** | `context::instance()` accessible from anywhere |
| **Global ref management** | Activity + ClassLoader `NewGlobalRef`/`DeleteGlobalRef` automatic |
| **`IsSameObject` idempotency** | Re-`init()` with same activity = no-op |
| **ClassLoader cache** | App classes visible from native threads |
| **Rotation handling** | `APP_CMD_INIT_WINDOW` / `APP_CMD_CONFIG_CHANGED` automatic |
| **`session` RAII** | Construct = init, destruct = shutdown + finish |
| **onAppCmd chain** | User's handler preserved, ours added on top |
| **Java helper auto-bind** | `KnstPrintDiscoveryHelper` + `KnstMlKitScannerHelper` |
| **Missing helper warning** | `stderr`: "missing required / optional not linked" |
| **Attach/Detach per call** | `env()` attaches calling thread |
| **`shutdown` attach guard** | Only detaches the thread **it** attached |
| **`find_class` from any thread** | `loadClass` via ClassLoader |
| **Legacy compatibility** | Old `set_*_context` calls still work |
| **`__has_include` guard** | 3 different `native_app_glue` paths automatic |
| **Exception clear** | JNI exceptions silently cleared |
| **Null-safety** | `nullptr` → returns `false` / `nullptr` |

---

## 🚀 Quick Start

### Simplest — `session` (recommended)

```cpp
#include "KernelNucleusT.hpp"

void android_main(struct android_app* app) {
    knst_android::session session(app);   // ← single line
    if (!knst_android::session::activity()) {
        KNST_LOG_ERROR("session init failed");
        return;
    }

    // Everything is ready — no vm/clazz/env
    test_cpu();
    test_memory();
    // ...
}
```

**What happens?**
1. `context::instance().init(vm, activity)` is called
2. Modules receive the fanout (`knst_devices`, `knst_net`, `knst_prn`)
3. `onAppCmd` is **chained** (user's handler preserved)
4. Java helpers are bound
5. Destructor: `shutdown()` + `ANativeActivity_finish()`

### Manual — `context` (rare)

```cpp
// Wherever you know the Activity
knst_android::context::instance().init(vm, activity);

// ... use ...

// Cleanup
knst_android::context::instance().shutdown();
```

### Advanced accessors

```cpp
// Anywhere inside session
JNIEnv* env = knst_android::session::env();
jobject activity = knst_android::session::activity();

// Class lookup (from any thread)
jclass cls = knst_android::session::find_class("com.knst.print.MyHelper");
if (cls) {
    // ... use ...
    env->DeleteLocalRef(cls);   // ← caller's responsibility
}
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Context access** | `context::instance()` |
| **Lifecycle** | `init()`, `update_activity()`, `shutdown()` |
| **State** | `valid()` |
| **Accessors** | `vm()`, `activity()`, `class_loader()` |
| **JNI helpers** | `env()`, `find_class()` |
| **Session** | `session(app)`, `session::activity()`, `session::env()`, `session::find_class()` |

---

## 🎯 1) `context` — Singleton Context

```cpp
namespace knst_android {

class context {
public:
    static context& instance() noexcept;
    bool init(JavaVM* vm, jobject activity) noexcept;
    bool update_activity(jobject new_activity) noexcept;
    void shutdown() noexcept;

    JavaVM* vm()           const noexcept;
    jobject activity()     const noexcept;
    jobject class_loader() const noexcept;
    bool    valid()        const noexcept;

    JNIEnv* env() noexcept;
    jclass  find_class(const char* dotted_name) noexcept;
};

}
```

### 1.1) `instance()` — Singleton

```cpp
static context& instance() noexcept;
```

**Thread-safe** (C++11 static init). Same instance reached from anywhere.

### 1.2) `init(vm, activity)` — Set up the context

```cpp
bool init(JavaVM* vm, jobject activity) noexcept;
```

**What it does?**
1. Attaches the calling thread (if needed)
2. **`IsSameObject`** check on `activity` — if same, **no-op**, returns `true`
3. `activity` → `NewGlobalRef` (survives across threads)
4. **ClassLoader** cache: `activity.getClassLoader()` → `NewGlobalRef`
5. Free old global refs (`DeleteGlobalRef`)
6. Update `vm_` atomically
7. Call **`configure_modules(vm, activity)`** — defined in KernelNucleusT.hpp, fans out to legacy `set_*_context`

**Returns:** `true` = context ready, `false` = fail (vm/activity `nullptr` or `NewGlobalRef` fail).

**Idempotent:** Call again with same Activity object → no-op. Even if `jobject` pointers differ, `IsSameObject` catches when they point to the same Java object.

### 1.3) `update_activity(new_activity)` — Rotation

```cpp
bool update_activity(jobject new_activity) noexcept;
```

**What it does?** Calls `init(vm, new_activity)` with the stored `vm`.

**When?**
- Rotation (`APP_CMD_CONFIG_CHANGED`)
- Theme change (Activity recreate)
- `APP_CMD_INIT_WINDOW`

**Note:** ClassLoader may differ on the new Activity — `init()` re-caches automatically.

### 1.4) `shutdown()` — Cleanup

```cpp
void shutdown() noexcept;
```

**What it does?**
1. `DeleteGlobalRef` on `activity_` and `class_loader_` global refs
2. Sets `vm_` atomic to `nullptr`
3. `DetachCurrentThread` **only if this call attached** the thread

**Idempotent:** Multiple calls safe.

**Critical:** Attach guard — if the calling thread was **already attached**, it stays attached after `shutdown()`. This is **correct behavior** — the library shouldn't hijack someone else's attach.

### 1.5) Accessors

```cpp
JavaVM* vm()           const noexcept;   // atomic load
jobject activity()     const noexcept;   // atomic load
jobject class_loader() const noexcept;   // atomic load
bool    valid()        const noexcept;   // vm && activity
```

**Thread-safe.** Acquire/release semantics.

**Caution:** The `jobject` returned by `activity()` is a **global ref** — don't delete it. `context` owns it.

### 1.6) `env()` — Calling thread's JNIEnv

```cpp
JNIEnv* env() noexcept;
```

**What it does?**
1. If no `vm`, returns `nullptr`
2. `GetEnv`:
   - `JNI_OK` → current env
   - `JNI_EDETACHED` → `AttachCurrentThread` to attach
   - Otherwise → `nullptr`

**Returns:** Calling thread's `JNIEnv*` — attach performed if necessary.

**Note:** Attach is **permanent**. The JVM auto-detaches when the thread ends. Don't detach manually unless you specifically want to.

### 1.7) `find_class(dotted_name)` — Class lookup

```cpp
jclass find_class(const char* dotted_name) noexcept;
```

**What it does?**
1. Gets `env()`
2. Gets `class_loader_` (returns `nullptr` if absent)
3. Calls `loadClass(String)` via the ClassLoader
4. **Exception clear** — ClassNotFound silently returns `nullptr`

**Returns:** **Local reference** — caller must `DeleteLocalRef`.

**Why via ClassLoader?** On Android, the system classloader **doesn't see app classes**. `FindClass` from a native thread only sees system classes. ClassLoader cache enables app class access.

---

## 🎁 2) `session` — RAII Wrapper

```cpp
#if KNST_HAVE_ANDROID_APP_STRUCT

class session {
public:
    explicit session(struct android_app* app) noexcept;
    ~session() noexcept;

    // Non-copyable, non-movable
    session(const session&) = delete;
    session& operator=(const session&) = delete;
    session(session&&) = delete;
    session& operator=(session&&) = delete;

    static jobject  activity()  noexcept;
    static JNIEnv*  env()       noexcept;
    static jclass   find_class(const char* dotted_name) noexcept;
};

#endif
```

### 2.1) Constructor

```cpp
explicit session(struct android_app* app) noexcept;
```

**What it does?**
1. If `app` or `app->activity` is null → stays empty (later `session::activity()` returns null)
2. `context::instance().init(vm, activity)` — on fail, stays empty
3. **`onAppCmd` chain**: previous handler saved, new handler installed
4. **Java helper bind**:
   - `com.knst.print.KnstPrintDiscoveryHelper` (**required**)
   - `com.knst.print.KnstMlKitScannerHelper` (optional)
5. Prints warning to `stderr` for each failed bind (no crash)

**`onAppCmd` chain:**

```cpp
user_cmd_ = app->onAppCmd;          // save old handler
app->onAppCmd = &on_app_cmd_static_; // install ours
```

Our handler:
- `APP_CMD_INIT_WINDOW` or `APP_CMD_CONFIG_CHANGED` → calls `update_activity()`
- Then **chains** to `user_cmd_` if present

**Result:** User's own `onAppCmd` handler is **preserved**, ours is added on top. Rotation is handled automatically.

### 2.2) Destructor

```cpp
~session() noexcept;
```

**What it does?**
1. If `onAppCmd` is our handler → restore `user_cmd_` (unchain)
2. `context::instance().shutdown()` — release global refs
3. **`ANativeActivity_finish(app_->activity)`** — tells OS "close the app"

**Critical:** If `ANativeActivity_finish` isn't called, the app stays in the background, consuming RAM. `session` destructor handles this.

### 2.3) Static accessors

```cpp
static jobject  activity()  noexcept;                         // context::instance().activity()
static JNIEnv*  env()       noexcept;                         // context::instance().env()
static jclass   find_class(const char* dotted_name) noexcept; // context::instance().find_class()
```

**Convenience** — same thing without writing `context::instance()`. Users don't need to know about `context`.

**`find_class` returns local ref** — caller must `DeleteLocalRef`.

### 2.4) When `session`, when `context`?

| Situation | Use |
|---|---|
| `android_main(android_app*)` present | **`session`** (recommended) |
| No `android_main`, init elsewhere | **`context`** (manual) |
| `native_app_glue` not linked | **`context`** (mandatory) |
| Both available | **`session`** (less code) |

If you don't use `session`, `KNST_HAVE_ANDROID_APP_STRUCT=0` is set, `session` class **isn't compiled** (guarded). `context` works in every case.

---

## 📱 3) Usage Examples

### Example 1: Classic `android_main` (from test suite)

```cpp
void android_main(struct android_app* app) {
    KNST_LOG_INFO("Test suite starting");

    knst_android::session session(app);
    if (!knst_android::session::activity()) {
        KNST_LOG_ERROR("FATAL: session init failed");
        return;
    }

    KNST_LOG_INFO("Bootstrap OK");

    // Every test — no JNI/vm/clazz/env
    test_cpu();
    test_memory();
    test_storage();
    // ...
}
```

**Line count:** 1 line (`session` construct). Rest is normal test code.

### Example 2: Manual `context` (without native_app_glue)

```cpp
// Any init function
extern "C" JNIEXPORT void JNICALL
Java_com_example_MainActivity_onCreate(JNIEnv* env, jobject, jobject activity) {
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);

    knst_android::context::instance().init(vm, activity);

    // ... use ...
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_MainActivity_onDestroy(JNIEnv*, jobject) {
    knst_android::context::instance().shutdown();
}
```

### Example 3: Rotation handling (session automatic)

```cpp
// Inside session constructor:
user_cmd_ = app->onAppCmd;
app->onAppCmd = &session::on_app_cmd_static_;

// Static handler:
static void on_app_cmd_static_(struct android_app* app, int32_t cmd) noexcept {
    if (cmd == APP_CMD_INIT_WINDOW || cmd == APP_CMD_CONFIG_CHANGED) {
        if (app && app->activity && app->activity->clazz)
            context::instance().update_activity(app->activity->clazz);
    }
    if (g_current_ && g_current_->user_cmd_)
        g_current_->user_cmd_(app, cmd);   // user's handler
}
```

**Result:** Even if the user sets `onAppCmd`, **session handles rotation**, user only does their own work.

### Example 4: Class lookup from native thread

```cpp
void my_worker_thread() {
    // context::instance().env() → attach thread
    // find_class → app class via ClassLoader
    jclass helper = knst_android::session::find_class("com.knst.print.KnstPrintDiscoveryHelper");
    if (!helper) {
        KNST_LOG_ERROR("Helper class not found");
        return;
    }

    JNIEnv* env = knst_android::session::env();
    jmethodID m = env->GetStaticMethodID(helper, "someMethod", "()V");
    if (m) {
        env->CallStaticVoidMethod(helper, m);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    env->DeleteLocalRef(helper);   // ← caller's responsibility
}
```

**Why not `FindClass`?** `FindClass` uses the system classloader — doesn't see app classes on Android. `find_class` goes through the cached ClassLoader.

### Example 5: Shutdown sequence

```cpp
// User closing the app
~session() {
    // 1. Unchain onAppCmd
    if (app_->onAppCmd == &session::on_app_cmd_static_)
        app_->onAppCmd = user_cmd_;

    // 2. Release global refs
    context::instance().shutdown();

    // 3. Tell OS "close"
    ANativeActivity_finish(app_->activity);
}
```

**If `ANativeActivity_finish` isn't called:** the app stays in background; even when the test suite finishes, the process keeps running.

---

## ⚠️ 4) Common Gotchas

### 1) Header only compiles on Android

```cpp
#if defined(KNST_USING_PLATFORM_ANDROID)
#include "knst_android_context.hpp"
#endif
```

Under `#if !defined(KNST_USING_PLATFORM_ANDROID)` the file is **completely empty** — safe to include but no `knst_android::` namespace.

### 2) `session` requires `native_app_glue`

```cpp
#if KNST_HAVE_ANDROID_APP_STRUCT
// session defined
#endif
```

If `native_app_glue.h` can't be included → `session` class **isn't compiled**. `context` still works (only needs `vm + activity`).

**Check:** `__has_include(<android/native_app_glue.h>)` → 3 different paths tried.

### 3) `activity` is a global ref — don't delete it

```cpp
jobject a = knst_android::session::activity();
// ... use ...
// ❌ env->DeleteLocalRef(a);   // WRONG — this is a GLOBAL ref
```

`activity()` returns a **global ref** — owned by `context`. **Don't delete.** Only `find_class()` returns a **local ref**.

### 4) `find_class` is a local ref — you must delete it

```cpp
jclass cls = knst_android::session::find_class("com.example.Foo");
// ... use ...
env->DeleteLocalRef(cls);   // ✅ MANDATORY
```

**Local ref limit:** Android has a 512 local ref limit. If you call `find_class` in a loop without deleting → **crash**.

### 5) `init` is idempotent but not cheap

```cpp
context::instance().init(vm, activity);   // first call — expensive
context::instance().init(vm, activity);   // second call — IsSameObject hit, cheap
```

**Same activity** → `IsSameObject` returns fast. **Different activity** → new global ref + ClassLoader re-cache.

### 6) `IsSameObject` — not pointer equality

```cpp
// ❌ Wrong:
if (cur == activity) return true;   // pointer comparison

// ✅ Correct:
if (cur && e->IsSameObject(cur, activity)) return true;
```

Different `jobject` **handles** may point to the same Java object. `IsSameObject` is the only correct way.

### 7) `shutdown` attach guard

```cpp
void shutdown() noexcept {
    // ...
    bool attached_here = false;
    if (GetEnv == JNI_EDETACHED) {
        AttachCurrentThread(...);
        attached_here = true;
    }
    // ... work ...
    if (attached_here) DetachCurrentThread();   // ← only detach our own attach
}
```

**Why?** If the calling thread was **already attached**, it must stay attached after `shutdown()`. Otherwise the library hijacks someone else's attach → that thread crashes.

### 8) `session` is non-copyable, non-movable

```cpp
knst_android::session s1(app);
knst_android::session s2 = std::move(s1);   // ❌ compile error
```

**Why?** The `onAppCmd` pointer is chained to `this`. Moving invalidates the pointer → crash.

**Fix:** `std::unique_ptr<session>` or stack scope.

### 9) `configure_modules` — defined in KernelNucleusT.hpp

```cpp
// knst_android_context.hpp:
void configure_modules(JavaVM* vm, jobject activity) noexcept;   // declaration

// KernelNucleusT.hpp:
inline void knst_android::context::configure_modules(JavaVM* vm, jobject activity) noexcept {
    knst_devices::detail_android::set_java_vm(vm, activity);
    knst_net_detail::android_jni::set_context(vm, activity);
    knst_prn::detail::set_jni_context(vm, activity);
}
```

**Not header-only** — the `configure_modules` body is defined in `KernelNucleusT.hpp` **after all module headers are included**. This breaks **circular dependencies**.

**Result:** Including `knst_android_context.hpp` **alone** leaves `configure_modules` unresolved. Always use it via `KernelNucleusT.hpp`.

### 10) Java helper missing — warning, no crash

```cpp
bind("com.knst.print.KnstPrintDiscoveryHelper", ..., /*required*/ true);
bind("com.knst.print.KnstMlKitScannerHelper", ..., /*required*/ false);
```

If a helper class is missing, writes to `stderr`:
```
[KNST] missing required helper: com.knst.print.KnstPrintDiscoveryHelper
[KNST] optional not linked helper: com.knst.print.KnstMlKitScannerHelper
```

**No crash** — the related feature returns `NotSupported`. Test suite skips gracefully.

### 11) `env()` attach is permanent

```cpp
JNIEnv* env = knst_android::session::env();
// ... thread exits here ...
// Thread still attached — JVM auto-detaches
```

**Attach lifetime = thread lifetime.** A native thread doesn't attach **every** call to `env()` (attaches on first call, then cached).

**No over-attach worry** — the Android JVM thread pool is limited but practically fine.

### 12) Rotation — `update_activity` is automatic

```cpp
// Inside session constructor:
if (cmd == APP_CMD_INIT_WINDOW || cmd == APP_CMD_CONFIG_CHANGED) {
    context::instance().update_activity(app->activity->clazz);
}
```

User does nothing. When **Activity recreates**, `context` is updated automatically, ClassLoader refreshed.

### 13) `ANativeActivity_finish` — why needed?

```cpp
~session() {
    // ...
    ANativeActivity_finish(app_->activity);   // ← signal to OS "close"
}
```

**If not called:** `android_main` returns but **the process keeps running**. RAM, CPU, battery consumption continue. Battery drain + ANR risk.

**If called:** OS begins the `onDestroy` chain, process exits cleanly.

### 14) `onAppCmd` chain — user handler preserved

```cpp
// session constructor:
user_cmd_ = app->onAppCmd;
app->onAppCmd = &session::on_app_cmd_static_;

// session destructor:
if (app_->onAppCmd == &session::on_app_cmd_static_)
    app_->onAppCmd = user_cmd_;
```

**If the user set `onAppCmd`** — `session` adds on top, restores on exit. **Ordered**: session handler → user handler.

**But:** If the user sets `app->onAppCmd` **after** the `session` construct, **session's is overwritten**. In this case rotation handling doesn't work. **Correct order:** session first, user handler after (or use session's statics).

### 15) `valid()` — vm + activity check

```cpp
if (!knst_android::context::instance().valid()) {
    // init failed
}
```

**After session construct**, if `session::activity()` returns null → init failed. Test suite first check.

### 16) `session::env()` — wrapper over `context::env()`

```cpp
static JNIEnv* env() noexcept { return context::instance().env(); }
```

**No extra cost** — inline. `context` singleton + `env()` call.

### 17) JNI exception clear — silent cleanup

```cpp
if (e->ExceptionCheck()) e->ExceptionClear();
```

ClassNotFound and similar errors create JNI exceptions. **If not cleared**, subsequent JNI calls fail. `init()` and `find_class()` call `ExceptionClear` after every exception.

**Result:** Missing class returns `nullptr`, library handles gracefully.

### 18) `NewStringUTF` — UTF-8 limit

```cpp
jstring jn = e->NewStringUTF(dotted_name);   // dotted_name must be UTF-8
```

`NewStringUTF` expects **modified UTF-8**. `dotted_name` must be ASCII (class names are ASCII anyway).

**Non-ASCII class name** (very rare) → `NewStringUTF` produces a broken string. On Android, class names are ASCII by Java standard.

### 19) `find_class` — 512 local ref limit

```cpp
// ❌ Wrong — every call leaks 1 local ref
for (int i = 0; i < 1000; ++i) {
    jclass c = knst_android::session::find_class("com.example.Foo");
    // ...
}

// ✅ Correct
for (int i = 0; i < 1000; ++i) {
    jclass c = knst_android::session::find_class("com.example.Foo");
    // ... use ...
    knst_android::session::env()->DeleteLocalRef(c);
}
```

When **512 local refs** fill up, Android **crashes**. If `find_class` is in a loop, deletion is mandatory.

### 20) Thread-safety guarantee

```cpp
std::atomic<JavaVM*> vm_;
std::atomic<jobject> activity_;
std::atomic<jobject> class_loader_;
```

**`vm()`, `activity()`, `class_loader()`, `valid()` are thread-safe.** Acquire/release semantics.

**`init`/`shutdown`** are not thread-safe (theoretically). **Practically:** init once (main thread), shutdown once (main thread). Caller's responsibility.

### 21) `class_loader` may change on rotation

```cpp
bool update_activity(jobject new_activity) noexcept {
    return init(vm_.load(), new_activity);
}
```

Inside `init`, **new ClassLoader is cached** — old ClassLoader **invalidated**. Different Activity may have a different classloader (plugins, etc.), handled automatically.

### 22) `nullptr` safety

```cpp
if (!vm || !activity) return false;      // init
if (!new_activity) return false;         // update_activity
if (!dotted_name) return nullptr;        // find_class
if (!app || !app->activity) return;      // session ctor
```

Every public API is defensive against `nullptr` inputs.

### 23) `session` destructor — no `env()` inside `shutdown()`

```cpp
~session() {
    context::instance().shutdown();   // ← no env(), only ref cleanup
    ANativeActivity_finish(app_->activity);
}
```

**Why no `env()`?** `shutdown()` already calls `AttachCurrentThread` internally. Extra `env()` is redundant.

### 24) Destructor double shutdown

```cpp
~session() {
    if (!app_) return;   // ← guard
    // ...
    context::instance().shutdown();
    // ...
    app_ = nullptr;
}
```

**Guard:** If the constructor failed (`app_ = nullptr`), the destructor **does nothing**. `shutdown()` on an empty context is no-op.

### 25) `session` non-movable — why?

```cpp
session(session&&) = delete;
```

**Why:** `g_current_` static pointer references `this`. Moving invalidates the pointer → `on_app_cmd_static_` crashes.

**Fix:** Stack scope (RAII already enforces this).

### 26) `configure_modules` — legacy compatibility

```cpp
// KernelNucleusT.hpp:
void context::configure_modules(JavaVM* vm, jobject activity) noexcept {
    knst_devices::detail_android::set_java_vm(vm, activity);       // legacy
    knst_net_detail::android_jni::set_context(vm, activity);       // legacy
    knst_prn::detail::set_jni_context(vm, activity);                // legacy
}
```

**Old code:** can still call `set_java_vm`. **New code:** only `context::instance().init(...)`. **Library:** fanout automatic.

**Result:** Migration is optional, but using `context` is **cleaner** (one init, one shutdown).

### 27) `session` — typical use in Android test suite

```cpp
void android_main(struct android_app* app) {
    KNST_LOG_INFO("=== KernelNucleusT Test Suite ===");

    knst_android::session session(app);   // ← bootstrap
    if (!knst_android::session::activity()) {
        KNST_LOG_ERROR("FATAL: session init failed");
        return;
    }

#if defined(KNST_NETWORK_ENABLE_TLS)
    knst_tls_set_skip_verify(true);        // LAN printer self-signed
#endif

    KNST_LOG_INFO("Bootstrap OK");

    // ... 40+ tests ...
}
```

**Note:** `knst_tls_set_skip_verify(true)` is **test-only** — **NEVER** in production.

### 28) `update_activity` — silent transition

```cpp
context::instance().update_activity(app->activity->clazz);
```

**Return value** usually ignored. On fail, **old activity stays** (maybe stale but works). Fixes on next rotation.

### 29) `class_loader` null — no fallback

```cpp
jobject loader = class_loader_.load();
if (!loader) return nullptr;   // ← find_class returns null
```

If `init` failed (class loader couldn't be cached), `find_class` returns **nullptr**. In that case there's **no `FindClass` fallback** — the system classloader doesn't see app classes.

**Result:** `find_class` works if `init` succeeded. Otherwise, it doesn't.

### 30) `session::find_class` — static, not instance

```cpp
static jclass find_class(const char* dotted_name) noexcept;
```

**Static** — no instance required. `session::find_class("...")` callable from anywhere (even outside session scope, if context is still valid).

### 31) `env()` inside `AttachCurrentThread` — JNI 1.6

```cpp
if (vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_EDETACHED) {
    if (vm->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
}
```

**`JNI_VERSION_1_6`** — standard since Android API 21. Works on older APIs too.

**Attach parameters:** `AttachCurrentThread(&env, nullptr)` — no thread name. You can use `JavaVMAttachArgs` to set thread name (library doesn't).

### 32) `AttachCurrentThread` inside `init` — temporary

```cpp
JNIEnv* e = nullptr;
if (vm->GetEnv(...) == JNI_EDETACHED)
    vm->AttachCurrentThread(&e, nullptr);
if (!e) return false;
```

**Attach is permanent** — not detached after `init`. The calling thread stays attached. This is **correct behavior** — attaching a thread is the responsibility of whoever attached it.

### 33) `context::shutdown` — attach + detach balanced

```cpp
bool attached_here = false;
if (GetEnv == JNI_EDETACHED) {
    AttachCurrentThread(...);
    attached_here = true;
}
// ... ref cleanup ...
if (attached_here) DetachCurrentThread();
```

**Only detach our own attach.** Don't hijack someone else's attach.

### 34) `session` static member — global state

```cpp
static inline session* g_current_ = nullptr;
```

**Single global pointer.** Don't create multiple `session`s — the last `g_current_` wins, earlier ones dangle.

**Rule:** One process = one session. Second session = bug.

### 35) `onAppCmd` chain — only restore our own handler

```cpp
if (app_->onAppCmd == &session::on_app_cmd_static_)
    app_->onAppCmd = user_cmd_;
```

**Why `if`?** The user may have overridden `onAppCmd` after session construct. In that case **our handler was already replaced** — restoring is wrong (would delete user's new handler).

**Result:** Only restore **our** installed handler. If user changed it later, don't touch.

---

## 📊 5) Performance

| Operation | Cost | Note |
|---|---|---|
| **`init` (first)** | ~100-500 µs | NewGlobalRef + ClassLoader lookup |
| **`init` (idempotent)** | ~1-5 µs | IsSameObject |
| **`update_activity`** | ~100-500 µs | New global ref + ClassLoader re-cache |
| **`shutdown`** | ~10-50 µs | DeleteGlobalRef |
| **`vm()` / `activity()` / `class_loader()`** | ~1 ns | Atomic load |
| **`valid()`** | ~2 ns | 2 atomic loads |
| **`env()` (attached)** | ~50-100 ns | GetEnv |
| **`env()` (detached)** | ~10-50 µs | AttachCurrentThread |
| **`find_class`** | ~1-10 µs | ClassLoader.loadClass |
| **`session` ctor** | ~200-600 µs | init + bind helpers |
| **`session` dtor** | ~50-100 µs | shutdown + finish |

**Typical latencies:**
- **session bootstrap:** 300-600 µs (once)
- **env() call (attached thread):** ~80 ns
- **find_class:** 1-10 µs

**Optimizations:**
- **Cache `env()`** — inside a loop, each call is ~80 ns; caching gives 0
- **Delete `find_class` in loops** — local ref limit
- **`session` once** — global state, only one

---

## ✅ 6) Do / ❌ Don't

### ✅ Do

```cpp
// 1. session in android_main
void android_main(struct android_app* app) {
    knst_android::session session(app);
    if (!knst_android::session::activity()) return;
    // ... work ...
}

// 2. Check session::activity() for null
if (!knst_android::session::activity()) {
    KNST_LOG_ERROR("init failed");
    return;
}

// 3. Delete find_class local ref
jclass cls = knst_android::session::find_class("com.example.Foo");
if (cls) {
    // ... use ...
    knst_android::session::env()->DeleteLocalRef(cls);
}

// 4. Cache env()
JNIEnv* env = knst_android::session::env();
for (int i = 0; i < 1000; ++i) {
    // use env, don't re-call
}

// 5. If user wants to set onAppCmd
app->onAppCmd = my_handler;   // ← after session construct BUT session handler overwritten
// ✅ Correct way: chain within session handler (already supported)

// 6. shutdown only on main thread
knst_android::context::instance().shutdown();   // ✅ main thread

// 7. No multiple sessions
knst_android::session s1(app);
knst_android::session s2(app);   // ❌ second — g_current_ broken
// ✅ std::unique_ptr<session> s1 = std::make_unique<session>(app);
```

### ❌ Don't

```cpp
// 1. Leak find_class local refs
for (int i = 0; i < 1000; ++i) {
    jclass c = knst_android::session::find_class("...");
    // ❌ No DeleteLocalRef → 512 limit crash
}

// 2. DeleteLocalRef activity()
jobject a = knst_android::session::activity();
knst_android::session::env()->DeleteLocalRef(a);   // ❌ GLOBAL ref deleted!

// 3. Move session
knst_android::session s1(app);
knst_android::session s2 = std::move(s1);   // ❌ compile error

// 4. Two sessions
knst_android::session s1(app);
knst_android::session s2(app);   // ❌ global state broken

// 5. Include header outside Android
#include "knst_android_context.hpp"   // ❌ empty on other platforms but no namespace

// 6. Call configure_modules directly
knst_android::context::instance().configure_modules(vm, activity);   // ❌ private, use KernelNucleusT.hpp

// 7. env() after shutdown
knst_android::context::instance().shutdown();
JNIEnv* env = knst_android::context::instance().env();   // ❌ nullptr

// 8. Override app->onAppCmd after session construct
knst_android::session session(app);
app->onAppCmd = my_handler;   // ❌ rotation handling lost

// 9. Call find_class on non-Android
knst_android::session::find_class(...);   // ❌ no namespace (guarded)

// 10. Call ANativeActivity_finish manually
ANativeActivity_finish(app->activity);   // ❌ session destructor does this
```

---

## 🔥 7) Real-World Examples

### Example 1: Test suite bootstrap

```cpp
void android_main(struct android_app* app) {
    KNST_LOG_INFO("=== Test Suite ===");

    knst_android::session session(app);
    if (!knst_android::session::activity()) {
        KNST_LOG_ERROR("FATAL: bootstrap failed");
        return;
    }

    KNST_LOG_INFO("Bootstrap OK");

    test_cpu();
    test_memory();
    // ... 40+ tests ...

    KNST_LOG_INFO("=== Tests complete ===");
}
// ← session destructor auto shutdown + finish
```

### Example 2: Manual init (without native_app_glue)

```cpp
// JNI entry point — if you're not using native_app_glue
extern "C" JNIEXPORT void JNICALL
Java_com_example_MainActivity_onCreate(JNIEnv* env, jobject, jobject activity) {
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    knst_android::context::instance().init(vm, activity);

    // ... app begins ...
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_MainActivity_onDestroy(JNIEnv*, jobject) {
    knst_android::context::instance().shutdown();
}
```

### Example 3: JNI from native thread

```cpp
void my_worker_thread() {
    // env() → attach (first time) or existing
    JNIEnv* env = knst_android::session::env();
    if (!env) {
        KNST_LOG_ERROR("no JNIEnv");
        return;
    }

    jclass helper = knst_android::session::find_class("com.knst.print.KnstPrintDiscoveryHelper");
    if (!helper) {
        KNST_LOG_ERROR("helper not found");
        return;
    }

    jmethodID m = env->GetStaticMethodID(helper, "someMethod", "()V");
    if (m) {
        env->CallStaticVoidMethod(helper, m);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    env->DeleteLocalRef(helper);   // ← delete local ref
}
```

### Example 4: Rotation handler (custom)

```cpp
// User wants to add their own onAppCmd
void my_on_app_cmd(struct android_app* app, int32_t cmd) {
    if (cmd == APP_CMD_GAINED_FOCUS) {
        KNST_LOG_INFO("Gained focus");
    }
}

void android_main(struct android_app* app) {
    knst_android::session session(app);

    // Session installed its own handler — ours is chained
    // BUT overriding after session construct breaks session:
    // app->onAppCmd = my_on_app_cmd;   // ❌ rotation handling lost

    // ✅ Correct: session's static handler already chains,
    // use a separate channel for state callbacks or
    // ignore session and use context manually
}
```

### Example 5: Manual rotation handling

```cpp
// If not using session
void my_on_app_cmd(struct android_app* app, int32_t cmd) {
    if (cmd == APP_CMD_INIT_WINDOW || cmd == APP_CMD_CONFIG_CHANGED) {
        if (app && app->activity && app->activity->clazz) {
            knst_android::context::instance().update_activity(app->activity->clazz);
        }
    }
}

void android_main(struct android_app* app) {
    JavaVM* vm = app->activity->vm;
    knst_android::context::instance().init(vm, app->activity->clazz);
    app->onAppCmd = my_on_app_cmd;
    // ...
}
```

---

## 📌 8) API Contract Summary

```cpp
namespace knst_android {

// ─── context ───
class context {
public:
    static context& instance() noexcept;

    // Lifecycle
    bool init(JavaVM* vm, jobject activity) noexcept;
    bool update_activity(jobject new_activity) noexcept;
    void shutdown() noexcept;

    // State
    JavaVM* vm()           const noexcept;
    jobject activity()     const noexcept;
    jobject class_loader() const noexcept;
    bool    valid()        const noexcept;

    // JNI helpers
    JNIEnv* env() noexcept;
    jclass  find_class(const char* dotted_name) noexcept;

private:
    context() noexcept = default;
    ~context() { shutdown(); }
    context(const context&) = delete;
    context& operator=(const context&) = delete;

    // Defined in KernelNucleusT.hpp (after all module headers)
    void configure_modules(JavaVM* vm, jobject activity) noexcept;

    std::atomic<JavaVM*> vm_{nullptr};
    std::atomic<jobject> activity_{nullptr};
    std::atomic<jobject> class_loader_{nullptr};
};

// ─── session (RAII) ───
#if KNST_HAVE_ANDROID_APP_STRUCT

class session {
public:
    explicit session(struct android_app* app) noexcept;
    ~session() noexcept;

    session(const session&) = delete;
    session& operator=(const session&) = delete;
    session(session&&) = delete;
    session& operator=(session&&) = delete;

    static jobject activity()  noexcept;
    static JNIEnv* env()       noexcept;
    static jclass  find_class(const char* dotted_name) noexcept;
};

#endif

} // namespace knst_android
```

---

## 🎯 What It Brings You

`knst_android_context` gives you serious advantages in these situations:

- ✅ **One init** — 3 modules don't each call `set_*_context` separately
- ✅ **RAII session** — Construct = init, destruct = shutdown + finish
- ✅ **Global ref management** — Activity + ClassLoader automatic
- ✅ **ClassLoader cache** — App class access from native threads
- ✅ **Automatic rotation** — `onAppCmd` chain, user handler preserved
- ✅ **Idempotent init** — Fast-path via `IsSameObject`
- ✅ **Thread-safe reads** — Atomic accessors
- ✅ **Attach guard** — `shutdown` only detaches our own attach
- ✅ **`nullptr` safety** — Every public API defensive
- ✅ **Java helper auto-bind** — Print helper + ML Kit scanner
- ✅ **Missing helper warning** — No crash, graceful skip
- ✅ **`ANativeActivity_finish`** — Signal to OS "close" (prevents battery drain)
- ✅ **`__has_include` guard** — 3 different `native_app_glue` paths
- ✅ **KernelNucleusT integration** — `configure_modules` fanout

---