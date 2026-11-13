# knst_converter — Media Converter, Extractor and Muxer

Hey! 👋 `knst_converter.hpp` is KernelNucleusT's **media converter / extractor / muxer** module. Built on top of `knst_audio.hpp` + `knst_video.hpp`. Audio conversion, video container conversion, audio extraction, audio muxing, frame extraction, remux and muxing — all in one header.

**In short:** **Synchronous, exception-free, no RTTI.** Every public call returns `result<T>`. On failure `converter::last_error()` / `last_message()` explain what and why. Progress + cancellation: `converter::set_progress()`, `converter::set_cancel()`. On failure, partial output files are **automatically removed**.

> ⚠️ **DEPENDENCIES:** Must be included in this order: `knst_devices.hpp` → `knst_file.hpp` → `knst_image_loader.hpp` → `knst_video.hpp` → `knst_audio.hpp`. `KernelNucleusT.hpp` handles this.

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

### Capability matrix (the library's real limits)

| Feature | Status | Note |
|---|---|---|
| **Audio conversion** | ✅ | WAV / AIFF / AU / RAW (u8..f64, A-law, mu-law) |
| **Audio processing** | ✅ | Sample rate, channel (5.1 downmix), gain, normalize, DC removal, fade, limiter, trim |
| **Audio extraction from AV containers** | ✅ | PCM → WAV/AIFF/AU/RAW |
| **MP3 track stream copy** | ✅ | Bit-exact |
| **AAC track → ADTS** | ✅ | ADTS framing from AudioSpecificConfig (AAC-LC/Main/SSR/LTP) |
| **Any track → MKA** | ✅ | Matroska audio, stream copy |
| **Video container conversion** | ✅ | AVI ↔ MKV ↔ MP4 ↔ MOV |
| **Stream copy (remux)** | ✅ | If the target container can hold the codec |
| **H.264 / H.265 framing** | ✅ | Length-prefixed (MP4/MKV) ↔ Annex-B (AVI) |
| **MJPEG / RAW transcode** | ✅ | Resize, crop, rotate, flip, brightness/contrast, grey, fps, quality |
| **Time range cut** | ✅ | Copy: previous key frame; transcode: frame accurate |
| **Audio mux** | ✅ | Add / replace / drop / PCM re-format / A/V offset |
| **Frame extraction** | ✅ | JPEG / PPM, single or sequence |
| **Probe** | ✅ | File info in one call |
| **H.264 / H.265 / VP9 / AV1 / AAC / Opus / MP3 / FLAC / Vorbis ENCODER** | ❌ | Not in the library |
| **H.264 / H.265 / VP9 / AV1 DECODER** | ❌ | Requires `vid::external_codec::register_video_decoder()` |
| **AAC / Opus / FLAC / Vorbis / MP3 DECODER** | ⚠️ | MP3 minimp3 (built-in), AAC FAAD2 (optional). Others via `aud::codec::registry::add()` or libsndfile |
| **"MP4 → MP3" conversion** | ⚠️ | Only if the MP4 already contains MP3 (stream copy) |
| **AAC → MP3** | ❌ | Needs a real encoder → `converter::register_audio_encoder()` |
| **Video re-encode to H.264** | ❌ | Transcode targets are only MJPEG and RAW (AVI) |
| **Non-MJPEG video decode** | ❌ | Resize/frame grab requires `vid::external_codec::register_video_decoder()` |
| **MP4 writer audio support** | ❌ | Video only |
| **MOV audio support** | ⚠️ | Only PCM_S16LE |
| **AVI audio support** | ⚠️ | Only PCM |
| **MKV audio support** | ✅ | Everything |
| **WebM writing** | ❌ | `video_writer` limitation → write `.mkv` (same streams) |
| **HE-AAC (SBR/PS) → ADTS** | ❌ | AAC-LC / Main / SSR / LTP only |
| **Full audio decode in RAM** | ⚠️ | 2^30 sample limit (`knst_audio`) |
| **Multiple raw blocks per ADTS frame** | ❌ | `NotSupported` |

**Critical note:** **There is NO H.264/H.265/VP9/AV1/AAC/Opus/MP3/FLAC/Vorbis encode/decode in this library.** Video and audio operations are:
- **Stream copy** (remux, no transcode) — when the codec already matches
- **MJPEG / RAW** transcoding (video)
- **PCM** transcoding (audio)
- Extendable via **external decoder/encoder** registration

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Audio conversion** | WAV / AIFF / AU / RAW, 8-bit..f64, A-law, mu-law |
| **Audio processing chain** | Gain, normalize, DC removal, fade, limiter, trim, resample, channel mix |
| **Stream copy** | Remux fast, no quality loss |
| **H.264/H.265 framing** | Length-prefixed ↔ Annex-B (between AVI/MP4/MKV) |
| **MJPEG transcode** | Resize, crop, rotate, flip, brightness, contrast, grey, fps, quality |
| **Frame-accurate time range** | Copy: previous key frame; transcode: frame accurate |
| **Audio mux** | Add / replace / drop / A/V offset |
| **Frame export** | JPEG / PPM (single frame or sequence) |
| **Probe** | All info in one call |
| **Progress + cancel** | `set_progress()`, `set_cancel()` |
| **Atomic error cleanup** | Partial output removed on failure |
| **Encoder/decoder registration** | `register_audio_encoder()`, `vid::external_codec::register_*` |
| **Bitstream helpers** | H.264/H.265 Annex-B ↔ length-prefixed, AAC ADTS, MP3 frame scanner |
| **JSON bridge** | `report_to_json()` (KNST_JSON_HPP) |

---

## 🚀 Quick Start

### Auto-mode (decides from file types)

```cpp
#include "KernelNucleusT.hpp"

knst_conv::converter cv;

// MP4 → MKV remux (stream copy, no re-encode)
auto r = cv.convert(u"clip.mp4", u"clip.mkv");
if (!r) {
    std::cerr << "Error: "
              << reinterpret_cast<const char*>(
                     knst_byte_string(cv.last_message()).data()) << "\n";
}
```

### Audio conversion (with processing)

```cpp
knst_conv::audio_options ao;
ao.sample_rate = 16000;    // 44.1 kHz → 16 kHz
ao.channels    = 1;        // stereo → mono
ao.normalize   = true;
ao.remove_dc   = true;

cv.convert_audio(u"voice.flac", u"voice_16k.wav", ao);
```

### Extract audio from AV container

```cpp
// Extract audio track from MKV to WAV, process it
knst_conv::audio_options ao;
ao.normalize = true;

cv.convert_audio(u"movie.mkv", u"speech.wav", ao);
```

### Video transcode (MJPEG AVI)

```cpp
knst_conv::video_options vo;
vo.filters.out_w    = 640;         // height by aspect
vo.jpeg_quality     = 80;
vo.start_s          = 10.0;
vo.end_s            = 25.0;

cv.convert_video(u"in.mkv", u"small.avi", vo);
```

### Add audio to video

```cpp
cv.add_audio(u"silent.mp4", u"music.wav", u"out.mov");
```

### Frame extraction

```cpp
// Frame at 5.0 seconds → JPEG
cv.extract_frame(u"in.mp4", u"thumb.jpg", 5.0);

// One frame per second → frames/000001.jpg...
cv.extract_frames(u"in.mkv", u"frames/", 1.0);
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Probe** | `probe(path)` |
| **Audio** | `convert_audio(in, out, opts)`, `extract_audio(in, out, opts)` |
| **Video** | `convert_video(in, out, opts)`, `strip_audio(in, out)` |
| **Mux** | `add_audio(video_in, audio_in, out, opts)` |
| **Frame** | `extract_frame(in, out, time_s, quality)`, `extract_frames(in, out_dir, interval_s, ...)` |
| **Auto** | `convert(in, out)` |
| **Control** | `set_progress()`, `set_cancel()`, `last_error()`, `last_message()`, `was_cancelled()` |
| **Registration** | `register_audio_encoder(format, fn)` |
| **Free functions** | `convert()`, `convert_audio()`, `convert_video()`, `extract_audio()`, `strip_audio()`, `add_audio()` |

---

## 🎯 1) Enums and Helpers

### 1.1) `format_t` — Supported formats

```cpp
enum class format_t : uint8_t {
    unknown,
    // AV containers:
    avi, mkv, webm, mp4, mov,
    // Audio files:
    wav, aiff, au, flac, ogg, mp3, aac,
    raw_pcm, mka,
    // Pictures:
    jpeg, ppm
};
```

**`format_name(f)`** → returns a human-readable string (`"mkv"`, `"wav"` etc.).

**`is_av_container(f)`** → true for `avi`, `mkv`, `webm`, `mp4`, `mov`.

**`to_vid_container(f)`** → converts to `vid::container` enum.

**`format_from_path(p)`** → extracts `format_t` from file extension. `.mp4` / `.m4v` / `.m4a` → `mp4`, `.qt` → `mov`, `.wave` / `.bwf` → `wav` etc.

### 1.2) `container_supports_video` / `container_supports_audio`

```cpp
bool container_supports_video(vid::container c, vid::video_codec v) noexcept;
bool container_supports_audio(vid::container c, vid::audio_codec a) noexcept;
```

**Video compatibility:**

| Container | Supported video codecs |
|---|---|
| **AVI** | MJPEG, RAW, H.264, H.265, VP8, VP9, AV1, MPEG4 |
| **MKV** / **WebM** | Everything **except** RAW, DNxHD, Unknown |
| **MP4** / **MOV** | MJPEG, H.264, H.265 |

**Audio compatibility:**

| Container | Supported audio codecs |
|---|---|
| **AVI** | PCM only |
| **MKV** / **WebM** | Everything |
| **MOV** | PCM_S16LE only |
| **MP4** | ❌ (writer is video only) |

### 1.3) `errc` — Error enum

Comes from `knst_video.hpp` (`vid::errc`). `cdetail::from_status()` converts `knst_dev::status` → `errc`.

### 1.4) `report` — Operation report

```cpp
struct report {
    bool     ok = false;
    bool     video_copied = false;      // Was video re-encoded?
    bool     audio_copied = false;      // Was audio re-encoded?
    uint64_t video_frames = 0;
    uint64_t audio_frames = 0;
    uint64_t bytes_out = 0;
    double   seconds = 0.0;
    format_t out_format = format_t::unknown;
    knst_vector<knst_c16string> notes;  // Decisions + warnings
};
```

**`notes`** — decisions made during the operation:
- `"audio dropped: the target container cannot store it (use .mkv or .mov to keep audio)"`
- `"stream copy cuts at the previous key frame (9.967 s instead of 10.000 s)"`
- `"H.264/H.265 rewritten from length-prefixed to Annex-B for AVI"`
- `"AAC stream copied (ADTS headers generated)"`
- `"external audio stream-copied"`

### 1.5) `input_info` — Probe result

```cpp
struct input_info {
    format_t format = format_t::unknown;
    bool     av_container = false;
    bool     has_video = false;
    bool     has_audio = false;
    double   duration_s = 0.0;
    vid::media_info av;                 // For AV containers
    aud::media_info au;                 // For audio-only files
};
```

### 1.6) `progress_fn`

```cpp
typedef bool (*progress_fn)(void* user, double fraction, const char* stage);
// Returns false → cancel
```

**`stage`** values: `"convert"`, `"audio"`, `"frames"`, `"copy"`, `"done"`.

---

## 🎵 2) `audio_options` — Audio Conversion Options

```cpp
struct audio_options {
    format_t format = format_t::unknown;              // unknown → from extension
    aud::sample_fmt sample = aud::sample_fmt::i16;    // output sample format
    uint32_t sample_rate = 0;                         // 0 = keep
    uint32_t channels = 0;                            // 0 = keep (e.g. 2 = 5.1 downmix)
    aud::dsp::resample_quality quality = high;
    double start_s = 0.0, end_s = 0.0;                // end 0 = until end
    float gain_db = 0.0f;
    bool  normalize = false;
    float normalize_peak_db = -1.0f;
    bool  remove_dc = false;
    float fade_in_ms = 0.0f, fade_out_ms = 0.0f;
    bool  limiter = false;
    float limiter_db = -1.0f;
    bool  dither = true;                              // TPDF, only for ≤16 bit
    bool  allow_copy = true;                          // stream copy (mp3→mp3 etc.)
    bool  raw_big_endian = false;                     // for RAW output
    uint32_t track = 0;                               // n-th audio track
    aud::decode_options decode;                       // RAW input parameters
};
```

**`needs_dsp()`** — does any processing need to be done? (sample_rate, channels, gain, normalize, remove_dc, fade, limiter)

### 2.1) Audio processing order

Inside `process_audio_()` the order is:

1. **Channel conversion** (5.1 → stereo/mono downmix)
2. **Resampling** (44.1 → 16 kHz etc.)
3. **DC removal**
4. **Gain** (`apply_gain_db`)
5. **Normalize** (peak)
6. **Fade in**
7. **Fade out**
8. **Limiter**

**Dither:** only applied for ≤16 bit targets and when bit-depth reduction is needed.

### 2.2) `sample_fmt` enum (from knst_audio)

- `u8`, `i16`, `i24`, `i32`, `f32`, `f64`, `alaw`, `ulaw`

**Note:** `dither` defaults to `true` but only applied when it makes sense (≤16 bit).

### 2.3) Encoding outputs

`encode_out_()` **tries native first**, then **sndfile fallback**, then **registered encoder**:

| Target | Native | Sndfile fallback | Registered encoder |
|---|---|---|---|
| **WAV** | ✅ | ✅ | — |
| **AIFF** | ✅ | ✅ | — |
| **AU** | ✅ | ✅ | — |
| **RAW** | ✅ | ✅ | — |
| **FLAC** | ✅ | ✅ | — |
| **OGG** | ⚠️ | ✅ (vorbis) | ✅ |
| **MP3** | ❌ | — | ✅ |
| **AAC** | ❌ | — | ✅ |

**WAV/AIFF/AU limit:** 4 GiB (RIFF limit).

**Note:** FLAC native writer supports PCM_16/24; f32/f64 requests are downgraded to PCM_16.

---

## 🎬 3) `video_options` — Video Conversion Options

```cpp
enum class video_mode : uint8_t { automatic, copy, transcode };
enum class audio_mode : uint8_t { automatic, copy, to_pcm, drop };

struct video_options {
    format_t   container = format_t::unknown;           // unknown → from extension
    video_mode vmode = video_mode::automatic;
    audio_mode amode = audio_mode::automatic;
    vid::video_codec vcodec = vid::video_codec::Unknown; // transcode: MJPEG (default) or RAW (AVI only)
    int  jpeg_quality = 0;                              // 0 = 85 (transcode), keep (MJPEG→MJPEG copy)
    vid::mjpeg::subsampling jpeg_sub = s420;
    video_filters filters;
    double fps = 0.0;                                   // 0 = keep (transcode only)
    double start_s = 0.0, end_s = 0.0;
    audio_options audio;                                // Audio decode/process options
    vid::audio_codec pcm_codec = vid::audio_codec::Unknown; // PCM type (Unknown = from source)
    knst_c16string external_audio;                      // External audio file (replace)
    double external_audio_offset_ms = 0.0;              // >0 delay, <0 advance
    uint32_t audio_track = 0;
    bool strict = false;                                // Fail instead of dropping audio
    knst_c16string title;                               // Title metadata override
};
```

### 3.1) `video_filters` — Video filters

```cpp
struct video_filters {
    uint32_t crop_x, crop_y, crop_w, crop_h;            // crop_w/h == 0 → no crop
    uint32_t rotate_deg;                                // 0/90/180/270 (clockwise)
    bool flip_h, flip_v;
    uint32_t out_w, out_h;                              // one set → keep aspect; both → exact
    double   scale;                                     // alternative to out_w/h
    img::resize_filter filter;
    bool     auto_filter = true;                        // box when shrinking, bilinear when enlarging
    float brightness, contrast;
    bool grey;
};
```

**Filter application order:**
1. **Crop** (first)
2. **Rotate**
3. **Flip H**
4. **Flip V**
5. **Resize**
6. **Brightness/Contrast**
7. **Grey**

**What does `auto_filter` do?**
- **Shrink** (`dw < w || dh < h`) → `box` (fast, antialiasing)
- **Enlarge** → `bilinear` (smooth)

---

## 🔀 4) `video_mode` / `audio_mode` — Decision Modes

### 4.1) `video_mode::automatic` (default)

The library decides:

```
copy = !needs_tc && copyable_codec && container_supports_video(oc, vsd.vcodec)

needs_tc = filt || fps > 0 || (jpeg_quality > 0 && src is MJPEG) || (vcodec != Unknown && vcodec != src)
copyable_codec = src != RAW && src != Unknown
```

**If `copy` is true** → stream copy (remux)
**If `copy` is false** → transcode (MJPEG or RAW)

### 4.2) `video_mode::copy` — mandatory stream copy

**If a filter / fps / quality change is set → `InvalidArgument`.**

**If the target container can't hold the source codec → `NotSupported`.**

### 4.3) `video_mode::transcode` — mandatory re-encode

**Can only target MJPEG or RAW.** Other codecs → `NotSupported`.

**If source codec is not MJPEG/RAW and no decoder is registered** → `NotSupported`.

### 4.4) `audio_mode` semantics

| Mode | Behavior |
|---|---|
| **`automatic`** | Decides: copy → to_pcm → drop |
| **`copy`** | Stream copy only; otherwise `NotSupported` |
| **`to_pcm`** | Decode + re-encode as PCM |
| **`drop`** | Audio dropped entirely |

### 4.5) Audio strategy (automatic)

```
1. No external audio and amode=copy → try stream copy
2. Codec + container compatible and no DSP → stream copy
3. Otherwise:
   a. Container can hold PCM_S16LE → decode + PCM encode
   b. Can't hold → audio drop + note
   c. In strict mode → NotSupported
```

---

## 🎼 5) Audio Feeds

The library uses 3 **`audio_feed`** classes to provide packets to the audio writer.

### 5.1) `packet_feed` — Packets from AV container

```cpp
class packet_feed : public audio_feed {
public:
    errc init(vid::video_reader* rd, uint32_t track,
              vid::audio_codec pcm_target,
              uint64_t start_us, uint64_t end_us) noexcept;
};
```

**What it does?**
- Reads audio packets from AV container
- PCM → bit-exact copy
- PCM with different target → `convert_pcm`
- Codecs: MP3, AAC, FLAC, Opus, Vorbis → passthrough
- For AAC → **AudioSpecificConfig** (extracted from esds or directly)

### 5.2) `buffer_feed` — Decoded float buffer

```cpp
class buffer_feed : public audio_feed {
public:
    errc init(aud::pcm_buffer&& b, vid::audio_codec target) noexcept;
};
```

**What it does?**
- Converts `pcm_buffer` (float samples) → target PCM codec
- Delivers 20 ms chunks
- Not zero-copy — per-sample conversion

### 5.3) `frame_feed` — MP3/AAC frames

```cpp
class frame_feed : public audio_feed {
public:
    errc init(knst_byte_string&& file, bs::elementary&& el,
              uint64_t start_us, uint64_t end_us,
              int64_t shift_us) noexcept;
};
```

**What it does?**
- Delivers frames from `.mp3` or `.aac` files in order
- Bit-exact passthrough
- Applies time range + shift

---

## 🎥 6) Video Pumps

The library uses 2 **`video_pump`** classes to provide frames to the writer.

### 6.1) `copy_pump` — Packet copy

```cpp
class copy_pump : public video_pump {
public:
    vid::video_reader* rd;
    uint32_t track;
    uint64_t idx, base_us, end_us;
    bool allow_empty, h265;
    int conv;                    // 0 none, 1 lp→annexb, 2 annexb→lp
    bs::param_sets ps;           // avcC/hvcC parameter sets
};
```

**What it does?**
- Reads packets **without re-encoding**
- Converts **framing** if needed:
  - `conv=1`: Length-prefixed → Annex-B (MP4/MKV → AVI)
  - `conv=2`: Annex-B → Length-prefixed (AVI → MP4/MKV)
- Prepends parameter sets (SPS/PPS/VPS) on key frames

**`allow_empty`** — AVI writer accepts empty packets (VP9/AV1).

### 6.2) `transcode_pump` — Decode + filter

```cpp
class transcode_pump : public video_pump {
public:
    vid::video_reader* rd;
    const video_filters* flt;
    double fps;
    uint64_t start_us, end_us, dur_us, idx;
};
```

**What it does?**
- Decodes each frame via `read_frame_rgb24`
- Applies filters
- Writer re-encodes via `write_frame` (MJPEG/RAW)

**`fps > 0`** → frame-at-time conversion:
- `start_us + (k / fps)` → `rd->frame_at_time(t)`
- Frame-accurate time range

**`fps == 0`** → source fps preserved, frames decoded sequentially.

---

## 🔧 7) Bitstream Helpers (`bs::`)

Helpers for H.264/H.265 framing and AAC/MP3 stream manipulation.

### 7.1) NAL unit split/parse

```cpp
struct nal_range { uint32_t off; uint32_t len; };
struct param_sets {
    knst_vector<knst_byte_string> sets;
    uint32_t len_size = 4;
};

bool is_annexb(const uint8_t* d, uint32_t n) noexcept;
void split_annexb(const uint8_t* d, uint32_t n, knst_vector<nal_range>& out) noexcept;
bool parse_avcc(const knst_byte_string& x, param_sets& ps) noexcept;
bool parse_hvcc(const knst_byte_string& x, param_sets& ps) noexcept;
```

**`is_annexb`** — Does it start with `00 00 01` or `00 00 00 01`?

**`split_annexb`** — Finds NAL units in Annex-B stream.

**`parse_avcc`** — Extracts SPS/PPS from `avcC` box.

**`parse_hvcc`** — Extracts VPS/SPS/PPS from `hvcC` box.

### 7.2) Framing conversion

```cpp
errc lp_to_annexb(const uint8_t* d, uint32_t n, uint32_t ls,
                  const param_sets* pre, knst_byte_string& out) noexcept;
errc annexb_to_lp(const uint8_t* d, uint32_t n, bool h265,
                  knst_byte_string& out) noexcept;
```

**`lp_to_annexb`** — Length-prefixed (MP4/MKV) → Annex-B (AVI):
- If `pre` is given, parameter sets are **prepended** (for key frames)
- `ls` = length size (usually 4)

**`annexb_to_lp`** — Annex-B (AVI) → 4-byte length-prefixed (MP4/MKV):
- SPS/PPS/VPS **skipped** (stored in avcC/hvcC)

### 7.3) SPS/PPS helpers

```cpp
bool is_param_nal(const uint8_t* nal, bool h265) noexcept;
bool collect_sps_pps(const uint8_t* d, uint32_t n,
                     knst_byte_string& sps, knst_byte_string& pps) noexcept;
knst_byte_string make_avcc(const knst_byte_string& sps,
                           const knst_byte_string& pps) noexcept;
```

**`is_param_nal`** — H.264: SPS (7) / PPS (8); H.265: VPS (32) / SPS (33) / PPS (34).

**`collect_sps_pps`** — Collects SPS/PPS from Annex-B stream (from initial frame).

**`make_avcc`** — Builds avcC box from SPS/PPS.

### 7.4) AAC ADTS

```cpp
struct asc_info { uint32_t aot, sfi, ch, rate; bool ok; };

asc_info parse_asc(const uint8_t* p, size_t n) noexcept;
void adts_header(uint8_t* h, const asc_info& a, uint32_t payload) noexcept;
```

**`parse_asc`** — AudioSpecificConfig parse:
- `aot` = Audio Object Type (1=Main, 2=LC, 3=SSR, 4=LTP)
- `sfi` = Sample rate index (0..12)
- `ch` = Channel config
- `rate` = Hz

**`adts_header`** — Generates ADTS header (7 bytes).

### 7.5) Elementary stream scanner

```cpp
struct elem_frame { uint32_t off; uint32_t size; uint32_t samples; };
struct elementary {
    vid::audio_codec codec;
    uint32_t rate, channels;
    knst_byte_string asc;
    knst_vector<elem_frame> frames;
    uint64_t total_samples;
};

errc scan_mp3(const knst_byte_string& f, elementary& e) noexcept;
errc scan_adts(const knst_byte_string& f, elementary& e) noexcept;
```

**`scan_mp3`** — Scans MP3 Layer III frames:
- Skips ID3v2
- Skips Xing/Info header frame
- Confirms next header for frame sync
- Layer III only

**`scan_adts`** — Scans ADTS frames:
- Skips ID3
- Rejects multiple raw blocks per frame
- Builds ASC from first frame

### 7.6) MP3 header parse

```cpp
bool mp3_header(const uint8_t* h, uint32_t& flen, uint32_t& rate,
                uint32_t& ch, uint32_t& spf, int& ver) noexcept;
```

**Returns:**
- `flen` = frame length
- `rate` = sample rate
- `ch` = channels
- `spf` = samples per frame (1152 / 576)
- `ver` = MPEG version (0=2.5, 2=2, 3=1)

### 7.7) ID3 skip

```cpp
uint64_t skip_id3(const uint8_t* d, uint64_t n) noexcept;
```

If ID3v2 header present, returns total size; otherwise 0.

---

## 🎯 8) `converter` — Main Class

```cpp
class converter {
public:
    converter() noexcept = default;

    // Control
    void set_progress(progress_fn f, void* user = nullptr) noexcept;
    void set_cancel(const std::atomic<bool>* flag) noexcept;
    errc last_error() const noexcept;
    const knst_c16string& last_message() const noexcept;
    bool was_cancelled() const noexcept;
    static bool register_audio_encoder(format_t f, audio_encoder_fn fn, void* user = nullptr) noexcept;

    // Probe
    result<input_info> probe(const knst_c16string& path) noexcept;

    // Audio
    result<report> convert_audio(const knst_c16string& in, const knst_c16string& out,
                                 const audio_options& o = {}) noexcept;
    result<report> extract_audio(...) noexcept;  // alias

    // Video
    result<report> convert_video(const knst_c16string& in, const knst_c16string& out,
                                 const video_options& o = {}) noexcept;
    result<report> strip_audio(const knst_c16string& in, const knst_c16string& out) noexcept;
    result<report> add_audio(const knst_c16string& video_in, const knst_c16string& audio_in,
                             const knst_c16string& out, video_options o = {}) noexcept;

    // Frame
    result<report> extract_frame(const knst_c16string& in, const knst_c16string& out,
                                 double time_s = 0.0, int quality = 90,
                                 const video_filters* flt = nullptr) noexcept;
    result<report> extract_frames(const knst_c16string& in, const knst_c16string& out_dir,
                                  double interval_s = 1.0, uint32_t max_frames = 0,
                                  int quality = 90, const video_filters* flt = nullptr,
                                  bool ppm = false) noexcept;

    // Auto
    result<report> convert(const knst_c16string& in, const knst_c16string& out) noexcept;
};
```

### 8.1) `probe` — File info

```cpp
auto r = cv.probe(u"video.mkv");
if (r) {
    std::cout << "Format: " << knst_conv::format_name(r.value.format) << "\n";
    std::cout << "Duration: " << r.value.duration_s << " s\n";
    std::cout << "Video: " << (r.value.has_video ? "yes" : "no") << "\n";
    std::cout << "Audio: " << (r.value.has_audio ? "yes" : "no") << "\n";

    if (r.value.av_container) {
        std::cout << "  Streams: " << r.value.av.streams.size() << "\n";
    }
}
```

**What it does?**
1. Format guess from header bytes (magic)
2. If AV container → uses `vid::video_reader` to gather stream info
3. If audio-only → calls `aud::probe()`
4. For MP3/AAC → `bs::scan_mp3` / `bs::scan_adts`

### 8.2) `convert_audio` — Audio conversion / extraction

```cpp
cv.convert_audio(in, out, audio_options);
```

**Decision tree:**

```
1. Is input an AV container?
   YES → audio_from_container_()
   NO  → audio_from_file_()

audio_from_container_():
  1. Find audio track (o.track = n-th)
  2. allow_copy && !DSP:
     - out=mp3 + src=mp3 → write_elementary_(adts=false)
     - out=aac + src=aac → write_elementary_(adts=true)
  3. out=mka → audio_feed (copy or buffer) → write_audio_only_
  4. Otherwise: load_track_pcm_ → process_audio_ → encode_out_

audio_from_file_():
  1. elem_in = mp3/aac
  2. !DSP && s_us==0 && e_us==0 && fin==fout → file copy
  3. out=mka && !DSP → frame_feed → write_audio_only_
  4. Otherwise: aud::decode_file → trim → process → encode_out_
```

### 8.3) `convert_video` — Video conversion

```cpp
cv.convert_video(in, out, video_options);
```

**Decision tree (summary):**

```
1. Input must be AV container
2. Output: avi/mkv/mp4/mov (webm rejected)
3. Audio + video track planning
4. copy / transcode decision
5. Plan audio feed (plan_audio_)
6. Set up video pump (copy_pump or transcode_pump)
7. Open writer
8. Interleaved mux loop:
   - Compare video and audio PTS
   - Write the smaller one
9. Close
```

**Interleaved mux loop:** Writes video and audio alternating by PTS. If video `pump->pts_us()` ≤ audio `feed->pts_us()` → write video; otherwise write audio.

### 8.4) `strip_audio`

```cpp
cv.strip_audio(u"in.mkv", u"mute.mkv");
```

**What it does?** `convert_video` with `video_options o; o.amode = audio_mode::drop;`

### 8.5) `add_audio`

```cpp
cv.add_audio(u"silent.mp4", u"music.wav", u"out.mov");
```

**What it does?** `convert_video` with `video_options o; o.external_audio = audio_in;`

**Note:** External audio **starts at its own t=0** (not the video's `start_us`), bounded by the trimmed video duration.

### 8.6) `extract_frame` / `extract_frames`

```cpp
// Single frame
cv.extract_frame(u"in.mp4", u"thumb.jpg", 5.0, 90);

// Frame sequence
cv.extract_frames(u"in.mkv", u"frames/", 1.0, 0, 90, nullptr, false);
```

**Target format:** `.jpg` or `.ppm`.

**If source is MJPEG:** Bit-exact JPEG passthrough (quality ignored). If DHT (Huffman table) is missing, **it's injected**.

**Non-MJPEG source:** Decode → filter → JPEG encode (or PPM write).

**`extract_frames`** — Names files as `frame_000001.jpg` etc.

### 8.7) `convert` — Auto dispatch

```cpp
cv.convert(in, out);
```

**Decision tree:**

```
1. Is output an AV container?
   YES → convert_video
   NO  → 2
2. Is output jpeg/ppm?
   YES → extract_frame
   NO  → 3
3. Output extension unknown?
   YES → InvalidArgument
   NO  → 4
4. → convert_audio
```

**Audio-only file → AV container:** `NotSupported` (meaningless; use `.mka` or `add_audio()`).

---

## ⚠️ 9) Common Gotchas

### 1) No H.264/H.265 ENCODER

The library has **no video encoder**. Transcode can only target **MJPEG** or **RAW**.

```cpp
video_options vo;
vo.vcodec = vid::video_codec::H264;   // ❌ NotSupported
// ✅ vo.vcodec = vid::video_codec::MJPEG;
```

### 2) No H.264/H.265 DECODER

Non-MJPEG video decode **requires** `vid::external_codec::register_video_decoder()`.

```cpp
// ✅ User registration:
vid::external_codec::register_video_decoder(
    vid::video_codec::H264, my_decoder_fn, user);
```

If not registered: filters can't apply, frame extraction can't happen → `NotSupported`.

### 3) MP4 writer doesn't support audio

For MP4 target, **audio is automatically dropped**. Notification in `report.notes`.

**To combine with audio:**
- Use **MKV** (everything)
- Use **MOV** (PCM_S16LE only)
- Set **strict = true** to fail instead

### 4) MOV only supports PCM_S16LE

MOV target with other PCM codecs (PCM_F32LE, PCM_S24LE etc.) → `NotSupported`.

Set `pcm_codec = vid::audio_codec::PCM_S16LE` or leave as `Unknown` (auto).

### 5) AVI only supports PCM

AVI target with non-PCM audio → `NotSupported`.

### 6) WebM can't be written

```cpp
video_options vo;
vo.container = format_t::webm;   // ❌ NotSupported
// ✅ vo.container = format_t::mkv;   (same streams, same quality)
```

### 7) HE-AAC → ADTS doesn't work

AAC stream with HE-AAC (SBR/PS) → ADTS `.aac` file can't be produced. Only AAC-LC/Main/SSR/LTP.

```cpp
// HE-AAC source:
cv.convert_audio(u"he-aac-in.mp4", u"out.aac");   // ❌ NotSupported
// ✅ Solution: extract as WAV or MKA
```

### 8) `AAC → MP3` needs a real encoder

```cpp
cv.convert_audio(u"in.aac", u"out.mp3");   // ❌ NotSupported (no MP3 encoder)
// ✅ Registration required:
knst_conv::converter::register_audio_encoder(
    knst_conv::format_t::mp3, my_mp3_encoder, user);
```

### 9) MP4 → MP3 only works if MP4 contains MP3

```cpp
// If MP4 has H.264 + AAC:
cv.convert_audio(u"clip.mp4", u"clip.mp3");   // ❌ NotSupported (AAC→MP3)
// ✅ cv.convert_audio(u"clip.mp4", u"clip.mka");   (AAC stream copy)

// If MP4 has H.264 + MP3:
cv.convert_audio(u"clip.mp4", u"clip.mp3");   // ✅ stream copy
```

### 10) Stream copy cuts at key frame

In `video_mode::copy`, `start_s` **rounds down to the previous key frame**.

```cpp
vo.vmode = video_mode::copy;
vo.start_s = 10.0;
// `report.notes`:
// "stream copy cuts at the previous key frame (9.967 s instead of 10.000 s)"
```

**For frame-accurate cut:** use `video_mode::transcode`.

### 11) MJPEG → MJPEG copy ignores `jpeg_quality`

```cpp
vo.vmode = video_mode::copy;   // MJPEG → MJPEG
vo.jpeg_quality = 90;          // ❌ ignored (bit-exact copy)
```

### 12) B-frames can break AVI

The AVI writer doesn't support **B-frame reordering**.

```cpp
// H.264 with B-frames copied to AVI:
// `report.notes`:
// "warning: AVI cannot represent B-frame reordering; streams with B-frames may play wrongly"
```

### 13) `audio_options::dither` logic

`dither = true` is the default, but **only** applied when:
- Target bit depth ≤ 16
- `b.source_bits == 0` (unknown) or `> target_bits`

Otherwise skipped.

### 14) `audio_options::normalize_peak_db = -1.0f`

`-1.0f` default = full-scale peak. `-3.0f` = 3 dB headroom.

**`from_db(-1.0)` → ~0.89 linear.**

### 15) `limiter_db = -1.0f` default

Default -1 dBFS limit. More aggressive: `-0.1f`, softer: `-3.0f`.

### 16) `audio_options::channels = 2` = 5.1 downmix

If source is 5.1 → stereo downmix. If source is stereo → unchanged.

### 17) `audio_options::sample_rate = 0` = from source

Set explicitly to change. `16000`, `48000` etc.

### 18) `external_audio_offset_ms` semantics

```cpp
vo.external_audio_offset_ms = 500.0;   // 500 ms delay (silence at start)
vo.external_audio_offset_ms = -500.0;  // 500 ms advance (cut from start)
```

**Positive** → adds leading silence (audio delayed)
**Negative** → cuts first N ms (audio advanced)

### 19) `external_audio` is affected by time range

**Critical:** External audio **is bounded by the video's trimmed duration**, not `start_us`.

```cpp
vo.start_s = 10.0;
vo.end_s = 30.0;
vo.external_audio = u"music.wav";

// Video: [10s, 30s] = 20 s
// Music: [0s, 20s] — 10s of music from the start is not skipped
```

### 20) `audio_track` = n-th audio track

```cpp
vo.audio_track = 1;   // use second audio track
```

If not found → `NotFound`.

### 21) `strict = true` fails instead of dropping audio

```cpp
vo.strict = true;
// If audio drop is needed → NotSupported
// Otherwise: audio drop + note
```

### 22) `pcm_codec = Unknown` auto

Best codec allowed by the container is chosen:
1. Is target container compatible?
2. Can source PCM type be kept?
3. Otherwise → PCM_S16LE

### 23) Explicit `pcm_codec` may fail

```cpp
vo.pcm_codec = vid::audio_codec::PCM_F32LE;
vo.container = format_t::mov;   // MOV only S16LE
// → NotSupported
```

### 24) `cdetail::write_bytes` — File write

Internal helper. `ensure_parent_dir` + `file_handle::open` + `write` + `close`. Returns `IoError` on failure.

### 25) `cdetail::file_size_of` — File size

Internal. Opens via `file_handle` and returns size.

### 26) Time range (audio) frame accuracy

`audio_from_container_`:
- **Stream copy** (`write_elementary_`): packet-based; may round to keyframe
- **Decode path**: sample-accurate trim (`dsp::trim`)

### 27) Time range (video transcode)

`transcode_pump`:
- **fps > 0**: `start_us + (k / fps)` → `frame_at_time(t)` → frame accurate
- **fps == 0**: source frame iterate + `pump->has()` check

### 28) `fps` conversion drift

`fps = 25.0` writes → 25 FPS output. If source is 29.97 → frame conversion + drop/duplicate.

Small fps deltas will inevitably drop/duplicate frames.

### 29) `extract_frames` output names

`frame_000001.jpg`, `frame_000002.jpg`, ... (6 digits, zero-padded).

**For PPM:** `frame_000001.ppm`.

### 30) `extract_frame` MJPEG passthrough

MJPEG source + `flt == nullptr` → bit-exact JPEG passthrough. `quality` ignored.

**If MJPEG lacks DHT (Huffman table)** standard table is injected.

### 31) `MJPEG inject_std_dht`

Some MJPEG encoders omit the DHT segment (for optimization). In that case:

```cpp
vid::mjpeg::inject_std_dht(data, len, patched);
```

Standard DHT table is added.

### 32) `container_supports_video(MKV, VC::RAW)` → false

MKV doesn't accept RAW video. Use AVI for RAW.

### 33) `container_supports_video(AVI, VC::AV1)` → true but warning

AV1 in AVI is "non-standard" — some players won't open it.

`report.notes` warns:
```
"warning: this codec in AVI is non-standard; many players will not open it"
```

### 34) `container_supports_video(AVI, VC::H265)` → true but B-frame issue

H.265 B-frames are common. AVI doesn't support B-frames. `report.notes` warns.

### 35) Transcode target codec required

If you use `video_mode::transcode`:
- `vcodec = VC::MJPEG` or
- `vcodec = VC::RAW` (AVI only)

`VC::Unknown` → default MJPEG.

### 36) `automatic` mode copy vs transcode

`copy` isn't always possible:
- `vsd.vcodec == RAW` or `Unknown` → copy NO
- `!container_supports_video(oc, vsd.vcodec)` → copy NO
- `filters.active()` → transcode
- `fps > 0` → transcode
- `jpeg_quality > 0 && src MJPEG` → transcode (quality change)
- `vcodec != Unknown && vcodec != src` → transcode

### 37) `bitstream` MP3 stateful decode

MP3 **shares state between frames** (bit reservoir). Packet-by-packet decode **is wrong**. The library feeds the **whole MP3 stream** to minimp3 in sequence.

RAM limit: 500 KB buffer + 1 frame (instead of the previous 2 GB approach).

### 38) AAC FAAD2 optional

`KNST_NETWORK_HAVE_FAAD2` must be defined. Otherwise AAC decode is **not supported** (only ADTS stream copy).

### 39) `MINIMP3_MAX_SAMPLES_PER_FRAME` dependency

MP3 decode uses `minimp3` (ships in knst_audio).

### 40) Audio RAM limit

`aud::pcm_buffer` is **fully decoded** (floats). 2^30 sample limit (~2 GB float).

**For long audio files:** use `knst_aud::stream` (streaming).

### 41) Read `report.notes`

`report.notes` contains **decisions** and **warnings**. Even when `ok=true`, notes are important.

```cpp
if (r.value.ok) {
    for (const auto& note : r.value.notes) {
        std::cout << "Note: " << note << "\n";
    }
}
```

### 42) `probe` — check `av.valid`

`input_info.av` is only valid when `av_container == true`.

### 43) `probe` — check `au.valid`

`input_info.au` is only valid when `av_container == false` and the input is an audio file.

### 44) On `error`, output is deleted

`convert_audio` / `convert_video` failure → `knst_file::remove_file(out)`. No partial output.

### 45) `knst_file::copy_file` fast path

`fin == fout && mp3/aac && no_dsp && no_trim` → **file copy** (no decode).

`report.notes`: `"same format, file copied"`.

### 46) `audio_from_container_` MKA output

`out=mka` → stream copy (packet_feed) or PCM re-encode (buffer_feed).

**Note:** packet_feed only copies from PCM/MP3/AAC/FLAC/Opus/Vorbis sources from MKV/WebM.

### 47) `plan_audio_` — external audio is an AV container

```cpp
vo.external_audio = u"movie.mkv";   // another video!
```

Video reader opens it, grabs the `audio_track` (n-th) audio track.

### 48) `cross-container` AAC copy

AAC in MP4 → MKA write:
- `sd.extradata` contains AudioSpecificConfig inside esds
- `bs::get_asc()` parses esds
- `packet_feed` stream copies

**Correct.**

### 49) AAC esds parse edge cases

`find_asc_in_esds` does recursive descent:
- Tag 0x03 (ES_Descriptor) → parse flags
- Tag 0x04 (DecoderConfigDescriptor) → skip 13 bytes
- Tag 0x05 (DecoderSpecificInfo) → ASC

Some older MP4s have **nested** esds.

### 50) `converter::register_audio_encoder` is static

```cpp
knst_conv::converter::register_audio_encoder(format_t::mp3, my_fn);
// or
knst_conv::register_audio_encoder(format_t::mp3, my_fn);
```

**Global registry** — process-wide. Protected with a `mutex`.

---

## 📊 10) Performance

| Operation | Cost | Note |
|---|---|---|
| **probe** | 10-100 ms | Container parse |
| **Audio conversion (copy)** | O(1) | Fast file copy |
| **Audio decode** | ~50-200× realtime | float decode |
| **Audio processing** | ~20-50× realtime | DSP |
| **Audio encode** | ~50-100× realtime | PCM encode |
| **MP3 stream copy** | ~100× realtime | Elementary copy |
| **AAC stream copy** | ~100× realtime | ADTS generate |
| **MKA write** | ~50× realtime | Matroska |
| **Video remux** | ~50-200× realtime | Packet copy |
| **Video MJPEG transcode** | ~1-5× realtime | Decode + re-encode |
| **Frame extract** | ~1 frame/sec | Per frame |
| **Frame sequence** | depends on interval | |

**Typical latencies:**
- **MP4 → MKV remux (5 min video):** 2-5 seconds
- **FLAC → WAV (3 min audio):** 1-2 seconds
- **5.1 downmix + 16 kHz resample (3 min):** 2-4 seconds
- **MJPEG AVI transcode (1 min video, 640×480):** 10-30 seconds
- **Frame extract (1 frame):** 100-500 ms

**Optimizations:**
- **Use stream copy** (`video_mode::copy`, `audio_mode::copy`) — no re-encode
- **Filter application** — bypass if `filters.active() == false`
- **Long files** — use streaming (`knst_aud::stream`)

---

## ✅ 11) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Use auto mode (short code)
auto r = cv.convert(u"in.mp4", u"out.mkv");

// 2. For remux use video_mode::copy
knst_conv::video_options vo;
vo.vmode = knst_conv::video_mode::copy;
cv.convert_video(u"in.mkv", u"out.mp4", vo);   // ✅ stream copy

// 3. Use .mkv or .mov to keep audio
vo.container = knst_conv::format_t::mkv;
cv.convert_video(u"in.mkv", u"out.mkv", vo);

// 4. Read the notes
if (r) {
    for (const auto& n : r.value.notes) std::cout << n << "\n";
}

// 5. Progress callback
cv.set_progress([](void* user, double frac, const char* stage) -> bool {
    std::cout << stage << ": " << (frac * 100) << "%\n";
    return true;   // false → cancel
}, nullptr);

// 6. Cancel support
std::atomic<bool> cancel_flag{false};
cv.set_cancel(&cancel_flag);

// 7. Use a registered encoder
knst_conv::converter::register_audio_encoder(
    knst_conv::format_t::mp3, my_mp3_encoder);

// 8. MJPEG quality
vo.jpeg_quality = 85;
vo.jpeg_sub = vid::mjpeg::subsampling::s420;

// 9. Frame-accurate cut via transcode
vo.vmode = knst_conv::video_mode::transcode;
vo.start_s = 10.0;
vo.end_s = 30.0;
```

### ❌ Don't

```cpp
// 1. Expect an H.264/H.265 encoder
vo.vcodec = vid::video_codec::H264;   // ❌ NotSupported

// 2. Expect a non-MJPEG decoder
cv.extract_frame(u"h264.mkv", u"thumb.jpg", 0);   // ❌ NotSupported

// 3. Expect audio in MP4
vo.container = knst_conv::format_t::mp4;
// ❌ Audio dropped (writer is video only)

// 4. Write WebM
vo.container = knst_conv::format_t::webm;   // ❌ NotSupported
// ✅ vo.container = knst_conv::format_t::mkv;

// 5. HE-AAC → ADTS
cv.convert_audio(u"he-aac.mp4", u"out.aac");   // ❌ NotSupported

// 6. AAC → MP3 without registration
cv.convert_audio(u"aac.aac", u"out.mp3");   // ❌ NotSupported

// 7. MP3 decoder without registration (actually minimp3 is built-in ✅)

// 8. Copy mode + filter
vo.vmode = knst_conv::video_mode::copy;
vo.filters.out_w = 640;   // ❌ InvalidArgument

// 9. Copy mode + fps
vo.vmode = knst_conv::video_mode::copy;
vo.fps = 25.0;   // ❌ InvalidArgument

// 10. Copy mode + quality change
vo.vmode = knst_conv::video_mode::copy;
vo.jpeg_quality = 90;   // ❌ ignored on MJPEG→MJPEG copy
```

---

## 🔥 12) Real-World Examples

### Example 1: Fast remux (no re-encode)

```cpp
knst_conv::converter cv;

knst_conv::video_options vo;
vo.vmode = knst_conv::video_mode::copy;
vo.amode = knst_conv::audio_mode::copy;

auto r = cv.convert_video(u"in.mp4", u"out.mkv", vo);
if (r) {
    std::cout << "Remux OK: "
              << (r.value.video_copied ? "video copied, " : "")
              << (r.value.audio_copied ? "audio copied" : "")
              << "\n";
}
```

### Example 2: Speech extraction (5.1 → mono 16 kHz)

```cpp
knst_conv::audio_options ao;
ao.sample_rate = 16000;
ao.channels    = 1;
ao.normalize   = true;
ao.remove_dc   = true;
ao.gain_db     = 3.0f;
ao.fade_in_ms  = 100.0f;
ao.fade_out_ms = 200.0f;

auto r = cv.convert_audio(u"movie.mkv", u"speech.wav", ao);
```

### Example 3: MJPEG AVI transcode (resize)

```cpp
knst_conv::video_options vo;
vo.vmode = knst_conv::video_mode::transcode;
vo.filters.out_w = 640;
vo.filters.filter = knst_vid::img::resize_filter::bilinear;
vo.jpeg_quality = 80;
vo.fps = 25.0;

auto r = cv.convert_video(u"in.mkv", u"small.avi", vo);
```

### Example 4: Add music to video

```cpp
knst_conv::video_options vo;
vo.external_audio = u"music.flac";
vo.external_audio_offset_ms = 500.0;   // 500 ms delay
vo.container = knst_conv::format_t::mov;

auto r = cv.add_audio(u"silent.mp4", u"music.flac", u"out.mov", vo);
```

### Example 5: Crop + rotate

```cpp
knst_conv::video_options vo;
vo.vmode = knst_conv::video_mode::transcode;
vo.filters.crop_x = 100;
vo.filters.crop_y = 50;
vo.filters.crop_w = 800;
vo.filters.crop_h = 600;
vo.filters.rotate_deg = 90;
vo.filters.flip_h = true;

cv.convert_video(u"portrait.mkv", u"landscape.avi", vo);
```

### Example 6: Frame sequence

```cpp
knst_conv::converter cv;
knst_conv::video_filters flt;
flt.out_w = 320;

// One frame per second
auto r = cv.extract_frames(u"in.mkv", u"thumbs/", 1.0, 0, 90, &flt);
if (r) {
    std::cout << r.value.video_frames << " frames extracted\n";
}
```

### Example 7: MP3 stream copy (remux)

```cpp
// If MP4 has MP3:
knst_conv::audio_options ao;
ao.allow_copy = true;

cv.convert_audio(u"clip.mp4", u"clip.mp3", ao);
// .mp3 stream copy, bit-exact
```

### Example 8: MKA output (any track)

```cpp
// Stream copy AAC track to MKA
knst_conv::audio_options ao;
ao.allow_copy = true;

auto r = cv.convert_audio(u"in.mp4", u"in.mka", ao);
// Audio copied, container changed
```

### Example 9: Custom MP3 encoder

```cpp
knst_dev::result<void> my_mp3_encoder(void* user,
                                       const knst_aud::pcm_buffer& pcm,
                                       const knst_c16string& path) {
    // Encode with LAME or libmp3lame
    // Use pcm.samples, pcm.rate, pcm.channels
    // Write to path
    return knst_dev::result<void>::success();
}

knst_conv::converter::register_audio_encoder(
    knst_conv::format_t::mp3, my_mp3_encoder);

knst_conv::audio_options ao;
cv.convert_audio(u"in.wav", u"out.mp3", ao);
```

### Example 10: Cancel support

```cpp
std::atomic<bool> cancel{false};
cv.set_cancel(&cancel);

// From another thread:
std::thread t([&]() {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    cancel = true;
});

auto r = cv.convert_video(u"big.mkv", u"out.avi", vo);
if (!r && cv.was_cancelled()) {
    std::cout << "Cancelled\n";
}
t.join();
```

### Example 11: Probe + decision

```cpp
auto info = cv.probe(u"unknown.mp4");
if (info) {
    std::cout << "Format: " << knst_conv::format_name(info.value.format) << "\n";
    std::cout << "Duration: " << info.value.duration_s << " s\n";

    if (info.value.has_video && info.value.has_audio) {
        // A/V conversion
        cv.convert(u"unknown.mp4", u"output.mkv");
    } else if (info.value.has_audio) {
        // Audio conversion
        knst_conv::audio_options ao;
        ao.normalize = true;
        cv.convert_audio(u"unknown.mp4", u"output.wav", ao);
    } else if (info.value.has_video) {
        // Video only
        knst_conv::video_options vo;
        vo.vmode = knst_conv::video_mode::copy;
        cv.convert_video(u"unknown.mp4", u"output.mkv", vo);
    }
}
```

### Example 12: JSON report

```cpp
#if defined(KNST_JSON_HPP)
    auto r = cv.convert_video(u"in.mkv", u"out.avi", vo);
    if (r) {
        knst_json j = knst_conv::report_to_json(r.value);
        knst_json_buffer out;
        j.dump(out, knst_json_dump_options::pretty(2));
        std::cout << out.data() << "\n";
    }
#endif
```

---

## 📌 13) API Contract Summary

```cpp
namespace knst_conv {

namespace vid = ::knst_vid;
namespace aud = ::knst_aud;
namespace img = ::knst_vid::img;

using knst_dev::status;
using knst_dev::result;
using vid::errc;

// ─── Enums ───
enum class format_t : uint8_t { /* ... */ };
enum class video_mode : uint8_t { automatic, copy, transcode };
enum class audio_mode : uint8_t { automatic, copy, to_pcm, drop };

const char* format_name(format_t) noexcept;
bool is_av_container(format_t) noexcept;
vid::container to_vid_container(format_t) noexcept;
format_t format_from_path(const knst_c16string&) noexcept;
bool container_supports_video(vid::container, vid::video_codec) noexcept;
bool container_supports_audio(vid::container, vid::audio_codec) noexcept;

// ─── Types ───
struct report { /* ... */ };
struct input_info { /* ... */ };
typedef bool (*progress_fn)(void*, double, const char*);
struct audio_options { /* ... */ };
struct video_filters { /* ... */ };
struct video_options { /* ... */ };
typedef result<void> (*audio_encoder_fn)(void*, const aud::pcm_buffer&, const knst_c16string&);

// ─── Bitstream helpers ───
namespace bs {
    struct nal_range;
    struct param_sets;
    struct asc_info;
    struct elem_frame;
    struct elementary;

    bool is_annexb(const uint8_t*, uint32_t) noexcept;
    void split_annexb(const uint8_t*, uint32_t, knst_vector<nal_range>&) noexcept;
    bool parse_avcc(const knst_byte_string&, param_sets&) noexcept;
    bool parse_hvcc(const knst_byte_string&, param_sets&) noexcept;
    errc lp_to_annexb(const uint8_t*, uint32_t, uint32_t, const param_sets*, knst_byte_string&) noexcept;
    errc annexb_to_lp(const uint8_t*, uint32_t, bool, knst_byte_string&) noexcept;
    bool is_param_nal(const uint8_t*, bool) noexcept;
    bool collect_sps_pps(const uint8_t*, uint32_t, knst_byte_string&, knst_byte_string&) noexcept;
    knst_byte_string make_avcc(const knst_byte_string&, const knst_byte_string&) noexcept;

    const uint32_t* aac_rates() noexcept;
    asc_info parse_asc(const uint8_t*, size_t) noexcept;
    void adts_header(uint8_t*, const asc_info&, uint32_t) noexcept;
    bool find_asc_in_esds(const uint8_t*, size_t, const uint8_t*&, size_t&) noexcept;
    bool get_asc(const vid::stream_desc&, vid::container, knst_byte_string&) noexcept;

    bool mp3_header(const uint8_t*, uint32_t&, uint32_t&, uint32_t&, uint32_t&, int&) noexcept;
    errc scan_mp3(const knst_byte_string&, elementary&) noexcept;
    errc scan_adts(const knst_byte_string&, elementary&) noexcept;
    uint64_t skip_id3(const uint8_t*, uint64_t) noexcept;
}

// ─── Audio feeds ───
class audio_feed {
public:
    vid::audio_codec codec;
    uint32_t rate, channels;
    knst_byte_string extra;
    bool is_pcm;
    bool copied;
    virtual bool has() const noexcept = 0;
    virtual uint64_t pts_us() const noexcept = 0;
    virtual errc pop(knst_byte_string&) noexcept = 0;
};

class packet_feed : public audio_feed { /* ... */ };
class buffer_feed : public audio_feed { /* ... */ };
class frame_feed : public audio_feed { /* ... */ };

// ─── Video pumps ───
class video_pump {
public:
    uint32_t out_w, out_h;
    bool is_grey;
    uint64_t written;
    virtual errc prime() noexcept = 0;
    virtual bool has() const noexcept = 0;
    virtual uint64_t pts_us() const noexcept = 0;
    virtual errc step(vid::video_writer&) noexcept = 0;
};

class copy_pump : public video_pump { /* ... */ };
class transcode_pump : public video_pump { /* ... */ };

// ─── Encoder registry ───
bool register_audio_encoder(format_t, audio_encoder_fn, void* = nullptr) noexcept;

// ─── Converter ───
class converter {
public:
    converter() noexcept;

    void set_progress(progress_fn, void* = nullptr) noexcept;
    void set_cancel(const std::atomic<bool>*) noexcept;
    errc last_error() const noexcept;
    const knst_c16string& last_message() const noexcept;
    bool was_cancelled() const noexcept;
    static bool register_audio_encoder(format_t, audio_encoder_fn, void* = nullptr) noexcept;

    result<input_info> probe(const knst_c16string&) noexcept;
    result<report> convert_audio(const knst_c16string&, const knst_c16string&, const audio_options& = {}) noexcept;
    result<report> extract_audio(const knst_c16string&, const knst_c16string&, const audio_options& = {}) noexcept;
    result<report> convert_video(const knst_c16string&, const knst_c16string&, const video_options& = {}) noexcept;
    result<report> strip_audio(const knst_c16string&, const knst_c16string&) noexcept;
    result<report> add_audio(const knst_c16string&, const knst_c16string&, const knst_c16string&, video_options = {}) noexcept;
    result<report> extract_frame(const knst_c16string&, const knst_c16string&, double = 0.0, int = 90, const video_filters* = nullptr) noexcept;
    result<report> extract_frames(const knst_c16string&, const knst_c16string&, double = 1.0, uint32_t = 0, int = 90, const video_filters* = nullptr, bool = false) noexcept;
    result<report> convert(const knst_c16string&, const knst_c16string&) noexcept;
};

// ─── Free functions ───
inline result<report> convert(const knst_c16string&, const knst_c16string&) noexcept;
inline result<report> convert_audio(const knst_c16string&, const knst_c16string&, const audio_options& = {}) noexcept;
inline result<report> convert_video(const knst_c16string&, const knst_c16string&, const video_options& = {}) noexcept;
inline result<report> extract_audio(const knst_c16string&, const knst_c16string&, const audio_options& = {}) noexcept;
inline result<report> strip_audio(const knst_c16string&, const knst_c16string&) noexcept;
inline result<report> add_audio(const knst_c16string&, const knst_c16string&, const knst_c16string&, const video_options& = {}) noexcept;

// ─── JSON bridge ───
#if defined(KNST_JSON_HPP)
inline knst_json report_to_json(const report&) noexcept;
#endif

} // namespace knst_conv
```

---

## 🎯 What It Brings You

`knst_converter` gives you serious advantages in these situations:

- ✅ **Audio conversion** — WAV / AIFF / AU / RAW, 8-bit..f64
- ✅ **Audio processing chain** — Gain, normalize, DC removal, fade, limiter, trim
- ✅ **Stream copy remux** — Fast, no quality loss
- ✅ **H.264/H.265 framing** — Length-prefixed ↔ Annex-B (between AVI/MP4/MKV)
- ✅ **MJPEG transcode** — Resize, crop, rotate, flip, brightness, contrast, grey, fps
- ✅ **Audio mux** — Add / replace / drop / A/V offset
- ✅ **Frame export** — JPEG / PPM
- ✅ **Probe** — All info in one call
- ✅ **Progress + cancel** — Progress tracking and cancellation
- ✅ **Atomic error cleanup** — Partial output removed
- ✅ **Encoder/decoder registration** — Extendable
- ✅ **Bitstream helpers** — H.264/H.265, AAC, MP3
- ✅ **KernelNucleusT integration** — `knst_audio`, `knst_video`, `knst_file`, `knst_json`

---
