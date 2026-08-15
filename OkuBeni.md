# KernelNucleusT

Modern C++17 kütüphane paketi. Yüksek performanslı, özelleştirilebilir , çapraz platform destekli bir kütüphane Paketidir

## İçerisinde 
### Paketli Yapılardan

- knst_window  ---> Window / Linux(X11 / Wayland) / Android <---  Desteği sağlamaktadır
___


### Temel Yapılardan

- knst_c16string  
- knst_byte_array
- knst_vector
- knst_memory
- knst_image_loader ---> şimdilik sadece `BMP` desteği mevcuttur

Sınıfları içerisinde bulundurmaktadır

## Felsefe

KernelNucleusT, **performans ve güvenlik arasında optimum denge** üzerine kurulmuştur. Temel prensipler:

- **noexcept + bool dönüş** — Çoğu fonksiyon exception fırlatmak yerine `bool` döndürür. Bu sayede hem performans artar hem de hata kontrolü tamamen geliştiricinin elinde olur

- **Force Inline** — Varsayılan olarak fonksiyonlar `force_inline` ile derlenir. Call overhead'i olmaz, kod direkt çağrıldığı yere kopyalanır. Sonuç: daha hızlı çalışma, fakat daha büyük binary

- **Ayarlanabilir Binary Boyutu** — `KNST_SMALL_SIZE_CLASS` tanımlanırsa, `force_inline` yerine standart `inline` kullanılır. Derleyici kendi karar verir, çoğu durumda `call` ile fonksiyona gidilir. Binary boyutu küçülür, hız azalabilir. Tercih sizin. Ayrıca genel olarak kütüphaneleri istediğiniz gibi özelleştirme imkanı sunmaktadır , ayrıca `knst_window` opengl contenti için özel title bar temalarıda sunmaktadır

- **Duruma Göre Esneklik** — Sık kullanılan kritik fonksiyonlarda binary boyut pahasına ek optimizasyonlar yapılabilir. Bu bir hata değil, bilinçli bir tercihtir. hedefimiz sizlere esneklik ve performansı en iyi şekilde harmanlamaktır , ayrıca makrolar kütüphaneyi istediğiniz gibi şekillendirme imkanı sunar

Bu felsefe, paketteki tüm mevcut ve gelecek kütüphaneler için geçerlidir

## 🚀 Kütüphaneler

## knst_window  (Beta)


**Çok kapsamlı windows , linux ve android için tam destekli bir pencere yönetim kütüphanesi olmayı ve sizlere olabildiğince çok özellik sunmayı hedefler**

**Özellikler:**
- **Çoğu kritik ve uygulama çalışırken sürekli tekrarlancak yerlerde `force inline` kullanılmıştır**
- **Modern C++ özellikleri ile kullanıcıya temiz kod yazma imkanı sunar**
- **işletim sisteminin event mantığına benzer bir şekilde eventleri yakalar ve işleme imkanı sunar**
- **Güvenlik** — Sınırlı sayıdaki , testlerden başarıyla geçmiştir


## knst_c16string

`char16_t` tabanlı, yüksek performanslı string sınıfı.
**Default olarak 22 byte'a kadar yani 10 karaktere kadar stack'te tutar, fazlasında heap'e geçer.**

**Özellikler:**
- **Çoklu Karakter Desteği** — Kurucu ve fonksiyonlarda `char16_t`, `char`, `wchar_t`, `char32_t` türlerini doğrudan kabul eder.
- **STL Uyumlu** — `std::string`, `std::wstring`, `std::u16string`, `std::u32string` ve view'leri gibi yapılar ile sorunsuz çalışır
- **Makro ile Özelleştirme:**
  - `KNST_C16STRING_DEACTIVE_COW` — Copy-On-Write'ı kapatır
  - `KNST_C16_STRING_USING_ATOMIC_COW` — COW sayacını thread-safe yapar
  - `KNST_C16STRING_ALIGN_64` / `KNST_C16STRING_ALIGN_32` — Sınıf hizalamasını ve SSO kapasitesini değiştirir
- **Güvenlik** — Kapsamlı test paketinden başarıyla geçmiştir


## knst_byte_string

'unsigned char` tabanlı, binary data ve utf8 değerlerini tutmak için knst_c16string alternetifidir, ileride ağ iletişimi için özellikler eklenecektir.
 -  **UTF-8 Desteği:** Ham byte olarak saklar
 -  **Güvenlik** — testlerden başarıyla geçmiştir
 -  **Binary Güvenli:** \0 (null) byte'ları içerebilir


## knst_vector
 Şimdilik kütüphanenin ihtiyaçlarına göre metotları mevcuttur ,metotları sınırlıdır, kullanıcı kullanımı için yeni metotlar eklenecektir

 - **Güvenlik** — testlerden başarıyla geçmiştir
 - **Bellek** — knst_memory ile beraber kullanılabilmektedir


## knst_memory

Instance tabanlı, memory pool allocator. Thread-safe opsiyonu mevcuttur ayrıca eklenecek yeni kütüphanelerin hepsinde knst_memory desteği mevcut olucaktır

---
## Hedefler

Gelecek için bi tür gui framework kütüphanesi tasarlamayı düşünüyorum  ayrıca regex destekli yapılar ve yine çapraz platform destekli dosya okuma işlemleri için temel yapılar eklemeyi düşünüyorum


---

## 📦 Kurulum


### WINDOWS (MSVC)

```bash
# OpenGL
- cmake -B build -DENABLE_OPENGL=ON
- cmake --build build --config Release

# Vulkan
cmake -B build -DENABLE_VULKAN=ON
cmake --build build --config Release

# OpenGL + Vulkan
cmake -B build -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build --config Release

# Run
.\build\Release\knst_app.exe
```


### WINDOWS (MinGW / MSYS2)

```bash
# OpenGL
cmake -B build -G "MinGW Makefiles" -DENABLE_OPENGL=ON
cmake --build build

# Vulkan
cmake -B build -G "MinGW Makefiles" -DENABLE_VULKAN=ON
cmake --build build

# OpenGL + Vulkan
cmake -B build -G "MinGW Makefiles" -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build

# Run
.\build\knst_app.exe
```



### WINDOWS (Ninja / MSYS2)

```bash
# OpenGL
cmake -B build -G Ninja -DENABLE_OPENGL=ON
cmake --build build

# Vulkan
cmake -B build -G Ninja -DENABLE_VULKAN=ON
cmake --build build

# OpenGL + Vulkan
cmake -B build -G Ninja -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build
```



### LINUX / X11

```bash
# X11 + OpenGL (GLX)
cmake -B build -DLINUX_PLATFORM=X11 -DOPENGL_BACKEND=GLX -DENABLE_OPENGL=ON
cmake --build build

# X11 + OpenGL (EGL)
cmake -B build -DLINUX_PLATFORM=X11 -DOPENGL_BACKEND=EGL -DENABLE_OPENGL=ON
cmake --build build

# X11 + Vulkan
cmake -B build -DLINUX_PLATFORM=X11 -DENABLE_VULKAN=ON
cmake --build build

# X11 + OpenGL (GLX) + Vulkan
cmake -B build -DLINUX_PLATFORM=X11 -DOPENGL_BACKEND=GLX -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build

# X11 + OpenGL (EGL) + Vulkan
cmake -B build -DLINUX_PLATFORM=X11 -DOPENGL_BACKEND=EGL -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build
```

### LINUX / WAYLAND
```bash
# Wayland + OpenGL (EGL)
cmake -B build -DLINUX_PLATFORM=Wayland -DENABLE_OPENGL=ON
cmake --build build

# Wayland + Vulkan
cmake -B build -DLINUX_PLATFORM=Wayland -DENABLE_VULKAN=ON
cmake --build build

# Wayland + OpenGL (EGL) + Vulkan
cmake -B build -DLINUX_PLATFORM=Wayland -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON
cmake --build build
```


### ANDROID

```bash
#Linux

    package_tests/knst_window/android/./build_android.sh # apk oluşturur
    package_tests/knst_window/android/./run_android.sh # telefonda apk çalışır

#Windows

    package_tests/knst_window/android/./build_android.bat # apk oluşturur
    package_tests/knst_window/android/./run_android.bat # telefonda apk çalışır
```


 📌 **NOT:** Android için : Android SDK ve Android NDK ' yı indirip derleyiciye tanıtmalısınız  örnek vscode json ayarı :

```json
{
    "configurations": [
        {
            "name": "Android",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/EGL",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES3",
                "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES2",
                "/opt/android-ndk/sources/android/native_app_glue"
            ],
            "defines": [
                "KNST_USING_PLATFORM_ANDROID",
                "KNST_PLATFORM_ANDROID_OPENGL"
            ],
            "compilerPath": "/opt/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-clang-arm64"
        },
        {
            "name": "Windows (MSVC)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "${env:ProgramFiles(x86)}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Microsoft Visual Studio/**/include",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/um",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/shared",
                "${env:ProgramFiles}/Windows Kits/**/Include/**/winrt"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "KNST_USING_OPENGL",
                "KNST_OPENGL_USING_WGL",
                "_CRT_SECURE_NO_WARNINGS",
                "UNICODE",
                "_UNICODE"
            ],
            "compilerPath": "cl.exe",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "windows-msvc-x64"
        },
        {
            "name": "Windows (MinGW)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "C:/msys64/mingw64/include",
                "C:/msys64/mingw64/include/SDL2",
                "C:/msys64/mingw64/include/GL",
                "C:/mingw-w64/x86_64-8.1.0-posix-seh-rt_v6-rev0/mingw64/include",
                "C:/mingw64/include"
            ],
            "defines": [
                "KNST_USING_PLATFORM_WINDOWS",
                "KNST_USING_OPENGL"
            ],
            "compilerPath": "g++.exe",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "windows-gcc-x64"
        },
        {
            "name": "Linux (X11)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/lib/gcc/x86_64-linux-gnu/*/include",
                "${workspaceFolder}/include/linux/x11"
            ],
            "defines": [
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_USING_OPENGL",
                "KNST_OPENGL_USING_GLX"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux (Wayland)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "${workspaceFolder}/include/linux/wayland",
                "/usr/include/wayland-client"
            ],
            "defines": [
                "KNST_USING_LINUX_PLATFORM_WAYLAND",
                "KNST_USING_OPENGL",
                "KNST_OPENGL_USING_EGL"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-gcc-x64"
        },
        {
            "name": "Linux (Vulkan)",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/include",
                "/usr/include",
                "/usr/include/x86_64-linux-gnu",
                "/usr/include/vulkan"
            ],
            "defines": [
                "KNST_USING_LINUX_PLATFORM_X11",
                "KNST_USING_VULKAN"
            ],
            "compilerPath": "/usr/bin/g++",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-gcc-x64"
        }
    ],
    "version": 4
}
```
📌 **NOT:** Örnek olarak size bu json ayarını verebilirim
 
---
---
```cpp
#include "../include/KernelNucleusT.hpp" // Hepsi bukadar
```

- İsterseniz direkt `KernelNucleusT.hpp` bu başlığı include edebilirsiniz yada ek olarak , sadece ihtiyacınız olan yapıları ayrı bir projede kullanabilirsiniz


```cpp

// Bütün temel yapılar `knst_global_functions.hpp` ye bağımlıdır

#include "knst_global_functions.hpp"

#include "knst_c16string.hpp"
#include "knst_byte_string.hpp"
#include "knst_vector.hpp"
#include "knst_image_loader.hpp"
```

```cpp
#include "knst_window.hpp" // Paket kütüphaneler ise temel yapılara bağımlıdırlar
```

## 🤝 Katkıda Bulunma

  - Bug raporları ve özellik istekleri için **Issues** sayfasını kullanın
  - Herangi bir özel istek veya benimle iletişime geçmek isterseniz mail adresimden ulaşabilirsiniz



##  📄 Lisans

- MIT License — Özgürce kullanın, geliştirin, paylaşın.
