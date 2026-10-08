# knst_function — Move-Only Callable Wrapper

Selamlar! 👋 Bu doküman `knst_function` sınıfının ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **`void()` imzasına sahip herhangi bir callable'ı (lambda, fonksiyon pointer'ı, functor) saklayabilen, move-only, SSO (Small Object Optimization) destekli bir wrapper.** `std::function`'a benzer ama **daha hafif, daha hızlı ve exception-free**.

---

## 🎯 Neden Bu Sınıf Var?

`std::function` C++ standardının bir parçası ama bazı dezavantajları var:

- **Ağır** — içinde çok fazla tip bilgisi taşır
- **Exception kullanır** — `bad_function_call`, heap allocation hataları
- **Copy edilebilir** — move-only semantik yok, bu da bazı senaryolarda yavaş
- **İade tipini destekler** — ama çoğu zaman `void()` yeterli

`knst_function` bunları çözer:

- ✅ **Move-only** — kopyalama yok, sadece sahiplik transferi
- ✅ **SSO** — 64 byte'a kadar stack'te, heap allocation yok
- ✅ **`nothrow` garantisi** — exception fırlatmaz
- ✅ **`void()` odaklı** — basit ve hızlı
- ✅ **Thread pool uyumlu** — `knst_thread_pool` ile mükemmel çalışır

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Move-only** | Kopyalama yok, sahiplik transferi net |
| **SSO (64 byte)** | Küçük lambda'lar stack'te, heap yok |
| **`nothrow`** | Exception güvenliği derdi yok |
| **`void()` imzası** | Basit, tek amaçlı |
| **`std::function` uyumlu** | Aynı kullanım kalıpları |
| **Küçük boyut** | Sadece ~72 byte (SSO buffer + pointer'lar) |
| **`constexpr`-dostu** | Sabit boyut, öngörülebilir |
| **Sıfır sanal fonksiyon** | vtable manuel, özelleştirilebilir |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // 1. Boş
    knst_function f;
    std::cout << "empty: " << (f.empty() ? "yes" : "no") << "\n";

    // 2. Lambda ile
    f = []() { std::cout << "Merhaba!\n"; };
    f();   // "Merhaba!"

    // 3. Fonksiyon pointer'ı ile
    knst_function g = &some_function;
    g();

    // 4. Çağırma (boş olsa bile güvenli)
    knst_function h;
    h();   // hiçbir şey yapmaz, çökmez

    return 0;
}
```

---

## 📋 Temel API

### Constructor'lar

```cpp
// Boş
knst_function f;

// Lambda ile
knst_function f1 = [](){ /* ... */ };

// Fonksiyon pointer'ı ile
void my_func();
knst_function f2 = &my_func;

// Functor (operator() olan sınıf) ile
struct MyFunctor {
    void operator()() { /* ... */ }
};
knst_function f3 = MyFunctor{};

// Move constructor
knst_function f4 = std::move(f1);   // f1 artık boş
```

### Metodlar

| Metod | Ne yapar? |
|---|---|
| `f()` | Saklanan callable'ı çağırır. Boşsa no-op. |
| `f.empty()` | İçeride bir şey var mı? `true`/`false` |
| `static_cast<bool>(f)` | `empty()`'in tersi — callable var mı? |
| `f.reset()` | Saklanan callable'ı siler, boşaltır |
| `f = std::move(other)` | Sahiplik transferi |

### Operatörler

```cpp
// Çağırma
f();                    // call operator

// Bool kontrolü
if (f) { /* dolu */ }
if (!f) { /* boş */ }

// Move assignment
knst_function a = [](){};
knst_function b;
b = std::move(a);       // b'ye taşı, a boşalır

// Copy YASAK (compile-time hata)
knst_function c = a;    // ❌ Derlenmez!
```

---

##  1) Boş Durum

```cpp
knst_function f;

std::cout << f.empty() << "\n";  // true
std::cout << (f ? "dolu" : "boş") << "\n";  // boş

// Boş fonksiyonu çağırmak GÜVENLİ — no-op yapar
f();  // Hiçbir şey olmaz, çökmez
```

**Bu önemli bir özellik.** `std::function` boşken çağrılırsa `std::bad_function_call` fırlatır. `knst_function` ise sessizce hiçbir şey yapmaz.

---

##  2) Lambda ile Kullanım

En yaygın kullanım. Herhangi bir capture'lı lambda:

```cpp
int counter = 0;

knst_function increment = [&counter]() {
    ++counter;
    std::cout << "counter = " << counter << "\n";
};

increment();  // counter = 1
increment();  // counter = 2
increment();  // counter = 3

std::cout << "final: " << counter << "\n";  // 3
```

**Not:** Lambda referans yakalıyorsa (`[&]`), referans edilen nesne `knst_function`'dan daha uzun yaşamalı. Aksi halde dangling reference olur.

```cpp
knst_function bad() {
    int local = 42;
    return [&local]() {           // ❌ Dangling!
        std::cout << local << "\n";
    };
}

knst_function good() {
    int local = 42;
    return [local]() {            // ✅ Value capture
        std::cout << local << "\n";
    };
}
```

---

##  3) Fonksiyon Pointer'ı

```cpp
void hello() {
    std::cout << "hello from pointer\n";
}

knst_function f = &hello;
f();
```

Fonksiyon pointer'ları için **özel bir vtable** oluşturulur, SSO kullanılır (pointer boyutu = 8 byte).

---

##  4) Functor (Function Object)

`operator()` tanımlı herhangi bir sınıf:

```cpp
struct Greeter {
    std::string name;
    
    void operator()() {
        std::cout << "Merhaba, " << name << "!\n";
    }
};

Greeter g{"Yusuf"};
knst_function f = g;
f();  // "Merhaba, Yusuf!"
```

---

##  5) SSO — Stack vs Heap

### SSO Eşiği: 64 Byte (Varsayılan)

`knst_function` içinde **64 byte'lık bir buffer** var. Bu boyuta sığan callable'lar **stack'te** saklanır (heap allocation yok). Aşanlar **heap'e** gider.

### Ne Zaman Stack'te Kalır?

```cpp
// ✅ SSO — 8 byte (int capture)
auto small = [x = 42]() { /* ... */ };
knst_function f1 = small;
// sizeof(small) <= 64 → stack

// ✅ SSO — 32 byte (birden fazla capture)
int a, b, c, d, e, f, g, h;
auto medium = [a,b,c,d,e,f,g,h]() { /* ... */ };
knst_function f2 = medium;
// 8*8 = 64 byte → hala stack

// ❌ Heap — 128 byte (büyük array)
char big[128] = {};
auto large = [big]() { /* ... */ };
knst_function f3 = large;
// 128 byte > 64 byte → heap
```

### `sizeof(knst_function)`

```cpp
std::cout << sizeof(knst_function) << " bytes\n";
// Genelde 72-80 byte:
//   - 64 byte m_buffer (SSO)
//   - 8 byte m_heap_ptr
//   - 8 byte m_vtable
//   - 1 byte m_is_heap (+ padding)
```

### Eşiği Değiştirme

`knst_settings.hpp` içine koy:

```cpp
#define KNST_FUNCTION_INLINE_SIZE 128
```

Bundan sonra 128 byte'a kadar olan callable'lar stack'te kalır. **Trade-off:**

| Değer | Avantaj | Dezavantaj |
|---|---|---|
| **32** | Küçük `sizeof` | Çoğu lambda heap'e gider |
| **64 (varsayılan)** | Dengeli | İyi bir ortalama |
| **128** | Çoğu lambda stack'te | `knst_function` 2x büyür |
| **256** | Neredeyse hep stack'te | Memory israfı |

**Tavsiye:** Varsayılan 64'ü koru. Sadece çok sayıda `knst_function` tutuyorsan ve her biri büyük capture'a sahipse artır.

---

##  6) Move Semantics

`knst_function` **kopyalanamaz** ama **taşınabilir**. Bu, sahiplik semantiğini netleştirir.

### Move Constructor

```cpp
knst_function a = [](){ std::cout << "a\n"; };
knst_function b = std::move(a);

std::cout << (a.empty() ? "a boş" : "a dolu") << "\n";  // a boş
std::cout << (b.empty() ? "b boş" : "b dolu") << "\n";  // b dolu

b();  // "a" çalışır
a();  // güvenli no-op
```

### Move Assignment

```cpp
knst_function a = [](){ std::cout << "A\n"; };
knst_function b = [](){ std::cout << "B\n"; };

b = std::move(a);  // b'nin eski içeriği silinir, a b'ye taşınır
b();               // "A"
```

### `std::move` Ne Yapar?

- **Stack'te ise:** Yeni objeye `memcpy` yapılmaz, **taşınma constructor'ı** çağrılır
- **Heap'te ise:** Pointer transfer edilir, heap bloğu kopyalanmaz
- **Kaynak:** Boş duruma döner (`m_vtable = nullptr`)

**Yani move her zaman ucuz** — SSO'da bile.

### Neden Copy Yasak?

Çünkü lambda'lar her zaman copy'lenebilir değil. Ayrıca:

1. **Sahiplik net olsun** — kim sahip?
2. **Race condition olmasın** — iki thread aynı callable'ı çağırmasın
3. **Nothrow garanti** — copy allocation başarısız olabilir

---

##  7) `reset()` — Temizleme

```cpp
knst_function f = [](){ std::cout << "merhaba\n"; };
f();
std::cout << "empty: " << f.empty() << "\n";  // false

f.reset();
std::cout << "empty: " << f.empty() << "\n";  // true

f();  // güvenli no-op
```

**Ne yapar?**
1. Vtable üzerinden `destroy` çağırır (lambda'nın destructor'ı)
2. Heap'te ise `operator delete` çağırır
3. Tüm pointer'ları `nullptr` yapar

**Not:** Destructor otomatik olarak `reset()` çağırır, yani manuel çağırmak zorunda değilsin.

---

##  8) Thread Pool ile Kullanım

`knst_function`'ın asıl tasarım amacı bu. `knst_thread_pool`'a job submit etmek için:

```cpp
knst_thread_pool pool;

// Job gönder
pool.submit([](){
    std::cout << "Thread: " << std::this_thread::get_id() << "\n";
});

// Capture'lı job
int data = 42;
pool.submit([data](){
    std::cout << "Data: " << data << "\n";
});
```

**Neden `knst_function`?**
- Thread pool job'ları `void()` imzasında
- Move-only olduğu için queue içine taşımak ucuz
- SSO sayesinde çoğu job heap allocation yapmaz
- `nothrow` sayesinde queue işlemleri güvenli

---

##  9) Performans Karşılaştırması

| İşlem | `std::function` | `knst_function` |
|---|---|---|
| Boş oluşturma | ~8 byte | ~72 byte |
| Küçük lambda (SSO) | Heap alloc | **Stack** ✅ |
| Büyük lambda | Heap alloc | Heap alloc |
| Çağırma | Sanal çağrı | Sanal çağrı |
| Taşıma | Kopya + sil | **Pointer transfer** ✅ |
| Kopyalama | Desteklenir | **Yasak** ❌ |
| Boş çağırma | Exception | **No-op** ✅ |
| `sizeof` | ~32 byte | ~72 byte |

**Not:** `knst_function` fiziksel olarak daha büyük (SSO buffer'ı yüzünden) ama runtime'da **çok daha hızlı** çünkü:

- SSO sayesinde heap allocation yok
- Move işlemi sadece pointer transferi
- Exception handling yok

---

##  10) Hata Yönetimi

### Boş Çağırma

```cpp
knst_function f;
f();  // ✅ Güvenli — no-op
```

### `nothrow` Garantisi

Sadece **`nothrow`-move-constructible** veya **`nothrow`-copy-constructible** callable'lar kabul edilir:

```cpp
// ✅ OK — lambda nothrow-moveable
auto ok = [x = 42]() {};

// ❌ Derleme hatası — string nothrow değil
std::string str = "hello";
auto bad = [str]() {};  // std::string nothrow değil
// static_assert tetiklenir
```

**Neden?** Çünkü `knst_function` exception-free olmalı. Copy/move başarısız olursa ne yapacağını bilemez.

### Heap Allocation Başarısız Olursa

```cpp
// Heap allocation başarısız olursa
knst_function f = [/* büyük capture */](){};

// f boş kalır, çağırma no-op olur
if (f.empty()) {
    std::cerr << "Allocation failed\n";
}
```

**Yani:** `operator new` dönerse `nullptr`, sessizce boş `knst_function` oluşur. Exception fırlatmaz.

---

##  11) Kullanım Senaryoları

### Senaryo 1: Callback Sistemi

```cpp
class Button {
    knst_function m_on_click;
    
public:
    void set_on_click(knst_function fn) {
        m_on_click = std::move(fn);
    }
    
    void click() {
        if (m_on_click) m_on_click();
    }
};

Button btn;
btn.set_on_click([](){
    std::cout << "Tıklandı!\n";
});
btn.click();
```

### Senaryo 2: Deferred Execution

```cpp
knst_vector<knst_function> deferred;

deferred.push_back([](){ std::cout << "Adım 1\n"; });
deferred.push_back([](){ std::cout << "Adım 2\n"; });
deferred.push_back([](){ std::cout << "Adım 3\n"; });

for (auto& fn : deferred) fn();
```

### Senaryo 3: Scope Guard

```cpp
class ScopeGuard {
    knst_function m_cleanup;
public:
    explicit ScopeGuard(knst_function fn) 
        : m_cleanup(std::move(fn)) {}
    
    ~ScopeGuard() {
        if (m_cleanup) m_cleanup();
    }
};

void do_work() {
    FILE* f = fopen("data.txt", "r");
    ScopeGuard guard([f](){ fclose(f); });
    
    // ... iş ...
    
}  // guard destructor'ı fclose çağırır
```

### Senaryo 4: Event System

```cpp
class EventEmitter {
    std::map<std::string, knst_vector<knst_function>> m_listeners;
    
public:
    void on(const std::string& event, knst_function fn) {
        m_listeners[event].push_back(std::move(fn));
    }
    
    void emit(const std::string& event) {
        auto it = m_listeners.find(event);
        if (it == m_listeners.end()) return;
        for (auto& fn : it->second) fn();
    }
};

EventEmitter emitter;
emitter.on("start", [](){ std::cout << "Başladı\n"; });
emitter.on("start", [](){ std::cout << "İkinci handler\n"; });
emitter.emit("start");   // İkisi de çalışır
```

---

##  12) `knst_thread_pool` ile Entegrasyon

```cpp
#include "KernelNucleusT.hpp"

int main() {
    knst_thread_pool pool;
    
    // 100 job gönder
    for (int i = 0; i < 100; ++i) {
        pool.submit([i](){
            std::cout << "Job " << i << "\n";
        });
    }
    
    pool.wait_all();
    return 0;
}
```

**Kazanç:** Her job `knst_function` olarak saklanır. SSO sayesinde int capture'lı job'lar heap allocation **yapmaz**. Yani 100 job = 100 heap allocation **yerine** 0 heap allocation.

---

##  13) Ayar Makrosu

`knst_settings.hpp` içine koy:

```cpp
// SSO eşiği (byte). Varsayılan: 64.
#define KNST_FUNCTION_INLINE_SIZE 64
```

### Değer Seçimi

| Kullanım | Önerilen |
|---|---|
| Genel amaçlı | **64** (varsayılan) |
| Çok sayıda küçük lambda | 32 |
| Sık büyük capture (server, DB) | 128 |
| Embedded / kısıtlı RAM | 16 veya 32 |

---

##  14) İpuçları

### ✅ Yapılması Gerekenler

```cpp
// 1. Move kullan, copy yok
knst_function a = [](){};
knst_function b = std::move(a);  // ✅

// 2. Boş kontrolü yap
if (f) f();  // ✅

// 3. Küçük capture tercih et (SSO)
int x = 42;
knst_function f = [x](){};  // ✅ stack'te

// 4. Referans capture'da dikkatli ol
auto fn = [&local]() { /* local hala yaşıyor mu? */ };  // ⚠️
```

### ❌ Kaçınılması Gerekenler

```cpp
// 1. Copy denemesi
knst_function a = [](){};
knst_function b = a;  // ❌ Derleme hatası

// 2. Boş çağırma — std::function'dan farklı davranır
// (bu aslında OK, ama emin olmak için check et)
f();  // no-op, sessiz

// 3. Capture'da büyük veri kopyalamak
char big[1024];
knst_function f = [big](){};  // ❌ Heap'e gider
knst_function g = [&big](){}; // ✅ Referans — ama dikkat

// 4. Exception fırlatan lambda
auto f = [](){ throw std::runtime_error("hata"); };  // ❌ Önerilmez
// knst_function exception-free tasarlanmıştır
```

---

##  15) Tam Örnek

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

void hello() {
    std::cout << "Merhaba!\n";
}

int main() {
    // 1. Boş
    knst_function f;
    std::cout << "Boş mu? " << (f.empty() ? "evet" : "hayır") << "\n";

    // 2. Lambda
    int counter = 0;
    f = [&counter]() {
        ++counter;
        std::cout << "Sayaç: " << counter << "\n";
    };
    f();
    f();
    f();

    // 3. Fonksiyon pointer'ı
    knst_function g = &hello;
    g();

    // 4. Move
    knst_function h = std::move(g);
    std::cout << "g boş mu? " << (g.empty() ? "evet" : "hayır") << "\n";
    h();  // "Merhaba!"

    // 5. Reset
    h.reset();
    std::cout << "h boş mu? " << (h.empty() ? "evet" : "hayır") << "\n";

    // 6. SSO boyutu
    std::cout << "sizeof(knst_function): " << sizeof(knst_function) << " byte\n";
              

    return 0;
}
```

### Örnek Çıktı

```
Boş mu? evet
Sayaç: 1
Sayaç: 2
Sayaç: 3
Merhaba!
g boş mu? evet
Merhaba!
h boş mu? evet
sizeof(knst_function): 80 byte
```

---

## 🎯 Size Getireceği Kazanç

`knst_function` şu durumlarda ciddi avantaj sağlar:

- ✅ **Thread pool job submission** — tasarım amacı bu
- ✅ **Callback sistemleri** — event handler, GUI, button
- ✅ **Deferred execution** — queue, task scheduler
- ✅ **Scope guard / RAII** — cleanup için
- ✅ **Move-only semantics** istediğinde
- ✅ **SSO performansı** gerektiğinde
- ✅ **Exception-free** ortam (embedded, kernel, real-time)

