// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_video.hpp
----------------------------
    Video / audio container & codec framework for KernelNucleusT. No FFmpeg, no external codec libs.

    HONESTY MATRIX  (what is real, what is a plug-in point)
    =======================================================
    REAL (implemented in this header, no dependencies):
      Containers
        AVI  write+read   MJPEG, uncompressed DIB (BGR24 / BGRA32 / GREY8), passthrough of any
                           already-encoded codec (H.264 etc. as opaque chunks); PCM audio; INFO metadata;
                           crash recovery (a file whose writer died is still readable)
        MKV  write+read   MJPEG, passthrough codecs with CodecPrivate (H.264/H.265/VP8/VP9/AV1/...),
                           PCM audio, Cues + SeekHead, Duration, title, chapters.  WebM files are READ too.
        MP4  write+read   MJPEG ('mp4v'/OTI 0x6C), passthrough 'avc1'/'hvc1' with avcC/hvcC. moov at the end.
        MOV  write+read   MJPEG ('jpeg'), PCM_S16LE audio ('sowt')
        WAV  write+read   PCM U8/S16/S24/S32/F32, WAVE_FORMAT_EXTENSIBLE, LIST/INFO
        RAW  write+read   headerless pixel dump (any pixel_format)
      Codecs
        MJPEG  encoder (baseline, 4:4:4 / 4:2:2 / 4:2:0 / grey, quality 1..100, restart markers) written
               from scratch in this header; decoder = knst_image_loader (+ automatic DHT injection for
               AVI MJPEG frames that omit Huffman tables)
        RAW    all packed / planar pixel formats <-> RGB24 (BT.601 / BT.709, full / limited range)
      Tools
        resize (nearest / bilinear / box), crop, rotate, flip, brightness / contrast, grey,
        Bayer demosaic, PCM conversion + resampler, SRT / WebVTT / ASS read+write,
        motion detector, timelapse gate, motion-triggered recorder with pre-roll, JSON export.

    CONFIG MACROS (define before including)
    =======================================
      KNST_VIDEO_MAX_DIMENSION      max width/height accepted          (default 16384)
      KNST_VIDEO_MAX_SAMPLE_BYTES   max size of one packet/frame       (default 256 MiB)
      KNST_VIDEO_MAX_SAMPLES        max packets per track in demuxers  (default 64M)
      KNST_VIDEO_MAX_TRACKS         max tracks parsed                  (default 16)
      KNST_VIDEO_HAS_IMAGE_LOADER   1 = use knst_image_loader to decode MJPEG (default 1, set 0 to disable)
      KNST_VIDEO_FILE_BUFFER        stdio buffer size                  (default 256 KiB)

    EXAMPLES
    ========

        // 1) Record 5 s of a generated image into an MJPEG AVI with stereo PCM audio
        knst_vid::video_writer w;
        knst_vid::video_writer::config cfg;
        cfg.cont = knst_vid::container::AVI;
        cfg.vcodec = knst_vid::video_codec::MJPEG;
        cfg.width = 640; cfg.height = 480; cfg.fps_num = 30;
        cfg.jpeg_quality = 85;
        cfg.acodec = knst_vid::audio_codec::PCM_S16LE; cfg.sample_rate = 48000; cfg.channels = 2;
        auto r = w.open(u"out/test.avi", cfg);
        if (!r) { ... }
        knst_byte_string rgb(...);                      // 640*480*3 bytes
        for (int i = 0; i < 150; ++i) {
            w.write_frame_rgb24(rgb.data());            // encodes to JPEG, pts = frame_index / fps
            w.write_audio(pcm.data(), 1600 * 4);        // 1/30 s of S16 stereo
        }
        w.close();

        // 2) Read it back, decode frame 10 to RGB24
        knst_vid::video_reader rd;
        if (rd.open(u"out/test.avi")) {
            auto f = rd.read_frame_rgb24(10);           // f.value.data = RGB24, width/height filled
            auto a = rd.read_audio_all();               // PCM
        }

        // 3) Pass through an H.264 stream you encoded yourself into MKV
        cfg.cont = knst_vid::container::MKV; cfg.vcodec = knst_vid::video_codec::H264;
        cfg.vextradata = avcC_bytes;                    // CodecPrivate
        w.open(u"out/clip.mkv", cfg);
        w.write_encoded(nal_data, nal_size, is_key, pts_us);
*/

#pragma once

// Some platform SDKs (X11 / quictls / V4L2 / Android NDK) #define RAW as a
// numeric constant. That collides with our `enum class video_codec::RAW`
// and `enum class container::RAW`, breaking the entire knst_vid namespace
// and cascading into every header that depends on it.
#ifdef RAW
    #undef RAW
#endif
#ifdef None
    #undef None
#endif
#ifdef DELETE
    #undef DELETE
#endif

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <utility>
#include <type_traits>

#if defined(KNST_USING_PLATFORM_WINDOWS) && KNST_USING_PLATFORM_WINDOWS
    #define KNST_VID_WINDOWS 1
#else
    #define KNST_VID_WINDOWS 0
    #include <sys/types.h>
#endif

#if defined(_MSC_VER)
    #include <intrin.h>
#endif

#ifndef KNST_VIDEO_MAX_DIMENSION
    #define KNST_VIDEO_MAX_DIMENSION 16384u
#endif
#ifndef KNST_VIDEO_MAX_SAMPLE_BYTES
    #define KNST_VIDEO_MAX_SAMPLE_BYTES (256u * 1024u * 1024u)
#endif
#ifndef KNST_VIDEO_MAX_SAMPLES
    #define KNST_VIDEO_MAX_SAMPLES (64u * 1024u * 1024u)
#endif
#ifndef KNST_VIDEO_MAX_TRACKS
    #define KNST_VIDEO_MAX_TRACKS 16u
#endif
#ifndef KNST_VIDEO_HAS_IMAGE_LOADER
    #define KNST_VIDEO_HAS_IMAGE_LOADER 1
#endif
#ifndef KNST_VIDEO_FILE_BUFFER
    #define KNST_VIDEO_FILE_BUFFER (256u * 1024u)
#endif

namespace knst_vid {

using knst_dev::status;
using knst_dev::result;

// ============================================================
// Errors
// ============================================================

enum class errc : uint8_t {
    Ok = 0,
    InvalidArgument,
    NotSupported,
    NotFound,
    IoError,
    OutOfMemory,
    BadState,
    Corrupt, // file / bitstream is damaged or violates the format
    TooLarge, // exceeds a container limit or a KNST_VIDEO_MAX_* limit
    EndOfStream
};

inline const char* errc_string(errc e) noexcept {
    switch (e) {
        case errc::Ok: return "ok";
        case errc::InvalidArgument: return "invalid argument";
        case errc::NotSupported: return "not supported";
        case errc::NotFound: return "not found / out of range";
        case errc::IoError: return "I/O error";
        case errc::OutOfMemory: return "out of memory";
        case errc::BadState: return "bad state (not open / already closed)";
        case errc::Corrupt: return "corrupt or malformed data";
        case errc::TooLarge: return "exceeds a size limit of the format or library";
        case errc::EndOfStream: return "end of stream";
    }
    return "unknown";
}

inline status to_status(errc e) noexcept {
    switch (e) {
        case errc::InvalidArgument: return status::InvalidArgument;
        case errc::NotSupported: return status::NotSupported;
        case errc::NotFound: return status::NotFound;
        case errc::EndOfStream: return status::NotFound;
        case errc::OutOfMemory: return status::OutOfMemory;
        case errc::BadState: return status::BadState;
        case errc::TooLarge: return status::InvalidArgument;
        default: return status::IoError;
    }
}

template<class T> inline result<T> err_result(errc e) noexcept { return result<T>::fail(to_status(e)); }
inline result<void> ok_void() noexcept { return result<void>::success(); }
inline result<void> void_result(errc e) noexcept {
    return e == errc::Ok ? result<void>::success() : result<void>::fail(to_status(e));
}

// ============================================================
// Enums
// ============================================================

enum class video_codec : uint8_t {
    MJPEG, // REAL: encode + decode
    RAW, // REAL: uncompressed
    H264, H265, VP8, VP9, AV1, MPEG4,  // passthrough (opaque packets)
    ProRes, DNxHD,
    Unknown
};

enum class audio_codec : uint8_t {
    None,
    PCM_S16LE, // REAL
    PCM_F32LE,
    PCM_S24LE,
    PCM_U8,
    PCM_S32LE,
    AAC, Opus, MP3, FLAC, Vorbis, // passthrough
    Unknown
};

enum class container : uint8_t {
    AVI, WAV, RAW, MKV, MP4, MOV, WebM, Unknown
};

enum class track_kind : uint8_t { Video, Audio, Subtitle, Other };

enum class pixel_format : uint8_t {
    RGB24, BGR24, RGBA32, BGRA32, GREY,
    YUYV, UYVY, YVYU,
    NV12, NV21, I420, YV12,
    MJPEG,
    Bayer_BGGR, Bayer_GBRG, Bayer_GRBG, Bayer_RGGB,
    Unknown
};

inline const char* pixel_format_name(pixel_format p) noexcept {
    switch (p) {
        case pixel_format::RGB24: return "RGB24";
        case pixel_format::BGR24: return "BGR24";
        case pixel_format::RGBA32: return "RGBA32";
        case pixel_format::BGRA32: return "BGRA32";
        case pixel_format::GREY: return "GREY";
        case pixel_format::YUYV: return "YUYV";
        case pixel_format::UYVY: return "UYVY";
        case pixel_format::YVYU: return "YVYU";
        case pixel_format::NV12: return "NV12";
        case pixel_format::NV21: return "NV21";
        case pixel_format::I420: return "I420";
        case pixel_format::YV12: return "YV12";
        case pixel_format::MJPEG: return "MJPEG";
        case pixel_format::Bayer_BGGR: return "BGGR";
        case pixel_format::Bayer_GBRG: return "GBRG";
        case pixel_format::Bayer_GRBG: return "GRBG";
        case pixel_format::Bayer_RGGB: return "RGGB";
        default: return "Unknown";
    }
}

inline const char* video_codec_name(video_codec c) noexcept {
    switch (c) {
        case video_codec::MJPEG: return "MJPEG";   case video_codec::RAW: return "RAW";
        case video_codec::H264: return "H.264";    case video_codec::H265: return "H.265";
        case video_codec::VP8: return "VP8";       case video_codec::VP9: return "VP9";
        case video_codec::AV1: return "AV1";       case video_codec::MPEG4: return "MPEG-4";
        case video_codec::ProRes: return "ProRes"; case video_codec::DNxHD: return "DNxHD";
        default: return "Unknown";
    }
}

inline const char* audio_codec_name(audio_codec c) noexcept {
    switch (c) {
        case audio_codec::None: return "none";       case audio_codec::PCM_S16LE: return "PCM S16LE";
        case audio_codec::PCM_F32LE: return "PCM F32LE"; case audio_codec::PCM_S24LE: return "PCM S24LE";
        case audio_codec::PCM_U8: return "PCM U8";   case audio_codec::PCM_S32LE: return "PCM S32LE";
        case audio_codec::AAC: return "AAC";         case audio_codec::Opus: return "Opus";
        case audio_codec::MP3: return "MP3";         case audio_codec::FLAC: return "FLAC";
        case audio_codec::Vorbis: return "Vorbis";
        default: return "Unknown";
    }
}

inline bool audio_codec_is_pcm(audio_codec c) noexcept {
    return c == audio_codec::PCM_S16LE || c == audio_codec::PCM_F32LE || c == audio_codec::PCM_S24LE ||
           c == audio_codec::PCM_U8 || c == audio_codec::PCM_S32LE;
}

inline uint32_t audio_codec_bits(audio_codec c) noexcept {
    switch (c) {
        case audio_codec::PCM_U8: return 8;   case audio_codec::PCM_S16LE: return 16;
        case audio_codec::PCM_S24LE: return 24; case audio_codec::PCM_S32LE: return 32;
        case audio_codec::PCM_F32LE: return 32; default: return 0;
    }
}


struct plane_layout {
    uint32_t planes = 0;
    uint64_t offset[3] = {0, 0, 0};
    uint32_t stride[3] = {0, 0, 0};
    uint32_t width[3]  = {0, 0, 0};   // in samples
    uint32_t height[3] = {0, 0, 0};
    uint64_t total = 0;
};

// stride == 0 -> tightly packed. For planar formats `stride` is the luma stride.
inline bool compute_layout(pixel_format p, uint32_t w, uint32_t h, uint32_t stride, plane_layout& L) noexcept {
    L = plane_layout();
    if (w == 0 || h == 0 || w > KNST_VIDEO_MAX_DIMENSION || h > KNST_VIDEO_MAX_DIMENSION) return false;
    uint32_t bpp = 0;
    switch (p) {
        case pixel_format::RGB24: case pixel_format::BGR24: bpp = 3; break;
        case pixel_format::RGBA32: case pixel_format::BGRA32: bpp = 4; break;
        case pixel_format::GREY: case pixel_format::Bayer_BGGR: case pixel_format::Bayer_GBRG:
        case pixel_format::Bayer_GRBG: case pixel_format::Bayer_RGGB: bpp = 1; break;
        case pixel_format::YUYV: case pixel_format::UYVY: case pixel_format::YVYU: bpp = 2; break;
        default: break;
    }
    if (bpp) {
        uint32_t minstride = (p == pixel_format::YUYV || p == pixel_format::UYVY || p == pixel_format::YVYU)? ((w + 1) / 2) * 4 : w * bpp;
                                 
        if (stride == 0) stride = minstride;
        if (stride < minstride) return false;

        L.planes = 1; L.stride[0] = stride; L.width[0] = w; L.height[0] = h;

        L.total = (uint64_t)stride * h;
        return true;
    }
    uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
    if (p == pixel_format::NV12 || p == pixel_format::NV21) {
        if (stride == 0) stride = (w + 1) & ~1u;
        if (stride < cw * 2) return false;

        L.planes = 2;


        L.stride[0] = stride; L.width[0] = w; L.height[0] = h; L.offset[0] = 0;
        L.stride[1] = stride; L.width[1] = cw * 2; L.height[1] = ch; L.offset[1] = (uint64_t)stride * h;
        L.total = L.offset[1] + (uint64_t)stride * ch;
        return true;
    }
    if (p == pixel_format::I420 || p == pixel_format::YV12) {
        if (stride == 0) stride = w;
        if (stride < w) return false;
        uint32_t cs = (stride + 1) / 2;

        L.planes = 3;
        L.stride[0] = stride; L.width[0] = w; L.height[0] = h; L.offset[0] = 0;
        L.stride[1] = cs; L.width[1] = cw; L.height[1] = ch; L.offset[1] = (uint64_t)stride * h;
        L.stride[2] = cs; L.width[2] = cw; L.height[2] = ch; L.offset[2] = L.offset[1] + (uint64_t)cs * ch;
        L.total = L.offset[2] + (uint64_t)cs * ch;
        return true;
    }
    return false;   // MJPEG / Unknown: not a pixel layout
}

inline uint32_t pixel_format_bpp(pixel_format p) noexcept {
    switch (p) {
        case pixel_format::RGB24: case pixel_format::BGR24:  return 24;
        case pixel_format::RGBA32: case pixel_format::BGRA32: return 32;
        case pixel_format::GREY: case pixel_format::Bayer_BGGR: case pixel_format::Bayer_GBRG:
        case pixel_format::Bayer_GRBG: case pixel_format::Bayer_RGGB: return 8;
        case pixel_format::YUYV: case pixel_format::UYVY: case pixel_format::YVYU: return 16;
        case pixel_format::NV12: case pixel_format::NV21: case pixel_format::I420: case pixel_format::YV12: return 12;
        default: return 0;
    }
}

// ============================================================
// Types
// ============================================================

struct video_frame {
    knst_byte_string data;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;            // 0 = tightly packed
    pixel_format pix = pixel_format::Unknown;
    uint64_t pts_us = 0;
    uint32_t sequence = 0;
    bool keyframe = false;
    bool bottom_up = false;         // rows stored bottom row first (AVI DIB)
    bool valid = false;
};

struct audio_frame {
    knst_byte_string data;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    audio_codec codec = audio_codec::None;
    uint64_t pts_us = 0;
    uint32_t frames = 0;  // PCM frames (one frame = one sample for every channel)
    bool valid = false;
};

struct stream_desc {
    uint32_t index = 0;
    track_kind kind = track_kind::Other;
    bool is_video = false;
    video_codec vcodec = video_codec::Unknown;
    audio_codec acodec = audio_codec::None;
    uint32_t width = 0, height = 0;
    uint32_t sample_rate = 0, channels = 0, bits_per_sample = 0;
    double fps = 0.0;
    uint32_t fps_num = 0, fps_den = 1;
    uint64_t bitrate = 0;
    uint64_t duration_us = 0;
    uint64_t frame_count = 0;  // packets in the index
    pixel_format pix = pixel_format::Unknown; // layout of RAW payloads
    bool bottom_up = false;
    uint32_t stride = 0;
    uint32_t codec_tag = 0; // fourcc (AVI) / sample entry (MP4); 0 if none
    knst_c16string codec_name; // e.g. "V_MPEG4/ISO/AVC"
    knst_c16string language;
    knst_byte_string extradata;  // avcC / hvcC / CodecPrivate / esds
};

struct chapter {
    knst_c16string title;
    uint64_t start_us = 0;
    uint64_t end_us = 0;
};

struct metadata {
    knst_c16string title;
    knst_c16string artist;
    knst_c16string album;
    knst_c16string comment;
    knst_c16string copyright;
    knst_c16string software;
    uint32_t year = 0;
    uint32_t track = 0;
    knst_vector<chapter> chapters;
};

struct subtitle_entry {
    uint64_t start_ms = 0;
    uint64_t end_ms = 0;
    knst_c16string text;
};

struct media_info {
    container kind = container::Unknown;
    knst_vector<stream_desc> streams;
    metadata meta;
    uint64_t duration_us = 0;
};

inline uint64_t now_us() noexcept {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ============================================================
// detail: arithmetic, endian, buffers, files
// ============================================================

namespace detail {

inline bool mul_u64(uint64_t a, uint64_t b, uint64_t& o) noexcept {
    if (a != 0 && b > UINT64_MAX / a) return false;
    o = a * b; return true;
}
// a*b/c without 64-bit overflow for the value ranges used here (c, b < 2^32)
inline uint64_t muldiv_u64(uint64_t a, uint64_t b, uint64_t c) noexcept {
    if (c == 0) return 0;
    return (a / c) * b + ((a % c) * b) / c;
}
inline uint32_t gcd_u32(uint32_t a, uint32_t b) noexcept { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }
inline uint8_t clamp8(int v) noexcept { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

inline uint32_t bit_length(uint32_t v) noexcept {
    if (!v) return 0;
#if defined(_MSC_VER)
    unsigned long idx; _BitScanReverse(&idx, v); return (uint32_t)idx + 1;
#else
    return 32u - (uint32_t)__builtin_clz(v);
#endif
}

// Resizes to exactly n bytes (contents of existing bytes are preserved, new bytes are zero).
inline errc alloc_bytes(knst_byte_string& s, uint64_t n) noexcept {
    if (n > 0xFFFFFF00ull) return errc::TooLarge;
    if (s.length() == (uint32_t)n) return errc::Ok;
    s.resize((uint32_t)n);
    return s.length() == (uint32_t)n ? errc::Ok : errc::OutOfMemory;
}

inline void put_u16le(uint8_t* p, uint32_t v) noexcept { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void put_u32le(uint8_t* p, uint32_t v) noexcept { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
inline void put_u32be(uint8_t* p, uint32_t v) noexcept { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
inline uint16_t get_u16le(const uint8_t* p) noexcept { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t get_u32le(const uint8_t* p) noexcept { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint64_t get_u64le(const uint8_t* p) noexcept { return (uint64_t)get_u32le(p) | ((uint64_t)get_u32le(p + 4) << 32); }
inline uint16_t get_u16be(const uint8_t* p) noexcept { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t get_u32be(const uint8_t* p) noexcept { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }
inline uint64_t get_u64be(const uint8_t* p) noexcept { return ((uint64_t)get_u32be(p) << 32) | get_u32be(p + 4); }

// 4-character code as it appears in the file (first char = first byte)
inline constexpr uint32_t fcc(char a, char b, char c, char d) noexcept {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}
inline uint32_t fcc_lower(uint32_t v) noexcept {
    uint32_t r = 0;
    for (int i = 0; i < 4; ++i) {
        uint32_t c = (v >> (i * 8)) & 0xFF;
        if (c >= 'A' && c <= 'Z') c += 32;
        r |= c << (i * 8);
    }
    return r;
}

// ---- growable byte writer (headers, indexes, boxes, EBML) ----
class wbuf {
public:
    uint32_t size() const noexcept { return (uint32_t)b_.length(); }
    const uint8_t* data() const noexcept { return b_.data(); }
    bool ok() const noexcept { return !oom_; }
    void clear() noexcept { b_.clear(); oom_ = false; }
    void put(const void* p, size_t n) noexcept {
        if (n == 0 || oom_) return;
        uint32_t before = b_.length();
        if ((uint64_t)before + n > 0xFFFFFF00ull) { oom_ = true; return; }
        b_.append(static_cast<const unsigned char*>(p), (uint32_t)n);
        if (b_.length() != before + (uint32_t)n) oom_ = true;
    }
    void zeros(size_t n) noexcept { uint8_t z[64] = {0}; while (n) { size_t k = n < 64 ? n : 64; put(z, k); n -= k; } }


    void u8(uint32_t v) noexcept { uint8_t b = (uint8_t)v; put(&b, 1); }
    void u16le(uint32_t v) noexcept { uint8_t b[2]; put_u16le(b, v); put(b, 2); }
    void u32le(uint32_t v) noexcept { uint8_t b[4]; put_u32le(b, v); put(b, 4); }
    void u64le(uint64_t v) noexcept { u32le((uint32_t)v); u32le((uint32_t)(v >> 32)); }

    void u16be(uint32_t v) noexcept { u8(v >> 8); u8(v); }
    void u24be(uint32_t v) noexcept { u8(v >> 16); u8(v >> 8); u8(v); }
    void u32be(uint32_t v) noexcept { uint8_t b[4]; put_u32be(b, v); put(b, 4); }
    void u64be(uint64_t v) noexcept { u32be((uint32_t)(v >> 32)); u32be((uint32_t)v); }


    void tag(const char* s) noexcept { put(s, 4); } // 4 ASCII bytes
    void fcc_le(uint32_t v) noexcept { u32le(v); } // fourcc already packed with detail::fcc


    void set8(uint32_t at, uint32_t v) noexcept { if (at < size()) b_[at] = (uint8_t)v; }


    void set_u32le(uint32_t at, uint32_t v) noexcept { for (int i = 0; i < 4; ++i) set8(at + i, v >> (8 * i)); }
    void set_u32be(uint32_t at, uint32_t v) noexcept { for (int i = 0; i < 4; ++i) set8(at + i, v >> (24 - 8 * i)); }
    void set_u64be(uint32_t at, uint64_t v) noexcept { for (int i = 0; i < 8; ++i) set8(at + i, (uint32_t)(v >> (56 - 8 * i))); }

    // ---- ISO BMFF boxes ----
    uint32_t box_begin(const char* type) noexcept { uint32_t p = size(); u32be(0); tag(type); return p; }
    void box_end(uint32_t p) noexcept { set_u32be(p, size() - p); }
    uint32_t fullbox_begin(const char* type, uint32_t version, uint32_t flags) noexcept {
        uint32_t p = box_begin(type); u8(version); u24be(flags); return p;
    }

    // ---- EBML ----
    void ebml_id(uint32_t id) noexcept {
        if (id > 0xFFFFFF) u8(id >> 24);
        if (id > 0xFFFF)   u8(id >> 16);
        if (id > 0xFF)     u8(id >> 8);
        u8(id);
    }
    void ebml_size(uint64_t v) noexcept { // minimal-length vint (never "unknown")
        int n = 1;
        while (n < 8 && v >= ((1ull << (7 * n)) - 1)) ++n;
        uint64_t marked = v | (1ull << (7 * n));
        for (int i = n - 1; i >= 0; --i) u8((uint32_t)(marked >> (8 * i)));
    }
    uint32_t ebml_master_begin(uint32_t id) noexcept { // 8-byte size, patched by ebml_master_end
        ebml_id(id); uint32_t p = size(); u8(0x01); zeros(7); return p;
    }
    void ebml_master_end(uint32_t size_pos) noexcept {
        uint64_t len = size() - (size_pos + 8);
        set8(size_pos, 0x01);
        for (int i = 0; i < 7; ++i) set8(size_pos + 1 + i, (uint32_t)(len >> (48 - 8 * i)));
    }
    void ebml_uint(uint32_t id, uint64_t v) noexcept {
        int n = 1; while (n < 8 && (v >> (8 * n))) ++n;
        ebml_id(id); ebml_size((uint64_t)n);
        for (int i = n - 1; i >= 0; --i) u8((uint32_t)(v >> (8 * i)));
    }
    void ebml_float64(uint32_t id, double d) noexcept {
        uint64_t u; std::memcpy(&u, &d, 8); ebml_id(id); ebml_size(8); u64be(u);
    }
    void ebml_str(uint32_t id, const char* s) noexcept {
        size_t n = std::strlen(s); ebml_id(id); ebml_size(n); put(s, n);
    }
    void ebml_bin(uint32_t id, const void* p, size_t n) noexcept { ebml_id(id); ebml_size(n); put(p, n); }
private:
    knst_byte_string b_;
    bool oom_ = false;
};

// ---- bounds-checked reader over memory ----
class cursor {
public:
    cursor(const uint8_t* p, size_t n) noexcept : p_(p), n_(n) {}
    bool bad() const noexcept { return bad_; }
    size_t pos() const noexcept { return pos_; }
    size_t left() const noexcept { return n_ - pos_; }
    const uint8_t* ptr() const noexcept { return p_ + pos_; }
    bool skip(size_t k) noexcept { if (k > left()) { bad_ = true; pos_ = n_; return false; } pos_ += k; return true; }
    uint8_t u8() noexcept { if (left() < 1) { bad_ = true; return 0; } return p_[pos_++]; }
    uint16_t u16le() noexcept { if (left() < 2) { bad_ = true; pos_ = n_; return 0; } uint16_t v = get_u16le(p_ + pos_); pos_ += 2; return v; }
    uint32_t u32le() noexcept { if (left() < 4) { bad_ = true; pos_ = n_; return 0; } uint32_t v = get_u32le(p_ + pos_); pos_ += 4; return v; }
    uint64_t u64le() noexcept { if (left() < 8) { bad_ = true; pos_ = n_; return 0; } uint64_t v = get_u64le(p_ + pos_); pos_ += 8; return v; }
    uint16_t u16be() noexcept { if (left() < 2) { bad_ = true; pos_ = n_; return 0; } uint16_t v = get_u16be(p_ + pos_); pos_ += 2; return v; }
    uint32_t u32be() noexcept { if (left() < 4) { bad_ = true; pos_ = n_; return 0; } uint32_t v = get_u32be(p_ + pos_); pos_ += 4; return v; }
    uint64_t u64be() noexcept { if (left() < 8) { bad_ = true; pos_ = n_; return 0; } uint64_t v = get_u64be(p_ + pos_); pos_ += 8; return v; }
private:
    const uint8_t* p_; size_t n_; size_t pos_ = 0; bool bad_ = false;
};

// ---- file with 64-bit offsets, tracked position, RAII ----
class file_handle {
public:
    enum class mode : uint8_t { read, write, update };

    file_handle() noexcept = default;
    ~file_handle() { close(); }
    file_handle(const file_handle&) = delete;
    file_handle& operator=(const file_handle&) = delete;
    file_handle(file_handle&& o) noexcept { steal(o); }
    file_handle& operator=(file_handle&& o) noexcept { if (this != &o) { close(); steal(o); } return *this; }

    errc open(const knst_c16string& path, mode m) noexcept {
        close();
        if (path.empty()) return errc::InvalidArgument;
        const char* cm = (m == mode::read) ? "rb" : (m == mode::write ? "wb" : "r+b");
#if KNST_VID_WINDOWS
        wchar_t wm[4] = {0, 0, 0, 0};
        for (int i = 0; cm[i] && i < 3; ++i) wm[i] = (wchar_t)cm[i];
        f_ = ::_wfopen(reinterpret_cast<const wchar_t*>(path.data()), wm);
#else
        knst_byte_string u8p(path);
        if (u8p.empty()) return errc::InvalidArgument;
        u8p.push_back(0); // guarantee a C string regardless of the container's own terminator
        f_ = std::fopen(reinterpret_cast<const char*>(u8p.data()), cm);
#endif
        if (!f_) return m == mode::read ? errc::NotFound : errc::IoError;
        std::setvbuf(f_, nullptr, _IOFBF, KNST_VIDEO_FILE_BUFFER);
        pos_ = 0; end_ = 0;
        if (m == mode::read || m == mode::update) {
            if (!raw_seek(0, SEEK_END)) { close(); return errc::IoError; }
            int64_t e = raw_tell();
            if (e < 0) { close(); return errc::IoError; }
            end_ = (uint64_t)e;
            if (!raw_seek(0, SEEK_SET)) { close(); return errc::IoError; }
        }
        return errc::Ok;
    }
    bool is_open() const noexcept { return f_ != nullptr; }
    uint64_t tell() const noexcept { return pos_; }
    uint64_t size() const noexcept { return end_; }
    bool bad() const noexcept { return failed_; }

    size_t read(void* dst, size_t n) noexcept {
        if (!f_ || n == 0) return 0;
        size_t g = std::fread(dst, 1, n, f_);
        pos_ += g;
        if (g != n && std::ferror(f_)) failed_ = true;
        return g;
    }
    bool read_exact(void* dst, size_t n) noexcept { return read(dst, n) == n; }
    bool write(const void* src, size_t n) noexcept {
        if (!f_) return false;
        if (n == 0) return true;
        if (std::fwrite(src, 1, n, f_) != n) { failed_ = true; return false; }
        pos_ += n;
        if (pos_ > end_) end_ = pos_;
        return true;
    }
    bool seek(uint64_t p) noexcept {
        if (!f_) return false;
        if (p == pos_) return true;
        if (!raw_seek((int64_t)p, SEEK_SET)) { failed_ = true; return false; }
        pos_ = p; return true;
    }
    bool seek_end() noexcept { return seek(end_); }
    bool flush() noexcept { return f_ && std::fflush(f_) == 0; }
    // returns false if any earlier I/O operation failed (including delayed write errors)
    bool close() noexcept {
        bool ok = !failed_;
        if (f_) { if (std::fclose(f_) != 0) ok = false; f_ = nullptr; }
        pos_ = 0; failed_ = false;
        return ok;
    }
private:
    std::FILE* f_ = nullptr;
    uint64_t pos_ = 0, end_ = 0;
    bool failed_ = false;

    void steal(file_handle& o) noexcept { f_ = o.f_; pos_ = o.pos_; end_ = o.end_; failed_ = o.failed_; o.f_ = nullptr; o.pos_ = 0; o.end_ = 0; o.failed_ = false; }
    bool raw_seek(int64_t off, int whence) noexcept {
#if KNST_VID_WINDOWS
        return ::_fseeki64(f_, off, whence) == 0;
#else
        return ::fseeko(f_, (off_t)off, whence) == 0;
#endif
    }
    int64_t raw_tell() noexcept {
#if KNST_VID_WINDOWS
        return (int64_t)::_ftelli64(f_);
#else
        return (int64_t)::ftello(f_);
#endif
    }
};

} // namespace detail

// ***************************************************************
// Color conversion (fixed-point, no floats in the hot loops)
// ***************************************************************

namespace color {

struct yuv_params {
    bool full_range = false;  // false: Y 16..235, UV 16..240 (video / camera); true: 0..255 (JPEG)
    bool bt709 = false;  // false: BT.601 (SD / webcams); true: BT.709 (HD)
};

namespace detail_ {
struct inv_coef { int y_off, y_mul, rv, gu, gv, bu; };
struct fwd_coef { int yr, yg, yb, ur, ug, ub, vr, vg, vb, y_off; };

inline inv_coef make_inv(const yuv_params& p) noexcept {
    inv_coef c;
    if (p.full_range) {
        c.y_off = 0; c.y_mul = 65536;
        if (!p.bt709) { c.rv = 91881;  c.gu = 22554; c.gv = 46802; c.bu = 116130; }
        else { c.rv = 103206; c.gu = 12276; c.gv = 30679; c.bu = 121609; }
    } else {
        c.y_off = 16; c.y_mul = 76284;
        if (!p.bt709) { c.rv = 104595; c.gu = 25690; c.gv = 53281; c.bu = 132186; }
        else { c.rv = 117506; c.gu = 13959; c.gv = 34931; c.bu = 138412; }
    }
    return c;
}
inline fwd_coef make_fwd(const yuv_params& p) noexcept {
    if (p.full_range) {
        if (!p.bt709) return { 19595, 38470, 7471, -11059, -21709, 32768, 32768, -27439, -5329, 0 };
        return { 13933, 46871, 4732, -7511, -25258, 32768, 32768, -29767, -3001, 0 };
    }
    if (!p.bt709) return { 16843, 33030, 6423, -9699, -19071, 28770, 28770, -24117, -4653, 16 };
    return { 11966, 40254, 4063, -6593, -22190, 28784, 28784, -26143, -2641, 16 };
}
inline void yuv_px(const inv_coef& c, int y, int u, int v, uint8_t* o) noexcept {
    int yy = (y - c.y_off) * c.y_mul;
    int d = u - 128, e = v - 128;
    o[0] = ::knst_vid::detail::clamp8((yy + c.rv * e + 32768) >> 16);
    o[1] = ::knst_vid::detail::clamp8((yy - c.gu * d - c.gv * e + 32768) >> 16);
    o[2] = ::knst_vid::detail::clamp8((yy + c.bu * d + 32768) >> 16);
}
} // namespace detail_

// Demosaic (bilinear, 3x3 neighbourhood). Pattern letters = colours of pixels (0,0),(1,0),(0,1),(1,1).
inline errc bayer_to_rgb24(const uint8_t* src, uint32_t w, uint32_t h, uint32_t stride, pixel_format pat,knst_byte_string& dst) noexcept {
    // site colour (0=R,1=G,2=B) at (x&1, y&1)
    static const uint8_t site[4][4] = {
        /* BGGR */ {2, 1, 1, 0}, /* GBRG */ {1, 2, 0, 1}, /* GRBG */ {1, 0, 2, 1}, /* RGGB */ {0, 1, 1, 2} };
    int pi = (int)pat - (int)pixel_format::Bayer_BGGR;
    if (pi < 0 || pi > 3 || !src || w == 0 || h == 0) return errc::InvalidArgument;
    if (stride == 0) stride = w;
    errc e = detail::alloc_bytes(dst, (uint64_t)w * h * 3);
    if (e != errc::Ok) return e;
    uint8_t* d = &dst[0];
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            unsigned sum[3] = {0, 0, 0}, cnt[3] = {0, 0, 0};
            for (int dy = -1; dy <= 1; ++dy) {
                int yy = (int)y + dy; if (yy < 0) yy = 0; if (yy >= (int)h) yy = (int)h - 1;
                for (int dx = -1; dx <= 1; ++dx) {
                    int xx = (int)x + dx; if (xx < 0) xx = 0; if (xx >= (int)w) xx = (int)w - 1;
                    int c = site[pi][((yy & 1) << 1) | (xx & 1)];
                    sum[c] += src[(size_t)yy * stride + xx]; ++cnt[c];
                }
            }
            uint8_t* o = d + ((size_t)y * w + x) * 3;
            for (int c = 0; c < 3; ++c) o[c] = (uint8_t)(cnt[c] ? (sum[c] + cnt[c] / 2) / cnt[c] : 0);
        }
    }
    return errc::Ok;
}

// Any supported pixel format -> tightly packed RGB24. stride == 0 -> tight (luma stride for planar formats).
inline errc convert_to_rgb24(const uint8_t* src, pixel_format pix, uint32_t w, uint32_t h, uint32_t stride,knst_byte_string& dst, const yuv_params& yp = yuv_params()) noexcept {
    if (!src) return errc::InvalidArgument;
    if (pix >= pixel_format::Bayer_BGGR && pix <= pixel_format::Bayer_RGGB) return bayer_to_rgb24(src, w, h, stride, pix, dst);
    plane_layout L;
    if (!compute_layout(pix, w, h, stride, L)) return errc::InvalidArgument;
    errc e = detail::alloc_bytes(dst, (uint64_t)w * h * 3);
    if (e != errc::Ok) return e;
    uint8_t* d = &dst[0];
    const detail_::inv_coef c = detail_::make_inv(yp);

    switch (pix) {
    case pixel_format::RGB24:
        for (uint32_t y = 0; y < h; ++y) std::memcpy(d + (size_t)y * w * 3, src + (size_t)y * L.stride[0], (size_t)w * 3);
        break;
    case pixel_format::BGR24:
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = src + (size_t)y * L.stride[0]; uint8_t* o = d + (size_t)y * w * 3;
            for (uint32_t x = 0; x < w; ++x, s += 3, o += 3) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; }
        }
        break;
    case pixel_format::RGBA32: case pixel_format::BGRA32: {
        const bool bgr = (pix == pixel_format::BGRA32);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = src + (size_t)y * L.stride[0]; uint8_t* o = d + (size_t)y * w * 3;
            for (uint32_t x = 0; x < w; ++x, s += 4, o += 3) {
                if (bgr) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; } else { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; }
            }
        }
        break;
    }
    case pixel_format::GREY:
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = src + (size_t)y * L.stride[0]; uint8_t* o = d + (size_t)y * w * 3;
            for (uint32_t x = 0; x < w; ++x, o += 3) { o[0] = o[1] = o[2] = s[x]; }
        }
        break;

    case pixel_format::YUYV: case pixel_format::UYVY: case pixel_format::YVYU: {
        const int iy0 = (pix == pixel_format::UYVY) ? 1 : 0;
        const int iu  = (pix == pixel_format::UYVY) ? 0 : (pix == pixel_format::YUYV ? 1 : 3);
        const int iv  = (pix == pixel_format::UYVY) ? 2 : (pix == pixel_format::YUYV ? 3 : 1);
        const int iy1 = (pix == pixel_format::UYVY) ? 3 : 2;
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = src + (size_t)y * L.stride[0]; uint8_t* o = d + (size_t)y * w * 3;
            for (uint32_t x = 0; x < w; x += 2, s += 4) {
                detail_::yuv_px(c, s[iy0], s[iu], s[iv], o); o += 3;
                if (x + 1 < w) { detail_::yuv_px(c, s[iy1], s[iu], s[iv], o); o += 3; }
            }
        }
        break;
    }
    case pixel_format::NV12: case pixel_format::NV21: {
        const bool nv21 = (pix == pixel_format::NV21);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* ys = src + (size_t)y * L.stride[0];
            const uint8_t* uv = src + L.offset[1] + (size_t)(y >> 1) * L.stride[1];
            uint8_t* o = d + (size_t)y * w * 3;
            for (uint32_t x = 0; x < w; ++x, o += 3) {
                const uint8_t* p = uv + (x & ~1u);
                detail_::yuv_px(c, ys[x], nv21 ? p[1] : p[0], nv21 ? p[0] : p[1], o);
            }
        }
        break;
    }

    case pixel_format::I420: case pixel_format::YV12: {
        const bool yv = (pix == pixel_format::YV12);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* ys = src + (size_t)y * L.stride[0];
            const uint8_t* up = src + L.offset[yv ? 2 : 1] + (size_t)(y >> 1) * L.stride[1];
            const uint8_t* vp = src + L.offset[yv ? 1 : 2] + (size_t)(y >> 1) * L.stride[1];
            uint8_t* o = d + (size_t)y * w * 3;
            for (uint32_t x = 0; x < w; ++x, o += 3) detail_::yuv_px(c, ys[x], up[x >> 1], vp[x >> 1], o);
        }
        break;
    }
    default: return errc::NotSupported;
    }
    return errc::Ok;
}

// Tightly packed RGB24 -> any pixel format (except MJPEG / Bayer). dst gets the layout of compute_layout(.., stride 0).
inline errc convert_from_rgb24(const uint8_t* rgb, uint32_t w, uint32_t h, uint32_t src_stride, pixel_format to,knst_byte_string& dst, const yuv_params& yp = yuv_params()) noexcept {
    if (!rgb || w == 0 || h == 0) return errc::InvalidArgument;
    if (src_stride == 0) src_stride = w * 3;
    if (src_stride < w * 3) return errc::InvalidArgument;
    plane_layout L;
    if (!compute_layout(to, w, h, 0, L)) return errc::NotSupported;
    errc e = detail::alloc_bytes(dst, L.total);
    if (e != errc::Ok) return e;


    uint8_t* d = &dst[0];


    const detail_::fwd_coef f = detail_::make_fwd(yp);


    auto Y = [&](const uint8_t* p) { return detail::clamp8(((f.yr * p[0] + f.yg * p[1] + f.yb * p[2] + 32768) >> 16) + f.y_off); };
    auto U = [&](const uint8_t* p) { return detail::clamp8(((f.ur * p[0] + f.ug * p[1] + f.ub * p[2] + 32767) >> 16) + 128); };
    auto V = [&](const uint8_t* p) { return detail::clamp8(((f.vr * p[0] + f.vg * p[1] + f.vb * p[2] + 32767) >> 16) + 128); };

    switch (to) {
    case pixel_format::RGB24:
        for (uint32_t y = 0; y < h; ++y) std::memcpy(d + (size_t)y * L.stride[0], rgb + (size_t)y * src_stride, (size_t)w * 3);
        break;
    case pixel_format::BGR24: case pixel_format::RGBA32: case pixel_format::BGRA32: {
        const bool swap = (to != pixel_format::RGBA32);
        const int n = (to == pixel_format::BGR24) ? 3 : 4;
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = rgb + (size_t)y * src_stride; uint8_t* o = d + (size_t)y * L.stride[0];
            for (uint32_t x = 0; x < w; ++x, s += 3, o += n) {
                o[0] = swap ? s[2] : s[0]; o[1] = s[1]; o[2] = swap ? s[0] : s[2]; if (n == 4) o[3] = 255;
            }
        }
        break;
    }

    case pixel_format::GREY:
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = rgb + (size_t)y * src_stride; uint8_t* o = d + (size_t)y * L.stride[0];
            for (uint32_t x = 0; x < w; ++x, s += 3) o[x] = Y(s);
        }
        break;
    case pixel_format::YUYV: case pixel_format::UYVY: case pixel_format::YVYU: {
        const int iy0 = (to == pixel_format::UYVY) ? 1 : 0, iy1 = (to == pixel_format::UYVY) ? 3 : 2;
        const int iu = (to == pixel_format::UYVY) ? 0 : (to == pixel_format::YUYV ? 1 : 3);
        const int iv = (to == pixel_format::UYVY) ? 2 : (to == pixel_format::YUYV ? 3 : 1);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = rgb + (size_t)y * src_stride; uint8_t* o = d + (size_t)y * L.stride[0];
            for (uint32_t x = 0; x < w; x += 2, o += 4) {
                const uint8_t* p0 = s + (size_t)x * 3; const uint8_t* p1 = (x + 1 < w) ? p0 + 3 : p0;
                o[iy0] = Y(p0); o[iy1] = Y(p1);
                o[iu] = (uint8_t)((U(p0) + U(p1) + 1) >> 1); o[iv] = (uint8_t)((V(p0) + V(p1) + 1) >> 1);
            }
        }
        break;
    }
    case pixel_format::NV12: case pixel_format::NV21: case pixel_format::I420: case pixel_format::YV12: {
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* s = rgb + (size_t)y * src_stride; uint8_t* o = d + (size_t)y * L.stride[0];
            for (uint32_t x = 0; x < w; ++x, s += 3) o[x] = Y(s);
        }
        const bool semi = (to == pixel_format::NV12 || to == pixel_format::NV21);
        const bool vfirst = (to == pixel_format::NV21 || to == pixel_format::YV12);
        const uint32_t cw = L.width[1] / (semi ? 2 : 1), ch = L.height[1];
        for (uint32_t cy = 0; cy < ch; ++cy) {
            for (uint32_t cx = 0; cx < cw; ++cx) {
                int su = 0, sv = 0, n = 0;
                for (uint32_t dy = 0; dy < 2; ++dy) for (uint32_t dx = 0; dx < 2; ++dx) {
                    uint32_t yy = cy * 2 + dy, xx = cx * 2 + dx;
                    if (yy >= h || xx >= w) continue;
                    const uint8_t* p = rgb + (size_t)yy * src_stride + (size_t)xx * 3;
                    su += U(p); sv += V(p); ++n;
                }
                uint8_t u = (uint8_t)((su + n / 2) / n), v = (uint8_t)((sv + n / 2) / n);
                if (semi) {
                    uint8_t* o = d + L.offset[1] + (size_t)cy * L.stride[1] + (size_t)cx * 2;
                    o[0] = vfirst ? v : u; o[1] = vfirst ? u : v;
                } else {
                    d[L.offset[vfirst ? 2 : 1] + (size_t)cy * L.stride[1] + cx] = u;
                    d[L.offset[vfirst ? 1 : 2] + (size_t)cy * L.stride[2] + cx] = v;
                }
            }
        }
        break;
    }
    default: return errc::NotSupported;
    }
    return errc::Ok;
}

} // namespace color

// ============================================================
// Image processing (RGB24, tightly packed unless a stride is given)
// ============================================================

namespace img {

enum class resize_filter : uint8_t { nearest, bilinear, box };

inline result<knst_byte_string> resize_rgb24(const uint8_t* src, uint32_t sw, uint32_t sh,
                                              uint32_t dw, uint32_t dh,
                                              resize_filter f = resize_filter::bilinear) noexcept {
    using R = result<knst_byte_string>;
    if (!src || sw == 0 || sh == 0 || dw == 0 || dh == 0) return R::fail(status::InvalidArgument);
    if (sw > KNST_VIDEO_MAX_DIMENSION || sh > KNST_VIDEO_MAX_DIMENSION || dw > KNST_VIDEO_MAX_DIMENSION || dh > KNST_VIDEO_MAX_DIMENSION)
        return R::fail(status::InvalidArgument);
    knst_byte_string out;
    if (detail::alloc_bytes(out, (uint64_t)dw * dh * 3) != errc::Ok) return R::fail(status::OutOfMemory);
    uint8_t* d = &out[0];

    if (dw == sw && dh == sh) {
        std::memcpy(d, src, (size_t)dw * dh * 3);
        return R::success(std::move(out));
    }

    if (f == resize_filter::nearest) {
        for (uint32_t dy = 0; dy < dh; ++dy) {
            const uint8_t* s = src + (size_t)((uint64_t)dy * sh / dh) * sw * 3;
            uint8_t* o = d + (size_t)dy * dw * 3;
            for (uint32_t dx = 0; dx < dw; ++dx, o += 3) {
                const uint8_t* p = s + (size_t)((uint64_t)dx * sw / dw) * 3;
                o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
            }
        }
        return R::success(std::move(out));
    }

    if (f == resize_filter::box && (dw < sw || dh < sh)) {
        // Area average with integer cell edges; one pass over the source.
        knst_vector<uint32_t> acc; acc.resize(sw * 3);
        knst_vector<uint32_t> xs; xs.resize(dw + 1);
        for (uint32_t dx = 0; dx <= dw; ++dx) xs[dx] = (uint32_t)((uint64_t)dx * sw / dw);
        for (uint32_t dy = 0; dy < dh; ++dy) {
            uint32_t y0 = (uint32_t)((uint64_t)dy * sh / dh), y1 = (uint32_t)((uint64_t)(dy + 1) * sh / dh);
            if (y1 <= y0) y1 = y0 + 1;
            if (y1 > sh) y1 = sh;
            for (uint32_t i = 0; i < sw * 3; ++i) acc[i] = 0;
            for (uint32_t y = y0; y < y1; ++y) {
                const uint8_t* s = src + (size_t)y * sw * 3;
                for (uint32_t i = 0; i < sw * 3; ++i) acc[i] += s[i];
            }

            const uint32_t rows = y1 - y0;
            uint8_t* o = d + (size_t)dy * dw * 3;
            for (uint32_t dx = 0; dx < dw; ++dx, o += 3) {
                uint32_t x0 = xs[dx], x1 = xs[dx + 1];
                if (x1 <= x0) x1 = x0 + 1;
                if (x1 > sw) x1 = sw;
                uint64_t n = (uint64_t)rows * (x1 - x0);
                uint64_t r = 0, g = 0, b = 0;
                for (uint32_t x = x0; x < x1; ++x) { r += acc[x * 3]; g += acc[x * 3 + 1]; b += acc[x * 3 + 2]; }
                o[0] = (uint8_t)((r + n / 2) / n); o[1] = (uint8_t)((g + n / 2) / n); o[2] = (uint8_t)((b + n / 2) / n);
            }
        }
        return R::success(std::move(out));
    }

    // Bilinear, 8-bit fractional weights, pixel-centre aligned.
    knst_vector<uint32_t> x0; x0.resize(dw);
    knst_vector<uint32_t> x1; x1.resize(dw);
    knst_vector<uint32_t> wx; wx.resize(dw);
    for (uint32_t dx = 0; dx < dw; ++dx) {
        int64_t fx = ((int64_t)(2 * dx + 1) * sw * 128) / dw - 128;   // (dx+0.5)*sw/dw-0.5 in 1/256
        if (fx < 0) fx = 0;
        int64_t maxfx = (int64_t)(sw - 1) * 256;
        if (fx > maxfx) fx = maxfx;
        uint32_t i0 = (uint32_t)(fx >> 8);
        x0[dx] = i0; x1[dx] = (i0 + 1 < sw) ? i0 + 1 : i0; wx[dx] = (uint32_t)(fx & 255);
    }

    for (uint32_t dy = 0; dy < dh; ++dy) {
        int64_t fy = ((int64_t)(2 * dy + 1) * sh * 128) / dh - 128;
        if (fy < 0) fy = 0;
        int64_t maxfy = (int64_t)(sh - 1) * 256;
        if (fy > maxfy) fy = maxfy;
        uint32_t y0 = (uint32_t)(fy >> 8), y1 = (y0 + 1 < sh) ? y0 + 1 : y0, wy = (uint32_t)(fy & 255);
        const uint8_t* r0 = src + (size_t)y0 * sw * 3;
        const uint8_t* r1 = src + (size_t)y1 * sw * 3;
        uint8_t* o = d + (size_t)dy * dw * 3;
        for (uint32_t dx = 0; dx < dw; ++dx, o += 3) {
            const uint32_t a = x0[dx] * 3, b = x1[dx] * 3, w1 = wx[dx], w0 = 256 - w1, v1 = wy, v0 = 256 - wy;
            for (int c = 0; c < 3; ++c) {
                uint32_t top = r0[a + c] * w0 + r0[b + c] * w1;
                uint32_t bot = r1[a + c] * w0 + r1[b + c] * w1;
                o[c] = (uint8_t)((top * v0 + bot * v1 + 32768) >> 16);
            }
        }
    }
    return R::success(std::move(out));
}

inline result<knst_byte_string> crop_rgb24(const uint8_t* src, uint32_t sw, uint32_t sh,
                                            uint32_t x, uint32_t y, uint32_t cw, uint32_t ch) noexcept {
    using R = result<knst_byte_string>;
    if (!src || sw == 0 || sh == 0 || cw == 0 || ch == 0) return R::fail(status::InvalidArgument);
    if ((uint64_t)x + cw > sw || (uint64_t)y + ch > sh) return R::fail(status::InvalidArgument);
    knst_byte_string out;
    if (detail::alloc_bytes(out, (uint64_t)cw * ch * 3) != errc::Ok) return R::fail(status::OutOfMemory);
    for (uint32_t row = 0; row < ch; ++row)
        std::memcpy(&out[(size_t)row * cw * 3], src + ((size_t)(y + row) * sw + x) * 3, (size_t)cw * 3);
    return R::success(std::move(out));
}

inline result<knst_byte_string> rotate_90_rgb24(const uint8_t* src, uint32_t sw, uint32_t sh, bool clockwise) noexcept {
    using R = result<knst_byte_string>;
    if (!src || sw == 0 || sh == 0) return R::fail(status::InvalidArgument);
    const uint32_t dw = sh, dh = sw;
    knst_byte_string out;
    if (detail::alloc_bytes(out, (uint64_t)dw * dh * 3) != errc::Ok) return R::fail(status::OutOfMemory);
    uint8_t* d = &out[0];
    for (uint32_t y = 0; y < sh; ++y) {
        const uint8_t* s = src + (size_t)y * sw * 3;
        for (uint32_t x = 0; x < sw; ++x, s += 3) {
            uint32_t dx = clockwise ? (sh - 1 - y) : y;
            uint32_t dy = clockwise ? x : (sw - 1 - x);
            uint8_t* o = d + ((size_t)dy * dw + dx) * 3;
            o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
        }
    }
    return R::success(std::move(out));
}

inline result<knst_byte_string> rotate_180_rgb24(const uint8_t* src, uint32_t w, uint32_t h) noexcept {
    using R = result<knst_byte_string>;
    if (!src || w == 0 || h == 0) return R::fail(status::InvalidArgument);
    knst_byte_string out;
    if (detail::alloc_bytes(out, (uint64_t)w * h * 3) != errc::Ok) return R::fail(status::OutOfMemory);
    uint8_t* d = &out[0];
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i) { const uint8_t* s = src + i * 3; uint8_t* o = d + (n - 1 - i) * 3; o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; }
    return R::success(std::move(out));
}

inline result<knst_byte_string> flip_h_rgb24(const uint8_t* src, uint32_t w, uint32_t h) noexcept {
    using R = result<knst_byte_string>;
    if (!src || w == 0 || h == 0) return R::fail(status::InvalidArgument);
    knst_byte_string out;
    if (detail::alloc_bytes(out, (uint64_t)w * h * 3) != errc::Ok) return R::fail(status::OutOfMemory);
    uint8_t* d = &out[0];
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* s = src + (size_t)y * w * 3; uint8_t* o = d + (size_t)y * w * 3 + (size_t)(w - 1) * 3;
        for (uint32_t x = 0; x < w; ++x, s += 3, o -= 3) { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; }
    }
    return R::success(std::move(out));
}

inline result<knst_byte_string> flip_v_rgb24(const uint8_t* src, uint32_t w, uint32_t h) noexcept {
    using R = result<knst_byte_string>;
    if (!src || w == 0 || h == 0) return R::fail(status::InvalidArgument);
    knst_byte_string out;
    if (detail::alloc_bytes(out, (uint64_t)w * h * 3) != errc::Ok) return R::fail(status::OutOfMemory);
    for (uint32_t y = 0; y < h; ++y) std::memcpy(&out[(size_t)y * w * 3], src + (size_t)(h - 1 - y) * w * 3, (size_t)w * 3);
    return R::success(std::move(out));
}

inline result<knst_byte_string> bgr_to_rgb24(const uint8_t* src, uint32_t w, uint32_t h) noexcept {
    using R = result<knst_byte_string>;
    if (!src || w == 0 || h == 0) return R::fail(status::InvalidArgument);
    knst_byte_string out;
    if (detail::alloc_bytes(out, (uint64_t)w * h * 3) != errc::Ok) return R::fail(status::OutOfMemory);
    uint8_t* d = &out[0];
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i) { d[i * 3] = src[i * 3 + 2]; d[i * 3 + 1] = src[i * 3 + 1]; d[i * 3 + 2] = src[i * 3]; }
    return R::success(std::move(out));
}

// In-place: out = clamp((in-128)*contrast + 128 + brightness). contrast 1.0 = unchanged, brightness -255..255.
inline errc adjust_rgb24(uint8_t* px, uint32_t w, uint32_t h, float brightness, float contrast) noexcept {
    if (!px || w == 0 || h == 0) return errc::InvalidArgument;
    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) {
        float v = ((float)i - 128.f) * contrast + 128.f + brightness;
        lut[i] = detail::clamp8((int)(v < 0 ? v - 0.5f : v + 0.5f));
    }
    const size_t n = (size_t)w * h * 3;
    for (size_t i = 0; i < n; ++i) px[i] = lut[px[i]];
    return errc::Ok;
}

// RGB24 -> GREY (BT.601 luma)
inline result<knst_byte_string> to_grey_rgb24(const uint8_t* src, uint32_t w, uint32_t h) noexcept {
    using R = result<knst_byte_string>;
    if (!src || w == 0 || h == 0) return R::fail(status::InvalidArgument);
    knst_byte_string out;
    if (detail::alloc_bytes(out, (uint64_t)w * h) != errc::Ok) return R::fail(status::OutOfMemory);
    uint8_t* d = &out[0];
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i) d[i] = (uint8_t)((19595u * src[i * 3] + 38470u * src[i * 3 + 1] + 7471u * src[i * 3 + 2] + 32768u) >> 16);
    return R::success(std::move(out));
}

} // namespace img

// ============================================================
// Audio helpers (PCM conversion, mixing, resampling)
// ============================================================

namespace audio {

// Reads sample `i` of any PCM codec as float in [-1, 1].
inline float sample_to_f32(const uint8_t* p, audio_codec c) noexcept {
    switch (c) {
        case audio_codec::PCM_U8:    return ((int)p[0] - 128) * (1.0f / 128.0f);
        case audio_codec::PCM_S16LE: return (int16_t)detail::get_u16le(p) * (1.0f / 32768.0f);
        case audio_codec::PCM_S24LE: { int32_t v = (int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24)) >> 8; return v * (1.0f / 8388608.0f); }
        case audio_codec::PCM_S32LE: return (float)((double)(int32_t)detail::get_u32le(p) / 2147483648.0);
        case audio_codec::PCM_F32LE: { uint32_t u = detail::get_u32le(p); float f; std::memcpy(&f, &u, 4); return f; }
        default: return 0.f;
    }
}

inline void f32_to_sample(float v, uint8_t* p, audio_codec c) noexcept {
    if (!(v == v)) v = 0.f;                    // NaN
    if (v > 1.f) v = 1.f; else if (v < -1.f) v = -1.f;
    switch (c) {
        case audio_codec::PCM_U8:    p[0] = (uint8_t)((int)std::lrintf(v * 127.f) + 128); break;
        case audio_codec::PCM_S16LE: detail::put_u16le(p, (uint16_t)(int16_t)std::lrintf(v * 32767.f)); break;
        case audio_codec::PCM_S24LE: { int32_t s = (int32_t)std::lrintf(v * 8388607.f); p[0] = (uint8_t)s; p[1] = (uint8_t)(s >> 8); p[2] = (uint8_t)(s >> 16); break; }
        case audio_codec::PCM_S32LE: detail::put_u32le(p, (uint32_t)(int32_t)std::llrint((double)v * 2147483647.0)); break;
        case audio_codec::PCM_F32LE: { uint32_t u; std::memcpy(&u, &v, 4); detail::put_u32le(p, u); break; }
        default: break;
    }
}

// Converts interleaved PCM between any two PCM codecs. `count` = number of samples (frames * channels).
inline errc convert_pcm(const uint8_t* src, audio_codec from, uint8_t* dst, audio_codec to, uint64_t count) noexcept {
    if (!src || !dst || !audio_codec_is_pcm(from) || !audio_codec_is_pcm(to)) return errc::NotSupported;
    const uint32_t sb = audio_codec_bits(from) / 8, db = audio_codec_bits(to) / 8;
    for (uint64_t i = 0; i < count; ++i) f32_to_sample(sample_to_f32(src + i * sb, from), dst + i * db, to);
    return errc::Ok;
}

inline result<audio_frame> convert(const audio_frame& in, audio_codec to) noexcept {
    using R = result<audio_frame>;
    if (!audio_codec_is_pcm(in.codec) || !audio_codec_is_pcm(to) || in.channels == 0) return R::fail(status::NotSupported);
    const uint64_t n = (uint64_t)in.frames * in.channels;
    if (in.data.length() < n * (audio_codec_bits(in.codec) / 8)) return R::fail(status::InvalidArgument);
    audio_frame out;
    if (detail::alloc_bytes(out.data, n * (audio_codec_bits(to) / 8)) != errc::Ok) return R::fail(status::OutOfMemory);
    if (n && convert_pcm(in.data.data(), in.codec, &out.data[0], to, n) != errc::Ok) return R::fail(status::NotSupported);
    out.sample_rate = in.sample_rate; out.channels = in.channels; out.codec = to;
    out.frames = in.frames; out.pts_us = in.pts_us; out.valid = true;
    return R::success(std::move(out));
}

// Channel count change: mono->N duplicates, N->mono averages, otherwise first min(N,M) channels are kept / extra are silent.
inline result<audio_frame> remix(const audio_frame& in, uint32_t out_channels) noexcept {
    using R = result<audio_frame>;
    if (!audio_codec_is_pcm(in.codec) || in.channels == 0 || out_channels == 0 || out_channels > 32) return R::fail(status::InvalidArgument);
    const uint32_t bps = audio_codec_bits(in.codec) / 8;
    if (in.data.length() < (uint64_t)in.frames * in.channels * bps) return R::fail(status::InvalidArgument);
    audio_frame out;
    if (detail::alloc_bytes(out.data, (uint64_t)in.frames * out_channels * bps) != errc::Ok) return R::fail(status::OutOfMemory);
    uint8_t* d = out.data.length() ? &out.data[0] : nullptr;
    const uint8_t* s = in.data.data();
    for (uint32_t f = 0; f < in.frames; ++f) {
        const uint8_t* sf = s + (size_t)f * in.channels * bps;
        uint8_t* df = d + (size_t)f * out_channels * bps;
        if (out_channels == 1 && in.channels > 1) {
            float acc = 0.f;
            for (uint32_t c = 0; c < in.channels; ++c) acc += sample_to_f32(sf + c * bps, in.codec);
            f32_to_sample(acc / (float)in.channels, df, in.codec);
        } else {
            for (uint32_t c = 0; c < out_channels; ++c) {
                if (in.channels == 1) std::memcpy(df + c * bps, sf, bps);
                else if (c < in.channels) std::memcpy(df + c * bps, sf + c * bps, bps);
                else std::memset(df + c * bps, in.codec == audio_codec::PCM_U8 ? 128 : 0, bps);
            }
        }
    }
    out.sample_rate = in.sample_rate; out.channels = out_channels; out.codec = in.codec;
    out.frames = in.frames; out.pts_us = in.pts_us; out.valid = true;
    return R::success(std::move(out));
}

// Linear-interpolation resampler (fine for monitoring / speech; use a polyphase filter for mastering).
inline result<audio_frame> resample(const audio_frame& in, uint32_t out_rate) noexcept {
    using R = result<audio_frame>;
    if (!audio_codec_is_pcm(in.codec) || in.sample_rate == 0 || out_rate == 0 || in.channels == 0 || in.frames == 0) return R::fail(status::InvalidArgument);
    const uint32_t bps = audio_codec_bits(in.codec) / 8;
    if (in.data.length() < (uint64_t)in.frames * in.channels * bps) return R::fail(status::InvalidArgument);
    const uint64_t out_frames = detail::muldiv_u64(in.frames, out_rate, in.sample_rate);
    if (out_frames == 0 || out_frames > 0xFFFFFFFFull) return R::fail(status::InvalidArgument);
    audio_frame out;
    if (detail::alloc_bytes(out.data, out_frames * in.channels * bps) != errc::Ok) return R::fail(status::OutOfMemory);
    uint8_t* d = &out.data[0];
    const uint8_t* s = in.data.data();
    for (uint64_t o = 0; o < out_frames; ++o) {
        const uint64_t num = o * in.sample_rate;                  // position in source frames = num / out_rate
        uint64_t i0 = num / out_rate; uint32_t frac = (uint32_t)(((num % out_rate) << 16) / out_rate);
        if (i0 >= in.frames) { i0 = in.frames - 1; frac = 0; }
        const uint64_t i1 = (i0 + 1 < in.frames) ? i0 + 1 : i0;
        for (uint32_t c = 0; c < in.channels; ++c) {
            float a = sample_to_f32(s + ((size_t)i0 * in.channels + c) * bps, in.codec);
            float b = sample_to_f32(s + ((size_t)i1 * in.channels + c) * bps, in.codec);
            f32_to_sample(a + (b - a) * (float)frac * (1.0f / 65536.0f), d + ((size_t)o * in.channels + c) * bps, in.codec);
        }
    }
    out.sample_rate = out_rate; out.channels = in.channels; out.codec = in.codec;
    out.frames = (uint32_t)out_frames; out.pts_us = in.pts_us; out.valid = true;
    return R::success(std::move(out));
}

// Peak level 0..1 of an interleaved PCM block (VU meter, silence detection).
inline float peak(const uint8_t* src, audio_codec c, uint64_t sample_count) noexcept {
    if (!src || !audio_codec_is_pcm(c)) return 0.f;
    const uint32_t b = audio_codec_bits(c) / 8; float m = 0.f;
    for (uint64_t i = 0; i < sample_count; ++i) { float v = std::fabs(sample_to_f32(src + i * b, c)); if (v > m) m = v; }
    return m;
}

} // namespace audio

// ============================================================
// MJPEG codec: baseline JPEG encoder (from scratch) + decoder wrapper
// ============================================================

namespace mjpeg {

enum class subsampling : uint8_t { s444, s422, s420 };

namespace tbl {

inline constexpr uint8_t std_lum_q[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,   12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,   14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
inline constexpr uint8_t std_chr_q[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,   18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,   47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,   99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,   99, 99, 99, 99, 99, 99, 99, 99 };
inline constexpr uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

inline constexpr uint8_t dc_lum_bits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
inline constexpr uint8_t dc_chr_bits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
inline constexpr uint8_t dc_vals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
inline constexpr uint8_t ac_lum_bits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
inline constexpr uint8_t ac_lum_vals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };
inline constexpr uint8_t ac_chr_bits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
inline constexpr uint8_t ac_chr_vals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };

} // namespace tbl

// One DHT segment (marker + length + the 4 standard tables), 420 bytes. dst needs >= 420 bytes. Returns bytes written.
inline uint32_t write_std_dht(uint8_t* dst) noexcept {
    uint8_t* p = dst;
    *p++ = 0xFF; *p++ = 0xC4;
    const uint32_t len = 2 + (17 + 12) + (17 + 162) + (17 + 12) + (17 + 162);
    *p++ = (uint8_t)(len >> 8); *p++ = (uint8_t)len;
    auto put = [&](uint8_t tcth, const uint8_t* bits, const uint8_t* vals, int n) {
        *p++ = tcth; std::memcpy(p, bits, 16); p += 16; std::memcpy(p, vals, (size_t)n); p += n;
    };
    put(0x00, tbl::dc_lum_bits, tbl::dc_vals, 12);
    put(0x10, tbl::ac_lum_bits, tbl::ac_lum_vals, 162);
    put(0x01, tbl::dc_chr_bits, tbl::dc_vals, 12);
    put(0x11, tbl::ac_chr_bits, tbl::ac_chr_vals, 162);
    return (uint32_t)(p - dst);
}

// Reads width / height / component count from the SOF marker without decoding.
inline errc probe(const uint8_t* d, uint32_t n, uint32_t& w, uint32_t& h, uint32_t& comps) noexcept {
    if (!d || n < 4 || d[0] != 0xFF || d[1] != 0xD8) return errc::Corrupt;
    uint32_t p = 2;
    while (p + 4 <= n) {
        if (d[p] != 0xFF) { ++p; continue; }
        uint8_t m = d[p + 1];
        if (m == 0xFF) { ++p; continue; }
        if (m == 0x00 || m == 0x01 || (m >= 0xD0 && m <= 0xD8)) { p += 2; continue; }
        if (m == 0xD9 || m == 0xDA) return errc::Corrupt;
        uint32_t len = detail::get_u16be(d + p + 2);
        if (len < 2 || p + 2 + len > n) return errc::Corrupt;
        if ((m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            if (len < 8) return errc::Corrupt;
            h = detail::get_u16be(d + p + 5); w = detail::get_u16be(d + p + 7); comps = d[p + 9];
            return (w && h) ? errc::Ok : errc::Corrupt;
        }
        p += 2 + len;
    }
    return errc::Corrupt;
}

// Many AVI/MJPEG files (cameras) omit the Huffman tables (they use the standard ones). Returns true and fills `out`
// if tables had to be injected, false if the frame already carries a DHT (or is not a JPEG).
inline bool inject_std_dht(const uint8_t* d, uint32_t n, knst_byte_string& out) noexcept {
    if (!d || n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    uint32_t p = 2;
    while (p + 4 <= n) {
        if (d[p] != 0xFF) { ++p; continue; }
        uint8_t m = d[p + 1];
        if (m == 0xFF) { ++p; continue; }
        if (m == 0xC4) return false;
        if (m == 0xDA) {
            uint8_t dht[432];
            uint32_t k = write_std_dht(dht);
            if (detail::alloc_bytes(out, (uint64_t)n + k) != errc::Ok) return false;
            uint8_t* o = &out[0];
            std::memcpy(o, d, p); std::memcpy(o + p, dht, k); std::memcpy(o + p + k, d + p, n - p);
            return true;
        }
        if (m == 0x00 || m == 0x01 || (m >= 0xD0 && m <= 0xD8)) { p += 2; continue; }
        if (m == 0xD9) return false;
        uint32_t len = detail::get_u16be(d + p + 2);
        if (len < 2) return false;
        p += 2 + len;
    }
    return false;
}

// ---------------- encoder ----------------

class encoder {
public:
    struct options {
        int quality = 85;                         // 1..100 (libjpeg scaling)
        subsampling sub = subsampling::s420;
        uint32_t restart_mcus = 0;                // 0 = no restart markers
        bool jfif = true;
    };
        errc configure(const options& o) noexcept {
        if (o.quality < 1 || o.quality > 100) return errc::InvalidArgument;
        opt_ = o;
        // DRI marker stores a 16-bit count. If the loop uses a raw value
        // >65535 but write_headers clamps to 65535, the decoder sees a
        // different restart interval → corrupt bitstream. Clamp once here.
        if (opt_.restart_mcus > 65535u) opt_.restart_mcus = 65535u;
        const int q = o.quality;
        const int scale = (q < 50) ? 5000 / q : 200 - q * 2;
        for (int i = 0; i < 64; ++i) {
            int a = (tbl::std_lum_q[i] * scale + 50) / 100;
            int b = (tbl::std_chr_q[i] * scale + 50) / 100;
            qy_[i] = (uint8_t)(a < 1 ? 1 : (a > 255 ? 255 : a));
            qc_[i] = (uint8_t)(b < 1 ? 1 : (b > 255 ? 255 : b));
            rcp_y_[i] = 1.0f / (float)(qy_[i] * 8);
            rcp_c_[i] = 1.0f / (float)(qc_[i] * 8);
        }
        build_huff(tbl::dc_lum_bits, tbl::dc_vals, hdc_[0]);
        build_huff(tbl::ac_lum_bits, tbl::ac_lum_vals, hac_[0]);
        build_huff(tbl::dc_chr_bits, tbl::dc_vals, hdc_[1]);
        build_huff(tbl::ac_chr_bits, tbl::ac_chr_vals, hac_[1]);
        configured_ = true;
        return errc::Ok;
    }
    const options& current() const noexcept { return opt_; }

    // src: RGB24 / BGR24 / RGBA32 / BGRA32 / GREY directly; other pixel formats are converted to RGB24 first.
    errc encode(const uint8_t* src, uint32_t w, uint32_t h, uint32_t stride, pixel_format pix,
                knst_byte_string& out) noexcept {
        if (!configured_) { errc e = configure(options()); if (e != errc::Ok) return e; }
        if (!src || w == 0 || h == 0 || w > 65500 || h > 65500 || w > KNST_VIDEO_MAX_DIMENSION || h > KNST_VIDEO_MAX_DIMENSION)
            return errc::InvalidArgument;
        uint32_t bpp = 0;
        switch (pix) {
            case pixel_format::RGB24: case pixel_format::BGR24: bpp = 3; break;
            case pixel_format::RGBA32: case pixel_format::BGRA32: bpp = 4; break;
            case pixel_format::GREY: bpp = 1; break;
            case pixel_format::MJPEG: case pixel_format::Unknown: return errc::InvalidArgument;
            default: {
                errc e = color::convert_to_rgb24(src, pix, w, h, stride, conv_);
                if (e != errc::Ok) return e;
                return encode(conv_.data(), w, h, w * 3, pixel_format::RGB24, out);
            }
        }
        if (stride == 0) stride = w * bpp;
        if (stride < w * bpp) return errc::InvalidArgument;

        const bool grey = (pix == pixel_format::GREY);
        const int hs = grey ? 1 : (opt_.sub == subsampling::s444 ? 1 : 2);
        const int vs = grey ? 1 : (opt_.sub == subsampling::s420 ? 2 : 1);
        const uint32_t mcuw = 8u * hs, mcuh = 8u * vs;
        const uint32_t mcux = (w + mcuw - 1) / mcuw, mcuy = (h + mcuh - 1) / mcuh;
        const uint32_t padw = mcux * mcuw, padh = mcuy * mcuh;
        const uint32_t cw = padw / hs, ch = padh / vs;

        errc e = detail::alloc_bytes(yp_, (uint64_t)padw * padh);
        if (e != errc::Ok) return e;
        if (!grey) {
            if ((e = detail::alloc_bytes(cbp_, (uint64_t)cw * ch)) != errc::Ok) return e;
            if ((e = detail::alloc_bytes(crp_, (uint64_t)cw * ch)) != errc::Ok) return e;
            if ((e = detail::alloc_bytes(row_, (uint64_t)padw * 3)) != errc::Ok) return e;
            if ((e = detail::alloc_bytes(lines_, (uint64_t)padw * 2 * vs)) != errc::Ok) return e;
        }
        uint8_t* Y = &yp_[0];

        // ---- colour transform + chroma subsampling, one MCU row-group at a time ----
        if (grey) {
            for (uint32_t y = 0; y < padh; ++y) {
                const uint8_t* s = src + (size_t)(y < h ? y : h - 1) * stride;
                uint8_t* o = Y + (size_t)y * padw;
                std::memcpy(o, s, w);
                for (uint32_t x = w; x < padw; ++x) o[x] = s[w - 1];
            }
        } else {
            uint8_t* rowbuf = &row_[0];
            uint8_t* cbl = &lines_[0];                       // vs lines of padw Cb, then vs lines of padw Cr
            uint8_t* crl = cbl + (size_t)padw * vs;
            uint8_t* Cb = &cbp_[0]; uint8_t* Cr = &crp_[0];
            const int div = hs * vs;
            for (uint32_t cy = 0; cy < ch; ++cy) {
                for (int r = 0; r < vs; ++r) {
                    const uint32_t y = cy * vs + r;
                    const uint8_t* s = src + (size_t)(y < h ? y : h - 1) * stride;
                    load_rgb_row(s, w, padw, pix, rowbuf);
                    uint8_t* yo = Y + (size_t)y * padw;
                    uint8_t* cbo = cbl + (size_t)r * padw;
                    uint8_t* cro = crl + (size_t)r * padw;
                    for (uint32_t x = 0; x < padw; ++x) {
                        const int R = rowbuf[x * 3], G = rowbuf[x * 3 + 1], B = rowbuf[x * 3 + 2];
                        yo[x]  = (uint8_t)((19595 * R + 38470 * G + 7471 * B + 32768) >> 16);
                        cbo[x] = (uint8_t)((-11059 * R - 21709 * G + 32768 * B + 8421375) >> 16);
                        cro[x] = (uint8_t)((32768 * R - 27439 * G - 5329 * B + 8421375) >> 16);
                    }
                }
                uint8_t* cbo = Cb + (size_t)cy * cw; uint8_t* cro = Cr + (size_t)cy * cw;
                for (uint32_t cx = 0; cx < cw; ++cx) {
                    int sb = 0, sr = 0;
                    for (int r = 0; r < vs; ++r) for (int k = 0; k < hs; ++k) {
                        sb += cbl[(size_t)r * padw + cx * hs + k];
                        sr += crl[(size_t)r * padw + cx * hs + k];
                    }
                    cbo[cx] = (uint8_t)((sb + div / 2) / div);
                    cro[cx] = (uint8_t)((sr + div / 2) / div);
                }
            }
        }

        // ---- output ----
        const uint64_t want = (uint64_t)padw * padh * (grey ? 1 : 3) / 2 + 8192;
        if (scratch_.length() < want) {
            if (detail::alloc_bytes(scratch_, want < 65536 ? 65536 : want) != errc::Ok) return errc::OutOfMemory;
        }
        n_ = 0;
        write_headers(w, h, grey, hs, vs);
        if (oom_) return errc::OutOfMemory;

        bits_ = 0; nbits_ = 0;
        int pred[3] = {0, 0, 0};
        uint32_t mcu_count = 0; uint32_t rst = 0;
        const uint8_t* Cbp = grey ? nullptr : &cbp_[0];
        const uint8_t* Crp = grey ? nullptr : &crp_[0];
        const uint32_t total_mcus = mcux * mcuy;
        for (uint32_t my = 0; my < mcuy; ++my) {
            for (uint32_t mx = 0; mx < mcux; ++mx) {
                if (opt_.restart_mcus && mcu_count && (mcu_count % opt_.restart_mcus) == 0) {
                    flush_bits();
                    ensure(4);
                    sp_[n_++] = 0xFF; sp_[n_++] = (uint8_t)(0xD0 + (rst & 7)); ++rst;
                    pred[0] = pred[1] = pred[2] = 0;
                }
                ++mcu_count;
                for (int v = 0; v < vs; ++v) for (int hh = 0; hh < hs; ++hh) {
                    const uint8_t* blk = Y + (size_t)(my * mcuh + v * 8) * padw + (mx * mcuw + hh * 8);
                    encode_block(blk, padw, rcp_y_, pred[0], hdc_[0], hac_[0]);
                }
                if (!grey) {
                    encode_block(Cbp + (size_t)(my * 8) * cw + mx * 8, cw, rcp_c_, pred[1], hdc_[1], hac_[1]);
                    encode_block(Crp + (size_t)(my * 8) * cw + mx * 8, cw, rcp_c_, pred[2], hdc_[1], hac_[1]);
                }
                if (oom_) return errc::OutOfMemory;
            }
        }
        (void)total_mcus;
        flush_bits();
        ensure(2);
        sp_[n_++] = 0xFF; sp_[n_++] = 0xD9;
        if (oom_) return errc::OutOfMemory;

        if (detail::alloc_bytes(out, n_) != errc::Ok) return errc::OutOfMemory;
        std::memcpy(&out[0], sp_, n_);
        return errc::Ok;
    }

private:
    struct huff { uint16_t code[256]; uint8_t size[256]; };

    options opt_;
    bool configured_ = false;
    uint8_t qy_[64], qc_[64];
    float rcp_y_[64], rcp_c_[64];
    huff hdc_[2], hac_[2];
    knst_byte_string yp_, cbp_, crp_, row_, lines_, scratch_, conv_;
    uint8_t* sp_ = nullptr;
    size_t n_ = 0, cap_ = 0;
    bool oom_ = false;
    uint32_t bits_ = 0; int nbits_ = 0;

    static void build_huff(const uint8_t* bits, const uint8_t* vals, huff& t) noexcept {
        std::memset(&t, 0, sizeof(t));
        uint32_t code = 0; int k = 0;
        for (int l = 1; l <= 16; ++l) {
            for (int i = 0; i < bits[l - 1]; ++i) { t.code[vals[k]] = (uint16_t)code; t.size[vals[k]] = (uint8_t)l; ++k; ++code; }
            code <<= 1;
        }
    }

    static void load_rgb_row(const uint8_t* s, uint32_t w, uint32_t padw, pixel_format pix, uint8_t* o) noexcept {
        if (pix == pixel_format::RGB24) std::memcpy(o, s, (size_t)w * 3);
        else if (pix == pixel_format::BGR24) { for (uint32_t x = 0; x < w; ++x) { o[x*3] = s[x*3+2]; o[x*3+1] = s[x*3+1]; o[x*3+2] = s[x*3]; } }
        else if (pix == pixel_format::RGBA32) { for (uint32_t x = 0; x < w; ++x) { o[x*3] = s[x*4]; o[x*3+1] = s[x*4+1]; o[x*3+2] = s[x*4+2]; } }
        else { for (uint32_t x = 0; x < w; ++x) { o[x*3] = s[x*4+2]; o[x*3+1] = s[x*4+1]; o[x*3+2] = s[x*4]; } }   // BGRA32
        for (uint32_t x = w; x < padw; ++x) { o[x*3] = o[(w-1)*3]; o[x*3+1] = o[(w-1)*3+1]; o[x*3+2] = o[(w-1)*3+2]; }
    }

    void ensure(size_t k) noexcept {
        if (n_ + k <= cap_) return;
        size_t newcap = cap_ * 2 + k + 4096;
        if (newcap > 0xFFFFFF00ull) { oom_ = true; n_ = 0; return; }
        scratch_.resize((uint32_t)newcap);
        if (scratch_.length() != (uint32_t)newcap) { oom_ = true; n_ = 0; return; }
        sp_ = &scratch_[0]; cap_ = newcap;
    }

    inline void put_bits(uint32_t code, int len) noexcept {
        bits_ = (bits_ << len) | code; nbits_ += len;
        while (nbits_ >= 8) {
            uint8_t b = (uint8_t)(bits_ >> (nbits_ - 8));
            sp_[n_++] = b; if (b == 0xFF) sp_[n_++] = 0x00;
            nbits_ -= 8;
        }
        bits_ &= (nbits_ ? ((1u << nbits_) - 1u) : 0u);
    }
    void flush_bits() noexcept {
        if (nbits_ > 0) { ensure(4); if (oom_) return; put_bits((1u << (8 - nbits_)) - 1u, 8 - nbits_); }
        bits_ = 0; nbits_ = 0;
    }

    static void fdct(int* d) noexcept {   // jfdctint (LL&M), output scaled by 8
        enum { CB = 13, P1 = 2 };
        auto DS = [](int x, int n) { return (x + (1 << (n - 1))) >> n; };
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < 8; ++i) {
                int* p = pass == 0 ? d + i * 8 : d + i;
                const int st = pass == 0 ? 1 : 8;
                int t0 = p[0] + p[7 * st], t7 = p[0] - p[7 * st];
                int t1 = p[st] + p[6 * st], t6 = p[st] - p[6 * st];
                int t2 = p[2 * st] + p[5 * st], t5 = p[2 * st] - p[5 * st];
                int t3 = p[3 * st] + p[4 * st], t4 = p[3 * st] - p[4 * st];
                int t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
                if (pass == 0) { p[0] = (t10 + t11) * (1 << P1); p[4 * st] = (t10 - t11) * (1 << P1); }
                else { p[0] = DS(t10 + t11, P1); p[4 * st] = DS(t10 - t11, P1); }
                const int sh = pass == 0 ? CB - P1 : CB + P1;
                int z1 = (t12 + t13) * 4433;
                p[2 * st] = DS(z1 + t13 * 6270, sh);
                p[6 * st] = DS(z1 + t12 * -15137, sh);
                z1 = t4 + t7; int z2 = t5 + t6, z3 = t4 + t6, z4 = t5 + t7;
                int z5 = (z3 + z4) * 9633;
                t4 *= 2446; t5 *= 16819; t6 *= 25172; t7 *= 12299;
                z1 *= -7373; z2 *= -20995; z3 *= -16069; z4 *= -3196;
                z3 += z5; z4 += z5;
                p[7 * st] = DS(t4 + z1 + z3, sh); p[5 * st] = DS(t5 + z2 + z4, sh);
                p[3 * st] = DS(t6 + z2 + z3, sh); p[st]     = DS(t7 + z1 + z4, sh);
            }
        }
    }

    void encode_block(const uint8_t* px, uint32_t stride, const float* rcp, int& pred, const huff& dc, const huff& ac) noexcept {
        ensure(1024);
        if (oom_) return;
        int b[64];
        for (int r = 0; r < 8; ++r) for (int c = 0; c < 8; ++c) b[r * 8 + c] = (int)px[(size_t)r * stride + c] - 128;
        fdct(b);
        int q[64];
        for (int i = 0; i < 64; ++i) {
            const int nat = tbl::zigzag[i];
            float f = (float)b[nat] * rcp[nat];
            q[i] = (int)(f < 0.f ? f - 0.5f : f + 0.5f);
        }
                // DC magnitude is limited to 11 bits by the JPEG standard (max
        // |diff| = 2047). For pathological blocks (e.g. full black then
        // full white at q=100) the DCT can produce larger swings; without
        // this clamp the resulting Huffman symbol (>=12) is missing from
        // the standard table and the bitstream becomes undecodable.
        // `pred` is advanced by the *clamped* diff so the encoder tracks
        // exactly what the decoder will reconstruct.
        int diff = q[0] - pred;
        if (diff > 2047) diff = 2047;
        else if (diff < -2047) diff = -2047;
        pred = pred + diff;
        int ad = diff < 0 ? -diff : diff;
        uint32_t nb = detail::bit_length((uint32_t)ad);
        put_bits(dc.code[nb], dc.size[nb]);
        if (nb) put_bits((uint32_t)(diff < 0 ? diff - 1 : diff) & ((1u << nb) - 1u), (int)nb);
        int run = 0;
        for (int i = 1; i < 64; ++i) {
            int v = q[i];
            // AC coefficients are limited to 10 bits (|v| <= 1023).
            // Clamp before the sign/run encoder so the code stays in table.
            if (v > 1023) v = 1023;
            else if (v < -1023) v = -1023;
            if (v == 0) { ++run; continue; }
            while (run > 15) { put_bits(ac.code[0xF0], ac.size[0xF0]); run -= 16; }
            int av = v < 0 ? -v : v;
            uint32_t s = detail::bit_length((uint32_t)av);
            uint32_t sym = ((uint32_t)run << 4) | s;
            put_bits(ac.code[sym], ac.size[sym]);
            put_bits((uint32_t)(v < 0 ? v - 1 : v) & ((1u << s) - 1u), (int)s);
            run = 0;
        }
        if (run) put_bits(ac.code[0x00], ac.size[0x00]);
    }

    void write_headers(uint32_t w, uint32_t h, bool grey, int hs, int vs) noexcept {
        ensure(2048);
        if (oom_) return;
        auto b = [&](uint32_t v) { sp_[n_++] = (uint8_t)v; };
        auto w16 = [&](uint32_t v) { b(v >> 8); b(v); };
        b(0xFF); b(0xD8);
        if (opt_.jfif) {
            b(0xFF); b(0xE0); w16(16);
            b('J'); b('F'); b('I'); b('F'); b(0); b(1); b(1); b(0); w16(1); w16(1); b(0); b(0);
        }
        for (int t = 0; t < (grey ? 1 : 2); ++t) {
            b(0xFF); b(0xDB); w16(67); b((uint32_t)t);
            const uint8_t* q = t == 0 ? qy_ : qc_;
            for (int i = 0; i < 64; ++i) b(q[tbl::zigzag[i]]);
        }
        const int nc = grey ? 1 : 3;
        b(0xFF); b(0xC0); w16(8 + 3 * nc); b(8); w16(h); w16(w); b((uint32_t)nc);
        b(1); b((uint32_t)((hs << 4) | vs)); b(0);
        if (!grey) { b(2); b(0x11); b(1); b(3); b(0x11); b(1); }
        n_ += write_std_dht(sp_ + n_);
        if (opt_.restart_mcus) { b(0xFF); b(0xDD); w16(4); w16(opt_.restart_mcus > 65535 ? 65535 : opt_.restart_mcus); }
        b(0xFF); b(0xDA); w16(6 + 2 * nc); b((uint32_t)nc);
        b(1); b(0x00);
        if (!grey) { b(2); b(0x11); b(3); b(0x11); }
        b(0); b(63); b(0);
    }
};

// Convenience (allocates a fresh encoder; reuse mjpeg::encoder in loops).
inline result<knst_byte_string> encode_rgb24(const uint8_t* rgb, uint32_t w, uint32_t h, int quality = 85) noexcept {
    using R = result<knst_byte_string>;
    encoder enc; encoder::options o; o.quality = quality;
    errc e = enc.configure(o);
    knst_byte_string out;
    if (e == errc::Ok) e = enc.encode(rgb, w, h, 0, pixel_format::RGB24, out);
    if (e != errc::Ok) return err_result<knst_byte_string>(e);
    return R::success(std::move(out));
}

// ---------------- decoder (knst_image_loader) ----------------

inline result<knst_byte_string> decode_rgb24(const uint8_t* data, uint32_t size, uint32_t& out_w, uint32_t& out_h) noexcept {
    using R = result<knst_byte_string>;
    if (!data || size < 4) return R::fail(status::InvalidArgument);
#if KNST_VIDEO_HAS_IMAGE_LOADER
    knst_byte_string patched;
    const uint8_t* src = data; uint32_t n = size;
    if (inject_std_dht(data, size, patched)) { src = patched.data(); n = patched.length(); }
    knst_image_load_options opt;
    opt.output_format = KNST_BITMAP_OUTPUT_RGB;
    opt.apply_exif_orientation = false;
    opt.max_threads = 1;
    opt.max_dimension = KNST_VIDEO_MAX_DIMENSION;
    opt.allowed_formats = KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_JPEG);
    knst_image img = knst_image_loader::load_from_memory(src, n, opt);
    if (!img.valid()) return R::fail(status::IoError);
    out_w = img.width; out_h = img.height;
    return R::success(std::move(img.pixels));
#else
    (void)out_w; (void)out_h;
    return R::fail(status::NotSupported);
#endif
}

} // namespace mjpeg

// ============================================================
// Shared demuxer model (AVI / MKV / MP4 / WAV / RAW all fill this)
// ============================================================

struct sample_ref {
    uint64_t offset = 0;        // file offset of the payload
    uint64_t pts_us = 0;
    uint64_t dts_us = ~0ull;    // decode time; ~0 = same as pts_us (only MP4 with B-frames differs)
    uint64_t unit_start = 0;    // audio: index of the first PCM frame of this packet (else packet index)
    uint32_t size = 0;
    uint32_t units = 1;         // audio PCM: frames in this packet
    bool key = true;
};

struct track_data {
    stream_desc desc;
    knst_vector<sample_ref> samples;
};

struct demux_base {
    detail::file_handle file;
    knst_vector<track_data> tracks;
    metadata meta;
    uint64_t duration_us = 0;
    container kind = container::Unknown;

    void reset() noexcept { file.close(); tracks.clear(); meta = metadata(); duration_us = 0; kind = container::Unknown; }

    uint32_t add_track(const stream_desc& d) noexcept {
        track_data td; td.desc = d;
        td.desc.index = tracks.size();
        td.desc.is_video = (d.kind == track_kind::Video);
        tracks.push_back(std::move(td));
        return tracks.size() - 1;
    }
    int find_track(track_kind k, uint32_t nth = 0) const noexcept {
        for (uint32_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].desc.kind == k) { if (nth == 0) return (int)i; --nth; }
        return -1;
    }
    errc read_sample(uint32_t t, uint64_t idx, knst_byte_string& out) noexcept {
        if (t >= tracks.size()) return errc::InvalidArgument;
        const track_data& td = tracks[t];
        if (idx >= td.samples.size()) return errc::NotFound;
        const sample_ref& s = td.samples[(uint32_t)idx];
        if (s.size > KNST_VIDEO_MAX_SAMPLE_BYTES) return errc::TooLarge;
        if (s.offset + s.size > file.size()) return errc::Corrupt;
        errc e = detail::alloc_bytes(out, s.size);
        if (e != errc::Ok) return e;
        if (s.size == 0) return errc::Ok;
        if (!file.seek(s.offset) || !file.read_exact(&out[0], s.size)) return errc::IoError;
        return errc::Ok;
    }
    // index of the last packet whose pts <= t (0 if none)
    uint64_t index_at_time(uint32_t t, uint64_t pts_us) const noexcept {
        if (t >= tracks.size() || tracks[t].samples.empty()) return 0;
        const auto& v = tracks[t].samples;
        uint32_t lo = 0, hi = v.size();
        while (lo < hi) { uint32_t mid = lo + (hi - lo) / 2; if (v[mid].pts_us <= pts_us) lo = mid + 1; else hi = mid; }
        return lo ? lo - 1 : 0;
    }
    uint64_t keyframe_at_or_before(uint32_t t, uint64_t idx) const noexcept {
        if (t >= tracks.size() || tracks[t].samples.empty()) return 0;
        const auto& v = tracks[t].samples;
        if (idx >= v.size()) idx = v.size() - 1;
        while (idx > 0 && !v[(uint32_t)idx].key) --idx;
        return idx;
    }
};

// ---- helpers shared by writers ----
struct writer_params {
    bool has_video = true;
    uint32_t width = 0, height = 0;
    uint32_t fps_num = 30, fps_den = 1;
    video_codec vcodec = video_codec::MJPEG;
    pixel_format raw_pix = pixel_format::BGR24;     // AVI RAW payload layout (BGR24 / BGRA32 / GREY)
    uint32_t codec_tag = 0;                         // override fourcc (AVI)
    knst_byte_string vextra;                        // CodecPrivate / avcC / hvcC
    audio_codec acodec = audio_codec::None;
    uint32_t sample_rate = 0, channels = 0;
    knst_byte_string aextra;
    metadata meta;
};

namespace detail {
inline knst_byte_string utf8_of(const knst_c16string& s) noexcept { return knst_byte_string(s); }
inline void read_cstr(const uint8_t* p, size_t n, knst_c16string& out) noexcept {
    size_t k = 0; while (k < n && p[k]) ++k;
    knst_byte_string b(p, (uint32_t)k);
    char* tmp = nullptr; (void)tmp;
    knst_byte_string z = b; z.push_back(0);
    out = knst_c16string(reinterpret_cast<const char*>(z.data()));
}
inline uint32_t parse_year(const knst_byte_string& s) noexcept {
    uint32_t v = 0; int n = 0;
    for (uint32_t i = 0; i < s.length() && n < 4; ++i) { uint8_t c = s[i]; if (c < '0' || c > '9') break; v = v * 10 + (c - '0'); ++n; }
    return n == 4 ? v : 0;
}
} // namespace detail

// ============================================================
// WAV
// ============================================================

namespace wav {

// Writes the body of a WAVEFORMAT(EX/EXTENSIBLE) structure. Used by WAV and AVI.
inline errc write_fmt_body(detail::wbuf& b, audio_codec c, uint32_t rate, uint32_t ch) noexcept {
    const uint32_t bits = audio_codec_bits(c);
    if (!bits || !audio_codec_is_pcm(c)) return errc::NotSupported;
    if (ch < 1 || ch > 32 || rate < 1 || rate > 10000000u) return errc::InvalidArgument;
    const bool flt = (c == audio_codec::PCM_F32LE);
    const bool ext = ch > 2 || (!flt && bits > 16);
    const uint32_t align = ch * bits / 8;
    static const uint32_t masks[9] = {0, 0x4, 0x3, 0x7, 0x33, 0x37, 0x3F, 0x13F, 0x63F};
    b.u16le(ext ? 0xFFFE : (flt ? 3 : 1)); b.u16le(ch); b.u32le(rate); b.u32le(rate * align); b.u16le(align); b.u16le(bits);
    if (ext) {
        b.u16le(22); b.u16le(bits); b.u32le(ch <= 8 ? masks[ch] : 0);
        b.u16le(flt ? 3 : 1);
        static const uint8_t guid_tail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
        b.put(guid_tail, 14);
    } else if (flt) b.u16le(0);
    return b.ok() ? errc::Ok : errc::OutOfMemory;
}

inline audio_codec codec_from_tag(uint32_t tag, uint32_t bits) noexcept {
    if (tag == 1) { if (bits == 8) return audio_codec::PCM_U8; if (bits == 16) return audio_codec::PCM_S16LE; if (bits == 24) return audio_codec::PCM_S24LE; if (bits == 32) return audio_codec::PCM_S32LE; return audio_codec::Unknown; }
    if (tag == 3) return bits == 32 ? audio_codec::PCM_F32LE : audio_codec::Unknown;
    if (tag == 0x55) return audio_codec::MP3;
    if (tag == 0xFF || tag == 0x1610 || tag == 0x1600) return audio_codec::AAC;
    if (tag == 0x704F) return audio_codec::Opus;
    if (tag == 0x6750 || tag == 0x674F || tag == 0x6751) return audio_codec::Vorbis;
    if (tag == 0xF1AC) return audio_codec::FLAC;
    return audio_codec::Unknown;
}

// Parses a WAVEFORMAT body into a stream_desc.
inline bool parse_fmt_body(const uint8_t* p, size_t n, stream_desc& sd) noexcept {
    if (n < 16) return false;
    uint32_t tag = detail::get_u16le(p);
    sd.channels = detail::get_u16le(p + 2); sd.sample_rate = detail::get_u32le(p + 4);
    sd.bitrate = (uint64_t)detail::get_u32le(p + 8) * 8; sd.bits_per_sample = detail::get_u16le(p + 14);
    if (tag == 0xFFFE && n >= 26) tag = detail::get_u16le(p + 24);
    sd.codec_tag = tag;
    sd.acodec = codec_from_tag(tag, sd.bits_per_sample);
    sd.kind = track_kind::Audio;
    if (n > 18) {
        uint32_t cb = detail::get_u16le(p + 16);
        if (cb && 18 + (size_t)cb <= n && tag != 1 && tag != 3) sd.extradata = knst_byte_string(p + 18, cb);
    }
    return sd.channels >= 1 && sd.channels <= 32 && sd.sample_rate >= 1 && sd.sample_rate <= 10000000u;
}

inline errc parse(demux_base& d) noexcept {
    detail::file_handle& f = d.file; const uint64_t fsz = f.size();
    uint8_t h[12];
    if (fsz < 12 || !f.seek(0) || !f.read_exact(h, 12)) return errc::Corrupt;
    if (std::memcmp(h, "RIFF", 4) != 0 && std::memcmp(h, "RF64", 4) != 0) return errc::Corrupt;
    if (std::memcmp(h + 8, "WAVE", 4) != 0) return errc::Corrupt;
    stream_desc sd; bool have_fmt = false; uint64_t data_off = 0, data_size = 0; bool have_data = false;
    uint64_t pos = 12;
    while (pos + 8 <= fsz) {
        uint8_t ch[8];
        if (!f.seek(pos) || !f.read_exact(ch, 8)) break;
        uint64_t sz = detail::get_u32le(ch + 4), body = pos + 8;
        if (std::memcmp(ch, "data", 4) == 0) {
            have_data = true; data_off = body;
            if (sz == 0 || sz == 0xFFFFFFFFull || body + sz > fsz) sz = fsz - body;   // streamed / unfinished file
            data_size = sz;
            pos = body + sz + (sz & 1);
            continue;
        }
        if (body + sz > fsz) sz = fsz - body;
        if (std::memcmp(ch, "fmt ", 4) == 0 && sz >= 16 && sz <= 4096) {
            uint8_t buf[4096];
            if (f.read_exact(buf, (size_t)sz)) have_fmt = parse_fmt_body(buf, (size_t)sz, sd);
        } else if (std::memcmp(ch, "LIST", 4) == 0 && sz >= 4 && sz <= (1u << 20)) {
            knst_byte_string lb; if (detail::alloc_bytes(lb, sz) != errc::Ok) return errc::OutOfMemory;
            if (f.read_exact(&lb[0], (size_t)sz) && std::memcmp(lb.data(), "INFO", 4) == 0) {
                size_t q = 4;
                while (q + 8 <= sz) {
                    uint32_t cs = detail::get_u32le(lb.data() + q + 4);
                    if (q + 8 + cs > sz) break;
                    const uint8_t* s = lb.data() + q + 8; const uint8_t* id = lb.data() + q;
                    if (!std::memcmp(id, "INAM", 4)) detail::read_cstr(s, cs, d.meta.title);
                    else if (!std::memcmp(id, "IART", 4)) detail::read_cstr(s, cs, d.meta.artist);
                    else if (!std::memcmp(id, "IPRD", 4)) detail::read_cstr(s, cs, d.meta.album);
                    else if (!std::memcmp(id, "ICMT", 4)) detail::read_cstr(s, cs, d.meta.comment);
                    else if (!std::memcmp(id, "ICOP", 4)) detail::read_cstr(s, cs, d.meta.copyright);
                    else if (!std::memcmp(id, "ISFT", 4)) detail::read_cstr(s, cs, d.meta.software);
                    q += 8 + cs + (cs & 1);
                }
            }
        }
        pos = body + sz + (sz & 1);
    }
    if (!have_fmt || !have_data) return errc::Corrupt;
    if (sd.acodec == audio_codec::Unknown) return errc::NotSupported;
    const uint32_t align = audio_codec_is_pcm(sd.acodec) ? sd.channels * (sd.bits_per_sample / 8) : 0;
    if (align == 0) return errc::NotSupported;
    uint32_t ti = d.add_track(sd);
    data_size -= data_size % align;
    const uint64_t total_frames = data_size / align;
    const uint32_t per = 4096;
    uint64_t nchunks = (total_frames + per - 1) / per;
    if (nchunks > KNST_VIDEO_MAX_SAMPLES) return errc::TooLarge;
    d.tracks[ti].samples.reserve((uint32_t)nchunks);
    for (uint64_t fr = 0; fr < total_frames; fr += per) {
        sample_ref s; uint64_t n = total_frames - fr < per ? total_frames - fr : per;
        s.offset = data_off + fr * align; s.size = (uint32_t)(n * align); s.units = (uint32_t)n; s.unit_start = fr;
        s.pts_us = detail::muldiv_u64(fr, 1000000ull, sd.sample_rate);
        d.tracks[ti].samples.push_back(s);
    }
    d.tracks[ti].desc.duration_us = detail::muldiv_u64(total_frames, 1000000ull, sd.sample_rate);
    d.tracks[ti].desc.frame_count = total_frames;
    d.duration_us = d.tracks[ti].desc.duration_us;
    d.kind = container::WAV;
    return errc::Ok;
}

class writer {
public:
    writer() noexcept = default;
    ~writer() { close(); }
    writer(const writer&) = delete;
    writer& operator=(const writer&) = delete;

    result<void> open(const knst_c16string& path, uint32_t sample_rate, uint32_t channels,
                      audio_codec codec = audio_codec::PCM_S16LE, const metadata* meta = nullptr) noexcept {
        close();
        err_ = errc::Ok;
        if (path.empty()) return fail_(errc::InvalidArgument);
        detail::wbuf b;
        b.tag("RIFF"); b.u32le(0); b.tag("WAVE");
        b.tag("fmt "); uint32_t szp = b.size(); b.u32le(0);
        errc e = write_fmt_body(b, codec, sample_rate, channels);
        if (e != errc::Ok) return fail_(e);
        b.set_u32le(szp, b.size() - szp - 4);
        const bool flt = codec == audio_codec::PCM_F32LE;
        fact_pos_ = 0;
        if (flt) { b.tag("fact"); b.u32le(4); fact_pos_ = b.size(); b.u32le(0); }
        b.tag("data"); data_size_pos_ = b.size(); b.u32le(0);
        if (!b.ok()) return fail_(errc::OutOfMemory);
        if (!knst_file::ensure_parent_dir(path)) return fail_(errc::IoError);
        e = f_.open(path, detail::file_handle::mode::write);
        if (e != errc::Ok) return fail_(e);
        if (!f_.write(b.data(), b.size())) { f_.close(); return fail_(errc::IoError); }
        rate_ = sample_rate; ch_ = channels; codec_ = codec; align_ = channels * audio_codec_bits(codec) / 8;
        data_bytes_ = 0; if (meta) meta_ = *meta; else meta_ = metadata();
        return ok_void();
    }
    result<void> write(const uint8_t* data, uint32_t bytes) noexcept {
        if (!f_.is_open()) return fail_(errc::BadState);
        if (!data || bytes == 0 || bytes % align_) return fail_(errc::InvalidArgument);
        if (data_bytes_ + bytes + 4096 > 0xFFFFFFF0ull) return fail_(errc::TooLarge);
        if (!f_.write(data, bytes)) return fail_(errc::IoError);
        data_bytes_ += bytes;
        return ok_void();
    }
    result<void> write_s16(const int16_t* s, uint32_t count) noexcept {
        if (codec_ != audio_codec::PCM_S16LE) return fail_(errc::BadState);
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        return write(reinterpret_cast<const uint8_t*>(s), count * 2);
#else
        knst_byte_string t; if (detail::alloc_bytes(t, (uint64_t)count * 2) != errc::Ok) return fail_(errc::OutOfMemory);
        for (uint32_t i = 0; i < count; ++i) detail::put_u16le(&t[i * 2], (uint16_t)s[i]);
        return write(t.data(), count * 2);
#endif
    }
    result<void> write_f32(const float* s, uint32_t count) noexcept {
        if (codec_ != audio_codec::PCM_F32LE) return fail_(errc::BadState);
        return write(reinterpret_cast<const uint8_t*>(s), count * 4);
    }
    result<void> close() noexcept {
        if (!f_.is_open()) return ok_void();
        errc e = errc::Ok;
        if (data_bytes_ & 1) { uint8_t z = 0; f_.write(&z, 1); }
        // INFO list
        if (!meta_.title.empty() || !meta_.artist.empty() || !meta_.album.empty() || !meta_.comment.empty() || !meta_.copyright.empty() || !meta_.software.empty()) {
            detail::wbuf b; b.tag("LIST"); b.u32le(0); b.tag("INFO");
            auto add = [&](const char* id, const knst_c16string& s) {
                if (s.empty()) return;
                knst_byte_string u = detail::utf8_of(s);
                b.tag(id); b.u32le(u.length() + 1); b.put(u.data(), u.length()); b.u8(0); if ((u.length() + 1) & 1) b.u8(0);
            };
            add("INAM", meta_.title); add("IART", meta_.artist); add("IPRD", meta_.album);
            add("ICMT", meta_.comment); add("ICOP", meta_.copyright); add("ISFT", meta_.software);
            b.set_u32le(4, b.size() - 8);
            if (!f_.write(b.data(), b.size())) e = errc::IoError;
        }
        uint64_t end = f_.tell();
        uint8_t t[4];
        auto patch = [&](uint64_t at, uint32_t v) { detail::put_u32le(t, v); if (!f_.seek(at) || !f_.write(t, 4)) e = errc::IoError; };
        patch(4, (uint32_t)(end - 8));
        patch(data_size_pos_, (uint32_t)data_bytes_);
        if (fact_pos_) patch(fact_pos_, (uint32_t)(data_bytes_ / align_));
        if (!f_.close() && e == errc::Ok) e = errc::IoError;
        if (e != errc::Ok) err_ = e;
        return void_result(e);
    }
    uint64_t bytes_written() const noexcept { return data_bytes_; }
    uint64_t frames_written() const noexcept { return align_ ? data_bytes_ / align_ : 0; }
    errc last_error() const noexcept { return err_; }
private:
    detail::file_handle f_;
    uint32_t rate_ = 0, ch_ = 0, align_ = 0;
    audio_codec codec_ = audio_codec::PCM_S16LE;
    uint64_t data_bytes_ = 0; uint64_t data_size_pos_ = 0, fact_pos_ = 0;
    metadata meta_;
    errc err_ = errc::Ok;
    result<void> fail_(errc e) noexcept { err_ = e; return void_result(e); }
};

} // namespace wav

// ============================================================
// AVI (RIFF AVI 1.0: up to ~4 GB; larger files report TooLarge)
// ============================================================

namespace avi {

inline uint32_t dib_stride(uint32_t w, uint32_t bits) noexcept { return (uint32_t)(((uint64_t)w * bits + 31) / 32 * 4); }

inline video_codec video_codec_from_fourcc(uint32_t tag) noexcept {
    using detail::fcc;
    const uint32_t t = detail::fcc_lower(tag);
    if (t == fcc('m','j','p','g') || t == fcc('a','v','i','1') || t == fcc('j','p','e','g') || t == fcc('m','j','p','a')) return video_codec::MJPEG;
    if (t == fcc('h','2','6','4') || t == fcc('x','2','6','4') || t == fcc('a','v','c','1') || t == fcc('d','a','v','c')) return video_codec::H264;
    if (t == fcc('h','e','v','c') || t == fcc('h','2','6','5') || t == fcc('h','v','c','1') || t == fcc('h','e','v','1')) return video_codec::H265;
    if (t == fcc('v','p','8','0')) return video_codec::VP8;
    if (t == fcc('v','p','9','0')) return video_codec::VP9;
    if (t == fcc('a','v','0','1')) return video_codec::AV1;
    if (t == fcc('x','v','i','d') || t == fcc('d','i','v','x') || t == fcc('d','x','5','0') || t == fcc('m','p','4','v') || t == fcc('f','m','p','4')) return video_codec::MPEG4;
    return video_codec::Unknown;
}

namespace detail_ {

struct strm_timing { uint32_t scale = 1, rate = 0, sample_size = 0; };
struct range { uint64_t start = 0, end = 0; };

inline bool read_at(detail::file_handle& f, uint64_t pos, void* dst, size_t n) noexcept { return f.seek(pos) && f.read_exact(dst, n); }

inline errc make_stream(detail::file_handle& /*f*/, const uint8_t* strh, size_t strh_n, const knst_byte_string& strf,
                        demux_base& d, knst_vector<strm_timing>& timing, uint32_t avih_usec) noexcept {
    using detail::fcc;
    if (strh_n < 48) return errc::Corrupt;
    const uint32_t type = detail::get_u32le(strh), handler = detail::get_u32le(strh + 4);
    strm_timing tm; tm.scale = detail::get_u32le(strh + 20); tm.rate = detail::get_u32le(strh + 24); tm.sample_size = detail::get_u32le(strh + 44);
    stream_desc sd;
    const uint8_t* sf = strf.data(); const size_t sn = strf.length();
    if (type == fcc('v','i','d','s')) {
        sd.kind = track_kind::Video;
        if (sn < 40) return errc::Corrupt;
        int32_t w = (int32_t)detail::get_u32le(sf + 4), h = (int32_t)detail::get_u32le(sf + 8);
        uint32_t bits = detail::get_u16le(sf + 14), comp = detail::get_u32le(sf + 16), clr = detail::get_u32le(sf + 32);
        if (w <= 0 || h == 0) return errc::Corrupt;
        sd.width = (uint32_t)w; sd.height = (uint32_t)(h < 0 ? -(int64_t)h : h);
        if (sd.width > KNST_VIDEO_MAX_DIMENSION || sd.height > KNST_VIDEO_MAX_DIMENSION) return errc::TooLarge;
        sd.bottom_up = h > 0; sd.codec_tag = comp ? comp : handler;
        if (comp == 0 || comp == 3 || detail::fcc_lower(comp) == fcc('d','i','b',' ') || detail::fcc_lower(comp) == fcc('r','g','b',' ')) {
            sd.vcodec = video_codec::RAW; sd.stride = dib_stride(sd.width, bits);
            if (bits == 24) sd.pix = pixel_format::BGR24;
            else if (bits == 32) sd.pix = pixel_format::BGRA32;
            else if (bits == 8) {
                bool grey = sn >= 40 + 1024 ? true : false;
                if (grey) { for (uint32_t i = 0; i < 256; ++i) { const uint8_t* e = sf + 40 + i * 4; if (e[0] != i || e[1] != i || e[2] != i) { grey = false; break; } } }
                sd.pix = grey || clr == 0 ? pixel_format::GREY : pixel_format::Unknown;
                if (!grey && clr != 0) sd.vcodec = video_codec::Unknown;
            } else sd.vcodec = video_codec::Unknown;
        } else {
            sd.vcodec = video_codec_from_fourcc(comp);
            if (sd.vcodec == video_codec::Unknown) sd.vcodec = video_codec_from_fourcc(handler);
            if (sn > 40) sd.extradata = knst_byte_string(sf + 40, (uint32_t)(sn - 40));
        }
        if (tm.scale && tm.rate) {
            uint32_t g = detail::gcd_u32(tm.rate, tm.scale); sd.fps_num = tm.rate / g; sd.fps_den = tm.scale / g;
        } else if (avih_usec) { sd.fps_num = 1000000u; sd.fps_den = avih_usec; }
        if (sd.fps_num) sd.fps = (double)sd.fps_num / (double)sd.fps_den;
        if (sd.fps_num == 0) { sd.fps_num = 30; sd.fps_den = 1; sd.fps = 30.0; }
    } else if (type == fcc('a','u','d','s')) {
        if (!wav::parse_fmt_body(sf, sn, sd)) sd.acodec = audio_codec::Unknown;
        sd.kind = track_kind::Audio;
    } else {
        sd.kind = (type == fcc('t','x','t','s')) ? track_kind::Subtitle : track_kind::Other;
    }
    d.add_track(sd);
    timing.push_back(tm);
    return errc::Ok;
}

inline errc parse_strl(detail::file_handle& f, range r, demux_base& d, knst_vector<strm_timing>& timing, uint32_t avih_usec) noexcept {
    uint8_t strh[64]; size_t strh_n = 0; knst_byte_string strf; bool have_h = false;
    uint64_t pos = r.start;
    while (pos + 8 <= r.end) {
        uint8_t ch[8]; if (!read_at(f, pos, ch, 8)) break;
        uint64_t sz = detail::get_u32le(ch + 4), body = pos + 8;
        if (std::memcmp(ch, "strh", 4) == 0 && sz >= 48) { strh_n = sz < 64 ? (size_t)sz : 64; if (f.read_exact(strh, strh_n)) have_h = true; }
        else if (std::memcmp(ch, "strf", 4) == 0 && sz <= (1u << 20) && body + sz <= r.end) {
            if (detail::alloc_bytes(strf, sz) != errc::Ok) return errc::OutOfMemory;
            if (sz && !f.read_exact(&strf[0], (size_t)sz)) return errc::Corrupt;
        }
        pos = body + sz + (sz & 1);
    }
    if (!have_h) return errc::Corrupt;
    return make_stream(f, strh, strh_n, strf, d, timing, avih_usec);
}

inline void add_chunk(demux_base& d, uint32_t stream, uint64_t off, uint32_t size, bool key) noexcept {
    if (stream >= d.tracks.size()) return;
    auto& v = d.tracks[stream].samples;
    if (v.size() >= KNST_VIDEO_MAX_SAMPLES) return;
    sample_ref s; s.offset = off; s.size = size; s.key = key; v.push_back(s);
}

// '00dc' -> 0 ; returns -1 if not a stream chunk
inline int chunk_stream(const uint8_t* id, char& t0, char& t1) noexcept {
    if (id[0] < '0' || id[0] > '9' || id[1] < '0' || id[1] > '9') return -1;
    t0 = (char)id[2]; t1 = (char)id[3];
    if (!((t0 == 'd' && (t1 == 'c' || t1 == 'b')) || (t0 == 'w' && t1 == 'b') || (t0 == 't' && t1 == 'x'))) return -1;
    return (id[0] - '0') * 10 + (id[1] - '0');
}

} // namespace detail_

inline errc parse(demux_base& d) noexcept {
    using namespace detail_;
    detail::file_handle& f = d.file; const uint64_t fsz = f.size();
    uint8_t h[12];
    if (fsz < 12 || !read_at(f, 0, h, 12)) return errc::Corrupt;
    if (std::memcmp(h, "RIFF", 4) != 0 || (std::memcmp(h + 8, "AVI ", 4) != 0 && std::memcmp(h + 8, "AVIX", 4) != 0)) return errc::Corrupt;

    range hdrl, info, idx1; bool has_hdrl = false, has_info = false, has_idx1 = false;
    knst_vector<range> movis; uint32_t riffs = 1;
    uint64_t pos = 12;
    while (pos + 8 <= fsz) {
        uint8_t ch[8]; if (!read_at(f, pos, ch, 8)) break;
        const uint32_t sz = detail::get_u32le(ch + 4);
        const uint64_t body = pos + 8; uint64_t end = body + sz; const bool trunc = end > fsz; if (trunc) end = fsz;
        if (std::memcmp(ch, "RIFF", 4) == 0) { ++riffs; pos = body + 4; continue; }
        if (std::memcmp(ch, "LIST", 4) == 0) {
            uint8_t t[4]; if (!f.read_exact(t, 4)) break;
            if (!std::memcmp(t, "hdrl", 4)) { hdrl.start = body + 4; hdrl.end = end; has_hdrl = true; }
            else if (!std::memcmp(t, "INFO", 4)) { info.start = body + 4; info.end = end; has_info = true; }
            else if (!std::memcmp(t, "movi", 4)) {
                range m; m.start = body + 4; m.end = (sz < 4 || trunc) ? fsz : end;     // sz < 4: writer died before patching
                movis.push_back(m);
                if (sz < 4 || trunc) break;
            }
        } else if (std::memcmp(ch, "idx1", 4) == 0) { idx1.start = body; idx1.end = end; has_idx1 = true; }
        if (trunc) break;
        pos = end + (sz & 1);
    }
    if (!has_hdrl) return errc::Corrupt;

    // ---- hdrl ----
    knst_vector<strm_timing> timing;
    uint32_t avih_usec = 0;
    pos = hdrl.start;
    while (pos + 8 <= hdrl.end) {
        uint8_t ch[8]; if (!read_at(f, pos, ch, 8)) break;
        const uint32_t sz = detail::get_u32le(ch + 4); const uint64_t body = pos + 8;
        if (std::memcmp(ch, "avih", 4) == 0 && sz >= 40) { uint8_t a[56] = {0}; if (f.read_exact(a, sz < 56 ? sz : 56)) avih_usec = detail::get_u32le(a); }
        else if (std::memcmp(ch, "LIST", 4) == 0) {
            uint8_t t[4];
            if (sz >= 4 && f.read_exact(t, 4) && !std::memcmp(t, "strl", 4)) {
                if (d.tracks.size() >= KNST_VIDEO_MAX_TRACKS) return errc::TooLarge;
                range r; r.start = body + 4; r.end = body + sz < hdrl.end ? body + sz : hdrl.end;
                errc e = parse_strl(f, r, d, timing, avih_usec);
                if (e != errc::Ok) return e;
            }
        }
        pos = body + sz + (sz & 1);
    }
    if (d.tracks.empty()) return errc::Corrupt;

    // ---- INFO ----
    if (has_info) {
        pos = info.start;
        while (pos + 8 <= info.end) {
            uint8_t ch[8]; if (!read_at(f, pos, ch, 8)) break;
            uint32_t sz = detail::get_u32le(ch + 4); uint64_t body = pos + 8;
            if (sz && sz <= 4096 && body + sz <= info.end) {
                uint8_t s[4096]; if (f.read_exact(s, sz)) {
                    knst_c16string* dst = nullptr; bool yr = false;
                    if (!std::memcmp(ch, "INAM", 4)) dst = &d.meta.title; else if (!std::memcmp(ch, "IART", 4)) dst = &d.meta.artist;
                    else if (!std::memcmp(ch, "IPRD", 4)) dst = &d.meta.album; else if (!std::memcmp(ch, "ICMT", 4)) dst = &d.meta.comment;
                    else if (!std::memcmp(ch, "ICOP", 4)) dst = &d.meta.copyright; else if (!std::memcmp(ch, "ISFT", 4)) dst = &d.meta.software;
                    else if (!std::memcmp(ch, "ICRD", 4)) yr = true;
                    if (dst) detail::read_cstr(s, sz, *dst);
                    if (yr) d.meta.year = detail::parse_year(knst_byte_string(s, sz));
                }
            }
            pos = body + sz + (sz & 1);
        }
    }

    // ---- index: idx1 fast path (single movi) else scan ----
    bool indexed = false;
    if (has_idx1 && movis.size() == 1 && riffs == 1) {
        const uint64_t n = (idx1.end - idx1.start) / 16;
        if (n > 0 && n <= KNST_VIDEO_MAX_SAMPLES) {
            uint8_t e0[16]; uint64_t base = 0; bool base_ok = false;
            if (read_at(f, idx1.start, e0, 16)) {
                const uint32_t off0 = detail::get_u32le(e0 + 8);
                uint8_t tag[4];
                const uint64_t cand[2] = { movis[0].start - 4 + off0, (uint64_t)off0 };
                for (int c = 0; c < 2 && !base_ok; ++c) {
                    if (cand[c] + 8 <= fsz && read_at(f, cand[c], tag, 4) && !std::memcmp(tag, e0, 4)) { base = cand[c] - off0; base_ok = true; }
                }
            }
            if (base_ok) {
                knst_byte_string blk; bool bad = false;
                for (uint64_t i = 0; i < n && !bad;) {
                    const uint64_t take = (n - i) < 4096 ? (n - i) : 4096;
                    if (detail::alloc_bytes(blk, take * 16) != errc::Ok) return errc::OutOfMemory;
                    if (!read_at(f, idx1.start + i * 16, &blk[0], (size_t)take * 16)) { bad = true; break; }
                    for (uint64_t k = 0; k < take; ++k) {
                        const uint8_t* e = blk.data() + k * 16; char t0, t1;
                        int s = chunk_stream(e, t0, t1);
                        if (s < 0) continue;
                        uint32_t flags = detail::get_u32le(e + 4), off = detail::get_u32le(e + 8), size = detail::get_u32le(e + 12);
                        uint64_t dpos = base + off + 8;
                        if (dpos + size > fsz) { bad = true; break; }
                        add_chunk(d, (uint32_t)s, dpos, size, (flags & 0x10) != 0);
                    }
                    i += take;
                }
                indexed = !bad;
                if (bad) for (uint32_t t = 0; t < d.tracks.size(); ++t) d.tracks[t].samples.clear();
            }
        }
    }
    if (!indexed) {
        for (uint32_t m = 0; m < movis.size(); ++m) {
            uint64_t p = movis[m].start; const uint64_t end = movis[m].end < fsz ? movis[m].end : fsz;
            while (p + 8 <= end) {
                uint8_t ch[8]; if (!read_at(f, p, ch, 8)) break;
                const uint32_t sz = detail::get_u32le(ch + 4);
                if (!std::memcmp(ch, "LIST", 4)) { p += 12; continue; }
                char t0, t1; int s = chunk_stream(ch, t0, t1);
                if (p + 8 + sz > end) break;
                if (s >= 0) add_chunk(d, (uint32_t)s, p + 8, sz, true);
                p += 8 + (uint64_t)sz + (sz & 1);
            }
        }
    }

    // ---- timestamps ----
    uint64_t dur = 0;
    for (uint32_t t = 0; t < d.tracks.size(); ++t) {
        track_data& td = d.tracks[t]; stream_desc& sd = td.desc; const strm_timing& tm = timing[t];
        const uint32_t n = td.samples.size();
        uint64_t units = 0;
        const bool intra = (sd.kind == track_kind::Video) ? (sd.vcodec == video_codec::MJPEG || sd.vcodec == video_codec::RAW) : true;
        for (uint32_t i = 0; i < n; ++i) {
            sample_ref& s = td.samples[i];
            if (!indexed && !intra) s.key = (i == 0);
            s.unit_start = units;
            if (sd.kind == track_kind::Video) {
                s.units = 1;
                s.pts_us = sd.fps_num ? detail::muldiv_u64(units, 1000000ull * sd.fps_den, sd.fps_num) : 0;
            } else if (sd.kind == track_kind::Audio && audio_codec_is_pcm(sd.acodec) && sd.channels && sd.bits_per_sample) {
                const uint32_t align = sd.channels * (sd.bits_per_sample / 8);
                s.units = align ? s.size / align : 0;
                s.pts_us = detail::muldiv_u64(units, 1000000ull, sd.sample_rate ? sd.sample_rate : 1);
            } else {
                s.units = 1;
                s.pts_us = tm.rate ? detail::muldiv_u64(units * tm.scale, 1000000ull, tm.rate) : 0;
            }
            units += s.units;
        }
        sd.frame_count = (sd.kind == track_kind::Audio && audio_codec_is_pcm(sd.acodec)) ? units : n;
        if (sd.kind == track_kind::Video && sd.fps_num) sd.duration_us = detail::muldiv_u64(n, 1000000ull * sd.fps_den, sd.fps_num);
        else if (sd.kind == track_kind::Audio && sd.sample_rate && audio_codec_is_pcm(sd.acodec)) sd.duration_us = detail::muldiv_u64(units, 1000000ull, sd.sample_rate);
        else if (n) sd.duration_us = td.samples[n - 1].pts_us;
        if (sd.duration_us > dur) dur = sd.duration_us;
    }
    d.duration_us = dur; d.kind = container::AVI;
    return errc::Ok;
}

class writer {
public:
    writer() noexcept = default;
    ~writer() { close(); }
    writer(const writer&) = delete;
    writer& operator=(const writer&) = delete;

    result<void> open(const knst_c16string& path, const writer_params& p) noexcept {
        close();
        err_ = errc::Ok; idx_.clear(); v_frames_ = 0; a_bytes_ = 0; max_chunk_ = 0; a_chunks_ = 0;
        if (path.empty() || p.width == 0 || p.height == 0 || p.fps_num == 0 || p.fps_den == 0) return fail_(errc::InvalidArgument);
        if (p.width > KNST_VIDEO_MAX_DIMENSION || p.height > KNST_VIDEO_MAX_DIMENSION) return fail_(errc::InvalidArgument);
        if (p.acodec != audio_codec::None && !audio_codec_is_pcm(p.acodec)) return fail_(errc::NotSupported);
        p_ = p;
        detail::wbuf b;
        errc e = build_header(b);
        if (e != errc::Ok) return fail_(e);
        if (!knst_file::ensure_parent_dir(path)) return fail_(errc::IoError);
        e = f_.open(path, detail::file_handle::mode::write);
        if (e != errc::Ok) return fail_(e);
        if (!f_.write(b.data(), b.size())) { f_.close(); return fail_(errc::IoError); }
        movi_tag_pos_ = movi_list_pos_ + 8;
        return ok_void();
    }

    // `data` is the codec payload (JPEG / DIB rows / H.264 AU ...). size == 0 writes a dropped frame (player repeats the last one).
    result<void> write_video(const uint8_t* data, uint32_t size, bool keyframe = true) noexcept {
        if (!f_.is_open()) return fail_(errc::BadState);
        if (size && !data) return fail_(errc::InvalidArgument);
        errc e = put_chunk(video_id_, data, size, keyframe ? 0x10u : 0u);
        if (e != errc::Ok) return fail_(e);
        ++v_frames_;
        return ok_void();
    }
    result<void> write_audio(const uint8_t* data, uint32_t size) noexcept {
        if (!f_.is_open() || p_.acodec == audio_codec::None) return fail_(errc::BadState);
        if (!data || size == 0 || size % align_a_) return fail_(errc::InvalidArgument);
        errc e = put_chunk(detail::fcc('0', '1', 'w', 'b'), data, size, 0x10u);
        if (e != errc::Ok) return fail_(e);
        a_bytes_ += size; ++a_chunks_;
        return ok_void();
    }

    result<void> close() noexcept {
        if (!f_.is_open()) return ok_void();
        errc e = errc::Ok;
        const uint64_t idx_pos = f_.tell();
        {
            detail::wbuf h; h.tag("idx1"); h.u32le((uint32_t)idx_.size() * 16);
            if (!f_.write(h.data(), h.size())) e = errc::IoError;
            for (uint32_t i = 0; i < idx_.size() && e == errc::Ok;) {
                detail::wbuf blk; uint32_t take = idx_.size() - i < 4096 ? idx_.size() - i : 4096;
                for (uint32_t k = 0; k < take; ++k) { const idx_entry& x = idx_[i + k]; blk.u32le(x.id); blk.u32le(x.flags); blk.u32le(x.off); blk.u32le(x.size); }
                if (!blk.ok() || !f_.write(blk.data(), blk.size())) e = errc::IoError;
                i += take;
            }
        }
        const uint64_t end = f_.tell();
        uint8_t t[4];
        auto patch = [&](uint64_t at, uint32_t v) { detail::put_u32le(t, v); if (!f_.seek(at) || !f_.write(t, 4)) e = errc::IoError; };
        if (e == errc::Ok) {
            patch(4, (uint32_t)(end - 8));
            patch(movi_list_pos_ + 4, (uint32_t)(idx_pos - movi_list_pos_ - 8));
            patch(pos_total_frames_, (uint32_t)v_frames_);
            patch(pos_vlength_, (uint32_t)v_frames_);
            patch(pos_bufsize_, max_chunk_);
            const uint64_t abps = p_.acodec != audio_codec::None ? (uint64_t)p_.sample_rate * align_a_ : 0;
            const uint64_t vbps = (uint64_t)max_chunk_ * p_.fps_num / (p_.fps_den ? p_.fps_den : 1);
            patch(pos_maxbps_, (uint32_t)(vbps + abps > 0xFFFFFFFFull ? 0xFFFFFFFFull : vbps + abps));
            if (p_.acodec != audio_codec::None) patch(pos_alength_, (uint32_t)(a_bytes_ / align_a_));
        }
        if (!f_.close() && e == errc::Ok) e = errc::IoError;
        if (e != errc::Ok) err_ = e;
        return void_result(e);
    }

    uint64_t video_frames() const noexcept { return v_frames_; }
    uint64_t audio_chunks() const noexcept { return a_chunks_; }
    uint64_t bytes_written() const noexcept { return f_.tell(); }
    errc last_error() const noexcept { return err_; }

private:
    struct idx_entry { uint32_t id, flags, off, size; };

    detail::file_handle f_;
    writer_params p_;
    knst_vector<idx_entry> idx_;
    uint64_t v_frames_ = 0, a_bytes_ = 0, a_chunks_ = 0;
    uint32_t max_chunk_ = 0, align_a_ = 1, video_id_ = 0;
    uint64_t movi_list_pos_ = 0, movi_tag_pos_ = 0;
    uint64_t pos_total_frames_ = 0, pos_vlength_ = 0, pos_alength_ = 0, pos_bufsize_ = 0, pos_maxbps_ = 0;
    errc err_ = errc::Ok;

    result<void> fail_(errc e) noexcept { err_ = e; return void_result(e); }

    errc put_chunk(uint32_t id, const uint8_t* d, uint32_t size, uint32_t flags) noexcept {
        const uint64_t pos = f_.tell();
        const uint64_t need = 8 + (uint64_t)size + (size & 1);
        const uint64_t idx_bytes = 8 + 16ull * (idx_.size() + 2);
        if (pos + need + idx_bytes > 0xFFFFFFF0ull) return errc::TooLarge;      // AVI 1.0 limit
        uint8_t h[8]; detail::put_u32le(h, id); detail::put_u32le(h + 4, size);
        if (!f_.write(h, 8)) return errc::IoError;
        if (size && !f_.write(d, size)) return errc::IoError;
        if (size & 1) { uint8_t z = 0; if (!f_.write(&z, 1)) return errc::IoError; }
        idx_entry x; x.id = id; x.flags = flags; x.off = (uint32_t)(pos - movi_tag_pos_); x.size = size;
        idx_.push_back(x);
        if (size > max_chunk_) max_chunk_ = size;
        return errc::Ok;
    }

    errc build_header(detail::wbuf& b) noexcept {
        using detail::fcc;
        const bool raw = (p_.vcodec == video_codec::RAW);
        uint32_t comp = 0, bits = 24, clr = 0; uint32_t handler = 0;
        if (raw) {
            if (p_.raw_pix == pixel_format::BGR24) bits = 24;
            else if (p_.raw_pix == pixel_format::BGRA32) bits = 32;
            else if (p_.raw_pix == pixel_format::GREY) { bits = 8; clr = 256; }
            else return errc::NotSupported;
            handler = fcc('D','I','B',' ');
        } else {
            switch (p_.vcodec) {
                case video_codec::MJPEG: comp = fcc('M','J','P','G'); break;
                case video_codec::H264:  comp = fcc('H','2','6','4'); break;
                case video_codec::H265:  comp = fcc('H','E','V','C'); break;
                case video_codec::VP8:   comp = fcc('V','P','8','0'); break;
                case video_codec::VP9:   comp = fcc('V','P','9','0'); break;
                case video_codec::AV1:   comp = fcc('A','V','0','1'); break;
                case video_codec::MPEG4: comp = fcc('F','M','P','4'); break;
                default: break;
            }
            if (p_.codec_tag) comp = p_.codec_tag;
            if (!comp) return errc::NotSupported;
            handler = comp;
        }
        video_id_ = raw ? fcc('0','0','d','b') : fcc('0','0','d','c');
        const bool has_a = p_.acodec != audio_codec::None;
        uint32_t a_bits = 0;
        if (has_a) {
            a_bits = audio_codec_bits(p_.acodec);
            if (p_.channels < 1 || p_.channels > 32 || p_.sample_rate < 1) return errc::InvalidArgument;
            align_a_ = p_.channels * a_bits / 8;
        }
        const uint32_t usec = (uint32_t)(1000000ull * p_.fps_den / p_.fps_num);

        b.tag("RIFF"); b.u32le(0); b.tag("AVI ");
        const uint32_t hdrl = b.size(); b.tag("LIST"); b.u32le(0); b.tag("hdrl");
        b.tag("avih"); b.u32le(56);
        b.u32le(usec ? usec : 1);
        pos_maxbps_ = b.size(); b.u32le(0);
        b.u32le(0);                                   // padding granularity
        b.u32le(0x10 | 0x100);                        // HASINDEX | ISINTERLEAVED
        pos_total_frames_ = b.size(); b.u32le(0);
        b.u32le(0); b.u32le(has_a ? 2 : 1);
        pos_bufsize_ = b.size(); b.u32le(0);
        b.u32le(p_.width); b.u32le(p_.height); b.zeros(16);

        // video stream
        uint32_t s = b.size(); b.tag("LIST"); b.u32le(0); b.tag("strl");
        b.tag("strh"); b.u32le(56);
        b.tag("vids"); b.u32le(handler); b.u32le(0); b.u32le(0); b.u32le(0);
        b.u32le(p_.fps_den); b.u32le(p_.fps_num); b.u32le(0);
        pos_vlength_ = b.size(); b.u32le(0);
        b.u32le(0); b.u32le(0xFFFFFFFFu); b.u32le(0);
        b.u16le(0); b.u16le(0); b.u16le(p_.width > 32767 ? 32767 : p_.width); b.u16le(p_.height > 32767 ? 32767 : p_.height);
        const uint32_t stride = raw ? dib_stride(p_.width, bits) : 0;
        b.tag("strf"); uint32_t sf = b.size(); b.u32le(0);
        b.u32le(40); b.u32le(p_.width); b.u32le(p_.height); b.u16le(1); b.u16le(bits); b.u32le(comp);
        b.u32le(raw ? stride * p_.height : p_.width * p_.height * 3); b.u32le(0); b.u32le(0); b.u32le(clr); b.u32le(0);
        if (raw && bits == 8) for (uint32_t i = 0; i < 256; ++i) { b.u8(i); b.u8(i); b.u8(i); b.u8(0); }
        if (!raw && p_.vextra.length()) b.put(p_.vextra.data(), p_.vextra.length());
        { uint32_t n = b.size() - sf - 4; b.set_u32le(sf, n); if (n & 1) b.u8(0); }
        b.set_u32le(s + 4, b.size() - s - 8);

        if (has_a) {
            s = b.size(); b.tag("LIST"); b.u32le(0); b.tag("strl");
            b.tag("strh"); b.u32le(56);
            b.tag("auds"); b.u32le(0); b.u32le(0); b.u32le(0); b.u32le(0);
            b.u32le(align_a_); b.u32le(p_.sample_rate * align_a_); b.u32le(0);
            pos_alength_ = b.size(); b.u32le(0);
            b.u32le(0); b.u32le(0xFFFFFFFFu); b.u32le(align_a_);
            b.zeros(8);
            b.tag("strf"); sf = b.size(); b.u32le(0);
            errc e = wav::write_fmt_body(b, p_.acodec, p_.sample_rate, p_.channels);
            if (e != errc::Ok) return e;
            { uint32_t n = b.size() - sf - 4; b.set_u32le(sf, n); if (n & 1) b.u8(0); }
            b.set_u32le(s + 4, b.size() - s - 8);
        }
        b.set_u32le(hdrl + 4, b.size() - hdrl - 8);

        // INFO
        {
            const metadata& m = p_.meta;
            if (!m.title.empty() || !m.artist.empty() || !m.album.empty() || !m.comment.empty() || !m.copyright.empty() || !m.software.empty()) {
                uint32_t il = b.size(); b.tag("LIST"); b.u32le(0); b.tag("INFO");
                auto add = [&](const char* id, const knst_c16string& str) {
                    if (str.empty()) return;
                    knst_byte_string u = detail::utf8_of(str);
                    b.tag(id); b.u32le(u.length() + 1); b.put(u.data(), u.length()); b.u8(0); if ((u.length() + 1) & 1) b.u8(0);
                };
                add("INAM", m.title); add("IART", m.artist); add("IPRD", m.album);
                add("ICMT", m.comment); add("ICOP", m.copyright); add("ISFT", m.software);
                b.set_u32le(il + 4, b.size() - il - 8);
            }
        }
        movi_list_pos_ = b.size();
        b.tag("LIST"); b.u32le(0); b.tag("movi");
        return b.ok() ? errc::Ok : errc::OutOfMemory;
    }
};

} // namespace avi

namespace detail {
// fps (double) -> small rational, recognising NTSC 24000/1001, 30000/1001, 60000/1001 ...
inline void rational_from_fps(double fps, uint32_t& num, uint32_t& den) noexcept {
    if (!(fps > 0.0) || fps > 100000.0) { num = 30; den = 1; return; }
    double r = std::floor(fps + 0.5);
    if (std::fabs(fps - r) < 0.005) { num = (uint32_t)r; den = 1; return; }
    double n = std::floor(fps * 1001.0 + 0.5);
    if (std::fabs(n / 1001.0 - fps) < 0.01 * fps / 100.0 + 1e-4 && ((uint64_t)n % 1000) == 0) { num = (uint32_t)n; den = 1001; return; }
    uint32_t a = (uint32_t)std::floor(fps * 1000.0 + 0.5), b = 1000, g = gcd_u32(a, b);
    num = a / g; den = b / g;
}
} // namespace detail

// ============================================================
// Matroska / WebM
// ============================================================

namespace mkv {

namespace ids {
inline constexpr uint32_t EBML = 0x1A45DFA3, DocType = 0x4282, DocTypeVersion = 0x4287, DocTypeReadVersion = 0x4285,
    EBMLVersion = 0x4286, EBMLReadVersion = 0x42F7, EBMLMaxIDLength = 0x42F2, EBMLMaxSizeLength = 0x42F3,
    Segment = 0x18538067, SeekHead = 0x114D9B74, Seek = 0x4DBB, SeekID = 0x53AB, SeekPosition = 0x53AC,
    Info = 0x1549A966, TimecodeScale = 0x2AD7B1, Duration = 0x4489, MuxingApp = 0x4D80, WritingApp = 0x5741, Title = 0x7BA9,
    Tracks = 0x1654AE6B, TrackEntry = 0xAE, TrackNumber = 0xD7, TrackUID = 0x73C5, TrackType = 0x83, FlagLacing = 0x9C,
    CodecID = 0x86, CodecPrivate = 0x63A2, DefaultDuration = 0x23E383, Language = 0x22B59C,
    Video = 0xE0, PixelWidth = 0xB0, PixelHeight = 0xBA, Audio = 0xE1, SamplingFrequency = 0xB5, Channels = 0x9F, BitDepth = 0x6264,
    Cluster = 0x1F43B675, Timecode = 0xE7, SimpleBlock = 0xA3, BlockGroup = 0xA0, Block = 0xA1, BlockDuration = 0x9B, ReferenceBlock = 0xFB,
    Cues = 0x1C53BB6B, CuePoint = 0xBB, CueTime = 0xB3, CueTrackPositions = 0xB7, CueTrack = 0xF7, CueClusterPosition = 0xF1,
    Chapters = 0x1043A770, EditionEntry = 0x45B9, ChapterAtom = 0xB6, ChapterUID = 0x73C4, ChapterTimeStart = 0x91, ChapterTimeEnd = 0x92,
    ChapterDisplay = 0x80, ChapString = 0x85, ChapLanguage = 0x437C, Tags = 0x1254C367, Attachments = 0x1941A469, Void = 0xEC;
}

class writer {
public:
    writer() noexcept = default;
    ~writer() { close(); }
    writer(const writer&) = delete;
    writer& operator=(const writer&) = delete;

    result<void> open(const knst_c16string& path, const writer_params& p, bool webm = false) noexcept {
        close();
        err_ = errc::Ok; cues_.clear(); cluster_open_ = false; last_ms_ = 0; vframes_ = 0;
        if (path.empty()) return fail_(errc::InvalidArgument);
        if (!p.has_video && p.acodec == audio_codec::None) return fail_(errc::InvalidArgument);
        if (p.has_video && (p.width == 0 || p.height == 0 || p.fps_num == 0 || p.fps_den == 0)) return fail_(errc::InvalidArgument);
        p_ = p; webm_ = webm;
        vtrack_ = p.has_video ? 1 : 0; atrack_ = p.acodec != audio_codec::None ? (p.has_video ? 2 : 1) : 0;
        if (p.has_video && !video_codec_id(p.vcodec)) return fail_(errc::NotSupported);
        if (atrack_ && !audio_codec_id(p.acodec, nullptr)) return fail_(errc::NotSupported);
        if (atrack_ && (p.channels < 1 || p.sample_rate < 1)) return fail_(errc::InvalidArgument);

        detail::wbuf b;
        uint32_t m = b.ebml_master_begin(ids::EBML);
        b.ebml_uint(ids::EBMLVersion, 1); b.ebml_uint(ids::EBMLReadVersion, 1);
        b.ebml_uint(ids::EBMLMaxIDLength, 4); b.ebml_uint(ids::EBMLMaxSizeLength, 8);
        b.ebml_str(ids::DocType, webm ? "webm" : "matroska"); b.ebml_uint(ids::DocTypeVersion, 4); b.ebml_uint(ids::DocTypeReadVersion, 2);
        b.ebml_master_end(m);
        b.ebml_id(ids::Segment); seg_size_pos_ = b.size(); b.u8(0x01); b.zeros(7);
        seg_data_ = b.size();
        // reserved space for the SeekHead (patched on close)
        seekhead_pos_ = b.size(); b.ebml_id(ids::Void); b.ebml_size(157); b.zeros(157);
        info_pos_ = b.size() - seg_data_;
        m = b.ebml_master_begin(ids::Info);
        b.ebml_uint(ids::TimecodeScale, 1000000);
        b.ebml_str(ids::MuxingApp, "knst_vid"); b.ebml_str(ids::WritingApp, "knst_vid");
        if (!p.meta.title.empty()) { knst_byte_string t = detail::utf8_of(p.meta.title); b.ebml_bin(ids::Title, t.data(), t.length()); }
        b.ebml_id(ids::Duration); b.ebml_size(8); dur_pos_ = b.size(); b.u64be(0);
        b.ebml_master_end(m);
        tracks_pos_ = b.size() - seg_data_;
        m = b.ebml_master_begin(ids::Tracks);
        if (vtrack_) {
            uint32_t te = b.ebml_master_begin(ids::TrackEntry);
            b.ebml_uint(ids::TrackNumber, vtrack_); b.ebml_uint(ids::TrackUID, vtrack_); b.ebml_uint(ids::TrackType, 1); b.ebml_uint(ids::FlagLacing, 0);
            b.ebml_str(ids::CodecID, video_codec_id(p.vcodec));
            if (p.vextra.length()) b.ebml_bin(ids::CodecPrivate, p.vextra.data(), p.vextra.length());
            b.ebml_uint(ids::DefaultDuration, (uint64_t)(1000000000ull * p.fps_den / p.fps_num));
            uint32_t v = b.ebml_master_begin(ids::Video); b.ebml_uint(ids::PixelWidth, p.width); b.ebml_uint(ids::PixelHeight, p.height); b.ebml_master_end(v);
            b.ebml_master_end(te);
        }
        if (atrack_) {
            uint32_t te = b.ebml_master_begin(ids::TrackEntry);
            b.ebml_uint(ids::TrackNumber, atrack_); b.ebml_uint(ids::TrackUID, atrack_); b.ebml_uint(ids::TrackType, 2); b.ebml_uint(ids::FlagLacing, 0);
            uint32_t bits = 0; b.ebml_str(ids::CodecID, audio_codec_id(p.acodec, &bits));
            if (p.aextra.length()) b.ebml_bin(ids::CodecPrivate, p.aextra.data(), p.aextra.length());
            uint32_t a = b.ebml_master_begin(ids::Audio);
            b.ebml_float64(ids::SamplingFrequency, (double)p.sample_rate); b.ebml_uint(ids::Channels, p.channels);
            if (bits) b.ebml_uint(ids::BitDepth, bits);
            b.ebml_master_end(a); b.ebml_master_end(te);
            align_a_ = audio_codec_is_pcm(p.acodec) ? p.channels * audio_codec_bits(p.acodec) / 8 : 0;
        }
        b.ebml_master_end(m);
        chapters_pos_ = 0;
        if (!p.meta.chapters.empty()) {
            chapters_pos_ = b.size() - seg_data_;
            uint32_t ch = b.ebml_master_begin(ids::Chapters), ed = b.ebml_master_begin(ids::EditionEntry);
            for (uint32_t i = 0; i < p.meta.chapters.size(); ++i) {
                const chapter& c = p.meta.chapters[i];
                uint32_t at = b.ebml_master_begin(ids::ChapterAtom);
                b.ebml_uint(ids::ChapterUID, i + 1); b.ebml_uint(ids::ChapterTimeStart, c.start_us * 1000ull);
                if (c.end_us > c.start_us) b.ebml_uint(ids::ChapterTimeEnd, c.end_us * 1000ull);
                uint32_t dsp = b.ebml_master_begin(ids::ChapterDisplay);
                knst_byte_string t = detail::utf8_of(c.title); b.ebml_bin(ids::ChapString, t.data(), t.length()); b.ebml_str(ids::ChapLanguage, "eng");
                b.ebml_master_end(dsp); b.ebml_master_end(at);
            }
            b.ebml_master_end(ed); b.ebml_master_end(ch);
        }
        if (!b.ok()) return fail_(errc::OutOfMemory);
        if (!knst_file::ensure_parent_dir(path)) return fail_(errc::IoError);
        errc e = f_.open(path, detail::file_handle::mode::write);
        if (e != errc::Ok) return fail_(e);
        if (!f_.write(b.data(), b.size())) { f_.close(); return fail_(errc::IoError); }
        return ok_void();
    }

    result<void> write_video(const uint8_t* data, uint32_t size, uint64_t pts_us, bool keyframe) noexcept {
        if (!f_.is_open() || !vtrack_) return fail_(errc::BadState);
        errc e = put_block(vtrack_, data, size, pts_us, keyframe, true);
        if (e != errc::Ok) return fail_(e);
        ++vframes_;
        return ok_void();
    }
    result<void> write_audio(const uint8_t* data, uint32_t size, uint64_t pts_us) noexcept {
        if (!f_.is_open() || !atrack_) return fail_(errc::BadState);
        if (align_a_ && size % align_a_) return fail_(errc::InvalidArgument);
        errc e = put_block(atrack_, data, size, pts_us, true, false);
        if (e != errc::Ok) return fail_(e);
        return ok_void();
    }

    result<void> close() noexcept {
        if (!f_.is_open()) return ok_void();
        errc e = errc::Ok;
        end_cluster_();
        const uint64_t cues_abs = f_.tell();
        if (!cues_.empty()) {
            detail::wbuf b; uint32_t cm = b.ebml_master_begin(ids::Cues);
            for (uint32_t i = 0; i < cues_.size(); ++i) {
                uint32_t cp = b.ebml_master_begin(ids::CuePoint); b.ebml_uint(ids::CueTime, cues_[i].ms);
                uint32_t tp = b.ebml_master_begin(ids::CueTrackPositions);
                b.ebml_uint(ids::CueTrack, vtrack_ ? vtrack_ : atrack_); b.ebml_uint(ids::CueClusterPosition, cues_[i].pos);
                b.ebml_master_end(tp); b.ebml_master_end(cp);
            }
            b.ebml_master_end(cm);
            if (!b.ok() || !f_.write(b.data(), b.size())) e = errc::IoError;
        }
        const uint64_t end = f_.tell();
        // SeekHead
        {
            detail::wbuf s; uint32_t sh = s.ebml_master_begin(ids::SeekHead);
            auto entry = [&](uint32_t id, uint64_t pos) {
                uint32_t se = s.ebml_master_begin(ids::Seek);
                uint8_t idb[4] = { (uint8_t)(id >> 24), (uint8_t)(id >> 16), (uint8_t)(id >> 8), (uint8_t)id };
                s.ebml_bin(ids::SeekID, idb, 4); s.ebml_uint(ids::SeekPosition, pos); s.ebml_master_end(se);
            };
            entry(ids::Info, info_pos_); entry(ids::Tracks, tracks_pos_);
            if (chapters_pos_) entry(ids::Chapters, chapters_pos_);
            if (!cues_.empty()) entry(ids::Cues, cues_abs - seg_data_);
            s.ebml_master_end(sh);
            const uint32_t total = 160; const uint32_t gap = total - s.size();
            if (s.size() + 2 > total) e = errc::TooLarge;
            else {
                s.ebml_id(ids::Void);
                if (gap - 2 <= 126) { s.ebml_size(gap - 2); s.zeros(gap - 2); } else { s.u8(0x40 | ((gap - 3) >> 8)); s.u8((gap - 3) & 0xFF); s.zeros(gap - 3); }
                if (!f_.seek(seekhead_pos_) || !f_.write(s.data(), s.size())) e = errc::IoError;
            }
        }
        // Duration (ms, float64) and Segment size
        {
            double dur = (double)last_ms_;
            uint64_t u; std::memcpy(&u, &dur, 8); uint8_t t[8];
            for (int i = 0; i < 8; ++i) t[i] = (uint8_t)(u >> (56 - 8 * i));
            if (!f_.seek(dur_pos_) || !f_.write(t, 8)) e = errc::IoError;
            uint64_t len = end - seg_data_; uint8_t sz[8]; sz[0] = 0x01;
            for (int i = 0; i < 7; ++i) sz[1 + i] = (uint8_t)(len >> (48 - 8 * i));
            if (!f_.seek(seg_size_pos_) || !f_.write(sz, 8)) e = errc::IoError;
        }
        if (!f_.close() && e == errc::Ok) e = errc::IoError;
        if (e != errc::Ok) err_ = e;
        return void_result(e);
    }

    uint64_t video_frames() const noexcept { return vframes_; }
    uint64_t bytes_written() const noexcept { return f_.tell(); }
    errc last_error() const noexcept { return err_; }

private:
    struct cue { uint64_t ms, pos; };
    detail::file_handle f_;
    writer_params p_; bool webm_ = false;
    uint32_t vtrack_ = 0, atrack_ = 0, align_a_ = 0;
    uint64_t seg_size_pos_ = 0, seg_data_ = 0, seekhead_pos_ = 0, dur_pos_ = 0, info_pos_ = 0, tracks_pos_ = 0, chapters_pos_ = 0;
    knst_vector<cue> cues_;
    bool cluster_open_ = false; uint64_t cluster_pos_ = 0, cluster_ms_ = 0, cluster_start_bytes_ = 0;
    uint64_t last_ms_ = 0, vframes_ = 0;
    errc err_ = errc::Ok;

    result<void> fail_(errc e) noexcept { err_ = e; return void_result(e); }

    static const char* video_codec_id(video_codec c) noexcept {
        switch (c) {
            case video_codec::MJPEG: return "V_MJPEG";          case video_codec::H264: return "V_MPEG4/ISO/AVC";
            case video_codec::H265: return "V_MPEGH/ISO/HEVC";  case video_codec::VP8: return "V_VP8";
            case video_codec::VP9: return "V_VP9";              case video_codec::AV1: return "V_AV1";
            case video_codec::MPEG4: return "V_MPEG4/ISO/ASP";  case video_codec::ProRes: return "V_PRORES";
            default: return nullptr;
        }
    }
    static const char* audio_codec_id(audio_codec c, uint32_t* bits) noexcept {
        if (bits) *bits = audio_codec_bits(c);
        switch (c) {
            case audio_codec::PCM_U8: case audio_codec::PCM_S16LE: case audio_codec::PCM_S24LE: case audio_codec::PCM_S32LE: return "A_PCM/INT/LIT";
            case audio_codec::PCM_F32LE: return "A_PCM/FLOAT/IEEE";
            case audio_codec::AAC: return "A_AAC";   case audio_codec::Opus: return "A_OPUS";
            case audio_codec::MP3: return "A_MPEG/L3"; case audio_codec::FLAC: return "A_FLAC"; case audio_codec::Vorbis: return "A_VORBIS";
            default: return nullptr;
        }
    }

    void end_cluster_() noexcept {
        if (!cluster_open_) return;
        const uint64_t len = f_.tell() - (cluster_pos_ + 12);
        uint8_t sz[8]; sz[0] = 0x01;
        for (int i = 0; i < 7; ++i) sz[1 + i] = (uint8_t)(len >> (48 - 8 * i));
        const uint64_t keep = f_.tell();
        if (!f_.seek(cluster_pos_ + 4) || !f_.write(sz, 8)) err_ = errc::IoError;
        f_.seek(keep);
        cluster_open_ = false;
    }

    errc put_block(uint32_t track, const uint8_t* d, uint32_t size, uint64_t pts_us, bool key, bool is_video) noexcept {
        if (!d || size == 0) return errc::InvalidArgument;
        const uint64_t ms = (pts_us + 500) / 1000;
        const int64_t rel0 = (int64_t)ms - (int64_t)cluster_ms_;
        bool nc = !cluster_open_;
        if (!nc) {
            const bool boundary_ok = vtrack_ ? (is_video && key) : true;
            if ((boundary_ok && rel0 >= 1000) || rel0 > 30000 || rel0 < -30000 || f_.tell() - cluster_start_bytes_ > (16u << 20)) nc = true;
        }
        if (nc) {
            end_cluster_();
            if (err_ != errc::Ok) return err_;
            cluster_pos_ = f_.tell(); cluster_ms_ = ms; cluster_start_bytes_ = cluster_pos_;
            detail::wbuf b; b.ebml_id(ids::Cluster); b.u8(0x01); b.zeros(7); b.ebml_uint(ids::Timecode, ms);
            if (!f_.write(b.data(), b.size())) return errc::IoError;
            cluster_open_ = true;
            if ((is_video && key) || !vtrack_) { cue c; c.ms = ms; c.pos = cluster_pos_ - seg_data_; cues_.push_back(c); }
        }
        const int64_t rel = (int64_t)ms - (int64_t)cluster_ms_;
        uint8_t h[16]; uint32_t n = 0;
        h[n++] = 0xA3;
        // size = 4 + payload (vint, minimal up to 8 bytes)
        { detail::wbuf t; t.ebml_size((uint64_t)size + 4); for (uint32_t i = 0; i < t.size(); ++i) h[n++] = t.data()[i]; }
        h[n++] = (uint8_t)(0x80 | track); h[n++] = (uint8_t)((rel >> 8) & 0xFF); h[n++] = (uint8_t)(rel & 0xFF); h[n++] = key ? 0x80 : 0x00;
        if (!f_.write(h, n) || !f_.write(d, size)) return errc::IoError;
        uint64_t end_ms = ms;
        if (is_video && p_.fps_num) end_ms += (1000ull * p_.fps_den + p_.fps_num - 1) / p_.fps_num;
        if (!is_video && align_a_ && p_.sample_rate) end_ms += (uint64_t)size / align_a_ * 1000ull / p_.sample_rate;
        if (end_ms > last_ms_) last_ms_ = end_ms;
        return errc::Ok;
    }
};

// ---------------- parser ----------------

namespace detail_ {

struct elem { uint32_t id = 0; uint64_t size = 0; bool unknown = false; uint64_t data = 0; uint64_t end = 0; uint32_t hlen = 0; };

// parses id+size from a buffer; returns header length or 0 on failure
inline uint32_t parse_head(const uint8_t* b, size_t avail, elem& e) noexcept {
    if (avail < 2) return 0;
    uint8_t b0 = b[0]; uint32_t il = 1; uint8_t m = 0x80;
    while (il <= 4 && !(b0 & m)) { m >>= 1; ++il; }
    if (il > 4 || avail < il + 1) return 0;
    uint32_t id = 0; for (uint32_t i = 0; i < il; ++i) id = (id << 8) | b[i];
    const uint8_t* s = b + il; uint8_t s0 = s[0]; uint32_t sl = 1; m = 0x80;
    while (sl <= 8 && !(s0 & m)) { m >>= 1; ++sl; }
    if (sl > 8 || avail < il + sl) return 0;
    uint64_t v = s0 & (m - 1); bool allones = (v == (uint64_t)(m - 1));
    for (uint32_t i = 1; i < sl; ++i) { v = (v << 8) | s[i]; if (s[i] != 0xFF) allones = false; }
    e.id = id; e.size = v; e.unknown = allones; e.hlen = il + sl;
    return il + sl;
}
inline bool read_elem(detail::file_handle& f, uint64_t pos, uint64_t limit, elem& e) noexcept {
    uint8_t b[16]; if (pos >= limit) return false;
    size_t avail = (size_t)((limit - pos) < 16 ? (limit - pos) : 16);
    if (!f.seek(pos)) return false;
    size_t got = f.read(b, avail);
    uint32_t h = parse_head(b, got, e);
    if (!h) return false;
    e.data = pos + h;
    e.end = e.unknown ? limit : e.data + e.size;
    if (e.end > limit) e.end = limit;
    return true;
}
inline uint64_t get_uint(const uint8_t* p, size_t n) noexcept { uint64_t v = 0; for (size_t i = 0; i < n && i < 8; ++i) v = (v << 8) | p[i]; return v; }
inline double get_float(const uint8_t* p, size_t n) noexcept {
    if (n == 4) { uint32_t u = detail::get_u32be(p); float f; std::memcpy(&f, &u, 4); return f; }
    if (n == 8) { uint64_t u = detail::get_u64be(p); double d; std::memcpy(&d, &u, 8); return d; }
    return 0.0;
}
inline bool is_l1(uint32_t id) noexcept {
    return id == ids::Cluster || id == ids::Cues || id == ids::Tags || id == ids::Chapters || id == ids::Tracks || id == ids::Info ||
           id == ids::SeekHead || id == ids::Attachments || id == ids::Segment || id == ids::EBML;
}
inline bool str_is(const knst_byte_string& s, const char* lit) noexcept { size_t n = std::strlen(lit); return s.length() >= n && std::memcmp(s.data(), lit, n) == 0; }

struct ctx {
    demux_base* d = nullptr;
    uint64_t scale_ns = 1000000; double duration_ticks = 0.0; bool webm = false;
    knst_vector<uint32_t> nums;
    uint64_t laced_skipped = 0;
};

inline void parse_track_entry(const uint8_t* p, size_t n, ctx& c) noexcept {
    stream_desc sd; uint32_t num = 0, type = 0; uint64_t defdur = 0; knst_byte_string codec; double rate = 8000.0; uint32_t bits = 0;
    size_t pos = 0; elem e;
    while (pos < n) {
        uint32_t h = parse_head(p + pos, n - pos, e); if (!h || e.unknown) break;
        const uint8_t* body = p + pos + h; size_t bl = (size_t)(e.size < n - pos - h ? e.size : n - pos - h);
        switch (e.id) {
            case ids::TrackNumber: num = (uint32_t)get_uint(body, bl); break;
            case ids::TrackType: type = (uint32_t)get_uint(body, bl); break;
            case ids::CodecID: codec = knst_byte_string(body, (uint32_t)bl); break;
            case ids::CodecPrivate: if (bl <= (16u << 20)) sd.extradata = knst_byte_string(body, (uint32_t)bl); break;
            case ids::DefaultDuration: defdur = get_uint(body, bl); break;
            case ids::Language: { knst_byte_string z(body, (uint32_t)bl); z.push_back(0); sd.language = knst_c16string(reinterpret_cast<const char*>(z.data())); break; }
            case ids::Video: {
                size_t q = 0; elem v;
                while (q < bl) { uint32_t hh = parse_head(body + q, bl - q, v); if (!hh || v.unknown) break; size_t vl = (size_t)(v.size < bl - q - hh ? v.size : bl - q - hh);
                    if (v.id == ids::PixelWidth) sd.width = (uint32_t)get_uint(body + q + hh, vl); else if (v.id == ids::PixelHeight) sd.height = (uint32_t)get_uint(body + q + hh, vl);
                    q += hh + vl; }
                break;
            }
            case ids::Audio: {
                size_t q = 0; elem v;
                while (q < bl) { uint32_t hh = parse_head(body + q, bl - q, v); if (!hh || v.unknown) break; size_t vl = (size_t)(v.size < bl - q - hh ? v.size : bl - q - hh);
                    if (v.id == ids::SamplingFrequency) rate = get_float(body + q + hh, vl); else if (v.id == ids::Channels) sd.channels = (uint32_t)get_uint(body + q + hh, vl);
                    else if (v.id == ids::BitDepth) bits = (uint32_t)get_uint(body + q + hh, vl);
                    q += hh + vl; }
                break;
            }
            default: break;
        }
        pos += h + bl;
    }
    if (num == 0) return;
    knst_byte_string cz = codec; cz.push_back(0); sd.codec_name = knst_c16string(reinterpret_cast<const char*>(cz.data()));
    if (type == 1) {
        sd.kind = track_kind::Video;
        if (str_is(codec, "V_MJPEG")) sd.vcodec = video_codec::MJPEG;
        else if (str_is(codec, "V_MPEG4/ISO/AVC")) sd.vcodec = video_codec::H264;
        else if (str_is(codec, "V_MPEGH/ISO/HEVC")) sd.vcodec = video_codec::H265;
        else if (str_is(codec, "V_VP8")) sd.vcodec = video_codec::VP8;
        else if (str_is(codec, "V_VP9")) sd.vcodec = video_codec::VP9;
        else if (str_is(codec, "V_AV1")) sd.vcodec = video_codec::AV1;
        else if (str_is(codec, "V_MPEG4/ISO")) sd.vcodec = video_codec::MPEG4;
        else if (str_is(codec, "V_PRORES")) sd.vcodec = video_codec::ProRes;
        if (defdur) { uint32_t g = detail::gcd_u32(1000000000u, (uint32_t)(defdur > 0xFFFFFFFFull ? 0xFFFFFFFFull : defdur)); (void)g; detail::rational_from_fps(1e9 / (double)defdur, sd.fps_num, sd.fps_den); sd.fps = (double)sd.fps_num / sd.fps_den; }
    } else if (type == 2) {
        sd.kind = track_kind::Audio; sd.sample_rate = (uint32_t)(rate + 0.5); sd.bits_per_sample = bits;
        if (str_is(codec, "A_PCM/INT/LIT")) sd.acodec = bits == 8 ? audio_codec::PCM_U8 : bits == 16 ? audio_codec::PCM_S16LE : bits == 24 ? audio_codec::PCM_S24LE : bits == 32 ? audio_codec::PCM_S32LE : audio_codec::Unknown;
        else if (str_is(codec, "A_PCM/FLOAT/IEEE")) sd.acodec = bits == 32 ? audio_codec::PCM_F32LE : audio_codec::Unknown;
        else if (str_is(codec, "A_AAC")) sd.acodec = audio_codec::AAC;
        else if (str_is(codec, "A_OPUS")) sd.acodec = audio_codec::Opus;
        else if (str_is(codec, "A_VORBIS")) sd.acodec = audio_codec::Vorbis;
        else if (str_is(codec, "A_FLAC")) sd.acodec = audio_codec::FLAC;
        else if (str_is(codec, "A_MPEG/L3")) sd.acodec = audio_codec::MP3;
    } else sd.kind = (type == 17) ? track_kind::Subtitle : track_kind::Other;
    if (c.d->tracks.size() >= KNST_VIDEO_MAX_TRACKS) return;
    c.d->add_track(sd); c.nums.push_back(num);
}

inline void parse_chapters(const uint8_t* p, size_t n, ctx& c, int depth) noexcept {
    size_t pos = 0; elem e;
    while (pos < n && depth < 6) {
        uint32_t h = parse_head(p + pos, n - pos, e); if (!h || e.unknown) break;
        size_t bl = (size_t)(e.size < n - pos - h ? e.size : n - pos - h); const uint8_t* body = p + pos + h;
        if (e.id == ids::EditionEntry) parse_chapters(body, bl, c, depth + 1);
        else if (e.id == ids::ChapterAtom) {
            chapter ch; size_t q = 0; elem v;
            while (q < bl) { uint32_t hh = parse_head(body + q, bl - q, v); if (!hh || v.unknown) break;
                size_t vl = (size_t)(v.size < bl - q - hh ? v.size : bl - q - hh); const uint8_t* vb = body + q + hh;
                if (v.id == ids::ChapterTimeStart) ch.start_us = get_uint(vb, vl) / 1000; else if (v.id == ids::ChapterTimeEnd) ch.end_us = get_uint(vb, vl) / 1000;
                else if (v.id == ids::ChapterDisplay) {
                    size_t r = 0; elem w; while (r < vl) { uint32_t h3 = parse_head(vb + r, vl - r, w); if (!h3 || w.unknown) break;
                        size_t wl = (size_t)(w.size < vl - r - h3 ? w.size : vl - r - h3);
                        if (w.id == ids::ChapString) { knst_byte_string z(vb + r + h3, (uint32_t)wl); z.push_back(0); ch.title = knst_c16string(reinterpret_cast<const char*>(z.data())); }
                        r += h3 + wl; }
                }
                q += hh + vl; }
            c.d->meta.chapters.push_back(ch);
        }
        pos += h + bl;
    }
}

inline void add_block(ctx& c, uint64_t data_pos, uint64_t size, uint64_t cluster_tc, bool force_key, bool use_key, bool key_val, detail::file_handle& f) noexcept {
    uint8_t b[8]; size_t avail = size < 8 ? (size_t)size : 8;
    if (avail < 4 || !f.seek(data_pos) || f.read(b, avail) != avail) return;
    uint8_t m = 0x80; uint32_t tl = 1; while (tl <= 2 && !(b[0] & m)) { m >>= 1; ++tl; }
    if (tl > 2 || avail < tl + 3) return;
    uint32_t num = b[0] & (m - 1); if (tl == 2) num = (num << 8) | b[1];
    int16_t rel = (int16_t)((b[tl] << 8) | b[tl + 1]); uint8_t flags = b[tl + 2];
    const uint32_t hdr = tl + 3;
    if (flags & 0x06) { ++c.laced_skipped; return; }
    int ti = -1; for (uint32_t i = 0; i < c.nums.size(); ++i) if (c.nums[i] == num) { ti = (int)i; break; }
    if (ti < 0 || size < hdr) return;
    int64_t t = (int64_t)cluster_tc + rel; if (t < 0) t = 0;
    sample_ref s; s.offset = data_pos + hdr; s.size = (uint32_t)(size - hdr);
    s.pts_us = (uint64_t)t * c.scale_ns / 1000;
    s.key = use_key ? key_val : ((flags & 0x80) != 0 || force_key);
    auto& v = c.d->tracks[(uint32_t)ti].samples; if (v.size() < KNST_VIDEO_MAX_SAMPLES) v.push_back(s);
}

inline uint64_t parse_cluster(ctx& c, const elem& cl, uint64_t fsz) noexcept {
    detail::file_handle& f = c.d->file; uint64_t tc = 0, p = cl.data; const uint64_t end = cl.unknown ? fsz : cl.end;
    elem e;
    while (p < end && read_elem(f, p, end, e)) {
        if (cl.unknown && is_l1(e.id)) return p;
        if (e.id == ids::Timecode) { uint8_t b[8]; size_t n = e.size < 8 ? (size_t)e.size : 8; if (f.seek(e.data) && f.read(b, n) == n) tc = get_uint(b, n); }
        else if (e.id == ids::SimpleBlock) add_block(c, e.data, e.end - e.data, tc, false, false, false, f);
        else if (e.id == ids::BlockGroup) {
            uint64_t q = e.data; elem g; uint64_t bpos = 0, bsize = 0; bool have_ref = false, have_block = false;
            while (q < e.end && read_elem(f, q, e.end, g)) {
                if (g.id == ids::Block) { bpos = g.data; bsize = g.end - g.data; have_block = true; }
                else if (g.id == ids::ReferenceBlock) have_ref = true;
                q = g.end;
            }
            if (have_block) add_block(c, bpos, bsize, tc, false, true, !have_ref, f);
        }
        if (e.end <= p) break;
        p = e.end;
    }
    return end;
}

} // namespace detail_

inline errc parse(demux_base& d) noexcept {
    using namespace detail_;
    detail::file_handle& f = d.file; const uint64_t fsz = f.size();
    elem e;
    if (!read_elem(f, 0, fsz, e) || e.id != ids::EBML || e.unknown || e.size > 4096) return errc::Corrupt;
    ctx c; c.d = &d;
    {
        uint8_t hb[4096]; size_t n = (size_t)e.size;
        if (!f.seek(e.data) || f.read(hb, n) != n) return errc::Corrupt;
        size_t pos = 0; elem v;
        while (pos < n) { uint32_t h = parse_head(hb + pos, n - pos, v); if (!h || v.unknown) break; size_t vl = (size_t)(v.size < n - pos - h ? v.size : n - pos - h);
            if (v.id == ids::DocType) c.webm = (vl == 4 && std::memcmp(hb + pos + h, "webm", 4) == 0);
            pos += h + vl; }
    }
    uint64_t pos = e.end;
    elem seg;
    if (!read_elem(f, pos, fsz, seg) || seg.id != ids::Segment) return errc::Corrupt;
    const uint64_t seg_end = seg.unknown ? fsz : seg.end;
    pos = seg.data;
    bool have_tracks = false;
    while (pos < seg_end && read_elem(f, pos, seg_end, e)) {
        const bool small_master = (e.id == ids::Info || e.id == ids::Tracks || e.id == ids::Chapters);
        if (small_master && !e.unknown && e.size <= (64u << 20)) {
            knst_byte_string buf; if (detail::alloc_bytes(buf, e.size) != errc::Ok) return errc::OutOfMemory;
            if (e.size && (!f.seek(e.data) || !f.read_exact(&buf[0], (size_t)e.size))) return errc::Corrupt;
            const uint8_t* b = buf.data(); size_t n = buf.length();
            if (e.id == ids::Info) {
                size_t q = 0; elem v;
                while (q < n) { uint32_t h = parse_head(b + q, n - q, v); if (!h || v.unknown) break; size_t vl = (size_t)(v.size < n - q - h ? v.size : n - q - h);
                    if (v.id == ids::TimecodeScale) { c.scale_ns = get_uint(b + q + h, vl); if (c.scale_ns == 0) c.scale_ns = 1000000; }
                    else if (v.id == ids::Duration) c.duration_ticks = get_float(b + q + h, vl);
                    else if (v.id == ids::Title) { knst_byte_string z(b + q + h, (uint32_t)vl); z.push_back(0); d.meta.title = knst_c16string(reinterpret_cast<const char*>(z.data())); }
                    else if (v.id == ids::WritingApp) { knst_byte_string z(b + q + h, (uint32_t)vl); z.push_back(0); d.meta.software = knst_c16string(reinterpret_cast<const char*>(z.data())); }
                    q += h + vl; }
            } else if (e.id == ids::Tracks) {
                size_t q = 0; elem v;
                while (q < n) { uint32_t h = parse_head(b + q, n - q, v); if (!h || v.unknown) break; size_t vl = (size_t)(v.size < n - q - h ? v.size : n - q - h);
                    if (v.id == ids::TrackEntry) parse_track_entry(b + q + h, vl, c);
                    q += h + vl; }
                have_tracks = true;
            } else parse_chapters(b, n, c, 0);
            pos = e.end;
        } else if (e.id == ids::Cluster) {
            if (!have_tracks) return errc::Corrupt;
            pos = parse_cluster(c, e, fsz);
            if (!e.unknown) pos = e.end;
        } else {
            if (e.unknown) break;
            pos = e.end;
        }
    }
    if (d.tracks.empty()) return errc::Corrupt;

    uint64_t dur = (uint64_t)(c.duration_ticks * (double)c.scale_ns / 1000.0);
    for (uint32_t t = 0; t < d.tracks.size(); ++t) {
        track_data& td = d.tracks[t]; stream_desc& sd = td.desc; const uint32_t n = td.samples.size();
        uint64_t units = 0;
        for (uint32_t i = 0; i < n; ++i) {
            sample_ref& s = td.samples[i]; s.unit_start = units;
            if (sd.kind == track_kind::Audio && audio_codec_is_pcm(sd.acodec) && sd.channels && sd.bits_per_sample) {
                s.units = s.size / (sd.channels * (sd.bits_per_sample / 8)); units += s.units;
            } else { s.units = 1; units += 1; }
        }
        sd.frame_count = units;
        if (n) { uint64_t last = td.samples[n - 1].pts_us; if (last > sd.duration_us) sd.duration_us = last; }
        if (sd.kind == track_kind::Video && sd.fps_num == 0 && n > 1 && td.samples[n - 1].pts_us > td.samples[0].pts_us) {
            double fps = (double)(n - 1) * 1e6 / (double)(td.samples[n - 1].pts_us - td.samples[0].pts_us);
            detail::rational_from_fps(fps, sd.fps_num, sd.fps_den); sd.fps = fps;
        }
        if (sd.duration_us > dur) dur = sd.duration_us;
        if (c.duration_ticks == 0.0 && sd.duration_us > d.duration_us) d.duration_us = sd.duration_us;
    }
    if (dur) d.duration_us = dur;
    d.kind = c.webm ? container::WebM : container::MKV;
    return errc::Ok;
}

} // namespace mkv

// ============================================================
// MP4 / MOV  (progressive files; moov is written/read as one block)
// ============================================================

namespace mp4 {

class writer {
public:
    writer() noexcept = default;
    ~writer() { close(); }
    writer(const writer&) = delete;
    writer& operator=(const writer&) = delete;

    // mov = true -> QuickTime brand, 'jpeg' sample entry, optional PCM_S16LE audio ('sowt').
    // mov = false -> ISO MP4: MJPEG as 'mp4v' (OTI 0x6C), H.264 'avc1' (vextra = avcC), H.265 'hvc1' (vextra = hvcC); video only.
    result<void> open(const knst_c16string& path, const writer_params& p, bool mov = false) noexcept {
        close();
        err_ = errc::Ok; v_.clear(); a_.clear(); a_frames_ = 0; any_cts_ = false;
        if (path.empty() || !p.has_video || p.width == 0 || p.height == 0 || p.fps_num == 0 || p.fps_den == 0) return fail_(errc::InvalidArgument);
        if (p.width > 65535 || p.height > 65535) return fail_(errc::InvalidArgument);
        if (p.vcodec != video_codec::MJPEG && p.vcodec != video_codec::H264 && p.vcodec != video_codec::H265) return fail_(errc::NotSupported);
        if ((p.vcodec == video_codec::H264 || p.vcodec == video_codec::H265) && p.vextra.empty()) return fail_(errc::InvalidArgument);
        if (p.acodec != audio_codec::None) {
            if (!mov || p.acodec != audio_codec::PCM_S16LE) return fail_(errc::NotSupported);
            if (p.channels < 1 || p.channels > 8 || p.sample_rate < 1 || p.sample_rate > 65535) return fail_(errc::InvalidArgument);
        }
        p_ = p; mov_ = mov;
        if (!knst_file::ensure_parent_dir(path)) return fail_(errc::IoError);
        errc e = f_.open(path, detail::file_handle::mode::write);
        if (e != errc::Ok) return fail_(e);
        detail::wbuf b;
        uint32_t ft = b.box_begin("ftyp");
        if (mov) { b.tag("qt  "); b.u32be(0x200); b.tag("qt  "); }
        else { b.tag("isom"); b.u32be(0x200); b.tag("isom"); b.tag("iso2"); b.tag("mp41"); }
        b.box_end(ft);
        mdat_pos_ = b.size();
        b.u32be(1); b.tag("mdat"); b.u64be(0);
        if (!b.ok() || !f_.write(b.data(), b.size())) { f_.close(); return fail_(errc::IoError); }
        return ok_void();
    }

    // dts_us: decode timestamp. Pass it for streams with B-frames (packets are written in decode order); ~0 = use pts_us.
    result<void> write_video(const uint8_t* data, uint32_t size, uint64_t pts_us, bool keyframe = true, uint64_t dts_us = ~0ull) noexcept {
        if (!f_.is_open()) return fail_(errc::BadState);
        if (!data || size == 0) return fail_(errc::InvalidArgument);
        uint64_t ts = detail::muldiv_u64(dts_us == ~0ull ? pts_us : dts_us, 90000ull, 1000000ull);
        if (v_.ts.size() && ts <= v_.ts.back()) ts = v_.ts.back() + 1;
        const uint64_t pts_ts = detail::muldiv_u64(pts_us, 90000ull, 1000000ull);
        const uint32_t cts = pts_ts > ts ? (uint32_t)(pts_ts - ts) : 0;
        if (cts) any_cts_ = true;
        v_.cts.push_back(cts);
        const uint64_t off = f_.tell();
        if (!f_.write(data, size)) return fail_(errc::IoError);
        v_.ts.push_back(ts); v_.sizes.push_back(size); v_.offsets.push_back(off);
        if (keyframe) v_.keys.push_back(v_.sizes.size());     // 1-based
        return ok_void();
    }
    result<void> write_audio(const uint8_t* data, uint32_t size) noexcept {
        if (!f_.is_open() || p_.acodec == audio_codec::None) return fail_(errc::BadState);
        const uint32_t align = p_.channels * 2;
        if (!data || size == 0 || size % align) return fail_(errc::InvalidArgument);
        const uint64_t off = f_.tell();
        if (!f_.write(data, size)) return fail_(errc::IoError);
        a_.offsets.push_back(off); a_.sizes.push_back(size / align); a_frames_ += size / align;
        return ok_void();
    }

    result<void> close() noexcept {
        if (!f_.is_open()) return ok_void();
        errc e = errc::Ok;
        const uint64_t data_end = f_.tell();
        if (v_.sizes.empty()) e = errc::BadState;
        detail::wbuf m;
        if (e == errc::Ok) e = build_moov(m);
        if (e == errc::Ok && (!f_.write(m.data(), m.size()))) e = errc::IoError;
        if (e == errc::Ok) {
            uint8_t t[8]; for (int i = 0; i < 8; ++i) t[i] = (uint8_t)((data_end - mdat_pos_) >> (56 - 8 * i));
            if (!f_.seek(mdat_pos_ + 8) || !f_.write(t, 8)) e = errc::IoError;
        }
        if (!f_.close() && e == errc::Ok) e = errc::IoError;
        if (e != errc::Ok) err_ = e;
        return void_result(e);
    }
    uint64_t video_frames() const noexcept { return v_.sizes.size(); }
    uint64_t bytes_written() const noexcept { return f_.tell(); }
    errc last_error() const noexcept { return err_; }

private:
    struct vtrack { knst_vector<uint64_t> ts, offsets; knst_vector<uint32_t> sizes, keys, cts;
        void clear() { ts.clear(); offsets.clear(); sizes.clear(); keys.clear(); cts.clear(); } };
    struct atrack { knst_vector<uint64_t> offsets; knst_vector<uint32_t> sizes;   // sizes = PCM frames per chunk
        void clear() { offsets.clear(); sizes.clear(); } };
    detail::file_handle f_; writer_params p_; bool mov_ = false;
    vtrack v_; atrack a_; bool any_cts_ = false; uint64_t a_frames_ = 0; uint64_t mdat_pos_ = 0;
    errc err_ = errc::Ok;

    result<void> fail_(errc e) noexcept { err_ = e; return void_result(e); }

    static void matrix(detail::wbuf& b) noexcept { for (int i = 0; i < 9; ++i) b.u32be((i == 0 || i == 4) ? 0x00010000u : (i == 8 ? 0x40000000u : 0u)); }

    void chunk_offsets(detail::wbuf& b, const knst_vector<uint64_t>& off) noexcept {
        bool big = false; for (uint32_t i = 0; i < off.size(); ++i) if (off[i] > 0xFFFFFFFFull) { big = true; break; }
        uint32_t bx = b.fullbox_begin(big ? "co64" : "stco", 0, 0); b.u32be(off.size());
        for (uint32_t i = 0; i < off.size(); ++i) { if (big) b.u64be(off[i]); else b.u32be((uint32_t)off[i]); }
        b.box_end(bx);
    }
    void track_head(detail::wbuf& b, uint32_t id, uint64_t dur_ms, bool video) noexcept {
        // tkhd is in movie timescale (1000 = ms); v1 is needed above
        // ~49.7 days. Practically rare, but a real overflow, so support it.
        if (dur_ms > 0xFFFFFFFFull) {
            uint32_t t = b.fullbox_begin("tkhd", 1, 7);
            b.u64be(0); b.u64be(0); b.u32be(id); b.u32be(0); b.u64be(dur_ms); b.zeros(8);
            b.u16be(0); b.u16be(0); b.u16be(video ? 0 : 0x0100); b.u16be(0); matrix(b);
            b.u32be(video ? p_.width << 16 : 0); b.u32be(video ? p_.height << 16 : 0);
            b.box_end(t);
            return;
        }
        uint32_t t = b.fullbox_begin("tkhd", 0, 7);
        b.u32be(0); b.u32be(0); b.u32be(id); b.u32be(0); b.u32be((uint32_t)dur_ms); b.zeros(8);
        b.u16be(0); b.u16be(0); b.u16be(video ? 0 : 0x0100); b.u16be(0); matrix(b);
        b.u32be(video ? p_.width << 16 : 0); b.u32be(video ? p_.height << 16 : 0);
        b.box_end(t);
    }
    void media_head(detail::wbuf& b, uint32_t timescale, uint64_t dur, bool video) noexcept {
        // Video mdhd is in 90 kHz — v0 caps at ~13.25 hours. Switch to v1
        // before overflow so long recordings keep the correct duration.
        if (dur > 0xFFFFFFFFull) {
            uint32_t m = b.fullbox_begin("mdhd", 1, 0);
            b.u64be(0); b.u64be(0); b.u32be(timescale); b.u64be(dur); b.u16be(0x55C4); b.u16be(0); b.box_end(m);
        } else {
            uint32_t m = b.fullbox_begin("mdhd", 0, 0);
            b.u32be(0); b.u32be(0); b.u32be(timescale); b.u32be((uint32_t)dur); b.u16be(0x55C4); b.u16be(0); b.box_end(m);
        }
        uint32_t h = b.fullbox_begin("hdlr", 0, 0);
        b.u32be(mov_ ? 0x6D686C72u : 0u); b.tag(video ? "vide" : "soun"); b.zeros(12);
        const char* nm = video ? "VideoHandler" : "SoundHandler"; b.put(nm, std::strlen(nm) + 1); b.box_end(h);
    }

    errc build_moov(detail::wbuf& b) noexcept {
        const uint64_t last_ts = v_.ts.back();
        const uint64_t last_delta = v_.ts.size() > 1 ? v_.ts.back() - v_.ts[v_.ts.size() - 2] : 90000ull * p_.fps_den / p_.fps_num;
        const uint64_t vdur = last_ts + last_delta;
        const uint64_t vdur_ms = vdur * 1000 / 90000;
        const uint64_t adur_ms = (p_.acodec != audio_codec::None) ? a_frames_ * 1000 / p_.sample_rate : 0;
        const uint64_t mdur_ms = vdur_ms > adur_ms ? vdur_ms : adur_ms;
        const bool has_a = p_.acodec != audio_codec::None && !a_.sizes.empty();

        uint32_t moov = b.box_begin("moov");
        // mvhd in movie timescale (1000 = ms). v1 above ~49.7 days.
        if (mdur_ms > 0xFFFFFFFFull) {
            uint32_t mv = b.fullbox_begin("mvhd", 1, 0);
            b.u64be(0); b.u64be(0); b.u32be(1000); b.u64be(mdur_ms); b.u32be(0x00010000); b.u16be(0x0100); b.u16be(0); b.zeros(8);
            matrix(b); b.zeros(24); b.u32be(has_a ? 3 : 2); b.box_end(mv);
        } else {
            uint32_t mv = b.fullbox_begin("mvhd", 0, 0);
            b.u32be(0); b.u32be(0); b.u32be(1000); b.u32be((uint32_t)mdur_ms); b.u32be(0x00010000); b.u16be(0x0100); b.u16be(0); b.zeros(8);
            matrix(b); b.zeros(24); b.u32be(has_a ? 3 : 2); b.box_end(mv);
        }

        // ---- video trak ----
        {
            uint32_t trak = b.box_begin("trak");
            track_head(b, 1, (uint32_t)mdur_ms, true);
            uint32_t mdia = b.box_begin("mdia"); media_head(b, 90000, vdur, true);
            uint32_t minf = b.box_begin("minf");
            uint32_t vm = b.fullbox_begin("vmhd", 0, 1); b.zeros(8); b.box_end(vm);
            dinf(b);
            uint32_t stbl = b.box_begin("stbl");
            // stsd
            uint32_t sd = b.fullbox_begin("stsd", 0, 0); b.u32be(1);
            const char* fmt = mov_ ? "jpeg" : "mp4v";
            if (p_.vcodec == video_codec::H264) fmt = "avc1"; else if (p_.vcodec == video_codec::H265) fmt = "hvc1";
            uint32_t en = b.box_begin(fmt);
            b.zeros(6); b.u16be(1); b.u16be(0); b.u16be(0); b.u32be(0); b.u32be(0); b.u32be(0);
            b.u16be(p_.width); b.u16be(p_.height); b.u32be(0x00480000); b.u32be(0x00480000); b.u32be(0); b.u16be(1);
            { uint8_t name[32] = {0}; const char* nm = "knst_vid"; name[0] = 8; std::memcpy(name + 1, nm, 8); b.put(name, 32); }
            b.u16be(24); b.u16be(0xFFFF);
            if (p_.vcodec == video_codec::H264) { uint32_t x = b.box_begin("avcC"); b.put(p_.vextra.data(), p_.vextra.length()); b.box_end(x); }
            else if (p_.vcodec == video_codec::H265) { uint32_t x = b.box_begin("hvcC"); b.put(p_.vextra.data(), p_.vextra.length()); b.box_end(x); }
            else if (!mov_) {
                uint32_t x = b.fullbox_begin("esds", 0, 0);
                b.u8(0x03); b.u8(21); b.u16be(1); b.u8(0);
                b.u8(0x04); b.u8(13); b.u8(0x6C); b.u8(0x11); b.u24be(0); b.u32be(0); b.u32be(0);
                b.u8(0x06); b.u8(1); b.u8(0x02);
                b.box_end(x);
            }
            b.box_end(en); b.box_end(sd);
            // stts (run-length)
            {
                uint32_t st = b.fullbox_begin("stts", 0, 0); uint32_t cnt_pos = b.size(); b.u32be(0);
                uint32_t entries = 0; uint32_t n = v_.ts.size(); uint32_t i = 0;
                while (i < n) {
                    uint64_t delta = (i + 1 < n) ? v_.ts[i + 1] - v_.ts[i] : last_delta; uint32_t run = 1;
                    while (i + run < n) { uint64_t d2 = (i + run + 1 < n) ? v_.ts[i + run + 1] - v_.ts[i + run] : last_delta; if (d2 != delta) break; ++run; }
                    b.u32be(run); b.u32be((uint32_t)delta); ++entries; i += run;
                }
                b.set_u32be(cnt_pos, entries); b.box_end(st);
            }
            if (any_cts_) {      // composition offsets (B-frames): one entry per sample, run-length encoded
                uint32_t ct = b.fullbox_begin("ctts", 0, 0); uint32_t cp = b.size(); b.u32be(0); uint32_t entries = 0; uint32_t n = v_.cts.size();
                for (uint32_t i = 0; i < n;) { uint32_t run = 1; while (i + run < n && v_.cts[i + run] == v_.cts[i]) ++run; b.u32be(run); b.u32be(v_.cts[i]); ++entries; i += run; }
                b.set_u32be(cp, entries); b.box_end(ct);
            }
            if (p_.vcodec != video_codec::MJPEG && v_.keys.size() < v_.sizes.size()) {
                uint32_t ss = b.fullbox_begin("stss", 0, 0); b.u32be(v_.keys.size());
                for (uint32_t i = 0; i < v_.keys.size(); ++i) b.u32be(v_.keys[i]);
                b.box_end(ss);
            }
            uint32_t sc = b.fullbox_begin("stsc", 0, 0); b.u32be(1); b.u32be(1); b.u32be(1); b.u32be(1); b.box_end(sc);
            uint32_t sz = b.fullbox_begin("stsz", 0, 0); b.u32be(0); b.u32be(v_.sizes.size());
            for (uint32_t i = 0; i < v_.sizes.size(); ++i) b.u32be(v_.sizes[i]);
            b.box_end(sz);
            chunk_offsets(b, v_.offsets);
            b.box_end(stbl); b.box_end(minf); b.box_end(mdia); b.box_end(trak);
        }
        // ---- audio trak ----
        if (has_a) {
            uint32_t trak = b.box_begin("trak");
            track_head(b, 2, (uint32_t)adur_ms, false);
            uint32_t mdia = b.box_begin("mdia"); media_head(b, p_.sample_rate, a_frames_, false);
            uint32_t minf = b.box_begin("minf");
            uint32_t sm = b.fullbox_begin("smhd", 0, 0); b.u32be(0); b.box_end(sm);
            dinf(b);
            uint32_t stbl = b.box_begin("stbl");
            uint32_t sd = b.fullbox_begin("stsd", 0, 0); b.u32be(1);
            uint32_t en = b.box_begin("sowt");
            b.zeros(6); b.u16be(1); b.u16be(0); b.u16be(0); b.u32be(0); b.u16be(p_.channels); b.u16be(16); b.u16be(0); b.u16be(0); b.u32be(p_.sample_rate << 16);
            b.box_end(en); b.box_end(sd);
            uint32_t st = b.fullbox_begin("stts", 0, 0); b.u32be(1); b.u32be((uint32_t)a_frames_); b.u32be(1); b.box_end(st);
            { // stsc run-length by frames per chunk
                uint32_t sc = b.fullbox_begin("stsc", 0, 0); uint32_t cp = b.size(); b.u32be(0); uint32_t entries = 0; uint32_t prev = 0;
                for (uint32_t i = 0; i < a_.sizes.size(); ++i) if (a_.sizes[i] != prev) { b.u32be(i + 1); b.u32be(a_.sizes[i]); b.u32be(1); prev = a_.sizes[i]; ++entries; }
                b.set_u32be(cp, entries); b.box_end(sc);
            }
            uint32_t sz = b.fullbox_begin("stsz", 0, 0); b.u32be(p_.channels * 2); b.u32be((uint32_t)a_frames_); b.box_end(sz);
            chunk_offsets(b, a_.offsets);
            b.box_end(stbl); b.box_end(minf); b.box_end(mdia); b.box_end(trak);
        }
        // ---- metadata (title) ----
        if (!p_.meta.title.empty()) {
            knst_byte_string t = detail::utf8_of(p_.meta.title);
            uint32_t udta = b.box_begin("udta"); uint32_t meta = b.fullbox_begin("meta", 0, 0);
            uint32_t hd = b.fullbox_begin("hdlr", 0, 0); b.u32be(0); b.tag("mdir"); b.tag("appl"); b.zeros(9); b.box_end(hd);
            uint32_t il = b.box_begin("ilst"); uint32_t nm = b.box_begin("\xA9nam");
            uint32_t dt = b.box_begin("data"); b.u32be(1); b.u32be(0); b.put(t.data(), t.length()); b.box_end(dt);
            b.box_end(nm); b.box_end(il); b.box_end(meta); b.box_end(udta);
        }
        b.box_end(moov);
        return b.ok() ? errc::Ok : errc::OutOfMemory;
    }
    static void dinf(detail::wbuf& b) noexcept {
        uint32_t di = b.box_begin("dinf"); uint32_t dr = b.fullbox_begin("dref", 0, 0); b.u32be(1);
        uint32_t u = b.fullbox_begin("url ", 0, 1); b.box_end(u); b.box_end(dr); b.box_end(di);
    }
};

namespace detail_ {

struct tk {
    uint32_t timescale = 0; uint64_t mdhd_dur = 0; uint32_t handler = 0; uint32_t tw = 0, th = 0; bool have_stsd = false;
    stream_desc sd; uint32_t sample_size = 0, sample_count = 0; bool have_stsz = false;
    knst_vector<uint32_t> stts_n, stts_d, stsc_first, stsc_n, sizes, keys, ctts_n; knst_vector<int32_t> ctts_o; knst_vector<uint64_t> offsets; bool have_stss = false;
};
struct ctx {
    demux_base* d = nullptr; tk* cur = nullptr; uint32_t movie_ts = 1000; uint64_t movie_dur = 0; errc err = errc::Ok;
};

inline int esds_oti(const uint8_t* p, size_t n) noexcept {      // p = esds payload after the 4-byte version/flags
    size_t pos = 0;
    auto rd_len = [&](uint32_t& len) { len = 0; for (int i = 0; i < 4 && pos < n; ++i) { uint8_t b = p[pos++]; len = (len << 7) | (b & 0x7F); if (!(b & 0x80)) return true; } return false; };
    while (pos < n) {
        uint8_t tag = p[pos++]; uint32_t len; if (!rd_len(len)) return -1;
        if (tag == 0x03) { if (pos + 3 > n) return -1; uint8_t fl = p[pos + 2]; pos += 3; if (fl & 0x80) pos += 2; if (fl & 0x40) { if (pos >= n) return -1; pos += 1 + p[pos]; } if (fl & 0x20) pos += 2; continue; }
        if (tag == 0x04) return pos < n ? p[pos] : -1;
        pos += len;
    }
    return -1;
}

inline void parse_stsd(const uint8_t* p, size_t n, tk& t) noexcept {
    if (n < 16 + 8) return;
    const uint8_t* en = p + 8; size_t el = n - 8;     // first entry (size u32, format u32, ...)
    uint32_t esz = detail::get_u32be(en); if (esz < 8 || esz > el) esz = (uint32_t)el;
    const uint32_t fmt = detail::get_u32le(en + 4); t.sd.codec_tag = fmt;
    using detail::fcc;
    size_t child = 0;
    if (t.handler == fcc('v','i','d','e')) {
        t.sd.kind = track_kind::Video;
        if (esz >= 86) { t.sd.width = detail::get_u16be(en + 32); t.sd.height = detail::get_u16be(en + 34); child = 86; }
        if (fmt == fcc('j','p','e','g') || fmt == fcc('m','j','p','a') || fmt == fcc('m','j','p','b') || fmt == fcc('A','V','D','J')) t.sd.vcodec = video_codec::MJPEG;
        else if (fmt == fcc('a','v','c','1') || fmt == fcc('a','v','c','3')) t.sd.vcodec = video_codec::H264;
        else if (fmt == fcc('h','v','c','1') || fmt == fcc('h','e','v','1')) t.sd.vcodec = video_codec::H265;
        else if (fmt == fcc('v','p','0','8')) t.sd.vcodec = video_codec::VP8;
        else if (fmt == fcc('v','p','0','9')) t.sd.vcodec = video_codec::VP9;
        else if (fmt == fcc('a','v','0','1')) t.sd.vcodec = video_codec::AV1;
        else if (fmt == fcc('m','p','4','v')) t.sd.vcodec = video_codec::MPEG4;
        else if (fmt == fcc('a','p','c','h') || fmt == fcc('a','p','c','n') || fmt == fcc('a','p','c','s') || fmt == fcc('a','p','c','o') || fmt == fcc('a','p','4','h') || fmt == fcc('a','p','4','x')) t.sd.vcodec = video_codec::ProRes;
    } else if (t.handler == fcc('s','o','u','n')) {
        t.sd.kind = track_kind::Audio;
        if (esz >= 36) {
            uint32_t ver = detail::get_u16be(en + 16);
            t.sd.channels = detail::get_u16be(en + 24); t.sd.bits_per_sample = detail::get_u16be(en + 26); t.sd.sample_rate = detail::get_u32be(en + 32) >> 16;
            child = 36 + (ver == 1 ? 16 : 0); if (ver == 2) child = 0;
        }
        if (fmt == fcc('s','o','w','t')) t.sd.acodec = audio_codec::PCM_S16LE;
        else if (fmt == fcc('r','a','w',' ') && t.sd.bits_per_sample == 8) t.sd.acodec = audio_codec::PCM_U8;
        else if (fmt == fcc('m','p','4','a')) t.sd.acodec = audio_codec::AAC;
        else if (fmt == fcc('O','p','u','s')) t.sd.acodec = audio_codec::Opus;
        else if (fmt == fcc('.','m','p','3') || fmt == fcc('m','p','3',' ')) t.sd.acodec = audio_codec::MP3;
        else if (fmt == fcc('f','L','a','C')) t.sd.acodec = audio_codec::FLAC;
    } else return;
    t.have_stsd = true;
    for (size_t pos = child; child && pos + 8 <= esz;) {
        uint32_t cs = detail::get_u32be(en + pos); if (cs < 8 || pos + cs > esz) break;
        const uint8_t* cb = en + pos + 8; size_t cl = cs - 8; uint32_t ct = detail::get_u32le(en + pos + 4);
        if (ct == fcc('a','v','c','C') || ct == fcc('h','v','c','C')) t.sd.extradata = knst_byte_string(cb, (uint32_t)cl);
        else if (ct == fcc('e','s','d','s') && cl > 4) {
            t.sd.extradata = knst_byte_string(cb + 4, (uint32_t)(cl - 4));
            int oti = esds_oti(cb + 4, cl - 4);
            if (fmt == fcc('m','p','4','v') && oti == 0x6C) t.sd.vcodec = video_codec::MJPEG;
            if (fmt == fcc('m','p','4','a') && (oti == 0x6B || oti == 0x69)) t.sd.acodec = audio_codec::MP3;
        }
        pos += cs;
    }
}

inline void finalize_track(ctx& c, tk& t) noexcept {
    if (!t.have_stsd || !t.have_stsz || t.timescale == 0 || t.offsets.empty()) return;
    if (c.d->tracks.size() >= KNST_VIDEO_MAX_TRACKS) return;
    stream_desc sd = t.sd;
    if (sd.kind == track_kind::Video && (sd.width == 0 || sd.height == 0)) { sd.width = t.tw; sd.height = t.th; }
    const bool pcm = sd.kind == track_kind::Audio && audio_codec_is_pcm(sd.acodec) && t.sample_size > 0 && sd.channels && sd.bits_per_sample;
    const uint32_t N = t.sample_count;
    if (!pcm && N > KNST_VIDEO_MAX_SAMPLES) { c.err = errc::TooLarge; return; }
    const uint32_t ti = c.d->add_track(sd);
    track_data& td = c.d->tracks[ti];
    const uint64_t fsz = c.d->file.size();
    uint32_t s = 0, stsc_i = 0, stts_i = 0, stts_left = t.stts_n.size() ? t.stts_n[0] : 0, ctts_i = 0, ctts_left = t.ctts_n.size() ? t.ctts_n[0] : 0, key_i = 0;
    uint64_t dts = 0, units = 0;
    for (uint32_t ch = 0; ch < t.offsets.size() && s < N; ++ch) {
        while (stsc_i + 1 < t.stsc_first.size() && t.stsc_first[stsc_i + 1] <= ch + 1) ++stsc_i;
        const uint32_t spc = t.stsc_first.size() ? t.stsc_n[stsc_i] : 1;
        uint64_t off = t.offsets[ch];
        if (pcm) {
            uint32_t n = spc < N - s ? spc : N - s;
            const uint64_t bytes = (uint64_t)n * t.sample_size;
            if (off + bytes > fsz || bytes > 0xFFFFFFFFull) { s = N; break; }
            sample_ref r; r.offset = off; r.size = (uint32_t)bytes; r.units = (uint32_t)(bytes / (sd.channels * (sd.bits_per_sample / 8))); r.unit_start = units;
            r.pts_us = detail::muldiv_u64(units, 1000000ull, sd.sample_rate ? sd.sample_rate : t.timescale);
            units += r.units; td.samples.push_back(r); s += n; continue;
        }
        for (uint32_t j = 0; j < spc && s < N; ++j, ++s) {
            const uint32_t sz = t.sample_size ? t.sample_size : (s < t.sizes.size() ? t.sizes[s] : 0);
            sample_ref r; r.offset = off; r.size = sz; off += sz;
            if (r.offset + sz > fsz) { s = N; break; }
            int64_t pts = (int64_t)dts;
            if (ctts_i < t.ctts_n.size()) { pts += t.ctts_o[ctts_i]; if (--ctts_left == 0 && ++ctts_i < t.ctts_n.size()) ctts_left = t.ctts_n[ctts_i]; }
            if (pts < 0) pts = 0;
            r.pts_us = detail::muldiv_u64((uint64_t)pts, 1000000ull, t.timescale);
            if (t.ctts_n.size()) r.dts_us = detail::muldiv_u64(dts, 1000000ull, t.timescale);
            if (stts_i < t.stts_n.size()) { dts += t.stts_d[stts_i]; if (--stts_left == 0 && ++stts_i < t.stts_n.size()) stts_left = t.stts_n[stts_i]; }
            if (sd.kind == track_kind::Video && t.have_stss) { while (key_i < t.keys.size() && t.keys[key_i] < s + 1) ++key_i; r.key = (key_i < t.keys.size() && t.keys[key_i] == s + 1); }
            r.unit_start = units; r.units = 1; ++units;
            td.samples.push_back(r);
        }
    }
    td.desc.frame_count = pcm ? units : td.samples.size();
    td.desc.duration_us = detail::muldiv_u64(t.mdhd_dur, 1000000ull, t.timescale);
    if (sd.kind == track_kind::Video && td.desc.duration_us) {
        double fps = (double)td.samples.size() * 1e6 / (double)td.desc.duration_us;
        detail::rational_from_fps(fps, td.desc.fps_num, td.desc.fps_den); td.desc.fps = fps;
    }
}

inline void walk(const uint8_t* p, size_t n, ctx& c, int depth) noexcept {
    using detail::fcc;
    if (depth > 12) return;
    for (size_t pos = 0; pos + 8 <= n;) {
        uint64_t sz = detail::get_u32be(p + pos); uint32_t ty = detail::get_u32le(p + pos + 4); size_t hdr = 8;
        if (sz == 1) { if (pos + 16 > n) break; sz = detail::get_u64be(p + pos + 8); hdr = 16; }
        else if (sz == 0) sz = n - pos;
        if (sz < hdr || pos + sz > n) break;
        const uint8_t* b = p + pos + hdr; const size_t bl = (size_t)(sz - hdr);
        tk* t = c.cur;
        if (ty == fcc('m','o','o','v') || ty == fcc('m','d','i','a') || ty == fcc('m','i','n','f') || ty == fcc('s','t','b','l') || ty == fcc('u','d','t','a') || ty == fcc('i','l','s','t')) walk(b, bl, c, depth + 1);
        else if (ty == fcc('t','r','a','k')) { tk tr; tk* saved = c.cur; c.cur = &tr; walk(b, bl, c, depth + 1); finalize_track(c, tr); c.cur = saved; }
        else if (ty == fcc('m','e','t','a') && bl > 4) walk(b + 4, bl - 4, c, depth + 1);
        else if (ty == fcc('m','v','h','d') && bl >= 20) { if (b[0] == 1 && bl >= 32) { c.movie_ts = detail::get_u32be(b + 20); c.movie_dur = detail::get_u64be(b + 24); } else { c.movie_ts = detail::get_u32be(b + 12); c.movie_dur = detail::get_u32be(b + 16); } }
        else if (t && ty == fcc('t','k','h','d')) { if (b[0] == 1 && bl >= 96) { t->tw = detail::get_u32be(b + 88) >> 16; t->th = detail::get_u32be(b + 92) >> 16; } else if (bl >= 84) { t->tw = detail::get_u32be(b + 76) >> 16; t->th = detail::get_u32be(b + 80) >> 16; } }
        else if (t && ty == fcc('m','d','h','d') && bl >= 24) {
            if (b[0] == 1 && bl >= 32) { t->timescale = detail::get_u32be(b + 20); t->mdhd_dur = detail::get_u64be(b + 24); }
            else { t->timescale = detail::get_u32be(b + 12); t->mdhd_dur = detail::get_u32be(b + 16); }
        }
        else if (t && ty == fcc('h','d','l','r') && bl >= 12) { if (t->handler == 0) t->handler = detail::get_u32le(b + 8); }   // minf/hdlr ('alis') must not override mdia/hdlr
        else if (t && ty == fcc('s','t','s','d')) parse_stsd(b, bl, *t);
        else if (t && ty == fcc('s','t','t','s') && bl >= 8) {
            uint32_t cnt = detail::get_u32be(b + 4); if ((uint64_t)cnt * 8 <= bl - 8) for (uint32_t i = 0; i < cnt; ++i) { t->stts_n.push_back(detail::get_u32be(b + 8 + i * 8)); t->stts_d.push_back(detail::get_u32be(b + 12 + i * 8)); } }
        else if (t && ty == fcc('c','t','t','s') && bl >= 8) {
            uint32_t cnt = detail::get_u32be(b + 4); if ((uint64_t)cnt * 8 <= bl - 8) for (uint32_t i = 0; i < cnt; ++i) { t->ctts_n.push_back(detail::get_u32be(b + 8 + i * 8)); t->ctts_o.push_back((int32_t)detail::get_u32be(b + 12 + i * 8)); } }
        else if (t && ty == fcc('s','t','s','s') && bl >= 8) {
            uint32_t cnt = detail::get_u32be(b + 4); t->have_stss = true; if ((uint64_t)cnt * 4 <= bl - 8) for (uint32_t i = 0; i < cnt; ++i) t->keys.push_back(detail::get_u32be(b + 8 + i * 4)); }
        else if (t && ty == fcc('s','t','s','c') && bl >= 8) {
            uint32_t cnt = detail::get_u32be(b + 4); if ((uint64_t)cnt * 12 <= bl - 8) for (uint32_t i = 0; i < cnt; ++i) { t->stsc_first.push_back(detail::get_u32be(b + 8 + i * 12)); t->stsc_n.push_back(detail::get_u32be(b + 12 + i * 12)); } }
        else if (t && ty == fcc('s','t','s','z') && bl >= 12) {
            t->sample_size = detail::get_u32be(b + 4); t->sample_count = detail::get_u32be(b + 8); t->have_stsz = true;
            if (t->sample_size == 0 && (uint64_t)t->sample_count * 4 <= bl - 12) for (uint32_t i = 0; i < t->sample_count; ++i) t->sizes.push_back(detail::get_u32be(b + 12 + i * 4)); }
        else if (t && (ty == fcc('s','t','c','o') || ty == fcc('c','o','6','4')) && bl >= 8) {
            const bool big = ty == fcc('c','o','6','4'); uint32_t cnt = detail::get_u32be(b + 4); const uint32_t es = big ? 8 : 4;
            if ((uint64_t)cnt * es <= bl - 8) for (uint32_t i = 0; i < cnt; ++i) t->offsets.push_back(big ? detail::get_u64be(b + 8 + i * 8) : detail::get_u32be(b + 8 + i * 4)); }
        else if (!t && depth >= 3 && bl > 16) {      // ilst item: '(c)nam' etc. -> 'data'
            const uint32_t k = ty; knst_c16string* dst = nullptr; metadata& m = c.d->meta;
            if (k == detail::fcc('\xA9','n','a','m')) dst = &m.title; else if (k == detail::fcc('\xA9','A','R','T')) dst = &m.artist;
            else if (k == detail::fcc('\xA9','a','l','b')) dst = &m.album; else if (k == detail::fcc('\xA9','c','m','t')) dst = &m.comment;
            else if (k == detail::fcc('\xA9','t','o','o')) dst = &m.software; else if (k == detail::fcc('c','p','r','t')) dst = &m.copyright;
            if (dst && detail::get_u32le(b + 4) == fcc('d','a','t','a') && detail::get_u32be(b) >= 16) {
                knst_byte_string z(b + 16, (uint32_t)(detail::get_u32be(b) - 16 <= bl - 16 ? detail::get_u32be(b) - 16 : bl - 16)); z.push_back(0);
                *dst = knst_c16string(reinterpret_cast<const char*>(z.data()));
            }
        }
        pos += (size_t)sz;
    }
}

} // namespace detail_

inline errc parse(demux_base& d) noexcept {
    using namespace detail_;
    detail::file_handle& f = d.file; const uint64_t fsz = f.size();
    bool have_moov = false, fragmented = false; d.kind = container::MP4;
    ctx c; c.d = &d;
    uint64_t pos = 0;
    while (pos + 8 <= fsz) {
        uint8_t h[16]; size_t got = (size_t)((fsz - pos) < 16 ? (fsz - pos) : 16);
        if (!f.seek(pos) || f.read(h, got) != got || got < 8) break;
        uint64_t sz = detail::get_u32be(h); const uint32_t ty = detail::get_u32le(h + 4); uint64_t hdr = 8;
        if (sz == 1) { if (got < 16) break; sz = detail::get_u64be(h + 8); hdr = 16; }
        else if (sz == 0) sz = fsz - pos;
        if (sz < hdr) break;
        if (pos + sz > fsz) sz = fsz - pos;
        if (ty == detail::fcc('f','t','y','p') && sz >= hdr + 4) { uint32_t brand = detail::get_u32le(h + 8); if (hdr == 8 && brand == detail::fcc('q','t',' ',' ')) d.kind = container::MOV; }
        else if (ty == detail::fcc('m','o','o','v')) {
            if (sz - hdr > (256u << 20)) return errc::TooLarge;
            knst_byte_string buf; if (detail::alloc_bytes(buf, sz) != errc::Ok) return errc::OutOfMemory;
            if (!f.seek(pos) || !f.read_exact(&buf[0], (size_t)sz)) return errc::IoError;
            walk(buf.data(), buf.length(), c, 0);
            have_moov = true;
        } else if (ty == detail::fcc('m','o','o','f')) fragmented = true;
        pos += sz;
    }
    if (c.err != errc::Ok) return c.err;
    if (!have_moov) return errc::Corrupt;
    if (d.tracks.empty()) return fragmented ? errc::NotSupported : errc::Corrupt;
    uint64_t dur = c.movie_ts ? detail::muldiv_u64(c.movie_dur, 1000000ull, c.movie_ts) : 0;
    for (uint32_t i = 0; i < d.tracks.size(); ++i) if (d.tracks[i].desc.duration_us > dur) dur = d.tracks[i].desc.duration_us;
    d.duration_us = dur;
    return errc::Ok;
}

} // namespace mp4

// ============================================================
// RAW (headerless pixel dump)
// ============================================================

namespace rawvid {

// Builds an index for a headerless dump: frames are fixed-size and back to back.
inline errc parse(demux_base& d, uint32_t w, uint32_t h, pixel_format pix, uint32_t fps_num, uint32_t fps_den, uint32_t stride = 0) noexcept {
    plane_layout L;
    if (!compute_layout(pix, w, h, stride, L) || fps_num == 0 || fps_den == 0) return errc::InvalidArgument;
    if (L.total == 0 || L.total > KNST_VIDEO_MAX_SAMPLE_BYTES) return errc::TooLarge;
    stream_desc sd; sd.kind = track_kind::Video; sd.vcodec = video_codec::RAW; sd.width = w; sd.height = h; sd.pix = pix;
    sd.stride = L.stride[0]; sd.fps_num = fps_num; sd.fps_den = fps_den; sd.fps = (double)fps_num / fps_den;
    const uint64_t n = d.file.size() / L.total;
    if (n == 0) return errc::Corrupt;
    if (n > KNST_VIDEO_MAX_SAMPLES) return errc::TooLarge;
    uint32_t ti = d.add_track(sd);
    d.tracks[ti].samples.reserve((uint32_t)n);
    for (uint64_t i = 0; i < n; ++i) {
        sample_ref s; s.offset = i * L.total; s.size = (uint32_t)L.total; s.unit_start = i;
        s.pts_us = detail::muldiv_u64(i, 1000000ull * fps_den, fps_num);
        d.tracks[ti].samples.push_back(s);
    }
    d.tracks[ti].desc.frame_count = n;
    d.tracks[ti].desc.duration_us = detail::muldiv_u64(n, 1000000ull * fps_den, fps_num);
    d.duration_us = d.tracks[ti].desc.duration_us;
    d.kind = container::RAW;
    return errc::Ok;
}

} // namespace rawvid

// ============================================================
// Subtitles (SRT / WebVTT / ASS)
// ============================================================

namespace sub {

// "00:00:01,500" | "00:01.500" | "0:00:01.50" (ASS centiseconds). Fraction digits decide the scale.
inline bool parse_timestamp(const char* s, uint64_t& ms) noexcept {
    if (!s) return false;
    while (*s == ' ' || *s == '\t') ++s;
    uint64_t nums[3] = {0, 0, 0}; int n = 0;
    while (n < 3) {
        if (*s < '0' || *s > '9') return false;
        uint64_t v = 0; int dg = 0;
        while (*s >= '0' && *s <= '9') { if (++dg > 9) return false; v = v * 10 + (uint64_t)(*s - '0'); ++s; }
        nums[n++] = v;
        if (*s == ':') { ++s; continue; }
        break;
    }
    uint64_t h = 0, m = 0, sec = 0;
    if (n == 3) { h = nums[0]; m = nums[1]; sec = nums[2]; } else if (n == 2) { m = nums[0]; sec = nums[1]; } else return false;
    uint64_t frac = 0;
    if (*s == '.' || *s == ',') {
        ++s; int dg = 0;
        while (*s >= '0' && *s <= '9') { if (dg < 3) { frac = frac * 10 + (uint64_t)(*s - '0'); ++dg; } ++s; }
        while (dg < 3) { frac *= 10; ++dg; }
    }
    ms = ((h * 60 + m) * 60 + sec) * 1000 + frac;
    return true;
}

namespace detail_ {
inline knst_c16string fmt(const char* f, uint32_t a, uint32_t b, uint32_t c, uint32_t d) noexcept { char buf[48]; std::snprintf(buf, sizeof(buf), f, a, b, c, d); return knst_c16string(buf); }
inline void split(uint64_t ms, uint32_t& h, uint32_t& m, uint32_t& s, uint32_t& r) noexcept { h = (uint32_t)(ms / 3600000); m = (uint32_t)((ms / 60000) % 60); s = (uint32_t)((ms / 1000) % 60); r = (uint32_t)(ms % 1000); }

struct lines {
    knst_byte_string raw; size_t pos = 0;
    bool next(knst_byte_string& out) noexcept {       // NUL-terminated line without CR/LF
        const size_t n = raw.length();
        if (pos >= n) return false;
        size_t e = pos; while (e < n && raw[e] != '\n') ++e;
        size_t len = e - pos; if (len && raw[pos + len - 1] == '\r') --len;
        out.clear(); if (len) out.append(raw.data() + pos, (uint32_t)len); out.push_back(0);
        pos = e + 1; return true;
    }
};
inline errc load(const knst_c16string& path, lines& L) noexcept {
    knst_file_error fe = knst_file_error::None;
    L.raw = knst_file::read_file_data<>(path, &fe);
    if (fe != knst_file_error::None) return errc::NotFound;
    if (L.raw.empty()) return errc::Corrupt;
    if (L.raw.length() >= 3 && L.raw[0] == 0xEF && L.raw[1] == 0xBB && L.raw[2] == 0xBF) L.pos = 3;
    return errc::Ok;
}
inline const char* cs(const knst_byte_string& b) noexcept { return reinterpret_cast<const char*>(b.data()); }
inline bool is_blank(const knst_byte_string& b) noexcept { for (uint32_t i = 0; i + 1 < b.length(); ++i) if (b[i] != ' ' && b[i] != '\t') return false; return true; }
inline void add_text(knst_c16string& t, const knst_byte_string& line) noexcept { if (!t.empty()) t.append(u"\n"); t.append(knst_c16string(cs(line))); }
inline errc read_cues(const knst_c16string& path, bool vtt, knst_vector<subtitle_entry>& out) noexcept {
    lines L; errc e = load(path, L); if (e != errc::Ok) return e;
    knst_byte_string ln; subtitle_entry cur; bool in_cue = false;
    auto flush = [&]() { if (in_cue) out.push_back(std::move(cur)); cur = subtitle_entry(); in_cue = false; };
    while (L.next(ln)) {
        if (in_cue) { if (is_blank(ln)) flush(); else add_text(cur.text, ln); continue; }
        const char* a = std::strstr(cs(ln), "-->");
        if (!a) continue;                                 // index line / cue id / header / NOTE blocks
        char ta[64] = {0}, tb[64] = {0};
        size_t la = (size_t)(a - cs(ln)); if (la >= sizeof(ta)) continue;
        std::memcpy(ta, cs(ln), la);
        const char* bs = a + 3; while (*bs == ' ' || *bs == '\t') ++bs;
        size_t lb = 0; while (bs[lb] && bs[lb] != ' ' && bs[lb] != '\t' && lb + 1 < sizeof(tb)) { tb[lb] = bs[lb]; ++lb; }
        uint64_t s0, s1;
        if (parse_timestamp(ta, s0) && parse_timestamp(tb, s1)) { cur.start_ms = s0; cur.end_ms = s1; in_cue = true; }
    }
    (void)vtt; flush();
    return errc::Ok;
}
} // namespace detail_

namespace srt {
inline result<knst_vector<subtitle_entry>> read(const knst_c16string& path) noexcept {
    using R = result<knst_vector<subtitle_entry>>;
    knst_vector<subtitle_entry> v; errc e = detail_::read_cues(path, false, v);
    if (e != errc::Ok) return err_result<knst_vector<subtitle_entry>>(e);
    return R::success(std::move(v));
}
inline result<void> write(const knst_c16string& path, const knst_vector<subtitle_entry>& entries) noexcept {
    if (path.empty()) return void_result(errc::InvalidArgument);
    if (!knst_file::ensure_parent_dir(path)) return void_result(errc::IoError);
    knst_c16string out;
    for (uint32_t i = 0; i < entries.size(); ++i) {
        uint32_t h, m, s, r;
        out.append(static_cast<unsigned long long>(i + 1)); out.append(u"\n");
        detail_::split(entries[i].start_ms, h, m, s, r); out.append(detail_::fmt("%02u:%02u:%02u,%03u", h, m, s, r));
        out.append(u" --> ");
        detail_::split(entries[i].end_ms, h, m, s, r); out.append(detail_::fmt("%02u:%02u:%02u,%03u", h, m, s, r));
        out.append(u"\n"); out.append(entries[i].text); out.append(u"\n\n");
    }
    return void_result(knst_file::write_file_atomic_text(path, out) ? errc::Ok : errc::IoError);
}
} // namespace srt

namespace vtt {
inline result<knst_vector<subtitle_entry>> read(const knst_c16string& path) noexcept {
    using R = result<knst_vector<subtitle_entry>>;
    knst_vector<subtitle_entry> v; errc e = detail_::read_cues(path, true, v);
    if (e != errc::Ok) return err_result<knst_vector<subtitle_entry>>(e);
    return R::success(std::move(v));
}
inline result<void> write(const knst_c16string& path, const knst_vector<subtitle_entry>& entries) noexcept {
    if (path.empty()) return void_result(errc::InvalidArgument);
    if (!knst_file::ensure_parent_dir(path)) return void_result(errc::IoError);
    knst_c16string out(u"WEBVTT\n\n");
    for (uint32_t i = 0; i < entries.size(); ++i) {
        uint32_t h, m, s, r;
        detail_::split(entries[i].start_ms, h, m, s, r); out.append(detail_::fmt("%02u:%02u:%02u.%03u", h, m, s, r));
        out.append(u" --> ");
        detail_::split(entries[i].end_ms, h, m, s, r); out.append(detail_::fmt("%02u:%02u:%02u.%03u", h, m, s, r));
        out.append(u"\n"); out.append(entries[i].text); out.append(u"\n\n");
    }
    return void_result(knst_file::write_file_atomic_text(path, out) ? errc::Ok : errc::IoError);
}
} // namespace vtt

namespace ass {
// Reads [Events] Dialogue lines; override tags {...} are stripped and \N / \n become newlines.
inline result<knst_vector<subtitle_entry>> read(const knst_c16string& path) noexcept {
    using R = result<knst_vector<subtitle_entry>>;
    detail_::lines L; errc e = detail_::load(path, L); if (e != errc::Ok) return err_result<knst_vector<subtitle_entry>>(e);
    knst_vector<subtitle_entry> rv; knst_byte_string ln; bool events = false; int start_i = 1, end_i = 2, text_i = 9, nfields = 10;
    while (L.next(ln)) {
        const char* s = detail_::cs(ln);
        if (s[0] == '[') { events = (std::strncmp(s, "[Events]", 8) == 0); continue; }
        if (!events) continue;
        if (std::strncmp(s, "Format:", 7) == 0) {
            int idx = 0; const char* p = s + 7; nfields = 0;
            while (*p) {
                while (*p == ' ') ++p;
                const char* q = p; while (*q && *q != ',') ++q;
                size_t len = (size_t)(q - p); while (len && p[len - 1] == ' ') --len;
                if (len == 5 && !std::strncmp(p, "Start", 5)) start_i = idx; else if (len == 3 && !std::strncmp(p, "End", 3)) end_i = idx; else if (len == 4 && !std::strncmp(p, "Text", 4)) text_i = idx;
                ++idx; ++nfields; p = *q ? q + 1 : q;
            }
            continue;
        }
        if (std::strncmp(s, "Dialogue:", 9) != 0) continue;
        const char* p = s + 9; if (*p == ' ') ++p;
        const char* fld[32]; int nf = 0; fld[nf++] = p;
        for (const char* q = p; *q && nf < 32; ++q) if (*q == ',' && nf < nfields) fld[nf++] = q + 1;
        if (nf <= text_i || nf <= start_i || nf <= end_i) continue;
        subtitle_entry se; char tb[64];
        auto field = [&](int i, char* dst, size_t cap) { const char* a = fld[i]; const char* b = (i + 1 < nf) ? fld[i + 1] - 1 : a + std::strlen(a); size_t k = (size_t)(b - a); if (k >= cap) k = cap - 1; std::memcpy(dst, a, k); dst[k] = 0; };
        field(start_i, tb, sizeof(tb)); if (!parse_timestamp(tb, se.start_ms)) continue;
        field(end_i, tb, sizeof(tb)); if (!parse_timestamp(tb, se.end_ms)) continue;
        knst_byte_string text; bool tag = false;
        for (const char* q = fld[text_i]; *q; ++q) {
            if (*q == '{') { tag = true; continue; } if (*q == '}') { tag = false; continue; } if (tag) continue;
            if (*q == '\\' && (q[1] == 'N' || q[1] == 'n')) { text.push_back('\n'); ++q; continue; }
            text.push_back((uint8_t)*q);
        }
        text.push_back(0); se.text = knst_c16string(reinterpret_cast<const char*>(text.data()));
        rv.push_back(std::move(se));
    }
    return R::success(std::move(rv));
}
inline result<void> write(const knst_c16string& path, const knst_vector<subtitle_entry>& entries) noexcept {
    if (path.empty()) return void_result(errc::InvalidArgument);
    if (!knst_file::ensure_parent_dir(path)) return void_result(errc::IoError);
    knst_c16string out(u"[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n");
    out.append(u"[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n");
    out.append(u"Style: Default,Arial,54,&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,40,1\n\n");
    out.append(u"[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    for (uint32_t i = 0; i < entries.size(); ++i) {
        uint32_t h, m, s, r;
        out.append(u"Dialogue: 0,");
        detail_::split(entries[i].start_ms, h, m, s, r); out.append(detail_::fmt("%u:%02u:%02u.%02u", h, m, s, r / 10));
        out.append(u",");
        detail_::split(entries[i].end_ms, h, m, s, r); out.append(detail_::fmt("%u:%02u:%02u.%02u", h, m, s, r / 10));
        out.append(u",Default,,0,0,0,,");
        knst_c16string t;                                   // newlines -> \N
        for (uint32_t k = 0; k < entries[i].text.length(); ++k) { char16_t ch = entries[i].text[k]; if (ch == u'\n') t.append(u"\\N"); else { char16_t one[2] = {ch, 0}; t.append(one); } }
        out.append(t); out.append(u"\n");
    }
    return void_result(knst_file::write_file_atomic_text(path, out) ? errc::Ok : errc::IoError);
}
} // namespace ass

// Shift (and optionally scale) all timings. delta_ms may be negative; results are clamped at 0.
inline void shift(knst_vector<subtitle_entry>& v, int64_t delta_ms, double scale = 1.0) noexcept {
    for (uint32_t i = 0; i < v.size(); ++i) {
        int64_t a = (int64_t)((double)v[i].start_ms * scale) + delta_ms, b = (int64_t)((double)v[i].end_ms * scale) + delta_ms;
        v[i].start_ms = a < 0 ? 0 : (uint64_t)a; v[i].end_ms = b < 0 ? 0 : (uint64_t)b;
    }
}

} // namespace sub

// ============================================================
// Motion detector / timelapse
// ============================================================

class motion_detector {
public:
    struct config {
        uint32_t block = 8;              // sample one pixel per block x block cell
        uint32_t threshold = 25;         // per-cell luma change (0..255)
        double   fraction = 0.10;        // fraction of cells that must change
        uint32_t min_interval_ms = 500;  // do not analyse more often than this
    };
    motion_detector() noexcept {}
    explicit motion_detector(const config& c) noexcept : cfg_(c) { if (cfg_.block == 0) cfg_.block = 1; }

    bool feed_rgb24(const uint8_t* rgb, uint32_t w, uint32_t h, uint64_t ts_ms) noexcept {
        if (!rgb || w == 0 || h == 0) return false;
        if (has_prev_ && ts_ms - last_ts_ms_ < cfg_.min_interval_ms) return last_motion_;
        const uint32_t bw = w / cfg_.block, bh = h / cfg_.block;
        if (bw == 0 || bh == 0) return false;
        if (cur_.size() != bw * bh) { cur_.resize(bw * bh); has_prev_ = false; }
        for (uint32_t by = 0; by < bh; ++by) for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t* p = rgb + ((size_t)(by * cfg_.block + cfg_.block / 2) * w + (bx * cfg_.block + cfg_.block / 2)) * 3;
            cur_[by * bw + bx] = (uint8_t)((77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8);
        }
        last_ts_ms_ = ts_ms;
        bool motion = false; fraction_ = 0.0;
        if (has_prev_ && prev_.size() == cur_.size()) {
            uint32_t changed = 0;
            for (uint32_t i = 0; i < cur_.size(); ++i) { int d = (int)cur_[i] - (int)prev_[i]; if (d < 0) d = -d; if (d >= (int)cfg_.threshold) ++changed; }
            fraction_ = (double)changed / (double)cur_.size();
            motion = fraction_ >= cfg_.fraction;
        }
        std::swap(prev_, cur_);
        has_prev_ = true; last_motion_ = motion;
        return motion;
    }
    double last_fraction() const noexcept { return fraction_; }
    void reset() noexcept { has_prev_ = false; last_motion_ = false; fraction_ = 0.0; }
private:
    config cfg_; knst_vector<uint8_t> prev_, cur_;
    bool has_prev_ = false, last_motion_ = false; uint64_t last_ts_ms_ = 0; double fraction_ = 0.0;
};

class timelapse {
public:
    timelapse() noexcept = default;
    explicit timelapse(uint32_t interval_ms) noexcept : interval_ms_(interval_ms) {}
    void set_interval(uint32_t ms) noexcept { interval_ms_ = ms; }
    uint32_t interval() const noexcept { return interval_ms_; }
    bool due(uint64_t now_ms) noexcept {
        if (!has_last_ || now_ms - last_emit_ms_ >= interval_ms_) { has_last_ = true; last_emit_ms_ = now_ms; return true; }
        return false;
    }
    void reset() noexcept { has_last_ = false; }
private:
    uint32_t interval_ms_ = 1000; uint64_t last_emit_ms_ = 0; bool has_last_ = false;
};

// ============================================================
// External codec registry (plug your own H.264 / AAC / ... here)
// ============================================================

namespace external_codec {

// Decode one packet of `sd.vcodec` into an RGB24 frame (out.data, width, height must be filled by the callback).
typedef errc (*video_decode_fn)(void* user, const stream_desc& sd, const uint8_t* data, uint32_t size, video_frame& out_rgb24);
// Encode one RGB24 frame; `keyframe` tells the muxer whether the packet is a sync sample.
typedef errc (*video_encode_fn)(void* user, const video_frame& in_rgb24, knst_byte_string& out, bool& keyframe);
// Decode one audio packet into interleaved PCM (codec / channels / sample_rate / frames filled by the callback).
typedef errc (*audio_decode_fn)(void* user, const stream_desc& sd, const uint8_t* data, uint32_t size, audio_frame& out_pcm);

struct registry {
    struct vdec { video_decode_fn fn = nullptr; void* user = nullptr; };
    struct venc { video_encode_fn fn = nullptr; void* user = nullptr; };
    struct adec { audio_decode_fn fn = nullptr; void* user = nullptr; };
    std::mutex m; vdec vd[16]; venc ve[16]; adec ad[16];
    static registry& get() noexcept { static registry r; return r; }
};
inline bool register_video_decoder(video_codec c, video_decode_fn fn, void* user = nullptr) noexcept { auto& r = registry::get(); std::lock_guard<std::mutex> g(r.m); if ((unsigned)c >= 16) return false; r.vd[(unsigned)c].fn = fn; r.vd[(unsigned)c].user = user; return true; }
inline bool register_video_encoder(video_codec c, video_encode_fn fn, void* user = nullptr) noexcept { auto& r = registry::get(); std::lock_guard<std::mutex> g(r.m); if ((unsigned)c >= 16) return false; r.ve[(unsigned)c].fn = fn; r.ve[(unsigned)c].user = user; return true; }
inline bool register_audio_decoder(audio_codec c, audio_decode_fn fn, void* user = nullptr) noexcept { auto& r = registry::get(); std::lock_guard<std::mutex> g(r.m); if ((unsigned)c >= 16) return false; r.ad[(unsigned)c].fn = fn; r.ad[(unsigned)c].user = user; return true; }
inline registry::vdec find_video_decoder(video_codec c) noexcept { auto& r = registry::get(); std::lock_guard<std::mutex> g(r.m); return (unsigned)c < 16 ? r.vd[(unsigned)c] : registry::vdec(); }
inline registry::venc find_video_encoder(video_codec c) noexcept { auto& r = registry::get(); std::lock_guard<std::mutex> g(r.m); return (unsigned)c < 16 ? r.ve[(unsigned)c] : registry::venc(); }
inline registry::adec find_audio_decoder(audio_codec c) noexcept { auto& r = registry::get(); std::lock_guard<std::mutex> g(r.m); return (unsigned)c < 16 ? r.ad[(unsigned)c] : registry::adec(); }

} // namespace external_codec

// ============================================================
// High-level writer
// ============================================================

class video_writer {
public:
    static constexpr uint64_t AUTO_PTS = ~0ull;

    struct config {
        container cont = container::AVI;
        video_codec vcodec = video_codec::MJPEG;
        bool has_video = true;                      // false: audio-only (WAV / MKV)
        pixel_format pix = pixel_format::RGB24;     // pixel format of frames passed to write_frame()
        uint32_t width = 0, height = 0;
        uint32_t fps_num = 30, fps_den = 1;
        int jpeg_quality = 85;
        mjpeg::subsampling jpeg_sub = mjpeg::subsampling::s420;
        uint32_t jpeg_restart = 0;
        pixel_format raw_pix = pixel_format::Unknown;   // RAW output layout (AVI: BGR24 / BGRA32 / GREY; RAW file: any). Unknown = default
        bool scale_to_fit = false;                  // rescale frames whose size differs from width/height instead of failing
        bool fill_gaps = true;                      // AVI (constant frame rate): insert dropped frames when pts jumps ahead
        color::yuv_params yuv;                      // colour matrix for YUV <-> RGB
        audio_codec acodec = audio_codec::None;
        uint32_t sample_rate = 0, channels = 0;
        knst_byte_string vextradata, aextradata;    // CodecPrivate for passthrough codecs
        metadata meta;
    };

    video_writer() noexcept = default;
    ~video_writer() { close(); }
    video_writer(const video_writer&) = delete;
    video_writer& operator=(const video_writer&) = delete;

    result<void> open(const knst_c16string& path, const config& c) noexcept {
        close();
        err_ = errc::Ok; cfg_ = c; frames_ = 0; audio_frames_ = 0; kind_ = container::Unknown;
        if (path.empty()) return fail_(errc::InvalidArgument);
        if (c.has_video) {
            if (c.width == 0 || c.height == 0 || c.fps_num == 0 || c.fps_den == 0) return fail_(errc::InvalidArgument);
            if (c.width > KNST_VIDEO_MAX_DIMENSION || c.height > KNST_VIDEO_MAX_DIMENSION) return fail_(errc::InvalidArgument);
        }
        if (c.acodec != audio_codec::None && (c.channels < 1 || c.channels > 32 || c.sample_rate < 1)) return fail_(errc::InvalidArgument);
        if (c.acodec != audio_codec::None) audio_align_ = audio_codec_is_pcm(c.acodec) ? c.channels * audio_codec_bits(c.acodec) / 8 : 0;
        raw_out_ = c.raw_pix;
        if (raw_out_ == pixel_format::Unknown) raw_out_ = (c.cont == container::RAW && c.pix != pixel_format::MJPEG && c.pix != pixel_format::Unknown) ? c.pix : pixel_format::BGR24;
        if (c.vcodec == video_codec::MJPEG && c.has_video) {
            mjpeg::encoder::options o; o.quality = c.jpeg_quality; o.sub = c.jpeg_sub; o.restart_mcus = c.jpeg_restart;
            errc e = enc_.configure(o); if (e != errc::Ok) return fail_(e);
        }
        writer_params p; p.has_video = c.has_video; p.width = c.width; p.height = c.height; p.fps_num = c.fps_num; p.fps_den = c.fps_den;
        p.vcodec = c.vcodec; p.raw_pix = raw_out_; p.vextra = c.vextradata; p.acodec = c.acodec; p.sample_rate = c.sample_rate; p.channels = c.channels;
        p.aextra = c.aextradata; p.meta = c.meta;

        result<void> r = ok_void();
        switch (c.cont) {
            case container::AVI: if (!c.has_video) return fail_(errc::InvalidArgument); r = avi_.open(path, p); break;
            case container::MKV: r = mkv_.open(path, p, false); break;
            case container::MP4: r = mp4_.open(path, p, false); break;
            case container::MOV: r = mp4_.open(path, p, true); break;
            case container::WAV:
                if (c.acodec == audio_codec::None) return fail_(errc::InvalidArgument);
                r = wav_.open(path, c.sample_rate, c.channels, c.acodec, &c.meta); break;
            case container::RAW: {
                if (!c.has_video) return fail_(errc::InvalidArgument);
                plane_layout L; if (!compute_layout(raw_out_, c.width, c.height, 0, L)) return fail_(errc::NotSupported);
                if (!knst_file::ensure_parent_dir(path)) return fail_(errc::IoError);
                errc e = rawf_.open(path, detail::file_handle::mode::write); if (e != errc::Ok) return fail_(e);
                break;
            }
            default: return fail_(errc::NotSupported);   // WebM writing needs VP8/VP9 which this library cannot encode
        }
        if (!r) { err_ = errc::IoError; return r; }
        kind_ = c.cont;
        return ok_void();
    }

    // Any pixel format; MJPEG-encodes / converts as configured. stride 0 = tight. pts_us = AUTO_PTS -> frame_index / fps.
    result<void> write_frame(const uint8_t* data, uint32_t w, uint32_t h, pixel_format pix, uint32_t stride = 0, uint64_t pts_us = AUTO_PTS) noexcept {
        if (kind_ == container::Unknown || kind_ == container::WAV || !cfg_.has_video) return fail_(errc::BadState);
        if (!data) return fail_(errc::InvalidArgument);
        if (pix == pixel_format::MJPEG || pix == pixel_format::Unknown) return fail_(errc::InvalidArgument);
        const uint8_t* src = data; pixel_format spix = pix; uint32_t sstride = stride;
        if (w != cfg_.width || h != cfg_.height) {
            if (!cfg_.scale_to_fit) return fail_(errc::InvalidArgument);
            errc e = color::convert_to_rgb24(data, pix, w, h, stride, rgb_, cfg_.yuv); if (e != errc::Ok) return fail_(e);
            auto rs = img::resize_rgb24(rgb_.data(), w, h, cfg_.width, cfg_.height, (cfg_.width < w || cfg_.height < h) ? img::resize_filter::box : img::resize_filter::bilinear);
            if (!rs) return fail_(errc::OutOfMemory);
            rgb_ = std::move(rs.value); src = rgb_.data(); spix = pixel_format::RGB24; sstride = 0;
        }
        errc e = errc::Ok; const bool key = true;
        if (kind_ == container::RAW) {
            e = to_layout(src, spix, sstride, raw_out_, false);
            if (e == errc::Ok) e = rawf_.write(conv2_.data(), conv2_.length()) ? errc::Ok : errc::IoError;
            if (e != errc::Ok) return fail_(e);
            ++frames_; return ok_void();
        }
        if (cfg_.vcodec == video_codec::MJPEG) {
            e = enc_.encode(src, cfg_.width, cfg_.height, sstride, spix, jpg_);
            if (e != errc::Ok) return fail_(e);
            return write_encoded(jpg_.data(), jpg_.length(), key, pts_us);
        }
        if (cfg_.vcodec == video_codec::RAW) {
            if (kind_ != container::AVI) return fail_(errc::NotSupported);
            e = to_layout(src, spix, sstride, raw_out_, true);
            if (e != errc::Ok) return fail_(e);
            return write_encoded(conv2_.data(), conv2_.length(), true, pts_us);
        }
        // registered external encoder (H.264 ...)
        external_codec::registry::venc enc = external_codec::find_video_encoder(cfg_.vcodec);
        if (!enc.fn) return fail_(errc::NotSupported);
        e = color::convert_to_rgb24(src, spix, cfg_.width, cfg_.height, sstride, rgb_, cfg_.yuv); if (e != errc::Ok) return fail_(e);
        video_frame vf; vf.width = cfg_.width; vf.height = cfg_.height; vf.pix = pixel_format::RGB24; vf.data = rgb_; vf.valid = true;
        bool k = true; knst_byte_string pkt;
        e = enc.fn(enc.user, vf, pkt, k); if (e != errc::Ok) return fail_(e);
        return write_encoded(pkt.data(), pkt.length(), k, pts_us);
    }
    result<void> write_frame_rgb24(const uint8_t* rgb, uint32_t stride = 0, uint64_t pts_us = AUTO_PTS) noexcept {
        return write_frame(rgb, cfg_.width, cfg_.height, pixel_format::RGB24, stride, pts_us);
    }
    result<void> write_frame(const video_frame& f) noexcept { return write_frame(f.data.data(), f.width, f.height, f.pix, f.stride, f.pts_us ? f.pts_us : AUTO_PTS); }

    // Already-encoded packet (a JPEG for MJPEG, an access unit for H.264 ...). Required for passthrough codecs.
    // dts_us: only used by MP4/MOV (B-frame streams); other containers store just the pts.
    result<void> write_encoded(const uint8_t* data, uint32_t size, bool keyframe, uint64_t pts_us = AUTO_PTS, uint64_t dts_us = ~0ull) noexcept {
        if (kind_ == container::Unknown || !cfg_.has_video) return fail_(errc::BadState);

        // MJPEG passthrough: many cameras emit JPEG frames without a DHT
        // segment (they expect a "shared" default table). AVI/MOV MJPEG
        // files that omit DHT play in our reader (which injects during
        // decode) but are rejected by external players. Inject the standard
        // tables once at write time so the output is universally playable.
        // No-op for encoder-produced JPEGs (already contain DHT) and for
        // non-MJPEG codecs.
        knst_byte_string patched;
        if (cfg_.vcodec == video_codec::MJPEG && data && size >= 4) {
            if (mjpeg::inject_std_dht(data, size, patched)) {
                data = patched.data();
                size = patched.length();
            }
        }

        const bool autop = (pts_us == AUTO_PTS);
        if (autop) pts_us = detail::muldiv_u64(frames_, 1000000ull * cfg_.fps_den, cfg_.fps_num);
        result<void> r = ok_void();
        if (kind_ == container::AVI) {
            if (!autop && cfg_.fill_gaps) {
                uint64_t target = detail::muldiv_u64(pts_us, cfg_.fps_num, 1000000ull * cfg_.fps_den);
                uint64_t gap = target > frames_ ? target - frames_ : 0;
                if (gap > 0 && gap <= (uint64_t)cfg_.fps_num * 60 / cfg_.fps_den + 1) {
                    for (uint64_t i = 0; i < gap; ++i) { r = avi_.write_video(nullptr, 0, false); if (!r) return fail_(avi_.last_error()); ++frames_; }
                }
            }
            r = avi_.write_video(data, size, keyframe); if (!r) return fail_(avi_.last_error());
        } else if (kind_ == container::MKV) {
            r = mkv_.write_video(data, size, pts_us, keyframe); if (!r) return fail_(mkv_.last_error());
        } else if (kind_ == container::MP4 || kind_ == container::MOV) {
            r = mp4_.write_video(data, size, pts_us, keyframe, dts_us); if (!r) return fail_(mp4_.last_error());
        } else if (kind_ == container::RAW) {
            if (!rawf_.write(data, size)) return fail_(errc::IoError);
        } else return fail_(errc::BadState);
        ++frames_;
        return ok_void();
    }

    // Interleaved PCM (or an already-encoded audio packet for MKV passthrough).
    result<void> write_audio(const uint8_t* data, uint32_t bytes, uint64_t pts_us = AUTO_PTS) noexcept {
        if (kind_ == container::Unknown || cfg_.acodec == audio_codec::None) return fail_(errc::BadState);
        if (!data || bytes == 0) return fail_(errc::InvalidArgument);
        if (audio_align_ && bytes % audio_align_) return fail_(errc::InvalidArgument);
        if (pts_us == AUTO_PTS) pts_us = detail::muldiv_u64(audio_frames_, 1000000ull, cfg_.sample_rate);
        result<void> r = ok_void();
        switch (kind_) {
            case container::AVI: r = avi_.write_audio(data, bytes); if (!r) return fail_(avi_.last_error()); break;
            case container::MKV: r = mkv_.write_audio(data, bytes, pts_us); if (!r) return fail_(mkv_.last_error()); break;
            case container::MP4: case container::MOV: r = mp4_.write_audio(data, bytes); if (!r) return fail_(mp4_.last_error()); break;
            case container::WAV: r = wav_.write(data, bytes); if (!r) return fail_(wav_.last_error()); break;
            default: return fail_(errc::NotSupported);
        }
        if (audio_align_) audio_frames_ += bytes / audio_align_;
        return ok_void();
    }
    result<void> write_audio_s16(const int16_t* s, uint32_t sample_count) noexcept {
        if (cfg_.acodec != audio_codec::PCM_S16LE) return fail_(errc::BadState);
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        return write_audio(reinterpret_cast<const uint8_t*>(s), sample_count * 2);
#else
        if (detail::alloc_bytes(conv2_, (uint64_t)sample_count * 2) != errc::Ok) return fail_(errc::OutOfMemory);
        for (uint32_t i = 0; i < sample_count; ++i) detail::put_u16le(&conv2_[i * 2], (uint16_t)s[i]);
        return write_audio(conv2_.data(), sample_count * 2);
#endif
    }
    result<void> write_audio_f32(const float* s, uint32_t sample_count) noexcept {
        if (cfg_.acodec == audio_codec::PCM_F32LE) return write_audio(reinterpret_cast<const uint8_t*>(s), sample_count * 4);
        if (!audio_codec_is_pcm(cfg_.acodec)) return fail_(errc::BadState);
        const uint32_t b = audio_codec_bits(cfg_.acodec) / 8;
        if (detail::alloc_bytes(conv2_, (uint64_t)sample_count * b) != errc::Ok) return fail_(errc::OutOfMemory);
        for (uint32_t i = 0; i < sample_count; ++i) audio::f32_to_sample(s[i], &conv2_[i * b], cfg_.acodec);
        return write_audio(conv2_.data(), sample_count * b);
    }

    result<void> close() noexcept {
        if (kind_ == container::Unknown) return ok_void();
        result<void> r = ok_void(); errc e = errc::Ok;
        switch (kind_) {
            case container::AVI: r = avi_.close(); break;
            case container::MKV: r = mkv_.close(); break;
            case container::MP4: case container::MOV: r = mp4_.close(); break;
            case container::WAV: r = wav_.close(); break;
            case container::RAW: if (!rawf_.close()) e = errc::IoError; break;
            default: break;
        }
        kind_ = container::Unknown;
        if (!r) { err_ = errc::IoError; return r; }
        if (e != errc::Ok) { err_ = e; return void_result(e); }
        return ok_void();
    }

    bool is_open() const noexcept { return kind_ != container::Unknown; }
    uint64_t frames_written() const noexcept { return frames_; }
    uint64_t audio_frames_written() const noexcept { return audio_frames_; }
    uint64_t pts_next_us() const noexcept { return detail::muldiv_u64(frames_, 1000000ull * cfg_.fps_den, cfg_.fps_num ? cfg_.fps_num : 1); }
    uint64_t bytes_written() const noexcept {
        switch (kind_) { case container::AVI: return avi_.bytes_written(); case container::MKV: return mkv_.bytes_written(); case container::MP4: case container::MOV: return mp4_.bytes_written();
                         case container::WAV: return wav_.bytes_written(); case container::RAW: return rawf_.tell(); default: return 0; }
    }
    errc last_error() const noexcept { return err_; }
    const char* last_error_string() const noexcept { return errc_string(err_); }
    const config& settings() const noexcept { return cfg_; }

private:
    config cfg_; container kind_ = container::Unknown; errc err_ = errc::Ok;
    uint64_t frames_ = 0, audio_frames_ = 0; uint32_t audio_align_ = 0; pixel_format raw_out_ = pixel_format::BGR24;
    avi::writer avi_; mkv::writer mkv_; mp4::writer mp4_; wav::writer wav_; detail::file_handle rawf_;
    mjpeg::encoder enc_; knst_byte_string jpg_, rgb_, conv_, conv2_;

    result<void> fail_(errc e) noexcept { err_ = e; return void_result(e); }

    // Converts src to `target` layout into conv2_. dib = AVI DIB (rows padded to 4 bytes, bottom row first).
    errc to_layout(const uint8_t* src, pixel_format spix, uint32_t sstride, pixel_format target, bool dib) noexcept {
        const uint32_t w = cfg_.width, h = cfg_.height;
        const uint8_t* conv_src = src; uint32_t cs = sstride; pixel_format cp = spix;
        if (spix != target && !(spix == pixel_format::RGB24 && false)) {
            errc e = color::convert_to_rgb24(src, spix, w, h, sstride, conv_, cfg_.yuv); if (e != errc::Ok) return e;
            conv_src = conv_.data(); cs = 0; cp = pixel_format::RGB24;
            if (target != pixel_format::RGB24) {
                e = color::convert_from_rgb24(conv_src, w, h, 0, target, conv2_, cfg_.yuv); if (e != errc::Ok) return e;
                conv_src = conv2_.data(); cp = target; cs = 0;
            }
        }
        plane_layout L; if (!compute_layout(cp, w, h, cs, L)) return errc::InvalidArgument;
        if (!dib) {
            knst_byte_string out; plane_layout T; if (!compute_layout(target, w, h, 0, T)) return errc::NotSupported;
            if (conv_src == conv2_.data()) return errc::Ok;
            errc e = detail::alloc_bytes(out, T.total); if (e != errc::Ok) return e;
            if (L.planes == 1) { for (uint32_t y = 0; y < h; ++y) std::memcpy(&out[(size_t)y * T.stride[0]], conv_src + (size_t)y * L.stride[0], T.stride[0]); }
            else std::memcpy(&out[0], conv_src, (size_t)T.total);
            conv2_ = std::move(out); return errc::Ok;
        }
        const uint32_t bits = pixel_format_bpp(target), rowbytes = w * bits / 8, ds = avi::dib_stride(w, bits);
        knst_byte_string out; errc e = detail::alloc_bytes(out, (uint64_t)ds * h); if (e != errc::Ok) return e;
        for (uint32_t y = 0; y < h; ++y) std::memcpy(&out[(size_t)(h - 1 - y) * ds], conv_src + (size_t)y * L.stride[0], rowbytes);
        conv2_ = std::move(out); return errc::Ok;
    }
};

// ============================================================
// High-level reader (auto-detects AVI / MKV / WebM / MP4 / MOV / WAV)
// ============================================================

class video_reader {
public:
    video_reader() noexcept = default;

    result<void> open(const knst_c16string& path) noexcept {
        close();
        errc e = d_.file.open(path, detail::file_handle::mode::read);
        if (e != errc::Ok) return fail_(e);
        uint8_t h[16] = {0};
        size_t got = d_.file.read(h, 16);
        if (got < 12) { d_.reset(); return fail_(errc::Corrupt); }
        if (!std::memcmp(h, "RIFF", 4) && !std::memcmp(h + 8, "AVI ", 4)) e = avi::parse(d_);
        else if (!std::memcmp(h, "RIFF", 4) && !std::memcmp(h + 8, "WAVE", 4)) e = wav::parse(d_);
        else if (!std::memcmp(h, "RF64", 4)) e = wav::parse(d_);
        else if (h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3) e = mkv::parse(d_);
        else if (!std::memcmp(h + 4, "ftyp", 4) || !std::memcmp(h + 4, "moov", 4) || !std::memcmp(h + 4, "mdat", 4) || !std::memcmp(h + 4, "free", 4) || !std::memcmp(h + 4, "wide", 4)) e = mp4::parse(d_);
        else e = errc::NotSupported;
        if (e != errc::Ok) { d_.reset(); return fail_(e); }
        finish_open();
        return ok_void();
    }
    // Headerless dumps need their parameters.
    result<void> open_raw(const knst_c16string& path, uint32_t w, uint32_t h, pixel_format pix, uint32_t fps_num = 30, uint32_t fps_den = 1, uint32_t stride = 0) noexcept {
        close();
        errc e = d_.file.open(path, detail::file_handle::mode::read);
        if (e == errc::Ok) e = rawvid::parse(d_, w, h, pix, fps_num, fps_den, stride);
        if (e != errc::Ok) { d_.reset(); return fail_(e); }
        finish_open();
        return ok_void();
    }
    void close() noexcept { d_.reset(); info_ = media_info(); vt_ = at_ = -1; err_ = errc::Ok; }
    bool is_open() const noexcept { return d_.file.is_open(); }

    container kind() const noexcept { return d_.kind; }
    const media_info& info() const noexcept { return info_; }
    const metadata& meta() const noexcept { return d_.meta; }
    uint64_t duration_us() const noexcept { return d_.duration_us; }
    uint32_t stream_count() const noexcept { return d_.tracks.size(); }
    const stream_desc& stream(uint32_t i) const noexcept { return d_.tracks[i].desc; }
    int video_stream() const noexcept { return vt_; }
    int audio_stream() const noexcept { return at_; }
    uint32_t width() const noexcept { return vt_ >= 0 ? d_.tracks[(uint32_t)vt_].desc.width : 0; }
    uint32_t height() const noexcept { return vt_ >= 0 ? d_.tracks[(uint32_t)vt_].desc.height : 0; }
    double fps() const noexcept { return vt_ >= 0 ? d_.tracks[(uint32_t)vt_].desc.fps : 0.0; }
    uint64_t video_frames() const noexcept { return vt_ >= 0 ? d_.tracks[(uint32_t)vt_].samples.size() : 0; }
    uint64_t audio_frames() const noexcept { return at_ >= 0 ? d_.tracks[(uint32_t)at_].desc.frame_count : 0; }

    // Native payload of video frame `idx` (JPEG bytes, raw DIB rows, NAL units ...). A zero-length payload = dropped frame.
    result<video_frame> read_frame(uint64_t idx) noexcept {
        using R = result<video_frame>;
        if (vt_ < 0) return err_result<video_frame>(errc::NotFound);
        video_frame f; errc e = fill_frame((uint32_t)vt_, idx, f);
        if (e != errc::Ok) { err_ = e; return err_result<video_frame>(e); }
        return R::success(std::move(f));
    }
    // Decoded RGB24 (tight). MJPEG and RAW are decoded here; other codecs need external_codec::register_video_decoder.
    result<video_frame> read_frame_rgb24(uint64_t idx) noexcept {
        using R = result<video_frame>;
        if (vt_ < 0) return err_result<video_frame>(errc::NotFound);
        const track_data& td = d_.tracks[(uint32_t)vt_];
        if (idx >= td.samples.size()) return err_result<video_frame>(errc::NotFound);
        uint64_t real = idx; while (real > 0 && td.samples[(uint32_t)real].size == 0) --real;      // dropped frame -> previous picture
        video_frame in; errc e = fill_frame((uint32_t)vt_, real, in);
        video_frame out;
        if (e == errc::Ok) e = decode_to_rgb24(td.desc, in, out);
        if (e != errc::Ok) { err_ = e; return err_result<video_frame>(e); }
        out.pts_us = td.samples[(uint32_t)idx].pts_us; out.sequence = (uint32_t)idx; out.keyframe = td.samples[(uint32_t)idx].key;
        return R::success(std::move(out));
    }
    uint64_t frame_at_time(uint64_t pts_us) const noexcept { return vt_ >= 0 ? d_.index_at_time((uint32_t)vt_, pts_us) : 0; }
    uint64_t keyframe_at_or_before(uint64_t idx) const noexcept { return vt_ >= 0 ? d_.keyframe_at_or_before((uint32_t)vt_, idx) : 0; }

    // Generic packet access for any track (subtitles, passthrough audio, ...).
    result<knst_byte_string> read_packet(uint32_t track, uint64_t idx, uint64_t* pts_us = nullptr, bool* key = nullptr, uint64_t* dts_us = nullptr) noexcept {
        using R = result<knst_byte_string>;
        knst_byte_string b; errc e = d_.read_sample(track, idx, b);
        if (e != errc::Ok) { err_ = e; return err_result<knst_byte_string>(e); }
        const sample_ref& s = d_.tracks[track].samples[(uint32_t)idx];
        if (pts_us) *pts_us = s.pts_us;
        if (key) *key = s.key;
        if (dts_us) *dts_us = (s.dts_us == ~0ull) ? s.pts_us : s.dts_us;
        return R::success(std::move(b));
    }

    // PCM range of the first audio track; frame_count is clamped to what exists.
    result<audio_frame> read_audio(uint64_t start_frame, uint64_t frame_count) noexcept {
        using R = result<audio_frame>;
        if (at_ < 0) return err_result<audio_frame>(errc::NotFound);
        const track_data& td = d_.tracks[(uint32_t)at_]; const stream_desc& sd = td.desc;
        if (!audio_codec_is_pcm(sd.acodec) || !sd.channels || !sd.bits_per_sample) return err_result<audio_frame>(errc::NotSupported);
        const uint64_t total = sd.frame_count;
        if (start_frame >= total) return err_result<audio_frame>(errc::EndOfStream);
        if (frame_count > total - start_frame) frame_count = total - start_frame;
        const uint32_t align = sd.channels * (sd.bits_per_sample / 8);
        if (frame_count * align > 0xFFFFFF00ull) return err_result<audio_frame>(errc::TooLarge);
        audio_frame a; errc e = detail::alloc_bytes(a.data, frame_count * align);
        if (e != errc::Ok) return err_result<audio_frame>(e);
        // binary search for the packet containing start_frame
        uint32_t lo = 0, hi = td.samples.size();
        while (lo < hi) { uint32_t mid = lo + (hi - lo) / 2; if (td.samples[mid].unit_start + td.samples[mid].units <= start_frame) lo = mid + 1; else hi = mid; }
        uint64_t done = 0; knst_byte_string pk;
        for (uint32_t i = lo; i < td.samples.size() && done < frame_count; ++i) {
            const sample_ref& s = td.samples[i];
            const uint64_t skip = (i == lo) ? start_frame - s.unit_start : 0;
            const uint64_t take = std::min<uint64_t>(s.units - skip, frame_count - done);
            if (s.offset + s.size > d_.file.size()) { err_ = errc::Corrupt; return err_result<audio_frame>(errc::Corrupt); }
            if (!d_.file.seek(s.offset + skip * align) || !d_.file.read_exact(&a.data[(size_t)(done * align)], (size_t)(take * align))) { err_ = errc::IoError; return err_result<audio_frame>(errc::IoError); }
            done += take;
        }
        a.sample_rate = sd.sample_rate; a.channels = sd.channels; a.codec = sd.acodec; a.frames = (uint32_t)done; a.valid = true;
        a.pts_us = detail::muldiv_u64(start_frame, 1000000ull, sd.sample_rate);
        return R::success(std::move(a));
    }
    result<audio_frame> read_audio_all() noexcept { return read_audio(0, ~0ull); }

    errc last_error() const noexcept { return err_; }
    const char* last_error_string() const noexcept { return errc_string(err_); }

private:
    demux_base d_; media_info info_; int vt_ = -1, at_ = -1; errc err_ = errc::Ok;

    result<void> fail_(errc e) noexcept { err_ = e; return void_result(e); }

    void finish_open() noexcept {
        info_.kind = d_.kind; info_.meta = d_.meta; info_.duration_us = d_.duration_us;
        for (uint32_t i = 0; i < d_.tracks.size(); ++i) info_.streams.push_back(d_.tracks[i].desc);
        vt_ = d_.find_track(track_kind::Video); at_ = d_.find_track(track_kind::Audio);
        err_ = errc::Ok;
    }

    errc fill_frame(uint32_t t, uint64_t idx, video_frame& f) noexcept {
        errc e = d_.read_sample(t, idx, f.data);
        if (e != errc::Ok) return e;
        const track_data& td = d_.tracks[t]; const sample_ref& s = td.samples[(uint32_t)idx]; const stream_desc& sd = td.desc;
        f.width = sd.width; f.height = sd.height; f.pts_us = s.pts_us; f.sequence = (uint32_t)idx; f.keyframe = s.key; f.valid = true;
        if (sd.vcodec == video_codec::MJPEG) f.pix = pixel_format::MJPEG;
        else if (sd.vcodec == video_codec::RAW) { f.pix = sd.pix; f.stride = sd.stride; f.bottom_up = sd.bottom_up; }
        else f.pix = pixel_format::Unknown;
        return errc::Ok;
    }

    errc decode_to_rgb24(const stream_desc& sd, const video_frame& in, video_frame& out) noexcept {
        if (sd.vcodec == video_codec::MJPEG) {
            uint32_t w = 0, h = 0;
            auto r = mjpeg::decode_rgb24(in.data.data(), in.data.length(), w, h);
            if (!r) return errc::IoError;
            out.data = std::move(r.value); out.width = w; out.height = h; out.stride = 0; out.pix = pixel_format::RGB24; out.valid = true;
            return errc::Ok;
        }
        if (sd.vcodec == video_codec::RAW) {
            if (sd.pix == pixel_format::Unknown) return errc::NotSupported;
            errc e = color::convert_to_rgb24(in.data.data(), sd.pix, sd.width, sd.height, sd.stride, out.data);
            if (e != errc::Ok) return e;
            if (sd.bottom_up) {
                const size_t rb = (size_t)sd.width * 3; knst_byte_string tmp; if (detail::alloc_bytes(tmp, rb) != errc::Ok) return errc::OutOfMemory;
                uint8_t* p = &out.data[0];
                for (uint32_t y = 0; y < sd.height / 2; ++y) { uint8_t* a = p + (size_t)y * rb; uint8_t* b = p + (size_t)(sd.height - 1 - y) * rb; std::memcpy(&tmp[0], a, rb); std::memcpy(a, b, rb); std::memcpy(b, &tmp[0], rb); }
            }
            out.width = sd.width; out.height = sd.height; out.stride = 0; out.pix = pixel_format::RGB24; out.valid = true;
            return errc::Ok;
        }
        external_codec::registry::vdec dec = external_codec::find_video_decoder(sd.vcodec);
        if (!dec.fn) return errc::NotSupported;
        errc e = dec.fn(dec.user, sd, in.data.data(), in.data.length(), out);
        if (e == errc::Ok) { out.pix = pixel_format::RGB24; out.valid = true; }
        return e;
    }
};

// ============================================================
// Motion-triggered / timelapse recorder with pre-roll
// ============================================================

class recorder {
public:
    struct config {
        knst_c16string dir;
        knst_c16string prefix = u"clip_";
        container cont = container::AVI;           // AVI / MKV / MP4 / MOV (MJPEG inside)
        uint32_t fps = 30;
        int jpeg_quality = 80;
        uint32_t pre_roll_ms = 2000;               // frames before the trigger that are kept in RAM and written first
        uint32_t post_roll_ms = 3000;              // keep recording this long after the last motion
        uint32_t max_clip_ms = 0;                  // 0 = unlimited
        motion_detector::config motion;
        bool timelapse_mode = false;               // ignore motion: one file, one frame every timelapse_interval_ms
        uint32_t timelapse_interval_ms = 1000;
    };
    enum : uint32_t { EV_NONE = 0, EV_CLIP_STARTED = 1, EV_CLIP_FINISHED = 2, EV_FRAME_WRITTEN = 4 };

    result<void> start(const config& c) noexcept {
        stop();
        if (c.dir.empty() || c.fps == 0) return void_result(errc::InvalidArgument);
        if (c.cont != container::AVI && c.cont != container::MKV && c.cont != container::MP4 && c.cont != container::MOV) return void_result(errc::NotSupported);
        cfg_ = c; det_ = motion_detector(c.motion); tl_ = timelapse(c.timelapse_interval_ms); tl_.reset();
        mjpeg::encoder::options o; o.quality = c.jpeg_quality;
        errc e = enc_.configure(o); if (e != errc::Ok) return void_result(e);
        cap_ = (uint32_t)((uint64_t)c.pre_roll_ms * c.fps / 1000) + 1; ring_.resize(cap_); head_ = 0; count_ = 0;
        clips_ = 0; recording_ = false; w_ = h_ = 0; started_ = true;
        return ok_void();
    }

    // Feed frames in capture order. Returns a bit mask of EV_* flags.
    result<uint32_t> feed_rgb24(const uint8_t* rgb, uint32_t w, uint32_t h, uint64_t ts_us) noexcept {
        using R = result<uint32_t>;
        if (!started_) return err_result<uint32_t>(errc::BadState);
        if (!rgb || w == 0 || h == 0) return err_result<uint32_t>(errc::InvalidArgument);
        uint32_t ev = EV_NONE;
        if (w != w_ || h != h_) { if (recording_) { finish_clip(); ev |= EV_CLIP_FINISHED; } w_ = w; h_ = h; count_ = 0; det_.reset(); }
        const uint64_t ts_ms = ts_us / 1000;
        if (cfg_.timelapse_mode) {
            if (!tl_.due(ts_ms)) return R::success(std::move(ev));
            if (!recording_) { errc e = begin_clip(ts_us); if (e != errc::Ok) return err_result<uint32_t>(e); ev |= EV_CLIP_STARTED; }
            errc e = enc_.encode(rgb, w, h, 0, pixel_format::RGB24, jpg_); if (e != errc::Ok) return err_result<uint32_t>(e);
            // timelapse: frames are laid out at the output fps, not at wall-clock spacing
            if (!wr_.write_encoded(jpg_.data(), jpg_.length(), true, video_writer::AUTO_PTS)) return err_result<uint32_t>(wr_.last_error());
            ev |= EV_FRAME_WRITTEN; return R::success(std::move(ev));
        }
        const bool motion = det_.feed_rgb24(rgb, w, h, ts_ms);
        bool encoded = false;
        if (!recording_ && cfg_.pre_roll_ms > 0) {
            errc e = enc_.encode(rgb, w, h, 0, pixel_format::RGB24, jpg_); if (e != errc::Ok) return err_result<uint32_t>(e);
            encoded = true; push_ring(ts_us);
        }
        if (motion) last_motion_us_ = ts_us;
        if (!recording_ && motion) {
            uint64_t t0 = count_ ? ring_[oldest()].ts_us : ts_us;
            errc e = begin_clip(t0); if (e != errc::Ok) return err_result<uint32_t>(e);
            ev |= EV_CLIP_STARTED;
            for (uint32_t i = 0; i < count_; ++i) {
                item& it = ring_[(oldest() + i) % cap_];
                if (!wr_.write_encoded(it.jpg.data(), it.jpg.length(), true, it.ts_us - clip_t0_)) return err_result<uint32_t>(wr_.last_error());
                ev |= EV_FRAME_WRITTEN;
            }
            count_ = 0;
        } else if (recording_) {
            if (!encoded) { errc e = enc_.encode(rgb, w, h, 0, pixel_format::RGB24, jpg_); if (e != errc::Ok) return err_result<uint32_t>(e); }
            if (!wr_.write_encoded(jpg_.data(), jpg_.length(), true, ts_us - clip_t0_)) return err_result<uint32_t>(wr_.last_error());
            ev |= EV_FRAME_WRITTEN;
            const bool quiet = ts_us - last_motion_us_ >= (uint64_t)cfg_.post_roll_ms * 1000;
            const bool too_long = cfg_.max_clip_ms && (ts_us - clip_t0_ >= (uint64_t)cfg_.max_clip_ms * 1000);
            if (quiet || too_long) { finish_clip(); ev |= EV_CLIP_FINISHED; }
        }
        return R::success(std::move(ev));
    }

    result<void> stop() noexcept {
        result<void> r = ok_void();
        if (recording_) { r = wr_.close(); recording_ = false; }
        started_ = false; count_ = 0;
        return r;
    }
    bool recording() const noexcept { return recording_; }
    uint32_t clips_written() const noexcept { return clips_; }
    const knst_c16string& current_file() const noexcept { return cur_path_; }

private:
    struct item { knst_byte_string jpg; uint64_t ts_us = 0; };
    config cfg_; motion_detector det_; timelapse tl_; mjpeg::encoder enc_; video_writer wr_;
    knst_vector<item> ring_; uint32_t cap_ = 1, head_ = 0, count_ = 0;
    knst_byte_string jpg_; knst_c16string cur_path_;
    bool started_ = false, recording_ = false; uint32_t clips_ = 0, w_ = 0, h_ = 0; uint64_t clip_t0_ = 0, last_motion_us_ = 0;

    uint32_t oldest() const noexcept { return (head_ + cap_ - count_) % cap_; }
    void push_ring(uint64_t ts) noexcept {
        item& it = ring_[head_]; it.jpg = jpg_; it.ts_us = ts;
        head_ = (head_ + 1) % cap_; if (count_ < cap_) ++count_;
    }
    errc begin_clip(uint64_t t0_us) noexcept {
        char num[24]; std::snprintf(num, sizeof(num), "%04u", clips_ + 1);
        knst_c16string name = cfg_.prefix; name.append(knst_c16string(num));
        name.append(cfg_.cont == container::MKV ? u".mkv" : cfg_.cont == container::MP4 ? u".mp4" : cfg_.cont == container::MOV ? u".mov" : u".avi");
        cur_path_ = knst_file::join_path(cfg_.dir, name);
        video_writer::config vc; vc.cont = cfg_.cont; vc.vcodec = video_codec::MJPEG; vc.width = w_; vc.height = h_; vc.fps_num = cfg_.fps; vc.fps_den = 1;
        auto r = wr_.open(cur_path_, vc); if (!r) return wr_.last_error() != errc::Ok ? wr_.last_error() : errc::IoError;
        recording_ = true; clip_t0_ = t0_us; ++clips_;
        return errc::Ok;
    }
    void finish_clip() noexcept { wr_.close(); recording_ = false; }
};

// ============================================================
// JSON bridge
// ============================================================

} // namespace knst_vid

#if defined(KNST_JSON_HPP)

namespace knst_vid {

inline knst_json metadata_to_json(const metadata& m) noexcept {
    knst_json j = knst_json::make_object();
    if (!j.is_object()) return knst_json();
    j.set("title",     knst_json(m.title));
    j.set("artist",    knst_json(m.artist));
    j.set("album",     knst_json(m.album));
    j.set("comment",   knst_json(m.comment));
    j.set("copyright", knst_json(m.copyright));
    j.set("software",  knst_json(m.software));
    j.set("year",      knst_json((int64_t)m.year));
    j.set("track",     knst_json((int64_t)m.track));
    if (!m.chapters.empty()) {
        knst_json ch = knst_json::make_array();
        for (uint32_t i = 0; i < m.chapters.size(); ++i) {
            knst_json e = knst_json::make_object();
            if (!e.is_object()) continue;
            e.set("title",    knst_json(m.chapters[i].title));
            e.set("start_us", knst_json((int64_t)m.chapters[i].start_us));
            e.set("end_us",   knst_json((int64_t)m.chapters[i].end_us));
            ch.push_back(std::move(e));
        }
        j.set("chapters", std::move(ch));
    }
    return j;
}

inline knst_json subtitle_to_json(const knst_vector<subtitle_entry>& entries) noexcept {
    knst_json arr = knst_json::make_array();
    if (!arr.is_array()) return knst_json();
    for (uint32_t i = 0; i < entries.size(); ++i) {
        knst_json j = knst_json::make_object();
        if (!j.is_object()) continue;
        j.set("start_ms", knst_json((int64_t)entries[i].start_ms));
        j.set("end_ms",   knst_json((int64_t)entries[i].end_ms));
        j.set("text",     knst_json(entries[i].text));
        arr.push_back(std::move(j));
    }
    return arr;
}

inline knst_json stream_to_json(const stream_desc& s) noexcept {
    knst_json j = knst_json::make_object();
    if (!j.is_object()) return knst_json();
    j.set("index",  knst_json((int64_t)s.index));
    j.set("type",   knst_json(knst_c16string(s.kind == track_kind::Video ? u"video" : s.kind == track_kind::Audio ? u"audio" : s.kind == track_kind::Subtitle ? u"subtitle" : u"other")));
    if (s.kind == track_kind::Video) {
        j.set("codec", knst_json(knst_c16string(video_codec_name(s.vcodec))));
        j.set("width", knst_json((int64_t)s.width)); j.set("height", knst_json((int64_t)s.height));
        j.set("fps_num", knst_json((int64_t)s.fps_num)); j.set("fps_den", knst_json((int64_t)s.fps_den));
    } else if (s.kind == track_kind::Audio) {
        j.set("codec", knst_json(knst_c16string(audio_codec_name(s.acodec))));
        j.set("sample_rate", knst_json((int64_t)s.sample_rate)); j.set("channels", knst_json((int64_t)s.channels));
        j.set("bits", knst_json((int64_t)s.bits_per_sample));
    }
    j.set("packets",     knst_json((int64_t)s.frame_count));
    j.set("duration_us", knst_json((int64_t)s.duration_us));
    return j;
}

inline knst_json media_info_to_json(const media_info& m) noexcept {
    knst_json j = knst_json::make_object();
    if (!j.is_object()) return knst_json();
    j.set("duration_us", knst_json((int64_t)m.duration_us));
    j.set("metadata", metadata_to_json(m.meta));
    knst_json arr = knst_json::make_array();
    for (uint32_t i = 0; i < m.streams.size(); ++i) arr.push_back(stream_to_json(m.streams[i]));
    j.set("streams", std::move(arr));
    return j;
}

} // namespace knst_vid

#endif // KNST_JSON_HPP