# KernelNucleusT

#### C++ 20 ile yazılmış, yüksek performanslı, özelleştirilebilir, çapraz platform destekli bir kütüphane paketidir.

**Kararlı sürüm hazır.** Aylardır üzerinde çalıştım. Sızıntı testleri (ASan, UBSan, Valgrind), çoklu platform testleri , özel kontroller , gerçek donanım testleri... Hepsinden başarıyla geçti

---

## Felsefe

KernelNucleusT, **performans ve güvenlik arasında optimum denge** üzerine kurulmuştur. Temel prensipler:

- **noexcept + bool dönüş** — Çoğu fonksiyon exception fırlatmak yerine `bool` döndürür. Bu sayede hem performans artar hem de hata kontrolü tamamen geliştiricinin elinde olur.

- **Force Inline** — Varsayılan olarak bazı performans için kritik olan fonksiyonlar `force_inline` ile derlenir. Call overhead'i olmaz, kod direkt çağrıldığı yere kopyalanır. Sonuç: daha hızlı çalışma, fakat biraz daha büyük binary, tercih sizin.

- **Ayarlanabilir Binary Boyutu** — `KNST_SMALL_SIZE_CLASS` tanımlanırsa, `force_inline` yerine standart `inline` kullanılır. Derleyici kendi karar verir, çoğu durumda `call` ile fonksiyona gidilir. Binary boyutu küçülür, hız azalabilir. Tercih sizin. Ayrıca genel olarak kütüphaneleri istediğiniz gibi özelleştirme imkanı sunmaktadır; `include/knst_settings.hpp` içerisinde makrolar ve açıklamaları mevcuttur.

- **Duruma Göre Esneklik** — Sık kullanılan kritik fonksiyonlarda binary boyut pahasına ek optimizasyonlar yapılabilir. Bu bir hata değil, bilinçli bir tercihtir. Hedefimiz sizlere esneklik ve performansı en iyi şekilde harmanlamaktır, ayrıca makrolar kütüphaneyi istediğiniz gibi şekillendirme imkanı sunar.

Bu felsefe, paketteki tüm mevcut ve gelecek yapılar için geçerlidir.

---

## Detaylı Döküman İçin

Proje dizinindeki `docs/` yolu içerisinde `türkçe` ve `english` klasörleri altında basit ve karmaşık yapıların detaylı dökümanları mevcuttur.

Ek olarak `docs/` içerisinde `vscode_setting` altında platformlara özel hazır ayarlar vardır. Örneğin: `CTRL + SHIFT + P` ile `C/C++: Edit Configurations` menüsünden ilgili ayarı ekleyebilirsiniz.

---

## Desteklenen Platformlar

| Platform | Durum | Test Edildi |
|----------|:-----:|-------------|
| **Windows** (Win32/Win64) | ✅ | ✅ |
| **Linux X11** | ✅ | ✅ |
| **Linux Wayland** | ✅ | ✅ |
| **Linux Dual (X11 + Wayland, tek binary)** | ✅ | ✅ |
| **Android** | ✅ | ✅ |

---

## Öne Çıkan Özellikler

- **Tek kütüphane, çok platform** — Windows, Linux (X11/Wayland/Dual), Android.

- **Otomatik Android Build Script'leri** — SDK/NDK/cihaz tespiti, build, APK paketleme, kurulum ve canlı logcat tek komutla. `.sh` (Linux) ve `.bat` (Windows) hazır gelir.

- **Header-only** — Kütüphanenin tüm dosyaları .hpp 'dir
- **Modern C++20** — Özel allocator desteği gibi yerlerde etkili olmuştur
- **Exception-free** — Kritik API'lerin çoğu `bool` döner, `noexcept`. Hata kontrolü tamamen sende.
- **Force Inline** — Kritik fonksiyonlar `KNST_FORCE_INLINE` ile derlenir. Overhead yok.
- **Ayarlanabilir Binary Boyutu** — `KNST_SMALL_SIZE_CLASS` ile binary'yi küçültebilirsin.
- **Dual-mode (X11 + Wayland)** — Tek binary'de iki Linux backend. Runtime'da otomatik seçim.
- **CSD (Custom Title Bar)** — SDL3 tarzı hit-test callback'i. Başlık çubuğunu istediğiniz gibi çizebilirsiniz.
- **Touch Screen** — Windows, X11, Wayland, Android — hepsinde desktek mevcuttur.
- **IME (Input Method)** — XIM (X11), text-input-v3 (Wayland), JNI bridge (Android), TSF (Windows).
- **Modern Wayland** — `cursor-shape-v1`, `fractional-scale`, `viewporter`, `xdg-activation`, `primary-selection`, `idle-inhibit`, `text-input-v3`, `xdg-toplevel-icon`, `tablet-v2`.
- **Flicker-free X11 resize** — `_NET_WM_SYNC_REQUEST` sayesinde pencere boyutlandırırken titremiyor.
- **Wayland frame callback gating** — compositor hazır değilse çizmiyor.
- **Windows Per-Monitor V2 DPI** — HiDPI ekranlarda keskin görüntü.
- **OpenGL + Vulkan** — İki backend bağımsız veya birlikte kullanılabilir. GLAD 3.3 Core kütüphaneyle birlikte gelir.
- **UTF-16 doğru** — Emoji, CJK, Arapça, Türkçe karakterler bozulmuyor.

---

## İçerisinde

### Karmaşık Yapılardan

- **knst_window** — pencere + olay sistemi
  - **OpenGL Core Backend** — `KNST_WINDOW_USE_OPENGL`
  - **Vulkan Backend** — `KNST_WINDOW_USE_VULKAN`
  - **Embedded (X11 + Wayland, tek binary)** — `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND`
- **knst_vgui** <---------> (Çok yakında)

### Basit Yapılardan

- knst_byte_string
- knst_c16string
- knst_devices
- knst_file
- knst_function
- knst_image_loader
- knst_process
- knst_thread
- knst_thread_pool
- knst_vector

Karmaşık yapılar, temel yapılar kullanılarak oluşturulmaktadır. Basit yapıların çoğu birbirlerine bağımlıdır; karmaşık yapılar ise basit yapılara bağımlıdır.

---

## Window Backends

`knst_window` opsiyonel GPU backend'lerini destekler. İkisi **bağımsız** ve **birlikte** kullanılabilir.

| Backend | CMake Option | Makro | Durum |
|---------|--------------|-------|-------|
| **OpenGL** | `-DKNST_WINDOW_USE_OPENGL=ON` | `KNST_WINDOW_USE_OPENGL` | ✅ |
| **Vulkan** | `-DKNST_WINDOW_USE_VULKAN=ON` | `KNST_WINDOW_USE_VULKAN` | ✅ |

| İstediğin | Flags |
|-----------|-------|
| Basic pencere (GPU backend yok) | (hiçbir şey) |
| Sadece OpenGL | `-DKNST_WINDOW_USE_OPENGL=ON` |
| Sadece Vulkan | `-DKNST_WINDOW_USE_VULKAN=ON` |
| İkisi birden | `-DKNST_WINDOW_USE_OPENGL=ON -DKNST_WINDOW_USE_VULKAN=ON` |

---

## Embedded Mode (X11 + Wayland, tek binary)

Kütüphanenin **en güçlü özelliklerinden biri**: aynı binary'ye **hem X11 hem Wayland** backend'ini derleyip, uygulama başlarken **çalışma zamanında** hangisinin aktif olduğunu otomatik algılar.

**Neden?** Son kullanıcı hangi masaüstü oturumunda olduğunu bilmiyor olabilir. Wayland'de X11-only binary çalışmaz; X11'de Wayland-only binary çalışmaz. **Embedded mod**, tek binary ile her iki oturumda da sorunsuz çalışır.

**Aktivasyon:**

```bash
cmake -S . -B build -G Ninja -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON
```

**Çalışma zamanında seçim sırası:**

1. Ortam değişkeni override — `KNST_WINDOW_FORCE_X11=1` veya `KNST_WINDOW_FORCE_WAYLAND=1`
2. Wayland probe — `WAYLAND_DISPLAY` + `wl_compositor` + `xdg_wm_base` mevcut mu?
3. X11 probe — `DISPLAY` set mi ve bağlantı kurulabiliyor mu?
4. Hiçbiri — `knst_backend::none`

**Zorla seçim:**

```bash
KNST_WINDOW_FORCE_X11=1 ./build/bin/knst_app
KNST_WINDOW_FORCE_WAYLAND=1 ./build/bin/knst_app
```

**Dual-mode'da otomatik seçilen özellikler:**

| Özellik | X11 | Wayland |
|---------|:---:|:---:|
| Pencere açma | ✅ | ✅ |
| Klavye / fare / tekerlek | ✅ | ✅ |
| CSD (Custom Title Bar) | ✅ | ✅ |
| Clipboard | ✅ | ✅ |
| Primary selection | ✅ | ✅ |
| Drag & Drop (Dosya) | ✅ | ✅ |
| OpenGL (EGL) | ✅ | ✅ |
| Vulkan | ✅ | ✅ |
| Touch Screen | ✅ (XInput2) | ✅ (wl_touch) |
| IME (Input Method) | ✅ (XIM) | ✅ (text-input-v3) |
| Raw mouse | ✅ (XInput2) | ✅ (relative-pointer) |
| Cursor shape | ✅ | ✅ (cursor-shape-v1) |
| Fractional scale | — | ✅ (fractional-scale) |
| Idle inhibit | — | ✅ (idle-inhibit) |

Kullanıcı `#if` yazmak zorunda kalmaz — kütüphane doğru backend'i otomatik arka planda seçer.

---

## Touch Screen

`knst_window` **tüm platformlarda** dokunmatik desteği sunar. Cihazın fiziksel olarak dokunmatik olup olmadığı **çalışma zamanında** tespit edilir.

| Platform | Mantık 
|----------|-----------
| **Windows** | `WM_TOUCH` + `RegisterTouchWindow`
| **Linux X11** | XInput2 `XI_TouchBegin/Update/End`
| **Linux Wayland** | `wl_touch` protocol
| **Android** | `AInputEvent` (built-in)

**Temel özellikler:**
- Runtime'da donanım tespiti (varsa otomatik aktif)
- Çoklu parmak desteği (varsayılan 10 nokta, `KNST_MAX_TOUCH_POINTS` ile ayarlanır)
- Basınç, pozisyon, ID bilgisi
- Dokunmatik aktif değilken otomatik mouse fallback
- Wayland'de `wl_touch.frame` ile tek event olarak toplanır — pinch/zoom çalışır
- X11'de snapshot mantığı — çoklu parmak stabil ID'lerle gelir

Detaylı API ve olay yapısı için `docs/` klasörüne bakınız.

---

## CSD (Custom Title Bar)

Kütüphane **başlık çubuğunu asla çizmez** — o senin işin. `knst_window` sadece şunu yapar:

1. Sen `set_csd_hit_test()` ile bir callback kurarsın.
2. Her sol tıklamada kütüphane callback'i çağırır.
3. Callback `draggable` dönerse → OS pencereyi sürükler.
4. `resize_*` dönerse → OS pencereyi boyutlandırır.
5. `normal` dönerse → normal mouse event olarak uygulamana gider (senin butonların).

Her platformda native karşılığı otomatik seçilir:
- X11: `_NET_WM_MOVERESIZE`
- Wayland: `xdg_toplevel_move` / `xdg_toplevel_resize`
- Windows: `WM_NCHITTEST` → `HT*` kodları

**SDL3'ün `SDL_SetWindowHitTest()` API'sinin birebir eşdeğeri.** Sdl bu işi biliyor :)

```cpp
window.set_title_bar_height(40);
window.set_csd_hit_test([](const knst_window& w, int x, int y) {
    if (y < 8) return knst_window::csd_hit::resize_top;
    if (y < 40) return knst_window::csd_hit::draggable;
    return knst_window::csd_hit::normal;
});
```

---



# KernelNucleusT – Kurulum Bilgilendirmesi

Bu kısım KernelNucleusT kütüphanesini **Linux (X11 / Wayland / Dual)**, **Windows** ve **Android** için nasıl derleyeceğini ve makro detaylarını anlattığım kısımdır.

---

## 1. Platform Seçimi

| Seçim | Ne yapar? |
|---|---|
| **Platform** | Otomatik algılanır. Linux'ta ayrıca **X11** mi **Wayland** mı (yoksa **ikisi birden**) olduğunu sizin seçmeniz gerekir. |

---

## 2. Tüm CMake Seçenekleri

| Seçenek | Varsayılan | Açıklama |
|---|---|---|
| `KNST_LINUX_PLATFORM` | `X11` | Sadece Linux: `X11` veya `Wayland` |
| `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` | `OFF` | Linux: her iki backend aynı binary'ye (runtime-selected) |
| `KNST_BUILD_EXAMPLES` | `OFF` | `examples/` klasöründeki örnekleri de derle |
| `KNST_APP_SOURCE` | Boş | Kendi `.cpp` dosyanı `knst_app` olarak derle |
| `KNST_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UBSan (hata ayıklama için) |
| `KNST_WARNINGS_AS_ERRORS` | `OFF` | Uyarıları hata say |
| `KNST_STATIC_MSVC_RUNTIME` | `ON` | Sadece MSVC: statik runtime (`/MT`) |
| `KNST_WINDOW_USE_OPENGL` | `OFF` | OpenGL 3.3 Core backend (GLAD + GL libs) |
| `KNST_WINDOW_USE_VULKAN` | `OFF` | Vulkan backend |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |

---

## 3. Hangi Makrolar Tanımlanır?

Bu makrolar kütüphaneyi kullanan her projeye **otomatik** geçer. Elle tanımlamana gerek yok.

| Durum | Tanımlanan makrolar |
|---|---|
| Linux + X11 | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11` |
| Linux + Wayland | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_WAYLAND` |
| Linux + Dual | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11`, `KNST_USING_LINUX_PLATFORM_WAYLAND`, `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND` |
| Windows | `KNST_USING_PLATFORM_WINDOWS`, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `UNICODE`, `_UNICODE` |
| Android | `KNST_USING_PLATFORM_ANDROID`, `KNST_USING_PLATFORM_LINUX` |
| OpenGL açık | `KNST_WINDOW_USE_OPENGL` |
| Vulkan açık | `KNST_WINDOW_USE_VULKAN` |

Backend makroları **bağımsız** — hepsi birlikte tanımlı olabilir. Derleme sırasında CMake sonunda bir özet yazdırılır; hangi makroların tanımlandığını oradan görebilirsin.

---

## 4. Hızlı Başlangıç

```bash
git clone https://github.com/YusufGundar/KernelNucleusT
cd KernelNucleusT

# Yapılandır (Linux X11, kendi .cpp dosyanı derle)
cmake -S . -B build \
  -DKNST_APP_SOURCE=examples/simple_structures/knst_vector/knst_vector_basic.cpp

# Derle
cmake --build build -j

# Çalıştır
./build/bin/knst_app
```

Kütüphane örneklerini de derlemek isterseniz:

```bash
cmake -S . -B build -DKNST_BUILD_EXAMPLES=ON
cmake --build build -j
./build/bin/knst_window_basic
```

💡 Her iki durumda da çıktılar `build/bin/` klasörüne düşer. Örnek modunda her örnek kendi adıyla ayrı bir program olur (`knst_window_basic`, `knst_vector_basic` gibi).

---

## 5. Linux

### 5.1 Gerekli Araçlar

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config
```

**Fedora**
```bash
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config
```

**Arch**
```bash
sudo pacman -S base-devel cmake ninja pkgconf
```

### 5.2 X11 için Paketler

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libx11-dev libx11-xcb-dev libxcb1-dev libxcb-randr0-dev \
  libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-util-dev libxcb-sync-dev \
  libxext-dev libxfixes-dev libxi-dev libxrandr-dev libxcursor-dev libxcb-xinput-dev
```

**Fedora**
```bash
sudo dnf install libX11-devel libxcb-devel xcb-util-devel xcb-util-keysyms-devel \
  xcb-util-wm-devel libXext-devel libXfixes-devel libXi-devel libXrandr-devel libXcursor-devel
```

**Arch**
```bash
sudo pacman -S libx11 libxcb xcb-util xcb-util-keysyms xcb-util-wm \
  libxext libxfixes libxi libxrandr libxcursor
```

Derle:
```bash
cmake -S . -B build -DKNST_LINUX_PLATFORM=X11
cmake --build build -j
```

### 5.3 Wayland için Paketler

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libwayland-dev libwayland-bin libxkbcommon-dev
```

**Fedora**
```bash
sudo dnf install wayland-devel libxkbcommon-devel
```

**Arch**
```bash
sudo pacman -S wayland libxkbcommon
```

Derle:
```bash
cmake -S . -B build -DKNST_LINUX_PLATFORM=Wayland
cmake --build build -j
```

> Wayland protokol dosyaları (`.c` dosyaları) `include/platform/linux/wayland/protocol_files/` içinde hazır gelir. CMake bunları kendisi derler. Sizin pek birşey yapmanıza gerek yoktur.

### 5.4 Embedded (X11 + Wayland) için Ek Paketler

Yukarıdaki X11 **ve** Wayland paketlerinin **hepsi** gerekir. Ek olarak:

```bash
sudo apt install libwayland-bin wayland-protocols
```

Derle:
```bash
cmake -S . -B build -G Ninja -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON
cmake --build build -j
```

Tek binary çıkar; Wayland oturumunda Wayland, X11 oturumunda X11 seçer.

### 5.5 OpenGL Backend için Ek Paketler

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev
```

**Fedora**
```bash
sudo dnf install mesa-libGL-devel mesa-libEGL-devel mesa-libGLES-devel
```

**Arch**
```bash
sudo pacman -S mesa
```

### 5.6 Vulkan Backend için Ek Paketler

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libvulkan-dev vulkan-tools
```

**Fedora**
```bash
sudo dnf install vulkan-loader-devel vulkan-headers vulkan-tools
```

**Arch**
```bash
sudo pacman -S vulkan-headers vulkan-icd-loader vulkan-tools
```

### 5.7 GLAD (OpenGL Yükleyici)

Kütüphane, **GLAD 3.3 Core** ile birlikte gelir. Varsayılan olarak şu dizinde hazırdır:

```
include/platform/knst_window/opengl_content/glad_3_3/
├── include/
│   ├── glad/glad.h
│   └── KHR/khrplatform.h
└── src/glad.c
```

`KNST_WINDOW_USE_OPENGL=ON` verdiğinde CMake bu dosyaları otomatik derler.

**Farklı bir sürüm / profil kullanmak istersen** (örneğin GL 4.6 Core, ya da GLES):

1. https://glad.dav1d.de/ adresinden istediğin konfigürasyonu seç.
2. İndirdiğin zip'in içindeki `include/` ve `src/` klasörlerini
   `include/platform/knst_window/opengl_content/glad_3_3/` altındaki aynı isimli klasörlerle **değiştirebilirsin**, ek olarak `KernelNucleusT.hpp` bu dosyadan eskinin yerine indirdiğinizi ekleyebilirsiniz
3. `rm -rf build && cmake -S . -B build ...` ile yeniden yapılandır.

---

## 6. Windows

### 6.1 Seçenek A: Visual Studio (MSVC)

Gerekenler:
- **Visual Studio 2022** ("Desktop development with C++" iş yükü ile)
- **CMake** (Visual Studio ile birlikte gelir)

"x64 Native Tools Command Prompt" veya PowerShell'de:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Çıktı: `build\bin\Release\`

### 6.2 Seçenek B: MSYS2 (MinGW)

1. https://www.msys2.org/ adresinden MSYS2'yi kur.
2. **MSYS2 UCRT64** terminalini aç ve şunları kur:

```bash
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja
```

3. Derle:
```bash
cmake -S . -B build -G Ninja
cmake --build build
```
Çıktı: `build/bin/`

### 6.3 OpenGL Backend (Windows)

Ek kurulum gerekmez — `opengl32.dll` Windows'ta hazır gelir. GLAD kütüphane ile birlikte gelir.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DKNST_WINDOW_USE_OPENGL=ON
cmake --build build --config Release
```

### 6.4 Vulkan Backend (Windows)

**Vulkan SDK** gerekir → https://vulkan.lunarg.com/

Kurduktan **sonra terminali yeniden başlat** (`VULKAN_SDK` env var'ı için).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DKNST_WINDOW_USE_VULKAN=ON
cmake --build build --config Release
```

MSYS2'de Vulkan:
```bash
pacman -S mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader
```

---

## 7. Android

### 7.1 Gerekenler

- **Android NDK** (r26 veya daha yenisi önerilir). Android Studio → SDK Manager → SDK Tools → "NDK (Side by side)" ile kurabilirsiniz.
- **CMake** ve **Ninja**
- Ortam değişkeni:
```bash
export ANDROID_NDK=$HOME/Android/Sdk/ndk/<sürüm>
```
Windows PowerShell: `$env:ANDROID_NDK="C:\Users\<kullanici>\AppData\Local\Android\Sdk\ndk\<sürüm>"`

### 7.2 Derleme

İki yol var: **hazır script'ler** (önerilen) veya **manuel CMake**.

#### 7.2.1 Hazır Script'ler (En Kolay Yol)

`examples/complex_structures/knst_window/android/` altında 4 script var. Bunlar sırasıyla şunları yapar:

1. SDK + NDK + build-tools otomatik bulur
2. ADB server'ı başlatır
3. USB / mDNS / Wi-Fi / cache üzerinden cihazı bulur
4. CMake configure (değişiklik yoksa atlar)
5. Native build (Ninja parallel)
6. APK paketler (aapt2 + javac + d8 + zipalign + apksigner)
7. APK'yı cihaza kurar + uygulamayı açar
8. Filtrelenmiş canlı logcat başlatır (Ctrl+C'ye kadar)

| Script | Backend | Platform |
|--------|---------|----------|
| `build-android-opengl.sh` | OpenGL ES | Linux |
| `build-android-opengl.bat` | OpenGL ES | Windows |
| `build-android-vulkan.sh` | Vulkan | Linux |
| `build-android-vulkan.bat` | Vulkan | Windows |

Kullanım:

```bash
# Linux / macOS — OpenGL
cd examples/complex_structures/knst_window/android
./build-android-opengl.sh

# Linux / macOS — Vulkan
./build-android-vulkan.sh
```

```bat
:: Windows — OpenGL
cd examples\complex_structures\knst_window\android
build-android-opengl.bat

:: Windows — Vulkan
build-android-vulkan.bat
```

Script SDK'yı bulamazsa ortam değişkenlerini önceden set edebilirsin:

```bash
export ANDROID_HOME=$HOME/Android/Sdk
export ANDROID_NDK=$ANDROID_HOME/ndk/26.1.10909125   # opsiyonel, yoksa en yenisi seçilir
```

```bat
set ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk
set ANDROID_NDK=%ANDROID_HOME%\ndk\26.1.10909125
```

Cihaz bağlı değilse script sana nasıl bağlanacağını adım adım gösterir (USB / VSCode eklentisi / manuel `adb pair`).

> 💡 Script'ler build dizinini (`build-android-opengl/` veya `build-android-vulkan/`) ayrı tutar. Yani OpenGL ve Vulkan sürümlerini aynı cihazda yan yana kurabilirsin — paket isimleri de farklı (`com.knst.test.gl` vs `com.knst.test.vk`).

#### 7.2.2 Manuel CMake

Script kullanmak istemiyorsan klasik yolla da derleyebilirsin:

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DKNST_BUILD_EXAMPLES=ON
cmake --build build-android
```

Çıktı: `build-android/libknst_app.so`

Önemli noktalar:
- `ANDROID_ABI` değerleri: `arm64-v8a` (çoğu telefon), `armeabi-v7a`, `x86_64` (emülatör).
- Kendi kodunu kullanmak istersen: `-DKNST_APP_SOURCE=yol/dosyam.cpp`

> Bu yol sadece `.so` dosyasını üretir. APK paketlemek, cihaza kurmak ve çalıştırmak için ya script'i kullan ya da `aapt2` + `d8` + `zipalign` + `apksigner` zincirini manuel çalıştırman gerekir.

### 7.3 OpenGL Backend (Android)

NDK'da `libEGL.so` ve `libGLESv2.so` hazır gelir, ek kurulum yok. GLAD kütüphane ile birlikte gelir.

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DKNST_WINDOW_USE_OPENGL=ON \
  -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_opengl_test.cpp
cmake --build build-android
```

> Not: Android'de varsayılan olarak **GLES 3.x** kullanılır (`opts.client_api = ClientAPI::OpenGLES`).

### 7.4 Vulkan Backend (Android)

Android API **24+** gerekir (NDK'da `libvulkan.so` hazır).

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DKNST_WINDOW_USE_VULKAN=ON \
  -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_vulkan_test.cpp
cmake --build build-android
```

> `ANDROID_PLATFORM=android-24` altındaysa CMake hata verir.

---

## 8. Kendi Projende Kullanmak

Kütüphane sadece başlık dosyalarından oluşur (header-only). En kolay yol `add_subdirectory`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(BenimUygulamam CXX)

# Linux'ta anlamlı
set(KNST_LINUX_PLATFORM "X11" CACHE STRING "" FORCE)

# Backend seçimi (opsiyonel)
set(KNST_WINDOW_USE_OPENGL ON  CACHE BOOL "" FORCE)
# set(KNST_WINDOW_USE_VULKAN ON CACHE BOOL "" FORCE)

add_subdirectory(KernelNucleusT)

add_executable(benim_uygulamama main.cpp)
target_link_libraries(benim_uygulamama PRIVATE KernelNucleusT::KernelNucleusT)
```

Bu satır yeterlidir: include klasörleri, platform makroları, X11/Wayland kütüphaneleri ve backend bağımlılıkları otomatik bağlanır.

---

## 9. Sık Karşılaşılan Sorunlar

| Sorun | Çözüm |
|---|---|
| `Missing X11 development packages` | Bölüm 5.2'deki paketleri kur, sonra `rm -rf build` ve tekrar yapılandır. |
| `Wayland protocol source not found` | `include/platform/linux/wayland/protocol_files/` klasöründeki `.c` dosyalarının silinmediğinden emin ol. |
| `Android NDK not found` | `-DCMAKE_TOOLCHAIN_FILE=.../android.toolchain.cmake` parametresini ver. |
| `Vulkan was not found` | Vulkan SDK kur (Windows) veya `libvulkan-dev` (Linux). Terminali yenile. |
| OpenGL örneği açılmıyor / segfault | Init sırasına dikkat et: `Init → MakeCurrent → gladLoadGLLoader → gl*` çağrıları. |
| Ayarı değiştirdim ama etkisi yok | Önbelleği temizle: `rm -rf build` (Windows: `rmdir /s /q build`) ve baştan yapılandır. |
| Hangi makrolar tanımlı? | `cmake` çıktısının sonundaki özet tablosundaki **Definitions** satırına bak. |
| `ninja: command not found` (Windows) | MSYS2 UCRT64 terminalini kullan, normal PowerShell değil. |
| `cmake: command not found` (Windows) | MSYS2 UCRT64'te: `pacman -S mingw-w64-ucrt-x86_64-cmake` |
| PowerShell `-D` argümanını `.cpp`'den bölüyor | Argümanı tırnak içine al: `"-DKNST_APP_SOURCE=yol/dosya.cpp"` |
| `cl.exe not found` (MSVC) | "x64 Native Tools Command Prompt for VS" aç, normal PowerShell değil |
| Visual Studio 2026 kullanıyorum | Generator adı: `-G "Visual Studio 18 2026"` |

---

## 10. Hazır Komut Özeti

> **PowerShell kullanıyorsan:** `-D` ile başlayan ve içinde `.` (nokta) olan her argümanı çift tırnak içine al.
> Aksi halde PowerShell argümanı `.cpp` kısmından böler ve CMake `Ignoring extra path from command line: ".cpp"` uyarısı verir.

---

### 🔹 MSYS2 UCRT64 / PowerShell (Windows)

```powershell
#-DKNST_BUILD_EXAMPLES=ON bütün örnekleri derlemek için

# ------------------------------Temel Pencere----------------------------------------
  cmake -S . -B build -G Ninja "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp"
  cmake --build build
  ./build/bin/knst_app.exe
# -----------------------------------------------------------------------------------

# ------------------------------OpenGL Backend---------------------------------------
  cmake -S . -B build -G Ninja "-DKNST_WINDOW_USE_OPENGL=ON" "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_opengl_basic.cpp"
  cmake --build build
  ./build/bin/knst_app.exe
# -----------------------------------------------------------------------------------

# ------------------------------Vulkan Backend---------------------------------------
  cmake -S . -B build -G Ninja "-DKNST_WINDOW_USE_VULKAN=ON" "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_vulkan_basic.cpp"
  cmake --build build
  ./build/bin/knst_app.exe
# -----------------------------------------------------------------------------------

# ------------------------------Tüm Örnekleri Derle----------------------------------
  cmake -S . -B build -G Ninja "-DKNST_BUILD_EXAMPLES=ON"
  cmake --build build
# -----------------------------------------------------------------------------------

# ------------------------------Kendi .cpp Dosyanı Derle-----------------------------
  cmake -S . -B build -G Ninja "-DKNST_APP_SOURCE=yol/dosyan.cpp"
  cmake --build build
# -----------------------------------------------------------------------------------

# ------------------------------Build klasörünü sıfırla------------------------------
  Remove-Item -Recurse -Force build
# -----------------------------------------------------------------------------------
```

---

### 🔹 Windows MSVC (Visual Studio)

> **NOT:** `"x64 Native Tools Command Prompt for VS"` içinde çalıştır. Normal PowerShell değil.

```powershell
# Temel
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

# Kendi .cpp dosyan
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

# OpenGL backend
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_WINDOW_USE_OPENGL=ON" "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_opengl_basic.cpp"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

# Vulkan backend
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_WINDOW_USE_VULKAN=ON" "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_vulkan_basic.cpp"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe
```

---

### 🔹 Linux / macOS (Bash)

```bash
# ------------------------------------X11--------------------------------------------
  # Kendi .cpp dosyanı derle
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app

  # Örnekleri de derle
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_BUILD_EXAMPLES=ON
  cmake --build build -j
  ./build/bin/knst_window_basic
# -----------------------------------------------------------------------------------

# ------------------------------------Wayland-----------------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp # wayland da pencere gözükmez çünkü wayland pencere gözükmesi için çizim yapmalıdır.
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------Dual (X11 + Wayland)----------------------------
  cmake -S . -B build -G Ninja -DKNST_WINDOW_EMBEDDED_X11_AND_WAYLAND=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp # wayland da pencere gözükmez çünkü wayland pencere gözükmesi için çizim yapmalıdır.
  cmake --build build -j
  ./build/bin/knst_app              # Otomatik seçim
  KNST_WINDOW_FORCE_X11=1 ./build/bin/knst_app       # Zorla X11
  KNST_WINDOW_FORCE_WAYLAND=1 ./build/bin/knst_app   # Zorla Wayland
# -----------------------------------------------------------------------------------

# ------------------------------------OpenGL (X11)------------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_WINDOW_USE_OPENGL=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_opengl_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------OpenGL (Wayland)--------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_WINDOW_USE_OPENGL=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_opengl_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------Vulkan (X11)------------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_WINDOW_USE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_vulkan_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# ------------------------------------Vulkan (Wayland)--------------------------------
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_WINDOW_USE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_vulkan_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
# -----------------------------------------------------------------------------------

# Tüm Örnekleri Derle
  cmake -S . -B build -G Ninja -DKNST_BUILD_EXAMPLES=ON
  cmake --build build
# -----------------------------------------------------------------------------------
```

---

### 🔹 Android

> **En kolay yol:** hazır script'i çalıştır. SDK/NDK/cihaz tespiti, build, APK, kurulum ve logcat hepsi tek komutta.

```bash
# Hazır script — Linux / macOS
  cd examples/complex_structures/knst_window/android
  ./build-android-opengl.sh     # OpenGL ES
  ./build-android-vulkan.sh     # Vulkan
```

```powershell
# Hazır script — Windows
  cd examples\complex_structures\knst_window\android
  build-android-opengl.bat      # OpenGL ES
  build-android-vulkan.bat      # Vulkan
```

Manuel CMake (sadece `.so` üretir — APK / install / logcat yok):

```bash
# Ortam değişkeni (Linux / macOS)
  export ANDROID_NDK=$HOME/Android/Sdk/ndk/<sürüm>

# Ortam değişkeni (Windows PowerShell)
  $env:ANDROID_NDK="C:\Users\<kullanici>\AppData\Local\Android\Sdk\ndk\<sürüm>"

# Temel build
  cmake -S . -B build-android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
  cmake --build build-android

# OpenGL (GLES) backend
  cmake -S . -B build-android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DKNST_WINDOW_USE_OPENGL=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_opengl_test.cpp
  cmake --build build-android

# Vulkan backend
  cmake -S . -B build-android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DKNST_WINDOW_USE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/android/knst_window_android_vulkan_test.cpp
  cmake --build build-android
```

---

### 📌 Kural Özeti

| Kabuk | `-D...=.cpp` içeren argüman |
|-------|-----------------------------|
| **Bash (Linux/macOS)** | Tırnaksız çalışır |
| **cmd.exe (Windows)** | Tırnaksız çalışır |
| **PowerShell** | **Tırnak şart** |
| **MSYS2 UCRT64** | Tırnaklı da tırnaksız da olur, garanti için tırnakla |

> **Neden?** PowerShell, `=` sonrası path içindeki `.` karakterini bazen ayrı argüman sanır. Çözüm: tüm `-D...=...` argümanını `"..."` içine al.

---

# Changelog

## v1.0.0 — Kararlı Sürüm

Beta süreci bitti. Bu sürüm, kütüphanenin **ilk kararlı** hali. Tüm platformlarda (Windows, Linux X11, Linux Wayland, Linux Dual, Android) sızıntı testlerinden ve diğer testlerden geçildi, gerçek donanımda denendi. Beta sürümüne göre çok fazla teknik değişiklik yapıldı. Elimden gelenin en iyisini yapmaya çalıştım , aynı zamanda yorum satırları ve düzenin çok önemli şeyler olduğunun farkına vardım , ve her platformda bütün özellik testlerini ve sızıntı testlerini yapmanında ne kadar önemli olduğunu anladım 

Anlık olarak bilgisiyarımda 2 farklı işletim sistemi , android geliştirme ortamı , linux (x11/wayland için birsürü masaüstü ortamı, özel olarak gnome 'title bar testi vs için') yani çok kapsamlı her platformda her koşulda testler yapıldı ve istediğim sonuç alınana kadar düzeltildi ve şimdi sizlerin karşısında hazır durumda `knst_window`


---

## 🤝 Katkıda Bulunma

Bug raporları ve özellik istekleri için **Issues** sayfasını kullanabilirsiniz. Aynı zamanda istek , öneri gibi şeyler içinde bana ulaşabilirsiniz


---

## 📄 License

- MPL 2.0