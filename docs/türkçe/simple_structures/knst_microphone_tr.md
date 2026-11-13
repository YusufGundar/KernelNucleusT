# knst_microphone — Mikrofon Yakalama ve Ses İşleme

Selamlar! 👋 `knst_microphone` namespace'i, KernelNucleusT'nin **mikrofon yakalama ve ses işleme** modülü. Mikrofon listeleme, PCM/WAV kayıt, gerçek zamanlı stream, seviye ölçümü, VAD (Voice Activity Detection), AGC, noise gate, pitch detection, silence detection, voice-triggered recorder, loopback capture ve beamforming — hepsi tek header'da.

**Kısaca:** `knst_audio.hpp`'ın üzerine kurulur. **Synchronous, thread yok, her çağrı `result<T>` döner.** Ses işleme state machine'leri (VAD, AGC, gate) **step function** — blok blok beslersin, iç state güncellenir, karar döner. Bu sayede herhangi bir loop'a (thread'li veya thread'siz) entegre edebilirsin.

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

### Platform matrisi

| Özellik | Windows | Linux | Android |
|---|---|---|---|
| **i16 capture (raw)** | ✅ waveIn (default) / WASAPI | ✅ `parec` | ✅ AAudio |
| **i24 / i32 / f32 capture** | ✅ WASAPI | ✅ `parec` | ✅ AAudio (f32) |
| **Default mic** | ✅ | ✅ | ✅ |
| **Adlandırılmış cihaz seçimi** | ✅ WASAPI (endpoint id) | ✅ `parec --device` | ❌ AAudio default only |
| **Loopback (system audio)** | ✅ WASAPI | ⚠️ monitor source gerekli | ✅ MediaProjection (JNI) |
| **Mikrofon enumerasyonu** | ✅ `knst_aud::audio::list_sources` | ✅ | ✅ |
| **Monitor detection** | ✅ | ✅ | ✅ |
| **WAV kayıt** | ✅ | ✅ | ✅ |
| **Stream (real-time)** | ✅ | ✅ | ✅ |
| **Seviye ölçümü (peak/rms/dB/clip)** | ✅ | ✅ | ✅ |
| **VAD (voice detection)** | ✅ | ✅ | ✅ |
| **AGC** | ✅ | ✅ | ✅ |
| **Noise gate** | ✅ | ✅ | ✅ |
| **Pitch detection** | ✅ | ✅ | ✅ |
| **Silence detection** | ✅ | ✅ | ✅ |
| **Voice-triggered recorder** | ✅ | ✅ | ✅ |
| **Beamforming (2 mic)** | ✅ | ✅ | ✅ |
| **Format conversion** | ✅ | ✅ | ✅ |
| **JSON bridge** | ✅ (KNST_JSON_HPP) | ✅ | ✅ |
| **Kayıt süresi limiti** | **≤ 60 saniye** | ≤ 60 s | ≤ 60 s |

**Kritik notlar:**
- **Windows:** WaveIn default cihaz için hızlı (COM yok). Adlandırılmış cihaz veya loopback/multi-format için **WASAPI** kullanılır.
- **Linux:** `parec` (PulseAudio/PipeWire) gerekli. **Loopback için monitor source'u kendin seçmelisin** — `parec`'in built-in "default monitor" özelliği yok.
- **Android:** PlaybackCapture için **app'in MediaProjection permission alması** gerekli. `mic_playback::set_permission()` çağırılmadan loopback `BadState` döner.
- **AAudio isimli cihaz seçemez** — sadece default input veya `device_id` (Android `AudioDeviceInfo.getId()`).
- **`record_raw` / `record_fmt` max 60 saniye.** Daha uzun kayıt için `recorder` sınıfı veya `knst_aud::stream` kullan.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Multi-format capture** | i16, i24, i32, f32 — platform backend'leri optimize |
| **Loopback** | Windows WASAPI, Android PlaybackCapture |
| **VAD** | Enerji + ZCR + hangover state machine |
| **AGC** | Peak follower, attack/release |
| **Noise gate** | Threshold + envelope follower |
| **Pitch detection** | Autocorrelation + parabolic interpolation |
| **Silence detection** | RMS threshold + duration |
| **Voice recorder** | Pre-roll ring buffer + VAD-gated |
| **Beamforming** | Delay-and-sum 2-mic |
| **Level measurement** | Peak, RMS, dB, clip count |
| **Format helpers** | int16 ↔ float, bytes ↔ float |
| **Recorder class** | File + memory mode, header-aware |

---

## 🚀 Hızlı Başlangıç

### Basit kayıt (i16, default mic)

```cpp
#include "KernelNucleusT.hpp"

// 2 saniyelik 16 kHz mono kayıt
auto raw = knst_mic::record_raw(nullptr, 2000, 16000, 1);
if (raw) {
    std::cout << "Captured: " << raw.value.length() << " bytes\n";
}
```

### WAV dosyası

```cpp
auto r = knst_mic::record_wav(nullptr, u"output.wav", 3, 48000, 2);
if (!r) {
    std::cerr << "Kayıt başarısız\n";
}
```

### Mikrofon listesi

```cpp
auto mics = knst_mic::enumerate_microphones();
for (auto& m : mics.value) {
    std::cout << "[" << m.index << "] " << m.name
              << (m.is_default ? " (default)" : "")
              << (m.is_monitor ? " (monitor)" : "")
              << "\n";
}
```

### Seviye ölçümü

```cpp
auto lvl = knst_mic::measure_level(nullptr, 500);
if (lvl) {
    std::cout << "Peak: " << lvl.value.peak_db << " dB\n";
    std::cout << "RMS : " << lvl.value.rms_db  << " dB\n";
    std::cout << "Clip: " << lvl.value.clipped << "\n";
}
```

---

## 📋 Genel API

| Kategori | Fonksiyon / Sınıf |
|---|---|
| **Format** | `capture_format` |
| **Record** | `record_raw`, `record_fmt`, `record_wav` |
| **Recorder** | `recorder` sınıfı |
| **Enumerasyon** | `enumerate_microphones`, `default_microphone`, `monitor_source`, `is_monitor` |
| **Seviye** | `measure_level`, `measure_all` |
| **VAD** | `voice_detector`, `vad_config` |
| **AGC** | `agc`, `agc_config` |
| **Gate** | `noise_gate`, `gate_config` |
| **Pitch** | `pitch_detector`, `pitch_config` |
| **Silence** | `silence_detector`, `silence_config` |
| **Voice recorder** | `voice_recorder` |
| **Beamforming** | `estimate_delay`, `beamform_2mic`, `mic_array` |
| **Format helpers** | `fmt::*` |
| **JSON** | `mic_refs_to_json` (KNST_JSON_HPP) |
| **Platform backend** | `mic_aaudio`, `mic_playback`, `mic_wavein`, `mic_wasapi` |

---

## 🎯 1) `capture_format` — Yakalama Formatı

```cpp
struct capture_format {
    enum class sample_t : uint8_t { i16 = 0, i24, i32, f32 };
    uint32_t  sample_rate = 48000;
    uint32_t  channels    = 2;
    sample_t  sample      = sample_t::i16;
    bool      loopback    = false;
    int32_t   device_id   = 0;    // 0 = default; Android AudioDeviceInfo.getId()
};
```

| Alan | Anlamı |
|---|---|
| `sample_rate` | Örnekleme hızı (Hz). **8000..192000** valid. |
| `channels` | Kanal sayısı. **1..8** valid. |
| `sample` | `i16`, `i24`, `i32`, `f32` |
| `loopback` | `true` → sistem ses çıkışını yakala |
| `device_id` | Android `AudioDeviceInfo.getId()` — sadece AAudio |

**Platform desteği:**
- **i16:** Her platformda çalışır (waveIn/AAudio/parec)
- **i24/i32/f32:** WASAPI (Windows), AAudio f32 (Android), `parec` (Linux)
- **Loopback:** WASAPI (Windows), PlaybackCapture (Android), monitor source (Linux — manuel)
- **device_id:** Sadece Android AAudio, `>0` ise set edilir

---

## 🎤 2) Kayıt Fonksiyonları

### 2.1) `record_raw` — Basit (i16)

```cpp
result<knst_byte_string> record_raw(
    const knst_audio_device* device,
    uint32_t ms,                    // 50..60000
    uint32_t rate = 16000,
    uint32_t channels = 1) noexcept;
```

**Kısıtlar:**
- `ms` — **50..60000** (1 dakika)
- `rate` — 8000..192000
- `channels` — 1..8
- `device->is_input` olmalı (device verildiyse)

**Dönüş:** i16 interleaved PCM byte'ları. Kısa gelirse **zero-pad**.

**Platform davranışı:**
- **Windows + device yok:** waveIn (hızlı)
- **Windows + device var:** WASAPI (adlandırılmış cihaz)
- **Linux:** `parec` (timeout command ile)
- **Android:** AAudio

### 2.2) `record_fmt` — Extended

```cpp
result<knst_byte_string> record_fmt(
    const knst_audio_device* device,
    uint32_t ms,                    // 50..60000
    const capture_format& f) noexcept;
```

**Kısıtlar:**
- Aynı `ms` limiti (50..60000)
- Aynı `rate` / `channels` limitleri
- **Linux:** `f.loopback && !device` → `NotSupported` (parec default monitor yok)

**`f.sample == i16 && !f.loopback` fast path:** `_record_raw_legacy` — ucuz default.
**Diğer:** platform-spesifik extended path.

### 2.3) `record_wav` — WAV dosyası

```cpp
result<void> record_wav(
    const knst_audio_device* device,
    const knst_c16string& path,
    uint32_t seconds = 5,           // 1..3600 (ölçüm değil, uint32_t saniye)
    uint32_t rate = 48000,
    uint32_t channels = 1) noexcept;
```

**İç çağrı:** `record_raw(device, seconds * 1000u, rate, channels)` — yani **max 60 saniye** (record_raw limiti). Daha uzun için `recorder` sınıfı.

---

## 💾 3) `recorder` — Dosya + Memory Recorder

```cpp
class recorder {
public:
    enum class mode_t : uint8_t { none, file, memory };

    static result<recorder> open_file(const knst_c16string& path,
                                       const knst_aud::wav::format& f) noexcept;
    static result<recorder> open_memory(const knst_aud::wav::format& f) noexcept;

    result<uint32_t> capture_interleaved(const float* buffer, uint32_t frames) noexcept;
    result<uint32_t> pump_from(knst_aud::stream& source, uint32_t frames) noexcept;
    result<void>     finish() noexcept;
    void             abort() noexcept;

    uint32_t total_frames() const noexcept;
    double   recorded_seconds() const noexcept;
    const knst_aud::wav::format& fmt() const noexcept;
    const knst_vector<float>& samples() const noexcept;
    mode_t mode() const noexcept;
    bool is_open() const noexcept;
};
```

### 3.1) File mode

**`open_file`** — WAV header yazar (correct size `hdr_len_` saklar). `finish()` çağrısında **header yeniden yazılır** — toplam boyut + fakt frame güncellenir.

**`capture_interleaved`** — float input alır, WAV encode eder (`knst_aud::wav::encode_memory`), payload'u dosyaya append eder.

**`hdr_len_` ne önemli?** WAV header boyutu formata göre değişir:
- **PCM i16, ≤2 ch:** 44 byte
- **float / alaw / ulaw:** +12 = 56 byte (`fact` chunk)
- **PCM 3+ ch (extensible):** +24 = 68 byte
- **float 3+ ch:** +24 +12 = 80 byte

`make_wav_header` doğru boyutu döner, `hdr_len_` saklanır. `capture_interleaved` **header byte'larını atlar**, sadece payload'u append eder. Aksi halde capture sırasında PCM başlangıcı header'ın ortasına denk gelir (garbage).

### 3.2) Memory mode

`capture_interleaved` float'ları `samples_` vector'üne append eder.

**`finish()`** — memory mode'da no-op (`mode_ = none`).

### 3.3) `pump_from`

```cpp
result<uint32_t> pump_from(knst_aud::stream& source, uint32_t frames) noexcept;
```

`knst_aud::stream`'den N frame okur, recorder'a yazar. **Channel sayısı eşleşmeli** — değilse `InvalidArgument`.

### 3.4) `finish` vs `abort`

- **`finish()`** — file mode'da header yeniden yazılır (correct size). Mode `none` olur.
- **`abort()`** — destructive. Header yazılmaz, samples clear. Yarım dosya kalır (garbage header).

---

## 🔍 4) Enumerasyon

### 4.1) `mic_ref` — Mikrofon referansı

```cpp
struct mic_ref {
    knst_c16string name;
    knst_c16string description;
    uint32_t       index = 0;
    bool           is_default = false;
    bool           is_monitor = false;
};
```

### 4.2) `is_monitor`

```cpp
bool is_monitor(const knst_c16string& name) noexcept;
```

**Ne yapar?** Name içinde `.monitor` veya `Monitor` substring arar. Monitor kaynaklar (loopback) gerçek mikrofon değil — enumerated list'te filter'lanır.

### 4.3) `enumerate_microphones`

```cpp
result<knst_vector<mic_ref>> enumerate_microphones(bool include_monitors = false) noexcept;
```

`knst_aud::audio::list_sources()` üzerinden input device'ları filtreler, monitor'ları default olarak atar.

### 4.4) `default_microphone`

```cpp
result<knst_audio_device> default_microphone() noexcept;
```

Default + non-monitor'u döner. Yoksa **ilk non-monitor**. Hiçbiri yoksa `NotFound`.

### 4.5) `monitor_source`

```cpp
result<knst_audio_device> monitor_source() noexcept;
```

İlk monitor kaynağı döner. Loopback capture için gerekli (Linux).

---

## 📊 5) Seviye Ölçümü

### 5.1) `measure_level`

```cpp
result<knst_aud::level_info> measure_level(
    const knst_audio_device* device,
    uint32_t ms = 500) noexcept;    // 100..5000
```

**İç çağrı:** `record_raw(device, ms, 16000, 1)` — i16 mono 16 kHz.

**`level_info` alanları:**
- `peak` — 0..1 (mutlak tepe)
- `rms` — 0..1 (RMS)
- `peak_db` — `20*log10(peak)` veya sessizse -144
- `rms_db` — `20*log10(rms)` veya sessizse -144
- `samples` — örnek sayısı
- `clipped` — clip örnek sayısı (i16 -32768 veya 32767)

### 5.2) `measure_all`

```cpp
result<knst_vector<knst_aud::level_info>> measure_all(uint32_t ms = 300) noexcept;
```

**Her kaynak için** `measure_level` çağırır. **Önemli detay:** Her capture arasında **30 ms sleep** — PulseAudio/PipeWire'ın önceki `parec` device'ı bırakması için. Aksi halde back-to-back `measure_all` bazen fail eder.

**Fail davranışı:** Level ölçülemezse `peak_db = -144`, `rms_db = -144` sentinel ile push edilir. Böylece "sessiz" ile "ölçülemedi" ayrılır (default `level_info()` 0 dB gösterir ki yanıltıcı).

---

## 🎙️ 6) VAD — Voice Activity Detection

### 6.1) `vad_config`

```cpp
struct vad_config {
    uint32_t frame_ms         = 20;       // frame length
    float    energy_threshold = 0.01f;    // RMS threshold
    float    zcr_max          = 0.3f;     // ZCR threshold
    uint32_t hangover_frames  = 5;        // keep "active" after silence
    uint32_t trigger_frames   = 3;        // N consecutive active → trigger
    uint32_t sample_rate      = 16000;
};
```

| Alan | Anlamı |
|---|---|
| `frame_ms` | Frame uzunluğu (bilgi amaçlı) |
| `energy_threshold` | RMS eşiği. Yüksekse konuşma zor algılanır |
| `zcr_max` | Zero-crossing rate üst limiti. Sesli harfler ~0.05, sessiz ~0.4 |
| `hangover_frames` | Ses bittikten sonra N frame daha "aktif" tut |
| `trigger_frames` | N ardışık aktif frame → start trigger |
| `sample_rate` | Sadece bilgi (RMS hesaplaması için) |

### 6.2) `voice_detector`

```cpp
class voice_detector {
public:
    explicit voice_detector(const vad_config& c = vad_config()) noexcept;

    bool feed(const float* block, uint32_t n) noexcept;
    enum class edge : uint8_t { none, start, end };
    edge last_transition() const noexcept;
    bool is_active() const noexcept;
};
```

**`feed` ne yapar?**
1. **RMS energy** hesapla
2. **Zero-crossing rate** hesapla
3. `rms >= energy_threshold && zcr <= zcr_max` → frame aktif
4. `active_run_` sayacı + `hangover_left_` timer
5. **Trigger kararı:** `active_run >= trigger_frames || hangover_left > 0`
6. `was_active_` değiştiyse **edge** set et
7. **Dönüş:** şu an aktif mi

**State machine:**
```
idle ──(trigger)──> active
active ──(hangover ends)──> idle
```

**`last_transition()`** — `start` (sessiz → ses), `end` (ses → sessiz), `none` (aynı).

---

## 🎚️ 7) AGC — Automatic Gain Control

### 7.1) `agc_config`

```cpp
struct agc_config {
    float target_peak    = 0.5f;
    float min_gain       = 0.1f;
    float max_gain       = 10.0f;
    float attack_factor  = 0.3f;    // hızlı (sinyal yüksekken)
    float release_factor = 0.05f;   // yavaş (sinyal düşükken)
    float noise_floor    = 0.001f;  // altında amplify etme
};
```

### 7.2) `agc`

```cpp
class agc {
public:
    explicit agc(const agc_config& c = agc_config()) noexcept;

    float process(float* block, uint32_t n) noexcept;
    float current_gain() const noexcept;
    void  set_gain(float g) noexcept;
    void  reset() noexcept;
};
```

**`process` ne yapar?**
1. Blok peak hesapla
2. `peak < noise_floor` → gain değiştirme (sessizliği amplify etme), **gain uygula**
3. `want = target_peak / peak` → min/max'a clamp
4. Attack (want < gain) veya release (want > gain) katsayısı seç
5. Exponential smoothing: `gain = gain * (1-factor) + want * factor`
6. Blok'a gain uygula, ±1'e clamp

**Dönüş:** Uygulanan gain.

---

## 🚪 8) Noise Gate

### 8.1) `gate_config`

```cpp
struct gate_config {
    float threshold_db = -45.0f;    // altında gate kapat
    float attack_ms    = 5.0f;      // açılma (hızlı)
    float release_ms   = 100.0f;    // kapanma (yavaş)
    float floor_db     = -80.0f;    // tamamen kapanınca gain
    uint32_t sample_rate = 16000;
};
```

### 8.2) `noise_gate`

```cpp
class noise_gate {
public:
    explicit noise_gate(const gate_config& c = gate_config()) noexcept;

    void process(float* block, uint32_t n) noexcept;
    float envelope() const noexcept;
};
```

**Ne yapar?**
1. Envelope follower — attack/release coefficients
2. `env >= thresh` → gain = 1.0 (açık)
3. `env <= thresh * 0.5` → gain = floor (kapalı)
4. Arada → linear ramp floor ↔ 1.0
5. Blok'a gain uygula

**Coefficient formülü:** `coef = 1 - exp(-1 / (ms * sample_rate / 1000))`. `ms <= 0.1` ise `1.0` (anlık).

---

## 🎵 9) Pitch Detection

### 9.1) `pitch_config`

```cpp
struct pitch_config {
    float    min_hz    = 80.0f;
    float    max_hz    = 800.0f;
    uint32_t sample_rate = 16000;
};
```

### 9.2) `pitch_detector`

```cpp
class pitch_detector {
public:
    explicit pitch_detector(const pitch_config& c = pitch_config()) noexcept;

    float detect(const float* block, uint32_t n) noexcept;
};
```

**`detect` ne yapar?** Autocorrelation + parabolic interpolation:
1. Min/max lag = `sample_rate / max_hz` ve `sample_rate / min_hz`
2. **Energy check** — `sum < 1e-9` → return 0
3. Normalized autocorrelation — her lag için `sum(block[i] * block[i+lag]) / energy`
4. Global max bul → threshold = 0.9 * global_max
5. **İlk peak** (threshold üstünde) → best_lag
6. **Parabolic interpolation** — `refined = best_lag + 0.5 * (a - c) / (a - 2b + c)`
7. **Pitch = sample_rate / refined**

**Dönüş:** Hz veya 0 (pitch yok / çok sessiz).

**Neden ilk peak?** Autocorrelation pitch period'da peak verir ama harmoniklerde de peak olabilir. İlk peak **temel frekansı** verir.

---

## 🤫 10) Silence Detection

### 10.1) `silence_config`

```cpp
struct silence_config {
    float    threshold_db   = -50.0f;
    uint32_t min_silence_ms = 500;
    uint32_t sample_rate    = 16000;
};
```

### 10.2) `silence_detector`

```cpp
class silence_detector {
public:
    explicit silence_detector(const silence_config& c = silence_config()) noexcept;

    bool feed(const float* block, uint32_t n) noexcept;
    void reset() noexcept;
};
```

**`feed`:** RMS hesapla, dB'ye çevir. `rms_db < threshold_db` ise `silent_samples_` biriktir, aksi halde sıfırla. `min_silence_ms` aşıldıysa `true`.

---

## 📼 11) Voice-Triggered Recorder

### 11.1) `voice_recorder::config`

```cpp
struct config {
    vad_config vad;
    knst_aud::wav::format format;
    uint32_t min_speech_ms  = 300;    // kısa konuşmayı kaydetme
    uint32_t max_silence_ms = 1000;   // bu kadar sessizlik sonrası dur
    uint32_t pre_roll_ms    = 200;    // VAD öncesi N ms sakla
};
```

### 11.2) `voice_recorder`

```cpp
class voice_recorder {
public:
    explicit voice_recorder(const config& c) noexcept;

    result<void> start(const knst_c16string& path) noexcept;
    uint32_t feed(const float* block, uint32_t n) noexcept;
    result<void> stop() noexcept;
    bool is_recording() const noexcept;
};
```

**`start`:** `format.channels == 1` **zorunlu** — `feed` mono blok alır, `capture_interleaved` `channels` ile çarpar. Aksi halde OOB.

**`feed` state machine:**
```
idle ──(VAD trigger)──> recording
     ↓                      ↓
  pre-roll buffer     live capture
                          ↓
                  (silence > max_silence && speech > min_speech)
                          ↓
                        idle (reset pre-roll)
```

**Pre-roll ring buffer:**
- `pre_ring_capacity = pre_roll_ms * rate / 1000`
- Cap: **4.8M samples** (~100 sn @ 48 kHz)
- `add_to_pre_roll` — yuvarlak buffer, valid count artar
- **`flush_pre_roll`** — VAD trigger'da **kronolojik sıraya dizilir** (write_pos'tan başlar), `capture_interleaved`'a verilir

**Kritik:** Pre-roll capture **VAD öncesi** ses içerir → konuşmanın başı kaybolmaz.

**`feed` dönüş:** yazılan byte sayısı (0 = yazılmadı).

**`stop`:** `recorder::finish()` → WAV header yeniden yazılır.

---

## 🎤 12) Beamforming (2 Mikrofon)

### 12.1) `estimate_delay`

```cpp
int32_t estimate_delay(
    const float* a, const float* b,
    uint32_t n, int32_t max_delay) noexcept;
```

**Ne yapar?** Cross-correlation ile iki kanal arasındaki **time delay**'i tahmin eder:
1. `d = -max_delay..+max_delay` için cross-correlation
2. `sum(a[i] * b[i+d]) / count` — normalize
3. **En yüksek** correlation'ın delay'ı döner

**Kullanım:** 2-mic array'de ses kaynağının hangi taraftan geldiğini tahmin.

### 12.2) `beamform_2mic`

```cpp
knst_vector<float> beamform_2mic(
    const float* ch0, const float* ch1,
    uint32_t n, int32_t delay) noexcept;
```

**Ne yapar?** Delay-and-sum:
```
out[i] = 0.5 * (ch0[i] + ch1[i + delay])
```

**`i + delay`** out-of-range ise `0.0f`.

### 12.3) `mic_array`

```cpp
class mic_array {
public:
    result<knst_vector<float>> beamform(
        const float* interleaved,
        uint32_t frames,
        uint32_t channels) noexcept;
};
```

**Davranış:**
- **1 ch:** doğrudan copy
- **2 ch:** `estimate_delay` + `beamform_2mic`
- **3+ ch:** `NotSupported` (basit 2-mic)

**Not:** `max_delay = 16` sample (yaklaşık 1 ms @ 16 kHz). Daha geniş aralık için `estimate_delay` doğrudan çağır.

---

## 🔧 13) Format Conversion Helpers

```cpp
namespace fmt {

inline void s16_to_float(const int16_t* in, float* out, uint32_t n) noexcept;
inline void float_to_s16(const float* in, int16_t* out, uint32_t n) noexcept;

inline knst_vector<float> s16_bytes_to_float(const knst_byte_string& b) noexcept;
inline knst_byte_string float_to_s16_bytes(const float* f, uint32_t n) noexcept;

}
```

**`s16_to_float`** — `/ 32768.0` (asimetrik ama yaygın).
**`float_to_s16`** — `× 32767.0` (clamp + round).
**`s16_bytes_to_float`** — little-endian byte'lardan okur.
**`float_to_s16_bytes`** — little-endian byte'lara yazar.

---

## 🌐 14) Platform Backend'leri

### 14.1) `mic_aaudio` — Android

```cpp
namespace mic_aaudio {
    struct api { /* dlopen'd fonksiyon pointers */ };
    api& get() noexcept;

    result<knst_byte_string> capture_s16le(...) noexcept;
    result<knst_byte_string> capture_f32(...) noexcept;
    result<knst_byte_string> capture_ext(...) noexcept;
}
```

**Dinamik yükleme:** `libaaudio.so` — `AAudio_*` sembolleri. Runtime'da `NotSupported` dönebilir (Android 8.0 öncesi).

**`capture_s16le` / `capture_f32`:** AAudio builder → stream → read loop. **Timeout:** `total_frames * 1000 / rate + 5000` ms. **Kısa gelirse zero-pad.**

**Eski sürüm bug'ı:** Hard-coded 10 sn timeout vardı, 30sn/60sn kayıtları silently truncate ediyordu. **Şimdi** `total_frames` bazlı ölçeklenir.

**`capture_ext`** — `f.loopback` ise `mic_playback::capture` (Java). Aksi halde AAudio. **Adlandırılmış cihaz AAudio'da yok** — `device->name` ignore.

### 14.2) `mic_playback` — Android PlaybackCapture

```cpp
namespace mic_playback {
    void set_permission(int result_code, jobject result_data);
    bool has_permission();
    result<knst_byte_string> capture(const capture_format& f, uint32_t total_frames) noexcept;
}
```

**App entegrasyonu gerekli:**
```java
// 1) MediaProjectionManager al
mpm = context.getSystemService(MEDIA_PROJECTION_SERVICE);

// 2) Intent oluştur
intent = mpm.createScreenCaptureIntent();

// 3) startActivityForResult(intent, REQ_CAPTURE)

// 4) onActivityResult:
knst_mic::mic_playback::set_permission(resultCode, data);
```

**`capture` ne yapar?**
1. **Temp PCM path** — `knst_playcap.pcm`
2. **JNI call** — `KnstPlaybackCapture.capture(path, rate, ch, fmt, frames)`
3. **File read** — raw PCM'i oku
4. **Remove temp**

**Hata durumları:**
- JNI env yok → `BadState`
- Class bulunamadı → `NotSupported`
- Exception → `IoError`
- File read fail → `IoError`
- Kısa PCM → zero-pad
- Uzun PCM → truncate

### 14.3) JNI Env — 3 kullanım modu

Kod 3 mod destekler:

1. **`KNST_MIC_USE_PRINTER_JNI=1`** — `knst_printer.hpp` önceden include edildiyse printer'ın JNI context'ini reuse et
2. **`KNST_MIC_HAS_EXTERNAL_ENV=1`** — kendi `knst_mic::knst_mic_external_env()` fonksiyonunu sağla (strong symbol weak'i override eder)
3. **Hiçbiri** — weak default `nullptr` döner → `PlaybackCapture` runtime'da `BadState`. Header test programlarında link'lenebilir kalır.

### 14.4) `mic_wavein` — Windows legacy

```cpp
namespace mic_wavein {
    result<knst_byte_string> capture_s16le(rate, ch, total_frames) noexcept;
}
```

**waveIn API** — Windows default cihaz, hızlı, COM yok. **Sadece i16.** Adlandırılmış cihaz veya loopback için WASAPI.

**Zero-fill:** `hdr.dwBytesRecorded < want_bytes` ise tail zero'lanır. **Önceden** `out` uninitialized memory içerebiliyordu → caller garbage sample görüyordu.

### 14.5) `mic_wasapi` — Windows modern

```cpp
namespace mic_wasapi {
    result<knst_byte_string> capture(
        const capture_format& f,
        uint32_t total_frames,
        const knst_audio_device* device) noexcept;
}
```

**Ne yapar?**
1. **COM init** (`CoInitializeEx` MULTITHREADED)
2. **MMDeviceEnumerator** → device veya default
3. **IAudioClient** → `GetMixFormat` (float32 olduğunu doğrula)
4. **Initialize** — SHARED mode, `loopback` flag
5. **IAudioCaptureClient** → event-driven read
6. **Downmix / upmix** — `mix_channels != f.channels` ise
7. **Format conversion** — float32 → `f.sample`
8. **Zero-pad** — kısa gelirse

**Kritik detaylar:**

**`resolve_device` — E_NOTIMPL kaldırıldı:**
```cpp
if (dev && !dev->name.empty()) {
    // LPCWSTR is the endpoint id "{0.0.0.00000000}.{GUID}"
    return en->GetDevice(reinterpret_cast<LPCWSTR>(dev->name.data()), out);
}
return en->GetDefaultAudioEndpoint(flow, eConsole, out);
```

**Eski bug:** `E_NOTIMPL` dönerse caller waveIn'e fallback ediyordu → kullanıcı "USB mic'ten kaydet" dese bile **default mic** kaydediliyordu. **Şimdi** cihaz seçimi honor edilir.

**Float32 mix formatı kontrolü:**
```cpp
bool is_float_mix = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                    (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ... subtype_float);
if (!is_float_mix) { CoTaskMemFree(mix); return NotSupported; }
```

**Neden?** `_w::convert` float32 olarak okur. Mix 16-bit PCM ise garbage çıkardı.

**Downmix / upmix:**
- **1 ch istenen:** sum / N
- **M < N (downmix):** ilk M kanal
- **M > N (upmix):** cyclic duplicate

**i32 conversion precision:**
```cpp
// Double, not .0f — 2147483647 cannot be exactly represented in float,
// .0f rounds to 2^31, overflows int32_t on v=1.0 → INT_MIN.
int32_t s = (int32_t)std::lround(v * 2147483647.0);
```

**COM init:** `RPC_E_CHANGED_MODE` kabul edilir (başka thread önceden init etmiş olabilir).

---

## 🐧 15) Linux-Spesifik

### 15.1) `parec` çağrısı

```cpp
knst_dev::argv a;
a << "-s" << "TERM" << "2.50" << "parec";
if (device) a << "device=...";
a << "--format=s16le" << "--rate=16000" << "--channels=1"
  << "--latency-msec=20" << "--raw";
knst_dev::run_codes("timeout", a, ms + 5000, { 124, 130, 143 });
```

**Timeout wrapper:** `timeout` komutu ile `parec` sınırlanır. `124` (timeout), `130` (SIGINT), `143` (SIGTERM) kabul edilir (normal sonlanma).

**Format mapping (`_parec_detail::parec_fmt`):**
- `i16` → `s16le`
- `i24` → `s24le`
- `i32` → `s32le`
- `f32` → `float32le`

### 15.2) Warmup trim

`k_warmup_ms = 80`. Total capture süresi `ms + 80`. `parec` başladıktan sonra ilk 80 ms drop edilir (PulseAudio latency). Sonra `ms` kadar data alınır.

### 15.3) Loopback

**Linux'ta built-in "default monitor" yok.** `parec --device=<monitor>` gerekli. `f.loopback && !device` → **`NotSupported`**.

**Doğru kullanım:**
```cpp
auto mon = knst_mic::monitor_source();
if (mon) {
    knst_mic::capture_format f;
    f.sample_rate = 48000;
    f.channels = 2;
    f.sample = knst_mic::capture_format::sample_t::i16;
    f.loopback = true;

    auto r = knst_mic::record_fmt(&mon.value, 5000, f);
}
```

---

## ⚠️ 16) Sık Tuzaklar

### 1) 60 saniye limiti

`record_raw` ve `record_fmt`: `ms <= 60000`. Daha uzun için:
- `recorder` sınıfı — `capture_interleaved` sınırsız
- `knst_aud::stream` — real-time

### 2) Windows'ta adlandırılmış cihaz

**Eski bug:** `resolve_device` `E_NOTIMPL` dönüyordu → waveIn fallback → default mic. **Şimdi** endpoint id `GetDevice()`'a geçer.

**Device name:** WASAPI endpoint id — `{0.0.0.00000000}.{GUID}`. `knst_aud::audio::list_sources()` bu formatta döner.

### 3) Windows'ta WASAPI loopback

`f.loopback = true` → `eRender` (output) endpoint. **IAudioClientInitialize** `AUDCLNT_STREAMFLAGS_LOOPBACK` flag'i ile. **Bu flag sadece shared mode** kabul eder (WASAPI kısıtı).

### 4) Windows'ta mix format 16-bit PCM olabilir

**Kontrol:** `WAVE_FORMAT_IEEE_FLOAT` veya `WAVE_FORMAT_EXTENSIBLE` + subtype float. **Değilse** `NotSupported` — `_w::convert` float32 bekler.

### 5) Windows'ta i32 round hatası

`v * 2147483647.0f` (`.0f`) → `2^31` (2147483648) → `int32_t` overflow → `INT_MIN`. **Doğrusu** `v * 2147483647.0` (double).

### 6) Android'de PlaybackCapture setup

`mic_playback::set_permission()` çağırılmadan loopback `BadState` döner. **App tarafı:**
```java
// onActivityResult:
KnstPlaybackCapture.setProjection(resultCode, data);
```

### 7) Android'de AAudio isimli cihaz

`s.setDeviceId(builder, device_id)` — sadece `device_id > 0` ise. **`device->name` ignore edilir.** `device_id` almak için Android `AudioManager.getDevices(GET_DEVICES_INPUTS)` + `AudioDeviceInfo.getId()`.

### 8) Linux'ta loopback parametresi

`f.loopback && !device` → `NotSupported`. **Önce** `monitor_source()` çağır, sonra `record_fmt(&mon, ms, f)`.

### 9) Linux'ta `parec` warmup

İlk 80 ms drop edilir. **`record_raw`** da aynı (aynı legacy path).

### 10) `measure_all` 30 ms sleep

Her `measure_level` arasında **30 ms** bekle. PulseAudio/PipeWire'ın önceki `parec` device'ı bırakması için. **Back-to-back calls** bunsuz intermittent fail eder.

### 11) VAD `energy_threshold` çok yüksek

Default `0.01f` (yani -40 dB). Çok gürültülü ortamda **arttırın** (0.05 = -26 dB). Sessiz odada **azaltın** (0.005 = -46 dB).

### 12) VAD `zcr_max` yanlış

Default `0.3f`. Sesli harfler ~0.05, sessiz ~0.4. **Müzik** için arttırın (0.5+), **konuşma** için 0.25 yeterli.

### 13) AGC gain oscillation

`attack_factor = 0.3` çok hızlı → gain pompalama. **Yavaşlatmak için** `attack_factor = 0.1`, `release_factor = 0.02`.

### 14) `noise_gate::coef_for` `ms <= 0.1`

Anlık (coefficient = 1.0). `attack_ms = 0` istiyorsan bu davranış.

### 15) Pitch detection sessiz sinyal

`energy < 1e-9` → 0 döner. **`detect` öncesi** `energy` veya `rms` kontrolü yap.

### 16) Pitch detection min/max lag

`min_lag = sample_rate / max_hz`, `max_lag = sample_rate / min_hz`. **Config'i doğru seç:**
- **Erkek konuşma:** 80-200 Hz → `min_hz = 80`, `max_hz = 200`
- **Kadın konuşma:** 150-400 Hz
- **Çocuk:** 200-500 Hz
- **Müzik:** 40-1000 Hz

### 17) Pitch detection harmonik karışıklığı

Autocorrelation **ilk peak** (threshold üstünde) alır → temel frekans. **Yanlış** harmonik peak'e takılırsa threshold düşür.

### 18) `silence_detector` sample_rate

`sample_rate` **doğru olmalı** yoksa `silent_samples → ms` yanlış. Block'lar rate ile uyumlu olmalı.

### 19) Voice recorder channels == 1

`feed` mono alır, `capture_interleaved` `channels` ile çarpar. **Çoklu kanal için `NotSupported`.** Mono capture loop'u yap.

### 20) Voice recorder pre-roll cap

`pre_ring_capacity = min(pre_roll_ms * rate / 1000, 4800000)` — yaklaşık 100 sn @ 48 kHz. Daha fazlası gerekirse `pre_roll_ms` azalt.

### 21) Voice recorder `flush_pre_roll` 16 MB limit

`frames * ch > 16 MB` → 0 döner. Pre-roll çok büyükse kayıt kaybolur.

### 22) Beamforming 3+ kanal

`mic_array::beamform` sadece 1 veya 2 kanal destekler. 3+ → `NotSupported`.

### 23) `estimate_delay` `max_delay` sınırı

`max_delay >= n` ise `n - 1`'e clamp edilir. Çok büyük `max_delay` gereksiz slow (O(n * max_delay)).

### 24) `recorder::open_file` header boyutu

`make_wav_header` **format-spesifik** boyut döner:
- PCM i16 ≤2ch → 44
- float/alaw/ulaw → 56 (fact chunk)
- PCM 3+ch → 68 (extensible)
- float 3+ch → 80

**Hard-coded 44 kullanmayın** — `hdr_len_` saklayın.

### 25) `recorder::finish` header rewrite

Header **yeniden yazılır** — `data_bytes` + `fact_frames` + `riff_size` güncellenir. `finish` çağrılmadan dosya **bozuk** (0 boyut header).

### 26) `record_wav` limit

`record_raw` çağrısı yapar → **60 saniye** limit. `seconds > 60` bile `record_raw` `InvalidArgument` döner.

### 27) `_record_raw_legacy` device check

`!device->is_input` → `InvalidArgument`. Monitor source input mu? `list_sources` doğru işaretler.

### 28) Android AAudio min API

Android **8.0 (API 26)** AAudio. Daha önce `NotSupported`. **Fallback:** OpenSL ES (bu modülde yok).

### 29) JNI env modu

`KNST_MIC_USE_PRINTER_JNI` veya `KNST_MIC_HAS_EXTERNAL_ENV` yoksa env `nullptr`. Loopback/PlaybackCapture **runtime** `BadState` döner. **Test programları** için sorun değil (link başarılı).

### 30) `KNST_LOG_WARN` fallback

Header **self-contained** kalabilmek için fallback macro var. `knst_printer.hpp` önceden include edildiyse gerçek macro kullanılır.

---

## 📊 17) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| `record_raw` (i16, 1s) | ~1s + I/O | parec/waveIn/AAudio |
| `record_fmt` | ~1s + I/O | Backend seçimi |
| `measure_level` | ~500 ms | i16 16k mono |
| `measure_all` | N × (500 + 30) ms | Sleep zorunlu |
| `enumerate_microphones` | O(devices) | `list_sources` çağrısı |
| `default_microphone` | O(devices) | |
| **VAD `feed`** | O(n) | RMS + ZCR |
| **AGC `process`** | O(n) | Peak + gain |
| **Noise gate `process`** | O(n) | Envelope |
| **Pitch `detect`** | O(n × max_lag) | Autocorrelation |
| **Silence `feed`** | O(n) | RMS |
| **Beamforming** | O(n × max_delay) | Cross-correlation |
| **Format conversion** | O(n) | Direct |

**Optimizasyon:**
- **Uzun kayıt:** `recorder` + `pump_from(stream)` — vector resize amortized
- **Real-time processing:** `knst_aud::stream` + VAD/AGC/gate blok blok
- **Pitch detection:** `n = 512` (32 ms @ 16 kHz) yeterli
- **Beamforming:** `max_delay = 16` (1 ms) genelde yeterli
- **`measure_all`:** Paralel yapmak cazip ama **30 ms sleep kaldırma** — PipeWire race

---

## ✅ 18) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Uzun kayıt için recorder
knst_mic::recorder rec;
knst_aud::wav::format fmt;
fmt.rate = 48000; fmt.channels = 2; fmt.sample = knst_aud::sample_t::i16;

auto r = knst_mic::recorder::open_file(u"long.wav", fmt);
// ... capture_interleaved defalarca ...
r.value.finish();
```

```cpp
// 2. VAD state machine — blok blok besle
knst_mic::voice_detector vad;
while (running) {
    float block[320];
    stream.read_interleaved(block, 320);  // 20 ms @ 16 kHz
    bool active = vad.feed(block, 320);
    if (vad.last_transition() == knst_mic::voice_detector::edge::start) {
        std::cout << "Konuşma başladı\n";
    }
}
```

```cpp
// 3. Loopback — platform-spesifik hazırlık
#if defined(_WIN32)
    // device = nullptr → default render, loopback=true
    knst_mic::capture_format f;
    f.loopback = true;
    f.sample_rate = 48000;
    f.channels = 2;
    auto r = knst_mic::record_fmt(nullptr, 5000, f);
#elif defined(KNST_USING_PLATFORM_ANDROID)
    // App: onActivityResult → mic_playback::set_permission(resultCode, data);
    auto mon = knst_mic::monitor_source();   // veya nullptr
    knst_mic::capture_format f;
    f.loopback = true;
    auto r = knst_mic::record_fmt(nullptr, 5000, f);
#elif defined(KNST_USING_PLATFORM_LINUX)
    auto mon = knst_mic::monitor_source();
    if (!mon) return;   // monitor yok
    knst_mic::capture_format f;
    f.loopback = true;
    auto r = knst_mic::record_fmt(&mon.value, 5000, f);
#endif
```

```cpp
// 4. Voice-triggered recording
knst_mic::voice_recorder::config cfg;
cfg.format.rate = 16000;
cfg.format.channels = 1;
cfg.format.sample = knst_aud::sample_t::i16;
cfg.max_silence_ms = 800;
cfg.pre_roll_ms = 300;

knst_mic::voice_recorder vr(cfg);
vr.start(u"speech.wav");

knst_aud::stream stream = /* ... */;
while (running) {
    float block[320];
    stream.read_interleaved(block, 320);
    vr.feed(block, 320);   // sadece konuşma olduğunda yazar
}

vr.stop();   // WAV header finalize
```

```cpp
// 5. SSRF değil ama... gerçek zamanlı pipeline
knst_mic::agc agc;
knst_mic::noise_gate gate;
knst_mic::voice_detector vad;

float block[480];   // 10 ms @ 48 kHz
while (stream.read_interleaved(block, 480)) {
    gate.process(block, 480);
    agc.process(block, 480);
    bool speaking = vad.feed(block, 480);
    // ... process ...
}
```

### ❌ Yapma

```cpp
// 1. 60 sn üstü record_raw
knst_mic::record_raw(nullptr, 120000);   // ❌ InvalidArgument
// ✅ recorder sınıfı kullan

// 2. Linux'ta loopback cihazsız
#if defined(KNST_USING_PLATFORM_LINUX)
knst_mic::capture_format f; f.loopback = true;
knst_mic::record_fmt(nullptr, 5000, f);   // ❌ NotSupported
// ✅ monitor_source() al, sonra record_fmt(&mon, ...)
#endif
```

```cpp
// 3. Android PlaybackCapture permission olmadan
// ❌ App onActivityResult'ta set_permission çağırmadan loopback
// ✅ Önce set_permission(resultCode, data), sonra record_fmt(loopback=true)

// 4. WASAPI mix format farz etme
// ❌ Bazı exotic driver'lar 16-bit PCM döner → NotSupported beklenmeli
// ✅ Hata işle: if (err == NotSupported) { /* waveIn ile fallback */ }

// 5. Pitch detection sessiz blokta
float pitch = detector.detect(silent_block, 512);   // ❌ 0 döner
// ✅ Önce RMS/energy kontrolü

// 6. VAD çok düşük threshold
vad_config cfg; cfg.energy_threshold = 0.0001f;   // ❌ Her şey "aktif"
// ✅ Ortam gürültüsüne göre ayarla

// 7. AGC `attack_factor` çok hızlı
agc_config cfg; cfg.attack_factor = 0.9f;   // ❌ Pompalama/gain oscillation
// ✅ 0.1-0.3 arası

// 8. Voice recorder `channels != 1`
voice_recorder::config cfg;
cfg.format.channels = 2;   // ❌ NotSupported
// ✅ Mono capture, sonra istersen downmix

// 9. Beamforming 3+ kanal
mic_array arr;
arr.beamform(interleaved_4ch, frames, 4);   // ❌ NotSupported
// ✅ 2-kolona indir veya kendi implementasyonun

// 10. `recorder::finish` unutma
auto r = recorder::open_file(u"x.wav", fmt);
r.value.capture_interleaved(...);
// ❌ finish() çağrılmadı → header yanlış
// ✅ r.value.finish() veya destructor abort() — farklı davranış
```

---

## 🔥 19) Gerçek Kullanım Örnekleri

### Örnek 1: Ses kaydetme CLI

```cpp
int main() {
    auto mon = knst_mic::default_microphone();
    if (!mon) { std::cerr << "Mic yok\n"; return 1; }

    auto r = knst_mic::record_wav(&mon.value, u"test.wav", 5, 48000, 2);
    if (!r) std::cerr << "Kayıt başarısız\n";
    else    std::cout << "test.wav yazıldı\n";
}
```

### Örnek 2: Mikrofon test (seviye ölçümü)

```cpp
void test_all_mics() {
    auto mics = knst_mic::enumerate_microphones();
    if (!mics) return;

    for (auto& m : mics.value) {
        knst_audio_device d;
        d.name = m.name;
        d.is_input = true;

        auto lvl = knst_mic::measure_level(&d, 500);
        if (lvl) {
            std::cout << m.name << ": "
                      << lvl.value.peak_db << " dB peak, "
                      << lvl.value.rms_db  << " dB RMS\n";
        } else {
            std::cout << m.name << ": ölçülemedi\n";
        }
    }
}
```

### Örnek 3: VAD-gated voice recorder

```cpp
void record_speech(const knst_c16string& path) {
    knst_mic::voice_recorder::config cfg;
    cfg.format.rate = 16000;
    cfg.format.channels = 1;
    cfg.format.sample = knst_aud::sample_t::i16;
    cfg.vad.energy_threshold = 0.02f;
    cfg.max_silence_ms = 1000;
    cfg.pre_roll_ms = 300;

    knst_mic::voice_recorder vr(cfg);
    auto r = vr.start(path);
    if (!r) { std::cerr << "start fail\n"; return; }

    knst_aud::stream stream;
    if (!stream.open(16000, 1)) return;

    for (int i = 0; i < 3000; ++i) {   // 60 sn @ 20 ms blok
        float block[320];
        auto read = stream.read_interleaved(block, 320);
        if (!read || read.value == 0) break;
        vr.feed(block, 320);
    }

    vr.stop();
}
```

### Örnek 4: Real-time voice activity monitor

```cpp
void monitor_activity() {
    knst_aud::stream stream;
    if (!stream.open(16000, 1)) return;

    knst_mic::voice_detector vad;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        float block[320];
        auto r = stream.read_interleaved(block, 320);
        if (!r || r.value == 0) break;

        vad.feed(block, 320);

        if (vad.last_transition() == knst_mic::voice_detector::edge::start) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::cout << "[" << ms << " ms] Konuşma başladı\n";
        } else if (vad.last_transition() == knst_mic::voice_detector::edge::end) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::cout << "[" << ms << " ms] Konuşma bitti\n";
        }
    }
}
```

### Örnek 5: Pitch (nota) detection

```cpp
void detect_pitch_stream() {
    knst_aud::stream stream;
    if (!stream.open(16000, 1)) return;

    knst_mic::pitch_config cfg;
    cfg.min_hz = 80.0f;
    cfg.max_hz = 800.0f;
    cfg.sample_rate = 16000;
    knst_mic::pitch_detector pd(cfg);

    for (int i = 0; i < 500; ++i) {
        float block[512];
        auto r = stream.read_interleaved(block, 512);
        if (!r || r.value < 256) break;

        float hz = pd.detect(block, r.value);
        if (hz > 0) {
            std::cout << "Pitch: " << hz << " Hz\n";
        }
    }
}
```

### Örnek 6: Noise gate + AGC pipeline

```cpp
void preprocess_stream() {
    knst_aud::stream stream;
    stream.open(48000, 1);

    knst_mic::noise_gate gate;
    knst_mic::agc agc;

    float block[480];   // 10 ms @ 48 kHz
    while (true) {
        auto r = stream.read_interleaved(block, 480);
        if (!r || r.value == 0) break;

        gate.process(block, 480);
        agc.process(block, 480);

        // İşlenmiş blok — kayıt, network, vb.
    }
}
```

### Örnek 7: Format conversion

```cpp
void convert_and_save() {
    auto raw = knst_mic::record_raw(nullptr, 1000, 16000, 1);
    if (!raw) return;

    // i16 bytes → float
    knst_vector<float> samples = knst_mic::fmt::s16_bytes_to_float(raw.value);

    // İşle (AGC, gate, vb.)
    knst_mic::agc agc;
    agc.process(samples.data(), samples.size());

    // Float → i16 bytes
    knst_byte_string processed = knst_mic::fmt::float_to_s16_bytes(
        samples.data(), samples.size());

    // WAV yaz
    unsigned char hdr[44];
    knst_aud::write_wav_header(hdr, processed.length(), 16000, 1, 16);
    knst_byte_string full;
    for (int i = 0; i < 44; ++i) full.push_back(hdr[i]);
    for (uint32_t i = 0; i < processed.length(); ++i) full.push_back(processed[i]);
    knst_file::write_file_data(u"processed.wav", full.data(), full.length());
}
```

### Örnek 8: Loopback (platform-spesifik)

```cpp
bool capture_system_audio(const knst_c16string& path, uint32_t secs) {
    knst_mic::capture_format f;
    f.sample_rate = 48000;
    f.channels = 2;
    f.sample = knst_mic::capture_format::sample_t::i16;
    f.loopback = true;

#if defined(_WIN32)
    auto r = knst_mic::record_fmt(nullptr, secs * 1000, f);
    if (!r) return false;
    // WAV yaz...
    return true;

#elif defined(KNST_USING_PLATFORM_ANDROID)
    if (!knst_mic::mic_playback::has_permission()) return false;
    auto r = knst_mic::record_fmt(nullptr, secs * 1000, f);
    if (!r) return false;
    // WAV yaz...
    return true;

#elif defined(KNST_USING_PLATFORM_LINUX)
    auto mon = knst_mic::monitor_source();
    if (!mon) return false;
    auto r = knst_mic::record_fmt(&mon.value, secs * 1000, f);
    if (!r) return false;
    // WAV yaz...
    return true;
#else
    return false;
#endif
}
```

---

## 📌 20) API Kontrat Özeti

```cpp
namespace knst_mic {

using knst_dev::status;
using knst_dev::result;

struct capture_format {
    enum class sample_t : uint8_t { i16 = 0, i24, i32, f32 };
    uint32_t  sample_rate = 48000;
    uint32_t  channels    = 2;
    sample_t  sample      = sample_t::i16;
    bool      loopback    = false;
    int32_t   device_id   = 0;
};

result<knst_byte_string> record_fmt(
    const knst_audio_device*, uint32_t ms, const capture_format&) noexcept;

// Platform backends
namespace mic_aaudio {
    result<knst_byte_string> capture_s16le(...) noexcept;
    result<knst_byte_string> capture_f32(...) noexcept;
    result<knst_byte_string> capture_ext(
        const capture_format&, uint32_t, const knst_audio_device*) noexcept;
}
namespace mic_playback {   // Android
    void set_permission(int, jobject);
    bool has_permission();
    result<knst_byte_string> capture(const capture_format&, uint32_t) noexcept;
}
namespace mic_wavein {     // Windows
    result<knst_byte_string> capture_s16le(...) noexcept;
}
namespace mic_wasapi {     // Windows
    result<knst_byte_string> capture(
        const capture_format&, uint32_t, const knst_audio_device*) noexcept;
}

// Enumerasyon
struct mic_ref {
    knst_c16string name;
    knst_c16string description;
    uint32_t       index = 0;
    bool           is_default = false;
    bool           is_monitor = false;
};

bool is_monitor(const knst_c16string&) noexcept;
result<knst_vector<mic_ref>> enumerate_microphones(bool = false) noexcept;
result<knst_audio_device> default_microphone() noexcept;
result<knst_audio_device> monitor_source() noexcept;

// Recorder
class recorder {
public:
    enum class mode_t : uint8_t { none, file, memory };
    static result<recorder> open_file(const knst_c16string&,
                                       const knst_aud::wav::format&) noexcept;
    static result<recorder> open_memory(const knst_aud::wav::format&) noexcept;
    result<uint32_t> capture_interleaved(const float*, uint32_t) noexcept;
    result<uint32_t> pump_from(knst_aud::stream&, uint32_t) noexcept;
    result<void>     finish() noexcept;
    void             abort() noexcept;
    uint32_t total_frames() const noexcept;
    double   recorded_seconds() const noexcept;
    const knst_aud::wav::format& fmt() const noexcept;
    const knst_vector<float>& samples() const noexcept;
    mode_t mode() const noexcept;
    bool   is_open() const noexcept;
};

// Kolay kayıt
result<knst_byte_string> record_raw(
    const knst_audio_device*, uint32_t ms,
    uint32_t rate = 16000, uint32_t channels = 1) noexcept;

result<void> record_wav(
    const knst_audio_device*, const knst_c16string&, uint32_t seconds = 5,
    uint32_t rate = 48000, uint32_t channels = 1) noexcept;

// Seviye
result<knst_aud::level_info> measure_level(
    const knst_audio_device*, uint32_t ms = 500) noexcept;
result<knst_vector<knst_aud::level_info>> measure_all(uint32_t ms = 300) noexcept;

// VAD
struct vad_config { /* ... */ };
class voice_detector {
public:
    explicit voice_detector(const vad_config& = {}) noexcept;
    bool feed(const float*, uint32_t) noexcept;
    enum class edge : uint8_t { none, start, end };
    edge last_transition() const noexcept;
    bool is_active() const noexcept;
};

// AGC
struct agc_config { /* ... */ };
class agc {
public:
    explicit agc(const agc_config& = {}) noexcept;
    float process(float*, uint32_t) noexcept;
    float current_gain() const noexcept;
    void  set_gain(float) noexcept;
    void  reset() noexcept;
};

// Gate
struct gate_config { /* ... */ };
class noise_gate {
public:
    explicit noise_gate(const gate_config& = {}) noexcept;
    void process(float*, uint32_t) noexcept;
    float envelope() const noexcept;
};

// Pitch
struct pitch_config { /* ... */ };
class pitch_detector {
public:
    explicit pitch_detector(const pitch_config& = {}) noexcept;
    float detect(const float*, uint32_t) noexcept;
};

// Silence
struct silence_config { /* ... */ };
class silence_detector {
public:
    explicit silence_detector(const silence_config& = {}) noexcept;
    bool feed(const float*, uint32_t) noexcept;
    void reset() noexcept;
};

// Voice recorder
class voice_recorder {
public:
    struct config { /* ... */ };
    explicit voice_recorder(const config&) noexcept;
    result<void> start(const knst_c16string&) noexcept;
    uint32_t feed(const float*, uint32_t) noexcept;
    result<void> stop() noexcept;
    bool is_recording() const noexcept;
};

// Beamforming
int32_t estimate_delay(
    const float*, const float*, uint32_t, int32_t) noexcept;
knst_vector<float> beamform_2mic(
    const float*, const float*, uint32_t, int32_t) noexcept;
class mic_array {
public:
    result<knst_vector<float>> beamform(
        const float*, uint32_t, uint32_t) noexcept;
};

// Format helpers
namespace fmt {
    void s16_to_float(const int16_t*, float*, uint32_t) noexcept;
    void float_to_s16(const float*, int16_t*, uint32_t) noexcept;
    knst_vector<float> s16_bytes_to_float(const knst_byte_string&) noexcept;
    knst_byte_string float_to_s16_bytes(const float*, uint32_t) noexcept;
}

// JSON
#if defined(KNST_JSON_HPP)
knst_json mic_refs_to_json(const knst_vector<mic_ref>&) noexcept;
#endif

} // namespace knst_mic
```

---

## 🎯 Size Getireceği Kazanç

`knst_microphone` şu durumlarda ciddi avantaj sağlar:

- ✅ **Cross-platform kayıt** — Windows / Linux / Android aynı API
- ✅ **Multi-format** — i16, i24, i32, f32 (WASAPI + parec + AAudio)
- ✅ **Loopback** — Windows WASAPI, Android PlaybackCapture
- ✅ **Adlandırılmış cihaz seçimi** — Windows endpoint id
- ✅ **Voice Activity Detection** — enerji + ZCR + hangover state machine
- ✅ **AGC** — attack/release peak follower
- ✅ **Noise gate** — threshold + envelope
- ✅ **Pitch detection** — autocorrelation + parabolic interpolation
- ✅ **Silence detection** — RMS threshold + duration
- ✅ **Voice-triggered recorder** — pre-roll + VAD-gated WAV
- ✅ **Beamforming** — 2-mic delay-and-sum
- ✅ **Seviye ölçümü** — peak/rms/dB/clip
- ✅ **Format helpers** — int16 ↔ float, bytes ↔ float
- ✅ **Recorder class** — file + memory, header-aware
- ✅ **Zero thread** — synchronous, tam kontrol sende
- ✅ **KernelNucleusT entegrasyonu** — `knst_audio`, `knst_file`, `knst_json`

---