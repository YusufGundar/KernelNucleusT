# KernelNucleusT (*Beta)

####  C++ 20 ile yazılmış Yüksek performanslı, Özelleştirilebilir , Çapraz Platform Destekli bir kütüphane paketidir.

# knst_window — Deskteklenen Platformlar

| Platform | Durum |
|----------|:-----:|
| **Windows** | ✅ |
| **Linux X11** | ✅ |
| **Linux Wayland** | ✅ |
| **Android** | ✅ |

Yukarıdaki platformlarda pencere açma, olay alma, klavye/fare, pano ve yaşam döngüsü test edildi

## İçerisinde 


### Karmaşık Yapılardan

- knst_window
- knst_vgui <---------> (Çok yakında)

### Basit Yapılardan

- knst_byte_array
- knst_c16string  
- knst_device
- knst_file
- knst_function
- knst_image_loader
- knst_process
- knst_thread
- knst_thread_pool
- knst_vector


#### Bünyesinde barındırır. Karmaşık yapılar, temel yapılar kullanılarak oluşturulmaktadır. Basit yapıların çoğu birbirlerine bağımlıdır; karmaşık yapılar ise basit yapılara bağımlıdır.


## Felsefe

KernelNucleusT, **performans ve güvenlik arasında optimum denge** üzerine kurulmuştur. Temel prensipler:

- **noexcept + bool dönüş** — Çoğu fonksiyon exception fırlatmak yerine `bool` döndürür. Bu sayede hem performans artar hem de hata kontrolü tamamen geliştiricinin elinde olur.

- **Force Inline** — Varsayılan olarak bazı performans için kritik olan fonksiyonlar `force_inline` ile derlenir. Call overhead'i olmaz, kod direkt çağrıldığı yere kopyalanır. Sonuç: daha hızlı çalışma, fakat biraz daha daha büyük binary, tercih sizin.

- **Ayarlanabilir Binary Boyutu** — `KNST_SMALL_SIZE_CLASS` tanımlanırsa, `force_inline` yerine standart `inline` kullanılır. Derleyici kendi karar verir, çoğu durumda `call` ile fonksiyona gidilir. Binary boyutu küçülür, hız azalabilir. Tercih sizin. Ayrıca genel olarak kütüphaneleri istediğiniz gibi özelleştirme imkanı sunmaktadır `include/knst_settings.hpp` içerisinde makrolar ve açıklamaları mevcuttur.

- **Duruma Göre Esneklik** — Sık kullanılan kritik fonksiyonlarda binary boyut pahasına ek optimizasyonlar yapılabilir. Bu bir hata değil, bilinçli bir tercihtir. hedefimiz sizlere esneklik ve performansı en iyi şekilde harmanlamaktır , ayrıca makrolar kütüphaneyi istediğiniz gibi şekillendirme imkanı sunar

Bu felsefe, paketteki tüm mevcut ve gelecek yapılar için geçerlidir

---

## Detaylı Döküman İçin

#### Proje dizinindeki `docs/` yolu içerisinde `türkçe` ve `english` klasörleri altında , basit ve karmaşık yapıların dökümanları mevcuttur ek olarak `docs/` içerisinde vscode_setting altında platformlara özel hazır ayarlar vardır.

Örneğin : CTRL + SHİFT + P ile `C/C++: Edit Configurations` ile ayarı ekleyebilirsiniz
 
---

# KernelNucleusT – Kurulum Bilgilendirmesi

Bu kısım KernelNucleusT kütüphanesini **Linux (X11 / Wayland)**, **Windows** ve **Android** için nasıl derleyeceğini ve makro detaylarını adım adım anlatmaktadır;

---

## 1. Temelde 2 farklı seçim vardır

| Seçim | Ne yapar? |
|---|---|
| **Platform** | Otomatik algılanır. Linux'ta ayrıca **X11** mi **Wayland** mı olduğunu sizin seçmeniz gerekir. |
| **Grafik modu** | **Vulkan** (`-DKNST_ENABLE_VULKAN=ON`) veya **Headless** (`-DKNST_ENABLE_VULKAN=OFF`, varsayılan) olarak seçilmektedir.|


---

## 2. Tüm CMake seçenekleri

| Seçenek | Varsayılan | Açıklama |
|---|---|---|
| `KNST_ENABLE_VULKAN` | `OFF` | `ON` = Vulkan ile derle, `OFF` = headless |
| `KNST_LINUX_PLATFORM` | `X11` | Sadece Linux: `X11` veya `Wayland` |
| `KNST_BUILD_EXAMPLES` | `OFF` | `examples/` klasöründeki örnekleri de derle |
| `KNST_APP_SOURCE` | Boş | Kendi `.cpp` dosyanı `knst_app` olarak derle |
| `KNST_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UBSan (hata ayıklama için) |
| `KNST_WARNINGS_AS_ERRORS` | `OFF` | Uyarıları hata say |
| `KNST_STATIC_MSVC_RUNTIME` | `ON` | Sadece MSVC: statik runtime (`/MT`) |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |



---

## 3. Hangi makrolar tanımlanır?

Bu makrolar kütüphaneyi kullanan her projeye **otomatik** geçer. Elle tanımlamana gerek yok.

| Durum | Tanımlanan makrolar |
|---|---|
| Linux + X11 | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_X11` |
| Linux + Wayland | `KNST_USING_PLATFORM_LINUX`, `KNST_USING_LINUX_PLATFORM_WAYLAND` |
| Windows | `KNST_USING_PLATFORM_WINDOWS`, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `UNICODE`, `_UNICODE` |
| Android | `KNST_USING_PLATFORM_ANDROID`, `KNST_USING_PLATFORM_LINUX` |
| Vulkan açık | `KNST_USING_VULKAN` (Android'de ayrıca `KNST_PLATFORM_ANDROID_VULKAN`) |
| Headless | `KNST_HEADLESS_MODE` tanımlanır, Vulkan makrosu **tanımlanmaz** |

Derleme sırasında CMake sonunda bir özet yazdırır; hangi makroların tanımlandığını oradan görebilirsin.

---

## 4. Hızlı başlangıç (küçük bir örnek)

```bash
git clone https://github.com/YusufGundar/KernelNucleusT
cd KernelNucleusT

# Yapılandır (Linux X11 + Vulkan, kendi .cpp dosyanı derle)
cmake -S . -B build \
  -DKNST_ENABLE_VULKAN=ON \
  -DKNST_APP_SOURCE=examples/simple_structures/knst_vector/knst_vector_basic.cpp

# Derle
cmake --build build -j

# Çalıştır (örnek)
./build/bin/knst_app
```
### Kütüphane örnekleri de derlemek isterseniz

```bash
cmake -S . -B build -DKNST_ENABLE_VULKAN=ON -DKNST_BUILD_EXAMPLES=ON

cmake --build build -j

./build/bin/knst_window_basic
```

💡 Not: Her iki durumda da çıktılar build/bin/ klasörüne düşer. Örnek modunda her örnek kendi adıyla ayrı bir program olur (knst_window_basic, knst_vector_basic gibi).


---

## 5. Linux

### 5.1 Gerekli araçlar

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

### 5.2 X11 için paketler

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libx11-dev libx11-xcb-dev libxcb1-dev libxcb-randr0-dev \
  libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-util-dev libxcb-sync-dev \
  libxext-dev libxfixes-dev libxi-dev libxrandr-dev libxcursor-dev
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
cmake -S . -B build -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=ON
cmake --build build -j
```

### 5.3 Wayland için paketler

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libwayland-dev libxkbcommon-dev
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
cmake -S . -B build -DKNST_LINUX_PLATFORM=Wayland -DKNST_ENABLE_VULKAN=ON
cmake --build build -j
```

> Wayland protokol dosyaları (`.c` dosyaları) `include/platform/linux/wayland/protocol_files/` içinde hazır gelir. CMake bunları kendisi derler, sizin bir şey yapmanıza gerek yok.

### 5.4 Vulkan için paketler (Linux)

**Ubuntu / Debian / Pop!_OS**
```bash
sudo apt install libvulkan-dev vulkan-tools
# İsteğe bağlı (hata ayıklama katmanları):
sudo apt install vulkan-validationlayers
```

**Fedora**
```bash
sudo dnf install vulkan-loader-devel vulkan-headers vulkan-tools
```

**Arch**
```bash
sudo pacman -S vulkan-headers vulkan-icd-loader vulkan-tools
```

Kontrol: `vulkaninfo --summary` komutu ekran kartını göstermelidir. NVIDIA / AMD / Intel için güncel sürücü kurulu olmalıdır.

### 5.5 Linux'ta Headless

Ekran kartı veya Vulkan paketi gerekmez:
```bash
cmake -S . -B build -DKNST_ENABLE_VULKAN=OFF
cmake --build build -j
```

---

## 6. Windows

### 6.1 Seçenek A: Visual Studio (MSVC) – önerilen

Gerekenler:
- **Visual Studio 2022** ("Desktop development with C++" iş yükü ile)
- **CMake** (Visual Studio ile birlikte gelir)
- Vulkan kullanacaksan **Vulkan SDK**: https://vulkan.lunarg.com/ (kurulumdan sonra terminali kapatıp aç; `VULKAN_SDK` ortam değişkeni otomatik ayarlanır)

"x64 Native Tools Command Prompt" veya PowerShell'de:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_ENABLE_VULKAN=ON"
cmake --build build --config Release
```
Çıktı: `build\bin\Release\`

Headless için `-DKNST_ENABLE_VULKAN=OFF` yaz, Vulkan SDK gerekmez.

### 6.2 Seçenek B: MSYS2 (MinGW)



1. https://www.msys2.org/ adresinden MSYS2'yi kur.
2. **MSYS2 UCRT64** terminalini aç ve şunları kur:

Ekstra : pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja

```bash
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja
# Vulkan için:
pacman -S mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader
```
3. Derle:
```bash
cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=ON
cmake --build build
```
Çıktı: `build/bin/`

---

## 7. Android

### 7.1 Gerekenler
- **Android NDK** (r26 veya daha yenisi önerilir). Android Studio → SDK Manager → SDK Tools → "NDK (Side by side)" ile kurabilirsiniz.
- **CMake** ve **Ninja**
- Ortam değişkeni (yolu kendi kurulumuna göre değiştir):
```bash
export ANDROID_NDK=$HOME/Android/Sdk/ndk/<sürüm>
```
Windows PowerShell: `$env:ANDROID_NDK="C:\Users\<kullanici>\AppData\Local\Android\Sdk\ndk\<sürüm>"`

### 7.2 Derleme (Vulkan ile)

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DKNST_ENABLE_VULKAN=ON \
  -DKNST_BUILD_EXAMPLES=ON
cmake --build build-android
```

Çıktı: `build-android/libknst_app.so`

Önemli noktalar:
- **Vulkan için en az `android-24` gerekir.** Daha düşük verirsen CMake hata verir.
- `ANDROID_ABI` değerleri: `arm64-v8a` (çoğu telefon), `armeabi-v7a`, `x86_64` (emülatör).
- Headless için `-DKNST_ENABLE_VULKAN=OFF` yaz.
- Kendi kodunu kullanmak istersen: `-DKNST_APP_SOURCE=yol/dosyam.cpp`

Hazır olarak 'build-android' scriptleri mevcuttur gerekli ortamı sağladıktan sonra o scriptleri çalıştırarak bağlantıyı sağlayabilirsiniz.


## 8. Kendi projende kullanmak

Kütüphane sadece başlık dosyalarından oluşur (header-only). En kolay yol `add_subdirectory`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(BenimUygulamam CXX)

set(KNST_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
set(KNST_LINUX_PLATFORM "X11" CACHE STRING "" FORCE)   # sadece Linux'ta anlamlı

add_subdirectory(KernelNucleusT)

add_executable(benim_uygulamam main.cpp)
target_link_libraries(benim_uygulamam PRIVATE KernelNucleusT::KernelNucleusT)
```

Bu satır yeterlidir: include klasörleri, platform makroları, `KNST_USING_VULKAN`, X11/Wayland/Vulkan kütüphaneleri otomatik bağlanır. Kodunda şöyle kullanabilirsin:

```cpp
#include "KernelNucleusT.hpp"

#if defined(KNST_USING_VULKAN)
    // Vulkan modu
#else
    // Headless modu
#endif
```

---

## 9. Sık karşılaşılan sorunlar

| Sorun | Çözüm |
|---|---|
| `Missing X11 development packages` | Bölüm 5.2'deki paketleri kur, sonra `rm -rf build` ve tekrar yapılandır. |
| `KNST_ENABLE_VULKAN=ON but Vulkan was not found` | Vulkan paketini / SDK'yı kur (Bölüm 5.4 veya 6), ya da `-DKNST_ENABLE_VULKAN=OFF` kullan. |
| `Wayland protocol source not found` | `include/platform/linux/wayland/protocol_files/` klasöründeki `.c` dosyalarının silinmediğinden emin ol. |
| `Android NDK not found` | `-DCMAKE_TOOLCHAIN_FILE=.../android.toolchain.cmake` parametresini ver. |
| `Vulkan requires Android API level 24` | `-DANDROID_PLATFORM=android-24` (veya daha yenisi) kullan. |
| Windows'ta Vulkan bulunamıyor | Vulkan SDK'yı kur, terminali kapatıp yeniden aç (`VULKAN_SDK` değişkeni yüklensin). |
| Ayarı değiştirdim ama etkisi yok | Önbelleği temizle: `rm -rf build` (Windows: `rmdir /s /q build`) ve baştan yapılandır. |
| Hangi makrolar tanımlı? | `cmake` çıktısının sonundaki özet tablosundaki **Definitions** satırına bak. |
| `ninja: command not found` (Windows) | MSYS2 UCRT64 terminalini kullan, normal PowerShell değil. Yoksa: `pacman -S mingw-w64-ucrt-x86_64-ninja` |
| `cmake: command not found` (Windows) | MSYS2 UCRT64'te: `pacman -S mingw-w64-ucrt-x86_64-cmake` |
| PowerShell `-D` argümanını `.cpp`'den bölüyor | Argümanı tırnak içine al: `"-DKNST_APP_SOURCE=yol/dosya.cpp"` |
| `cl.exe not found` (MSVC) | "x64 Native Tools Command Prompt for VS" aç, normal PowerShell değil |
| Visual Studio 2026 kullanıyorum | Generator adı: `-G "Visual Studio 18 2026"` |
---


## 10. Hazır komut özeti
```bash

#-DKNST_BUILD_EXAMPLES=ON bütün örnekleri derlemek için
# ------------------------------------X11--------------------------------------------
  # Vulkan
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app

  # Headless
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=OFF -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app

  # Örnekleri de derle (X11 + Vulkan)
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=ON -DKNST_BUILD_EXAMPLES=ON  
  cmake --build build -j
  ./build/bin/knst_window_basic

  # Örnekleri de derle (X11 + Headless)
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=X11 -DKNST_ENABLE_VULKAN=OFF -DKNST_BUILD_EXAMPLES=ON
  cmake --build build -j
  ./build/bin/knst_vector_basic
# ------------------------------------------------------------------------------------

# ------------------------------------Wayland-----------------------------------------
  # Vulkan 
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_ENABLE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build -j
  ./build/bin/knst_app

  # Headless
  cmake -S . -B build -G Ninja -DKNST_LINUX_PLATFORM=Wayland -DKNST_ENABLE_VULKAN=OFF
  cmake --build build -j
  ./build/bin/knst_app
# ------------------------------------------------------------------------------------

# ------------------------------------Windows MSVC------------------------------------
# NOT: "x64 Native Tools Command Prompt for VS" icinde calistir
# NOT: -D argumanlarini tirnak icine al (PowerShell tuzagi)

  # Vulkan
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_ENABLE_VULKAN=ON"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

  # Headless
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DKNST_ENABLE_VULKAN=OFF"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe

  # Kendi .cpp dosyanla
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
    "-DKNST_ENABLE_VULKAN=ON" ^
    "-DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp"
  cmake --build build --config Release
  build\bin\Release\knst_app.exe
# ------------------------------------------------------------------------------------

# --------------------------------Windows (MSYS2 / MinGW)-----------------------------
# NOT: Normal PowerShell degil, "MSYS2 UCRT64" terminalini kullan
# NOT: Bash'te tirnak gerekmez, -D argumanlari bolunmez

  # Vulkan
  cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=ON -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build
  ./build/bin/knst_app.exe

  # Headless
  cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=OFF -DKNST_APP_SOURCE=examples/complex_structures/knst_window/pc/knst_window_basic.cpp
  cmake --build build
  ./build/bin/knst_app.exe
# -----------------------------------------------------------------------------------

# ------------------------------------Android----------------------------------------
  # Ortam değişkeni (Linux / macOS)
  export ANDROID_NDK=$HOME/Android/Sdk/ndk/<sürüm>

  # Ortam değişkeni (Windows PowerShell)
  $env:ANDROID_NDK="C:\Users\<kullanici>\AppData\Local\Android\Sdk\ndk\<sürüm>"

  # Vulkan
  cmake -S . -B build-android -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DKNST_ENABLE_VULKAN=ON
  cmake --build build-android

  # Headless
  cmake -S . -B build-android -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DKNST_ENABLE_VULKAN=OFF
  cmake --build build-android
# -----------------------------------------------------------------------------------

# Kendi .cpp Dosyanı Derle
  cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=ON \
    -DKNST_APP_SOURCE=yol/dosyan.cpp
  cmake --build build
# -----------------------------------------------------------------------------------

# Tüm Örnekleri Derle
  cmake -S . -B build -G Ninja -DKNST_ENABLE_VULKAN=ON -DKNST_BUILD_EXAMPLES=ON
  cmake --build build
# -----------------------------------------------------------------------------------
```


## 🤝 Katkıda Bulunma

#### Bug raporları ve özellik istekleri için **Issues** sayfasını kullanabilirsiniz

#### Eğer özel olarak yardımda bulunmak isterseniz

  [![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-ffdd00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/developeryk)

---

## 📄 License

- MPL 2.0