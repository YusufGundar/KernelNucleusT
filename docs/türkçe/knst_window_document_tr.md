# knst_window — Kullanım Kılavuzu (Türkçe)

**Çok kapsamlı windows , linux ve android için tam destekli bir pencere yönetim kütüphanesi olmayı ve sizlere olabildiğince çok özellik sunmayı hedefler**

**Özellikler:**
- **Çoğu kritik ve uygulama çalışırken sürekli tekrarlancak yerlerde `force inline` kullanılmıştır**
- **Modern C++ özellikleri ile kullanıcıya temiz kod yazma imkanı sunar**
- **işletim sisteminin event mantığına benzer bir şekilde eventleri yakalar ve işleme imkanı sunar**
- **Güvenlik** — Sınırlı sayıdaki , testlerden başarıyla geçmiştir

---

**Kütüphane Mimarisi**

Kütüphaneyi kullanmak için ilk önce `KnstWindowSources::Init()` çağrısını yapmanız gerejmektedir. `KnstWindowSources` kütüphane boyunca global olarak tutulması gereken yapıları içerisinde barındırır `Init()` fonksiyonu ile başlatırsınız ayrıca içerisinde `knst_display::refresh_screens()` çağrısını yaparak mevcut ekranları ve bilgilerini almaktadır , program içerisinde ek olarak tekrardan gerekmedikçe çağırmanın bir anlamı yoktur , çağrı zaten yapılmış `knst_monitor` yapısı zaten monitör/ekran özelliklerini barındırmaktadır

Kütüphaneyi başlattıktan sonra artık pencere oluşturmaya ve event işlemeye hazır hale gelmiş olursunuz.

#### pencere oluşturma: ####
```cpp
knst_window window('genişlik', 'yükseklik', 'başlık', 'ana ekrandaki x konumu', 'ana ekrandaki y konumu', 'monitör');
```


| Parametre | Tip | Açıklama | Örnek |
|-----------|-----|----------|-------|
| genişlik | int | Pencere genişliği (piksel) | 800 |
| yükseklik | int | Pencere yüksekliği (piksel) | 600 |
| başlık | std::u16string | Pencere başlığı (UTF-16) | u"Triangle Test" |
| ana ekrandaki x konumu | int | x konumu (pixel) | u"200" |
| ana ekrandaki y konumu | int | y konumu (pixel) | u"200" |
| monitör | knst_monitor | knst_monitor nesnesi | knst_display::get_primary_monitor() |
---

⚠️: Linux (Wayland)'da pencerenin nerde oluşacağını biz değil compositorü karar verir yani ana ekrandaki konumunu versekte 'dikkate alınmayacaktır'

#### Sıradaki adım ise  'Oluşturma ve Göstermedir' : 

- creation()
- show()
- creation_and_show()

Kurucu sadece bilgileri  alıp saklamaktadır, asıl pencere oluşturma işlemi `creation()` fonksiyonu içerisinde gerçekleşir bu aşamada işletim sistemine pencere tanıtılır ve kaynaklar başlatılır bu işlevi `creation()` fonksiyonu yapar , sonrasında show() ile gösterebilirsiniz , `creation_and_show()` fonksiyonu ise içerisinde önce `creation()` u sonrasında ise `show()` fonksiyonları çağırır isterseniz önce `creation()` sonra `show()` yaparsınız isterseniz tek satırda `creation_and_show()` ile hem oluşturup hem gösterirsiniz . Flicker sorunları olmasın diye `creation()` fonksiyonun içine showu koymadım yapıcağınız ayarlamalar vs varsa `creation()` dan sonra yaparsınız sonrada `show()` yaparsınız ve pencereniz o ayarlara göre açılır

- Eğer pencerenin ana ekrandaki konumu sizin için önemli değilse `KNST_DEFAULT` makrosu ile işletim sisteminin karar vermesini sağlayabilirsiniz bu sadece creation için geçerli değildir konum bilgileri içinde değişmesini istemediğiniz değerler için `KNST_DEFAULT` makrosunu verebilirsiniz

#### Sonraki adım 'Event işleme ve Event Döngüsü' : 

**Event İşleme:**
#### knst_windowda 3 temel event işleme türü vardır ###

- block_pool_event ---> event geldiğinde, döner
- non_block_pool_event ---> event varsa alır ve döner, yoksa direkt döner
- timeout_pool_event ---> verdiğiniz süre kadar bekler, sonrasında event var alır, yoksa direkt döner

📌 **NOT:** Bu yapılar `knst_window_event_system` içerisinde public, static şekilde bulunur, sadece çağırmanız yeterlidir çoklu pencere uygulamalarında uygun olan pencereye, doğru event iletilicektir

- Eğer event nesnesinin `type`'ı KNST_UNKNOWN ise döngüde türü tespit edilememiştir

- Event nesnesi bu 3 döngüden sonra `knst_window` sınıfından oluşturduğunuz nesnenin içindeki `m_knst_event (private)` nesnesinin içini doldurur sizde `window.get_window_event_handle()` ile bu m_knst_event nesnesine const olarak erişip içerisindeki değeleri kontrol edebilirsiniz , bu yapının gerçek bi örneği bu şekildedir:

```cpp
    #if defined(KNST_USING_PLATFORM_ANDROID)

        int pointer_count = 0; // ekrana aynı anda basan parmak sayısı (çoklu dokunmatik)
        float pointer_x[10] = {0}; // her bir parmağın X koordinatı (piksel cinsinden)
        float pointer_y[10] = {0}; // her bir parmağın Y koordinatı (piksel cinsinden)
        int pointer_id[10] = {0}; // her bir parmağın benzersiz ID'si (parmak takibi için)
        int touch_action = 0; // dokunmatik olay tipi bastı , çekti , haraket etti , iptal gibi
        int content_left = 0; // pencere içeriğinin sol kenarı
        int content_top = 0; // pencere içeriğinin üst kenarı
        int content_right = 0; // pencere içeriğinin sağ kenarı
        int content_bottom = 0; // pencere içeriğinin alt kenarı
        int orientation = 0; // ekran yönü dikey = 0 , yatay = 1 gibi
        char language[4] = {0}; // sistem dili
        char country[4] = {0}; // sistem ülke kodu
        bool is_night_mode = false; // gece modu durumu
        bool is_low_memory = false; // cihazın bellek durumu
        float density = 1.0f; // dpi oranı
        int screen_width_dp = 0; // ekran genişliği (dp cinsinden)
        int screen_height_dp = 0; // ekran yüksekliği (dp cinsinden)
        void* saved_state = nullptr; // uygulama kapanırken kaydedilen veri bloğu
        size_t saved_state_size = 0; // kaydedilen veri bloğunun boyutu (byte)

    #endif

        int type = 0; // gelen eventin türü
        int key_code = 0; // hangi tuş olduğu       
        int scancode = 0; // donanımdaki gerçek kodu
        int key_action = 0; // tuş durumu basma , çekme , tekrarlama / press , release , repeat bilgileri
        int mods = 0; // bir tuş basıldığınla hangi tuşunda o tuşla beraber basılı olduğu bilgisi
        bool is_focused = 0; // pencerenin focus durumu
        bool mouse_on_window = 0; // mousenin pencerenin içinde olup olmadığı durumu
        int mouse_x = 0; // mousenin penceredeki x konumu
        int mouse_y = 0; // mousenin penceredeki y konumu
        int mouse_root_x = 0; // mousenin monitördeki x konumu
        int mouse_root_y = 0; // mousenin monitördeki y konumu
        int mouse_button = 0; // mouse tuş durumu sol , sağ / left , right          
        int mouse_action = 0; // mouse durumu tıklama , bırakma / press , release
        int mouse_scroll_delta = 0; // mouse tekerleği durumu 'pozitifse yukarı' 'negatifse aşağı'
        int window_width = 0; // pencerenin genişliği
        int window_height = 0; // pencerenin yüksekliği
        int window_root_x = 0; // pencerenin monitördeki x konumu
        int window_root_y = 0; // pencerenin monitördeki y konumu
        bool is_full_screen = false; // pencerenin full_screen durumu
        bool is_minimized = false; // pencerenin minimize durumu
        bool is_maximized = false; // pencerenin maximize durumu

        knst_vector<knst_c16string>drop_files; // drag drop özelliği açıksa dosyaların yolları burda saklanır
        uint32_t drop_count = 0; // drag drop ile kaç dosya bırakıldığı

        // Bunu window nesnesinden 'clear_temporary_events()' ile çağırırsınız
        KNST_FORCE_INLINE void clear()noexcept{
            key_code = 0;
            scancode = 0;
            key_action = 0;
            mouse_button = 0;
            mouse_action = 0;
            mouse_scroll_delta = 0;
            type = 0;        
        }

```

⚠️: Yine bir uyarı daha Linux(Wayland) için .Pencere içerisindeki Bvent nesnesindeki `window_root_y`, `window_root_x`, `mouse_root_x`,`mouse_root_y` gibi global değerler asla dolmayacaktır , detaylar için araştırabilirsiniz waylandın compositorü güvenlik nedeniyle çoğu şeyi engelleyebiliyor

⚠️: Android tarafındada yine mouse action maximized durumları vs defaulttur yani siz belirleyemezsiniz  ayrıca mouse eventi yerine touch eventleri gelmektedir 

#### Tavsiyem şu yöndedir: ####
- While döngüsünün koşulunu `!window.is_should_close()` ile kontrol etmenizdir bu sayede pencere içerisinde kapanma veya bağlantı sorunları yaşandığında örneğin `window.should_close()` yapıp pencerenin kapanmasını yani while döngüsünden çıkmasını sağlayabilirsiniz

- Ayrıca önemli bir bilgi daha `KNST_CLOSE_WINDOW` makrosu pencerenin çarpı tuşuna veya kapanması gerektiğinde gelirken `KNST_DISCONNECT` Linux (X11) de event nesnesi alma sırasında başarısız olursa gelmektedir ayrıca önemli bir bilgi daha:

⚠️: `KNST_DISCONNECT` eventi Linux(Wayland)'da uygulamanız açıkken bilgisiyar uyku moduna alınıp sonrasında tekrar açıldığında uygulamanıza `KNST_DISCONNECT` eventi gelmektedir, eğer bu durumda uygulamanızın kapanmamasını istiyorsanız `KNST_DISCONNECT` bu eventi ayrı olarak kontrol edip yapılarınızı tekrardan başlatmanız gerekmektedir

#### Basit bir uygulama örneği vericek olursam : ####

```cpp
#include <iostream>
#include "../../include/KernelNucleusT.hpp"

int main(){

    KnstWindowSources::Init(); // Kütüphane başlangıcı

    knst_window window(800,600,u"Triangle Test"); // kurucu çağırılır

    window.creation_and_show(); // pencere gerçekten oluşturulur

    while(!window.is_should_close()){ // pencere kapanma durumu sorgulanır

        knst_window_event_system::block_pool_event(); // sadece event geldiğinde tetiklenir

        if(window.get_window_event_handle().type == KNST_CLOSE_WINDOW || window.get_window_event_handle().type == KNST_DISCONNECT){ // kapanma veya bağlantı kesilmeyi algılanınca kendisini kapatır

            window.destroy();
            window.should_close();
              
        }        
        
        window.clear_temporary_events(); // geçici eventler temizlenir

    }

    KnstWindowSources::CleanUp(); // global kaynaklar temizlenir

    std::cout << "Cleaning all sources..." << std::endl;
    return 0;
}
```

- Basit bir örneği işte böyle gözükmektedir


### Pencere Özelleştirme: ###

- Eğer pencerenizde callbacksiz kullanmak isterseniz `KNST_DISABLE_REDRAW_ON_EVENT_MANAGER` bu makroyu #define ile tanımlamanızı öneriyorum ancak eğer bu makroyu tanımlarsanız windowsta resize sırasında beyaz ekran ve yüklenmeme sorunları çıkabilir çapraz platforma uyumlu olsun diye default olarak callback atamanız gerekir

- Benim önerim `KNST_DISABLE_REDRAW_ON_EVENT_MANAGER` bu makroyu tanımlamamanızdır ancak isterseniz tanımlyarak tamamen render durumunu elinize alabilirsiniz tabiki tercih sizin ayrıca eğer yavaşlık olucağından şüpheliyseniz bütün bu çağrı işlemler `KNST_FORCE_INLINE` ile call çağrısı olmadan direkt yapıştırılarak yapılıyordur

___

- Eğer pencerenizde işletim sisteminin vermiş olduğu default title barı kapatmak isterseniz `KNST_DISABLE_TITLE_BAR` makrosunu #define ile tanımlamanız yeterli olucaktır, bu makro knst_window'daki `set_attribute(KNST_WINDOW_ATTRIB_DECORATED , false)` ile verdiğinizden farklı olarak size resize imkanı gibi aynı pencerenin sadece title barının kalkmış halini sunmaktadır normalde `set_attribute` ile yaparsanız pencereyi büyültüp küçültemezsiniz genelde oyun yapımcıları için kullanılır ancak bu makroyu tanımlayarak sadece title barı kaldırmış olursunuz


### OpenGL ve Vulkan desteği : ###

- Pencerenizde `opengl` veya `vulkan` kullanmak için öncelikle istediğiniz grafik apisinin contentini oluşturup pencereyi contentinize tanıtmanız gerekmektedir

#### OpenGL : ####
- `knst_window_opengl_content content` ile content oluşturabilir
- content.Init(`knst_window`,`vsync durumu`) knst_window ' a pencerenizi & ile adresni vermelisiniz , vsync durumu ise default olarak false dır ancak istediğiniz değeri verebilirsiniz

⚠️: Title barı kendiniz çizmeyi düşünüyorsanız vsync durumunu false olarak vermenizi öneriyorum özellikle wayland için

- OpenGl için özel yapmış olduğum title bar temaları mevcuttur bunları tam olarak pencerenizde kullanmak için öncelikle `KNST_DISABLE_TITLE_BAR` makrosunu #define ile tanımlayın sonrasında ise istediğiniz temanın makrosunu #define ile tanımlayabilirsiniz

```cpp
#define KNST_WINDOW_USING_KNST_TITLE_BAR_WHITE_MODERN
#define KNST_WINDOW_USING_KNST_TITLE_BAR_BLUE_MODERN
#define KNST_WINDOW_USING_KNST_TITLE_BAR_FUTURISTIC
#define KNST_WINDOW_USING_KNST_TITLE_BAR_SUNSET_GLOW
```
- Bu makrolardan istediğinizi tanımlayabilirsiniz hatta eğer tema öneriniz varsa bana ulaşabilirsiniz bu makroların arasına ekleyebilirim


- Bu title bar çizme olayı openGl contentinin bir fonksiyonu olan `SwapBuffers` içerisinde gerçekleşmektedir 'Android için geçerli değildir' isterseniz tema makroları tanımlayıp normal bir şekilde swap buffers ile kullanabilirsiniz isterseniz de sadece title barı makroyla kapatıp 
runtimede istediğiniz gibi title barı değiştirebilirsiniz

```cpp
DrawKnstTitleBarBlueModern()
DrawKnstTitleBarWhiteModern()
DrawKnstTitleBarFuturistic()
DrawKnstTitleBarSunsetGlow()
```
- gibi fonksiyonları kullanabilirsiniz


⚠️: Linux (Wayland)'da çoğu linux dağıtımı özel bir title bar vermez oyüzden kesin olarak bizim çizmemiz gerekir elbette;  SSD (Server-Side Decoration) yani işletim sisteminin özel verdiği title bar desteği olan dağıtımlar var KDE gibi ancak ben kütüphaneyi tasarlarken her dağıtımın waylandına uyumlu olması için kendim default olarak `DrawKnstTitleBarWhiteModern()` title barını çiziyorum tabiki makroyla değiştirebilirsiniz bunuda ayrıca contentin içindeki BeginFrame fonksiyonu ile başlarsanız bu title bar ve içerik alanı doğru şekilde ayarlanacaktır

___

- OpenGl contentini kullanımını size şu şekilde tavsiye ediyorum eğer callback ile kullanıcaksanız `package_tests/knst_window` örneği içindeki gibi o halde contenti userdata ile geçirdikten sonra görüntüyü çizmeye başlamak için opengl contentinin içindeki BeginFrame() fonksiyonu ile başlamanızı öneriyorum bu hem title bar alanını ve içerik alanını çapraz platform'a uygun şekilde ayarlar hemde her görüntünün temiz bir şekilde çizilmesini sağlar

- OpenGl contentinizi en son `Shutdown()` ile kapatmanız gerekmetedir


#### Vulkan : ####

- Vulkan desteğinde ise sadece gerekli kaynakları sağlamaktayız özel title bar çizim işlemleri temaları vs yapmak isterim tabikide ancak şimdilik kütüphanenin bu sürümünde sadece kaynakları sağlamakla yetiniyoruz


- `knst_window_vulkan_content content` ile content oluşturabilir
- content.Init(`knst_window`) knst_window ' a pencerenizi verip contentinize pencerenizi bağlayabilirsiniz


- Vulkan contentinizi en son `Destroy()` ile kapatmanız gerekmektedir


## Android ##

- `knst_window` çapraz platformlu bir kütüphane olduğu için sizlere bu platform farkını en düşük seviyede hissettirmemeye çalışır ,ancak androide gelince iş biraz değişiyor

- Temel bi uygulama yapmak için android tarafı için tek farklılık burda `int main` yerine `void android_main(struct android_app* app)` ile uygulamanız başlar ve masaüstündekine göre tek fark burda `KnstWindowSources::Init()` fonksiyonunun `struct android_app* app` parametresi beklemesidir sizin farklı olarak yapmanız gereken tek şey `KnstWindowSources::Init(app)` sadece budur , bu çapraz platformu sağlar ancak tabiki sizinde istiyeceğiniz gibi androidde çok daha fazla şeyi kontrol etmeniz gerekmetedir örneğin uygulama arka plana alındığında nolcak sanal klavye açılınca nolcak nasıl olcak uygulama kapanma durumu gibi bunlarıda zaten ilerleyen kısımda bahsedicem

- Androidde sanal klavye açmanız için `knst_mobile_keyboard` sınıfını tasarladım bu sınıfın içindeki 

```cpp
    static bool hide(); // klavyeyi gizler
    static void toggle(); // klavyeyi gözükür yapar
    static bool is_visible(); // klavye açık mı kapalımı onun durumunu döndürür
```
- Metotları ile mobil klavye ile iletişime geçebilirsiniz Init ve Shutdown yapmak size düşmüyor KnstWindowSources kendi içerisinde bunları hallediyor , ayrıca detaylı örnek `package_tests/knst_window/android` içerisinde mevcuttur

- Ayrıca belirtmek isterimki Android tarafında uygulama kapatılırken ,her zaman `KNST_CLOSE_WINDOW` `KNST_DISCONNECT` eventleri gelmiyor bazen OS zorla uygulamayı bırakıyor ve kaynakları kendisi silebiliyor bu durumda sizin tek yapmanız gereken şey `KNST_SAVE_STATE` eventinde eğer kaydetmeniz gereke birşey varsa uygulamanız kapanmadan önce onu kaydetmenizdir `KNST_SAVE_STATE` eventi uygulama kapanmadan önce kesin gelmektedir

- Ayrıca `creation()` fonksiyonuna verceğin parametreler geçersizdir onları biz belirleyemeyiz , bazı gerekli bilgileri `AndroidManifest.xml` içinden tanımlayabilirsiniz



#### Yardımcı Yapılar ####

### knst_display ---> Sistemdeki ekranlar ve ekran hakkında bilgileri saklar

- `refresh_screens()` ---> ile sistemdeki güncel takılı ekranların bilgileri alır ve `knst_vector` içerisindeki `knst_monitor` nesnelerine kaydeder 
- `KnstWindowSources::Init()` içerisinde zaten `refresh_screens()` çağrısı yapılmaktadır

- `get_monitor_list()` ---> bu fonksiyon ise mevcut knst_vector<knst_monitor> nesnesini döndürür bunun içindeki ekranları döngü yardımıyla alabilirsiniz

- `get_primary_monitor()` ---> bu fonksiyonla asıl aktif olan monitor / ekranının knst_monitor nesnesini alırsınız 

Örnek vericek olursam:
```cpp
    for (size_t i = 0; i < knst_display::get_monitor_list().size(); i++) {
        const auto& mon = knst_display::get_monitor_list()[i];
        std::cout << "\n--- Monitor " << (i + 1) << " ---" << std::endl;
        std::cout << "Name: " << mon.name << std::endl;
        std::cout << "Primary: " << (mon.is_primary ? "Yes" : "No") << std::endl;
        std::cout << "Position: (" << mon.root_x << ", " << mon.root_y << ")" << std::endl;
        std::cout << "Resolution: " << mon.width << "x" << mon.height << std::endl;
        std::cout << "Physical size: " << mon.physical_width << "x" << mon.physical_height << " mm" << std::endl;
        std::cout << "Refresh rate: " << mon.refresh_rate << " Hz" << std::endl;
        std::cout << "DPI: " << mon.dpi_scale << std::endl;
    }
```
___

### knst_image_loader ---> İstediğiniz yoldaki resim dosyasını okuyabilir (şimdilik sadece .bmp)

- Kod içerisinden gerçek örnek
```cpp
    #define KNST_BITMAP_16_16       (1 << 0)
    #define KNST_BITMAP_24_24       (1 << 1)
    #define KNST_BITMAP_32_32       (1 << 2)
    #define KNST_BITMAP_48_48       (1 << 3)
    #define KNST_BITMAP_64_64       (1 << 4)
    #define KNST_BITMAP_96_96       (1 << 5)
    #define KNST_BITMAP_128_128     (1 << 6)
    #define KNST_BITMAP_256_256     (1 << 7)


    #define KNST_BITMAP_OUTPUT_RGB   (1 << 8)   
    #define KNST_BITMAP_OUTPUT_RGBA  (1 << 9)   
    #define KNST_BITMAP_OUTPUT_BGR   (1 << 10)  
    #define KNST_BITMAP_OUTPUT_BGRA  (1 << 11)  


    #define KNST_BITMAP_GET_SIZE(flags)     ((flags) & 0xFF)
    #define KNST_BITMAP_GET_FORMAT(flags)   ((flags) & 0xFF00)
```

- Makroları ile `|` ile `KNST_BITMAP_96_96 | KNST_BITMAP_OUTPUT_BGRA` gibi birleşik tanımlar yapabilirsiniz

```cpp
    int width,height;

    // bu örnek çapraz platformun hepsinde çalışır (android dışında) test edildi
    knst_byte_string bmp_data = knst_image_loader::load_bmp("/home/knst_user/Desktop/KernelNucleusT/icon_example/cpp_logo.bmp",&width,&height,KNST_BITMAP_64_64 | KNST_BITMAP_OUTPUT_RGBA);

    // Kütüphane içerisinde 
    window.set_bmp_cursor(bmp_data,width,height);
    window.apply_bmp_icon(bmp_data,width,height)
    // gibi amaçlar için kullanılmaktadır
    // tabiki bu ayarlar pencere oluşturulduktan 'creation()' dan sonra yapılmalıdır
```

## knst_window Metot ve Makro Özellikleri


## PLATFORM VE CONTENT ALGILAMA MAKROLARI
```cpp
#define KNST_LINUX_PLATFORM_WAYLAND // Eğer Linux Wayland kullanıyorsanız
#define KNST_LINUX_PLATFORM_X11 // Eğer Linux X11 kullanıyorsanız
#define KNST_USING_PLATFORM_ANDROID // Eğer Android kullanıyorsanız


// eğer Android kullanıyorsanız OpenGl mi Vulkan mı kullandığınızı belirtmeniz gerekmektedir
#define KNST_PLATFORM_ANDROID_OPENGL
#define KNST_PLATFORM_ANDROID_VULKAN


// eğer Linux X11 kullanıyorsanız opengl de iki seçeneğiniz vardır ve mutlaka belirtmeniz gerekmektedir
// EGL / GLX
#define KNST_OPENGL_USING_EGL
#define KNST_OPENGL_USING_GLX
```

___
### Metotlar ###

```cpp

inline void knst_window::creation() noexcept; // pencereyi oluşturur , işletim sistemine tanıtır
inline void knst_window::show() noexcept; // pencereyi göstermek için
KNST_FORCE_INLINE void creation_and_show()noexcept; // pencerenizi önce oluşturur sonrasında gösterir
inline void knst_window::destroy() noexcept; // pencere kaynaklarını yok etmek içindir
inline void knst_window::set_title(const knst_c16string& title) noexcept // pencere başlığını ayarlar
inline void knst_window::move(int root_x, int root_y, const knst_monitor& monitor) noexcept // pencereyi istediğiniz konuma taşır daha önceden bahsettiğim gibi KNST_DEFAULT parametresi ile değişmesini istemediğiniz kordinata o parametreyi verebilirsiniz mesela sadece root_x değişcek root_y sabit kalıcaksa root_y yerine KNST_DEFAULT makrosunu verebilirsiniz ,!! önemli uyarı waylandda güvenlik nedeniyle çalışmaz , waylandı davranışına dökümandan bakabilirsiniz
inline void knst_window::move(int root_x, int root_y) noexcept; // pencereyi istediğiniz yere taşır
inline void knst_window::toggle_fullscreen(bool fullscreen) noexcept; // pencerenizi full screen durumuna getirir
inline void knst_window::set_minimized() noexcept; // pencerenizi minimized durumuna getirir
inline void knst_window::set_maximized() noexcept; // pencerenizi maximized durumuna getirir
inline void knst_window::restore() noexcept; // pencerenizin durumunu eski haline getirir
inline void knst_window::hide() noexcept; // pecerenizi gizler
inline void knst_window::focus() noexcept; // pencerenizi öne çıkartıp focus atar

inline void knst_window::set_cursor(uint16_t cursor_type) noexcept; // işletim sisteminin cursorlarını kullanabilirsiniz
#define KNST_CURSOR_ARROW          
#define KNST_CURSOR_IBEAM          
#define KNST_CURSOR_CROSSHAIR      
#define KNST_CURSOR_HAND           
#define KNST_CURSOR_HRESIZE       
#define KNST_CURSOR_VRESIZE        
#define KNST_CURSOR_MOVE           
#define KNST_CURSOR_WAIT           
#define KNST_CURSOR_HELP           
#define KNST_CURSOR_NOT_ALLOWED    
// gibi makroları parametre olarak verebilirsiniz

inline void knst_window::apply_bmp_icon(const knst_byte_string& bytes, int icon_width, int icon_height) noexcept; // uygulamanızın logosunu atamanıza olanak sağlar ! waylandda .desktop ile yapabilirsiniz

inline void knst_window::set_bmp_cursor(const knst_byte_string& data,int width,int height,int hot_x, int hot_y) noexcept; // uygulamanızdaki cursoru istediğiniz bmp dosyası yapar

inline void knst_window::reset_cursor() noexcept; // cursoru eski haline getirir

inline void knst_window::resize(int width, int height) noexcept; // pencerenin boyutunu değiştirir

inline void knst_window::set_cursor_mode(int mode) noexcept; // cursorun durumunu ayarlar
#define KNST_CURSOR_NORMAL
#define KNST_CURSOR_HIDDEN
#define KNST_CURSOR_DISABLED
// gibi makrolarla cursorun durumunu ayarlayabilirsiniz

inline void knst_window::set_cursor_pos_on_window(int x, int y) noexcept; // pencere içindeki cursorun konumunu ayarlar 

inline void knst_window::set_cursor_pos_global(int root_x, int root_y) noexcept; // cursorun konumunu bütün ekrana göre atar


inline void knst_window::set_clipboard(const knst_c16string& text) noexcept; // işletim sisteminin clipbardına /panosuna istediğiniz değeri yapıştırır

inline void knst_window::request_clipboard() noexcept; // işletim sisteminden clipboardı/pano'daki veriyi alır ve knst_window nesnenizin içindeki clipboard_text değişkenine atar ve sizde get_clipboard() ile panodaki veriyi alabilirsiniz

inline void knst_window::set_drag_drop_status(bool enabled) noexcept; // uygulamanız içindeki drag, drop durumunu ayarlamanızı sağlar

inline void knst_window::set_opacity(float opacity) noexcept; // uygulamanızın opaklığını ayarlar ! waylandda sadece title bar ın opaklığını ayarlar içeriği sizin ayarlamanız gerekiyor


inline void knst_window::set_attribute(int attribute, bool value) noexcept; // pencereye özellik atayabilirsiniz ilk parametre makro ikincisi o değerin açıkmı kapalımı olacağıdır
#define KNST_WINDOW_ATTRIB_DECORATED 
#define KNST_WINDOW_ATTRIB_RESIZABLE
#define KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP
#define KNST_WINDOW_ATTRIB_TRANSPARENT

inline bool knst_window::get_attribute(int attribute) const noexcept; // yukarıdaki makroları parametre olarak alır ve pencerenizde verdiğiniz özelliğin durumunu döndürür

inline void knst_window::set_minimum_size(int width, int height) noexcept; // pencerenin boyutunun en az kaç olacağını belirtirsiniz

inline void knst_window::set_maximum_size(int width, int height) noexcept; // pencere boyutunun en fazla ne kadar olacağını belirtirsiniz


KNST_FORCE_INLINE const knst_c16string& get_title() const noexcept; // pencerenin titlesini verir
KNST_FORCE_INLINE void set_user_data(void* data)noexcept; // penceye özel verinizi atamanızı sağlar
KNST_FORCE_INLINE const void* get_user_data()const noexcept; // pencereden özel verinizi almanızı sağlar
KNST_FORCE_INLINE const bool& is_should_close()const noexcept; // pencerenizin kapatılması gerektiğini belirtir
KNST_FORCE_INLINE void should_close() noexcept; // pencerenin kapatılmasını söyler
KNST_FORCE_INLINE void clear_temporary_events() noexcept; // her döngüde sıfırlanması gereken eventleri sıfırlar
KNST_FORCE_INLINE const knst_window_event& get_window_event_handle() const noexcept; // pencerenizin içindeki event nesnesini döndürür

template<typename Callback>
KNST_FORCE_INLINE void set_redraw_callback(Callback&& callback) noexcept; // callback atamanızı sağlar
KNST_FORCE_INLINE void set_redraw_callback(void (*callback)(knst_window&, void*)) noexcept; // callback atamanızı sağlar
KNST_FORCE_INLINE void call_redraw_callback() noexcept; // callbacki çağırmaya yarar
KNST_FORCE_INLINE const knst_c16string& get_clipboard() const noexcept; // pencere nesnesindeki clipboard / panodan kopyalanan veriyi alırsınız
KNST_FORCE_INLINE void clear_clipboard() noexcept; // pencere nesnesindeki cliboard / pandoki veriyi sıfırlar
KNST_FORCE_INLINE const float& get_opacity() const noexcept; // opacity değerini döndürür
KNST_FORCE_INLINE void set_title_bar_height(int height) noexcept; // title bar uzunluğu atamak için
KNST_FORCE_INLINE int get_title_bar_height() const noexcept; // title bar uzunluğunu almak için

//platforma özel nesneler gerçek örnek:
// aynı zamanda global olarak saklanan nesneleri almak içinde KnstWindowSources içerisinden alabilirsiniz

#if KNST_USING_PLATFORM_WINDOWS
        
    KNST_FORCE_INLINE const HWND& get_windows_window_handle() const noexcept{
        return m_window;
    }

#elif KNST_USING_LINUX_PLATFORM_X11

    KNST_FORCE_INLINE const xcb_window_t& get_x11_window_handle() const noexcept{
        return m_window;
    }

#elif KNST_USING_LINUX_PLATFORM_WAYLAND

        KNST_FORCE_INLINE const wl_surface * get_wayland_surface_handle() const noexcept{
            return m_surface;
        }

#endif


```






## 1. MOUSE TUŞLARI

| Makro | Açıklama |
|-------|----------|
| KNST_MOUSE_BUTTON_LEFT | Sol fare tuşu |
| KNST_MOUSE_BUTTON_MIDDLE | Orta fare tuşu |
| KNST_MOUSE_BUTTON_RIGHT | Sağ fare tuşu |
| KNST_MOUSE_SCROLL_UP | Tekerlek yukarı |
| KNST_MOUSE_SCROLL_DOWN | Tekerlek aşağı |

___

## 2. MOUSE EVENTLERİ

| Makro | Açıklama |
|-------|----------|
| KNST_MOUSE_BUTTON_PRESS | Fare tuşuna basıldı |
| KNST_MOUSE_BUTTON_RELEASE | Fare tuşu bırakıldı |
| KNST_MOUSE_SCROLL | Tekerlek hareket etti |
| KNST_MOUSE_EVENT | Fare olayı |
___

## 3. Klavye Tuşları

## Tuşlar (A-Z)

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_A | A tuşu |
| KNST_KEY_B | B tuşu |
| KNST_KEY_C | C tuşu |
| KNST_KEY_D | D tuşu |
| KNST_KEY_E | E tuşu |
| KNST_KEY_F | F tuşu |
| KNST_KEY_G | G tuşu |
| KNST_KEY_H | H tuşu |
| KNST_KEY_I | I tuşu |
| KNST_KEY_J | J tuşu |
| KNST_KEY_K | K tuşu |
| KNST_KEY_L | L tuşu |
| KNST_KEY_M | M tuşu |
| KNST_KEY_N | N tuşu |
| KNST_KEY_O | O tuşu |
| KNST_KEY_P | P tuşu |
| KNST_KEY_Q | Q tuşu |
| KNST_KEY_R | R tuşu |
| KNST_KEY_S | S tuşu |
| KNST_KEY_T | T tuşu |
| KNST_KEY_U | U tuşu |
| KNST_KEY_V | V tuşu |
| KNST_KEY_W | W tuşu |
| KNST_KEY_X | X tuşu |
| KNST_KEY_Y | Y tuşu |
| KNST_KEY_Z | Z tuşu |

## Türkçe Karakterler

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_C_CEDILLA | Ç harfi |
| KNST_KEY_G_BREVE | Ğ harfi |
| KNST_KEY_I_DOTLESS | ı harfi |
| KNST_KEY_O_DIAERESIS | Ö harfi |
| KNST_KEY_S_CEDILLA | Ş harfi |
| KNST_KEY_U_DIAERESIS | Ü harfi |

## Sayılar (0-9)

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_0 | 0 tuşu |
| KNST_KEY_1 | 1 tuşu |
| KNST_KEY_2 | 2 tuşu |
| KNST_KEY_3 | 3 tuşu |
| KNST_KEY_4 | 4 tuşu |
| KNST_KEY_5 | 5 tuşu |
| KNST_KEY_6 | 6 tuşu |
| KNST_KEY_7 | 7 tuşu |
| KNST_KEY_8 | 8 tuşu |
| KNST_KEY_9 | 9 tuşu |

## Fonksiyon Tuşları (F1-F12)

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_F1 | F1 tuşu |
| KNST_KEY_F2 | F2 tuşu |
| KNST_KEY_F3 | F3 tuşu |
| KNST_KEY_F4 | F4 tuşu |
| KNST_KEY_F5 | F5 tuşu |
| KNST_KEY_F6 | F6 tuşu |
| KNST_KEY_F7 | F7 tuşu |
| KNST_KEY_F8 | F8 tuşu |
| KNST_KEY_F9 | F9 tuşu |
| KNST_KEY_F10 | F10 tuşu |
| KNST_KEY_F11 | F11 tuşu |
| KNST_KEY_F12 | F12 tuşu |

## Kontrol Tuşları

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_ESCAPE | ESC tuşu |
| KNST_KEY_ENTER | Enter tuşu |
| KNST_KEY_SPACE | Boşluk tuşu |
| KNST_KEY_BACKSPACE | Backspace tuşu |
| KNST_KEY_TAB | Tab tuşu |
| KNST_KEY_CAPS_LOCK | Caps Lock tuşu |
| KNST_KEY_NUM_LOCK | Num Lock tuşu |
| KNST_KEY_SCROLL_LOCK | Scroll Lock tuşu |
| KNST_KEY_INSERT | Insert tuşu |
| KNST_KEY_DELETE | Delete tuşu |
| KNST_KEY_PRINT | Print Screen tuşu |
| KNST_KEY_PAUSE | Pause tuşu |
| KNST_KEY_BREAK | Break tuşu |

## Modifier Tuşları

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_SHIFT | Shift tuşu |
| KNST_KEY_CONTROL | Ctrl tuşu |
| KNST_KEY_ALT | Alt tuşu |
| KNST_KEY_SUPER | Windows/Command tuşu |
| KNST_KEY_MENU | Menu tuşu |

## Yön Tuşları

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_LEFT | Sol ok tuşu |
| KNST_KEY_RIGHT | Sağ ok tuşu |
| KNST_KEY_UP | Yukarı ok tuşu |
| KNST_KEY_DOWN | Aşağı ok tuşu |
| KNST_KEY_HOME | Home tuşu |
| KNST_KEY_END | End tuşu |
| KNST_KEY_PAGE_UP | Page Up tuşu |
| KNST_KEY_PAGE_DOWN | Page Down tuşu |

## Numped Tuşları

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_NUMPAD_0 | Numpad 0 |
| KNST_KEY_NUMPAD_1 | Numpad 1 |
| KNST_KEY_NUMPAD_2 | Numpad 2 |
| KNST_KEY_NUMPAD_3 | Numpad 3 |
| KNST_KEY_NUMPAD_4 | Numpad 4 |
| KNST_KEY_NUMPAD_5 | Numpad 5 |
| KNST_KEY_NUMPAD_6 | Numpad 6 |
| KNST_KEY_NUMPAD_7 | Numpad 7 |
| KNST_KEY_NUMPAD_8 | Numpad 8 |
| KNST_KEY_NUMPAD_9 | Numpad 9 |
| KNST_KEY_NUMPAD_ADD | Numpad toplama (+) |
| KNST_KEY_NUMPAD_SUBTRACT | Numpad çıkarma (-) |
| KNST_KEY_NUMPAD_MULTIPLY | Numpad çarpma (*) |
| KNST_KEY_NUMPAD_DIVIDE | Numpad bölme (/) |
| KNST_KEY_NUMPAD_DECIMAL | Numpad ondalık (.) |
| KNST_KEY_NUMPAD_ENTER | Numpad enter |

## Noktalama İşaretleri

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_SEMICOLON | ; noktalı virgül |
| KNST_KEY_SLASH | / eğik çizgi |
| KNST_KEY_GRAVE | ` ters tırnak |
| KNST_KEY_LEFT_BRACKET | [ sol köşeli parantez |
| KNST_KEY_BACKSLASH | \ ters eğik çizgi |
| KNST_KEY_RIGHT_BRACKET | ] sağ köşeli parantez |
| KNST_KEY_APOSTROPHE | ' kesme işareti |
| KNST_KEY_PERIOD | . nokta |
| KNST_KEY_COMMA | , virgül |
| KNST_KEY_MINUS | - eksi |
| KNST_KEY_PLUS | + artı |
| KNST_KEY_EQUALS | = eşittir |
| KNST_KEY_QUOTE | " tırnak |
| KNST_KEY_COLON | : iki nokta |
| KNST_KEY_TILDE | ~ tilde |
| KNST_KEY_LESS | < küçüktür |
| KNST_KEY_GREATER | > büyüktür |
| KNST_KEY_QUESTION | ? soru işareti |
| KNST_KEY_PIPE | | dikey çizgi |
| KNST_KEY_EXCLAM | ! ünlem |
| KNST_KEY_AT | @ et işareti |
| KNST_KEY_HASH | # diyez |
| KNST_KEY_DOLLAR | $ dolar |
| KNST_KEY_PERCENT | % yüzde |
| KNST_KEY_CIRCUMFLEX | ^ düzeltme işareti |
| KNST_KEY_AMPERSAND | & ve işareti |
| KNST_KEY_ASTERISK | * yıldız |
| KNST_KEY_LEFT_PAREN | ( sol parantez |
| KNST_KEY_RIGHT_PAREN | ) sağ parantez |
| KNST_KEY_UNDERSCORE | _ alt çizgi |
| KNST_KEY_LEFT_BRACE | { sol süslü parantez |
| KNST_KEY_RIGHT_BRACE | } sağ süslü parantez |

## Medya Tuşları

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_VOLUME_UP | Ses açma |
| KNST_KEY_VOLUME_DOWN | Ses kısma |
| KNST_KEY_VOLUME_MUTE | Sessize alma |
| KNST_KEY_MEDIA_PLAY | Oynat |
| KNST_KEY_MEDIA_STOP | Durdur |
| KNST_KEY_MEDIA_NEXT | Sonraki parça |
| KNST_KEY_MEDIA_PREV | Önceki parça |
| KNST_KEY_MEDIA_PAUSE | Duraklat |

## Tarayıcı Tuşları

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_BROWSER_HOME | Tarayıcı ana sayfa |
| KNST_KEY_BROWSER_BACK | Tarayıcı geri |
| KNST_KEY_BROWSER_FORWARD | Tarayıcı ileri |
| KNST_KEY_BROWSER_REFRESH | Tarayıcı yenile |
| KNST_KEY_BROWSER_SEARCH | Tarayıcı arama |
| KNST_KEY_BROWSER_FAVORITES | Tarayıcı favoriler |

## Mod Tuşları

| Makro | Açıklama |
|-------|----------|
| KNST_MOD_SHIFT | Shift tuşu maskesi |
| KNST_MOD_CONTROL | Ctrl tuşu maskesi |
| KNST_MOD_ALT | Alt tuşu maskesi |
| KNST_MOD_SUPER | Super/Windows tuşu maskesi |
| KNST_MOD_CAPS_LOCK | Caps Lock maskesi |
| KNST_MOD_NUM_LOCK | Num Lock maskesi |


___

## 4. KLAVYE EVENTLERİ

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_PRESS | Tuşa basıldı |
| KNST_KEY_RELEASE | Tuş bırakıldı |
| KNST_KEY_REPEAT | Tuş tekrarlandı |
| KNST_KEYBOARD_EVENT | Klavye olayı |

___

## 5. PENCERE EVENTLERİ

| Makro | Açıklama |
|-------|----------|
| KNST_WINDOW_FULL_SCREEN | Tam ekran |
| KNST_WINDOW_RESTORE | Eski haline dön |
| KNST_MOTION_NOTIFY | Fare hareket etti |
| KNST_WINDOW_RESIZE | Pencere boyutlandı |
| KNST_WINDOW_MOVE | Pencere taşındı |
| KNST_CLOSE_WINDOW | Pencere kapatılıyor |
| KNST_WINDOW_MAXIMIZE | Maksimize |
| KNST_WINDOW_MINIMIZE | Minimize |

## 6. FOCUS EVENTLERİ

| Makro | Açıklama |
|-------|----------|
| KNST_FOCUS_IN | Odak kazanıldı |
| KNST_FOCUS_OUT | Odak kaybedildi |
| KNST_ENTER_NOTIFY | Fare pencereye girdi |
| KNST_LEAVE_NOTIFY | Fare pencereden çıktı |


## 7. Diğer Eventler

| Makro | Açıklama |
|-------|----------|
| KNST_UNKNOWN | Bilinmeyen olay |
| KNST_EXPOSE | Yeniden çizim gerekli |
| KNST_DISCONNECT | Bağlantı koptu |

## 8. CURSOR

| Makro | Açıklama |
|-------|----------|
| KNST_CURSOR_NORMAL | Normal imleç |
| KNST_CURSOR_HIDDEN | Gizli imleç |
| KNST_CURSOR_DISABLED | Devre dışı imleç |

## 9. DOSYA SÜRÜKLEME BIRAKMA (DRAG / DROP)

| Makro | Açıklama |
|-------|----------|
| KNST_FILE_DROP_ENTER | Dosya pencereye girdi |
| KNST_FILE_DROP_MOVE | Dosya hareket etti |
| KNST_FILE_DROP_LEAVE | Dosya pencereden çıktı |
| KNST_FILE_DROP | Dosya bırakıldı |


## 10. PENCERE ÖZELLİKLERİ

| Makro | Açıklama |
|-------|----------|
| KNST_WINDOW_ATTRIB_DECORATED | Başlık çubuğu var |
| KNST_WINDOW_ATTRIB_RESIZABLE | Boyutlandırılabilir |
| KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP | Her zaman üstte |
| KNST_WINDOW_ATTRIB_TRANSPARENT | Şeffaf pencere |



## 11. MOBİL APP DURUMU

| Makro | Açıklama |
|-------|----------|
| KNST_WINDOW_LOST | Pencere kayboldu |
| KNST_LOW_MEMORY | Düşük bellek |
| KNST_APP_STARTED | Uygulama başladı |
| KNST_APP_RESUMED | Uygulama devam ediyor |
| KNST_APP_PAUSED | Uygulama duraklatıldı |
| KNST_SAVE_STATE | Durum kaydedilmeli |
| KNST_CONTENT_RECT_CHANGED | İçerik alanı değişti |
| KNST_CONFIG_CHANGED | Konfigürasyon değişti |
| KNST_INPUT_CHANGED | Giriş yöntemi değişti |
| KNST_APP_STOPPED | Uygulama durduruldu |


## 12. MOBİL DOKUNMA

| Makro | Açıklama |
|-------|----------|
| KNST_MOBILE_TOUCH_EVENT | Dokunmatik olay |


## 13. MOBİL SİSTEM TUŞLARI

| Makro | Açıklama |
|-------|----------|
| KNST_MOBILE_BACK_PRESS | Geri tuşu |
| KNST_MOBILE_HOME_PRESS | Ana ekran tuşu |
| KNST_MOBILE_MENU_PRESS | Menü tuşu |
| KNST_MOBILE_SEARCH_PRESS | Arama tuşu |
| KNST_MOBILE_VOLUME_UP | Ses açma |
| KNST_MOBILE_VOLUME_DOWN | Ses kısma |
| KNST_MOBILE_APP_SWITCH | Uygulama değiştir |
| KNST_MOBILE_RECENT_APPS | Son uygulamalar |
| KNST_MOBILE_VOLUME_MUTE | Sessiz mod |
| KNST_MOBILE_POWER | Güç tuşu |
| KNST_MOBILE_CAMERA | Kamera tuşu |
| KNST_MOBILE_HELP | Yardım tuşu |
| KNST_MOBILE_SETTINGS | Ayarlar tuşu |
| KNST_MOBILE_SLEEP | Uyku modu |
| KNST_MOBILE_WAKEUP | Uyandırma |


## 14. MOBİL MEDYA TUŞLARI

| Makro | Açıklama |
|-------|----------|
| KNST_MOBILE_MEDIA_PLAY_PAUSE | Oynat/Duraklat |
| KNST_MOBILE_MEDIA_STOP | Durdur |
| KNST_MOBILE_MEDIA_NEXT | Sonraki |
| KNST_MOBILE_MEDIA_PREVIOUS | Önceki |
| KNST_MOBILE_MEDIA_REWIND | Geri sar |
| KNST_MOBILE_MEDIA_FAST_FORWARD | Hızlı ileri |
| KNST_MOBILE_MEDIA_RECORD | Kaydet |
| KNST_MOBILE_MEDIA_CLOSE | Kapat |
| KNST_MOBILE_MEDIA_EJECT | Çıkart |
| KNST_MOBILE_MEDIA_PAUSE | Duraklat |

## 15. MOBİL SİSTEM AKSİYONLARI

| Makro | Açıklama |
|-------|----------|
| KNST_MOBILE_NOTIFICATION | Bildirimler |
| KNST_MOBILE_ASSIST | Asistan |
| KNST_MOBILE_VOICE_ASSIST | Sesli asistan |
| KNST_MOBILE_BOOKMARK | Yer imi |
| KNST_MOBILE_CALCULATOR | Hesap makinesi |
| KNST_MOBILE_CALENDAR | Takvim |
| KNST_MOBILE_CONTACTS | Kişiler |
| KNST_MOBILE_EXPLORER | Dosya gezgini |
| KNST_MOBILE_MUSIC | Müzik çalar |

## 16. MOBİL NUMPAD TUŞLARI

| Makro | Açıklama |
|-------|----------|
| KNST_KEY_NUMPAD_COMMA | Numpad virgül |
| KNST_KEY_NUMPAD_EQUALS | Numpad eşittir |
| KNST_KEY_NUMPAD_LEFT_PAREN | Numpad sol parantez |
| KNST_KEY_NUMPAD_RIGHT_PAREN | Numpad sağ parantez |

## 17. MOBİL DOKUNMA AKSİYONLARI

| Makro | Açıklama |
|-------|----------|
| KNST_MOBILE_TOUCH_ACTION_PRESS | Parmak bastı |
| KNST_MOBILE_TOUCH_ACTION_RELEASE | Parmak kaldırdı |
| KNST_MOBILE_TOUCH_ACTION_MOVE | Parmak hareket etti |
| KNST_MOBILE_TOUCH_ACTION_CANCEL | Olay iptal edildi |
| KNST_MOBILE_TOUCH_ACTION_OUTSIDE | Pencere dışına tıklandı |
| KNST_MOBILE_TOUCH_ACTION_POINTER_PRESS | İkincil parmak bastı |
| KNST_MOBILE_TOUCH_ACTION_POINTER_RELEASE | İkincil parmak kaldırdı 


## 18. MOBİL EKRANI ORYANTASYONU

| Makro | Açıklama |
|-------|----------|
| KNST_MOBILE_ORIENTATION_UNDEFINED | Tanımsız |
| KNST_MOBILE_ORIENTATION_PORTRAIT | Dikey mod |
| KNST_MOBILE_ORIENTATION_LANDSCAPE | Yatay mod |
| KNST_MOBILE_ORIENTATION_SQUARE | Kare mod |

// Android sürümüne göre değişebilir 
## 19. MOBİL NIGHT MODE

| Makro | Açıklama |
|-------|----------|
| KNST_MOBILE_NIGHT_MODE_OFF | Gece modu kapalı |
| KNST_MOBILE_NIGHT_MODE_ON | Gece modu açık |

## 20. DEFAULT

| Makro | Açıklama |
|-------|----------|
| KNST_DEFAULT | Varsayılan/değer atanmamış durum |
