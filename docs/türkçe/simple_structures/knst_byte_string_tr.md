# knst_byte_string — Binary-Safe Byte Container

Selamlar! 👋 Bu doküman `knst_byte_string` sınıfının ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de kullanıldığını açıklar.

Kısaca: **`unsigned char` tabanlı, binary-safe (gömülü null'ları koruyan), SSO destekli, allocator-aware bir byte container'ı.** Özellikle ağ protokolleri, dosya içerikleri ve UTF-8 metinlerle çalışırken hayat kurtarır.

---

## 🎯 Neden Bu Sınıf Var?

`std::string` çoğu durumda yeterlidir ama:

- **Binary-safe değil** — gömülü `\0` gördüğünde durur, `strlen` mantığı ile çalışır
- **`unsigned char` yok** — ağ paketleri, kriptografi, resim verileri hep `unsigned char`'dır
- **UTF-16'dan dönüşüm yok** — Windows API'leriyle çalışırken elle dönüşüm gerekir
- **`take_ownership` yok** — `new[]` ile ayrılmış bir buffer'ı sahiplenmek için ekstra kod

`knst_byte_string` bunların hepsini çözüyor. 🎉

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Binary-safe** | Gömülü `\0` byte'ları korur, veri kaybı olmaz |
| **`unsigned char` tabanlı** | Ağ paketleri, kriptografi, resim verileri için doğal tip |
| **SSO** | Küçük string'ler (varsayılan olarak 10 byte'a kadar) stack'te, heap allocation yok |
| **COW yok** | Her kopya kendi buffer'ına sahip — thread-safe, öngörülebilir |
| **Allocator-aware** | Pool allocator ile küçük byte string'ler için ciddi hız kazancı |
| **`[[no_unique_address]]`** | Boş allocator kullanırsan sıfır ek yük (C++20) |
| **Zengin constructor seti** | `char*`, `unsigned char*`, `char16_t*` (UTF-16 → UTF-8), `knst_c16string`, `initializer_list` |
| **`take_ownership`** | `new[]` ile ayrılmış bir buffer'ı sahiplenir, uygun şekilde siler |
| **STL uyumlu** | Iterator'ler, karşılaştırma operatörleri, stream operatörleri |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Boş
    knst_byte_string b1;

    // char* ile (strlen kullanır)
    knst_byte_string b2 = "hello";

    // Boyutlu ham veri (gömülü null destekler)
    const char raw[] = {'A', '\0', 'B', '\0', 'C'};
    knst_byte_string b3(raw, sizeof(raw));      // ✅ binary-safe

    // unsigned char* ile (ağ paketi)
    const unsigned char packet[] = {0xDE, 0xAD, 0xBE, 0xEF};
    knst_byte_string b4(packet, sizeof(packet));

    // UTF-16'dan UTF-8'e dönüşüm
    const char16_t utf16[] = u"Merhaba Dünya";
    knst_byte_string b5(utf16, 13);              // UTF-8 byte'ları

    // knst_c16string'den
    knst_c16string c16 = u"merhaba";
    knst_byte_string b6(c16);

    // initializer_list
    knst_byte_string b7 = {0x01, 0x02, 0x03, 0x04};

    // Çıktı (binary-safe, tam length() byte yazar)
    std::cout << b2 << "\n";                     // "hello"
    return 0;
}
```

---

## 🏗️ Constructor'lar

### Ham Veri

```cpp
knst_byte_string a;  // boş
knst_byte_string b("hello");  // char* (strlen)
knst_byte_string c("hello", 5); // char* + boyut
knst_byte_string d(data, size);  // unsigned char* + boyut
knst_byte_string e = {0xDE, 0xAD, 0xBE, 0xEF}; // initializer_list
```

### Encoding Dönüşümleri

```cpp
// UTF-16 → UTF-8
const char16_t* utf16 = u"Merhaba 🌍";
knst_byte_string s1(utf16, 10);

// knst_c16string → UTF-8 byte string
knst_c16string c16 = u"ünïcödé";
knst_byte_string s2(c16);
```

### Allocator'lı Kullanım

```cpp
knst_pool_allocator pool;
knst_byte_string_sm<> s(pool);   // pool allocator ile
```

`knst_byte_string_sm<>` = `basic_byte_string<knst_pool_allocator>`. Küçük byte string'ler için hızlı, pool'dan allocation yapar.

---

## 📝 Temel Metodlar

### Okuma

```cpp
knst_byte_string s = "merhaba";

s.data();      // const unsigned char* — ham pointer
s.length();    // byte sayısı (null hariç)
s.capacity();  // kapasite (byte)
s.empty();     // true/false
s.is_heap();   // heap'te mi, SSO'da mı?
```

### Yazma / Değiştirme

```cpp
knst_byte_string s;

s.append(data, size);        // ham veri ekle
s.append(other);             // başka byte_string ekle
s.push_back(0xFF);           // tek byte ekle

s.prepend(data, size);       // başa ekle
s.prepend(other);            // başka byte_string'i başa ekle

s.resize(20);                // 20 byte'a getir (0 ile doldur)
s.resize(20, 0xAA);          // 20 byte, kalanları 0xAA ile doldur
s.reserve(100);              // en az 100 byte kapasite
s.shrink_to_fit();           // kapasiteyi length+1'e indir
s.clear();                   // temizle
```

### Erişim

```cpp
knst_byte_string s = "abc";

s[0];         // 'a' (unsigned char)
s[1];         // 'b'
s[2];         // 'c'

s[0] = 'A';   // değiştir
```

---

## ⚙️ Operatörler

### Birleştirme

```cpp
knst_byte_string s = "Hello";
s += " ";
s += "World";
s += '!';
// s == "Hello World!"

knst_byte_string combined = knst_byte_string("[") + s + "]";
// combined == "[Hello World!]"
```

### Karşılaştırma

Tüm karşılaştırma operatörleri mevcut — hem `basic_byte_string` hem `char*` / `unsigned char*` ile:

```cpp
knst_byte_string a = "apple";
knst_byte_string b = "banana";

a == b;         // false
a != b;         // true
a < b;          // true (lexicographic)
a > b;          // false
a == "apple";   // true
"apple" == a;   // true (friend)
```

### Stream Operatörü

```cpp
knst_byte_string s = "merhaba";

std::cout << s;   // tam olarak length() byte yazar — binary-safe
```

**Not:** `operator<<` null terminator aramaz. `length()` kadar byte yazar. Yani gömülü null içeren veriler de **bozulmadan** çıkar.

---

## 🔁 Iterator'ler

Standart STL uyumlu iterator'ler:

```cpp
knst_byte_string s = "abc";

for (unsigned char c : s) {
    std::cout << static_cast<char>(c) << ' ';
}

// veya
for (auto it = s.begin(); it != s.end(); ++it) {
    unsigned char c = *it;
}

// const versiyonlar
for (auto it = s.cbegin(); it != s.cend(); ++it) {
    // okuma
}
```

`knst_iterator<unsigned char>` ve `knst_const_iterator<unsigned char>` kullanılır — `knst_c16string` ile aynı iterator altyapısı.

---

## 🔐 Binary-Safe Kullanım

`knst_byte_string`'in en güçlü yanı **binary-safe** olması. Gömülü `\0` byte'ları **korunur**.

```cpp
const unsigned char blob[] = {0x00, 0x11, 0x00, 0x22, 0x00, 0x33};
knst_byte_string b(blob, sizeof(blob));

std::cout << "length = " << b.length() << "\n";   // 6 (0x00'da durmaz!)
```

Ağ paketleri, şifreleme çıktıları, resim verileri için ideal.

### Hex Dump Örneği

```cpp
void print_hex(const unsigned char* p, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        std::printf("%02X ", static_cast<unsigned>(p[i]));
    }
    std::cout << "\n";
}

knst_byte_string packet = {0xDE, 0xAD, 0xBE, 0xEF};
print_hex(packet.data(), packet.length());
// Çıktı: DE AD BE EF
```

---

## 🌍 UTF-16 → UTF-8 Dönüşümü

Windows API'leri UTF-16 kullanır ama ağ protokolleri, dosya formatları (JSON, UTF-8 text) UTF-8 bekler. `knst_byte_string` bu dönüşümü otomatik yapar:

```cpp
// UTF-16 literal'den
const char16_t u16[] = u"Merhaba dünya";
knst_byte_string b1(u16, 13);                // UTF-8 byte'ları
// b1.length() = 13 değil, UTF-8 byte sayısı!

// knst_c16string'den
knst_c16string c16 = u"ünïcödé";
knst_byte_string b2(c16);

std::cout << b1 << "\n";   // "Merhaba dünya"
std::cout << b2 << "\n";   // "ünïcödé"
```

**Önemli:** UTF-8'de non-ASCII karakterler 2+ byte tutar. Yani `b2.length()` (byte cinsinden) `c16.length()` (UTF-16 code unit) değerinden **büyük olabilir**.

---

## 🎁 take_ownership — `new[]` Buffer Sahiplenme

`new[]` ile ayrılmış bir buffer'ı byte string'e **sahiplendirir** ve uygun şekilde siler:

```cpp
unsigned char* raw = new unsigned char[8];
std::memcpy(raw, "network", 7);
raw[7] = '\0';

knst_byte_string owned = knst_byte_string::take_ownership(raw, 7);
// raw artık geçersiz! Byte string veriyi kopyaladı ve orijinali delete[] etti.

std::cout << owned << "\n";   // "network"
```

**Ne yapar?**
- Eğer boyut SSO'ya sığıyorsa → stack'e kopyalar, `delete[]` yapar
- Aksi halde → allocator'dan yeni heap ayırır, kopyalar, `delete[]` yapar
- Yani "sahiplenme" aslında **kopyalama + silme** olarak gerçekleşir

**Neden?** `new[]` ile ayrılmış bir buffer'ı `free` ile serbest bırakmak **tehlikelidir**. Bu metot `delete[]` garantisi verir.

---

## 🧮 SSO (Small String Optimization)

Küçük byte string'ler heap'e gitmez — sınıfın içindeki **union** içinde saklanır.

| Ayar | SSO Kapasitesi |
|---|---|
| Varsayılan | **10 byte** (KNST_SSO_BUFFER_LENGTH) |

**Örnek:** `knst_byte_string("short")` (5 byte) heap'e hiç gitmez.

**Not:** `alignas` ayarları `knst_c16string` ile aynı şekilde çalışır (`KNST_CLASS_ALIGNMENT`).

---

## 🧵 Thread Safety — COW Yok

`knst_byte_string` **COW (Copy-on-Write) kullanmaz.** Bu kasıtlıdır:

- Her kopya **kendi buffer'ına** sahiptir
- Thread'ler arasında paylaşım güvenli (her thread kendi kopyasını alır)
- Davranış öngörülebilir (yazma sırasında detach yok)

**Karşılaştırma:**

| | `knst_c16string` | `knst_byte_string` |
|---|---|---|
| COW | ✅ Var (opsiyonel) | ❌ Yok |
| Kopyalama | O(1) (COW açıkken) | O(n) (her zaman) |
| Thread safety | COW kapatılırsa güvenli | Her zaman güvenli |

Ağ protokolleri ve binary veri için **öngörülebilirlik** önemlidir — bu yüzden COW tercih edilmemiştir.

---

## 📊 Performans Notları

| İşlem | Karmaşıklık | Not |
|---|---|---|
| Kısa string (< SSO) oluşturma | **O(1)** | Heap yok |
| Kopyalama | **O(n)** | Her zaman derin kopya |
| `append` | **Amortized O(1)** | Kapasite 2x büyür |
| `push_back` | **Amortized O(1)** | Kapasite 2x büyür |
| `prepend` | **O(n)** | Veri kaydırma gerekir |
| `resize` | **O(n)** | Yeni allocation olabilir |
| `take_ownership` | **O(n)** | Kopyalama zorunlu |

### Ne Zaman Yavaş?

- **Çok sık `prepend`** — her seferinde `memmove` gerekir
- **Çok uzun byte string'ler** — heap allocation maliyetli
- **UTF-16'dan dönüşüm** — encoding hesaplaması gerektirir

---

## 🧠 Pool Allocator ile Kullanım (`knst_byte_string_sm`)

`knst_pool_allocator`, küçük allocation'ları **önceden ayrılmış bloklardan** verir. Çok sayıda ağ paketi, kısa metin oluşturup yok ediyorsan **ciddi hız kazancı** sağlar.

### Varsayılan Pool ile

```cpp
knst_byte_string_sm<> s = "kısa metin";

std::cout << "pool_count     : " << s.pool_count() << "\n";       // 4
std::cout << "max_block_size : " << s.max_block_size() << "\n";   // 2048
```

Varsayılan olarak 4 boyut vardır: **64, 256, 1024, 2048 byte**.

### Kendi Pool Boyutlarınla

```cpp
knst_pool_allocator pool(128, 512, 4096);
knst_byte_string_sm<> s(pool);
s.append("merhaba");
```

### Ne Zaman Pool Kullanmalı?

| Durum | Pool kullan? |
|---|---|
| Çok sayıda kısa paket (network, parser) | ✅ Evet, ciddi kazanç |
| Tek tük byte string | ❌ Gereksiz |
| Thread-safe gerekiyorsa | ✅ Ama `KNST_MEMORY_POOL_USE_MUTEX` tanımla |
| Uzun veriler (2048+ byte) | ⚠️ Pool devreye girmez, `malloc`'a düşer |

---

## 📚 Tam Örnek

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // 1. Farklı kaynaklardan oluşturma
    knst_byte_string a = "Merhaba";
    knst_byte_string b(data, size);              // ham veri
    knst_byte_string c = {0x01, 0x02, 0x03};    // binary
    knst_byte_string d(u"UTF-16 metin", 12);    // UTF-8'e çevrilir

    // 2. Birleştirme
    knst_byte_string greeting = a + " Dünya!";
    std::cout << greeting << "\n";

    // 3. Binary-safe veri
    const unsigned char packet[] = {0xDE, 0xAD, 0x00, 0xBE, 0xEF};
    knst_byte_string p(packet, sizeof(packet));
    std::cout << "Uzunluk: " << p.length() << " (gömülü 0x00 korundu)\n";

    // 4. Değiştirme
    p.push_back(0xFF);
    p.prepend(reinterpret_cast<const unsigned char*>("HDR"), 3);

    // 5. take_ownership
    unsigned char* buf = new unsigned char[4];
    std::memcpy(buf, "data", 4);
    knst_byte_string owned = knst_byte_string::take_ownership(buf, 4);

    // 6. Pool allocator
    knst_byte_string_sm<> fast = "pool'dan string";
    std::cout << "pool count: " << fast.pool_count() << "\n";

    return 0;
}
```

---

## 💡 Kullanım İpuçları

### ✅ Yapılması Gerekenler

```cpp
// 1. Ham veri için boyut ver
knst_byte_string p(packet_data, packet_size);   // ✅ binary-safe

// 2. UTF-16 dönüşümünü sınıfa bırak
knst_byte_string b(u"UTF-16 metin", length);    // ✅ otomatik UTF-8

// 3. take_ownership ile new[] buffer'ları sahiplen
auto s = knst_byte_string::take_ownership(ptr, size);  // ✅ delete[] garantili

// 4. Pool allocator'ı çok kısa byte string'lerde kullan
knst_byte_string_sm<> packet = read_packet();
```

### ❌ Kaçınılması Gerekenler

```cpp
// 1. strlen ile binary veri ölçme
knst_byte_string s(some_binary_data);   // ❌ gömülü 0x00'da durur!
// ✅ Çözüm: Boyutu her zaman elle ver

// 2. Çok sık prepend yapmak
for (...) {
    s.prepend(byte, 1);   // ❌ O(n) — her seferinde kaydırma
}
// ✅ Çözüm: append ile topla, sonra bir kez ters çevir

// 3. UTF-16'dan dönüşümde length karıştırmak
knst_byte_string b(u16_str, 13);       // ❌ 13 UTF-16 code unit, byte değil!
// ✅ Çözüm: Kaynak code unit sayısını ver, byte otomatik hesaplanır

// 4. operator<< ile binary veri yazdırmak
std::cout << binary_packet;   // ❌ anlamsız karakterler
// ✅ Çözüm: hex dump kullan
```

---

## 🎯 Size Getireceği Kazanç

`knst_byte_string` şu durumlarda ciddi avantaj sağlar:

- ✅ **Ağ protokolleri yazıyorsan** — paketler, header'lar, checksum verileri
- ✅ **Binary veri işliyorsan** — kriptografi, resim/ses codec'leri, dosya formatları
- ✅ **UTF-8 metin işliyorsan** — JSON, XML, REST API yanıtları
- ✅ **Windows API'lerinden gelen UTF-16 veriyi dönüştürüyorsan** — `knst_c16string` ile birlikte
- ✅ **`new[]` buffer'ları yönetiyorsan** — `take_ownership` hayat kurtarır
- ✅ **KernelNucleusT'nin diğer modüllerini kullanıyorsan** — `knst_file`, `knst_window` gibi sınıflar `knst_byte_string` bekliyor
