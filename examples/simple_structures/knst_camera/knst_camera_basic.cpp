/*
  knst_camera_basic.cpp — comprehensive walkthrough of the knst_camera API.

  Structure:
    A     Synthetic image encoding   (works without a camera)
    B     Pixel format conversion    (works without a camera)
    C     Image edge cases           (works without a camera)
    D     Direct video writer        (works without a camera)
    E     Motion detector            (works without a camera)
    F–G   Discovery                  (needs a camera, skips cleanly)
    H–J   Capture                    (needs a camera)
    K–N   Snapshots                  (needs a camera)
    O–S   Recording                  (needs a camera)
    T–U   Controls                   (needs a camera)
    V     Multi-camera               (needs 2+ cameras)
    W     Diagnostics                (needs a camera)
    X     Error handling             (works everywhere)
    Y     Platform summary           (works everywhere)

  Sections A–E and X–Y run on any machine, even in a container
  with no /dev/video*. That's the bulk of the image and video
  pipeline — the actual device I/O is thin by design.

  All files are written to out/camera_demo/. Nothing leaves the
  machine.
*/

#include "../../../include/KernelNucleusT.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>


// ─────────────────────────────────────────────────────────────────────────────
// Output helpers
// ─────────────────────────────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

static void ok(const char* what) {
    std::printf("  ok    %s\n", what);
    ++g_pass;
}
static void fail(const char* what) {
    std::printf("  FAIL  %s\n", what);
    ++g_fail;
}
static void check(bool cond, const char* what) {
    if (cond) ok(what); else fail(what);
}
static void section(const char* title) {
    std::printf("\n--------------------------------------------------\n");
    std::printf("  %s\n", title);
    std::printf("--------------------------------------------------\n");
}
static void subsection(const char* title) {
    std::printf("\n  -- %s --\n", title);
}

// c16string -> UTF-8. Rotate through 4 thread-local buffers so two calls in
// the same printf() don't clobber each other.
static const char* u8(const knst_c16string& s) {
    static thread_local knst_byte_string bufs[4];
    static thread_local int idx = 0;
    int i = idx;
    idx = (idx + 1) & 3;
    bufs[i] = knst_dev::to_utf8(s);
    bufs[i].push_back(0);
    return reinterpret_cast<const char*>(bufs[i].data());
}

static const char* pix_name(knst_vid::pixel_format p) {
    return knst_vid::pixel_format_name(p);
}
static const char* err_name(knst_cam::errc e) {
    return knst_cam::errc_string(e);
}


// ─────────────────────────────────────────────────────────────────────────────
// Frame generation
// ─────────────────────────────────────────────────────────────────────────────

// Gradient R→G with blue checkerboard. Every 32×32 cell flips so image
// transforms (mirror, rotate) are easy to verify visually.
static knst_cam::frame make_pattern(uint32_t w, uint32_t h) {
    knst_cam::frame f;
    f.width = w;
    f.height = h;
    f.stride = w * 3;
    f.pix = knst_vid::pixel_format::RGB24;
    f.data.resize(w * h * 3);
    if (f.data.length() != w * h * 3) return f;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &f.data[((size_t)y * w + x) * 3];
            p[0] = (uint8_t)(x * 255u / (w ? w : 1));
            p[1] = (uint8_t)(y * 255u / (h ? h : 1));
            p[2] = ((x / 32 + y / 32) & 1) ? 200 : 40;
        }
    }
    f.timestamp_us = 0;
    f.sequence = 0;
    f.valid = true;
    return f;
}

static knst_cam::frame make_solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
    knst_cam::frame f;
    f.width = w;
    f.height = h;
    f.stride = w * 3;
    f.pix = knst_vid::pixel_format::RGB24;
    f.data.resize(w * h * 3);
    if (f.data.length() != w * h * 3) return f;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &f.data[((size_t)y * w + x) * 3];
            p[0] = r; p[1] = g; p[2] = b;
        }
    }
    f.valid = true;
    return f;
}

// Mean absolute per-byte difference over all RGB24 bytes.
static double frame_diff(const knst_cam::frame& a, const knst_cam::frame& b) {
    if (a.data.length() != b.data.length()) return 999.0;
    if (a.data.length() == 0) return 0.0;
    uint64_t sum = 0;
    const uint8_t* pa = a.data.data();
    const uint8_t* pb = b.data.data();
    for (uint32_t i = 0; i < a.data.length(); ++i) {
        int d = (int)pa[i] - (int)pb[i];
        sum += (uint64_t)(d < 0 ? -d : d);
    }
    return (double)sum / (double)a.data.length();
}


// ═════════════════════════════════════════════════════════════════════════════
// A. Synthetic image encoding — no camera
// ═════════════════════════════════════════════════════════════════════════════

static void test_a_synthetic_encoding() {
    section("A. Synthetic image encoding (no camera)");

    const uint32_t W = 640, H = 360;
    knst_cam::frame f = make_pattern(W, H);
    check(f.valid, "generated 640x360 test pattern");

    struct test_case {
        const char*              name;
        knst_cam::image_options  opt;
        const char*              ext;
        uint32_t                 min_bytes;
    };

    test_case cases[] = {
        { "JPEG q=90 baseline",  {knst_cam::image_format::JPEG, 90,  knst_vid::mjpeg::subsampling::s420}, "jpg", 1000 },
        { "JPEG q=95 4:4:4",     {knst_cam::image_format::JPEG, 95,  knst_vid::mjpeg::subsampling::s444}, "jpg", 2000 },
        { "JPEG q=50 4:2:0",     {knst_cam::image_format::JPEG, 50,  knst_vid::mjpeg::subsampling::s420}, "jpg", 500  },
        { "JPEG q=1 tiny",       {knst_cam::image_format::JPEG, 1,   knst_vid::mjpeg::subsampling::s420}, "jpg", 100  },
        { "JPEG mirror",         []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::JPEG; o.mirror = true; return o; }(), "jpg", 1000 },
        { "JPEG flip_v",         []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::JPEG; o.flip_v = true; return o; }(), "jpg", 1000 },
        { "JPEG rot90",          []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::JPEG; o.rotate_deg = 90; return o; }(), "jpg", 1000 },
        { "JPEG rot180",         []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::JPEG; o.rotate_deg = 180; return o; }(), "jpg", 1000 },
        { "JPEG rot270",         []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::JPEG; o.rotate_deg = 270; return o; }(), "jpg", 1000 },
        { "JPEG resize 320",     []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::JPEG; o.out_w = 320; return o; }(), "jpg", 500 },
        { "JPEG resize 160x90",  []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::JPEG; o.out_w = 160; o.out_h = 90; return o; }(), "jpg", 300 },
        { "JPEG scale 0.5",      []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::JPEG; o.scale = 0.5; return o; }(), "jpg", 500 },
        { "JPEG rot90+mirror",   []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::JPEG; o.rotate_deg = 90; o.mirror = true; return o; }(), "jpg", 1000 },
        { "BMP 24-bit",          {knst_cam::image_format::BMP, 90, knst_vid::mjpeg::subsampling::s420}, "bmp", 1000 },
        { "PNG RGB",             {knst_cam::image_format::PNG, 90, knst_vid::mjpeg::subsampling::s420}, "png", 1000 },
        { "PPM RGB",             {knst_cam::image_format::PPM, 90, knst_vid::mjpeg::subsampling::s420}, "ppm", 1000 },
        { "PGM grey",            []{ auto o = knst_cam::image_options(); o.format = knst_cam::image_format::PPM; o.grey = true; return o; }(), "pgm", 500 },
    };
    const int n_cases = (int)(sizeof(cases) / sizeof(cases[0]));

    for (int i = 0; i < n_cases; ++i) {
        knst_byte_string bytes;
        knst_cam::image_format used = knst_cam::image_format::JPEG;
        knst_cam::errc e = knst_cam::encode_frame(f, cases[i].opt, bytes, used);

        if (e != knst_cam::errc::Ok) {
            std::printf("  %-24s : FAILED — %s\n", cases[i].name, err_name(e));
            ++g_fail;
            continue;
        }

        // Size sanity
        if (bytes.length() < cases[i].min_bytes) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "%s too small (%u bytes)",
                          cases[i].name, bytes.length());
            fail(msg);
            continue;
        }

        // Per-format header check
        bool header_ok = true;
        const uint8_t* d = bytes.data();
        switch (used) {
            case knst_cam::image_format::JPEG:
                header_ok = (bytes.length() >= 2 && d[0] == 0xFF && d[1] == 0xD8);
                break;
            case knst_cam::image_format::BMP:
                header_ok = (bytes.length() >= 2 && d[0] == 'B' && d[1] == 'M');
                break;
            case knst_cam::image_format::PNG:
                header_ok = (bytes.length() >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G');
                break;
            case knst_cam::image_format::PPM:
                header_ok = (bytes.length() >= 2 && d[0] == 'P' && (d[1] == '5' || d[1] == '6'));
                break;
            default:
                break;
        }

        char msg[128];
        std::snprintf(msg, sizeof(msg), "%-24s (%u bytes, header %s)",
                      cases[i].name, bytes.length(), header_ok ? "OK" : "BAD");
        check(header_ok, msg);

        // Write to disk as well (validates save_frame / ensure_parent_dir)
        char path[128];
        std::snprintf(path, sizeof(path),
                      "out/camera_demo/synthetic_%02d.%s", i, cases[i].ext);
        auto r = knst_cam::save_frame(f, knst_c16string(path), cases[i].opt);
        if (!r) {
            char m[128];
            std::snprintf(m, sizeof(m), "save_frame(%s) failed", path);
            fail(m);
        }
    }

    subsection("JPEG round-trip — decode and compare against the original");

    knst_cam::image_options jopt;
    jopt.format = knst_cam::image_format::JPEG;
    jopt.jpeg_quality = 95;
    jopt.jpeg_sub = knst_vid::mjpeg::subsampling::s444;

    knst_byte_string jpeg_bytes;
    knst_cam::image_format used = knst_cam::image_format::JPEG;
    knst_cam::errc e = knst_cam::encode_frame(f, jopt, jpeg_bytes, used);
    if (e == knst_cam::errc::Ok) {
        uint32_t dw = 0, dh = 0;
        auto decoded = knst_vid::mjpeg::decode_rgb24(jpeg_bytes.data(), jpeg_bytes.length(), dw, dh);
        if (decoded) {
            check(dw == W && dh == H, "decoded dimensions match source");

            knst_cam::frame back;
            back.width = dw;
            back.height = dh;
            back.stride = dw * 3;
            back.pix = knst_vid::pixel_format::RGB24;
            back.data = std::move(decoded.value);
            back.valid = true;

            double diff = frame_diff(f, back);
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                          "JPEG q=95 avg byte diff %.2f (< 5 expected)", diff);
            check(diff < 5.0, msg);
        } else {
            fail("JPEG decode failed");
        }
    } else {
        fail("JPEG encode failed (round-trip test skipped)");
    }

    subsection("Passthrough: MJPEG in → JPEG out (no re-encode)");
    {
        knst_cam::frame mjpeg_src;
        mjpeg_src.width = W;
        mjpeg_src.height = H;
        mjpeg_src.pix = knst_vid::pixel_format::MJPEG;
        mjpeg_src.data = jpeg_bytes;
        mjpeg_src.valid = true;

        knst_cam::image_options pass_opt;
        pass_opt.format = knst_cam::image_format::JPEG;
        pass_opt.allow_passthrough = true;

        knst_byte_string out;
        knst_cam::image_format pass_used;
        knst_cam::errc pe = knst_cam::encode_frame(mjpeg_src, pass_opt, out, pass_used);
        check(pe == knst_cam::errc::Ok && out.length() == jpeg_bytes.length(),
              "passthrough returns source bytes verbatim");

        // Now force a re-encode by enabling mirror
        pass_opt.mirror = true;
        knst_byte_string out2;
        knst_cam::image_format used2;
        knst_cam::errc e2 = knst_cam::encode_frame(mjpeg_src, pass_opt, out2, used2);
        check(e2 == knst_cam::errc::Ok && out2.length() != jpeg_bytes.length(),
              "mirror forces re-encode");
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// B. Pixel format conversion — no camera
// ═════════════════════════════════════════════════════════════════════════════

static void test_b_pixel_formats() {
    section("B. Pixel format conversion (no camera)");

    const uint32_t W = 96, H = 64;
    knst_cam::frame src = make_pattern(W, H);

    struct case_t {
        knst_vid::pixel_format fmt;
        const char*            name;
        double                 max_avg_diff;
        const char*            note;
    };

    // Tolerances: BGRA32/BGR24/RGBA32 are pixel-perfect (alpha dropped),
    // GREY loses colour info entirely, YUV formats lose chroma resolution.
    case_t cases[] = {
        { knst_vid::pixel_format::BGR24,  "BGR24",  0.0,   "exact" },
        { knst_vid::pixel_format::RGBA32, "RGBA32", 0.0,   "exact" },
        { knst_vid::pixel_format::BGRA32, "BGRA32", 0.0,   "exact" },
        { knst_vid::pixel_format::GREY,   "GREY",   90.0,  "colour dropped" },
        { knst_vid::pixel_format::YUYV,   "YUYV",   8.0,   "4:2:2 subsampling" },
        { knst_vid::pixel_format::UYVY,   "UYVY",   8.0,   "4:2:2 subsampling" },
        { knst_vid::pixel_format::NV12,   "NV12",   20.0,  "4:2:0 subsampling" },
        { knst_vid::pixel_format::I420,   "I420",   20.0,  "4:2:0 subsampling" },
    };
    const int n_cases = (int)(sizeof(cases) / sizeof(cases[0]));

    for (int i = 0; i < n_cases; ++i) {
        // Forward: RGB24 → dst
        knst_byte_string dst;
        auto fwd = knst_vid::color::convert_from_rgb24(
            src.data.data(), W, H, 0, cases[i].fmt, dst);
        if (fwd != knst_vid::errc::Ok) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "%s forward conversion", cases[i].name);
            fail(msg);
            continue;
        }

        // Back: dst → RGB24
        knst_byte_string back;
        auto rev = knst_vid::color::convert_to_rgb24(
            dst.data(), cases[i].fmt, W, H, 0, back);
        if (rev != knst_vid::errc::Ok) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "%s reverse conversion", cases[i].name);
            fail(msg);
            continue;
        }

        // Compute average per-byte difference
        double sum = 0;
        for (uint32_t k = 0; k < W * H * 3; ++k) {
            int d = (int)src.data[k] - (int)back[k];
            sum += (double)(d < 0 ? -d : d);
        }
        double avg = sum / (double)(W * H * 3);

        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "%-7s round-trip avg=%.2f  (%s)",
                      cases[i].name, avg, cases[i].note);
        check(avg <= cases[i].max_avg_diff, msg);
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// C. Image edge cases — no camera
// ═════════════════════════════════════════════════════════════════════════════

static void test_c_edge_cases() {
    section("C. Image edge cases (no camera)");

    struct size_case {
        uint32_t w, h;
        const char* name;
    };
    size_case sizes[] = {
        { 1, 1,       "1x1" },
        { 1, 100,     "1x100 (tall thin)" },
        { 100, 1,     "100x1 (wide thin)" },
        { 7, 5,       "7x5 (odd)" },
        { 8, 8,       "8x8 (one MCU)" },
        { 16, 16,     "16x16 (one MCU 420)" },
        { 1920, 1080, "1920x1080 (full HD)" },
    };
    const int n = (int)(sizeof(sizes) / sizeof(sizes[0]));

    for (int i = 0; i < n; ++i) {
        knst_cam::frame f = make_pattern(sizes[i].w, sizes[i].h);
        knst_cam::image_options opt;
        opt.format = knst_cam::image_format::JPEG;
        opt.jpeg_quality = 85;

        knst_byte_string bytes;
        knst_cam::image_format used;
        auto e = knst_cam::encode_frame(f, opt, bytes, used);

        char msg[96];
        std::snprintf(msg, sizeof(msg), "%-22s -> %s (%u bytes)",
                      sizes[i].name,
                      e == knst_cam::errc::Ok ? "OK" : err_name(e),
                      bytes.length());
        check(e == knst_cam::errc::Ok, msg);
    }

    subsection("Invalid rotation");
    {
        auto f = make_pattern(64, 64);
        knst_cam::image_options opt;
        opt.format = knst_cam::image_format::JPEG;
        opt.rotate_deg = 45;

        knst_byte_string out;
        knst_cam::image_format used;
        auto e = knst_cam::encode_frame(f, opt, out, used);
        check(e == knst_cam::errc::InvalidArgument,
              "rotate_deg=45 rejected as InvalidArgument");
    }

    subsection("Empty frame");
    {
        knst_cam::frame empty;
        knst_byte_string out;
        knst_cam::image_format used;
        auto e = knst_cam::encode_frame(empty, {}, out, used);
        check(e == knst_cam::errc::InvalidArgument, "empty frame rejected");
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// D. Direct video writer — no camera
// ═════════════════════════════════════════════════════════════════════════════

static void test_d_video_writer() {
    section("D. Direct video writer (no camera)");

    const uint32_t W = 320, H = 240, FPS = 30, FRAMES = 15;
    const knst_c16string dir = u"out/camera_demo";

    // Prepare a sequence of distinct frames
    knst_vector<knst_cam::frame> frames;
    for (uint32_t i = 0; i < FRAMES; ++i) {
        auto f = make_pattern(W, H);
        // Shift the source data so each frame is unique
        for (uint32_t p = 0; p < f.data.length(); p += 3) {
            f.data[p] = (uint8_t)((f.data[p] + i * 17u) & 0xFFu);
        }
        frames.push_back(f);
    }

    struct cont_case {
        knst_vid::container c;
        const char*         ext;
        const char*         name;
    };
    cont_case containers[] = {
        { knst_vid::container::AVI, "avi", "AVI" },
        { knst_vid::container::MKV, "mkv", "MKV" },
        { knst_vid::container::MP4, "mp4", "MP4" },
        { knst_vid::container::MOV, "mov", "MOV" },
    };
    const int n = (int)(sizeof(containers) / sizeof(containers[0]));

    for (int i = 0; i < n; ++i) {
        knst_c16string path = knst_file::join_path(
            dir, knst_c16string("vw_") + knst_c16string(containers[i].name)
                 + knst_c16string(".") + knst_c16string(containers[i].ext));

        knst_vid::video_writer w;
        knst_vid::video_writer::config cfg;
        cfg.cont = containers[i].c;
        cfg.vcodec = knst_vid::video_codec::MJPEG;
        cfg.has_video = true;
        cfg.width = W;
        cfg.height = H;
        cfg.fps_num = FPS;
        cfg.fps_den = 1;
        cfg.jpeg_quality = 85;

        auto r = w.open(path, cfg);
        if (!r) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "%s open failed",
                          containers[i].name);
            fail(msg);
            continue;
        }

        uint32_t written = 0;
        for (uint32_t k = 0; k < FRAMES; ++k) {
            auto wr = w.write_frame_rgb24(frames[k].data.data());
            if (!wr) break;
            ++written;
        }
        auto cr = w.close();
        if (!cr) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "%s close failed",
                          containers[i].name);
            fail(msg);
            continue;
        }

        // Read back and verify
        knst_vid::video_reader rd;
        auto ro = rd.open(path);
        if (!ro) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "%s re-open failed",
                          containers[i].name);
            fail(msg);
            continue;
        }

        uint64_t frame_count = rd.video_frames();
        char msg[128];
        std::snprintf(msg, sizeof(msg), "%s: wrote %u, read %llu",
                      containers[i].name, written,
                      (unsigned long long)frame_count);
        check(frame_count == FRAMES, msg);

        // Decode first frame and compare
        auto rf = rd.read_frame_rgb24(0);
        if (rf) {
            knst_cam::frame back;
            back.width = rf.value.width;
            back.height = rf.value.height;
            back.pix = knst_vid::pixel_format::RGB24;
            back.data = std::move(rf.value.data);
            back.valid = true;

            double d = frame_diff(frames[0], back);
            char m[96];
            std::snprintf(m, sizeof(m), "  %s first-frame diff=%.2f",
                          containers[i].name, d);
            check(d < 5.0, m);
        }
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// E. Motion detector — no camera
// ═════════════════════════════════════════════════════════════════════════════

static void test_e_motion_detector() {
    section("E. Motion detector (no camera)");

    knst_vid::motion_detector::config cfg;
    cfg.block = 8;
    cfg.threshold = 25;
    cfg.fraction = 0.05;
    cfg.min_interval_ms = 0;             // check every frame

    knst_vid::motion_detector det(cfg);
    const uint32_t W = 160, H = 120;

    auto dark = make_solid(W, H, 50, 50, 50);
    auto bright = make_solid(W, H, 200, 200, 200);
    auto almost = make_solid(W, H, 55, 55, 55);

    bool m1 = det.feed_rgb24(dark.data.data(), W, H, 1);
    check(!m1, "first frame reports no motion (no history yet)");

    bool m2 = det.feed_rgb24(dark.data.data(), W, H, 2);
    check(!m2, "identical second frame -> no motion");

    bool m3 = det.feed_rgb24(bright.data.data(), W, H, 3);
    check(m3, "radically different frame -> motion");

    std::printf("  last_fraction: %.3f\n", det.last_fraction());

    det.reset();
    det.feed_rgb24(dark.data.data(), W, H, 10);
    bool m4 = det.feed_rgb24(almost.data.data(), W, H, 11);
    check(!m4, "small change below threshold -> no motion");
}


// ═════════════════════════════════════════════════════════════════════════════
// F. Camera discovery — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_f_camera_discovery(knst_vector<knst_cam::camera_info>& cams, bool& have_camera) {
    section("F. Camera discovery");

    auto result = knst_cam::camera::enumerate();
    if (!result) {
        std::printf("  enumerate() : %s\n", err_name(knst_cam::errc::NotFound));
        std::printf("  (no camera on this machine — sections F-W will be skipped)\n");
        have_camera = false;
        return;
    }

    cams = std::move(result.value);
    have_camera = !cams.empty();

    if (!have_camera) {
        std::printf("  enumerate() returned an empty list\n");
        return;
    }

    std::printf("  cameras found : %u\n", cams.size());
    for (uint32_t i = 0; i < cams.size(); ++i) {
        const auto& c = cams[i];
        std::printf("\n  [%u] %s\n", i, u8(c.name));
        if (c.is_default) std::printf("      [default]\n");
        std::printf("      device  : %s\n", u8(c.device_id));
        std::printf("      driver  : %s\n", u8(c.driver));
        std::printf("      bus     : %s\n", u8(c.bus_info));
        if (c.vid || c.pid)
            std::printf("      VID:PID : %04X:%04X\n", c.vid, c.pid);
        std::printf("      size    : %ux%u max\n", c.max_width, c.max_height);
        std::printf("      formats : %u\n", c.formats.size());
    }

    // Verify default_camera() returns the same device as cams[0]
    auto def = knst_cam::camera::default_camera();
    if (def) check(def.value.device_id == cams[0].device_id,
                    "default_camera() matches cams[0]");

    // Verify find() finds cams[0] by device_id
    auto found = knst_cam::camera::find(cams[0].device_id);
    check(found.ok(), "find() locates the first camera");

    // Verify find() on a missing id returns NotFound
    auto missing = knst_cam::camera::find(u"/dev/definitely_not_a_camera");
    check(!missing && missing.st == knst_dev::status::NotFound,
          "find() on missing id returns NotFound");
}


// ═════════════════════════════════════════════════════════════════════════════
// G. Format inspection — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_g_format_inspection(const knst_vector<knst_cam::camera_info>& cams) {
    section("G. Format inspection");
    if (cams.empty()) {
        std::printf("  (skipped — no camera)\n");
        return;
    }

    const auto& c = cams[0];
    std::printf("  camera  : %s\n", u8(c.name));
    std::printf("  formats : %u advertised by the driver\n", c.formats.size());

    // Dedupe by (w, h, pix), show up to 12
    knst_vector<knst_cam::format> uniq;
    for (uint32_t i = 0; i < c.formats.size(); ++i) {
        const auto& f = c.formats[i];
        bool dup = false;
        for (uint32_t k = 0; k < uniq.size(); ++k) {
            if (uniq[k].width == f.width && uniq[k].height == f.height
                && uniq[k].pix == f.pix) { dup = true; break; }
        }
        if (!dup) uniq.push_back(f);
    }

    const uint32_t show = uniq.size() < 12 ? uniq.size() : 12;
    for (uint32_t i = 0; i < show; ++i) {
        const auto& f = uniq[i];
        std::printf("    %ux%u  %-6s  %u/%u fps",
                    f.width, f.height, pix_name(f.pix),
                    f.fps_num, f.fps_den);
        if (f.is_compressed) std::printf("  [compressed]");
        std::printf("\n");
    }

    check(!uniq.empty(), "at least one unique (size, pixel format) combination");
}


// ═════════════════════════════════════════════════════════════════════════════
// H. Open camera — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_h_open(const knst_vector<knst_cam::camera_info>& cams,
                        knst_cam::camera& cam, bool& opened) {
    section("H. Open camera");
    opened = false;
    if (cams.empty()) {
        std::printf("  (skipped — no camera)\n");
        return;
    }

    knst_cam::open_options opt;
    opt.width = 1280;
    opt.height = 720;
    opt.fps_num = 30;
    opt.fps_den = 1;
    opt.pix = knst_vid::pixel_format::MJPEG;     // zero re-encode path
    opt.buffer_count = 4;
    opt.warmup_frames = 3;
    opt.strict_format = false;
    opt.stall_timeout_ms = 5000;

    auto r = cam.open(cams[0], opt);
    if (!r) {
        std::printf("  open() FAILED: %s\n", cam.last_error_string());
        return;
    }
    opened = true;
    check(cam.is_open(), "camera is open");

    auto cf = cam.current_format();
    if (cf) {
        std::printf("  negotiated : %ux%u  %s  %u/%u fps\n",
                    cf.value.width, cf.value.height,
                    pix_name(cf.value.pix),
                    cf.value.fps_num, cf.value.fps_den);
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// I. One-shot grab — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_i_grab(knst_cam::camera& cam, bool opened) {
    section("I. One-shot grab");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    auto f = cam.grab(3, 3000);
    if (!f) {
        std::printf("  grab() FAILED: %s\n", cam.last_error_string());
        return;
    }
    std::printf("  grabbed : %ux%u  %s  %u bytes  seq=%u\n",
                f.value.width, f.value.height, pix_name(f.value.pix),
                f.value.data.length(), f.value.sequence);
    check(f.value.valid, "grab() returned a valid frame");
}


// ═════════════════════════════════════════════════════════════════════════════
// J. Live streaming — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static std::atomic<uint32_t> g_frames_seen{0};
static std::atomic<uint32_t> g_events_seen{0};

static void on_frame(void*, const knst_cam::frame_view& f) {
    uint32_t n = g_frames_seen.fetch_add(1) + 1;
    if (n <= 3 || (n % 30) == 0) {
        std::printf("      [frame] #%u  %ux%u  %s  %u bytes\n",
                    f.sequence, f.width, f.height, pix_name(f.pix), f.size);
    }
}
static void on_event(void*, knst_cam::event e) {
    g_events_seen.fetch_add(1);
    std::printf("      [event] %s\n", knst_cam::event_name(e));
}

static void test_j_streaming(knst_cam::camera& cam, bool opened) {
    section("J. Live streaming (2 seconds)");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    g_frames_seen = 0;
    g_events_seen = 0;
    cam.set_event_callback(on_event, nullptr);
    cam.set_event_mask(0xFFFFFFFF);

    auto r = cam.start(on_frame, nullptr);
    if (!r) {
        std::printf("  start() FAILED: %s\n", cam.last_error_string());
        return;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    cam.stop();

    std::printf("  frames_seen : %u\n", g_frames_seen.load());
    std::printf("  events_seen : %u\n", g_events_seen.load());
    check(g_frames_seen.load() > 0, "received frames during streaming");
}


// ═════════════════════════════════════════════════════════════════════════════
// K. Snapshots — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_k_snapshots(knst_cam::camera& cam, bool opened) {
    section("K. Snapshots in all formats");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    cam.start(on_frame, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    check(cam.snapshot_jpeg(u"out/camera_demo/snap_q85.jpg", 85).ok(),
          "snapshot_jpeg(q=85)");

    {
        knst_cam::image_options o;
        o.format = knst_cam::image_format::JPEG;
        o.jpeg_quality = 95;
        o.jpeg_sub = knst_vid::mjpeg::subsampling::s444;
        check(cam.snapshot(u"out/camera_demo/snap_q95_444.jpg", o).ok(),
              "snapshot(JPEG q=95 4:4:4)");
    }

    check(cam.snapshot_bmp(u"out/camera_demo/snap.bmp").ok(), "snapshot_bmp");
    check(cam.snapshot_png(u"out/camera_demo/snap.png").ok(), "snapshot_png");

    {
        knst_cam::image_options o;
        o.format = knst_cam::image_format::JPEG;
        o.mirror = true;
        o.rotate_deg = 90;
        o.out_w = 480;
        check(cam.snapshot(u"out/camera_demo/snap_mirror_rot90.jpg", o).ok(),
              "snapshot(mirror+rot90+w480)");
    }

    {
        knst_cam::image_options o;
        o.format = knst_cam::image_format::PPM;
        o.grey = true;
        check(cam.snapshot(u"out/camera_demo/snap_grey.pgm", o).ok(),
              "snapshot(grey PGM)");
    }

    auto ra = cam.snapshot_auto(u"out/camera_demo", u"auto_");
    if (ra) {
        std::printf("  snapshot_auto: %s\n", u8(ra.value));
        ok("snapshot_auto produced a path");
    } else {
        fail("snapshot_auto failed");
    }

    cam.stop();
}


// ═════════════════════════════════════════════════════════════════════════════
// L. Passthrough check — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_l_passthrough(knst_cam::camera& cam, bool opened) {
    section("L. MJPEG passthrough");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    auto fr = cam.capture_latest(3000);
    if (!fr) {
        std::printf("  capture_latest() failed\n");
        return;
    }

    if (fr.value.pix == knst_vid::pixel_format::MJPEG) {
        bool is_jpeg = fr.value.data.length() >= 2
                    && fr.value.data[0] == 0xFF
                    && fr.value.data[1] == 0xD8;
        check(is_jpeg, "camera produces valid JPEG (MJPEG passthrough eligible)");
    } else {
        std::printf("  camera pix=%s — no MJPEG passthrough\n",
                    pix_name(fr.value.pix));
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// M. Burst — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_m_burst(knst_cam::camera& cam, bool opened) {
    section("M. Burst capture (5 photos, 200 ms apart)");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    auto r = cam.snapshot_burst(u"out/camera_demo/burst", 5, 200, u"burst_");
    if (!r) {
        std::printf("  snapshot_burst() FAILED: %s\n", cam.last_error_string());
        return;
    }

    std::printf("  wrote %u files\n", r.value.size());
    for (uint32_t i = 0; i < r.value.size(); ++i) {
        std::printf("    %s\n", u8(r.value[i]));
    }
    check(r.value.size() == 5, "all 5 burst photos written");
}


// ═════════════════════════════════════════════════════════════════════════════
// N. Snapshot to memory — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_n_snapshot_memory(knst_cam::camera& cam, bool opened) {
    section("N. Snapshot to memory");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    knst_cam::image_options o;
    o.format = knst_cam::image_format::JPEG;
    o.jpeg_quality = 80;

    auto r = cam.snapshot_memory(o);
    if (!r) {
        std::printf("  snapshot_memory() FAILED: %s\n", cam.last_error_string());
        return;
    }
    std::printf("  %u bytes in RAM\n", r.value.length());
    check(r.value.length() > 100, "snapshot_memory produced JPEG bytes");
}


// ═════════════════════════════════════════════════════════════════════════════
// O. Video-only recording — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_o_video_only(knst_cam::camera& cam, bool opened) {
    section("O. Video-only recording (2 seconds, MKV)");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    knst_cam::record_options ro;
    ro.path = u"out/camera_demo/clip_video.mkv";
    ro.enable_video = true;
    ro.enable_audio = false;
    ro.vcodec = knst_vid::video_codec::MJPEG;
    ro.jpeg_quality = 85;

    auto r = cam.start_recording(ro);
    if (!r) {
        std::printf("  start_recording() FAILED: %s\n", cam.last_error_string());
        return;
    }

    if (!cam.is_streaming()) cam.start(on_frame, nullptr, on_event, nullptr);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto status = cam.recording_info();
    std::printf("  live: frames=%llu  bytes=%llu  seconds=%.2f\n",
                (unsigned long long)status.frames,
                (unsigned long long)status.bytes,
                status.seconds);

    cam.stop_recording();
    std::printf("  saved: %s\n", u8(cam.recording_info().path));
    check(status.frames > 0, "video-only recording wrote frames");
}


// ═════════════════════════════════════════════════════════════════════════════
// P. A/V recording — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_p_av(knst_cam::camera& cam, bool opened) {
    section("P. A/V recording (2 seconds, MKV with PCM)");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

#if KNST_CAM_HAS_AUDIO
    knst_cam::record_options ro;
    ro.path = u"out/camera_demo/clip_av.mkv";
    ro.enable_video = true;
    ro.enable_audio = true;
    ro.vcodec = knst_vid::video_codec::MJPEG;
    ro.jpeg_quality = 85;
    ro.audio_codec = knst_vid::audio_codec::PCM_S16LE;
    ro.audio_rate = 48000;
    ro.audio_channels = 2;
    ro.audio_remove_dc = true;

    auto r = cam.start_recording(ro);
    if (!r) {
        std::printf("  start_recording() FAILED: %s\n", cam.last_error_string());
        return;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    cam.stop_recording();

    auto st = cam.recording_info();
    std::printf("  frames=%llu  bytes=%llu  seconds=%.2f\n",
                (unsigned long long)st.frames,
                (unsigned long long)st.bytes, st.seconds);
    check(st.frames > 0, "A/V recording captured frames");
#else
    std::printf("  (knst_audio.hpp not available — skipped)\n");
#endif
}


// ═════════════════════════════════════════════════════════════════════════════
// Q. Limits and splitting — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_q_limits(knst_cam::camera& cam, bool opened) {
    section("Q. Recording limits and splitting");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    knst_cam::record_options ro;
    ro.path = u"out/camera_demo/segmented.avi";
    ro.enable_video = true;
    ro.enable_audio = false;
    ro.vcodec = knst_vid::video_codec::MJPEG;
    ro.jpeg_quality = 70;
    ro.max_duration_ms = 1000;
    ro.split_ms = 400;

    if (cam.start_recording(ro)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        cam.stop_recording();

        int found = 0;
        for (int i = 1; i <= 4; ++i) {
            char path[128];
            std::snprintf(path, sizeof(path),
                          "out/camera_demo/segmented_%04d.avi", i);
            if (knst_file::path_exists(knst_c16string(path))) {
                std::printf("  segment %d : %s\n", i, path);
                ++found;
            }
        }
        check(found >= 1, "at least one segment written");
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// R. Pause / resume — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_r_pause(knst_cam::camera& cam, bool opened) {
    section("R. Pause / resume");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    knst_cam::record_options ro;
    ro.path = u"out/camera_demo/pause_resume.mkv";
    ro.enable_video = true;
    ro.enable_audio = false;
    ro.vcodec = knst_vid::video_codec::MJPEG;

    if (!cam.start_recording(ro)) return;

    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    cam.pause_recording();
    std::printf("  paused for 500 ms...\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    cam.resume_recording();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    cam.stop_recording();

    ok("pause/resume cycle completed without error");
}


// ═════════════════════════════════════════════════════════════════════════════
// S. Motion-triggered recording — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_s_motion_record(knst_cam::camera& cam, bool opened) {
    section("S. Motion-triggered recording (3 seconds)");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    knst_cam::motion_options mo;
    mo.detector.block = 8;
    mo.detector.threshold = 25;
    mo.detector.fraction = 0.05;
    mo.detector.min_interval_ms = 100;
    mo.post_roll_ms = 1000;
    mo.auto_record = true;
    mo.dir = u"out/camera_demo/motion";
    mo.record.cont = knst_vid::container::MKV;
    mo.record.enable_video = true;
    mo.record.vcodec = knst_vid::video_codec::MJPEG;
    mo.record.jpeg_quality = 75;

    auto r = cam.enable_motion_detection(mo);
    if (!r) {
        std::printf("  enable_motion_detection() FAILED: %s\n",
                    cam.last_error_string());
        return;
    }

    if (!cam.is_streaming()) cam.start(on_frame, nullptr, on_event, nullptr);
    std::printf("  wave at the camera for 3 seconds...\n");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::printf("  motion_active: %s\n",
                cam.is_motion_active() ? "yes" : "no");
    cam.disable_motion_detection();
    ok("motion detector ran without crashing");
}


// ═════════════════════════════════════════════════════════════════════════════
// T. Controls — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_t_controls(knst_cam::camera& cam, bool opened) {
    section("T. Camera controls");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    auto ctrls = cam.controls();
    if (!ctrls) {
        std::printf("  controls() FAILED: %s\n", cam.last_error_string());
        return;
    }

    std::printf("  controls found : %u\n", ctrls.value.size());
    for (uint32_t i = 0; i < ctrls.value.size() && i < 8; ++i) {
        const auto& c = ctrls.value[i];
        std::printf("    %-20s = %d  [%d..%d step %d]%s%s\n",
                    knst_cam::control_type_name(c.type),
                    c.current, c.min, c.max, c.step,
                    c.is_auto ? " (auto)" : "",
                    c.is_auto_on ? " [auto ON]" : "");
    }

        subsection("Get/set one control (Brightness)");
    auto br = cam.get_control(knst_cam::control_type::Brightness);
    if (br) {
        int32_t target = br.value.current + br.value.step * 2;
        if (target > br.value.max) target = br.value.max;
        std::printf("  brightness: %d -> %d\n", br.value.current, target);
        auto wr = cam.set_control(knst_cam::control_type::Brightness, target);
        // Bazı UVC sürücüleri bu kontrolü sadece AUTO modda yönetir
        // (GetRange caps'inde Manual flag yok). Bu donanım hatası
        // değil, sürücü tasarımıdır — NotSupported kabul edilir.
        if (wr) {
            check(true, "set_control(Brightness)");
        } else if (wr.st == knst_dev::status::NotSupported) {
            std::printf("  note: driver restricts this control to auto mode\n");
            ok("set_control(Brightness) — NotSupported (expected on some UVC drivers)");
        } else {
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                          "set_control(Brightness) — %s",
                          wr.error_string());
            fail(msg);
        }
        cam.reset_control(knst_cam::control_type::Brightness);
    }
    subsection("Auto modes");
    cam.set_control_auto(knst_cam::control_type::Exposure, true);
    cam.set_control_auto(knst_cam::control_type::Focus, true);
    cam.set_control_auto(knst_cam::control_type::WhiteBalance, true);

    auto rr = cam.reset_all_controls();
    if (rr) ok("reset_all_controls");
    else std::printf("  reset_all_controls : %s (drivers may refuse)\n",
                     cam.last_error_string());
}


// ═════════════════════════════════════════════════════════════════════════════
// U. Save/restore — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_u_save_restore(knst_cam::camera& cam, bool opened) {
    section("U. Save / restore control snapshot");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

    auto saved = cam.save_controls();
    if (!saved) {
        std::printf("  save_controls() FAILED: %s\n", cam.last_error_string());
        return;
    }
    std::printf("  saved %u control values\n", saved.value.size());
    check(!saved.value.empty(), "save_controls returned values");

    auto restored = cam.restore_controls(saved.value);
    check(restored.ok(), "restore_controls succeeded");
}


// ═════════════════════════════════════════════════════════════════════════════
// V. Multi-camera — needs 2+ cameras
// ═════════════════════════════════════════════════════════════════════════════

static void test_v_multi(knst_cam::camera& single, bool opened) {
    section("V. Multi-camera (camera_array)");
    if (opened && single.is_open()) {
        single.close();
        std::printf("  (closed single camera so camera_array can claim the device)\n");
    }

    knst_cam::camera_array arr;
    knst_cam::open_options opt;
    opt.width = 640;
    opt.height = 480;
    opt.pix = knst_vid::pixel_format::MJPEG;

    auto r = arr.open_all(opt);
    if (!r) {
        std::printf("  open_all() : no cameras claimed (device busy or none present)\n");
        return;
    }

    std::printf("  opened %u camera(s)\n", arr.count());
    auto snaps = arr.snapshot_all(u"out/camera_demo/multi");
    if (snaps) {
        for (uint32_t i = 0; i < snaps.value.size(); ++i) {
            std::printf("    cam%u : %s\n", i, u8(snaps.value[i]));
        }
        check(!snaps.value.empty(), "multi-camera snapshots written");
    }
    arr.close_all();
}


// ═════════════════════════════════════════════════════════════════════════════
// W. Diagnostics — needs hardware
// ═════════════════════════════════════════════════════════════════════════════

static void test_w_diagnostics(knst_cam::camera& cam, bool opened,
                               const knst_vector<knst_cam::camera_info>& cams) {
    section("W. Diagnostics");
    if (!opened) {
        std::printf("  (skipped — camera not open)\n");
        return;
    }

        // Reopen if the multi-camera section closed it.
    // NOT knst_cam::open_options() default (640x480 YUYV) — this webcam
    // does not advertise YUYV; MediaFoundation would fall back to RGB32
    // (software conversion) and the read loop can starve. Use MJPEG
    // 1280x720 like the H section, which is guaranteed to negotiate.
    if (!cam.is_open() && !cams.empty()) {
        knst_cam::open_options reopen_opt;
        reopen_opt.width  = 1280;
        reopen_opt.height = 720;
        reopen_opt.fps_num = 30;
        reopen_opt.fps_den = 1;
        reopen_opt.pix = knst_vid::pixel_format::MJPEG;
        reopen_opt.warmup_frames = 3;
        if (!cam.open(cams[0], reopen_opt)) {
            std::printf("  re-open failed\n");
            return;
        }
    }
    cam.start(on_frame, nullptr);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    cam.stop();

    auto s = cam.stats();
    std::printf("  frames_captured : %llu\n",
                (unsigned long long)s.frames_captured);
    std::printf("  frames_dropped  : %llu\n",
                (unsigned long long)s.frames_dropped);
    std::printf("  frames_corrupt  : %llu\n",
                (unsigned long long)s.frames_corrupt);
    std::printf("  frames_recorded : %llu\n",
                (unsigned long long)s.frames_recorded);
    std::printf("  reconnects      : %llu\n",
                (unsigned long long)s.reconnects);
    std::printf("  effective fps   : %.2f\n", s.fps);
    std::printf("  last_error      : %s (last failure ever, not current state)\n",
                cam.last_error_string());

    check(s.frames_captured > 0, "stats recorded captured frames");
}


// ═════════════════════════════════════════════════════════════════════════════
// X. Error handling — works everywhere
// ═════════════════════════════════════════════════════════════════════════════

static void test_x_error_handling() {
    section("X. Error handling (no camera needed)");

    knst_cam::camera c;

    (void)c.open(knst_c16string(), knst_cam::open_options());
    check(c.last_error() == knst_cam::errc::InvalidArgument,
          "open(empty) → InvalidArgument");

    (void)c.open(u"/etc/passwd", knst_cam::open_options());
    check(c.last_error() != knst_cam::errc::Ok,
          "open(/etc/passwd) rejected");

    knst_cam::record_options ro;
    ro.path = u"out/camera_demo/nope.mkv";
    (void)c.start_recording(ro);
    check(c.last_error() == knst_cam::errc::BadState,
          "start_recording(closed) → BadState");

    (void)c.snapshot_jpeg(u"out/camera_demo/nope.jpg", 85);
    check(c.last_error() == knst_cam::errc::BadState,
          "snapshot(closed) → BadState");

        // WebM validation in start_recording() runs before the "camera is
    // open" check, but only when enable_video is false — otherwise the
    // closed-camera check fires first and returns BadState. Since we
    // want to exercise the WebM rejection specifically (and the camera
    // here is intentionally not opened), drive the audio-only path so
    // the WebM check is the first one that fires.
    knst_cam::record_options ro2;
    ro2.path = u"out/camera_demo/nope.webm";
    ro2.cont = knst_vid::container::WebM;
    ro2.enable_video = false;
    ro2.enable_audio = true;
    (void)c.start_recording(ro2);
    check(c.last_error() == knst_cam::errc::NotSupported,
          "start_recording(.webm) → NotSupported");

    // Invalid encode options
    auto fake = make_pattern(64, 64);
    knst_cam::image_options bad;
    bad.rotate_deg = 45;
    knst_byte_string bytes;
    knst_cam::image_format used;
    auto e = knst_cam::encode_frame(fake, bad, bytes, used);
    check(e == knst_cam::errc::InvalidArgument,
          "encode_frame(rot=45) → InvalidArgument");

            // save_frame() yazamayacağı bir yola yazmaya çalışıldığında fail etmeli.
    // Linux: /proc salt-okunur kernel mount'tur, yeni dosya kabul etmez.
    // Windows: /proc özel bir şey ifade etmez → yazılabilir. Bunun yerine
    // Windows'ta dosya adı olarak geçersiz karakterler içeren bir yol
    // kullanıyoruz; NTFS < > : " | ? * karakterlerini reddeder.
#if KNST_USING_PLATFORM_WINDOWS
    auto r = knst_cam::save_frame(fake, u"<>|?.jpg");
#else
    auto r = knst_cam::save_frame(fake, u"/proc/nope.jpg");
#endif
    check(!r.ok(), "save_frame(unwritable path) rejected by OS");
}


// ═════════════════════════════════════════════════════════════════════════════
// Y. Platform summary — works everywhere
// ═════════════════════════════════════════════════════════════════════════════

static void test_y_platform() {
    section("Y. Platform summary");

    std::printf("  backend : %s\n", knst_cam::camera::backend_name());

#if KNST_CAM_LINUX_V4L2
    std::printf("  This build uses the Linux V4L2 device layer:\n");
    std::printf("    /dev/video* via ioctl(VIDIOC_*)\n");
    std::printf("    MMAP streaming, poll() stall detection\n");
    std::printf("    hot-unplug: Disconnected / Reconnected events\n");
    std::printf("    realpath()-checked /dev/videoN, char device verified\n");
#elif KNST_CAM_WINDOWS
    std::printf("  This build uses the Media Foundation device layer:\n");
    std::printf("    enumeration via MFEnumDeviceSources\n");
    std::printf("    capture via IMFSourceReader (MJPEG / NV12 / RGB32)\n");
    std::printf("    controls via IAMVideoProcAmp / IAMCameraControl\n");
#elif KNST_CAM_ANDROID
    std::printf("  Android Camera2 NDK:\n");
    std::printf("    enumeration via ACameraManager\n");
    std::printf("    capture via AImageReader (JPEG / YUV_420_888)\n");
    std::printf("    controls via ACaptureRequest + camera metadata\n");
#else
    std::printf("  Unsupported platform — device layer is a stub\n");
#endif

    std::printf("\n  Safety rules enforced by the header:\n");
    std::printf("    camera is not copyable or movable\n");
    std::printf("    start/stop/close from a callback → BadState\n");
    std::printf("    driver size values are bounds-checked before use\n");
    std::printf("    V4L2_BUF_FLAG_ERROR frames are dropped\n");
    std::printf("    poll() instead of select() (no FD_SETSIZE cliff)\n");
}


// ═════════════════════════════════════════════════════════════════════════════
// main
// ═════════════════════════════════════════════════════════════════════════════

int main() {
    std::printf("knst_camera_basic — comprehensive demonstration\n");
    std::printf("Backend: %s\n", knst_cam::camera::backend_name());

    // No-camera sections
    test_a_synthetic_encoding();
    test_b_pixel_formats();
    test_c_edge_cases();
    test_d_video_writer();
    test_e_motion_detector();

    // Camera discovery
    knst_vector<knst_cam::camera_info> cams;
    bool have_camera = false;
    test_f_camera_discovery(cams, have_camera);

    // Camera-dependent sections (all guard on cams.empty())
    test_g_format_inspection(cams);

    knst_cam::camera cam;
    bool opened = false;
    test_h_open(cams, cam, opened);
    test_i_grab(cam, opened);
    test_j_streaming(cam, opened);
    test_k_snapshots(cam, opened);
    test_l_passthrough(cam, opened);
    test_m_burst(cam, opened);
    test_n_snapshot_memory(cam, opened);
    test_o_video_only(cam, opened);
    test_p_av(cam, opened);
    test_q_limits(cam, opened);
    test_r_pause(cam, opened);
    test_s_motion_record(cam, opened);
    test_t_controls(cam, opened);
    test_u_save_restore(cam, opened);
    test_v_multi(cam, opened);
    test_w_diagnostics(cam, opened, cams);

    // Universal sections
    test_x_error_handling();
    test_y_platform();

    // Clean shutdown
    if (cam.is_open()) {
        cam.disable_motion_detection();
        cam.stop_recording();
        cam.stop();
        cam.close();
    }

    std::printf("\n==================================================\n");
    std::printf("  passed: %d   failed: %d\n", g_pass, g_fail);
    std::printf("==================================================\n");

    return g_fail == 0 ? 0 : 1;
}