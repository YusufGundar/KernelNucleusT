# knst_image_loader — Self-Contained Image Loader

Hello! 👋 This document explains what the `knst_image_loader` class is, how to use it, and why it exists in KernelNucleusT.

In short: **an image loader that requires no external library (no zlib/libpng/libjpeg), containing its own DEFLATE / JPEG / GIF / BMP / TGA / PNM / ICO decoders.** The format is detected from **magic bytes**, not from the file extension.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Self-contained** | No external library needed — no zlib, libpng, libjpeg |
| **Own DEFLATE** | Decompresses PNG compression itself |
| **Own JPEG** | Baseline + progressive, Huffman, CMYK, YCCK, EXIF orientation |
| **Multiple formats** | PNG, JPEG, GIF, BMP, TGA, PNM, ICO/CUR + custom decoder |
| **Magic byte detection** | Does not trust file extensions — reads the real format |
| **Memory-mapped I/O** | Fast even for large files (mmap/MapViewOfFile) |
| **Multi-threaded** | SIMD-friendly processing via `ParallelForRows` |
| **SIMD swizzle** | RGBA ↔ BGRA conversion via SSE2/NEON |
| **Rich options** | Resize, flip, premultiply, flatten, EXIF orientation |
| **5 resample filters** | Nearest, Box, Bilinear, Bicubic, Lanczos3 |
| **Pool allocator** | Custom memory management with `knst_pool_allocator` |
| **Custom decoder** | Add your own formats like WebP, QOI, TIFF |
| **Legacy API** | The old `load_image()` signature still works |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Load a PNG file
    knst_image img = knst_image_loader::load("photo.png");

    if (!img) {
        std::cerr << "Failed to load: "
                  << knst_image_error_string(img.error) << "\n";
        return 1;
    }

    std::cout << "Size    : " << img.width << " x " << img.height << "\n";
    std::cout << "Channels: " << img.channels << "\n";
    std::cout << "Format  : "
              << knst_image_loader::format_name(img.source_format) << "\n";

    return 0;
}
```

---

## 🖼️ Supported Formats

### PNG

- **Color types:** Gray, RGB, Palette, Gray+Alpha, RGBA
- **Bit depths:** 1, 2, 4, 8, 16
- **Interlace:** Adam7 (all 7 passes)
- **Transparency:** `tRNS` chunk (palette + color-key)
- **DEFLATE:** Own inflate implementation

### JPEG

- **Baseline** + **Extended sequential** + **Progressive**
- **Encoding:** Huffman (no arithmetic)
- **Color spaces:** Gray, YCbCr, RGB, CMYK, YCCK
- **Subsampling:** All layouts (4:4:4, 4:2:2, 4:2:0, ...)
- **Upsampling:** libjpeg-compatible fancy upsampling
- **EXIF orientation:** Tag 0x0112 (1-8)
- **Restart intervals**

### GIF

- **Versions:** GIF87a, GIF89a
- **First frame** is decoded (no animation support)
- **Transparency** (GIF89a)
- **Interlace** support
- **LZW decompression** — own implementation

### BMP

- **Bit depths:** 1, 2, 4, 8, 16, 24, 32
- **RLE4** + **RLE8** compression
- **BITFIELDS** (16/32-bit custom mask)
- **OS/2 core** header + **INFO** / **V4** / **V5**
- **Top-down** and **bottom-up** orientation
- **Embedded PNG/JPEG** (compression 4/5)

### TGA

- **Types:** 1, 2, 3, 9, 10, 11 (palette/truecolor/gray + RLE)
- **Bit depths:** 8, 15, 16, 24, 32
- **All orientation bits** (top/bottom, left/right)

### PNM (PBM / PGM / PPM / PAM)

- **P1-P6:** ASCII + binary
- **P7 (PAM):** Header-based format
- **maxval:** 1..65535
- **Comments** (`#`)

### ICO / CUR

- **Best entry selection** (`target_size` option)
- **PNG entry** support
- **BMP (DIB + AND mask)** support
- **Cursor hotspot** reading

### Custom Decoder

Register your own decoder with `register_decoder()`:

```cpp
knst_image_loader::register_decoder(my_sniff_fn, my_decode_fn);
```

---

## 🎯 Format Detection

The format is detected from **magic bytes** — file extensions are not trusted.

```cpp
knst_byte_string bytes = knst_file::read_file_data<>("unknown.dat");

int fmt = knst_image_loader::detect_format(bytes.data(), bytes.length());
std::cout << "Format: " << knst_image_loader::format_name(fmt) << "\n";
```

### Supported Format Constants

| Constant | Description |
|---|---|
| `KNST_IMAGE_FORMAT_UNKNOWN` | Unknown |
| `KNST_IMAGE_FORMAT_PNG` | PNG |
| `KNST_IMAGE_FORMAT_JPEG` | JPEG |
| `KNST_IMAGE_FORMAT_GIF` | GIF |
| `KNST_IMAGE_FORMAT_BMP` | BMP |
| `KNST_IMAGE_FORMAT_ICO` | ICO / CUR |
| `KNST_IMAGE_FORMAT_TGA` | TGA |
| `KNST_IMAGE_FORMAT_PNM` | PBM / PGM / PPM / PAM |
| `KNST_IMAGE_FORMAT_CUSTOM` | Custom decoder |

---

## 📦 `knst_image_t` — Result Structure

```cpp
template<typename Alloc = knst_default_allocator>
struct knst_image_t {
    basic_byte_string<Alloc> pixels;   // width * height * channels bytes
    uint32_t width         = 0;
    uint32_t height        = 0;
    int      channels      = 0;        // 3 (RGB/BGR) or 4 (RGBA/BGRA)
    int      output_format = 0;        // KNST_BITMAP_OUTPUT_*
    int      source_format = KNST_IMAGE_FORMAT_UNKNOWN;
    int      hotspot_x     = -1;       // for ICO/CUR
    int      hotspot_y     = -1;
    int      error         = KNST_IMAGE_OK;

    bool   valid() const noexcept;     // is it valid?
    explicit operator bool() const;    // if (img) check
    size_t stride() const noexcept;    // width * channels
    size_t byte_size() const noexcept; // stride * height
};
```

### Usage

```cpp
knst_image img = knst_image_loader::load("photo.png");

if (img) {                             // ✅ validity check
    std::cout << img.width << "x" << img.height << "\n";
    std::cout << "Total bytes: " << img.byte_size() << "\n";
    std::cout << "Stride: " << img.stride() << "\n";

    // Pixel access
    const uint8_t* px = img.pixels.data();
    // In RGBA format: px[0]=R, px[1]=G, px[2]=B, px[3]=A
}
```

---

## 🛠️ Loading APIs

### Loading from a File

```cpp
// With knst_c16string
knst_c16string path = u"/home/user/photo.png";
knst_image img1 = knst_image_loader::load(path);

// With UTF-8 char*
knst_image img2 = knst_image_loader::load("/home/user/photo.png");

// With char16_t*
knst_image img3 = knst_image_loader::load(u"/home/user/photo.png");
```

### Loading from Memory

```cpp
knst_byte_string bytes = knst_file::read_file_data<>("photo.png");
knst_image img = knst_image_loader::load_from_memory(
    bytes.data(), bytes.length());
```

### Loading with Options

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

Load options — every field has a sensible default.

| Field | Default | Description |
|---|---|---|
| `output_format` | `KNST_BITMAP_OUTPUT_RGBA` | Output format (RGB/RGBA/BGR/BGRA) |
| `target_size` | `0` | ICO/CUR: choose nearest size (0 = largest) |
| `resize_width` | `0` | 0 = keep. If only one is set, aspect ratio is preserved |
| `resize_height` | `0` | |
| `keep_aspect` | `false` | If both are set: fit inside the box instead of stretching |
| `resize_filter` | `KNST_IMAGE_RESIZE_CATMULL_ROM` | Resample filter |
| `flip_vertical` | `false` | For OpenGL-style (bottom-left origin) |
| `flatten_alpha` | `false` | Composite over `background_*`, make opaque |
| `background_r/g/b` | `0` | Flatten background color |
| `premultiply_alpha` | `false` | Output premultiplied (RGBA/BGRA) |
| `apply_exif_orientation` | `true` | JPEG only |
| `max_dimension` | `0` | 0 = `KNST_IMAGE_MAX_DIMENSION` (16384) |
| `max_threads` | `0` | 0 = automatic, 1 = single thread |
| `allowed_formats` | All | Format restriction via bit mask |

---

## 🔄 Output Formats

```cpp
#define KNST_BITMAP_OUTPUT_RGB   (1 << 8)   // 3 channels
#define KNST_BITMAP_OUTPUT_RGBA  (1 << 9)   // 4 channels
#define KNST_BITMAP_OUTPUT_BGR   (1 << 10)  // 3 channels
#define KNST_BITMAP_OUTPUT_BGRA  (1 << 11)  // 4 channels
```

**Usage:**

```cpp
knst_image_load_options opts;
opts.output_format = KNST_BITMAP_OUTPUT_BGRA;   // for OpenGL/Vulkan
knst_image img = knst_image_loader::load("photo.png", opts);
```

**Difference:**
- **RGBA:** Red-Green-Blue-Alpha (web standard)
- **BGRA:** Blue-Green-Red-Alpha (Windows/GDI, OpenGL, Vulkan)

**Note:** RGBA → BGRA conversion is accelerated with **SSE2 / NEON** SIMD.

---

## 🔍 5 Resample Filters

```cpp
enum knst_image_resize_filter : int {
    KNST_IMAGE_RESIZE_NEAREST     = 0,  // pixel art
    KNST_IMAGE_RESIZE_BOX         = 1,  // area average (good for downscaling)
    KNST_IMAGE_RESIZE_TRIANGLE    = 2,  // bilinear
    KNST_IMAGE_RESIZE_CATMULL_ROM = 3,  // bicubic (sharp)
    KNST_IMAGE_RESIZE_LANCZOS3    = 4   // best quality, mild ringing
};
```

### Comparison

| Filter | Quality | Speed | When to use? |
|---|---|---|---|
| **Nearest** | Low | Very fast | Pixel art, icon downscaling |
| **Box** | Medium | Fast | Thumbnail generation (downscale) |
| **Triangle** | Good | Medium | General-purpose bilinear |
| **Catmull-Rom** | Very good | Slow | Photo upscaling (default) |
| **Lanczos3** | Best | Slowest | High-quality thumbnails |

**Usage:**

```cpp
opts.resize_filter = KNST_IMAGE_RESIZE_LANCZOS3;
```

---

## 🎨 Transformations

### Resize

```cpp
knst_image_load_options o;

// Preserve aspect ratio
o.resize_width  = 512;
o.resize_height = 512;
o.keep_aspect   = true;   // fits inside 512x512 box

knst_image thumb = knst_image_loader::load("photo.png", o);
```

**Note:** If only `resize_width` is provided, `resize_height` is **automatically** computed (and vice versa).

### Flip Vertical

```cpp
knst_image_load_options o;
o.flip_vertical = true;  // for OpenGL textures

knst_image img = knst_image_loader::load("photo.png", o);
```

### Premultiply Alpha

```cpp
knst_image_load_options o;
o.premultiply_alpha = true;  // ready for the render pipeline

knst_image img = knst_image_loader::load("photo.png", o);
```

**What is premultiply?** Color channels are multiplied by alpha: `RGB *= A/255`. It is a format optimized for GPU blending.

### Flatten Alpha

```cpp
knst_image_load_options o;
o.flatten_alpha = true;
o.background_r  = 255;   // white background
o.background_g  = 255;
o.background_b  = 255;

knst_image img = knst_image_loader::load("transparent.png", o);
// Transparent pixels are painted white, alpha=255
```

### EXIF Orientation

```cpp
knst_image_load_options o;
o.apply_exif_orientation = true;   // default
```

**What it does:** Automatically rotates the image according to the orientation value (1-8) stored in the JPEG's EXIF. Without this, phone photos appear sideways.

| Orientation | What it does |
|---|---|
| 1 | Normal |
| 2 | Horizontal flip |
| 3 | 180° rotate |
| 4 | Vertical flip |
| 5 | 90° CW + flip |
| 6 | 90° CW |
| 7 | 90° CCW + flip |
| 8 | 90° CCW |

---

## 🧠 Pool Allocator

All `load` functions support a **template allocator**. The default is `knst_default_allocator` (malloc/free).

```cpp
// Default (malloc)
knst_image img1 = knst_image_loader::load("photo.png");

// With pool allocator
auto img2 = knst_image_loader::load<knst_pool_allocator>("photo.png");

// With options
knst_image_load_options o;
o.resize_width  = 256;
o.resize_height = 256;

auto img3 = knst_image_loader::load<knst_pool_allocator>("photo.png", o);

// From memory
auto img4 = knst_image_loader::load_from_memory<knst_pool_allocator>(
    bytes.data(), bytes.length(), o);
```

**When should you use the pool?**
- ✅ When loading many small images (icons, thumbnails)
- ✅ Game engine asset pipeline
- ✅ Server-side image processing
- ❌ Occasional single large image (malloc is more suitable)

**The type changes:** `knst_image` = `knst_image_t<knst_default_allocator>`, `knst_image_t<knst_pool_allocator>` is a different type.

---

## 🔌 Custom Decoder (Extensibility)

You can add your own format — WebP, QOI, TIFF, AVIF...

### 1. Sniff Function

```cpp
static bool webp_sniff(const uint8_t* data, size_t size) {
    return size >= 12 &&
           std::memcmp(data, "RIFF", 4) == 0 &&
           std::memcmp(data + 8, "WEBP", 4) == 0;
}
```

### 2. Decode Function

```cpp
static bool webp_decode(const uint8_t* data, size_t size,
                        knst_image_decode_result& result) {
    // ... decode WebP ...
    // result.rgba must be filled: width * height * 4 bytes, RGBA8
    // result.width, result.height must be set

    if (!knst_image_loader::allocate_rgba(result, w, h)) {
        return false;
    }
    // Write pixel data into result.rgba
    return true;
}
```

### 3. Register

```cpp
int main() {
    knst_image_loader::register_decoder(webp_sniff, webp_decode);

    knst_image img = knst_image_loader::load("photo.webp");
    // WebP is now recognized automatically

    return 0;
}
```

### When Should You Use a Custom Decoder?

- ✅ Modern formats (WebP, AVIF, QOI)
- ✅ If you have defined your own format
- ✅ If you want to override a built-in decoder

**Note:** Custom decoders are tried **before** the built-ins.

### Limit

By default 16 custom decoders can be registered:

```cpp
#define KNST_IMAGE_MAX_CUSTOM_DECODERS 16
```

---

## 📜 Legacy API

For backward compatibility, `load_image` and its siblings are preserved:

```cpp
int w = 0, h = 0;
knst_byte_string pixels = knst_image_loader::load_image(
    u"photo.png", &w, &h, KNST_BITMAP_OUTPUT_RGBA);

// or format-specific
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
// Size hint (for ICO/CUR)
KNST_BITMAP_16_16
KNST_BITMAP_24_24
KNST_BITMAP_32_32
KNST_BITMAP_48_48
KNST_BITMAP_64_64
KNST_BITMAP_96_96
KNST_BITMAP_128_128
KNST_BITMAP_256_256

// Output format
KNST_BITMAP_OUTPUT_RGB
KNST_BITMAP_OUTPUT_RGBA
KNST_BITMAP_OUTPUT_BGR
KNST_BITMAP_OUTPUT_BGRA
```

**Example:**

```cpp
// Choose the 32x32 icon, output BGRA
knst_byte_string px = knst_image_loader::load_ico(
    u"icon.ico", &w, &h,
    KNST_BITMAP_32_32 | KNST_BITMAP_OUTPUT_BGRA);
```

---

## 🛡️ Error Handling

### `knst_image_error` Enum

```cpp
enum knst_image_error : int {
    KNST_IMAGE_OK = 0,
    KNST_IMAGE_ERR_EMPTY_INPUT,      // empty path / null data
    KNST_IMAGE_ERR_FILE_OPEN,        // file could not be opened
    KNST_IMAGE_ERR_UNKNOWN_FORMAT,   // magic byte not recognized
    KNST_IMAGE_ERR_FORMAT_DISABLED,  // blocked by allowed_formats
    KNST_IMAGE_ERR_DECODE,           // corrupt / truncated
    KNST_IMAGE_ERR_LIMIT,            // max_dimension exceeded
    KNST_IMAGE_ERR_OUT_OF_MEMORY
};
```

### Error String

```cpp
knst_image img = knst_image_loader::load("broken.png");
if (!img) {
    std::cerr << "Error: " << knst_image_error_string(img.error) << "\n";
}
```

### Format Restriction

```cpp
knst_image_load_options o;

// Only allow PNG and JPEG
o.allowed_formats = KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_PNG) |
                    KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_JPEG);

knst_image img = knst_image_loader::load("photo.gif", o);
// img.error = KNST_IMAGE_ERR_FORMAT_DISABLED
```

**Why?** Security — accept only the formats you know. You can block dangerous formats like GIF (LZW exploits).

### Size Limit

```cpp
knst_image_load_options o;
o.max_dimension = 4096;   // images larger than 4096x4096 are rejected

knst_image img = knst_image_loader::load("huge.png", o);
// img.error = KNST_IMAGE_ERR_LIMIT
```

**Default:** 16384 (16K).

### Thread Control

```cpp
knst_image_load_options o;
o.max_threads = 1;   // single thread (deterministic)

knst_image img = knst_image_loader::load("photo.png", o);
```

---

## 📊 Performance Notes

### Memory-Mapped I/O

Uses `mmap()` on Linux and `MapViewOfFile()` on Windows. This means:

- **Even large files** are not fully loaded into RAM
- **OS page cache** engages automatically
- **`madvise(MADV_SEQUENTIAL)`** (Linux) and **`PrefetchVirtualMemory`** (Windows) prefetch data

### Parallel Decoding

Image processing is **parallelized row by row**:

```cpp
ParallelForRows(c.h, (size_t)w * 4, [](uint32_t r0, uint32_t r1) {
    // each thread handles a different range of rows
});
```

**Threshold:** Workloads larger than 256 KB are parallelized. Small images are faster single-threaded (thread overhead).

### SIMD Swizzle

RGBA → BGRA conversion is done via **SSE2** (x86) or **NEON** (ARM):

```cpp
// SSE2: 4 pixels at a time (16 bytes)
__m128i px = _mm_loadu_si128(...);
// ... mask and shift operations ...
```

**Gain:** **4-8x faster** than scalar code.

### DECOMPRESSION BOMB Protection

The DEFLATE decoder has a **100 MB** output limit (in the legacy API). This prevents a malicious PNG from exhausting RAM.

The new API guards via `max_dimension` and expected size computation.

### When Is It Slow?

- **Very large images** (100 MP+)
- **Lanczos3 filter** (mathematically heavy)
- **Premultiply + resize + BGRA** together (3 passes)
- **Progressive JPEG** (slower than baseline)

---

## 💡 Usage Examples

### Example 1: Thumbnail Generation

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

### Example 2: Icon Selection

```cpp
// Choose the 32x32 entry from an ICO
knst_image_load_options o;
o.target_size = 32;

knst_image icon = knst_image_loader::load("app.ico", o);
```

### Example 3: Texture Loading (GPU)

```cpp
knst_image_load_options o;
o.output_format     = KNST_BITMAP_OUTPUT_RGBA;
o.flip_vertical     = true;   // OpenGL
o.premultiply_alpha = true;   // blending

knst_image tex = knst_image_loader::load("sprite.png", o);

// Now it can be uploaded directly to the GPU
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height,
             0, GL_RGBA, GL_UNSIGNED_BYTE, tex.pixels.data());
```

### Example 4: Format-Agnostic Loading

```cpp
knst_image load_any(const char* path) {
    // No format restriction — accept whatever comes
    knst_image img = knst_image_loader::load(path);

    if (!img) {
        std::cerr << "Unsupported format: " << path << "\n";
    }
    return img;
}
```

### Example 5: Loading from Memory (Network)

```cpp
void on_http_response(const uint8_t* data, size_t size) {
    knst_image_load_options o;
    o.max_dimension = 2048;   // security limit

    knst_image img = knst_image_loader::load_from_memory(data, size, o);

    if (img) {
        // ... process ...
    }
}
```

### Example 6: Custom Decoder (WebP / QOI)

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

## 🌍 Platform Support

| Platform | Memory Mapping | SIMD |
|---|---|---|
| **Linux** | `mmap` + `madvise` | SSE2 / NEON |
| **Windows** | `MapViewOfFile` + `PrefetchVirtualMemory` | SSE2 |
| **Android** | `mmap` + `madvise` | NEON |

**All of them use the same API** — the code does not change.