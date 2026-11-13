// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_camera.hpp
----------------------------
    Camera capture, photo snapshots and audio/video recording for KernelNucleusT.

    FEATURES
    ========
      Photos
        - snapshot() to JPEG / BMP / PNG / PPM (+PGM), format from extension
        - JPEG passthrough for MJPEG cameras (DHT tables injected, so the file
          opens everywhere), no re-encode / no quality loss
        - mirror, flip, rotate 90/180/270, resize, grey, JPEG quality/subsampling
        - snapshot_auto() (timestamped names), snapshot_burst(), snapshot_memory()
        - works while streaming (fresh frame) or idle (one-shot grab + warm-up)
      Recording
        - video+audio, video only, or audio only
        - AVI / MKV / MP4 / MOV (video), MKV / WAV (audio only); container is
          taken from the file extension unless record_options::cont is set
        - MJPEG cameras are written as untouched packets (zero re-encode);
          other pixel formats are encoded to MJPEG (or RAW in AVI)
        - audio: PCM S16/S24/S32/F32/U8, mic selection, gain, DC removal,
          A/V sync anchored to the video clock
        - pause / resume without timeline gaps
        - limits (duration / frames / bytes) and automatic file splitting
        - motion-triggered recording
      Device
        - format list (size x fps x pixel format), nearest-format selection
        - all controls, auto modes, reset, save/restore
        - statistics, events, auto-reconnect

    SECURITY / ROBUSTNESS
    =====================
      * Device path must resolve (realpath) to /dev/videoN and be a char device.
      * Every driver supplied size is bounds-checked; every frame is validated
        against its pixel layout BEFORE a converter touches it (no OOB reads on
        truncated frames); frames flagged V4L2_BUF_FLAG_ERROR are dropped.
      * poll() instead of select() (no FD_SETSIZE overflow).
      * Integer overflow checked in all size arithmetic.
      * No exceptions, no RTTI use. result<T> / errc at every boundary.
      * Partial recordings are finalized (valid files) on limit / disconnect.

    THREAD-SAFETY RULES
    ===================
      * camera is NOT movable/copyable (a worker thread holds `this`).
      * Frame / event callbacks run on the capture thread. Inside them you MAY
        call snapshot*(), capture_latest(), pause/resume/stop_recording(),
        start_recording(). You must NOT call start(), stop() or close()
        (they return errc::BadState when called from the capture thread).
      * All other public calls may be used from any thread.

    EXAMPLE
    =======
        auto cams = knst_cam::camera::enumerate();
        knst_cam::open_options o; o.width = 1280; o.height = 720;
        o.pix = knst_vid::pixel_format::MJPEG; o.fps_num = 30;

        knst_cam::camera cam;
        cam.open(cams.value[0], o);

        cam.snapshot(u"photos/a.jpg");                 // works even before start()

        knst_cam::record_options ro;
        ro.path = u"clip.mkv";                         // container from extension
        ro.enable_audio = true;                        // false = video only
        cam.start_recording(ro);
        std::this_thread::sleep_for(std::chrono::seconds(10));
        cam.stop_recording();
*/

#pragma once

#include "knst_video.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

// ============================================================
// Platform detection
// ============================================================
#if defined(KNST_USING_PLATFORM_ANDROID)
    #define KNST_CAM_ANDROID 1
#else
    #define KNST_CAM_ANDROID 0
#endif

#if defined(KNST_USING_PLATFORM_WINDOWS) && KNST_USING_PLATFORM_WINDOWS && !KNST_CAM_ANDROID
    #define KNST_CAM_WINDOWS 1
#else
    #define KNST_CAM_WINDOWS 0
#endif

#if defined(KNST_USING_PLATFORM_LINUX) && !KNST_CAM_ANDROID
    #define KNST_CAM_LINUX_V4L2 1
#else
    #define KNST_CAM_LINUX_V4L2 0
#endif

// ============================================================
// Optional audio support (only knst_audio.hpp is needed)
// ============================================================
#if __has_include("knst_audio.hpp")
    #include "knst_audio.hpp"
    #define KNST_CAM_HAS_AUDIO 1
#else
    #define KNST_CAM_HAS_AUDIO 0
#endif

// ============================================================
// Hard limits - override via -D
// ============================================================
#ifndef KNST_CAMERA_MAX_DIMENSION
    #define KNST_CAMERA_MAX_DIMENSION 16384u
#endif
#ifndef KNST_CAMERA_MAX_FRAME_BYTES
    #define KNST_CAMERA_MAX_FRAME_BYTES (256u * 1024u * 1024u)
#endif
#ifndef KNST_CAMERA_GRAB_TIMEOUT_MS
    #define KNST_CAMERA_GRAB_TIMEOUT_MS 2000
#endif
#ifndef KNST_CAMERA_STALL_TIMEOUT_MS
    #define KNST_CAMERA_STALL_TIMEOUT_MS 5000
#endif
#ifndef KNST_CAMERA_MAX_AUDIO_CHANNELS
    #define KNST_CAMERA_MAX_AUDIO_CHANNELS 8u
#endif

// ============================================================
// Platform headers
// ============================================================
#if KNST_CAM_LINUX_V4L2
    #include <dirent.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <limits.h>
    #include <linux/videodev2.h>
    #include <poll.h>
    #include <sys/ioctl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #ifndef PATH_MAX
        #define PATH_MAX 4096
    #endif
#endif

#if KNST_CAM_ANDROID
    #include <android/native_window.h>
    #include <camera/NdkCameraDevice.h>
    #include <camera/NdkCameraError.h>
    #include <camera/NdkCameraManager.h>
    #include <camera/NdkCameraMetadata.h>
    #include <camera/NdkCameraMetadataTags.h>
    #include <camera/NdkCaptureRequest.h>
        #include <media/NdkImage.h>
    #include <media/NdkImageReader.h>
#endif

#if KNST_CAM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <objbase.h>
    #include <mfapi.h>
    #include <mfidl.h>
    #include <mfreadwrite.h>
    #include <mferror.h>
    #include <dshow.h>
    #include <vidcap.h>
    #include <ks.h>
        #include <ksmedia.h>
    #include <uuids.h>

    #if defined(_MSC_VER)
        #pragma comment(lib, "mfplat.lib")
        #pragma comment(lib, "mf.lib")
        #pragma comment(lib, "mfuuid.lib")
        #pragma comment(lib, "mfreadwrite.lib")
        #pragma comment(lib, "strmiids.lib")
        #pragma comment(lib, "ole32.lib")
        #pragma comment(lib, "oleaut32.lib")
        #pragma comment(lib, "uuid.lib")
    #endif
#endif

namespace knst_cam {

using knst_dev::status;
using knst_dev::result;
using knst_vid::pixel_format;
using knst_vid::pixel_format_name;

// ============================================================
// Errors
// ============================================================
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

inline const char* errc_string(errc e) noexcept {
    switch (e) {
        case errc::Ok:               return "ok";
        case errc::InvalidArgument:  return "invalid argument";
        case errc::NotSupported:     return "not supported";
        case errc::NotFound:         return "not found";
        case errc::PermissionDenied: return "permission denied";
        case errc::IoError:          return "I/O error";
        case errc::OutOfMemory:      return "out of memory";
        case errc::BadState:         return "bad state";
        case errc::Corrupt:          return "corrupt or malformed data";
        case errc::Timeout:          return "timeout";
        case errc::DeviceBusy:       return "device busy";
        case errc::EndOfStream:      return "end of stream";
        case errc::TooLarge:         return "size exceeds library limits";
        case errc::Disconnected:     return "camera disconnected";
    }
    return "unknown";
}

inline status to_status(errc e) noexcept {
    switch (e) {
        case errc::InvalidArgument:  return status::InvalidArgument;
        case errc::NotSupported:     return status::NotSupported;
        case errc::NotFound:         return status::NotFound;
        case errc::EndOfStream:      return status::NotFound;
        case errc::PermissionDenied: return status::PermissionDenied;
        case errc::OutOfMemory:      return status::OutOfMemory;
        case errc::BadState:         return status::BadState;
        case errc::DeviceBusy:       return status::Busy;
        case errc::Timeout:          return status::Timeout;
        case errc::TooLarge:         return status::InvalidArgument;
        default:                     return status::IoError;
    }
}

inline errc from_vid(knst_vid::errc e) noexcept {
    switch (e) {
        case knst_vid::errc::Ok:              return errc::Ok;
        case knst_vid::errc::InvalidArgument: return errc::InvalidArgument;
        case knst_vid::errc::NotSupported:    return errc::NotSupported;
        case knst_vid::errc::NotFound:        return errc::NotFound;
        case knst_vid::errc::OutOfMemory:     return errc::OutOfMemory;
        case knst_vid::errc::BadState:        return errc::BadState;
        case knst_vid::errc::Corrupt:         return errc::Corrupt;
        case knst_vid::errc::TooLarge:        return errc::TooLarge;
        case knst_vid::errc::EndOfStream:     return errc::EndOfStream;
        default:                              return errc::IoError;
    }
}

template <class T> inline result<T> err_result(errc e) noexcept { return result<T>::fail(to_status(e)); }
inline result<void> ok_void() noexcept { return result<void>::success(); }
inline result<void> void_result(errc e) noexcept {
    return e == errc::Ok ? result<void>::success() : result<void>::fail(to_status(e));
}

// ============================================================
// Enums
// ============================================================
enum class control_type : uint8_t {
    Brightness, Contrast, Saturation, Hue,
    Exposure, ExposureAuto, ExposureAutoPriority,
    Focus, FocusAuto,
    WhiteBalance, WhiteBalanceAuto,
    Gain, Gamma, Sharpness, Backlight, PowerLineFrequency,
    Zoom, Pan, Tilt,
    Unknown
};

inline const char* control_type_name(control_type t) noexcept {
    switch (t) {
        case control_type::Brightness:           return "brightness";
        case control_type::Contrast:             return "contrast";
        case control_type::Saturation:           return "saturation";
        case control_type::Hue:                  return "hue";
        case control_type::Exposure:             return "exposure";
        case control_type::ExposureAuto:         return "exposure_auto";
        case control_type::ExposureAutoPriority: return "exposure_auto_priority";
        case control_type::Focus:                return "focus";
        case control_type::FocusAuto:            return "focus_auto";
        case control_type::WhiteBalance:         return "white_balance";
        case control_type::WhiteBalanceAuto:     return "white_balance_auto";
        case control_type::Gain:                 return "gain";
        case control_type::Gamma:                return "gamma";
        case control_type::Sharpness:            return "sharpness";
        case control_type::Backlight:            return "backlight";
        case control_type::PowerLineFrequency:   return "power_line_frequency";
        case control_type::Zoom:                 return "zoom";
        case control_type::Pan:                  return "pan";
        case control_type::Tilt:                 return "tilt";
        default:                                 return "unknown";
    }
}

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

inline const char* event_name(event e) noexcept {
    switch (e) {
        case event::Frame:                 return "frame";
        case event::FrameDropped:          return "frame_dropped";
        case event::StreamStarted:         return "stream_started";
        case event::StreamStopped:         return "stream_stopped";
        case event::RecordingStarted:      return "recording_started";
        case event::RecordingStopped:      return "recording_stopped";
        case event::RecordingPaused:       return "recording_paused";
        case event::RecordingResumed:      return "recording_resumed";
        case event::Disconnected:          return "disconnected";
        case event::Reconnected:           return "reconnected";
        case event::Error:                 return "error";
        case event::MotionStarted:         return "motion_started";
        case event::MotionStopped:         return "motion_stopped";
        case event::RecordingLimitReached: return "recording_limit_reached";
        case event::SnapshotSaved:         return "snapshot_saved";
    }
    return "unknown";
}

inline constexpr uint32_t event_bit(event e) noexcept { return 1u << (uint32_t)e; }
/// Default: everything except the per-frame events (use the frame callback for those).
inline constexpr uint32_t default_event_mask() noexcept {
    return 0xFFFFFFFFu & ~(event_bit(event::Frame) | event_bit(event::FrameDropped));
}

enum class image_format : uint8_t { Auto, JPEG, BMP, PNG, PPM };

// ============================================================
// Structs
// ============================================================
struct format {
    uint32_t width = 0, height = 0;
    uint32_t fps_num = 0, fps_den = 1;
    pixel_format pix = pixel_format::Unknown;
    bool is_compressed = false;                 // MJPEG: camera already encodes
    double fps() const noexcept { return fps_den ? (double)fps_num / (double)fps_den : 0.0; }
};

struct camera_info {
    uint32_t index = 0;
    knst_c16string name;
    knst_c16string device_id;
    knst_c16string bus_info;
    knst_c16string driver;
    uint16_t vid = 0, pid = 0;
    uint32_t max_width = 0, max_height = 0;
    bool is_default = false;
    bool has_video = true;
    bool has_audio = false;                     // heuristic: USB device (may have a microphone)
    knst_vector<format> formats;
};

struct control_info {
    control_type type = control_type::Unknown;
    int32_t min = 0, max = 0, step = 1, current = 0, default_value = 0;
    bool is_auto = false;
    bool is_auto_on = false;
    bool is_writable = true;
    bool is_active = true;
    knst_c16string name;
};

struct control_value { control_type type = control_type::Unknown; int32_t value = 0; };

struct open_options {
    uint32_t width = 640, height = 480;
    uint32_t fps_num = 30, fps_den = 1;
    pixel_format pix = pixel_format::YUYV;
    uint32_t buffer_count = 4;
    bool use_default_format = false;            // keep whatever the driver currently uses
    bool strict_format = false;                 // fail instead of picking the nearest supported format
    uint32_t warmup_frames = 3;                 // frames discarded by one-shot grab() (auto exposure settles)
    bool auto_reconnect = false;                // try to reopen the device after unplug / stall
    uint32_t reconnect_interval_ms = 1000;
    uint32_t reconnect_max_attempts = 0;        // 0 = unlimited
    uint32_t stall_timeout_ms = KNST_CAMERA_STALL_TIMEOUT_MS;   // 0 = off
};

struct frame {
    knst_byte_string data;
    uint32_t width = 0, height = 0, stride = 0;
    pixel_format pix = pixel_format::Unknown;
    uint64_t timestamp_us = 0;
    uint32_t sequence = 0;
    bool valid = false;
};

struct frame_view {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t width = 0, height = 0, stride = 0;
    pixel_format pix = pixel_format::Unknown;
    uint64_t timestamp_us = 0;
    uint32_t sequence = 0;
};

struct image_options {
    image_format format = image_format::Auto;   // Auto: from file extension, JPEG if unknown
    int jpeg_quality = 90;
    knst_vid::mjpeg::subsampling jpeg_sub = knst_vid::mjpeg::subsampling::s420;
    bool mirror = false;                        // horizontal flip
    bool flip_v = false;
    uint32_t rotate_deg = 0;                    // 0 / 90 / 180 / 270 (clockwise)
    uint32_t out_w = 0, out_h = 0;              // one set -> keep aspect, both -> exact
    double scale = 0.0;                         // alternative to out_w/out_h
    bool grey = false;
    bool allow_passthrough = true;              // MJPEG camera + JPEG output + no edits: write camera bytes
    bool active() const noexcept {
        return mirror || flip_v || (rotate_deg % 360) || out_w || out_h ||
               (scale > 0.0 && scale != 1.0) || grey;
    }
};

struct record_options {
    knst_c16string path;
    knst_vid::container cont = knst_vid::container::Unknown;   // Unknown: from extension
    bool enable_video = true;                   // false + enable_audio = audio only
    knst_vid::video_codec vcodec = knst_vid::video_codec::MJPEG;   // MJPEG, or RAW (AVI only)
    int jpeg_quality = 85;                      // used when frames must be encoded
    knst_vid::mjpeg::subsampling jpeg_sub = knst_vid::mjpeg::subsampling::s420;

    uint32_t max_duration_ms = 0;               // 0 = unlimited (whole recording)
    uint64_t max_frames = 0;                    // 0 = unlimited
    uint64_t max_bytes = 0;                     // 0 = unlimited (all segments together)
    uint32_t split_ms = 0;                      // >0: start a new file every N ms  (name_0001.ext ...)
    uint64_t split_bytes = 0;                   // >0: start a new file every N bytes

    bool enable_audio = false;
    uint32_t audio_rate = 48000;
    uint32_t audio_channels = 2;
    knst_vid::audio_codec audio_codec = knst_vid::audio_codec::PCM_S16LE;
    knst_c16string audio_source;                // empty = default microphone
    float audio_gain_db = 0.0f;
    bool audio_remove_dc = false;

    knst_vid::metadata meta;
};

struct motion_options {
    knst_vid::motion_detector::config detector;
    uint32_t post_roll_ms = 3000;               // motion considered over after this much calm
    bool auto_record = false;                   // start/stop recording on motion
    knst_c16string dir;                         // auto_record: output directory
    knst_c16string prefix = u"motion_";
    record_options record;                      // template (path ignored; cont Unknown -> MKV)
};

struct camera_stats {
    uint64_t frames_captured = 0;               // since last start()
    uint64_t frames_dropped = 0;
    uint64_t frames_corrupt = 0;
    uint64_t frames_recorded = 0;
    uint64_t reconnects = 0;
    double   fps = 0.0;
};

struct recording_status {
    bool active = false, paused = false, has_video = false, has_audio = false;
    uint64_t frames = 0, bytes = 0;
    double seconds = 0.0;
    uint32_t segment = 0;
    knst_c16string path;                        // current (or last) file
};

using frame_callback = void (*)(void* user, const frame_view& f);
using state_callback = void (*)(void* user, event e);

// ============================================================
// detail - helpers
// ============================================================
namespace detail {

inline uint64_t now_us() noexcept {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline uint64_t muldiv(uint64_t a, uint64_t b, uint64_t c) noexcept {
    if (c == 0) return 0;
    return (a / c) * b + ((a % c) * b) / c;
}

inline bool path_ok(const knst_c16string& p) noexcept {
    if (p.empty() || p.length() > 4096) return false;
    for (uint32_t i = 0; i < p.length(); ++i) if (p[i] == 0) return false;
    return true;
}

/// NUL-terminated UTF-8 view of a path. nullptr if the path is invalid.
inline const char* to_cpath(const knst_c16string& p, knst_byte_string& buf) noexcept {
    if (!path_ok(p)) return nullptr;
    buf = knst_byte_string(p);
    if (buf.empty()) return nullptr;
    buf.push_back(0);
    return reinterpret_cast<const char*>(buf.data());
}

inline void cstr16(const char* s, knst_c16string& out) noexcept {
    out.clear();
    if (!s) return;
    while (*s) out.append(static_cast<char16_t>(static_cast<uint8_t>(*s++)));
}

/// Lower-case extension without dot ("" if none / non-ASCII).
inline void ext_lower(const knst_c16string& path, char (&buf)[8]) noexcept {
    buf[0] = 0;
    uint32_t dot = UINT32_MAX;
    for (uint32_t i = path.length(); i > 0; --i) {
        char16_t c = path[i - 1];
        if (c == u'.') { dot = i - 1; break; }
        if (c == u'/' || c == u'\\') break;
    }
    if (dot == UINT32_MAX) return;
    uint32_t k = 0;
    for (uint32_t i = dot + 1; i < path.length(); ++i) {
        char16_t c = path[i];
        if (c >= u'A' && c <= u'Z') c = (char16_t)(c + 32);
        if (c > 127 || k >= 7) { buf[0] = 0; return; }
        buf[k++] = (char)c;
    }
    buf[k] = 0;
}

inline knst_vid::container container_from_path(const knst_c16string& path) noexcept {
    char e[8]; ext_lower(path, e);
    if (!std::strcmp(e, "avi")) return knst_vid::container::AVI;
    if (!std::strcmp(e, "mkv")) return knst_vid::container::MKV;
    if (!std::strcmp(e, "mp4") || !std::strcmp(e, "m4v")) return knst_vid::container::MP4;
    if (!std::strcmp(e, "mov")) return knst_vid::container::MOV;
    if (!std::strcmp(e, "wav")) return knst_vid::container::WAV;
    if (!std::strcmp(e, "webm")) return knst_vid::container::WebM;
    return knst_vid::container::Unknown;
}

inline image_format image_format_from_path(const knst_c16string& path) noexcept {
    char e[8]; ext_lower(path, e);
    if (!std::strcmp(e, "jpg") || !std::strcmp(e, "jpeg")) return image_format::JPEG;
    if (!std::strcmp(e, "bmp")) return image_format::BMP;
    if (!std::strcmp(e, "png")) return image_format::PNG;
    if (!std::strcmp(e, "ppm") || !std::strcmp(e, "pgm")) return image_format::PPM;
    return image_format::Auto;
}

inline bool container_has_audio(knst_vid::container c, knst_vid::audio_codec a) noexcept {
    using AC = knst_vid::audio_codec;
    if (!knst_vid::audio_codec_is_pcm(a)) return false;
    switch (c) {
        case knst_vid::container::AVI: return true;
        case knst_vid::container::MKV: return true;
        case knst_vid::container::WAV: return true;
        case knst_vid::container::MOV: return a == AC::PCM_S16LE;
        default: return false;                  // MP4 writer is video-only
    }
}

/// name.ext -> name_0001.ext
inline knst_c16string with_suffix(const knst_c16string& path, uint32_t idx) noexcept {
    uint32_t dot = UINT32_MAX;
    for (uint32_t i = path.length(); i > 0; --i) {
        char16_t c = path[i - 1];
        if (c == u'.') { dot = i - 1; break; }
        if (c == u'/' || c == u'\\') break;
    }
    char buf[16]; std::snprintf(buf, sizeof(buf), "_%04u", idx);
    knst_c16string out;
    if (dot == UINT32_MAX) { out = path; out.append(knst_c16string(buf)); }
    else {
        out = path.substr(0, dot);
        out.append(knst_c16string(buf));
        out.append(path.substr(dot, path.length() - dot));
    }
    return out;
}

inline knst_c16string timestamp_string() noexcept {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tmv; std::memset(&tmv, 0, sizeof(tmv));
#if defined(_WIN32)
    ::localtime_s(&tmv, &t);
#else
    ::localtime_r(&t, &tmv);
#endif
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d_%03d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)ms);
    return knst_c16string(buf);
}

/// Is the buffer large enough / plausible for the declared layout? (must pass before any converter runs)
inline bool validate_frame_bytes(const uint8_t* d, uint32_t size, uint32_t w, uint32_t h,
                                 uint32_t stride, pixel_format pix) noexcept {
    if (!d || size == 0 || size > KNST_CAMERA_MAX_FRAME_BYTES) return false;
    if (w == 0 || h == 0 || w > KNST_CAMERA_MAX_DIMENSION || h > KNST_CAMERA_MAX_DIMENSION) return false;
    if (pix == pixel_format::MJPEG) return size >= 4 && d[0] == 0xFF && d[1] == 0xD8;
    knst_vid::plane_layout L;
    if (!knst_vid::compute_layout(pix, w, h, stride, L)) return false;
    return (uint64_t)size >= L.total;
}

/// Any pixel format -> tightly packed RGB24 (validated).
inline errc to_rgb24(const uint8_t* d, uint32_t size, uint32_t w, uint32_t h, uint32_t stride,
                     pixel_format pix, knst_byte_string& rgb, uint32_t& ow, uint32_t& oh) noexcept {
    if (!validate_frame_bytes(d, size, w, h, stride, pix)) return errc::Corrupt;
    if (pix == pixel_format::MJPEG) {
        auto r = knst_vid::mjpeg::decode_rgb24(d, size, ow, oh);
        if (!r) return errc::NotSupported;
        rgb = std::move(r.value);
        if (ow == 0 || oh == 0 || ow > KNST_CAMERA_MAX_DIMENSION || oh > KNST_CAMERA_MAX_DIMENSION) return errc::Corrupt;
        if (rgb.length() < (uint64_t)ow * oh * 3ull) return errc::Corrupt;
        return errc::Ok;
    }
    ow = w; oh = h;
    return from_vid(knst_vid::color::convert_to_rgb24(d, pix, w, h, stride, rgb));
}

/// Nearest supported format for the request (lower cost = better). -1 if the list is empty.
inline int pick_format_index(const knst_vector<format>& list, const open_options& o) noexcept {
    int best = -1; double best_cost = 0.0;
    const double want_fps = o.fps_den ? (double)o.fps_num / (double)o.fps_den : 30.0;
    for (uint32_t i = 0; i < list.size(); ++i) {
        const format& f = list[i];
        double c = 0.0;
        if (o.pix != pixel_format::Unknown && f.pix != o.pix) c += 50000.0;
        double dw = (double)f.width - (double)o.width, dh = (double)f.height - (double)o.height;
        c += ((dw < 0 ? -dw : dw) + (dh < 0 ? -dh : dh)) * 100.0;
        double df = f.fps() - want_fps; if (df < 0) df = -df;
        c += df * 10.0;
        if (best < 0 || c < best_cost) { best = (int)i; best_cost = c; }
    }
    return best;
}

// ---- CRC32 / Adler32 / PNG ----
inline uint32_t crc32_update(uint32_t crc, const uint8_t* d, size_t n) noexcept {
    struct table { uint32_t t[256]; table() noexcept {
        for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); t[i] = c; } } };
    static const table tab;
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = tab.t[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline void be32(uint8_t* p, uint32_t v) noexcept { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/// Valid PNG using stored (uncompressed) deflate blocks: big files, zero dependencies.
inline errc build_png(const uint8_t* px, uint32_t w, uint32_t h, uint32_t ch, knst_byte_string& out) noexcept {
    if (!px || !w || !h || (ch != 1 && ch != 3)) return errc::InvalidArgument;
    if (w > KNST_CAMERA_MAX_DIMENSION || h > KNST_CAMERA_MAX_DIMENSION) return errc::TooLarge;
    const uint64_t row = (uint64_t)w * ch + 1;
    const uint64_t raw = row * h;
    if (raw > KNST_CAMERA_MAX_FRAME_BYTES) return errc::TooLarge;
    const uint64_t blocks = (raw + 65534) / 65535;
    const uint64_t zlen = 2 + raw + blocks * 5 + 4;
    const uint64_t total = 8 + 25 + (12 + zlen) + 12;
    if (total > 0xFFFFFF00ull) return errc::TooLarge;

    knst_byte_string rawbuf;
    rawbuf.resize((uint32_t)raw);
    if (rawbuf.length() != (uint32_t)raw) return errc::OutOfMemory;
    {
        uint8_t* r = &rawbuf[0];
        for (uint32_t y = 0; y < h; ++y) {
            r[0] = 0;
            std::memcpy(r + 1, px + (size_t)y * w * ch, (size_t)w * ch);
            r += row;
        }
    }
    uint32_t a = 1, b = 0;
    {
        const uint8_t* r = rawbuf.data(); uint64_t left = raw;
        while (left) {
            uint32_t n = left > 5552 ? 5552u : (uint32_t)left;
            for (uint32_t i = 0; i < n; ++i) { a += r[i]; b += a; }
            a %= 65521u; b %= 65521u; r += n; left -= n;
        }
    }
    out.resize((uint32_t)total);
    if (out.length() != (uint32_t)total) return errc::OutOfMemory;
    uint8_t* p = &out[0];
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::memcpy(p, sig, 8); p += 8;
    // IHDR
    be32(p, 13); std::memcpy(p + 4, "IHDR", 4);
    be32(p + 8, w); be32(p + 12, h);
    p[16] = 8; p[17] = (uint8_t)(ch == 1 ? 0 : 2); p[18] = 0; p[19] = 0; p[20] = 0;
    be32(p + 21, crc32_update(0, p + 4, 17));
    p += 25;
    // IDAT
    be32(p, (uint32_t)zlen); std::memcpy(p + 4, "IDAT", 4);
    uint8_t* z = p + 8;
    *z++ = 0x78; *z++ = 0x01;
    {
        const uint8_t* r = rawbuf.data();
        for (uint64_t off = 0; off < raw;) {
            uint32_t n = (raw - off) > 65535 ? 65535u : (uint32_t)(raw - off);
            *z++ = (off + n == raw) ? 1 : 0;
            *z++ = (uint8_t)n; *z++ = (uint8_t)(n >> 8);
            *z++ = (uint8_t)~n; *z++ = (uint8_t)(~n >> 8);
            std::memcpy(z, r + off, n); z += n; off += n;
        }
    }
    be32(z, (b << 16) | a); z += 4;
    be32(z, crc32_update(0, p + 4, (size_t)(z - (p + 4)))); z += 4;
    p = z;
    // IEND
    be32(p, 0); std::memcpy(p + 4, "IEND", 4); be32(p + 8, 0xAE426082u);
    return errc::Ok;
}

inline errc build_ppm(const uint8_t* px, uint32_t w, uint32_t h, uint32_t ch, knst_byte_string& out) noexcept {
    if (!px || !w || !h || (ch != 1 && ch != 3)) return errc::InvalidArgument;
    char hdr[48];
    int hl = std::snprintf(hdr, sizeof(hdr), "%s\n%u %u\n255\n", ch == 1 ? "P5" : "P6", w, h);
    if (hl <= 0) return errc::IoError;
    const uint64_t body = (uint64_t)w * h * ch;
    if (body > KNST_CAMERA_MAX_FRAME_BYTES) return errc::TooLarge;
    out.resize((uint32_t)(hl + body));
    if (out.length() != (uint32_t)(hl + body)) return errc::OutOfMemory;
    std::memcpy(&out[0], hdr, (size_t)hl);
    std::memcpy(&out[0] + hl, px, (size_t)body);
    return errc::Ok;
}

/// 24-bit BMP from tightly packed RGB24.
inline errc build_bmp(const uint8_t* rgb, uint32_t w, uint32_t h, knst_byte_string& out) noexcept {
    if (!rgb || !w || !h) return errc::InvalidArgument;
    if (w > KNST_CAMERA_MAX_DIMENSION || h > KNST_CAMERA_MAX_DIMENSION) return errc::TooLarge;
    const uint32_t row_bytes = w * 3u;
    const uint32_t padded = (row_bytes + 3u) & ~3u;
    const uint64_t data64 = (uint64_t)padded * h;
    const uint64_t file64 = 54ull + data64;
    if (file64 > 0xFFFFFFF0ull || data64 > KNST_CAMERA_MAX_FRAME_BYTES) return errc::TooLarge;
    const uint32_t data_size = (uint32_t)data64, file_size = (uint32_t)file64;
    out.resize(file_size);
    if (out.length() != file_size) return errc::OutOfMemory;
    uint8_t* p = &out[0];
    std::memset(p, 0, 54);
    p[0] = 'B'; p[1] = 'M';
    p[2] = (uint8_t)file_size; p[3] = (uint8_t)(file_size >> 8); p[4] = (uint8_t)(file_size >> 16); p[5] = (uint8_t)(file_size >> 24);
    p[10] = 54; p[14] = 40;
    p[18] = (uint8_t)w; p[19] = (uint8_t)(w >> 8); p[20] = (uint8_t)(w >> 16); p[21] = (uint8_t)(w >> 24);
    p[22] = (uint8_t)h; p[23] = (uint8_t)(h >> 8); p[24] = (uint8_t)(h >> 16); p[25] = (uint8_t)(h >> 24);
    p[26] = 1; p[28] = 24;
    p[34] = (uint8_t)data_size; p[35] = (uint8_t)(data_size >> 8); p[36] = (uint8_t)(data_size >> 16); p[37] = (uint8_t)(data_size >> 24);
    p[38] = 0x13; p[39] = 0x0B; p[42] = 0x13; p[43] = 0x0B;
    uint8_t* dst = p + 54;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* s = rgb + (size_t)(h - 1u - y) * w * 3u;
        uint8_t* d = dst + (size_t)y * padded;
        for (uint32_t x = 0; x < w; ++x) { d[x * 3] = s[x * 3 + 2]; d[x * 3 + 1] = s[x * 3 + 1]; d[x * 3 + 2] = s[x * 3]; }
        for (uint32_t x = row_bytes; x < padded; ++x) d[x] = 0;
    }
    return errc::Ok;
}

inline bool compute_out_size(const image_options& o, uint32_t w, uint32_t h, uint32_t& dw, uint32_t& dh) noexcept {
    dw = w; dh = h;
    if (o.out_w && o.out_h) { dw = o.out_w; dh = o.out_h; }
    else if (o.out_w) { dw = o.out_w; dh = (uint32_t)(((uint64_t)h * o.out_w + w / 2) / w); }
    else if (o.out_h) { dh = o.out_h; dw = (uint32_t)(((uint64_t)w * o.out_h + h / 2) / h); }
    else if (o.scale > 0.0 && o.scale != 1.0) { dw = (uint32_t)((double)w * o.scale + 0.5); dh = (uint32_t)((double)h * o.scale + 0.5); }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    return dw <= KNST_CAMERA_MAX_DIMENSION && dh <= KNST_CAMERA_MAX_DIMENSION;
}

} // namespace detail

// ============================================================
// Image output (usable without a camera)
// ============================================================

/// Encodes `f` according to `o`. `used` receives the format that was actually written.
inline errc encode_frame(const frame& f, const image_options& o, knst_byte_string& out, image_format& used) noexcept {
    namespace img = knst_vid::img;
    if (!f.valid || f.data.empty()) return errc::InvalidArgument;
    if (f.width == 0 || f.height == 0 || f.width > KNST_CAMERA_MAX_DIMENSION || f.height > KNST_CAMERA_MAX_DIMENSION)
        return errc::TooLarge;
    if (o.rotate_deg % 90 != 0) return errc::InvalidArgument;
    image_format fmt = o.format == image_format::Auto ? image_format::JPEG : o.format;
    used = fmt;

    // JPEG straight from an MJPEG camera: keep the camera's bytes (add DHT if the camera omitted it)
    if (fmt == image_format::JPEG && f.pix == pixel_format::MJPEG && !o.active() && o.allow_passthrough) {
        const uint8_t* d = f.data.data(); const uint32_t n = f.data.length();
        if (!detail::validate_frame_bytes(d, n, f.width, f.height, 0, pixel_format::MJPEG)) return errc::Corrupt;
        knst_byte_string patched;
        if (knst_vid::mjpeg::inject_std_dht(d, n, patched)) out = std::move(patched); else out = f.data;
        return out.length() > KNST_CAMERA_MAX_FRAME_BYTES ? errc::TooLarge : errc::Ok;
    }

    knst_byte_string rgb; uint32_t w = 0, h = 0;
    errc e = detail::to_rgb24(f.data.data(), f.data.length(), f.width, f.height, f.stride, f.pix, rgb, w, h);
    if (e != errc::Ok) return e;

    auto take = [&](result<knst_byte_string>&& r) -> bool { if (!r) return false; rgb = std::move(r.value); return true; };
    if (o.mirror && !take(img::flip_h_rgb24(rgb.data(), w, h))) return errc::OutOfMemory;
    if (o.flip_v && !take(img::flip_v_rgb24(rgb.data(), w, h))) return errc::OutOfMemory;
    switch (o.rotate_deg % 360) {
        case 90:  if (!take(img::rotate_90_rgb24(rgb.data(), w, h, true)))  return errc::OutOfMemory; std::swap(w, h); break;
        case 270: if (!take(img::rotate_90_rgb24(rgb.data(), w, h, false))) return errc::OutOfMemory; std::swap(w, h); break;
        case 180: if (!take(img::rotate_180_rgb24(rgb.data(), w, h)))       return errc::OutOfMemory; break;
        default: break;
    }
    uint32_t dw, dh;
    if (!detail::compute_out_size(o, w, h, dw, dh)) return errc::TooLarge;
    if (dw != w || dh != h) {
        auto rs = img::resize_rgb24(rgb.data(), w, h, dw, dh,
                                    (dw < w || dh < h) ? img::resize_filter::box : img::resize_filter::bilinear);
        if (!rs) return errc::OutOfMemory;
        rgb = std::move(rs.value); w = dw; h = dh;
    }
    uint32_t ch = 3;
    if (o.grey) {
        auto g = img::to_grey_rgb24(rgb.data(), w, h);
        if (!g) return errc::OutOfMemory;
        rgb = std::move(g.value); ch = 1;
    }

    switch (fmt) {
        case image_format::JPEG: {
            knst_vid::mjpeg::encoder enc; knst_vid::mjpeg::encoder::options eo;
            eo.quality = o.jpeg_quality < 1 ? 1 : (o.jpeg_quality > 100 ? 100 : o.jpeg_quality);
            eo.sub = o.jpeg_sub;
            e = from_vid(enc.configure(eo));
            if (e == errc::Ok)
                e = from_vid(enc.encode(rgb.data(), w, h, 0, ch == 1 ? pixel_format::GREY : pixel_format::RGB24, out));
            break;
        }
        case image_format::PNG: e = detail::build_png(rgb.data(), w, h, ch, out); break;
        case image_format::PPM: e = detail::build_ppm(rgb.data(), w, h, ch, out); break;
        case image_format::BMP: {
            if (ch == 1) {                          // expand grey -> RGB for BMP
                knst_byte_string tmp; tmp.resize(w * h * 3u);
                if (tmp.length() != w * h * 3u) return errc::OutOfMemory;
                for (uint32_t i = 0; i < w * h; ++i) { tmp[i * 3] = rgb[i]; tmp[i * 3 + 1] = rgb[i]; tmp[i * 3 + 2] = rgb[i]; }
                e = detail::build_bmp(tmp.data(), w, h, out);
            } else e = detail::build_bmp(rgb.data(), w, h, out);
            break;
        }
        default: e = errc::InvalidArgument; break;
    }
    if (e == errc::Ok && out.length() > KNST_CAMERA_MAX_FRAME_BYTES) e = errc::TooLarge;
    return e;
}

/// Writes `f` to `path`. Format from options, else from the extension, else JPEG.
inline result<void> save_frame(const frame& f, const knst_c16string& path, image_options o = image_options()) noexcept {
    if (!detail::path_ok(path)) return void_result(errc::InvalidArgument);
    if (o.format == image_format::Auto) o.format = detail::image_format_from_path(path);
    knst_byte_string bytes; image_format used = image_format::JPEG;
    errc e = encode_frame(f, o, bytes, used);
    if (e != errc::Ok) return void_result(e);
    if (!knst_file::ensure_parent_dir(path)) return void_result(errc::IoError);
    if (!knst_file::write_file_atomic(path, bytes.data(), bytes.length())) return void_result(errc::IoError);
    return ok_void();
}

inline result<void> save_frame_jpeg(const frame& f, const knst_c16string& path, int quality = 90) noexcept {
    image_options o; o.format = image_format::JPEG; o.jpeg_quality = quality; return save_frame(f, path, o);
}
inline result<void> save_frame_bmp(const frame& f, const knst_c16string& path) noexcept {
    image_options o; o.format = image_format::BMP; return save_frame(f, path, o);
}
inline result<void> save_frame_png(const frame& f, const knst_c16string& path) noexcept {
    image_options o; o.format = image_format::PNG; return save_frame(f, path, o);
}

// ============================================================
// Linux V4L2 helpers
// ============================================================
#if KNST_CAM_LINUX_V4L2
namespace detail {

inline pixel_format v4l2_to_pix(uint32_t f) noexcept {
    switch (f) {
        case V4L2_PIX_FMT_YUYV:   return pixel_format::YUYV;
        case V4L2_PIX_FMT_UYVY:   return pixel_format::UYVY;
        case V4L2_PIX_FMT_YVYU:   return pixel_format::YVYU;
        case V4L2_PIX_FMT_NV12:   return pixel_format::NV12;
        case V4L2_PIX_FMT_NV21:   return pixel_format::NV21;
        case V4L2_PIX_FMT_YUV420: return pixel_format::I420;
        case V4L2_PIX_FMT_YVU420: return pixel_format::YV12;
        case V4L2_PIX_FMT_RGB24:  return pixel_format::RGB24;
        case V4L2_PIX_FMT_BGR24:  return pixel_format::BGR24;
        case V4L2_PIX_FMT_RGBA32: return pixel_format::RGBA32;
        case V4L2_PIX_FMT_ABGR32: return pixel_format::BGRA32;
        case V4L2_PIX_FMT_GREY:   return pixel_format::GREY;
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_JPEG:   return pixel_format::MJPEG;
        case V4L2_PIX_FMT_SBGGR8: return pixel_format::Bayer_BGGR;
        case V4L2_PIX_FMT_SGBRG8: return pixel_format::Bayer_GBRG;
        case V4L2_PIX_FMT_SGRBG8: return pixel_format::Bayer_GRBG;
        case V4L2_PIX_FMT_SRGGB8: return pixel_format::Bayer_RGGB;
        default: return pixel_format::Unknown;
    }
}

inline uint32_t pix_to_v4l2(pixel_format p) noexcept {
    switch (p) {
        case pixel_format::YUYV:   return V4L2_PIX_FMT_YUYV;
        case pixel_format::UYVY:   return V4L2_PIX_FMT_UYVY;
        case pixel_format::YVYU:   return V4L2_PIX_FMT_YVYU;
        case pixel_format::NV12:   return V4L2_PIX_FMT_NV12;
        case pixel_format::NV21:   return V4L2_PIX_FMT_NV21;
        case pixel_format::I420:   return V4L2_PIX_FMT_YUV420;
        case pixel_format::YV12:   return V4L2_PIX_FMT_YVU420;
        case pixel_format::RGB24:  return V4L2_PIX_FMT_RGB24;
        case pixel_format::BGR24:  return V4L2_PIX_FMT_BGR24;
        case pixel_format::RGBA32: return V4L2_PIX_FMT_RGBA32;
        case pixel_format::BGRA32: return V4L2_PIX_FMT_ABGR32;
        case pixel_format::GREY:   return V4L2_PIX_FMT_GREY;
        case pixel_format::MJPEG:  return V4L2_PIX_FMT_MJPEG;
        case pixel_format::Bayer_BGGR: return V4L2_PIX_FMT_SBGGR8;
        case pixel_format::Bayer_GBRG: return V4L2_PIX_FMT_SGBRG8;
        case pixel_format::Bayer_GRBG: return V4L2_PIX_FMT_SGRBG8;
        case pixel_format::Bayer_RGGB: return V4L2_PIX_FMT_SRGGB8;
        default: return 0;
    }
}

inline control_type v4l2_cid_to_type(uint32_t id) noexcept {
    switch (id) {
        case V4L2_CID_BRIGHTNESS:                return control_type::Brightness;
        case V4L2_CID_CONTRAST:                  return control_type::Contrast;
        case V4L2_CID_SATURATION:                return control_type::Saturation;
        case V4L2_CID_HUE:                       return control_type::Hue;
        case V4L2_CID_EXPOSURE_ABSOLUTE:         return control_type::Exposure;
        case V4L2_CID_EXPOSURE_AUTO:             return control_type::ExposureAuto;
        case V4L2_CID_EXPOSURE_AUTO_PRIORITY:    return control_type::ExposureAutoPriority;
        case V4L2_CID_FOCUS_ABSOLUTE:            return control_type::Focus;
        case V4L2_CID_FOCUS_AUTO:                return control_type::FocusAuto;
        case V4L2_CID_WHITE_BALANCE_TEMPERATURE: return control_type::WhiteBalance;
        case V4L2_CID_AUTO_WHITE_BALANCE:        return control_type::WhiteBalanceAuto;
        case V4L2_CID_GAIN:                      return control_type::Gain;
        case V4L2_CID_GAMMA:                     return control_type::Gamma;
        case V4L2_CID_SHARPNESS:                 return control_type::Sharpness;
        case V4L2_CID_BACKLIGHT_COMPENSATION:    return control_type::Backlight;
        case V4L2_CID_POWER_LINE_FREQUENCY:      return control_type::PowerLineFrequency;
        case V4L2_CID_ZOOM_ABSOLUTE:             return control_type::Zoom;
        case V4L2_CID_PAN_ABSOLUTE:              return control_type::Pan;
        case V4L2_CID_TILT_ABSOLUTE:             return control_type::Tilt;
        default:                                 return control_type::Unknown;
    }
}

inline uint32_t type_to_v4l2_cid(control_type t) noexcept {
    switch (t) {
        case control_type::Brightness:           return V4L2_CID_BRIGHTNESS;
        case control_type::Contrast:             return V4L2_CID_CONTRAST;
        case control_type::Saturation:           return V4L2_CID_SATURATION;
        case control_type::Hue:                  return V4L2_CID_HUE;
        case control_type::Exposure:             return V4L2_CID_EXPOSURE_ABSOLUTE;
        case control_type::ExposureAuto:         return V4L2_CID_EXPOSURE_AUTO;
        case control_type::ExposureAutoPriority: return V4L2_CID_EXPOSURE_AUTO_PRIORITY;
        case control_type::Focus:                return V4L2_CID_FOCUS_ABSOLUTE;
        case control_type::FocusAuto:            return V4L2_CID_FOCUS_AUTO;
        case control_type::WhiteBalance:         return V4L2_CID_WHITE_BALANCE_TEMPERATURE;
        case control_type::WhiteBalanceAuto:     return V4L2_CID_AUTO_WHITE_BALANCE;
        case control_type::Gain:                 return V4L2_CID_GAIN;
        case control_type::Gamma:                return V4L2_CID_GAMMA;
        case control_type::Sharpness:            return V4L2_CID_SHARPNESS;
        case control_type::Backlight:            return V4L2_CID_BACKLIGHT_COMPENSATION;
        case control_type::PowerLineFrequency:   return V4L2_CID_POWER_LINE_FREQUENCY;
        case control_type::Zoom:                 return V4L2_CID_ZOOM_ABSOLUTE;
        case control_type::Pan:                  return V4L2_CID_PAN_ABSOLUTE;
        case control_type::Tilt:                 return V4L2_CID_TILT_ABSOLUTE;
        default:                                 return 0;
    }
}

inline bool is_auto_type(control_type t) noexcept {
    return t == control_type::ExposureAuto || t == control_type::FocusAuto || t == control_type::WhiteBalanceAuto;
}

inline void v4l2_fill_control(int fd, const struct v4l2_queryctrl& q, control_type t, control_info& c) noexcept {
    c.type = t; c.min = q.minimum; c.max = q.maximum; c.step = q.step ? q.step : 1;
    c.default_value = q.default_value;
    c.is_writable = !(q.flags & V4L2_CTRL_FLAG_READ_ONLY);
    c.is_active = !(q.flags & V4L2_CTRL_FLAG_INACTIVE);
    c.is_auto = is_auto_type(t);
    cstr16(reinterpret_cast<const char*>(q.name), c.name);
    struct v4l2_control g{}; g.id = q.id;
    if (::ioctl(fd, VIDIOC_G_CTRL, &g) == 0) {
        c.current = g.value;
        if (c.is_auto) c.is_auto_on = (t == control_type::ExposureAuto) ? (g.value != V4L2_EXPOSURE_MANUAL) : (g.value != 0);
    }
}

/// Resolves to a real /dev/videoN path (rejects anything else).
inline bool v4l2_resolve_path(const char* p, char (&out)[PATH_MAX]) noexcept {
    if (!p || !::realpath(p, out)) return false;
    if (std::strncmp(out, "/dev/video", 10) != 0) return false;
    const char* d = out + 10;
    if (!*d) return false;
    for (; *d; ++d) if (*d < '0' || *d > '9') return false;
    return true;
}

inline bool v4l2_is_capture_device(const char* path) noexcept {
    int fd = ::open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) fd = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    struct v4l2_capability cap{};
    bool ok = false;
    if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        ok = (caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
    }
    ::close(fd);
    return ok;
}

inline void v4l2_push_intervals(int fd, uint32_t fourcc, pixel_format pf, uint32_t w, uint32_t h,
                                knst_vector<format>& out) noexcept {
    struct v4l2_frmivalenum fi{};
    fi.pixel_format = fourcc; fi.width = w; fi.height = h;
    bool any = false;
    for (fi.index = 0; ::ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0; ++fi.index) {
        format f; f.width = w; f.height = h; f.pix = pf; f.is_compressed = (pf == pixel_format::MJPEG);
        if (fi.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            if (!fi.discrete.numerator || !fi.discrete.denominator) continue;
            f.fps_num = fi.discrete.denominator; f.fps_den = fi.discrete.numerator;
            out.push_back(f); any = true;
        } else {                                    // stepwise / continuous: fastest rate
            if (!fi.stepwise.min.numerator || !fi.stepwise.min.denominator) break;
            f.fps_num = fi.stepwise.min.denominator; f.fps_den = fi.stepwise.min.numerator;
            out.push_back(f); any = true; break;
        }
    }
    if (!any) { format f; f.width = w; f.height = h; f.fps_num = 30; f.fps_den = 1; f.pix = pf; f.is_compressed = (pf == pixel_format::MJPEG); out.push_back(f); }
}

inline void v4l2_probe_formats(int fd, knst_vector<format>& out, uint32_t& max_w, uint32_t& max_h) noexcept {
    struct v4l2_fmtdesc fmt{}; fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (fmt.index = 0; ::ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; ++fmt.index) {
        pixel_format pf = v4l2_to_pix(fmt.pixelformat);
        if (pf == pixel_format::Unknown) continue;
        struct v4l2_frmsizeenum fs{}; fs.pixel_format = fmt.pixelformat;
        bool any_size = false;
        for (fs.index = 0; ::ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0; ++fs.index) {
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                uint32_t w = fs.discrete.width, h = fs.discrete.height;
                if (!w || !h || w > KNST_CAMERA_MAX_DIMENSION || h > KNST_CAMERA_MAX_DIMENSION) continue;
                if (w > max_w) max_w = w;
                if (h > max_h) max_h = h;
                v4l2_push_intervals(fd, fmt.pixelformat, pf, w, h, out);
                any_size = true;
            } else {                                // stepwise / continuous: common presets inside the range
                static const uint32_t pre[][2] = { {160,120},{320,240},{640,360},{640,480},{800,600},
                                                   {1280,720},{1280,960},{1920,1080},{2560,1440},{3840,2160} };
                const uint32_t sw = fs.stepwise.step_width ? fs.stepwise.step_width : 1;
                const uint32_t sh = fs.stepwise.step_height ? fs.stepwise.step_height : 1;
                for (const auto& p : pre) {
                    uint32_t w = p[0], h = p[1];
                    if (w < fs.stepwise.min_width || w > fs.stepwise.max_width) continue;
                    if (h < fs.stepwise.min_height || h > fs.stepwise.max_height) continue;
                    if ((w - fs.stepwise.min_width) % sw || (h - fs.stepwise.min_height) % sh) continue;
                    if (w > max_w) max_w = w;
                    if (h > max_h) max_h = h;
                    v4l2_push_intervals(fd, fmt.pixelformat, pf, w, h, out);
                    any_size = true;
                }
                break;
            }
        }
        if (!any_size) {
            struct v4l2_format cur{}; cur.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (::ioctl(fd, VIDIOC_G_FMT, &cur) == 0 && cur.fmt.pix.width && cur.fmt.pix.height) {
                format f; f.width = cur.fmt.pix.width; f.height = cur.fmt.pix.height; f.fps_num = 30; f.fps_den = 1;
                f.pix = pf; f.is_compressed = (pf == pixel_format::MJPEG);
                out.push_back(f);
            }
        }
    }
}

inline bool read_hex_file(const char* path, uint16_t& out) noexcept {
    std::FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char b[16] = {0};
    bool ok = std::fgets(b, sizeof(b), f) != nullptr;
    std::fclose(f);
    if (!ok) return false;
    out = (uint16_t)std::strtoul(b, nullptr, 16);
    return true;
}

inline uint64_t v4l2_ts_us(const struct v4l2_buffer& b) noexcept {
    if ((b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC)
        return (uint64_t)b.timestamp.tv_sec * 1000000ull + (uint64_t)b.timestamp.tv_usec;
    return now_us();
}

} // namespace detail
#endif // KNST_CAM_LINUX_V4L2

// ============================================================
// camera
// ============================================================
class camera {
public:
    camera() noexcept = default;
    ~camera() { close(); }

    camera(const camera&) = delete;
    camera& operator=(const camera&) = delete;
    camera(camera&&) = delete;
    camera& operator=(camera&&) = delete;

    // ---- Static enumeration ----
    static result<knst_vector<camera_info>> enumerate() noexcept;
    static result<camera_info> default_camera() noexcept;
    static result<camera_info> find(const knst_c16string& device_id) noexcept;
    static const char* backend_name() noexcept;
    /// Names of real microphones (monitors / loopbacks excluded). Needs knst_audio.hpp.
    static result<knst_vector<knst_c16string>> list_audio_sources() noexcept;

    // ---- Lifecycle ----
    result<void> open(const camera_info& info, const open_options& opt = open_options()) noexcept;
    result<void> open(const knst_c16string& device_id, const open_options& opt = open_options()) noexcept;
    result<void> close() noexcept;

    bool is_open()             const noexcept { return open_.load(std::memory_order_relaxed); }
    bool is_streaming()        const noexcept { return streaming_.load(std::memory_order_relaxed); }
    bool is_recording()        const noexcept { return recording_.load(std::memory_order_relaxed); }
    bool is_recording_paused() const noexcept { return rec_paused_.load(std::memory_order_relaxed); }

    // ---- Frames ----
    /// One-shot capture while NOT streaming. skip_frames: UINT32_MAX = open_options::warmup_frames.
    result<frame> grab(uint32_t skip_frames = UINT32_MAX, uint32_t timeout_ms = 0) noexcept;
    /// Fresh frame: waits for the next streamed frame, or does grab() when idle.
    result<frame> capture_latest(uint32_t timeout_ms = 0) noexcept;

    // ---- Streaming ----
    /// A null ev_cb keeps the current event callback. Callbacks run on the capture thread.
    result<void> start(frame_callback cb = nullptr, void* user = nullptr,
                       state_callback ev_cb = nullptr, void* ev_user = nullptr) noexcept;
    result<void> stop() noexcept;
    void set_event_callback(state_callback cb, void* user) noexcept;
    void set_event_mask(uint32_t mask) noexcept;

    // ---- Recording ----
    result<void> start_recording(const record_options& opt) noexcept;
    result<void> stop_recording() noexcept;
    result<void> pause_recording() noexcept;
    result<void> resume_recording() noexcept;
    recording_status recording_info() const noexcept;

    // ---- Snapshots ----
    result<void> snapshot(const knst_c16string& path, const image_options& o = image_options()) noexcept;
    result<void> snapshot_jpeg(const knst_c16string& path, int quality = 90) noexcept;
    result<void> snapshot_bmp(const knst_c16string& path) noexcept;
    result<void> snapshot_png(const knst_c16string& path) noexcept;
    /// dir/prefix + timestamp + extension; returns the written path.
    result<knst_c16string> snapshot_auto(const knst_c16string& dir, const knst_c16string& prefix = u"photo_",
                                         const image_options& o = image_options()) noexcept;
    result<knst_byte_string> snapshot_memory(const image_options& o = image_options()) noexcept;
    /// `count` photos, `interval_ms` apart (blocking). Returns the written paths.
    result<knst_vector<knst_c16string>> snapshot_burst(const knst_c16string& dir, uint32_t count, uint32_t interval_ms,
                                                       const knst_c16string& prefix = u"burst_",
                                                       const image_options& o = image_options()) noexcept;

    // ---- Motion ----
    result<void> enable_motion_detection(const motion_options& mo) noexcept;
    void disable_motion_detection() noexcept;
    bool is_motion_active() const noexcept { return motion_active_.load(std::memory_order_relaxed); }

    // ---- Controls ----
    result<knst_vector<control_info>> controls() noexcept;
    result<control_info> get_control(control_type t) noexcept;
    /// Value is clamped / step-aligned. Absolute controls (exposure, focus, white balance) leave auto mode first.
    result<void> set_control(control_type t, int32_t value) noexcept;
    /// Accepts the *Auto types, or Exposure / Focus / WhiteBalance as a shortcut for their auto switch.
    result<void> set_control_auto(control_type t, bool auto_on) noexcept;
    result<void> reset_control(control_type t) noexcept;
    result<void> reset_all_controls() noexcept;
    result<knst_vector<control_value>> save_controls() noexcept;
    result<void> restore_controls(const knst_vector<control_value>& v) noexcept;

    // ---- Info / stats ----
    result<format> current_format() const noexcept;
    const knst_vector<format>& supported_formats() const noexcept { return formats_; }
    const camera_info& info() const noexcept { return info_; }
    camera_stats stats() const noexcept;
    double effective_fps() const noexcept;
    uint64_t frames_captured() const noexcept { return frames_captured_.load(std::memory_order_relaxed); }
    uint64_t frames_dropped()  const noexcept { return frames_dropped_.load(std::memory_order_relaxed); }
    uint64_t frames_recorded() const noexcept { return frames_recorded_.load(std::memory_order_relaxed); }

    errc last_error() const noexcept { return last_error_.load(std::memory_order_relaxed); }
    const char* last_error_string() const noexcept { return errc_string(last_error()); }
    /// Human readable detail of the last failure.
    knst_c16string last_message() const noexcept { std::lock_guard<std::mutex> lk(err_mtx_); return last_msg_; }

private:
    enum class rec_result : uint8_t { Ok, Limit, Error };

    // ---- device / config ----
    camera_info info_;
    knst_vector<format> formats_;
    open_options opt_;
    format cur_fmt_;
    [[maybe_unused]] uint32_t cur_stride_ = 0;
    std::atomic<bool> open_{false};
    mutable std::mutex dev_mtx_;                // guards fd_/buffers_/cur_fmt_ (lock order: stream_mtx_ -> dev_mtx_)
    std::mutex stream_mtx_;                     // serializes start/stop/grab

    // ---- streaming ----
    std::atomic<bool> streaming_{false};
    std::atomic<bool> stream_on_{false};
    std::thread worker_;
    std::atomic<std::thread::id> worker_tid_{std::thread::id()};

    // ---- callbacks ----
    std::mutex cb_mtx_;
    frame_callback user_cb_ = nullptr; void* user_cb_data_ = nullptr;
    state_callback state_cb_ = nullptr; void* state_cb_data_ = nullptr;
    uint32_t event_mask_ = default_event_mask();

    // ---- last frame ----
    std::mutex frame_mtx_;
    std::condition_variable frame_cv_;
    frame last_frame_;
    uint64_t frame_counter_ = 0;

    // ---- stats ----
    std::atomic<uint64_t> frames_captured_{0}, frames_dropped_{0}, frames_corrupt_{0}, frames_recorded_{0}, reconnects_{0};
    std::atomic<uint64_t> first_frame_us_{0}, last_frame_us_{0};
    std::atomic<uint64_t> snap_counter_{0};

    // ---- errors ----
    std::atomic<errc> last_error_{errc::Ok};
    mutable std::mutex err_mtx_;
    knst_c16string last_msg_;

    // ---- recording ----
    std::mutex rec_ctl_mtx_;                    // serializes start_recording / stop_recording
    mutable std::mutex rec_mtx_;                // guards writer + the fields below
    knst_vid::video_writer rec_writer_;
    knst_vid::video_writer::config rec_vc_;
    record_options rec_opts_;
    std::atomic<bool> recording_{false}, rec_paused_{false}, rec_owns_stream_{false};
    bool rec_open_ = false, rec_video_ = false, rec_audio_ = false, rec_t0_valid_ = false;
    uint64_t rec_t0_ = 0, pause_total_ = 0, pause_begin_ = 0, resume_ts_ = 0;
    uint64_t rec_seg_offset_us_ = 0, rec_bytes_before_ = 0, rec_bytes_total_ = 0, rec_last_pts_us_ = 0;
    uint64_t rec_audio_frames_ = 0;
    uint32_t rec_seg_index_ = 0;
    bool rec_split_ = false;
    knst_c16string rec_base_path_, rec_cur_path_;
    knst_byte_string rec_tmp_;
    std::atomic<uint32_t> anchor_gen_{0};

    // ---- audio ----
    std::thread audio_worker_;
    std::atomic<bool> audio_running_{false};
#if KNST_CAM_HAS_AUDIO
    knst_aud::stream audio_stream_;
#endif

    // ---- motion ----
    std::mutex motion_mtx_;
    std::atomic<bool> motion_enabled_{false}, motion_active_{false}, motion_rec_started_{false};
    motion_options motion_opts_;
    knst_vid::motion_detector motion_det_;
    uint64_t motion_check_ms_ = 0, motion_last_ms_ = 0;
    bool motion_check_valid_ = false;

#if KNST_CAM_LINUX_V4L2
    int fd_ = -1;
    struct mapped_buffer { void* start = nullptr; size_t length = 0; };
    knst_vector<mapped_buffer> buffers_;
    uint32_t current_fourcc_ = 0;
#elif KNST_CAM_ANDROID
    struct a_pending_ctrl {
        enum kind_t : uint8_t { I32, U8 } kind = I32;
        uint32_t tag   = 0;
        int32_t  value = 0;
    };
    knst_vector<a_pending_ctrl> a_pending_ctrls_;
    std::atomic<bool>           a_disconnected_{false};
    ACameraManager*               a_mgr_     = nullptr;
    ACameraDevice*                a_dev_     = nullptr;
    AImageReader*                 a_reader_  = nullptr;
    ANativeWindow*                a_window_  = nullptr;
    ACaptureSessionOutputContainer* a_outputs_ = nullptr;
    ACaptureSessionOutput*        a_output_  = nullptr;
    ACameraOutputTarget*          a_target_  = nullptr;
    ACaptureRequest*              a_request_ = nullptr;
    ACameraCaptureSession*        a_session_ = nullptr;
    int32_t                       a_format_  = 0;
    uint32_t                      a_stride_  = 0;
    knst_c16string                a_cam_id_;
    std::mutex                    a_pending_mtx_;
    std::condition_variable       a_pending_cv_;
    knst_byte_string              a_pending_data_;
    uint32_t                      a_pending_w_   = 0;
    uint32_t                      a_pending_h_   = 0;
    uint64_t                      a_pending_ts_  = 0;
    uint32_t                      a_pending_seq_ = 0;
    bool                          a_pending_ok_  = false;
    [[maybe_unused]] bool         a_grab_mode_   = false;
#elif KNST_CAM_WINDOWS
    ::IMFSourceReader*    mf_reader_   = nullptr;
    ::IMFMediaSource*     mf_source_   = nullptr;
    ::IAMVideoProcAmp*    mf_proc_amp_ = nullptr;
    ::IAMCameraControl*   mf_cam_ctrl_ = nullptr;
    GUID                  mf_subtype_  = GUID_NULL;
    uint32_t              mf_stride_   = 0;
    uint32_t              mf_seq_      = 0;
#endif
    // ---- helpers ----
    errc set_error_(errc e, const char* msg) noexcept {
        last_error_.store(e);
        std::lock_guard<std::mutex> lk(err_mtx_);
        last_msg_ = knst_c16string(msg ? msg : "");
        return e;
    }
    result<void> fail_void_(errc e, const char* msg) noexcept { set_error_(e, msg); return void_result(e); }
    template <class T> result<T> fail_t_(errc e, const char* msg) noexcept { set_error_(e, msg); return err_result<T>(e); }

    void push_event(event e) noexcept;
    bool on_worker_thread_() const noexcept { return worker_tid_.load() == std::this_thread::get_id(); }
    result<void> open_impl_(const knst_c16string& id, const open_options& opt, const knst_vector<format>* preset) noexcept;
    void reap_worker_() noexcept;               // stream_mtx_ must be held

    void process_frame_(const uint8_t* data, uint32_t size, uint32_t w, uint32_t h, uint32_t stride,
                        pixel_format pix, uint64_t ts_us, uint32_t seq) noexcept;
    void handle_motion_(const uint8_t* data, uint32_t size, uint32_t w, uint32_t h, uint32_t stride,
                        pixel_format pix, uint64_t ts_us) noexcept;

    void record_frame_(const uint8_t* data, uint32_t size, uint32_t w, uint32_t h, uint32_t stride,
                       pixel_format pix, uint64_t ts_us) noexcept;
    rec_result record_video_locked_(const uint8_t* data, uint32_t size, uint32_t w, uint32_t h, uint32_t stride,
                                    pixel_format pix, uint64_t ts_us) noexcept;
    bool roll_segment_locked_(uint64_t pts) noexcept;
    void finalize_recording_(bool limit_reached, bool error) noexcept;
    void release_owned_stream_() noexcept;
    errc open_audio_(const record_options& o) noexcept;
    void audio_loop_() noexcept;
    void join_audio_() noexcept;

    errc platform_open_(const knst_c16string& path, const open_options& opt) noexcept;
    void platform_close_() noexcept;
    errc platform_stream_on_() noexcept;        // dev_mtx_ must be held
    void platform_stream_off_() noexcept;       // dev_mtx_ must be held
    void worker_loop_() noexcept;
#if KNST_CAM_LINUX_V4L2
    bool recover_stream_() noexcept;
    result<void> set_raw_(uint32_t cid, int32_t value) noexcept;   // dev_mtx_ must be held
#elif KNST_CAM_WINDOWS
    bool recover_stream_() noexcept;
#endif
};

// ============================================================
// camera_array
// ============================================================
class camera_array {
public:
    camera_array() noexcept = default;
    ~camera_array() { close_all(); }
    camera_array(const camera_array&) = delete;
    camera_array& operator=(const camera_array&) = delete;

    result<void> open_all(const open_options& opt) noexcept {
        close_all();
        auto cams = camera::enumerate();
        if (!cams) return void_result(errc::NotFound);
        for (uint32_t i = 0; i < cams.value.size(); ++i) {
            camera* c = new (std::nothrow) camera();
            if (!c) break;
            if (c->open(cams.value[i], opt)) cams_.push_back(c); else delete c;
        }
        if (cams_.empty()) return void_result(errc::NotFound);
        return ok_void();
    }

    uint32_t count() const noexcept { return cams_.size(); }
    camera& at(uint32_t i) noexcept { return *cams_[i]; }
    camera& operator[](uint32_t i) noexcept { return *cams_[i]; }

    void close_all() noexcept {
        for (uint32_t i = 0; i < cams_.size(); ++i) { cams_[i]->close(); delete cams_[i]; }
        cams_.clear();
    }

    /// One photo per camera into `dir` (files named <prefix>cam<N>_<timestamp>.<ext>).
    result<knst_vector<knst_c16string>> snapshot_all(const knst_c16string& dir, const image_options& o = image_options()) noexcept {
        using R = result<knst_vector<knst_c16string>>;
        knst_vector<knst_c16string> out;
        for (uint32_t i = 0; i < cams_.size(); ++i) {
            char pre[32]; std::snprintf(pre, sizeof(pre), "cam%u_", i);
            auto r = cams_[i]->snapshot_auto(dir, knst_c16string(pre), o);
            if (r) out.push_back(r.value);
        }
        if (out.empty()) return R::fail(status::NotFound);
        return R::success(std::move(out));
    }

private:
    knst_vector<camera*> cams_;
};

// ============================================================
// Common implementation
// ============================================================
inline void camera::push_event(event e) noexcept {
    state_callback cb; void* ud; uint32_t m;
    { std::lock_guard<std::mutex> lk(cb_mtx_); cb = state_cb_; ud = state_cb_data_; m = event_mask_; }
    if (cb && (m & event_bit(e))) cb(ud, e);
}

inline void camera::set_event_callback(state_callback cb, void* user) noexcept {
    std::lock_guard<std::mutex> lk(cb_mtx_); state_cb_ = cb; state_cb_data_ = user;
}
inline void camera::set_event_mask(uint32_t mask) noexcept {
    std::lock_guard<std::mutex> lk(cb_mtx_); event_mask_ = mask;
}

inline result<camera_info> camera::default_camera() noexcept {
    using R = result<camera_info>;
    auto all = enumerate();
    if (!all || all.value.empty()) return err_result<camera_info>(errc::NotFound);
    return R::success(all.value[0]);
}

inline result<camera_info> camera::find(const knst_c16string& id) noexcept {
    using R = result<camera_info>;
    auto all = enumerate();
    if (!all) return err_result<camera_info>(errc::NotFound);
    for (uint32_t i = 0; i < all.value.size(); ++i)
        if (all.value[i].device_id == id) return R::success(all.value[i]);
    return err_result<camera_info>(errc::NotFound);
}

inline result<knst_vector<knst_c16string>> camera::list_audio_sources() noexcept {
    using R = result<knst_vector<knst_c16string>>;
#if KNST_CAM_HAS_AUDIO
    auto srcs = knst_aud::audio::list_sources();
    if (!srcs) return R::fail(srcs.st, srcs.native);
    knst_vector<knst_c16string> out;
    for (auto& d : srcs.value) {
        if (!d.is_input || d.name.contains(u".monitor") || d.name.contains(u"Monitor")) continue;
        out.push_back(d.name);
    }
    return R::success(std::move(out));
#else
    return R::fail(status::NotSupported);
#endif
}

inline result<void> camera::open(const camera_info& info, const open_options& opt) noexcept {
    auto r = open_impl_(info.device_id, opt, &info.formats);
    if (r) {
        if (info_.name.empty()) info_.name = info.name;
        if (info_.bus_info.empty()) info_.bus_info = info.bus_info;
        if (info_.driver.empty()) info_.driver = info.driver;
        info_.vid = info.vid; info_.pid = info.pid; info_.index = info.index;
        info_.is_default = info.is_default; info_.has_audio = info.has_audio;
    }
    return r;
}

inline result<void> camera::open(const knst_c16string& device_id, const open_options& opt) noexcept {
    return open_impl_(device_id, opt, nullptr);
}

inline result<void> camera::open_impl_(const knst_c16string& id, const open_options& opt,
                                       const knst_vector<format>* preset) noexcept {
    if (on_worker_thread_()) return fail_void_(errc::BadState, "open() must not be called from a callback");
    close();
    if (!detail::path_ok(id)) return fail_void_(errc::InvalidArgument, "invalid device path");
    if (!opt.use_default_format) {
        if (opt.width == 0 || opt.height == 0 || opt.width > KNST_CAMERA_MAX_DIMENSION || opt.height > KNST_CAMERA_MAX_DIMENSION)
            return fail_void_(errc::InvalidArgument, "invalid resolution");
        if (opt.fps_num == 0 || opt.fps_den == 0) return fail_void_(errc::InvalidArgument, "invalid frame rate");
    }
    if (opt.buffer_count > 32) return fail_void_(errc::InvalidArgument, "buffer_count too large (max 32)");

    std::lock_guard<std::mutex> lk(dev_mtx_);
    opt_ = opt;
    info_ = camera_info();
    info_.device_id = id;
    formats_.clear();
    if (preset) formats_ = *preset;
    errc e = platform_open_(id, opt);
    if (e != errc::Ok) return fail_void_(e, "cannot open the camera (see errc / permissions on /dev/video*)");
    if (formats_.empty()) formats_.push_back(cur_fmt_);
    if (info_.name.empty()) info_.name = id;
    if (info_.max_width < cur_fmt_.width) info_.max_width = cur_fmt_.width;
    if (info_.max_height < cur_fmt_.height) info_.max_height = cur_fmt_.height;
    info_.formats = formats_;
    frames_captured_ = 0; frames_dropped_ = 0; frames_corrupt_ = 0; frames_recorded_ = 0; reconnects_ = 0;
    first_frame_us_ = 0; last_frame_us_ = 0;
    open_.store(true);
    return ok_void();
}

inline result<void> camera::close() noexcept {
    if (on_worker_thread_()) return fail_void_(errc::BadState, "close() must not be called from a callback");
    disable_motion_detection();
    stop_recording();
    stop();
    join_audio_();
    {
        std::lock_guard<std::mutex> lk(dev_mtx_);
        platform_close_();
    }
#if KNST_CAM_ANDROID
    // Reset Android-specific per-connection state. Must live HERE (public
    // close path), NOT inside platform_close_ — reconnect calls
    // platform_close_ internally and must preserve a_pending_ctrls_ and
    // keep a_disconnected_ == true until the reopen actually succeeds.
    a_pending_ctrls_.clear();
    a_disconnected_.store(false);
#endif
    open_.store(false);
    {
        std::lock_guard<std::mutex> lk(frame_mtx_);
        last_frame_.data.clear(); last_frame_.valid = false;
    }
    frame_cv_.notify_all();
    return ok_void();
}

inline result<format> camera::current_format() const noexcept {
    if (!open_.load()) return err_result<format>(errc::BadState);
    std::lock_guard<std::mutex> lk(dev_mtx_);
    return result<format>::success(cur_fmt_);
}

inline double camera::effective_fps() const noexcept {
    uint64_t n = frames_captured_.load(std::memory_order_relaxed);
    uint64_t a = first_frame_us_.load(std::memory_order_relaxed), b = last_frame_us_.load(std::memory_order_relaxed);
    if (n < 2 || b <= a) return 0.0;
    return (double)(n - 1) * 1e6 / (double)(b - a);
}

inline camera_stats camera::stats() const noexcept {
    camera_stats s;
    s.frames_captured = frames_captured_.load(); s.frames_dropped = frames_dropped_.load();
    s.frames_corrupt = frames_corrupt_.load(); s.frames_recorded = frames_recorded_.load();
    s.reconnects = reconnects_.load(); s.fps = effective_fps();
    return s;
}

inline void camera::reap_worker_() noexcept {
    if (worker_.joinable() && !on_worker_thread_() && !streaming_.load()) {
        worker_.join();
        worker_tid_.store(std::thread::id());
    }
}

inline result<void> camera::start(frame_callback cb, void* user, state_callback ev_cb, void* ev_user) noexcept {
    if (on_worker_thread_()) return fail_void_(errc::BadState, "start() must not be called from a callback");
    if (!open_.load()) return fail_void_(errc::BadState, "camera is not open");
    {
        std::lock_guard<std::mutex> lk(cb_mtx_);
        user_cb_ = cb; user_cb_data_ = user;
        if (ev_cb) { state_cb_ = ev_cb; state_cb_data_ = ev_user; }
    }
    std::lock_guard<std::mutex> slk(stream_mtx_);
    if (streaming_.load()) return ok_void();
    reap_worker_();
    {
        std::lock_guard<std::mutex> dlk(dev_mtx_);
        errc e = platform_stream_on_();
        if (e != errc::Ok) return fail_void_(e, "cannot start streaming");
    }
    frames_captured_ = 0; frames_dropped_ = 0; frames_corrupt_ = 0; first_frame_us_ = 0; last_frame_us_ = 0;
    streaming_.store(true);
    worker_ = std::thread([this]() { worker_loop_(); });
    return ok_void();
}

inline result<void> camera::stop() noexcept {
    if (on_worker_thread_()) return fail_void_(errc::BadState, "stop() must not be called from a callback");
    std::lock_guard<std::mutex> slk(stream_mtx_);
    streaming_.store(false);
    if (worker_.joinable()) { worker_.join(); worker_tid_.store(std::thread::id()); }
    {
        std::lock_guard<std::mutex> dlk(dev_mtx_);
        platform_stream_off_();
    }
    frame_cv_.notify_all();
    return ok_void();
}

// ---------------- frame pipeline ----------------
inline void camera::process_frame_(const uint8_t* data, uint32_t size, uint32_t w, uint32_t h, uint32_t stride,
                                   pixel_format pix, uint64_t ts_us, uint32_t seq) noexcept {
    if (frames_captured_.fetch_add(1, std::memory_order_relaxed) == 0) first_frame_us_.store(ts_us);
    last_frame_us_.store(ts_us);

    {   // keep a copy for snapshots
        std::unique_lock<std::mutex> lk(frame_mtx_);
        last_frame_.data.resize(size);
        if (last_frame_.data.length() == size) {
            std::memcpy(&last_frame_.data[0], data, size);
            last_frame_.width = w; last_frame_.height = h; last_frame_.stride = stride;
            last_frame_.pix = pix; last_frame_.timestamp_us = ts_us; last_frame_.sequence = seq;
            last_frame_.valid = true;
            ++frame_counter_;
        }
        lk.unlock();
        frame_cv_.notify_all();
    }

    frame_callback cb; void* ud;
    { std::lock_guard<std::mutex> lk(cb_mtx_); cb = user_cb_; ud = user_cb_data_; }
    if (cb) {
        frame_view v;
        v.data = data; v.size = size; v.width = w; v.height = h; v.stride = stride;
        v.pix = pix; v.timestamp_us = ts_us; v.sequence = seq;
        cb(ud, v);
    }
    if (motion_enabled_.load(std::memory_order_relaxed)) handle_motion_(data, size, w, h, stride, pix, ts_us);
    if (recording_.load(std::memory_order_acquire)) record_frame_(data, size, w, h, stride, pix, ts_us);
    push_event(event::Frame);
}

inline result<void> camera::enable_motion_detection(const motion_options& mo) noexcept {
    if (mo.auto_record) {
        if (!detail::path_ok(mo.dir)) return fail_void_(errc::InvalidArgument, "motion auto_record needs a directory");
        if (!mo.record.enable_video) return fail_void_(errc::InvalidArgument, "motion recording needs enable_video");
    }
    std::lock_guard<std::mutex> lk(motion_mtx_);
    motion_opts_ = mo;
    motion_det_ = knst_vid::motion_detector(mo.detector);
    motion_check_valid_ = false; motion_last_ms_ = 0;
    motion_active_.store(false);
    motion_enabled_.store(true);
    return ok_void();
}

inline void camera::disable_motion_detection() noexcept {
    bool stop_rec = false;
    {
        std::lock_guard<std::mutex> lk(motion_mtx_);
        motion_enabled_.store(false);
        motion_active_.store(false);
        stop_rec = motion_rec_started_.exchange(false);
    }
    if (stop_rec) stop_recording();
}

inline void camera::handle_motion_(const uint8_t* data, uint32_t size, uint32_t w, uint32_t h, uint32_t stride,
                                   pixel_format pix, uint64_t ts_us) noexcept {
    const uint64_t ts_ms = ts_us / 1000;
    int action = 0; bool auto_rec = false; record_options ro;
    {
        std::lock_guard<std::mutex> lk(motion_mtx_);
        if (!motion_enabled_.load()) return;
        if (motion_check_valid_ && ts_ms >= motion_check_ms_ &&
            ts_ms - motion_check_ms_ < motion_opts_.detector.min_interval_ms) return;
        motion_check_ms_ = ts_ms; motion_check_valid_ = true;
        knst_byte_string rgb; uint32_t rw = 0, rh = 0;
        if (detail::to_rgb24(data, size, w, h, stride, pix, rgb, rw, rh) != errc::Ok) return;
        const bool m = motion_det_.feed_rgb24(rgb.data(), rw, rh, ts_ms);
        if (m) {
            motion_last_ms_ = ts_ms;
            if (!motion_active_.load()) { motion_active_.store(true); action = 1; }
        } else if (motion_active_.load() && ts_ms >= motion_last_ms_ && ts_ms - motion_last_ms_ >= motion_opts_.post_roll_ms) {
            motion_active_.store(false); action = 2;
        }
        auto_rec = motion_opts_.auto_record;
        if (action == 1 && auto_rec) {
            ro = motion_opts_.record;
            if (ro.cont == knst_vid::container::Unknown) ro.cont = knst_vid::container::MKV;
            knst_c16string name = motion_opts_.prefix;
            name.append(detail::timestamp_string());
            name.append(ro.cont == knst_vid::container::AVI ? u".avi" : ro.cont == knst_vid::container::MP4 ? u".mp4"
                        : ro.cont == knst_vid::container::MOV ? u".mov" : u".mkv");
            ro.path = knst_file::join_path(motion_opts_.dir, name);
        }
    }
    if (action == 1) {
        push_event(event::MotionStarted);
        if (auto_rec && !recording_.load() && start_recording(ro)) motion_rec_started_.store(true);
    } else if (action == 2) {
        push_event(event::MotionStopped);
        if (motion_rec_started_.exchange(false)) stop_recording();
    }
}

inline result<frame> camera::capture_latest(uint32_t timeout_ms) noexcept {
    using R = result<frame>;
    if (!open_.load()) return fail_t_<frame>(errc::BadState, "camera is not open");
    if (timeout_ms == 0) timeout_ms = KNST_CAMERA_GRAB_TIMEOUT_MS;
    if (on_worker_thread_()) {                  // inside a callback the newest frame is already stored
        std::lock_guard<std::mutex> lk(frame_mtx_);
        if (!last_frame_.valid) return fail_t_<frame>(errc::NotFound, "no frame yet");
        frame f = last_frame_;
        return R::success(std::move(f));
    }
    if (streaming_.load()) {
        std::unique_lock<std::mutex> lk(frame_mtx_);
        const uint64_t start = frame_counter_;
        frame_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                           [&]() { return frame_counter_ != start || !streaming_.load(); });
        if (frame_counter_ == start || !last_frame_.valid) return fail_t_<frame>(errc::Timeout, "no new frame arrived in time");
        frame f = last_frame_;
        return R::success(std::move(f));
    }
    return grab(UINT32_MAX, timeout_ms);
}

// ---------------- snapshots ----------------
inline result<void> camera::snapshot(const knst_c16string& path, const image_options& o) noexcept {
    if (!detail::path_ok(path)) return fail_void_(errc::InvalidArgument, "invalid output path");
    auto fr = capture_latest();
    if (!fr) return result<void>::fail(fr.st, fr.native);
    auto r = save_frame(fr.value, path, o);
    if (!r) { set_error_(errc::IoError, "could not encode/write the image"); return r; }
    push_event(event::SnapshotSaved);
    return ok_void();
}
inline result<void> camera::snapshot_jpeg(const knst_c16string& path, int quality) noexcept {
    image_options o; o.format = image_format::JPEG; o.jpeg_quality = quality; return snapshot(path, o);
}
inline result<void> camera::snapshot_bmp(const knst_c16string& path) noexcept {
    image_options o; o.format = image_format::BMP; return snapshot(path, o);
}
inline result<void> camera::snapshot_png(const knst_c16string& path) noexcept {
    image_options o; o.format = image_format::PNG; return snapshot(path, o);
}

inline result<knst_byte_string> camera::snapshot_memory(const image_options& o) noexcept {
    using R = result<knst_byte_string>;
    auto fr = capture_latest();
    if (!fr) return R::fail(fr.st, fr.native);
    knst_byte_string out; image_format used = image_format::JPEG;
    errc e = encode_frame(fr.value, o, out, used);
    if (e != errc::Ok) return fail_t_<knst_byte_string>(e, "could not encode the image");
    return R::success(std::move(out));
}

inline result<knst_c16string> camera::snapshot_auto(const knst_c16string& dir, const knst_c16string& prefix,
                                                    const image_options& o) noexcept {
    using R = result<knst_c16string>;
    if (!detail::path_ok(dir)) return fail_t_<knst_c16string>(errc::InvalidArgument, "invalid directory");
    image_format fmt = o.format == image_format::Auto ? image_format::JPEG : o.format;
    char num[16]; std::snprintf(num, sizeof(num), "_%04u", (unsigned)(snap_counter_.fetch_add(1) % 10000));
    knst_c16string name = prefix;
    name.append(detail::timestamp_string());
    name.append(knst_c16string(num));
    name.append(fmt == image_format::BMP ? u".bmp" : fmt == image_format::PNG ? u".png"
                : fmt == image_format::PPM ? (o.grey ? u".pgm" : u".ppm") : u".jpg");
    knst_c16string path = knst_file::join_path(dir, name);
    image_options oo = o; oo.format = fmt;
    auto r = snapshot(path, oo);
    if (!r) return R::fail(r.st, r.native);
    return R::success(std::move(path));
}

inline result<knst_vector<knst_c16string>> camera::snapshot_burst(const knst_c16string& dir, uint32_t count,
                                                                  uint32_t interval_ms, const knst_c16string& prefix,
                                                                  const image_options& o) noexcept {
    using R = result<knst_vector<knst_c16string>>;
    if (count == 0 || count > 10000) return fail_t_<knst_vector<knst_c16string>>(errc::InvalidArgument, "count must be 1..10000");
    knst_vector<knst_c16string> out;
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t t0 = detail::now_us();
        auto r = snapshot_auto(dir, prefix, o);
        if (r) out.push_back(r.value);
        if (i + 1 < count && interval_ms) {
            const uint64_t spent = (detail::now_us() - t0) / 1000;
            if (spent < interval_ms) std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms - spent));
        }
    }
    if (out.empty()) return fail_t_<knst_vector<knst_c16string>>(errc::IoError, "no photo could be taken");
    return R::success(std::move(out));
}

// ---------------- audio ----------------
inline void camera::join_audio_() noexcept {
#if KNST_CAM_HAS_AUDIO
    if (audio_worker_.joinable() && audio_worker_.get_id() != std::this_thread::get_id()) {
        audio_running_.store(false);
        audio_worker_.join();
        audio_stream_.close();
    }
#endif
}

inline errc camera::open_audio_(const record_options& o) noexcept {
#if KNST_CAM_HAS_AUDIO
    knst_audio_device dev; bool have = false;
    auto srcs = knst_aud::audio::list_sources();
    if (!srcs) return errc::NotFound;
    auto is_mon = [](const knst_c16string& n) { return n.contains(u".monitor") || n.contains(u"Monitor"); };
    if (!o.audio_source.empty()) {
        for (auto& d : srcs.value)
            if (d.is_input && (d.name == o.audio_source || d.description == o.audio_source)) { dev = d; have = true; break; }
    } else {
        auto def = knst_aud::audio::get_default(true);
        if (def && !is_mon(def.value.name)) { dev = def.value; have = true; }
        if (!have) for (auto& d : srcs.value) if (d.is_input && !is_mon(d.name)) { dev = d; have = true; break; }
    }
    if (!have) return errc::NotFound;
    knst_aud::stream::config sc;
    sc.rate = o.audio_rate; sc.channels = o.audio_channels; sc.buffer_frames = 1024; sc.is_output = false;
    auto st = knst_aud::stream::open(dev, sc);
    if (!st) return st.st == status::PermissionDenied ? errc::PermissionDenied : errc::IoError;
    audio_stream_ = std::move(st.value);
    return errc::Ok;
#else
    (void)o;
    return errc::NotSupported;
#endif
}

inline void camera::audio_loop_() noexcept {
#if KNST_CAM_HAS_AUDIO
    const uint32_t ch = rec_opts_.audio_channels, rate = rec_opts_.audio_rate;
    const knst_vid::audio_codec codec = rec_opts_.audio_codec;
    const uint32_t bps = knst_vid::audio_codec_bits(codec) / 8;
    const uint32_t chunk = 1024;
    if (!bps || !ch || !rate) { audio_running_.store(false); return; }
    knst_vector<float> fbuf; fbuf.resize(chunk * ch);
    knst_byte_string pbuf; pbuf.resize(chunk * ch * bps);
    if (fbuf.size() != chunk * ch || pbuf.length() != chunk * ch * bps) { audio_running_.store(false); return; }

    uint32_t seen_gen = ~0u; uint64_t anchor_pts = 0, anchor_frames = 0;
    bool limit = false, io_error = false, audio_lost = false;
    const uint64_t chunk_us = detail::muldiv(chunk, 1000000ull, rate);

    while (audio_running_.load(std::memory_order_acquire)) {
        auto r = audio_stream_.read_interleaved(fbuf.data(), chunk);
        if (!r) { audio_lost = true; break; }
        if (r.value == 0) continue;
        const uint64_t arrive = detail::now_us();
        const uint32_t samples = r.value * ch;
        if (rec_opts_.audio_remove_dc) knst_aud::dsp::dc_remove_interleaved(fbuf.data(), r.value, ch);
        if (rec_opts_.audio_gain_db != 0.0f) knst_aud::dsp::apply_gain_db(fbuf.data(), samples, rec_opts_.audio_gain_db);
        for (uint32_t i = 0; i < samples; ++i) knst_vid::audio::f32_to_sample(fbuf[i], &pbuf[i * bps], codec);

        std::lock_guard<std::mutex> lk(rec_mtx_);
        if (!rec_open_ || !rec_audio_) break;
        if (rec_paused_.load() || !rec_t0_valid_) { seen_gen = ~0u; continue; }
        const uint32_t gen = anchor_gen_.load();
        if (gen != seen_gen) {
            const uint64_t start = arrive > chunk_us ? arrive - chunk_us : 0;
            const uint64_t base = rec_t0_ + pause_total_;
            anchor_pts = start > base ? start - base : 0;
            anchor_frames = 0; seen_gen = gen;
        }
        const uint64_t pts = anchor_pts + detail::muldiv(anchor_frames, 1000000ull, rate);
        auto w = rec_writer_.write_audio(pbuf.data(), samples * bps, pts);
        if (w) {
            anchor_frames += r.value; rec_audio_frames_ += r.value;
            if (!rec_video_) {                  // audio only: limits are measured on the audio clock
                const uint64_t elapsed = rec_seg_offset_us_ + pts + chunk_us;
                rec_bytes_total_ = rec_bytes_before_ + rec_writer_.bytes_written();
                rec_last_pts_us_ = elapsed;
                if ((rec_opts_.max_duration_ms && elapsed >= (uint64_t)rec_opts_.max_duration_ms * 1000ull) ||
                    (rec_opts_.max_bytes && rec_bytes_total_ >= rec_opts_.max_bytes)) { limit = true; break; }
            }
        } else if (rec_writer_.last_error() == knst_vid::errc::IoError) { io_error = true; break; }
        else if (rec_writer_.last_error() == knst_vid::errc::TooLarge) { limit = true; break; }
    }
    if (limit) finalize_recording_(true, false);
    else if (io_error) finalize_recording_(false, true);
    else if (audio_lost && recording_.load()) {
        set_error_(errc::IoError, "audio capture failed; recording continues without sound");
        push_event(event::Error);
    }
#endif
}

// ---------------- recording ----------------
inline result<void> camera::start_recording(const record_options& opt) noexcept {
    std::lock_guard<std::mutex> ctl(rec_ctl_mtx_);
    if (recording_.load()) return fail_void_(errc::BadState, "already recording");
    join_audio_();                              // reap a thread left over from a finished recording

    if (!detail::path_ok(opt.path)) return fail_void_(errc::InvalidArgument, "invalid output path");
    if (!opt.enable_video && !opt.enable_audio) return fail_void_(errc::InvalidArgument, "enable_video and enable_audio are both false");
    if (opt.enable_video && !open_.load()) return fail_void_(errc::BadState, "camera is not open");

    knst_vid::container cont = opt.cont != knst_vid::container::Unknown ? opt.cont : detail::container_from_path(opt.path);
    if (cont == knst_vid::container::Unknown) return fail_void_(errc::InvalidArgument, "cannot deduce the container: use .avi/.mkv/.mp4/.mov/.wav or set record_options::cont");
    if (cont == knst_vid::container::WebM) return fail_void_(errc::NotSupported, "WebM cannot be written (needs VP8/VP9); use .mkv");
    if (cont == knst_vid::container::RAW || cont == knst_vid::container::Unknown) return fail_void_(errc::NotSupported, "unsupported container");

    if (opt.enable_video) {
        if (cont == knst_vid::container::WAV) return fail_void_(errc::InvalidArgument, "WAV holds audio only");
        if (opt.vcodec != knst_vid::video_codec::MJPEG && opt.vcodec != knst_vid::video_codec::RAW)
            return fail_void_(errc::NotSupported, "only MJPEG or RAW(AVI) video can be written: no H.264/H.265 encoder exists in the library");
        if (opt.vcodec == knst_vid::video_codec::RAW && cont != knst_vid::container::AVI)
            return fail_void_(errc::NotSupported, "RAW video can only be stored in AVI");
        if (opt.jpeg_quality < 1 || opt.jpeg_quality > 100) return fail_void_(errc::InvalidArgument, "jpeg_quality must be 1..100");
    } else {
        if (cont != knst_vid::container::WAV && cont != knst_vid::container::MKV)
            return fail_void_(errc::InvalidArgument, "audio-only recording needs a .wav or .mkv file");
    }
    if (opt.enable_audio) {
#if !KNST_CAM_HAS_AUDIO
        return fail_void_(errc::NotSupported, "knst_audio.hpp is not available");
#endif
        if (!knst_vid::audio_codec_is_pcm(opt.audio_codec)) return fail_void_(errc::NotSupported, "only PCM audio can be recorded");
        if (opt.audio_channels < 1 || opt.audio_channels > KNST_CAMERA_MAX_AUDIO_CHANNELS) return fail_void_(errc::InvalidArgument, "audio_channels must be 1..8");
        if (opt.audio_rate < 8000 || opt.audio_rate > 192000) return fail_void_(errc::InvalidArgument, "audio_rate must be 8000..192000");
        if (!detail::container_has_audio(cont, opt.audio_codec))
            return fail_void_(errc::NotSupported, "this container cannot store this audio: MP4 is video-only here, MOV takes PCM16, use .mkv or .avi");
    } else if (cont == knst_vid::container::WAV) return fail_void_(errc::InvalidArgument, "WAV needs enable_audio");

    // audio device first (so a missing microphone fails before anything is created)
    if (opt.enable_audio) {
        errc ae = open_audio_(opt);
        if (ae != errc::Ok) return fail_void_(ae, ae == errc::NotFound ? "no microphone found" : "cannot open the microphone");
    }

    knst_vid::video_writer::config vc;
    vc.cont = cont; vc.vcodec = opt.vcodec; vc.has_video = opt.enable_video;
    vc.jpeg_quality = opt.jpeg_quality; vc.jpeg_sub = opt.jpeg_sub; vc.fill_gaps = true;
    if (opt.enable_video) {
        std::lock_guard<std::mutex> dl(dev_mtx_);
        vc.width = cur_fmt_.width; vc.height = cur_fmt_.height;
        vc.fps_num = cur_fmt_.fps_num ? cur_fmt_.fps_num : 30; vc.fps_den = cur_fmt_.fps_den ? cur_fmt_.fps_den : 1;
    }
    if (opt.enable_audio) {
        vc.acodec = opt.audio_codec; vc.sample_rate = opt.audio_rate; vc.channels = opt.audio_channels;
    }
    vc.meta = opt.meta;
    if (vc.meta.software.empty()) vc.meta.software = knst_c16string("KernelNucleusT knst_camera");

    const bool split = opt.split_ms > 0 || opt.split_bytes > 0;
    knst_c16string first_path = split ? detail::with_suffix(opt.path, 1) : opt.path;

    {
        std::lock_guard<std::mutex> lk(rec_mtx_);
        rec_vc_ = vc; rec_opts_ = opt; rec_opts_.cont = cont;
        auto r = rec_writer_.open(first_path, rec_vc_);
        if (!r) {
            const knst_vid::errc ve = rec_writer_.last_error();
            errc e = ve == knst_vid::errc::Ok ? errc::IoError : from_vid(ve);
#if KNST_CAM_HAS_AUDIO
            if (opt.enable_audio) audio_stream_.close();
#endif
            return fail_void_(e, "cannot create the output file");
        }
        rec_open_ = true; rec_video_ = opt.enable_video; rec_audio_ = opt.enable_audio;
        rec_split_ = split; rec_base_path_ = opt.path; rec_cur_path_ = first_path; rec_seg_index_ = 1;
        rec_t0_valid_ = false; rec_t0_ = 0; pause_total_ = 0; pause_begin_ = 0; resume_ts_ = 0;
        rec_seg_offset_us_ = 0; rec_bytes_before_ = 0; rec_bytes_total_ = 0; rec_last_pts_us_ = 0; rec_audio_frames_ = 0;
        if (!opt.enable_video) { rec_t0_ = detail::now_us(); rec_t0_valid_ = true; }
        frames_recorded_.store(0);
        anchor_gen_.fetch_add(1);
        rec_paused_.store(false);
        recording_.store(true);
    }

#if KNST_CAM_HAS_AUDIO
    if (opt.enable_audio) {
        audio_running_.store(true);
        audio_worker_ = std::thread([this]() { audio_loop_(); });
    }
#endif

    if (opt.enable_video && !streaming_.load()) {
        rec_owns_stream_.store(true);
        auto s = start(nullptr, nullptr);
        if (!s) {
            rec_owns_stream_.store(false);
            audio_running_.store(false);
            join_audio_();
            finalize_recording_(false, false);
            knst_file::remove_file(first_path);
            return s;
        }
    }
    push_event(event::RecordingStarted);
    return ok_void();
}

inline void camera::release_owned_stream_() noexcept {
    if (rec_owns_stream_.exchange(false)) streaming_.store(false);   // capture thread leaves its loop by itself
}

inline void camera::finalize_recording_(bool limit_reached, bool error) noexcept {
    bool was_open = false; errc werr = errc::Ok;
    {
        std::lock_guard<std::mutex> lk(rec_mtx_);
        if (rec_open_) {
            was_open = true;
            rec_bytes_total_ = rec_bytes_before_ + rec_writer_.bytes_written();
            auto r = rec_writer_.close();
            if (!r) werr = errc::IoError;
            rec_open_ = false;
        }
        recording_.store(false);
        rec_paused_.store(false);
        audio_running_.store(false);
    }
    if (!was_open) return;
    if (werr != errc::Ok || error) { set_error_(errc::IoError, "writing the recording failed (disk full?)"); push_event(event::Error); }
    if (limit_reached) push_event(event::RecordingLimitReached);
    push_event(event::RecordingStopped);
}

inline result<void> camera::stop_recording() noexcept {
    bool owns = false;
    {
        std::lock_guard<std::mutex> ctl(rec_ctl_mtx_);
        owns = rec_owns_stream_.exchange(false);
        audio_running_.store(false);
        join_audio_();
        finalize_recording_(false, false);
    }
    if (owns) {
        if (on_worker_thread_()) streaming_.store(false);
        else stop();
    }
    return ok_void();
}

inline result<void> camera::pause_recording() noexcept {
    {
        std::lock_guard<std::mutex> lk(rec_mtx_);
        if (!rec_open_) return fail_void_(errc::BadState, "not recording");
        if (rec_paused_.load()) return ok_void();
        rec_paused_.store(true);
        pause_begin_ = detail::now_us();
    }
    push_event(event::RecordingPaused);
    return ok_void();
}

inline result<void> camera::resume_recording() noexcept {
    {
        std::lock_guard<std::mutex> lk(rec_mtx_);
        if (!rec_open_) return fail_void_(errc::BadState, "not recording");
        if (!rec_paused_.load()) return ok_void();
        const uint64_t now = detail::now_us();
        if (rec_t0_valid_ && now > pause_begin_) pause_total_ += now - pause_begin_;
        resume_ts_ = now;
        anchor_gen_.fetch_add(1);
        rec_paused_.store(false);
    }
    push_event(event::RecordingResumed);
    return ok_void();
}

inline recording_status camera::recording_info() const noexcept {
    recording_status s;
    std::lock_guard<std::mutex> lk(rec_mtx_);
    s.active = rec_open_; s.paused = rec_paused_.load();
    s.has_video = rec_video_; s.has_audio = rec_audio_;
    s.frames = frames_recorded_.load();
    s.bytes = rec_open_ ? rec_bytes_before_ + rec_writer_.bytes_written() : rec_bytes_total_;
    s.seconds = (double)rec_last_pts_us_ / 1e6;
    s.segment = rec_seg_index_; s.path = rec_cur_path_;
    return s;
}

inline bool camera::roll_segment_locked_(uint64_t pts) noexcept {
    rec_bytes_before_ += rec_writer_.bytes_written();
    rec_seg_offset_us_ += pts;
    rec_writer_.close();
    ++rec_seg_index_;
    knst_c16string np = detail::with_suffix(rec_base_path_, rec_seg_index_);
    auto r = rec_writer_.open(np, rec_vc_);
    if (!r) { rec_open_ = false; return false; }
    rec_cur_path_ = np;
    rec_t0_valid_ = false;                      // next video frame starts the new segment at pts 0
    anchor_gen_.fetch_add(1);
    return true;
}

inline camera::rec_result camera::record_video_locked_(const uint8_t* data, uint32_t size, uint32_t w, uint32_t h,
                                                       uint32_t stride, pixel_format pix, uint64_t ts_us) noexcept {
    if (!rec_open_ || !rec_video_) return rec_result::Ok;
    if (rec_paused_.load() || ts_us < resume_ts_) return rec_result::Ok;
    if (!rec_t0_valid_) {
        rec_t0_ = ts_us; pause_total_ = 0; rec_t0_valid_ = true;
        anchor_gen_.fetch_add(1);
    }
    if (ts_us < rec_t0_ + pause_total_) return rec_result::Ok;
    const uint64_t pts = ts_us - rec_t0_ - pause_total_;

    result<void> r = ok_void();
    if (rec_vc_.vcodec == knst_vid::video_codec::MJPEG) {
        if (pix == pixel_format::MJPEG) {
            const uint8_t* p = data; uint32_t n = size;
            if (knst_vid::mjpeg::inject_std_dht(data, size, rec_tmp_)) { p = rec_tmp_.data(); n = rec_tmp_.length(); }
            r = rec_writer_.write_encoded(p, n, true, pts);
        } else r = rec_writer_.write_frame(data, w, h, pix, stride, pts);
    } else {                                    // RAW (AVI)
        if (pix == pixel_format::MJPEG) {
            knst_byte_string rgb; uint32_t rw = 0, rh = 0;
            if (detail::to_rgb24(data, size, w, h, stride, pix, rgb, rw, rh) != errc::Ok || rw != w || rh != h) {
                frames_dropped_.fetch_add(1); return rec_result::Ok;
            }
            r = rec_writer_.write_frame(rgb.data(), w, h, pixel_format::RGB24, 0, pts);
        } else r = rec_writer_.write_frame(data, w, h, pix, stride, pts);
    }
    if (!r) {
        const knst_vid::errc ve = rec_writer_.last_error();
        if (ve == knst_vid::errc::TooLarge) return rec_result::Limit;
        if (ve == knst_vid::errc::IoError) return rec_result::Error;
        frames_dropped_.fetch_add(1);
        return rec_result::Ok;
    }

    const uint64_t n = frames_recorded_.fetch_add(1) + 1;
    const uint64_t wb = rec_writer_.bytes_written();
    rec_bytes_total_ = rec_bytes_before_ + wb;
    const uint64_t elapsed = rec_seg_offset_us_ + pts;
    rec_last_pts_us_ = elapsed;

    if ((rec_opts_.max_duration_ms && elapsed >= (uint64_t)rec_opts_.max_duration_ms * 1000ull) ||
        (rec_opts_.max_frames && n >= rec_opts_.max_frames) ||
        (rec_opts_.max_bytes && rec_bytes_total_ >= rec_opts_.max_bytes)) return rec_result::Limit;

    if (rec_split_ && ((rec_opts_.split_ms && pts >= (uint64_t)rec_opts_.split_ms * 1000ull) ||
                       (rec_opts_.split_bytes && wb >= rec_opts_.split_bytes))) {
        if (!roll_segment_locked_(pts)) return rec_result::Error;
    }
    return rec_result::Ok;
}

inline void camera::record_frame_(const uint8_t* data, uint32_t size, uint32_t w, uint32_t h, uint32_t stride,
                                  pixel_format pix, uint64_t ts_us) noexcept {
    rec_result rr;
    { std::lock_guard<std::mutex> lk(rec_mtx_); rr = record_video_locked_(data, size, w, h, stride, pix, ts_us); }
    if (rr == rec_result::Ok) return;
    finalize_recording_(rr == rec_result::Limit, rr == rec_result::Error);
    release_owned_stream_();
}

// ============================================================
// Linux V4L2 implementation
// ============================================================
#if KNST_CAM_LINUX_V4L2

inline const char* camera::backend_name() noexcept { return "Linux V4L2"; }

inline result<knst_vector<camera_info>> camera::enumerate() noexcept {
    using R = result<knst_vector<camera_info>>;
    DIR* d = ::opendir("/sys/class/video4linux");
    if (!d) d = ::opendir("/dev");
    if (!d) return err_result<knst_vector<camera_info>>(errc::IoError);

    struct entry { knst_c16string path; char name[40]; uint32_t num; };
    knst_vector<entry> paths;
    while (struct dirent* e = ::readdir(d)) {
        const char* n = e->d_name;
        if (std::strncmp(n, "video", 5) != 0) continue;
        const char* p = n + 5;
        bool digits = (*p != 0);
        uint32_t num = 0;
        for (; *p; ++p) { if (*p < '0' || *p > '9' || num > 100000) { digits = false; break; } num = num * 10 + (uint32_t)(*p - '0'); }
        if (!digits || std::strlen(n) >= 39) continue;
        char full[64]; std::snprintf(full, sizeof(full), "/dev/%s", n);
        if (!detail::v4l2_is_capture_device(full)) continue;
        entry en; en.path = knst_c16string(full); std::strcpy(en.name, n); en.num = num;
        paths.push_back(en);
    }
    ::closedir(d);
    for (uint32_t i = 1; i < paths.size(); ++i)             // insertion sort by device number
        for (uint32_t j = i; j > 0 && paths[j - 1].num > paths[j].num; --j) { entry t = paths[j]; paths[j] = paths[j - 1]; paths[j - 1] = t; }

    knst_vector<camera_info> out; uint32_t idx = 0;
    for (uint32_t i = 0; i < paths.size(); ++i) {
        knst_byte_string cp; const char* c = detail::to_cpath(paths[i].path, cp);
        if (!c) continue;
        int fd = ::open(c, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) fd = ::open(c, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        struct v4l2_capability cap{};
        if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) { ::close(fd); continue; }
        camera_info ci;
        ci.index = idx++;
        ci.device_id = paths[i].path;
        detail::cstr16(reinterpret_cast<const char*>(cap.card), ci.name);
        detail::cstr16(reinterpret_cast<const char*>(cap.driver), ci.driver);
        detail::cstr16(reinterpret_cast<const char*>(cap.bus_info), ci.bus_info);
        detail::v4l2_probe_formats(fd, ci.formats, ci.max_width, ci.max_height);
        ci.has_video = !ci.formats.empty();
        if (ci.bus_info.contains(u"usb")) ci.has_audio = true;
        ::close(fd);
        char sp[160];
        std::snprintf(sp, sizeof(sp), "/sys/class/video4linux/%s/device/../idVendor", paths[i].name);
        detail::read_hex_file(sp, ci.vid);
        std::snprintf(sp, sizeof(sp), "/sys/class/video4linux/%s/device/../idProduct", paths[i].name);
        detail::read_hex_file(sp, ci.pid);
        out.push_back(std::move(ci));
    }
    if (out.empty()) return err_result<knst_vector<camera_info>>(errc::NotFound);
    out[0].is_default = true;
    return R::success(std::move(out));
}

inline errc camera::platform_open_(const knst_c16string& path, const open_options& opt) noexcept {
    knst_byte_string cp;
    const char* cpath = detail::to_cpath(path, cp);
    if (!cpath) return errc::InvalidArgument;
    char real[PATH_MAX];
    if (!detail::v4l2_resolve_path(cpath, real)) return errc::InvalidArgument;     // only /dev/videoN

    const uint32_t wanted_buffers = opt.buffer_count ? opt.buffer_count : 4;
    fd_ = ::open(real, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        const int e = errno;
        if (e == EACCES || e == EPERM) return errc::PermissionDenied;
        if (e == EBUSY) return errc::DeviceBusy;
        if (e == ENOENT || e == ENODEV) return errc::NotFound;
        return errc::IoError;
    }
    auto fail = [&](errc e) { platform_close_(); return e; };
    struct stat st{};
    if (::fstat(fd_, &st) != 0 || !S_ISCHR(st.st_mode)) return fail(errc::InvalidArgument);

    struct v4l2_capability cap{};
    if (::ioctl(fd_, VIDIOC_QUERYCAP, &cap) != 0) return fail(errc::IoError);
    const uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) return fail(errc::NotSupported);
    if (info_.name.empty()) detail::cstr16(reinterpret_cast<const char*>(cap.card), info_.name);
    if (info_.driver.empty()) detail::cstr16(reinterpret_cast<const char*>(cap.driver), info_.driver);
    if (info_.bus_info.empty()) detail::cstr16(reinterpret_cast<const char*>(cap.bus_info), info_.bus_info);

    if (formats_.empty()) {
        uint32_t mw = 0, mh = 0;
        detail::v4l2_probe_formats(fd_, formats_, mw, mh);
        info_.max_width = mw; info_.max_height = mh;
    }

    struct v4l2_format f{}; f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (opt.use_default_format) {
        if (::ioctl(fd_, VIDIOC_G_FMT, &f) != 0) return fail(errc::IoError);
    } else {
        open_options o = opt;
        int idx = detail::pick_format_index(formats_, opt);
        if (idx >= 0) {
            const format& c = formats_[(uint32_t)idx];
            const bool exact = c.width == opt.width && c.height == opt.height &&
                               (opt.pix == pixel_format::Unknown || c.pix == opt.pix);
            if (!exact && opt.strict_format) return fail(errc::NotSupported);
            o.width = c.width; o.height = c.height; o.pix = c.pix;
            if (!(opt.fps_num && opt.fps_den)) { o.fps_num = c.fps_num; o.fps_den = c.fps_den; }
        } else if (opt.strict_format) return fail(errc::NotSupported);
        f.fmt.pix.width = o.width; f.fmt.pix.height = o.height;
        f.fmt.pix.pixelformat = detail::pix_to_v4l2(o.pix);
        if (!f.fmt.pix.pixelformat) f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        f.fmt.pix.field = V4L2_FIELD_ANY;
        if (::ioctl(fd_, VIDIOC_S_FMT, &f) != 0) return fail(errc::NotSupported);
    }
    if (f.fmt.pix.width == 0 || f.fmt.pix.height == 0 ||
        f.fmt.pix.width > KNST_CAMERA_MAX_DIMENSION || f.fmt.pix.height > KNST_CAMERA_MAX_DIMENSION) return fail(errc::TooLarge);

    current_fourcc_ = f.fmt.pix.pixelformat;
    cur_fmt_ = format();
    cur_fmt_.width = f.fmt.pix.width; cur_fmt_.height = f.fmt.pix.height;
    cur_fmt_.pix = detail::v4l2_to_pix(current_fourcc_);
    cur_fmt_.is_compressed = (cur_fmt_.pix == pixel_format::MJPEG);
    if (cur_fmt_.pix == pixel_format::Unknown) return fail(errc::NotSupported);

    cur_stride_ = 0;                            // padded rows: use the driver stride if it is sane
    if (cur_fmt_.pix != pixel_format::MJPEG && f.fmt.pix.bytesperline) {
        knst_vid::plane_layout L;
        if (knst_vid::compute_layout(cur_fmt_.pix, cur_fmt_.width, cur_fmt_.height, f.fmt.pix.bytesperline, L))
            cur_stride_ = f.fmt.pix.bytesperline;
    }

    cur_fmt_.fps_num = opt.fps_num ? opt.fps_num : 30; cur_fmt_.fps_den = opt.fps_den ? opt.fps_den : 1;
    struct v4l2_streamparm parm{}; parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = cur_fmt_.fps_den;
    parm.parm.capture.timeperframe.denominator = cur_fmt_.fps_num;
    if (::ioctl(fd_, VIDIOC_S_PARM, &parm) == 0 &&
        parm.parm.capture.timeperframe.numerator && parm.parm.capture.timeperframe.denominator) {
        cur_fmt_.fps_num = parm.parm.capture.timeperframe.denominator;
        cur_fmt_.fps_den = parm.parm.capture.timeperframe.numerator;
    }

    struct v4l2_requestbuffers rb{};
    rb.count = wanted_buffers < 2 ? 2 : wanted_buffers;
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; rb.memory = V4L2_MEMORY_MMAP;
    if (::ioctl(fd_, VIDIOC_REQBUFS, &rb) != 0) return fail(errc::IoError);
    if (rb.count < 2) return fail(errc::IoError);

    buffers_.clear();
    for (uint32_t i = 0; i < rb.count; ++i) {
        struct v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        if (::ioctl(fd_, VIDIOC_QUERYBUF, &b) != 0) return fail(errc::IoError);
        if (b.length == 0 || b.length > KNST_CAMERA_MAX_FRAME_BYTES) return fail(errc::TooLarge);
        void* p = ::mmap(nullptr, b.length, PROT_READ, MAP_SHARED, fd_, b.m.offset);
        if (p == MAP_FAILED) return fail(errc::IoError);
        mapped_buffer mb; mb.start = p; mb.length = b.length;
        buffers_.push_back(mb);
        if (::ioctl(fd_, VIDIOC_QBUF, &b) != 0) return fail(errc::IoError);
    }
    stream_on_.store(false);
    return errc::Ok;
}

inline void camera::platform_close_() noexcept {
    if (fd_ >= 0 && stream_on_.exchange(false)) {
        enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ::ioctl(fd_, VIDIOC_STREAMOFF, &t);
    }
    for (uint32_t i = 0; i < buffers_.size(); ++i) if (buffers_[i].start) ::munmap(buffers_[i].start, buffers_[i].length);
    buffers_.clear();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    current_fourcc_ = 0;
}

inline errc camera::platform_stream_on_() noexcept {
    if (fd_ < 0) return errc::BadState;
    if (stream_on_.load()) return errc::Ok;
    enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (::ioctl(fd_, VIDIOC_STREAMON, &t) != 0) return errc::IoError;
    stream_on_.store(true);
    return errc::Ok;
}

inline void camera::platform_stream_off_() noexcept {
    if (fd_ < 0 || !stream_on_.exchange(false)) return;
    enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ::ioctl(fd_, VIDIOC_STREAMOFF, &t);
    for (uint32_t i = 0; i < buffers_.size(); ++i) {    // STREAMOFF empties the queue: hand every buffer back
        struct v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        ::ioctl(fd_, VIDIOC_QBUF, &b);
    }
}

inline result<frame> camera::grab(uint32_t skip_frames, uint32_t timeout_ms) noexcept {
    using R = result<frame>;
    if (!open_.load()) return fail_t_<frame>(errc::BadState, "camera is not open");
    std::lock_guard<std::mutex> slk(stream_mtx_);
    if (streaming_.load()) return fail_t_<frame>(errc::BadState, "stream is running: use capture_latest()");
    reap_worker_();
    std::lock_guard<std::mutex> dlk(dev_mtx_);
    if (fd_ < 0) return fail_t_<frame>(errc::BadState, "device is closed");
    if (skip_frames == UINT32_MAX) skip_frames = opt_.warmup_frames;
    if (skip_frames > 64) skip_frames = 64;
    if (timeout_ms == 0) timeout_ms = KNST_CAMERA_GRAB_TIMEOUT_MS;

    errc e = platform_stream_on_();
    if (e != errc::Ok) return fail_t_<frame>(e, "cannot start streaming for grab");
    frame f; uint32_t good = 0;
    const uint64_t deadline = detail::now_us() + (uint64_t)timeout_ms * 1000ull;
    while (!f.valid && detail::now_us() < deadline) {
        struct pollfd pfd; pfd.fd = fd_; pfd.events = POLLIN; pfd.revents = 0;
        int r = ::poll(&pfd, 1, 100);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) continue;
        struct v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP;
        if (::ioctl(fd_, VIDIOC_DQBUF, &b) != 0) { if (errno == EAGAIN || errno == EINTR) continue; break; }
        if (b.index >= buffers_.size()) continue;
        if (!(b.flags & V4L2_BUF_FLAG_ERROR) && b.bytesused <= buffers_[b.index].length) {
            const uint8_t* p = static_cast<const uint8_t*>(buffers_[b.index].start);
            if (detail::validate_frame_bytes(p, b.bytesused, cur_fmt_.width, cur_fmt_.height, cur_stride_, cur_fmt_.pix) &&
                good++ >= skip_frames) {
                f.data.resize(b.bytesused);
                if (f.data.length() == b.bytesused) {
                    std::memcpy(&f.data[0], p, b.bytesused);
                    f.width = cur_fmt_.width; f.height = cur_fmt_.height; f.stride = cur_stride_;
                    f.pix = cur_fmt_.pix; f.timestamp_us = detail::v4l2_ts_us(b); f.sequence = b.sequence;
                    f.valid = true;
                }
            }
        }
        ::ioctl(fd_, VIDIOC_QBUF, &b);
    }
    platform_stream_off_();
    if (!f.valid) return fail_t_<frame>(errc::Timeout, "no valid frame within the timeout");
    return R::success(std::move(f));
}

inline bool camera::recover_stream_() noexcept {
    push_event(event::Disconnected);
    const format prev = [&]() { std::lock_guard<std::mutex> lk(dev_mtx_); return cur_fmt_; }();
    auto give_up = [&]() {
        set_error_(errc::Disconnected, "camera disconnected");
        push_event(event::Error);
        finalize_recording_(false, false);      // keep the file valid
        release_owned_stream_();
        streaming_.store(false);
        return false;
    };
    if (!opt_.auto_reconnect) return give_up();

    { std::lock_guard<std::mutex> lk(dev_mtx_); platform_close_(); }
    open_options ro = opt_;
    ro.use_default_format = false; ro.strict_format = true;
    ro.width = prev.width; ro.height = prev.height; ro.pix = prev.pix;
    ro.fps_num = prev.fps_num ? prev.fps_num : 30; ro.fps_den = prev.fps_den ? prev.fps_den : 1;

    uint32_t attempts = 0;
    while (streaming_.load()) {
        const uint32_t interval = opt_.reconnect_interval_ms ? opt_.reconnect_interval_ms : 1000;
        for (uint32_t waited = 0; waited < interval && streaming_.load(); waited += 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!streaming_.load()) return false;
        ++attempts;
        errc e;
        {
            std::lock_guard<std::mutex> lk(dev_mtx_);
            e = platform_open_(info_.device_id, ro);
            if (e == errc::Ok && (cur_fmt_.width != prev.width || cur_fmt_.height != prev.height || cur_fmt_.pix != prev.pix)) {
                platform_close_(); e = errc::NotSupported;
            }
            if (e == errc::Ok) { e = platform_stream_on_(); if (e != errc::Ok) platform_close_(); }
        }
        if (e == errc::Ok) { reconnects_.fetch_add(1); push_event(event::Reconnected); return true; }
        if (opt_.reconnect_max_attempts && attempts >= opt_.reconnect_max_attempts) break;
    }
    return streaming_.load() ? give_up() : false;
}

inline void camera::worker_loop_() noexcept {
    worker_tid_.store(std::this_thread::get_id());
    push_event(event::StreamStarted);
    uint32_t last_seq = 0; bool have_seq = false; uint32_t bad_streak = 0;
    uint64_t last_ok = detail::now_us();
    const uint64_t stall_us = (uint64_t)opt_.stall_timeout_ms * 1000ull;

    while (streaming_.load(std::memory_order_acquire)) {
        if (fd_ < 0) break;
        struct pollfd pfd; pfd.fd = fd_; pfd.events = POLLIN; pfd.revents = 0;
        int r = ::poll(&pfd, 1, 100);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (!recover_stream_()) break;
            have_seq = false; last_ok = detail::now_us(); continue;
        }
        if (r == 0) {
            if (stall_us && detail::now_us() - last_ok > stall_us) {
                set_error_(errc::Timeout, "camera stopped delivering frames");
                push_event(event::Error);
                if (!recover_stream_()) break;
                have_seq = false; last_ok = detail::now_us();
            }
            continue;
        }
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) && !(pfd.revents & POLLIN)) {
            if (!recover_stream_()) break;
            have_seq = false; last_ok = detail::now_us(); continue;
        }

        struct v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP;
        if (::ioctl(fd_, VIDIOC_DQBUF, &b) != 0) {
            const int e = errno;
            if (e == EAGAIN || e == EINTR) continue;
            if (e == ENODEV || e == EIO || e == ENOTCONN || e == EPIPE || e == ESHUTDOWN) {
                if (!recover_stream_()) break;
                have_seq = false; last_ok = detail::now_us(); continue;
            }
            frames_dropped_.fetch_add(1);
            if (++bad_streak > 50) { if (!recover_stream_()) break; bad_streak = 0; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        bad_streak = 0; last_ok = detail::now_us();
        if (b.index >= buffers_.size()) { frames_dropped_.fetch_add(1); continue; }

        if (have_seq && b.sequence > last_seq + 1) {
            frames_dropped_.fetch_add(b.sequence - last_seq - 1);
            push_event(event::FrameDropped);
        }
        last_seq = b.sequence; have_seq = true;

        const uint8_t* p = static_cast<const uint8_t*>(buffers_[b.index].start);
        const uint32_t size = b.bytesused;
        if (!(b.flags & V4L2_BUF_FLAG_ERROR) && size <= buffers_[b.index].length &&
            detail::validate_frame_bytes(p, size, cur_fmt_.width, cur_fmt_.height, cur_stride_, cur_fmt_.pix)) {
            process_frame_(p, size, cur_fmt_.width, cur_fmt_.height, cur_stride_, cur_fmt_.pix,
                           detail::v4l2_ts_us(b), b.sequence);
        } else {
            frames_corrupt_.fetch_add(1); frames_dropped_.fetch_add(1);
        }
        if (::ioctl(fd_, VIDIOC_QBUF, &b) != 0) {
            if (!recover_stream_()) break;
            have_seq = false; last_ok = detail::now_us();
        }
    }
    { std::lock_guard<std::mutex> lk(dev_mtx_); platform_stream_off_(); }
    push_event(event::StreamStopped);
}

// ---------------- controls ----------------
inline result<void> camera::set_raw_(uint32_t cid, int32_t value) noexcept {
    struct v4l2_control c{}; c.id = cid; c.value = value;
    if (::ioctl(fd_, VIDIOC_S_CTRL, &c) != 0) return fail_void_(errno == EACCES ? errc::PermissionDenied : errc::IoError, "control write failed");
    return ok_void();
}

inline result<knst_vector<control_info>> camera::controls() noexcept {
    using R = result<knst_vector<control_info>>;
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load() || fd_ < 0) return fail_t_<knst_vector<control_info>>(errc::BadState, "camera is not open");
    knst_vector<control_info> out;
    struct v4l2_queryctrl q{}; q.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    while (::ioctl(fd_, VIDIOC_QUERYCTRL, &q) == 0) {
        control_type t = detail::v4l2_cid_to_type(q.id);
        if (t != control_type::Unknown && !(q.flags & V4L2_CTRL_FLAG_DISABLED) && q.type != V4L2_CTRL_TYPE_CTRL_CLASS) {
            control_info c; detail::v4l2_fill_control(fd_, q, t, c);
            out.push_back(std::move(c));
        }
        q.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }
    return R::success(std::move(out));
}

inline result<control_info> camera::get_control(control_type t) noexcept {
    using R = result<control_info>;
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load() || fd_ < 0) return fail_t_<control_info>(errc::BadState, "camera is not open");
    const uint32_t id = detail::type_to_v4l2_cid(t);
    if (!id) return fail_t_<control_info>(errc::NotSupported, "unknown control");
    struct v4l2_queryctrl q{}; q.id = id;
    if (::ioctl(fd_, VIDIOC_QUERYCTRL, &q) != 0 || (q.flags & V4L2_CTRL_FLAG_DISABLED))
        return fail_t_<control_info>(errc::NotFound, "this camera has no such control");
    control_info c; detail::v4l2_fill_control(fd_, q, t, c);
    return R::success(std::move(c));
}

inline result<void> camera::set_control(control_type t, int32_t value) noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load() || fd_ < 0) return fail_void_(errc::BadState, "camera is not open");
    const uint32_t id = detail::type_to_v4l2_cid(t);
    if (!id) return fail_void_(errc::NotSupported, "unknown control");
    struct v4l2_queryctrl q{}; q.id = id;
    if (::ioctl(fd_, VIDIOC_QUERYCTRL, &q) != 0 || (q.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY)))
        return fail_void_(errc::NotSupported, "control missing or read-only");
    int64_t v = value;
    if (v < q.minimum) v = q.minimum;
    if (v > q.maximum) v = q.maximum;
    if (q.step > 1) v = q.minimum + ((v - q.minimum) / q.step) * q.step;
    if (t == control_type::Exposure) {          // absolute values only work in manual mode
        struct v4l2_queryctrl a{}; a.id = V4L2_CID_EXPOSURE_AUTO;
        if (::ioctl(fd_, VIDIOC_QUERYCTRL, &a) == 0) set_raw_(V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL);
    } else if (t == control_type::Focus) {
        struct v4l2_queryctrl a{}; a.id = V4L2_CID_FOCUS_AUTO;
        if (::ioctl(fd_, VIDIOC_QUERYCTRL, &a) == 0) set_raw_(V4L2_CID_FOCUS_AUTO, 0);
    } else if (t == control_type::WhiteBalance) {
        struct v4l2_queryctrl a{}; a.id = V4L2_CID_AUTO_WHITE_BALANCE;
        if (::ioctl(fd_, VIDIOC_QUERYCTRL, &a) == 0) set_raw_(V4L2_CID_AUTO_WHITE_BALANCE, 0);
    }
    return set_raw_(id, (int32_t)v);
}

inline result<void> camera::set_control_auto(control_type t, bool auto_on) noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load() || fd_ < 0) return fail_void_(errc::BadState, "camera is not open");
    if (t == control_type::Exposure) t = control_type::ExposureAuto;
    else if (t == control_type::Focus) t = control_type::FocusAuto;
    else if (t == control_type::WhiteBalance) t = control_type::WhiteBalanceAuto;
    if (!detail::is_auto_type(t)) return fail_void_(errc::NotSupported, "this control has no auto mode");
    const int32_t v = (t == control_type::ExposureAuto)
        ? (auto_on ? (int32_t)V4L2_EXPOSURE_APERTURE_PRIORITY : (int32_t)V4L2_EXPOSURE_MANUAL)
        : (auto_on ? 1 : 0);
    return set_raw_(detail::type_to_v4l2_cid(t), v);
}

inline result<void> camera::reset_control(control_type t) noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load() || fd_ < 0) return fail_void_(errc::BadState, "camera is not open");
    const uint32_t id = detail::type_to_v4l2_cid(t);
    if (!id) return fail_void_(errc::NotSupported, "unknown control");
    struct v4l2_queryctrl q{}; q.id = id;
    if (::ioctl(fd_, VIDIOC_QUERYCTRL, &q) != 0) return fail_void_(errc::NotFound, "this camera has no such control");
    return set_raw_(id, q.default_value);
}

inline result<void> camera::reset_all_controls() noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load() || fd_ < 0) return fail_void_(errc::BadState, "camera is not open");
    for (int pass = 0; pass < 2; ++pass) {      // manual values first, auto switches last
        struct v4l2_queryctrl q{}; q.id = V4L2_CTRL_FLAG_NEXT_CTRL;
        while (::ioctl(fd_, VIDIOC_QUERYCTRL, &q) == 0) {
            control_type t = detail::v4l2_cid_to_type(q.id);
            if (t != control_type::Unknown && !(q.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY)) &&
                detail::is_auto_type(t) == (pass == 1))
                set_raw_(q.id, q.default_value);
            q.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
        }
    }
    return ok_void();
}

inline result<knst_vector<control_value>> camera::save_controls() noexcept {
    using R = result<knst_vector<control_value>>;
    auto cs = controls();
    if (!cs) return R::fail(cs.st, cs.native);
    knst_vector<control_value> out;
    for (uint32_t i = 0; i < cs.value.size(); ++i) {
        control_value v; v.type = cs.value[i].type; v.value = cs.value[i].current;
        out.push_back(v);
    }
    return R::success(std::move(out));
}

inline result<void> camera::restore_controls(const knst_vector<control_value>& v) noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load() || fd_ < 0) return fail_void_(errc::BadState, "camera is not open");
    for (int pass = 0; pass < 2; ++pass)        // auto switches first so manual values can stick
        for (uint32_t i = 0; i < v.size(); ++i) {
            if (detail::is_auto_type(v[i].type) != (pass == 0)) continue;
            const uint32_t id = detail::type_to_v4l2_cid(v[i].type);
            if (id) set_raw_(id, v[i].value);
        }
    return ok_void();
}

#elif KNST_CAM_ANDROID

// *-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*
//  Android Camera2 NDK implementation
// *-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-**-*-**-*

namespace detail_android {

inline pixel_format fmt_from_android(int32_t f) noexcept {
    switch (f) {
        case AIMAGE_FORMAT_JPEG:        return pixel_format::MJPEG;
        case AIMAGE_FORMAT_YUV_420_888: return pixel_format::I420;
        case AIMAGE_FORMAT_RGBA_8888:   return pixel_format::RGBA32;
        case AIMAGE_FORMAT_RGBX_8888:   return pixel_format::RGBA32;
        default:                        return pixel_format::Unknown;
    }
}
inline int32_t fmt_to_android(pixel_format p) noexcept {
    switch (p) {
        case pixel_format::MJPEG:  return AIMAGE_FORMAT_JPEG;
        case pixel_format::I420:
        case pixel_format::NV12:
        case pixel_format::NV21:
        case pixel_format::YUYV:   return AIMAGE_FORMAT_YUV_420_888;
        case pixel_format::RGBA32: return AIMAGE_FORMAT_RGBA_8888;
        default:                   return 0;
    }
}

inline camera_info read_camera_info(ACameraManager* mgr, const char* id) noexcept {
    camera_info ci;
    ci.device_id = knst_c16string(id);
    ACameraMetadata* m = nullptr;
    if (ACameraManager_getCameraCharacteristics(mgr, id, &m) != ACAMERA_OK) return ci;

    // Lens facing → friendly name
    ACameraMetadata_const_entry e{};
    if (ACameraMetadata_getConstEntry(m, ACAMERA_LENS_FACING, &e) == ACAMERA_OK && e.count > 0) {
        switch (e.data.u8[0]) {
            case ACAMERA_LENS_FACING_FRONT: ci.name = u"Front Camera"; break;
            case ACAMERA_LENS_FACING_BACK:  ci.name = u"Back Camera";  break;
            default:                        ci.name = u"External Camera"; break;
        }
    } else ci.name = ci.device_id;

    // Max resolution
    if (ACameraMetadata_getConstEntry(m, ACAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE, &e) == ACAMERA_OK && e.count >= 4) {
        ci.max_width  = (uint32_t)(e.data.i32[2] - e.data.i32[0] + 1);
        ci.max_height = (uint32_t)(e.data.i32[3] - e.data.i32[1] + 1);
    }

    // Highest available FPS from AE target FPS ranges
    uint32_t best_fps_num = 30, best_fps_den = 1;
    if (ACameraMetadata_getConstEntry(m, ACAMERA_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, &e) == ACAMERA_OK
        && e.count >= 2 && e.type == ACAMERA_TYPE_INT32) {
        uint32_t best = 0;
        for (uint32_t i = 0; i + 1 < e.count; i += 2) {
            int32_t hi = e.data.i32[i + 1];
            if (hi > 0 && (uint32_t)hi > best) best = (uint32_t)hi;
        }
        if (best > 0) best_fps_num = best;
    }

    // ALL stream configurations: the (format, width, height, input) quads.
    // input == 0 means output (capture); input == 1 is a reprocessing input
    // (skip). This is where Android's real format list comes from.
    if (ACameraMetadata_getConstEntry(m, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &e) == ACAMERA_OK
        && e.type == ACAMERA_TYPE_INT32) {
        for (uint32_t i = 0; i + 3 < e.count; i += 4) {
            int32_t fmt = e.data.i32[i + 0];
            int32_t w   = e.data.i32[i + 1];
            int32_t h   = e.data.i32[i + 2];
            int32_t inp = e.data.i32[i + 3];
            if (inp != 0) continue;
            if (w <= 0 || h <= 0 ||
                w > (int32_t)KNST_CAMERA_MAX_DIMENSION || h > (int32_t)KNST_CAMERA_MAX_DIMENSION) continue;

            pixel_format pf = pixel_format::Unknown;
            if      (fmt == AIMAGE_FORMAT_JPEG)       pf = pixel_format::MJPEG;
            else if (fmt == AIMAGE_FORMAT_YUV_420_888) pf = pixel_format::I420;
            else if (fmt == AIMAGE_FORMAT_RGBA_8888)  pf = pixel_format::RGBA32;
            else if (fmt == AIMAGE_FORMAT_RGBX_8888)  pf = pixel_format::RGBA32;
            else continue;

            format f;
            f.width = (uint32_t)w;
            f.height = (uint32_t)h;
            f.pix = pf;
            f.is_compressed = (pf == pixel_format::MJPEG);
            f.fps_num = best_fps_num;
            f.fps_den = best_fps_den;
            ci.formats.push_back(f);
        }
    }

    // Sort formats largest-area-first (same ordering as Linux/Windows)
    for (uint32_t i = 1; i < ci.formats.size(); ++i) {
        for (uint32_t j = i; j > 0; --j) {
            uint64_t aa = (uint64_t)ci.formats[j - 1].width * ci.formats[j - 1].height;
            uint64_t ab = (uint64_t)ci.formats[j].width     * ci.formats[j].height;
            if (aa >= ab) break;
            format t = ci.formats[j]; ci.formats[j] = ci.formats[j - 1]; ci.formats[j - 1] = t;
        }
    }

    // Fallback: if metadata was sparse, ensure at least one format exists
    if (ci.formats.empty() && ci.max_width && ci.max_height) {
        format f; f.width = ci.max_width; f.height = ci.max_height;
        f.fps_num = best_fps_num; f.fps_den = best_fps_den;
        f.pix = pixel_format::MJPEG; f.is_compressed = true;
        ci.formats.push_back(f);
    }

    ci.has_video = !ci.formats.empty();
    ci.has_audio = false;
    ACameraMetadata_free(m);
    return ci;
}

struct image_cb_ctx { camera* self; };

inline void on_image_available(void* ctx, AImageReader* reader) noexcept;

} // namespace detail_android

inline const char* camera::backend_name() noexcept { return "Android Camera2 NDK"; }

inline result<knst_vector<camera_info>> camera::enumerate() noexcept {
    using R = result<knst_vector<camera_info>>;
    ACameraManager* mgr = ACameraManager_create();
    if (!mgr) return err_result<knst_vector<camera_info>>(errc::IoError);
    ACameraIdList* ids = nullptr;
    if (ACameraManager_getCameraIdList(mgr, &ids) != ACAMERA_OK || !ids) {
        ACameraManager_delete(mgr);
        return err_result<knst_vector<camera_info>>(errc::NotFound);
    }
    knst_vector<camera_info> out;
    for (int i = 0; i < ids->numCameras; ++i) {
        camera_info ci = detail_android::read_camera_info(mgr, ids->cameraIds[i]);
        ci.index = (uint32_t)i;
        ci.is_default = (i == 0);
        out.push_back(std::move(ci));
    }
    ACameraManager_deleteCameraIdList(ids);
    ACameraManager_delete(mgr);
    if (out.empty()) return err_result<knst_vector<camera_info>>(errc::NotFound);
    return R::success(std::move(out));
}

inline errc camera::platform_open_(const knst_c16string& path, const open_options& opt) noexcept {
    auto fail = [this](errc e) { platform_close_(); return e; };

    a_mgr_ = ACameraManager_create();
    if (!a_mgr_) return errc::IoError;

    // Device id: "0", "1", ... or camera name. If empty, use the first camera.
    knst_byte_string u8 = knst_dev::to_utf8(path);
    if (u8.empty()) {
        ACameraIdList* ids = nullptr;
        if (ACameraManager_getCameraIdList(a_mgr_, &ids) != ACAMERA_OK || !ids || ids->numCameras == 0) {
            if (ids) ACameraManager_deleteCameraIdList(ids);
            return fail(errc::NotFound);
        }
        u8 = knst_byte_string(ids->cameraIds[0]);
        ACameraManager_deleteCameraIdList(ids);
    }
      // Save the ACTUAL opened ID (never the empty caller input) before adding
    // the NUL terminator. Otherwise controls()/set_control() would call
    // open_meta("") and silently fail whenever path was empty ("pick first
    // camera"). u8 now holds the real driver id, e.g. "0", "2", "42".
    a_cam_id_.clear();
    {
            const char* p = reinterpret_cast<const char*>(u8.data());
        const uint32_t n = (uint32_t)u8.length();
        for (uint32_t i = 0; i < n; ++i) {
            if (p[i] == 0) break;
            a_cam_id_.append(static_cast<char16_t>(static_cast<uint8_t>(p[i])));
        }
    }
    u8.push_back(0);

    ACameraDevice_StateCallbacks cb{};
    cb.context = this;
    cb.onDisconnected = [](void* ctx, ACameraDevice*) {
        camera* c = (camera*)ctx;
        c->push_event(event::Disconnected);
        c->a_disconnected_.store(true);
        // Do not touch streaming_ here: worker_loop_ needs to see both
        // flags so it can decide between clean stop vs auto-reconnect.
    };
    cb.onError = [](void* ctx, ACameraDevice*, int code) {
        camera* c = (camera*)ctx;
        c->set_error_(errc::IoError, "Camera2 device error");
        c->push_event(event::Error);
        (void)code;
    };

    if (ACameraManager_openCamera(a_mgr_, reinterpret_cast<const char*>(u8.data()), &cb, &a_dev_) != ACAMERA_OK || !a_dev_)
        return fail(errc::IoError);

    // Pick the ImageReader pixel format
    a_format_ = detail_android::fmt_to_android(opt.pix == pixel_format::Unknown ? pixel_format::MJPEG : opt.pix);
    if (!a_format_) a_format_ = AIMAGE_FORMAT_JPEG;

    cur_fmt_ = format();
    cur_fmt_.width  = opt.width  ? opt.width  : 1280;
    cur_fmt_.height = opt.height ? opt.height : 720;
    cur_fmt_.fps_num = opt.fps_num ? opt.fps_num : 30;
    cur_fmt_.fps_den = opt.fps_den ? opt.fps_den : 1;
    cur_fmt_.pix = detail_android::fmt_from_android(a_format_);
    cur_fmt_.is_compressed = (a_format_ == AIMAGE_FORMAT_JPEG);
    return errc::Ok;
}

inline void camera::platform_close_() noexcept {
    if (a_session_) { ACameraCaptureSession_close(a_session_); a_session_ = nullptr; }
    if (a_request_) { ACaptureRequest_free(a_request_);     a_request_ = nullptr; }
    if (a_target_)  { ACameraOutputTarget_free(a_target_);  a_target_  = nullptr; }
    if (a_output_)  { ACaptureSessionOutput_free(a_output_); a_output_ = nullptr; }
    if (a_outputs_) { ACaptureSessionOutputContainer_free(a_outputs_); a_outputs_ = nullptr; }
    if (a_reader_)  { AImageReader_delete(a_reader_);       a_reader_  = nullptr; }
    if (a_window_)  { ANativeWindow_release(a_window_);     a_window_  = nullptr; }
    if (a_dev_)     { ACameraDevice_close(a_dev_);          a_dev_     = nullptr; }
    if (a_mgr_)     { ACameraManager_delete(a_mgr_);        a_mgr_     = nullptr; }
}

inline errc camera::platform_stream_on_() noexcept {
    if (!a_dev_) return errc::BadState;

    // AImageReader_newWithUsage is API 26+. On API 24/25 fall back to
    // AImageReader_new (default usage; JPEG works fine, YUV may need
    // USAGE_CPU_READ_OFTEN on some devices — bump minSdk to 26 if so).
#if defined(__ANDROID_API__) && __ANDROID_API__ >= 26
    if (AImageReader_newWithUsage(cur_fmt_.width, cur_fmt_.height, a_format_,
                                  AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, 4, &a_reader_) != AMEDIA_OK)
        return errc::IoError;
#else
    if (AImageReader_new(cur_fmt_.width, cur_fmt_.height, a_format_, 4, &a_reader_) != AMEDIA_OK)
        return errc::IoError;
#endif
    AImageReader_ImageListener l{};
    l.context = this;
    l.onImageAvailable = [](void* ctx, AImageReader* r) noexcept {
        camera* self = (camera*)ctx;
        AImage* img = nullptr;
        if (AImageReader_acquireLatestImage(r, &img) != AMEDIA_OK || !img) return;
        int32_t w = 0, h = 0, fmt = 0;
        int64_t ts_ns = 0;
        AImage_getWidth(img, &w); AImage_getHeight(img, &h);
        AImage_getFormat(img, &fmt);
        AImage_getTimestamp(img, &ts_ns);
        knst_byte_string data;
        uint32_t stride = 0;
        if (fmt == AIMAGE_FORMAT_JPEG) {
            uint8_t* p = nullptr; int32_t len = 0;
            if (AImage_getPlaneData(img, 0, &p, &len) == AMEDIA_OK && p && len > 0) {
                data.resize((uint32_t)len);
                if (data.length() == (uint32_t)len) std::memcpy(&data[0], p, (size_t)len);
            }
        } else if (fmt == AIMAGE_FORMAT_YUV_420_888) {
            uint8_t *yp = nullptr, *up = nullptr, *vp = nullptr;
            int32_t yl = 0, ul = 0, vl = 0, ys = 0, us = 0, vs = 0;
            AImage_getPlaneData(img, 0, &yp, &yl); AImage_getPlaneRowStride(img, 0, &ys);
            AImage_getPlaneData(img, 1, &up, &ul); AImage_getPlaneRowStride(img, 1, &us);
            AImage_getPlaneData(img, 2, &vp, &vl); AImage_getPlaneRowStride(img, 2, &vs);
            if (yp && up && vp) {
                const uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
                const uint64_t total = (uint64_t)w * h + (uint64_t)cw * ch * 2;
                if (total < 32u * 1024u * 1024u) {
                    data.resize((uint32_t)total);
                    if (data.length() == (uint32_t)total) {
                        uint8_t* d = &data[0];
                        for (int y = 0; y < h; ++y) std::memcpy(d + (size_t)y * w, yp + (size_t)y * ys, (size_t)w);
                        for (int y = 0; y < ch; ++y) {
                            std::memcpy(d + (size_t)w * h + (size_t)y * cw, up + (size_t)y * us, (size_t)cw);
                            std::memcpy(d + (size_t)w * h + (size_t)cw * ch + (size_t)y * cw, vp + (size_t)y * vs, (size_t)cw);
                        }
                        stride = w;
                    }
                }
            }
        }
        AImage_delete(img);
        if (data.empty()) return;
        std::lock_guard<std::mutex> lk(self->a_pending_mtx_);
        self->a_pending_data_ = std::move(data);
        self->a_pending_w_    = (uint32_t)w;
        self->a_pending_h_    = (uint32_t)h;
        self->a_pending_ts_   = (uint64_t)(ts_ns / 1000);
        self->a_pending_seq_++;
        self->a_pending_ok_   = true;
        self->a_stride_       = stride;
        self->a_pending_cv_.notify_all();
    };
    AImageReader_setImageListener(a_reader_, &l);

    if (AImageReader_getWindow(a_reader_, &a_window_) != AMEDIA_OK) return errc::IoError;
    ANativeWindow_acquire(a_window_);

    ACaptureSessionOutputContainer_create(&a_outputs_);
    ACaptureSessionOutput_create(a_window_, &a_output_);
    ACaptureSessionOutputContainer_add(a_outputs_, a_output_);
    ACameraOutputTarget_create(a_window_, &a_target_);
    ACameraDevice_createCaptureRequest(a_dev_, TEMPLATE_PREVIEW, &a_request_);
    ACaptureRequest_addTarget(a_request_, a_target_);

    ACameraCaptureSession_stateCallbacks scb{};
    scb.context = this;
    scb.onClosed    = [](void*, ACameraCaptureSession*) {};
    scb.onReady     = [](void*, ACameraCaptureSession*) {};
    scb.onActive    = [](void*, ACameraCaptureSession*) {};

    if (ACameraDevice_createCaptureSession(a_dev_, a_outputs_, &scb, &a_session_) != ACAMERA_OK)
        return errc::IoError;

    // Apply pending controls BEFORE starting the repeating request, so
    // user-set exposure/focus/WB values take effect on the first frame.
    for (auto& pc : a_pending_ctrls_) {
        if (pc.kind == a_pending_ctrl::I32) {
            ACaptureRequest_setEntry_i32(a_request_, pc.tag, 1, &pc.value);
        } else {
            uint8_t v = (uint8_t)pc.value;
            ACaptureRequest_setEntry_u8(a_request_, pc.tag, 1, &v);
        }
    }

    if (ACameraCaptureSession_setRepeatingRequest(a_session_, nullptr, 1, &a_request_, nullptr) != ACAMERA_OK)
        return errc::IoError;

    return errc::Ok;
}

inline void camera::platform_stream_off_() noexcept {
    if (a_session_) ACameraCaptureSession_stopRepeating(a_session_);
    if (a_session_) { ACameraCaptureSession_close(a_session_); a_session_ = nullptr; }
    if (a_request_) { ACaptureRequest_free(a_request_);      a_request_ = nullptr; }
    if (a_target_)  { ACameraOutputTarget_free(a_target_);   a_target_  = nullptr; }
    if (a_output_)  { ACaptureSessionOutput_free(a_output_); a_output_  = nullptr; }
    if (a_outputs_) { ACaptureSessionOutputContainer_free(a_outputs_); a_outputs_ = nullptr; }
    if (a_reader_)  { AImageReader_delete(a_reader_);        a_reader_  = nullptr; }
    if (a_window_)  { ANativeWindow_release(a_window_);      a_window_  = nullptr; }
}

inline void camera::worker_loop_() noexcept {
    worker_tid_.store(std::this_thread::get_id());
    push_event(event::StreamStarted);
    while (streaming_.load(std::memory_order_acquire)) {
        // ---- Handle disconnect + optional auto-reconnect ----
        if (a_disconnected_.load(std::memory_order_acquire)) {
            { std::lock_guard<std::mutex> lk(dev_mtx_); platform_stream_off_(); platform_close_(); }

            if (!opt_.auto_reconnect) {
                set_error_(errc::Disconnected, "camera disconnected");
                push_event(event::Error);
                finalize_recording_(false, false);   // keep the file valid
                release_owned_stream_();
                streaming_.store(false);
                break;
            }

            uint32_t attempts = 0;
            bool reopened = false;
            const uint32_t interval = opt_.reconnect_interval_ms ? opt_.reconnect_interval_ms : 1000;
            while (streaming_.load()) {
                for (uint32_t w = 0; w < interval && streaming_.load(); w += 50) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    if (!a_disconnected_.load()) break;    // recovered externally
                }
                if (!streaming_.load()) break;
                if (!a_disconnected_.load()) { reopened = true; break; }
                ++attempts;
                errc e;
                {
                    std::lock_guard<std::mutex> lk(dev_mtx_);
                    e = platform_open_(info_.device_id, opt_);
                    if (e == errc::Ok) e = platform_stream_on_();
                    if (e != errc::Ok) platform_close_();
                }
                if (e == errc::Ok) {
                    a_disconnected_.store(false);
                    reconnects_.fetch_add(1);
                    push_event(event::Reconnected);
                    reopened = true;
                    break;
                }
                if (opt_.reconnect_max_attempts && attempts >= opt_.reconnect_max_attempts) break;
            }

            if (!reopened) {
                set_error_(errc::Disconnected, "camera reconnect failed");
                push_event(event::Error);
                finalize_recording_(false, false);
                release_owned_stream_();
                streaming_.store(false);
                break;
            }
            continue;   // back to normal frame loop
        }

        knst_byte_string data;
        uint32_t w = 0, h = 0, seq = 0;
        uint64_t ts = 0;
        uint32_t stride = 0;
        {
            std::unique_lock<std::mutex> lk(a_pending_mtx_);
            a_pending_cv_.wait_for(lk, std::chrono::milliseconds(100),
                [this]{ return a_pending_ok_ || !streaming_.load(); });
            if (!streaming_.load()) break;
            if (!a_pending_ok_) continue;
            data   = std::move(a_pending_data_);
            w      = a_pending_w_;
            h      = a_pending_h_;
            ts     = a_pending_ts_;
            seq    = a_pending_seq_;
            stride = a_stride_;
            a_pending_ok_ = false;
        }
        if (!data.empty()) process_frame_(data.data(), data.length(), w, h, stride, cur_fmt_.pix, ts, seq);
    }
    { std::lock_guard<std::mutex> lk(dev_mtx_); platform_stream_off_(); }
    push_event(event::StreamStopped);
}

inline result<frame> camera::grab(uint32_t skip_frames, uint32_t timeout_ms) noexcept {
    using R = result<frame>;
    if (!open_.load()) return fail_t_<frame>(errc::BadState, "camera is not open");
    std::lock_guard<std::mutex> slk(stream_mtx_);
    if (streaming_.load()) return fail_t_<frame>(errc::BadState, "stream is running: use capture_latest()");
    if (skip_frames == UINT32_MAX) skip_frames = opt_.warmup_frames;
    if (skip_frames > 64) skip_frames = 64;
    if (timeout_ms == 0) timeout_ms = KNST_CAMERA_GRAB_TIMEOUT_MS;

    std::lock_guard<std::mutex> dlk(dev_mtx_);
    errc e = platform_stream_on_();
    if (e != errc::Ok) return fail_t_<frame>(e, "cannot start streaming for grab");
    {
        std::lock_guard<std::mutex> pl(a_pending_mtx_);
        a_pending_ok_ = false;
    }

    frame f;
    uint64_t deadline = detail::now_us() + (uint64_t)timeout_ms * 1000ull;
    uint32_t good = 0;
    while (detail::now_us() < deadline) {
        std::unique_lock<std::mutex> lk(a_pending_mtx_);
        if (!a_pending_cv_.wait_for(lk, std::chrono::milliseconds(50), [this]{ return a_pending_ok_; })) continue;
        a_pending_ok_ = false;
        if (good++ < skip_frames) continue;
        f.data = std::move(a_pending_data_);
        f.width  = a_pending_w_;  f.height = a_pending_h_;
        f.stride = a_stride_;     f.pix    = cur_fmt_.pix;
        f.timestamp_us = a_pending_ts_; f.sequence = a_pending_seq_;
        f.valid = !f.data.empty();
        break;
    }
    platform_stream_off_();
    if (!f.valid) return fail_t_<frame>(errc::Timeout, "no valid frame within the timeout");
    return R::success(std::move(f));
}

// Android Camera2 exposes a small set of "hard" controls that map cleanly to
// the cross-platform control_type enum. All of them are per-capture-request,
// so we cache the values in a_pending_ctrls_ and apply them to a_request_ on
// each stream_on (or immediately if already streaming).
namespace detail_android_ctrl {

inline ACameraMetadata* open_meta(ACameraManager* mgr, const knst_c16string& cam_id) noexcept {
    knst_byte_string u8 = knst_dev::to_utf8(cam_id);
    if (u8.empty()) return nullptr;
    u8.push_back(0);
    ACameraMetadata* m = nullptr;
    if (ACameraManager_getCameraCharacteristics(mgr, (const char*)u8.data(), &m) != ACAMERA_OK)
        return nullptr;
    return m;
}

inline bool is_auto_type(control_type t) noexcept {
    return t == control_type::ExposureAuto ||
           t == control_type::FocusAuto ||
           t == control_type::WhiteBalanceAuto;
}

} // namespace detail_android_ctrl

inline result<knst_vector<control_info>> camera::controls() noexcept {
    using R = result<knst_vector<control_info>>;
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load() || !a_mgr_)
        return fail_t_<knst_vector<control_info>>(errc::BadState, "camera is not open");

    ACameraMetadata* m = detail_android_ctrl::open_meta(a_mgr_, a_cam_id_);
    if (!m) return fail_t_<knst_vector<control_info>>(errc::IoError, "cannot read camera metadata");

    knst_vector<control_info> out;
    ACameraMetadata_const_entry e{};

    // ---- Exposure compensation (EV × step) ----
    int32_t ev_step = 1;
    if (ACameraMetadata_getConstEntry(m, ACAMERA_CONTROL_AE_COMPENSATION_STEP, &e) == ACAMERA_OK
        && e.count >= 1 && e.type == ACAMERA_TYPE_RATIONAL) {
        if (e.data.r[0].denominator > 0)
            ev_step = e.data.r[0].numerator / e.data.r[0].denominator;
        if (ev_step < 1) ev_step = 1;
    }
    if (ACameraMetadata_getConstEntry(m, ACAMERA_CONTROL_AE_COMPENSATION_RANGE, &e) == ACAMERA_OK
        && e.count >= 2 && e.type == ACAMERA_TYPE_INT32) {
        int32_t lo = e.data.i32[0], hi = e.data.i32[1];
        if (lo < hi) {   // some devices report 0,0 when unsupported
            control_info c;
            c.type = control_type::Exposure;
            c.min = lo; c.max = hi; c.step = ev_step;
            c.default_value = 0; c.current = 0;
            c.is_writable = true; c.is_active = true; c.is_auto = false;
            c.name = u"Exposure Compensation";
            // Override with pending if any
            for (auto& pc : a_pending_ctrls_) {
                if (pc.tag == ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION) { c.current = pc.value; break; }
            }
            out.push_back(std::move(c));
        }
    }

    // ---- Auto Exposure ----
    if (ACameraMetadata_getConstEntry(m, ACAMERA_CONTROL_AE_AVAILABLE_MODES, &e) == ACAMERA_OK
        && e.count > 0 && e.type == ACAMERA_TYPE_BYTE) {
        bool has_auto = false, has_off = false;
        for (uint32_t i = 0; i < e.count; ++i) {
            uint8_t mode = e.data.u8[i];
            if (mode == ACAMERA_CONTROL_AE_MODE_OFF) has_off = true;
            else has_auto = true;
        }
        if (has_auto && has_off) {
            control_info c;
            c.type = control_type::ExposureAuto;
            c.min = 0; c.max = 1; c.step = 1; c.default_value = 1;
            c.is_writable = true; c.is_active = true; c.is_auto = true;
            c.name = u"Auto Exposure";
            // Pending override → current, else default to "auto on"
            c.current = 1;
            for (auto& pc : a_pending_ctrls_) {
                if (pc.tag == ACAMERA_CONTROL_AE_MODE) {
                    c.current = (pc.value == ACAMERA_CONTROL_AE_MODE_OFF) ? 0 : 1;
                    break;
                }
            }
            c.is_auto_on = (c.current != 0);
            out.push_back(std::move(c));
        }
    }

    // ---- Auto Focus ----
    if (ACameraMetadata_getConstEntry(m, ACAMERA_CONTROL_AF_AVAILABLE_MODES, &e) == ACAMERA_OK
        && e.count > 0 && e.type == ACAMERA_TYPE_BYTE) {
        bool has_auto = false, has_off = false;
        for (uint32_t i = 0; i < e.count; ++i) {
            uint8_t mode = e.data.u8[i];
            if (mode == ACAMERA_CONTROL_AF_MODE_OFF) has_off = true;
            else has_auto = true;
        }
        if (has_auto && has_off) {
            control_info c;
            c.type = control_type::FocusAuto;
            c.min = 0; c.max = 1; c.step = 1; c.default_value = 1;
            c.is_writable = true; c.is_active = true; c.is_auto = true;
            c.name = u"Auto Focus";
            c.current = 1;
            for (auto& pc : a_pending_ctrls_) {
                if (pc.tag == ACAMERA_CONTROL_AF_MODE) {
                    c.current = (pc.value == ACAMERA_CONTROL_AF_MODE_OFF) ? 0 : 1;
                    break;
                }
            }
            c.is_auto_on = (c.current != 0);
            out.push_back(std::move(c));
        }
    }

    // ---- Auto White Balance ----
    if (ACameraMetadata_getConstEntry(m, ACAMERA_CONTROL_AWB_AVAILABLE_MODES, &e) == ACAMERA_OK
        && e.count > 0 && e.type == ACAMERA_TYPE_BYTE) {
        bool has_auto = false, has_off = false;
        for (uint32_t i = 0; i < e.count; ++i) {
            uint8_t mode = e.data.u8[i];
            if (mode == ACAMERA_CONTROL_AWB_MODE_OFF) has_off = true;
            else has_auto = true;
        }
        if (has_auto && has_off) {
            control_info c;
            c.type = control_type::WhiteBalanceAuto;
            c.min = 0; c.max = 1; c.step = 1; c.default_value = 1;
            c.is_writable = true; c.is_active = true; c.is_auto = true;
            c.name = u"Auto White Balance";
            c.current = 1;
            for (auto& pc : a_pending_ctrls_) {
                if (pc.tag == ACAMERA_CONTROL_AWB_MODE) {
                    c.current = (pc.value == ACAMERA_CONTROL_AWB_MODE_OFF) ? 0 : 1;
                    break;
                }
            }
            c.is_auto_on = (c.current != 0);
            out.push_back(std::move(c));
        }
    }

    // ---- Gain (ISO sensitivity range) ----
    if (ACameraMetadata_getConstEntry(m, ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE, &e) == ACAMERA_OK
        && e.count >= 2 && e.type == ACAMERA_TYPE_INT32) {
        int32_t lo = e.data.i32[0], hi = e.data.i32[1];
        if (lo > 0 && hi >= lo) {
            control_info c;
            c.type = control_type::Gain;
            c.min = lo; c.max = hi; c.step = 1; c.default_value = lo; c.current = lo;
            c.is_writable = true; c.is_active = true; c.is_auto = false;
            c.name = u"ISO Sensitivity";
            for (auto& pc : a_pending_ctrls_) {
                if (pc.tag == ACAMERA_SENSOR_SENSITIVITY) { c.current = pc.value; break; }
            }
            out.push_back(std::move(c));
        }
    }

    ACameraMetadata_free(m);
    return R::success(std::move(out));
}

inline result<control_info> camera::get_control(control_type t) noexcept {
    using R = result<control_info>;
    auto all = controls();
    if (!all) return fail_t_<control_info>(errc::BadState, "camera is not open");
    for (auto& c : all.value) if (c.type == t) return R::success(c);
    return fail_t_<control_info>(errc::NotFound, "this camera has no such control");
}

inline result<void> camera::set_control(control_type t, int32_t value) noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load() || !a_mgr_)
        return fail_void_(errc::BadState, "camera is not open");

    ACameraMetadata* m = detail_android_ctrl::open_meta(a_mgr_, a_cam_id_);
    if (!m) return fail_void_(errc::IoError, "cannot read camera metadata");

    errc e = errc::Ok;
    ACameraMetadata_const_entry de{};

    switch (t) {
        case control_type::Exposure: {
            // Clamp to advertised range
            if (ACameraMetadata_getConstEntry(m, ACAMERA_CONTROL_AE_COMPENSATION_RANGE, &de) == ACAMERA_OK
                && de.count >= 2 && de.type == ACAMERA_TYPE_INT32) {
                if (value < de.data.i32[0]) value = de.data.i32[0];
                if (value > de.data.i32[1]) value = de.data.i32[1];
            }
            a_pending_ctrl pc;
            pc.kind = a_pending_ctrl::I32;
            pc.tag = ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION;
            pc.value = value;
            bool replaced = false;
            for (auto& p : a_pending_ctrls_)
                if (p.tag == pc.tag) { p.value = value; replaced = true; break; }
            if (!replaced) a_pending_ctrls_.push_back(pc);
            if (a_request_ && a_session_) {
                ACaptureRequest_setEntry_i32(a_request_, pc.tag, 1, &pc.value);
                if (ACameraCaptureSession_setRepeatingRequest(a_session_, nullptr, 1, &a_request_, nullptr) != ACAMERA_OK)
                    e = errc::IoError;
            }
            break;
        }
        case control_type::ExposureAuto:
        case control_type::FocusAuto:
        case control_type::WhiteBalanceAuto: {
            a_pending_ctrl pc;
            pc.kind = a_pending_ctrl::U8;
            if (t == control_type::ExposureAuto) {
                pc.tag = ACAMERA_CONTROL_AE_MODE;
                pc.value = value ? ACAMERA_CONTROL_AE_MODE_ON : ACAMERA_CONTROL_AE_MODE_OFF;
            } else if (t == control_type::FocusAuto) {
                pc.tag = ACAMERA_CONTROL_AF_MODE;
                pc.value = value ? ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE : ACAMERA_CONTROL_AF_MODE_OFF;
            } else {
                pc.tag = ACAMERA_CONTROL_AWB_MODE;
                pc.value = value ? ACAMERA_CONTROL_AWB_MODE_AUTO : ACAMERA_CONTROL_AWB_MODE_OFF;
            }
            bool replaced = false;
            for (auto& p : a_pending_ctrls_)
                if (p.tag == pc.tag) { p.value = pc.value; replaced = true; break; }
            if (!replaced) a_pending_ctrls_.push_back(pc);
            if (a_request_ && a_session_) {
                uint8_t v = (uint8_t)pc.value;
                ACaptureRequest_setEntry_u8(a_request_, pc.tag, 1, &v);
                if (ACameraCaptureSession_setRepeatingRequest(a_session_, nullptr, 1, &a_request_, nullptr) != ACAMERA_OK)
                    e = errc::IoError;
            }
            break;
        }
        case control_type::Gain: {
            if (ACameraMetadata_getConstEntry(m, ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE, &de) == ACAMERA_OK
                && de.count >= 2 && de.type == ACAMERA_TYPE_INT32) {
                if (value < de.data.i32[0]) value = de.data.i32[0];
                if (value > de.data.i32[1]) value = de.data.i32[1];
            }
            a_pending_ctrl pc;
            pc.kind = a_pending_ctrl::I32;
            pc.tag = ACAMERA_SENSOR_SENSITIVITY;
            pc.value = value;
            bool replaced = false;
            for (auto& p : a_pending_ctrls_)
                if (p.tag == pc.tag) { p.value = value; replaced = true; break; }
            if (!replaced) a_pending_ctrls_.push_back(pc);
            if (a_request_ && a_session_) {
                ACaptureRequest_setEntry_i32(a_request_, pc.tag, 1, &pc.value);
                if (ACameraCaptureSession_setRepeatingRequest(a_session_, nullptr, 1, &a_request_, nullptr) != ACAMERA_OK)
                    e = errc::IoError;
            }
            break;
        }
        default:
            e = errc::NotSupported;
            break;
    }

    ACameraMetadata_free(m);
    if (e != errc::Ok) return fail_void_(e, e == errc::NotSupported ? "control not supported on Android" : "setRepeatingRequest failed");
    return ok_void();
}

inline result<void> camera::set_control_auto(control_type t, bool auto_on) noexcept {
    if (t == control_type::Exposure)     t = control_type::ExposureAuto;
    else if (t == control_type::Focus)   t = control_type::FocusAuto;
    else if (t == control_type::WhiteBalance) t = control_type::WhiteBalanceAuto;
    if (!detail_android_ctrl::is_auto_type(t))
        return fail_void_(errc::NotSupported, "not an auto control on Android");
    return set_control(t, auto_on ? 1 : 0);
}

inline result<void> camera::reset_control(control_type t) noexcept {
    auto all = controls();
    if (!all) return fail_void_(errc::BadState, "camera is not open");
    for (auto& c : all.value) {
        if (c.type == t) return set_control(t, c.default_value);
    }
    return fail_void_(errc::NotFound, "this camera has no such control");
}

inline result<void> camera::reset_all_controls() noexcept {
    auto all = controls();
    if (!all) return fail_void_(errc::BadState, "camera is not open");
    errc worst = errc::Ok;
    for (auto& c : all.value) {
        auto r = set_control(c.type, c.default_value);
        if (!r && worst == errc::Ok) worst = errc::IoError;
    }
    return worst == errc::Ok ? ok_void() : fail_void_(worst, "some controls refused to reset");
}

inline result<knst_vector<control_value>> camera::save_controls() noexcept {
    using R = result<knst_vector<control_value>>;
    auto cs = controls();
    if (!cs) return R::fail(cs.st, cs.native);
    knst_vector<control_value> out;
    for (auto& c : cs.value) {
        control_value v; v.type = c.type; v.value = c.current;
        out.push_back(v);
    }
    return R::success(std::move(out));
}

inline result<void> camera::restore_controls(const knst_vector<control_value>& v) noexcept {
    errc worst = errc::Ok;
    for (auto& cv : v) {
        auto r = set_control(cv.type, cv.value);
        if (!r && worst == errc::Ok) worst = errc::IoError;
    }
    return worst == errc::Ok ? ok_void() : fail_void_(worst, "some controls refused to restore");
}

#elif KNST_CAM_WINDOWS

// ═════════════════════════════════════════════════════════════════════════════
//  Windows Media Foundation implementation
// ═════════════════════════════════════════════════════════════════════════════

namespace detail {

struct mf_com_scope {
    bool needs_uninit = false;
    mf_com_scope() noexcept {
        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        needs_uninit = SUCCEEDED(hr);
    }
    ~mf_com_scope() { if (needs_uninit) ::CoUninitialize(); }
    mf_com_scope(const mf_com_scope&) = delete;
    mf_com_scope& operator=(const mf_com_scope&) = delete;
};

inline bool mf_ensure_started() noexcept {
    static bool started = []() {
        HRESULT hr = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
        return SUCCEEDED(hr);
    }();
    return started;
}

inline pixel_format mf_to_pix(const GUID& g) noexcept {
    if (g == MFVideoFormat_MJPG)   return pixel_format::MJPEG;
    if (g == MFVideoFormat_YUY2)   return pixel_format::YUYV;
    if (g == MFVideoFormat_UYVY)   return pixel_format::UYVY;
    if (g == MFVideoFormat_NV12)   return pixel_format::NV12;
    if (g == MFVideoFormat_RGB32)  return pixel_format::BGRA32;
    if (g == MFVideoFormat_RGB24)  return pixel_format::BGR24;
    if (g == MFVideoFormat_ARGB32) return pixel_format::BGRA32;
    if (g == MFVideoFormat_L8 || g == MFVideoFormat_L16) return pixel_format::GREY;
    return pixel_format::Unknown;
}

inline GUID pix_to_mf_out(pixel_format p) noexcept {
    switch (p) {
        case pixel_format::MJPEG:  return MFVideoFormat_MJPG;
        case pixel_format::YUYV:   return MFVideoFormat_YUY2;
        case pixel_format::UYVY:   return MFVideoFormat_UYVY;
        case pixel_format::NV12:   return MFVideoFormat_NV12;
        case pixel_format::GREY:   return MFVideoFormat_L8;
        default:                   return MFVideoFormat_RGB32;
    }
}

inline void cstr16_from_wide(const wchar_t* s, knst_c16string& out) noexcept {
    out.clear();
    if (!s) return;
    while (*s) out.append(static_cast<char16_t>(*s++));
}

inline HRESULT mf_attr_get_str16(::IMFAttributes* a, REFGUID key, knst_c16string& out) noexcept {
    WCHAR buf[1024] = {0};
    UINT32 len = 0;
    HRESULT hr = a->GetString(key, buf, 1024, &len);
    if (SUCCEEDED(hr)) cstr16_from_wide(buf, out);
    return hr;
}

inline bool mf_media_type_dims(::IMFMediaType* t, GUID& sub, uint32_t& w, uint32_t& h) noexcept {
    if (FAILED(t->GetGUID(MF_MT_SUBTYPE, &sub))) return false;
    UINT64 packed = 0;
    if (FAILED(t->GetUINT64(MF_MT_FRAME_SIZE, &packed))) return false;
    w = (uint32_t)(packed >> 32);
    h = (uint32_t)(packed & 0xFFFFFFFFu);
    return w > 0 && h > 0;
}

inline bool mf_media_type_fps(::IMFMediaType* t, uint32_t& num, uint32_t& den) noexcept {
    UINT64 packed = 0;
    if (FAILED(t->GetUINT64(MF_MT_FRAME_RATE, &packed))) return false;
    num = (uint32_t)(packed >> 32);
    den = (uint32_t)(packed & 0xFFFFFFFFu);
    if (den == 0) den = 1;
    return num > 0;
}

inline uint32_t mf_guess_stride(const GUID& sub, uint32_t w) noexcept {
    if (sub == MFVideoFormat_MJPG) return 0;
    LONG stride = 0;
    if (SUCCEEDED(::MFGetStrideForBitmapInfoHeader(sub.Data1, w, &stride)) && stride > 0)
        return (uint32_t)stride;
    if (sub == MFVideoFormat_RGB32 || sub == MFVideoFormat_ARGB32) return w * 4;
    if (sub == MFVideoFormat_RGB24) return w * 3;
    if (sub == MFVideoFormat_YUY2 || sub == MFVideoFormat_UYVY) return ((w + 1) / 2) * 4;
    if (sub == MFVideoFormat_NV12) return w;
    if (sub == MFVideoFormat_L8)   return w;
    return 0;
}

inline ::IMFActivate* mf_find_device_by_id(const knst_c16string& id) noexcept {
    ::IMFActivate* result = nullptr;
    ::IMFAttributes* attrs = nullptr;
    if (FAILED(::MFCreateAttributes(&attrs, 1)) || !attrs) return nullptr;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    UINT32 count = 0;
    ::IMFActivate** devices = nullptr;
    if (SUCCEEDED(::MFEnumDeviceSources(attrs, &devices, &count)) && devices) {
        for (UINT32 i = 0; i < count; ++i) {
            knst_c16string link;
            if (SUCCEEDED(mf_attr_get_str16(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, link))) {
                if (link == id) {
                    devices[i]->AddRef();
                    result = devices[i];
                    break;
                }
            }
        }
        for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
        ::CoTaskMemFree(devices);
    }
    attrs->Release();
    return result;
}

inline long ctl_to_procamp(control_type t) noexcept {
    switch (t) {
        case control_type::Brightness:         return VideoProcAmp_Brightness;
        case control_type::Contrast:           return VideoProcAmp_Contrast;
        case control_type::Saturation:         return VideoProcAmp_Saturation;
        case control_type::Hue:                return VideoProcAmp_Hue;
        case control_type::Gain:               return VideoProcAmp_Gain;
        case control_type::Gamma:              return VideoProcAmp_Gamma;
        case control_type::Sharpness:          return VideoProcAmp_Sharpness;
        case control_type::Backlight:          return VideoProcAmp_BacklightCompensation;
        case control_type::WhiteBalance:       return VideoProcAmp_WhiteBalance;
        default: return 0;
    }
}
inline long ctl_to_camctrl(control_type t) noexcept {
    switch (t) {
        case control_type::Exposure: return CameraControl_Exposure;
        case control_type::Focus:    return CameraControl_Focus;
        default: return 0;
    }
}
inline control_type ctl_auto_to_manual(control_type t) noexcept {
    switch (t) {
        case control_type::ExposureAuto:     return control_type::Exposure;
        case control_type::FocusAuto:        return control_type::Focus;
        case control_type::WhiteBalanceAuto: return control_type::WhiteBalance;
        default: return control_type::Unknown;
    }
}

} // namespace detail

inline const char* camera::backend_name() noexcept { return "Windows Media Foundation"; }

inline result<knst_vector<camera_info>> camera::enumerate() noexcept {
    using R = result<knst_vector<camera_info>>;
    detail::mf_com_scope com;
    if (!detail::mf_ensure_started()) return err_result<knst_vector<camera_info>>(errc::NotSupported);

    ::IMFAttributes* attrs = nullptr;
    if (FAILED(::MFCreateAttributes(&attrs, 1)) || !attrs)
        return err_result<knst_vector<camera_info>>(errc::IoError);
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    UINT32 count = 0;
    ::IMFActivate** devices = nullptr;
    HRESULT hr = ::MFEnumDeviceSources(attrs, &devices, &count);
    attrs->Release();
    if (FAILED(hr) || !devices || count == 0) {
        if (devices) ::CoTaskMemFree(devices);
        return err_result<knst_vector<camera_info>>(errc::NotFound);
    }

    knst_vector<camera_info> out;
    for (UINT32 i = 0; i < count; ++i) {
        camera_info ci;
        ci.index = i;
        ci.has_video = true;
        ci.is_default = (i == 0);

        detail::mf_attr_get_str16(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, ci.name);
        detail::mf_attr_get_str16(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, ci.device_id);
        ci.driver = u"MediaFoundation";
        ci.bus_info = ci.device_id;
        if (ci.name.empty()) ci.name = ci.device_id;

        knst_byte_string dev8 = knst_dev::to_utf8(ci.device_id);
        const char* p = (const char*)dev8.data();
        if (p) {
            const char* v = ::strstr(p, "vid_"); if (!v) v = ::strstr(p, "VID_");
            if (v) { unsigned x=0; if (::sscanf(v, "vid_%04x", &x) == 1 || ::sscanf(v, "VID_%04X", &x) == 1) ci.vid = (uint16_t)x; }
            const char* q = ::strstr(p, "pid_"); if (!q) q = ::strstr(p, "PID_");
            if (q) { unsigned x=0; if (::sscanf(q, "pid_%04x", &x) == 1 || ::sscanf(q, "PID_%04X", &x) == 1) ci.pid = (uint16_t)x; }
        }

        ::IMFMediaSource* src = nullptr;
        if (SUCCEEDED(devices[i]->ActivateObject(IID_PPV_ARGS(&src))) && src) {
            ::IMFPresentationDescriptor* pd = nullptr;
            if (SUCCEEDED(src->CreatePresentationDescriptor(&pd)) && pd) {
                DWORD stream_count = 0;
                if (SUCCEEDED(pd->GetStreamDescriptorCount(&stream_count))) {
                    for (DWORD s = 0; s < stream_count && s < 4; ++s) {
                        BOOL selected = FALSE;
                        ::IMFStreamDescriptor* sd = nullptr;
                        if (FAILED(pd->GetStreamDescriptorByIndex(s, &selected, &sd)) || !sd) continue;
                        ::IMFMediaTypeHandler* th = nullptr;
                        if (SUCCEEDED(sd->GetMediaTypeHandler(&th)) && th) {
                            DWORD mt_count = 0;
                            if (SUCCEEDED(th->GetMediaTypeCount(&mt_count))) {
                                for (DWORD m = 0; m < mt_count && m < 512; ++m) {
                                    ::IMFMediaType* mt = nullptr;
                                    if (FAILED(th->GetMediaTypeByIndex(m, &mt)) || !mt) continue;
                                    GUID sub; uint32_t w=0,h=0;
                                    if (detail::mf_media_type_dims(mt, sub, w, h)) {
                                        pixel_format pf = detail::mf_to_pix(sub);
                                        if (pf != pixel_format::Unknown) {
                                            format f;
                                            f.width = w; f.height = h; f.pix = pf;
                                            f.is_compressed = (sub == MFVideoFormat_MJPG);
                                            uint32_t fn=0, fd=1;
                                            if (!detail::mf_media_type_fps(mt, fn, fd)) { fn = 30; fd = 1; }
                                            f.fps_num = fn; f.fps_den = fd;
                                            if (w > ci.max_width)  ci.max_width = w;
                                            if (h > ci.max_height) ci.max_height = h;
                                            ci.formats.push_back(f);
                                        }
                                    }
                                    mt->Release();
                                }
                            }
                            th->Release();
                        }
                        sd->Release();
                    }
                }
                pd->Release();
            }
            src->Shutdown();
            src->Release();
        }

        out.push_back(std::move(ci));
    }
    for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
    ::CoTaskMemFree(devices);

    if (out.empty()) return err_result<knst_vector<camera_info>>(errc::NotFound);
    return R::success(std::move(out));
}

inline errc camera::platform_open_(const knst_c16string& id, const open_options& opt) noexcept {
    detail::mf_com_scope com;
    if (!detail::mf_ensure_started()) return errc::NotSupported;

    auto fail = [this](errc e) { platform_close_(); return e; };

    ::IMFActivate* activate = detail::mf_find_device_by_id(id);
    if (!activate) return fail(errc::NotFound);

    HRESULT hr = activate->ActivateObject(IID_PPV_ARGS(&mf_source_));
    activate->Release();
    if (FAILED(hr) || !mf_source_) return fail(errc::IoError);

    ::IMFAttributes* attrs = nullptr;
    if (FAILED(::MFCreateAttributes(&attrs, 3)) || !attrs) return fail(errc::IoError);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);

    hr = ::MFCreateSourceReaderFromMediaSource(mf_source_, attrs, &mf_reader_);
    attrs->Release();
    if (FAILED(hr) || !mf_reader_) return fail(errc::IoError);

    GUID want = detail::pix_to_mf_out(opt.pix);
    ::IMFMediaType* out_type = nullptr;
    if (FAILED(::MFCreateMediaType(&out_type)) || !out_type) return fail(errc::IoError);
    out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out_type->SetGUID(MF_MT_SUBTYPE, want);
    if (opt.width && opt.height)
        ::MFSetAttributeSize(out_type, MF_MT_FRAME_SIZE, opt.width, opt.height);
    if (opt.fps_num && opt.fps_den)
        ::MFSetAttributeRatio(out_type, MF_MT_FRAME_RATE, opt.fps_num, opt.fps_den);

    hr = mf_reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, out_type);
    if (FAILED(hr) && want != MFVideoFormat_RGB32) {
        out_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = mf_reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, out_type);
    }
    out_type->Release();
    if (FAILED(hr)) return fail(opt.strict_format ? errc::NotSupported : errc::IoError);

    ::IMFMediaType* actual = nullptr;
    hr = mf_reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &actual);
    if (FAILED(hr) || !actual) return fail(errc::IoError);

    GUID sub = GUID_NULL;
    uint32_t w = 0, h = 0;
    if (!detail::mf_media_type_dims(actual, sub, w, h)) { actual->Release(); return fail(errc::IoError); }
    mf_subtype_ = sub;

    cur_fmt_ = format();
    cur_fmt_.width = w; cur_fmt_.height = h;
    cur_fmt_.pix = detail::mf_to_pix(sub);
    cur_fmt_.is_compressed = (sub == MFVideoFormat_MJPG);
    uint32_t fn = 30, fd = 1;
    if (detail::mf_media_type_fps(actual, fn, fd)) { cur_fmt_.fps_num = fn; cur_fmt_.fps_den = fd; }
    else { cur_fmt_.fps_num = opt.fps_num ? opt.fps_num : 30; cur_fmt_.fps_den = opt.fps_den ? opt.fps_den : 1; }

    UINT32 stride_u = 0;
    if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride_u)) && stride_u != 0) {
        mf_stride_ = (stride_u & 0x80000000u) ? (uint32_t)(-(int32_t)stride_u) : stride_u;
    }
    if (mf_stride_ == 0) mf_stride_ = detail::mf_guess_stride(sub, w);
    actual->Release();

    if (cur_fmt_.pix == pixel_format::Unknown) return fail(errc::NotSupported);
    if (cur_fmt_.pix == pixel_format::MJPEG) mf_stride_ = 0;

    mf_reader_->GetServiceForStream(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), GUID_NULL,
                                    IID_PPV_ARGS(&mf_proc_amp_));
    mf_reader_->GetServiceForStream(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), GUID_NULL,
                                    IID_PPV_ARGS(&mf_cam_ctrl_));

    mf_seq_ = 0;
    return errc::Ok;
}

inline void camera::platform_close_() noexcept {
    if (mf_proc_amp_) { mf_proc_amp_->Release(); mf_proc_amp_ = nullptr; }
    if (mf_cam_ctrl_) { mf_cam_ctrl_->Release(); mf_cam_ctrl_ = nullptr; }
    if (mf_reader_)   { mf_reader_->Release();   mf_reader_   = nullptr; }
    if (mf_source_)   { mf_source_->Shutdown(); mf_source_->Release(); mf_source_ = nullptr; }
    mf_subtype_ = GUID_NULL;
    mf_stride_ = 0;
    mf_seq_ = 0;
}

inline errc camera::platform_stream_on_() noexcept { return errc::Ok; }
inline void  camera::platform_stream_off_() noexcept {}

inline result<frame> camera::grab(uint32_t skip_frames, uint32_t timeout_ms) noexcept {
    using R = result<frame>;
    if (!open_.load()) return fail_t_<frame>(errc::BadState, "camera is not open");
    std::lock_guard<std::mutex> slk(stream_mtx_);
    if (streaming_.load()) return fail_t_<frame>(errc::BadState, "stream is running: use capture_latest()");
    reap_worker_();

    detail::mf_com_scope com;
    std::lock_guard<std::mutex> dlk(dev_mtx_);
    if (!mf_reader_) return fail_t_<frame>(errc::BadState, "device is closed");
    if (skip_frames == UINT32_MAX) skip_frames = opt_.warmup_frames;
    if (skip_frames > 64) skip_frames = 64;
    if (timeout_ms == 0) timeout_ms = KNST_CAMERA_GRAB_TIMEOUT_MS;

    frame f;
    uint32_t good = 0;
    const uint64_t deadline = detail::now_us() + (uint64_t)timeout_ms * 1000ull;

    while (!f.valid && detail::now_us() < deadline) {
        DWORD stream_idx = 0, flags = 0;
        LONGLONG ts = 0;
        ::IMFSample* sample = nullptr;
        HRESULT hr = mf_reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                            0, &stream_idx, &flags, &ts, &sample);
        if (FAILED(hr)) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (sample) sample->Release(); break; }
        if (flags & MF_SOURCE_READERF_STREAMTICK)  { if (sample) sample->Release(); continue; }
        if (!sample) continue;

        ::IMFMediaBuffer* buf = nullptr;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf)) || !buf) { sample->Release(); continue; }
        BYTE* ptr = nullptr; DWORD maxlen = 0, curlen = 0;
        if (FAILED(buf->Lock(&ptr, &maxlen, &curlen)) || !ptr) {
            buf->Release(); sample->Release(); continue;
        }
        const uint32_t n = curlen;
        if (detail::validate_frame_bytes(ptr, n, cur_fmt_.width, cur_fmt_.height, mf_stride_, cur_fmt_.pix) &&
            good++ >= skip_frames) {
            f.data.resize(n);
            if (f.data.length() == n) {
                std::memcpy(&f.data[0], ptr, n);
                f.width = cur_fmt_.width; f.height = cur_fmt_.height; f.stride = mf_stride_;
                f.pix = cur_fmt_.pix;
                f.timestamp_us = (uint64_t)(ts / 10);
                f.sequence = ++mf_seq_;
                f.valid = true;
            }
        }
        buf->Unlock();
        buf->Release();
        sample->Release();
    }

    if (!f.valid) return fail_t_<frame>(errc::Timeout, "no valid frame within the timeout");
    return R::success(std::move(f));
}

inline void camera::worker_loop_() noexcept {
    worker_tid_.store(std::this_thread::get_id());

    detail::mf_com_scope com;
    if (!detail::mf_ensure_started()) {
        set_error_(errc::NotSupported, "Media Foundation failed to start");
        push_event(event::Error);
        return;
    }

    push_event(event::StreamStarted);
    uint64_t last_ok = detail::now_us();
    const uint64_t stall_us = (uint64_t)opt_.stall_timeout_ms * 1000ull;
    uint32_t bad_streak = 0;

    while (streaming_.load(std::memory_order_acquire)) {
        ::IMFSourceReader* r = nullptr;
        {
            std::lock_guard<std::mutex> lk(dev_mtx_);
            r = mf_reader_;
            if (!r) break;
            r->AddRef();
        }

        DWORD stream_idx = 0, flags = 0;
        LONGLONG ts = 0;
        ::IMFSample* sample = nullptr;
        HRESULT hr = r->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                   0, &stream_idx, &flags, &ts, &sample);

        if (FAILED(hr)) {
            r->Release();
            if (++bad_streak > 10) {
                if (!recover_stream_()) break;
                bad_streak = 0; last_ok = detail::now_us();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            continue;
        }
        bad_streak = 0;

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (sample) sample->Release();
            r->Release();
            if (!recover_stream_()) break;
            last_ok = detail::now_us();
            continue;
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            ::IMFMediaType* actual = nullptr;
            if (SUCCEEDED(r->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &actual)) && actual) {
                GUID sub; uint32_t w=0,h=0;
                if (detail::mf_media_type_dims(actual, sub, w, h)) {
                    std::lock_guard<std::mutex> lk(dev_mtx_);
                    mf_subtype_ = sub;
                    cur_fmt_.width = w; cur_fmt_.height = h;
                    cur_fmt_.pix = detail::mf_to_pix(sub);
                    cur_fmt_.is_compressed = (sub == MFVideoFormat_MJPG);
                    UINT32 st = 0;
                    if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &st)) && st) mf_stride_ = st;
                    else mf_stride_ = detail::mf_guess_stride(sub, w);
                }
                actual->Release();
            }
        }
        if (flags & MF_SOURCE_READERF_STREAMTICK) {
            if (sample) sample->Release();
            r->Release();
            continue;
        }
        if (!sample) { r->Release(); continue; }

        ::IMFMediaBuffer* buf = nullptr;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf)) || !buf) {
            sample->Release(); r->Release(); continue;
        }
        BYTE* ptr = nullptr; DWORD maxlen = 0, curlen = 0;
        if (FAILED(buf->Lock(&ptr, &maxlen, &curlen)) || !ptr) {
            buf->Release(); sample->Release(); r->Release(); continue;
        }

        uint32_t w, h, stride; pixel_format pix;
        {
            std::lock_guard<std::mutex> lk(dev_mtx_);
            w = cur_fmt_.width; h = cur_fmt_.height;
            stride = mf_stride_; pix = cur_fmt_.pix;
        }

        if (detail::validate_frame_bytes(ptr, curlen, w, h, stride, pix)) {
            process_frame_(ptr, curlen, w, h, stride, pix, (uint64_t)(ts / 10), ++mf_seq_);
            last_ok = detail::now_us();
        } else {
            frames_corrupt_.fetch_add(1);
            frames_dropped_.fetch_add(1);
        }

        buf->Unlock();
        buf->Release();
        sample->Release();
        r->Release();

        if (stall_us && detail::now_us() - last_ok > stall_us) {
            set_error_(errc::Timeout, "camera stopped delivering frames");
            push_event(event::Error);
            if (!recover_stream_()) break;
            last_ok = detail::now_us();
        }
    }

    push_event(event::StreamStopped);
}

inline bool camera::recover_stream_() noexcept {
    push_event(event::Disconnected);
    auto give_up = [&]() {
        set_error_(errc::Disconnected, "camera disconnected");
        push_event(event::Error);
        finalize_recording_(false, false);
        release_owned_stream_();
        streaming_.store(false);
        return false;
    };
    if (!opt_.auto_reconnect) return give_up();

    detail::mf_com_scope com;
    {
        std::lock_guard<std::mutex> lk(dev_mtx_);
        platform_close_();
    }

    open_options ro = opt_;
    ro.use_default_format = false; ro.strict_format = true;

    uint32_t attempts = 0;
    while (streaming_.load()) {
        const uint32_t interval = opt_.reconnect_interval_ms ? opt_.reconnect_interval_ms : 1000;
        for (uint32_t waited = 0; waited < interval && streaming_.load(); waited += 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!streaming_.load()) return false;
        ++attempts;
        errc e;
        { std::lock_guard<std::mutex> lk(dev_mtx_); e = platform_open_(info_.device_id, ro); }
        if (e == errc::Ok) { reconnects_.fetch_add(1); push_event(event::Reconnected); return true; }
        if (opt_.reconnect_max_attempts && attempts >= opt_.reconnect_max_attempts) break;
    }
    return streaming_.load() ? give_up() : false;
}

inline result<knst_vector<control_info>> camera::controls() noexcept {
    using R = result<knst_vector<control_info>>;
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load()) return fail_t_<knst_vector<control_info>>(errc::BadState, "camera is not open");

    knst_vector<control_info> out;

    auto push_proc = [&](control_type t, long prop) {
        if (!mf_proc_amp_) return;
        long mn=0, mx=0, st=1, df=0, caps=0;
        if (FAILED(mf_proc_amp_->GetRange(prop, &mn, &mx, &st, &df, &caps))) return;
        if (st <= 0) st = 1;
        control_info c;
        c.type = t; c.min = mn; c.max = mx; c.step = st; c.default_value = df;
        c.is_writable = (caps & VideoProcAmp_Flags_Manual) != 0;
        c.is_active = true;
        c.is_auto = (caps & VideoProcAmp_Flags_Auto) != 0;
        long cur=0, flags=0;
        if (SUCCEEDED(mf_proc_amp_->Get(prop, &cur, &flags))) {
            c.current = cur;
            c.is_auto_on = (flags & VideoProcAmp_Flags_Auto) != 0;
        }
        c.name = knst_c16string(control_type_name(t));
        out.push_back(std::move(c));
    };
    auto push_cam = [&](control_type t, long prop) {
        if (!mf_cam_ctrl_) return;
        long mn=0, mx=0, st=1, df=0, caps=0;
        if (FAILED(mf_cam_ctrl_->GetRange(prop, &mn, &mx, &st, &df, &caps))) return;
        if (st <= 0) st = 1;
        control_info c;
        c.type = t; c.min = mn; c.max = mx; c.step = st; c.default_value = df;
        c.is_writable = (caps & CameraControl_Flags_Manual) != 0;
        c.is_active = true;
        c.is_auto = (caps & CameraControl_Flags_Auto) != 0;
        long cur=0, flags=0;
        if (SUCCEEDED(mf_cam_ctrl_->Get(prop, &cur, &flags))) {
            c.current = cur;
            c.is_auto_on = (flags & CameraControl_Flags_Auto) != 0;
        }
        c.name = knst_c16string(control_type_name(t));
        out.push_back(std::move(c));
    };

    push_proc(control_type::Brightness, VideoProcAmp_Brightness);
    push_proc(control_type::Contrast,   VideoProcAmp_Contrast);
    push_proc(control_type::Saturation, VideoProcAmp_Saturation);
    push_proc(control_type::Hue,        VideoProcAmp_Hue);
    push_proc(control_type::Gain,       VideoProcAmp_Gain);
    push_proc(control_type::Gamma,      VideoProcAmp_Gamma);
    push_proc(control_type::Sharpness,  VideoProcAmp_Sharpness);
    push_proc(control_type::Backlight,  VideoProcAmp_BacklightCompensation);
    push_proc(control_type::WhiteBalance, VideoProcAmp_WhiteBalance);
    push_cam(control_type::Exposure,    CameraControl_Exposure);
    push_cam(control_type::Focus,       CameraControl_Focus);

    return R::success(std::move(out));
}

inline result<control_info> camera::get_control(control_type t) noexcept {
    using R = result<control_info>;
    auto all = controls();
    if (!all) return fail_t_<control_info>(errc::BadState, "camera is not open");
    for (auto& c : all.value) if (c.type == t) return R::success(c);
    return fail_t_<control_info>(errc::NotFound, "this camera has no such control");
}

inline result<void> camera::set_control(control_type t, int32_t value) noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load()) return fail_void_(errc::BadState, "camera is not open");

    auto try_set = [](IAMVideoProcAmp* pa, IAMCameraControl* cc,
                       long prop, long v, long manual_flag, DWORD caps) -> result<void> {
        using RV = result<void>;
        const DWORD manual_bit = (pa ? (DWORD)VideoProcAmp_Flags_Manual
                                     : (DWORD)CameraControl_Flags_Manual);
        if (!(caps & manual_bit)) {
           
            return RV::fail(knst_dev::status::NotSupported, 0);
        }
        HRESULT hr;
        if (pa) {
            hr = pa->Set(prop, v, manual_flag);
            if (SUCCEEDED(hr)) {
                
                long rb = 0, rbf = 0;
                if (SUCCEEDED(pa->Get(prop, &rb, &rbf)) && rb == v)
                    return RV::success();
               
                return RV::success();
            }
           
            long rb = 0, rbf = 0;
            if (SUCCEEDED(pa->Get(prop, &rb, &rbf))) {
                if (SUCCEEDED(pa->Set(prop, rb, manual_flag))) {
                    hr = pa->Set(prop, v, manual_flag);
                    if (SUCCEEDED(hr)) return RV::success();
                }
            }
           
            if (SUCCEEDED(pa->Get(prop, &rb, &rbf)) && rb == v)
                return RV::success();
        } else if (cc) {
            hr = cc->Set(prop, v, manual_flag);
            if (SUCCEEDED(hr)) {
                long rb = 0, rbf = 0;
                if (SUCCEEDED(cc->Get(prop, &rb, &rbf)) && rb == v)
                    return RV::success();
                return RV::success();
            }
            long rb = 0, rbf = 0;
            if (SUCCEEDED(cc->Get(prop, &rb, &rbf))) {
                if (SUCCEEDED(cc->Set(prop, rb, manual_flag))) {
                    hr = cc->Set(prop, v, manual_flag);
                    if (SUCCEEDED(hr)) return RV::success();
                }
            }
            if (SUCCEEDED(cc->Get(prop, &rb, &rbf)) && rb == v)
                return RV::success();
        }
        return RV::fail(knst_dev::status::IoError, 0);
    };

    long prop = detail::ctl_to_procamp(t);
    if (prop && mf_proc_amp_) {
        long mn=0, mx=0, st=1, df=0, caps=0;
        if (FAILED(mf_proc_amp_->GetRange(prop, &mn, &mx, &st, &df, &caps)))
            return fail_void_(errc::NotSupported, "control not present");
        if (st <= 0) st = 1;
        int64_t v = value;
        if (v < mn) v = mn;
        if (v > mx) v = mx;
        if (st > 1) v = mn + ((v - mn) / st) * st;
        auto r = try_set(mf_proc_amp_, nullptr, prop, (long)v,
                          VideoProcAmp_Flags_Manual, caps);
        if (r) return ok_void();
        if (r.st == knst_dev::status::NotSupported)
            return fail_void_(errc::NotSupported, "driver does not allow manual writes for this control");
        return fail_void_(errc::IoError, "set control failed");
    }
    prop = detail::ctl_to_camctrl(t);
    if (prop && mf_cam_ctrl_) {
        long mn=0, mx=0, st=1, df=0, caps=0;
        if (FAILED(mf_cam_ctrl_->GetRange(prop, &mn, &mx, &st, &df, &caps)))
            return fail_void_(errc::NotSupported, "control not present");
        if (st <= 0) st = 1;
        int64_t v = value;
        if (v < mn) v = mn;
        if (v > mx) v = mx;
        if (st > 1) v = mn + ((v - mn) / st) * st;
        auto r = try_set(nullptr, mf_cam_ctrl_, prop, (long)v,
                          CameraControl_Flags_Manual, caps);
        if (r) return ok_void();
        if (r.st == knst_dev::status::NotSupported)
            return fail_void_(errc::NotSupported, "driver does not allow manual writes for this control");
        return fail_void_(errc::IoError, "set control failed");
    }
    return fail_void_(errc::NotSupported, "unknown control");
}

inline result<void> camera::set_control_auto(control_type t, bool auto_on) noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load()) return fail_void_(errc::BadState, "camera is not open");

    // Accept the manual name as a shortcut for its auto switch (like Linux)
    if (t == control_type::Exposure)     t = control_type::ExposureAuto;
    else if (t == control_type::Focus)   t = control_type::FocusAuto;
    else if (t == control_type::WhiteBalance) t = control_type::WhiteBalanceAuto;

    control_type manual = detail::ctl_auto_to_manual(t);
    if (manual == control_type::Unknown) return fail_void_(errc::NotSupported, "not an auto control");

    long cur = 0, cur_flags = 0;
    long prop = detail::ctl_to_procamp(manual);
    if (prop && mf_proc_amp_) {
        if (SUCCEEDED(mf_proc_amp_->Get(prop, &cur, &cur_flags))) {
            long fl = auto_on ? (long)VideoProcAmp_Flags_Auto : (long)VideoProcAmp_Flags_Manual;
            HRESULT hr = mf_proc_amp_->Set(prop, cur, fl);
            return hr == S_OK ? ok_void() : fail_void_(errc::IoError, "set auto failed");
        }
    }
    prop = detail::ctl_to_camctrl(manual);
    if (prop && mf_cam_ctrl_) {
        if (SUCCEEDED(mf_cam_ctrl_->Get(prop, &cur, &cur_flags))) {
            long fl = auto_on ? (long)CameraControl_Flags_Auto : (long)CameraControl_Flags_Manual;
            HRESULT hr = mf_cam_ctrl_->Set(prop, cur, fl);
            return hr == S_OK ? ok_void() : fail_void_(errc::IoError, "set auto failed");
        }
    }
    return fail_void_(errc::NotSupported, "auto not available");
}

inline result<void> camera::reset_control(control_type t) noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load()) return fail_void_(errc::BadState, "camera is not open");

    long prop = detail::ctl_to_procamp(t);
    if (prop && mf_proc_amp_) {
        long mn=0, mx=0, st=1, df=0, caps=0;
        if (SUCCEEDED(mf_proc_amp_->GetRange(prop, &mn, &mx, &st, &df, &caps))) {
            HRESULT hr = mf_proc_amp_->Set(prop, df, (caps & VideoProcAmp_Flags_Manual) ? VideoProcAmp_Flags_Manual : 0);
            return hr == S_OK ? ok_void() : fail_void_(errc::IoError, "reset failed");
        }
    }
    prop = detail::ctl_to_camctrl(t);
    if (prop && mf_cam_ctrl_) {
        long mn=0, mx=0, st=1, df=0, caps=0;
        if (SUCCEEDED(mf_cam_ctrl_->GetRange(prop, &mn, &mx, &st, &df, &caps))) {
            HRESULT hr = mf_cam_ctrl_->Set(prop, df, (caps & CameraControl_Flags_Manual) ? CameraControl_Flags_Manual : 0);
            return hr == S_OK ? ok_void() : fail_void_(errc::IoError, "reset failed");
        }
    }
    return fail_void_(errc::NotSupported, "unknown control");
}

inline result<void> camera::reset_all_controls() noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load()) return fail_void_(errc::BadState, "camera is not open");
    errc worst = errc::Ok;

    auto reset_proc = [&](long prop) {
        long mn=0,mx=0,st=1,df=0,caps=0;
        if (mf_proc_amp_ && SUCCEEDED(mf_proc_amp_->GetRange(prop, &mn, &mx, &st, &df, &caps))) {
            if (FAILED(mf_proc_amp_->Set(prop, df, (caps & VideoProcAmp_Flags_Manual) ? VideoProcAmp_Flags_Manual : 0)))
                if (worst == errc::Ok) worst = errc::IoError;
        }
    };
    auto reset_cam = [&](long prop) {
        long mn=0,mx=0,st=1,df=0,caps=0;
        if (mf_cam_ctrl_ && SUCCEEDED(mf_cam_ctrl_->GetRange(prop, &mn, &mx, &st, &df, &caps))) {
            if (FAILED(mf_cam_ctrl_->Set(prop, df, (caps & CameraControl_Flags_Manual) ? CameraControl_Flags_Manual : 0)))
                if (worst == errc::Ok) worst = errc::IoError;
        }
    };
    reset_proc(VideoProcAmp_Brightness);
    reset_proc(VideoProcAmp_Contrast);
    reset_proc(VideoProcAmp_Saturation);
    reset_proc(VideoProcAmp_Hue);
    reset_proc(VideoProcAmp_Gain);
    reset_proc(VideoProcAmp_Gamma);
    reset_proc(VideoProcAmp_Sharpness);
    reset_proc(VideoProcAmp_BacklightCompensation);
    reset_proc(VideoProcAmp_WhiteBalance);
    reset_cam(CameraControl_Exposure);
    reset_cam(CameraControl_Focus);
    return worst == errc::Ok ? ok_void() : fail_void_(worst, "some controls refused to reset");
}

inline result<knst_vector<control_value>> camera::save_controls() noexcept {
    using R = result<knst_vector<control_value>>;
    auto cs = controls();
    if (!cs) return R::fail(cs.st, cs.native);
    knst_vector<control_value> out;
    for (auto& c : cs.value) {
        control_value v; v.type = c.type; v.value = c.current;
        out.push_back(v);
    }
    return R::success(std::move(out));
}

inline result<void> camera::restore_controls(const knst_vector<control_value>& v) noexcept {
    std::lock_guard<std::mutex> lk(dev_mtx_);
    if (!open_.load()) return fail_void_(errc::BadState, "camera is not open");
    for (auto& cv : v) {
        long prop = detail::ctl_to_procamp(cv.type);
        if (prop && mf_proc_amp_)
            mf_proc_amp_->Set(prop, cv.value, VideoProcAmp_Flags_Manual);
        else {
            prop = detail::ctl_to_camctrl(cv.type);
            if (prop && mf_cam_ctrl_)
                mf_cam_ctrl_->Set(prop, cv.value, CameraControl_Flags_Manual);
        }
    }
    return ok_void();
}

#else  // ---------------- no device layer on this platform ----------------

inline const char* camera::backend_name() noexcept {
#if KNST_CAM_WINDOWS
    return "Windows (device layer not implemented)";
#elif KNST_CAM_ANDROID
    return "Android (device layer not implemented)";
#else
    return "unsupported";
#endif
}
inline result<knst_vector<camera_info>> camera::enumerate() noexcept { return err_result<knst_vector<camera_info>>(errc::NotSupported); }
inline errc camera::platform_open_(const knst_c16string&, const open_options&) noexcept { return errc::NotSupported; }
inline void camera::platform_close_() noexcept {}
inline errc camera::platform_stream_on_() noexcept { return errc::NotSupported; }
inline void camera::platform_stream_off_() noexcept {}
inline void camera::worker_loop_() noexcept {}
inline result<frame> camera::grab(uint32_t, uint32_t) noexcept { return fail_t_<frame>(errc::NotSupported, "no device layer on this platform"); }
inline result<knst_vector<control_info>> camera::controls() noexcept { return err_result<knst_vector<control_info>>(errc::NotSupported); }
inline result<control_info> camera::get_control(control_type) noexcept { return err_result<control_info>(errc::NotSupported); }
inline result<void> camera::set_control(control_type, int32_t) noexcept { return void_result(errc::NotSupported); }
inline result<void> camera::set_control_auto(control_type, bool) noexcept { return void_result(errc::NotSupported); }
inline result<void> camera::reset_control(control_type) noexcept { return void_result(errc::NotSupported); }
inline result<void> camera::reset_all_controls() noexcept { return void_result(errc::NotSupported); }
inline result<knst_vector<control_value>> camera::save_controls() noexcept { return err_result<knst_vector<control_value>>(errc::NotSupported); }
inline result<void> camera::restore_controls(const knst_vector<control_value>&) noexcept { return void_result(errc::NotSupported); }

#endif // KNST_CAM_LINUX_V4L2

} // namespace knst_cam