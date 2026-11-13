# knst_android_context — Android JNI Bağlam Yönetimi

Selamlar! 👋 `knst_android_context.hpp`, KernelNucleusT'nin **Android JNI bağlam** modülü. Tek header'da: **process-genelinde JavaVM + Activity + ClassLoader** saklama, **otomatik rotation handling**, **modüllere fanout**, **RAII `session`** ile kullanıcıyı tüm JNI detaylarından kurtarma.

**Kısaca:** **Android-only**, header-only, exception-free, RTTI yok. Sadece `#if defined(KNST_USING_PLATFORM_ANDROID)` altında var — diğer platformlarda içerik yok (no-op). `android_main()` içinde **tek satır** `knst_android::session session(app);` yeterli — JavaVM, Activity, ClassLoader, modül context'leri hepsi halledilir. **Thread-safe okuma** (`vm()`, `activity()`, `env()`). `env()` çağıran thread'i **gerektiğinde otomatik attach eder** (detach sizin sorumluluğunuzda değil — Android böyle çalışır).

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

### Platform matrisi

| Özellik | Android | Diğer Platformlar |
|---|---|---|
| **Header içeriği** | ✅ | ❌ Tamamen boş (guard'lı) |
| **JavaVM saklama** | ✅ | — |
| **Activity saklama (GlobalRef)** | ✅ | — |
| **ClassLoader cache** | ✅ | — |
| **`init()` / `shutdown()`** | ✅ | — |
| **Rotation handling (`update_activity`)** | ✅ | — |
| **Thread-safe okuma** | ✅ | — |
| **Otomatik AttachCurrentThread** | ✅ | — |
| **`find_class()` native thread'den** | ✅ | — |
| **`session` RAII wrapper** | ✅ (native_app_glue varsa) | — |
| **Otomatik rotation (onAppCmd chain)** | ✅ | — |
| **Java helper otomatik bind** | ✅ | — |
| **`ANativeActivity_finish()`** | ✅ | — |
| **Legacy `set_*_context` uyumu** | ✅ | — |

**Not:** Header **sadece Android build'lerde** derlenir. `#if !defined(KNST_USING_PLATFORM_ANDROID)` durumunda tamamen boş — diğer platformlarda include etmek güvenli.

### Bağımlılıklar

| Bağımlılık | Ne için? | Zorunlu? |
|---|---|---|
| **`<jni.h>`** | JNI tipleri | ✅ |
| **`<atomic>`** | Thread-safe state | ✅ |
| **`<cstdio>`** | `fprintf` (helper warning) | ✅ |
| **`<android/native_app_glue.h>`** | `session` RAII | ⚠️ Sadece `session` için |
| **`KernelNucleusT.hpp`** | `configure_modules()` tanımı | ✅ (sonra) |

**`session` kullanmıyorsanız** `native_app_glue` gerekmez — `context` tek başına yeterli. `KNST_HAVE_ANDROID_APP_STRUCT` makro guard'ı bunu otomatik halleder (`__has_include` ile 3 farklı path denenir).

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Tek init, tüm modüller** | `set_java_vm` 3 modülde ayrı ayrı çağrılmaz |
| **Singleton context** | `context::instance()` her yerden erişim |
| **Global ref yönetimi** | Activity + ClassLoader `NewGlobalRef`/`DeleteGlobalRef` otomatik |
| **`IsSameObject` idempotency** | Aynı activity ile tekrar `init()` = no-op |
| **ClassLoader cache** | App class'ları native thread'den görülebilir |
| **Rotation handling** | `APP_CMD_INIT_WINDOW` / `APP_CMD_CONFIG_CHANGED` otomatik |
| **`session` RAII** | Construct = init, destruct = shutdown + finish |
| **onAppCmd chain** | Kullanıcının handler'ı korunur, üstüne eklenir |
| **Java helper auto-bind** | `KnstPrintDiscoveryHelper` + `KnstMlKitScannerHelper` |
| **Missing helper warning** | `stderr`'a "missing required / optional not linked" |
| **Attach/Detach per call** | `env()` çağıran thread'i attach eder |
| **`shutdown` attach guard** | Sadece **kendi** attach ettiği thread'i detach eder |
| **`find_class` herhangi thread'den** | ClassLoader üzerinden `loadClass` |
| **Legacy uyum** | Eski `set_*_context` çağrıları hâlâ çalışır |
| **`__has_include` guard** | 3 farklı `native_app_glue` yolu otomatik |
| **Exception clear** | JNI exception'ları sessizce temizlenir |
| **Null-safety** | `nullptr` → `false` / `nullptr` döner |

---

## 🚀 Hızlı Başlangıç

### En basit — `session` (önerilen)

```cpp
#include "KernelNucleusT.hpp"

void android_main(struct android_app* app) {
    knst_android::session session(app);   // ← tek satır
    if (!knst_android::session::activity()) {
        KNST_LOG_ERROR("session init failed");
        return;
    }

    // Artık her şey hazır — vm/clazz/env YOK
    test_cpu();
    test_memory();
    // ...
}
```

**Ne olur?**
1. `context::instance().init(vm, activity)` çağrılır
2. Modüller fanout alır (`knst_devices`, `knst_net`, `knst_prn`)
3. `onAppCmd` **chain**'lenir (kullanıcının handler'ı korunur)
4. Java helper'lar bind edilir
5. Destructor: `shutdown()` + `ANativeActivity_finish()`

### Manuel — `context` (nadir)

```cpp
// Activity'yi bildiğiniz yerde
knst_android::context::instance().init(vm, activity);

// ... kullan ...

// Temizlik
knst_android::context::instance().shutdown();
```

### Advanced accessors

```cpp
// session içinden herhangi yerde
JNIEnv* env = knst_android::session::env();
jobject activity = knst_android::session::activity();

// Class lookup (herhangi thread'den)
jclass cls = knst_android::session::find_class("com.knst.print.MyHelper");
if (cls) {
    // ... kullan ...
    env->DeleteLocalRef(cls);   // ← caller sorumluluğunda
}
```

---

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Context erişimi** | `context::instance()` |
| **Yaşam döngüsü** | `init()`, `update_activity()`, `shutdown()` |
| **Durum** | `valid()` |
| **Accessor** | `vm()`, `activity()`, `class_loader()` |
| **JNI yardımcıları** | `env()`, `find_class()` |
| **Session** | `session(app)`, `session::activity()`, `session::env()`, `session::find_class()` |

---

## 🎯 1) `context` — Singleton Bağlam

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

**Thread-safe** (C++11 static init). Her yerden aynı instance'a erişilir.

### 1.2) `init(vm, activity)` — Bağlamı kur

```cpp
bool init(JavaVM* vm, jobject activity) noexcept;
```

**Ne yapar?**
1. Calling thread'i attach eder (gerekirse)
2. `activity` üzerinden **`IsSameObject`** kontrolü — aynıysa **no-op**, `true` döner
3. `activity` → `NewGlobalRef` (thread'ler arası yaşar)
4. **ClassLoader** cache: `activity.getClassLoader()` → `NewGlobalRef`
5. Eski global ref'leri temizle (`DeleteGlobalRef`)
6. `vm_` atomic olarak güncelle
7. **`configure_modules(vm, activity)`** çağır — KernelNucleusT.hpp'de tanımlı, eski `set_*_context`'lere fanout

**Döndürdüğü:** `true` = bağlam hazır, `false` = fail (vm/activity `nullptr` veya `NewGlobalRef` fail).

**Idempotent:** Aynı Activity objesi ile tekrar çağırırsanız no-op. `jobject` pointer'ları farklı olsa bile aynı Java objesini gösteriyorsa `IsSameObject` bunu yakalar.

### 1.3) `update_activity(new_activity)` — Rotation

```cpp
bool update_activity(jobject new_activity) noexcept;
```

**Ne yapar?** Saklı `vm` ile `init(vm, new_activity)` çağırır.

**Ne zaman?**
- Rotation (`APP_CMD_CONFIG_CHANGED`)
- Tema değişikliği (Activity recreate)
- `APP_CMD_INIT_WINDOW`

**Not:** ClassLoader yeni Activity'de farklı olabilir — `init()` otomatik yeniden cache'ler.

### 1.4) `shutdown()` — Temizlik

```cpp
void shutdown() noexcept;
```

**Ne yapar?**
1. `activity_` ve `class_loader_` global ref'lerini `DeleteGlobalRef` eder
2. `vm_` atomic'ini `nullptr` yapar
3. **Sadece bu çağrı attach ettiyse** `DetachCurrentThread` çalışır

**Idempotent:** Birden fazla çağrı güvenli.

**Kritik:** Attach guard — eğer çağıran thread **zaten attach'tıysa**, `shutdown()` sonrası da attach kalır. Bu **doğru davranış** — kütüphane başka birinin attach'ını çalmamalı.

### 1.5) Accessor'lar

```cpp
JavaVM* vm()           const noexcept;   // atomic load
jobject activity()     const noexcept;   // atomic load
jobject class_loader() const noexcept;   // atomic load
bool    valid()        const noexcept;   // vm && activity
```

**Thread-safe.** Acquire/release semantics.

**Dikkat:** `activity()` dönen `jobject` bir **global ref** — silmemelisiniz. `context` sahibi.

### 1.6) `env()` — Calling thread'in JNIEnv'i

```cpp
JNIEnv* env() noexcept;
```

**Ne yapar?**
1. `vm` yoksa `nullptr`
2. `GetEnv` den:
   - `JNI_OK` → mevcut env
   - `JNI_EDETACHED` → `AttachCurrentThread` ile attach et
   - Diğer → `nullptr`

**Döndürdüğü:** Calling thread'in `JNIEnv*` — attach gerekiyorsa yapılmış.

**Not:** Attach **kalıcıdır**. Thread biterse JVM otomatik detach eder. Detach istemiyorsanız (genelde istemezsiniz) elle yapmayın.

### 1.7) `find_class(dotted_name)` — Class lookup

```cpp
jclass find_class(const char* dotted_name) noexcept;
```

**Ne yapar?**
1. `env()` al
2. `class_loader_` al (yoksa `nullptr`)
3. ClassLoader üzerinden `loadClass(String)` çağır
4. **Exception clear** — ClassNotFound sessizce `nullptr` döner

**Döndürdüğü:** **Local reference** — caller `DeleteLocalRef` yapmalı.

**Neden ClassLoader üzerinden?** Android'de system classloader app class'larını **görmez**. Native thread'den `FindClass` çağırırsanız sadece system class'ları görürsünüz. ClassLoader cache ile app class'larına erişilir.

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

**Ne yapar?**
1. `app` veya `app->activity` null ise → boş kal (sonradan `session::activity()` null döner)
2. `context::instance().init(vm, activity)` — fail ise boş kal
3. **`onAppCmd` chain**: önceki handler saklanır, yeni handler kurulur
4. **Java helper bind**:
   - `com.knst.print.KnstPrintDiscoveryHelper` (**required**)
   - `com.knst.print.KnstMlKitScannerHelper` (optional)
5. Her başarısız bind için `stderr`'a uyarı basar (crash yok)

**`onAppCmd` chain:**

```cpp
user_cmd_ = app->onAppCmd;          // eski handler'ı sakla
app->onAppCmd = &on_app_cmd_static_; // bizimkini kur
```

Bizim handler:
- `APP_CMD_INIT_WINDOW` veya `APP_CMD_CONFIG_CHANGED` → `update_activity()` çağır
- Sonra `user_cmd_` varsa onu **chain** et

**Sonuç:** Kullanıcının kendi `onAppCmd` handler'ı **korunur**, üstüne eklenir. Rotation otomatik yönetilir.

### 2.2) Destructor

```cpp
~session() noexcept;
```

**Ne yapar?**
1. `onAppCmd` bizim handler ise → `user_cmd_`'e geri döndür (chain'i çöz)
2. `context::instance().shutdown()` — global ref'leri serbest bırak
3. **`ANativeActivity_finish(app_->activity)`** — OS'e "uygulamayı kapat" der

**Kritik:** `ANativeActivity_finish` **çağrılmazsa** uygulama arka planda kalır, RAM tüketmeye devam eder. `session` destructor'u bunu halleder.

### 2.3) Static accessor'lar

```cpp
static jobject  activity()  noexcept;                     // context::instance().activity()
static JNIEnv*  env()       noexcept;                     // context::instance().env()
static jclass   find_class(const char* dotted_name) noexcept; // context::instance().find_class()
```

**Convenience** — `context::instance()` yazmadan aynı şey. Kullanıcının `context`'i bilmesi gerekmiyor.

**`find_class` local ref döner** — caller `DeleteLocalRef` yapmalı.

### 2.4) Ne zaman `session` kullanmalı, ne zaman `context`?

| Durum | Kullan |
|---|---|
| `android_main(android_app*)` var | **`session`** (önerilen) |
| `android_main` yok, başka yerden init | **`context`** (manuel) |
| `native_app_glue` linkli değil | **`context`** (mecburi) |
| İkisi de | **`session`** (daha az kod) |

`session` kullanmıyorsanız `KNST_HAVE_ANDROID_APP_STRUCT=0` olur, `session` sınıfı **derlenmez** (guard'lı). `context` her durumda kullanılabilir.

---

## 📱 3) Kullanım Örnekleri

### Örnek 1: Klasik `android_main` (test suite'den)

```cpp
void android_main(struct android_app* app) {
    KNST_LOG_INFO("Test suite starting");

    knst_android::session session(app);
    if (!knst_android::session::activity()) {
        KNST_LOG_ERROR("FATAL: session init failed");
        return;
    }

    KNST_LOG_INFO("Bootstrap OK");

    // Her test — JNI/vm/clazz/env YOK
    test_cpu();
    test_memory();
    test_storage();
    // ...
}
```

**Satır sayısı:** 1 satır (`session` construct). Gerisi normal test kodu.

### Örnek 2: Manuel `context` (native_app_glue yoksa)

```cpp
// Herhangi bir init fonksiyonu
extern "C" JNIEXPORT void JNICALL
Java_com_example_MainActivity_onCreate(JNIEnv* env, jobject, jobject activity) {
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);

    knst_android::context::instance().init(vm, activity);

    // ... kullan ...
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_MainActivity_onDestroy(JNIEnv*, jobject) {
    knst_android::context::instance().shutdown();
}
```

### Örnek 3: Rotation handling (session otomatik)

```cpp
// session constructor içinde:
user_cmd_ = app->onAppCmd;
app->onAppCmd = &session::on_app_cmd_static_;

// static handler:
static void on_app_cmd_static_(struct android_app* app, int32_t cmd) noexcept {
    if (cmd == APP_CMD_INIT_WINDOW || cmd == APP_CMD_CONFIG_CHANGED) {
        if (app && app->activity && app->activity->clazz)
            context::instance().update_activity(app->activity->clazz);
    }
    if (g_current_ && g_current_->user_cmd_)
        g_current_->user_cmd_(app, cmd);   // kullanıcının handler'ı
}
```

**Sonuç:** Kullanıcı `onAppCmd` set etse bile **session rotasyonu yönetir**, kullanıcı sadece kendi işini yapar.

### Örnek 4: Native thread'den class lookup

```cpp
void my_worker_thread() {
    // context::instance().env() → thread attach
    // find_class → ClassLoader üzerinden app class
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

    env->DeleteLocalRef(helper);   // ← caller sorumluluğu
}
```

**Neden `FindClass` değil?** `FindClass` system classloader kullanır — Android'de app class'larını görmez. `find_class` cached ClassLoader üzerinden gider.

### Örnek 5: Shutdown sequence

```cpp
// Kullanıcı uygulamayı kapatıyor
~session() {
    // 1. onAppCmd chain'ini çöz
    if (app_->onAppCmd == &session::on_app_cmd_static_)
        app_->onAppCmd = user_cmd_;

    // 2. Global ref'leri bırak
    context::instance().shutdown();

    // 3. OS'e "kapat" de
    ANativeActivity_finish(app_->activity);
}
```

**`ANativeActivity_finish` çağrılmazsa:** uygulama arka planda kalır, test suite bitse bile process devam eder.

---

## ⚠️ 4) Sık Tuzaklar

### 1) Header sadece Android'de derlenir

```cpp
#if defined(KNST_USING_PLATFORM_ANDROID)
#include "knst_android_context.hpp"
#endif
```

`#if !defined(KNST_USING_PLATFORM_ANDROID)` durumunda dosya **tamamen boş** — include etmek güvenli ama `knst_android::` namespace'i yok.

### 2) `session` için `native_app_glue` gerekli

```cpp
#if KNST_HAVE_ANDROID_APP_STRUCT
// session defined
#endif
```

`native_app_glue.h` include edilemezse → `session` sınıfı **derlenmez**. `context` hâlâ çalışır (sadece `vm + activity` gerekli).

**Kontrol:** `__has_include(<android/native_app_glue.h>)` → 3 farklı path denenir.

### 3) `activity` global ref — silmeyin

```cpp
jobject a = knst_android::session::activity();
// ... kullan ...
// ❌ env->DeleteLocalRef(a);   // YANLIŞ — bu GLOBAL ref
```

`activity()` **global ref** döner — `context` sahibi. **Silmeyin.** Sadece `find_class()` **local ref** döner.

### 4) `find_class` local ref — silmelisiniz

```cpp
jclass cls = knst_android::session::find_class("com.example.Foo");
// ... kullan ...
env->DeleteLocalRef(cls);   // ✅ ZORUNLU
```

**Local ref limiti:** Android'de 512 local ref sınırı var. `find_class` loop içinde çağırıp silmezseniz **crash** eder.

### 5) `init` idempotent ama ucuz değil

```cpp
context::instance().init(vm, activity);   // ilk çağrı — pahalı
context::instance().init(vm, activity);   // ikinci çağrı — IsSameObject hit, ucuz
```

**Aynı activity** → `IsSameObject` sayesinde hızlı döner. **Farklı activity** → yeni global ref + ClassLoader re-cache.

### 6) `IsSameObject` — pointer eşitliği değil

```cpp
// ❌ Yanlış:
if (cur == activity) return true;   // pointer karşılaştırması

// ✅ Doğru:
if (cur && e->IsSameObject(cur, activity)) return true;
```

Farklı `jobject` **handle'ları** aynı Java objesine işaret edebilir. `IsSameObject` tek doğru yol.

### 7) `shutdown` attach guard

```cpp
void shutdown() noexcept {
    // ...
    bool attached_here = false;
    if (GetEnv == JNI_EDETACHED) {
        AttachCurrentThread(...);
        attached_here = true;
    }
    // ... iş ...
    if (attached_here) DetachCurrentThread();   // ← sadece kendi attach'ımızı detach et
}
```

**Neden?** Eğer çağıran thread **zaten attach'tıysa**, `shutdown()` sonrası attach kalmalı. Aksi halde kütüphane başkasının attach'ını çalar → o thread crash eder.

### 8) `session` non-copyable, non-movable

```cpp
knst_android::session s1(app);
knst_android::session s2 = std::move(s1);   // ❌ compile error
```

**Neden?** `onAppCmd` pointer'ı `this`'e chain'li. Move sırasında pointer invalidate olur → crash.

**Çözüm:** `std::unique_ptr<session>` veya stack scope.

### 9) `configure_modules` — KernelNucleusT.hpp'de tanımlı

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

**Header-only değil** — `configure_modules` gövdesi **tüm modül header'ları include edildikten sonra** `KernelNucleusT.hpp`'de tanımlı. Bu **circular dependency** kırmak için.

**Sonuç:** `knst_android_context.hpp` **tek başına include edilirse** `configure_modules` unresolved kalır. Her zaman `KernelNucleusT.hpp` üzerinden kullanın.

### 10) Java helper missing — uyarı, crash yok

```cpp
bind("com.knst.print.KnstPrintDiscoveryHelper", ..., /*required*/ true);
bind("com.knst.print.KnstMlKitScannerHelper", ..., /*required*/ false);
```

Helper class yoksa `stderr`'a yazar:
```
[KNST] missing required helper: com.knst.print.KnstPrintDiscoveryHelper
[KNST] optional not linked helper: com.knst.print.KnstMlKitScannerHelper
```

**Crash yok** — ilgili özellik `NotSupported` döner. Test suite'de graceful skip.

### 11) `env()` attach kalıcıdır

```cpp
JNIEnv* env = knst_android::session::env();
// ... thread buradan çıkıyor ...
// Thread hâlâ attached — JVM otomatik detach eder
```

**Attach süresi = thread ömrü.** Native thread her `env()` çağrısında attach **etmez** (ilk sefer attach eder, sonra cache'li).

**Aşırı attach endişesi yok** — Android JVM thread pool'u sınırlı olsa da pratikte sorun çıkmaz.

### 12) Rotation — `update_activity` otomatik

```cpp
// session constructor:
if (cmd == APP_CMD_INIT_WINDOW || cmd == APP_CMD_CONFIG_CHANGED) {
    context::instance().update_activity(app->activity->clazz);
}
```

Kullanıcı hiçbir şey yapmaz. **Activity recreate** olduğunda `context` otomatik güncellenir, ClassLoader yenilenir.

### 13) `ANativeActivity_finish` — neden gerekli?

```cpp
~session() {
    // ...
    ANativeActivity_finish(app_->activity);   // ← OS'e "kapat" sinyali
}
```

**Çağrılmazsa:** `android_main` return eder ama **process yaşamaya devam eder**. RAM, CPU, pil tüketimi sürer. Battery drain + ANR riski.

**Çağrılırsa:** OS `onDestroy` zincirini başlatır, process temiz kapanır.

### 14) `onAppCmd` chain — kullanıcı handler'ı korunur

```cpp
// session constructor:
user_cmd_ = app->onAppCmd;
app->onAppCmd = &session::on_app_cmd_static_;

// session destructor:
if (app_->onAppCmd == &session::on_app_cmd_static_)
    app_->onAppCmd = user_cmd_;
```

**Kullanıcı `onAppCmd` set etmişse** — `session` üstüne ekler, `session` bitince geri koyar. **Sıralı**: session handler → user handler.

**Ama:** kullanıcı `session` construct'ından **sonra** `app->onAppCmd` set ederse **session'ınki ezilir**. Bu durumda rotation handling çalışmaz. **Doğru sıra:** session önce, kullanıcı handler sonra (veya session'ın static'lerini kullan).

### 15) `valid()` — vm + activity kontrolü

```cpp
if (!knst_android::context::instance().valid()) {
    // init başarısız
}
```

**`session` construct sonrası** `session::activity()` null dönerse → init fail. Test suite'de ilk kontrol.

### 16) `session::env()` — `context::env()` wrapper

```cpp
static JNIEnv* env() noexcept { return context::instance().env(); }
```

**Ekstra maliyet yok** — inline. `context` singleton + `env()` çağrısı.

### 17) JNI exception clear — sessiz temizlik

```cpp
if (e->ExceptionCheck()) e->ExceptionClear();
```

ClassNotFound gibi hatalarda JNI exception oluşur. **Temizlenmezse** sonraki JNI çağrıları fail eder. `init()` ve `find_class()` her exception sonrası `ExceptionClear` çağırır.

**Sonuç:** Missing class `nullptr` döner, kütüphane graceful handle eder.

### 18) `NewStringUTF` — UTF-8 sınırı

```cpp
jstring jn = e->NewStringUTF(dotted_name);   // dotted_name must be UTF-8
```

`NewStringUTF` **modified UTF-8** bekler. `dotted_name` ASCII olmalı (class isimleri ASCII olur).

**Non-ASCII class ismi** (çok nadir) → `NewStringUTF` bozuk string üretir. Android'de class isimleri Java standardı gereği ASCII.

### 19) `find_class` — 512 local ref limiti

```cpp
// ❌ Yanlış — her çağrı 1 local ref, silmez
for (int i = 0; i < 1000; ++i) {
    jclass c = knst_android::session::find_class("com.example.Foo");
    // ...
}

// ✅ Doğru
for (int i = 0; i < 1000; ++i) {
    jclass c = knst_android::session::find_class("com.example.Foo");
    // ... kullan ...
    knst_android::session::env()->DeleteLocalRef(c);
}
```

**512 local ref** dolunca Android **crash** eder. `find_class` loop içindeyse silmek zorunlu.

### 20) Thread-safety garantisi

```cpp
std::atomic<JavaVM*> vm_;
std::atomic<jobject> activity_;
std::atomic<jobject> class_loader_;
```

**`vm()`, `activity()`, `class_loader()`, `valid()` thread-safe.** Acquire/release semantics.

**`init`/`shutdown`** thread-safe değil (teorik olarak). **Pratik:** bir kez init (main thread), bir kez shutdown (main thread). Çağırıcı sorumluluğu.

### 21) `class_loader` rotation'da değişebilir

```cpp
bool update_activity(jobject new_activity) noexcept {
    return init(vm_.load(), new_activity);
}
```

`init` içinde **yeni ClassLoader cache'lenir** — eski ClassLoader **invalidate** olur. Yeni Activity'de farklı classloader olabilir (plugin'ler vs.), otomatik handle.

### 22) `nullptr` safety

```cpp
if (!vm || !activity) return false;      // init
if (!new_activity) return false;         // update_activity
if (!dotted_name) return nullptr;        // find_class
if (!app || !app->activity) return;      // session ctor
```

Her public API `nullptr` input'a karşı savunmacı.

### 23) `session` destructor — `shutdown()` içinde `env()` yok

```cpp
~session() {
    context::instance().shutdown();   // ← env() yok, sadece ref temizliği
    ANativeActivity_finish(app_->activity);
}
```

**Neden `env()` çağrılmıyor?** `shutdown()` içinde `AttachCurrentThread` zaten yapılıyor. Ekstra `env()` gereksiz.

### 24) Destructor çift shutdown

```cpp
~session() {
    if (!app_) return;   // ← guard
    // ...
    context::instance().shutdown();
    // ...
    app_ = nullptr;
}
```

**Guard:** Eğer constructor fail ettiyse (`app_ = nullptr`), destructor **hiçbir şey yapmaz**. `shutdown()` boş context'e no-op.

### 25) `session` move edilemez — neden?

```cpp
session(session&&) = delete;
```

**Neden:** `g_current_` static pointer `this`'e işaret eder. Move sırasında pointer invalidate olur → `on_app_cmd_static_` crash eder.

**Çözüm:** Stack scope (RAII zaten bunu zorlar).

### 26) `configure_modules` — legacy uyum

```cpp
// KernelNucleusT.hpp:
void context::configure_modules(JavaVM* vm, jobject activity) noexcept {
    knst_devices::detail_android::set_java_vm(vm, activity);       // eski
    knst_net_detail::android_jni::set_context(vm, activity);       // eski
    knst_prn::detail::set_jni_context(vm, activity);                // eski
}
```

**Eski kod:** hâlâ `set_java_vm` çağırabilir. **Yeni kod:** sadece `context::instance().init(...)`. **Kütüphane:** fanout otomatik.

**Sonuç:** Migration zorunlu değil, ama `context` kullanmak **daha temiz** (tek init, tek shutdown).

### 27) `session` — Android test suite'de tipik kullanım

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

    // ... 40+ test ...
}
```

**Not:** `knst_tls_set_skip_verify(true)` sadece **test** için — production'da ASLA.

### 28) `update_activity` — sessiz geçiş

```cpp
context::instance().update_activity(app->activity->clazz);
```

**Dönüş değeri** genelde ignore edilir. Fail olursa **eski activity kalır** (belki stale ama çalışır). Sonraki rotation'da düzelir.

### 29) `class_loader` null — fallback yok

```cpp
jobject loader = class_loader_.load();
if (!loader) return nullptr;   // ← find_class null döner
```

`init` fail ettiyse (class loader cache'lenemedi) `find_class` **nullptr** döner. Bu durumda `FindClass` fallback **yok** — çünkü system classloader app class'larını görmez.

**Sonuç:** `init` başarılıysa `find_class` çalışır. Aksi halde çalışmaz.

### 30) `session::find_class` — static, instance değil

```cpp
static jclass find_class(const char* dotted_name) noexcept;
```

**Static** — instance gerekmez. `session::find_class("...")` herhangi yerden çağrılabilir (session scope'u dışında bile, context hâlâ geçerliyse).

### 31) `env()` içinde `AttachCurrentThread` — JNI 1.6

```cpp
if (vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_EDETACHED) {
    if (vm->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
}
```

**`JNI_VERSION_1_6`** — Android API 21'den itibaren standart. Eski API'lerde de çalışır.

**Attach parametreleri:** `AttachCurrentThread(&env, nullptr)` — thread name belirtilmez. İsterseniz `JavaVMAttachArgs` ile thread name set edebilirsiniz (kütüphane bunu yapmaz).

### 32) `init` içinde `AttachCurrentThread` — geçici

```cpp
JNIEnv* e = nullptr;
if (vm->GetEnv(...) == JNI_EDETACHED)
    vm->AttachCurrentThread(&e, nullptr);
if (!e) return false;
```

**Attach kalıcı** — `init` sonrası detach **edilmez**. Çağıran thread attach kalır. Bu **doğru davranış** — attach edilen thread'in detach'ı sorumluluk sahibine ait.

### 33) `context::shutdown` — attach + detach dengeli

```cpp
bool attached_here = false;
if (GetEnv == JNI_EDETACHED) {
    AttachCurrentThread(...);
    attached_here = true;
}
// ... ref temizliği ...
if (attached_here) DetachCurrentThread();
```

**Sadece kendi attach'ımızı detach ediyoruz.** Başka birinin attach'ını çalmıyoruz.

### 34) `session` static üyeler — global state

```cpp
static inline session* g_current_ = nullptr;
```

**Tek global pointer.** Birden fazla `session` oluşturmayın — son `g_current_` kazanır, önceki dangling olur.

**Kural:** Bir process = bir session. İkinci session = bug.

### 35) `onAppCmd` chain — sadece kendi handler'ımızı geri koy

```cpp
if (app_->onAppCmd == &session::on_app_cmd_static_)
    app_->onAppCmd = user_cmd_;
```

**Neden `if`?** Kullanıcı session construct sonrası `onAppCmd` override etmiş olabilir. Bu durumda **bizim handler zaten değiştirilmiş** — geri koymak yanlış olur (kullanıcının yeni handler'ı silinir).

**Sonuç:** Sadece **bizim koyduğumuz** handler'ı geri alıyoruz. Kullanıcı sonradan değiştirdiyse dokunmuyoruz.

---

## 📊 5) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| **`init` (ilk)** | ~100-500 µs | NewGlobalRef + ClassLoader lookup |
| **`init` (idempotent)** | ~1-5 µs | IsSameObject |
| **`update_activity`** | ~100-500 µs | Yeni global ref + ClassLoader re-cache |
| **`shutdown`** | ~10-50 µs | DeleteGlobalRef |
| **`vm()` / `activity()` / `class_loader()`** | ~1 ns | Atomic load |
| **`valid()`** | ~2 ns | 2 atomic load |
| **`env()` (attached)** | ~50-100 ns | GetEnv |
| **`env()` (detached)** | ~10-50 µs | AttachCurrentThread |
| **`find_class`** | ~1-10 µs | ClassLoader.loadClass |
| **`session` ctor** | ~200-600 µs | init + bind helpers |
| **`session` dtor** | ~50-100 µs | shutdown + finish |

**Tipik latencies:**
- **session bootstrap:** 300-600 µs (bir kez)
- **env() çağrısı (attached thread):** ~80 ns
- **find_class:** 1-10 µs

**Optimizasyon:**
- **`env()` cache'leyin** — loop içinde her çağrı ~80 ns, ama cache'lerseniz 0
- **`find_class` loop'ta silin** — local ref limit
- **`session` bir kez** — global state, iki tane olmaz

---

## ✅ 6) Do / ❌ Don't

### ✅ Do

```cpp
// 1. android_main'de session
void android_main(struct android_app* app) {
    knst_android::session session(app);
    if (!knst_android::session::activity()) return;
    // ... iş ...
}

// 2. session::activity() null kontrolü
if (!knst_android::session::activity()) {
    KNST_LOG_ERROR("init failed");
    return;
}

// 3. find_class local ref sil
jclass cls = knst_android::session::find_class("com.example.Foo");
if (cls) {
    // ... kullan ...
    knst_android::session::env()->DeleteLocalRef(cls);
}

// 4. env() cache
JNIEnv* env = knst_android::session::env();
for (int i = 0; i < 1000; ++i) {
    // env kullan, tekrar çağırma
}

// 5. Kullanıcı onAppCmd set etmek istiyorsa
app->onAppCmd = my_handler;   // ← session construct sonrası AMA session handler'ı ezilir
// ✅ Doğru yol: session handler'ı içinde chain'le (zaten destekli)

// 6. shutdown sadece main thread'de
knst_android::context::instance().shutdown();   // ✅ main thread

// 7. Birden fazla session YOK
knst_android::session s1(app);
knst_android::session s2(app);   // ❌ ikinci — g_current_ bozulur
// ✅ std::unique_ptr<session> s1 = std::make_unique<session>(app);
```

### ❌ Yapma

```cpp
// 1. find_class local ref sızdırma
for (int i = 0; i < 1000; ++i) {
    jclass c = knst_android::session::find_class("...");
    // ❌ DeleteLocalRef YOK → 512 limit crash
}

// 2. activity() DeleteLocalRef
jobject a = knst_android::session::activity();
knst_android::session::env()->DeleteLocalRef(a);   // ❌ GLOBAL ref silinir!

// 3. session move
knst_android::session s1(app);
knst_android::session s2 = std::move(s1);   // ❌ compile error

// 4. session iki tane
knst_android::session s1(app);
knst_android::session s2(app);   // ❌ global state bozulur

// 5. header'ı Android dışında include
#include "knst_android_context.hpp"   // ❌ diğer platformlarda boş ama namespace yok

// 6. configure_modules'ı doğrudan çağır
knst_android::context::instance().configure_modules(vm, activity);   // ❌ private, KernelNucleusT.hpp kullan

// 7. shutdown sonrası env()
knst_android::context::instance().shutdown();
JNIEnv* env = knst_android::context::instance().env();   // ❌ nullptr

// 8. session construct sonrası app->onAppCmd override
knst_android::session session(app);
app->onAppCmd = my_handler;   // ❌ rotation handling kaybolur

// 9. find_class'ı non-Android'de çağır
knst_android::session::find_class(...);   // ❌ namespace yok (guard'lı)

// 10. ANativeActivity_finish'i elle çağır
ANativeActivity_finish(app->activity);   // ❌ session destructor yapar
```

---

## 🔥 7) Gerçek Kullanım Örnekleri

### Örnek 1: Test suite bootstrap

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
    // ... 40+ test ...

    KNST_LOG_INFO("=== Tests complete ===");
}
// ← session destructor otomatik shutdown + finish
```

### Örnek 2: Manuel init (native_app_glue yok)

```cpp
// JNI entry point — native_app_glue kullanmıyorsanız
extern "C" JNIEXPORT void JNICALL
Java_com_example_MainActivity_onCreate(JNIEnv* env, jobject, jobject activity) {
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    knst_android::context::instance().init(vm, activity);

    // ... uygulama başlar ...
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_MainActivity_onDestroy(JNIEnv*, jobject) {
    knst_android::context::instance().shutdown();
}
```

### Örnek 3: Native thread'den JNI

```cpp
void my_worker_thread() {
    // env() → attach (ilk sefer) veya mevcut
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

    env->DeleteLocalRef(helper);   // ← local ref sil
}
```

### Örnek 4: Rotation handler (custom)

```cpp
// Kullanıcı kendi onAppCmd'sini eklemek istiyor
void my_on_app_cmd(struct android_app* app, int32_t cmd) {
    if (cmd == APP_CMD_GAINED_FOCUS) {
        KNST_LOG_INFO("Gained focus");
    }
}

void android_main(struct android_app* app) {
    knst_android::session session(app);

    // Session kendi handler'ını koydu — bizimki chain'lenir
    // Ama session construct SONRASI override etmek session'ı bozar:
    // app->onAppCmd = my_on_app_cmd;   // ❌ rotation handling kaybolur

    // ✅ Doğru: session'ın static handler'ı zaten chain'liyor,
    // sadece state callback için ayrı bir yol kullanın veya
    // session'ı yok sayıp manuel context kullanın
}
```

### Örnek 5: Manuel rotation handling

```cpp
// session kullanmıyorsanız
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

## 📌 8) API Kontrat Özeti

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

## 🎯 Size Getireceği Kazanç

`knst_android_context` şu durumlarda ciddi avantaj sağlar:

- ✅ **Tek init** — 3 modül ayrı ayrı `set_*_context` çağırmaz
- ✅ **RAII session** — Construct = init, destruct = shutdown + finish
- ✅ **Global ref yönetimi** — Activity + ClassLoader otomatik
- ✅ **ClassLoader cache** — Native thread'den app class erişimi
- ✅ **Rotation otomatik** — `onAppCmd` chain, kullanıcı handler korunur
- ✅ **Idempotent init** — `IsSameObject` ile fast-path
- ✅ **Thread-safe okuma** — Atomic accessor'lar
- ✅ **Attach guard** — `shutdown` sadece kendi attach'ımızı detach eder
- ✅ **`nullptr` safety** — Her public API savunmacı
- ✅ **Java helper auto-bind** — Print helper + ML Kit scanner
- ✅ **Missing helper warning** — Crash yok, graceful skip
- ✅ **`ANativeActivity_finish`** — OS'e "kapat" sinyali (battery drain önler)
- ✅ **`__has_include` guard** — 3 farklı `native_app_glue` yolu
- ✅ **KernelNucleusT entegrasyonu** — `configure_modules` fanout

---