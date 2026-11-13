# knst_thread — Move-Only Thread Wrapper

Selamlar! 👋 Bu doküman `knst_thread` sınıfının ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **Move-only, `std::thread`'den daha yetenekli bir thread wrapper'ı.** Priority ayarı, callback desteği, timeout'lu join, kill, detach ve **thread pool entegrasyonu** sunar. `std::thread`'in yapamadığı her şeyi yapar.

---

## 🎯 Neden Bu Sınıf Var?

`std::thread` C++ standardının bir parçası ama bazı eksikleri var:

- **Priority ayarı yok** — thread önceliği atayamazsın
- **Timeout'lu join yok** — `join()` sonsuza kadar bekler
- **Callback desteği yok** — task bitince bir şey çalıştıramazsın
- **Kill yok** — zorla durduramazsın
- **State sorgulama yok** — "çalışıyor mu, bitti mi?" belli değil
- **Pool entegrasyonu yok** — her seferinde yeni OS thread'i

`knst_thread` bunların hepsini çözer:

- ✅ **Priority** — `knst_thread_priority` enum'ı ile 7 seviye
- ✅ **Timeout'lu join** — `join_for(ms)`, `try_join()`
- ✅ **Callback** — task bitince otomatik çalışır
- ✅ **Kill** — son çare olarak zorla durdurma
- ✅ **State sorgulama** — `running()`, `finished()`, `joinable()`
- ✅ **Pool desteği** — istersen pool'a gönder, istersen yeni thread aç
- ✅ **Move-only** — sahiplik net, kopyalama yok
- ✅ **`shared_ptr` paylaşımı** — thread + wrapper aynı veriyi tutar

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Move-only** | Kopyalama yok, sahiplik transferi net |
| **Priority** | 7 seviye: `inherit`, `lowest`, `low`, `normal`, `high`, `highest`, `time_critical` |
| **Callback** | Task bitince otomatik çağrılır |
| **Timeout'lu join** | `join_for(ms)` ve `try_join()` |
| **Kill** | Son çare zorla durdurma |
| **Detach** | Arka planda çalışsın, bekleme |
| **State sorgulama** | `running()`, `finished()`, `joinable()`, `detached()` |
| **Pool entegrasyonu** | `start(pool, fn)` ile pool'a gönder |
| **Self-cleanup** | `self_ref` sayesinde thread kendini temizler |
| **Exception-safe** | Task exception fırlatsa bile thread temiz kapanır |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    knst_thread t;

    // Thread başlat
    t.start([]() {
        std::cout << "Merhaba, thread'den!\n";
    });

    // Bekle
    t.join();
    return 0;
}
```

---

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Başlatma** | `start(fn)`, `start(fn, cb)`, `start_with_priority(prio, fn)`, `start(pool, fn)` |
| **Bekleme** | `join()`, `join_for(ms)`, `try_join()` |
| **Kontrol** | `kill()`, `detach()` |
| **State** | `state()`, `running()`, `finished()`, `joinable()`, `detached()`, `is_pooled()` |
| **Priority** | `set_priority(prio)`, `priority()`, `achieved_priority()`, `native_priority()` |

---

## 🎯 1) Boş Durum

Default-constructed `knst_thread` hiçbir thread tutmaz:

```cpp
knst_thread t;

std::cout << "state    : " << (int)t.state() << "\n";   // 0 (idle)
std::cout << "joinable : " << t.joinable() << "\n";     // false
std::cout << "running  : " << t.running()  << "\n";     // false
std::cout << "finished : " << t.finished() << "\n";     // false
```

**State enum:**

```cpp
enum class knst_thread_state : uint8_t {
    idle = 0,       // boş
    running = 1,    // çalışıyor
    finished = 2,   // bitti, join bekliyor
    joined = 3,     // join edildi
    detached = 4    // ayrıldı
};
```

---

## 🚀 2) Thread Başlatma

### `start(fn)` — Basit Başlatma

```cpp
knst_thread t;

bool ok = t.start([]() {
    std::cout << "worker thread çalışıyor\n";
});

if (!ok) {
    std::cerr << "Thread başlatılamadı!\n";
}

t.join();   // bekle
```

**Dönüş:** `true` = başarılı, `false` = başarısız (zaten çalışıyor veya OS hatası)

### `start(fn, cb)` — Callback'li Başlatma

Task bittiğinde callback otomatik çağrılır:

```cpp
knst_thread t;

t.start(
    []() { std::cout << "[task] çalışıyor\n"; },
    []() { std::cout << "[callback] bitti\n"; }
);

t.join();
```

**Çıktı:**
```
[task] çalışıyor
[callback] bitti
```

**Kullanım alanı:** Loglama, cleanup, sonuç işleme

### `start_with_priority(prio, fn)` — Priority'li Başlatma

```cpp
knst_thread t;

t.start_with_priority(knst_thread_priority::high, []() {
    // yüksek öncelikli iş
});

t.join();
```

### `start(pool, fn)` — Pool'a Gönderim

```cpp
knst_thread_pool pool;

knst_thread t;
t.start(pool, []() {
    // pool'da çalışacak
});
```

**Detaylar için:** `knst_thread_pool` dokümanı

---

## 🎯 3) Priority Sistemi

### `knst_thread_priority` Enum

```cpp
enum class knst_thread_priority : int8_t {
    inherit       = -128,   // parent'ın önceliğini al
    lowest        = -2,     // en düşük
    low           = -1,     // düşük
    normal        = 0,      // normal (varsayılan)
    high          = 1,      // yüksek
    highest       = 2,      // en yüksek
    time_critical = 3       // kritik (dikkatli kullan!)
};
```

### Priority Ayarlama

```cpp
knst_thread t;

t.start_with_priority(knst_thread_priority::high, []() {
    // ...
});

// İstenen priority
std::cout << "İstenen : " << (int)t.priority() << "\n";   // 1 (high)

t.join();

// Gerçekte elde edilen priority
std::cout << "Elde edilen : " << (int)t.achieved_priority() << "\n";
```

**Neden iki farklı priority var?**

- `priority()` → senin istediğin
- `achieved_priority()` → OS'un verdiği

OS bazen istediğin seviyeyi reddeder (izin sorunu). Bu durumda **daha düşük bir seviyeye düşer** ve `achieved_priority()` bunu yansıtır.

### Sonradan Priority Değiştirme

```cpp
knst_thread t;
t.start([]() { /* ... */ });

// Çalışırken priority değiştir
t.set_priority(knst_thread_priority::low);

t.join();
```

**Ladder mekanizması:** İstediğin seviye reddedilirse, bir alt seviye denenir. En kötü ihtimalle `lowest`'a kadar iner.

### Native Priority

```cpp
// OS'dan doğrudan oku (kendin istemedin bile)
knst_thread_priority native = t.native_priority();
```

**Platform farkları:**

| Priority | Windows | POSIX (nice) |
|---|---|---|
| `time_critical` | `THREAD_PRIORITY_TIME_CRITICAL` | -20 |
| `highest` | `THREAD_PRIORITY_HIGHEST` | -15 |
| `high` | `THREAD_PRIORITY_ABOVE_NORMAL` | -10 |
| `normal` | `THREAD_PRIORITY_NORMAL` | 0 |
| `low` | `THREAD_PRIORITY_BELOW_NORMAL` | 10 |
| `lowest` | `THREAD_PRIORITY_LOWEST` | 19 |
| `inherit` | Değiştirmez | Değiştirmez |

**Not:** POSIX'te priority düşürmek (nice artırmak) izinsiz mümkün, ama priority yükseltmek için `CAP_SYS_NICE` gerekir.

---

## ⏱️ 4) Bekleme Metodları

### `join()` — Sonsuz Bekleme

```cpp
knst_thread t;
t.start([]() { /* ... */ });

t.join();   // thread bitene kadar bekler
```

**Dönüş:** `true` = başarılı, `false` = zaten join edilmiş/detached/idle

### `join_for(ms)` — Timeout'lu Bekleme

```cpp
knst_thread t;
t.start([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
});

bool done = t.join_for(50);   // 50 ms bekle

if (!done) {
    std::cout << "Hala çalışıyor\n";
    t.join_for(1000);   // Bir 1000 ms daha bekle
}
```

**Kullanım alanı:** UI thread'i bloklamadan bekleme, timeout kontrolü

### `try_join()` — Beklemeden Kontrol

```cpp
knst_thread t;
t.start([]() { /* ... */ });

// Anında dön — bekletmez
if (t.try_join()) {
    std::cout << "Bitti, join edildi\n";
} else {
    std::cout << "Hala çalışıyor\n";
}
```

**Ne yapar?**
1. Mutex'i **non-blocking** kilitlemeye çalışır
2. Kilit alınamazsa → false (thread meşgul)
3. `finished_flag` kontrol eder
4. Bittiyse → `join()` çağırır, true döner
5. Bitmediyse → false

---

## 🔪 5) Kill ve Detach

### `kill()` — Son Çare

Thread'i **zorla** durdurur. **Çok dikkatli kullan!**

```cpp
knst_thread t;
t.start([]() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
});

std::this_thread::sleep_for(std::chrono::milliseconds(50));

bool ok = t.kill();
std::cout << "kill: " << (ok ? "başarılı" : "başarısız") << "\n";
```

**Platform davranışı:**

| Platform | Ne yapar? |
|---|---|
| **Windows** | `TerminateThread()` — thread anında öldürülür |
| **POSIX** | `pthread_cancel()` — iptal noktalarında durur |

**⚠️ Uyarılar:**
- **Mutex'ler kilitli kalabilir** → deadlock riski
- **Kaynaklar sızabilir** → dosya handle'ları vs.
- **`volatile` veri bozulabilir**
- **Pooled thread'lerde çalışmaz** (`is_pooled() == true` ise `false` döner)

**Ne zaman kullan?** Sadece graceful shutdown imkansızsa.

### `detach()` — Arka Planda Bırakma

Thread'i kendi haline bırakır. Bitince OS otomatik temizler.

```cpp
knst_thread t;
t.start([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Arka planda bitti\n";
});

t.detach();

// Beklemek zorunda değilsin
std::cout << "Main devam ediyor\n";

// Ama main çıkmadan önce detached thread bitmeli
std::this_thread::sleep_for(std::chrono::milliseconds(200));
```

**Ne zaman kullan?** Fire-and-forget görevler için.

**⚠️ Uyarı:** Main thread çıkarsa detached thread de ölür. Program sonlanmadan önce bitmesini bekle.

---

## 🔍 6) State Sorgulama

### Mevcut State

```cpp
knst_thread t;
std::cout << (int)t.state() << "\n";   // 0 (idle)

t.start([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
std::cout << (int)t.state() << "\n";   // 1 (running)

t.join();
std::cout << (int)t.state() << "\n";   // 3 (joined)
```

### Pratik Sorgular

```cpp
knst_thread t;

// Başlamadan önce
t.joinable();   // false
t.running();    // false
t.finished();   // false

// Başladıktan sonra
t.start([]() { /* ... */ });
t.joinable();   // true (running veya finished)
t.running();    // true
t.finished();   // false

// Bittikten sonra
t.join();
t.joinable();   // false
t.running();    // false
t.finished();   // true
t.state();      // joined
```

### Tüm Metodlar

| Metod | Ne döner? |
|---|---|
| `state()` | `knst_thread_state` enum değeri |
| `running()` | true: çalışıyor |
| `finished()` | true: bitti veya joined |
| `joinable()` | true: running veya finished |
| `detached()` | true: ayrıldı |
| `is_pooled()` | true: pool'a ait |

---

## 📦 7) Move Semantics

`knst_thread` **move-only**. Kopyalama yok, sahiplik transferi var.

### Move Constructor

```cpp
knst_thread a;
a.start([]() { /* ... */ });

knst_thread b = std::move(a);   // Sahiplik b'ye geçti

std::cout << a.joinable() << "\n";   // false (boş)
std::cout << b.joinable() << "\n";   // true

b.join();
```

### Move Assignment

```cpp
knst_thread a;
a.start([]() { /* ... */ });

knst_thread b;
b = std::move(a);   // a boşalır, b sahiplenir

b.join();
```

### Neden Copy Yasak?

- **Sahiplik net olsun** — kim join edecek?
- **Kaynak sızmasın** — bir shared_ptr, iki sahip olmaz
- **Thread safety** — race condition riski yok

### Ne Zaman Move Kullan?

```cpp
// ✅ Fonksiyona geçir
void take_thread(knst_thread t) {
    t.join();
}

knst_thread t;
t.start([]() { /* ... */ });
take_thread(std::move(t));

// ✅ Vector'a ekle
knst_vector<knst_thread> threads;
for (int i = 0; i < 10; ++i) {
    knst_thread t;
    t.start([]() { /* ... */ });
    threads.push_back(std::move(t));
}
```

---

## 🎁 8) Callback Sistemi

### Basit Callback

```cpp
knst_thread t;

t.start(
    []() { std::cout << "İş yapılıyor\n"; },
    []() { std::cout << "İş bitti\n"; }
);

t.join();
```

**Çıktı:**
```
İş yapılıyor
İş bitti
```

### Callback ile Cleanup

```cpp
std::atomic<bool> finished{false};

knst_thread t;
t.start(
    []() {
        // Ana iş
    },
    [&finished]() {
        finished.store(true);
        // temizlik yap
    }
);
```

### Callback + Priority

```cpp
knst_thread t;

t.start_with_priority(
    knst_thread_priority::high,
    []() { /* task */ },
    []() { /* callback */ }
);
```

---

## 🧠 9) Shared State

Thread'ler arasında veri paylaşımı **senin sorumluluğunda**. `knst_thread` senkronizasyon sağlamaz.

### Atomic ile Güvenli Paylaşım

```cpp
std::atomic<int> counter{0};

knst_thread t;
t.start([&counter]() {
    for (int i = 0; i < 100000; ++i) {
        counter.fetch_add(1);
    }
});
t.join();

std::cout << "Counter: " << counter.load() << "\n";   // 100000
```

### Mutex ile Güvenli Paylaşım

```cpp
std::mutex mtx;
knst_vector<int> shared_data;

knst_thread t;
t.start([&]() {
    for (int i = 0; i < 100; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        shared_data.push_back(i);
    }
});
t.join();
```

### ❌ Yanlış Kullanım

```cpp
int counter = 0;   // atomic değil!

knst_thread t;
t.start([&counter]() {
    for (int i = 0; i < 100000; ++i) {
        ++counter;   // ❌ race condition!
    }
});
t.join();

// Sonuç 100000 değil, rastgele bir sayı
```

---

## 🔧 10) `knst_thread_data` Yapısı

Thread ile wrapper'ın paylaştığı veri:

```cpp
struct knst_thread_data {
    std::mutex mtx;                             // senkronizasyon
    std::condition_variable cv;                 // bitti sinyali
    bool finished_flag = false;                 // bitti mi?
    std::atomic<uint8_t> state{0};              // durum
    knst_function task;                         // çalıştırılacak iş
    bool is_pooled = false;                     // pool'a mı ait?

    std::atomic<int8_t> requested_priority;     // istenen
    std::atomic<int8_t> achieved_priority;      // elde edilen

    std::shared_ptr<knst_thread_data> self_ref; // kendine referans

    // Platform-spesifik
    #if KNST_USING_PLATFORM_WINDOWS
        HANDLE handle = nullptr;
    #else
        pthread_t handle{};
        bool handle_valid = false;
        std::atomic<int32_t> native_tid{-1};
    #endif
};
```

### `self_ref` Ne İşe Yarar?

Thread çalışırken, `knst_thread` objesi yok edilebilir. `self_ref` sayesinde `knst_thread_data` **thread çalışırken hayatta kalır**:

```cpp
{
    knst_thread t;
    t.start([]() { /* ... */ });
    // t scope'tan çıkar, destructor çağrılır
    // AMA: thread hala çalışıyor
    // self_ref sayesinde veri hayatta kalır
}
```

Thread bitince `self_ref.reset()` çağrılır ve veri otomatik temizlenir.

---

## ⚙️ 11) Pool Entegrasyonu

### Pool'a Gönderim

```cpp
knst_thread_pool pool;

knst_thread t;
t.start(pool, []() {
    // pool içinde çalışacak
});
```

**Avantaj:** Yeni OS thread'i oluşturmaz, mevcut bir worker'ı kullanır.

### Pool'da Priority

```cpp
t.start_with_priority(pool, knst_thread_priority::high, []() {
    // ...
});
```

**Not:** Pool içinde priority **sadece bilgi amaçlıdır** — OS thread'i değişmez.

### Pool'da Callback

```cpp
t.start(
    pool,
    []() { /* task */ },
    []() { /* callback */ }
);
```

### Pooled vs Standalone

```cpp
knst_thread t;
t.start(pool, []() { /* ... */ });

std::cout << "Pooled: " << t.is_pooled() << "\n";   // true
```

**Pooled thread'lerde:**
- `kill()` **çalışmaz** (pool'u bozabilir)
- `detach()` **çalışmaz**
- `join()` çalışır (pool'a "bitti" sinyali)

---

## 🌍 12) Platform Farkları

| Konu | Windows | POSIX |
|---|---|---|
| **Thread oluşturma** | `CreateThread` | `pthread_create` |
| **Priority** | `SetThreadPriority` | `setpriority` / `nice` |
| **Kill** | `TerminateThread` (anında) | `pthread_cancel` (iptal noktasında) |
| **Detach** | `CloseHandle` | `pthread_detach` |
| **Join** | `WaitForSingleObject` | `pthread_join` |
| **Native TID** | `GetCurrentThreadId` | `gettid()` (Linux) |
| **Priority izni** | Genelde ok | `CAP_SYS_NICE` gerekir (yükseltmek için) |

### Exception Handling Farkı (POSIX)

POSIX'te `pthread_cancel` çağrıldığında, thread `abi::__forced_unwind` exception fırlatır. `knst_thread` bunu **özel olarak yakalar** ve `rethrow` eder (aksi halde undefined behavior):

```cpp
try {
    self->task();
}
#if defined(__GLIBCXX__) || defined(__GLIBC__)
catch (abi::__forced_unwind&) {
    throw;   // rethrow — pthread_cancel bunu bekler
}
#endif
catch (...) {
    // diğer exception'lar yutulur
}
```

---

## 🔥 13) Gerçek Kullanım Örnekleri

### Örnek 1: Paralel İşleme

```cpp
knst_vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
std::atomic<int> sum{0};

knst_vector<knst_thread> threads;

for (int value : data) {
    knst_thread t;
    t.start([&sum, value]() {
        sum.fetch_add(value * value);
    });
    threads.push_back(std::move(t));
}

// Hepsini bekle
for (auto& t : threads) {
    t.join();
}

std::cout << "Sum of squares: " << sum.load() << "\n";   // 204
```

### Örnek 2: Timeout'lu İş

```cpp
bool run_with_timeout(knst_function task, uint32_t ms) {
    knst_thread t;
    t.start(std::move(task));

    if (!t.join_for(ms)) {
        std::cerr << "Timeout, kill ediliyor\n";
        t.kill();
        return false;
    }
    return true;
}

run_with_timeout([]() {
    std::this_thread::sleep_for(std::chrono::seconds(10));
}, 500);   // 500 ms sonra kill
```

### Örnek 3: Background Worker

```cpp
class BackgroundWorker {
    knst_thread m_thread;
    std::atomic<bool> m_stop{false};

public:
    void start() {
        m_thread.start([this]() {
            while (!m_stop.load()) {
                // iş yap
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    void stop() {
        m_stop.store(true);
        m_thread.join_for(1000);   // 1 saniye bekle
        if (m_thread.running()) {
            m_thread.kill();       // zorla durdur
        }
    }
};
```

### Örnek 4: Callback ile Pipeline

```cpp
void process_data(knst_vector<int>& data) {
    std::atomic<int> processed{0};

    knst_vector<knst_thread> threads;

    for (int value : data) {
        knst_thread t;

        t.start(
            [value]() {
                // Ağır iş
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            },
            [&processed]() {
                processed.fetch_add(1);
                std::cout << "İşlenen: " << processed.load() << "\n";
            }
        );

        threads.push_back(std::move(t));
    }

    for (auto& t : threads) t.join();
}
```

### Örnek 5: Priority ile Kritik İş

```cpp
void run_critical_task() {
    knst_thread t;

    t.start_with_priority(
        knst_thread_priority::highest,
        []() {
            // Gerçek zamanlı iş
            auto start = std::chrono::steady_clock::now();
            // ... iş ...
            auto end = std::chrono::steady_clock::now();
        }
    );

    // Priority gerçekten atandı mı?
    if (t.achieved_priority() != knst_thread_priority::highest) {
        std::cerr << "Uyarı: İstenen priority elde edilemedi\n";
    }

    t.join();
}
```

### Örnek 6: Detached Logger

```cpp
void log_async(const knst_c16string& message) {
    knst_thread t;
    t.start([message]() {
        // Dosyaya yaz (yavaş olabilir)
        knst_file::append_file_text(u"app.log", message + u"\n");
    });
    t.detach();   // Ana thread beklemez
}

int main() {
    log_async(u"Uygulama başladı");
    log_async(u"Veri yüklendi");
    log_async(u"İşlem tamamlandı");

    // Main çıkmadan önce logger'ların bitmesini bekle
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
```

### Örnek 7: Thread Pool Karşılaştırma

```cpp
void compare_approaches() {
    // Yaklaşım 1: Her iş için yeni thread (yavaş)
    auto start = std::chrono::steady_clock::now();

    knst_vector<knst_thread> threads;
    for (int i = 0; i < 1000; ++i) {
        knst_thread t;
        t.start([]() { /* hafif iş */ });
        threads.push_back(std::move(t));
    }
    for (auto& t : threads) t.join();

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Yeni thread: " << duration.count() << " ms\n";

    // Yaklaşım 2: Pool (hızlı)
    knst_thread_pool pool;
    start = std::chrono::steady_clock::now();

    for (int i = 0; i < 1000; ++i) {
        knst_thread t;
        t.start(pool, []() { /* hafif iş */ });
    }
    pool.wait_all();

    end = std::chrono::steady_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Pool: " << duration.count() << " ms\n";
}
```

**Tipik sonuç:** Pool **10-50x daha hızlı** (1000 OS thread'i açmak yerine 8-16 worker kullanır).

---

## 📊 14) Performans Notları

| İşlem | Karmaşıklık | Not |
|---|---|---|
| `start()` | **O(1)** + OS maliyeti | Yeni thread açmak ~10-100 μs |
| `start(pool, ...)` | **O(1)** | Mevcut worker'a queue — çok hızlı |
| `join()` | **O(1)** | Bitene kadar bekler |
| `join_for(ms)` | **O(1)** | Timeout'lu bekleme |
| `try_join()` | **O(1)** | Non-blocking kontrol |
| `detach()` | **O(1)** | Handle bırakır |
| `kill()` | **O(1)** + OS | Tehlikeli ama hızlı |
| `set_priority()` | **O(1)** | OS syscall |

### Ne Zaman Yavaş?

- **`start()` çağrısı 10.000 kez** — OS thread maliyeti (pool kullan!)
- **`kill()` çağrısı** — sadece son çare
- **Sık `join_for()` kontrolü** — busy-wait yerine event-driven tasarım

### Optimizasyon İpuçları

```cpp
// ❌ Yavaş: Her iş için yeni thread
for (int i = 0; i < 10000; ++i) {
    knst_thread t;
    t.start([]() { /* ... */ });
    t.join();
}

// ✅ Hızlı: Pool + batch
knst_thread_pool pool;
for (int i = 0; i < 10000; ++i) {
    knst_thread t;
    t.start(pool, []() { /* ... */ });
}
pool.wait_all();
```

---

## 🛡️ 15) Exception Safety

### Task İçindeki Exception

Task exception fırlatırsa, `knst_thread` **yutar** ve normal kapanır:

```cpp
knst_thread t;
t.start([]() {
    throw std::runtime_error("hata!");
});

t.join();   // ✅ Sorunsuz — exception içeride yakalandı
```

**Ama exception'ı göremezsin!** Bunun için `std::exception_ptr` kullan:

```cpp
std::exception_ptr err;

knst_thread t;
t.start([&err]() {
    try {
        // riskli iş
        throw std::runtime_error("hata");
    } catch (...) {
        err = std::current_exception();
    }
});
t.join();

if (err) {
    try { std::rethrow_exception(err); }
    catch (const std::exception& e) {
        std::cerr << "Hata: " << e.what() << "\n";
    }
}
```

### Destructor Davranışı

`knst_thread` destructor'ı **akıllı**:

- **Running** → otomatik detach
- **Finished** → otomatik join
- **Idle** → hiçbir şey
- **Pooled** → OS handle'a dokunmaz

```cpp
{
    knst_thread t;
    t.start([]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    });
    // scope'tan çıkarken: t destructor'ı otomatik detach eder
    // thread arka planda çalışmaya devam eder
}
```

**Yani:** Bellek sızması veya zombie thread olmaz.

---

## 🎯 Size Getireceği Kazanç

`knst_thread` şu durumlarda ciddi avantaj sağlar:

- ✅ **Priority kontrolü** gereken işler (media, RT)
- ✅ **Timeout'lu işlem** — `join_for()`
- ✅ **Callback'li task** — pipeline, logging
- ✅ **Thread pool** kullanıyorsan — `start(pool, fn)`
- ✅ **State sorgulama** — "çalışıyor mu?" sık soruyorsan
- ✅ **Move semantics** — vector'a thread eklemek

