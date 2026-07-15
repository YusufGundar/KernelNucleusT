# knst_c16string — Kullanım Kılavuzu (Türkçe)

`char16_t` tabanlı, yüksek performanslı string sınıfı.
Varsayılan olarak 10 karaktere kadar veriyi **stack**'te tutar (SSO), fazlasında **heap**'e geçer.
**COW (Copy-On-Write)** özelliği varsayılan olarak aktiftir, makro ile kapatılabilir.

> Gereksinim: **C++17** veya üzeri.


---

## Kurucular (Constructors)

```cpp
knst_c16string s1;                          // Boş string oluşturur
knst_c16string s2 = u"Hello World";         // char16_t (Unicode) string'den oluşturur
knst_c16string s3 = "Hello World";          // char (UTF-8) string'den oluşturur
knst_c16string s4 = L"Hello World";         // wchar_t string'den oluşturur
knst_c16string s5 = U"Hello World";         // char32_t (UTF-32) string'den oluşturur
knst_c16string s6(u"Hello", 3);             // Belirtilen uzunlukta karakter alır -> "Hel"
knst_c16string s7(10, u'A');                // Tekrar eden karakter oluşturur -> "AAAAAAAAAA"
knst_c16string s8(3, u"Test");              // Tekrar eden string oluşturur -> "TestTestTest"
knst_c16string s9(12345);                   // Sayısal değerden string oluşturur -> "12345"
knst_c16string s10(-42);                    // Negatif sayısal değerden string oluşturur -> "-42"
knst_c16string s11(3.14);                   // Ondalıklı sayıdan string oluşturur -> "3.14"
knst_c16string s12(std::string("Test"));    // std::string'den dönüştürür
knst_c16string s13(std::u16string(u"X"));   // std::u16string'den dönüştürür
knst_c16string s14(std::u32string(U"X"));   // std::u32string'den dönüştürür
knst_c16string s15(std::wstring(L"X"));     // std::wstring'den dönüştürür
knst_c16string s16(std::string_view("X"));  // string_view'den dönüştürür
knst_c16string s17{u'A', u'B', u'C'};       // initializer_list'ten oluşturur -> "ABC"
```
*Not:* `count + str` kurucusunda (`s8` gibi) tekrarlama, iç mekanizmada "üstel kopyalama" ile hızlıca yapılır — `memcpy` çağrı sayısı `O(log n)`'dir.

## Kopyalama & Taşıma (Copy & Move)

```cpp
knst_c16string original = u"This is a long string";  // Orijinal string
knst_c16string copy = original;              // Kopya (COW aktifse veri paylaşılır, kopyalanmaz)
knst_c16string moved = std::move(original);  // Move ile taşır
// original artık boş bir string'e döner (stack modunda, length() == 0)
```

## Atama Operatörleri

```cpp
knst_c16string s;
s = u"char16_t string";       // char16_t literal atar
s = "char string";            // char (UTF-8) literal atar
s = L"wchar_t string";        // wchar_t literal atar
s = U"char32_t string";       // char32_t literal atar
s = another_string;           // Başka bir knst_c16string'den kopyalar
s = std::move(other);         // Move ile atama yapar
```
*Not:* Hedef zaten heap modda ve yeterli kapasiteye sahipse, yeniden ayırma (reallocation) yapılmadan doğrudan üzerine yazılır — performans optimizasyonudur.

## String Bilgileri

```cpp
knst_c16string s = u"Hello World";
s.length();          // Uzunluğu döndürür -> 11
s.capacity();        // Ayrılmış kapasiteyi döndürür (SSO ya da heap'e göre)
s.empty();           // Boş mu kontrol eder -> false
s.is_heap();         // Şu an heap modunda mı -> false (stack'te)
s.data();            // const char16_t* pointer döndürür
s.pool_count();      // Kullanılan allocator'daki havuz sayısını döndürür
s.max_block_size();  // Allocator'daki en büyük blok boyutunu döndürür
```
*Not:* `pool_count()` / `max_block_size()`, `knst_default_allocator` ile her zaman `0` döner; asıl anlamlı bilgiyi `knst_pool_allocator` kullanıldığında verir.

## Ekleme (Append / +=)

```cpp
knst_c16string s = u"Hello";
s.append(u" World");           // char16_t string ekler
s.append(" how are you");      // char (UTF-8) string ekler
s.append(L" I'm fine");        // wchar_t string ekler
s.append(U" there");           // char32_t string ekler
s.append(another_string);      // Başka bir knst_c16string ekler
s.append(123456);      // int , unsigned int, long  , long long , unsigned long , float ve double gibi türleri destekler
s += u" test";                  // += operatörüyle ekler
s += "!";                      // char literal ekler
```
*Not:* `append`, kapasite yetiyorsa veriyi **yerinde** büyütür; yetmiyorsa kapasiteyi ikiye katlar (ya da gereken minimuma göre büyütür) ve yeniden ayırma yapar.

## Arama (Find & Contains)

```cpp
knst_c16string s = u"Hello World, how are you?";
s.find(u"World");              // Alt-string arar -> true döner
s.find("Hello");               // char* (UTF-8) ile arama -> true döner
s.find(u"World", 10);          // Belirtilen offset'ten itibaren arar
s.find(u'o');                  // Tek karakter arar -> true döner
s.find(u"Mars");               // Bulunamazsa -> false döner
s.contains(u"how");            // İçeriyor mu -> true döner
s.contains(u"xyz");            // İçermiyorsa -> false döner
s.contains('e');               // Karakter içeriyor mu -> true döner
```
*Not:* Kısa desenlerde (`<= 8` karakter) basit (naive) arama, daha uzun desenlerde **Two-Way string matching algoritması** kullanılır — her ikisi de `find()` içinde otomatik seçilir.

## Başlangıç / Bitiş Kontrolü (Starts/Ends With)

```cpp
knst_c16string s = u"Hello World";
s.starts_with(u"Hel");         // Bu string ile başlıyor mu -> true
s.starts_with("Hey");          // -> false
s.starts_with(u'H');           // Bu karakterle başlıyor mu -> true
s.ends_with(u"rld");           // Bu string ile bitiyor mu -> true
s.ends_with(u'd');             // Bu karakterle bitiyor mu -> true
```

## Karakter Erişimi (At, Front, Back, `[]`)

```cpp
knst_c16string s = u"Hello";

// Sınır kontrolü ile erişim
s.at(0);          // 'H' döner
// s.at(10);      // KNST_ASSERT tetiklenir (index sınır dışında)

// Sınır kontrolü olmadan erişim
s[0];             // 'H' döner
s[0] = u'h';      // Yazma işlemi -> "hello" (COW aktifse detach() tetiklenir)
s.front();        // İlk karakter -> 'H'
s.back();         // Son karakter -> 'o'

// Değiştirme
s.front() = u'X'; // -> "Xello"
s.back() = u'Y';  // -> "XellY"

// Const erişim (sadece okuma)
const knst_c16string& cs = s;
cs[0];             // Okuma
cs.at(1);          // Okuma
```

## Alt String (Substr)

```cpp
knst_c16string s = u"Hello World";
auto part1 = s.substr(0, 5);            // 0. indeksten 5 karakter -> "Hello"
auto part2 = s.substr(6, 5);            // 6. indeksten 5 karakter -> "World"
auto part3 = s.substr(6, s.length());   // 6. indeksten sona kadar -> "World"
auto part4 = s.substr(20, 5);           // pos, length()'ten büyükse -> boş string döner
```
*Not:* `substr` her zaman **iki parametre** ister (`pos` ve `count`); tek parametreli overload yoktur. "Sona kadar al" istiyorsan `count` için `s.length()` (veya daha büyük bir değer) ver — fonksiyon içeride `std::min(count, length()-pos)` ile otomatik kırpar.

## Reserve & Resize

```cpp
knst_c16string s = u"Hello";

// Reserve - kapasiteyi önceden ayırır
s.reserve(1000);           // capacity = 1001, heap'e geçer

// Resize - doldurma karakteriyle uzatır
s.resize(20, u'X');        // "Hello" + 15 tane 'X'
s.resize(20, 'X');         // char ile aynı işlev
s.resize(20, L'X');        // wchar_t ile aynı işlev
s.resize(20, U'X');        // char32_t ile aynı işlev

// Resize - kısaltma
s.resize(5);               // "Hello" (kısaltır)
s.resize(3, u'A');         // "Hel" (kısaltmada dolgu karakteri kullanılmaz)

// Resize - varsayılan dolgu karakteri (u'\0')
s.resize(10);               // "Hello" + 5 tane u'\0'
```

## Shrink To Fit

```cpp
knst_c16string s;

s.reserve(1000);           // capacity = 1001
s = u"Hello";               // length = 5
s.shrink_to_fit();          // capacity SSO sınırına düşer (heap->stack geçişi olabilir)

s = u"This is a very long string stored on heap";
s.reserve(10000);           // capacity = 10001
s.shrink_to_fit();          // capacity = length + 1
```

## Clear

```cpp
knst_c16string s = u"Hello World";
s.clear();                 // length = 0, stack moduna döner
s.empty();                 // true döner
```

## Birleştirme (`+` Operatörü)

```cpp
knst_c16string s1 = u"Hello";
knst_c16string s2 = u" World";

// String + String
auto s3 = s1 + s2;              // "Hello World"

// String + Literal
auto s4 = s1 + u" Mars";        // "Hello Mars"
auto s5 = "Venus" + s1;         // "VenusHello"
auto s6 = s1 + L" Jupiter";     // "Hello Jupiter"
auto s7 = U"Saturn" + s1;       // "SaturnHello"

// String + Karakter
auto s8 = s1 + u'!';            // "Hello!"
auto s9 = u'!' + s1;            // "!Hello"

// Zincirleme ekleme
s1 += u" Planet";               // "Hello Planet"
s1 += " Earth";                 // "Hello Planet Earth"
```

## Karşılaştırma Operatörleri

```cpp
knst_c16string s1 = u"abc";
knst_c16string s2 = u"def";

s1 == s2;          // false
s1 != s2;          // true
s1 < s2;           // true  ("abc" < "def")
s1 <= s2;          // true
s1 > s2;           // false
s1 >= s2;          // false

// Literal ile karşılaştırma (her yönde çalışır)
s1 == u"abc";      // true
s1 == "abc";       // true
"abc" == s1;       // true
s1 < "def";        // true
"abc" < s2;        // true
```
*Not:* Tüm karşılaştırmalar önce uzunlukları, sonra byte içeriğini (`memcmp`) kıyaslar; `char*`/`wchar_t*`/`char32_t*` ile karşılaştırmada karşı taraf önce UTF-16'ya çevrilir.

## Iterator

```cpp
knst_c16string s = u"Hello";

// Mutable iterator
for (auto it = s.begin(); it != s.end(); ++it) {
    char16_t c = *it;
    *it = u'a';               // Karakteri değiştirir (COW aktifse detach() tetiklenir)
}

// Const iterator (sadece okuma)
for (auto it = s.cbegin(); it != s.cend(); ++it) {
    char16_t c = *it;
}

// Range-based for
for (char16_t c : s) { /* ... */ }
for (char16_t& c : s) { c = u'X'; }   // Değiştirir, detach() tetikler

// STL algoritmaları
knst_c16string s2 = u"cba";
std::sort(s2.begin(), s2.end());       // "abc"
std::reverse(s2.begin(), s2.end());    // "cba"
auto it = std::find(s.begin(), s.end(), u'e');
```
*Not:* `iterator`/`const_iterator` tipleri (`knst_iterator<char16_t>`, `knst_const_iterator<char16_t>`) bu header'da **tanımlı değil** — projenin başka bir header'ında tanımlı olmaları gerekir. `std::sort` gibi algoritmalar için random-access iterator gereksinimlerini karşıladıkları varsayılır; kesinleştirmek için o header'a bakman gerekir.

## Konsol Çıktısı

```cpp
knst_c16string s = u"Hello World";

std::cout << s << std::endl;    // UTF-16 -> UTF-8'e çevirip yazar
std::wcout << s << std::endl;   // Windows'ta doğrudan, Linux'ta dönüştürerek yazar

// Manuel UTF-8 dönüşümü
size_t utf8_size = knst_get_utf16_to_utf8_exact_byte_size(s.data(), s.length());
char* utf8_buffer = (char*)malloc(utf8_size + 1);
knst_convert_utf16_to_utf8(s.data(), s.length(), utf8_buffer);
utf8_buffer[utf8_size] = '\0';
printf("%s", utf8_buffer);
free(utf8_buffer);
```

## COW (Copy-On-Write)

```cpp
// COW varsayılan olarak aktiftir
knst_c16string a = u"This is a long string stored on heap";
knst_c16string b = a;           // Veri kopyalanmaz, sadece paylaşılır (ref_count++)

a.data() == b.data();          // true (aynı pointer)

b.append(u" modified");        // Yazma -> detach() otomatik tetiklenir
a.data() != b.data();          // true (artık ayrı)

// Explicit (manuel) detach
knst_c16string c = a;
c.detach();                    // true döner (paylaşımlıydı, artık ayrıldı)
c.detach();                    // false döner (zaten ayrık, yapılacak iş yok)
```

```cpp
// COW'u derleme zamanında kapatmak için:
// #define KNST_C16STRING_DEACTIVE_COW
// Bu tanımlıyken her kopya derin kopya (deep copy) olur, paylaşım olmaz.
```

## Pool Allocator ile Kullanım

```cpp
// Özel blok boyutlarıyla pool allocator oluşturur
knst_pool_allocator pool(64, 256, 1024, 2048);

// Pool allocator kullanan string oluşturur
knst_c16string_sm<knst_pool_allocator> s(pool);
s = u"String using pool memory";

s.pool_count();                // 4 döner (4 farklı havuz)
s.max_block_size();            // 2048 döner

// Allocator değiştirme (aynı Allocator tipiyle)
knst_pool_allocator pool2(128, 512, 4096);
s.bridge_memory(pool2);        // Veriyi yeni havuza taşır

// Varsayılan pool allocator (varsayılan boyutlar: 64, 256, 1024, 2048)
knst_c16string_sm<> default_pool;

// Constructor ile özel allocator
knst_c16string_sm<knst_pool_allocator> s2(knst_pool_allocator(32, 128, 512));
s2 = u"Custom pool string";

// Havuzu yeni boyutlarla sıfırlar
pool.reset(16, 64, 256, 1024);
```

## Makrolar (Derleme Zamanı Seçenekleri)

| Makro | Etkisi |
|---|---|
| `KNST_C16STRING_DEACTIVE_COW` | COW'u kapatır, her kopya derin kopya (deep copy) olur. |
| `KNST_C16_STRING_USING_ATOMIC_COW` | COW referans sayacını `std::atomic<int>` yapar (thread-safe). |
| `KNST_C16STRING_ALIGN_64` | 64 byte hizalama, SSO kapasitesi 30 karaktere çıkar. |
| `KNST_C16STRING_ALIGN_32` | 32 byte hizalama, SSO kapasitesi 14 karaktere çıkar. |
| (hiçbiri) | Varsayılan: 8 byte hizalama, SSO kapasitesi 10 karakter. |
| `KNST_MEMORY_POOL_USE_MUTEX` | `knst_pool_allocator`'ı mutex ile korur, thread-safe yapar. |
