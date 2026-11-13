/*
  knst_microphone_basic.cpp —  TUTORIAL (40 sections)

*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <cmath>
#include <cstdint>
#include <cstring>


// ============================================================
// Helpers
// ============================================================

static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}

static const char* platform_name() {
#if defined(KNST_USING_PLATFORM_ANDROID)
    return "Android (AAudio)";
#elif KNST_USING_PLATFORM_WINDOWS
    return "Windows (waveIn default + WASAPI named)";
#else
    return "Linux (parec / PulseAudio / PipeWire-pulse)";
#endif
}

static void print_name(const knst_audio_device& d) {
    if (!d.description.empty()) std::cout << d.description;
    else                        std::cout << d.name;
}

static knst_vector<float> make_sine_f32(uint32_t frames, uint32_t channels,
                                        uint32_t rate, double freq, double amp) {
    knst_vector<float> out;
    out.resize(frames * channels);
    double phase = 0.0;
    double inc = 2.0 * 3.14159265358979323846 * freq / rate;
    for (uint32_t i = 0; i < frames; ++i) {
        float v = (float)(std::sin(phase) * amp);
        for (uint32_t c = 0; c < channels; ++c) out[i * channels + c] = v;
        phase += inc;
        if (phase >= 2.0 * 3.14159265358979323846) phase -= 2.0 * 3.14159265358979323846;
    }
    return out;
}


// ============================================================
// main
// ============================================================

int main() {

    std::cout << "Platform: " << platform_name() << "\n";
    std::cout << "SCOPE    : knst_mic only (knst_audio is separate)\n";
    std::cout << "SAFE     : uses default mic; no persistent changes\n";

    knst_c16string sandbox = knst_file::join_path(
        knst_file::get_temp_dir(), u"knst_mic_demo");
    knst_file::create_directories(sandbox);

    // =====================================================================
    section("1) Environment & availability");
    // =====================================================================
    {
        auto sources = knst_aud::audio::list_sources();
        std::cout << "  sources available : " << (sources ? sources.value.size() : 0) << "\n";
#if KNST_USING_PLATFORM_LINUX
        std::cout << "  parec on PATH     : ";
        auto r = knst_process::run_capture(u"which", {u"parec"}, {}, nullptr, 0, 1000);
        std::cout << (r.error == knst_process_error::None && r.exit_code == 0 ? "yes" : "no") << "\n";
#endif
        std::cout << "  sizeof(mic_ref)   : " << sizeof(knst_mic::mic_ref) << "\n";
        std::cout << "  sizeof(vad_config): " << sizeof(knst_mic::vad_config) << "\n";
    }

    // =====================================================================
    section("2) is_monitor() — name classification");
    // =====================================================================
    {
        struct { const char16_t* name; bool expect; } cases[] = {
            { u"alsa_output.pci-0000_08_00.4.analog-stereo.monitor", true  },
            { u"Monitor of Starship Analog Stereo",                  true  },
            { u"alsa_input.pci-0000_08_00.4.analog-stereo",          false },
            { u"GENERAL WEBCAM Tek Kanallı",                          false },
        };
        int ok = 0;
        for (auto& c : cases) {
            bool got = knst_mic::is_monitor(knst_c16string(c.name));
            std::cout << "  " << knst_dev::text(knst_dev::to_utf8(knst_c16string(c.name)))
                      << "  -> " << (got ? "monitor" : "mic")
                      << (got == c.expect ? "  ✓" : "  ✗") << "\n";
            if (got == c.expect) ++ok;
        }
        std::cout << "  result: " << ok << "/4 correct\n";
    }

    // =====================================================================
    section("3) enumerate_microphones() — real mics only");
    // =====================================================================
    {
        auto mics = knst_mic::enumerate_microphones(false);
        if (!mics) {
            std::cout << "  failed: " << mics.error_string() << "\n";
        } else {
            std::cout << "  mics (monitors filtered): " << mics.value.size() << "\n";
            for (auto& m : mics.value) {
                std::cout << "    [" << m.index << "] " << knst_dev::text(knst_dev::to_utf8(m.name));
                if (m.is_default) std::cout << "  [DEFAULT]";
                std::cout << "\n";
            }
        }
    }

    // =====================================================================
    section("4) enumerate_microphones(true) — with monitors");
    // =====================================================================
    {
        auto all = knst_mic::enumerate_microphones(true);
        if (all) {
            int monitors = 0;
            for (auto& m : all.value) if (m.is_monitor) ++monitors;
            std::cout << "  total sources : " << all.value.size() << "\n";
            std::cout << "  monitors      : " << monitors << "\n";
            std::cout << "  real mics     : " << (all.value.size() - monitors) << "\n";
        }
    }

    // =====================================================================
    section("5) default_microphone()");
    // =====================================================================
    {
        auto def = knst_mic::default_microphone();
        if (def) {
            std::cout << "  "; print_name(def.value); std::cout << "\n";
            std::cout << "  is_monitor: " << (knst_mic::is_monitor(def.value.name) ? "yes (BUG)" : "no") << "\n";
        } else {
            std::cout << "  not found: " << def.error_string() << "\n";
        }
    }

    // =====================================================================
    section("6) monitor_source() — loopback");
    // =====================================================================
    {
        auto mon = knst_mic::monitor_source();
        if (mon) {
            std::cout << "  "; print_name(mon.value); std::cout << "\n";
        } else {
            std::cout << "  none: " << mon.error_string() << "\n";
        }
    }

    // =====================================================================
    section("7) record_raw() — 500ms @ 16kHz mono");
    // =====================================================================
    {
        auto r = knst_mic::record_raw(nullptr, 500, 16000, 1);
        if (!r) {
            std::cout << "  failed: " << r.error_string() << "\n";
        } else {
            uint32_t expected = 500 * 16000 * 2 / 1000;
            std::cout << "  bytes: " << r.value.length() << " (expected " << expected << ")\n";
            // Check non-zero
            int nonzero = 0;
            for (uint32_t i = 0; i < r.value.length(); ++i) if (r.value[i] != 0) ++nonzero;
            std::cout << "  nonzero bytes: " << nonzero << " / " << r.value.length() << "\n";
        }
    }

    // =====================================================================
    section("8) record_raw() — different rates");
    // =====================================================================
    {
        struct { uint32_t rate, ch; const char* name; } cases[] = {
            {  8000, 1, "8k/mono"    },
            { 16000, 1, "16k/mono"   },
            { 44100, 2, "44.1k/stereo" },
            { 48000, 2, "48k/stereo"  },
        };
        for (auto& c : cases) {
            auto r = knst_mic::record_raw(nullptr, 200, c.rate, c.ch);
            if (r) {
                uint32_t exp = 200 * c.rate * c.ch * 2 / 1000;
                std::cout << "  " << c.name << ": " << r.value.length() << "/" << exp;
                if (r.value.length() == exp) std::cout << "  ✓";
                else std::cout << "  ✗";
                std::cout << "\n";
            } else {
                std::cout << "  " << c.name << ": " << r.error_string() << "\n";
            }
        }
    }

    // =====================================================================
    section("9) record_wav() — 1s + decode verification");
    // =====================================================================
    {
        knst_c16string path = knst_file::join_path(sandbox, u"rec_1s.wav");
        auto r = knst_mic::record_wav(nullptr, path, 1, 16000, 1);
        if (!r) {
            std::cout << "  record failed: " << r.error_string() << "\n";
        } else {
            uint64_t sz = knst_file::get_file_size(path);
            std::cout << "  wav size: " << sz << " bytes\n";
            auto info = knst_aud::wav::probe(path);
            if (info) {
                std::cout << "  rate=" << info.value.fmt.rate
                          << "  ch=" << info.value.fmt.channels
                          << "  frames=" << info.value.frame_count << "\n";
            }
        }
    }

    // =====================================================================
    section("10) record_wav() — different sample rates");
    // =====================================================================
    {
        struct { uint32_t rate; uint32_t ch; } rates[] = {
            {  8000, 1 }, { 16000, 1 }, { 44100, 2 }, { 48000, 2 },
        };
        for (auto& r : rates) {
            knst_c16string path = knst_file::join_path(
                sandbox, knst_c16string(u"rate_") + knst_dev::u32_to_string(r.rate) + u".wav");
            auto w = knst_mic::record_wav(nullptr, path, 1, r.rate, r.ch);
            if (w) {
                auto info = knst_aud::wav::probe(path);
                std::cout << "  " << r.rate << "/" << r.ch
                          << ": " << (info ? knst_dev::text(knst_dev::to_utf8(knst_dev::u32_to_string(info.value.frame_count))) : "?")
                          << " frames\n";
            } else {
                std::cout << "  " << r.rate << "/" << r.ch << ": " << w.error_string() << "\n";
            }
        }
    }

    // =====================================================================
    section("11) measure_level() — 500ms peak/rms");
    // =====================================================================
    {
        auto r = knst_mic::measure_level(nullptr, 500);
        if (!r) {
            std::cout << "  failed: " << r.error_string() << "\n";
        } else {
            std::cout << "  peak  : " << r.value.peak << " (" << r.value.peak_db << " dB)\n";
            std::cout << "  rms   : " << r.value.rms  << " (" << r.value.rms_db  << " dB)\n";
            std::cout << "  samples: " << r.value.samples
                      << "  clipped: " << r.value.clipped << "\n";
        }
    }

        // =====================================================================
    section("12) measure_all() — all sources");
    // =====================================================================
    {
    // Every platform can now measure all sources:
    //   Linux   — parec --device
    //   Windows — WASAPI GetDevice (waveIn fallback for default)
    //   Android — default mic only (AAudio)
#if defined(KNST_USING_PLATFORM_ANDROID)
    std::cout << "  (Android: only the default mic is capturable)\n";
    auto def = knst_mic::default_microphone();
    if (def) {
        auto lv = knst_mic::measure_level(nullptr, 200);
        std::cout << "    ";
        print_name(def.value);
        if (lv) std::cout << "  peak=" << lv.value.peak_db << " dB\n";
        else    std::cout << "  peak=" << lv.error_string() << "\n";
    }
#else
        auto r = knst_mic::measure_all(200);
        if (!r) {
            std::cout << "  failed: " << r.error_string() << "\n";
        } else {
            std::cout << "  measured " << r.value.size() << " sources\n";
            auto srcs = knst_aud::audio::list_sources();
            for (uint32_t i = 0; i < r.value.size() && srcs && i < srcs.value.size(); ++i) {
                std::cout << "    [" << i << "] ";
                print_name(srcs.value[i]);
                std::cout << "  peak=" << r.value[i].peak_db << " dB"
          << "  linear=" << r.value[i].peak
          << "  clipped=" << r.value[i].clipped
          << "  samples=" << r.value[i].samples << "\n";
            }
        }
#endif
    }

    // =====================================================================
    section("13) recorder — file mode (i16)");
    // =====================================================================
    {
        knst_aud::wav::format f;
        f.rate = 48000; f.channels = 1;
        f.sample = knst_aud::wav::format::sample_t::i16;
        knst_c16string path = knst_file::join_path(sandbox, u"rec_i16.wav");

        auto rec = knst_mic::recorder::open_file(path, f);
        if (!rec) {
            std::cout << "  open failed: " << rec.error_string() << "\n";
        } else {
            knst_vector<float> buf = make_sine_f32(4800, 1, 48000, 440.0, 0.3);
            for (int i = 0; i < 5; ++i) rec.value.capture_interleaved(buf.data(), 4800);
            rec.value.finish();

            auto info = knst_aud::wav::probe(path);
            if (info) {
                std::cout << "  wrote " << info.value.frame_count << " frames (expect 24000)\n";
            }
        }
    }

    // =====================================================================
    section("14) recorder — file mode (i24) — bug check");
    // =====================================================================
    {
        // This section verifies the WAV header for i24 format.
        // The old bug wrote bits=16/32 instead of 24.
        knst_aud::wav::format f;
        f.rate = 48000; f.channels = 1;
        f.sample = knst_aud::wav::format::sample_t::i24;
        knst_c16string path = knst_file::join_path(sandbox, u"rec_i24.wav");

        auto rec = knst_mic::recorder::open_file(path, f);
        if (rec) {
            knst_vector<float> buf = make_sine_f32(4800, 1, 48000, 440.0, 0.3);
            for (int i = 0; i < 5; ++i) rec.value.capture_interleaved(buf.data(), 4800);
            rec.value.finish();

            auto info = knst_aud::wav::probe(path);
            if (info) {
                                std::cout << "  bits_per_sample: " << (int)info.value.fmt.sample << "\n";
                if (info.value.fmt.sample == knst_aud::wav::format::sample_t::i24)
                    std::cout << "  ✓ WAV header correct for i24\n";
                else
                    std::cout << "  ✗ BUG: header says " << (int)info.value.fmt.sample
                              << " but sample is i24\n";
            }
        }
    }

    // =====================================================================
    section("15) recorder — memory mode");
    // =====================================================================
    {
        knst_aud::wav::format f;
        f.rate = 16000; f.channels = 1;
        f.sample = knst_aud::wav::format::sample_t::i16;

        auto rec = knst_mic::recorder::open_memory(f);
        if (rec) {
            knst_vector<float> buf = make_sine_f32(1600, 1, 16000, 440.0, 0.3);
            for (int i = 0; i < 5; ++i) rec.value.capture_interleaved(buf.data(), 1600);
            std::cout << "  frames captured: " << rec.value.total_frames() << "\n";
            std::cout << "  samples stored : " << rec.value.samples().size() << "\n";
            std::cout << "  seconds        : " << rec.value.recorded_seconds() << "\n";
        }
    }

    // =====================================================================
    section("16) recorder — pump_from(stream)");
    // =====================================================================
    {
        auto sources = knst_aud::audio::list_sources();
        if (!sources || sources.value.empty()) {
            std::cout << "  (no sources)\n";
        } else {
            knst_aud::stream::config sc;
            sc.rate = 16000; sc.channels = 1; sc.buffer_frames = 512; sc.is_output = false;
            auto src = knst_aud::stream::open(sources.value[0], sc);

            if (!src) {
                std::cout << "  stream open failed: " << src.error_string() << "\n";
            } else {
                knst_aud::wav::format f;
                f.rate = 16000; f.channels = 1;
                f.sample = knst_aud::wav::format::sample_t::i16;
                knst_c16string path = knst_file::join_path(sandbox, u"pump.wav");

                auto rec = knst_mic::recorder::open_file(path, f);
                if (rec) {
                    for (int i = 0; i < 10; ++i) {
                        auto r = rec.value.pump_from(src.value, 512);
                        if (!r || r.value == 0) break;
                    }
                    rec.value.finish();
                    std::cout << "  captured " << rec.value.total_frames() << " frames\n";
                }
            }
        }
    }

    // =====================================================================
    section("17) VAD — silence (should stay inactive)");
    // =====================================================================
    {
        knst_mic::vad_config cfg;
        cfg.sample_rate = 16000;
        knst_mic::voice_detector vad(cfg);

        const uint32_t fs = 320;  // 20ms at 16kHz
        knst_vector<float> silent(fs, 0.0f);
        int active_frames = 0;
        for (int i = 0; i < 20; ++i)
            if (vad.feed(silent.data(), fs)) ++active_frames;

        std::cout << "  active frames: " << active_frames << " / 20\n";
        if (active_frames == 0) std::cout << "  ✓ VAD stayed inactive\n";
        else std::cout << "  ✗ BUG: activated on silence\n";
    }

    // =====================================================================
    section("18) VAD — tone burst (should activate)");
    // =====================================================================
    {
        knst_mic::vad_config cfg;
        cfg.sample_rate = 16000;
        knst_mic::voice_detector vad(cfg);

        const uint32_t fs = 320;
        double phase = 0.0;
        double inc = 2.0 * 3.14159265358979323846 * 440.0 / 16000;
        int active_frames = 0;
        for (int i = 0; i < 20; ++i) {
            knst_vector<float> buf(fs);
            for (uint32_t k = 0; k < fs; ++k) {
                buf[k] = 0.5f * (float)std::sin(phase);
                phase += inc;
            }
            if (vad.feed(buf.data(), fs)) ++active_frames;
        }
        std::cout << "  active frames: " << active_frames << " / 20\n";
        if (active_frames >= 5) std::cout << "  ✓ VAD activated on tone\n";
        else std::cout << "  ✗ VAD failed to activate\n";
    }

    // =====================================================================
    section("19) VAD — edge transitions");
    // =====================================================================
    {
        knst_mic::vad_config cfg;
        cfg.sample_rate = 16000;
        knst_mic::voice_detector vad(cfg);

        const uint32_t fs = 320;
        double phase = 0.0;
        double inc = 2.0 * 3.14159265358979323846 * 440.0 / 16000;

        // 10 silent frames → then 10 tone frames → then 10 silent frames
        auto feed_silent = [&]() {
            knst_vector<float> buf(fs, 0.0f);
            return vad.feed(buf.data(), fs);
        };
        auto feed_tone = [&]() {
            knst_vector<float> buf(fs);
            for (uint32_t k = 0; k < fs; ++k) {
                buf[k] = 0.5f * (float)std::sin(phase);
                phase += inc;
            }
            return vad.feed(buf.data(), fs);
        };

        int start_edges = 0, end_edges = 0;
        for (int i = 0; i < 10; ++i) {
            feed_silent();
            if (vad.last_transition() == knst_mic::voice_detector::edge::start) ++start_edges;
        }
        for (int i = 0; i < 10; ++i) {
            feed_tone();
            if (vad.last_transition() == knst_mic::voice_detector::edge::start) ++start_edges;
        }
        for (int i = 0; i < 10; ++i) {
            feed_silent();
            if (vad.last_transition() == knst_mic::voice_detector::edge::end) ++end_edges;
        }
        std::cout << "  start edges: " << start_edges << "\n";
        std::cout << "  end edges  : " << end_edges << "\n";
        if (start_edges == 1 && end_edges == 1)
            std::cout << "  ✓ single start + single end\n";
        else
            std::cout << "  ✗ unexpected edge count\n";
    }

    // =====================================================================
    section("20) AGC — quiet signal boost");
    // =====================================================================
    {
        knst_mic::agc a;
        knst_vector<float> buf(1000);
        for (uint32_t i = 0; i < 1000; ++i)
            buf[i] = 0.05f * (float)std::sin(i * 0.1);
        float g0 = a.current_gain();
        for (int k = 0; k < 5; ++k) a.process(buf.data(), 1000);
        float g1 = a.current_gain();
        std::cout << "  gain: " << g0 << " -> " << g1 << "\n";
        if (g1 > g0) std::cout << "  ✓ gain increased\n";
        else std::cout << "  ✗ gain did not increase\n";
    }

    // =====================================================================
    section("21) AGC — loud signal attenuation");
    // =====================================================================
    {
        knst_mic::agc a;
        knst_vector<float> buf(1000);
        for (uint32_t i = 0; i < 1000; ++i)
            buf[i] = 0.9f * (float)std::sin(i * 0.1);
        float g0 = a.current_gain();
        for (int k = 0; k < 5; ++k) a.process(buf.data(), 1000);
        float g1 = a.current_gain();
        std::cout << "  gain: " << g0 << " -> " << g1 << "\n";
        if (g1 < g0) std::cout << "  ✓ gain decreased\n";
        else std::cout << "  ✗ gain did not decrease\n";
    }

    // =====================================================================
    section("22) Noise gate — silence attenuation");
    // =====================================================================
    {
        knst_mic::gate_config gc;
        gc.sample_rate = 16000;
        gc.threshold_db = -45.0f;
        knst_mic::noise_gate g(gc);

        knst_vector<float> quiet(500);
        for (uint32_t i = 0; i < 500; ++i) quiet[i] = (i & 1) ? 1e-4f : -1e-4f;
        double rms_in = 0.0;
        for (uint32_t i = 0; i < 500; ++i) rms_in += quiet[i] * quiet[i];
        rms_in = std::sqrt(rms_in / 500);

        g.process(quiet.data(), 500);

        double rms_out = 0.0;
        for (uint32_t i = 0; i < 500; ++i) rms_out += quiet[i] * quiet[i];
        rms_out = std::sqrt(rms_out / 500);

        std::cout << "  rms: " << rms_in << " -> " << rms_out << "\n";
        if (rms_out < rms_in * 0.1) std::cout << "  ✓ attenuated\n";
        else std::cout << "  ✗ not attenuated\n";
    }

    // =====================================================================
    section("23) Noise gate — loud signal pass-through");
    // =====================================================================
    {
        knst_mic::gate_config gc;
        gc.sample_rate = 16000;
        knst_mic::noise_gate g(gc);

        knst_vector<float> loud(500);
        for (uint32_t i = 0; i < 500; ++i) loud[i] = 0.5f * (float)std::sin(i * 0.1);
        float peak_in = 0.0f;
        for (uint32_t i = 0; i < 500; ++i)
            if (std::fabs(loud[i]) > peak_in) peak_in = std::fabs(loud[i]);

        g.process(loud.data(), 500);

        float peak_out = 0.0f;
        for (uint32_t i = 0; i < 500; ++i)
            if (std::fabs(loud[i]) > peak_out) peak_out = std::fabs(loud[i]);

        std::cout << "  peak: " << peak_in << " -> " << peak_out << "\n";
        if (peak_out > peak_in * 0.9f) std::cout << "  ✓ loud passed\n";
        else std::cout << "  ✗ loud attenuated\n";
    }

    // =====================================================================
    section("24) Pitch detector — 440 Hz");
    // =====================================================================
    {
        knst_mic::pitch_config pc;
        pc.sample_rate = 16000;
        pc.min_hz = 200; pc.max_hz = 1000;
        knst_mic::pitch_detector pd(pc);

        knst_vector<float> sig = make_sine_f32(4096, 1, 16000, 440.0, 0.5);
        float f0 = pd.detect(sig.data(), 4096);
        std::cout << "  detected: " << f0 << " Hz (expect 440)\n";
        if (std::fabs(f0 - 440.0f) < 10.0f) std::cout << "  ✓ within 10 Hz\n";
        else std::cout << "  ✗ out of range\n";
    }

    // =====================================================================
    section("25) Pitch detector — 220 Hz");
    // =====================================================================
    {
        knst_mic::pitch_config pc;
        pc.sample_rate = 16000;
        pc.min_hz = 100; pc.max_hz = 500;
        knst_mic::pitch_detector pd(pc);

        knst_vector<float> sig = make_sine_f32(4096, 1, 16000, 220.0, 0.5);
        float f0 = pd.detect(sig.data(), 4096);
        std::cout << "  detected: " << f0 << " Hz (expect 220)\n";
        if (std::fabs(f0 - 220.0f) < 5.0f) std::cout << "  ✓ within 5 Hz\n";
        else std::cout << "  ✗ out of range\n";
    }

    // =====================================================================
    section("26) Pitch detector — silence");
    // =====================================================================
    {
        knst_mic::pitch_config pc;
        pc.sample_rate = 16000;
        knst_mic::pitch_detector pd(pc);

        knst_vector<float> silent(4096, 0.0f);
        float f0 = pd.detect(silent.data(), 4096);
        std::cout << "  detected: " << f0 << " Hz\n";
        if (f0 == 0.0f) std::cout << "  ✓ silence correctly gives 0\n";
        else std::cout << "  ✗ false pitch\n";
    }

    // =====================================================================
    section("27) Silence detector — synthetic silence");
    // =====================================================================
    {
        knst_mic::silence_config sc;
        sc.sample_rate = 16000;
        sc.min_silence_ms = 100;
        knst_mic::silence_detector sd(sc);

        knst_vector<float> silent(1600, 0.0f);
        bool triggered = false;
        for (int i = 0; i < 5; ++i)
            if (sd.feed(silent.data(), 1600)) { triggered = true; break; }
        std::cout << "  triggered: " << (triggered ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("28) Silence detector — tone (shouldn't trigger)");
    // =====================================================================
    {
        knst_mic::silence_config sc;
        sc.sample_rate = 16000;
        sc.min_silence_ms = 100;
        knst_mic::silence_detector sd(sc);

        knst_vector<float> tone = make_sine_f32(1600, 1, 16000, 440.0, 0.5);
        bool triggered = false;
        for (int i = 0; i < 5; ++i)
            if (sd.feed(tone.data(), 1600)) { triggered = true; break; }
        std::cout << "  triggered: " << (triggered ? "yes (BUG)" : "no (correct)") << "\n";
    }

    // =====================================================================
    section("29) voice_recorder — VAD-triggered recording");
    // =====================================================================
    {
        knst_mic::voice_recorder::config vc;
        vc.vad.sample_rate = 16000;
        vc.format.rate = 16000;
        vc.format.channels = 1;
        vc.format.sample = knst_aud::wav::format::sample_t::i16;
        vc.min_speech_ms = 100;
        vc.max_silence_ms = 200;

        knst_c16string path = knst_file::join_path(sandbox, u"voice.wav");
        knst_mic::voice_recorder vr(vc);
        if (vr.start(path)) {
            // Feed 5 frames of tone (VAD activates after 3 frames)
            knst_vector<float> tone = make_sine_f32(320, 1, 16000, 440.0, 0.5);
            uint32_t total_bytes = 0;
            for (int i = 0; i < 5; ++i) {
                total_bytes += vr.feed(tone.data(), 320);
            }
            std::cout << "  feed returned " << total_bytes << " total bytes\n";
            if (vr.stop()) {
                uint64_t sz = knst_file::get_file_size(path);
                std::cout << "  file size: " << sz << " bytes\n";
            }
        }
    }

    // =====================================================================
    section("30) voice_recorder — i24 format bug check");
    // =====================================================================
    {
        // Section 30 verifies that voice_recorder::feed returns the
        // correct byte count for non-i16 formats (3 bytes for i24).
        knst_mic::voice_recorder::config vc;
        vc.vad.sample_rate = 16000;
        vc.format.rate = 16000;
        vc.format.channels = 1;
        vc.format.sample = knst_aud::wav::format::sample_t::i24;
        vc.min_speech_ms = 50;
        vc.max_silence_ms = 100;

        knst_c16string path = knst_file::join_path(sandbox, u"voice_i24.wav");
        knst_mic::voice_recorder vr(vc);
        if (vr.start(path)) {
            knst_vector<float> tone = make_sine_f32(320, 1, 16000, 440.0, 0.5);
            uint32_t bytes_per_feed = 0;
            for (int i = 0; i < 5; ++i) {
                uint32_t b = vr.feed(tone.data(), 320);
                if (b > 0 && bytes_per_feed == 0) bytes_per_feed = b;
            }
            vr.stop();
            // i24 → 3 bytes per sample; hardcoded *2 bug → 2 bytes
            std::cout << "  bytes returned for 320 samples: " << bytes_per_feed << "\n";
            std::cout << "  expected: " << (320 * 3) << " (320 * 3 bytes/sample i24)\n";
            if (bytes_per_feed == 320 * 3)
                std::cout << "  ✓ correct i24 byte count\n";
            else if (bytes_per_feed == 320 * 2)
                std::cout << "  ✗ BUG: hardcoded *2 (assumes i16)\n";
            else
                std::cout << "  ? unexpected value\n";
        }
    }

    // =====================================================================
    section("31) estimate_delay — known delay");
    // =====================================================================
    {
        const uint32_t N = 1024;
        const uint32_t SR = 16000;
        knst_vector<float> a(N), b(N);
        for (uint32_t i = 0; i < N; ++i) {
            a[i] = (float)std::sin(2.0 * 3.14159265358979323846 * 300.0 * i / SR);
            b[i] = (i >= 2) ? a[i - 2] : 0.0f;
        }
        int32_t delay = knst_mic::estimate_delay(a.data(), b.data(), N, 16);
        std::cout << "  estimated delay: " << delay << " samples (expect 2)\n";
        if (delay == 2) std::cout << "  ✓ correct\n";
        else std::cout << "  ✗ wrong delay\n";
    }

    // =====================================================================
    section("32) beamform_2mic — output verification");
    // =====================================================================
    {
        const uint32_t N = 1024;
        const uint32_t SR = 16000;
        knst_vector<float> a(N), b(N);
        for (uint32_t i = 0; i < N; ++i) {
            a[i] = (float)std::sin(2.0 * 3.14159265358979323846 * 300.0 * i / SR);
            b[i] = (i >= 2) ? a[i - 2] : 0.0f;
        }
        auto out = knst_mic::beamform_2mic(a.data(), b.data(), N, 2);
        std::cout << "  output size: " << out.size() << " (expect " << N << ")\n";
        if (out.size() == N) std::cout << "  ✓ correct size\n";
        else std::cout << "  ✗ wrong size\n";
    }

    // =====================================================================
    section("33) mic_array — mono pass-through");
    // =====================================================================
    {
        knst_mic::mic_array arr;
        knst_vector<float> mono = make_sine_f32(1024, 1, 16000, 440.0, 0.5);
        auto out = arr.beamform(mono.data(), 1024, 1);
        if (out) std::cout << "  mono output: " << out.value.size() << " samples\n";
        else    std::cout << "  failed: " << out.error_string() << "\n";
    }

    // =====================================================================
    section("34) mic_array — stereo beamform");
    // =====================================================================
    {
        knst_mic::mic_array arr;
        // Interleaved stereo: L then R, R delayed by 2
        const uint32_t N = 1024;
        knst_vector<float> st(N * 2);
        for (uint32_t i = 0; i < N; ++i) {
            float v = (float)std::sin(2.0 * 3.14159265358979323846 * 300.0 * i / 16000);
            st[i * 2]     = v;
            st[i * 2 + 1] = (i >= 2) ? v : 0.0f;  // approximate delay
        }
        auto out = arr.beamform(st.data(), N, 2);
        if (out) std::cout << "  stereo beamform: " << out.value.size() << " samples\n";
        else    std::cout << "  failed: " << out.error_string() << "\n";
    }

    // =====================================================================
    section("35) fmt::float_to_s16 / s16_to_float roundtrip");
    // =====================================================================
    {
        float src[4] = { 0.0f, 0.5f, -0.5f, 1.0f };
        int16_t s16[4];
        knst_mic::fmt::float_to_s16(src, s16, 4);
        float back[4];
        knst_mic::fmt::s16_to_float(s16, back, 4);
        float max_err = 0.0f;
        for (int i = 0; i < 4; ++i) {
            float e = std::fabs(back[i] - src[i]);
            if (e > max_err) max_err = e;
        }
        std::cout << "  max error: " << max_err << "\n";
        if (max_err < 0.001f) std::cout << "  ✓ roundtrip OK\n";
        else std::cout << "  ✗ roundtrip error\n";
    }

    // =====================================================================
    section("36) fmt::s16_bytes_to_float / float_to_s16_bytes");
    // =====================================================================
    {
        float src[4] = { 0.0f, 0.5f, -0.5f, 1.0f };
        knst_byte_string bytes = knst_mic::fmt::float_to_s16_bytes(src, 4);
        auto back = knst_mic::fmt::s16_bytes_to_float(bytes);
        std::cout << "  bytes: " << bytes.length() << " (expect 8)\n";
        std::cout << "  samples: " << back.size() << " (expect 4)\n";
    }

    // =====================================================================
    section("37) JSON bridge (if KNST_JSON_HPP available)");
    // =====================================================================
    {
#if defined(KNST_JSON_HPP)
        auto mics = knst_mic::enumerate_microphones(true);
        if (mics) {
            knst_json j = knst_mic::mic_refs_to_json(mics.value);
            knst_json_buffer b;
            knst_json_dump_options opt;
            opt.indent = 2;
            if (j.dump(b, opt))
                std::cout << "  mic_refs_to_json: " << b.size() << " bytes\n";
        }
#else
        std::cout << "  (KNST_JSON_HPP not defined)\n";
#endif
    }

    // =====================================================================
    section("38) Error handling");
    // =====================================================================
    {
        auto r1 = knst_mic::record_raw(nullptr, 10, 16000, 1);   // too short
        std::cout << "  record_raw(10ms) : "
                  << (r1 ? "ok" : r1.error_string()) << "\n";

        auto r2 = knst_mic::record_raw(nullptr, 500, 100, 1);    // bad rate
        std::cout << "  record_raw(100Hz): "
                  << (r2 ? "ok" : r2.error_string()) << "\n";

        knst_aud::wav::format f;
        f.rate = 0;
        auto r3 = knst_mic::recorder::open_file(u"/tmp/x.wav", f);
        std::cout << "  open_file(rate=0): "
                  << (r3 ? "ok" : r3.error_string()) << "\n";

        knst_mic::mic_array arr;
        auto r4 = arr.beamform(nullptr, 0, 0);
        std::cout << "  beamform(null)   : "
                  << (r4 ? "ok" : r4.error_string()) << "\n";
    }

    section("39) Platform summary");
    {
#if defined(KNST_USING_PLATFORM_ANDROID)
        std::cout << "  Backend   : AAudio (mic_aaudio::capture_s16le)\n";
        std::cout << "  record_raw: default mic only\n";
        std::cout << "  Stream    : full support\n";
#elif KNST_USING_PLATFORM_WINDOWS
        std::cout << "  Backend   : waveIn  (default endpoint, no COM)\n";
        std::cout << "              WASAPI  (named endpoint, via IMMDeviceEnumerator)\n";
        std::cout << "  record_raw(nullptr)      : waveIn  (default mic)\n";
        std::cout << "  record_raw(&device, ...) : WASAPI  (that specific mic)\n";
        std::cout << "  measure_all()            : every source measured separately\n";
#else
        std::cout << "  Backend   : parec (PulseAudio/PipeWire-pulse)\n";
        std::cout << "  record_raw: any source via --device\n";
        std::cout << "  Stream    : pa_simple capture\n";
#endif
    }

    // =====================================================================
    section("40) Cleanup");
    // =====================================================================
    {
        knst_file::remove_all(sandbox);
        std::cout << "  sandbox removed: " << sandbox << "\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}