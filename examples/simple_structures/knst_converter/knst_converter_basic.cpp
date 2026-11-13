/*
  knst_converter_basic.cpp

  Comprehensive tutorial-style demonstration of the KernelNucleusT
  converter module (knst_converter.hpp).

  Requirement: KernelNucleusT.hpp (pulls in knst_audio.hpp, knst_video.hpp,
  then knst_converter.hpp).

  Sections
  --------
    A. Types & format         1-3   format_from_path, format_name, is_av_container
    B. Support matrix         4-5   container_supports_video / _audio
    C. Errors                 6-7   probe on missing / invalid input
    D. Audio generation       8     create test WAV (aud::encode_file)
    E. Probe positive         9     probe on the generated WAV
    F. Format conversion     10-13  WAV -> AIFF / AU / RAW / WAV
    G. Processing            14-17  resample, mono, gain, normalize, trim
    H. Dispatcher            18     convert() picks audio vs video
    I. Errors                19-20  missing input, bad output, bad range
    J. Encoder registry      21-22  register_audio_encoder + use it
    K. Video/AV guards       23-25  extract_frame / strip_audio on audio
    L. Report & JSON         26-27  report fields + report_to_json
    M. Cleanup               28
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>

namespace aud = knst_aud;


/* =====================================================================
 *  Console helpers
 * ===================================================================== */

static int s_pass = 0, s_fail = 0;

static void section(const char* title, const char* what, const char* expect) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "  WHAT    : " << what   << "\n";
    std::cout << "  EXPECT  : " << expect << "\n";
    std::cout << "--------------------------------------------------\n";
}

static void check(const char* name, bool ok) {
    if (ok) { ++s_pass; std::cout << "  >>> PASS : " << name << "\n"; }
    else    { ++s_fail; std::cout << "  >>> FAIL : " << name << "\n"; }
}

static void info(const char* what) {
    std::cout << "  " << what << "\n";
}

static knst_c16string tmp_path(const char* name) {
    return knst_file::join_path(knst_file::get_temp_dir(), knst_c16string(name));
}

static bool file_exists_and_nonempty(const knst_c16string& p) {
    if (!knst_file::file_exists(p)) return false;
    return knst_file::get_file_size(p) > 0;
}

static const char* to_cstr(const knst_c16string& s) {
    static thread_local std::string buf;
    knst_byte_string u = knst_dev::to_utf8(s);
    buf.assign(reinterpret_cast<const char*>(u.data()), u.length());
    return buf.c_str();
}

// Progress callback (used in section 22).
static int s_prog_calls = 0;
static bool progress_cb(void* /*user*/, double /*fraction*/, const char* /*stage*/) {
    ++s_prog_calls;
    return true;
}

// Dummy encoder used in section 21: registered for MP3 because the
// header has no native MP3 encoder. OGG is NOT used here — this build
// can have a native OGG-Vorbis encoder (KNST_NETWORK_HAVE_VORBIS),
// in which case encode_out_() would call that and never reach the
// user-registered path, hiding what section 21 is trying to prove.
static int s_enc_calls = 0;
static knst_dev::result<void> dummy_mp3_encoder(void* /*user*/, const aud::pcm_buffer& pcm,
                                                const knst_c16string& path) {
    ++s_enc_calls;
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), "MP3-DUMMY %llu samples\n",
                  (unsigned long long)pcm.samples.size());
    knst_file::write_file_data(path, reinterpret_cast<const unsigned char*>(hdr),
                               (uint32_t)std::strlen(hdr));
    return knst_dev::result<void>::success();
}


/* =====================================================================
 *  Generate a test WAV file (48 kHz stereo, 1.0 s, 440 Hz + 880 Hz)
 * ===================================================================== */

static bool make_test_wav(const knst_c16string& path, uint32_t rate = 48000) {
    const uint32_t frames = rate;
    const uint32_t ch = 2;
    knst_vector<float> s;
    s.resize((size_t)frames * ch);
    for (uint32_t i = 0; i < frames; ++i) {
        float t = (float)i / (float)rate;
        float l = 0.35f * std::sin(2.0f * 3.14159265f * 440.0f * t);
        float r = 0.35f * std::sin(2.0f * 3.14159265f * 880.0f * t);
        s[(size_t)i * ch + 0] = l;
        s[(size_t)i * ch + 1] = r;
    }
    aud::pcm_buffer b;
    b.samples     = std::move(s);
    b.rate        = rate;
    b.channels    = ch;
    b.source_bits = 16;
    b.codec       = aud::codec_t::pcm;

    aud::encode_options eo;
    eo.container = aud::container_t::wav;
    eo.fmt       = aud::sample_fmt::i16;
    eo.dither    = false;
    if (!knst_file::ensure_parent_dir(path)) return false;
    auto r = aud::encode_file(path, b, eo);
    return static_cast<bool>(r);
}


/* =====================================================================
 *  main
 * ===================================================================== */

int main() {

    std::cout << "knst_converter_basic — comprehensive test suite\n";
    std::cout << "sizeof(knst_conv::report)      : " << sizeof(knst_conv::report)      << " bytes\n";
    std::cout << "sizeof(knst_conv::audio_options): " << sizeof(knst_conv::audio_options) << " bytes\n";
    std::cout << "sizeof(knst_conv::video_options): " << sizeof(knst_conv::video_options) << " bytes\n";

    using knst_conv::format_t;
    using knst_conv::format_name;
    using knst_conv::format_from_path;
    using knst_conv::is_av_container;

    const knst_c16string in_wav  = tmp_path("knst_conv_test_in.wav");
    const knst_c16string out_wav = tmp_path("knst_conv_test_out.wav");
    const knst_c16string out_aiff= tmp_path("knst_conv_test_out.aiff");
    const knst_c16string out_au  = tmp_path("knst_conv_test_out.au");
    const knst_c16string out_raw = tmp_path("knst_conv_test_out.raw");
    const knst_c16string out_ogg = tmp_path("knst_conv_test_out.ogg");
    const knst_c16string out_mp3 = tmp_path("knst_conv_test_out.mp3");
    const knst_c16string missing = tmp_path("knst_conv_no_such_file.wav");

    knst_file::remove_file(in_wav);
    knst_file::remove_file(out_wav);
    knst_file::remove_file(out_aiff);
    knst_file::remove_file(out_au);
    knst_file::remove_file(out_raw);
    knst_file::remove_file(out_ogg);
    knst_file::remove_file(out_mp3);


    /* =================================================================
     *  1) format_from_path
     * ================================================================= */
    section("1) format_from_path",
            "Extension -> format_t for every supported family",
            "correct id; case-insensitive; unknown for garbage");
    {
        check("wav",     format_from_path(u"a.wav")    == format_t::wav);
        check("wave",    format_from_path(u"a.wave")   == format_t::wav);
        check("WAV (up)",format_from_path(u"A.WAV")    == format_t::wav);
        check("mp3",     format_from_path(u"x.mp3")    == format_t::mp3);
        check("mp4",     format_from_path(u"clip.mp4") == format_t::mp4);
        check("m4a",     format_from_path(u"music.m4a")== format_t::mp4);
        check("mkv",     format_from_path(u"y.mkv")    == format_t::mkv);
        check("aiff",    format_from_path(u"z.aiff")   == format_t::aiff);
        check("aif",     format_from_path(u"z.aif")    == format_t::aiff);
        check("jpeg",    format_from_path(u"p.jpg")    == format_t::jpeg);
        check("raw",     format_from_path(u"r.raw")    == format_t::raw_pcm);
        check("noext",   format_from_path(u"nofile")   == format_t::unknown);
        check("unknown", format_from_path(u"a.xyz")    == format_t::unknown);
        check("dots",    format_from_path(u"v1.2.mkv") == format_t::mkv);
    }


    /* =================================================================
     *  2) format_name
     * ================================================================= */
    section("2) format_name",
            "format_t -> human string",
            "every enum has a name; unknown is 'unknown'");
    {
        bool ok = true;
        const format_t all[] = {
            format_t::unknown, format_t::avi, format_t::mkv, format_t::webm,
            format_t::mp4, format_t::mov, format_t::wav, format_t::aiff,
            format_t::au, format_t::flac, format_t::ogg, format_t::mp3,
            format_t::aac, format_t::raw_pcm, format_t::mka, format_t::jpeg,
            format_t::ppm
        };
        for (format_t f : all) {
            const char* n = format_name(f);
            if (!n || !*n) { ok = false; break; }
        }
        info("(all enum values have non-empty names)");
        check("format_name", ok && std::strcmp(format_name(format_t::unknown), "unknown") == 0);
    }


    /* =================================================================
     *  3) is_av_container
     * ================================================================= */
    section("3) is_av_container",
            "AV container vs. audio-only / image",
            "avi/mkv/webm/mp4/mov true; others false");
    {
        check("avi",  is_av_container(format_t::avi));
        check("mkv",  is_av_container(format_t::mkv));
        check("webm", is_av_container(format_t::webm));
        check("mp4",  is_av_container(format_t::mp4));
        check("mov",  is_av_container(format_t::mov));
        check("wav not AV",  !is_av_container(format_t::wav));
        check("mp3 not AV",  !is_av_container(format_t::mp3));
        check("jpeg not AV", !is_av_container(format_t::jpeg));
    }


    /* =================================================================
     *  4) container_supports_video
     * ================================================================= */
    section("4) container_supports_video",
            "Which containers can hold which codecs (this library's writers)",
            "AVI takes MJPEG/H264/MPEG4/...; MP4/MOV only MJPEG/H264/H265; MKV almost all");
    {
        using VC = knst_vid::video_codec;
        using C  = knst_vid::container;
        check("AVI + MJPEG",   knst_conv::container_supports_video(C::AVI,  VC::MJPEG));
        check("AVI + H264",    knst_conv::container_supports_video(C::AVI,  VC::H264));
        check("AVI + RAW",     knst_conv::container_supports_video(C::AVI,  VC::RAW));
        check("MKV + H265",    knst_conv::container_supports_video(C::MKV,  VC::H265));
        check("MKV + VP9",     knst_conv::container_supports_video(C::MKV,  VC::VP9));
        check("MKV - RAW",     !knst_conv::container_supports_video(C::MKV, VC::RAW));
        check("MP4 + H264",    knst_conv::container_supports_video(C::MP4,  VC::H264));
        check("MP4 + MJPEG",   knst_conv::container_supports_video(C::MP4,  VC::MJPEG));
        check("MP4 - VP9",     !knst_conv::container_supports_video(C::MP4, VC::VP9));
        check("MOV - VP9",     !knst_conv::container_supports_video(C::MOV, VC::VP9));
    }


    /* =================================================================
     *  5) container_supports_audio
     * ================================================================= */
    section("5) container_supports_audio",
            "Which containers can store which audio codecs",
            "AVI only PCM; MOV only PCM_S16LE; MKV/WebM anything; MP4 writer has no audio");
    {
        using AC = knst_vid::audio_codec;
        using C  = knst_vid::container;
        check("AVI + PCM_S16LE",     knst_conv::container_supports_audio(C::AVI,  AC::PCM_S16LE));
        check("AVI + AAC (no)",     !knst_conv::container_supports_audio(C::AVI,  AC::AAC));
        check("MOV + PCM_S16LE",     knst_conv::container_supports_audio(C::MOV,  AC::PCM_S16LE));
        check("MOV + PCM_F32LE (no)",!knst_conv::container_supports_audio(C::MOV, AC::PCM_F32LE));
        check("MKV + AAC",           knst_conv::container_supports_audio(C::MKV,  AC::AAC));
        check("MKV + Opus",          knst_conv::container_supports_audio(C::MKV,  AC::Opus));
        check("WebM + Vorbis",       knst_conv::container_supports_audio(C::WebM, AC::Vorbis));
        check("MP4 + PCM (no)",     !knst_conv::container_supports_audio(C::MP4,  AC::PCM_S16LE));
        check("MP4 + AAC (no writer)", !knst_conv::container_supports_audio(C::MP4, AC::AAC));
    }


    /* =================================================================
     *  6) probe — error paths
     * ================================================================= */
    section("6) probe: missing / unsupported",
            "probe() must fail cleanly on a missing or nonsense file",
            "fails with a valid errc; last_message() explains it");
    {
        knst_conv::converter cv;

        auto r1 = cv.probe(missing);
        info(r1 ? "unexpected success" : cv.last_message().data() ? to_cstr(cv.last_message()) : "(empty)");
        check("probe(missing) fails", !r1);

        const knst_c16string garbage = tmp_path("knst_conv_garbage.bin");
        knst_file::write_file_text(garbage, u"this is not a media file, just text");
        auto r2 = cv.probe(garbage);
        info(r2 ? "unexpected success" : "(correctly rejected)");
        check("probe(garbage) fails", !r2);
        knst_file::remove_file(garbage);
    }


    /* =================================================================
     *  7) probe — empty path
     * ================================================================= */
    section("7) probe: empty path",
            "probe(u\"\") must fail with InvalidArgument",
            "returns error, does not crash");
    {
        knst_conv::converter cv;
        auto r = cv.probe(knst_c16string());
        check("probe(empty) fails", !r);
    }


    /* =================================================================
     *  8) Create a test WAV programmatically
     * ================================================================= */
    section("8) Generate test WAV",
            "aud::encode_file() -> 1 s of 48 kHz stereo PCM",
            "file exists and is non-empty");
    {
        const bool ok = make_test_wav(in_wav);
        if (!ok) {
            std::cout << "  (could not create the test WAV — skipping audio tests)\n";
        } else {
            info("(test wav created)");
        }
        check("test wav generated", ok && file_exists_and_nonempty(in_wav));
    }

    if (!file_exists_and_nonempty(in_wav)) {
        std::cout << "\n  (no test WAV available — aborting the audio sections)\n";
        std::cout << "\n==================================================\n";
        std::cout << "  RESULT: " << s_pass << " PASS / " << s_fail << " FAIL\n";
        std::cout << "==================================================\n";
        return s_fail == 0 ? 0 : 1;
    }


    /* =================================================================
     *  9) probe — positive
     * ================================================================= */
    section("9) probe (WAV)",
            "probe a valid audio file",
            "success, has_audio=true, duration ~1 s, rate 48000, channels 2");
    {
        knst_conv::converter cv;
        auto r = cv.probe(in_wav);
        if (r) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "format=%s has_audio=%d duration=%.3f s rate=%u ch=%u",
                          format_name(r.value.format), (int)r.value.has_audio,
                          r.value.duration_s, r.value.au.rate, r.value.au.channels);
            info(buf);
            const bool ok = r.value.has_audio &&
                            r.value.au.rate == 48000 &&
                            r.value.au.channels == 2 &&
                            r.value.duration_s > 0.9 && r.value.duration_s < 1.1;
            check("probe(WAV)", ok);
        } else {
            info(cv.last_message().data() ? to_cstr(cv.last_message()) : "?");
            check("probe(WAV)", false);
        }
    }


    /* =================================================================
     *  10) convert_audio — WAV -> AIFF
     * ================================================================= */
    section("10) convert_audio WAV -> AIFF",
            "Format change, same sample rate & channels",
            "output file exists, non-empty, converter reports ok");
    {
        knst_conv::converter cv;
        auto r = cv.convert_audio(in_wav, out_aiff);
        if (r) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "audio_frames=%llu seconds=%.3f bytes=%llu copied=%d",
                          (unsigned long long)r.value.audio_frames, r.value.seconds,
                          (unsigned long long)r.value.bytes_out, (int)r.value.audio_copied);
            info(buf);
        } else {
            info(cv.last_message().data() ? to_cstr(cv.last_message()) : "?");
        }
        check("WAV -> AIFF", r && file_exists_and_nonempty(out_aiff));
    }


    /* =================================================================
     *  11) convert_audio — WAV -> AU
     * ================================================================= */
    section("11) convert_audio WAV -> AU",
            "Same data, AU container",
            "output file exists, non-empty");
    {
        knst_conv::converter cv;
        auto r = cv.convert_audio(in_wav, out_au);
        check("WAV -> AU", r && file_exists_and_nonempty(out_au));
    }


    /* =================================================================
     *  12) convert_audio — WAV -> RAW
     * ================================================================= */
    section("12) convert_audio WAV -> RAW",
            "Raw PCM bytes (little-endian by default)",
            "output file exists; byte count = frames * channels * bytes_per_sample");
    {
        knst_conv::converter cv;
        auto r = cv.convert_audio(in_wav, out_raw);
        if (r) {
            const uint64_t sz = knst_file::get_file_size(out_raw);
            info(("raw bytes = " + std::to_string((unsigned long long)sz)).c_str());
        }
        check("WAV -> RAW", r && file_exists_and_nonempty(out_raw));
    }


    /* =================================================================
     *  13) convert_audio — WAV -> WAV (copy through the converter)
     * ================================================================= */
    section("13) convert_audio WAV -> WAV",
            "Round-trip through the converter, same settings",
            "output file exists, non-empty");
    {
        knst_conv::converter cv;
        auto r = cv.convert_audio(in_wav, out_wav);
        check("WAV -> WAV", r && file_exists_and_nonempty(out_wav));
    }


    /* =================================================================
     *  14) Resample (48 kHz -> 22.05 kHz)
     * ================================================================= */
    section("14) Resample",
            "audio_options::sample_rate = 22050",
            "output file exists; probe reports 22050 Hz");
    {
        knst_conv::converter cv;
        knst_conv::audio_options o;
        o.sample_rate = 22050;
        const knst_c16string dst = tmp_path("knst_conv_resampled.wav");
        knst_file::remove_file(dst);
        auto r = cv.convert_audio(in_wav, dst, o);
        auto p = cv.probe(dst);
        if (r && p) {
            info(("rate after resample = " + std::to_string(p.value.au.rate)).c_str());
        }
        check("resample 48k -> 22.05k", r && p && p.value.au.rate == 22050);
        knst_file::remove_file(dst);
    }


    /* =================================================================
     *  15) Mono downmix
     * ================================================================= */
    section("15) Mono downmix",
            "audio_options::channels = 1",
            "output file exists; probe reports 1 channel");
    {
        knst_conv::converter cv;
        knst_conv::audio_options o;
        o.channels = 1;
        const knst_c16string dst = tmp_path("knst_conv_mono.wav");
        knst_file::remove_file(dst);
        auto r = cv.convert_audio(in_wav, dst, o);
        auto p = cv.probe(dst);
        if (r && p) {
            info(("channels after downmix = " + std::to_string(p.value.au.channels)).c_str());
        }
        check("stereo -> mono", r && p && p.value.au.channels == 1);
        knst_file::remove_file(dst);
    }


    /* =================================================================
     *  16) Gain + normalize
     * ================================================================= */
    section("16) Gain + normalize",
            "gain_db = -6, normalize = true",
            "call succeeds; file is non-empty");
    {
        knst_conv::converter cv;
        knst_conv::audio_options o;
        o.gain_db   = -6.0f;
        o.normalize = true;
        const knst_c16string dst = tmp_path("knst_conv_gain.wav");
        knst_file::remove_file(dst);
        auto r = cv.convert_audio(in_wav, dst, o);
        check("gain + normalize", r && file_exists_and_nonempty(dst));
        knst_file::remove_file(dst);
    }


    /* =================================================================
     *  17) Trim (start_s / end_s)
     * ================================================================= */
    section("17) Trim",
            "start_s = 0.2, end_s = 0.6 -> ~0.4 s output",
            "call succeeds; output shorter than input");
    {
        knst_conv::converter cv;
        knst_conv::audio_options o;
        o.start_s = 0.2;
        o.end_s   = 0.6;
        const knst_c16string dst = tmp_path("knst_conv_trim.wav");
        knst_file::remove_file(dst);
        auto r = cv.convert_audio(in_wav, dst, o);
        const uint64_t sz_in  = knst_file::get_file_size(in_wav);
        const uint64_t sz_out = knst_file::get_file_size(dst);
        info(("in = " + std::to_string((unsigned long long)sz_in) +
              " bytes, out = " + std::to_string((unsigned long long)sz_out) + " bytes").c_str());
        check("trim 0.2..0.6 s", r && sz_out > 0 && sz_out < sz_in);
        knst_file::remove_file(dst);
    }


    /* =================================================================
     *  18) convert() dispatcher
     * ================================================================= */
    section("18) convert() auto-dispatcher",
            "convert(wav -> aiff) auto-detects the audio path",
            "returns report; file exists");
    {
        knst_conv::converter cv;
        const knst_c16string dst = tmp_path("knst_conv_dispatch.aiff");
        knst_file::remove_file(dst);
        auto r = cv.convert(in_wav, dst);
        check("convert dispatcher", r && file_exists_and_nonempty(dst));
        knst_file::remove_file(dst);
    }


    /* =================================================================
     *  19) convert_audio error paths
     * ================================================================= */
    section("19) convert_audio error paths",
            "missing input; unknown output extension; empty time range",
            "each call fails without crashing");
    {
        knst_conv::converter cv;

        // missing input
        auto r1 = cv.convert_audio(missing, out_wav);
        check("missing input fails", !r1);

        // unknown output extension
        auto r2 = cv.convert_audio(in_wav, tmp_path("bad.zzz"));
        check("unknown output ext fails", !r2);

        // end <= start
        knst_conv::audio_options o;
        o.start_s = 0.8;
        o.end_s   = 0.2;
        const knst_c16string dst = tmp_path("knst_conv_badrange.wav");
        knst_file::remove_file(dst);
        auto r3 = cv.convert_audio(in_wav, dst, o);
        check("start_s > end_s fails", !r3 && !knst_file::file_exists(dst));
    }


    /* =================================================================
     *  20) convert_audio — unsupported encoder target
     * ================================================================= */
    section("20) convert_audio -> MP3 without encoder",
            "No MP3 encoder is registered by default",
            "fails; last_message() mentions 'encoder' or 'no encoder'");
    {
        knst_conv::converter cv;
        const knst_c16string dst = tmp_path("knst_conv_no_mp3.mp3");
        knst_file::remove_file(dst);
        auto r = cv.convert_audio(in_wav, dst);
        const std::string msg(cv.last_message().data() ? to_cstr(cv.last_message()) : "");
        info(("last_message = \"" + msg + "\"").c_str());
        const bool mentions_encoder = msg.find("encoder") != std::string::npos ||
                                      msg.find("Encoder") != std::string::npos;
        check("mp3 without encoder fails cleanly", !r && mentions_encoder);
        knst_file::remove_file(dst);
    }


    /* =================================================================
     *  21) register_audio_encoder + use it
     * ================================================================= */
    section("21) register_audio_encoder",
            "Register a dummy encoder for MP3, then convert WAV -> MP3",
            "registration succeeds; conversion calls the encoder; output exists");
    {
        // Why MP3 and not OGG: encode_out_() prefers the native encoder
        // when the format is on its `try_native` list (wav/aiff/au/raw/
        // flac/ogg). If KNST_NETWORK_HAVE_VORBIS was compiled in, the
        // OGG conversion succeeds natively and the registry is never
        // consulted — the test would silently "pass" without exercising
        // the path it is trying to demonstrate. MP3 has no native
        // encoder, so the registry is guaranteed to be hit.
        const bool reg = knst_conv::converter::register_audio_encoder(
            knst_conv::format_t::mp3, dummy_mp3_encoder);
        check("register mp3 encoder", reg);

        knst_conv::converter cv;
        auto r = cv.convert_audio(in_wav, out_mp3);
        info(("encoder was called " + std::to_string(s_enc_calls) + " time(s)").c_str());
        check("WAV -> MP3 via registered encoder",
              r && s_enc_calls > 0 && file_exists_and_nonempty(out_mp3));
    }

    /* =================================================================
     *  22) progress callback
     * ================================================================= */
    section("22) progress callback",
            "set_progress() is accepted; converter keeps it (audio-only\n"
            "conversions do not emit audio progress in this version)",
            "no crash; callback API works");
    {
        s_prog_calls = 0;
        knst_conv::converter cv;
        cv.set_progress(&progress_cb);
        const knst_c16string dst = tmp_path("knst_conv_progress.wav");
        knst_file::remove_file(dst);
        knst_conv::audio_options o;
        o.sample_rate = 16000;
        auto r = cv.convert_audio(in_wav, dst, o);
        info(("progress callbacks = " + std::to_string(s_prog_calls)).c_str());
        // Audio-only convert does not fire callbacks in this build; the API must not crash.
        check("set_progress accepted", r && !cv.was_cancelled());
        knst_file::remove_file(dst);
    }


    /* =================================================================
     *  23) extract_frame / extract_frames — non-video input
     * ================================================================= */
    section("23) extract_frame on audio",
            "extract_frame() requires an AV container",
            "fails cleanly");
    {
        knst_conv::converter cv;
        const knst_c16string dst = tmp_path("knst_conv_frame.jpg");
        knst_file::remove_file(dst);
        auto r = cv.extract_frame(in_wav, dst, 0.0);
        check("extract_frame on WAV fails", !r && !knst_file::file_exists(dst));

        auto r2 = cv.extract_frames(in_wav, knst_file::get_temp_dir(), 1.0);
        check("extract_frames on WAV fails", !r2);
    }


    /* =================================================================
     *  24) strip_audio on audio
     * ================================================================= */
    section("24) strip_audio on audio",
            "strip_audio() requires a video container",
            "fails cleanly");
    {
        knst_conv::converter cv;
        const knst_c16string dst = tmp_path("knst_conv_mute.mkv");
        knst_file::remove_file(dst);
        auto r = cv.strip_audio(in_wav, dst);
        check("strip_audio on WAV fails", !r);
    }


    /* =================================================================
     *  25) add_audio — audio + audio as 'external video'
     * ================================================================= */
    section("25) add_audio on audio inputs",
            "add_audio() needs a video; audio+audio fails",
            "fails cleanly; output not created");
    {
        knst_conv::converter cv;
        const knst_c16string dst = tmp_path("knst_conv_mux.mkv");
        knst_file::remove_file(dst);
        auto r = cv.add_audio(in_wav, in_wav, dst);
        check("add_audio(audio, audio) fails", !r && !knst_file::file_exists(dst));
    }


    /* =================================================================
     *  26) report fields
     * ================================================================= */
    section("26) report fields",
            "After a successful WAV -> AIFF the report has non-zero counts",
            "ok=true, audio_frames > 0, bytes_out > 0, seconds > 0");
    {
        knst_conv::converter cv;
        const knst_c16string dst = tmp_path("knst_conv_report.aiff");
        knst_file::remove_file(dst);
        auto r = cv.convert_audio(in_wav, dst);
        if (r) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "ok=%d frames=%llu bytes=%llu s=%.3f copied=%d",
                          (int)r.value.ok,
                          (unsigned long long)r.value.audio_frames,
                          (unsigned long long)r.value.bytes_out,
                          r.value.seconds, (int)r.value.audio_copied);
            info(buf);
            check("report fields",
                  r.value.ok && r.value.audio_frames > 0 &&
                  r.value.bytes_out > 0 && r.value.seconds > 0.0);
        } else {
            info(cv.last_message().data() ? to_cstr(cv.last_message()) : "?");
            check("report fields", false);
        }
        knst_file::remove_file(dst);
    }


    /* =================================================================
     *  27) JSON bridge (optional)
     * ================================================================= */
#if defined(KNST_JSON_HPP)
    section("27) report_to_json",
            "Convert a report to a knst_json object",
            "object with ok / format / audio_frames / bytes_out fields");
    {
        knst_conv::report rep;
        rep.ok            = true;
        rep.out_format    = knst_conv::format_t::wav;
        rep.video_copied  = false;
        rep.audio_copied  = false;
        rep.video_frames  = 0;
        rep.audio_frames  = 48000;
        rep.bytes_out     = 192000;
        rep.seconds       = 1.0;
        rep.notes.push_back(u"test note 1");
        rep.notes.push_back(u"test note 2");

        knst_json j = knst_conv::report_to_json(rep);
        info(("json object ok? " + std::string(j.is_object() ? "yes" : "no")).c_str());
        const bool ok = j.is_object() &&
                        j.find("ok") != nullptr &&
                        j.find("format") != nullptr &&
                        j.find("audio_frames") != nullptr &&
                        j.find("notes") != nullptr;
        check("report_to_json", ok);
    }
#else
    section("27) report_to_json", "KNST_JSON_HPP not defined", "skipped");
    info("(JSON bridge not compiled in — skipping)");
#endif


    /* =================================================================
     *  28) Cleanup
     * ================================================================= */
    section("28) Cleanup",
            "Remove all temp files",
            "no residue in %TEMP%");
    {
        knst_file::remove_file(in_wav);
        knst_file::remove_file(out_wav);
        knst_file::remove_file(out_aiff);
        knst_file::remove_file(out_au);
        knst_file::remove_file(out_raw);
        knst_file::remove_file(out_ogg);
        knst_file::remove_file(out_mp3);
        check("cleanup", !knst_file::file_exists(in_wav) && !knst_file::file_exists(out_aiff));
    }


    /* =================================================================
     *  Summary
     * ================================================================= */
    std::cout << "\n==================================================\n";
    std::cout << "  RESULT: " << s_pass << " PASS / " << s_fail << " FAIL\n";
    std::cout << "==================================================\n";
    if (s_fail > 0) std::cout << "\nSome checks failed.\n";
    std::cout << "\nDone.\n";
    return s_fail == 0 ? 0 : 1;
}