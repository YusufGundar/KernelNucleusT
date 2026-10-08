# knst_c16string — UTF-16 String Sınıfı

Selamlar! 👋 Bu doküman `knst_c16string` sınıfının ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de kullanıldığını açıklar

Kısaca: **UTF-16 tabanlı, SSO (small string optimization) ve COW (copy-on-write) destekli, allocator-aware bir string sınıfı.** Özellikle Windows API'leri ve Unicode metinlerle çalışırken hayat kurtarır

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **UTF-16 tabanlı** | Windows API'leriyle doğrudan uyumlu, POSIX'te UTF-8'e kolayca çevrilebilir |
| **SSO** | Küçük stringler (varsayılan olarak 10 karaktere kadar) stack'te, heap allocation yok |
| **COW** | Kopyalama anında veri kopyalanmaz, sadece referans sayacı artar |
| **Allocator-aware** | Pool allocator ile küçük string'ler için ciddi hız kazancı |
| **`[[no_unique_address]]`** | Boş allocator kullanırsan sıfır ek yük (C++20) |
| **Zengin constructor seti** | `char16_t*`, `char*` (UTF-8), `wchar_t*`, `char32_t*`, `std::string`, `initializer_list`, sayılar, `float`, `double` |
| **Hızlı `find`** | Kısa desenler için naive, uzun desenler için Two-Way algoritması |
| **STL uyumlu** | Iterator'ler, ostream operatörleri, `std::string` dönüşümü |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Boş string
    knst_c16string s1;

    // char16_t* ile
    knst_c16string s2 = u"Merhaba Dünya";

    // UTF-8'den (POSIX path, JSON, vs.)
    knst_c16string s3 = "Dosya yolu: /home/knst";

    // Geniş karakterden (Windows API)
    knst_c16string s4 = L"Windows stili";

    // UTF-32'den (emoji, nadir karakter)
    knst_c16string s5 = U"Merhaba 🌍";

    // Sayıdan
    knst_c16string s6 = 42;
    knst_c16string s7 = 3.14;

    // Tekrar eden karakter
    knst_c16string s8(5, u'a');   // "aaaaa"

    // STL string'den
    std::string std_str = "hello";
    knst_c16string s9 = std_str;

    // initializer_list
    knst_c16string s10 = {u'H', u'e', u'l', u'l', u'o'};

    std::cout << s2 << "\n";      // "Merhaba Dünya"
    return 0;
}
```

---

## 🏗️ Constructor'lar

### Ham String'ler

```cpp
knst_c16string a;                    // boş
knst_c16string b(u"UTF-16 metin");   // char16_t*
knst_c16string c(u"UTF-16", 6);      // uzunluk verilmiş
knst_c16string d("UTF-8 metin");     // UTF-8'den çevrim
knst_c16string e(L"wchar_t metin");  // wchar_t*
knst_c16string f(U"UTF-32 metin");   // char32_t*
```

### Sayılar

```cpp
knst_c16string n1 = 42;
knst_c16string n2 = -123;
knst_c16string n3 = 100L;
knst_c16string n4 = 999999999ULL;
knst_c16string n5 = 3.14f;
knst_c16string n6 = 2.718281828;
```

### Tekrarlı String'ler

```cpp
knst_c16string a(5, u'a');      // "aaaaa"
knst_c16string b(3, u"ab");     // "ababab" (exponential replication)
knst_c16string c(4, 'x');       // "xxxx"
knst_c16string d(2, L"mer");    // "mermer"
```

### STL Tipleri

```cpp
std::string     s1 = "hello";
std::wstring     s2 = L"world";
std::u16string     s3 = u"u16";
std::u32string      s4 = U"u32";
std::string_view     sv = "view";
std::vector<char>    vec = {'a','b','c'};

knst_c16string k1 = s1;
knst_c16string k2 = s2;
knst_c16string k3 = s3;
knst_c16string k4 = s4;
knst_c16string k5 = sv;
knst_c16string k6 = vec;
```

### Allocator'lı Kullanım

```cpp
knst_pool_allocator pool;
knst_c16string_sm<> s(pool);   // pool allocator ile
```

`knst_c16string_sm<>` = `basic_c16string<knst_pool_allocator>`. Küçük string'ler için hızlı, pool'dan allocation yapar.

---

## 📝 Temel Metodlar

### Okuma

```cpp
knst_c16string s = u"Merhaba";

s.data();      // const char16_t* — ham pointer
s.length();    // karakter sayısı (null hariç)
s.capacity();  // kapasite
s.empty();     // true/false
s.is_heap();   // heap'te mi, SSO'da mı?
```

### Yazma / Değiştirme

```cpp
knst_c16string s;

s.append(u"Dünya");          // char16_t*
s.append("UTF-8 metin");     // UTF-8'den
s.append(L"geniş");          // wchar_t*
s.append(U"UTF-32");         // char32_t*
s.append(42);                // int
s.append(3.14);              // double
s.append(u'!');              // tek karakter

s.clear();                   // temizle
s.resize(20);                // 20 karaktere getir
s.resize(20, u'x');          // 20 karakter, kalanları 'x' ile doldur
s.reserve(100);              // en az 100 karakterlik kapasite
s.shrink_to_fit();           // kapasiteyi length+1'e indir
```

### Erişim

```cpp
knst_c16string s = u"Merhaba";

s[0];         // 'M' (non-const: COW detach yapar)
s.at(0);      // 'M' + bounds check (debug'da assert)
s.front();    // ilk karakter
s.back();     // son karakter

// const versiyonlar — detach yapmaz
const knst_c16string& cs = s;
cs[0];
cs.front();
```

### Alt String

```cpp
knst_c16string s = u"Merhaba Dünya";

knst_c16string sub  = s.substr(0, 7);    // "Merhaba"
knst_c16string tail = s.substr(8, 100);  // "Dünya" (kırpılır)
knst_c16string empty = s.substr(100);    // boş string
```

---

## 🔍 Arama — `find`, `contains`, `starts_with`, `ends_with`

### `find` / `contains`

Kısa desenler için **naive** algoritma (≤8 karakter), uzun desenler için **Two-Way** algoritması kullanılır. İkisi de **O(n)** garantili.

```cpp
knst_c16string text = u"Merhaba Dünya";

text.find(u"Mer");         // true
text.find("Dünya");        // UTF-8'den
text.find(L"a");           // wchar_t
text.find(u'ü');           // tek karakter
text.find(u"xyz");         // false

// Offset ile
text.find(u"a", 5);        // 5. karakterden sonra ara

// contains = find'in alias'ı (okunabilirlik için)
if (text.contains(u"Merhaba")) {
    // ...
}
```

### `starts_with` / `ends_with`

```cpp
knst_c16string path = u"/home/user/file.txt";

path.starts_with(u"/home");     // true
path.ends_with(u".txt");        // true
path.ends_with("txt");          // UTF-8 ile de çalışır
path.starts_with(u'/');         // tek karakter
path.ends_with(L't');           // wchar_t
```

**Performans notu:**
- `char16_t*` versiyonu `memcmp` kullanır → çok hızlı
- Diğer tipler önce UTF-16'ya çevrilir (kısa string'ler için stack buffer)

---

## ⚙️ Operatörler

### Karşılaştırma

Tüm karşılaştırma operatörleri mevcut — hem `basic_c16string` hem ham pointer tipleriyle:

```cpp
knst_c16string a = u"apple";
knst_c16string b = u"banana";

a == b;      // false
a != b;      // true
a < b;       // true (lexicographic)
a <= b;      // true
a > b;       // false
a >= b;      // false

// Ham pointer'larla
a == u"apple";       // true
a < "banana";        // UTF-8 ile de çalışır
L"cherry" > a;       // wchar_t solda
```

### Birleştirme

```cpp
knst_c16string s = u"Merhaba";
s += u" Dünya";      // "Merhaba Dünya"
s += '!';            // tek karakter
s += 42;             // sayı
s += L" (wchar)";    // wchar_t

knst_c16string greeting = u"Sayın " + u"Yusuf";    // + operatörü
knst_c16string mixed    = u"Sonuç: " + 42;         // sayı ile
knst_c16string fromChar = 'X' + u"marks";          // char solda
```

### Stream Operatörleri

```cpp
knst_c16string s = u"Merhaba";

std::cout  << s;    // UTF-8'e çevirip yazar
std::wcout << s;    // geniş stream'e yazar
```

---

## 🔁 Iterator'ler

Standart STL uyumlu iterator'ler:

```cpp
knst_c16string s = u"Merhaba";

for (auto it = s.begin(); it != s.end(); ++it) {
    char16_t c = *it;
}

// const versiyonları
for (auto it = s.cbegin(); it != s.cend(); ++it) {
    // okuma
}

// Range-based for
for (char16_t c : s) {
    // ...
}

// Non-const iterator'la değiştirme
for (auto& ch : s) {
    ch = static_cast<char16_t>(ch - 32);   // ASCII lowercase → uppercase
}
```

**Not:** Non-const `begin()` / `end()` çağırmak **COW detach** tetikler. Bu kasıtlıdır çünkü iterator üzerinden yazma yapabileceğin varsayılır.

### Iterator Sınıfları

İki iterator sınıfı var:

| Sınıf | Ne işe yarar? |
|---|---|
| **`knst_iterator<T>`** | Mutable (yazılabilir) random access iterator |
| **`knst_const_iterator<T>`** | Const (sadece okunabilir) random access iterator |

İkisi de `std::vector<T>::iterator` ile birebir aynı davranışa sahip. Tüm random access operatörlerini destekler: `*`, `->`, `[]`, `++`, `--`, `+=`, `-=`, `+`, `-`, `==`, `!=`, `<`, `>`, `<=`, `>=`.

**Mutable → Const otomatik dönüşüm:**

```cpp
knst_iterator<int> it = v.begin();              // mutable
knst_const_iterator<int> cit = it;              // ✅ otomatik dönüşüm
knst_const_iterator<int> cit2 = v.cbegin();     // ✅ doğrudan
```

Bu sayede `v.begin()`'i `const_iterator` bekleyen fonksiyonlara verebilirsin (tıpkı `std::vector`'da olduğu gibi).

---

## 🎛️ COW (Copy-on-Write) Mekanizması

### Nasıl Çalışır?

```cpp
knst_c16string a = u"uzun bir string burada";   // heap allocation

knst_c16string b = a;   // ⚡ Sadece referans sayacı 2 oldu, veri kopyalanmadı!

b.append(u"!");         // 🔄 Şimdi detach oldu, b kendi kopyasını aldı
                        //    a hâlâ eski verisini gösteriyor

// a: "uzun bir string burada"
// b: "uzun bir string burada!"
```

### Ne Zaman Faydalı?

- **Parametre geçirme** — fonksiyonlara by-value verebilirsin, kopyalama bedava
- **Container kullanımı** — `std::vector<knst_c16string>` içine eklemek ucuz
- **Fonksiyonel stil** — `auto copy = original;` anında ve ucuz

### Ne Zaman Kapatmalısın?

Multithreaded kodda COW **tehlikelidir** çünkü referans sayacı aynı anda iki thread tarafından değiştirilebilir. Bunun için:

```cpp
#define KNST_C16_STRING_USING_ATOMIC_COW
```

Bu makro referans sayacını `std::atomic` yapar.

**Tamamen kapatmak için:**

```cpp
#define KNST_C16STRING_DEACTIVE_COW
```

Her kopya derin kopya olur.

---

## 🧮 SSO (Small String Optimization)

Küçük stringler heap'e gitmez — sınıfın içindeki **union** içinde saklanır.

| Ayar | SSO Kapasitesi | Hizalama |
|---|---|---|
| Varsayılan | **10 karakter** | `alignas(8)` |
| `KNST_C16STRING_ALIGN_32` | **14 karakter** | `alignas(32)` |
| `KNST_C16STRING_ALIGN_64` | **30 karakter** | `alignas(64)` |

**Örnek:** Varsayılan ayarda `knst_c16string(u"Merhaba")` (7 karakter) heap'e hiç gitmez.

### Hangi Ayarlamayı Seçmelisiniz?

- **Varsayılan (10)** — genel amaçlı, en dengeli
- **`_ALIGN_32` (14)** — dosya yolları, kısa kullanıcı adları
- **`_ALIGN_64` (30)** — uzun metinler, cache-line hizalı erişim

**Not:** `alignas` değeri büyüdükçe sınıfın `sizeof`'u büyür. 64-byte hizalama = her string 64 byte yer kaplar.

---

## 🧵 Thread Safety

Varsayılan olarak `knst_c16string` **thread-safe değildir.** Çünkü:

- COW referans sayacı atomic değil
- SSO verisi sınıf içinde, paylaşılırsa yarış olur

**Thread-safe yapmak için:**

```cpp
#define KNST_C16_STRING_USING_ATOMIC_COW
```

Bu, COW sayacını atomic yapar. Ama dikkat: **hâlâ aynı string'i iki thread'den aynı anda yazamazsın.** Sadece kopyalama/okuma güvenli olur.

---

## 📊 Performans Notları

| İşlem | Karmaşıklık | Not |
|---|---|---|
| Kısa string (< SSO) oluşturma | **O(1)** | Heap yok |
| Kopyalama (COW açıkken) | **O(1)** | Sadece ref count++ |
| Yazma (COW detach) | **O(n)** | İlk yazmada kopyalar |
| `append` | **Amortized O(1)** | Kapasite 2x büyür |
| `find` (kısa desen) | **O(n)** | Naive |
| `find` (uzun desen) | **O(n)** | Two-Way, garantili |
| `starts_with` / `ends_with` | **O(m)** | `m` = prefix/suffix uzunluğu |
| `substr` | **O(m)** | Yeni string oluşturur |

### Ne Zaman Yavaş?

- **Sık sık yazma + COW açık** — her yazma detach tetikleyebilir
- **Çok uzun string'ler** — heap'e gider, `realloc` maliyetli
- **`char16_t` dışı tiplerle `starts_with`** — encoding dönüşümü maliyetli

---

## 🌍 Platform Desteği

| Platform | Davranış |
|---|---|
| **Windows** | `wchar_t` = UTF-16 → `to_wchar_alloc()` doğrudan `memcpy`, çok hızlı |
| **Linux / macOS** | `wchar_t` = UTF-32 → UTF-16'dan dönüşüm gerekir |
| **Android** | Windows gibi (`wchar_t` 32-bit ama UTF-16 kullanıyoruz) |

### Dosya Yolu İşlemleri

```cpp
#ifdef KNST_USING_PLATFORM_WINDOWS
    // UTF-16 doğrudan Windows API'ye geçirilebilir
    knst_c16string path = u"C:\\Users\\Yusuf\\dosya.txt";
    // Windows API çağrısı...
#else
    // POSIX: UTF-8'e çevir
    knst_c16string path = u"/home/knst/dosya.txt";
    char* utf8_path = path.to_char();   // kullan, sonra free et
#endif
```

### UTF-8 Dönüşümü

```cpp
knst_c16string s = u"ünïcödé";

// UTF-16 → UTF-8 (caller free etmeli)
char* u8 = s.to_char();
if (u8) {
    std::cout << u8 << "\n";
    knst_default_allocator().deallocate(u8, std::strlen(u8) + 1);
}

// UTF-16 → wchar_t* (caller free etmeli)
wchar_t* w = s.to_wchar_alloc();
if (w) {
    knst_c16string round_trip(w);
    knst_default_allocator().deallocate(w, (s.length() + 1) * sizeof(wchar_t));
}
```

---

## 🧠 Pool Allocator ile Kullanım (`knst_c16string_sm`)

`knst_pool_allocator`, küçük allocation'ları **önceden ayrılmış bloklardan** verir. Çok sayıda kısa string oluşturup yok ediyorsan (parser, JSON işleme, dosya yolu listesi) **ciddi hız kazancı** sağlar.

### Varsayılan Pool ile

```cpp
#include "KernelNucleusT.hpp"

// knst_c16string_sm = basic_c16string<knst_pool_allocator>
knst_c16string_sm<> s = u"kısa metin";

std::cout << "pool_count     : " << s.pool_count() << "\n";       // 4
std::cout << "max_block_size : " << s.max_block_size() << "\n";   // 2048
```

Varsayılan olarak 4 boyut vardır: **64, 256, 1024, 2048 byte**.

### Kendi Pool Boyutlarınla

```cpp
// Sadece belirli boyutları kullan
knst_pool_allocator pool(128, 512, 4096);

knst_c16string_sm<> s(pool);
s.append(u"merhaba");

std::cout << "pool_count     : " << s.pool_count() << "\n";       // 3
std::cout << "max_block_size : " << s.max_block_size() << "\n";   // 4096
```

### Size + Block Count Kontrolü

```cpp
// Her pool için blok sayısını da sen belirle
knst_pool_allocator pool(
    knst_pool_config(128, 256),   // 128-byte blok × 256 adet
    knst_pool_config(1024, 64)    // 1024-byte blok × 64 adet
);

knst_c16string_sm<> s(pool);
```

### Ne Zaman Pool Kullanmalı?

| Durum | Pool kullan? |
|---|---|
| Çok sayıda kısa string (parser, tokenizer) | ✅ Evet, ciddi kazanç |
| Tek tük string oluşturma | ❌ Gereksiz |
| Thread-safe gerekiyorsa | ✅ Ama `KNST_MEMORY_POOL_USE_MUTEX` tanımla |
| Uzun string'ler (2048+ byte) | ⚠️ Pool devreye girmez, `malloc`'a düşer |

### Pool ve COW Birlikte

```cpp
knst_c16string_sm<> a = u"uzun bir metin...";
knst_c16string_sm<> b = a;       // COW: kopyalama yok, sadece ref count++
b.append(u"!");                  // Detach: pool'dan yeni blok alır
```

Pool allocator ve COW birbirini çok iyi tamamlar — COW sayesinde kopyalama ucuz, pool sayesinde detach sırasındaki allocation hızlı.

---

## 🔤 Global UTF Dönüşüm Fonksiyonları

`knst_c16string`'in altında yatan encoding dönüşümleri `knst_global_functions.hpp` içinde tanımlıdır. Bunları **doğrudan** da çağırabilirsin — örneğin bir buffer'ı dönüştürmek için string nesnesi oluşturmadan.

### Dönüşüm Fonksiyonları

| Fonksiyon | Ne yapar? |
|---|---|
| `knst_convert_utf8_to_utf16(src, src_len, dst)` | UTF-8 → UTF-16 |
| `knst_convert_utf16_to_utf8(src, src_len, dst)` | UTF-16 → UTF-8 |
| `knst_convert_wchar_to_utf16(src, src_len, dst)` | wchar_t → UTF-16 (Windows'ta doğrudan, POSIX'te UTF-32'den) |
| `knst_convert_utf16_to_wchar(src, src_len, dst)` | UTF-16 → wchar_t |
| `knst_convert_char32_to_utf16(src, src_len, dst)` | UTF-32 → UTF-16 (surrogate pair'ler dahil) |

### Uzunluk Hesaplama Fonksiyonları

Dönüşüm öncesi **kaç karakter / byte gerektiğini** öğrenmek için:

| Fonksiyon | Ne döner? |
|---|---|
| `knst_get_utf8_to_utf16_exact_length(str, byte_count)` | UTF-16 karakter sayısı |
| `knst_get_utf16_to_utf8_exact_byte_size(str, src_len)` | UTF-8 byte sayısı |
| `knst_get_wchar_to_utf16_exact_length(str, count)` | UTF-16 karakter sayısı |
| `knst_get_char32_to_utf16_exact_length(str, count)` | UTF-16 karakter sayısı |

### C String Uzunluğu

```cpp
knst_get_str_length(u"merhaba");   // char16_t* → 7
knst_get_str_length("merhaba");    // char* (UTF-8) → 7
knst_get_str_length(L"merhaba");   // wchar_t* → 7
knst_get_str_length(U"merhaba");   // char32_t* → 7
```

### Örnek: Manuel Dönüşüm

```cpp
#include "KernelNucleusT.hpp"

const char* utf8 = "Merhaba Dünya 🌍";
uint32_t utf8_bytes = std::strlen(utf8);

// 1. UTF-16 için kaç karakter gerektiğini hesapla
uint32_t utf16_len = knst_get_utf8_to_utf16_exact_length(utf8, utf8_bytes);

// 2. Buffer ayır (stack veya heap)
std::vector<char16_t> buffer(utf16_len + 1);

// 3. Dönüştür
uint32_t written = knst_convert_utf8_to_utf16(utf8, utf8_bytes, buffer.data());
buffer[written] = u'\0';

// 4. Doğrudan kullan
std::wcout << reinterpret_cast<const wchar_t*>(buffer.data()) << L"\n";
```

### Ne Zaman Doğrudan Çağırmalısın?

- **Buffer'ı sen yönetiyorsan** — string nesnesi oluşturmadan dönüşüm
- **Performans kritikse** — iki adımlı allocation'dan kaçınmak için
- **Boyutu önceden biliyorsan** — gereksiz heap tahsisinden kaçınmak için

Normal kullanımda bunları **doğrudan çağırmana gerek yok** — `knst_c16string` constructor'ları zaten otomatik yapıyor. Ama alt seviye kontrol istiyorsan, kapı açık. 👍

### Performans Notu

Bu fonksiyonlar **SIMD benzeri döngü kullanır** — 4 karakteri bir seferde işleyerek dallanma (branch) maliyetini azaltır:

```cpp
while (i + 3 < src_len) {
    uint8_t c0 = src[i], c1 = src[i+1], c2 = src[i+2], c3 = src[i+3];
    if ((c0 | c1 | c2 | c3) < 0x80) {   // 4 karakter birden ASCII mi?
        // hızlı yol: hepsini doğrudan kopyala
    }
    // yavaş yol: tek tek işle
}
```

Bu teknik özellikle **çoğunlukla ASCII olan metinlerde** ciddi hız kazancı sağlar (JSON, XML, kaynak kodu — hepsi tipik olarak %90+ ASCII'dir).

---

## 📚 Tam Örnek

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // 1. Farklı kaynaklardan oluşturma
    knst_c16string a = u"Merhaba";
    knst_c16string b = "Dünya";         // UTF-8'den
    knst_c16string c = 2026;            // int
    knst_c16string d(5, u'*');          // "*****"

    // 2. Birleştirme
    knst_c16string greeting = a + u", " + b + u"!";
    std::cout << greeting << "\n";

    // 3. Arama
    if (greeting.contains(u"Merhaba")) {
        std::cout << "Selam var\n";
    }

    // 4. Alt string
    knst_c16string sub = greeting.substr(0, 7);
    std::cout << "sub = " << sub << "\n";

    // 5. COW kopyalama
    knst_c16string original = u"uzun metin burada";
    knst_c16string copy = original;      // paylaşımlı buffer
    copy.append(u" [değişti]");          // detach tetikler

    std::cout << "original: " << original << "\n";
    std::cout << "copy    : " << copy << "\n";

    // 6. Pool allocator
    knst_c16string_sm<> fast = u"pool'dan string";
    std::cout << "pool count: " << fast.pool_count() << "\n";

    return 0;
}
```

---

## ⚙️ Tüm Ayar Makroları

`knst_settings.hpp` içine koy, **kütüphaneyi kullanmadan önce**:

| Makro | Varsayılan | Ne yapar? |
|---|---|---|
| `KNST_C16STRING_DEACTIVE_COW` | Tanımsız | COW'u kapatır, her kopya derin kopya olur |
| `KNST_C16_STRING_USING_ATOMIC_COW` | Tanımsız | COW sayacını atomic yapar |
| `KNST_C16STRING_ALIGN_32` | Tanımsız | SSO'yu 32-byte'a hizala (14 karakter) |
| `KNST_C16STRING_ALIGN_64` | Tanımsız | SSO'yu 64-byte'a hizala (30 karakter) |
| `KNST_MEMORY_POOL_USE_MUTEX` | Tanımsız | Pool allocator'ı thread-safe yapar |

---

## 💡 Kullanım İpuçları

### ✅ Yapılması Gerekenler

```cpp
// 1. UTF-8 dönüşümlerini sınıfa bırak
knst_c16string path = get_utf8_path();  // otomatik çevirir

// 2. Parametre geçirirken by-value kullan (COW sayesinde ucuz)
void process(knst_c16string text);   // ✅ iyi — kopyalama ucuz

// 3. Sabit string'ler için u"" kullan
if (path.starts_with(u"/home/")) { ... }   // ✅ hızlı (memcmp)

// 4. Pool allocator'ı çok kısa string'lerde kullan
knst_c16string_sm<> token = tokenize_next();
```

### ❌ Kaçınılması Gerekenler

```cpp
// 1. COW'lu string'i thread'ler arası paylaşma
// ✅ Çözüm: KNST_C16_STRING_USING_ATOMIC_COW tanımla

// 2. Sürekli tek karakter append yapma
for (char c : str) {
    s.append(c);   // ❌ yavaş, her seferinde kontrol
}
// ✅ Çözüm: Önce reserve, sonra toplu append

// 3. Uzun string'lerde char tabanlı starts_with
s.starts_with("çok uzun bir utf8 prefix");   // ❌ her çağrıda dönüşüm
// ✅ Çözüm: Bir kez knst_c16string'e çevir, onu kullan

// 4. to_char() sonucunu free etmeyi unutma
char* p = s.to_char();
// ... kullan ...
// ❌ free etmedin! Memory leak.
```

---

## 🎯 Size Getireceği Kazanç

`knst_c16string` şu durumlarda ciddi avantaj sağlar:

- ✅ **Windows API'leriyle çalışıyorsan** — dosya yolları, mesaj kutuları, kayıt defteri
- ✅ **Unicode metin işliyorsan** — emoji, çok dilli içerik, Arapça/Hintçe gibi scriptler
- ✅ **Performans kritikse** — SSO/COW sayesinde kısa string'ler bedava, kopyalama ucuz
- ✅ **Aynı string'i çok kopyalıyorsan** — COW sayesinde kopyalama neredeyse bedava
- ✅ **Kütüphane içindeki karmaşık yapıları kullanıyorsan** — `knst_window`, `knst_file`, `knst_display` gibi sınıflar `knst_c16string` bekliyor
