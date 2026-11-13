# knst_vector — Dinamik Dizi Sınıfı

Selamlar! 👋 Bu doküman `knst_vector` sınıfının ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **`std::vector`'e benzer ama allocator-aware, `[[no_unique_address]]` destekli, Exception-free bir dinamik dizi sınıfı.** Custom allocator (pool allocator dahil) kullanabilir, `bool` döner hata yerine exception fırlatmaz, ve pool allocator ile küçük vektörler için ciddi hız kazancı sağlar.

---

## 🎯 Neden Bu Sınıf Var?

`std::vector` C++ standardının temel taşı ama bazı eksikleri var:

- **Exception fırlatır** — `bad_alloc` yakalamak zorunda kalırsın
- **Allocator propagation karmaşık** — hangi durumda kopyalanır, hangi durumda taşınır belirsiz
- **Pool allocator desteği yok** — her `push_back` `malloc` çağırır
- **`[[no_unique_address]]` kullanmaz** — boş allocator bile yer kaplar

`knst_vector` bunları çözer:

- ✅ **Exception-free** — hata durumunda `false` döner
- ✅ **Allocator-aware** — `knst_pool_allocator` ile ciddi hız kazancı
- ✅ **`[[no_unique_address]]`** — boş allocator 0 byte
- ✅ **`_sm` suffix** — pool-backed versiyon hazır
- ✅ **Bridge memory** — farklı allocator'lara taşıma
- ✅ **Custom iterator** — `knst_iterator` ile uyumlu
- ✅ **`emplace_back`** — perfect forwarding

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Exception-free** | `bad_alloc` yerine `false` döner |
| **`[[no_unique_address]]`** | Boş allocator sıfır yer kaplar (C++20) |
| **Pool allocator** | Küçük vektörler için ciddi hız kazancı |
| **`_sm` versiyon** | `knst_vector_sm<T>` pool-backed |
| **`emplace_back`** | In-place construction, kopyasız |
| **Self-reference safe** | `v.push_back(v[0])` güvenli |
| **Rich insert/erase** | Iterator, index, range, count — hepsi var |
| **`find` / `erase_value`** | Değer bazlı arama/silme |
| **`shrink_to_fit`** | Kapasiteyi küçült, bellek tasarruf |
| **Bridge memory** | Farklı allocator'a taşıma |
| **Custom iterator** | STL uyumlu, random access |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Boş vektör
    knst_vector<int> v;

    // Eleman ekle
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    // Dolaş
    for (int x : v) {
        std::cout << x << " ";   // 10 20 30
    }
    std::cout << "\n";

    return 0;
}
```

---

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Constructor** | Default, `(count, value)`, `initializer_list`, `(ptr, count)`, copy, move |
| **Ekleme** | `push_back`, `emplace_back`, `insert` (6 farklı overload) |
| **Silme** | `pop_back`, `erase` (iterator/index/range), `erase_value` |
| **Erişim** | `operator[]`, `data`, `back` |
| **Arama** | `find` |
| **Boyut** | `size`, `capacity`, `empty` |
| **Değiştirme** | `resize`, `reserve`, `shrink_to_fit`, `clear`, `assign` |
| **Karşılaştırma** | `==`, `!=` |
| **Iterator** | `begin`, `end`, `cbegin`, `cend` |
| **Allocator** | `bridge_memory` (3 overload) |

---

## 🏗️ 1) Constructor'lar

### Default Constructor

```cpp
knst_vector<int> v;   // boş, kapasite 0
```

### `(count, value)` — N Adet Kopya

```cpp
knst_vector<int> v(5, 42);   // {42, 42, 42, 42, 42}
```

### `initializer_list` — Süslü Parantez

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
```

### `(ptr, count)` — C Array'den

```cpp
const int raw[] = {10, 20, 30};
knst_vector<int> v(raw, 3);   // {10, 20, 30}
```

### Copy / Move

```cpp
knst_vector<int> a = {1, 2, 3};

knst_vector<int> b = a;              // kopya
knst_vector<int> c = std::move(a);   // move — a boşalır

std::cout << a.size() << "\n";   // 0
std::cout << c.size() << "\n";   // 3
```

### Allocator'lı Kullanım

```cpp
knst_pool_allocator pool;
knst_vector<int> v(pool);   // pool kullanır
```

---

## 📥 2) Eleman Ekleme

### `push_back(value)` — Sona Ekle

```cpp
knst_vector<int> v;

v.push_back(10);
v.push_back(20);
v.push_back(30);

// {10, 20, 30}
```

**Kopya:** Değer kopyalanır (`const T&` overload)
**Move:** `std::move` ile taşınır (`T&&` overload)

### `emplace_back(args...)` — Yerinde Üret

```cpp
struct Point { int x, y; Point(int x, int y) : x(x), y(y) {} };

knst_vector<Point> v;
v.emplace_back(10, 20);   // doğrudan Point(10, 20) oluşturur — kopya YOK
```

**Kazanç:** Geçici nesne oluşturulmaz, doğrudan hedef bellekte inşa edilir.

### `insert(pos, value)` — Araya Ekle

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};

// Iterator pozisyonuna
v.insert(v.begin() + 2, 99);   // {1, 2, 99, 3, 4, 5}

// Index pozisyonuna
v.insert(0, 0);   // {0, 1, 2, 99, 3, 4, 5}
```

### `insert(pos, count, value)` — N Adet Ekle

```cpp
knst_vector<int> v = {1, 2, 3};
v.insert(v.begin(), 3, 0);   // {0, 0, 0, 1, 2, 3}
```

### `insert(pos, first, last)` — Range Ekle

```cpp
knst_vector<int> src = {10, 20, 30};
knst_vector<int> dst = {1, 2};

dst.insert(dst.end(), src.begin(), src.end());
// {1, 2, 10, 20, 30}
```

### `insert(pos, initializer_list)` — Süslü Parantez Ekle

```cpp
knst_vector<int> v = {1, 2};
v.insert(v.end(), {3, 4, 5});   // {1, 2, 3, 4, 5}
```

**Not:** Tüm `insert` overload'ları self-reference korumalıdır:

```cpp
knst_vector<int> v = {1, 2, 3};
v.insert(v.begin(), v[0]);   // ✅ güvenli — kopya alınır
```

---

## 📤 3) Eleman Silme

### `pop_back()` — Sondan Sil

```cpp
knst_vector<int> v = {1, 2, 3};
v.pop_back();   // {1, 2}
```

**Not:** Kapasite **değişmez**, sadece `size` azalır.

### `erase(pos)` — Iterator ile Sil

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
auto it = v.erase(v.begin() + 2);   // 3 silindi
std::cout << *it << "\n";   // 4
// {1, 2, 4, 5}
```

**Dönüş:** Silinen elemanın yerini gösteren iterator.

### `erase(index)` — Index ile Sil

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
v.erase(1);   // index 1 = 2 silindi
// {1, 3, 4, 5}
```

### `erase(first, last)` — Range Sil

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
v.erase(v.begin(), v.begin() + 3);   // ilk 3 eleman silindi
// {4, 5}
```

### `erase_value(value)` — Değer ile Sil

```cpp
knst_vector<int> v = {10, 20, 30, 40, 30};
bool removed = v.erase_value(30);   // ilk eşleşeni sil
// {10, 20, 40, 30}
```

**Dönüş:** `true` = bulundu ve silindi, `false` = bulunamadı.

### `clear()` — Hepsini Sil

```cpp
knst_vector<int> v = {1, 2, 3};
v.clear();
// size = 0, capacity değişmedi
```

---

## 🔍 4) Erişim

### `operator[]` — Hızlı Erişim (Sınır Kontrolü YOK)

```cpp
knst_vector<int> v = {100, 200, 300};

int x = v[0];    // 100
v[1] = 999;      // değiştir
```

**⚠️ Uyarı:** `v[100]` gibi geçersiz index **undefined behavior**'dır. Sınır kontrolü yok.

### `data()` — Ham Pointer

```cpp
knst_vector<int> v = {1, 2, 3};
int* ptr = v.data();
// ptr[0] == 1, ptr[1] == 2, ptr[2] == 3
```

**Kullanım alanı:** C API'lerine geçirme, pointer aritmetiği.

### `back()` — Son Eleman

```cpp
knst_vector<int> v = {10, 20, 30};
int last = v.back();   // 30
v.back() = 99;         // değiştir
```

**⚠️ Uyarı:** Boş vektörde `back()` **undefined behavior**.

### `front()` — İlk Eleman

**Not:** `front()` metodu **yok**. Bunun yerine `v[0]` veya `v.data()[0]` kullan.

---

## 🔎 5) Arama

### `find(value)` — Değer Ara

```cpp
knst_vector<int> v = {10, 20, 30, 40};

auto it = v.find(30);
if (it != v.end()) {
    std::cout << "Bulundu: index " << (it.get() - v.data()) << "\n";
} else {
    std::cout << "Bulunamadı\n";
}
```

**Dönüş:** Bulunduysa iterator, bulunamazsa `end()`.

---

## 📏 6) Boyut ve Kapasite

### `size()` — Eleman Sayısı

```cpp
knst_vector<int> v = {1, 2, 3};
std::cout << v.size() << "\n";   // 3
```

### `capacity()` — Kapasite

```cpp
knst_vector<int> v = {1, 2, 3};
std::cout << v.capacity() << "\n";   // 3 veya daha büyük
```

### `empty()` — Boş mu?

```cpp
if (v.empty()) { /* boş */ }
```

### Kapasite Büyümesi

`knst_vector` **2x growth** stratejisi kullanır:

| Başlangıç | push_back sonrası |
|---|---|
| 0 | 4 |
| 4 | 8 |
| 8 | 16 |
| 16 | 32 |
| ... | ... |

**Yani:** Amortized O(1) `push_back`.

---

## 🔧 7) Boyut Değiştirme

### `reserve(n)` — Kapasite Ayır

```cpp
knst_vector<int> v;
v.reserve(100);   // en az 100 elemanlık yer ayır
// size = 0, capacity >= 100
```

**Ne zaman kullan?** Kaç eleman olacağını biliyorsan, `push_back` sırasında reallocation olmasın.

### `resize(n)` — Boyutu Değiştir

```cpp
knst_vector<int> v = {1, 2, 3};

v.resize(6);      // {1, 2, 3, 0, 0, 0} — yeni yerler default
v.resize(8, 7);   // {1, 2, 3, 0, 0, 0, 7, 7}
v.resize(2);      // {1, 2}
```

**Kurallar:**
- `new_size < size` → fazla elemanlar silinir
- `new_size > size` → yeni elemanlar default (veya verilen değer) ile doldurulur
- `new_size > capacity` → otomatik `reserve` çağrılır

### `shrink_to_fit()` — Kapasiteyi Küçült

```cpp
knst_vector<int> v;
v.reserve(1000);
v.push_back(1);
v.push_back(2);
// size = 2, capacity = 1000

v.shrink_to_fit();
// size = 2, capacity = 2
```

**Kazanç:** Bellek tasarrufu. Büyük vektörlerde önemli.

### `assign(count, value)` — Baştan Doldur

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
v.assign(3, 7);
// {7, 7, 7}
```

### `assign(first, last)` — Range ile Doldur

```cpp
knst_vector<int> src = {10, 20, 30};
knst_vector<int> v;
v.assign(src.begin(), src.end());
// {10, 20, 30}
```

### `assign(initializer_list)` — Süslü Parantez

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};
v.assign({100, 200, 300});
// {100, 200, 300}
```

---

## 🔁 8) Iterator'ler

### Range-Based For

```cpp
knst_vector<int> v = {1, 2, 3, 4, 5};

for (int x : v) {
    std::cout << x << " ";
}
```

### Iterator Loop

```cpp
for (auto it = v.begin(); it != v.end(); ++it) {
    std::cout << *it << " ";
}
```

### Değiştirme

```cpp
// Non-const iterator/reference ile
for (auto& x : v) {
    x *= 10;
}
// {10, 20, 30, 40, 50}
```

### `const_iterator`

```cpp
const knst_vector<int>& cv = v;

for (auto it = cv.cbegin(); it != cv.cend(); ++it) {
    // okuma
}
```

### Iterator Türleri

- **`knst_iterator<T>`** — Mutable, random access
- **`knst_const_iterator<T>`** — Const, random access
- **Otomatik dönüşüm:** `iterator` → `const_iterator`

```cpp
knst_vector<int> v = {1, 2, 3};
knst_iterator<int> it = v.begin();
knst_const_iterator<int> cit = it;   // ✅ otomatik dönüşüm
```

---

## ⚖️ 9) Karşılaştırma

```cpp
knst_vector<int> a = {1, 2, 3};
knst_vector<int> b = {1, 2, 3};
knst_vector<int> c = {1, 2, 4};

a == b;   // true
a == c;   // false
a != c;   // true
```

**Not:** Sadece `==` ve `!=` var. `<`, `>`, `<=`, `>=` **desteklenmiyor** (lexicographic karşılaştırma yok).

---

## 🧠 10) Pool Allocator ile Kullanım

### Varsayılan Allocator (malloc)

```cpp
knst_vector<int> v;   // knst_default_allocator
```

### Pool Allocator — `_sm` Suffix

```cpp
knst_vector_sm<int> v;   // knst_pool_allocator
v.push_back(1);
v.push_back(2);
v.push_back(3);
```

**`_sm` = "small memory"** — pool'dan yer alır, hızlı ama büyük veriler için uygun değil.

### Karşılaştırma

| | `knst_vector<T>` | `knst_vector_sm<T>` |
|---|---|---|
| **Allocator** | `knst_default_allocator` (malloc) | `knst_pool_allocator` |
| **Küçük vektörler** | Yavaş (her seferinde malloc) | Hızlı (pool'dan) |
| **Büyük vektörler** | İyi | Pool limitini aşarsa malloc'a düşer |
| **Thread safety** | Güvenli | `KNST_MEMORY_POOL_USE_MUTEX` gerekir |

### Ne Zaman Pool Kullanmalı?

| Durum | Pool? |
|---|---|
| Çok sayıda küçük vektör | ✅ Evet |
| Parser, tokenizer | ✅ Evet |
| Oyun motoru, kısa-lived container | ✅ Evet |
| Tek tük büyük vektör | ❌ Hayır, malloc daha uygun |
| Thread'ler arası paylaşım | ⚠️ Mutex gerekir |

### Pool Boyutu Örneği

```cpp
knst_pool_allocator pool(128, 512, 4096);

knst_vector<int, knst_pool_allocator> v(pool);
v.push_back(1);
```

---

## 🌉 11) Bridge Memory — Allocator Değiştirme

Vektörü farklı bir allocator'a taşı:

```cpp
knst_vector<int> v = {1, 2, 3};

knst_pool_allocator pool;
if (v.bridge_memory(pool)) {
    // v artık pool allocator kullanıyor
}
```

**3 overload:**

```cpp
knst_pool_allocator pool;

// 1. lvalue
v.bridge_memory(pool);

// 2. const lvalue
const knst_pool_allocator cpool;
v.bridge_memory(cpool);

// 3. rvalue (geçici)
v.bridge_memory(knst_pool_allocator{});
```

**Ne yapar?**
1. Yeni allocator'dan yer ayırır
2. Eski elemanları **move** eder
3. Eski allocator'a iade eder
4. Yeni allocator'ı kaydeder

**Kısıt:** Allocator tipleri **aynı olmalıdır**. Aksi halde `false` döner.

---

## 🎁 12) `[[no_unique_address]]` — Sıfır Yer

`knst_vector` sınıfının içinde:

```cpp
[[no_unique_address]] mutable Allocator m_allocator;
```

**Ne demek?**
- **Boş allocator** (örn. `knst_default_allocator`) → **0 byte** yer
- **Dolu allocator** (örn. `knst_pool_allocator` = 8 byte pointer) → 8 byte

**Yani:** Varsayılan vektör için allocator **bedava**, pool vektörü için 8 byte.

---

## 📊 13) Performans Notları

| İşlem | Karmaşıklık | Not |
|---|---|---|
| `push_back` | **Amortized O(1)** | 2x growth |
| `pop_back` | **O(1)** | |
| `insert` (sona) | **Amortized O(1)** | push_back gibi |
| `insert` (başa) | **O(n)** | Shift gerekir |
| `erase` (sondan) | **O(1)** | |
| `erase` (baştan) | **O(n)** | Shift gerekir |
| `operator[]` | **O(1)** | |
| `find` | **O(n)** | Lineer arama |
| `reserve` | **O(n)** | Reallocation |
| `resize` | **O(n)** | |
| `shrink_to_fit` | **O(n)** | |

### Ne Zaman Yavaş?

- **Sık `insert` başa** — her seferinde shift
- **Sık `erase` baştan** — her seferinde shift
- **Rezerve etmeden 1M `push_back`** — çok reallocation
- **Büyük T tip** (ör. `knst_c16string`) — move maliyetli

### Optimizasyon İpuçları

```cpp
// ❌ Yavaş: Reserve yok
knst_vector<int> v;
for (int i = 0; i < 1000000; ++i) {
    v.push_back(i);   // ~20 reallocation
}

// ✅ Hızlı: Reserve
knst_vector<int> v;
v.reserve(1000000);
for (int i = 0; i < 1000000; ++i) {
    v.push_back(i);   // 0 reallocation
}
```

**Kazanç:** 5-10x hızlanma.

### `emplace_back` vs `push_back`

```cpp
struct Point { int x, y; Point(int x, int y) : x(x), y(y) {} };

// ❌ Kopya: geçici Point oluşturulur
v.push_back(Point(10, 20));

// ✅ Doğrudan: yerinde inşa
v.emplace_back(10, 20);
```

**Kazanç:** Kopya/taşıma maliyeti yok.

---

## 🛡️ 14) Exception Safety

### Tasarım Felsefesi: Exception-Free

`knst_vector` **exception fırlatmaz**. Hata durumunda `false` döner:

```cpp
knst_vector<int> v;

if (!v.push_back(42)) {
    std::cerr << "Ekleme başarısız — muhtemelen OOM\n";
}

if (!v.reserve(1000000)) {
    std::cerr << "Rezervasyon başarısız\n";
}
```

**Avantajı:**
- `try/catch` bloğu gerekmez
- Embedded/RT ortamlarda kullanılabilir
- `nothrow` garantisi

### Ne Zaman `false` Döner?

- **Out of Memory** — `allocate` başarısız
- **Çok büyük kapasite** — `uint32_t` limiti aşıldı
- **Pool tükendi** ve fallback başarısız

### `[[nodiscard]]` uyarısı?

Hayır, dönüş değerini kontrol etmek **senin sorumluluğunda**:

```cpp
v.push_back(42);   // ⚠️ dönüş kontrol edilmedi
// vs
if (!v.push_back(42)) { /* hata */ }   // ✅
```

---

## 📋 15) Tip Kısıtlamaları

`knst_vector<T>` için `T` şunları desteklemeli:

| Gereksinim | Neden? |
|---|---|
| **Copy veya Move constructor** | Eleman eklemede |
| **Destructor** | Temizlemede |
| **`operator==`** | `find`/`erase_value` için |
| **`operator!=`** | `operator!=` overload'u için |

**Not:** `trivially_copyable` **gerekli değil**. `knst_c16string` gibi karmaşık tiplerle çalışır.

### Desteklenen Tipler

```cpp
knst_vector<int> v1;
knst_vector<knst_c16string> v2;
knst_vector<knst_byte_string> v3;
knst_vector<knst_image> v4;
knst_vector<Point> v5;   // custom struct
```

### Desteklenmeyen Tipler

```cpp
// ❌ Referans tutamaz
knst_vector<int&> v;   // derlenmez

// ❌ Move-only tip (paylaşılacak elemanlar)
// Ancak emplace_back ile push_back(std::move(...)) çalışabilir
knst_vector<std::unique_ptr<int>> v;
v.push_back(std::make_unique<int>(42));   // ✅ move
```

---

## 🌍 16) Platform ve Allocator

### `knst_default_allocator`

- **Linux:** `malloc` / `free` / `realloc`
- **Windows:** `HeapAlloc` / `HeapFree` / `HeapReAlloc`
- **Özellik:** Zero-initialized memory döner

### `knst_pool_allocator`

- **Küçük bloklar:** Pool'dan (64, 256, 1024, 2048 byte)
- **Büyük bloklar:** `malloc` fallback
- **Referans sayımı:** Kopyalanınca aynı pool'u paylaşır
- **Thread safety:** Opsiyonel `KNST_MEMORY_POOL_USE_MUTEX`

---

## 💡 17) Kullanım İpuçları

### ✅ Yapılması Gerekenler

```cpp
// 1. Reserve et — büyüklüğü biliyorsan
v.reserve(1000);

// 2. emplace_back kullan — karmaşık tiplerde
v.emplace_back(arg1, arg2);

// 3. const reference ile dolaş — kopya yok
for (const auto& x : v) { /* ... */ }

// 4. Bool dönüşleri kontrol et
if (!v.push_back(42)) { /* hata */ }

// 5. Pool kullan — çok sayıda küçük vektörde
knst_vector_sm<int> small;
```

### ❌ Kaçınılması Gerekenler

```cpp
// 1. Sınır kontrolü olmadan index
v[1000];   // ❌ UB riski

// 2. Boş vektörde back/pop_back
knst_vector<int> empty;
empty.back();       // ❌ UB
empty.pop_back();   // ✅ güvenli (no-op)

// 3. Reserve olmadan 1M push_back
for (int i = 0; i < 1000000; ++i) v.push_back(i);   // ❌ yavaş

// 4. Dönüş değerini görmezden gelmek
v.push_back(42);   // ⚠️ OOM kontrolü yok

// 5. Self-reference olmadan insert
v.insert(v.begin(), v[0]);   // ✅ aslında güvenli (kopya alınır)
```

---

## 🔥 18) Gerçek Kullanım Örnekleri

### Örnek 1: Dinamik Liste

```cpp
knst_vector<knst_c16string> read_lines(const knst_c16string& path) {
    knst_vector<knst_c16string> lines;
    knst_file f = knst_file::open(path);
    f.read_file_lines(lines);
    return lines;
}
```

### Örnek 2: Batch Processing

```cpp
knst_vector<int> data;
data.reserve(10000);

for (int i = 0; i < 10000; ++i) {
    data.push_back(compute(i));
}

// İşle
for (int& x : data) x *= 2;
```

### Örnek 3: Filter

```cpp
knst_vector<int> filter_even(const knst_vector<int>& src) {
    knst_vector<int> result;
    result.reserve(src.size());

    for (int x : src) {
        if (x % 2 == 0) result.push_back(x);
    }
    return result;
}
```

### Örnek 4: Stack

```cpp
template<typename T>
class Stack {
    knst_vector<T> m_data;

public:
    void push(const T& value) { m_data.push_back(value); }
    void pop() { m_data.pop_back(); }
    T& top() { return m_data.back(); }
    bool empty() const { return m_data.empty(); }
    uint32_t size() const { return m_data.size(); }
};

Stack<int> s;
s.push(1);
s.push(2);
std::cout << s.top() << "\n";   // 2
s.pop();
```

### Örnek 5: Pool-Backed Cache

```cpp
class SmallObjectCache {
    knst_vector_sm<int> m_ids;

public:
    void add(int id) { m_ids.push_back(id); }

    bool contains(int id) {
        return m_ids.find(id) != m_ids.end();
    }

    void remove(int id) { m_ids.erase_value(id); }
};
```

### Örnek 6: Thread Pool Queue

```cpp
std::mutex mtx;
knst_vector<knst_function> queue;

void enqueue(knst_function fn) {
    std::lock_guard<std::mutex> lock(mtx);
    queue.push_back(std::move(fn));
}

void process_all() {
    knst_vector<knst_function> local;

    {
        std::lock_guard<std::mutex> lock(mtx);
        local = std::move(queue);
    }

    for (auto& fn : local) fn();
}
```

### Örnek 7: Matrix

```cpp
class Matrix {
    uint32_t rows, cols;
    knst_vector<double> data;

public:
    Matrix(uint32_t r, uint32_t c) : rows(r), cols(c) {
        data.resize(r * c, 0.0);
    }

    double& at(uint32_t r, uint32_t c) {
        return data[r * cols + c];
    }

    const double& at(uint32_t r, uint32_t c) const {
        return data[r * cols + c];
    }
};
```