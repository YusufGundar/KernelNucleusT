# knst_image_loader — Self-Contained Görüntü Yükleyici

Selamlar! 👋 Bu doküman `knst_image_loader` sınıfının ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **Hiçbir harici kütüphaneye ihtiyaç duymayan (zlib/libpng/libjpeg YOK), kendi DEFLATE / JPEG / GIF / BMP / TGA / PNM / ICO çözücülerini içeren bir görüntü yükleyici.** Formatı dosya uzantısından değil, **magic byte**'lardan tespit eder.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Self-contained** | Hiçbir harici kütüphane gerekmez — zlib, libpng, libjpeg YOK |
| **Kendi DEFLATE** | PNG'nin sıkıştırmasını kendi çözer |
| **Kendi JPEG** | Baseline + progressive, Huffman, CMYK, YCCK, EXIF orientation |
| **Çoklu format** | PNG, JPEG, GIF, BMP, TGA, PNM, ICO/CUR + custom decoder |
| **Magic byte detection** | Dosya uzantısına güvenmez — gerçekten formatı okur |
| **Memory-mapped I/O** | Büyük dosyalar bile hızlı (mmap/MapViewOfFile) |
| **Çok iş parçacıklı** | `ParallelForRows` ile SIMD-dostu işlem |
| **SIMD swizzle** | RGBA ↔ BGRA dönüşümü SSE2/NEON ile |
| **Zengin seçenekler** | Resize, flip, premultiply, flatten, EXIF orientation |
| **5 resample filtresi** | Nearest, Box, Bilinear, Bicubic, Lanczos3 |
| **Pool allocator** | `knst_pool_allocator` ile özel bellek yönetimi |
| **Custom decoder** | WebP, QOI, TIFF gibi formatları kendin ekle |
| **Legacy API** | Eski `load_image()` imzası hala çalışıyor |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // PNG dosyasını yükle
    knst_image img = knst_image_loader::load("photo.png");

    if (!img) {
        std::cerr << "Yüklenemedi: "
                  << knst_image_error_string(img.error) << "\n";
        return 1;
    }

    std::cout << "Boyut   : " << img.width << " x " << img.height << "\n";
    std::cout << "Kanallar: " << img.channels << "\n";
    std::cout << "Format  : "
              << knst_image_loader::format_name(img.source_format) << "\n";

    return 0;
}
```

---

## 🖼️ Desteklenen Formatlar

### PNG

- **Renk tipleri:** Gray, RGB, Palette, Gray+Alpha, RGBA
- **Bit derinliği:** 1, 2, 4, 8, 16
- **Interlace:** Adam7 (tüm 7 pass)
- **Şeffaflık:** `tRNS` chunk (palette + color-key)
- **DEFLATE:** Kendi inflate implementasyonu

### JPEG

- **Baseline** + **Extended sequential** + **Progressive**
- **Kodlama:** Huffman (aritmetik YOK)
- **Renk uzayları:** Gray, YCbCr, RGB, CMYK, YCCK
- **Subsampling:** Tüm layout'lar (4:4:4, 4:2:2, 4:2:0, ...)
- **Upsampling:** libjpeg-uyumlu fancy upsampling
- **EXIF orientation:** Tag 0x0112 (1-8)
- **Restart intervals**

### GIF

- **Sürümler:** GIF87a, GIF89a
- **İlk frame** çözülür (animasyon desteği yok)
- **Transparency** (GIF89a)
- **Interlace** desteği
- **LZW decompression** kendi implementasyonu

### BMP

- **Bit derinliği:** 1, 2, 4, 8, 16, 24, 32
- **RLE4** + **RLE8** sıkıştırma
- **BITFIELDS** (16/32 bit custom mask)
- **OS/2 core** header + **INFO** / **V4** / **V5**
- **Top-down** ve **bottom-up** yön
- **Embedded PNG/JPEG** (compression 4/5)

### TGA

- **Tipler:** 1, 2, 3, 9, 10, 11 (palette/truecolor/gray + RLE)
- **Bit derinliği:** 8, 15, 16, 24, 32
- **Tüm orientation bit'leri** (top/bottom, left/right)

### PNM (PBM / PGM / PPM / PAM)

- **P1-P6:** ASCII + binary
- **P7 (PAM):** Header'lı format
- **maxval:** 1..65535
- **Yorumlar** (`#`)

### ICO / CUR

- **En iyi entry seçimi** (`target_size` seçeneği)
- **PNG entry** desteği
- **BMP (DIB + AND mask)** desteği
- **Cursor hotspot** okuma

### Custom Decoder

Kendi decoder'ını `register_decoder()` ile ekle:

```cpp
knst_image_loader::register_decoder(my_sniff_fn, my_decode_fn);
```

---

## 🎯 Format Tespiti

Format **magic byte**'lardan tespit edilir — dosya uzantısına güvenilmez.

```cpp
knst_byte_string bytes = knst_file::read_file_data<>("unknown.dat");

int fmt = knst_image_loader::detect_format(bytes.data(), bytes.length());
std::cout << "Format: " << knst_image_loader::format_name(fmt) << "\n";
```

### Desteklenen Format Sabitleri

| Sabit | Açıklama |
|---|---|
| `KNST_IMAGE_FORMAT_UNKNOWN` | Bilinmeyen |
| `KNST_IMAGE_FORMAT_PNG` | PNG |
| `KNST_IMAGE_FORMAT_JPEG` | JPEG |
| `KNST_IMAGE_FORMAT_GIF` | GIF |
| `KNST_IMAGE_FORMAT_BMP` | BMP |
| `KNST_IMAGE_FORMAT_ICO` | ICO / CUR |
| `KNST_IMAGE_FORMAT_TGA` | TGA |
| `KNST_IMAGE_FORMAT_PNM` | PBM / PGM / PPM / PAM |
| `KNST_IMAGE_FORMAT_CUSTOM` | Custom decoder |

---

## 📦 `knst_image_t` — Sonuç Yapısı

```cpp
template<typename Alloc = knst_default_allocator>
struct knst_image_t {
    basic_byte_string<Alloc> pixels;   // width * height * channels byte
    uint32_t width         = 0;
    uint32_t height        = 0;
    int      channels      = 0;        // 3 (RGB/BGR) veya 4 (RGBA/BGRA)
    int      output_format = 0;        // KNST_BITMAP_OUTPUT_*
    int      source_format = KNST_IMAGE_FORMAT_UNKNOWN;
    int      hotspot_x     = -1;       // ICO/CUR için
    int      hotspot_y     = -1;
    int      error         = KNST_IMAGE_OK;

    bool   valid() const noexcept;     // geçerli mi?
    explicit operator bool() const;    // if (img) kontrolü
    size_t stride() const noexcept;    // width * channels
    size_t byte_size() const noexcept; // stride * height
};
```

### Kullanım

```cpp
knst_image img = knst_image_loader::load("photo.png");

if (img) {                             // ✅ geçerlilik kontrolü
    std::cout << img.width << "x" << img.height << "\n";
    std::cout << "Toplam byte: " << img.byte_size() << "\n";
    std::cout << "Stride: " << img.stride() << "\n";

    // Piksel erişimi
    const uint8_t* px = img.pixels.data();
    // RGBA formatında: px[0]=R, px[1]=G, px[2]=B, px[3]=A
}
```

---

## 🛠️ Yükleme API'leri

### Dosyadan Yükleme

```cpp
// knst_c16string ile
knst_c16string path = u"/home/user/photo.png";
knst_image img1 = knst_image_loader::load(path);

// UTF-8 char* ile
knst_image img2 = knst_image_loader::load("/home/user/photo.png");

// char16_t* ile
knst_image img3 = knst_image_loader::load(u"/home/user/photo.png");
```

### Bellekten Yükleme

```cpp
knst_byte_string bytes = knst_file::read_file_data<>("photo.png");
knst_image img = knst_image_loader::load_from_memory(
    bytes.data(), bytes.length());
```

### Seçeneklerle Yükleme

```cpp
knst_image_load_options opts;
opts.output_format = KNST_BITMAP_OUTPUT_BGRA;
opts.resize_width  = 256;
opts.resize_height = 256;
opts.keep_aspect   = true;

knst_image img = knst_image_loader::load("photo.png", opts);
```

---

## ⚙️ `knst_image_load_options`

Yükleme seçenekleri — her alanın makul bir varsayılanı vardır.

| Alan | Varsayılan | Açıklama |
|---|---|---|
| `output_format` | `KNST_BITMAP_OUTPUT_RGBA` | Çıkış formatı (RGB/RGBA/BGR/BGRA) |
| `target_size` | `0` | ICO/CUR: en yakın boyutu seç (0 = en büyük) |
| `resize_width` | `0` | 0 = koru. Sadece biri verilirse aspect oranı korunur |
| `resize_height` | `0` | |
| `keep_aspect` | `false` | İkisi de verilirse: stretch yerine kutuya sığdır |
| `resize_filter` | `KNST_IMAGE_RESIZE_CATMULL_ROM` | Resample filtresi |
| `flip_vertical` | `false` | OpenGL tarzı (alt-sol origin) için |
| `flatten_alpha` | `false` | `background_*` üzerine composite, opak yap |
| `background_r/g/b` | `0` | Flatten arka plan rengi |
| `premultiply_alpha` | `false` | Çıkış premultiplied (RGBA/BGRA) |
| `apply_exif_orientation` | `true` | Sadece JPEG |
| `max_dimension` | `0` | 0 = `KNST_IMAGE_MAX_DIMENSION` (16384) |
| `max_threads` | `0` | 0 = otomatik, 1 = tek thread |
| `allowed_formats` | Tümü | Bit mask ile format kısıtlaması |

---

## 🔄 Çıkış Formatları

```cpp
#define KNST_BITMAP_OUTPUT_RGB   (1 << 8)   // 3 kanal
#define KNST_BITMAP_OUTPUT_RGBA  (1 << 9)   // 4 kanal
#define KNST_BITMAP_OUTPUT_BGR   (1 << 10)  // 3 kanal
#define KNST_BITMAP_OUTPUT_BGRA  (1 << 11)  // 4 kanal
```

**Kullanım:**

```cpp
knst_image_load_options opts;
opts.output_format = KNST_BITMAP_OUTPUT_BGRA;   // OpenGL/Vulkan için
knst_image img = knst_image_loader::load("photo.png", opts);
```

**Fark:**
- **RGBA:** Kırmızı-Yeşil-Mavi-Alfa (web standardı)
- **BGRA:** Mavi-Yeşil-Kırmızı-Alfa (Windows/GDI, OpenGL, Vulkan)

**Not:** RGBA → BGRA dönüşümü **SSE2 / NEON** ile SIMD hızlandırmalı.

---

## 🔍 5 Resample Filtresi

```cpp
enum knst_image_resize_filter : int {
    KNST_IMAGE_RESIZE_NEAREST     = 0,  // pixel art
    KNST_IMAGE_RESIZE_BOX         = 1,  // area average (küçültme için iyi)
    KNST_IMAGE_RESIZE_TRIANGLE    = 2,  // bilinear
    KNST_IMAGE_RESIZE_CATMULL_ROM = 3,  // bicubic (keskin)
    KNST_IMAGE_RESIZE_LANCZOS3    = 4   // en iyi kalite, hafif ringing
};
```

### Karşılaştırma

| Filtre | Kalite | Hız | Ne zaman kullan? |
|---|---|---|---|
| **Nearest** | Düşük | Çok hızlı | Pixel art, ikon küçültme |
| **Box** | Orta | Hızlı | Thumbnail üretimi (küçültme) |
| **Triangle** | İyi | Orta | Genel amaçlı bilinear |
| **Catmull-Rom** | Çok iyi | Yavaş | Fotoğraf büyütme (varsayılan) |
| **Lanczos3** | En iyi | En yavaş | Kaliteli thumbnail |

**Kullanım:**

```cpp
opts.resize_filter = KNST_IMAGE_RESIZE_LANCZOS3;
```

---

## 🎨 Dönüşümler

### Resize (Yeniden Boyutlandırma)

```cpp
knst_image_load_options o;

// Aspect oranını koru
o.resize_width  = 512;
o.resize_height = 512;
o.keep_aspect   = true;   // 512x512 kutuya sığar

knst_image thumb = knst_image_loader::load("photo.png", o);
```

**Not:** Sadece `resize_width` verilirse, `resize_height` **otomatik** hesaplanır (ve tersi).

### Flip Vertical

```cpp
knst_image_load_options o;
o.flip_vertical = true;  // OpenGL texture için

knst_image img = knst_image_loader::load("photo.png", o);
```

### Premultiply Alpha

```cpp
knst_image_load_options o;
o.premultiply_alpha = true;  // render pipeline için hazır

knst_image img = knst_image_loader::load("photo.png", o);
```

**Premultiply nedir?** Renk kanalları alfa ile çarpılır: `RGB *= A/255`. GPU blending için optimize edilmiş format.

### Flatten Alpha

```cpp
knst_image_load_options o;
o.flatten_alpha = true;
o.background_r  = 255;   // beyaz arka plan
o.background_g  = 255;
o.background_b  = 255;

knst_image img = knst_image_loader::load("transparent.png", o);
// Şeffaf pikseller beyaza boyanır, alpha=255 olur
```

### EXIF Orientation

```cpp
knst_image_load_options o;
o.apply_exif_orientation = true;   // varsayılan
```

**Ne yapar?** JPEG'in EXIF'inde saklanan orientation (1-8) değerine göre görüntüyü **otomatik döndürür**. Bu olmadan telefon fotoğrafları yan yatar.

| Orientation | Ne yapar? |
|---|---|
| 1 | Normal |
| 2 | Yatay flip |
| 3 | 180° döndür |
| 4 | Dikey flip |
| 5 | 90° CW + flip |
| 6 | 90° CW |
| 7 | 90° CCW + flip |
| 8 | 90° CCW |

---

## 🧠 Pool Allocator

Tüm `load` fonksiyonları **template allocator** destekler. Varsayılan `knst_default_allocator` (malloc/free).

```cpp
// Varsayılan (malloc)
knst_image img1 = knst_image_loader::load("photo.png");

// Pool allocator ile
auto img2 = knst_image_loader::load<knst_pool_allocator>("photo.png");

// Seçeneklerle birlikte
knst_image_load_options o;
o.resize_width  = 256;
o.resize_height = 256;

auto img3 = knst_image_loader::load<knst_pool_allocator>("photo.png", o);

// Bellekten
auto img4 = knst_image_loader::load_from_memory<knst_pool_allocator>(
    bytes.data(), bytes.length(), o);
```

**Ne zaman pool kullanmalı?**
- ✅ Çok sayıda küçük görüntü yüklüyorsan (ikon, thumbnail)
- ✅ Oyun motoru asset pipeline'ı
- ✅ Server-side görüntü işleme
- ❌ Tek tük büyük görüntü (malloc daha uygun)

**Tip değişir:** `knst_image` = `knst_image_t<knst_default_allocator>`, `knst_image_t<knst_pool_allocator>` ayrı bir tip.

---

## 🔌 Custom Decoder (Genişletilebilirlik)

Kendi formatını ekleyebilirsin — WebP, QOI, TIFF, AVIF...

### 1. Sniff Fonksiyonu

```cpp
static bool webp_sniff(const uint8_t* data, size_t size) {
    return size >= 12 &&
           std::memcmp(data, "RIFF", 4) == 0 &&
           std::memcmp(data + 8, "WEBP", 4) == 0;
}
```

### 2. Decode Fonksiyonu

```cpp
static bool webp_decode(const uint8_t* data, size_t size,
                        knst_image_decode_result& result) {
    // ... WebP çöz ...
    // result.rgba doldurulmalı: width * height * 4 byte, RGBA8
    // result.width, result.height ayarlanmalı

    if (!knst_image_loader::allocate_rgba(result, w, h)) {
        return false;
    }
    // result.rgba'ya piksel verisi yaz
    return true;
}
```

### 3. Kayıt Et

```cpp
int main() {
    knst_image_loader::register_decoder(webp_sniff, webp_decode);

    knst_image img = knst_image_loader::load("photo.webp");
    // WebP artık otomatik tanınır

    return 0;
}
```

### Ne Zaman Custom Decoder Kullanmalı?

- ✅ Modern formatlar (WebP, AVIF, QOI)
- ✅ Kendi formatını tanımladıysan
- ✅ Built-in bir decoder'ı override etmek istiyorsan

**Not:** Custom decoder'lar **built-in'lerden önce** denenir.

### Limit

Varsayılan 16 custom decoder eklenebilir:

```cpp
#define KNST_IMAGE_MAX_CUSTOM_DECODERS 16
```

---

## 📜 Legacy API

Eski kod uyumluluğu için `load_image` ve kardeşleri korunmuştur:

```cpp
int w = 0, h = 0;
knst_byte_string pixels = knst_image_loader::load_image(
    u"photo.png", &w, &h, KNST_BITMAP_OUTPUT_RGBA);

// veya format-specific
knst_byte_string png_px  = knst_image_loader::load_png(u"photo.png", &w, &h);
knst_byte_string jpg_px  = knst_image_loader::load_jpeg(u"photo.jpg", &w, &h);
knst_byte_string bmp_px  = knst_image_loader::load_bmp(u"photo.bmp", &w, &h);
knst_byte_string gif_px  = knst_image_loader::load_gif(u"photo.gif", &w, &h);
knst_byte_string tga_px  = knst_image_loader::load_tga(u"photo.tga", &w, &h);
knst_byte_string ico_px  = knst_image_loader::load_ico(u"icon.ico", &w, &h);
knst_byte_string ppm_px  = knst_image_loader::load_ppm(u"photo.ppm", &w, &h);
```

### Legacy Flags

```cpp
// Boyut hint'i (ICO/CUR için)
KNST_BITMAP_16_16
KNST_BITMAP_24_24
KNST_BITMAP_32_32
KNST_BITMAP_48_48
KNST_BITMAP_64_64
KNST_BITMAP_96_96
KNST_BITMAP_128_128
KNST_BITMAP_256_256

// Çıkış formatı
KNST_BITMAP_OUTPUT_RGB
KNST_BITMAP_OUTPUT_RGBA
KNST_BITMAP_OUTPUT_BGR
KNST_BITMAP_OUTPUT_BGRA
```

**Örnek:**

```cpp
// 32x32 icon seç, BGRA çıkış
knst_byte_string px = knst_image_loader::load_ico(
    u"icon.ico", &w, &h,
    KNST_BITMAP_32_32 | KNST_BITMAP_OUTPUT_BGRA);
```

---

## 🛡️ Hata Yönetimi

### `knst_image_error` Enum

```cpp
enum knst_image_error : int {
    KNST_IMAGE_OK = 0,
    KNST_IMAGE_ERR_EMPTY_INPUT,      // boş yol / null veri
    KNST_IMAGE_ERR_FILE_OPEN,        // dosya açılamadı
    KNST_IMAGE_ERR_UNKNOWN_FORMAT,   // magic byte tanınmadı
    KNST_IMAGE_ERR_FORMAT_DISABLED,  // allowed_formats ile engellendi
    KNST_IMAGE_ERR_DECODE,           // corrupt / truncated
    KNST_IMAGE_ERR_LIMIT,            // max_dimension aşıldı
    KNST_IMAGE_ERR_OUT_OF_MEMORY
};
```

### Hata String'i

```cpp
knst_image img = knst_image_loader::load("bozuk.png");
if (!img) {
    std::cerr << "Hata: " << knst_image_error_string(img.error) << "\n";
}
```

### Format Kısıtlaması

```cpp
knst_image_load_options o;

// Sadece PNG ve JPEG'e izin ver
o.allowed_formats = KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_PNG) |
                    KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_JPEG);

knst_image img = knst_image_loader::load("photo.gif", o);
// img.error = KNST_IMAGE_ERR_FORMAT_DISABLED
```

**Neden?** Güvenlik — sadece bildiğin formatları kabul et. GIF gibi tehlikeli formatları (LZW exploit'leri) engelleyebilirsin.

### Boyut Limiti

```cpp
knst_image_load_options o;
o.max_dimension = 4096;   // 4096x4096'dan büyük görüntüler reddedilir

knst_image img = knst_image_loader::load("huge.png", o);
// img.error = KNST_IMAGE_ERR_LIMIT
```

**Varsayılan:** 16384 (16K).

### Thread Kontrolü

```cpp
knst_image_load_options o;
o.max_threads = 1;   // tek thread (deterministik)

knst_image img = knst_image_loader::load("photo.png", o);
```

---

## 📊 Performans Notları

### Memory-Mapped I/O

Linux'ta `mmap()`, Windows'ta `MapViewOfFile()` kullanılır. Bu sayede:

- **Büyük dosyalar bile** RAM'e tamamen yüklenmez
- **OS page cache** otomatik devreye girer
- **`madvise(MADV_SEQUENTIAL)`** (Linux) ve **`PrefetchVirtualMemory`** (Windows) ile önden okuma

### Parallel Decoding

Görüntü işleme **satır bazlı paralelleştirilir**:

```cpp
ParallelForRows(c.h, (size_t)w * 4, [](uint32_t r0, uint32_t r1) {
    // her thread farklı satırları işler
});
```

**Eşik:** 256 KB'dan büyük iş yükleri paralelleştirilir. Küçük görüntüler tek thread'de daha hızlı (thread overhead'i yüzünden).

### SIMD Swizzle

RGBA → BGRA dönüşümü **SSE2** (x86) veya **NEON** (ARM) ile yapılır:

```cpp
// SSE2: 4 piksel bir seferde (16 byte)
__m128i px = _mm_loadu_si128(...);
// ... mask ve shift işlemleri ...
```

**Kazanç:** Scalar koda göre **4-8x hızlı**.

### DECOMPRESSION BOMB Koruması

DEFLATE çözücüde **100 MB** output limiti vardır (eski API'de). Bu, kötü niyetli bir PNG'nin RAM'i tüketmesini engeller.

Yeni API'de `max_dimension` ve `expected` boyut hesabı ile korunur.

### Ne Zaman Yavaş?

- **Çok büyük görüntüler** (100 MP+)
- **Lanczos3 filter** (matematiksel olarak ağır)
- **Premultiply + resize + BGRA** birlikte (3 pass)
- **Progressive JPEG** (baseline'dan yavaş)

---

## 💡 Kullanım Örnekleri

### Örnek 1: Thumbnail Üretimi

```cpp
knst_image make_thumbnail(const char* path) {
    knst_image_load_options o;
    o.resize_width  = 256;
    o.resize_height = 256;
    o.keep_aspect   = true;
    o.resize_filter = KNST_IMAGE_RESIZE_LANCZOS3;

    return knst_image_loader::load(path, o);
}
```

### Örnek 2: Icon Seçimi

```cpp
// ICO'dan 32x32 entry'i seç
knst_image_load_options o;
o.target_size = 32;

knst_image icon = knst_image_loader::load("app.ico", o);
```

### Örnek 3: Texture Yükleme (GPU)

```cpp
knst_image_load_options o;
o.output_format     = KNST_BITMAP_OUTPUT_RGBA;
o.flip_vertical     = true;   // OpenGL
o.premultiply_alpha = true;   // blending

knst_image tex = knst_image_loader::load("sprite.png", o);

// Artık doğrudan GPU'ya yüklenebilir
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height,
             0, GL_RGBA, GL_UNSIGNED_BYTE, tex.pixels.data());
```

### Örnek 4: Format-Agnostik Yükleme

```cpp
knst_image load_any(const char* path) {
    // Format kısıtlaması yok — ne olursa kabul et
    knst_image img = knst_image_loader::load(path);

    if (!img) {
        std::cerr << "Desteklenmeyen format: " << path << "\n";
    }
    return img;
}
```

### Örnek 5: Bellekten Yükleme (Network)

```cpp
void on_http_response(const uint8_t* data, size_t size) {
    knst_image_load_options o;
    o.max_dimension = 2048;   // güvenlik limiti

    knst_image img = knst_image_loader::load_from_memory(data, size, o);

    if (img) {
        // ... işle ...
    }
}
```

### Örnek 6: Custom Decoder (WebP)

```cpp
static bool qoi_sniff(const uint8_t* d, size_t n) {
    return n >= 4 && std::memcmp(d, "qoif", 4) == 0;
}

static bool qoi_decode(const uint8_t* d, size_t n,
                       knst_image_decode_result& r) {
    // QOI header: magic(4) + width(4) + height(4) + channels(1) + colorspace(1)
    uint32_t w = (d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7];
    uint32_t h = (d[8] << 24) | (d[9] << 16) | (d[10] << 8) | d[11];

    if (!knst_image_loader::allocate_rgba(r, w, h)) return false;

    // ... QOI pixel decoding ...
    return true;
}

int main() {
    knst_image_loader::register_decoder(qoi_sniff, qoi_decode);

    knst_image img = knst_image_loader::load("image.qoi");
    return 0;
}
```

---

## 🌍 Platform Desteği

| Platform | Memory Mapping | SIMD |
|---|---|---|
| **Linux** | `mmap` + `madvise` | SSE2 / NEON |
| **Windows** | `MapViewOfFile` + `PrefetchVirtualMemory` | SSE2 |
| **Android** | `mmap` + `madvise` | NEON |

**Hepsi aynı API'yi kullanır** — kod değişmez.
