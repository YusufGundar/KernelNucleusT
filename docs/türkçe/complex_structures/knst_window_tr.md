# knst_window — Tam Kapsamlı Dokümantasyon

> Çapraz platform pencere kütüphanesi.
> **Windows** · **Linux (X11 + Wayland + Dual)** · **Android**


---

## İçindekiler

- [1. Genel Bakış](#1-genel-bakış)
- [2. Mimari ve İç İşleyiş](#2-mimari-ve-i̇ç-i̇şleyiş)
- [3. Gereksinimler ve Derleme](#3-gereksinimler-ve-derleme)
- [4. Hızlı Başlangıç](#4-hızlı-başlangıç)
- [5. Sınıf Referansı](#5-sınıf-referansı)
- [6. Fonksiyon Referansı](#6-fonksiyon-referansı)
- [7. Tüm Fonksiyonlar — Tek Satırda (`//` yorumlu)](#7-tüm-fonksiyonlar--tek-satırda--yorumlu)
- [8. Makro Referansı](#8-makro-referansı)
- [9. Platform Notları](#9-platform-notları)
- [10. İç Tasarım, Taktikler ve Arka Plan Bilgisi](#10-i̇ç-tasarım-taktikler-ve-arka-plan-bilgisi)
- [11. Yapılandırma (`knst_settings.hpp`)](#11-yapılandırma-knst_settingshpp)
- [12. Kalıplar ve Örnekler](#12-kalıplar-ve-örnekler)
- [13. SSS ve Tuzaklar](#13-sss-ve-tuzaklar)
- [14. Lisans](#14-lisans)

---

## 1. Genel Bakış

`knst_window`, **tek bir `#include "KernelNucleusT.hpp"`** ile dört platformda (Windows, Linux X11, Linux Wayland, Linux Dual) ve Android'de pencere açmanı sağlar. Kütüphane, Linux (Wayland) harici tamamen **header-only** olarak tasarlandı — sadece `include/` klasörünü kopyala, derle, çalıştır. Wayland'da protokol `.c`/`.h` dosyaları `wayland-scanner` (veya `wayland-protocols`) üzerinden CMake tarafından derlenir. Protocol dosyaları projenin içinde otomatik gelir , isterseniz silip cmake tarafının oluşturmasınıda sağlayabilirsiniz.

### Temel Prensipler

- **Olay senin, çizim senin.** Kütüphane olayları toplar, pencereyi yönetir, imleci çevirir, panoyu tutar. Çizimden sorumlu değildir.
- **`noexcept` + `bool`.** Exception fırlatmaz. Hata kontrolü tamamen geliştiricinin elinde. `set_clipboard()` başarısız olsa bile çökmez.
- **Zero-cost abstraction.** Modern C++ (`if constexpr`, `force_inline`, ring buffer, `atomic`, `unique_ptr`) kullanıldı. Kullanmadığın özelliğin binary boyutuna maliyeti minimum.
- **Header-only (Wayland hariç).** Ekstra `.c`/`.cpp` derlemen gerekmez. Windows ve X11'de `include/` klasörünü direkt kullanabilirsin.
- **Sıfır VM/GC.** Her şey derleme zamanında bilinir, allocation yalnızca kullanıcı isteğiyle yapılır.

### Kütüphanenin İskeleti

```
KnstWindowSources::Init()          ← Platform kaynaklarını hazırla
        ↓
window.creation_and_show()         ← Pencereyi oluştur ve göster
        ↓
    ┌─────────────────────────────────────────┐
    │  while (!window.is_should_close())      │   ← Ana döngü
    │    non_block_pool_event()               │
    │    call_redraw_callback()               │
    └─────────────────────────────────────────┘
        ↓
window.destroy()
KnstWindowSources::CleanUp()       ← Kaynakları temizle
```

### Event Flow Özeti

```
[OS] → [Event Manager] → [Ring Buffer] → [Sen oku] → [İşle]
                            ↑
                    (dispatch_event)
```

Her pencere kendi **256 slotluk ring buffer**'a sahiptir. Coalescing sayesinde hızlı `MOTION` / `RESIZE` / `EXPOSE` event'leri birleştirilir — buffer taşmaz. Raw mouse deltaları **toplanır** (overwrite değil), FPS kamerada hareket kaybı olmaz.

---

## 2. Mimari ve İç İşleyiş

`knst_window`, iki katmanlı bir mimariye sahiptir:

### 2.1 Katmanlar

```
┌────────────────────────────────────────────────────────────┐
│  User Code (game loop / event handler / renderer)          │
└────────────────────────────────────────────────────────────┘
                            ↕
┌────────────────────────────────────────────────────────────┐
│  knst_window                                               │
│  ├── Event ring buffer (256 slot)                          │
│  ├── Held-key table (8 slot)                               │
│  ├── CSD state (callback, hit-test)                        │
│  ├── Surface client registry                               │
│  └── Per-platform state (HWND / xcb_window_t / wl_surface) │
└────────────────────────────────────────────────────────────┘
                            ↕
┌────────────────────────────────────────────────────────────┐
│  knst_window_event_system (global event pump)              │
│  └── Registers all windows → OS events → dispatch          │
└────────────────────────────────────────────────────────────┘
                            ↕
┌────────────────────────────────────────────────────────────┐
│  KnstWindowSources (platform lifecycle, handles, display)  │
│  ├── Init / CleanUp                                        │
│  ├── Active backend (x11 / wayland / windows / android)    │
│  └── Native handles (HINSTANCE, xcb_connection_t, wl_*)    │
└────────────────────────────────────────────────────────────┘
                            ↕
┌────────────────────────────────────────────────────────────┐
│  OS APIs (Win32, XCB, Wayland, Android NDK)                │
└────────────────────────────────────────────────────────────┘
```

### 2.2 Veri Akışı

1. **OS event gelir** → `non_block_pool_event()` veya `block_pool_event()` çağrısı
2. **Event manager** OS event'ini `knst_window_event` yapısına dönüştürür
3. **Ring buffer'a push** — coalescing uygulanır (MOTION/RESIZE/EXPOSE birleşir, raw delta toplanır)
4. **Kullanıcı** `event_count()` / `get_window_event_handle(i)` ile okur
5. **`clear_events()`** ile buffer temizlenir

### 2.3 Backend Seçimi (Dual Mode)

```
KnstWindowSources::Init()
        ↓
KNST_WINDOW_FORCE_X11=1 var mı? → evetse X11
KNST_WINDOW_FORCE_WAYLAND=1 var mı? → evetse Wayland
        ↓
WAYLAND_DISPLAY / XDG_SESSION_TYPE=wayland set mi?
        ↓
wl_display_connect() probe → wl_compositor + xdg_wm_base var mı?
        ↓
X11 probe: DISPLAY set mi? XOpenDisplay çalışıyor mu?
        ↓
Hiçbiri → knst_backend::none (Init başarısız)
```

### 2.4 Sunulan Özellik Katmanları

| Katman | Amaç | Örnek |
|--------|------|-------|
| **Core** | Pencere + event + input | `knst_window`, `knst_window_event_system` |
| **Display** | Monitör yönetimi | `knst_display`, `knst_monitor` |
| **Surface** | GPU context entegrasyonu | `knst_surface_client`, `knst_window_opengl_content` |
| **Platform** | Kaynak yaşam döngüsü | `KnstWindowSources` |
| **Mobile** | Android-specific | `knst_mobile_keyboard`, `knst_android_jni` |

---

## 3. Gereksinimler ve Derleme

### 3.1 Sistem Gereksinimleri

| Platform | Minimum | Önerilen |
|----------|---------|----------|
| **Windows** | Windows 7+ | Windows 10/11 |
| **Linux X11** | X.Org 1.17+ | X.Org 21+ |
| **Linux Wayland** | wayland-client 1.15+ | 1.22+ |
| **Android** | API 24 (Android 7.0) | API 30+ |
| **C++** | C++20 | C++20 |
| **CMake** | 3.20 | 3.25+ |

### 3.2 Header-Only Notu

Windows ve X11 için **tamamen header-only**. `include/` klasörünü projene kopyala, `#include "KernelNucleusT.hpp"` yaz, derle. Wayland için `include/platform/linux/wayland/protocol_files/` altındaki `.c` ve `.h` dosyaları derlenmeli — CMake bunu otomatik yapar.

### 3.3 CMake Entegrasyonu

```cmake
cmake_minimum_required(VERSION 3.20)
project(BenimUygulamam CXX)

# Linux için (Windows'ta yok sayılır)
set(KNST_LINUX_PLATFORM "X11" CACHE STRING "" FORCE)
# set(KNST_LINUX_PLATFORM "Wayland" CACHE STRING "" FORCE)
# set(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND ON CACHE BOOL "" FORCE)

# Backend seçimi (opsiyonel)
set(KNST_WINDOW_USE_OPENGL ON  CACHE BOOL "" FORCE)
# set(KNST_WINDOW_USE_VULKAN ON CACHE BOOL "" FORCE)

add_subdirectory(KernelNucleusT)

add_executable(benim_uygulamam main.cpp)
target_link_libraries(benim_uygulamam PRIVATE KernelNucleusT::KernelNucleusT)
```

Bu 4 satır:
- `include/` yolunu ekler
- Platform makrolarını tanımlar (`KNST_USING_PLATFORM_WINDOWS`, `KNST_USING_LINUX_PLATFORM_X11`, vs.)
- X11/Wayland/GDI kütüphanelerini link eder
- Backend seçimini uygular

### 3.4 Tüm CMake Seçenekleri

| Seçenek | Varsayılan | Açıklama |
|---------|-----------|----------|
| `KNST_LINUX_PLATFORM` | `X11` | Linux: `X11` veya `Wayland` |
| `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` | `OFF` | Linux: her iki backend aynı binary'ye (runtime-selected) |
| `KNST_BUILD_EXAMPLES` | `OFF` | `examples/` klasöründeki örnekleri derle |
| `KNST_APP_SOURCE` | Boş | Kendi `.cpp` dosyanı `knst_app` olarak derle |
| `KNST_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UBSan |
| `KNST_WARNINGS_AS_ERRORS` | `OFF` | Uyarıları hata say |
| `KNST_STATIC_MSVC_RUNTIME` | `ON` | MSVC: statik runtime (`/MT`) |
| `KNST_WINDOW_USE_OPENGL` | `OFF` | OpenGL 3.3 Core backend |
| `KNST_WINDOW_USE_VULKAN` | `OFF` | Vulkan backend |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |

### 3.5 Otomatik Tanımlanan Makrolar

| Durum | Tanımlanan makrolar |
|-------|---------------------|
| Linux + X11 | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11` |
| Linux + Wayland | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_WAYLAND` |
| Linux + Dual | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11`, `KNST_USING_LINUX_PLATFORM_WAYLAND`, `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` |
| Windows | `KNST_USING_PLATFORM_WINDOWS`, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `UNICODE`, `_UNICODE` |
| Android | `KNST_USING_PLATFORM_ANDROID`, `KNST_USING_PLATFORM_LINUX` |
| OpenGL açık | `KNST_WINDOW_USE_OPENGL` |
| Vulkan açık | `KNST_WINDOW_USE_VULKAN` |

### 3.6 Manuel Derleme

**Zorunlu makrolar:**
- Windows: `-DKNST_USING_PLATFORM_WINDOWS -DNOMINMAX -DUNICODE -D_UNICODE`
- X11: `-DKNST_USING_PLATFORM_LINUX -DKNST_USING_LINUX_PLATFORM_X11`
- Wayland: `-DKNST_USING_PLATFORM_LINUX -DKNST_USING_LINUX_PLATFORM_WAYLAND`
- Android: `-DKNST_USING_PLATFORM_ANDROID -DKNST_USING_PLATFORM_LINUX`

**Zorunlu kütüphaneler:**
- Windows: `gdi32 user32 shell32 shcore ole32 oleaut32 uuid hid bthprops setupapi iphlpapi ws2_32`
- X11: `xcb xcb-randr xcb-keysyms xcb-icccm x11-xcb x11 xext xfixes xi xrandr xcursor xcb-xinput xcb-sync`
- Wayland: `wayland-client wayland-cursor wayland-egl xkbcommon`

### 3.7 Linux Paket Kurulumları

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install build-essential cmake ninja-build pkg-config
sudo apt install libx11-dev libx11-xcb-dev libxcb1-dev libxcb-randr0-dev \
  libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-util-dev libxcb-sync-dev \
  libxext-dev libxfixes-dev libxi-dev libxrandr-dev libxcursor-dev libxcb-xinput-dev
sudo apt install libwayland-dev libwayland-bin libxkbcommon-dev
sudo apt install libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev
sudo apt install libvulkan-dev vulkan-tools
```

**Fedora**
```bash
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config
sudo dnf install libX11-devel libxcb-devel xcb-util-devel xcb-util-keysyms-devel \
  xcb-util-wm-devel libXext-devel libXfixes-devel libXi-devel libXrandr-devel libXcursor-devel
sudo dnf install wayland-devel libxkbcommon-devel
sudo dnf install mesa-libGL-devel mesa-libEGL-devel mesa-libGLES-devel
sudo dnf install vulkan-loader-devel vulkan-headers vulkan-tools
```

**Arch**
```bash
sudo pacman -S base-devel cmake ninja pkgconf
sudo pacman -S libx11 libxcb xcb-util xcb-util-keysyms xcb-util-wm \
  libxext libxfixes libxi libxrandr libxcursor
sudo pacman -S wayland libxkbcommon
sudo pacman -S mesa vulkan-headers vulkan-icd-loader vulkan-tools
```

### 3.8 Android

**NDK r26+** önerilir. Android Studio → SDK Manager → SDK Tools → "NDK (Side by side)".

**Hazır script'ler** (`examples/complex_structures/knst_window/android/`):
- `build-android-opengl.sh` / `.bat`
- `build-android-vulkan.sh` / `.bat`

Script pipeline:
1. **Environment** — SDK/NDK/build-tools tespit
2. **ADB server** — başlat veya yeniden kullan
3. **Device discovery** — USB → mDNS → cache → Wi-Fi → bekle
4. **CMake configure** — cache ile (değişiklik yoksa atla)
5. **Native build** — Ninja parallel
6. **APK packaging** — aapt2 + javac + d8 + zipalign + apksigner
7. **Deploy** — uninstall → install → launch
8. **Live logcat** — `Ctrl+C`'ye kadar

**Vulkan için `minSdkVersion >= 24`** zorunlu — script daha düşükse erken çıkar.

### 3.9 GLAD (OpenGL Yükleyici)

Kütüphane **GLAD 3.3 Core** ile birlikte gelir. `include/platform/knst_window/opengl_content/glad_3_3/` altında hazır. `KNST_WINDOW_USE_OPENGL=ON` verdiğinde CMake otomatik derler.

Farklı bir sürüm istiyorsanız: https://glad.dav1d.de/ adresinden yeni konfigürasyon al, `include/` ve `src/` klasörlerini aynı dizinle değiştir, `rm -rf build` + yeniden yapılandırabilirsiniz. İsterseniz `KernelNucleusT.hpp` dosyasında kendi indirdiğiniz glad'ı da kütüphanenin glad'ı yerine include edebilirsiniz

---

## 4. Hızlı Başlangıç

### 4.1 En Basit Örnek

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

static void on_frame(knst_window& w, void*) {
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

    knst_window window(800, 600, u"Selam Dünya");
    window.set_redraw_callback(on_frame);
    window.creation_and_show();

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
    }

    window.destroy();
    KnstWindowSources::CleanUp();
}
```

> **Önemli not:** `call_redraw_callback()` kullanımı **zorunlu değildir** — ama Windows'ta `WM_WINDOWPOSCHANGED` sırasında yeniden çizim kilitlenmesini çözer. Çapraz platformluğu tam sağlamak için bu kalıbı kullanmanızı tavsiye ediyorum.

### 4.2 Android Örneği

```cpp
#include "KernelNucleusT.hpp"

static void on_frame(knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);
        if (ev.type == KNST_WINDOW_EVENT_MOBILE_BACK) {
            w.should_close();
        }
    }
    w.clear_events();

    android_app* app = KnstWindowSources::get_android_app();
    if (app && app->window) {
        ANativeWindow_Buffer buf;
        if (ANativeWindow_lock(app->window, &buf, nullptr) == 0) {
            uint32_t* px = (uint32_t*)buf.bits;
            for (int y = 0; y < buf.height; ++y)
                for (int x = 0; x < buf.width; ++x)
                    px[y * buf.stride + x] = 0xFF202060;
            ANativeWindow_unlockAndPost(app->window);
        }
    }
}

void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);

    knst_window window;
    window.set_redraw_callback(on_frame);
    window.creation_and_show();

    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();
    }

    window.destroy();
    KnstWindowSources::CleanUp();
}
```

**Android farkları:**
- Giriş noktası `android_main(struct android_app*)`, `main()` değil
- `KnstWindowSources::Init(app)` parametre alır
- `std::cout` otomatik `logcat`'e gider (tag: `KNST`)
- `app->window` başlangıçta `NULL` — `Init()` bekler

### 4.3 OpenGL Kurulum Sırası

OpenGL kullanacaksan sıra **kritik**:

```cpp
// 1. Platform layer
KnstWindowSources::Init();

// 2. Global GL state (EGL display + config)
knst_opengl_global::Options opts;
opts.gl_major = 3;
opts.gl_minor = 3;
opts.core_profile = true;
knst_opengl_global::Init(opts);

// 3. Window oluştur ama gösterme
knst_window window(800, 600, u"GL");
window.set_redraw_callback(on_frame);
window.creation();

// 4. Per-window GL context
knst_window_opengl_content gl;
gl.Init(&window);

// 5. MakeCurrent + GLAD load (sıra önemli)
gl.MakeCurrent();
gladLoadGLLoader((GLADloadproc)knst_window_opengl_content::LoadGLProc);

// 6. GPU bilgisini oku
std::cout << "GPU: " << gl.GetRendererString() << "\n";
std::cout << "GL : " << gl.GetVersionString() << "\n";

// 7. İlk frame (siyah ekran önlemi)
gl.MakeCurrent();
render_frame();
gl.SwapBuffers();

// 8. Şimdi göster
window.show();

// 9. Ana döngü
while (!window.is_should_close()) {
    knst_window_event_system::non_block_pool_event();
    window.call_redraw_callback();
}

// 10. Ters sırada temizlik
gl.MakeCurrent();
// GL kaynaklarını sil
gl.Shutdown();
knst_opengl_global::CleanUp();
window.destroy();
KnstWindowSources::CleanUp();
```

> **Sıra önemli:** `gl.Shutdown()` → `window.destroy()` → `KnstWindowSources::CleanUp()`. Ters sıra **crash** veya **UB** demektir. Dikkatli olmanızda fayda var.

---

## 5. Sınıf Referansı

### 5.1 `knst_window`

Ana pencere sınıfı. **Kopyalanamaz** ve **taşınamaz** (non-copyable, non-movable).

```cpp
knst_window(const knst_window&) = delete;
knst_window& operator=(const knst_window&) = delete;
knst_window(knst_window&&) = delete;
knst_window& operator=(knst_window&&) = delete;
```

**Kullanım:** Stack'te ya da `std::make_unique<knst_window>(...)` ile heap'te.

#### Constructor

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

| Parametre | Açıklama |
|-----------|----------|
| `width`, `height` | Pencere boyutu (piksel). Android'de yok sayılır. |
| `title` | UTF-16 başlık. |
| `root_x`, `root_y` | Monitöre **göre** göreceli konum. `KNST_WINDOW_DEFAULT` → ortala. |
| `monitor` | Hangi monitörde açılacak. Boş → varsayılan davranış. |

**Not:** `monitor.physical_width > 0 && monitor.physical_height > 0` ise constructor otomatik ortalama yapar. Aksi halde `root_x/root_y` olduğu gibi kullanılır.

#### Non-movable / Non-copyable — Neden?

- **Platform kaynakları:** HWND, `xcb_window_t`, `wl_surface`, XIC, HGLRC, `IDropTarget` — hepsi `this`'e bağlı.
- **Listener user-data:** Wayland `xdg_surface`, `xdg_toplevel`, `wl_callback` — hepsi `this`'i gösteriyor. Move olsa pointer'lar dangling kalır → UAF.
- **Callback lambdaları:** CSD hit-test, redraw callback — hepsi `this`'i capture etmiş durumda.
- **Global registry:** `knst_window_event_system::windows`, `active_window`, `keyboard_focus_window`, `drag_target_window` — hepsi raw pointer.

**Doğru kullanım:**
```cpp
knst_window w(800, 600, u"Title");   // stack
auto w = std::make_unique<knst_window>(...);    // heap
```

**Yanlış kullanım:**
```cpp
knst_window make_window() { return knst_window(800, 600, u"X"); }  // derlenmez — move yok
```

---

### 5.2 `knst_window_event`

Tek bir olayı temsil eder. **Union kullanır** — `type` alanına göre hangi üyeyi okuyacağını bilmen gerekir.

```cpp
struct knst_window_event {
    uint32_t type; // KNST_WINDOW_EVENT_* — hangi alanlar geçerli?
    uint32_t timestamp_ms; // Event oluşturulduğu an (ms)

    int32_t mods; // Modifier bit maskesi
    int32_t mouse_x; // Pencere içi koordinat
    int32_t mouse_y;
    int32_t mouse_root_x; // Ekran koordinatı
    int32_t mouse_root_y;
    int32_t window_width; // Bu event sırasında pencere genişliği
    int32_t window_height;
    int32_t window_root_x; // Pencere konumu (ekran)
    int32_t window_root_y;

    bool is_focused;
    bool mouse_on_window;
    bool is_full_screen;
    bool is_maximized;
    bool is_minimized;

    // ⚠️ UNION: Sadece BİRİ geçerli (type'a göre)
    union {
        struct { int32_t mouse_button, mouse_action, mouse_scroll_delta; };
        struct { int32_t key_code, scancode, key_action; };
    };

    // Dosya drop
    std::shared_ptr<knst_vector<knst_c16string>> drop_files;
    uint32_t drop_count;

    // Metin / IME
    knst_c16string text_input;
    knst_c16string text_preedit;
    int32_t text_preedit_cursor_begin = -1;
    int32_t text_preedit_cursor_end   = -1;
    int32_t text_delete_before = 0;
    int32_t text_delete_after  = 0;

    // Raw fare
    int32_t raw_delta_x = 0;
    int32_t raw_delta_y = 0;
    bool is_raw = false;

    // Touch (10 noktaya kadar)
    int pointer_count;
    float pointer_x[10];
    float pointer_y[10];
    int pointer_id[10];
    int touch_action;

#if defined(KNST_USING_PLATFORM_ANDROID)
    int content_left, content_top, content_right, content_bottom;
    int orientation;
    char language[4];
    char country[4];
    bool is_night_mode;
    bool is_low_memory;
    float density;
    int screen_width_dp;
    int screen_height_dp;
    void* saved_state;
    size_t saved_state_size;
#endif

    void begin(uint32_t ev_type) noexcept;
};
```

#### ⚠️ Union Uyarısı

`mouse_button` ve `key_code` **aynı bellek alanını** paylaşır. `type` alanına göre **doğru olanı** oku:

```cpp
if (ev.type == KNST_WINDOW_EVENT_MOUSE) {
    std::cout << ev.mouse_button;   // ✓ doğru
    // std::cout << ev.key_code;    // ✗ yanlış — mouse_button'ın byte'ları
}
```

#### `begin()` Davranışı

`begin()` **her şeyi sıfırlar** ve `timestamp_ms`'i ayarlar. `text_input`, `text_preedit` gibi string'ler boşaltılır. **Sadece** `density`, `screen_width_dp`, `screen_height_dp` — Android'de tutarlılık için — kasıtlı olarak korunur.

---

### 5.3 `knst_monitor`

Bir monitörü temsil eder. `knst_display` tarafından doldurulur.

```cpp
class knst_monitor {
public:
    int root_x = 0; // Sanal masaüstündeki X konumu
    int root_y = 0;
    int width = 0; // Piksel genişlik
    int height = 0;
    int physical_width = 0; // mm (EDID'den)
    int physical_height = 0;
    float refresh_rate = 60.0f;  // Hz (X11/Windows'ta sürücüden)
    float dpi_scale = 96.0f;  // 96 = 1.0x, 144 = 1.5x
    bool is_primary = false;  // Ana monitör mü?
    knst_c16string name;  // Monitör adı ("DELL U2720Q" gibi)
};
```

#### DPI Nasıl Hesaplanır?

| Platform | Kaynak |
|----------|--------|
| **Windows** | `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` |
| **X11** | `Xft.dpi` → `GDK_SCALE` → fiziksel hesap |
| **Wayland** | `wl_output.scale` veya DPI'dan tahmin |
| **Android** | `AConfiguration_getDensity()` / 160 |

**X11 öncelik sırası:**
1. `Xft.dpi` X resource (kullanıcının ayarı) — Qt/GTK bunu yazar
2. `GDK_SCALE` env var (2 → 192 DPI)
3. Fiziksel hesap: `sqrt(w²+h²) / (sqrt(pw²+ph²) / 25.4)`

---

### 5.4 `knst_display`

Tüm monitörleri yöneten statik sınıf.

```cpp
class knst_display {
public:
    static void refresh_screens() noexcept;
    static const knst_vector<std::unique_ptr<knst_monitor>>& get_monitor_list() noexcept;
    static const knst_monitor* get_primary_monitor() noexcept;
    static size_t get_monitor_count() noexcept;
    static const knst_monitor* get_monitor(size_t index) noexcept;
};
```

**Not:** `refresh_screens()` pahalıdır (X11'de RandR sorgusu, Windows'ta `EnumDisplayDevices`). Açılışta **bir kez** çağır. Wayland'da compositor monitör değişikliğini otomatik bildirir; sadece ilk okumada manuel çağır.

---

### 5.5 `KnstWindowSources`

Platform kaynaklarını yönetir. Genelde **doğrudan kullanmazsın**, sadece `Init()` / `CleanUp()` / `get_current_time_ms()`.

```cpp
class KnstWindowSources {
public:
    static void Init() noexcept; // Windows / Linux
    static void Init(android_app* app); // Android
    static void CleanUp() noexcept;

    static uint32_t get_current_time_ms() noexcept;

    static knst_backend active_backend() noexcept;
    static bool is_x11_active() noexcept;
    static bool is_wayland_active() noexcept;
    static bool is_windows_active() noexcept;

#if KNST_USING_PLATFORM_WINDOWS
    static HINSTANCE& get_windows_native_instance_handle() noexcept;
#elif KNST_USING_LINUX_PLATFORM_X11
    static xcb_window_t& get_native_x11_root_handle() noexcept;
    static xcb_connection_t* get_native_x11_connection_handle() noexcept;
    static Display* get_native_x11_display() noexcept;
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    static wl_display* wayland_display;
#elif defined(KNST_USING_PLATFORM_ANDROID)
    static android_app* get_android_app() noexcept;
#endif
};
```

#### `knst_backend` Enum

```cpp
enum class knst_backend : uint8_t {
    none    = 0,
    x11     = 1,
    wayland = 2,
    android = 3,
    windows = 4
};
```

---

### 5.6 `knst_window_event_system`

Global olay yöneticisi. **Statiktir** — tüm pencereleri tek yerden yönetir.

```cpp
struct knst_window_event_system {
    static void block_pool_event() noexcept; // Olay gelene kadar bloklar
    static void non_block_pool_event() noexcept; // Hemen döner
    static size_t get_window_count() noexcept; // Kayıtlı pencere sayısı
};
```

| Fonksiyon | Davranış | Kullanım |
|-----------|----------|----------|
| `block_pool_event()` | Olay gelene kadar bekler | Basit CLI araçları, GUI uygulaması |
| `non_block_pool_event()` | Hemen döner | Oyun döngüleri, animasyonlar |

**Wayland uyarısı:** Wayland'da çizim zorunludur. `block_pool_event()` kullanırsan pencere asla güncellenmez çünkü compositor `frame` eventini bekler. **Her zaman `non_block_pool_event()`** kullan.

---

### 5.7 `knst_mobile_keyboard` (Android)

Android yumuşak klavyesini kontrol eder.

```cpp
class knst_mobile_keyboard {
public:
    static bool show();
    static bool hide();
    static void toggle();
    static bool is_visible();
};
```

**PC'de no-op:** Aynı API PC'de tanımlıdır ve `false` döner. `#ifdef` yazmana gerek yok.

**Notlar:**
- `show()` 3 kez retry yapar (200ms aralıklarla) — Android focus settle için gerekli.
- `toggle()` 300ms debounce içerir — hızlı iki çağrı yok sayılır.
- `is_visible()` thread-safe (mutex korumalı).

---

### 5.8 `knst_window_capabilities`

Runtime özellik sorgusu. `knst_window::capabilities()` statik fonksiyonundan döner.

```cpp
struct knst_window_capabilities {
    bool move; // Pencere taşınabilir mi
    bool resize_by_user; // Kullanıcı boyutlandırabilir mi
    bool force_focus; // Kod ile odaklanabilir mi
    bool cursor_position; // İmleç konumu değiştirilebilir mi
    bool opacity; // Pencere saydamlığı
    bool always_on_top; // Z-sırası kontrolü
    bool set_icon; // Pencere ikonu
    bool input_transparent; // Click-through
    bool drag_drop; // Dosya sürükle-bırak
    bool text_input; // Karakter girişi
    bool ime; // IME (CJK, dead keys)
    bool raw_mouse; // Göreceli fare hareketi
    bool clipboard; // Sistem panosu
    bool primary_selection; // X11 orta-tık yapıştır
    bool fractional_scale; // 1.25x, 1.5x gibi
    bool vsync_control; // SwapInterval
    bool multiple_windows; // Çoklu pencere desteği
    bool touch_screen; // Runtime tespit edilmiş touch
    bool idle_inhibit; // Ekran uyku engeli
};
```

```cpp
auto caps = knst_window::capabilities();
if (caps.move) window.move(100, 100);
if (caps.touch_screen) window.enable_touch_screen(true);
```

---

### 5.9 `knst_surface_client`

OpenGL/Vulkan context'leri için arayüz. **Kullanıcı implemente etmez** — kütüphane içindeki `knst_window_opengl_content` / `knst_window_vulkan_content` sınıfları bunu implement eder.

```cpp
class knst_surface_client {
public:
    virtual void on_surface_release() noexcept = 0;
    virtual bool on_surface_recreate() noexcept = 0;
    virtual void on_window_destroyed() noexcept {}
};
```

**Ne zaman çağrılır:**
- Android `APP_CMD_TERM_WINDOW` → `on_surface_release`
- Android `APP_CMD_INIT_WINDOW` → `on_surface_recreate`
- Windows/X11 `destroy()` → `on_window_destroyed`
- Wayland surface destruction → `on_surface_release`

Kendi renderer'ını yazarsan bu arayüzü implement et, `window.register_surface_client(this)` ile kaydettir.

---

### 5.10 `knst_window_opengl_content`

OpenGL context manager. Her pencere başına bir tane. `knst_surface_client` implement eder.

```cpp
class knst_window_opengl_content : public knst_surface_client {
public:
    bool Init(knst_window* window, bool reuse_old = false) noexcept;
    bool InitShared(knst_window* window, const knst_window_opengl_content& share) noexcept;
    void Shutdown() noexcept;

    void MakeCurrent() noexcept;
    void SwapBuffers() noexcept;

    void SetSwapInterval(int interval) noexcept;
    int GetSwapInterval() const noexcept;

    void OnResize(int new_width, int new_height) noexcept;
    void OnScaleChanged() noexcept;

    bool RecreateSurface() noexcept;
    void ReleaseSurface() noexcept;

    bool IsValid() const noexcept;
    bool HasSurface() const noexcept;
    knst_window* GetWindow() const noexcept;

    int GetWidth() const noexcept;
    int GetHeight() const noexcept;
    int GetFramebufferWidth() const noexcept;
    int GetFramebufferHeight() const noexcept;

    const char* GetVendorString() const noexcept;
    const char* GetRendererString() const noexcept;
    const char* GetVersionString() const noexcept;
    const char* GetGLSLVersionString() const noexcept;
    int GetActualMajorVersion() const noexcept;
    int GetActualMinorVersion() const noexcept;
    knst_opengl_global::ClientAPI GetClientAPI() const noexcept;

    static void* LoadGLProc(const char* name) noexcept;
};
```

---

### 5.11 `knst_opengl_global`

Paylaşılan GL state. Process başına bir tane.

```cpp
class knst_opengl_global {
public:
    enum class ClientAPI : uint8_t { OpenGL, OpenGLES };
    enum class Robustness : uint8_t { Off, NoReset, Reset };
    enum class ReleaseBehavior : uint8_t { Flush, NoFlush };

    struct Options {
        ClientAPI client_api = ClientAPI::OpenGL; // Android default: OpenGLES
        int gl_major = 3;
        int gl_minor = 3;
        bool core_profile = true;
        bool forward_compat = false;
        bool debug_context = false;
        bool robust_access = false;
        Robustness robustness = Robustness::Off;
        ReleaseBehavior release_behavior = ReleaseBehavior::Flush;
        int red_bits = 8, green_bits = 8, blue_bits = 8, alpha_bits = 8;
        int depth_bits = 24, stencil_bits = 8;
        int accum_red_bits = 0, accum_green_bits = 0;
        int accum_blue_bits = 0, accum_alpha_bits = 0;
        int aux_buffers = 0;
        bool double_buffer = true;
        bool stereo = false;
        bool srgb = false;
        bool msaa = false;
        int  msaa_samples = 4;
        bool vsync = false;
    };

    static bool Init() noexcept;
    static bool Init(const Options& opts) noexcept;
    static void CleanUp() noexcept;
    static bool IsInitialized() noexcept;
    static const Options& GetOptions() noexcept;

#if KNST_USING_PLATFORM_WINDOWS
    static HINSTANCE GetHInstance() noexcept;
#else
    static EGLDisplay GetEGLDisplay() noexcept;
    static EGLConfig GetEGLConfig() noexcept;
#endif
};
```

---

### 5.12 `knst_window_vulkan_content`

Vulkan surface manager. Her pencere başına bir tane. `knst_surface_client` implement eder.

```cpp
class knst_window_vulkan_content : public knst_surface_client {
public:
    void ReleaseSurface() noexcept;
    bool RecreateSurface() noexcept;

    static const char** RequiredInstanceExtensions(uint32_t* out_count) noexcept;
    static bool GetPhysicalDevicePresentationSupport(
        VkPhysicalDevice device, uint32_t queue_family) noexcept;

    bool Init(knst_window* window, VkInstance instance) noexcept;
    void Shutdown() noexcept;

    bool IsValid() const noexcept;
    bool HasSurface() const noexcept;
    VkSurfaceKHR GetSurface() const noexcept;
    VkInstance GetInstance() const noexcept;
    knst_window* GetWindow() const noexcept;
};
```

---

## 6. Fonksiyon Referansı

### 6.1 Yaşam Döngüsü

#### `void creation() noexcept`
Pencereyi **oluşturur ama göstermez**. Platform kaynağı yaratılır, event sistemine kaydedilir.

**Ne zaman kullan:**
- Pencere görünmeden önce ayarlamalar yapacaksan
- İlk frame'i siyah ekran olmadan göstermek istiyorsan
- Custom rendering (OpenGL/Vulkan) context'i önce kuracaksan

#### `void show() noexcept`
Pencereyi ekranda gösterir.

#### `void creation_and_show() noexcept`
`creation()` + `show()` kısayolu. En çok kullanılan.

#### `void destroy() noexcept`
Pencereyi yok eder, kaynakları serbest bırakır. Destructor otomatik çağırır, ama **açıkça** çağırman önerilir — sıralı temizlik için.

**Sıra önemli:**
```
gl.Shutdown()  →  window.destroy()  →  KnstWindowSources::CleanUp()
```

#### `void should_close() noexcept`
Kapatma sinyali gönderir. `is_should_close()` bir sonraki çağrıda `true` döner.

**Kritik:** `should_close()` **sadece flag set eder**, hiçbir şey silmez. Android'de bile lifecycle çağrısı yapmaz — bunu `KnstWindowSources::CleanUp()` yapar.

#### `bool is_should_close() const noexcept`
Ana döngü koşulu.

**Android özel:** `app->destroyRequested != 0` ise otomatik `true` döner — kullanıcı Recents'ten uygulamayı kapattığında da çalışır.

#### `bool is_disconnected() const noexcept`
Wayland compositor kapandığında `true` olur. X11/Windows'ta bağlantı koptuğunda da set edilir.

### 6.2 Başlık ve Meta

#### `void set_title(const knst_c16string&) noexcept`
#### `const knst_c16string& get_title() const noexcept`

UTF-16. Windows'ta native UTF-16 (W API), Linux/Android'de UTF-8'e çevrilir.

### 6.3 Konum ve Boyut

#### `void move(int root_x, int root_y) noexcept`
#### `void move(int root_x, int root_y, const knst_monitor& monitor) noexcept`

Ekran koordinatına taşır. İkinci overload monitöre göre **göreli** taşır.

**Wayland:** Çalışmaz (compositor izin vermez).
**X11/Windows:** WM kısıtlamalarına takılabilir.

#### `void resize(int width, int height) noexcept`
Pencereyi yeniden boyutlandırır.

**Wayland:** Compositor bir sonraki `configure` event'inde yeni boyutu bildirir.
**Windows:** `AdjustWindowRectEx` ile çerçeve hesaba katılır.

#### `void set_minimum_size(int width, int height) noexcept`
#### `void set_maximum_size(int width, int height) noexcept`

`KNST_WINDOW_DEFAULT` geçilirse sınır **kaldırılır**.

### 6.4 Pencere Durumu

#### `void toggle_fullscreen(bool fullscreen) noexcept`
Tam ekran. Wayland'da `xdg_toplevel_set_fullscreen`.

**Not:** Maximize != Fullscreen.
- Maximize → ekranın çalışma alanı (taskbar görünür)
- Fullscreen → tüm ekran (taskbar gizli)

#### `void set_minimized() noexcept`
#### `void set_maximized() noexcept`
#### `void restore() noexcept`
`restore()` hem minimize hem maximize hem fullscreen'den çıkarır.

#### `void hide() noexcept`
#### `void focus() noexcept`

#### `void set_opacity(float opacity) noexcept`
#### `float get_opacity() const noexcept`
0.0 = görünmez, 1.0 = opak.

**Platform:**
- Windows: `WS_EX_LAYERED` + `SetLayeredWindowAttributes`
- X11: `_NET_WM_WINDOW_OPACITY` property
- Wayland: `wp_alpha_modifier_v1` **YOK** → sadece opaque region değişir
- Android: Yok sayılır

### 6.5 Pencere Özellikleri

#### `void set_attribute(int attribute, bool value) noexcept`
#### `bool get_attribute(int attribute) const noexcept`

| Attribute | Anlam |
|-----------|-------|
| `KNST_WINDOW_ATTR_DECORATED` | Sistem çerçevesi göster |
| `KNST_WINDOW_ATTR_RESIZABLE` | Kullanıcı boyutlandırabilir |
| `KNST_WINDOW_ATTR_ALWAYS_ON_TOP` | Diğer pencerelerin üstünde |
| `KNST_WINDOW_ATTR_CLICK_THROUGH` | Fare tıklamaları geçsin |
| `KNST_WINDOW_ATTR_TRANSPARENT` | **Eski isim**, aynı şey |

### 6.6 Başlık Çubuğu (CSD)

#### `void set_title_bar_height(int height) noexcept`
#### `int get_title_bar_height() const noexcept`
`-1` → otomatik DPI hesabı (varsayılan 34px @ 96 DPI).

**Otomatik hesap:**
- Wayland: 34px (logical, compositor scale eder)
- X11/Windows: 34 * (dpi / 96), minimum 20px

#### `template<typename F> void set_csd_hit_test(F&& cb) noexcept`
Kullanıcı tanımlı hit-test callback'i kurar. **CSD modunu aktive eder.**

**Callback imzası:**
```cpp
knst_window::csd_hit cb(const knst_window& w, int x, int y);
```

**Dönüş değerleri:**

| Değer | Etki |
|-------|------|
| `csd_hit::normal` | Olay uygulamaya gider |
| `csd_hit::draggable` | OS pencereyi sürükler |
| `csd_hit::resize_top_left` | Sol üst köşe boyutlandırma |
| `csd_hit::resize_top` | Üst kenar |
| `csd_hit::resize_top_right` | Sağ üst köşe |
| `csd_hit::resize_right` | Sağ kenar |
| `csd_hit::resize_bottom_right` | Sağ alt köşe |
| `csd_hit::resize_bottom` | Alt kenar |
| `csd_hit::resize_bottom_left` | Sol alt köşe |
| `csd_hit::resize_left` | Sol kenar |

#### `void clear_csd_hit_test() noexcept`
CSD modundan çıkar, OS çerçevesine döner.

#### `bool is_csd_enabled() const noexcept`
CSD modunda mı?

#### `void set_csd_auto_cursor(bool enable) noexcept`
#### `bool is_csd_auto_cursor() const noexcept`
Fare hareket ettiğinde otomatik cursor değişsin mi? (default: `true`)

#### `csd_hit hit_test_csd(int x, int y) const noexcept`
Manuel hit-test.

#### `void refresh_csd_cursor(int x, int y) noexcept`
Manuel cursor güncelle.

#### `void csd_handle_hit(csd_hit hit, int x, int y, uint32_t input_serial = 0) noexcept`
CSD hit → OS action (drag/resize). Kütüphane otomatik çağırır.

#### `int get_content_y_offset() const noexcept`
#### `int get_content_height() const noexcept`
İçerik alanının Y offset'i ve yüksekliği.

### 6.7 İmleç

#### `void set_cursor(uint16_t cursor_type) noexcept`
Sistem imleci.

#### `void set_bmp_cursor(const knst_byte_string& data, int w, int h, int hot_x = -1, int hot_y = -1) noexcept`
**RGBA8888** formatında özel imleç. `-1` → otomatik merkez.

#### `void reset_cursor() noexcept`
Varsayılan arrow'a döner.

#### `void set_cursor_mode(int mode) noexcept`

| Mod | Etki |
|-----|------|
| `KNST_WINDOW_CURSOR_ARROW` | Normal |
| `KNST_WINDOW_CURSOR_HIDDEN` | Görünmez ama hareket eder |
| `KNST_WINDOW_CURSOR_DISABLED` | Görünmez + merkeze kilitli |

#### `void set_cursor_pos_on_window(int x, int y) noexcept`
#### `void set_cursor_pos_global(int root_x, int root_y) noexcept`
**Wayland:** Çalışmaz.

### 6.8 Pano

#### `void set_clipboard(const knst_c16string& text) noexcept`
Panoya yazar. UTF-16.

#### `void request_clipboard() noexcept`
#### `const knst_c16string& get_clipboard() const noexcept`
#### `void clear_clipboard() noexcept`

**Okuma asenkron (Wayland):**
```cpp
window.request_clipboard();
// 1-2 frame sonra:
const auto& text = window.get_clipboard();
```

#### `void set_primary_selection(const knst_c16string& text) noexcept`
#### `void request_primary_selection() noexcept`
X11/Wayland'da orta-tık yapıştırma. Windows/Android'de **no-op**.

### 6.9 Drag & Drop

#### `void set_drag_drop_status(bool enabled) noexcept`
Dosya sürükle-bırak aktif/pasif. **Android'de yok sayılır.**

**Event'ler:**
- `FILE_DROP_ENTER` — fare pencereye girdi
- `FILE_DROP_MOVE` — sürükleniyor
- `FILE_DROP_LEAVE` — çıktı
- `FILE_DROP` — bırakıldı, `drop_files` dolu

### 6.10 Icon

#### `void apply_bmp_icon(const knst_byte_string& data, int w, int h) noexcept`
RGBA8888. **Windows** ve **X11**'de çalışır. Wayland/Android'de no-op.

### 6.11 Raw Mouse

#### `void set_raw_mouse_mode(bool enable) noexcept`
Göreceli fare hareketi. FPS kamerası için.

**Event:** `MOTION` with `is_raw = true`, `raw_delta_x/y` dolu.

**Platform:**
- Windows: `RegisterRawInputDevices` + `WM_INPUT`
- X11: XInput2 `RawMotion` (root window'a select)
- Wayland: `zwp_relative_pointer_v1` (pointer lock gerekli)
- Android: Yok sayılır

### 6.12 Idle Inhibit

#### `void set_idle_inhibit(bool enable) noexcept`
Ekran uyumasın.

- **Windows:** `SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)`
- **Wayland:** `zwp_idle_inhibit_manager_v1`
- **X11:** No-op (standart yok)

### 6.13 Display Affinity

#### `void set_display_affinity(bool protect) noexcept`
Sadece **Windows**. `WDA_EXCLUDEFROMCAPTURE` — pencere ekran görüntüsünden gizlenir (DRM için).

### 6.14 Touch Screen

#### `void enable_touch_screen(bool enable) noexcept`
#### `bool supports_touch() const noexcept`
#### `bool is_touch_enabled() const noexcept`
#### `int max_touch_points() const noexcept`

**Üç katman:**

| Katman | Fonksiyon | Ne zaman |
|--------|-----------|----------|
| Derleme | `KNST_WINDOW_ENABLE_TOUCH_SCREEN` | Build |
| Donanım | `supports_touch()` | Runtime |
| Kullanıcı | `is_touch_enabled()` | Runtime |

**Android:** Her zaman açık, macro etkisi yok.

**Fallback:** Touch kapalıysa → mouse event'lerine çevrilir. Donanım yoksa → normal mouse event'leri geliyor zaten.

### 6.15 Event Queue

#### `size_t event_count() const noexcept`
#### `const knst_window_event& get_window_event_handle(size_t i) const noexcept`
#### `void clear_events() noexcept`

Ring buffer okuma. **Her frame sonunda `clear_events()` çağır.**

```cpp
for (size_t i = 0; i < w.event_count(); ++i) {
    const auto& ev = w.get_window_event_handle(i);
    // ...
}
w.clear_events();  // ← ZORUNLU
```

**Ring boyutu:** 256 slot. Coalescing sayesinde MOTION/RESIZE/EXPOSE birleştirilir.

#### `void dispatch_event(const knst_window_event& ev) noexcept`
#### `void dispatch_current_event() noexcept`
Event enjekte et. Kütüphane içinde kullanılır.

### 6.16 Key State

#### `bool is_key_held(int key_code) const noexcept`
#### `bool is_caps_lock_on() const noexcept`
#### `bool is_num_lock_on() const noexcept`

Event beklemeden o anki tuş durumu. **Oyun hareket kontrolü için ideal.**

### 6.17 User Data

#### `void set_user_data(void* data) noexcept`
#### `const void* get_user_data() const noexcept`
Callback'e ikinci parametre olarak geçer.

### 6.18 Callback

#### `template<typename F> void set_redraw_callback(F&& cb) noexcept`
Callback imzası: `void(knst_window&, void*)`

#### `void call_redraw_callback() noexcept`
Callback'i **manuel olarak** çağır. `present_pre()` + callback + `present_post()` üçlüsünü sarar.

### 6.19 Frame Gating

#### `bool is_frame_ready() const noexcept`
#### `void request_frame_callback() noexcept`

**Wayland:** Compositor bir sonraki frame'i hazır olduğunda callback gönderir. `false` iken çizmezsen donma olmaz.

**Windows/X11/Android:** Her zaman `true` (Android'de `app->window != nullptr` kontrolü).

### 6.20 Platform Handles

```cpp
HWND hwnd = window.get_windows_window_handle(); // Windows
xcb_window_t win = window.get_x11_window_handle(); // X11
const wl_surface* surf = window.get_wayland_surface_handle(); // Wayland
```

### 6.21 Sorgular

```cpp
int get_width() const noexcept;
int get_height() const noexcept;
int get_root_x() const noexcept;
int get_root_y() const noexcept;
bool is_maximized() const noexcept;
bool is_minimized() const noexcept;
bool is_fullscreen() const noexcept;
bool is_focused() const noexcept;
```

### 6.22 GL Context Lost

#### `bool is_gl_context_lost() noexcept`
**EGL_CONTEXT_LOST** olduğunda `true` döner. Okuma flag'i sıfırlar.

```cpp
if (window.is_gl_context_lost()) {
    rebuild_shaders();
    rebuild_vaos();
    rebuild_textures();
}
```

Sadece **Android/Linux EGL**'de tetiklenir. Windows'ta her zaman `false`.

---

## 7. Tüm Fonksiyonlar -- Özet

> Bu bölüm hızlı referans içindir. Her fonksiyon tek satırda, yanında `//` yorumu ile amacı yazılıdır. Bu kısmı genel öğrenme için iyi olcağını düşündüğüm için ekledim

### 7.1 `knst_window` — Yaşam Döngüsü

```cpp
knst_window(int width, int height, knst_c16string title, int root_x, int root_y, const knst_monitor& monitor);
            
// Pencere nesnesi oluştur (native pencere henüz yok — creation() çağır)

void creation() noexcept; // Native pencere oluştur ama gösterme (ön-ayar için)
void show() noexcept; // Pencereyi ekranda göster
void creation_and_show() noexcept; // creation() + show() kısayolu
void destroy() noexcept; // Pencereyi yok et, kaynakları bırak
void should_close() noexcept; // Kapatma sinyali gönder (flag set eder, silmez)
bool is_should_close() const noexcept; // Ana döngü koşulu (Android'de otomatik true olabilir)
bool is_disconnected() const noexcept; // Wayland compositor / bağlantı koptu mu?
```

### 7.2 `knst_window` — Başlık & Meta

```cpp
void set_title(const knst_c16string& title) noexcept; // Pencere başlığını değiştir (UTF-16)
const knst_c16string& get_title() const noexcept; // Mevcut başlığı oku
```

### 7.3 `knst_window` — Konum & Boyut

```cpp
void move(int root_x, int root_y) noexcept; // Ekran koordinatına taşı (Wayland: no-op)
void move(int root_x, int root_y, const knst_monitor& monitor) noexcept; // Monitöre göre göreli taşı
void resize(int width, int height) noexcept; // Pencere boyutlandır (asenkron)
void set_minimum_size(int width, int height) noexcept; // Minimum boyut sınırı (DEFAULT → kaldır)
void set_maximum_size(int width, int height) noexcept; // Maximum boyut sınırı (DEFAULT → kaldır)
```

### 7.4 `knst_window` — Pencere Durumu

```cpp
void toggle_fullscreen(bool fullscreen) noexcept; // Tam ekran aç/kapa (maximize'den farklı)
void set_minimized() noexcept; // Simge durumuna küçült
void set_maximized() noexcept; // Ekranın çalışma alanına büyüt (taskbar görünür)
void restore() noexcept; // Minimize / maximize / fullscreen hepsinden çık
void hide() noexcept;  // Pencereyi gizle (destroy değil)
void focus() noexcept;  // Pencereyi öne getir ve odakla
```

### 7.5 `knst_window` — Görünürlük

```cpp
void set_opacity(float opacity) noexcept; // Pencere saydamlığı 0.0-1.0 (Wayland/Android: sınırlı)
float get_opacity() const noexcept; // Mevcut saydamlık değerini oku
```

### 7.6 `knst_window` — Pencere Özellikleri

```cpp
void set_attribute(int attribute, bool value) noexcept; // ATTR_* makroları ile özellik aç/kapa
bool get_attribute(int attribute) const noexcept; // ATTR_* özelliğinin mevcut değerini oku
```

### 7.7 `knst_window` — CSD (Custom Title Bar)

```cpp
void set_title_bar_height(int height) noexcept; // CSD başlık çubuğu yüksekliği (-1 = otomatik DPI)
int get_title_bar_height() const noexcept; // Mevcut başlık yüksekliği
template<typename F> void set_csd_hit_test(F&& cb) noexcept; // CSD hit-test callback'i kur (CSD'yi aktive eder)
void clear_csd_hit_test() noexcept; // CSD'yi kapat, OS çerçevesine dön
bool is_csd_enabled() const noexcept; // CSD aktif mi?
void set_csd_auto_cursor(bool enable) noexcept; // Fare kenara gelince cursor otomatik değişsin mi?
bool is_csd_auto_cursor() const noexcept; // Auto-cursor aktif mi?
csd_hit hit_test_csd(int x, int y) const noexcept; // Belirtilen noktada CSD hit-test sonucu
void refresh_csd_cursor(int x, int y) noexcept; // Auto-cursor'ı manuel tetikle
void csd_handle_hit(csd_hit hit, int x, int y, uint32_t serial = 0) noexcept; // CSD hit → OS drag/resize
int get_content_y_offset() const noexcept; // Başlık çubuğu yüksekliği (içerik başlangıç Y)
int get_content_height() const noexcept;  // Başlık çubuğu hariç içerik yüksekliği
```

### 7.8 `knst_window` — İmleç

```cpp
void set_cursor(uint16_t cursor_type) noexcept;  // Sistem imleci
void set_bmp_cursor(const knst_byte_string& data, int w, int h, int hot_x = -1, int hot_y = -1) noexcept; // RGBA8888 bitmap imleç
void reset_cursor() noexcept; // Varsayılan arrow'a dön
void set_cursor_mode(int mode) noexcept; // ARROW / HIDDEN / DISABLED
void set_cursor_pos_on_window(int x, int y) noexcept; // İmleci pencere içi taşı (Wayland: no-op)
void set_cursor_pos_global(int root_x, int root_y) noexcept; // İmleci ekran koordinatına taşı (Wayland: no-op)
```

### 7.9 `knst_window` — Pano

```cpp
void set_clipboard(const knst_c16string& text) noexcept; // Panoya yaz (UTF-16)
void request_clipboard() noexcept; // Panodan oku (async — CLIPBOARD_DATA event'i gelir)
const knst_c16string& get_clipboard() const noexcept; // Son okunan pano içeriği
void clear_clipboard() noexcept; // Pano içeriğini temizle (sadece local cache)
void set_primary_selection(const knst_c16string& text) noexcept; // X11/Wayland orta-tık yapıştırma yaz
void request_primary_selection() noexcept; // X11/Wayland orta-tık yapıştırma oku
```

### 7.10 `knst_window` — Drag & Drop

```cpp
void set_drag_drop_status(bool enabled) noexcept; // Dosya sürükle-bırak aç/kapa (Android: no-op)
```

### 7.11 `knst_window` — Icon

```cpp
void apply_bmp_icon(const knst_byte_string& data, int width, int height) noexcept;  // RGBA8888 pencere ikonu
```

### 7.12 `knst_window` — Input Modları

```cpp
void set_raw_mouse_mode(bool enable) noexcept; // FPS tarzı göreceli fare hareketi (Android: no-op)
void set_idle_inhibit(bool enable) noexcept; // Ekran uyumasın (Win/Wayland: çalışır, X11: no-op)
void set_display_affinity(bool protect) noexcept; // Ekran görüntüsünden gizle (Windows only)
```

### 7.13 `knst_window` — Touch Screen

```cpp
void enable_touch_screen(bool enable) noexcept; // Touch event'lerini aç/kapa (runtime)
bool supports_touch() const noexcept; // Donanım touch destekliyor mu?
bool is_touch_enabled() const noexcept; // Kullanıcı touch'ı açmış mı?
int max_touch_points() const noexcept; // Maksimum eşzamanlı parmak sayısı
```

### 7.14 `knst_window` — Event Queue

```cpp
size_t event_count() const noexcept; // Bu frame'deki event sayısı
const knst_window_event& get_window_event_handle(size_t i) const noexcept; // i. event'i oku
void clear_events() noexcept; // Frame sonunda event'leri temizle (ZORUNLU)
void dispatch_event(const knst_window_event& ev) noexcept; // Manuel event enjekte et
void dispatch_current_event() noexcept;  // m_knst_event'i kuyruğa at
```

### 7.15 `knst_window` — Key State

```cpp
bool is_key_held(int key_code) const noexcept; // Tuş şu anda basılı mı? (event beklemez)
bool is_caps_lock_on() const noexcept; // CapsLock açık mı?
bool is_num_lock_on() const noexcept; // NumLock açık mı?
```

### 7.16 `knst_window` — User Data & Callback

```cpp
void set_user_data(void* data) noexcept; // Callback'e geçirilecek kullanıcı verisi
const void* get_user_data() const noexcept; // Kayıtlı kullanıcı verisini oku
template<typename F> void set_redraw_callback(F&& cb) noexcept; // Her frame'de çağrılacak callback
void call_redraw_callback() noexcept; // Callback'i manuel çağır (present_pre+cb+present_post sarar)
```

### 7.17 `knst_window` — Frame Gating

```cpp
bool is_frame_ready() const noexcept; // Wayland: compositor hazır mı? (diğerleri: true)
void request_frame_callback() noexcept; // Wayland: bir sonraki frame callback'ini iste
```

### 7.18 `knst_window` — Sorgular

```cpp
int get_width() const noexcept; // Pencere içerik genişliği (logical)
int get_height() const noexcept; // Pencere içerik yüksekliği (logical)
int get_root_x() const noexcept; // Pencere X konumu (ekran)
int get_root_y() const noexcept; // Pencere Y konumu (ekran)
bool is_maximized() const noexcept; // Maximize mi?
bool is_minimized() const noexcept; // Minimize mi?
bool is_fullscreen() const noexcept; // Fullscreen mi?
bool is_focused() const noexcept; // Odaklı mı?
```

### 7.19 `knst_window` — GL Context & Wayland

```cpp
bool is_gl_context_lost() noexcept; // EGL_CONTEXT_LOST algılandı mı? (okuma flag'i sıfırlar)
void notify_gl_context_lost() noexcept; // Kütüphane içi — context lost flag set et

void apply_wayland_viewport_resize(int logical_w, int logical_h) noexcept; // Wayland viewport boyutlandır
int wayland_physical_width(int logical_w) const noexcept; // Wayland: fiziksel (buffer) genişlik
int wayland_physical_height(int logical_h) const noexcept; // Wayland: fiziksel (buffer) yükseklik
```

### 7.20 `knst_window` — Surface Client & Static

```cpp
void register_surface_client(knst_surface_client* client) noexcept; // Surface client kaydet (internal)
void unregister_surface_client(knst_surface_client* client) noexcept; // Surface client kaldır (internal)
void notify_window_destroyed() noexcept; // Tüm surface client'lara destroy bildir
void notify_surface_release() noexcept; // Tüm surface client'lara release bildir
void notify_surface_recreate() noexcept; // Tüm surface client'lara recreate bildir

static knst_window_capabilities capabilities() noexcept; // Platform özellik matrisi (runtime)
```

### 7.21 `KnstWindowSources` — Platform Kaynakları

```cpp
static void Init() noexcept; // Windows/Linux: platform kaynaklarını başlat
static void Init(android_app* app); // Android: android_app ile başlat
static void CleanUp() noexcept; // Platform kaynaklarını temizle (Android: looper pump eder)
static uint32_t get_current_time_ms() noexcept; // Monotonik zaman (ms) — event timestamp'leri için

static knst_backend active_backend() noexcept; // Aktif backend
static bool is_x11_active() noexcept; // Aktif backend X11 mi?
static bool is_wayland_active() noexcept; // Aktif backend Wayland mı?
static bool is_windows_active() noexcept; // Aktif backend Windows mu?

#if KNST_USING_PLATFORM_WINDOWS
static HINSTANCE& get_windows_native_instance_handle() noexcept; // Windows HINSTANCE
#elif KNST_USING_LINUX_PLATFORM_X11
static xcb_window_t& get_native_x11_root_handle() noexcept; // X11 root window
static xcb_connection_t* get_native_x11_connection_handle() noexcept; // X11 XCB connection
static Display* get_native_x11_display() noexcept; // X11 Display (Xlib)
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
static wl_display* wayland_display; // Wayland wl_display (public)
#elif defined(KNST_USING_PLATFORM_ANDROID)
static android_app* get_android_app() noexcept; // Android app pointer
#endif
```

### 7.22 `knst_window_event_system` — Global Event Pump

```cpp
static void block_pool_event() noexcept; // Olay gelene kadar blokla (Wayland: kullanma)
static void non_block_pool_event() noexcept; // Olayları hemen çek ve dağıt (her frame)
static size_t get_window_count() noexcept; // Kayıtlı pencere sayısı
```

### 7.23 `knst_display` — Monitör Yönetimi

```cpp
static void refresh_screens() noexcept; // Monitör listesini yenile
static const knst_vector<std::unique_ptr<knst_monitor>>& get_monitor_list() noexcept; // Tüm monitörler
static const knst_monitor* get_primary_monitor() noexcept; // Ana monitör
static size_t get_monitor_count() noexcept; // Monitör sayısı
static const knst_monitor* get_monitor(size_t index) noexcept; // i. monitör
```

### 7.24 `knst_mobile_keyboard` — Android Soft Keyboard

```cpp
static bool show() noexcept; // Android yumuşak klavyeyi göster (PC: false döner, no-op)
static bool hide() noexcept; // Android yumuşak klavyeyi gizle (PC: false döner, no-op)
static void toggle() noexcept; // Aç/kapa (300ms debounce)
static bool is_visible() noexcept;  // Klavye görünür mü? (mutex korumalı)
```

### 7.25 `knst_opengl_global` — GL Context Init

```cpp
static bool Init() noexcept; // Varsayılan Options ile GL global init
static bool Init(const Options& opts) noexcept; // Verilen Options ile GL global init
static void CleanUp() noexcept; // GL global cleanup
static bool IsInitialized() noexcept; // GL global hazır mı?
static const Options& GetOptions() noexcept; // Mevcut Options'ı oku
static HINSTANCE GetHInstance() noexcept; // Windows: HINSTANCE
static EGLDisplay GetEGLDisplay() noexcept; // EGL display (Windows hariç)
static EGLConfig GetEGLConfig() noexcept; // EGL config (Windows hariç)
```

### 7.26 `knst_window_opengl_content` — Per-Window GL Context

```cpp
bool Init(knst_window* window, bool reuse_old = false) noexcept; // Pencereye GL context bağla
bool InitShared(knst_window* window, const knst_window_opengl_content& share) noexcept; // Paylaşımlı context
void Shutdown() noexcept; // GL context kapat

void MakeCurrent() noexcept; // Bu context'i current yap
void SwapBuffers() noexcept; // Buffer swap (vsync'e göre present eder)

void SetSwapInterval(int interval) noexcept; // 0=off, 1=on, -1=adaptive
int GetSwapInterval() const noexcept; // Kullanıcı tercihi

void OnResize(int new_width, int new_height) noexcept; // Pencere yeniden boyutlandı
void OnScaleChanged() noexcept; // Wayland fractional-scale değişti

bool RecreateSurface() noexcept; // Surface yeniden oluştur (Android INIT_WINDOW)
void ReleaseSurface() noexcept; // Surface bırak (Android TERM_WINDOW)

bool IsValid() const noexcept; // Context geçerli mi?
bool HasSurface() const noexcept; // Surface geçerli mi?
knst_window* GetWindow() const noexcept; // Bağlı pencere

int GetWidth() const noexcept; // Logical genişlik
int GetHeight() const noexcept; // Logical yükseklik
int GetFramebufferWidth() const noexcept;  // Fiziksel genişlik
int GetFramebufferHeight() const noexcept; // Fiziksel yükseklik

const char* GetVendorString() const noexcept; // GPU vendor
const char* GetRendererString() const noexcept; // GPU renderer
const char* GetVersionString() const noexcept; // GL version string
const char* GetGLSLVersionString() const noexcept; // GLSL version string
int GetActualMajorVersion() const noexcept; // Gerçek GL major
int GetActualMinorVersion() const noexcept; // Gerçek GL minor
knst_opengl_global::ClientAPI GetClientAPI() const noexcept; // OpenGL mi OpenGLES mi?

static void* LoadGLProc(const char* name) noexcept;  // GLAD/GLEW loader entry
```

### 7.27 `knst_window_vulkan_content` — Per-Window Vulkan Surface

```cpp
void ReleaseSurface() noexcept;   // VkSurfaceKHR bırak
bool RecreateSurface() noexcept;  // VkSurfaceKHR yeniden oluştur

static const char** RequiredInstanceExtensions(uint32_t* out_count) noexcept; // Gerekli VkInstance extensions
static bool GetPhysicalDevicePresentationSupport(
    VkPhysicalDevice device, uint32_t queue_family) noexcept; // Fiziksel cihaz present destekliyor mu?

bool Init(knst_window* window, VkInstance instance) noexcept; // VkSurfaceKHR oluştur
void Shutdown() noexcept; // VkSurfaceKHR temizle

bool IsValid() const noexcept; // Surface geçerli mi?
bool HasSurface() const noexcept; // VkSurfaceKHR != VK_NULL_HANDLE mi?
VkSurfaceKHR GetSurface() const noexcept; // VkSurfaceKHR handle
VkInstance GetInstance() const noexcept; // VkInstance handle
knst_window* GetWindow() const noexcept; // Bağlı pencere
```

### 7.28 `knst_window_event` — Event Alanları

```cpp
void begin(uint32_t ev_type) noexcept;   // Event'i sıfırla + type & timestamp ata (kütüphane çağırır)
```

---

## 8. Makro Referansı

### 8.1 Olay Tipleri — `ev.type`

#### Aralıklar

| Aralık | Kategori |
|--------|----------|
| 0 | `UNKNOWN` — dağıtılmaz |
| 100-103 | Fare |
| 200-201 | Klavye / metin |
| 300-309 | Pencere durumu |
| 400-401 | Odak |
| 450-453 | Pano / IME |
| 500-503 | Dosya drop |
| 600 | Touch |
| 603-632 | Mobil (Android) |
| 700-709 | Uygulama yaşam döngüsü |

#### Fare (100-199)

| Makro | Değer | Ne zaman? |
|-------|-------|-----------|
| `KNST_WINDOW_EVENT_MOUSE` | 100 | Bas / bırak / scroll |
| `KNST_WINDOW_EVENT_MOTION` | 101 | Hareket (normal veya raw) |
| `KNST_WINDOW_EVENT_ENTER` | 102 | Fare pencereye girdi |
| `KNST_WINDOW_EVENT_LEAVE` | 103 | Fare çıktı |

#### Klavye (200-299)

| Makro | Değer | Ne zaman? |
|-------|-------|-----------|
| `KNST_WINDOW_EVENT_KEYBOARD` | 200 | Bas / bırak / tekrar |
| `KNST_WINDOW_EVENT_TEXT_INPUT` | 201 | Karakter girişi (IME, shift+digit, AltGr) |

**Önemli:** `KEYBOARD` event'i `key_code` verir (fiziksel tuş). `TEXT_INPUT` event'i `text_input` verir (üretilen karakter).

**Örnek fark:**
- `Shift+1` → `KEYBOARD` `key_code = '1'` + `mods = SHIFT`
- `Shift+1` → `TEXT_INPUT` `text_input = "!"`

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
| `KNST_WINDOW_EVENT_SCALE_CHANGED` | 309 |

#### Odak (400-499)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_FOCUS_IN` | 400 |
| `KNST_WINDOW_EVENT_FOCUS_OUT` | 401 |

#### Pano / IME (450-453)

| Makro | Değer | Ne zaman? |
|-------|-------|-----------|
| `KNST_WINDOW_EVENT_CLIPBOARD_UPDATE` | 450 | Pano değişti (Windows only) |
| `KNST_WINDOW_EVENT_CLIPBOARD_DATA` | 451 | `request_clipboard()` sonucu hazır |
| `KNST_WINDOW_EVENT_TEXT_PREEDIT` | 452 | IME preedit güncellendi |
| `KNST_WINDOW_EVENT_TEXT_DELETE_SURROUNDING` | 453 | IME metin silmek istiyor |

#### Dosya Drop (500-599)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_FILE_DROP` | 500 |
| `KNST_WINDOW_EVENT_FILE_DROP_ENTER` | 501 |
| `KNST_WINDOW_EVENT_FILE_DROP_MOVE` | 502 |
| `KNST_WINDOW_EVENT_FILE_DROP_LEAVE` | 503 |

#### Touch (600)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_TOUCH` | 600 |
| `KNST_WINDOW_EVENT_MOBILE_TOUCH` | 600 (alias) |

#### Mobil (603-632)

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_EVENT_MOBILE_MENU` | 603 |
| `KNST_WINDOW_EVENT_MOBILE_SEARCH` | 604 |
| `KNST_WINDOW_EVENT_MOBILE_APP_SWITCH` | 605 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_UP` | 607 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_DOWN` | 608 |
| `KNST_WINDOW_EVENT_MOBILE_VOLUME_MUTE` | 609 |
| `KNST_WINDOW_EVENT_MOBILE_POWER` | 610 |
| `KNST_WINDOW_EVENT_MOBILE_CAMERA` | 611 |
| `KNST_WINDOW_EVENT_MOBILE_HELP` | 612 |
| `KNST_WINDOW_EVENT_MOBILE_SETTINGS` | 613 |
| `KNST_WINDOW_EVENT_MOBILE_SLEEP` | 614 |
| `KNST_WINDOW_EVENT_MOBILE_WAKEUP` | 615 |
| `KNST_WINDOW_EVENT_MOBILE_ASSIST` | 616 |
| `KNST_WINDOW_EVENT_MOBILE_BOOKMARK` | 617 |
| `KNST_WINDOW_EVENT_MOBILE_CALCULATOR` | 618 |
| `KNST_WINDOW_EVENT_MOBILE_CALENDAR` | 619 |
| `KNST_WINDOW_EVENT_MOBILE_CONTACTS` | 620 |
| `KNST_WINDOW_EVENT_MOBILE_EXPLORER` | 621 |
| `KNST_WINDOW_EVENT_MOBILE_MUSIC` | 622 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PLAY_PAUSE` | 623 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_STOP` | 624 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_NEXT` | 625 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PREVIOUS` | 626 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_REWIND` | 627 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_FORWARD` | 628 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_RECORD` | 629 |
| `KNST_WINDOW_EVENT_MOBILE_MEDIA_PAUSE` | 630 |
| `KNST_WINDOW_EVENT_MOBILE_BACK` | 631 |
| `KNST_WINDOW_EVENT_MOBILE_HOME` | 632 |

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

### 8.2 Tuş Aksiyonları — `ev.key_action`

| Makro | Değer | Anlam |
|-------|-------|-------|
| `KNST_WINDOW_KEY_ACTION_PRESS` | 1 | Basıldı (ilk) |
| `KNST_WINDOW_KEY_ACTION_RELEASE` | 2 | Bırakıldı |
| `KNST_WINDOW_KEY_ACTION_REPEAT` | 3 | Basılı tutuluyor (auto-repeat) |

### 8.3 Fare Aksiyonları — `ev.mouse_action`

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_MOUSE_ACTION_PRESS` | 1 |
| `KNST_WINDOW_MOUSE_ACTION_RELEASE` | 2 |
| `KNST_WINDOW_MOUSE_ACTION_SCROLL` | 3 |

### 8.4 Fare Düğmeleri — `ev.mouse_button`

| Makro | Değer | Fiziksel |
|-------|-------|----------|
| `KNST_WINDOW_MOUSE_BUTTON_LEFT` | 1 | Sol |
| `KNST_WINDOW_MOUSE_BUTTON_MIDDLE` | 2 | Orta |
| `KNST_WINDOW_MOUSE_BUTTON_RIGHT` | 3 | Sağ |
| `KNST_WINDOW_MOUSE_BUTTON_BACK` | 8 | Yan geri |
| `KNST_WINDOW_MOUSE_BUTTON_FORWARD` | 9 | Yan ileri |

**Scroll event'lerinde:**
- Vertical scroll → `mouse_button = 4`
- Horizontal scroll → `mouse_button = 6`
- `mouse_scroll_delta` işareti yönü belirtir (+1 yukarı, -1 aşağı)

### 8.5 Modifier Bitleri — `ev.mods`

| Makro | Değer | Bit |
|-------|-------|-----|
| `KNST_WINDOW_MOD_SHIFT` | 1 | 0 |
| `KNST_WINDOW_MOD_CONTROL` | 2 | 1 |
| `KNST_WINDOW_MOD_ALT` | 4 | 2 |
| `KNST_WINDOW_MOD_SUPER` | 8 | 3 |
| `KNST_WINDOW_MOD_CAPS_LOCK` | 16 | 4 |
| `KNST_WINDOW_MOD_NUM_LOCK` | 32 | 5 |

```cpp
const bool ctrl = (ev.mods & KNST_WINDOW_MOD_CONTROL) != 0;
const bool shift = (ev.mods & KNST_WINDOW_MOD_SHIFT) != 0;
```

### 8.6 Tuş Kodları — `ev.key_code`

#### Harfler

```cpp
KNST_WINDOW_KEY_CODE_A ... KNST_WINDOW_KEY_CODE_Z
```

Değer: ASCII `'A'` - `'Z'` (tüm platformlarda normalize).

#### Türkçe Karakterler

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
```

#### Numpad

```cpp
KNST_WINDOW_KEY_CODE_NUMPAD_0 ... KNST_WINDOW_KEY_CODE_NUMPAD_9
KNST_WINDOW_KEY_CODE_NUMPAD_ADD / SUBTRACT / MULTIPLY / DIVIDE
KNST_WINDOW_KEY_CODE_NUMPAD_DECIMAL / ENTER
```

#### Fonksiyon Tuşları

```cpp
KNST_WINDOW_KEY_CODE_F1 ... KNST_WINDOW_KEY_CODE_F12
```

#### Özel

```cpp
KNST_WINDOW_KEY_CODE_ESCAPE / ENTER / SPACE / BACKSPACE / TAB
KNST_WINDOW_KEY_CODE_CAPS_LOCK / NUM_LOCK / SCROLL_LOCK
KNST_WINDOW_KEY_CODE_DELETE / INSERT
```

#### Modifier

```cpp
KNST_WINDOW_KEY_CODE_SHIFT / CONTROL / ALT / SUPER / MENU
```

#### Yön

```cpp
KNST_WINDOW_KEY_CODE_LEFT / RIGHT / UP / DOWN
```

#### Navigasyon

```cpp
KNST_WINDOW_KEY_CODE_HOME / END / PAGE_UP / PAGE_DOWN
KNST_WINDOW_KEY_CODE_PRINT / PAUSE / BREAK
```

#### Sembol

```cpp
KNST_WINDOW_KEY_CODE_SEMICOLON / SLASH / GRAVE
KNST_WINDOW_KEY_CODE_LEFT_BRACKET / BACKSLASH / RIGHT_BRACKET
KNST_WINDOW_KEY_CODE_APOSTROPHE / PERIOD / COMMA
KNST_WINDOW_KEY_CODE_MINUS / PLUS / EQUAL / QUOTE / COLON
KNST_WINDOW_KEY_CODE_TILDE / LESS / GREATER / QUESTION
KNST_WINDOW_KEY_CODE_PIPE / LEFT_BRACE / RIGHT_BRACE / UNDERSCORE
```

#### Medya

```cpp
KNST_WINDOW_KEY_CODE_VOLUME_UP / DOWN / MUTE
KNST_WINDOW_KEY_CODE_MEDIA_PLAY / STOP / NEXT / PREV / PAUSE
```

#### Tarayıcı

```cpp
KNST_WINDOW_KEY_CODE_BROWSER_HOME / BACK / FORWARD / REFRESH / SEARCH / FAVORITES
```

### 8.7 İmleç Tipleri

#### Sistem İmleçleri (set_cursor)

| Makro | Windows | X11 | Wayland |
|-------|---------|-----|---------|
| `KNST_WINDOW_CURSOR_ARROW` | `OCR_NORMAL` | `XC_left_ptr` | `left_ptr` |
| `KNST_WINDOW_CURSOR_IBEAM` | `OCR_IBEAM` | `XC_xterm` | `xterm` |
| `KNST_WINDOW_CURSOR_CROSSHAIR` | `OCR_CROSS` | `XC_crosshair` | `crosshair` |
| `KNST_WINDOW_CURSOR_HAND` | `OCR_HAND` | `XC_hand2` | `hand2` |
| `KNST_WINDOW_CURSOR_HRESIZE` | `OCR_SIZEWE` | `XC_sb_h_double_arrow` | `sb_h_double_arrow` |
| `KNST_WINDOW_CURSOR_VRESIZE` | `OCR_SIZENS` | `XC_sb_v_double_arrow` | `sb_v_double_arrow` |
| `KNST_WINDOW_CURSOR_MOVE` | `OCR_SIZEALL` | `XC_fleur` | `fleur` |
| `KNST_WINDOW_CURSOR_WAIT` | `OCR_WAIT` | `XC_watch` | `watch` |
| `KNST_WINDOW_CURSOR_HELP` | `OCR_HELP` | `XC_question_arrow` | `question_arrow` |
| `KNST_WINDOW_CURSOR_NOT_ALLOWED` | `OCR_NO` | `XC_X_cursor` | `X_cursor` |
| `KNST_WINDOW_CURSOR_NWSE_RESIZE` | `OCR_SIZENWSE` | `nwse-resize` | `nwse-resize` |
| `KNST_WINDOW_CURSOR_NESW_RESIZE` | `OCR_SIZENESW` | `nesw-resize` | `nesw-resize` |

#### Mod İmleçleri (set_cursor_mode)

| Makro | Anlam |
|-------|-------|
| `KNST_WINDOW_CURSOR_ARROW` | Normal |
| `KNST_WINDOW_CURSOR_HIDDEN` | Görünmez |
| `KNST_WINDOW_CURSOR_DISABLED` | Kilitli |

### 8.8 Pencere Özellikleri

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_ATTR_DECORATED` | 1 |
| `KNST_WINDOW_ATTR_RESIZABLE` | 2 |
| `KNST_WINDOW_ATTR_ALWAYS_ON_TOP` | 3 |
| `KNST_WINDOW_ATTR_CLICK_THROUGH` | 4 |
| `KNST_WINDOW_ATTR_TRANSPARENT` | 4 (eski isim) |

### 8.9 Touch Aksiyonları

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_TOUCH_ACTION_PRESS` | 0 |
| `KNST_WINDOW_TOUCH_ACTION_RELEASE` | 1 |
| `KNST_WINDOW_TOUCH_ACTION_MOVE` | 2 |
| `KNST_WINDOW_TOUCH_ACTION_CANCEL` | 3 |
| `KNST_WINDOW_TOUCH_ACTION_OUTSIDE` | 4 |
| `KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS` | 5 |
| `KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE` | 6 |

### 8.10 Android Ekran Yönü

| Makro | Değer |
|-------|-------|
| `KNST_WINDOW_ORIENTATION_UNDEFINED` | 0 |
| `KNST_WINDOW_ORIENTATION_PORTRAIT` | 1 |
| `KNST_WINDOW_ORIENTATION_LANDSCAPE` | 2 |
| `KNST_WINDOW_ORIENTATION_SQUARE` | 3 |

### 8.11 Diğer Sabitler

| Makro | Değer | Anlam |
|-------|-------|-------|
| `KNST_WINDOW_DEFAULT` | -10000 | Otomatik değer işareti |
| `KNST_MAX_TOUCH_POINTS` | 10 | Varsayılan touch nokta limiti |
| `KNST_MAX_HELD_KEYS` | 8 | Basılı tuş tablosu boyutu |
| `KNST_WINDOW_KEY_REPEAT_DELAY` | 200 | İlk tekrar (ms) |
| `KNST_WINDOW_KEY_REPEAT_INTERVAL` | 20 | Tekrar aralığı (ms) |
| `KNST_ANDROID_KEY_OFFSET` | 0x10000 | Android keycode offset |

---

## 9. Platform Notları

### 9.1 Windows

#### Native Handle
```cpp
HWND hwnd = window.get_windows_window_handle();
```

#### Özellik Matrisi

| Özellik | Destek |
|---------|:------:|
| move | ✅ |
| resize | ✅ |
| fullscreen | ✅ |
| minimize | ✅ |
| maximize | ✅ |
| focus | ✅ |
| opacity | ✅ |
| always_on_top | ✅ |
| click_through | ✅ |
| CSD | ✅ |
| touch | ✅ (WM_TOUCH) |
| clipboard | ✅ |
| drag_drop | ✅ (IDropTarget) |
| raw_mouse | ✅ |
| idle_inhibit | ✅ |
| display_affinity | ✅ |
| cursor_position | ✅ |
| set_icon | ✅ |
| fractional_scale | ✅ |
| multiple_windows | ✅ |

#### Otomatik DPI

`SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` **ilk pencere açılmadan önce** çağrılır. Yüksek DPI ekranlarda metin ve boyutlar doğru ölçeklenir.

#### CSD

- `WM_NCCALCSIZE` → çerçeve kaldırılır (ilk `CreateWindowExW` öncesi flag set edilir)
- `WM_NCHITTEST` → hit-test callback'i çağrılır, `HT*` kodlarına çevrilir
- `WM_GETMINMAXINFO` → max-size hint (maximize sırasında geçici kaldırılır)

#### Raw Mouse

`RegisterRawInputDevices` + `WM_INPUT`. `MOUSE_MOVE_ABSOLUTE` flag kontrol edilir — sadece göreceli hareket işlenir.

---

### 9.2 Linux X11

#### Native Handle
```cpp
xcb_window_t win = window.get_x11_window_handle();
xcb_connection_t* c = KnstWindowSources::get_native_x11_connection_handle();
Display* dpy = KnstWindowSources::get_native_x11_display();
```

#### Özellik Matrisi

| Özellik | Destek |
|---------|:------:|
| move | ✅ |
| resize | ✅ |
| fullscreen | ✅ |
| minimize | ✅ |
| maximize | ✅ |
| focus | ✅ |
| opacity | ✅ |
| always_on_top | ✅ |
| click_through | ✅ |
| CSD | ✅ |
| touch | ✅ (XInput2) |
| clipboard | ✅ |
| primary_selection | ✅ |
| drag_drop | ✅ (Xdnd) |
| raw_mouse | ✅ (XInput2) |
| idle_inhibit | ⚠️ (yok) |
| cursor_position | ✅ |
| set_icon | ✅ |
| fractional_scale | ❌ |
| multiple_windows | ✅ |

#### `_NET_WM_SYNC_REQUEST`

Flicker-free resize için `xcb_sync_counter` kullanılır. WM her resize'da counter bekler — kütüphane yeni boyut frame'i çizildikten sonra counter'ı arttırır.

---

### 9.3 Linux Wayland

#### Native Handle
```cpp
const wl_surface* surf = window.get_wayland_surface_handle();
wl_display* dpy = KnstWindowSources::wayland_display;
wl_compositor* comp = KnstWindowSources::compositor;
wl_shm* shm = KnstWindowSources::shm;
```

#### Özellik Matrisi

| Özellik | Destek |
|---------|:------:|
| move | ❌ (protokol) |
| resize | ✅ |
| fullscreen | ✅ |
| minimize | ✅ |
| maximize | ✅ |
| focus | ❌ (compositor) |
| opacity | ❌ |
| always_on_top | ❌ |
| click_through | ✅ |
| CSD | ✅ |
| touch | ✅ (wl_touch) |
| clipboard | ✅ |
| primary_selection | ✅ |
| drag_drop | ✅ |
| raw_mouse | ✅ (relative-pointer) |
| idle_inhibit | ✅ |
| cursor_position | ❌ |
| set_icon | ✅ (xdg-toplevel-icon) |
| fractional_scale | ✅ |
| multiple_windows | ✅ |

#### ⚠️ Çizim Zorunlu

Wayland'da pencere göstermek için **her frame'de** `wl_surface_attach` + `wl_surface_commit` gerekli. Boş bırakırsan pencere görünmez.

**Yazılım render:**

```cpp
void draw_sw(knst_window& w) {
    wl_surface* s = const_cast<wl_surface*>(w.get_wayland_surface_handle());
    if (!s || !KnstWindowSources::shm) return;

    int W = w.get_width(), H = w.get_height();
    int stride = W * 4;

    int fd = memfd_create("buf", MFD_CLOEXEC);
    ftruncate(fd, stride * H);
    uint32_t* px = (uint32_t*)mmap(nullptr, stride * H, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    for (int i = 0; i < W * H; ++i) px[i] = 0xFF202060;

    wl_shm_pool* pool = wl_shm_create_pool(KnstWindowSources::shm, fd, stride * H);
    wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_surface_attach(s, buf, 0, 0);
    wl_surface_damage_buffer(s, 0, 0, W, H);
    wl_surface_commit(s);
    wl_display_flush(KnstWindowSources::wayland_display);

    wl_buffer_destroy(buf);
    wl_shm_pool_destroy(pool);
    munmap(px, stride * H);
    close(fd);
}
```

#### Frame Callback Gating

Compositor hazır olana kadar `is_frame_ready()` `false` döner:

```cpp
if (!window.is_frame_ready()) return;
// ... çiz ...
window.request_frame_callback();
gl.SwapBuffers();
```

---

### 9.4 Android

#### Native Handle
```cpp
android_app* app = KnstWindowSources::get_android_app();
ANativeWindow* win = app->window;
```

#### Özellik Matrisi

| Özellik | Destek |
|---------|:------:|
| move | ❌ |
| resize | ❌ |
| fullscreen | ❌ |
| minimize | ❌ |
| maximize | ❌ |
| focus | ❌ |
| opacity | ❌ |
| always_on_top | ❌ |
| click_through | ❌ |
| CSD | ❌ |
| touch | ✅ (built-in) |
| clipboard | ✅ (JNI) |
| primary_selection | ❌ |
| drag_drop | ❌ |
| raw_mouse | ❌ |
| idle_inhibit | ❌ |
| cursor_position | ❌ |
| set_icon | ❌ |
| fractional_scale | ✅ |
| multiple_windows | ❌ |

#### `android_main`

```cpp
void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);
    // ...
}
```

`app->window` başlangıçta `NULL` — `Init()` bunu bekler.

---

## 10. İç Tasarım, Taktikler ve Arka Plan Bilgisi

> Bu bölüm, kütüphanenin **iç tasarımını**, **alınan kararların nedenlerini** ve **kritik taktikleri** anlatır. Kullanıcı olarak bilmen gerekmez ama projeye hakim olmak için altın değerinde. İsterseniz bakabilirsiniz

### 10.1 Event Ring Buffer Tasarımı

```cpp
template<size_t N>
struct knst_event_ring {
    static_assert((N & (N - 1)) == 0, "must be power of two");

    knst_window_event data[N];
    size_t count = 0;
    size_t head  = 0;

    void push(const knst_window_event& ev) noexcept {
        data[head] = ev;
        head = (head + 1) & (N - 1);   // bitmask — modülo yerine
        if (count < N) ++count;
    }

    const knst_window_event& at(size_t i) const noexcept {
        size_t start = (head + N - count) & (N - 1);
        return data[(start + i) & (N - 1)];
    }
};
```

**Neden power-of-two?** `% N` yerine `& (N - 1)` kullanmak için. CPU'da modülo pahalıdır (özellikle ARM'de), bitmask tek cycle.

**Neden fixed size?** Dinamik büyüme allocation demek → allocation event sırasında olmamalı. 256 slot, saniyede 60 frame × 4 event/frame ortalaması için bol bol yeterli. Taşarsa en eski event **atılır** (ring buffer wrap-around davranışı).

**Coalescing:** `dispatch_event()` içinde MOTION/RESIZE/EXPOSE birleştirilir:

```cpp
if (ev.type == KNST_WINDOW_EVENT_MOTION ||
    ev.type == KNST_WINDOW_EVENT_RESIZE ||
    ev.type == KNST_WINDOW_EVENT_EXPOSE) {

    const size_t n = m_events.size();
    if (n > 0) {
        auto& newest = const_cast<knst_window_event&>(m_events.at(n - 1));
        if (newest.type == ev.type && newest.is_raw == ev.is_raw) {
            if (ev.is_raw) {
                // Raw delta TOPLANIR (FPS kamera için)
                const int32_t dx = newest.raw_delta_x + ev.raw_delta_x;
                const int32_t dy = newest.raw_delta_y + ev.raw_delta_y;
                newest = ev;
                newest.raw_delta_x = dx;
                newest.raw_delta_y = dy;
            } else {
                // Normal motion overwrite (son pozisyon geçerli)
                newest = ev;
            }
            return;
        }
    }
}
```

**Neden raw sum, normal overwrite?** FPS kamerası için **tüm hareketi görmek** gerekir — iki motion arasındaki delta toplanmalı. Ama normal motion'da sadece son pozisyon önemli, ara pozisyonlar kaybolabilir.

### 10.2 Held Key Table & Auto-Repeat

```cpp
struct knst_held_key {
    int32_t key_code = 0;
    int32_t scancode = 0;
    uint32_t last_key_time = 0;
    uint32_t last_repeat_time = 0;
    bool repeat_initialized = false;
    bool active = false;
    char utf8[8] = {0};     // UTF-8 char (tekrar TEXT_INPUT için)
    uint8_t utf8_len = 0;
};

knst_window_event::knst_held_key m_held_keys[KNST_MAX_HELD_KEYS]; // 8 slot
```

**Nasıl çalışır:**

1. `add_held_key()` — tuş basılınca ekle. `last_key_time = now`.
2. `non_block_pool_event()` her çağrıda `check_key_repeat()` çalışır.
3. `DELAY` (400ms) geçtiyse ilk repeat event'i fire edilir.
4. Sonraki her `INTERVAL` (30ms) tekrar fire edilir.
5. `remove_held_key()` — tuş bırakılınca kaldır.

**Neden kütüphane tarafından simüle?** Her OS farklı davranır:
- X11 `XkbSetDetectableAutoRepeat(True)` yapar ve kendi repeat'ini uygular.
- Wayland `keyboard.repeat_info` event'i rate/delay verir — ama sabit değil.
- Windows `WM_KEYDOWN` auto-repeat'i ayrı mesaj olarak gönderir ama modifier'larda garip davranır.
- Android `KeyEvent.getRepeatCount()` verir ama basit API.

**Çözüm:** Kütüphane bunları normalize eder. Kullanıcı sadece `PRESS` / `RELEASE` / `REPEAT` görür. `KNST_WINDOW_KEY_REPEAT_DELAY/INTERVAL` ile sabit tempo.

**UTF-8 cache neden?** Wayland'da `xkb_state_key_get_utf8()` repeat event'inde pahalı — tekrar tekrar çağırmak yerine ilk basışta cache'lenir.

**Modifier filtresi:** Modifier tuşları (Shift/Ctrl/Alt) repeat **etmez**. `is_modifier()` kontrolü Android'de `KNST_ANDROID_KEY_OFFSET` geri alır — çünkü offset'lenmiş kod ASCII ile karışmasın.

### 10.3 CSD "Probe Pattern"

**Sorun:** `knst_function` sınıfı yalnızca `void()` imzasını destekler (SSO buffer + std::function ağırlığından kaçınmak için). Ama `set_csd_hit_test()` callback'i şu imzayı taşır:

```cpp
knst_window::csd_hit cb(const knst_window& w, int x, int y);
```

**Çözüm — probe pattern:**

```cpp
// Üye değişkenler
knst_function m_csd_hit_invoke; // void() wrapper
int m_csd_probe_x = 0; // Giriş arg 1
int m_csd_probe_y = 0; // Giriş arg 2
csd_hit m_csd_probe_result; // Çıkış değeri

// set_csd_hit_test implementasyonu
template<typename F>
void set_csd_hit_test(F&& cb) noexcept {
    auto* self = this;
    m_csd_hit_invoke = [self, cb = std::forward<F>(cb)]() mutable {
        self->m_csd_probe_result = cb(
            static_cast<const knst_window&>(*self),
            self->m_csd_probe_x,
            self->m_csd_probe_y);
    };
    m_csd_enabled = true;
}

// hit_test_csd implementasyonu
csd_hit hit_test_csd(int x, int y) const noexcept {
    if (!m_csd_enabled || m_csd_hit_invoke.empty()) return csd_hit::normal;
    auto* self = const_cast<knst_window*>(this);
    self->m_csd_probe_x = x;
    self->m_csd_probe_y = y;
    self->m_csd_probe_result = csd_hit::normal;
    self->m_csd_hit_invoke();   // void() çalışır, sonucu m_csd_probe_result'a yazar
    return self->m_csd_probe_result;
}
```

**Neden const_cast?** `knst_function::operator()` const değil ama `hit_test_csd` const. Probe değişkenleri zaten "mutable scratchpad" gibi davranıyor — CSD callback tek thread'den çağrılır (event handler), race yok.

**Sonuç:** Kullanıcı normal lambda yazar, kütüphane `void()` imzasıyla sarar, sonucu probe değişkenlerinden okur.

### 10.4 Surface Client Pattern

```cpp
class knst_surface_client {
    virtual void on_surface_release() noexcept = 0;
    virtual bool on_surface_recreate() noexcept = 0;
    virtual void on_window_destroyed() noexcept {}
};
```

**Ne işe yarar:** OpenGL/Vulkan context'leri native surface'e bağlıdır. Android'de `TERM_WINDOW` gelince surface geçersiz olur — context'i değil, sadece surface'i bırakmak gerekir. Bu pattern, "surface geçersiz oldu" / "surface hazır" bildirimlerini `knst_window`'a bağımlı olmadan yapmayı sağlar.

**Nasıl çalışır:**

1. `gl.Init(&window)` çağrıldığında içeride `window.register_surface_client(this)` yapılır.
2. Android `TERM_WINDOW` event gelince → `notify_surface_release()` → her client'a `on_surface_release()` → `gl` EGL surface'i bırakır (ama context kalır).
3. `INIT_WINDOW` gelince → `notify_surface_recreate()` → `on_surface_recreate()` → `gl` yeni EGL surface yaratır.
4. Window destroy olunca → `notify_window_destroyed()` → `on_window_destroyed()` → `gl` tamamen Shutdown.

**Kritik:** Surface client `knst_window`'dan **önce** yok edilmeli. `gl.Shutdown()` → `window.destroy()` → `KnstWindowSources::CleanUp()`. Ters sıra → dangling pointer.

### 10.5 `present_pre()` / `present_post()` — Renderer-Agnostic Sync

```cpp
void call_redraw_callback() noexcept {
    if (m_in_redraw) return; // Re-entrancy guard
    m_in_redraw = true;
    present_pre(); // Wayland: ack_configure
    if (m_redraw_callback) m_redraw_callback();
    present_post(); // X11: _NET_WM_SYNC_REQUEST counter
    m_in_redraw = false;
}
```

**Neden iki ayrı hook?**

- **`present_pre`** — Wayland'da `xdg_surface.ack_configure` çağrılmalı. Buffer attach etmeden **önce**.
- **`present_post`** — X11'de `_NET_WM_SYNC_REQUEST` counter'ı arttırılmalı. Yeni boyut frame'i ekrana geldikten **sonra**.

Bunlar renderer-agnostic. OpenGL kullanıyorsan `SwapBuffers` içinde, Vulkan kullanıyorsan `vkQueuePresentKHR` etrafında. Kütüphane her ikisini de destekler.

**`m_in_redraw` guard:** Windows'ta `WM_WINDOWPOSCHANGED` bazen `restore()` veya `set_maximized()` çağırır — bunlar **senkron olarak** `call_redraw_callback()` çağırır. Guard olmasa infinite recursion → stack overflow.

### 10.6 Wayland Frame Callback Gating

```cpp
bool is_frame_ready() const noexcept {
    if (m_has_pending_configure) return true;  // Resize sırasında bypass
    if (m_frame_ready) return true;

    // Timeout: 500ms içinde callback gelmezse ready say
    if (m_frame_request_time != 0) {
        const uint32_t now = KnstWindowSources::get_current_time_ms();
        if (now - m_frame_request_time > 500) return true;
    }
    return false;
}
```

**Neden bu pattern?**

Wayland'da pencere gösterme süreci asenkron:

1. `wl_surface_commit()` → compositor'a "yeni buffer var" der
2. Compositor `frame` event'i gönderir (pencere görünür olduğunda)
3. `wl_callback.done` gelir → bir sonraki frame hazır

Eğer sen sürekli `eglSwapBuffers()` çağırırsan compositor yavaşsa `eglSwapBuffers()` **bloklanır**. Kötü compositor'da (özellikle **NVIDIA Wayland**'de) bu donmaya dönüşür → `ECONNRESET`.

**Çözüm:** Compositor hazır olana kadar **çizme**. Ama sonsuza kadar bekleme (buggy WM) — 500ms timeout koy, ready say.

**Resize sırasında bypass:** Resize sırasında `configure` event'i geliyor — bu zaten "compositor hazır" demek. Frame callback beklemek anlamsız.

### 10.7 `wl_output` User-Data Stabilitesi

```cpp
// Monitör listesi unique_ptr<knst_monitor> tutar
auto mon = std::make_unique<knst_monitor>();
knst_monitor* raw_ptr = mon.get();
knst_display::m_monitor_list.push_back(std::move(mon));

// Listener user-data olarak raw_ptr ver
wl_output_add_listener(raw_ptr->output, &listener, raw_ptr);
```

**Neden unique_ptr?**

Eğer `knst_monitor` doğrudan vector içinde tutulsa (`knst_vector<knst_monitor>`) ve vector resize olsa → `data()` adresi değişir → `raw_ptr` dangling olur → `geometry` event'i geldiğinde UAF.

`unique_ptr` heap allocation yapar → nesne adresi sabit kalır. Vektör büyüse de `raw_ptr` geçerli.

**Ek koruma:** `knst_display::refresh_screens_wayland()` içinde **sıralama yok**. Sıralama olsaydı vektördeki sıra değişirdi ama `unique_ptr` olduğu için nesne adresi sabit — yine de sorting gerekmez, compositor zaten doğru sırada gönderir.

### 10.8 Dual-Mode Makro Sistemi

```cpp
#if defined(KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND)
    #define KNST_X11_METHOD(name)      name##_x11
    #define KNST_WAYLAND_METHOD(name)  name##_wayland
    #define KNST_IS_DUAL_BACKEND 1
#else
    #define KNST_X11_METHOD(name)      name
    #define KNST_WAYLAND_METHOD(name)  name
    #define KNST_IS_DUAL_BACKEND 0
#endif
```

**Ne yapar?**

- **Single-mode X11:** `KNST_X11_METHOD(creation)` → `creation`. Header'da `void creation() noexcept;` gibi görünür.
- **Single-mode Wayland:** `KNST_WAYLAND_METHOD(creation)` → `creation`. Aynı.
- **Dual-mode:** `creation_x11()` ve `creation_wayland()` **iki ayrı fonksiyon** olur. `creation()` bunları runtime'da seçer.

```cpp
#if KNST_IS_DUAL_BACKEND
inline void knst_window::creation() noexcept {
    if (KnstWindowSources::s_active_backend == knst_backend::x11)
        return creation_x11();
    if (KnstWindowSources::s_active_backend == knst_backend::wayland)
        return creation_wayland();
}
#endif
```

**Avantaj:** Tek binary'de iki backend. Kullanıcı `KNST_WINDOW_FORCE_X11=1` veya `KNST_WINDOW_FORCE_WAYLAND=1` ile zorlayabilir. Aksi halde runtime algılama.

**Dezavantaj:** Binary boyutu ~2x (her iki backend'in kodu). Ama iki ayrı binary bakımından çok daha kolay.

### 10.9 Android JNI Bridge — Thread-Safe Event Queue

Android'de `commitText`, `deleteSurrounding` gibi IME callback'leri **UI thread'den** gelir. Ama render döngüsü `native_app_glue` **native thread'den** çalışır.

```cpp
namespace knst_android_jni {
    inline std::mutex s_queue_mutex;
    inline std::vector<knst_window_event> s_queue;
}

// UI thread (Java → JNI):
KNST_JNI_FUNC void JNICALL
Java_com_knst_window_KnstActivity_nativeCommitText(JNIEnv* env, jclass, jstring js) {
    knst_window_event ev;
    ev.begin(KNST_WINDOW_EVENT_TEXT_INPUT);
    // ... js → ev.text_input ...

    std::lock_guard<std::mutex> lk(s_queue_mutex);
    s_queue.push_back(std::move(ev));
}

// Native thread (render loop başı):
inline void drain(knst_window* w) {
    std::vector<knst_window_event> local;
    {
        std::lock_guard<std::mutex> lk(s_queue_mutex);
        if (s_queue.empty()) return;
        local.swap(s_queue);   // Kilit dışında dispatch için swap
    }
    for (auto& ev : local) {
        w->dispatch_event(ev);
    }
}
```

**Taktik:** **`swap`** — kilit altında sadece pointer swap, dispatch kilidin **dışında**. Bu sayede UI thread drain sırasında bile event push edebilir. Kilit contention minimal.

**`inline` + `__attribute__((used))`:** JNI fonksiyonları reflection ile çağrıldığı için linker dead code elimination'a takılabilir. `__attribute__((used))` bunu engeller.

### 10.10 UTF-16 Preservation

Android JNI'de iki API:

- **`GetStringUTFChars`** — **Modified UTF-8** (CESU-8) döner. Emoji (`U+1F600` = surrogate pair) **iki ayrı 3-byte sequence** olarak gelir → standart UTF-8 decode bozulur.
- **`GetStringChars`** — **UTF-16** döner. Doğru.

```cpp
// DOĞRU: UTF-16
const jchar* ch = env->GetStringChars(js, nullptr);
knst_window_event ev;
for (jsize i = 0; i < n; ++i) {
    ev.text_input += static_cast<char16_t>(ch[i]);
}
env->ReleaseStringChars(js, ch);

// YANLIŞ: CESU-8 (emoji bozulur)
const char* u8 = env->GetStringUTFChars(js, nullptr);
```

Aynı şekilde clipboard okuma:

```cpp
// NewStringUTF expects modified UTF-8 — emoji aborts under CheckJNI
jstring jText = env->NewString(
    reinterpret_cast<const jchar*>(text.data()),
    (jsize)text.length());
```

### 10.11 Fractional Delta Accumulators

```cpp
// Wayland: wl_fixed_t = 24.8 fixed point (256 alt bölme)
// X11: xcb_input_fp1616_t = 16.16 fixed point
// Windows: int delta

// Raw motion'da fractional truncate edilirse kaybolur
double m_raw_accum_x = 0.0;

// Her event:
m_raw_accum_x += wl_fixed_to_double(dx);
int32_t ix = (int32_t)m_raw_accum_x;
if (ix == 0) return;   // Henüz hareket tamamlanmadı
m_raw_accum_x -= (double)ix;

// Şimdi ix kullan (tam sayı delta)
```

**Neden?** `wl_fixed_to_int(0.4) = 0`. Yavaş fare hareketi (FPS'te precision aim) yarım piksellerle sürekli kaybolur. Accumulator ile **toplanır**, integer olduğunda emit edilir.

**Aynı pattern scroll için de geçerli:**

```cpp
// Windows WM_MOUSEWHEEL yüksek precision wheel'de 40 unit delta gönderebilir
// 120 unit = 1 notch (WHEEL_DELTA)
int m_wheel_accum = 0;

m_wheel_accum += raw_delta;
int notches = m_wheel_accum / 120;
if (notches == 0) return;   // Birikim devam
m_wheel_accum -= notches * 120;
```

### 10.12 DPI Caching (Per-Window)

```cpp
mutable float m_cached_dpi     = 0.0f;
mutable int   m_cached_title_h = 0;

int get_title_bar_height() const noexcept {
    // ...
    const knst_monitor* mon = knst_display::get_primary_monitor();
    if (m_cached_dpi == mon->dpi_scale && m_cached_title_h > 0) {
        return m_cached_title_h;   // Cache hit — pahalı hesabı atla
    }
    // ... hesap ...
}
```

**Neden `mutable`?** `get_title_bar_height()` `const` ama cache güncellemek istiyor. `mutable` bu izni verir.

**Neden per-window?** Çoklu monitörde her pencere farklı monitörde olabilir. Global cache yanlış değer döner. Ama pratikte DPI değişimi nadir — cache hit oranı yüksek.

### 10.13 Backend Detection Algoritması

```cpp
knst_backend KnstWindowSources::detect_backend() noexcept {
    // 1. Env override
    if (getenv("KNST_WINDOW_FORCE_X11"))       return knst_backend::x11;
    if (getenv("KNST_WINDOW_FORCE_WAYLAND"))   return knst_backend::wayland;

    // 2. Wayland probe
    if (WAYLAND_DISPLAY || XDG_SESSION_TYPE=wayland) {
        wl_display* probe = wl_display_connect(nullptr);
        if (probe) {
            // wl_compositor + xdg_wm_base var mı?
            // yoksa disconnect + fallback
        }
    }

    // 3. X11 probe
    if (DISPLAY set) {
        Display* d = XOpenDisplay(nullptr);
        if (d) { XCloseDisplay(d); return knst_backend::x11; }
    }

    // 4. Son şans: X11 default connect
    Display* d = XOpenDisplay(nullptr);
    if (d) { XCloseDisplay(d); return knst_backend::x11; }

    return knst_backend::none;
}
```

**Taktik:** Wayland probe başarısız olursa (compositor açık değilse) XWayland'e düşer. Bu, Wayland oturumunda eski uygulamalarla uyumluluk sağlar.

### 10.14 Platform Parity — No-Op Approach

```cpp
// Windows'ta primary selection yok
inline void knst_window::set_primary_selection(const knst_c16string&) noexcept {
    // no-op
}

// Android'de CSD hit-test yok
inline void knst_window::csd_handle_hit(csd_hit, int, int, uint32_t) noexcept {
    // no-op
}
```

**Neden?** Kullanıcı kodunda `#ifdef` istemiyoruz:

```cpp
// KÖTÜ:
#if defined(KNST_USING_LINUX_PLATFORM_X11)
    window.set_primary_selection(u"Merhaba");
#endif

// İYİ:
window.set_primary_selection(u"Merhaba");   // Windows'ta no-op, crash yok
```

**`capabilities()`** ile runtime kontrol daha doğru:

```cpp
auto caps = knst_window::capabilities();
if (caps.primary_selection) {
    window.set_primary_selection(u"Merhaba");
}
```

### 10.15 Cleanup Sırası — Kritik

**Wayland proxy hiyerarşisi:**

```
wl_display
  └── wl_registry
        ├── wl_compositor
        │     └── wl_surface (per-window)
        │           └── xdg_surface (per-window)
        │                 └── xdg_toplevel (per-window)
        ├── wl_seat
        │     ├── wl_pointer
        │     └── wl_keyboard
        ├── wl_shm
        ├── wl_output (per-monitor)
        └── ... (diğer manager'lar)
```

**Kural:** Child önce, parent sonra. `wl_surface` destroy etmeden `wl_compositor` destroy edersen → parent link dangling → **segfault**.

```cpp
// Doğru sıra:
1. Per-window (s_windows listesindeki her pencere destroy())
2. Registry'den bind edilmiş singleton'lar (cursor_theme, wl_pointer, vb.)
3. Registry-bound manager'lar (compositor, shm, seat)
4. wl_registry
5. wl_display
```

**Android looper pump:** `CleanUp()` içinde `destroyRequested` gelene kadar looper pump edilir. Aksi halde UI thread `onPause` içinde `pthread_cond_wait`'te kilitlenir → ANR.

### 10.16 Windows OLE Refcount

Windows'ta `OleInitialize` process başına refcounted. Her `OleInitialize` bir `OleUninitialize` ile eşleşmeli.

```cpp
namespace knst_ole_detail {
    inline int& ole_refcount() noexcept {
        static int s = 0;
        return s;
    }
    inline void add_ref() noexcept {
        if (ole_refcount()++ == 0) OleInitialize(nullptr);
    }
    inline void release_ref() noexcept {
        if (--ole_refcount() == 0) OleUninitialize();
    }
}
```

**Neden?** Her pencere drag-drop aktif ettiğinde `OleInitialize` çağırsaydık → çoklu pencere → aşırı çağrı. Refcount çözer.

### 10.17 X11 Cursor Cache

`XCreateFontCursor` ve `XcursorLibraryLoadCursor` **pahalı** (tema dosyalarını okur). Her `set_cursor()` çağrısında yeni handle yaratmak yerine cache'le:

```cpp
namespace knst_x11_cursor_cache {
    struct entry { Display* dpy; unsigned int shape; char name[64]; Cursor cur; };
    inline knst_vector<entry>& store() { static knst_vector<entry> s; return s; }

    inline Cursor get_font(Display* dpy, unsigned int shape) noexcept {
        for (auto& e : store()) {
            if (e.dpy == dpy && e.shape == shape && e.name[0] == '\0') return e.cur;
        }
        // miss → yarat + cache'le
    }
    // get_named benzer
}
```

**Critical:** `clear_all()` **`XCloseDisplay`'den önce** çağrılmalı. Aksi halde cached Cursor handle'lar dangling → UB.

### 10.18 Wayland Clipboard Self-Read Deadlock

**Senaryo:** Sen clipboard'ın sahibisin (`set_clipboard` çağırdın). `request_clipboard()` çağırırsan Wayland protokolünde **kendi `wl_data_source.send()`'ini tetiklersin**. Aynı thread'de `write()` → pipe → kendi `read()` → **deadlock**.

**Çözüm:**

```cpp
void request_clipboard() noexcept {
    if (!m_clipboard_owned.empty()) {
        // Biz sahibiz → kendi copy'imizi döndür, protokole gitme
        clipboard_text = m_clipboard_owned;
        m_knst_event.begin(KNST_WINDOW_EVENT_CLIPBOARD_DATA);
        dispatch_current_event();
        return;
    }
    // Normal yol
}
```

Aynı pattern primary selection için de geçerli.

### 10.19 Wayland `wl_data_offer_finish` Kuralı

`DataDeviceDrop` handler'ında **`accept(serial, nullptr)`** çağrıldıysa (drag reddedildi), `finish` çağırmak **protokol hatası**dır (`invalid_finish`). Compositor seni disconnect eder.

```cpp
if (!has_uri_list) {
    // accept(serial, nullptr) yapıldıysa finish YASAK
    wl_data_offer_destroy(local_offer);
    return;
}
// Sadece uri-list kabul edildiyse finish OK
wl_data_offer_finish(local_offer);
```

### 10.20 Wayland Event Dispatch — `prepare_read` Pattern

```cpp
// Her frame başında:
if (wl_display_prepare_read(d) != 0) {
    // Başka thread okuyor — pending dispatch et
    wl_display_dispatch_pending(d);
    return;
}
// ...
if (wl_display_flush(d) == -1 && errno != EAGAIN) {
    wl_display_cancel_read(d);
    return;
}
// poll
if (ret > 0 && (pfd.revents & POLLIN)) {
    wl_display_read_events(d);
    wl_display_dispatch_pending(d);
} else {
    wl_display_cancel_read(d);
}
```

**Kritik:** `POLLHUP` / `POLLERR` kontrolü `POLLIN` kontrolünün **dışında** olmalı. İçerideyse unreachable kod — disconnect algılanmaz.

### 10.21 X11 Scroll Delta İşareti

Wayland `axis` event'inde **pozitif = aşağı**, X11'de **pozitif = yukarı**, Windows'ta `WHEEL_DELTA` **pozitif = yukarı**. Kütüphane hepsini `+1 = yukarı` normalize eder.

**Taktik:** Wayland'da `-value / 10.0` kullanırız. 10.0 çünkü Wayland `~10.0 = 1 notch`.

### 10.22 X11 NumLock Özel Durumu

NumLock açıkken X11 keypad tuşları `XK_KP_End` gibi navigation symbols gönderir, `XK_KP_1` değil. Kütüphane bunu `XK_KP_1`'e dönüştürür.

```cpp
if (num_lock) {
    switch (keysym) {
        case XK_KP_End:    keysym = XK_KP_1; break;
        case XK_KP_Down:   keysym = XK_KP_2; break;
        case XK_KP_Next:   keysym = XK_KP_3; break;
        // ... diğerleri
    }
}
```

**Ekstra:** Shift+CapsLock birlikte basıldığında harf **küçük** olur (`XOR` mantığı). Rakam/noktalama için Shift direkt etkili (CapsLock etkisiz).

```cpp
bool is_letter = (base_sym >= XK_a && base_sym <= XK_z) ||
                 (base_sym >= XK_A && base_sym <= XK_Z);
bool shifted = is_letter ? (shift_down != caps_lock) : shift_down;
```

### 10.23 Windows ShowCursor Counter

`ShowCursor()` bir **counter** üzerinde çalışır, boolean değil. Her `ShowCursor(TRUE)` +1, her `ShowCursor(FALSE)` -1. Counter negatif olduğunda cursor gizli.

**Problem:** İdempotent değil. İki kez `ShowCursor(FALSE)` → counter -2 → iki kez `ShowCursor(TRUE)` gerekir.

**Çözüm:**

```cpp
bool m_cursor_visible = true;   // Gerçek durum

if (!m_cursor_visible) {
    while (ShowCursor(TRUE) < 0) {}   // 0'a kadar arttır
    m_cursor_visible = true;
}
```

### 10.24 Android `drain()` No-Op Pattern

PC'de Android'e özel bir şey yok. `drain()` gibi fonksiyonlar PC'de **no-op shim**:

```cpp
#if !defined(KNST_USING_PLATFORM_ANDROID)
namespace knst_android_jni {
    inline void drain(knst_window*) noexcept {}
    inline bool input_connection_active() noexcept { return false; }
}
#endif
```

Kullanıcı `#ifdef` yazmadan çağırabilir:

```cpp
void on_frame(knst_window& w, void*) {
    knst_android_jni::drain(&w);   // Android'de IME event'lerini dispatch, PC'de no-op
    // ...
}
```

### 10.25 Android `KeyEvent.getUnicodeChar` via JNI

Android `AKeyEvent` doğrudan karakter vermez — sadece keycode. Karakter için `android.view.KeyEvent.getUnicodeChar()` çağrılmalı. Kütüphane JNI üzerinden bunu yapar.

```cpp
// JNI'den Java KeyEvent yarat
jclass cls = env->FindClass("android/view/KeyEvent");
jmethodID ctor = env->GetMethodID(cls, "<init>", "(II)V");
jmethodID getU = env->GetMethodID(cls, "getUnicodeChar", "(I)I");
jobject ev = env->NewObject(cls, ctor, (jint)0, (jint)keyCode);
int32_t result = env->CallIntMethod(ev, getU, (jint)metaState);
```

Bu şekilde **Shift+letter, punctuation, Türkçe karakterler, dead keys** doğru çözülür.

**Taktik:** `InputConnection` aktifse (yumuşak klavye açık) `getUnicodeChar` **atlanır** — `commitText` zaten karakteri gönderecek. Aksi halde çift karakter.

### 10.26 Android `AKEYCODE` — ASCII Çakışması

`AKEYCODE_ENTER = 66 = 'B'`, `AKEYCODE_DEL = 67 = 'C'`, `AKEYCODE_ESCAPE = 111 = 'o'`... düzinelerce çakışma.

**Çözüm:**
- `A-Z` → ASCII (`'A'` - `'Z'`)
- `0-9` → ASCII (`'0'` - `'9'`)
- **Diğer tümü** → `AKEYCODE_X + 0x10000`

```cpp
if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
    keyCode = 'A' + (keyCode - AKEYCODE_A);
} else if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
    keyCode = '0' + (keyCode - AKEYCODE_0);
} else {
    keyCode = keyCode + KNST_ANDROID_KEY_OFFSET;  // 0x10000
}
```

`KNST_WINDOW_KEY_CODE_*` macro'ları her platformda doğru değeri verir.

### 10.27 Multi-Touch Snapshot Pattern

**Wayland:** `wl_touch` event'leri parça parça gelir (`down`, `motion`, `up`), sonra `frame` gelir. Tek bir event'te **tüm aktif noktaların snapshot'ı** verilir.

```cpp
// Her down/motion/up state'i güncelle
window->m_active_touches[slot] = {...};

// frame gelince tüm state'i tek event'e paketle
ev.begin(KNST_WINDOW_EVENT_TOUCH);
int idx = 0;
for (int i = 0; i < KNST_MAX_TOUCH_POINTS; i++) {
    if (window->m_active_touches[i].id >= 0) {
        ev.pointer_id[idx] = ...;
        ev.pointer_x[idx]  = ...;
        ev.pointer_y[idx]  = ...;
        ++idx;
    }
}
ev.pointer_count = idx;
```

**X11:** `XI_TouchBegin/Update/End` event'leri tek tek gelir ama kütüphane **snapshot** yapar — her event'te tüm aktif noktaları bildirir. Pinch/zoom çalışır çünkü uygulama iki noktayı aynı event'te görür.

**Android:** `AMotionEvent` **zaten** tüm pointer'ları taşır. Ama `ACTION_POINTER_UP` sırasında **kalkan parmak hâlâ snapshot'ta** olur → ghost parmak. Kütüphane bunu `ACTION_POINTER_INDEX_MASK` ile bulup atlar.

### 10.28 UTF-16 String COW (Copy-On-Write)

`knst_c16string` copy-on-write kullanır. `s1 = s2` çağrısı veri kopyalamaz, sadece refcount artar. **Yazma** olduğunda kopya yaratılır.

**Avantaj:** Event'lerde `text_input` copy etmek ucuz.

**Dezavantaj:** Thread-safety. `KNST_C16_STRING_USING_ATOMIC_COW` ile atomic refcount açılır ama yavaşlar.

**Default:** Non-atomic COW. Tek thread'de hızlı, çoklu thread'de **dikkat**.

### 10.29 X11 `_NET_WM_SYNC_REQUEST` Akışı

```
1. Window oluşturulurken WM'ye "WM_PROTOCOLS = [WM_DELETE_WINDOW, _NET_WM_SYNC_REQUEST]" yaz
2. WM'ye _NET_WM_SYNC_REQUEST_COUNTER property'si ile bir xcb_sync_counter yaz
3. Kullanıcı pencereyi resize eder:
   a. WM _NET_WM_SYNC_REQUEST counter'ı bizim penceremize gönderir (ClientMessage)
   b. Biz yeni boyut frame'i çizeriz
   c. Çizim bittikten sonra xcb_sync_set_counter(counter, pending_value) çağırırız
   d. WM yeni boyutu ancak bu counter ack'inden sonra gösterir
```

**Sonuç:** Titremesiz resize. Counter ack olmadan WM eski frame'i gösterir.

**Taktik:** `present_post()` içinde counter set edilir. `m_syncConfigured` veya 100ms timeout ile. 100ms çünkü WM sonsuza kadar bekleyemez.

### 10.30 Wayland Fractional Scale & Viewporter

`wp_fractional_scale_v1` **tek başına yeterli değil**. Yanında `wp_viewporter` gerekli. Sebep: fractional scale sadece "compositor tercih ettiği scale" bildirir. Piksel seviyesinde uygulama için viewport'a ihtiyaç var.

```cpp
// 1. fractional_scale manager'dan fractional_scale_v1 al
m_fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(manager, m_surface);
wp_fractional_scale_v1_add_listener(m_fractional_scale, &listener, this);

// 2. viewporter'dan viewport al
m_viewport = wp_viewporter_get_viewport(viewporter, m_surface);

// 3. compositor preferred_scale bildirdiğinde (120 = 1.0, 180 = 1.5):
void FractionalScalePreferred(...) {
    m_fractional_scale_120 = scale;
    // Viewport destination = logical size
    wp_viewport_set_destination(m_viewport, logical_w, logical_h);
}

// 4. EGL window'u FİZİKSEL boyutla resize et:
wl_egl_window_resize(egl_win,
    wayland_physical_width(logical_w),   // = (logical_w * 180 + 60) / 120
    wayland_physical_height(logical_h),
    0, 0);
```

**Rounding:** `(logical * scale + 60) / 120` — 120'nin yarısı 60, yarım-yukarı yuvarlama için. Protokol bunu öneriyor.

### 10.31 Android `native_app_glue` Deadlock

`native_app_glue`'da UI thread lifecycle senkronizasyonu:

```
UI thread:  android_app_set_activity_state(...)
                ↓
              pthread_cond_wait     ← BLOCKED
                ↓
Native:     ALooper_pollOnce → APP_CMD_PAUSE
                ↓
            android_app_set_activity_state done
                ↓
            pthread_cond_signal   ← UI uyanır
```

**Sorun:** `android_main`'den erken dönersen native thread looper poll'u durdurur → UI thread `onPause` içinde **sonsuza kadar** bekler → **ANR** → telefon donar.

**Çözüm — `CleanUp()` içinde:**

```cpp
if (m_app != nullptr) {
    if (m_app->destroyRequested == 0 && m_app->activity != nullptr) {
        ANativeActivity_finish(m_app->activity);   // Async, sadece mesaj post eder
    }

    // destroyRequested gelene kadar looper pump et
    const uint32_t t0 = get_current_time_ms();
    while (m_app->destroyRequested == 0) {
        int events;
        struct android_poll_source* source = nullptr;
        const int r = ALooper_pollOnce(100, nullptr, &events, (void**)&source);
        if (r >= 0 && source != nullptr) {
            source->process(m_app, source);
        }
        if (get_current_time_ms() - t0 > 10000) break;  // 10s timeout
    }
}
```

**10s timeout** — yavaş cihazlar + transition animasyonları için. Önceden 3s çok kısaydı.

### 10.32 `m_destroyed` Double-Destroy Guard

Kullanıcı `window.destroy()` çağırırsa açık, sonra `~knst_window()` yine `destroy()` çağırır. İki kez `notify_window_destroyed()` çağrılırsa surface client'lar iki kez `on_window_destroyed()` alır.

**Çözüm:**

```cpp
inline void knst_window::destroy() noexcept {
    if (m_destroyed) return;
    m_destroyed = true;
    // ... gerçek destroy ...
}
```

**Kritik:** `creation()` içinde `m_destroyed = false` sıfırlanır — yeniden kullanım için.

---

## 11. Yapılandırma (`knst_settings.hpp`)

Kütüphane derlemeden önce `include/knst_settings.hpp` içindeki **kullanıcı makroları** ile özelleştirilebilir.

### 11.1 Platform Seçimi

```cpp
// Bunlar genelde CMake tarafından otomatik set edilir
// #define KNST_USING_PLATFORM_WINDOWS
// #define KNST_USING_PLATFORM_LINUX
// #define KNST_USING_PLATFORM_ANDROID
// #define KNST_USING_LINUX_PLATFORM_X11
// #define KNST_USING_LINUX_PLATFORM_WAYLAND
// #define KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND
```

### 11.2 GPU Backend'leri

```cpp
// #define KNST_WINDOW_USE_OPENGL
// #define KNST_WINDOW_USE_VULKAN
```

### 11.3 String

```cpp
// #define KNST_C16STRING_DEACTIVE_COW       // COW kapalı (güvenli, yavaş)
// #define KNST_C16_STRING_USING_ATOMIC_COW  // Atomic COW (thread-safe)
// #define KNST_C16STRING_ALIGN_64           // SSO 30 char, alignas(64)
// #define KNST_C16STRING_ALIGN_32           // SSO 14 char, alignas(32)
```

### 11.4 Function

```cpp
// #define KNST_FUNCTION_INLINE_SIZE 64      // SSO buffer boyutu (byte)
```

### 11.5 Memory Pool

```cpp
// #define KNST_MEMORY_POOL_USE_MUTEX        // Pool thread-safe (mutex)
// #define KNST_SMALL_SIZE_CLASS             // Force inline yerine inline
```

### 11.6 Thread Pool

```cpp
// #define KNST_OVERFLOW_MAX 32              // Kuyruk dolarsa max thread
// #define KNST_QUEUE_MAX 1024               // Kuyruk kapasitesi
// #define KNST_DEFAULT_WORKER_COUNT 0       // 0 = CPU core sayısı
// #define KNST_WORKER_BATCH_SIZE 8          // Worker başına iş batch'i
```

### 11.7 Image Loader

```cpp
// #define KNST_IMAGE_MAX_DIMENSION 16384    // Max genişlik/yükseklik
// #define KNST_IMAGE_MAX_CUSTOM_DECODERS 16 // Max custom decoder
```

### 11.8 Window

```cpp
// #define KNST_WINDOW_KEY_REPEAT_DELAY 200      // İlk tekrar (ms)
// #define KNST_WINDOW_KEY_REPEAT_INTERVAL 20    // Tekrar aralığı (ms)
// #define KNST_DISABLE_TITLE_BAR                // Custom title bar
// #define KNST_MAX_HELD_KEYS 8                  // Max basılı tuş
```

**`KNST_WINDOW_KEY_REPEAT_DELAY/INTERVAL`:**

- Varsayılan `400/30` → masaüstü hissi
- Hızlı `250/16` → FPS, aksiyon oyunları
- Yavaş `500/50` → metin editörü

### 11.9 Touch

```cpp
// #define KNST_WINDOW_ENABLE_TOUCH_SCREEN
// #define KNST_MAX_TOUCH_POINTS 10
```

**`KNST_WINDOW_ENABLE_TOUCH_SCREEN`:** Touch kodunu derler.

- Windows: `WM_TOUCH` handler + `RegisterTouchWindow`
- X11: XInput2 touch event seçimi
- Wayland: `m_touch_supported = true`
- **Android:** Her zaman açık (macro etkisiz)

**Tanımlanmazsa:** Windows/X11'de touch kodu **derlenmez**. Binary küçülür. Wayland'de listener kayıtlı ama `m_touch_enabled = false`.

---

## 12. Kalıplar ve Örnekler

### 12.1 Oyun Döngüsü

```cpp
struct Game {
    float px = 400, py = 300;
    bool keys[4] = {};

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
                    case KNST_WINDOW_KEY_CODE_ESCAPE: w.should_close(); break;
                }
            }
        }
        w.clear_events();
    }

    void update(float dt) {
        float speed = 300.0f;
        if (keys[0]) py -= speed * dt;
        if (keys[2]) py += speed * dt;
        if (keys[1]) px -= speed * dt;
        if (keys[3]) px += speed * dt;
    }
};
```

### 12.2 Editor (Sürükle-Bırak)

```cpp
struct Editor {
    bool dragging = false;
    int drag_dx = 0, drag_dy = 0;
    int box_x = 100, box_y = 100, box_w = 200, box_h = 100;

    void on_event(const knst_window_event& ev) {
        if (ev.type == KNST_WINDOW_EVENT_MOUSE) {
            if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS &&
                ev.mouse_button == KNST_WINDOW_MOUSE_BUTTON_LEFT) {
                if (ev.mouse_x >= box_x && ev.mouse_x < box_x + box_w &&
                    ev.mouse_y >= box_y && ev.mouse_y < box_y + box_h) {
                    dragging = true;
                    drag_dx = ev.mouse_x - box_x;
                    drag_dy = ev.mouse_y - box_y;
                }
            } else if (ev.mouse_action == KNST_WINDOW_MOUSE_ACTION_RELEASE) {
                dragging = false;
            }
        } else if (ev.type == KNST_WINDOW_EVENT_MOTION) {
            if (dragging) {
                box_x = ev.mouse_x - drag_dx;
                box_y = ev.mouse_y - drag_dy;
            }
        }
    }
};
```

### 12.3 Çoklu Pencere

```cpp
knst_window editor(800, 600, u"Editor");
knst_window preview(400, 300, u"Preview");

editor.set_redraw_callback([](knst_window& w, void*) { /* editor */ });
preview.set_redraw_callback([](knst_window& w, void*) { /* preview */ });

editor.creation_and_show();
preview.creation_and_show();

while (!editor.is_should_close() || !preview.is_should_close()) {
    knst_window_event_system::non_block_pool_event();

    if (!editor.is_should_close())  editor.call_redraw_callback();
    if (!preview.is_should_close()) preview.call_redraw_callback();
}
```

### 12.4 Kısayol Kontrolü

```cpp
if (ev.type == KNST_WINDOW_EVENT_KEYBOARD &&
    ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS) {

    const bool ctrl  = (ev.mods & KNST_WINDOW_MOD_CONTROL) != 0;
    const bool shift = (ev.mods & KNST_WINDOW_MOD_SHIFT)   != 0;

    if (ctrl && !shift && ev.key_code == KNST_WINDOW_KEY_CODE_S) save();
    else if (ctrl && shift && ev.key_code == KNST_WINDOW_KEY_CODE_S) save_as();
    else if (ctrl && ev.key_code == KNST_WINDOW_KEY_CODE_Z) undo();
    else if (ctrl && ev.key_code == KNST_WINDOW_KEY_CODE_Y) redo();
}
```

### 12.5 CSD ile Özel Başlık Çubuğu

```cpp
constexpr int TITLE_H = 40;
constexpr int BTN_W   = 46;

window.set_title_bar_height(TITLE_H);

window.set_csd_hit_test([](const knst_window& w, int x, int y) {
    using CH = knst_window::csd_hit;
    const int W = w.get_width();
    const int H = w.get_height();

    constexpr int B = 12;
    if (y < B && x < B) return CH::resize_top_left;
    if (y < B && x >= W-B) return CH::resize_top_right;
    if (y >= H-B && x < B) return CH::resize_bottom_left;
    if (y >= H-B && x >= W-B) return CH::resize_bottom_right;
    if (y < B) return CH::resize_top;
    if (y >= H-B) return CH::resize_bottom;
    if (x < B) return CH::resize_left;
    if (x >= W-B) return CH::resize_right;

    if (y < TITLE_H) {
        if (x >= W - BTN_W*3) return CH::normal;
        return CH::draggable;
    }

    return CH::normal;
});
```

### 12.6 Wayland Frame Gating + Vulkan

```cpp
while (!window.is_should_close()) {
    knst_window_event_system::non_block_pool_event();
    window.call_redraw_callback();

    if (!window.is_frame_ready()) continue;
    if (!vk.HasSurface())        continue;
    if (window.is_gl_context_lost()) continue;

    window.request_frame_callback();
    // ... vkAcquireNextImageKHR, draw, present ...
}
```

### 12.7 Panoyu Bekleme (Wayland)

```cpp
enum class ClipState { Idle, Requesting };
ClipState clip_state = ClipState::Idle;

void on_frame(knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);

        if (ev.type == KNST_WINDOW_EVENT_KEYBOARD &&
            ev.key_code == KNST_WINDOW_KEY_CODE_V &&
            ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS &&
            (ev.mods & KNST_WINDOW_MOD_CONTROL)) {

            w.request_clipboard();
            clip_state = ClipState::Requesting;
        }
        else if (ev.type == KNST_WINDOW_EVENT_CLIPBOARD_DATA) {
            if (clip_state == ClipState::Requesting) {
                clip_state = ClipState::Idle;
                std::wcout << L"Panodan: " << w.get_clipboard().data() << L"\n";
            }
        }
    }
    w.clear_events();
}
```

### 12.8 Yüksek DPI / Scale

```cpp
window.set_redraw_callback([](knst_window& w, void*) {
    int logical_w = w.get_width();
    int logical_h = w.get_height();
    int fb_w = logical_w, fb_h = logical_h;

#if KNST_USING_LINUX_PLATFORM_WAYLAND
    fb_w = w.wayland_physical_width(logical_w);
    fb_h = w.wayland_physical_height(logical_h);
#endif

    glViewport(0, 0, fb_w, fb_h);
    draw_ui(logical_w, logical_h);
});
```

### 12.9 FPS Kamera (Raw Mouse)

```cpp
window.set_raw_mouse_mode(true);
window.set_cursor_mode(KNST_WINDOW_CURSOR_DISABLED);

float yaw = 0, pitch = 0;
constexpr float SENSITIVITY = 0.1f;

window.set_redraw_callback([&](knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);
        if (ev.type == KNST_WINDOW_EVENT_MOTION && ev.is_raw) {
            yaw   += ev.raw_delta_x * SENSITIVITY;
            pitch -= ev.raw_delta_y * SENSITIVITY;
            if (pitch >  89.0f) pitch =  89.0f;
            if (pitch < -89.0f) pitch = -89.0f;
        }
    }
    w.clear_events();
});
```

### 12.10 Touch Screen Multi-Finger

```cpp
window.enable_touch_screen(true);

struct TouchDot {
    int   id;
    float x, y;
    float hue_r, hue_g, hue_b;
};
TouchDot touches[KNST_MAX_TOUCH_POINTS];
int touch_count = 0;

window.set_redraw_callback([&](knst_window& w, void*) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const auto& ev = w.get_window_event_handle(i);
        if (ev.type == KNST_WINDOW_EVENT_TOUCH) {
            touch_count = ev.pointer_count;
            for (int j = 0; j < touch_count; ++j) {
                touches[j].id = ev.pointer_id[j];
                touches[j].x  = ev.pointer_x[j];
                touches[j].y  = ev.pointer_y[j];
                // Renk: id hash
            }
        }
    }
    w.clear_events();
});
```

### 12.11 Android IME Bridge

```cpp
void android_main(struct android_app* app) {
    KnstWindowSources::Init(app);

    knst_window window;
    window.set_redraw_callback([](knst_window& w, void*) {
        knst_android_jni::drain(&w);   // IME event'lerini dispatch et

        for (size_t i = 0; i < w.event_count(); ++i) {
            const auto& ev = w.get_window_event_handle(i);

            if (ev.type == KNST_WINDOW_EVENT_TEXT_INPUT && ev.text_input.length() > 0) {
                // Yazılan metin — emoji, Türkçe, CJK hepsi doğru
                g_text += ev.text_input;
            }
            else if (ev.type == KNST_WINDOW_EVENT_TEXT_PREEDIT) {
                // IME preedit (henüz commit edilmedi)
                g_preedit = ev.text_preedit;
            }
        }
        w.clear_events();
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

---

## 13. SSS ve Tuzaklar

### 13.1 Pencere Açılıyor Ama Boş

**Wayland:** Çizim yapmadın. Her frame'de `wl_surface_attach` + `commit` gerekli.

**Android:** `app->window == nullptr` iken çizim denedin. Önce `INIT_WINDOW` event'ini bekle.

**Diğer:** `set_redraw_callback` çağrılmamış olabilir.

### 13.2 Aynı Olay Tekrar İşleniyor

`clear_events()` çağrılmamış. Her frame sonunda **zorunlu**.

### 13.3 Klavye Tekrarları Gelmiyor

`PRESS` dinliyorsun ama `REPEAT`'i dinlemiyorsun:

```cpp
if (ev.key_action == KNST_WINDOW_KEY_ACTION_PRESS ||
    ev.key_action == KNST_WINDOW_KEY_ACTION_REPEAT) { /* ... */ }
```

### 13.4 Pano Boş (Wayland)

Asenkron. `request_clipboard()` bir sonraki frame'de hazır olur. `CLIPBOARD_DATA` event'ini bekle.

### 13.5 `move()` İşe Yaramıyor

**Wayland:** Compositor izin vermez — beklenen.
**X11/Windows:** WM kısıtlamaları (GNOME kullanıcı etkileşimi olmadan taşımaz).

### 13.6 `set_opacity()` Android/Wayland

Android: Yok sayılır.
Wayland: `wp_alpha_modifier_v1` yok → sadece opaque region değişir, gerçek saydamlık yok.

### 13.7 `KNST_WINDOW_EVENT_MOUSE` vs `KEY_CODE`

Union! `type` alanına göre doğru üyeyi oku:

```cpp
if (ev.type == KNST_WINDOW_EVENT_MOUSE) {
    // ev.mouse_button, ev.mouse_action ✓
    // ev.key_code ✗ (aynı bellek)
}
```

### 13.8 IME Çift Karakter (Android)

Kütüphane `s_input_connection_active` flag'i ile otomatik çözer. Ama kendi `commitText`'ini yazarsan ikiye katlarsın.

### 13.9 Wayland'da `block_pool_event()`

Kullanma. Compositor frame bekler, sen block'larsan pencere donar. **Her zaman `non_block_pool_event()`.**

### 13.10 `g_ui_scale = 1.0` Android

`APP_CONFIG_CHANGED` event'i başlamadan önce geliyor. `creation()` içinde manuel emit zaten yapılıyor — `event.density` alanını oku.

### 13.11 CSD'de Sürükleme Çalışmıyor

`set_csd_hit_test` callback `draggable` döndürmüyor olabilir. Kontrol et:

```cpp
if (y < TITLE_H) return knst_window::csd_hit::draggable;  // ✓
```

### 13.12 Multi-Touch Sadece 1 Nokta

`pointer_count` yerine `ev.mouse_x` kullanıyorsun. Touch için:

```cpp
if (ev.type == KNST_WINDOW_EVENT_TOUCH) {
    for (int i = 0; i < ev.pointer_count; ++i) {
        draw_circle(ev.pointer_x[i], ev.pointer_y[i]);
    }
}
```

### 13.13 `is_should_close()` Android'de Otomatik True

Beklenen davranış. Kullanıcı Recents'ten kapattığında `app->destroyRequested != 0` olur. Sen `should_close()` çağırmasan bile `true` döner.

### 13.14 `static` Pencere

```cpp
static knst_window w;  // ✗ YANLIŞ — destructor sırasında platform CleanUp çoktan çalışmış olabilir
```

Global/static yerine stack veya `unique_ptr` kullan.

### 13.15 Klavye Modifier'ı `ev.mods`'da Yok

Wayland'de `modifiers` event'i keydown'dan önce gelebilir (race). Kütüphane bunu halleder ama `ev.mods` boşsa `is_caps_lock_on()` gibi runtime sorgulara güven.

### 13.16 X11'de Klavye Tekrarı

Kütüphane `XkbSetDetectableAutoRepeat(True)` yapar ve kendi repeat'ini uygular. `key_action == REPEAT` senaryosunu işle.

### 13.17 Android `AConfiguration` NULL

`Init()` hemen `app->config` erişir. Çok nadir durumda `NULL` olabilir — kütüphane guard koyar.

### 13.18 Wayland HiDPI Yanlış Boyut

`wl_egl_window_resize` **fiziksel** boyutu alır. `wayland_physical_width(logical)` kullan:

```cpp
wl_egl_window_resize(eglWin,
    window.wayland_physical_width(logical_w),
    window.wayland_physical_height(logical_h),
    0, 0);
```

### 13.19 `drop_files` Event Bittiğinde Kayboluyor

`begin()` her event'te `drop_files.reset()` yapar. Sadece `FILE_DROP` event'inde dolu olur — kopyala:

```cpp
if (ev.type == KNST_WINDOW_EVENT_FILE_DROP) {
    auto files = ev.drop_files;   // shared_ptr kopyala
    // Sonra işle
}
```

### 13.20 `ev.text_input` UTF-16

`std::cout << ev.text_input` çalışır (operator<< var, UTF-8'e çevirir). Ama `printf("%s")` çalışmaz.

### 13.21 SSS — Genel

**S: `set_clipboard()` Android'de çalışıyor mu?**
Evet, JNI üzerinden `ClipboardManager`'a yazar. `get_clipboard()` de çalışır.

**S: Kütüphane `set_opacity` Wayland'da çalışıyor mu?**
Hayır. `wp_alpha_modifier_v1` yok. Sadece opaque region değişir. Kendi fragment shader'ında premultiplied alpha kullan.

**S: `window.resize()` neden hemen etki etmiyor?**
Wayland/X11 asenkron. `CONFIGURE_NOTIFY` gelene kadar eski boyut döner.

**S: `should_close()` sonrası callback çağrılır mı?**
Evet, o frame'in callback'i tamamlanır. Sonraki iterasyonda döngü çıkar.

**S: `clear_events()` sonrası `event_count()` ne olur?**
0. Ama aynı frame'de yeni event gelirse `event_count()` artar.

**S: Dual-mode'da hangi backend aktif?**
`KnstWindowSources::active_backend()` → `x11` / `wayland` / `windows` / `none`.

**S: Android `android_main` içinde `return` edersem?**
**Yapma.** `CleanUp()` bitene kadar bekle. Aksi halde native_app_glue deadlock → ANR.

**S: Custom renderer'ımı (Vulkan) bağlıyorum, `knst_window_opengl_content` kullanmıyorum. Sorun olur mu?**
Hayır. `knst_window` sadece event + pencere yönetir. Renderer'ı sen bağlarsın.

**S: Neden pencere move/copy edilemiyor?**
Platform kaynakları, listener pointer'ları, callback lambdaları hepsi `this`'e bağlı. Move olsa UAF. Stack veya `unique_ptr` kullan.

**S: `capabilities()` ne zaman çağırılmalı?**
`Init()` sonrası herhangi bir zamanda. Platform sabittir, çok çağırmak zararsız (static).

**S: Aynı anda iki `knst_opengl_global::Init` çağırırsam?**
İkinci çağrı `s_initialized` kontrolü yapar → hemen `true` döner. Idempotent.

**S: Pencereyi yeniden kullanabilir miyim?** (destroy sonrası creation)
Evet, `creation()` içinde `m_destroyed = false` sıfırlanır. Ama platform kaynakları yeniden oluşturulur — state kaybı olabilir.

**S: `capabilities().fractional_scale` X11'de neden false?**
X11 protokolünde native fractional scale yok. `Xft.dpi` üzerinden tahmin edilebilir ama bu "gerçek" fractional scale değil.

**S: Wayland'de `move()` neden çalışmıyor?**
Protokol gereği. Kullanıcı compositor'a taşıma yetkisi vermeden pencere taşınamaz. `xdg_toplevel_move` kullanıcı etkileşimi (drag) gerektirir.

**S: X11'de pencere ikonu neden görünmüyor?**
Bazı WM'ler `_NET_WM_ICON`'ı yok sayar. GNOME/KDE genelde çalışır. Sway/Hyprland kendi ikon sistemini kullanır.


> **Not:** Bu doküman beta süreci sonunda yazıldı. Tüm platformlarda (Windows, Linux X11, Wayland, Dual, Android) sızıntı testleri (ASan, UBSan, Valgrind) geçildi, gerçek donanımda denendi.

> **Soru / Katkı / Öneri:** [Issues](https://github.com/YusufGundar/KernelNucleusT/issues)

> Umarım işine yarar. İyi günler :)