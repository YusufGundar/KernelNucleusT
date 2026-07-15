# knst_memory — Kullanım Kılavuzu (Türkçe)

`knst_default_allocator` ve `knst_pool_allocator` — `knst_c16string` (ve diğer knst bileşenleri) tarafından kullanılabilecek iki allocator (bellek ayırıcı) tipidir.


---

## 1. `knst_default_allocator` — Klasik Heap Allocator

Durumsuz (stateless), `malloc/free/realloc` etrafında ince bir sarmalayıcıdır. Tüm fonksiyonları `static`'tir, yani nesne oluşturmaya gerek yoktur.

```cpp
// Bellek ayırır (Linux/macOS'ta malloc + memset(0), Windows'ta HeapAlloc + HEAP_ZERO_MEMORY)
void* p = knst_default_allocator::allocate(128);   // 128 byte, sıfırlanmış bellek

// Belleği serbest bırakır (ikinci parametre kullanılmaz, sadece imza uyumluluğu için var)
knst_default_allocator::deallocate(p, 128);

// Yeniden boyutlandırır
void* p2 = knst_default_allocator::allocate(64);
p2 = knst_default_allocator::reallocate(p2, 256);  // 256 byte'a büyütür
knst_default_allocator::reallocate(p2, 0);          // new_size==0 -> serbest bırakır, nullptr döner
```
*Not:* `allocate(0)` Linux/macOS'ta `nullptr` döner (Windows'ta `HeapAlloc` davranışı platforma bağlıdır). `reallocate(nullptr, n)` çağrısı `allocate(n)`'e eşdeğerdir.

```cpp
knst_default_allocator a1, a2;
a1.pool_count();       // 0 (havuz kavramı yok, her zaman 0 döner)
a1.max_block_size();   // 0 (aynı sebeple)
a1 == a2;               // true (durumsuz olduğu için her zaman eşit kabul edilir)
a1 != a2;               // false
```
*Not:* `pool_count()`/`max_block_size()` sadece API uyumluluğu için vardır (`knst_pool_allocator` ile aynı arayüzü paylaşmak için); `knst_default_allocator` için anlamlı bir bilgi taşımaz.

---

## 2. `knst_pool_allocator` — Havuz Tabanlı Allocator

Sabit boyutlu bloklardan oluşan havuzlar (pool) kullanır. Referans sayımlı (`ref_count`), paylaşılabilir bir `pool_impl*` etrafında ince bir sarmalayıcıdır — nesnenin kendisi tam olarak **bir pointer boyutunda**dır (`static_assert(sizeof(knst_pool_allocator) == sizeof(void*))`).

### 2.1 Oluşturma (Constructors)

```cpp
knst_pool_allocator pa1;                     // Varsayılan boyutlar: {64, 256, 1024, 2048}
knst_pool_allocator pa2(32, 128, 512);        // Özel blok boyutları (integral tipler olmalı)
knst_pool_allocator pa3(pa1);                 // Kopya: aynı pool_impl'i paylaşır (ref_count++)
knst_pool_allocator pa4(std::move(pa1));      // Taşıma: pool_impl pointer'ı devralır, pa1 boşalır
knst_pool_allocator string_pool(
    knst_pool_config{32, 500},    // Çok sayıda küçük string (isimler, tag'ler)
    knst_pool_config{128, 200},   // Orta boy string'ler (dosya yolları)
    knst_pool_config{512, 50}     // Az sayıda büyük string (JSON/XML)
); // gibi amaçlar için kullanılabilir özel knst_pool_config içinde bilgileri doldurmanız yeterli olucaktır ilk parametre : blok boyutu ikinci parametre : blok sayısı'dır
```
*Not:* Değişken sayıda parametre alan kurucu `explicit`'tir; `knst_pool_allocator pa = 64;` gibi örtük dönüşüm çalışmaz, doğrudan `knst_pool_allocator pa(64);` yazman gerekir. Ayrıca parametreler yalnızca **integral tipler** olabilir (`std::is_integral_v` kontrolü vardır).

### 2.2 Havuz Kurulumu — Arka Planda Ne Olur?

```cpp
knst_pool_allocator pa(10, 300, 300, 2000);
// 1) Verilen boyutlar sıralanır ve tekrarlar silinir -> {10, 300, 2000}
// 2) Her boyut MIN_BLOCK'tan (sizeof(void*)*2, genelde 16 byte) küçükse 16'ya yükseltilir -> {16, 300, 2000}
// 3) Her havuzun kapasitesi = clamp(1024 / block_size, 8, 4096) formülüyle hesaplanır
pa.pool_count();       // 3
pa.max_block_size();   // 2000
```

### 2.3 Allocate / Deallocate / Reallocate

```cpp
knst_pool_allocator pa; // {64, 256, 1024, 2048}

void* p1 = pa.allocate(50);     // 64 byte'lık havuzdan bir blok döner (50 <= 64 uyar)
void* p2 = pa.allocate(5000);   // 5000 > max_block_size(2048) -> knst_default_allocator'a düşer (fallback)

pa.deallocate(p1, 50);          // size_hint verilirse doğru havuzu hızlıca bulur
pa.deallocate(p2, 5000);        // hiçbir havuz sahiplenmiyor (owns() false) -> default_allocator ile serbest bırakılır

void* p3 = pa.allocate(60);
p3 = pa.reallocate(p3, 60);     // yeni boyut eski blok boyutuna sığıyor -> aynı pointer döner, kopyalama yapılmaz
p3 = pa.reallocate(p3, 500);    // sığmıyor -> uygun havuzdan yeni blok alınır, veri memcpy'lenir, eski blok serbest bırakılır
```
*Not:* `allocate(size)`, `size`'a sığan **ilk havuzdan** (küçükten büyüğe sıralı) boş blok almayı dener; o havuzda boş blok kalmamışsa (havuz dolu), **sığan bir sonraki (daha büyük) havuza** geçer. Hiçbir havuzda yer yoksa veya `size` en büyük havuzu aşıyorsa `knst_default_allocator`'a düşer.

### 2.4 Paylaşım (Reference Counting) Davranışı

```cpp
knst_pool_allocator a;
knst_pool_allocator b = a;      // Kopya: aynı pool_impl paylaşılır, ref_count = 2

a == b;                         // true (aynı pool_impl pointer'ına sahipler)

void* p = a.allocate(100);      // a üzerinden ayrılan bellek b tarafından da görülebilir/serbest bırakılabilir
b.deallocate(p, 100);           // Geçerlidir, çünkü aynı havuzları paylaşıyorlar
```

### 2.5 `reset()` — Paylaşımlı ve Paylaşımsız Durum Farkı

```cpp
knst_pool_allocator a;
knst_pool_allocator b = a;       // paylaşımlı: ref_count = 2

a.reset(16, 64, 256);           // a PAYLAŞIMLI olduğu için: yeni bir pool_impl oluşturur,
                                 // eski (paylaşılan) pool_impl'e DOKUNMAZ.
                                 // Artık a farklı bir havuza, b hâlâ eski havuza bakıyor.
a == b;                          // false (artık farklı pool_impl'ler)

knst_pool_allocator c;            // paylaşımsız: ref_count = 1
c.reset(8, 32);                  // c PAYLAŞIMSIZ olduğu için: mevcut pool_impl'i yok edip
                                  // yerinde (in-place) yeniden kurar. Aynı pool_impl kalır.
```
*Not:* Bu, "paylaşılan veriyi başkalarını etkilemeden değiştirme" (copy-on-write benzeri) bir güvenlik davranışıdır — `reset()` çağıran taraf her zaman kendi bağımsız (ya da zaten tek sahip olduğu) havuzuna sahip olur.

### 2.6 Thread-Safety

```cpp
// #define KNST_MEMORY_POOL_USE_MUTEX   // .hpp include edilmeden ÖNCE tanımlanmalı , yada knst_settings.hpp de en üstte tanımlanmalı
```


*Not:* Bu makro tanımlıysa, `pool_impl` içine bir `std::mutex` eklenir ve `allocate`/`deallocate`/`reallocate`/`pool_count` çağrıları bu mutex ile korunur. Tanımlı **değilse**, `knst_pool_allocator` thread-safe **değildir** — aynı havuzu paylaşan iki `knst_pool_allocator` kopyasını farklı thread'lerden eşzamanlı kullanmak veri yarışına (data race) yol açar.

### 2.7 Boyut Garantisi

```cpp
static_assert(sizeof(knst_pool_allocator) == sizeof(void*),
    "knst_pool_allocator must be exactly one pointer in size.");
```
*Not:* Bu, `knst_pool_allocator`'ın `[[no_unique_address]]` ile bir üye değişken olarak (örn. `knst_c16string` içinde) tutulduğunda ekstra yer kaplamamasını garanti eder

---

## Özet Tablo

| Fonksiyon | `knst_default_allocator` | `knst_pool_allocator` |
|---|---|---|
| `allocate(size)` | Her zaman heap'ten ayırır (sıfırlanmış). | Uygun havuzdan ayırır, sığmazsa heap'e düşer. |
| `deallocate(ptr, size_hint)` | Doğrudan `free`. | Sahibi olan havuza geri koyar, değilse `free`. |
| `reallocate(ptr, new_size)` | `realloc`. | Havuz sınırları içinde akıllıca büyütür, dışına taşarsa yeni blok + `memcpy`. |
| `pool_count()` | Her zaman `0`. | Kurulu havuz sayısı. |
| `max_block_size()` | Her zaman `0`. | En büyük havuzun blok boyutu. |
| `operator==` | Her zaman `true` (durumsuz). | Aynı `pool_impl`'e işaret ediyorlarsa `true`. |
| Kopyalama maliyeti | Sıfır (boş struct). | Ucuz (pointer + atomik ref_count artışı). |
| Thread-safety | Zaten stateless, doğal olarak güvenli. | Sadece `KNST_MEMORY_POOL_USE_MUTEX` tanımlıysa güvenli. |