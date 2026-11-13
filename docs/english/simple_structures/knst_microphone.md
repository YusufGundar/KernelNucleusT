# knst_microphone — Microphone Capture and Voice Processing

Hey! 👋 The `knst_microphone` namespace is KernelNucleusT's **microphone capture and voice processing** module. Microphone enumeration, PCM/WAV capture, real-time stream, level measurement, VAD (Voice Activity Detection), AGC, noise gate, pitch detection, silence detection, voice-triggered recorder, loopback capture, and beamforming — all in one header.

**In short:** Built on top of `knst_audio.hpp`. **Synchronous, no threads, every call returns `result<T>`.** Speech processing state machines (VAD, AGC, gate) are **step functions** — you feed them one block at a time, they update internal state and return a decision. This lets you integrate them into any loop, with or without threading.

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

### Platform matrix

| Feature | Windows | Linux | Android |
|---|---|---|---|
| **i16 capture (raw)** | ✅ waveIn (default) / WASAPI | ✅ `parec` | ✅ AAudio |
| **i24 / i32 / f32 capture** | ✅ WASAPI | ✅ `parec` | ✅ AAudio (f32) |
| **Default mic** | ✅ | ✅ | ✅ |
| **Named device selection** | ✅ WASAPI (endpoint id) | ✅ `parec --device` | ❌ AAudio default only |
| **Loopback (system audio)** | ✅ WASAPI | ⚠️ needs monitor source | ✅ MediaProjection (JNI) |
| **Mic enumeration** | ✅ `knst_aud::audio::list_sources` | ✅ | ✅ |
| **Monitor detection** | ✅ | ✅ | ✅ |
| **WAV recording** | ✅ | ✅ | ✅ |
| **Stream (real-time)** | ✅ | ✅ | ✅ |
| **Level measurement (peak/rms/dB/clip)** | ✅ | ✅ | ✅ |
| **VAD (voice detection)** | ✅ | ✅ | ✅ |
| **AGC** | ✅ | ✅ | ✅ |
| **Noise gate** | ✅ | ✅ | ✅ |
| **Pitch detection** | ✅ | ✅ | ✅ |
| **Silence detection** | ✅ | ✅ | ✅ |
| **Voice-triggered recorder** | ✅ | ✅ | ✅ |
| **Beamforming (2 mic)** | ✅ | ✅ | ✅ |
| **Format conversion** | ✅ | ✅ | ✅ |
| **JSON bridge** | ✅ (KNST_JSON_HPP) | ✅ | ✅ |
| **Capture duration limit** | **≤ 60 seconds** | ≤ 60 s | ≤ 60 s |

**Critical notes:**
- **Windows:** WaveIn is fast for the default device (no COM). Named devices or loopback/multi-format use **WASAPI**.
- **Linux:** Requires `parec` (PulseAudio/PipeWire). **For loopback you must select a monitor source yourself** — `parec` has no built-in "default monitor."
- **Android:** PlaybackCapture requires **the app to obtain MediaProjection permission**. If `mic_playback::set_permission()` isn't called, loopback returns `BadState`.
- **AAudio can't select named devices** — only default input or `device_id` (Android `AudioDeviceInfo.getId()`).
- **`record_raw` / `record_fmt` max 60 seconds.** For longer recording use the `recorder` class or `knst_aud::stream`.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Multi-format capture** | i16, i24, i32, f32 — platform backends optimized |
| **Loopback** | Windows WASAPI, Android PlaybackCapture |
| **VAD** | Energy + ZCR + hangover state machine |
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

## 🚀 Quick Start

### Simple recording (i16, default mic)

```cpp
#include "KernelNucleusT.hpp"

// 2 seconds at 16 kHz mono
auto raw = knst_mic::record_raw(nullptr, 2000, 16000, 1);
if (raw) {
    std::cout << "Captured: " << raw.value.length() << " bytes\n";
}
```

### WAV file

```cpp
auto r = knst_mic::record_wav(nullptr, u"output.wav", 3, 48000, 2);
if (!r) {
    std::cerr << "Recording failed\n";
}
```

### Microphone list

```cpp
auto mics = knst_mic::enumerate_microphones();
for (auto& m : mics.value) {
    std::cout << "[" << m.index << "] " << m.name
              << (m.is_default ? " (default)" : "")
              << (m.is_monitor ? " (monitor)" : "")
              << "\n";
}
```

### Level measurement

```cpp
auto lvl = knst_mic::measure_level(nullptr, 500);
if (lvl) {
    std::cout << "Peak: " << lvl.value.peak_db << " dB\n";
    std::cout << "RMS : " << lvl.value.rms_db  << " dB\n";
    std::cout << "Clip: " << lvl.value.clipped << "\n";
}
```

---

## 📋 General API

| Category | Function / Class |
|---|---|
| **Format** | `capture_format` |
| **Record** | `record_raw`, `record_fmt`, `record_wav` |
| **Recorder** | `recorder` class |
| **Enumeration** | `enumerate_microphones`, `default_microphone`, `monitor_source`, `is_monitor` |
| **Level** | `measure_level`, `measure_all` |
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

## 🎯 1) `capture_format` — Capture Format

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

| Field | Meaning |
|---|---|
| `sample_rate` | Sample rate (Hz). **8000..192000** valid. |
| `channels` | Channel count. **1..8** valid. |
| `sample` | `i16`, `i24`, `i32`, `f32` |
| `loopback` | `true` → capture system audio output |
| `device_id` | Android `AudioDeviceInfo.getId()` — AAudio only |

**Platform support:**
- **i16:** Works on all platforms (waveIn/AAudio/parec)
- **i24/i32/f32:** WASAPI (Windows), AAudio f32 (Android), `parec` (Linux)
- **Loopback:** WASAPI (Windows), PlaybackCapture (Android), monitor source (Linux — manual)
- **device_id:** Android AAudio only, set if `>0`

---

## 🎤 2) Recording Functions

### 2.1) `record_raw` — Simple (i16)

```cpp
result<knst_byte_string> record_raw(
    const knst_audio_device* device,
    uint32_t ms,                    // 50..60000
    uint32_t rate = 16000,
    uint32_t channels = 1) noexcept;
```

**Constraints:**
- `ms` — **50..60000** (1 minute)
- `rate` — 8000..192000
- `channels` — 1..8
- `device->is_input` must be true (if device given)

**Returns:** i16 interleaved PCM bytes. **Zero-padded** if short.

**Platform behavior:**
- **Windows + no device:** waveIn (fast)
- **Windows + device:** WASAPI (named device)
- **Linux:** `parec` (via `timeout` command)
- **Android:** AAudio

### 2.2) `record_fmt` — Extended

```cpp
result<knst_byte_string> record_fmt(
    const knst_audio_device* device,
    uint32_t ms,                    // 50..60000
    const capture_format& f) noexcept;
```

**Constraints:**
- Same `ms` limit (50..60000)
- Same `rate` / `channels` limits
- **Linux:** `f.loopback && !device` → `NotSupported` (parec has no default monitor)

**`f.sample == i16 && !f.loopback` fast path:** `_record_raw_legacy` — cheap default.
**Otherwise:** platform-specific extended path.

### 2.3) `record_wav` — WAV file

```cpp
result<void> record_wav(
    const knst_audio_device* device,
    const knst_c16string& path,
    uint32_t seconds = 5,           // 1..3600 (uint32_t seconds, not ms)
    uint32_t rate = 48000,
    uint32_t channels = 1) noexcept;
```

**Internal call:** `record_raw(device, seconds * 1000u, rate, channels)` — so **max 60 seconds** (record_raw limit). For longer, use the `recorder` class.

---

## 💾 3) `recorder` — File + Memory Recorder

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

**`open_file`** — Writes a WAV header (saves correct size in `hdr_len_`). On `finish()`, the **header is rewritten** — total size + fact frames updated.

**`capture_interleaved`** — Takes float input, encodes WAV (`knst_aud::wav::encode_memory`), appends payload to the file.

**Why does `hdr_len_` matter?** WAV header size varies by format:
- **PCM i16, ≤2 ch:** 44 bytes
- **float / alaw / ulaw:** +12 = 56 bytes (`fact` chunk)
- **PCM 3+ ch (extensible):** +24 = 68 bytes
- **float 3+ ch:** +24 +12 = 80 bytes

`make_wav_header` returns the correct size, `hdr_len_` stores it. `capture_interleaved` **skips the header bytes**, appends only the payload. Otherwise PCM during capture would begin mid-header (garbage).

### 3.2) Memory mode

`capture_interleaved` appends floats to the `samples_` vector.

**`finish()`** — no-op in memory mode (`mode_ = none`).

### 3.3) `pump_from`

```cpp
result<uint32_t> pump_from(knst_aud::stream& source, uint32_t frames) noexcept;
```

Reads N frames from `knst_aud::stream`, writes to the recorder. **Channel count must match** — otherwise `InvalidArgument`.

### 3.4) `finish` vs `abort`

- **`finish()`** — in file mode the header is rewritten (correct size). Mode becomes `none`.
- **`abort()`** — destructive. No header written, samples cleared. Leftover file has a garbage header.

---

## 🔍 4) Enumeration

### 4.1) `mic_ref` — Microphone reference

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

**What it does?** Searches for `.monitor` or `Monitor` substring in the name. Monitor sources (loopback) aren't real microphones — they're filtered out of the enumerated list.

### 4.3) `enumerate_microphones`

```cpp
result<knst_vector<mic_ref>> enumerate_microphones(bool include_monitors = false) noexcept;
```

Filters input devices from `knst_aud::audio::list_sources()`, skips monitors by default.

### 4.4) `default_microphone`

```cpp
result<knst_audio_device> default_microphone() noexcept;
```

Returns default + non-monitor. If not present, **first non-monitor**. If none → `NotFound`.

### 4.5) `monitor_source`

```cpp
result<knst_audio_device> monitor_source() noexcept;
```

Returns the first monitor source. Required for loopback capture (Linux).

---

## 📊 5) Level Measurement

### 5.1) `measure_level`

```cpp
result<knst_aud::level_info> measure_level(
    const knst_audio_device* device,
    uint32_t ms = 500) noexcept;    // 100..5000
```

**Internal call:** `record_raw(device, ms, 16000, 1)` — i16 mono 16 kHz.

**`level_info` fields:**
- `peak` — 0..1 (absolute peak)
- `rms` — 0..1 (RMS)
- `peak_db` — `20*log10(peak)` or -144 if silent
- `rms_db` — `20*log10(rms)` or -144 if silent
- `samples` — sample count
- `clipped` — number of clipped samples (i16 -32768 or 32767)

### 5.2) `measure_all`

```cpp
result<knst_vector<knst_aud::level_info>> measure_all(uint32_t ms = 300) noexcept;
```

**For each source** calls `measure_level`. **Important detail:** **30 ms sleep** between captures — for PulseAudio/PipeWire to release the previous `parec` device. Without it, back-to-back `measure_all` intermittently fails.

**Fail behavior:** If a level can't be measured, pushes a `peak_db = -144`, `rms_db = -144` sentinel. This distinguishes "silent" from "couldn't measure" (default `level_info()` shows 0 dB, which is misleading).

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

| Field | Meaning |
|---|---|
| `frame_ms` | Frame length (informational) |
| `energy_threshold` | RMS threshold. Higher = harder to detect speech |
| `zcr_max` | Zero-crossing rate upper bound. Voiced sounds ~0.05, unvoiced ~0.4 |
| `hangover_frames` | Keep "active" N more frames after silence |
| `trigger_frames` | N consecutive active frames → start trigger |
| `sample_rate` | Informational only (for RMS computation) |

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

**What does `feed` do?**
1. **RMS energy** computation
2. **Zero-crossing rate** computation
3. `rms >= energy_threshold && zcr <= zcr_max` → frame active
4. `active_run_` counter + `hangover_left_` timer
5. **Trigger decision:** `active_run >= trigger_frames || hangover_left > 0`
6. If `was_active_` changed → **edge** set
7. **Return:** currently active?

**State machine:**
```
idle ──(trigger)──> active
active ──(hangover ends)──> idle
```

**`last_transition()`** — `start` (silence → speech), `end` (speech → silence), `none` (no change).

---

## 🎚️ 7) AGC — Automatic Gain Control

### 7.1) `agc_config`

```cpp
struct agc_config {
    float target_peak    = 0.5f;
    float min_gain       = 0.1f;
    float max_gain       = 10.0f;
    float attack_factor  = 0.3f;    // fast (when signal is loud)
    float release_factor = 0.05f;   // slow (when signal is quiet)
    float noise_floor    = 0.001f;  // don't amplify below this
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

**What does `process` do?**
1. Compute block peak
2. `peak < noise_floor` → don't change gain (avoid amplifying silence), **apply gain**
3. `want = target_peak / peak` → clamp to min/max
4. Choose attack (want < gain) or release (want > gain) coefficient
5. Exponential smoothing: `gain = gain * (1-factor) + want * factor`
6. Apply gain to block, clamp to ±1

**Returns:** Applied gain.

---

## 🚪 8) Noise Gate

### 8.1) `gate_config`

```cpp
struct gate_config {
    float threshold_db = -45.0f;    // close below
    float attack_ms    = 5.0f;      // opening (fast)
    float release_ms   = 100.0f;    // closing (slow)
    float floor_db     = -80.0f;    // gain when fully closed
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

**What it does?**
1. Envelope follower — attack/release coefficients
2. `env >= thresh` → gain = 1.0 (open)
3. `env <= thresh * 0.5` → gain = floor (closed)
4. In between → linear ramp floor ↔ 1.0
5. Apply gain to block

**Coefficient formula:** `coef = 1 - exp(-1 / (ms * sample_rate / 1000))`. If `ms <= 0.1` → `1.0` (instant).

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

**What does `detect` do?** Autocorrelation + parabolic interpolation:
1. Min/max lag = `sample_rate / max_hz` and `sample_rate / min_hz`
2. **Energy check** — `sum < 1e-9` → return 0
3. Normalized autocorrelation — for each lag `sum(block[i] * block[i+lag]) / energy`
4. Find global max → threshold = 0.9 * global_max
5. **First peak** (above threshold) → best_lag
6. **Parabolic interpolation** — `refined = best_lag + 0.5 * (a - c) / (a - 2b + c)`
7. **Pitch = sample_rate / refined**

**Returns:** Hz or 0 (no pitch / too quiet).

**Why the first peak?** Autocorrelation peaks at the pitch period but also at harmonics. The first peak gives the **fundamental frequency**.

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

**`feed`:** Computes RMS, converts to dB. If `rms_db < threshold_db`, accumulates `silent_samples_`, otherwise resets to 0. Returns `true` once `min_silence_ms` is exceeded.

---

## 📼 11) Voice-Triggered Recorder

### 11.1) `voice_recorder::config`

```cpp
struct config {
    vad_config vad;
    knst_aud::wav::format format;
    uint32_t min_speech_ms  = 300;    // don't record short speech
    uint32_t max_silence_ms = 1000;   // stop after this much silence
    uint32_t pre_roll_ms    = 200;    // keep N ms before VAD
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

**`start`:** `format.channels == 1` **mandatory** — `feed` receives mono blocks, `capture_interleaved` multiplies by `channels`. Otherwise OOB.

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
- Cap: **4.8M samples** (~100 s @ 48 kHz)
- `add_to_pre_roll` — circular buffer, valid count increments
- **`flush_pre_roll`** — on VAD trigger **reorders chronologically** (from write_pos), feeds to `capture_interleaved`

**Critical:** Pre-roll capture contains **pre-VAD** audio → the beginning of speech isn't lost.

**`feed` return:** bytes written (0 = nothing written).

**`stop`:** `recorder::finish()` → WAV header rewritten.

---

## 🎤 12) Beamforming (2 Microphones)

### 12.1) `estimate_delay`

```cpp
int32_t estimate_delay(
    const float* a, const float* b,
    uint32_t n, int32_t max_delay) noexcept;
```

**What it does?** Estimates the **time delay** between two channels via cross-correlation:
1. `d = -max_delay..+max_delay` cross-correlation
2. `sum(a[i] * b[i+d]) / count` — normalized
3. **Highest** correlation's delay returned

**Use:** In a 2-mic array, estimate which side the sound comes from.

### 12.2) `beamform_2mic`

```cpp
knst_vector<float> beamform_2mic(
    const float* ch0, const float* ch1,
    uint32_t n, int32_t delay) noexcept;
```

**What it does?** Delay-and-sum:
```
out[i] = 0.5 * (ch0[i] + ch1[i + delay])
```

If `i + delay` is out of range → `0.0f`.

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

**Behavior:**
- **1 ch:** direct copy
- **2 ch:** `estimate_delay` + `beamform_2mic`
- **3+ ch:** `NotSupported` (simple 2-mic)

**Note:** `max_delay = 16` samples (~1 ms @ 16 kHz). For a wider range call `estimate_delay` directly.

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

**`s16_to_float`** — `/ 32768.0` (asymmetric but common).
**`float_to_s16`** — `× 32767.0` (clamp + round).
**`s16_bytes_to_float`** — reads little-endian bytes.
**`float_to_s16_bytes`** — writes little-endian bytes.

---

## 🌐 14) Platform Backends

### 14.1) `mic_aaudio` — Android

```cpp
namespace mic_aaudio {
    struct api { /* dlopen'd function pointers */ };
    api& get() noexcept;

    result<knst_byte_string> capture_s16le(...) noexcept;
    result<knst_byte_string> capture_f32(...) noexcept;
    result<knst_byte_string> capture_ext(...) noexcept;
}
```

**Dynamic loading:** `libaaudio.so` — `AAudio_*` symbols. Returns `NotSupported` at runtime (before Android 8.0).

**`capture_s16le` / `capture_f32`:** AAudio builder → stream → read loop. **Timeout:** `total_frames * 1000 / rate + 5000` ms. **Zero-padded if short.**

**Old bug:** Hard-coded 10 s timeout truncated anything over 10 s. **Now** scales with `total_frames`.

**`capture_ext`** — if `f.loopback`, uses `mic_playback::capture` (Java). Otherwise AAudio. **Named devices aren't supported by AAudio** — `device->name` ignored.

### 14.2) `mic_playback` — Android PlaybackCapture

```cpp
namespace mic_playback {
    void set_permission(int result_code, jobject result_data);
    bool has_permission();
    result<knst_byte_string> capture(const capture_format& f, uint32_t total_frames) noexcept;
}
```

**App integration required:**
```java
// 1) Get MediaProjectionManager
mpm = context.getSystemService(MEDIA_PROJECTION_SERVICE);

// 2) Create Intent
intent = mpm.createScreenCaptureIntent();

// 3) startActivityForResult(intent, REQ_CAPTURE)

// 4) onActivityResult:
knst_mic::mic_playback::set_permission(resultCode, data);
```

**What `capture` does:**
1. **Temp PCM path** — `knst_playcap.pcm`
2. **JNI call** — `KnstPlaybackCapture.capture(path, rate, ch, fmt, frames)`
3. **File read** — read raw PCM
4. **Remove temp**

**Error cases:**
- No JNI env → `BadState`
- Class not found → `NotSupported`
- Exception → `IoError`
- File read fail → `IoError`
- Short PCM → zero-pad
- Long PCM → truncate

### 14.3) JNI Env — 3 usage modes

Code supports 3 modes:

1. **`KNST_MIC_USE_PRINTER_JNI=1`** — if `knst_printer.hpp` was included earlier, reuse its JNI context
2. **`KNST_MIC_HAS_EXTERNAL_ENV=1`** — provide your own `knst_mic::knst_mic_external_env()` (strong symbol overrides weak)
3. **Neither** — weak default returns `nullptr` → `PlaybackCapture` returns `BadState` at runtime. Keeps header linkable in test programs.

### 14.4) `mic_wavein` — Windows legacy

```cpp
namespace mic_wavein {
    result<knst_byte_string> capture_s16le(rate, ch, total_frames) noexcept;
}
```

**waveIn API** — Windows default device, fast, no COM. **Only i16.** For named devices or loopback, use WASAPI.

**Zero-fill:** if `hdr.dwBytesRecorded < want_bytes`, tail is zeroed. **Previously** `out` could contain uninitialized memory → caller saw garbage samples.

### 14.5) `mic_wasapi` — Windows modern

```cpp
namespace mic_wasapi {
    result<knst_byte_string> capture(
        const capture_format& f,
        uint32_t total_frames,
        const knst_audio_device* device) noexcept;
}
```

**What it does?**
1. **COM init** (`CoInitializeEx` MULTITHREADED)
2. **MMDeviceEnumerator** → device or default
3. **IAudioClient** → `GetMixFormat` (verify float32)
4. **Initialize** — SHARED mode, `loopback` flag
5. **IAudioCaptureClient** → event-driven read
6. **Downmix / upmix** — if `mix_channels != f.channels`
7. **Format conversion** — float32 → `f.sample`
8. **Zero-pad** — if short

**Critical details:**

**`resolve_device` — E_NOTIMPL removed:**
```cpp
if (dev && !dev->name.empty()) {
    // LPCWSTR is the endpoint id "{0.0.0.00000000}.{GUID}"
    return en->GetDevice(reinterpret_cast<LPCWSTR>(dev->name.data()), out);
}
return en->GetDefaultAudioEndpoint(flow, eConsole, out);
```

**Old bug:** `E_NOTIMPL` returned → caller fell back to waveIn → user's "record from my USB mic" got **default mic**. **Now** device selection is honored.

**Float32 mix format check:**
```cpp
bool is_float_mix = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                    (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ... subtype_float);
if (!is_float_mix) { CoTaskMemFree(mix); return NotSupported; }
```

**Why?** `_w::convert` reads as float32. If mix format is 16-bit PCM → garbage.

**Downmix / upmix:**
- **Requested 1 ch:** sum / N
- **M < N (downmix):** first M channels
- **M > N (upmix):** cyclic duplicate

**i32 conversion precision:**
```cpp
// Double, not .0f — 2147483647 cannot be exactly represented in float,
// .0f rounds to 2^31, overflows int32_t on v=1.0 → INT_MIN.
int32_t s = (int32_t)std::lround(v * 2147483647.0);
```

**COM init:** `RPC_E_CHANGED_MODE` accepted (another thread may have initialized differently).

---

## 🐧 15) Linux-Specific

### 15.1) `parec` call

```cpp
knst_dev::argv a;
a << "-s" << "TERM" << "2.50" << "parec";
if (device) a << "device=...";
a << "--format=s16le" << "--rate=16000" << "--channels=1"
  << "--latency-msec=20" << "--raw";
knst_dev::run_codes("timeout", a, ms + 5000, { 124, 130, 143 });
```

**Timeout wrapper:** uses the `timeout` command to bound `parec`. `124` (timeout), `130` (SIGINT), `143` (SIGTERM) accepted (normal termination).

**Format mapping (`_parec_detail::parec_fmt`):**
- `i16` → `s16le`
- `i24` → `s24le`
- `i32` → `s32le`
- `f32` → `float32le`

### 15.2) Warmup trim

`k_warmup_ms = 80`. Total capture time `ms + 80`. First 80 ms dropped (PulseAudio latency). Then `ms` of data taken.

### 15.3) Loopback

**Linux has no built-in "default monitor."** Need `parec --device=<monitor>`. `f.loopback && !device` → **`NotSupported`**.

**Correct usage:**
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

## ⚠️ 16) Common Gotchas

### 1) 60 second limit

`record_raw` and `record_fmt`: `ms <= 60000`. For longer:
- `recorder` class — `capture_interleaved` unlimited
- `knst_aud::stream` — real-time

### 2) Named devices on Windows

**Old bug:** `resolve_device` returned `E_NOTIMPL` → waveIn fallback → default mic. **Now** the endpoint id passes to `GetDevice()`.

**Device name:** WASAPI endpoint id — `{0.0.0.00000000}.{GUID}`. `knst_aud::audio::list_sources()` returns it in this format.

### 3) WASAPI loopback on Windows

`f.loopback = true` → `eRender` (output) endpoint. **IAudioClientInitialize** with `AUDCLNT_STREAMFLAGS_LOOPBACK` flag. **This flag only works in shared mode** (WASAPI restriction).

### 4) Windows mix format might be 16-bit PCM

**Check:** `WAVE_FORMAT_IEEE_FLOAT` or `WAVE_FORMAT_EXTENSIBLE` + subtype float. **Otherwise** `NotSupported` — `_w::convert` expects float32.

### 5) Windows i32 round error

`v * 2147483647.0f` (`.0f`) → `2^31` (2147483648) → `int32_t` overflow → `INT_MIN`. **Correct:** `v * 2147483647.0` (double).

### 6) Android PlaybackCapture setup

Loopback returns `BadState` unless `mic_playback::set_permission()` is called. **App side:**
```java
// onActivityResult:
KnstPlaybackCapture.setProjection(resultCode, data);
```

### 7) AAudio named devices on Android

`s.setDeviceId(builder, device_id)` — only if `device_id > 0`. **`device->name` is ignored.** To obtain `device_id`: Android `AudioManager.getDevices(GET_DEVICES_INPUTS)` + `AudioDeviceInfo.getId()`.

### 8) Linux loopback parameter

`f.loopback && !device` → `NotSupported`. **First** call `monitor_source()`, then `record_fmt(&mon, ms, f)`.

### 9) Linux `parec` warmup

First 80 ms dropped. **`record_raw`** same (shares legacy path).

### 10) `measure_all` 30 ms sleep

**30 ms** sleep between each `measure_level`. For PulseAudio/PipeWire to release the previous `parec` device. **Back-to-back calls** fail intermittently without it.

### 11) VAD `energy_threshold` too high

Default `0.01f` (-40 dB). **Increase** in noisy environments (0.05 = -26 dB). **Decrease** in quiet rooms (0.005 = -46 dB).

### 12) VAD `zcr_max` wrong

Default `0.3f`. Voiced sounds ~0.05, unvoiced ~0.4. **For music** raise (0.5+), **for speech** 0.25 is enough.

### 13) AGC gain oscillation

`attack_factor = 0.3` too fast → gain pumping. **To slow down:** `attack_factor = 0.1`, `release_factor = 0.02`.

### 14) `noise_gate::coef_for` `ms <= 0.1`

Instant (coefficient = 1.0). If `attack_ms = 0`, this behavior.

### 15) Pitch detection on silent signal

`energy < 1e-9` → 0. **Before `detect`** check `energy` or `rms`.

### 16) Pitch detection min/max lag

`min_lag = sample_rate / max_hz`, `max_lag = sample_rate / min_hz`. **Choose config correctly:**
- **Male speech:** 80-200 Hz → `min_hz = 80`, `max_hz = 200`
- **Female speech:** 150-400 Hz
- **Child:** 200-500 Hz
- **Music:** 40-1000 Hz

### 17) Pitch detection harmonic confusion

Autocorrelation picks **first peak** (above threshold) → fundamental. **If wrong** harmonic is picked, lower the threshold.

### 18) `silence_detector` sample_rate

`sample_rate` **must be correct**, otherwise `silent_samples → ms` is wrong. Blocks must align with rate.

### 19) Voice recorder channels == 1

`feed` receives mono, `capture_interleaved` multiplies by `channels`. **Multi-channel is `NotSupported`.** Do a mono capture loop.

### 20) Voice recorder pre-roll cap

`pre_ring_capacity = min(pre_roll_ms * rate / 1000, 4800000)` — about 100 s @ 48 kHz. For more, lower `pre_roll_ms`.

### 21) Voice recorder `flush_pre_roll` 16 MB limit

`frames * ch > 16 MB` → returns 0. Pre-roll too large loses the recording.

### 22) Beamforming 3+ channels

`mic_array::beamform` only supports 1 or 2 channels. 3+ → `NotSupported`.

### 23) `estimate_delay` `max_delay` limit

If `max_delay >= n`, clamped to `n - 1`. Very large `max_delay` is unnecessarily slow (O(n * max_delay)).

### 24) `recorder::open_file` header size

`make_wav_header` returns **format-specific** size:
- PCM i16 ≤2ch → 44
- float/alaw/ulaw → 56 (fact chunk)
- PCM 3+ch → 68 (extensible)
- float 3+ch → 80

**Don't hard-code 44** — store `hdr_len_`.

### 25) `recorder::finish` header rewrite

Header is **rewritten** — `data_bytes` + `fact_frames` + `riff_size` updated. Without `finish`, the file is **corrupt** (0-size header).

### 26) `record_wav` limit

Calls `record_raw` → **60 second** limit. Even `seconds > 60` returns `InvalidArgument` from `record_raw`.

### 27) `_record_raw_legacy` device check

`!device->is_input` → `InvalidArgument`. Are monitor sources inputs? `list_sources` marks them correctly.

### 28) Android AAudio min API

Android **8.0 (API 26)** for AAudio. Before that → `NotSupported`. **Fallback:** OpenSL ES (not in this module).

### 29) JNI env mode

Without `KNST_MIC_USE_PRINTER_JNI` or `KNST_MIC_HAS_EXTERNAL_ENV`, env is `nullptr`. Loopback/PlaybackCapture returns **`BadState` at runtime**. **Test programs** are fine (link succeeds).

### 30) `KNST_LOG_WARN` fallback

Header provides a **fallback macro** to stay self-contained. If `knst_printer.hpp` was included first, the real macro is used.

---

## 📊 17) Performance

| Operation | Cost | Note |
|---|---|---|
| `record_raw` (i16, 1s) | ~1s + I/O | parec/waveIn/AAudio |
| `record_fmt` | ~1s + I/O | Backend selection |
| `measure_level` | ~500 ms | i16 16k mono |
| `measure_all` | N × (500 + 30) ms | Sleep required |
| `enumerate_microphones` | O(devices) | `list_sources` call |
| `default_microphone` | O(devices) | |
| **VAD `feed`** | O(n) | RMS + ZCR |
| **AGC `process`** | O(n) | Peak + gain |
| **Noise gate `process`** | O(n) | Envelope |
| **Pitch `detect`** | O(n × max_lag) | Autocorrelation |
| **Silence `feed`** | O(n) | RMS |
| **Beamforming** | O(n × max_delay) | Cross-correlation |
| **Format conversion** | O(n) | Direct |

**Optimization:**
- **Long recording:** `recorder` + `pump_from(stream)` — vector resize amortized
- **Real-time processing:** `knst_aud::stream` + VAD/AGC/gate block by block
- **Pitch detection:** `n = 512` (32 ms @ 16 kHz) is enough
- **Beamforming:** `max_delay = 16` (1 ms) is usually enough
- **`measure_all`:** Parallelization tempting but **don't remove the 30 ms sleep** — PipeWire race

---

## ✅ 18) Do / ❌ Don't

### ✅ Do

```cpp
// 1. recorder for long recording
knst_mic::recorder rec;
knst_aud::wav::format fmt;
fmt.rate = 48000; fmt.channels = 2; fmt.sample = knst_aud::sample_t::i16;

auto r = knst_mic::recorder::open_file(u"long.wav", fmt);
// ... capture_interleaved repeatedly ...
r.value.finish();
```

```cpp
// 2. VAD state machine — feed block by block
knst_mic::voice_detector vad;
while (running) {
    float block[320];
    stream.read_interleaved(block, 320);  // 20 ms @ 16 kHz
    bool active = vad.feed(block, 320);
    if (vad.last_transition() == knst_mic::voice_detector::edge::start) {
        std::cout << "Speech started\n";
    }
}
```

```cpp
// 3. Loopback — platform-specific prep
#if defined(_WIN32)
    // device = nullptr → default render, loopback=true
    knst_mic::capture_format f;
    f.loopback = true;
    f.sample_rate = 48000;
    f.channels = 2;
    auto r = knst_mic::record_fmt(nullptr, 5000, f);
#elif defined(KNST_USING_PLATFORM_ANDROID)
    // App: onActivityResult → mic_playback::set_permission(resultCode, data);
    knst_mic::capture_format f;
    f.loopback = true;
    auto r = knst_mic::record_fmt(nullptr, 5000, f);
#elif defined(KNST_USING_PLATFORM_LINUX)
    auto mon = knst_mic::monitor_source();
    if (!mon) return;   // no monitor
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
    vr.feed(block, 320);   // only writes while speaking
}

vr.stop();   // WAV header finalized
```

```cpp
// 5. Real-time pipeline
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

### ❌ Don't

```cpp
// 1. record_raw over 60s
knst_mic::record_raw(nullptr, 120000);   // ❌ InvalidArgument
// ✅ use the recorder class

// 2. Loopback without device on Linux
#if defined(KNST_USING_PLATFORM_LINUX)
knst_mic::capture_format f; f.loopback = true;
knst_mic::record_fmt(nullptr, 5000, f);   // ❌ NotSupported
// ✅ get monitor_source(), then record_fmt(&mon, ...)
#endif
```

```cpp
// 3. Android PlaybackCapture without permission
// ❌ Loopback before set_permission(resultCode, data) in onActivityResult
// ✅ set_permission first, then record_fmt(loopback=true)

// 4. Assuming WASAPI mix format
// ❌ Some exotic drivers return 16-bit PCM → NotSupported expected
// ✅ Handle error: if (err == NotSupported) { /* fallback to waveIn */ }

// 5. Pitch detection on silent block
float pitch = detector.detect(silent_block, 512);   // ❌ returns 0
// ✅ Check RMS/energy first

// 6. VAD threshold too low
vad_config cfg; cfg.energy_threshold = 0.0001f;   // ❌ Everything "active"
// ✅ Tune to ambient noise

// 7. AGC attack_factor too fast
agc_config cfg; cfg.attack_factor = 0.9f;   // ❌ Pumping/gain oscillation
// ✅ 0.1-0.3

// 8. Voice recorder channels != 1
voice_recorder::config cfg;
cfg.format.channels = 2;   // ❌ NotSupported
// ✅ Mono capture, downmix afterwards if needed

// 9. Beamforming 3+ channels
mic_array arr;
arr.beamform(interleaved_4ch, frames, 4);   // ❌ NotSupported
// ✅ Reduce to 2 channels or implement your own

// 10. Forgetting recorder::finish
auto r = recorder::open_file(u"x.wav", fmt);
r.value.capture_interleaved(...);
// ❌ finish() not called → wrong header
// ✅ r.value.finish() or destructor abort() — different behavior
```

---

## 🔥 19) Real-World Examples

### Example 1: Recording CLI

```cpp
int main() {
    auto mon = knst_mic::default_microphone();
    if (!mon) { std::cerr << "No mic\n"; return 1; }

    auto r = knst_mic::record_wav(&mon.value, u"test.wav", 5, 48000, 2);
    if (!r) std::cerr << "Recording failed\n";
    else    std::cout << "test.wav written\n";
}
```

### Example 2: Microphone test (level measurement)

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
            std::cout << m.name << ": couldn't measure\n";
        }
    }
}
```

### Example 3: VAD-gated voice recorder

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

    for (int i = 0; i < 3000; ++i) {   // 60 s @ 20 ms blocks
        float block[320];
        auto read = stream.read_interleaved(block, 320);
        if (!read || read.value == 0) break;
        vr.feed(block, 320);
    }

    vr.stop();
}
```

### Example 4: Real-time voice activity monitor

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
            std::cout << "[" << ms << " ms] Speech started\n";
        } else if (vad.last_transition() == knst_mic::voice_detector::edge::end) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::cout << "[" << ms << " ms] Speech ended\n";
        }
    }
}
```

### Example 5: Pitch (note) detection

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

### Example 6: Noise gate + AGC pipeline

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

        // Processed block — recording, network, etc.
    }
}
```

### Example 7: Format conversion

```cpp
void convert_and_save() {
    auto raw = knst_mic::record_raw(nullptr, 1000, 16000, 1);
    if (!raw) return;

    // i16 bytes → float
    knst_vector<float> samples = knst_mic::fmt::s16_bytes_to_float(raw.value);

    // Process (AGC, gate, etc.)
    knst_mic::agc agc;
    agc.process(samples.data(), samples.size());

    // Float → i16 bytes
    knst_byte_string processed = knst_mic::fmt::float_to_s16_bytes(
        samples.data(), samples.size());

    // Write WAV
    unsigned char hdr[44];
    knst_aud::write_wav_header(hdr, processed.length(), 16000, 1, 16);
    knst_byte_string full;
    for (int i = 0; i < 44; ++i) full.push_back(hdr[i]);
    for (uint32_t i = 0; i < processed.length(); ++i) full.push_back(processed[i]);
    knst_file::write_file_data(u"processed.wav", full.data(), full.length());
}
```

### Example 8: Loopback (platform-specific)

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
    // Write WAV...
    return true;

#elif defined(KNST_USING_PLATFORM_ANDROID)
    if (!knst_mic::mic_playback::has_permission()) return false;
    auto r = knst_mic::record_fmt(nullptr, secs * 1000, f);
    if (!r) return false;
    // Write WAV...
    return true;

#elif defined(KNST_USING_PLATFORM_LINUX)
    auto mon = knst_mic::monitor_source();
    if (!mon) return false;
    auto r = knst_mic::record_fmt(&mon.value, secs * 1000, f);
    if (!r) return false;
    // Write WAV...
    return true;
#else
    return false;
#endif
}
```

---

## 📌 20) API Contract Summary

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

// Enumeration
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

// Convenience recording
result<knst_byte_string> record_raw(
    const knst_audio_device*, uint32_t ms,
    uint32_t rate = 16000, uint32_t channels = 1) noexcept;

result<void> record_wav(
    const knst_audio_device*, const knst_c16string&, uint32_t seconds = 5,
    uint32_t rate = 48000, uint32_t channels = 1) noexcept;

// Level
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

## 🎯 What It Brings You

`knst_microphone` gives you serious advantages in these situations:

- ✅ **Cross-platform capture** — Windows / Linux / Android same API
- ✅ **Multi-format** — i16, i24, i32, f32 (WASAPI + parec + AAudio)
- ✅ **Loopback** — Windows WASAPI, Android PlaybackCapture
- ✅ **Named device selection** — Windows endpoint id
- ✅ **Voice Activity Detection** — energy + ZCR + hangover state machine
- ✅ **AGC** — attack/release peak follower
- ✅ **Noise gate** — threshold + envelope
- ✅ **Pitch detection** — autocorrelation + parabolic interpolation
- ✅ **Silence detection** — RMS threshold + duration
- ✅ **Voice-triggered recorder** — pre-roll + VAD-gated WAV
- ✅ **Beamforming** — 2-mic delay-and-sum
- ✅ **Level measurement** — peak/rms/dB/clip
- ✅ **Format helpers** — int16 ↔ float, bytes ↔ float
- ✅ **Recorder class** — file + memory, header-aware
- ✅ **Zero thread** — synchronous, full control in your hands
- ✅ **KernelNucleusT integration** — `knst_audio`, `knst_file`, `knst_json`

---