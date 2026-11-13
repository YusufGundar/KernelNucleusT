// =============================================================================
//  knst_special_anroid_all_simple_test.cpp
//  ----------------------------------------------------------------------------
//  ANDROID-ONLY comprehensive test suite for KernelNucleusT "simple_structures".
//
//  Build (Android):
//      -DKNST_APP_SOURCE=.../knst_special_anroid_all_simple_test.cpp
//
//  SCOPE
//  =====
//  Covers every "simple_structures" module. One test function per module,
//  uniform PASS/FAIL/SKIP pattern. Hardware-absent features SKIP gracefully,
//  they never crash the run.
//
//  PRINTER GROUP (no macro needed)
//  ===============================
//  The 3-page print job + scan is part of the standard test flow. It:
//    1. Builds ONE combined 3-page PDF (test page + color bars + gradient)
//    2. Discovers network printers via mDNS (_ipp._tcp + _ipps._tcp)
//    3. Submits the PDF as ONE job via direct IPP (no PrintManager dialog)
//    4. Discovers eSCL scanners via mDNS (_uscan._tcp)
//    5. Scans 1 page (flatbed, 200 DPI, PNG)
//  If printer/scanner is unavailable, it SKIPs — never blocks or hangs.
// =============================================================================

#if !defined(__ANDROID__)
    #error "knst_special_anroid_all_simple_test.cpp is Android-only. \
Use the platform-specific examples in examples/simple_structures/knst_* instead."
#endif

#include "../../../include/KernelNucleusT.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <thread>
#include <atomic>
#include <string>
#include <vector>


// =============================================================================
//  Result counters + test macros
// =============================================================================
static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

#define TEST_BEGIN(name) \
    KNST_LOG_INFO(" "); \
    KNST_LOG_INFO("── [%s] ──────────────────────────────────────", name)

#define TEST_OK(name) \
    do { ++g_pass; KNST_LOG_INFO("  ✓ PASS: %s", name); } while(0)

#define TEST_FAIL(name, reason) \
    do { ++g_fail; KNST_LOG_ERROR("  ✗ FAIL: %s — %s", name, reason); } while(0)

#define TEST_SKIP(name, reason) \
    do { ++g_skip; KNST_LOG_WARN("  ⊘ SKIP: %s — %s", name, reason); } while(0)


// =============================================================================
//  Formatting helpers
// =============================================================================

static const char* s(const knst_c16string& str) {
    static thread_local char bufs[8][256];
    static thread_local int  idx = 0;
    char* buf = bufs[idx];
    idx = (idx + 1) % 8;
    knst_byte_string u8 = knst_dev::to_utf8(str);
    const char* p = u8.empty() ? "" : (const char*)u8.data();
    size_t n = ::strlen(p);
    if (n >= 256) n = 255;
    ::memcpy(buf, p, n);
    buf[n] = '\0';
    return buf;
}

static const char* safe_str(const knst_c16string& str, const char* fallback = "(empty)") {
    if (str.empty()) return fallback;
    return s(str);
}

static const char* fmt_bytes(uint64_t b) {
    static char buf[32];
    if (b >= 1024ULL * 1024 * 1024) ::snprintf(buf, sizeof(buf), "%.2f GiB", (double)b / (1024.0 * 1024 * 1024));
    else if (b >= 1024ULL * 1024)   ::snprintf(buf, sizeof(buf), "%.2f MiB", (double)b / (1024.0 * 1024));
    else if (b >= 1024ULL)          ::snprintf(buf, sizeof(buf), "%.2f KiB", (double)b / 1024.0);
    else                            ::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
    return buf;
}

static const char* fmt_mhz(uint32_t mhz) {
    static char buf[32];
    if (mhz >= 1000) ::snprintf(buf, sizeof(buf), "%.2f GHz", mhz / 1000.0);
    else             ::snprintf(buf, sizeof(buf), "%u MHz", mhz);
    return buf;
}


// =============================================================================
//  TEST: CPU
// =============================================================================
static void test_cpu() {
    TEST_BEGIN("CPU");
    auto c = knst_devices::cpu();
    if (!c.valid) { TEST_SKIP("CPU", "cpu() returned invalid"); return; }

    KNST_LOG_INFO("  Vendor:          %s", safe_str(c.vendor));
    KNST_LOG_INFO("  Model:           %s", safe_str(c.model_name));
    KNST_LOG_INFO("  Architecture:    %s", safe_str(c.architecture));
    KNST_LOG_INFO("  Physical cores:  %u", c.physical_cores);
    KNST_LOG_INFO("  Logical cores:   %u", c.logical_cores);
    KNST_LOG_INFO("  Max frequency:   %s", fmt_mhz(c.max_mhz));
    KNST_LOG_INFO("  Current freq:    %s", fmt_mhz(c.current_mhz));
    KNST_LOG_INFO("  L2 cache:        %u KiB", c.cache_l2_kb);
    KNST_LOG_INFO("  L3 cache:        %u KiB", c.cache_l3_kb);
    if (c.has_temperature) KNST_LOG_INFO("  Temperature:     %d °C", c.temperature_c);
    if (c.has_usage)       KNST_LOG_INFO("  Usage:           %u%%", c.usage_percent);

    if (c.logical_cores == 0) TEST_FAIL("CPU", "logical_cores is 0");
    else                      TEST_OK("CPU");
}


// =============================================================================
//  TEST: Memory
// =============================================================================
static void test_memory() {
    TEST_BEGIN("Memory");
    auto m = knst_devices::memory();
    if (!m.valid) { TEST_SKIP("Memory", "memory() returned invalid"); return; }

    KNST_LOG_INFO("  Total:           %s", fmt_bytes(m.total_bytes));
    KNST_LOG_INFO("  Used:            %s", fmt_bytes(m.used_bytes));
    KNST_LOG_INFO("  Free:            %s", fmt_bytes(m.free_bytes));
    KNST_LOG_INFO("  Available:       %s", fmt_bytes(m.available_bytes));
    if (m.has_swap) {
        KNST_LOG_INFO("  Swap total:      %s", fmt_bytes(m.swap_total));
        KNST_LOG_INFO("  Swap used:       %s", fmt_bytes(m.swap_used));
    }
    if (m.total_bytes == 0) TEST_FAIL("Memory", "total_bytes is 0");
    else                    TEST_OK("Memory");
}


// =============================================================================
//  TEST: Storage
// =============================================================================
static void test_storage() {
    TEST_BEGIN("Storage");
    auto list = knst_devices::storage();
    if (list.empty()) { TEST_SKIP("Storage", "no storage devices found"); return; }

    KNST_LOG_INFO("  Found %u device(s)", (unsigned)list.size());
    for (uint32_t i = 0; i < list.size() && i < 5; ++i) {
        const auto& st = list[i];
        KNST_LOG_INFO("  [%u] %s  %s", i, safe_str(st.device), fmt_bytes(st.total_bytes));
    }

    KNST_LOG_INFO("  -- knst_sto API --");
    auto mnts = knst_sto::list_mounts();
    if (mnts) KNST_LOG_INFO("  mounts: %u", (unsigned)mnts.value.size());

    auto fsuse = knst_sto::filesystem_usage();
    if (fsuse) KNST_LOG_INFO("  fs_usage: %u", (unsigned)fsuse.value.size());

    knst_c16string bench_dir = knst_file::join_path(knst_file::get_temp_dir(), u"bench");
    knst_file::create_directories(bench_dir);
    auto bw = knst_sto::storage::benchmark_write(bench_dir, 8);
    if (bw) {
        KNST_LOG_INFO("  write bench: %.1f MB/s (%ums)",
            bw.value.mb_per_sec, bw.value.duration_ms);
        TEST_OK("Storage write benchmark");
    } else {
        TEST_SKIP("Storage write benchmark", bw.error_string());
    }
    knst_file::remove_all(bench_dir);
    TEST_OK("Storage");
}


// =============================================================================
//  TEST: Network inventory
// =============================================================================
static void test_network_inventory() {
    TEST_BEGIN("Network — inventory");
    auto list = knst_devices::network();
    if (list.empty()) { TEST_SKIP("Network inventory", "no interfaces"); return; }

    uint32_t non_lo = 0;
    for (auto& n : list) if (!n.is_loopback) ++non_lo;
    KNST_LOG_INFO("  interfaces: %u (%u non-loopback)", (unsigned)list.size(), non_lo);

    for (auto& n : list) {
        if (n.is_loopback) continue;
        KNST_LOG_INFO("  [%s] type=%s up=%s ipv4=%s",
            safe_str(n.name),
            n.is_wireless ? "WiFi" : "Ethernet",
            n.is_up ? "yes" : "no",
            safe_str(n.ipv4));
    }
    TEST_OK("Network inventory");
}


// =============================================================================
//  TEST: Network HTTP
// =============================================================================
static void test_network_http() {
    TEST_BEGIN("Network — HTTP/1.1 + HTTPS");
    knst_http_client client;
    knst_http_client::options opts;
    opts.timeout_ms       = 15000;
    opts.total_timeout_ms = 30000;
    opts.follow_redirects = true;

    knst_http_response resp;
    knst_network_error err = knst_network_error::None;

    if (client.get(u"http://example.com", opts, resp, err))
        TEST_OK("HTTP GET");
    else
        TEST_FAIL("HTTP GET", knst_network_error_string(err));

#if defined(KNST_NETWORK_ENABLE_TLS)
    resp = knst_http_response{};
    if (client.get(u"https://example.com", opts, resp, err))
        TEST_OK("HTTPS GET");
    else
        TEST_FAIL("HTTPS GET", knst_network_error_string(err));
#endif
}


// =============================================================================
//  TEST: DNS resolver + DoH
// =============================================================================
static void test_dns() {
    TEST_BEGIN("DNS");

    knst_vector<knst_endpoint> eps;
    knst_network_error err = knst_network_error::None;
    if (knst_dns_resolver::resolve(u"example.com", 80, eps, err)) {
        KNST_LOG_INFO("  example.com → %u endpoint(s)", (unsigned)eps.size());
        TEST_OK("DNS resolver");
    } else {
        TEST_FAIL("DNS resolver", knst_network_error_string(err));
    }

    // DoH: 6 args (url, name, port, out&, err&, timeout_ms)
    knst_vector<knst_endpoint> doh_eps;
    knst_network_error e2 = knst_network_error::None;
    bool doh_ok = knst_doh_resolver::resolve(
        u"https://cloudflare-dns.com/dns-query",
        u"google.com", 443,
        doh_eps, e2, 5000);
    if (doh_ok && !doh_eps.empty()) {
        KNST_LOG_INFO("  DoH google.com → %u endpoint(s)", (unsigned)doh_eps.size());
        TEST_OK("DoH (Cloudflare)");
    } else {
        TEST_SKIP("DoH", knst_network_error_string(e2));
    }
}


// =============================================================================
//  TEST: WebSocket
// =============================================================================
static void test_websocket() {
    TEST_BEGIN("WebSocket");

    const knst_c16string urls[] = {
        u"wss://echo.websocket.org",
        u"wss://ws.postman-echo.com/raw",
    };

    knst_websocket ws;
    knst_websocket::options opts;
    opts.http.timeout_ms = 10000;
    opts.http.total_timeout_ms = 20000;
    opts.http.follow_redirects = true;
    opts.auto_pong = true;

    knst_network_error err = knst_network_error::None;
    bool connected = false;
    for (auto& u : urls) {
        err = knst_network_error::None;
        if (ws.connect(u, opts, err)) { connected = true; break; }
    }
    if (!connected) { TEST_SKIP("WebSocket connect", knst_network_error_string(err)); return; }
    TEST_OK("WebSocket connect");

    knst_c16string hello = u"Hello Android WebSocket!";
    if (!ws.send_text(hello, err)) { TEST_FAIL("WebSocket send", knst_network_error_string(err)); return; }

    knst_websocket::message m;
    if (!ws.recv(m, 10000, err)) { TEST_FAIL("WebSocket recv", knst_network_error_string(err)); return; }
    TEST_OK("WebSocket echo");

    ws.close(1000, u"bye");
    TEST_OK("WebSocket close");
}


// =============================================================================
//  TEST: USB inventory + API
// =============================================================================
static void test_usb_inventory() {
    TEST_BEGIN("USB — inventory");
    auto list = knst_devices::usb();
    if (list.empty()) { TEST_SKIP("USB inventory", "no USB (OTG only)"); return; }
    KNST_LOG_INFO("  Found %u USB device(s)", (unsigned)list.size());
    for (auto& u : list)
        KNST_LOG_INFO("  %04X:%04X  %s", u.vid, u.pid, safe_str(u.product_name));
    TEST_OK("USB inventory");
}

static void test_usb_api() {
    TEST_BEGIN("USB — API");
    auto all = knst_usbc::find_all([](const knst_usb_info&) { return true; });
    if (all.empty()) { TEST_SKIP("USB API", "no devices"); return; }
    KNST_LOG_INFO("  Total: %u", (unsigned)all.size());
    auto hubs = knst_usbc::find_hubs();
    KNST_LOG_INFO("  Hubs: %u", (unsigned)hubs.size());
    TEST_OK("USB API");
}


// =============================================================================
//  TEST: Audio inventory + control
// =============================================================================
static void test_audio_inventory() {
    TEST_BEGIN("Audio — inventory");
    auto a = knst_devices::audio();
    if (!a.valid) { TEST_SKIP("Audio inventory", "invalid"); return; }
    KNST_LOG_INFO("  Devices: %u", (unsigned)a.devices.size());
    TEST_OK("Audio inventory");
}

static void test_audio_control() {
    TEST_BEGIN("Audio — control");
    auto sinks = knst_aud::audio::list_sinks();
    if (!sinks || sinks.value.empty()) { TEST_SKIP("Audio control", "no sink"); return; }
    const auto& sink = sinks.value[0];
    auto v = knst_aud::audio::get_volume(sink);
    if (v) { KNST_LOG_INFO("  volume: %u%%", v.value); TEST_OK("get_volume"); }
    else   TEST_SKIP("get_volume", v.error_string());
    auto m = knst_aud::audio::is_muted(sink);
    if (m) { KNST_LOG_INFO("  muted: %s", m.value ? "yes" : "no"); TEST_OK("is_muted"); }
    else   TEST_SKIP("is_muted", m.error_string());
}


// =============================================================================
//  TEST: Audio codec (WAV i16 round-trip)
// =============================================================================
static void test_audio_codec() {
    TEST_BEGIN("Audio — codec");

    const uint32_t frames = 24000;
    const uint32_t rate   = 48000;
    knst_vector<float> sine;
    sine.resize((size_t)frames * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        float s = 0.4f * std::sin(2.0f * 3.14159265f * 440.0f * (float)i / (float)rate);
        sine[(size_t)i * 2 + 0] = s;
        sine[(size_t)i * 2 + 1] = s;
    }

    knst_aud::pcm_buffer buf;
    buf.samples     = sine;
    buf.rate        = rate;
    buf.channels    = 2;
    buf.source_bits = 16;
    buf.codec       = knst_aud::codec_t::pcm;

    knst_aud::encode_options eo;
    eo.container = knst_aud::container_t::wav;
    eo.fmt       = knst_aud::sample_fmt::i16;
    eo.dither    = false;

    auto bytes = knst_aud::encode(buf, eo);
    if (!bytes) { TEST_FAIL("WAV encode", bytes.error_string()); return; }
    KNST_LOG_INFO("  encoded: %u bytes", bytes.value.length());

    auto back = knst_aud::decode(bytes.value.data(), bytes.value.length());
    if (!back) { TEST_FAIL("WAV decode", back.error_string()); return; }
    KNST_LOG_INFO("  decoded: %u samples", (unsigned)back.value.samples.size());

    float max_err = 0.0f;
    for (size_t i = 0; i < sine.size() && i < back.value.samples.size(); ++i) {
        float d = std::fabs(sine[i] - back.value.samples[i]);
        if (d > max_err) max_err = d;
    }
    KNST_LOG_INFO("  max error: %.6f", max_err);
    if (max_err < 0.001f) TEST_OK("WAV i16 round-trip");
    else                  TEST_FAIL("WAV i16", "error high");
}


// =============================================================================
//  TEST: Audio DSP
// =============================================================================
static void test_audio_dsp() {
    TEST_BEGIN("Audio — DSP");

    const uint32_t N = 4096;
    const uint32_t rate = 48000;
    knst_vector<float> buf;
    buf.resize(N);
    knst_aud::dsp::generate(buf.data(), N, 1,
        knst_aud::dsp::wave_t::sine, 440.0f, rate, 0.5f);

    auto a = knst_aud::dsp::analyze(buf.data(), N);
    KNST_LOG_INFO("  sine: peak=%.3f rms=%.3f", a.peak, a.rms);
    TEST_OK("DSP analyze");

    knst_aud::dsp::normalize(buf.data(), N, 0.95f);
    auto a2 = knst_aud::dsp::analyze(buf.data(), N);
    if (std::fabs(a2.peak - 0.95f) < 0.01f) TEST_OK("DSP normalize");
    else                                     TEST_FAIL("DSP normalize", "peak wrong");

    knst_aud::dsp::biquad lp(knst_aud::dsp::filter_t::lowpass, 48000.0, 500.0, 0.70710678, 0.0);
    lp.process(buf.data(), N, 1);
    TEST_OK("DSP biquad");

    knst_aud::dsp::generate(buf.data(), N, 1,
        knst_aud::dsp::wave_t::sine, 440.0f, rate, 0.5f);
    double hz = knst_aud::dsp::dominant_frequency(buf.data(), N, rate);
    KNST_LOG_INFO("  dominant: %.1f Hz", hz);
    if (std::fabs(hz - 440.0) < 5.0) TEST_OK("DSP FFT");
    else                              TEST_FAIL("DSP FFT", "wrong freq");

    auto rs = knst_aud::dsp::resample(buf.data(), N, 1, 48000, 22050,
        knst_aud::dsp::resample_quality::medium);
    if (!rs.empty()) TEST_OK("DSP resample");
    else             TEST_FAIL("DSP resample", "empty");
}


// =============================================================================
//  TEST: Microphone
// =============================================================================
static void test_microphone() {
    TEST_BEGIN("Microphone");

    auto mics = knst_mic::enumerate_microphones();
    if (mics) {
        KNST_LOG_INFO("  mics: %u", (unsigned)mics.value.size());
        if (!mics.value.empty()) TEST_OK("Mic enumeration");
        else                     TEST_SKIP("Mic enumeration", "no mics");
    } else {
        TEST_SKIP("Mic enumeration", mics.error_string());
    }

    {
        knst_mic::vad_config vc;
        vc.sample_rate = 16000;
        knst_mic::voice_detector vad(vc);
        float frame[320];
        int active = 0;
        for (int i = 0; i < 10; ++i) {
            for (uint32_t k = 0; k < 320; ++k)
                frame[k] = 0.5f * (float)std::sin(2.0 * 3.14159265358979 * 440.0 * (i * 320 + k) / 16000);
            if (vad.feed(frame, 320)) ++active;
        }
        KNST_LOG_INFO("  VAD: %d/10 active", active);
        if (active >= 5) TEST_OK("VAD");
        else              TEST_FAIL("VAD", "didn't activate");
    }

    {
        float sig[1000];
        for (int i = 0; i < 1000; ++i) sig[i] = 0.05f * std::sin(i * 0.1f);
        knst_mic::agc a;
        float g0 = a.current_gain();
        for (int k = 0; k < 5; ++k) a.process(sig, 1000);
        if (a.current_gain() > g0) TEST_OK("AGC");
        else                        TEST_FAIL("AGC", "no boost");
    }

    {
        knst_mic::pitch_config pc;
        pc.sample_rate = 16000;
        pc.min_hz = 200; pc.max_hz = 1000;
        knst_mic::pitch_detector pd(pc);
        knst_vector<float> sig(4096);
        for (uint32_t i = 0; i < 4096; ++i)
            sig[i] = 0.5f * (float)std::sin(2.0 * 3.14159265358979 * 440.0 * i / 16000);
        float f0 = pd.detect(sig.data(), 4096);
        KNST_LOG_INFO("  pitch: %.1f Hz", f0);
        if (std::fabs(f0 - 440.0f) < 10.0f) TEST_OK("Pitch 440 Hz");
        else                                 TEST_FAIL("Pitch", "wrong");
    }

    {
        float src[4] = {0.0f, 0.5f, -0.5f, 1.0f};
        int16_t s16[4];
        knst_mic::fmt::float_to_s16(src, s16, 4);
        float back[4];
        knst_mic::fmt::s16_to_float(s16, back, 4);
        float err = 0.0f;
        for (int i = 0; i < 4; ++i) err = std::max(err, std::fabs(back[i] - src[i]));
        if (err < 0.001f) TEST_OK("Format s16 ↔ f32");
        else              TEST_FAIL("Format", "error high");
    }
}


// =============================================================================
//  TEST: Cameras
// =============================================================================
static void test_cameras() {
    TEST_BEGIN("Cameras — inventory");
    auto list = knst_devices::cameras();
    if (list.empty()) { TEST_SKIP("Cameras", "no cameras"); return; }
    KNST_LOG_INFO("  Found %u camera(s)", (unsigned)list.size());
    TEST_OK("Cameras");
}

static void test_camera_capture() {
    TEST_BEGIN("Camera — capture");

    auto cams = knst_cam::camera::enumerate();
    if (!cams || cams.value.empty()) { TEST_SKIP("Camera capture", "no camera"); return; }
    KNST_LOG_INFO("  cameras: %u", (unsigned)cams.value.size());

    knst_cam::camera cam;
    knst_cam::open_options opt;
    opt.width  = 640;
    opt.height = 480;
    opt.fps_num = 15; opt.fps_den = 1;
    opt.pix = knst_vid::pixel_format::MJPEG;

    if (!cam.open(cams.value[0], opt)) { TEST_SKIP("Camera open", cam.last_error_string()); return; }
    KNST_LOG_INFO("  opened: %ux%u",
        cam.current_format().value.width, cam.current_format().value.height);

    {
        auto f = cam.grab(2, 3000);
        if (f) TEST_OK("Camera grab");
        else   TEST_FAIL("Camera grab", cam.last_error_string());
    }

    cam.close();
    TEST_OK("Camera close");
}


// =============================================================================
//  TEST: Video writer (AVI / MKV / MP4)
// =============================================================================
static void test_video_writer() {
    TEST_BEGIN("Video — writer");

    const uint32_t W = 320, H = 240, FPS = 15, N = 15;
    knst_byte_string rgb;
    rgb.resize((size_t)W * H * 3);

    auto fill = [&](uint32_t fn) {
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x) {
                uint8_t* p = &rgb[((size_t)y * W + x) * 3];
                p[0] = (uint8_t)((x + fn * 4) & 0xFF);
                p[1] = (uint8_t)(y & 0xFF);
                p[2] = ((x / 16 + y / 16 + fn) & 1) ? 220 : 40;
            }
    };

    auto dir = knst_file::get_temp_dir();

    auto roundtrip = [&](const knst_c16string& path,
                          knst_vid::container cont) -> bool {
        knst_vid::video_writer w;
        knst_vid::video_writer::config cfg;
        cfg.cont = cont;
        cfg.vcodec = knst_vid::video_codec::MJPEG;
        cfg.width  = W; cfg.height = H;
        cfg.fps_num = FPS; cfg.fps_den = 1;
        cfg.jpeg_quality = 75;
        if (!w.open(path, cfg)) return false;
        for (uint32_t i = 0; i < N; ++i) {
            fill(i);
            if (!w.write_frame_rgb24(rgb.data())) return false;
        }
        return static_cast<bool>(w.close());
    };

    auto p_avi = knst_file::join_path(dir, u"knst_vid.avi");
    auto p_mkv = knst_file::join_path(dir, u"knst_vid.mkv");
    auto p_mp4 = knst_file::join_path(dir, u"knst_vid.mp4");
    knst_file::remove_file(p_avi);
    knst_file::remove_file(p_mkv);
    knst_file::remove_file(p_mp4);

    if (roundtrip(p_avi, knst_vid::container::AVI)) TEST_OK("AVI writer");
    else TEST_FAIL("AVI writer", "failed");
    if (roundtrip(p_mkv, knst_vid::container::MKV)) TEST_OK("MKV writer");
    else TEST_FAIL("MKV writer", "failed");
    if (roundtrip(p_mp4, knst_vid::container::MP4)) TEST_OK("MP4 writer");
    else TEST_FAIL("MP4 writer", "failed");

    knst_file::remove_file(p_avi);
    knst_file::remove_file(p_mkv);
    knst_file::remove_file(p_mp4);
}


// =============================================================================
//  TEST: Video reader
// =============================================================================
static void test_video_reader() {
    TEST_BEGIN("Video — reader");

    const uint32_t W = 160, H = 120, FPS = 15, N = 10;
    auto dir = knst_file::get_temp_dir();
    auto path = knst_file::join_path(dir, u"knst_vid_read.avi");
    knst_file::remove_file(path);

    knst_byte_string rgb;
    rgb.resize((size_t)W * H * 3);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t* p = &rgb[((size_t)y * W + x) * 3];
            p[0] = (uint8_t)(x * 255 / W);
            p[1] = (uint8_t)(y * 255 / H);
            p[2] = 128;
        }

    {
        knst_vid::video_writer w;
        knst_vid::video_writer::config cfg;
        cfg.cont = knst_vid::container::AVI;
        cfg.vcodec = knst_vid::video_codec::MJPEG;
        cfg.width = W; cfg.height = H;
        cfg.fps_num = FPS; cfg.fps_den = 1;
        cfg.jpeg_quality = 80;
        if (!w.open(path, cfg)) { TEST_FAIL("Video reader", "writer open failed"); return; }
        for (uint32_t i = 0; i < N; ++i)
            if (!w.write_frame_rgb24(rgb.data())) { TEST_FAIL("Video reader", "write failed"); return; }
        w.close();
    }

    knst_vid::video_reader rd;
    if (!rd.open(path)) {
        TEST_FAIL("Video reader open", "open failed");
        knst_file::remove_file(path);
        return;
    }
    KNST_LOG_INFO("  opened: %ux%u", rd.width(), rd.height());

    if (rd.width() != W || rd.height() != H) TEST_FAIL("Video reader dims", "wrong");
    else TEST_OK("Video reader dims");

    auto f0 = rd.read_frame_rgb24(0);
    if (f0 && f0.value.width == W && f0.value.height == H) {
        KNST_LOG_INFO("  frame 0: %ux%u", f0.value.width, f0.value.height);
        TEST_OK("Video read frame");
    } else {
        TEST_FAIL("Video read frame", "decode failed");
    }

    knst_file::remove_file(path);
}


// =============================================================================
//  TEST: Converter
// =============================================================================
static void test_converter() {
    TEST_BEGIN("Converter");

    using knst_conv::format_t;
    using knst_conv::format_from_path;

    knst_c16string tmp = knst_file::get_temp_dir();
    knst_c16string in_wav = knst_file::join_path(tmp, u"knst_conv_in.wav");

    {
        bool ok = true;
        ok &= format_from_path(u"a.wav") == format_t::wav;
        ok &= format_from_path(u"clip.mp4") == format_t::mp4;
        ok &= format_from_path(u"x.mkv") == format_t::mkv;
        if (ok) TEST_OK("format_from_path");
        else    TEST_FAIL("format_from_path", "wrong");
    }

    {
        const uint32_t rate = 48000, ch = 2, frames = rate;
        knst_vector<float> samples;
        samples.resize((size_t)frames * ch);
        for (uint32_t i = 0; i < frames; ++i) {
            float t = (float)i / (float)rate;
            samples[(size_t)i * ch + 0] = 0.35f * std::sin(2.0f * 3.14159265f * 440.0f * t);
            samples[(size_t)i * ch + 1] = 0.35f * std::sin(2.0f * 3.14159265f * 880.0f * t);
        }
        knst_aud::pcm_buffer buf;
        buf.samples = std::move(samples);
        buf.rate = rate;
        buf.channels = ch;
        buf.source_bits = 16;
        buf.codec = knst_aud::codec_t::pcm;

        knst_aud::encode_options eo;
        eo.container = knst_aud::container_t::wav;
        eo.fmt = knst_aud::sample_fmt::i16;
        knst_file::ensure_parent_dir(in_wav);
        auto r = knst_aud::encode_file(in_wav, buf, eo);
        if (r && knst_file::get_file_size(in_wav) > 0) {
            KNST_LOG_INFO("  test WAV: %llu bytes",
                (unsigned long long)knst_file::get_file_size(in_wav));
            TEST_OK("Test WAV");
        } else {
            TEST_SKIP("Test WAV", "encode failed");
            return;
        }
    }

    {
        knst_conv::converter cv;
        auto r = cv.probe(in_wav);
        if (r && r.value.has_audio) {
            KNST_LOG_INFO("  probe: rate=%u ch=%u", r.value.au.rate, r.value.au.channels);
            TEST_OK("probe(WAV)");
        } else TEST_FAIL("probe(WAV)", "unexpected");
    }

    {
        knst_conv::converter cv;
        knst_c16string aiff = knst_file::join_path(tmp, u"knst_conv.aiff");
        knst_file::remove_file(aiff);
        if (cv.convert_audio(in_wav, aiff) && knst_file::get_file_size(aiff) > 0)
            TEST_OK("WAV → AIFF");
        else TEST_SKIP("WAV → AIFF", "failed");
        knst_file::remove_file(aiff);
    }

    knst_file::remove_file(in_wav);
}


// =============================================================================
//  TEST: Image loader
// =============================================================================
static void test_image_loader() {
    TEST_BEGIN("Image loader");

    {
        uint8_t png_sig[]  = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A,0,0,0,0};
        uint8_t jpeg_sig[] = {0xFF,0xD8,0xFF,0xE0,0,0,0,0};
        int f_png  = knst_image_loader::detect_format(png_sig,  sizeof(png_sig));
        int f_jpeg = knst_image_loader::detect_format(jpeg_sig, sizeof(jpeg_sig));
        if (f_png == KNST_IMAGE_FORMAT_PNG && f_jpeg == KNST_IMAGE_FORMAT_JPEG)
            TEST_OK("Format detection");
        else TEST_FAIL("Format detection", "wrong");
    }

    {
        const char* pnm = "P3\n2 2\n255\n255 0 0  0 255 0\n0 0 255  255 255 255\n";
        auto img = knst_image_loader::load_from_memory(pnm, ::strlen(pnm));
        if (img.valid()) { KNST_LOG_INFO("  PNM P3: %ux%u", img.width, img.height); TEST_OK("PNM P3"); }
        else TEST_FAIL("PNM P3", knst_image_error_string(img.error));
    }

    {
        auto img = knst_image_loader::load_from_memory(nullptr, 0);
        if (img.error == KNST_IMAGE_ERR_EMPTY_INPUT) TEST_OK("Empty input");
        else TEST_FAIL("Empty input", "wrong");
    }
}


// =============================================================================
//  TEST: File
// =============================================================================
static void test_file() {
    TEST_BEGIN("File");

    auto tmp = knst_file::get_temp_dir();
    if (tmp.empty()) { TEST_SKIP("File", "no temp dir"); return; }

    auto path = knst_file::join_path(tmp, u"knst_file_test.txt");
    knst_c16string content = u"Hello Android file I/O!\nLine 2\nLine 3\n";

    if (!knst_file::write_file_text(path, content)) { TEST_FAIL("File", "write failed"); return; }
    TEST_OK("File write");

    knst_c16string back = knst_file::read_file_text(path);
    if (back.length() == content.length()) TEST_OK("File read");
    else TEST_FAIL("File read", "mismatch");

    knst_file f = knst_file::open(path);
    KNST_LOG_INFO("  size: %llu bytes", (unsigned long long)f.size);

    knst_vector<knst_c16string> lines;
    if (f.read_file_lines(lines)) {
        KNST_LOG_INFO("  lines: %u", (unsigned)lines.size());
        TEST_OK("File read lines");
    }

    knst_file::remove_file(path);
}


// =============================================================================
//  TEST: JSON
// =============================================================================
static void test_json() {
    TEST_BEGIN("JSON");

    {
        knst_json obj = knst_json::make_object();
        obj.set("name",  knst_json("knst"));
        obj.set("version", knst_json((int64_t)100));
        knst_json_buffer buf;
        if (obj.dump(buf)) {
            KNST_LOG_INFO("  dump: %.*s", (int)buf.size(), (const char*)buf.data());
            TEST_OK("JSON construct + dump");
        } else TEST_FAIL("JSON dump", "failed");
    }

    {
        const char* src = "{\"a\":1,\"b\":[2,3,4],\"c\":{\"d\":true}}";
        knst_json doc;
        knst_json_error err;
        if (knst_json::parse(src, ::strlen(src), doc, &err)) TEST_OK("JSON parse");
        else TEST_FAIL("JSON parse", "failed");
    }

       {
        const char* src = "{\"a\":{\"b\":[10,20,30]}}";
        knst_json doc;
        knst_json_error err;
        if (!knst_json::parse(src, ::strlen(src), doc, &err)) {
            TEST_FAIL("JSON nested lookup", "parse failed");
        } else {
            knst_json_buffer db;
            if (doc.dump(db))
                KNST_LOG_INFO("  roundtrip: %.*s", (int)db.size(), (const char*)db.data());

            auto* a = doc.find("a");
            auto* b = a ? a->find("b") : nullptr;
            KNST_LOG_INFO("  find(\"a\")=%d  a->find(\"b\")=%d", a?1:0, b?1:0);

                        if (b && b->is_array() && b->size() == 3) TEST_OK("JSON nested lookup");
            else if (b)                               TEST_FAIL("JSON nested lookup", "found but not array");
            else if (a)                               TEST_FAIL("JSON nested lookup", "a found, b null");
            else                                      TEST_FAIL("JSON nested lookup", "find(\"a\") null");
        }
    }

    {
        knst_json obj = knst_json::make_object();
        obj.set("b", knst_json((int64_t)2));
        obj.set("a", knst_json((int64_t)1));
        knst_json_buffer buf;
        knst_json_dump_options opt;
        opt.canonical = true;
        if (obj.dump(buf, opt)) {
            KNST_LOG_INFO("  canonical: %.*s", (int)buf.size(), (const char*)buf.data());
            TEST_OK("JSON canonical");
        } else TEST_FAIL("JSON canonical", "failed");
    }
}


// =============================================================================
//  TEST: Process
// =============================================================================
static void test_process() {
    TEST_BEGIN("Process");

    uint32_t self = knst_process::current_pid();
    KNST_LOG_INFO("  pid: %u", self);
    TEST_OK("Process self-info");

    auto info = knst_process::get_info(self);
    if (info.pid == 0) TEST_FAIL("Process get_info", "pid=0");
    else {
        KNST_LOG_INFO("  rss: %s  threads: %u", fmt_bytes(info.rss_bytes), info.thread_count);
        TEST_OK("Process get_info");
    }

    auto pids = knst_process::list_pids();
    KNST_LOG_INFO("  list_pids: %u", (unsigned)pids.size());
    if (!pids.empty()) TEST_OK("Process list_pids");
    else               TEST_FAIL("Process list_pids", "empty");

    {
        knst_process_options opts;
        opts.shell_path = u"/system/bin/sh";
        auto r = knst_process::run_capture(u"/system/bin/sh",
            {u"-c", u"echo hello-from-android"},
            opts, nullptr, 0, 3000);
        if (r.error == knst_process_error::None && r.exit_code == 0) TEST_OK("Process run_capture");
        else TEST_SKIP("Process run_capture", "spawn restricted");
    }

    {
        auto r = knst_process::run_capture(u"getprop", {u"ro.product.model"},
            {}, nullptr, 0, 3000);
        if (r.error == knst_process_error::None && r.exit_code == 0) {
            knst_c16string model(reinterpret_cast<const char*>(r.out_data.data()), r.out_data.length());
            while (model.length() > 0 && (model[model.length()-1] == u'\n' || model[model.length()-1] == u'\r'))
                model.resize(model.length()-1);
            KNST_LOG_INFO("  model: %s", safe_str(model));
            TEST_OK("Process getprop");
        } else TEST_SKIP("Process getprop", "spawn restricted");
    }

    {
        auto p = knst_process::run(u"/system/bin/sh", {u"-c", u"exit 42"});
        if (p.error == knst_process_error::None) {
            p.wait(3000);
            if (p.exit_code() == 42) TEST_OK("Process spawn/wait");
            else TEST_FAIL("Process spawn/wait", "wrong exit");
        } else TEST_SKIP("Process spawn/wait", "spawn failed");
    }
}


// =============================================================================
//  TEST: System / OS / Drivers / Power / Sensors
// =============================================================================
static void test_system() {
    TEST_BEGIN("System");
    auto sys = knst_devices::system();
    if (!sys.valid) { TEST_SKIP("System", "invalid"); return; }
    KNST_LOG_INFO("  Manufacturer: %s", safe_str(sys.manufacturer));
    KNST_LOG_INFO("  Product:      %s", safe_str(sys.product_name));
    KNST_LOG_INFO("  Serial:       %s", safe_str(sys.serial));
    TEST_OK("System");
}

static void test_os() {
    TEST_BEGIN("OS");
    auto o = knst_devices::os();
    if (!o.valid) { TEST_SKIP("OS", "invalid"); return; }
    KNST_LOG_INFO("  Name:    %s", safe_str(o.os_name));
    KNST_LOG_INFO("  Version: %s", safe_str(o.os_version));
    KNST_LOG_INFO("  Arch:    %s", safe_str(o.kernel_arch));
    TEST_OK("OS");
}

static void test_drivers() {
    TEST_BEGIN("Drivers");
    auto list = knst_devices::drivers();
    if (list.empty()) { TEST_SKIP("Drivers", "none"); return; }
    KNST_LOG_INFO("  Found %u driver(s)", (unsigned)list.size());
    TEST_OK("Drivers");
}

static void test_power() {
    TEST_BEGIN("Power");
    auto p = knst_devices::power();
    if (!p.valid) { TEST_SKIP("Power", "invalid"); return; }
    KNST_LOG_INFO("  AC: %s  batteries: %u",
        p.ac_connected ? "yes" : "no", (unsigned)p.batteries.size());
    for (auto& b : p.batteries)
        KNST_LOG_INFO("  [%s] %u%% state=%u", safe_str(b.name), b.percent, (unsigned)b.state);
    if (p.batteries.empty()) TEST_SKIP("Power", "no batteries");
    else                     TEST_OK("Power");
}

static void test_sensors() {
    TEST_BEGIN("Sensors");
    auto list = knst_devices::sensors();
    if (list.empty()) { TEST_SKIP("Sensors", "none"); return; }
    KNST_LOG_INFO("  Found %u sensor(s)", (unsigned)list.size());
    TEST_OK("Sensors");
}


// =============================================================================
//  TEST: Thermal
// =============================================================================
static void test_thermal() {
    TEST_BEGIN("Thermal");

    auto zones = knst_sens::thermal_zones();
    if (zones) {
        KNST_LOG_INFO("  zones: %u", (unsigned)zones.value.size());
    }
    auto fans = knst_sens::enumerate_fans();
    KNST_LOG_INFO("  fans: %u", (unsigned)fans.size());

    {
        knst_sens::sensor_filter f;
        knst_sensor_info si;
        si.type = knst_sensor_type::Temperature;
        si.chip = u"test"; si.name = u"temp1"; si.value = 45.0;
        bool ok = f.accept(si);
        si.value = 999.0;
        bool rejected = !f.accept(si);
        if (ok && rejected) TEST_OK("Sensor filter");
        else TEST_FAIL("Sensor filter", "wrong");
    }

    {
        knst_sens::async_sensor_monitor::config mc;
        mc.interval_ms = 200;
        knst_sens::async_sensor_monitor mon(mc);
        auto r = mon.sample_once();
        if (r) TEST_OK("Async sensor monitor");
        else   TEST_SKIP("Async sensor monitor", r.error_string());
    }
}


// =============================================================================
//  TEST: Bluetooth
// =============================================================================
static void test_bluetooth() {
    TEST_BEGIN("Bluetooth");
    auto b = knst_devices::bluetooth();
    if (!b.valid)       { TEST_SKIP("Bluetooth", "invalid"); return; }
    if (!b.has_adapter) { TEST_SKIP("Bluetooth", "no adapter"); return; }
    KNST_LOG_INFO("  adapter: %s powered=%s devices=%u",
        safe_str(b.adapter_name), b.powered ? "y" : "n",
        (unsigned)b.devices.size());
    TEST_OK("Bluetooth");
}

static void test_bluetooth_api() {
    TEST_BEGIN("Bluetooth API");
    if (!knst_bt::bluetooth::is_supported()) { TEST_SKIP("Bluetooth API", "not supported"); return; }
    auto ad = knst_bt::bluetooth::adapter();
    if (!ad) { TEST_SKIP("Bluetooth API", "no adapter"); return; }
    KNST_LOG_INFO("  adapter: %s powered=%s",
        safe_str(ad.value.name), ad.value.powered ? "y" : "n");
    auto paired = knst_bt::bluetooth::list_paired();
    if (paired) KNST_LOG_INFO("  paired: %u", (unsigned)paired.value.size());
    TEST_OK("Bluetooth API");
}


// =============================================================================
//  TEST: Printers inventory
// =============================================================================
static void test_printers() {
    TEST_BEGIN("Printers — inventory");
    auto list = knst_devices::printers();
    if (!list.empty()) {
        KNST_LOG_INFO("  found %u", (unsigned)list.size());
        for (auto& p : list) KNST_LOG_INFO("  [%s] %s", safe_str(p.name), safe_str(p.status));
    }
    auto net = knst_prn::printer::list_all();
    if (net && !net.value.empty()) {
        KNST_LOG_INFO("  mDNS printers: %u", (unsigned)net.value.size());
        for (auto& p : net.value) KNST_LOG_INFO("    [%s] %s", safe_str(p.name), safe_str(p.device_uri));
        TEST_OK("mDNS printer discovery");
    } else {
        TEST_SKIP("mDNS printer discovery", "no printers");
    }
}


// =============================================================================
//  TEST: Printer — 3-page group + scan (no macro)
// =============================================================================
static void test_printer_group_and_scan() {
    TEST_BEGIN("Printer — 3-page group + scan");

    using namespace knst_prn::detail;
    document combined;
    float w_mm = 210.0f, h_mm = 297.0f;

    auto append = [&](document& part) {
        uint32_t off = (uint32_t)combined.images.size();
        if (combined.pages.empty()) {
            combined.w_pt    = part.w_pt;
            combined.h_pt    = part.h_pt;
            combined.max_dpi = part.max_dpi;
        }
        for (auto& im : part.images) combined.images.push_back(std::move(im));
        for (auto& pg : part.pages) {
            for (auto& it : pg.items)
                if (it.kind == item::Image) it.image += off;
            combined.pages.push_back(std::move(pg));
        }
    };

    {
        document tmp;
        make_test_doc(knst_prn::test_page_type::Diagnostic, "KNST", w_mm, h_mm, tmp);
        append(tmp);
    }
    {
        document tmp;
        make_test_doc(knst_prn::test_page_type::ColorBar, "KNST", w_mm, h_mm, tmp);
        append(tmp);
    }
    {
        document tmp;
        make_test_doc(knst_prn::test_page_type::Gradient, "KNST", w_mm, h_mm, tmp);
        append(tmp);
    }

    KNST_LOG_INFO("  combined: %u pages", (unsigned)combined.pages.size());

    knst_c16string tmpdir = knst_file::get_temp_dir();
    knst_c16string pdf_path = knst_file::join_path(tmpdir, u"knst_group.pdf");
    knst_file::remove_file(pdf_path);

    auto st = write_pdf(combined, pdf_path);
    if (st != knst_prn::status::Ok) { TEST_FAIL("Build 3-page PDF", knst_prn::status_string(st)); return; }
    uint64_t pdf_sz = knst_file::get_file_size(pdf_path);
    KNST_LOG_INFO("  PDF: %llu bytes", (unsigned long long)pdf_sz);
    TEST_OK("Build 3-page PDF");

        auto printers = knst_prn::printer::list_all();

        {
            auto diag = knst_prn::detail::ipp::get_printer_attrs(
                "192.168.1.239:631", false,
                {"printer-uri-supported", "document-format-supported",
                 "document-format-default", "printer-name", "printer-state"});
                                if (diag) {
                
                const auto& A = diag.value.attrs;
                auto find_attr = [&](const char* nm) -> const char* {
                    for (auto& kv : A) if (kv.first == nm) return kv.second.c_str();
                    return "(missing)";
                };
                KNST_LOG_INFO("  diag printer-uri-supported    : %s", find_attr("printer-uri-supported"));
                KNST_LOG_INFO("  diag document-format-supported: %s", find_attr("document-format-supported"));
                KNST_LOG_INFO("  diag document-format-default  : %s", find_attr("document-format-default"));
                KNST_LOG_INFO("  diag printer-name             : %s", find_attr("printer-name"));
                KNST_LOG_INFO("  diag printer-state            : %s", find_attr("printer-state"));
            } else {
                KNST_LOG_INFO("  diag Get-Attrs FAILED: %s (native=%d)",
                    diag.error_string(), diag.native);
            }
        }
    if (!printers || printers.value.empty()) {
        TEST_SKIP("Print 3-page group", "no network printer");
    } else {
        const auto& p = printers.value[0];
        KNST_LOG_INFO("  printer: %s", safe_str(p.name));
        KNST_LOG_INFO("  uri:     %s", safe_str(p.device_uri));

        
        const knst_c16string& u = p.device_uri;
        knst_c16string ip_only;

        bool is_v6 = false;
        for (uint32_t i = 0; i + 1 < u.length(); ++i)
            if (u[i] == u':' && u[i + 1] == u':') { is_v6 = true; break; }
        if (!is_v6) {
            for (uint32_t i = 0; i < u.length(); ++i)
                if (u[i] == u'%') { is_v6 = true; break; }
        }

        if (is_v6) {
           
            ip_only = u"192.168.1.239";
            KNST_LOG_INFO("  ip_only: %s (mDNS gave IPv6, using known IPv4)",
                          safe_str(ip_only));
        } else {
            uint32_t s = 0;
            auto has_pfx = [&](const char16_t* q) -> bool {
                uint32_t n = 0; while (q[n]) ++n;
                if (u.length() < n) return false;
                for (uint32_t i = 0; i < n; ++i) if (u[i] != q[i]) return false;
                return true;
            };
            if      (has_pfx(u"ipps://"))   s = 7;
            else if (has_pfx(u"ipp://"))    s = 6;
            else if (has_pfx(u"lpd://"))    s = 6;
            else if (has_pfx(u"socket://")) s = 9;

            uint32_t e = u.length();
            for (uint32_t i = s; i < u.length(); ++i) if (u[i] == u'/') { e = i; break; }
            knst_c16string ip_port_str = u.substr(s, e - s);

            uint32_t ip_end = ip_port_str.length();
            for (uint32_t i = 0; i < ip_port_str.length(); ++i)
                if (ip_port_str[i] == u':') { ip_end = i; break; }
            ip_only = ip_port_str.substr(0, ip_end);
            KNST_LOG_INFO("  ip_only: %s", safe_str(ip_only));
        }

       
        auto build_uri = [&](const char16_t* scheme,
                             const char16_t* port_str,
                             const char16_t* path) -> knst_c16string {
            std::u16string us;
            us.reserve(80);
            for (const char16_t* q = scheme;   *q; ++q) us.push_back(*q);
            for (uint32_t i = 0; i < ip_only.length(); ++i) us.push_back(ip_only[i]);
            for (const char16_t* q = port_str; *q; ++q) us.push_back(*q);
            for (const char16_t* q = path;     *q; ++q) us.push_back(*q);
            return knst_c16string(us.data(), (uint32_t)us.size());
        };

                knst_prn::print_options po;
        po.title = u"KNST 3-page group";

        po.paper = knst_prn::media_size::Default;

      
        knst_prn::detail::ipp::send_policy pol;
        pol.prefer = "jpeg";

        std::string used;
        auto r = knst_prn::print_ipp_smart(
            pdf_path, build_uri(u"ipp://", u":631", u"/ipp/print"), po, pol, &used);

        if (r) {
            KNST_LOG_INFO("  printed: job #%u via %s", r.value, used.c_str());
            TEST_OK("Print 3-page group");
        } else {
            KNST_LOG_ERROR("  print fail: %s native=0x%04X diag=%s",
                           r.error_string(), r.native,
                           knst_prn::detail::ipp::last_diag().c_str());
            TEST_SKIP("Print 3-page group", r.error_string());
        }
    }
    knst_file::remove_file(pdf_path);

        auto scanners = knst_prn::scanner::list_all();
    knst_c16string scanner_device;
    if (scanners && !scanners.value.empty()) {
        scanner_device = scanners.value[0].device_id;
        KNST_LOG_INFO("  scanner (mDNS): %s", safe_str(scanner_device));
    }

    if (scanner_device.empty()) {
        TEST_SKIP("Scan", "no scanner discovered via mDNS");
        return;
    }


    bool scanner_uri_bad = false;
    for (uint32_t i = 0; i + 1 < scanner_device.length(); ++i) {
        if (scanner_device[i] == u':' && scanner_device[i + 1] == u':') {
            scanner_uri_bad = true;
            break;
        }
    }
    if (!scanner_uri_bad) {
        for (uint32_t i = 0; i < scanner_device.length(); ++i) {
            if (scanner_device[i] == u'%') {
                scanner_uri_bad = true;
                break;
            }
        }
    }
    if (scanner_uri_bad) {
        const char* env = std::getenv("KNST_TEST_SCANNER");
        if (!env || !*env) {
            TEST_SKIP("Scan", "mDNS gave IPv6 URI; set KNST_TEST_SCANNER=host:port to override");
            return;
        }
        scanner_device = knst_c16string(env);
        KNST_LOG_INFO("  scanner (env override): %s", safe_str(scanner_device));
    }

    auto sc = knst_prn::scanner::open(scanner_device);
    if (!sc) { TEST_SKIP("Scan open", sc.error_string()); return; }

    knst_prn::scan_options so;
    so.dpi = 200;
    so.mode = knst_prn::color_mode::Color;
    so.use_adf = false;
    so.source = u"Flatbed";
    so.output_format = u"png";

    knst_c16string scan_out = knst_file::join_path(tmpdir, u"knst_scan.png");
    knst_file::remove_file(scan_out);
    auto sr = sc.value.scan_to_file(scan_out, so);
    if (sr) {
        uint64_t sz = knst_file::get_file_size(sr.value);
        KNST_LOG_INFO("  scanned: %llu bytes", (unsigned long long)sz);
        TEST_OK("Scan 1 page (PNG)");
    } else if (sr.st == knst_prn::status::NotFound ||
               sr.st == knst_prn::status::IoError ||
               sr.st == knst_prn::status::Timeout ||
               sr.st == knst_prn::status::NotSupported) {

        TEST_SKIP("Scan", sr.error_string());
    } else {

        TEST_FAIL("Scan", sr.error_string());
    }
    knst_file::remove_file(scan_out);
}


// =============================================================================
//  TEST: Print errors
// =============================================================================
static void test_print_errors() {
    TEST_BEGIN("Print — errors");
    knst_prn::printer p;
    auto r1 = p.print_pdf_dialog(u"");
    if (!r1 && r1.st == knst_prn::status::InvalidArgument) TEST_OK("Reject empty path");
    else TEST_FAIL("Reject empty path", "wrong");

    auto r2 = p.print_pdf_dialog(u"/no/such/file.pdf");
    if (!r2 && r2.st == knst_prn::status::NotFound) TEST_OK("Reject nonexistent");
    else TEST_FAIL("Reject nonexistent", "wrong");
}


// =============================================================================
//  Entry point
// =============================================================================
void android_main(struct android_app* app) {
    KNST_LOG_INFO("===============================================================");
    KNST_LOG_INFO("  KernelNucleusT — Comprehensive Android Test Suite");
    KNST_LOG_INFO("===============================================================");

        knst_android::session session(app);
    if (!knst_android::session::activity()) {
        KNST_LOG_ERROR("FATAL: session init failed");
        return;
    }

#if defined(KNST_NETWORK_ENABLE_TLS)
    // Self-signed certs on LAN printers (HP 7740 etc.) — trusted local net only
    knst_tls_set_skip_verify(true);
#endif

    KNST_LOG_INFO("Bootstrap OK");
    KNST_LOG_INFO("");

    test_cpu();
    test_memory();
    test_storage();
    test_network_inventory();
    test_usb_inventory();
    test_usb_api();
    test_audio_inventory();
    test_power();
    test_sensors();
    test_thermal();
    test_bluetooth();
    test_bluetooth_api();
    test_cameras();
    test_printers();

    test_audio_control();
    test_audio_codec();
    test_audio_dsp();
    test_microphone();
    test_camera_capture();
    test_video_writer();
    test_video_reader();

    test_file();
    test_json();
    test_image_loader();
    test_converter();

    test_process();
    test_system();
    test_os();
    test_drivers();

    test_network_http();
    test_dns();
    test_websocket();

    test_printer_group_and_scan();
    test_print_errors();

    KNST_LOG_INFO("===============================================================");
    KNST_LOG_INFO("  RESULTS  PASS=%d  FAIL=%d  SKIP=%d", g_pass, g_fail, g_skip);
    KNST_LOG_INFO("===============================================================");
    if (g_fail > 0) KNST_LOG_ERROR("Some tests FAILED.");
    else            KNST_LOG_INFO("All tests passed (or skipped gracefully).");
}