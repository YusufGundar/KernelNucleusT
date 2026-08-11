# KernelNucleusT

Modern C++17 kütüphane paketi. Yüksek performanslı, özelleştirilebilir , çapraz platform destekli

## Felsefe

KernelNucleusT, **performans ve güvenlik arasında optimum denge** üzerine kurulmuştur. Temel prensipler:

- **noexcept + bool dönüş** — Çoğu fonksiyon exception fırlatmak yerine `bool` döndürür. Bu sayede hem performans artar hem de hata kontrolü tamamen geliştiricinin elinde olur.

- **Force Inline** — Varsayılan olarak fonksiyonlar `force_inline` ile derlenir. Call overhead'i olmaz, kod direkt çağrıldığı yere kopyalanır. Sonuç: daha hızlı çalışma, fakat daha büyük binary.

- **Ayarlanabilir Binary Boyutu** — `KNST_SMALL_SIZE_CLASS` tanımlanırsa, `force_inline` yerine standart `inline` kullanılır. Derleyici kendi karar verir, çoğu durumda `call` ile fonksiyona gidilir. Binary boyutu küçülür, hız azalabilir. Tercih sizin.

- **Duruma Göre Esneklik** — Sık kullanılan kritik fonksiyonlarda binary boyut pahasına ek optimizasyonlar yapılabilir. Bu bir hata değil, bilinçli bir tercihtir.

Bu felsefe, paketteki tüm mevcut ve gelecek kütüphaneler için geçerlidir.

## 🚀 Kütüphaneler

### knst_window  ---BETA---


  * basit bi örnek


    ```cpp
        #include <iostream>
        #include "../include/KernelNucleusT.hpp"

        int main() {
            
            KnstWindowSources::Init();

            knst_window window(300, 300, u"Test");
            
            window.creation_and_show();

            while (!window.is_should_close()) {

                knst_window_event_system::block_pool_event(window);

                if (window.get_window_event_handle().type == KNST_CLOSE_WINDOW || 
                    window.get_window_event_handle().type == KNST_DISCONNECT) {
                    std::cout << "cleaning window..." << std::endl;
                    window.destroy();
                    window.should_close();
                }
                window.clear_temporary_events(); // eventi sıfırlar
            }

            std::cout << "cleaning sources..." << std::endl;
            KnstWindowSources::CleanUp();

            return 0;
        }
        ```

  *


**Çok kapsamlı windows ve linux için tam destekli bir pencere yönetim kütüphanesi olmaya aday bir kütüphanedir**

**Özellikler:**
- **Performans ve Boyut arasındaki en iyi dengeyi sunmayı hedefler**
- **Modern C++ özellikleri ile kullanıcıya temiz kod yazma imkanı sunar**
- **işletim sistemi event mantığına benzer bir şekilde eventleri yakalar ve işlersiniz**
- **Güvenlik açısından daha beta durumundadır**


**Kullanım ve Bilmeniz gerekenler**
  — Öncelikle kütüphaneyi başlatmak adına programın başında 'KnstWindowSources::Init()' çağrısının yapılması gereklidir bu kütüphane için tanımlanan static kaynakları hazırlar ve başlatır
  ayrıca bu çağrı kendi içinde işletim sistemine bağlı olan monitörlerin listelerinide çıkartır yani içinde 'knst_display::refresh_monitors()' da otomatik yapılır ve knst_display:: içinden gerekli bilgileri okumak için tekrar ::refresh_monitors() yapmanı gerek yoktur ancak yenileme işlemleri için yeniden kontrol etmek için yapaiblirsiniz.

  — Bazı olaylar waylandda kısıtlıdır , wayland compositor gereği güvenlik amacıyla bazı olayları engellenmiştir

  — Wayland Kısıtlamaları: Wayland compositor güvenlik modeli nedeniyle move(), set_cursor_pos_*() gibi bazı fonksiyonlar çalışmaz.

**Event İşleme Hakkında**
  — Pencerenize gelen eventler , pencere nesnesinin içinde event yapısına dolar , bunu 'get_window_event_handle()' fonksiyonu ile const olarak alıp içindeki yapıları switch case ve if else yapıları ile kontrol edebilirsiniz

**Döngüler**
  — 3 ana döngü çeşidi knst_window_event_system içerisinde mevcuttur
    — block_pool_event == sadece event geldiğinde tetiklenir
    — non_block_pool_event == event varsa alır yoksa hemen döner
    — timeout_pool_event == verdiğiniz süre kadar bekler event varsa alır yoksa hemen döner

**Opengl ve Vulkan**
  — knst_window_opengl_content ve knst_window_vulkan_content sayesinde pencere ile çizim apilerini kullanabilirsiniz

  isterseniz #define KNST_DISABLE_TITLE_BAR ile title barı kapatıp , şimdilik opengl için özel title barları aktif edebilirsiniz  , yakında vulkanada gelicektir...
  ayrıca opengl için koda BeginFrame() ile başlamanızı öneriyorum

    #define KNST_WINDOW_USING_KNST_TITLE_BAR_WHITE_MODERN
    #define KNST_WINDOW_USING_KNST_TITLE_BAR_BLUE_MODERN
    #define KNST_WINDOW_USING_KNST_TITLE_BAR_FUTURISTIC
    #define KNST_WINDOW_USING_KNST_TITLE_BAR_SUNSET_GLOW


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


### knst_byte_string

'unsigned char` tabanlı, binary data ve utf8 değerlerini tutmak için knst_c16string alternetifidir
 -  **UTF-8 Desteği:** Ham byte olarak saklar
 -  **Güvenlik** — testlerden başarıyla geçmiştir.
 -  **Binary Güvenli:** \0 (null) byte'ları içerebilir


### knst_vector
 kütüphanenin ihtiyaçlarına göre metotları mevcuttur , kullanıcı kullanımı için yeni metotlar eklenecektir

 - **Güvenlik** — testlerden başarıyla geçmiştir.


### knst_memory

Instance tabanlı, memory pool allocator. Thread-safe opsiyonu mevcuttur ayrıca eklenecek yeni kütüphanelerin hepsinde knst_memory desteği mevcut olucaktır, tıpkı knst_c16string deki gibi.

### Hedefler

İleride knst_vector , knst_functional , knst_regex , gui framework gibi yapılar çapraz platform destekli kapsamlı bir şekilde getirmeyi planlıyorum , her kullanıma uygun çok özelleştirilebilir olmasına özen gösteririm




## 📦 Kurulum

'cmake_compile_helper.txt' dosyası içinde detaylı cmake bilgileri verilmiştir 

```cpp
#include "../include/KernelNucleusT.hpp"
```

🤝 Katkıda Bulunma

  # Bug raporları ve özellik istekleri için Issues sayfasını kullanın
  # Herangi bir özel istek veya beni bilgilendirmek isterseniz benimle iletişime geçebilirsiniz



📄 Lisans

  MIT License — Özgürce kullanın, geliştirin, paylaşın.
