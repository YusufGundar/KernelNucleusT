# knst_audio — Ses Cihazları, Kodekler, I/O ve DSP Kütüphanesi

Selamlar! 👋 `knst_audio.hpp`, KernelNucleusT'nin **genel amaçlı ses** modülü. Tek header'da: cihaz numaralandırma, volume/mute/balance/port/card/server, per-app stream'ler, gerçek zamanlı blocking stream, format detection (20+ konteyner), **native decoder'lar** (WAV/AIFF/AU/FLAC/MP3/RAW), **native encoder'lar** (WAV/AIFF/AU/RAW/**FLAC**/**Vorbis**), streaming WAV writer, **memory-bounded decoder_stream**, DSP (gain/fade/mix/normalize/resample/biquad/FFT/generator), `player`, `mixer`, hotplug monitor.

**Kısaca:** **Synchronous, exception-free, RTTI yok.** Her public çağrı `result<T>` döner. **Header-only**, `knst_file`, `knst_devices`, `knst_dev` üzerine kurulu. İç sample temsili **interleaved float [-1, 1]**. `decoder_stream` sayesinde **RAM sabit** (~200 KB, dosya boyutundan bağımsız) streaming decode yapılır — 3 saatlik MP3 bile RAM'i şişirmez.

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

### Platform matrisi

| Özellik | Linux (PulseAudio/PipeWire) | Windows (WASAPI) | Android (AAudio) |
|---|---|---|---|
| **Device list** | ✅ | ✅ | ✅ |
| **Volume / Mute** | ✅ | ✅ | ✅ (system stream) |
| **Volume dB** | ✅ | ✅ | ⚠️ (percent → dB) |
| **Channel volume** | ✅ (front L/R) | ✅ (endpoint channels) | ⚠️ (system only) |
| **Balance** | ✅ | ❌ `NotSupported` | ❌ `NotSupported` |
| **Ports** | ✅ | ❌ `NotSupported` | ❌ `NotSupported` |
| **Cards / Profiles** | ✅ | ❌ `NotSupported` | ❌ `NotSupported` |
| **Server info** | ✅ (`pactl info`) | ❌ `NotSupported` | ❌ `NotSupported` |
| **Default device** | ✅ | ✅ | ✅ |
| **Set default** | ✅ | ✅ (`IPolicyConfig`) | ❌ `NotSupported` |
| **Per-app streams (sink-inputs)** | ✅ | ✅ (`IAudioSessionManager2`) | ❌ `NotSupported` |
| **Per-app volume/mute** | ✅ | ✅ | ❌ `NotSupported` |
| **Move stream** | ✅ | ❌ `NotSupported` | ❌ `NotSupported` |
| **Cork/suspend stream** | ✅ | ❌ `NotSupported` | ❌ `NotSupported` |
| **Real-time stream** | ✅ `pa_simple` (dlopen) | ✅ WASAPI shared | ✅ AAudio |
| **Stream format** | float32 interleaved | float32 interleaved | float32 interleaved |
| **Peak meter** | ✅ (`parec`/`pw-cat`/`pw-record`) | ✅ (`IAudioMeterInformation` + loopback) | ❌ `NotSupported` |
| **Hotplug monitor** | ✅ (polling) | ✅ (polling) | ✅ (polling) |
| **AudioFocus** | — | — | ✅ (`KnstAudioFocus` helper) |
| **Native WAV/RF64/BW64 decode** | ✅ | ✅ | ✅ |
| **Native AIFF/AIFC decode** | ✅ | ✅ | ✅ |
| **Native AU decode** | ✅ | ✅ | ✅ |
| **Native FLAC decode** | ✅ | ✅ | ✅ |
| **Native FLAC encode** | ✅ | ✅ | ✅ |
| **Native MP3 decode (minimp3)** | ✅ | ✅ | ✅ |
| **Native OGG Vorbis encode** | ⚠️ (`libvorbis`) | ⚠️ (`libvorbis`) | ⚠️ (`libvorbis`) |
| **Native AAC decode (FAAD2)** | ⚠️ (`KNST_NETWORK_HAVE_FAAD2`) | ⚠️ | ⚠️ |
| **RAW decode/encode** | ✅ | ✅ | ✅ |
| **ADPCM (IMA/MS) WAV decode** | ✅ | ✅ | ✅ |
| **Sndfile backend (OGG/OPUS/WavPack/APE/TTA)** | ⚠️ (`libsndfile`) | ⚠️ (`libsndfile`) | ⚠️ (`libsndfile`) |
| **Streaming decoder (memory-bounded)** | ✅ | ✅ | ✅ |
| **MP3 seek (lazy frame index)** | ✅ | ✅ | ✅ |
| **MP3 duration (Xing/VBRI)** | ✅ | ✅ | ✅ |
| **DSD / MIDI / AC3 / AMR** | ❌ `NotSupported` | ❌ | ❌ |
| **DSP (gain, fade, mix, normalize)** | ✅ | ✅ | ✅ |
| **Channel conversion (5.1 → stereo)** | ✅ | ✅ | ✅ |
| **Resampling (linear + windowed-sinc)** | ✅ | ✅ | ✅ |
| **Biquad filters (RBJ)** | ✅ | ✅ | ✅ |
| **FFT / spectrum** | ✅ | ✅ | ✅ |
| **`player` (seek/loop/gain)** | ✅ | ✅ | ✅ |
| **`mixer` (N sources → 1 bus)** | ✅ | ✅ | ✅ |
| **JSON bridge (`KNST_JSON_HPP`)** | ✅ | ✅ | ✅ |

**Sembol açıklaması:** ✅ = tam destek · ⚠️ = koşullu · ❌ = yok

**Kritik notlar:**
- **OPUS decode/encode**: Yalnızca libsndfile üzerinden (native yok).
- **MP3 encode**: **YOK**. AAC→MP3 gibi dönüşümler için `codec::registry::add()` ile kendi decoder'ını kaydet veya `knst_converter::register_audio_encoder()` kullan.
- **AAC decode**: `KNST_NETWORK_HAVE_FAAD2` build-time define'ı gerekir. Yoksa ADTS stream copy (`knst_converter` içinde) kullan.
- **OGG Vorbis encode**: `KNST_NETWORK_HAVE_VORBIS` build-time define'ı + libvorbis/libvorbisenc/libogg kütüphaneleri gerekir.
- **Windows per-app capture streams** (mikrofon kullanan uygulamalar): `list_source_outputs()` → `NotSupported`. Sadece render (çıkış) sessions desteklenir.
- **Android** `NotSupported`: balance, ports, cards, profiles, server, set_default, move/cork stream, peak meter.
- **Android AudioFocus**: `request_audio_focus()` / `abandon_audio_focus()` için app tarafında `com.knst.print.KnstAudioFocus` Java class'ı gerekir.

### Bağımlılıklar

| Bağımlılık | Ne için? | Not |
|---|---|---|
| **`knst_file`** | Dosya okuma/yazma | Zorunlu |
| **`knst_devices`** | `audio()` device list | Zorunlu |
| **`knst_dev`** | `status`, `result`, `argv`, `run_tool_raw` | Zorunlu |
| **`minimp3.h`** | MP3 decode | Header dahil |
| **`libpulse-simple`** (Linux) | `stream` için | `dlopen`, **zorunlu değil** |
| **`pactl`/`paplay`** (Linux) | Device/volume/play | **Zorunlu** (Linux'ta) |
| **`libsndfile`** (opsiyonel) | OGG/OPUS/WavPack/APE/TTA + AAC backend | `dlopen`/`LoadLibrary` |
| **FAAD2** (opsiyonel) | AAC decode | `KNST_NETWORK_HAVE_FAAD2` |
| **libvorbis** (opsiyonel) | OGG encode | `KNST_NETWORK_HAVE_VORBIS` |
| **WASAPI** (Windows) | `stream`, per-app session | Windows SDK |

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Native WAV/RF64/BW64 decode** | PCM 8/16/24/32, float32/64, A-law, µ-law, IMA-ADPCM, MS-ADPCM, extensible |
| **Native AIFF/AIFC decode** | PCM (BE/LE "sowt"), float, A-law, µ-law |
| **Native AU decode** | PCM 8/16/24/32, float32/64, A-law, µ-law |
| **Native FLAC decode** | Fixed/LPC/verbatim, CRC-8/16, tüm channel modes, 4..32 bit |
| **Native FLAC encode** | **Zero dependency** — sıfırdan yazılmış spec-compliant encoder |
| **Native MP3 decode** | minimp3 (CC0), tüm MPEG-1/2/2.5 Layer III |
| **Format detection** | Magic bytes + extension (20+ konteyner) |
| **Sndfile backend** | OGG/OPUS/WavPack/APE/TTA (opsiyonel `libsndfile`) |
| **Streaming WAV writer** | Header on open, patch on close |
| **`decoder_stream`** | **~200 KB RAM**, dosya boyutundan bağımsız, MP3 seek + duration |
| **DSP zinciri** | gain, fade, mix, trim, normalize (peak/RMS), DC remove, limiter, soft-clip |
| **Channel conversion** | 5.1/7.1 → stereo/mono, mono → N |
| **Resampling** | Linear (fast) + windowed-sinc (Kaiser, 4 seviye) |
| **Biquad filters** | RBJ cookbook (LP/HP/BP/notch/allpass/peak/shelf) |
| **FFT / spectrum** | Radix-2 in-place FFT, magnitude spectrum, dominant freq |
| **Generators** | Sine, square, saw, triangle, noise |
| **`player`** | Seek + loop + gain, any format |
| **`mixer`** | N kaynağın tek bus'a mixlenmesi, allocation-free render |
| **Real-time stream** | Linux `pa_simple`, Windows WASAPI shared, Android AAudio |
| **Hotplug monitor** | Polling-based (500 ms latency), thread-safe |
| **Per-app streams** | Linux sink-inputs, Windows `IAudioSessionManager2` |
| **Peak meter** | Linux (parec/pw-cat/pw-record), Windows (`IAudioMeterInformation` + loopback) |
| **AudioFocus** | Android best practice helper |
| **JSON bridge** | `devices_to_json`, `stream_info_to_json`, `media_info_to_json` |
| **Registry** | `codec::registry::add()` custom decoder |
| **Memory limits** | `max_samples = 2^30` (4 GB float) | 

---

## 🚀 Hızlı Başlangıç

### Cihazları listele

```cpp
#include "KernelNucleusT.hpp"

auto sinks = knst_aud::audio::list_sinks();
if (sinks) {
    for (const auto& d : sinks.value) {
        std::cout << d.name << "  " << d.description << "\n";
    }
}
```

### Volume ayarla

```cpp
auto sinks = knst_aud::audio::list_sinks();
if (!sinks || sinks.value.empty()) return;

// Linear percent
knst_aud::audio::set_volume(sinks.value[0], 75);

// dB
knst_aud::audio::set_volume_db(sinks.value[0], -6.0);   // ~50%
```

### WAV decode + oynat

```cpp
auto r = knst_aud::play_file_any(nullptr, u"music.wav");
if (!r) {
    std::cerr << knst_dev::status_string(r.st) << "\n";
}
```

### FLAC encode

```cpp
knst_aud::pcm_buffer buf;
// ... buf.samples, buf.rate, buf.channels doldur ...

knst_aud::encode_options o;
o.container = knst_aud::container_t::flac;
o.fmt = knst_aud::sample_fmt::i16;

knst_aud::encode_file(u"out.flac", buf, o);
```

### Streaming decode (memory-bounded)

```cpp
auto ds = knst_aud::decoder_stream::open(u"3hour-long.mp3");
if (!ds) return;

knst_aud::stream::config c;
c.rate = ds.value.rate();
c.channels = ds.value.channels();
c.is_output = true;

auto s = knst_aud::stream::open_default(c);
if (!s) return;

// 3 saat oynat — RAM sabit
ds.value.play_to(s.value);
```

### DSP zinciri

```cpp
knst_aud::pcm_buffer buf;
knst_aud::decode_file(u"input.wav").value;   // örnek

knst_aud::dsp::normalize(buf.samples.data(),
                          buf.samples.size(),
                          0.95f);
knst_aud::dsp::fade_in(buf.samples.data(),
                        buf.frames(), buf.channels,
                        buf.rate / 4);   // 250 ms

// 5.1 → stereo downmix
auto st = knst_aud::dsp::convert_channels(
    buf.samples.data(), buf.frames(),
    buf.channels, 2);
```

---

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Device** | `list()`, `list_sinks()`, `list_sources()`, `get_default()`, `set_default()`, `get_device_detail()` |
| **Volume** | `get_volume`, `set_volume`, `get_volume_db`, `set_volume_db`, `get_channel_volume`, `set_channel_volume`, `get_balance`, `set_balance`, `is_muted`, `set_mute` |
| **Format** | `get_format`, `get_channel_count` |
| **Port/Card/Server** | `get_ports`, `set_port`, `list_cards`, `set_profile`, `server` |
| **Streams** | `list_sink_inputs`, `list_source_outputs`, `list_streams`, `set_stream_volume`, `set_stream_mute`, `move_stream`, `cork_stream`, `suspend` |
| **Peak** | `get_peak_meter` |
| **Playback** | `play_file`, `play_raw`, `play_buffer`, `play_file_any` |
| **Android** | `request_audio_focus`, `abandon_audio_focus` |
| **Format det.** | `container_t`, `codec_t`, `sample_fmt`, `detect()`, `detect_extension()`, `is_native()` |
| **Decode** | `decode()`, `decode_file()`, `decode_to_float()`, `probe()` |
| **Encode** | `encode()`, `encode_file()`, `wav::encode`, `wav::encode_memory` |
| **WAV writer** | `wav_writer` (streaming) |
| **DSP** | `dsp::*` |
| **Stream** | `stream::open()`, `stream::open_default()`, `write_interleaved`, `read_interleaved`, `drain` |
| **Player/Mixer** | `player::open`, `player::from_buffer`, `mixer::add`, `mixer::render` |
| **Streaming decode** | `decoder_stream::open`, `read`, `seek_frames`, `play_to` |
| **Hotplug** | `hotplug_monitor::start()` |
| **Registry** | `codec::registry::add()` |

---

## 🎯 1) Tipler ve Enum'lar

### 1.1) `container_t` — Konteyner tipi

```cpp
enum class container_t : uint8_t {
    unknown, raw, wav, rf64, w64, aiff, aifc, au, caf, flac, ogg, mp3,
    aac_adts, mp4, wavpack, ape, tta, dsf, dsdiff, midi, amr, ac3, asf,
    matroska
};
```

**Native decode:** `raw`, `wav`, `rf64`, `aiff`, `aifc`, `au`, `flac`, `mp3`.
**Sndfile backend:** `ogg`, `wavpack`, `ape`, `tta`, `mp3` (yedek), `aac_adts` (FAAD2), `mp4`.
**Desteklenmez:** `dsf`, `dsdiff`, `midi`, `ac3`, `asf`, `matroska`, `caf`, `w64`.

`container_name(c)` → insan-okur string.

### 1.2) `codec_t` — Kodek tipi

```cpp
enum class codec_t : uint8_t {
    unknown, pcm, float_pcm, alaw, ulaw, ima_adpcm, ms_adpcm,
    flac, mp3, vorbis, opus, aac
};
```

### 1.3) `sample_fmt` — Ham sample formatı

```cpp
enum class sample_fmt : uint8_t { u8, i8, i16, i24, i32, f32, f64, alaw, ulaw };
```

`sample_bytes(sample_fmt)` → 1, 2, 3, 4 veya 8.

### 1.4) `level_info` — Analiz sonucu

```cpp
struct level_info {
    double peak = 0.0;
    double rms = 0.0;
    uint32_t samples = 0;
    uint32_t clipped = 0;
    double peak_db = -144.0;
    double rms_db = -144.0;
};
```

### 1.5) `pcm_buffer` — Decode edilmiş ses

```cpp
struct pcm_buffer {
    knst_vector<float> samples;      // interleaved, [-1, 1]
    uint32_t    rate        = 0;
    uint32_t    channels    = 0;
    uint32_t    source_bits = 0;     // bit depth (0 = lossy)
    container_t container   = container_t::unknown;
    codec_t     codec       = codec_t::unknown;

    uint32_t frames()   const noexcept;
    double   duration() const noexcept;
    bool     valid()    const noexcept;
};
```

### 1.6) `media_info` — Header bilgisi (decode etmeden)

```cpp
struct media_info {
    container_t container = container_t::unknown;
    codec_t     codec     = codec_t::unknown;
    uint32_t    rate      = 0;
    uint32_t    channels  = 0;
    uint32_t    bits      = 0;
    uint64_t    frames    = 0;
    double      duration  = 0.0;
    bool        native    = false;
    bool        valid     = false;
};
```

### 1.7) `decode_options`

```cpp
struct decode_options {
    uint32_t max_samples  = 0x40000000u;   // hard cap (samples, not frames)
    bool     lenient      = true;           // damaged tail → partial
    bool     use_backend  = true;           // registry / libsndfile
    // RAW only:
    uint32_t raw_rate     = 44100;
    uint32_t raw_channels = 2;
    sample_fmt raw_format = sample_fmt::i16;
    bool     raw_big_endian = false;
};
```

### 1.8) `encode_options`

```cpp
struct encode_options {
    container_t container = container_t::unknown;   // unknown → WAV
    sample_fmt  fmt       = sample_fmt::i16;
    bool        dither    = false;                  // TPDF ≤16 bit
    uint64_t    dither_seed = 0x1234ABCD5678EF01ull;
    bool        big_endian  = false;                // RAW only
};
```

### 1.9) `stream::config`

```cpp
struct config {
    uint32_t rate            = 48000;
    uint32_t channels        = 2;
    uint32_t buffer_frames   = 512;
    bool     is_output       = true;
};
```

### 1.10) `play_options`

```cpp
struct play_options {
    uint32_t rate = 48000;
    uint32_t channels = 2;
    uint32_t bits_per_sample = 16;   // sadece 16 destekli
};
```

### 1.11) `hotplug_event` + `device_change`

```cpp
enum class hotplug_event : uint8_t { added, removed, default_changed };

struct device_change {
    hotplug_event  event;
    bool           is_input;
    bool           is_default;
    knst_c16string device_id;
    knst_c16string device_name;
};

using hotplug_callback = void (*)(void* user, const device_change& change);
```

---

## 🔍 2) Format Detection — `codec::detect`

```cpp
container_t codec::detect(const uint8_t* d, size_t n) noexcept;
container_t codec::detect_extension(const knst_c16string& path) noexcept;
bool        codec::is_native(container_t c) noexcept;
```

**Magic bytes ile tanınan:**

| Konteyner | Signature |
|---|---|
| WAV | `RIFF....WAVE` |
| RF64/BW64 | `RF64/BW64....WAVE` |
| W64 | `riff ....` (Wave64) |
| AIFF/AIFC | `FORM....AIFF/AIFC` |
| AU | `.snd` |
| CAF | `caff` |
| FLAC | `fLaC` |
| OGG | `OggS` |
| WavPack | `wvpk` |
| APE | `MAC ` |
| TTA | `TTA1` |
| DSF | `DSD ` |
| DSDIFF | `FRM8` |
| MIDI | `MThd` |
| AMR | `#!AMR` |
| MP4 | `....ftyp` |
| ASF | `30 26 B2 75` |
| Matroska/WebM | `1A 45 DF A3` |
| AC3 | `0B 77` |
| MP3/AAC | ID3 varsa tag sonrası, yoksa sync bytes (`0xFF 0xEx`) |

---

## 🎧 3) Decode — `decode` / `decode_file`

```cpp
result<pcm_buffer> decode(const uint8_t* d, uint32_t n,
                          const decode_options& opt = {},
                          container_t hint = container_t::unknown) noexcept;

result<pcm_buffer> decode_file(const knst_c16string& path,
                               const decode_options& opt = {}) noexcept;

result<media_info> probe(const knst_c16string& path) noexcept;
```

### 3.1) Native decode önceliği

1. **FLAC** → `flacdec::decode`
2. **WAV/RF64/AIFF/AIFC/AU** → `parse_*` + `decode_layout`
3. **RAW** → `decode_raw`
4. **MP3** → `mp3_native::decode` (minimp3)
5. **AAC** → `aac_native::decode_adts` (FAAD2 varsa)
6. **Registry** (`codec::registry::add`)
7. **Sndfile** fallback

### 3.2) FLAC native decoder detay

- **Frame decoding:** `bitreader` + `residual()` + `subframe()` + `frame()`
- **Fixed predictor:** order 0..4
- **LPC predictor:** order 1..32
- **Rice coding:** method 0/1 + escape
- **Channel modes:** independent, left-side, side-right, mid-side
- **CRC-8 (header), CRC-16 (frame)**
- **MD5:** doğrulanmaz (streaming için pahalı)

### 3.3) MP3 native decoder detay

- `minimp3` (CC0 public domain)
- Tüm bitrate, VBR, free format
- ID3v2 tag otomatik atlanır
- MPEG-1/2/2.5 Layer III

### 3.4) AAC decode (opsiyonel)

**Koşul:** `KNST_NETWORK_HAVE_FAAD2` build define'ı + `neaacdec.h`.

**Desteklenen:**
- **`decode_adts()`** — raw ADTS stream (`.aac` dosyaları)
- **`aac_native::frame_decoder`** — per-frame decode (container içinden)

**HE-AAC (SBR/PS):** SBR açıksa `out_rate` otomatik olarak iki katına çıkarılır (FAAD2 `info.sbr` alanı).

**Sınır:** `KNST_NETWORK_HAVE_FAAD2` yoksa → `NotSupported`.

### 3.5) Sndfile backend

```cpp
namespace codec::sndfile {
    api& get() noexcept;                     // dlopen/LoadLibrary ile yüklenir
    result<pcm_buffer> decode(...) noexcept; // SF_VIRTUAL_IO + sf_open_virtual
    result<knst_byte_string> encode(...) noexcept;
}
```

**Platform:**
- **Linux:** `libsndfile.so.1`, `libsndfile.so`
- **Windows:** `sndfile.dll`, `libsndfile-1.dll`

**Desteklenen formatlar:** OGG/OPUS, AAC (MP4/M4A), WavPack, APE, TTA, eski AIFC `ima4`/`MAC3`.

**`SF_VIRTUAL_IO`** — bellek içi decode, FILE* açmadan.

### 3.6) Registry — Custom decoder

```cpp
namespace codec {
    using decoder_fn = result<pcm_buffer> (*)(const uint8_t* data, uint32_t size,
                                              const decode_options& opt, void* user);
    class registry {
    public:
        static bool add(container_t c, decoder_fn fn, void* user = nullptr) noexcept;
        static const registry_entry* find(container_t c) noexcept;
    };
}
```

**Kullanım:**

```cpp
knst_aud::codec::registry::add(knst_aud::container_t::ogg, my_ogg_decoder, nullptr);
```

`decode()` çağrısı `registry::find()` öncelikle kontrol eder.

---

## 🎼 4) Encode — `encode` / `encode_file`

```cpp
result<knst_byte_string> encode(const float* interleaved, uint32_t frames,
                                uint32_t rate, uint32_t channels,
                                const encode_options& o = {}) noexcept;

result<knst_byte_string> encode(const pcm_buffer& b,
                                const encode_options& o = {}) noexcept;

result<void> encode_file(const knst_c16string& path, const pcm_buffer& b,
                         encode_options o = {}) noexcept;
```

### 4.1) Native encoder'lar

| Format | Encoder | Bağımlılık |
|---|---|---|
| **WAV** | `codec::make_wav_header` + `encode_samples` | ✅ Zero |
| **AIFF / AIFC** | Native | ✅ Zero |
| **AU** | Native | ✅ Zero |
| **RAW** | Native | ✅ Zero |
| **FLAC** | `flacenc::encode` (sıfırdan yazılmış) | ✅ Zero |
| **OGG Vorbis** | `vorbis_enc::encode` | `KNST_NETWORK_HAVE_VORBIS` |

### 4.2) FLAC native encoder

**Spec-compliant**, external dependency yok:
- `STREAMINFO` block + `FIXED` predictor frames
- Rice-coded residuals
- CRC-8/CRC-16 doğru
- MD5 = zeros (legal)
- Block size 4096
- 8/16/24 bit support

**`flacenc::encode(interleaved, frames, rate, channels, bits)`** — doğrudan çağır veya `encode_file(.flac)` kullan.

### 4.3) WAV writer (streaming)

```cpp
class wav_writer {
public:
    result<void> open(const knst_c16string& path, uint32_t rate, uint32_t channels,
                      sample_fmt fmt = sample_fmt::i16, bool dither = false) noexcept;
    result<uint32_t> write(const float* interleaved, uint32_t frames) noexcept;
    result<void> close() noexcept;      // header patch
    bool     is_open() const noexcept;
    uint64_t frames_written() const noexcept;
    double   seconds_written() const noexcept;
};
```

**Kullanım:** Kayıt için ideal. `open` WAV header yazar, `write` sample yazar, `close` boyutları patch eder.

**Max dosya boyutu:** 4 GiB (WAV RIFF limit).

### 4.4) Container otomatik tespit

`encode_file` `o.container == unknown` ise dosya uzantısından tespit eder:
- `.wav` → WAV
- `.flac` → FLAC (native!)
- `.ogg` → OGG (Vorbis, varsa)
- `.aif`/`.aiff` → AIFF
- `.au` → AU
- `.raw`/`.pcm` → RAW
- Bilinmeyen → WAV fallback

---

## 🎛️ 5) DSP Modülü — `dsp::`

### 5.1) Analiz

```cpp
dsp::stats analyze(const float* in, uint32_t n) noexcept;
level_info measure(const float* in, uint32_t n) noexcept;
float dc_offset(const float* buf, uint32_t n) noexcept;
void  dc_remove(float* buf, uint32_t n) noexcept;
void  dc_remove_interleaved(float* buf, uint32_t frames, uint32_t ch) noexcept;
```

**`stats`:**
- `peak`, `rms`, `peak_db`, `rms_db`
- `clipped` (≥1.0 sample sayısı)
- `samples`, `valid`

### 5.2) Gain ve fade

```cpp
void apply_gain(float* inout, uint32_t n, float gain) noexcept;
void apply_gain_db(float* inout, uint32_t n, float db) noexcept;
void fade_in(float* inout, uint32_t frames, uint32_t ch, uint32_t fade_frames) noexcept;
void fade_out(float* inout, uint32_t frames, uint32_t ch, uint32_t fade_frames) noexcept;
```

### 5.3) Mix / concat / crossfade

```cpp
void mix_into(float* dst, const float* src, uint32_t n, float gain = 1.0f) noexcept;
knst_vector<float> mix(const float* a, const float* b,
                       uint32_t frames, uint32_t ch) noexcept;
knst_vector<float> concat(const float* a, uint32_t fa,
                          const float* b, uint32_t fb, uint32_t ch) noexcept;
knst_vector<float> crossfade(const float* a, uint32_t fa,
                             const float* b, uint32_t fb,
                             uint32_t ch, uint32_t xf) noexcept;
```

**`crossfade`** — Equal-power (`cos/sin` curve) crossfade. Loop yaparken tık sesi çıkarmaz.

### 5.4) Trim / reverse / pan

```cpp
knst_vector<float> trim(const float* in, uint32_t frames, uint32_t ch,
                        uint32_t start_frame, uint32_t end_frame) noexcept;
knst_vector<float> trim_silence(const float* in, uint32_t frames, uint32_t ch,
                                float threshold_db = -60.0f) noexcept;
void reverse(float* inout, uint32_t frames, uint32_t ch) noexcept;
void pan_stereo(float* inout, uint32_t frames, float pan) noexcept;   // -1..+1
```

### 5.5) Normalize

```cpp
void normalize(float* inout, uint32_t n, float target_peak = 0.95f) noexcept;
void normalize_rms(float* inout, uint32_t n, float target_rms_db = -18.0f,
                   bool protect_peak = true) noexcept;
```

**`normalize`** — Peak'i `target_peak`'e çeker.
**`normalize_rms`** — RMS'i `target_rms_db`'ye çeker, peak protection ile clip engellenir.

### 5.6) Limiter ve soft clip

```cpp
void limiter(float* inout, uint32_t frames, uint32_t ch, uint32_t rate,
             float threshold_db = -1.0f, float release_ms = 80.0f) noexcept;

void soft_clip(float* inout, uint32_t n) noexcept;
```

**`limiter`** — Peak limiter, instant attack, exponential release, linked channels.

### 5.7) Channel conversion

```cpp
knst_vector<float> convert_channels(const float* in, uint32_t frames,
                                    uint32_t from_ch, uint32_t to_ch) noexcept;
```

**Davranış:**
- Aynı sayı → memcpy
- **N → 1:** downmix
- **N > 2 → 2:** normalize edilmiş surround downmix (WAV/SMPTE sırası: FL FR FC LFE BL BR SL SR)
- **1 → N:** mono duplicate
- **2 → N:** L/R → ilk 2 kanal, kalan 0

### 5.8) Resampling

```cpp
enum class resample_quality : uint8_t { fast, medium, high, best };

knst_vector<float> resample_linear(const float* in, uint32_t frames_in,
                                    uint32_t in_rate, uint32_t ch,
                                    uint32_t out_rate) noexcept;

knst_vector<float> resample(const float* in, uint32_t frames_in, uint32_t ch,
                            uint32_t in_rate, uint32_t out_rate,
                            resample_quality q = resample_quality::high) noexcept;
```

**Kalite seviyeleri:**

| Seviye | Method | Zero-crossings | Beta |
|---|---|---|---|
| **fast** | Linear interpolate | — | — |
| **medium** | Windowed-sinc | 8 | 6.0 |
| **high** | Windowed-sinc | 16 | 8.6 |
| **best** | Windowed-sinc | 32 | 10.5 |

**Anti-alias:** Down-sampling sırasında cutoff otomatik düşürülür (`fc = out/in`).

### 5.9) Biquad filters (RBJ cookbook)

```cpp
enum class filter_t : uint8_t {
    lowpass, highpass, bandpass, notch, allpass, peak, lowshelf, highshelf
};

struct biquad_coeffs { double b0, b1, b2, a1, a2; };

biquad_coeffs make_biquad(filter_t t, double rate, double freq,
                          double q = 0.7071, double gain_db = 0.0) noexcept;

class biquad {
public:
    biquad() noexcept;
    biquad(filter_t, double rate, double freq, double q = 0.7071, double gain_db = 0.0) noexcept;
    void  set(const biquad_coeffs&) noexcept;
    void  reset() noexcept;
    float process_sample(float x, uint32_t ch) noexcept;
    bool  process(float* inout, uint32_t frames, uint32_t ch) noexcept;
};
```

**Multi-channel:** max 16 kanal, double-precision state, transposed direct form II.

### 5.10) FFT / spectrum

```cpp
bool fft(float* re, float* im, uint32_t n) noexcept;   // radix-2, in-place

knst_vector<float> magnitude_spectrum(const float* in, uint32_t n) noexcept;

double dominant_frequency(const float* in, uint32_t n, uint32_t rate) noexcept;
```

**`magnitude_spectrum`** — Hann window, n/2+1 bins. Full-scale bin-centered sine ≈ 1.0.

**`dominant_frequency`** — Parabolic peak interpolation.

### 5.11) Generators

```cpp
enum class wave_t : uint8_t { sine, square, saw, triangle, noise };

void generate(float* out, uint32_t frames, uint32_t ch, wave_t type,
              float freq_hz, uint32_t rate, float amplitude = 1.0f) noexcept;
```

---

## 🌊 6) `stream` — Gerçek Zamanlı Blocking I/O

```cpp
class stream {
public:
    static result<stream> open(const knst_audio_device& device,
                               const config& c) noexcept;
    static result<stream> open_default(const config& c) noexcept;

    result<uint32_t> write_interleaved(const float* buffer, uint32_t frames) noexcept;
    result<uint32_t> write_interleaved(const int16_t* buffer, uint32_t frames) noexcept;
    result<uint32_t> read_interleaved(float* buffer, uint32_t frames) noexcept;
    result<uint32_t> read_interleaved(int16_t* buffer, uint32_t frames) noexcept;

    void   drain() noexcept;
    double latency_ms() const noexcept;
    void   close() noexcept;

    uint32_t rate()          const noexcept;
    uint32_t channels()      const noexcept;
    uint32_t buffer_frames() const noexcept;
    uint32_t xruns()         const noexcept;
    bool     is_output()     const noexcept;
    bool     is_open()       const noexcept;
};
```

### 6.1) Backend'ler

**Linux:** `pa_simple` (`dlopen` ile yüklenir, link bağımlılığı yok). Sample format `FLOAT32LE`.

**Windows:** WASAPI shared-mode render/capture. `AUTOCONVERTPCM | SRC_DEFAULT_QUALITY` flag'leri ile engine rate/channel conversion yapar.

**Android:** AAudio (dynamic `dlopen`). `FLOAT32` native.

### 6.2) `drain` semantiği

- **Linux:** `pa_simple_drain`
- **Windows:** padding polling
- **Android:** AAudio explicit drain yok — natural drain

### 6.3) `xruns`

Kuyruk dolduğunda veya yazma/okuma başarısız olduğunda artar.

---

## 🎵 7) `player` — Seek / Loop / Gain

```cpp
class player {
public:
    static result<player> from_buffer(pcm_buffer&& b) noexcept;
    static result<player> open(const knst_c16string& path,
                                const decode_options& o = {}) noexcept;
    static result<player> open(const uint8_t* data, uint32_t size,
                               const decode_options& o = {}) noexcept;

    uint32_t channels() const noexcept;
    uint32_t sample_rate() const noexcept;
    uint32_t total_frames() const noexcept;
    double   duration_seconds() const noexcept;
    const pcm_buffer& buffer() const noexcept;

    uint32_t position_frames() const noexcept;
    double   position_seconds() const noexcept;
    bool     eof() const noexcept;

    void seek_frames(uint32_t f) noexcept;
    void seek_seconds(double s) noexcept;
    void rewind() noexcept;

    void  set_loop(bool) noexcept;
    bool  loop() const noexcept;
    void  set_gain(float) noexcept;
    float gain() const noexcept;

    result<uint32_t> read_interleaved(float* buffer, uint32_t frames) noexcept;
    result<uint32_t> read_interleaved(int16_t* buffer, uint32_t frames) noexcept;
    result<uint32_t> play_frames(stream& sink, uint32_t frames) noexcept;
    result<player>   resampled(uint32_t rate, dsp::resample_quality = high) const noexcept;
};
```

**Loop davranışı:** Loop açıkken `read_interleaved` sonuna gelince otomatik başa döner. `read` döngüde `frames` dolduğunda çıkar.

**Gain:** Doğrudan uygulanır (clip yok). `write_interleaved` öncesi değeri akılda tut.

---

## 🎚️ 8) `mixer` — N Kaynak → 1 Bus

```cpp
class mixer {
public:
    static constexpr int32_t kMaxSources = 32;
    static constexpr uint32_t kChunk = 1024;

    mixer(uint32_t rate, uint32_t channels) noexcept;

    int32_t add(const pcm_buffer& clip, float gain = 1.0f,
                float pan = 0.0f, bool loop = false) noexcept;
    void remove(int32_t id) noexcept;
    void set_gain(int32_t id, float g) noexcept;
    void set_pan(int32_t id, float p) noexcept;
    void pause(int32_t id, bool paused) noexcept;
    bool is_playing(int32_t id) const noexcept;

    void set_master(float g) noexcept;
    void clear() noexcept;

    uint32_t active_count() const noexcept;
    uint32_t rate() const noexcept;
    uint32_t channels() const noexcept;

    result<uint32_t> render(float* out, uint32_t frames) noexcept;
};
```

**`add`:** Clip otomatik olarak mixer'in rate/channel'ına çevrilir.
**`render`:** Allocation-free (iç buffer `kChunk`'a kadar hazır).
**One-shot clips:** Render sırasında bitince otomatik silinir.
**Pan:** Sadece stereo mix için etkili, unity at centre (`×√2`).

---

## 🌊 9) `decoder_stream` — Memory-Bounded Streaming

```cpp
class decoder_stream {
public:
    static result<decoder_stream> open(const knst_c16string& path) noexcept;

    result<uint32_t> read(float* out, uint32_t frames) noexcept;
    result<uint32_t> read(int16_t* out, uint32_t frames) noexcept;

    result<uint64_t> seek_frames(uint64_t target) noexcept;
    result<uint64_t> play_to(stream& sink,
                             const std::atomic<bool>* stop = nullptr) noexcept;

    uint32_t    rate() const noexcept;
    uint32_t    channels() const noexcept;
    container_t container() const noexcept;
    uint64_t    total_frames() const noexcept;
    double      duration() const noexcept;
    uint64_t    position() const noexcept;
    bool        eof() const noexcept;

    void close() noexcept;
};
```

### 9.1) RAM profili

**~200 KB sabit** — dosya boyutundan bağımsız.

- **MP3:** input buffer 32 KB + 1 frame PCM
- **WAV/AIFF/AU PCM:** okuma chunk'ı (read çağrısına göre)
- **WAV ADPCM:** tek blok (genelde 1 KB)
- **FLAC:** bitreader buffer (64 KB) + 1 frame sample
- **Sndfile:** 4 KB wrapper + sink chunk

### 9.2) MP3 seek — lazy frame index

**İlk `seek_frames()` çağrısında** tüm dosya taranır ve frame index'i (`byte_offset` → `sample_offset`) oluşturulur.

- **Max entries:** 2.000.000 (32 MB)
- **Sonrası:** Her seek O(log N) binary search + kısa forward decode

### 9.3) MP3 duration — Xing/Info/VBRI

`open_mp3` sırasında ilk frame'deki **Xing/Info** veya **VBRI** header'ı okunur. Yoksa `total_frames = 0` (`duration()` → 0).

### 9.4) `play_to` — otomatik oynat

```cpp
auto ds = knst_aud::decoder_stream::open(u"3hour.mp3");
auto s = knst_aud::stream::open_default({ds.value.rate(), ds.value.channels()});

// 3 saat oynat — RAM sabit
ds.value.play_to(s.value);
```

**Cancel:** `stop` atomic bool pointer'ı verilir. Chunk'lar arasında kontrol edilir.

### 9.5) Adım adım kullanım

```cpp
auto ds = knst_aud::decoder_stream::open(u"song.flac");
if (!ds) return;

// Seek to 60 seconds
auto r = ds.value.seek_frames(ds.value.rate() * 60);
if (!r) { /* FLAC'ta seek NotSupported */ }

// Chunk chunk oku
float buf[4096 * 2];
for (;;) {
    auto n = ds.value.read(buf, 4096);
    if (!n || n.value == 0) break;
    // ... buf'u işle
}
```

### 9.6) Platform desteği

| Format | Native decode | Sndfile fallback | Seek |
|---|---|---|---|
| WAV (PCM) | ✅ | — | ✅ |
| WAV (ADPCM) | ✅ | — | ⚠️ blok sınırı |
| RF64/BW64 | ✅ | — | ✅ |
| AIFF (PCM) | ✅ | — | ✅ |
| AIFC (ima4/MAC3) | ❌ | ⚠️ | ❌ |
| AU | ✅ | — | ✅ |
| FLAC | ✅ | — | ❌ `NotSupported` |
| MP3 | ✅ | — | ✅ (lazy index) |
| OGG/OPUS | ❌ | ✅ | ❌ |
| AAC (ADTS) | ⚠️ FAAD2 | ✅ | ❌ |
| WavPack/APE/TTA | ❌ | ✅ | ❌ |

---

## 📡 10) `hotplug_monitor` — Cihaz Değişim İzleyicisi

```cpp
class hotplug_monitor {
public:
    static result<hotplug_monitor> start(hotplug_callback cb, void* user,
                                          uint32_t interval_ms = 1000) noexcept;
    ~hotplug_monitor();
    hotplug_monitor(hotplug_monitor&& o) noexcept;
    void stop() noexcept;
    bool is_running() const noexcept;
};

using hotplug_callback = void (*)(void* user, const device_change& change);
```

### 10.1) Çalışma prensibi

**Polling-based** — arka plan thread her `interval_ms` ms'de bir `list_sinks()` + `list_sources()` çağırır ve diff'leri (added/removed/default_changed) event olarak fırlatır.

**Interval range:** 200..60000 ms. Default 1000.

**Gecikme:** ~500 ms (kaba) — interval_ms'e göre.

### 10.2) Callback kuralları

**Kritik:**
- **Arka plan thread'inde çalışır** → user thread-safe olmalı
- **Hızlı olmalı** — audio API'sini callback içinden çağırma
- **Callback `nullptr`** → `InvalidArgument`

### 10.3) Kullanım

```cpp
auto m = knst_aud::hotplug_monitor::start(
    [](void* user, const knst_aud::device_change& dc) {
        const char* ev = dc.event == knst_aud::hotplug_event::added ? "added"
                       : dc.event == knst_aud::hotplug_event::removed ? "removed"
                       : "default";
        std::cout << "[" << ev << "] "
                  << (dc.is_input ? "input: " : "output: ")
                  << dc.device_name << "\n";
    },
    nullptr,
    500);

// ... çalış ...

m.value.stop();
```

---

## 🎧 11) `wav` — Legacy WAV API

Eski kod uyumluluğu için:

```cpp
namespace knst_aud::wav {
    struct format { uint32_t rate; uint32_t channels; sample_fmt sample; };
    struct info   { format fmt; uint32_t data_bytes; uint32_t frame_count;
                    uint16_t format_tag; bool valid; };

    result<info> probe(const knst_c16string& path) noexcept;
    result<info> probe_memory(const uint8_t* data, uint32_t size) noexcept;

    result<knst_vector<float>> decode_to_float(const knst_c16string& path) noexcept;
    result<knst_vector<float>> decode_to_float(const uint8_t* data, uint32_t size) noexcept;

    result<knst_byte_string> encode_memory(const float* interleaved,
                                            uint32_t frames, const format& f) noexcept;
    result<void> encode(const knst_c16string& path, const float* interleaved,
                        uint32_t frames, const format& f) noexcept;
}
```

**Not:** `decode_to_float` herhangi bir desteklenen format kabul eder (WAV değil).

---

## 📊 12) Platform-Spesifik

### 12.1) Linux

**Sistem:** PulseAudio veya PipeWire-pulse.

**Kullanılan araçlar:**
- `pactl` — device/volume/stream/session
- `paplay` — `play_file` (fallback)
- `parec` / `pw-cat` / `pw-record` — peak meter (3 backend fallback)
- `pa_simple` (dlopen) — `stream` için
- `rfkill` — (bluetooth için, bu header'da yok)

**Device ID:** `alsa_output.pci-0000_00_1f.3.analog-stereo` formatında PulseAudio sink adı.

**Peak meter fallback sırası:**
1. **`parec`** (PulseAudio / PipeWire-pulse)
2. **`pw-cat --record`** (native PipeWire)
3. **`pw-record`** (eski binary adı)

**Capture window:** 200 ms (50 ms çok kısaydı — stream negotiate etmeden timeout oluyordu).

**Sink monitor:** Sink adına `.monitor` suffix eklenir.

### 12.2) Windows

**WASAPI:** `IMMDeviceEnumerator`, `IAudioClient`, `IAudioEndpointVolume`, `ISimpleAudioVolume`.

**Per-app streams:** `IAudioSessionManager2` → `IAudioSessionEnumerator` → `IAudioSessionControl2` + `ISimpleAudioVolume`.

**Session mapping:**
- `index` → session position (Linux sink-input index gibi unstable)
- `application` → session display name veya process image name
- `media_name` → `[PID N]` hint

**Rendering rate:** AUTOCONVERTPCM + SRC_DEFAULT_QUALITY → engine resampling.

**Format doğrulaması:** Mix format float32 olmalı. Aksi halde → `NotSupported`.

**Set default:** `IPolicyConfig::SetDefaultEndpoint` — eConsole + eMultimedia + eCommunications için çağrılır.

**Peak meter:**
1. **`IAudioMeterInformation`** (Activate → vtable `GetPeakValue`) — hızlı
2. **Loopback capture** — 200 ms WASAPI capture

**Sınırlar:**
- `list_source_outputs` → `NotSupported` (capture sessions)
- `move_stream`, `cork_stream`, `suspend` → `NotSupported`
- `get_ports`, `list_cards`, `set_profile`, `server` → `NotSupported`
- `get_balance`, `set_balance` → `NotSupported`

### 12.3) Android

**AAudio:** Dynamic `dlopen` (`libaaudio.so`). Sample format `FLOAT32`.

**AudioManager (JNI):** Stream volume/mute için. `STREAM_MUSIC` = 3.

**AudioFocus:** `com.knst.print.KnstAudioFocus` Java helper (app tarafında yazılmalı). `request_audio_focus()` / `abandon_audio_focus()`.

**`get_channel_count`:** `AudioDeviceInfo.getChannelCounts()` → **max element** (array length değil).

**`get_format`:** `AudioManager.getProperty("android.media.property.OUTPUT_SAMPLE_RATE")` + channel count.

**Sınırlar:**
- `list_sink_inputs` → `NotSupported`
- `move_stream`, `cork_stream`, `suspend` → `NotSupported`
- `get_ports`, `list_cards`, `set_profile`, `server` → `NotSupported`
- `get_balance`, `set_balance` → `NotSupported`
- `get_peak_meter` → `NotSupported`
- `set_default` → `NotSupported`

---

## ⚠️ 13) Sık Tuzaklar

### 1) `max_samples = 2^30` — decode RAM limiti

`decode_file` tüm dosyayı belleğe alır. Limit: `decode_options::max_samples = 0x40000000u` (~4 GB float).

**Fix:** Uzun dosyalar için `decoder_stream` kullan (`read` chunk chunk).

### 2) `decoder_stream` RAM ~200 KB

Dosya boyutundan bağımsız. 3 saatlik MP3 bile RAM'i şişirmez.

### 3) MP3 seek lazy index

**İlk seek** tüm dosyayı tarar. 2M frame sınırı var. Sonrası hızlı.

### 4) MP3 duration Xing/VBRI olmadan 0

`total_frames = 0` → `duration() = 0`. Player'da progress bar göstermek için frame counting yap.

### 5) FLAC seek `NotSupported`

`decoder_stream::seek_frames()` FLAC için `NotSupported` döner (seek table yok).

### 6) WAV ADPCM seek block boundary

`seek_frames` en yakın **block başına** atlar. Frame-accurate değil.

### 7) FLAC encode sadece 8/16/24 bit

`encode(.flac, fmt = f32)` → `NotSupported`. FLAC integer-only.

### 8) FLAC native encoder hiçbir kütüphane gerektirmez

Sıfırdan yazılmış. Sadece `encode_file(.flac)` çağır veya `flacenc::encode()`.

### 9) OGG encode libvorbis olmadan çalışmaz

`KNST_NETWORK_HAVE_VORBIS` build-time define'ı + libvorbis/libvorbisenc/libogg gerekli. Yoksa `NotSupported`.

### 10) AAC decode FAAD2 olmadan çalışmaz

`KNST_NETWORK_HAVE_FAAD2` + `neaacdec.h` gerekli. Yoksa ADTS stream copy (`knst_converter` içinde) kullan.

### 11) HE-AAC (SBR) output rate 2x

FAAD2 `info.sbr` → `out_rate = rate * 2`. Yoksa playback yavaş olur.

### 12) MP3 encode YOK

AAC → MP3 dönüşümü için `codec::registry::add()` ile custom decoder kaydet veya `knst_converter::register_audio_encoder()`.

### 13) Sndfile version kontrolü

`decode` sadece `sf_open_virtual` + `sf_readf_float` + `sf_close` kullanır. Bu fonksiyonlar varsa `ok = true`.

**Encode** `sf_writef_float` da gerekir. Eski libsndfile'da olmayabilir → encode `NotSupported`.

### 14) Registry priority

`codec::registry::find()` **native decoder'lardan önce** kontrol edilir. Yani custom decoder native MP3 decoder'ı override edebilir.

### 15) Linux peak meter 3 backend fallback

`parec` (PulseAudio/PipeWire-pulse) → `pw-cat` (native PipeWire) → `pw-record` (eski).

**Capture 200 ms** — 50 ms çok kısaydı.

### 16) Linux sink monitor `.monitor`

Sink adı `alsa_output.pci-0000_00_1f.3.analog-stereo` → capture target `alsa_output.pci-0000_00_1f.3.analog-stereo.monitor`.

### 17) Windows per-app capture `NotSupported`

`list_source_outputs` → capture endpoint sessions. Windows'ta `IAudioSessionManager2` capture için sınırlı.

### 18) Windows `IAudioMeterInformation` vtable

Raw `IUnknown` cast + `vtable[3]` = `GetPeakValue`. Fallback loopback capture.

### 19) Windows AUTOCONVERTPCM

WASAPI engine'in rate/channel conversion yapması için `0x80000000 | 0x08000000` flag'leri. Aksi halde mix format zorunlu.

### 20) Android AudioFocus helper gerekli

`request_audio_focus()` / `abandon_audio_focus()` — `com.knst.print.KnstAudioFocus` Java class'ı olmadan `NotSupported`.

### 21) Android `get_channel_count` max element

`AudioDeviceInfo.getChannelCounts()` **array döner** (ör. `{1, 2, 6, 8}`). `array.length` değil, `max(arr)` kullanılır.

### 22) `hotplug_monitor` polling

**500 ms latency** — anlık değil. Kritik uygulamalarda interval'i 200 ms'ye düşür.

### 23) `hotplug_monitor` thread-safe callback

**Arka plan thread'inde çalışır**. User thread-safe olmalı. Callback içinde audio API'sini çağırma.

### 24) `mixer::render` allocation-free

İç `tmp_` buffer 1024 frame'e kadar hazır. `frames > 1024` verilirse chunk'lar halinde işlenir.

### 25) `player::set_gain` clip yapmaz

`read_interleaved` gain uygular ama clip yapmaz. Streaming output'ta limiter gerekebilir.

### 26) `stream::read_interleaved` blocking

**Blocking** — yeterli frame gelene kadar bekler. Non-blocking API yok.

### 27) `stream::latency_ms`

- **Linux:** `pa_simple_get_latency` (varsa), yoksa buffer hesaplama
- **Windows:** `wbuf_ / rate_`
- **Android:** `buf_frames_ / rate_` (fallback)

### 28) `dsp::resample` çok büyük tablo

`half * RES > 8M` → linear fallback. Çok düşük out_rate/in_rate oranı için linear.

### 29) `dsp::convert_channels` WAV/SMPTE sırası

`3`: FL FR FC · `4`: FL FR BL BR · `5`: FL FR FC BL BR · `6`: FL FR FC LFE BL BR · `7`: FL FR FC LFE BC SL SR · `8`: FL FR FC LFE BL BR SL SR

### 30) `dsp::pan_stereo` sadece stereo

Mono buffer için etkisi yok. `mixer::render` içinde pan uygulanır.

### 31) `wav_writer` 4 GiB limit

RIFF boyut alanı 32-bit → 4 GiB. Aşarsa `IoError`.

### 32) `wav::encode_memory` sadece WAV

Diğer formatlar için `encode_file` kullan.

### 33) `parse_wav` streaming data size clamp

`decoder_stream::open_wav` 8 KB probe buffer kullanır. `data_size` probe'a clamp'lanır → `fixup_stream_data_size` gerçek boyutu dosyadan okur.

### 34) RF64 `data_size = 0xFFFFFFFF`

RF64 `ds64` chunk'ında gerçek boyut. Kod `ds64` varsa kullanır, yoksa dosya boyutuna fallback.

### 35) `codec::layout_frames` ADPCM

IMA: `(block_align - 4*ch) * 8 / (4*ch) + 1` sample/block.
MS: `(block_align - 7*ch) * 2 / ch + 2` sample/block.

### 36) `stream::open` cihaz eşleşmesi

Linux'ta `pa_simple_new` sink adı `device` olarak geçer. Windows'ta `GetDevice(id)` — id = endpoint GUID.

### 37) Android AudioManager stream

Sadece `STREAM_MUSIC` (3) desteklenir. Diğer stream'ler (`STREAM_RING`, `STREAM_ALARM`) için ayrı helper yaz.

### 38) `sndfile::encode` FLAC bit depth

`sf_format` örneğin `0x170000 | 0x0002` = FLAC 16-bit. `0x0004` (PCM_32) veya `0x0006` (FLOAT) FLAC için desteklenmez → `NotSupported`.

### 39) `decoder_stream` `close()` idempotent

Birden fazla çağrı → no-op.

### 40) `hotplug_monitor` move constructor

Thread + stop flag taşınır. Eski monitor `stop()` çağrılırsa **yeni** monitor durmaz (shared_ptr copy).

### 41) `play_file` Linux fallback

`paplay` üzerinden çalışır. `paplay` yoksa → `ToolMissing`. Native `play_file_any` kullan.

### 42) `play_file_any` native decoder kullanır

WAV/FLAC/MP3/AIFF/AU → native. OGG/OPUS/AAC → sndfile. Native yoksa `NotSupported`.

### 43) `decode_options::lenient`

Damaged tail → `decode` başarılı döner (partial data). Aksi halde fail.

### 44) FLAC `MD5` doğrulanmaz

Streaming decode sırasında MD5 check pahalı. `STREAMINFO` MD5 = zeros genelde.

### 45) `flacenc::encode` block size

Default 4096 — spec uyumlu, common block size.

### 46) `flacenc::encode` VERBATIM fallback

Compression ratio 1.0'dan büyükse VERBATIM subframe kullanılır (uncompressed, ama geçerli FLAC).

### 47) `flacenc::encode` FIXED predictor

LPC predictor yok — sadece FIXED 0..4. Sıkıştırma oranı biraz daha düşük ama **çok hızlı**.

### 48) `channel_mask_for` genişlik

8 kanal üstü → 0 (`NotSupported` yerine). WAV spec 18 kanala kadar extensible destekler.

### 49) `make_wav_header` `fmt_size` extensible

`ch > 2` → `WAVE_FORMAT_EXTENSIBLE` (0xFFFE), `SubFormat` tag doğru GUID.

### 50) `sndfile::decode` `SFM_READ = 0x10`

`sf_open_virtual` second arg. `SFM_WRITE = 0x20` encode için.

---

## 📊 14) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| **Device list** | 50-200 ms | `pactl` spawn |
| **Volume get/set** | 50-100 ms | `pactl` spawn |
| **Peak meter** | 200-500 ms | Capture window |
| **Native decode WAV** | ~100-500 MB/s | Hızlı |
| **Native decode FLAC** | ~20-50 MB/s | CPU-bound |
| **Native decode MP3** | ~50-100× realtime | minimp3 |
| **Native encode WAV** | ~100-500 MB/s | Dönüşüm hızı |
| **Native encode FLAC** | ~5-15× realtime | FIXED predictor |
| **Sndfile decode** | ~10-30× realtime | Kütüphane overhead |
| **DSP gain** | O(n) | ~1 GB/s |
| **DSP resample (high)** | ~1-5× realtime | Sinc convolution |
| **DSP FFT** | O(n log n) | Radix-2 |
| **Stream write** | ~100-200 μs/chunk | Sink pipeline |
| **`decoder_stream::seek`** | İlk: O(n) / Sonra: O(log N) | MP3 lazy index |
| **`player::seek`** | O(1) | In-memory |
| **`mixer::render`** | O(N × frames) | Allocation-free |

**Tipik latencies:**
- **10 MB WAV decode:** 20-50 ms
- **10 MB FLAC decode:** 200-500 ms
- **3 dk MP3 → WAV:** 2-4 saniye (decode + encode)
- **10 saniye audio player::open:** 500 ms - 1 saniye
- **`decoder_stream::open`:** 10-50 ms
- **MP3 seek (post-index):** 5-20 ms

**Optimizasyon:**
- **Streaming için `decoder_stream`** — RAM sabit
- **`wav_writer` for recording** — chunk chunk yaz, RAM küçük
- **`stream::write_interleaved` batch** — frame'i toplu yaz
- **`mixer` yeniden kullan** — her render'da yeni mixer oluşturma
- **`player::from_buffer`** — decode sonrası buffer'ı bir kez sakla

---

## ✅ 15) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Uzun dosya için decoder_stream
auto ds = knst_aud::decoder_stream::open(u"long.mp3");
ds.value.play_to(stream);

// 2. FLAC için native encoder
knst_aud::encode_options o;
o.container = knst_aud::container_t::flac;
o.fmt = knst_aud::sample_fmt::i16;
knst_aud::encode_file(u"out.flac", buf, o);

// 3. Kayıt için wav_writer
knst_aud::wav_writer wr;
wr.open(u"rec.wav", 48000, 2, knst_aud::sample_fmt::i16);
wr.write(samples, frames);
wr.close();

// 4. Hotplug için thread-safe callback
auto m = knst_aud::hotplug_monitor::start(
    [](void* user, const knst_aud::device_change& dc) {
        // Sadece atomic flag set et, audio API çağırma
        ((std::atomic<int>*)user)->fetch_add(1);
    },
    &counter);

// 5. Resampling kalitesi
auto r = knst_aud::dsp::resample(in, frames, ch, 44100, 48000,
                                  knst_aud::dsp::resample_quality::high);

// 6. FLAC seek için sınırlamayı bil
auto sr = knst_aud::decoder_stream::seek_frames(1000);   // FLAC: NotSupported
```

### ❌ Yapma

```cpp
// 1. decode_file ile çok uzun dosya
auto r = knst_aud::decode_file(u"3hour.mp3");   // ❌ max_samples aşılır

// 2. FLAC encode with float
knst_aud::encode_options o;
o.container = knst_aud::container_t::flac;
o.fmt = knst_aud::sample_fmt::f32;   // ❌ NotSupported

// 3. MP3 encode bekle
knst_aud::encode_file(u"out.mp3", buf, {});   // ❌ NotSupported

// 4. OGG encode libvorbis olmadan
knst_aud::encode_file(u"out.ogg", buf, {});   // ❌ NotSupported

// 5. AAC decode FAAD2 olmadan
auto r = knst_aud::decode_file(u"test.aac");   // ❌ NotSupported (FAAD2 yoksa)

// 6. Android'de peak meter bekle
knst_aud::audio::get_peak_meter(device);   // ❌ NotSupported

// 7. Windows'ta capture streams
knst_aud::audio::list_source_outputs();   // ❌ NotSupported

// 8. Windows'ta balance
knst_aud::audio::set_balance(device, 30);   // ❌ NotSupported

// 9. Hotplug callback içinde audio API
[](void* user, const knst_aud::device_change& dc) {
    knst_aud::audio::list_sinks();   // ❌ deadlock riski
}

// 10. decoder_stream FLAC seek
knst_aud::decoder_stream::seek_frames(1000);   // ❌ NotSupported (FLAC)
```

---

## 🔥 16) Gerçek Kullanım Örnekleri

### Örnek 1: Müzik çalar

```cpp
class MusicPlayer {
    knst_aud::decoder_stream ds_;
    knst_aud::stream sink_;
    bool playing_ = false;
    
public:
    bool open(const knst_c16string& path) {
        auto ds = knst_aud::decoder_stream::open(path);
        if (!ds) return false;
        ds_ = std::move(ds.value);
        
        knst_aud::stream::config c;
        c.rate = ds_.rate();
        c.channels = ds_.channels();
        c.is_output = true;
        auto s = knst_aud::stream::open_default(c);
        if (!s) return false;
        sink_ = std::move(s.value);
        return true;
    }
    
    void play() { playing_ = true; ds_.play_to(sink_); }
    void stop() { playing_ = false; }
};
```

### Örnek 2: Mikser (müzik + jingle)

```cpp
knst_aud::mixer mix(48000, 2);

auto music = knst_aud::decode_file(u"music.wav").value;
auto jingle = knst_aud::decode_file(u"jingle.wav").value;

int32_t m = mix.add(music, 0.8f, 0.0f, true);    // loop
int32_t j = mix.add(jingle, 1.0f, 0.0f, false);

knst_aud::stream::config c;
c.rate = 48000; c.channels = 2;
auto sink = knst_aud::stream::open_default(c);

float buf[1024 * 2];
for (;;) {
    auto r = mix.render(buf, 1024);
    if (!r) break;
    sink.value.write_interleaved(buf, 1024);
}
```

### Örnek 3: Kayıt + DSP

```cpp
knst_aud::stream::config c;
c.rate = 48000; c.channels = 1; c.is_output = false;
auto mic = knst_aud::stream::open_default(c);

knst_aud::wav_writer wr;
wr.open(u"rec.wav", 48000, 1, knst_aud::sample_fmt::i16);

float buf[1024];
auto start = std::chrono::steady_clock::now();
while (std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
    auto r = mic.value.read_interleaved(buf, 1024);
    if (!r) break;
    knst_aud::dsp::dc_remove_interleaved(buf, r.value, 1);
    knst_aud::dsp::apply_gain_db(buf, r.value * 1, 3.0f);   // +3 dB
    knst_aud::dsp::soft_clip(buf, r.value * 1);
    wr.write(buf, r.value);
}
wr.close();
```

### Örnek 4: Spectrum analyzer

```cpp
knst_aud::decoder_stream ds = knst_aud::decoder_stream::open(u"song.mp3").value;
float buf[1024 * 2];

for (int i = 0; i < 20; ++i) {
    auto n = ds.read(buf, 1024);
    if (!n || n.value == 0) break;
    
    // FFT for spectrum (mono)
    float mono[1024];
    for (int j = 0; j < 1024; ++j) {
        mono[j] = (buf[j * 2] + buf[j * 2 + 1]) * 0.5f;
    }
    
    auto mag = knst_aud::dsp::magnitude_spectrum(mono, 1024);
    double freq = knst_aud::dsp::dominant_frequency(mono, 1024, ds.rate());
    
    std::cout << "Dominant: " << freq << " Hz\n";
}
```

### Örnek 5: 5.1 → stereo downmix

```cpp
auto buf = knst_aud::decode_file(u"movie_5_1.wav").value;
auto stereo = knst_aud::dsp::convert_channels(
    buf.samples.data(), buf.frames(), 6, 2);

buf.samples = std::move(stereo);
buf.channels = 2;

knst_aud::encode_options o;
o.container = knst_aud::container_t::wav;
knst_aud::encode_file(u"stereo.wav", buf, o);
```

### Örnek 6: Cihaz değişimi izleme

```cpp
std::atomic<bool> running{true};

auto m = knst_aud::hotplug_monitor::start(
    [](void* user, const knst_aud::device_change& dc) {
        const char* ev = dc.event == knst_aud::hotplug_event::added   ? "+"
                       : dc.event == knst_aud::hotplug_event::removed ? "-"
                       : "*";
        std::cout << ev << " " 
                  << (dc.is_input ? "in: " : "out: ")
                  << dc.device_name << "\n";
    },
    nullptr,
    500);

std::this_thread::sleep_for(std::chrono::seconds(60));
m.value.stop();
```

### Örnek 7: Custom decoder kaydı

```cpp
static knst_dev::result<knst_aud::pcm_buffer>
my_decoder(const uint8_t* data, uint32_t size,
           const knst_aud::decode_options& opt, void* user) {
    knst_aud::pcm_buffer out;
    // ... decode logic ...
    return knst_dev::result<knst_aud::pcm_buffer>::success(std::move(out));
}

knst_aud::codec::registry::add(knst_aud::container_t::ogg, my_decoder, nullptr);
auto r = knst_aud::decode_file(u"my.ogg");   // custom decoder kullanılır
```

### Örnek 8: Peak meter + volume loop

```cpp
auto sinks = knst_aud::audio::list_sinks();
if (!sinks || sinks.value.empty()) return;

while (running) {
    auto peak = knst_aud::audio::get_peak_meter(sinks.value[0]);
    if (peak) {
        std::cout << "Peak: " << (peak.value * 100.0) << "%\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
```

---

## 📌 17) API Kontrat Özeti

```cpp
namespace knst_aud {

using knst_dev::status;
using knst_dev::result;

// ─── Enums ───
enum class container_t : uint8_t { /* ... */ };
enum class codec_t     : uint8_t { /* ... */ };
enum class sample_fmt  : uint8_t { /* ... */ };
enum class hotplug_event : uint8_t { added, removed, default_changed };

// ─── Types ───
struct level_info;
struct pcm_buffer;
struct media_info;
struct decode_options;
struct encode_options;
struct play_options;
struct server_info;
struct card_info;
struct stream_info;
struct device_format;
struct wav_info;
struct device_change;

// ─── Helpers ───
const char* container_name(container_t) noexcept;
uint32_t    sample_bytes(sample_fmt) noexcept;
knst_c16string format_duration(uint32_t) noexcept;
double percent_to_db(uint32_t) noexcept;
uint32_t db_to_percent(double) noexcept;
void write_wav_header(unsigned char*, uint32_t, uint32_t,
                      uint16_t, uint16_t, uint16_t = 1) noexcept;
wav_info parse_wav_header(const unsigned char*, uint32_t) noexcept;

// ─── Codec ───
namespace codec {
    container_t detect(const uint8_t*, size_t) noexcept;
    container_t detect_extension(const knst_c16string&) noexcept;
    bool        is_native(container_t) noexcept;

    class registry {
    public:
        static bool add(container_t, decoder_fn, void* = nullptr) noexcept;
    };

    namespace sndfile {
        api& get() noexcept;
        result<pcm_buffer> decode(...) noexcept;
        result<knst_byte_string> encode(...) noexcept;
    }
    namespace flacdec { result<pcm_buffer> decode(...) noexcept; }
    namespace flacenc { result<knst_byte_string> encode(const float*, uint32_t, uint32_t, uint32_t, int) noexcept; }
    namespace mp3_native { result<pcm_buffer> decode(...) noexcept; }
#if defined(KNST_NETWORK_HAVE_FAAD2)
    namespace aac_native { result<pcm_buffer> decode_adts(...) noexcept; }
#endif
#if defined(KNST_NETWORK_HAVE_VORBIS)
    namespace vorbis_enc { result<knst_byte_string> encode(...) noexcept; }
#endif

    struct wav_hdr { uint8_t b[96]; uint32_t len; };
    bool  make_wav_header(wav_hdr&, uint32_t, uint32_t, sample_fmt, uint64_t, uint64_t) noexcept;
    uint32_t channel_mask_for(uint32_t) noexcept;
}

// ─── Decode / Encode ───
result<pcm_buffer> decode(const uint8_t*, uint32_t,
                          const decode_options& = {}, container_t = {}) noexcept;
result<pcm_buffer> decode_file(const knst_c16string&,
                               const decode_options& = {}) noexcept;
result<media_info> probe(const knst_c16string&) noexcept;
result<media_info> probe(const uint8_t*, uint32_t) noexcept;

result<knst_byte_string> encode(const float*, uint32_t, uint32_t, uint32_t,
                                const encode_options& = {}) noexcept;
result<knst_byte_string> encode(const pcm_buffer&,
                                const encode_options& = {}) noexcept;
result<void> encode_file(const knst_c16string&, const pcm_buffer&,
                         encode_options = {}) noexcept;

// ─── WAV writer ───
class wav_writer {
public:
    result<void> open(const knst_c16string&, uint32_t, uint32_t, sample_fmt = i16, bool = false) noexcept;
    result<uint32_t> write(const float*, uint32_t) noexcept;
    result<void> close() noexcept;
    bool is_open() const noexcept;
    uint64_t frames_written() const noexcept;
    double seconds_written() const noexcept;
};

// ─── WAV legacy ───
namespace wav {
    struct format { uint32_t rate; uint32_t channels; sample_fmt sample; };
    struct info { format fmt; uint32_t data_bytes; uint32_t frame_count; uint16_t format_tag; bool valid; };

    result<info> probe(const knst_c16string&) noexcept;
    result<info> probe_memory(const uint8_t*, uint32_t) noexcept;
    result<knst_vector<float>> decode_to_float(const knst_c16string&) noexcept;
    result<knst_vector<float>> decode_to_float(const uint8_t*, uint32_t) noexcept;
    result<knst_byte_string> encode_memory(const float*, uint32_t, const format&) noexcept;
    result<void> encode(const knst_c16string&, const float*, uint32_t, const format&) noexcept;
}

// ─── DSP ───
namespace dsp {
    struct stats { float peak, rms, peak_db, rms_db; uint32_t clipped, samples; bool valid; };
    enum class resample_quality : uint8_t { fast, medium, high, best };
    enum class filter_t : uint8_t { lowpass, highpass, bandpass, notch, allpass, peak, lowshelf, highshelf };
    enum class wave_t   : uint8_t { sine, square, saw, triangle, noise };

    float to_db(float) noexcept;
    float from_db(float) noexcept;

    stats analyze(const float*, uint32_t) noexcept;
    level_info measure(const float*, uint32_t) noexcept;
    float dc_offset(const float*, uint32_t) noexcept;
    void  dc_remove(float*, uint32_t) noexcept;
    void  dc_remove_interleaved(float*, uint32_t, uint32_t) noexcept;
    void  clip(float*, uint32_t) noexcept;
    void  apply_gain(float*, uint32_t, float) noexcept;
    void  apply_gain_db(float*, uint32_t, float) noexcept;
    void  fade_in(float*, uint32_t, uint32_t, uint32_t) noexcept;
    void  fade_out(float*, uint32_t, uint32_t, uint32_t) noexcept;
    void  mix_into(float*, const float*, uint32_t, float = 1.0f) noexcept;
    knst_vector<float> mix(const float*, const float*, uint32_t, uint32_t) noexcept;
    knst_vector<float> trim(const float*, uint32_t, uint32_t, uint32_t, uint32_t) noexcept;
    knst_vector<float> concat(const float*, uint32_t, const float*, uint32_t, uint32_t) noexcept;
    knst_vector<float> crossfade(const float*, uint32_t, const float*, uint32_t, uint32_t, uint32_t) noexcept;
    void reverse(float*, uint32_t, uint32_t) noexcept;
    void pan_stereo(float*, uint32_t, float) noexcept;
    void normalize(float*, uint32_t, float = 0.95f) noexcept;
    void normalize_rms(float*, uint32_t, float = -18.0f, bool = true) noexcept;
    bool is_silent(const float*, uint32_t, float = -60.0f) noexcept;
    bool find_active_range(const float*, uint32_t, uint32_t, float, uint32_t&, uint32_t&) noexcept;
    knst_vector<float> trim_silence(const float*, uint32_t, uint32_t, float = -60.0f) noexcept;
    void soft_clip(float*, uint32_t) noexcept;
    void limiter(float*, uint32_t, uint32_t, uint32_t, float = -1.0f, float = 80.0f) noexcept;
    void interleave(const float* const*, uint32_t, uint32_t, float*) noexcept;
    void deinterleave(const float*, uint32_t, uint32_t, float* const*) noexcept;
    knst_vector<float> convert_channels(const float*, uint32_t, uint32_t, uint32_t) noexcept;
    knst_vector<float> resample_linear(const float*, uint32_t, uint32_t, uint32_t, uint32_t) noexcept;
    knst_vector<float> resample(const float*, uint32_t, uint32_t, uint32_t, uint32_t, resample_quality = high) noexcept;
    struct biquad_coeffs { double b0, b1, b2, a1, a2; };
    biquad_coeffs make_biquad(filter_t, double, double, double = 0.7071, double = 0.0) noexcept;
    class biquad { /* ... */ };
    bool is_pow2(uint32_t) noexcept;
    bool fft(float*, float*, uint32_t) noexcept;
    knst_vector<float> magnitude_spectrum(const float*, uint32_t) noexcept;
    double dominant_frequency(const float*, uint32_t, uint32_t) noexcept;
    void generate(float*, uint32_t, uint32_t, wave_t, float, uint32_t, float = 1.0f) noexcept;
}

// ─── Buffer conveniences ───
result<pcm_buffer> resample_buffer(const pcm_buffer&, uint32_t,
                                    dsp::resample_quality = high) noexcept;
result<pcm_buffer> convert_buffer_channels(const pcm_buffer&, uint32_t) noexcept;

// ─── stream ───
class stream {
public:
    struct config { uint32_t rate, channels, buffer_frames; bool is_output; };
    static result<stream> open(const knst_audio_device&, const config&) noexcept;
    static result<stream> open_default(const config&) noexcept;
    result<uint32_t> write_interleaved(const float*, uint32_t) noexcept;
    result<uint32_t> write_interleaved(const int16_t*, uint32_t) noexcept;
    result<uint32_t> read_interleaved(float*, uint32_t) noexcept;
    result<uint32_t> read_interleaved(int16_t*, uint32_t) noexcept;
    void drain() noexcept;
    double latency_ms() const noexcept;
    void close() noexcept;
    uint32_t rate() const noexcept;
    uint32_t channels() const noexcept;
    uint32_t xruns() const noexcept;
    bool is_output() const noexcept;
    bool is_open() const noexcept;
};

// ─── decoder_stream ───
class decoder_stream {
public:
    static result<decoder_stream> open(const knst_c16string&) noexcept;
    result<uint32_t> read(float*, uint32_t) noexcept;
    result<uint32_t> read(int16_t*, uint32_t) noexcept;
    result<uint64_t> seek_frames(uint64_t) noexcept;
    result<uint64_t> play_to(stream&, const std::atomic<bool>* = nullptr) noexcept;
    uint32_t rate() const noexcept;
    uint32_t channels() const noexcept;
    container_t container() const noexcept;
    uint64_t total_frames() const noexcept;
    double duration() const noexcept;
    uint64_t position() const noexcept;
    bool eof() const noexcept;
    void close() noexcept;
};

// ─── player / mixer ───
class player {
public:
    static result<player> from_buffer(pcm_buffer&&) noexcept;
    static result<player> open(const knst_c16string&, const decode_options& = {}) noexcept;
    static result<player> open(const uint8_t*, uint32_t, const decode_options& = {}) noexcept;
    uint32_t channels() const noexcept;
    uint32_t sample_rate() const noexcept;
    uint32_t total_frames() const noexcept;
    double duration_seconds() const noexcept;
    uint32_t position_frames() const noexcept;
    double position_seconds() const noexcept;
    bool eof() const noexcept;
    void seek_frames(uint32_t) noexcept;
    void seek_seconds(double) noexcept;
    void rewind() noexcept;
    void set_loop(bool) noexcept;
    void set_gain(float) noexcept;
    result<uint32_t> read_interleaved(float*, uint32_t) noexcept;
    result<uint32_t> read_interleaved(int16_t*, uint32_t) noexcept;
    result<uint32_t> play_frames(stream&, uint32_t) noexcept;
    result<player> resampled(uint32_t, dsp::resample_quality = high) const noexcept;
};

class mixer {
public:
    static constexpr int32_t kMaxSources = 32;
    mixer(uint32_t, uint32_t) noexcept;
    int32_t add(const pcm_buffer&, float = 1.0f, float = 0.0f, bool = false) noexcept;
    void remove(int32_t) noexcept;
    void set_gain(int32_t, float) noexcept;
    void set_pan(int32_t, float) noexcept;
    void pause(int32_t, bool) noexcept;
    void set_master(float) noexcept;
    void clear() noexcept;
    uint32_t active_count() const noexcept;
    uint32_t rate() const noexcept;
    uint32_t channels() const noexcept;
    result<uint32_t> render(float*, uint32_t) noexcept;
};

// ─── Playback helpers ───
result<void> play_buffer(const knst_audio_device*, const pcm_buffer&,
                          const std::atomic<bool>* = nullptr) noexcept;
result<void> play_file_any(const knst_audio_device*, const knst_c16string&,
                            const std::atomic<bool>* = nullptr) noexcept;

// ─── audio class ───
class audio {
public:
    static bool is_running() noexcept;
    static result<knst_vector<knst_audio_device>> list() noexcept;
    static result<knst_vector<knst_audio_device>> list_sinks() noexcept;
    static result<knst_vector<knst_audio_device>> list_sources() noexcept;
    static result<knst_audio_device> get_default(bool) noexcept;
    static result<void> set_default(const knst_audio_device&) noexcept;
    static result<knst_audio_device> get_device_detail(const knst_audio_device&) noexcept;
    static result<device_format> get_format(const knst_audio_device&) noexcept;
    static result<uint32_t> get_volume(const knst_audio_device&) noexcept;
    static result<void> set_volume(const knst_audio_device&, uint32_t) noexcept;
    static result<double> get_volume_db(const knst_audio_device&) noexcept;
    static result<void> set_volume_db(const knst_audio_device&, double) noexcept;
    static result<uint32_t> get_channel_count(const knst_audio_device&) noexcept;
    static result<uint32_t> get_channel_volume(const knst_audio_device&, uint32_t) noexcept;
    static result<void> set_channel_volume(const knst_audio_device&, uint32_t, uint32_t) noexcept;
    static result<bool> is_muted(const knst_audio_device&) noexcept;
    static result<void> set_mute(const knst_audio_device&, bool) noexcept;
    static result<knst_vector<knst_c16string>> get_ports(const knst_audio_device&) noexcept;
    static result<void> set_port(const knst_audio_device&, const knst_c16string&) noexcept;
    static result<knst_vector<card_info>> list_cards() noexcept;
    static result<void> set_profile(uint32_t, const knst_c16string&) noexcept;
    static result<server_info> server() noexcept;
    static result<uint32_t> get_balance(const knst_audio_device&) noexcept;
    static result<void> set_balance(const knst_audio_device&, uint32_t) noexcept;
    static result<knst_vector<stream_info>> list_sink_inputs() noexcept;
    static result<knst_vector<stream_info>> list_source_outputs() noexcept;
    static result<knst_vector<stream_info>> list_streams() noexcept;
    static result<void> set_stream_volume(uint32_t, uint32_t, bool = true) noexcept;
    static result<void> set_stream_mute(uint32_t, bool, bool = true) noexcept;
    static result<void> move_stream(uint32_t, uint32_t, bool = true) noexcept;
    static result<void> cork_stream(uint32_t, bool, bool = true) noexcept;
    static result<void> suspend(const knst_audio_device&, bool) noexcept;
    static result<double> get_peak_meter(const knst_audio_device&) noexcept;
    static result<void> play_file(const knst_audio_device*, const knst_c16string&,
                                   uint32_t = 600000) noexcept;
    static result<void> play_raw(const knst_audio_device*,
                                  const knst_byte_string&,
                                  const play_options& = {},
                                  uint32_t = 600000) noexcept;
    // Android only
    static result<void> request_audio_focus() noexcept;
    static result<void> abandon_audio_focus() noexcept;
};

// ─── Hotplug ───
using hotplug_callback = void (*)(void*, const device_change&);
class hotplug_monitor {
public:
    static result<hotplug_monitor> start(hotplug_callback, void*,
                                          uint32_t = 1000) noexcept;
    void stop() noexcept;
    bool is_running() const noexcept;
};

// ─── JSON (KNST_JSON_HPP) ───
#if defined(KNST_JSON_HPP)
inline knst_json devices_to_json(const knst_vector<knst_audio_device>&) noexcept;
inline knst_json stream_info_to_json(const knst_vector<stream_info>&) noexcept;
inline knst_json media_info_to_json(const media_info&) noexcept;
#endif

} // namespace knst_aud
```

---

## 🎯 Size Getireceği Kazanç

`knst_audio` şu durumlarda ciddi avantaj sağlar:

- ✅ **Native decoder stack** — WAV/AIFF/AU/FLAC/MP3 hiçbir dış kütüphane olmadan
- ✅ **Native FLAC encoder** — Zero dependency, spec-compliant
- ✅ **Streaming decode** — `decoder_stream` ile 3 saatlik MP3 bile RAM'i şişirmez
- ✅ **Format detection** — 20+ konteyner, magic bytes + extension
- ✅ **Sndfile backend** — OGG/OPUS/WavPack/APE/TTA opsiyonel
- ✅ **DSP zinciri** — gain, fade, normalize, mix, trim, DC remove, limiter
- ✅ **Channel conversion** — 5.1/7.1 → stereo/mono, mono → N
- ✅ **Resampling** — Linear + windowed-sinc (Kaiser, 4 seviye)
- ✅ **Biquad filters** — RBJ cookbook, multi-channel
- ✅ **FFT / spectrum** — Radix-2, magnitude, dominant freq
- ✅ **Real-time stream** — Linux/Windows/Android
- ✅ **`player` + `mixer`** — seek, loop, gain, mix
- ✅ **Per-app streams** — Linux sink-inputs, Windows sessions
- ✅ **Hotplug monitor** — polling-based, thread-safe
- ✅ **Peak meter** — Linux + Windows
- ✅ **AudioFocus** — Android best practice
- ✅ **KernelNucleusT entegrasyonu** — `knst_file`, `knst_devices`, `knst_converter`, `knst_microphone`, `knst_camera`

---