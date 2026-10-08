// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
knst_image_loader.hpp
----------------------------

    Self-contained image loader (no zlib / libpng / libjpeg). Own DEFLATE, JPEG, GIF, BMP, TGA, PNM, ICO decoders.

    FORMATS (detected from magic bytes, not from the file extension)
      PNG   : gray / RGB / palette / gray+alpha / RGBA, 1-16 bit, Adam7 interlace, tRNS (palette / color-key)
      JPEG  : baseline, extended sequential, progressive (Huffman, 8 bit), gray / YCbCr / RGB / CMYK / YCCK,
              restart intervals, all subsampling layouts, libjpeg-style fancy upsampling, EXIF orientation
      GIF   : first frame (GIF87a / GIF89a), transparency, interlace
      BMP   : 1/2/4/8/16/24/32 bit, RLE4 / RLE8, BITFIELDS, OS/2 core, INFO / V4 / V5, top-down / bottom-up, embedded PNG / JPEG
      TGA   : types 1/2/3/9/10/11 (palette / truecolor / gray, RLE), 8/15/16/24/32 bit, all orientation bits
      PNM   : P1..P6 (ASCII + binary), P7 (PAM), maxval 1..65535, comments
      ICO/CUR : picks the best entry (see target_size), PNG and BMP(DIB + AND mask) entries, cursor hotspot
      Anything else : plug in your own decoder with knst_image_loader::register_decoder()
*/

#pragma once


#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <utility>
#include <thread>
#include <algorithm>
#include <functional>
#include <mutex>
#include <atomic>
#include <type_traits>


#if KNST_USING_PLATFORM_LINUX
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

#if KNST_USING_PLATFORM_WINDOWS
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <memoryapi.h>
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #include <emmintrin.h>
    #define KNST_HAS_SSE2 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
    #include <arm_neon.h>
    #define KNST_HAS_NEON 1
#endif

#ifndef KNST_IMAGE_MAX_DIMENSION
    #define KNST_IMAGE_MAX_DIMENSION 16384
#endif

#ifndef KNST_IMAGE_MAX_CUSTOM_DECODERS
    #define KNST_IMAGE_MAX_CUSTOM_DECODERS 16
#endif

#define KNST_BITMAP_16_16       (1 << 0)
#define KNST_BITMAP_24_24       (1 << 1)
#define KNST_BITMAP_32_32       (1 << 2)
#define KNST_BITMAP_48_48       (1 << 3)
#define KNST_BITMAP_64_64       (1 << 4)
#define KNST_BITMAP_96_96       (1 << 5)
#define KNST_BITMAP_128_128     (1 << 6)
#define KNST_BITMAP_256_256     (1 << 7)

#define KNST_BITMAP_OUTPUT_RGB   (1 << 8)
#define KNST_BITMAP_OUTPUT_RGBA  (1 << 9)
#define KNST_BITMAP_OUTPUT_BGR   (1 << 10)
#define KNST_BITMAP_OUTPUT_BGRA  (1 << 11)

#define KNST_BITMAP_GET_SIZE(flags)     ((flags) & 0xFF)
#define KNST_BITMAP_GET_FORMAT(flags)   ((flags) & 0xFF00)


static const uint32_t KNST_MAX_IMAGE_DIMENSION = KNST_IMAGE_MAX_DIMENSION;

inline int knst_bitmap_flag_to_size(int flags) noexcept { // Extracts the size from the bitmap flag. KNST_BITMAP_128_128 ==> 128, KNST_BITMAP_16_16 ==> 16. Unknown flag ==> 0
    int size_flag = flags & 0xFF;
    switch (size_flag) {
        case KNST_BITMAP_16_16: return 16;
        case KNST_BITMAP_24_24: return 24;
        case KNST_BITMAP_32_32: return 32;
        case KNST_BITMAP_48_48: return 48;
        case KNST_BITMAP_64_64: return 64;
        case KNST_BITMAP_96_96: return 96;
        case KNST_BITMAP_128_128: return 128;
        case KNST_BITMAP_256_256: return 256;
        default: return 0;
    }
}

#pragma pack(push, 1)
struct BMPHeader { // BMP file header. #pragma pack(push, 1) ==> no padding; fields are strictly byte-aligned (as required by the file format). Signature 0x4D42 ("BM"), followed by size, offset, width, height, bit depth, compression, etc.
    uint16_t signature;
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t data_offset;
    uint32_t header_size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_ppm;
    int32_t  y_ppm;
    uint32_t colors_used;
    uint32_t important_colors;
};
#pragma pack(pop)


// ====================================================================================================================
//  Public types
// ====================================================================================================================

enum knst_image_format : int { // detected container format
    KNST_IMAGE_FORMAT_UNKNOWN = 0,
    KNST_IMAGE_FORMAT_PNG     = 1,
    KNST_IMAGE_FORMAT_JPEG    = 2,
    KNST_IMAGE_FORMAT_GIF     = 3,
    KNST_IMAGE_FORMAT_BMP     = 4,
    KNST_IMAGE_FORMAT_ICO     = 5, // ICO and CUR
    KNST_IMAGE_FORMAT_TGA     = 6,
    KNST_IMAGE_FORMAT_PNM     = 7, // PBM / PGM / PPM / PAM
    KNST_IMAGE_FORMAT_CUSTOM  = 8  // decoded by a user registered decoder
};
#define KNST_IMAGE_FORMAT_MASK(fmt)   (1u << (unsigned)(fmt))
#define KNST_IMAGE_FORMAT_MASK_ALL    0xFFFFFFFFu

enum knst_image_error : int {
    KNST_IMAGE_OK = 0,
    KNST_IMAGE_ERR_EMPTY_INPUT,      // empty path / null data
    KNST_IMAGE_ERR_FILE_OPEN,        // file missing, not a regular file or unreadable
    KNST_IMAGE_ERR_UNKNOWN_FORMAT,   // magic bytes not recognised
    KNST_IMAGE_ERR_FORMAT_DISABLED,  // recognised, but excluded via allowed_formats
    KNST_IMAGE_ERR_DECODE,           // corrupt / truncated / unsupported variant
    KNST_IMAGE_ERR_LIMIT,            // larger than max_dimension (or 4 GB output)
    KNST_IMAGE_ERR_OUT_OF_MEMORY
};

inline const char* knst_image_error_string(int e) noexcept {
    switch (e) {
        case KNST_IMAGE_OK:                  return "ok";
        case KNST_IMAGE_ERR_EMPTY_INPUT:     return "empty input";
        case KNST_IMAGE_ERR_FILE_OPEN:       return "cannot open file";
        case KNST_IMAGE_ERR_UNKNOWN_FORMAT:  return "unknown image format";
        case KNST_IMAGE_ERR_FORMAT_DISABLED: return "image format disabled by options";
        case KNST_IMAGE_ERR_DECODE:          return "corrupt or unsupported image data";
        case KNST_IMAGE_ERR_LIMIT:           return "image exceeds size limit";
        case KNST_IMAGE_ERR_OUT_OF_MEMORY:   return "out of memory";
        default:                             return "unknown error";
    }
}

enum knst_image_resize_filter : int {
    KNST_IMAGE_RESIZE_NEAREST     = 0, // pixel art
    KNST_IMAGE_RESIZE_BOX         = 1, // area average (good for shrinking)
    KNST_IMAGE_RESIZE_TRIANGLE    = 2, // bilinear (scaled when shrinking)
    KNST_IMAGE_RESIZE_CATMULL_ROM = 3, // bicubic, sharp
    KNST_IMAGE_RESIZE_LANCZOS3    = 4  // best quality, slight ringing
};

struct knst_image_load_options { // every field has a sensible default
    int      output_format          = KNST_BITMAP_OUTPUT_RGBA; // KNST_BITMAP_OUTPUT_RGB / RGBA / BGR / BGRA
    int      target_size            = 0;     // ICO/CUR: pick the entry whose width is closest (0 = largest). Never resizes by itself
    uint32_t resize_width           = 0;     // 0 = keep. If only one of width/height is set the aspect ratio is kept
    uint32_t resize_height          = 0;
    bool     keep_aspect            = false; // both set + keep_aspect: fit inside the box instead of stretching
    int      resize_filter          = KNST_IMAGE_RESIZE_CATMULL_ROM;
    bool     flip_vertical          = false; // e.g. for OpenGL style bottom-left origin
    bool     flatten_alpha          = false; // composite onto background_* and make the image opaque
    uint8_t  background_r           = 0;
    uint8_t  background_g           = 0;
    uint8_t  background_b           = 0;
    bool     premultiply_alpha      = false; // output premultiplied colors (RGBA / BGRA)
    bool     apply_exif_orientation = true;  // JPEG only
    uint32_t max_dimension          = 0;     // 0 = KNST_IMAGE_MAX_DIMENSION. Rejects larger width/height
    uint32_t max_threads            = 0;     // 0 = auto, 1 = single threaded
    uint32_t allowed_formats        = KNST_IMAGE_FORMAT_MASK_ALL; // bit mask of KNST_IMAGE_FORMAT_MASK(fmt)
};

template<typename Alloc = knst_default_allocator>
struct knst_image_t { // result of knst_image_loader::load
    basic_byte_string<Alloc> pixels;          // width * height * channels bytes, tightly packed, top row first
    uint32_t width         = 0;
    uint32_t height        = 0;
    int      channels      = 0;
    int      output_format = 0;
    int      source_format = KNST_IMAGE_FORMAT_UNKNOWN;
    int      hotspot_x     = -1;
    int      hotspot_y     = -1;
    int      error         = KNST_IMAGE_OK;

    bool   valid() const noexcept { return error == KNST_IMAGE_OK && width != 0 && height != 0; }
    explicit operator bool() const noexcept { return valid(); }
    size_t stride() const noexcept { return (size_t)width * (size_t)channels; }
    size_t byte_size() const noexcept { return stride() * height; }
};

// Backward-compatible alias
using knst_image = knst_image_t<knst_default_allocator>;

struct knst_image_decode_result { // what a custom decoder fills in
    uint32_t         width       = 0;
    uint32_t         height      = 0;
    knst_byte_string rgba;                    // MUST be width*height*4 bytes, RGBA8, non-premultiplied (use knst_image_loader::allocate_rgba)
    int              size_hint   = 0;         // options.target_size (read only)
    int              orientation = 1;         // optional EXIF style orientation 1..8
    int              hotspot_x   = -1;        // optional
    int              hotspot_y   = -1;
};

typedef bool (*knst_image_sniff_fn)(const uint8_t* data, size_t size);                           // true if this decoder understands the data
typedef bool (*knst_image_decode_fn)(const uint8_t* data, size_t size, knst_image_decode_result& result);


// ====================================================================================================================
//  Small malloc based array (no exceptions, reports allocation failure)
// ====================================================================================================================
template<typename T>
class knst_img_array {
    static_assert(std::is_trivially_copyable<T>::value, "knst_img_array: trivially copyable types only");
    T* m_p = nullptr;
    size_t m_n = 0, m_cap = 0;
public:
    knst_img_array() = default;
    ~knst_img_array() { std::free(m_p); }
    knst_img_array(const knst_img_array&) = delete;
    knst_img_array& operator=(const knst_img_array&) = delete;
    knst_img_array(knst_img_array&& o) noexcept : m_p(o.m_p), m_n(o.m_n), m_cap(o.m_cap) { o.m_p = nullptr; o.m_n = o.m_cap = 0; }

    bool reserve(size_t c) {
        if (c <= m_cap) return true;
        if (c > (size_t)-1 / sizeof(T)) return false;
        T* np = static_cast<T*>(std::realloc(m_p, c * sizeof(T)));
        if (!np) return false;
        m_p = np; m_cap = c;
        return true;
    }
    bool resize(size_t n) { // new elements are zero
        if (!reserve(n)) return false;
        if (n > m_n) std::memset(static_cast<void*>(m_p + m_n), 0, (n - m_n) * sizeof(T));
        m_n = n;
        return true;
    }
    bool assign(size_t n, T v) {
        if (!reserve(n)) return false;
        for (size_t i = 0; i < n; i++) m_p[i] = v;
        m_n = n;
        return true;
    }
    bool append(const T* p, size_t n) {
        if (n == 0) return true;
        size_t need = m_n + n;
        if (need < m_n) return false;
        if (need > m_cap) { size_t g = m_cap + m_cap / 2; if (g < need) g = need; if (!reserve(g)) return false; }
        std::memcpy(static_cast<void*>(m_p + m_n), p, n * sizeof(T));
        m_n = need;
        return true;
    }
    void release() { std::free(m_p); m_p = nullptr; m_n = m_cap = 0; }
    void set_size(size_t n) { m_n = n <= m_cap ? n : m_cap; }
    T* data() { return m_p; }
    const T* data() const { return m_p; }
    size_t size() const { return m_n; }
    size_t capacity() const { return m_cap; }
    bool empty() const { return m_n == 0; }
    T& operator[](size_t i) { return m_p[i]; }
    const T& operator[](size_t i) const { return m_p[i]; }
};


// ====================================================================================================================
//  DEFLATE / zlib
// ====================================================================================================================
class KnstInflate {
public:
    // Legacy API: inflates a zlib stream and appends the result to output. 100MB output limit (decompression bomb protection).
    static bool Inflate(const uint8_t* input, size_t inputSize, knst_byte_string& output) {
        knst_img_array<uint8_t> tmp;
        if (!tmp.reserve(inputSize * 3 + 64)) return false;
        size_t len = 0;
        if (!InflateZlib(input, inputSize, tmp, len, 100u * 1024u * 1024u, false)) return false;
        if (len) output.append(tmp.data(), (uint32_t)len);
        return true;
    }

    // Validates the zlib header (CM=8, FCHECK, no FDICT) and inflates the deflate stream.
    // buf's capacity is the initial capacity; it grows up to maxOut. With tolerateOverflow data past maxOut is silently cut.
    // On success buf.size() == outLen.
    static bool InflateZlib(const uint8_t* in, size_t n, knst_img_array<uint8_t>& buf, size_t& outLen,
                            size_t maxOut, bool tolerateOverflow) {
        outLen = 0;
        if (in == nullptr || n < 2) return false;
        uint8_t cmf = in[0], flg = in[1];
        if ((cmf & 0x0F) != 8) return false;
        if ((cmf >> 4) > 7) return false;
        if (flg & 0x20) return false;
        if (((uint32_t)cmf * 256 + flg) % 31 != 0) return false;
        return InflateRaw(in + 2, n - 2, buf, outLen, maxOut, tolerateOverflow);
    }

    // Raw deflate stream (RFC 1951).
    static bool InflateRaw(const uint8_t* in, size_t n, knst_img_array<uint8_t>& buf, size_t& outLen,
                           size_t maxOut, bool tolerate) {
        outLen = 0;
        if (in == nullptr || maxOut == 0) return false;
        if (buf.capacity() == 0 && !buf.reserve(maxOut < 4096 ? maxOut : 4096)) return false;
        BitReader br(in, n);
        size_t o = 0;
        bool ok = RunBlocks(br, buf, o, maxOut, tolerate);
        if (!ok) return false;
        outLen = o;
        buf.set_size(o);
        return true;
    }

private:
    struct BitReader { // LSB-first bit reader, 64 bit buffer. Pads with zeros past the end and sets eof.
        const uint8_t* p;
        const uint8_t* end;
        uint64_t buf = 0;
        int cnt = 0;
        bool eof = false;

        BitReader(const uint8_t* d, size_t s) : p(d), end(d + s) {}

        inline void Refill() {
            while (cnt <= 56 && p < end) { buf |= (uint64_t)(*p++) << cnt; cnt += 8; }
        }
        inline void Drop(int n) {
            if (n > cnt) { eof = true; buf = 0; cnt = 0; return; }
            buf >>= n; cnt -= n;
        }
        inline uint32_t Bits(int n) { // n <= 32
            if (n == 0) return 0;
            if (cnt < n) Refill();
            uint32_t v = (uint32_t)(buf & ((1ull << n) - 1ull));
            Drop(n);
            return v;
        }
        inline void DropToByte() { int r = cnt & 7; if (r) Drop(r); }
    };

    struct Huff { // Canonical Huffman: fast lookup table (10 bit) + slow path (puff style)
        enum { FAST = 10 };
        uint16_t fast[1 << FAST];
        uint16_t count[16];
        uint16_t symbols[288];

        Huff() { std::memset(fast, 0, sizeof(fast)); std::memset(count, 0, sizeof(count)); std::memset(symbols, 0, sizeof(symbols)); }

        bool Build(const uint8_t* lens, int n) {
            std::memset(fast, 0, sizeof(fast));
            uint16_t cnt[16];
            std::memset(cnt, 0, sizeof(cnt));
            if (n > 288) return false;
            for (int i = 0; i < n; i++) { if (lens[i] > 15) return false; cnt[lens[i]]++; }
            cnt[0] = 0;
            int left = 1;
            for (int l = 1; l <= 15; l++) { left <<= 1; left -= cnt[l]; if (left < 0) return false; } // over-subscribed
            std::memcpy(count, cnt, sizeof(cnt));

            uint16_t offs[16];
            offs[0] = 0; offs[1] = 0;
            for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + cnt[l]);

            uint16_t nextCode[16];
            uint32_t code = 0;
            nextCode[0] = 0;
            for (int l = 1; l <= 15; l++) { code = (code + cnt[l - 1]) << 1; nextCode[l] = (uint16_t)code; }

            for (int i = 0; i < n; i++) {
                int l = lens[i];
                if (l == 0) continue;
                uint32_t c = nextCode[l]++;
                symbols[offs[l]++] = (uint16_t)i;
                if (l <= FAST) {
                    uint32_t rev = 0;
                    for (int b = 0; b < l; b++) if (c & (1u << b)) rev |= 1u << (l - 1 - b);
                    for (uint32_t k = rev; k < (1u << FAST); k += (1u << l)) fast[k] = (uint16_t)((l << 9) | i);
                }
            }
            return true;
        }

        inline int Decode(BitReader& br) const {
            if (br.cnt < 15) br.Refill();
            uint32_t b = (uint32_t)br.buf;
            uint16_t e = fast[b & ((1u << FAST) - 1)];
            if (e) { br.Drop(e >> 9); return e & 511; }
            int code = 0, first = 0, index = 0;
            for (int len = 1; len <= 15; len++) {
                code |= (int)((b >> (len - 1)) & 1);
                int c = count[len];
                if (code - c < first) { br.Drop(len); return symbols[index + (code - first)]; }
                index += c; first += c; first <<= 1; code <<= 1;
            }
            return -1;
        }
    };

    static bool Grow(knst_img_array<uint8_t>& buf, size_t need, size_t maxOut) {
        if (need <= buf.capacity()) return true;
        if (need > maxOut) return false;
        size_t nc = buf.capacity() * 2;
        if (nc < need) nc = need;
        if (nc > maxOut) nc = maxOut;
        return buf.reserve(nc);
    }

    static bool RunBlocks(BitReader& br, knst_img_array<uint8_t>& buf, size_t& o, size_t maxOut, bool tolerate) {
        for (;;) {
            uint32_t fin = br.Bits(1);
            uint32_t type = br.Bits(2);
            if (br.eof) return false;

            if (type == 0) {
                br.DropToByte();
                uint32_t len = br.Bits(16);
                uint32_t nlen = br.Bits(16);
                if (br.eof || (len ^ 0xFFFFu) != nlen) return false;
                if (o + len > buf.capacity() && !Grow(buf, o + len, maxOut)) return tolerate;
                uint8_t* dst = buf.data() + o;
                uint32_t remaining = len;
                while (remaining && br.cnt >= 8) {      // whole bytes left in the bit buffer
                    *dst++ = (uint8_t)(br.buf & 0xFF);
                    br.buf >>= 8; br.cnt -= 8; remaining--;
                }
                if (remaining) {
                    if ((size_t)(br.end - br.p) < remaining) return false;
                    std::memcpy(dst, br.p, remaining);
                    br.p += remaining;
                }
                o += len;
            } else if (type == 1 || type == 2) {
                const Huff* lit; const Huff* dist;
                Huff dl, dd;
                if (type == 1) { lit = &StaticLit(); dist = &StaticDist(); }
                else {
                    if (!ReadDynamic(br, dl, dd)) return false;
                    lit = &dl; dist = &dd;
                }
                int r = DecodeBlock(br, *lit, *dist, buf, o, maxOut, tolerate);
                if (r == 0) return false;
                if (r == 2) return true;
            } else {
                return false;
            }
            if (fin) return true;
        }
    }

    static const Huff& StaticLit() {
        static const Huff table = [] {
            uint8_t lengths[288];
            for (int i = 0;   i < 144; i++) lengths[i] = 8;
            for (int i = 144; i < 256; i++) lengths[i] = 9;
            for (int i = 256; i < 280; i++) lengths[i] = 7;
            for (int i = 280; i < 288; i++) lengths[i] = 8;
            Huff t; t.Build(lengths, 288);
            return t;
        }();
        return table;
    }
    static const Huff& StaticDist() {
        static const Huff table = [] {
            uint8_t lengths[32];
            for (int i = 0; i < 32; i++) lengths[i] = 5;
            Huff t; t.Build(lengths, 32);
            return t;
        }();
        return table;
    }

    // Dynamic Huffman: literal/length and distance code lengths form ONE sequence (repeat codes may cross the boundary).
    static bool ReadDynamic(BitReader& br, Huff& lit, Huff& dist) {
        uint32_t hlit = br.Bits(5) + 257;
        uint32_t hdist = br.Bits(5) + 1;
        uint32_t hclen = br.Bits(4) + 4;
        if (br.eof || hlit > 286 || hdist > 30) return false;

        static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        uint8_t cl[19];
        std::memset(cl, 0, sizeof(cl));
        for (uint32_t i = 0; i < hclen; i++) cl[order[i]] = (uint8_t)br.Bits(3);

        Huff clh;
        if (!clh.Build(cl, 19)) return false;

        uint8_t lens[286 + 32];
        std::memset(lens, 0, sizeof(lens));
        uint32_t total = hlit + hdist, i = 0;
        while (i < total) {
            int sym = clh.Decode(br);
            if (sym < 0 || br.eof) return false;
            if (sym < 16) { lens[i++] = (uint8_t)sym; }
            else {
                uint32_t rep; uint8_t val = 0;
                if (sym == 16) { if (i == 0) return false; val = lens[i - 1]; rep = 3 + br.Bits(2); }
                else if (sym == 17) { rep = 3 + br.Bits(3); }
                else { rep = 11 + br.Bits(7); }
                if (i + rep > total) return false;
                while (rep--) lens[i++] = val;
            }
        }
        if (lens[256] == 0) return false; // end-of-block code is mandatory
        if (!lit.Build(lens, (int)hlit)) return false;
        if (!dist.Build(lens + hlit, (int)hdist)) return false;
        return true;
    }

    // return: 0 = error, 1 = done, 2 = output limit reached (tolerate mode)
    static int DecodeBlock(BitReader& br, const Huff& lit, const Huff& dist, knst_img_array<uint8_t>& buf,
                           size_t& o, size_t maxOut, bool tolerate) {
        static const uint16_t LBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static const uint8_t  LExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static const uint16_t DBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
        static const uint8_t  DExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

        for (;;) {
            int sym = lit.Decode(br);
            if (sym < 0 || br.eof) return 0;
            if (sym < 256) {
                if (o >= buf.capacity() && !Grow(buf, o + 1, maxOut)) return tolerate ? 2 : 0;
                buf.data()[o++] = (uint8_t)sym;
                continue;
            }
            if (sym == 256) return 1;
            sym -= 257;
            if (sym >= 29) return 0;
            uint32_t len = LBase[sym] + br.Bits(LExtra[sym]);
            int ds = dist.Decode(br);
            if (ds < 0 || ds >= 30 || br.eof) return 0;
            uint32_t d = DBase[ds] + br.Bits(DExtra[ds]);
            if (br.eof || d > o) return 0;
            if (o + len > buf.capacity() && !Grow(buf, o + len, maxOut)) return tolerate ? 2 : 0;
            uint8_t* dst = buf.data() + o;
            const uint8_t* src = dst - d;
            if (d >= len) std::memcpy(dst, src, len);
            else if (d == 1) std::memset(dst, src[0], len);
            else for (uint32_t i = 0; i < len; i++) dst[i] = src[i];
            o += len;
        }
    }
};


// ====================================================================================================================
//  Image loader
// ====================================================================================================================
class knst_image_loader {
public:

    // ------------------------------------------------------------------------------------------------ modern API
    // All `load` variants accept an optional allocator template parameter.
    // Default = knst_default_allocator (backward compatible with knst_image).
    template<typename Alloc = knst_default_allocator>
    static knst_image_t<Alloc> load(const knst_c16string& path, const knst_image_load_options& opt = knst_image_load_options()) {
        knst_byte_string p(path);
        return load<Alloc>(p, opt);
    }
    template<typename Alloc = knst_default_allocator>
    static knst_image_t<Alloc> load(const char16_t* path, const knst_image_load_options& opt = knst_image_load_options()) {
        if (!path) return make_error<Alloc>(KNST_IMAGE_ERR_EMPTY_INPUT);
        uint32_t n = 0;
        while (path[n]) n++;
        knst_byte_string p(path, n);
        return load<Alloc>(p, opt);
    }
    template<typename Alloc = knst_default_allocator>
    static knst_image_t<Alloc> load(const char* utf8_path, const knst_image_load_options& opt = knst_image_load_options()) {
        if (!utf8_path) return make_error<Alloc>(KNST_IMAGE_ERR_EMPTY_INPUT);
        knst_byte_string p(utf8_path);
        return load<Alloc>(p, opt);
    }
    template<typename Alloc = knst_default_allocator>
    static knst_image_t<Alloc> load(const knst_byte_string& utf8_path, const knst_image_load_options& opt = knst_image_load_options()) {
        if (utf8_path.empty()) return make_error<Alloc>(KNST_IMAGE_ERR_EMPTY_INPUT);
        MappedFile f;
        if (!f.Open(utf8_path)) return make_error<Alloc>(KNST_IMAGE_ERR_FILE_OPEN);
        return run_pipeline<Alloc>(f.data, f.size, opt);
    }
    template<typename Alloc = knst_default_allocator>
    static knst_image_t<Alloc> load_from_memory(const void* data, size_t size, const knst_image_load_options& opt = knst_image_load_options()) {
        return run_pipeline<Alloc>(static_cast<const uint8_t*>(data), size, opt);
    }

    // Detects the container format without decoding (custom decoders are not consulted).
    static int detect_format(const void* data, size_t size) {
        const uint8_t* d = static_cast<const uint8_t*>(data);
        if (!d || size < 4) return KNST_IMAGE_FORMAT_UNKNOWN;
        if (size >= 8 && d[0] == 0x89 && d[1] == 0x50 && d[2] == 0x4E && d[3] == 0x47 &&
            d[4] == 0x0D && d[5] == 0x0A && d[6] == 0x1A && d[7] == 0x0A) return KNST_IMAGE_FORMAT_PNG;
        if (d[0] == 0xFF && d[1] == 0xD8) return KNST_IMAGE_FORMAT_JPEG;
        if (size >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8') return KNST_IMAGE_FORMAT_GIF;
        if (d[0] == 'B' && d[1] == 'M') return KNST_IMAGE_FORMAT_BMP;
        if (looks_like_ico(d, size)) return KNST_IMAGE_FORMAT_ICO;
        if (d[0] == 'P' && d[1] >= '1' && d[1] <= '7') return KNST_IMAGE_FORMAT_PNM;
        if (looks_like_tga(d, size)) return KNST_IMAGE_FORMAT_TGA;
        return KNST_IMAGE_FORMAT_UNKNOWN;
    }
    static const char* format_name(int fmt) {
        switch (fmt) {
            case KNST_IMAGE_FORMAT_PNG: return "PNG";   case KNST_IMAGE_FORMAT_JPEG: return "JPEG";
            case KNST_IMAGE_FORMAT_GIF: return "GIF";   case KNST_IMAGE_FORMAT_BMP: return "BMP";
            case KNST_IMAGE_FORMAT_ICO: return "ICO";   case KNST_IMAGE_FORMAT_TGA: return "TGA";
            case KNST_IMAGE_FORMAT_PNM: return "PNM";   case KNST_IMAGE_FORMAT_CUSTOM: return "CUSTOM";
            default: return "UNKNOWN";
        }
    }

    // ------------------------------------------------------------------------------------------------ extensibility
    // Registers your own decoder (WebP, TIFF, QOI ...). Custom decoders are tried BEFORE the built-in ones, in registration order,
    // so you can also override a built-in format. Returns false if the table (KNST_IMAGE_MAX_CUSTOM_DECODERS) is full.
    // Register at start-up; clearing while other threads load images is not supported.
    static bool register_decoder(knst_image_sniff_fn sniff, knst_image_decode_fn decode) {
        if (!sniff || !decode) return false;
        std::lock_guard<std::mutex> lock(custom_mutex());
        int n = custom_count().load();
        if (n >= KNST_IMAGE_MAX_CUSTOM_DECODERS) return false;
        custom_table()[n].sniff = sniff;
        custom_table()[n].decode = decode;
        custom_count().store(n + 1, std::memory_order_release);
        return true;
    }
    static void clear_custom_decoders() {
        std::lock_guard<std::mutex> lock(custom_mutex());
        custom_count().store(0);
    }
    // For custom decoders: sizes result.rgba (zero filled) and sets width/height. Honors the max_dimension option.
    static bool allocate_rgba(knst_image_decode_result& r, uint32_t w, uint32_t h) {
        if (!alloc_rgba(w, h, r.rgba)) return false;
        r.width = w; r.height = h;
        return true;
    }

    // ------------------------------------------------------------------------------------------------ legacy API (unchanged signatures)
    // Format is detected from the magic bytes. flags: KNST_BITMAP_OUTPUT_xxx | KNST_BITMAP_xx_xx (ICO entry size hint)
    static knst_byte_string load_image(const knst_c16string& path, int* out_width, int* out_height, int flags = KNST_BITMAP_OUTPUT_RGBA) {
        return legacy(path, out_width, out_height, flags, KNST_IMAGE_FORMAT_MASK_ALL);
    }
    static knst_byte_string load_bmp(const knst_c16string& path, int* out_width, int* out_height, int flags = KNST_BITMAP_OUTPUT_RGBA) {
        return legacy(path, out_width, out_height, flags, KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_BMP));
    }
    static knst_byte_string load_png(const knst_c16string& path, int* out_width, int* out_height, int flags = KNST_BITMAP_OUTPUT_RGBA) {
        return legacy(path, out_width, out_height, flags, KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_PNG));
    }
    static knst_byte_string load_ppm(const knst_c16string& path, int* out_width, int* out_height, int flags = KNST_BITMAP_OUTPUT_RGBA) {
        return legacy(path, out_width, out_height, flags, KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_PNM));
    }
    static knst_byte_string load_tga(const knst_c16string& path, int* out_width, int* out_height, int flags = KNST_BITMAP_OUTPUT_RGBA) {
        return legacy(path, out_width, out_height, flags, KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_TGA));
    }
    static knst_byte_string load_jpeg(const knst_c16string& path, int* out_width, int* out_height, int flags = KNST_BITMAP_OUTPUT_RGBA) {
        return legacy(path, out_width, out_height, flags, KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_JPEG));
    }
    static knst_byte_string load_gif(const knst_c16string& path, int* out_width, int* out_height, int flags = KNST_BITMAP_OUTPUT_RGBA) {
        return legacy(path, out_width, out_height, flags, KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_GIF));
    }
    static knst_byte_string load_ico(const knst_c16string& path, int* out_width, int* out_height, int flags = KNST_BITMAP_OUTPUT_RGBA) {
        return legacy(path, out_width, out_height, flags, KNST_IMAGE_FORMAT_MASK(KNST_IMAGE_FORMAT_ICO));
    }
    static knst_byte_string load_image_from_memory(const uint8_t* data, size_t size, int* out_width, int* out_height,
                                                   int flags = KNST_BITMAP_OUTPUT_RGBA) {
        knst_image_load_options o = legacy_options(flags, KNST_IMAGE_FORMAT_MASK_ALL);
        knst_image img = load_from_memory(data, size, o);
        return legacy_result(img, out_width, out_height);
    }

private:

    // ---------------------------------------------------------------- per-call runtime context (thread local)
    struct RtCtx {
        uint32_t maxDim;
        unsigned threads;
        int      err;
        int      orientation;
        int      hotspotX, hotspotY;
    };
    static RtCtx& rt() {
        static thread_local RtCtx c = { (uint32_t)KNST_IMAGE_MAX_DIMENSION, 0u, 0, 1, -1, -1 };
        return c;
    }
    static inline uint32_t max_dim() { return rt().maxDim; }
    static inline void set_err(int e) { if (!rt().err) rt().err = e; }
    static inline bool dim_over(uint32_t w, uint32_t h) { if (w > max_dim() || h > max_dim()) { set_err(KNST_IMAGE_ERR_LIMIT); return true; } return false; }

    struct Scope { // installs the options for the duration of one load on this thread
        RtCtx saved;
        explicit Scope(const knst_image_load_options& o) {
            saved = rt();
            RtCtx& c = rt();
            c.maxDim = o.max_dimension ? o.max_dimension : (uint32_t)KNST_IMAGE_MAX_DIMENSION;
            c.threads = o.max_threads;
            c.err = 0; c.orientation = 1; c.hotspotX = -1; c.hotspotY = -1;
        }
        ~Scope() { rt() = saved; }
    };

    struct CustomDecoder { knst_image_sniff_fn sniff; knst_image_decode_fn decode; };
    static CustomDecoder* custom_table() { static CustomDecoder t[KNST_IMAGE_MAX_CUSTOM_DECODERS]; return t; }
    static std::atomic<int>& custom_count() { static std::atomic<int> c(0); return c; }
    static std::mutex& custom_mutex() { static std::mutex m; return m; }

    // ---------------------------------------------------------------- byte helpers
    static inline uint16_t rd16le(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    static inline uint32_t rd32le(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
    static inline uint16_t rd16be(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
    static inline uint32_t rd32be(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }

    // Generic byte-string allocation. Works with knst_byte_string and
    // basic_byte_string<AnyAllocator> — both expose reserve/resize/length.
    template<typename ByteStr>
    static bool alloc_bytes(ByteStr& s, uint64_t bytes) {
        if (bytes > 0xFFFFFF00ull) { set_err(KNST_IMAGE_ERR_LIMIT); return false; }
        if (!s.reserve((uint32_t)bytes)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
        s.resize((uint32_t)bytes);
        if (s.length() != (uint32_t)bytes) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
        return true;
    }
    template<typename ByteStr>
    static inline bool alloc_rgba(uint32_t w, uint32_t h, ByteStr& out) {
        if (w == 0 || h == 0 || w > max_dim() || h > max_dim()) { set_err(KNST_IMAGE_ERR_LIMIT); return false; }
        return alloc_bytes(out, (uint64_t)w * h * 4);
    }

    // ---------------------------------------------------------------- file mapping (RAII)
    class MappedFile {
    public:
        uint8_t* data = nullptr;
        size_t   size = 0;
        MappedFile() = default;
        MappedFile(const MappedFile&) = delete;
        MappedFile& operator=(const MappedFile&) = delete;
        ~MappedFile() { Close(); }

        bool Open(const knst_byte_string& path) { // path: UTF-8
            Close();
            if (path.empty()) return false;

        #if KNST_USING_PLATFORM_WINDOWS
            int wl = MultiByteToWideChar(CP_UTF8, 0, (const char*)path.data(), (int)path.length(), nullptr, 0);
            if (wl <= 0) return false;
            wchar_t* wpath = (wchar_t*)std::malloc(((size_t)wl + 1) * sizeof(wchar_t));
            if (!wpath) return false;
            MultiByteToWideChar(CP_UTF8, 0, (const char*)path.data(), (int)path.length(), wpath, wl);
            wpath[wl] = 0;
            HANDLE hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                      
            std::free(wpath);
            if (hFile == INVALID_HANDLE_VALUE) return false;
            LARGE_INTEGER sz;
            if (!GetFileSizeEx(hFile, &sz) || sz.QuadPart <= 0) { CloseHandle(hFile); return false; }
            size_t fileSize = (size_t)sz.QuadPart;
            HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
            if (hMap == NULL) { CloseHandle(hFile); return false; }
            void* view = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, fileSize);
            CloseHandle(hMap);
            CloseHandle(hFile);
            if (!view) return false;
            data = (uint8_t*)view; size = fileSize; m_mode = 1;
            #if _WIN32_WINNT >= 0x0602
            {
                WIN32_MEMORY_RANGE_ENTRY range;
                range.VirtualAddress = data;
                range.NumberOfBytes = size;
                PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
            }
            #endif
            return true;

        #elif KNST_USING_PLATFORM_LINUX
            int fd = open((const char*)path.data(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) return false;
            struct stat st;
            if (fstat(fd, &st) != 0 || st.st_size <= 0 || !S_ISREG(st.st_mode)) { close(fd); return false; }
            size_t fileSize = (size_t)st.st_size;
            void* p = MAP_FAILED;
            #if defined(KNST_USING_LINUX_PLATFORM_ANDROID)
                p = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
                if (p != MAP_FAILED) {
                    #ifdef MADV_SEQUENTIAL
                        madvise(p, fileSize, MADV_SEQUENTIAL);
                    #endif
                    #ifdef MADV_WILLNEED
                        madvise(p, fileSize, MADV_WILLNEED);
                    #endif
                }
            #else
                #ifdef POSIX_FADV_SEQUENTIAL
                    posix_fadvise(fd, 0, st.st_size, POSIX_FADV_SEQUENTIAL);
                #endif
                #ifdef POSIX_FADV_WILLNEED
                    posix_fadvise(fd, 0, st.st_size, POSIX_FADV_WILLNEED);
                #endif
                int mapFlags = MAP_PRIVATE;
                #ifdef MAP_POPULATE
                    mapFlags |= MAP_POPULATE;
                #endif
                p = mmap(NULL, fileSize, PROT_READ, mapFlags, fd, 0);
                if (p == MAP_FAILED) p = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
            #endif
            close(fd);
            if (p == MAP_FAILED) return false;
            data = (uint8_t*)p; size = fileSize; m_mode = 2;
            return true;

        #else   // portable fallback: read the whole file
            FILE* f = std::fopen((const char*)path.data(), "rb");
            if (!f) return false;
            if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
            long len = std::ftell(f);
            if (len <= 0 || std::fseek(f, 0, SEEK_SET) != 0) { std::fclose(f); return false; }
            uint8_t* buf = (uint8_t*)std::malloc((size_t)len);
            if (!buf) { std::fclose(f); return false; }
            size_t got = std::fread(buf, 1, (size_t)len, f);
            std::fclose(f);
            if (got != (size_t)len) { std::free(buf); return false; }
            data = buf; size = (size_t)len; m_mode = 3;
            return true;
        #endif
        }

        void Close() {
            if (data) {
                if (m_mode == 1) {
                #if KNST_USING_PLATFORM_WINDOWS
                    UnmapViewOfFile(data);
                #endif
                } else if (m_mode == 2) {
                #if KNST_USING_PLATFORM_LINUX
                    munmap(data, size);
                #endif
                } else if (m_mode == 3) {
                    std::free(data);
                }
            }
            data = nullptr; size = 0; m_mode = 0;
        }
    private:
        int m_mode = 0;
    };

    // ---------------------------------------------------------------- row parallelism
    static void ParallelForRows(uint32_t height, size_t workPerRow, const std::function<void(uint32_t, uint32_t)>& fn) {

        unsigned hwThreads = rt().threads ? rt().threads : std::thread::hardware_concurrency();
        if (hwThreads == 0) hwThreads = 2;

        const size_t PARALLEL_WORK_THRESHOLD = 256 * 1024;
        size_t totalWork = (size_t)height * workPerRow;

        unsigned numThreads = 1;
        if (totalWork > PARALLEL_WORK_THRESHOLD && height > 1) {
            numThreads = hwThreads;
            if (numThreads > height) numThreads = height;
            if (numThreads > 16) numThreads = 16;
        }

        if (numThreads <= 1) {
            fn(0, height);
            return;
        }

        knst_vector<knst_thread> pool;
        uint32_t rowsPerThread = (height + numThreads - 1) / numThreads;
        for (unsigned t = 0; t < numThreads; t++) {
            uint32_t startRow = t * rowsPerThread;
            uint32_t endRow = startRow + rowsPerThread;
            if (startRow >= height) break;
            if (endRow > height) endRow = height;

            knst_thread th;
            th.start([&fn, startRow, endRow]() {
                fn(startRow, endRow);
            });
            pool.push_back(std::move(th));
        }
        for (auto& th : pool) th.join();
    }

    // ---------------------------------------------------------------- RGBA <-> BGRA (SIMD)
#if defined(KNST_HAS_SSE2)
    static inline void SwizzleRGBA_BGRA_SSE2(const uint8_t* src, uint8_t* dst, size_t pixelCount) {
        size_t i = 0;
        const __m128i maskRB = _mm_set1_epi32(0x00FF00FF);
        for (; i + 4 <= pixelCount; i += 4) {
            __m128i px = _mm_loadu_si128((const __m128i*)(src + i * 4));
            __m128i rb = _mm_and_si128(px, maskRB);
            __m128i ga = _mm_andnot_si128(maskRB, px);
            __m128i rbSwapped = _mm_or_si128(
                _mm_slli_epi32(rb, 16),
                _mm_srli_epi32(rb, 16));
            __m128i result = _mm_or_si128(rbSwapped, ga);
            _mm_storeu_si128((__m128i*)(dst + i * 4), result);
        }
        for (; i < pixelCount; i++) {
            uint8_t r = src[i*4+0], g = src[i*4+1], b = src[i*4+2], a = src[i*4+3];
            dst[i*4+0] = b; dst[i*4+1] = g; dst[i*4+2] = r; dst[i*4+3] = a;
        }
    }
#endif
#if defined(KNST_HAS_NEON)
    static inline void SwizzleRGBA_BGRA_NEON(const uint8_t* src, uint8_t* dst, size_t pixelCount) {
        size_t i = 0;
        for (; i + 8 <= pixelCount; i += 8) {
            uint8x8x4_t px = vld4_u8(src + i * 4);
            uint8x8x4_t out;
            out.val[0] = px.val[2];
            out.val[1] = px.val[1];
            out.val[2] = px.val[0];
            out.val[3] = px.val[3];
            vst4_u8(dst + i * 4, out);
        }
        for (; i < pixelCount; i++) {
            uint8_t r = src[i*4+0], g = src[i*4+1], b = src[i*4+2], a = src[i*4+3];
            dst[i*4+0] = b; dst[i*4+1] = g; dst[i*4+2] = r; dst[i*4+3] = a;
        }
    }
#endif
    static inline void SwizzleRGBA_BGRA(const uint8_t* src, uint8_t* dst, size_t pixelCount) { // src == dst (in place) is fine
#if defined(KNST_HAS_SSE2)
        SwizzleRGBA_BGRA_SSE2(src, dst, pixelCount);
#elif defined(KNST_HAS_NEON)
        SwizzleRGBA_BGRA_NEON(src, dst, pixelCount);
#else
        for (size_t i = 0; i < pixelCount; i++) {
            uint8_t r = src[i*4+0], g = src[i*4+1], b = src[i*4+2], a = src[i*4+3];
            dst[i*4+0] = b; dst[i*4+1] = g; dst[i*4+2] = r; dst[i*4+3] = a;
        }
#endif
    }

    // ---------------------------------------------------------------- post processing (all operate on RGBA8)
    template<typename ByteStr>
    static bool ApplyOrientation(ByteStr& img, uint32_t& w, uint32_t& h, int o) {
        if (o < 2 || o > 8) return true;
        bool swap = (o >= 5);
        uint32_t nw = swap ? h : w, nh = swap ? w : h;
        ByteStr out;
        if (!alloc_bytes(out, (uint64_t)nw * nh * 4)) return false;
        const uint8_t* s = img.data();
        uint8_t* d = &out[0];
        for (uint32_t y = 0; y < nh; y++) {
            for (uint32_t x = 0; x < nw; x++) {
                uint32_t sx, sy;
                switch (o) {
                    case 2: sx = w - 1 - x; sy = y; break;
                    case 3: sx = w - 1 - x; sy = h - 1 - y; break;
                    case 4: sx = x; sy = h - 1 - y; break;
                    case 5: sx = y; sy = x; break;
                    case 6: sx = y; sy = h - 1 - x; break;
                    case 7: sx = w - 1 - y; sy = h - 1 - x; break;
                    default: sx = w - 1 - y; sy = x; break; // 8
                }
                std::memcpy(d + ((size_t)y * nw + x) * 4, s + ((size_t)sy * w + sx) * 4, 4);
            }
        }
        img = std::move(out);
        w = nw; h = nh;
        return true;
    }

        template<typename ByteStr>
    static void FlipVertical(ByteStr& img, uint32_t w, uint32_t h) {
        uint8_t* b = &img[0];
        size_t stride = (size_t)w * 4;
        for (uint32_t y = 0; y < h / 2; y++) std::swap_ranges(b + (size_t)y * stride, b + (size_t)(y + 1) * stride, b + (size_t)(h - 1 - y) * stride);
    }

    static float KernelSupport(int f) {
        switch (f) { case KNST_IMAGE_RESIZE_BOX: return 0.5f; case KNST_IMAGE_RESIZE_TRIANGLE: return 1.f;
                     case KNST_IMAGE_RESIZE_CATMULL_ROM: return 2.f; default: return 3.f; }
    }
    static float KernelEval(int f, float x) {
        x = std::fabs(x);
        switch (f) {
            case KNST_IMAGE_RESIZE_BOX:      return x <= 0.5f ? 1.f : 0.f;
            case KNST_IMAGE_RESIZE_TRIANGLE: return x < 1.f ? 1.f - x : 0.f;
            case KNST_IMAGE_RESIZE_CATMULL_ROM:
                if (x < 1.f) return (1.5f * x - 2.5f) * x * x + 1.f;
                if (x < 2.f) return ((-0.5f * x + 2.5f) * x - 4.f) * x + 2.f;
                return 0.f;
            default: {
                if (x >= 3.f) return 0.f;
                if (x < 1e-6f) return 1.f;
                float px = 3.14159265f * x;
                return 3.f * std::sin(px) * std::sin(px / 3.f) / (px * px);
            }
        }
    }
    static bool BuildWeights(int srcN, int dstN, int filter, knst_img_array<int>& start, knst_img_array<int>& cnt,
                             knst_img_array<float>& wts, int& taps) {
        double scale = (double)srcN / dstN;
        double fs = scale > 1.0 ? scale : 1.0;
        double support = KernelSupport(filter) * fs;
        taps = (int)std::ceil(support * 2.0) + 2;
        if (!start.resize((size_t)dstN) || !cnt.resize((size_t)dstN) || !wts.resize((size_t)dstN * taps)) return false;
        for (int i = 0; i < dstN; i++) {
            double center = (i + 0.5) * scale;
            int s0 = (int)std::ceil(center - support - 0.5), s1 = (int)std::floor(center + support - 0.5);
            if (s0 < 0) s0 = 0;
            if (s1 > srcN - 1) s1 = srcN - 1;
            if (s1 < s0) { s0 = s1 = std::min(srcN - 1, std::max(0, (int)center)); }
            int n = s1 - s0 + 1;
            if (n > taps) n = taps;
            float* w = wts.data() + (size_t)i * taps;
            double sum = 0;
            for (int k = 0; k < n; k++) { double v = KernelEval(filter, (float)((s0 + k + 0.5 - center) / fs)); w[k] = (float)v; sum += v; }
            if (sum == 0) { for (int k = 0; k < n; k++) w[k] = 0; w[std::min(n - 1, std::max(0, (int)center - s0))] = 1.f; sum = 1; }
            for (int k = 0; k < n; k++) w[k] = (float)(w[k] / sum);
            start[(size_t)i] = s0; cnt[(size_t)i] = n;
        }
        return true;
    }
    template<typename ByteStr>
    static bool ResizeRGBA(ByteStr& img, uint32_t& w, uint32_t& h, uint32_t nw, uint32_t nh, int filter) {
        if (nw == w && nh == h) return true;
        if (nw == 0 || nh == 0 || nw > max_dim() || nh > max_dim()) { set_err(KNST_IMAGE_ERR_LIMIT); return false; }
        ByteStr out;
        if (!alloc_bytes(out, (uint64_t)nw * nh * 4)) return false;
        const uint8_t* src = img.data();
        uint8_t* dst = &out[0];

        if (filter == KNST_IMAGE_RESIZE_NEAREST) {
            for (uint32_t y = 0; y < nh; y++) {
                uint32_t sy = (uint32_t)(((uint64_t)y * h) / nh);
                for (uint32_t x = 0; x < nw; x++) {
                    uint32_t sx = (uint32_t)(((uint64_t)x * w) / nw);
                    std::memcpy(dst + ((size_t)y * nw + x) * 4, src + ((size_t)sy * w + sx) * 4, 4);
                }
            }
        } else {
            knst_img_array<int> xs, xc, ys, yc;
            knst_img_array<float> xw, yw, tmp;
            int xt = 0, yt = 0;
            if (!BuildWeights((int)w, (int)nw, filter, xs, xc, xw, xt) || !BuildWeights((int)h, (int)nh, filter, ys, yc, yw, yt) ||
                !tmp.reserve((size_t)h * nw * 4)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
            float* t = tmp.data();
            // horizontal pass: premultiplied color (0..255 scale) + alpha (0..255)
            ParallelForRows(h, (size_t)nw * xt * 4, [&](uint32_t r0, uint32_t r1) {
                for (uint32_t y = r0; y < r1; y++) {
                    const uint8_t* row = src + (size_t)y * w * 4;
                    float* o = t + (size_t)y * nw * 4;
                    for (uint32_t x = 0; x < nw; x++, o += 4) {
                        const float* wt = xw.data() + (size_t)x * xt;
                        int s0 = xs[x], n = xc[x];
                        float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
                        for (int k = 0; k < n; k++) {
                            const uint8_t* p = row + (size_t)(s0 + k) * 4;
                            float al = p[3] * (1.f / 255.f), wk = wt[k];
                            a0 += wk * p[0] * al; a1 += wk * p[1] * al; a2 += wk * p[2] * al; a3 += wk * p[3];
                        }
                        o[0] = a0; o[1] = a1; o[2] = a2; o[3] = a3;
                    }
                }
            });
            // vertical pass
            ParallelForRows(nh, (size_t)nw * yt * 4, [&](uint32_t r0, uint32_t r1) {
                for (uint32_t y = r0; y < r1; y++) {
                    const float* wt = yw.data() + (size_t)y * yt;
                    int s0 = ys[y], n = yc[y];
                    uint8_t* o = dst + (size_t)y * nw * 4;
                    for (uint32_t x = 0; x < nw; x++, o += 4) {
                        float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
                        for (int k = 0; k < n; k++) {
                            const float* p = t + ((size_t)(s0 + k) * nw + x) * 4;
                            float wk = wt[k];
                            a0 += wk * p[0]; a1 += wk * p[1]; a2 += wk * p[2]; a3 += wk * p[3];
                        }
                        if (a3 <= 0.f) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
                        float inv = 255.f / a3;
                        float c0 = a0 * inv, c1 = a1 * inv, c2 = a2 * inv;
                        o[0] = (uint8_t)(c0 < 0.f ? 0 : (c0 > 255.f ? 255 : (int)(c0 + 0.5f)));
                        o[1] = (uint8_t)(c1 < 0.f ? 0 : (c1 > 255.f ? 255 : (int)(c1 + 0.5f)));
                        o[2] = (uint8_t)(c2 < 0.f ? 0 : (c2 > 255.f ? 255 : (int)(c2 + 0.5f)));
                        o[3] = (uint8_t)(a3 < 0.f ? 0 : (a3 > 255.f ? 255 : (int)(a3 + 0.5f)));
                    }
                }
            });
        }
        img = std::move(out);
        w = nw; h = nh;
        return true;
    }

    static inline uint8_t Div255(uint32_t t) { t += 128; return (uint8_t)((t + (t >> 8)) >> 8); }

       template<typename ByteStr>
    static void FlattenAlpha(ByteStr& img, uint32_t w, uint32_t h, uint8_t br, uint8_t bg, uint8_t bb) {
        uint8_t* b = &img[0];
        ParallelForRows(h, (size_t)w * 4, [b, w, br, bg, bb](uint32_t r0, uint32_t r1) {
            for (size_t i = (size_t)r0 * w; i < (size_t)r1 * w; i++) {
                uint8_t* p = b + i * 4;
                uint32_t a = p[3];
                if (a != 255) {
                    p[0] = (uint8_t)Div255(p[0] * a + br * (255 - a));
                    p[1] = (uint8_t)Div255(p[1] * a + bg * (255 - a));
                    p[2] = (uint8_t)Div255(p[2] * a + bb * (255 - a));
                    p[3] = 255;
                }
            }
        });
    }

        template<typename ByteStr>
    static void Premultiply(ByteStr& img, uint32_t w, uint32_t h) {
        uint8_t* b = &img[0];
        ParallelForRows(h, (size_t)w * 4, [b, w](uint32_t r0, uint32_t r1) {
            for (size_t i = (size_t)r0 * w; i < (size_t)r1 * w; i++) {
                uint8_t* p = b + i * 4;
                uint32_t a = p[3];
                if (a != 255) { p[0] = Div255(p[0] * a); p[1] = Div255(p[1] * a); p[2] = Div255(p[2] * a); }
            }
        });
    }

    // RGBA -> requested output layout. Returns channel count (0 on failure).
    template<typename ByteStr>
    static int FinalizeFormat(ByteStr& buf, uint32_t w, uint32_t h, int fmt) {
        if (fmt == KNST_BITMAP_OUTPUT_RGB || fmt == KNST_BITMAP_OUTPUT_BGR) {
            bool bgr = (fmt == KNST_BITMAP_OUTPUT_BGR);
            ByteStr o;
            if (!alloc_bytes(o, (uint64_t)w * h * 3)) return 0;
            const uint8_t* sb = buf.data();
            uint8_t* db = &o[0];
            ParallelForRows(h, (size_t)w * 4, [sb, db, w, bgr](uint32_t r0, uint32_t r1) {
                for (uint32_t y = r0; y < r1; y++) {
                    const uint8_t* s = sb + (size_t)y * w * 4;
                    uint8_t* d = db + (size_t)y * w * 3;
                    if (bgr) for (uint32_t x = 0; x < w; x++, s += 4, d += 3) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; }
                    else     for (uint32_t x = 0; x < w; x++, s += 4, d += 3) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
                }
            });
            buf = std::move(o);
            return 3;
        }
        if (fmt == KNST_BITMAP_OUTPUT_BGRA) {
            uint8_t* base = &buf[0];
            ParallelForRows(h, (size_t)w * 4, [base, w](uint32_t r0, uint32_t r1) {
                size_t off = (size_t)r0 * w * 4;
                SwizzleRGBA_BGRA(base + off, base + off, (size_t)(r1 - r0) * w);
            });
        }
        return 4;
    }

    // ---------------------------------------------------------------- pipeline
    template<typename Alloc = knst_default_allocator>
    static knst_image_t<Alloc> make_error(int e) {
        knst_image_t<Alloc> img;
        img.error = e;
        return img;
    }

    template<typename Alloc>
    static knst_image_t<Alloc> run_pipeline(const uint8_t* d, size_t n, const knst_image_load_options& o) {
        using ByteStr = basic_byte_string<Alloc>;

        if (!d || n == 0) return make_error<Alloc>(KNST_IMAGE_ERR_EMPTY_INPUT);
        Scope scope(o);

        // ---- pick decoder
        knst_image_decode_fn custom = nullptr;
        int fmt = KNST_IMAGE_FORMAT_UNKNOWN;
        int cc = custom_count().load(std::memory_order_acquire);
        for (int i = 0; i < cc; i++) {
            if (custom_table()[i].sniff(d, n)) { custom = custom_table()[i].decode; fmt = KNST_IMAGE_FORMAT_CUSTOM; break; }
        }
        if (!custom) {
            fmt = detect_format(d, n);
            switch (fmt) {
                case KNST_IMAGE_FORMAT_PNG:
                case KNST_IMAGE_FORMAT_JPEG:
                case KNST_IMAGE_FORMAT_GIF:
                case KNST_IMAGE_FORMAT_BMP:
                case KNST_IMAGE_FORMAT_ICO:
                case KNST_IMAGE_FORMAT_TGA:
                case KNST_IMAGE_FORMAT_PNM:
                    break;
                default: return make_error<Alloc>(KNST_IMAGE_ERR_UNKNOWN_FORMAT);
            }
        }
        if (!(o.allowed_formats & KNST_IMAGE_FORMAT_MASK(fmt))) return make_error<Alloc>(KNST_IMAGE_ERR_FORMAT_DISABLED);

        // ---- decode
        uint32_t W = 0, H = 0;
        ByteStr px;
        bool ok = false;
        if (custom) {
            knst_image_decode_result r;
            r.size_hint = o.target_size;
            ok = custom(d, n, r) && r.width && r.height && r.rgba.length() == (size_t)r.width * r.height * 4;
            if (ok) {
                W = r.width; H = r.height;
                if (!alloc_bytes(px, r.rgba.length())) return make_error<Alloc>(rt().err ? rt().err : KNST_IMAGE_ERR_OUT_OF_MEMORY);
                if (!r.rgba.empty()) std::memcpy(&px[0], r.rgba.data(), r.rgba.length());
                rt().orientation = r.orientation; rt().hotspotX = r.hotspot_x; rt().hotspotY = r.hotspot_y;
            }
        } else {
            switch (fmt) {
                case KNST_IMAGE_FORMAT_PNG:  ok = decode_png<ByteStr>(d, n, W, H, px, o.target_size);  break;
                case KNST_IMAGE_FORMAT_JPEG: ok = decode_jpeg<ByteStr>(d, n, W, H, px, o.target_size); break;
                case KNST_IMAGE_FORMAT_GIF:  ok = decode_gif<ByteStr>(d, n, W, H, px, o.target_size);  break;
                case KNST_IMAGE_FORMAT_BMP:  ok = decode_bmp<ByteStr>(d, n, W, H, px, o.target_size);  break;
                case KNST_IMAGE_FORMAT_ICO:  ok = decode_ico<ByteStr>(d, n, W, H, px, o.target_size);  break;
                case KNST_IMAGE_FORMAT_TGA:  ok = decode_tga<ByteStr>(d, n, W, H, px, o.target_size);  break;
                case KNST_IMAGE_FORMAT_PNM:  ok = decode_pnm<ByteStr>(d, n, W, H, px, o.target_size);  break;
                default: return make_error<Alloc>(KNST_IMAGE_ERR_UNKNOWN_FORMAT);
            }
            ok = ok && W && H && px.length() == (size_t)W * H * 4;
        }
        if (!ok) return make_error<Alloc>(rt().err ? rt().err : KNST_IMAGE_ERR_DECODE);

        int hx = rt().hotspotX, hy = rt().hotspotY;

        // ---- transforms
        if (o.apply_exif_orientation && rt().orientation > 1) {
            if (!ApplyOrientation<ByteStr>(px, W, H, rt().orientation))
                return make_error<Alloc>(rt().err ? rt().err : KNST_IMAGE_ERR_OUT_OF_MEMORY);
            hx = hy = -1;
        }
        if (o.flip_vertical) { FlipVertical<ByteStr>(px, W, H); if (hy >= 0) hy = (int)H - 1 - hy; }

        if (o.resize_width || o.resize_height) {
            uint64_t nw = o.resize_width, nh = o.resize_height;
            if (!nw) nw = std::max<uint64_t>(1, ((uint64_t)W * nh + H / 2) / H);
            else if (!nh) nh = std::max<uint64_t>(1, ((uint64_t)H * nw + W / 2) / W);
            else if (o.keep_aspect) {
                double s = std::min((double)nw / W, (double)nh / H);
                nw = std::max<uint64_t>(1, (uint64_t)(W * s + 0.5));
                nh = std::max<uint64_t>(1, (uint64_t)(H * s + 0.5));
            }
            if (nw > 0xFFFFFFFFull || nh > 0xFFFFFFFFull) return make_error<Alloc>(KNST_IMAGE_ERR_LIMIT);
            uint32_t ow = W, oh = H;
            if (!ResizeRGBA<ByteStr>(px, W, H, (uint32_t)nw, (uint32_t)nh, o.resize_filter))
                return make_error<Alloc>(rt().err ? rt().err : KNST_IMAGE_ERR_OUT_OF_MEMORY);
            if (hx >= 0) hx = (int)(((uint64_t)hx * W) / ow);
            if (hy >= 0) hy = (int)(((uint64_t)hy * H) / oh);
        }
        if (o.flatten_alpha) FlattenAlpha<ByteStr>(px, W, H, o.background_r, o.background_g, o.background_b);
        else if (o.premultiply_alpha) Premultiply<ByteStr>(px, W, H);

        int outFmt = o.output_format;
        if (outFmt != KNST_BITMAP_OUTPUT_RGB && outFmt != KNST_BITMAP_OUTPUT_BGR && outFmt != KNST_BITMAP_OUTPUT_BGRA)
            outFmt = KNST_BITMAP_OUTPUT_RGBA;
        int ch = FinalizeFormat<ByteStr>(px, W, H, outFmt);
        if (!ch) return make_error<Alloc>(rt().err ? rt().err : KNST_IMAGE_ERR_OUT_OF_MEMORY);

        knst_image_t<Alloc> img;
        img.pixels = std::move(px);
        img.width = W; img.height = H; img.channels = ch;
        img.output_format = outFmt; img.source_format = fmt;
        img.hotspot_x = hx; img.hotspot_y = hy;
        return img;
    }

    // ---------------------------------------------------------------- legacy glue
    static knst_image_load_options legacy_options(int flags, uint32_t mask) {
        knst_image_load_options o;
        int f = flags & 0xFF00;
        o.output_format = f ? f : KNST_BITMAP_OUTPUT_RGBA;
        o.target_size = knst_bitmap_flag_to_size(flags);
        o.apply_exif_orientation = false; // legacy behaviour: raw decoded pixels
        o.allowed_formats = mask;
        return o;
    }
    static knst_byte_string legacy_result(knst_image& img, int* ow, int* oh) {
        if (ow) *ow = img.valid() ? (int)img.width : 0;
        if (oh) *oh = img.valid() ? (int)img.height : 0;
        if (!img.valid()) { knst_byte_string e; return e; }
        return std::move(img.pixels);
    }
    static knst_byte_string legacy(const knst_c16string& path, int* ow, int* oh, int flags, uint32_t mask) {
        knst_image img = load(path, legacy_options(flags, mask));
        return legacy_result(img, ow, oh);
    }

    // ---------------------------------------------------------------- format sniffing helpers
    static bool looks_like_ico(const uint8_t* d, size_t n) {
        if (n < 22) return false;
        if (d[0] != 0 || d[1] != 0 || (d[2] != 1 && d[2] != 2) || d[3] != 0) return false;
        uint32_t count = rd16le(d + 4);
        if (count == 0 || 6 + (size_t)count * 16 > n) return false;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* e = d + 6 + i * 16;
            uint32_t sz = rd32le(e + 8), off = rd32le(e + 12);
            if (e[3] != 0) return false;
            if (sz == 0 || off >= n || sz > n - off) return false;
        }
        return true;
    }

    static bool looks_like_tga(const uint8_t* d, size_t n) {
        if (n < 18) return false;
        uint8_t cmType = d[1], type = d[2];
        if (cmType > 1) return false;
        if (type != 1 && type != 2 && type != 3 && type != 9 && type != 10 && type != 11) return false;
        uint16_t w = rd16le(d + 12), h = rd16le(d + 14);
        if (w == 0 || h == 0) return false;
        uint8_t bpp = d[16];
        int base = type & 7;
        if (base == 1) { if (cmType != 1 || (bpp != 8 && bpp != 16)) return false; }
        else if (base == 2) { if (bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return false; }
        else { if (bpp != 8 && bpp != 16) return false; }
        if (cmType == 1) { uint8_t cd = d[7]; if (cd != 15 && cd != 16 && cd != 24 && cd != 32) return false; }
        if ((d[17] & 0xC0) != 0) return false; // bits 6-7 (interleave) unsupported / invalid
        return true;
    }

    // ====================================================================================================================
    //  PNG
    // ====================================================================================================================
    struct PngCtx {
        uint32_t w = 0, h = 0;
        int depth = 8, ctype = 0, spp = 1;
        uint8_t pal[256][4];
        bool hasKey = false;
        uint16_t key[3] = {0, 0, 0};
    };

    static inline uint8_t png16to8(uint32_t v) { return (uint8_t)((v * 255u + 32895u) >> 16); }

    static void png_convert_row(const PngCtx& c, const uint8_t* s, uint8_t* d, uint32_t w) {
        switch (c.ctype) {
        case 0: {
            if (c.depth == 8) {
                for (uint32_t x = 0; x < w; x++, d += 4) {
                    uint8_t g = s[x];
                    d[0] = d[1] = d[2] = g;
                    d[3] = (c.hasKey && g == (c.key[0] & 0xFF)) ? 0 : 255;
                }
            } else if (c.depth == 16) {
                for (uint32_t x = 0; x < w; x++, d += 4) {
                    uint32_t v = ((uint32_t)s[2*x] << 8) | s[2*x+1];
                    uint8_t g = png16to8(v);
                    d[0] = d[1] = d[2] = g;
                    d[3] = (c.hasKey && v == c.key[0]) ? 0 : 255;
                }
            } else {
                int dep = c.depth;
                uint32_t mask = (1u << dep) - 1, maxv = mask;
                for (uint32_t x = 0; x < w; x++, d += 4) {
                    uint32_t bit = (uint32_t)x * dep;
                    uint32_t v = (s[bit >> 3] >> (8 - dep - (bit & 7))) & mask;
                    uint8_t g = (uint8_t)(v * 255 / maxv);
                    d[0] = d[1] = d[2] = g;
                    d[3] = (c.hasKey && v == (c.key[0] & mask)) ? 0 : 255;
                }
            }
            break;
        }
        case 2: { // RGB
            if (c.depth == 8) {
                uint8_t kr = c.key[0] & 0xFF, kg = c.key[1] & 0xFF, kb = c.key[2] & 0xFF;
                for (uint32_t x = 0; x < w; x++, s += 3, d += 4) {
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                    d[3] = (c.hasKey && s[0] == kr && s[1] == kg && s[2] == kb) ? 0 : 255;
                }
            } else {
                for (uint32_t x = 0; x < w; x++, s += 6, d += 4) {
                    uint32_t r = ((uint32_t)s[0] << 8) | s[1], g = ((uint32_t)s[2] << 8) | s[3], b = ((uint32_t)s[4] << 8) | s[5];
                    d[0] = png16to8(r); d[1] = png16to8(g); d[2] = png16to8(b);
                    d[3] = (c.hasKey && r == c.key[0] && g == c.key[1] && b == c.key[2]) ? 0 : 255;
                }
            }
            break;
        }
        case 3: {
            int dep = c.depth;
            if (dep == 8) {
                for (uint32_t x = 0; x < w; x++, d += 4) std::memcpy(d, c.pal[s[x]], 4);
            } else {
                uint32_t mask = (1u << dep) - 1;
                for (uint32_t x = 0; x < w; x++, d += 4) {
                    uint32_t bit = (uint32_t)x * dep;
                    uint32_t v = (s[bit >> 3] >> (8 - dep - (bit & 7))) & mask;
                    std::memcpy(d, c.pal[v], 4);
                }
            }
            break;
        }
        case 4: { // gri + alfa
            if (c.depth == 8) {
                for (uint32_t x = 0; x < w; x++, d += 4) { d[0] = d[1] = d[2] = s[2*x]; d[3] = s[2*x+1]; }
            } else {
                for (uint32_t x = 0; x < w; x++, d += 4) {
                    uint8_t g = png16to8(((uint32_t)s[4*x] << 8) | s[4*x+1]);
                    d[0] = d[1] = d[2] = g;
                    d[3] = png16to8(((uint32_t)s[4*x+2] << 8) | s[4*x+3]);
                }
            }
            break;
        }
        case 6: { // RGBA
            if (c.depth == 8) std::memcpy(d, s, (size_t)w * 4);
            else {
                for (uint32_t x = 0; x < w * 4u; x++) d[x] = png16to8(((uint32_t)s[2*x] << 8) | s[2*x+1]);
            }
            break;
        }
        }
    }

    static inline int PaethPred(int a, int b, int c) {
        int p = a + b - c;
        int pa = p > a ? p - a : a - p;
        int pb = p > b ? p - b : b - p;
        int pc = p > c ? p - c : c - p;
        if (pa <= pb && pa <= pc) return a;
        return (pb <= pc) ? b : c;
    }

    static bool png_unfilter(uint8_t* buf, size_t stride, uint32_t rows, int bpp, const uint8_t* zero) {
        for (uint32_t y = 0; y < rows; y++) {
            uint8_t* row = buf + (size_t)y * (stride + 1);
            uint8_t ft = row[0];
            uint8_t* cur = row + 1;
            const uint8_t* prev = (y == 0) ? zero : (row - (stride + 1) + 1);
            switch (ft) {
            case 0: break;
            case 1:
                for (size_t x = bpp; x < stride; x++) cur[x] = (uint8_t)(cur[x] + cur[x - bpp]);
                break;
            case 2:
                for (size_t x = 0; x < stride; x++) cur[x] = (uint8_t)(cur[x] + prev[x]);
                break;
            case 3:
                for (size_t x = 0; x < (size_t)bpp && x < stride; x++) cur[x] = (uint8_t)(cur[x] + (prev[x] >> 1));
                for (size_t x = bpp; x < stride; x++) cur[x] = (uint8_t)(cur[x] + ((cur[x - bpp] + prev[x]) >> 1));
                break;
            case 4:
                for (size_t x = 0; x < (size_t)bpp && x < stride; x++) cur[x] = (uint8_t)(cur[x] + prev[x]);
                for (size_t x = bpp; x < stride; x++) cur[x] = (uint8_t)(cur[x] + PaethPred(cur[x - bpp], prev[x], prev[x - bpp]));
                break;
            default: return false;
            }
        }
        return true;
    }

    template<typename ByteStr>
static bool decode_png(const uint8_t* data, size_t size, uint32_t& W, uint32_t& H, ByteStr& out, int = 0) {
        static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (data == nullptr || size < 8 || std::memcmp(data, sig, 8) != 0) return false;

        PngCtx c;
        bool haveIHDR = false;
        uint8_t interlace = 0;
        uint8_t plte[768], trns[256];
        size_t plteLen = 0, trnsLen = 0;
        knst_img_array<uint8_t> idat;
        const uint8_t* idat1 = nullptr; size_t idat1Len = 0; int idatChunks = 0;

        size_t pos = 8;
        while (pos + 8 <= size) {
            uint32_t len = rd32be(data + pos);
            const uint8_t* t = data + pos + 4;
            pos += 8;
            size_t avail = size - pos;
            bool trunc = (size_t)len > avail;
            size_t take = trunc ? avail : (size_t)len;
            const uint8_t* p = data + pos;

            if (std::memcmp(t, "IHDR", 4) == 0) {
                if (take < 13 || haveIHDR) return false;
                c.w = rd32be(p); c.h = rd32be(p + 4);
                c.depth = p[8]; c.ctype = p[9];
                if (p[10] != 0 || p[11] != 0) return false;
                interlace = p[12];
                if (interlace > 1) return false;
                if (c.w == 0 || c.h == 0) return false;
                if (dim_over(c.w, c.h)) return false;
                bool ok = false;
                switch (c.ctype) {
                    case 0: ok = (c.depth==1||c.depth==2||c.depth==4||c.depth==8||c.depth==16); c.spp = 1; break;
                    case 2: ok = (c.depth==8||c.depth==16); c.spp = 3; break;
                    case 3: ok = (c.depth==1||c.depth==2||c.depth==4||c.depth==8); c.spp = 1; break;
                    case 4: ok = (c.depth==8||c.depth==16); c.spp = 2; break;
                    case 6: ok = (c.depth==8||c.depth==16); c.spp = 4; break;
                }
                if (!ok) return false;
                haveIHDR = true;
            } else if (std::memcmp(t, "PLTE", 4) == 0) {
                plteLen = take > sizeof(plte) ? sizeof(plte) : take; std::memcpy(plte, p, plteLen);
            } else if (std::memcmp(t, "tRNS", 4) == 0) {
                trnsLen = take > sizeof(trns) ? sizeof(trns) : take; std::memcpy(trns, p, trnsLen);
            } else if (std::memcmp(t, "IDAT", 4) == 0) {
                if (idatChunks == 0) { idat1 = p; idat1Len = take; }
                else {
                    if (idatChunks == 1 && !idat.append(idat1, idat1Len)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
                    if (!idat.append(p, take)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
                }
                idatChunks++;
            } else if (std::memcmp(t, "IEND", 4) == 0) {
                break;
            }
            pos += take;
            if (trunc) break;
            pos = (pos + 4 <= size) ? pos + 4 : size; // CRC
        }

        if (!haveIHDR || idatChunks == 0) return false;
        const uint8_t* idatPtr = (idatChunks == 1) ? idat1 : idat.data();
        size_t idatLen = (idatChunks == 1) ? idat1Len : idat.size();

        for (int i = 0; i < 256; i++) { c.pal[i][0] = c.pal[i][1] = c.pal[i][2] = 0; c.pal[i][3] = 255; }
        if (c.ctype == 3) {
            size_t entries = plteLen / 3;
            if (entries == 0) return false;
            if (entries > 256) entries = 256;
            for (size_t i = 0; i < entries; i++) { c.pal[i][0] = plte[i*3]; c.pal[i][1] = plte[i*3+1]; c.pal[i][2] = plte[i*3+2]; }
            for (size_t i = 0; i < trnsLen && i < 256; i++) c.pal[i][3] = trns[i];
        } else if (c.ctype == 0 && trnsLen >= 2) {
            c.hasKey = true; c.key[0] = rd16be(trns);
            if (c.depth < 16) c.key[0] &= (uint16_t)((1u << c.depth) - 1);
        } else if (c.ctype == 2 && trnsLen >= 6) {
            c.hasKey = true;
            for (int i = 0; i < 3; i++) { c.key[i] = rd16be(trns + i * 2); if (c.depth < 16) c.key[i] &= 0xFF; }
        }

        static const int pSX[7] = {0,4,0,2,0,1,0}, pSY[7] = {0,0,4,0,2,0,1}, pDX[7] = {8,8,4,4,2,2,1}, pDY[7] = {8,8,8,4,4,2,2};
        auto strideOf = [&](uint32_t pw) -> size_t { return ((size_t)pw * c.spp * c.depth + 7) / 8; };
        size_t expected = 0;
        uint32_t passW[7], passH[7];
        if (interlace == 0) {
            expected = (strideOf(c.w) + 1) * c.h;
        } else {
            for (int p = 0; p < 7; p++) {
                passW[p] = (c.w > (uint32_t)pSX[p]) ? (c.w - pSX[p] + pDX[p] - 1) / pDX[p] : 0;
                passH[p] = (c.h > (uint32_t)pSY[p]) ? (c.h - pSY[p] + pDY[p] - 1) / pDY[p] : 0;
                if (passW[p] && passH[p]) expected += (strideOf(passW[p]) + 1) * passH[p];
            }
        }

        knst_img_array<uint8_t> raw;
        if (!raw.reserve(expected)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
        size_t got = 0;
        if (!KnstInflate::InflateZlib(idatPtr, idatLen, raw, got, expected, true)) return false;
        if (got < expected) return false;
        idat.release();

        if (!alloc_rgba(c.w, c.h, out)) return false;
        W = c.w; H = c.h;
        uint8_t* ob = &out[0];
        int bpp = (int)(((size_t)c.spp * c.depth + 7) / 8);
        if (bpp < 1) bpp = 1;

        if (interlace == 0) {
            size_t stride = strideOf(c.w);
            knst_img_array<uint8_t> zero;
            if (!zero.resize(stride)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
            if (!png_unfilter(raw.data(), stride, c.h, bpp, zero.data())) return false;
            const uint8_t* rb = raw.data();
            uint32_t w = c.w;
            ParallelForRows(c.h, (size_t)w * 4, [&c, rb, ob, stride, w](uint32_t r0, uint32_t r1) {
                for (uint32_t y = r0; y < r1; y++)
                    png_convert_row(c, rb + (size_t)y * (stride + 1) + 1, ob + (size_t)y * w * 4, w);
            });
        } else {
            size_t off = 0;
            knst_img_array<uint8_t> zero, tmp;
            if (!zero.resize(strideOf(c.w)) || !tmp.resize((size_t)c.w * 4)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
            for (int p = 0; p < 7; p++) {
                uint32_t pw = passW[p], ph = passH[p];
                if (!pw || !ph) continue;
                size_t stride = strideOf(pw);
                if (!png_unfilter(raw.data() + off, stride, ph, bpp, zero.data())) return false;
                for (uint32_t y = 0; y < ph; y++) {
                    png_convert_row(c, raw.data() + off + (size_t)y * (stride + 1) + 1, tmp.data(), pw);
                    uint32_t oy = pSY[p] + y * pDY[p];
                    for (uint32_t x = 0; x < pw; x++) {
                        uint32_t ox = pSX[p] + x * pDX[p];
                        std::memcpy(ob + ((size_t)oy * c.w + ox) * 4, tmp.data() + (size_t)x * 4, 4);
                    }
                }
                off += (stride + 1) * ph;
            }
        }
        return true;
    }

    struct BmpField {
        uint32_t mask = 0; int shift = 0; int bits = 0;
        void Init(uint32_t m) {
            mask = m; shift = 0; bits = 0;
            if (!m) return;
            while (!((m >> shift) & 1)) shift++;
            uint32_t t = m >> shift;
            while (t & 1) { bits++; t >>= 1; }
        }
        inline uint8_t Get(uint32_t px, uint8_t dflt) const {
            if (!mask) return dflt;
            uint32_t v = (px & mask) >> shift;
            if (bits >= 8) return (uint8_t)(v >> (bits - 8));
            return (uint8_t)((v * 255u) / ((1u << bits) - 1u));
        }
    };

        template<typename ByteStr>
    static bool decode_bmp(const uint8_t* d, size_t n, uint32_t& W, uint32_t& H, ByteStr& out, int = 0) {
        if (n < 14 + 12 || d[0] != 'B' || d[1] != 'M') return false;
        uint32_t dataOff = rd32le(d + 10);
        size_t pixOff = 0; 
        if (dataOff > 14) pixOff = dataOff - 14;
        return decode_dib(d + 14, n - 14, pixOff, false, W, H, out);
    }

    template<typename ByteStr>
    static bool decode_dib(const uint8_t* d, size_t n, size_t pixOff, bool icon, uint32_t& W, uint32_t& H, ByteStr& out) {
        if (n < 12) return false;
        uint32_t hs = rd32le(d);
        int64_t width, height;
        int bpp;
        uint32_t comp = 0, colorsUsed = 0, imageSize = 0;
        size_t entrySize = 4;
        if (hs == 12) {
            width = rd16le(d + 4); height = rd16le(d + 6); bpp = rd16le(d + 10);
            entrySize = 3;
        } else {
            if (hs < 16 || hs > n) return false;
            width = (int32_t)rd32le(d + 4); height = (int32_t)rd32le(d + 8); bpp = rd16le(d + 14);
            if (hs >= 20) comp = rd32le(d + 16);
            if (hs >= 24) imageSize = rd32le(d + 20);
            if (hs >= 36) colorsUsed = rd32le(d + 32);
        }
        if (width <= 0 || height == 0) return false;
        bool topDown = height < 0;
        int64_t absH = topDown ? -height : height;
        int64_t imgH = icon ? absH / 2 : absH;
        if (imgH <= 0) return false;
        if (width > (int64_t)max_dim() || imgH > (int64_t)max_dim()) { set_err(KNST_IMAGE_ERR_LIMIT); return false; }
        uint32_t w = (uint32_t)width, h = (uint32_t)imgH;

        uint32_t masks[4] = {0, 0, 0, 0};
        size_t palOff = hs;
        bool explicitAlpha = false;
        if (comp == 3 || comp == 6) {
            if (hs >= 52) {
                masks[0] = rd32le(d + 40); masks[1] = rd32le(d + 44); masks[2] = rd32le(d + 48);
                if (hs >= 56) masks[3] = rd32le(d + 52);
            } else if (hs == 40) {
                size_t nm = (comp == 6) ? 4 : 3;
                if (n < 40 + nm * 4) return false;
                for (size_t i = 0; i < nm; i++) masks[i] = rd32le(d + 40 + i * 4);
                palOff = 40 + nm * 4;
            }
            explicitAlpha = masks[3] != 0;
        } else if (comp == 0 && bpp == 16) {
            masks[0] = 0x7C00; masks[1] = 0x03E0; masks[2] = 0x001F;
        } else if (comp == 0 && bpp == 32) {
            masks[0] = 0x00FF0000; masks[1] = 0x0000FF00; masks[2] = 0x000000FF; masks[3] = 0xFF000000;
        }
        if ((bpp == 16 || bpp == 32) && masks[0] == 0 && masks[1] == 0 && masks[2] == 0) { // BITFIELDS 
            if (bpp == 16) { masks[0] = 0x7C00; masks[1] = 0x03E0; masks[2] = 0x001F; }
            else { masks[0] = 0x00FF0000; masks[1] = 0x0000FF00; masks[2] = 0x000000FF; masks[3] = 0xFF000000; }
        }

        // palet
        uint8_t pal[256][4];
        for (int i = 0; i < 256; i++) { pal[i][0] = pal[i][1] = pal[i][2] = 0; pal[i][3] = 255; }
        size_t declaredPal = 0;
        if (bpp > 0 && bpp <= 8) {
            declaredPal = colorsUsed ? colorsUsed : ((size_t)1 << bpp);
            if (declaredPal > ((size_t)1 << bpp)) declaredPal = (size_t)1 << bpp;
            size_t avail = (palOff <= n) ? (n - palOff) / entrySize : 0;
            size_t cnt = std::min(declaredPal, avail);
            for (size_t i = 0; i < cnt; i++) {
                const uint8_t* e = d + palOff + i * entrySize;
                pal[i][0] = e[2]; pal[i][1] = e[1]; pal[i][2] = e[0]; pal[i][3] = 255;
            }
        } else {
            declaredPal = colorsUsed;
        }
        if (pixOff == 0 || pixOff < palOff) pixOff = palOff + declaredPal * entrySize;
        if (pixOff >= n) return false;

        if (comp == 5 || comp == 4) {
            size_t len = imageSize ? std::min<size_t>(imageSize, n - pixOff) : (n - pixOff);
            if (comp == 5) return decode_png(d + pixOff, len, W, H, out, 0);
            return decode_jpeg(d + pixOff, len, W, H, out, 0);
        }
        if (comp > 3 && comp != 6) return false;
        if (planesInvalid(bpp)) return false;

        if (!alloc_rgba(w, h, out)) return false;
        uint8_t* ob = &out[0];
        W = w; H = h;
        const uint8_t* px = d + pixOff;
        size_t avail = n - pixOff;
        bool anyAlpha = false;

        if (comp == 1 || comp == 2) { // RLE8 / RLE4
            if ((comp == 1 && bpp != 8) || (comp == 2 && bpp != 4)) return false;
            knst_img_array<uint16_t> idx;
            if (!idx.assign((size_t)w * h, 0xFFFF)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
            const uint8_t* p = px; const uint8_t* e = px + avail;
            uint32_t x = 0, y = 0;
            bool rle8 = (comp == 1);
            auto put = [&](uint8_t v) { if (x < w && y < h) idx[(size_t)y * w + x] = v; x++; };
            while (p + 1 < e) {
                uint8_t c = p[0], v = p[1]; p += 2;
                if (c) {
                    for (int i = 0; i < c; i++) put(rle8 ? v : (uint8_t)((i & 1) ? (v & 15) : (v >> 4)));
                } else if (v == 0) { x = 0; y++; }
                else if (v == 1) break;
                else if (v == 2) { if (p + 1 >= e) break; x += p[0]; y += p[1]; p += 2; }
                else {
                    size_t bytes = rle8 ? v : (size_t)(v + 1) / 2;
                    if (p + bytes > e) break;
                    for (int i = 0; i < v; i++) {
                        if (rle8) put(p[i]);
                        else put((uint8_t)((i & 1) ? (p[i >> 1] & 15) : (p[i >> 1] >> 4)));
                    }
                    p += bytes;
                    if (bytes & 1) p++;
                }
                if (y >= h && x >= w) { /* EOI goo broo */ }
            }
            for (uint32_t r = 0; r < h; r++) {
                uint32_t dy = topDown ? r : (h - 1 - r);
                uint8_t* dr = ob + (size_t)dy * w * 4;
                for (uint32_t cx = 0; cx < w; cx++) {
                    uint16_t v = idx[(size_t)r * w + cx];
                    std::memcpy(dr + cx * 4, pal[v == 0xFFFF ? 0 : (v & 255)], 4);
                }
            }
            return true;
        }

        size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
        if (stride * h > avail) return false;

        BmpField fr, fg, fb, fa;
        if (bpp == 16 || bpp == 32) { fr.Init(masks[0]); fg.Init(masks[1]); fb.Init(masks[2]); fa.Init(masks[3]); }
        bool stdBGRA32 = (bpp == 32 && masks[0] == 0x00FF0000 && masks[1] == 0x0000FF00 && masks[2] == 0x000000FF);

        for (uint32_t r = 0; r < h; r++) {
            const uint8_t* s = px + (size_t)r * stride;
            uint32_t dy = topDown ? r : (h - 1 - r);
            uint8_t* o = ob + (size_t)dy * w * 4;
            switch (bpp) {
            case 1: case 2: case 4: case 8: {
                uint32_t mask = (1u << bpp) - 1;
                for (uint32_t x = 0; x < w; x++, o += 4) {
                    uint32_t bit = x * bpp;
                    uint32_t v = (s[bit >> 3] >> (8 - bpp - (bit & 7))) & mask;
                    std::memcpy(o, pal[v], 4);
                }
                break;
            }
            case 16:
                for (uint32_t x = 0; x < w; x++, o += 4) {
                    uint32_t v = rd16le(s + x * 2);
                    o[0] = fr.Get(v, 0); o[1] = fg.Get(v, 0); o[2] = fb.Get(v, 0); o[3] = fa.Get(v, 255);
                    if (fa.mask && o[3]) anyAlpha = true;
                }
                break;
            case 24:
                for (uint32_t x = 0; x < w; x++, o += 4, s += 3) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; }
                break;
            case 32:
                if (stdBGRA32) {
                    for (uint32_t x = 0; x < w; x++, o += 4, s += 4) {
                        o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3];
                        if (s[3]) anyAlpha = true;
                    }
                } else {
                    for (uint32_t x = 0; x < w; x++, o += 4) {
                        uint32_t v = rd32le(s + x * 4);
                        o[0] = fr.Get(v, 0); o[1] = fg.Get(v, 0); o[2] = fb.Get(v, 0); o[3] = fa.Get(v, 255);
                        if (fa.mask && o[3]) anyAlpha = true;
                    }
                }
                break;
            default: return false;
            }
        }

        bool hasAlphaChannel = (bpp == 32 || (bpp == 16 && fa.mask));
        if (hasAlphaChannel && !anyAlpha) {
            if (!(explicitAlpha && comp != 0 && bpp == 16)) {
                for (size_t i = 3; i < (size_t)w * h * 4; i += 4) ob[i] = 255;
            }
        }

        if (icon && !(bpp == 32 && anyAlpha)) {
            size_t mstride = (((size_t)w + 31) / 32) * 4;
            size_t moff = stride * h;
            if (moff + mstride * h <= avail) {
                for (uint32_t r = 0; r < h; r++) {
                    const uint8_t* ms = px + moff + (size_t)r * mstride;
                    uint32_t dy = topDown ? r : (h - 1 - r);
                    uint8_t* o = ob + (size_t)dy * w * 4;
                    for (uint32_t x = 0; x < w; x++) if ((ms[x >> 3] >> (7 - (x & 7))) & 1) o[x * 4 + 3] = 0;
                }
            }
        }
        return true;
    }
    static inline bool planesInvalid(int bpp) {
        return !(bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32);
    }

    // ====================================================================================================================
    //  ICO / CUR
    // ====================================================================================================================
        template<typename ByteStr>
    static bool decode_ico(const uint8_t* d, size_t n, uint32_t& W, uint32_t& H, ByteStr& out, int sizeHint) {
        if (n < 22 || d[0] != 0 || d[1] != 0 || (d[2] != 1 && d[2] != 2) || d[3] != 0) return false;
        bool isCur = (d[2] == 2);
        uint32_t count = rd16le(d + 4);
        if (count == 0 || 6 + (size_t)count * 16 > n) return false;

        int best = -1; int64_t bestKey = 0;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* e = d + 6 + i * 16;
            uint32_t w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256;
            uint32_t sz = rd32le(e + 8), off = rd32le(e + 12);
            if (sz == 0 || off >= n || sz > n - off) continue;
            int bpp = isCur ? 0 : rd16le(e + 6);
            int64_t key;
            if (sizeHint > 0) {
                int64_t diff = (int64_t)(w > (uint32_t)sizeHint ? w - sizeHint : sizeHint - w);
                key = -(diff * 1024) + bpp;
            } else {
                key = (int64_t)w * h * 1024 + bpp;
            }
            if (best < 0 || key > bestKey) { best = (int)i; bestKey = key; }
        }
        if (best < 0) return false;
        const uint8_t* e = d + 6 + best * 16;
        uint32_t sz = rd32le(e + 8), off = rd32le(e + 12);
        if (isCur) { rt().hotspotX = rd16le(e + 4); rt().hotspotY = rd16le(e + 6); }
        const uint8_t* img = d + off;
        if (sz >= 8 && img[0] == 0x89 && img[1] == 'P' && img[2] == 'N' && img[3] == 'G') return decode_png(img, sz, W, H, out, 0);
        return decode_dib(img, sz, 0, true, W, H, out);
    }

    // ====================================================================================================================
    //  TGA
    // ====================================================================================================================
    static void tga_color(const uint8_t* s, int depth, int alphaBits, uint8_t* o) {
        if (depth == 15 || depth == 16) {
            uint32_t v = rd16le(s);
            uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
            o[0] = (uint8_t)((r << 3) | (r >> 2)); o[1] = (uint8_t)((g << 3) | (g >> 2)); o[2] = (uint8_t)((b << 3) | (b >> 2));
            o[3] = (depth == 16 && alphaBits > 0) ? ((v & 0x8000) ? 255 : 0) : 255;
        } else if (depth == 24) {
            o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255;
        } else {
            o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3];
        }
    }

        template<typename ByteStr>
    static bool decode_tga(const uint8_t* d, size_t n, uint32_t& W, uint32_t& H, ByteStr& out, int = 0) {
        if (n < 18 || !looks_like_tga(d, n)) return false;
        uint8_t idLen = d[0], cmType = d[1], type = d[2];
        uint32_t cmFirst = rd16le(d + 3), cmLen = rd16le(d + 5);
        int cmDepth = d[7];
        uint32_t w = rd16le(d + 12), h = rd16le(d + 14);
        int bpp = d[16];
        uint8_t desc = d[17];
        int alphaBits = desc & 0x0F;
        bool rle = type >= 9;
        int base = type & 7;
        if (dim_over(w, h)) return false;

        size_t pos = 18 + (size_t)idLen;
        const uint8_t* cm = nullptr;
        size_t cmBytes = (cmDepth + 7) / 8;
        if (cmType == 1) {
            size_t total = (size_t)cmLen * cmBytes;
            if (pos + total > n) return false;
            cm = d + pos;
            pos += total;
        }
        int pixBytes = (bpp + 7) / 8;
        size_t npix = (size_t)w * h;

        knst_img_array<uint8_t> rawStore;
        const uint8_t* raw = nullptr;
        if (!rle) {
            if (pos > n || npix * pixBytes > n - pos) return false;
            raw = d + pos;
        } else {
            if (!rawStore.resize(npix * pixBytes)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
            size_t i = 0, p = pos;
            while (i < npix) {
                if (p >= n) return false;
                uint8_t hdr = d[p++];
                size_t cnt = (size_t)(hdr & 0x7F) + 1;
                if (cnt > npix - i) cnt = npix - i;
                if (hdr & 0x80) {
                    if (p + pixBytes > n) return false;
                    for (size_t k = 0; k < cnt; k++) std::memcpy(&rawStore[(i + k) * pixBytes], d + p, pixBytes);
                    p += pixBytes;
                } else {
                    if (p + cnt * pixBytes > n) return false;
                    std::memcpy(&rawStore[i * pixBytes], d + p, cnt * pixBytes);
                    p += cnt * pixBytes;
                }
                i += cnt;
            }
            raw = rawStore.data();
        }

        if (!alloc_rgba(w, h, out)) return false;
        W = w; H = h;
        uint8_t* ob = &out[0];
        bool topToBottom = (desc & 0x20) != 0;
        bool rightToLeft = (desc & 0x10) != 0;
        bool has32 = (base == 2 && bpp == 32) || (base == 1 && cmDepth == 32);

        for (uint32_t r = 0; r < h; r++) {
            uint32_t dy = topToBottom ? r : (h - 1 - r);
            uint8_t* row = ob + (size_t)dy * w * 4;
            const uint8_t* s = raw + (size_t)r * w * pixBytes;
            for (uint32_t c = 0; c < w; c++, s += pixBytes) {
                uint32_t dx = rightToLeft ? (w - 1 - c) : c;
                uint8_t* o = row + (size_t)dx * 4;
                if (base == 2) {
                    tga_color(s, bpp, alphaBits, o);
                } else if (base == 3) {
                    o[0] = o[1] = o[2] = s[0];
                    o[3] = (pixBytes == 2) ? s[1] : 255;
                } else {
                    uint32_t idx = (pixBytes == 1) ? s[0] : rd16le(s);
                    if (idx < cmFirst || idx - cmFirst >= cmLen) { o[0] = o[1] = o[2] = 0; o[3] = 255; }
                    else tga_color(cm + (size_t)(idx - cmFirst) * cmBytes, cmDepth, alphaBits, o);
                }
            }
        }
        
        if (has32 && alphaBits == 0) {
            bool any = false;
            for (size_t i = 3; i < npix * 4; i += 4) if (ob[i]) { any = true; break; }
            if (!any) for (size_t i = 3; i < npix * 4; i += 4) ob[i] = 255;
        }
        return true;
    }

    // ====================================================================================================================
    //  PNM (PBM/PGM/PPM, ASCII + binary) ve PAM (P7)
    // ====================================================================================================================
    static bool pnm_skip(const uint8_t*& p, const uint8_t* e) {
        for (;;) {
            while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f')) p++;
            if (p < e && *p == '#') { while (p < e && *p != '\n' && *p != '\r') p++; continue; }
            break;
        }
        return p < e;
    }
    static bool pnm_int(const uint8_t*& p, const uint8_t* e, uint32_t& v) {
        if (!pnm_skip(p, e)) return false;
        if (*p < '0' || *p > '9') return false;
        uint64_t x = 0;
        while (p < e && *p >= '0' && *p <= '9') { x = x * 10 + (*p - '0'); if (x > 0x7FFFFFFFull) return false; p++; }
        v = (uint32_t)x;
        return true;
    }

    struct PnmReader { 
        const uint8_t* p; const uint8_t* e; bool ascii; bool wide;
        inline bool Next(uint32_t& v) {
            if (ascii) return pnm_int(p, e, v);
            if (wide) { if (e - p < 2) return false; v = ((uint32_t)p[0] << 8) | p[1]; p += 2; return true; }
            if (p >= e) return false;
            v = *p++;
            return true;
        }
    };

        template<typename ByteStr>
    static bool decode_pnm(const uint8_t* d, size_t n, uint32_t& W, uint32_t& H, ByteStr& out, int = 0) {
        if (n < 3 || d[0] != 'P' || d[1] < '1' || d[1] > '7') return false;
        int kind = d[1] - '0';
        const uint8_t* p = d + 2;
        const uint8_t* e = d + n;
        uint32_t w = 0, h = 0, maxv = 1, depth = 1;
        bool pamBW = false;

        if (kind == 7) {
            bool end = false;
            depth = 0; maxv = 0;
            while (!end) {
                while (p < e && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')) p++;
                if (p >= e) return false;
                if (*p == '#') { while (p < e && *p != '\n') p++; continue; }
                const uint8_t* ls = p;
                while (p < e && *p != '\n' && *p != '\r') p++;
                size_t len = (size_t)(p - ls);
                auto starts = [&](const char* k) { size_t kl = std::strlen(k); return len >= kl && std::memcmp(ls, k, kl) == 0; };
                auto num = [&](size_t off) { uint32_t v = 0; const uint8_t* q = ls + off; const uint8_t* qe = ls + len;
                                              while (q < qe && (*q == ' ' || *q == '\t')) q++;
                                              while (q < qe && *q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; } return v; };
                if (starts("ENDHDR")) end = true;
                else if (starts("WIDTH")) w = num(5);
                else if (starts("HEIGHT")) h = num(6);
                else if (starts("DEPTH")) depth = num(5);
                else if (starts("MAXVAL")) maxv = num(6);
                else if (starts("TUPLTYPE")) { if (len >= 20 && std::memcmp(ls + 9, "BLACKANDWHITE", 13) == 0) pamBW = true; }
            }
            if (p < e) p++; 
            if (depth < 1 || depth > 4) return false;
        } else {
            if (!pnm_int(p, e, w) || !pnm_int(p, e, h)) return false;
            if (kind != 1 && kind != 4) { if (!pnm_int(p, e, maxv)) return false; }
            if (kind == 3 || kind == 6) depth = 3;
            if (kind == 4 || kind == 5 || kind == 6) { if (p < e) p++; }
        }
        if (w == 0 || h == 0 || maxv == 0 || maxv > 65535) return false;
        if (!alloc_rgba(w, h, out)) return false;
        W = w; H = h;
        uint8_t* ob = &out[0];
        size_t npix = (size_t)w * h;

       
        if (kind == 4) {
            size_t stride = ((size_t)w + 7) / 8;
            if ((size_t)(e - p) < stride * h) return false;
            for (uint32_t y = 0; y < h; y++) {
                const uint8_t* s = p + (size_t)y * stride;
                uint8_t* o = ob + (size_t)y * w * 4;
                for (uint32_t x = 0; x < w; x++, o += 4) {
                    uint8_t v = ((s[x >> 3] >> (7 - (x & 7))) & 1) ? 0 : 255; 
                    o[0] = o[1] = o[2] = v; o[3] = 255;
                }
            }
            return true;
        }
        if (kind == 1) {
            for (size_t i = 0; i < npix; i++) {
                if (!pnm_skip(p, e)) return false;
                uint8_t c = *p++;
                if (c != '0' && c != '1') return false;
                uint8_t v = (c == '1') ? 0 : 255;
                uint8_t* o = ob + i * 4;
                o[0] = o[1] = o[2] = v; o[3] = 255;
            }
            return true;
        }

        if ((kind == 6 || kind == 5) && maxv == 255) {
            size_t need = npix * depth;
            if ((size_t)(e - p) < need) return false;
            const uint8_t* s = p;
            if (kind == 6) { for (size_t i = 0; i < npix; i++, s += 3) { uint8_t* o = ob + i * 4; o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; } }
            else { for (size_t i = 0; i < npix; i++, s++) { uint8_t* o = ob + i * 4; o[0] = o[1] = o[2] = *s; o[3] = 255; } }
            return true;
        }

        PnmReader rd{p, e, (kind == 2 || kind == 3), maxv > 255};
        auto scale = [&](uint32_t v) -> uint8_t {
            if (v > maxv) v = maxv;
            return (uint8_t)(((uint64_t)v * 255 + maxv / 2) / maxv);
        };
        for (size_t i = 0; i < npix; i++) {
            uint32_t v[4] = {0, 0, 0, 0};
            for (uint32_t c = 0; c < depth; c++) if (!rd.Next(v[c])) return false;
            uint8_t* o = ob + i * 4;
            uint8_t a = 255;
            if (depth == 1)      { uint8_t g = scale(v[0]); if (pamBW) g = scale(v[0]); o[0] = o[1] = o[2] = g; }
            else if (depth == 2) { uint8_t g = scale(v[0]); o[0] = o[1] = o[2] = g; a = scale(v[1]); }
            else if (depth == 3) { o[0] = scale(v[0]); o[1] = scale(v[1]); o[2] = scale(v[2]); }
            else                 { o[0] = scale(v[0]); o[1] = scale(v[1]); o[2] = scale(v[2]); a = scale(v[3]); }
            o[3] = a;
        }
        return true;
    }

    // ====================================================================================================================
    //  GIF (ilk kare)
    // ====================================================================================================================
        template<typename ByteStr>
    static bool decode_gif(const uint8_t* d, size_t n, uint32_t& W, uint32_t& H, ByteStr& out, int = 0) {
        if (n < 13 || std::memcmp(d, "GIF8", 4) != 0) return false;
        uint32_t lw = rd16le(d + 6), lh = rd16le(d + 8);
        uint8_t flags = d[10];
        size_t pos = 13;
        const uint8_t* gct = nullptr; size_t gctN = 0;
        if (flags & 0x80) {
            gctN = (size_t)2 << (flags & 7);
            if (pos + gctN * 3 > n) return false;
            gct = d + pos; pos += gctN * 3;
        }
        int trans = -1;
        while (pos < n) {
            uint8_t b = d[pos++];
            if (b == 0x3B) return false;
            if (b == 0x21) {
                if (pos >= n) return false;
                uint8_t label = d[pos++];
                if (label == 0xF9 && pos + 5 <= n && d[pos] >= 4) {
                    if (d[pos + 1] & 1) trans = d[pos + 4];
                }
                while (pos < n) { uint8_t sz = d[pos++]; if (!sz) break; pos += sz; }
                continue;
            }
            if (b != 0x2C) return false;

            if (pos + 9 > n) return false;
            uint32_t fx = rd16le(d + pos), fy = rd16le(d + pos + 2), fw = rd16le(d + pos + 4), fh = rd16le(d + pos + 6);
            uint8_t fl = d[pos + 8];
            pos += 9;
            const uint8_t* ct = gct; size_t ctN = gctN;
            if (fl & 0x80) {
                ctN = (size_t)2 << (fl & 7);
                if (pos + ctN * 3 > n) return false;
                ct = d + pos; pos += ctN * 3;
            }
            bool interlaced = (fl & 0x40) != 0;
            if (fw == 0 || fh == 0 || pos >= n) return false;
            int minCode = d[pos++];
            if (minCode < 1 || minCode > 11) return false;

            knst_img_array<uint8_t> lz;
            while (pos < n) {
                uint8_t sz = d[pos++];
                if (!sz) break;
                size_t take = std::min<size_t>(sz, n - pos);
                if (!lz.append(d + pos, take)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
                pos += take;
            }

            uint32_t cw = lw ? lw : fw, ch = lh ? lh : fh;
            if (dim_over(cw, ch) || dim_over(fw, fh)) return false;

            // LZW
            size_t total = (size_t)fw * fh;
            knst_img_array<uint8_t> idx;
            if (!idx.resize(total)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
            {
                int clear = 1 << minCode, eoi = clear + 1, next = clear + 2, size = minCode + 1;
                static thread_local uint16_t prefix[4096];
                static thread_local uint8_t suffix[4096];
                static thread_local uint8_t stack[4097];
                uint32_t bitbuf = 0; int bitcnt = 0; size_t lp = 0, np = 0;
                int prev = -1; uint8_t first = 0;
                while (np < total) {
                    while (bitcnt < size && lp < lz.size()) { bitbuf |= (uint32_t)lz[lp++] << bitcnt; bitcnt += 8; }
                    if (bitcnt < size) break;
                    int code = (int)(bitbuf & ((1u << size) - 1));
                    bitbuf >>= size; bitcnt -= size;
                    if (code == clear) { size = minCode + 1; next = clear + 2; prev = -1; continue; }
                    if (code == eoi) break;
                    if (prev < 0) {
                        if (code >= clear) break;
                        idx[np++] = (uint8_t)code; prev = code; first = (uint8_t)code;
                        continue;
                    }
                    int cur = code, sp = 0;
                    if (code > next || (code == next && next >= 4096)) break;
                    if (code == next) { stack[sp++] = first; cur = prev; }
                    while (cur >= clear) { if (sp >= 4096) return false; stack[sp++] = suffix[cur]; cur = prefix[cur]; }
                    stack[sp++] = (uint8_t)cur;
                    first = (uint8_t)cur;
                    while (sp && np < total) idx[np++] = stack[--sp];
                    if (next < 4096) {
                        prefix[next] = (uint16_t)prev; suffix[next] = first; next++;
                        if (next == (1 << size) && size < 12) size++;
                    }
                    prev = code;
                }
            }

            if (!alloc_rgba(cw, ch, out)) return false;
            W = cw; H = ch;
            uint8_t* ob = &out[0];
            static const int istart[4] = {0, 4, 2, 1}, istep[4] = {8, 8, 4, 2};
            uint32_t srcRow = 0;
            auto drawRow = [&](uint32_t destRow, uint32_t sr) {
                uint32_t oy = fy + destRow;
                if (oy >= ch) return;
                const uint8_t* s = &idx[(size_t)sr * fw];
                for (uint32_t x = 0; x < fw; x++) {
                    uint32_t ox = fx + x;
                    if (ox >= cw) break;
                    uint8_t v = s[x];
                    if ((int)v == trans) continue;
                    uint8_t* o = ob + ((size_t)oy * cw + ox) * 4;
                    if (ct && v < ctN) { o[0] = ct[v*3]; o[1] = ct[v*3+1]; o[2] = ct[v*3+2]; }
                    else if (ct) { o[0] = o[1] = o[2] = 0; }
                    else { o[0] = o[1] = o[2] = v; }
                    o[3] = 255;
                }
            };
            if (!interlaced) { for (uint32_t y = 0; y < fh; y++) drawRow(y, y); }
            else { for (int ps = 0; ps < 4; ps++) for (uint32_t y = istart[ps]; y < fh; y += istep[ps]) drawRow(y, srcRow++); }
            return true;
        }
        return false;
    }

    // ====================================================================================================================
    //  JPEG (baseline / extended sequential / progressive, Huffman, 8 bit)
    // ====================================================================================================================
    struct JpegDecoder {
        static const uint8_t* ZZ() {
            static const uint8_t t[64] = {0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
                                          35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
            return t;
        }

        struct Huff {
            uint8_t vals[256];
            int maxcode[18];
            int mincode[18];
            int valptr[18];
            uint16_t fast[512];
            bool valid = false;

            bool Build(const uint8_t* counts /*16*/, const uint8_t* v, int nv) {
                valid = false;
                std::memset(fast, 0, sizeof(fast));
                std::memcpy(vals, v, nv);
                int code = 0, k = 0;
                for (int l = 1; l <= 16; l++) {
                    valptr[l] = k; mincode[l] = code;
                    int cnt = counts[l - 1];
                    for (int i = 0; i < cnt; i++) {
                        if (code >= (1 << l)) return false;
                        if (l <= 9) {
                            int base = code << (9 - l), ncopy = 1 << (9 - l);
                            for (int j = 0; j < ncopy; j++) fast[base + j] = (uint16_t)((l << 8) | vals[k]);
                        }
                        code++; k++;
                    }
                    maxcode[l] = cnt ? code - 1 : -1;
                    code <<= 1;
                }
                valid = true;
                return true;
            }
        };

        struct Bits { 
            const uint8_t* p; const uint8_t* end;
            uint32_t buf = 0; int cnt = 0; int marker = 0;
            Bits(const uint8_t* a, const uint8_t* b) : p(a), end(b) {}

            void Fill() {
                while (cnt <= 24) {
                    uint32_t b = 0;
                    if (!marker) {
                        if (p >= end) marker = 0xD9;
                        else if (*p == 0xFF) {
                            const uint8_t* q = p + 1;
                            while (q < end && *q == 0xFF) q++;
                            if (q >= end) marker = 0xD9;
                            else if (*q == 0) { b = 0xFF; p = q + 1; }
                            else { marker = *q; p = q - 1; }
                        } else b = *p++;
                    }
                    buf |= b << (24 - cnt);
                    cnt += 8;
                }
            }
            inline uint32_t Get(int n) {
                if (n == 0) return 0;
                if (cnt < n) Fill();
                uint32_t v = buf >> (32 - n);
                buf <<= n; cnt -= n;
                return v;
            }
            inline int Bit() { return (int)Get(1); }

            bool NextRestart() {
                buf = 0; cnt = 0;
                if (marker) {
                    if (marker >= 0xD0 && marker <= 0xD7) { p += 2; marker = 0; return true; }
                    return false;
                }
                while (p + 1 < end) {
                    if (p[0] == 0xFF && p[1] >= 0xD0 && p[1] <= 0xD7) { p += 2; return true; }
                    if (p[0] == 0xFF && p[1] != 0 && p[1] != 0xFF) { marker = p[1]; return false; }
                    p++;
                }
                return false;
            }
        };

        struct Comp {
            int id = 0, h = 1, v = 1, tq = 0, td = 0, ta = 0;
            int bwa = 0, bha = 0;
            int bwr = 0, bhr = 0;
            int cw = 0, ch = 0;
            int pred = 0;
            knst_img_array<int16_t> coef;
            knst_img_array<uint8_t> plane;
            knst_img_array<uint8_t> full;   // upsampled plane
            const uint8_t* fp = nullptr; int fstride = 0;
        };

        const uint8_t* d; size_t n;
        uint32_t W = 0, H = 0;
        int nc = 0;
        bool progressive = false, frame = false;
        Comp comp[4];
        uint16_t qt[4][64]; bool qtSet[4] = {false, false, false, false};
        Huff hdc[4], hac[4];
        int hmax = 1, vmax = 1, mcux = 0, mcuy = 0, ri = 0;
        int adobe = -1; bool jfif = false;
        int scans = 0;
        int Ss = 0, Se = 63, Ah = 0, Al = 0, eobrun = 0;

        JpegDecoder(const uint8_t* data, size_t size) : d(data), n(size) { std::memset(qt, 0, sizeof(qt)); }

        static inline int Extend(int v, int t) { return v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }

        static inline int Dec(Bits& br, const Huff& h) {
            if (br.cnt < 16) br.Fill();
            uint16_t e = h.fast[br.buf >> 23];
            if (e) { int l = e >> 8; br.buf <<= l; br.cnt -= l; return e & 255; }
            uint32_t code16 = br.buf >> 16;
            for (int l = 10; l <= 16; l++) {
                int c = (int)(code16 >> (16 - l));
                if (c <= h.maxcode[l]) {
                    br.buf <<= l; br.cnt -= l;
                    return h.vals[h.valptr[l] + c - h.mincode[l]];
                }
            }
            return -1;
        }

        bool BlockBaseline(Bits& br, Comp& c, int16_t* blk) {
            int t = Dec(br, hdc[c.td]);
            if (t < 0 || t > 15) return false;
            int diff = t ? Extend((int)br.Get(t), t) : 0;
            c.pred += diff;
            blk[0] = (int16_t)c.pred;
            const Huff& ac = hac[c.ta];
            const uint8_t* zz = ZZ();
            for (int k = 1; k < 64;) {
                int rs = Dec(br, ac);
                if (rs < 0) return false;
                int r = rs >> 4, s = rs & 15;
                if (!s) { if (r != 15) break; k += 16; continue; }
                k += r;
                if (k > 63) return false;
                blk[zz[k]] = (int16_t)Extend((int)br.Get(s), s);
                k++;
            }
            return true;
        }

        bool BlockDCFirst(Bits& br, Comp& c, int16_t* blk) {
            int t = Dec(br, hdc[c.td]);
            if (t < 0 || t > 15) return false;
            int diff = t ? Extend((int)br.Get(t), t) : 0;
            c.pred += diff;
            blk[0] = (int16_t)(c.pred * (1 << Al));
            return true;
        }
        bool BlockDCRefine(Bits& br, Comp&, int16_t* blk) {
            if (br.Bit()) blk[0] = (int16_t)(blk[0] | (1 << Al));
            return true;
        }
        bool BlockACFirst(Bits& br, Comp& c, int16_t* blk) {
            if (eobrun > 0) { eobrun--; return true; }
            const Huff& ac = hac[c.ta];
            const uint8_t* zz = ZZ();
            for (int k = Ss; k <= Se;) {
                int rs = Dec(br, ac);
                if (rs < 0) return false;
                int r = rs >> 4, s = rs & 15;
                if (!s) {
                    if (r < 15) {
                        eobrun = (1 << r) - 1;
                        if (r) eobrun += (int)br.Get(r);
                        break;
                    }
                    k += 16; continue;
                }
                k += r;
                if (k > 63) return false;
                blk[zz[k]] = (int16_t)(Extend((int)br.Get(s), s) * (1 << Al));
                k++;
            }
            return true;
        }
        bool BlockACRefine(Bits& br, Comp& c, int16_t* blk) {
            const Huff& ac = hac[c.ta];
            const uint8_t* zz = ZZ();
            int p1 = 1 << Al, m1 = -(1 << Al);
            int k = Ss;
            if (eobrun == 0) {
                for (; k <= Se; k++) {
                    int rs = Dec(br, ac);
                    if (rs < 0) return false;
                    int r = rs >> 4, s = rs & 15;
                    if (s) {
                        if (s != 1) return false;
                        s = br.Bit() ? p1 : m1;
                    } else if (r != 15) {
                        eobrun = 1 << r;
                        if (r) eobrun += (int)br.Get(r);
                        break;
                    }
                    do {
                        int16_t* cf = &blk[zz[k]];
                        if (*cf != 0) {
                            if (br.Bit()) { if ((*cf & p1) == 0) { if (*cf >= 0) *cf = (int16_t)(*cf + p1); else *cf = (int16_t)(*cf + m1); } }
                        } else {
                            if (--r < 0) break;
                        }
                        k++;
                    } while (k <= Se);
                    if (s && k <= Se) blk[zz[k]] = (int16_t)s;
                }
            }
            if (eobrun > 0) {
                for (; k <= Se; k++) {
                    int16_t* cf = &blk[zz[k]];
                    if (*cf != 0) {
                        if (br.Bit()) { if ((*cf & p1) == 0) { if (*cf >= 0) *cf = (int16_t)(*cf + p1); else *cf = (int16_t)(*cf + m1); } }
                    }
                }
                eobrun--;
            }
            return true;
        }

        bool Block(Bits& br, Comp& c, int16_t* blk) {
            if (!progressive) return BlockBaseline(br, c, blk);
            if (Ss == 0) return Ah == 0 ? BlockDCFirst(br, c, blk) : BlockDCRefine(br, c, blk);
            return Ah == 0 ? BlockACFirst(br, c, blk) : BlockACRefine(br, c, blk);
        }

        bool ScanStep(Bits& br, Comp& c, int bx, int by) {
            int16_t* blk = &c.coef[((size_t)by * c.bwa + bx) * 64];
            return Block(br, c, blk);
        }

        bool DecodeScan(Bits& br, Comp** sc, int ns) {
            for (int i = 0; i < ns; i++) sc[i]->pred = 0;
            eobrun = 0;
            int count = 0;
            auto restart = [&]() -> bool {
                if (!br.NextRestart()) return false;
                for (int i = 0; i < ns; i++) sc[i]->pred = 0;
                eobrun = 0;
                return true;
            };
            if (ns == 1) {
                Comp& c = *sc[0];
                int total = c.bwr * c.bhr;
                for (int i = 0; i < total; i++) {
                    if (ri && i && (i % ri) == 0) { if (!restart()) return false; }
                    if (!ScanStep(br, c, i % c.bwr, i / c.bwr)) return false;
                }
            } else {
                for (int my = 0; my < mcuy; my++) for (int mx = 0; mx < mcux; mx++) {
                    if (ri && count && (count % ri) == 0) { if (!restart()) return false; }
                    count++;
                    for (int i = 0; i < ns; i++) {
                        Comp& c = *sc[i];
                        for (int v = 0; v < c.v; v++) for (int h = 0; h < c.h; h++)
                            if (!ScanStep(br, c, mx * c.h + h, my * c.v + v)) return false;
                    }
                }
            }
            return true;
        }

        // ---------------- IDCT (jidctint benzeri tamsayi)
        static inline int f2f(double x) { return (int)(x * 4096 + 0.5); }
        static inline uint8_t clamp8(int x) { return (uint8_t)(x < 0 ? 0 : (x > 255 ? 255 : x)); }

        struct Idct1D { int x0, x1, x2, x3, t0, t1, t2, t3; };
        static inline Idct1D Idct1(int s0, int s1, int s2, int s3, int s4, int s5, int s6, int s7) {
            static const int c0_541 = f2f(0.5411961), cm1_847 = f2f(-1.847759065), c0_765 = f2f(0.765366865),
                             c1_175 = f2f(1.175875602), c0_298 = f2f(0.298631336), c2_053 = f2f(2.053119869),
                             c3_072 = f2f(3.072711026), c1_501 = f2f(1.501321110), cm0_899 = f2f(-0.899976223),
                             cm2_562 = f2f(-2.562915447), cm1_961 = f2f(-1.961570560), cm0_390 = f2f(-0.390180644);
            Idct1D r;
            int p2 = s2, p3 = s6;
            int p1 = (p2 + p3) * c0_541;
            int t2 = p1 + p3 * cm1_847;
            int t3 = p1 + p2 * c0_765;
            p2 = s0; p3 = s4;
            int t0 = (p2 + p3) * 4096;
            int t1 = (p2 - p3) * 4096;
            r.x0 = t0 + t3; r.x3 = t0 - t3; r.x1 = t1 + t2; r.x2 = t1 - t2;
            t0 = s7; t1 = s5; t2 = s3; t3 = s1;
            p3 = t0 + t2; int p4 = t1 + t3; p1 = t0 + t3; p2 = t1 + t2;
            int p5 = (p3 + p4) * c1_175;
            t0 = t0 * c0_298; t1 = t1 * c2_053; t2 = t2 * c3_072; t3 = t3 * c1_501;
            p1 = p5 + p1 * cm0_899; p2 = p5 + p2 * cm2_562; p3 = p3 * cm1_961; p4 = p4 * cm0_390;
            t3 += p1 + p4; t2 += p2 + p3; t1 += p2 + p4; t0 += p1 + p3;
            r.t0 = t0; r.t1 = t1; r.t2 = t2; r.t3 = t3;
            return r;
        }

        static void IdctBlock(const int16_t* in, const uint16_t* q, uint8_t* out, int stride) {
            int tmp[64];
            int v[64];
            for (int i = 0; i < 64; i++) tmp[i] = (int)in[i] * (int)q[i];
            for (int i = 0; i < 8; i++) {
                const int* s = tmp + i;
                if (s[8] == 0 && s[16] == 0 && s[24] == 0 && s[32] == 0 && s[40] == 0 && s[48] == 0 && s[56] == 0) {
                    int dc = s[0] * 4;
                    for (int k = 0; k < 8; k++) v[i + k * 8] = dc;
                } else {
                    Idct1D r = Idct1(s[0], s[8], s[16], s[24], s[32], s[40], s[48], s[56]);
                    r.x0 += 512; r.x1 += 512; r.x2 += 512; r.x3 += 512;
                    v[i]      = (r.x0 + r.t3) >> 10; v[i + 56] = (r.x0 - r.t3) >> 10;
                    v[i + 8]  = (r.x1 + r.t2) >> 10; v[i + 48] = (r.x1 - r.t2) >> 10;
                    v[i + 16] = (r.x2 + r.t1) >> 10; v[i + 40] = (r.x2 - r.t1) >> 10;
                    v[i + 24] = (r.x3 + r.t0) >> 10; v[i + 32] = (r.x3 - r.t0) >> 10;
                }
            }
            for (int i = 0; i < 8; i++) {
                const int* s = v + i * 8;
                Idct1D r = Idct1(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
                int bias = 65536 + (128 << 17);
                r.x0 += bias; r.x1 += bias; r.x2 += bias; r.x3 += bias;
                uint8_t* o = out + (size_t)i * stride;
                o[0] = clamp8((r.x0 + r.t3) >> 17); o[7] = clamp8((r.x0 - r.t3) >> 17);
                o[1] = clamp8((r.x1 + r.t2) >> 17); o[6] = clamp8((r.x1 - r.t2) >> 17);
                o[2] = clamp8((r.x2 + r.t1) >> 17); o[5] = clamp8((r.x2 - r.t1) >> 17);
                o[3] = clamp8((r.x3 + r.t0) >> 17); o[4] = clamp8((r.x3 - r.t0) >> 17);
            }
        }

        static void Upsample(const uint8_t* src, int sstride, int cw, int ch, int fx, int fy,
                             uint8_t* dst, uint32_t W, uint32_t H) {
            if (fx == 2 && fy == 1) {
                for (uint32_t y = 0; y < H; y++) {
                    const uint8_t* row = src + (size_t)std::min<int>((int)y, ch - 1) * sstride;
                    uint8_t* o = dst + (size_t)y * W;
                    for (uint32_t x = 0; x < W; x++) {
                        int i = (int)(x >> 1);
                        int cur = row[std::min(i, cw - 1)];
                        int nb = (x & 1) ? row[std::min(i + 1, cw - 1)] : row[std::max(i - 1, 0)];
                        o[x] = (uint8_t)((3 * cur + nb + ((x & 1) ? 2 : 1)) >> 2);
                    }
                }
            } else if (fx == 1 && fy == 2) {
                for (uint32_t y = 0; y < H; y++) {
                    int j = (int)(y >> 1);
                    const uint8_t* cr = src + (size_t)std::min(j, ch - 1) * sstride;
                    int nj = (y & 1) ? std::min(j + 1, ch - 1) : std::max(j - 1, 0);
                    const uint8_t* nr = src + (size_t)nj * sstride;
                    uint8_t* o = dst + (size_t)y * W;
                    int bias = (y & 1) ? 2 : 1;
                    for (uint32_t x = 0; x < W; x++) o[x] = (uint8_t)((3 * cr[std::min<int>((int)x, cw - 1)] + nr[std::min<int>((int)x, cw - 1)] + bias) >> 2);
                }
            } else if (fx == 2 && fy == 2) {
                knst_img_array<int> cs;
                if (!cs.resize((size_t)cw)) return;
                for (uint32_t y = 0; y < H; y++) {
                    int j = (int)(y >> 1);
                    const uint8_t* cr = src + (size_t)std::min(j, ch - 1) * sstride;
                    int nj = (y & 1) ? std::min(j + 1, ch - 1) : std::max(j - 1, 0);
                    const uint8_t* nr = src + (size_t)nj * sstride;
                    for (int i = 0; i < cw; i++) cs[i] = 3 * cr[i] + nr[i];
                    uint8_t* o = dst + (size_t)y * W;
                    for (uint32_t x = 0; x < W; x++) {
                        int i = (int)(x >> 1);
                        int cur = cs[std::min(i, cw - 1)];
                        int nb = (x & 1) ? cs[std::min(i + 1, cw - 1)] : cs[std::max(i - 1, 0)];
                        o[x] = (uint8_t)((3 * cur + nb + ((x & 1) ? 7 : 8)) >> 4);
                    }
                }
            }
        }

        static void UpsampleNearest(const uint8_t* src, int sstride, int cw, int ch, int h, int v, int hmax, int vmax,
                                    uint8_t* dst, uint32_t W, uint32_t H) {
            for (uint32_t y = 0; y < H; y++) {
                int sy = std::min<int>((int)((uint64_t)y * v / vmax), ch - 1);
                const uint8_t* row = src + (size_t)sy * sstride;
                uint8_t* o = dst + (size_t)y * W;
                for (uint32_t x = 0; x < W; x++) o[x] = row[std::min<int>((int)((uint64_t)x * h / hmax), cw - 1)];
            }
        }

        template<typename ByteStr>
        bool Run(ByteStr& out) {
            if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
            const uint8_t* p = d + 2;
            const uint8_t* end = d + n;
            for (;;) {
                while (p < end && *p != 0xFF) p++;
                while (p < end && *p == 0xFF) p++;
                if (p >= end) break;
                uint8_t m = *p++;
                if (m == 0 || m == 0x01 || m == 0xD8 || (m >= 0xD0 && m <= 0xD7)) continue;
                if (m == 0xD9) break;
                if (p + 2 > end) break;
                size_t len = rd16be(p);
                if (len < 2 || (size_t)(end - p) < len) break;
                const uint8_t* seg = p + 2;
                size_t slen = len - 2;
                p += len;

                switch (m) {
                case 0xC0: case 0xC1: case 0xC2:
                    if (frame) return false;
                    progressive = (m == 0xC2);
                    if (!ParseSOF(seg, slen)) return false;
                    break;
                case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
                    return false; // lossless / hiyerarsik / aritmetik kodlama desteklenmez
                case 0xC4: if (!ParseDHT(seg, slen)) return false; break;
                case 0xDB: if (!ParseDQT(seg, slen)) return false; break;
                case 0xDD: if (slen >= 2) ri = rd16be(seg); break;
                case 0xE0: if (slen >= 5 && std::memcmp(seg, "JFIF", 4) == 0) jfif = true; break;
                case 0xE1: if (slen >= 14 && std::memcmp(seg, "Exif\0\0", 6) == 0) ParseExif(seg + 6, slen - 6); break;
                case 0xEE: if (slen >= 12 && std::memcmp(seg, "Adobe", 5) == 0) adobe = seg[11]; break;
                case 0xDA: {
                    if (!frame) return false;
                    if (!ParseSOSAndDecode(seg, slen, p, end)) { if (scans == 0) return false; goto done; }
                    break;
                }
                default: break;
                }
            }
        done:
            if (!frame || scans == 0) return false;
            return Finish(out);
        }

        // EXIF: read the orientation tag (0x0112) from IFD0
        static void ParseExif(const uint8_t* t, size_t len) {
            if (len < 8) return;
            bool le;
            if (t[0] == 'I' && t[1] == 'I') le = true; else if (t[0] == 'M' && t[1] == 'M') le = false; else return;
            auto r16 = [&](const uint8_t* p) -> uint32_t { return le ? (uint32_t)(p[0] | (p[1] << 8)) : (uint32_t)((p[0] << 8) | p[1]); };
            auto r32 = [&](const uint8_t* p) -> uint32_t { return le ? rd32le(p) : rd32be(p); };
            if (r16(t + 2) != 42) return;
            uint32_t ifd = r32(t + 4);
            if ((size_t)ifd + 2 > len) return;
            uint32_t cnt = r16(t + ifd);
            for (uint32_t i = 0; i < cnt; i++) {
                size_t eo = (size_t)ifd + 2 + (size_t)i * 12;
                if (eo + 12 > len) return;
                if (r16(t + eo) == 0x0112) {
                    uint32_t v = r16(t + eo + 8);
                    if (v >= 1 && v <= 8) rt().orientation = (int)v;
                    return;
                }
            }
        }

        bool ParseSOF(const uint8_t* s, size_t len) {
            if (len < 6 || s[0] != 8) return false;
            H = rd16be(s + 1); W = rd16be(s + 3);
            nc = s[5];
            if (W == 0 || H == 0) return false;
            if (dim_over(W, H)) return false;
            if (nc != 1 && nc != 3 && nc != 4) return false;
            if (len < 6 + (size_t)nc * 3) return false;
            hmax = vmax = 1;
            for (int i = 0; i < nc; i++) {
                Comp& c = comp[i];
                c.id = s[6 + i * 3];
                c.h = s[7 + i * 3] >> 4; c.v = s[7 + i * 3] & 15; c.tq = s[8 + i * 3];
                if (c.h < 1 || c.h > 4 || c.v < 1 || c.v > 4 || c.tq > 3) return false;
                if (nc == 1) c.h = c.v = 1;
                hmax = std::max(hmax, c.h); vmax = std::max(vmax, c.v);
            }
            mcux = (int)((W + 8 * hmax - 1) / (8 * hmax));
            mcuy = (int)((H + 8 * vmax - 1) / (8 * vmax));
            for (int i = 0; i < nc; i++) {
                Comp& c = comp[i];
                c.bwa = mcux * c.h; c.bha = mcuy * c.v;
                c.cw = (int)(((uint64_t)W * c.h + hmax - 1) / hmax);
                c.ch = (int)(((uint64_t)H * c.v + vmax - 1) / vmax);
                c.bwr = (c.cw + 7) / 8; c.bhr = (c.ch + 7) / 8;
                if (!c.coef.resize((size_t)c.bwa * c.bha * 64)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
            }
            frame = true;
            return true;
        }

        bool ParseDQT(const uint8_t* s, size_t len) {
            const uint8_t* zz = ZZ();
            while (len >= 1) {
                int pq = s[0] >> 4, tq = s[0] & 15;
                s++; len--;
                if (tq > 3 || pq > 1) return false;
                size_t need = pq ? 128 : 64;
                if (len < need) return false;
                for (int i = 0; i < 64; i++) qt[tq][zz[i]] = pq ? rd16be(s + i * 2) : s[i];
                qtSet[tq] = true;
                s += need; len -= need;
            }
            return true;
        }

        bool ParseDHT(const uint8_t* s, size_t len) {
            while (len >= 17) {
                int tc = s[0] >> 4, th = s[0] & 15;
                if (tc > 1 || th > 3) return false;
                int total = 0;
                for (int i = 0; i < 16; i++) total += s[1 + i];
                if (total > 256 || len < (size_t)17 + total) return false;
                Huff& h = tc ? hac[th] : hdc[th];
                if (!h.Build(s + 1, s + 17, total)) return false;
                s += 17 + total; len -= 17 + total;
            }
            return true;
        }

        bool ParseSOSAndDecode(const uint8_t* s, size_t len, const uint8_t*& p, const uint8_t* end) {
            if (len < 1) return false;
            int ns = s[0];
            if (ns < 1 || ns > 4 || len < (size_t)(1 + ns * 2 + 3)) return false;
            Comp* sc[4];
            for (int i = 0; i < ns; i++) {
                int id = s[1 + i * 2], tt = s[2 + i * 2];
                int ci = -1;
                for (int k = 0; k < nc; k++) if (comp[k].id == id) { ci = k; break; }
                if (ci < 0) return false;
                comp[ci].td = tt >> 4; comp[ci].ta = tt & 15;
                if (comp[ci].td > 3 || comp[ci].ta > 3) return false;
                sc[i] = &comp[ci];
            }
            if (progressive) {
                Ss = s[1 + ns * 2]; Se = s[2 + ns * 2]; Ah = s[3 + ns * 2] >> 4; Al = s[3 + ns * 2] & 15;
                if (Ss > 63 || Se > 63 || Ss > Se || Al > 13) return false;
                if (Ss == 0 && Se != 0) return false;
                if (Ss > 0 && ns != 1) return false;
            } else { Ss = 0; Se = 63; Ah = 0; Al = 0; }

            for (int i = 0; i < ns; i++) {
                bool needDC = !progressive || (Ss == 0 && Ah == 0);
                bool needAC = !progressive || Ss > 0;
                if (needDC && !hdc[sc[i]->td].valid) return false;
                if (needAC && !hac[sc[i]->ta].valid) return false;
            }
            Bits br(p, end);
            bool ok = DecodeScan(br, sc, ns);
            scans++;
            p = br.p;
            return ok;
        }

               template<typename ByteStr>
        bool Finish(ByteStr& out) {
            for (int i = 0; i < nc; i++) {
                if (!qtSet[comp[i].tq]) return false;
            }
            // IDCT
            for (int i = 0; i < nc; i++) {
                Comp& c = comp[i];
                int stride = c.bwa * 8;
                if (!c.plane.resize((size_t)stride * c.bha * 8)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
                const uint16_t* q = qt[c.tq];
                Comp* cp = &c;
                knst_image_loader::ParallelForRows((uint32_t)c.bha, (size_t)c.bwa * 64 * 4, [cp, q, stride](uint32_t r0, uint32_t r1) {
                    for (uint32_t by = r0; by < r1; by++)
                        for (int bx = 0; bx < cp->bwa; bx++)
                            IdctBlock(&cp->coef[((size_t)by * cp->bwa + bx) * 64], q,
                                      &cp->plane[(size_t)by * 8 * stride + (size_t)bx * 8], stride);
                });
                c.coef.release();
            }
            for (int i = 0; i < nc; i++) {
                Comp& c = comp[i];
                int stride = c.bwa * 8;
                if (c.h == hmax && c.v == vmax) { c.fp = c.plane.data(); c.fstride = stride; continue; }
                if (!c.full.resize((size_t)W * H)) { set_err(KNST_IMAGE_ERR_OUT_OF_MEMORY); return false; }
                int fx = hmax / c.h, fy = vmax / c.v;
                bool exact = (hmax % c.h == 0) && (vmax % c.v == 0);
                if (exact && ((fx == 2 && fy == 1) || (fx == 1 && fy == 2) || (fx == 2 && fy == 2)))
                    Upsample(c.plane.data(), stride, c.cw, c.ch, fx, fy, c.full.data(), W, H);
                else
                    UpsampleNearest(c.plane.data(), stride, c.cw, c.ch, c.h, c.v, hmax, vmax, c.full.data(), W, H);
                c.fp = c.full.data(); c.fstride = (int)W;
                c.plane.release();
            }

            if (!alloc_rgba(W, H, out)) return false;
            uint8_t* ob = &out[0];
            bool rgbMode = false;
            if (nc == 3) {
                if (adobe == 0) rgbMode = true;
                else if (adobe < 0 && !jfif && comp[0].id == 'R' && comp[1].id == 'G' && comp[2].id == 'B') rgbMode = true;
            }
            int ncl = nc, adb = adobe;
            Comp* cc = comp;
            uint32_t w = W;
            knst_image_loader::ParallelForRows(H, (size_t)W * 8, [cc, ncl, adb, rgbMode, ob, w](uint32_t r0, uint32_t r1) {
                for (uint32_t y = r0; y < r1; y++) {
                    const uint8_t* ch[4] = {nullptr, nullptr, nullptr, nullptr};
                    for (int i = 0; i < ncl; i++) ch[i] = cc[i].fp + (size_t)y * cc[i].fstride;
                    uint8_t* o = ob + (size_t)y * w * 4;
                    for (uint32_t x = 0; x < w; x++, o += 4) {
                        if (ncl == 1) { o[0] = o[1] = o[2] = ch[0][x]; o[3] = 255; }
                        else if (ncl == 3) {
                            if (rgbMode) { o[0] = ch[0][x]; o[1] = ch[1][x]; o[2] = ch[2][x]; }
                            else YccToRgb(ch[0][x], ch[1][x], ch[2][x], o);
                            o[3] = 255;
                        } else {
                            uint8_t r, g, b, k = ch[3][x];
                            if (adb == 2) { // YCCK
                                uint8_t t[3]; YccToRgb(ch[0][x], ch[1][x], ch[2][x], t);
                                r = Blinn(255 - t[0], k); g = Blinn(255 - t[1], k); b = Blinn(255 - t[2], k);
                            } else if (adb >= 0) { 
                                r = Blinn(ch[0][x], k); g = Blinn(ch[1][x], k); b = Blinn(ch[2][x], k);
                            } else {
                                r = Blinn(255 - ch[0][x], 255 - k); g = Blinn(255 - ch[1][x], 255 - k); b = Blinn(255 - ch[2][x], 255 - k);
                            }
                            o[0] = r; o[1] = g; o[2] = b; o[3] = 255;
                        }
                    }
                }
            });
            return true;
        }

        static inline uint8_t Blinn(int a, int b) { int t = a * b + 128; return (uint8_t)((t + (t >> 8)) >> 8); }
        static inline void YccToRgb(int y, int cb, int cr, uint8_t* o) {
            cb -= 128; cr -= 128;
            int r = y + ((91881 * cr + 32768) >> 16);
            int g = y + ((-22554 * cb - 46802 * cr + 32768) >> 16);
            int b = y + ((116130 * cb + 32768) >> 16);
            o[0] = clamp8(r); o[1] = clamp8(g); o[2] = clamp8(b);
        }
    };

    template<typename ByteStr>
    static bool decode_jpeg(const uint8_t* data, size_t size, uint32_t& W, uint32_t& H, ByteStr& out, int = 0) {
        JpegDecoder dec(data, size);
        if (!dec.Run(out)) return false;
        W = dec.W; H = dec.H;
        return true;
    }
};