# knst_video — Video & Audio Container/Codec Framework

Hey! 👋 The `knst_vid` namespace is a **video + audio framework** that comes in a single header — no FFmpeg, no external codec libraries. It writes and reads AVI/WAV/RAW/MKV/MP4/MOV containers. The MJPEG encoder is built in — from scratch, producing baseline JPEG. Other codecs (H.264, H.265, VP8, VP9, AV1, ProRes) are carried as passthrough.

---

## 🎯 Honesty Matrix — What's Real

### ✅ Real (in the code, zero dependencies)

| Container | Write | Read | What |
|---|---|---|---|
| **AVI** | ✅ | ✅ | MJPEG, raw DIB (BGR24/BGRA32/GREY8), passthrough, PCM, INFO, crash recovery |
| **MKV** | ✅ | ✅ | MJPEG, passthrough (with CodecPrivate), PCM, Cues, chapters. WebM readable |
| **MP4** | ✅ | ✅ | MJPEG (`mp4v`), `avc1`/`hvc1` passthrough. `moov` at end |
| **MOV** | ✅ | ✅ | MJPEG (`jpeg`), PCM_S16LE (`sowt`) |
| **WAV** | ✅ | ✅ | PCM U8/S16/S24/S32/F32, EXTENSIBLE, LIST/INFO |
| **RAW** | ✅ | ✅ | Headerless pixel dump |

**Codecs:** MJPEG encoder (baseline from scratch), MJPEG decoder (image_loader + automatic DHT injection), RAW ↔ RGB24 (BT.601/709, full/limited).

**Tools:** resize (nearest/bilinear/box), crop, rotate (90/180), flip, brightness/contrast, grey, Bayer demosaic, PCM conversion + resampler, SRT/VTT/ASS read+write, motion detector, timelapse, motion-triggered recorder with pre-roll, JSON export.

### ⚠️ Passthrough (you encode/decode)

H.264, H.265, VP8, VP9, AV1, MPEG-4, ProRes, DNxHD — carried as packets, not decoded. Give your encoder output via `write_encoded()`. Get raw packets via `read_frame()`. If you want, hook up your own decoder with `external_codec::register_video_decoder()`.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Zero-dependency** | No FFmpeg, just this header |
| **MJPEG encoder** | Baseline JPEG from scratch (quality 1-100) |
| **MJPEG decoder** | image_loader + automatic DHT injection for missing tables |
| **6 containers** | AVI/WAV/RAW/MKV/MP4/MOV (WebM readable) |
| **Passthrough** | H.264/H.265/VP8/VP9/AV1/ProRes embedded in containers |
| **PCM audio** | U8/S16/S24/S32/F32, resample + remix |
| **Image tools** | resize, crop, rotate, flip, brightness/contrast, Bayer demosaic |
| **Subtitles** | SRT/VTT/ASS read+write, timing shift |
| **Motion detector** | Block-based, timelapse gate, pre-roll recorder |
| **External codec** | Hook up your own H.264 decoder if you want |
| **Crash recovery** | If the AVI writer died, the file is still readable |

---

## 🚀 Quick Start

### Recording

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

### Reading

```cpp
knst_vid::video_reader rd;
if (rd.open(u"out/test.avi")) {
    auto f = rd.read_frame_rgb24(10);   // RGB24, width/height filled
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

## 📋 General API

| Category | Methods |
|---|---|
| **Video write** | `open`, `write_frame`, `write_frame_rgb24`, `write_encoded`, `close` |
| **Audio write** | `write_audio`, `write_audio_s16`, `write_audio_f32` |
| **Video read** | `open`, `read_frame`, `read_frame_rgb24`, `read_packet` |
| **Audio read** | `read_audio`, `read_audio_all` |
| **Status** | `frames_written`, `audio_frames_written`, `pts_next_us`, `bytes_written`, `last_error` |
| **Metadata** | `kind`, `info`, `meta`, `duration_us`, `stream_count`, `stream`, `video_stream`, `audio_stream` |
| **Recorder** | `start`, `feed_rgb24`, `stop`, `recording`, `clips_written` |
| **Image** | `img::resize_rgb24`, `crop_rgb24`, `rotate_90_rgb24`, `flip_h_rgb24`, `adjust_rgb24`, `to_grey_rgb24` |
| **Audio** | `audio::convert`, `remix`, `resample`, `peak` |
| **Subtitles** | `sub::srt/vtt/ass::read/write`, `sub::shift` |
| **Motion** | `motion_detector`, `timelapse` |
| **External codec** | `external_codec::register_video_decoder` etc. |

---

## 🔧 1) Config Macros

Define **before** including the header:

```cpp
#define KNST_VIDEO_MAX_DIMENSION       8192u    // default 16384
#define KNST_VIDEO_MAX_SAMPLE_BYTES    (128u * 1024u * 1024u)  // default 256 MiB
#define KNST_VIDEO_MAX_SAMPLES         (16u * 1024u * 1024u)   // default 64M
#define KNST_VIDEO_MAX_TRACKS          8u        // default 16
#define KNST_VIDEO_HAS_IMAGE_LOADER    1         // default 1
#define KNST_VIDEO_FILE_BUFFER         (512u * 1024u)  // default 256 KiB
```

`KNST_VIDEO_HAS_IMAGE_LOADER=0` → MJPEG decoder disabled. Encoder still works, `read_frame_rgb24()` returns `NotSupported` for MJPEG.

---

## ⚠️ 2) Error Handling

Via the `knst_vid::errc` enum:

| Error | Meaning |
|---|---|
| `Ok` | Success |
| `InvalidArgument` | Wrong parameter |
| `NotSupported` | Format/codec not supported |
| `NotFound` | Track missing / index out of range |
| `IoError` | File I/O error |
| `OutOfMemory` | Allocation failed |
| `BadState` | `open()` not called / file closed |
| `Corrupt` | Container/bitstream broken |
| `TooLarge` | `KNST_VIDEO_MAX_*` limit hit |
| `EndOfStream` | File ended |

```cpp
auto r = w.open(u"out/test.avi", cfg);
if (!r) std::cerr << w.last_error_string() << "\n";
```

`result<T>` and `status` come from the `knst_dev` namespace.

---

## 🎬 3) `video_writer` — High-Level Writer

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

### Support by container

| Container | vcodec | acodec | Notes |
|---|---|---|---|
| AVI | MJPEG/RAW/passthrough | PCM | Video required |
| MKV | MJPEG/passthrough | PCM/passthrough | WebM readable, not writable |
| MP4 | MJPEG/H.264/H.265 | none | `moov` at end |
| MOV | MJPEG/H.264/H.265 | PCM_S16LE | QuickTime |
| WAV | none (`has_video=false`) | PCM | Audio required |
| RAW | RAW (any pix) | none | Headerless |

### `write_frame()` — Any pixel format

```cpp
w.write_frame(rgb, w, h, pixel_format::RGB24);
```

- `pix` cannot be MJPEG/Unknown
- `w, h` different from config: `scale_to_fit=true` auto-resizes, `false` → `InvalidArgument`
- `pts_us=AUTO_PTS` → `frame_index / fps`
- Explicit pts: `w.write_frame(rgb, w, h, pix, 0, 1234567)`

### `write_encoded()` — Your own encoded packet

```cpp
w.write_encoded(nal_data, nal_size, is_key, pts_us);
```

- For MJPEG: JPEG bytes (**auto-injects DHT** if missing)
- For H.264: access unit
- `dts_us` only used in MP4/MOV (B-frame streams)

> ⚠️ Some cameras write JPEG **without DHT**. `write_encoded()` auto-fixes this — output plays in every player.

### `write_audio()` — Interleaved PCM

```cpp
w.write_audio(pcm_bytes, byte_count, pts_us);
w.write_audio_s16(int16_data, sample_count);
w.write_audio_f32(float_data, sample_count);
```

`bytes % audio_align_ != 0` → `InvalidArgument`.

### `AUTO_PTS`

`~0ull` — pts calculated automatically.

---

## 📖 4) `video_reader` — High-Level Reader

```cpp
knst_vid::video_reader rd;
if (!rd.open(u"out/test.avi")) {
    std::cerr << rd.last_error_string() << "\n";
    return;
}
```

Auto-detect: `RIFF+AVI `→AVI, `RIFF+WAVE`/`RF64`→WAV, `0x1A45DFA3`→MKV/WebM, `????ftyp`/`moov`/`mdat`→MP4/MOV, otherwise→`NotSupported`.

Headerless: `rd.open_raw(u"frames.raw", 640, 480, pixel_format::RGB24, 30, 1);`

### Info

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

### `read_frame_rgb24(idx)` — Decoded RGB

```cpp
auto f = rd.read_frame_rgb24(10);
// f.value.data → tightly packed RGB24
```

- MJPEG → image_loader
- RAW → convert_to_rgb24 + bottom-up flip
- Other codecs → **requires `external_codec::register_video_decoder`**

**Dropped frame:** `size=0` → returns the last real frame.

### `read_packet(track, idx, ...)` — Any track

```cpp
knst_byte_string b;
uint64_t pts; bool key;
if (rd.read_packet(track_index, 0, &pts, &key)) { /* b → raw packet */ }
```

### `read_audio(start, count)` — PCM range

```cpp
auto a = rd.read_audio(0, 48000);   // first 1 second @ 48 kHz
// a.value.data, a.value.frames, a.value.sample_rate, a.value.channels
```

> ⚠️ PCM only. For AAC/MP3/Opus use `read_packet()` + your own decoder.

### Seek

```cpp
uint64_t idx = rd.frame_at_time(2'500'000);
uint64_t kf  = rd.keyframe_at_or_before(idx);
```

---

## 🎥 5) `recorder` — Motion-Triggered Recording

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

### Usage

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
    if (r.value & knst_vid::recorder::EV_CLIP_STARTED) std::cout << "Started\n";
    if (r.value & knst_vid::recorder::EV_CLIP_FINISHED) std::cout << "Finished\n";
}
rec.stop();
```

**Timelapse mode:** `cfg.timelapse_mode = true; cfg.timelapse_interval_ms = 1000;` — ignore motion, just skip frames by interval.

---

## 🧰 6) Low-Level Tools

### 6.1) Color conversion (`color`)

```cpp
color::convert_to_rgb24(src, pixel_format::NV12, w, h, 0, dst);
color::convert_from_rgb24(rgb, w, h, 0, pixel_format::I420, dst);
color::bayer_to_rgb24(raw, w, h, stride, pixel_format::Bayer_RGGB, dst);

color::yuv_params yp;
yp.full_range = true;   // JPEG (false = video range)
yp.bt709 = true;        // HD (false = BT.601)
```

Supported: RGB24 ↔ BGR24, RGBA32, BGRA32, GREY, YUYV, UYVY, YVYU, NV12, NV21, I420, YV12, Bayer_BGGR/GBRG/GRBG/RGGB.

### 6.2) Image (`img`)

```cpp
auto small = img::resize_rgb24(rgb, w, h, 320, 240, img::resize_filter::bilinear);
auto crop = img::crop_rgb24(rgb, w, h, 100, 50, 200, 150);
auto rot = img::rotate_90_rgb24(rgb, w, h, true);
auto fh = img::flip_h_rgb24(rgb, w, h);
img::adjust_rgb24(rgb_ptr, w, h, 20.0f, 1.5f);   // brightness, contrast
auto g = img::to_grey_rgb24(rgb, w, h);
```

### 6.3) Audio (`audio`)

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

### 6.5) Subtitles (`sub`)

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

## 📀 7) Container Notes

- **AVI:** `TooLarge` above ~4 GB. Crash recovery included. `fill_gaps=true` adds dropped frames if PTS jumps.
- **MKV/WebM:** WebM is **not writable** (no VP8/VP9 encoding). Lacing not supported for read.
- **MP4/MOV:** `moov` at end. Fragmented MP4 (moof) **not supported**. Give `dts_us` for B-frames.
- **WAV:** 6+ channels → `WAVE_FORMAT_EXTENSIBLE`.
- **RAW:** Headerless, via `open_raw()`.

---

## ⚠️ 8) Common Gotchas

1. `write_frame()` **does not accept MJPEG pix** — use `write_encoded()`
2. `scale_to_fit=false` (default) → size mismatch gives `InvalidArgument`
3. `write_audio()` checks **`bytes % audio_align_`**
4. Passthrough codecs **require `vextradata`** (H.264 needs `avcC`)
5. B-frame streams **require `dts_us`** or ctts will be wrong
6. `read_frame_rgb24()` for passthrough **requires decoder registered**
7. **Call `close()` before writer goes out of scope** (destructor closes silently, no error check)
8. `read_audio_all()` can **blow up memory** on huge files
9. `#undef RAW`/`None`/`DELETE` — done automatically for some SDKs, don't do it manually
10. MJPEG encoder clamps DC `±2047` / AC `±1023` — no broken output, quality slightly down
11. `motion_detector::config::block` cannot be 0 (constructor sets it to 1)
12. `recorder` returns **`NotSupported`** for anything other than AVI/MKV/MP4/MOV

---

## 📊 9) Performance

| Operation | Complexity | Note |
|---|---|---|
| MJPEG encode | O(n) | FDCT + Huffman |
| MJPEG decode | O(n) | image_loader SIMD |
| RGB↔YUV | O(n) | Fixed-point, no float |
| resize (bilinear) | O(dw×dh) | 4 samples/pixel |
| resize (box) | O(src) | Single pass |
| motion_detector | O((w/block)×(h/block)) | 8x8 default |
| read_audio | O(frames) | Binary search + memcpy |
| resample | O(out_frames) | Linear interp |

**Tips:**
- **Reuse the encoder** (`configure()` once, `encode()` in loop)
- Only enable `scale_to_fit` if needed
- For MJPEG, **don't pick `s444`** — s420 gives smaller files

---

## 🌍 10) Platform

| Concern | Linux/macOS | Windows |
|---|---|---|
| File open | `fopen` + UTF-8 | `_wfopen` + UTF-16 |
| 64-bit seek | `fseeko`/`ftello` | `_fseeki64`/`_ftelli64` |

Header-only, just include.

---

## 🎯 11) When to Use?

### ✅ Use
- Screen/camera recording (MJPEG AVI/MKV/MP4)
- Simple video edit (crop, resize, rotate, flip)
- Timelapse / motion detection
- Subtitle conversion (SRT↔VTT↔ASS)
- Muxing your own H.264 stream
- Reading video metadata

### ❌ Don't use
- H.264 decoding (passthrough — separate decoder needed)
- Fragmented MP4 (not supported)
- AVI over 4 GB (use MKV/MP4)
- 4K real-time encoding (MJPEG is heavy)
- AAC/Opus decoding (separate decoder needed)

---

## ✅ 12) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Reuse the encoder
mjpeg::encoder enc;
enc.configure(opt);
for (auto& frame : frames) enc.encode(frame.data, w, h, 0, pixel_format::RGB24, jpg);

// 2. Use write_frame_rgb24() (when size matches config)
w.write_frame_rgb24(rgb.data());

// 3. Safely write raw JPEG from camera (DHT auto-injected)
w.write_encoded(jpeg, size, true, pts);

// 4. Seek via keyframe
uint64_t kf = rd.keyframe_at_or_before(idx);
for (uint64_t i = kf; i <= idx; ++i) rd.read_frame_rgb24(i);

// 5. Give dts_us for B-frame H.264
w.write_encoded(nal, sz, key, pts, dts);

// 6. Check last_error()
if (!w.close()) std::cerr << w.last_error_string() << "\n";
```

### ❌ Don't

```cpp
// 1. Pass MJPEG to write_frame()
w.write_frame(jpeg, w, h, pixel_format::MJPEG);   // ❌ use write_encoded()

// 2. Size mismatch with scale_to_fit=false
w.write_frame(rgb, 320, 240, pixel_format::RGB24);   // ❌ config is 640x480

// 3. Write WebM
cfg.cont = container::WebM;   // ❌ NotSupported

// 4. Skip vextradata for H.264 passthrough
cfg.vcodec = video_codec::H264;   // no vextradata → ❌ InvalidArgument

// 5. Skip dts for B-frames
w.write_encoded(nal, sz, key, pts);   // ❌ ctts wrong

// 6. Use read_frame_rgb24() without decoder for passthrough
auto f = rd.read_frame_rgb24(0);   // ❌ NotSupported for H.264

// 7. AVI over 4 GB
// ✅ Use MKV/MP4
```

---

## 🔥 13) Full Example

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // RECORD
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
    std::cout << "Written: " << w.frames_written() << " frames\n";

    // READ
    knst_vid::video_reader rd;
    if (!rd.open(u"out/test.mkv")) {
        std::cerr << "Read: " << rd.last_error_string() << "\n";
        return 1;
    }
    std::cout << rd.video_frames() << " frames, " << rd.duration_us() / 1000 << " ms\n";

    auto f = rd.read_frame_rgb24(10);
    if (f) std::cout << "Frame 10: " << f.value.width << "x" << f.value.height << "\n";

    auto a = rd.read_audio(0, cfg.sample_rate);
    if (a) std::cout << "Audio: " << a.value.frames << " frames\n";

    rd.close();
    return 0;
}
```

---

## 📌 14) API Contract Summary

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

// Sub-modules:
namespace color, img, audio, mjpeg, sub, external_codec { ... }
class motion_detector; class timelapse;

} // namespace knst_vid
```

---

## 🎯 What It Brings You

`knst_vid` gives you serious advantages in these situations:

- ✅ **Zero-dependency video recording** — no FFmpeg, single header
- ✅ **Screen/camera recording** — MJPEG AVI/MKV/MP4 ready
- ✅ **MJPEG encoder** — from scratch, quality 1-100, s444/s422/s420, restart markers
- ✅ **MJPEG decoder** — via image_loader + automatic DHT fix
- ✅ **6 containers** — AVI/WAV/RAW/MKV/MP4/MOV (+WebM readable)
- ✅ **Passthrough** — embed your H.264 stream in a container
- ✅ **PCM audio** — U8/S16/S24/S32/F32, resample + remix
- ✅ **Image tools** — resize, crop, rotate, flip, brightness/contrast
- ✅ **Bayer demosaic** — straight from camera sensor to RGB
- ✅ **Subtitles** — SRT/VTT/ASS read+write, timing shift
- ✅ **Motion detector** — pre-roll trigger, timelapse
- ✅ **Crash recovery** — AVI writer died? File still readable
- ✅ **External codec** — hook up your H.264 decoder with `register_video_decoder`
- ✅ **JSON export** — ready if `KNST_JSON_HPP` is included
- ✅ **KernelNucleusT compatibility** — fully integrated with `knst_c16string`, `knst_byte_string`, `knst_file`, `knst_image_loader`

---