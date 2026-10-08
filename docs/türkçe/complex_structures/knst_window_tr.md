# knst_window — Dokümantasyon

> Cross-platform pencere kütüphanesi.
> Windows · Linux (X11 + Wayland) · Android

---

## İçindekiler

1. [Genel Bakış](#genel-bakış)
2. [Kurulum ve İlk Pencere](#kurulum-ve-i̇lk-pencere)
3. [Sınıflar ve Yapılar](#sınıflar-ve-yapılar)
   - [knst_window](#knst_window)
   - [knst_window_event](#knst_window_event)
   - [knst_monitor](#knst_monitor)
   - [knst_display](#knst_display)
   - [KnstWindowSources](#knstwwindowsources)
   - [knst_window_event_system](#knst_window_event_system)
   - [knst_mobile_keyboard](#knst_mobile_keyboard-android)
4. [Kurucular](#kurucular)
5. [Pencere Fonksiyonları](#pencere-fonksiyonları)
6. [Olay (Event) Fonksiyonları](#olay-event-fonksiyonları)
7. [İmleç Fonksiyonları](#i̇mleç-fonksiyonları)
8. [Pano (Clipboard) Fonksiyonları](#pano-clipboard-fonksiyonları)
9. [Pencere Özellikleri](#pencere-özellikleri)
10. [Monitör Fonksiyonları](#monitör-fonksiyonları)
11. [Makro Referansı](#makro-referansı)
    - [Olay Tipleri](#1-olay-tipleri-evtype)
    - [Tuş Aksiyonları](#2-tuş-aksiyonları-evkey_action)
    - [Fare Aksiyonları](#3-fare-aksiyonları-evmouse_action)
    - [Fare Düğmeleri](#4-fare-düğmeleri-evmouse_button)
    - [Modifier Bayrakları](#5-modifier-bayrakları-evmods)
    - [Tuş Kodları](#6-tuş-kodları-evkey_code)
    - [İmleç Tipleri](#7-i̇mleç-tipleri)
    - [Pencere Özellikleri](#8-pencere-özellikleri-makroları)
    - [Android Özel](#9-android-özel-makroları)
12. [Platform Notları](#platform-notları)
    - [Windows](#windows)
    - [Linux X11](#linux-x11)
    - [Linux Wayland](#linux-wayland)
    - [Android](#android)
13. [Sık Sorulan Sorular](#sık-sorulan-sorular)
14. [İpuçları ve Kalıplar](#i̇puçları-ve-kalıplar)

---

## Genel Bakış

`knst_window`, tek bir `#include "KernelNucleusT.hpp"` ile 4 platformda pencere açmanı sağlar. Kütüphane **header-only**'dir — link edilecek binary yok.

### Felsefe

- **Olay senin, çizim senin.** Kütüphane olayları toplar, pencereyi yönetir; çizimden sorumlu değildir.
- **`noexcept` + `bool` dönüş.** Exception fırlatmaz; hata kontrolü senin elinde.
- **Zero-cost abstraction.** Modern C++ (`if constexpr`, `force_inline`, ring buffer) ile yazıldı. Amacı , maksimum performans ve esneklik.

### Kütüphanenin İskeleti

```
KnstWindowSources::Init()        ← Platform kaynaklarını hazırla
        ↓
window.creation_and_show()       ← Pencereyi oluştur ve göster
        ↓
    ┌─────────────────────┐
    │  while (!closing)   │       ← Ana döngü
    │    poll_events()    │
    │    redraw_callback()│
    └─────────────────────┘
        ↓
window.destroy()
KnstWindowSources::CleanUp()     ← Kaynakları temizle
```

---

## Kurulum ve İlk Pencere

### Derleme

Header-only olduğu için sadece `include/` klasörünü projene ekleyebilirsiniz ancak waylandda protocol dosyaları gereği tam anlamda full Header-only denemez 'README.md' deki derleme komutlarını kullanarak derlemeyi yapabilirsiniz:

```cmake
add_subdirectory(KernelNucleusT)
target_link_libraries(benim_uygulamam PRIVATE KernelNucleusT::KernelNucleusT)
```

CMake otomatik olarak platform makrolarını (`KNST_USING_PLATFORM_WINDOWS`, `KNST_USING_LINUX_PLATFORM_X11`, vs.) tanımlar, gerekli kütüphaneleri (GDI, xcb, wayland-client, vs.) bağlar.

### En Basit Örnek

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

KNST_FORCE_INLINE static void on_frame(knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);

        if (ev.type == KNST_WINDOW_EVENT_KEYBOARD &&
            ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS &&
            ev.key_code == KNST_WINDOW_KEY_CODE_ESCAPE) {
            w.should_close();
        }
    }
    w.clear_events();
}

int main() {
    KnstWindowSources::Init();
    knst_window window(800, 600, "Selam Dünya");
    window.set_redraw_callback(on_frame);
    window.creation_and_show();

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
    }

    window.destroy();
    KnstWindowSources::CleanUp();
}

// Bilgi notu : call_redraw_callback mantığı windows tarafındaki , 'resize olurken renderı kesme' sorununu çözmek için  oluşturulmuştur , zorunlu değildir ancak çapraz platformluğu tam sağlamak isterseniz örnekteki gibi yapabilirsiniz , her platformda aynı sonucu verir.
```

---

## Sınıflar ve Yapılar

### `knst_window`

Ana pencere sınıfı. Her örnek **kopyalanamaz** ama **taşınabilir** (move-only).

**Sorumlulukları:**
- Pencereyi oluşturmak, göstermek, yok etmek
- Olay kuyruğunu yönetmek
- Çizim callback'ini çağırmak
- Pano, imleç, pencere özelliklerini yönetmek


### `knst_window_event`

Tek bir olayı temsil eder. Tüm olay alanları bu struct içindedir.

```cpp
struct knst_window_event {
    uint32_t type;              // KNST_WINDOW_EVENT_*
    uint32_t timestamp_ms;      // ms cinsinden zaman

    int32_t  mods;              // Modifier bitleri (shift/ctrl/...)
    int32_t  mouse_x;           // Fare X (pencere içi)
    int32_t  mouse_y;           // Fare Y (pencere içi)
    int32_t  mouse_root_x;      // Fare X (ekran)
    int32_t  mouse_root_y;      // Fare Y (ekran)
    int32_t  window_width;      // Pencere genişliği
    int32_t  window_height;     // Pencere yüksekliği
    int32_t  window_root_x;     // Pencere konumu X (ekran)
    int32_t  window_root_y;     // Pencere konumu Y (ekran)

    bool is_focused;
    bool mouse_on_window;
    bool is_full_screen;
    bool is_maximized;
    bool is_minimized;

    // Union: fare VEYA klavye alanları
    union {
        struct { int32_t mouse_button; int32_t mouse_action; int32_t mouse_scroll_delta; };
        struct { int32_t key_code; int32_t scancode; int32_t key_action; };
    };

    // Dosya sürükle-bırak
    std::shared_ptr<knst_vector<knst_c16string>> drop_files;
    uint32_t drop_count;

    #if defined(KNST_USING_PLATFORM_ANDROID)
        // Android'e özel alanlar
        int pointer_count;
        float pointer_x[10];      // 10 parmağa kadar
        float pointer_y[10];
        int pointer_id[10];
        int touch_action;
        int content_left, content_top, content_right, content_bottom;
        int orientation;
        char language[4];
        char country[4];
        bool is_night_mode;
        bool is_low_memory;
        float density;
        int screen_width_dp, screen_height_dp;
        void* saved_state;
        size_t saved_state_size;
    #endif
};
```

### `knst_monitor`

Bir monitörü temsil eder.

```cpp
class knst_monitor {
public:
    int root_x = 0;              // Ekran konumu X
    int root_y = 0;              // Ekran konumu Y
    int width = 0;               // Piksel genişliği
    int height = 0;              // Piksel yüksekliği
    int physical_width = 0;      // mm cinsinden genişlik
    int physical_height = 0;     // mm cinsinden yükseklik
    float refresh_rate = 60.0f;  // Hz
    float dpi_scale = 96.0f;     // DPI
    bool is_primary = false;     // Ana monitör mü?
    knst_c16string name;         // Monitör adı
};
```

### `knst_display`

Tüm monitörleri yöneten statik sınıf.

```cpp
class knst_display {
public:
    // Monitör listesini yeniden tara
    static void refresh_screens() noexcept;

    // Tüm monitörleri al
    static const knst_vector<knst_monitor>& get_monitor_list() noexcept;

    // Ana monitörü al
    static const knst_monitor* get_primary_monitor() noexcept;
};
```

**Notlar:**
- `refresh_screens()` pahalıdır (sistem çağrıları). Pencere açılışında bir kere çağır.
- Monitör değişikliklerini otomatik yakalamaz; istersen periyodik çağır.

### `KnstWindowSources`

Platform kaynaklarını yönetir. **Doğrudan kullanma**, sadece `Init()` / `CleanUp()` / `get_current_time_ms()`.

```cpp
class KnstWindowSources {
public:
    // ─── Zorunlu ─────────────────────────────────────────
    static void Init() noexcept;             // Windows/Linux
    static void Init(android_app* app);      // Android
    static void CleanUp() noexcept;

    static uint32_t get_current_time_ms() noexcept;

    // ─── Platform Handle Erişimi ─────────────────────────
    #if KNST_USING_PLATFORM_WINDOWS
        static HINSTANCE& get_windows_native_instance_handle() noexcept;
    #elif KNST_USING_LINUX_PLATFORM_X11
        static xcb_window_t& get_native_x11_root_handle() noexcept;
        static xcb_connection_t* get_native_x11_connection_handle() noexcept;
        static Display* get_native_x11_display() noexcept;
    #elif KNST_USING_LINUX_PLATFORM_WAYLAND
        static wl_display* wayland_display;  // public
    #elif defined(KNST_USING_PLATFORM_ANDROID)
        static android_app* get_android_app() noexcept;
    #endif

    // Wayland özel kaynaklar (public)
    #if KNST_USING_LINUX_PLATFORM_WAYLAND
        static wl_registry*    registery;       // Kayıt nesnesi
        static wl_compositor*  compositor;      // Surface yaratıcı
        static xdg_wm_base*    wmBase;          // Pencere yöneticisi
        static wl_shm*         shm;             // Paylaşımlı bellek
        static wl_seat*        seat;            // Girdi aygıtları
        static wl_pointer*     pointer;         // Fare
        static wl_keyboard*    keyboard;        // Klavye
        static wl_cursor_theme* cursor_theme;   // İmleç teması
        static wl_surface*     cursor_surface;  // İmleç surface'i
        // ...
    #endif
};
```

### `knst_window_event_system`

Olay kuyruğunu global olarak yönetir.

```cpp
struct knst_window_event_system {
    // Olayları bekle ve işle (bloklayan)
    static void block_pool_event() noexcept;

    // Olayları anlık işle (bloklamayan, oyunlarda kullan)
    static void non_block_pool_event() noexcept;

    // Kayıtlı pencere sayısı
    static size_t get_window_count() noexcept;
};
```

**`block_pool_event()` vs `non_block_pool_event()`:**

| Fonksiyon | Davranış | Kullanım |
|-----------|----------|----------|
| `block_pool_event()` | Olay gelene kadar bekler | Basit CLI araçları |
| `non_block_pool_event()` | Hemen döner | Oyun döngüleri, animasyonlar |

Linux (Wayland)'da default olarak çizim zorunludur , compositor çoğu zaman title bar vs vermediği için her durumda event gelmektedir.

### `knst_mobile_keyboard` (Android)

Android yumuşak klavyesini kontrol eder.

```cpp
class knst_mobile_keyboard {
public:
    static bool show(); // Klavyeyi aç → başarılıysa true
    static bool hide(); // Klavyeyi kapat
    static void toggle(); // Aç/kapat geçiş
    static bool is_visible();  // Görünür mü?
};
```

**Notlar:**
- `show()` çift tıklama koruması içerir (300ms). Kısa sürede iki kez çağırırsan ikincisi yok sayılır.
- JNI çağrıları içerdiğinden diğer platformlarda **derlenmez**.

---

## Kurucular

### `knst_window` (Masaüstü)

```cpp
knst_window(
    int width = 800,
    int height = 800,
    knst_c16string title = u"Knst_Window",
    int root_x = KNST_WINDOW_DEFAULT,
    int root_y = KNST_WINDOW_DEFAULT,
    const knst_monitor& monitor = knst_monitor()
) noexcept;
```

**Parametreler:**

| Parametre | Açıklama |
|-----------|----------|
| `width`, `height` | Pencere boyutu (piksel) |
| `title` | Pencere başlığı (UTF-16) |
| `root_x`, `root_y` | Monitöre göre göreceli konum. `KNST_WINDOW_DEFAULT` verilirse monitörün ortasına yerleşir |
| `monitor` | Hangi monitörde açılacak. Varsayılan: boş (birincil) |

**Örnekler:**

```cpp
// Basit pencere
knst_window w1(800, 600, u"Merhaba");

// Belirli konumda
knst_window w2(800, 600, u"Pencere", 100, 200);

// İkincil monitörde
knst_display::refresh_screens();
auto monitors = knst_display::get_monitor_list();
if (monitors.size() > 1) {
    knst_window w3(800, 600, u"İkincil", KNST_WINDOW_DEFAULT, KNST_WINDOW_DEFAULT, monitors[1]);
}
```

### `knst_window` (Android)

```cpp
knst_window(
    int width = KNST_WINDOW_DEFAULT,
    int height = KNST_WINDOW_DEFAULT,
    knst_c16string title = u"Knst_Window",
    int root_x = KNST_WINDOW_DEFAULT,
    int root_y = KNST_WINDOW_DEFAULT,
    const knst_monitor& monitor = knst_monitor()
);
```

**Not:** Android'de boyut ve konum parametreleri **yok sayılır** — Android penceresi zaten ekran boyutunda açar.

### Kopyalama / Taşıma

```cpp
knst_window(const knst_window&) = delete;             // Kopyalanamaz
knst_window& operator=(const knst_window&) = delete;

knst_window(knst_window&&) noexcept;                  // Taşınabilir
knst_window& operator=(knst_window&&) noexcept;
```

**Neden move-only?** Pencere, platform kaynaklarını (HWND, xcb_window_t, wl_surface) tutar. Kopyalamak bu kaynakları çift serbest bırakmaya yol açar. Move ise güvenli transfer yapar ve callback'leri günceller.

---

## Pencere Fonksiyonları

### Yaşam Döngüsü

#### `void creation() noexcept`

Pencereyi oluşturur ama ekranda göstermez. Platform kaynağını yaratır ve event sistemine kaydeder.

```cpp
knst_window w;
w.creation();     // Pencere var, ekranda yok
// ... ayarlamalar ...
w.show();         // Şimdi görünür
```

#### `void show() noexcept`

Pencereyi ekranda gösterir.

#### `void creation_and_show() noexcept`

`creation()` + `show()` kısayolu. Örneklerde en sık kullanılan.

#### `void destroy() noexcept`

Pencereyi yok eder ve kaynakları serbest bırakır. Yıkıcı (`~knst_window`) zaten çağırır, ama açıkça çağırmak isteyebilirsin.

#### `void should_close() noexcept`

Pencereyi kapatma sinyali gönderir. Ana döngü bir sonraki iterasyonda `is_should_close()` → `true` görür ve döngüden çıkar.

#### `bool is_should_close() const noexcept`

Kapatma sinyali geldi mi? Ana döngü koşulunda kullan:

```cpp
while (!window.is_should_close()) { /* ... */ }
```

#### `bool is_disconnected() const noexcept`

Bağlantı koptu mu? (Wayland compositor kapandığında `true` olur.)
Compositor'e göre değişmektedir

### Başlık

```cpp
void set_title(const knst_c16string& title) noexcept;
const knst_c16string& get_title() const noexcept;
```

UTF-16 string kullanır. Windows'ta native UTF-16, Linux/Android'de UTF-8'e çevrilir.

### Konum ve Boyut

```cpp
void move(int root_x, int root_y) noexcept;
void move(int root_x, int root_y, const knst_monitor& monitor) noexcept;
void resize(int width, int height) noexcept;
void set_minimum_size(int width, int height) noexcept;
void set_maximum_size(int width, int height) noexcept;
```

**Örnek:**

```cpp
window.move(100, 100); // Ekranın (100,100) noktasına
window.resize(1280, 720); // 1280x720 yap
window.set_minimum_size(400, 300); // Küçültülemez eşiği
window.set_maximum_size(1920, 1080); // Büyütülemez eşiği
window.set_maximum_size(KNST_WINDOW_DEFAULT); // Sınır kaldır
```

### Pencere Durumu

```cpp
void toggle_fullscreen(bool fullscreen) noexcept;
void set_minimized() noexcept;
void set_maximized() noexcept;
void restore() noexcept;
void hide() noexcept;
void focus() noexcept;
void set_opacity(float opacity) noexcept;
float get_opacity() const noexcept;
```

**Örnek:**

```cpp
window.set_minimized();  // Görev çubuğuna küçült
window.set_maximized();  // Tam ekran (maximize)
window.toggle_fullscreen(true);  // Gerçek tam ekran (F11 gibi)
window.restore();  // Normal boyuta dön
window.set_opacity(0.5f);  // Yarı saydam
```

**Notlar:**
- **Android'de** bu fonksiyonların çoğu **yok sayılır** — Android'de bu tür pencere kontrolleri sistem tarafından yönetilir.
- **Wayland'de** `move()` ve `focus()` çalışmaz (compositor güvenlik kısıtlamaları).

### Pencere Özellikleri

```cpp
void set_attribute(int attribute, bool value) noexcept;
bool get_attribute(int attribute) const noexcept;
```

**Kullanılabilir özellikler:**

| Özellik | Açıklama |
|---------|----------|
| `KNST_WINDOW_ATTR_DECORATED` | Sistem çerçevesi gösterilsin mi? |
| `KNST_WINDOW_ATTR_RESIZABLE` | Kullanıcı boyutlandırabilir mi? |
| `KNST_WINDOW_ATTR_ALWAYS_ON_TOP` | Diğer pencerelerin üstünde mi? |
| `KNST_WINDOW_ATTR_TRANSPARENT` | Fare tıklamaları geçsin mi? |

**Örnek:**

```cpp
// Çerçevesiz, sabit boyutlu, hep üstte
window.set_attribute(KNST_WINDOW_ATTR_DECORATED, false);
window.set_attribute(KNST_WINDOW_ATTR_RESIZABLE, false);
window.set_attribute(KNST_WINDOW_ATTR_ALWAYS_ON_TOP, true);

// Tıklamaları pencereye geçirmeyen overlay
window.set_attribute(KNST_WINDOW_ATTR_TRANSPARENT, true);
```

**Platform notları:**
- **Windows:** `WS_POPUP` vs `WS_OVERLAPPEDWINDOW`, `WS_EX_TOPMOST`, `WS_EX_TRANSPARENT` ile uygulanır.
- **X11:** `_MOTIF_WM_HINTS`, `_NET_WM_STATE_ABOVE`, `XFixesSetWindowShapeRegion`.
- **Wayland:** `xdg-decoration`, `xdg_toplevel_set_min/max_size`, `wl_surface_set_input_region`.

### Başlık Çubuğu (Kendi Çizimin)

```cpp
void set_title_bar_height(int height) noexcept;
int get_title_bar_height() const noexcept;
```

Kütüphane varsayılan olarak başlık çubuğu yüksekliğini **DPI'a göre otomatik** hesaplar (96 DPI temel, 34 piksel).

Manuel değiştirmek için:

```cpp
window.set_title_bar_height(48);  // Sabit 48 piksel
```

---

## Olay (Event) Fonksiyonları

### Kuyruk Yönetimi

```cpp
size_t event_count() const noexcept;
const knst_window_event& get_window_event_handle(size_t i) const noexcept;
void clear_events() noexcept;
void dispatch_event(const knst_window_event& ev) noexcept;
void dispatch_current_event() noexcept;
```

**En sık kullanım:**

```cpp
for (size_t i = 0; i < w.event_count(); ++i) {
    const auto& ev = w.get_window_event_handle(i);
    // ... ev ile bir şey yap
}
w.clear_events();  // ← BU ÇOK ÖNEMLİ , Eski eventlerden iz kalmaması için
```

`dispatch_event()` ve `dispatch_current_event()` ileri seviye — kendi olaylarını enjekte etmek için. Test/simülasyon dışında gerekmez.

### Tuş Durumu Sorgulama

```cpp
bool is_key_held(int key_code) const noexcept;
bool is_caps_lock_on() const noexcept;
bool is_num_lock_on() const noexcept;
```

Olay beklemeden o anki tuş durumunu sorar. Metin editörü / oyun için:

```cpp
if (window.is_key_held(KNST_WINDOW_KEY_CODE_W)) {
    player.y += speed * dt;  // W basılı → ileri git
}
```

### Dahili Tuş Takibi (İleri Seviye)

```cpp
knst_window_event::knst_held_key* find_held_by_scancode(int sc) noexcept;
knst_window_event::knst_held_key* add_held_key(int kc, int sc, uint32_t now) noexcept;
void remove_held_key(int sc) noexcept;
void clear_held_keys() noexcept;
```

Bu fonksiyonlar genelde **event manager** tarafından çağrılır. Sen doğrudan kullanmasan da olur.

### Kullanıcı Verisi

```cpp
void set_user_data(void* data) noexcept;
const void* get_user_data() const noexcept;
```

Callback içinde state'ine erişmek için:

```cpp
struct AppState { int counter; };
AppState state;

window.set_user_data(&state);

// on_frame içinde:
static void on_frame(knst_window& w, void* user_data) {
    AppState* s = static_cast<AppState*>(user_data);
    s->counter++;
}
```

**Not:** `set_redraw_callback` zaten `m_user_data`'yı callback'e geçirir. Alternatif olarak `user_data`'yı yakalayan lambda da kullanabilirsin:

```cpp
window.set_redraw_callback([&state](knst_window& w, void*) {
    state.counter++;
});
```

### Çizim Callback'i

```cpp
template<typename Callback>
void set_redraw_callback(Callback&& callback) noexcept;

void call_redraw_callback() noexcept;
```

Callback imzası: `void(knst_window&, void* user_data)`

**Örnek:**

```cpp
window.set_redraw_callback([](knst_window& w, void* data) {
    // Bu frame'de çiz
    for (size_t i = 0; i < w.event_count(); ++i) {
        // Olayları işle
    }
    w.clear_events();

    
});

// Ana döngüde:
while (!window.is_should_close()) {
    knst_window_event_system::non_block_pool_event();
    window.call_redraw_callback();
}
```

---

## İmleç Fonksiyonları

### Sistem İmleçleri

```cpp
void set_cursor(uint16_t cursor_type) noexcept;
```

**Kullanılabilir tipler:**

| Makro | Görünüm |
|-------|---------|
| `KNST_WINDOW_CURSOR_ARROW` | Standart ok |
| `KNST_WINDOW_CURSOR_IBEAM` | Metin seçme |
| `KNST_WINDOW_CURSOR_CROSSHAIR` | Artı |
| `KNST_WINDOW_CURSOR_HAND` | El (link üzerinde) |
| `KNST_WINDOW_CURSOR_HRESIZE` | Yatay boyutlandırma |
| `KNST_WINDOW_CURSOR_VRESIZE` | Dikey boyutlandırma |
| `KNST_WINDOW_CURSOR_MOVE` | Taşıma |
| `KNST_WINDOW_CURSOR_WAIT` | Bekleme (saat) |
| `KNST_WINDOW_CURSOR_HELP` | Yardım |
| `KNST_WINDOW_CURSOR_NOT_ALLOWED` | Yasak |

**Örnek:**

```cpp
window.set_cursor(KNST_WINDOW_CURSOR_HAND);  // Butona gelince el
```

### Özel İmleç

```cpp
void set_bmp_cursor(
    const knst_byte_string& data,
    int width,
    int height,
    int hot_x = -1,
    int hot_y = -1
) noexcept;
```

**Girdi formatı:** RGBA8888 — her pixel 4 byte, sırayla `R, G, B, A`.

**Parametreler:**
- `data`: Ham RGBA byte'ları
- `width`, `height`: Boyutlar
- `hot_x`, `hot_y`: Tıklama noktası (`-1` → otomatik merkez)

**Örnek:**

```cpp
knst_byte_string cursor_data;

// RGBA8888 array'i hazırla
// cursor_data = ...  (32*32*4 = 4096 byte)

window.set_bmp_cursor(cursor_data, 32, 32, 0, 0);  // Hotspot sol üst
window.set_bmp_cursor(cursor_data, 32, 32, 16, 16); // Hotspot merkez
```

**Not:** Kütüphane RGBA'yı her platformda native formata çevirir:
- **Windows:** `CreateIconIndirect` — BGRA + AND mask
- **X11:** `XcursorImageLoadCursor` — ARGB
- **Wayland:** `wl_shm` buffer — ARGB8888

### Sıfırla

```cpp
void reset_cursor() noexcept;
```

Varsayılan ok imlecine döner.

### İmleç Modu

```cpp
void set_cursor_mode(int mode) noexcept;
```

| Mod | Etki |
|-----|------|
| `KNST_WINDOW_CURSOR_ARROW` | Normal |
| `KNST_WINDOW_CURSOR_HIDDEN` | Görünmez (ama hareket eder) |
| `KNST_WINDOW_CURSOR_DISABLED` | Görünmez + kilitle (merkezde tutulur) |

**Örnek — FPS bakış kontrolü:**

```cpp
window.set_cursor_mode(KNST_WINDOW_CURSOR_DISABLED);
// Şimdi fare hareketleri görecelidir, imleç ekranın dışına çıkmaz
```

**Platform notları:**
- **Windows:** `ShowCursor(FALSE)` + `ClipCursor(rect)`
- **X11:** `xcb_grab_pointer` + `warp_pointer`
- **Wayland:** `zwp_locked_pointer_v1` + `zwp_confined_pointer_v1`

### İmleç Konumu

```cpp
void set_cursor_pos_on_window(int x, int y) noexcept;
void set_cursor_pos_global(int root_x, int root_y) noexcept;
```

**Not:** Wayland'de compositor güvenlik nedeniyle **çalışmaz**. X11 ve Windows'ta çalışır.

---

## Pano (Clipboard) Fonksiyonları

### Pano Yaz

```cpp
void set_clipboard(const knst_c16string& text) noexcept;
```

**Örnek:**

```cpp
window.set_clipboard(u"Merhaba dünya!");
```

### Pano Oku

```cpp
void request_clipboard() noexcept;
const knst_c16string& get_clipboard() const noexcept;
void clear_clipboard() noexcept;
```

**Kullanım:**

```cpp
window.request_clipboard();
// Wayland'de asenkron — bir sonraki frame'de içerik gelir
// X11/Windows'ta senkron

// Bir sonraki frame'de:
const auto& text = window.get_clipboard();
std::wcout << text.data() << L"\n";
```

**Platform notları:**
- **Windows:** `OpenClipboard`, `SetClipboardData`, `CF_UNICODETEXT`
- **X11:** `xcb_set_selection_owner`, `XCB_SELECTION_NOTIFY`
- **Wayland:** `wl_data_source` + `wl_data_offer`
- **Android:** JNI üzerinden `ClipboardManager`

---

## Pencere Özellikleri

### `void set_drag_drop_status(bool enabled) noexcept`

Dosya sürükle-bırak'ı aktif/pasif yapar.

```cpp
window.set_drag_drop_status(true);   // Aktif et
window.set_drag_drop_status(false);  // Pasif et
```

**Not:** Android'de yok sayılır.

### `void apply_bmp_icon(const knst_byte_string& data, int width, int height) noexcept`

Pencere ikonunu ayarlar. `data` yine RGBA8888.

```cpp
knst_byte_string icon = load_icon();
window.apply_bmp_icon(icon, 64, 64);
```

**Platform notları:**
- **Windows:** `CreateIcon` + `WM_SETICON`
- **X11:** `_NET_WM_ICON` property
- **Wayland:** Desteklenmez (compositor'a göre değişir)
- **Android:** Yok sayılır

---

## Monitör Fonksiyonları

### Monitörleri Tara

```cpp
knst_display::refresh_screens();
```

Sistemdeki tüm monitörleri listeler. Bu **pahalı bir işlem** — pencere açmadan önce bir kere çağır.

### Monitör Listesi

```cpp
const auto& monitors = knst_display::get_monitor_list();
for (const auto& mon : monitors) {
    std::cout << "İsim:     " << mon.name << "\n";
    std::cout << "Çözünürlük: " << mon.width << "x" << mon.height << "\n";
    std::cout << "Konum:    (" << mon.root_x << ", " << mon.root_y << ")\n";
    std::cout << "DPI:      " << mon.dpi_scale << "\n";
    std::cout << "Yenileme: " << mon.refresh_rate << " Hz\n";
    std::cout << "Fiziksel: " << mon.physical_width << "x" << mon.physical_height << " mm\n";
    std::cout << "Primary:  " << (mon.is_primary ? "evet" : "hayır") << "\n";
}
```

### Ana Monitör

```cpp
const knst_monitor* primary = knst_display::get_primary_monitor();
if (primary) {
    std::cout << "Birincil: " << primary->width << "x" << primary->height << "\n";
}
```

**Platform notları:**
- **Windows:** `EnumDisplayDevicesW`, `GetDpiForMonitor`
- **X11:** `xcb_randr_get_monitors`, `xcb_randr_get_crtc_info`
- **Wayland:** `wl_output` protokolü
- **Android:** `ANativeWindow_getWidth/Height` + `AConfiguration`

---

## Makro Referansı

Tüm makrolar `knst_window_identifiers.hpp` içinde tanımlıdır.

### 1. Olay Tipleri — `ev.type`

Olay aralıkları birbirinden ayrıdır:

| Aralık | Kategori |
|--------|----------|
| `0` | `UNKNOWN` — dağıtılmaz |
| `100..199` | Fare olayları |
| `200..299` | Klavye olayları |
| `300..399` | Pencere olayları |
| `400..499` | Odak olayları |
| `500..599` | Dosya bırakma |
| `600..699` | Mobil |
| `700..799` | Uygulama yaşam döngüsü |

#### Fare (100-199)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_MOUSE` | 100 |
| `KNST_WINDOW_EVENT_MOTION` | 101 |
| `KNST_WINDOW_EVENT_ENTER` | 102 |
| `KNST_WINDOW_EVENT_LEAVE` | 103 |

#### Klavye (200-299)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_KEYBOARD` | 200 |

#### Pencere (300-399)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_RESIZE` | 300 |
| `KNST_WINDOW_EVENT_MOVE` | 301 |
| `KNST_WINDOW_EVENT_MAXIMIZE` | 302 |
| `KNST_WINDOW_EVENT_MINIMIZE` | 303 |
| `KNST_WINDOW_EVENT_RESTORE` | 304 |
| `KNST_WINDOW_EVENT_FULLSCREEN` | 305 |
| `KNST_WINDOW_EVENT_EXPOSE` | 306 |
| `KNST_WINDOW_EVENT_CLOSE` | 307 |
| `KNST_WINDOW_EVENT_DISCONNECT` | 308 |

#### Odak (400-499)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_FOCUS_IN` | 400 |
| `KNST_WINDOW_EVENT_FOCUS_OUT` | 401 |

#### Dosya Bırakma (500-599)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_FILE_DROP` | 500 |
| `KNST_WINDOW_EVENT_FILE_DROP_ENTER` | 501 |
| `KNST_WINDOW_EVENT_FILE_DROP_MOVE` | 502 |
| `KNST_WINDOW_EVENT_FILE_DROP_LEAVE` | 503 |

#### Mobil (600-699)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_MOBILE_TOUCH` | 600 |
| `KNST_WINDOW_EVENT_MOBILE_BACK` | 601 |
| `KNST_WINDOW_EVENT_MOBILE_HOME` | 602 |
| `KNST_WINDOW_EVENT_MOBILE_MENU` | 603 |
| `KNST_WINDOW_EVENT_MOBILE_SEARCH` | 604 |
| `KNST_WINDOW_EVENT_MOBILE_APP_SWITCH` | 605 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_UP` | 607 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_DOWN` | 608 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_MUTE` | 609 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PLAY_PAUSE` | 623 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_NEXT` | 625 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PREVIOUS` | 626 |
| (daha fazlası `identifiers.hpp`'da) | ... |

#### Uygulama Yaşam Döngüsü (700-799)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_APP_STARTED` | 700 |
| `KNST_WINDOW_EVENT_APP_RESUMED` | 701 |
| `KNST_WINDOW_EVENT_APP_PAUSED` | 702 |
| `KNST_WINDOW_EVENT_APP_STOPPED` | 703 |
| `KNST_WINDOW_EVENT_APP_SAVE_STATE` | 704 |
| `KNST_WINDOW_EVENT_APP_LOW_MEMORY` | 705 |
| `KNST_WINDOW_EVENT_APP_CONFIG_CHANGED` | 706 |
| `KNST_WINDOW_EVENT_APP_INPUT_CHANGED` | 707 |
| `KNST_WINDOW_EVENT_APP_CONTENT_RECT` | 708 |
| `KNST_WINDOW_EVENT_APP_WINDOW_LOST` | 709 |

### 2. Tuş Aksiyonları — `ev.key_action`

| Makro | Değer | Anlam |
|-------|-------|-------|
| `KNST_WINDOW_KEY_ACTION_PRESS` | 1 | Tuşa basıldı |
| `KNST_WINDOW_KEY_ACTION_RELEASE` | 2 | Tuş bırakıldı |
| `KNST_WINDOW_KEY_ACTION_REPEAT` | 3 | Tuş tekrarlanıyor |

**Klavye tekrarı** `knst_settings.hpp`'den ayarlanır:

```cpp
#define KNST_WINDOW_KEY_REPEAT_DELAY     100  // İlk tekrar gecikmesi (ms)
#define KNST_WINDOW_KEY_REPEAT_INTERVAL   23  // Tekrar aralığı (ms)
```

### 3. Fare Aksiyonları — `ev.mouse_action`

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_MOUSE_ACTION_PRESS` | 1 |
| `KNST_WINDOW_MOUSE_ACTION_RELEASE` | 2 |
| `KNST_WINDOW_MOUSE_ACTION_SCROLL` | 3 |

### 4. Fare Düğmeleri — `ev.mouse_button`

X11/POSIX standardına uyar:

| Makro | Değer | Fiziksel |
|-------|-------|----------|
| `KNST_WINDOW_MOUSE_BUTTON_LEFT` | 1 | Sol tık |
| `KNST_WINDOW_MOUSE_BUTTON_MIDDLE` | 2 | Orta tık |
| `KNST_WINDOW_MOUSE_BUTTON_RIGHT` | 3 | Sağ tık |
| `KNST_WINDOW_MOUSE_BUTTON_BACK` | 8 | Yan "geri" |
| `KNST_WINDOW_MOUSE_BUTTON_FORWARD` | 9 | Yan "ileri" |

### 5. Modifier Bayrakları — `ev.mods`

Bitsel `|` operatörüyle birleştirilir:

| Makro | Değer | Anlam |
|-------|-------|-------|
| `KNST_WINDOW_MOD_SHIFT` | 1 | Shift |
| `KNST_WINDOW_MOD_CONTROL` | 2 | Ctrl |
| `KNST_WINDOW_MOD_ALT` | 4 | Alt |
| `KNST_WINDOW_MOD_SUPER` | 8 | Windows/Cmd tuşu |
| `KNST_WINDOW_MOD_CAPS_LOCK` | 16 | Caps Lock aktif |
| `KNST_WINDOW_MOD_NUM_LOCK` | 32 | Num Lock aktif |

**Örnek:**

```cpp
bool ctrl_shift_s = (ev.mods & KNST_WINDOW_MOD_CONTROL) &&
                    (ev.mods & KNST_WINDOW_MOD_SHIFT) &&
                    ev.key_code == KNST_WINDOW_KEY_CODE_S;
```

### 6. Tuş Kodları — `ev.key_code`

Tüm platformlarda aynı isim — native değer farklı. Platform detayları için `identifiers.hpp`'ya bak.

#### Harfler

```cpp
KNST_WINDOW_KEY_CODE_A ... KNST_WINDOW_KEY_CODE_Z
```

**Türkçe karakterler:**

```cpp
KNST_WINDOW_KEY_CODE_C_CEDILLA    // Ç
KNST_WINDOW_KEY_CODE_G_BREVE      // Ğ
KNST_WINDOW_KEY_CODE_I_DOTLESS    // ı
KNST_WINDOW_KEY_CODE_O_DIAERESIS  // Ö
KNST_WINDOW_KEY_CODE_S_CEDILLA    // Ş
KNST_WINDOW_KEY_CODE_U_DIAERESIS  // Ü
```

#### Rakamlar

```cpp
KNST_WINDOW_KEY_CODE_0 ... KNST_WINDOW_KEY_CODE_9
KNST_WINDOW_KEY_CODE_NUMPAD_0 ... KNST_WINDOW_KEY_CODE_NUMPAD_9
```

#### Fonksiyon Tuşları

```cpp
KNST_WINDOW_KEY_CODE_F1 ... KNST_WINDOW_KEY_CODE_F12
```

#### Özel Tuşlar

```cpp
KNST_WINDOW_KEY_CODE_ESCAPE
KNST_WINDOW_KEY_CODE_ENTER
KNST_WINDOW_KEY_CODE_SPACE
KNST_WINDOW_KEY_CODE_BACKSPACE
KNST_WINDOW_KEY_CODE_TAB
KNST_WINDOW_KEY_CODE_CAPS_LOCK
KNST_WINDOW_KEY_CODE_NUM_LOCK
KNST_WINDOW_KEY_CODE_SCROLL_LOCK
```

#### Modifier Tuşları

```cpp
KNST_WINDOW_KEY_CODE_SHIFT
KNST_WINDOW_KEY_CODE_CONTROL
KNST_WINDOW_KEY_CODE_ALT
KNST_WINDOW_KEY_CODE_SUPER
KNST_WINDOW_KEY_CODE_MENU
```

#### Yön Tuşları

```cpp
KNST_WINDOW_KEY_CODE_LEFT
KNST_WINDOW_KEY_CODE_RIGHT
KNST_WINDOW_KEY_CODE_UP
KNST_WINDOW_KEY_CODE_DOWN
```

#### Navigasyon

```cpp
KNST_WINDOW_KEY_CODE_HOME
KNST_WINDOW_KEY_CODE_END
KNST_WINDOW_KEY_CODE_PAGE_UP
KNST_WINDOW_KEY_CODE_PAGE_DOWN
KNST_WINDOW_KEY_CODE_INSERT
KNST_WINDOW_KEY_CODE_DELETE
```

#### Semboller

```cpp
KNST_WINDOW_KEY_CODE_SEMICOLON
KNST_WINDOW_KEY_CODE_SLASH
KNST_WINDOW_KEY_CODE_GRAVE
KNST_WINDOW_KEY_CODE_LEFT_BRACKET
KNST_WINDOW_KEY_CODE_BACKSLASH
KNST_WINDOW_KEY_CODE_RIGHT_BRACKET
KNST_WINDOW_KEY_CODE_APOSTROPHE
KNST_WINDOW_KEY_CODE_PERIOD
KNST_WINDOW_KEY_CODE_COMMA
KNST_WINDOW_KEY_CODE_MINUS
KNST_WINDOW_KEY_CODE_PLUS
KNST_WINDOW_KEY_CODE_EQUAL
```

#### Medya Tuşları

```cpp
KNST_WINDOW_KEY_CODE_VOLUME_UP
KNST_WINDOW_KEY_CODE_VOLUME_DOWN
KNST_WINDOW_KEY_CODE_VOLUME_MUTE
KNST_WINDOW_KEY_CODE_MEDIA_PLAY
KNST_WINDOW_KEY_CODE_MEDIA_STOP
KNST_WINDOW_KEY_CODE_MEDIA_NEXT
KNST_WINDOW_KEY_CODE_MEDIA_PREV
KNST_WINDOW_KEY_CODE_MEDIA_PAUSE
```

#### Tarayıcı Tuşları

```cpp
KNST_WINDOW_KEY_CODE_BROWSER_HOME
KNST_WINDOW_KEY_CODE_BROWSER_BACK
KNST_WINDOW_KEY_CODE_BROWSER_FORWARD
KNST_WINDOW_KEY_CODE_BROWSER_REFRESH
KNST_WINDOW_KEY_CODE_BROWSER_SEARCH
KNST_WINDOW_KEY_CODE_BROWSER_FAVORITES
```

### 7. İmleç Tipleri

`set_cursor()` fonksiyonuna parametre olarak geçilir.

**Not:** Değerler platforma özeldir. Aynı isim, farklı değer.

| Makro | Windows | X11 | Wayland |
|-------|---------|-----|---------|
| `KNST_WINDOW_CURSOR_ARROW` | `OCR_NORMAL` | `XC_left_ptr` | `"left_ptr"` |
| `KNST_WINDOW_CURSOR_IBEAM` | `OCR_IBEAM` | `XC_xterm` | `"xterm"` |
| `KNST_WINDOW_CURSOR_HAND` | `OCR_HAND` | `XC_hand2` | `"hand2"` |
| `KNST_WINDOW_CURSOR_HRESIZE` | `OCR_SIZEWE` | `XC_sb_h_double_arrow` | `"sb_h_double_arrow"` |
| `KNST_WINDOW_CURSOR_VRESIZE` | `OCR_SIZENS` | `XC_sb_v_double_arrow` | `"sb_v_double_arrow"` |
| `KNST_WINDOW_CURSOR_MOVE` | `OCR_SIZEALL` | `XC_fleur` | `"fleur"` |

`set_cursor_mode()` için ayrıca:

```cpp
KNST_WINDOW_CURSOR_HIDDEN     // Görünmez
KNST_WINDOW_CURSOR_DISABLED   // Kilitli + görünmez
```

### 8. Pencere Özellikleri Makroları

`set_attribute()` / `get_attribute()` fonksiyonlarına:

| Makro | Değer | Anlam |
|-------|-------|-------|
| `KNST_WINDOW_ATTR_DECORATED` | 1 | Sistem çerçevesi |
| `KNST_WINDOW_ATTR_RESIZABLE` | 2 | Boyutlandırılabilir |
| `KNST_WINDOW_ATTR_ALWAYS_ON_TOP` | 3 | Hep üstte |
| `KNST_WINDOW_ATTR_TRANSPARENT` | 4 | Girdi geçirgen |

### 9. Android Özel Makroları

#### Dokunma Aksiyonları

```cpp
KNST_WINDOW_TOUCH_ACTION_PRESS           // 0 — Parmağı koydu
KNST_WINDOW_TOUCH_ACTION_RELEASE         // 1 — Parmağı kaldırdı
KNST_WINDOW_TOUCH_ACTION_MOVE            // 2 — Sürükledi
KNST_WINDOW_TOUCH_ACTION_CANCEL          // 3 — İptal edildi
KNST_WINDOW_TOUCH_ACTION_OUTSIDE         // 4 — Sınır dışında
KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS   // 5 — Ek parmak koydu
KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE // 6 — Ek parmağı kaldırdı
```

#### Ekran Yönleri

```cpp
KNST_WINDOW_ORIENTATION_UNDEFINED   // 0
KNST_WINDOW_ORIENTATION_PORTRAIT    // 1
KNST_WINDOW_ORIENTATION_LANDSCAPE   // 2
KNST_WINDOW_ORIENTATION_SQUARE      // 3
```

#### Diğer Sabitler

```cpp
KNST_WINDOW_DEFAULT  // -10000 — Otomatik değer işareti
```

---

## Platform Notları

### Windows

#### Native Handle Erişimi

```cpp
HWND hwnd = window.get_windows_window_handle();
```

#### Notlar

- **DPI farkındalığı** otomatik. Yüksek DPI ekranlarda metin/ölçüler doğru ölçeklenir.
- **Unicode** pencere başlıkları UTF-16 olarak yerel destekli.
- **Dosya sürükle-bırak** iki yoldan çalışır: klasik `WM_DROPFILES` ve modern `IDropTarget` (COM). İkisi de aynı anda aktif.
- **Kendi çerçeven** (`KNST_DISABLE_TITLE_BAR`) — `WM_NCHITTEST` ile sürükleme, boyutlandırma, kapatma düğmeleri otomatik işlenir. Sen sadece çizersin.
- **Şeffaflık** (`set_opacity`) `WS_EX_LAYERED` + `SetLayeredWindowAttributes` ile uygulanır. 0.0 = tamamen görünmez, 1.0 = opak.

#### Desteklenmeyenler

- `move()` kompozisyon etkin değilse kullanıcı tarafından engellenebilir (`WM_WINDOWPOSCHANGING`).

### Linux X11

#### Native Handle Erişimi

```cpp
xcb_window_t win = window.get_x11_window_handle();
xcb_connection_t* c = KnstWindowSources::get_native_x11_connection_handle();
Display* dpy = KnstWindowSources::get_native_x11_display();
```

#### Notlar

- **Klavye tekrarı** X sunucusu tarafından yönetilir, ama kütüphane `XkbSetDetectableAutoRepeat(True)` ile kendi tekrar mantığını çalıştırır. Sonuç: her platformda aynı tekrar hızı.
- **`_NET_WM_STATE`** protokolü tam desteklenir: `_NET_WM_STATE_FULLSCREEN`, `_MAXIMIZED_HORZ`, `_MAXIMIZED_VERT`, `_HIDDEN`, `_ABOVE`.
- **`_NET_WM_SYNC_REQUEST`** frame senkronizasyonu — yırtılma yok.
- **Xdnd** protokolü tam destekli.
- **Pano** senkron. `request_clipboard()` bir sonraki frame'de içerik hazır olur.
- **Wayland'a kıyasla** pencere yönetimi çok daha esnek: `move()`, `focus()`, `set_position` çalışır.

#### Desteklenmeyenler

- Wayland altında XWayland üzerinden çalışıyorsa bazı özellikler (şeffaflık) sınırlı olabilir.

### Linux Wayland

#### Native Handle Erişimi

```cpp
const wl_surface* surface = window.get_wayland_surface_handle();
wl_display* dpy = KnstWindowSources::wayland_display;
wl_compositor* comp = KnstWindowSources::compositor;
wl_shm* shm = KnstWindowSources::shm;
```

#### Notlar

- **⚠️ Çizim zorunludur.** Wayland'da pencere göstermek için her frame'de bir buffer attach etmen gerekir. Boş bırakırsan pencere göstermez.
- **`move()` ve `focus()`** compositor güvenlik kısıtlamaları nedeniyle **çalışmaz**.
- **`set_cursor_pos_*`** da çalışmaz.
- **Pano asenkron** — `request_clipboard()` bir sonraki frame'de hazır olur.
- **İmleç kilitleme** `zwp_pointer_constraints_v1` ile: `zwp_locked_pointer_v1` (merkezde tut) ve `zwp_confined_pointer_v1` (pencere içinde tut).
- **Dosya sürükle-bırak** `wl_data_device` protokolü üzerinden çalışır.

#### Wayland'da Çizim Örneği (Yazılım Render)

```cpp
static void on_frame(knst_window& w, void*) {
    // ... olayları işle ...

    struct wl_surface* s = const_cast<struct wl_surface*>(w.get_wayland_surface_handle());
    if (!s || !KnstWindowSources::shm) return;

    // Buffer oluştur
    int W = w.m_knst_event.window_width;
    int H = w.m_knst_event.window_height;
    int stride = W * 4;

    int fd = memfd_create("buf", MFD_CLOEXEC);
    ftruncate(fd, stride * H);
    uint32_t* px = (uint32_t*)mmap(nullptr, stride * H, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    // Pikselleri doldur (BGRA / ARGB8888)
    for (int i = 0; i < W * H; ++i) px[i] = 0xFF202060;  // koyu mavi

    struct wl_shm_pool* pool = wl_shm_create_pool(KnstWindowSources::shm, fd, stride * H);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);

    wl_surface_attach(s, buf, 0, 0);
    wl_surface_damage_buffer(s, 0, 0, W, H);
    wl_surface_commit(s);
    wl_display_flush(KnstWindowSources::wayland_display);

    // NOT: buffer'ı ileride destroy etmek gerekir.
    wl_shm_pool_destroy(pool);
    munmap(px, stride * H);
    close(fd);
}
```

### Android

#### Native Handle Erişimi

```cpp
struct android_app* app = KnstWindowSources::get_android_app();
ANativeWindow* win = app->window;
```

#### Notlar

- **`android_main`** giriş noktası — normal `main` **değil**.
- **`app->window`** başlangıçta `NULL`'dur. `APP_CMD_INIT_WINDOW` event'i gelene kadar bekle.
- **Yumuşak klavye** `knst_mobile_keyboard` ile kontrol edilir.
- **Dokunma** çok parmaklı destekli (`pointer_x[10]`).
- **Yaşam döngüsü** event'leri (pause/resume/stop/config-changed) tam destekli.
- **Pano** JNI üzerinden sistem panosuna bağlanır.
- **`move()` / `resize()` / `focus()`** yok sayılır (Android'de yok).

#### Android `Init` Farkı

```cpp
// Windows / Linux
KnstWindowSources::Init();

// Android
void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);
    // ...
}
```

#### Android `app->window` NULL Kontrolü

```cpp
while (app->window == nullptr) {
    int events;
    struct android_poll_source* source = nullptr;
    while (ALooper_pollAll(-1, nullptr, &events, (void**)&source) >= 0) {
        if (source) source->process(app, source);
        if (app->destroyRequested != 0) return;
    }
}
```

#### Yazılım Render Örneği (Android)

```cpp
ANativeWindow_Buffer buf;
if (ANativeWindow_lock(app->window, &buf, nullptr) != 0) return;

uint32_t* px = (uint32_t*)buf.bits;
for (int y = 0; y < buf.height; ++y)
    for (int x = 0; x < buf.width; ++x)
        px[y * buf.stride + x] = 0xFF202060;

ANativeWindow_unlockAndPost(app->window);
```

Apk oluşturma ve telefonda çalıştırmak için hazır olarak 'build-android' scriptleri mevcuttur. Hem linux hem windows için isterseniz onlarıda kullanabilirsiniz. Zaten gerekli ortamı bir kere kurduktan sonrası kolaydır. Android kısmıda aslında pc kısmına benzer ancak 'struct android_app* app' gibi bir uygulama handlesine sahiptir bunuda 'KnstWindowSources::Init(app);' yapısına verdikten sonrasında onunla işiniz olmayacaktır ,  verdiğim örnekte şimdilik herhangi bi vulkan backendi vs olmadığı için yazılım renderi (androide özel) yaptım ilerde kapsamlı bir çizim backendi eklenicektir.

---

## Sık Sorulan Sorular

### 1. Pencere açılıyor ama hiçbir şey görünmüyor

- **Wayland:** Çizim yapmadın. Her frame'de `wl_surface_attach` + `commit` gerekli.
- **Android:** `app->window == nullptr` iken çizim yaptın. Kontrol ekle.
- **Diğer:** `on_frame` callback'i `set_redraw_callback` ile ayarlanmamış olabilir.

### 2. Aynı olay tekrar tekrar işleniyor

`clear_events()` çağrılmamış. Her frame'in **sonunda** çağırmalısın.

### 3. Klavye tekrarları gelmiyor

`KNST_WINDOW_KEY_ACTION_PRESS` olayını arıyorsun, `REPEAT` değil. İkisini de yakala:

```cpp
if (ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS ||
    ev.key_action == KNST_WINDOW_KEY_ACTION_REPEAT) {
    // ...
}
```

### 4. Pano boş geliyor (Wayland)

Asenkron çalışıyor. `request_clipboard()` çağırdıktan sonra bir sonraki frame'de `get_clipboard()` oku. Ya da birkaç frame bekle.

### 5. `move()` işe yaramıyor

- **Wayland:** Compositor izin vermiyor. Beklenen davranış.
- **X11/Windows:** Pencere yöneticisi (WM) kurallarına takılıyor olabilir. Örneğin GNOME pencereyi kullanıcı etkileşimi olmadan taşımayabilir.

### 6. `set_opacity()` Android'de çalışmıyor

Android'de pencere opaklığı **yok sayılır**. Sistem tarafından yönetilir.

### 7. İki pencere açtım, hangisi olay alıyor?

Her pencere **kendi olay kuyruğuna** sahiptir. `non_block_pool_event()` hepsini birden besler, her pencere kendi olaylarını alır.

### 8. `user_data` ve callback arasındaki fark

- `set_user_data(void*)` — Keyfi bir pointer saklar. Callback'e ikinci parametre olarak geçer.
- `set_redraw_callback(fn)` — Frame başına çağrılan fonksiyon. `user_data`'yı da alır.

İkisi birlikte kullanılır:

```cpp
struct MyState { int x; };
MyState s;

window.set_user_data(&s);
window.set_redraw_callback([](knst_window& w, void* data) {
    MyState* st = (MyState*)data;
    st->x++;
});
```

### 9. Kendi başlık çubuğumu çizmek istiyorum

`KNST_DISABLE_TITLE_BAR` makrosunu derlemeden önce tanımla:

```cpp
#define KNST_DISABLE_TITLE_BAR
#include "KernelNucleusT.hpp"
```

Sonra `get_title_bar_height()` ile yüksekliği al ve `on_frame` içinde çiz.

**Windows'ta** sürükleme/kapatma otomatik çalışır. **X11'de** `_NET_WM_MOVERESIZE` kütüphane tarafından yönetilir. **Wayland'de** `xdg_toplevel_move/resize` kullanılır.

### 10. Kendi OpenGL context'imi nasıl bağlarım?

```cpp
#if KNST_USING_PLATFORM_WINDOWS
    HWND hwnd = window.get_windows_window_handle();
    HDC hdc = GetDC(hwnd);
    // wglCreateContext(hdc) ...
#elif KNST_USING_LINUX_PLATFORM_X11
    xcb_window_t win = window.get_x11_window_handle();
    Display* dpy = KnstWindowSources::get_native_x11_display();
    // glXCreateContext(dpy, ...) ...
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    wl_surface* s = const_cast<wl_surface*>(window.get_wayland_surface_handle());
    wl_egl_window* eglWin = wl_egl_window_create(s, W, H);
    // eglCreateWindowSurface(...)
#elif defined(KNST_USING_PLATFORM_ANDROID)
    ANativeWindow* win = KnstWindowSources::get_android_app()->window;
    // eglCreateWindowSurface(..., win, ...)
#endif
```

Kütüphanenin native handle'ını al, kendi GL katmanına ver:
Kütüphanenin eski sürümlerini incelersiniz vulkan ve opengl contentlerini sağlardı ancak, şimdilik desteği kesmeyi düşünüyorum en azından opengl tarafı için , vulkan odaklı çalışmayı düşünüyorum

---

## İpuçları ve Kalıplar

### Kalıp 1 — Oyun Döngüsü

```cpp
struct Game {
    float player_x = 400, player_y = 300;
    bool keys[6] = {};  // W, A, S, D, Space, Shift

    void handle(knst_window& w) {
        for (size_t i = 0; i < w.event_count(); ++i) {
            const auto& ev = w.get_window_event_handle(i);

            if (ev.type == KNST_WINDOW_EVENT_KEYBOARD) {
                bool pressed = (ev.key_action != KNST_WINDOW_KEY_ACTION_RELEASE);
                switch (ev.key_code) {
                    case KNST_WINDOW_KEY_CODE_W: keys[0] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_A: keys[1] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_S: keys[2] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_D: keys[3] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_SPACE:  keys[4] = pressed; break;
                    case KNST_WINDOW_KEY_CODE_ESCAPE:
                        w.should_close(); break;
                }
            }
        }
        w.clear_events();
    }

    void update(float dt) {
        const float speed = 300.f;
        if (keys[0]) player_y -= speed * dt;
        if (keys[2]) player_y += speed * dt;
        if (keys[1]) player_x -= speed * dt;
        if (keys[3]) player_x += speed * dt;
    }

    void render() { /* çiz */ }
};

int main() {
    KnstWindowSources::Init();

    Game game;
    knst_window window(800, 600, "Oyun");

    window.set_redraw_callback([&game](knst_window& w, void*) {
        game.handle(w);
        game.update(1.0f / 60.0f);
        game.render();
    });

    window.creation_and_show();

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
    }

    window.destroy();
    KnstWindowSources::CleanUp();
}
```

### Kalıp 2 — UI Editor

```cpp
// Fare ile sürükle-bırak
struct Editor {
    bool dragging = false;
    int drag_offset_x = 0, drag_offset_y = 0;
    int box_x = 100, box_y = 100, box_w = 200, box_h = 100;

    void on_event(const knst_window_event& ev) {
        if (ev.type == KNST_WINDOW_EVENT_MOUSE) {
            if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS &&
                ev.mouse_button == KNST_WINDOW_MOUSE_BUTTON_LEFT) {
                // Kutunun içinde mi?
                if (ev.mouse_x >= box_x && ev.mouse_x < box_x + box_w &&
                    ev.mouse_y >= box_y && ev.mouse_y < box_y + box_h) {
                    dragging = true;
                    drag_offset_x = ev.mouse_x - box_x;
                    drag_offset_y = ev.mouse_y - box_y;
                }
            }
            else if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_RELEASE) {
                dragging = false;
            }
        }
        else if (ev.type == KNST_WINDOW_EVENT_MOTION) {
            if (dragging) {
                box_x = ev.mouse_x - drag_offset_x;
                box_y = ev.mouse_y - drag_offset_y;
            }
        }
    }
};
```

### Kalıp 3 — Çoklu Pencere + Farklı Callback

```cpp
knst_window editor(800, 600, "Editor");
knst_window preview(400, 300, "Preview");

editor.set_redraw_callback([](knst_window& w, void*) { /* editor çiz */ });
preview.set_redraw_callback([](knst_window& w, void*) { /* preview çiz */ });

editor.creation_and_show();
preview.creation_and_show();

while (!editor.is_should_close() || !preview.is_should_close()) {
    knst_window_event_system::non_block_pool_event();

    if (!editor.is_should_close())  editor.call_redraw_callback();
    if (!preview.is_should_close()) preview.call_redraw_callback();
}
```

### Kalıp 4 — Kısayol Kontrolü

```cpp
if (ev.type == KNST_WINDOW_EVENT_KEYBOARD &&
    ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS) {

    const bool ctrl  = ev.mods & KNST_WINDOW_MOD_CONTROL;
    const bool shift = ev.mods & KNST_WINDOW_MOD_SHIFT;

    if (ctrl && !shift && ev.key_code == KNST_WINDOW_KEY_CODE_S) {
        save();
    }
    else if (ctrl && shift && ev.key_code == KNST_WINDOW_KEY_CODE_S) {
        save_as();
    }
    else if (ctrl && ev.key_code == KNST_WINDOW_KEY_CODE_Z) {
        undo();
    }
}
```
Dökümanı  herkesin anlayacağı gibi yazmaya ve olabildiğince kullanım klavuzu gibi yapmaya çalıştım , bazı yerlerde kalıp örnekler verdim , işinize yarar umarım , ancak eğer hala sorularınız veya herhangi bi şekilde iletişime geçmek isterseniz github profilimde bilgilerim mevcuttur.

Umarım ki bu kütüphane herkesin işine yarar , iyi günler :)