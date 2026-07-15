# KernelNucleusT

Modern C++17 kütüphane paketi. Yüksek performanslı, özelleştirilebilir,full header-only

## Felsefe

KernelNucleusT, **performans ve güvenlik arasında optimum denge** üzerine kurulmuştur. Temel prensipler:

- **noexcept + bool dönüş** — Çoğu fonksiyon exception fırlatmak yerine `bool` döndürür. Bu sayede hem performans artar hem de hata kontrolü tamamen geliştiricinin elinde olur.

- **Force Inline** — Varsayılan olarak fonksiyonlar `force_inline` ile derlenir. Call overhead'i olmaz, kod direkt çağrıldığı yere kopyalanır. Sonuç: daha hızlı çalışma, fakat daha büyük binary.

- **Ayarlanabilir Binary Boyutu** — `KNST_SMALL_SIZE_CLASS` tanımlanırsa, `force_inline` yerine standart `inline` kullanılır. Derleyici kendi karar verir, çoğu durumda `call` ile fonksiyona gidilir. Binary boyutu küçülür, hız azalabilir. Tercih sizin.

- **Duruma Göre Esneklik** — Sık kullanılan kritik fonksiyonlarda binary boyut pahasına ek optimizasyonlar yapılabilir. Bu bir hata değil, bilinçli bir tercihtir.

Bu felsefe, paketteki tüm mevcut ve gelecek kütüphaneler için geçerlidir.

## 🚀 Kütüphaneler

### knst_c16string

`char16_t` tabanlı, yüksek performanslı string sınıfı.
**Default olarak 22 byte'a kadar yani 10 karaktere kadar stack'te tutar, fazlasında heap'e geçer.**

**Özellikler:**
- **Çoklu Karakter Desteği** — Kurucu ve fonksiyonlarda `char16_t`, `char`, `wchar_t`, `char32_t` türlerini doğrudan kabul eder.
- **STL Uyumlu** — `std::string`, `std::wstring`, `std::u16string`, `std::u32string` ve view'leri gibi yapılar ile sorunsuz çalışır.
- **Makro ile Özelleştirme:**
  - `KNST_C16STRING_DEACTIVE_COW` — Copy-On-Write'ı kapatır
  - `KNST_C16_STRING_USING_ATOMIC_COW` — COW sayacını thread-safe yapar
  - `KNST_C16STRING_ALIGN_64` / `KNST_C16STRING_ALIGN_32` — Sınıf hizalamasını ve SSO kapasitesini değiştirir
- **Güvenlik** — Kapsamlı test paketinden başarıyla geçmiştir.


### knst_memory

Instance tabanlı, memory pool allocator. Thread-safe opsiyonu mevcuttur ayrıca eklenecek yeni kütüphanelerin hepsinde knst_memory desteği mevcut olucaktır, tıpkı knst_c16string deki gibi.

### Hedefler

İleride knst_vector , knst_functional , knst_regex , knst_window gibi yapılar çapraz platform destekli kapsamlı bir şekilde getirmeyi planlıyorum , her kullanıma uygun çok özelleştirilebilir olmasına özen gösteririm


## 📦 Kurulum

## Header-only'dir herhangi ek bi derleme ayarı gerektirmez

```cpp
#include "../include/KernelNucleusT.hpp"  // Hepsi bukadar