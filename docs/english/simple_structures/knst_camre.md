# knst_camera — Camera Capture, Photo and Recording Library

Hey! 👋 `knst_camera.hpp` is KernelNucleusT's **camera capture, photo snapshot and audio+video recording** module. All in one header: V4L2 (Linux), Media Foundation (Windows), Camera2 NDK (Android) — all under one API. Photo capture (JPEG/BMP/PNG/PPM), MJPEG passthrough, video+audio recording, motion-triggered capture, camera controls, auto-reconnect.

**In short:** **Synchronous, exception-free, no RTTI.** Every public call returns `result<T>`. **`camera` is NOT movable/copyable** — a worker thread holds `this`. **Callbacks run on the capture thread** — `start()/stop()/close()` cannot be called from inside them. Partial recordings (limit/disconnect) are **automatically finalized** (valid files).

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

### Platform matrix

| Feature | Linux (V4L2) | Windows (MF) | Android (Camera2) |
|---|---|---|---|
| **Camera enumerate** | ✅ | ✅ | ✅ |
| **Format list** | ✅ | ✅ | ✅ |
| **Open/close** | ✅ | ✅ | ✅ |
| **Streaming** | ✅ | ✅ | ✅ |
| **Photo snapshot** | ✅ | ✅ | ✅ |
| **MJPEG passthrough** | ✅ | ✅ | ✅ |
| **JPEG/BMP/PNG/PPM output** | ✅ | ✅ | ✅ |
| **Video recording (MJPEG/AVI, MKV, MP4, MOV)** | ✅ | ✅ | ✅ |
| **Audio recording (PCM)** | ✅ | ✅ | ✅ |
| **Audio-only recording (WAV/MKV)** | ✅ | ✅ | ✅ |
| **Pause/resume** | ✅ | ✅ | ✅ |
| **Limits (duration/frames/bytes)** | ✅ | ✅ | ✅ |
| **Segment split** | ✅ | ✅ | ✅ |
| **Motion-triggered recording** | ✅ | ✅ | ✅ |
| **Camera controls** | ✅ (full) | ✅ (VideoProcAmp + CameraControl) | ✅ (subset — AE/AF/AWB/EV/ISO) |
| **Auto modes** | ✅ | ✅ | ✅ (AE/AF/AWB) |
| **Reset/save/restore** | ✅ | ✅ | ✅ |
| **Auto-reconnect** | ✅ | ✅ | ✅ |
| **Uevent/hotplug** | ❌ | ❌ | ❌ |
| **RAW video (AVI only)** | ✅ | ✅ | ⚠️ |
| **H.264/H.265 ENCODER** | ❌ | ❌ | ❌ |
| **H.264/H.265 DECODER (for recording)** | ❌ | ❌ | ❌ |
| **WebM writing** | ❌ | ❌ | ❌ |
| **MP4 audio** | ❌ | ❌ | ❌ |
| **AVI audio (non-PCM)** | ❌ | ❌ | ❌ |
| **MOV audio (non-PCM16)** | ❌ | ❌ | ❌ |
| **USB microphone enumerate** | ✅ (`list_audio_sources`) | ✅ | ✅ |

### Dependencies

| Dependency | For | Version |
|---|---|---|
| **`knst_video.hpp`** | Writer, MJPEG, motion_detector, pixel_format | internal |
| **`knst_audio.hpp`** (optional) | Audio recording | internal |
| **`knst_dev.hpp`** | `status`, `result`, `to_utf8` | internal |
| **`knst_file.hpp`** | Path handling, `write_file_atomic` | internal |

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **3 platforms one API** | V4L2 / MF / Camera2 |
| **MJPEG passthrough** | Zero re-encode in photo capture |
| **DHT injection** | For MJPEG streams missing Huffman tables |
| **Bound check** | Every frame validated against pixel layout |
| **Buffer validation** | Truncated frames dropped |
| **poll() (not select)** | No FD_SETSIZE overflow |
| **Auto-reconnect** | USB unplug → reopen |
| **Motion detection** | Motion detect + auto record |
| **Recording split** | Split by file size / duration |
| **Pause/resume** | No timeline gaps |
| **Thread-safe callback** | Frame/event callbacks on capture thread |
| **Reentrancy guard** | `start/stop/close` forbidden from callback |
| **Partial finalization** | File valid on limit/disconnect |
| **Stop-signal-safe** | No race conditions |
| **Format picker** | Nearest format auto-selected |
| **UTF-16 path** | Windows compatible |
| **Timestamped names** | `snapshot_auto` → `photo_20241112_143025_123_0001.jpg` |
| **Burst mode** | N photos, M ms apart |
| **Free functions** | `save_frame`, `save_frame_jpeg/bmp/png` |
| **camera_array** | Multi-camera management |

---

## 🚀 Quick Start

### Enumerate + open + snapshot

```cpp
#include "KernelNucleusT.hpp"

auto cams = knst_cam::camera::enumerate();
if (!cams || cams.value.empty()) {
    std::cerr << "No camera\n";
    return 1;
}

knst_cam::open_options o;
o.width  = 1280;
o.height = 720;
o.pix    = knst_vid::pixel_format::MJPEG;
o.fps_num = 30;

knst_cam::camera cam;
cam.open(cams.value[0], o);

// Photo works even without streaming
auto r = cam.snapshot(u"photo.jpg");
if (!r) {
    std::cerr << "Snapshot fail: " << cam.last_error_string() << "\n";
}
```

### Video + audio recording

```cpp
knst_cam::record_options ro;
ro.path = u"clip.mkv";           // container from extension
ro.enable_audio = true;
ro.audio_rate = 48000;
ro.audio_channels = 2;
ro.jpeg_quality = 85;

auto r = cam.start_recording(ro);
if (!r) {
    std::cerr << "Cannot start recording: "
              << cam.last_error_string() << "\n";
    return 1;
}

std::this_thread::sleep_for(std::chrono::seconds(10));
cam.stop_recording();
```

### Streaming + frame callback

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

cam.start(
    [](void* user, const knst_cam::frame_view& f) {
        // Frame arrived — copy, process, save
        std::cout << "Frame " << f.sequence
                  << " (" << f.width << "x" << f.height << ")\n";
    },
    nullptr,
    [](void* user, knst_cam::event e) {
        std::cout << "Event: " << knst_cam::event_name(e) << "\n";
    },
    nullptr
);

std::this_thread::sleep_for(std::chrono::seconds(30));
cam.stop();
```

### Motion-triggered recording

```cpp
knst_cam::motion_options mo;
mo.detector.min_interval_ms = 100;
mo.post_roll_ms = 3000;
mo.auto_record = true;
mo.dir = u"motion/";
mo.prefix = u"motion_";
mo.record.enable_audio = false;
mo.record.jpeg_quality = 85;

cam.enable_motion_detection(mo);
cam.start(nullptr, nullptr);   // Motion events start recording automatically
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Enumerate** | `enumerate()`, `default_camera()`, `find(id)`, `backend_name()`, `list_audio_sources()` |
| **Lifecycle** | `open(info/opt)`, `close()`, `is_open()`, `is_streaming()`, `is_recording()`, `is_recording_paused()` |
| **Frames** | `grab()`, `capture_latest()` |
| **Streaming** | `start(cb)`, `stop()`, `set_event_callback()`, `set_event_mask()` |
| **Recording** | `start_recording(opts)`, `stop_recording()`, `pause_recording()`, `resume_recording()`, `recording_info()` |
| **Snapshots** | `snapshot()`, `snapshot_jpeg/bmp/png()`, `snapshot_auto()`, `snapshot_memory()`, `snapshot_burst()` |
| **Motion** | `enable_motion_detection(opts)`, `disable_motion_detection()`, `is_motion_active()` |
| **Controls** | `controls()`, `get_control(t)`, `set_control(t,v)`, `set_control_auto(t,b)`, `reset_control(t)`, `reset_all_controls()`, `save_controls()`, `restore_controls(v)` |
| **Info** | `current_format()`, `supported_formats()`, `info()`, `stats()`, `effective_fps()`, `frames_captured/dropped/recorded()` |
| **Error** | `last_error()`, `last_error_string()`, `last_message()` |
| **Free** | `encode_frame()`, `save_frame()`, `save_frame_jpeg/bmp/png()` |
| **camera_array** | `open_all()`, `count()`, `at(i)`, `close_all()`, `snapshot_all()` |

---

## 🎯 1) Error Enum

```cpp
enum class errc : uint8_t {
    Ok = 0,
    InvalidArgument,
    NotSupported,
    NotFound,
    PermissionDenied,
    IoError,
    OutOfMemory,
    BadState,
    Corrupt,
    Timeout,
    DeviceBusy,
    EndOfStream,
    TooLarge,
    Disconnected
};

const char* errc_string(errc e) noexcept;
knst_dev::status to_status(errc e) noexcept;
```

| Err | Meaning |
|---|---|
| **`Ok`** | Success |
| **`InvalidArgument`** | Invalid parameter |
| **`NotSupported`** | Not on this platform/device |
| **`NotFound`** | Camera/file missing |
| **`PermissionDenied`** | No permission on `/dev/video*` |
| **`IoError`** | Disk/file I/O |
| **`OutOfMemory`** | Memory exhausted |
| **`BadState`** | Wrong state (e.g. `grab()` while streaming) |
| **`Corrupt`** | Corrupt frame/file |
| **`Timeout`** | No frame arrived |
| **`DeviceBusy`** | Device in use |
| **`EndOfStream`** | End of stream |
| **`TooLarge`** | Size exceeds library limits |
| **`Disconnected`** | Camera unplugged |

---

## 🎛️ 2) Camera Controls

### 2.1) `control_type` — Control types

```cpp
enum class control_type : uint8_t {
    Brightness, Contrast, Saturation, Hue,
    Exposure, ExposureAuto, ExposureAutoPriority,
    Focus, FocusAuto,
    WhiteBalance, WhiteBalanceAuto,
    Gain, Gamma, Sharpness, Backlight, PowerLineFrequency,
    Zoom, Pan, Tilt,
    Unknown
};
```

| Type | Linux V4L2 | Windows | Android |
|---|---|---|---|
| **Brightness** | ✅ | ✅ | ❌ |
| **Contrast** | ✅ | ✅ | ❌ |
| **Saturation** | ✅ | ✅ | ❌ |
| **Hue** | ✅ | ✅ | ❌ |
| **Exposure** (absolute) | ✅ | ✅ | ⚠️ EV compensation |
| **ExposureAuto** | ✅ | ✅ | ✅ (AE mode) |
| **Focus** | ✅ | ✅ | ❌ |
| **FocusAuto** | ✅ | ✅ | ✅ (AF mode) |
| **WhiteBalance** | ✅ | ✅ | ❌ |
| **WhiteBalanceAuto** | ✅ | ✅ | ✅ (AWB mode) |
| **Gain** | ✅ | ✅ | ⚠️ ISO sensitivity |
| **Gamma** | ✅ | ✅ | ❌ |
| **Sharpness** | ✅ | ✅ | ❌ |
| **Backlight** | ✅ | ✅ | ❌ |
| **PowerLineFrequency** | ✅ | ❌ | ❌ |
| **Zoom/Pan/Tilt** | ✅ | ❌ | ❌ |

### 2.2) `control_info` — Control info

```cpp
struct control_info {
    control_type type = control_type::Unknown;
    int32_t min = 0, max = 0, step = 1, current = 0, default_value = 0;
    bool is_auto = false;        // Does this control have an auto mode?
    bool is_auto_on = false;     // Currently in auto mode?
    bool is_writable = true;
    bool is_active = true;
    knst_c16string name;
};
```

### 2.3) `control_value` — For save/restore

```cpp
struct control_value {
    control_type type = control_type::Unknown;
    int32_t value = 0;
};
```

### 2.4) Usage

```cpp
// List all controls
auto cs = cam.controls();
if (cs) {
    for (const auto& c : cs.value) {
        std::cout << c.name << ": "
                  << c.current << " (" << c.min << ".." << c.max << ")\n";
    }
}

// Set brightness (auto-clamped + step-aligned)
cam.set_control(knst_cam::control_type::Brightness, 128);

// Toggle auto exposure
cam.set_control_auto(knst_cam::control_type::Exposure, true);

// Reset to default
cam.reset_control(knst_cam::control_type::Brightness);

// Reset all
cam.reset_all_controls();

// Save/restore
auto saved = cam.save_controls();
// ... change stuff ...
cam.restore_controls(saved.value);
```

### 2.5) Leaving auto mode for absolute values

**Critical:** Setting absolute controls like `Exposure`, `Focus`, `WhiteBalance` **automatically leaves auto mode** (on V4L2):

```cpp
cam.set_control(knst_cam::control_type::Exposure, 100);
// → V4L2_CID_EXPOSURE_AUTO = MANUAL
// → V4L2_CID_EXPOSURE_ABSOLUTE = 100
```

Windows behaves similarly: `VideoProcAmp_Flags_Manual` is set.

**On Android:** AE/AF/AWB mode is set as a separate control. For absolute values use `set_control_auto` + `set_control` combination.

---

## 📸 3) Photo Snapshot

### 3.1) `image_options` — Image options

```cpp
struct image_options {
    image_format format = image_format::Auto;   // Auto → from extension, JPEG if unknown
    int jpeg_quality = 90;
    knst_vid::mjpeg::subsampling jpeg_sub = s420;
    bool mirror = false;
    bool flip_v = false;
    uint32_t rotate_deg = 0;                    // 0 / 90 / 180 / 270
    uint32_t out_w = 0, out_h = 0;              // one → keep aspect
    double scale = 0.0;
    bool grey = false;
    bool allow_passthrough = true;              // MJPEG → JPEG bit-exact
};
```

### 3.2) `image_format`

```cpp
enum class image_format : uint8_t { Auto, JPEG, BMP, PNG, PPM };
```

**Extension detection:**
- `.jpg`, `.jpeg` → JPEG
- `.bmp` → BMP
- `.png` → PNG
- `.ppm`, `.pgm` → PPM/PGM

### 3.3) Snapshot methods

```cpp
result<void> snapshot(const knst_c16string& path, const image_options& o = {}) noexcept;
result<void> snapshot_jpeg(const knst_c16string& path, int quality = 90) noexcept;
result<void> snapshot_bmp(const knst_c16string& path) noexcept;
result<void> snapshot_png(const knst_c16string& path) noexcept;
result<knst_byte_string> snapshot_memory(const image_options& o = {}) noexcept;
result<knst_c16string> snapshot_auto(const knst_c16string& dir,
                                     const knst_c16string& prefix = u"photo_",
                                     const image_options& o = {}) noexcept;
result<knst_vector<knst_c16string>> snapshot_burst(
    const knst_c16string& dir, uint32_t count, uint32_t interval_ms,
    const knst_c16string& prefix = u"burst_",
    const image_options& o = {}) noexcept;
```

### 3.4) MJPEG passthrough

**If the camera already outputs MJPEG** and the output format is JPEG, **bit-exact passthrough** happens. `jpeg_quality` is **ignored**.

**DHT injection:** Some cameras don't send a **Huffman table (DHT segment)** in their JPEG (optimized output). In that case `inject_std_dht()` prepends the standard DHT — the file **opens in every player**.

```cpp
image_options o;
o.allow_passthrough = true;   // default
// If camera is MJPEG and no edits → bit-exact JPEG
cam.snapshot(u"fast.jpg", o);
```

**To disable passthrough:** set `allow_passthrough = false` or enable any edit (mirror, rotate, resize, grey).

### 3.5) Snapshot during streaming

`capture_latest()`:
- **While streaming** → returns the latest frame immediately
- **Not streaming** → calls `grab()` (one-shot capture + warmup)

### 3.6) `snapshot_auto` — Timestamped name

Format: `<prefix><timestamp>_<counter>.<ext>`

```
photo_20241112_143025_123_0001.jpg
```

**Components:**
- **`<prefix>`** — user-supplied (`u"photo_"`)
- **`<timestamp>`** — `YYYYMMDD_HHMMSS_mmm`
- **`<counter>`** — 0..9999 cyclic
- **`<ext>`** — based on format (`.jpg`, `.bmp`, `.png`, `.ppm`/`.pgm`)

### 3.7) `snapshot_burst`

N photos, M ms apart (blocking). If snapshot time > `interval_ms`, the next one starts **without waiting**.

```cpp
auto paths = cam.snapshot_burst(u"burst/", 10, 100);
// 10 photos, 100ms apart
```

**Max count:** 10,000 (DoS guard).

### 3.8) Free functions (without camera)

```cpp
// Encode a frame to any format
errc encode_frame(const frame&, const image_options&, knst_byte_string&, image_format& used) noexcept;

// Save one frame to a file
result<void> save_frame(const frame&, const knst_c16string& path,
                        image_options = {}) noexcept;
result<void> save_frame_jpeg(const frame&, const knst_c16string&, int quality = 90) noexcept;
result<void> save_frame_bmp(const frame&, const knst_c16string&) noexcept;
result<void> save_frame_png(const frame&, const knst_c16string&) noexcept;
```

**Use:** To write JPEG/BMP/PNG without a camera (e.g. after `knst_converter` frame extract).

---

## 🎥 4) Video + Audio Recording

### 4.1) `record_options` — Recording options

```cpp
struct record_options {
    knst_c16string path;
    knst_vid::container cont = Unknown;    // from extension
    bool enable_video = true;
    knst_vid::video_codec vcodec = MJPEG;  // only MJPEG or RAW(AVI)
    int jpeg_quality = 85;
    knst_vid::mjpeg::subsampling jpeg_sub = s420;

    // Limits
    uint32_t max_duration_ms = 0;   // 0 = unlimited
    uint64_t max_frames = 0;
    uint64_t max_bytes = 0;
    uint32_t split_ms = 0;          // >0: new file every N ms
    uint64_t split_bytes = 0;       // >0: new file every N bytes

    // Audio
    bool enable_audio = false;
    uint32_t audio_rate = 48000;
    uint32_t audio_channels = 2;
    knst_vid::audio_codec audio_codec = PCM_S16LE;
    knst_c16string audio_source;    // empty = default microphone
    float audio_gain_db = 0.0f;
    bool audio_remove_dc = false;

    knst_vid::metadata meta;
};
```

### 4.2) Container compatibility

| Container | Video | Audio |
|---|---|---|
| **AVI** | MJPEG, RAW, H.264, H.265, VP8, VP9, AV1, MPEG4 | ✅ PCM |
| **MKV** | Everything (except RAW) | ✅ Everything |
| **MP4** | MJPEG, H.264, H.265 | ❌ **video only** |
| **MOV** | MJPEG, H.264, H.265 | ⚠️ **PCM_S16LE only** |
| **WAV** | ❌ | ✅ PCM (audio-only) |
| **WebM** | ❌ | ❌ |

**Extension detection:** `.avi` → AVI, `.mkv` → MKV, `.mp4`/`.m4v` → MP4, `.mov` → MOV, `.wav` → WAV, `.webm` → WebM.

**Container override:** If `ro.cont` is set, the extension is ignored.

### 4.3) Video codec support

**Only MJPEG or RAW (AVI only)** can be recorded:

```cpp
ro.vcodec = knst_vid::video_codec::MJPEG;    // ✅
ro.vcodec = knst_vid::video_codec::RAW;      // ✅ (AVI only)
ro.vcodec = knst_vid::video_codec::H264;     // ❌ NotSupported
```

**If camera is MJPEG** → **zero re-encode**, packet copy. Other pixel formats → **MJPEG encode**.

### 4.4) Audio recording

```cpp
ro.enable_audio = true;
ro.audio_rate = 48000;
ro.audio_channels = 2;
ro.audio_codec = knst_vid::audio_codec::PCM_S16LE;   // PCM mandatory

// Specific microphone
ro.audio_source = u"alsa_input.pci-0000_00_1f.3.analog-stereo";

// Gain + DC removal
ro.audio_gain_db = 3.0f;
ro.audio_remove_dc = true;
```

**Supported codecs:** `PCM_S16LE`, `PCM_S24LE`, `PCM_S32LE`, `PCM_F32LE`, `PCM_U8`.

**Container compatibility:**
- **AVI, MKV, WAV** → any PCM
- **MOV** → `PCM_S16LE` only
- **MP4** → no audio

### 4.5) Pause/resume

```cpp
cam.pause_recording();    // Pause
// ... wait 5 seconds ...
cam.resume_recording();   // Resume
```

**No timeline gap** — the `pause_total_` duration is subtracted from timestamps. Recording **looks like it continues from where it left off**.

### 4.6) Limits

**`max_duration_ms`** — Total duration (seconds). Exceeded → `RecordingLimitReached` event + recording closed.

**`max_frames`** — Total frames. Requires `enable_video = true`.

**`max_bytes`** — Total bytes (all segments).

**Audio-only recording:** Limits measured against the **audio clock**.

### 4.7) Split

**`split_ms`** — Start a new file every N ms:
```
clip_0001.mkv, clip_0002.mkv, clip_0003.mkv, ...
```

**`split_bytes`** — Start a new file every N bytes.

**Combined:** If both are set, **whichever triggers first** causes the split.

### 4.8) `recording_status`

```cpp
struct recording_status {
    bool active = false, paused = false;
    bool has_video = false, has_audio = false;
    uint64_t frames = 0, bytes = 0;
    double seconds = 0.0;
    uint32_t segment = 0;
    knst_c16string path;    // current (or last) file
};
```

**Usage:**
```cpp
auto s = cam.recording_info();
std::cout << "Segment: " << s.segment
          << ", bytes: " << s.bytes
          << ", seconds: " << s.seconds << "\n";
```

### 4.9) Auto-start stream

When `start_recording` is called **without an active stream**, the library starts its own stream (`rec_owns_stream_`).

```cpp
// No active stream
cam.start_recording(ro);   // → auto start()
// ...
cam.stop_recording();      // → auto stop()
```

**Note:** If streaming is **already active**, the stream is untouched (recording stop doesn't affect it).

---

## 🚶 5) Motion Detection

### 5.1) `motion_options`

```cpp
struct motion_options {
    knst_vid::motion_detector::config detector;
    uint32_t post_roll_ms = 3000;       // Wait N ms more after motion
    bool auto_record = false;           // Motion → start recording
    knst_c16string dir;                 // Output dir for auto_record
    knst_c16string prefix = u"motion_";
    record_options record;              // Template (path ignored)
};
```

### 5.2) Usage

```cpp
knst_cam::motion_options mo;
mo.detector.min_interval_ms = 100;   // Check at most every 100ms
mo.detector.threshold = 0.05;        // 5% pixel change
mo.post_roll_ms = 3000;
mo.auto_record = true;
mo.dir = u"motion/";
mo.record.enable_audio = true;
mo.record.jpeg_quality = 85;

cam.enable_motion_detection(mo);
cam.start(
    nullptr, nullptr,                  // No frame cb
    [](void* user, knst_cam::event e) {
        if (e == knst_cam::event::MotionStarted) {
            std::cout << "Motion started\n";
        } else if (e == knst_cam::event::MotionStopped) {
            std::cout << "Motion ended\n";
        }
    },
    nullptr
);

// Motion events auto:
//   MotionStarted → start_recording(mo.record)
//   MotionStopped (after post_roll) → stop_recording()

cam.disable_motion_detection();   // Turn off
```

### 5.3) `is_motion_active()`

```cpp
bool active = cam.is_motion_active();
```

Current motion state.

---

## 📡 6) Event System

### 6.1) `event` — Event types

```cpp
enum class event : uint8_t {
    Frame = 0,
    FrameDropped,
    StreamStarted,
    StreamStopped,
    RecordingStarted,
    RecordingStopped,
    RecordingPaused,
    RecordingResumed,
    Disconnected,
    Reconnected,
    Error,
    MotionStarted,
    MotionStopped,
    RecordingLimitReached,
    SnapshotSaved
};
```

### 6.2) Event mask

```cpp
// Default: everything except per-frame events
constexpr uint32_t default_event_mask() noexcept;
// = 0xFFFFFFFF & ~(bit(Frame) | bit(FrameDropped))

// Custom mask
cam.set_event_mask(
    event_bit(knst_cam::event::RecordingStarted) |
    event_bit(knst_cam::event::RecordingStopped) |
    event_bit(knst_cam::event::RecordingLimitReached) |
    event_bit(knst_cam::event::Disconnected)
);
```

**Why mask Frame?** Per-frame events are **very high frequency** (30 Hz, 60 Hz). Listening to them via the frame callback is more efficient.

### 6.3) Callbacks

```cpp
using frame_callback = void (*)(void* user, const frame_view& f);
using state_callback = void (*)(void* user, event e);
```

**Critical:** Callbacks run **on the capture thread**. Reentrancy rules:

**✅ Allowed:**
- `snapshot*()`
- `capture_latest()`
- `pause_recording()`, `resume_recording()`, `stop_recording()`
- `start_recording()`

**❌ Forbidden:**
- `start()`, `stop()`, `close()` → return `BadState`

---

## 🔄 7) Auto-Reconnect

### 7.1) In `open_options`

```cpp
struct open_options {
    // ...
    bool auto_reconnect = false;
    uint32_t reconnect_interval_ms = 1000;
    uint32_t reconnect_max_attempts = 0;   // 0 = unlimited
    uint32_t stall_timeout_ms = 5000;      // 0 = off
};
```

### 7.2) When is it triggered?

**Linux V4L2:**
- `poll()` `POLLERR/POLLHUP`
- `VIDIOC_DQBUF` → `ENODEV`, `EIO`, `ESHUTDOWN`, ...
- No frames for **stall_timeout_ms**

**Windows:**
- `ReadSample` FAILED (10 consecutive times)
- `MF_SOURCE_READERF_ENDOFSTREAM`
- Stall timeout

**Android:**
- `onDisconnected` callback
- `onError` callback

### 7.3) Recovery

1. `Disconnected` event
2. `platform_close_()` + `platform_open_()`
3. Requests **same format** (`strict_format = true`)
4. Success → `Reconnected` event
5. Fail → `reconnect_max_attempts` exhausted → `Error` + `streaming_ = false`

**Critical:** Disconnect during recording → **file finalized** (valid) → new stream begins.

### 7.4) Usage

```cpp
knst_cam::open_options o;
o.auto_reconnect = true;
o.reconnect_interval_ms = 2000;
o.reconnect_max_attempts = 30;
o.stall_timeout_ms = 3000;

cam.open(cam_info, o);
cam.start();
// USB unplug → auto-reopen
```

---

## 🌐 8) `camera_array` — Multi-Camera

### 8.1) API

```cpp
class camera_array {
public:
    result<void> open_all(const open_options& opt) noexcept;
    uint32_t count() const noexcept;
    camera& at(uint32_t i) noexcept;
    camera& operator[](uint32_t i) noexcept;
    void close_all() noexcept;
    result<knst_vector<knst_c16string>> snapshot_all(
        const knst_c16string& dir,
        const image_options& o = {}) noexcept;
};
```

### 8.2) Usage

```cpp
knst_cam::camera_array arr;
auto r = arr.open_all({});
if (!r) return;

// Start streaming on all cameras
for (uint32_t i = 0; i < arr.count(); ++i) {
    arr[i].start();
}

// Snapshot all at once
auto paths = arr.snapshot_all(u"snapshots/");
// → snapshots/cam0_20241112_...jpg
//   snapshots/cam1_20241112_...jpg
```

---

## 🖥️ 9) Platform-Specific Backends

### 9.1) Linux V4L2

**APIs used:**
- `VIDIOC_QUERYCAP` — device capabilities
- `VIDIOC_ENUM_FMT` — formats
- `VIDIOC_ENUM_FRAMESIZES` — sizes
- `VIDIOC_ENUM_FRAMEINTERVALS` — fps values
- `VIDIOC_S_FMT` / `VIDIOC_G_FMT` — set/get format
- `VIDIOC_S_PARM` — set fps
- `VIDIOC_REQBUFS` + `mmap` — buffer allocation
- `VIDIOC_QBUF` / `VIDIOC_DQBUF` — buffer queue
- `VIDIOC_STREAMON/OFF` — stream control
- `poll()` — frame wait (instead of select)
- `VIDIOC_QUERYCTRL` / `VIDIOC_G_CTRL` / `VIDIOC_S_CTRL` — controls

**Security:**
- **Path guard:** `realpath()` + `/dev/videoN` check
- **Char device check:** `fstat()` with `S_ISCHR`
- **Bound check:** every frame validated via `validate_frame_bytes()`

**Format selection:** `pick_format_index()` picks the nearest format. `strict_format = true` fails instead.

**Buffer count:** 2..32 (default 4).

**Stride handling:** If the driver's `bytesperline` is sane (padded rows), it's used. Otherwise `cur_stride_ = 0` (tightly packed).

### 9.2) Windows Media Foundation

**APIs used:**
- `MFStartup` / `MFShutdown` — COM init
- `MFEnumDeviceSources` — camera list
- `MFCreateSourceReaderFromMediaSource` — reader
- `IMFSourceReader::SetCurrentMediaType` — set format
- `IMFSourceReader::ReadSample` — read frame
- `IAMVideoProcAmp` — brightness, contrast, ...
- `IAMCameraControl` — exposure, focus, ...

**Supported subtypes:**
- `MFVideoFormat_MJPG` → MJPEG
- `MFVideoFormat_YUY2` → YUYV
- `MFVideoFormat_UYVY` → UYVY
- `MFVideoFormat_NV12` → NV12
- `MFVideoFormat_RGB32` → BGRA32
- `MFVideoFormat_ARGB32` → BGRA32
- `MFVideoFormat_RGB24` → BGR24

**Fallback:** If requested format unavailable → `MFVideoFormat_RGB32` tried.

**Controls:**
- **VideoProcAmp:** Brightness, Contrast, Saturation, Hue, Gain, Gamma, Sharpness, Backlight, WhiteBalance
- **CameraControl:** Exposure, Focus

**Fallback strategy:** If `Set(prop, v, manual)` fails → read current value → set manual → set `v` → verify with get.

**Note:** Windows has no `powered` concept — always `true` if adapter exists.

### 9.3) Android Camera2 NDK

**APIs used:**
- `ACameraManager_create` / `_delete`
- `ACameraManager_getCameraIdList`
- `ACameraMetadata_getConstEntry` — characteristics
- `ACameraManager_openCamera` — device
- `AImageReader_new` / `_newWithUsage` (API 26+)
- `AImageReader_setImageListener`
- `ACameraDevice_createCaptureRequest`
- `ACameraCaptureSession_setRepeatingRequest`

**Supported formats:**
- `AIMAGE_FORMAT_JPEG` → MJPEG (passthrough)
- `AIMAGE_FORMAT_YUV_420_888` → I420 (Y/U/V planes collected)
- `AIMAGE_FORMAT_RGBA_8888` → RGBA32
- `AIMAGE_FORMAT_RGBX_8888` → RGBA32

**Format list:** Parsed from `ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS` quads. Each entry is `(format, w, h, input)` 4-tuple. `input == 0` = output, `input != 0` = input (skipped).

**Highest fps:** Scanned from `ACAMERA_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES`.

**Controls (subset):**
- **Exposure** → `ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION` (EV compensation)
- **ExposureAuto** → `ACAMERA_CONTROL_AE_MODE` (ON/OFF)
- **FocusAuto** → `ACAMERA_CONTROL_AF_MODE` (CONTINUOUS_PICTURE/OFF)
- **WhiteBalanceAuto** → `ACAMERA_CONTROL_AWB_MODE` (AUTO/OFF)
- **Gain** → `ACAMERA_SENSOR_SENSITIVITY` (ISO)

**Pending controls:** Cached in `a_pending_ctrls_`. Applied when `a_request_` is created. During streaming, `set_control` → update `a_request_` + `setRepeatingRequest` retry.

**Note:** `platform_close_()` **doesn't clear Android-specific state** (`a_pending_ctrls_`, `a_disconnected_`). Those are cleared in `close()` — they must survive reconnect.

---

## ⚠️ 10) Common Gotchas

### 1) Camera is NOT movable/copyable

```cpp
knst_cam::camera a;
knst_cam::camera b = std::move(a);   // ❌ compile error
```

**Why?** A worker thread holds `this`. Moving would invalidate the pointer → crash.

**Fix:** Use `camera*` pointer or `unique_ptr<camera>`.

### 2) Callbacks run on the capture thread

```cpp
cam.start([](void* user, const knst_cam::frame_view& f) {
    // This is the capture thread
    // Don't call start/stop from another thread
    return;
});
```

**Rules:**
- **`snapshot*`**, **`capture_latest`**, **`pause/resume/stop_recording`**, **`start_recording`** → OK
- **`start`**, **`stop`**, **`close`** → return `BadState`

### 3) `grab()` doesn't work while streaming

```cpp
cam.start();
auto f = cam.grab();   // ❌ BadState
// ✅ cam.capture_latest()
```

**Why?** The stream is already running. `grab()` opens a separate stream → conflicts.

### 4) MJPEG → JPEG passthrough ignores `quality`

```cpp
image_options o;
o.jpeg_quality = 95;
// MJPEG camera + JPEG output + no edits → bit-exact, quality=95 unused
```

**To change quality:** set `allow_passthrough = false` or apply any edit.

### 5) MP4 doesn't support audio

```cpp
ro.path = u"clip.mp4";
ro.enable_audio = true;
// ❌ NotSupported
// ✅ ro.path = u"clip.mkv";
```

**MP4 writer is video only.**

### 6) MOV only supports PCM_S16LE

```cpp
ro.path = u"clip.mov";
ro.audio_codec = knst_vid::audio_codec::PCM_F32LE;
// ❌ NotSupported
// ✅ ro.audio_codec = knst_vid::audio_codec::PCM_S16LE;
```

### 7) No H.264/H.265 encoding

```cpp
ro.vcodec = knst_vid::video_codec::H264;
// ❌ NotSupported
// ✅ ro.vcodec = MJPEG;   // or RAW (AVI)
```

### 8) WebM can't be written

```cpp
ro.path = u"clip.webm";
// ❌ NotSupported
// ✅ ro.path = u"clip.mkv";   (same streams, same quality)
```

### 9) Recording limit → auto-closes

```cpp
ro.max_duration_ms = 10000;
cam.start_recording(ro);
// 10 seconds later → RecordingLimitReached event + recording stops
```

**To continue:** call `start_recording` again. Or use **`split_ms`** (auto-new segment).

### 10) Motion recording path

```cpp
mo.record.path = u"ignored.mkv";   // ❌ ignored
// Motion recorder builds the path itself:
//   <dir>/<prefix><timestamp>.<ext>
```

### 11) `open_options::strict_format`

```cpp
o.width = 1234;    // unsupported size
o.strict_format = true;
// ❌ NotSupported
// ✅ o.strict_format = false;   // nearest format auto-selected
```

### 12) `open_options::use_default_format`

```cpp
o.use_default_format = true;
// Driver's default format used (width/height/pix ignored)
```

**Use:** To open quickly without format negotiation.

### 13) Android `set_control` retry

On Android, `set_control` **during streaming** updates `a_request_` and retries `setRepeatingRequest`. On failure returns `IoError`.

**Fix:** Set before `stream_on_()` (queued as pending).

### 14) Windows `set_control` fallback

On Windows, if `Set(prop, v, manual)` fails:
1. Read current value
2. **Set current value with manual mode** (mode switch)
3. Then set `v`
4. Verify with get

**Why?** Some drivers require the current value to be set as `manual` before mode switch takes effect.

### 15) Windows `set_control_auto` with `VideoProcAmp_Flags_Auto`

`auto_on = true` → `VideoProcAmp_Flags_Auto`
`auto_on = false` → `VideoProcAmp_Flags_Manual`

Absolute control names like `Exposure`, `Focus`, `WhiteBalance` are accepted → mapped to their own auto.

### 16) `snapshot_burst` is blocking

`snapshot_burst(dir, 100, 100)` — 100 photos × 100ms ≈ 10 seconds **blocking**.

**No cancellation.** Use a separate thread for long bursts.

### 17) `snapshot_auto` counter wrap

`snap_counter_ % 10000` — wraps to 0 after 10,000. No collision unless 10,000 photos taken within the same second.

### 18) `recording_info().seconds`

Returns **last frame PTS**. 0 before the first frame.

### 19) `pause_recording` audio

The audio thread checks `rec_paused_.load()`. During pause, **audio chunks are discarded** (no timeline gap).

### 20) `roll_segment_locked_` continuity

New segment starts with `rec_t0_valid_ = false`. **The first frame** becomes the new segment's t0. **PTS starts from zero**.

**Result:** Segments are **independent** — each has its own `t=0` at its first frame.

### 21) `anchor_gen_` A/V sync

Video t0 → `anchor_gen_++` → audio thread takes new base. Pause/resume also does `anchor_gen_++`.

**Result:** Video pause → audio pause. Video resume → audio resume. **No A/V drift**.

### 22) Motion `min_interval_ms`

`mo.detector.min_interval_ms` — the motion detector's **minimum check interval**. Too low (0) = check every frame (slow). 100-500 ms recommended.

### 23) Motion `threshold` semantics

Depends on `knst_vid::motion_detector::config`. Usually **percentage of pixel change** (0.0..1.0).

### 24) Motion detection start order

```cpp
cam.enable_motion_detection(mo);   // Motion first
cam.start();                       // Then stream
```

**Reverse also works** but motion events start after the stream opens.

### 25) Motion auto_record failure

```cpp
cam.start_recording(mo.record);
// On failure motion_rec_started_ = false
// Next motion started retries
```

### 26) `event_mask` default

```cpp
default_event_mask() = 0xFFFFFFFF & ~(bit(Frame) | bit(FrameDropped))
```

**Why?** Per-frame events are high frequency. **The frame callback** handles them separately.

### 27) Android `AImageReader_new` fallback

**API 24/25:** `AImageReader_new` used (no usage flag). Fine for JPEG, may be problematic for YUV on some devices.

**API 26+:** `AImageReader_newWithUsage` with `AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN`.

### 28) Android YUV planar → interleaved

`AIMAGE_FORMAT_YUV_420_888` → `pixel_format::I420` (Y, U, V separate planes). The library packs them into **contiguous I420**.

**Stride handling:** `AImage_getPlaneRowStride` read separately per plane.

### 29) Windows `MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED`

**Runtime format change** (e.g. auto exposure change) → new `IMFMediaType` read, `cur_fmt_` updated.

**Result:** `validate_frame_bytes` validates with the correct layout.

### 30) Linux `V4L2_BUF_FLAG_ERROR`

**Corrupt frame** → `frames_corrupt_++`, `frames_dropped_++`, buffer returned to queue.

**Never reaches the user.**

### 31) Linux `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`

If frame timestamp is **monotonic**, `v4l2_ts_us()` used directly. Otherwise (`V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN`) → `now_us()` fallback.

### 32) Frame validation order

1. `size > 0 && size <= MAX_FRAME_BYTES`
2. `width, height <= MAX_DIMENSION`
3. MJPEG → `0xFF 0xD8` (SOI marker)
4. Others → `compute_layout()` + `size >= L.total`

**Fail → drop.**

### 33) `bytesperline` stride

On Linux, if the driver reports `bytesperline` and it's **sane** (matches `compute_layout`), `cur_stride_ = f.fmt.pix.bytesperline`. Otherwise 0 (tightly packed).

### 34) `record_options::vcodec` MJPEG→MJPEG copy

Camera MJPEG + `vcodec = MJPEG` → **packet copy**. DHT injected.

**`jpeg_quality` ignored** (bit-exact copy).

### 35) RAW(AVI) fallback

`vcodec = RAW` + camera MJPEG → decode RGB + write raw.

**Quality loss** — decoded, no re-encode.

### 36) MP4 writer `fill_gaps = true`

Video writer gap-fill active. Important for A/V sync.

**Why?** If frame timing drifts, writer **duplicates frames**.

### 37) `rec_owns_stream_` auto-manage

```cpp
cam.start_recording(ro);
// If not streaming → rec_owns_stream_ = true + start() called

cam.stop_recording();
// If rec_owns_stream_ true → stop() called
```

**If streaming was already active** → untouched.

### 38) `release_owned_stream_` (from capture thread)

`stop_recording` from worker loop has deadlock risk. `release_owned_stream_()` only sets `streaming_ = false` — the worker thread exits by itself.

### 39) `finalize_recording_` is idempotent

Multiple `stop_recording()` calls → first finalizes, rest are no-ops.

### 40) `with_suffix("clip.mkv", 3)` → `"clip_0003.mkv"`

**Extensionless file** (`"clip"`) → `"clip_0003"` (no extension).

### 41) `timestamp_string()` is local time

**Local timezone** used (`localtime_r`). For UTC, call `gmtime_r` yourself (library doesn't).

### 42) Path guard

All `path` parameters checked via `path_ok()`:
- Not empty
- ≤ 4096 chars
- No NUL chars

**Null byte injection blocked.**

### 43) UTF-8 vs UTF-16

`knst_c16string` used. `to_cpath()` converts to UTF-8. **On Windows wide chars** used (`_wfopen`).

### 44) `snapshot_auto` prefix

The prefix gets **`_` appended after the timestamp**, then the counter.

Format: `<prefix><timestamp>_<counter>.<ext>`

**Default prefix:** `u"photo_"`, `u"burst_"`.

### 45) `snapshot_memory` size

**Max frame bytes** checked (`KNST_CAMERA_MAX_FRAME_BYTES` = 256 MB). Exceeded → `TooLarge`.

### 46) PPM PGM

`grey = true` → PGM (`P5`), otherwise PPM (`P6`).

### 47) BMP `grey` handling

For BMP with `ch == 1` (grey), **expands to RGB** (24-bit BMP standard).

### 48) PNG stored deflate

**Uncompressed deflate** (stored blocks). **Large files** but zero dependency (no zlib/libpng).

**For smaller PNGs:** use JPEG or an external encoder.

### 49) CRC32 implementation

**Static table** (call_once-like). Initialized on first call.

### 50) `record_options::meta`

`knst_vid::metadata` — title, author, software, comment, copyright, date.

**If `software` empty:** `"KernelNucleusT knst_camera"` written.

### 51) `audio_source` empty

Default microphone used (`aud::audio::get_default(true)`).

**Monitors filtered** (`is_mon()` check).

### 52) `list_audio_sources()` monitor filter

```cpp
if (!d.is_input || d.name.contains(u".monitor") || d.name.contains(u"Monitor")) continue;
```

Loopback/monitor sources **invisible**.

### 53) Audio open failure = recording failure

If `open_audio_` fails, **recording doesn't start**. File not created.

### 54) Audio thread lifecycle

`start_recording` → `audio_worker_` spawned.
`stop_recording` → `audio_running_ = false` + `join_audio_()`.

**`join_audio_` self-join check:** Skipped if called from capture thread.

### 55) Audio `chunk_us` offset

Audio thread backs off by **`chunk_us`** each chunk (arrival - chunk_us). **Latency compensation**.

### 56) Audio-only `rec_t0_`

Set **immediately** at `start_recording` with `rec_t0_ = now_us()`. First audio chunk becomes `t0`.

### 57) Video-only `rec_t0_`

`rec_t0_valid_ = false` **until the first frame**. First frame sets `t0`.

### 58) Audio-only limits

Measured by audio clock:
```cpp
const uint64_t elapsed = rec_seg_offset_us_ + pts + chunk_us;
```

**No video** → goes through this path.

### 59) Segment roll timing

`rec_seg_offset_us_` accumulates. New segment starts `t0` from zero.

**Total elapsed** = `seg_offset + pts`.

### 60) `roll_segment_locked_` failure

`rec_writer_.open()` fails → `rec_open_ = false`. **No finalize** (already closed).

Next `record_video_locked_` → `rec_result::Error`.

---

## 📊 11) Performance

| Operation | Cost | Note |
|---|---|---|
| **enumerate** | 100-500 ms | Probe all devices |
| **open** | 50-200 ms | Device + format + mmap |
| **start** | 10-50 ms | Stream on + thread spawn |
| **frame capture (V4L2)** | ~1-33 ms/frame | Depends on fps |
| **frame capture (MF)** | ~1-33 ms/frame | Depends on fps |
| **snapshot MJPEG passthrough** | ~1-5 ms | Copy + DHT |
| **snapshot JPEG encode** | ~10-50 ms | 1080p |
| **snapshot PNG encode** | ~20-100 ms | Stored deflate, large file |
| **snapshot BMP** | ~5-20 ms | Raw copy |
| **grab (one-shot)** | 100-500 ms | Includes warmup frames |
| **start_recording** | 50-200 ms | Writer open + audio open |
| **record frame** | ~1-10 ms | Copy or encode |
| **stop_recording** | 10-100 ms | Writer close + segment finalize |

**Typical latencies:**
- **Camera enumerate:** 200-500 ms (V4L2)
- **Open 1080p MJPEG:** 100-200 ms
- **Frame delivery:** ~33 ms @ 30 fps
- **Snapshot passthrough:** ~2 ms
- **Snapshot JPEG 1080p:** ~15 ms
- **Recording start → first frame:** ~100 ms

**Optimizations:**
- **MJPEG passthrough** — zero re-encode
- **Streaming + capture_latest** — instead of grab()
- **Frame callback** — faster than per-frame events
- **Recording split** — segments instead of huge files

---

## ✅ 12) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Enumerate + open
auto cams = knst_cam::camera::enumerate();
if (!cams || cams.value.empty()) return 1;

knst_cam::camera cam;
auto r = cam.open(cams.value[0], o);
if (!r) {
    std::cerr << cam.last_error_string() << "\n";
    return 1;
}

// 2. Snapshot (without opening stream)
cam.snapshot(u"photo.jpg");

// 3. Don't edit for MJPEG passthrough
knst_cam::image_options io;
// No mirror/rotate/resize → bit-exact JPEG
cam.snapshot(u"fast.jpg", io);

// 4. Snapshot from callback
cam.start([](void* user, const knst_cam::frame_view& f) {
    auto* c = static_cast<knst_cam::camera*>(user);
    c->snapshot(u"frame.jpg");   // ✅ OK
}, &cam);

// 5. Motion auto-record
knst_cam::motion_options mo;
mo.auto_record = true;
mo.dir = u"motion/";
cam.enable_motion_detection(mo);

// 6. Auto-reconnect
o.auto_reconnect = true;
o.stall_timeout_ms = 5000;

// 7. Save/restore controls
auto saved = cam.save_controls();
// ... changes ...
cam.restore_controls(saved.value);

// 8. Recording limits
ro.max_duration_ms = 60000;   // 60 seconds
ro.split_ms = 30000;          // new segment every 30s

// 9. Use .mkv for audio
ro.path = u"clip.mkv";
ro.enable_audio = true;

// 10. camera_array
knst_cam::camera_array arr;
arr.open_all({});
arr.snapshot_all(u"snapshots/");
```

### ❌ Don't

```cpp
// 1. Don't move camera
knst_cam::camera a;
knst_cam::camera b = std::move(a);   // ❌ compile error

// 2. Don't call start/stop/close from callback
cam.start([](void* user, const knst_cam::frame_view& f) {
    auto* c = static_cast<knst_cam::camera*>(user);
    c->stop();   // ❌ BadState (deadlock risk)
    return;
});

// 3. grab() while streaming
cam.start();
auto f = cam.grab();   // ❌ BadState
// ✅ cam.capture_latest()

// 4. H.264 encode
ro.vcodec = knst_vid::video_codec::H264;   // ❌ NotSupported

// 5. WebM record
ro.path = u"clip.webm";   // ❌ NotSupported
// ✅ ro.path = u"clip.mkv";

// 6. MP4 + audio
ro.path = u"clip.mp4";
ro.enable_audio = true;   // ❌ NotSupported
// ✅ ro.path = u"clip.mkv";

// 7. MOV + PCM32
ro.path = u"clip.mov";
ro.audio_codec = knst_vid::audio_codec::PCM_F32LE;   // ❌ NotSupported
// ✅ PCM_S16LE

// 8. Motion record.path
mo.record.path = u"ignored.mkv";   // ❌ ignored
// Motion recorder builds the path itself

// 9. strict_format wrong
o.width = 1234;   // unsupported
o.strict_format = true;   // ❌ NotSupported
// ✅ o.strict_format = false;

// 10. Expect quality on MJPEG passthrough
knst_cam::image_options io;
io.jpeg_quality = 95;
cam.snapshot(u"photo.jpg", io);   // ❌ quality ignored
// ✅ io.allow_passthrough = false;
```

---

## 🔥 13) Real-World Examples

### Example 1: Time-lapse

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

for (int i = 0; i < 300; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "timelapse_%04d.jpg", i);
    auto r = cam.snapshot(knst_c16string(name));
    if (!r) break;
    std::this_thread::sleep_for(std::chrono::seconds(10));
}
```

### Example 2: Motion-triggered security camera

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

knst_cam::motion_options mo;
mo.detector.min_interval_ms = 200;
mo.detector.threshold = 0.05;
mo.post_roll_ms = 5000;
mo.auto_record = true;
mo.dir = u"security/";
mo.record.enable_audio = true;
mo.record.jpeg_quality = 85;
mo.record.max_duration_ms = 300000;   // max 5 minutes per recording

cam.enable_motion_detection(mo);
cam.start(
    nullptr, nullptr,
    [](void* user, knst_cam::event e) {
        if (e == knst_cam::event::MotionStarted) {
            std::cout << "Motion!\n";
        }
    },
    nullptr
);
```

### Example 3: Recording — pause/resume

```cpp
knst_cam::record_options ro;
ro.path = u"lecture.mkv";
ro.enable_audio = true;
ro.audio_rate = 48000;
ro.audio_channels = 2;

cam.start_recording(ro);

// Record 5 seconds
std::this_thread::sleep_for(std::chrono::seconds(5));

// Pause
cam.pause_recording();
std::this_thread::sleep_for(std::chrono::seconds(3));

// Resume
cam.resume_recording();
std::this_thread::sleep_for(std::chrono::seconds(5));

cam.stop_recording();
// lecture.mkv: 10-second video (3-second gap removed)
```

### Example 4: Control snapshot

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

// List all controls
auto cs = cam.controls();
if (cs) {
    for (auto& c : cs.value) {
        std::cout << c.name << " = " << c.current
                  << " [" << c.min << ".." << c.max << "]\n";
    }
}

// Brightness + Exposure manual
cam.set_control(knst_cam::control_type::Brightness, 128);
cam.set_control(knst_cam::control_type::Exposure, 100);
```

### Example 5: Scheduled photo

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

while (running) {
    // Photo every hour on the hour
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv; localtime_r(&tt, &tmv);
    if (tmv.tm_min == 0 && tmv.tm_sec == 0) {
        cam.snapshot_auto(u"hourly/");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
}
```

### Example 6: Recording + event monitoring

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

cam.set_event_mask(
    knst_cam::event_bit(knst_cam::event::RecordingStarted) |
    knst_cam::event_bit(knst_cam::event::RecordingStopped) |
    knst_cam::event_bit(knst_cam::event::RecordingLimitReached) |
    knst_cam::event_bit(knst_cam::event::Disconnected)
);

cam.start(nullptr, nullptr,
    [](void* user, knst_cam::event e) {
        std::cout << "Event: " << knst_cam::event_name(e) << "\n";
    }, nullptr);

knst_cam::record_options ro;
ro.path = u"long.mkv";
ro.max_duration_ms = 3600000;   // 1 hour
ro.split_ms = 600000;           // new file every 10 min

cam.start_recording(ro);
std::this_thread::sleep_for(std::chrono::hours(1));
cam.stop_recording();
```

### Example 7: Save/restore controls

```cpp
cam.open(cams.value[0], o);

// Save default values
auto saved = cam.save_controls();

// Try something
cam.set_control(knst_cam::control_type::Brightness, 50);
cam.set_control(knst_cam::control_type::Contrast, 100);

// ... work ...

// Restore
if (saved) cam.restore_controls(saved.value);
```

### Example 8: AUDIO-only recording

```cpp
knst_cam::record_options ro;
ro.path = u"audio_only.wav";
ro.enable_video = false;
ro.enable_audio = true;
ro.audio_rate = 44100;
ro.audio_channels = 1;
ro.audio_codec = knst_vid::audio_codec::PCM_S16LE;
ro.audio_remove_dc = true;

knst_cam::camera cam;
cam.start_recording(ro);

std::this_thread::sleep_for(std::chrono::seconds(30));
cam.stop_recording();
```

### Example 9: Android front/back camera selection

```cpp
#if defined(KNST_USING_PLATFORM_ANDROID)
auto cams = knst_cam::camera::enumerate();
for (auto& c : cams.value) {
    // On Android, name is "Front Camera" or "Back Camera"
    if (c.name.contains(u"Back")) {
        knst_cam::camera cam;
        cam.open(c, o);
        cam.snapshot(u"back.jpg");
    }
}
#endif
```

### Example 10: Snapshot on trigger

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

cam.start([](void* user, const knst_cam::frame_view& f) {
    auto* c = static_cast<knst_cam::camera*>(user);

    // If a condition is met inside the frame → snapshot
    // (e.g. brightness > threshold)
    if (/* condition */) {
        c->snapshot_auto(u"triggers/");
    }
}, &cam);
```

---

## 📌 14) API Contract Summary

```cpp
namespace knst_cam {

using knst_dev::status;
using knst_dev::result;
using knst_vid::pixel_format;

// ─── Enums ───
enum class errc : uint8_t { /* ... */ };
enum class control_type : uint8_t { /* ... */ };
enum class event : uint8_t { /* ... */ };
enum class image_format : uint8_t { Auto, JPEG, BMP, PNG, PPM };

const char* errc_string(errc) noexcept;
status to_status(errc) noexcept;
const char* control_type_name(control_type) noexcept;
const char* event_name(event) noexcept;
constexpr uint32_t event_bit(event) noexcept;
constexpr uint32_t default_event_mask() noexcept;

// ─── Structs ───
struct format { /* ... */ };
struct camera_info { /* ... */ };
struct control_info { /* ... */ };
struct control_value { /* ... */ };
struct open_options { /* ... */ };
struct frame { /* ... */ };
struct frame_view { /* ... */ };
struct image_options { /* ... */ };
struct record_options { /* ... */ };
struct motion_options { /* ... */ };
struct camera_stats { /* ... */ };
struct recording_status { /* ... */ };

using frame_callback = void (*)(void*, const frame_view&);
using state_callback = void (*)(void*, event);

// ─── Free functions ───
errc encode_frame(const frame&, const image_options&, knst_byte_string&, image_format&) noexcept;
result<void> save_frame(const frame&, const knst_c16string&, image_options = {}) noexcept;
result<void> save_frame_jpeg(const frame&, const knst_c16string&, int = 90) noexcept;
result<void> save_frame_bmp(const frame&, const knst_c16string&) noexcept;
result<void> save_frame_png(const frame&, const knst_c16string&) noexcept;

// ─── camera ───
class camera {
public:
    camera() noexcept;
    ~camera();
    // NOT movable/copyable
    camera(const camera&) = delete;
    camera& operator=(const camera&) = delete;
    camera(camera&&) = delete;
    camera& operator=(camera&&) = delete;

    // Static
    static result<knst_vector<camera_info>> enumerate() noexcept;
    static result<camera_info> default_camera() noexcept;
    static result<camera_info> find(const knst_c16string&) noexcept;
    static const char* backend_name() noexcept;
    static result<knst_vector<knst_c16string>> list_audio_sources() noexcept;

    // Lifecycle
    result<void> open(const camera_info&, const open_options& = {}) noexcept;
    result<void> open(const knst_c16string&, const open_options& = {}) noexcept;
    result<void> close() noexcept;
    bool is_open() const noexcept;
    bool is_streaming() const noexcept;
    bool is_recording() const noexcept;
    bool is_recording_paused() const noexcept;

    // Frames
    result<frame> grab(uint32_t skip_frames = UINT32_MAX, uint32_t timeout_ms = 0) noexcept;
    result<frame> capture_latest(uint32_t timeout_ms = 0) noexcept;

    // Streaming
    result<void> start(frame_callback = nullptr, void* = nullptr,
                        state_callback = nullptr, void* = nullptr) noexcept;
    result<void> stop() noexcept;
    void set_event_callback(state_callback, void*) noexcept;
    void set_event_mask(uint32_t) noexcept;

    // Recording
    result<void> start_recording(const record_options&) noexcept;
    result<void> stop_recording() noexcept;
    result<void> pause_recording() noexcept;
    result<void> resume_recording() noexcept;
    recording_status recording_info() const noexcept;

    // Snapshots
    result<void> snapshot(const knst_c16string&, const image_options& = {}) noexcept;
    result<void> snapshot_jpeg(const knst_c16string&, int = 90) noexcept;
    result<void> snapshot_bmp(const knst_c16string&) noexcept;
    result<void> snapshot_png(const knst_c16string&) noexcept;
    result<knst_byte_string> snapshot_memory(const image_options& = {}) noexcept;
    result<knst_c16string> snapshot_auto(const knst_c16string&, const knst_c16string& = u"photo_",
                                          const image_options& = {}) noexcept;
    result<knst_vector<knst_c16string>> snapshot_burst(const knst_c16string&, uint32_t, uint32_t,
                                                        const knst_c16string& = u"burst_",
                                                        const image_options& = {}) noexcept;

    // Motion
    result<void> enable_motion_detection(const motion_options&) noexcept;
    void disable_motion_detection() noexcept;
    bool is_motion_active() const noexcept;

    // Controls
    result<knst_vector<control_info>> controls() noexcept;
    result<control_info> get_control(control_type) noexcept;
    result<void> set_control(control_type, int32_t) noexcept;
    result<void> set_control_auto(control_type, bool) noexcept;
    result<void> reset_control(control_type) noexcept;
    result<void> reset_all_controls() noexcept;
    result<knst_vector<control_value>> save_controls() noexcept;
    result<void> restore_controls(const knst_vector<control_value>&) noexcept;

    // Info
    result<format> current_format() const noexcept;
    const knst_vector<format>& supported_formats() const noexcept;
    const camera_info& info() const noexcept;
    camera_stats stats() const noexcept;
    double effective_fps() const noexcept;
    uint64_t frames_captured/dropped/recorded() const noexcept;
    errc last_error() const noexcept;
    const char* last_error_string() const noexcept;
    knst_c16string last_message() const noexcept;
};

// ─── camera_array ───
class camera_array {
public:
    result<void> open_all(const open_options&) noexcept;
    uint32_t count() const noexcept;
    camera& at(uint32_t) noexcept;
    camera& operator[](uint32_t) noexcept;
    void close_all() noexcept;
    result<knst_vector<knst_c16string>> snapshot_all(const knst_c16string&,
                                                      const image_options& = {}) noexcept;
};

} // namespace knst_cam
```

---

## 🎯 What It Brings You

`knst_camera` gives you serious advantages in these situations:

- ✅ **3 platforms one API** — V4L2 / MF / Camera2
- ✅ **MJPEG passthrough** — zero re-encode, no quality loss
- ✅ **DHT injection** — for MJPEG streams missing Huffman tables
- ✅ **Bound check** — every frame validated, no OOB
- ✅ **poll() (no FD_SETSIZE overflow)**
- ✅ **Auto-reconnect** — USB unplug tolerated
- ✅ **Motion detection** — pixel-based motion
- ✅ **Motion auto-record** — record on motion
- ✅ **Segment split** — split large recordings
- ✅ **Pause/resume** — no timeline gaps
- ✅ **A/V sync** — `anchor_gen_` keeps video/audio aligned
- ✅ **Recording limits** — duration/frames/bytes
- ✅ **Thread-safe callback** — on capture thread
- ✅ **Reentrancy guard** — start/stop/close forbidden from callback
- ✅ **Partial finalization** — valid file on limit/disconnect
- ✅ **Format picker** — nearest format auto
- ✅ **UTF-16 path** — Windows compatible
- ✅ **Timestamped names** — `snapshot_auto`
- ✅ **Burst mode** — N photos
- ✅ **Free functions** — `save_frame` without camera
- ✅ **camera_array** — multi-camera
- ✅ **Controls** — brightness, contrast, exposure, focus, WB, gain, ...
- ✅ **Auto modes** — exposure/focus/WB
- ✅ **Save/restore** — control snapshots
- ✅ **`knst_video` integration** — writer, MJPEG, motion_detector
- ✅ **`knst_audio` integration** — audio recording
- ✅ **`knst_file` integration** — atomic write

---