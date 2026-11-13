// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


// =============================================================================
//  knst_json.hpp  --  KernelNucleusT JSON module
//
//  RFC 8259 compliant, exception-free, allocator-hookable, binary-safe JSON
//  value type with parser, serializer and a large toolbox around it.
//
//  Core
//    * knst_json value: 24 bytes, SSO strings (<= 14 bytes), insertion-ordered
//      objects with a seeded hash index, 64-bit integers kept exact
//      (int64 + uint64), double for everything else.
//    * noexcept + bool return. No exceptions are thrown by this header.
//    * Header only, C++17 or newer. Windows / Linux / Android / macOS / iOS.
//
//  Robustness (v2)
//    * Parser, serializer, destructor, deep copy and operator== are all
//      ITERATIVE (explicit stacks / zero-alloc frame reuse): arbitrarily deep
//      documents can neither overflow the stack nor crash.
//    * Hard limits: depth, input size, string length, container size.
//    * Strict UTF-8 validation, lone-surrogate handling, locale independent
//      number parsing/printing, shortest round-trip doubles.
//    * Allocation failure never throws: copies degrade to null, mutators
//      return false / nullptr, the parser reports out_of_memory.
//
//  Performance (v2)
//    * SSE2 / NEON / SWAR accelerated string scanning in parser and writer.
//    * Exact-size arrays/objects after parsing (shrink-to-fit on close).
//    * Parser writes strings straight into the final value (no temp copies).
//    * Streaming output (FILE*, callback) with 64 KiB chunking.
//
//  Features
//    * DOM parse, SAX parse (parse_sax), prefix parse (concatenated values),
//      NDJSON / JSON Lines (parse_lines, load_lines_*, dump_lines)
//    * Lenient mode: comments, trailing commas, NaN/Infinity, 'single quotes',
//      unquoted keys, lone surrogate replacement
//    * Dump: pretty / compact, sort_keys, ensure_ascii, escape_slash,
//      escape_html, RFC 8785 JCS canonical form, invalid UTF-8 policies
//    * JSON Pointer RFC 6901 (get / set / insert / remove / replace, URI
//      fragment form), JSON Patch RFC 6902 (apply atomic or in place, diff),
//      JSON Merge Patch RFC 7386 (apply + generate)
//    * JSONPath subset (RFC 9535 style): $ . .. [] * slices unions filters
//    * JSON Schema validation (draft-07 style subset, local $ref)
//    * flatten / unflatten, walk, compare (total order), hash, sort helpers
//    * Atomic file save (unique temp + fsync + rename + dir fsync)
//    * Optional std::string / std::string_view bridge, knst string bridges
//
//  Customisation macros (define BEFORE including this header):
//    KNST_JSON_MALLOC(n) / KNST_JSON_REALLOC(p,n) / KNST_JSON_FREE(p)
//    KNST_JSON_NO_CHARCONV         never use std::from_chars/to_chars for double
//    KNST_JSON_NO_KNST_STRINGS     disable knst_byte_string/knst_c16string bridges
//    KNST_JSON_HAS_BYTE_STRING     force-enable the knst_byte_string bridge
//    KNST_JSON_HAS_C16STRING       force-enable the knst_c16string bridge
//    KNST_JSON_ENABLE_STD_STRING   enable std::string / std::string_view bridge
//    KNST_JSON_NO_FILE_IO          remove all file functions (embedded targets)
//    KNST_JSON_NO_SIMD             disable SSE2/NEON paths
//    KNST_JSON_MAX_RECURSION       recursion cap of the (rare) recursive helpers
//                                  (compare, hash, merge_patch, walk, query,
//                                  schema, diff). Default 1000.
//    KNST_SMALL_SIZE_CLASS         inline instead of force-inline
//
//  Thread safety: a knst_json tree has no shared state; distinct trees can be
//  used from distinct threads freely. One tree must not be mutated concurrently.
// =============================================================================




#pragma once
#ifndef KNST_JSON_HPP
#define KNST_JSON_HPP

#include <algorithm>
#include <atomic>
#include <cctype>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

#if defined(KNST_JSON_ENABLE_STD_STRING)
#  include <string>
#  if defined(__has_include)
#    if __has_include(<string_view>)
#      include <string_view>
#    endif
#  endif
#endif

#if !defined(KNST_JSON_NO_CHARCONV) && defined(__has_include)
#  if __has_include(<charconv>)
#    include <charconv>
#    if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#      define KNST_JSON_HAS_FP_CHARCONV 1
#    endif
#  endif
#endif

#if !defined(KNST_JSON_NO_SIMD)
#  if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#    include <emmintrin.h>
#    define KNST_JSON_SSE2 1
#  elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__ARM_NEON) || defined(_M_ARM64))
#    include <arm_neon.h>
#    define KNST_JSON_NEON 1
#  endif
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
#endif

#if !defined(KNST_JSON_NO_FILE_IO)
#  if defined(_WIN32)
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    include <io.h>
#    include <windows.h>
#  else
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#    include <unistd.h>
#  endif
#endif

#if !defined(KNST_JSON_NO_KNST_STRINGS) && defined(__has_include)
#  if !defined(KNST_JSON_HAS_BYTE_STRING) && __has_include("knst_byte_string.hpp")
#    include "knst_byte_string.hpp"
#    define KNST_JSON_HAS_BYTE_STRING 1
#  endif
#  if !defined(KNST_JSON_HAS_C16STRING) && __has_include("knst_c16string.hpp")
#    include "knst_c16string.hpp"
#    define KNST_JSON_HAS_C16STRING 1
#  endif
#endif

// ----------------------------------------------------------------------------
//  Configuration macros
// ----------------------------------------------------------------------------
#ifndef KNST_JSON_MALLOC
#  define KNST_JSON_MALLOC(n) std::malloc(n)
#endif
#ifndef KNST_JSON_REALLOC
#  define KNST_JSON_REALLOC(p, n) std::realloc((p), (n))
#endif
#ifndef KNST_JSON_FREE
#  define KNST_JSON_FREE(p) std::free(p)
#endif
#ifndef KNST_JSON_MAX_RECURSION
#  define KNST_JSON_MAX_RECURSION 1000
#endif

#if defined(KNST_SMALL_SIZE_CLASS)
#  define KNST_JSON_FI inline
#elif defined(_MSC_VER) && !defined(__clang__)
#  define KNST_JSON_FI __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define KNST_JSON_FI inline __attribute__((always_inline))
#else
#  define KNST_JSON_FI inline
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define KNST_JSON_LIKELY(x) __builtin_expect(!!(x), 1)
#  define KNST_JSON_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define KNST_JSON_LIKELY(x) (x)
#  define KNST_JSON_UNLIKELY(x) (x)
#endif

#define KNST_JSON_VERSION_MAJOR 2
#define KNST_JSON_VERSION_MINOR 0
#define KNST_JSON_VERSION_PATCH 0

#define KNST_JSON_TRY(expr) \
    do { if (KNST_JSON_UNLIKELY(!(expr))) return false; } while (0)

class knst_json;
struct knst_json_member;

// ----------------------------------------------------------------------------
//  Public enums / option structs / error
// ----------------------------------------------------------------------------
enum class knst_json_type : uint8_t {
    null_value = 0,
    boolean,
    integer,           // int64_t
    unsigned_integer,  // uint64_t, ONLY used for values > INT64_MAX
    floating,          // double
    string,
    array,
    object
};

enum class knst_json_errc : uint8_t {
    none = 0,
    out_of_memory,
    empty_input,
    unexpected_end,
    unexpected_char,
    invalid_literal,
    invalid_number,
    number_out_of_range,
    invalid_escape,
    invalid_unicode_escape,
    lone_surrogate,
    invalid_utf8,
    control_char_in_string,
    max_depth_exceeded,
    input_too_large,
    string_too_long,
    container_too_large,
    duplicate_key,
    trailing_characters,
    non_finite_number,
    io_error,
    invalid_argument,
    // ---- v2 ----
    handler_aborted,       // SAX handler returned false
    pointer_syntax,        // malformed JSON Pointer / JSONPath
    path_not_found,        // pointer / path does not resolve
    patch_invalid,         // malformed JSON Patch document
    patch_test_failed,     // JSON Patch "test" operation failed
    schema_violation       // JSON Schema validation failed
};

struct knst_json_error {
    knst_json_errc code = knst_json_errc::none;
    size_t offset = 0;      // byte offset of the error (patch: operation index)
    uint32_t line = 0;      // 1-based
    uint32_t column = 0;    // 1-based (bytes)

    explicit operator bool() const noexcept { return code != knst_json_errc::none; }

    const char* message() const noexcept {
        switch (code) {
            case knst_json_errc::none: return "no error";
            case knst_json_errc::out_of_memory: return "out of memory";
            case knst_json_errc::empty_input: return "empty input";
            case knst_json_errc::unexpected_end: return "unexpected end of input";
            case knst_json_errc::unexpected_char: return "unexpected character";
            case knst_json_errc::invalid_literal: return "invalid literal";
            case knst_json_errc::invalid_number: return "invalid number";
            case knst_json_errc::number_out_of_range: return "number out of range";
            case knst_json_errc::invalid_escape: return "invalid escape sequence";
            case knst_json_errc::invalid_unicode_escape: return "invalid \\u escape";
            case knst_json_errc::lone_surrogate: return "unpaired UTF-16 surrogate";
            case knst_json_errc::invalid_utf8: return "invalid UTF-8";
            case knst_json_errc::control_char_in_string: return "unescaped control character in string";
            case knst_json_errc::max_depth_exceeded: return "maximum nesting depth exceeded";
            case knst_json_errc::input_too_large: return "input too large";
            case knst_json_errc::string_too_long: return "string too long";
            case knst_json_errc::container_too_large: return "container too large";
            case knst_json_errc::duplicate_key: return "duplicate object key";
            case knst_json_errc::trailing_characters: return "trailing characters after JSON value";
            case knst_json_errc::non_finite_number: return "NaN/Infinity cannot be represented";
            case knst_json_errc::io_error: return "I/O error";
            case knst_json_errc::invalid_argument: return "invalid argument";
            case knst_json_errc::handler_aborted: return "aborted by handler";
            case knst_json_errc::pointer_syntax: return "invalid pointer / path syntax";
            case knst_json_errc::path_not_found: return "path not found";
            case knst_json_errc::patch_invalid: return "invalid JSON Patch";
            case knst_json_errc::patch_test_failed: return "JSON Patch test failed";
            case knst_json_errc::schema_violation: return "schema violation";
        }
        return "unknown error";
    }
};

enum class knst_json_duplicate_keys : uint8_t { last_wins, first_wins, error };

struct knst_json_parse_options {
    uint32_t max_depth = 512;                    // nesting limit (DoS protection; any value is stack-safe)
    size_t   max_input_size = 0xFFFFFFF0u;       // bytes
    uint32_t max_string_length = 0xFFFFFFF0u;    // bytes per string / key
    uint32_t max_container_size = 0xFFFFFFF0u;   // elements per array / object
    knst_json_duplicate_keys duplicate_keys = knst_json_duplicate_keys::last_wins;
    bool validate_utf8 = true;                   // reject invalid UTF-8 in strings
    bool allow_bom = true;                       // skip a leading UTF-8 BOM
    bool allow_comments = false;                 // // and /* */
    bool allow_trailing_commas = false;
    bool allow_nan_inf = false;                  // NaN, Infinity, -Infinity (and overflowing doubles)
    bool allow_single_quotes = false;            // 'strings' and 'keys'
    bool allow_unquoted_keys = false;            // {key: 1}  (identifier keys)
    bool replace_lone_surrogates = false;        // \uD800 alone -> U+FFFD instead of an error

    // Everything lenient at once (JSON5-ish, config files).
    static knst_json_parse_options lenient() noexcept {
        knst_json_parse_options o;
        o.allow_comments = o.allow_trailing_commas = o.allow_nan_inf = true;
        o.allow_single_quotes = o.allow_unquoted_keys = o.replace_lone_surrogates = true;
        return o;
    }
};

enum class knst_json_invalid_utf8 : uint8_t { error, replace, ignore };
enum class knst_json_non_finite : uint8_t { error, null_value, literal };

struct knst_json_dump_options {
    uint32_t indent = 0;               // 0 = compact
    char     indent_char = ' ';        // ' ' or '\t'
    bool     sort_keys = false;
    bool     ensure_ascii = false;     // escape all non-ASCII as \uXXXX
    bool     trailing_newline = false;
    uint32_t max_depth = 512;
    knst_json_invalid_utf8 invalid_utf8 = knst_json_invalid_utf8::error;
    knst_json_non_finite   non_finite = knst_json_non_finite::error;
    // ---- v2 ----
    bool     escape_slash = false;     // '/' -> "\/"
    bool     escape_html = false;      // < > & ' and U+2028/2029 -> \uXXXX  (safe inside <script>)
    bool     canonical = false;        // RFC 8785 (JCS): overrides indent/sort/ascii/number format

    static knst_json_dump_options pretty(uint32_t n = 2) noexcept { knst_json_dump_options o; o.indent = n; return o; }
    static knst_json_dump_options canonical_form() noexcept { knst_json_dump_options o; o.canonical = true; return o; }
};

// ----------------------------------------------------------------------------
//  Detail
// ----------------------------------------------------------------------------
namespace knst_json_detail {

class writer;
struct dom_sink;

// ---- bit tricks ---------------------------------------------------------------
KNST_JSON_FI unsigned ctz32(uint32_t x) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long r; _BitScanForward(&r, x); return static_cast<unsigned>(r);
#else
    return static_cast<unsigned>(__builtin_ctz(x));
#endif
}
KNST_JSON_FI unsigned ctz64(uint64_t x) noexcept {
    const uint32_t lo = static_cast<uint32_t>(x);
    return lo ? ctz32(lo) : 32u + ctz32(static_cast<uint32_t>(x >> 32));
}

// ---- growable raw buffer ----------------------------------------------------
template <class T>
class basic_buffer {
public:
    basic_buffer() noexcept = default;
    basic_buffer(const basic_buffer&) = delete;
    basic_buffer& operator=(const basic_buffer&) = delete;
    basic_buffer(basic_buffer&& o) noexcept : p_(o.p_), n_(o.n_), c_(o.c_) { o.p_ = nullptr; o.n_ = o.c_ = 0; }
    basic_buffer& operator=(basic_buffer&& o) noexcept {
        if (this != &o) { release_memory(); p_ = o.p_; n_ = o.n_; c_ = o.c_; o.p_ = nullptr; o.n_ = o.c_ = 0; }
        return *this;
    }
    ~basic_buffer() { release_memory(); }

    const T* data() const noexcept { return p_; }
    T* data() noexcept { return p_; }
    size_t size() const noexcept { return n_; }
    size_t capacity() const noexcept { return c_; }
    bool empty() const noexcept { return n_ == 0; }
    void clear() noexcept { n_ = 0; }
    const T* begin() const noexcept { return p_; }
    const T* end() const noexcept { return p_ + n_; }
    T& back() noexcept { return p_[n_ - 1]; }
    // Marks `n` elements as used (n <= capacity()); for data written in place.
    void set_size(size_t n) noexcept { n_ = n <= c_ ? n : c_; }

    bool reserve(size_t want) noexcept {
        if (want <= c_) return true;
        if (want > (std::numeric_limits<size_t>::max)() / sizeof(T) - 1) return false;
        size_t nc = c_ ? c_ : 64;
        while (nc < want) {
            if (nc > (std::numeric_limits<size_t>::max)() / 2 / sizeof(T)) { nc = want; break; }
            nc *= 2;
        }
        void* np = KNST_JSON_REALLOC(p_, nc * sizeof(T));
        if (!np) return false;
        p_ = static_cast<T*>(np);
        c_ = nc;
        return true;
    }
    KNST_JSON_FI bool push(T v) noexcept {
        if (KNST_JSON_UNLIKELY(n_ == c_)) KNST_JSON_TRY(reserve(n_ + 1));
        p_[n_++] = v;
        return true;
    }
    KNST_JSON_FI bool append(const T* s, size_t k) noexcept {
        if (k == 0) return true;
        if (KNST_JSON_UNLIKELY(k > c_ - n_)) {
            if (k > (std::numeric_limits<size_t>::max)() - n_) return false;
            KNST_JSON_TRY(reserve(n_ + k));
        }
        std::memcpy(p_ + n_, s, k * sizeof(T));
        n_ += k;
        return true;
    }
    // Guarantees a readable NUL terminator after size(); never changes size().
    const T* c_str() noexcept {
        static const T empty_z = T();
        if (!reserve(n_ + 1)) return &empty_z;
        p_[n_] = T();
        return p_;
    }
    // Gives up ownership. Caller frees with KNST_JSON_FREE.
    T* release(size_t* out_size = nullptr) noexcept {
        T* r = p_;
        if (out_size) *out_size = n_;
        p_ = nullptr; n_ = c_ = 0;
        return r;
    }
private:
    void release_memory() noexcept { if (p_) KNST_JSON_FREE(p_); p_ = nullptr; n_ = c_ = 0; }
    T* p_ = nullptr;
    size_t n_ = 0;
    size_t c_ = 0;
};

// ---- compact string: 16 bytes, SSO <= 14 bytes, binary-safe ------------------
// Trivially copyable on purpose: ownership is handled manually (init/destroy)
class str {
public:
    static constexpr uint32_t small_max = 14;
    static constexpr uint32_t max_len = 0xFFFFFFF0u;

    KNST_JSON_FI void init() noexcept { std::memset(&u_, 0, sizeof(u_)); }
    KNST_JSON_FI bool is_heap() const noexcept { return u_.h.tag == 0x80; }
    KNST_JSON_FI uint32_t size() const noexcept { return is_heap() ? u_.h.len : static_cast<uint32_t>(u_.h.tag); }
    KNST_JSON_FI const char* data() const noexcept { return is_heap() ? u_.h.p : u_.sb; }
    KNST_JSON_FI char* data() noexcept { return is_heap() ? u_.h.p : u_.sb; }
    KNST_JSON_FI uint32_t capacity() const noexcept { return is_heap() ? heap_cap() : small_max; }

    KNST_JSON_FI void destroy() noexcept {
        if (is_heap()) KNST_JSON_FREE(u_.h.p - 8);
        init();
    }
    KNST_JSON_FI void set_size(uint32_t n) noexcept {
        if (is_heap()) { u_.h.len = n; u_.h.p[n] = '\0'; }
        else { u_.h.tag = static_cast<unsigned char>(n); u_.sb[n] = '\0'; }
    }
    bool reserve(size_t n) noexcept {
        if (n > max_len) return false;
        const uint32_t want = static_cast<uint32_t>(n);
        if (is_heap()) {
            if (want <= heap_cap()) return true;
            char* blk = static_cast<char*>(KNST_JSON_REALLOC(u_.h.p - 8, static_cast<size_t>(want) + 9));
            if (!blk) return false;
            std::memcpy(blk, &want, 4);
            u_.h.p = blk + 8;
            return true;
        }
        if (want <= small_max) return true;
        const uint32_t len = u_.h.tag;
        char* blk = static_cast<char*>(KNST_JSON_MALLOC(static_cast<size_t>(want) + 9));
        if (!blk) return false;
        std::memcpy(blk, &want, 4);
        std::memset(blk + 4, 0, 4);
        std::memcpy(blk + 8, u_.sb, len + 1u);   // copy BEFORE the union is overwritten
        u_.h.p = blk + 8;
        u_.h.len = len;
        u_.h.r[0] = u_.h.r[1] = u_.h.r[2] = 0;
        u_.h.tag = 0x80;
        return true;
    }
    // Safe even when `s` points into this very string.
    bool append(const char* s, size_t n) noexcept {
        if (n == 0) return true;
        const uint32_t cur = size();
        if (n > static_cast<size_t>(max_len) - cur) return false;
        const size_t need = cur + n;
        const uint32_t cap = capacity();
        if (need > cap) {
            size_t nc = need;
            if (cur != 0) {
                const size_t dbl = static_cast<size_t>(cap) * 2;
                if (dbl > nc) nc = dbl;
                if (nc > max_len) nc = max_len;
            }
            const char* d = data();
            const bool alias = s >= d && s <= d + cur;
            const size_t off = alias ? static_cast<size_t>(s - d) : 0;
            KNST_JSON_TRY(reserve(nc));
            if (alias) s = data() + off;
        }
        std::memmove(data() + cur, s, n);
        set_size(static_cast<uint32_t>(need));
        return true;
    }
    bool assign(const char* s, size_t n) noexcept {
        if (n && s >= data() && s <= data() + size()) {   // aliasing: go through a temporary
            str t; t.init();
            if (!t.append(s, n)) { t.destroy(); return false; }
            destroy(); *this = t; return true;
        }
        set_size(0); return append(s, n);
    }
    KNST_JSON_FI bool equals(const char* s, size_t n) const noexcept {
        return size() == n && (n == 0 || std::memcmp(data(), s, n) == 0);
    }
    // Frees surplus heap capacity (no-op for small strings).
    void shrink() noexcept {
        if (!is_heap()) return;
        const uint32_t len = u_.h.len;
        if (len <= small_max) {
            char tmp[16]; std::memcpy(tmp, u_.h.p, len + 1u);
            KNST_JSON_FREE(u_.h.p - 8);
            init();
            std::memcpy(u_.sb, tmp, len + 1u);
            u_.h.tag = static_cast<unsigned char>(len);
            return;
        }
        if (heap_cap() == len) return;
        char* blk = static_cast<char*>(KNST_JSON_REALLOC(u_.h.p - 8, static_cast<size_t>(len) + 9));
        if (!blk) return;
        std::memcpy(blk, &len, 4);
        u_.h.p = blk + 8;
    }
private:
    uint32_t heap_cap() const noexcept { uint32_t c; std::memcpy(&c, u_.h.p - 8, 4); return c; }
    struct H { char* p; uint32_t len; unsigned char r[3]; unsigned char tag; };
    union U { H h; char sb[16]; } u_;
};
static_assert(sizeof(str) == 16, "knst_json_detail::str must be 16 bytes");

struct str_guard {
    str s;
    str_guard() noexcept { s.init(); }
    ~str_guard() { s.destroy(); }
    str_guard(const str_guard&) = delete;
    str_guard& operator=(const str_guard&) = delete;
};

// ---- hashing -----------------------------------------------------------------
KNST_JSON_FI uint64_t mix64(uint64_t x) noexcept {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
// Per-process seed (ASLR derived) so crafted-collision payloads cannot be precomputed.
inline uint64_t hash_seed() noexcept {
    static const uint64_t s = mix64(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&s)) ^ 0x9E3779B97F4A7C15ULL);
    return s;
}
inline uint32_t hash_key(const char* p, size_t n) noexcept {
    uint64_t h = hash_seed() ^ (static_cast<uint64_t>(n) * 0x9E3779B97F4A7C15ULL);
    while (n >= 8) {
        uint64_t w; std::memcpy(&w, p, 8);
        h = (h ^ w) * 0xff51afd7ed558ccdULL;
        h ^= h >> 32;
        p += 8; n -= 8;
    }
    uint64_t w = 0;
    if (n) std::memcpy(&w, p, n);
    h = (h ^ w) * 0xc4ceb9fe1a85ec53ULL;
    h = mix64(h);
    return static_cast<uint32_t>(h ^ (h >> 32));
}
// Deterministic (unseeded) 64-bit hash for knst_json::hash().
inline uint64_t hash_bytes64(const char* p, size_t n) noexcept {
    uint64_t h = 0xcbf29ce484222325ULL ^ (n * 0x9E3779B97F4A7C15ULL);
    while (n >= 8) { uint64_t w; std::memcpy(&w, p, 8); h = mix64(h ^ w); p += 8; n -= 8; }
    uint64_t w = 0;
    if (n) std::memcpy(&w, p, n);
    return mix64(h ^ w ^ 0xA5A5A5A5A5A5A5A5ULL);
}

struct obj_index { uint32_t idx1; uint32_t hash; };   // idx1 == 0 -> empty
struct obj {
    knst_json_member* e;
    uint32_t n, c;
    obj_index* ix;
    uint32_t ixcap;
};

// Frame stored in a dead 24-byte value slot by the allocation-free iterative destructor.
struct dframe { void* prev; void* ref; uint32_t i; uint32_t kind; };

// ---- UTF helpers -------------------------------------------------------------
// Returns the byte length (1..4) of the well-formed sequence at p, or 0 if invalid.
KNST_JSON_FI size_t utf8_len(const unsigned char* p, const unsigned char* e) noexcept {
    const unsigned char c = *p;
    if (c < 0x80) return 1;
    if (c < 0xC2) return 0;
    if (c < 0xE0) return (e - p >= 2 && (p[1] & 0xC0) == 0x80) ? 2 : 0;
    if (c < 0xF0) {
        if (e - p < 3 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 0;
        if (c == 0xE0 && p[1] < 0xA0) return 0;
        if (c == 0xED && p[1] > 0x9F) return 0;
        return 3;
    }
    if (c <= 0xF4) {
        if (e - p < 4 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return 0;
        if (c == 0xF0 && p[1] < 0x90) return 0;
        if (c == 0xF4 && p[1] > 0x8F) return 0;
        return 4;
    }
    return 0;
}
inline uint32_t utf8_decode(const unsigned char* p, size_t l) noexcept {
    switch (l) {
        case 1: return p[0];
        case 2: return (static_cast<uint32_t>(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        case 3: return (static_cast<uint32_t>(p[0] & 0x0F) << 12) | (static_cast<uint32_t>(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        default: return (static_cast<uint32_t>(p[0] & 0x07) << 18) | (static_cast<uint32_t>(p[1] & 0x3F) << 12) |
                        (static_cast<uint32_t>(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }
}
inline size_t utf8_encode(uint32_t cp, char* o) noexcept {
    if (cp < 0x80) { o[0] = static_cast<char>(cp); return 1; }
    if (cp < 0x800) { o[0] = static_cast<char>(0xC0 | (cp >> 6)); o[1] = static_cast<char>(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        o[0] = static_cast<char>(0xE0 | (cp >> 12)); o[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o[2] = static_cast<char>(0x80 | (cp & 0x3F)); return 3;
    }
    o[0] = static_cast<char>(0xF0 | (cp >> 18)); o[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    o[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o[3] = static_cast<char>(0x80 | (cp & 0x3F)); return 4;
}
// UTF-16 -> UTF-8 (unpaired surrogates become U+FFFD)
inline bool utf16_to_utf8(const char16_t* s, size_t n, basic_buffer<char>& out) noexcept {
    KNST_JSON_TRY(out.reserve(out.size() + n + 1));
    char tmp[4];
    for (size_t i = 0; i < n; ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00); ++i;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        KNST_JSON_TRY(out.append(tmp, utf8_encode(cp, tmp)));
    }
    return true;
}
// UTF-8 -> UTF-16 (invalid sequences become U+FFFD)
template <class C16>
inline bool utf8_to_utf16(const char* s, size_t n, basic_buffer<C16>& out) noexcept {
    KNST_JSON_TRY(out.reserve(out.size() + n + 1));
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    const unsigned char* e = p + n;
    while (p < e) {
        size_t l = utf8_len(p, e);
        uint32_t cp = 0xFFFD;
        if (l) cp = utf8_decode(p, l); else l = 1;
        p += l;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            KNST_JSON_TRY(out.push(static_cast<C16>(0xD800 + (cp >> 10))));
            KNST_JSON_TRY(out.push(static_cast<C16>(0xDC00 + (cp & 0x3FF))));
        } else KNST_JSON_TRY(out.push(static_cast<C16>(cp)));
    }
    return true;
}
// Compares two UTF-8 strings by UTF-16 code units (RFC 8785 key order).
inline int cmp_utf16_order(const char* a, size_t na, const char* b, size_t nb) noexcept {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* pe = p + na;
    const unsigned char* q = reinterpret_cast<const unsigned char*>(b);
    const unsigned char* qe = q + nb;
    while (p < pe && q < qe) {
        size_t lp = utf8_len(p, pe), lq = utf8_len(q, qe);
        const uint32_t cp = lp ? utf8_decode(p, lp) : *p;
        const uint32_t cq = lq ? utf8_decode(q, lq) : *q;
        if (!lp) lp = 1;
        if (!lq) lq = 1;
        if (cp != cq) {
            const uint32_t kp = cp >= 0x10000 ? 0xD800 + ((cp - 0x10000) >> 10) : cp;
            const uint32_t kq = cq >= 0x10000 ? 0xD800 + ((cq - 0x10000) >> 10) : cq;
            if (kp != kq) return kp < kq ? -1 : 1;
            return cp < cq ? -1 : 1;
        }
        p += lp; q += lq;
    }
    if (p < pe) return 1;
    if (q < qe) return -1;
    return 0;
}

// ---- fast scanning of "plain" string bytes ----------------------------------
// Returns the first byte that is '"', '\\', < 0x20 or >= 0x80 (or `e`).
KNST_JSON_FI const unsigned char* scan_plain(const unsigned char* p, const unsigned char* e) noexcept {
#if defined(KNST_JSON_SSE2)
    const __m128i vq = _mm_set1_epi8('"'), vb = _mm_set1_epi8('\\'), vl = _mm_set1_epi8(0x20);
    while (e - p >= 16) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        // signed compare: bytes >= 0x80 are negative, hence "< 0x20" as well
        const __m128i m = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, vq), _mm_cmpeq_epi8(v, vb)), _mm_cmplt_epi8(v, vl));
        const unsigned mm = static_cast<unsigned>(_mm_movemask_epi8(m));
        if (mm) return p + ctz32(mm);
        p += 16;
    }
#elif defined(KNST_JSON_NEON)
    const uint8x16_t vq = vdupq_n_u8('"'), vb = vdupq_n_u8('\\'), vl = vdupq_n_u8(0x20), vh = vdupq_n_u8(0x80);
    while (e - p >= 16) {
        const uint8x16_t v = vld1q_u8(p);
        const uint8x16_t m = vorrq_u8(vorrq_u8(vceqq_u8(v, vq), vceqq_u8(v, vb)), vorrq_u8(vcltq_u8(v, vl), vcgeq_u8(v, vh)));
        if (vmaxvq_u8(m)) break;   // the scalar tail below locates the byte
        p += 16;
    }
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || defined(_MSC_VER)
    constexpr uint64_t ones = 0x0101010101010101ULL, highs = 0x8080808080808080ULL;
    while (e - p >= 8) {
        uint64_t x; std::memcpy(&x, p, 8);
        const uint64_t q = x ^ (ones * '"');
        const uint64_t b = x ^ (ones * '\\');
        const uint64_t m = (((x - ones * 0x20) & ~x) | ((q - ones) & ~q) | ((b - ones) & ~b) | x) & highs;
        if (m) return p + (ctz64(m) >> 3);
        p += 8;
    }
#endif
    while (p < e && *p >= 0x20 && *p != '"' && *p != '\\' && *p < 0x80) ++p;
    return p;
}
// Same for single-quoted strings (scalar; rare).
inline const unsigned char* scan_plain_sq(const unsigned char* p, const unsigned char* e) noexcept {
    while (p < e && *p >= 0x20 && *p != '\'' && *p != '\\' && *p < 0x80) ++p;
    return p;
}

// ---- number <-> text -----------------------------------------------------------
// Locale independent strtod (the C locale of a Turkish/German system uses ',').
inline bool strtod_c(const char* s, size_t n, double& out) noexcept {
    char stackbuf[128];
    char* buf = stackbuf;
    char* heap = nullptr;
    if (n + 1 > sizeof(stackbuf)) {
        heap = static_cast<char*>(KNST_JSON_MALLOC(n + 1));
        if (!heap) return false;
        buf = heap;
    }
    std::memcpy(buf, s, n);
    buf[n] = '\0';
    const std::lconv* lc = std::localeconv();
    if (lc && lc->decimal_point && lc->decimal_point[0] && lc->decimal_point[0] != '.' && lc->decimal_point[1] == '\0') {
        for (size_t i = 0; i < n; ++i) if (buf[i] == '.') buf[i] = lc->decimal_point[0];
    }
    char* end = nullptr;
    out = std::strtod(buf, &end);
    const bool ok = (end == buf + n);
    if (heap) KNST_JSON_FREE(heap);
    return ok;
}
inline bool parse_double_text(const char* s, size_t n, double& out) noexcept {
#if defined(KNST_JSON_HAS_FP_CHARCONV)
    auto r = std::from_chars(s, s + n, out);
    if (r.ec == std::errc() && r.ptr == s + n) return true;
    if (r.ec == std::errc::result_out_of_range) {
        // from_chars leaves `out` untouched on range errors; derive inf / 0 ourselves
        return strtod_c(s, n, out);
    }
    return false;
#else
    return strtod_c(s, n, out);
#endif
}
// Writes the shortest round-trip representation. buf must hold >= 48 bytes. Value must be finite.
inline size_t format_double(double d, char* buf) noexcept {
    size_t len = 0;
#if defined(KNST_JSON_HAS_FP_CHARCONV)
    auto r = std::to_chars(buf, buf + 39, d);
    len = static_cast<size_t>(r.ptr - buf);
#else
    for (int prec = 15; prec <= 17; ++prec) {
        int k = std::snprintf(buf, 40, "%.*g", prec, d);
        if (k <= 0 || k >= 40) { k = 0; }
        len = static_cast<size_t>(k);
        const std::lconv* lc = std::localeconv();
        if (lc && lc->decimal_point && lc->decimal_point[0] != '.' && lc->decimal_point[1] == '\0')
            for (size_t i = 0; i < len; ++i) if (buf[i] == lc->decimal_point[0]) buf[i] = '.';
        double back = 0;
        if (strtod_c(buf, len, back) && back == d) break;
    }
#endif
    bool looks_float = false;
    for (size_t i = 0; i < len; ++i) if (buf[i] == '.' || buf[i] == 'e' || buf[i] == 'E') { looks_float = true; break; }
    if (!looks_float) { buf[len++] = '.'; buf[len++] = '0'; }
    return len;
}
// Shortest digits of a positive finite double: value = d.ddd * 10^e10 (k digits, no trailing zeros).
inline void sci_digits(double d, char* dig, int& k, int& e10) noexcept {
    char t[64]; size_t len = 0;
#if defined(KNST_JSON_HAS_FP_CHARCONV)
    auto r = std::to_chars(t, t + 63, d, std::chars_format::scientific);
    len = static_cast<size_t>(r.ptr - t);
#else
    for (int prec = 0; prec <= 16; ++prec) {
        const int n = std::snprintf(t, sizeof(t), "%.*e", prec, d);
        len = n > 0 ? static_cast<size_t>(n) : 0;
        const std::lconv* lc = std::localeconv();
        if (lc && lc->decimal_point && lc->decimal_point[0] != '.' && lc->decimal_point[1] == '\0')
            for (size_t i = 0; i < len; ++i) if (t[i] == lc->decimal_point[0]) t[i] = '.';
        double back = 0;
        if (strtod_c(t, len, back) && back == d) break;
    }
#endif
    k = 0; e10 = 0;
    size_t i = 0;
    for (; i < len && t[i] != 'e' && t[i] != 'E'; ++i) if (t[i] >= '0' && t[i] <= '9') dig[k++] = t[i];
    if (i < len) {
        ++i; bool neg = false;
        if (i < len && t[i] == '+') ++i; else if (i < len && t[i] == '-') { neg = true; ++i; }
        int ev = 0;
        for (; i < len; ++i) ev = ev * 10 + (t[i] - '0');
        e10 = neg ? -ev : ev;
    }
    while (k > 1 && dig[k - 1] == '0') --k;
}
// ECMAScript Number::toString (RFC 8785 section 3.2.2.3). buf >= 48 bytes. Finite only.
inline size_t format_double_jcs(double d, char* buf) noexcept {
    if (d == 0) { buf[0] = '0'; return 1; }
    char* o = buf;
    if (d < 0) { *o++ = '-'; d = -d; }
    char dig[40]; int k, e10;
    sci_digits(d, dig, k, e10);
    const int n = e10 + 1;
    if (k <= n && n <= 21) {
        std::memcpy(o, dig, static_cast<size_t>(k)); o += k;
        for (int i = k; i < n; ++i) *o++ = '0';
    } else if (0 < n && n <= 21) {
        std::memcpy(o, dig, static_cast<size_t>(n)); o += n;
        *o++ = '.';
        std::memcpy(o, dig + n, static_cast<size_t>(k - n)); o += k - n;
    } else if (-6 < n && n <= 0) {
        *o++ = '0'; *o++ = '.';
        for (int i = 0; i < -n; ++i) *o++ = '0';
        std::memcpy(o, dig, static_cast<size_t>(k)); o += k;
    } else {
        *o++ = dig[0];
        if (k > 1) { *o++ = '.'; std::memcpy(o, dig + 1, static_cast<size_t>(k - 1)); o += k - 1; }
        *o++ = 'e';
        int ex = n - 1;
        *o++ = ex < 0 ? '-' : '+';
        if (ex < 0) ex = -ex;
        char t[8]; int tl = 0;
        do { t[tl++] = static_cast<char>('0' + ex % 10); ex /= 10; } while (ex);
        while (tl) *o++ = t[--tl];
    }
    return static_cast<size_t>(o - buf);
}
inline size_t format_i64(int64_t v, char* buf) noexcept {
    char tmp[24]; size_t t = 0;
    uint64_t u = v < 0 ? (~static_cast<uint64_t>(v) + 1u) : static_cast<uint64_t>(v);
    do { tmp[t++] = static_cast<char>('0' + u % 10); u /= 10; } while (u);
    size_t n = 0;
    if (v < 0) buf[n++] = '-';
    while (t) buf[n++] = tmp[--t];
    return n;
}
inline size_t format_u64(uint64_t u, char* buf) noexcept {
    char tmp[24]; size_t t = 0;
    do { tmp[t++] = static_cast<char>('0' + u % 10); u /= 10; } while (u);
    size_t n = 0;
    while (t) buf[n++] = tmp[--t];
    return n;
}

// ---- JSON pointer helpers ---------------------------------------------------------
// Appends "/" + escaped token to `b`.
inline bool pointer_escape_append(basic_buffer<char>& b, const char* s, size_t n) noexcept {
    KNST_JSON_TRY(b.push('/'));
    size_t run = 0;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (c == '~' || c == '/') {
            KNST_JSON_TRY(b.append(s + run, i - run));
            KNST_JSON_TRY(b.append(c == '~' ? "~0" : "~1", 2));
            run = i + 1;
        }
    }
    return b.append(s + run, n - run);
}
// Unescaped pointer token
struct token_buf {
    char sb[64];
    char* p = sb;
    size_t n = 0;
    bool heap = false;
    token_buf() noexcept = default;
    token_buf(const token_buf&) = delete;
    token_buf& operator=(const token_buf&) = delete;
    ~token_buf() { if (heap) KNST_JSON_FREE(p); }
    bool set(const char* s, size_t len) noexcept {
        if (len > sizeof(sb)) {
            p = static_cast<char*>(KNST_JSON_MALLOC(len));
            if (!p) { p = sb; return false; }
            heap = true;
        }
        size_t o = 0;
        for (size_t i = 0; i < len; ++i) {
            char c = s[i];
            if (c == '~') {
                if (i + 1 >= len) return false;
                const char d = s[++i];
                if (d == '0') c = '~'; else if (d == '1') c = '/'; else return false;
            }
            p[o++] = c;
        }
        n = o;
        return true;
    }
    bool is_index(size_t& idx) const noexcept {
        if (n == 0 || n > 18) return false;
        if (n > 1 && p[0] == '0') return false;
        size_t v = 0;
        for (size_t i = 0; i < n; ++i) { if (p[i] < '0' || p[i] > '9') return false; v = v * 10 + static_cast<size_t>(p[i] - '0'); }
        idx = v;
        return true;
    }
    bool is_dash() const noexcept { return n == 1 && p[0] == '-'; }
};

// ---- native path helpers ----------------------------------------------------------
#if defined(_WIN32)
using native_char = wchar_t;
#else
using native_char = char;
#endif
using native_path = basic_buffer<native_char>;

inline bool native_from_utf8(const char* s, size_t n, native_path& out) noexcept {
#if defined(_WIN32)
    KNST_JSON_TRY(utf8_to_utf16<wchar_t>(s, n, out));
#else
    KNST_JSON_TRY(out.append(s, n));
#endif
    return out.reserve(out.size() + 1);
}
inline bool native_from_u16(const char16_t* s, size_t n, native_path& out) noexcept {
#if defined(_WIN32)
    for (size_t i = 0; i < n; ++i) KNST_JSON_TRY(out.push(static_cast<wchar_t>(s[i])));
#else
    KNST_JSON_TRY(utf16_to_utf8(s, n, out));
#endif
    return out.reserve(out.size() + 1);
}

} // namespace knst_json_detail

// ----------------------------------------------------------------------------
//  knst_json_buffer : owning byte buffer that dump() writes into
// ----------------------------------------------------------------------------
using knst_json_buffer = knst_json_detail::basic_buffer<char>;
// List of non-owning node pointers (results of select()).
using knst_json_ptr_list = knst_json_detail::basic_buffer<const knst_json*>;

// Result of schema validation
struct knst_json_validation {
    bool valid = true;
    const char* keyword = "";     // keyword that failed (static string)
    knst_json_buffer path;        // JSON Pointer of the offending instance location
};

// SAX handler base: derive and shadow only what you need. Return false to abort.
struct knst_json_sax_base {
    bool null_value() { return true; }
    bool boolean(bool) { return true; }
    bool integer(int64_t) { return true; }
    bool uinteger(uint64_t) { return true; }
    bool number_float(double) { return true; }
    bool string(const char*, size_t) { return true; }
    bool key(const char*, size_t) { return true; }
    bool start_object() { return true; }
    bool end_object() { return true; }
    bool start_array() { return true; }
    bool end_array() { return true; }
};

// ----------------------------------------------------------------------------
//  knst_json
// ----------------------------------------------------------------------------
class knst_json {
public:
    using type = knst_json_type;
    using member = knst_json_member;

    // ---- construction / destruction -------------------------------------------
    knst_json() noexcept : type_(type::null_value) { v_.u = 0; }
    knst_json(std::nullptr_t) noexcept : type_(type::null_value) { v_.u = 0; }
    knst_json(bool b) noexcept : type_(type::boolean) { v_.u = 0; v_.b = b; }
    template <class T, std::enable_if_t<std::is_integral<T>::value && !std::is_same<T, bool>::value, int> = 0>
    knst_json(T v) noexcept : type_(type::null_value) { v_.u = 0; set_integer(v); }
    template <class T, std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
    knst_json(T v) noexcept : type_(type::floating) { v_.u = 0; v_.d = static_cast<double>(v); }
    // UTF-8 / binary-safe string. On allocation failure the value stays null.
    knst_json(const char* s) noexcept : type_(type::null_value) { v_.u = 0; if (s) set_string(s, std::strlen(s)); }
    knst_json(const char* s, size_t n) noexcept : type_(type::null_value) { v_.u = 0; set_string(s, n); }

    knst_json(knst_json&& o) noexcept { take(o); }
    knst_json& operator=(knst_json&& o) noexcept { if (this != &o) { destroy(); take(o); } return *this; }
    // Deep copy. On allocation failure the copy becomes null (use copy_from() to detect it).
    knst_json(const knst_json& o) noexcept : type_(type::null_value) { v_.u = 0; if (!copy_from(o)) reset(); }
    knst_json& operator=(const knst_json& o) noexcept { if (this != &o) { knst_json t; if (t.copy_from(o)) { destroy(); take(t); } else reset(); } return *this; }
    ~knst_json() { destroy(); }

    static knst_json make_array() noexcept { knst_json j; j.type_ = type::array; j.v_.a.p = nullptr; j.v_.a.n = j.v_.a.c = 0; return j; }
    static knst_json make_object() noexcept { knst_json j; if (!j.obj_make()) j.type_ = type::null_value; return j; }
    static knst_json make_string(const char* s, size_t n) noexcept { return knst_json(s, n); }
    static knst_json make_array(std::initializer_list<knst_json> items) noexcept {
        knst_json j = make_array();
        if (!j.reserve(items.size())) return knst_json();
        for (const knst_json& it : items) if (!j.push_back(it)) return knst_json();
        return j;
    }
    static knst_json make_object(std::initializer_list<std::pair<const char*, knst_json>> items) noexcept {
        knst_json j = make_object();
        if (j.type_ != type::object) return knst_json();
        for (const auto& it : items) if (!j.set(it.first, it.second)) return knst_json();
        return j;
    }

    // ---- type queries -----------------------------------------------------------
    KNST_JSON_FI type get_type() const noexcept { return type_; }
    KNST_JSON_FI bool is_null() const noexcept { return type_ == type::null_value; }
    KNST_JSON_FI bool is_bool() const noexcept { return type_ == type::boolean; }
    KNST_JSON_FI bool is_integer() const noexcept { return type_ == type::integer || type_ == type::unsigned_integer; }
    KNST_JSON_FI bool is_float() const noexcept { return type_ == type::floating; }
    KNST_JSON_FI bool is_number() const noexcept { return is_integer() || is_float(); }
    KNST_JSON_FI bool is_string() const noexcept { return type_ == type::string; }
    KNST_JSON_FI bool is_array() const noexcept { return type_ == type::array; }
    KNST_JSON_FI bool is_object() const noexcept { return type_ == type::object; }
    KNST_JSON_FI bool is_container() const noexcept { return is_array() || is_object(); }
    KNST_JSON_FI bool is_scalar() const noexcept { return !is_container(); }
    // true for integers AND for floats with an exact integral value
    bool is_integral_value() const noexcept {
        return is_integer() || (type_ == type::floating && std::isfinite(v_.d) && v_.d == std::floor(v_.d));
    }
    const char* type_name() const noexcept {
        switch (type_) {
            case type::null_value: return "null"; case type::boolean: return "boolean";
            case type::integer: case type::unsigned_integer: return "integer";
            case type::floating: return "float"; case type::string: return "string";
            case type::array: return "array"; case type::object: return "object";
        }
        return "unknown";
    }

    // ---- scalar access ----------------------------------------------------------
    bool get(bool& out) const noexcept { if (type_ != type::boolean) return false; out = v_.b; return true; }
    bool get(double& out) const noexcept {
        switch (type_) {
            case type::integer: out = static_cast<double>(v_.i); return true;
            case type::unsigned_integer: out = static_cast<double>(v_.u); return true;
            case type::floating: out = v_.d; return true;
            default: return false;
        }
    }
    bool get(float& out) const noexcept { double d; if (!get(d)) return false; out = static_cast<float>(d); return true; }
    // Integral getters succeed only when the value is exactly representable in T.
    template <class T, std::enable_if_t<std::is_integral<T>::value && !std::is_same<T, bool>::value, int> = 0>
    bool get(T& out) const noexcept {
        if constexpr (std::is_signed<T>::value) {
            int64_t v;
            if (!to_i64(v)) return false;
            if (v < static_cast<int64_t>((std::numeric_limits<T>::min)()) ||
                v > static_cast<int64_t>((std::numeric_limits<T>::max)())) return false;
            out = static_cast<T>(v);
        } else {
            uint64_t v;
            if (!to_u64(v)) return false;
            if (v > static_cast<uint64_t>((std::numeric_limits<T>::max)())) return false;
            out = static_cast<T>(v);
        }
        return true;
    }
    // Pointer into the value's own storage (NUL terminated, valid until mutated/destroyed).
    bool get_string(const char*& ptr, size_t& len) const noexcept {
        if (type_ != type::string) return false;
        ptr = v_.s.data(); len = v_.s.size(); return true;
    }
    const char* c_str(const char* fallback = "") const noexcept { return type_ == type::string ? v_.s.data() : fallback; }
    size_t length() const noexcept { return type_ == type::string ? v_.s.size() : 0; }
    bool string_equals(const char* s, size_t n) const noexcept { return type_ == type::string && v_.s.equals(s, n); }
    bool string_equals(const char* s) const noexcept { return s && string_equals(s, std::strlen(s)); }

    template <class T> T value_or(T def) const noexcept { T v; return get(v) ? v : def; }
    const char* value_or(const char* def) const noexcept { return type_ == type::string ? v_.s.data() : def; }
    // object convenience: obj.value("port", 8080)
    template <class T> T value(const char* key, T def) const noexcept {
        const knst_json* p = find(key); T v;
        return (p && p->get(v)) ? v : def;
    }
    const char* value(const char* key, const char* def) const noexcept {
        const knst_json* p = find(key);
        return (p && p->type_ == type::string) ? p->v_.s.data() : def;
    }
    // JSON Pointer convenience: cfg.value_at("/net/port", 80)
    template <class T> T value_at(const char* ptr, T def) const noexcept {
        const knst_json* p = find_pointer(ptr); T v;
        return (p && p->get(v)) ? v : def;
    }
    const char* value_at(const char* ptr, const char* def) const noexcept {
        const knst_json* p = find_pointer(ptr);
        return (p && p->type_ == type::string) ? p->v_.s.data() : def;
    }

    // ---- scalar mutation (replace current contents) -------------------------------
    void reset() noexcept { destroy(); }
    void set_null() noexcept { destroy(); }
    void set_bool(bool b) noexcept { destroy(); type_ = type::boolean; v_.u = 0; v_.b = b; }
    void set_double(double d) noexcept { destroy(); type_ = type::floating; v_.d = d; }
    void set_int64(int64_t i) noexcept { destroy(); type_ = type::integer; v_.i = i; }
    void set_uint64(uint64_t u) noexcept {
        destroy();
        if (u > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) { type_ = type::unsigned_integer; v_.u = u; }
        else { type_ = type::integer; v_.i = static_cast<int64_t>(u); }
    }
    bool set_string(const char* p, size_t n) noexcept {
        detail_str ns; ns.init();
        if (n && !p) return false;
        if (!ns.append(p, n)) { ns.destroy(); return false; }
        destroy(); type_ = type::string; v_.s = ns; return true;
    }
    bool set_string(const char* cstr) noexcept { return cstr ? set_string(cstr, std::strlen(cstr)) : false; }
    // UTF-16 input is converted to UTF-8.
    bool set_string_u16(const char16_t* p, size_t n) noexcept {
        knst_json_buffer b;
        if (!knst_json_detail::utf16_to_utf8(p, n, b)) return false;
        return set_string(b.data(), b.size());
    }
    // Raw bytes (not necessarily UTF-8). dump() will reject/replace invalid UTF-8 per its options.
    bool set_bytes(const unsigned char* p, size_t n) noexcept { return set_string(reinterpret_cast<const char*>(p), n); }
    // Appends to a string value (null becomes an empty string first).
    bool append_string(const char* p, size_t n) noexcept {
        if (type_ == type::null_value) { if (!set_string("", 0)) return false; }
        if (type_ != type::string) return false;
        return v_.s.append(p, n);
    }

    // ---- size ---------------------------------------------------------------------
    size_t size() const noexcept {
        if (type_ == type::array) return v_.a.n;
        if (type_ == type::object) return v_.o ? v_.o->n : 0;
        return 0;
    }
    bool empty() const noexcept {
        switch (type_) {
            case type::null_value: return true;
            case type::string: return v_.s.size() == 0;
            case type::array: case type::object: return size() == 0;
            default: return false;
        }
    }
    size_t capacity() const noexcept {
        if (type_ == type::array) return v_.a.c;
        if (type_ == type::object) return v_.o ? v_.o->c : 0;
        if (type_ == type::string) return v_.s.capacity();
        return 0;
    }
    // Removes all elements / members / characters but keeps the type (and array capacity).
    inline void clear() noexcept;
    // Releases surplus capacity of THIS container / string (not recursive).
    inline void shrink_to_fit() noexcept;

    // ---- array --------------------------------------------------------------------
    // null -> becomes an array automatically; any other non-array type returns false.
    bool reserve(size_t n) noexcept {
        KNST_JSON_TRY(ensure_array());
        if (n <= v_.a.c) return true;
        if (n > 0xFFFFFFF0u || n > (std::numeric_limits<size_t>::max)() / sizeof(knst_json)) return false;
        void* np = KNST_JSON_REALLOC(static_cast<void*>(v_.a.p), n * sizeof(knst_json));
        if (!np) return false;
        v_.a.p = static_cast<knst_json*>(np); v_.a.c = static_cast<uint32_t>(n);
        return true;
    }
    bool push_back(knst_json&& v) noexcept {
        KNST_JSON_TRY(ensure_array());
        knst_json* s = arr_push_slot();
        if (!s) return false;
        s->take(v);
        return true;
    }
    // Deep copy; returns false (and changes nothing) on allocation failure.
    bool push_back(const knst_json& v) noexcept { knst_json t; KNST_JSON_TRY(t.copy_from(v)); return push_back(std::move(t)); }
    // Appends a null element and returns it (nullptr on failure / wrong type).
    knst_json* push_back_slot() noexcept { return ensure_array() ? arr_push_slot() : nullptr; }
    bool insert(size_t idx, knst_json&& v) noexcept {
        KNST_JSON_TRY(ensure_array());
        if (idx > v_.a.n) return false;
        knst_json* s = arr_push_slot();
        if (!s) return false;
        knst_json* base = v_.a.p;
        std::memmove(static_cast<void*>(base + idx + 1), static_cast<const void*>(base + idx), (v_.a.n - 1 - idx) * sizeof(knst_json));
        base[idx].type_ = type::null_value;
        base[idx].take(v);
        return true;
    }
    bool insert(size_t idx, const knst_json& v) noexcept { knst_json t; KNST_JSON_TRY(t.copy_from(v)); return insert(idx, std::move(t)); }
    bool erase_at(size_t idx) noexcept {
        if (type_ != type::array || idx >= v_.a.n) return false;
        v_.a.p[idx].destroy();
        std::memmove(static_cast<void*>(v_.a.p + idx), static_cast<const void*>(v_.a.p + idx + 1), (v_.a.n - idx - 1) * sizeof(knst_json));
        --v_.a.n;
        return true;
    }
    // Erases [first, last).
    bool erase_range(size_t first, size_t last) noexcept {
        if (type_ != type::array || first > last || last > v_.a.n) return false;
        for (size_t i = first; i < last; ++i) v_.a.p[i].destroy();
        std::memmove(static_cast<void*>(v_.a.p + first), static_cast<const void*>(v_.a.p + last), (v_.a.n - last) * sizeof(knst_json));
        v_.a.n -= static_cast<uint32_t>(last - first);
        return true;
    }
    bool pop_back() noexcept { return type_ == type::array && v_.a.n && erase_at(v_.a.n - 1); }
    knst_json* at(size_t idx) noexcept { return (type_ == type::array && idx < v_.a.n) ? v_.a.p + idx : nullptr; }
    const knst_json* at(size_t idx) const noexcept { return (type_ == type::array && idx < v_.a.n) ? v_.a.p + idx : nullptr; }
    // Appends copies of all elements of another array.
    inline bool append(const knst_json& other_array) noexcept;
    // Moves the elements of `other_array` to the end of this array (other becomes empty).
    inline bool append_move(knst_json& other_array) noexcept;
    // Removes elements / members for which pred(const knst_json&) is true. Returns number removed.
    template <class Pred> size_t remove_if(Pred pred) noexcept;
    // Array sort (stable not guaranteed). Default: total order of compare().
    template <class Cmp> void sort_array(Cmp less) noexcept {
        if (type_ == type::array && v_.a.n > 1) std::sort(v_.a.p, v_.a.p + v_.a.n, less);
    }
    void sort_array() noexcept { sort_array([](const knst_json& a, const knst_json& b) noexcept { return compare(a, b) < 0; }); }
    // Removes duplicate elements (keeps first occurrences, O(n^2) worst case via hash prefilter).
    inline size_t unique_array() noexcept;
    // Range-for support for arrays (empty range for every other type).
    knst_json* begin() noexcept { return type_ == type::array ? v_.a.p : nullptr; }
    knst_json* end() noexcept { return type_ == type::array ? v_.a.p + v_.a.n : nullptr; }
    const knst_json* begin() const noexcept { return type_ == type::array ? v_.a.p : nullptr; }
    const knst_json* end() const noexcept { return type_ == type::array ? v_.a.p + v_.a.n : nullptr; }

    // Never-failing convenience accessors: out-of-range / wrong-type reads yield a shared null;
    // writes through a failed non-const operator[] land in a thread-local scratch value.
    template <class I, std::enable_if_t<std::is_integral<I>::value, int> = 0>
    const knst_json& operator[](I idx) const noexcept {
        const knst_json* p = idx_ok(idx) ? at(static_cast<size_t>(idx)) : nullptr;
        return p ? *p : null_ref();
    }
    template <class I, std::enable_if_t<std::is_integral<I>::value, int> = 0>
    knst_json& operator[](I idx) noexcept {
        knst_json* p = idx_ok(idx) ? at(static_cast<size_t>(idx)) : nullptr;
        return p ? *p : scratch();
    }

    // ---- object -------------------------------------------------------------------
    // null -> becomes an object automatically; any other non-object type returns false/nullptr.
    inline bool set(const char* key, size_t key_len, knst_json&& v) noexcept;
    bool set(const char* key, size_t key_len, const knst_json& v) noexcept { knst_json t; KNST_JSON_TRY(t.copy_from(v)); return set(key, key_len, std::move(t)); }
    bool set(const char* key, knst_json&& v) noexcept { return key && set(key, std::strlen(key), std::move(v)); }
    bool set(const char* key, const knst_json& v) noexcept { return key && set(key, std::strlen(key), v); }
    inline knst_json* find(const char* key, size_t key_len) noexcept;
    inline const knst_json* find(const char* key, size_t key_len) const noexcept;
    knst_json* find(const char* key) noexcept { return key ? find(key, std::strlen(key)) : nullptr; }
    const knst_json* find(const char* key) const noexcept { return key ? find(key, std::strlen(key)) : nullptr; }
    bool contains(const char* key) const noexcept { return find(key) != nullptr; }
    inline knst_json* get_or_insert(const char* key, size_t key_len) noexcept;
    inline bool erase(const char* key, size_t key_len) noexcept;
    bool erase(const char* key) noexcept { return key && erase(key, std::strlen(key)); }
    // Renames a key in place (keeps position). Fails if old is missing or new already exists.
    inline bool rename_key(const char* old_key, const char* new_key) noexcept;
    // Reorders members by key (bytewise / UTF-8 order). Rebuilds the hash index.
    inline bool sort_keys() noexcept;
    // Pre-allocates room for n members (null becomes an object).
    inline bool reserve_members(size_t n) noexcept;
    // Merges members of `other` into this object (deep copy; existing keys are overwritten).
    inline bool update(const knst_json& other) noexcept;

    knst_json& operator[](const char* key) noexcept {
        knst_json* p = key ? get_or_insert(key, std::strlen(key)) : nullptr;
        return p ? *p : scratch();
    }
    const knst_json& operator[](const char* key) const noexcept {
        const knst_json* p = find(key);
        return p ? *p : null_ref();
    }

    // Iteration over object members (insertion order). Defined after knst_json_member.
    inline member* members_begin() noexcept;
    inline member* members_end() noexcept;
    inline const member* members_begin() const noexcept;
    inline const member* members_end() const noexcept;
    struct member_range { member* b; member* e; member* begin() const noexcept { return b; } member* end() const noexcept { return e; } };
    struct const_member_range { const member* b; const member* e; const member* begin() const noexcept { return b; } const member* end() const noexcept { return e; } };
    member_range members() noexcept { return { members_begin(), members_end() }; }
    const_member_range members() const noexcept { return { members_begin(), members_end() }; }

    // ---- whole-value operations -----------------------------------------------------
    inline bool copy_from(const knst_json& o) noexcept;      // strict deep copy (iterative)
    inline bool operator==(const knst_json& o) const noexcept;   // iterative, objects order-insensitive
    bool operator!=(const knst_json& o) const noexcept { return !(*this == o); }
    bool operator<(const knst_json& o) const noexcept { return compare(*this, o) < 0; }
    void swap(knst_json& o) noexcept { knst_json t; t.take(*this); take(o); o.take(t); }
    // Total order: null < bool < number < string < array < object. NaN sorts first among numbers.
    static inline int compare(const knst_json& a, const knst_json& b) noexcept;
    // Exact numeric comparison (both must be numbers; int64/uint64/double are compared exactly).
    static inline int compare_numbers(const knst_json& a, const knst_json& b) noexcept;
    // Structural hash, consistent with operator== (1 == 1.0 hash alike, object order ignored).
    inline uint64_t hash() const noexcept;
    // RFC 7386 JSON Merge Patch. Not atomic: on failure *this may be partially patched
    // (use merge_patch_atomic for all-or-nothing).
    inline bool merge_patch(const knst_json& patch) noexcept;
    inline bool merge_patch_atomic(const knst_json& patch) noexcept;
    // Builds the merge patch that turns `from` into `to`.
    static inline bool make_merge_patch(const knst_json& from, const knst_json& to, knst_json& out) noexcept;

    // ---- JSON Pointer (RFC 6901) -----------------------------------------------------
    inline const knst_json* find_pointer(const char* ptr, size_t len) const noexcept;
    const knst_json* find_pointer(const char* ptr) const noexcept { return ptr ? find_pointer(ptr, std::strlen(ptr)) : nullptr; }
    knst_json* find_pointer(const char* ptr, size_t len) noexcept { return const_cast<knst_json*>(static_cast<const knst_json*>(this)->find_pointer(ptr, len)); }
    knst_json* find_pointer(const char* ptr) noexcept { return ptr ? find_pointer(ptr, std::strlen(ptr)) : nullptr; }
    // URI fragment representation: "#/a/b%20c"
    inline const knst_json* find_pointer_uri(const char* uri) const noexcept;
    // Returns the addressed value, creating intermediate objects/arrays ("-" appends).
    inline knst_json* pointer_slot(const char* ptr, size_t len) noexcept;
    bool pointer_set(const char* ptr, knst_json&& v) noexcept {
        knst_json* s = ptr ? pointer_slot(ptr, std::strlen(ptr)) : nullptr;
        if (!s) return false;
        *s = std::move(v);
        return true;
    }
    bool pointer_set(const char* ptr, const knst_json& v) noexcept { knst_json t; KNST_JSON_TRY(t.copy_from(v)); return pointer_set(ptr, std::move(t)); }
    // RFC 6902 "add" semantics: inserts into arrays (shifting), sets object members, "-" appends.
    inline bool pointer_insert(const char* ptr, size_t len, knst_json&& v) noexcept;
    bool pointer_insert(const char* ptr, knst_json&& v) noexcept { return ptr && pointer_insert(ptr, std::strlen(ptr), std::move(v)); }
    // Removes the addressed value (optionally handing it out).
    inline bool pointer_remove(const char* ptr, size_t len, knst_json* removed = nullptr) noexcept;
    bool pointer_remove(const char* ptr) noexcept { return ptr && pointer_remove(ptr, std::strlen(ptr), nullptr); }
    // Replaces an EXISTING value.
    bool pointer_replace(const char* ptr, knst_json&& v) noexcept {
        knst_json* s = find_pointer(ptr);
        if (!s) return false;
        *s = std::move(v);
        return true;
    }

    // ---- JSON Patch (RFC 6902) --------------------------------------------------------
    // atomic = true: works on a copy and commits only if every operation succeeds.
    inline bool apply_patch(const knst_json& patch, knst_json_error* err = nullptr, bool atomic = true) noexcept;
    // Generates a patch (array of operations) that turns `from` into `to`.
    static inline bool make_patch(const knst_json& from, const knst_json& to, knst_json& out) noexcept;

    // ---- queries / validation ---------------------------------------------------------
    // JSONPath subset (RFC 9535 flavoured): $ .name ['name'] [n] [-n] [*] .* [a:b:c] [1,2,'x']
    // ..name ..[*] and filters [?(@.price < 10)] [?(@.tags)] [?(@ == 3)].
    // `out` receives non-owning pointers into this tree.
    inline bool select(const char* path, size_t len, knst_json_ptr_list& out, knst_json_error* err = nullptr) const noexcept;
    bool select(const char* path, knst_json_ptr_list& out, knst_json_error* err = nullptr) const noexcept {
        if (!path) { fill_err(err, knst_json_errc::invalid_argument); return false; }
        return select(path, std::strlen(path), out, err);
    }
    // JSON Schema (draft-07 style subset; "pattern"/"format" are not evaluated).
    // `*this` is the instance. Returns true when valid.
    inline bool validate(const knst_json& schema, knst_json_validation* result = nullptr) const noexcept;

    // ---- traversal / flattening ---------------------------------------------------------
    // fn(const knst_json& value, const char* pointer, size_t pointer_len) -> bool (false stops).
    // Depth-first, parents before children. Returns false when stopped / out of memory / too deep.
    template <class F> bool walk(F&& fn) const noexcept(false);
    // {"a":{"b":[1,2]}} -> {"/a/b/0":1,"/a/b/1":2}   (empty containers are kept as leaves)
    inline bool flatten(knst_json& out) const noexcept;
    static inline bool unflatten(const knst_json& flat, knst_json& out) noexcept;
    // Number of values in the tree (including this one).
    inline size_t count_nodes() const noexcept;

    // ---- parsing ---------------------------------------------------------------------
    // `out` is only modified on success.
    static inline bool parse(const char* data, size_t size, knst_json& out, knst_json_error* err = nullptr,
                             const knst_json_parse_options& opt = knst_json_parse_options()) noexcept;
    static bool parse_cstr(const char* cstr, knst_json& out, knst_json_error* err = nullptr,
                           const knst_json_parse_options& opt = knst_json_parse_options()) noexcept {
        if (!cstr) { fill_err(err, knst_json_errc::invalid_argument); return false; }
        return parse(cstr, std::strlen(cstr), out, err, opt);
    }
    // Parses ONE value from the start of the input and reports how many bytes were used
    // (trailing whitespace included). Lets you read concatenated / streamed values.
    static inline bool parse_prefix(const char* data, size_t size, knst_json& out, size_t& consumed,
                                    knst_json_error* err = nullptr,
                                    const knst_json_parse_options& opt = knst_json_parse_options()) noexcept;
    // SAX: no tree is built. Handler: see knst_json_sax_base. Returns false on error or abort.
    template <class Handler>
    static inline bool parse_sax(const char* data, size_t size, Handler& handler, knst_json_error* err = nullptr,
                                 const knst_json_parse_options& opt = knst_json_parse_options()) noexcept;
    // NDJSON / JSON Lines: cb(size_t line_index, knst_json&& value) -> bool (false stops, still returns true).
    // Blank lines are skipped. A syntax error aborts and reports the absolute offset / line.
    template <class Callback>
    static inline bool parse_lines(const char* data, size_t size, Callback&& cb, knst_json_error* err = nullptr,
                                   const knst_json_parse_options& opt = knst_json_parse_options()) noexcept;
#if !defined(KNST_JSON_NO_FILE_IO)
    static inline bool load_file_utf8(const char* path_utf8, knst_json& out, knst_json_error* err = nullptr,
                                      const knst_json_parse_options& opt = knst_json_parse_options()) noexcept;
    static inline bool load_file_u16(const char16_t* path, size_t path_len, knst_json& out, knst_json_error* err = nullptr,
                                     const knst_json_parse_options& opt = knst_json_parse_options()) noexcept;
    // Streams an NDJSON file line by line (constant memory).
    template <class Callback>
    static inline bool load_lines_utf8(const char* path_utf8, Callback&& cb, knst_json_error* err = nullptr,
                                       const knst_json_parse_options& opt = knst_json_parse_options()) noexcept;
    // Reads everything from an open FILE* and parses it.
    static inline bool parse_stream(std::FILE* f, knst_json& out, knst_json_error* err = nullptr,
                                    const knst_json_parse_options& opt = knst_json_parse_options()) noexcept;
#endif

    // ---- serialization -----------------------------------------------------------------
    // `out` is cleared first. On failure `out` is left empty.
    inline bool dump(knst_json_buffer& out, const knst_json_dump_options& opt = knst_json_dump_options(),
                     knst_json_error* err = nullptr) const noexcept;
    // Appends instead of clearing (on failure the buffer is restored to its previous size).
    inline bool dump_append(knst_json_buffer& out, const knst_json_dump_options& opt = knst_json_dump_options(),
                            knst_json_error* err = nullptr) const noexcept;
    // Array -> one compact element per line (JSON Lines). Any other value -> a single line.
    inline bool dump_lines(knst_json_buffer& out, const knst_json_dump_options& opt = knst_json_dump_options(),
                           knst_json_error* err = nullptr) const noexcept;
    // Streaming output in ~64 KiB chunks: sink(ctx, data, len) must return true on success.
    inline bool dump_stream(bool (*sink)(void* ctx, const char* data, size_t len), void* ctx,
                            const knst_json_dump_options& opt = knst_json_dump_options(),
                            knst_json_error* err = nullptr) const noexcept;
#if !defined(KNST_JSON_NO_FILE_IO)
    inline bool dump_file(std::FILE* f, const knst_json_dump_options& opt = knst_json_dump_options(),
                          knst_json_error* err = nullptr) const noexcept;
    inline bool save_file_utf8(const char* path_utf8, const knst_json_dump_options& opt = knst_json_dump_options(),
                               bool atomic = true, knst_json_error* err = nullptr) const noexcept;
    inline bool save_file_u16(const char16_t* path, size_t path_len, const knst_json_dump_options& opt = knst_json_dump_options(),
                              bool atomic = true, knst_json_error* err = nullptr) const noexcept;
#endif

    // ---- std::string bridge (KNST_JSON_ENABLE_STD_STRING) ---------------------------------
#if defined(KNST_JSON_ENABLE_STD_STRING)
    knst_json(const std::string& s) noexcept : type_(type::null_value) { v_.u = 0; set_string(s.data(), s.size()); }
    bool set_string(const std::string& s) noexcept { return set_string(s.data(), s.size()); }
    std::string to_std_string() const { return type_ == type::string ? std::string(v_.s.data(), v_.s.size()) : std::string(); }
    bool get(std::string& out) const { if (type_ != type::string) return false; out.assign(v_.s.data(), v_.s.size()); return true; }
    std::string dump_string(const knst_json_dump_options& opt = knst_json_dump_options()) const {
        knst_json_buffer b; return dump(b, opt) ? std::string(b.data(), b.size()) : std::string();
    }
    static bool parse(const std::string& s, knst_json& out, knst_json_error* err = nullptr,
                      const knst_json_parse_options& opt = knst_json_parse_options()) noexcept {
        return parse(s.data(), s.size(), out, err, opt);
    }
    const knst_json* find(const std::string& key) const noexcept { return find(key.data(), key.size()); }
    knst_json* find(const std::string& key) noexcept { return find(key.data(), key.size()); }
    bool set(const std::string& key, knst_json&& v) noexcept { return set(key.data(), key.size(), std::move(v)); }
    bool set(const std::string& key, const knst_json& v) noexcept { return set(key.data(), key.size(), v); }
    knst_json& operator[](const std::string& key) noexcept {
        knst_json* p = get_or_insert(key.data(), key.size()); return p ? *p : scratch();
    }
#  if defined(__cpp_lib_string_view) || (defined(__has_include) && __has_include(<string_view>))
    std::string_view to_string_view() const noexcept { return type_ == type::string ? std::string_view(v_.s.data(), v_.s.size()) : std::string_view(); }
#  endif
#endif
    // ---- knst string bridges ---------------------------------------------------------------
#if defined(KNST_JSON_HAS_BYTE_STRING)
    explicit knst_json(const knst_byte_string& s) noexcept : type_(type::null_value) { v_.u = 0; set_string(s); }
    bool set_string(const knst_byte_string& s) noexcept {
        return set_string(reinterpret_cast<const char*>(s.data()), static_cast<size_t>(s.length()));
    }
    // Empty string when the value is not a string.
    knst_byte_string to_byte_string() const noexcept {
        if (type_ != type::string) return knst_byte_string();
        return knst_byte_string(reinterpret_cast<const unsigned char*>(v_.s.data()), static_cast<size_t>(v_.s.size()));
    }
    bool dump(knst_byte_string& out, const knst_json_dump_options& opt = knst_json_dump_options(), knst_json_error* err = nullptr) const noexcept {
        knst_json_buffer b;
        if (!dump(b, opt, err)) return false;
        out = knst_byte_string(reinterpret_cast<const unsigned char*>(b.data()), b.size());
        return true;
    }
    static bool parse(const knst_byte_string& s, knst_json& out, knst_json_error* err = nullptr,
                      const knst_json_parse_options& opt = knst_json_parse_options()) noexcept {
        return parse(reinterpret_cast<const char*>(s.data()), static_cast<size_t>(s.length()), out, err, opt);
    }
#endif
#if defined(KNST_JSON_HAS_C16STRING)
    explicit knst_json(const knst_c16string& s) noexcept : type_(type::null_value) { v_.u = 0; set_string(s); }
    bool set_string(const knst_c16string& s) noexcept { return set_string_u16(s.data(), static_cast<size_t>(s.length())); }
    knst_c16string to_c16string() const noexcept {
        if (type_ != type::string) return knst_c16string();
        knst_json_detail::basic_buffer<char16_t> b;
        if (!knst_json_detail::utf8_to_utf16<char16_t>(v_.s.data(), v_.s.size(), b)) return knst_c16string();
        return knst_c16string(b.data(), b.size());
    }
#  if !defined(KNST_JSON_NO_FILE_IO)
    static bool load_file(const knst_c16string& path, knst_json& out, knst_json_error* err = nullptr,
                          const knst_json_parse_options& opt = knst_json_parse_options()) noexcept {
        return load_file_u16(path.data(), static_cast<size_t>(path.length()), out, err, opt);
    }
    bool save_file(const knst_c16string& path, const knst_json_dump_options& opt = knst_json_dump_options(),
                   bool atomic = true, knst_json_error* err = nullptr) const noexcept {
        return save_file_u16(path.data(), static_cast<size_t>(path.length()), opt, atomic, err);
    }
#  endif
#endif

private:
    friend class knst_json_detail::writer;
    friend struct knst_json_detail::dom_sink;
    friend struct knst_json_member;
    using detail_str = knst_json_detail::str;

    static constexpr uint32_t index_threshold = 8;
    static constexpr uint32_t max_rec = KNST_JSON_MAX_RECURSION;

    struct arr_t { knst_json* p; uint32_t n; uint32_t c; };
    union Val {
        bool b; int64_t i; uint64_t u; double d; detail_str s; arr_t a; knst_json_detail::obj* o;
        Val() noexcept {}
    };

    type type_;
    Val v_;

    template <class I> static constexpr bool idx_ok(I idx) noexcept {
        if constexpr (std::is_signed<I>::value) return idx >= 0; else { (void)idx; return true; }
    }
    static void fill_err(knst_json_error* e, knst_json_errc c) noexcept { if (e) { *e = knst_json_error(); e->code = c; } }
    static const knst_json& null_ref() noexcept { static const knst_json n; return n; }
    static knst_json& scratch() noexcept { thread_local knst_json s; s.destroy(); return s; }

    // Raw take: steals `o` into *this (this must hold no resources). o becomes null.
    KNST_JSON_FI void take(knst_json& o) noexcept {
        type_ = o.type_;
        std::memcpy(static_cast<void*>(&v_), static_cast<const void*>(&o.v_), sizeof(Val));
        o.type_ = type::null_value;
    }
    template <class T> void set_integer(T v) noexcept {
        destroy();
        if constexpr (std::is_signed<T>::value) { type_ = type::integer; v_.i = static_cast<int64_t>(v); }
        else if (static_cast<uint64_t>(v) > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) { type_ = type::unsigned_integer; v_.u = static_cast<uint64_t>(v); }
        else { type_ = type::integer; v_.i = static_cast<int64_t>(v); }
    }
    bool to_i64(int64_t& out) const noexcept {
        switch (type_) {
            case type::integer: out = v_.i; return true;
            case type::floating:
                if (std::isfinite(v_.d) && v_.d >= -9223372036854775808.0 && v_.d < 9223372036854775808.0 && v_.d == std::floor(v_.d)) { out = static_cast<int64_t>(v_.d); return true; }
                return false;
            default: return false;
        }
    }
    bool to_u64(uint64_t& out) const noexcept {
        switch (type_) {
            case type::integer: if (v_.i < 0) return false; out = static_cast<uint64_t>(v_.i); return true;
            case type::unsigned_integer: out = v_.u; return true;
            case type::floating:
                if (std::isfinite(v_.d) && v_.d >= 0.0 && v_.d < 18446744073709551616.0 && v_.d == std::floor(v_.d)) { out = static_cast<uint64_t>(v_.d); return true; }
                return false;
            default: return false;
        }
    }

    bool ensure_array() noexcept {
        if (type_ == type::array) return true;
        if (type_ != type::null_value) return false;
        type_ = type::array; v_.a.p = nullptr; v_.a.n = v_.a.c = 0;
        return true;
    }
    knst_json* arr_push_slot() noexcept {
        arr_t& a = v_.a;
        if (a.n == a.c) {
            if (a.n >= 0xFFFFFFF0u) return nullptr;
            const uint32_t nc = a.c ? (a.c > 0x7FFFFFFFu ? 0xFFFFFFF0u : a.c * 2u) : 4u;
            void* np = KNST_JSON_REALLOC(static_cast<void*>(a.p), static_cast<size_t>(nc) * sizeof(knst_json));
            if (!np) return nullptr;
            a.p = static_cast<knst_json*>(np); a.c = nc;
        }
        knst_json* s = a.p + a.n;
        s->type_ = type::null_value; s->v_.u = 0;
        ++a.n;
        return s;
    }

    // object internals (defined after knst_json_member)
    inline bool obj_make() noexcept;
    inline bool ensure_object() noexcept;
    inline member* obj_lookup(const char* k, size_t kl) const noexcept;
    inline member* obj_emplace(const char* k, size_t kl, detail_str* take_key, bool& inserted) noexcept;
    inline bool obj_rebuild_index() noexcept;
    inline void destroy() noexcept;
    inline void destroy_tree() noexcept;
    inline bool copy_node(const knst_json& s, void* work) noexcept;
    inline int compare_rec(const knst_json& b, uint32_t depth) const noexcept;
    inline uint64_t hash_rec(uint32_t depth) const noexcept;
    inline bool merge_rec(const knst_json& patch, uint32_t depth) noexcept;
    inline bool pointer_parent(const char* ptr, size_t len, knst_json*& parent, size_t& tok_off) noexcept;
    inline bool patch_in_place(const knst_json& patch, knst_json_error* err) noexcept;
    template <class F> bool walk_rec(F& fn, knst_json_buffer& path, uint32_t depth) const;
    inline bool flatten_rec(knst_json& out, knst_json_buffer& path, uint32_t depth) const noexcept;
    static inline bool diff_rec(const knst_json& a, const knst_json& b, knst_json_buffer& path, knst_json& out, uint32_t depth) noexcept;
    static inline bool merge_patch_rec(const knst_json& from, const knst_json& to, knst_json& out, uint32_t depth) noexcept;
    static inline bool parse_impl(const char* data, size_t size, knst_json& out, size_t* consumed,
                                  knst_json_error* err, const knst_json_parse_options& opt) noexcept;
};
static_assert(sizeof(knst_json) <= 32, "knst_json should stay compact");
static_assert(sizeof(knst_json_detail::dframe) <= sizeof(knst_json), "iterative destroy frame must fit a value slot");

// ----------------------------------------------------------------------------
//  knst_json_member
// ----------------------------------------------------------------------------
struct knst_json_member {
    knst_json_detail::str k;
    knst_json v;
    const char* key() const noexcept { return k.data(); }
    size_t key_size() const noexcept { return k.size(); }
    bool key_equals(const char* s, size_t n) const noexcept { return k.equals(s, n); }
};

namespace knst_json_detail {
struct cpair { const knst_json* s; knst_json* d; };
struct eqpair { const knst_json* a; const knst_json* b; };
inline bool nul_term(knst_json_buffer& b) noexcept {
    if (!b.reserve(b.size() + 1)) return false;
    b.data()[b.size()] = '\0';
    return true;
}
inline int cmp_bytes(const char* a, size_t la, const char* b, size_t lb) noexcept {
    const int c = std::memcmp(a, b, la < lb ? la : lb);
    return c != 0 ? (c < 0 ? -1 : 1) : (la < lb ? -1 : (la > lb ? 1 : 0));
}
inline int type_rank(knst_json_type t) noexcept {
    switch (t) {
        case knst_json_type::null_value: return 0;
        case knst_json_type::boolean: return 1;
        case knst_json_type::integer: case knst_json_type::unsigned_integer: case knst_json_type::floating: return 2;
        case knst_json_type::string: return 3;
        case knst_json_type::array: return 4;
        default: return 5;
    }
}
} // namespace knst_json_detail

// ---- destroy (iterative, allocation free) -----------------------------------------------
inline void knst_json::destroy() noexcept {
    if (type_ == type::string) v_.s.destroy();
    else if (type_ == type::array || type_ == type::object) destroy_tree();
    type_ = type::null_value;
    v_.u = 0;
}
// Walks the tree with an explicit stack that lives INSIDE the already-dead value slots,
// so it needs no memory and cannot overflow the call stack however deep the tree is.
inline void knst_json::destroy_tree() noexcept {
    using knst_json_detail::dframe;
    knst_json* abase = nullptr;
    knst_json_member* mbase = nullptr;
    knst_json_detail::obj* ob = nullptr;
    uint32_t n = 0, i = 0, kind = 0;
    knst_json* top = nullptr;
    auto load = [&](const knst_json* c) noexcept {
        i = 0;
        if (c->type_ == type::array) { kind = 0; abase = c->v_.a.p; n = c->v_.a.n; ob = nullptr; mbase = nullptr; }
        else { kind = 1; ob = c->v_.o; abase = nullptr; mbase = ob ? ob->e : nullptr; n = ob ? ob->n : 0; }
    };
    load(this);
    for (;;) {
        while (i < n) {
            knst_json* ch;
            if (kind == 0) ch = abase + i;
            else { knst_json_member* m = mbase + i; m->k.destroy(); ch = &m->v; }
            ++i;
            if (ch->type_ == type::string) ch->v_.s.destroy();
            else if (ch->type_ == type::array || ch->type_ == type::object) {
                dframe f;
                f.prev = top;
                f.ref = kind == 0 ? reinterpret_cast<void*>(static_cast<uintptr_t>(n)) : static_cast<void*>(ob);
                f.i = i; f.kind = kind;
                load(ch);                                   // read the child's payload first ...
                std::memcpy(static_cast<void*>(ch), &f, sizeof(f));   // ... then reuse its dead slot as a frame
                top = ch;
            }
        }
        if (kind == 0) { if (abase) KNST_JSON_FREE(abase); }
        else if (ob) {
            if (ob->e) KNST_JSON_FREE(ob->e);
            if (ob->ix) KNST_JSON_FREE(ob->ix);
            KNST_JSON_FREE(ob);
        }
        if (!top) break;
        dframe f;
        std::memcpy(&f, static_cast<const void*>(top), sizeof(f));
        knst_json* slot = top;
        top = static_cast<knst_json*>(f.prev);
        kind = f.kind; i = f.i;
        if (kind == 0) { n = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f.ref)); abase = slot - (i - 1); ob = nullptr; mbase = nullptr; }
        else { ob = static_cast<knst_json_detail::obj*>(f.ref); mbase = ob->e; n = ob->n; abase = nullptr; }
    }
}

// ---- object ------------------------------------------------------------------------
inline bool knst_json::obj_make() noexcept {
    knst_json_detail::obj* o = static_cast<knst_json_detail::obj*>(KNST_JSON_MALLOC(sizeof(knst_json_detail::obj)));
    if (!o) return false;
    std::memset(o, 0, sizeof(*o));
    type_ = type::object; v_.o = o;
    return true;
}
inline bool knst_json::ensure_object() noexcept {
    if (type_ == type::object) return true;
    if (type_ != type::null_value) return false;
    return obj_make();
}
inline knst_json_member* knst_json::obj_lookup(const char* k, size_t kl) const noexcept {
    const knst_json_detail::obj* o = v_.o;
    if (!o || o->n == 0) return nullptr;
    if (!o->ix) {
        for (uint32_t i = 0; i < o->n; ++i) if (o->e[i].k.equals(k, kl)) return o->e + i;
        return nullptr;
    }
    const uint32_t h = knst_json_detail::hash_key(k, kl);
    const uint32_t mask = o->ixcap - 1;
    for (uint32_t pos = h & mask;; pos = (pos + 1) & mask) {
        const knst_json_detail::obj_index s = o->ix[pos];
        if (!s.idx1) return nullptr;
        if (s.hash == h) { knst_json_member* m = o->e + (s.idx1 - 1); if (m->k.equals(k, kl)) return m; }
    }
}
inline bool knst_json::obj_rebuild_index() noexcept {
    knst_json_detail::obj* o = v_.o;
    if (o->ix) { KNST_JSON_FREE(o->ix); o->ix = nullptr; o->ixcap = 0; }
    if (o->n <= index_threshold) return true;
    uint64_t cap = 16;
    while (cap < static_cast<uint64_t>(o->n) * 2) cap <<= 1;
    if (cap > 0x80000000ULL) return false;
    knst_json_detail::obj_index* ix = static_cast<knst_json_detail::obj_index*>(KNST_JSON_MALLOC(static_cast<size_t>(cap) * sizeof(knst_json_detail::obj_index)));
    if (!ix) return false;   // object stays valid, lookups fall back to linear scan
    std::memset(ix, 0, static_cast<size_t>(cap) * sizeof(knst_json_detail::obj_index));
    const uint32_t mask = static_cast<uint32_t>(cap - 1);
    for (uint32_t i = 0; i < o->n; ++i) {
        const uint32_t h = knst_json_detail::hash_key(o->e[i].k.data(), o->e[i].k.size());
        uint32_t pos = h & mask;
        while (ix[pos].idx1) pos = (pos + 1) & mask;
        ix[pos].idx1 = i + 1; ix[pos].hash = h;
    }
    o->ix = ix; o->ixcap = static_cast<uint32_t>(cap);
    return true;
}
inline knst_json_member* knst_json::obj_emplace(const char* k, size_t kl, detail_str* take_key, bool& inserted) noexcept {
    inserted = false;
    if (knst_json_member* m = obj_lookup(k, kl)) return m;
    knst_json_detail::obj* o = v_.o;
    if (o->n >= 0xFFFFFFF0u || kl > detail_str::max_len) return nullptr;
    if (o->n == o->c) {
        const uint32_t nc = o->c ? (o->c > 0x7FFFFFFFu ? 0xFFFFFFF0u : o->c * 2u) : 4u;
        void* np = KNST_JSON_REALLOC(static_cast<void*>(o->e), static_cast<size_t>(nc) * sizeof(knst_json_member));
        if (!np) return nullptr;
        o->e = static_cast<knst_json_member*>(np); o->c = nc;
    }
    knst_json_member* m = o->e + o->n;
    if (take_key) { m->k = *take_key; take_key->init(); }
    else { m->k.init(); if (!m->k.append(k, kl)) { m->k.destroy(); return nullptr; } }
    m->v.type_ = type::null_value; m->v.v_.u = 0;
    ++o->n;
    inserted = true;
    if (o->ix) {
        if (static_cast<uint64_t>(o->n) * 2 > o->ixcap) obj_rebuild_index();
        else {
            const uint32_t h = knst_json_detail::hash_key(m->k.data(), m->k.size());
            const uint32_t mask = o->ixcap - 1;
            uint32_t pos = h & mask;
            while (o->ix[pos].idx1) pos = (pos + 1) & mask;
            o->ix[pos].idx1 = o->n; o->ix[pos].hash = h;
        }
    } else if (o->n > index_threshold) obj_rebuild_index();
    return m;
}
inline bool knst_json::set(const char* key, size_t key_len, knst_json&& v) noexcept {
    if (!ensure_object() || (!key && key_len)) return false;
    bool ins;
    knst_json_member* m = obj_emplace(key ? key : "", key_len, nullptr, ins);
    if (!m) return false;
    m->v.destroy();
    m->v.take(v);
    return true;
}
inline knst_json* knst_json::find(const char* key, size_t key_len) noexcept {
    if (type_ != type::object || (!key && key_len)) return nullptr;
    knst_json_member* m = obj_lookup(key ? key : "", key_len);
    return m ? &m->v : nullptr;
}
inline const knst_json* knst_json::find(const char* key, size_t key_len) const noexcept {
    if (type_ != type::object || (!key && key_len)) return nullptr;
    const knst_json_member* m = obj_lookup(key ? key : "", key_len);
    return m ? &m->v : nullptr;
}
inline knst_json* knst_json::get_or_insert(const char* key, size_t key_len) noexcept {
    if (!ensure_object() || (!key && key_len)) return nullptr;
    bool ins;
    knst_json_member* m = obj_emplace(key ? key : "", key_len, nullptr, ins);
    return m ? &m->v : nullptr;
}
inline bool knst_json::erase(const char* key, size_t key_len) noexcept {
    if (type_ != type::object || (!key && key_len)) return false;
    knst_json_member* m = obj_lookup(key ? key : "", key_len);
    if (!m) return false;
    knst_json_detail::obj* o = v_.o;
    const size_t idx = static_cast<size_t>(m - o->e);
    m->k.destroy(); m->v.destroy();
    std::memmove(static_cast<void*>(o->e + idx), static_cast<const void*>(o->e + idx + 1), (o->n - idx - 1) * sizeof(knst_json_member));
    --o->n;
    if (o->ix) obj_rebuild_index();
    return true;
}
inline bool knst_json::rename_key(const char* old_key, const char* new_key) noexcept {
    if (type_ != type::object || !old_key || !new_key) return false;
    const size_t nl = std::strlen(new_key);
    knst_json_member* m = obj_lookup(old_key, std::strlen(old_key));
    if (!m || obj_lookup(new_key, nl)) return false;
    if (!m->k.assign(new_key, nl)) return false;
    if (v_.o->ix) obj_rebuild_index();
    return true;
}
inline bool knst_json::sort_keys() noexcept {
    if (type_ != type::object) return false;
    knst_json_detail::obj* o = v_.o;
    if (!o || o->n < 2) return true;
    const uint32_t n = o->n;
    knst_json_member** idx = static_cast<knst_json_member**>(KNST_JSON_MALLOC(sizeof(void*) * n));
    if (!idx) return false;
    knst_json_member* ne = static_cast<knst_json_member*>(KNST_JSON_MALLOC(sizeof(knst_json_member) * n));
    if (!ne) { KNST_JSON_FREE(idx); return false; }
    for (uint32_t i = 0; i < n; ++i) idx[i] = o->e + i;
    std::sort(idx, idx + n, [](const knst_json_member* a, const knst_json_member* b) noexcept {
        return knst_json_detail::cmp_bytes(a->k.data(), a->k.size(), b->k.data(), b->k.size()) < 0;
    });
    for (uint32_t i = 0; i < n; ++i) std::memcpy(static_cast<void*>(ne + i), static_cast<const void*>(idx[i]), sizeof(knst_json_member));  // bitwise relocation
    KNST_JSON_FREE(idx);
    KNST_JSON_FREE(o->e);
    o->e = ne; o->c = n;
    obj_rebuild_index();
    return true;
}
inline bool knst_json::reserve_members(size_t n) noexcept {
    KNST_JSON_TRY(ensure_object());
    knst_json_detail::obj* o = v_.o;
    if (n <= o->c) return true;
    if (n > 0xFFFFFFF0u || n > (std::numeric_limits<size_t>::max)() / sizeof(knst_json_member)) return false;
    void* np = KNST_JSON_REALLOC(static_cast<void*>(o->e), n * sizeof(knst_json_member));
    if (!np) return false;
    o->e = static_cast<knst_json_member*>(np); o->c = static_cast<uint32_t>(n);
    return true;
}
inline bool knst_json::update(const knst_json& other) noexcept {
    if (!other.is_object() || this == &other) return other.is_object();
    KNST_JSON_TRY(ensure_object());
    for (const member& m : other.members()) KNST_JSON_TRY(set(m.key(), m.key_size(), m.v));
    return true;
}
inline knst_json_member* knst_json::members_begin() noexcept { return (type_ == type::object && v_.o) ? v_.o->e : nullptr; }
inline knst_json_member* knst_json::members_end() noexcept { return (type_ == type::object && v_.o && v_.o->e) ? v_.o->e + v_.o->n : nullptr; }
inline const knst_json_member* knst_json::members_begin() const noexcept { return (type_ == type::object && v_.o) ? v_.o->e : nullptr; }
inline const knst_json_member* knst_json::members_end() const noexcept { return (type_ == type::object && v_.o && v_.o->e) ? v_.o->e + v_.o->n : nullptr; }

// ---- clear / shrink / array helpers ------------------------------------------------------
inline void knst_json::clear() noexcept {
    switch (type_) {
        case type::string: v_.s.set_size(0); break;
        case type::array:
            for (uint32_t i = 0; i < v_.a.n; ++i) v_.a.p[i].destroy();
            v_.a.n = 0;
            break;
        case type::object:
            if (v_.o) {
                for (uint32_t i = 0; i < v_.o->n; ++i) { v_.o->e[i].k.destroy(); v_.o->e[i].v.destroy(); }
                v_.o->n = 0;
                if (v_.o->ix) { KNST_JSON_FREE(v_.o->ix); v_.o->ix = nullptr; v_.o->ixcap = 0; }
            }
            break;
        default: break;
    }
}
inline void knst_json::shrink_to_fit() noexcept {
    if (type_ == type::string) { v_.s.shrink(); return; }
    if (type_ == type::array) {
        arr_t& a = v_.a;
        if (a.c == a.n) return;
        if (a.n == 0) { if (a.p) KNST_JSON_FREE(a.p); a.p = nullptr; a.c = 0; return; }
        void* np = KNST_JSON_REALLOC(static_cast<void*>(a.p), static_cast<size_t>(a.n) * sizeof(knst_json));
        if (np) { a.p = static_cast<knst_json*>(np); a.c = a.n; }
        return;
    }
    if (type_ == type::object && v_.o) {
        knst_json_detail::obj* o = v_.o;
        if (o->c == o->n) return;
        if (o->n == 0) { if (o->e) KNST_JSON_FREE(o->e); o->e = nullptr; o->c = 0; return; }
        void* np = KNST_JSON_REALLOC(static_cast<void*>(o->e), static_cast<size_t>(o->n) * sizeof(knst_json_member));
        if (np) { o->e = static_cast<knst_json_member*>(np); o->c = o->n; }
    }
}
inline bool knst_json::append(const knst_json& o) noexcept {
    if (!o.is_array()) return false;
    if (this == &o) { knst_json t; KNST_JSON_TRY(t.copy_from(o)); return append(t); }
    KNST_JSON_TRY(ensure_array());
    const size_t n0 = v_.a.n;
    if (!reserve(n0 + o.v_.a.n)) return false;
    for (uint32_t i = 0; i < o.v_.a.n; ++i) {
        knst_json* s = arr_push_slot();
        if (!s || !s->copy_from(o.v_.a.p[i])) { erase_range(n0, v_.a.n); return false; }
    }
    return true;
}
inline bool knst_json::append_move(knst_json& o) noexcept {
    if (!o.is_array() || this == &o) return false;
    KNST_JSON_TRY(ensure_array());
    if (!reserve(static_cast<size_t>(v_.a.n) + o.v_.a.n)) return false;
    if (o.v_.a.n) std::memcpy(static_cast<void*>(v_.a.p + v_.a.n), static_cast<const void*>(o.v_.a.p), o.v_.a.n * sizeof(knst_json));
    v_.a.n += o.v_.a.n;
    o.v_.a.n = 0;
    return true;
}
template <class Pred> size_t knst_json::remove_if(Pred pred) noexcept {
    size_t removed = 0;
    if constexpr (std::is_invocable<Pred&, const knst_json&>::value) {
        if (type_ == type::array) {
            uint32_t w = 0;
            for (uint32_t r = 0; r < v_.a.n; ++r) {
                if (pred(static_cast<const knst_json&>(v_.a.p[r]))) { v_.a.p[r].destroy(); ++removed; }
                else { if (w != r) std::memcpy(static_cast<void*>(v_.a.p + w), static_cast<const void*>(v_.a.p + r), sizeof(knst_json)); ++w; }
            }
            v_.a.n = w;
        }
    }
    if constexpr (std::is_invocable<Pred&, const knst_json_member&>::value) {
        if (type_ == type::object && v_.o) {
            knst_json_detail::obj* o = v_.o;
            uint32_t w = 0;
            for (uint32_t r = 0; r < o->n; ++r) {
                if (pred(static_cast<const knst_json_member&>(o->e[r]))) { o->e[r].k.destroy(); o->e[r].v.destroy(); ++removed; }
                else { if (w != r) std::memcpy(static_cast<void*>(o->e + w), static_cast<const void*>(o->e + r), sizeof(knst_json_member)); ++w; }
            }
            o->n = w;
            if (removed && o->ix) obj_rebuild_index();
        }
    }
    return removed;
}
inline size_t knst_json::unique_array() noexcept {
    if (type_ != type::array || v_.a.n < 2) return 0;
    const uint32_t n = v_.a.n;
    uint64_t* hs = static_cast<uint64_t*>(KNST_JSON_MALLOC(sizeof(uint64_t) * n));   // optional prefilter
    uint32_t w = 0; size_t removed = 0;
    for (uint32_t r = 0; r < n; ++r) {
        knst_json* a = v_.a.p;
        const uint64_t hr = hs ? a[r].hash() : 0;
        bool dup = false;
        for (uint32_t k = 0; k < w; ++k) if ((!hs || hs[k] == hr) && a[k] == a[r]) { dup = true; break; }
        if (dup) { a[r].destroy(); ++removed; }
        else { if (w != r) std::memcpy(static_cast<void*>(a + w), static_cast<const void*>(a + r), sizeof(knst_json)); if (hs) hs[w] = hr; ++w; }
    }
    v_.a.n = w;
    if (hs) KNST_JSON_FREE(hs);
    return removed;
}

// ---- deep copy (iterative) ---------------------------------------------------------------
// Fills *this (which must be a fresh null value) from `s`; containers are queued in `work`
// as (source, destination) pairs. Destination storage is allocated at its final size, so
// the queued destination pointers never move.
inline bool knst_json::copy_node(const knst_json& s, void* w) noexcept {
    using knst_json_detail::cpair;
    auto& work = *static_cast<knst_json_detail::basic_buffer<cpair>*>(w);
    switch (s.type_) {
        case type::string: {
            detail_str ns; ns.init();
            if (!ns.append(s.v_.s.data(), s.v_.s.size())) { ns.destroy(); return false; }
            type_ = type::string; v_.s = ns;
            return true;
        }
        case type::array: {
            type_ = type::array; v_.a.p = nullptr; v_.a.n = v_.a.c = 0;
            const uint32_t n = s.v_.a.n;
            if (!n) return true;
            knst_json* np = static_cast<knst_json*>(KNST_JSON_MALLOC(static_cast<size_t>(n) * sizeof(knst_json)));
            if (!np) return false;
            for (uint32_t j = 0; j < n; ++j) { np[j].type_ = type::null_value; np[j].v_.u = 0; }
            v_.a.p = np; v_.a.c = n; v_.a.n = n;
            for (uint32_t j = 0; j < n; ++j) {
                const knst_json& c = s.v_.a.p[j];
                if (c.is_container()) { if (!work.push(cpair{ &c, np + j })) return false; }
                else if (!np[j].copy_node(c, w)) return false;
            }
            return true;
        }
        case type::object: {
            const knst_json_detail::obj* so = s.v_.o;
            const uint32_t n = so ? so->n : 0;
            if (!obj_make()) return false;
            if (!n) return true;
            knst_json_detail::obj* o = v_.o;
            knst_json_member* e = static_cast<knst_json_member*>(KNST_JSON_MALLOC(static_cast<size_t>(n) * sizeof(knst_json_member)));
            if (!e) return false;
            o->e = e; o->c = n;
            for (uint32_t j = 0; j < n; ++j) {
                knst_json_member* m = e + j;
                m->k.init();
                if (!m->k.append(so->e[j].k.data(), so->e[j].k.size())) { m->k.destroy(); return false; }
                m->v.type_ = type::null_value; m->v.v_.u = 0;
                o->n = j + 1;
                const knst_json& c = so->e[j].v;
                if (c.is_container()) { if (!work.push(cpair{ &c, &m->v })) return false; }
                else if (!m->v.copy_node(c, w)) return false;
            }
            if (n > index_threshold) obj_rebuild_index();
            return true;
        }
        default:
            type_ = s.type_;
            std::memcpy(static_cast<void*>(&v_), static_cast<const void*>(&s.v_), sizeof(Val));
            return true;
    }
}
inline bool knst_json::copy_from(const knst_json& o) noexcept {
    if (this == &o) return true;
    using knst_json_detail::cpair;
    knst_json t;   // build aside, commit on success
    knst_json_detail::basic_buffer<cpair> work;
    KNST_JSON_TRY(t.copy_node(o, &work));
    while (!work.empty()) {
        const cpair c = work.data()[work.size() - 1];
        work.set_size(work.size() - 1);
        KNST_JSON_TRY(c.d->copy_node(*c.s, &work));
    }
    destroy();
    take(t);
    return true;
}

// ---- comparison ----------------------------------------------------------------------------
inline int knst_json::compare_numbers(const knst_json& a, const knst_json& b) noexcept {
    auto sgn = [](auto x, auto y) noexcept { return x < y ? -1 : (x > y ? 1 : 0); };
    // sign of (integer node - f)
    auto int_vs_float = [](const knst_json& n, double f) noexcept -> int {
        if (std::isnan(f)) return 1;                          // NaN sorts below every number
        if (n.type_ == type::integer) {
            if (f >= 9223372036854775808.0) return -1;
            if (f < -9223372036854775808.0) return 1;
            const double fl = std::floor(f);
            const int64_t fi = static_cast<int64_t>(fl);
            if (n.v_.i != fi) return n.v_.i < fi ? -1 : 1;
            return f > fl ? -1 : 0;
        }
        if (f >= 18446744073709551616.0) return -1;
        if (f < 0.0) return 1;
        const double fl = std::floor(f);
        const uint64_t fu = static_cast<uint64_t>(fl);
        if (n.v_.u != fu) return n.v_.u < fu ? -1 : 1;
        return f > fl ? -1 : 0;
    };
    if (a.type_ == b.type_) {
        switch (a.type_) {
            case type::integer: return sgn(a.v_.i, b.v_.i);
            case type::unsigned_integer: return sgn(a.v_.u, b.v_.u);
            default: {
                const bool an = std::isnan(a.v_.d), bn = std::isnan(b.v_.d);
                if (an || bn) return an && bn ? 0 : (an ? -1 : 1);
                return sgn(a.v_.d, b.v_.d);
            }
        }
    }
    if (a.type_ == type::integer && b.type_ == type::unsigned_integer) return -1;
    if (a.type_ == type::unsigned_integer && b.type_ == type::integer) return 1;
    if (b.type_ == type::floating) return int_vs_float(a, b.v_.d);
    return -int_vs_float(b, a.v_.d);
}
inline bool knst_json::operator==(const knst_json& o) const noexcept {
    using knst_json_detail::eqpair;
    knst_json_detail::basic_buffer<eqpair> st;
    const knst_json* a = this;
    const knst_json* b = &o;
    for (;;) {
        if (a == b) {
            if (a->type_ == type::floating && std::isnan(a->v_.d)) return false;
        } else if (a->is_number() && b->is_number()) {
            if ((a->type_ == type::floating && std::isnan(a->v_.d)) || (b->type_ == type::floating && std::isnan(b->v_.d))) return false;
            if (compare_numbers(*a, *b) != 0) return false;
        } else {
            if (a->type_ != b->type_) return false;
            switch (a->type_) {
                case type::boolean: if (a->v_.b != b->v_.b) return false; break;
                case type::string: if (!a->v_.s.equals(b->v_.s.data(), b->v_.s.size())) return false; break;
                case type::array:
                    if (a->v_.a.n != b->v_.a.n) return false;
                    for (uint32_t i = 0; i < a->v_.a.n; ++i)
                        if (!st.push(eqpair{ a->v_.a.p + i, b->v_.a.p + i })) return false;
                    break;
                case type::object: {
                    if (a->size() != b->size()) return false;
                    for (const member& m : a->members()) {
                        const knst_json* r = b->find(m.key(), m.key_size());
                        if (!r || !st.push(eqpair{ &m.v, r })) return false;
                    }
                    break;
                }
                default: break;
            }
        }
        if (st.empty()) return true;
        const eqpair p = st.data()[st.size() - 1];
        st.set_size(st.size() - 1);
        a = p.a; b = p.b;
    }
}
inline int knst_json::compare(const knst_json& a, const knst_json& b) noexcept { return a.compare_rec(b, 0); }
inline int knst_json::compare_rec(const knst_json& b, uint32_t depth) const noexcept {
    using namespace knst_json_detail;
    const int ra = type_rank(type_), rb = type_rank(b.type_);
    if (ra != rb) return ra < rb ? -1 : 1;
    switch (type_) {
        case type::null_value: return 0;
        case type::boolean: return static_cast<int>(v_.b) - static_cast<int>(b.v_.b);
        case type::string: return cmp_bytes(v_.s.data(), v_.s.size(), b.v_.s.data(), b.v_.s.size());
        case type::array: {
            if (depth >= max_rec) return 0;
            const uint32_t n = v_.a.n < b.v_.a.n ? v_.a.n : b.v_.a.n;
            for (uint32_t i = 0; i < n; ++i) { const int c = v_.a.p[i].compare_rec(b.v_.a.p[i], depth + 1); if (c) return c; }
            return v_.a.n < b.v_.a.n ? -1 : (v_.a.n > b.v_.a.n ? 1 : 0);
        }
        case type::object: {
            if (depth >= max_rec) return 0;
            const size_t na = size(), nb = b.size();
            const int szc = na < nb ? -1 : (na > nb ? 1 : 0);
            if (na == 0 || nb == 0) return szc;
            const member** sa = static_cast<const member**>(KNST_JSON_MALLOC(sizeof(void*) * (na + nb)));
            if (!sa) return szc;
            const member** sb = sa + na;
            for (size_t i = 0; i < na; ++i) sa[i] = v_.o->e + i;
            for (size_t i = 0; i < nb; ++i) sb[i] = b.v_.o->e + i;
            auto kl = [](const member* x, const member* y) noexcept { return cmp_bytes(x->k.data(), x->k.size(), y->k.data(), y->k.size()) < 0; };
            std::sort(sa, sa + na, kl);
            std::sort(sb, sb + nb, kl);
            int res = 0;
            const size_t n = na < nb ? na : nb;
            for (size_t i = 0; i < n && !res; ++i) {
                res = cmp_bytes(sa[i]->k.data(), sa[i]->k.size(), sb[i]->k.data(), sb[i]->k.size());
                if (!res) res = sa[i]->v.compare_rec(sb[i]->v, depth + 1);
            }
            KNST_JSON_FREE(sa);
            return res ? res : szc;
        }
        default: return compare_numbers(*this, b);
    }
}
inline uint64_t knst_json::hash() const noexcept { return hash_rec(0); }
inline uint64_t knst_json::hash_rec(uint32_t depth) const noexcept {
    using namespace knst_json_detail;
    auto hi = [](int64_t v) noexcept { return mix64(static_cast<uint64_t>(v) + 0x51ED270BULL); };
    auto hu = [](uint64_t v) noexcept { return mix64(v + 0x51ED270BULL); };
    switch (type_) {
        case type::null_value: return 0x9E3779B97F4A7C15ULL;
        case type::boolean: return mix64(0x100u + (v_.b ? 1u : 0u));
        case type::integer: return hi(v_.i);
        case type::unsigned_integer: return hu(v_.u);
        case type::floating: {
            const double d = v_.d;
            if (std::isnan(d)) return 0x7FF8000000000001ULL;
            if (d == std::floor(d)) {
                if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) return hi(static_cast<int64_t>(d));
                if (d >= 9223372036854775808.0 && d < 18446744073709551616.0) return hu(static_cast<uint64_t>(d));
            }
            uint64_t bits; std::memcpy(&bits, &d, 8);
            return mix64(bits ^ 0xD1B54A32D192ED03ULL);
        }
        case type::string: return hash_bytes64(v_.s.data(), v_.s.size()) ^ 0x57;
        case type::array: {
            uint64_t h = 0xA11CEULL;
            if (depth >= max_rec) return h;
            for (uint32_t i = 0; i < v_.a.n; ++i) h = mix64(h * 31 + v_.a.p[i].hash_rec(depth + 1));
            return h;
        }
        case type::object: {
            uint64_t h = 0x0B1EC7ULL;
            if (depth >= max_rec || !v_.o) return h;
            for (uint32_t i = 0; i < v_.o->n; ++i)   // order independent
                h += mix64(hash_bytes64(v_.o->e[i].k.data(), v_.o->e[i].k.size()) ^ (v_.o->e[i].v.hash_rec(depth + 1) * 0x9E3779B97F4A7C15ULL));
            return h;
        }
    }
    return 0;
}
inline size_t knst_json::count_nodes() const noexcept {
    knst_json_ptr_list st;
    size_t cnt = 0;
    if (!st.push(this)) return 0;
    while (!st.empty()) {
        const knst_json* c = st.data()[st.size() - 1];
        st.set_size(st.size() - 1);
        ++cnt;
        if (c->type_ == type::array) { for (uint32_t i = 0; i < c->v_.a.n; ++i) if (!st.push(c->v_.a.p + i)) return 0; }
        else if (c->type_ == type::object && c->v_.o) { for (uint32_t i = 0; i < c->v_.o->n; ++i) if (!st.push(&c->v_.o->e[i].v)) return 0; }
    }
    return cnt;
}

// ---- merge patch (RFC 7386) ----------------------------------------------------------------
inline bool knst_json::merge_patch(const knst_json& patch) noexcept { return merge_rec(patch, 0); }
inline bool knst_json::merge_rec(const knst_json& patch, uint32_t depth) noexcept {
    if (this == &patch) return true;
    if (!patch.is_object()) return copy_from(patch);
    if (depth >= max_rec) return false;
    if (type_ != type::object) { destroy(); KNST_JSON_TRY(obj_make()); }
    for (const member& m : patch.members()) {
        if (m.v.is_null()) { erase(m.key(), m.key_size()); continue; }
        knst_json* slot = get_or_insert(m.key(), m.key_size());
        if (!slot || !slot->merge_rec(m.v, depth + 1)) return false;
    }
    return true;
}
inline bool knst_json::merge_patch_atomic(const knst_json& patch) noexcept {
    knst_json w;
    KNST_JSON_TRY(w.copy_from(*this));
    KNST_JSON_TRY(w.merge_rec(patch, 0));
    destroy(); take(w);
    return true;
}
inline bool knst_json::make_merge_patch(const knst_json& from, const knst_json& to, knst_json& out) noexcept {
    knst_json r;
    KNST_JSON_TRY(merge_patch_rec(from, to, r, 0));
    out = std::move(r);
    return true;
}
inline bool knst_json::merge_patch_rec(const knst_json& from, const knst_json& to, knst_json& out, uint32_t depth) noexcept {
    if (!from.is_object() || !to.is_object() || depth >= max_rec) return out.copy_from(to);
    knst_json r = make_object();
    if (!r.is_object()) return false;
    for (const member& m : to.members()) {
        const knst_json* f = from.find(m.key(), m.key_size());
        if (!f) { KNST_JSON_TRY(r.set(m.key(), m.key_size(), m.v)); continue; }
        if (*f == m.v) continue;
        knst_json sub;
        KNST_JSON_TRY(merge_patch_rec(*f, m.v, sub, depth + 1));
        KNST_JSON_TRY(r.set(m.key(), m.key_size(), std::move(sub)));
    }
    for (const member& m : from.members())
        if (!to.find(m.key(), m.key_size())) KNST_JSON_TRY(r.set(m.key(), m.key_size(), knst_json()));
    out = std::move(r);
    return true;
}

// ---- JSON pointer -----------------------------------------------------------------------
inline const knst_json* knst_json::find_pointer(const char* ptr, size_t len) const noexcept {
    if (!ptr && len) return nullptr;
    const knst_json* cur = this;
    if (len == 0) return cur;
    if (ptr[0] != '/') return nullptr;
    const char* p = ptr + 1;
    const char* e = ptr + len;
    for (;;) {
        const char* ts = p;
        while (p < e && *p != '/') ++p;
        knst_json_detail::token_buf tb;
        if (!tb.set(ts, static_cast<size_t>(p - ts))) return nullptr;
        if (cur->type_ == type::object) cur = cur->find(tb.p, tb.n);
        else if (cur->type_ == type::array) { size_t idx; cur = tb.is_index(idx) ? cur->at(idx) : nullptr; }
        else return nullptr;
        if (!cur) return nullptr;
        if (p == e) return cur;
        ++p;
    }
}
inline const knst_json* knst_json::find_pointer_uri(const char* uri) const noexcept {
    if (!uri) return nullptr;
    if (*uri == '#') ++uri;
    knst_json_buffer b;
    for (const char* p = uri; *p; ++p) {
        char c = *p;
        if (c == '%' && p[1] && p[2]) {
            auto hv = [](char h) noexcept { return h >= '0' && h <= '9' ? h - '0' : (h >= 'a' && h <= 'f' ? h - 'a' + 10 : (h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1)); };
            const int h1 = hv(p[1]), h2 = hv(p[2]);
            if (h1 < 0 || h2 < 0) return nullptr;
            c = static_cast<char>(h1 * 16 + h2); p += 2;
        }
        if (!b.push(c)) return nullptr;
    }
    return find_pointer(b.data(), b.size());
}
inline knst_json* knst_json::pointer_slot(const char* ptr, size_t len) noexcept {
    if (!ptr && len) return nullptr;
    knst_json* cur = this;
    if (len == 0) return cur;
    if (ptr[0] != '/') return nullptr;
    const char* p = ptr + 1;
    const char* e = ptr + len;
    for (;;) {
        const char* ts = p;
        while (p < e && *p != '/') ++p;
        knst_json_detail::token_buf tb;
        if (!tb.set(ts, static_cast<size_t>(p - ts))) return nullptr;
        size_t idx = 0;
        const bool numeric = tb.is_dash() || tb.is_index(idx);
        if (cur->type_ == type::null_value) { if (numeric ? !cur->ensure_array() : !cur->ensure_object()) return nullptr; }
        if (cur->type_ == type::object) cur = cur->get_or_insert(tb.p, tb.n);
        else if (cur->type_ == type::array) {
            if (!numeric) return nullptr;
            if (tb.is_dash() || idx == cur->v_.a.n) cur = cur->arr_push_slot();
            else cur = cur->at(idx);   // only existing index or append position are allowed
        } else return nullptr;
        if (!cur) return nullptr;
        if (p == e) return cur;
        ++p;
    }
}
inline bool knst_json::pointer_parent(const char* ptr, size_t len, knst_json*& parent, size_t& tok_off) noexcept {
    if (!ptr || len == 0 || ptr[0] != '/') return false;
    size_t pl = len;
    while (pl > 0 && ptr[pl - 1] != '/') --pl;
    --pl;                                   // index of the last '/'
    parent = find_pointer(ptr, pl);
    tok_off = pl + 1;
    return parent != nullptr;
}
inline bool knst_json::pointer_insert(const char* ptr, size_t len, knst_json&& v) noexcept {
    if (!ptr && len) return false;
    if (len == 0) { *this = std::move(v); return true; }
    knst_json* par; size_t off;
    KNST_JSON_TRY(pointer_parent(ptr, len, par, off));
    knst_json_detail::token_buf tb;
    KNST_JSON_TRY(tb.set(ptr + off, len - off));
    if (par->is_object()) return par->set(tb.p, tb.n, std::move(v));
    if (par->is_array()) {
        if (tb.is_dash()) return par->push_back(std::move(v));
        size_t idx;
        if (!tb.is_index(idx) || idx > par->v_.a.n) return false;
        return par->insert(idx, std::move(v));
    }
    return false;
}
inline bool knst_json::pointer_remove(const char* ptr, size_t len, knst_json* removed) noexcept {
    if (!ptr && len) return false;
    if (len == 0) { if (removed) *removed = std::move(*this); else destroy(); return true; }
    knst_json* par; size_t off;
    KNST_JSON_TRY(pointer_parent(ptr, len, par, off));
    knst_json_detail::token_buf tb;
    KNST_JSON_TRY(tb.set(ptr + off, len - off));
    if (par->is_object()) {
        knst_json* v = par->find(tb.p, tb.n);
        if (!v) return false;
        if (removed) *removed = std::move(*v);
        return par->erase(tb.p, tb.n);
    }
    if (par->is_array()) {
        size_t idx;
        if (!tb.is_index(idx) || idx >= par->v_.a.n) return false;
        if (removed) *removed = std::move(par->v_.a.p[idx]);
        return par->erase_at(idx);
    }
    return false;
}

// ---- JSON Patch (RFC 6902) ----------------------------------------------------------------
namespace knst_json_detail {
inline bool emit_op(knst_json& out, const char* op, const knst_json_buffer& path, const knst_json* val) noexcept {
    knst_json o = knst_json::make_object();
    if (!o.is_object()) return false;
    KNST_JSON_TRY(o.set("op", knst_json(op)));
    KNST_JSON_TRY(o.set("path", knst_json(path.data(), path.size())));
    if (val) KNST_JSON_TRY(o.set("value", *val));
    return out.push_back(std::move(o));
}
} // namespace knst_json_detail

inline bool knst_json::apply_patch(const knst_json& patch, knst_json_error* err, bool atomic) noexcept {
    if (err) *err = knst_json_error();
    if (!patch.is_array()) { fill_err(err, knst_json_errc::patch_invalid); return false; }
    if (!atomic) return patch_in_place(patch, err);
    knst_json w;
    if (!w.copy_from(*this)) { fill_err(err, knst_json_errc::out_of_memory); return false; }
    if (!w.patch_in_place(patch, err)) return false;
    destroy(); take(w);
    return true;
}
inline bool knst_json::patch_in_place(const knst_json& patch, knst_json_error* err) noexcept {
    auto bad = [&](size_t i, knst_json_errc c) noexcept {
        if (err) { *err = knst_json_error(); err->code = c; err->offset = i; }
        return false;
    };
    for (size_t i = 0; i < patch.size(); ++i) {
        const knst_json& op = patch[i];
        if (!op.is_object()) return bad(i, knst_json_errc::patch_invalid);
        const knst_json* jo = op.find("op");
        const knst_json* jp = op.find("path");
        if (!jo || !jo->is_string() || !jp || !jp->is_string()) return bad(i, knst_json_errc::patch_invalid);
        const char* pp = jp->c_str(); const size_t pl = jp->length();
        const knst_json* val = op.find("value");
        const knst_json* jf = op.find("from");
        if (jo->string_equals("add") || jo->string_equals("replace") || jo->string_equals("test")) {
            if (!val) return bad(i, knst_json_errc::patch_invalid);
        }
        if (jo->string_equals("move") || jo->string_equals("copy")) {
            if (!jf || !jf->is_string()) return bad(i, knst_json_errc::patch_invalid);
        }
        if (jo->string_equals("add")) {
            knst_json t;
            if (!t.copy_from(*val)) return bad(i, knst_json_errc::out_of_memory);
            if (!pointer_insert(pp, pl, std::move(t))) return bad(i, knst_json_errc::path_not_found);
        } else if (jo->string_equals("remove")) {
            if (!pointer_remove(pp, pl, nullptr)) return bad(i, knst_json_errc::path_not_found);
        } else if (jo->string_equals("replace")) {
            knst_json t;
            if (!t.copy_from(*val)) return bad(i, knst_json_errc::out_of_memory);
            knst_json* s = find_pointer(pp, pl);
            if (!s) return bad(i, knst_json_errc::path_not_found);
            *s = std::move(t);
        } else if (jo->string_equals("test")) {
            const knst_json* s = find_pointer(pp, pl);
            if (!s) return bad(i, knst_json_errc::path_not_found);
            if (!(*s == *val)) return bad(i, knst_json_errc::patch_test_failed);
        } else if (jo->string_equals("move")) {
            const char* fp = jf->c_str(); const size_t fl = jf->length();
            if (!find_pointer(fp, fl)) return bad(i, knst_json_errc::path_not_found);
            if (fl == pl && std::memcmp(fp, pp, fl) == 0) continue;                     // no-op
            if (pl > fl && std::memcmp(fp, pp, fl) == 0 && pp[fl] == '/') return bad(i, knst_json_errc::patch_invalid);
            knst_json t;
            if (!pointer_remove(fp, fl, &t)) return bad(i, knst_json_errc::path_not_found);
            if (!pointer_insert(pp, pl, std::move(t))) return bad(i, knst_json_errc::path_not_found);
        } else if (jo->string_equals("copy")) {
            const knst_json* s = find_pointer(jf->c_str(), jf->length());
            if (!s) return bad(i, knst_json_errc::path_not_found);
            knst_json t;
            if (!t.copy_from(*s)) return bad(i, knst_json_errc::out_of_memory);
            if (!pointer_insert(pp, pl, std::move(t))) return bad(i, knst_json_errc::path_not_found);
        } else return bad(i, knst_json_errc::patch_invalid);
    }
    return true;
}
inline bool knst_json::make_patch(const knst_json& from, const knst_json& to, knst_json& out) noexcept {
    knst_json r = make_array();
    knst_json_buffer path;
    KNST_JSON_TRY(diff_rec(from, to, path, r, 0));
    out = std::move(r);
    return true;
}
inline bool knst_json::diff_rec(const knst_json& a, const knst_json& b, knst_json_buffer& path, knst_json& out, uint32_t depth) noexcept {
    using knst_json_detail::emit_op;
    if (a == b) return true;
    if (a.is_object() && b.is_object() && depth < max_rec) {
        for (const member& m : a.members()) {
            const size_t mark = path.size();
            KNST_JSON_TRY(knst_json_detail::pointer_escape_append(path, m.key(), m.key_size()));
            const knst_json* o = b.find(m.key(), m.key_size());
            const bool ok = o ? diff_rec(m.v, *o, path, out, depth + 1) : emit_op(out, "remove", path, nullptr);
            path.set_size(mark);
            KNST_JSON_TRY(ok);
        }
        for (const member& m : b.members()) {
            if (a.find(m.key(), m.key_size())) continue;
            const size_t mark = path.size();
            KNST_JSON_TRY(knst_json_detail::pointer_escape_append(path, m.key(), m.key_size()));
            const bool ok = emit_op(out, "add", path, &m.v);
            path.set_size(mark);
            KNST_JSON_TRY(ok);
        }
        return true;
    }
    if (a.is_array() && b.is_array() && depth < max_rec) {
        const size_t na = a.size(), nb = b.size(), m = na < nb ? na : nb;
        char t[24];
        for (size_t i = 0; i < m; ++i) {
            const size_t mark = path.size();
            KNST_JSON_TRY(path.push('/'));
            KNST_JSON_TRY(path.append(t, knst_json_detail::format_u64(i, t)));
            const bool ok = diff_rec(a[i], b[i], path, out, depth + 1);
            path.set_size(mark);
            KNST_JSON_TRY(ok);
        }
        for (size_t i = na; i > m; --i) {         // remove from the tail so indices stay valid
            const size_t mark = path.size();
            KNST_JSON_TRY(path.push('/'));
            KNST_JSON_TRY(path.append(t, knst_json_detail::format_u64(i - 1, t)));
            const bool ok = emit_op(out, "remove", path, nullptr);
            path.set_size(mark);
            KNST_JSON_TRY(ok);
        }
        for (size_t i = m; i < nb; ++i) {
            const size_t mark = path.size();
            KNST_JSON_TRY(path.push('/'));
            KNST_JSON_TRY(path.append(t, knst_json_detail::format_u64(i, t)));
            const bool ok = emit_op(out, "add", path, &b[i]);
            path.set_size(mark);
            KNST_JSON_TRY(ok);
        }
        return true;
    }
    return emit_op(out, "replace", path, &b);
}

// ---- walk / flatten ---------------------------------------------------------------------------
template <class F> bool knst_json::walk(F&& fn) const noexcept(false) {
    knst_json_buffer path;
    return walk_rec(fn, path, 0);
}
template <class F> bool knst_json::walk_rec(F& fn, knst_json_buffer& path, uint32_t depth) const {
    KNST_JSON_TRY(knst_json_detail::nul_term(path));
    if (!fn(*this, static_cast<const char*>(path.data()), path.size())) return false;
    if (!is_container()) return true;
    if (depth >= max_rec) return false;
    char t[24];
    if (type_ == type::array) {
        for (uint32_t i = 0; i < v_.a.n; ++i) {
            const size_t mark = path.size();
            KNST_JSON_TRY(path.push('/'));
            KNST_JSON_TRY(path.append(t, knst_json_detail::format_u64(i, t)));
            const bool ok = v_.a.p[i].walk_rec(fn, path, depth + 1);
            path.set_size(mark);
            KNST_JSON_TRY(ok);
        }
    } else if (v_.o) {
        for (uint32_t i = 0; i < v_.o->n; ++i) {
            const size_t mark = path.size();
            KNST_JSON_TRY(knst_json_detail::pointer_escape_append(path, v_.o->e[i].k.data(), v_.o->e[i].k.size()));
            const bool ok = v_.o->e[i].v.walk_rec(fn, path, depth + 1);
            path.set_size(mark);
            KNST_JSON_TRY(ok);
        }
    }
    return true;
}
inline bool knst_json::flatten_rec(knst_json& out, knst_json_buffer& path, uint32_t depth) const noexcept {
    if (is_container() && size() > 0) {
        if (depth >= max_rec) return false;
        char t[24];
        if (type_ == type::array) {
            for (uint32_t i = 0; i < v_.a.n; ++i) {
                const size_t mark = path.size();
                KNST_JSON_TRY(path.push('/'));
                KNST_JSON_TRY(path.append(t, knst_json_detail::format_u64(i, t)));
                const bool ok = v_.a.p[i].flatten_rec(out, path, depth + 1);
                path.set_size(mark);
                KNST_JSON_TRY(ok);
            }
        } else {
            for (uint32_t i = 0; i < v_.o->n; ++i) {
                const size_t mark = path.size();
                KNST_JSON_TRY(knst_json_detail::pointer_escape_append(path, v_.o->e[i].k.data(), v_.o->e[i].k.size()));
                const bool ok = v_.o->e[i].v.flatten_rec(out, path, depth + 1);
                path.set_size(mark);
                KNST_JSON_TRY(ok);
            }
        }
        return true;
    }
    return out.set(path.data(), path.size(), *this);
}
inline bool knst_json::flatten(knst_json& out) const noexcept {
    knst_json r = make_object();
    if (!r.is_object()) return false;
    knst_json_buffer path;
    KNST_JSON_TRY(flatten_rec(r, path, 0));
    out = std::move(r);
    return true;
}
inline bool knst_json::unflatten(const knst_json& flat, knst_json& out) noexcept {
    if (!flat.is_object()) return false;
    knst_json r;
    for (const member& m : flat.members()) {
        knst_json* s = r.pointer_slot(m.key(), m.key_size());
        if (!s || !s->copy_from(m.v)) return false;
    }
    out = std::move(r);
    return true;
}

// ============================================================================
//  Parser  (iterative: explicit frame stack, no recursion)
// ============================================================================
namespace knst_json_detail {

#define KNST_JSON_SINK_(call) \
    do { const knst_json_errc ec__ = (call); if (KNST_JSON_UNLIKELY(ec__ != knst_json_errc::none)) return fail(ec__); } while (0)

// Sink that builds a knst_json tree. Containers are tracked on an explicit stack; every
// pointer kept on it stays valid because a container is never appended to while one of
// its children is still open.
struct dom_sink {
    using E = knst_json_errc;
    using T = knst_json_type;
    knst_json& root;
    basic_buffer<knst_json*> st;
    knst_json* keyslot = nullptr;
    knst_json discard;                         // receives values of ignored duplicate keys
    knst_json_duplicate_keys dup;

    dom_sink(knst_json& r, knst_json_duplicate_keys d) noexcept : root(r), dup(d) {}

    knst_json* next() noexcept {
        if (st.empty()) return &root;
        knst_json* c = st.data()[st.size() - 1];
        if (c->type_ == T::array) return c->arr_push_slot();
        knst_json* s = keyslot; keyslot = nullptr;
        return s;
    }
    E null_value() noexcept { return next() ? E::none : E::out_of_memory; }
    E boolean(bool b) noexcept { knst_json* s = next(); if (!s) return E::out_of_memory; s->type_ = T::boolean; s->v_.u = 0; s->v_.b = b; return E::none; }
    E integer(int64_t v) noexcept { knst_json* s = next(); if (!s) return E::out_of_memory; s->type_ = T::integer; s->v_.i = v; return E::none; }
    E uinteger(uint64_t v) noexcept { knst_json* s = next(); if (!s) return E::out_of_memory; s->type_ = T::unsigned_integer; s->v_.u = v; return E::none; }
    E floating(double v) noexcept { knst_json* s = next(); if (!s) return E::out_of_memory; s->type_ = T::floating; s->v_.d = v; return E::none; }
    E string(str& v) noexcept { knst_json* s = next(); if (!s) return E::out_of_memory; s->type_ = T::string; s->v_.s = v; v.init(); return E::none; }
    E key(str& k) noexcept {
        knst_json* c = st.data()[st.size() - 1];
        bool ins;
        knst_json_member* m = c->obj_emplace(k.data(), k.size(), &k, ins);
        if (!m) return E::out_of_memory;
        if (ins) { keyslot = &m->v; return E::none; }
        switch (dup) {
            case knst_json_duplicate_keys::error: return E::duplicate_key;
            case knst_json_duplicate_keys::last_wins: m->v.destroy(); keyslot = &m->v; return E::none;
            default: discard.destroy(); keyslot = &discard; return E::none;
        }
    }
    E start_object() noexcept {
        knst_json* s = next();
        if (!s || !s->obj_make()) return E::out_of_memory;
        return st.push(s) ? E::none : E::out_of_memory;
    }
    E start_array() noexcept {
        knst_json* s = next();
        if (!s) return E::out_of_memory;
        s->type_ = T::array; s->v_.a.p = nullptr; s->v_.a.n = s->v_.a.c = 0;
        return st.push(s) ? E::none : E::out_of_memory;
    }
    E end_array() noexcept {      // trim wasted capacity (cheap, in-place for most allocators)
        knst_json* c = st.data()[st.size() - 1];
        knst_json::arr_t& a = c->v_.a;
        if (a.c != a.n && (a.c - a.n) > (a.n >> 2) && a.n) {
            void* np = KNST_JSON_REALLOC(static_cast<void*>(a.p), static_cast<size_t>(a.n) * sizeof(knst_json));
            if (np) { a.p = static_cast<knst_json*>(np); a.c = a.n; }
        }
        st.set_size(st.size() - 1);
        return E::none;
    }
    E end_object() noexcept {
        knst_json* c = st.data()[st.size() - 1];
        obj* o = c->v_.o;
        if (o && o->c != o->n && (o->c - o->n) > (o->n >> 2) && o->n) {
            void* np = KNST_JSON_REALLOC(static_cast<void*>(o->e), static_cast<size_t>(o->n) * sizeof(knst_json_member));
            if (np) { o->e = static_cast<knst_json_member*>(np); o->c = o->n; }
        }
        st.set_size(st.size() - 1);
        return E::none;
    }
};

// Adapter: forwards parser events to a user SAX handler.
template <class H> struct sax_sink {
    using E = knst_json_errc;
    H& h;
    static E ok(bool b) noexcept { return b ? E::none : E::handler_aborted; }
    E null_value() { return ok(h.null_value()); }
    E boolean(bool b) { return ok(h.boolean(b)); }
    E integer(int64_t v) { return ok(h.integer(v)); }
    E uinteger(uint64_t v) { return ok(h.uinteger(v)); }
    E floating(double v) { return ok(h.number_float(v)); }
    E string(str& s) { return ok(h.string(s.data(), s.size())); }
    E key(str& s) { return ok(h.key(s.data(), s.size())); }
    E start_object() { return ok(h.start_object()); }
    E end_object() { return ok(h.end_object()); }
    E start_array() { return ok(h.start_array()); }
    E end_array() { return ok(h.end_array()); }
};

template <class Sink>
class basic_parser {
public:
    basic_parser(const unsigned char* b, size_t n, const knst_json_parse_options& o, Sink& s) noexcept
        : b_(b), p_(b), e_(b + n), opt_(o), sink_(s) {}

    // require_eof=false: stop after the first value (parse_prefix)
    bool run(bool require_eof, size_t* consumed) noexcept {
        if (static_cast<size_t>(e_ - b_) > opt_.max_input_size) return fail(knst_json_errc::input_too_large);
        if (opt_.allow_bom && e_ - p_ >= 3 && p_[0] == 0xEF && p_[1] == 0xBB && p_[2] == 0xBF) p_ += 3;
        KNST_JSON_TRY(skip_ws());
        if (p_ == e_) return fail(knst_json_errc::empty_input);
        KNST_JSON_TRY(document());
        if (require_eof) {
            KNST_JSON_TRY(skip_ws());
            if (p_ != e_) return fail(knst_json_errc::trailing_characters);
        } else {
            const unsigned char* keep = p_;
            if (!skip_ws()) { p_ = keep; ec_ = knst_json_errc::none; }
        }
        if (consumed) *consumed = static_cast<size_t>(p_ - b_);
        return true;
    }

    void fill(knst_json_error* err) const noexcept {
        if (!err) return;
        *err = knst_json_error();
        err->code = ec_;
        err->offset = static_cast<size_t>(epos_ - b_);
        uint32_t line = 1;
        const unsigned char* ls = b_;
        for (const unsigned char* q = b_; q < epos_; ++q) if (*q == '\n') { ++line; ls = q + 1; }
        err->line = line;
        err->column = static_cast<uint32_t>(epos_ - ls) + 1;
    }

private:
    struct frame { uint32_t n; bool obj; };
    const unsigned char* b_;
    const unsigned char* p_;
    const unsigned char* e_;
    const unsigned char* epos_ = nullptr;
    const knst_json_parse_options& opt_;
    Sink& sink_;
    basic_buffer<frame> st_;
    knst_json_errc ec_ = knst_json_errc::none;

    bool fail(knst_json_errc c) noexcept { if (ec_ == knst_json_errc::none) { ec_ = c; epos_ = p_; } return false; }

    bool skip_ws() noexcept {
        for (;;) {
            while (p_ < e_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\r' || *p_ == '\t')) ++p_;
            if (!opt_.allow_comments || p_ >= e_ || *p_ != '/') return true;
            if (p_ + 1 >= e_) return fail(knst_json_errc::unexpected_end);
            if (p_[1] == '/') { p_ += 2; while (p_ < e_ && *p_ != '\n') ++p_; }
            else if (p_[1] == '*') {
                p_ += 2;
                for (;;) {
                    if (p_ + 1 >= e_) { p_ = e_; return fail(knst_json_errc::unexpected_end); }
                    if (p_[0] == '*' && p_[1] == '/') { p_ += 2; break; }
                    ++p_;
                }
            } else return fail(knst_json_errc::unexpected_char);
        }
    }

    bool literal(const char* lit, size_t n) noexcept {
        if (static_cast<size_t>(e_ - p_) < n || std::memcmp(p_, lit, n) != 0) return fail(knst_json_errc::invalid_literal);
        p_ += n;
        return true;
    }

    // ---- structure -------------------------------------------------------------
    bool close() noexcept {
        const bool obj = st_.data()[st_.size() - 1].obj;
        st_.set_size(st_.size() - 1);
        KNST_JSON_SINK_(obj ? sink_.end_object() : sink_.end_array());
        return true;
    }
    bool array_entry() noexcept {
        frame& f = st_.data()[st_.size() - 1];
        if (f.n >= opt_.max_container_size) return fail(knst_json_errc::container_too_large);
        ++f.n;
        return true;
    }
    static bool ident_start(unsigned char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; }
    bool object_entry() noexcept {
        frame& f = st_.data()[st_.size() - 1];
        if (f.n >= opt_.max_container_size) return fail(knst_json_errc::container_too_large);
        ++f.n;
        if (p_ >= e_) return fail(knst_json_errc::unexpected_end);
        str_guard k;
        const unsigned char c = *p_;
        if (c == '"') { KNST_JSON_TRY(parse_string(k.s, '"')); }
        else if (c == '\'' && opt_.allow_single_quotes) { KNST_JSON_TRY(parse_string(k.s, '\'')); }
        else if (opt_.allow_unquoted_keys && ident_start(c)) {
            const unsigned char* s = p_;
            while (p_ < e_ && (ident_start(*p_) || (*p_ >= '0' && *p_ <= '9'))) ++p_;
            KNST_JSON_TRY(app(k.s, s, static_cast<size_t>(p_ - s)));
        } else return fail(knst_json_errc::unexpected_char);
        KNST_JSON_TRY(skip_ws());
        if (p_ >= e_) return fail(knst_json_errc::unexpected_end);
        if (*p_ != ':') return fail(knst_json_errc::unexpected_char);
        ++p_;
        KNST_JSON_TRY(skip_ws());
        KNST_JSON_SINK_(sink_.key(k.s));
        return true;
    }

    bool document() noexcept {
        st_.clear();
        for (;;) {
            // ---------- one value ----------
            if (p_ >= e_) return fail(knst_json_errc::unexpected_end);
            switch (*p_) {
                case '{':
                    if (st_.size() >= opt_.max_depth) return fail(knst_json_errc::max_depth_exceeded);
                    ++p_;
                    KNST_JSON_SINK_(sink_.start_object());
                    if (!st_.push(frame{ 0, true })) return fail(knst_json_errc::out_of_memory);
                    KNST_JSON_TRY(skip_ws());
                    if (p_ >= e_) return fail(knst_json_errc::unexpected_end);
                    if (*p_ == '}') { ++p_; KNST_JSON_TRY(close()); break; }
                    KNST_JSON_TRY(object_entry());
                    continue;
                case '[':
                    if (st_.size() >= opt_.max_depth) return fail(knst_json_errc::max_depth_exceeded);
                    ++p_;
                    KNST_JSON_SINK_(sink_.start_array());
                    if (!st_.push(frame{ 0, false })) return fail(knst_json_errc::out_of_memory);
                    KNST_JSON_TRY(skip_ws());
                    if (p_ >= e_) return fail(knst_json_errc::unexpected_end);
                    if (*p_ == ']') { ++p_; KNST_JSON_TRY(close()); break; }
                    KNST_JSON_TRY(array_entry());
                    continue;
                default:
                    KNST_JSON_TRY(scalar());
                    break;
            }
            // ---------- after a value: close containers / find the next element ----------
            for (;;) {
                if (st_.empty()) return true;
                KNST_JSON_TRY(skip_ws());
                if (p_ >= e_) return fail(knst_json_errc::unexpected_end);
                const bool obj = st_.data()[st_.size() - 1].obj;
                const unsigned char c = *p_;
                if (c == ',') {
                    ++p_;
                    KNST_JSON_TRY(skip_ws());
                    if (opt_.allow_trailing_commas && p_ < e_ && *p_ == (obj ? '}' : ']')) { ++p_; KNST_JSON_TRY(close()); continue; }
                    if (obj) { KNST_JSON_TRY(object_entry()); } else { KNST_JSON_TRY(array_entry()); }
                    break;      // parse the next value
                }
                if (c == (obj ? '}' : ']')) { ++p_; KNST_JSON_TRY(close()); continue; }
                return fail(knst_json_errc::unexpected_char);
            }
        }
    }

    bool scalar() noexcept {
        switch (*p_) {
            case '"': return string_value('"');
            case '\'':
                if (opt_.allow_single_quotes) return string_value('\'');
                return fail(knst_json_errc::unexpected_char);
            case 't': KNST_JSON_TRY(literal("true", 4)); KNST_JSON_SINK_(sink_.boolean(true)); return true;
            case 'f': KNST_JSON_TRY(literal("false", 5)); KNST_JSON_SINK_(sink_.boolean(false)); return true;
            case 'n': KNST_JSON_TRY(literal("null", 4)); KNST_JSON_SINK_(sink_.null_value()); return true;
            case 'N':
                if (!opt_.allow_nan_inf) return fail(knst_json_errc::unexpected_char);
                KNST_JSON_TRY(literal("NaN", 3)); KNST_JSON_SINK_(sink_.floating(std::numeric_limits<double>::quiet_NaN())); return true;
            case 'I':
                if (!opt_.allow_nan_inf) return fail(knst_json_errc::unexpected_char);
                KNST_JSON_TRY(literal("Infinity", 8)); KNST_JSON_SINK_(sink_.floating(std::numeric_limits<double>::infinity())); return true;
            case '-': case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                return number();
            default: return fail(knst_json_errc::unexpected_char);
        }
    }
    bool string_value(unsigned char q) noexcept {
        str_guard g;
        KNST_JSON_TRY(parse_string(g.s, q));
        KNST_JSON_SINK_(sink_.string(g.s));
        return true;
    }

    // ---- strings ---------------------------------------------------------------
    bool app(str& out, const unsigned char* s, size_t n) noexcept {
        if (n == 0) return true;
        if (static_cast<uint64_t>(out.size()) + n > opt_.max_string_length) return fail(knst_json_errc::string_too_long);
        if (!out.append(reinterpret_cast<const char*>(s), n)) return fail(knst_json_errc::out_of_memory);
        return true;
    }
    bool hex4(uint32_t& v) noexcept {
        if (e_ - p_ < 4) { p_ = e_; return fail(knst_json_errc::unexpected_end); }
        uint32_t r = 0;
        for (int i = 0; i < 4; ++i) {
            const unsigned char c = p_[i];
            uint32_t d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else { p_ += i; return fail(knst_json_errc::invalid_unicode_escape); }
            r = (r << 4) | d;
        }
        p_ += 4; v = r;
        return true;
    }
    bool parse_escape(str& out, unsigned char q) noexcept {   // p_ points just after the backslash
        if (p_ >= e_) return fail(knst_json_errc::unexpected_end);
        char c;
        switch (*p_) {
            case '"': c = '"'; break;   case '\\': c = '\\'; break;  case '/': c = '/'; break;
            case 'b': c = '\b'; break;  case 'f': c = '\f'; break;   case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;  case 't': c = '\t'; break;
            case '\'':
                if (q != '\'') return fail(knst_json_errc::invalid_escape);
                c = '\''; break;
            case 'u': {
                ++p_;
                uint32_t cp;
                KNST_JSON_TRY(hex4(cp));
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (e_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                        const unsigned char* save = p_;
                        p_ += 2;
                        uint32_t lo;
                        KNST_JSON_TRY(hex4(lo));
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else if (opt_.replace_lone_surrogates) { p_ = save; cp = 0xFFFD; }
                        else return fail(knst_json_errc::lone_surrogate);
                    } else if (opt_.replace_lone_surrogates) cp = 0xFFFD;
                    else return fail(knst_json_errc::lone_surrogate);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    if (!opt_.replace_lone_surrogates) return fail(knst_json_errc::lone_surrogate);
                    cp = 0xFFFD;
                }
                char tmp[4];
                const size_t l = utf8_encode(cp, tmp);
                return app(out, reinterpret_cast<const unsigned char*>(tmp), l);
            }
            default: return fail(knst_json_errc::invalid_escape);
        }
        ++p_;
        return app(out, reinterpret_cast<const unsigned char*>(&c), 1);
    }
    bool parse_string(str& out, unsigned char q) noexcept {   // p_ at the opening quote
        ++p_;
        const unsigned char* run = p_;
        for (;;) {
            p_ = (q == '"') ? scan_plain(p_, e_) : scan_plain_sq(p_, e_);
            if (p_ >= e_) return fail(knst_json_errc::unexpected_end);
            const unsigned char c = *p_;
            if (c == q) { KNST_JSON_TRY(app(out, run, static_cast<size_t>(p_ - run))); ++p_; return true; }
            if (c == '\\') {
                KNST_JSON_TRY(app(out, run, static_cast<size_t>(p_ - run)));
                ++p_;
                KNST_JSON_TRY(parse_escape(out, q));
                run = p_;
                continue;
            }
            if (c < 0x20) return fail(knst_json_errc::control_char_in_string);
            if (c >= 0x80) {
                if (opt_.validate_utf8) {
                    do {
                        const size_t l = utf8_len(p_, e_);
                        if (!l) return fail(knst_json_errc::invalid_utf8);
                        p_ += l;
                    } while (p_ < e_ && *p_ >= 0x80);
                } else {
                    while (p_ < e_ && *p_ >= 0x80) ++p_;
                }
                continue;
            }
            ++p_;   // plain ASCII byte the scanner stopped on
        }
    }

    // ---- numbers ---------------------------------------------------------------
    static KNST_JSON_FI bool mul10_fits(uint64_t m, unsigned d) noexcept {
        return m < 1844674407370955161ULL || (m == 1844674407370955161ULL && d <= 5);
    }
    bool number() noexcept {
        static const double pow10[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                        1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
        const unsigned char* s = p_;
        bool neg = false;
        if (*p_ == '-') {
            neg = true; ++p_;
            if (p_ < e_ && *p_ == 'I' && opt_.allow_nan_inf) {
                KNST_JSON_TRY(literal("Infinity", 8));
                KNST_JSON_SINK_(sink_.floating(-std::numeric_limits<double>::infinity()));
                return true;
            }
        }
        if (p_ >= e_) return fail(knst_json_errc::invalid_number);
        uint64_t mant = 0;
        bool overflow = false;
        int64_t e10 = 0;
        if (*p_ == '0') {
            ++p_;
            if (p_ < e_ && *p_ >= '0' && *p_ <= '9') return fail(knst_json_errc::invalid_number);
        } else if (*p_ >= '1' && *p_ <= '9') {
            while (p_ < e_ && *p_ >= '0' && *p_ <= '9') {
                const unsigned d = *p_ - '0';
                if (!overflow && mul10_fits(mant, d)) mant = mant * 10 + d; else { overflow = true; ++e10; }
                ++p_;
            }
        } else return fail(knst_json_errc::invalid_number);

        bool is_float = false;
        if (p_ < e_ && *p_ == '.') {
            is_float = true; ++p_;
            if (p_ >= e_ || *p_ < '0' || *p_ > '9') return fail(knst_json_errc::invalid_number);
            while (p_ < e_ && *p_ >= '0' && *p_ <= '9') {
                const unsigned d = *p_ - '0';
                if (!overflow && mul10_fits(mant, d)) { mant = mant * 10 + d; --e10; } else overflow = true;
                ++p_;
            }
        }
        if (p_ < e_ && (*p_ == 'e' || *p_ == 'E')) {
            is_float = true; ++p_;
            bool eneg = false;
            if (p_ < e_ && (*p_ == '+' || *p_ == '-')) { eneg = (*p_ == '-'); ++p_; }
            if (p_ >= e_ || *p_ < '0' || *p_ > '9') return fail(knst_json_errc::invalid_number);
            int64_t ev = 0;
            while (p_ < e_ && *p_ >= '0' && *p_ <= '9') { if (ev < 100000000) ev = ev * 10 + (*p_ - '0'); ++p_; }
            e10 += eneg ? -ev : ev;
        }
        if (p_ < e_ && ((*p_ >= '0' && *p_ <= '9') || *p_ == '.')) return fail(knst_json_errc::invalid_number);

        if (!is_float && !overflow) {
            if (!neg) {
                if (mant <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) KNST_JSON_SINK_(sink_.integer(static_cast<int64_t>(mant)));
                else KNST_JSON_SINK_(sink_.uinteger(mant));
                return true;
            }
            if (mant == 0) { KNST_JSON_SINK_(sink_.floating(-0.0)); return true; }
            if (mant <= 0x8000000000000000ULL) {
                KNST_JSON_SINK_(sink_.integer(mant == 0x8000000000000000ULL ? (std::numeric_limits<int64_t>::min)() : -static_cast<int64_t>(mant)));
                return true;
            }
            // magnitude beyond int64: falls through to double
        }
        double d;
        if (!overflow && mant == 0) d = 0.0;
        else if (!overflow && mant <= (1ULL << 53) && e10 >= -22 && e10 <= 22) {
            d = static_cast<double>(mant);
            d = e10 < 0 ? d / pow10[-e10] : d * pow10[e10];
        } else if (!parse_double_text(reinterpret_cast<const char*>(s) + (neg ? 1 : 0), static_cast<size_t>(p_ - s) - (neg ? 1 : 0), d)) {
            return fail(knst_json_errc::invalid_number);
        }
        if (neg) d = -d;
        if (!std::isfinite(d) && !opt_.allow_nan_inf) return fail(knst_json_errc::number_out_of_range);
        KNST_JSON_SINK_(sink_.floating(d));
        return true;
    }
};

#undef KNST_JSON_SINK_

} // namespace knst_json_detail

// ---- parse entry points ------------------------------------------------------------------
inline bool knst_json::parse_impl(const char* data, size_t size, knst_json& out, size_t* consumed,
                                  knst_json_error* err, const knst_json_parse_options& opt) noexcept {
    if (!data && size) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    if (err) *err = knst_json_error();
    static const char empty_doc[1] = { 0 };
    knst_json tmp;
    knst_json_detail::dom_sink sink(tmp, opt.duplicate_keys);
    knst_json_detail::basic_parser<knst_json_detail::dom_sink> ps(
        reinterpret_cast<const unsigned char*>(data ? data : empty_doc), size, opt, sink);
    if (!ps.run(consumed == nullptr, consumed)) { ps.fill(err); return false; }
    out = std::move(tmp);
    return true;
}
inline bool knst_json::parse(const char* data, size_t size, knst_json& out, knst_json_error* err,
                             const knst_json_parse_options& opt) noexcept {
    return parse_impl(data, size, out, nullptr, err, opt);
}
inline bool knst_json::parse_prefix(const char* data, size_t size, knst_json& out, size_t& consumed,
                                    knst_json_error* err, const knst_json_parse_options& opt) noexcept {
    consumed = 0;
    return parse_impl(data, size, out, &consumed, err, opt);
}
template <class Handler>
inline bool knst_json::parse_sax(const char* data, size_t size, Handler& handler, knst_json_error* err,
                                 const knst_json_parse_options& opt) noexcept {
    if (!data && size) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    if (err) *err = knst_json_error();
    static const char empty_doc[1] = { 0 };
    knst_json_detail::sax_sink<Handler> sink{ handler };
    knst_json_detail::basic_parser<knst_json_detail::sax_sink<Handler>> ps(
        reinterpret_cast<const unsigned char*>(data ? data : empty_doc), size, opt, sink);
    if (ps.run(true, nullptr)) return true;
    ps.fill(err);
    return false;
}
template <class Callback>
inline bool knst_json::parse_lines(const char* data, size_t size, Callback&& cb, knst_json_error* err,
                                   const knst_json_parse_options& opt) noexcept {
    if (!data && size) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    if (err) *err = knst_json_error();
    size_t pos = 0, index = 0;
    uint32_t line_no = 0;
    while (pos < size) {
        const void* nlp = std::memchr(data + pos, '\n', size - pos);
        const size_t end = nlp ? static_cast<size_t>(static_cast<const char*>(nlp) - data) : size;
        ++line_no;
        size_t s = pos, e = end;
        while (s < e && (data[s] == ' ' || data[s] == '\t' || data[s] == '\r')) ++s;
        while (e > s && (data[e - 1] == ' ' || data[e - 1] == '\t' || data[e - 1] == '\r')) --e;
        if (s < e) {
            knst_json v;
            knst_json_error le;
            if (!parse(data + s, e - s, v, &le, opt)) {
                if (err) { *err = le; err->offset += s; err->line = line_no; err->column += static_cast<uint32_t>(s - pos); }
                return false;
            }
            if (!cb(index++, std::move(v))) return true;
        }
        pos = end + 1;
    }
    return true;
}

// ============================================================================
//  Writer  (iterative)
// ============================================================================
namespace knst_json_detail {

class writer {
public:
    using flush_fn = bool (*)(void*, const char*, size_t);
    writer(const knst_json_dump_options& o, knst_json_buffer& b, flush_fn fl = nullptr, void* ctx = nullptr) noexcept
        : o_(o), b_(b), fl_(fl), ctx_(ctx) {
        if (o_.canonical) {          // RFC 8785: no whitespace, UTF-16 key order, raw UTF-8, ES number format
            o_.indent = 0; o_.sort_keys = true; o_.ensure_ascii = false; o_.escape_slash = false; o_.escape_html = false;
            o_.trailing_newline = false;
            o_.invalid_utf8 = knst_json_invalid_utf8::error; o_.non_finite = knst_json_non_finite::error;
        }
    }
    ~writer() { for (size_t i = 0; i < st_.size(); ++i) if (st_.data()[i].sorted) KNST_JSON_FREE(st_.data()[i].sorted); }
    knst_json_errc ec = knst_json_errc::none;

    bool run(const knst_json& root) noexcept {
        using T = knst_json_type;
        const knst_json* cur = &root;
        for (;;) {
            switch (cur->type_) {
                case T::null_value: KNST_JSON_TRY(put("null", 4)); break;
                case T::boolean: if (cur->v_.b) { KNST_JSON_TRY(put("true", 4)); } else { KNST_JSON_TRY(put("false", 5)); } break;
                case T::integer: {
                    char t[48];
                    KNST_JSON_TRY(put(t, o_.canonical ? format_double_jcs(static_cast<double>(cur->v_.i), t) : format_i64(cur->v_.i, t)));
                    break;
                }
                case T::unsigned_integer: {
                    char t[48];
                    KNST_JSON_TRY(put(t, o_.canonical ? format_double_jcs(static_cast<double>(cur->v_.u), t) : format_u64(cur->v_.u, t)));
                    break;
                }
                case T::floating: KNST_JSON_TRY(write_double(cur->v_.d)); break;
                case T::string: KNST_JSON_TRY(write_string(cur->v_.s.data(), cur->v_.s.size())); break;
                case T::array: {
                    if (st_.size() >= o_.max_depth) return fail(knst_json_errc::max_depth_exceeded);
                    const uint32_t n = cur->v_.a.n;
                    if (n == 0) { KNST_JSON_TRY(put("[]", 2)); break; }
                    KNST_JSON_TRY(put('['));
                    if (!st_.push(frame{ cur, nullptr, 0, n, false })) return fail(knst_json_errc::out_of_memory);
                    break;
                }
                case T::object: {
                    if (st_.size() >= o_.max_depth) return fail(knst_json_errc::max_depth_exceeded);
                    const uint32_t n = cur->v_.o ? cur->v_.o->n : 0;
                    if (n == 0) { KNST_JSON_TRY(put("{}", 2)); break; }
                    KNST_JSON_TRY(put('{'));
                    const knst_json_member** sorted = nullptr;
                    if (o_.sort_keys) {
                        sorted = static_cast<const knst_json_member**>(KNST_JSON_MALLOC(sizeof(void*) * n));
                        if (!sorted) return fail(knst_json_errc::out_of_memory);
                        for (uint32_t i = 0; i < n; ++i) sorted[i] = cur->v_.o->e + i;
                        if (o_.canonical)
                            std::sort(sorted, sorted + n, [](const knst_json_member* a, const knst_json_member* b) noexcept {
                                return cmp_utf16_order(a->k.data(), a->k.size(), b->k.data(), b->k.size()) < 0; });
                        else
                            std::sort(sorted, sorted + n, [](const knst_json_member* a, const knst_json_member* b) noexcept {
                                return cmp_bytes(a->k.data(), a->k.size(), b->k.data(), b->k.size()) < 0; });
                    }
                    if (!st_.push(frame{ cur, sorted, 0, n, true })) { if (sorted) KNST_JSON_FREE(sorted); return fail(knst_json_errc::out_of_memory); }
                    break;
                }
            }
            // ---- advance to the next value to emit ----
            for (;;) {
                if (st_.empty()) return true;
                frame& f = st_.data()[st_.size() - 1];
                if (f.i == f.n) {
                    KNST_JSON_TRY(nl(st_.size() - 1));
                    KNST_JSON_TRY(put(f.obj ? '}' : ']'));
                    if (f.sorted) KNST_JSON_FREE(f.sorted);
                    st_.set_size(st_.size() - 1);
                    continue;
                }
                if (f.i) KNST_JSON_TRY(put(','));
                KNST_JSON_TRY(nl(st_.size()));
                if (f.obj) {
                    const knst_json_member* m = f.sorted ? f.sorted[f.i] : f.v->v_.o->e + f.i;
                    KNST_JSON_TRY(write_string(m->k.data(), m->k.size()));
                    KNST_JSON_TRY(put(':'));
                    if (o_.indent) KNST_JSON_TRY(put(' '));
                    cur = &m->v;
                } else cur = f.v->v_.a.p + f.i;
                ++f.i;
                break;
            }
            if (fl_ && b_.size() >= 65536) KNST_JSON_TRY(flush());
        }
    }
    bool flush() noexcept {
        if (!fl_ || b_.empty()) return true;
        if (!fl_(ctx_, b_.data(), b_.size())) return fail(knst_json_errc::io_error);
        b_.clear();
        return true;
    }
    bool fail(knst_json_errc c) noexcept { if (ec == knst_json_errc::none) ec = c; return false; }

private:
    struct frame { const knst_json* v; const knst_json_member** sorted; uint32_t i, n; bool obj; };
    knst_json_dump_options o_;
    knst_json_buffer& b_;
    flush_fn fl_;
    void* ctx_;
    basic_buffer<frame> st_;

    KNST_JSON_FI bool put(char c) noexcept { if (!b_.push(c)) return fail(knst_json_errc::out_of_memory); return true; }
    KNST_JSON_FI bool put(const char* s, size_t n) noexcept { if (!b_.append(s, n)) return fail(knst_json_errc::out_of_memory); return true; }
    bool nl(size_t depth) noexcept {
        if (!o_.indent) return true;
        KNST_JSON_TRY(put('\n'));
        char fill[64];
        std::memset(fill, o_.indent_char, sizeof(fill));
        size_t total = static_cast<size_t>(o_.indent) * depth;
        while (total) {
            const size_t k = total < sizeof(fill) ? total : sizeof(fill);
            KNST_JSON_TRY(put(fill, k));
            total -= k;
        }
        return true;
    }
    bool write_double(double d) noexcept {
        if (!std::isfinite(d)) {
            switch (o_.non_finite) {
                case knst_json_non_finite::error: return fail(knst_json_errc::non_finite_number);
                case knst_json_non_finite::null_value: return put("null", 4);
                case knst_json_non_finite::literal:
                    if (std::isnan(d)) return put("NaN", 3);
                    return d < 0 ? put("-Infinity", 9) : put("Infinity", 8);
            }
        }
        char t[48];
        return put(t, o_.canonical ? format_double_jcs(d, t) : format_double(d, t));
    }
    bool write_u_escape(uint32_t cu) noexcept {
        static const char hex[] = "0123456789abcdef";
        char t[6] = { '\\', 'u', hex[(cu >> 12) & 15], hex[(cu >> 8) & 15], hex[(cu >> 4) & 15], hex[cu & 15] };
        return put(t, 6);
    }
    bool write_string(const char* s, size_t n) noexcept {
        KNST_JSON_TRY(put('"'));
        const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
        const unsigned char* e = p + n;
        const unsigned char* run = p;
        const bool special_ascii = o_.escape_slash || o_.escape_html;
        const bool pass_high = !o_.ensure_ascii && !o_.escape_html && o_.invalid_utf8 == knst_json_invalid_utf8::ignore;
        auto flush_run = [&]() noexcept { return put(reinterpret_cast<const char*>(run), static_cast<size_t>(p - run)); };
        while (p < e) {
            if (!special_ascii) p = scan_plain(p, e);
            else {
                while (p < e && *p >= 0x20 && *p < 0x80 && *p != '"' && *p != '\\' &&
                       !(o_.escape_slash && *p == '/') &&
                       !(o_.escape_html && (*p == '<' || *p == '>' || *p == '&' || *p == '\''))) ++p;
            }
            if (p >= e) break;
            const unsigned char c = *p;
            if (c >= 0x80) {
                if (pass_high) { ++p; while (p < e && *p >= 0x80) ++p; continue; }
                const size_t l = utf8_len(p, e);
                if (l == 0) {
                    KNST_JSON_TRY(flush_run());
                    if (o_.invalid_utf8 == knst_json_invalid_utf8::error) return fail(knst_json_errc::invalid_utf8);
                    if (o_.invalid_utf8 == knst_json_invalid_utf8::replace) {
                        if (o_.ensure_ascii) { KNST_JSON_TRY(write_u_escape(0xFFFD)); } else { KNST_JSON_TRY(put("\xEF\xBF\xBD", 3)); }
                    }
                    ++p; run = p;
                    continue;
                }
                if (!o_.ensure_ascii) {
                    if (o_.escape_html && l == 3 && p[0] == 0xE2 && p[1] == 0x80 && (p[2] & 0xFE) == 0xA8) {   // U+2028 / U+2029
                        KNST_JSON_TRY(flush_run());
                        KNST_JSON_TRY(write_u_escape(0x2028u + (p[2] & 1u)));
                        p += 3; run = p;
                        continue;
                    }
                    p += l;
                    continue;
                }
                KNST_JSON_TRY(flush_run());
                uint32_t cp = utf8_decode(p, l);
                if (cp >= 0x10000) {
                    cp -= 0x10000;
                    KNST_JSON_TRY(write_u_escape(0xD800 + (cp >> 10)));
                    KNST_JSON_TRY(write_u_escape(0xDC00 + (cp & 0x3FF)));
                } else KNST_JSON_TRY(write_u_escape(cp));
                p += l; run = p;
                continue;
            }
            KNST_JSON_TRY(flush_run());
            switch (c) {
                case '"': KNST_JSON_TRY(put("\\\"", 2)); break;
                case '\\': KNST_JSON_TRY(put("\\\\", 2)); break;
                case '\b': KNST_JSON_TRY(put("\\b", 2)); break;
                case '\f': KNST_JSON_TRY(put("\\f", 2)); break;
                case '\n': KNST_JSON_TRY(put("\\n", 2)); break;
                case '\r': KNST_JSON_TRY(put("\\r", 2)); break;
                case '\t': KNST_JSON_TRY(put("\\t", 2)); break;
                case '/': KNST_JSON_TRY(put("\\/", 2)); break;
                default: KNST_JSON_TRY(write_u_escape(c)); break;
            }
            ++p; run = p;
        }
        KNST_JSON_TRY(flush_run());
        return put('"');
    }
};

inline bool file_sink(void* ctx, const char* d, size_t n) noexcept {
    return std::fwrite(d, 1, n, static_cast<std::FILE*>(ctx)) == n;
}

} // namespace knst_json_detail

// ---- dump entry points -------------------------------------------------------------------
inline bool knst_json::dump_append(knst_json_buffer& out, const knst_json_dump_options& opt, knst_json_error* err) const noexcept {
    if (err) *err = knst_json_error();
    const size_t base = out.size();
    knst_json_detail::writer w(opt, out);
    bool ok = w.run(*this);
    if (ok && opt.trailing_newline && !opt.canonical) ok = out.push('\n') || w.fail(knst_json_errc::out_of_memory);
    if (!ok) { out.set_size(base); fill_err(err, w.ec); return false; }
    return true;
}
inline bool knst_json::dump(knst_json_buffer& out, const knst_json_dump_options& opt, knst_json_error* err) const noexcept {
    out.clear();
    return dump_append(out, opt, err);
}
inline bool knst_json::dump_lines(knst_json_buffer& out, const knst_json_dump_options& opt, knst_json_error* err) const noexcept {
    knst_json_dump_options o = opt;
    o.indent = 0; o.trailing_newline = false;
    out.clear();
    if (err) *err = knst_json_error();
    if (is_array()) {
        for (const knst_json& el : *this) {
            if (!el.dump_append(out, o, err) || !out.push('\n')) {
                if (err && err->code == knst_json_errc::none) fill_err(err, knst_json_errc::out_of_memory);
                out.clear();
                return false;
            }
        }
        return true;
    }
    if (!dump_append(out, o, err) || !out.push('\n')) {
        if (err && err->code == knst_json_errc::none) fill_err(err, knst_json_errc::out_of_memory);
        out.clear();
        return false;
    }
    return true;
}
inline bool knst_json::dump_stream(bool (*sink)(void*, const char*, size_t), void* ctx,
                                   const knst_json_dump_options& opt, knst_json_error* err) const noexcept {
    if (err) *err = knst_json_error();
    if (!sink) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    knst_json_buffer b;
    knst_json_detail::writer w(opt, b, sink, ctx);
    bool ok = w.run(*this);
    if (ok && opt.trailing_newline && !opt.canonical) ok = b.push('\n') || w.fail(knst_json_errc::out_of_memory);
    if (ok) ok = w.flush();
    if (!ok) { fill_err(err, w.ec); return false; }
    return true;
}
#if !defined(KNST_JSON_NO_FILE_IO)
inline bool knst_json::dump_file(std::FILE* f, const knst_json_dump_options& opt, knst_json_error* err) const noexcept {
    if (!f) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    return dump_stream(&knst_json_detail::file_sink, f, opt, err);
}
#endif

// ============================================================================
//  File I/O
// ============================================================================
#if !defined(KNST_JSON_NO_FILE_IO)
namespace knst_json_detail {

inline std::FILE* open_native(native_path& p, bool write) noexcept {
#if defined(_WIN32)
    return _wfopen(p.c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(p.c_str(), write ? "wb" : "rb");
#endif
}
inline void set_io_err(knst_json_error* err, knst_json_errc c) noexcept {
    if (err) { *err = knst_json_error(); err->code = c; }
}
// Reads the rest of an open file. `hint` (>0) pre-sizes the buffer.
inline knst_json_errc read_all(std::FILE* f, knst_json_buffer& buf, size_t max_size, long hint) noexcept {
    if (hint > 0) {
        if (static_cast<unsigned long>(hint) > max_size) return knst_json_errc::input_too_large;
        if (!buf.reserve(static_cast<size_t>(hint) + 1)) return knst_json_errc::out_of_memory;
    }
    for (;;) {
        if (buf.size() > max_size) return knst_json_errc::input_too_large;
        if (buf.capacity() - buf.size() < 4096 && !buf.reserve(buf.size() + 65536)) return knst_json_errc::out_of_memory;
        const size_t room = buf.capacity() - buf.size();
        const size_t got = std::fread(buf.data() + buf.size(), 1, room, f);
        buf.set_size(buf.size() + got);
        if (got < room) return std::ferror(f) ? knst_json_errc::io_error : knst_json_errc::none;
    }
}
inline bool load_native(native_path& path, knst_json& out, knst_json_error* err, const knst_json_parse_options& opt) noexcept {
    std::FILE* f = open_native(path, false);
    if (!f) { set_io_err(err, knst_json_errc::io_error); return false; }
    long sz = -1;
    if (std::fseek(f, 0, SEEK_END) == 0) { sz = std::ftell(f); if (std::fseek(f, 0, SEEK_SET) != 0) sz = -1; }
    knst_json_buffer buf;
    const knst_json_errc ec = read_all(f, buf, opt.max_input_size, sz);
    std::fclose(f);
    if (ec != knst_json_errc::none) { set_io_err(err, ec); return false; }
    return knst_json::parse(buf.data(), buf.size(), out, err, opt);
}
inline bool remove_native(native_path& p) noexcept {
#if defined(_WIN32)
    return ::DeleteFileW(p.c_str()) != 0;
#else
    return std::remove(p.c_str()) == 0;
#endif
}
inline bool save_native(native_path& path, const knst_json_buffer& data, bool atomic, knst_json_error* err) noexcept {
    native_path tmp;
    native_path* target = &path;
    if (atomic) {
        // unique sibling name: path + ".<pid><counter>.tmp~"
        static std::atomic<uint32_t> counter{ 0 };
#if defined(_WIN32)
        const unsigned long pid = static_cast<unsigned long>(::GetCurrentProcessId());
#else
        const unsigned long pid = static_cast<unsigned long>(::getpid());
#endif
        char suffix[48];
        std::snprintf(suffix, sizeof(suffix), ".%lx%x.tmp~", pid, static_cast<unsigned>(counter.fetch_add(1)));
        bool ok = tmp.append(path.data(), path.size());
        for (size_t i = 0; ok && suffix[i]; ++i) ok = tmp.push(static_cast<native_char>(suffix[i]));
        ok = ok && tmp.reserve(tmp.size() + 1);
        if (!ok) { set_io_err(err, knst_json_errc::out_of_memory); return false; }
        target = &tmp;
    }
    std::FILE* f = open_native(*target, true);
    if (!f) { set_io_err(err, knst_json_errc::io_error); return false; }
    bool ok = data.size() == 0 || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = ok && std::fflush(f) == 0;
    if (ok) {
#if defined(_WIN32)
        ok = _commit(_fileno(f)) == 0;
#else
        ok = ::fsync(::fileno(f)) == 0;
#endif
    }
    ok = (std::fclose(f) == 0) && ok;
    if (ok && atomic) {
#if defined(_WIN32)
        ok = ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        struct stat st;
        if (::stat(path.c_str(), &st) == 0) (void)::chmod(tmp.c_str(), st.st_mode & 07777);   // keep existing permissions
        ok = std::rename(tmp.c_str(), path.c_str()) == 0;
        if (ok) {                                                                              // make the rename itself durable
            size_t sl = path.size();
            while (sl > 0 && path.data()[sl - 1] != '/') --sl;
            knst_json_buffer dir;
            bool dok = sl == 0 ? dir.append(".", 1) : dir.append(path.data(), sl);
            if (dok) {
                const int fd = ::open(dir.c_str(), O_RDONLY);
                if (fd >= 0) { (void)::fsync(fd); ::close(fd); }
            }
        }
#endif
    }
    if (!ok) { if (atomic) remove_native(tmp); set_io_err(err, knst_json_errc::io_error); return false; }
    return true;
}

} // namespace knst_json_detail

inline bool knst_json::load_file_utf8(const char* path, knst_json& out, knst_json_error* err,
                                      const knst_json_parse_options& opt) noexcept {
    if (!path) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    knst_json_detail::native_path np;
    if (!knst_json_detail::native_from_utf8(path, std::strlen(path), np)) { fill_err(err, knst_json_errc::out_of_memory); return false; }
    return knst_json_detail::load_native(np, out, err, opt);
}
inline bool knst_json::load_file_u16(const char16_t* path, size_t n, knst_json& out, knst_json_error* err,
                                     const knst_json_parse_options& opt) noexcept {
    if (!path) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    knst_json_detail::native_path np;
    if (!knst_json_detail::native_from_u16(path, n, np)) { fill_err(err, knst_json_errc::out_of_memory); return false; }
    return knst_json_detail::load_native(np, out, err, opt);
}
inline bool knst_json::parse_stream(std::FILE* f, knst_json& out, knst_json_error* err, const knst_json_parse_options& opt) noexcept {
    if (!f) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    knst_json_buffer buf;
    const knst_json_errc ec = knst_json_detail::read_all(f, buf, opt.max_input_size, -1);
    if (ec != knst_json_errc::none) { knst_json_detail::set_io_err(err, ec); return false; }
    return parse(buf.data(), buf.size(), out, err, opt);
}
inline bool knst_json::save_file_utf8(const char* path, const knst_json_dump_options& opt, bool atomic, knst_json_error* err) const noexcept {
    if (!path) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    knst_json_buffer data;
    if (!dump(data, opt, err)) return false;
    knst_json_detail::native_path np;
    if (!knst_json_detail::native_from_utf8(path, std::strlen(path), np)) { fill_err(err, knst_json_errc::out_of_memory); return false; }
    return knst_json_detail::save_native(np, data, atomic, err);
}
inline bool knst_json::save_file_u16(const char16_t* path, size_t n, const knst_json_dump_options& opt, bool atomic, knst_json_error* err) const noexcept {
    if (!path) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    knst_json_buffer data;
    if (!dump(data, opt, err)) return false;
    knst_json_detail::native_path np;
    if (!knst_json_detail::native_from_u16(path, n, np)) { fill_err(err, knst_json_errc::out_of_memory); return false; }
    return knst_json_detail::save_native(np, data, atomic, err);
}
template <class Callback>
inline bool knst_json::load_lines_utf8(const char* path, Callback&& cb, knst_json_error* err, const knst_json_parse_options& opt) noexcept {
    if (!path) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    knst_json_detail::native_path np;
    if (!knst_json_detail::native_from_utf8(path, std::strlen(path), np)) { fill_err(err, knst_json_errc::out_of_memory); return false; }
    std::FILE* f = knst_json_detail::open_native(np, false);
    if (!f) { knst_json_detail::set_io_err(err, knst_json_errc::io_error); return false; }
    knst_json_buffer buf;
    size_t base_index = 0, base_off = 0;
    uint32_t base_line = 0;
    bool stopped = false, ok = true;
    knst_json_errc ioec = knst_json_errc::none;
    auto process = [&](size_t len) noexcept -> bool {      // parse buf[0, len) which ends at a line boundary
        knst_json_error le;
        size_t n = 0;
        const bool r = parse_lines(buf.data(), len, [&](size_t i, knst_json&& v) noexcept { ++n; const bool c = cb(base_index + i, std::move(v)); if (!c) stopped = true; return c; }, &le, opt);
        if (!r) { if (err) { *err = le; err->offset += base_off; err->line += base_line; } return false; }
        for (size_t i = 0; i < len; ++i) if (buf.data()[i] == '\n') ++base_line;
        base_index += n; base_off += len;
        return true;
    };
    for (;;) {
        if (!buf.reserve(buf.size() + 65536)) { ioec = knst_json_errc::out_of_memory; break; }
        const size_t got = std::fread(buf.data() + buf.size(), 1, 65536, f);
        buf.set_size(buf.size() + got);
        const bool eof = got < 65536;
        if (eof && std::ferror(f)) { ioec = knst_json_errc::io_error; break; }
        size_t cut = buf.size();
        if (!eof) { while (cut > 0 && buf.data()[cut - 1] != '\n') --cut; }
        if (cut) {
            if (!process(cut)) { ok = false; break; }
            if (stopped) break;
            std::memmove(buf.data(), buf.data() + cut, buf.size() - cut);
            buf.set_size(buf.size() - cut);
        }
        if (buf.size() > opt.max_input_size) { ioec = knst_json_errc::input_too_large; break; }
        if (eof) break;
    }
    std::fclose(f);
    if (!ok) return false;
    if (ioec != knst_json_errc::none) { knst_json_detail::set_io_err(err, ioec); return false; }
    return true;
}
#endif // KNST_JSON_NO_FILE_IO

// ============================================================================
//  JSONPath subset
// ============================================================================
namespace knst_json_detail {
namespace jp {

inline bool is_sp(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
inline void skip(const char*& p, const char* e) noexcept { while (p < e && is_sp(*p)) ++p; }
inline void trim(const char*& s, const char*& e) noexcept { skip(s, e); while (e > s && is_sp(e[-1])) --e; }

// 'quoted' or "quoted" string with simple escapes -> out; p ends after the closing quote
inline bool quoted(const char*& p, const char* e, knst_json_buffer& out) noexcept {
    const char q = *p++;
    while (p < e && *p != q) {
        char c = *p;
        if (c == '\\' && p + 1 < e) {
            ++p; c = *p;
            switch (c) { case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break; case 'b': c = '\b'; break; case 'f': c = '\f'; break; default: break; }
        }
        if (!out.push(c)) return false;
        ++p;
    }
    if (p >= e) return false;
    ++p;
    return true;
}
inline bool integer(const char*& p, const char* e, long long& v) noexcept {
    bool neg = false;
    if (p < e && *p == '-') { neg = true; ++p; }
    if (p >= e || *p < '0' || *p > '9') return false;
    long long r = 0;
    while (p < e && *p >= '0' && *p <= '9') { if (r < 100000000000000000LL) r = r * 10 + (*p - '0'); ++p; }
    v = neg ? -r : r;
    return true;
}
inline bool children(const knst_json* n, knst_json_ptr_list& out) noexcept {
    if (n->is_array()) { for (const knst_json& c : *n) KNST_JSON_TRY(out.push(&c)); }
    else if (n->is_object()) { for (const knst_json_member& m : n->members()) KNST_JSON_TRY(out.push(&m.v)); }
    return true;
}
// self + all descendants in document order
inline bool descendants(const knst_json* n, knst_json_ptr_list& out) noexcept {
    knst_json_ptr_list st;
    KNST_JSON_TRY(st.push(n));
    while (!st.empty()) {
        const knst_json* c = st.data()[st.size() - 1];
        st.set_size(st.size() - 1);
        KNST_JSON_TRY(out.push(c));
        if (c->is_array()) { for (size_t i = c->size(); i > 0; --i) KNST_JSON_TRY(st.push(c->at(i - 1))); }
        else if (c->is_object()) {
            const knst_json_member* b = c->members_begin();
            for (size_t i = c->size(); i > 0; --i) KNST_JSON_TRY(st.push(&b[i - 1].v));
        }
    }
    return true;
}
inline bool filter(const knst_json* node, const char* s, const char* e) noexcept {
    trim(s, e);
    if (e - s >= 2 && *s == '(' && e[-1] == ')') { ++s; --e; trim(s, e); }
    if (s >= e || *s != '@') return false;
    ++s;
    const knst_json* cur = node;
    while (s < e && cur && (*s == '.' || *s == '[')) {
        if (*s == '.') {
            ++s;
            const char* ns = s;
            while (s < e && !is_sp(*s) && *s != '.' && *s != '[' && *s != '=' && *s != '!' && *s != '<' && *s != '>') ++s;
            cur = cur->find(ns, static_cast<size_t>(s - ns));
        } else {
            ++s;
            skip(s, e);
            if (s >= e) return false;
            if (*s == '\'' || *s == '"') {
                knst_json_buffer k;
                if (!quoted(s, e, k)) return false;
                cur = cur->find(k.data() ? k.data() : "", k.size());
            } else {
                long long i;
                if (!integer(s, e, i)) return false;
                if (i < 0) i += static_cast<long long>(cur->size());
                cur = i >= 0 ? cur->at(static_cast<size_t>(i)) : nullptr;
            }
            skip(s, e);
            if (s >= e || *s != ']') return false;
            ++s;
        }
    }
    skip(s, e);
    if (s >= e) return cur != nullptr;          // existence test
    if (!cur) return false;
    int op;                                    // 0 == 1 != 2 < 3 <= 4 > 5 >=
    if (e - s >= 2 && s[0] == '=' && s[1] == '=') { op = 0; s += 2; }
    else if (e - s >= 2 && s[0] == '!' && s[1] == '=') { op = 1; s += 2; }
    else if (e - s >= 2 && s[0] == '<' && s[1] == '=') { op = 3; s += 2; }
    else if (e - s >= 2 && s[0] == '>' && s[1] == '=') { op = 5; s += 2; }
    else if (*s == '<') { op = 2; ++s; }
    else if (*s == '>') { op = 4; ++s; }
    else return false;
    trim(s, e);
    knst_json lit;
    knst_json_parse_options po;
    po.allow_single_quotes = true;
    if (!knst_json::parse(s, static_cast<size_t>(e - s), lit, nullptr, po)) return false;
    if (op == 0) return *cur == lit;
    if (op == 1) return !(*cur == lit);
    int c;
    if (cur->is_number() && lit.is_number()) {
        if ((cur->is_float() && std::isnan(cur->value_or(0.0))) || (lit.is_float() && std::isnan(lit.value_or(0.0)))) return false;
        c = knst_json::compare_numbers(*cur, lit);
    } else if (cur->is_string() && lit.is_string()) c = cmp_bytes(cur->c_str(), cur->length(), lit.c_str(), lit.length());
    else return false;
    switch (op) { case 2: return c < 0; case 3: return c <= 0; case 4: return c > 0; default: return c >= 0; }
}
// Applies ONE bracket item (between commas) to `n`.
inline bool item(const knst_json* n, const char* s, const char* e, knst_json_ptr_list& out, knst_json_errc& ec) noexcept {
    trim(s, e);
    if (s >= e) { ec = knst_json_errc::pointer_syntax; return false; }
    if (*s == '*') { if (!children(n, out)) { ec = knst_json_errc::out_of_memory; return false; } return true; }
    if (*s == '\'' || *s == '"') {
        knst_json_buffer k;
        const char* q = s;
        if (!quoted(q, e, k)) { ec = knst_json_errc::pointer_syntax; return false; }
        const knst_json* v = n->find(k.data() ? k.data() : "", k.size());
        if (v && !out.push(v)) { ec = knst_json_errc::out_of_memory; return false; }
        return true;
    }
    if (*s == '?') {
        ++s;
        if (n->is_container()) {
            knst_json_ptr_list ch;
            if (!children(n, ch)) { ec = knst_json_errc::out_of_memory; return false; }
            for (const knst_json* c : ch) if (filter(c, s, e) && !out.push(c)) { ec = knst_json_errc::out_of_memory; return false; }
        }
        return true;
    }
    // index or slice
    long long part[3] = { 0, 0, 0 };
    bool has[3] = { false, false, false };
    int np = 0;
    bool slice = false;
    const char* p = s;
    for (;;) {
        skip(p, e);
        if (p < e && *p != ':') {
            if (!integer(p, e, part[np])) { ec = knst_json_errc::pointer_syntax; return false; }
            has[np] = true;
            skip(p, e);
        }
        if (p < e && *p == ':') { slice = true; ++p; if (++np > 2) { ec = knst_json_errc::pointer_syntax; return false; } continue; }
        break;
    }
    if (p != e) { ec = knst_json_errc::pointer_syntax; return false; }
    if (!n->is_array()) return true;
    const long long len = static_cast<long long>(n->size());
    if (!slice) {
        long long i = part[0] < 0 ? part[0] + len : part[0];
        if (i >= 0 && i < len && !out.push(n->at(static_cast<size_t>(i)))) { ec = knst_json_errc::out_of_memory; return false; }
        return true;
    }
    const long long step = has[2] ? part[2] : 1;
    if (step == 0) return true;
    auto norm = [len](long long i) noexcept { return i >= 0 ? i : len + i; };
    auto clamp = [](long long v, long long lo, long long hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); };
    if (step > 0) {
        const long long lo = clamp(has[0] ? norm(part[0]) : 0, 0, len);
        const long long hi = clamp(has[1] ? norm(part[1]) : len, 0, len);
        for (long long i = lo; i < hi; i += step) if (!out.push(n->at(static_cast<size_t>(i)))) { ec = knst_json_errc::out_of_memory; return false; }
    } else {
        const long long hi = clamp(has[0] ? norm(part[0]) : len - 1, -1, len - 1);
        const long long lo = clamp(has[1] ? norm(part[1]) : -1, -1, len - 1);
        for (long long i = hi; lo < i; i += step) if (!out.push(n->at(static_cast<size_t>(i)))) { ec = knst_json_errc::out_of_memory; return false; }
    }
    return true;
}
// Applies the whole bracket content (comma separated items).
inline bool bracket(const knst_json* n, const char* s, const char* e, knst_json_ptr_list& out, knst_json_errc& ec) noexcept {
    const char* it = s;
    char q = 0; int depth = 0;
    for (const char* p = s;; ++p) {
        const bool end = p >= e;
        if (!end) {
            const char c = *p;
            if (q) { if (c == '\\') ++p; else if (c == q) q = 0; continue; }
            if (c == '\'' || c == '"') { q = c; continue; }
            if (c == '(') { ++depth; continue; }
            if (c == ')') { --depth; continue; }
        }
        if (end || (*p == ',' && depth == 0)) {
            if (!item(n, it, p, out, ec)) return false;
            if (end) break;
            it = p + 1;
        }
    }
    return true;
}

} // namespace jp
} // namespace knst_json_detail

inline bool knst_json::select(const char* path, size_t len, knst_json_ptr_list& out, knst_json_error* err) const noexcept {
    if (err) *err = knst_json_error();
    out.clear();
    if (!path && len) { fill_err(err, knst_json_errc::invalid_argument); return false; }
    using namespace knst_json_detail;
    const char* b = path;
    const char* p = path;
    const char* e = path + len;
    knst_json_errc ec = knst_json_errc::pointer_syntax;
    auto bad = [&]() noexcept {
        if (err) { *err = knst_json_error(); err->code = ec; err->offset = static_cast<size_t>(p - b); }
        return false;
    };
    jp::skip(p, e);
    if (p >= e || *p != '$') return bad();
    ++p;
    knst_json_ptr_list cur, nxt;
    if (!cur.push(this)) { ec = knst_json_errc::out_of_memory; return bad(); }
    for (;;) {
        jp::skip(p, e);
        if (p >= e) break;
        bool desc = false;
        if (*p == '.' && p + 1 < e && p[1] == '.') { desc = true; p += 2; }
        int mode;                                  // 0 name, 1 wildcard, 2 bracket
        const char* ss = nullptr; const char* se = nullptr;
        if (p < e && *p == '[') {
            const char* q = p + 1;
            char qc = 0; int depth = 0;
            for (; q < e; ++q) {
                const char c = *q;
                if (qc) { if (c == '\\') ++q; else if (c == qc) qc = 0; continue; }
                if (c == '\'' || c == '"') qc = c;
                else if (c == '(') ++depth;
                else if (c == ')') --depth;
                else if (c == ']' && depth == 0) break;
            }
            if (q >= e) return bad();
            ss = p + 1; se = q; p = q + 1; mode = 2;
        } else {
            if (!desc) { if (*p != '.') return bad(); ++p; }
            if (p < e && *p == '*') { mode = 1; ++p; }
            else {
                ss = p;
                while (p < e && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_' || *p == '-' || *p == '$' || static_cast<unsigned char>(*p) >= 0x80)) ++p;
                if (ss == p) return bad();
                se = p; mode = 0;
            }
        }
        nxt.clear();
        for (const knst_json* n : cur) {
            knst_json_ptr_list src;
            if (desc) { if (!jp::descendants(n, src)) { ec = knst_json_errc::out_of_memory; return bad(); } }
            else if (!src.push(n)) { ec = knst_json_errc::out_of_memory; return bad(); }
            for (const knst_json* s : src) {
                if (mode == 0) {
                    const knst_json* v = s->find(ss, static_cast<size_t>(se - ss));
                    if (v && !nxt.push(v)) { ec = knst_json_errc::out_of_memory; return bad(); }
                } else if (mode == 1) {
                    if (!jp::children(s, nxt)) { ec = knst_json_errc::out_of_memory; return bad(); }
                } else if (!jp::bracket(s, ss, se, nxt, ec)) return bad();
            }
        }
        std::swap(cur, nxt);
    }
    if (!out.append(cur.data(), cur.size())) { ec = knst_json_errc::out_of_memory; return bad(); }
    return true;
}

// ============================================================================
//  JSON Schema (draft-07 style subset)
// ============================================================================
namespace knst_json_detail {

class schema_validator {
public:
    schema_validator(const knst_json& root, knst_json_validation* r) noexcept : root_(root), res_(r) {}
    bool check(const knst_json& inst, const knst_json& sch, uint32_t depth) noexcept {
        if (sch.is_bool()) { bool b = true; sch.get(b); return b ? true : fail("false"); }
        if (!sch.is_object()) return true;
        if (depth >= KNST_JSON_MAX_RECURSION) return fail("depth");
        if (const knst_json* r = sch.find("$ref")) {
            if (!r->is_string() || r->length() == 0 || r->c_str()[0] != '#') return fail("$ref");
            const knst_json* t = root_.find_pointer_uri(r->c_str());
            return t ? check(inst, *t, depth + 1) : fail("$ref");
        }
        if (const knst_json* t = sch.find("type")) {
            bool ok = false;
            if (t->is_string()) ok = type_ok(inst, t->c_str());
            else if (t->is_array()) { for (const knst_json& x : *t) if (x.is_string() && type_ok(inst, x.c_str())) { ok = true; break; } }
            if (!ok) return fail("type");
        }
        if (const knst_json* en = sch.find("enum")) {
            bool ok = false;
            for (const knst_json& x : *en) if (x == inst) { ok = true; break; }
            if (!ok) return fail("enum");
        }
        if (const knst_json* c = sch.find("const")) if (!(*c == inst)) return fail("const");

        if (inst.is_number()) {
            if (!num_limit(inst, sch, "minimum", -1, true)) return fail("minimum");
            if (!num_limit(inst, sch, "maximum", 1, true)) return fail("maximum");
            if (!num_limit(inst, sch, "exclusiveMinimum", -1, false)) return fail("exclusiveMinimum");
            if (!num_limit(inst, sch, "exclusiveMaximum", 1, false)) return fail("exclusiveMaximum");
            if (const knst_json* m = sch.find("multipleOf")) {
                double d, x;
                if (m->get(d) && d > 0 && inst.get(x)) {
                    const double q = x / d;
                    if (std::fabs(q - std::round(q)) > 1e-9 * (std::fabs(q) > 1 ? std::fabs(q) : 1)) return fail("multipleOf");
                }
            }
        }
        if (inst.is_string()) {
            size_t cps = 0;
            for (size_t i = 0; i < inst.length(); ++i) if ((static_cast<unsigned char>(inst.c_str()[i]) & 0xC0) != 0x80) ++cps;
            uint64_t lim;
            if (const knst_json* m = sch.find("minLength")) if (m->get(lim) && cps < lim) return fail("minLength");
            if (const knst_json* m = sch.find("maxLength")) if (m->get(lim) && cps > lim) return fail("maxLength");
        }
        if (inst.is_array()) KNST_JSON_TRY(check_array(inst, sch, depth));
        if (inst.is_object()) KNST_JSON_TRY(check_object(inst, sch, depth));

        if (const knst_json* a = sch.find("allOf")) for (const knst_json& s : *a) KNST_JSON_TRY(check(inst, s, depth + 1));
        if (const knst_json* a = sch.find("anyOf")) {
            bool ok = false;
            for (const knst_json& s : *a) if (sub(inst, s, depth + 1)) { ok = true; break; }
            if (!ok) return fail("anyOf");
        }
        if (const knst_json* a = sch.find("oneOf")) {
            int cnt = 0;
            for (const knst_json& s : *a) if (sub(inst, s, depth + 1)) ++cnt;
            if (cnt != 1) return fail("oneOf");
        }
        if (const knst_json* n = sch.find("not")) if (sub(inst, *n, depth + 1)) return fail("not");
        if (const knst_json* i = sch.find("if")) {
            if (sub(inst, *i, depth + 1)) { if (const knst_json* t = sch.find("then")) KNST_JSON_TRY(check(inst, *t, depth + 1)); }
            else if (const knst_json* el = sch.find("else")) KNST_JSON_TRY(check(inst, *el, depth + 1));
        }
        return true;
    }
private:
    const knst_json& root_;
    knst_json_validation* res_;
    knst_json_buffer path_;
    int quiet_ = 0;
    bool recorded_ = false;

    bool fail(const char* kw) noexcept {
        if (!quiet_ && !recorded_ && res_) {
            recorded_ = true;
            res_->valid = false; res_->keyword = kw;
            res_->path.clear();
            res_->path.append(path_.data(), path_.size());
        }
        return false;
    }
    bool sub(const knst_json& inst, const knst_json& sch, uint32_t depth) noexcept {
        ++quiet_; const bool r = check(inst, sch, depth); --quiet_;
        return r;
    }
    static bool type_ok(const knst_json& v, const char* t) noexcept {
        if (!std::strcmp(t, "null")) return v.is_null();
        if (!std::strcmp(t, "boolean")) return v.is_bool();
        if (!std::strcmp(t, "object")) return v.is_object();
        if (!std::strcmp(t, "array")) return v.is_array();
        if (!std::strcmp(t, "string")) return v.is_string();
        if (!std::strcmp(t, "number")) return v.is_number();
        if (!std::strcmp(t, "integer")) return v.is_integral_value();
        return false;
    }
    // dir -1: inst must be >= (inclusive) / > limit ; dir +1: <= / <
    static bool num_limit(const knst_json& inst, const knst_json& sch, const char* kw, int dir, bool inclusive) noexcept {
        const knst_json* l = sch.find(kw);
        if (!l || !l->is_number()) return true;
        const int c = knst_json::compare_numbers(inst, *l);
        if (dir < 0) return inclusive ? c >= 0 : c > 0;
        return inclusive ? c <= 0 : c < 0;
    }
    bool with_index(size_t i, const knst_json& inst, const knst_json& sch, uint32_t depth) noexcept {
        const size_t mark = path_.size();
        char t[24];
        path_.push('/'); path_.append(t, format_u64(i, t));
        const bool r = check(inst, sch, depth + 1);
        path_.set_size(mark);
        return r;
    }
    bool with_key(const char* k, size_t kl, const knst_json& inst, const knst_json& sch, uint32_t depth) noexcept {
        const size_t mark = path_.size();
        pointer_escape_append(path_, k, kl);
        const bool r = check(inst, sch, depth + 1);
        path_.set_size(mark);
        return r;
    }
    bool check_array(const knst_json& inst, const knst_json& sch, uint32_t depth) noexcept {
        uint64_t lim;
        if (const knst_json* m = sch.find("minItems")) if (m->get(lim) && inst.size() < lim) return fail("minItems");
        if (const knst_json* m = sch.find("maxItems")) if (m->get(lim) && inst.size() > lim) return fail("maxItems");
        if (const knst_json* u = sch.find("uniqueItems")) {
            bool b = false;
            if (u->get(b) && b)
                for (size_t i = 1; i < inst.size(); ++i) for (size_t j = 0; j < i; ++j) if (inst[i] == inst[j]) return fail("uniqueItems");
        }
        size_t positional = 0;
        if (const knst_json* it = sch.find("items")) {
            if (it->is_array()) {
                positional = it->size();
                for (size_t i = 0; i < inst.size() && i < positional; ++i) KNST_JSON_TRY(with_index(i, inst[i], (*it)[i], depth));
            } else for (size_t i = 0; i < inst.size(); ++i) KNST_JSON_TRY(with_index(i, inst[i], *it, depth));
        }
        if (positional) if (const knst_json* ai = sch.find("additionalItems"))
            for (size_t i = positional; i < inst.size(); ++i) KNST_JSON_TRY(with_index(i, inst[i], *ai, depth));
        if (const knst_json* c = sch.find("contains")) {
            bool ok = false;
            for (const knst_json& x : inst) if (sub(x, *c, depth + 1)) { ok = true; break; }
            if (!ok) return fail("contains");
        }
        return true;
    }
    bool check_object(const knst_json& inst, const knst_json& sch, uint32_t depth) noexcept {
        uint64_t lim;
        if (const knst_json* m = sch.find("minProperties")) if (m->get(lim) && inst.size() < lim) return fail("minProperties");
        if (const knst_json* m = sch.find("maxProperties")) if (m->get(lim) && inst.size() > lim) return fail("maxProperties");
        if (const knst_json* r = sch.find("required"))
            for (const knst_json& k : *r) if (k.is_string() && !inst.find(k.c_str(), k.length())) return fail("required");
        const knst_json* props = sch.find("properties");
        if (props && props->is_object())
            for (const knst_json_member& pm : props->members())
                if (const knst_json* v = inst.find(pm.key(), pm.key_size())) KNST_JSON_TRY(with_key(pm.key(), pm.key_size(), *v, pm.v, depth));
        if (const knst_json* ap = sch.find("additionalProperties")) {
            for (const knst_json_member& m : inst.members()) {
                if (props && props->is_object() && props->find(m.key(), m.key_size())) continue;
                KNST_JSON_TRY(with_key(m.key(), m.key_size(), m.v, *ap, depth));
            }
        }
        if (const knst_json* dep = sch.find("dependencies")) {
            if (dep->is_object()) for (const knst_json_member& d : dep->members()) {
                if (!inst.find(d.key(), d.key_size())) continue;
                if (d.v.is_array()) { for (const knst_json& k : d.v) if (k.is_string() && !inst.find(k.c_str(), k.length())) return fail("dependencies"); }
                else KNST_JSON_TRY(check(inst, d.v, depth + 1));
            }
        }
        return true;
    }
};

} // namespace knst_json_detail

inline bool knst_json::validate(const knst_json& schema, knst_json_validation* result) const noexcept {
    if (result) { result->valid = true; result->keyword = ""; result->path.clear(); }
    knst_json_detail::schema_validator v(schema, result);
    return v.check(*this, schema, 0);
}

#endif // KNST_JSON_HPP