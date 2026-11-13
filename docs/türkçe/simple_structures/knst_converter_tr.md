# knst_converter — Medya Dönüştürücü, Çıkarıcı ve Muxer

Selamlar! 👋 `knst_converter.hpp`, KernelNucleusT'nin **medya dönüştürücü / çıkarıcı / muxer** modülü. `knst_audio.hpp` + `knst_video.hpp` üzerine kurulu. Ses dönüştürme, video konteyner dönüştürme, ses çıkarma, ses mux, kare çıkarma, remux ve muxing — hepsi tek header'da.

**Kısaca:** **Synchronous, exception-free, RTTI kullanmaz.** Her public çağrı `result<T>` döner. Hata durumunda `converter::last_error()` / `last_message()` ne ve neden olduğunu açıklar. İlerleme + iptal: `converter::set_progress()`, `converter::set_cancel()`. Başarısızlıkta partial output dosyaları **otomatik silinir**.

> ⚠️ **BAĞIMLILIKLAR:** `knst_devices.hpp` → `knst_file.hpp` → `knst_image_loader.hpp` → `knst_video.hpp` → `knst_audio.hpp` — bu sırayla include edilmeli. `KernelNucleusT.hpp` bunu halleder.

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

### Kapasite matrisi (kütüphanenin gerçek sınırları)

| Özellik | Durum | Not |
|---|---|---|
| **Ses dönüştürme** | ✅ | WAV / AIFF / AU / RAW (u8..f64, A-law, mu-law) |
| **Ses işleme** | ✅ | Sample rate, channel (5.1 downmix), gain, normalize, DC removal, fade, limiter, trim |
| **AV konteynerden ses çıkarma** | ✅ | PCM → WAV/AIFF/AU/RAW |
| **MP3 track stream copy** | ✅ | Bit-exact |
| **AAC track → ADTS** | ✅ | AudioSpecificConfig'ten ADTS framing (AAC-LC/Main/SSR/LTP) |
| **Herhangi track → MKA** | ✅ | Matroska audio, stream copy |
| **Video konteyner dönüştürme** | ✅ | AVI ↔ MKV ↔ MP4 ↔ MOV |
| **Stream copy (remux)** | ✅ | Hedef konteyner codec'i tutabiliyorsa |
| **H.264 / H.265 framing** | ✅ | Length-prefixed (MP4/MKV) ↔ Annex-B (AVI) |
| **MJPEG / RAW transcode** | ✅ | Resize, crop, rotate, flip, brightness/contrast, grey, fps, quality |
| **Time range cut** | ✅ | Copy: previous key frame; transcode: frame accurate |
| **Audio mux** | ✅ | Ekle / değiştir / düşür / PCM re-format / A/V offset |
| **Frame çıkarma** | ✅ | JPEG / PPM, tek veya sequence |
| **Probe** | ✅ | Tek çağrı ile dosya bilgisi |
| **H.264 / H.265 / VP9 / AV1 / AAC / Opus / MP3 / FLAC / Vorbis ENCODER** | ❌ | Kütüphanede yok |
| **H.264 / H.265 / VP9 / AV1 DECODER** | ❌ | `vid::external_codec::register_video_decoder()` gerekli |
| **AAC / Opus / FLAC / Vorbis / MP3 DECODER** | ⚠️ | MP3 minimp3 (dahili), AAC FAAD2 (opsiyonel). Diğerleri için `aud::codec::registry::add()` veya libsndfile |
| **"MP4 → MP3" dönüşümü** | ⚠️ | Sadece MP4 zaten MP3 içeriyorsa (stream copy) |
| **AAC → MP3** | ❌ | Gerçek encoder gerekir → `converter::register_audio_encoder()` |
| **Video H.264'e re-encode** | ❌ | Transcode hedefleri sadece MJPEG ve RAW (AVI) |
| **Non-MJPEG video decode** | ❌ | Resize/frame grab için `vid::external_codec::register_video_decoder()` |
| **MP4 yazıcı ses desteği** | ❌ | Sadece video |
| **MOV ses desteği** | ⚠️ | Sadece PCM_S16LE |
| **AVI ses desteği** | ⚠️ | Sadece PCM |
| **MKV ses desteği** | ✅ | Her şey |
| **WebM yazma** | ❌ | `video_writer` limiti → `.mkv` yaz (aynı stream'ler) |
| **HE-AAC (SBR/PS) → ADTS** | ❌ | AAC-LC / Main / SSR / LTP sadece |
| **Ses RAM'de tam decode** | ⚠️ | 2^30 sample limiti (`knst_audio`) |
| **Çoklu raw block per ADTS frame** | ❌ | `NotSupported` |

**Kritik not:** **Bu kütüphanede H.264/H.265/VP9/AV1/AAC/Opus/MP3/FLAC/Vorbis encode/decode YOK.** Video ve ses işlemleri:
- **Stream copy** (remux, transcode yok) — codec zaten uygunsa
- **MJPEG / RAW** transcoding (video)
- **PCM** transcoding (ses)
- **Harici decoder/encoder** kayıt ile genişletilebilir

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Ses dönüştürme** | WAV / AIFF / AU / RAW, 8-bit..f64, A-law, mu-law |
| **Ses işleme zinciri** | Gain, normalize, DC removal, fade, limiter, trim, resample, channel mix |
| **Stream copy** | Remux hızlı, kalite kaybı yok |
| **H.264/H.265 framing** | Length-prefixed ↔ Annex-B (AVI/MP4/MKV arası) |
| **MJPEG transcode** | Resize, crop, rotate, flip, brightness, contrast, grey, fps, quality |
| **Kare doğru time range** | Copy: previous key frame; transcode: frame accurate |
| **Audio mux** | Ekle / değiştir / düşür / A/V offset |
| **Frame export** | JPEG / PPM (tek kare veya sequence) |
| **Probe** | Tek çağrıda tüm bilgiler |
| **Progress + cancel** | `set_progress()`, `set_cancel()` |
| **Atomik hata temizliği** | Başarısızlıkta partial output silinir |
| **Encoder/decoder kaydı** | `register_audio_encoder()`, `vid::external_codec::register_*` |
| **Bitstream helpers** | H.264/H.265 Annex-B ↔ length-prefixed, AAC ADTS, MP3 frame scanner |
| **JSON bridge** | `report_to_json()` (KNST_JSON_HPP) |

---

## 🚀 Hızlı Başlangıç

### Auto-mode (dosya tiplerinden karar)

```cpp
#include "KernelNucleusT.hpp"

knst_conv::converter cv;

// MP4 → MKV remux (stream copy, re-encode yok)
auto r = cv.convert(u"clip.mp4", u"clip.mkv");
if (!r) {
    std::cerr << "Error: "
              << reinterpret_cast<const char*>(
                     knst_byte_string(cv.last_message()).data()) << "\n";
}
```

### Ses dönüştürme (işleme ile)

```cpp
knst_conv::audio_options ao;
ao.sample_rate = 16000;    // 44.1 kHz → 16 kHz
ao.channels    = 1;        // stereo → mono
ao.normalize   = true;
ao.remove_dc   = true;

cv.convert_audio(u"voice.flac", u"voice_16k.wav", ao);
```

### AV konteynerden ses çıkarma

```cpp
// MKV'den ses track'i WAV olarak çıkar, işle
knst_conv::audio_options ao;
ao.normalize = true;

cv.convert_audio(u"movie.mkv", u"speech.wav", ao);
```

### Video transcode (MJPEG AVI)

```cpp
knst_conv::video_options vo;
vo.filters.out_w    = 640;         // yükseklik aspect'ten
vo.jpeg_quality     = 80;
vo.start_s          = 10.0;
vo.end_s            = 25.0;

cv.convert_video(u"in.mkv", u"small.avi", vo);
```

### Video'ya ses ekleme

```cpp
cv.add_audio(u"silent.mp4", u"music.wav", u"out.mov");
```

### Frame çıkarma

```cpp
// 5. saniyedeki kare → JPEG
cv.extract_frame(u"in.mp4", u"thumb.jpg", 5.0);

// Her saniyede bir kare → frames/000001.jpg...
cv.extract_frames(u"in.mkv", u"frames/", 1.0);
```

---

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Probe** | `probe(path)` |
| **Ses** | `convert_audio(in, out, opts)`, `extract_audio(in, out, opts)` |
| **Video** | `convert_video(in, out, opts)`, `strip_audio(in, out)` |
| **Mux** | `add_audio(video_in, audio_in, out, opts)` |
| **Frame** | `extract_frame(in, out, time_s, quality)`, `extract_frames(in, out_dir, interval_s, ...)` |
| **Auto** | `convert(in, out)` |
| **Kontrol** | `set_progress()`, `set_cancel()`, `last_error()`, `last_message()`, `was_cancelled()` |
| **Kayıt** | `register_audio_encoder(format, fn)` |
| **Free functions** | `convert()`, `convert_audio()`, `convert_video()`, `extract_audio()`, `strip_audio()`, `add_audio()` |

---

## 🎯 1) Enum'lar ve Yardımcılar

### 1.1) `format_t` — Desteklenen formatlar

```cpp
enum class format_t : uint8_t {
    unknown,
    // AV konteynerler:
    avi, mkv, webm, mp4, mov,
    // Ses dosyaları:
    wav, aiff, au, flac, ogg, mp3, aac,
    raw_pcm, mka,
    // Resim:
    jpeg, ppm
};
```

**`format_name(f)`** → insan-okur string döner (`"mkv"`, `"wav"` vs.).

**`is_av_container(f)`** → `avi`, `mkv`, `webm`, `mp4`, `mov` için true.

**`to_vid_container(f)`** → `vid::container` enum'una çevirir.

**`format_from_path(p)`** → Dosya uzantısından `format_t` çıkarır. `.mp4` / `.m4v` / `.m4a` → `mp4`, `.qt` → `mov`, `.wave` / `.bwf` → `wav` vs.

### 1.2) `container_supports_video` / `container_supports_audio`

```cpp
bool container_supports_video(vid::container c, vid::video_codec v) noexcept;
bool container_supports_audio(vid::container c, vid::audio_codec a) noexcept;
```

**Video uyumu:**

| Konteyner | Desteklenen video codec'ler |
|---|---|
| **AVI** | MJPEG, RAW, H.264, H.265, VP8, VP9, AV1, MPEG4 |
| **MKV** / **WebM** | RAW, DNxHD, Unknown **hariç** her şey |
| **MP4** / **MOV** | MJPEG, H.264, H.265 |

**Ses uyumu:**

| Konteyner | Desteklenen ses codec'ler |
|---|---|
| **AVI** | Sadece PCM |
| **MKV** / **WebM** | Her şey |
| **MOV** | Sadece PCM_S16LE |
| **MP4** | ❌ (writer video only) |

### 1.3) `errc` — Hata enum'u

`knst_video.hpp`'den geliyor (`vid::errc`). `cdetail::from_status()` ile `knst_dev::status` → `errc` dönüşümü.

### 1.4) `report` — İşlem raporu

```cpp
struct report {
    bool     ok = false;
    bool     video_copied = false;      // Video re-encode edilmedi mi?
    bool     audio_copied = false;      // Ses re-encode edilmedi mi?
    uint64_t video_frames = 0;
    uint64_t audio_frames = 0;
    uint64_t bytes_out = 0;
    double   seconds = 0.0;
    format_t out_format = format_t::unknown;
    knst_vector<knst_c16string> notes;  // Kararlar + uyarılar
};
```

**`notes`** — İşlem sırasında verilen kararlar:
- `"audio dropped: the target container cannot store it (use .mkv or .mov to keep audio)"`
- `"stream copy cuts at the previous key frame (9.967 s instead of 10.000 s)"`
- `"H.264/H.265 rewritten from length-prefixed to Annex-B for AVI"`
- `"AAC stream copied (ADTS headers generated)"`
- `"external audio stream-copied"`

### 1.5) `input_info` — Probe sonucu

```cpp
struct input_info {
    format_t format = format_t::unknown;
    bool     av_container = false;
    bool     has_video = false;
    bool     has_audio = false;
    double   duration_s = 0.0;
    vid::media_info av;                 // AV konteyner için
    aud::media_info au;                 // Ses-only dosyalar için
};
```

### 1.6) `progress_fn`

```cpp
typedef bool (*progress_fn)(void* user, double fraction, const char* stage);
// false dönerse → iptal
```

**`stage`** değerleri: `"convert"`, `"audio"`, `"frames"`, `"copy"`, `"done"`.

---

## 🎵 2) `audio_options` — Ses Dönüştürme Seçenekleri

```cpp
struct audio_options {
    format_t format = format_t::unknown;              // unknown → uzantıdan
    aud::sample_fmt sample = aud::sample_fmt::i16;    // Çıktı sample format
    uint32_t sample_rate = 0;                         // 0 = koru
    uint32_t channels = 0;                            // 0 = koru (örn. 2 = 5.1 downmix)
    aud::dsp::resample_quality quality = high;
    double start_s = 0.0, end_s = 0.0;                // end 0 = sonuna kadar
    float gain_db = 0.0f;
    bool  normalize = false;
    float normalize_peak_db = -1.0f;
    bool  remove_dc = false;
    float fade_in_ms = 0.0f, fade_out_ms = 0.0f;
    bool  limiter = false;
    float limiter_db = -1.0f;
    bool  dither = true;                              // TPDF, sadece ≤16 bit için
    bool  allow_copy = true;                          // MP3→MP3 gibi stream copy
    bool  raw_big_endian = false;                     // RAW çıktı için
    uint32_t track = 0;                               // n-th ses track
    aud::decode_options decode;                       // RAW input parametreleri
};
```

**`needs_dsp()`** — Herhangi bir işlem gerekiyor mu? (sample_rate, channels, gain, normalize, remove_dc, fade, limiter)

### 2.1) Ses işleme sırası

`process_audio_()` içinde sırası:

1. **Channel conversion** (5.1 → stereo/mono downmix)
2. **Resampling** (44.1 → 16 kHz vs.)
3. **DC removal**
4. **Gain** (`apply_gain_db`)
5. **Normalize** (peak)
6. **Fade in**
7. **Fade out**
8. **Limiter**

**Dither:** Sadece ≤16 bit ve bit-depth reduction varsa uygulanır.

### 2.2) `sample_fmt` enum (knst_audio'dan)

- `u8`, `i16`, `i24`, `i32`, `f32`, `f64`, `alaw`, `ulaw`

**Not:** `dither` default `true` ama sadece mantıklıysa (≤16 bit) uygulanır.

### 2.3) Encoding çıktıları

`encode_out_()` **önce native** dener, sonra **sndfile fallback**, sonra **registered encoder**:

| Hedef | Native | Sndfile fallback | Registered encoder |
|---|---|---|---|
| **WAV** | ✅ | ✅ | — |
| **AIFF** | ✅ | ✅ | — |
| **AU** | ✅ | ✅ | — |
| **RAW** | ✅ | ✅ | — |
| **FLAC** | ✅ | ✅ | — |
| **OGG** | ⚠️ | ✅ (vorbis) | ✅ |
| **MP3** | ❌ | — | ✅ |
| **AAC** | ❌ | — | ✅ |

**WAV/AIFF/AU sınırı:** 4 GiB (RIFF limiti).

**Not:** FLAC native yazıcı PCM_16/24 destekler; f32/f64 isteği PCM_16'ya düşürülür.

---

## 🎬 3) `video_options` — Video Dönüştürme Seçenekleri

```cpp
enum class video_mode : uint8_t { automatic, copy, transcode };
enum class audio_mode : uint8_t { automatic, copy, to_pcm, drop };

struct video_options {
    format_t   container = format_t::unknown;           // unknown → uzantıdan
    video_mode vmode = video_mode::automatic;
    audio_mode amode = audio_mode::automatic;
    vid::video_codec vcodec = vid::video_codec::Unknown; // transcode: MJPEG (default) veya RAW (AVI only)
    int  jpeg_quality = 0;                              // 0 = 85 (transcode), keep (MJPEG→MJPEG copy)
    vid::mjpeg::subsampling jpeg_sub = s420;
    video_filters filters;
    double fps = 0.0;                                   // 0 = koru (transcode only)
    double start_s = 0.0, end_s = 0.0;
    audio_options audio;                                // Audio decode/process seçenekleri
    vid::audio_codec pcm_codec = vid::audio_codec::Unknown; // PCM tipi (Unknown = kaynaktan)
    knst_c16string external_audio;                      // Harici ses dosyası (replace)
    double external_audio_offset_ms = 0.0;              // >0 geciktir, <0 ilerlet
    uint32_t audio_track = 0;
    bool strict = false;                                // Audio drop yerine fail et
    knst_c16string title;                               // Title metadata override
};
```

### 3.1) `video_filters` — Video filtreleri

```cpp
struct video_filters {
    uint32_t crop_x, crop_y, crop_w, crop_h;            // crop_w/h == 0 → crop yok
    uint32_t rotate_deg;                                // 0/90/180/270 (saat yönü)
    bool flip_h, flip_v;
    uint32_t out_w, out_h;                              // biri set → aspect korunur; ikisi → exact
    double   scale;                                     // out_w/h yerine
    img::resize_filter filter;
    bool     auto_filter = true;                        // küçültmede box, büyütmede bilinear
    float brightness, contrast;
    bool grey;
};
```

**Filtre uygulama sırası:**
1. **Crop** (ilk)
2. **Rotate**
3. **Flip H**
4. **Flip V**
5. **Resize**
6. **Brightness/Contrast**
7. **Grey**

**`auto_filter` ne yapar?**
- **Küçültme** (`dw < w || dh < h`) → `box` (hızlı, antialiasing)
- **Büyütme** → `bilinear` (yumuşak)

---

## 🔀 4) `video_mode` / `audio_mode` — Karar Modları

### 4.1) `video_mode::automatic` (default)

Kütüphane karar verir:

```
copy = !needs_tc && copyable_codec && container_supports_video(oc, vsd.vcodec)

needs_tc = filt || fps > 0 || (jpeg_quality > 0 && src is MJPEG) || (vcodec != Unknown && vcodec != src)
copyable_codec = src != RAW && src != Unknown
```

**`copy` true ise** → stream copy (remux)
**`copy` false ise** → transcode (MJPEG veya RAW)

### 4.2) `video_mode::copy` — zorunlu stream copy

**Filtre / fps / quality değişikliği varsa → `InvalidArgument`.**

**Kodcağın hedef konteyner tarafından desteklenmiyorsa → `NotSupported`.**

### 4.3) `video_mode::transcode` — zorunlu re-encode

**Sadece MJPEG veya RAW hedefleyebilir.** Diğer codec'ler → `NotSupported`.

**Kaynak codec MJPEG/RAW değilse ve decoder kayıtlı değilse** → `NotSupported`.

### 4.4) `audio_mode` semantiği

| Mod | Davranış |
|---|---|
| **`automatic`** | Karar verir: copy → to_pcm → drop |
| **`copy`** | Sadece stream copy; aksi halde `NotSupported` |
| **`to_pcm`** | Decode + PCM olarak yeniden encode |
| **`drop`** | Ses tamamen düşürülür |

### 4.5) Audio stratejisi (automatic)

```
1. Dış ses yoksa ve amode=copy → stream copy dene
2. Codec + container uyumluysa ve DSP yoksa → stream copy
3. Aksi halde:
   a. PCM_S16LE container'da tutulabiliyorsa → decode + PCM encode
   b. Tutamıyorsa → audio drop + note
   c. strict modda → NotSupported
```

---

## 🎼 5) Audio Feed'leri

Kütüphane, audio writer'a paket sağlamak için 3 **`audio_feed`** sınıfı kullanır.

### 5.1) `packet_feed` — AV container'dan paket

```cpp
class packet_feed : public audio_feed {
public:
    errc init(vid::video_reader* rd, uint32_t track,
              vid::audio_codec pcm_target,
              uint64_t start_us, uint64_t end_us) noexcept;
};
```

**Ne yapar?**
- AV container'dan ses paketleri okur
- PCM ise bit-exact copy
- PCM ise ve target farklıysa → `convert_pcm`
- Codec'ler için: MP3, AAC, FLAC, Opus, Vorbis → passthrough
- AAC için **AudioSpecificConfig** (esds içinden veya direkt) çıkarır

### 5.2) `buffer_feed` — Decode edilmiş float buffer

```cpp
class buffer_feed : public audio_feed {
public:
    errc init(aud::pcm_buffer&& b, vid::audio_codec target) noexcept;
};
```

**Ne yapar?**
- `pcm_buffer` (float sample'lar) → hedef PCM codec'e çevirir
- 20 ms chunk'lar halinde verir
- Zero-copy değil — sample-by-sample dönüşüm

### 5.3) `frame_feed` — MP3/AAC frame'leri

```cpp
class frame_feed : public audio_feed {
public:
    errc init(knst_byte_string&& file, bs::elementary&& el,
              uint64_t start_us, uint64_t end_us,
              int64_t shift_us) noexcept;
};
```

**Ne yapar?**
- `.mp3` veya `.aac` dosyasından frame'leri sırayla verir
- Bit-exact passthrough
- Time range + shift uygular

---

## 🎥 6) Video Pump'ları

Kütüphane, writer'a frame sağlamak için 2 **`video_pump`** sınıfı kullanır.

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

**Ne yapar?**
- Paketleri **re-encode etmeden** okur
- Gerekirse **framing** dönüştürür:
  - `conv=1`: Length-prefixed → Annex-B (MP4/MKV → AVI)
  - `conv=2`: Annex-B → Length-prefixed (AVI → MP4/MKV)
- Parameter set'ler (SPS/PPS/VPS) key frame'de prepend edilir

**`allow_empty`** — AVI writer'ı empty packet kabul eder (VP9/AV1'de).

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

**Ne yapar?**
- Her frame'i `read_frame_rgb24` ile decode eder
- Filtreleri uygular
- Writer `write_frame` ile re-encode eder (MJPEG/RAW)

**`fps > 0`** → frame-at-time conversion:
- `start_us + (k / fps)` → `rd->frame_at_time(t)`
- Frame accurate time range

**`fps == 0`** → kaynak fps korunur, frame'ler sırayla decode edilir.

---

## 🔧 7) Bitstream Yardımcıları (`bs::`)

H.264/H.265 framing ve AAC/MP3 stream manipülasyonu için yardımcılar.

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

**`is_annexb`** — İlk 4 byte `00 00 01` veya `00 00 00 01` mi?

**`split_annexb`** — Annex-B stream'deki NAL unit'leri bulur.

**`parse_avcc`** — `avcC` box'ından SPS/PPS çıkarır.

**`parse_hvcc`** — `hvcC` box'ından VPS/SPS/PPS çıkarır.

### 7.2) Framing conversion

```cpp
errc lp_to_annexb(const uint8_t* d, uint32_t n, uint32_t ls,
                  const param_sets* pre, knst_byte_string& out) noexcept;
errc annexb_to_lp(const uint8_t* d, uint32_t n, bool h265,
                  knst_byte_string& out) noexcept;
```

**`lp_to_annexb`** — Length-prefixed (MP4/MKV) → Annex-B (AVI):
- `pre` verilirse parameter set'ler **öne eklenir** (key frame için)
- `ls` = length size (genellikle 4)

**`annexb_to_lp`** — Annex-B (AVI) → 4-byte length-prefixed (MP4/MKV):
- SPS/PPS/VPS **atlanır** (bunlar avcC/hvcC'de saklanır)

### 7.3) SPS/PPS yardımcıları

```cpp
bool is_param_nal(const uint8_t* nal, bool h265) noexcept;
bool collect_sps_pps(const uint8_t* d, uint32_t n,
                     knst_byte_string& sps, knst_byte_string& pps) noexcept;
knst_byte_string make_avcc(const knst_byte_string& sps,
                           const knst_byte_string& pps) noexcept;
```

**`is_param_nal`** — H.264: SPS (7) / PPS (8); H.265: VPS (32) / SPS (33) / PPS (34).

**`collect_sps_pps`** — Annex-B stream'den SPS/PPS toplar (initial frame'den).

**`make_avcc`** — SPS/PPS'den avcC box üretir.

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

**`adts_header`** — ADTS header (7 byte) üretir.

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

**`scan_mp3`** — MP3 Layer III frame'lerini tarar:
- ID3v2 atlar
- Xing/Info header frame atlar
- Frame senkronizasyonu için ardışık header kontrolü
- Sadece Layer III

**`scan_adts`** — ADTS frame'lerini tarar:
- ID3 atlar
- Birden fazla raw block reddeder
- İlk frame'den ASC oluşturur

### 7.6) MP3 header parse

```cpp
bool mp3_header(const uint8_t* h, uint32_t& flen, uint32_t& rate,
                uint32_t& ch, uint32_t& spf, int& ver) noexcept;
```

**Döndürdüğü:**
- `flen` = frame length
- `rate` = sample rate
- `ch` = channels
- `spf` = samples per frame (1152 / 576)
- `ver` = MPEG version (0=2.5, 2=2, 3=1)

### 7.7) ID3 skip

```cpp
uint64_t skip_id3(const uint8_t* d, uint64_t n) noexcept;
```

ID3v2 header varsa toplam boyut döner, yoksa 0.

---

## 🎯 8) `converter` — Ana Sınıf

```cpp
class converter {
public:
    converter() noexcept = default;

    // Kontrol
    void set_progress(progress_fn f, void* user = nullptr) noexcept;
    void set_cancel(const std::atomic<bool>* flag) noexcept;
    errc last_error() const noexcept;
    const knst_c16string& last_message() const noexcept;
    bool was_cancelled() const noexcept;
    static bool register_audio_encoder(format_t f, audio_encoder_fn fn, void* user = nullptr) noexcept;

    // Probe
    result<input_info> probe(const knst_c16string& path) noexcept;

    // Ses
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

### 8.1) `probe` — Dosya bilgisi

```cpp
auto r = cv.probe(u"video.mkv");
if (r) {
    std::cout << "Format: " << knst_conv::format_name(r.value.format) << "\n";
    std::cout << "Süre  : " << r.value.duration_s << " s\n";
    std::cout << "Video : " << (r.value.has_video ? "var" : "yok") << "\n";
    std::cout << "Ses   : " << (r.value.has_audio ? "var" : "yok") << "\n";

    if (r.value.av_container) {
        std::cout << "  Streams: " << r.value.av.streams.size() << "\n";
    }
}
```

**Ne yapar?**
1. Header byte'larından format tahmini (magic byte)
2. AV container ise `vid::video_reader` ile stream bilgilerini toplar
3. Audio-only ise `aud::probe()` çağırır
4. MP3/AAC için `bs::scan_mp3` / `bs::scan_adts`

### 8.2) `convert_audio` — Ses dönüştürme / çıkarma

```cpp
cv.convert_audio(in, out, audio_options);
```

**Karar ağacı:**

```
1. Input AV container mı?
   EVET → audio_from_container_()
   HAYIR → audio_from_file_()

audio_from_container_():
  1. Ses track'i bul (o.track = n-th)
  2. allow_copy && !DSP:
     - out=mp3 + src=mp3 → write_elementary_(adts=false)
     - out=aac + src=aac → write_elementary_(adts=true)
  3. out=mka → audio_feed (copy veya buffer) → write_audio_only_
  4. Aksi halde: load_track_pcm_ → process_audio_ → encode_out_

audio_from_file_():
  1. elem_in = mp3/aac
  2. !DSP && s_us==0 && e_us==0 && fin==fout → file copy
  3. out=mka && !DSP → frame_feed → write_audio_only_
  4. Aksi halde: aud::decode_file → trim → process → encode_out_
```

### 8.3) `convert_video` — Video dönüştürme

```cpp
cv.convert_video(in, out, video_options);
```

**Karar ağacı (özet):**

```
1. Input AV container olmalı
2. Output: avi/mkv/mp4/mov (webm reddedilir)
3. Ses + video track planlaması
4. copy / transcode kararı
5. Audio feed planla (plan_audio_)
6. Video pump kur (copy_pump veya transcode_pump)
7. Writer aç
8. Interleaved mux loop:
   - Video ve audio PTS karşılaştır
   - Küçük olanı yaz
9. Close
```

**Interleaved mux loop:** Video ve audio PTS'lerine göre dönüşümlü yazar. Video `pump->pts_us()` ≤ audio `feed->pts_us()` → video yaz; değilse audio yaz.

### 8.4) `strip_audio`

```cpp
cv.strip_audio(u"in.mkv", u"mute.mkv");
```

**Ne yapar?** `video_options o; o.amode = audio_mode::drop;` ile `convert_video`.

### 8.5) `add_audio`

```cpp
cv.add_audio(u"silent.mp4", u"music.wav", u"out.mov");
```

**Ne yapar?** `video_options o; o.external_audio = audio_in;` ile `convert_video`.

**Not:** External audio **kendi t=0'ından** başlar (videonun `start_us`'inden değil), video'nun trimmed süresiyle sınırlanır.

### 8.6) `extract_frame` / `extract_frames`

```cpp
// Tek kare
cv.extract_frame(u"in.mp4", u"thumb.jpg", 5.0, 90);

// Frame sequence
cv.extract_frames(u"in.mkv", u"frames/", 1.0, 0, 90, nullptr, false);
```

**Hedef format:** `.jpg` veya `.ppm`.

**MJPEG kaynak ise:** Bit-exact JPEG passthrough (quality igno). DHT standart tablo eksikse **inject edilir**.

**Non-MJPEG kaynak:** Decode → filtre → JPEG encode (veya PPM yaz).

**`extract_frames`** — `frame_000001.jpg` formatında isimlendirir.

### 8.7) `convert` — Auto dispatch

```cpp
cv.convert(in, out);
```

**Karar ağacı:**

```
1. Output AV container mı?
   EVET → convert_video
   HAYIR → 2
2. Output jpeg/ppm mi?
   EVET → extract_frame
   HAYIR → 3
3. Output uzantı bilinmiyor mu?
   EVET → InvalidArgument
   HAYIR → 4
4. → convert_audio
```

**Audio-only dosya → AV container:** `NotSupported` (anlamsız; `.mka` veya `add_audio()` kullan).

---

## ⚠️ 9) Sık Tuzaklar

### 1) H.264/H.265 ENCODER yok

Kütüphanede video encoder **YOK**. Transcode sadece **MJPEG** veya **RAW** hedefleyebilir.

```cpp
video_options vo;
vo.vcodec = vid::video_codec::H264;   // ❌ NotSupported
// ✅ vo.vcodec = vid::video_codec::MJPEG;
```

### 2) H.264/H.265 DECODER yok

Non-MJPEG video decode için `vid::external_codec::register_video_decoder()` **ŞART**.

```cpp
// ✅ Kullanıcı kayıt:
vid::external_codec::register_video_decoder(
    vid::video_codec::H264, my_decoder_fn, user);
```

Kayıtlı değilse: filter uygulanamaz, frame extract yapılamaz → `NotSupported`.

### 3) MP4 writer ses desteklemez

MP4 hedefli dönüşümde **ses otomatik düşürülür**. `report.notes`'ta bildirim olur.

**Ses ile birleştirmek için:**
- **MKV** kullan (her şey)
- **MOV** kullan (sadece PCM_S16LE)
- **strict = true** ile fail et

### 4) MOV sadece PCM_S16LE

MOV hedefinde başka PCM codec (PCM_F32LE, PCM_S24LE vs.) → `NotSupported`.

`pcm_codec = vid::audio_codec::PCM_S16LE` set et veya `Unknown` bırak (auto).

### 5) AVI sadece PCM

AVI hedefinde non-PCM ses → `NotSupported`.

### 6) WebM yazılamaz

```cpp
video_options vo;
vo.container = format_t::webm;   // ❌ NotSupported
// ✅ vo.container = format_t::mkv;   (aynı stream'ler, aynı kalite)
```

### 7) HE-AAC → ADTS çalışmaz

HE-AAC (SBR/PS) içeren AAC stream → ADTS `.aac` dosyası oluşturulamaz. AAC-LC/Main/SSR/LTP sadece.

```cpp
// HE-AAC kaynak:
cv.convert_audio(u"he-aac-in.mp4", u"out.aac");   // ❌ NotSupported
// ✅ çözüm: WAV veya MKA olarak çıkar
```

### 8) `AAC → MP3` gerçek encoder ister

```cpp
cv.convert_audio(u"in.aac", u"out.mp3");   // ❌ NotSupported (MP3 encoder yok)
// ✅ Kayıt gerekir:
knst_conv::converter::register_audio_encoder(
    knst_conv::format_t::mp3, my_mp3_encoder, user);
```

### 9) MP4 → MP3 sadece MP4'te MP3 varsa

```cpp
// MP4'te H.264 + AAC varsa:
cv.convert_audio(u"clip.mp4", u"clip.mp3");   // ❌ NotSupported (AAC→MP3)
// ✅ cv.convert_audio(u"clip.mp4", u"clip.mka");   (AAC stream copy)

// MP4'te H.264 + MP3 varsa:
cv.convert_audio(u"clip.mp4", u"clip.mp3");   // ✅ stream copy
```

### 10) Stream copy key frame'den keser

`video_mode::copy` modunda `start_s` **önceki key frame**'e yuvarlanır.

```cpp
vo.vmode = video_mode::copy;
vo.start_s = 10.0;
// `report.notes`:
// "stream copy cuts at the previous key frame (9.967 s instead of 10.000 s)"
```

**Frame-accurate cut için:** `video_mode::transcode` kullan.

### 11) MJPEG → MJPEG copy `jpeg_quality` ignore

```cpp
vo.vmode = video_mode::copy;   // MJPEG → MJPEG
vo.jpeg_quality = 90;          // ❌ ignore (bit-exact copy)
```

### 12) B-frame AVI'de sorun çıkarabilir

AVI writer **B-frame reordering** desteklemez.

```cpp
// H.264 B-frame içeren stream AVI'ye copy:
// `report.notes`:
// "warning: AVI cannot represent B-frame reordering; streams with B-frames may play wrongly"
```

### 13) `audio_options::dither` mantık kontrolü

`dither = true` default ama **sadece**:
- Hedef bit depth ≤ 16
- `b.source_bits == 0` (bilinmiyor) veya `> hedef_bits`

Aksi halde uygulanmaz.

### 14) `audio_options::normalize_peak_db = -1.0f`

`-1.0f` default = peak tam scale. `-3.0f` = 3 dB headroom.

**`from_db(-1.0)` → ~0.89 lineer.**

### 15) `limiter_db = -1.0f` default

Default -1 dBFS limit. Daha agresif için `-0.1f`, daha yumuşak için `-3.0f`.

### 16) `audio_options::channels = 2` = 5.1 downmix

Kaynak 5.1 ise → stereo downmix. Kaynak stereo ise → değişmez.

### 17) `audio_options::sample_rate = 0` = kaynaktan

Değiştirmek için explicit set et. `16000`, `48000` vs.

### 18) `external_audio_offset_ms` semantiği

```cpp
vo.external_audio_offset_ms = 500.0;   // 500 ms gecikme (başlangıçta silence)
vo.external_audio_offset_ms = -500.0;  // 500 ms ilerlet (baştan kes)
```

**Positive** → başlangıca silence ekle (audio delayed)
**Negative** → ilk N ms kes (audio advanced)

### 19) `external_audio` time range etkilenir

**Kritik:** External audio **video'nun trimmed süresiyle sınırlanır**, `start_us` ile değil.

```cpp
vo.start_s = 10.0;
vo.end_s = 30.0;
vo.external_audio = u"music.wav";

// Video: [10s, 30s] = 20 s
// Music: [0s, 20s] — music'in başında 10s atlanmaz
```

### 20) `audio_track` = n-th ses track

```cpp
vo.audio_track = 1;   // ikinci ses track'ini kullan
```

Yoksa → `NotFound`.

### 21) `strict = true` audio drop yerine fail

```cpp
vo.strict = true;
// Audio drop gerekirse → NotSupported
// Aksi halde: audio drop + note
```

### 22) `pcm_codec = Unknown` auto

Container tarafından kabul edilen en uygun codec seçilir:
1. Hedef container destekliyor mu?
2. Kaynak PCM tipi korunabilir mi?
3. Değilse → PCM_S16LE

### 23) `pcm_codec` explicit set → fail edebilir

```cpp
vo.pcm_codec = vid::audio_codec::PCM_F32LE;
vo.container = format_t::mov;   // MOV sadece S16LE
// → NotSupported
```

### 24) `cdetail::write_bytes` — Dosya yaz

Internal yardımcı. `ensure_parent_dir` + `file_handle::open` + `write` + `close`. Başarısızsa `IoError`.

### 25) `cdetail::file_size_of` — Dosya boyutu

Internal. `file_handle` ile dosyayı açar ve boyutunu döner.

### 26) Time range (audio) frame accuracy

`audio_from_container_`:
- **Stream copy** (`write_elementary_`): paket bazlı; keyframe'e yuvarlanabilir
- **Decode path**: sample-accurate trim (`dsp::trim`)

### 27) Time range (video transcode)

`transcode_pump`:
- **fps > 0**: `start_us + (k / fps)` → `frame_at_time(t)` → frame-accurate
- **fps == 0**: source frame iterate + `pump->has()` kontrol

### 28) `fps` conversion drift

`fps = 25.0` yazar → 25 FPS output. Kaynak 29.97 ise → frame conversion + drop/duplicate.

Küçük fps farkları için drop/duplicate kaçınılmaz.

### 29) `extract_frames` çıkış isimleri

`frame_000001.jpg`, `frame_000002.jpg`, ... (6 haneli, sıfır padded).

**PPM için:** `frame_000001.ppm`.

### 30) `extract_frame` MJPEG passthrough

MJPEG kaynak + `flt == nullptr` → bit-exact JPEG passthrough. `quality` igno.

**MJPEG'de DHT (Huffman table) yoksa** standart tablo inject edilir.

### 31) `MJPEG inject_std_dht`

Bazı MJPEG encoder'lar DHT segment'i göndermez (optimize). Bu durumda:

```cpp
vid::mjpeg::inject_std_dht(data, len, patched);
```

Standart DHT tablosu eklenir.

### 32) `container_supports_video(MKV, VC::RAW)` → false

MKV RAW video kabul etmez. AVI'de RAW → kullan.

### 33) `container_supports_video(AVI, VC::AV1)` → true ama uyarı

AVI'de AV1 "non-standard" — bazı player'lar açmaz.

`report.notes`'ta uyarı verilir:
```
"warning: this codec in AVI is non-standard; many players will not open it"
```

### 34) `container_supports_video(AVI, VC::H265)` → true ama B-frame sorunu

H.265 B-frame yaygın. AVI B-frame desteklemez. `report.notes`'ta uyarı.

### 35) Transcode hedef codec zorunlu

`video_mode::transcode` kullanıyorsan:
- `vcodec = VC::MJPEG` veya
- `vcodec = VC::RAW` (AVI only)

`VC::Unknown` → default MJPEG.

### 36) `automatic` modda copy vs transcode

`copy` her zaman mümkün değil:
- `vsd.vcodec == RAW` veya `Unknown` → copy NO
- `!container_supports_video(oc, vsd.vcodec)` → copy NO
- `filters.active()` → transcode
- `fps > 0` → transcode
- `jpeg_quality > 0 && src MJPEG` → transcode (kalite değiştirmek)
- `vcodec != Unknown && vcodec != src` → transcode

### 37) `bitstream` MP3 stateful decode

MP3 **frame'ler arası state** paylaşır (bit reservoir). Paket paket decode **YANLIŞ**. Kütüphane **tüm MP3 stream'i sırayla** minimp3'e verir.

RAM limiti: 500 KB buffer + 1 frame (previous 2 GB approach yerine).

### 38) AAC FAAD2 opsiyonel

`KNST_NETWORK_HAVE_FAAD2` define'lı olmalı. Yoksa AAC decode **desteklenmiyor** (ADTS stream copy sadece).

### 39) `MINIMP3_MAX_SAMPLES_PER_FRAME` bağımlılığı

MP3 decode `minimp3` kullanır (knst_audio içinde gelir).

### 40) Audio RAM limiti

`aud::pcm_buffer` **tam decode** edilir (float). 2^30 sample sınırı (~2 GB float).

**Uzun ses dosyaları için:** `knst_aud::stream` kullan (streaming).

### 41) `report.notes` uyarıları okuyun

`report.notes` **kararları** ve **uyarıları** içerir. `ok=true` olsa bile notlar önemli.

```cpp
if (r.value.ok) {
    for (const auto& note : r.value.notes) {
        std::cout << "Note: " << note << "\n";
    }
}
```

### 42) `probe` için `av.valid` kontrolü

`input_info.av` sadece `av_container == true` ise geçerli.

### 43) `probe` için `au.valid` kontrolü

`input_info.au` sadece `av_container == false` ve ses dosyası ise geçerli.

### 44) `error` durumunda output silinir

`convert_audio` / `convert_video` failure → `knst_file::remove_file(out)`. Partial output yok.

### 45) `knst_file::copy_file` fast path

`fin == fout && mp3/aac && no_dsp && no_trim` → **dosya kopyalama** (decode yok).

`report.notes`: `"same format, file copied"`.

### 46) `audio_from_container_` MKA çıkışı

`out=mka` → stream copy (packet_feed) veya PCM re-encode (buffer_feed).

**Not:** packet_feed sadece PCM/MP3/AAC/FLAC/Opus/Vorbis kaynaktan MKV/WebM'den copy yapar.

### 47) `plan_audio_` external audio AV container ise

```cpp
vo.external_audio = u"movie.mkv";   // başka video!
```

Video reader açılır, `audio_track` (n-th) ses track'i alınır.

### 48) `cross-container` AAC copy

MP4'teki AAC → MKA yaz:
- `sd.extradata` esds içinde AudioSpecificConfig
- `bs::get_asc()` esds'i parse eder
- `packet_feed` stream copy yapar

**Doğru.**

### 49) AAC esds parse edge case

`find_asc_in_esds` recursive descent yapar:
- Tag 0x03 (ES_Descriptor) → flags parse
- Tag 0x04 (DecoderConfigDescriptor) → 13 byte skip
- Tag 0x05 (DecoderSpecificInfo) → ASC

Bazı eski MP4'ler **nested** esds içerir.

### 50) `converter::register_audio_encoder` static

```cpp
knst_conv::converter::register_audio_encoder(format_t::mp3, my_fn);
// veya
knst_conv::register_audio_encoder(format_t::mp3, my_fn);
```

**Global registry** — process-wide. `mutex` ile korunur.

---

## 📊 10) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| **probe** | 10-100 ms | Container parse |
| **Ses dönüştürme (kopya)** | O(1) | Fast file copy |
| **Ses decode** | ~50-200× realtime | float decode |
| **Ses işleme** | ~20-50× realtime | DSP |
| **Ses encode** | ~50-100× realtime | PCM encode |
| **MP3 stream copy** | ~100× realtime | Elementary copy |
| **AAC stream copy** | ~100× realtime | ADTS generate |
| **MKA write** | ~50× realtime | Matroska |
| **Video remux** | ~50-200× realtime | Packet copy |
| **Video MJPEG transcode** | ~1-5× realtime | Decode + re-encode |
| **Frame extract** | ~1 kare/sn | Per frame |
| **Frame sequence** | interval bağımlı | |

**Tipik latencies:**
- **MP4 → MKV remux (5 dk video):** 2-5 saniye
- **FLAC → WAV (3 dk ses):** 1-2 saniye
- **5.1 downmix + 16 kHz resample (3 dk):** 2-4 saniye
- **MJPEG AVI transcode (1 dk video, 640×480):** 10-30 saniye
- **Frame extract (1 kare):** 100-500 ms

**Optimizasyon:**
- **Stream copy** kullan (`video_mode::copy`, `audio_mode::copy`) — re-encode yok
- **Filter uygulama** — `filters.active() == false` ise bypass
- **Uzun dosyalar için** streaming (`knst_aud::stream`)

---

## ✅ 11) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Auto mode kullan (kod uzunluğu)
auto r = cv.convert(u"in.mp4", u"out.mkv");

// 2. Remux için video_mode::copy
knst_conv::video_options vo;
vo.vmode = knst_conv::video_mode::copy;
cv.convert_video(u"in.mkv", u"out.mp4", vo);   // ✅ stream copy

// 3. Ses ile birleştirmek için .mkv veya .mov
vo.container = knst_conv::format_t::mkv;
cv.convert_video(u"in.mkv", u"out.mkv", vo);

// 4. Notes okuyun
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

// 7. Kayıtlı encoder kullan
knst_conv::converter::register_audio_encoder(
    knst_conv::format_t::mp3, my_mp3_encoder);

// 8. MJPEG quality
vo.jpeg_quality = 85;
vo.jpeg_sub = vid::mjpeg::subsampling::s420;

// 9. Frame-accurate cut için transcode
vo.vmode = knst_conv::video_mode::transcode;
vo.start_s = 10.0;
vo.end_s = 30.0;
```

### ❌ Yapma

```cpp
// 1. H.264/H.265 encoder bekle
vo.vcodec = vid::video_codec::H264;   // ❌ NotSupported

// 2. Non-MJPEG decoder bekle
cv.extract_frame(u"h264.mkv", u"thumb.jpg", 0);   // ❌ NotSupported

// 3. MP4'te ses bekle
vo.container = knst_conv::format_t::mp4;
// ❌ Audio dropped (writer video only)

// 4. WebM yaz
vo.container = knst_conv::format_t::webm;   // ❌ NotSupported
// ✅ vo.container = knst_conv::format_t::mkv;

// 5. HE-AAC → ADTS
cv.convert_audio(u"he-aac.mp4", u"out.aac");   // ❌ NotSupported

// 6. AAC → MP3 kayıt olmadan
cv.convert_audio(u"aac.aac", u"out.mp3");   // ❌ NotSupported

// 7. MP3 decoder kayıt olmadan (aslında minimp3 dahili ✅)

// 8. Copy mode + filter
vo.vmode = knst_conv::video_mode::copy;
vo.filters.out_w = 640;   // ❌ InvalidArgument

// 9. Copy mode + fps
vo.vmode = knst_conv::video_mode::copy;
vo.fps = 25.0;   // ❌ InvalidArgument

// 10. Copy mode + quality change
vo.vmode = knst_conv::video_mode::copy;
vo.jpeg_quality = 90;   // ❌ MJPEG→MJPEG copy'de ignore
```

---

## 🔥 12) Gerçek Kullanım Örnekleri

### Örnek 1: Hızlı remux (re-encode yok)

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

### Örnek 2: Speech extraction (5.1 → mono 16 kHz)

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

### Örnek 3: MJPEG AVI transcode (resize)

```cpp
knst_conv::video_options vo;
vo.vmode = knst_conv::video_mode::transcode;
vo.filters.out_w = 640;
vo.filters.filter = knst_vid::img::resize_filter::bilinear;
vo.jpeg_quality = 80;
vo.fps = 25.0;

auto r = cv.convert_video(u"in.mkv", u"small.avi", vo);
```

### Örnek 4: Video'ya müzik ekleme

```cpp
knst_conv::video_options vo;
vo.external_audio = u"music.flac";
vo.external_audio_offset_ms = 500.0;   // 500 ms gecikme
vo.container = knst_conv::format_t::mov;

auto r = cv.add_audio(u"silent.mp4", u"music.flac", u"out.mov", vo);
```

### Örnek 5: Crop + rotate

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

### Örnek 6: Frame sequence

```cpp
knst_conv::converter cv;
knst_conv::video_filters flt;
flt.out_w = 320;

// Her saniyede bir kare
auto r = cv.extract_frames(u"in.mkv", u"thumbs/", 1.0, 0, 90, &flt);
if (r) {
    std::cout << r.value.video_frames << " frames extracted\n";
}
```

### Örnek 7: MP3 stream copy (remux)

```cpp
// MP4'te MP3 varsa:
knst_conv::audio_options ao;
ao.allow_copy = true;

cv.convert_audio(u"clip.mp4", u"clip.mp3", ao);
// .mp3 stream copy, bit-exact
```

### Örnek 8: MKA çıkışı (herhangi track)

```cpp
// AAC track'ini MKA'ya stream copy
knst_conv::audio_options ao;
ao.allow_copy = true;

auto r = cv.convert_audio(u"in.mp4", u"in.mka", ao);
// Audio copied, container changed
```

### Örnek 9: Custom MP3 encoder

```cpp
knst_dev::result<void> my_mp3_encoder(void* user,
                                       const knst_aud::pcm_buffer& pcm,
                                       const knst_c16string& path) {
    // LAME veya libmp3lame ile encode
    // pcm.samples, pcm.rate, pcm.channels kullan
    // path'e yaz
    return knst_dev::result<void>::success();
}

knst_conv::converter::register_audio_encoder(
    knst_conv::format_t::mp3, my_mp3_encoder);

knst_conv::audio_options ao;
cv.convert_audio(u"in.wav", u"out.mp3", ao);
```

### Örnek 10: Cancel support

```cpp
std::atomic<bool> cancel{false};
cv.set_cancel(&cancel);

// Başka thread:
std::thread t([&]() {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    cancel = true;
});

auto r = cv.convert_video(u"big.mkv", u"out.avi", vo);
if (!r && cv.was_cancelled()) {
    std::cout << "İptal edildi\n";
}
t.join();
```

### Örnek 11: Probe + karar

```cpp
auto info = cv.probe(u"unknown.mp4");
if (info) {
    std::cout << "Format: " << knst_conv::format_name(info.value.format) << "\n";
    std::cout << "Duration: " << info.value.duration_s << " s\n";

    if (info.value.has_video && info.value.has_audio) {
        // A/V conversion
        cv.convert(u"unknown.mp4", u"output.mkv");
    } else if (info.value.has_audio) {
        // Ses dönüştürme
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

### Örnek 12: JSON rapor

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

## 📌 13) API Kontrat Özeti

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

## 🎯 Size Getireceği Kazanç

`knst_converter` şu durumlarda ciddi avantaj sağlar:

- ✅ **Ses dönüştürme** — WAV / AIFF / AU / RAW, 8-bit..f64
- ✅ **Ses işleme zinciri** — Gain, normalize, DC removal, fade, limiter, trim
- ✅ **Stream copy remux** — Hızlı, kalite kaybı yok
- ✅ **H.264/H.265 framing** — Length-prefixed ↔ Annex-B (AVI/MP4/MKV arası)
- ✅ **MJPEG transcode** — Resize, crop, rotate, flip, brightness, contrast, grey, fps
- ✅ **Audio mux** — Ekle / değiştir / düşür / A/V offset
- ✅ **Frame export** — JPEG / PPM
- ✅ **Probe** — Tek çağrıda tüm bilgi
- ✅ **Progress + cancel** — İlerleme takibi ve iptal
- ✅ **Atomik hata temizliği** — Partial output silinir
- ✅ **Encoder/decoder kaydı** — Genişletilebilir
- ✅ **Bitstream helpers** — H.264/H.265, AAC, MP3
- ✅ **KernelNucleusT entegrasyonu** — `knst_audio`, `knst_video`, `knst_file`, `knst_json`

---