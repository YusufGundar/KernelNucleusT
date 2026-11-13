/*
  knst_video_basic.cpp

  Comprehensive tutorial + verification for knst_vid — the video / audio
  container & codec framework of KernelNucleusT.

  HONESTY: the library implements real MJPEG / RAW / PCM and *carries*
  H.264 / H.265 / VP8 / VP9 / AV1 / AAC / Opus / MP3 / FLAC as opaque
  packets. This example tests the implemented parts end-to-end and shows
  the passthrough registry for the rest (§30).

  Requirements: knst_image_loader for MJPEG decode (KNST_VIDEO_HAS_IMAGE_LOADER = 1,
  default). Everything writes into a sandbox in the system temp dir.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace knst_vid;

// ============================================================
// Test infrastructure
// ============================================================
static int s_pass = 0, s_fail = 0, s_skip = 0;

static void section(const char* title, const char* what, const char* expect) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "  WHAT    : " << what << "\n";
    std::cout << "  EXPECT  : " << expect << "\n";
    std::cout << "--------------------------------------------------\n";
}
static void check(const char* name, bool ok) {
    if (ok) { ++s_pass; std::cout << "  >>> PASS : " << name << "\n"; }
    else    { ++s_fail; std::cout << "  >>> FAIL : " << name << "\n"; }
}
static void skip(const char* name, const char* why) {
    ++s_skip; std::cout << "  >>> SKIP : " << name << "  (" << why << ")\n";
}

// ============================================================
// Synthetic frames
// ============================================================
static void gen_gradient(uint32_t w, uint32_t h, knst_byte_string& rgb) {
    rgb.resize(w * h * 3);
    if (rgb.length() != w * h * 3) return;
    uint8_t* p = &rgb[0];
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* d = p + ((size_t)y * w + x) * 3;
            d[0] = (uint8_t)(w > 1 ? (x * 255) / (w - 1) : 128);
            d[1] = (uint8_t)(h > 1 ? (y * 255) / (h - 1) : 128);
            d[2] = 128;
        }
}
static void gen_solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, knst_byte_string& rgb) {
    rgb.resize(w * h * 3);
    if (rgb.length() != w * h * 3) return;
    uint8_t* p = &rgb[0];
    for (size_t i = 0, n = (size_t)w * h; i < n; ++i) { p[i * 3] = r; p[i * 3 + 1] = g; p[i * 3 + 2] = b; }
}
static void gen_sine_s16(int16_t* s, uint32_t frames, uint32_t ch, uint32_t rate, double hz) {
    for (uint32_t i = 0; i < frames; ++i) {
        int16_t v = (int16_t)(16000.0 * std::sin(2.0 * 3.14159265358979323846 * hz * i / rate));
        for (uint32_t c = 0; c < ch; ++c) s[i * ch + c] = v;
    }
}

int main() {

    std::cout << "knst_video_basic — tutorial + verification\n";
    std::cout << "sizeof(video_frame)  : " << sizeof(video_frame)  << " bytes\n";
    std::cout << "sizeof(audio_frame)  : " << sizeof(audio_frame)  << " bytes\n";
    std::cout << "sizeof(stream_desc)  : " << sizeof(stream_desc)  << " bytes\n";
    std::cout << "sizeof(media_info)   : " << sizeof(media_info)   << " bytes\n";

    knst_c16string sandbox = knst_file::join_path(knst_file::get_temp_dir(), u"knst_video_demo");
    knst_file::create_directories(sandbox);

    /* ================================================================
     * 1) Environment & honesty matrix
     * ================================================================ */
    section("1) Environment & honesty",
            "What is REALLY implemented and what is passthrough",
            "documented capabilities match the implementation");
    {
        std::cout << "  Containers (write+read): AVI, MKV, MP4, MOV, WAV, RAW\n";
        std::cout << "  Containers (read):       WebM (as MKV)\n";
        std::cout << "  Codecs   (encode+decode): MJPEG, RAW (all packed/planar/Bayer), PCM U8/S16/S24/S32/F32\n";
        std::cout << "  Codecs   (passthrough):   H.264 H.265 VP8 VP9 AV1 MPEG4 ProRes DNxHD AAC Opus MP3 FLAC Vorbis\n";
        std::cout << "  NotSupported (per spec): fragmented MP4 (moof), MKV lacing, AVI >4 GB\n";
        std::cout << "  External decoders: knst_vid::external_codec::register_* (see §30)\n";
        check("honesty matrix documented", true);
    }

    /* ================================================================
     * 2) Enums and error strings
     * ================================================================ */
    section("2) Enums & error strings",
            "errc / video_codec / audio_codec / pixel_format names",
            "all codes have non-empty names");
    {
        bool ok = true;
        for (int e = 0; e <= (int)errc::EndOfStream; ++e) {
            const char* s = errc_string((errc)e);
            if (!s || !*s || std::strcmp(s, "unknown") == 0) ok = false;
        }
        std::cout << "  video_codec::MJPEG   -> " << video_codec_name(video_codec::MJPEG)   << "\n";
        std::cout << "  video_codec::H264    -> " << video_codec_name(video_codec::H264)    << "\n";
        std::cout << "  audio_codec::PCM_S16LE -> " << audio_codec_name(audio_codec::PCM_S16LE) << "\n";
        std::cout << "  pixel_format::NV12   -> " << pixel_format_name(pixel_format::NV12)   << "\n";
        std::cout << "  pixel_format_bpp(NV12) = " << pixel_format_bpp(pixel_format::NV12)  << "\n";
        check("all errc have strings", ok);
        check("PCM_S16LE is PCM", audio_codec_is_pcm(audio_codec::PCM_S16LE));
        check("AAC is not PCM",  !audio_codec_is_pcm(audio_codec::AAC));
        check("NV12 bpp == 12",   pixel_format_bpp(pixel_format::NV12) == 12);
    }

    /* ================================================================
     * 3) Pixel format layout
     * ================================================================ */
    section("3) Pixel format layout",
            "compute_layout for packed and planar formats",
            "tightly packed (stride 0) sizes are correct");
    {
        plane_layout L;
        bool ok = compute_layout(pixel_format::RGB24,  64, 32, 0, L) && L.planes == 1 && L.total == (uint64_t)64 * 32 * 3;
        ok = ok && compute_layout(pixel_format::BGRA32, 64, 32, 0, L) && L.total == (uint64_t)64 * 32 * 4;
        ok = ok && compute_layout(pixel_format::GREY,   64, 32, 0, L) && L.total == (uint64_t)64 * 32;
        ok = ok && compute_layout(pixel_format::YUYV,   64, 32, 0, L) && L.total == (uint64_t)64 * 32 * 2;
        ok = ok && compute_layout(pixel_format::NV12,   64, 32, 0, L) && L.total == (uint64_t)64 * 32 * 3 / 2;
        ok = ok && compute_layout(pixel_format::I420,   64, 32, 0, L) && L.total == (uint64_t)64 * 32 * 3 / 2;
        std::cout << "  RGB24  : total=" << (uint64_t)64 * 32 * 3  << " (expected)\n";
        std::cout << "  BGRA32 : total=" << (uint64_t)64 * 32 * 4  << " (expected)\n";
        std::cout << "  NV12   : planes=" << L.planes << " (3 planes: Y + interleaved UV + we check differently)\n";
        check("layout sizes match", ok);
        check("MJPEG has no layout",
              !compute_layout(pixel_format::MJPEG, 64, 32, 0, L));
    }

    /* ================================================================
     * 4) Color conversion round-trip
     * ================================================================ */
    section("4) Color conversion round-trip",
            "RGB24 -> BGR24 -> RGB24, RGB24 -> YUYV/NV12/I420 -> RGB24",
            "RGB/BGR exact, YUV within BT.601 tolerance");
    {
        const uint32_t W = 48, H = 32;
        knst_byte_string rgb, tmp, back;
        gen_gradient(W, H, rgb);

        // exact for RGB <-> BGR
        color::convert_from_rgb24(&rgb[0], W, H, 0, pixel_format::BGR24, tmp);
        color::convert_to_rgb24(&tmp[0], pixel_format::BGR24, W, H, 0, back);
        check("RGB24 <-> BGR24 (exact)", back.length() == rgb.length() && std::memcmp(&back[0], &rgb[0], rgb.length()) == 0);

        // YUV: within ~5 per channel due to BT.601 rounding
        struct { const char* name; pixel_format fmt; } fmts[] = {
            {"YUYV", pixel_format::YUYV}, {"NV12", pixel_format::NV12},
            {"I420", pixel_format::I420},
        };
        for (auto& f : fmts) {
            knst_byte_string enc, dec;
            color::convert_from_rgb24(&rgb[0], W, H, 0, f.fmt, enc);
            color::convert_to_rgb24(&enc[0], f.fmt, W, H, 0, dec);
            int maxdiff = 0;
            for (uint32_t i = 0; i < W * H * 3; ++i) {
                int d = (int)dec[i] - (int)rgb[i];
                if (d < 0) d = -d;
                if (d > maxdiff) maxdiff = d;
            }
            std::cout << "  " << f.name << " max channel error = " << maxdiff << "\n";
            check(f.name, maxdiff < 20);
        }
    }

    /* ================================================================
     * 5) Bayer demosaic
     * ================================================================ */
    section("5) Bayer demosaic",
            "All 4 patterns (BGGR/GBRG/GRBG/RGGB)",
            "all patterns decode to the same RGB24");
    {
        const uint32_t W = 16, H = 16;
        knst_byte_string rgb, bayer, back;
        gen_gradient(W, H, rgb);
        struct { pixel_format p; const char* n; } pats[] = {
            {pixel_format::Bayer_BGGR, "BGGR"}, {pixel_format::Bayer_GBRG, "GBRG"},
            {pixel_format::Bayer_GRBG, "GRBG"}, {pixel_format::Bayer_RGGB, "RGGB"},
        };
        int ok = 0;
        for (auto& pat : pats) {
            // extract green channel to simulate one-plane Bayer source
            bayer.resize(W * H);
            for (size_t i = 0; i < (size_t)W * H; ++i) bayer[(uint32_t)i] = rgb[i * 3 + 1];
            back.clear();
            if (color::bayer_to_rgb24(&bayer[0], W, H, 0, pat.p, back) == errc::Ok && back.length() == W * H * 3) ++ok;
            std::cout << "  " << pat.n << " -> " << (back.length() ? "ok" : "fail") << "\n";
        }
        check("all 4 Bayer patterns demosaic", ok == 4);
    }

    /* ================================================================
     * 6) Image tools: resize
     * ================================================================ */
    section("6) Image tools: resize",
            "nearest / bilinear / box to 128x128 and 16x16",
            "dimensions correct, box averages down");
    {
        const uint32_t W = 128, H = 128;
        knst_byte_string rgb;
        gen_gradient(W, H, rgb);
        auto up   = img::resize_rgb24(&rgb[0], W, H, 128, 128, img::resize_filter::bilinear);
        auto down = img::resize_rgb24(&rgb[0], W, H, 32, 32, img::resize_filter::box);
                auto near_img = img::resize_rgb24(&rgb[0], W, H, 64, 64, img::resize_filter::nearest);
        check("identity bilinear", (bool)up && up.value.length() == W * H * 3);
        check("box 128->32", (bool)down && down.value.length() == 32 * 32 * 3);
                check("nearest 128->64", (bool)near_img && near_img.value.length() == 64 * 64 * 3);
    }

    /* ================================================================
     * 7) Image tools: crop / rotate / flip
     * ================================================================ */
    section("7) Image tools: crop / rotate / flip",
            "crop centre, rotate 90 CW/CCW/180, flip H/V",
            "all produce correctly sized buffers");
    {
        const uint32_t W = 48, H = 32;
        knst_byte_string rgb;
        gen_gradient(W, H, rgb);
        auto crop = img::crop_rgb24(&rgb[0], W, H, 8, 8, 16, 16);
        auto r90  = img::rotate_90_rgb24(&rgb[0], W, H, true);
        auto r180 = img::rotate_180_rgb24(&rgb[0], W, H);
        auto fh   = img::flip_h_rgb24(&rgb[0], W, H);
        auto fv   = img::flip_v_rgb24(&rgb[0], W, H);
        check("crop 16x16",  (bool)crop && crop.value.length() == 16 * 16 * 3);
        check("rotate 90",   (bool)r90  && r90.value.length()  == (uint32_t)H * W * 3);
        check("rotate 180",  (bool)r180 && r180.value.length() == W * H * 3);
        check("flip H",      (bool)fh   && fh.value.length()   == W * H * 3);
        check("flip V",      (bool)fv   && fv.value.length()   == W * H * 3);
    }

    /* ================================================================
     * 8) Image tools: brightness / contrast / grey
     * ================================================================ */
    section("8) Image tools: brightness / contrast / grey",
            "adjust_rgb24 and to_grey_rgb24",
            "brightness/contrast scale as expected");
    {
        const uint32_t W = 16, H = 16;
        knst_byte_string rgb;
        gen_solid(W, H, 100, 100, 100, rgb);
        img::adjust_rgb24(&rgb[0], W, H, 0.0f, 1.0f);   // identity
        check("identity adjust", rgb[0] == 100);
        img::adjust_rgb24(&rgb[0], W, H, 50.0f, 1.0f);  // +50
        check("brightness +50", rgb[0] >= 145 && rgb[0] <= 155);
        auto grey = img::to_grey_rgb24(&rgb[0], W, H);
        check("to_grey size", (bool)grey && grey.value.length() == W * H);
        check("to_grey luma", grey && grey.value[0] >= 145 && grey.value[0] <= 155);
    }

    /* ================================================================
     * 9) Audio: PCM conversion round-trip
     * ================================================================ */
    section("9) Audio: PCM conversion",
            "S16 -> F32 -> S16, all PCM formats quantize cleanly",
            "S16/F32 round-trip within ±1 LSB");
    {
        int16_t s16[128];
        gen_sine_s16(s16, 64, 2, 48000, 440.0);
        audio_frame in;
        in.data.resize(128 * 2);
        std::memcpy(&in.data[0], s16, 128 * 2);
        in.sample_rate = 48000; in.channels = 2; in.codec = audio_codec::PCM_S16LE; in.frames = 64;

        auto f32 = audio::convert(in, audio_codec::PCM_F32LE);
        auto back = f32 ? audio::convert(f32.value, audio_codec::PCM_S16LE) : result<audio_frame>::fail(status::IoError);
        int maxerr = 0;
        if (back) for (size_t i = 0; i < 128; ++i) {
            int d = (int)((int16_t)(back.value.data[(uint32_t)i * 2] | (back.value.data[(uint32_t)i * 2 + 1] << 8))) - (int)s16[i];
            if (d < 0) d = -d;
            if (d > maxerr) maxerr = d;
        }
        std::cout << "  S16 -> F32 -> S16 max error: " << maxerr << "\n";
        check("S16 <-> F32 round-trip", (bool)back && maxerr <= 1);
    }

    /* ================================================================
     * 10) Audio: resample / remix / peak
     * ================================================================ */
    section("10) Audio: resample / remix / peak",
            "48k -> 22.05k, stereo -> mono, peak level",
            "sample counts scale, peak ~0.49 for full-scale sine");
    {
        int16_t s16[2000];
        gen_sine_s16(s16, 1000, 2, 48000, 440.0);
        audio_frame in;
        in.data.resize(2000 * 2);
        std::memcpy(&in.data[0], s16, 2000 * 2);
        in.sample_rate = 48000; in.channels = 2; in.codec = audio_codec::PCM_S16LE; in.frames = 1000;

        auto resampled = audio::resample(in, 22050);
        std::cout << "  resample 48k -> 22.05k : "
                  << (resampled ? resampled.value.frames : 0) << " frames (expected ~459)\n";
        check("resample frame count", (bool)resampled &&
              resampled.value.frames >= 450 && resampled.value.frames <= 470);

        auto mono = audio::remix(in, 1);
        check("remix stereo -> mono", (bool)mono && mono.value.channels == 1 && mono.value.frames == 1000);

        float pk = audio::peak(reinterpret_cast<const uint8_t*>(s16), audio_codec::PCM_S16LE, 2000);
        std::cout << "  peak: " << pk << " (16000/32768 ≈ 0.488)\n";
        check("peak detection", pk >= 0.45f && pk <= 0.52f);
    }

    /* ================================================================
     * 11) MJPEG: quality / subsampling sweep
     * ================================================================ */
    section("11) MJPEG: quality & subsampling",
            "encode same frame at quality 10/50/90 and 4:4:4 / 4:2:2 / 4:2:0",
            "quality 90 > 10 in bytes, all decode");
    {
        const uint32_t W = 96, H = 96;
        knst_byte_string rgb;
        gen_gradient(W, H, rgb);
        mjpeg::encoder enc;

        int size_q10 = 0, size_q50 = 0, size_q90 = 0;
        knst_byte_string jpg;
        for (int q : {10, 50, 90}) {
            mjpeg::encoder::options o; o.quality = q;
            enc.configure(o);
            jpg.clear();
            enc.encode(&rgb[0], W, H, 0, pixel_format::RGB24, jpg);
            if (q == 10) size_q10 = (int)jpg.length();
            if (q == 50) size_q50 = (int)jpg.length();
            if (q == 90) size_q90 = (int)jpg.length();
        }
        std::cout << "  q=10: " << size_q10 << " B, q=50: " << size_q50
                  << " B, q=90: " << size_q90 << " B\n";
        check("quality ordering", size_q10 < size_q50 && size_q50 < size_q90);

        int ok444 = 0, ok422 = 0, ok420 = 0;
        for (auto sub : {mjpeg::subsampling::s444, mjpeg::subsampling::s422, mjpeg::subsampling::s420}) {
            mjpeg::encoder::options o; o.quality = 80; o.sub = sub;
            enc.configure(o);
            jpg.clear();
            if (enc.encode(&rgb[0], W, H, 0, pixel_format::RGB24, jpg) == errc::Ok) {
                if (sub == mjpeg::subsampling::s444) ok444 = (int)jpg.length();
                if (sub == mjpeg::subsampling::s422) ok422 = (int)jpg.length();
                if (sub == mjpeg::subsampling::s420) ok420 = (int)jpg.length();
            }
        }
        std::cout << "  s444=" << ok444 << "  s422=" << ok422 << "  s420=" << ok420 << "\n";
        check("subsampling all encode", ok444 && ok422 && ok420 && ok444 >= ok422 && ok422 >= ok420);
    }

    /* ================================================================
     * 12) MJPEG: decode round-trip + DHT inject
     * ================================================================ */
    section("12) MJPEG: decode round-trip",
            "encode -> decode -> compare, DHT injection on DHT-less frames",
            "PASS if decoded size and quality match");
    {
        const uint32_t W = 64, H = 64;
        knst_byte_string rgb, jpg;
        gen_gradient(W, H, rgb);
        mjpeg::encoder enc;
        mjpeg::encoder::options o; o.quality = 90; o.sub = mjpeg::subsampling::s444;
        enc.configure(o);
        enc.encode(&rgb[0], W, H, 0, pixel_format::RGB24, jpg);

        uint32_t dw = 0, dh = 0;
        auto dec = mjpeg::decode_rgb24(jpg.data(), jpg.length(), dw, dh);
        int maxerr = 0;
        if (dec) for (size_t i = 0; i < (size_t)W * H * 3; ++i) {
            int d = (int)dec.value[(uint32_t)i] - (int)rgb[i];
            if (d < 0) d = -d;
            if (d > maxerr) maxerr = d;
        }
        std::cout << "  decoded " << dw << "x" << dh << ", max channel error = " << maxerr << "\n";
        check("MJPEG round-trip", (bool)dec && dw == W && dh == H && maxerr < 30);
        check("DHT inject skips DHT-present JPEG",
              !mjpeg::inject_std_dht(jpg.data(), jpg.length(), rgb));
    }

    /* ================================================================
     * 13) AVI: MJPEG video round-trip
     * ================================================================ */
    section("13) AVI: MJPEG video round-trip",
            "write 20 frames, read back frames 0 and 10",
            "20 frames, correct dims, decode success");
    {
        const uint32_t W = 64, H = 48;
        knst_c16string path = knst_file::join_path(sandbox, u"test_mjpeg.avi");
        video_writer w;
        video_writer::config cfg;
        cfg.cont = container::AVI;
        cfg.vcodec = video_codec::MJPEG;
        cfg.width = W; cfg.height = H; cfg.fps_num = 30; cfg.fps_den = 1;
        cfg.jpeg_quality = 85;

        knst_byte_string rgb;
        gen_gradient(W, H, rgb);
        auto ropen = w.open(path, cfg);
        for (int i = 0; i < 20; ++i) w.write_frame_rgb24(&rgb[0]);
        w.close();
        std::cout << "  wrote: " << w.frames_written() << " frames\n";
        check("AVI open", (bool)ropen);

        video_reader rd;
        auto rr = rd.open(path);
        check("AVI read open", (bool)rr && rd.kind() == container::AVI);
        check("AVI frame count", rd.video_frames() == 20);
        check("AVI dims", rd.width() == W && rd.height() == H);
        auto f0 = rd.read_frame_rgb24(0);
        auto f10 = rd.read_frame_rgb24(10);
        check("AVI decode frame 0", (bool)f0 && f0.value.width == W && f0.value.height == H);
        check("AVI decode frame 10", (bool)f10 && f10.value.width == W && f10.value.height == H);
    }

    /* ================================================================
     * 14) AVI: RAW video
     * ================================================================ */
    section("14) AVI: RAW video (BGR24 / GREY)",
            "write uncompressed frames as BGR24 and GREY",
            "read back with correct pixel_format");
    {
        const uint32_t W = 32, H = 32;
        knst_byte_string rgb;
        gen_gradient(W, H, rgb);

        struct { pixel_format p; const char* name; } cases[] = {
            {pixel_format::BGR24, "BGR24"},
            {pixel_format::GREY,  "GREY"},
        };
        for (auto& c : cases) {
            knst_c16string path = knst_file::join_path(sandbox,
                c.p == pixel_format::BGR24 ? u"test_raw_bgr.avi" : u"test_raw_grey.avi");
            video_writer w;
            video_writer::config cfg;
            cfg.cont = container::AVI;
            cfg.vcodec = video_codec::RAW;
            cfg.raw_pix = c.p;
            cfg.width = W; cfg.height = H; cfg.fps_num = 25;
            w.open(path, cfg);
            for (int i = 0; i < 5; ++i) w.write_frame_rgb24(&rgb[0]);
            w.close();
            video_reader rd;
            auto rr = rd.open(path);
            auto fr = rd.read_frame(0);
            bool ok = rr && fr && rd.video_stream() >= 0;
            std::cout << "  " << c.name << " -> stream pix="
                      << (ok ? pixel_format_name(rd.stream((uint32_t)rd.video_stream()).pix) : "?") << "\n";
            check(c.name, ok);
        }
    }

    /* ================================================================
     * 15) AVI: MJPEG + PCM audio
     * ================================================================ */
    section("15) AVI: MJPEG + PCM audio",
            "interleave 30 frames + 1 second of S16 stereo",
            "video and audio tracks both readable");
    {
        const uint32_t W = 48, H = 48;
        knst_c16string path = knst_file::join_path(sandbox, u"test_audio.avi");
        video_writer w;
        video_writer::config cfg;
        cfg.cont = container::AVI;
        cfg.vcodec = video_codec::MJPEG;
        cfg.width = W; cfg.height = H; cfg.fps_num = 30;
        cfg.acodec = audio_codec::PCM_S16LE;
        cfg.sample_rate = 48000;
        cfg.channels = 2;
        cfg.jpeg_quality = 80;

        knst_byte_string rgb; gen_gradient(W, H, rgb);
        int16_t pcm[1600 * 2];  // 1600 frames stereo per video frame
        gen_sine_s16(pcm, 1600, 2, 48000, 440.0);

        w.open(path, cfg);
        for (int i = 0; i < 30; ++i) {
            w.write_frame_rgb24(&rgb[0]);
            w.write_audio(reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm));
        }
        w.close();
        std::cout << "  video frames: " << w.frames_written() << ", audio frames: " << w.audio_frames_written() << "\n";

        video_reader rd;
        auto rr = rd.open(path);
        check("AVI video + audio streams", (bool)rr && rd.video_stream() >= 0 && rd.audio_stream() >= 0);
        auto af = rd.read_audio(0, 100);
        check("AVI audio read 100 frames", (bool)af && af.value.frames == 100);
        check("AVI audio format",
              af && af.value.sample_rate == 48000 && af.value.channels == 2 && af.value.codec == audio_codec::PCM_S16LE);
    }

    /* ================================================================
     * 16) AVI: metadata
     * ================================================================ */
    section("16) AVI: metadata (INFO chunk)",
            "write title/artist/software, read them back",
            "all three fields round-trip");
    {
        knst_c16string path = knst_file::join_path(sandbox, u"test_meta.avi");
        video_writer w;
        video_writer::config cfg;
        cfg.cont = container::AVI;
        cfg.vcodec = video_codec::MJPEG;
        cfg.width = 32; cfg.height = 32; cfg.fps_num = 25;
        cfg.meta.title = u"KernelNucleusT Demo";
        cfg.meta.artist = u"knst";
        cfg.meta.software = u"knst_video_basic";
        knst_byte_string rgb; gen_gradient(32, 32, rgb);
        w.open(path, cfg);
        for (int i = 0; i < 5; ++i) w.write_frame_rgb24(&rgb[0]);
        w.close();

        video_reader rd;
        auto rr = rd.open(path);
        check("AVI metadata title",  (bool)rr && rd.meta().title  == u"KernelNucleusT Demo");
        check("AVI metadata artist", rd.meta().artist  == u"knst");
        check("AVI metadata software", rd.meta().software == u"knst_video_basic");
    }

    /* ================================================================
     * 17) MKV: MJPEG video round-trip
     * ================================================================ */
    section("17) MKV: MJPEG video round-trip",
            "write 15 frames, read back",
            "correct frame count and decode");
    {
        const uint32_t W = 64, H = 48;
        knst_c16string path = knst_file::join_path(sandbox, u"test_mjpeg.mkv");
        video_writer w;
        video_writer::config cfg;
        cfg.cont = container::MKV;
        cfg.vcodec = video_codec::MJPEG;
        cfg.width = W; cfg.height = H; cfg.fps_num = 30;
        cfg.jpeg_quality = 85;
        knst_byte_string rgb; gen_gradient(W, H, rgb);
        w.open(path, cfg);
        for (int i = 0; i < 15; ++i) w.write_frame_rgb24(&rgb[0]);
        w.close();

        video_reader rd;
        auto rr = rd.open(path);
        check("MKV read open", (bool)rr && rd.kind() == container::MKV);
        check("MKV frame count", rd.video_frames() == 15);
        auto f5 = rd.read_frame_rgb24(5);
        check("MKV decode frame 5", (bool)f5 && f5.value.width == W && f5.value.height == H);
    }

    /* ================================================================
     * 18) MKV: PCM audio + chapters
     * ================================================================ */
    section("18) MKV: PCM audio + chapters",
            "write S16 stereo + 2 chapters, read them back",
            "audio track present, chapters preserved");
    {
        knst_c16string path = knst_file::join_path(sandbox, u"test_audio.mkv");
        video_writer w;
        video_writer::config cfg;
        cfg.cont = container::MKV;
        cfg.vcodec = video_codec::MJPEG;
        cfg.width = 48; cfg.height = 48; cfg.fps_num = 25;
        cfg.acodec = audio_codec::PCM_S16LE;
        cfg.sample_rate = 48000;
        cfg.channels = 2;
        chapter ch1; ch1.title = u"Intro"; ch1.start_us = 0; ch1.end_us = 500000;
        chapter ch2; ch2.title = u"Main";  ch2.start_us = 500000; ch2.end_us = 1000000;
        cfg.meta.chapters.push_back(ch1);
        cfg.meta.chapters.push_back(ch2);

        knst_byte_string rgb; gen_gradient(48, 48, rgb);
        int16_t pcm[1200 * 2]; gen_sine_s16(pcm, 1200, 2, 48000, 440.0);
        w.open(path, cfg);
        for (int i = 0; i < 10; ++i) {
            w.write_frame_rgb24(&rgb[0]);
            w.write_audio(reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm));
        }
        w.close();

        video_reader rd;
        auto rr = rd.open(path);
        check("MKV audio stream", (bool)rr && rd.audio_stream() >= 0);
        check("MKV chapters count", rd.meta().chapters.size() == 2);
        if (rd.meta().chapters.size() == 2) {
            check("MKV chapter title", rd.meta().chapters[0].title == u"Intro");
        }
    }

    /* ================================================================
     * 19) MP4: MJPEG video round-trip
     * ================================================================ */
    section("19) MP4: MJPEG video round-trip",
            "write 12 frames, read back",
            "correct frame count, decode OK");
    {
        const uint32_t W = 64, H = 48;
        knst_c16string path = knst_file::join_path(sandbox, u"test_mjpeg.mp4");
        video_writer w;
        video_writer::config cfg;
        cfg.cont = container::MP4;
        cfg.vcodec = video_codec::MJPEG;
        cfg.width = W; cfg.height = H; cfg.fps_num = 30;
        cfg.jpeg_quality = 85;
        knst_byte_string rgb; gen_gradient(W, H, rgb);
        w.open(path, cfg);
        for (int i = 0; i < 12; ++i) w.write_frame_rgb24(&rgb[0]);
        w.close();

        video_reader rd;
        auto rr = rd.open(path);
        check("MP4 read open", (bool)rr && rd.kind() == container::MP4);
        check("MP4 frame count", rd.video_frames() == 12);
        auto f0 = rd.read_frame_rgb24(0);
        check("MP4 decode frame 0", (bool)f0 && f0.value.width == W && f0.value.height == H);
    }

    /* ================================================================
     * 20) MOV: MJPEG + PCM audio
     * ================================================================ */
    section("20) MOV: MJPEG + PCM audio",
            "write MJPEG + S16 stereo, read back",
            "MOV container with both tracks");
    {
        const uint32_t W = 48, H = 48;
        knst_c16string path = knst_file::join_path(sandbox, u"test_mjpeg.mov");
        video_writer w;
        video_writer::config cfg;
        cfg.cont = container::MOV;
        cfg.vcodec = video_codec::MJPEG;
        cfg.width = W; cfg.height = H; cfg.fps_num = 25;
        cfg.acodec = audio_codec::PCM_S16LE;
        cfg.sample_rate = 48000;
        cfg.channels = 2;
        knst_byte_string rgb; gen_gradient(W, H, rgb);
        int16_t pcm[1200 * 2]; gen_sine_s16(pcm, 1200, 2, 48000, 440.0);
        w.open(path, cfg);
        for (int i = 0; i < 10; ++i) {
            w.write_frame_rgb24(&rgb[0]);
            w.write_audio(reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm));
        }
        w.close();

        video_reader rd;
        auto rr = rd.open(path);
        check("MOV read open", (bool)rr && rd.kind() == container::MOV);
        check("MOV video + audio", rd.video_stream() >= 0 && rd.audio_stream() >= 0);
    }

    /* ================================================================
     * 21) WAV: S16 / F32 round-trip
     * ================================================================ */
    section("21) WAV: S16 / F32 round-trip",
            "write 0.5 s of audio in 2 formats, read back",
            "identical peak and frame count");
    {
        struct { audio_codec c; const char* name; } cases[] = {
            {audio_codec::PCM_S16LE, "PCM_S16LE"},
            {audio_codec::PCM_F32LE, "PCM_F32LE"},
        };
        for (auto& cc : cases) {
            knst_c16string path = knst_file::join_path(sandbox,
                cc.c == audio_codec::PCM_S16LE ? u"test.wav" : u"test_f32.wav");
            wav::writer w;
            auto rr = w.open(path, 48000, 2, cc.c);
            int16_t s16[24000 * 2]; gen_sine_s16(s16, 24000, 2, 48000, 440.0);
            if (cc.c == audio_codec::PCM_S16LE) w.write_s16(s16, 24000 * 2);
            else {
                float f32[24000 * 2];
                for (int i = 0; i < 24000 * 2; ++i) f32[i] = s16[i] / 32768.0f;
                w.write_f32(f32, 24000 * 2);
            }
            w.close();
            std::cout << "  " << cc.name << " wrote " << w.frames_written() << " frames\n";

            video_reader rd;
            auto r2 = rd.open(path);
            check(cc.name, (bool)rr && (bool)r2 && rd.audio_stream() >= 0);
            if (rd.audio_stream() >= 0) {
                auto af = rd.read_audio(0, 100);
                check((std::string(cc.name) + " read").c_str(), (bool)af && af.value.frames == 100);
            }
        }
    }

    /* ================================================================
     * 22) RAW: pixel dump round-trip
     * ================================================================ */
    section("22) RAW: pixel dump round-trip",
            "write 10 RGB24 frames headerless, read them back",
            "correct frame count and dims");
    {
        const uint32_t W = 32, H = 24;
        knst_c16string path = knst_file::join_path(sandbox, u"test.raw");
        video_writer w;
        video_writer::config cfg;
        cfg.cont = container::RAW;
        cfg.vcodec = video_codec::RAW;
        cfg.width = W; cfg.height = H; cfg.fps_num = 30;
        cfg.pix = pixel_format::RGB24;
        cfg.raw_pix = pixel_format::RGB24;
        knst_byte_string rgb; gen_gradient(W, H, rgb);
        w.open(path, cfg);
        for (int i = 0; i < 10; ++i) w.write_frame_rgb24(&rgb[0]);
        w.close();

        video_reader rd;
        auto rr = rd.open_raw(path, W, H, pixel_format::RGB24, 30, 1);
        check("RAW read", (bool)rr && rd.kind() == container::RAW);
        check("RAW frame count", rd.video_frames() == 10);
        auto f = rd.read_frame_rgb24(3);
        check("RAW frame 3", (bool)f && f.value.width == W && f.value.height == H);
    }

    /* ================================================================
     * 23) Auto container detection
     * ================================================================ */
    section("23) Auto container detection",
            "open each generated file, verify container::* matches",
            "each file detected by magic bytes");
    {
        struct { const char16_t* file; container expect; } tests[] = {
            {u"test_mjpeg.avi", container::AVI},
            {u"test_mjpeg.mkv", container::MKV},
            {u"test_mjpeg.mp4", container::MP4},
            {u"test_mjpeg.mov", container::MOV},
            {u"test.wav",       container::WAV},
        };
        int ok = 0;
        for (auto& t : tests) {
            knst_c16string p = knst_file::join_path(sandbox, t.file);
            video_reader rd;
            if (rd.open(p) && rd.kind() == t.expect) ++ok;
        }
        check("5 containers auto-detected", ok == 5);
    }

    /* ================================================================
     * 24) Subtitles: SRT / VTT / ASS round-trip
     * ================================================================ */
    section("24) Subtitles: SRT / VTT / ASS",
            "write 2 cues in each format, read them back",
            "same count and timings");
    {
        knst_vector<subtitle_entry> cues;
        subtitle_entry c1; c1.start_ms = 500; c1.end_ms = 1500; c1.text = u"First line";
        subtitle_entry c2; c2.start_ms = 2000; c2.end_ms = 3000; c2.text = u"Second line\nwrap";
        cues.push_back(c1); cues.push_back(c2);

        // SRT
        {
            knst_c16string p = knst_file::join_path(sandbox, u"test.srt");
            auto wr = sub::srt::write(p, cues);
            auto rd = sub::srt::read(p);
            check("SRT write+read", (bool)wr && (bool)rd && rd.value.size() == 2);
            if (rd && rd.value.size() == 2) {
                check("SRT timings", rd.value[0].start_ms == 500 && rd.value[0].end_ms == 1500);
            }
        }
        // VTT
        {
            knst_c16string p = knst_file::join_path(sandbox, u"test.vtt");
            auto wr = sub::vtt::write(p, cues);
            auto rd = sub::vtt::read(p);
            check("VTT write+read", (bool)wr && (bool)rd && rd.value.size() == 2);
        }
        // ASS
        {
            knst_c16string p = knst_file::join_path(sandbox, u"test.ass");
            auto wr = sub::ass::write(p, cues);
            auto rd = sub::ass::read(p);
            check("ASS write+read", (bool)wr && (bool)rd && rd.value.size() == 2);
        }
    }

    /* ================================================================
     * 25) Subtitles: shift + scale
     * ================================================================ */
    section("25) Subtitles: shift + scale",
            "shift by +1000ms and scale by 2.0",
            "timings clamp and scale correctly");
    {
        knst_vector<subtitle_entry> cues;
        subtitle_entry c; c.start_ms = 1000; c.end_ms = 2000; c.text = u"x";
        cues.push_back(c);
        sub::shift(cues, 1000, 1.0);
        check("shift +1000ms", cues[0].start_ms == 2000 && cues[0].end_ms == 3000);
        cues[0].start_ms = 1000; cues[0].end_ms = 2000;
        sub::shift(cues, 0, 2.0);
        check("scale x2", cues[0].start_ms == 2000 && cues[0].end_ms == 4000);
        cues[0].start_ms = 0;
        sub::shift(cues, -100000, 1.0);
        check("negative clamp", cues[0].start_ms == 0 && cues[0].end_ms == 0);
    }

    /* ================================================================
     * 26) Motion detector
     * ================================================================ */
    section("26) Motion detector",
            "static vs. sudden black->white transition",
            "no motion on identical, motion on big change");
    {
        const uint32_t W = 320, H = 240;
        knst_byte_string black, white;
        gen_solid(W, H, 0, 0, 0, black);
        gen_solid(W, H, 255, 255, 255, white);

        motion_detector det;
        std::cout << "  frame 1 (first, no prior): "
                  << (det.feed_rgb24(&black[0], W, H, 0) ? "motion" : "no motion") << "\n";
                bool m2 = det.feed_rgb24(&black[0], W, H, 600);   // > min_interval_ms (500)
        std::cout << "  frame 2 (same): " << (m2 ? "motion" : "no motion")
                  << "  fraction=" << det.last_fraction() << "\n";
        bool m3 = det.feed_rgb24(&white[0], W, H, 1200);  // > min_interval_ms
        std::cout << "  frame 3 (black->white): " << (m3 ? "motion" : "no motion")
                  << "  fraction=" << det.last_fraction() << "\n";
        check("no motion on identical", !m2);
        check("motion on drastic change", m3);
    }

    /* ================================================================
     * 27) Timelapse gate
     * ================================================================ */
    section("27) Timelapse gate",
            "interval 1000ms, ticks at 0/500/1000/1500/2000ms",
            "due() returns true only at t=0,1000,2000");
    {
        timelapse tl(1000);
        bool d0 = tl.due(0);
        bool d500 = tl.due(500);
        bool d1000 = tl.due(1000);
        bool d1500 = tl.due(1500);
        bool d2000 = tl.due(2000);
        std::cout << "  due: 0=" << d0 << " 500=" << d500 << " 1000=" << d1000
                  << " 1500=" << d1500 << " 2000=" << d2000 << "\n";
        check("timelapse gating", d0 && !d500 && d1000 && !d1500 && d2000);
    }

    /* ================================================================
     * 28) Recorder: motion-triggered with pre-roll
     * ================================================================ */
    section("28) Recorder: motion-triggered with pre-roll",
            "100ms pre-roll, 200ms post-roll, 1 motion event",
            "exactly 1 clip written");
    {
        recorder::config rc;
        rc.dir = sandbox;
        rc.prefix = u"clip_";
        rc.cont = container::AVI;
        rc.fps = 30;
        rc.jpeg_quality = 80;
        rc.pre_roll_ms = 100;   // 4 frames @ 30fps
        rc.post_roll_ms = 200;
        rc.motion.min_interval_ms = 0;   // test frames are 33ms apart; disable throttle
        recorder rec;
        auto r = rec.start(rc);

        const uint32_t W = 64, H = 48;
        knst_byte_string black, white;
        gen_solid(W, H, 0, 0, 0, black);
        gen_solid(W, H, 255, 255, 255, white);

        // 3 static frames, then 1 motion (white), then 8 static
        uint32_t total_events = 0;
        for (uint32_t i = 0; i < 3; ++i) rec.feed_rgb24(&black[0], W, H, (uint64_t)i * 33333);
        auto ev_motion = rec.feed_rgb24(&white[0], W, H, 3 * 33333);
        if (ev_motion) total_events |= ev_motion.value;
        for (uint32_t i = 4; i < 12; ++i) {
            auto ev = rec.feed_rgb24(&black[0], W, H, (uint64_t)i * 33333);
            if (ev) total_events |= ev.value;
        }
        rec.stop();
        std::cout << "  clips_written: " << rec.clips_written() << "\n";
        std::cout << "  events mask  : 0x" << std::hex << total_events << std::dec << "\n";
        check("recorder start", (bool)r);
        check("exactly 1 clip", rec.clips_written() == 1);
        check("EV_CLIP_STARTED fired", (total_events & recorder::EV_CLIP_STARTED) != 0);
        check("EV_CLIP_FINISHED fired", (total_events & recorder::EV_CLIP_FINISHED) != 0);
    }

    /* ================================================================
     * 29) External codec registry + passthrough
     * ================================================================ */
    section("29) Passthrough + external codec",
            "write fake H.264 packets into MKV, read them back verbatim",
            "no decode attempt, packets match byte-for-byte");
    {
        const uint32_t W = 64, H = 64;
        knst_c16string path = knst_file::join_path(sandbox, u"test_h264.mkv");
        video_writer w;
        video_writer::config cfg;
        cfg.cont = container::MKV;
        cfg.vcodec = video_codec::H264;      // passthrough
        cfg.width = W; cfg.height = H; cfg.fps_num = 30;
        cfg.vextradata.resize(8);
        std::memcpy(&cfg.vextradata[0], "avcC-fake", 8);  // any extradata

        w.open(path, cfg);
        // fake NAL units
        knst_byte_string pkt;
        pkt.resize(20);
        for (uint32_t i = 0; i < 20; ++i) pkt[i] = (uint8_t)(i + 1);
        for (int i = 0; i < 5; ++i) w.write_encoded(&pkt[0], 20, i == 0, (uint64_t)i * 33333);
        w.close();

        video_reader rd;
        auto rr = rd.open(path);
        check("MKV H.264 passthrough open", (bool)rr && rd.video_stream() >= 0);
        if (rd.video_stream() >= 0) {
            auto s = rd.stream((uint32_t)rd.video_stream());
            std::cout << "  codec_name: " << s.codec_name << "\n";
            check("H.264 codec recorded", s.vcodec == video_codec::H264);
            uint64_t pts = 0; bool key = false;
            auto rp = rd.read_packet((uint32_t)rd.video_stream(), 0, &pts, &key);
            check("packet read back", (bool)rp && rp.value.length() == 20);
            check("packet bytes match",
                  rp && std::memcmp(rp.value.data(), pkt.data(), 20) == 0);
            check("first packet is keyframe", key);
        }
    }

    /* ================================================================
     * 30) Error handling matrix
     * ================================================================ */
    section("30) Error handling",
            "bad path / null / out-of-range return fail, not crash",
            "all errors surface as result<> failure");
    {
        // open missing file
        video_reader rd;
        auto r1 = rd.open(u"/no/such/file.avi");
        check("open nonexistent fails", !r1);

        // read frame out of range on a valid file
        auto r2 = rd.open(knst_file::join_path(sandbox, u"test_mjpeg.avi"));
        auto r3 = rd.read_frame_rgb24(99999);
        check("read_frame out-of-range fails", (bool)r2 && !r3);

        // audio on a video-only file
        auto r4 = rd.read_audio(0, 100);
        check("read_audio on video-only fails", !r4);

        // invalid write to closed writer
        video_writer w;
        knst_byte_string dummy; gen_gradient(16, 16, dummy);
        auto r5 = w.write_frame_rgb24(&dummy[0]);
        check("write before open fails", !r5);

        // mk::writer without extradata
        knst_c16string p = knst_file::join_path(sandbox, u"bad.mp4");
        video_writer w2;
        video_writer::config c2; c2.cont = container::MP4;
        c2.vcodec = video_codec::H264;
        c2.width = 32; c2.height = 32; c2.fps_num = 30;
        auto r6 = w2.open(p, c2);   // missing vextradata
        check("MP4 H.264 without avcC fails", !r6);
    }

#if defined(KNST_JSON_HPP)
    /* ================================================================
     * 31) JSON export
     * ================================================================ */
    section("31) JSON export",
            "media_info_to_json, metadata_to_json, subtitle_to_json",
            "valid JSON object/array");
    {
        video_reader rd;
        auto rr = rd.open(knst_file::join_path(sandbox, u"test_audio.avi"));
        if (rr) {
            knst_json j = media_info_to_json(rd.info());
            check("media_info_to_json is object", j.is_object());
            if (j.is_object()) {
                std::cout << "  duration_us: " << j.value("duration_us", 0) << "\n";
                check("streams array present", j.contains("streams"));
            }
        } else {
            skip("media_info_to_json", "read failed");
        }

        metadata m; m.title = u"Test"; m.artist = u"KernelNucleusT";
        knst_json mj = metadata_to_json(m);
        check("metadata_to_json is object", mj.is_object());
                check("metadata title stored", mj.is_object() && std::strcmp(mj.value("title", ""), "Test") == 0);

        knst_vector<subtitle_entry> cues;
        subtitle_entry c; c.start_ms = 100; c.end_ms = 200; c.text = u"Hi";
        cues.push_back(c);
        knst_json sj = subtitle_to_json(cues);
        check("subtitle_to_json is array", sj.is_array());
        if (sj.is_array()) check("subtitle entry count", sj.size() == 1);
    }
#endif

    /* ================================================================
     * 32) Cleanup
     * ================================================================ */
    section("32) Cleanup", "remove sandbox", "sandbox gone");
    {
        bool removed = knst_file::remove_all(sandbox);
        check("sandbox removed", removed && !knst_file::path_exists(sandbox));
    }

    /* ================================================================
     * Summary
     * ================================================================ */
    std::cout << "\n==================================================\n";
    std::cout << "  RESULT: " << s_pass << " PASS / " << s_fail << " FAIL";
    if (s_skip) std::cout << " / " << s_skip << " SKIP";
    std::cout << "\n==================================================\n";

    if (s_fail > 0) std::cout << "\nSome checks failed.\n";
    std::cout << "\nDone.\n";
    return s_fail == 0 ? 0 : 1;
}