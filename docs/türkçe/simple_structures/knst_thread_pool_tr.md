# knst_thread_pool — Worker-Thread Pool

Selamlar! 👋 Bu doküman `knst_thread_pool` sınıfının ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **Thread oluşturma maliyetini amortize eden bir worker-thread havuzu.** Görevleri kuyruğa alır, worker'lar tarafından işlenir. Kuyruk dolduğunda **otomatik olarak overflow thread'ler** açar (32'ye kadar) ve iş yükü azalınca bunları kapatır. **Priority desteği** hem worker'lar hem de görev bazında mevcuttur.

---

## 🎯 Neden Bu Sınıf Var?

Her görev için yeni bir `std::thread` açmak **pahalıdır** — fork/exec kadar olmasa da OS kaynakları tüketir:

- **10.000 küçük görev** için 10.000 OS thread'i açmak **saniyeler** alır
- Her thread ~1 MB stack alanı tüketir
- Context switch maliyeti katlanır

`knst_thread_pool` bunu çözer:

- ✅ **8-16 worker** açılır bir kez, hepsi kuyruktan görev çeker
- ✅ **Thread reuse** — OS thread maliyeti amortize edilir
- ✅ **Priority** — worker'ların base priority'si ve görev bazında geçici priority
- ✅ **Overflow desteği** — kuyruk şişerse geçici thread'ler açar
- ✅ **Graceful/Force shutdown** — kuyruktaki görevleri tamamla veya iptal et
- ✅ **`knst_thread` entegrasyonu** — `submit()` bir `knst_thread` döner

**Kazanç:** 1000 görev için **10-50x** hızlanma.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Worker reuse** | Thread oluşturma maliyeti amortize edilir |
| **Batch pulling** | Her worker kuyruktan 8 görev çeker (kilit maliyeti düşer) |
| **Overflow threads** | Kuyruk şişerse geçici yardımcı thread'ler (max 32) |
| **Priority** | Worker base priority + görev bazında geçici priority |
| **Graceful shutdown** | Kuyruktaki görevler tamamlanır |
| **Force shutdown** | Kuyruktaki görevler iptal edilir |
| **`wait_all`** | Tüm görevler bitene kadar bekle |
| **`wait_all_for`** | Timeout'lu bekleme |
| **Introspection** | `pending_count`, `active_count`, `queue_size`, `overflow_count` |
| **`knst_thread` uyumu** | `submit()` joinable thread döner |
| **Auto worker count** | `hardware_concurrency()` bazlı otomatik |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Otomatik worker sayısıyla pool oluştur
    knst_thread_pool pool;
    pool.start();

    // Görev gönder
    pool.submit([]() {
        std::cout << "Merhaba, pool'dan!\n";
    });

    // Bitmesini bekle
    pool.wait_all();

    pool.shutdown();
    return 0;
}
```

---

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Yaşam döngüsü** | `start`, `shutdown`, `is_running` |
| **Görev gönderme** | `submit`, `submit_with_priority` |
| **Bekleme** | `wait_all`, `wait_all_for` |
| **Introspection** | `worker_count`, `pending_count`, `queue_size`, `active_count`, `overflow_count` |
| **Priority** | `set_worker_priority`, `worker_priority`, `worker_priority_achieved` |

---

## 🏗️ 1) Pool Oluşturma

### Otomatik Worker Sayısı

```cpp
knst_thread_pool pool;   // hardware_concurrency() kullanır
pool.start();
```

**Varsayılan davranış:**
- `hardware_concurrency()` → CPU çekirdek sayısı
- 0 dönerse (bilinmiyorsa) → 3

### Manuel Worker Sayısı

```cpp
knst_thread_pool pool(4);   // 4 worker
pool.start();
```

### Priority ile

```cpp
knst_thread_pool pool(4, knst_thread_priority::high);
pool.start();
```

### Başlatma ve Kapatma

```cpp
knst_thread_pool pool;

// Başlat
bool ok = pool.start();
if (!ok) {
    std::cerr << "Pool başlatılamadı!\n";
}

// Çalışıyor mu?
if (pool.is_running()) {
    // ...
}

// Kapat
pool.shutdown();
```

**Not:** Destructor `shutdown()` çağırır — manuel çağırmasan bile sızıntı olmaz.

---

## 🚀 2) Görev Gönderme

### `submit(fn)` — Basit Gönderim

```cpp
knst_thread_pool pool(2);
pool.start();

knst_thread t = pool.submit([]() {
    std::cout << "Görev çalışıyor\n";
});

t.join();   // Bitmesini bekle (opsiyonel)

pool.shutdown();
```

**Dönüş:** `knst_thread` — joinable, `join()`, `join_for()`, `finished()` gibi metodlar mevcut.

### `submit(fn, cb)` — Callback ile

```cpp
knst_thread t = pool.submit(
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

### `submit_with_priority(prio, fn)` — Priority ile

```cpp
knst_thread t = pool.submit_with_priority(
    knst_thread_priority::high,
    []() {
        // yüksek öncelikli görev
    }
);

t.join();

// İstenen priority
std::cout << "İstenen  : " << (int)t.priority() << "\n";       // 1 (high)

// Elde edilen (worker geçici olarak bu priority'yi aldı)
std::cout << "Elde edilen: " << (int)t.achieved_priority() << "\n";
```

**Nasıl çalışır?**
1. Worker base priority'sini geçici olarak `high` yapar
2. Görevi çalıştırır
3. Bitince base priority'ye döner

### `submit_with_priority(prio, fn, cb)` — Priority + Callback

```cpp
knst_thread t = pool.submit_with_priority(
    knst_thread_priority::low,
    []() { /* task */ },
    []() { /* callback */ }
);

t.join();
```

---

## ⏱️ 3) Bekleme Metodları

### `wait_all()` — Sonsuz Bekleme

Tüm görevler bitene kadar bekler:

```cpp
knst_thread_pool pool(4);
pool.start();

for (int i = 0; i < 100; ++i) {
    pool.submit([i]() {
        // iş yap
    });
}

pool.wait_all();   // 100 görev de bitene kadar bekler
pool.shutdown();
```

**Ne zaman kullan?** Batch işlem bitince bir şey yapmak istiyorsan.

### `wait_all_for(ms)` — Timeout'lu Bekleme

```cpp
for (int i = 0; i < 100; ++i) {
    pool.submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
}

// 20 ms bekle
if (pool.wait_all_for(20)) {
    std::cout << "Bitti\n";
} else {
    std::cout << "Hala çalışıyor\n";
}

// 5000 ms daha bekle
pool.wait_all_for(5000);
```

**Kullanım alanı:** UI thread'i bloklamadan bekleme, shutdown kontrolü.

### `knst_thread::join()` — Tek Görev için

```cpp
knst_thread t = pool.submit([]() { /* ... */ });
t.join();      // Bu görev özelinde bekle
```

---

## 🔍 4) Introspection

### `worker_count()` — Worker Sayısı

```cpp
std::cout << "Workers: " << pool.worker_count() << "\n";
// Örn: 8
```

### `pending_count()` — Kuyruk + Çalışan

```cpp
std::cout << "Pending: " << pool.pending_count() << "\n";
// Kuyrukta bekleyen + şu anda çalışan
```

### `queue_size()` — Kuyrukta Bekleyen

```cpp
std::cout << "Queued: " << pool.queue_size() << "\n";
// Henüz bir worker tarafından alınmamış
```

### `active_count()` — Çalışan Görev

```cpp
std::cout << "Active: " << pool.active_count() << "\n";
// pending_count() - queue_size()
```

### `overflow_count()` — Aktif Overflow Thread

```cpp
std::cout << "Overflow: " << pool.overflow_count() << "\n";
// Kuyruk şiştiğinde açılan geçici thread'ler
```

### İlişki

```
pending_count() = queue_size() + active_count() + overflow_working

Örnek:
  pending = 100
  queue   = 60 (bekleyen)
  active  = 40 (worker'lar + overflow thread'ler tarafından işlenen)
```

---

## 🎛️ 5) Priority Sistemi

### Worker Base Priority

Tüm worker'ların başlangıç priority'si:

```cpp
knst_thread_pool pool(4, knst_thread_priority::high);

pool.start();

// İstenen
std::cout << "İstenen  : " << (int)pool.worker_priority() << "\n";           // 1

// OS'un verdiği (reddedilebilir)
std::cout << "Elde edilen: " << (int)pool.worker_priority_achieved() << "\n";
```

### Dinamik Değiştirme

```cpp
pool.set_worker_priority(knst_thread_priority::low);
// Yeni worker'lar ve sonraki görevler için geçerli
```

### Görev Bazında Priority

`submit_with_priority()` kullan:

```cpp
// Bu görev yüksek priority ile çalışsın
pool.submit_with_priority(knst_thread_priority::highest, []() {
    // kritik iş
});

// Bu görev düşük priority ile çalışsın
pool.submit_with_priority(knst_thread_priority::lowest, []() {
    // arka plan iş
});
```

**Nasıl çalışır?**
1. Worker base priority'sini saklar
2. Görev priority'sini uygular
3. Görevi çalıştırır
4. **Base priority'ye döner**

**Yani:** Her görev kendi priority'siyle çalışır, bir sonrakini etkilemez.

### Priority Enum

```cpp
enum class knst_thread_priority : int8_t {
    inherit       = -128,
    lowest        = -2,
    low           = -1,
    normal        = 0,
    high          = 1,
    highest       = 2,
    time_critical = 3
};
```

---

## 🌊 6) Overflow Mekanizması

### Ne Zaman Overflow Açılır?

Worker sayısı = 4, kuyruk = 10 görev:

```
Zaman 0: 4 worker + 10 görev
         → 4 worker 4 görevi alır
         → 6 görev kuyrukta bekler
         → Kuyruk worker sayısından büyük (6 > 4)
         → Overflow thread açılır!
```

### Nasıl Çalışır?

```cpp
// Kuyruk worker sayısını aşarsa
if (m_queue.size() > m_workers.size() &&
    m_overflow_count < KNST_OVERFLOW_MAX) {
    spawn_overflow();   // Geçici thread açar
}
```

**Overflow thread:**
1. Priority'yi uygular
2. Kuyruktan **bir** görev alır
3. Çalıştırır
4. Kendini yok eder

**Limit:** `KNST_OVERFLOW_MAX` (varsayılan 32)

### Ne Zaman Kapanır?

Overflow thread'ler **tek görev** için yaşar. Görev bitince `std::thread` sona erer ve sayaç düşer.

### Örnek Senaryo

```cpp
knst_thread_pool pool(1);   // 1 worker!
pool.start();

// 200 görev gönder
for (int i = 0; i < 200; ++i) {
    pool.submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
}

// Anında:
// - 1 worker çalışıyor
// - 32 overflow thread açıldı (limit)
// - ~167 görev kuyrukta bekliyor
std::cout << "Active  : " << pool.active_count()  << "\n";   // ~33
std::cout << "Queued  : " << pool.queue_size()    << "\n";   // ~167
std::cout << "Overflow: " << pool.overflow_count() << "\n";   // ~32
```

**Yani:** 1 worker olsa bile 200 görev için **33 paralel iş** yapılıyor.

---

## 🔌 7) Shutdown Modları

### Graceful Shutdown (Varsayılan)

Kuyruktaki tüm görevler tamamlanır, sonra worker'lar kapanır:

```cpp
knst_thread_pool pool(2);
pool.start();

for (int i = 0; i < 100; ++i) {
    pool.submit([]() { /* iş */ });
}

pool.shutdown(knst_shutdown::graceful);   // veya sadece shutdown()

// 100 görev de tamamlanmış olur
```

**Ne yapar?**
1. `running = false` yapar
2. Worker'lar kuyruğu **boşaltana kadar** çalışır
3. Sonra kapanır

### Force Shutdown

Kuyruktaki görevler **iptal edilir**, çalışanlar tamamlanır:

```cpp
pool.shutdown(knst_shutdown::force);

// Kuyruktakiler iptal, çalışanlar bitti
```

**Ne yapar?**
1. Kuyruktaki tüm görevleri **iptal** eder
   - Her biri için `finished_flag = true` yapar
   - Bekleyen `join()` çağrıları uyanır (boş döner)
2. `running = false` yapar
3. Çalışan görevler normal biter

**Kullanım alanı:** Uygulama kapanırken hızlı shutdown.

### Force Shutdown Örneği

```cpp
knst_thread_pool pool(1);
pool.start();

// 200 görev — 33 tanesi hemen başlar, 167 kuyrukta
for (int i = 0; i < 200; ++i) {
    pool.submit([&started, &completed]() {
        started.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        completed.fetch_add(1);
    });
}

std::this_thread::sleep_for(std::chrono::milliseconds(30));

pool.shutdown(knst_shutdown::force);

std::cout << "Started : " << started.load() << " / 200\n";   // ~33
std::cout << "Completed: " << completed.load() << " / 200\n"; // ~33
// 167 görev iptal edildi
```

---

## 🔗 8) `knst_thread` Entegrasyonu

Pool'a gönderilen görev bir `knst_thread` döner — join edilebilir:

### `knst_thread::start(pool, fn)`

```cpp
knst_thread_pool pool(2);
pool.start();

knst_thread t;
bool ok = t.start(pool, []() {
    std::cout << "Pool'da çalışıyor\n";
});

std::cout << "is_pooled(): " << t.is_pooled() << "\n";   // true

t.join();
pool.shutdown();
```

### `knst_thread::start_with_priority(pool, prio, fn)`

```cpp
knst_thread t;
t.start_with_priority(
    pool,
    knst_thread_priority::high,
    []() { /* kritik iş */ }
);

t.join();
```

### Pooled Thread'in Farkları

**Pooled thread'lerde çalışmayan metodlar:**

- ❌ `kill()` — pool'u bozabilir
- ❌ `detach()` — pool yönetiyor
- ✅ `join()` — çalışır (pool "bitti" sinyali verir)

---

## 📊 9) Performans Notları

### Batch Pulling

Her worker kuyruktan **8 görev** birden çeker (`KNST_WORKER_BATCH_SIZE`):

```cpp
while (batch.size() < KNST_WORKER_BATCH_SIZE && m_queue.pop(item)) {
    batch.push_back(std::move(item));
}
```

**Kazanç:** Kilit maliyeti **8x azalır** — her görev için ayrı kilit yok.

### Ne Zaman Yavaş?

- **Çok küçük görevler** — batch pulling'e rağmen overhead
- **Uzun süren görevler** — worker'lar meşgulken yenisi açılmaz (overflow hariç)
- **Aşırı overflow** — 32'den fazla geçici thread OS için ağır

### Karşılaştırma

```cpp
// ❌ Yavaş: Her görev için yeni thread
auto start = std::chrono::steady_clock::now();
knst_vector<knst_thread> threads;
for (int i = 0; i < 10000; ++i) {
    knst_thread t;
    t.start([]() { /* hafif iş */ });
    threads.push_back(std::move(t));
}
for (auto& t : threads) t.join();
auto end = std::chrono::steady_clock::now();
// ~2000-5000 ms

// ✅ Hızlı: Pool kullan
start = std::chrono::steady_clock::now();
knst_thread_pool pool;
pool.start();
for (int i = 0; i < 10000; ++i) {
    pool.submit([]() { /* hafif iş */ });
}
pool.wait_all();
pool.shutdown();
end = std::chrono::steady_clock::now();
// ~50-200 ms
```

**Kazanç:** **10-100x**.

### Worker Sayısı Seçimi

| İş Yükü | Önerilen Worker |
|---|---|
| CPU-bound (hesaplama) | `hardware_concurrency()` |
| I/O-bound (dosya, ağ) | 2x `hardware_concurrency()` |
| Mixed | 1.5x `hardware_concurrency()` |
| Sadece birkaç ağır görev | 2-4 |

---

## ⚙️ 10) Ayar Makroları

`knst_settings.hpp` içine koy:

```cpp
#define KNST_OVERFLOW_MAX 32          // Overflow thread limiti
#define KNST_QUEUE_MAX 1024           // Kuyruk kapasitesi
#define KNST_DEFAULT_WORKER_COUNT 0   // 0 = auto
#define KNST_WORKER_BATCH_SIZE 8      // Worker'ın çektiği görev sayısı
```

### Ayar Açıklamaları

| Makro | Varsayılan | Ne yapar? |
|---|---|---|
| `KNST_OVERFLOW_MAX` | 32 | Max geçici thread |
| `KNST_QUEUE_MAX` | 1024 | Kuyruk dolduğunda `submit()` false döner |
| `KNST_DEFAULT_WORKER_COUNT` | 0 | 0 = `hardware_concurrency()` |
| `KNST_WORKER_BATCH_SIZE` | 8 | Kilit maliyetini amortize eder |

---

## 🌍 11) Platform Farkları

| Konu | Windows | POSIX |
|---|---|---|
| **Thread** | `std::thread` | `std::thread` |
| **Priority** | `SetThreadPriority` | `setpriority` |
| **Baseline** | `IDLE_PRIORITY_CLASS` vs | nice değeri |
| **Overflow** | Aynı | Aynı |

**Not:** `std::thread` kullanıldığı için davranış büyük ölçüde aynıdır.

---

## 🔥 12) Gerçek Kullanım Örnekleri

### Örnek 1: Paralel Veri İşleme

```cpp
void process_data(knst_vector<int>& data) {
    knst_thread_pool pool;
    pool.start();

    std::atomic<int> sum{0};

    for (int value : data) {
        pool.submit([&sum, value]() {
            // Ağır hesaplama
            sum.fetch_add(value * value);
        });
    }

    pool.wait_all();
    pool.shutdown();

    std::cout << "Toplam: " << sum.load() << "\n";
}
```

### Örnek 2: Web Crawler

```cpp
void crawl_urls(const knst_vector<knst_c16string>& urls) {
    knst_thread_pool pool(8);   // I/O-bound, 2x CPU
    pool.start();

    std::atomic<int> downloaded{0};

    for (const auto& url : urls) {
        pool.submit([url, &downloaded]() {
            // HTTP isteği (I/O)
            auto response = http_get(url);
            save_to_file(url, response);
            downloaded.fetch_add(1);
        });
    }

    pool.wait_all();
    std::cout << "İndirilen: " << downloaded.load() << "\n";
    pool.shutdown();
}
```

### Örnek 3: Server Request Handler

```cpp
class Server {
    knst_thread_pool m_pool;

public:
    Server() : m_pool(16) {
        m_pool.start();
    }

    void handle_request(int client_fd) {
        m_pool.submit([client_fd]() {
            // Request işle
            process_request(client_fd);
            close(client_fd);
        });
    }

    ~Server() {
        m_pool.shutdown(knst_shutdown::graceful);
    }
};
```

### Örnek 4: Priority Scheduling

```cpp
void process_jobs() {
    knst_thread_pool pool(4);
    pool.start();

    // Yüksek öncelikli işler
    for (int i = 0; i < 10; ++i) {
        pool.submit_with_priority(
            knst_thread_priority::high,
            [i]() { handle_urgent(i); }
        );
    }

    // Normal işler
    for (int i = 0; i < 100; ++i) {
        pool.submit([i]() { handle_normal(i); });
    }

    // Düşük öncelikli işler
    for (int i = 0; i < 1000; ++i) {
        pool.submit_with_priority(
            knst_thread_priority::lowest,
            [i]() { handle_background(i); }
        );
    }

    pool.wait_all();
    pool.shutdown();
}
```

### Örnek 5: Progress Monitoring

```cpp
void process_with_progress() {
    knst_thread_pool pool;
    pool.start();

    std::atomic<int> done{0};
    constexpr int total = 1000;

    for (int i = 0; i < total; ++i) {
        pool.submit([&done]() {
            // İş
            done.fetch_add(1);
        });
    }

    // Progress göster
    while (done.load() < total) {
        std::cout << "\rİlerleme: " << done.load() << "/" << total
                  << "  (pending=" << pool.pending_count()
                  << ", active=" << pool.active_count() << ")" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "\nBitti!\n";

    pool.shutdown();
}
```

### Örnek 6: Graceful vs Force Shutdown

```cpp
void test_shutdown_modes() {
    // Graceful — hepsi biter
    {
        knst_thread_pool pool(2);
        pool.start();

        std::atomic<int> done{0};
        for (int i = 0; i < 100; ++i) {
            pool.submit([&done]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                done.fetch_add(1);
            });
        }

        pool.shutdown(knst_shutdown::graceful);
        std::cout << "Graceful: " << done.load() << " / 100\n";   // 100
    }

    // Force — kuyruktakiler iptal
    {
        knst_thread_pool pool(2);
        pool.start();

        std::atomic<int> done{0};
        for (int i = 0; i < 100; ++i) {
            pool.submit([&done]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                done.fetch_add(1);
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pool.shutdown(knst_shutdown::force);
        std::cout << "Force: " << done.load() << " / 100\n";   // ~10-40
    }
}
```

### Örnek 7: Batch Image Processing

```cpp
void process_images(const knst_vector<knst_c16string>& paths) {
    knst_thread_pool pool(8);
    pool.start();

    std::atomic<int> processed{0};

    for (const auto& path : paths) {
        pool.submit([path, &processed]() {
            knst_image img = knst_image_loader::load(path);

            if (img) {
                // Thumbnail üret
                knst_image_load_options o;
                o.resize_width  = 256;
                o.resize_height = 256;
                o.keep_aspect   = true;

                auto thumb = knst_image_loader::load(path, o);
                // thumbs.push_back(thumb);   // sync gerekir
            }

            processed.fetch_add(1);
        });
    }

    pool.wait_all();
    std::cout << processed.load() << " resim işlendi\n";
    pool.shutdown();
}
```

---

## 🛡️ 13) Exception Safety

### Task İçindeki Exception

Task exception fırlatırsa, pool **yutar** ve normal devam eder:

```cpp
knst_thread_pool pool(2);
pool.start();

for (int i = 0; i < 10; ++i) {
    pool.submit([i]() {
        if (i == 5) throw std::runtime_error("hata!");
        // diğerleri normal çalışır
    });
}

pool.wait_all();   // ✅ Sorunsuz biter
pool.shutdown();
```

**Ama exception'ı göremezsin!** Bunun için `std::exception_ptr` kullan:

```cpp
std::mutex err_mtx;
knst_vector<std::exception_ptr> errors;

for (int i = 0; i < 10; ++i) {
    pool.submit([&]() {
        try {
            // riskli iş
            throw std::runtime_error("hata");
        } catch (...) {
            std::lock_guard<std::mutex> lock(err_mtx);
            errors.push_back(std::current_exception());
        }
    });
}

pool.wait_all();

for (const auto& e : errors) {
    try { std::rethrow_exception(e); }
    catch (const std::exception& ex) {
        std::cerr << "Hata: " << ex.what() << "\n";
    }
}
```

### Destructor Davranışı

Pool destructor'ı **otomatik force shutdown** yapar:

```cpp
{
    knst_thread_pool pool(2);
    pool.start();
    // submit(...);
    // shutdown çağrılmadı
}   // destructor → force shutdown → kuyruğa bakılmaz
```

**Ama:** Graceful shutdown istiyorsan manuel `pool.shutdown(knst_shutdown::graceful)` çağır.

---

## 🎯 Size Getireceği Kazanç

`knst_thread_pool` şu durumlarda ciddi avantaj sağlar:

- ✅ **Çok sayıda görev** — 100+ görev varsa pool şart
- ✅ **Server uygulaması** — request handling
- ✅ **Batch işleme** — resim, veri, dosya
- ✅ **Paralel hesaplama** — CPU-bound iş
- ✅ **Web scraping** — I/O-bound, çok sayıda istek
- ✅ **Priority scheduling** — kritik işler öne alınsın

