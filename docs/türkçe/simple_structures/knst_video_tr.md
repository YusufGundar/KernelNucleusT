# knst_video — Video & Ses Container/Codec Framework'ü

Selamlar! 👋 `knst_vid` namespace'i, FFmpeg'siz, harici codec kütüphanesi olmayan, tek header'da gelen bir **video + ses framework'ü**. AVI/WAV/RAW/MKV/MP4/MOV container'ları yazar-okur. MJPEG encoder'ı kodun içinde — sıfırdan baseline JPEG üretir. Diğer codec'ler (H.264, H.265, VP8, VP9, AV1, ProRes) passthrough olarak taşınır.

---

## 🎯 Honesty Matrix — Neyin Gerçek Olduğu

### ✅ Gerçek (kodun içinde, sıfır bağımlılık)

| Container | Yaz | Oku | Ne |
|---|---|---|---|
| **AVI** | ✅ | ✅ | MJPEG, raw DIB (BGR24/BGRA32/GREY8), passthrough, PCM, INFO, crash recovery |
| **MKV** | ✅ | ✅ | MJPEG, passthrough (CodecPrivate'lı), PCM, Cues, chapters. WebM okunur |
| **MP4** | ✅ | ✅ | MJPEG (`mp4v`), `avc1`/`hvc1` passthrough. `moov` sonda |
| **MOV** | ✅ | ✅ | MJPEG (`jpeg`), PCM_S16LE (`sowt`) |
| **WAV** | ✅ | ✅ | PCM U8/S16/S24/S32/F32, EXTENSIBLE, LIST/INFO |
| **RAW** | ✅ | ✅ | Headerless pixel dump |

**Codec'ler:** MJPEG encoder (sıfırdan baseline), MJPEG decoder (image_loader + otomatik DHT injection), RAW ↔ RGB24 (BT.601/709, full/limited).

**Araçlar:** resize (nearest/bilinear/box), crop, rotate (90/180), flip, brightness/contrast, grey, Bayer demosaic, PCM conversion + resampler, SRT/VTT/ASS read+write, motion detector, timelapse, motion-triggered recorder with pre-roll, JSON export.

### ⚠️ Passthrough (sen encode/decode edersin)

H.264, H.265, VP8, VP9, AV1, MPEG-4, ProRes, DNxHD — paket olarak taşınır, decode edilmez. `write_encoded()` ile kendi encoder çıktını ver. `read_frame()` ile ham paketi al. İstersen `external_codec::register_video_decoder()` ile kendi decoder'ını bağla.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Zero-dependency** | FFmpeg yok, sadece bu header |
| **MJPEG encoder** | Sıfırdan baseline JPEG (quality 1-100) |
| **MJPEG decoder** | image_loader + eksik DHT otomatik inject |
| **6 container** | AVI/WAV/RAW/MKV/MP4/MOV (WebM okunur) |
| **Passthrough** | H.264/H.265/VP8/VP9/AV1/ProRes container'a gömülür |
| **PCM audio** | U8/S16/S24/S32/F32, resample + remix |
| **Görüntü araçları** | resize, crop, rotate, flip, brightness/contrast, Bayer demosaic |
| **Altyazı** | SRT/VTT/ASS read+write, timing shift |
| **Motion detector** | Blok bazlı, timelapse gate, pre-roll recorder |
| **External codec** | İstersen kendi H.264 decoder'ını bağla |
| **Crash recovery** | AVI writer öldüyse dosya hâlâ okunabilir |

---

## 🚀 Hızlı Başlangıç

### Kayıt

```cpp
#include "KernelNucleusT.hpp"

knst_vid::video_writer w;
knst_vid::video_writer::config cfg;
cfg.cont = knst_vid::container::AVI;
cfg.vcodec = knst_vid::video_codec::MJPEG;
cfg.width = 640; cfg.height = 480; cfg.fps_num = 30;
cfg.jpeg_quality = 85;
cfg.acodec = knst_vid::audio_codec::PCM_S16LE;
cfg.sample_rate = 48000; cfg.channels = 2;

auto r = w.open(u"out/test.avi", cfg);
if (!r) { std::cerr << w.last_error_string() << "\n"; return 1; }

knst_byte_string rgb;  // 640*480*3
for (int i = 0; i < 150; ++i) {
    w.write_frame_rgb24(rgb.data());
    w.write_audio(pcm.data(), 1600 * 4);
}
w.close();
```

### Okuma

```cpp
knst_vid::video_reader rd;
if (rd.open(u"out/test.avi")) {
    auto f = rd.read_frame_rgb24(10);   // RGB24, width/height dolu
    auto a = rd.read_audio_all();        // PCM
}
```

### H.264 passthrough

```cpp
cfg.cont = knst_vid::container::MKV;
cfg.vcodec = knst_vid::video_codec::H264;
cfg.vextradata = avcC_bytes;             // CodecPrivate
w.open(u"out/clip.mkv", cfg);
w.write_encoded(nal_data, nal_size, is_key, pts_us);
```

---

## 📋 Genel API

| Kategori | Metotlar |
|---|---|
| **Video yazma** | `open`, `write_frame`, `write_frame_rgb24`, `write_encoded`, `close` |
| **Ses yazma** | `write_audio`, `write_audio_s16`, `write_audio_f32` |
| **Video okuma** | `open`, `read_frame`, `read_frame_rgb24`, `read_packet` |
| **Ses okuma** | `read_audio`, `read_audio_all` |
| **Durum** | `frames_written`, `audio_frames_written`, `pts_next_us`, `bytes_written`, `last_error` |
| **Metadata** | `kind`, `info`, `meta`, `duration_us`, `stream_count`, `stream`, `video_stream`, `audio_stream` |
| **Recorder** | `start`, `feed_rgb24`, `stop`, `recording`, `clips_written` |
| **Görüntü** | `img::resize_rgb24`, `crop_rgb24`, `rotate_90_rgb24`, `flip_h_rgb24`, `adjust_rgb24`, `to_grey_rgb24` |
| **Ses** | `audio::convert`, `remix`, `resample`, `peak` |
| **Altyazı** | `sub::srt/vtt/ass::read/write`, `sub::shift` |
| **Motion** | `motion_detector`, `timelapse` |
| **External codec** | `external_codec::register_video_decoder` vs. |

---

## 🔧 1) Config Macro'ları

Header'ı include etmeden **önce**:

```cpp
#define KNST_VIDEO_MAX_DIMENSION       8192u    // default 16384
#define KNST_VIDEO_MAX_SAMPLE_BYTES    (128u * 1024u * 1024u)  // default 256 MiB
#define KNST_VIDEO_MAX_SAMPLES         (16u * 1024u * 1024u)   // default 64M
#define KNST_VIDEO_MAX_TRACKS          8u        // default 16
#define KNST_VIDEO_HAS_IMAGE_LOADER    1         // default 1
#define KNST_VIDEO_FILE_BUFFER         (512u * 1024u)  // default 256 KiB
```

`KNST_VIDEO_HAS_IMAGE_LOADER=0` → MJPEG decoder devre dışı. Encoder çalışır, `read_frame_rgb24()` MJPEG için `NotSupported` döner.

---

## ⚠️ 2) Hata Yönetimi

`knst_vid::errc` enum'u:

| Hata | Anlam |
|---|---|
| `Ok` | Başarılı |
| `InvalidArgument` | Parametre yanlış |
| `NotSupported` | Format/codec desteklenmiyor |
| `NotFound` | Track yok / index aralık dışı |
| `IoError` | Dosya I/O hatası |
| `OutOfMemory` | Allocation başarısız |
| `BadState` | `open()` yok / dosya kapalı |
| `Corrupt` | Container/bitstream bozuk |
| `TooLarge` | `KNST_VIDEO_MAX_*` sınırı |
| `EndOfStream` | Dosya bitti |

```cpp
auto r = w.open(u"out/test.avi", cfg);
if (!r) std::cerr << w.last_error_string() << "\n";
```

`result<T>` ve `status` `knst_dev` namespace'inden geliyor.

---

## 🎬 3) `video_writer` — Yüksek Seviye Yazıcı

### `config`

```cpp
struct video_writer::config {
    container cont = container::AVI;
    video_codec vcodec = video_codec::MJPEG;
    bool has_video = true;
    pixel_format pix = pixel_format::RGB24;
    uint32_t width = 0, height = 0;
    uint32_t fps_num = 30, fps_den = 1;
    int jpeg_quality = 85;
    mjpeg::subsampling jpeg_sub = mjpeg::subsampling::s420;
    uint32_t jpeg_restart = 0;
    pixel_format raw_pix = pixel_format::Unknown;
    bool scale_to_fit = false;
    bool fill_gaps = true;
    color::yuv_params yuv;
    audio_codec acodec = audio_codec::None;
    uint32_t sample_rate = 0, channels = 0;
    knst_byte_string vextradata, aextradata;
    metadata meta;
};
```

### Container başına destek

| Container | vcodec | acodec | Not |
|---|---|---|---|
| AVI | MJPEG/RAW/passthrough | PCM | Video zorunlu |
| MKV | MJPEG/passthrough | PCM/passthrough | WebM okunur, yazılamaz |
| MP4 | MJPEG/H.264/H.265 | yok | `moov` sonda |
| MOV | MJPEG/H.264/H.265 | PCM_S16LE | QuickTime |
| WAV | yok (`has_video=false`) | PCM | Ses zorunlu |
| RAW | RAW (herhangi pix) | yok | Headerless |

### `write_frame()` — Herhangi bir pixel format

```cpp
w.write_frame(rgb, w, h, pixel_format::RGB24);
```

- `pix` MJPEG/Unknown olamaz
- `w, h` farklıysa: `scale_to_fit=true` otomatik resize, `false` → `InvalidArgument`
- `pts_us=AUTO_PTS` → `frame_index / fps`
- Explicit pts: `w.write_frame(rgb, w, h, pix, 0, 1234567)`

### `write_encoded()` — Kendi encode ettiğin paket

```cpp
w.write_encoded(nal_data, nal_size, is_key, pts_us);
```

- MJPEG için: JPEG byte'ları (DHT eksikse **otomatik inject**)
- H.264 için: access unit
- `dts_us` sadece MP4/MOV'da (B-frame stream için)

> ⚠️ Bazı kameralar JPEG'i **DHT olmadan** yazar. `write_encoded()` bunu otomatik fix eder, çıktı her oynatıcıda çalışır.

### `write_audio()` — Interleaved PCM

```cpp
w.write_audio(pcm_bytes, byte_count, pts_us);
w.write_audio_s16(int16_data, sample_count);
w.write_audio_f32(float_data, sample_count);
```

`bytes % audio_align_ != 0` → `InvalidArgument`.

### `AUTO_PTS`

`~0ull` — pts otomatik hesaplanır.

---

## 📖 4) `video_reader` — Yüksek Seviye Okuyucu

```cpp
knst_vid::video_reader rd;
if (!rd.open(u"out/test.avi")) {
    std::cerr << rd.last_error_string() << "\n";
    return;
}
```

Auto-detect: `RIFF+AVI `→AVI, `RIFF+WAVE`/`RF64`→WAV, `0x1A45DFA3`→MKV/WebM, `????ftyp`/`moov`/`mdat`→MP4/MOV, hiçbiri→`NotSupported`.

Headerless: `rd.open_raw(u"frames.raw", 640, 480, pixel_format::RGB24, 30, 1);`

### Bilgi

```cpp
rd.kind(); rd.info(); rd.meta(); rd.duration_us();
rd.video_stream(); rd.audio_stream();
rd.width(); rd.height(); rd.fps();
rd.video_frames(); rd.audio_frames();
```

### `read_frame(idx)` — Native payload

```cpp
auto f = rd.read_frame(10);
// f.value.data → JPEG bytes (MJPEG) / DIB rows (RAW) / NAL (passthrough)
// f.value.pix → MJPEG / RAW pix / Unknown
```

### `read_frame_rgb24(idx)` — Decode edilmiş RGB

```cpp
auto f = rd.read_frame_rgb24(10);
// f.value.data → tightly packed RGB24
```

- MJPEG → image_loader
- RAW → convert_to_rgb24 + bottom-up flip
- Diğer codec'ler → **`external_codec::register_video_decoder` gerekli**

**Dropped frame:** `size=0` ise en son gerçek frame döner.

### `read_packet(track, idx, ...)` — Herhangi bir track

```cpp
knst_byte_string b;
uint64_t pts; bool key;
if (rd.read_packet(track_index, 0, &pts, &key)) { /* b → ham paket */ }
```

### `read_audio(start, count)` — PCM aralık

```cpp
auto a = rd.read_audio(0, 48000);   // ilk 1 saniye @ 48 kHz
// a.value.data, a.value.frames, a.value.sample_rate, a.value.channels
```

> ⚠️ Sadece PCM. AAC/MP3/Opus için `read_packet()` + kendi decoder'ın.

### Seek

```cpp
uint64_t idx = rd.frame_at_time(2'500'000);
uint64_t kf  = rd.keyframe_at_or_before(idx);
```

---

## 🎥 5) `recorder` — Motion-Triggered Kayıt

```cpp
struct recorder::config {
    knst_c16string dir;
    knst_c16string prefix = u"clip_";
    container cont = container::AVI;
    uint32_t fps = 30;
    int jpeg_quality = 80;
    uint32_t pre_roll_ms = 2000;
    uint32_t post_roll_ms = 3000;
    uint32_t max_clip_ms = 0;
    motion_detector::config motion;
    bool timelapse_mode = false;
    uint32_t timelapse_interval_ms = 1000;
};

enum : uint32_t { EV_NONE=0, EV_CLIP_STARTED=1, EV_CLIP_FINISHED=2, EV_FRAME_WRITTEN=4 };
```

### Kullanım

```cpp
knst_vid::recorder rec;
knst_vid::recorder::config cfg;
cfg.dir = u"recordings";
cfg.fps = 30;
cfg.pre_roll_ms = 2000;
cfg.post_roll_ms = 3000;

rec.start(cfg);
for (;;) {
    auto r = rec.feed_rgb24(rgb, w, h, knst_vid::now_us());
    if (!r) break;
    if (r.value & knst_vid::recorder::EV_CLIP_STARTED) std::cout << "Başladı\n";
    if (r.value & knst_vid::recorder::EV_CLIP_FINISHED) std::cout << "Bitti\n";
}
rec.stop();
```

**Timelapse modu:** `cfg.timelapse_mode = true; cfg.timelapse_interval_ms = 1000;` — hareketi yoksay, sadece süreli frame at.

---

## 🧰 6) Alt Seviye Araçlar

### 6.1) Renk dönüşümü (`color`)

```cpp
color::convert_to_rgb24(src, pixel_format::NV12, w, h, 0, dst);
color::convert_from_rgb24(rgb, w, h, 0, pixel_format::I420, dst);
color::bayer_to_rgb24(raw, w, h, stride, pixel_format::Bayer_RGGB, dst);

color::yuv_params yp;
yp.full_range = true;   // JPEG (false = video range)
yp.bt709 = true;        // HD (false = BT.601)
```

Desteklenen: RGB24 ↔ BGR24, RGBA32, BGRA32, GREY, YUYV, UYVY, YVYU, NV12, NV21, I420, YV12, Bayer_BGGR/GBRG/GRBG/RGGB.

### 6.2) Görüntü (`img`)

```cpp
auto small = img::resize_rgb24(rgb, w, h, 320, 240, img::resize_filter::bilinear);
auto crop = img::crop_rgb24(rgb, w, h, 100, 50, 200, 150);
auto rot = img::rotate_90_rgb24(rgb, w, h, true);
auto fh = img::flip_h_rgb24(rgb, w, h);
img::adjust_rgb24(rgb_ptr, w, h, 20.0f, 1.5f);   // brightness, contrast
auto g = img::to_grey_rgb24(rgb, w, h);
```

### 6.3) Ses (`audio`)

```cpp
audio::convert_pcm(src, audio_codec::PCM_S16LE, dst, audio_codec::PCM_F32LE, count);
auto f32 = audio::convert(s16_frame, audio_codec::PCM_F32LE);
auto stereo = audio::remix(mono_frame, 2);
auto resampled = audio::resample(frame, 48000);
float peak = audio::peak(pcm_data, codec, sample_count);
```

### 6.4) MJPEG (`mjpeg`)

```cpp
auto jpg = mjpeg::encode_rgb24(rgb, w, h, 85);

mjpeg::encoder enc;
mjpeg::encoder::options opt;
opt.quality = 90;
opt.sub = mjpeg::subsampling::s444;
opt.restart_mcus = 8;
enc.configure(opt);
for (auto& frame : frames) enc.encode(frame.data, w, h, 0, pixel_format::RGB24, jpg);

auto rgb = mjpeg::decode_rgb24(jpg_data, jpg_size, out_w, out_h);
uint32_t w, h, comps;
mjpeg::probe(jpg_data, jpg_size, w, h, comps);
```

### 6.5) Altyazı (`sub`)

```cpp
auto srt = sub::srt::read(u"movie.srt");
sub::srt::write(u"out.srt", srt.value);
sub::shift(srt.value, +1500, 1.0);          // +1.5 s
sub::shift(srt.value, 0, 1.042);             // 25→23.976 fps
```

### 6.6) External codec

```cpp
external_codec::register_video_decoder(
    video_codec::H264,
    my_h264_decode_fn,    // errc (*)(void*, const stream_desc&, const uint8_t*, uint32_t, video_frame&)
    my_user_pointer
);
```

---

## 📀 7) Container Notları

- **AVI:** ~4 GB üstünde `TooLarge`. Crash recovery var. `fill_gaps=true` PTS atlarsa dropped frame ekler.
- **MKV/WebM:** WebM **yazılamaz** (VP8/VP9 encode yok). Lacing okuma desteklenmez.
- **MP4/MOV:** `moov` sonda. Fragmented MP4 (moof) **desteklenmez**. B-frame için `dts_us` ver.
- **WAV:** 6+ kanal → `WAVE_FORMAT_EXTENSIBLE`.
- **RAW:** Headerless, `open_raw()` ile.

---

## ⚠️ 8) Sık Tuzaklar

1. `write_frame()` **MJPEG pix kabul etmez** — `write_encoded()` kullan
2. `scale_to_fit=false` (default) → boyut farkı `InvalidArgument`
3. `write_audio()` **`bytes % audio_align_`** kontrolü
4. Passthrough codec için **`vextradata` zorunlu** (H.264 için `avcC`)
5. B-frame stream'de **`dts_us` vermezsen ctts yanlış**
6. `read_frame_rgb24()` passthrough için **decoder register gerekli**
7. **`close()` çağırmadan writer kapatma** (yıkıcı sessizce kapatır, hata göremezsin)
8. `read_audio_all()` dev dosyalarda **bellek patlatabilir**
9. `#undef RAW`/`None`/`DELETE` — bazı SDK'lar için otomatik, elle yapma
10. MJPEG encoder DC `±2047` / AC `±1023` clamp'liyor — bozuk dosya çıkmaz, kalite birazcık düşer
11. `motion_detector::config::block` 0 olamaz (constructor otomatik 1)
12. `recorder` AVI/MKV/MP4/MOV dışında **`NotSupported`**

---

## 📊 9) Performans

| İşlem | Karmaşıklık | Not |
|---|---|---|
| MJPEG encode | O(n) | FDCT + Huffman |
| MJPEG decode | O(n) | image_loader SIMD |
| RGB↔YUV | O(n) | Fixed-point, float yok |
| resize (bilinear) | O(dw×dh) | 4 örnek/piksel |
| resize (box) | O(src) | Tek pass |
| motion_detector | O((w/block)×(h/block)) | 8x8 default |
| read_audio | O(frames) | Binary search + memcpy |
| resample | O(out_frames) | Linear interp |

**İpuçları:**
- Encoder'ı **yeniden kullan** (loop dışında `configure()` bir kere)
- `scale_to_fit` ancak gerekiyorsa aç
- MJPEG için **`s444` seçme**, s420 daha küçük dosya

---

## 🌍 10) Platform

| Konu | Linux/macOS | Windows |
|---|---|---|
| Dosya açma | `fopen` + UTF-8 | `_wfopen` + UTF-16 |
| 64-bit seek | `fseeko`/`ftello` | `_fseeki64`/`_ftelli64` |

Header-only, include yeter.

---

## 🎯 11) Ne Zaman Kullanmalı?

### ✅ Kullan
- Ekran/kamera kaydı (MJPEG AVI/MKV/MP4)
- Basit video edit (crop, resize, rotate, flip)
- Timelapse / motion detection
- Altyazı dönüşümü (SRT↔VTT↔ASS)
- Kendi H.264 stream'ini mux etmek
- Video metadata okumak

### ❌ Kullanma
- H.264 decode (passthrough, ayrı decoder gerek)
- Fragmented MP4 (desteklenmez)
- >4 GB AVI (MKV/MP4 kullan)
- 4K real-time encode (MJPEG ağır)
- AAC/Opus decode (ayrı decoder gerek)

---

## ✅ 12) Yap / ❌ Yapma

### ✅ Yap

```cpp
// 1. Encoder'ı yeniden kullan
mjpeg::encoder enc;
enc.configure(opt);
for (auto& frame : frames) enc.encode(frame.data, w, h, 0, pixel_format::RGB24, jpg);

// 2. write_frame_rgb24() kullan (config boyutu aynıysa)
w.write_frame_rgb24(rgb.data());

// 3. Kamera ham JPEG'ini güvenle ver (DHT otomatik inject)
w.write_encoded(jpeg, size, true, pts);

// 4. Seek için keyframe'e git
uint64_t kf = rd.keyframe_at_or_before(idx);
for (uint64_t i = kf; i <= idx; ++i) rd.read_frame_rgb24(i);

// 5. B-frame H.264 için dts_us ver
w.write_encoded(nal, sz, key, pts, dts);

// 6. last_error() kontrol et
if (!w.close()) std::cerr << w.last_error_string() << "\n";
```

### ❌ Yapma

```cpp
// 1. write_frame() içinde MJPEG verme
w.write_frame(jpeg, w, h, pixel_format::MJPEG);   // ❌ write_encoded() kullan

// 2. Scale farkı (scale_to_fit=false)
w.write_frame(rgb, 320, 240, pixel_format::RGB24);   // ❌ config 640x480 ise hata

// 3. WebM yaz
cfg.cont = container::WebM;   // ❌ NotSupported

// 4. H.264 passthrough'ta vextradata vermemek
cfg.vcodec = video_codec::H264;   // vextradata YOK → ❌ InvalidArgument

// 5. B-frame'de dts vermemek
w.write_encoded(nal, sz, key, pts);   // ❌ ctts yanlış

// 6. read_frame_rgb24() passthrough codec'te decoder register etmeden
auto f = rd.read_frame_rgb24(0);   // ❌ H.264 ise NotSupported

// 7. >4 GB AVI
// ✅ MKV/MP4 kullan
```

---

## 🔥 13) Tam Örnek

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // KAYIT
    knst_vid::video_writer w;
    knst_vid::video_writer::config cfg;
    cfg.cont = knst_vid::container::MKV;
    cfg.vcodec = knst_vid::video_codec::MJPEG;
    cfg.width = 640; cfg.height = 480;
    cfg.fps_num = 30; cfg.fps_den = 1;
    cfg.jpeg_quality = 85;
    cfg.jpeg_sub = knst_vid::mjpeg::subsampling::s420;
    cfg.acodec = knst_vid::audio_codec::PCM_S16LE;
    cfg.sample_rate = 48000; cfg.channels = 2;
    cfg.meta.title = u"Test Recording";

    if (!w.open(u"out/test.mkv", cfg)) {
        std::cerr << "Open: " << w.last_error_string() << "\n";
        return 1;
    }

    knst_byte_string rgb;
    rgb.resize(640 * 480 * 3);
    for (int i = 0; i < 150; ++i) {
        std::memset(rgb.data(), (i * 5) & 0xFF, rgb.length());
        if (!w.write_frame_rgb24(rgb.data())) break;
    }
    if (!w.close()) std::cerr << "Close: " << w.last_error_string() << "\n";
    std::cout << "Yazıldı: " << w.frames_written() << " frame\n";

    // OKUMA
    knst_vid::video_reader rd;
    if (!rd.open(u"out/test.mkv")) {
        std::cerr << "Read: " << rd.last_error_string() << "\n";
        return 1;
    }
    std::cout << rd.video_frames() << " frame, " << rd.duration_us() / 1000 << " ms\n";

    auto f = rd.read_frame_rgb24(10);
    if (f) std::cout << "Frame 10: " << f.value.width << "x" << f.value.height << "\n";

    auto a = rd.read_audio(0, cfg.sample_rate);
    if (a) std::cout << "Ses: " << a.value.frames << " frame\n";

    rd.close();
    return 0;
}
```

---

## 📌 14) API Kontrat Özeti

```cpp
namespace knst_vid {

enum class errc : uint8_t { Ok, InvalidArgument, NotSupported, NotFound,
    IoError, OutOfMemory, BadState, Corrupt, TooLarge, EndOfStream };
inline const char* errc_string(errc);

enum class video_codec : uint8_t { MJPEG, RAW, H264, H265, VP8, VP9,
    AV1, MPEG4, ProRes, DNxHD, Unknown };
enum class audio_codec : uint8_t { None, PCM_S16LE, PCM_F32LE, PCM_S24LE,
    PCM_U8, PCM_S32LE, AAC, Opus, MP3, FLAC, Vorbis, Unknown };
enum class container : uint8_t { AVI, WAV, RAW, MKV, MP4, MOV, WebM, Unknown };
enum class pixel_format : uint8_t { RGB24, BGR24, RGBA32, BGRA32, GREY,
    YUYV, UYVY, YVYU, NV12, NV21, I420, YV12, MJPEG,
    Bayer_BGGR, Bayer_GBRG, Bayer_GRBG, Bayer_RGGB, Unknown };

class video_writer {
public:
    static constexpr uint64_t AUTO_PTS = ~0ull;
    struct config { /* ... */ };
    result<void> open(const knst_c16string&, const config&);
    result<void> write_frame(const uint8_t*, uint32_t, uint32_t,
        pixel_format, uint32_t = 0, uint64_t = AUTO_PTS);
    result<void> write_frame_rgb24(const uint8_t*, uint32_t = 0, uint64_t = AUTO_PTS);
    result<void> write_encoded(const uint8_t*, uint32_t, bool,
        uint64_t = AUTO_PTS, uint64_t = ~0ull);
    result<void> write_audio(const uint8_t*, uint32_t, uint64_t = AUTO_PTS);
    result<void> write_audio_s16(const int16_t*, uint32_t);
    result<void> write_audio_f32(const float*, uint32_t);
    result<void> close();
    uint64_t frames_written() const;
    uint64_t bytes_written() const;
    errc last_error() const; const char* last_error_string() const;
};

class video_reader {
public:
    result<void> open(const knst_c16string&);
    result<void> open_raw(const knst_c16string&, uint32_t, uint32_t,
        pixel_format, uint32_t = 30, uint32_t = 1, uint32_t = 0);
    result<video_frame> read_frame(uint64_t);
    result<video_frame> read_frame_rgb24(uint64_t);
    result<knst_byte_string> read_packet(uint32_t, uint64_t,
        uint64_t* = nullptr, bool* = nullptr, uint64_t* = nullptr);
    result<audio_frame> read_audio(uint64_t, uint64_t);
    result<audio_frame> read_audio_all();
    void close(); bool is_open() const;
};

class recorder {
public:
    enum : uint32_t { EV_NONE=0, EV_CLIP_STARTED=1, EV_CLIP_FINISHED=2, EV_FRAME_WRITTEN=4 };
    struct config { /* ... */ };
    result<void> start(const config&);
    result<uint32_t> feed_rgb24(const uint8_t*, uint32_t, uint32_t, uint64_t);
    result<void> stop();
    bool recording() const;
};

// Alt modüller:
namespace color, img, audio, mjpeg, sub, external_codec { ... }
class motion_detector; class timelapse;

} // namespace knst_vid
```

---

## 🎯 Size Getireceği Kazanç

`knst_vid` şu durumlarda ciddi avantaj sağlar:

- ✅ **Zero-dependency video kaydı** — FFmpeg yok, tek header
- ✅ **Ekran/kamera kaydı** — MJPEG AVI/MKV/MP4 hemen hazır
- ✅ **MJPEG encoder** — sıfırdan, kalite 1-100, s444/s422/s420, restart markers
- ✅ **MJPEG decoder** — image_loader + otomatik DHT fix
- ✅ **6 container** — AVI/WAV/RAW/MKV/MP4/MOV (+WebM okunur)
- ✅ **Passthrough** — kendi H.264 stream'ini container'a göm
- ✅ **PCM audio** — U8/S16/S24/S32/F32, resample + remix
- ✅ **Görüntü araçları** — resize, crop, rotate, flip, brightness/contrast
- ✅ **Bayer demosaic** — kamera sensöründen direkt RGB
- ✅ **Altyazı** — SRT/VTT/ASS read+write, timing shift
- ✅ **Motion detector** — pre-roll tetikleme, timelapse
- ✅ **Crash recovery** — AVI writer öldüyse dosya hâlâ okunabilir
- ✅ **External codec** — H.264 decoder'ını `register_video_decoder` ile bağla
- ✅ **JSON export** — `KNST_JSON_HPP` include edildiyse hazır
- ✅ **KernelNucleusT uyumu** — `knst_c16string`, `knst_byte_string`, `knst_file`, `knst_image_loader` ile tam entegre

---