// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_converter.hpp
----------------------------
    Media converter / extractor / muxer built on knst_audio.hpp + knst_video.hpp.
    Include AFTER those two headers (KernelNucleusT.hpp does this).

    WHAT IT DOES
    ============
      AUDIO  any decodable audio file  ->  WAV / AIFF / AU / RAW (u8..f64, A-law, mu-law)
             + sample-rate, channel (5.1 downmix etc), gain, normalize, DC removal, fades, limiter, trim
      EXTRACT audio from AVI / MKV / MP4 / MOV:
             - PCM tracks          -> decoded to WAV / AIFF / AU / RAW
             - MP3 tracks          -> .mp3   (bit-exact stream copy)
             - AAC tracks          -> .aac   (ADTS framing generated from AudioSpecificConfig)
             - any track           -> .mka   (Matroska audio, stream copy)
      VIDEO  container conversion AVI <-> MKV <-> MP4 <-> MOV
             - stream copy (no re-encode) when the target container can hold the codec
             - H.264 / H.265 length-prefixed (MP4/MKV) <-> Annex-B (AVI) rewriting
             - transcode to MJPEG / RAW: resize, crop, rotate, flip, brightness/contrast, grey, fps change, quality
             - time range cut (copy mode cuts at the previous key frame; transcode is frame accurate)
      MUX    add / replace audio of a video (external WAV/FLAC/MP3/AAC/... or the track of another video),
             drop audio, keep audio, PCM re-formatting, A/V offset
      FRAMES save one frame or a frame sequence as JPEG / PPM
      PROBE  one call describing any supported file

    WHAT IT CANNOT DO (honest list, inherited from knst_audio / knst_video)
    =======================================================================
      * No H.264/H.265/VP9/AV1/AAC/Opus/MP3/FLAC/Vorbis ENCODER or DECODER exists in the library.
        - "MP4 -> MP3" works only if the MP4 already contains MP3 audio (stream copy).
          AAC -> MP3 needs a real encoder: register one with converter::register_audio_encoder().
        - Re-encoding video to H.264 etc is not possible. Transcoding targets are MJPEG and RAW (AVI).
        - Decoding non-MJPEG video (for resize, frame grabs, ...) needs vid::external_codec::register_video_decoder().
      * MP4 writer has no audio support, MOV only PCM_S16LE, AVI only PCM. Container choice for audio:
        MKV holds everything. The converter tells you when audio had to be converted or dropped (report.notes).
      * WebM cannot be written (video_writer limitation) - write .mkv, it accepts the same streams.
      * HE-AAC (SBR/PS) to ADTS is rejected (AAC-LC / Main / SSR / LTP only).
      * Audio is decoded fully into RAM as float for processing (limit: 2^30 samples, see knst_audio).

    DESIGN
    ======
      Synchronous, no exceptions, no RTTI use, every public call returns result<T>.
      On failure converter::last_error() / last_message() explain what and why.
      Progress + cancellation: converter::set_progress(), converter::set_cancel().
      Partial output files are removed on failure.

    EXAMPLES BASICS BRO
    ========
        knst_conv::converter cv;

        cv.convert(u"clip.mp4", u"clip.mkv");                           // auto: stream copy remux
        cv.convert(u"voice.flac", u"voice.wav");                        // auto: decode + encode

        knst_conv::audio_options ao;  ao.sample_rate = 16000; ao.channels = 1; ao.normalize = true;
        cv.convert_audio(u"movie.mkv", u"speech.wav", ao);              // extract + process

        cv.convert_audio(u"song.mp4", u"song.mp3");                     // only works if MP4 carries MP3

        knst_conv::video_options vo;  vo.filters.out_w = 640;  vo.jpeg_quality = 80;
        vo.start_s = 10; vo.end_s = 25;
        cv.convert_video(u"in.mkv", u"small.avi", vo);                  // transcode to MJPEG AVI

        cv.add_audio(u"silent.mp4", u"music.wav", u"out.mov");          // put a sound track on a video
        cv.strip_audio(u"in.mkv", u"mute.mkv");
        cv.extract_frame(u"in.mp4", u"thumb.jpg", 5.0);
        cv.extract_frames(u"in.mkv", u"frames/", 1.0);                  // one frame per second

        auto r = cv.convert(u"a.avi", u"b.mp4");
        if (!r) printf("%s\n", reinterpret_cast<const char*>(knst_byte_string(cv.last_message()).data()));
*/

#pragma once

// Explicit dependencies — knst_converter.hpp relies on knst_video.hpp and
// knst_audio.hpp being fully defined (namespace knst_vid / knst_aud). Without
// these, the vid:: alias resolves to an incomplete forward declaration and
// half the header fails to compile. Order matters: devices → file →
// image_loader → video → audio.
#include "knst_devices.hpp"
#include "knst_file.hpp"
#include "knst_image_loader.hpp"
#include "knst_video.hpp"
#include "knst_audio.hpp"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <utility>
#include <algorithm>

namespace knst_conv {

namespace vid = ::knst_vid;
namespace aud = ::knst_aud;
namespace img = ::knst_vid::img;

using knst_dev::status;
using knst_dev::result;
using vid::errc;

// ============================================================
// Public types
// ============================================================

enum class format_t : uint8_t {
    unknown,
    avi, mkv, webm, mp4, mov,                 // A/V containers
    wav, aiff, au, flac, ogg, mp3, aac,       // audio files
    raw_pcm, mka,
    jpeg, ppm                                 // pictures
};

inline const char* format_name(format_t f) noexcept {
    switch (f) {
        case format_t::avi: return "avi";     case format_t::mkv: return "mkv";
        case format_t::webm: return "webm";   case format_t::mp4: return "mp4";
        case format_t::mov: return "mov";     case format_t::wav: return "wav";
        case format_t::aiff: return "aiff";   case format_t::au: return "au";
        case format_t::flac: return "flac";   case format_t::ogg: return "ogg";
        case format_t::mp3: return "mp3";     case format_t::aac: return "aac (adts)";
        case format_t::raw_pcm: return "raw pcm"; case format_t::mka: return "mka";
        case format_t::jpeg: return "jpeg";   case format_t::ppm: return "ppm";
        default: return "unknown";
    }
}

inline bool is_av_container(format_t f) noexcept {
    return f == format_t::avi || f == format_t::mkv || f == format_t::webm || f == format_t::mp4 || f == format_t::mov;
}

inline vid::container to_vid_container(format_t f) noexcept {
    switch (f) {
        case format_t::avi: return vid::container::AVI;   case format_t::mkv: return vid::container::MKV;
        case format_t::webm: return vid::container::WebM; case format_t::mp4: return vid::container::MP4;
        case format_t::mov: return vid::container::MOV;   default: return vid::container::Unknown;
    }
}

namespace cdetail {

inline void ext_lower(const knst_c16string& path, char (&buf)[16]) noexcept {
    buf[0] = 0;
    uint32_t dot = UINT32_MAX;
    for (uint32_t i = path.length(); i > 0; --i) {
        char16_t c = path[i - 1];
        if (c == u'.') { dot = i - 1; break; }
        if (c == u'/' || c == u'\\') break;
    }
    if (dot == UINT32_MAX) return;
    uint32_t k = 0;
    for (uint32_t i = dot + 1; i < path.length() && k < 15; ++i) {
        char16_t c = path[i];
        if (c >= u'A' && c <= u'Z') c = (char16_t)(c + 32);
        if (c > 127) { k = 0; break; }
        buf[k++] = (char)c;
    }
    buf[k] = 0;
}

inline uint64_t sec_us(double s) noexcept { return s <= 0.0 ? 0ull : (uint64_t)(s * 1e6 + 0.5); }

inline uint64_t file_size_of(const knst_c16string& p) noexcept {
    vid::detail::file_handle f;
    if (f.open(p, vid::detail::file_handle::mode::read) != errc::Ok) return 0;
    return f.size();
}

inline bool write_bytes(const knst_c16string& path, const uint8_t* d, size_t n) noexcept {
    if (!knst_file::ensure_parent_dir(path)) return false;
    vid::detail::file_handle f;
    if (f.open(path, vid::detail::file_handle::mode::write) != errc::Ok) return false;
    if (n && !f.write(d, n)) { f.close(); return false; }
    return f.close();
}

inline errc from_status(status s) noexcept {
    if (s == status::InvalidArgument) return errc::InvalidArgument;
    if (s == status::NotSupported) return errc::NotSupported;
    if (s == status::NotFound) return errc::NotFound;
    if (s == status::OutOfMemory) return errc::OutOfMemory;
    if (s == status::BadState) return errc::BadState;
    return errc::IoError;
}

inline int nth_stream(const vid::video_reader& rd, vid::track_kind k, uint32_t nth) noexcept {
    for (uint32_t i = 0; i < rd.stream_count(); ++i)
        if (rd.stream(i).kind == k) { if (nth == 0) return (int)i; --nth; }
    return -1;
}

inline vid::audio_codec codec_for_sample(aud::sample_fmt s) noexcept {
    switch (s) {
        case aud::sample_fmt::u8:  return vid::audio_codec::PCM_U8;
        case aud::sample_fmt::i24: return vid::audio_codec::PCM_S24LE;
        case aud::sample_fmt::i32: return vid::audio_codec::PCM_S32LE;
        case aud::sample_fmt::f32: return vid::audio_codec::PCM_F32LE;
        default: return vid::audio_codec::PCM_S16LE;
    }
}

// libsndfile format constants for encode targets we cannot write natively.
// 0 = "not supported / needs libsndfile"; caller falls back to enc_registry.
inline int sndfile_format_for(format_t f, aud::sample_fmt s) noexcept {
    int sub;
    switch (s) {
        case aud::sample_fmt::u8:   sub = 0x0005; break;   // SF_FORMAT_PCM_U8
        case aud::sample_fmt::i16:  sub = 0x0002; break;   // SF_FORMAT_PCM_16
        case aud::sample_fmt::i24:  sub = 0x0003; break;   // SF_FORMAT_PCM_24
        case aud::sample_fmt::i32:  sub = 0x0004; break;   // SF_FORMAT_PCM_32
        case aud::sample_fmt::f32:  sub = 0x0006; break;   // SF_FORMAT_FLOAT
        case aud::sample_fmt::f64:  sub = 0x0007; break;   // SF_FORMAT_DOUBLE
        case aud::sample_fmt::alaw: sub = 0x0011; break;   // SF_FORMAT_ALAW
        case aud::sample_fmt::ulaw: sub = 0x0010; break;   // SF_FORMAT_ULAW
        default:                    sub = 0x0002; break;
    }
    switch (f) {
        case format_t::flac:
            // libsndfile FLAC supports only PCM_8 / PCM_16 / PCM_24.
            if (sub == 0x0004 || sub == 0x0006 || sub == 0x0007) sub = 0x0002;
            return 0x170000 | sub;
        case format_t::ogg:
            return 0x200000 | 0x0060;                      // SF_FORMAT_VORBIS
        case format_t::aiff:
            return 0x020000 | sub;
        case format_t::wav:
            return 0x010000 | sub;
        case format_t::au:
            return 0x030000 | sub;
        default:
            return 0;                                      // not a sndfile target
    }
}

} // namespace cdetail

inline format_t format_from_path(const knst_c16string& p) noexcept {
    char e[16]; cdetail::ext_lower(p, e);
    auto is = [&](const char* s) { return std::strcmp(e, s) == 0; };
    if (is("avi")) return format_t::avi;
    if (is("mkv")) return format_t::mkv;
    if (is("webm")) return format_t::webm;
    if (is("mp4") || is("m4v") || is("m4a") || is("m4b")) return format_t::mp4;
    if (is("mov") || is("qt")) return format_t::mov;
    if (is("wav") || is("wave") || is("bwf")) return format_t::wav;
    if (is("aif") || is("aiff") || is("aifc")) return format_t::aiff;
    if (is("au") || is("snd")) return format_t::au;
    if (is("flac")) return format_t::flac;
    if (is("ogg") || is("oga") || is("opus")) return format_t::ogg;
    if (is("mp3")) return format_t::mp3;
    if (is("aac")) return format_t::aac;
    if (is("mka")) return format_t::mka;
    if (is("raw") || is("pcm")) return format_t::raw_pcm;
    if (is("jpg") || is("jpeg")) return format_t::jpeg;
    if (is("ppm")) return format_t::ppm;
    return format_t::unknown;
}

/// Can `c` store `v` as an untouched packet stream (this library's writers)?
inline bool container_supports_video(vid::container c, vid::video_codec v) noexcept {
    using VC = vid::video_codec;
    switch (c) {
        case vid::container::AVI:
            return v == VC::MJPEG || v == VC::RAW || v == VC::H264 || v == VC::H265 || v == VC::VP8 ||
                   v == VC::VP9 || v == VC::AV1 || v == VC::MPEG4;
        case vid::container::MKV: case vid::container::WebM:
            return v != VC::RAW && v != VC::DNxHD && v != VC::Unknown;
        case vid::container::MP4: case vid::container::MOV:
            return v == VC::MJPEG || v == VC::H264 || v == VC::H265;
        default: return false;
    }
}

inline bool container_supports_audio(vid::container c, vid::audio_codec a) noexcept {
    using AC = vid::audio_codec;
    if (a == AC::None || a == AC::Unknown) return false;
    switch (c) {
        case vid::container::AVI: return vid::audio_codec_is_pcm(a);
        case vid::container::MKV: case vid::container::WebM: return true;
        case vid::container::MOV: return a == AC::PCM_S16LE;
        default: return false;                      // MP4 writer: video only
    }
}

struct report {
    bool     ok = false;
    bool     video_copied = false;      // video packets were not re-encoded
    bool     audio_copied = false;      // audio packets/bytes were not re-encoded
    uint64_t video_frames = 0;
    uint64_t audio_frames = 0;
    uint64_t bytes_out = 0;
    double   seconds = 0.0;
    format_t out_format = format_t::unknown;
    knst_vector<knst_c16string> notes;  // decisions and warnings ("audio dropped: ...", "cut at key frame", ...)
};

struct input_info {
    format_t format = format_t::unknown;
    bool     av_container = false;
    bool     has_video = false;
    bool     has_audio = false;
    double   duration_s = 0.0;
    vid::media_info av;                 // valid when av_container
    aud::media_info au;                 // valid for audio-only files
};

typedef bool (*progress_fn)(void* user, double fraction, const char* stage);   // return false = cancel

struct audio_options {
    format_t format = format_t::unknown;                 // unknown -> from output extension
    aud::sample_fmt sample = aud::sample_fmt::i16;       // output sample format (WAV/AIFF/AU/RAW)
    uint32_t sample_rate = 0;                            // 0 = keep
    uint32_t channels = 0;                               // 0 = keep (e.g. 2 = downmix 5.1)
    aud::dsp::resample_quality quality = aud::dsp::resample_quality::high;
    double start_s = 0.0, end_s = 0.0;                   // end 0 = until the end
    float gain_db = 0.0f;
    bool  normalize = false; float normalize_peak_db = -1.0f;
    bool  remove_dc = false;
    float fade_in_ms = 0.0f, fade_out_ms = 0.0f;
    bool  limiter = false; float limiter_db = -1.0f;
    bool  dither = true;                                 // TPDF, only when it makes sense (<=16 bit and bit-depth reduction)
    bool  allow_copy = true;                             // stream copy when codec already matches (mp3->mp3, ...)
    bool  raw_big_endian = false;                        // RAW output only
    uint32_t track = 0;                                  // n-th audio track of a container
    aud::decode_options decode;                          // RAW input parameters etc.

    bool needs_dsp() const noexcept {
        return sample_rate || channels || gain_db != 0.0f || normalize || remove_dc ||
               fade_in_ms > 0.0f || fade_out_ms > 0.0f || limiter;
    }
};

struct video_filters {
    uint32_t crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;     // crop_w/h == 0: no crop (applied first)
    uint32_t rotate_deg = 0;                                     // 0 / 90 / 180 / 270 (clockwise)
    bool flip_h = false, flip_v = false;
    uint32_t out_w = 0, out_h = 0;                               // one set -> keep aspect, both -> exact
    double   scale = 0.0;                                        // alternative to out_w/out_h
    img::resize_filter filter = img::resize_filter::bilinear;
    bool     auto_filter = true;                                 // box when shrinking, bilinear when enlarging
    float brightness = 0.0f, contrast = 1.0f;
    bool grey = false;

    bool active() const noexcept {
        return (crop_w && crop_h) || (rotate_deg % 360) || flip_h || flip_v || out_w || out_h ||
               (scale > 0.0 && scale != 1.0) || brightness != 0.0f || contrast != 1.0f || grey;
    }
};

enum class video_mode : uint8_t { automatic, copy, transcode };
enum class audio_mode : uint8_t { automatic, copy, to_pcm, drop };

struct video_options {
    format_t   container = format_t::unknown;            // unknown -> from output extension
    video_mode vmode = video_mode::automatic;
    audio_mode amode = audio_mode::automatic;
    vid::video_codec vcodec = vid::video_codec::Unknown; // transcode target: MJPEG (default) or RAW (AVI only)
    int  jpeg_quality = 0;                               // 0 = 85 when transcoding, keep frames when MJPEG->MJPEG copy
    vid::mjpeg::subsampling jpeg_sub = vid::mjpeg::subsampling::s420;
    video_filters filters;
    double fps = 0.0;                                    // 0 = keep (transcode only)
    double start_s = 0.0, end_s = 0.0;                   // time range
    audio_options audio;                                 // used when audio must be decoded/processed (rate, channels, gain, ...)
    vid::audio_codec pcm_codec = vid::audio_codec::Unknown; // Unknown = keep source PCM format if the container allows, else S16
    knst_c16string external_audio;                       // replace the audio by this file (audio file or another video)
    double external_audio_offset_ms = 0.0;               // >0 delays the external audio, <0 advances it
    uint32_t audio_track = 0;
    bool strict = false;                                 // fail instead of dropping audio
    knst_c16string title;                                // override the title metadata
};

typedef result<void> (*audio_encoder_fn)(void* user, const aud::pcm_buffer& pcm, const knst_c16string& path);

// ============================================================
// Bitstream helpers (H.264/H.265 framing, AAC/ADTS, MP3 frames)
// ============================================================

namespace bs {

struct nal_range { uint32_t off; uint32_t len; };
struct param_sets { knst_vector<knst_byte_string> sets; uint32_t len_size = 4; };

inline bool is_annexb(const uint8_t* d, uint32_t n) noexcept {
    return n >= 4 && d[0] == 0 && d[1] == 0 && (d[2] == 1 || (d[2] == 0 && d[3] == 1));
}

inline void split_annexb(const uint8_t* d, uint32_t n, knst_vector<nal_range>& out) noexcept {
    int64_t start = -1; uint32_t i = 0;
    while (i + 3 <= n) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            if (start >= 0) {
                uint32_t end = i;
                while (end > (uint32_t)start && d[end - 1] == 0) --end;
                if (end > (uint32_t)start) { nal_range r; r.off = (uint32_t)start; r.len = end - (uint32_t)start; out.push_back(r); }
            }
            start = (int64_t)i + 3; i += 3;
        } else ++i;
    }
    if (start >= 0 && (uint32_t)start < n) { nal_range r; r.off = (uint32_t)start; r.len = n - (uint32_t)start; out.push_back(r); }
}

inline bool parse_avcc(const knst_byte_string& x, param_sets& ps) noexcept {
    const uint8_t* p = x.data(); size_t n = x.length();
    if (n < 7 || p[0] != 1) return false;
    ps.len_size = (uint32_t)(p[4] & 3) + 1;
    size_t pos = 5; uint32_t ns = p[pos++] & 0x1F;
    for (uint32_t i = 0; i < ns; ++i) {
        if (pos + 2 > n) return false;
        uint32_t l = vid::detail::get_u16be(p + pos); pos += 2;
        if (pos + l > n) return false;
        ps.sets.push_back(knst_byte_string(p + pos, l)); pos += l;
    }
    if (pos >= n) return !ps.sets.empty();
    uint32_t np = p[pos++];
    for (uint32_t i = 0; i < np; ++i) {
        if (pos + 2 > n) return false;
        uint32_t l = vid::detail::get_u16be(p + pos); pos += 2;
        if (pos + l > n) return false;
        ps.sets.push_back(knst_byte_string(p + pos, l)); pos += l;
    }
    return !ps.sets.empty();
}

inline bool parse_hvcc(const knst_byte_string& x, param_sets& ps) noexcept {
    const uint8_t* p = x.data(); size_t n = x.length();
    if (n < 23 || p[0] != 1) return false;
    ps.len_size = (uint32_t)(p[21] & 3) + 1;
    uint32_t arrays = p[22]; size_t pos = 23;
    for (uint32_t a = 0; a < arrays; ++a) {
        if (pos + 3 > n) return false;
        ++pos;                                                   // array_completeness | type
        uint32_t cnt = vid::detail::get_u16be(p + pos); pos += 2;
        for (uint32_t i = 0; i < cnt; ++i) {
            if (pos + 2 > n) return false;
            uint32_t l = vid::detail::get_u16be(p + pos); pos += 2;
            if (pos + l > n) return false;
            ps.sets.push_back(knst_byte_string(p + pos, l)); pos += l;
        }
    }
    return !ps.sets.empty();
}

/// Length-prefixed (MP4/MKV) -> Annex-B. `pre` (parameter sets) is prepended when non-null (use on key frames).
inline errc lp_to_annexb(const uint8_t* d, uint32_t n, uint32_t ls, const param_sets* pre, knst_byte_string& out) noexcept {
    static const uint8_t sc[4] = { 0, 0, 0, 1 };
    if (ls < 1 || ls > 4) return errc::Corrupt;
    uint64_t total = 0;
    if (pre) for (uint32_t i = 0; i < pre->sets.size(); ++i) total += 4 + pre->sets[i].length();
    uint64_t pos = 0;
    while (pos + ls <= n) {
        uint32_t len = 0; for (uint32_t k = 0; k < ls; ++k) len = (len << 8) | d[pos + k];
        pos += ls;
        if (pos + len > n) return errc::Corrupt;
        total += 4 + (uint64_t)len; pos += len;
    }
    if (pos != n) return errc::Corrupt;
    errc e = vid::detail::alloc_bytes(out, total);
    if (e != errc::Ok) return e;
    if (total == 0) return errc::Ok;
    uint8_t* o = &out[0]; size_t w = 0;
    if (pre) for (uint32_t i = 0; i < pre->sets.size(); ++i) {
        std::memcpy(o + w, sc, 4); w += 4;
        std::memcpy(o + w, pre->sets[i].data(), pre->sets[i].length()); w += pre->sets[i].length();
    }
    pos = 0;
    while (pos + ls <= n) {
        uint32_t len = 0; for (uint32_t k = 0; k < ls; ++k) len = (len << 8) | d[pos + k];
        pos += ls;
        std::memcpy(o + w, sc, 4); w += 4;
        std::memcpy(o + w, d + pos, len); w += len; pos += len;
    }
    return errc::Ok;
}

inline bool is_param_nal(const uint8_t* nal, bool h265) noexcept {
    if (h265) { uint8_t t = (uint8_t)((nal[0] >> 1) & 0x3F); return t >= 32 && t <= 34; }
    uint8_t t = nal[0] & 0x1F; return t == 7 || t == 8;
}

/// Annex-B -> 4-byte length-prefixed. SPS/PPS(/VPS) are dropped (they live in avcC/hvcC).
inline errc annexb_to_lp(const uint8_t* d, uint32_t n, bool h265, knst_byte_string& out) noexcept {
    knst_vector<nal_range> nals; split_annexb(d, n, nals);
    uint64_t total = 0;
    for (uint32_t i = 0; i < nals.size(); ++i) if (!is_param_nal(d + nals[i].off, h265)) total += 4 + (uint64_t)nals[i].len;
    errc e = vid::detail::alloc_bytes(out, total);
    if (e != errc::Ok) return e;
    if (total == 0) return errc::Ok;
    uint8_t* o = &out[0]; size_t w = 0;
    for (uint32_t i = 0; i < nals.size(); ++i) {
        if (is_param_nal(d + nals[i].off, h265)) continue;
        vid::detail::put_u32be(o + w, nals[i].len); w += 4;
        std::memcpy(o + w, d + nals[i].off, nals[i].len); w += nals[i].len;
    }
    return errc::Ok;
}

inline bool collect_sps_pps(const uint8_t* d, uint32_t n, knst_byte_string& sps, knst_byte_string& pps) noexcept {
    knst_vector<nal_range> nals; split_annexb(d, n, nals);
    for (uint32_t i = 0; i < nals.size(); ++i) {
        uint8_t t = d[nals[i].off] & 0x1F;
        if (t == 7 && sps.empty()) sps = knst_byte_string(d + nals[i].off, nals[i].len);
        else if (t == 8 && pps.empty()) pps = knst_byte_string(d + nals[i].off, nals[i].len);
    }
    return sps.length() >= 4 && !pps.empty();
}

inline knst_byte_string make_avcc(const knst_byte_string& sps, const knst_byte_string& pps) noexcept {
    knst_byte_string a;
    uint8_t h[8] = { 1, sps[1], sps[2], sps[3], 0xFF, 0xE1, (uint8_t)(sps.length() >> 8), (uint8_t)sps.length() };
    a.append(h, 8); a.append(sps.data(), sps.length());
    uint8_t m[3] = { 1, (uint8_t)(pps.length() >> 8), (uint8_t)pps.length() };
    a.append(m, 3); a.append(pps.data(), pps.length());
    return a;
}

// ---- AAC ----

inline const uint32_t* aac_rates() noexcept {
    static const uint32_t r[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    return r;
}

struct asc_info { uint32_t aot = 0, sfi = 0, ch = 0, rate = 0; bool ok = false; };

inline asc_info parse_asc(const uint8_t* p, size_t n) noexcept {
    asc_info a;
    if (!p || n < 2) return a;
    a.aot = p[0] >> 3; a.sfi = ((uint32_t)(p[0] & 7) << 1) | (p[1] >> 7); a.ch = (p[1] >> 3) & 0xF;
    if (a.aot < 1 || a.aot > 4 || a.sfi > 12 || a.ch < 1 || a.ch > 7) return a;
    a.rate = aac_rates()[a.sfi]; a.ok = true;
    return a;
}

inline void adts_header(uint8_t* h, const asc_info& a, uint32_t payload) noexcept {
    const uint32_t len = payload + 7;
    h[0] = 0xFF; h[1] = 0xF1;
    h[2] = (uint8_t)(((a.aot - 1) << 6) | (a.sfi << 2) | (a.ch >> 2));
    h[3] = (uint8_t)(((a.ch & 3) << 6) | (len >> 11));
    h[4] = (uint8_t)((len >> 3) & 0xFF);
    h[5] = (uint8_t)(((len & 7) << 5) | 0x1F);
    h[6] = 0xFC;
}

inline bool find_asc_in_esds(const uint8_t* p, size_t n, const uint8_t*& asc, size_t& alen) noexcept {
    size_t pos = 0;
    while (pos < n) {
        uint8_t tag = p[pos++];
        uint32_t len = 0; bool ok = false;
        for (int i = 0; i < 4 && pos < n; ++i) { uint8_t b = p[pos++]; len = (len << 7) | (b & 0x7F); if (!(b & 0x80)) { ok = true; break; } }
        if (!ok) return false;
        if (tag == 0x03) {
            if (pos + 3 > n) return false;
            uint8_t fl = p[pos + 2]; pos += 3;
            if (fl & 0x80) pos += 2;
            if (fl & 0x40) { if (pos >= n) return false; pos += 1 + p[pos]; }
            if (fl & 0x20) pos += 2;
            continue;
        }
        if (tag == 0x04) { pos += 13; continue; }
        if (tag == 0x05) { if (pos + len > n) return false; asc = p + pos; alen = len; return true; }
        pos += len;
    }
    return false;
}

/// AudioSpecificConfig of an AAC stream_desc (MP4/MOV store it inside esds, MKV/AVI store it directly).
inline bool get_asc(const vid::stream_desc& sd, vid::container k, knst_byte_string& asc) noexcept {
    if (k == vid::container::MP4 || k == vid::container::MOV) {
        const uint8_t* a = nullptr; size_t al = 0;
        if (!find_asc_in_esds(sd.extradata.data(), sd.extradata.length(), a, al) || al < 2) return false;
        asc = knst_byte_string(a, (uint32_t)al); return true;
    }
    if (sd.extradata.length() < 2) return false;
    asc = sd.extradata; return true;
}

// ---- elementary streams (.mp3 / .aac) ----

struct elem_frame { uint32_t off; uint32_t size; uint32_t samples; };

struct elementary {
    vid::audio_codec codec = vid::audio_codec::None;
    uint32_t rate = 0, channels = 0;
    knst_byte_string asc;
    knst_vector<elem_frame> frames;
    uint64_t total_samples = 0;
};

inline uint64_t skip_id3(const uint8_t* d, uint64_t n) noexcept {
    if (n >= 10 && std::memcmp(d, "ID3", 3) == 0)
        return 10 + (((uint64_t)(d[6] & 0x7F) << 21) | ((uint64_t)(d[7] & 0x7F) << 14) | ((uint64_t)(d[8] & 0x7F) << 7) | (d[9] & 0x7F));
    return 0;
}

inline bool mp3_header(const uint8_t* h, uint32_t& flen, uint32_t& rate, uint32_t& ch, uint32_t& spf, int& ver) noexcept {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
    ver = (h[1] >> 3) & 3; int layer = (h[1] >> 1) & 3;
    if (ver == 1 || layer != 1) return false;                          // Layer III only
    int bi = (h[2] >> 4) & 15, si = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
    if (bi == 0 || bi == 15 || si == 3) return false;
    static const uint16_t b1[15] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
    static const uint16_t b2[15] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 };
    static const uint32_t base[3] = { 44100, 48000, 32000 };
    rate = base[si]; if (ver == 2) rate /= 2; else if (ver == 0) rate /= 4;
    const uint32_t br = (ver == 3 ? b1[bi] : b2[bi]) * 1000u;
    flen = (ver == 3 ? 144u : 72u) * br / rate + (uint32_t)pad;
    spf = ver == 3 ? 1152u : 576u;
    ch = ((h[3] >> 6) == 3) ? 1u : 2u;
    return flen >= 24;
}

inline errc scan_mp3(const knst_byte_string& f, elementary& e) noexcept {
    const uint8_t* d = f.data(); const uint64_t n = f.length();
    uint64_t pos = skip_id3(d, n), expected = ~0ull;
    while (pos + 4 <= n) {
        uint32_t len, r, c, spf; int ver;
        if (!mp3_header(d + pos, len, r, c, spf, ver) || pos + len > n) { ++pos; continue; }
        if (e.rate && r != e.rate) { ++pos; continue; }
        if (pos != expected) {                                       // (re)sync: confirm with the next header
            uint32_t l2, r2, c2, s2; int v2;
            if (pos + len + 4 <= n) { if (!mp3_header(d + pos + len, l2, r2, c2, s2, v2) || r2 != r) { ++pos; continue; } }
            else if (pos + len != n) { ++pos; continue; }
        }
        if (e.frames.empty()) {
            const uint32_t so = 4 + (ver == 3 ? (c == 1 ? 17u : 32u) : (c == 1 ? 9u : 17u));
            if (pos + so + 4 <= n && (!std::memcmp(d + pos + so, "Xing", 4) || !std::memcmp(d + pos + so, "Info", 4))) {
                pos += len; expected = pos; continue;                // VBR header frame: not audio
            }
        }
        elem_frame fr; fr.off = (uint32_t)pos; fr.size = len; fr.samples = spf;
        e.frames.push_back(fr);
        e.rate = r; e.channels = c; e.total_samples += spf;
        pos += len; expected = pos;
    }
    if (e.frames.empty()) return errc::Corrupt;
    e.codec = vid::audio_codec::MP3;
    return errc::Ok;
}

inline errc scan_adts(const knst_byte_string& f, elementary& e) noexcept {
    const uint8_t* d = f.data(); const uint64_t n = f.length();
    uint64_t pos = skip_id3(d, n);
    while (pos + 7 <= n) {
        const uint8_t* h = d + pos;
        if (h[0] != 0xFF || (h[1] & 0xF6) != 0xF0) { ++pos; continue; }
        uint32_t sfi = (h[2] >> 2) & 15;
        uint32_t len = ((uint32_t)(h[3] & 3) << 11) | ((uint32_t)h[4] << 3) | (h[5] >> 5);
        uint32_t hdr = (h[1] & 1) ? 7u : 9u;
        if (sfi > 12 || len < hdr || pos + len > n) { ++pos; continue; }
        if (h[6] & 3) return errc::NotSupported;                     // several raw blocks per ADTS frame
        if (e.frames.empty()) {
            uint32_t aot = (uint32_t)(h[2] >> 6) + 1, ch = ((uint32_t)(h[2] & 1) << 2) | (h[3] >> 6);
            uint8_t a[2] = { (uint8_t)((aot << 3) | (sfi >> 1)), (uint8_t)(((sfi & 1) << 7) | (ch << 3)) };
            e.asc = knst_byte_string(a, 2); e.rate = aac_rates()[sfi]; e.channels = ch;
        }
        elem_frame fr; fr.off = (uint32_t)(pos + hdr); fr.size = len - hdr; fr.samples = 1024;
        e.frames.push_back(fr); e.total_samples += 1024;
        pos += len;
    }
    if (e.frames.empty() || e.channels == 0) return errc::Corrupt;
    e.codec = vid::audio_codec::AAC;
    return errc::Ok;
}

} // namespace bs

// ============================================================
// Audio feeds: everything that can deliver audio packets to a writer
// ============================================================

class audio_feed {
public:
    vid::audio_codec codec = vid::audio_codec::None;
    uint32_t rate = 0, channels = 0;
    knst_byte_string extra;                    // CodecPrivate (AAC: AudioSpecificConfig)
    bool is_pcm = false;
    bool copied = false;                       // packets are passed through untouched

    virtual ~audio_feed() {}
    virtual bool has() const noexcept = 0;
    virtual uint64_t pts_us() const noexcept = 0;
    virtual errc pop(knst_byte_string& out) noexcept = 0;
};

/// Packets (or PCM chunks) of an audio track inside an AV container.
class packet_feed : public audio_feed {
public:
    errc init(vid::video_reader* rd, uint32_t track, vid::audio_codec pcm_target, uint64_t start_us, uint64_t end_us) noexcept {
        rd_ = rd; track_ = track; start_ = start_us; end_ = end_us;
        const vid::stream_desc& sd = rd->stream(track);
        src_ = sd.acodec; codec = sd.acodec; rate = sd.sample_rate; channels = sd.channels;
        is_pcm = vid::audio_codec_is_pcm(src_);
        if (is_pcm && pcm_target != vid::audio_codec::Unknown && pcm_target != vid::audio_codec::None) codec = pcm_target;
        if (is_pcm && (!vid::audio_codec_bits(src_) || !channels)) return errc::Corrupt;
        copied = !is_pcm || codec == src_;
        if (src_ == vid::audio_codec::AAC) {
            if (!bs::get_asc(sd, rd->kind(), extra)) return errc::Corrupt;
        } else if (!is_pcm) {
            extra = sd.extradata;
        }
        return fetch_();
    }
    bool has() const noexcept override { return have_; }
    uint64_t pts_us() const noexcept override { return cur_pts_; }
    errc pop(knst_byte_string& out) noexcept override { out = std::move(cur_); return fetch_(); }

private:
    vid::video_reader* rd_ = nullptr; uint32_t track_ = 0; uint64_t idx_ = 0, start_ = 0, end_ = 0;
    vid::audio_codec src_ = vid::audio_codec::None;
    knst_byte_string cur_; uint64_t cur_pts_ = 0; bool have_ = false;

    errc fetch_() noexcept {
        have_ = false;
        for (;;) {
            uint64_t pts = 0;
            auto r = rd_->read_packet(track_, idx_, &pts);
            if (!r) { errc e = rd_->last_error(); return e == errc::NotFound ? errc::Ok : e; }
            ++idx_;
            if (end_ && pts >= end_) return errc::Ok;
            knst_byte_string pk = std::move(r.value);
            if (is_pcm) {
                const uint32_t sb = vid::audio_codec_bits(src_) / 8, align = sb * channels;
                uint64_t frames = pk.length() / align;
                if (frames == 0) continue;
                if (pts + vid::detail::muldiv_u64(frames, 1000000ull, rate ? rate : 1) <= start_) continue;
                if (pk.length() != frames * align) { if (vid::detail::alloc_bytes(pk, frames * align) != errc::Ok) return errc::OutOfMemory; }
                if (codec != src_) {
                    knst_byte_string conv;
                    if (vid::detail::alloc_bytes(conv, frames * channels * (vid::audio_codec_bits(codec) / 8)) != errc::Ok) return errc::OutOfMemory;
                    errc e = vid::audio::convert_pcm(pk.data(), src_, &conv[0], codec, frames * channels);
                    if (e != errc::Ok) return e;
                    pk = std::move(conv);
                }
            } else if (pts < start_) continue;
            cur_ = std::move(pk);
            cur_pts_ = pts > start_ ? pts - start_ : 0;
            have_ = true;
            return errc::Ok;
        }
    }
};

/// Decoded float audio, delivered as 20 ms PCM chunks of the wanted codec.
class buffer_feed : public audio_feed {
public:
    errc init(aud::pcm_buffer&& b, vid::audio_codec target) noexcept {
        if (!b.valid() || !vid::audio_codec_is_pcm(target)) return errc::InvalidArgument;
        buf_ = std::move(b); codec = target; rate = buf_.rate; channels = buf_.channels; is_pcm = true;
        total_ = buf_.frames(); chunk_ = rate / 50; if (chunk_ < 64) chunk_ = 64;
        bps_ = vid::audio_codec_bits(target) / 8;
        return errc::Ok;
    }
    bool has() const noexcept override { return pos_ < total_; }
    uint64_t pts_us() const noexcept override { return vid::detail::muldiv_u64(pos_, 1000000ull, rate); }
    errc pop(knst_byte_string& out) noexcept override {
        uint64_t n = total_ - pos_; if (n > chunk_) n = chunk_;
        errc e = vid::detail::alloc_bytes(out, n * channels * bps_);
        if (e != errc::Ok) return e;
        const float* s = buf_.samples.data() + (size_t)pos_ * channels;
        uint8_t* d = &out[0]; const uint64_t cnt = n * channels;
        for (uint64_t i = 0; i < cnt; ++i) vid::audio::f32_to_sample(s[i], d + i * bps_, codec);
        pos_ += n;
        return errc::Ok;
    }
private:
    aud::pcm_buffer buf_; uint64_t pos_ = 0, total_ = 0; uint32_t chunk_ = 960, bps_ = 2;
};

/// Compressed frames of an .mp3 / .aac (ADTS) file, passed through untouched.
class frame_feed : public audio_feed {
public:
    errc init(knst_byte_string&& file, bs::elementary&& el, uint64_t start_us, uint64_t end_us, int64_t shift_us) noexcept {
        file_ = std::move(file); el_ = std::move(el); start_ = start_us; end_ = end_us; shift_ = shift_us;
        codec = el_.codec; rate = el_.rate; channels = el_.channels; extra = el_.asc; is_pcm = false; copied = true;
        advance_(false);
        return errc::Ok;
    }
    bool has() const noexcept override { return have_; }
    uint64_t pts_us() const noexcept override { return eff_pts_; }
    errc pop(knst_byte_string& out) noexcept override {
        const bs::elem_frame& f = el_.frames[idx_];
        out = knst_byte_string(file_.data() + f.off, f.size);
        advance_(true);
        return errc::Ok;
    }
private:
    knst_byte_string file_; bs::elementary el_; uint64_t start_ = 0, end_ = 0; int64_t shift_ = 0;
    uint32_t idx_ = 0; uint64_t cum_ = 0; bool have_ = false; uint64_t eff_pts_ = 0;

    void advance_(bool consume) noexcept {
        if (consume) { cum_ += el_.frames[idx_].samples; ++idx_; }
        have_ = false;
        while (idx_ < el_.frames.size()) {
            const uint64_t p = vid::detail::muldiv_u64(cum_, 1000000ull, rate);
            if (end_ && p >= end_) return;
            const int64_t eff = (int64_t)p - (int64_t)start_ + shift_;
            if (p < start_ || eff < 0) { cum_ += el_.frames[idx_].samples; ++idx_; continue; }
            eff_pts_ = (uint64_t)eff; have_ = true; return;
        }
    }
};

// ============================================================
// Video pumps (produce one video packet at a time, in presentation order of the container)
// ============================================================

namespace cdetail {

inline errc apply_filters(const knst_byte_string& in, uint32_t w, uint32_t h, const video_filters& f,
                          knst_byte_string& out, uint32_t& ow, uint32_t& oh, bool& grey) noexcept {
    knst_byte_string hold; const uint8_t* p = in.data(); bool owned = false;
    auto take = [&](result<knst_byte_string>&& r) -> bool {
        if (!r) return false;
        hold = std::move(r.value); p = hold.data(); owned = true; return true;
    };
    if (f.crop_w && f.crop_h) {
        if (!take(img::crop_rgb24(p, w, h, f.crop_x, f.crop_y, f.crop_w, f.crop_h))) return errc::InvalidArgument;
        w = f.crop_w; h = f.crop_h;
    }
    switch (f.rotate_deg % 360) {
        case 90:  if (!take(img::rotate_90_rgb24(p, w, h, true)))  return errc::OutOfMemory; std::swap(w, h); break;
        case 270: if (!take(img::rotate_90_rgb24(p, w, h, false))) return errc::OutOfMemory; std::swap(w, h); break;
        case 180: if (!take(img::rotate_180_rgb24(p, w, h)))       return errc::OutOfMemory; break;
        default: break;
    }
    if (f.flip_h && !take(img::flip_h_rgb24(p, w, h))) return errc::OutOfMemory;
    if (f.flip_v && !take(img::flip_v_rgb24(p, w, h))) return errc::OutOfMemory;

    uint32_t dw = w, dh = h;
    if (f.out_w && f.out_h) { dw = f.out_w; dh = f.out_h; }
    else if (f.out_w) { dw = f.out_w; dh = (uint32_t)(((uint64_t)h * f.out_w + w / 2) / w); }
    else if (f.out_h) { dh = f.out_h; dw = (uint32_t)(((uint64_t)w * f.out_h + h / 2) / h); }
    else if (f.scale > 0.0 && f.scale != 1.0) { dw = (uint32_t)((double)w * f.scale + 0.5); dh = (uint32_t)((double)h * f.scale + 0.5); }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (dw != w || dh != h) {
        img::resize_filter rf = f.filter;
        if (f.auto_filter) rf = (dw < w || dh < h) ? img::resize_filter::box : img::resize_filter::bilinear;
        if (!take(img::resize_rgb24(p, w, h, dw, dh, rf))) return errc::InvalidArgument;
        w = dw; h = dh;
    }
    if (f.brightness != 0.0f || f.contrast != 1.0f) {
        if (!owned) { hold = in; p = hold.data(); owned = true; }
        errc e = img::adjust_rgb24(&hold[0], w, h, f.brightness, f.contrast);
        if (e != errc::Ok) return e;
    }
    grey = false;
    if (f.grey) {
        auto g = img::to_grey_rgb24(p, w, h);
        if (!g) return errc::OutOfMemory;
        out = std::move(g.value); grey = true;
    } else if (owned) out = std::move(hold);
    else out = in;
    ow = w; oh = h;
    return errc::Ok;
}

} // namespace cdetail

class video_pump {
public:
    uint32_t out_w = 0, out_h = 0; bool is_grey = false; uint64_t written = 0;
    virtual ~video_pump() {}
    virtual errc prime() noexcept = 0;
    virtual bool has() const noexcept = 0;
    virtual uint64_t pts_us() const noexcept = 0;
    virtual errc step(vid::video_writer& w) noexcept = 0;
};

/// Packet copy (no re-encode), optional H.264/H.265 framing conversion.
class copy_pump : public video_pump {
public:
    vid::video_reader* rd = nullptr; uint32_t track = 0; uint64_t idx = 0, base_us = 0, end_us = 0;
    bool allow_empty = false, h265 = false; int conv = 0;       // 0 none, 1 length-prefixed -> Annex-B, 2 Annex-B -> length-prefixed
    bs::param_sets ps;

    errc prime() noexcept override { return fetch_(); }
    bool has() const noexcept override { return have_; }
    uint64_t pts_us() const noexcept override { return pts_ > base_us ? pts_ - base_us : 0; }
    errc step(vid::video_writer& w) noexcept override {
        const uint8_t* d = pk_.data(); uint32_t n = pk_.length();
        if (conv == 1) {
            errc e = bs::lp_to_annexb(d, n, ps.len_size, key_ ? &ps : nullptr, tmp_);
            if (e != errc::Ok) return e;
            d = tmp_.data(); n = tmp_.length();
        } else if (conv == 2) {
            errc e = bs::annexb_to_lp(d, n, h265, tmp_);
            if (e != errc::Ok) return e;
            d = tmp_.data(); n = tmp_.length();
            if (n == 0) return fetch_();                          // parameter-set-only access unit
        }
        const uint64_t dts = (dts_ == ~0ull || dts_ == pts_) ? ~0ull : (dts_ > base_us ? dts_ - base_us : 0);
        auto r = w.write_encoded(d, n, key_, pts_us(), dts);
        if (!r) return w.last_error() != errc::Ok ? w.last_error() : errc::IoError;
        ++written;
        return fetch_();
    }
private:
    knst_byte_string pk_, tmp_; uint64_t pts_ = 0, dts_ = ~0ull; bool key_ = true, have_ = false;
    errc fetch_() noexcept {
        have_ = false;
        for (;;) {
            uint64_t p = 0, dt = 0; bool k = false;
            auto r = rd->read_packet(track, idx, &p, &k, &dt);
            if (!r) { errc e = rd->last_error(); return e == errc::NotFound ? errc::Ok : e; }
            ++idx;
            if (end_us && p >= end_us) return errc::Ok;
            if (r.value.length() == 0 && !allow_empty) continue;
            pk_ = std::move(r.value); pts_ = p; dts_ = dt; key_ = k; have_ = true;
            return errc::Ok;
        }
    }
};

/// Decode -> filters -> (re-encode by the writer). Frame accurate time range and optional fps conversion.
class transcode_pump : public video_pump {
public:
    vid::video_reader* rd = nullptr; const video_filters* flt = nullptr;
    double fps = 0.0; uint64_t start_us = 0, end_us = 0, dur_us = 0, idx = 0;

    errc prime() noexcept override { return fetch_(); }
    bool has() const noexcept override { return have_; }
    uint64_t pts_us() const noexcept override { return cur_pts_; }
    errc step(vid::video_writer& w) noexcept override {
        auto r = w.write_frame(cur_.data(), out_w, out_h, is_grey ? vid::pixel_format::GREY : vid::pixel_format::RGB24, 0, cur_pts_);
        if (!r) return w.last_error() != errc::Ok ? w.last_error() : errc::IoError;
        ++written;
        return fetch_();
    }
private:
    knst_byte_string cur_; uint64_t cur_pts_ = 0, k_ = 0, cur_idx_ = 0; bool have_ = false, have_cur_ = false;

    errc decode_(uint64_t sidx) noexcept {
        auto r = rd->read_frame_rgb24(sidx);
        if (!r) return rd->last_error() != errc::Ok ? rd->last_error() : errc::IoError;
        uint32_t ow = 0, oh = 0; bool g = false;
        errc e = cdetail::apply_filters(r.value.data, r.value.width, r.value.height, *flt, cur_, ow, oh, g);
        if (e != errc::Ok) return e;
        out_w = ow; out_h = oh; is_grey = g; cur_idx_ = sidx; have_cur_ = true;
        return errc::Ok;
    }
    errc fetch_() noexcept {
        have_ = false;
        if (fps > 0.0) {
            const uint64_t limit = end_us ? end_us : dur_us;
            const uint64_t t = start_us + (uint64_t)((double)k_ * 1e6 / fps);
            if (limit && t >= limit) return errc::Ok;
            const uint64_t sidx = rd->frame_at_time(t);
            if (!have_cur_ || sidx != cur_idx_) { errc e = decode_(sidx); if (e != errc::Ok) return e; }
            cur_pts_ = t - start_us; ++k_; have_ = true;
            return errc::Ok;
        }
        const uint64_t n = rd->video_frames();
        if (idx >= n) return errc::Ok;
        auto r = rd->read_frame_rgb24(idx);
        if (!r) return rd->last_error() != errc::Ok ? rd->last_error() : errc::IoError;
        const uint64_t p = r.value.pts_us;
        if (end_us && p >= end_us) return errc::Ok;
        uint32_t ow = 0, oh = 0; bool g = false;
        errc e = cdetail::apply_filters(r.value.data, r.value.width, r.value.height, *flt, cur_, ow, oh, g);
        if (e != errc::Ok) return e;
        out_w = ow; out_h = oh; is_grey = g; cur_pts_ = p > start_us ? p - start_us : 0;
        ++idx; have_ = true;
        return errc::Ok;
    }
};

// ============================================================
// Audio encoder registry (MP3 / FLAC / OGG / AAC ... supplied by the user)
// ============================================================

namespace cdetail {
struct enc_registry {
    struct entry { audio_encoder_fn fn = nullptr; void* user = nullptr; };
    std::mutex m; entry e[32];
    static enc_registry& get() noexcept { static enc_registry r; return r; }
};
} // namespace cdetail

inline bool register_audio_encoder(format_t f, audio_encoder_fn fn, void* user = nullptr) noexcept {
    auto& r = cdetail::enc_registry::get(); std::lock_guard<std::mutex> g(r.m);
    if ((unsigned)f >= 32) return false;
    r.e[(unsigned)f].fn = fn; r.e[(unsigned)f].user = user; return true;
}

// ============================================================
// converter
// ============================================================

class converter {
public:
    converter() noexcept = default;

    void set_progress(progress_fn f, void* user = nullptr) noexcept { prog_ = f; prog_user_ = user; }
    void set_cancel(const std::atomic<bool>* flag) noexcept { cancel_ = flag; }
    errc last_error() const noexcept { return last_err_; }
    const knst_c16string& last_message() const noexcept { return last_msg_; }
    bool was_cancelled() const noexcept { return cancelled_; }
    static bool register_audio_encoder(format_t f, audio_encoder_fn fn, void* user = nullptr) noexcept {
        return knst_conv::register_audio_encoder(f, fn, user);
    }

    // ------------------------------------------------------------------
    // probe
    // ------------------------------------------------------------------
    result<input_info> probe(const knst_c16string& path) noexcept {
        using R = result<input_info>;
        begin_();
        input_info ii;
        errc e = sniff_(path, ii.format);
        if (e != errc::Ok) return fail_<input_info>(e, "cannot open or recognise the file");
        if (is_av_container(ii.format)) {
            vid::video_reader rd;
            auto r = rd.open(path);
            if (!r) return fail_<input_info>(rd.last_error(), "container could not be parsed");
            ii.av_container = true; ii.av = rd.info();
            ii.has_video = rd.video_stream() >= 0; ii.has_audio = rd.audio_stream() >= 0;
            ii.duration_s = (double)rd.duration_us() / 1e6;
            return R::success(std::move(ii));
        }
        if (ii.format == format_t::mp3 || ii.format == format_t::aac) {
            knst_file_error fe = knst_file_error::None;
            knst_byte_string data = knst_file::read_file_data<>(path, &fe);
            bs::elementary el;
            e = ii.format == format_t::mp3 ? bs::scan_mp3(data, el) : bs::scan_adts(data, el);
            if (e != errc::Ok) return fail_<input_info>(e, "no valid audio frames found");
            ii.au.container = ii.format == format_t::mp3 ? aud::container_t::mp3 : aud::container_t::aac_adts;
            ii.au.codec = ii.format == format_t::mp3 ? aud::codec_t::mp3 : aud::codec_t::aac;
            ii.au.rate = el.rate; ii.au.channels = el.channels; ii.au.frames = el.total_samples;
            ii.au.duration = el.rate ? (double)el.total_samples / el.rate : 0.0; ii.au.valid = true;
            ii.has_audio = true; ii.duration_s = ii.au.duration;
            return R::success(std::move(ii));
        }
        auto pr = aud::probe(path);
        if (pr) { ii.au = pr.value; ii.has_audio = true; ii.duration_s = ii.au.duration; }
        else if (ii.format == format_t::raw_pcm || ii.format == format_t::unknown) return fail_<input_info>(errc::NotSupported, "unsupported file");
        else ii.has_audio = true;
        return R::success(std::move(ii));
    }

    // ------------------------------------------------------------------
    // audio conversion / extraction
    // ------------------------------------------------------------------
    result<report> convert_audio(const knst_c16string& in, const knst_c16string& out,
                                 const audio_options& o = audio_options()) noexcept {
        begin_();
        if (in.empty() || out.empty()) return fail_<report>(errc::InvalidArgument, "empty path");
        format_t fin = format_t::unknown;
        errc e = sniff_(in, fin);
        if (e != errc::Ok) return fail_<report>(e, "cannot open input file");
        format_t fout = o.format != format_t::unknown ? o.format : format_from_path(out);
        if (fout == format_t::unknown) return fail_<report>(errc::InvalidArgument, "cannot deduce output format (set audio_options::format)");
        rep_.out_format = fout;
        const uint64_t s_us = cdetail::sec_us(o.start_s), e_us = cdetail::sec_us(o.end_s);
        if (e_us && e_us <= s_us) return fail_<report>(errc::InvalidArgument, "end_s must be greater than start_s");

        result<report> rr = is_av_container(fin) ? audio_from_container_(in, out, fout, o, s_us, e_us)
                                                  : audio_from_file_(in, out, fin, fout, o, s_us, e_us);
        if (!rr) knst_file::remove_file(out);
        return rr;
    }

    result<report> extract_audio(const knst_c16string& in, const knst_c16string& out,
                                 const audio_options& o = audio_options()) noexcept { return convert_audio(in, out, o); }

    // ------------------------------------------------------------------
    // video conversion / remux / mux
    // ------------------------------------------------------------------
    result<report> convert_video(const knst_c16string& in, const knst_c16string& out,
                                 const video_options& o = video_options()) noexcept {
        begin_();
        result<report> rr = convert_video_(in, out, o);
        if (!rr) knst_file::remove_file(out);
        return rr;
    }

    result<report> strip_audio(const knst_c16string& in, const knst_c16string& out) noexcept {
        video_options o; o.amode = audio_mode::drop; return convert_video(in, out, o);
    }

    /// Replaces / adds the sound track of `video_in` with `audio_in` (audio file or another video container).
    result<report> add_audio(const knst_c16string& video_in, const knst_c16string& audio_in,
                             const knst_c16string& out, video_options o = video_options()) noexcept {
        o.external_audio = audio_in;
        return convert_video(video_in, out, o);
    }

    // ------------------------------------------------------------------
    // frames
    // ------------------------------------------------------------------
    /// Saves the frame at `time_s` as .jpg or .ppm (extension of `out`).
    result<report> extract_frame(const knst_c16string& in, const knst_c16string& out, double time_s = 0.0,
                                 int quality = 90, const video_filters* flt = nullptr) noexcept {
        begin_();
        vid::video_reader rd;
        format_t fin = format_t::unknown;
        errc e = sniff_(in, fin);
        if (e != errc::Ok || !is_av_container(fin)) return fail_<report>(e != errc::Ok ? e : errc::NotSupported, "input is not a readable video container");
        { auto r = rd.open(in); if (!r) return fail_<report>(rd.last_error(), "container could not be parsed"); }
        if (rd.video_stream() < 0) return fail_<report>(errc::NotFound, "no video track");
        format_t fo = format_from_path(out);
        if (fo != format_t::jpeg && fo != format_t::ppm) return fail_<report>(errc::InvalidArgument, "output must be .jpg or .ppm");
        e = grab_(rd, rd.frame_at_time(cdetail::sec_us(time_s)), out, fo, quality, flt);
        if (e != errc::Ok) { knst_file::remove_file(out); return vid::err_result<report>(e); }
        rep_.ok = true; rep_.video_frames = 1; rep_.out_format = fo; rep_.bytes_out = cdetail::file_size_of(out);
        return result<report>::success(std::move(rep_));
    }

    /// Saves one frame every `interval_s` seconds as frame_000001.jpg ... into `out_dir`.
    result<report> extract_frames(const knst_c16string& in, const knst_c16string& out_dir, double interval_s = 1.0,
                                  uint32_t max_frames = 0, int quality = 90, const video_filters* flt = nullptr,
                                  bool ppm = false) noexcept {
        begin_();
        if (!(interval_s > 0.0)) return fail_<report>(errc::InvalidArgument, "interval_s must be > 0");
        vid::video_reader rd;
        { auto r = rd.open(in); if (!r) return fail_<report>(rd.last_error(), "cannot open input video"); }
        if (rd.video_stream() < 0) return fail_<report>(errc::NotFound, "no video track");
        const double dur = (double)rd.duration_us() / 1e6;
        uint32_t n = 0;
        for (double t = 0.0; t < dur || n == 0; t += interval_s) {
            if (max_frames && n >= max_frames) break;
            char nm[40]; std::snprintf(nm, sizeof(nm), ppm ? "frame_%06u.ppm" : "frame_%06u.jpg", n + 1);
            knst_c16string path = knst_file::join_path(out_dir, knst_c16string(nm));
            errc e = grab_(rd, rd.frame_at_time(cdetail::sec_us(t)), path, ppm ? format_t::ppm : format_t::jpeg, quality, flt);
            if (e != errc::Ok) return fail_<report>(e, "frame export failed");
            ++n;
            if (!progress_(dur > 0 ? t / dur : 1.0, "frames")) return fail_<report>(errc::BadState, "cancelled");
        }
        rep_.ok = true; rep_.video_frames = n; rep_.seconds = dur;
        return result<report>::success(std::move(rep_));
    }

    // ------------------------------------------------------------------
    // one-call dispatcher: decides from the file types
    // ------------------------------------------------------------------
    result<report> convert(const knst_c16string& in, const knst_c16string& out) noexcept {
        format_t fin = format_t::unknown;
        errc e = sniff_(in, fin);
        if (e != errc::Ok) { begin_(); return fail_<report>(e, "cannot open input file"); }
        const format_t fout = format_from_path(out);
        if (is_av_container(fout)) {
            if (is_av_container(fin)) return convert_video(in, out);
            begin_(); return fail_<report>(errc::NotSupported, "audio file -> video container is not meaningful; use .mka or add_audio()");
        }
        if (fout == format_t::jpeg || fout == format_t::ppm) return extract_frame(in, out, 0.0);
        if (fout == format_t::unknown) { begin_(); return fail_<report>(errc::InvalidArgument, "unknown output extension"); }
        return convert_audio(in, out);
    }

private:
    progress_fn prog_ = nullptr; void* prog_user_ = nullptr; const std::atomic<bool>* cancel_ = nullptr;
    errc last_err_ = errc::Ok; knst_c16string last_msg_; bool cancelled_ = false; report rep_;

    // ---------------- state / messages ----------------
    void begin_() noexcept { rep_ = report(); last_err_ = errc::Ok; last_msg_ = knst_c16string(); cancelled_ = false; }

    errc set_err_(errc e, const char* msg) noexcept { last_err_ = e; last_msg_ = knst_c16string(msg); return e; }
    template<class T> result<T> fail_(errc e, const char* msg) noexcept { set_err_(e, msg); return vid::err_result<T>(e); }

    void note_(const char* msg) noexcept { rep_.notes.push_back(knst_c16string(msg)); }
    void notef_(const char* fmt, ...) noexcept {
        char buf[320]; va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
        rep_.notes.push_back(knst_c16string(buf));
    }
    bool progress_(double f, const char* stage) noexcept {
        if (prog_ && !prog_(prog_user_, f < 0 ? 0 : (f > 1 ? 1 : f), stage)) cancelled_ = true;
        if (cancel_ && cancel_->load(std::memory_order_relaxed)) cancelled_ = true;
        return !cancelled_;
    }

    // ---------------- input sniffing ----------------
    errc sniff_(const knst_c16string& path, format_t& fmt) noexcept {
        fmt = format_t::unknown;
        vid::detail::file_handle f;
        errc e = f.open(path, vid::detail::file_handle::mode::read);
        if (e != errc::Ok) return e;
        uint8_t h[32] = { 0 };
        const size_t got = f.read(h, sizeof(h));
        if (got < 4) return errc::Corrupt;
        if (got >= 12 && !std::memcmp(h, "RIFF", 4) && !std::memcmp(h + 8, "AVI ", 4)) { fmt = format_t::avi; return errc::Ok; }
        if (h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3) { fmt = format_t::mkv; return errc::Ok; }
        if (got >= 12 && (!std::memcmp(h + 4, "ftyp", 4) || !std::memcmp(h + 4, "moov", 4) || !std::memcmp(h + 4, "mdat", 4) ||
                          !std::memcmp(h + 4, "free", 4) || !std::memcmp(h + 4, "wide", 4))) {
            fmt = !std::memcmp(h + 8, "qt  ", 4) ? format_t::mov : format_t::mp4; return errc::Ok;
        }
        aud::container_t c = aud::container_t::unknown;
        if (got >= 10 && !std::memcmp(h, "ID3", 3)) {
            const uint64_t off = bs::skip_id3(h, got);
            uint8_t h2[32] = { 0 };
            if (f.seek(off)) { size_t g2 = f.read(h2, sizeof(h2)); if (g2 >= 4) c = aud::codec::detect(h2, g2); }
            if (c == aud::container_t::unknown) c = aud::container_t::mp3;
        } else c = aud::codec::detect(h, got);
        switch (c) {
            case aud::container_t::wav: case aud::container_t::rf64: fmt = format_t::wav; break;
            case aud::container_t::aiff: case aud::container_t::aifc: fmt = format_t::aiff; break;
            case aud::container_t::au: fmt = format_t::au; break;
            case aud::container_t::flac: fmt = format_t::flac; break;
            case aud::container_t::ogg: fmt = format_t::ogg; break;
            case aud::container_t::mp3: fmt = format_t::mp3; break;
            case aud::container_t::aac_adts: fmt = format_t::aac; break;
            default: break;
        }
        if (fmt == format_t::unknown) {
            format_t x = format_from_path(path);
            if (x == format_t::raw_pcm) fmt = x;
        }
        return fmt == format_t::unknown ? errc::NotSupported : errc::Ok;
    }

    // ---------------- audio: processing / encoding ----------------
    errc process_audio_(aud::pcm_buffer& b, const audio_options& o) noexcept {
        namespace dsp = aud::dsp;
        if (o.channels && o.channels != b.channels) {
            knst_vector<float> v = dsp::convert_channels(b.samples.data(), b.frames(), b.channels, o.channels);
            if (v.empty()) return set_err_(errc::OutOfMemory, "channel conversion failed");
            b.samples = std::move(v); b.channels = o.channels;
        }
        if (o.sample_rate && o.sample_rate != b.rate) {
            knst_vector<float> v = dsp::resample(b.samples.data(), b.frames(), b.channels, b.rate, o.sample_rate, o.quality);
            if (v.empty()) return set_err_(errc::OutOfMemory, "resampling failed");
            b.samples = std::move(v); b.rate = o.sample_rate;
        }
        float* d = b.samples.data(); const uint32_t n = (uint32_t)b.samples.size(), fr = b.frames();
        if (o.remove_dc) dsp::dc_remove_interleaved(d, fr, b.channels);
        if (o.gain_db != 0.0f) dsp::apply_gain_db(d, n, o.gain_db);
        if (o.normalize) dsp::normalize(d, n, dsp::from_db(o.normalize_peak_db));
        if (o.fade_in_ms > 0.0f)  dsp::fade_in(d, fr, b.channels, (uint32_t)((double)o.fade_in_ms * 0.001 * b.rate));
        if (o.fade_out_ms > 0.0f) dsp::fade_out(d, fr, b.channels, (uint32_t)((double)o.fade_out_ms * 0.001 * b.rate));
        if (o.limiter) dsp::limiter(d, fr, b.channels, b.rate, o.limiter_db);
        return errc::Ok;
    }

    errc encode_out_(aud::pcm_buffer& b, format_t fout, const knst_c16string& out, const audio_options& o, bool processed) noexcept {
        if (!b.valid()) return set_err_(errc::Corrupt, "decoded audio is empty");
        const bool try_native = (fout == format_t::wav || fout == format_t::aiff ||
                                 fout == format_t::au  || fout == format_t::raw_pcm ||
                                 fout == format_t::flac || fout == format_t::ogg);
        if (try_native) {
            aud::encode_options eo;
            eo.container = fout == format_t::wav  ? aud::container_t::wav
                         : fout == format_t::aiff ? aud::container_t::aiff
                         : fout == format_t::au   ? aud::container_t::au
                         : fout == format_t::flac ? aud::container_t::flac
                         : fout == format_t::ogg  ? aud::container_t::ogg
                         : aud::container_t::raw;
            eo.fmt = o.sample; eo.big_endian = o.raw_big_endian;
            const uint32_t bits = aud::sample_bytes(o.sample) * 8;
            eo.dither = o.dither && bits <= 16 && (processed || b.source_bits == 0 || b.source_bits > bits);
            auto r = aud::encode_file(out, b, eo);
            if (r) {
                rep_.ok = true; rep_.audio_frames = b.frames(); rep_.seconds = b.duration();
                rep_.bytes_out = cdetail::file_size_of(out);
                return errc::Ok;
            }
            // OGG native vorbis missing → try sndfile fallback below
            if (fout != format_t::ogg || r.st != status::NotSupported)
                return set_err_(cdetail::from_status(r.st), "writing the audio file failed (WAV/AIFF/AU are limited to 4 GiB)");
        }
        if (!try_native || fout == format_t::ogg) {
            cdetail::enc_registry::entry en;
            { auto& rg = cdetail::enc_registry::get(); std::lock_guard<std::mutex> g(rg.m); en = rg.e[(unsigned)fout]; }
            if (en.fn) {
                if (!knst_file::ensure_parent_dir(out)) return set_err_(errc::IoError, "cannot create the output directory");
                auto r = en.fn(en.user, b, out);
                if (!r) return set_err_(cdetail::from_status(r.st), "registered encoder reported a failure");
            } else {
                // Try libsndfile encode fallback (FLAC / OGG-Vorbis / AIFF / AU / WAV).
                // Only kicks in when no user-registered encoder exists.
                int sf_fmt = cdetail::sndfile_format_for(fout, o.sample);
                if (sf_fmt && aud::codec::sndfile::get().ok) {
                    auto enc = aud::codec::sndfile::encode(b.samples.data(), b.frames(),
                                                            b.rate, b.channels, sf_fmt);
                    if (enc) {
                        if (!knst_file::ensure_parent_dir(out)) return set_err_(errc::IoError, "cannot create the output directory");
                        if (!knst_file::write_file_atomic(out, enc.value.data(), enc.value.length()))
                            return set_err_(errc::IoError, "writing the audio file failed");
                        rep_.ok = true;
                        rep_.audio_frames = b.frames();
                        rep_.seconds = b.duration();
                        rep_.bytes_out = enc.value.length();
                        return errc::Ok;
                    }
                }
                char m[240]; std::snprintf(m, sizeof(m),
                    "no encoder for %s: libsndfile is not installed or lacks this format; register one with converter::register_audio_encoder()",
                    format_name(fout));
                return set_err_(errc::NotSupported, m);
            }
        }
        rep_.ok = true; rep_.audio_frames = b.frames(); rep_.seconds = b.duration(); rep_.bytes_out = cdetail::file_size_of(out);
        return errc::Ok;
    }

    result<report> finish_ok_() noexcept { rep_.ok = true; return result<report>::success(std::move(rep_)); }

    // ---------------- audio: loading ----------------
    /// Decodes an audio track of a container (PCM natively, others through external_codec::register_audio_decoder).
    errc load_track_pcm_(vid::video_reader& rd, uint32_t ti, uint64_t start_us, uint64_t end_us, aud::pcm_buffer& out) noexcept {
        const vid::stream_desc sd = rd.stream(ti);
        const bool pcm = vid::audio_codec_is_pcm(sd.acodec);

        // ── MP3 special case: stream-decode via minimp3 (ships in knst_audio.hpp) ──
        //
        // MP3 Layer III is stateful (bit reservoir / main_data_begin spans
        // frames), so we cannot decode packet-by-packet in isolation.
        // Instead we stream: feed container packets in order to minimp3,
        // collect PCM into `out.samples` with geometric growth, apply the
        // [start_us, end_us) window via frame-skip + one final trim.
        //
        // RAM is bounded (~500 KB input buffer + one frame), not 2 GB like
        // the previous buffer-all approach.
        if (sd.acodec == vid::audio_codec::MP3) {
            mp3dec_t dec;
            mp3dec_init(&dec);
            mp3dec_frame_info_t info{};
            mp3d_sample_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];

            uint32_t rate = 0, channels = 0;
            uint64_t base_pts = 0;
            bool first = true;
            uint32_t cap = 0;
            uint64_t written = 0;
            bool done = false;

            // Accumulate raw MP3 bytes across packet boundaries so a frame
            // that got split between two container packets still decodes.
            // 4096 is enough for any single MP3 frame; we grow if needed.
            knst_vector<uint8_t> pending;
            if (!aud::adetail::resize_ok(pending, 4096))
                return set_err_(errc::OutOfMemory, "MP3 pending buffer");
            size_t pending_len = 0;

            for (uint64_t i = 0; !done; ++i) {
                uint64_t pts = 0;
                auto r = rd.read_packet(ti, i, &pts);
                if (!r) {
                    errc e = rd.last_error();
                    if (e == errc::NotFound) break;
                    return set_err_(e, "reading an MP3 packet failed");
                }

                // Grow pending buffer if the packet is large.
                uint32_t need = (uint32_t)(pending_len + r.value.length());
                if (need > pending.size()) {
                    uint32_t nc = need < 8192 ? 8192 : need + 4096;
                    if (!aud::adetail::resize_ok(pending, nc))
                        return set_err_(errc::OutOfMemory, "MP3 pending buffer");
                }
                if (r.value.length() > 0) {
                    ::memcpy(pending.data() + pending_len, r.value.data(), r.value.length());
                    pending_len += r.value.length();
                }

                size_t off = 0;
                while (pending_len - off >= 4) {
                    int samples = mp3dec_decode_frame(&dec, pending.data() + off,
                        (int)(pending_len - off), pcm_buf, &info);

                    if (info.frame_bytes == 0) {
                        // Not enough bytes for a complete frame in the tail.
                        // If pending is unreasonably big, we've lost sync.
                        if (pending_len - off > 65536)
                            return set_err_(errc::Corrupt, "MP3 sync lost");
                        break;
                    }
                    off += info.frame_bytes;

                    if (samples <= 0) continue;

                    if (first) {
                        rate = (uint32_t)info.hz;
                        channels = (uint32_t)info.channels;
                        base_pts = pts;
                        first = false;
                    } else if (rate != (uint32_t)info.hz || channels != (uint32_t)info.channels) {
                        return set_err_(errc::NotSupported, "MP3 parameters change mid-stream");
                    }

                    uint64_t frames_here = (uint64_t)samples;
                    uint64_t samples_here = frames_here * channels;
                    uint64_t dur_us = vid::detail::muldiv_u64(frames_here, 1000000ull, rate);

                    // Skip frames entirely before start_us.
                    if (pts + dur_us <= start_us) continue;

                    if (!aud::adetail::grow(out.samples, cap, written + samples_here))
                        return set_err_(errc::OutOfMemory, "audio is too large for memory");
                    float* dst = out.samples.data() + written;
                    for (uint64_t k = 0; k < samples_here; ++k)
                        dst[k] = pcm_buf[k] * (1.0f / 32768.0f);
                    written += samples_here;

                    if (end_us && pts + dur_us >= end_us) { done = true; break; }
                }

                // Compact pending.
                if (off > 0) {
                    size_t rem = pending_len - off;
                    if (rem > 0) ::memmove(pending.data(), pending.data() + off, rem);
                    pending_len = rem;
                }
            }

            if (first || written == 0)
                return set_err_(errc::NotFound, "no MP3 samples in the selected range");
            aud::adetail::resize_ok(out.samples, (uint32_t)written);
            out.rate = rate;
            out.channels = channels;
            out.source_bits = 0;
            out.codec = aud::codec_t::mp3;

            // Precise window trim (minimp3 output isn't frame-boundary-aligned
            // to start_us; we computed base_pts from the first kept packet).
            const uint64_t total = written / channels;
            uint64_t skip = start_us > base_pts ? vid::detail::muldiv_u64(start_us - base_pts, rate, 1000000ull) : 0;
            uint64_t endf = total;
            if (end_us && end_us > base_pts) {
                endf = vid::detail::muldiv_u64(end_us - base_pts, rate, 1000000ull);
                if (endf > total) endf = total;
            }
            if (skip >= endf)
                return set_err_(errc::NotFound, "no MP3 samples in the selected range");
                        if (skip > 0 || endf < total) {
                knst_vector<float> t = aud::dsp::trim(out.samples.data(), (uint32_t)total, channels,
                                                       (uint32_t)skip, (uint32_t)endf);
                if (t.empty()) return set_err_(errc::OutOfMemory, "trim failed");
                out.samples = std::move(t);
            }
            return errc::Ok;
        }

#if defined(KNST_NETWORK_HAVE_FAAD2)
        // ── AAC special case: stateful FAAD2 decode (MP4/MKV AAC tracks) ──
        if (sd.acodec == vid::audio_codec::AAC) {
            knst_byte_string asc;
            if (!bs::get_asc(sd, rd.kind(), asc) || asc.length() < 2)
                return set_err_(errc::Corrupt, "AAC stream has no AudioSpecificConfig");

                        aud::codec::aac_native::frame_decoder dec;
            if (!dec.init(asc.data(), asc.length()))
                return set_err_(errc::Corrupt, "FAAD2 couldn't initialize AAC decoder");

            uint32_t cap = 0; uint64_t written = 0;
            bool first = true; uint64_t base_pts = 0;
            uint32_t rate = 0, ch = 0;

            for (uint64_t i = 0;; ++i) {
                uint64_t pts = 0;
                auto r = rd.read_packet(ti, i, &pts);
                if (!r) { errc ee = rd.last_error(); if (ee == errc::NotFound) break; return set_err_(ee, "reading AAC packet failed"); }
                if (end_us && pts >= end_us) break;

                const float* pcm_out = nullptr; uint32_t nf = 0, nc = 0;
                if (!dec.decode_frame(r.value.data(), r.value.length(), &pcm_out, nf, nc)) continue;
                if (nf == 0 || nc == 0) continue;

                if (first) { rate = dec.out_rate; ch = nc; base_pts = pts; first = false; }
                else if (nc != ch) return set_err_(errc::NotSupported, "AAC channels change mid-stream");

                uint64_t frames_here = nf;
                uint64_t dur_us = vid::detail::muldiv_u64(frames_here, 1000000ull, rate);
                if (pts + dur_us <= start_us) continue;

                uint64_t nsamp = frames_here * ch;
                if (!aud::adetail::grow(out.samples, cap, written + nsamp))
                    return set_err_(errc::OutOfMemory, "audio too large");
                float* dst = out.samples.data() + written;
                for (uint64_t k = 0; k < nsamp; ++k) dst[k] = pcm_out[k];
                written += nsamp;
            }
            if (first || written == 0) return set_err_(errc::NotFound, "no AAC samples in range");
            aud::adetail::resize_ok(out.samples, (uint32_t)written);
            out.rate = rate; out.channels = ch; out.source_bits = 0; out.codec = aud::codec_t::aac;

            const uint64_t total = written / ch;
            uint64_t skip = start_us > base_pts ? vid::detail::muldiv_u64(start_us - base_pts, rate, 1000000ull) : 0;
            uint64_t endf = total;
            if (end_us && end_us > base_pts) {
                endf = vid::detail::muldiv_u64(end_us - base_pts, rate, 1000000ull);
                if (endf > total) endf = total;
            }
            if (skip >= endf) return set_err_(errc::NotFound, "no AAC samples in range");
            if (skip > 0 || endf < total) {
                knst_vector<float> t = aud::dsp::trim(out.samples.data(), (uint32_t)total, ch,
                                                       (uint32_t)skip, (uint32_t)endf);
                if (t.empty()) return set_err_(errc::OutOfMemory, "trim failed");
                out.samples = std::move(t);
            }
            return errc::Ok;
        }
#endif // KNST_NETWORK_HAVE_FAAD2

        // ── Non-MP3/AAC path: existing packet-by-packet decoder ──
        vid::external_codec::registry::adec dec;
        if (!pcm) {
            dec = vid::external_codec::find_audio_decoder(sd.acodec);
            if (!dec.fn) {
                char m[220]; std::snprintf(m, sizeof(m), "no decoder for %s audio; register one with vid::external_codec::register_audio_decoder() "
                                                        "or use stream copy (mp3/aac/mka)", vid::audio_codec_name(sd.acodec));
                return set_err_(errc::NotSupported, m);
            }
        }
        uint32_t cap = 0, rate = 0, ch = 0; uint64_t written = 0, base_pts = 0; bool first = true;
        for (uint64_t i = 0;; ++i) {
            uint64_t pts = 0;
            auto r = rd.read_packet(ti, i, &pts);
            if (!r) { errc e = rd.last_error(); if (e == errc::NotFound) break; return set_err_(e, "reading an audio packet failed"); }
            if (end_us && pts >= end_us) break;
            const uint8_t* src = nullptr; uint64_t nsamp = 0; vid::audio_codec codec = sd.acodec; uint32_t prate = sd.sample_rate, pch = sd.channels;
            vid::audio_frame af;
            if (pcm) {
                const uint32_t sb = vid::audio_codec_bits(codec) / 8;
                if (!sb || !pch) return set_err_(errc::Corrupt, "bad PCM stream description");
                nsamp = (uint64_t)(r.value.length() / (sb * pch)) * pch; src = r.value.data();
            } else {
                errc e = dec.fn(dec.user, sd, r.value.data(), r.value.length(), af);
                if (e != errc::Ok) return set_err_(e, "the registered audio decoder failed");
                if (!af.valid || !vid::audio_codec_is_pcm(af.codec)) return set_err_(errc::Corrupt, "decoder returned no PCM");
                src = af.data.data(); codec = af.codec; prate = af.sample_rate; pch = af.channels; nsamp = (uint64_t)af.frames * pch;
            }
            if (!prate || !pch) return set_err_(errc::Corrupt, "bad sample rate / channel count");
            const uint64_t frames_here = nsamp / pch;
            if (pts + vid::detail::muldiv_u64(frames_here, 1000000ull, prate) <= start_us) continue;
            if (first) { rate = prate; ch = pch; base_pts = pts; first = false; }
            else if (prate != rate || pch != ch) return set_err_(errc::NotSupported, "audio format changes inside the stream");
            if (!aud::adetail::grow(out.samples, cap, written + nsamp)) return set_err_(errc::OutOfMemory, "audio is too large for memory");
            const uint32_t sb = vid::audio_codec_bits(codec) / 8;
            float* dst = out.samples.data() + written;
            for (uint64_t k = 0; k < nsamp; ++k) dst[k] = vid::audio::sample_to_f32(src + k * sb, codec);
            written += nsamp;
        }
        if (first || written == 0) return set_err_(errc::NotFound, "no audio samples in the selected range");
        aud::adetail::resize_ok(out.samples, (uint32_t)written);
        out.rate = rate; out.channels = ch; out.source_bits = sd.bits_per_sample; out.codec = aud::codec_t::pcm;
        const uint64_t total = written / ch;
        uint64_t skip = start_us > base_pts ? vid::detail::muldiv_u64(start_us - base_pts, rate, 1000000ull) : 0;
        uint64_t endf = total;
        if (end_us && end_us > base_pts) { endf = vid::detail::muldiv_u64(end_us - base_pts, rate, 1000000ull); if (endf > total) endf = total; }
        if (skip >= endf) return set_err_(errc::NotFound, "no audio samples in the selected range");
        if (skip > 0 || endf < total) {
            knst_vector<float> t = aud::dsp::trim(out.samples.data(), (uint32_t)total, ch, (uint32_t)skip, (uint32_t)endf);
            if (t.empty()) return set_err_(errc::OutOfMemory, "trim failed");
            out.samples = std::move(t);
        }
        return errc::Ok;
    }

    /// Writes a whole stream-copied elementary stream (.mp3 / .aac) from container packets.
    result<report> write_elementary_(vid::video_reader& rd, uint32_t ti, const knst_c16string& out, bool adts,
                                     uint64_t start_us, uint64_t end_us) noexcept {
        const vid::stream_desc sd = rd.stream(ti);
        bs::asc_info asc;
        if (adts) {
            knst_byte_string a;
            if (!bs::get_asc(sd, rd.kind(), a)) return fail_<report>(errc::Corrupt, "AAC stream has no AudioSpecificConfig");
            asc = bs::parse_asc(a.data(), a.length());
            if (!asc.ok) return fail_<report>(errc::NotSupported, "only AAC-LC/Main/SSR/LTP can be wrapped into ADTS (HE-AAC is not supported)");
        }
        if (!knst_file::ensure_parent_dir(out)) return fail_<report>(errc::IoError, "cannot create the output directory");
        vid::detail::file_handle f;
        if (f.open(out, vid::detail::file_handle::mode::write) != errc::Ok) return fail_<report>(errc::IoError, "cannot create the output file");
        uint64_t frames = 0, samples = 0;
        for (uint64_t i = 0;; ++i) {
            uint64_t pts = 0;
            auto r = rd.read_packet(ti, i, &pts);
            if (!r) { errc e = rd.last_error(); if (e == errc::NotFound) break; f.close(); return fail_<report>(e, "reading an audio packet failed"); }
            if (end_us && pts >= end_us) break;
            if (pts < start_us) continue;
            if (adts) {
                uint8_t h[7]; bs::adts_header(h, asc, r.value.length());
                if (!f.write(h, 7)) { f.close(); return fail_<report>(errc::IoError, "write failed"); }
            }
            if (r.value.length() && !f.write(r.value.data(), r.value.length())) { f.close(); return fail_<report>(errc::IoError, "write failed"); }
            ++frames; samples += adts ? 1024 : (sd.sample_rate > 24000 ? 1152 : 576);
            if ((frames & 255) == 0 && !progress_(0.5, "copy")) { f.close(); return fail_<report>(errc::BadState, "cancelled"); }
        }
        if (!f.close()) return fail_<report>(errc::IoError, "write failed");
        if (frames == 0) return fail_<report>(errc::NotFound, "no audio packets in the selected range");
        rep_.audio_copied = true; rep_.audio_frames = samples;
        rep_.seconds = sd.sample_rate ? (double)samples / sd.sample_rate : 0.0; rep_.bytes_out = cdetail::file_size_of(out);
        note_(adts ? "AAC stream copied (ADTS headers generated)" : "MP3 stream copied bit-exact");
        return finish_ok_();
    }

    /// MKA (Matroska audio-only) writer for any feed.
    errc write_audio_only_(audio_feed& feed, const knst_c16string& out) noexcept {
        vid::video_writer w; vid::video_writer::config c;
        c.cont = vid::container::MKV; c.has_video = false; c.acodec = feed.codec;
        c.sample_rate = feed.rate; c.channels = feed.channels; c.aextradata = feed.extra;
        auto r = w.open(out, c);
        if (!r) return set_err_(w.last_error() != errc::Ok ? w.last_error() : errc::IoError, "cannot create the MKA file");
        uint64_t tick = 0;
        while (feed.has()) {
            const uint64_t pts = feed.pts_us(); knst_byte_string chunk;
            errc e = feed.pop(chunk);
            if (e != errc::Ok) { w.close(); return set_err_(e, "audio read failed"); }
            auto wr = w.write_audio(chunk.data(), chunk.length(), pts);
            if (!wr) { errc we = w.last_error(); w.close(); return set_err_(we, "MKA write failed"); }
            if ((++tick & 127) == 0 && !progress_(0.5, "audio")) { w.close(); return set_err_(errc::BadState, "cancelled"); }
        }
        rep_.audio_frames = w.audio_frames_written();
        if (!w.close()) return set_err_(errc::IoError, "closing the MKA file failed");
        rep_.bytes_out = cdetail::file_size_of(out); rep_.audio_copied = feed.copied;
        return errc::Ok;
    }

    // ---------------- audio: input = container ----------------
    result<report> audio_from_container_(const knst_c16string& in, const knst_c16string& out, format_t fout,
                                         const audio_options& o, uint64_t s_us, uint64_t e_us) noexcept {
        vid::video_reader rd;
        { auto r = rd.open(in); if (!r) return fail_<report>(rd.last_error(), "input container could not be parsed"); }
        const int ti = cdetail::nth_stream(rd, vid::track_kind::Audio, o.track);
        if (ti < 0) return fail_<report>(errc::NotFound, "the input has no audio track");
        const vid::stream_desc sd = rd.stream((uint32_t)ti);
        using AC = vid::audio_codec;
        const bool dsp = o.needs_dsp();

        if (o.allow_copy && !dsp) {
            if (fout == format_t::mp3 && sd.acodec == AC::MP3) return write_elementary_(rd, (uint32_t)ti, out, false, s_us, e_us);
            if (fout == format_t::aac && sd.acodec == AC::AAC) return write_elementary_(rd, (uint32_t)ti, out, true, s_us, e_us);
        }
        if ((fout == format_t::mp3 || fout == format_t::aac) && !(sd.acodec == AC::MP3 || sd.acodec == AC::AAC)) { /* needs encoder: falls through */ }

        if (fout == format_t::mka) {
            std::unique_ptr<audio_feed> feed;
            const bool mkv_ok = sd.acodec != AC::Unknown && sd.acodec != AC::None &&
                                (vid::audio_codec_is_pcm(sd.acodec) || sd.acodec == AC::MP3 || sd.acodec == AC::AAC ||
                                 rd.kind() == vid::container::MKV || rd.kind() == vid::container::WebM);
            if (o.allow_copy && !dsp && mkv_ok) {
                std::unique_ptr<packet_feed> pf(new (std::nothrow) packet_feed());
                if (!pf) return fail_<report>(errc::OutOfMemory, "out of memory");
                errc e = pf->init(&rd, (uint32_t)ti, AC::Unknown, s_us, e_us);
                if (e != errc::Ok) return fail_<report>(e, "cannot prepare the audio packets");
                feed = std::move(pf);
            } else {
                aud::pcm_buffer buf;
                errc e = load_track_pcm_(rd, (uint32_t)ti, s_us, e_us, buf);
                if (e != errc::Ok) return vid::err_result<report>(e);
                e = process_audio_(buf, o); if (e != errc::Ok) return vid::err_result<report>(e);
                std::unique_ptr<buffer_feed> bf(new (std::nothrow) buffer_feed());
                if (!bf) return fail_<report>(errc::OutOfMemory, "out of memory");
                e = bf->init(std::move(buf), cdetail::codec_for_sample(o.sample));
                if (e != errc::Ok) return fail_<report>(e, "cannot prepare the audio buffer");
                feed = std::move(bf);
            }
            errc e = write_audio_only_(*feed, out);
            if (e != errc::Ok) return vid::err_result<report>(e);
            rep_.seconds = feed->rate ? (double)rep_.audio_frames / feed->rate : 0.0;
            return finish_ok_();
        }

        aud::pcm_buffer buf;
        errc e = load_track_pcm_(rd, (uint32_t)ti, s_us, e_us, buf);
        if (e != errc::Ok) return vid::err_result<report>(e);
        e = process_audio_(buf, o);
        if (e != errc::Ok) return vid::err_result<report>(e);
        e = encode_out_(buf, fout, out, o, dsp);
        if (e != errc::Ok) return vid::err_result<report>(e);
        return finish_ok_();
    }

    // ---------------- audio: input = audio file ----------------
    result<report> audio_from_file_(const knst_c16string& in, const knst_c16string& out, format_t fin, format_t fout,
                                    const audio_options& o, uint64_t s_us, uint64_t e_us) noexcept {
        const bool dsp = o.needs_dsp();
        const bool elem_in = (fin == format_t::mp3 || fin == format_t::aac);

        if (elem_in && o.allow_copy && !dsp && s_us == 0 && e_us == 0 && fin == fout) {
            if (!knst_file::ensure_parent_dir(out) || !knst_file::copy_file(in, out, true)) return fail_<report>(errc::IoError, "file copy failed");
            rep_.audio_copied = true; rep_.bytes_out = cdetail::file_size_of(out); note_("same format, file copied");
            return finish_ok_();
        }
        if (elem_in && fout == format_t::mka && o.allow_copy && !dsp) {
            std::unique_ptr<frame_feed> ff;
            errc e = make_frame_feed_(in, fin == format_t::aac, s_us, e_us, 0, ff);
            if (e != errc::Ok) return vid::err_result<report>(e);
            e = write_audio_only_(*ff, out);
            if (e != errc::Ok) return vid::err_result<report>(e);
            return finish_ok_();
        }

        auto dr = aud::decode_file(in, o.decode);
        if (!dr) {
            return fail_<report>(cdetail::from_status(dr.st), elem_in
                ? "no decoder for this stream: copy it (mp3->mp3/mka, aac->aac/mka), install libsndfile, or register a decoder with aud::codec::registry::add()"
                : "decoding failed (unsupported or damaged file; MP3/OGG/OPUS/AAC need libsndfile or a registered decoder)");
        }
        aud::pcm_buffer buf = std::move(dr.value);
        if (s_us || e_us) {
            const uint32_t fr = buf.frames();
            uint64_t a = vid::detail::muldiv_u64(s_us, buf.rate, 1000000ull);
            uint64_t b = e_us ? vid::detail::muldiv_u64(e_us, buf.rate, 1000000ull) : fr;
            if (b > fr) b = fr;
            if (a >= b) return fail_<report>(errc::InvalidArgument, "the selected range is empty");
            knst_vector<float> t = aud::dsp::trim(buf.samples.data(), fr, buf.channels, (uint32_t)a, (uint32_t)b);
            if (t.empty()) return fail_<report>(errc::OutOfMemory, "trim failed");
            buf.samples = std::move(t);
        }
        errc e = process_audio_(buf, o);
        if (e != errc::Ok) return vid::err_result<report>(e);
        if (fout == format_t::mka) {
            std::unique_ptr<buffer_feed> bf(new (std::nothrow) buffer_feed());
            if (!bf) return fail_<report>(errc::OutOfMemory, "out of memory");
            e = bf->init(std::move(buf), cdetail::codec_for_sample(o.sample));
            if (e != errc::Ok) return fail_<report>(e, "cannot prepare the audio buffer");
            e = write_audio_only_(*bf, out);
            if (e != errc::Ok) return vid::err_result<report>(e);
            return finish_ok_();
        }
        e = encode_out_(buf, fout, out, o, dsp || s_us || e_us);
        if (e != errc::Ok) return vid::err_result<report>(e);
        return finish_ok_();
    }

    errc make_frame_feed_(const knst_c16string& path, bool adts, uint64_t s_us, uint64_t e_us, int64_t shift_us,
                          std::unique_ptr<frame_feed>& out) noexcept {
        knst_file_error fe = knst_file_error::None;
        knst_byte_string data = knst_file::read_file_data<>(path, &fe);
        if (fe != knst_file_error::None || data.empty()) return set_err_(errc::NotFound, "cannot read the audio file");
        bs::elementary el;
        errc e = adts ? bs::scan_adts(data, el) : bs::scan_mp3(data, el);
        if (e != errc::Ok) return set_err_(e, adts ? "ADTS stream is damaged or uses unsupported features" : "no valid MP3 (Layer III) frames found");
        if (adts && !bs::parse_asc(el.asc.data(), el.asc.length()).ok) return set_err_(errc::NotSupported, "unsupported AAC profile");
        std::unique_ptr<frame_feed> ff(new (std::nothrow) frame_feed());
        if (!ff) return set_err_(errc::OutOfMemory, "out of memory");
        e = ff->init(std::move(data), std::move(el), s_us, e_us, shift_us);
        if (e != errc::Ok) return set_err_(e, "cannot prepare the audio frames");
        out = std::move(ff);
        return errc::Ok;
    }

    // ---------------- video: audio planning ----------------
    errc plan_audio_(vid::video_reader& rd, vid::video_reader& rd_ext, vid::container oc, const video_options& o,
                     uint64_t start_us, uint64_t end_us, std::unique_ptr<audio_feed>& feed) noexcept {
        using AC = vid::audio_codec;
        feed.reset();
        if (o.amode == audio_mode::drop) { note_("audio dropped on request"); return errc::Ok; }
        const bool external = !o.external_audio.empty();
        const int64_t shift_us = (int64_t)(o.external_audio_offset_ms * 1000.0);
        const bool dsp = o.audio.needs_dsp();
        vid::video_reader* ar = &rd; int ati = -1;

        // External audio plays from ITS OWN t=0 (not the video's start_us),
        // bounded by the trimmed video's duration. This is what users expect
        // when they write "cut video [10s, 30s] and add this song": the video
        // becomes 20 s and the song plays [0s, 20s).
        if (external) {
            const uint64_t video_end = end_us ? end_us : rd.duration_us();
            const uint64_t trimmed = video_end > start_us ? video_end - start_us : 0;
            start_us = 0;
            end_us = trimmed;   // 0 => unbounded (rare; only if trimmed == 0)
        }

        auto pick_pcm = [&](AC src) -> AC {
            AC t = o.pcm_codec;
            if (t == AC::Unknown || t == AC::None || !vid::audio_codec_is_pcm(t) || !container_supports_audio(oc, t)) {
                t = (vid::audio_codec_is_pcm(src) && container_supports_audio(oc, src)) ? src : AC::PCM_S16LE;
            }
            return t;
        };
        auto cannot_hold = [&](bool hard) -> errc {
            if (hard || o.strict) return set_err_(errc::NotSupported, "the target container cannot store audio (the MP4 writer is video-only, MOV takes PCM16, AVI takes PCM): use .mkv or .mov");
            note_("audio dropped: the target container cannot store it (use .mkv or .mov to keep audio)");
            return errc::Ok;
        };

        if (external) {
            format_t fx = format_t::unknown;
            errc e = sniff_(o.external_audio, fx);
            if (e != errc::Ok) return set_err_(e, "cannot open the external audio file");
            if (!is_av_container(fx)) {
                const bool elem = (fx == format_t::mp3 || fx == format_t::aac);
                const AC ec = fx == format_t::mp3 ? AC::MP3 : AC::AAC;
                if (elem && !dsp && o.amode != audio_mode::to_pcm && container_supports_audio(oc, ec)) {
                    std::unique_ptr<frame_feed> ff;
                    e = make_frame_feed_(o.external_audio, fx == format_t::aac, start_us, end_us, shift_us, ff);
                    if (e != errc::Ok) return e;
                    feed = std::move(ff); note_("external audio stream-copied"); return errc::Ok;
                }
                if (o.amode == audio_mode::copy) return set_err_(errc::NotSupported, "cannot stream-copy this external audio into the target container");
                if (!container_supports_audio(oc, AC::PCM_S16LE)) return cannot_hold(true);
                auto dr = aud::decode_file(o.external_audio, o.audio.decode);
                if (!dr) return set_err_(cdetail::from_status(dr.st), "cannot decode the external audio (MP3/OGG/AAC need libsndfile or a registered decoder)");
                aud::pcm_buffer buf = std::move(dr.value);
                e = window_buffer_(buf, start_us, end_us, shift_us); if (e != errc::Ok) return e;
                e = process_audio_(buf, o.audio); if (e != errc::Ok) return e;
                std::unique_ptr<buffer_feed> bf(new (std::nothrow) buffer_feed());
                if (!bf) return set_err_(errc::OutOfMemory, "out of memory");
                e = bf->init(std::move(buf), pick_pcm(AC::PCM_S16LE)); if (e != errc::Ok) return set_err_(e, "bad audio buffer");
                feed = std::move(bf); note_("external audio decoded and converted to PCM"); return errc::Ok;
            }
            { auto r = rd_ext.open(o.external_audio); if (!r) return set_err_(rd_ext.last_error(), "external container could not be parsed"); }
            ar = &rd_ext; ati = cdetail::nth_stream(*ar, vid::track_kind::Audio, o.audio_track);
            if (ati < 0) return set_err_(errc::NotFound, "the external file has no audio track");
        } else {
            ati = cdetail::nth_stream(rd, vid::track_kind::Audio, o.audio_track);
            if (ati < 0) return errc::Ok;                                 // silent source: nothing to do
        }

        const vid::stream_desc asd = ar->stream((uint32_t)ati);
        const AC sc = asd.acodec;
        const bool pcm_src = vid::audio_codec_is_pcm(sc);
        const vid::container sk = ar->kind();
        const bool extra_ok = pcm_src || sc == AC::MP3 || sc == AC::AAC ||
                              ((sc == AC::FLAC || sc == AC::Opus || sc == AC::Vorbis) && (sk == vid::container::MKV || sk == vid::container::WebM));
        if (o.amode != audio_mode::to_pcm && !dsp && extra_ok && container_supports_audio(oc, sc)) {
            std::unique_ptr<packet_feed> pf(new (std::nothrow) packet_feed());
            if (!pf) return set_err_(errc::OutOfMemory, "out of memory");
            errc e = pf->init(ar, (uint32_t)ati, pcm_src ? sc : AC::Unknown, start_us, end_us);
            if (e != errc::Ok) return set_err_(e, "cannot prepare the audio packets");
            feed = std::move(pf); note_("audio stream-copied"); return errc::Ok;
        }
        if (o.amode == audio_mode::copy) return set_err_(errc::NotSupported, "audio cannot be stream-copied into this container (codec/container mismatch or audio processing requested)");
                if (!container_supports_audio(oc, AC::PCM_S16LE)) return cannot_hold(false);
        // MP3 is decodable via knst_audio's minimp3 (see load_track_pcm_
        // special case). Do not drop it here just because no external
        // decoder is registered.
        const bool has_native_decoder =
            vid::external_codec::find_audio_decoder(sc).fn != nullptr ||
            sc == AC::MP3;
        if (!pcm_src && !has_native_decoder) {
            char m[200]; std::snprintf(m, sizeof(m), "%s audio cannot be stored in this container and no decoder is registered to convert it", vid::audio_codec_name(sc));
            if (o.strict || external) return set_err_(errc::NotSupported, m);
            notef_("audio dropped: %s", m); return errc::Ok;
        }
        aud::pcm_buffer buf;
        errc e = load_track_pcm_(*ar, (uint32_t)ati, start_us, end_us, buf);
        if (e != errc::Ok) return e;
        if (external) { e = window_buffer_(buf, 0, 0, shift_us); if (e != errc::Ok) return e; }
        e = process_audio_(buf, o.audio); if (e != errc::Ok) return e;
        std::unique_ptr<buffer_feed> bf(new (std::nothrow) buffer_feed());
        if (!bf) return set_err_(errc::OutOfMemory, "out of memory");
        e = bf->init(std::move(buf), pick_pcm(sc)); if (e != errc::Ok) return set_err_(e, "bad audio buffer");
        feed = std::move(bf); note_("audio decoded and re-written as PCM");
        return errc::Ok;
    }

    /// Time window + A/V offset on a decoded buffer (positive shift = delay -> leading silence).
    errc window_buffer_(aud::pcm_buffer& b, uint64_t start_us, uint64_t end_us, int64_t shift_us) noexcept {
        const uint32_t fr = b.frames();
        if (start_us || end_us) {
            uint64_t a = vid::detail::muldiv_u64(start_us, b.rate, 1000000ull);
            uint64_t e = end_us ? vid::detail::muldiv_u64(end_us, b.rate, 1000000ull) : fr;
            if (e > fr) e = fr;
            if (a >= e) return set_err_(errc::InvalidArgument, "the selected audio range is empty");
            knst_vector<float> t = aud::dsp::trim(b.samples.data(), fr, b.channels, (uint32_t)a, (uint32_t)e);
            if (t.empty()) return set_err_(errc::OutOfMemory, "trim failed");
            b.samples = std::move(t);
        }
        if (shift_us != 0) {
            const uint64_t sf = vid::detail::muldiv_u64((uint64_t)(shift_us < 0 ? -shift_us : shift_us), b.rate, 1000000ull);
            const uint32_t cur = b.frames();
            if (shift_us > 0) {
                knst_vector<float> v;
                if (!aud::adetail::resize_ok(v, (uint32_t)((sf + cur) * b.channels))) return set_err_(errc::OutOfMemory, "offset failed");
                std::memset(v.data(), 0, (size_t)sf * b.channels * sizeof(float));
                std::memcpy(v.data() + (size_t)sf * b.channels, b.samples.data(), (size_t)cur * b.channels * sizeof(float));
                b.samples = std::move(v);
            } else {
                if (sf >= cur) return set_err_(errc::InvalidArgument, "negative audio offset removes the whole track");
                knst_vector<float> t = aud::dsp::trim(b.samples.data(), cur, b.channels, (uint32_t)sf, cur);
                if (t.empty()) return set_err_(errc::OutOfMemory, "offset failed");
                b.samples = std::move(t);
            }
        }
        return errc::Ok;
    }

    // ---------------- video ----------------
    result<report> convert_video_(const knst_c16string& in, const knst_c16string& out, const video_options& o) noexcept {
        using VC = vid::video_codec;
        if (in.empty() || out.empty()) return fail_<report>(errc::InvalidArgument, "empty path");
        format_t fin = format_t::unknown;
        errc e = sniff_(in, fin);
        if (e != errc::Ok) return fail_<report>(e, "cannot open input file");
        if (!is_av_container(fin)) return fail_<report>(errc::NotSupported, "the input is not a video container (use convert_audio for audio files)");
        const format_t fout = o.container != format_t::unknown ? o.container : format_from_path(out);
        if (fout == format_t::webm) return fail_<report>(errc::NotSupported, "WebM cannot be written by this library; use .mkv (same streams)");
        if (fout != format_t::avi && fout != format_t::mkv && fout != format_t::mp4 && fout != format_t::mov)
            return fail_<report>(errc::InvalidArgument, "output container must be .avi, .mkv, .mp4 or .mov");
        const vid::container oc = to_vid_container(fout);
        rep_.out_format = fout;

        vid::video_reader rd, rd_ext;
        { auto r = rd.open(in); if (!r) return fail_<report>(rd.last_error(), "input container could not be parsed"); }
        const int vt = rd.video_stream();
        if (vt < 0) return fail_<report>(errc::NotFound, "the input has no video track (use convert_audio to extract the sound)");
        const vid::stream_desc vsd = rd.stream((uint32_t)vt);

        uint64_t start_us = cdetail::sec_us(o.start_s); const uint64_t end_us = cdetail::sec_us(o.end_s);
        if (end_us && end_us <= start_us) return fail_<report>(errc::InvalidArgument, "end_s must be greater than start_s");

        // ---------- mode decision ----------
        const bool filt = o.filters.active();
        const bool needs_tc = filt || o.fps > 0.0 || (o.jpeg_quality > 0 && vsd.vcodec == VC::MJPEG) ||
                              (o.vcodec != VC::Unknown && o.vcodec != vsd.vcodec);
        const bool copyable_codec = vsd.vcodec != VC::RAW && vsd.vcodec != VC::Unknown;
        bool copy;
        if (o.vmode == video_mode::copy) {
            if (needs_tc) return fail_<report>(errc::InvalidArgument, "copy mode cannot apply filters / fps / quality changes");
            copy = true;
        } else if (o.vmode == video_mode::transcode) copy = false;
        else copy = !needs_tc && copyable_codec && container_supports_video(oc, vsd.vcodec);
        if (copy && !(copyable_codec && container_supports_video(oc, vsd.vcodec))) {
            char m[240]; std::snprintf(m, sizeof(m), "%s video cannot be stored in %s by this library's writer; pick another container (mkv takes almost everything)",
                                       vid::video_codec_name(vsd.vcodec), format_name(fout));
            return fail_<report>(errc::NotSupported, m);
        }
        VC tc = VC::MJPEG;
        if (!copy) {
            if (o.vcodec != VC::Unknown) tc = o.vcodec;
            if (tc != VC::MJPEG && tc != VC::RAW) return fail_<report>(errc::NotSupported, "re-encoding is only possible to MJPEG or RAW (no H.264/H.265/VP9/AV1 encoder exists in this library)");
            if (tc == VC::RAW && oc != vid::container::AVI) return fail_<report>(errc::NotSupported, "RAW video can only be stored in AVI");
            if (!container_supports_video(oc, tc)) return fail_<report>(errc::NotSupported, "the target container cannot hold the requested codec");
            if (vsd.vcodec != VC::MJPEG && vsd.vcodec != VC::RAW && !vid::external_codec::find_video_decoder(vsd.vcodec).fn) {
                char m[260]; std::snprintf(m, sizeof(m), "%s cannot be decoded here: register a decoder with vid::external_codec::register_video_decoder(), or use stream copy (no filters)",
                                           vid::video_codec_name(vsd.vcodec));
                return fail_<report>(errc::NotSupported, m);
            }
        }

        // ---------- copy: start position + bitstream framing ----------
        knst_byte_string vextra; int conv = 0; bs::param_sets ps; const bool h265 = vsd.vcodec == VC::H265;
        uint64_t first_idx = 0;
        if (copy) {
            if (start_us) {
                first_idx = rd.keyframe_at_or_before(rd.frame_at_time(start_us));
                uint64_t p0 = 0;
                auto r = rd.read_packet((uint32_t)vt, first_idx, &p0);
                if (!r) return fail_<report>(rd.last_error(), "cannot seek in the video");
                if (p0 != start_us) notef_("stream copy cuts at the previous key frame (%.3f s instead of %.3f s)", (double)p0 / 1e6, (double)start_us / 1e6);
                start_us = p0;
            }
            if (vsd.vcodec == VC::H264 || h265) {
                bool src_lp = (fin != format_t::avi);
                if (fin == format_t::avi) {
                    for (uint64_t i = 0; i < 64; ++i) {
                        auto r = rd.read_packet((uint32_t)vt, i);
                        if (!r) break;
                        if (r.value.length() >= 4) { src_lp = !bs::is_annexb(r.value.data(), r.value.length()); break; }
                    }
                }
                const bool dst_annexb = (oc == vid::container::AVI);
                if (src_lp) {
                    if (!(h265 ? bs::parse_hvcc(vsd.extradata, ps) : bs::parse_avcc(vsd.extradata, ps)))
                        return fail_<report>(errc::Corrupt, "missing or damaged avcC/hvcC configuration in the source");
                    if (dst_annexb) { conv = 1; note_("H.264/H.265 rewritten from length-prefixed to Annex-B for AVI"); }
                    else vextra = vsd.extradata;
                } else if (!dst_annexb) {
                    if (h265) return fail_<report>(errc::NotSupported, "Annex-B H.265 -> MP4/MKV needs hvcC construction (not implemented)");
                    knst_byte_string sps, pps;
                    for (uint64_t i = 0; i < 400 && (sps.empty() || pps.empty()); ++i) {
                        auto r = rd.read_packet((uint32_t)vt, i);
                        if (!r) break;
                        bs::collect_sps_pps(r.value.data(), r.value.length(), sps, pps);
                    }
                    if (sps.length() < 4 || pps.empty()) return fail_<report>(errc::Corrupt, "no SPS/PPS found in the H.264 stream");
                    vextra = bs::make_avcc(sps, pps); conv = 2;
                    note_("H.264 rewritten from Annex-B to length-prefixed with a generated avcC");
                }
            } else if (vsd.vcodec == VC::AV1) vextra = vsd.extradata;
            if (oc == vid::container::AVI && vsd.vcodec != VC::MJPEG && vsd.vcodec != VC::H264 && vsd.vcodec != VC::MPEG4)
                note_("warning: this codec in AVI is non-standard; many players will not open it");
            if (oc == vid::container::AVI && vsd.vcodec != VC::MJPEG) note_("warning: AVI cannot represent B-frame reordering; streams with B-frames may play wrongly");
                }

        // ---------- audio ----------
        std::unique_ptr<audio_feed> feed;
        e = plan_audio_(rd, rd_ext, oc, o, start_us, end_us, feed);
        if (e != errc::Ok) return vid::err_result<report>(e);

        // ---------- pump ----------
        std::unique_ptr<video_pump> pump;
        uint32_t ow = vsd.width, oh = vsd.height;
        if (copy) {
            std::unique_ptr<copy_pump> cp(new (std::nothrow) copy_pump());
            if (!cp) return fail_<report>(errc::OutOfMemory, "out of memory");
            cp->rd = &rd; cp->track = (uint32_t)vt; cp->idx = first_idx; cp->base_us = start_us; cp->end_us = end_us;
            cp->allow_empty = (oc == vid::container::AVI); cp->h265 = h265; cp->conv = conv; cp->ps = ps;
            pump = std::move(cp);
        } else {
            std::unique_ptr<transcode_pump> tp(new (std::nothrow) transcode_pump());
            if (!tp) return fail_<report>(errc::OutOfMemory, "out of memory");
            tp->rd = &rd; tp->flt = &o.filters; tp->fps = o.fps; tp->start_us = start_us; tp->end_us = end_us;
            tp->dur_us = rd.duration_us(); tp->idx = start_us ? rd.frame_at_time(start_us) : 0;
            pump = std::move(tp);
        }
        e = pump->prime();
        if (e != errc::Ok) return fail_<report>(e, "cannot read or decode the first video frame");
        if (!copy) { if (!pump->has()) return fail_<report>(errc::NotFound, "no video frames in the selected range"); ow = pump->out_w; oh = pump->out_h; }
        if (ow == 0 || oh == 0) return fail_<report>(errc::Corrupt, "video dimensions are unknown");

        // ---------- writer ----------
        vid::video_writer w; vid::video_writer::config c;
        c.cont = oc; c.has_video = true; c.vcodec = copy ? vsd.vcodec : tc; c.pix = vid::pixel_format::RGB24;
        c.width = ow; c.height = oh;
        if (!copy && o.fps > 0.0) vid::detail::rational_from_fps(o.fps, c.fps_num, c.fps_den);
        else { c.fps_num = vsd.fps_num ? vsd.fps_num : 30; c.fps_den = vsd.fps_den ? vsd.fps_den : 1; }
        c.jpeg_quality = o.jpeg_quality > 0 ? o.jpeg_quality : 85; c.jpeg_sub = o.jpeg_sub;
        c.raw_pix = vid::pixel_format::BGR24; c.vextradata = vextra;
        c.meta = rd.meta(); if (!o.title.empty()) c.meta.title = o.title;
        if (feed) { c.acodec = feed->codec; c.sample_rate = feed->rate; c.channels = feed->channels; c.aextradata = feed->extra; }
        { auto r = w.open(out, c); if (!r) return fail_<report>(w.last_error() != errc::Ok ? w.last_error() : errc::IoError, "cannot create the output file"); }

        // ---------- interleaved mux loop ----------
        const uint64_t total_us = end_us ? end_us - start_us : (rd.duration_us() > start_us ? rd.duration_us() - start_us : 0);
        uint64_t tick = 0;
        for (;;) {
            const bool vh = pump->has(), ah = feed && feed->has();
            if (!vh && !ah) break;
            if (vh && (!ah || pump->pts_us() <= feed->pts_us())) {
                e = pump->step(w);
                if (e != errc::Ok) { w.close(); return fail_<report>(e, "writing a video frame failed"); }
            } else {
                const uint64_t ap = feed->pts_us(); knst_byte_string chunk;
                e = feed->pop(chunk);
                if (e != errc::Ok) { w.close(); return fail_<report>(e, "reading audio failed"); }
                auto wr = w.write_audio(chunk.data(), chunk.length(), ap);
                if (!wr) { errc we = w.last_error(); w.close(); return fail_<report>(we != errc::Ok ? we : errc::IoError, "writing audio failed"); }
            }
            if ((++tick & 31) == 0) {
                if (!progress_(total_us && pump->has() ? (double)pump->pts_us() / (double)total_us : 0.99, "convert")) { w.close(); return fail_<report>(errc::BadState, "cancelled"); }
            }
        }
        rep_.video_frames = w.frames_written(); rep_.audio_frames = w.audio_frames_written();
        rep_.seconds = (double)(w.pts_next_us()) / 1e6;
        if (!w.close()) return fail_<report>(errc::IoError, "finalising the output file failed (disk full?)");
        rep_.video_copied = copy; rep_.audio_copied = feed ? feed->copied : false;
        rep_.bytes_out = cdetail::file_size_of(out);
        progress_(1.0, "done");
        return finish_ok_();
    }

    // ---------------- frame export ----------------
    errc grab_(vid::video_reader& rd, uint64_t idx, const knst_c16string& path, format_t fo, int quality, const video_filters* flt) noexcept {
        const vid::stream_desc& sd = rd.stream((uint32_t)rd.video_stream());
        const bool filt = flt && flt->active();
        if (fo == format_t::jpeg && sd.vcodec == vid::video_codec::MJPEG && !filt) {
            auto r = rd.read_frame(idx);
            if (!r) return set_err_(rd.last_error(), "cannot read the frame");
            uint64_t real = idx; while (real > 0 && r.value.data.length() == 0) { --real; r = rd.read_frame(real); if (!r) return set_err_(rd.last_error(), "cannot read the frame"); }
            knst_byte_string patched;
            const bool inj = vid::mjpeg::inject_std_dht(r.value.data.data(), r.value.data.length(), patched);
            const knst_byte_string& src = inj ? patched : r.value.data;
            if (!cdetail::write_bytes(path, src.data(), src.length())) return set_err_(errc::IoError, "cannot write the image");
            return errc::Ok;
        }
        auto r = rd.read_frame_rgb24(idx);
        if (!r) return set_err_(rd.last_error() != errc::Ok ? rd.last_error() : errc::NotSupported, "cannot decode the frame (non-MJPEG video needs a registered decoder)");
        knst_byte_string px; uint32_t w = r.value.width, h = r.value.height; bool grey = false;
        if (filt) { errc e = cdetail::apply_filters(r.value.data, w, h, *flt, px, w, h, grey); if (e != errc::Ok) return set_err_(e, "filter failed"); }
        else px = std::move(r.value.data);
        if (fo == format_t::jpeg) {
            vid::mjpeg::encoder enc; vid::mjpeg::encoder::options eo; eo.quality = quality < 1 ? 1 : (quality > 100 ? 100 : quality);
            errc e = enc.configure(eo); knst_byte_string jpg;
            if (e == errc::Ok) e = enc.encode(px.data(), w, h, 0, grey ? vid::pixel_format::GREY : vid::pixel_format::RGB24, jpg);
            if (e != errc::Ok) return set_err_(e, "JPEG encoding failed");
            if (!cdetail::write_bytes(path, jpg.data(), jpg.length())) return set_err_(errc::IoError, "cannot write the image");
            return errc::Ok;
        }
        char hdr[48]; int hl = std::snprintf(hdr, sizeof(hdr), "%s\n%u %u\n255\n", grey ? "P5" : "P6", w, h);
        knst_byte_string file; file.append(reinterpret_cast<const uint8_t*>(hdr), (uint32_t)hl); file.append(px.data(), px.length());
        if (!cdetail::write_bytes(path, file.data(), file.length())) return set_err_(errc::IoError, "cannot write the image");
        return errc::Ok;
    }
};

// ============================================================
// Free-function conveniences
// ============================================================

inline result<report> convert(const knst_c16string& in, const knst_c16string& out) noexcept { converter c; return c.convert(in, out); }
inline result<report> convert_audio(const knst_c16string& in, const knst_c16string& out, const audio_options& o = audio_options()) noexcept { converter c; return c.convert_audio(in, out, o); }
inline result<report> convert_video(const knst_c16string& in, const knst_c16string& out, const video_options& o = video_options()) noexcept { converter c; return c.convert_video(in, out, o); }
inline result<report> extract_audio(const knst_c16string& in, const knst_c16string& out, const audio_options& o = audio_options()) noexcept { converter c; return c.convert_audio(in, out, o); }
inline result<report> strip_audio(const knst_c16string& in, const knst_c16string& out) noexcept { converter c; return c.strip_audio(in, out); }
inline result<report> add_audio(const knst_c16string& v, const knst_c16string& a, const knst_c16string& out, const video_options& o = video_options()) noexcept { converter c; return c.add_audio(v, a, out, o); }

// ============================================================
// JSON bridge
// ============================================================

#if defined(KNST_JSON_HPP)

inline knst_json report_to_json(const report& r) noexcept {
    knst_json j = knst_json::make_object();
    if (!j.is_object()) return knst_json();
    j.set("ok",           knst_json(r.ok));
    j.set("format",       knst_json(format_name(r.out_format)));
    j.set("video_copied", knst_json(r.video_copied));
    j.set("audio_copied", knst_json(r.audio_copied));
    j.set("video_frames", knst_json((int64_t)r.video_frames));
    j.set("audio_frames", knst_json((int64_t)r.audio_frames));
    j.set("bytes_out",    knst_json((int64_t)r.bytes_out));
    j.set("seconds",      knst_json(r.seconds));
    knst_json n = knst_json::make_array();
    for (uint32_t i = 0; i < r.notes.size(); ++i) n.push_back(knst_json(r.notes[i]));
    j.set("notes", std::move(n));
    return j;
}

#endif // KNST_JSON_HPP

} // namespace knst_conv