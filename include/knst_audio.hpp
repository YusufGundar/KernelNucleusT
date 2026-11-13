// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_audio.hpp
----------------------------
    General-purpose audio: devices, volume, streams, container I/O, codecs, DSP.

    SCOPE
    =====
      • Device enumeration / volume / mute / balance / ports / cards / server
      • Per-application streams (sink-inputs)               (Linux)
      • Real-time blocking output/input stream
            Linux   : pa_simple (dlopen, no link dependency)
            Windows : WASAPI shared-mode render
      • FORMAT DETECTION (magic bytes + extension) for ~20 containers
      • NATIVE DECODERS (no external library, bounds-checked, no exceptions)
            WAV / RF64 / BW64 : PCM 8/16/24/32, float32/64, A-law, µ-law,
                                IMA-ADPCM, MS-ADPCM, WAVE_FORMAT_EXTENSIBLE
            AIFF / AIFC       : PCM (BE/LE "sowt"), float, A-law, µ-law
            AU / SND          : PCM 8/16/24/32, float32/64, A-law, µ-law
            FLAC              : full decoder (fixed/LPC/verbatim, CRC-8/16,
                                all channel modes, 4..32 bit)
            RAW               : any sample format / endianness
      • BACKENDS for MP3 / OGG-Vorbis / OPUS / AAC / M4A / WavPack / ...
            - libsndfile via dlopen/LoadLibrary (automatic, if installed)
            - your own decoder via codec_registry::add()
      • ENCODERS : WAV (u8..f64, A-law, µ-law), AIFF/AIFC, AU, RAW
                   + streaming wav_writer (recording, header patched on close)
      • DSP : analyze, gain, fade, trim, concat, mix, normalize (peak/RMS),
              channel matrix (5.1/7.1 downmix), windowed-sinc resampler
              (Kaiser, 4 quality levels), biquad filters (RBJ), FFT/spectrum,
              TPDF dither, limiter, reverse, pan, silence trim, generators
      • player (seek/loop, any format) and mixer (N sources → 1 bus)

    Microphone-specific APIs (capture, VAD, AGC, noise gate, pitch,
    beamforming, voice-activated recording) live in knst_microphone.hpp.

    NOTES
    =====
      • Internal sample representation is interleaved float [-1,1].
      • knst_vector sizes are uint32_t, so one decoded buffer is limited to
        decode_options::max_samples (default 2^30 samples = 4 GiB of float).
      • Callbacks are plain function pointers (+ void* user) so the header
        does not depend on a particular knst_function signature.
*/

#pragma once

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>
#include <limits>
#include <utility>

#if !defined(M_PI)
#  define M_PI 3.14159265358979323846
#endif

#if KNST_USING_PLATFORM_WINDOWS
    #include <objbase.h>
    #include <propsys.h>
    #include <mmdeviceapi.h>
    #include <endpointvolume.h>
    #include <audioclient.h>
    #include <audiopolicy.h>
    #include <functiondiscoverykeys_devpkey.h>
    #include <ksmedia.h>
    #include <mmsystem.h>
    #pragma comment(lib, "ole32.lib")
    #pragma comment(lib, "winmm.lib")
#else
    #include <dlfcn.h>
    #include <unistd.h>
    #include <sys/stat.h>
#endif

// ═════════════════════════════════════════════════════════════════════════════
//  MP3 decoder — minimp3 single-header (CC0 public domain)
//  sOURCE: https://github.com/lieff/minimp3
// ═════════════════════════════════════════════════════════════════════════════
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#undef MINIMP3_IMPLEMENTATION

// ═════════════════════════════════════════════════════════════════════════════
//  AAC decoder — FAAD2 (optional, build-time dep — CMake sets KNST_NETWORK_HAVE_FAAD2)
//  Vorbis encoder — libvorbis/libvorbisenc/libogg (build-time — KNST_NETWORK_HAVE_VORBIS)
// ═════════════════════════════════════════════════════════════════════════════
#if defined(KNST_NETWORK_HAVE_FAAD2)
#include <neaacdec.h>
#endif
#if defined(KNST_NETWORK_HAVE_VORBIS)
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <ogg/ogg.h>
#endif

namespace knst_aud {

using knst_dev::status;
using knst_dev::result;

// Forward declarations (defined at the end of this file)
class stream;
inline result<void> play_file_any(const knst_audio_device* device,const knst_c16string& path, const std::atomic<bool>* stop) noexcept;



struct level_info {
    double peak = 0.0;
    double rms = 0.0;
    uint32_t samples = 0;
    uint32_t clipped = 0;
    double peak_db = -144.0;
    double rms_db = -144.0;
};

struct play_options {
    uint32_t rate = 48000;
    uint32_t channels = 2;
    uint32_t bits_per_sample = 16;
};

struct server_info {
    knst_c16string name;
    knst_c16string version;
    knst_c16string default_sink;
    knst_c16string default_source;
    knst_c16string sample_spec;
    knst_c16string channel_map;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    bool valid = false;
};

struct card_info {
    uint32_t index = 0;
    knst_c16string name;
    knst_c16string driver;
    knst_c16string active_profile;
    knst_vector<knst_c16string> profiles;
    knst_vector<knst_c16string> ports;
    bool valid = false;
};

struct stream_info {
    uint32_t index = 0;
    knst_c16string driver;
    knst_c16string application;
    knst_c16string media_name;
    uint32_t device_index = 0;
    uint32_t volume_percent = 0;
    bool muted      = false;
    bool corked     = false;
    bool is_output  = true;
    bool valid      = false;
};

struct device_format {
    uint32_t rate            = 0;
    uint32_t channels        = 0;
    uint32_t bits_per_sample = 0;
    uint64_t channel_mask    = 0;
    bool valid = false;
};

/// Legacy flat WAV header description (kept for source compatibility).
struct wav_info {
    uint32_t rate            = 0;
    uint32_t channels        = 0;
    uint32_t bits_per_sample = 0;
    uint32_t data_offset     = 0;
    uint32_t data_size       = 0;
    uint16_t format_tag      = 0;
    bool valid = false;
};

/// Container (file) type.
enum class container_t : uint8_t {
    unknown, raw, wav, rf64, w64, aiff, aifc, au, caf, flac, ogg, mp3,
    aac_adts, mp4, wavpack, ape, tta, dsf, dsdiff, midi, amr, ac3, asf,
    matroska
};

/// Codec carried inside a container.
enum class codec_t : uint8_t {
    unknown, pcm, float_pcm, alaw, ulaw, ima_adpcm, ms_adpcm,
    flac, mp3, vorbis, opus, aac
};

/// Raw sample encoding (also used by raw I/O).
enum class sample_fmt : uint8_t { u8, i8, i16, i24, i32, f32, f64, alaw, ulaw };

inline const char* container_name(container_t c) noexcept {
    switch (c) {
        case container_t::raw:      return "raw";
        case container_t::wav:      return "wav";
        case container_t::rf64:     return "rf64";
        case container_t::w64:      return "w64";
        case container_t::aiff:     return "aiff";
        case container_t::aifc:     return "aifc";
        case container_t::au:       return "au";
        case container_t::caf:      return "caf";
        case container_t::flac:     return "flac";
        case container_t::ogg:      return "ogg";
        case container_t::mp3:      return "mp3";
        case container_t::aac_adts: return "aac";
        case container_t::mp4:      return "mp4/m4a";
        case container_t::wavpack:  return "wavpack";
        case container_t::ape:      return "ape";
        case container_t::tta:      return "tta";
        case container_t::dsf:      return "dsf";
        case container_t::dsdiff:   return "dsdiff";
        case container_t::midi:     return "midi";
        case container_t::amr:      return "amr";
        case container_t::ac3:      return "ac3";
        case container_t::asf:      return "asf/wma";
        case container_t::matroska: return "matroska/webm";
        default:                    return "unknown";
    }
}

inline uint32_t sample_bytes(sample_fmt s) noexcept {
    switch (s) {
        case sample_fmt::u8: case sample_fmt::i8:
        case sample_fmt::alaw: case sample_fmt::ulaw: return 1;
        case sample_fmt::i16: return 2;
        case sample_fmt::i24: return 3;
        case sample_fmt::i32: case sample_fmt::f32: return 4;
        case sample_fmt::f64: return 8;
    }
    return 2;
}

// ============================================================
// Low-level helpers
// ============================================================

namespace adetail {

inline uint16_t le16(const uint8_t* p) noexcept { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t* p) noexcept {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint64_t le64(const uint8_t* p) noexcept { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }
inline uint16_t be16(const uint8_t* p) noexcept { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) noexcept {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
inline uint64_t be64(const uint8_t* p) noexcept { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

inline void put_le16(uint8_t* p, uint32_t v) noexcept { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void put_le32(uint8_t* p, uint32_t v) noexcept {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline void put_be16(uint8_t* p, uint32_t v) noexcept { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
inline void put_be32(uint8_t* p, uint32_t v) noexcept {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

inline bool tag4(const uint8_t* p, const char* t) noexcept { return std::memcmp(p, t, 4) == 0; }

inline int clz64(uint64_t x) noexcept {   // x != 0
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long i; _BitScanReverse64(&i, x); return 63 - (int)i;
#else
    return __builtin_clzll(x);
#endif
}

// size() for knst_vector, length() for knst_byte_string / knst_c16string
template<typename V> inline auto cur_len(const V& v, int) noexcept -> decltype((uint32_t)v.size()) { return (uint32_t)v.size(); }
template<typename V> inline uint32_t cur_len(const V& v, long) noexcept { return (uint32_t)v.length(); }

template<typename V>
inline bool resize_ok(V& v, uint32_t n) noexcept { v.resize(n); return cur_len(v, 0) == n; }

/// Geometric growth helper: keeps capacity bookkeeping so repeated
/// "append n more samples" never degrades to O(n^2) reallocation.
template<typename V>
inline bool grow(V& v, uint32_t& cap, uint64_t new_size) noexcept {
    if (new_size > 0xFFFFFFFFull) return false;
    if (new_size > cap) {
        uint64_t nc = (uint64_t)cap * 2;
        if (nc < new_size) nc = new_size;
        if (nc > 0xFFFFFFFFull) nc = 0xFFFFFFFFull;
        v.reserve((uint32_t)nc);
        cap = (uint32_t)nc;
    }
    return resize_ok(v, (uint32_t)new_size);
}

inline float finite(float v) noexcept { return (v == v && v - v == 0.0f) ? v : 0.0f; }

/// xorshift64* PRNG — thread-local friendly, no global state (unlike rand()).
struct rng {
    uint64_t s;
    explicit rng(uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept : s(seed ? seed : 1) {}
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 32); }
    float uniform() noexcept { return (float)(next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float tpdf() noexcept { return uniform() - uniform(); }                           // (-1,1)
};

// ---- G.711 -------------------------------------------------

inline int16_t alaw_to_i16(uint8_t a) noexcept {
    a ^= 0x55;
    int t = (a & 0x0F) << 4;
    int seg = (a & 0x70) >> 4;
    if (seg == 0) t += 8;
    else { t += 0x108; if (seg > 1) t <<= (seg - 1); }
    return (int16_t)((a & 0x80) ? t : -t);
}
inline int16_t ulaw_to_i16(uint8_t u) noexcept {
    u = (uint8_t)~u;
    int t = (((u & 0x0F) << 3) + 0x84) << ((u & 0x70) >> 4);
    t -= 0x84;
    return (int16_t)((u & 0x80) ? -t : t);
}
inline uint8_t i16_to_alaw(int pcm) noexcept {
    static const int seg_end[8] = { 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF };
    int mask;
    pcm >>= 3;
    if (pcm >= 0) mask = 0xD5; else { mask = 0x55; pcm = -pcm - 1; }
    int seg = 0;
    while (seg < 8 && pcm > seg_end[seg]) ++seg;
    if (seg >= 8) return (uint8_t)(0x7F ^ mask);
    int a = seg << 4;
    a |= (seg < 2) ? ((pcm >> 1) & 0x0F) : ((pcm >> seg) & 0x0F);
    return (uint8_t)(a ^ mask);
}
inline uint8_t i16_to_ulaw(int pcm) noexcept {
    int sign = (pcm >> 8) & 0x80;
    if (sign) pcm = -pcm;
    if (pcm > 32635) pcm = 32635;
    pcm += 0x84;
    int e = 0;
    for (int t = (pcm >> 7) & 0xFF; t > 1; t >>= 1) ++e;
    int m = (pcm >> (e + 3)) & 0x0F;
    return (uint8_t)~(sign | (e << 4) | m);
}

struct g711_tables {
    float alaw[256];
    float ulaw[256];
    g711_tables() noexcept {
        for (int i = 0; i < 256; ++i) {
            alaw[i] = alaw_to_i16((uint8_t)i) * (1.0f / 32768.0f);
            ulaw[i] = ulaw_to_i16((uint8_t)i) * (1.0f / 32768.0f);
        }
    }
};
inline const g711_tables& g711() noexcept { static const g711_tables t; return t; }

// ---- generic sample (de)serialisation ------------------------

/// Convert `n` raw samples to float.
inline void decode_samples(const uint8_t* p, size_t n, sample_fmt f, bool be, float* out) noexcept {
    switch (f) {
        case sample_fmt::u8:
            for (size_t i = 0; i < n; ++i) out[i] = ((int)p[i] - 128) * (1.0f / 128.0f);
            break;
        case sample_fmt::i8:
            for (size_t i = 0; i < n; ++i) out[i] = (int8_t)p[i] * (1.0f / 128.0f);
            break;
        case sample_fmt::i16:
            if (be) for (size_t i = 0; i < n; ++i) out[i] = (int16_t)be16(p + i * 2) * (1.0f / 32768.0f);
            else    for (size_t i = 0; i < n; ++i) out[i] = (int16_t)le16(p + i * 2) * (1.0f / 32768.0f);
            break;
        case sample_fmt::i24:
            for (size_t i = 0; i < n; ++i) {
                const uint8_t* q = p + i * 3;
                int32_t v = be ? ((q[0] << 16) | (q[1] << 8) | q[2]) : (q[0] | (q[1] << 8) | (q[2] << 16));
                if (v & 0x800000) v |= (int32_t)0xFF000000;
                out[i] = v * (1.0f / 8388608.0f);
            }
            break;
        case sample_fmt::i32:
            if (be) for (size_t i = 0; i < n; ++i) out[i] = (float)((int32_t)be32(p + i * 4) * (1.0 / 2147483648.0));
            else    for (size_t i = 0; i < n; ++i) out[i] = (float)((int32_t)le32(p + i * 4) * (1.0 / 2147483648.0));
            break;
        case sample_fmt::f32:
            for (size_t i = 0; i < n; ++i) {
                uint32_t u = be ? be32(p + i * 4) : le32(p + i * 4);
                float v; std::memcpy(&v, &u, 4);
                out[i] = finite(v);
            }
            break;
        case sample_fmt::f64:
            for (size_t i = 0; i < n; ++i) {
                uint64_t u = be ? be64(p + i * 8) : le64(p + i * 8);
                double v; std::memcpy(&v, &u, 8);
                out[i] = finite((float)v);
            }
            break;
        case sample_fmt::alaw: { const auto& t = g711(); for (size_t i = 0; i < n; ++i) out[i] = t.alaw[p[i]]; break; }
        case sample_fmt::ulaw: { const auto& t = g711(); for (size_t i = 0; i < n; ++i) out[i] = t.ulaw[p[i]]; break; }
    }
}

/// Convert `n` floats to raw samples. `dither` != nullptr → TPDF (1 LSB) for
/// integer formats up to 16 bit.
inline void encode_samples(const float* in, size_t n, sample_fmt f, bool be,
                           uint8_t* out, rng* dither = nullptr) noexcept {
    auto clampf = [](float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : (v == v ? v : 0.0f)); };
    switch (f) {
        case sample_fmt::u8:
            for (size_t i = 0; i < n; ++i) {
                float v = clampf(in[i]) * 128.0f + (dither ? dither->tpdf() : 0.0f);
                long q = std::lrintf(v) + 128; out[i] = (uint8_t)(q < 0 ? 0 : (q > 255 ? 255 : q));
            }
            break;
        case sample_fmt::i8:
            for (size_t i = 0; i < n; ++i) {
                float v = clampf(in[i]) * 128.0f + (dither ? dither->tpdf() : 0.0f);
                long q = std::lrintf(v); out[i] = (uint8_t)(int8_t)(q < -128 ? -128 : (q > 127 ? 127 : q));
            }
            break;
        case sample_fmt::i16:
            for (size_t i = 0; i < n; ++i) {
                float v = clampf(in[i]) * 32768.0f + (dither ? dither->tpdf() : 0.0f);
                long q = std::lrintf(v);
                if (q < -32768) q = -32768;
                if (q > 32767) q = 32767;
                if (be) put_be16(out + i * 2, (uint32_t)q & 0xFFFF); else put_le16(out + i * 2, (uint32_t)q & 0xFFFF);
            }
            break;
        case sample_fmt::i24:
            for (size_t i = 0; i < n; ++i) {
                long q = std::lrintf(clampf(in[i]) * 8388608.0f);
                if (q < -8388608) q = -8388608;
                if (q > 8388607) q = 8388607;
                uint32_t u = (uint32_t)q & 0xFFFFFF; uint8_t* o = out + i * 3;
                if (be) { o[0] = (uint8_t)(u >> 16); o[1] = (uint8_t)(u >> 8); o[2] = (uint8_t)u; }
                else    { o[0] = (uint8_t)u; o[1] = (uint8_t)(u >> 8); o[2] = (uint8_t)(u >> 16); }
            }
            break;
        case sample_fmt::i32:
            for (size_t i = 0; i < n; ++i) {
                double d = (double)clampf(in[i]) * 2147483648.0;
                int64_t q = std::llrint(d); if (q > 2147483647ll) q = 2147483647ll; if (q < -2147483648ll) q = -2147483648ll;
                if (be) put_be32(out + i * 4, (uint32_t)(int32_t)q); else put_le32(out + i * 4, (uint32_t)(int32_t)q);
            }
            break;
        case sample_fmt::f32:
            for (size_t i = 0; i < n; ++i) {
                float v = finite(in[i]); uint32_t u; std::memcpy(&u, &v, 4);
                if (be) put_be32(out + i * 4, u); else put_le32(out + i * 4, u);
            }
            break;
        case sample_fmt::f64:
            for (size_t i = 0; i < n; ++i) {
                double v = finite(in[i]); uint64_t u; std::memcpy(&u, &v, 8);
                uint8_t* o = out + i * 8;
                if (be) { put_be32(o, (uint32_t)(u >> 32)); put_be32(o + 4, (uint32_t)u); }
                else    { put_le32(o, (uint32_t)u); put_le32(o + 4, (uint32_t)(u >> 32)); }
            }
            break;
        case sample_fmt::alaw:
            for (size_t i = 0; i < n; ++i) {
                float v = clampf(in[i]) * 32768.0f + (dither ? dither->tpdf() * 16.0f : 0.0f);
                long q = std::lrintf(v); if (q < -32768) q = -32768; if (q > 32767) q = 32767;
                out[i] = i16_to_alaw((int)q);
            }
            break;
        case sample_fmt::ulaw:
            for (size_t i = 0; i < n; ++i) {
                float v = clampf(in[i]) * 32768.0f + (dither ? dither->tpdf() * 16.0f : 0.0f);
                long q = std::lrintf(v); if (q < -32768) q = -32768; if (q > 32767) q = 32767;
                out[i] = i16_to_ulaw((int)q);
            }
            break;
    }
}

// ---- 80-bit IEEE extended (AIFF sample rate) ---------------

inline double read_f80(const uint8_t* p) noexcept {
    int e = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t m = be64(p + 2);
    if (e == 0 && m == 0) return 0.0;
    if (e == 0x7FFF) return 0.0;                         // inf / nan: reject
    double v = std::ldexp((double)m, e - 16383 - 63);
    return (p[0] & 0x80) ? -v : v;
}
inline void write_f80(uint8_t* p, double v) noexcept {
    std::memset(p, 0, 10);
    if (!(v > 0.0)) return;
    int e; double m = std::frexp(v, &e);                 // v = m * 2^e, m in [0.5,1)
    uint64_t mant = (uint64_t)std::ldexp(m, 64);
    uint32_t be_ = (uint32_t)(e - 1 + 16383);
    put_be16(p, be_);
    put_be32(p + 2, (uint32_t)(mant >> 32));
    put_be32(p + 6, (uint32_t)mant);
}

} // namespace adetail

// ============================================================
// Helpers
// ============================================================

inline knst_c16string format_duration(uint32_t seconds) noexcept {
    char buf[32];
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds % 3600) / 60;
    uint32_t s = seconds % 60;
    if (h > 0) ::snprintf(buf, sizeof(buf), "%u:%02u:%02u", h, m, s);
    else       ::snprintf(buf, sizeof(buf), "%u:%02u", m, s);
    return knst_c16string(buf);
}

inline double percent_to_db(uint32_t percent) noexcept {
    if (percent == 0) return -144.0;
    double linear = percent / 100.0;
    double cubic  = linear * linear * linear;
    if (cubic <= 0.0) return -144.0;
    return 20.0 * std::log10(cubic);
}

inline uint32_t db_to_percent(double db) noexcept {
    if (db <= -144.0) return 0;
    double cubic  = std::pow(10.0, db / 20.0);
    double linear = std::cbrt(cubic);
    if (linear >= 1.5) linear = 1.5;
    uint32_t p = static_cast<uint32_t>(linear * 100.0 + 0.5);
    return p > 150 ? 150 : p;
}

/// Writes a canonical 44-byte PCM WAV header (legacy helper).
inline void write_wav_header(unsigned char* hdr, uint32_t data_size, uint32_t rate,
                             uint16_t channels, uint16_t bits, uint16_t format_tag = 1) noexcept {
    std::memcpy(hdr + 0, "RIFF", 4);
    adetail::put_le32(hdr + 4, 36 + data_size);
    std::memcpy(hdr + 8, "WAVE", 4);
    std::memcpy(hdr + 12, "fmt ", 4);
    adetail::put_le32(hdr + 16, 16);
    adetail::put_le16(hdr + 20, format_tag);
    adetail::put_le16(hdr + 22, channels);
    adetail::put_le32(hdr + 24, rate);
    adetail::put_le32(hdr + 28, rate * channels * (bits / 8));
    adetail::put_le16(hdr + 32, (uint32_t)(channels * (bits / 8)));
    adetail::put_le16(hdr + 34, bits);
    std::memcpy(hdr + 36, "data", 4);
    adetail::put_le32(hdr + 40, data_size);
}

// ============================================================
// Decoded audio + public codec types
// ============================================================

/// Decoded audio: interleaved float samples in [-1, 1].
struct pcm_buffer {
    knst_vector<float> samples;
    uint32_t    rate        = 0;
    uint32_t    channels    = 0;
    uint32_t    source_bits = 0;               // bit depth in the source (0 = lossy / unknown)
    container_t container   = container_t::unknown;
    codec_t     codec       = codec_t::unknown;

    uint32_t frames()   const noexcept { return channels ? (uint32_t)(samples.size() / channels) : 0; }
    double   duration() const noexcept { return rate ? (double)frames() / rate : 0.0; }
    bool     valid()    const noexcept { return rate && channels && samples.size() >= channels; }
};

/// Header-level description of a file (no full decode needed).
struct media_info {
    container_t container = container_t::unknown;
    codec_t     codec     = codec_t::unknown;
    uint32_t    rate      = 0;
    uint32_t    channels  = 0;
    uint32_t    bits      = 0;
    uint64_t    frames    = 0;                 // 0 = unknown
    double      duration  = 0.0;               // seconds, 0 = unknown
    bool        native    = false;             // decodable without a backend
    bool        valid     = false;
};

struct decode_options {
    uint32_t max_samples  = 0x40000000u;       // hard cap (samples, not frames)
    bool     lenient      = true;              // damaged tail → return what was decoded
    bool     use_backend  = true;              // allow codec_registry / libsndfile
    // RAW only:
    uint32_t raw_rate     = 44100;
    uint32_t raw_channels = 2;
    sample_fmt raw_format = sample_fmt::i16;
    bool     raw_big_endian = false;
};

using decoder_fn = result<pcm_buffer> (*)(const uint8_t* data, uint32_t size,
                                          const decode_options& opt, void* user);

namespace codec {

// ------------------------------------------------------------
// Container detection
// ------------------------------------------------------------

inline container_t detect(const uint8_t* d, size_t n) noexcept {
    using namespace adetail;
    if (!d || n < 4) return container_t::unknown;
    if (n >= 12 && tag4(d, "RIFF") && tag4(d + 8, "WAVE")) return container_t::wav;
    if (n >= 12 && (tag4(d, "RF64") || tag4(d, "BW64")) && tag4(d + 8, "WAVE")) return container_t::rf64;
    if (n >= 16 && tag4(d, "riff") && d[4] == 0x2E && d[5] == 0x91 && d[6] == 0xCF && d[7] == 0x11) return container_t::w64;
    if (n >= 12 && tag4(d, "FORM")) {
        if (tag4(d + 8, "AIFF")) return container_t::aiff;
        if (tag4(d + 8, "AIFC")) return container_t::aifc;
    }
    if (tag4(d, ".snd")) return container_t::au;
    if (tag4(d, "caff")) return container_t::caf;
    if (tag4(d, "fLaC")) return container_t::flac;
    if (tag4(d, "OggS")) return container_t::ogg;
    if (tag4(d, "wvpk")) return container_t::wavpack;
    if (tag4(d, "MAC ")) return container_t::ape;
    if (tag4(d, "TTA1")) return container_t::tta;
    if (tag4(d, "DSD ")) return container_t::dsf;
    if (tag4(d, "FRM8")) return container_t::dsdiff;
    if (tag4(d, "MThd")) return container_t::midi;
    if (n >= 6 && std::memcmp(d, "#!AMR", 5) == 0) return container_t::amr;
    if (n >= 8 && tag4(d + 4, "ftyp")) return container_t::mp4;
    if (d[0] == 0x30 && d[1] == 0x26 && d[2] == 0xB2 && d[3] == 0x75) return container_t::asf;
    if (d[0] == 0x1A && d[1] == 0x45 && d[2] == 0xDF && d[3] == 0xA3) return container_t::matroska;
    if (d[0] == 0x0B && d[1] == 0x77) return container_t::ac3;
    if (n >= 10 && std::memcmp(d, "ID3", 3) == 0) {          // tagged MP3 / AAC / FLAC
        uint32_t sz = ((uint32_t)(d[6] & 0x7F) << 21) | ((uint32_t)(d[7] & 0x7F) << 14) |
                      ((uint32_t)(d[8] & 0x7F) << 7) | (uint32_t)(d[9] & 0x7F);
        size_t off = 10ull + sz;
        if (off + 4 <= n) {
            container_t inner = detect(d + off, n - off);
            if (inner != container_t::unknown) return inner;
        }
        return container_t::mp3;
    }
    if (d[0] == 0xFF && (d[1] & 0xE0) == 0xE0) {                // MPEG audio sync
        if ((d[1] & 0xF6) == 0xF0) return container_t::aac_adts;  // layer 00 + sync 0xFFF
        int ver = (d[1] >> 3) & 3, layer = (d[1] >> 1) & 3;
        if (ver != 1 && layer != 0) return container_t::mp3;
    }
    return container_t::unknown;
}

inline bool ext_eq(const knst_c16string& ext, const char* lower) noexcept {
    uint32_t i = 0;
    for (; lower[i]; ++i) {
        if (i >= ext.length()) return false;
        char16_t c = ext[i];
        if (c >= u'A' && c <= u'Z') c = (char16_t)(c + 32);
        if (c != (char16_t)(unsigned char)lower[i]) return false;
    }
    return i == ext.length();
}

/// Guess the container from a file extension (".wav", "mp3", ...).
inline container_t detect_extension(const knst_c16string& path) noexcept {
    uint32_t dot = UINT32_MAX;
    for (uint32_t i = path.length(); i > 0; --i) {
        char16_t c = path[i - 1];
        if (c == u'.') { dot = i - 1; break; }
        if (c == u'/' || c == u'\\') break;
    }
    if (dot == UINT32_MAX) return container_t::unknown;
    knst_c16string e = path.substr(dot + 1, path.length() - dot - 1);
    if (ext_eq(e, "wav") || ext_eq(e, "wave") || ext_eq(e, "bwf")) return container_t::wav;
    if (ext_eq(e, "w64")) return container_t::w64;
    if (ext_eq(e, "aif") || ext_eq(e, "aiff")) return container_t::aiff;
    if (ext_eq(e, "aifc")) return container_t::aifc;
    if (ext_eq(e, "au") || ext_eq(e, "snd")) return container_t::au;
    if (ext_eq(e, "caf")) return container_t::caf;
    if (ext_eq(e, "flac")) return container_t::flac;
    if (ext_eq(e, "ogg") || ext_eq(e, "oga") || ext_eq(e, "opus") || ext_eq(e, "spx")) return container_t::ogg;
    if (ext_eq(e, "mp3") || ext_eq(e, "mp2")) return container_t::mp3;
    if (ext_eq(e, "aac")) return container_t::aac_adts;
    if (ext_eq(e, "m4a") || ext_eq(e, "mp4") || ext_eq(e, "m4b") || ext_eq(e, "alac")) return container_t::mp4;
    if (ext_eq(e, "wv")) return container_t::wavpack;
    if (ext_eq(e, "ape")) return container_t::ape;
    if (ext_eq(e, "tta")) return container_t::tta;
    if (ext_eq(e, "dsf")) return container_t::dsf;
    if (ext_eq(e, "mid") || ext_eq(e, "midi")) return container_t::midi;
    if (ext_eq(e, "amr")) return container_t::amr;
    if (ext_eq(e, "ac3")) return container_t::ac3;
    if (ext_eq(e, "wma") || ext_eq(e, "asf")) return container_t::asf;
    if (ext_eq(e, "mka") || ext_eq(e, "webm") || ext_eq(e, "weba")) return container_t::matroska;
    if (ext_eq(e, "raw") || ext_eq(e, "pcm")) return container_t::raw;
    return container_t::unknown;
}

inline bool is_native(container_t c) noexcept {
    return c == container_t::wav || c == container_t::rf64 || c == container_t::aiff ||
           c == container_t::aifc || c == container_t::au || c == container_t::flac ||
           c == container_t::raw;
}

// ------------------------------------------------------------
// Layout (header) parsers: WAV / AIFF / AU
// ------------------------------------------------------------

struct layout {
    container_t container = container_t::unknown;
    codec_t     codec     = codec_t::unknown;
    sample_fmt  fmt       = sample_fmt::i16;
    bool        big_endian = false;
    uint32_t    rate = 0, channels = 0, bits = 0;
    uint32_t    block_align = 0, samples_per_block = 0;
    uint64_t    data_off = 0, data_size = 0;
    uint64_t    declared_frames = 0;           // AIFF COMM / AU unknown = 0
    int16_t     coefs[16][2] = {};
    uint32_t    num_coefs = 0;
    bool        valid = false;
};

inline bool pick_pcm_format(uint32_t tag, uint32_t bytes, layout& L) noexcept {
    switch (tag) {
        case 1:                                  // integer PCM
            L.codec = codec_t::pcm;
            if (bytes == 1) { L.fmt = sample_fmt::u8;  return true; }
            if (bytes == 2) { L.fmt = sample_fmt::i16; return true; }
            if (bytes == 3) { L.fmt = sample_fmt::i24; return true; }
            if (bytes == 4) { L.fmt = sample_fmt::i32; return true; }
            return false;
        case 3:                                  // IEEE float
            L.codec = codec_t::float_pcm;
            if (bytes == 4) { L.fmt = sample_fmt::f32; return true; }
            if (bytes == 8) { L.fmt = sample_fmt::f64; return true; }
            return false;
        case 6: L.codec = codec_t::alaw; L.fmt = sample_fmt::alaw; return bytes == 1;
        case 7: L.codec = codec_t::ulaw; L.fmt = sample_fmt::ulaw; return bytes == 1;
        default: return false;
    }
}

inline layout parse_wav(const uint8_t* d, size_t n) noexcept {
    using namespace adetail;
    layout L;
    if (!d || n < 12) return L;
    bool rf = tag4(d, "RF64") || tag4(d, "BW64");
    if (!(rf || tag4(d, "RIFF")) || !tag4(d + 8, "WAVE")) return L;
    L.container = rf ? container_t::rf64 : container_t::wav;

    uint64_t ds64_data = 0; bool have_ds64 = false, have_fmt = false;
    uint32_t tag = 0;
    uint64_t pos = 12;
    while (pos + 8 <= n) {
        const uint8_t* c = d + pos;
        uint64_t csz = le32(c + 4);
        uint64_t body = pos + 8;
        if (tag4(c, "ds64") && csz >= 16 && body + 16 <= n) {
            ds64_data = le64(d + body + 8); have_ds64 = true;
        } else if (tag4(c, "fmt ") && csz >= 16 && body + 16 <= n) {
            tag = le16(d + body);
            L.channels = le16(d + body + 2);
            L.rate = le32(d + body + 4);
            L.block_align = le16(d + body + 12);
            L.bits = le16(d + body + 14);
            if (tag == 0xFFFE && csz >= 40 && body + 40 <= n) tag = le16(d + body + 24);   // SubFormat GUID
            if ((tag == 0x11 || tag == 0x02) && csz >= 20 && body + 20 <= n) L.samples_per_block = le16(d + body + 18);
            if (tag == 0x02 && csz >= 22 && body + 22 <= n) {
                uint32_t nc = le16(d + body + 20);
                if (nc > 16) return L;
                if (body + 22 + (uint64_t)nc * 4 > n) return L;
                for (uint32_t i = 0; i < nc; ++i) {
                    L.coefs[i][0] = (int16_t)le16(d + body + 22 + i * 4);
                    L.coefs[i][1] = (int16_t)le16(d + body + 24 + i * 4);
                }
                L.num_coefs = nc;
            }
            have_fmt = true;
        } else if (tag4(c, "data")) {
            if (!have_fmt) return L;
            uint64_t sz = csz;
            if (rf && csz == 0xFFFFFFFFull && have_ds64) sz = ds64_data;
            if (sz > n - body || (csz == 0xFFFFFFFFull && !(rf && have_ds64))) sz = n - body;   // streamed / truncated
            L.data_off = body; L.data_size = sz;
            break;
        }
        uint64_t next = body + csz + (csz & 1);
        if (next <= pos) return L;
        pos = next;
    }
    if (!have_fmt || L.data_off == 0 || L.channels == 0 || L.rate == 0) return L;

    if (tag == 0x11 || tag == 0x02) {
        if (L.block_align == 0) return L;
        L.codec = (tag == 0x11) ? codec_t::ima_adpcm : codec_t::ms_adpcm;
        if (tag == 0x11 && L.bits != 4) return L;
        if (tag == 0x02) {
            if (L.bits != 4) return L;
            if (L.num_coefs == 0) {                 // default MS-ADPCM coefficient set
                static const int16_t def[7][2] = { {256,0},{512,-256},{0,0},{192,64},{240,0},{460,-208},{392,-232} };
                for (int i = 0; i < 7; ++i) { L.coefs[i][0] = def[i][0]; L.coefs[i][1] = def[i][1]; }
                L.num_coefs = 7;
            }
        }
        if (L.channels > 8) return L;
        L.valid = true;
        return L;
    }
    uint32_t bytes = (L.block_align && L.channels) ? L.block_align / L.channels : (L.bits + 7) / 8;
    if (bytes == 0) bytes = (L.bits + 7) / 8;
    if (!pick_pcm_format(tag, bytes, L)) return L;
    if (L.channels > 256) return L;
    if (L.bits == 0) L.bits = bytes * 8;
    L.valid = true;
    return L;
}

inline layout parse_aiff(const uint8_t* d, size_t n) noexcept {
    using namespace adetail;
    layout L;
    if (!d || n < 12 || !tag4(d, "FORM")) return L;
    bool aifc = tag4(d + 8, "AIFC");
    if (!aifc && !tag4(d + 8, "AIFF")) return L;
    L.container = aifc ? container_t::aifc : container_t::aiff;
    L.big_endian = true;

    bool have_comm = false;
    uint8_t comp[4] = { 'N','O','N','E' };
    uint64_t pos = 12;
    while (pos + 8 <= n) {
        const uint8_t* c = d + pos;
        uint64_t csz = be32(c + 4), body = pos + 8;
        if (tag4(c, "COMM") && csz >= 18 && body + 18 <= n) {
            L.channels = be16(d + body);
            L.declared_frames = be32(d + body + 2);
            L.bits = be16(d + body + 6);
            double r = read_f80(d + body + 8);
            L.rate = (r > 0.0 && r < 4294967295.0) ? (uint32_t)(r + 0.5) : 0;
            if (aifc && csz >= 22 && body + 22 <= n) std::memcpy(comp, d + body + 18, 4);
            have_comm = true;
        } else if (tag4(c, "SSND") && csz >= 8 && body + 8 <= n) {
            uint64_t off = be32(d + body);
            L.data_off = body + 8 + off;
            uint64_t avail = (L.data_off <= n) ? n - L.data_off : 0;
            uint64_t sz = csz - 8; if (off <= sz) sz -= off; else sz = 0;
            L.data_size = sz < avail ? sz : avail;
            if (have_comm) break;
        }
        uint64_t next = body + csz + (csz & 1);
        if (next <= pos) return L;
        pos = next;
    }
    if (!have_comm || L.data_off == 0 || L.channels == 0 || L.rate == 0 || L.channels > 256) return L;

    uint32_t bytes = (L.bits + 7) / 8;
    auto is = [&](const char* t) { return std::memcmp(comp, t, 4) == 0; };
    if (is("NONE") || is("twos") || is("in24") || is("in32")) {
        L.codec = codec_t::pcm;
        if (bytes == 1) L.fmt = sample_fmt::i8; else if (bytes == 2) L.fmt = sample_fmt::i16;
        else if (bytes == 3) L.fmt = sample_fmt::i24; else if (bytes == 4) L.fmt = sample_fmt::i32; else return L;
    } else if (is("sowt")) {
        L.codec = codec_t::pcm; L.big_endian = false;
        if (bytes == 2) L.fmt = sample_fmt::i16; else if (bytes == 3) L.fmt = sample_fmt::i24;
        else if (bytes == 4) L.fmt = sample_fmt::i32; else if (bytes == 1) L.fmt = sample_fmt::i8; else return L;
    } else if (is("fl32") || is("FL32")) { L.codec = codec_t::float_pcm; L.fmt = sample_fmt::f32; L.bits = 32; }
    else if (is("fl64") || is("FL64"))   { L.codec = codec_t::float_pcm; L.fmt = sample_fmt::f64; L.bits = 64; }
    else if (is("alaw") || is("ALAW"))   { L.codec = codec_t::alaw; L.fmt = sample_fmt::alaw; L.bits = 8; }
    else if (is("ulaw") || is("ULAW"))   { L.codec = codec_t::ulaw; L.fmt = sample_fmt::ulaw; L.bits = 8; }
    else return L;                                       // e.g. ima4 / MAC3: not supported
    L.valid = true;
    return L;
}

inline layout parse_au(const uint8_t* d, size_t n) noexcept {
    using namespace adetail;
    layout L;
    if (!d || n < 24 || !tag4(d, ".snd")) return L;
    L.container = container_t::au; L.big_endian = true;
    uint64_t off = be32(d + 4), dsz = be32(d + 8);
    uint32_t enc = be32(d + 12);
    L.rate = be32(d + 16); L.channels = be32(d + 20);
    if (off < 24 || off > n || L.rate == 0 || L.channels == 0 || L.channels > 256) return L;
    uint64_t avail = n - off;
    L.data_off = off;
    L.data_size = (dsz == 0xFFFFFFFFull || dsz > avail) ? avail : dsz;
    switch (enc) {
        case 1:  L.codec = codec_t::ulaw; L.fmt = sample_fmt::ulaw; L.bits = 8;  break;
        case 2:  L.codec = codec_t::pcm;  L.fmt = sample_fmt::i8;   L.bits = 8;  break;
        case 3:  L.codec = codec_t::pcm;  L.fmt = sample_fmt::i16;  L.bits = 16; break;
        case 4:  L.codec = codec_t::pcm;  L.fmt = sample_fmt::i24;  L.bits = 24; break;
        case 5:  L.codec = codec_t::pcm;  L.fmt = sample_fmt::i32;  L.bits = 32; break;
        case 6:  L.codec = codec_t::float_pcm; L.fmt = sample_fmt::f32; L.bits = 32; break;
        case 7:  L.codec = codec_t::float_pcm; L.fmt = sample_fmt::f64; L.bits = 64; break;
        case 27: L.codec = codec_t::alaw; L.fmt = sample_fmt::alaw; L.bits = 8;  break;
        default: return L;
    }
    L.valid = true;
    return L;
}

inline uint64_t layout_frames(const layout& L) noexcept {
    if (!L.valid || L.channels == 0) return 0;
    if (L.codec == codec_t::ima_adpcm || L.codec == codec_t::ms_adpcm) {
        if (L.block_align == 0) return 0;
        uint64_t full = L.data_size / L.block_align, rem = L.data_size % L.block_align;
        uint64_t spb = L.samples_per_block;
        if (spb == 0) {
            spb = (L.codec == codec_t::ima_adpcm)
                ? ((uint64_t)(L.block_align - 4u * L.channels) * 8) / (4u * L.channels) + 1
                : ((uint64_t)(L.block_align - 7u * L.channels) * 2) / L.channels + 2;
        }
        uint64_t f = full * spb;
        if (rem > 4u * L.channels) f += (L.codec == codec_t::ima_adpcm)
            ? 1 + ((rem - 4u * L.channels) / (4u * L.channels)) * 8 : 0;
        return f;
    }
    return L.data_size / ((uint64_t)sample_bytes(L.fmt) * L.channels);
}

// ------------------------------------------------------------
// ADPCM (WAV)
// ------------------------------------------------------------

namespace adpcm {

inline const int16_t ima_step[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
    157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,
    1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,
    8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767 };
inline const int8_t ima_index[16] = { -1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8 };

inline int ima_nibble(int nib, int& pred, int& idx) noexcept {
    int step = ima_step[idx];
    int diff = step >> 3;
    if (nib & 1) diff += step >> 2;
    if (nib & 2) diff += step >> 1;
    if (nib & 4) diff += step;
    if (nib & 8) diff = -diff;
    pred += diff;
    if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
    idx += ima_index[nib & 15];
    if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
    return pred;
}

/// Decodes one IMA block into `out` (frames*ch floats). Returns frames written.
inline uint32_t ima_block(const uint8_t* b, size_t len, uint32_t ch, uint32_t spb, float* out) noexcept {
    using namespace adetail;
    if (len < 4u * ch) return 0;
    int pred[8], idx[8];
    for (uint32_t c = 0; c < ch; ++c) {
        pred[c] = (int16_t)le16(b + 4 * c);
        idx[c]  = b[4 * c + 2]; if (idx[c] > 88) idx[c] = 88;
        out[c]  = pred[c] * (1.0f / 32768.0f);
    }
    uint32_t groups = (uint32_t)((len - 4u * ch) / (4u * ch));
    uint32_t frames = 1 + groups * 8;
    if (spb && frames > spb) frames = spb;
    for (uint32_t g = 0; g < groups; ++g)
        for (uint32_t c = 0; c < ch; ++c) {
            const uint8_t* src = b + 4u * ch + ((size_t)g * ch + c) * 4;
            for (uint32_t k = 0; k < 4; ++k) {
                uint32_t j = 1 + g * 8 + k * 2;
                if (j < frames)     out[(size_t)j * ch + c]       = ima_nibble(src[k] & 15, pred[c], idx[c]) * (1.0f / 32768.0f);
                if (j + 1 < frames) out[(size_t)(j + 1) * ch + c] = ima_nibble(src[k] >> 4, pred[c], idx[c]) * (1.0f / 32768.0f);
            }
        }
    return frames;
}

inline uint32_t ms_block(const uint8_t* b, size_t len, uint32_t ch, uint32_t spb,
                         const int16_t (*coefs)[2], uint32_t ncoef, float* out) noexcept {
    using namespace adetail;
    static const int adapt[16] = { 230,230,230,230,307,409,512,614,768,614,512,409,307,230,230,230 };
    if (ch > 8 || len < 7u * ch) return 0;
    int c1[8], c2[8], delta[8], s1[8], s2[8];
    const uint8_t* p = b;
    for (uint32_t c = 0; c < ch; ++c) {
        uint32_t i = *p++;
        if (i >= ncoef) return 0;
        c1[c] = coefs[i][0]; c2[c] = coefs[i][1];
    }
    for (uint32_t c = 0; c < ch; ++c) { delta[c] = (int16_t)le16(p); p += 2; }
    for (uint32_t c = 0; c < ch; ++c) { s1[c] = (int16_t)le16(p); p += 2; }
    for (uint32_t c = 0; c < ch; ++c) { s2[c] = (int16_t)le16(p); p += 2; }
    size_t nib_total = (len - 7u * ch) * 2;
    uint32_t frames = 2 + (uint32_t)(nib_total / ch);
    if (spb && frames > spb) frames = spb;
    for (uint32_t c = 0; c < ch; ++c) {
        out[c] = s2[c] * (1.0f / 32768.0f);
        if (frames > 1) out[ch + c] = s1[c] * (1.0f / 32768.0f);
    }
    uint32_t total = (frames > 2) ? (frames - 2) * ch : 0;
    for (uint32_t k = 0; k < total; ++k) {
        int nib = (k & 1) ? (p[k >> 1] & 15) : (p[k >> 1] >> 4);
        uint32_t c = k % ch;
        int pr = (s1[c] * c1[c] + s2[c] * c2[c]) >> 8;
        pr += (nib >= 8 ? nib - 16 : nib) * delta[c];
        if (pr > 32767) pr = 32767; else if (pr < -32768) pr = -32768;
        s2[c] = s1[c]; s1[c] = pr;
        int dl = (adapt[nib] * delta[c]) >> 8; delta[c] = dl < 16 ? 16 : dl;
        out[(size_t)(2 + k / ch) * ch + c] = pr * (1.0f / 32768.0f);
    }
    return frames;
}

} // namespace adpcm

// ------------------------------------------------------------
// FLAC decoder
// ------------------------------------------------------------

namespace flacdec {

struct bitreader {
    const uint8_t* start; const uint8_t* p; const uint8_t* end;
    uint64_t acc = 0; int cnt = 0; bool err = false;
    bitreader(const uint8_t* s, const uint8_t* e) noexcept : start(s), p(s), end(e) {}
    void fill() noexcept { while (cnt <= 56 && p < end) { acc |= (uint64_t)*p++ << (56 - cnt); cnt += 8; } }
    uint32_t read(int n) noexcept {                       // 0..32
        if (n == 0) return 0;
        if (cnt < n) { fill(); if (cnt < n) { err = true; return 0; } }
        uint32_t v = (uint32_t)(acc >> (64 - n)); acc <<= n; cnt -= n; return v;
    }
    int64_t read_s(int n) noexcept {                      // 0..33 two's complement
        if (n == 0) return 0;
        if (n <= 32) { uint32_t v = read(n); return (int64_t)((int32_t)(v << (32 - n)) >> (32 - n)); }
        uint64_t hi = read(n - 32), lo = read(32);
        uint64_t v = (hi << 32) | lo;
        return (int64_t)(v << (64 - n)) >> (64 - n);
    }
    uint32_t unary() noexcept {
        uint32_t q = 0;
        for (;;) {
            if (cnt == 0) { fill(); if (cnt == 0) { err = true; return 0; } }
            if (acc == 0) { q += (uint32_t)cnt; acc = 0; cnt = 0; if (q > (1u << 28)) { err = true; return 0; } continue; }
            int lz = adetail::clz64(acc);
            q += (uint32_t)lz;
            if (lz + 1 >= 64) acc = 0; else acc <<= (lz + 1);
            cnt -= lz + 1;
            return q;
        }
    }
    void align() noexcept { int r = cnt & 7; acc <<= r; cnt -= r; }
    size_t bytepos() const noexcept { return (size_t)(p - start) - (size_t)(cnt / 8); }
};

inline uint8_t crc8(const uint8_t* d, size_t n) noexcept {
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) { c ^= d[i]; for (int k = 0; k < 8; ++k) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1); }
    return c;
}
inline uint16_t crc16(const uint8_t* d, size_t n) noexcept {
    static uint16_t table[256]; static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i) { uint16_t c = (uint16_t)(i << 8); for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x8005) : (uint16_t)(c << 1); table[i] = c; }
        init = true;
    }
    uint16_t c = 0;
    for (size_t i = 0; i < n; ++i) c = (uint16_t)((c << 8) ^ table[(c >> 8) ^ d[i]]);
    return c;
}

struct frame_ctx {
    uint32_t sr_info, bps_info;                 // STREAMINFO values
    knst_vector<int64_t> ch[8];                 // per-channel decoded samples
};

inline bool residual(bitreader& br, uint32_t bs, uint32_t order, int64_t* x) noexcept {
    uint32_t method = br.read(2);
    if (method > 1) return false;
    int pbits = method ? 5 : 4;
    uint32_t porder = br.read(4);
    uint32_t nparts = 1u << porder;
    if (porder > 0 && (bs & (nparts - 1))) return false;
    uint32_t psamples = bs >> porder;
    if (psamples < order) return false;
    uint32_t i = order;
    for (uint32_t p = 0; p < nparts; ++p) {
        uint32_t cntp = psamples - (p == 0 ? order : 0);
        uint32_t param = br.read(pbits);
        if (br.err) return false;
        if (param == (1u << pbits) - 1) {
            int bits = (int)br.read(5);
            for (uint32_t k = 0; k < cntp; ++k) x[i++] = br.read_s(bits);
        } else {
            for (uint32_t k = 0; k < cntp; ++k) {
                uint64_t q = br.unary();
                uint64_t u = (q << param) | (param ? br.read((int)param) : 0);
                x[i++] = (int64_t)(u >> 1) ^ -(int64_t)(u & 1);
            }
        }
        if (br.err) return false;
    }
    return true;
}

inline bool subframe(bitreader& br, uint32_t bs, int bps, int64_t* x) noexcept {
    if (br.read(1) != 0) return false;
    uint32_t type = br.read(6);
    uint32_t wasted = 0;
    if (br.read(1)) wasted = br.unary() + 1;
    if (br.err || (int)wasted >= bps) return false;
    int sb = bps - (int)wasted;
    if (type == 0) {                                    // CONSTANT
        int64_t v = br.read_s(sb);
        for (uint32_t i = 0; i < bs; ++i) x[i] = v;
    } else if (type == 1) {                             // VERBATIM
        for (uint32_t i = 0; i < bs; ++i) x[i] = br.read_s(sb);
    } else if (type >= 8 && type <= 12) {               // FIXED
        uint32_t order = type - 8;
        if (order > bs) return false;
        for (uint32_t i = 0; i < order; ++i) x[i] = br.read_s(sb);
        if (!residual(br, bs, order, x)) return false;
        for (uint32_t i = order; i < bs; ++i) {
            int64_t r = x[i];
            switch (order) {
                case 0: break;
                case 1: r += x[i-1]; break;
                case 2: r += 2*x[i-1] - x[i-2]; break;
                case 3: r += 3*x[i-1] - 3*x[i-2] + x[i-3]; break;
                case 4: r += 4*x[i-1] - 6*x[i-2] + 4*x[i-3] - x[i-4]; break;
            }
            x[i] = r;
        }
    } else if (type >= 32) {                            // LPC
        uint32_t order = (type & 31) + 1;
        if (order > bs) return false;
        for (uint32_t i = 0; i < order; ++i) x[i] = br.read_s(sb);
        uint32_t prec = br.read(4) + 1;
        if (prec == 16) return false;
        int shift = (int)br.read_s(5);
        if (shift < 0) return false;
        int64_t coef[32];
        for (uint32_t i = 0; i < order; ++i) coef[i] = br.read_s((int)prec);
        if (br.err || !residual(br, bs, order, x)) return false;
        for (uint32_t i = order; i < bs; ++i) {
            int64_t sum = 0;
            const int64_t* h = x + i - 1;
            for (uint32_t j = 0; j < order; ++j) sum += coef[j] * h[-(int)j];
            x[i] += sum >> shift;
        }
    } else return false;
    if (wasted) for (uint32_t i = 0; i < bs; ++i) x[i] = (int64_t)((uint64_t)x[i] << wasted);
    return !br.err;
}

/// Decodes one frame starting at `pos`; appends interleaved floats to `out`.
/// Returns false on a damaged frame. `adv` receives bytes consumed.
inline bool frame(const uint8_t* d, size_t n, size_t pos, frame_ctx& fc, uint32_t& rate_out,
                  uint32_t& ch_out, uint32_t& bps_out, uint32_t& bs_out, size_t& adv) noexcept {
    if (pos + 6 > n) return false;
    bitreader br(d + pos, d + n);
    if (br.read(14) != 0x3FFE) return false;
    if (br.read(1) != 0) return false;
    br.read(1);                                          // blocking strategy
    uint32_t bsc = br.read(4), src = br.read(4), chc = br.read(4), ssc = br.read(3);
    if (br.read(1) != 0) return false;
    uint32_t b0 = br.read(8);                            // UTF-8 coded frame/sample number
    int extra = b0 < 0x80 ? 0 : b0 < 0xC0 ? -1 : b0 < 0xE0 ? 1 : b0 < 0xF0 ? 2 : b0 < 0xF8 ? 3 : b0 < 0xFC ? 4 : b0 < 0xFE ? 5 : 6;
    if (extra < 0) return false;
    for (int i = 0; i < extra; ++i) if ((br.read(8) & 0xC0) != 0x80) return false;
    uint32_t bs;
    if (bsc == 0) return false;
    else if (bsc == 1) bs = 192;
    else if (bsc <= 5) bs = 576u << (bsc - 2);
    else if (bsc == 6) bs = br.read(8) + 1;
    else if (bsc == 7) bs = br.read(16) + 1;
    else bs = 256u << (bsc - 8);
    uint32_t sr;
    switch (src) {
        case 0: sr = fc.sr_info; break;   case 1: sr = 88200; break;  case 2: sr = 176400; break;
        case 3: sr = 192000; break;       case 4: sr = 8000; break;   case 5: sr = 16000; break;
        case 6: sr = 22050; break;        case 7: sr = 24000; break;  case 8: sr = 32000; break;
        case 9: sr = 44100; break;        case 10: sr = 48000; break; case 11: sr = 96000; break;
        case 12: sr = br.read(8) * 1000; break;
        case 13: sr = br.read(16); break;
        case 14: sr = br.read(16) * 10; break;
        default: return false;
    }
    uint32_t bps;
    switch (ssc) { case 0: bps = fc.bps_info; break; case 1: bps = 8; break; case 2: bps = 12; break;
                   case 4: bps = 16; break; case 5: bps = 20; break; case 6: bps = 24; break; case 7: bps = 32; break;
                   default: return false; }
    br.align();
    size_t hdr_end = br.bytepos();
    uint32_t crc = br.read(8);
    if (br.err || crc8(d + pos, hdr_end) != crc) return false;
    uint32_t nch = chc < 8 ? chc + 1 : (chc <= 10 ? 2 : 0);
    if (nch == 0 || bps < 4 || bps > 32 || bs > 65535) return false;

    for (uint32_t c = 0; c < nch; ++c) {
        if (!adetail::resize_ok(fc.ch[c], bs)) return false;
        int sbps = (int)bps;
        if ((chc == 8 && c == 1) || (chc == 9 && c == 0) || (chc == 10 && c == 1)) ++sbps;
        if (!subframe(br, bs, sbps, fc.ch[c].data())) return false;
    }
    br.align();
    size_t body_end = br.bytepos();
    uint32_t crc2 = br.read(16);
    if (br.err || crc16(d + pos, body_end) != crc2) return false;

    int64_t* a = fc.ch[0].data();
    int64_t* b = nch > 1 ? fc.ch[1].data() : nullptr;
    if (chc == 8)       for (uint32_t i = 0; i < bs; ++i) b[i] = a[i] - b[i];                      // left/side
    else if (chc == 9)  for (uint32_t i = 0; i < bs; ++i) a[i] = a[i] + b[i];                      // side/right
    else if (chc == 10) for (uint32_t i = 0; i < bs; ++i) {                                         // mid/side
        int64_t mid = (int64_t)((uint64_t)a[i] << 1) | (b[i] & 1);
        int64_t side = b[i];
        a[i] = (mid + side) >> 1; b[i] = (mid - side) >> 1;
    }
    rate_out = sr; ch_out = nch; bps_out = bps; bs_out = bs; adv = body_end + 2;
    return true;
}

inline result<pcm_buffer> decode(const uint8_t* d, size_t n, const decode_options& opt) noexcept {
    using R = result<pcm_buffer>;
    size_t pos = 0;
    if (n >= 10 && std::memcmp(d, "ID3", 3) == 0) {
        uint32_t sz = ((uint32_t)(d[6] & 0x7F) << 21) | ((uint32_t)(d[7] & 0x7F) << 14) | ((uint32_t)(d[8] & 0x7F) << 7) | (d[9] & 0x7F);
        pos = 10 + (size_t)sz + ((d[5] & 0x10) ? 10 : 0);
    }
    if (pos + 4 > n || std::memcmp(d + pos, "fLaC", 4) != 0) return R::fail(status::InvalidArgument);
    pos += 4;
    uint32_t sr = 0, nch = 0, bps = 0; uint64_t total = 0; bool have_si = false, last = false;
    while (!last) {
        if (pos + 4 > n) return R::fail(status::InvalidArgument);
        last = (d[pos] & 0x80) != 0;
        uint32_t type = d[pos] & 0x7F, len = ((uint32_t)d[pos+1] << 16) | ((uint32_t)d[pos+2] << 8) | d[pos+3];
        pos += 4;
        if (pos + len > n) return R::fail(status::InvalidArgument);
        if (type == 0 && len >= 34) {
            bitreader b(d + pos, d + pos + len);
            b.read(16); b.read(16); b.read(24); b.read(24);
            sr = b.read(20); nch = b.read(3) + 1; bps = b.read(5) + 1;
            total = ((uint64_t)b.read(4) << 32) | b.read(32);
            have_si = true;
        }
        pos += len;
    }
    if (!have_si || nch == 0 || nch > 8 || bps < 4 || bps > 32) return R::fail(status::InvalidArgument);

    pcm_buffer out;
    out.rate = sr; out.channels = nch; out.source_bits = bps;
    out.container = container_t::flac; out.codec = codec_t::flac;
    frame_ctx fc; fc.sr_info = sr; fc.bps_info = bps;
    uint32_t cap = 0; uint64_t written = 0;
    if (total && total * nch <= opt.max_samples) { if (!adetail::grow(out.samples, cap, total * nch)) return R::fail(status::OutOfMemory); out.samples.resize(0); }
    bool ok_any = false;
    while (pos + 6 <= n) {
        if (!(d[pos] == 0xFF && (d[pos + 1] & 0xFE) == 0xF8)) break;     // trailing tags / padding
        uint32_t fr, fch, fbps, fbs; size_t adv;
        if (!frame(d, n, pos, fc, fr, fch, fbps, fbs, adv) || fch != nch) {
            if (opt.lenient && ok_any) break;
            return R::fail(status::InvalidArgument);
        }
        if (written + (uint64_t)fbs * nch > opt.max_samples) return R::fail(status::OutOfMemory);
        if (!adetail::grow(out.samples, cap, written + (uint64_t)fbs * nch)) return R::fail(status::OutOfMemory);
        float* dst = out.samples.data() + written;
        const double inv = 1.0 / (double)(1ull << (fbps - 1));
        for (uint32_t c = 0; c < nch; ++c) {
            const int64_t* s = fc.ch[c].data();
            for (uint32_t i = 0; i < fbs; ++i) dst[(size_t)i * nch + c] = (float)(s[i] * inv);
        }
        written += (uint64_t)fbs * nch; pos += adv; ok_any = true;
    }
    if (!ok_any) return R::fail(status::InvalidArgument);
    return R::success(std::move(out));
}

} // namespace flacdec

// ------------------------------------------------------------
// Native FLAC encoder (no external library — works everywhere)
// Produces spec-compliant FLAC: 1 STREAMINFO block + FIXED-predictor
// frames with Rice-coded residuals. Zero MD5 in STREAMINFO (allowed).
// ------------------------------------------------------------
namespace flacenc {

inline uint8_t crc8(const uint8_t* d, size_t n) noexcept {
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    }
    return c;
}
inline uint16_t crc16(const uint8_t* d, size_t n) noexcept {
    uint16_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= (uint16_t)d[i] << 8;
        for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x8005) : (uint16_t)(c << 1);
    }
    return c;
}

struct bit_writer {
    knst_byte_string& out;
    uint32_t acc = 0;
    int cnt = 0;
    explicit bit_writer(knst_byte_string& o) noexcept : out(o) {}
    void write(uint64_t v, int bits) noexcept {
        for (int i = bits - 1; i >= 0; --i) {
            acc = (acc << 1) | (uint32_t)((v >> i) & 1);
            if (++cnt == 8) { out.push_back((uint8_t)acc); acc = 0; cnt = 0; }
        }
    }
    void write_signed(int64_t v, int bits) noexcept { write((uint64_t)v, bits); }
    void unary(uint32_t n) noexcept {
        while (n >= 32) { write(0, 32); n -= 32; }
        if (n) write(0, (int)n);
        write(1, 1);
    }
    void align() noexcept { if (cnt) write(0, 8 - cnt); }
};

inline int best_k(const int64_t* res, size_t n, int max_k) noexcept {
    int64_t best_total = INT64_MAX; int best = 0;
    for (int k = 0; k <= max_k; ++k) {
        int64_t total = (int64_t)n * (1 + k);
        for (size_t i = 0; i < n; ++i) {
            int64_t v = res[i];
            uint64_t f = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
            total += (int64_t)(f >> k);
            if (total > best_total) break;
        }
        if (total < best_total) { best_total = total; best = k; }
    }
    return best;
}

inline void encode_residual(bit_writer& bw, const int64_t* res, size_t n, int k) noexcept {
    bw.write(0, 2);            // method: 4-bit Rice
    bw.write(0, 4);            // partition order 0
    bw.write((uint32_t)k, 4);  // Rice parameter
    for (size_t i = 0; i < n; ++i) {
        int64_t v = res[i];
        uint64_t f = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
        bw.unary((uint32_t)(f >> k));
        if (k > 0) bw.write(f & ((1u << k) - 1), k);
    }
}

inline void encode_subframe(bit_writer& bw, const int64_t* samples, size_t n, int bps) noexcept {
    // CONSTANT fast path
    bool constant = true;
    for (size_t i = 1; i < n; ++i) if (samples[i] != samples[0]) { constant = false; break; }
    if (constant) {
        bw.write(0, 1); bw.write(0, 6); bw.write(0, 1);
        bw.write_signed(samples[0], bps);
        return;
    }
    // Try FIXED predictor orders 0..4, pick smallest
    int max_order = (int)(n >= 5 ? 4 : n - 1);
    if (max_order < 0) max_order = 0;
    int64_t best_bits = INT64_MAX;
    int best_order = 0, best_kv = 0;
    knst_vector<int64_t> best_res;
    for (int order = 0; order <= max_order; ++order) {
        size_t rn = n - (size_t)order;
        knst_vector<int64_t> res;
        if (!adetail::resize_ok(res, (uint32_t)rn)) return;
        for (size_t i = 0; i < rn; ++i) {
            int64_t s = samples[i + order];
            switch (order) {
                case 1: s -= samples[i]; break;
                case 2: s -= 2*samples[i+1] - samples[i]; break;
                case 3: s -= 3*samples[i+2] - 3*samples[i+1] + samples[i]; break;
                case 4: s -= 4*samples[i+3] - 6*samples[i+2] + 4*samples[i+1] - samples[i]; break;
                default: break;
            }
            res[i] = s;
        }
        int k = best_k(res.data(), rn, 14);
        int64_t bits = (int64_t)order * bps + 8 + 2 + 4 + 4;
        bits += (int64_t)rn * (1 + k);
        for (size_t i = 0; i < rn; ++i) {
            int64_t v = res[i];
            uint64_t f = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
            bits += (int64_t)(f >> k);
        }
        if (bits < best_bits) { best_bits = bits; best_order = order; best_kv = k; best_res = std::move(res); }
    }
    // VERBATIM is better for incompressible data
    int64_t verbatim_bits = 8 + (int64_t)n * bps;
    if (verbatim_bits < best_bits) {
        bw.write(0, 1); bw.write(1, 6); bw.write(0, 1);
        for (size_t i = 0; i < n; ++i) bw.write_signed(samples[i], bps);
        return;
    }
    // Write FIXED subframe
    bw.write(0, 1);
    bw.write((uint32_t)(8 + best_order), 6);
    bw.write(0, 1);
    for (int i = 0; i < best_order; ++i) bw.write_signed(samples[i], bps);
    encode_residual(bw, best_res.data(), best_res.size(), best_kv);
}

inline void write_utf8(bit_writer& bw, uint32_t v) noexcept {
    if (v < 0x80) { bw.write(v, 8); return; }
    if (v < 0x800) { bw.write(0xC0 | (v >> 6), 8); bw.write(0x80 | (v & 0x3F), 8); return; }
    if (v < 0x10000) {
        bw.write(0xE0 | (v >> 12), 8);
        bw.write(0x80 | ((v >> 6) & 0x3F), 8);
        bw.write(0x80 | (v & 0x3F), 8);
        return;
    }
    if (v < 0x200000) {
        bw.write(0xF0 | (v >> 18), 8);
        bw.write(0x80 | ((v >> 12) & 0x3F), 8);
        bw.write(0x80 | ((v >> 6) & 0x3F), 8);
        bw.write(0x80 | (v & 0x3F), 8);
        return;
    }
    if (v < 0x4000000) {
        bw.write(0xF8 | (v >> 24), 8);
        bw.write(0x80 | ((v >> 18) & 0x3F), 8);
        bw.write(0x80 | ((v >> 12) & 0x3F), 8);
        bw.write(0x80 | ((v >> 6) & 0x3F), 8);
        bw.write(0x80 | (v & 0x3F), 8);
        return;
    }
    bw.write(0xFC | (v >> 30), 8);
    bw.write(0x80 | ((v >> 24) & 0x3F), 8);
    bw.write(0x80 | ((v >> 18) & 0x3F), 8);
    bw.write(0x80 | ((v >> 12) & 0x3F), 8);
    bw.write(0x80 | ((v >> 6) & 0x3F), 8);
    bw.write(0x80 | (v & 0x3F), 8);
}

inline int rate_code(uint32_t sr) noexcept {
    switch (sr) {
        case 88200: return 1;  case 176400: return 2; case 192000: return 3;
        case 8000:  return 4;  case 16000:  return 5; case 22050:  return 6;
        case 24000: return 7;  case 32000:  return 8; case 44100:  return 9;
        case 48000: return 10; case 96000:  return 11;
        default:    return 0;  // from STREAMINFO
    }
}

inline bool write_frame(knst_byte_string& out, uint32_t frame_num,
                        const int64_t* const* ch_data, uint32_t block_size,
                        uint32_t channels, int bps, uint32_t rate) noexcept {
    size_t start = out.length();
    bit_writer bw(out);
    bw.write(0x3FFE, 14);              // sync
    bw.write(0, 1);                     // reserved
    bw.write(0, 1);                     // blocking = fixed (frame number)
    bw.write(7, 4);                     // block size = 16-bit from end
    bw.write((uint32_t)rate_code(rate), 4);
    bw.write(channels - 1, 4);
    bw.write(0, 3);                     // sample size from STREAMINFO
    bw.write(0, 1);                     // reserved
    write_utf8(bw, frame_num);
    bw.write(block_size - 1, 16);
    bw.align();
    uint8_t hcrc = crc8((const uint8_t*)out.data() + start, out.length() - start);
    bw.write(hcrc, 8);
    for (uint32_t c = 0; c < channels; ++c)
        encode_subframe(bw, ch_data[c], block_size, bps);
    bw.align();
    uint16_t fcrc = crc16((const uint8_t*)out.data() + start, out.length() - start);
    bw.write(fcrc, 16);
    bw.align();
    return true;
}

/// Encode interleaved float samples to a spec-compliant FLAC stream.
/// `bits_per_sample` must be 8, 16 or 24 (FLAC is integer-only).
inline result<knst_byte_string> encode(const float* interleaved, uint32_t frames,
                                        uint32_t rate, uint32_t channels, int bits_per_sample) noexcept {
    using R = result<knst_byte_string>;
    if (!interleaved || frames == 0 || rate == 0 || channels == 0 || channels > 8)
        return R::fail(status::InvalidArgument);
    if (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 24)
        return R::fail(status::NotSupported);

    const uint32_t block_size = 4096;
    int64_t max_v = (1ll << (bits_per_sample - 1)) - 1;
    int64_t min_v = -(1ll << (bits_per_sample - 1));
    double scale = (double)(1ll << (bits_per_sample - 1));

    knst_byte_string out;
    out.push_back('f'); out.push_back('L'); out.push_back('a'); out.push_back('C');
    out.push_back(0x80);  // last=1, type=STREAMINFO
    out.push_back(0); out.push_back(0); out.push_back(34);
    {
        bit_writer sw(out);
        sw.write(block_size, 16);
        sw.write(block_size, 16);
        sw.write(0, 24); sw.write(0, 24);       // min/max frame size (unknown)
        sw.write(rate, 20);
        sw.write(channels - 1, 3);
        sw.write((uint32_t)(bits_per_sample - 1), 5);
        uint64_t total = frames;
        sw.write((uint32_t)(total >> 32), 4);
        sw.write((uint32_t)(total & 0xFFFFFFFFull), 32);
        for (int i = 0; i < 16; ++i) sw.write(0, 8);  // MD5 = zeros (allowed)
        sw.align();
    }

    knst_vector<knst_vector<int64_t>> ch_bufs;
    if (!adetail::resize_ok(ch_bufs, channels)) return R::fail(status::OutOfMemory);
    for (uint32_t c = 0; c < channels; ++c)
        if (!adetail::resize_ok(ch_bufs[c], block_size)) return R::fail(status::OutOfMemory);
    knst_vector<const int64_t*> ptrs;
    if (!adetail::resize_ok(ptrs, channels)) return R::fail(status::OutOfMemory);

    uint32_t remaining = frames, pos = 0, frame_num = 0;
    while (remaining > 0) {
        uint32_t n = remaining < block_size ? remaining : block_size;
        for (uint32_t c = 0; c < channels; ++c) {
            for (uint32_t i = 0; i < n; ++i) {
                double v = (double)interleaved[(size_t)(pos + i) * channels + c] * scale;
                long long q = (long long)(v + (v >= 0 ? 0.5 : -0.5));
                if (q > max_v) q = max_v;
                if (q < min_v) q = min_v;
                ch_bufs[c][i] = q;
            }
            ptrs[c] = ch_bufs[c].data();
        }
        if (!write_frame(out, frame_num, ptrs.data(), n, channels, bits_per_sample, rate))
            return R::fail(status::OutOfMemory);
        remaining -= n; pos += n; ++frame_num;
    }
    return R::success(std::move(out));
}

} // namespace flacenc

#if defined(KNST_NETWORK_HAVE_VORBIS)
namespace vorbis_enc {

/// Encode interleaved float to OGG Vorbis. `quality` in [0.0, 1.0].
/// 0.5 = ~128 kbps stereo @ 44.1 kHz (Vorbis VBR quality 5).
inline result<knst_byte_string> encode(const float* interleaved, uint32_t frames,
                                       uint32_t rate, uint32_t channels,
                                       float quality) noexcept {
    using R = result<knst_byte_string>;
    if (!interleaved || frames == 0 || rate == 0 || channels == 0 || channels > 8)
        return R::fail(status::InvalidArgument);
    if (quality < 0.0f) quality = 0.0f;
    if (quality > 1.0f) quality = 1.0f;

    vorbis_info vi;
    vorbis_info_init(&vi);
    if (vorbis_encode_init_vbr(&vi, (long)channels, (long)rate, quality) != 0) {
        vorbis_info_clear(&vi);
        return R::fail(status::NotSupported);
    }

    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, const_cast<char*>("ENCODER"), const_cast<char*>("KernelNucleusT"));

    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);

    ogg_stream_state os;
    ogg_stream_init(&os, 0x4B4E5354); // serial "KNST"

    knst_byte_string out;
    auto flush_pages = [&](int force) -> void {
        ogg_page og;
        while (force ? ogg_stream_flush(&os, &og) : ogg_stream_pageout(&os, &og)) {
            out.append(og.header, (uint32_t)og.header_len);
            out.append(og.body, (uint32_t)og.body_len);
        }
    };

    {
        ogg_packet hd, hc, hcode;
        if (vorbis_analysis_headerout(&vd, &vc, &hd, &hc, &hcode) != 0) {
            ogg_stream_clear(&os); vorbis_block_clear(&vb); vorbis_dsp_clear(&vd);
            vorbis_comment_clear(&vc); vorbis_info_clear(&vi);
            return R::fail(status::NotSupported);
        }
        ogg_stream_packetin(&os, &hd);
        ogg_stream_packetin(&os, &hc);
        ogg_stream_packetin(&os, &hcode);
        flush_pages(1);
    }

    const uint32_t chunk = 1024;
    uint32_t pos = 0;
    while (pos < frames) {
        uint32_t n = frames - pos < chunk ? frames - pos : chunk;
        float** buffer = vorbis_analysis_buffer(&vd, (int)n);
        for (uint32_t i = 0; i < n; ++i)
            for (uint32_t c = 0; c < channels; ++c)
                buffer[c][i] = interleaved[(size_t)(pos + i) * channels + c];
        vorbis_analysis_wrote(&vd, (int)n);
        pos += n;
        ogg_packet op;
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                flush_pages(0);
            }
        }
    }
    vorbis_analysis_wrote(&vd, 0); // EOF
    {
        ogg_packet op;
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                flush_pages(0);
            }
        }
        flush_pages(1);
    }

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    if (out.empty()) return R::fail(status::OutOfMemory);
    return R::success(std::move(out));
}

} // namespace vorbis_enc
#endif // KNST_NETWORK_HAVE_VORBIS

// ------------------------------------------------------------
// Backend registry + libsndfile (optional)
// ------------------------------------------------------------

struct registry_entry { container_t c = container_t::unknown; decoder_fn fn = nullptr; void* user = nullptr; };
// minimp3 'in yapımcısına teşekkürler :)
// ─────────────────────────────────────────────────────────────────────────────
//  Native MP3 decoder — minimp3 single-header (CC0 public domain)
//
//  Kaynak: https://github.com/lieff/minimp3
//  Lisans: CC0 (public domain, serbestçe değiştirilebilir)
//  Runtime bağımlılık: SIFIR (header exe'ye compile olur, DLL/so YOK) çalışırken bir sıkıntı çıkmaması gerekiyor
//
//  MPEG-1 / MPEG-2 / MPEG-2.5 Layer III <-_->tüm bitrate, VBR, free format.
// ─────────────────────────────────────────────────────────────────────────────
namespace mp3_native {

inline result<pcm_buffer> decode(const uint8_t* d, uint32_t n,
                                  const decode_options& opt) noexcept {
    using R = result<pcm_buffer>;
    if (!d || n == 0) return R::fail(status::InvalidArgument);

    mp3dec_t dec;
    mp3dec_init(&dec);

    mp3dec_frame_info_t info{};
    pcm_buffer out;
    size_t pos = 0;
        while (pos < n) {
        mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int samples = mp3dec_decode_frame(&dec, d + pos, n - pos, pcm, &info);

        if (info.frame_bytes == 0) break;
        pos += info.frame_bytes;

        if (samples > 0) {
            if (out.channels == 0) {
                out.rate        = (uint32_t)info.hz;
                out.channels    = (uint32_t)info.channels;
                out.container   = container_t::mp3;
                out.codec       = codec_t::mp3;
                out.source_bits = 0;
            } else if (out.rate != (uint32_t)info.hz ||
                       out.channels != (uint32_t)info.channels) {
                break;
            }

            size_t prev = out.samples.size();
            size_t add  = (size_t)samples * info.channels;
            if (prev + add > opt.max_samples) return R::fail(status::OutOfMemory);
            out.samples.resize(prev + add);

            for (size_t i = 0; i < add; ++i)
                out.samples[prev + i] = pcm[i] * (1.0f / 32768.0f);
        }
    }

    if (out.samples.empty()) return R::fail(status::InvalidArgument);
    return R::success(std::move(out));
}

} // namespace mp3_native

#if defined(KNST_NETWORK_HAVE_FAAD2)
namespace aac_native {

/// Decode a raw ADTS stream (`.aac` file, or concatenated AAC-in-container).
inline result<pcm_buffer> decode_adts(const uint8_t* d, uint32_t n,
                                      const decode_options& opt) noexcept {
    using R = result<pcm_buffer>;
    if (!d || n < 7) return R::fail(status::InvalidArgument);

    NeAACDecHandle h = NeAACDecOpen();
    if (!h) return R::fail(status::NotSupported);

    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(h);
    if (cfg) {
        cfg->defObjectType  = LC;
        cfg->defSampleRate  = 44100;
        cfg->outputFormat   = FAAD_FMT_FLOAT;
        cfg->downMatrix     = 0;
        cfg->useOldADTSFormat = 0;
        NeAACDecSetConfiguration(h, cfg);
    }

    unsigned long rate = 0;
    unsigned char channels = 0;
    char init = NeAACDecInit(h, const_cast<unsigned char*>(d), (unsigned long)n, &rate, &channels);
    if (init < 0 || rate == 0 || channels == 0) {
        NeAACDecClose(h);
        return R::fail(status::InvalidArgument);
    }

    pcm_buffer out;
    out.rate = (uint32_t)rate; out.channels = (uint32_t)channels;
    out.container = container_t::aac_adts; out.codec = codec_t::aac;

    uint32_t cap = 0; uint64_t written = 0;
    size_t pos = (size_t)init;
    NeAACDecFrameInfo info;

    while (pos + 7 <= n) {
        std::memset(&info, 0, sizeof(info));
        void* samples = NeAACDecDecode(h, &info,
                                        const_cast<unsigned char*>(d + pos),
                                        (unsigned long)(n - pos));
        if (info.error > 0) {
            if (opt.lenient && written > 0) break;
            NeAACDecClose(h);
            return R::fail(status::InvalidArgument);
        }
        if (info.samples == 0 || info.bytesconsumed == 0) break;
        pos += (size_t)info.bytesconsumed;

                if (info.channels != (unsigned char)out.channels) {
            NeAACDecClose(h);
            return R::fail(status::NotSupported);
        }
        // HE-AAC SBR doubles the actual output rate. The ADTS header and
        // NeAACDecInit report the BASE rate, so fix up out.rate the first
        // time we see an SBR frame (otherwise playback is at 1/2 speed).
        if (info.sbr && written == 0) out.rate = (uint32_t)rate * 2;
        uint64_t nsamp = (uint64_t)info.samples * out.channels;
        if (written + nsamp > opt.max_samples) {
            NeAACDecClose(h); return R::fail(status::OutOfMemory);
        }
        if (!adetail::grow(out.samples, cap, written + nsamp)) {
            NeAACDecClose(h); return R::fail(status::OutOfMemory);
        }
        const float* src = static_cast<const float*>(samples);
        for (uint64_t i = 0; i < nsamp; ++i) out.samples[written + i] = src[i];
        written += nsamp;
    }
    NeAACDecClose(h);
    if (written == 0) return R::fail(status::InvalidArgument);
    adetail::resize_ok(out.samples, (uint32_t)written);
    return R::success(std::move(out));
}

/// Per-frame AAC decoder for containers. Stateful across packets.
struct frame_decoder {
    NeAACDecHandle h = nullptr;
    uint32_t rate = 0, channels = 0, out_rate = 0;
    ~frame_decoder() { close(); }
    void close() noexcept { if (h) { NeAACDecClose(h); h = nullptr; } }
    bool init(const uint8_t* asc, size_t asc_len) noexcept {
        close();
        h = NeAACDecOpen();
        if (!h) return false;
        NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(h);
        if (cfg) {
            cfg->outputFormat = FAAD_FMT_FLOAT;
            cfg->downMatrix   = 0;
            NeAACDecSetConfiguration(h, cfg);
        }
        unsigned long r = 0;
        unsigned char c = 0;
        char ret = NeAACDecInit2(h, const_cast<unsigned char*>(asc),
                                  (unsigned long)asc_len, &r, &c);
        if (ret < 0 || r == 0 || c == 0) { close(); return false; }
        rate = (uint32_t)r; channels = (uint32_t)c; out_rate = rate;
        return true;
    }
    // Returns false on end-of-stream; true on success. Fills out_pcm/out_frames/out_ch.
    bool decode_frame(const uint8_t* d, uint32_t n, const float** out_pcm,
                      uint32_t& out_frames, uint32_t& out_ch) noexcept {
        if (!h) return false;
        NeAACDecFrameInfo info;
        std::memset(&info, 0, sizeof(info));
        void* samples = NeAACDecDecode(h, &info,
                                        const_cast<unsigned char*>(d),
                                        (unsigned long)n);
        if (info.error > 0 || info.samples == 0) return false;
        // HE-AAC SBR doubles the output rate
        if (info.sbr) out_rate = rate * 2; else out_rate = rate;
                if (out_pcm) *out_pcm = static_cast<const float*>(samples);
        out_frames = (uint32_t)info.samples;
        out_ch = (uint32_t)info.channels;
        return true;
    }
};

} // namespace aac_native
#endif // KNST_NETWORK_HAVE_FAAD2

class registry {
public:
    enum { kMax = 32 };
    /// Register (or replace) a decoder. Call during start-up, not concurrently with decode().
    static bool add(container_t c, decoder_fn fn, void* user = nullptr) noexcept {
        auto& r = get();
        for (uint32_t i = 0; i < r.n; ++i) if (r.e[i].c == c) { r.e[i].fn = fn; r.e[i].user = user; return true; }
        if (r.n >= kMax) return false;
        r.e[r.n].c = c; r.e[r.n].fn = fn; r.e[r.n].user = user; ++r.n; return true;
    }
    static const registry_entry* find(container_t c) noexcept {
        auto& r = get();
        for (uint32_t i = 0; i < r.n; ++i) if (r.e[i].c == c && r.e[i].fn) return &r.e[i];
        return nullptr;
    }
private:
    registry_entry e[kMax]; uint32_t n = 0;
    static registry& get() noexcept { static registry r; return r; }
};

namespace sndfile {

struct SF_INFO_c { int64_t frames; int samplerate; int channels; int format; int sections; int seekable; };
struct SF_VIRTUAL_IO_c {
    int64_t (*get_filelen)(void*);
    int64_t (*seek)(int64_t, int, void*);
    int64_t (*read)(void*, int64_t, void*);
    int64_t (*write)(const void*, int64_t, void*);
    int64_t (*tell)(void*);
};
struct mem_io { const uint8_t* d; int64_t n, pos; };

struct api {
    void* h = nullptr;
    void* (*open_virtual)(SF_VIRTUAL_IO_c*, int, SF_INFO_c*, void*) = nullptr;
    int64_t (*readf_float)(void*, float*, int64_t) = nullptr;
    // sf_writef_float — used only by sndfile::encode() for FLAC/OGG/AIFF export.
    // Kept out of `ok` so existing decode-only paths stay valid on old libsndfile.
    int64_t (*writef_float)(void*, const float*, int64_t) = nullptr;
    int (*close)(void*) = nullptr;
    bool ok = false;
};

inline api& get() noexcept {
    static api a = []() {
        api r;
#if KNST_USING_PLATFORM_WINDOWS
        const char* names[] = { "sndfile.dll", "libsndfile-1.dll" };
        HMODULE h = nullptr;
        for (const char* nm : names) { h = ::LoadLibraryA(nm); if (h) break; }
        if (!h) return r;
        r.h = (void*)h;
        r.open_virtual = (decltype(r.open_virtual)) ::GetProcAddress(h, "sf_open_virtual");
        r.readf_float  = (decltype(r.readf_float))  ::GetProcAddress(h, "sf_readf_float");
        r.writef_float = (decltype(r.writef_float)) ::GetProcAddress(h, "sf_writef_float");
        r.close        = (decltype(r.close))        ::GetProcAddress(h, "sf_close");
#else
        const char* names[] = { "libsndfile.so.1", "libsndfile.so", "libsndfile.dylib" };
        for (const char* nm : names) { r.h = ::dlopen(nm, RTLD_LAZY); if (r.h) break; }
        if (!r.h) return r;
        r.open_virtual = (decltype(r.open_virtual)) ::dlsym(r.h, "sf_open_virtual");
        r.readf_float  = (decltype(r.readf_float))  ::dlsym(r.h, "sf_readf_float");
        r.writef_float = (decltype(r.writef_float)) ::dlsym(r.h, "sf_writef_float");
        r.close        = (decltype(r.close))        ::dlsym(r.h, "sf_close");
#endif
        r.ok = r.open_virtual && r.readf_float && r.close;
        return r;
    }();
    return a;
}

inline result<pcm_buffer> decode(const uint8_t* d, uint32_t n, const decode_options& opt, container_t c) noexcept {
    using R = result<pcm_buffer>;
    auto& a = get();
    if (!a.ok) return R::fail(status::NotSupported);
    mem_io io{ d, (int64_t)n, 0 };
    SF_VIRTUAL_IO_c vio;
    vio.get_filelen = [](void* u) -> int64_t { return ((mem_io*)u)->n; };
    vio.seek = [](int64_t off, int wh, void* u) -> int64_t {
        auto* m = (mem_io*)u; int64_t p = wh == 0 ? off : wh == 1 ? m->pos + off : m->n + off;
        if (p < 0) p = 0;
        if (p > m->n) p = m->n;
        m->pos = p; return p; };
    vio.read = [](void* dst, int64_t cnt, void* u) -> int64_t {
        auto* m = (mem_io*)u; int64_t k = m->n - m->pos; if (k > cnt) k = cnt; if (k <= 0) return 0;
        std::memcpy(dst, m->d + m->pos, (size_t)k); m->pos += k; return k; };
    vio.write = [](const void*, int64_t, void*) -> int64_t { return 0; };
    vio.tell  = [](void* u) -> int64_t { return ((mem_io*)u)->pos; };
    SF_INFO_c info; std::memset(&info, 0, sizeof(info));
    void* f = a.open_virtual(&vio, 0x10 /*SFM_READ*/, &info, &io);
    if (!f) return R::fail(status::NotSupported);
    if (info.channels <= 0 || info.channels > 64 || info.samplerate <= 0) { a.close(f); return R::fail(status::InvalidArgument); }
    pcm_buffer out; out.rate = (uint32_t)info.samplerate; out.channels = (uint32_t)info.channels; out.container = c;
    out.codec = c == container_t::mp3 ? codec_t::mp3 : c == container_t::ogg ? codec_t::vorbis : codec_t::unknown;
    const uint32_t chunk = 4096; uint32_t cap = 0; uint64_t written = 0;
    for (;;) {
        if (written + (uint64_t)chunk * out.channels > opt.max_samples) { a.close(f); return R::fail(status::OutOfMemory); }
        if (!adetail::grow(out.samples, cap, written + (uint64_t)chunk * out.channels)) { a.close(f); return R::fail(status::OutOfMemory); }
        int64_t got = a.readf_float(f, out.samples.data() + written, chunk);
        if (got <= 0) { written += 0; break; }
        written += (uint64_t)got * out.channels;
    }
    a.close(f);
    adetail::resize_ok(out.samples, (uint32_t)written);
    if (written == 0) return R::fail(status::InvalidArgument);
    return R::success(std::move(out));
}

namespace encode_helpers {
    // Heap-persisted write I/O state for libsndfile virtual IO.
    // libsndfile seeks back to patch headers → we must support SEEK_SET.
    struct write_io {
        knst_byte_string* out;
        uint32_t pos;
    };

    inline int64_t w_getlen(void* u) {
        return (int64_t)((write_io*)u)->out->length();
    }
    inline int64_t w_seek(int64_t off, int wh, void* u) {
        auto* w = (write_io*)u;
        int64_t target;
        if (wh == 0)      target = off;                      // SEEK_SET
        else if (wh == 1) target = (int64_t)w->pos + off;    // SEEK_CUR
        else              target = (int64_t)w->out->length() + off;   // SEEK_END
        if (target < 0) return -1;
        uint32_t nu = (uint32_t)target;
        if (nu > w->out->length()) {
            uint32_t old = w->out->length();
            w->out->resize(nu);
            if (w->out->length() != nu) return -1;
                        if (nu > old) ::memset(const_cast<uint8_t*>(w->out->data()) + old, 0, nu - old);
        }
        w->pos = nu;
        return target;
    }
    inline int64_t w_read(void*, int64_t, void*) { return 0; }  // read unused in SFM_WRITE
    inline int64_t w_write(const void* src, int64_t cnt, void* u) {
        if (cnt <= 0) return 0;
        auto* w = (write_io*)u;
        uint64_t need = (uint64_t)w->pos + (uint64_t)cnt;
        if (need > 0xFFFFFF00ull) return -1;
        uint32_t nu = (uint32_t)need;
        if (nu > w->out->length()) {
            uint32_t old = w->out->length();
            w->out->resize(nu);
            if (w->out->length() != nu) return -1;
                       if (nu > old) ::memset(const_cast<uint8_t*>(w->out->data()) + old, 0, nu - old);
        }
                ::memcpy(const_cast<uint8_t*>(w->out->data()) + w->pos, src, (size_t)cnt);
        w->pos = nu;
        return cnt;
    }
    inline int64_t w_tell(void* u) { return (int64_t)((write_io*)u)->pos; }
} // namespace encode_helpers

// Encode interleaved float [-1,1] to libsndfile format `sf_format`.
// Examples: FLAC 16-bit = 0x170000|0x0002, OGG-Vorbis = 0x200000|0x0060,
//           AIFF 16-bit = 0x020000|0x0002, WAV 24-bit = 0x010000|0x0003.
// Returns the encoded file bytes. On failure: NotSupported (format not compiled
// into libsndfile) or IoError.
inline result<knst_byte_string> encode(const float* interleaved, uint32_t frames,
                                       uint32_t rate, uint32_t channels, int sf_format) noexcept {
    using R = result<knst_byte_string>;
    auto& a = get();
    if (!a.ok || !a.writef_float) return R::fail(status::NotSupported);
    if (!interleaved || frames == 0 || rate == 0 || channels == 0 || channels > 64)
        return R::fail(status::InvalidArgument);

    knst_byte_string out;
    encode_helpers::write_io io{ &out, 0 };
    SF_VIRTUAL_IO_c vio;
    vio.get_filelen = encode_helpers::w_getlen;
    vio.seek        = encode_helpers::w_seek;
    vio.read        = encode_helpers::w_read;
    vio.write       = encode_helpers::w_write;
    vio.tell        = encode_helpers::w_tell;

    SF_INFO_c info;
    std::memset(&info, 0, sizeof(info));
    info.samplerate = (int)rate;
    info.channels   = (int)channels;
    info.format     = sf_format;

    // SFM_WRITE = 0x20. libsndfile validates `format` at open time; unsupported
    // combinations (e.g. FLAC with a float subformat) return nullptr here.
    void* f = a.open_virtual(&vio, 0x20 /*SFM_WRITE*/, &info, &io);
    if (!f) return R::fail(status::NotSupported);

    int64_t written = a.writef_float(f, interleaved, (int64_t)frames);
    int cr = a.close(f);
    if (written != (int64_t)frames || cr != 0) return R::fail(status::IoError);
    return R::success(std::move(out));
}

} // namespace sndfile

// ------------------------------------------------------------
// Light header sniffing for backend formats (MP3 / OGG)
// ------------------------------------------------------------

inline media_info sniff_lossy(const uint8_t* d, size_t n, container_t c) noexcept {
    using namespace adetail;
    media_info m; m.container = c; m.valid = false;
    if (c == container_t::mp3) {
        size_t off = 0;
        if (n >= 10 && std::memcmp(d, "ID3", 3) == 0)
            off = 10 + (((size_t)(d[6] & 0x7F) << 21) | ((size_t)(d[7] & 0x7F) << 14) | ((size_t)(d[8] & 0x7F) << 7) | (d[9] & 0x7F));
        for (size_t i = off; i + 4 <= n && i < off + 65536; ++i) {
            if (d[i] != 0xFF || (d[i + 1] & 0xE0) != 0xE0) continue;
            int ver = (d[i + 1] >> 3) & 3, layer = (d[i + 1] >> 1) & 3, sri = (d[i + 2] >> 2) & 3;
            if (ver == 1 || layer == 0 || sri == 3 || (d[i + 2] >> 4) == 15) continue;
            static const uint32_t base[3] = { 44100, 48000, 32000 };
            uint32_t r = base[sri]; if (ver == 2) r /= 2; else if (ver == 0) r /= 4;
            m.rate = r; m.channels = ((d[i + 3] >> 6) == 3) ? 1 : 2; m.codec = codec_t::mp3; m.valid = true; break;
        }
    } else if (c == container_t::ogg && n >= 64) {
        uint32_t segs = d[26]; size_t p = 27 + segs;
        if (p + 30 <= n && d[p] == 1 && std::memcmp(d + p + 1, "vorbis", 6) == 0) {
            m.channels = d[p + 11]; m.rate = le32(d + p + 12); m.codec = codec_t::vorbis; m.valid = m.channels && m.rate;
        } else if (p + 19 <= n && std::memcmp(d + p, "OpusHead", 8) == 0) {
            m.channels = d[p + 9]; m.rate = 48000; m.codec = codec_t::opus; m.valid = m.channels != 0;
        }
    }
    return m;
}

// ------------------------------------------------------------
// probe / decode
// ------------------------------------------------------------

inline result<media_info> probe(const uint8_t* d, uint32_t n) noexcept {
    using R = result<media_info>;
    if (!d || n < 4) return R::fail(status::InvalidArgument);
    container_t c = detect(d, n);
    media_info m; m.container = c; m.native = is_native(c) && c != container_t::raw;
    layout L;
    if (c == container_t::wav || c == container_t::rf64) L = parse_wav(d, n);
    else if (c == container_t::aiff || c == container_t::aifc) L = parse_aiff(d, n);
    else if (c == container_t::au) L = parse_au(d, n);
    else if (c == container_t::flac) {
        size_t pos = 0;
        if (n >= 10 && std::memcmp(d, "ID3", 3) == 0) {
            pos = 10 + (((size_t)(d[6] & 0x7F) << 21) | ((size_t)(d[7] & 0x7F) << 14) | ((size_t)(d[8] & 0x7F) << 7) | (d[9] & 0x7F));
        }
        if (pos + 4 + 4 + 34 > n) return R::fail(status::InvalidArgument);
        const uint8_t* s = d + pos + 8;                    // STREAMINFO body
        uint64_t v = adetail::be64(s + 10);                // rate(20) ch(3) bps(5) total(36)
        m.rate = (uint32_t)(v >> 44); m.channels = (uint32_t)((v >> 41) & 7) + 1; m.bits = (uint32_t)((v >> 36) & 31) + 1;
        m.frames = v & 0xFFFFFFFFFull; m.codec = codec_t::flac; m.valid = m.rate != 0;
        m.duration = m.rate ? (double)m.frames / m.rate : 0.0;
        return R::success(std::move(m));
    } else if (c == container_t::mp3 || c == container_t::ogg) {
        media_info s = sniff_lossy(d, n, c);
        if (!s.valid) s.container = c;
        s.native = false; return R::success(std::move(s));
    } else if (c == container_t::unknown) return R::fail(status::NotSupported);
    else return R::success(std::move(m));                  // known container, no header parse
    if (!L.valid) return R::fail(status::InvalidArgument);
    m.codec = L.codec; m.rate = L.rate; m.channels = L.channels; m.bits = L.bits;
    m.frames = layout_frames(L); m.duration = (double)m.frames / L.rate; m.valid = true;
    return R::success(std::move(m));
}

inline result<pcm_buffer> decode_layout(const uint8_t* d, size_t n, const layout& L, const decode_options& opt) noexcept {
    using R = result<pcm_buffer>;
    if (!L.valid || L.data_off > n) return R::fail(status::InvalidArgument);
    uint64_t avail = n - L.data_off; if (L.data_size > avail) { /* clamp */ }
    uint64_t dsz = L.data_size < avail ? L.data_size : avail;
    const uint8_t* src = d + L.data_off;
    pcm_buffer out;
    out.rate = L.rate; out.channels = L.channels; out.source_bits = L.bits;
    out.container = L.container; out.codec = L.codec;
    uint64_t frames = layout_frames(L);
    if (frames * L.channels > opt.max_samples) return R::fail(status::OutOfMemory);

    if (L.codec == codec_t::ima_adpcm || L.codec == codec_t::ms_adpcm) {
        uint32_t spb = L.samples_per_block;
        if (spb == 0) spb = (L.codec == codec_t::ima_adpcm)
            ? (L.block_align - 4u * L.channels) * 8 / (4u * L.channels) + 1 : (L.block_align - 7u * L.channels) * 2 / L.channels + 2;
        uint32_t cap = 0; uint64_t written = 0;
        if (!adetail::grow(out.samples, cap, frames * L.channels)) return R::fail(status::OutOfMemory);
        for (uint64_t off = 0; off < dsz; off += L.block_align) {
            size_t len = (size_t)((dsz - off) < L.block_align ? (dsz - off) : L.block_align);
            uint64_t room = (uint64_t)spb * L.channels;       // each block may write up to spb frames
            if (written + room > out.samples.size()) {
                if (!adetail::grow(out.samples, cap, written + room)) return R::fail(status::OutOfMemory);
            }
            uint32_t f = (L.codec == codec_t::ima_adpcm)
                ? adpcm::ima_block(src + off, len, L.channels, spb, out.samples.data() + written)
                : adpcm::ms_block(src + off, len, L.channels, spb, L.coefs, L.num_coefs, out.samples.data() + written);
            if (f == 0) { if (len < L.block_align) break; return R::fail(status::InvalidArgument); }
            written += (uint64_t)f * L.channels;
        }
        if (written > opt.max_samples) return R::fail(status::OutOfMemory);
        adetail::resize_ok(out.samples, (uint32_t)written);
        return R::success(std::move(out));
    }
    uint64_t total = frames * L.channels;
    if (!adetail::resize_ok(out.samples, (uint32_t)total)) return R::fail(status::OutOfMemory);
    adetail::decode_samples(src, (size_t)total, L.fmt, L.big_endian, out.samples.data());
    return R::success(std::move(out));
}

inline result<pcm_buffer> decode_raw(const uint8_t* d, size_t n, const decode_options& o) noexcept {
    layout L; L.valid = true; L.container = container_t::raw; L.codec = codec_t::pcm;
    L.fmt = o.raw_format; L.big_endian = o.raw_big_endian; L.rate = o.raw_rate; L.channels = o.raw_channels;
    L.bits = sample_bytes(o.raw_format) * 8; L.data_off = 0; L.data_size = n;
    if (L.rate == 0 || L.channels == 0 || L.channels > 256) return result<pcm_buffer>::fail(status::InvalidArgument);
    if (o.raw_format == sample_fmt::f32 || o.raw_format == sample_fmt::f64) L.codec = codec_t::float_pcm;
    return decode_layout(d, n, L, o);
}

} // namespace codec

// Namespace aliases — decoder_stream and other code outside `codec`
// reference adpcm::/flacdec::/aac_native:: directly. Those namespaces
// live inside codec, so expose them at knst_aud scope for backward
// compatibility (and for the free-function decode() path).
namespace adpcm      = codec::adpcm;
namespace flacdec    = codec::flacdec;
#if defined(KNST_NETWORK_HAVE_FAAD2)
namespace aac_native = codec::aac_native;
#endif

/// Decode any supported file image to interleaved float.
inline result<pcm_buffer> decode(const uint8_t* d, uint32_t n,
                                 const decode_options& opt = decode_options(),
                                 container_t hint = container_t::unknown) noexcept {
    using R = result<pcm_buffer>;
    if (!d || n == 0) return R::fail(status::InvalidArgument);
    container_t c = codec::detect(d, n);
    if (c == container_t::unknown) c = hint;
    if (c == container_t::raw) return codec::decode_raw(d, n, opt);

    codec::layout L;
    switch (c) {
        case container_t::wav: case container_t::rf64: L = codec::parse_wav(d, n);  break;
        case container_t::aiff: case container_t::aifc: L = codec::parse_aiff(d, n); break;
        case container_t::au:   L = codec::parse_au(d, n); break;
        case container_t::flac: return codec::flacdec::decode(d, n, opt);
        default: break;
    }
    if (codec::is_native(c)) {
        if (!L.valid) return R::fail(status::InvalidArgument);
        return codec::decode_layout(d, n, L, opt);
    }
    if (c == container_t::unknown) return R::fail(status::NotSupported);
    if (c == container_t::midi || c == container_t::dsf || c == container_t::dsdiff) return R::fail(status::NotSupported);
    if (opt.use_backend) {
        // Custom registry takes priority
        if (auto* e = codec::registry::find(c)) {
            auto r = e->fn(d, n, opt, e->user);
            if (r) { if (r.value.container == container_t::unknown) r.value.container = c; return r; }
        }
        // MP3: native decoder öncelikli
        if (c == container_t::mp3) {
            auto r = codec::mp3_native::decode(d, n, opt);
            if (r) return r;
        }
#if defined(KNST_NETWORK_HAVE_FAAD2)
        // AAC: native decoder (FAAD2)
        if (c == container_t::aac_adts) {
            auto r = codec::aac_native::decode_adts(d, n, opt);
            if (r) return r;
        }
#endif
        // Generic backend (libsndfile — handles OGG/OPUS/WavPack/APE/TTA)
        return codec::sndfile::decode(d, n, opt, c);
    }
    return R::fail(status::NotSupported);
}

inline result<media_info> probe(const uint8_t* d, uint32_t n) noexcept { return codec::probe(d, n); }

inline result<media_info> probe(const knst_c16string& path) noexcept {
    using R = result<media_info>;
    knst_file_error fe = knst_file_error::None;
    knst_byte_string data = knst_file::read_file_data<>(path, &fe);
    if (fe != knst_file_error::None || data.empty()) return R::fail(status::NotFound);
    return codec::probe(data.data(), data.length());
}

inline result<pcm_buffer> decode_file(const knst_c16string& path,
                                      const decode_options& opt = decode_options()) noexcept {
    using R = result<pcm_buffer>;
    knst_file_error fe = knst_file_error::None;
    knst_byte_string data = knst_file::read_file_data<>(path, &fe);
    if (fe != knst_file_error::None || data.empty()) return R::fail(status::NotFound);
    return decode(data.data(), data.length(), opt, codec::detect_extension(path));
}

// ============================================================
// Encoders
// ============================================================

struct encode_options {
    container_t container = container_t::unknown;   // unknown → WAV (or deduced from path in encode_file)
    sample_fmt  fmt       = sample_fmt::i16;
    bool        dither    = false;                  // TPDF, applied to integer formats <= 16 bit and G.711
    uint64_t    dither_seed = 0x1234ABCD5678EF01ull;
    bool        big_endian  = false;                // RAW only
};

namespace codec {

struct wav_hdr { uint8_t b[96]; uint32_t len = 0; };

inline uint32_t channel_mask_for(uint32_t ch) noexcept {
    switch (ch) { case 1: return 0x4; case 2: return 0x3; case 3: return 0x7; case 4: return 0x33;
                  case 5: return 0x37; case 6: return 0x3F; case 7: return 0x13F; case 8: return 0x63F; }
    return 0;
}

/// Builds a RIFF/WAVE header (PCM, float, G.711; WAVE_FORMAT_EXTENSIBLE for >2 ch).
inline bool make_wav_header(wav_hdr& h, uint32_t rate, uint32_t ch, sample_fmt f,
                            uint64_t data_bytes, uint64_t frames) noexcept {
    using namespace adetail;
    if (ch == 0 || ch > 65535 || rate == 0 || f == sample_fmt::i8) return false;
    bool fl = (f == sample_fmt::f32 || f == sample_fmt::f64);
    uint32_t tag = fl ? 3 : f == sample_fmt::alaw ? 6 : f == sample_fmt::ulaw ? 7 : 1;
    uint32_t bps = sample_bytes(f);
    bool ext = ch > 2;
    uint32_t fmt_size = ext ? 40 : (tag == 1 ? 16 : 18);
    uint8_t* b = h.b; uint32_t p = 0;
    std::memcpy(b + p, "RIFF", 4); p += 8;
    std::memcpy(b + p, "WAVE", 4); p += 4;
    std::memcpy(b + p, "fmt ", 4); put_le32(b + p + 4, fmt_size); p += 8;
    put_le16(b + p, ext ? 0xFFFE : tag); put_le16(b + p + 2, ch); put_le32(b + p + 4, rate);
    put_le32(b + p + 8, (uint32_t)((uint64_t)rate * ch * bps)); put_le16(b + p + 12, ch * bps);
    put_le16(b + p + 14, bps * 8); p += 16;
    if (fmt_size >= 18) { put_le16(b + p, ext ? 22 : 0); p += 2; }
    if (ext) {
        put_le16(b + p, bps * 8); put_le32(b + p + 2, channel_mask_for(ch)); p += 6;
        static const uint8_t tail[14] = { 0,0,0,0,0x10,0,0x80,0,0,0xAA,0,0x38,0x9B,0x71 };
        put_le16(b + p, tag); std::memcpy(b + p + 2, tail, 14); p += 16;
    }
    if (tag != 1) { std::memcpy(b + p, "fact", 4); put_le32(b + p + 4, 4); put_le32(b + p + 8, (uint32_t)frames); p += 12; }
    std::memcpy(b + p, "data", 4); put_le32(b + p + 4, (uint32_t)data_bytes); p += 8;
    uint64_t riff = (uint64_t)(p - 8) + data_bytes + (data_bytes & 1);
    if (riff > 0xFFFFFFFFull || data_bytes > 0xFFFFFFFFull) return false;
    put_le32(b + 4, (uint32_t)riff);
    h.len = p;
    return true;
}

inline void put_pstring(uint8_t* p, const char* s, uint32_t& pos) noexcept {
    uint32_t n = (uint32_t)std::strlen(s);
    p[pos++] = (uint8_t)n; std::memcpy(p + pos, s, n); pos += n;
    if ((n + 1) & 1) p[pos++] = 0;
}

inline result<knst_byte_string> encode_impl(const float* in, uint32_t frames, uint32_t rate, uint32_t ch,
                                            const encode_options& o) noexcept {
    using namespace adetail;
    using R = result<knst_byte_string>;
    if (!in || frames == 0 || ch == 0 || rate == 0) return R::fail(status::InvalidArgument);
    container_t c = o.container == container_t::unknown ? container_t::wav : o.container;
    sample_fmt f = o.fmt;
    // FLAC: native encoder (no external dependency — works everywhere).
    if (c == container_t::flac) {
        int bps;
        switch (f) {
            case sample_fmt::i16: bps = 16; break;
            case sample_fmt::i24: bps = 24; break;
            case sample_fmt::u8:  bps = 8;  break;
            default: return R::fail(status::NotSupported);  // FLAC is integer-only
        }
        return flacenc::encode(in, frames, rate, ch, bps);
    }
#if defined(KNST_NETWORK_HAVE_VORBIS)
    // OGG Vorbis: native encoder (build-time dep, Linux/Win/Android).
    if (c == container_t::ogg) {
        return vorbis_enc::encode(in, frames, rate, ch, 0.5f);
    }
#endif

    bool be = false; uint32_t hdr_len = 0, trail = 0;
    uint8_t hdr[160]; std::memset(hdr, 0, sizeof(hdr));
    uint64_t samples = (uint64_t)frames * ch;
    uint64_t data = samples * sample_bytes(f);

    if (c == container_t::wav) {
        wav_hdr h; if (!make_wav_header(h, rate, ch, f, data, frames)) return R::fail(status::InvalidArgument);
        std::memcpy(hdr, h.b, h.len); hdr_len = h.len; trail = (uint32_t)(data & 1);
    } else if (c == container_t::aiff || c == container_t::aifc) {
        be = true;
        if (f == sample_fmt::u8) f = sample_fmt::i8;
        data = samples * sample_bytes(f);
        bool aifc = (f == sample_fmt::f32 || f == sample_fmt::f64 || f == sample_fmt::alaw || f == sample_fmt::ulaw);
        uint32_t bits = (f == sample_fmt::alaw || f == sample_fmt::ulaw) ? 16 : sample_bytes(f) * 8;
        const char* comp = "NONE"; const char* name = "not compressed";
        if (f == sample_fmt::f32) { comp = "fl32"; name = "32-bit floating point"; }
        else if (f == sample_fmt::f64) { comp = "fl64"; name = "64-bit floating point"; }
        else if (f == sample_fmt::alaw) { comp = "alaw"; name = "alaw 2:1"; }
        else if (f == sample_fmt::ulaw) { comp = "ulaw"; name = "ulaw 2:1"; }
        uint32_t p = 12;
        std::memcpy(hdr, "FORM", 4); std::memcpy(hdr + 8, aifc ? "AIFC" : "AIFF", 4);
        if (aifc) { std::memcpy(hdr + p, "FVER", 4); put_be32(hdr + p + 4, 4); put_be32(hdr + p + 8, 0xA2805140u); p += 12; }
        uint32_t comm_at = p; p += 8;
        put_be16(hdr + p, ch); put_be32(hdr + p + 2, frames); put_be16(hdr + p + 6, bits); write_f80(hdr + p + 8, (double)rate); p += 18;
        if (aifc) { std::memcpy(hdr + p, comp, 4); p += 4; put_pstring(hdr, name, p); }
        std::memcpy(hdr + comm_at, "COMM", 4); put_be32(hdr + comm_at + 4, p - comm_at - 8);
        uint32_t ssnd_at = p;
        std::memcpy(hdr + p, "SSND", 4); put_be32(hdr + p + 4, (uint32_t)(8 + data)); p += 16;   // + offset + blocksize
        hdr_len = p; trail = (uint32_t)(data & 1);
        uint64_t form = (uint64_t)(hdr_len - 8) + data + trail;
        if (form > 0xFFFFFFFFull || data + 8 > 0xFFFFFFFFull) return R::fail(status::InvalidArgument);
        put_be32(hdr + 4, (uint32_t)form); (void)ssnd_at;
    } else if (c == container_t::au) {
        be = true;
        if (f == sample_fmt::u8) f = sample_fmt::i8;
        data = samples * sample_bytes(f);
        uint32_t enc = f == sample_fmt::ulaw ? 1 : f == sample_fmt::i8 ? 2 : f == sample_fmt::i16 ? 3 : f == sample_fmt::i24 ? 4
                     : f == sample_fmt::i32 ? 5 : f == sample_fmt::f32 ? 6 : f == sample_fmt::f64 ? 7 : 27;
        if (data > 0xFFFFFFFEull) return R::fail(status::InvalidArgument);
        std::memcpy(hdr, ".snd", 4); put_be32(hdr + 4, 24); put_be32(hdr + 8, (uint32_t)data);
        put_be32(hdr + 12, enc); put_be32(hdr + 16, rate); put_be32(hdr + 20, ch); hdr_len = 24;
    } else if (c == container_t::raw) {
        be = o.big_endian; hdr_len = 0;
    } else return R::fail(status::NotSupported);

    uint64_t total = (uint64_t)hdr_len + data + trail;
    if (total > 0xFFFFFFFFull) return R::fail(status::InvalidArgument);
    knst_byte_string out;
    out.resize((uint32_t)total);
    if (out.length() != (uint32_t)total) return R::fail(status::OutOfMemory);
    uint8_t* dst = &out[0];
    if (hdr_len) std::memcpy(dst, hdr, hdr_len);
    rng rg(o.dither_seed);
    encode_samples(in, (size_t)samples, f, be, dst + hdr_len, o.dither ? &rg : nullptr);
    if (trail) dst[hdr_len + data] = 0;
    return R::success(std::move(out));
}

} // namespace codec

inline result<knst_byte_string> encode(const float* interleaved, uint32_t frames, uint32_t rate,
                                       uint32_t channels, const encode_options& o = encode_options()) noexcept {
    return codec::encode_impl(interleaved, frames, rate, channels, o);
}

inline result<knst_byte_string> encode(const pcm_buffer& b, const encode_options& o = encode_options()) noexcept {
    return codec::encode_impl(b.samples.data(), b.frames(), b.rate, b.channels, o);
}

/// Writes a whole buffer. Container is taken from `o.container`, or deduced from the extension.
inline result<void> encode_file(const knst_c16string& path, const pcm_buffer& b,
                                encode_options o = encode_options()) noexcept {
    using R = result<void>;
    if (o.container == container_t::unknown) {
        container_t c = codec::detect_extension(path);
        o.container = (c == container_t::aiff || c == container_t::aifc ||
                       c == container_t::au   || c == container_t::raw  ||
                       c == container_t::flac || c == container_t::ogg)
                      ? c : container_t::wav;
    }
    auto bytes = encode(b, o);
    if (!bytes) return R::fail(bytes.st, bytes.native);
    if (!knst_file::ensure_parent_dir(path)) return R::fail(status::IoError);
    if (!knst_file::write_file_atomic(path, bytes.value.data(), bytes.value.length())) return R::fail(status::IoError);
    return R::success();  
}

/// Streaming WAV recorder: header is written on open, sizes are patched on close().
class wav_writer {
public:
    wav_writer() noexcept = default;
    wav_writer(const wav_writer&) = delete;
    wav_writer& operator=(const wav_writer&) = delete;
    wav_writer(wav_writer&& o) noexcept { *this = std::move(o); }
    wav_writer& operator=(wav_writer&& o) noexcept {
        if (this != &o) {
            close();
            f_ = o.f_; rate_ = o.rate_; ch_ = o.ch_; fmt_ = o.fmt_; frames_ = o.frames_; data_ = o.data_;
            dither_ = o.dither_; rng_ = o.rng_; buf_ = std::move(o.buf_); o.f_ = nullptr;
        }
        return *this;
    }
    ~wav_writer() { close(); }

    result<void> open(const knst_c16string& path, uint32_t rate, uint32_t channels,
                      sample_fmt fmt = sample_fmt::i16, bool dither = false) noexcept {
        using R = result<void>;
        close();
        codec::wav_hdr h;
        if (!codec::make_wav_header(h, rate, channels, fmt, 0, 0)) return R::fail(status::InvalidArgument);
        if (!knst_file::ensure_parent_dir(path)) return R::fail(status::IoError);
#if KNST_USING_PLATFORM_WINDOWS
        f_ = ::_wfopen(reinterpret_cast<const wchar_t*>(path.data()), L"wb");
#else
        knst_byte_string u8(path);
        f_ = ::fopen(reinterpret_cast<const char*>(u8.data()), "wb");
#endif
        if (!f_) return R::fail(status::IoError);
        if (std::fwrite(h.b, 1, h.len, f_) != h.len) { std::fclose(f_); f_ = nullptr; return R::fail(status::IoError); }
        rate_ = rate; ch_ = channels; fmt_ = fmt; frames_ = 0; data_ = 0; dither_ = dither; rng_ = adetail::rng(0x51ED2701ull);
        return R::success();
    }

    result<uint32_t> write(const float* interleaved, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!f_) return R::fail(status::BadState);
        if (!interleaved || frames == 0) return R::fail(status::InvalidArgument);
        const uint32_t bps = sample_bytes(fmt_);
        const uint32_t step = 16384;
        uint32_t done = 0;
        while (done < frames) {
            uint32_t n = frames - done < step ? frames - done : step;
            uint64_t bytes = (uint64_t)n * ch_ * bps;
            if (data_ + bytes > 0xFFFFFFFFull - 128) return R::fail(status::IoError);          // 4 GiB WAV limit
            if (!adetail::resize_ok(buf_, (uint32_t)bytes)) return R::fail(status::OutOfMemory);
            adetail::encode_samples(interleaved + (size_t)done * ch_, (size_t)n * ch_, fmt_, false, &buf_[0], dither_ ? &rng_ : nullptr);
            if (std::fwrite(&buf_[0], 1, (size_t)bytes, f_) != (size_t)bytes) return R::fail(status::IoError);
            data_ += bytes; frames_ += n; done += n;
        }
        return R::success(frames);
    }

    result<void> close() noexcept {
        using R = result<void>;
        if (!f_) return R::success();
        bool ok = true;
        if (data_ & 1) { uint8_t z = 0; ok = std::fwrite(&z, 1, 1, f_) == 1; }
        codec::wav_hdr h;
        if (ok && codec::make_wav_header(h, rate_, ch_, fmt_, data_, frames_)) {
            ok = std::fseek(f_, 0, SEEK_SET) == 0 && std::fwrite(h.b, 1, h.len, f_) == h.len;
        } else ok = false;
        if (std::fclose(f_) != 0) ok = false;
        f_ = nullptr;
        return ok ? R::success() : R::fail(status::IoError);
    }

    bool     is_open() const noexcept { return f_ != nullptr; }
    uint64_t frames_written() const noexcept { return frames_; }
    double   seconds_written() const noexcept { return rate_ ? (double)frames_ / rate_ : 0.0; }

private:
    std::FILE* f_ = nullptr;
    uint32_t rate_ = 0, ch_ = 0; sample_fmt fmt_ = sample_fmt::i16;
    uint64_t frames_ = 0, data_ = 0; bool dither_ = false; adetail::rng rng_;
    knst_byte_string buf_;
};

// ------------------------------------------------------------
// Legacy `wav` API (source compatible with the previous header)
// ------------------------------------------------------------

namespace wav {

struct format {
    using sample_t = sample_fmt;
    uint32_t rate     = 48000;
    uint32_t channels = 2;
    sample_t sample   = sample_fmt::i16;
};

struct info {
    format fmt;
    uint32_t data_bytes  = 0;
    uint32_t frame_count = 0;
    uint16_t format_tag  = 1;
    bool valid = false;
};

inline result<info> probe_memory(const uint8_t* data, uint32_t size) noexcept {
    using R = result<info>;
    if (!data) return R::fail(status::InvalidArgument);
    codec::layout L = codec::parse_wav(data, size);
    if (!L.valid) return R::fail(status::InvalidArgument);
    info out;
    out.fmt.rate = L.rate; out.fmt.channels = L.channels;
    out.fmt.sample = (L.codec == codec_t::ima_adpcm || L.codec == codec_t::ms_adpcm) ? sample_fmt::i16 : L.fmt;
    out.data_bytes = (uint32_t)L.data_size; out.frame_count = (uint32_t)codec::layout_frames(L);
    out.format_tag = L.codec == codec_t::float_pcm ? 3 : L.codec == codec_t::alaw ? 6 : L.codec == codec_t::ulaw ? 7
                   : L.codec == codec_t::ima_adpcm ? 0x11 : L.codec == codec_t::ms_adpcm ? 2 : 1;
    out.valid = true;
    return R::success(std::move(out));
}

inline result<info> probe(const knst_c16string& path) noexcept {
    using R = result<info>;
    knst_file_error fe = knst_file_error::None;
    knst_byte_string data = knst_file::read_file_data<>(path, &fe);
    if (fe != knst_file_error::None || data.empty()) return R::fail(status::NotFound);
    return probe_memory(data.data(), data.length());
}

/// Decodes any supported format (not only WAV) to interleaved float.
inline result<knst_vector<float>> decode_to_float(const uint8_t* data, uint32_t size) noexcept {
    using R = result<knst_vector<float>>;
    auto r = knst_aud::decode(data, size);
    if (!r) return R::fail(r.st, r.native);
    return R::success(std::move(r.value.samples));
}

inline result<knst_vector<float>> decode_to_float(const knst_c16string& path) noexcept {
    using R = result<knst_vector<float>>;
    auto r = knst_aud::decode_file(path);
    if (!r) return R::fail(r.st, r.native);
    return R::success(std::move(r.value.samples));
}

inline result<knst_byte_string> encode_memory(const float* interleaved, uint32_t frames, const format& f) noexcept {
    encode_options o; o.container = container_t::wav; o.fmt = f.sample;
    return knst_aud::encode(interleaved, frames, f.rate, f.channels, o);
}

inline result<void> encode(const knst_c16string& path, const float* interleaved, uint32_t frames, const format& f) noexcept {
    using R = result<void>;
    auto bytes = encode_memory(interleaved, frames, f);
    if (!bytes) return R::fail(bytes.st, bytes.native);
    if (!knst_file::ensure_parent_dir(path)) return R::fail(status::IoError);
    if (!knst_file::write_file_data(path, bytes.value.data(), bytes.value.length())) return R::fail(status::IoError);
    return R::success();
}

} // namespace wav

/// Legacy flat WAV header parse (kept for source compatibility).
inline wav_info parse_wav_header(const unsigned char* data, uint32_t size) noexcept {
    wav_info w;
    codec::layout L = codec::parse_wav(data, size);
    if (!L.valid) return w;
    w.rate = L.rate; w.channels = L.channels; w.bits_per_sample = L.bits;
    w.data_offset = (uint32_t)L.data_off; w.data_size = (uint32_t)L.data_size;
    w.format_tag = L.codec == codec_t::float_pcm ? 3 : L.codec == codec_t::alaw ? 6 : L.codec == codec_t::ulaw ? 7
                 : L.codec == codec_t::ima_adpcm ? 0x11 : L.codec == codec_t::ms_adpcm ? 2 : 1;
    w.valid = true;
    return w;
}

// ============================================================
// DSP module
// ============================================================

namespace dsp {

struct stats {
    float    peak      = 0.0f;
    float    rms       = 0.0f;
    float    peak_db   = -144.0f;
    float    rms_db    = -144.0f;
    uint32_t clipped   = 0;
    uint32_t samples   = 0;
    bool     valid     = false;
};

inline float to_db(float linear) noexcept { return linear > 1e-7f ? 20.0f * std::log10(linear) : -144.0f; }
inline float from_db(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

inline stats analyze(const float* in, uint32_t n) noexcept {
    stats s;
    if (!in || n == 0) return s;
    double sum = 0; float pk = 0; uint32_t cl = 0;
    for (uint32_t i = 0; i < n; ++i) {
        float a = std::fabs(in[i]);
        if (a > pk) pk = a;
        if (a >= 1.0f) ++cl;
        sum += (double)in[i] * in[i];
    }
    s.peak = pk; s.rms = (float)std::sqrt(sum / n); s.clipped = cl; s.samples = n;
    s.peak_db = to_db(pk); s.rms_db = to_db(s.rms); s.valid = true;
    return s;
}

inline level_info measure(const float* in, uint32_t n) noexcept {
    level_info l; stats s = analyze(in, n);
    if (!s.valid) return l;
    l.peak = s.peak; l.rms = s.rms; l.samples = n; l.clipped = s.clipped; l.peak_db = s.peak_db; l.rms_db = s.rms_db;
    return l;
}

inline float dc_offset(const float* buf, uint32_t n) noexcept {
    if (!buf || n == 0) return 0.0f;
    double sum = 0.0;
    for (uint32_t i = 0; i < n; ++i) sum += buf[i];
    return (float)(sum / n);
}

inline void dc_remove(float* buf, uint32_t n) noexcept {
    if (!buf || n == 0) return;
    float dc = dc_offset(buf, n);
    if (std::fabs(dc) < 1e-9f) return;
    for (uint32_t i = 0; i < n; ++i) buf[i] -= dc;
}

inline void dc_remove_interleaved(float* buf, uint32_t frames, uint32_t channels) noexcept {
    if (!buf || frames == 0 || channels == 0) return;
    knst_vector<double> mean;
    if (!adetail::resize_ok(mean, channels)) return;
    for (uint32_t c = 0; c < channels; ++c) mean[c] = 0.0;
    for (uint32_t f = 0; f < frames; ++f)
        for (uint32_t c = 0; c < channels; ++c) mean[c] += buf[(size_t)f * channels + c];
    for (uint32_t c = 0; c < channels; ++c) mean[c] /= frames;
    for (uint32_t f = 0; f < frames; ++f)
        for (uint32_t c = 0; c < channels; ++c) buf[(size_t)f * channels + c] -= (float)mean[c];
}

inline void clip(float* inout, uint32_t n) noexcept {
    for (uint32_t i = 0; i < n; ++i) { float v = inout[i]; inout[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : (v == v ? v : 0.0f)); }
}

inline void apply_gain(float* inout, uint32_t n, float gain) noexcept {
    if (!inout) return;
    for (uint32_t i = 0; i < n; ++i) {
        float v = inout[i] * gain;
        inout[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    }
}
inline void apply_gain_db(float* inout, uint32_t n, float db) noexcept { apply_gain(inout, n, from_db(db)); }

inline void fade_in(float* inout, uint32_t frames, uint32_t channels, uint32_t fade_frames) noexcept {
    if (!inout || channels == 0 || fade_frames == 0) return;
    if (fade_frames > frames) fade_frames = frames;
    for (uint32_t i = 0; i < fade_frames; ++i) {
        float g = (float)i / (float)fade_frames;
        for (uint32_t c = 0; c < channels; ++c) inout[(size_t)i * channels + c] *= g;
    }
}

inline void fade_out(float* inout, uint32_t frames, uint32_t channels, uint32_t fade_frames) noexcept {
    if (!inout || channels == 0 || fade_frames == 0) return;
    if (fade_frames > frames) fade_frames = frames;
    uint32_t start = frames - fade_frames;
    for (uint32_t i = 0; i < fade_frames; ++i) {
        float g = (float)(fade_frames - i) / (float)fade_frames;
        for (uint32_t c = 0; c < channels; ++c) inout[(size_t)(start + i) * channels + c] *= g;
    }
}

/// dst += src * gain (no clipping — clip once at the end of your chain).
inline void mix_into(float* dst, const float* src, uint32_t n, float gain = 1.0f) noexcept {
    if (!dst || !src) return;
    for (uint32_t i = 0; i < n; ++i) dst[i] += src[i] * gain;
}

inline knst_vector<float> mix(const float* a, const float* b, uint32_t frames, uint32_t channels) noexcept {
    knst_vector<float> out;
    if (!a || !b || frames == 0 || channels == 0) return out;
    uint64_t n = (uint64_t)frames * channels;
    if (n > 0xFFFFFFFFull || !adetail::resize_ok(out, (uint32_t)n)) { out.clear(); return out; }
    for (uint32_t i = 0; i < (uint32_t)n; ++i) {
        float v = a[i] + b[i];
        out[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    }
    return out;
}

inline knst_vector<float> trim(const float* in, uint32_t frames, uint32_t channels,
                               uint32_t start_frame, uint32_t end_frame) noexcept {
    knst_vector<float> out;
    if (!in || channels == 0) return out;
    if (end_frame > frames) end_frame = frames;
    if (start_frame >= end_frame) return out;
    uint64_t len = (uint64_t)(end_frame - start_frame) * channels;
    if (len > 0xFFFFFFFFull || !adetail::resize_ok(out, (uint32_t)len)) { out.clear(); return out; }
    std::memcpy(out.data(), in + (size_t)start_frame * channels, (size_t)len * sizeof(float));
    return out;
}

inline knst_vector<float> concat(const float* a, uint32_t fa, const float* b, uint32_t fb,
                                 uint32_t channels) noexcept {
    knst_vector<float> out;
    if (channels == 0) return out;
    uint64_t n = ((uint64_t)fa + fb) * channels;
    if (n == 0 || n > 0xFFFFFFFFull || !adetail::resize_ok(out, (uint32_t)n)) { out.clear(); return out; }
    if (a && fa) std::memcpy(out.data(), a, (size_t)fa * channels * sizeof(float));
    if (b && fb) std::memcpy(out.data() + (size_t)fa * channels, b, (size_t)fb * channels * sizeof(float));
    return out;
}

/// Equal-power crossfade of the last `xf` frames of A into the first `xf` frames of B.
inline knst_vector<float> crossfade(const float* a, uint32_t fa, const float* b, uint32_t fb,
                                    uint32_t channels, uint32_t xf) noexcept {
    knst_vector<float> out;
    if (!a || !b || channels == 0) return out;
    if (xf > fa) xf = fa;
    if (xf > fb) xf = fb;
    uint64_t total = ((uint64_t)fa + fb - xf) * channels;
    if (total == 0 || total > 0xFFFFFFFFull || !adetail::resize_ok(out, (uint32_t)total)) { out.clear(); return out; }
    std::memcpy(out.data(), a, (size_t)(fa - xf) * channels * sizeof(float));
    float* o = out.data() + (size_t)(fa - xf) * channels;
    for (uint32_t i = 0; i < xf; ++i) {
        float t = (xf > 1) ? (float)i / (float)(xf - 1) : 1.0f;
        float ga = std::cos(t * (float)(M_PI / 2)), gb = std::sin(t * (float)(M_PI / 2));
        for (uint32_t c = 0; c < channels; ++c)
            o[(size_t)i * channels + c] = a[(size_t)(fa - xf + i) * channels + c] * ga + b[(size_t)i * channels + c] * gb;
    }
    std::memcpy(o + (size_t)xf * channels, b + (size_t)xf * channels, (size_t)(fb - xf) * channels * sizeof(float));
    return out;
}

inline void reverse(float* inout, uint32_t frames, uint32_t channels) noexcept {
    if (!inout || channels == 0) return;
    for (uint32_t i = 0, j = frames ? frames - 1 : 0; i < j; ++i, --j)
        for (uint32_t c = 0; c < channels; ++c) {
            float t = inout[(size_t)i * channels + c];
            inout[(size_t)i * channels + c] = inout[(size_t)j * channels + c];
            inout[(size_t)j * channels + c] = t;
        }
}

/// Constant-power pan of a stereo buffer. pan: -1 (left) … 0 (center, -3 dB each) … +1 (right).
inline void pan_stereo(float* inout, uint32_t frames, float pan) noexcept {
    if (!inout) return;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    float th = (pan + 1.0f) * (float)(M_PI / 4);
    float gl = std::cos(th), gr = std::sin(th);
    for (uint32_t i = 0; i < frames; ++i) { inout[i * 2] *= gl; inout[i * 2 + 1] *= gr; }
}

inline void normalize(float* inout, uint32_t n, float target_peak = 0.95f) noexcept {
    if (!inout || n == 0) return;
    float pk = 0;
    for (uint32_t i = 0; i < n; ++i) { float a = std::fabs(inout[i]); if (a > pk) pk = a; }
    if (pk < 1e-9f) return;
    float g = target_peak / pk;
    for (uint32_t i = 0; i < n; ++i) inout[i] *= g;
}

/// Scales so that the RMS equals `target_rms_db` (dBFS); optionally prevents clipping by capping the gain.
inline void normalize_rms(float* inout, uint32_t n, float target_rms_db = -18.0f, bool protect_peak = true) noexcept {
    stats s = analyze(inout, n);
    if (!s.valid || s.rms < 1e-9f) return;
    float g = from_db(target_rms_db) / s.rms;
    if (protect_peak && s.peak * g > 1.0f) g = 1.0f / s.peak;
    for (uint32_t i = 0; i < n; ++i) inout[i] *= g;
}

inline bool is_silent(const float* in, uint32_t n, float threshold_db = -60.0f) noexcept {
    if (!in || n == 0) return true;
    float th = from_db(threshold_db);
    for (uint32_t i = 0; i < n; ++i) if (std::fabs(in[i]) >= th) return false;
    return true;
}

/// Finds the non-silent range [start,end) in frames. Returns false if the whole buffer is silent.
inline bool find_active_range(const float* in, uint32_t frames, uint32_t channels, float threshold_db,
                              uint32_t& start, uint32_t& end) noexcept {
    start = end = 0;
    if (!in || channels == 0 || frames == 0) return false;
    float th = from_db(threshold_db);
    auto loud = [&](uint32_t f) { for (uint32_t c = 0; c < channels; ++c) if (std::fabs(in[(size_t)f * channels + c]) >= th) return true; return false; };
    uint32_t s = 0; while (s < frames && !loud(s)) ++s;
    if (s == frames) return false;
    uint32_t e = frames; while (e > s && !loud(e - 1)) --e;
    start = s; end = e;
    return true;
}

inline knst_vector<float> trim_silence(const float* in, uint32_t frames, uint32_t channels,
                                       float threshold_db = -60.0f) noexcept {
    uint32_t s, e;
    if (!find_active_range(in, frames, channels, threshold_db, s, e)) return knst_vector<float>();
    return trim(in, frames, channels, s, e);
}

/// tanh soft clipper.
inline void soft_clip(float* inout, uint32_t n) noexcept {
    for (uint32_t i = 0; i < n; ++i) inout[i] = std::tanh(inout[i]);
}

/// Peak limiter (instant attack, exponential release, linked channels).
inline void limiter(float* inout, uint32_t frames, uint32_t channels, uint32_t rate,
                    float threshold_db = -1.0f, float release_ms = 80.0f) noexcept {
    if (!inout || channels == 0 || rate == 0) return;
    float th = from_db(threshold_db);
    float rel = std::exp(-1.0f / (0.001f * release_ms * (float)rate + 1.0f));
    float env = 1.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float pk = 0.0f;
        for (uint32_t c = 0; c < channels; ++c) { float a = std::fabs(inout[(size_t)i * channels + c]); if (a > pk) pk = a; }
        float target = pk > th ? th / pk : 1.0f;
        env = target < env ? target : target + (env - target) * rel;
        for (uint32_t c = 0; c < channels; ++c) inout[(size_t)i * channels + c] *= env;
    }
}

inline void interleave(const float* const* planar, uint32_t frames, uint32_t channels, float* out) noexcept {
    for (uint32_t c = 0; c < channels; ++c)
        for (uint32_t i = 0; i < frames; ++i) out[(size_t)i * channels + c] = planar[c][i];
}
inline void deinterleave(const float* in, uint32_t frames, uint32_t channels, float* const* planar) noexcept {
    for (uint32_t c = 0; c < channels; ++c)
        for (uint32_t i = 0; i < frames; ++i) planar[c][i] = in[(size_t)i * channels + c];
}

// ---- channel layout conversion ------------------------------------------
// Assumed (WAV / SMPTE) orders: 3: FL FR FC · 4: FL FR BL BR · 5: FL FR FC BL BR ·
// 6: FL FR FC LFE BL BR · 7: FL FR FC LFE BC SL SR · 8: FL FR FC LFE BL BR SL SR

inline void downmix_coefs(uint32_t from, uint32_t c, float& l, float& r) noexcept {
    const float k = 0.70710678f;
    l = r = 0.0f;
    if (c == 0) { l = 1.0f; return; }
    if (c == 1) { r = 1.0f; return; }
    switch (from) {
        case 3: if (c == 2) l = r = k; break;
        case 4: if (c == 2) l = k; else if (c == 3) r = k; break;
        case 5: if (c == 2) l = r = k; else if (c == 3) l = k; else if (c == 4) r = k; break;
        case 6: if (c == 2) l = r = k; else if (c == 4) l = k; else if (c == 5) r = k; break;        // LFE dropped
        case 7: if (c == 2) l = r = k; else if (c == 4) l = r = 0.5f; else if (c == 5) l = k; else if (c == 6) r = k; break;
        case 8: if (c == 2) l = r = k; else if (c == 4 || c == 6) l = k; else if (c == 5 || c == 7) r = k; break;
        default: break;
    }
}

inline knst_vector<float> convert_channels(const float* in, uint32_t frames,
                                           uint32_t from_ch, uint32_t to_ch) noexcept {
    knst_vector<float> out;
    if (!in || frames == 0 || from_ch == 0 || to_ch == 0) return out;
    uint64_t n = (uint64_t)frames * to_ch;
    if (n > 0xFFFFFFFFull || !adetail::resize_ok(out, (uint32_t)n)) { out.clear(); return out; }
    float* o = out.data();
    if (from_ch == to_ch) { std::memcpy(o, in, (size_t)n * sizeof(float)); return out; }

    if (to_ch == 1) {                                  // → mono
        if (from_ch == 2) { for (uint32_t i = 0; i < frames; ++i) o[i] = 0.5f * (in[i * 2] + in[i * 2 + 1]); return out; }
        knst_vector<float> st = convert_channels(in, frames, from_ch, 2);
        if (st.empty()) { out.clear(); return out; }
        for (uint32_t i = 0; i < frames; ++i) o[i] = 0.5f * (st[i * 2] + st[i * 2 + 1]);
        return out;
    }
    if (to_ch == 2 && from_ch > 2) {                   // surround → stereo (normalised so in-phase peaks never clip)
        float lc[16] = {}, rc[16] = {}, sl = 0.0f, sr = 0.0f;
        uint32_t m = from_ch < 16 ? from_ch : 16;
        for (uint32_t c = 0; c < m; ++c) { downmix_coefs(from_ch, c, lc[c], rc[c]); sl += lc[c]; sr += rc[c]; }
        float gl = sl > 0 ? 1.0f / sl : 1.0f, gr = sr > 0 ? 1.0f / sr : 1.0f;
        for (uint32_t f = 0; f < frames; ++f) {
            const float* s = in + (size_t)f * from_ch; float l = 0, r = 0;
            for (uint32_t c = 0; c < m; ++c) { l += s[c] * lc[c]; r += s[c] * rc[c]; }
            o[f * 2] = l * gl; o[f * 2 + 1] = r * gr;
        }
        return out;
    }
    std::memset(o, 0, (size_t)n * sizeof(float));      // mono/stereo → N, or generic N → M
    for (uint32_t f = 0; f < frames; ++f) {
        const float* s = in + (size_t)f * from_ch; float* d = o + (size_t)f * to_ch;
        if (from_ch == 1) { d[0] = s[0]; if (to_ch > 1) d[1] = s[0]; }
        else { uint32_t m = from_ch < to_ch ? from_ch : to_ch; for (uint32_t c = 0; c < m; ++c) d[c] = s[c]; }
    }
    return out;
}

// ---- sample-rate conversion ------------------------------------------------

inline knst_vector<float> resample_linear(const float* in, uint32_t frames_in, uint32_t in_rate,
                                          uint32_t channels, uint32_t out_rate) noexcept {
    knst_vector<float> out;
    if (!in || frames_in == 0 || channels == 0 || in_rate == 0 || out_rate == 0) return out;
    uint64_t frames_out = (uint64_t)frames_in * out_rate / in_rate;
    if (frames_out == 0 || frames_out * channels > 0xFFFFFFFFull || !adetail::resize_ok(out, (uint32_t)(frames_out * channels))) { out.clear(); return out; }
    double ratio = (double)in_rate / (double)out_rate;
    for (uint64_t i = 0; i < frames_out; ++i) {
        double src = (double)i * ratio; uint64_t s = (uint64_t)src; float fr = (float)(src - (double)s);
        if (s + 1 >= frames_in) {
            for (uint32_t c = 0; c < channels; ++c) out[(uint32_t)i * channels + c] = in[(size_t)(frames_in - 1) * channels + c];
            continue;
        }
        for (uint32_t c = 0; c < channels; ++c) {
            float a = in[s * channels + c], b = in[(s + 1) * channels + c];
            out[(uint32_t)i * channels + c] = a + (b - a) * fr;
        }
    }
    return out;
}

enum class resample_quality : uint8_t { fast, medium, high, best };   // linear, 8, 16, 32 zero-crossings

inline double bessel_i0(double x) noexcept {
    double sum = 1.0, term = 1.0, q = x * x * 0.25;
    for (int k = 1; k < 64; ++k) { term *= q / ((double)k * k); sum += term; if (term < 1e-14 * sum) break; }
    return sum;
}

/// Band-limited windowed-sinc (Kaiser) resampler. Anti-aliasing cutoff adapts when down-sampling.
/// Output length = floor(frames_in * out_rate / in_rate).
inline knst_vector<float> resample(const float* in, uint32_t frames_in, uint32_t channels,
                                   uint32_t in_rate, uint32_t out_rate,
                                   resample_quality q = resample_quality::high) noexcept {
    knst_vector<float> out;
    if (!in || frames_in == 0 || channels == 0 || in_rate == 0 || out_rate == 0) return out;
    if (in_rate == out_rate) {
        uint64_t n = (uint64_t)frames_in * channels;
        if (n > 0xFFFFFFFFull || !adetail::resize_ok(out, (uint32_t)n)) { out.clear(); return out; }
        std::memcpy(out.data(), in, (size_t)n * sizeof(float));
        return out;
    }
    if (q == resample_quality::fast) return resample_linear(in, frames_in, in_rate, channels, out_rate);

    const int    zeros = q == resample_quality::medium ? 8 : q == resample_quality::high ? 16 : 32;
    const double beta  = q == resample_quality::medium ? 6.0 : q == resample_quality::high ? 8.6 : 10.5;
    const uint32_t RES = q == resample_quality::best ? 1024 : 512;           // table entries per input sample
    const double fc    = (out_rate < in_rate) ? (double)out_rate / (double)in_rate : 1.0;
    const double half  = (double)zeros / fc;
    if (half * RES > 8.0e6) return resample_linear(in, frames_in, in_rate, channels, out_rate);

    uint64_t frames_out = (uint64_t)frames_in * out_rate / in_rate;
    if (frames_out == 0 || frames_out * channels > 0xFFFFFFFFull) return out;

    uint32_t tsize = (uint32_t)(half * RES) + 2;
    knst_vector<float> tab;
    if (!adetail::resize_ok(tab, tsize)) return out;
    const double i0b = bessel_i0(beta);
    for (uint32_t k = 0; k < tsize; ++k) {
        double x = (double)k / RES, u = x / half;
        if (u >= 1.0) { tab[k] = 0.0f; continue; }
        double w = bessel_i0(beta * std::sqrt(1.0 - u * u)) / i0b;
        double a = M_PI * fc * x;
        double s = (a < 1e-12) ? 1.0 : std::sin(a) / a;
        tab[k] = (float)(fc * s * w);
    }
    knst_vector<float> wbuf;
    uint32_t maxtaps = (uint32_t)(2.0 * half) + 4;
    if (!adetail::resize_ok(wbuf, maxtaps) || !adetail::resize_ok(out, (uint32_t)(frames_out * channels))) { out.clear(); return out; }

    const double ratio = (double)in_rate / (double)out_rate;
    for (uint64_t i = 0; i < frames_out; ++i) {
        double t = (double)i * ratio;
        int64_t j0 = (int64_t)std::ceil(t - half), j1 = (int64_t)std::floor(t + half);
        int64_t a = j0 < 0 ? 0 : j0, b = j1 >= (int64_t)frames_in ? (int64_t)frames_in - 1 : j1;
        uint32_t taps = 0;
        for (int64_t j = a; j <= b; ++j, ++taps) {
            double idx = std::fabs(t - (double)j) * RES;
            uint32_t k = (uint32_t)idx; float fr = (float)(idx - k);
            wbuf[taps] = tab[k] + (tab[k + 1] - tab[k]) * fr;
        }
        for (uint32_t c = 0; c < channels; ++c) {
            const float* src = in + (size_t)a * channels + c; double acc = 0.0;
            for (uint32_t k = 0; k < taps; ++k) acc += (double)src[(size_t)k * channels] * wbuf[k];
            out[(uint32_t)i * channels + c] = (float)acc;
        }
    }
    return out;
}

// ---- biquad filters (RBJ cookbook) -----------------------------------------

enum class filter_t : uint8_t { lowpass, highpass, bandpass, notch, allpass, peak, lowshelf, highshelf };

struct biquad_coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

inline biquad_coeffs make_biquad(filter_t t, double rate, double freq, double q = 0.70710678,
                                 double gain_db = 0.0) noexcept {
    biquad_coeffs k;
    if (rate <= 0.0) return k;
    if (freq < 1.0) freq = 1.0;
    if (freq > rate * 0.499) freq = rate * 0.499;
    if (q < 0.01) q = 0.01;
    double w0 = 2.0 * M_PI * freq / rate, cs = std::cos(w0), sn = std::sin(w0), al = sn / (2.0 * q);
    double A = std::pow(10.0, gain_db / 40.0), sA = std::sqrt(A);
    double b0, b1, b2, a0, a1, a2;
    switch (t) {
        case filter_t::lowpass:  b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = b0; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
        case filter_t::highpass: b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = b0; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
        case filter_t::bandpass: b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
        case filter_t::notch:    b0 = 1; b1 = -2 * cs; b2 = 1; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
        case filter_t::allpass:  b0 = 1 - al; b1 = -2 * cs; b2 = 1 + al; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
        case filter_t::peak:     b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A; break;
        case filter_t::lowshelf:
            b0 = A * ((A + 1) - (A - 1) * cs + 2 * sA * al); b1 = 2 * A * ((A - 1) - (A + 1) * cs);
            b2 = A * ((A + 1) - (A - 1) * cs - 2 * sA * al); a0 = (A + 1) + (A - 1) * cs + 2 * sA * al;
            a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - 2 * sA * al; break;
        default: /* highshelf */
            b0 = A * ((A + 1) + (A - 1) * cs + 2 * sA * al); b1 = -2 * A * ((A - 1) + (A + 1) * cs);
            b2 = A * ((A + 1) + (A - 1) * cs - 2 * sA * al); a0 = (A + 1) - (A - 1) * cs + 2 * sA * al;
            a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - 2 * sA * al; break;
    }
    k.b0 = b0 / a0; k.b1 = b1 / a0; k.b2 = b2 / a0; k.a1 = a1 / a0; k.a2 = a2 / a0;
    return k;
}

/// Multi-channel biquad (transposed direct form II, double precision state).
class biquad {
public:
    enum { kMaxChannels = 16 };
    biquad() noexcept { reset(); }
    biquad(filter_t t, double rate, double freq, double q = 0.70710678, double gain_db = 0.0) noexcept {
        set(make_biquad(t, rate, freq, q, gain_db)); reset();
    }
    void set(const biquad_coeffs& c) noexcept { c_ = c; }
    void reset() noexcept { for (auto& v : z1_) v = 0.0; for (auto& v : z2_) v = 0.0; }
    float process_sample(float x, uint32_t ch) noexcept {
        double y = c_.b0 * x + z1_[ch];
        z1_[ch] = c_.b1 * x - c_.a1 * y + z2_[ch];
        z2_[ch] = c_.b2 * x - c_.a2 * y;
        return (float)y;
    }
    bool process(float* inout, uint32_t frames, uint32_t channels) noexcept {
        if (!inout || channels == 0 || channels > kMaxChannels) return false;
        for (uint32_t i = 0; i < frames; ++i)
            for (uint32_t c = 0; c < channels; ++c) { float& s = inout[(size_t)i * channels + c]; s = process_sample(s, c); }
        return true;
    }
private:
    biquad_coeffs c_; double z1_[kMaxChannels]; double z2_[kMaxChannels];
};

// ---- FFT / spectrum ----------------------------------------------------------

inline bool is_pow2(uint32_t n) noexcept { return n && !(n & (n - 1)); }

/// In-place forward radix-2 FFT. n must be a power of two.
inline bool fft(float* re, float* im, uint32_t n) noexcept {
    if (!re || !im || !is_pow2(n)) return false;
    for (uint32_t i = 1, j = 0; i < n; ++i) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len, wr = std::cos(ang), wi = std::sin(ang);
        for (uint32_t i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (uint32_t k = 0; k < len / 2; ++k) {
                uint32_t a = i + k, b = i + k + len / 2;
                double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = (float)(re[a] - xr); im[b] = (float)(im[a] - xi);
                re[a] = (float)(re[a] + xr); im[a] = (float)(im[a] + xi);
                double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
    return true;
}

/// Amplitude spectrum (n/2+1 bins, Hann window). A full-scale bin-centred sine reads ≈ 1.0.
inline knst_vector<float> magnitude_spectrum(const float* in, uint32_t n) noexcept {
    knst_vector<float> mag;
    if (!in || !is_pow2(n) || n < 2) return mag;
    knst_vector<float> re, im;
    if (!adetail::resize_ok(re, n) || !adetail::resize_ok(im, n) || !adetail::resize_ok(mag, n / 2 + 1)) { mag.clear(); return mag; }
    double wsum = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / n);
        re[i] = (float)(in[i] * w); im[i] = 0.0f; wsum += w;
    }
    fft(re.data(), im.data(), n);
    for (uint32_t k = 0; k <= n / 2; ++k) {
        double m = std::sqrt((double)re[k] * re[k] + (double)im[k] * im[k]);
        mag[k] = (float)((k == 0 || k == n / 2 ? 1.0 : 2.0) * m / wsum);
    }
    return mag;
}

/// Dominant frequency in Hz (parabolic peak interpolation). Returns 0 on failure.
inline double dominant_frequency(const float* in, uint32_t n, uint32_t rate) noexcept {
    knst_vector<float> m = magnitude_spectrum(in, n);
    if (m.size() < 4) return 0.0;
    uint32_t pk = 1;
    for (uint32_t k = 2; k + 1 < m.size(); ++k) if (m[k] > m[pk]) pk = k;
    double a = m[pk - 1], b = m[pk], c = m[pk + 1], den = a - 2 * b + c;
    double off = std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
    return ((double)pk + off) * rate / n;
}

// ---- generators --------------------------------------------------------------

enum class wave_t : uint8_t { sine, square, saw, triangle, noise };

inline void generate(float* out, uint32_t frames, uint32_t channels, wave_t type,
                     float freq_hz, uint32_t rate, float amplitude = 1.0f) noexcept {
    if (!out || channels == 0 || rate == 0) return;
    double phase = 0.0, inc = 2.0 * M_PI * freq_hz / (double)rate;
    adetail::rng rg;
    for (uint32_t i = 0; i < frames; ++i) {
        float v = 0;
        switch (type) {
            case wave_t::sine:     v = (float)std::sin(phase); break;
            case wave_t::square:   v = phase < M_PI ? 1.0f : -1.0f; break;
            case wave_t::saw:      v = (float)(phase / M_PI - 1.0); break;
            case wave_t::triangle: v = (float)(1.0 - 4.0 * std::fabs(phase / (2.0 * M_PI) - 0.5)); break;
            case wave_t::noise:    v = rg.uniform() * 2.0f - 1.0f; break;
        }
        v *= amplitude;
        for (uint32_t c = 0; c < channels; ++c) out[(size_t)i * channels + c] = v;
        phase += inc;
        if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
    }
}

} // namespace dsp

// ---- pcm_buffer conveniences -----------------------------------------------

inline result<pcm_buffer> resample_buffer(const pcm_buffer& b, uint32_t out_rate,
                                          dsp::resample_quality q = dsp::resample_quality::high) noexcept {
    using R = result<pcm_buffer>;
    if (!b.valid() || out_rate == 0) return R::fail(status::InvalidArgument);
    pcm_buffer o; o.rate = out_rate; o.channels = b.channels; o.source_bits = b.source_bits; o.container = b.container; o.codec = b.codec;
    o.samples = dsp::resample(b.samples.data(), b.frames(), b.channels, b.rate, out_rate, q);
    if (o.samples.empty()) return R::fail(status::OutOfMemory);
    return R::success(std::move(o));
}

inline result<pcm_buffer> convert_buffer_channels(const pcm_buffer& b, uint32_t out_ch) noexcept {
    using R = result<pcm_buffer>;
    if (!b.valid() || out_ch == 0) return R::fail(status::InvalidArgument);
    pcm_buffer o; o.rate = b.rate; o.channels = out_ch; o.source_bits = b.source_bits; o.container = b.container; o.codec = b.codec;
    o.samples = dsp::convert_channels(b.samples.data(), b.frames(), b.channels, out_ch);
    if (o.samples.empty()) return R::fail(status::OutOfMemory);
    return R::success(std::move(o));
}

// ============================================================
// stream — real-time blocking PCM I/O
// ============================================================

#if KNST_USING_PLATFORM_LINUX

namespace detail_pa {

struct api {
    void* handle = nullptr;
    void* (*new_)(const char*, const char*, int, const char*,
                  const char*, const void*, const void*, const void*,
                  int*) = nullptr;
    int   (*write)(void*, const void*, size_t, int*) = nullptr;
    int   (*read)(void*, void*, size_t, int*) = nullptr;
    int   (*drain)(void*, int*) = nullptr;
    int64_t (*get_latency)(void*, int*) = nullptr;
    void  (*free_)(void*) = nullptr;
    bool ok = false;
};

inline api& get() noexcept {
    static api a = []() {
        api r;
        const char* names[] = { "libpulse-simple.so.0", "libpulse-simple.so" };
        for (const char* n : names) {
            r.handle = ::dlopen(n, RTLD_LAZY);
            if (r.handle) break;
        }
        if (!r.handle) return r;
        r.new_        = (decltype(r.new_))        ::dlsym(r.handle, "pa_simple_new");
        r.write       = (decltype(r.write))       ::dlsym(r.handle, "pa_simple_write");
        r.read        = (decltype(r.read))        ::dlsym(r.handle, "pa_simple_read");
        r.drain       = (decltype(r.drain))       ::dlsym(r.handle, "pa_simple_drain");
        r.get_latency = (decltype(r.get_latency)) ::dlsym(r.handle, "pa_simple_get_latency");   // optional
        r.free_       = (decltype(r.free_))       ::dlsym(r.handle, "pa_simple_free");
        r.ok = r.new_ && r.write && r.read && r.drain && r.free_;
        return r;
    }();
    return a;
}

enum : int { PA_SAMPLE_S16LE = 3, PA_SAMPLE_FLOAT32LE = 5 };
enum : int { PA_STREAM_PLAYBACK = 1, PA_STREAM_RECORD = 2 };

struct sample_spec_c { int format; uint32_t rate; uint8_t channels; };
struct buffer_attr_c { uint32_t maxlength, tlength, prebuf, minreq, fragsize; };

// pa_error_code_t: 1 ACCESS, 2 COMMAND, 3 INVALID, 4 EXIST, 5 NOENTITY, 6 CONNECTIONREFUSED, 19 NOTSUPPORTED ...
inline status map_err(int e) noexcept {
    switch (e) {
        case 1:  return status::PermissionDenied;
        case 2:
        case 3:  return status::InvalidArgument;
        case 5:  return status::NotFound;
        case 6:  return status::IoError;
        case 19: return status::NotSupported;
        default: return status::Unknown;
    }
}

inline const char* kind(const knst_audio_device& d) noexcept {
    return d.is_input ? "source" : "sink";
}

inline bool ok_name(const knst_audio_device& d) noexcept {
    return knst_dev::is_safe_token(d.name, 200);
}

inline uint32_t parse_percent(const char* text) noexcept {
    if (!text) return 0;
    const char* p = ::strchr(text, '%');
    if (!p) return 0;
    const char* s = p;
    while (s > text && s[-1] >= '0' && s[-1] <= '9') --s;
    return (uint32_t)::atoi(s);
}

inline double parse_db(const char* text) noexcept {
    if (!text) return -144.0;
    const char* p = ::strstr(text, " dB");
    if (!p) return -144.0;
    const char* s = p;
    while (s > text) {
        char c = s[-1];
        if ((c >= '0' && c <= '9') || c == '.' || c == ',' || c == '-') --s;
        else break;
    }
    if (s == p) return -144.0;
    char buf[64];
    size_t n = (size_t)(p - s);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    for (size_t i = 0; i < n; ++i) buf[i] = (s[i] == ',') ? '.' : s[i];
    buf[n] = '\0';
    return ::atof(buf);
}

inline bool line_value(const char* line, const char* key, const char*& v) noexcept {
    size_t kl = ::strlen(key);
    if (::strncmp(line, key, kl) != 0) return false;
    const char* p = line + kl;
    if (*p != ' ' && *p != '\t' && *p != ':' && *p != '=') return false;
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=') ++p;
    v = p;
    return true;
}

inline knst_c16string kv_string(const char* v) noexcept {
    if (!v) return knst_c16string();
    if (*v == '"') ++v;
    knst_c16string s(v);
    if (s.length() > 0 && s[s.length() - 1] == u'"') s.resize(s.length() - 1);
    return s;
}

inline void parse_streams(const char* text, bool is_output,
                          knst_vector<stream_info>& out) noexcept {
    if (!text) return;
    stream_info cur;
    bool in_stream = false, in_props = false;
    knst_dev::for_each_line(text, [&](const char* p, size_t len) {
        char line[512];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        ::memcpy(line, p, len); line[len] = '\0';
        const char* header = is_output ? "Sink Input #" : "Source Output #";
        size_t hl = ::strlen(header);
        if (::strncmp(line, header, hl) == 0) {
            if (in_stream && cur.valid) out.push_back(cur);
            cur = stream_info();
            cur.is_output = is_output;
            cur.index     = (uint32_t)::atoi(line + hl);
            cur.valid     = true;
            in_stream     = true;
            in_props      = false;
            return;
        }
        if (!in_stream) return;
        char* content = line;
        while (*content == '\t' || *content == ' ') ++content;
        if (in_props) {
            const char* v;
            if      (line_value(content, "application.name", v)) cur.application = kv_string(v);
            else if (line_value(content, "media.name", v))       cur.media_name  = kv_string(v);
        } else if (::strcmp(content, "Properties:") == 0) {
            in_props = true;
        } else {
            const char* v;
            if      (line_value(content, "Driver", v))  cur.driver = knst_c16string(v);
            else if (line_value(content, "Sink",   v))  cur.device_index = (uint32_t)::atoi(v);
            else if (line_value(content, "Source", v))  cur.device_index = (uint32_t)::atoi(v);
            else if (line_value(content, "Volume", v))  cur.volume_percent = parse_percent(v);
            else if (line_value(content, "Mute",   v))  cur.muted = (::strcmp(v, "yes") == 0);
            else if (line_value(content, "Corked", v))  cur.corked = (::strcmp(v, "yes") == 0);
        }
    });
    if (in_stream && cur.valid) out.push_back(cur);
}

inline void parse_volume_lr(const char* text, uint32_t& left, uint32_t& right,
                            double& left_db, double& right_db) noexcept {
    left = right = 0;
    left_db = right_db = -144.0;
    if (!text) return;
    const char* fl = ::strstr(text, "front-left:");
    const char* fr = ::strstr(text, "front-right:");
    if (fl) left = parse_percent(fl);
    if (fr) right = parse_percent(fr);
    if (!fl && !fr) left = right = parse_percent(text);
    if (fl) left_db  = parse_db(fl);
    if (fr) right_db = parse_db(fr);
}

} // namespace detail_pa
#endif // Linux

#if KNST_USING_PLATFORM_WINDOWS
namespace detail_wasapi {
// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT without needing ksguid.lib / INITGUID
static const GUID subtype_float = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
inline void sleep_ms(uint32_t ms) noexcept { ::Sleep(ms ? ms : 1); }
}
#endif

#if defined(KNST_USING_PLATFORM_ANDROID)
namespace detail_aaudio {

enum : int32_t { AAUDIO_OK_ = 0, AAUDIO_DIR_OUT = 0, AAUDIO_DIR_IN = 1,
                 AAUDIO_FMT_I16 = 1, AAUDIO_FMT_F32 = 2,
                 AAUDIO_PERF_LOWLAT = 12, AAUDIO_SHARE_SHARED = 1 };

struct api {
    void* h = nullptr;
    int32_t (*createBuilder)(void**) = nullptr;
    void    (*builderDelete)(void*) = nullptr;
    void    (*setDirection)(void*, int32_t) = nullptr;
    void    (*setSampleRate)(void*, int32_t) = nullptr;
    void    (*setChannelCount)(void*, int32_t) = nullptr;
    void    (*setFormat)(void*, int32_t) = nullptr;
    void    (*setPerfMode)(void*, int32_t) = nullptr;
    void    (*setSharingMode)(void*, int32_t) = nullptr;
    int32_t (*openStream)(void*, void**) = nullptr;
    int32_t (*streamRead)(void*, void*, int32_t, int64_t) = nullptr;
    int32_t (*streamWrite)(void*, const void*, int32_t, int64_t) = nullptr;
    int32_t (*requestStart)(void*) = nullptr;
    int32_t (*requestStop)(void*) = nullptr;
    int32_t (*streamClose)(void*) = nullptr;
    int32_t (*getSampleRate)(void*) = nullptr;
    int32_t (*getChannelCount)(void*) = nullptr;
    bool ok = false;
};

inline api& get() noexcept {
    static api a = []() {
        api r;
        r.h = ::dlopen("libaaudio.so", RTLD_LAZY);
        if (!r.h) return r;
        r.createBuilder     = (decltype(r.createBuilder))     ::dlsym(r.h, "AAudio_createStreamBuilder");
        r.builderDelete     = (decltype(r.builderDelete))     ::dlsym(r.h, "AAudioStreamBuilder_delete");
        r.setDirection      = (decltype(r.setDirection))      ::dlsym(r.h, "AAudioStreamBuilder_setDirection");
        r.setSampleRate     = (decltype(r.setSampleRate))     ::dlsym(r.h, "AAudioStreamBuilder_setSampleRate");
        r.setChannelCount   = (decltype(r.setChannelCount))   ::dlsym(r.h, "AAudioStreamBuilder_setChannelCount");
        r.setFormat         = (decltype(r.setFormat))         ::dlsym(r.h, "AAudioStreamBuilder_setFormat");
        r.setPerfMode       = (decltype(r.setPerfMode))       ::dlsym(r.h, "AAudioStreamBuilder_setPerformanceMode");
        r.setSharingMode    = (decltype(r.setSharingMode))    ::dlsym(r.h, "AAudioStreamBuilder_setSharingMode");
        r.openStream        = (decltype(r.openStream))        ::dlsym(r.h, "AAudioStreamBuilder_openStream");
        r.streamRead        = (decltype(r.streamRead))        ::dlsym(r.h, "AAudioStream_read");
        r.streamWrite       = (decltype(r.streamWrite))       ::dlsym(r.h, "AAudioStream_write");
        r.requestStart      = (decltype(r.requestStart))      ::dlsym(r.h, "AAudioStream_requestStart");
        r.requestStop       = (decltype(r.requestStop))       ::dlsym(r.h, "AAudioStream_requestStop");
        r.streamClose       = (decltype(r.streamClose))       ::dlsym(r.h, "AAudioStream_close");
        r.getSampleRate     = (decltype(r.getSampleRate))     ::dlsym(r.h, "AAudioStream_getSampleRate");
        r.getChannelCount   = (decltype(r.getChannelCount))   ::dlsym(r.h, "AAudioStream_getChannelCount");
        r.ok = r.createBuilder && r.builderDelete && r.setDirection && r.setSampleRate &&
               r.setChannelCount && r.setFormat && r.openStream && r.streamRead &&
               r.streamWrite && r.requestStart && r.requestStop && r.streamClose;
        return r;
    }();
    return a;
}

inline void* open(int direction, uint32_t rate, uint32_t channels) noexcept {
    auto& aa = get();
    if (!aa.ok) return nullptr;
    void* b = nullptr;
    if (aa.createBuilder(&b) != AAUDIO_OK_ || !b) return nullptr;
    aa.setDirection(b, direction);
    aa.setSampleRate(b, (int32_t)rate);
    aa.setChannelCount(b, (int32_t)channels);
    aa.setFormat(b, AAUDIO_FMT_F32);
    if (aa.setPerfMode)    aa.setPerfMode(b, AAUDIO_PERF_LOWLAT);
    if (aa.setSharingMode) aa.setSharingMode(b, AAUDIO_SHARE_SHARED);
    void* s = nullptr;
    int32_t rc = aa.openStream(b, &s);
    aa.builderDelete(b);
    if (rc != AAUDIO_OK_ || !s) return nullptr;
    if (aa.requestStart(s) != AAUDIO_OK_) { aa.streamClose(s); return nullptr; }
    return s;
}

inline void close(void* s) noexcept {
    if (!s) return;
    auto& aa = get();
    if (aa.ok) { aa.requestStop(s); aa.streamClose(s); }
}

} // namespace detail_aaudio
#endif

/// Blocking real-time PCM stream (float32 interleaved). No hidden thread:
/// call write_interleaved() / read_interleaved() from your own loop.
class stream {
public:
    struct config {
        uint32_t rate            = 48000;
        uint32_t channels        = 2;
        uint32_t buffer_frames   = 512;
        bool     is_output       = true;
    };

    stream() noexcept = default;
    stream(stream&& o) noexcept { take(o); }
    stream& operator=(stream&& o) noexcept { if (this != &o) { close(); take(o); } return *this; }
    ~stream() { close(); }
    stream(const stream&) = delete;
    stream& operator=(const stream&) = delete;

    /// Opens a stream on a specific device.
    static result<stream> open(const knst_audio_device& device, const config& c) noexcept {
        if (c.is_output && !device.is_output) return result<stream>::fail(status::InvalidArgument);
        if (!c.is_output && !device.is_input) return result<stream>::fail(status::InvalidArgument);
        return open_impl(&device, c);
    }

    /// Opens a stream on the system default output / input.
    static result<stream> open_default(const config& c) noexcept { return open_impl(nullptr, c); }

    result<uint32_t> write_interleaved(const float* buffer, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!is_open()) return R::fail(status::BadState);
        if (!is_output_) return R::fail(status::InvalidArgument);
        if (!buffer || frames == 0) return R::fail(status::InvalidArgument);
    #if defined(KNST_USING_PLATFORM_ANDROID)
        auto& aa = detail_aaudio::get();
        uint32_t done = 0;
        while (done < frames) {
            int32_t n = aa.streamWrite(aa_stream_,
                                        buffer + (size_t)done * channels_,
                                        (int32_t)(frames - done),
                                        500000000LL);
            if (n <= 0) { ++xruns_; return R::fail(status::IoError, (int)n); }
            done += (uint32_t)n;
        }
        return R::success(frames);
    #elif KNST_USING_PLATFORM_LINUX
        auto& pa = detail_pa::get();
        size_t bytes = (size_t)frames * channels_ * sizeof(float);
        int err = 0;
        if (pa.write(pa_, buffer, bytes, &err) < 0) { ++xruns_; return R::fail(detail_pa::map_err(err), err); }
        return R::success(frames);
    #elif KNST_USING_PLATFORM_WINDOWS
        uint32_t done = 0;
        while (done < frames) {
            UINT32 pad = 0;
            HRESULT hr = wclient_->GetCurrentPadding(&pad);
            if (FAILED(hr)) return R::fail(status::IoError, (int)hr);
            uint32_t avail = wbuf_ > pad ? wbuf_ - pad : 0;
            if (avail == 0) { detail_wasapi::sleep_ms((uint32_t)(1000ull * wbuf_ / (rate_ ? rate_ : 1) / 4)); continue; }
            uint32_t n = frames - done < avail ? frames - done : avail;
            BYTE* dst = nullptr;
            hr = wrender_->GetBuffer(n, &dst);
            if (FAILED(hr)) { ++xruns_; return R::fail(status::IoError, (int)hr); }
            ::memcpy(dst, buffer + (size_t)done * channels_, (size_t)n * channels_ * sizeof(float));
            hr = wrender_->ReleaseBuffer(n, 0);
            if (FAILED(hr)) return R::fail(status::IoError, (int)hr);
            done += n;
        }
        return R::success(frames);
    #else
        return R::fail(status::NotSupported);
    #endif
    }

    result<uint32_t> write_interleaved(const int16_t* buffer, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!buffer || frames == 0) return R::fail(status::InvalidArgument);
        const uint32_t step = 4096 / (channels_ ? channels_ : 1) + 1;
        if (!adetail::resize_ok(scratch_, step * channels_)) return R::fail(status::OutOfMemory);
        uint32_t done = 0;
        while (done < frames) {
            uint32_t n = frames - done < step ? frames - done : step;
            for (uint32_t i = 0; i < n * channels_; ++i) scratch_[i] = buffer[(size_t)done * channels_ + i] / 32768.0f;
            auto w = write_interleaved(scratch_.data(), n);
            if (!w) return w;
            done += n;
        }
        return R::success(frames);
    }

    result<uint32_t> read_interleaved(float* buffer, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!is_open()) return R::fail(status::BadState);
        if (is_output_) return R::fail(status::InvalidArgument);
        if (!buffer || frames == 0) return R::fail(status::InvalidArgument);
    #if defined(KNST_USING_PLATFORM_ANDROID)
        auto& aa = detail_aaudio::get();
        uint32_t done = 0;
        while (done < frames) {
            int32_t n = aa.streamRead(aa_stream_,
                                       buffer + (size_t)done * channels_,
                                       (int32_t)(frames - done),
                                       500000000LL);
            if (n <= 0) { ++xruns_; return R::fail(status::IoError, (int)n); }
            done += (uint32_t)n;
        }
        return R::success(frames);
    #elif KNST_USING_PLATFORM_LINUX
        auto& pa = detail_pa::get();
        size_t bytes = (size_t)frames * channels_ * sizeof(float);
        int err = 0;
        if (pa.read(pa_, buffer, bytes, &err) < 0) { ++xruns_; return R::fail(detail_pa::map_err(err), err); }
        return R::success(frames);
    #elif KNST_USING_PLATFORM_WINDOWS
        if (!wcapture_) return R::fail(status::NotSupported);
        uint32_t done = 0;
        while (done < frames) {
            UINT32 packet = 0;
            HRESULT hr = wcapture_->GetNextPacketSize(&packet);
            if (FAILED(hr)) return R::fail(status::IoError, (int)hr);
            if (packet == 0) { detail_wasapi::sleep_ms(10); continue; }
            BYTE* data = nullptr; UINT32 nframes = 0; DWORD flags = 0;
            hr = wcapture_->GetBuffer(&data, &nframes, &flags, nullptr, nullptr);
            if (FAILED(hr)) return R::fail(status::IoError, (int)hr);
            uint32_t take = (frames - done < nframes) ? (frames - done) : nframes;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                ::memset(buffer + (size_t)done * channels_, 0, (size_t)take * channels_ * sizeof(float));
            else
                ::memcpy(buffer + (size_t)done * channels_, data, (size_t)take * channels_ * sizeof(float));
            wcapture_->ReleaseBuffer(nframes);
            done += take;
        }
        return R::success(frames);
    #else
        return R::fail(status::NotSupported);
    #endif
    }

    result<uint32_t> read_interleaved(int16_t* buffer, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!buffer || frames == 0) return R::fail(status::InvalidArgument);
        knst_vector<float> tmp;
        if (!adetail::resize_ok(tmp, frames * channels_)) return R::fail(status::OutOfMemory);
        auto r = read_interleaved(tmp.data(), frames);
        if (!r) return r;
        uint32_t n = frames * channels_;
        for (uint32_t i = 0; i < n; ++i) {
            float v = tmp[i];
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            long q = std::lrintf(v * 32768.0f);
            buffer[i] = (int16_t)(q > 32767 ? 32767 : q);
        }
        return R::success(frames);
    }

    /// Blocks until all queued audio has been played.
    void drain() noexcept {
    #if defined(KNST_USING_PLATFORM_ANDROID)
        // AAudio has no explicit drain; the stream drains naturally
    #elif KNST_USING_PLATFORM_LINUX
        if (pa_ && is_output_) { auto& pa = detail_pa::get(); int err = 0; pa.drain(pa_, &err); }
    #elif KNST_USING_PLATFORM_WINDOWS
        if (wclient_ && is_output_) {
            for (int i = 0; i < 400; ++i) {
                UINT32 pad = 0;
                if (FAILED(wclient_->GetCurrentPadding(&pad)) || pad == 0) break;
                detail_wasapi::sleep_ms(5);
            }
        }
    #endif
    }

    /// Output latency in milliseconds (Linux: from PulseAudio if available; Windows: buffer size).
    double latency_ms() const noexcept {
    #if defined(KNST_USING_PLATFORM_ANDROID)
        // AAudio doesn't expose a stable latency query via dlsym path here
    #elif KNST_USING_PLATFORM_LINUX
        auto& pa = detail_pa::get();
        if (pa_ && pa.get_latency) { int err = 0; int64_t us = pa.get_latency(pa_, &err); if (us >= 0) return us / 1000.0; }
    #elif KNST_USING_PLATFORM_WINDOWS
        if (wclient_ && rate_) return 1000.0 * wbuf_ / rate_;
    #endif
        return rate_ ? 1000.0 * buf_frames_ / rate_ : 0.0;
    }

    void close() noexcept {
    #if defined(KNST_USING_PLATFORM_ANDROID)
        if (aa_stream_) { detail_aaudio::close(aa_stream_); aa_stream_ = nullptr; }
    #elif KNST_USING_PLATFORM_LINUX
        if (pa_) { auto& pa = detail_pa::get(); if (pa.free_) pa.free_(pa_); pa_ = nullptr; }
    #elif KNST_USING_PLATFORM_WINDOWS
        if (wclient_) wclient_->Stop();
                if (wrender_)  { wrender_->Release();  wrender_  = nullptr; }
        if (wcapture_) { wcapture_->Release(); wcapture_ = nullptr; }
        if (wclient_) { wclient_->Release(); wclient_ = nullptr; }
        if (wcom_) { ::CoUninitialize(); wcom_ = false; }
    #endif
    }

    uint32_t rate()          const noexcept { return rate_; }
    uint32_t channels()      const noexcept { return channels_; }
    uint32_t buffer_frames() const noexcept { return buf_frames_; }
    uint32_t xruns()         const noexcept { return xruns_; }
    bool     is_output()     const noexcept { return is_output_; }
    bool     is_open()       const noexcept {
    #if defined(KNST_USING_PLATFORM_ANDROID)
        return aa_stream_ != nullptr;
    #elif KNST_USING_PLATFORM_LINUX
        return pa_ != nullptr;
    #elif KNST_USING_PLATFORM_WINDOWS
        return wclient_ != nullptr;
    #else
        return false;
    #endif
    }

private:
    static result<stream> open_impl(const knst_audio_device* device, const config& c) noexcept {
        using R = result<stream>;
        if (c.rate == 0 || c.channels == 0 || c.channels > 8 || c.buffer_frames == 0)
            return R::fail(status::InvalidArgument);
    #if defined(KNST_USING_PLATFORM_ANDROID)
        auto& aa = detail_aaudio::get();
        if (!aa.ok) return R::fail(status::NotSupported);
        void* s = detail_aaudio::open(c.is_output ? detail_aaudio::AAUDIO_DIR_OUT
                                                  : detail_aaudio::AAUDIO_DIR_IN,
                                      c.rate, c.channels);
        if (!s) return R::fail(status::Unknown);
        stream ret;
        ret.aa_stream_ = s;
        ret.rate_ = c.rate;
        ret.channels_ = c.channels;
        ret.buf_frames_ = c.buffer_frames;
        ret.is_output_ = c.is_output;
        return R::success(std::move(ret));
    #elif KNST_USING_PLATFORM_LINUX
        auto& pa = detail_pa::get();
        if (!pa.ok) return R::fail(status::NotSupported);

        knst_byte_string name_utf8;
        const char* dev_name = nullptr;
        if (device) { name_utf8 = knst_dev::to_utf8(device->name); dev_name = knst_dev::text(name_utf8); }

        detail_pa::sample_spec_c ss;
        ss.format = detail_pa::PA_SAMPLE_FLOAT32LE; ss.rate = c.rate; ss.channels = (uint8_t)c.channels;
        detail_pa::buffer_attr_c attr;
        uint32_t bytes = c.buffer_frames * c.channels * 4;
        attr.maxlength = (uint32_t)-1;
        attr.tlength   = c.is_output ? bytes : (uint32_t)-1;
        attr.prebuf    = (uint32_t)-1;
        attr.minreq    = (uint32_t)-1;
        attr.fragsize  = c.is_output ? (uint32_t)-1 : bytes;

        int err = 0;
        void* h = pa.new_(nullptr, "KernelNucleusT",
                          c.is_output ? detail_pa::PA_STREAM_PLAYBACK : detail_pa::PA_STREAM_RECORD,
                          dev_name, "knst_aud::stream", &ss, nullptr, &attr, &err);
        if (!h) return R::fail(detail_pa::map_err(err), err);

        stream s;
        s.pa_ = h; s.rate_ = c.rate; s.channels_ = c.channels; s.buf_frames_ = c.buffer_frames; s.is_output_ = c.is_output;
        return R::success(std::move(s));
    #elif KNST_USING_PLATFORM_WINDOWS
        stream s;
        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        s.wcom_ = SUCCEEDED(hr);
        if (!c.is_output) {
            IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr;
            hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en));
            if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
            if (device && !device->name.empty())
                hr = en->GetDevice(reinterpret_cast<LPCWSTR>(device->name.data()), &dev);
            else
                hr = en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
            en->Release();
            if (FAILED(hr) || !dev) return R::fail(status::NotFound, (int)hr);
            hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(&s.wclient_));
            dev->Release();
            if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
            WAVEFORMATEX* mix = nullptr;
            hr = s.wclient_->GetMixFormat(&mix);
            if (FAILED(hr) || !mix) return R::fail(status::Unknown, (int)hr);
            bool format_ok = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                             (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22 &&
                              IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat,
                                          detail_wasapi::subtype_float));
            if (!format_ok) { ::CoTaskMemFree(mix); return R::fail(status::NotSupported); }
            hr = s.wclient_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, mix, nullptr);
            ::CoTaskMemFree(mix);
            if (FAILED(hr)) return R::fail(status::NotSupported, (int)hr);
            UINT32 bf = 0; s.wclient_->GetBufferSize(&bf);
            hr = s.wclient_->GetService(__uuidof(IAudioCaptureClient),
                                        reinterpret_cast<void**>(&s.wcapture_));
            if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
            s.wclient_->Start();
            s.wbuf_ = bf;
            s.rate_ = c.rate; s.channels_ = c.channels; s.buf_frames_ = c.buffer_frames;
            s.is_output_ = false;
            return R::success(std::move(s));
        }
        IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr;
        hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en));
        if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
        if (device && !device->name.empty()) hr = en->GetDevice(reinterpret_cast<LPCWSTR>(device->name.data()), &dev);
        else hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        en->Release();
        if (FAILED(hr) || !dev) return R::fail(status::NotFound, (int)hr);
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&s.wclient_));
        dev->Release();
        if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);

        WAVEFORMATEXTENSIBLE wf; ::memset(&wf, 0, sizeof(wf));
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; wf.Format.nChannels = (WORD)c.channels;
        wf.Format.nSamplesPerSec = c.rate; wf.Format.wBitsPerSample = 32;
        wf.Format.nBlockAlign = (WORD)(c.channels * 4); wf.Format.nAvgBytesPerSec = c.rate * c.channels * 4;
        wf.Format.cbSize = 22; wf.Samples.wValidBitsPerSample = 32;
        wf.dwChannelMask = (DWORD)codec::channel_mask_for(c.channels);
        wf.SubFormat = detail_wasapi::subtype_float;
        REFERENCE_TIME dur = (REFERENCE_TIME)(10000000.0 * c.buffer_frames / c.rate);
        // AUTOCONVERTPCM | SRC_DEFAULT_QUALITY: the engine converts rate/channels for us.
        hr = s.wclient_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0x80000000u | 0x08000000u, dur, 0, &wf.Format, nullptr);
        if (FAILED(hr)) return R::fail(status::NotSupported, (int)hr);
        UINT32 bf = 0;
        hr = s.wclient_->GetBufferSize(&bf);
        if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
        hr = s.wclient_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&s.wrender_));
        if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
        hr = s.wclient_->Start();
        if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
        s.wbuf_ = bf; s.rate_ = c.rate; s.channels_ = c.channels; s.buf_frames_ = c.buffer_frames; s.is_output_ = true;
        return R::success(std::move(s));
    #else
        (void)device; (void)c;
        return R::fail(status::NotSupported);
    #endif
    }

    void take(stream& o) noexcept {
        rate_ = o.rate_; channels_ = o.channels_; buf_frames_ = o.buf_frames_; xruns_ = o.xruns_; is_output_ = o.is_output_;
        scratch_ = std::move(o.scratch_);
    #if defined(KNST_USING_PLATFORM_ANDROID)
        aa_stream_ = o.aa_stream_; o.aa_stream_ = nullptr;
    #elif KNST_USING_PLATFORM_LINUX
        pa_ = o.pa_; o.pa_ = nullptr;
    #elif KNST_USING_PLATFORM_WINDOWS
        wclient_ = o.wclient_; wrender_ = o.wrender_; wcapture_ = o.wcapture_; wbuf_ = o.wbuf_; wcom_ = o.wcom_;
        o.wclient_ = nullptr; o.wrender_ = nullptr; o.wcapture_ = nullptr; o.wcom_ = false;
    #endif
    }

#if defined(KNST_USING_PLATFORM_ANDROID)
    void*    aa_stream_   = nullptr;
#elif KNST_USING_PLATFORM_LINUX
    void*    pa_          = nullptr;
#elif KNST_USING_PLATFORM_WINDOWS
        IAudioClient*        wclient_  = nullptr;
    IAudioRenderClient*  wrender_  = nullptr;
    IAudioCaptureClient* wcapture_ = nullptr;
    uint32_t             wbuf_    = 0;
    bool                 wcom_    = false;
#endif
    uint32_t rate_        = 0;
    uint32_t channels_    = 0;
    uint32_t buf_frames_  = 0;
    uint32_t xruns_       = 0;
    bool     is_output_   = true;
    knst_vector<float> scratch_;
};

// ============================================================
// decoder_stream — memory-bounded streaming decode
//
// Reads any supported format frame-by-frame from a FILE*.
//   RAM = ~200 KB regardless of file length
//   No decode_options::max_samples limit
//   No max_frames limit
//
// Native:  WAV/RF64, AIFF/AIFC, AU, FLAC, MP3 (minimp3)
// Sndfile: OGG/OPUS/AAC/WavPack/APE/TTA (if libsndfile present)
//
// Usage:
//   auto ds = decoder_stream::open(u"song.mp3");
//   auto sink = stream::open_default({ds.value.rate(), ds.value.channels()});
//   ds.value.play_to(sink.value);   // streams 3+ hours, RAM fixed
// ============================================================

namespace detail_file_io {
    /// Persistent FILE-backed state for libsndfile virtual IO.
    /// Heap-allocated because libsndfile stores the pointer (not a copy).
    struct file_io { FILE* f; int64_t size; };

    inline void init_vio(codec::sndfile::SF_VIRTUAL_IO_c& vio) noexcept {
        vio.get_filelen = [](void* u) -> int64_t {
            return ((file_io*)u)->size;
        };
        vio.seek = [](int64_t off, int wh, void* u) -> int64_t {
            auto* fi = (file_io*)u;
            int when = (wh == 0) ? SEEK_SET : (wh == 1) ? SEEK_CUR : SEEK_END;
            if (::fseek(fi->f, (long)off, when) != 0) return -1;
            return (int64_t)::ftell(fi->f);
        };
        vio.read = [](void* dst, int64_t cnt, void* u) -> int64_t {
            auto* fi = (file_io*)u;
            return (int64_t)::fread(dst, 1, (size_t)cnt, fi->f);
        };
        vio.write = [](const void*, int64_t, void*) -> int64_t { return 0; };
        vio.tell = [](void* u) -> int64_t {
            return (int64_t)::ftell(((file_io*)u)->f);
        };
    }

    // ─── Fast MPEG Layer III header parser (no decode, just frame size) ───
    struct mp3_hdr_fast {
        uint32_t frame_size = 0;
        uint32_t samples_per_frame = 0;
        bool valid = false;
    };

    inline mp3_hdr_fast parse_mp3_hdr(const uint8_t* d, size_t n) noexcept {
        mp3_hdr_fast h{};
        if (n < 4 || d[0] != 0xFF || (d[1] & 0xE0) != 0xE0) return h;
        int ver_bits = (d[1] >> 3) & 3;
        int layer_bits = (d[1] >> 1) & 3;
        if (ver_bits == 1 || layer_bits != 1) return h;   // Layer III only
        int bitrate_idx = (d[2] >> 4) & 0xF;
        int sr_idx = (d[2] >> 2) & 3;
        if (bitrate_idx == 0 || bitrate_idx == 15 || sr_idx == 3) return h;
        static const uint16_t br_m1[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
        static const uint16_t br_m2[16] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
        static const uint32_t sr_m1[3] = {44100,48000,32000};
        int ver = (ver_bits == 3) ? 1 : (ver_bits == 2) ? 2 : 25;
        uint32_t sr = sr_m1[sr_idx];
        if (ver == 2) sr /= 2; else if (ver == 25) sr /= 4;
        uint32_t br = (ver == 1) ? br_m1[bitrate_idx] : br_m2[bitrate_idx];
        if (sr == 0 || br == 0) return h;
        bool padding = (d[2] & 0x02) != 0;
        uint32_t coef = (ver == 1) ? 144 : 72;
        h.frame_size = (coef * br * 1000) / sr + (padding ? 1 : 0);
        h.samples_per_frame = (ver == 1) ? 1152 : 576;
        h.valid = (h.frame_size >= 4);
        return h;
    }

    // ─── Parse Xing/Info/VBRI header for total duration ───
    inline uint64_t try_parse_xing(const uint8_t* frame, uint32_t avail, int channels) noexcept {
        if (avail < 4) return 0;
        int ver_bits = (frame[1] >> 3) & 3;
        if (ver_bits == 1) return 0;
        int is_mpeg1 = (ver_bits == 3);
        int is_mono = (channels == 1);
        int spf = is_mpeg1 ? 1152 : 576;
        uint32_t xing_off = is_mpeg1 ? (is_mono ? 21 : 36) : (is_mono ? 13 : 21);
        if (avail >= xing_off + 12) {
            const uint8_t* p = frame + xing_off;
            if (std::memcmp(p, "Xing", 4) == 0 || std::memcmp(p, "Info", 4) == 0) {
                uint32_t flags = adetail::be32(p + 4);
                if (flags & 1) {
                    uint32_t frames = adetail::be32(p + 8);
                    return (uint64_t)frames * spf;
                }
            }
        }
        if (avail >= 50 && std::memcmp(frame + 32, "VBRI", 4) == 0) {
            uint32_t frames = adetail::be32(frame + 46);
            return (uint64_t)frames * spf;
        }
        return 0;
    }
} // namespace detail_file_io

class decoder_stream {
public:
    decoder_stream() noexcept = default;
    ~decoder_stream() { close(); }
    decoder_stream(decoder_stream&& o) noexcept { *this = std::move(o); }
    decoder_stream& operator=(decoder_stream&& o) noexcept {
        if (this != &o) {
            close();
            c_ = o.c_; rate_ = o.rate_; channels_ = o.channels_;
            total_frames_ = o.total_frames_; pos_ = o.pos_; eof_ = o.eof_;
            file_ = o.file_; o.file_ = nullptr;
            mp3_dec_ = o.mp3_dec_;
            mp3_in_ = std::move(o.mp3_in_);
            mp3_in_pos_ = o.mp3_in_pos_; mp3_in_filled_ = o.mp3_in_filled_;
            mp3_pcm_ = std::move(o.mp3_pcm_);
            mp3_pcm_filled_ = o.mp3_pcm_filled_; mp3_pcm_pos_ = o.mp3_pcm_pos_;
            L_ = o.L_;
            wav_file_pos_ = o.wav_file_pos_;
            flac_fc_ = o.flac_fc_; o.flac_fc_ = nullptr;
            flac_pcm_ = std::move(o.flac_pcm_);
            flac_pcm_filled_ = o.flac_pcm_filled_; flac_pcm_pos_ = o.flac_pcm_pos_;
            flac_file_pos_ = o.flac_file_pos_; flac_init_done_ = o.flac_init_done_;
            sf_vio_ = o.sf_vio_; o.sf_vio_ = nullptr;
            sf_fio_ = o.sf_fio_; o.sf_fio_ = nullptr;
            sf_file_ = o.sf_file_; o.sf_file_ = nullptr;
        }
        return *this;
    }
    decoder_stream(const decoder_stream&) = delete;
    decoder_stream& operator=(const decoder_stream&) = delete;

    static result<decoder_stream> open(const knst_c16string& path) noexcept {
        using R = result<decoder_stream>;
        if (path.empty()) return R::fail(status::InvalidArgument);
    #if KNST_USING_PLATFORM_WINDOWS
        FILE* f = ::_wfopen(reinterpret_cast<const wchar_t*>(path.data()), L"rb");
    #else
        knst_byte_string u8(path);
        FILE* f = ::fopen(reinterpret_cast<const char*>(u8.data()), "rb");
    #endif
        if (!f) return R::fail(status::NotFound);
        uint8_t probe[4096];
        size_t n = ::fread(probe, 1, sizeof(probe), f);
        ::rewind(f);
        if (n < 4) { ::fclose(f); return R::fail(status::InvalidArgument); }
        container_t c = codec::detect(probe, n);
        if (c == container_t::unknown) c = codec::detect_extension(path);
        R r = R::fail(status::NotSupported);
        if      (c == container_t::mp3)                          r = open_mp3(f);
        else if (c == container_t::wav || c == container_t::rf64) r = open_wav(f);
        else if (c == container_t::aiff || c == container_t::aifc) r = open_aiff(f);
        else if (c == container_t::au)                            r = open_au(f);
        else if (c == container_t::flac)                          r = open_flac(f);
        else if (c != container_t::raw && c != container_t::unknown)
            r = open_sndfile(f, c);   // OGG/OPUS/AAC/WavPack/APE/TTA
        // Fallback: if native open failed and libsndfile is available,
        // try sndfile — handles AIFF "ima4", "MAC3", and other exotic codecs.
        if (!r && c != container_t::raw && c != container_t::unknown &&
            codec::sndfile::get().ok) {
            ::rewind(f);
            r = open_sndfile(f, c);
        }
        if (!r) { ::fclose(f); return r; }
        return r;
    }

    result<uint32_t> read(float* out, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!out || frames == 0) return R::fail(status::InvalidArgument);
        if (eof_ || !file_) return R::success(0u);
        if (sf_file_) return read_sndfile(out, frames);
        if (c_ == container_t::mp3) return read_mp3(out, frames);
        if (c_ == container_t::wav || c_ == container_t::rf64 ||
            c_ == container_t::aiff || c_ == container_t::aifc ||
            c_ == container_t::au) {
            if (L_.codec == codec_t::ima_adpcm || L_.codec == codec_t::ms_adpcm)
                return read_wav_adpcm(out, frames);
            return read_pcm_layout(out, frames);
        }
        if (c_ == container_t::flac) return read_flac(out, frames);
        return R::fail(status::NotSupported);
    }

    /// Seek to `target` (frame number). Returns actual frame reached.
    ///
    ///   MP3:    lazy-built frame index → seek to nearest frame → decode forward
    ///   WAV/AIFF/AU (PCM):    direct byte offset
    ///   WAV/AIFF (ADPCM):     nearest block boundary
    ///   FLAC:                 NotSupported (needs STREAMINFO seek table)
    ///   Sndfile:              NotSupported (needs sf_seek API)
    result<uint64_t> seek_frames(uint64_t target) noexcept {
        using R = result<uint64_t>;
        if (!file_) return R::fail(status::BadState);

        if (c_ == container_t::mp3) {
            if (!mp3_index_built_) {
                auto r = build_mp3_index();
                if (!r) return R::fail(r.st, r.native);
            }
            if (mp3_index_.empty()) return R::fail(status::BadState);
            // Binary search: closest frame <= target
            size_t lo = 0, hi = mp3_index_.size();
            while (lo < hi) {
                size_t mid = (lo + hi) / 2;
                if (mp3_index_[mid].sample_offset <= target) lo = mid + 1;
                else hi = mid;
            }
            size_t idx = lo > 0 ? lo - 1 : 0;
            auto& e = mp3_index_[idx];
            ::fseek(file_, (long)e.byte_offset, SEEK_SET);
            mp3dec_init(&mp3_dec_);
            mp3_in_pos_ = 0; mp3_in_filled_ = 0;
            mp3_in_.resize(0);
            mp3_pcm_filled_ = 0; mp3_pcm_pos_ = 0;
            pos_ = e.sample_offset;
            eof_ = false;
            // Decode forward to exact target
            uint64_t remaining = target - e.sample_offset;
            if (remaining > 0) {
                knst_vector<float> tmp;
                const uint32_t skip_chunk = 4096;
                if (!adetail::resize_ok(tmp, skip_chunk * channels_))
                    return R::fail(status::OutOfMemory);
                while (remaining > 0 && !eof_) {
                    uint32_t want = remaining < skip_chunk ? (uint32_t)remaining : skip_chunk;
                    auto r = read_mp3(tmp.data(), want);
                    if (!r || r.value == 0) break;
                    remaining -= r.value;
                }
            }
            return R::success(pos_);
        }

        if (c_ == container_t::wav || c_ == container_t::rf64 ||
            c_ == container_t::aiff || c_ == container_t::aifc ||
            c_ == container_t::au) {
            if (L_.codec == codec_t::ima_adpcm || L_.codec == codec_t::ms_adpcm) {
                uint32_t spb = L_.samples_per_block;
                if (spb == 0) spb = (L_.codec == codec_t::ima_adpcm)
                    ? (L_.block_align - 4u * channels_) * 8 / (4u * channels_) + 1
                    : (L_.block_align - 7u * channels_) * 2 / channels_ + 2;
                uint64_t target_block = target / spb;
                uint64_t block_off = target_block * L_.block_align;
                if (block_off >= L_.data_size) {
                    eof_ = true; pos_ = total_frames_;
                    return R::success(pos_);
                }
                wav_adpcm_block_idx_ = target_block;
                wav_adpcm_filled_ = 0; wav_adpcm_pos_ = 0;
                pos_ = target_block * spb;
                eof_ = false;
                return R::success(pos_);
            }
            uint32_t bps = sample_bytes(L_.fmt);
            uint64_t byte_off = L_.data_off + target * channels_ * bps;
            if (byte_off >= L_.data_off + L_.data_size) {
                eof_ = true; pos_ = total_frames_;
                return R::success(pos_);
            }
            ::fseek(file_, (long)byte_off, SEEK_SET);
            wav_file_pos_ = byte_off;
            pos_ = target;
            eof_ = false;
            return R::success(pos_);
        }
        return R::fail(status::NotSupported);
    }

    result<uint32_t> read(int16_t* out, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!out || frames == 0) return R::fail(status::InvalidArgument);
        knst_vector<float> tmp;
        if (!adetail::resize_ok(tmp, frames * channels_)) return R::fail(status::OutOfMemory);
        auto r = read(tmp.data(), frames);
        if (!r) return r;
        uint32_t total = r.value * channels_;
        for (uint32_t i = 0; i < total; ++i) {
            float v = tmp[i]; v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            long q = std::lrintf(v * 32768.0f);
            out[i] = (int16_t)(q > 32767 ? 32767 : q);
        }
        return r;
    }

    uint32_t rate()          const noexcept { return rate_; }
    uint32_t channels()      const noexcept { return channels_; }
    container_t container()  const noexcept { return c_; }
    uint64_t total_frames()  const noexcept { return total_frames_; }  // 0 = unknown
    double   duration()      const noexcept { return rate_ ? (double)total_frames_ / rate_ : 0.0; }
    uint64_t position()      const noexcept { return pos_; }
    bool     eof()           const noexcept { return eof_; }

    /// Streams entire file to sink. RAM stays bounded (~200 KB).
    /// Returns total frames played. `stop` polled between chunks for cancel.
    result<uint64_t> play_to(stream& sink, const std::atomic<bool>* stop = nullptr) noexcept {
        using R = result<uint64_t>;
        if (sink.channels() != channels_ || sink.rate() != rate_)
            return R::fail(status::InvalidArgument);
        const uint32_t chunk = 4096;
        knst_vector<float> buf;
        if (!adetail::resize_ok(buf, chunk * channels_)) return R::fail(status::OutOfMemory);
        uint64_t total = 0;
        while (!eof_) {
            if (stop && stop->load(std::memory_order_relaxed)) break;
            auto n = read(buf.data(), chunk);
            if (!n) return R::fail(n.st, n.native);
            if (n.value == 0) break;
            auto w = sink.write_interleaved(buf.data(), n.value);
            if (!w) return R::fail(w.st, w.native);
            total += n.value;
        }
        sink.drain();
        return R::success(total);
    }

    void close() noexcept {
        if (sf_file_) {
            auto& a = codec::sndfile::get();
            if (a.ok && a.close) a.close(sf_file_);
            sf_file_ = nullptr;
        }
        delete sf_vio_; sf_vio_ = nullptr;
        delete sf_fio_; sf_fio_ = nullptr;
        if (file_) { ::fclose(file_); file_ = nullptr; }
        if (flac_fc_) { delete flac_fc_; flac_fc_ = nullptr; }
        eof_ = true;
    }

private:
        // parse_wav / parse_aiff / parse_au clamp data_size to the size of the
    // header buffer they were given. For the streaming API we feed them an
    // 8 KB probe, so any file whose data chunk extends past 8 KB gets its
    // data_size clamped to ~8 KB. When that happens, re-read the declared
    // size from the file header and re-derive data_size against the real
    // file length.
    //
    // Detection: the clamp is visible as `data_off + data_size == probe_bytes`
    // (parse_wav set data_size to (probe_bytes - data_off)). If the real data
    // chunk fit inside the probe, that equality doesn't hold.
    static void fixup_stream_data_size(FILE* f, codec::layout& L, size_t probe_bytes) noexcept {
        if (!L.valid || L.data_off == 0) return;
        if (L.data_off + L.data_size != probe_bytes) return;   // not clamped

        long saved = ::ftell(f);
        if (saved < 0) return;

        uint64_t declared = 0;
        if (L.container == container_t::wav || L.container == container_t::rf64) {
            // WAV: data size is LE u32 stored 4 bytes before the audio data.
            if (L.data_off >= 4) {
                uint8_t b[4];
                if (::fseek(f, static_cast<long>(L.data_off - 4), SEEK_SET) == 0 &&
                    ::fread(b, 1, 4, f) == 4) {
                    declared = static_cast<uint64_t>(b[0]) |
                               (static_cast<uint64_t>(b[1]) << 8) |
                               (static_cast<uint64_t>(b[2]) << 16) |
                               (static_cast<uint64_t>(b[3]) << 24);
                    if (declared == 0xFFFFFFFFull) declared = 0;   // RF64 / streamed
                }
            }
        } else if (L.container == container_t::au) {
            // AU: data size is BE u32 at offset 8.
            uint8_t b[4];
            if (::fseek(f, 8, SEEK_SET) == 0 && ::fread(b, 1, 4, f) == 4) {
                declared = (static_cast<uint64_t>(b[0]) << 24) |
                           (static_cast<uint64_t>(b[1]) << 16) |
                           (static_cast<uint64_t>(b[2]) << 8) |
                            static_cast<uint64_t>(b[3]);
                if (declared == 0xFFFFFFFFull) declared = 0;      // unknown
            }
        }
        // AIFF/AIFC: SSND declared size is buried inside chunk headers;
        // using file_size as a fallback is good enough for a streaming reader.

        if (::fseek(f, 0, SEEK_END) != 0) { ::fseek(f, saved, SEEK_SET); return; }
        long fsz = ::ftell(f);
        ::fseek(f, saved, SEEK_SET);
        if (fsz <= 0) return;

        uint64_t avail = static_cast<uint64_t>(fsz) > L.data_off
                       ? static_cast<uint64_t>(fsz) - L.data_off : 0;
        // Trust the declared size when it's sane; otherwise use what's
        // actually on disk (handles truncated files and RF64 placeholders).
        L.data_size = (declared != 0 && declared <= avail) ? declared : avail;
    }

    static result<decoder_stream> open_mp3(FILE* f) noexcept {
        using R = result<decoder_stream>;
        decoder_stream ds;
        ds.file_ = f; ds.c_ = container_t::mp3;
        mp3dec_init(&ds.mp3_dec_);

        const uint32_t probe_sz = 65536;
        knst_vector<uint8_t> probe;
        if (!adetail::resize_ok(probe, probe_sz)) return R::fail(status::OutOfMemory);
        size_t n = ::fread(probe.data(), 1, probe_sz, f);
        if (n < 4) return R::fail(status::InvalidArgument);
        ::rewind(f);

                // Skip ID3 tag
        size_t id3_end = 0;
        if (n >= 10 && std::memcmp(probe.data(), "ID3", 3) == 0) {
            uint32_t sz = ((uint32_t)(probe[6] & 0x7F) << 21) | ((uint32_t)(probe[7] & 0x7F) << 14)
                        | ((uint32_t)(probe[8] & 0x7F) << 7) | (uint32_t)(probe[9] & 0x7F);
            id3_end = 10 + sz;
            if (probe[5] & 0x10) id3_end += 10;
        }

        // If ID3 tag is bigger than our probe buffer, we can't find the sync
        // within it. Re-read from the ID3 end.
        uint64_t file_base = 0;
        if (id3_end + 4 > n) {
            ::fseek(f, (long)id3_end, SEEK_SET);
            file_base = id3_end;
            size_t got = ::fread(probe.data(), 1, probe_sz, f);
            if (got < 4) return R::fail(status::InvalidArgument);
            n = got;
            id3_end = 0;
        }

        // Find first MP3 frame
        size_t sync = id3_end;
        detail_file_io::mp3_hdr_fast fh{};
        while (sync + 4 <= n) {
            fh = detail_file_io::parse_mp3_hdr(probe.data() + sync, n - sync);
            if (fh.valid) break;
            ++sync;
        }
        if (!fh.valid) return R::fail(status::InvalidArgument);

        // Set the file position to the first valid frame so read_mp3()
        // starts decoding at the right byte (not byte-skipping through ID3).
        uint64_t sync_file_pos = file_base + sync;
        ds.mp3_start_ = sync_file_pos;
        ::fseek(f, (long)sync_file_pos, SEEK_SET);

        // Parse header for rate/channels
        mp3dec_frame_info_t info{};
        mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int samples = mp3dec_decode_frame(&ds.mp3_dec_, probe.data() + sync, (int)(n - sync), pcm, &info);
        (void)samples;
        if (info.frame_bytes == 0 || info.hz == 0 || info.channels == 0)
            return R::fail(status::InvalidArgument);
        ds.rate_ = (uint32_t)info.hz;
        ds.channels_ = (uint32_t)info.channels;

        // Duration from Xing/Info/VBRI
        ds.total_frames_ = detail_file_io::try_parse_xing(
            probe.data() + sync, (uint32_t)(n - sync), (int)info.channels);

        mp3dec_init(&ds.mp3_dec_);   // reset probe state
        return R::success(std::move(ds));
    }

    static result<decoder_stream> open_wav(FILE* f) noexcept {
        using R = result<decoder_stream>;
        uint8_t hdr[8192];
        size_t n = ::fread(hdr, 1, sizeof(hdr), f);
        if (n < 12) return R::fail(status::InvalidArgument);
        codec::layout L = codec::parse_wav(hdr, n);
        if (!L.valid) return R::fail(status::InvalidArgument);
        fixup_stream_data_size(f, L, n);
        decoder_stream ds;
        ds.file_ = f; ds.c_ = L.container;
        ds.rate_ = L.rate; ds.channels_ = L.channels;
        ds.L_ = L;
        ds.total_frames_ = codec::layout_frames(L);
        ds.wav_file_pos_ = L.data_off;
        ::fseek(f, (long)L.data_off, SEEK_SET);
        return R::success(std::move(ds));
    }

    static result<decoder_stream> open_flac(FILE* f) noexcept {
        using R = result<decoder_stream>;
        uint8_t hdr[8192];
        size_t n = ::fread(hdr, 1, sizeof(hdr), f);
        if (n < 12) return R::fail(status::InvalidArgument);
        size_t pos = 0;
        if (n >= 10 && std::memcmp(hdr, "ID3", 3) == 0)
            pos = 10 + (((size_t)(hdr[6] & 0x7F) << 21) | ((size_t)(hdr[7] & 0x7F) << 14)
                     | ((size_t)(hdr[8] & 0x7F) << 7) | (hdr[9] & 0x7F));
        if (pos + 4 + 4 + 34 > n || std::memcmp(hdr + pos, "fLaC", 4) != 0)
            return R::fail(status::InvalidArgument);
        const uint8_t* si = hdr + pos + 8;
        uint64_t v = adetail::be64(si + 10);
        uint32_t sr  = (uint32_t)(v >> 44);
        uint32_t nch = (uint32_t)((v >> 41) & 7) + 1;
        uint32_t bps = (uint32_t)((v >> 36) & 31) + 1;
        uint64_t total = v & 0xFFFFFFFFFull;
        if (sr == 0 || nch == 0 || nch > 8 || bps < 4 || bps > 32)
            return R::fail(status::InvalidArgument);
        decoder_stream ds;
        ds.file_ = f; ds.c_ = container_t::flac;
        ds.rate_ = sr; ds.channels_ = nch;
        ds.total_frames_ = total;
        ds.flac_fc_ = new flacdec::frame_ctx();
        if (!ds.flac_fc_) return R::fail(status::OutOfMemory);
        ds.flac_fc_->sr_info = sr;
        ds.flac_fc_->bps_info = bps;
        // Skip metadata blocks. Album art (PICTURE block) can be several
        // hundred KB, so we must handle metadata larger than our probe buffer.
        // If we run out of buffered data, seek + read the next 4-byte header
        // directly from the file.
        size_t p = pos + 4;
        bool last = false;
        while (!last) {
            uint32_t len;
            if (p + 4 > n) {
                ::fseek(f, (long)p, SEEK_SET);
                uint8_t hdr4[4];
                if (::fread(hdr4, 1, 4, f) != 4) return R::fail(status::IoError);
                last = (hdr4[0] & 0x80) != 0;
                len  = ((uint32_t)hdr4[1] << 16) | ((uint32_t)hdr4[2] << 8) | hdr4[3];
            } else {
                last = (hdr[p] & 0x80) != 0;
                len  = ((uint32_t)hdr[p+1] << 16) | ((uint32_t)hdr[p+2] << 8) | hdr[p+3];
            }
            p += 4 + (size_t)len;
        }
        ds.flac_file_pos_ = p;
        ds.flac_init_done_ = true;
        ::fseek(f, (long)p, SEEK_SET);
        return R::success(std::move(ds));
    }

    static result<decoder_stream> open_aiff(FILE* f) noexcept {
        using R = result<decoder_stream>;
        uint8_t hdr[8192];
        size_t n = ::fread(hdr, 1, sizeof(hdr), f);
        if (n < 12) return R::fail(status::InvalidArgument);
        codec::layout L = codec::parse_aiff(hdr, n);
        if (!L.valid) return R::fail(status::InvalidArgument);
        fixup_stream_data_size(f, L, n);
        decoder_stream ds;
        ds.file_ = f; ds.c_ = L.container;
        ds.rate_ = L.rate; ds.channels_ = L.channels;
        ds.L_ = L;
        ds.total_frames_ = codec::layout_frames(L);
        ds.wav_file_pos_ = L.data_off;
        ::fseek(f, (long)L.data_off, SEEK_SET);
        return R::success(std::move(ds));
    }

    static result<decoder_stream> open_au(FILE* f) noexcept {
        using R = result<decoder_stream>;
        uint8_t hdr[8192];
        size_t n = ::fread(hdr, 1, sizeof(hdr), f);
        if (n < 24) return R::fail(status::InvalidArgument);
        codec::layout L = codec::parse_au(hdr, n);
        if (!L.valid) return R::fail(status::InvalidArgument);
        fixup_stream_data_size(f, L, n);
        decoder_stream ds;
        ds.file_ = f; ds.c_ = L.container;
        ds.rate_ = L.rate; ds.channels_ = L.channels;
        ds.L_ = L;
        ds.total_frames_ = codec::layout_frames(L);
        ds.wav_file_pos_ = L.data_off;
        ::fseek(f, (long)L.data_off, SEEK_SET);
        return R::success(std::move(ds));
    }

    static result<decoder_stream> open_sndfile(FILE* f, container_t hint) noexcept {
        using R = result<decoder_stream>;
        auto& a = codec::sndfile::get();
        if (!a.ok) return R::fail(status::NotSupported);

        // Determine file size (needed by SF_VIRTUAL_IO::get_filelen)
        if (::fseek(f, 0, SEEK_END) != 0) return R::fail(status::IoError);
        long sz = ::ftell(f);
        if (sz <= 0) return R::fail(status::InvalidArgument);
        ::rewind(f);

        // Heap-allocate: libsndfile stores the pointer, not a copy,
        // so these must survive both move-construction and decoder_stream lifetime.
        auto* fio = new detail_file_io::file_io{ f, (int64_t)sz };
        auto* vio = new codec::sndfile::SF_VIRTUAL_IO_c;
        detail_file_io::init_vio(*vio);

        codec::sndfile::SF_INFO_c info;
        std::memset(&info, 0, sizeof(info));
        void* sf = a.open_virtual(vio, 0x10 /* SFM_READ */, &info, fio);
        if (!sf) { delete vio; delete fio; return R::fail(status::NotSupported); }
        if (info.channels <= 0 || info.channels > 64 || info.samplerate <= 0) {
            a.close(sf); delete vio; delete fio;
            return R::fail(status::InvalidArgument);
        }

        decoder_stream ds;
        ds.file_ = f;
        ds.c_ = hint;
        ds.rate_ = (uint32_t)info.samplerate;
        ds.channels_ = (uint32_t)info.channels;
        ds.total_frames_ = info.frames > 0 ? (uint64_t)info.frames : 0;
        ds.sf_vio_ = vio;
        ds.sf_fio_ = fio;
        ds.sf_file_ = sf;
        return R::success(std::move(ds));
    }

    result<uint32_t> read_mp3(float* out, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        const uint32_t ch = channels_;
        uint32_t out_pos = 0;
        mp3dec_frame_info_t info{};
        mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        while (out_pos < frames) {
            if (mp3_pcm_pos_ < mp3_pcm_filled_) {
                uint32_t avail = (uint32_t)(mp3_pcm_filled_ - mp3_pcm_pos_);
                uint32_t take = frames - out_pos < avail ? frames - out_pos : avail;
                for (uint32_t f = 0; f < take; ++f)
                    for (uint32_t c = 0; c < ch; ++c)
                        out[(size_t)(out_pos + f) * ch + c] =
                            mp3_pcm_[(size_t)(mp3_pcm_pos_ + f) * ch + c] / 32768.0f;
                mp3_pcm_pos_ += take; out_pos += take;
                if (out_pos >= frames) break;
            }
            // Need more input? Compact + read
                        if (mp3_in_filled_ - mp3_in_pos_ < 4096) {
                size_t remaining = mp3_in_filled_ - mp3_in_pos_;
                if (remaining > 0 && mp3_in_pos_ > 0)
                    ::memmove(mp3_in_.data(), mp3_in_.data() + mp3_in_pos_, remaining);
                mp3_in_pos_ = 0; mp3_in_filled_ = remaining;
                const uint32_t chunk_size = 32768;
                if (!adetail::resize_ok(mp3_in_, (uint32_t)remaining + chunk_size))
                    return R::fail(status::OutOfMemory);
                size_t got = ::fread(mp3_in_.data() + remaining, 1, chunk_size, file_);
                if (got == 0 && remaining == 0) { eof_ = true; break; }
                mp3_in_filled_ = remaining + got;
                mp3_in_.resize((uint32_t)mp3_in_filled_);
            }
            int samples = mp3dec_decode_frame(&mp3_dec_,
                mp3_in_.data() + mp3_in_pos_,
                (int)(mp3_in_filled_ - mp3_in_pos_), pcm, &info);
            if (info.frame_bytes == 0) {
                if (mp3_in_pos_ >= mp3_in_filled_) { eof_ = true; break; }
                ++mp3_in_pos_;  // skip garbage byte, retry sync
                continue;
            }
            mp3_in_pos_ += info.frame_bytes;
            if (samples > 0 && (uint32_t)info.channels == ch) {
                uint32_t total = (uint32_t)samples * ch;
                if (!adetail::resize_ok(mp3_pcm_, total)) return R::fail(status::OutOfMemory);
                for (uint32_t i = 0; i < total; ++i) mp3_pcm_[i] = pcm[i];
                mp3_pcm_filled_ = (size_t)samples;
                mp3_pcm_pos_ = 0;
            }
        }
        pos_ += out_pos;
        return R::success(out_pos);
    }

    // Handles WAV/RF64/AIFF/AIFC/AU — all use the same layout struct.
    result<uint32_t> read_pcm_layout(float* out, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        uint32_t bps = sample_bytes(L_.fmt);
        uint64_t bytes_needed = (uint64_t)frames * channels_ * bps;
        uint64_t consumed = wav_file_pos_ - L_.data_off;
        if (consumed >= L_.data_size) { eof_ = true; return R::success(0u); }
        uint64_t bytes_left = L_.data_size - consumed;
        uint64_t bytes_to_read = bytes_needed < bytes_left ? bytes_needed : bytes_left;
        if (bytes_to_read == 0) { eof_ = true; return R::success(0u); }
        knst_vector<uint8_t> buf;
        if (!adetail::resize_ok(buf, (uint32_t)bytes_to_read)) return R::fail(status::OutOfMemory);
        size_t n = ::fread(buf.data(), 1, (size_t)bytes_to_read, file_);
        if (n == 0) { eof_ = true; return R::success(0u); }
        uint32_t actual = (uint32_t)(n / ((size_t)channels_ * bps));
        if (actual == 0) { eof_ = true; return R::success(0u); }
        adetail::decode_samples(buf.data(), (size_t)actual * channels_, L_.fmt, L_.big_endian, out);
        wav_file_pos_ += n;
        pos_ += actual;
        if (wav_file_pos_ - L_.data_off >= L_.data_size) eof_ = true;
        return R::success(actual);
    }

    // WAV/AIFF ADPCM (IMA, MS) — block-wise streaming.
    // Each block resets its own predictor state → perfectly streamable.
    result<uint32_t> read_wav_adpcm(float* out, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        const uint32_t ch = channels_;
        uint32_t out_pos = 0;
        while (out_pos < frames) {
            if (wav_adpcm_pos_ < wav_adpcm_filled_) {
                uint32_t avail = wav_adpcm_filled_ - wav_adpcm_pos_;
                uint32_t take = frames - out_pos < avail ? frames - out_pos : avail;
                for (uint32_t i = 0; i < take * ch; ++i)
                    out[(size_t)out_pos * ch + i] =
                        wav_adpcm_buf_[(size_t)wav_adpcm_pos_ * ch + i];
                wav_adpcm_pos_ += take;
                out_pos += take;
                if (out_pos >= frames) break;
            }
            uint64_t block_off = wav_adpcm_block_idx_ * L_.block_align;
            if (block_off >= L_.data_size) { eof_ = true; break; }
            uint64_t to_read = L_.block_align;
            if (block_off + to_read > L_.data_size) to_read = L_.data_size - block_off;
            if (to_read < 4 * ch) { eof_ = true; break; }
            knst_vector<uint8_t> block;
            if (!adetail::resize_ok(block, (uint32_t)to_read)) return R::fail(status::OutOfMemory);
            ::fseek(file_, (long)(L_.data_off + block_off), SEEK_SET);
            size_t n = ::fread(block.data(), 1, (size_t)to_read, file_);
            if (n == 0) { eof_ = true; break; }
            uint32_t spb = L_.samples_per_block;
            if (spb == 0) spb = (L_.codec == codec_t::ima_adpcm)
                ? (L_.block_align - 4u * ch) * 8 / (4u * ch) + 1
                : (L_.block_align - 7u * ch) * 2 / ch + 2;
            if (!adetail::resize_ok(wav_adpcm_buf_, spb * ch))
                return R::fail(status::OutOfMemory);
            uint32_t f = (L_.codec == codec_t::ima_adpcm)
                ? adpcm::ima_block(block.data(), n, ch, spb, wav_adpcm_buf_.data())
                : adpcm::ms_block(block.data(), n, ch, spb, L_.coefs, L_.num_coefs, wav_adpcm_buf_.data());
            if (f == 0) { eof_ = true; break; }
            wav_adpcm_filled_ = f;
            wav_adpcm_pos_ = 0;
            wav_adpcm_block_idx_++;
        }
        pos_ += out_pos;   // accumulate: pos_ must reflect frames actually delivered
        return R::success(out_pos);
    }

    result<uint32_t> read_flac(float* out, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        const uint32_t ch = channels_;
        uint32_t out_pos = 0;
        while (out_pos < frames) {
            if (flac_pcm_pos_ < flac_pcm_filled_) {
                uint32_t avail = (uint32_t)(flac_pcm_filled_ - flac_pcm_pos_);
                uint32_t take = frames - out_pos < avail ? frames - out_pos : avail;
                for (uint32_t i = 0; i < take * ch; ++i)
                    out[(size_t)out_pos * ch + i] = flac_pcm_[(size_t)flac_pcm_pos_ * ch + i];
                flac_pcm_pos_ += take; out_pos += take;
                if (out_pos >= frames) break;
            }
            // Read next frame raw bytes
            uint8_t hdr[8];
            size_t n = ::fread(hdr, 1, sizeof(hdr), file_);
            if (n < 6 || hdr[0] != 0xFF || (hdr[1] & 0xFE) != 0xF8) { eof_ = true; break; }
            // We already consumed 8 bytes; need the whole frame.
            // Read into a large buffer (max FLAC frame ~ 16KB typically, 64KB safe)
            knst_vector<uint8_t> buf;
            if (!adetail::resize_ok(buf, 65536)) return R::fail(status::OutOfMemory);
            ::memcpy(buf.data(), hdr, 8);
            // Read until we can determine frame size (or use br subframe decoding which reads via bitreader)
            // Simplest: read a big chunk and let `frame()` determine adv
            size_t extra = ::fread(buf.data() + 8, 1, 65536 - 8, file_);
            if (extra == 0) { eof_ = true; break; }
            uint32_t fr, fch, fbps, fbs;
            size_t adv = 0;
            if (!flacdec::frame(buf.data(), 8 + extra, 0, *flac_fc_, fr, fch, fbps, fbs, adv)
                || fch != ch) {
                if (out_pos == 0) { eof_ = true; }
                break;
            }
            // Rewind file by whatever remains after adv
            long back = (long)((8 + extra) - adv);
            if (back > 0) ::fseek(file_, -back, SEEK_CUR);
            flac_file_pos_ += adv;
            // Convert int64 → float interleaved
            if (!adetail::resize_ok(flac_pcm_, fbs * ch)) return R::fail(status::OutOfMemory);
            const double inv = 1.0 / (double)(1ull << (fbps - 1));
            for (uint32_t c = 0; c < ch; ++c) {
                const int64_t* s = flac_fc_->ch[c].data();
                for (uint32_t i = 0; i < fbs; ++i)
                    flac_pcm_[(size_t)i * ch + c] = (float)(s[i] * inv);
            }
            flac_pcm_filled_ = fbs;
            flac_pcm_pos_ = 0;
        }
        pos_ += out_pos;
        return R::success(out_pos);
    }

    result<uint32_t> read_sndfile(float* out, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!sf_file_) return R::fail(status::BadState);
        auto& a = codec::sndfile::get();
        if (!a.ok) return R::fail(status::NotSupported);
        int64_t got = a.readf_float(sf_file_, out, (int64_t)frames);
        if (got < 0) { eof_ = true; return R::fail(status::IoError, (int)got); }
        if (got == 0) { eof_ = true; return R::success(0u); }
        pos_ += (uint64_t)got;
        return R::success((uint32_t)got);
    }

    container_t c_ = container_t::unknown;
    uint32_t    rate_ = 0, channels_ = 0;
    uint64_t    total_frames_ = 0, pos_ = 0;
    bool        eof_ = false;
    FILE*       file_ = nullptr;

    mp3dec_t mp3_dec_{};
    knst_vector<uint8_t> mp3_in_;
    size_t mp3_in_pos_ = 0, mp3_in_filled_ = 0;
    knst_vector<int16_t> mp3_pcm_;
    size_t mp3_pcm_filled_ = 0, mp3_pcm_pos_ = 0;

    codec::layout L_;
    uint64_t wav_file_pos_ = 0;

    flacdec::frame_ctx* flac_fc_ = nullptr;
    knst_vector<float> flac_pcm_;
    size_t flac_pcm_filled_ = 0, flac_pcm_pos_ = 0;
    uint64_t flac_file_pos_ = 0;
    bool flac_init_done_ = false;

    // libsndfile streaming (OGG/OPUS/AAC/WavPack/APE/TTA).
    // Heap-allocated because libsndfile keeps the pointer, not a copy;
    // both must survive move-construction and decoder_stream lifetime.
    codec::sndfile::SF_VIRTUAL_IO_c* sf_vio_ = nullptr;
    detail_file_io::file_io* sf_fio_ = nullptr;
    void* sf_file_ = nullptr;

    // MP3 seek index — built lazily on first seek_frames().
    // Each entry: 16 bytes × up to 2M entries = 32 MB max.
    struct mp3_index_entry {
        uint64_t byte_offset;
        uint64_t sample_offset;
    };
    knst_vector<mp3_index_entry> mp3_index_;
    bool mp3_index_built_ = false;
    uint64_t mp3_start_ = 0;

    // WAV/AIFF ADPCM streaming state
    uint64_t wav_adpcm_block_idx_ = 0;
    knst_vector<float> wav_adpcm_buf_;
    uint32_t wav_adpcm_filled_ = 0;
    uint32_t wav_adpcm_pos_ = 0;

    result<void> build_mp3_index() noexcept {
        using R = result<void>;
        if (mp3_index_built_) return R::success();
        long saved = ::ftell(file_);
        if (saved < 0) return R::fail(status::IoError);
        ::fseek(file_, 0, SEEK_END);
        long sz = ::ftell(file_);
        if (sz <= 0) { ::fseek(file_, saved, SEEK_SET); return R::fail(status::IoError); }
        uint64_t file_sz = (uint64_t)sz;

        mp3_index_.clear();
        mp3_index_.reserve(8192);

        const uint32_t kChunk = 262144;
        knst_vector<uint8_t> buf;
        if (!adetail::resize_ok(buf, kChunk)) return R::fail(status::OutOfMemory);

        ::fseek(file_, (long)mp3_start_, SEEK_SET);
        size_t buf_size = ::fread(buf.data(), 1, kChunk, file_);
        size_t buf_off = 0;
        uint64_t byte_pos = mp3_start_;
        uint64_t sample_pos = 0;

        while (buf_off + 4 <= buf_size) {
            auto fh = detail_file_io::parse_mp3_hdr(buf.data() + buf_off, buf_size - buf_off);
            if (!fh.valid) { ++buf_off; ++byte_pos; continue; }
            if (byte_pos + fh.frame_size > file_sz) break;
            mp3_index_entry e{byte_pos, sample_pos};
            mp3_index_.push_back(e);
            sample_pos += fh.samples_per_frame;
            buf_off += fh.frame_size;
            byte_pos += fh.frame_size;
            if (mp3_index_.size() >= 2000000) break;
                        if (buf_off + 4096 > buf_size && buf_size == kChunk) {
                size_t rem = buf_size - buf_off;
                if (rem > 0) ::memmove(buf.data(), buf.data() + buf_off, rem);
                // After memmove, buf[0..rem) holds file positions
                // [byte_pos, byte_pos + rem). New data starts at byte_pos + rem.
                // Seeking to `byte_pos` would duplicate the first `rem` bytes.
                ::fseek(file_, (long)(byte_pos + rem), SEEK_SET);
                size_t got = ::fread(buf.data() + rem, 1, kChunk - rem, file_);
                buf_size = rem + got;
                buf_off = 0;
                if (got == 0 && rem < 4) break;
            }
        }
        ::fseek(file_, saved, SEEK_SET);
        if (mp3_index_.empty()) return R::fail(status::InvalidArgument);
        mp3_index_built_ = true;
        if (total_frames_ == 0) total_frames_ = sample_pos;
        return R::success();
    }
};
// ============================================================
// player — decoded audio with seek / loop / gain
// ============================================================

class player {
public:
    player() noexcept = default;

    static result<player> from_buffer(pcm_buffer&& b) noexcept {
        using R = result<player>;
        if (!b.valid()) return R::fail(status::InvalidArgument);
        player p; p.buf_ = std::move(b);
        return R::success(std::move(p));
    }
    /// Any supported format (see decode()).
    static result<player> open(const knst_c16string& path, const decode_options& o = decode_options()) noexcept {
        using R = result<player>;
        auto r = decode_file(path, o);
        if (!r) return R::fail(r.st, r.native);
        return from_buffer(std::move(r.value));
    }
    static result<player> open(const uint8_t* data, uint32_t size, const decode_options& o = decode_options()) noexcept {
        using R = result<player>;
        auto r = decode(data, size, o);
        if (!r) return R::fail(r.st, r.native);
        return from_buffer(std::move(r.value));
    }

    uint32_t channels()       const noexcept { return buf_.channels; }
    uint32_t sample_rate()    const noexcept { return buf_.rate; }
    uint32_t total_frames()   const noexcept { return buf_.frames(); }
    double   duration_seconds() const noexcept { return buf_.duration(); }
    const pcm_buffer& buffer() const noexcept { return buf_; }

    uint32_t position_frames() const noexcept { return position_; }
    double   position_seconds() const noexcept { return buf_.rate ? (double)position_ / buf_.rate : 0.0; }
    bool     eof() const noexcept { return position_ >= buf_.frames(); }

    void seek_frames(uint32_t f) noexcept { uint32_t t = buf_.frames(); position_ = f > t ? t : f; }
    void seek_seconds(double s) noexcept { if (s < 0) s = 0; double f = s * buf_.rate; seek_frames(f >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)f); }
    void rewind() noexcept { position_ = 0; }

    void  set_loop(bool l) noexcept { loop_ = l; }
    bool  loop() const noexcept { return loop_; }
    void  set_gain(float g) noexcept { gain_ = g < 0 ? 0 : g; }
    float gain() const noexcept { return gain_; }

    /// Reads up to `frames` frames (fills completely when looping). Returns frames read (0 = end).
    result<uint32_t> read_interleaved(float* buffer, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!buffer || frames == 0) return R::fail(status::InvalidArgument);
        const uint32_t total = buf_.frames(), ch = buf_.channels;
        if (total == 0) return R::success(0u);
        uint32_t got = 0;
        while (got < frames) {
            if (position_ >= total) { if (!loop_) break; position_ = 0; }
            uint32_t take = frames - got < total - position_ ? frames - got : total - position_;
            const float* src = buf_.samples.data() + (size_t)position_ * ch;
            float* dst = buffer + (size_t)got * ch;
            if (gain_ == 1.0f) std::memcpy(dst, src, (size_t)take * ch * sizeof(float));
            else for (size_t i = 0; i < (size_t)take * ch; ++i) dst[i] = src[i] * gain_;
            position_ += take; got += take;
        }
        return R::success(got);
    }

    result<uint32_t> read_interleaved(int16_t* buffer, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!buffer || frames == 0) return R::fail(status::InvalidArgument);
        knst_vector<float> tmp;
        if (!adetail::resize_ok(tmp, frames * buf_.channels)) return R::fail(status::OutOfMemory);
        auto r = read_interleaved(tmp.data(), frames);
        if (!r) return r;
        uint32_t n = r.value * buf_.channels;
        for (uint32_t i = 0; i < n; ++i) {
            float v = tmp[i]; v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            long q = std::lrintf(v * 32768.0f); buffer[i] = (int16_t)(q > 32767 ? 32767 : q);
        }
        return r;
    }

    /// Pulls `frames` frames and writes them to a stream (must have matching channels).
    result<uint32_t> play_frames(stream& sink, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (sink.channels() != buf_.channels) return R::fail(status::InvalidArgument);
        knst_vector<float> tmp;
        if (!adetail::resize_ok(tmp, frames * buf_.channels)) return R::fail(status::OutOfMemory);
        auto r = read_interleaved(tmp.data(), frames);
        if (!r || r.value == 0) return r;
        auto w = sink.write_interleaved(tmp.data(), r.value);
        if (!w) return R::fail(w.st, w.native);
        return r;
    }

    /// New player at another sample rate (windowed-sinc).
    result<player> resampled(uint32_t rate, dsp::resample_quality q = dsp::resample_quality::high) const noexcept {
        using R = result<player>;
        auto b = resample_buffer(buf_, rate, q);
        if (!b) return R::fail(b.st, b.native);
        return from_buffer(std::move(b.value));
    }

    const wav::info info() const noexcept {
        wav::info i; i.fmt.rate = buf_.rate; i.fmt.channels = buf_.channels; i.frame_count = buf_.frames(); i.valid = buf_.valid();
        return i;
    }

private:
    pcm_buffer buf_;
    uint32_t   position_ = 0;
    bool       loop_     = false;
    float      gain_     = 1.0f;
};

// ============================================================
// mixer — N sources → one bus (render is allocation-free)
// ============================================================

class mixer {
public:
    static constexpr int32_t  kMaxSources = 32;
    static constexpr uint32_t kChunk      = 1024;

    mixer(uint32_t rate, uint32_t channels) noexcept : rate_(rate), ch_(channels) {
        if (rate_ && ch_) adetail::resize_ok(tmp_, kChunk * ch_);
    }

    /// Adds a clip; it is converted to the mixer's rate/channels up-front. Returns slot id or -1.
    int32_t add(const pcm_buffer& clip, float gain = 1.0f, float pan = 0.0f, bool loop = false) noexcept {
        if (!clip.valid() || !rate_ || !ch_) return -1;
        int32_t id = -1;
        for (int32_t i = 0; i < kMaxSources; ++i) if (!slot_[i].used) { id = i; break; }
        if (id < 0) return -1;
        pcm_buffer b;
        b.rate = clip.rate; b.channels = clip.channels; b.samples = clip.samples;
        if (b.channels != ch_) { auto r = convert_buffer_channels(b, ch_); if (!r) return -1; b = std::move(r.value); }
        if (b.rate != rate_)   { auto r = resample_buffer(b, rate_);       if (!r) return -1; b = std::move(r.value); }
        auto p = player::from_buffer(std::move(b));
        if (!p) return -1;
        slot_[id].p = std::move(p.value); slot_[id].p.set_loop(loop);
        slot_[id].gain = gain; slot_[id].pan = pan; slot_[id].paused = false; slot_[id].used = true;
        return id;
    }
    void remove(int32_t id)               noexcept { if (ok(id)) { slot_[id].used = false; slot_[id].p = player(); } }
    void set_gain(int32_t id, float g)    noexcept { if (ok(id)) slot_[id].gain = g; }
    void set_pan(int32_t id, float p)     noexcept { if (ok(id)) slot_[id].pan = p; }
    void pause(int32_t id, bool paused)   noexcept { if (ok(id)) slot_[id].paused = paused; }
    bool is_playing(int32_t id)     const noexcept { return ok(id) && !slot_[id].paused; }
    void set_master(float g)              noexcept { master_ = g < 0 ? 0 : g; }
    void clear()                          noexcept { for (int32_t i = 0; i < kMaxSources; ++i) remove(i); }
    uint32_t active_count()         const noexcept { uint32_t n = 0; for (auto& s : slot_) if (s.used) ++n; return n; }
    uint32_t rate() const noexcept { return rate_; }
    uint32_t channels() const noexcept { return ch_; }

    /// Mixes `frames` frames into `out` (overwritten, hard-clipped to ±1). Finished one-shots are freed.
    result<uint32_t> render(float* out, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!out || frames == 0 || tmp_.size() < kChunk * ch_) return R::fail(status::InvalidArgument);
        std::memset(out, 0, (size_t)frames * ch_ * sizeof(float));
        for (int32_t id = 0; id < kMaxSources; ++id) {
            slot& s = slot_[id];
            if (!s.used || s.paused) continue;
            float gl = s.gain, gr = s.gain;
            if (ch_ == 2 && s.pan != 0.0f) {
                float pn = s.pan < -1.0f ? -1.0f : (s.pan > 1.0f ? 1.0f : s.pan), th = (pn + 1.0f) * (float)(M_PI / 4);
                gl *= std::cos(th) * 1.41421356f; gr *= std::sin(th) * 1.41421356f;     // unity at centre
            }
            uint32_t done = 0;
            while (done < frames) {
                uint32_t want = frames - done < kChunk ? frames - done : kChunk;
                auto r = s.p.read_interleaved(tmp_.data(), want);
                if (!r || r.value == 0) { remove(id); break; }
                float* d = out + (size_t)done * ch_;
                if (ch_ == 2) for (uint32_t i = 0; i < r.value; ++i) { d[i * 2] += tmp_[i * 2] * gl; d[i * 2 + 1] += tmp_[i * 2 + 1] * gr; }
                else dsp::mix_into(d, tmp_.data(), r.value * ch_, s.gain);
                done += r.value;
                if (r.value < want) { remove(id); break; }
            }
        }
        for (uint32_t i = 0; i < frames * ch_; ++i) { float v = out[i] * master_; out[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }
        return R::success(frames);
    }

private:
    struct slot { player p; float gain = 1.0f, pan = 0.0f; bool paused = false, used = false; };
    bool ok(int32_t id) const noexcept { return id >= 0 && id < kMaxSources && slot_[id].used; }
    slot slot_[kMaxSources];
    knst_vector<float> tmp_;
    uint32_t rate_ = 0, ch_ = 0;
    float master_ = 1.0f;
};

// ============================================================
// Platform implementations of audio class
// ============================================================

#if defined(KNST_USING_PLATFORM_ANDROID)
namespace detail_android_audio {
    inline jobject mgr(JNIEnv* env) noexcept {
        if (!env) return nullptr;
        jobject activity = knst_devices::detail_android::g_activity_obj;
        if (!activity) return nullptr;
        jclass ctx = env->GetObjectClass(activity);
        jmethodID m = env->GetMethodID(ctx, "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
        if (!m) { env->DeleteLocalRef(ctx); return nullptr; }
        jstring nm = env->NewStringUTF("audio");
        jobject svc = env->CallObjectMethod(activity, m, nm);
        env->DeleteLocalRef(nm);
        env->DeleteLocalRef(ctx);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
        return svc;
    }
    inline int32_t get_music_vol() noexcept {
        JNIEnv* env = knst_devices::detail_android::get_env();
        jobject a = mgr(env);
        if (!a) return -1;
        jclass c = env->GetObjectClass(a);
        jmethodID g = env->GetMethodID(c, "getStreamVolume", "(I)I");
        jmethodID x = env->GetMethodID(c, "getStreamMaxVolume", "(I)I");
        int32_t v = -1, mx = -1;
        if (g && x) { v = env->CallIntMethod(a, g, 3); mx = env->CallIntMethod(a, x, 3); }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(c); env->DeleteLocalRef(a);
        if (mx > 0 && v >= 0) return (v * 100) / mx;
        return -1;
    }
    inline bool set_music_vol(int32_t pct) noexcept {
        JNIEnv* env = knst_devices::detail_android::get_env();
        jobject a = mgr(env);
        if (!a) return false;
        jclass c = env->GetObjectClass(a);
        jmethodID x = env->GetMethodID(c, "getStreamMaxVolume", "(I)I");
        jmethodID s = env->GetMethodID(c, "setStreamVolume", "(III)V");
        bool ok = false;
        if (x && s) {
            int32_t mx = env->CallIntMethod(a, x, 3);
            if (mx > 0) {
                int32_t v = (pct * mx) / 100;
                env->CallVoidMethod(a, s, 3, v, 0);
                ok = !env->ExceptionCheck();
            }
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(c); env->DeleteLocalRef(a);
        return ok;
    }
    inline bool is_music_mute() noexcept {
        JNIEnv* env = knst_devices::detail_android::get_env();
        jobject a = mgr(env);
        if (!a) return false;
        jclass c = env->GetObjectClass(a);
        jmethodID m = env->GetMethodID(c, "isStreamMute", "(I)Z");
        bool v = false;
        if (m) v = env->CallBooleanMethod(a, m, 3);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(c); env->DeleteLocalRef(a);
        return v;
    }
        inline bool set_music_mute(bool mute) noexcept {
        JNIEnv* env = knst_devices::detail_android::get_env();
        jobject a = mgr(env);
        if (!a) return false;
        jclass c = env->GetObjectClass(a);
        jmethodID adj = env->GetMethodID(c, "adjustStreamVolume", "(III)V");
        bool ok = false;
        if (adj) {
            int dir = mute ? -100 : 100;
            env->CallVoidMethod(a, adj, 3, dir, 1);
            ok = !env->ExceptionCheck();
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(c); env->DeleteLocalRef(a);
        return ok;
    }

    // ── AudioFocus (via KnstAudioFocus.java helper) ─────────────────
    inline jclass load_audio_focus_helper(JNIEnv* e) noexcept {
        if (!e) return nullptr;
        jobject activity = knst_devices::detail_android::g_activity_obj;
        if (!activity) return nullptr;
        jclass act_cls = e->GetObjectClass(activity);
        jclass helper = nullptr;
        if (act_cls) {
            jmethodID get_cl = e->GetMethodID(act_cls, "getClassLoader",
                                               "()Ljava/lang/ClassLoader;");
            if (get_cl) {
                jobject cl = e->CallObjectMethod(activity, get_cl);
                if (cl) {
                    jclass cl_cls = e->GetObjectClass(cl);
                    jmethodID load = e->GetMethodID(cl_cls, "loadClass",
                                                     "(Ljava/lang/String;)Ljava/lang/Class;");
                    if (load) {
                        jstring jn = e->NewStringUTF("com.knst.print.KnstAudioFocus");
                        helper = (jclass)e->CallObjectMethod(cl, load, jn);
                        e->DeleteLocalRef(jn);
                    }
                    e->DeleteLocalRef(cl_cls); e->DeleteLocalRef(cl);
                }
            }
            e->DeleteLocalRef(act_cls);
        }
        if (e->ExceptionCheck()) { e->ExceptionClear(); return nullptr; }
        return helper;
    }

    inline bool request_audio_focus() noexcept {
        JNIEnv* e = knst_devices::detail_android::get_env();
        jclass h = load_audio_focus_helper(e);
        if (!h) return false;
        jmethodID m = e->GetStaticMethodID(h, "request", "()Z");
        bool ok = m && e->CallStaticBooleanMethod(h, m);
        if (e->ExceptionCheck()) { e->ExceptionClear(); ok = false; }
        e->DeleteLocalRef(h);
        return ok;
    }

    inline void abandon_audio_focus() noexcept {
        JNIEnv* e = knst_devices::detail_android::get_env();
        jclass h = load_audio_focus_helper(e);
        if (!h) return;
        jmethodID m = e->GetStaticMethodID(h, "abandon", "()V");
        if (m) e->CallStaticVoidMethod(h, m);
        if (e->ExceptionCheck()) e->ExceptionClear();
        e->DeleteLocalRef(h);
    }
} // namespace detail_android_audio
#endif

#if defined(KNST_USING_PLATFORM_ANDROID)

class audio {
public:
    static bool is_running() noexcept { return true; }

    static result<knst_vector<knst_audio_device>> list() noexcept {
        using R = result<knst_vector<knst_audio_device>>;
        knst_audio_info a = knst_devices::audio();
        if (!a.valid) return R::fail(status::NotFound);
        return R::success(std::move(a.devices));
    }
    static result<knst_vector<knst_audio_device>> list_sinks() noexcept {
        using R = result<knst_vector<knst_audio_device>>;
        auto all = list();
        if (!all) return R::fail(all.st, all.native);
        knst_vector<knst_audio_device> out;
        for (auto& d : all.value) if (!d.is_input) out.push_back(std::move(d));
        return R::success(std::move(out));
    }
    static result<knst_vector<knst_audio_device>> list_sources() noexcept {
        using R = result<knst_vector<knst_audio_device>>;
        auto all = list();
        if (!all) return R::fail(all.st, all.native);
        knst_vector<knst_audio_device> out;
        for (auto& d : all.value) if (d.is_input) out.push_back(std::move(d));
        return R::success(std::move(out));
    }

    static result<uint32_t> get_volume(const knst_audio_device&) noexcept {
        using R = result<uint32_t>;
        int32_t v = detail_android_audio::get_music_vol();
        if (v < 0) return R::fail(status::NotSupported);
        return R::success((uint32_t)v);
    }
    static result<void> set_volume(const knst_audio_device&, uint32_t percent) noexcept {
        if (percent > 100) return result<void>::fail(status::InvalidArgument);
        return detail_android_audio::set_music_vol((int32_t)percent)
            ? result<void>::success() : result<void>::fail(status::NotSupported);
    }
    static result<double> get_volume_db(const knst_audio_device& d) noexcept {
        auto v = get_volume(d);
        if (!v) return result<double>::fail(v.st, v.native);
        return result<double>::success(20.0 * std::log10((v.value > 0 ? v.value : 1) / 100.0));
    }
    static result<void> set_volume_db(const knst_audio_device& d, double db) noexcept {
        if (db < -120.0 || db > 0.0) return result<void>::fail(status::InvalidArgument);
        uint32_t pct = (uint32_t)(std::pow(10.0, db / 20.0) * 100.0 + 0.5);
        if (pct > 100) pct = 100;
        return set_volume(d, pct);
    }
        static result<uint32_t> get_channel_count(const knst_audio_device&) noexcept {
        using R = result<uint32_t>;
        JNIEnv* env = knst_devices::detail_android::get_env();
        if (!env) return R::fail(status::BadState);
        jobject am = detail_android_audio::mgr(env);
        if (!am) return R::fail(status::BadState);
        jclass am_cls = env->GetObjectClass(am);
        jmethodID get_devices = env->GetMethodID(am_cls, "getDevices",
            "(I)[Landroid/media/AudioDeviceInfo;");
        uint32_t result_ch = 2;
        if (get_devices) {
            // AudioManager.GET_DEVICES_OUTPUTS = 2
            jobjectArray devices = (jobjectArray)env->CallObjectMethod(am, get_devices, 2);
            if (devices) {
                jsize n = env->GetArrayLength(devices);
                if (n > 0) {
                    jobject dev0 = env->GetObjectArrayElement(devices, 0);
                    if (dev0) {
                                                jclass dc = env->GetObjectClass(dev0);
                        jmethodID gc = env->GetMethodID(dc, "getChannelCounts", "()[I");
                        if (gc) {
                            jintArray chs = (jintArray)env->CallObjectMethod(dev0, gc);
                            if (chs) {
                                jsize cl = env->GetArrayLength(chs);
                                if (cl > 0) {
                                    // getChannelCounts() returns an array of *supported*
                                    // channel counts (e.g. {1, 2, 6, 8}). The max element
                                    // is the actual maximum — NOT the array length.
                                    jint* arr = env->GetIntArrayElements(chs, nullptr);
                                    if (arr) {
                                        uint32_t max_ch = 0;
                                        for (jsize k = 0; k < cl; ++k)
                                            if ((uint32_t)arr[k] > max_ch) max_ch = (uint32_t)arr[k];
                                        if (max_ch > 0) result_ch = max_ch;
                                        env->ReleaseIntArrayElements(chs, arr, JNI_ABORT);
                                    }
                                }
                                env->DeleteLocalRef(chs);
                            }
                        }
                        env->DeleteLocalRef(dc); env->DeleteLocalRef(dev0);
                    }
                }
                env->DeleteLocalRef(devices);
            }
        }
        if (env->ExceptionCheck()) { env->ExceptionClear(); }
        env->DeleteLocalRef(am_cls); env->DeleteLocalRef(am);
        return R::success(result_ch);
    }
    static result<uint32_t> get_channel_volume(const knst_audio_device& d, uint32_t) noexcept {
        return get_volume(d);
    }
    static result<void> set_channel_volume(const knst_audio_device& d, uint32_t, uint32_t pct) noexcept {
        return set_volume(d, pct);
    }

    static result<bool> is_muted(const knst_audio_device&) noexcept {
        return result<bool>::success(detail_android_audio::is_music_mute());
    }
    static result<void> set_mute(const knst_audio_device&, bool mute) noexcept {
        return detail_android_audio::set_music_mute(mute)
            ? result<void>::success() : result<void>::fail(status::NotSupported);
    }

    static result<void> set_default(const knst_audio_device&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<knst_audio_device> get_default(bool is_input) noexcept {
        using R = result<knst_audio_device>;
        auto srcs = is_input ? list_sources() : list_sinks();
        if (!srcs) return R::fail(srcs.st, srcs.native);
        if (srcs.value.empty()) return R::fail(status::NotFound);
        return R::success(std::move(srcs.value[0]));
    }

    static result<void> play_file(const knst_audio_device* device, const knst_c16string& path,
                                   uint32_t = 600000) noexcept {
        using R = result<void>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        // AAudio yalnızca default cihazı destekler — non-default reddedilir
        if (device) return R::fail(status::NotSupported);
        return play_file_any(nullptr, path, nullptr);
    }

    static result<void> play_raw(const knst_audio_device* device,
                                  const knst_byte_string& pcm,
                                  const play_options& opt = play_options(),
                                  uint32_t = 600000) noexcept {
        using R = result<void>;
        if (pcm.empty() || opt.bits_per_sample != 16) return R::fail(status::InvalidArgument);
        if (device && device->is_input) return R::fail(status::InvalidArgument);
        if (device) return R::fail(status::NotSupported);   // AAudio: default only
        stream::config c;
        c.rate = opt.rate; c.channels = opt.channels; c.is_output = true;
        auto s = stream::open_default(c);
        if (!s) return R::fail(s.st, s.native);
        const uint32_t total_frames = (uint32_t)pcm.length() / (opt.channels * 2);
        const uint32_t chunk = 2048;
        knst_vector<float> tmp;
        if (!adetail::resize_ok(tmp, chunk * opt.channels)) return R::fail(status::OutOfMemory);
        const uint8_t* src = (const uint8_t*)pcm.data();
        for (uint32_t pos = 0; pos < total_frames; pos += chunk) {
            uint32_t n = total_frames - pos < chunk ? total_frames - pos : chunk;
            for (uint32_t i = 0; i < n * opt.channels; ++i) {
                size_t off = ((size_t)pos * opt.channels + i) * 2;
                int16_t v = (int16_t)(src[off] | (src[off + 1] << 8));
                tmp[i] = v / 32768.0f;
            }
            auto w = s.value.write_interleaved(tmp.data(), n);
            if (!w) return R::fail(w.st, w.native);
        }
        s.value.drain();
        return R::success();
    }
    static result<knst_vector<stream_info>> list_sink_inputs() noexcept { return result<knst_vector<stream_info>>::fail(status::NotSupported); }
    static result<knst_vector<stream_info>> list_source_outputs() noexcept { return result<knst_vector<stream_info>>::fail(status::NotSupported); }
    static result<knst_vector<stream_info>> list_streams() noexcept { return result<knst_vector<stream_info>>::fail(status::NotSupported); }
    static result<void> set_stream_volume(uint32_t, uint32_t, bool = true) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> set_stream_mute(uint32_t, bool, bool = true) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> move_stream(uint32_t, uint32_t, bool = true) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> cork_stream(uint32_t, bool, bool = true) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> suspend(const knst_audio_device&, bool) noexcept { return result<void>::fail(status::NotSupported); }
    static result<knst_audio_device> get_device_detail(const knst_audio_device& d) noexcept {
        using R = result<knst_audio_device>;
        knst_audio_device out = d;
        auto v = get_volume(d); if (v) out.volume_percent = v.value;
        auto m = is_muted(d);   if (m) out.muted = m.value;
        return R::success(std::move(out));
    }
        static result<device_format> get_format(const knst_audio_device&) noexcept {
        using R = result<device_format>;
        JNIEnv* env = knst_devices::detail_android::get_env();
        jobject am = detail_android_audio::mgr(env);
        device_format fmt;
        fmt.channels = 2; fmt.bits_per_sample = 16; fmt.rate = 48000;
        if (am) {
            jclass am_cls = env->GetObjectClass(am);
            jmethodID gp = env->GetMethodID(am_cls, "getProperty",
                "(Ljava/lang/String;)Ljava/lang/String;");
            if (gp) {
                jstring key = env->NewStringUTF("android.media.property.OUTPUT_SAMPLE_RATE");
                jstring val = (jstring)env->CallObjectMethod(am, gp, key);
                env->DeleteLocalRef(key);
                if (val) {
                    const char* s = env->GetStringUTFChars(val, nullptr);
                    if (s) { fmt.rate = (uint32_t)::atoi(s); env->ReleaseStringUTFChars(val, s); }
                    env->DeleteLocalRef(val);
                }
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(am_cls); env->DeleteLocalRef(am);
        }
        auto cc = get_channel_count(knst_audio_device{});
        if (cc && cc.value > 0) fmt.channels = cc.value;
        fmt.valid = true;
        return R::success(std::move(fmt));
    }
        // ─── AudioFocus (Android best practice) ────────────────────────
    static result<void> request_audio_focus() noexcept {
        return detail_android_audio::request_audio_focus()
            ? result<void>::success() : result<void>::fail(status::NotSupported);
    }
    static result<void> abandon_audio_focus() noexcept {
        detail_android_audio::abandon_audio_focus();
        return result<void>::success();
    }

    static result<knst_vector<knst_c16string>> get_ports(const knst_audio_device&) noexcept { return result<knst_vector<knst_c16string>>::fail(status::NotSupported); }
    static result<void> set_port(const knst_audio_device&, const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<knst_vector<card_info>> list_cards() noexcept { return result<knst_vector<card_info>>::fail(status::NotSupported); }
    static result<void> set_profile(uint32_t, const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<server_info> server() noexcept { return result<server_info>::fail(status::NotSupported); }
    static result<uint32_t> get_balance(const knst_audio_device&) noexcept { return result<uint32_t>::fail(status::NotSupported); }
    static result<void> set_balance(const knst_audio_device&, uint32_t) noexcept { return result<void>::fail(status::NotSupported); }
};

#elif KNST_USING_PLATFORM_LINUX

class audio {
public:
    static bool is_running() noexcept {
        char path[128];
        ::snprintf(path, sizeof(path), "/run/user/%u/pulse/native", (unsigned)::getuid());
        struct stat st;
        if (::stat(path, &st) == 0) return true;
        const char* remote = ::getenv("PULSE_SERVER");
        return remote && *remote;
    }

    // ----- Device listing -----

    static result<knst_vector<knst_audio_device>> list() noexcept {
        using R = result<knst_vector<knst_audio_device>>;
        knst_audio_info a = knst_devices::audio();
        if (!a.valid) return R::fail(status::NotFound);
        return R::success(std::move(a.devices));
    }

    static result<knst_vector<knst_audio_device>> list_sinks() noexcept {
        using R = result<knst_vector<knst_audio_device>>;
        auto all = list();
        if (!all) return R::fail(all.st, all.native);
        knst_vector<knst_audio_device> out;
        for (auto& d : all.value) if (!d.is_input) out.push_back(std::move(d));
        return R::success(std::move(out));
    }

    static result<knst_vector<knst_audio_device>> list_sources() noexcept {
        using R = result<knst_vector<knst_audio_device>>;
        auto all = list();
        if (!all) return R::fail(all.st, all.native);
        knst_vector<knst_audio_device> out;
        for (auto& d : all.value) if (d.is_input) out.push_back(std::move(d));
        return R::success(std::move(out));
    }

    static result<knst_audio_device> get_default(bool is_input) noexcept {
        using R = result<knst_audio_device>;
        auto info = server();
        if (!info) return R::fail(info.st, info.native);
        const knst_c16string& target = is_input ? info.value.default_source
                                                : info.value.default_sink;
        if (target.empty()) return R::fail(status::NotFound);
        auto all = list();
        if (!all) return R::fail(all.st, all.native);
        for (auto& d : all.value)
            if (d.name == target && d.is_input == is_input) return R::success(std::move(d));
        return R::fail(status::NotFound);
    }

    static result<void> set_default(const knst_audio_device& d) noexcept {
        if (!detail_pa::ok_name(d)) return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("set-default-%s", detail_pa::kind(d)) << d.name;
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    // ----- Volume -----

    static result<uint32_t> get_volume(const knst_audio_device& d) noexcept {
        using R = result<uint32_t>;
        if (!detail_pa::ok_name(d)) return R::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("get-%s-volume", detail_pa::kind(d)) << d.name;
        auto out = knst_dev::run("pactl", a, 3000);
        if (!out) return R::fail(out.st, out.native);
        uint32_t l = 0, r = 0; double ldb = 0, rdb = 0;
        detail_pa::parse_volume_lr(knst_dev::text(out.value), l, r, ldb, rdb);
        return R::success(l ? l : r);
    }

    static result<void> set_volume(const knst_audio_device& d, uint32_t percent) noexcept {
        if (!detail_pa::ok_name(d) || percent > 150)
            return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("set-%s-volume", detail_pa::kind(d)) << d.name
          << knst_dev::format("%u%%", percent);
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    static result<double> get_volume_db(const knst_audio_device& d) noexcept {
        using R = result<double>;
        if (!detail_pa::ok_name(d)) return R::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("get-%s-volume", detail_pa::kind(d)) << d.name;
        auto out = knst_dev::run("pactl", a, 3000);
        if (!out) return R::fail(out.st, out.native);
        uint32_t l = 0, r = 0; double ldb = 0, rdb = 0;
        detail_pa::parse_volume_lr(knst_dev::text(out.value), l, r, ldb, rdb);
        return R::success(ldb);
    }

    static result<void> set_volume_db(const knst_audio_device& d, double db) noexcept {
        if (!detail_pa::ok_name(d) || db < -120.0 || db > 10.0)
            return result<void>::fail(status::InvalidArgument);
        return set_volume(d, db_to_percent(db));
    }

    static result<uint32_t> get_channel_count(const knst_audio_device& d) noexcept {
        using R = result<uint32_t>;
        if (!detail_pa::ok_name(d)) return R::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("get-%s-volume", detail_pa::kind(d)) << d.name;
        auto out = knst_dev::run("pactl", a, 3000);
        if (!out) return R::fail(out.st, out.native);
        const char* text = knst_dev::text(out.value);
        uint32_t count = 0;
        const char* p = text;
        while ((p = ::strstr(p, "front-")) != nullptr) { ++count; p += 6; }
        if (count == 0 && ::strstr(text, "%")) count = 1;
        return R::success(count);
    }

    static result<uint32_t> get_channel_volume(const knst_audio_device& d,
                                               uint32_t channel) noexcept {
        using R = result<uint32_t>;
        if (!detail_pa::ok_name(d)) return R::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("get-%s-volume", detail_pa::kind(d)) << d.name;
        auto out = knst_dev::run("pactl", a, 3000);
        if (!out) return R::fail(out.st, out.native);
        uint32_t l = 0, r = 0; double ldb = 0, rdb = 0;
        detail_pa::parse_volume_lr(knst_dev::text(out.value), l, r, ldb, rdb);
        if (channel == 0) return R::success(l);
        if (channel == 1) return R::success(r);
        return R::fail(status::InvalidArgument);
    }

    static result<void> set_channel_volume(const knst_audio_device& d,
                                           uint32_t channel, uint32_t percent) noexcept {
        if (!detail_pa::ok_name(d) || percent > 150)
            return result<void>::fail(status::InvalidArgument);
        auto cur_l = get_channel_volume(d, 0);
        auto cur_r = get_channel_volume(d, 1);
        if (!cur_l) return result<void>::fail(cur_l.st, cur_l.native);
        if (!cur_r) return result<void>::fail(cur_r.st, cur_r.native);
        uint32_t l = cur_l.value, r = cur_r.value;
        if (channel == 0) l = percent;
        else if (channel == 1) r = percent;
        else return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("set-%s-volume", detail_pa::kind(d))
          << d.name << knst_dev::format("%u%%", l) << knst_dev::format("%u%%", r);
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    static result<uint32_t> get_balance(const knst_audio_device& d) noexcept {
        using R = result<uint32_t>;
        auto l = get_channel_volume(d, 0);
        auto r = get_channel_volume(d, 1);
        if (!l || !r) return R::fail(status::Unknown);
        uint32_t total = l.value + r.value;
        if (total == 0) return R::success(50);
        return R::success((uint32_t)(100ULL * r.value / total));
    }

    static result<void> set_balance(const knst_audio_device& d, uint32_t balance) noexcept {
        if (!detail_pa::ok_name(d) || balance > 100)
            return result<void>::fail(status::InvalidArgument);
        auto cur = get_volume(d);
        if (!cur) return result<void>::fail(cur.st, cur.native);
        uint32_t base = cur.value, l, r;
        if (balance <= 50) {
            l = base;
            r = (uint32_t)((uint64_t)base * balance / 50);
        } else {
            l = (uint32_t)((uint64_t)base * (100 - balance) / 50);
            r = base;
        }
        knst_dev::argv a;
        a << knst_dev::format("set-%s-volume", detail_pa::kind(d))
          << d.name << knst_dev::format("%u%%", l) << knst_dev::format("%u%%", r);
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    static result<bool> is_muted(const knst_audio_device& d) noexcept {
        using R = result<bool>;
        if (!detail_pa::ok_name(d)) return R::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("get-%s-mute", detail_pa::kind(d)) << d.name;
        auto out = knst_dev::run("pactl", a, 3000);
        if (!out) return R::fail(out.st, out.native);
        return R::success(::strstr(knst_dev::text(out.value), "Mute: yes") != nullptr);
    }

    static result<void> set_mute(const knst_audio_device& d, bool mute) noexcept {
        if (!detail_pa::ok_name(d)) return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("set-%s-mute", detail_pa::kind(d)) << d.name << (mute ? "1" : "0");
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    // ----- Ports & cards -----

    static result<knst_vector<knst_c16string>> get_ports(const knst_audio_device& d) noexcept {
        using R = result<knst_vector<knst_c16string>>;
        if (!detail_pa::ok_name(d)) return R::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << "list" << knst_dev::format("%ss", detail_pa::kind(d));
        auto t = knst_dev::run_tool_raw("pactl", a, 3000);
        if (!t.launched) return R::fail(status::ToolMissing);
        if (t.out.empty()) return R::success(knst_vector<knst_c16string>());
        knst_byte_string target_name = knst_dev::to_utf8(d.name);
        const char* target = knst_dev::text(target_name);
        knst_vector<knst_c16string> ports;
        bool in_target = false, in_ports = false;
        knst_dev::for_each_line(knst_dev::text(t.out), [&](const char* p, size_t len) {
            char line[512];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';
            char* content = line;
            while (*content == '\t' || *content == ' ') ++content;
            const char* v;
            if (!in_target) {
                if (detail_pa::line_value(content, "Name", v) && ::strcmp(v, target) == 0)
                    in_target = true;
                return;
            }
            if (::strcmp(content, "Ports:") == 0)  { in_ports = true;  return; }
            if (::strstr(content, "Active Port:")) { in_ports = false; return; }
            if (!in_ports) return;
            const char* sep = ::strstr(content, ": ");
            if (!sep) return;
            knst_c16string port_name(content, (uint32_t)(sep - content));
            if (!port_name.empty()) ports.push_back(std::move(port_name));
        });
        return R::success(std::move(ports));
    }

    static result<void> set_port(const knst_audio_device& d,
                                 const knst_c16string& port) noexcept {
        if (!detail_pa::ok_name(d) || !knst_dev::is_safe_token(port, 100))
            return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("set-%s-port", detail_pa::kind(d)) << d.name << port;
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    static result<knst_vector<card_info>> list_cards() noexcept {
        using R = result<knst_vector<card_info>>;
        knst_dev::argv a; a << "list" << "cards";
        auto out = knst_dev::run("pactl", a, 3000);
        if (!out) return R::fail(out.st, out.native);
        knst_vector<card_info> cards;
        card_info cur;
        bool in_card = false, in_profiles = false, in_ports = false;
        knst_dev::for_each_line(knst_dev::text(out.value), [&](const char* p, size_t len) {
            char line[512];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';
            if (::strncmp(line, "Card #", 6) == 0) {
                if (in_card && cur.valid) cards.push_back(std::move(cur));
                cur = card_info();
                cur.index = (uint32_t)::atoi(line + 6);
                cur.valid = true;
                in_card = true;
                in_profiles = in_ports = false;
                return;
            }
            if (!in_card) return;
            char* content = line;
            while (*content == '\t' || *content == ' ') ++content;
            const char* v;
            if (::strcmp(content, "Profiles:") == 0) { in_profiles = true;  in_ports = false; return; }
            if (::strcmp(content, "Ports:")    == 0) { in_ports    = true;  in_profiles = false; return; }
            if (detail_pa::line_value(content, "Active Profile", v)) {
                cur.active_profile = knst_c16string(v);
                in_profiles = in_ports = false;
                return;
            }
            if (detail_pa::line_value(content, "Name", v))   { cur.name   = knst_c16string(v); return; }
            if (detail_pa::line_value(content, "Driver", v)) { cur.driver = knst_c16string(v); return; }
            if (in_profiles) {
                const char* sep = ::strstr(content, ": ");
                if (sep) {
                    knst_c16string prof(content, (uint32_t)(sep - content));
                    if (!prof.empty()) cur.profiles.push_back(std::move(prof));
                }
            } else if (in_ports) {
                const char* sep = ::strchr(content, ':');
                if (sep) {
                    knst_c16string port(content, (uint32_t)(sep - content));
                    if (!port.empty()) cur.ports.push_back(std::move(port));
                }
            }
        });
        if (in_card && cur.valid) cards.push_back(std::move(cur));
        return R::success(std::move(cards));
    }

    static result<void> set_profile(uint32_t card_index,
                                    const knst_c16string& profile) noexcept {
        if (!knst_dev::is_safe_token(profile, 100))
            return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << "set-card-profile" << knst_dev::u32_to_string(card_index) << profile;
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    // ----- Server info -----

    static result<server_info> server() noexcept {
        using R = result<server_info>;
        knst_dev::argv a; a << "info";
        auto out = knst_dev::run("pactl", a, 3000);
        if (!out) return R::fail(out.st, out.native);
        server_info s;
        knst_dev::for_each_line(knst_dev::text(out.value), [&](const char* p, size_t len) {
            char line[512];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';
            const char* v;
            if      (detail_pa::line_value(line, "Server Name", v))    s.name    = knst_c16string(v);
            else if (detail_pa::line_value(line, "Server Version", v)) s.version = knst_c16string(v);
            else if (detail_pa::line_value(line, "Default Sink", v))   s.default_sink   = knst_c16string(v);
            else if (detail_pa::line_value(line, "Default Source", v)) s.default_source = knst_c16string(v);
            else if (detail_pa::line_value(line, "Default Sample Specification", v))
                s.sample_spec = knst_c16string(v);
            else if (detail_pa::line_value(line, "Default Channel Map", v))
                s.channel_map = knst_c16string(v);
        });
        if (!s.sample_spec.empty()) {
            knst_byte_string spec = knst_dev::to_utf8(s.sample_spec);
            unsigned ch = 0, hz = 0;
            if (::sscanf(knst_dev::text(spec), "%*s %uch %uHz", &ch, &hz) == 2) {
                s.channels = ch; s.sample_rate = hz;
            }
        }
        s.valid = !s.name.empty();
        return R::success(std::move(s));
    }

    // ----- Streams (playback apps) -----

    static result<knst_vector<stream_info>> list_sink_inputs() noexcept {
        using R = result<knst_vector<stream_info>>;
        knst_dev::argv a; a << "list" << "sink-inputs";
        auto out = knst_dev::run("pactl", a, 3000);
        if (!out) return R::fail(out.st, out.native);
        knst_vector<stream_info> v;
        detail_pa::parse_streams(knst_dev::text(out.value), true, v);
        return R::success(std::move(v));
    }

    static result<knst_vector<stream_info>> list_source_outputs() noexcept {
        using R = result<knst_vector<stream_info>>;
        knst_dev::argv a; a << "list" << "source-outputs";
        auto out = knst_dev::run("pactl", a, 3000);
        if (!out) return R::fail(out.st, out.native);
        knst_vector<stream_info> v;
        detail_pa::parse_streams(knst_dev::text(out.value), false, v);
        return R::success(std::move(v));
    }

    static result<knst_vector<stream_info>> list_streams() noexcept {
        using R = result<knst_vector<stream_info>>;
        auto in  = list_sink_inputs();
        auto out = list_source_outputs();
        if (!in)  return R::fail(in.st, in.native);
        if (!out) return R::fail(out.st, out.native);
        knst_vector<stream_info> all;
        for (auto& s : in.value)  all.push_back(std::move(s));
        for (auto& s : out.value) all.push_back(std::move(s));
        return R::success(std::move(all));
    }

    static result<void> set_stream_volume(uint32_t idx, uint32_t percent, bool is_output = true) noexcept {
        if (percent > 150) return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << (is_output ? "set-sink-input-volume" : "set-source-output-volume")
          << knst_dev::u32_to_string(idx) << knst_dev::format("%u%%", percent);
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    static result<void> set_stream_mute(uint32_t idx, bool mute, bool is_output = true) noexcept {
        knst_dev::argv a;
        a << (is_output ? "set-sink-input-mute" : "set-source-output-mute")
          << knst_dev::u32_to_string(idx) << (mute ? "1" : "0");
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    static result<void> move_stream(uint32_t idx, uint32_t device_idx, bool is_output = true) noexcept {
        knst_dev::argv a;
        a << (is_output ? "move-sink-input" : "move-source-output")
          << knst_dev::u32_to_string(idx) << knst_dev::u32_to_string(device_idx);
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    static result<void> cork_stream(uint32_t idx, bool cork, bool is_output = true) noexcept {
        knst_dev::argv a;
        a << (is_output ? "suspend-sink-input" : "suspend-source-output")
          << knst_dev::u32_to_string(idx) << (cork ? "1" : "0");
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    static result<void> suspend(const knst_audio_device& d, bool suspend) noexcept {
        if (!detail_pa::ok_name(d)) return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a;
        a << knst_dev::format("suspend-%s", detail_pa::kind(d))
          << d.name << (suspend ? "1" : "0");
        return knst_dev::drop(knst_dev::run("pactl", a, 3000));
    }

    // ----- Playback (external paplay: WAV/FLAC/OGG via libsndfile; see play_file_any for the native path) -----

    static result<void> play_file(const knst_audio_device* device,
                                  const knst_c16string& path,
                                  uint32_t timeout_ms = 600000) noexcept {
        using R = result<void>;
        if (path.empty() || (device && (device->is_input || !detail_pa::ok_name(*device))))
            return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        knst_dev::argv a;
        if (device) {
            knst_byte_string u8 = knst_dev::to_utf8(device->name);
            a << knst_dev::format("--device=%s", knst_dev::text(u8));
        }
        a << abs;
        return knst_dev::drop(knst_dev::run("paplay", a, timeout_ms));
    }

    static result<void> play_raw(const knst_audio_device* device,
                                 const knst_byte_string& pcm,
                                 const play_options& opt = play_options(),
                                 uint32_t timeout_ms = 600000) noexcept {
        using R = result<void>;
        if (pcm.empty() || opt.bits_per_sample != 16)
            return R::fail(status::InvalidArgument);
        if (device && (device->is_input || !detail_pa::ok_name(*device)))
            return R::fail(status::InvalidArgument);
        knst_c16string tmp = knst_file::get_temp_dir();
        if (tmp.empty()) return R::fail(status::IoError);
        tmp = knst_file::join_path(tmp, u"knst_play.raw.wav");
        unsigned char hdr[44];
        write_wav_header(hdr, (uint32_t)pcm.length(), opt.rate, (uint16_t)opt.channels, 16);
        knst_byte_string full;
        full.reserve(44 + (uint32_t)pcm.length());
        for (int i = 0; i < 44; ++i) full.push_back(hdr[i]);
        for (uint32_t i = 0; i < pcm.length(); ++i) full.push_back(pcm[i]);
        if (!knst_file::write_file_data(tmp, full.data(), full.length()))
            return R::fail(status::IoError);
        auto res = play_file(device, tmp, timeout_ms);
        knst_file::remove_file(tmp);
        return res;
    }

    static result<knst_audio_device> get_device_detail(const knst_audio_device& d) noexcept {
        using R = result<knst_audio_device>;
        knst_audio_device out = d;
        auto v = get_volume(d); if (v) out.volume_percent = v.value;
        auto m = is_muted(d);   if (m) out.muted = m.value;
        return R::success(std::move(out));
    }

    static result<device_format> get_format(const knst_audio_device& d) noexcept {
        using R = result<device_format>;
        if (!detail_pa::ok_name(d)) return R::fail(status::InvalidArgument);
        knst_dev::argv a; a << "list" << knst_dev::format("%ss", detail_pa::kind(d));
        auto t = knst_dev::run_tool_raw("pactl", a, 3000);
        if (!t.launched) return R::fail(status::ToolMissing);
        knst_byte_string target_name = knst_dev::to_utf8(d.name);
        const char* target = knst_dev::text(target_name);
        device_format fmt;
        bool in_target = false;
        knst_dev::for_each_line(knst_dev::text(t.out), [&](const char* p, size_t len) {
            char line[512];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';
            char* content = line;
            while (*content == '\t' || *content == ' ') ++content;
            const char* v;
            if (!in_target) {
                if (detail_pa::line_value(content, "Name", v) && ::strcmp(v, target) == 0) in_target = true;
                return;
            }
            if (detail_pa::line_value(content, "Sample Specification", v)) {
                unsigned ch = 0, hz = 0;
                if (::sscanf(v, "%*s %uch %uHz", &ch, &hz) == 2) {
                    fmt.channels = ch; fmt.rate = hz; fmt.bits_per_sample = 16; fmt.valid = true;
                }
                in_target = false;
            }
        });
        return fmt.valid ? R::success(std::move(fmt)) : R::fail(status::NotFound);
    }

        // Short-window peak meter. Captures ~200 ms of audio from a sink/source
    // monitor and reports the linear peak (0..1). Three capture backends are
    // tried in order because different Linux audio stacks expose different
    // tools:
    //
    //   1. parec       — PulseAudio, and PipeWire-pulse (the compat shim)
    //   2. pw-cat       — native PipeWire (bypasses the pulse shim entirely)
    //   3. pw-record    — older binary name for the same PipeWire tool
    //
    // 50 ms was too short for PipeWire-pulse; the stream doesn't even finish
    // negotiating before the kill timer fires. 200 ms is the smallest window
    // that's reliably long enough on slow systems while still feeling instant.
    static result<double> get_peak_meter(const knst_audio_device& d) noexcept {
        using R = result<double>;
        if (!detail_pa::ok_name(d)) return R::fail(status::InvalidArgument);

        // Sinks need a ".monitor" suffix to capture their loopback. Both
        // PulseAudio and PipeWire-pulse create these monitor sources with
        // the same naming convention, so the same string works for all three
        // capture tools below.
        knst_byte_string dev_u8 = knst_dev::to_utf8(d.name);
        std::string target = knst_dev::text(dev_u8);
        if (!d.is_input) target += ".monitor";

        constexpr uint32_t kCaptureMs  = 200;
        constexpr uint32_t kMaxRunMs   = kCaptureMs + 1200;   // kill slack
        // Exit codes considered "ran to completion" by the outer `timeout`:
        //   0   — tool finished before its own kill timer fired
        //   124 — timeout sent SIGTERM (or SIGKILL fallback) and reported it
        //   130 — SIGINT (rare, some parec builds)
        //   143 — SIGTERM reached the process directly (older parec)
        const std::initializer_list<int> kOkCodes = { 0, 124, 130, 143 };

        auto peak_of = [](const knst_byte_string& data) -> double {
            if (data.length() < 16) return -1.0;              // need >= 4 floats
            const float* f = reinterpret_cast<const float*>(data.data());
            const uint32_t n = data.length() / 4;
            float peak = 0.0f;
            for (uint32_t i = 0; i < n; ++i) {
                const float v = std::fabs(f[i]);
                if (v > peak) peak = v;
            }
            return static_cast<double>(peak);
        };

        status last_st = status::Unknown;
        int    last_exit = -1;

        // ---- 1) parec (PulseAudio / PipeWire-pulse) ------------------------
        {
            knst_dev::argv a;
            a << "-s" << "TERM"
              << knst_dev::format("0.%03u", kCaptureMs) << "parec";
            a << knst_dev::format("--device=%s", target.c_str());
            a << "--format=float32le" << "--rate=16000" << "--channels=1"
              << "--latency-msec=50" << "--raw";
            auto out = knst_dev::run_codes("timeout", a, kMaxRunMs, kOkCodes);
            if (out) {
                const double p = peak_of(out.value);
                if (p >= 0.0) return R::success(p);
                last_st = status::IoError;
            } else {
                last_st = out.st;
                last_exit = out.native;
            }
        }

        // ---- 2) pw-cat --record (native PipeWire) --------------------------
        // Bypasses the pulse compatibility layer. The .monitor suffix is
        // accepted because PipeWire mirrors pulse names for its nodes.
        {
            knst_dev::argv a;
            a << "-s" << "TERM"
              << knst_dev::format("0.%03u", kCaptureMs) << "pw-cat" << "--record";
            a << "--target" << target.c_str();
            a << "--format" << "f32" << "--rate" << "16000" << "--channels" << "1";
            a << "--raw";
            auto out = knst_dev::run_codes("timeout", a, kMaxRunMs, kOkCodes);
            if (out) {
                const double p = peak_of(out.value);
                if (p >= 0.0) return R::success(p);
            }
        }

        // ---- 3) pw-record (older binary name, same tool) -------------------
        {
            knst_dev::argv a;
            a << "-s" << "TERM"
              << knst_dev::format("0.%03u", kCaptureMs) << "pw-record";
            a << "--target" << target.c_str();
            a << "--format" << "f32" << "--rate" << "16000" << "--channels" << "1";
            a << "--raw";
            auto out = knst_dev::run_codes("timeout", a, kMaxRunMs, kOkCodes);
            if (out) {
                const double p = peak_of(out.value);
                if (p >= 0.0) return R::success(p);
            }
        }

        // All three failed. Return the first error (with exit code so callers
        // can tell "tool missing" from "device not found" from "silent").
        return R::fail(last_st == status::Unknown ? status::IoError : last_st,
                       last_exit);
    }
};

#endif // Linux

// ============================================================
// Windows (WASAPI)
// ============================================================

#if KNST_USING_PLATFORM_WINDOWS

namespace detail_win {

template<typename T>
class com_ptr {
    T* p_ = nullptr;
public:
    com_ptr() noexcept = default;
    com_ptr(const com_ptr&) = delete;
    com_ptr& operator=(const com_ptr&) = delete;
    com_ptr(com_ptr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }              // needed by result<com_ptr<T>>
    com_ptr& operator=(com_ptr&& o) noexcept { if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; } return *this; }
    ~com_ptr() { if (p_) p_->Release(); }
    T** addr() noexcept { return &p_; }
    T*  get()  const noexcept { return p_; }
    T*  operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    void reset(T* p = nullptr) noexcept { if (p_) p_->Release(); p_ = p; }
};

class com_initializer {
public:
    com_initializer() noexcept {
        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owns_ = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE) owns_ = false;
    }
    ~com_initializer() noexcept { if (owns_) ::CoUninitialize(); }
    com_initializer(const com_initializer&) = delete;
    com_initializer& operator=(const com_initializer&) = delete;
private:
    bool owns_ = false;
};

struct DeviceShareMode { DWORD mode; };

interface IPolicyConfig : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

static const CLSID KNST_CLSID_CPolicyConfigClient =
    {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
static const IID KNST_IID_IPolicyConfig =
    {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};

inline result<com_ptr<IMMDevice>> open_device(const knst_audio_device& d) noexcept {
    using R = result<com_ptr<IMMDevice>>;
    if (d.name.empty()) return R::fail(status::InvalidArgument);
    com_ptr<IMMDeviceEnumerator> en;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(en.addr()));
    if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
    com_ptr<IMMDevice> dev;
    LPCWSTR id = reinterpret_cast<LPCWSTR>(d.name.data());
    hr = en->GetDevice(id, dev.addr());
    if (FAILED(hr))
        return R::fail(hr == E_NOTFOUND ? status::NotFound : status::Unknown, (int)hr);
    return R::success(std::move(dev));
}

template<typename F>
inline result<void> with_endpoint_volume(const knst_audio_device& d, F&& fn) noexcept {
    using R = result<void>;
    // knst_devices::audio() (which powers list_sinks / list) calls
    // CoInitializeEx + CoUninitialize as a balanced pair, so by the
    // time we get here on a worker thread COM is back to its previous
    // (possibly uninitialized) state. Without this initializer
    // CoCreateInstance in open_device fails with CO_E_NOTINITIALIZED.
    com_initializer com;
    auto dev = open_device(d);
    if (!dev) return R::fail(dev.st, dev.native);
    com_ptr<IAudioEndpointVolume> vol;
    HRESULT hr = dev.value->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                                     nullptr, reinterpret_cast<void**>(vol.addr()));
    if (FAILED(hr))
        return R::fail(hr == E_NOINTERFACE ? status::NotSupported : status::Unknown, (int)hr);
    hr = fn(vol.get());
    if (FAILED(hr))
        return R::fail(hr == E_ACCESSDENIED ? status::PermissionDenied : status::Unknown, (int)hr);
    return R::success();
}

inline result<void> mci_play(const knst_c16string& path, uint32_t timeout_ms) noexcept {
    using R = result<void>;
    wchar_t cmd[1024];
    ::swprintf_s(cmd, L"open \"%s\" alias knstplay", reinterpret_cast<const wchar_t*>(path.data()));
    MCIERROR err = ::mciSendStringW(cmd, nullptr, 0, nullptr);
    if (err) return R::fail(status::IoError, (int)err);
    err = ::mciSendStringW(L"play knstplay", nullptr, 0, nullptr);
    if (err) {
        ::mciSendStringW(L"close knstplay", nullptr, 0, nullptr);
        return R::fail(status::IoError, (int)err);
    }
    const uint64_t deadline = ::GetTickCount64() + timeout_ms;
    for (;;) {
        wchar_t status_buf[64] = {0};
        ::mciSendStringW(L"status knstplay mode", status_buf, 64, nullptr);
        if (::wcscmp(status_buf, L"playing") != 0) break;
        if (::GetTickCount64() > deadline) break;
        ::Sleep(100);
    }
    ::mciSendStringW(L"stop knstplay", nullptr, 0, nullptr);
    ::mciSendStringW(L"close knstplay", nullptr, 0, nullptr);
    return R::success();
}

} // namespace detail_win

// ─────────────────────────────────────────────────────────────────────────────
//  Windows per-app streams (IAudioSessionManager2 → IAudioSessionEnumerator)
//
//  Matches Linux `pactl list sink-inputs` / `set-sink-input-volume` /
//  `set-sink-input-mute` semantics:
//
//    • Each unique IAudioSessionControl2 = one playback stream (one app).
//    • index   → position in enumeration (unstable across app restarts,
//                same as Linux's sink-input index)
//    • application → display name from session, or process image name
//    • volume_percent → ISimpleAudioVolume::GetMasterVolume × 100
//    • muted → ISimpleAudioVolume::GetMute
//    • media_name → "[PID <n>]" hint so callers can match to a process
//
//  Session "grouping param" can group multiple sessions from one app, but
//  ISimpleAudioVolume operates per-session — same as Linux.
// ─────────────────────────────────────────────────────────────────────────────
namespace detail_win_session {

// Explicit IIDs — avoids __uuidof incompatibilities on older MinGW.
static const IID KNST_IID_SessionManager2 =
    {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
static const IID KNST_IID_SessionControl2 =
    {0xbfb7ff88, 0x7239, 0x4fc9, {0x8f, 0xa2, 0x07, 0xc9, 0x50, 0xbe, 0x9c, 0x6d}};
static const IID KNST_IID_SimpleAudioVolume =
    {0x87CE5498, 0x68D6, 0x44E5, {0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8}};

/// Best-effort process image name from a PID. Returns empty on failure.
inline knst_c16string process_name(DWORD pid) noexcept {
    if (pid == 0) return knst_c16string();
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return knst_c16string();
    wchar_t buf[MAX_PATH];
    DWORD size = MAX_PATH;
    knst_c16string result;
    if (::QueryFullProcessImageNameW(h, 0, buf, &size) && size > 0) {
        const wchar_t* slash = ::wcsrchr(buf, L'\\');
        const wchar_t* name  = slash ? slash + 1 : buf;
        result = knst_c16string(reinterpret_cast<const char16_t*>(name));
    }
    ::CloseHandle(h);
    return result;
}

/// Opens the default CAPTURE endpoint + its IAudioSessionManager2 (eCapture flow).
inline result<IAudioSessionEnumerator*> open_default_capture_enumerator() noexcept {
    using R = result<IAudioSessionEnumerator*>;
    detail_win::com_initializer com;

    detail_win::com_ptr<IMMDeviceEnumerator> en;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                     __uuidof(IMMDeviceEnumerator),
                                     reinterpret_cast<void**>(en.addr()));
    if (FAILED(hr) || !en) return R::fail(status::Unknown, (int)hr);

    detail_win::com_ptr<IMMDevice> dev;
    hr = en->GetDefaultAudioEndpoint(eCapture, eConsole, dev.addr());
    if (FAILED(hr) || !dev) return R::fail(status::NotFound, (int)hr);

    detail_win::com_ptr<IAudioSessionManager2> mgr;
    hr = dev->Activate(KNST_IID_SessionManager2, CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(mgr.addr()));
    if (FAILED(hr) || !mgr) return R::fail(status::NotSupported, (int)hr);

    IAudioSessionEnumerator* se = nullptr;
    hr = mgr->GetSessionEnumerator(&se);
    if (FAILED(hr) || !se) return R::fail(status::Unknown, (int)hr);
    return R::success(se);
}

/// Opens the default render endpoint + its IAudioSessionManager2.
/// Returns the enumerator (caller must Release after use).
inline result<IAudioSessionEnumerator*> open_default_enumerator() noexcept {
    using R = result<IAudioSessionEnumerator*>;
    detail_win::com_initializer com;

    detail_win::com_ptr<IMMDeviceEnumerator> en;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                     __uuidof(IMMDeviceEnumerator),
                                     reinterpret_cast<void**>(en.addr()));
    if (FAILED(hr) || !en) return R::fail(status::Unknown, (int)hr);

    detail_win::com_ptr<IMMDevice> dev;
    hr = en->GetDefaultAudioEndpoint(eRender, eConsole, dev.addr());
    if (FAILED(hr) || !dev) return R::fail(status::NotFound, (int)hr);

    detail_win::com_ptr<IAudioSessionManager2> mgr;
    hr = dev->Activate(KNST_IID_SessionManager2, CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(mgr.addr()));
    if (FAILED(hr) || !mgr) return R::fail(status::NotSupported, (int)hr);

    IAudioSessionEnumerator* se = nullptr;
    hr = mgr->GetSessionEnumerator(&se);
    if (FAILED(hr) || !se) return R::fail(status::Unknown, (int)hr);
    return R::success(se);
}

inline result<knst_vector<stream_info>> list(bool is_output = true) noexcept {
    using R = result<knst_vector<stream_info>>;
    auto se_r = is_output ? open_default_enumerator()
                          : open_default_capture_enumerator();
    if (!se_r) return R::fail(se_r.st, se_r.native);
    IAudioSessionEnumerator* se = se_r.value;

    int count = 0;
    HRESULT hr = se->GetCount(&count);
    if (FAILED(hr)) { se->Release(); return R::fail(status::Unknown, (int)hr); }

    R r;
    for (int i = 0; i < count; ++i) {
        detail_win::com_ptr<IAudioSessionControl> ctl;
        hr = se->GetSession(i, ctl.addr());
        if (FAILED(hr) || !ctl) continue;

        detail_win::com_ptr<IAudioSessionControl2> ctl2;
        hr = ctl->QueryInterface(KNST_IID_SessionControl2,
                                   reinterpret_cast<void**>(ctl2.addr()));
        if (FAILED(hr) || !ctl2) continue;

        AudioSessionState state = AudioSessionStateExpired;
        ctl2->GetState(&state);
        if (state == AudioSessionStateExpired) continue;

        DWORD pid = 0;
        ctl2->GetProcessId(&pid);
        bool is_system_sound = SUCCEEDED(ctl2->IsSystemSoundsSession());

        LPWSTR disp = nullptr;
        ctl2->GetDisplayName(&disp);
        knst_c16string display;
        if (disp && *disp) display = knst_c16string(reinterpret_cast<const char16_t*>(disp));

        // Fallback chain for a friendly name:
        //   1. session display name
        //   2. process image name (e.g. "spotify.exe")
        //   3. system sounds / unknown
        knst_c16string app = display;
        if (app.empty() && !is_system_sound) app = process_name(pid);
        if (app.empty() && is_system_sound) app = u"System Sounds";
        if (app.empty()) app = u"(unknown)";

        detail_win::com_ptr<ISimpleAudioVolume> vol;
        hr = ctl->QueryInterface(KNST_IID_SimpleAudioVolume,
                                   reinterpret_cast<void**>(vol.addr()));
        float v_level = 0.0f;
        BOOL  v_mute  = FALSE;
        if (SUCCEEDED(hr) && vol) {
            vol->GetMasterVolume(&v_level);
            vol->GetMute(&v_mute);
        }

        stream_info si;
        si.index          = (uint32_t)i;
        si.driver         = u"WASAPI";
        si.application    = std::move(app);
        si.is_output      = is_output;
        si.volume_percent = (uint32_t)(v_level * 100.0f + 0.5f);
        si.muted          = (v_mute != FALSE);
        si.valid          = true;

        // Media name carries PID so a caller can match by process if needed.
        if (pid != 0) {
            char pid_buf[32];
            ::snprintf(pid_buf, sizeof(pid_buf), "[PID %lu]", (unsigned long)pid);
            si.media_name = knst_c16string(pid_buf);
        }

        if (disp) ::CoTaskMemFree(disp);
        r.value.push_back(std::move(si));
    }
    se->Release();
    return R::success(std::move(r.value));
}

inline result<void> set_volume(uint32_t idx, uint32_t percent) noexcept {
    using R = result<void>;
    if (percent > 100) return R::fail(status::InvalidArgument);
    auto se_r = open_default_enumerator();
    if (!se_r) return R::fail(se_r.st, se_r.native);
    IAudioSessionEnumerator* se = se_r.value;

    int count = 0;
    se->GetCount(&count);
    if (idx >= (uint32_t)count) { se->Release(); return R::fail(status::NotFound); }

    detail_win::com_ptr<IAudioSessionControl> ctl;
    HRESULT hr = se->GetSession((int)idx, ctl.addr());
    se->Release();
    if (FAILED(hr) || !ctl) return R::fail(status::NotFound);

    detail_win::com_ptr<ISimpleAudioVolume> vol;
    hr = ctl->QueryInterface(KNST_IID_SimpleAudioVolume,
                               reinterpret_cast<void**>(vol.addr()));
    if (FAILED(hr) || !vol) return R::fail(status::NotSupported);

    hr = vol->SetMasterVolume(percent / 100.0f, nullptr);
    if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
    return R::success();
}

inline result<void> set_mute(uint32_t idx, bool mute) noexcept {
    using R = result<void>;
    auto se_r = open_default_enumerator();
    if (!se_r) return R::fail(se_r.st, se_r.native);
    IAudioSessionEnumerator* se = se_r.value;

    int count = 0;
    se->GetCount(&count);
    if (idx >= (uint32_t)count) { se->Release(); return R::fail(status::NotFound); }

    detail_win::com_ptr<IAudioSessionControl> ctl;
    HRESULT hr = se->GetSession((int)idx, ctl.addr());
    se->Release();
    if (FAILED(hr) || !ctl) return R::fail(status::NotFound);

    detail_win::com_ptr<ISimpleAudioVolume> vol;
    hr = ctl->QueryInterface(KNST_IID_SimpleAudioVolume,
                               reinterpret_cast<void**>(vol.addr()));
    if (FAILED(hr) || !vol) return R::fail(status::NotSupported);

    hr = vol->SetMute(mute ? TRUE : FALSE, nullptr);
    if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
    return R::success();
}

} // namespace detail_win_session

class audio {
public:
    static bool is_running() noexcept { return true; }

    static result<knst_vector<knst_audio_device>> list() noexcept {
        using R = result<knst_vector<knst_audio_device>>;
        knst_audio_info a = knst_devices::audio();
        if (!a.valid) return R::fail(status::NotFound);
        return R::success(std::move(a.devices));
    }
    static result<knst_vector<knst_audio_device>> list_sinks() noexcept {
        using R = result<knst_vector<knst_audio_device>>;
        auto all = list();
        if (!all) return R::fail(all.st, all.native);
        knst_vector<knst_audio_device> out;
        for (auto& d : all.value) if (!d.is_input) out.push_back(std::move(d));
        return R::success(std::move(out));
    }
    static result<knst_vector<knst_audio_device>> list_sources() noexcept {
        using R = result<knst_vector<knst_audio_device>>;
        auto all = list();
        if (!all) return R::fail(all.st, all.native);
        knst_vector<knst_audio_device> out;
        for (auto& d : all.value) if (d.is_input) out.push_back(std::move(d));
        return R::success(std::move(out));
    }

    static result<uint32_t> get_volume(const knst_audio_device& d) noexcept {
        using R = result<uint32_t>;
        float v = 0.0f;
        auto r = detail_win::with_endpoint_volume(d, [&](IAudioEndpointVolume* p) { return p->GetMasterVolumeLevelScalar(&v); });
        if (!r) return R::fail(r.st, r.native);
        return R::success((uint32_t)(v * 100.0f + 0.5f));
    }
    static result<void> set_volume(const knst_audio_device& d, uint32_t percent) noexcept {
        if (percent > 100) return result<void>::fail(status::InvalidArgument);
        return detail_win::with_endpoint_volume(d, [&](IAudioEndpointVolume* p) {
            return p->SetMasterVolumeLevelScalar(percent / 100.0f, nullptr);
        });
    }
    static result<double> get_volume_db(const knst_audio_device& d) noexcept {
        using R = result<double>;
        float db = 0.0f;
        auto r = detail_win::with_endpoint_volume(d, [&](IAudioEndpointVolume* p) { return p->GetMasterVolumeLevel(&db); });
        if (!r) return R::fail(r.st, r.native);
        return R::success((double)db);
    }
    static result<void> set_volume_db(const knst_audio_device& d, double db) noexcept {
        if (db < -120.0 || db > 12.0) return result<void>::fail(status::InvalidArgument);
        return detail_win::with_endpoint_volume(d, [&](IAudioEndpointVolume* p) {
            return p->SetMasterVolumeLevel((float)db, nullptr);
        });
    }

    static result<uint32_t> get_channel_count(const knst_audio_device& d) noexcept {
        using R = result<uint32_t>;
        UINT ch = 0;
        auto r = detail_win::with_endpoint_volume(d, [&](IAudioEndpointVolume* p) { return p->GetChannelCount(&ch); });
        if (!r) return R::fail(r.st, r.native);
        return R::success((uint32_t)ch);
    }
    static result<uint32_t> get_channel_volume(const knst_audio_device& d, uint32_t channel) noexcept {
        using R = result<uint32_t>;
        float v = 0.0f;
        auto r = detail_win::with_endpoint_volume(d, [&](IAudioEndpointVolume* p) -> HRESULT {
            UINT ch = 0;
            HRESULT hr = p->GetChannelCount(&ch);
            if (FAILED(hr)) return hr;
            if (channel >= ch) return E_INVALIDARG;
            return p->GetChannelVolumeLevelScalar(channel, &v);
        });
        if (!r) return R::fail(r.st, r.native);
        return R::success((uint32_t)(v * 100.0f + 0.5f));
    }
    static result<void> set_channel_volume(const knst_audio_device& d, uint32_t channel, uint32_t percent) noexcept {
        if (percent > 100) return result<void>::fail(status::InvalidArgument);
        return detail_win::with_endpoint_volume(d, [&](IAudioEndpointVolume* p) -> HRESULT {
            UINT ch = 0;
            HRESULT hr = p->GetChannelCount(&ch);
            if (FAILED(hr)) return hr;
            if (channel >= ch) return E_INVALIDARG;
            return p->SetChannelVolumeLevelScalar(channel, percent / 100.0f, nullptr);
        });
    }

    static result<bool> is_muted(const knst_audio_device& d) noexcept {
        using R = result<bool>;
        BOOL m = FALSE;
        auto r = detail_win::with_endpoint_volume(d, [&](IAudioEndpointVolume* p) { return p->GetMute(&m); });
        if (!r) return R::fail(r.st, r.native);
        return R::success(m != FALSE);
    }
    static result<void> set_mute(const knst_audio_device& d, bool mute) noexcept {
        return detail_win::with_endpoint_volume(d, [&](IAudioEndpointVolume* p) { return p->SetMute(mute ? TRUE : FALSE, nullptr); });
    }

    static result<void> set_default(const knst_audio_device& d) noexcept {
        using R = result<void>;
        detail_win::com_initializer com;
        detail_win::IPolicyConfig* pc = nullptr;
        HRESULT hr = ::CoCreateInstance(detail_win::KNST_CLSID_CPolicyConfigClient, nullptr, CLSCTX_ALL,
                                        detail_win::KNST_IID_IPolicyConfig, reinterpret_cast<void**>(&pc));
        if (FAILED(hr) || !pc) return R::fail(status::NotSupported, (int)hr);
        LPCWSTR id = reinterpret_cast<LPCWSTR>(d.name.data());
        hr = pc->SetDefaultEndpoint(id, eConsole);
        if (SUCCEEDED(hr)) hr = pc->SetDefaultEndpoint(id, eMultimedia);
        if (SUCCEEDED(hr)) hr = pc->SetDefaultEndpoint(id, eCommunications);
        pc->Release();
        if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
        return R::success();
    }

    static result<knst_audio_device> get_default(bool is_input) noexcept {
        using R = result<knst_audio_device>;
        detail_win::com_initializer com;
        detail_win::com_ptr<IMMDeviceEnumerator> en;
        HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(en.addr()));
        if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
        EDataFlow flow = is_input ? eCapture : eRender;
        detail_win::com_ptr<IMMDevice> dev;
        hr = en->GetDefaultAudioEndpoint(flow, eConsole, dev.addr());
        if (FAILED(hr)) return R::fail(status::NotFound, (int)hr);
        LPWSTR id = nullptr;
        hr = dev->GetId(&id);
        if (FAILED(hr) || !id) return R::fail(status::Unknown, (int)hr);
        knst_audio_device out;
        out.name       = knst_c16string(reinterpret_cast<const char16_t*>(id));
        // Friendly name for display
        detail_win::com_ptr<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, props.addr()))) {
            PROPVARIANT pv; ::PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) &&
                pv.vt == VT_LPWSTR && pv.pwszVal) {
                out.description = knst_c16string(
                    reinterpret_cast<const char16_t*>(pv.pwszVal));
            }
            ::PropVariantClear(&pv);
        }
        out.is_input   = is_input;
        out.is_output  = !is_input;
        out.is_default = true;
        out.valid      = true;
        ::CoTaskMemFree(id);
        return R::success(std::move(out));
    }

    static result<device_format> get_format(const knst_audio_device& d) noexcept {
        using R = result<device_format>;
        detail_win::com_initializer com;
        auto dev = detail_win::open_device(d);
        if (!dev) return R::fail(dev.st, dev.native);
        detail_win::com_ptr<IAudioClient> client;
        HRESULT hr = dev.value->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.addr()));
        if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);
        WAVEFORMATEX* mix = nullptr;
        hr = client->GetMixFormat(&mix);
        if (FAILED(hr) || !mix) return R::fail(status::Unknown, (int)hr);
        device_format f;
        f.rate = mix->nSamplesPerSec; f.channels = mix->nChannels; f.bits_per_sample = mix->wBitsPerSample;
        if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22) {
            auto* wefx = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix);
            f.channel_mask = wefx->dwChannelMask;
        }
        f.valid = true;
        ::CoTaskMemFree(mix);
        return R::success(std::move(f));
    }

        static result<double> get_peak_meter(const knst_audio_device& d) noexcept {
        using R = result<double>;

        detail_win::com_initializer com;


        {
            static const IID iid_meter = {
                0xC02216F6, 0x8C67, 0x4B5B,
                { 0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x01, 0xE0 }
            };
            auto dev = detail_win::open_device(d);
            if (!dev) return R::fail(dev.st, dev.native);

            void* raw = nullptr;
            HRESULT hr = dev.value->Activate(iid_meter, CLSCTX_ALL,
                                              nullptr, &raw);
            if (SUCCEEDED(hr) && raw) {
                float peak = 0.0f;
                typedef HRESULT (__stdcall *Fn)(IUnknown*, float*);
                Fn get_peak = reinterpret_cast<Fn>(((void**)raw)[3]);
                hr = get_peak(reinterpret_cast<IUnknown*>(raw), &peak);
                reinterpret_cast<IUnknown*>(raw)->Release();
                if (SUCCEEDED(hr)) return R::success((double)peak);
            }
        }

       
        {
            auto dev = detail_win::open_device(d);
            if (!dev) return R::fail(dev.st, dev.native);

            detail_win::com_ptr<IAudioClient> client;
            HRESULT hr = dev.value->Activate(__uuidof(IAudioClient),
                                              CLSCTX_ALL, nullptr,
                                              reinterpret_cast<void**>(client.addr()));
            if (FAILED(hr) || !client) return R::fail(status::Unknown, (int)hr);

            WAVEFORMATEX* mix = nullptr;
            hr = client->GetMixFormat(&mix);
            if (FAILED(hr) || !mix) return R::fail(status::Unknown, (int)hr);

            const WORD channels = mix->nChannels;
            bool is_float32 = false;
            if (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                is_float32 = (mix->wBitsPerSample == 32);
            } else if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22) {
                auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix);
                is_float32 = (mix->wBitsPerSample == 32)
                          && IsEqualGUID(ext->SubFormat, detail_wasapi::subtype_float);
            }

            // 50ms buffer, shared mode, LOOPBACK flag
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_LOOPBACK,
                                     500000, 0, mix, nullptr);
            ::CoTaskMemFree(mix);
            if (FAILED(hr)) return R::fail(status::NotSupported, (int)hr);

            detail_win::com_ptr<IAudioCaptureClient> cap;
            hr = client->GetService(__uuidof(IAudioCaptureClient),
                                     reinterpret_cast<void**>(cap.addr()));
            if (FAILED(hr) || !cap) return R::fail(status::Unknown, (int)hr);

            hr = client->Start();
            if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);

            float peak = 0.0f;
            const DWORD deadline = ::GetTickCount() + 200;  // hard timeout
            while (::GetTickCount() < deadline) {
                UINT32 pkt = 0;
                hr = cap->GetNextPacketSize(&pkt);
                if (FAILED(hr)) break;
                if (pkt == 0) { ::Sleep(5); continue; }

                BYTE*  data   = nullptr;
                UINT32 frames = 0;
                DWORD  flags  = 0;
                hr = cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;

                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && is_float32) {
                    const float* f = reinterpret_cast<const float*>(data);
                    const UINT32 samples = frames * channels;
                    for (UINT32 i = 0; i < samples; ++i) {
                        float v = std::fabs(f[i]);
                        if (v > peak) peak = v;
                    }
                }
                cap->ReleaseBuffer(frames);
            }
            client->Stop();
            return R::success((double)peak);
        }
    }
    /// Plays a file: default device → MCI (any format the OS knows); named device
    /// ->> native decode + WASAPI stream.
    static result<void> play_file(const knst_audio_device* device, const knst_c16string& path,uint32_t timeout_ms = 600000) noexcept {
        using R = result<void>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        if (!device) return detail_win::mci_play(abs, timeout_ms);
        // Specific device: decode + stream
        auto dec = decode_file(abs);
        if (!dec) return R::fail(dec.st, dec.native);
        stream::config c;
        c.rate = dec.value.rate; c.channels = dec.value.channels; c.is_output = true;
        auto s = stream::open(*device, c);
        if (!s) return R::fail(s.st, s.native);
        const uint32_t total = dec.value.frames();
        const uint32_t chunk = 2048;
        for (uint32_t pos = 0; pos < total; pos += chunk) {
            uint32_t n = total - pos < chunk ? total - pos : chunk;
            auto w = s.value.write_interleaved(
                dec.value.samples.data() + (size_t)pos * c.channels, n);
            if (!w) return R::fail(w.st, w.native);
        }
        s.value.drain();
        return R::success();
    }

    static result<void> play_raw(const knst_audio_device* device,
                                  const knst_byte_string& pcm,
                                  const play_options& opt = play_options(),
                                  uint32_t = 600000) noexcept {
        using R = result<void>;
        if (pcm.empty() || opt.bits_per_sample != 16) return R::fail(status::InvalidArgument);
        if (device && device->is_input) return R::fail(status::InvalidArgument);
        stream::config c;
        c.rate = opt.rate; c.channels = opt.channels; c.is_output = true;
        auto s = device ? stream::open(*device, c) : stream::open_default(c);
        if (!s) return R::fail(s.st, s.native);
        const uint32_t total_frames = (uint32_t)pcm.length() / (opt.channels * 2);
        const uint32_t chunk = 2048;
        knst_vector<float> tmp;
        if (!adetail::resize_ok(tmp, chunk * opt.channels)) return R::fail(status::OutOfMemory);
        const uint8_t* src = (const uint8_t*)pcm.data();
        for (uint32_t pos = 0; pos < total_frames; pos += chunk) {
            uint32_t n = total_frames - pos < chunk ? total_frames - pos : chunk;
            for (uint32_t i = 0; i < n * opt.channels; ++i) {
                size_t off = ((size_t)pos * opt.channels + i) * 2;
                int16_t v = (int16_t)(src[off] | (src[off + 1] << 8));
                tmp[i] = v / 32768.0f;
            }
            auto w = s.value.write_interleaved(tmp.data(), n);
            if (!w) return R::fail(w.st, w.native);
        }
        s.value.drain();
        return R::success();
    }

    static result<knst_vector<stream_info>> list_sink_inputs() noexcept {
        return detail_win_session::list(true);
    }
    static result<knst_vector<stream_info>> list_source_outputs() noexcept {
        return detail_win_session::list(false);
    }
    static result<knst_vector<stream_info>> list_streams() noexcept {
        using R = result<knst_vector<stream_info>>;
        auto in  = detail_win_session::list(true);
        auto out = detail_win_session::list(false);
        if (!in) return in;
        if (!out) return in;    // capture enum failed — return render list only
        auto merged = std::move(in.value);
        for (auto& s : out.value) merged.push_back(std::move(s));
        return R::success(std::move(merged));
    }
    static result<void> set_stream_volume(uint32_t idx, uint32_t percent, bool is_output = true) noexcept {
        if (!is_output) return result<void>::fail(status::NotSupported);
        return detail_win_session::set_volume(idx, percent);
    }
    static result<void> set_stream_mute(uint32_t idx, bool mute, bool is_output = true) noexcept {
        if (!is_output) return result<void>::fail(status::NotSupported);
        return detail_win_session::set_mute(idx, mute);
    }
    static result<void> move_stream(uint32_t, uint32_t, bool = true) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> cork_stream(uint32_t, bool, bool = true) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> suspend(const knst_audio_device&, bool) noexcept { return result<void>::fail(status::NotSupported); }
    static result<knst_audio_device> get_device_detail(const knst_audio_device& d) noexcept {
        using R = result<knst_audio_device>;
        knst_audio_device out = d;
        auto v = get_volume(d); if (v) out.volume_percent = v.value;
        auto m = is_muted(d);   if (m) out.muted = m.value;
        return R::success(std::move(out));
    }
    static result<knst_vector<knst_c16string>> get_ports(const knst_audio_device&) noexcept { return result<knst_vector<knst_c16string>>::fail(status::NotSupported); }
    static result<void> set_port(const knst_audio_device&, const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<knst_vector<card_info>> list_cards() noexcept { return result<knst_vector<card_info>>::fail(status::NotSupported); }
    static result<void> set_profile(uint32_t, const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<server_info> server() noexcept { return result<server_info>::fail(status::NotSupported); }
    static result<uint32_t> get_balance(const knst_audio_device&) noexcept { return result<uint32_t>::fail(status::NotSupported); }
    static result<void> set_balance(const knst_audio_device&, uint32_t) noexcept { return result<void>::fail(status::NotSupported); }
};

#endif // Windows

// ============================================================
// High-level playback (cross-platform, native decoders)
// ============================================================

/// Plays a decoded buffer through a blocking stream (rate/channel conversion is done by the OS mixer).
/// `device` == nullptr → default output. `stop` (optional) is polled between chunks for cancellation.
inline result<void> play_buffer(const knst_audio_device* device, const pcm_buffer& b,
                                const std::atomic<bool>* stop = nullptr) noexcept {
    using R = result<void>;
    if (!b.valid() || b.channels > 8) return R::fail(status::InvalidArgument);
    stream::config c; c.rate = b.rate; c.channels = b.channels; c.is_output = true;
    auto s = device ? stream::open(*device, c) : stream::open_default(c);
    if (!s) return R::fail(s.st, s.native);
    const uint32_t chunk = 2048, total = b.frames();
    for (uint32_t pos = 0; pos < total; pos += chunk) {
        if (stop && stop->load(std::memory_order_relaxed)) return R::success();       // drop remaining audio
        uint32_t n = total - pos < chunk ? total - pos : chunk;
        auto w = s.value.write_interleaved(b.samples.data() + (size_t)pos * b.channels, n);
        if (!w) return R::fail(w.st, w.native);
    }
    s.value.drain();
    return R::success();
}

/// Decodes any supported file and plays it (WAV/AIFF/AU/FLAC natively; MP3/OGG/... via backend).
inline result<void> play_file_any(const knst_audio_device* device, const knst_c16string& path,
                                  const std::atomic<bool>* stop = nullptr) noexcept {
    auto r = decode_file(path);
    if (!r) return result<void>::fail(r.st, r.native);
    return play_buffer(device, r.value, stop);
}

// ============================================================
// Hotplug device notifications
// ============================================================
//
// Unified implementation: background thread polls list_sinks() +
// list_sources() every `interval_ms` and fires events for diffs.
//
//   • 1-second latency (configurable 200ms..60s)
//   • Cross-platform: same code for Linux / Windows / Android
//   • Callback fires on background thread — user MUST be thread-safe
//   • Callback should be fast (do not call audio APIs inside it)

enum class hotplug_event : uint8_t {
    added,           // device appeared (sink or source)
    removed,         // device disappeared
    default_changed  // system default sink/source changed
};

struct device_change {
    hotplug_event  event      = hotplug_event::added;
    bool           is_input   = false;      // true = capture device, false = render
    bool           is_default = false;      // only set for default_changed
    knst_c16string device_id;               // stable id (WASAPI endpoint id / Pulse index / Android id)
    knst_c16string device_name;             // friendly name from description()
};

using hotplug_callback = void (*)(void* user, const device_change& change);

namespace hotplug_detail {

inline void diff_and_fire(const knst_vector<knst_audio_device>& prev,
                          const knst_vector<knst_audio_device>& cur,
                          bool is_input,
                          hotplug_callback cb, void* user) noexcept {
    // Removed: in prev, not in cur (matched by name)
    for (auto& p : prev) {
        bool found = false;
        for (auto& c : cur) if (c.name == p.name) { found = true; break; }
        if (!found) {
            device_change dc;
            dc.event = hotplug_event::removed;
            dc.is_input = is_input;
            dc.device_id = p.name;
            dc.device_name = p.description;
            cb(user, dc);
        }
    }
    // Added: in cur, not in prev
    for (auto& c : cur) {
        bool found = false;
        for (auto& p : prev) if (c.name == p.name) { found = true; break; }
        if (!found) {
            device_change dc;
            dc.event = hotplug_event::added;
            dc.is_input = is_input;
            dc.device_id = c.name;
            dc.device_name = c.description;
            cb(user, dc);
        }
    }
    // Default changed: cur has a default that prev didn't consider default
    for (auto& c : cur) {
        if (!c.is_default) continue;
        bool was_default = false;
        for (auto& p : prev) {
            if (p.name == c.name && p.is_default) { was_default = true; break; }
        }
        if (!was_default) {
            device_change dc;
            dc.event = hotplug_event::default_changed;
            dc.is_input = is_input;
            dc.is_default = true;
            dc.device_id = c.name;
            dc.device_name = c.description;
            cb(user, dc);
        }
    }
}

} // namespace hotplug_detail

class hotplug_monitor {
public:
    /// Starts a background polling thread.
    /// interval_ms: 200 (min, realtime-ish) .. 60000 (minute polling). Default 1000.
    static result<hotplug_monitor> start(hotplug_callback cb, void* user,
                                          uint32_t interval_ms = 1000) noexcept {
        using R = result<hotplug_monitor>;
        if (!cb) return R::fail(status::InvalidArgument);
        if (interval_ms < 200)   interval_ms = 200;
        if (interval_ms > 60000) interval_ms = 60000;

        hotplug_monitor m;
        m.cb_ = cb;
        m.user_ = user;

        auto stop_local = m.stop_;   // shared_ptr copy for thread capture
        auto cb_local = cb;
        auto user_local = user;

        m.thread_ = std::thread([stop_local, cb_local, user_local, interval_ms]() {
            auto prev_sinks   = audio::list_sinks();
            auto prev_sources = audio::list_sources();

            while (!stop_local->load(std::memory_order_relaxed)) {
                // Sleep in small chunks so stop() doesn't wait the full interval.
                uint32_t slept = 0;
                while (slept < interval_ms && !stop_local->load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    slept += 50;
                }
                if (stop_local->load(std::memory_order_relaxed)) break;

                auto cur_sinks   = audio::list_sinks();
                auto cur_sources = audio::list_sources();

                if (prev_sinks && cur_sinks)
                    hotplug_detail::diff_and_fire(prev_sinks.value, cur_sinks.value, false, cb_local, user_local);
                if (prev_sources && cur_sources)
                    hotplug_detail::diff_and_fire(prev_sources.value, cur_sources.value, true, cb_local, user_local);

                if (cur_sinks)   prev_sinks = cur_sinks;
                if (cur_sources) prev_sources = cur_sources;
            }
        });

        return R::success(std::move(m));
    }

    ~hotplug_monitor() { stop(); }

    hotplug_monitor(hotplug_monitor&& o) noexcept
        : thread_(std::move(o.thread_)), stop_(std::move(o.stop_)),
          cb_(o.cb_), user_(o.user_) {
        o.cb_ = nullptr;
        o.user_ = nullptr;
    }
    hotplug_monitor& operator=(hotplug_monitor&& o) noexcept {
        if (this != &o) {
            stop();
            thread_ = std::move(o.thread_);
            stop_ = std::move(o.stop_);
            cb_ = o.cb_; user_ = o.user_;
            o.cb_ = nullptr; o.user_ = nullptr;
        }
        return *this;
    }
    hotplug_monitor(const hotplug_monitor&) = delete;
    hotplug_monitor& operator=(const hotplug_monitor&) = delete;

    /// Stops the background thread. Safe to call multiple times.
    void stop() noexcept {
        if (stop_) stop_->store(true, std::memory_order_relaxed);
        if (thread_.joinable()) thread_.join();
    }

    bool is_running() const noexcept {
        return stop_ && !stop_->load(std::memory_order_relaxed);
    }


    public:
    // Public default ctor — result<hotplug_monitor>::fail() requires
    // T value{} default construction. Unstarted monitor is a safe state.
    hotplug_monitor() noexcept
        : stop_(std::make_shared<std::atomic<bool>>(false)) {}

private:

    std::thread                        thread_;
    std::shared_ptr<std::atomic<bool>> stop_;
    hotplug_callback                   cb_ = nullptr;
    void*                              user_ = nullptr;
};

// ============================================================
// JSON bridge
// ============================================================

#if defined(KNST_JSON_HPP)

inline knst_json devices_to_json(const knst_vector<knst_audio_device>& devices) noexcept {
    knst_json arr = knst_json::make_array();
    if (!arr.is_array()) return knst_json();
    for (uint32_t i = 0; i < devices.size(); ++i) {
        knst_json d = knst_json::make_object();
        if (!d.is_object()) continue;
        d.set("index",       knst_json((int64_t)devices[i].index));
        d.set("name",        knst_json(devices[i].name));
        d.set("description", knst_json(devices[i].description));
        d.set("is_input",    knst_json(devices[i].is_input));
        d.set("is_output",   knst_json(devices[i].is_output));
        d.set("is_default",  knst_json(devices[i].is_default));
        arr.push_back(std::move(d));
    }
    return arr;
}

inline knst_json stream_info_to_json(const knst_vector<stream_info>& streams) noexcept {
    knst_json arr = knst_json::make_array();
    if (!arr.is_array()) return knst_json();
    for (uint32_t i = 0; i < streams.size(); ++i) {
        knst_json s = knst_json::make_object();
        if (!s.is_object()) continue;
        s.set("index",       knst_json((int64_t)streams[i].index));
        s.set("application", knst_json(streams[i].application));
        s.set("media_name",  knst_json(streams[i].media_name));
        s.set("volume",      knst_json((int64_t)streams[i].volume_percent));
        s.set("muted",       knst_json(streams[i].muted));
        s.set("corked",      knst_json(streams[i].corked));
        s.set("is_output",   knst_json(streams[i].is_output));
        arr.push_back(std::move(s));
    }
    return arr;
}

/// media_info → JSON (for UIs / tooling).
inline knst_json media_info_to_json(const media_info& m) noexcept {
    knst_json o = knst_json::make_object();
    if (!o.is_object()) return knst_json();
    o.set("container", knst_json(container_name(m.container)));
    o.set("rate",      knst_json((int64_t)m.rate));
    o.set("channels",  knst_json((int64_t)m.channels));
    o.set("bits",      knst_json((int64_t)m.bits));
    o.set("frames",    knst_json((int64_t)m.frames));
    o.set("duration",  knst_json(m.duration));
    o.set("native",    knst_json(m.native));
    return o;
}

#endif // KNST_JSON_HPP

} // namespace knst_aud