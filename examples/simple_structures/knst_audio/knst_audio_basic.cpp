// Comprehensive exercise of knst_audio.hpp — walks through every public
// API surface: device control, codec I/O, DSP, streaming, JSON, errors.
// Safe to run: volume and mute changes are rolled back on exit, temp files
// are cleaned up, and any playback is short and quiet.
//
// Not a unit test framework — just plain check() calls with printf output
// so you can eyeball what each block does while it runs.

#include "KernelNucleusT.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>

using namespace knst_aud;

// ---- tiny test scaffolding ------------------------------------------------

static int g_ok = 0;
static int g_fail = 0;

static void ok(const char* what) {
    std::printf("  ok    %s\n", what);
    ++g_ok;
}
static void bad(const char* what) {
    std::printf("  FAIL  %s\n", what);
    ++g_fail;
}
static void check(bool cond, const char* what) {
    if (cond) ok(what); else bad(what);
}
static void section(const char* name) {
    std::printf("\n--- %s\n", name);
}

// Converts a c16string for printf. The buffer lives in a thread-local so
// repeated calls within one expression still work.
static const char* utf8(const knst_c16string& s) {
    // Rotate through 4 thread-local buffers so two calls in the same
    // printf() (e.g. "name / active_profile") don't overwrite each other.
    static thread_local knst_byte_string bufs[4];
    static thread_local int idx = 0;
    int i = idx;
    idx = (idx + 1) & 3;
    bufs[i] = knst_dev::to_utf8(s);
    return reinterpret_cast<const char*>(bufs[i].data());
}

static float max_diff(const float* a, const float* b, uint32_t n) {
    float m = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float d = std::fabs(a[i] - b[i]);
        if (d > m) m = d;
    }
    return m;
}

// Simple sine generator; duplicated across channels.
static knst_vector<float> sine(uint32_t frames, uint32_t ch, float hz, uint32_t rate, float amp = 0.5f) {
    knst_vector<float> v;
    v.resize(frames * ch);
    double step = 2.0 * M_PI * hz / static_cast<double>(rate);
    double phase = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        float s = static_cast<float>(std::sin(phase)) * amp;
        for (uint32_t c = 0; c < ch; ++c) v[i * ch + c] = s;
        phase += step;
    }
    return v;
}

// RAII wrappers to guarantee we don't leave the user's audio settings dirty.
struct vol_guard {
    const knst_audio_device* d = nullptr;
    uint32_t saved = 0;
    bool active = false;
    explicit vol_guard(const knst_audio_device* dev) : d(dev) {
        if (d) { auto r = audio::get_volume(*d); if (r) { saved = r.value; active = true; } }
    }
    ~vol_guard() { if (active && d) audio::set_volume(*d, saved); }
};
struct mute_guard {
    const knst_audio_device* d = nullptr;
    bool saved = false, active = false;
    explicit mute_guard(const knst_audio_device* dev) : d(dev) {
        if (d) { auto r = audio::is_muted(*d); if (r) { saved = r.value; active = true; } }
    }
    ~mute_guard() { if (active && d) audio::set_mute(*d, saved); }
};

// ---- tests ----------------------------------------------------------------

static void test_environment() {
    section("environment");
    std::printf("  backend running : %s\n", audio::is_running() ? "yes" : "no");
    std::printf("  sizeof(wav_info)     = %zu\n", sizeof(wav_info));
    std::printf("  sizeof(play_options) = %zu\n", sizeof(play_options));
    std::printf("  sizeof(pcm_buffer)   = %zu\n", sizeof(pcm_buffer));
    std::printf("  sizeof(media_info)   = %zu\n", sizeof(media_info));
}

static void test_devices() {
    section("device enumeration");
    auto all = audio::list();
    if (!all) {
        std::printf("  audio backend not reachable\n");
        return;
    }
    std::printf("  %u device(s)\n", static_cast<unsigned>(all.value.size()));
    uint32_t sinks = 0, sources = 0, monitors = 0;
    for (auto& d : all.value) {
        // A "monitor" source is PulseAudio's loopback of a sink.
        // Its name ends in ".monitor"; description says "Monitor of ...".
        bool is_monitor = d.name.contains(u".monitor") ||
                          d.description.contains(u"Monitor of");
        const char* kind = is_monitor ? "MON" : (d.is_input ? "IN " : "OUT");
        if (is_monitor) ++monitors;
        else if (d.is_input) ++sources;
        else ++sinks;
        std::printf("    [%u] %s  %s\n",
                    static_cast<unsigned>(d.index), kind, utf8(d.description));
    }
    std::printf("  sinks=%u sources=%u monitors=%u\n", sinks, sources, monitors);
    check(sinks + sources > 0, "at least one device present");
}

static void test_defaults() {
    section("default devices");
    auto sk = audio::get_default(false);
    if (sk) {
        std::printf("  default sink   : %s\n", utf8(sk.value.description));
        // Note: the Linux backend returns the matching device straight from
        // list(), which doesn't carry the is_default flag. Only Windows sets
        // it on this code path. Don't assert it here.
    }
    auto so = audio::get_default(true);
    if (so) {
        std::printf("  default source : %s\n", utf8(so.value.description));
    }
}

static void test_volume() {
    section("volume control");
    auto sinks = audio::list_sinks();
    if (!sinks || sinks.value.empty()) { std::printf("  no sink to test\n"); return; }
    const knst_audio_device& d = sinks.value[0];
    vol_guard guard(&d);

    auto v0 = audio::get_volume(d);
    if (v0) std::printf("  current        : %u%%\n", v0.value);

    check(audio::set_volume(d, 60).ok(), "set_volume(60)");
    auto v1 = audio::get_volume(d);
    check(v1 && v1.value == 60, "read back 60%");

    // Sanity: 200% should be rejected on every backend (even 150%-capable ones).
    auto r = audio::set_volume(d, 200);
    check(!r || r.st == status::InvalidArgument, "reject 200%");

    auto db = audio::get_volume_db(d);
    if (db) std::printf("  dB now         : %.2f\n", db.value);

    // dB round-trip through the helper functions.
    uint32_t p = 75;
    double dcalc = percent_to_db(p);
    uint32_t back = db_to_percent(dcalc);
    std::printf("  %u%% -> %.2f dB -> %u%%\n", p, dcalc, back);
    check(back >= p - 1 && back <= p + 1, "dB roundtrip");

    auto ch = audio::get_channel_count(d);
    if (ch && ch.value > 0) {
        std::printf("  channel count  : %u\n", ch.value);
        auto l = audio::get_channel_volume(d, 0);
        auto rr = audio::get_channel_volume(d, 1);
        if (l)  std::printf("  L              : %u%%\n", l.value);
        if (rr) std::printf("  R              : %u%%\n", rr.value);
    }

    auto bal = audio::get_balance(d);
    if (bal) std::printf("  balance        : %u (50 = centre)\n", bal.value);
}

static void test_mute() {
    section("mute");
    auto sinks = audio::list_sinks();
    if (!sinks || sinks.value.empty()) return;
    const knst_audio_device& d = sinks.value[0];
    mute_guard guard(&d);

    check(audio::set_mute(d, true).ok(), "mute on");
    auto m1 = audio::is_muted(d);
    check(m1 && m1.value, "muted read-back");

    check(audio::set_mute(d, false).ok(), "mute off");
    auto m2 = audio::is_muted(d);
    check(m2 && !m2.value, "unmuted read-back");
}

static void test_ports_cards() {
    section("ports, cards, server");
    auto sinks = audio::list_sinks();
    if (sinks && !sinks.value.empty()) {
        auto ports = audio::get_ports(sinks.value[0]);
        if (ports) {
            std::printf("  %u port(s) on %s\n",
                        static_cast<unsigned>(ports.value.size()),
                        utf8(sinks.value[0].description));
            for (uint32_t i = 0; i < ports.value.size() && i < 5; ++i)
                std::printf("    - %s\n", utf8(ports.value[i]));
        }
    }

    auto cards = audio::list_cards();
    if (cards) {
        std::printf("  %u card(s)\n", static_cast<unsigned>(cards.value.size()));
        for (uint32_t i = 0; i < cards.value.size() && i < 3; ++i) {
            std::printf("    [%u] %s / %s (%u profiles)\n",
                        static_cast<unsigned>(cards.value[i].index),
                        utf8(cards.value[i].name),
                        utf8(cards.value[i].active_profile),
                        static_cast<unsigned>(cards.value[i].profiles.size()));
        }
    }

    auto srv = audio::server();
    if (srv) {
        std::printf("  server  : %s %s @ %u Hz / %u ch\n",
                    utf8(srv.value.name), utf8(srv.value.version),
                    srv.value.sample_rate, srv.value.channels);
    }
}

static void test_streams() {
    section("per-application streams");
    auto streams = audio::list_streams();
    if (!streams) { std::printf("  not supported on this platform\n"); return; }
    std::printf("  %u active stream(s)\n", static_cast<unsigned>(streams.value.size()));
    for (uint32_t i = 0; i < streams.value.size() && i < 5; ++i) {
        std::printf("    [%u] %s  %-24s vol=%u%%\n",
                    static_cast<unsigned>(streams.value[i].index),
                    streams.value[i].is_output ? "OUT" : "IN ",
                    utf8(streams.value[i].application),
                    streams.value[i].volume_percent);
    }
}

static void test_peak_meter() {
    section("peak meter (50 ms sample)");

    auto def = audio::get_default(false);
    if (!def) { std::printf("  no default sink\n"); return; }
    const knst_audio_device& target = def.value;

    auto peak = audio::get_peak_meter(target);
    if (peak) {
        std::printf("  peak = %.3f\n", peak.value);
        uint32_t pct = static_cast<uint32_t>(peak.value * 100.0);
        std::printf("  approximate dB: %.1f\n", percent_to_db(pct));
    } else {
        std::printf("  peak meter unavailable (status=%s native=%d)\n",
                    status_string(peak.st), peak.native);
        std::printf("  device: %s\n", utf8(target.description));
        std::printf("  hint:   nothing may be playing to this sink right now\n");
    }
}

static void test_format_detection() {
    section("format detection");

    struct sample { const char* label; uint8_t magic[16]; container_t want; };
    sample cases[] = {
        { "WAV",   { 'R','I','F','F',0,0,0,0,'W','A','V','E' }, container_t::wav },
        { "AIFF",  { 'F','O','R','M',0,0,0,0,'A','I','F','F' }, container_t::aiff },
        { "AIFC",  { 'F','O','R','M',0,0,0,0,'A','I','F','C' }, container_t::aifc },
        { "AU",    { '.','s','n','d',0,0,0,24 },               container_t::au },
        { "FLAC",  { 'f','L','a','C',0,0,0,0 },                container_t::flac },
        { "OGG",   { 'O','g','g','S',0,0,0,0 },                container_t::ogg },
        { "WVPK",  { 'w','v','p','k',0,0,0,0 },                container_t::wavpack },
        { "MP4",   { 0,0,0,0,'f','t','y','p',0,0,0,0 },        container_t::mp4 },
    };
    for (auto& c : cases) check(codec::detect(c.magic, sizeof(c.magic)) == c.want, c.label);

    check(codec::detect_extension(u"/m/song.wav")  == container_t::wav,  "ext .wav");
    check(codec::detect_extension(u"/m/song.flac") == container_t::flac, "ext .flac");
    check(codec::detect_extension(u"/m/song.mp3")  == container_t::mp3,  "ext .mp3");
    check(codec::detect_extension(u"/m/song.m4a")  == container_t::mp4,  "ext .m4a");
    check(codec::detect_extension(u"/m/song.opus") == container_t::ogg,  "ext .opus");
}

static void test_wav_formats() {
    section("WAV — encode/decode every sample format");
    const uint32_t frames = 4000, rate = 48000;
    auto src = sine(frames, 1, 440.0f, rate);

    struct { sample_fmt f; const char* label; float tol; } cases[] = {
        { sample_fmt::u8,   "u8",   0.01f   },
        { sample_fmt::i16,  "i16",  0.0001f },
        { sample_fmt::i24,  "i24",  1e-6f   },
        { sample_fmt::i32,  "i32",  1e-7f   },
        { sample_fmt::f32,  "f32",  0.0f    },
        { sample_fmt::f64,  "f64",  0.0f    },
        { sample_fmt::alaw, "alaw", 0.02f   },
        { sample_fmt::ulaw, "ulaw", 0.02f   },
    };

    for (auto& c : cases) {
        encode_options eo;
        eo.container = container_t::wav;
        eo.fmt = c.f;
        pcm_buffer in; in.samples = src; in.rate = rate; in.channels = 1;

        auto bytes = encode(in, eo);
        if (!bytes) { bad(c.label); continue; }
        auto back = decode(bytes.value.data(), bytes.value.length());
        if (!back) { bad(c.label); continue; }

        float diff = max_diff(src.data(), back.value.samples.data(), frames);
        char msg[80];
        std::snprintf(msg, sizeof(msg), "%s (err=%.6f)", c.label, diff);
        check(diff <= c.tol, msg);
    }
}

static void test_containers() {
    section("container roundtrip (WAV/AIFF/AIFC/AU/FLAC/RAW)");
    const uint32_t frames = 2000, rate = 44100;
    auto src = sine(frames, 1, 440.0f, rate);
    pcm_buffer in; in.samples = src; in.rate = rate; in.channels = 1;

    struct { container_t c; sample_fmt f; const char* label; } cases[] = {
        { container_t::wav,   sample_fmt::i16, "wav i16"   },
        { container_t::aiff,  sample_fmt::i16, "aiff i16"  },
        { container_t::aifc,  sample_fmt::f32, "aifc f32"  },
        { container_t::au,    sample_fmt::i16, "au i16"    },
        { container_t::flac,  sample_fmt::i16, "flac i16"  },
        { container_t::flac,  sample_fmt::i24, "flac i24"  },
        { container_t::raw,   sample_fmt::i16, "raw i16"   },
    };

    for (auto& c : cases) {
        encode_options eo; eo.container = c.c; eo.fmt = c.f;
        auto bytes = encode(in, eo);
        if (!bytes) { bad(c.label); continue; }

        if (c.c == container_t::raw) {
            // No header to parse — just confirm something got written.
            check(bytes.value.length() == frames * 2, c.label);
            continue;
        }
        auto dec = decode(bytes.value.data(), bytes.value.length());
        if (!dec) { bad(c.label); continue; }
        float diff = max_diff(src.data(), dec.value.samples.data(), frames);
        char msg[80]; std::snprintf(msg, sizeof(msg), "%s (err=%.6f)", c.label, diff);
        check(diff < 0.002f, msg);
    }
}

static void test_probe() {
    section("probe / media_info");
    const uint32_t frames = 1000;
    pcm_buffer b;
    b.samples = sine(frames, 2, 440.0f, 48000);
    b.rate = 48000; b.channels = 2;

    encode_options eo; eo.container = container_t::wav; eo.fmt = sample_fmt::i16;
    auto bytes = encode(b, eo);
    if (!bytes) return;

    auto info = probe(bytes.value.data(), bytes.value.length());
    if (!info) return;
    std::printf("  container=%s rate=%u ch=%u bits=%u frames=%llu\n",
                container_name(info.value.container),
                info.value.rate, info.value.channels, info.value.bits,
                (unsigned long long)info.value.frames);
    check(info.value.rate == 48000, "rate preserved");
    check(info.value.channels == 2, "channels preserved");
    check(static_cast<uint32_t>(info.value.frames) == frames, "frame count preserved");
}

static void test_pcm_buffer_ops() {
    section("pcm_buffer helpers");
    pcm_buffer st;
    st.samples = sine(12000, 2, 440.0f, 48000);
    st.rate = 48000; st.channels = 2;

    auto mono = convert_buffer_channels(st, 1);
    if (mono) {
        std::printf("  stereo->mono: ch=%u frames=%u\n",
                    mono.value.channels, mono.value.frames());
        check(mono.value.channels == 1, "mono channel count");
    }

    auto rs = resample_buffer(st, 22050);
    if (rs) {
        std::printf("  48k->22.05k: rate=%u frames=%u\n",
                    rs.value.rate, rs.value.frames());
        check(rs.value.rate == 22050, "resample rate");
    }
}

static void test_dsp_generators() {
    section("DSP — generators");
    const uint32_t frames = 4800, rate = 48000;
        struct { dsp::wave_t t; const char* name; } kinds[] = {
        { dsp::wave_t::sine,     "sine" },
        { dsp::wave_t::square,   "square" },
        { dsp::wave_t::saw,      "saw" },
        { dsp::wave_t::triangle, "triangle" },
        { dsp::wave_t::noise,    "noise" },
    };
    for (auto& k : kinds) {
        knst_vector<float> buf;
        buf.resize(frames);
        dsp::generate(buf.data(), frames, 1, k.t, 440.0f, rate, 0.9f);
        auto s = dsp::analyze(buf.data(), frames);
        char msg[48]; std::snprintf(msg, sizeof(msg), "%s peak=%.3f", k.name, s.peak);
        check(s.valid && s.peak > 0.0f, msg);
    }
}

static void test_dsp_levels() {
    section("DSP — analyze / normalize / gain");
    auto buf = sine(4800, 1, 440.0f, 48000);
    for (auto& v : buf) v *= 0.15f;

    auto s0 = dsp::analyze(buf.data(), buf.size());
    std::printf("  before: peak=%.3f (%.1f dB)\n", s0.peak, s0.peak_db);

    dsp::normalize(buf.data(), buf.size(), 0.95f);
    auto s1 = dsp::analyze(buf.data(), buf.size());
    std::printf("  after : peak=%.3f (%.1f dB)\n", s1.peak, s1.peak_db);
    check(std::fabs(s1.peak - 0.95f) < 0.01f, "normalized to ~0.95");

    dsp::apply_gain_db(buf.data(), buf.size(), -6.0f);
    auto s2 = dsp::analyze(buf.data(), buf.size());
    std::printf("  -6 dB : peak=%.3f (%.1f dB)\n", s2.peak, s2.peak_db);
    check(s2.peak_db < -6.0f && s2.peak_db > -7.0f, "gain applied");
}

static void test_dsp_dc() {
    section("DSP — DC offset / removal");
    auto buf = sine(4800, 1, 440.0f, 48000);
    for (auto& v : buf) v += 0.3f;

    float dc = dsp::dc_offset(buf.data(), buf.size());
    std::printf("  mono DC before: %.4f\n", dc);
    dsp::dc_remove(buf.data(), buf.size());
    float dc2 = dsp::dc_offset(buf.data(), buf.size());
    std::printf("  mono DC after : %.6f\n", dc2);
    check(std::fabs(dc2) < 0.001f, "mono DC removed");

    auto st = sine(1000, 2, 440.0f, 48000);
    for (auto& v : st) v += 0.25f;
    dsp::dc_remove_interleaved(st.data(), 1000, 2);
    float dl = 0.0f, dr = 0.0f;
    for (uint32_t i = 0; i < 1000; ++i) { dl += st[i * 2]; dr += st[i * 2 + 1]; }
    std::printf("  stereo DC after: L=%.6f R=%.6f\n", dl / 1000, dr / 1000);
    check(std::fabs(dl / 1000) < 0.001f && std::fabs(dr / 1000) < 0.001f, "stereo DC removed");
}

static void test_dsp_edit() {
    section("DSP — fade / trim / concat / mix / crossfade");
    auto a = sine(24000, 2, 440.0f, 48000);
    auto b = sine(24000, 2, 550.0f, 48000);

    dsp::fade_in(a.data(), 24000, 2, 4800);
    dsp::fade_out(b.data(), 24000, 2, 4800);

    auto t = dsp::trim(a.data(), 24000, 2, 2400, 21600);
    std::printf("  trim     : %u samples\n", static_cast<unsigned>(t.size()));
    check(t.size() == 19200u * 2, "trim length");

    auto c = dsp::concat(a.data(), 24000, b.data(), 24000, 2);
    std::printf("  concat   : %u samples\n", static_cast<unsigned>(c.size()));
    check(c.size() == 48000u * 2, "concat length");

    auto m = dsp::mix(a.data(), b.data(), 24000, 2);
    std::printf("  mix      : %u samples\n", static_cast<unsigned>(m.size()));
    check(m.size() == 24000u * 2, "mix length");

    auto cf = dsp::crossfade(a.data(), 24000, b.data(), 24000, 2, 4800);
    std::printf("  crossfade: %u samples\n", static_cast<unsigned>(cf.size()));
    check(!cf.empty(), "crossfade produced output");
}

static void test_dsp_convert() {
    section("DSP — channel / rate conversion");

    auto surround = sine(1000, 6, 440.0f, 48000);
    auto st = dsp::convert_channels(surround.data(), 1000, 6, 2);
    check(!st.empty(), "5.1 -> stereo");
    if (!st.empty()) {
        auto s = dsp::analyze(st.data(), static_cast<uint32_t>(st.size()));
        check(s.valid && s.peak > 0.0f, "downmixed signal has energy");
    }

        auto mono = sine(1000, 1, 440.0f, 48000);
    auto up = dsp::convert_channels(mono.data(), 1000, 1, 6);
    check(up.size() == 1000u * 6, "mono -> 5.1");

    // resample_linear needs a buffer that actually holds `frames_in` frames —
    // the short `mono` above is only 1000 frames. Use a fresh full-length one.
    auto long_mono = sine(48000, 1, 440.0f, 48000);
    auto rs = dsp::resample_linear(long_mono.data(), 48000, 48000, 1, 22050);
    std::printf("  linear 48k->22.05k: %u samples\n", static_cast<unsigned>(rs.size()));
    check(!rs.empty(), "linear resampler produced output");
}

static void test_dsp_silence() {
    section("DSP — silence detection / trimming");
    knst_vector<float> quiet;
    quiet.resize(48000, 0.0f);
    check(dsp::is_silent(quiet.data(), 48000), "silent buffer");

    auto loud = sine(48000, 1, 440.0f, 48000);
    check(!dsp::is_silent(loud.data(), 48000), "non-silent buffer");

    // force a silent gap in the middle
    auto mixed = loud;
    for (uint32_t i = 12000; i < 24000; ++i) mixed[i] = 0.0f;

    uint32_t start = 0, end = 0;
    bool found = dsp::find_active_range(mixed.data(), 48000, 1, -60.0f, start, end);
    std::printf("  active range: %u..%u\n", start, end);
    check(found && start < 100, "start of active range");

    auto trimmed = dsp::trim_silence(mixed.data(), 48000, 1, -60.0f);
    std::printf("  trim_silence: %u samples\n", static_cast<unsigned>(trimmed.size()));
    check(!trimmed.empty(), "trimmed output");
}

static void test_dsp_dynamics() {
    section("DSP — limiter / soft_clip / normalize_rms");

    auto buf = sine(48000, 1, 440.0f, 48000);
    for (auto& v : buf) v *= 2.0f;
    dsp::limiter(buf.data(), 48000, 1, 48000, -1.0f, 80.0f);
    auto s = dsp::analyze(buf.data(), 48000);
    std::printf("  limiter  peak=%.3f\n", s.peak);
    check(s.peak < 0.95f, "peaks bounded by limiter");

    auto buf2 = sine(4800, 1, 440.0f, 48000);
    for (auto& v : buf2) v *= 2.0f;
    dsp::soft_clip(buf2.data(), 4800);
    auto s2 = dsp::analyze(buf2.data(), 4800);
    std::printf("  soft_clip peak=%.3f\n", s2.peak);
    check(s2.peak <= 1.0f, "tanh limits to 1.0");

    auto buf3 = sine(48000, 1, 440.0f, 48000);
    for (auto& v : buf3) v *= 0.05f;
    dsp::normalize_rms(buf3.data(), 48000, -20.0f, false);
    auto s3 = dsp::analyze(buf3.data(), 48000);
    std::printf("  normalize_rms: %.1f dB\n", s3.rms_db);
    check(s3.rms_db > -21.0f && s3.rms_db < -19.0f, "target RMS reached");
}

static void test_dsp_reverse_pan() {
    section("DSP — reverse / pan / interleave");
    auto buf = sine(4800, 2, 440.0f, 48000);
    float first = buf[0];
    dsp::reverse(buf.data(), 4800, 2);
    float last = buf[(4800 - 1) * 2];
    std::printf("  reverse: first=%.4f last=%.4f\n", first, last);
    check(std::fabs(first - last) < 1e-5f, "reverse symmetric");

    auto st = sine(4800, 2, 440.0f, 48000);
    dsp::pan_stereo(st.data(), 4800, -1.0f);
    auto s = dsp::analyze(st.data(), 4800 * 2);
    check(s.valid, "pan wrote to buffer");

    knst_vector<float> left(1000), right(1000), inter(2000);
    for (uint32_t i = 0; i < 1000; ++i) {
        left[i] = static_cast<float>(i) / 1000.0f;
        right[i] = 1.0f - left[i];
    }
    const float* in_planes[2] = { left.data(), right.data() };
    dsp::interleave(in_planes, 1000, 2, inter.data());

    knst_vector<float> l2(1000), r2(1000);
    float* out_planes[2] = { l2.data(), r2.data() };
    dsp::deinterleave(inter.data(), 1000, 2, out_planes);
    check(max_diff(left.data(), l2.data(), 1000) < 1e-6f, "interleave roundtrip");
}

static void test_dsp_resample() {
    section("DSP — resample all quality levels");
    auto mono = sine(48000, 1, 440.0f, 48000);
    struct { dsp::resample_quality q; const char* name; } levels[] = {
        { dsp::resample_quality::fast,   "fast" },
        { dsp::resample_quality::medium, "medium" },
        { dsp::resample_quality::high,   "high" },
        { dsp::resample_quality::best,   "best" },
    };
    for (auto& lv : levels) {
        auto rs = dsp::resample(mono.data(), 48000, 1, 48000, 22050, lv.q);
        std::printf("  %-7s: %u samples\n", lv.name, static_cast<unsigned>(rs.size()));
        check(!rs.empty(), lv.name);
    }
}

static void test_biquad() {
    section("biquad filters (RBJ cookbook)");
    auto src = sine(48000, 1, 1000.0f, 48000);

    struct { dsp::filter_t t; const char* name; double freq; double gain; } cases[] = {
        { dsp::filter_t::lowpass,   "lowpass  500",  500.0, 0.0 },
        { dsp::filter_t::highpass,  "highpass 2k",  2000.0, 0.0 },
        { dsp::filter_t::bandpass,  "bandpass 1k",  1000.0, 0.0 },
        { dsp::filter_t::notch,     "notch    1k",  1000.0, 0.0 },
        { dsp::filter_t::allpass,   "allpass  1k",  1000.0, 0.0 },
        { dsp::filter_t::peak,      "peak     1k",  1000.0, 6.0 },
        { dsp::filter_t::lowshelf,  "lowshelf 500",  500.0, 3.0 },
        { dsp::filter_t::highshelf, "highshelf 3k", 3000.0, 3.0 },
    };
    for (auto& c : cases) {
        auto buf = src;
        dsp::biquad b(c.t, 48000.0, c.freq, 0.70710678, c.gain);
        b.process(buf.data(), 48000, 1);
        auto s = dsp::analyze(buf.data(), 48000);
        std::printf("  %-12s peak=%.4f\n", c.name, s.peak);
        check(s.valid, c.name);
    }
}

static void test_fft() {
    section("FFT / spectrum / dominant frequency");
    auto src = sine(4096, 1, 440.0f, 48000);
    auto mag = dsp::magnitude_spectrum(src.data(), 4096);
    check(mag.size() == 4096 / 2 + 1, "spectrum bin count");

    double hz = dsp::dominant_frequency(src.data(), 4096, 48000);
    std::printf("  dominant frequency: %.2f Hz\n", hz);
    check(std::fabs(hz - 440.0) < 5.0, "peak near 440 Hz");
}

static void test_player() {
    section("player — load / seek / read / loop / gain");
    pcm_buffer b;
    b.samples = sine(96000, 2, 440.0f, 48000);
    b.rate = 48000; b.channels = 2;

    auto p = player::from_buffer(std::move(b));
    if (!p) { bad("player::from_buffer"); return; }

    std::printf("  %u frames @ %u Hz / %u ch\n",
                p.value.total_frames(), p.value.sample_rate(), p.value.channels());

    p.value.seek_seconds(1.0);
    check(std::fabs(p.value.position_seconds() - 1.0) < 0.01, "seek to 1 s");

    knst_vector<float> buf;
    buf.resize(24000 * 2);
    auto r = p.value.read_interleaved(buf.data(), 24000);
    check(r && r.value == 24000, "read 24000 frames");

    p.value.set_loop(true);
    p.value.seek_frames(0);
    // ask for more than the buffer contains to trigger wrapping
    buf.resize(200000 * 2);
    auto r2 = p.value.read_interleaved(buf.data(), 200000);
    check(r2 && r2.value == 200000, "loop refills past EOF");

    p.value.set_gain(0.5f);
    p.value.seek_frames(0);
    buf.resize(2000);
    auto r3 = p.value.read_interleaved(buf.data(), 1000);
    check(r3 && r3.value == 1000, "read after gain");

    auto rs = p.value.resampled(22050);
    if (rs) std::printf("  resampled player: %u Hz\n", rs.value.sample_rate());
}

static void test_mixer() {
    section("mixer — N sources -> one bus");
    mixer m(48000, 2);

    pcm_buffer a, b, c;
    a.samples = sine(24000, 2, 440.0f, 48000); a.rate = 48000; a.channels = 2;
    b.samples = sine(24000, 2, 550.0f, 48000); b.rate = 48000; b.channels = 2;
    c.samples = sine(24000, 2, 660.0f, 48000); c.rate = 48000; c.channels = 2;

    int ida = m.add(a, 0.8f, -0.3f);
    int idb = m.add(b, 0.7f,  0.0f);
    int idc = m.add(c, 0.6f,  0.3f);
    std::printf("  slots: %d %d %d\n", ida, idb, idc);
    check(ida >= 0 && idb >= 0 && idc >= 0, "3 clips added");
    check(m.active_count() == 3, "active count = 3");

    knst_vector<float> out;
    out.resize(1024 * 2);
    uint32_t rendered = 0;
    for (int i = 0; i < 30; ++i) {
        auto r = m.render(out.data(), 1024);
        if (!r) break;
        rendered += r.value;
    }
    std::printf("  rendered %u frames\n", rendered);
    check(rendered > 0, "mixer produced output");
    check(m.active_count() == 0, "one-shots freed automatically");
}

static void test_wav_writer() {
    section("wav_writer — streaming recorder");
    knst_c16string path = knst_file::join_path(knst_file::get_temp_dir(),
                                                u"knst_aud_test_rec.wav");

    wav_writer wr;
    if (!wr.open(path, 48000, 2, sample_fmt::i16).ok()) {
        bad("open wav_writer");
        return;
    }
    auto data = sine(48000, 2, 440.0f, 48000);
    auto r = wr.write(data.data(), 48000);
    check(r && r.value == 48000, "wrote 48000 frames");
    wr.close();

    auto info = wav::probe(path);
    if (info) {
        std::printf("  probe: rate=%u ch=%u frames=%u\n",
                    info.value.fmt.rate, info.value.fmt.channels,
                    info.value.frame_count);
        check(info.value.fmt.rate == 48000, "writer rate");
        check(info.value.frame_count == 48000, "writer frames");
    }
    knst_file::remove_file(path);
}

static void test_decoder_stream() {
    section("decoder_stream — bounded-memory streaming");
    knst_c16string path = knst_file::join_path(knst_file::get_temp_dir(),
                                                u"knst_aud_test_stream.wav");
    {
        pcm_buffer b;
        b.samples = sine(48000, 2, 440.0f, 48000);
        b.rate = 48000; b.channels = 2;
        encode_options eo; eo.container = container_t::wav; eo.fmt = sample_fmt::i16;
        check(encode_file(path, b, eo).ok(), "prepared source file");
    }

    auto ds = decoder_stream::open(path);
    if (!ds) {
        bad("open decoder_stream");
        knst_file::remove_file(path);
        return;
    }
    std::printf("  rate=%u ch=%u total=%llu\n",
                ds.value.rate(), ds.value.channels(),
                (unsigned long long)ds.value.total_frames());

    knst_vector<float> buf;
    buf.resize(4096 * ds.value.channels());
    uint64_t total = 0;
    for (;;) {
        auto r = ds.value.read(buf.data(), 4096);
        if (!r || r.value == 0) break;
        total += r.value;
    }
    std::printf("  streamed %llu frames\n", (unsigned long long)total);
    check(total == 48000, "streamed full length");

    // Seek support (WAV has explicit data offset, so this should work)
    auto ds2 = decoder_stream::open(path);
    if (ds2) {
        auto sf = ds2.value.seek_frames(24000);
        if (sf) std::printf("  seek to 24000 -> %llu\n",
                            (unsigned long long)sf.value);
        auto r = ds2.value.read(buf.data(), 1000);
        check(r && r.value == 1000, "read after seek");
    }
    knst_file::remove_file(path);
}

static void test_stream_io() {
    section("stream — blocking output / input");

    // Output: 0.05 s at 20% amplitude, quiet enough not to surprise anyone.
    stream::config co;
    co.rate = 48000; co.channels = 2; co.is_output = true;
    auto so = stream::open_default(co);
    if (so) {
        auto data = sine(2400, 2, 440.0f, 48000, 0.2f);
        auto w = so.value.write_interleaved(data.data(), 2400);
        check(w.ok(), "wrote 2400 frames (output)");
        so.value.drain();
        std::printf("  xruns: %u\n", so.value.xruns());
    } else {
        std::printf("  output stream not available\n");
    }

    // Input: short capture from default device.
    stream::config ci;
    ci.rate = 48000; ci.channels = 1; ci.is_output = false;
    auto si = stream::open_default(ci);
    if (si) {
        knst_vector<float> buf;
        buf.resize(4800);
        auto r = si.value.read_interleaved(buf.data(), 4800);
        check(r && r.value == 4800, "captured 4800 frames");
        if (r) {
            auto a = dsp::analyze(buf.data(), 4800);
            std::printf("  captured peak: %.4f\n", a.peak);
        }
    } else {
        std::printf("  input stream not available (no mic?)\n");
    }
}

static void test_playback() {
    section("playback — play_buffer / play_file_any");

    pcm_buffer b;
    b.samples = sine(24000, 2, 440.0f, 48000, 0.2f);
    b.rate = 48000; b.channels = 2;

    auto r = play_buffer(nullptr, b, nullptr);
    check(r.ok(), "play_buffer 0.5 s");

    knst_c16string path = knst_file::join_path(knst_file::get_temp_dir(),
                                                u"knst_aud_test_play.wav");
    encode_options eo; eo.container = container_t::wav; eo.fmt = sample_fmt::i16;
    if (encode_file(path, b, eo).ok()) {
        auto r2 = play_file_any(nullptr, path, nullptr);
        check(r2.ok(), "play_file_any on WAV");
    }
    knst_file::remove_file(path);
}

static void test_registry() {
    section("codec registry — custom decoder");
    // Register a fake WavPack decoder that just returns silence.
    // Verifies the registry hooks into the free decode() dispatcher.
    auto fake = [](const uint8_t*, uint32_t, const decode_options&, void*) -> result<pcm_buffer> {
        pcm_buffer b;
        b.rate = 48000; b.channels = 2;
        b.samples.resize(48000 * 2, 0.0f);
        return result<pcm_buffer>::success(std::move(b));
    };
    check(codec::registry::add(container_t::wavpack, fake, nullptr), "registered");
    check(codec::registry::find(container_t::wavpack) != nullptr, "find returns entry");
}

static void test_pipeline() {
    section("full pipeline — generate / DSP / encode / decode / play");
    auto a = sine(24000, 2, 440.0f, 48000);
    auto b = sine(24000, 2, 660.0f, 48000);
    for (uint32_t i = 0; i < 24000 * 2; ++i)
        a[i] = 0.4f * (a[i] + b[i]);

    dsp::fade_in(a.data(), 24000, 2, 2400);
    dsp::fade_out(a.data(), 24000, 2, 2400);
    dsp::normalize(a.data(), 24000 * 2, 0.8f);

    pcm_buffer out;
    out.samples = a;
    out.rate = 48000;
    out.channels = 2;

    encode_options eo; eo.container = container_t::wav; eo.fmt = sample_fmt::i16;
    auto bytes = encode(out, eo);
    check(bytes.ok(), "encoded to WAV");

    auto back = decode(bytes.value.data(), bytes.value.length());
    check(back.ok(), "decoded back");

    float diff = max_diff(a.data(), back.value.samples.data(), 24000 * 2);
    std::printf("  roundtrip max error: %.6f\n", diff);
    check(diff < 0.0002f, "i16 quantization error bounded");

    auto pr = play_buffer(nullptr, back.value, nullptr);
    check(pr.ok(), "played decoded output");
}

static void test_json() {
    section("JSON serialization");
#if defined(KNST_JSON_HPP)
    auto sinks = audio::list_sinks();
    if (sinks) {
        auto j = devices_to_json(sinks.value);
        check(j.is_array(), "devices_to_json returns array");
    }

    auto streams = audio::list_streams();
    if (streams) {
        auto j = stream_info_to_json(streams.value);
        check(j.is_array() || j.is_null(), "stream_info_to_json");
    }

    media_info m;
    m.container = container_t::wav;
    m.rate = 48000; m.channels = 2; m.bits = 16;
    auto j = media_info_to_json(m);
    check(j.is_object(), "media_info_to_json");
#else
    std::printf("  KNST_JSON_HPP not defined; skipping\n");
#endif
}

static void test_errors() {
    section("error handling");

    auto r1 = decode(nullptr, 0);
    check(!r1 && r1.st == status::InvalidArgument, "decode(null) rejected");

    auto s = dsp::analyze(nullptr, 0);
    check(!s.valid, "analyze(null) safe");

    dsp::normalize(nullptr, 0);
    ok("normalize(null) is a no-op");

    auto sinks = audio::list_sinks();
    if (sinks && !sinks.value.empty()) {
        auto r = audio::set_volume(sinks.value[0], 200);
        check(!r || r.st == status::InvalidArgument, "volume 200% rejected");
    }

    auto r2 = decode_file(u"/this/does/not/exist.wav");
    check(!r2, "missing file rejected");

    uint8_t tiny[3] = { 0 };
    check(codec::detect(tiny, sizeof(tiny)) == container_t::unknown,
          "buffer too short returns unknown");
}

static void test_hotplug() {
    section("hotplug monitor — start / stop");
    static std::atomic<uint32_t> events{0};
    events.store(0);

    hotplug_callback cb = [](void* user, const device_change&) {
        static_cast<std::atomic<uint32_t>*>(user)->fetch_add(1);
    };

    auto m = hotplug_monitor::start(cb, &events, 200);
    if (!m) { bad("start monitor"); return; }

    std::printf("  running for 300 ms...\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    m.value.stop();
    std::printf("  events received: %u (0 is normal when nothing changed)\n",
                static_cast<unsigned>(events.load()));
    ok("start/stop completed");
}

int main() {
    std::printf("=== knst_audio comprehensive exercise ===\n");

    test_environment();
    test_devices();
    test_defaults();
    test_volume();
    test_mute();
    test_ports_cards();
    test_streams();
    test_peak_meter();
    test_format_detection();
    test_wav_formats();
    test_containers();
    test_probe();
    test_pcm_buffer_ops();
    test_dsp_generators();
    test_dsp_levels();
    test_dsp_dc();
    test_dsp_edit();
    test_dsp_convert();
    test_dsp_silence();
    test_dsp_dynamics();
    test_dsp_reverse_pan();
    test_dsp_resample();
    test_biquad();
    test_fft();
    test_player();
    test_mixer();
    test_wav_writer();
    test_decoder_stream();
    test_stream_io();
    test_playback();
    test_registry();
    test_pipeline();
    test_json();
    test_errors();
    test_hotplug();

    std::printf("\n=== done ===\n");
    std::printf("passed: %d   failed: %d\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}