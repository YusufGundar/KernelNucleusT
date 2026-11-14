# KernelNucleusT

<p align="center">
  <!-- Core -->
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white" alt="C++20"/>
  <img src="https://img.shields.io/badge/Header--Only-yes-success" alt="Header-Only"/>
  <img src="https://img.shields.io/badge/License-MPL_2.0-orange" alt="License"/>
  <img src="https://img.shields.io/badge/Version-2.0.0--alpha.1-success" alt="Version 2.0.0-alpha.1"/>
</p>

<p align="center">
  <!-- Platforms -->
  <img src="https://img.shields.io/badge/Windows-tested-3DDC84?logo=windows&logoColor=white" alt="Windows"/>
  <img src="https://img.shields.io/badge/Linux_X11-tested-3DDC84?logo=linux&logoColor=white" alt="Linux X11"/>
  <img src="https://img.shields.io/badge/Linux_Wayland-tested-3DDC84?logo=linux&logoColor=white" alt="Linux Wayland"/>
  <img src="https://img.shields.io/badge/Android-tested-3DDC84?logo=android&logoColor=white" alt="Android"/>
</p>
<p align="center">
  <!-- Graphics -->
  <img src="https://img.shields.io/badge/OpenGL-3.3_Core-5586A4?logo=opengl&logoColor=white" alt="OpenGL"/>
  <img src="https://img.shields.io/badge/Vulkan-supported-success?logo=vulkan&logoColor=white" alt="Vulkan"/>
</p>

<p align="center">
  <!-- Networking -->
  <img src="https://img.shields.io/badge/TLS-1.2_%2F_1.3-success" alt="TLS"/>
  <img src="https://img.shields.io/badge/HTTP-1.1_%7C_2_%7C_3-brightgreen" alt="HTTP"/>
  <img src="https://img.shields.io/badge/WebSocket-RFC_6455-green" alt="WebSocket"/>
</p>

<p align="center">
  <!-- Quality -->
  <img src="https://img.shields.io/badge/ASan-clean-success" alt="ASan clean"/>
  <img src="https://img.shields.io/badge/UBSan-clean-success" alt="UBSan clean"/>
  <img src="https://img.shields.io/badge/Valgrind-clean-success" alt="Valgrind clean"/>
</p>

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

- **Ağ (networking)** — HTTP/1.1, HTTP/2, HTTP/3 (QUIC), WebSocket (`ws://` + `wss://`), TLS 1.2/1.3, DoH (DNS over HTTPS), thread-safe DNS cache, HTTP CONNECT + SOCKS5/SOCKS5h proxy, cookie jar, gzip/deflate decoding, SSRF guard, `bind_local_ip`.

- **Otomatik algılama** — `knst_network` katmanları (TLS / H2 / H3) **CMake configure sırasında** otomatik algılanır. Kullanıcı elle `-D..._ENABLE_*` geçmez. CMake çıktısının sonundaki `Network: TLS=TRUE H2=TRUE H3=FALSE` satırı ile doğrulanır.

- **Kendi kendine yeten TLS** — Linux/macOS'ta sistem CA bundle'ı otomatik yüklenir, Windows'ta WinCertStore (ROOT depo) import edilir, Android'de `/apex/com.android.conscrypt/cacerts` + `/system/etc/security/cacerts` okunur. mTLS (`knst_tls_set_client_cert`) ve özel CA desteği (`knst_tls_set_ca_file` / `knst_tls_set_ca_path`) hazır.

- **Otomatik ses kodek algılama** — `libfaad2` varsa AAC decode, `libvorbis` + `libvorbisenc` + `libogg` varsa native OGG Vorbis encode aktif olur. CMake otomatik bulur.

---

## İçerisinde

### Karmaşık Yapılardan

- **knst_window** — pencere + olay sistemi
  - **OpenGL Core Backend** — `KNST_WINDOW_USE_OPENGL`
  - **Vulkan Backend** — `KNST_WINDOW_USE_VULKAN`
  - **Embedded (X11 + Wayland, tek binary)** — `KNST_WINDOW_EMBEDDED_X11_AND_WAYLAND`
- **knst_vgui** <---------> (Çok yakında)

### Basit Yapılardan

- knst_byte_string.hpp
- knst_c16string.hpp
- knst_devices.hpp
- knst_file.hpp
- knst_function.hpp
- knst_image_loader.hpp
- knst_process.hpp
- knst_thread.hpp
- knst_thread_pool.hpp
- knst_vector.hpp
- knst_json.hpp
- knst_video.hpp
- knst_camera.hpp
- knst_audio.hpp
- knst_microphone.hpp
- knst_converter.hpp
- knst_bluetooth.hpp
- knst_usb.hpp
- knst_storage.hpp
- knst_sensors.hpp
- knst_printer.hpp
- knst_network.hpp       — HTTP/1.1 client, DNS cache, DoH, proxy (HTTP CONNECT + SOCKS5), TLS
- knst_websocket.hpp     — WebSocket (ws:// + wss://), full RFC 6455, fragmented messages
- knst_network_h2.hpp    — HTTP/2 (nghttp2 tabanlı, h2 ALPN, otomatik session pool)
- knst_network_h3.hpp    — HTTP/3 (QUIC, ngtcp2 + nghttp3, TLS 1.3 zorunlu)


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
| `KNST_APP_SOURCE` | Boş | Kendi `.cpp` dosyanı `knst_app` olarak derle |
| `KNST_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UBSan (hata ayıklama için) |
| `KNST_WARNINGS_AS_ERRORS` | `OFF` | Uyarıları hata say |
| `KNST_STATIC_MSVC_RUNTIME` | `ON` | Sadece MSVC: statik runtime (`/MT`) |
| `KNST_WINDOW_USE_OPENGL` | `OFF` | OpenGL 3.3 Core backend (GLAD + GL libs) |
| `KNST_WINDOW_USE_VULKAN` | `OFF` | Vulkan backend |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |

### 2.1 Network Seçenekleri (opsiyonel — auto-detected)

> **Önemli:** `KNST_NETWORK_ENABLE_TLS/H2/H3` **kullanıcıya açık CMake flag'i DEĞİL**. Bunlar header tarafında **internal** makrolar. CMakeLists, ilgili kütüphaneleri bulunca bu makroları **otomatik** `target_compile_definitions`'a ekler.

| Seçenek | Varsayılan | Açıklama |
|---|---|---|
| `KNST_NETWORK_DISABLE_TLS` | `OFF` | OpenSSL kurulu olsa bile TLS'i tamamen kapat (H2 + H3 de kapanır) |
| `KNST_NETWORK_DISABLE_H2` | `OFF` | nghttp2 kurulu olsa bile HTTP/2'yi kapat |
| `KNST_NETWORK_DISABLE_H3` | `OFF` | quictls+ngtcp2+nghttp3 kurulu olsa bile HTTP/3'ü kapat |
| `KNST_QUICTLS_ROOT` | (auto) | quictls prefix override (H3 için) |
| `KNST_NGTCP2_ROOT` | (auto) | ngtcp2 prefix override |
| `KNST_NGHTTP3_ROOT` | (auto) | nghttp3 prefix override |
| `KNST_NGHTTP2_ROOT` | (auto) | nghttp2 prefix override |

**Auto-detection sırası** (her bir kütüphane için):
1. `find_package(<name> CONFIG)` — vcpkg / Conan / cmake-installed
2. `pkg_check_modules(<pc-name>)` — Linux / pkg-config
3. `find_library` + `find_path` — son çare

**Katman merdiveni** (en yüksekten alta):
- **H3** → quictls + nghttp2 + ngtcp2 + nghttp3 (TLS + H2 + H3)
- **H2** → sistem OpenSSL + nghttp2 (TLS + H2)
- **H1** → sistem OpenSSL (TLS + H1)
- **off** → hiçbiri (ağ TLS yok)

Configure sonrası CMake çıktısının sonunda görünür:
```
Network:        TLS=TRUE H2=TRUE H3=FALSE
```

> **⚠️ H3 + OpenSSL ABI uyarısı:** H3 aktifse CMakeLists **sadece quictls'ı linkler**, sistem OpenSSL'ini **devre dışı bırakır**. Aynı binary'de iki OpenSSL olmaz, ABI clash önlenir. Runtime'da RPATH quictls dizinini öne alır.

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
| OpenSSL bulundu | `KNST_NETWORK_ENABLE_TLS` |
| nghttp2 bulundu | `KNST_NETWORK_ENABLE_H2` |
| quictls + ngtcp2 + nghttp3 bulundu | `KNST_NETWORK_ENABLE_H3` |
| FAAD2 bulundu | `KNST_NETWORK_HAVE_FAAD2` (AAC decode) |
| Vorbis bulundu | `KNST_NETWORK_HAVE_VORBIS` (OGG encode) |

Backend ve network makroları **bağımsız** — hepsi birlikte tanımlı olabilir. Derleme sırasında CMake sonunda bir özet yazdırılır; hangi makroların tanımlandığını oradan görebilirsin.

> **Not:** Network makrolarının tamamı **auto-detected**. Kullanıcı `-D` ile geçmez. CMake configure çıktısındaki `Network: TLS=TRUE H2=TRUE H3=FALSE` satırı ile doğrulanır.

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

### 5.8 İnternet Kütüphaneleri (Linux)

> **Not:** Bu bölümdeki kütüphaneler **opsiyonel**. Sadece ihtiyaç duyduğunuz katmanı kurun. CMake **auto-detect** eder — `-DKNST_NETWORK_ENABLE_TLS=ON` gibi bir flag **yoktur**.

#### 5.8.1 TLS (HTTPS / WSS)

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libssl-dev
```

**Fedora**
```bash
sudo dnf install openssl-devel
```

**Arch**
```bash
sudo pacman -S openssl
```

Kurduğunuzda CMake otomatik algılar:
```
Network:        TLS=TRUE H2=FALSE H3=FALSE
```

TLS'i **istemiyorsanız** (nadir):
```bash
cmake -S . -B build -DKNST_NETWORK_DISABLE_TLS=ON ...
```

#### 5.8.2 HTTP/2 (`knst_network_h2.hpp`)

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libnghttp2-dev
```

**Fedora**
```bash
sudo dnf install nghttp2-devel
```

**Arch**
```bash
sudo pacman -S nghttp2
```

Kurduğunuzda TLS + H2 otomatik aktifleşir:
```
Network:        TLS=TRUE H2=TRUE H3=FALSE
```

Header'ı projenize ekleyin: `#include "knst_network_h2.hpp"` (veya `KernelNucleusT.hpp`).

#### 5.8.3 HTTP/3 (`knst_network_h3.hpp`) — En zor kısım

HTTP/3 için **4 kütüphane** gerekir: `quictls` + `ngtcp2` + `ngtcp2_crypto_quictls` + `nghttp3`. **Stok OpenSSL 3.0 yetersiz** — QUIC desteği yok, `quictls` fork'u gerekir.

**Ubuntu 24.04+ / Fedora 39+ / Arch:**
```bash
# Ubuntu 24.04+
sudo apt install libngtcp2-dev libngtcp2-crypto-quictls-dev libnghttp3-dev

# Fedora 39+
sudo dnf install ngtcp2-devel nghttp3-devel

# Arch
sudo pacman -S ngtcp2 nghttp3
```

**Ubuntu 22.04 / 20.04 / Pop!_OS (stok repo'da yok):**

İki seçenek var:

**Seçenek 1 — vcpkg (önerilen):**
```bash
git clone https://github.com/microsoft/vcpkg
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install quictls nghttp2 ngtcp2 nghttp3
```

Sonra CMake configure:
```bash
cmake -S . -B build -G Ninja \
    -DKNST_QUICTLS_ROOT=$PWD/vcpkg/installed/x64-linux \
    ...
```

**Seçenek 2 — kaynaktan derle:**
```bash
# 1) quictls (OpenSSL fork, QUIC destekli)
git clone https://github.com/quictls/openssl.git /tmp/quictls
cd /tmp/quictls
git checkout openssl-3.1.5+quic
./config --prefix=/usr/local/quictls --openssldir=/usr/local/quictls/ssl shared
make -j$(nproc)
sudo make install_sw

# 2) nghttp3
git clone --recursive https://github.com/ngtcp2/nghttp3.git /tmp/nghttp3
cd /tmp/nghttp3
autoreconf -i
./configure --prefix=/usr/local PKG_CONFIG_PATH=/usr/local/quictls/lib/pkgconfig
make -j$(nproc)
sudo make install

# 3) ngtcp2
git clone --recursive https://github.com/ngtcp2/ngtcp2.git /tmp/ngtcp2
cd /tmp/ngtcp2
autoreconf -i
./configure --prefix=/usr/local --with-openssl=/usr/local/quictls \
    PKG_CONFIG_PATH=/usr/local/quictls/lib/pkgconfig:/usr/local/lib/pkgconfig
make -j$(nproc)
sudo make install

# 4) Kütüphane yolu
echo '/usr/local/lib' | sudo tee /etc/ld.so.conf.d/knst-quic.conf
sudo ldconfig
```

Configure:
```bash
cmake -S . -B build -G Ninja -DKNST_QUICTLS_ROOT=/usr/local/quictls ...
```

**Seçenek 3 — HTTP/3'ü atla:** Sadece TLS + H2 yeterli olabilir. `knst_http_auto_client` otomatik olarak h3 → h2 → h1 fallback yapar; h3 olmadan da çalışır.

CMake, quictls bulursa **sadece quictls'ı linkler** (system OpenSSL **linklenmez** — ABI clash önleme):
```
Network:        TLS=TRUE H2=TRUE H3=TRUE
```

> **⚠️ ABI Uyarısı:** H3 aktifleştiğinde CMakeLists **bilinçli olarak sistem OpenSSL'ini devre dışı bırakır** ve sadece quictls'ı linkler. Aynı binary'de iki OpenSSL olmaz.

#### 5.8.4 Ses Kodekleri (opsiyonel — auto-detected)

**AAC decode — FAAD2:**
```bash
sudo apt install libfaad-dev          # Ubuntu / Debian
sudo dnf install faad2-devel          # Fedora
sudo pacman -S faad2                  # Arch
```

Bulunursa CMake otomatik `KNST_NETWORK_HAVE_FAAD2` makrosunu tanımlar → AAC dosyaları decode edilebilir.

**OGG Vorbis encode:**
```bash
sudo apt install libvorbis-dev libogg-dev    # Ubuntu / Debian
sudo dnf install libvorbis-devel libogg-devel # Fedora
sudo pacman -S libvorbis libogg               # Arch
```

Bulunursa CMake otomatik `KNST_NETWORK_HAVE_VORBIS` makrosunu tanımlar → `knst_aud::encode_file("...ogg")` native Vorbis ile çalışır.

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

### 6.5 İnternet Kütüphaneleri (Windows)

> **Not:** Windows'ta `KNST_NETWORK_DISABLE_TLS=ON` gibi flag'ler **auto-detect'i kapatmak** içindir. `-DKNST_NETWORK_ENABLE_TLS=ON` **yoktur**.

#### 6.5.1 vcpkg ile (Önerilen)

```powershell
# Bir kereye mahsus
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# TLS + HTTP/2
C:\vcpkg\vcpkg install openssl nghttp2 --triplet x64-windows

# HTTP/3 için (quictls + ngtcp2 + nghttp3)
C:\vcpkg\vcpkg install quictls nghttp2 ngtcp2 nghttp3 --triplet x64-windows
```

CMake configure:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

CMake auto-detect sonucu:
```
Network:        TLS=TRUE H2=TRUE H3=TRUE
```

vcpkg, **runtime DLL'lerini** (`libssl-3-x64.dll`, `libcrypto-3-x64.dll`, `nghttp2.dll` vb.) `build\bin\Release\` altına **otomatik kopyalar**. Elle DLL kopyalama gerekmez.

#### 6.5.2 MSYS2 UCRT64

```bash
pacman -S mingw-w64-ucrt-x86_64-openssl \
          mingw-w64-ucrt-x86_64-nghttp2
```

HTTP/3 için MSYS2'de **hazır paket yok** — vcpkg kullan.

#### 6.5.3 Manuel quictls (vcpkg istemiyorsanız)

[quictls/openssl releases](https://github.com/quictls/openssl/releases) → Win64 binary indir → bir klasöre çıkar (`C:\quictls` gibi). Sonra:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DKNST_QUICTLS_ROOT=C:/quictls"
```

#### 6.5.4 Windows WinCertStore Notu

`KNST_NETWORK_NO_WIN_CERTSTORE` **tanımlamazsan**, TLS ilk açıldığında **Windows ROOT sertifika deposu** otomatik import edilir. Kurumsal CA'lar (şirket proxy'leri vs.) sorunsuz çalışır. `crypt32.lib` link otomatiktir.

Kapatmak için (nadir):
```powershell
cmake -S . -B build "-DCMAKE_CXX_FLAGS=-DKNST_NETWORK_NO_WIN_CERTSTORE"
```

#### 6.5.5 Ses Kodekleri (opsiyonel)

```powershell
# FAAD2 (AAC decode)
C:\vcpkg\vcpkg install faad2 --triplet x64-windows

# Vorbis (OGG encode)
C:\vcpkg\vcpkg install libvorbis libogg --triplet x64-windows
```

Bulunursa CMake otomatik `KNST_NETWORK_HAVE_FAAD2` / `KNST_NETWORK_HAVE_VORBIS` makrolarını tanımlar.

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
# Linux — OpenGL
cd examples/complex_structures/knst_window/android
./build-android-opengl.sh

# Linux — Vulkan
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

### 7.5 İnternet Kütüphaneleri (Android)

> **Uyarı:** Android NDK'sında `openssl`, `nghttp2`, `ngtcp2`, `nghttp3` **hazır gelmez**. CMakeLists bunları **FetchContent** ile **otomatik derler** (ilk configure'da 10-15 dakika sürer).

#### 7.5.1 Otomatik Build (CMakeLists halleder)

`KNST_NETWORK_DISABLE_TLS=OFF` (varsayılan) verildiğinde CMakeLists:

1. **quictls** — GitHub'dan klonlar, NDK toolchain ile cross-compile eder
2. **nghttp2** — FetchContent ile derler
3. **nghttp3 + ngtcp2** — FetchContent ile derler
4. Hepsi `libknst_app.so` içine **statik linklenir**

Sonuç APK içinde **ek .so yok** — hepsi ana library'de.

**Cache:** İlk build sonrası `$HOME/.knst-cache/quictls-android-<ABI>/` altında saklanır. Sonraki build'ler hızlı (re-build yok).

**Süre tahmini:**
- İlk configure + build: 10-15 dk (quictls + nghttp2 + ngtcp2 + nghttp3)
- Sonraki build'ler: 1-2 dk

**Kapatmak için:**
```bash
cmake ... -DKNST_NETWORK_DISABLE_TLS=ON   # hepsini kapat
cmake ... -DKNST_NETWORK_DISABLE_H3=ON    # sadece H3'ü kapat (H2 kalır)
```

#### 7.5.2 Katman Sırası

| Flag kombinasyonu | Sonuç |
|---|---|
| (varsayılan) | TLS + H2 + H3 (auto-build) |
| `-DKNST_NETWORK_DISABLE_H3=ON` | TLS + H2 |
| `-DKNST_NETWORK_DISABLE_H3=ON -DKNST_NETWORK_DISABLE_H2=ON` | Sadece TLS |
| `-DKNST_NETWORK_DISABLE_TLS=ON` | Hiçbiri (plain TCP/UDP) |

Configure sonrası CMake çıktısında:
```
Network:        TLS=TRUE H2=TRUE H3=TRUE
```

#### 7.5.3 Android Sistem CA Store

TLS açıkken `knst_network.hpp` otomatik olarak şu yolları dener:
- `/apex/com.android.conscrypt/cacerts` (Android 14+)
- `/system/etc/security/cacerts` (eski Android)

Ek CA bundle gerekmez — Android sistem sertifikalarını kullanır.

#### 7.5.4 Sadece Local İşlem Yapacaksanız

Sadece cihaz içi işlemler (dosya, kamera, ses) yapacaksanız **internet header'larını include etmeyin**. `KernelNucleusT.hpp` zaten hepsini içermez — sadece ihtiyaç duyduğunuz header'lar seçilir. Build süresi ve binary boyutu küçülür.

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

**Network katmanları** (TLS / H2 / H3) `add_subdirectory` sonrasında **otomatik algılanır** — sistemde gerekli kütüphaneler varsa. Doğrulamak için CMake çıktısına bakın:

```
Network:        TLS=TRUE H2=TRUE H3=FALSE
```

Belirli bir katmanı **zorla kapatmak** için:
```cmake
set(KNST_NETWORK_DISABLE_H3 ON CACHE BOOL "" FORCE)   # H3 kapat, H2 kalsın
add_subdirectory(KernelNucleusT)
```

**JSON entegrasyonu** (opsiyonel):
```cmake
# knst_json.hpp'yi KernelNucleusT.hpp'den önce include ettiğinizden emin olun
target_compile_definitions(benim_uygulamama PRIVATE KNST_NETWORK_ENABLE_JSON)
```

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
| CMake çıktısında `Network: TLS=FALSE` görüyorum | OpenSSL kurulu değil. `libssl-dev` (Linux) / vcpkg `openssl` (Windows) kurun |
| `Network: H2=FALSE` görüyorum | `libnghttp2-dev` kurulu değil veya `-DKNST_NETWORK_DISABLE_H2=ON` geçmişsiniz |
| `Network: H3=FALSE` görüyorum | quictls + ngtcp2 + nghttp3 eksik. `-DKNST_QUICTLS_ROOT=...` geçin veya `apt install libngtcp2-dev libnghttp3-dev` (Ubuntu 24.04+) |
| `fatal error: openssl/ssl.h: No such file` | `libssl-dev` eksik. CMake, OpenSSL bulamadığı için TLS kapalı kalıyor |
| `#error "knst_network_h3.hpp requires KNST_NETWORK_ENABLE_TLS"` | H3 için TLS şart. `KNST_NETWORK_DISABLE_TLS=ON` vermeyin |
| `#error "knst_network_h3.hpp: ngtcp2 >= 1.0 required"` | Eski ngtcp2. `vcpkg upgrade ngtcp2` veya manuel güncelle |
| HTTP/3 istekleri hep Timeout | Ağ UDP/443'ü bloke ediyor. `knst_http_auto_client` otomatik H2/H1'e düşer, manuel `knst_http3_client` fallback yapmaz |
| `libssl.so.3: cannot open shared object` (runtime) | Statik link yapmadığınız için sistemde OpenSSL 3 gerekli. Ya `apt install libssl3` ya statik build |
| Windows'ta TLS handshake başarısız, sertifika hatası | `KNST_NETWORK_NO_WIN_CERTSTORE` tanımlıysa WinCertStore atlanıyor. Tanımlamayı kaldırın |
| Proxy CONNECT başarısız (`ProxyFailed`) | `options::proxy_url` formatı `http://user:pass@proxy:8080` olmalı. `no_proxy` bypass etmiyor mu kontrol edin |
| `libngtcp2.so.16: cannot open` (Linux runtime) | Statik link yoksa sistemde ngtcp2 runtime gerekli. `ldd ./app` ile doğrulayın |
| Android build ilk seferde 15 dk sürüyor | Normal — CMakeLists quictls + nghttp2 + ngtcp2 + nghttp3'ü kaynaktan derliyor. Sonraki build'ler cache'den hızlı |
| `Network: TLS=TRUE H2=TRUE H3=TRUE` ama binary çalışmıyor | Static linking sorunu. Linux'ta `ldd ./build/bin/knst_app` ile quictls kütüphane yolu doğru mu kontrol edin |

---

## 10. Hazır Komut Özeti

> **PowerShell kullanıyorsan:** `-D` ile başlayan ve içinde `.` (nokta) olan her argümanı çift tırnak içine al.
> Aksi halde PowerShell argümanı `.cpp` kısmından böler ve CMake `Ignoring extra path from command line: ".cpp"` uyarısı verir.

---

### 🔹 MSYS2 UCRT64 / PowerShell (Windows)

```powershell

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

### 🔹 Linux (Bash)

```bash
# ------------------------------------X11--------------------------------------------
  # Kendi .cpp dosyanı derle
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app
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
```

---

### 🔹 Android

> **En kolay yol:** hazır script'i çalıştır. SDK/NDK/cihaz tespiti, build, APK, kurulum ve logcat hepsi tek komutta.

```bash
# Hazır script — Linux
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
# Ortam değişkeni (Linux)
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

### 🔹 İnternet (TLS / H2 / H3) Örnek Komutlar

> **Önemli:** `-DKNST_NETWORK_ENABLE_TLS=ON` gibi flag **yoktur**. CMake otomatik algılar. Kapatmak için `KNST_NETWORK_DISABLE_*` kullanın.

**Linux (Bash):**
```bash
# TLS + H2 + H3 (varsa) — otomatik
# Sadece "OpenSSL + nghttp2 + quictls + ngtcp2 + nghttp3" kurulu olması yeterli
cmake -S . -B build -G Ninja \
  -DKNST_LINUX_PLATFORM=X11 \
  -DKNST_APP_SOURCE=yol/network_demo.cpp
cmake --build build -j
./build/bin/knst_app

# Çıktının sonunda: Network: TLS=TRUE H2=TRUE H3=TRUE
```

**Windows (vcpkg ile):**
```powershell
# vcpkg kurulu olmalı: C:\vcpkg
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DKNST_APP_SOURCE=yol/network_demo.cpp"
cmake --build build --config Release
build\bin\Release\knst_app.exe
```

**Android (FetchContent auto-build):**
```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DKNST_APP_SOURCE=yol/network_demo.cpp
cmake --build build-android
# İlk seferde 10-15 dk (quictls kaynaktan derlenir), sonra hızlı
```

**Sadece TLS (H2/H3 kapat):**
```bash
cmake -S . -B build -DKNST_NETWORK_DISABLE_H2=ON -DKNST_NETWORK_DISABLE_H3=ON ...
```

**Network tamamen kapalı:**
```bash
cmake -S . -B build -DKNST_NETWORK_DISABLE_TLS=ON ...
```

---

### 📌 Kural Özeti

| Kabuk | `-D...=.cpp` içeren argüman |
|-------|-----------------------------|
| **Bash (Linux)** | Tırnaksız çalışır |
| **cmd.exe (Windows)** | Tırnaksız çalışır |
| **PowerShell** | **Tırnak şart** |
| **MSYS2 UCRT64** | Tırnaklı da tırnaksız da olur, garanti için tırnakla |

> **Neden?** PowerShell, `=` sonrası path içindeki `.` karakterini bazen ayrı argüman sanır. Çözüm: tüm `-D...=...` argümanını `"..."` içine al.

---

## 🤝 Katkıda Bulunma

Bug raporları ve özellik istekleri için **Issues** sayfasını kullanabilirsiniz. Aynı zamanda istek , öneri gibi şeyler içinde bana ulaşabilirsiniz


---

## 📄 License

- MPL 2.0