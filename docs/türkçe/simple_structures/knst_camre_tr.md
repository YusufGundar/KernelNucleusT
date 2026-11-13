# knst_camera — Kamera Yakalama, Fotoğraf ve Kayıt Kütüphanesi

Selamlar! 👋 `knst_camera.hpp`, KernelNucleusT'nin **kamera yakalama, fotoğraf çekme ve ses+video kayıt** modülü. Tek header'da: V4L2 (Linux), Media Foundation (Windows), Camera2 NDK (Android) — hepsi tek API altında. Fotoğraf çekimi (JPEG/BMP/PNG/PPM), MJPEG passthrough, video+audio kayıt, motion-triggered capture, kamera kontrolleri, oto-reconnect.

**Kısaca:** **Synchronous, exception-free, RTTI yok.** Her public çağrı `result<T>` döner. **`camera` movable/copyable DEĞİL** — worker thread `this`'i tutar. **Callback'ler capture thread'inde çalışır** — `start()/stop()/close()` callback içinden çağrılamaz. Partial kayıtlar (limit/disconnect) **otomatik finalize edilir** (geçerli dosya).

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

### Platform matrisi

| Özellik | Linux (V4L2) | Windows (MF) | Android (Camera2) |
|---|---|---|---|
| **Kamera enumerate** | ✅ | ✅ | ✅ |
| **Format listesi** | ✅ | ✅ | ✅ |
| **Open/close** | ✅ | ✅ | ✅ |
| **Streaming** | ✅ | ✅ | ✅ |
| **Fotoğraf çekimi** | ✅ | ✅ | ✅ |
| **MJPEG passthrough** | ✅ | ✅ | ✅ |
| **JPEG/BMP/PNG/PPM çıktı** | ✅ | ✅ | ✅ |
| **Video kayıt (MJPEG/AVI, MKV, MP4, MOV)** | ✅ | ✅ | ✅ |
| **Ses kayıt (PCM)** | ✅ | ✅ | ✅ |
| **Audio-only kayıt (WAV/MKV)** | ✅ | ✅ | ✅ |
| **Pause/resume** | ✅ | ✅ | ✅ |
| **Limitler (süre/frame/byte)** | ✅ | ✅ | ✅ |
| **Segment split** | ✅ | ✅ | ✅ |
| **Motion-triggered kayıt** | ✅ | ✅ | ✅ |
| **Kamera kontrolleri** | ✅ (tam) | ✅ (VideoProcAmp + CameraControl) | ✅ (subset — AE/AF/AWB/EV/ISO) |
| **Auto modes** | ✅ | ✅ | ✅ (AE/AF/AWB) |
| **Reset/save/restore** | ✅ | ✅ | ✅ |
| **Oto-reconnect** | ✅ | ✅ | ✅ |
| **Uevent/hotplug** | ❌ | ❌ | ❌ |
| **RAW video (AVI only)** | ✅ | ✅ | ⚠️ |
| **H.264/H.265 ENCODER** | ❌ | ❌ | ❌ |
| **H.264/H.265 DECODER (kayıt için)** | ❌ | ❌ | ❌ |
| **WebM yazma** | ❌ | ❌ | ❌ |
| **MP4 ses** | ❌ | ❌ | ❌ |
| **AVI ses (non-PCM)** | ❌ | ❌ | ❌ |
| **MOV ses (non-PCM16)** | ❌ | ❌ | ❌ |
| **USB mikrofon enumerate** | ✅ (`list_audio_sources`) | ✅ | ✅ |

### Bağımlılıklar

| Bağımlılık | Ne için? | Versiyon |
|---|---|---|
| **`knst_video.hpp`** | Writer, MJPEG, motion_detector, pixel_format | dahili |
| **`knst_audio.hpp`** (opsiyonel) | Ses kayıt | dahili |
| **`knst_dev.hpp`** | `status`, `result`, `to_utf8` | dahili |
| **`knst_file.hpp`** | Path handling, `write_file_atomic` | dahili |

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **3 platform tek API** | V4L2 / MF / Camera2 |
| **MJPEG passthrough** | Fotoğraf çekiminde sıfır re-encode |
| **DHT injection** | Eksik Huffman tablosu olan MJPEG'ler için |
| **Bound check** | Her frame pixel layout'a karşı doğrulanır |
| **Buffer validation** | Truncated frame yok sayılır |
| **poll() (select yerine)** | FD_SETSIZE taşması yok |
| **Oto-reconnect** | USB çıkarılması → tekrar aç |
| **Motion detection** | Hareket algılama + oto kayıt |
| **Recording split** | Dosya boyutu / süre limitine göre bölme |
| **Pause/resume** | Timeline boşluğu yok |
| **Thread-safe callback** | Frame/event callback'ler capture thread'inde |
| **Reentrancy guard** | `start/stop/close` callback içinden yasak |
| **Partial finalization** | Limit/disconnect'te dosya geçerli |
| **Stop-signal-safe** | Race condition yok |
| **Format picker** | En yakın format otomatik seçilir |
| **UTF-16 path** | Windows uyumlu |
| **Timestamped names** | `snapshot_auto` → `photo_20241112_143025_123_0001.jpg` |
| **Burst mode** | N kare, M ms aralıkla |
| **Free functions** | `save_frame`, `save_frame_jpeg/bmp/png` |
| **camera_array** | Çoklu kamera yönetimi |

---

## 🚀 Hızlı Başlangıç

### Enumerate + open + snapshot

```cpp
#include "KernelNucleusT.hpp"

auto cams = knst_cam::camera::enumerate();
if (!cams || cams.value.empty()) {
    std::cerr << "Kamera yok\n";
    return 1;
}

knst_cam::open_options o;
o.width  = 1280;
o.height = 720;
o.pix    = knst_vid::pixel_format::MJPEG;
o.fps_num = 30;

knst_cam::camera cam;
cam.open(cams.value[0], o);

// Streaming olmadan bile fotoğraf çekilir
auto r = cam.snapshot(u"photo.jpg");
if (!r) {
    std::cerr << "Snapshot fail: " << cam.last_error_string() << "\n";
}
```

### Video + audio kayıt

```cpp
knst_cam::record_options ro;
ro.path = u"clip.mkv";           // container uzantıdan
ro.enable_audio = true;
ro.audio_rate = 48000;
ro.audio_channels = 2;
ro.jpeg_quality = 85;

auto r = cam.start_recording(ro);
if (!r) {
    std::cerr << "Kayıt başlatılamadı: "
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
        // Frame geldi — kopyala, işle, kaydet
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

### Motion-triggered kayıt

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
cam.start(nullptr, nullptr);   // Motion event'leri otomatik kayıt başlatır
```

---

## 📋 Genel API

| Kategori | Metodlar |
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

## 🎯 1) Hata Enum'u

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

| Err | Anlamı |
|---|---|
| **`Ok`** | Başarı |
| **`InvalidArgument`** | Geçersiz parametre |
| **`NotSupported`** | Bu platformda/cihazda yok |
| **`NotFound`** | Kamera / dosya yok |
| **`PermissionDenied`** | `/dev/video*` izni yok |
| **`IoError`** | Disk/dosya I/O |
| **`OutOfMemory`** | Bellek yetersiz |
| **`BadState`** | Yanlış state (ör. streaming sırasında `grab()`) |
| **`Corrupt`** | Bozuk frame/dosya |
| **`Timeout`** | Frame gelmedi |
| **`DeviceBusy`** | Cihaz başkası tarafından kullanılıyor |
| **`EndOfStream`** | Stream bitti |
| **`TooLarge`** | Boyut library limitini aşıyor |
| **`Disconnected`** | Kamera çıkarıldı |

---

## 🎛️ 2) Kamera Kontrolleri

### 2.1) `control_type` — Kontrol tipleri

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

| Tip | Linux V4L2 | Windows | Android |
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

### 2.2) `control_info` — Kontrol bilgisi

```cpp
struct control_info {
    control_type type = control_type::Unknown;
    int32_t min = 0, max = 0, step = 1, current = 0, default_value = 0;
    bool is_auto = false;        // Bu kontrol auto moda sahip mi?
    bool is_auto_on = false;     // Şu anda auto modda mı?
    bool is_writable = true;
    bool is_active = true;
    knst_c16string name;
};
```

### 2.3) `control_value` — Save/restore için

```cpp
struct control_value {
    control_type type = control_type::Unknown;
    int32_t value = 0;
};
```

### 2.4) Kullanım

```cpp
// Tüm kontrolleri listele
auto cs = cam.controls();
if (cs) {
    for (const auto& c : cs.value) {
        std::cout << c.name << ": "
                  << c.current << " (" << c.min << ".." << c.max << ")\n";
    }
}

// Brightness set (otomatik clamp + step align)
cam.set_control(knst_cam::control_type::Brightness, 128);

// Auto exposure aç/kapa
cam.set_control_auto(knst_cam::control_type::Exposure, true);

// Varsayılana döndür
cam.reset_control(knst_cam::control_type::Brightness);

// Tümünü sıfırla
cam.reset_all_controls();

// Save/restore
auto saved = cam.save_controls();
// ... bir şeyler değiştir ...
cam.restore_controls(saved.value);
```

### 2.5) Absolute değerler için auto'dan çık

**Kritik:** `Exposure`, `Focus`, `WhiteBalance` gibi absolute kontrolleri set etmek **otomatik olarak auto moddan çıkar** (V4L2'de):

```cpp
cam.set_control(knst_cam::control_type::Exposure, 100);
// → V4L2_CID_EXPOSURE_AUTO = MANUAL
// → V4L2_CID_EXPOSURE_ABSOLUTE = 100
```

Windows'ta da benzer: `VideoProcAmp_Flags_Manual` set edilir.

**Android'de:** AE/AF/AWB mode ayrı kontrol olarak set edilir. Absolute değer için `set_control_auto` + `set_control` kombinasyonu.

---

## 📸 3) Fotoğraf Çekimi

### 3.1) `image_options` — Görüntü seçenekleri

```cpp
struct image_options {
    image_format format = image_format::Auto;   // Auto → uzantıdan, bilinmiyorsa JPEG
    int jpeg_quality = 90;
    knst_vid::mjpeg::subsampling jpeg_sub = s420;
    bool mirror = false;
    bool flip_v = false;
    uint32_t rotate_deg = 0;                    // 0 / 90 / 180 / 270
    uint32_t out_w = 0, out_h = 0;              // biri → aspect korunur
    double scale = 0.0;
    bool grey = false;
    bool allow_passthrough = true;              // MJPEG → JPEG bit-exact
};
```

### 3.2) `image_format`

```cpp
enum class image_format : uint8_t { Auto, JPEG, BMP, PNG, PPM };
```

**Uzantı tespiti:**
- `.jpg`, `.jpeg` → JPEG
- `.bmp` → BMP
- `.png` → PNG
- `.ppm`, `.pgm` → PPM/PGM

### 3.3) Snapshot metodları

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

**Kamera zaten MJPEG veriyorsa** ve output JPEG ise, **bit-exact passthrough** yapılır. `jpeg_quality` **ignore** edilir.

**DHT injection:** Bazı kameralar JPEG'de **Huffman tablosu (DHT segmenti) göndermez** (optimize edilmiş). Bu durumda `inject_std_dht()` standart DHT tablosunu ekler — dosya **her oynatıcıda** açılır.

```cpp
image_options o;
o.allow_passthrough = true;   // default
// Kamera MJPEG ise ve edit yoksa → bit-exact JPEG
cam.snapshot(u"fast.jpg", o);
```

**Passthrough iptal etmek için** `allow_passthrough = false` veya **herhangi bir edit** (mirror, rotate, resize, grey) aç.

### 3.5) Streaming sırasında snapshot

`capture_latest()`:
- **Streaming sırasında** → son frame'i anında döner
- **Streaming yoksa** → `grab()` çağırır (one-shot capture + warmup)

### 3.6) `snapshot_auto` — timestamped isim

Format: `<prefix><timestamp>_<counter>.<ext>`

```
photo_20241112_143025_123_0001.jpg
```

**Bileşenler:**
- **`<prefix>`** — kullanıcı verir (`u"photo_"`)
- **`<timestamp>`** — `YYYYMMDD_HHMMSS_mmm`
- **`<counter>`** — 0..9999 döngü
- **`<ext>`** — format'a göre (`.jpg`, `.bmp`, `.png`, `.ppm`/`.pgm`)

### 3.7) `snapshot_burst`

N fotoğraf, M ms aralıkla (blocking). Snapshot süresi > interval_ms ise **beklemeden** bir sonraki çekime geçer.

```cpp
auto paths = cam.snapshot_burst(u"burst/", 10, 100);
// 10 fotoğraf, 100ms aralıkla
```

**Max count:** 10.000 (DoS guard).

### 3.8) Free functions (kamera olmadan)

```cpp
// frame'i herhangi bir formata encode et
errc encode_frame(const frame&, const image_options&, knst_byte_string&, image_format& used) noexcept;

// Tek frame'i dosyaya yaz
result<void> save_frame(const frame&, const knst_c16string& path,
                        image_options = {}) noexcept;
result<void> save_frame_jpeg(const frame&, const knst_c16string&, int quality = 90) noexcept;
result<void> save_frame_bmp(const frame&, const knst_c16string&) noexcept;
result<void> save_frame_png(const frame&, const knst_c16string&) noexcept;
```

**Kullanım:** Kamera olmadan JPEG/BMP/PNG yazmak için (örneğin, `knst_converter` frame extract sonrası).

---

## 🎥 4) Video + Ses Kayıt

### 4.1) `record_options` — Kayıt seçenekleri

```cpp
struct record_options {
    knst_c16string path;
    knst_vid::container cont = Unknown;    // uzantıdan
    bool enable_video = true;
    knst_vid::video_codec vcodec = MJPEG;  // sadece MJPEG veya RAW(AVI)
    int jpeg_quality = 85;
    knst_vid::mjpeg::subsampling jpeg_sub = s420;

    // Limitler
    uint32_t max_duration_ms = 0;   // 0 = sınırsız
    uint64_t max_frames = 0;
    uint64_t max_bytes = 0;
    uint32_t split_ms = 0;          // >0: her N ms'de yeni dosya
    uint64_t split_bytes = 0;       // >0: her N byte'ta yeni dosya

    // Ses
    bool enable_audio = false;
    uint32_t audio_rate = 48000;
    uint32_t audio_channels = 2;
    knst_vid::audio_codec audio_codec = PCM_S16LE;
    knst_c16string audio_source;    // boş = default mikrofon
    float audio_gain_db = 0.0f;
    bool audio_remove_dc = false;

    knst_vid::metadata meta;
};
```

### 4.2) Container uyumu

| Container | Video | Ses |
|---|---|---|
| **AVI** | MJPEG, RAW, H.264, H.265, VP8, VP9, AV1, MPEG4 | ✅ PCM |
| **MKV** | Her şey (RAW hariç) | ✅ Her şey |
| **MP4** | MJPEG, H.264, H.265 | ❌ **sadece video** |
| **MOV** | MJPEG, H.264, H.265 | ⚠️ **sadece PCM_S16LE** |
| **WAV** | ❌ | ✅ PCM (audio-only) |
| **WebM** | ❌ | ❌ |

**Uzantıdan tespit:** `.avi` → AVI, `.mkv` → MKV, `.mp4`/`.m4v` → MP4, `.mov` → MOV, `.wav` → WAV, `.webm` → WebM.

**Container override:** `ro.cont` set edilirse uzantı ignore edilir.

### 4.3) Video codec desteği

**Sadece MJPEG veya RAW (AVI only)** kayıt edilebilir:

```cpp
ro.vcodec = knst_vid::video_codec::MJPEG;    // ✅
ro.vcodec = knst_vid::video_codec::RAW;      // ✅ (sadece AVI)
ro.vcodec = knst_vid::video_codec::H264;     // ❌ NotSupported
```

**MJPEG kamera ise** → **zero re-encode**, packet copy. Diğer pixel format'lar → **MJPEG encode**.

### 4.4) Ses kayıt

```cpp
ro.enable_audio = true;
ro.audio_rate = 48000;
ro.audio_channels = 2;
ro.audio_codec = knst_vid::audio_codec::PCM_S16LE;   // PCM zorunlu

// Belirli mikrofon
ro.audio_source = u"alsa_input.pci-0000_00_1f.3.analog-stereo";

// Gain + DC removal
ro.audio_gain_db = 3.0f;
ro.audio_remove_dc = true;
```

**Desteklenen codec'ler:** `PCM_S16LE`, `PCM_S24LE`, `PCM_S32LE`, `PCM_F32LE`, `PCM_U8`.

**Container uyumu:**
- **AVI, MKV, WAV** → her PCM
- **MOV** → sadece `PCM_S16LE`
- **MP4** → ses yok

### 4.5) Pause/resume

```cpp
cam.pause_recording();    // Duraklat
// ... 5 saniye bekle ...
cam.resume_recording();   // Devam et
```

**Timeline boşluğu yok** — `pause_total_` süresi timestamp'ten çıkarılır. Kayıt **kaldığı yerden devam eder** gibi görünür.

### 4.6) Limitler

**`max_duration_ms`** — Toplam süre (sn). Aşılırsa `RecordingLimitReached` event + kayıt kapatılır.

**`max_frames`** — Toplam frame. Video zorunlu (`enable_video = true`).

**`max_bytes`** — Toplam byte (segmentler toplamı).

**Audio-only kayıt:** Limitler **audio clock**'a göre ölçülür.

### 4.7) Split

**`split_ms`** — Her N ms'de yeni dosya başlat:
```
clip_0001.mkv, clip_0002.mkv, clip_0003.mkv, ...
```

**`split_bytes`** — Her N byte'ta yeni dosya.

**Kombine:** İkisi de set edilirse **hangisi önce tetiklenirse** split olur.

### 4.8) `recording_status`

```cpp
struct recording_status {
    bool active = false, paused = false;
    bool has_video = false, has_audio = false;
    uint64_t frames = 0, bytes = 0;
    double seconds = 0.0;
    uint32_t segment = 0;
    knst_c16string path;    // current (veya son) dosya
};
```

**Kullanım:**
```cpp
auto s = cam.recording_info();
std::cout << "Segment: " << s.segment
          << ", bytes: " << s.bytes
          << ", seconds: " << s.seconds << "\n";
```

### 4.9) Auto-start stream

**`start_recording`** çağrıldığında **streaming yoksa** kütüphane kendi stream'ini başlatır (`rec_owns_stream_`).

```cpp
// Streaming yok
cam.start_recording(ro);   // → otomatik start()
// ...
cam.stop_recording();      // → otomatik stop()
```

**Not:** Streaming **zaten açıksa** stream'e dokunulmaz (recording bitse de stream devam eder).

---

## 🚶 5) Motion Detection

### 5.1) `motion_options`

```cpp
struct motion_options {
    knst_vid::motion_detector::config detector;
    uint32_t post_roll_ms = 3000;       // Hareket sonrası N ms daha bekle
    bool auto_record = false;           // Hareket → kayıt başlat
    knst_c16string dir;                 // auto_record için output dizin
    knst_c16string prefix = u"motion_";
    record_options record;              // Template (path ignore)
};
```

### 5.2) Kullanım

```cpp
knst_cam::motion_options mo;
mo.detector.min_interval_ms = 100;   // En sık 100ms'de bir kontrol
mo.detector.threshold = 0.05;        // %5 piksеl değişimi
mo.post_roll_ms = 3000;
mo.auto_record = true;
mo.dir = u"motion/";
mo.record.enable_audio = true;
mo.record.jpeg_quality = 85;

cam.enable_motion_detection(mo);
cam.start(
    nullptr, nullptr,                  // Frame cb yok
    [](void* user, knst_cam::event e) {
        if (e == knst_cam::event::MotionStarted) {
            std::cout << "Hareket başladı\n";
        } else if (e == knst_cam::event::MotionStopped) {
            std::cout << "Hareket bitti\n";
        }
    },
    nullptr
);

// Motion event'leri otomatik:
//   MotionStarted → start_recording(mo.record)
//   MotionStopped (post_roll sonrası) → stop_recording()

cam.disable_motion_detection();   // Kapat
```

### 5.3) `is_motion_active()`

```cpp
bool active = cam.is_motion_active();
```

Anlık motion durumu.

---

## 📡 6) Event Sistemi

### 6.1) `event` — Event tipleri

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
// Default: per-frame event'leri hariç her şey
constexpr uint32_t default_event_mask() noexcept;
// = 0xFFFFFFFF & ~(bit(Frame) | bit(FrameDropped))

// Özel maske
cam.set_event_mask(
    event_bit(knst_cam::event::RecordingStarted) |
    event_bit(knst_cam::event::RecordingStopped) |
    event_bit(knst_cam::event::Disconnected) |
    event_bit(knst_cam::event::Reconnected)
);
```

**Neden Frame mask'lendi?** Per-frame event **çok yüksek frekanslı** (30 Hz, 60 Hz). Frame callback ile ayrıca dinlemek daha verimli.

### 6.3) Callback'ler

```cpp
using frame_callback = void (*)(void* user, const frame_view& f);
using state_callback = void (*)(void* user, event e);
```

**Kritik:** Callback'ler **capture thread'inde** çalışır. Reentrancy kuralları:

**✅ Çağrılabilir:**
- `snapshot*()`
- `capture_latest()`
- `pause_recording()`, `resume_recording()`, `stop_recording()`
- `start_recording()`

**❌ Çağrılamaz:**
- `start()`, `stop()`, `close()` → `BadState` döner

---

## 🔄 7) Auto-Reconnect

### 7.1) `open_options` içinde

```cpp
struct open_options {
    // ...
    bool auto_reconnect = false;
    uint32_t reconnect_interval_ms = 1000;
    uint32_t reconnect_max_attempts = 0;   // 0 = sınırsız
    uint32_t stall_timeout_ms = 5000;      // 0 = kapalı
};
```

### 7.2) Ne zaman tetiklenir?

**Linux V4L2:**
- `poll()` `POLLERR/POLLHUP`
- `VIDIOC_DQBUF` → `ENODEV`, `EIO`, `ESHUTDOWN`, ...
- Frame **stall_timeout_ms** boyunca gelmediyse

**Windows:**
- `ReadSample` FAILED (üst üste 10 kez)
- `MF_SOURCE_READERF_ENDOFSTREAM`
- Stall timeout

**Android:**
- `onDisconnected` callback
- `onError` callback

### 7.3) Recovery

1. `Disconnected` event
2. `platform_close_()` + `platform_open_()`
3. **Aynı format** talep edilir (`strict_format = true`)
4. Başarı → `Reconnected` event
5. Fail → `reconnect_max_attempts` dolarsa `Error` + `streaming_ = false`

**Kritik:** Recording sırasında disconnect → **dosya finalize edilir** (geçerli kalır) → yeni stream başlar.

### 7.4) Kullanım

```cpp
knst_cam::open_options o;
o.auto_reconnect = true;
o.reconnect_interval_ms = 2000;
o.reconnect_max_attempts = 30;
o.stall_timeout_ms = 3000;

cam.open(cam_info, o);
cam.start();
// USB çıkarsa bile → otomatik tekrar aç
```

---

## 🌐 8) `camera_array` — Çoklu Kamera

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

### 8.2) Kullanım

```cpp
knst_cam::camera_array arr;
auto r = arr.open_all({});
if (!r) return;

// Tüm kameralar streaming
for (uint32_t i = 0; i < arr.count(); ++i) {
    arr[i].start();
}

// Tek seferde tüm kameralardan snapshot
auto paths = arr.snapshot_all(u"snapshots/");
// → snapshots/cam0_20241112_...jpg
//   snapshots/cam1_20241112_...jpg
```

---

## 🖥️ 9) Platform-Spesifik Backend'ler

### 9.1) Linux V4L2

**Kullandığı API'ler:**
- `VIDIOC_QUERYCAP` — device capabilities
- `VIDIOC_ENUM_FMT` — formatlar
- `VIDIOC_ENUM_FRAMESIZES` — boyutlar
- `VIDIOC_ENUM_FRAMEINTERVALS` — fps'ler
- `VIDIOC_S_FMT` / `VIDIOC_G_FMT` — format set/get
- `VIDIOC_S_PARM` — fps set
- `VIDIOC_REQBUFS` + `mmap` — buffer allocation
- `VIDIOC_QBUF` / `VIDIOC_DQBUF` — buffer queue
- `VIDIOC_STREAMON/OFF` — stream control
- `poll()` — frame bekleme (select yerine)
- `VIDIOC_QUERYCTRL` / `VIDIOC_G_CTRL` / `VIDIOC_S_CTRL` — kontroller

**Güvenlik:**
- **Path guard:** `realpath()` + `/dev/videoN` doğrulaması
- **Char device check:** `fstat()` ile `S_ISCHR`
- **Bound check:** Her frame `validate_frame_bytes()` ile doğrulanır

**Format seçimi:** `pick_format_index()` en yakın format seçer. `strict_format = true` ise fail eder.

**Buffer count:** 2..32 (default 4).

**Stride handling:** Driver'ın `bytesperline` değeri mantıklıysa kullanılır (padded row). Yoksa `cur_stride_ = 0` (tightly packed).

### 9.2) Windows Media Foundation

**Kullandığı API'ler:**
- `MFStartup` / `MFShutdown` — COM init
- `MFEnumDeviceSources` — kamera listesi
- `MFCreateSourceReaderFromMediaSource` — reader
- `IMFSourceReader::SetCurrentMediaType` — format set
- `IMFSourceReader::ReadSample` — frame okuma
- `IAMVideoProcAmp` — brightness, contrast, ...
- `IAMCameraControl` — exposure, focus, ...

**Desteklenen subtype'lar:**
- `MFVideoFormat_MJPG` → MJPEG
- `MFVideoFormat_YUY2` → YUYV
- `MFVideoFormat_UYVY` → UYVY
- `MFVideoFormat_NV12` → NV12
- `MFVideoFormat_RGB32` → BGRA32
- `MFVideoFormat_ARGB32` → BGRA32
- `MFVideoFormat_RGB24` → BGR24

**Fallback:** İstenen format yoksa → `MFVideoFormat_RGB32` denenir.

**Kontroller:**
- **VideoProcAmp:** Brightness, Contrast, Saturation, Hue, Gain, Gamma, Sharpness, Backlight, WhiteBalance
- **CameraControl:** Exposure, Focus

**Fallback stratejisi:** `Set(prop, v, manual)` başarısız olursa → mevcut değeri oku → manuel set → sonra tekrar `v` set → get ile doğrula.

**Not:** Windows'ta `powered` gibi adapter durumu yok — her zaman `true` (adapter varsa).

### 9.3) Android Camera2 NDK

**Kullandığı API'ler:**
- `ACameraManager_create` / `_delete`
- `ACameraManager_getCameraIdList`
- `ACameraMetadata_getConstEntry` — karakteristikler
- `ACameraManager_openCamera` — device
- `AImageReader_new` / `_newWithUsage` (API 26+)
- `AImageReader_setImageListener`
- `ACameraDevice_createCaptureRequest`
- `ACameraCaptureSession_setRepeatingRequest`

**Desteklenen formatlar:**
- `AIMAGE_FORMAT_JPEG` → MJPEG (passthrough)
- `AIMAGE_FORMAT_YUV_420_888` → I420 (Y/U/V planes toplanır)
- `AIMAGE_FORMAT_RGBA_8888` → RGBA32
- `AIMAGE_FORMAT_RGBX_8888` → RGBA32

**Format listesi:** `ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS` quad'ları parse edilir. Her entry `(format, w, h, input)` 4'lüsü. `input == 0` = output, `input != 0` = input (skip).

**En yüksek fps:** `ACAMERA_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES` taranır.

**Kontroller (subset):**
- **Exposure** → `ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION` (EV compensation)
- **ExposureAuto** → `ACAMERA_CONTROL_AE_MODE` (ON/OFF)
- **FocusAuto** → `ACAMERA_CONTROL_AF_MODE` (CONTINUOUS_PICTURE/OFF)
- **WhiteBalanceAuto** → `ACAMERA_CONTROL_AWB_MODE` (AUTO/OFF)
- **Gain** → `ACAMERA_SENSOR_SENSITIVITY` (ISO)

**Pending kontroller:** `a_pending_ctrls_` cache'lenir. `a_request_` oluşturulunca apply edilir. Streaming sırasında `set_control` → `a_request_` update + `setRepeatingRequest` retry.

**Not:** `platform_close_()` **Android-spesifik state'i temizlemez** (a_pending_ctrls_, a_disconnected_). Bunlar `close()` içinde temizlenir — reconnect sırasında korunması gerekir.

---

## ⚠️ 10) Sık Tuzaklar

### 1) Camera movable/copyable DEĞİL

```cpp
knst_cam::camera a;
knst_cam::camera b = std::move(a);   // ❌ compile error
```

**Neden?** Worker thread `this`'i tutar. Move sırasında pointer invalidate olur → crash.

**Çözüm:** `camera*` pointer'ı veya `unique_ptr<camera>` kullan.

### 2) Callback'ler capture thread'inde

```cpp
cam.start([](void* user, const knst_cam::frame_view& f) {
    // Bu thread capture thread
    // Başka thread'den start/stop çağırma
    return;
});
```

**Kurallar:**
- **`snapshot*`**, **`capture_latest`**, **`pause/resume/stop_recording`**, **`start_recording`** → OK
- **`start`**, **`stop`**, **`close`** → `BadState` döner

### 3) `grab()` streaming sırasında çalışmaz

```cpp
cam.start();
auto f = cam.grab();   // ❌ BadState
// ✅ cam.capture_latest()
```

**Neden?** Stream zaten çalışıyor. `grab()` ayrı stream açar → çakışır.

### 4) MJPEG → JPEG passthrough `quality` ignore eder

```cpp
image_options o;
o.jpeg_quality = 95;
// MJPEG kamera + JPEG output + edit yok → bit-exact, quality=95 kullanılmaz
```

**Kalite değiştirmek için:** `allow_passthrough = false` veya herhangi bir edit uygula.

### 5) MP4 ses desteklemez

```cpp
ro.path = u"clip.mp4";
ro.enable_audio = true;
// ❌ NotSupported
// ✅ ro.path = u"clip.mkv";
```

**MP4 writer video only.**

### 6) MOV sadece PCM_S16LE

```cpp
ro.path = u"clip.mov";
ro.audio_codec = knst_vid::audio_codec::PCM_F32LE;
// ❌ NotSupported
// ✅ ro.audio_codec = knst_vid::audio_codec::PCM_S16LE;
```

### 7) H.264/H.265 encode yok

```cpp
ro.vcodec = knst_vid::video_codec::H264;
// ❌ NotSupported
// ✅ ro.vcodec = MJPEG;   // veya RAW (AVI)
```

### 8) WebM yazılamaz

```cpp
ro.path = u"clip.webm";
// ❌ NotSupported
// ✅ ro.path = u"clip.mkv";   (aynı stream'ler, aynı kalite)
```

### 9) Recording limit → otomatik kapanır

```cpp
ro.max_duration_ms = 10000;
cam.start_recording(ro);
// 10 saniye sonra → RecordingLimitReached event + kayıt durur
```

**Devam etmek için:** Yeni `start_recording` çağır. Veya **`split_ms`** kullan (otomatik yeni segment).

### 10) Motion recording path

```cpp
mo.record.path = u"ignored.mkv";   // ❌ ignore
// Motion recorder path'i kendisi oluşturur:
//   <dir>/<prefix><timestamp>.<ext>
```

### 11) `open_options::strict_format`

```cpp
o.width = 1234;    // desteklenmeyen boyut
o.strict_format = true;
// ❌ NotSupported
// ✅ o.strict_format = false;   // en yakın format otomatik seçilir
```

### 12) `open_options::use_default_format`

```cpp
o.use_default_format = true;
// Driver'ın default format'ı kullanılır (width/height/pix ignore)
```

**Kullanım:** Format denemesi yapmadan hızlı açmak için.

### 13) Android `set_control` retry

Android'de `set_control` **streaming sırasında** `a_request_` update eder + `setRepeatingRequest` retry yapar. Başarısızsa `IoError`.

**Çözümü:** `stream_on_()` öncesi set et (pending queue'ya eklenir).

### 14) Windows `set_control` fallback

Windows'ta `Set(prop, v, manual)` başarısız olursa:
1. Mevcut değeri oku
2. **Manuel mod ile** mevcut değeri set (mode değiştir)
3. Sonra `v` set et
4. Get ile doğrula

**Neden?** Bazı driver'lar modu değiştirmek için önce mevcut değerin `manual` olarak set edilmesini ister.

### 15) Windows `set_control_auto` `VideoProcAmp_Flags_Auto`

`auto_on = true` → `VideoProcAmp_Flags_Auto`
`auto_on = false` → `VideoProcAmp_Flags_Manual`

`Exposure`, `Focus`, `WhiteBalance` gibi absolute kontrol isimleri kabul edilir → kendi auto'suna map edilir.

### 16) `snapshot_burst` blocking

`snapshot_burst(dir, 100, 100)` — 100 fotoğraf × 100ms aralıkla ≈ 10 saniye **blocking**.

**İptal yok.** Uzun burst'ler için ayrı thread kullan.

### 17) `snapshot_auto` counter wrap

`snap_counter_ % 10000` — 10.000 sonra **0'a döner**. Aynı saniye içinde 10.000 fotoğraf çekilmediği sürece **collision yok**.

### 18) `recording_info().seconds`

**Son frame PTS'i** döner. Frame gelmeden önce 0.

### 19) `pause_recording` audio

Audio thread `rec_paused_.load()` kontrol eder. Pause sırasında **audio chunk discard edilir** (timeline'da boşluk yok).

### 20) `roll_segment_locked_` continuity

Yeni segment `rec_t0_valid_ = false` ile başlar. **İlk frame** yeni segment'in t0'ı olur. **PTS sıfırdan** başlar.

**Sonuç:** Segment'ler **bağımsız** — her birinin `t=0`'ı kendi ilk frame'i.

### 21) `anchor_gen_` A/V sync

Video t0 → `anchor_gen_++` → audio thread yeni base alır. Pause/resume da `anchor_gen_++` yapar.

**Sonuç:** Video pause → audio pause. Video resume → audio resume. **A/V drift yok**.

### 22) Motion `min_interval_ms`

`mo.detector.min_interval_ms` — motion detector'ın **minimum kontrol aralığı**. Çok düşük (0) = her frame kontrol (yavaş). 100-500 ms önerilir.

### 23) Motion `threshold` semantiği

`knst_vid::motion_detector::config`'e bağlı. Genellikle **piksel değişim yüzdesi** (0.0..1.0).

### 24) Motion detection başlatma sırası

```cpp
cam.enable_motion_detection(mo);   // Önce motion
cam.start();                       // Sonra stream
```

**Tersi de olur** ama motion event'leri stream açıldıktan sonra başlar.

### 25) Motion auto_record fail

```cpp
cam.start_recording(mo.record);
// Fail ederse motion_rec_started_ = false
// Sonraki motion started tekrar dener
```

### 26) `event_mask` default

```cpp
default_event_mask() = 0xFFFFFFFF & ~(bit(Frame) | bit(FrameDropped))
```

**Neden?** Per-frame event'ler yüksek frekanslı. **Frame callback** zaten ayrı.

### 27) Android `AImageReader_new` fallback

**API 24/25:** `AImageReader_new` kullanılır (usage flag yok). JPEG için OK, bazı cihazlarda YUV için problem olabilir.

**API 26+:** `AImageReader_newWithUsage` ile `AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN`.

### 28) Android YUV planar → interleaved

`AIMAGE_FORMAT_YUV_420_888` → `pixel_format::I420` (Y, U, V separate planes). Kütüphane bunları **contiguous I420** olarak paketler.

**Stride handling:** `AImage_getPlaneRowStride` her plane için ayrı okunur.

### 29) Windows `MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED`

**Runtime format değişimi** (ör. otomatik exposure değişikliği) → yeni `IMFMediaType` alınır, `cur_fmt_` güncellenir.

**Sonuç:** `validate_frame_bytes` doğru layout ile doğrular.

### 30) Linux `V4L2_BUF_FLAG_ERROR`

**Frame corrupt** → `frames_corrupt_++`, `frames_dropped_++`, buffer geri queue'ya.

**Kullanıcıya gitmez.**

### 31) Linux `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`

Frame timestamp **monotonic** ise `v4l2_ts_us()` direkt kullanılır. Diğer tipte (`V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN`) → `now_us()` fallback.

### 32) Frame validation öncelik sırası

1. `size > 0 && size <= MAX_FRAME_BYTES`
2. `width, height <= MAX_DIMENSION`
3. MJPEG → `0xFF 0xD8` (SOI marker)
4. Diğer → `compute_layout()` + `size >= L.total`

**Fail → drop.**

### 33) `bytesperline` stride

Linux'ta driver `bytesperline` veriyorsa ve **sane** ise (`compute_layout` ile uyumluysa) `cur_stride_ = f.fmt.pix.bytesperline`. Yoksa 0 (tightly packed).

### 34) `record_options::vcodec` MJPEG→MJPEG copy

Kamera MJPEG + `vcodec = MJPEG` → **packet copy**. DHT inject edilir.

**`jpeg_quality` ignore** (bit-exact copy).

### 35) RAW(AVI) fallback

`vcodec = RAW` + kamera MJPEG → decode RGB + write raw.

**Kalite kaybı** — decode edilmiş, tekrar encode yok.

### 36) MP4 writer `fill_gaps = true`

Video writer gap fill aktif. A/V sync için önemli.

**Neden?** Frame timing drift varsa writer **duplicate frame** ekler.

### 37) `rec_owns_stream_` auto-manage

```cpp
cam.start_recording(ro);
// Streaming yoksa → rec_owns_stream_ = true + start() çağrılır

cam.stop_recording();
// rec_owns_stream_ true ise → stop() çağrılır
```

**Streaming zaten açıksa** → dokunulmaz.

### 38) `release_owned_stream_` (capture thread'den)

Worker loop'tan `stop_recording` sırasında deadlock riski var. `release_owned_stream_()` sadece `streaming_ = false` set eder — worker thread kendisi biter.

### 39) `finalize_recording_` idempotent

Birden fazla `stop_recording()` çağrısı → ilki finalize eder, sonrakiler no-op.

### 40) `with_suffix("clip.mkv", 3)` → `"clip_0003.mkv"`

**Uzantısız dosya** (`"clip"`) → `"clip_0003"` (uzantı yok).

### 41) `timestamp_string()` local time

**Local timezone** kullanır (`localtime_r`). UTC istiyorsan `gmtime_r` çağır (kütüphane bunu yapmaz).

### 42) Path guard

Tüm `path` parametreleri `path_ok()` ile kontrol edilir:
- Boş değil
- Max 4096 karakter
- NUL char yok

**Null byte injection engellenir.**

### 43) UTF-8 vs UTF-16

`knst_c16string` kullanılır. `to_cpath()` ile UTF-8'e çevrilir. **Windows'ta wide char** kullanılır (`_wfopen`).

### 44) `snapshot_auto` prefix

Prefix'e **timestamp sonrası `_` eklenir**, sonra counter.

Format: `<prefix><timestamp>_<counter>.<ext>`

**Default prefix:** `u"photo_"`, `u"burst_"`.

### 45) `snapshot_memory` boyut

**Max frame bytes** kontrolü var (`KNST_CAMERA_MAX_FRAME_BYTES` = 256 MB). Aşılırsa `TooLarge`.

### 46) PPM PGM

`grey = true` ise PGM (`P5`), değilse PPM (`P6`).

### 47) BMP `grey` handling

BMP'de `ch == 1` (grey) durumunda **RGB'ye expand** edilir (BMP 24-bit standart).

### 48) PNG stored deflate

**Uncompressed deflate** kullanılır (stored blocks). **Dosya büyük** ama zero dependency (zlib/libpng yok).

**Küçük PNG için:** JPEG kullan veya external encoder.

### 49) CRC32 implementation

**Static table** (call_once-like). İlk çağrıda initialize edilir.

### 50) `record_options::meta`

`knst_vid::metadata` — title, author, software, comment, copyright, date.

**`software` boşsa:** `"KernelNucleusT knst_camera"` yazılır.

### 51) `audio_source` empty

Default microphone kullanılır (`aud::audio::get_default(true)`).

**Monitor'lar filtrelenir** (`is_mon()` kontrol).

### 52) `list_audio_sources()` monitor filter

```cpp
if (!d.is_input || d.name.contains(u".monitor") || d.name.contains(u"Monitor")) continue;
```

Loopback/monitor kaynaklar **görünmez**.

### 53) Audio open fail = recording fail

`open_audio_` başarısız olursa **recording başlamaz**. Dosya create edilmez.

### 54) Audio thread lifecycle

`start_recording` → `audio_worker_` spawn.
`stop_recording` → `audio_running_ = false` + `join_audio_()`.

**`join_audio_` self-join kontrolü:** Capture thread'den çağrılırsa skip.

### 55) Audio `chunk_us` offset

Audio thread her chunk'ta **son `chunk_us` kadar geriden** hesaplar (arrival - chunk_us). **Latency compensation**.

### 56) Audio-only `rec_t0_`

`start_recording` sırasında **hemen** `rec_t0_ = now_us()` set edilir. İlk audio chunk `t0` olur.

### 57) Video-only `rec_t0_`

**İlk frame gelene kadar** `rec_t0_valid_ = false`. İlk frame `t0`.

### 58) Audio-only limits

Audio clock ile ölçülür:
```cpp
const uint64_t elapsed = rec_seg_offset_us_ + pts + chunk_us;
```

**Video yoksa** bu yoldan geçer.

### 59) Segment roll timing

`rec_seg_offset_us_` birikir. Yeni segment `t0` sıfırdan başlar.

**Toplam elapsed** = `seg_offset + pts`.

### 60) `roll_segment_locked_` failure

`rec_writer_.open()` fail → `rec_open_ = false`. **Finalize yok** (zaten kapanmış).

Sonraki `record_video_locked_` → `rec_result::Error`.

---

## 📊 11) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| **enumerate** | 100-500 ms | Tüm cihazları probe |
| **open** | 50-200 ms | Device + format + mmap |
| **start** | 10-50 ms | Stream on + thread spawn |
| **frame capture (V4L2)** | ~1-33 ms/frame | fps'e bağlı |
| **frame capture (MF)** | ~1-33 ms/frame | fps'e bağlı |
| **snapshot MJPEG passthrough** | ~1-5 ms | Copy + DHT |
| **snapshot JPEG encode** | ~10-50 ms | 1080p |
| **snapshot PNG encode** | ~20-100 ms | Stored deflate, büyük dosya |
| **snapshot BMP** | ~5-20 ms | Raw copy |
| **grab (one-shot)** | 100-500 ms | Warmup frames dahil |
| **start_recording** | 50-200 ms | Writer open + audio open |
| **record frame** | ~1-10 ms | Copy veya encode |
| **stop_recording** | 10-100 ms | Writer close + segment finalize |

**Tipik latencies:**
- **Camera enumerate:** 200-500 ms (V4L2)
- **Open 1080p MJPEG:** 100-200 ms
- **Frame delivery:** ~33 ms @ 30 fps
- **Snapshot passthrough:** ~2 ms
- **Snapshot JPEG 1080p:** ~15 ms
- **Recording start → first frame:** ~100 ms

**Optimizasyon:**
- **MJPEG passthrough** — sıfır re-encode
- **Streaming + capture_latest** — grab() yerine
- **Frame callback** — per-frame event'lerden daha hızlı
- **Recording split** — büyük dosyalar yerine segment'ler

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

// 2. Snapshot (stream açmadan)
cam.snapshot(u"photo.jpg");

// 3. MJPEG passthrough için edit yapma
knst_cam::image_options io;
// mirror/rotate/resize YOK → bit-exact JPEG
cam.snapshot(u"fast.jpg", io);

// 4. Callback içinden snapshot
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
// ... değişiklikler ...
cam.restore_controls(saved.value);

// 8. Recording limits
ro.max_duration_ms = 60000;   // 60 saniye
ro.split_ms = 30000;          // her 30 sn'de yeni segment

// 9. Audio için .mkv kullan
ro.path = u"clip.mkv";
ro.enable_audio = true;

// 10. camera_array
knst_cam::camera_array arr;
arr.open_all({});
arr.snapshot_all(u"snapshots/");
```

### ❌ Yapma

```cpp
// 1. Camera'yı move etme
knst_cam::camera a;
knst_cam::camera b = std::move(a);   // ❌ compile error

// 2. Callback içinden start/stop/close
cam.start([](void* user, const knst_cam::frame_view& f) {
    auto* c = static_cast<knst_cam::camera*>(user);
    c->stop();   // ❌ BadState (deadlock riski)
    return;
});

// 3. grab() streaming sırasında
cam.start();
auto f = cam.grab();   // ❌ BadState
// ✅ cam.capture_latest()

// 4. H.264 encode
ro.vcodec = knst_vid::video_codec::H264;   // ❌ NotSupported

// 5. WebM kayıt
ro.path = u"clip.webm";   // ❌ NotSupported
// ✅ ro.path = u"clip.mkv";

// 6. MP4 + ses
ro.path = u"clip.mp4";
ro.enable_audio = true;   // ❌ NotSupported
// ✅ ro.path = u"clip.mkv";

// 7. MOV + PCM32
ro.path = u"clip.mov";
ro.audio_codec = knst_vid::audio_codec::PCM_F32LE;   // ❌ NotSupported
// ✅ PCM_S16LE

// 8. Motion record.path
mo.record.path = u"ignored.mkv";   // ❌ ignore edilir
// Motion recorder path'i kendisi oluşturur

// 9. strict_format yanlış
o.width = 1234;   // desteklenmeyen
o.strict_format = true;   // ❌ NotSupported
// ✅ o.strict_format = false;

// 10. Snapshot MJPEG passthrough sırasında kalite bekle
knst_cam::image_options io;
io.jpeg_quality = 95;
cam.snapshot(u"photo.jpg", io);   // ❌ quality ignore
// ✅ io.allow_passthrough = false;
```

---

## 🔥 13) Gerçek Kullanım Örnekleri

### Örnek 1: Time-lapse

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

### Örnek 2: Motion-triggered güvenlik kamerası

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
mo.record.max_duration_ms = 300000;   // max 5 dakika/kayıt

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

### Örnek 3: Kamera kaydı — pause/resume

```cpp
knst_cam::record_options ro;
ro.path = u"lecture.mkv";
ro.enable_audio = true;
ro.audio_rate = 48000;
ro.audio_channels = 2;

cam.start_recording(ro);

// 5 saniye kayıt
std::this_thread::sleep_for(std::chrono::seconds(5));

// Duraklat
cam.pause_recording();
std::this_thread::sleep_for(std::chrono::seconds(3));

// Devam
cam.resume_recording();
std::this_thread::sleep_for(std::chrono::seconds(5));

cam.stop_recording();
// lecture.mkv: 10 saniyelik video (3 saniyelik boşluk çıkarıldı)
```

### Örnek 4: Ayar snapshot

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

// Tüm kontrolleri listele
auto cs = cam.controls();
if (cs) {
    for (auto& c : cs.value) {
        std::cout << c.name << " = " << c.current
                  << " [" << c.min << ".." << c.max << "]\n";
    }
}

// Brightness + Exposure manuel
cam.set_control(knst_cam::control_type::Brightness, 128);
cam.set_control(knst_cam::control_type::Exposure, 100);
```

### Örnek 5: Scheduled photo

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

while (running) {
    // Her saat başı fotoğraf
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

### Örnek 6: Recording + event monitoring

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
ro.max_duration_ms = 3600000;   // 1 saat
ro.split_ms = 600000;           // her 10 dakika yeni dosya

cam.start_recording(ro);
std::this_thread::sleep_for(std::chrono::hours(1));
cam.stop_recording();
```

### Örnek 7: Save/restore controls

```cpp
cam.open(cams.value[0], o);

// Varsayılan değerleri sakla
auto saved = cam.save_controls();

// Bir şeyler dene
cam.set_control(knst_cam::control_type::Brightness, 50);
cam.set_control(knst_cam::control_type::Contrast, 100);

// ... iş ...

// Eski değerlere dön
if (saved) cam.restore_controls(saved.value);
```

### Örnek 8: AUDIO-only recording

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

### Örnek 9: Android front/back kamera seçimi

```cpp
#if defined(KNST_USING_PLATFORM_ANDROID)
auto cams = knst_cam::camera::enumerate();
for (auto& c : cams.value) {
    // Android'de name "Front Camera" veya "Back Camera"
    if (c.name.contains(u"Back")) {
        knst_cam::camera cam;
        cam.open(c, o);
        cam.snapshot(u"back.jpg");
    }
}
#endif
```

### Örnek 10: Snapshot on trigger

```cpp
knst_cam::camera cam;
cam.open(cams.value[0], o);

cam.start([](void* user, const knst_cam::frame_view& f) {
    auto* c = static_cast<knst_cam::camera*>(user);
    
    // Frame içinde belirli bir koşul → snapshot
    // (örneğin brightness > threshold)
    if (/* koşul */) {
        c->snapshot_auto(u"triggers/");
    }
}, &cam);
```

---

## 📌 14) API Kontrat Özeti

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

## 🎯 Size Getireceği Kazanç

`knst_camera` şu durumlarda ciddi avantaj sağlar:

- ✅ **3 platform tek API** — V4L2 / MF / Camera2
- ✅ **MJPEG passthrough** — zero re-encode, kalite kaybı yok
- ✅ **DHT injection** — eksik Huffman tablosu olan MJPEG'ler için
- ✅ **Bound check** — her frame validation, OOB yok
- ✅ **poll() (FD_SETSIZE taşması yok)**
- ✅ **Oto-reconnect** — USB çıkarılması tolere edilir
- ✅ **Motion detection** — piksel bazlı hareket algılama
- ✅ **Motion auto-record** — hareket olunca kayıt
- ✅ **Segment split** — büyük kayıtları parçala
- ✅ **Pause/resume** — timeline boşluğu yok
- ✅ **A/V sync** — `anchor_gen_` ile video/audio hizalı
- ✅ **Recording limits** — süre/frame/byte
- ✅ **Thread-safe callback** — capture thread'de
- ✅ **Reentrancy guard** — start/stop/close callback içinden yasak
- ✅ **Partial finalization** — limit/disconnect'te dosya geçerli
- ✅ **Format picker** — en yakın format otomatik
- ✅ **UTF-16 path** — Windows uyumlu
- ✅ **Timestamped names** — `snapshot_auto`
- ✅ **Burst mode** — N fotoğraf
- ✅ **Free functions** — `save_frame` kamera olmadan
- ✅ **camera_array** — çoklu kamera
- ✅ **Controls** — brightness, contrast, exposure, focus, WB, gain, ...
- ✅ **Auto modes** — exposure/focus/WB
- ✅ **Save/restore** — kontrol snapshot
- ✅ **`knst_video` entegrasyonu** — writer, MJPEG, motion_detector
- ✅ **`knst_audio` entegrasyonu** — ses kayıt
- ✅ **`knst_file` entegrasyonu** — atomic write

---