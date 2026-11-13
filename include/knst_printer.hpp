// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

// ===========================================================================
//  knst_printer.hpp  -  KernelNucleusT printer + scanner controller Betaa
// ===========================================================================
#pragma once

// knst_network.hpp — TLS, proxy, DNS cache, mDNS for IPP/IPPS.
// If the header is not on the include path, IPPS support is disabled
// and only plain HTTP IPP works (raw sockets fallback).
#if defined(__has_include)
#  if __has_include(<knst_network.hpp>)
#    include <knst_network.hpp>
#    define KNST_PRN_HAS_NETWORK 1
#  elif __has_include("knst_network.hpp")
#    include "knst_network.hpp"
#    define KNST_PRN_HAS_NETWORK 1
#  else
#    define KNST_PRN_HAS_NETWORK 0
#  endif
#else
#  define KNST_PRN_HAS_NETWORK 0
#endif

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cctype>
#include <chrono>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <memory>

#ifndef MSG_NOSIGNAL
    
    #define MSG_NOSIGNAL 0
#endif

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <winspool.h>
#  include <shellapi.h>
#  if defined(_MSC_VER)
#    include <objbase.h>
#    include <oleauto.h>
#    include <shlwapi.h>
#    include <wia.h>
#    pragma comment(lib, "winspool.lib")
#    pragma comment(lib, "gdi32.lib")
#    pragma comment(lib, "shell32.lib")
#    pragma comment(lib, "ole32.lib")
#    pragma comment(lib, "oleaut32.lib")
#    pragma comment(lib, "shlwapi.lib")
#    pragma comment(lib, "wiaguid.lib")
#    pragma comment(lib, "ws2_32.lib")
#  endif
#elif defined(__linux__)
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <poll.h>
#  include <fcntl.h>
#  include <cerrno>
#endif

// Windows SDK <rpcndr.h> #defines "small" as "char" (MIDL legacy).
// This breaks any variable literally named "small". Undef it so user
// code and this library can use "small" as an identifier safely.
#if defined(_WIN32) && defined(small)
#  undef small
#endif


namespace knst_prn {

// ===========================================================================
//                                  Enums
// ===========================================================================
enum class status : uint8_t {
    Ok = 0, InvalidArgument, NotFound, IoError, Timeout, BadState,
    OutOfMemory, ToolMissing, NotSupported, Permission, DecodeFailed, Unknown
};

inline const char* status_string(status s) noexcept {
    switch (s) {
        case status::Ok:              return "ok";
        case status::InvalidArgument: return "invalid argument";
        case status::NotFound:        return "not found";
        case status::IoError:         return "I/O error";
        case status::Timeout:         return "timeout";
        case status::BadState:        return "bad state";
        case status::OutOfMemory:     return "out of memory";
        case status::ToolMissing:     return "tool missing";
        case status::NotSupported:    return "not supported";
        case status::Permission:      return "permission denied";
        case status::DecodeFailed:    return "image decode failed";
        default:                      return "unknown error";
    }
}

enum class printer_state : uint8_t {
    Unknown = 0, Ready, Paused, Error, PaperJam, PaperOut,
    TonerLow, DoorOpen, Offline, Busy, WarmingUp
};

enum class job_state : uint8_t {
    Unknown = 0, Pending, Held, Processing, Rendering, Spooling,
    Stopped, Blocked, Canceled, Aborted, Completed, Retrying
};

enum class duplex_mode   : uint8_t { Default = 0, Simplex, LongEdge, ShortEdge };
enum class color_mode    : uint8_t { Default = 0, Color, Mono, Auto, Gray };
enum class orientation   : uint8_t { Default = 0, Portrait, Landscape, ReverseLandscape, ReversePortrait };
enum class print_quality : uint8_t { Default = 0, Draft, Normal, High };
enum class media_size    : uint8_t { Default = 0, A3, A4, A5, A6, Letter, Legal, Tabloid, Executive };

enum class paper_type : uint8_t {
    Default = 0, Plain, Photo, Glossy, Matte, Transparency,
    Envelope, Label, Cardstock, Recycled, Bond, Letterhead
};

enum class job_sheet : uint8_t {
    None = 0, Standard, Classified, Confidential, Draft, Secret, TopSecret
};

enum class test_page_type : uint8_t {
    Diagnostic = 0, ColorBar, Alignment, Grid, Gradient, NozzleCheck
};

enum class supply_kind : uint8_t {
    Unknown = 0, Toner, Ink, Drum, WasteToner, WasteInk, Ribbon, Fuser, Other
};

enum class alignment  : uint8_t { Left = 0, Center, Right };
enum class shape_type : uint8_t { Line = 0, Rect, FilledRect };

// How a photo is placed inside its cell.
enum class fit_mode : uint8_t {
    Fit = 0,    // whole photo visible, aspect kept, centered
    Fill,       // cell completely covered, aspect kept, overflow is cropped
    Stretch     // cell completely covered, aspect NOT kept
};

// ===========================================================================
//                              Result wrapper
// ===========================================================================
template <typename T>
struct result {
    T      value{};
    status st     = status::Ok;
    int    native = 0;

    static result success(T v) { result r; r.value = std::move(v); return r; }
    static result fail(status s, int n = 0) { result r; r.st = s; r.native = n; return r; }
    explicit operator bool() const noexcept { return st == status::Ok; }
    const char* error_string() const noexcept { return status_string(st); }
};

template <>
struct result<void> {
    status st     = status::Ok;
    int    native = 0;

    static result success() noexcept { return result(); }
    static result fail(status s, int n = 0) noexcept { result r; r.st = s; r.native = n; return r; }
    explicit operator bool() const noexcept { return st == status::Ok; }
    const char* error_string() const noexcept { return status_string(st); }
};

// ===========================================================================
//                                 Printer structs
// ===========================================================================
struct printer_info_ex {
    knst_c16string name;
    knst_c16string description;
    knst_c16string location;
    knst_c16string model;
    knst_c16string make_and_model;
    knst_c16string device_uri;
    printer_state  state          = printer_state::Unknown;
    bool           is_default     = false;
    bool           is_shared      = false;
    bool           is_enabled     = false;
    bool           is_accepting   = false;
    uint32_t       pending_jobs   = 0;
};

struct queue_status {
    bool           enabled   = false;
    bool           accepting = false;
    uint32_t       job_count = 0;
    printer_state  state     = printer_state::Unknown;
    knst_c16string text;
};

struct printer_capabilities {
    bool color             = false;
    bool duplex_long_edge  = false;
    bool duplex_short_edge = false;
    bool stapling          = false;
    bool punching          = false;
    bool booklet           = false;
    knst_vector<knst_c16string> supported_media;
    knst_vector<knst_c16string> supported_resolutions;
    knst_vector<knst_c16string> supported_sources;
    knst_vector<knst_c16string> supported_output_bins;
};

struct supply_level {
    knst_c16string name;
    knst_c16string raw_type;
    supply_kind    type    = supply_kind::Unknown;
    uint32_t       percent = 0;
    bool           known   = true;   // false: printer does not report a level
    bool           low     = false;
};

struct printer_alert {
    uint32_t       severity = 0;     // 0 report, 1 warning, 2 error
    knst_c16string code;
    knst_c16string text;
};

struct usage_stats {
    uint64_t total_jobs   = 0;
    uint64_t done_jobs    = 0;
    uint64_t pending_jobs = 0;
    uint64_t total_pages  = 0;
    uint64_t total_bytes  = 0;
};

struct device_info  { knst_c16string type; knst_c16string uri; };
struct driver_info  { knst_c16string id;   knst_c16string description; };

struct server_info {
    bool           running = false;
    knst_c16string hostname;
    knst_c16string default_printer;
    uint32_t       total_printers = 0;
};

struct job_info {
    uint32_t       id             = 0;
    knst_c16string printer;
    knst_c16string user;
    knst_c16string title;
    knst_c16string state_text;
    knst_c16string message;
    job_state      state          = job_state::Unknown;
    uint64_t       size_bytes     = 0;
    uint32_t       pages          = 0;
    uint64_t       submitted_unix = 0;
    uint32_t       priority       = 50;
};

struct printer_option {
    knst_c16string name;
    knst_c16string text;
    knst_c16string current;
    knst_vector<knst_c16string> choices;
};

struct printer_class {
    knst_c16string name;
    knst_vector<knst_c16string> members;
};

struct cache_config {
    bool           enabled     = false;
    knst_c16string directory;
    uint32_t       max_size_mb = 500;
};

struct print_options {
    // page setup
    uint32_t       copies           = 1;
    duplex_mode    duplex           = duplex_mode::Default;
    color_mode     color            = color_mode::Default;
    orientation    orient           = orientation::Default;
    print_quality  quality          = print_quality::Default;
    media_size     paper            = media_size::Default;
    paper_type     ptype            = paper_type::Default;
    uint32_t       media_weight_gsm = 0;
    bool           borderless       = false;
    bool           bleed            = false;
    knst_c16string media;           // raw CUPS media name, overrides `paper`
    knst_c16string page_ranges;     // "1-3,5"
    knst_c16string title;
    uint32_t       priority         = 50;
    bool           fit_to_page      = false;
    bool           raw              = false;
    bool           collate          = false;
    uint32_t       scaling          = 100;
    knst_c16string resolution;      // "600dpi"
    bool           toner_save       = false;
    bool           auto_rotate      = false;
    bool           auto_center      = false;
    // image adjustments, percent delta -100..100 (driver/filter dependent)
    int32_t        brightness       = 0;
    int32_t        contrast         = 0;   // not supported by CUPS filters (ignored on Linux)
    int32_t        saturation       = 0;
    bool           mirror_horizontal = false;
    bool           mirror_vertical  = false;
    bool           invert_colors    = false; // not supported by CUPS filters (ignored on Linux)
    // layout
    uint32_t       n_up             = 1;
    bool           booklet          = false;
    bool           reverse          = false;
    uint32_t       custom_w_mm      = 0;
    uint32_t       custom_h_mm      = 0;
    int32_t        margin_top_mm    = 0;
    int32_t        margin_bottom_mm = 0;
    int32_t        margin_left_mm   = 0;
    int32_t        margin_right_mm  = 0;
    // finishing
    knst_c16string media_source;
    knst_c16string output_bin;
    bool           staple           = false;
    bool           punch            = false;
    bool           fold             = false;
    bool           face_down        = false;
    uint32_t       job_offset_mm    = 0;    // reserved (no CUPS equivalent)
    // job sheets
    job_sheet      job_sheet_start  = job_sheet::None;
    job_sheet      job_sheet_end    = job_sheet::None;
    // cache
    bool           cache_for_reprint = false;
    // Linux/CUPS: extra "key=value" options passed verbatim (validated)
    knst_vector<knst_c16string> extra_options;
};

struct secure_print_options {
    knst_c16string job_password;    // [A-Za-z0-9._:/,+*#-=@], 1..32 chars
    knst_c16string job_user;
};

// ---------------------------------------------------------------------------
//  Photo / image batch (many pictures -> ONE job)
// ---------------------------------------------------------------------------
struct image_batch_options {
    uint32_t rows = 1;                 // grid: rows x cols photos per page
    uint32_t cols = 1;
    fit_mode fit  = fit_mode::Fit;
    float    margin_mm = 8.0f;         // page margin
    float    gap_mm    = 4.0f;         // gap between cells
    float    border_mm = 0.0f;         // >0 draws a thin frame around each photo
    uint32_t copies_each = 1;          // every photo printed N times
    bool     rotate_to_fit      = true;   // rotate 90 deg when it uses the cell better
    bool     apply_exif         = true;   // honour JPEG EXIF orientation
    bool     show_filename      = false;  // small caption under each photo
    bool     skip_unreadable    = true;   // unreadable files are skipped, not fatal
    uint32_t max_dpi            = 300;    // downscale photos above this effective dpi
    knst_vector<knst_c16string>* skipped = nullptr; // optional: receives skipped paths
};

// ===========================================================================
//                          Rich text document
// ===========================================================================
struct text_style {
    knst_c16string font         = u"Helvetica";   // Helvetica | Courier | Times (+ -Bold / -Oblique ok)
    float          size_pt      = 12.0f;
    bool           bold         = false;
    bool           italic       = false;
    uint8_t        r = 0, g = 0, b = 0;
    float          line_spacing = 1.2f;
    alignment      align        = alignment::Left;
};

struct text_run {
    knst_c16string text;
    text_style     style;
};

struct draw_command {
    shape_type type = shape_type::Line;
    float      x1_mm = 0, y1_mm = 0, x2_mm = 0, y2_mm = 0;   // origin: top-left, millimetres
    float      line_width = 0.5f;                            // points
    uint8_t    r = 0, g = 0, b = 0;
    uint32_t   page = 1;                                     // 1-based, 0 = every page
};

struct rich_document {
    float    page_w_mm = 210.0f;
    float    page_h_mm = 297.0f;
    float    margin_top_mm    = 15.0f;
    float    margin_bottom_mm = 15.0f;
    float    margin_left_mm   = 20.0f;
    float    margin_right_mm  = 20.0f;

    knst_c16string header_text;
    knst_c16string footer_text;
    bool     show_page_number = false;   // "n / N" centered in the footer
    bool     show_date        = false;   // right aligned in the header

    knst_vector<text_run>     elements;  // word-wrapped and paginated automatically
    knst_vector<draw_command> shapes;    // drawn below the text

    // Optional custom font embedding (full Unicode support).
    // Empty (default)   → WinAnsi + 12 Type1 fonts (Latin-1 + Turkish only).
    // Non-empty (path)  → TTF/OTF file is embedded as Type0 / CIDFontType2 /
    //                     Identity-H in the PDF. Enables Cyrillic, Greek,
    //                     Arabic, Hebrew, CJK, Thai, Devanagari, etc.
    //                     RTL shaping and complex scripts still need shaping;
    //                     this handles per-character mapping.
    //                     The whole file is embedded (no subsetting) → PDF
    //                     grows by ~300-800 KB.
    //
    // Example: doc.embed_font_path = u"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf";
    //          doc.embed_font_path = u"C:\\Windows\\Fonts\\arial.ttf";
    //          doc.embed_font_path = u"NotoSans";   // system name, auto-resolved
    //          doc.embed_font_path = u"Arial";
    knst_c16string embed_font_path;

    // When true AND embed_font_path is empty, the PDF writer scans the text
    // for non-Latin-1 codepoints. If found, it auto-locates a Unicode-capable
    // system font (NotoSans / Arial / DejaVu Sans / system default) and embeds
    // it. If false (default), only WinAnsi is used.
    bool auto_fallback = false;
};

// ===========================================================================
//                              Scanner structs
// ===========================================================================
struct scanner_info {
    knst_c16string name;
    knst_c16string device_id;
    knst_c16string backend;      // "escl", "airscan", "hpaio", "wia" ...
    bool adf         = false;
    bool duplex_adf  = false;
    bool flatbed     = true;
    bool is_network  = false;
};

struct scan_options {
    uint32_t       dpi = 300;
    color_mode     mode = color_mode::Color;
    media_size     paper = media_size::A4;      // scan area, Default = device maximum
    bool           use_adf = false;
    bool           duplex = false;
    knst_c16string source;                      // explicit SANE source, e.g. "Flatbed", "ADF", "ADF Duplex"
    knst_c16string output_format = u"png";      // png, jpeg, tiff (scan_to_pdf makes the pdf)
    uint32_t       page_count = 1;              // 0 = until the feeder is empty (ADF)
    int32_t        brightness = 0;
    int32_t        contrast = 0;
    uint32_t       timeout_s = 120;   // 180 → 120: faster failure, user can override
};

struct scanner_capabilities {
    knst_vector<uint32_t>       resolutions;
    knst_vector<color_mode>     modes;
    knst_vector<knst_c16string> mode_names;
    knst_vector<knst_c16string> sources;
    bool has_adf = false;
    bool has_duplex_adf = false;
    bool has_flatbed = true;
    uint32_t max_width_mm = 0;
    uint32_t max_height_mm = 0;
};

// ===========================================================================
//                                  Detail
// ===========================================================================
namespace detail {

// ─── Forward declarations ──────────────────────────────────────────────────
// TTF structs are defined here in full because write_pdf() declares local
// variables (`knst_ttf_font ttf; knst_ttf_subset subset;`) which require
// complete types. Function bodies themselves are defined further below.
struct knst_ttf_font {
    std::vector<uint8_t> data;
    uint32_t units_per_em = 1000;
    uint16_t num_glyphs = 0;
    int16_t  index_to_loc_format = 0;
    int32_t  ascent = 0;
    int32_t  descent = 0;
    int32_t  cap_height = 0;
    int32_t  bbox[4] = {0, 0, 0, 0};
    std::vector<uint32_t> advance;
    std::vector<uint32_t> loca;
    uint32_t glyf_offset = 0;
    uint32_t glyf_length = 0;
    std::unordered_map<uint32_t, uint16_t> cmap;
    bool     valid = false;
};

struct knst_ttf_subset {
    std::vector<uint8_t> ttf_bytes;
    std::vector<uint16_t> advance;
    std::unordered_map<uint16_t, uint16_t> gid_map;
    std::unordered_map<uint16_t, uint32_t> unicode_map;
    uint16_t num_glyphs = 0;
    bool valid = false;
};

inline bool knst_ttf_load(const std::vector<uint8_t>& data, knst_ttf_font& f);
inline bool knst_ttf_build_subset(const knst_ttf_font& font,
                                   const std::vector<uint16_t>& used_cp,
                                   knst_ttf_subset& out);
inline bool knst_needs_unicode(char16_t c);
inline knst_c16string knst_find_font(const knst_c16string& name);

inline bool knst_deflate_compress(const uint8_t* data, size_t len, knst_byte_string& out);
inline bool knst_deflate_zlib(const uint8_t* data, size_t len, knst_byte_string& out);

constexpr float kMm2Pt = 72.0f / 25.4f;

// ---- string helpers -------------------------------------------------------
inline std::string to_u8(const knst_c16string& s) {
    if (s.empty()) return std::string();
    knst_byte_string b = knst_dev::to_utf8(s);
    return std::string(reinterpret_cast<const char*>(b.data()), b.length());
}
inline knst_c16string from_u8(const std::string& s) {
    if (s.empty()) return knst_c16string();
    return knst_c16string(s.data(), static_cast<uint32_t>(s.size()));
}
inline std::u16string to_u16(const knst_c16string& s) {
    return std::u16string(s.data(), s.length());
}
inline knst_c16string from_u16(const std::u16string& s) {
    if (s.empty()) return knst_c16string();
    return knst_c16string(s.data(), static_cast<uint32_t>(s.size()));
}

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline bool ieq(const std::string& a, const char* b) { return lower(a) == lower(std::string(b)); }
inline bool icontains(const std::string& h, const char* n) { return lower(h).find(lower(std::string(n))) != std::string::npos; }
inline bool starts_with(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

inline std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == d) { out.push_back(cur); cur.clear(); } else cur.push_back(c); }
    out.push_back(cur);
    return out;
}
inline std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

inline long to_long(const std::string& s, long def = 0) {
    if (s.empty()) return def;
    char* e = nullptr; long v = std::strtol(s.c_str(), &e, 10);
    return (e == s.c_str()) ? def : v;
}
// locale independent "123.45" parser
inline double c_atof(const std::string& s) {
    size_t i = 0; bool neg = false; double v = 0.0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); ++i; }
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) v = v * 10.0 + (s[i++] - '0');
    if (i < s.size() && s[i] == '.') {
        ++i; double f = 0.1;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { v += (s[i++] - '0') * f; f *= 0.1; }
    }
    return neg ? -v : v;
}
// locale independent number output with up to 3 decimals ("12.5", "3")
inline void put_num(std::string& o, double v) {
    if (v != v) v = 0.0;
    if (v < 0) { o.push_back('-'); v = -v; }
    long long scaled = static_cast<long long>(v * 1000.0 + 0.5);
    o += std::to_string(scaled / 1000);
    long long fr = scaled % 1000;
    if (fr) {
        char b[8]; std::snprintf(b, sizeof(b), "%03lld", fr);
        std::string s(b);
        while (!s.empty() && s.back() == '0') s.pop_back();
        o.push_back('.'); o += s;
    }
}

// Values that are placed into "-o key=value" must not contain spaces/quotes.
inline bool safe_value(const std::string& v) {
    if (v.empty() || v.size() > 128) return false;
    for (unsigned char c : v)
        if (!(std::isalnum(c) || std::strchr("._:/,+*#-=@", c))) return false;
    return true;
}
inline bool safe_value(const knst_c16string& v) { return safe_value(to_u8(v)); }

// key=value tokenizer (lpoptions style, 'quoted values' supported)
using kvlist = std::vector<std::pair<std::string, std::string>>;

inline void parse_kv_line(const std::string& s, kvlist& out) {
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= n) break;
        std::string key, val;
        while (i < n && s[i] != '=' && !std::isspace(static_cast<unsigned char>(s[i]))) key.push_back(s[i++]);
        if (i < n && s[i] == '=') {
            ++i;
            if (i < n && (s[i] == '\'' || s[i] == '"')) {
                char q = s[i++];
                while (i < n && s[i] != q) { if (s[i] == '\\' && i + 1 < n) ++i; val.push_back(s[i++]); }
                if (i < n) ++i;
            } else {
                while (i < n && !std::isspace(static_cast<unsigned char>(s[i]))) {
                    if (s[i] == '\\' && i + 1 < n) ++i;
                    val.push_back(s[i++]);
                }
            }
        }
        if (!key.empty()) out.emplace_back(key, val);
    }
}
inline const std::string* kv_find(const kvlist& l, const char* key) {
    for (auto& p : l) if (p.first == key) return &p.second;
    return nullptr;
}
inline std::string kv_get(const kvlist& l, const char* key, const char* def = "") {
    const std::string* v = kv_find(l, key);
    return v ? *v : std::string(def);
}

inline std::string now_string() {
    std::time_t t = std::time(nullptr); std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[40];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d", tmv.tm_year + 1900, tmv.tm_mon + 1,
                  tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return b;
}

// ---- paper ----------------------------------------------------------------
inline bool paper_mm(media_size m, float& w, float& h) {
    switch (m) {
        case media_size::A3:        w = 297.0f;  h = 420.0f;  return true;
        case media_size::A4:        w = 210.0f;  h = 297.0f;  return true;
        case media_size::A5:        w = 148.0f;  h = 210.0f;  return true;
        case media_size::A6:        w = 105.0f;  h = 148.0f;  return true;
        case media_size::Letter:    w = 215.9f;  h = 279.4f;  return true;
        case media_size::Legal:     w = 215.9f;  h = 355.6f;  return true;
        case media_size::Tabloid:   w = 279.4f;  h = 431.8f;  return true;
        case media_size::Executive: w = 184.15f; h = 266.7f;  return true;
        default: return false;
    }
}
inline media_size paper_from_name(const std::string& n) {
    std::string s = lower(n);
    auto has = [&](const char* k) { return s.find(k) != std::string::npos; };
    if (has("a3")) return media_size::A3;
    if (has("a4")) return media_size::A4;
    if (has("a5")) return media_size::A5;
    if (has("a6")) return media_size::A6;
    if (has("legal")) return media_size::Legal;
    if (has("letter")) return media_size::Letter;
    if (has("tabloid") || has("11x17") || has("ledger")) return media_size::Tabloid;
    if (has("executive")) return media_size::Executive;
    return media_size::Default;
}
inline const char16_t* media_str(media_size m) {
    switch (m) {
        case media_size::A3:        return u"iso_a3_297x420mm";
        case media_size::A4:        return u"iso_a4_210x297mm";
        case media_size::A5:        return u"iso_a5_148x210mm";
        case media_size::A6:        return u"iso_a6_105x148mm";
        case media_size::Letter:    return u"na_letter_8.5x11in";
        case media_size::Legal:     return u"na_legal_8.5x14in";
        case media_size::Tabloid:   return u"na_ledger_11x17in";
        case media_size::Executive: return u"na_executive_7.25x10.5in";
        default:                    return nullptr;
    }
}
inline const char* ppd_page_name(media_size m) {
    switch (m) {
        case media_size::A3: return "A3";         case media_size::A4: return "A4";
        case media_size::A5: return "A5";         case media_size::A6: return "A6";
        case media_size::Letter: return "Letter"; case media_size::Legal: return "Legal";
        case media_size::Tabloid: return "Tabloid"; case media_size::Executive: return "Executive";
        default: return nullptr;
    }
}
inline const char* paper_type_str(paper_type p) {
    switch (p) {
        case paper_type::Plain:        return "stationery";
        case paper_type::Photo:        return "photographic";
        case paper_type::Glossy:       return "photographic-glossy";
        case paper_type::Matte:        return "photographic-matte";
        case paper_type::Transparency: return "transparency";
        case paper_type::Envelope:     return "envelope";
        case paper_type::Label:        return "labels";
        case paper_type::Cardstock:    return "cardstock";
        case paper_type::Recycled:     return "stationery-recycled";
        case paper_type::Bond:         return "bond";
        case paper_type::Letterhead:   return "stationery-letterhead";
        default:                       return nullptr;
    }
}
inline const char* job_sheet_str(job_sheet s) {
    switch (s) {
        case job_sheet::Standard:     return "standard";
        case job_sheet::Classified:   return "classified";
        case job_sheet::Confidential: return "confidential";
        case job_sheet::Draft:        return "draft";
        case job_sheet::Secret:       return "secret";
        case job_sheet::TopSecret:    return "topsecret";
        default:                      return nullptr;
    }
}

inline bool valid_options(const print_options& o) {
    if (o.copies == 0 || o.copies > 9999) return false;
    if (o.priority == 0 || o.priority > 100) return false;
    if (o.n_up > 16) return false;
    if (o.scaling < 10 || o.scaling > 400) return false;
    if (o.media_weight_gsm > 500) return false;
    if (o.brightness < -100 || o.brightness > 100) return false;
    if (o.saturation < -100 || o.saturation > 100) return false;
    if (!o.media.empty() && !safe_value(o.media)) return false;
    if (!o.page_ranges.empty() && !safe_value(o.page_ranges)) return false;
    if (!o.resolution.empty() && !safe_value(o.resolution)) return false;
    if (!o.media_source.empty() && !safe_value(o.media_source)) return false;
    if (!o.output_bin.empty() && !safe_value(o.output_bin)) return false;
    for (uint32_t i = 0; i < o.extra_options.size(); ++i) {
        std::string kv = to_u8(o.extra_options[i]);
        if (kv.find('=') == std::string::npos || !safe_value(kv)) return false;
    }
    return true;
}

// ---- file kinds -----------------------------------------------------------
enum class file_kind : uint8_t { Unknown = 0, Text, Image, Pdf, PostScript, Pcl, Zpl, EscPos };

inline std::string path_ext(const knst_c16string& path) {
    std::string p = to_u8(path);
    size_t sl = p.find_last_of("/\\");
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos || (sl != std::string::npos && dot < sl)) return std::string();
    return lower(p.substr(dot));
}
inline file_kind detect_kind(const knst_c16string& path) {
    std::string e = path_ext(path);
    if (e == ".txt" || e == ".log" || e == ".md") return file_kind::Text;
    if (e == ".jpg" || e == ".jpeg" || e == ".png" || e == ".gif" || e == ".bmp" || e == ".ppm" ||
        e == ".pgm" || e == ".pnm" || e == ".tif" || e == ".tiff" || e == ".webp" || e == ".heic" ||
        e == ".avif" || e == ".ico") return file_kind::Image;
    if (e == ".pdf") return file_kind::Pdf;
    if (e == ".ps" || e == ".eps") return file_kind::PostScript;
    if (e == ".pcl") return file_kind::Pcl;
    if (e == ".zpl") return file_kind::Zpl;
    if (e == ".escpos" || e == ".prn") return file_kind::EscPos;
    return file_kind::Unknown;
}

// ---- temp files / raw file IO --------------------------------------------
inline knst_c16string make_temp(const char16_t* ext) {
    static std::atomic<uint32_t> ctr{0};
#if defined(_WIN32)
    uint32_t pid = static_cast<uint32_t>(GetCurrentProcessId());
#else
    uint32_t pid = static_cast<uint32_t>(::getpid());
#endif
    uint64_t t = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::string name = "knst_print_" + std::to_string(pid) + "_" + std::to_string(ctr++) + "_" +
                       std::to_string(static_cast<uint32_t>(t & 0xFFFFFF));
    knst_c16string p = knst_file::join_path(knst_file::get_temp_dir(), from_u8(name));
    p.append(ext);
    return p;
}

// Removes every registered temp file when it goes out of scope.
struct temp_set {
    std::vector<knst_c16string> paths;
    temp_set() = default;
    temp_set(const temp_set&) = delete;
    temp_set& operator=(const temp_set&) = delete;
    ~temp_set() { for (auto& p : paths) knst_file::remove_file(p); }
    knst_c16string add(const char16_t* ext) { knst_c16string p = make_temp(ext); paths.push_back(p); return p; }
};

inline std::FILE* open_file(const knst_c16string& path, bool write) {
#if defined(_WIN32)
    std::wstring w(reinterpret_cast<const wchar_t*>(path.data()), path.length());
    return _wfopen(w.c_str(), write ? L"wb" : L"rb");
#else
    std::FILE* f = std::fopen(to_u8(path).c_str(), write ? "wb" : "rb");
    if (f && write) ::fchmod(::fileno(f), 0600);   // spool data may be confidential
    return f;
#endif
}
inline bool read_magic(const knst_c16string& path, unsigned char* buf, size_t n) {
    std::FILE* f = open_file(path, false);
    if (!f) return false;
    size_t got = std::fread(buf, 1, n, f);
    std::fclose(f);
    return got == n;
}

// ---- reprint cache --------------------------------------------------------
struct cache_state { std::mutex m; cache_config cfg; };
inline cache_state& cache() { static cache_state c; return c; }

inline void cache_evict_locked(const cache_config& c) {
    knst_vector<knst_file> all;
    if (!knst_file::list_directory_recursive(c.directory, all)) return;
    uint64_t total = 0;
    for (auto& f : all) if (f.is_file) total += f.size;
    uint64_t max_bytes = static_cast<uint64_t>(c.max_size_mb) * 1024ull * 1024ull;
    if (total <= max_bytes) return;
    std::vector<knst_file> files;
    for (auto& f : all) if (f.is_file) files.push_back(f);
    std::sort(files.begin(), files.end(), [](const knst_file& a, const knst_file& b) {
        return a.modified_time < b.modified_time;
    });
    for (auto& f : files) {
        if (total <= max_bytes) break;
        if (knst_file::remove_file(f.path)) total -= f.size;
    }
}
inline void cache_store(const knst_c16string& printer, uint32_t job, const knst_c16string& src) {
    auto& cs = cache();
    std::lock_guard<std::mutex> lk(cs.m);
    if (!cs.cfg.enabled || cs.cfg.directory.empty()) return;
    knst_c16string pd = knst_file::join_path(cs.cfg.directory, knst_file::sanitize_filename(printer));
    knst_file::create_directories(pd);
    knst_c16string dst = knst_file::join_path(pd, from_u8(std::to_string(job) + path_ext(src)));
    knst_file::copy_file(src, dst, true);
    cache_evict_locked(cs.cfg);
}
inline knst_c16string cache_find(const knst_c16string& printer, uint32_t job) {
    auto& cs = cache();
    std::lock_guard<std::mutex> lk(cs.m);
    if (!cs.cfg.enabled || cs.cfg.directory.empty()) return knst_c16string();
    knst_c16string pd = knst_file::join_path(cs.cfg.directory, knst_file::sanitize_filename(printer));
    knst_vector<knst_file> list;
    knst_file::list_directory(pd, list);
    std::string prefix = std::to_string(job) + ".";
    for (auto& f : list) if (starts_with(to_u8(f.name), prefix.c_str())) return f.path;
    return knst_c16string();
}

// ─── PDF → JPEG rasterizer (Linux/Windows: shells out to pdftoppm or magick) ─
// Not used on Android — there we use the Java PdfRenderer via JNI.
#if !defined(__ANDROID__)
inline std::vector<knst_c16string> rasterize_pdf_to_jpegs(
        const knst_c16string& pdf_path,
        const knst_c16string& out_dir,
        uint32_t target_dpi = 300,
        uint32_t jpeg_quality = 85) {
    std::vector<knst_c16string> result;
    if (pdf_path.empty() || out_dir.empty()) return result;
    knst_file::create_directories(out_dir);

    // ─── Try pdftoppm (poppler-utils) — cross-platform ───
    // Output: <out_dir>/page-1.jpg, page-2.jpg, ...
    {
        knst_c16string prefix = knst_file::join_path(out_dir, u"page");
        knst_vector<knst_c16string> args;
        args.push_back(u"-jpeg");
        args.push_back(u"-r");
        args.push_back(from_u8(std::to_string(target_dpi)));
        args.push_back(u"-jpegopt");
        args.push_back(from_u8("quality=" + std::to_string(jpeg_quality)));
        args.push_back(knst_file::resolve_path(pdf_path));
        args.push_back(prefix);
        auto res = knst_process::run_capture(u"pdftoppm", args, {}, nullptr, 0, 120000);
        if (res.error == knst_process_error::None && res.exit_code == 0) {
            knst_vector<knst_file> entries;
            knst_file::list_directory(out_dir, entries);
            std::vector<std::pair<std::string, knst_c16string>> found;
            for (auto& e : entries) {
                std::string nm = to_u8(e.name);
                if (e.is_file && e.size > 0 &&
                    nm.compare(0, 5, "page-") == 0 &&
                    path_ext(e.name) == ".jpg")
                    found.emplace_back(nm, e.path);
            }
            std::sort(found.begin(), found.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            for (auto& f : found) result.push_back(f.second);
            if (!result.empty()) return result;
        }
    }

    // ─── Fallback: ImageMagick (magick.exe or magick) ───
    // Output: <out_dir>/page-0.jpg, page-1.jpg, ...
    {
        knst_c16string prefix = knst_file::join_path(out_dir, u"magick_");
        const char* prog = "magick";     // Windows: magick.exe
        knst_vector<knst_c16string> args;
        args.push_back(u"-density");
        args.push_back(from_u8(std::to_string(target_dpi)));
        args.push_back(knst_file::resolve_path(pdf_path));
        args.push_back(u"-quality");
        args.push_back(from_u8(std::to_string(jpeg_quality)));
        knst_c16string pattern = prefix; pattern.append(u"%d.jpg");
        args.push_back(pattern);
        auto res = knst_process::run_capture(knst_c16string(prog), args, {}, nullptr, 0, 180000);
        if (res.error == knst_process_error::None && res.exit_code == 0) {
            knst_vector<knst_file> entries;
            knst_file::list_directory(out_dir, entries);
            std::vector<std::pair<int, knst_c16string>> found;
            for (auto& e : entries) {
                std::string nm = to_u8(e.name);
                if (!e.is_file || e.size == 0) continue;
                if (nm.compare(0, 7, "magick_") != 0) continue;
                if (path_ext(e.name) != ".jpg") continue;
                int n = 0;
                size_t p = 7;
                while (p < nm.size() && nm[p] >= '0' && nm[p] <= '9') { n = n * 10 + (nm[p] - '0'); ++p; }
                found.emplace_back(n, e.path);
            }
            std::sort(found.begin(), found.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            for (auto& f : found) result.push_back(f.second);
            if (!result.empty()) return result;
        }
    }

    return result;   // empty = neither tool available or both failed
}
#endif // !__ANDROID__

} // namespace detail

// ===========================================================================
//                Document model  (shared by PDF writer and GDI renderer)
// ===========================================================================
namespace detail {

enum class font_family : uint8_t { Helvetica = 0, Courier, Times };

// Coordinates are in PDF points (1/72 in), origin = TOP-LEFT of the page.
struct item {
    enum kind_t : uint8_t { Text, Line, Rect, FillRect, Image };
    kind_t       kind = Text;
    float        x1 = 0, y1 = 0, x2 = 0, y2 = 0;   // Text: x1,y1 = baseline start
    float        lw = 0.5f;
    uint8_t      r = 0, g = 0, b = 0;
    std::u16string text;
    font_family  fam = font_family::Helvetica;
    bool         bold = false, italic = false;
    float        size = 12.0f;
    uint32_t     image = 0;                         // index into document::images
    bool         flip = false;                      // mirror horizontally first ...
    uint8_t      rot = 0;                           // ... then rotate N * 90 deg clockwise
    bool         clip = false;
    float        cx1 = 0, cy1 = 0, cx2 = 0, cy2 = 0;
};

struct page { std::vector<item> items; };

struct image_ref {
    knst_c16string path;
    uint32_t w = 0, h = 0;
    uint8_t  kind = 0;            // 0 = decode with knst_image_loader, 1 = JPEG passthrough, 2 = PNM
    uint8_t  comps = 3;
    bool     cmyk_inverted = false;
    uint8_t  exif = 1;
    double   scale_need = 0.0;    // fraction of the source resolution that is really needed
};

struct document {
    float w_pt = 595.276f, h_pt = 841.890f;
    float max_dpi = 300.0f;
    std::vector<page>      pages;
    std::vector<image_ref> images;
    knst_c16string embed_font_path;  // empty = WinAnsi, non-empty = TTF embed
    bool           auto_fallback = false;
};

// ---- text metrics / encoding ---------------------------------------------
// PDF text is written as WinAnsi with a few slots remapped to Turkish glyphs.
inline uint8_t to_pdf_byte(char16_t c) {
    if (c >= 32 && c < 127) return static_cast<uint8_t>(c);
    switch (c) {
        case 0x011E: return 0xD0; case 0x011F: return 0xF0;    // G breve
        case 0x0130: return 0xDD; case 0x0131: return 0xFD;    // dotted I / dotless i
        case 0x015E: return 0xDE; case 0x015F: return 0xFE;    // S cedilla
        case 0x20AC: return 0x80; case 0x2026: return 0x85;
        case 0x2018: return 0x91; case 0x2019: return 0x92;
        case 0x201C: return 0x93; case 0x201D: return 0x94;
        case 0x2022: return 0x95; case 0x2013: return 0x96; case 0x2014: return 0x97;
        case 0x2122: return 0x99; case 0x0152: return 0x8C; case 0x0153: return 0x9C;
        case 0x0160: return 0x8A; case 0x0161: return 0x9A;
        case 0x017D: return 0x8E; case 0x017E: return 0x9E;
        case 0x0178: return 0x9F; case 0x0192: return 0x83;
        case 0x00D0: return 'D';  case 0x00F0: return 'd';     // slots reused for Turkish
        case 0x00DD: return 'Y';  case 0x00FD: return 'y';
        case 0x00DE: return 'P';  case 0x00FE: return 'p';
        default: break;
    }
    if (c >= 0xA0 && c <= 0xFF) return static_cast<uint8_t>(c);
    return '?';
}
inline bool is_low_surrogate(char16_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

inline float glyph_units(uint8_t b, font_family f, bool bold) {
    if (f == font_family::Courier) return 600.0f;
    static const uint16_t R[95] = {
        278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
        556,556,556,556,556,556,556,556,556,556,
        278,278,584,584,584,556,1015,
        667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,778,722,667,611,722,667,944,667,667,611,
        278,278,278,469,556,333,
        556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,556,556,333,500,278,556,500,722,500,500,500,
        334,260,334,584 };
    static const uint16_t B[95] = {
        278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,
        556,556,556,556,556,556,556,556,556,556,
        333,333,584,584,584,611,975,
        722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,667,778,722,667,611,722,667,944,667,667,611,
        333,278,333,584,556,333,
        556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,611,611,389,556,333,611,556,778,556,556,500,
        389,280,389,584 };
    float u;
    if (b >= 32 && b <= 126) u = bold ? B[b - 32] : R[b - 32];
    else switch (b) {
        case 0xD0: u = 778; break;  case 0xF0: u = 556; break;
        case 0xDD: u = 278; break;  case 0xFD: u = 278; break;
        case 0xDE: u = 667; break;  case 0xFE: u = 500; break;
        default: u = (b >= 0xC0 && b <= 0xDF) ? 700.0f : (b >= 0xE0 ? 550.0f : 556.0f); break;
    }
    return (f == font_family::Times) ? u * 0.92f : u;     // Times widths are approximated
}
inline float text_width(const std::u16string& s, float size, font_family f, bool bold) {
    float w = 0;
    for (char16_t c : s) if (!is_low_surrogate(c)) w += glyph_units(to_pdf_byte(c), f, bold);
    return w * size / 1000.0f;
}
inline std::u16string sanitize(const std::u16string& in) {
    std::u16string o; o.reserve(in.size());
    for (char16_t c : in) {
        if (c == u'\r') continue;
        if (c == u'\t') { o.append(4, u' '); continue; }
        if (c == u'\n') { o.push_back(c); continue; }
        if (c < 32 || c == 0x7F) continue;
        o.push_back(c);
    }
    return o;
}
inline font_family fam_of(const knst_c16string& name, bool& bold, bool& italic) {
    std::string n = lower(to_u8(name));
    if (n.find("bold") != std::string::npos) bold = true;
    if (n.find("oblique") != std::string::npos || n.find("italic") != std::string::npos) italic = true;
    if (n.find("courier") != std::string::npos || n.find("mono") != std::string::npos ||
        n.find("consolas") != std::string::npos) return font_family::Courier;
    if (n.find("times") != std::string::npos || (n.find("serif") != std::string::npos &&
        n.find("sans") == std::string::npos)) return font_family::Times;
    return font_family::Helvetica;
}

inline item mk_text(float x, float y, const std::u16string& s, float size, font_family f, bool bold,
                    bool italic, uint8_t r, uint8_t g, uint8_t b) {
    item it; it.kind = item::Text; it.x1 = x; it.y1 = y; it.text = s; it.size = size;
    it.fam = f; it.bold = bold; it.italic = italic; it.r = r; it.g = g; it.b = b;
    return it;
}
inline item mk_shape(item::kind_t k, float x1, float y1, float x2, float y2, float lw,
                     uint8_t r, uint8_t g, uint8_t b) {
    item it; it.kind = k; it.x1 = x1; it.y1 = y1; it.x2 = x2; it.y2 = y2; it.lw = lw;
    it.r = r; it.g = g; it.b = b;
    return it;
}
inline std::u16string widen(const std::string& s) { return std::u16string(s.begin(), s.end()); }

inline std::vector<std::u16string> wrap_text(const std::u16string& s, float size, font_family f,
                                             bool bold, float max_w) {
    std::vector<std::u16string> out;
    std::u16string cur; float cur_w = 0;
    auto rtrim = [](std::u16string x) { while (!x.empty() && x.back() == u' ') x.pop_back(); return x; };
    size_t i = 0;
    while (i < s.size()) {
        size_t j = i; bool sp = (s[i] == u' ');
        while (j < s.size() && ((s[j] == u' ') == sp)) ++j;
        std::u16string tok = s.substr(i, j - i);
        float tw = text_width(tok, size, f, bold);
        if (cur_w + tw <= max_w) { cur += tok; cur_w += tw; }
        else if (sp) { out.push_back(rtrim(cur)); cur.clear(); cur_w = 0; }
        else {
            if (!cur.empty()) { out.push_back(rtrim(cur)); cur.clear(); cur_w = 0; }
            if (tw <= max_w) { cur = tok; cur_w = tw; }
            else {
                for (char16_t ch : tok) {
                    float w = is_low_surrogate(ch) ? 0.0f : glyph_units(to_pdf_byte(ch), f, bold) * size / 1000.0f;
                    if (cur_w + w > max_w && !cur.empty()) { out.push_back(cur); cur.clear(); cur_w = 0; }
                    cur.push_back(ch); cur_w += w;
                }
            }
        }
        i = j;
    }
    if (!cur.empty() || out.empty()) out.push_back(rtrim(cur));
    return out;
}

// ---- rich text -> pages ---------------------------------------------------
inline void layout_rich(const rich_document& rd, document& d) {
    const float M = kMm2Pt;
    d.w_pt = rd.page_w_mm * M; d.h_pt = rd.page_h_mm * M;
    d.embed_font_path = rd.embed_font_path;
    d.auto_fallback   = rd.auto_fallback;
    const float ml = rd.margin_left_mm * M, mr = rd.margin_right_mm * M;
    const float mt = rd.margin_top_mm * M,  mb = rd.margin_bottom_mm * M;
    const float cw = std::max(20.0f, d.w_pt - ml - mr);
    const float top = mt, bottom = d.h_pt - mb;
    d.pages.clear(); d.pages.emplace_back();
    float cy = top;

    for (uint32_t ei = 0; ei < rd.elements.size(); ++ei) {
        const text_run& run = rd.elements[ei];
        if (run.text.empty()) continue;
        bool bold = run.style.bold, ital = run.style.italic;
        font_family fam = fam_of(run.style.font, bold, ital);
        float size = std::min(200.0f, std::max(4.0f, run.style.size_pt));
        float lh = size * std::max(0.8f, run.style.line_spacing);
        std::u16string text = sanitize(to_u16(run.text));
        size_t start = 0;
        for (;;) {
            size_t nl = text.find(u'\n', start);
            std::u16string para = text.substr(start, nl == std::u16string::npos ? std::u16string::npos : nl - start);
            std::vector<std::u16string> lines = wrap_text(para, size, fam, bold, cw);
            for (auto& line : lines) {
                if (cy + lh > bottom && cy > top + 0.01f) { d.pages.emplace_back(); cy = top; }
                if (!line.empty()) {
                    float w = text_width(line, size, fam, bold), x = ml;
                    if (run.style.align == alignment::Center) x = ml + (cw - w) * 0.5f;
                    else if (run.style.align == alignment::Right) x = ml + cw - w;
                    d.pages.back().items.push_back(mk_text(x, cy + size * 0.9f, line, size, fam, bold, ital,
                                                           run.style.r, run.style.g, run.style.b));
                }
                cy += lh;
            }
            if (nl == std::u16string::npos) break;
            start = nl + 1;
        }
        if (d.pages.size() > 5000) break;      // runaway guard
    }

    // shapes (below text) -> make sure the target pages exist
    uint32_t want = static_cast<uint32_t>(d.pages.size());
    for (uint32_t i = 0; i < rd.shapes.size(); ++i)
        if (rd.shapes[i].page > want && rd.shapes[i].page <= 1000) want = rd.shapes[i].page;
    while (d.pages.size() < want) d.pages.emplace_back();

    const std::u16string hdr = to_u16(rd.header_text), ftr = to_u16(rd.footer_text);
    const uint32_t total = static_cast<uint32_t>(d.pages.size());
    for (uint32_t pi = 1; pi <= total; ++pi) {
        page& pg = d.pages[pi - 1];
        std::vector<item> head;
        for (uint32_t i = 0; i < rd.shapes.size(); ++i) {
            const draw_command& s = rd.shapes[i];
            if (s.page != 0 && s.page != pi) continue;
            item::kind_t k = s.type == shape_type::Line ? item::Line
                           : s.type == shape_type::Rect ? item::Rect : item::FillRect;
            head.push_back(mk_shape(k, s.x1_mm * M, s.y1_mm * M, s.x2_mm * M, s.y2_mm * M,
                                    s.line_width, s.r, s.g, s.b));
        }
        head.insert(head.end(), pg.items.begin(), pg.items.end());
        pg.items.swap(head);
        const float hy = std::max(10.0f, mt * 0.55f), fy = d.h_pt - std::max(6.0f, mb * 0.45f);
        if (!hdr.empty()) pg.items.push_back(mk_text(ml, hy, hdr, 9, font_family::Helvetica, false, false, 96, 96, 96));
        if (rd.show_date) {
            std::u16string ds = widen(now_string().substr(0, 10));
            float w = text_width(ds, 9, font_family::Helvetica, false);
            pg.items.push_back(mk_text(d.w_pt - mr - w, hy, ds, 9, font_family::Helvetica, false, false, 96, 96, 96));
        }
        if (!ftr.empty()) pg.items.push_back(mk_text(ml, fy, ftr, 9, font_family::Helvetica, false, false, 96, 96, 96));
        if (rd.show_page_number) {
            std::u16string pn = widen(std::to_string(pi) + " / " + std::to_string(total));
            float w = text_width(pn, 9, font_family::Helvetica, false);
            pg.items.push_back(mk_text((d.w_pt - w) * 0.5f, fy, pn, 9, font_family::Helvetica, false, false, 128, 128, 128));
        }
    }
}

inline void layout_plain_text(const knst_c16string& text, float w_mm, float h_mm, document& d) {
    rich_document rd;
    rd.page_w_mm = w_mm; rd.page_h_mm = h_mm;
    rd.margin_left_mm = rd.margin_right_mm = 18.0f;
    rd.margin_top_mm = rd.margin_bottom_mm = 18.0f;
    text_run r; r.text = text;
    r.style.font = u"Courier"; r.style.size_pt = 10.0f; r.style.line_spacing = 1.15f;
    rd.elements.push_back(r);
    layout_rich(rd, d);
}

// ---- image probing / decoding --------------------------------------------
inline uint32_t be16(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 8) | p[1]; }
inline uint32_t le16(const uint8_t* p) { return (static_cast<uint32_t>(p[1]) << 8) | p[0]; }
inline uint32_t be32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
inline uint32_t le32(const uint8_t* p) { return (static_cast<uint32_t>(p[3]) << 24) | (p[2] << 16) | (p[1] << 8) | p[0]; }

inline uint8_t exif_orientation(const uint8_t* p, size_t n) {
    if (n < 14 || std::memcmp(p, "Exif\0\0", 6) != 0) return 1;
    const uint8_t* t = p + 6; size_t tn = n - 6;
    bool le = (t[0] == 'I' && t[1] == 'I');
    if (!le && !(t[0] == 'M' && t[1] == 'M')) return 1;
    auto r16 = [&](size_t o) -> uint32_t { return le ? le16(t + o) : be16(t + o); };
    auto r32 = [&](size_t o) -> uint32_t { return le ? le32(t + o) : be32(t + o); };
    if (tn < 8) return 1;
    uint32_t ifd = r32(4);
    if (static_cast<size_t>(ifd) + 2 > tn) return 1;
    uint32_t cnt = r16(ifd);
    for (uint32_t i = 0; i < cnt; ++i) {
        size_t e = static_cast<size_t>(ifd) + 2 + static_cast<size_t>(i) * 12;
        if (e + 12 > tn) break;
        if (r16(e) == 0x0112) { uint32_t v = r16(e + 8); return (v >= 1 && v <= 8) ? static_cast<uint8_t>(v) : 1; }
    }
    return 1;
}

inline bool probe_jpeg(const uint8_t* b, size_t n, image_ref& r) {
    if (n < 4 || b[0] != 0xFF || b[1] != 0xD8) return false;
    size_t i = 2; bool adobe = false; uint8_t ori = 1; bool have_ori = false;
    while (i + 4 <= n) {
        if (b[i] != 0xFF) { ++i; continue; }
        uint8_t m = b[i + 1];
        if (m == 0xFF) { ++i; continue; }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        if (m == 0xD9 || m == 0xDA) break;
        size_t len = be16(b + i + 2);
        if (len < 2 || i + 2 + len > n) break;
        const uint8_t* seg = b + i + 4; size_t sl = len - 2;
        if (m == 0xE1 && !have_ori) { ori = exif_orientation(seg, sl); have_ori = true; }
        else if (m == 0xEE && sl >= 5 && std::memcmp(seg, "Adobe", 5) == 0) adobe = true;
        else if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            if (sl < 6) break;
            r.h = be16(seg + 1); r.w = be16(seg + 3); r.comps = seg[5];
            bool dct = (m == 0xC0 || m == 0xC1 || m == 0xC2);
            r.kind = (seg[0] == 8 && dct && (r.comps == 1 || r.comps == 3 || r.comps == 4)) ? 1 : 0;
            r.cmyk_inverted = adobe && r.comps == 4;
            r.exif = (r.kind == 1) ? ori : 1;
            return r.w > 0 && r.h > 0;
        }
        i += 2 + len;
    }
    return false;
}

inline bool parse_pnm_header(const uint8_t* b, size_t n, uint32_t& w, uint32_t& h, uint32_t& maxv,
                             size_t& data_off, int& type) {
    if (n < 8 || b[0] != 'P' || (b[1] != '5' && b[1] != '6')) return false;
    type = b[1] - '0';
    size_t i = 2; uint32_t v[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k) {
        for (;;) {
            while (i < n && std::isspace(b[i])) ++i;
            if (i < n && b[i] == '#') { while (i < n && b[i] != '\n') ++i; } else break;
        }
        if (i >= n || !std::isdigit(b[i])) return false;
        uint64_t x = 0;
        while (i < n && std::isdigit(b[i])) { x = x * 10 + (b[i] - '0'); if (x > 0xFFFFFFF) return false; ++i; }
        v[k] = static_cast<uint32_t>(x);
    }
    if (i >= n) return false;
    data_off = i + 1; w = v[0]; h = v[1]; maxv = v[2];
    return w > 0 && h > 0 && maxv > 0 && maxv <= 65535;
}

inline bool decode_rgb(const knst_byte_string& bytes, knst_byte_string& rgb, uint32_t& w, uint32_t& h) {
    const uint8_t* p = bytes.data(); size_t n = bytes.length();
    uint32_t pw, ph, maxv; size_t off; int type;
    if (parse_pnm_header(p, n, pw, ph, maxv, off, type)) {
        size_t bps = maxv > 255 ? 2 : 1, ch = (type == 6) ? 3 : 1;
        if (static_cast<uint64_t>(pw) * ph * ch * bps > n - off) return false;
        rgb.resize(static_cast<size_t>(pw) * ph * 3);
        uint8_t* o = &rgb[0]; const uint8_t* s = p + off;
        for (size_t px = 0; px < static_cast<size_t>(pw) * ph; ++px) {
            for (size_t c = 0; c < 3; ++c) {
                size_t sc = (ch == 3) ? c : 0;
                o[px * 3 + c] = s[(px * ch + sc) * bps];     // high byte for 16-bit samples
            }
        }
        w = pw; h = ph;
        return true;
    }
    knst_image_load_options opt;
    opt.output_format = KNST_BITMAP_OUTPUT_RGB;
    opt.max_threads   = 1;
    knst_image img = knst_image_loader::load_from_memory(bytes.data(), bytes.length(), opt);
    if (!img.valid()) return false;
    if (img.pixels.length() < static_cast<size_t>(img.width) * img.height * 3) return false;
    rgb = img.pixels; w = img.width; h = img.height;
    return true;
}

inline void downscale_rgb(const knst_byte_string& src, uint32_t w, uint32_t h,
                          uint32_t nw, uint32_t nh, knst_byte_string& dst) {
    dst.resize(static_cast<size_t>(nw) * nh * 3);
    uint8_t* d = &dst[0]; const uint8_t* s = src.data();
    for (uint32_t y = 0; y < nh; ++y) {
        uint64_t y0 = static_cast<uint64_t>(y) * h / nh;
        uint64_t y1 = std::max<uint64_t>(y0 + 1, static_cast<uint64_t>(y + 1) * h / nh);
        for (uint32_t x = 0; x < nw; ++x) {
            uint64_t x0 = static_cast<uint64_t>(x) * w / nw;
            uint64_t x1 = std::max<uint64_t>(x0 + 1, static_cast<uint64_t>(x + 1) * w / nw);
            uint64_t sr = 0, sg = 0, sb = 0, cnt = 0;
            for (uint64_t yy = y0; yy < y1 && yy < h; ++yy)
                for (uint64_t xx = x0; xx < x1 && xx < w; ++xx) {
                    const uint8_t* q = s + (yy * w + xx) * 3;
                    sr += q[0]; sg += q[1]; sb += q[2]; ++cnt;
                }
            if (!cnt) cnt = 1;
            uint8_t* o = d + (static_cast<size_t>(y) * nw + x) * 3;
            o[0] = static_cast<uint8_t>(sr / cnt); o[1] = static_cast<uint8_t>(sg / cnt); o[2] = static_cast<uint8_t>(sb / cnt);
        }
    }
}

// mirror (first) then rotate `rot` quarter turns clockwise
inline void transform_rgb(const knst_byte_string& src, uint32_t w, uint32_t h, bool flip, int rot,
                          knst_byte_string& dst, uint32_t& nw, uint32_t& nh) {
    rot &= 3;
    nw = (rot & 1) ? h : w; nh = (rot & 1) ? w : h;
    dst.resize(static_cast<size_t>(nw) * nh * 3);
    uint8_t* d = &dst[0]; const uint8_t* s = src.data();
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t px = flip ? (w - 1 - x) : x, py = y, cw = w, ch = h;
            for (int k = 0; k < rot; ++k) { uint32_t nx = ch - 1 - py, ny = px; px = nx; py = ny; std::swap(cw, ch); }
            const uint8_t* q = s + (static_cast<size_t>(y) * w + x) * 3;
            uint8_t* o = d + (static_cast<size_t>(py) * nw + px) * 3;
            o[0] = q[0]; o[1] = q[1]; o[2] = q[2];
        }
}

inline bool probe_image(const knst_c16string& path, image_ref& r, status& st) {
    r = image_ref(); r.path = path;
    knst_file_error fe = knst_file_error::None;
    knst_byte_string b = knst_file::read_file_data<>(path, &fe);
    if (fe != knst_file_error::None) { st = (fe == knst_file_error::NotFound) ? status::NotFound : status::IoError; return false; }
    if (b.length() < 8) { st = status::DecodeFailed; return false; }
    const uint8_t* p = b.data(); size_t n = b.length();
    if (probe_jpeg(p, n, r)) return true;
    uint32_t mv; size_t off; int type; uint32_t pw, ph;
    if (parse_pnm_header(p, n, pw, ph, mv, off, type)) { r.kind = 2; r.w = pw; r.h = ph; return true; }
    if (n >= 24 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) {
        r.w = be32(p + 16); r.h = be32(p + 20);
        if (r.w && r.h) return true;
    } else if (n >= 26 && p[0] == 'B' && p[1] == 'M') {
        int32_t bh = static_cast<int32_t>(le32(p + 22));
        r.w = le32(p + 18); r.h = static_cast<uint32_t>(bh < 0 ? -bh : bh);
        if (r.w && r.h) return true;
    } else if (n >= 10 && std::memcmp(p, "GIF8", 4) == 0) {
        r.w = le16(p + 6); r.h = le16(p + 8);
        if (r.w && r.h) return true;
    }
    knst_byte_string rgb; uint32_t w, h;                  // WEBP, TIFF, HEIC ... : decode once for the size
    if (!decode_rgb(b, rgb, w, h)) { st = status::DecodeFailed; return false; }
    r.w = w; r.h = h;
    return true;
}

// ---- photo batch -> pages -------------------------------------------------
inline knst_prn::status layout_images(const std::vector<knst_c16string>& paths, const image_batch_options& bo,
                            float page_w_mm, float page_h_mm, document& d) {
    const float M = kMm2Pt;
    d.w_pt = page_w_mm * M; d.h_pt = page_h_mm * M; d.max_dpi = static_cast<float>(std::max<uint32_t>(72, bo.max_dpi));
    d.pages.clear(); d.images.clear();
    const uint32_t rows = std::min<uint32_t>(8, std::max<uint32_t>(1, bo.rows));
    const uint32_t cols = std::min<uint32_t>(8, std::max<uint32_t>(1, bo.cols));
    const float margin = std::max(0.0f, bo.margin_mm) * M, gap = std::max(0.0f, bo.gap_mm) * M;
    const float cell_w = (d.w_pt - 2 * margin - (cols - 1) * gap) / cols;
    const float cell_h = (d.h_pt - 2 * margin - (rows - 1) * gap) / rows;
    if (cell_w < 20.0f || cell_h < 20.0f) return status::InvalidArgument;

    knst_prn::status first_err = status::NotFound;
    for (auto& p : paths) {
        image_ref r; knst_prn::status st = status::Ok;
        if (probe_image(knst_file::resolve_path(p), r, st)) { d.images.push_back(r); continue; }
        if (!bo.skip_unreadable) return st;
        first_err = st;
        if (bo.skipped) bo.skipped->push_back(p);
    }
    if (d.images.empty()) return first_err;

    const uint32_t per_page = rows * cols, copies = std::max<uint32_t>(1, bo.copies_each);
    const float cap_h = bo.show_filename ? 11.0f : 0.0f;
    const float pic_h = cell_h - cap_h;
    uint32_t k = 0;
    for (uint32_t idx = 0; idx < d.images.size(); ++idx) {
        for (uint32_t c = 0; c < copies; ++c, ++k) {
            uint32_t cell = k % per_page;
            if (cell == 0) d.pages.emplace_back();
            const float x = margin + (cell % cols) * (cell_w + gap);
            const float y = margin + (cell / cols) * (cell_h + gap);
            image_ref& r = d.images[idx];
            uint8_t o = bo.apply_exif ? r.exif : 1;
            bool flip = (o == 2 || o == 4 || o == 5 || o == 7);
            int rot = (o == 3 || o == 4) ? 2 : (o == 6 || o == 7) ? 1 : (o == 5 || o == 8) ? 3 : 0;
            float dw = (rot & 1) ? static_cast<float>(r.h) : static_cast<float>(r.w);
            float dh = (rot & 1) ? static_cast<float>(r.w) : static_cast<float>(r.h);
            if (bo.rotate_to_fit && bo.fit != fit_mode::Stretch && std::fabs(dw - dh) > 0.02f * std::max(dw, dh) &&
                ((cell_w >= pic_h) != (dw >= dh))) {
                rot = (rot + 1) & 3; std::swap(dw, dh);
            }
            float w = cell_w, h = pic_h, ox = x, oy = y;
            if (bo.fit != fit_mode::Stretch) {
                float s = (bo.fit == fit_mode::Fit) ? std::min(cell_w / dw, pic_h / dh) : std::max(cell_w / dw, pic_h / dh);
                w = dw * s; h = dh * s; ox = x + (cell_w - w) * 0.5f; oy = y + (pic_h - h) * 0.5f;
            }
            item it; it.kind = item::Image; it.image = idx; it.flip = flip; it.rot = static_cast<uint8_t>(rot);
            it.x1 = ox; it.y1 = oy; it.x2 = ox + w; it.y2 = oy + h;
            if (bo.fit == fit_mode::Fill) { it.clip = true; it.cx1 = x; it.cy1 = y; it.cx2 = x + cell_w; it.cy2 = y + pic_h; }
            d.pages.back().items.push_back(it);
            double need = std::max(w * d.max_dpi / 72.0 / dw, h * d.max_dpi / 72.0 / dh);
            r.scale_need = std::max(r.scale_need, need);
            if (bo.border_mm > 0.0f) {
                float lw = std::max(0.25f, bo.border_mm * M);
                d.pages.back().items.push_back(it.clip
                    ? mk_shape(item::Rect, x, y, x + cell_w, y + pic_h, lw, 0, 0, 0)
                    : mk_shape(item::Rect, ox, oy, ox + w, oy + h, lw, 0, 0, 0));
            }
            if (bo.show_filename) {
                std::string nm = to_u8(knst_file::get_filename(r.path));
                std::u16string cap = widen(nm);
                if (!cap.empty()) {
                    while (cap.size() > 4 && text_width(cap, 7, font_family::Helvetica, false) > cell_w) {
                        cap.resize(cap.size() - 4); cap += u"...";
                    }
                    float cw = text_width(cap, 7, font_family::Helvetica, false);
                    d.pages.back().items.push_back(mk_text(x + (cell_w - cw) * 0.5f, y + cell_h - 2.5f, cap, 7,
                                                           font_family::Helvetica, false, false, 90, 90, 90));
                }
            }
        }
    }
    return status::Ok;
}

// ---- test pages -----------------------------------------------------------
inline void make_test_doc(test_page_type type, const std::string& printer, float w_mm, float h_mm, document& d) {
    const float M = kMm2Pt;
    d.w_pt = w_mm * M; d.h_pt = h_mm * M; d.pages.clear(); d.pages.emplace_back();
    std::vector<item>& v = d.pages[0].items;
    const font_family H = font_family::Helvetica;
    auto mm = [&](float x) { return x * M; };
    auto T = [&](float xm, float ym, const std::u16string& s, float sz, bool bold = false,
                 font_family f = font_family::Helvetica, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) {
        v.push_back(mk_text(mm(xm), mm(ym), s, sz, f, bold, false, r, g, b));
    };
    auto L = [&](float x1, float y1, float x2, float y2, float lw, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) {
        v.push_back(mk_shape(item::Line, mm(x1), mm(y1), mm(x2), mm(y2), lw, r, g, b));
    };
    auto R = [&](float x1, float y1, float x2, float y2, float lw, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) {
        v.push_back(mk_shape(item::Rect, mm(x1), mm(y1), mm(x2), mm(y2), lw, r, g, b));
    };
    auto F = [&](float x1, float y1, float x2, float y2, uint8_t r, uint8_t g, uint8_t b) {
        v.push_back(mk_shape(item::FillRect, mm(x1), mm(y1), mm(x2), mm(y2), 0, r, g, b));
    };
    auto head = [&](const char* title) {
        T(15, 20, widen(title), 18, true);
        T(15, 27, widen("Printer: " + printer + "    " + now_string() + "    " +
                        std::to_string(static_cast<int>(w_mm + 0.5f)) + " x " +
                        std::to_string(static_cast<int>(h_mm + 0.5f)) + " mm"), 9, false, H, 90, 90, 90);
        L(15, 30, w_mm - 15, 30, 0.6f);
    };
    struct col { const char* n; uint8_t r, g, b; };
    static const col cols[7] = { {"Cyan",0,255,255}, {"Magenta",255,0,255}, {"Yellow",255,255,0}, {"Black",0,0,0},
                                 {"Red",255,0,0}, {"Green",0,255,0}, {"Blue",0,0,255} };

    switch (type) {
    case test_page_type::ColorBar: {
        head("KNST color bars");
        float bw = (w_mm - 30.0f) / 7.0f;
        for (int i = 0; i < 7; ++i) {
            F(15 + i * bw, 40, 15 + (i + 1) * bw, 120, cols[i].r, cols[i].g, cols[i].b);
            T(15 + i * bw + 2, 126, widen(cols[i].n), 8);
        }
        for (int i = 0; i <= 10; ++i) {                      // gray ramp
            uint8_t g = static_cast<uint8_t>(255 - i * 25);
            F(15 + i * ((w_mm - 30) / 11), 140, 15 + (i + 1) * ((w_mm - 30) / 11), 170, g, g, g);
        }
        T(15, 177, u"Gray ramp 100% .. 0%", 8);
        break; }
    case test_page_type::Alignment: {
        head("KNST alignment");
        R(5, 5, w_mm - 5, h_mm - 5, 0.5f);
        const float ar[4][2] = {{10,10},{w_mm-10,10},{10,h_mm-10},{w_mm-10,h_mm-10}};
        for (auto& c : ar) { L(c[0]-6, c[1], c[0]+6, c[1], 0.5f); L(c[0], c[1]-6, c[0], c[1]+6, 0.5f); }
        L(w_mm/2-8, h_mm/2, w_mm/2+8, h_mm/2, 0.5f); L(w_mm/2, h_mm/2-8, w_mm/2, h_mm/2+8, 0.5f);
        for (int x = 20; x < static_cast<int>(w_mm) - 10; x += 10) {
            L(static_cast<float>(x), 34, static_cast<float>(x), (x % 50 == 0) ? 40.0f : 37.0f, 0.4f);
            if (x % 50 == 0) T(static_cast<float>(x) - 3, 45, widen(std::to_string(x)), 7);
        }
        T(w_mm/2 - 40, h_mm/2 + 14, u"If the frame touches the paper edge, margins are clipped.", 8);
        break; }
    case test_page_type::Grid: {
        head("KNST grid (10 mm)");
        for (int x = 10; x <= static_cast<int>(w_mm) - 10; x += 10)
            L(static_cast<float>(x), 35, static_cast<float>(x), h_mm - 10, (x % 50 == 0) ? 0.5f : 0.2f, 100, 100, 100);
        for (int y = 40; y <= static_cast<int>(h_mm) - 10; y += 10)
            L(10, static_cast<float>(y), w_mm - 10, static_cast<float>(y), (y % 50 == 0) ? 0.5f : 0.2f, 100, 100, 100);
        break; }
    case test_page_type::Gradient: {
        head("KNST gradients");
        float x0 = 15, wid = w_mm - 30; const int steps = 128;
        for (int c = 0; c < 7; ++c) {
            float y = 38.0f + c * 26.0f;
            for (int s = 0; s < steps; ++s) {
                float t = static_cast<float>(s) / (steps - 1);
                uint8_t r = static_cast<uint8_t>(255 - (255 - cols[c].r) * t);
                uint8_t g = static_cast<uint8_t>(255 - (255 - cols[c].g) * t);
                uint8_t b = static_cast<uint8_t>(255 - (255 - cols[c].b) * t);
                F(x0 + wid * s / steps, y, x0 + wid * (s + 1) / steps + 0.2f, y + 16, r, g, b);
            }
            T(x0, y + 21, widen(cols[c].n), 7);
        }
        break; }
    case test_page_type::NozzleCheck: {
        head("KNST nozzle check");
        for (int c = 0; c < 4; ++c) {
            float y = 40.0f + c * 55.0f;
            T(15, y - 2, widen(cols[c].n), 8);
            for (int i = 0; i < 60; ++i) {
                float x = 15 + i * ((w_mm - 30) / 60.0f);
                L(x, y, x, y + 14, 0.4f, cols[c].r, cols[c].g, cols[c].b);
            }
            for (int i = 0; i < 40; ++i) {
                float x = 15 + i * ((w_mm - 30) / 40.0f), yy = y + 18 + (i % 10) * 1.2f;
                L(x, yy, x + (w_mm - 30) / 40.0f - 1, yy, 0.4f, cols[c].r, cols[c].g, cols[c].b);
            }
            F(15, y + 33, 55, y + 42, cols[c].r, cols[c].g, cols[c].b);
        }
        break; }
    default: {   // Diagnostic
        head("KNST diagnostic page");
        float y = 40;
        T(15, y, u"Helvetica: The quick brown fox jumps over the lazy dog 0123456789", 11); y += 8;
        v.push_back(mk_text(mm(15), mm(y), u"Helvetica Bold / Oblique: The quick brown fox", 11, H, true, true, 0, 0, 0)); y += 8;
        T(15, y, u"Courier: The quick brown fox jumps over the lazy dog", 10, false, font_family::Courier); y += 8;
        T(15, y, u"Times: The quick brown fox jumps over the lazy dog", 11, false, font_family::Times); y += 10;
        T(15, y, u"T\u00fcrk\u00e7e: \u00c7 \u011e \u0130 \u00d6 \u015e \u00dc  \u00e7 \u011f \u0131 \u00f6 \u015f \u00fc", 12, true); y += 8;
        T(15, y, u"Symbols: !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~  \u20ac \u00a9 \u00ae \u00b0", 10); y += 12;
        for (int i = 0; i <= 10; ++i) { uint8_t g = static_cast<uint8_t>(255 - i * 25); F(15 + i * 12.0f, y, 27 + i * 12.0f, y + 12, g, g, g); R(15 + i * 12.0f, y, 27 + i * 12.0f, y + 12, 0.2f); }
        y += 20;
        for (int i = 0; i < 7; ++i) F(15 + i * 18.0f, y, 31 + i * 18.0f, y + 14, cols[i].r, cols[i].g, cols[i].b);
        y += 24;
        for (int i = 0; i < 30; ++i) L(15, y + i * (0.4f + i * 0.03f), w_mm - 15, y + i * (0.4f + i * 0.03f), 0.15f);
        R(10, 10, w_mm - 10, h_mm - 10, 0.4f);
        break; }
    }
}

// ===========================================================================
//                               PDF writer
// ===========================================================================
class pdf_file {
public:
    explicit pdf_file(const knst_c16string& path) : path_(path) { f_ = open_file(path, true); }
    ~pdf_file() {
        if (f_) { std::fclose(f_); f_ = nullptr; }
        // If finish() was never called successfully, the file on disk is
        // incomplete (corrupt PDF). Remove it so callers cannot hand a
        // half-written file to a printer (data loss prevention).
        if (!finished_) knst_file::remove_file(path_);
    }
    pdf_file(const pdf_file&) = delete;
    pdf_file& operator=(const pdf_file&) = delete;
    bool ok() const { return f_ != nullptr && ok_; }
    void put(const void* p, size_t n) {
        if (!f_ || !ok_ || n == 0) return;
        if (std::fwrite(p, 1, n, f_) != n) ok_ = false;
        pos_ += n;
    }
    void put(const std::string& s) { put(s.data(), s.size()); }
    uint32_t reserve() { xref_.push_back(0); return static_cast<uint32_t>(xref_.size()); }
    void begin(uint32_t id) { xref_[id - 1] = pos_; put(std::to_string(id) + " 0 obj\n"); }
    void end() { put(std::string("\nendobj\n")); }
    bool finish(uint32_t root, uint32_t info) {
        uint64_t xp = pos_;
        put("xref\n0 " + std::to_string(xref_.size() + 1) + "\n0000000000 65535 f \n");
        for (uint64_t off : xref_) {
            char b[32]; std::snprintf(b, sizeof(b), "%010llu 00000 n \n", static_cast<unsigned long long>(off));
            put(std::string(b));
        }
        put("trailer\n<< /Size " + std::to_string(xref_.size() + 1) + " /Root " + std::to_string(root) +
            " 0 R /Info " + std::to_string(info) + " 0 R >>\nstartxref\n" + std::to_string(xp) + "\n%%EOF\n");
        bool good = ok_;
        if (f_) { if (std::fclose(f_) != 0) good = false; f_ = nullptr; }
        if (good) finished_ = true;
        return good;
    }
private:
    knst_c16string path_;
    std::FILE* f_ = nullptr;
    bool ok_ = true;
    bool finished_ = false;
    uint64_t pos_ = 0;
    std::vector<uint64_t> xref_;
};
inline void image_matrix(const item& it, float page_h, float m[6]) {
    auto map = [&](double sx, double sy, double& X, double& Y) {
        double x = sx, y = sy;
        if (it.flip) x = 1.0 - x;
        for (int k = 0; k < it.rot; ++k) { double nx = 1.0 - y, ny = x; x = nx; y = ny; }
        X = it.x1 + x * (it.x2 - it.x1);
        Y = page_h - (it.y1 + y * (it.y2 - it.y1));
    };
    double X00, Y00, X10, Y10, X01, Y01;
    map(0, 1, X00, Y00); map(1, 1, X10, Y10); map(0, 0, X01, Y01);   // PDF image space (u,v) -> (sx = u, sy = 1 - v)
    m[0] = static_cast<float>(X10 - X00); m[1] = static_cast<float>(Y10 - Y00);
    m[2] = static_cast<float>(X01 - X00); m[3] = static_cast<float>(Y01 - Y00);
    m[4] = static_cast<float>(X00);       m[5] = static_cast<float>(Y00);
}

inline void pdf_rgb(std::string& c, uint8_t r, uint8_t g, uint8_t b, const char* op) {
    put_num(c, r / 255.0); c += ' '; put_num(c, g / 255.0); c += ' '; put_num(c, b / 255.0); c += ' '; c += op; c += '\n';
}

inline knst_prn::status write_image_object(pdf_file& pf, uint32_t id, const image_ref& r) {
    knst_file_error fe = knst_file_error::None;
    knst_byte_string bytes = knst_file::read_file_data<>(r.path, &fe);
    if (fe != knst_file_error::None || bytes.empty()) return status::IoError;
    std::string d = "<< /Type /XObject /Subtype /Image /BitsPerComponent 8 ";
    // JPEG passthrough only when the source resolution is close to what's
    // actually needed. Otherwise the PDF blows up (16-photo batch:
    // 16 × 2 MB JPEG = 32 MB PDF vs 3 MB after downscale).
    if (r.kind == 1 && (r.scale_need <= 0.0 || r.scale_need >= 0.5)) {
        d += "/Width " + std::to_string(r.w) + " /Height " + std::to_string(r.h) + " /ColorSpace ";
        d += (r.comps == 1) ? "/DeviceGray" : (r.comps == 4 ? "/DeviceCMYK" : "/DeviceRGB");
        if (r.cmyk_inverted) d += " /Decode [1 0 1 0 1 0 1 0]";
        d += " /Filter /DCTDecode /Length " + std::to_string(bytes.length()) + " >>\nstream\n";
        pf.begin(id); pf.put(d); pf.put(bytes.data(), bytes.length()); pf.put(std::string("\nendstream")); pf.end();
        return pf.ok() ? status::Ok : status::IoError;
    }
    // Fall through: decode + downscale for large JPEGs where scale_need < 0.5
    knst_byte_string rgb; uint32_t w = 0, h = 0;
    if (!decode_rgb(bytes, rgb, w, h)) {
        // Placeholder: 1x1 gray pixel so the PDF object is still valid and
        // the rest of the batch (other photos) survive. The caller can
        // detect skipped images via image_batch_options::skipped.
        rgb.resize(3);
        rgb[0] = 200; rgb[1] = 200; rgb[2] = 200;
        w = 1; h = 1;
    }
    bytes = knst_byte_string();
        if (r.scale_need > 0.0 && r.scale_need < 0.98) {
        uint32_t nw = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(w * r.scale_need)));
        uint32_t nh = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(h * r.scale_need)));
        knst_byte_string small_buf; downscale_rgb(rgb, w, h, nw, nh, small_buf);
        rgb = small_buf; w = nw; h = nh;
    }
        knst_byte_string img_compressed;
    if (!knst_deflate_zlib(rgb.data(), static_cast<size_t>(w) * h * 3, img_compressed))
        return status::IoError;
    d += "/Width " + std::to_string(w) + " /Height " + std::to_string(h) +
         " /ColorSpace /DeviceRGB /Filter /FlateDecode /Length " +
         std::to_string(img_compressed.length()) + " >>\nstream\n";
    pf.begin(id); pf.put(d); pf.put(img_compressed.data(), img_compressed.length());
    pf.put(std::string("\nendstream")); pf.end();
    return pf.ok() ? status::Ok : status::IoError;
}

inline knst_prn::status write_pdf(const document& d, const knst_c16string& path) {
    if (d.pages.empty()) return status::InvalidArgument;
    pdf_file pf(path);
    if (!pf.ok()) return status::IoError;
    // ─── Resolve + load TTF ───
    knst_ttf_font ttf;
    bool use_ttf = false;

    knst_c16string font_path = d.embed_font_path;
    knst_ttf_subset  subset;   // populated below if TTF is used

    // Auto-fallback: if enabled and no explicit path, scan text for Unicode
    if (font_path.empty() && d.auto_fallback) {
        bool needs_unicode = false;
        for (auto& pg : d.pages) {
            for (auto& it : pg.items) {
                if (it.kind != item::Text) continue;
                for (char16_t ch : it.text) {
                    if (knst_needs_unicode(ch)) { needs_unicode = true; break; }
                }
                if (needs_unicode) break;
            }
            if (needs_unicode) break;
        }
        if (needs_unicode) {
            font_path = knst_find_font(u"NotoSans");
            if (font_path.empty()) font_path = knst_find_font(u"DejaVuSans");
            if (font_path.empty()) font_path = knst_find_font(u"Arial");
            if (font_path.empty()) font_path = knst_find_font(u"LiberationSans");
        }
    }

    // If font_path is a name (not an existing file), resolve it
    if (!font_path.empty() && !knst_file::file_exists(font_path)) {
        knst_c16string resolved = knst_find_font(font_path);
        if (!resolved.empty()) font_path = resolved;
    }

    if (!font_path.empty()) {
        knst_file_error fe = knst_file_error::None;
        knst_byte_string raw = knst_file::read_file_data<>(font_path, &fe);
        if (fe == knst_file_error::None && !raw.empty()) {
            std::vector<uint8_t> bytes(raw.length());
            std::memcpy(bytes.data(), raw.data(), raw.length());
            use_ttf = knst_ttf_load(bytes, ttf);

            if (use_ttf) {
                // Collect every UTF-16 codepoint actually used in the document
                std::unordered_map<uint16_t, bool> cp_seen;
                std::vector<uint16_t> used;
                for (auto& pg : d.pages) {
                    for (auto& it : pg.items) {
                        if (it.kind != item::Text) continue;
                        const std::u16string& t = it.text;
                        for (size_t k = 0; k < t.size(); ++k) {
                            uint32_t cp = t[k];
                            if (cp >= 0xD800 && cp <= 0xDBFF && k + 1 < t.size()) {
                                uint32_t lo = t[k + 1];
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                    ++k;
                                }
                            }
                            if (cp > 0xFFFF) continue;  // beyond BMP → skip from subset
                            if (cp_seen.count((uint16_t)cp)) continue;
                            cp_seen[(uint16_t)cp] = true;
                            used.push_back((uint16_t)cp);
                        }
                    }
                }
                if (!used.empty()) {
                    if (!knst_ttf_build_subset(ttf, used, subset)) use_ttf = false;
                } else {
                    use_ttf = false;
                }
            }
        }
    }

    pf.put(std::string("%PDF-1.4\n") + "%" + "\xE2\xE3\xCF\xD3" + "\n");
    const uint32_t cat = pf.reserve(), pages_id = pf.reserve(), enc = pf.reserve();
    uint32_t font_id[12];
    for (int i = 0; i < 12; ++i) font_id[i] = pf.reserve();
    const uint32_t info = pf.reserve();

    // TTF font object IDs (reserved only if use_ttf)
    uint32_t ttf_type0 = 0, ttf_cidfont = 0, ttf_descriptor = 0, ttf_file = 0, ttf_tounicode = 0;
    if (use_ttf) {
        ttf_type0      = pf.reserve();
        ttf_cidfont    = pf.reserve();
        ttf_descriptor = pf.reserve();
        ttf_file       = pf.reserve();
        ttf_tounicode  = pf.reserve();
    }

    std::vector<bool> used(d.images.size(), false);
    for (auto& pg : d.pages) for (auto& it : pg.items) if (it.kind == item::Image && it.image < used.size()) used[it.image] = true;
    std::vector<uint32_t> img_id(d.images.size(), 0);
    for (size_t i = 0; i < d.images.size(); ++i) {
        if (!used[i]) continue;
        img_id[i] = pf.reserve();
        knst_prn::status st = write_image_object(pf, img_id[i], d.images[i]);
        if (st != status::Ok) return st;
    }

    static const char* fnames[12] = { "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
        "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique",
        "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic" };
    // ─── TTF font objects (subset) ───
    if (use_ttf) {
        // FontFile2 — compressed subset TTF
        {
            knst_byte_string compressed;
            if (!knst_deflate_zlib(subset.ttf_bytes.data(), subset.ttf_bytes.size(), compressed))
                return status::IoError;
            pf.begin(ttf_file);
            pf.put("<< /Length " + std::to_string(compressed.length()) +
                   " /Length1 " + std::to_string(subset.ttf_bytes.size()) +
                   " /Filter /FlateDecode >>\nstream\n");
            pf.put(compressed.data(), compressed.length());
            pf.put(std::string("\nendstream"));
            pf.end();
        }
        // FontDescriptor
        {
            auto to1000 = [&](int32_t v) -> int32_t {
                return (int32_t)((int64_t)v * 1000 / (int64_t)ttf.units_per_em);
            };
            int32_t fb0 = to1000(ttf.bbox[0]), fb1 = to1000(ttf.bbox[1]);
            int32_t fb2 = to1000(ttf.bbox[2]), fb3 = to1000(ttf.bbox[3]);
            int32_t asc = to1000(ttf.ascent), desc = to1000(ttf.descent);
            int32_t cap = to1000(ttf.cap_height);
            pf.begin(ttf_descriptor);
            std::string fd = "<< /Type /FontDescriptor /FontName /KNSTEmbedded /Flags 4 ";
            fd += "/FontBBox [" + std::to_string(fb0) + " " + std::to_string(fb1) + " " +
                  std::to_string(fb2) + " " + std::to_string(fb3) + "] ";
            fd += "/ItalicAngle 0 /Ascent " + std::to_string(asc) +
                  " /Descent " + std::to_string(desc) +
                  " /CapHeight " + std::to_string(cap) +
                  " /StemV 80 /FontFile2 " + std::to_string(ttf_file) + " 0 R >>";
            pf.put(fd);
            pf.end();
        }
        // CIDFont
        {
            pf.begin(ttf_cidfont);
            std::string cf = "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /KNSTEmbedded ";
            cf += "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> ";
            cf += "/FontDescriptor " + std::to_string(ttf_descriptor) + " 0 R ";
            cf += "/CIDToGIDMap /Identity ";
            std::string w;
            w += "/W [0 [";
            for (uint32_t g = 0; g < subset.num_glyphs; ++g) {
                int32_t wv = (int32_t)((int64_t)subset.advance[g] * 1000 / (int64_t)ttf.units_per_em);
                if (wv < 0) wv = 0;
                w += std::to_string(wv);
                if (g + 1 < subset.num_glyphs) w += " ";
            }
            w += "]]";
            cf += w + " >>";
            pf.put(cf);
            pf.end();
        }
        // ToUnicode CMap — from subset's compact gid → codepoint map
        {
            std::string cm;
            cm += "/CIDInit /ProcSet findresource begin\n";
            cm += "12 dict begin\nbegincmap\n";
            cm += "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n";
            cm += "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n";
            cm += "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
            std::vector<std::pair<uint16_t, uint32_t>> entries(subset.unicode_map.begin(),
                                                                subset.unicode_map.end());
            std::sort(entries.begin(), entries.end());
            for (size_t i = 0; i < entries.size(); i += 100) {
                size_t n = std::min<size_t>(100, entries.size() - i);
                cm += std::to_string(n) + " beginbfchar\n";
                for (size_t j = 0; j < n; ++j) {
                    uint16_t gid = entries[i + j].first;
                    uint32_t cp = entries[i + j].second;
                    char hb[16]; std::snprintf(hb, sizeof(hb), "<%04X> ", (unsigned)gid); cm += hb;
                    cm += "<";
                    if (cp < 0x10000) { char cb[8]; std::snprintf(cb, sizeof(cb), "%04X", cp); cm += cb; }
                    else {
                        uint32_t hi = 0xD800 + ((cp - 0x10000) >> 10);
                        uint32_t lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
                        char cb[16]; std::snprintf(cb, sizeof(cb), "%04X%04X", hi, lo); cm += cb;
                    }
                    cm += ">\n";
                }
                cm += "endbfchar\n";
            }
            cm += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
            knst_byte_string compressed;
            if (!knst_deflate_zlib(reinterpret_cast<const uint8_t*>(cm.data()), cm.size(), compressed))
                return status::IoError;
            pf.begin(ttf_tounicode);
            pf.put("<< /Length " + std::to_string(compressed.length()) + " /Filter /FlateDecode >>\nstream\n");
            pf.put(compressed.data(), compressed.length());
            pf.put(std::string("\nendstream"));
            pf.end();
        }
        // Type0 font
        {
            pf.begin(ttf_type0);
            std::string f0 = "<< /Type /Font /Subtype /Type0 /BaseFont /KNSTEmbedded /Encoding /Identity-H ";
            f0 += "/DescendantFonts [" + std::to_string(ttf_cidfont) + " 0 R] ";
            f0 += "/ToUnicode " + std::to_string(ttf_tounicode) + " 0 R >>";
            pf.put(f0);
            pf.end();
        }
    }

    pf.begin(enc);
    pf.put(std::string("<< /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences "
                       "[208 /Gbreve 240 /gbreve 221 /Idotaccent 253 /dotlessi 222 /Scedilla 254 /scedilla] >>"));
    pf.end();
    for (int i = 0; i < 12; ++i) {
        pf.begin(font_id[i]);
        pf.put("<< /Type /Font /Subtype /Type1 /BaseFont /" + std::string(fnames[i]) + " /Encoding " + std::to_string(enc) + " 0 R >>");
        pf.end();
    }
    pf.begin(info);
    pf.put(std::string("<< /Producer (KernelNucleusT knst_printer) /Creator (KernelNucleusT) >>"));
    pf.end();

    std::string fontres = "/Font << ";
    for (int i = 0; i < 12; ++i) fontres += "/F" + std::to_string(i) + " " + std::to_string(font_id[i]) + " 0 R ";
    if (use_ttf) fontres += "/Fttf " + std::to_string(ttf_type0) + " 0 R ";
    fontres += ">>";

    std::vector<uint32_t> page_ids;
    for (auto& pg : d.pages) {
        std::string c; std::vector<uint32_t> in_page;
        for (auto& it : pg.items) {
            switch (it.kind) {
            case item::Text: {
                pdf_rgb(c, it.r, it.g, it.b, "rg");
                 if (use_ttf) {
                    // UTF-16 → codepoint → original GID → subset GID
                    std::string s;
                    uint32_t i = 0;
                    const std::u16string& t = it.text;
                    while (i < t.size()) {
                        uint32_t cp = t[i++];
                        if (cp >= 0xD800 && cp <= 0xDBFF && i < t.size()) {
                            uint32_t lo = t[i];
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                ++i;
                            }
                        }
                        uint16_t subset_gid = 0;
                        if (cp <= 0xFFFF) {
                            auto itg = ttf.cmap.find(cp);
                            if (itg != ttf.cmap.end()) {
                                auto its = subset.gid_map.find(itg->second);
                                if (its != subset.gid_map.end()) subset_gid = its->second;
                            }
                        }
                        char hb[8]; std::snprintf(hb, sizeof(hb), "%04X", (unsigned)subset_gid);
                        s += hb;
                    }
                    c += "BT /Fttf "; put_num(c, it.size); c += " Tf ";
                    put_num(c, it.x1); c += ' '; put_num(c, d.h_pt - it.y1);
                    c += " Td <" + s + "> Tj ET\n";
                } else {
                    std::string s;
                    for (char16_t ch : it.text) {
                        if (is_low_surrogate(ch)) continue;
                        uint8_t b = to_pdf_byte(ch);
                        if (b == '(' || b == ')' || b == '\\') { s.push_back('\\'); s.push_back(static_cast<char>(b)); }
                        else if (b < 32 || b > 126) { char o[8]; std::snprintf(o, sizeof(o), "\\%03o", b); s += o; }
                        else s.push_back(static_cast<char>(b));
                    }
                    int fi = static_cast<int>(it.fam) * 4 + (it.bold ? 1 : 0) + (it.italic ? 2 : 0);
                    c += "BT /F" + std::to_string(fi) + " "; put_num(c, it.size); c += " Tf ";
                    put_num(c, it.x1); c += ' '; put_num(c, d.h_pt - it.y1); c += " Td (" + s + ") Tj ET\n";
                }
                break; }
            case item::Line:
                pdf_rgb(c, it.r, it.g, it.b, "RG"); put_num(c, it.lw); c += " w ";
                put_num(c, it.x1); c += ' '; put_num(c, d.h_pt - it.y1); c += " m ";
                put_num(c, it.x2); c += ' '; put_num(c, d.h_pt - it.y2); c += " l S\n";
                break;
            case item::Rect:
            case item::FillRect:
                if (it.kind == item::Rect) { pdf_rgb(c, it.r, it.g, it.b, "RG"); put_num(c, it.lw); c += " w "; }
                else pdf_rgb(c, it.r, it.g, it.b, "rg");
                put_num(c, std::min(it.x1, it.x2)); c += ' '; put_num(c, d.h_pt - std::max(it.y1, it.y2)); c += ' ';
                put_num(c, std::fabs(it.x2 - it.x1)); c += ' '; put_num(c, std::fabs(it.y2 - it.y1));
                c += (it.kind == item::Rect) ? " re S\n" : " re f\n";
                break;
            case item::Image: {
                if (it.image >= img_id.size() || !img_id[it.image]) break;
                float m[6]; image_matrix(it, d.h_pt, m);
                c += "q\n";
                if (it.clip) {
                    put_num(c, it.cx1); c += ' '; put_num(c, d.h_pt - it.cy2); c += ' ';
                    put_num(c, it.cx2 - it.cx1); c += ' '; put_num(c, it.cy2 - it.cy1); c += " re W n\n";
                }
                for (int k = 0; k < 6; ++k) { put_num(c, m[k]); c += ' '; }
                c += "cm /Im" + std::to_string(it.image) + " Do\nQ\n";
                if (std::find(in_page.begin(), in_page.end(), it.image) == in_page.end()) in_page.push_back(it.image);
                break; }
            }
        }
        const uint32_t cid = pf.reserve(), pid = pf.reserve();
        knst_byte_string cs_compressed;
        if (!knst_deflate_zlib(reinterpret_cast<const uint8_t*>(c.data()), c.size(), cs_compressed))
            return status::IoError;
        pf.begin(cid);
        pf.put("<< /Length " + std::to_string(cs_compressed.length()) + " /Filter /FlateDecode >>\nstream\n");
        pf.put(cs_compressed.data(), cs_compressed.length());
        pf.put(std::string("\nendstream"));
        pf.end();
        std::string mb = "[0 0 "; put_num(mb, d.w_pt); mb += ' '; put_num(mb, d.h_pt); mb += ']';
        std::string xo;
        if (!in_page.empty()) {
            xo = " /XObject << ";
            for (uint32_t ix : in_page) xo += "/Im" + std::to_string(ix) + " " + std::to_string(img_id[ix]) + " 0 R ";
            xo += ">>";
        }
        pf.begin(pid);
        pf.put("<< /Type /Page /Parent " + std::to_string(pages_id) + " 0 R /MediaBox " + mb + " /Resources << " +
               fontres + xo + " >> /Contents " + std::to_string(cid) + " 0 R >>");
        pf.end();
        page_ids.push_back(pid);
        if (!pf.ok()) return status::IoError;
    }

    pf.begin(pages_id);
    std::string kids = "<< /Type /Pages /Count " + std::to_string(page_ids.size()) + " /Kids [";
    for (uint32_t pid : page_ids) kids += std::to_string(pid) + " 0 R ";
    kids += "] >>";
    pf.put(kids); pf.end();
    pf.begin(cat);
    pf.put("<< /Type /Catalog /Pages " + std::to_string(pages_id) + " 0 R >>");
    pf.end();
    return pf.finish(cat, info) ? status::Ok : status::IoError;
}

// Fallback page counter for PDFs when pdfinfo/qpdf are missing (works for
// files without compressed object streams).
inline uint32_t count_pdf_pages_scan(const knst_byte_string& b) {
    const unsigned char* p = b.data(); size_t n = b.length(); uint32_t cnt = 0;
    for (size_t i = 0; i + 6 < n; ++i) {
        if (p[i] != '/' || std::memcmp(p + i, "/Type", 5) != 0) continue;
        size_t j = i + 5;
        while (j < n && std::isspace(p[j])) ++j;
        if (j + 5 <= n && std::memcmp(p + j, "/Page", 5) == 0 && (j + 5 == n || p[j + 5] != 's')) ++cnt;
    }
    return cnt;
}

// ── PNG writer (fixed Huffman deflate + Sub filter) ──────────────────
// Writes a valid PNG from raw RGB bytes. Uses real deflate compression
// with the Sub filter (row-wise delta), giving 40-70% smaller files than
// stored blocks on typical photos and scans. Cross-platform: MinGW /
// Linux / Android. Uses only <cstdio>, <cstring>, <vector>.
inline bool write_png_simple(const knst_c16string& path, uint32_t w, uint32_t h,
                              const uint8_t* rgb) {
    if (!rgb || w == 0 || h == 0) return false;
    size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    if (raw_len > 500u * 1024u * 1024u) {
        return false;
    }
    std::FILE* f = open_file(path, true);
    if (!f) return false;

    static uint32_t tbl[256];
    static bool tbl_ok = false;
    if (!tbl_ok) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[n] = c;
        }
        tbl_ok = true;
    }
    auto crc_calc = [](const uint8_t* b, size_t n) {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i) c = tbl[(c ^ b[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };
    uint32_t crc_acc = 0xFFFFFFFFu;
    auto crc_upd = [&](const uint8_t* b, size_t n) {
        for (size_t i = 0; i < n; ++i) crc_acc = tbl[(crc_acc ^ b[i]) & 0xFF] ^ (crc_acc >> 8);
    };
    auto put32be = [f](uint32_t v) {
        uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
        std::fwrite(b, 1, 4, f);
    };

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::fwrite(sig, 1, 8, f);

    {
        uint8_t ihdr[13] = {0};
        ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16);
        ihdr[2] = (uint8_t)(w >>  8); ihdr[3] = (uint8_t)w;
        ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16);
        ihdr[6] = (uint8_t)(h >>  8); ihdr[7] = (uint8_t)h;
        ihdr[8] = 8;   ihdr[9] = 2;   ihdr[10] = 0;   ihdr[11] = 0;   ihdr[12] = 0;
        static const uint8_t typ[4] = { 'I','H','D','R' };
        put32be(13);
        std::fwrite(typ, 1, 4, f);
        std::fwrite(ihdr, 1, 13, f);
        uint8_t crcb[17];
        std::memcpy(crcb, typ, 4);
        std::memcpy(crcb + 4, ihdr, 13);
        put32be(crc_calc(crcb, 17));
    }

    std::vector<uint8_t> raw(raw_len);
    for (uint32_t y = 0; y < h; ++y) {
        size_t off = (size_t)y * (1 + (size_t)w * 3);
        raw[off] = 1;
        const uint8_t* row = &rgb[(size_t)y * w * 3];
        uint8_t* o = &raw[off + 1];
        o[0] = row[0];
        o[1] = row[1];
        o[2] = row[2];
        for (size_t i = 3; i < (size_t)w * 3; ++i)
            o[i] = (uint8_t)(row[i] - row[i - 3]);
    }

    knst_byte_string zlib_data;
    if (!knst_deflate_zlib(raw.data(), raw_len, zlib_data)) {
        std::fclose(f);
        knst_file::remove_file(path);  // don't leave a half-written PNG
        return false;
    }

    put32be((uint32_t)zlib_data.length());
    static const uint8_t idat[4] = { 'I','D','A','T' };
    std::fwrite(idat, 1, 4, f);
    crc_acc = 0xFFFFFFFFu;
    crc_upd(idat, 4);
    std::fwrite(zlib_data.data(), 1, zlib_data.length(), f);
    crc_upd(zlib_data.data(), zlib_data.length());
    put32be(crc_acc ^ 0xFFFFFFFFu);

    {
        put32be(0);
        static const uint8_t ie[4] = { 'I','E','N','D' };
        std::fwrite(ie, 1, 4, f);
        put32be(crc_calc(ie, 4));
    }

    std::fclose(f);
    return true;
}

// Decode any supported image (JPEG/PNG/BMP/GIF/...) to RGB and re-encode as PNG.
inline bool decode_to_png(const knst_c16string& src, const knst_c16string& dst) {
    knst_file_error fe = knst_file_error::None;
    knst_byte_string bytes = knst_file::read_file_data<>(src, &fe);
    if (fe != knst_file_error::None || bytes.empty()) {
        return false;
    }
    knst_byte_string rgb;
    uint32_t w = 0, h = 0;
    if (!decode_rgb(bytes, rgb, w, h)) {
        return false;
    }
    bool ok = write_png_simple(dst, w, h, rgb.data());

    return ok;
}

} // namespace detail

// ===========================================================================
//              DEFLATE encoder (RFC 1951) — fixed Huffman, LZ77
// ===========================================================================
namespace detail {

static inline uint32_t knst_def_reverse_bits(uint32_t v, int n) noexcept {
    uint32_t r = 0;
    for (int i = 0; i < n; ++i) { r = (r << 1) | (v & 1u); v >>= 1; }
    return r;
}

inline bool knst_deflate_compress(const uint8_t* data, size_t len, knst_byte_string& out) {
    out.clear();
    if (!data && len > 0) return false;

    std::vector<uint8_t> buf;
    buf.reserve(len / 2 + 64);

    uint32_t bitbuf = 0;
    int      bitcnt = 0;
    auto put_bits = [&](uint32_t v, int n) {
        bitbuf |= v << bitcnt;
        bitcnt += n;
        while (bitcnt >= 8) {
            buf.push_back(static_cast<uint8_t>(bitbuf & 0xFF));
            bitbuf >>= 8;
            bitcnt -= 8;
        }
    };
    auto put_lit = [&](int v) {
        if      (v <= 143) put_bits(knst_def_reverse_bits(0x30u + (uint32_t)v, 8), 8);
        else if (v <= 255) put_bits(knst_def_reverse_bits(0x190u + (uint32_t)v - 144, 9), 9);
        else if (v <= 279) put_bits(knst_def_reverse_bits((uint32_t)v - 256, 7), 7);
        else               put_bits(knst_def_reverse_bits(0xC0u + (uint32_t)v - 280, 8), 8);
    };
    auto put_dist = [&](int v) {
        put_bits(knst_def_reverse_bits((uint32_t)v, 5), 5);
    };

    put_bits(1, 1);
    put_bits(1, 2);

    static const int len_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const int len_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
        3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const int dist_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const int dist_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    auto put_match = [&](int length, int distance) {
        int lc = 28;
        for (int i = 28; i >= 0; --i) if (length >= len_base[i]) { lc = i; break; }
        put_lit(257 + lc);
        if (len_extra[lc]) put_bits((uint32_t)(length - len_base[lc]), len_extra[lc]);
        int dc = 29;
        for (int i = 29; i >= 0; --i) if (distance >= dist_base[i]) { dc = i; break; }
        put_dist(dc);
        if (dist_extra[dc]) put_bits((uint32_t)(distance - dist_base[dc]), dist_extra[dc]);
    };

    const size_t WINDOW    = 32768;
    const size_t MIN_MATCH = 3;
    const size_t MAX_MATCH = 258;
    const size_t HASH_BITS = 15;
    const size_t HASH_SIZE = 1u << HASH_BITS;
    const int    MAX_CHAIN = 32;
    const size_t WMASK     = WINDOW - 1;

    std::vector<int32_t> head(HASH_SIZE, -1);
    std::vector<int32_t> prev(WINDOW, -1);

    auto hash3 = [&](size_t p) -> uint32_t {
        uint32_t v = ((uint32_t)data[p] << 16) | ((uint32_t)data[p+1] << 8) | (uint32_t)data[p+2];
        v *= 2654435761u;
        return v >> (32 - HASH_BITS);
    };

    size_t pos = 0;
    while (pos < len) {
        if (pos + MIN_MATCH > len) { put_lit(data[pos]); ++pos; continue; }
        uint32_t h = hash3(pos);
        int32_t  chain = head[h];
        prev[pos & WMASK] = chain;
        head[h] = (int32_t)pos;

        int best_len = 0, best_dist = 0;
        int max_len = (int)std::min(MAX_MATCH, len - pos);
        int count = 0;
        int32_t cur = chain;
        while (cur >= 0 && count < MAX_CHAIN) {
            size_t dist = pos - (size_t)cur;
            if (dist > WINDOW) break;
            int m = 0;
            while (m < max_len && data[(size_t)cur + m] == data[pos + m]) ++m;
            if (m > best_len && m >= (int)MIN_MATCH) {
                best_len = m;
                best_dist = (int)dist;
                if (best_len >= max_len) break;
            }
            cur = prev[(size_t)cur & WMASK];
            ++count;
        }

        if (best_len >= (int)MIN_MATCH) {
            put_match(best_len, best_dist);
            for (int i = 1; i < best_len; ++i) {
                size_t p = pos + i;
                if (p + MIN_MATCH <= len) {
                    uint32_t ph = hash3(p);
                    prev[p & WMASK] = head[ph];
                    head[ph] = (int32_t)p;
                }
            }
            pos += best_len;
        } else {
            put_lit(data[pos]);
            ++pos;
        }
    }

    put_lit(256);
    if (bitcnt > 0) buf.push_back(static_cast<uint8_t>(bitbuf & 0xFF));

    out.resize(static_cast<uint32_t>(buf.size()));
    if (out.length() != buf.size()) return false;
    if (!buf.empty()) std::memcpy(&out[0], buf.data(), buf.size());
    return true;
}

inline bool knst_deflate_zlib(const uint8_t* data, size_t len, knst_byte_string& out) {
    out.clear();
    knst_byte_string compressed;
    if (!knst_deflate_compress(data, len, compressed)) return false;

    std::vector<uint8_t> buf;
    buf.reserve(compressed.length() + 6);
    buf.push_back(0x78);
    buf.push_back(0x9C);
    const uint8_t* cp = compressed.data();
    for (uint32_t i = 0; i < compressed.length(); ++i) buf.push_back(cp[i]);

    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    uint32_t adler = (b << 16) | a;
    buf.push_back((uint8_t)((adler >> 24) & 0xFF));
    buf.push_back((uint8_t)((adler >> 16) & 0xFF));
    buf.push_back((uint8_t)((adler >> 8)  & 0xFF));
    buf.push_back((uint8_t)( adler        & 0xFF));

    out.resize(static_cast<uint32_t>(buf.size()));
    if (out.length() != buf.size()) return false;
    if (!buf.empty()) std::memcpy(&out[0], buf.data(), buf.size());
    return true;
}

} // namespace detail

// ===========================================================================
//              TTF / OTF parser + PDF Type0 embedder
// ===========================================================================
// Minimal TrueType/OpenType parser sufficient for PDF text embedding.
// Handles: head, hhea, maxp, hmtx, loca, glyf, cmap (format 4 + 12).
// Rejects CFF-based OTF ('OTTO') — those need a different PDF font type.
namespace detail {

// ─── System font locator ────────────────────────────────────────────────
// Resolves a font name ("NotoSans", "Arial", "DejaVuSans") or a partial
// path to an absolute .ttf/.otf path. Returns empty on miss.
// 1. If the path exists as-is → return it.
// 2. Case-insensitive substring match against known font directories.
// 3. Fallback list ordered by Unicode coverage:
//    NotoSans > DejaVuSans > Arial > SegoeUI > LiberationSans.
inline knst_c16string knst_find_font(const knst_c16string& name) {
    if (name.empty()) return knst_c16string();

    // 1) Direct path
    if (knst_file::file_exists(name)) return knst_file::resolve_path(name);

    std::string query = detail::lower(detail::to_u8(name));
    // Strip extension if present
    size_t dot = query.rfind('.');
    if (dot != std::string::npos &&
        (query.substr(dot) == ".ttf" || query.substr(dot) == ".otf" ||
         query.substr(dot) == ".ttc"))
        query = query.substr(0, dot);
    // Strip hyphens/underscores/spaces for fuzzy match
    std::string qnorm;
    for (char c : query) if (c != '-' && c != '_' && c != ' ') qnorm.push_back(c);

    // 2) Search common font directories
#if defined(_WIN32)
    const char* dirs[] = {
        "C:\\Windows\\Fonts",
        "C:\\Windows\\Fonts\\Static",
        nullptr
    };
#elif defined(__ANDROID__)
    const char* dirs[] = {
        "/system/fonts",
        "/system/font",
        "/data/fonts",
        nullptr
    };
#else
    const char* dirs[] = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        "/usr/share/fonts/truetype",
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/opentype",
        "/Library/Fonts",
        "/System/Library/Fonts",
        nullptr
    };
#endif

    knst_c16string best;
    std::string best_score;  // shorter = better

    auto score_of = [&](const std::string& fname) -> std::string {
        std::string f = detail::lower(fname);
        if (f.find(qnorm) == std::string::npos) return std::string();
        return f;
    };

    for (int d = 0; dirs[d]; ++d) {
        knst_vector<knst_file> entries;
        if (!knst_file::list_directory_recursive(detail::from_u8(dirs[d]), entries))
            continue;
        for (auto& e : entries) {
            if (!e.is_file) continue;
            std::string nm = detail::to_u8(e.name);
            std::string ext = detail::lower(detail::to_u8(detail::path_ext(e.name)));
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            std::string sc = score_of(nm);
            if (sc.empty()) continue;
            // Prefer regular (not bold/italic/light) and shorter names
            bool prefer_regular =
                sc.find("bold") == std::string::npos &&
                sc.find("italic") == std::string::npos &&
                sc.find("oblique") == std::string::npos &&
                sc.find("light") == std::string::npos &&
                sc.find("thin") == std::string::npos &&
                sc.find("black") == std::string::npos &&
                sc.find("condensed") == std::string::npos &&
                sc.find("narrow") == std::string::npos;
            std::string weighted = (prefer_regular ? "0|" : "1|") + sc;
            if (best.empty() || weighted < best_score) {
                best_score = weighted;
                best = e.path;
            }
        }
        if (!best.empty()) break;  // stop at first dir that had a match
    }
    if (!best.empty()) return best;

    // 3) Fallback list — try well-known Unicode-complete fonts
    static const char* fallbacks[] = {
        "NotoSans-Regular", "NotoSans", "DejaVuSans",
        "Arial", "ArialUnicode", "SegoeUI",
        "LiberationSans-Regular", "FreeSans",
        nullptr
    };
    for (int i = 0; fallbacks[i]; ++i) {
        knst_c16string try_name = detail::from_u8(fallbacks[i]);
        if (try_name == name) continue;
        knst_c16string found = knst_find_font(try_name);
        if (!found.empty()) return found;
    }
    return knst_c16string();
}

// Returns true if the codepoint needs TTF embedding (outside WinAnsi space).
inline bool knst_needs_unicode(char16_t c) {
    if (c >= 32 && c < 127) return false;         // ASCII
    if (c >= 0xA0 && c <= 0xFF) return false;     // Latin-1
    // Turkish-specific PDF byte remap
    switch (c) {
        case 0x011E: case 0x011F: case 0x0130: case 0x0131:
        case 0x015E: case 0x015F: case 0x20AC: case 0x2026:
        case 0x2018: case 0x2019: case 0x201C: case 0x201D:
        case 0x2022: case 0x2013: case 0x2014: case 0x2122:
        case 0x0152: case 0x0153: case 0x0160: case 0x0161:
        case 0x017D: case 0x017E: case 0x0178: case 0x0192:
        case 0x00D0: case 0x00F0: case 0x00DD: case 0x00FD:
        case 0x00DE: case 0x00FE:
            return false;
    }
    if (c >= 0xD800 && c <= 0xDFFF) return false; // surrogates handled separately
    return true;
}

// (struct knst_ttf_font — moved to top of namespace detail, see forward decls)

inline uint16_t knst_ttf_rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t knst_ttf_rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline int16_t knst_ttf_rd16s(const uint8_t* p) { return (int16_t)knst_ttf_rd16(p); }
inline int32_t knst_ttf_rd32s(const uint8_t* p) { return (int32_t)knst_ttf_rd32(p); }

inline bool knst_ttf_load(const std::vector<uint8_t>& data, knst_ttf_font& f) {
    f = knst_ttf_font{};
    if (data.size() < 12) return false;

    uint32_t tag = knst_ttf_rd32(data.data());
    if (tag != 0x00010000 && tag != 0x74727565) {
        // 0x4F54544F = 'OTTO' (CFF) — not supported
        // 0x74797031 = 'typ1' — not supported
        return false;
    }
    uint16_t num_tables = knst_ttf_rd16(data.data() + 4);
    if (num_tables == 0 || data.size() < (size_t)12 + num_tables * 16) return false;

    auto find_table = [&](const char* tag4) -> const uint8_t* {
        for (uint16_t i = 0; i < num_tables; ++i) {
            const uint8_t* rec = data.data() + 12 + i * 16;
            if (std::memcmp(rec, tag4, 4) == 0) {
                uint32_t off = knst_ttf_rd32(rec + 8);
                if (off + 4 > data.size()) return nullptr;
                return data.data() + off;
            }
        }
        return nullptr;
    };

    const uint8_t* head = find_table("head");
    const uint8_t* hhea = find_table("hhea");
    const uint8_t* maxp = find_table("maxp");
    const uint8_t* hmtx = find_table("hmtx");
    const uint8_t* loca = find_table("loca");
    const uint8_t* glyf = find_table("glyf");
    const uint8_t* cmap_tbl = find_table("cmap");
    (void)glyf;
    if (!head || !hhea || !maxp || !hmtx || !loca || !cmap_tbl) return false;

    f.units_per_em = knst_ttf_rd16(head + 18);
    if (f.units_per_em == 0) f.units_per_em = 1000;
    f.index_to_loc_format = knst_ttf_rd16s(head + 50);
    f.bbox[0] = knst_ttf_rd16s(head + 36);
    f.bbox[1] = knst_ttf_rd16s(head + 38);
    f.bbox[2] = knst_ttf_rd16s(head + 40);
    f.bbox[3] = knst_ttf_rd16s(head + 42);

    f.ascent  = knst_ttf_rd16s(hhea + 4);
    f.descent = knst_ttf_rd16s(hhea + 6);
    uint16_t num_hmetrics = knst_ttf_rd16(hhea + 34);

    f.num_glyphs = knst_ttf_rd16(maxp + 4);
    if (f.num_glyphs == 0) return false;

    f.advance.resize(f.num_glyphs, 0);
    if (num_hmetrics == 0) return false;
    uint32_t last_adv = 0;
    for (uint16_t i = 0; i < num_hmetrics && i < f.num_glyphs; ++i) {
        last_adv = knst_ttf_rd16(hmtx + i * 4);
        f.advance[i] = last_adv;
    }
    for (uint32_t i = num_hmetrics; i < f.num_glyphs; ++i) f.advance[i] = last_adv;

    f.loca.resize(f.num_glyphs + 1, 0);
    if (f.index_to_loc_format == 0) {
        for (uint32_t i = 0; i <= f.num_glyphs; ++i)
            f.loca[i] = (uint32_t)knst_ttf_rd16(loca + i * 2) * 2;
    } else {
        for (uint32_t i = 0; i <= f.num_glyphs; ++i)
            f.loca[i] = knst_ttf_rd32(loca + i * 4);
    }

    for (uint16_t i = 0; i < num_tables; ++i) {
        const uint8_t* rec = data.data() + 12 + i * 16;
        if (std::memcmp(rec, "glyf", 4) == 0) {
            f.glyf_offset = knst_ttf_rd32(rec + 8);
            f.glyf_length = knst_ttf_rd32(rec + 12);
            break;
        }
    }

    const uint16_t num_cmap = knst_ttf_rd16(cmap_tbl + 2);
    const uint8_t* best4  = nullptr;
    const uint8_t* best12 = nullptr;
    for (uint16_t i = 0; i < num_cmap; ++i) {
        const uint8_t* rec = cmap_tbl + 4 + i * 8;
        uint16_t platform = knst_ttf_rd16(rec);
        uint16_t encoding = knst_ttf_rd16(rec + 2);
        uint32_t off = knst_ttf_rd32(rec + 4);
        if (off + 4 > data.size()) continue;
        const uint8_t* sub = data.data() + off;
        uint16_t fmt = knst_ttf_rd16(sub);
        if (fmt == 4 && platform == 3 && encoding == 1) best4 = sub;
        else if (fmt == 12 && platform == 3 && encoding == 10) best12 = sub;
        else if (fmt == 4 && !best4) best4 = sub;
        else if (fmt == 12 && !best12) best12 = sub;
    }

    if (best12) {
        uint32_t num_groups = knst_ttf_rd32(best12 + 12);
        for (uint32_t g = 0; g < num_groups; ++g) {
            const uint8_t* grp = best12 + 16 + g * 12;
            if (grp + 12 > data.data() + data.size()) break;
            uint32_t start = knst_ttf_rd32(grp);
            uint32_t end   = knst_ttf_rd32(grp + 4);
            uint32_t gid   = knst_ttf_rd32(grp + 8);
            for (uint32_t c = start; c <= end; ++c) {
                if (f.cmap.size() > 65535) break;
                f.cmap[c] = (uint16_t)std::min<uint32_t>(gid + (c - start), 0xFFFF);
            }
        }
    } else if (best4) {
        uint16_t seg_count = knst_ttf_rd16(best4 + 6) / 2;
        const uint8_t* end_codes   = best4 + 14;
        const uint8_t* start_codes = end_codes + seg_count * 2 + 2;
        const uint8_t* id_delta    = start_codes + seg_count * 2;
        const uint8_t* id_range    = id_delta + seg_count * 2;
        for (uint16_t s = 0; s < seg_count; ++s) {
            uint16_t sc = knst_ttf_rd16(start_codes + s * 2);
            uint16_t ec = knst_ttf_rd16(end_codes + s * 2);
            int16_t  delta = knst_ttf_rd16s(id_delta + s * 2);
            uint16_t ro    = knst_ttf_rd16(id_range + s * 2);
            if (sc == 0xFFFF) continue;
            for (uint32_t c = sc; c <= ec; ++c) {
                uint16_t gid;
                if (ro == 0) gid = (uint16_t)((c + delta) & 0xFFFF);
                else {
                    const uint8_t* gp = id_range + s * 2 + ro + (c - sc) * 2;
                    if (gp + 2 > data.data() + data.size()) continue;
                    uint16_t raw = knst_ttf_rd16(gp);
                    gid = raw ? (uint16_t)((raw + delta) & 0xFFFF) : 0;
                }
                if (gid != 0) f.cmap[c] = gid;
            }
        }
    }

    if (f.cmap.empty()) return false;

    const uint8_t* os2 = find_table("OS/2");
    if (os2 && os2 + 90 <= data.data() + data.size()) f.cap_height = knst_ttf_rd16s(os2 + 88);
    if (f.cap_height == 0) f.cap_height = (int32_t)(f.units_per_em * 7 / 10);

    f.data = data;
    f.valid = true;
    return true;
}

// ─── TTF subset ───
// Extracts only the glyphs used by the document, remaps GIDs to compact
// 0..N-1 range, rebuilds head/hhea/maxp/hmtx/loca/glyf/cmap. Other tables
// (name, post, OS/2 ...) are dropped — PDF readers only need the seven
// above when GIDs are explicit in the content stream.
// (struct knst_ttf_subset — moved to top of namespace detail, see forward decls)

inline uint32_t knst_ttf_table_checksum(const uint8_t* d, size_t n) {
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        sum += ((uint32_t)d[i] << 24) | ((uint32_t)d[i+1] << 16) |
               ((uint32_t)d[i+2] << 8) | d[i+3];
    }
    if (i < n) {
        uint32_t last = 0;
        for (size_t k = 0; k < 4; ++k)
            last |= (i + k < n ? (uint32_t)d[i+k] : 0u) << (24 - k*8);
        sum += last;
    }
    return sum;
}

inline void knst_ttf_put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((v >> 8) & 0xFF);
    b.push_back(v & 0xFF);
}
inline void knst_ttf_put32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((v >> 24) & 0xFF);
    b.push_back((v >> 16) & 0xFF);
    b.push_back((v >> 8) & 0xFF);
    b.push_back(v & 0xFF);
}

inline bool knst_ttf_build_subset(const knst_ttf_font& font,
                                   const std::vector<uint16_t>& used_cp,
                                   knst_ttf_subset& out) {
    out = knst_ttf_subset{};
    if (!font.valid) return false;

    // 1. Collect unique used codepoints
    struct inc { uint16_t orig_gid; uint32_t cp; };
    std::vector<inc> inc_list;
    std::unordered_map<uint16_t, bool> gid_seen;
    inc_list.push_back({0, 0});  // .notdef always
    gid_seen[0] = true;
    for (uint16_t cp16 : used_cp) {
        auto it = font.cmap.find((uint32_t)cp16);
        if (it == font.cmap.end()) continue;
        if (gid_seen.count(it->second)) continue;
        gid_seen[it->second] = true;
        inc_list.push_back({it->second, (uint32_t)cp16});
    }
    std::sort(inc_list.begin(), inc_list.end(),
              [](const inc& a, const inc& b) { return a.orig_gid < b.orig_gid; });

    uint16_t N = (uint16_t)inc_list.size();
    out.num_glyphs = N;
    for (uint16_t i = 0; i < N; i++) {
        out.gid_map[inc_list[i].orig_gid] = i;
        if (inc_list[i].cp != 0 && inc_list[i].cp <= 0xFFFF)
            out.unicode_map[i] = inc_list[i].cp;
    }

    // 2. Read table directory
    uint16_t num_tables = knst_ttf_rd16(font.data.data() + 4);
    struct tinfo { char tag[4]; uint32_t off; uint32_t len; };
    std::vector<tinfo> tables(num_tables);
    for (uint16_t i = 0; i < num_tables; ++i) {
        const uint8_t* rec = font.data.data() + 12 + i * 16;
        std::memcpy(tables[i].tag, rec, 4);
        tables[i].off = knst_ttf_rd32(rec + 8);
        tables[i].len = knst_ttf_rd32(rec + 12);
    }
    auto find_t = [&](const char* t) -> const tinfo* {
        for (auto& x : tables) if (std::memcmp(x.tag, t, 4) == 0) return &x;
        return nullptr;
    };

    const tinfo* t_head = find_t("head");
    const tinfo* t_hhea = find_t("hhea");
    const tinfo* t_maxp = find_t("maxp");
    const tinfo* t_hmtx = find_t("hmtx");
    if (!t_head || !t_hhea || !t_maxp || !t_hmtx) return false;

    const uint8_t* head_p = font.data.data() + t_head->off;
    const uint8_t* hhea_p = font.data.data() + t_hhea->off;
    const uint8_t* maxp_p = font.data.data() + t_maxp->off;
    const uint8_t* hmtx_p = font.data.data() + t_hmtx->off;
    uint16_t orig_num_hmetrics = knst_ttf_rd16(hhea_p + 34);

    // 3. Extract glyphs + advances
    std::vector<std::vector<uint8_t>> glyphs(N);
    out.advance.resize(N);
    std::vector<std::pair<uint16_t, int16_t>> hmetrics(N);
    for (uint16_t i = 0; i < N; i++) {
        uint16_t ogid = inc_list[i].orig_gid;
        uint32_t gs = font.loca[ogid];
        uint32_t ge = font.loca[ogid + 1];
        if (ge > font.glyf_length) ge = font.glyf_length;
        if (ge > gs && font.glyf_offset + ge <= font.data.size()) {
            glyphs[i].resize(ge - gs);
            std::memcpy(glyphs[i].data(), font.data.data() + font.glyf_offset + gs, ge - gs);
        }
        uint16_t adv = 0; int16_t lsb = 0;
        if (ogid < orig_num_hmetrics) {
            adv = knst_ttf_rd16(hmtx_p + ogid * 4);
            lsb = knst_ttf_rd16s(hmtx_p + ogid * 4 + 2);
        } else if (orig_num_hmetrics > 0) {
            adv = knst_ttf_rd16(hmtx_p + (orig_num_hmetrics - 1) * 4);
            if (hmtx_p + orig_num_hmetrics * 4 + (ogid - orig_num_hmetrics) * 2 + 2 <= font.data.data() + font.data.size())
                lsb = knst_ttf_rd16s(hmtx_p + orig_num_hmetrics * 4 + (ogid - orig_num_hmetrics) * 2);
        }
        hmetrics[i] = {adv, lsb};
        out.advance[i] = adv;
    }

    // 4. loca + glyf (4-byte aligned)
    std::vector<uint8_t> glyf_data;
    std::vector<uint8_t> loca_data;
    knst_ttf_put32(loca_data, 0);
    for (uint16_t i = 0; i < N; i++) {
        glyf_data.insert(glyf_data.end(), glyphs[i].begin(), glyphs[i].end());
        while (glyf_data.size() % 4) glyf_data.push_back(0);
        knst_ttf_put32(loca_data, (uint32_t)glyf_data.size());
    }

    // 5. hmtx
    std::vector<uint8_t> hmtx_data;
    for (uint16_t i = 0; i < N; i++) {
        knst_ttf_put16(hmtx_data, hmetrics[i].first);
        knst_ttf_put16(hmtx_data, (uint16_t)hmetrics[i].second);
    }

    // 6. head (copy + patch)
    std::vector<uint8_t> head_data(head_p, head_p + std::min<uint32_t>(t_head->len, 54));
    while (head_data.size() < 54) head_data.push_back(0);
    head_data[8]  = head_data[9]  = head_data[10] = head_data[11] = 0;
    head_data[50] = 0; head_data[51] = 1;  // indexToLocFormat = long

    // 7. hhea (copy + patch)
    std::vector<uint8_t> hhea_data(hhea_p, hhea_p + std::min<uint32_t>(t_hhea->len, 36));
    while (hhea_data.size() < 36) hhea_data.push_back(0);
    hhea_data[34] = (N >> 8) & 0xFF;
    hhea_data[35] = N & 0xFF;

    // 8. maxp (copy + patch numGlyphs)
    uint32_t maxp_ver = knst_ttf_rd32(maxp_p);
    uint32_t maxp_len = (maxp_ver == 0x00010000) ? 32 : 6;
    if (maxp_len > t_maxp->len) maxp_len = t_maxp->len;
    std::vector<uint8_t> maxp_data(maxp_p, maxp_p + maxp_len);
    while (maxp_data.size() < 6) maxp_data.push_back(0);
    maxp_data[4] = (N >> 8) & 0xFF;
    maxp_data[5] = N & 0xFF;

    // 9. cmap format 4 (per-cp single segments; idDelta encodes gid)
    struct seg_t { uint16_t end, start, delta; };
    std::vector<seg_t> segs;
    for (uint16_t i = 0; i < N; i++) {
        if (inc_list[i].cp != 0 && inc_list[i].cp <= 0xFFFF) {
            uint16_t cp = (uint16_t)inc_list[i].cp;
            segs.push_back({cp, cp, (uint16_t)(i - cp)});
        }
    }
    std::sort(segs.begin(), segs.end(),
              [](const seg_t& a, const seg_t& b) { return a.start < b.start; });
    segs.push_back({0xFFFF, 0xFFFF, 1});

    uint16_t segCount = (uint16_t)segs.size();
    uint16_t segX2 = segCount * 2;
    uint16_t es = 0, sr = 1;
    while (sr * 2 <= segCount) { sr *= 2; ++es; }
    sr *= 2;
    uint16_t rs = segX2 - sr;

    std::vector<uint8_t> fmt4;
    knst_ttf_put16(fmt4, 4);
    knst_ttf_put16(fmt4, 0);  // length placeholder
    knst_ttf_put16(fmt4, 0);  // language
    knst_ttf_put16(fmt4, segX2);
    knst_ttf_put16(fmt4, sr);
    knst_ttf_put16(fmt4, es);
    knst_ttf_put16(fmt4, rs);
    for (auto& s : segs) knst_ttf_put16(fmt4, s.end);
    knst_ttf_put16(fmt4, 0);
    for (auto& s : segs) knst_ttf_put16(fmt4, s.start);
    for (auto& s : segs) knst_ttf_put16(fmt4, s.delta);
    for (uint16_t i = 0; i < segCount; i++) knst_ttf_put16(fmt4, 0);
    uint16_t flen = (uint16_t)fmt4.size();
    fmt4[2] = (flen >> 8) & 0xFF;
    fmt4[3] = flen & 0xFF;

    std::vector<uint8_t> cmap_data;
    knst_ttf_put16(cmap_data, 0);
    knst_ttf_put16(cmap_data, 1);
    knst_ttf_put16(cmap_data, 3);
    knst_ttf_put16(cmap_data, 1);
    knst_ttf_put32(cmap_data, 12);
    cmap_data.insert(cmap_data.end(), fmt4.begin(), fmt4.end());

    // 10. Assemble TTF
    struct out_t { const char* tag; std::vector<uint8_t>* data; };
    std::vector<out_t> T = {
        {"cmap", &cmap_data}, {"glyf", &glyf_data}, {"head", &head_data},
        {"hhea", &hhea_data}, {"hmtx", &hmtx_data}, {"loca", &loca_data},
        {"maxp", &maxp_data},
    };
    std::sort(T.begin(), T.end(),
              [](const out_t& a, const out_t& b) { return std::memcmp(a.tag, b.tag, 4) < 0; });

    uint16_t ot_num = (uint16_t)T.size();
    uint32_t p2 = 1; uint16_t ot_es = 0;
    while (p2 * 2 <= ot_num) { p2 *= 2; ++ot_es; }
    uint16_t ot_sr = (uint16_t)(p2 * 16);
    uint16_t ot_rs = (uint16_t)(ot_num * 16 - ot_sr);

    std::vector<uint8_t> ttf;
    knst_ttf_put32(ttf, 0x00010000);
    knst_ttf_put16(ttf, ot_num);
    knst_ttf_put16(ttf, ot_sr);
    knst_ttf_put16(ttf, ot_es);
    knst_ttf_put16(ttf, ot_rs);

    // Directory: tag, checksum, offset, length
    uint32_t off = 12 + ot_num * 16;
    std::vector<uint32_t> offs(ot_num);
    for (uint16_t i = 0; i < ot_num; i++) {
        offs[i] = off;
        uint32_t len = (uint32_t)T[i].data->size();
        off += len;
        while (off % 4) off++;
    }
    for (uint16_t i = 0; i < ot_num; i++) {
        ttf.insert(ttf.end(), T[i].tag, T[i].tag + 4);
        knst_ttf_put32(ttf, knst_ttf_table_checksum(T[i].data->data(), T[i].data->size()));
        knst_ttf_put32(ttf, offs[i]);
        knst_ttf_put32(ttf, (uint32_t)T[i].data->size());
    }
    for (uint16_t i = 0; i < ot_num; i++) {
        ttf.insert(ttf.end(), T[i].data->begin(), T[i].data->end());
        while (ttf.size() % 4) ttf.push_back(0);
    }

    // 11. checkSumAdjustment
    uint32_t head_off = 0;
    for (uint16_t i = 0; i < ot_num; i++) {
        if (std::memcmp(T[i].tag, "head", 4) == 0) { head_off = offs[i]; break; }
    }
    if (head_off && head_off + 12 <= ttf.size()) {
        uint32_t adj = 0xB1B0AFBAu - knst_ttf_table_checksum(ttf.data(), ttf.size());
        ttf[head_off + 8]  = (adj >> 24) & 0xFF;
        ttf[head_off + 9]  = (adj >> 16) & 0xFF;
        ttf[head_off + 10] = (adj >> 8) & 0xFF;
        ttf[head_off + 11] = adj & 0xFF;
    }

    out.ttf_bytes = std::move(ttf);
    out.valid = true;
    return true;
}

} // namespace detail

// ===========================================================================
//             IPP client (RFC 8010/8011) — TLS + proxy + full attrs
// ===========================================================================
#if KNST_PRN_HAS_NETWORK
namespace detail {
namespace ipp {

enum : uint8_t {
    TAG_OPERATION_GROUP   = 0x01,
    TAG_JOB_ATTRIBUTES    = 0x02,
    TAG_END_OF_ATTRIBUTES = 0x03,
    TAG_INTEGER           = 0x21,
    TAG_BOOLEAN           = 0x22,
    TAG_ENUM              = 0x23,
    TAG_STRING            = 0x30,
    TAG_DATE              = 0x31,
    TAG_RESOLUTION        = 0x32,
    TAG_RANGE             = 0x33,
    TAG_NAME              = 0x42,   // nameWithoutLanguage
    TAG_KEYWORD           = 0x44,
    TAG_URI               = 0x45,
    TAG_CHARSET           = 0x47,
    TAG_NATURAL_LANGUAGE  = 0x48,
    TAG_MIME_TYPE         = 0x49,
};

enum : uint16_t {
    OP_PRINT_JOB              = 0x0002,
    OP_VALIDATE_JOB           = 0x0004,
    OP_CREATE_JOB             = 0x0005,
    OP_SEND_DOCUMENT          = 0x0006,
    OP_CANCEL_JOB             = 0x0008,
    OP_GET_JOB_ATTRIBUTES     = 0x0009,
    OP_GET_JOBS               = 0x000A,
    OP_GET_PRINTER_ATTRIBUTES = 0x000B,
    OP_SET_JOB_ATTRIBUTES     = 0x0013,
};

struct response {
    uint16_t status = 0;
    uint32_t request_id = 0;
    std::vector<uint8_t> raw;
    std::vector<std::pair<std::string, std::string>> attrs;
};

inline const std::string* attr_find(const response& r, const char* name) {
    for (auto& p : r.attrs) if (p.first == name) return &p.second;
    return nullptr;
}
inline std::string attr_get(const response& r, const char* name, const char* def = "") {
    auto p = attr_find(r, name);
    return p ? *p : std::string(def);
}
inline uint32_t attr_uint(const response& r, const char* name, uint32_t def = 0) {
    auto p = attr_find(r, name);
    if (!p || p->empty()) return def;
    return static_cast<uint32_t>(std::strtoul(p->c_str(), nullptr, 10));
}

// RFC 8011 §13.1.1: 0x0000-0x00FF = "successful" class (0x0001 ok-with-substitution included)
inline bool ipp_ok(uint16_t st) { return st < 0x0100; }

struct request {
    std::vector<uint8_t> buf;

        void begin(uint16_t op, uint32_t rid = 1) {
        buf.clear();
        
        buf.push_back(0x02); buf.push_back(0x00);
        buf.push_back(uint8_t(op >> 8)); buf.push_back(uint8_t(op & 0xFF));
        buf.push_back(uint8_t(rid >> 24)); buf.push_back(uint8_t(rid >> 16));
        buf.push_back(uint8_t(rid >> 8));  buf.push_back(uint8_t(rid & 0xFF));
        buf.push_back(TAG_OPERATION_GROUP);
    }
    void add_string(uint8_t tag, const std::string& n, const std::string& v) {
        buf.push_back(tag);
        buf.push_back(uint8_t(n.size() >> 8)); buf.push_back(uint8_t(n.size() & 0xFF));
        buf.insert(buf.end(), n.begin(), n.end());
        buf.push_back(uint8_t(v.size() >> 8)); buf.push_back(uint8_t(v.size() & 0xFF));
        buf.insert(buf.end(), v.begin(), v.end());
    }
    void add_int(uint8_t tag, const std::string& n, uint32_t v) {
        buf.push_back(tag);
        buf.push_back(uint8_t(n.size() >> 8)); buf.push_back(uint8_t(n.size() & 0xFF));
        buf.insert(buf.end(), n.begin(), n.end());
        buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x04);
        buf.push_back(uint8_t(v >> 24)); buf.push_back(uint8_t(v >> 16));
        buf.push_back(uint8_t(v >> 8));  buf.push_back(uint8_t(v & 0xFF));
    }
    void add_bool(const std::string& n, bool v) {
        buf.push_back(TAG_BOOLEAN);
        buf.push_back(uint8_t(n.size() >> 8)); buf.push_back(uint8_t(n.size() & 0xFF));
        buf.insert(buf.end(), n.begin(), n.end());
        buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x01);
        buf.push_back(v ? 1 : 0);
    }
    // Multiple enum values (1setOf): first entry carries the name, rest have empty name.
    void add_enum_1setof(const std::string& n, std::initializer_list<uint32_t> vals) {
        bool first = true;
        for (auto v : vals) { add_int(TAG_ENUM, first ? n : std::string(), v); first = false; }
    }
    // RFC 8011 §4.1.5: rangeOfInteger is 8 bytes (two 4-byte big-endians).
    // For 1setOf ranges the FIRST entry carries the name; subsequent
    // entries have an empty name.
    void add_range(const std::string& n, uint32_t lower, uint32_t upper) {
        buf.push_back(TAG_RANGE);                       // 0x33
        buf.push_back(uint8_t(n.size() >> 8)); buf.push_back(uint8_t(n.size() & 0xFF));
        buf.insert(buf.end(), n.begin(), n.end());
        buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x08);
        buf.push_back(uint8_t(lower >> 24)); buf.push_back(uint8_t(lower >> 16));
        buf.push_back(uint8_t(lower >> 8));  buf.push_back(uint8_t(lower & 0xFF));
        buf.push_back(uint8_t(upper >> 24)); buf.push_back(uint8_t(upper >> 16));
        buf.push_back(uint8_t(upper >> 8));  buf.push_back(uint8_t(upper & 0xFF));
    }

    // RFC 8011 §5.2.9: resolution is 4-byte cross-feed + 4-byte feed
    // + 1-byte units (3=dpi, 4=dpcm).
    void add_resolution(const std::string& n, uint32_t x, uint32_t y, uint8_t units = 3) {
        buf.push_back(TAG_RESOLUTION);
        buf.push_back(uint8_t(n.size() >> 8)); buf.push_back(uint8_t(n.size() & 0xFF));
        buf.insert(buf.end(), n.begin(), n.end());
        buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x09);
        buf.push_back(uint8_t(x >> 24)); buf.push_back(uint8_t(x >> 16));
        buf.push_back(uint8_t(x >> 8));  buf.push_back(uint8_t(x & 0xFF));
        buf.push_back(uint8_t(y >> 24)); buf.push_back(uint8_t(y >> 16));
        buf.push_back(uint8_t(y >> 8));  buf.push_back(uint8_t(y & 0xFF));
        buf.push_back(units);
    }

    void job_attrs()    { buf.push_back(TAG_JOB_ATTRIBUTES); }
    void end_of_attrs() { buf.push_back(TAG_END_OF_ATTRIBUTES); }
    void append_doc(const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); }
};

inline bool parse(const std::vector<uint8_t>& raw, response& out) {
    out = response{};
    if (raw.size() < 8) return false;
    out.status     = (uint16_t(raw[2]) << 8) | raw[3];
    out.request_id = (uint32_t(raw[4]) << 24) | (uint32_t(raw[5]) << 16) |
                     (uint32_t(raw[6]) << 8)  | raw[7];
    out.raw = raw;
    size_t pos = 8;
    std::string last_name;
    while (pos < raw.size()) {
        uint8_t tag = raw[pos++];
        if (tag == TAG_END_OF_ATTRIBUTES) break;
        if (tag < 0x10) { last_name.clear(); continue; }
        if (pos + 2 > raw.size()) return false;
        uint16_t nlen = (uint16_t(raw[pos]) << 8) | raw[pos + 1]; pos += 2;
        if (pos + nlen + 2 > raw.size()) return false;
        std::string name(reinterpret_cast<const char*>(&raw[pos]), nlen); pos += nlen;
        uint16_t vlen = (uint16_t(raw[pos]) << 8) | raw[pos + 1];         pos += 2;
        if (pos + vlen > raw.size()) return false;
        std::string val;
        if (tag == TAG_INTEGER || tag == TAG_ENUM) {
            if (vlen == 4) {
                uint32_t v = (uint32_t(raw[pos]) << 24) | (uint32_t(raw[pos+1]) << 16) |
                             (uint32_t(raw[pos+2]) << 8) | raw[pos+3];
                val = std::to_string(v);
            }
        } else if (tag == TAG_BOOLEAN) {
            val = (vlen == 1 && raw[pos]) ? "true" : "false";
        } else if (tag == TAG_RESOLUTION && vlen == 9) {
            val = std::to_string(detail::be32(&raw[pos])) + "x" +
                  std::to_string(detail::be32(&raw[pos + 4])) +
                  (raw[pos + 8] == 4 ? "dpcm" : "dpi");
        } else {
            val.assign(reinterpret_cast<const char*>(&raw[pos]), vlen);
        }
        if (name.empty()) {
            if (!last_name.empty()) {
                for (auto& p : out.attrs)
                    if (p.first == last_name) { p.second += ","; p.second += val; break; }
            }
        } else {
            last_name = name;
            out.attrs.emplace_back(name, val);
        }
        pos += vlen;
    }
    return true;
}

// ───────────── portable sockets (Linux / Android / Windows) ─────────────
#if defined(_WIN32)
using sock_t = SOCKET;
inline constexpr sock_t kBadSock = INVALID_SOCKET;
using net_len_t = int;
inline bool net_init() { static const bool ok = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }(); return ok; }
inline void sock_close(sock_t s) { ::closesocket(s); }
inline int  sock_send(sock_t s, const void* p, size_t n) { return ::send(s, static_cast<const char*>(p), static_cast<int>(std::min<size_t>(n, 1u << 20)), 0); }
inline int  sock_recv(sock_t s, void* p, size_t n) { return ::recv(s, static_cast<char*>(p), static_cast<int>(n), 0); }
inline bool sock_retry() { return false; }
inline void sock_timeouts(sock_t s, uint32_t ms) {
    DWORD t = ms;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
}
inline void sock_nonblock(sock_t s, bool nb) { u_long v = nb ? 1 : 0; ::ioctlsocket(s, FIONBIO, &v); }
inline bool sock_in_progress() { return ::WSAGetLastError() == WSAEWOULDBLOCK; }
inline bool sock_wait_writable(sock_t s, uint32_t ms) { WSAPOLLFD p{}; p.fd = s; p.events = POLLWRNORM; return ::WSAPoll(&p, 1, static_cast<INT>(ms)) > 0; }
#else
using sock_t = int;
inline constexpr sock_t kBadSock = -1;
using net_len_t = socklen_t;
inline bool net_init() { return true; }
inline void sock_close(sock_t s) { ::close(s); }
inline int  sock_send(sock_t s, const void* p, size_t n) { return static_cast<int>(::send(s, p, std::min<size_t>(n, 1u << 20), MSG_NOSIGNAL)); }
inline int  sock_recv(sock_t s, void* p, size_t n) { return static_cast<int>(::recv(s, p, n, 0)); }
inline bool sock_retry() { return errno == EINTR; }
inline void sock_timeouts(sock_t s, uint32_t ms) {
    timeval tv{}; tv.tv_sec = static_cast<time_t>(ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
inline void sock_nonblock(sock_t s, bool nb) {
    int f = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, nb ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK));
}
inline bool sock_in_progress() { return errno == EINPROGRESS; }
inline bool sock_wait_writable(sock_t s, uint32_t ms) {
    pollfd p{}; p.fd = s; p.events = POLLOUT;
    return ::poll(&p, 1, static_cast<int>(ms)) > 0;
}
#endif

// TCP connect with a real connect timeout (never hangs on unreachable host).
inline sock_t tcp_connect(const std::string& host, const std::string& port, uint32_t connect_ms,
                          uint32_t idle_ms, knst_network_error& err) {
    if (!net_init()) { err = knst_network_error::ProtocolError; return kBadSock; }
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        err = knst_network_error::DnsFailure; return kBadSock;
    }
    sock_t fd = kBadSock;
    for (addrinfo* ai = res; ai && fd == kBadSock; ai = ai->ai_next) {
        sock_t s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == kBadSock) continue;
        sock_nonblock(s, true);
        bool ok = (::connect(s, ai->ai_addr, static_cast<net_len_t>(ai->ai_addrlen)) == 0);
        if (!ok && sock_in_progress() && sock_wait_writable(s, connect_ms)) {
            int so = 1; net_len_t sl = sizeof(so);
            ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so), &sl);
            ok = (so == 0);
        }
        if (!ok) { sock_close(s); continue; }
        sock_nonblock(s, false);
        sock_timeouts(s, idle_ms);
        fd = s;
    }
    ::freeaddrinfo(res);
    if (fd == kBadSock) err = knst_network_error::ConnectionRefused;
    return fd;
}

// One IPP POST over plain HTTP. Verifies the response is COMPLETE (Content-Length / chunked end).
inline bool http_post_ipp(const std::string& host, uint16_t port, const std::vector<uint8_t>& body,
                          uint32_t connect_ms, uint32_t idle_ms, std::vector<uint8_t>& out,
                          knst_network_error& err) {
    out.clear();
    sock_t fd = tcp_connect(host, std::to_string(port), connect_ms, idle_ms, err);
    if (fd == kBadSock) return false;

    const std::string hdr = "POST /ipp/print HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) +
        "\r\nContent-Type: application/ipp\r\nAccept: application/ipp\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\nConnection: close\r\nUser-Agent: KernelNucleusT/1.0\r\n\r\n";

    auto send_all = [&](const void* p, size_t n) -> bool {
        const char* c = static_cast<const char*>(p);
        while (n > 0) {
            int w = sock_send(fd, c, n);
            if (w <= 0) { if (w < 0 && sock_retry()) continue; return false; }
            c += w; n -= static_cast<size_t>(w);
        }
        return true;
    };
    const bool sent = send_all(hdr.data(), hdr.size()) && send_all(body.data(), body.size());

    std::string raw; char buf[16384];
    size_t hdr_end = std::string::npos, clen = std::string::npos; bool chunked = false;
    for (;;) {
        int n = sock_recv(fd, buf, sizeof(buf));
        if (n < 0) { if (sock_retry()) continue; break; }
        if (n == 0) break;
        raw.append(buf, static_cast<size_t>(n));
        if (hdr_end == std::string::npos) {
            hdr_end = raw.find("\r\n\r\n");
            if (hdr_end != std::string::npos) {
                std::string low = raw.substr(0, hdr_end);
                for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                size_t cl = low.find("content-length:");
                if (cl != std::string::npos) clen = std::strtoul(low.c_str() + cl + 15, nullptr, 10);
                if (low.find("transfer-encoding: chunked") != std::string::npos) chunked = true;
            }
        }
        if (hdr_end != std::string::npos) {
            size_t have = raw.size() - (hdr_end + 4);
            if (!chunked && clen != std::string::npos && have >= clen) break;
            if (chunked && have >= 5 &&
                std::memcmp(raw.data() + raw.size() - 5, "0\r\n\r\n", 5) == 0) break;
        }
        if (raw.size() > 64u * 1024u * 1024u) break;
    }
    sock_close(fd);

    if (hdr_end == std::string::npos) {
        err = sent ? knst_network_error::ProtocolError : knst_network_error::ConnectionReset;
        return false;
    }
    int http_status = 0;
    size_t sp = raw.find(' ');
    if (sp != std::string::npos) http_status = std::atoi(raw.c_str() + sp + 1);
    if (http_status != 200) { err = knst_network_error::ProtocolError; return false; }

    std::string b = raw.substr(hdr_end + 4);
    if (chunked) {
        std::string dec; size_t pos = 0; bool done = false;
        while (pos < b.size()) {
            size_t eol = b.find("\r\n", pos);
            if (eol == std::string::npos) break;
            std::string szstr = b.substr(pos, eol - pos);
            size_t semi = szstr.find(';');
            if (semi != std::string::npos) szstr = szstr.substr(0, semi);
            unsigned long sz = std::strtoul(szstr.c_str(), nullptr, 16);
            pos = eol + 2;
            if (sz == 0) { done = true; break; }
            if (pos + sz > b.size()) break;
            dec.append(b, pos, sz); pos += sz + 2;
        }
        if (!done) { err = knst_network_error::ProtocolError; return false; }
        b.swap(dec);
    } else if (clen != std::string::npos) {
        if (b.size() < clen) { err = knst_network_error::ProtocolError; return false; }
        b.resize(clen);
    }
    out.assign(b.begin(), b.end());
    return true;
}
inline bool send(const std::string& host_port, bool use_tls,
                 const std::vector<uint8_t>& body, uint32_t timeout_ms,
                 response& out, knst_network_error& err) {
    err = knst_network_error::None;
    out = response{};

    // Host:port parse
    std::string host = host_port;
    uint16_t port = use_tls ? 443 : 631;
    size_t colon = host_port.rfind(':');
    if (colon != std::string::npos) {
        host = host_port.substr(0, colon);
        int p = std::atoi(host_port.c_str() + colon + 1);
        if (p > 0) port = static_cast<uint16_t>(p);
    }
    if (host.empty()) { err = knst_network_error::InvalidArgument; return false; }

    // ── TLS yolu: knst_http_client (self-signed cert bypass ile) ──
    if (use_tls) {
        std::string url_s = "https://" + host + ":" + std::to_string(port) + "/ipp/print";
        knst_c16string url = detail::from_u8(url_s);
        knst_http_client::options o;
        o.timeout_ms = timeout_ms;
        o.total_timeout_ms = timeout_ms;
        o.use_keepalive = false;                // HP printerlarda keep-alive KIRIK
        o.max_redirects = 0;
        o.allow_content_encoding = false;
        o.auto_cookie = false;
        o.allow_private_networks = true;
        o.user_agent = u"KernelNucleusT/1.0";
        { knst_http_header h; h.name = u"Content-Type"; h.value = u"application/ipp"; o.headers.push_back(h); }
        { knst_http_header h; h.name = u"Accept";       h.value = u"application/ipp"; o.headers.push_back(h); }
        knst_byte_string bb;
        bb.resize(static_cast<uint32_t>(body.size()));
        if (!body.empty()) std::memcpy(&bb[0], body.data(), body.size());
        knst_http_client client;
        knst_http_response resp;
        if (!client.post(url, bb, o, resp, err)) return false;
        if (resp.status != 200) { err = knst_network_error::ProtocolError; return false; }
        if (resp.body.length() < 8) { err = knst_network_error::ProtocolError; return false; }
        std::vector<uint8_t> raw(resp.body.length());
        std::memcpy(raw.data(), resp.body.data(), resp.body.length());
        return parse(raw, out);
    }


    std::string req_headers;
    req_headers.reserve(256 + body.size());
    req_headers += "POST /ipp/print HTTP/1.1\r\n";
    req_headers += "Host: " + host + ":" + std::to_string(port) + "\r\n";
    req_headers += "Content-Type: application/ipp\r\n";
    req_headers += "Accept: application/ipp\r\n";
    req_headers += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req_headers += "Connection: close\r\n";
    req_headers += "User-Agent: KernelNucleusT/1.0\r\n";
    req_headers += "\r\n";

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    std::string port_str = std::to_string(port);
    struct addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        err = knst_network_error::DnsFailure; return false;
    }
    int fd = -1;
    for (auto* ai = res; ai; ai = ai->ai_next) {
        int s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0) continue;
        struct timeval tv{};
        tv.tv_sec  = static_cast<time_t>(timeout_ms / 1000);
        #if KNST_USING_PLATFORM_WINDOWS
        // Windows'ta SO_RCVTIMEO/SO_SNDTIMEO bir DWORD (milisaniye) bekler,
        // timeval DEĞİL. `tv` (yukarıda tanımlıysa) bu platformda kullanılmaz.
        (void)tv;
        DWORD _knst_tmo = static_cast<DWORD>(timeout_ms);
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&_knst_tmo),
                     static_cast<int>(sizeof(_knst_tmo)));
        ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char*>(&_knst_tmo),
                     static_cast<int>(sizeof(_knst_tmo)));
#else
        tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
        if (::connect(s, ai->ai_addr, ai->ai_addrlen) == 0) { fd = s; break; }
        ::close(s);
    }
    ::freeaddrinfo(res);
    if (fd < 0) { err = knst_network_error::ConnectionRefused; return false; }

    // Headers gönder
    const char* hp = req_headers.data();
    size_t hleft = req_headers.size();
    while (hleft > 0) {
        ssize_t n = ::send(fd, hp, hleft, MSG_NOSIGNAL);
                if (n <= 0) { if (n < 0 && errno == EINTR) continue; ::close(fd); err = knst_network_error::ConnectionReset; return false; }
        hp += n; hleft -= static_cast<size_t>(n);
    }
    // Body gönder (JPEG/PDF bytes)
    const char* bp = reinterpret_cast<const char*>(body.data());
    size_t bleft = body.size();
    while (bleft > 0) {
        ssize_t n = ::send(fd, bp, bleft, MSG_NOSIGNAL);
        if (n <= 0) { if (n < 0 && errno == EINTR) continue; ::close(fd); err = knst_network_error::ConnectionReset; return false; }
        bp += n; bleft -= static_cast<size_t>(n);
    }

    // Response oku
    std::string raw;
    char rbuf[16384];
    size_t hdr_end = std::string::npos;
    size_t clen = std::string::npos;
    bool chunked = false;
    for (;;) {
        ssize_t n = ::recv(fd, rbuf, sizeof(rbuf), 0);
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        raw.append(rbuf, static_cast<size_t>(n));
        if (hdr_end == std::string::npos) {
            hdr_end = raw.find("\r\n\r\n");
            if (hdr_end != std::string::npos) {
                std::string hh = raw.substr(0, hdr_end);
                std::string low = hh;
                for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                size_t cl = low.find("content-length:");
                if (cl != std::string::npos)
                    clen = std::strtoul(hh.c_str() + cl + 15, nullptr, 10);
                if (low.find("chunked") != std::string::npos) chunked = true;
            }
        }
        if (hdr_end != std::string::npos) {
            size_t have = raw.size() - (hdr_end + 4);
            if (!chunked && clen != std::string::npos && have >= clen) break;
            if (chunked && have >= 5) {
                const char* tail = raw.data() + raw.size() - 5;
                if (std::memcmp(tail, "0\r\n\r\n", 5) == 0) break;
            }
        }
        if (raw.size() > 64u * 1024u * 1024u) break;
    }
    ::close(fd);

    if (hdr_end == std::string::npos) { err = knst_network_error::ProtocolError; return false; }

    int http_status = 0;
    size_t sp = raw.find(' ');
    if (sp != std::string::npos) http_status = std::atoi(raw.c_str() + sp + 1);
    if (http_status != 200) { err = knst_network_error::ProtocolError; return false; }

    std::string body_raw = raw.substr(hdr_end + 4);
    if (chunked) {
        std::string decoded;
        size_t pos = 0;
        while (pos < body_raw.size()) {
            size_t eol = body_raw.find("\r\n", pos);
            if (eol == std::string::npos) break;
            std::string szstr = body_raw.substr(pos, eol - pos);
            size_t semi = szstr.find(';');
            if (semi != std::string::npos) szstr = szstr.substr(0, semi);
            unsigned long sz = std::strtoul(szstr.c_str(), nullptr, 16);
            pos = eol + 2;
            if (sz == 0) break;
            if (pos + sz > body_raw.size()) { decoded.append(body_raw, pos, body_raw.size() - pos); break; }
            decoded.append(body_raw, pos, sz);
            pos += sz;
            if (pos + 2 <= body_raw.size() && body_raw[pos] == '\r' && body_raw[pos + 1] == '\n') pos += 2;
        }
        body_raw = std::move(decoded);
    } else if (clen != std::string::npos && body_raw.size() > clen) {
        body_raw.resize(clen);
    }

    if (body_raw.size() < 8) { err = knst_network_error::ProtocolError; return false; }

    std::vector<uint8_t> resp_raw(body_raw.size());
    std::memcpy(resp_raw.data(), body_raw.data(), body_raw.size());
    return parse(resp_raw, out);
}

// Split "ipp://host:port/path" → (host_port, use_tls). Returns false on malformed input.
inline bool split_uri(std::string hp, std::string& host_port, bool& use_tls, uint16_t& port) {
    use_tls = false;
    if (hp.compare(0, 7, "ipps://") == 0) { use_tls = true; port = 443; hp = hp.substr(7); }
    else if (hp.compare(0, 6, "ipp://") == 0) { hp = hp.substr(6); }
    else if (hp.compare(0, 8, "https://") == 0) { use_tls = true; port = 443; hp = hp.substr(8); }
    else if (hp.compare(0, 7, "http://") == 0) { hp = hp.substr(7); }
    size_t slash = hp.find('/');
    if (slash != std::string::npos) hp = hp.substr(0, slash);
    std::string host = hp;
    size_t colon = hp.rfind(':');
    if (colon != std::string::npos) {
        host = hp.substr(0, colon);
        int p = std::atoi(hp.c_str() + colon + 1);
        if (p > 0) port = static_cast<uint16_t>(p);
    }
    if (host.empty()) return false;
    host_port = host + ":" + std::to_string(port);
    return true;
}

// Parse "600dpi", "600x600dpi", "300x1200dpi", "300dpcm" → (x, y, units)
inline bool parse_resolution(const std::string& s, uint32_t& x, uint32_t& y, uint8_t& units) {
    units = 3;
    x = y = 0;
    size_t p = 0;
    auto parse_num = [&](uint32_t& v) -> bool {
        size_t start = p;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        if (p == start) return false;
        v = (uint32_t)std::strtoul(s.substr(start, p - start).c_str(), nullptr, 10);
        return v > 0;
    };
    if (!parse_num(x)) return false;
    y = x;
    if (p < s.size() && (s[p] == 'x' || s[p] == 'X')) {
        ++p;
        if (!parse_num(y)) return false;
    }
    std::string tail = s.substr(p);
    for (auto& c : tail) c = (char)std::tolower((unsigned char)c);
    if (tail == "dpi" || tail.empty()) units = 3;
    else if (tail == "dpcm") units = 4;
    else return false;
    return true;
}



inline job_state job_state_from_uint(uint32_t s) noexcept {
    switch (s) {
        case 3: return job_state::Pending;
        case 4: return job_state::Held;
        case 5: return job_state::Processing;
        case 6: return job_state::Stopped;
        case 7: return job_state::Canceled;
        case 8: return job_state::Aborted;
        case 9: return job_state::Completed;
        default: return job_state::Unknown;
    }
}

// ─── Shared Print-Job sender ────────────────────────────────────────────
// Reads a PDF (or any file) from disk, submits as IPP Print-Job.
// Works with ipp:// (plain) and ipps:// (TLS via knst_http_client).
inline result<uint32_t> print_pdf(
        const knst_c16string& pdf_path,
        const knst_c16string& host_port,
        uint16_t port,
        const print_options& opts,
        const knst_c16string& doc_format = u"application/pdf") {
    using R = result<uint32_t>;
    if (pdf_path.empty() || host_port.empty()) return R::fail(status::InvalidArgument);

    knst_file_error fe = knst_file_error::None;
    knst_byte_string pdf = knst_file::read_file_data<>(pdf_path, &fe);
    if (fe != knst_file_error::None || pdf.empty()) return R::fail(status::IoError);

    std::string hpu;
    bool use_tls = false;
    if (!split_uri(detail::to_u8(host_port), hpu, use_tls, port))
        return R::fail(status::InvalidArgument);

    std::string uri = std::string(use_tls ? "ipps" : "ipp") + "://" + hpu + "/ipp/print";

    request req;
    req.begin(OP_PRINT_JOB);


    req.add_string(TAG_CHARSET,          "attributes-charset",          "utf-8");
        req.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    req.add_string(TAG_URI,              "printer-uri",                 uri);
    req.add_string(TAG_NAME,             "requesting-user-name",        "knst");
    req.add_string(TAG_NAME,             "job-name",
                   detail::to_u8(opts.title.empty() ? u"KNST document" : opts.title));
    req.add_string(TAG_MIME_TYPE,        "document-format",             detail::to_u8(doc_format));

            req.job_attrs();

    if (opts.copies > 1) {
        req.add_int(TAG_INTEGER, "copies", opts.copies);
    }

    if (!opts.media.empty()) {
        req.add_string(TAG_KEYWORD, "media", detail::to_u8(opts.media));
    }

    switch (opts.duplex) {
        case duplex_mode::Simplex:   req.add_string(TAG_KEYWORD, "sides", "one-sided");            break;
        case duplex_mode::LongEdge:  req.add_string(TAG_KEYWORD, "sides", "two-sided-long-edge");  break;
        case duplex_mode::ShortEdge: req.add_string(TAG_KEYWORD, "sides", "two-sided-short-edge"); break;
        default: break;
    }
    switch (opts.color) {
        case color_mode::Color: req.add_string(TAG_KEYWORD, "print-color-mode", "color");      break;
        case color_mode::Mono:
        case color_mode::Gray:  req.add_string(TAG_KEYWORD, "print-color-mode", "monochrome"); break;
        default: break;
    }

    req.end_of_attrs();
    req.append_doc(pdf.data(), pdf.length());

        response resp;
    knst_network_error err = knst_network_error::None;
    // HP printers have slow TLS handshake (30-120s) — cap at 15s so ipps:443
    // fails fast and we can fall through to plain IPP.
    uint32_t send_ms = use_tls ? 15000u : 120000u;
    if (!send(hpu, use_tls, req.buf, send_ms, resp, err)) {

        return R::fail(status::IoError, resp.status);
    }
    return R::success(attr_uint(resp, "job-id", 0));
}

// ─── Print-Job alternatifi: Create-Job + Send-Document (1 PDF) ────────
// HP 7740 gibi bazı printer'lar Print-Job'da inline PDF kabul etmez.
// İki adımlı akış: önce job yarat, sonra PDF'i ayrı gönder.
inline result<uint32_t> print_pdf_send_doc(
        const knst_c16string& pdf_path,
        const knst_c16string& host_port,
        uint16_t port,
        const print_options& opts,
        const knst_c16string& doc_format = u"application/pdf") {
    using R = result<uint32_t>;
    if (pdf_path.empty() || host_port.empty()) return R::fail(status::InvalidArgument);

    knst_file_error fe = knst_file_error::None;
    knst_byte_string pdf = knst_file::read_file_data<>(pdf_path, &fe);
    if (fe != knst_file_error::None || pdf.empty()) return R::fail(status::IoError);

    std::string hpu; bool use_tls = false;
    if (!split_uri(detail::to_u8(host_port), hpu, use_tls, port))
        return R::fail(status::InvalidArgument);
    // Strip :631 for plain IPP (HP 7740 advertises without :631 — RFC 8011 §4.1.5)
    std::string uri_authority = hpu;
    if (!use_tls) {
        size_t colon = uri_authority.rfind(':');
        if (colon != std::string::npos) uri_authority = uri_authority.substr(0, colon);
    }
    std::string uri = std::string(use_tls ? "ipps" : "ipp") + "://" + uri_authority + "/ipp/print";
    knst_network_error err = knst_network_error::None;

    // ── 1) Create-Job ──
    request cj;
    cj.begin(OP_CREATE_JOB);
    cj.add_string(TAG_CHARSET,          "attributes-charset",          "utf-8");
        cj.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    cj.add_string(TAG_URI,              "printer-uri",                 uri);
    cj.add_string(TAG_NAME,             "requesting-user-name",        "knst");
    cj.add_string(TAG_NAME,             "job-name",                    "KNST document");
    cj.job_attrs();
    if (opts.copies > 1) cj.add_int(TAG_INTEGER, "copies", opts.copies);
    const char* media = nullptr;
    switch (opts.paper) {
        case media_size::A4:     media = "iso_a4_210x297mm";   break;
        case media_size::A3:     media = "iso_a3_297x420mm";   break;
        case media_size::Letter: media = "na_letter_8.5x11in"; break;
        default: break;
    }
    if (media) cj.add_string(TAG_KEYWORD, "media", media);
    cj.end_of_attrs();

    response cj_resp;
    if (!send(hpu, use_tls, cj.buf, 30000, cj_resp, err)) {

        return R::fail(status::IoError, static_cast<int>(err));
    }
    if (cj_resp.status >= 0x0300) {
        std::string msg = attr_get(cj_resp, "status-message");

        return R::fail(status::IoError, cj_resp.status);
    }
    uint32_t job_id = attr_uint(cj_resp, "job-id", 0);
    if (job_id == 0) return R::fail(status::BadState);

    // ── 2) Send-Document ──
    request sd;
    sd.begin(OP_SEND_DOCUMENT);
    sd.add_string(TAG_CHARSET,          "attributes-charset",          "utf-8");
    sd.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    sd.add_string(TAG_URI,              "printer-uri",                 uri);
    sd.add_int   (TAG_INTEGER,          "job-id",                      job_id);
    sd.add_string(TAG_NAME,             "requesting-user-name",        "knst");
    sd.add_string(TAG_MIME_TYPE,        "document-format",             detail::to_u8(doc_format));
    sd.add_bool("last-document", true);
    sd.end_of_attrs();
    sd.append_doc(pdf.data(), pdf.length());

    response sd_resp;
    if (!send(hpu, use_tls, sd.buf, 120000, sd_resp, err)) {

        return R::fail(status::IoError, static_cast<int>(err));
    }
    if (sd_resp.status >= 0x0300) {
        std::string msg = attr_get(sd_resp, "status-message");

        return R::fail(status::IoError, sd_resp.status);
    }
    return R::success(job_id);
}

// ─── Shared Create-Job + Send-Document × N ─────────────────────────────
// ─── Shared Create-Job + Send-Document × N ─────────────────────────────
// RFC 8011 §4.2.1 (Create-Job) + §4.3.1 (Send-Document).
// All N pages go into ONE IPP job → sides=duplex works, printer queue
// shows 1 job (not N), finishings applied to whole job, collate works.
inline result<uint32_t> print_jpegs(
        const std::vector<knst_c16string>& jpeg_paths,
        const knst_c16string& host_port,
        uint16_t port,
        const print_options& opts) {
    using R = result<uint32_t>;
    if (jpeg_paths.empty() || host_port.empty()) return R::fail(status::InvalidArgument);

    std::string hpu;
    bool use_tls = false;
    if (!split_uri(detail::to_u8(host_port), hpu, use_tls, port))
        return R::fail(status::InvalidArgument);

    // RFC 8011 §4.1.5: printer-uri MUST match printer-uri-supported EXACTLY.
    // HP 7740 advertises "ipp://192.168.1.239/ipp/print" (no :631).
    // Including :631 causes Create-Job → 0x0400 bad-request.
    std::string uri_authority = hpu;
    if (!use_tls && port == 631) {
        size_t colon = uri_authority.rfind(':');
        if (colon != std::string::npos) uri_authority = uri_authority.substr(0, colon);
    }
    std::string uri = std::string(use_tls ? "ipps" : "ipp") + "://" + uri_authority + "/ipp/print";

    knst_network_error err = knst_network_error::None;


    request cj;
    cj.begin(OP_CREATE_JOB);
    cj.add_string(TAG_CHARSET,          "attributes-charset",          "utf-8");
    cj.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    cj.add_string(TAG_URI,              "printer-uri",                 uri);
    cj.end_of_attrs();

    response cj_resp;
    if (!send(hpu, use_tls, cj.buf, 60000, cj_resp, err)) {

        return R::fail(status::IoError, static_cast<int>(err));
    }
    if (cj_resp.status != 0x0000) {
        std::string msg = attr_get(cj_resp, "status-message");

        return R::fail(status::IoError, cj_resp.status);
    }
    uint32_t job_id = attr_uint(cj_resp, "job-id", 0);
    if (job_id == 0) return R::fail(status::BadState);


    for (size_t i = 0; i < jpeg_paths.size(); ++i) {
        knst_file_error fe = knst_file_error::None;
        knst_byte_string data = knst_file::read_file_data<>(jpeg_paths[i], &fe);
        if (fe != knst_file_error::None || data.empty())
            return R::fail(status::IoError);
        bool last = (i + 1 == jpeg_paths.size());

        request sd;
        sd.begin(OP_SEND_DOCUMENT);
        sd.add_string(TAG_CHARSET,          "attributes-charset",          "utf-8");
        sd.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
        sd.add_string(TAG_URI,              "printer-uri",                 uri);
        sd.add_int   (TAG_INTEGER,          "job-id",                      job_id);
        sd.add_string(TAG_NAME,             "requesting-user-name",        "knst");
        sd.add_string(TAG_MIME_TYPE,        "document-format",             "image/jpeg");
        sd.add_bool("last-document", last);
        sd.end_of_attrs();
        sd.append_doc(data.data(), data.length());

        response sd_resp;
        if (!send(hpu, use_tls, sd.buf, 120000, sd_resp, err)) {

            return R::fail(status::IoError, static_cast<int>(err));
        }
        if (sd_resp.status != 0x0000) {
            std::string msg = attr_get(sd_resp, "status-message");

            return R::fail(status::IoError, sd_resp.status);
        }
    }
    return R::success(job_id);
}

// ─── Get-Printer-Attributes (all / requested) ───
inline result<response> get_printer_attrs(
        const std::string& host_port, bool use_tls,
        std::initializer_list<const char*> attrs) {
    using R = result<response>;
    if (host_port.empty()) return R::fail(status::InvalidArgument);

    std::string uri = std::string(use_tls ? "ipps" : "ipp") + "://" + host_port + "/ipp/print";
    request req;
    req.begin(OP_GET_PRINTER_ATTRIBUTES);
    req.add_string(TAG_CHARSET,          "attributes-charset",         "utf-8");
    req.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    req.add_string(TAG_URI,              "printer-uri",                uri);

    if (attrs.size() == 0) {
        req.add_string(TAG_KEYWORD, "requested-attributes", "all");
    } else {
        bool first = true;
        for (const char* a : attrs) {
            req.add_string(TAG_KEYWORD, first ? "requested-attributes" : std::string(), a);
            first = false;
        }
    }
    req.end_of_attrs();

    response resp;
    knst_network_error err = knst_network_error::None;
    if (!send(host_port, use_tls, req.buf, 8000, resp, err))
        return R::fail(status::IoError);
    if (!ipp_ok(resp.status))
        return R::fail(status::IoError, resp.status);
    return R::success(std::move(resp));
}

// ─── Get-Job-Attributes for a specific job ───
inline result<response> get_job_attrs(
        const std::string& host_port, bool use_tls, uint32_t job_id) {
    using R = result<response>;
    if (host_port.empty() || job_id == 0) return R::fail(status::InvalidArgument);

    std::string uri = std::string(use_tls ? "ipps" : "ipp") + "://" + host_port + "/ipp/print";
    request req;
    req.begin(OP_GET_JOB_ATTRIBUTES);
    req.add_string(TAG_CHARSET,          "attributes-charset",         "utf-8");
    req.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    req.add_string(TAG_URI,              "printer-uri",                uri);
    req.add_int   (TAG_INTEGER,          "job-id",                     job_id);
    req.add_string(TAG_NAME,             "requesting-user-name",       "knst");
    req.add_string(TAG_KEYWORD,          "requested-attributes",       "all");
    req.end_of_attrs();

    response resp;
    knst_network_error err = knst_network_error::None;
    if (!send(host_port, use_tls, req.buf, 8000, resp, err))
        return R::fail(status::IoError);
    if (resp.status == 0x0406) return R::fail(status::NotFound, resp.status);
    if (!ipp_ok(resp.status))
        return R::fail(status::IoError, resp.status);
    return R::success(std::move(resp));
}

// ─── Get-Jobs (list) ───
inline result<response> get_jobs(
        const std::string& host_port, bool use_tls, bool include_completed) {
    using R = result<response>;
    if (host_port.empty()) return R::fail(status::InvalidArgument);

    std::string uri = std::string(use_tls ? "ipps" : "ipp") + "://" + host_port + "/ipp/print";
    request req;
    req.begin(OP_GET_JOBS);
    req.add_string(TAG_CHARSET,          "attributes-charset",         "utf-8");
    req.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    req.add_string(TAG_URI,              "printer-uri",                uri);
    req.add_string(TAG_NAME,             "requesting-user-name",       "knst");
    req.add_bool("my-jobs", false);
    req.add_string(TAG_KEYWORD, "which-jobs",
                   include_completed ? "completed" : "not-completed");
    bool first = true;
    for (const char* a : {"job-id", "job-name", "job-state", "job-state-reasons",
                          "job-originating-user-name", "job-k-octets",
                          "job-media-sheets-completed", "job-priority",
                          "time-at-creation"}) {
        req.add_string(TAG_KEYWORD, first ? "requested-attributes" : std::string(), a);
        first = false;
    }
    req.end_of_attrs();

    response resp;
    knst_network_error err = knst_network_error::None;
    if (!send(host_port, use_tls, req.buf, 8000, resp, err))
        return R::fail(status::IoError);
    if (!ipp_ok(resp.status))
        return R::fail(status::IoError, resp.status);
    return R::success(std::move(resp));
}

// ─── Cancel-Job ───
inline result<void> cancel_job_ipp(
        const std::string& host_port, bool use_tls, uint32_t job_id) {
    using R = result<void>;
    if (host_port.empty() || job_id == 0) return R::fail(status::InvalidArgument);

    std::string uri = std::string(use_tls ? "ipps" : "ipp") + "://" + host_port + "/ipp/print";
    request req;
    req.begin(OP_CANCEL_JOB);
    req.add_string(TAG_CHARSET,          "attributes-charset",         "utf-8");
    req.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    req.add_string(TAG_URI,              "printer-uri",                uri);
    req.add_int   (TAG_INTEGER,          "job-id",                     job_id);
    req.add_string(TAG_NAME,             "requesting-user-name",       "knst");
    req.end_of_attrs();

    response resp;
    knst_network_error err = knst_network_error::None;
    if (!send(host_port, use_tls, req.buf, 8000, resp, err))
        return R::fail(status::IoError);
    // RFC 8011 §13.1.5: 0x0404 = not-possible (job already done), 0x0406 = not-found.
    if (resp.status == 0x0404 || resp.status == 0x0406) return R::fail(status::BadState, resp.status);
    if (!ipp_ok(resp.status)) return R::fail(status::IoError, resp.status);
    return R::success();
}

// ─── Set-Job-Attributes (RFC 8011 §4.3.6) ───
// Best-effort job state change. Most IPP printers accept:
//   job-state=3 (pending) for release
//   job-state=4 (held)    for hold
//   job-priority=1..100   for priority change
// Some printers reject job-state transitions → returns NotSupported.
inline result<void> set_job_attrs(
        const std::string& host_port, bool use_tls, uint32_t job_id,
        std::initializer_list<std::pair<const char*, uint32_t>> attrs) {
    using R = result<void>;
    if (host_port.empty() || job_id == 0) return R::fail(status::InvalidArgument);

    std::string uri = std::string(use_tls ? "ipps" : "ipp") + "://" + host_port + "/ipp/print";
    request req;
    req.begin(OP_SET_JOB_ATTRIBUTES);
    req.add_string(TAG_CHARSET,          "attributes-charset",         "utf-8");
    req.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    req.add_string(TAG_URI,              "printer-uri",                uri);
    req.add_int   (TAG_INTEGER,          "job-id",                     job_id);
    req.add_string(TAG_NAME,             "requesting-user-name",       "knst");
    req.job_attrs();
    for (auto& a : attrs) req.add_int(TAG_INTEGER, a.first, a.second);
    req.end_of_attrs();

    response resp;
    knst_network_error err = knst_network_error::None;
    if (!send(host_port, use_tls, req.buf, 8000, resp, err))
        return R::fail(status::IoError);
    // RFC 8011 §13.1.5: 0x0404 = client-error-not-possible
    //                   0x040B = client-error-attributes-or-values-not-supported
    if (resp.status == 0x0404 || resp.status == 0x040B)
        return R::fail(status::NotSupported, resp.status);
    if (!ipp_ok(resp.status)) return R::fail(status::IoError, resp.status);
    return R::success();
}

inline bool parse_cups_uri(const std::string& cups_uri,
                            std::string& host_port, bool& use_tls) {
    uint16_t port_tmp = 631;
    return split_uri(cups_uri, host_port, use_tls, port_tmp);
}

// ═══════════ Robust engine: probe / preflight / submit / wait / PWG raster ═══════════
inline std::string& last_diag() { static thread_local std::string s; return s; }

struct send_policy {
    uint32_t    io_idle_ms          = 60000;      // max silence on socket (not whole operation)
    uint32_t    busy_total_ms       = 180000;     // how long 0x0507/transport retried (backoff)
    uint32_t    stall_ms            = 180000;     // no progress for this long -> Timeout
    uint32_t    total_ms            = 3600000;    // absolute cap per job
    uint32_t    max_body_mb         = 160;        // PWG body cap (fallback to JPEG above)
    bool        cancel_on_stall     = false;      // false: leave stalled job queued (no data loss)
    bool        wait_for_completion = true;
    std::string prefer;                           // "", "pdf", "pwg", "jpeg"
};

struct printer_caps {
    std::string formats, reasons, pwg_res, pwg_types, media_default;
    uint32_t state = 3;
    bool accepting = true;
    bool fmt(const char* m) const { return formats.find(m) != std::string::npos; }
};

inline result<printer_caps> probe(const std::string& hp, bool tls) {
    using R = result<printer_caps>;
    auto r = get_printer_attrs(hp, tls, {"printer-state", "printer-state-reasons",
        "printer-is-accepting-jobs", "document-format-supported", "media-default",
        "pwg-raster-document-resolution-supported", "pwg-raster-document-type-supported"});
    if (!r) return R::fail(r.st, r.native);
    const response& a = r.value;
    R o;
    o.value.formats       = attr_get(a, "document-format-supported");
    o.value.pwg_res       = attr_get(a, "pwg-raster-document-resolution-supported");
    o.value.pwg_types     = attr_get(a, "pwg-raster-document-type-supported");
    o.value.media_default = attr_get(a, "media-default");
    o.value.reasons       = attr_get(a, "printer-state-reasons", "none");
    o.value.state         = attr_uint(a, "printer-state", 3);
    o.value.accepting     = attr_get(a, "printer-is-accepting-jobs", "true") != "false";
    return o;
}

// Only "-error" reasons block. media-empty-report / marker-supply-low-warning do not.
inline knst_prn::status preflight(const printer_caps& c) {
    last_diag() = c.reasons;
    if (!c.accepting || c.state == 5) return status::BadState;
    if (c.reasons.find("-error") != std::string::npos ||
        c.reasons.find("shutdown") != std::string::npos) return status::BadState;
    return status::Ok;
}

inline uint32_t pick_dpi(const std::string& list) {
    uint32_t best = 0;
    for (auto& t : split(list, ',')) {
        uint32_t x = 0, y = 0; uint8_t u = 3;
        if (!parse_resolution(t, x, y, u) || x != y) continue;
        if (x == 300) return 300;
        if (!best || x < best) best = x;
    }
    return best ? best : 300;
}

inline void begin_print_job(request& req, const std::string& uri, const std::string& name,
                            const char* mime, const print_options& o) {
    req.begin(OP_PRINT_JOB);
    req.add_string(TAG_CHARSET,          "attributes-charset",          "utf-8");
    req.add_string(TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
    req.add_string(TAG_URI,              "printer-uri",                 uri);
    req.add_string(TAG_NAME,             "requesting-user-name",        "knst");
    req.add_string(TAG_NAME,             "job-name",                    name);
    req.add_string(TAG_MIME_TYPE,        "document-format",             mime);
    req.job_attrs();
    if (o.copies > 1) req.add_int(TAG_INTEGER, "copies", o.copies);
    if (const char16_t* m = media_str(o.paper))
        req.add_string(TAG_KEYWORD, "media", to_u8(knst_c16string(m)));
    switch (o.duplex) {
        case duplex_mode::Simplex:   req.add_string(TAG_KEYWORD, "sides", "one-sided");            break;
        case duplex_mode::LongEdge:  req.add_string(TAG_KEYWORD, "sides", "two-sided-long-edge");  break;
        case duplex_mode::ShortEdge: req.add_string(TAG_KEYWORD, "sides", "two-sided-short-edge"); break;
        default: break;
    }
    if (o.color == color_mode::Color)
        req.add_string(TAG_KEYWORD, "print-color-mode", "color");
    else if (o.color == color_mode::Mono || o.color == color_mode::Gray)
        req.add_string(TAG_KEYWORD, "print-color-mode", "monochrome");
    req.end_of_attrs();
}

inline uint32_t find_job_by_name(const std::string& hp, bool tls, const std::string& name) {
    for (bool completed : {false, true}) {
        auto r = get_jobs(hp, tls, completed);
        if (!r) continue;
        std::vector<uint32_t> ids; std::vector<std::string> names;
        for (auto& kv : r.value.attrs) {
            if (kv.first == "job-id")        ids.push_back(static_cast<uint32_t>(std::strtoul(kv.second.c_str(), nullptr, 10)));
            else if (kv.first == "job-name") names.push_back(kv.second);
        }
        for (size_t i = 0; i < ids.size() && i < names.size(); ++i)
            if (names[i] == name) return ids[i];
    }
    return 0;
}

// Print-Job with exponential backoff on busy + duplicate protection after transport errors.
inline result<uint32_t> submit_job(const std::string& hp, bool tls, const std::vector<uint8_t>& body,
                                   const std::string& job_name, const send_policy& pol) {
    using R = result<uint32_t>;
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&]() { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - t0).count()); };
    uint32_t backoff = 500; int transport_fail = 0;
    for (;;) {
        response resp; knst_network_error err = knst_network_error::None;
        const bool sent = send(hp, tls, body, pol.io_idle_ms, resp, err);
        if (sent && ipp_ok(resp.status)) {
            uint32_t id = attr_uint(resp, "job-id", 0);
            if (!id) id = find_job_by_name(hp, tls, job_name);
            return id ? R::success(id) : R::fail(status::BadState);
        }
        if (sent && resp.status != 0x0507) {
            last_diag() = "ipp 0x" + std::to_string(resp.status) + " " + attr_get(resp, "status-message");
            return R::fail(status::IoError, resp.status);
        }
        if (!sent) {
            uint32_t id = find_job_by_name(hp, tls, job_name);
            if (id) return R::success(id);
            if (++transport_fail >= 3) return R::fail(status::IoError, static_cast<int>(err));
        }
        if (elapsed() >= pol.busy_total_ms)
            return R::fail(status::Timeout, sent ? 0x0507 : static_cast<int>(err));
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        backoff = std::min<uint32_t>(backoff * 2, 8000);
    }
}

// Progress-based wait — no fixed attempt count.
inline result<void> wait_job(const std::string& hp, bool tls, uint32_t id, const send_policy& pol) {
    using R = result<void>;
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now(); auto last_change = t0;
    std::string last_snap; int fails = 0;
    auto ms = [](clk::time_point a) { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - a).count()); };
    for (;;) {
        auto r = get_job_attrs(hp, tls, id);
        if (r) {
            fails = 0;
            const uint32_t st = attr_uint(r.value, "job-state", 0);
            const std::string reasons = attr_get(r.value, "job-state-reasons");
            last_diag() = "job " + std::to_string(id) + ": " + reasons;
            if (st == 9) return R::success();
            if (st == 7 || st == 8) return R::fail(status::BadState, static_cast<int>(st));
            std::string snap = std::to_string(st) + "|" + reasons + "|" +
                               attr_get(r.value, "job-impressions-completed") + "|" +
                               attr_get(r.value, "job-media-sheets-completed");
            if (snap != last_snap) { last_snap = snap; last_change = clk::now(); }
        } else if (r.st == status::NotFound) {
            return R::success();
        } else if (++fails > 15) {
            return R::fail(status::IoError, r.native);
        }
        if (ms(last_change) > pol.stall_ms || ms(t0) > pol.total_ms) {
            if (pol.cancel_on_stall) cancel_job_ipp(hp, tls, id);
            return R::fail(status::Timeout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

// PWG Raster (PWG 5102.4). ch = 1 (sgray_8) or 3 (srgb_8). PackBits literal/repeat both supported.
inline void pwg_add_page(std::vector<uint8_t>& o, const uint8_t* px, uint32_t w, uint32_t h, uint32_t ch,
                         uint32_t dpi, uint32_t w_pt, uint32_t h_pt) {
    uint8_t hd[1796]; std::memset(hd, 0, sizeof(hd));
    auto p32 = [&](size_t off, uint32_t v) {
        hd[off] = uint8_t(v >> 24); hd[off + 1] = uint8_t(v >> 16);
        hd[off + 2] = uint8_t(v >> 8); hd[off + 3] = uint8_t(v);
    };
    std::memcpy(hd, "PwgRaster", 9);
    p32(276, dpi); p32(280, dpi);
    p32(340, 1);
    p32(352, w_pt); p32(356, h_pt);
    p32(372, w); p32(376, h);
    p32(384, 8); p32(388, 8 * ch); p32(392, w * ch);
    p32(400, ch == 1 ? 18 : 19);
    p32(420, ch);
    p32(456, 1); p32(460, 1);
    p32(472, w); p32(476, h);
    p32(480, 0xFFFFFF);
    o.insert(o.end(), hd, hd + sizeof(hd));

    const size_t stride = static_cast<size_t>(w) * ch;
    uint32_t y = 0;
    while (y < h) {
        const uint8_t* row = px + static_cast<size_t>(y) * stride;
        uint32_t rep = 1;
        while (y + rep < h && rep < 256 &&
               std::memcmp(row, row + static_cast<size_t>(rep) * stride, stride) == 0) ++rep;
        o.push_back(static_cast<uint8_t>(rep - 1));
        size_t x = 0;
        while (x < w) {
            // Try pixel-run first (>= 2 identical pixels)
            size_t r = 1;
            while (x + r < w && r < 128 &&
                   std::memcmp(row + x * ch, row + (x + r) * ch, ch) == 0) ++r;
            if (r >= 2) {
                // PWG run control: (1 - r) & 0xFF = 0xFF..0x81 for r=2..128
                o.push_back(static_cast<uint8_t>((1 - static_cast<int>(r)) & 0xFF));
                o.insert(o.end(), row + x * ch, row + (x + 1) * ch);
                x += r;
                continue;
            }
            // Literal run (1..128) — control byte = n - 1
            size_t n = 1;
            while (x + n < w && n < 128) {
                if (x + n + 1 < w &&
                    std::memcmp(row + (x + n) * ch, row + (x + n + 1) * ch, ch) == 0) break;
                ++n;
            }
            o.push_back(static_cast<uint8_t>(n - 1));
            o.insert(o.end(), row + x * ch, row + (x + n) * ch);
            x += n;
        }
        y += rep;
    }
}

} // namespace ipp
} // namespace detail

// Public engine — one implementation for Linux/Windows/Android.
result<uint32_t> print_ipp_smart(const knst_c16string& pdf_path, const knst_c16string& host_port,
                                  const print_options& o = print_options(),
                                  const detail::ipp::send_policy& pol = detail::ipp::send_policy(),
                                  std::string* info = nullptr);
#endif // KNST_PRN_HAS_NETWORK
namespace detail {

inline result<void> cache_set(bool enabled, const knst_c16string& dir, uint32_t max_mb) {
    auto& cs = cache();
    std::lock_guard<std::mutex> lk(cs.m);
    cs.cfg.enabled = enabled;
    if (!enabled) return result<void>::success();
    if (dir.empty()) {
        knst_c16string home = knst_file::get_home_dir();
        if (home.empty()) return result<void>::fail(status::NotFound);
        cs.cfg.directory = knst_file::join_path(home, u".cache/knst_printer");
    } else cs.cfg.directory = knst_file::resolve_path(dir);
    cs.cfg.max_size_mb = max_mb ? max_mb : 500;
    knst_file::create_directories(cs.cfg.directory);
    return result<void>::success();
}
inline result<void> cache_clear() {
    auto& cs = cache();
    std::lock_guard<std::mutex> lk(cs.m);
    if (cs.cfg.directory.empty()) return result<void>::fail(status::BadState);
    return knst_file::remove_all(cs.cfg.directory) ? result<void>::success() : result<void>::fail(status::IoError);
}
inline uint64_t cache_size() {
    auto& cs = cache();
    std::lock_guard<std::mutex> lk(cs.m);
    if (cs.cfg.directory.empty()) return 0;
    knst_vector<knst_file> all; uint64_t total = 0;
    if (!knst_file::list_directory_recursive(cs.cfg.directory, all)) return 0;
    for (auto& f : all) if (f.is_file) total += f.size;
    return total;
}

} // namespace detail

#if defined(__linux__) && !defined(__ANDROID__)
// ===========================================================================
//                         Linux helpers (CUPS tools)
// ===========================================================================
namespace detail {

class cmd_output {
    knst_byte_string raw_;
public:
    cmd_output() = default;
    cmd_output(knst_byte_string raw) : raw_(std::move(raw)) {}

    const knst_byte_string& raw() const noexcept { return raw_; }
    const char* c_str() const noexcept { return raw_.empty() ? "" : reinterpret_cast<const char*>(raw_.data()); }
    const char* data() const noexcept { return c_str(); }
    uint32_t length() const noexcept { return raw_.length(); }
    uint32_t size() const noexcept { return raw_.length(); }
    bool empty() const noexcept { return raw_.empty(); }

    std::string str() const { return std::string(c_str(), raw_.length()); }

    size_t find(const char* s, size_t pos = 0) const { return str().find(s, pos); }
    size_t find(char ch, size_t pos = 0) const { return str().find(ch, pos); }
    size_t find(const std::string& s, size_t pos = 0) const { return str().find(s, pos); }
    std::string substr(size_t pos = 0, size_t len = std::string::npos) const { return str().substr(pos, len); }

    const char* begin() const noexcept { return c_str(); }
    const char* end() const noexcept { return c_str() + raw_.length(); }

    operator std::string() const { return str(); }
};

struct tool_out {
    bool       launched = false;
    int        code     = -1;
    cmd_output out;

    // Convenience delegations — old code used t.out, new code uses t.str()
    std::string str() const { return out.str(); }
    const char* c_str() const noexcept { return out.c_str(); }
    size_t find(const char* s, size_t pos = 0) const { return out.find(s, pos); }
    size_t find(char ch, size_t pos = 0) const { return out.find(ch, pos); }
};
class cmd {
public:
    cmd& operator<<(const char* s)            { a_ << s; return *this; }
    cmd& operator<<(const std::string& s)     { a_ << from_u8(s); return *this; }
        cmd& operator<<(const knst_c16string& s)  { a_ << s; return *this; }
    cmd& operator<<(const knst_dev::argv& a)  {
        for (uint32_t i = 0; i < a.v.size(); ++i) a_ << a.v[i];
        return *this;
    }
    tool_out run(const char* tool, uint32_t timeout_ms) const {
        auto t = knst_dev::run_tool_raw(tool, a_, timeout_ms);
        tool_out r;
        r.launched = t.launched;
        r.code     = t.exit_code;
        r.out      = cmd_output(std::move(t.out));
        return r;
    }
private:
    knst_dev::argv a_;
};

inline std::string printer_uri(const knst_c16string& name) { return "ipp://localhost/printers/" + to_u8(name); }

// Runs ipptool (cups-ipp-utils).  Output attributes are returned as name -> value.
inline bool ipp_query(const char* operation, const std::string& uri,
                      const std::vector<std::pair<std::string, std::string>>& defs, kvlist& out) {
    temp_set tmp;
    knst_c16string tf = tmp.add(u".test");
    std::string body = std::string("{\n\tNAME \"knst\"\n\tOPERATION ") + operation +
        "\n\tGROUP operation-attributes-tag\n\tATTR charset attributes-charset utf-8\n"
        "\tATTR language attributes-natural-language en\n\tATTR uri printer-uri $uri\n";
    for (auto& d : defs) if (d.first == "jobid") body += "\tATTR integer job-id $jobid\n";
    body += "\tATTR keyword requested-attributes all\n}\n";
    if (!knst_file::write_file_data(tf, reinterpret_cast<const unsigned char*>(body.data()),
                                    static_cast<uint32_t>(body.size()))) return false;
    cmd c; c << "-tv";
    for (auto& d : defs) { c << "-d"; c << (d.first + "=" + d.second); }
    c << uri << tf;
    tool_out t = c.run("ipptool", 12000);
    if (!t.launched || t.out.empty()) return false;
    bool any = false;
    for (auto& line : split_lines(t.out)) {
        std::string s = trim(line);
        size_t a = s.find(" ("), b = s.find(") = ");
        if (a == std::string::npos || b == std::string::npos || b < a) continue;
        std::string name = s.substr(0, a);
        if (name.find(' ') != std::string::npos) continue;
        out.emplace_back(name, s.substr(b + 4)); any = true;
    }
    return any;
}

// Returns true if `reason` appears in `rs` with error severity or bare
// (no suffix). RFC 8011 §5.4.12: `-report` and `-warning` are informational;
// only `-error` (or the bare form, treated as -report by spec) blocks a job.
inline bool has_blocking_reason(const std::string& rs, const char* reason) {
    std::string r(reason);
    size_t pos = 0;
    while ((pos = rs.find(r, pos)) != std::string::npos) {
        // Must be at start of string or preceded by comma
        bool at_start = (pos == 0) || (rs[pos - 1] == ',');
        if (!at_start) { pos += r.size(); continue; }
        size_t end = pos + r.size();
        if (end == rs.size()) return true;              // bare, end
        if (rs[end] == ',')   return true;              // bare, next reason
        if (rs.compare(end, 6, "-error") == 0)   return true;
        // -report, -warning, or anything else → not blocking
        pos = end;
    }
    return false;
}

inline printer_state state_from_attrs(const kvlist& kv) {
    long ps = to_long(kv_get(kv, "printer-state", "3"), 3);
    std::string rs = lower(kv_get(kv, "printer-state-reasons"));
    auto has = [&](const char* k) { return rs.find(k) != std::string::npos; };

    // ── Blocking reasons (only -error severity) ──
    if (has_blocking_reason(rs, "media-jam"))                                      return printer_state::PaperJam;
    if (has_blocking_reason(rs, "media-empty") ||
        has_blocking_reason(rs, "media-needed"))                                   return printer_state::PaperOut;
    if (has_blocking_reason(rs, "door-open") ||
        has_blocking_reason(rs, "cover-open"))                                     return printer_state::DoorOpen;
    if (has_blocking_reason(rs, "offline") ||
        has_blocking_reason(rs, "printer-unreachable") ||
        has_blocking_reason(rs, "connecting-to-device"))                           return printer_state::Offline;
    if (has("-error"))                                                             return printer_state::Error;

    // ── Non-blocking state reasons ──
    if (ps == 5 || has("paused") || has("stopped"))                                return printer_state::Paused;
    if (has("warming"))                                                            return printer_state::WarmingUp;
    if (ps == 4)                                                                   return printer_state::Busy;
    // marker-supply-*-warning / -report are informational → TonerLow, not blocking
    if (has("toner-low") || has("toner-empty") || has("marker-supply-low"))        return printer_state::TonerLow;

    return ps == 3 ? printer_state::Ready : printer_state::Unknown;
}

inline job_state ipp_job_state(const std::string& s0) {
    std::string s = lower(s0);
    if (s.find("pending-held") != std::string::npos) return job_state::Held;
    if (s.find("processing-stopped") != std::string::npos) return job_state::Stopped;
    if (s.find("pending") != std::string::npos) return job_state::Pending;
    if (s.find("processing") != std::string::npos) return job_state::Processing;
    if (s.find("canceled") != std::string::npos) return job_state::Canceled;
    if (s.find("aborted") != std::string::npos) return job_state::Aborted;
    if (s.find("completed") != std::string::npos) return job_state::Completed;
    return job_state::Unknown;
}

inline uint32_t parse_job_id(const std::string& out, const std::string& pname) {
    std::string key = pname + "-"; size_t pos = 0;
    while ((pos = out.find(key, pos)) != std::string::npos) {
        size_t d = pos + key.size();
        if (d < out.size() && std::isdigit(static_cast<unsigned char>(out[d])))
            return static_cast<uint32_t>(std::strtoul(out.c_str() + d, nullptr, 10));
        pos = d;
    }
    return 0;
}

inline std::vector<std::string> first_tokens(const std::string& line, size_t n) {
    std::vector<std::string> t; size_t i = 0;
    while (i < line.size() && t.size() < n) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        size_t j = i;
        while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
        if (j > i) t.push_back(line.substr(i, j - i));
        i = j;
    }
    return t;
}

inline void parse_job_lines(const std::string& out, job_state st, std::vector<job_info>& jobs) {
    for (auto& line : split_lines(out)) {
        if (line.empty() || std::isspace(static_cast<unsigned char>(line[0]))) continue;
        auto t = first_tokens(line, 3);
        if (t.size() < 2) continue;
        size_t dash = t[0].rfind('-');
        if (dash == std::string::npos || dash + 1 >= t[0].size()) continue;
        job_info j;
        j.printer = from_u8(t[0].substr(0, dash));
        j.id = static_cast<uint32_t>(std::strtoul(t[0].c_str() + dash + 1, nullptr, 10));
        j.user = from_u8(t[1]);
        if (t.size() > 2) j.size_bytes = std::strtoull(t[2].c_str(), nullptr, 10);
        j.state = st;
        if (j.id) jobs.push_back(std::move(j));
    }
}

inline supply_kind supply_kind_of(const std::string& t0) {
    std::string t = lower(t0);
    if (t.find("waste-toner") != std::string::npos) return supply_kind::WasteToner;
    if (t.find("waste-ink") != std::string::npos) return supply_kind::WasteInk;
    if (t.find("toner") != std::string::npos) return supply_kind::Toner;
    if (t.find("ink") != std::string::npos) return supply_kind::Ink;
    if (t.find("opc") != std::string::npos || t.find("drum") != std::string::npos) return supply_kind::Drum;
    if (t.find("fuser") != std::string::npos) return supply_kind::Fuser;
    if (t.find("ribbon") != std::string::npos) return supply_kind::Ribbon;
    return t.empty() ? supply_kind::Unknown : supply_kind::Other;
}

} // namespace detail

// ===========================================================================
//                            Printer class - Linux/CUPS
// ===========================================================================
class printer {
public:
    // ---- opening ----------------------------------------------------------
    static result<printer> open(const knst_c16string& name) {
        using R = result<printer>;
        if (name.empty() || !knst_dev::is_safe_token(name)) return R::fail(status::InvalidArgument);
        auto e = exists(name);
        if (!e) return R::fail(e.st, e.native);
        if (!e.value) return R::fail(status::NotFound);
        printer p; p.name_ = name;
        return R::success(std::move(p));
    }
    static result<printer> open_default() {
        auto n = get_default_name();
        if (!n) return result<printer>::fail(n.st, n.native);
        return open(n.value);
    }
    // default printer if there is one, otherwise the first enabled printer
    static result<printer> open_any() {
        auto d = open_default();
        if (d) return d;
        auto all = list_all();
        if (!all) return result<printer>::fail(all.st, all.native);
        for (auto& p : all.value) if (p.is_enabled && p.is_accepting) return open(p.name);
        if (!all.value.empty()) return open(all.value[0].name);
        return result<printer>::fail(status::NotFound);
    }

    const knst_c16string& name() const noexcept { return name_; }

    // ---- information ------------------------------------------------------
    result<queue_status> status() const {
        using R = result<queue_status>;
        detail::kvlist kv = dest_attrs();
        if (kv.empty()) return R::fail(status::NotFound);
        R r;
        r.value.state     = detail::state_from_attrs(kv);
        r.value.enabled   = detail::to_long(detail::kv_get(kv, "printer-state", "3"), 3) != 5 &&
                            detail::lower(detail::kv_get(kv, "printer-state-reasons")).find("paused") == std::string::npos;
        r.value.accepting = detail::lower(detail::kv_get(kv, "printer-is-accepting-jobs", "true")) != "false";
        std::string reasons = detail::kv_get(kv, "printer-state-reasons");
        if (reasons != "none") r.value.text = detail::from_u8(reasons);
        auto j = jobs(false);
        if (j) r.value.job_count = static_cast<uint32_t>(j.value.size());
        return r;
    }

    result<printer_info_ex> info() const {
        using R = result<printer_info_ex>;
        detail::kvlist kv = dest_attrs();
        if (kv.empty()) return R::fail(status::NotFound);
        R r; r.value = info_from_attrs(name_, kv);
        if (r.value.device_uri.empty()) r.value.device_uri = device_uri_fallback(name_);
        auto d = get_default_name();
        r.value.is_default = d && d.value == name_;
        auto j = jobs(false);
        if (j) r.value.pending_jobs = static_cast<uint32_t>(j.value.size());
        return r;
    }

    result<knst_c16string> make_and_model() const {
        using R = result<knst_c16string>;
        auto i = info();
        if (!i) return R::fail(i.st, i.native);
        if (i.value.make_and_model.empty()) return R::fail(status::NotFound);
        return R::success(i.value.make_and_model);
    }

    result<printer_capabilities> capabilities() const {
        using R = result<printer_capabilities>;
        auto o = options();
        if (!o) return R::fail(o.st, o.native);
        R r;
        for (auto& po : o.value) {
            std::string key = detail::to_u8(po.name);
            for (uint32_t i = 0; i < po.choices.size(); ++i) {
                std::string c = detail::to_u8(po.choices[i]), lc = detail::lower(c);
                if (key == "Duplex" || key == "sides") {
                    if (lc.find("notumble") != std::string::npos || lc == "two-sided-long-edge") r.value.duplex_long_edge = true;
                    if (lc == "duplextumble" || lc == "two-sided-short-edge") r.value.duplex_short_edge = true;
                } else if (key == "ColorModel" || key == "print-color-mode" || key == "ColorMode") {
                    if (lc.find("rgb") != std::string::npos || lc.find("cmyk") != std::string::npos ||
                        lc.find("color") != std::string::npos) r.value.color = true;
                } else if (key.find("Staple") != std::string::npos || key == "finishings") {
                    if (lc != "none" && lc != "false" &&
                        (key.find("Staple") != std::string::npos || lc.find("staple") != std::string::npos)) r.value.stapling = true;
                } else if (key.find("Punch") != std::string::npos) {
                    if (lc != "none" && lc != "false") r.value.punching = true;
                } else if (key.find("Booklet") != std::string::npos || key.find("Fold") != std::string::npos) {
                    if (lc != "none" && lc != "false") r.value.booklet = true;
                } else if (key == "PageSize")     r.value.supported_media.push_back(po.choices[i]);
                else if (key == "Resolution")     r.value.supported_resolutions.push_back(po.choices[i]);
                else if (key == "InputSlot")      r.value.supported_sources.push_back(po.choices[i]);
                else if (key == "OutputBin")      r.value.supported_output_bins.push_back(po.choices[i]);
            }
        }
        return r;
    }

    result<bool> supports_duplex(duplex_mode m) const {
        auto c = capabilities();
        if (!c) return result<bool>::fail(c.st, c.native);
        switch (m) {
            case duplex_mode::LongEdge:  return result<bool>::success(c.value.duplex_long_edge);
            case duplex_mode::ShortEdge: return result<bool>::success(c.value.duplex_short_edge);
            default:                     return result<bool>::success(true);
        }
    }
    result<bool> supports_media(media_size m) const {
        auto c = capabilities();
        if (!c) return result<bool>::fail(c.st, c.native);
        if (m == media_size::Default) return result<bool>::success(true);
        for (auto& s : c.value.supported_media) {
            std::string n = detail::lower(detail::to_u8(s));
            size_t dot = n.find('.');
            if (dot != std::string::npos) n = n.substr(0, dot);          // "a4.borderless"
            if (detail::paper_from_name(n) == m && n.find("custom") == std::string::npos) return result<bool>::success(true);
        }
        return result<bool>::success(false);
    }

    result<knst_vector<supply_level>> supply_levels() const {
        using R = result<knst_vector<supply_level>>;
        detail::kvlist kv = dest_attrs();

#if KNST_PRN_HAS_NETWORK
        // Fast path: query directly via IPP (no ipptool shell-out)
        if (!detail::kv_find(kv, "marker-levels")) {
            auto resp = detail::ipp::get_printer_attrs(
                "localhost:631", false,
                {"marker-levels", "marker-names", "marker-types", "marker-low-levels"});
            if (resp) {
                std::string lv = detail::ipp::attr_get(resp.value, "marker-levels");
                if (!lv.empty()) {
                    kv.emplace_back("marker-levels", lv);
                    kv.emplace_back("marker-names",  detail::ipp::attr_get(resp.value, "marker-names"));
                    kv.emplace_back("marker-types",  detail::ipp::attr_get(resp.value, "marker-types"));
                    kv.emplace_back("marker-low-levels", detail::ipp::attr_get(resp.value, "marker-low-levels"));
                }
            }
        }
#endif
        // Fallback: ipptool
        if (!detail::kv_find(kv, "marker-levels")) {
            detail::kvlist iq;
            if (detail::ipp_query("Get-Printer-Attributes", detail::printer_uri(name_), {}, iq) &&
                detail::kv_find(iq, "marker-levels")) kv = iq;
        }
        R r;
        std::vector<std::string> lv = detail::split(detail::kv_get(kv, "marker-levels"), ',');
        if (lv.size() == 1 && lv[0].empty()) return r;
        std::vector<std::string> nm = detail::split(detail::kv_get(kv, "marker-names"), ',');
        std::vector<std::string> ty = detail::split(detail::kv_get(kv, "marker-types"), ',');
        std::vector<std::string> lo = detail::split(detail::kv_get(kv, "marker-low-levels"), ',');
        for (size_t i = 0; i < lv.size(); ++i) {
            supply_level s;
            long p = detail::to_long(lv[i], -1);
            s.known = p >= 0 && p <= 100;
            s.percent = s.known ? static_cast<uint32_t>(p) : 0;
            if (i < nm.size()) s.name = detail::from_u8(detail::trim(nm[i]));
            if (i < ty.size()) { s.raw_type = detail::from_u8(ty[i]); s.type = detail::supply_kind_of(ty[i]); }
            long low = (i < lo.size()) ? detail::to_long(lo[i], -1) : -1;
            s.low = s.known && (low >= 0 ? static_cast<long>(s.percent) <= low : s.percent < 15);
            if (s.type == supply_kind::WasteToner || s.type == supply_kind::WasteInk) s.low = false;
            r.value.push_back(std::move(s));
        }
        return r;
    }

    result<knst_vector<printer_alert>> alerts() const {
        using R = result<knst_vector<printer_alert>>;
        detail::kvlist kv = dest_attrs();
        if (kv.empty()) return R::fail(status::NotFound);
        R r;
        for (auto& rs : detail::split(detail::kv_get(kv, "printer-state-reasons"), ',')) {
            std::string s = detail::trim(rs);
            if (s.empty() || s == "none") continue;
            printer_alert a; a.code = detail::from_u8(s); a.text = a.code;
            a.severity = (s.size() > 6 && s.compare(s.size() - 6, 6, "-error") == 0) ? 2 :
                         (s.size() > 8 && s.compare(s.size() - 8, 8, "-warning") == 0) ? 1 : 0;
            r.value.push_back(std::move(a));
        }
        std::string msg = detail::kv_get(kv, "marker-message");
        if (!msg.empty()) { printer_alert a; a.code = u"marker-message"; a.text = detail::from_u8(msg); r.value.push_back(a); }
        return r;
    }

    result<usage_stats> usage() const {
        result<usage_stats> r;
        auto p = jobs(false);
        if (p) for (auto& j : p.value) { r.value.pending_jobs++; r.value.total_jobs++; r.value.total_bytes += j.size_bytes; }
        auto d = completed_jobs(100000);
        if (d) for (auto& j : d.value) { r.value.done_jobs++; r.value.total_jobs++; r.value.total_bytes += j.size_bytes; r.value.total_pages += j.pages; }
        return r;
    }

    // ---- printing ---------------------------------------------------------
    // Raw bytes.  Formats CUPS recognises (PDF, PS, JPEG, PNG, GIF, BMP, PNM, TIFF) are sent normally,
    // anything else is sent with "-o raw" (printer languages such as PCL/ZPL/ESC-POS).
    result<uint32_t> print_data(const unsigned char* d, uint32_t len, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (!d || len == 0 || !detail::valid_options(o)) return R::fail(status::InvalidArgument);
        auto is = [&](const char* m, uint32_t n) { return len >= n && std::memcmp(d, m, n) == 0; };
        bool known = is("%PDF-", 5) || is("%!", 2) || is("\xFF\xD8\xFF", 3) || is("\x89PNG", 4) || is("GIF8", 4) ||
                     is("BM", 2) || is("P6", 2) || is("P5", 2) || is("II*\0", 4) || is("MM\0*", 4) ||
                     (len >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0);
        detail::temp_set tmp; knst_c16string t = tmp.add(u".bin");
        if (!knst_file::write_file_atomic(t, d, len)) return R::fail(status::IoError);
        return submit({t}, o, false, !known);
    }

    result<uint32_t> print_text(const knst_c16string& text, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (text.empty() || !detail::valid_options(o)) return R::fail(status::InvalidArgument);
        float w, h; page_dims(o, w, h);
        detail::document doc; detail::layout_plain_text(text, w, h, doc);
        return print_document(doc, o);
    }

    result<uint32_t> print_rich(const rich_document& rd, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if ((rd.elements.empty() && rd.shapes.empty()) || !detail::valid_options(o)) return R::fail(status::InvalidArgument);
        detail::document doc; detail::layout_rich(rd, doc);
        return print_document(doc, o);
    }

    // One picture (fit on the page).
    result<uint32_t> print_image(const knst_c16string& path, const print_options& o = print_options()) {
        image_batch_options bo;
        float m = static_cast<float>(std::max(std::max(o.margin_left_mm, o.margin_right_mm),
                                              std::max(o.margin_top_mm, o.margin_bottom_mm)));
        bo.margin_mm = m > 0 ? m : (o.borderless ? 0.0f : 5.0f);
        bo.fit = o.borderless ? fit_mode::Fill : fit_mode::Fit;
        std::vector<knst_c16string> one; one.push_back(path);
        return print_images(one, bo, o);
    }

    // MANY pictures -> ONE job.  The printer prints them back to back, nothing is interleaved.
    // rows x cols pictures per page, EXIF rotation, fit/fill/stretch, downscale to max_dpi.
    result<uint32_t> print_images(const knst_vector<knst_c16string>& paths,
                                  const image_batch_options& bo = image_batch_options(),
                                  const print_options& o = print_options()) {
        std::vector<knst_c16string> v;
        for (uint32_t i = 0; i < paths.size(); ++i) v.push_back(paths[i]);
        return print_images(v, bo, o);
    }
    result<uint32_t> print_images(const std::vector<knst_c16string>& paths, const image_batch_options& bo,
                                  const print_options& o) {
        using R = result<uint32_t>;
        if (paths.empty() || !detail::valid_options(o)) return R::fail(status::InvalidArgument);
        detail::temp_set tmp;
        knst_c16string pdf;
        knst_prn::status st = compose_images(paths, bo, o, tmp, pdf);
        if (st != status::Ok) return R::fail(st);
        return submit({pdf}, o, true);
    }

    // Mixed list (photos, text, PDF, anything CUPS knows) -> ONE job, order preserved.
    // Consecutive photos are laid out by `bo`; PDFs and other files are passed through unchanged.
    result<uint32_t> print_batch(const knst_vector<knst_c16string>& paths, const print_options& o = print_options(),
                                 const image_batch_options& bo = image_batch_options()) {
        using R = result<uint32_t>;
        if (paths.empty() || !detail::valid_options(o)) return R::fail(status::InvalidArgument);
        detail::temp_set tmp;
        std::vector<knst_c16string> files, run;
        bool passthrough = false;
        auto flush = [&]() -> knst_prn::status {
            if (run.empty()) return status::Ok;
            knst_c16string pdf;
            knst_prn::status st = compose_images(run, bo, o, tmp, pdf);
            run.clear();
            if (st == status::Ok) files.push_back(pdf);
            return st;
        };
        for (uint32_t i = 0; i < paths.size(); ++i) {
            const knst_c16string& p = paths[i];
            detail::file_kind k = detail::detect_kind(p);
            if (k == detail::file_kind::Image) { run.push_back(p); continue; }
            knst_prn::status st = flush();
            if (st != status::Ok) return R::fail(st);
            knst_c16string abs = knst_file::resolve_path(p);
            if (!knst_file::file_exists(abs)) { if (bo.skip_unreadable) { if (bo.skipped) bo.skipped->push_back(p); continue; } return R::fail(status::NotFound); }
            if (k == detail::file_kind::Text) {
                float w, h; page_dims(o, w, h);
                detail::document doc; detail::layout_plain_text(knst_file::read_file_text(abs), w, h, doc);
                knst_c16string pdf = tmp.add(u".pdf");
                st = detail::write_pdf(doc, pdf);
                if (st != status::Ok) return R::fail(st);
                files.push_back(pdf);
            } else { files.push_back(abs); passthrough = true; }
        }
        knst_prn::status st = flush();
        if (st != status::Ok) return R::fail(st);
        if (files.empty()) return R::fail(status::NotFound);
        // Return value: generated = !passthrough.
        //   • All generated (images/text) → generated=true → CUPS skips
        //     orientation-requested (PDF already has correct orientation).
        //   • All pass-through (PDFs)     → generated=false → CUPS applies
        //     orientation-requested (PDFs get rotated per user request).
        //   • Mixed batch                 → generated=false → CUPS sees the
        //     orientation option. Modern pdftopdf checks the existing page
        //     orientation first, so generated PDFs stay correct. Older CUPS
        //     may double-rotate the generated PDF — for those, use print_file()
        //     per-PDF with explicit orient, or use print_batch with orient=Default.
        return submit(files, o, !passthrough);
    }

    result<uint32_t> print_pdf(const knst_c16string& path, const print_options& o = print_options()) {
        if (path.empty()) return result<uint32_t>::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return result<uint32_t>::fail(status::NotFound);
        unsigned char m[5];
        if (!detail::read_magic(abs, m, 5)) return result<uint32_t>::fail(status::IoError);
        if (std::memcmp(m, "%PDF-", 5) != 0) return result<uint32_t>::fail(status::InvalidArgument);
        return print_file(abs, o);
    }

    // Sends the file as it is (CUPS filters decide).
    result<uint32_t> print_file(const knst_c16string& path, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty() || !detail::valid_options(o)) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        return submit({abs}, o, false);
    }

    result<uint32_t> print_auto(const knst_c16string& path, const print_options& o = print_options()) {
        switch (detail::detect_kind(path)) {
            case detail::file_kind::Text: {
                knst_c16string abs = knst_file::resolve_path(path);
                if (!knst_file::file_exists(abs)) return result<uint32_t>::fail(status::NotFound);
                return print_text(knst_file::read_file_text(abs), o);
            }
            case detail::file_kind::Image: return print_image(path, o);
            case detail::file_kind::Pdf:   return print_pdf(path, o);
            default:                       return print_file(path, o);
        }
    }

        result<uint32_t> send_raw(const knst_byte_string& data, const knst_c16string& title = u"knst raw job") {
        using R = result<uint32_t>;
        if (data.empty()) return R::fail(status::InvalidArgument);
        detail::temp_set tmp; knst_c16string t = tmp.add(u".prn");
        if (!knst_file::write_file_atomic(t, data.data(), data.length())) return R::fail(status::IoError);

        // GCC 13 false positive: "may be used uninitialized" on knst_c16string
        // copy-assign through print_options. The code is correct — the union
        // members ARE initialized by the default ctor. This is a known bug
        // in GCC 13's -Wmaybe-uninitialized analysis with SSO unions.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        print_options o;
        o.title = knst_c16string(title.data(), title.length());
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

        return submit({t}, o, false, true);
    }
    result<uint32_t> send_escpos(const knst_byte_string& d) { return send_raw(d, u"knst ESC/POS"); }
    result<uint32_t> send_zpl(const knst_byte_string& d)    { return send_raw(d, u"knst ZPL"); }
    result<uint32_t> send_pcl(const knst_byte_string& d)    { return send_raw(d, u"knst PCL"); }

    // ---- Network raw (TCP 9100) — ESC/POS, PCL, ZPL over network ----------
    // Bypasses CUPS entirely. For thermal / label printers not installed in CUPS.
    result<uint32_t> print_network(const knst_byte_string& data, const knst_c16string& host,
                                   uint16_t port = 9100, uint32_t timeout_ms = 5000) {
        using R = result<uint32_t>;
        if (data.empty() || host.empty()) return R::fail(status::InvalidArgument);
        if (timeout_ms < 100) timeout_ms = 100;

        std::string host_str = detail::to_u8(host);
        if (host_str.empty()) return R::fail(status::InvalidArgument);

        struct addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        std::string port_str = std::to_string(port ? port : 9100);
        struct addrinfo* res = nullptr;
        if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res) != 0 || !res)
            return R::fail(status::NotFound);

        int sock_fd = -1;
        for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
            int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK, ai->ai_protocol);
            if (fd < 0) continue;
            int rc = ::connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
            if (rc < 0 && errno != EINPROGRESS) { ::close(fd); continue; }
            struct pollfd pfd{}; pfd.fd = fd; pfd.events = POLLOUT;
            int pr = ::poll(&pfd, 1, static_cast<int>(timeout_ms));
            if (pr <= 0) { ::close(fd); continue; }
            int soerr = 0; socklen_t solen = sizeof(soerr);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &solen) != 0 || soerr != 0) {
                ::close(fd); continue;
            }
            int flags = ::fcntl(fd, F_GETFL, 0);
            ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            struct timeval tv{};
            tv.tv_sec  = static_cast<time_t>(timeout_ms / 1000);
            tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            sock_fd = fd;
            break;
        }
        ::freeaddrinfo(res);
        if (sock_fd < 0) return R::fail(status::IoError);

        const uint8_t* p = data.data();
        size_t left = data.length();
        bool ok = true;
        while (left > 0) {
            ssize_t n = ::send(sock_fd, p, left, MSG_NOSIGNAL);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                ok = false;
                break;
            }
            p += n;
            left -= static_cast<size_t>(n);
        }
        ::close(sock_fd);
        return ok ? R::success(0) : R::fail(status::IoError);
    }

    // Needs a printer that supports PIN / secure print (job-password).
    result<uint32_t> print_secure(const knst_c16string& path, const secure_print_options& s,
                                  const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty() || s.job_password.length() == 0 || s.job_password.length() > 32 ||
            !detail::safe_value(s.job_password) || !detail::valid_options(o)) return R::fail(status::InvalidArgument);
        if (!s.job_user.empty() && !detail::safe_value(s.job_user)) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);

        knst_vector<knst_c16string> extra;
        extra.push_back(u"job-password-encryption=none");
        knst_c16string pwd_kv = u"job-password=";
        pwd_kv.append(s.job_password);
        extra.push_back(std::move(pwd_kv));
        if (!s.job_user.empty()) {
            knst_c16string user_kv = u"job-originating-user-name=";
            user_kv.append(s.job_user);
            extra.push_back(std::move(user_kv));
        }
        return submit({abs}, o, false, false, extra);
    }

    // hold_until: "indefinite", "day-time", "night", "weekend" or "HH:MM:SS"
    result<uint32_t> print_held(const knst_c16string& path, const knst_c16string& hold_until,
                                const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty() || !detail::safe_value(hold_until) || !detail::valid_options(o)) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);

        knst_vector<knst_c16string> extra;
        knst_c16string hv = u"job-hold-until=";
        hv.append(hold_until);
        extra.push_back(std::move(hv));
        return submit({abs}, o, false, false, extra);
    }

    result<uint32_t> print_test_page(test_page_type type = test_page_type::Diagnostic,
                                     const print_options& o = print_options()) {
        if (!detail::valid_options(o)) return result<uint32_t>::fail(status::InvalidArgument);
        float w, h; page_dims(o, w, h);
        detail::document doc; detail::make_test_doc(type, detail::to_u8(name_), w, h, doc);
        print_options oo = o; if (oo.title.empty()) oo.title = u"knst test page";
        oo.copies = 1;
        return print_document(doc, oo);
    }

    // One job per file (use print_batch for a single continuous job).
    result<knst_vector<uint32_t>> print_files(const knst_vector<knst_c16string>& paths,
                                              const print_options& o = print_options(), bool continue_on_error = false) {
        using R = result<knst_vector<uint32_t>>;
        R r;
        for (uint32_t i = 0; i < paths.size(); ++i) {
            auto one = print_auto(paths[i], o);
            if (one) { r.value.push_back(one.value); continue; }
            if (!continue_on_error) { if (r.value.empty()) return R::fail(one.st, one.native); break; }
        }
        if (r.value.empty() && paths.size() > 0) return R::fail(status::IoError);
        return r;
    }

    static result<knst_vector<uint32_t>> print_to_multiple(const knst_vector<knst_c16string>& names,
                                                           const knst_c16string& path,
                                                           const print_options& o = print_options()) {
        using R = result<knst_vector<uint32_t>>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        R r; knst_prn::status last = status::NotFound; int nat = 0;
        for (uint32_t i = 0; i < names.size(); ++i) {
            auto pr = open(names[i]);
            if (!pr) { last = pr.st; nat = pr.native; continue; }
            auto one = pr.value.print_auto(path, o);
            if (one) r.value.push_back(one.value); else { last = one.st; nat = one.native; }
        }
        if (r.value.empty()) return R::fail(last, nat);
        return r;
    }

    // ---- jobs -------------------------------------------------------------
    result<knst_vector<job_info>> jobs(bool include_completed = false) const {
        using R = result<knst_vector<job_info>>;
        detail::cmd c; c << "-W" << (include_completed ? "all" : "not-completed") << "-o" << name_;
        auto t = c.run("lpstat", 6000);
        if (!t.launched) return R::fail(status::ToolMissing);
        std::vector<job_info> v;
        detail::parse_job_lines(t.out, include_completed ? job_state::Completed : job_state::Pending, v);
        R r; for (auto& j : v) r.value.push_back(std::move(j));
        return r;
    }

    result<job_info> job_details(uint32_t id) const {
        using R = result<job_info>;
        if (id == 0) return R::fail(status::InvalidArgument);

#if KNST_PRN_HAS_NETWORK
        // Fast path: query directly via IPP
        {
            auto resp = detail::ipp::get_job_attrs("localhost:631", false, id);
            if (resp) {
                R out;
                out.value.id = id;
                out.value.printer = name_;
                out.value.title = detail::from_u8(detail::ipp::attr_get(resp.value, "job-name"));
                out.value.user  = detail::from_u8(detail::ipp::attr_get(resp.value, "job-originating-user-name"));
                out.value.state_text = detail::from_u8(detail::ipp::attr_get(resp.value, "job-state"));
                out.value.state = detail::ipp::job_state_from_uint(
                    (uint32_t)std::strtoul(detail::ipp::attr_get(resp.value, "job-state", "0").c_str(), nullptr, 10));
                out.value.message = detail::from_u8(detail::ipp::attr_get(resp.value, "job-state-reasons"));
                out.value.size_bytes = (uint64_t)std::strtoull(
                    detail::ipp::attr_get(resp.value, "job-k-octets", "0").c_str(), nullptr, 10) * 1024ull;
                out.value.pages = (uint32_t)std::strtoul(
                    detail::ipp::attr_get(resp.value, "job-media-sheets-completed", "0").c_str(), nullptr, 10);
                out.value.submitted_unix = (uint64_t)std::strtoull(
                    detail::ipp::attr_get(resp.value, "time-at-creation", "0").c_str(), nullptr, 10);
                out.value.priority = (uint32_t)std::strtoul(
                    detail::ipp::attr_get(resp.value, "job-priority", "50").c_str(), nullptr, 10);
                return out;
            } else if (resp.st == status::NotFound) {
                return R::fail(status::NotFound);
            }
        }
#endif
        // Fallback: ipptool
        detail::kvlist kv;
        if (detail::ipp_query("Get-Job-Attributes", detail::printer_uri(name_), {{"jobid", std::to_string(id)}}, kv)) {
            // ipptool returns success with empty attributes for bogus job IDs.
            // Require at least one real job attribute to consider it found.
            if (!detail::kv_find(kv, "job-name") && !detail::kv_find(kv, "job-state"))
                return R::fail(status::NotFound);
            R r; r.value.id = id; r.value.printer = name_;
            r.value.title = detail::from_u8(detail::kv_get(kv, "job-name"));
            r.value.user  = detail::from_u8(detail::kv_get(kv, "job-originating-user-name"));
            r.value.state_text = detail::from_u8(detail::kv_get(kv, "job-state"));
            r.value.state = detail::ipp_job_state(detail::kv_get(kv, "job-state"));
            r.value.message = detail::from_u8(detail::kv_get(kv, "job-state-reasons"));
            r.value.size_bytes = static_cast<uint64_t>(detail::to_long(detail::kv_get(kv, "job-k-octets"), 0)) * 1024ull;
            r.value.pages = static_cast<uint32_t>(detail::to_long(detail::kv_get(kv, "job-media-sheets-completed"), 0));
            r.value.submitted_unix = static_cast<uint64_t>(detail::to_long(detail::kv_get(kv, "time-at-creation"), 0));
            r.value.priority = static_cast<uint32_t>(detail::to_long(detail::kv_get(kv, "job-priority"), 50));
            return r;
        }
        auto all = jobs(true);                    // fallback without ipptool
        if (!all) return R::fail(all.st, all.native);
        for (auto& j : all.value) if (j.id == id) return R::success(j);
        return R::fail(status::NotFound);
    }

    result<knst_vector<job_info>> jobs_by_user(const knst_c16string& user, bool incl = false) const {
        using R = result<knst_vector<job_info>>;
        auto all = jobs(incl);
        if (!all) return R::fail(all.st, all.native);
        R r; for (auto& j : all.value) if (j.user == user) r.value.push_back(j);
        return r;
    }

    result<knst_vector<job_info>> completed_jobs(uint32_t limit = 20) const {
        using R = result<knst_vector<job_info>>;
        detail::cmd c; c << "-W" << "completed" << "-o" << name_;
        auto t = c.run("lpstat", 6000);
        if (!t.launched) return R::fail(status::ToolMissing);
        std::vector<job_info> v;
        detail::parse_job_lines(t.out, job_state::Completed, v);
        R r;
        size_t from = v.size() > limit ? v.size() - limit : 0;
        for (size_t i = from; i < v.size(); ++i) r.value.push_back(v[i]);
        return r;
    }

    result<void> cancel_job(uint32_t id) {
        if (id == 0) return result<void>::fail(status::InvalidArgument);
#if KNST_PRN_HAS_NETWORK
        // Fast path: Cancel-Job over IPP
        {
            auto r = detail::ipp::cancel_job_ipp("localhost:631", false, id);
            if (r) return r;
            if (r.st == status::BadState) return r;      // already finished
        }
#endif
        // Fallback: `cancel` CLI
        detail::cmd c; c << (detail::to_u8(name_) + "-" + std::to_string(id));
        return run_admin("cancel", c);
    }
    result<void> cancel_all() { detail::cmd c; c << "-a" << name_; return run_admin("cancel", c); }
    result<void> cancel_by_user(const knst_c16string& user) {
        if (user.empty() || !knst_dev::is_safe_token(user)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << "-u" << user << name_;
        return run_admin("cancel", c);
    }
    result<void> hold_job(uint32_t id) {
        if (id == 0) return result<void>::fail(status::InvalidArgument);
#if KNST_PRN_HAS_NETWORK
        {
            auto r = detail::ipp::set_job_attrs("localhost:631", false, id, {{"job-state", 4}});
            if (r) return r;
            if (r.st != status::NotSupported) return r;   // real error → propagate
            // NotSupported → fall through to CLI
        }
#endif
        return job_hold(id, "hold");
    }
    result<void> release_job(uint32_t id) {
        if (id == 0) return result<void>::fail(status::InvalidArgument);
#if KNST_PRN_HAS_NETWORK
        {
            auto r = detail::ipp::set_job_attrs("localhost:631", false, id, {{"job-state", 3}});
            if (r) return r;
            if (r.st != status::NotSupported) return r;
        }
#endif
        return job_hold(id, "resume");
    }
    result<void> hold_all()    { return job_all("hold"); }
    result<void> release_all() { return job_all("resume"); }

    result<void> set_job_priority(uint32_t id, uint32_t prio) {
        if (id == 0 || prio == 0 || prio > 100) return result<void>::fail(status::InvalidArgument);
#if KNST_PRN_HAS_NETWORK
        {
            auto r = detail::ipp::set_job_attrs("localhost:631", false, id, {{"job-priority", prio}});
            if (r) return r;
            if (r.st != status::NotSupported) return r;
        }
#endif
        detail::cmd c; c << "-i" << std::to_string(id) << "-q" << std::to_string(prio);
        return run_admin("lp", c);
    }
    result<void> move_job(uint32_t id, const knst_c16string& dst) {
        if (id == 0 || dst.empty() || !knst_dev::is_safe_token(dst)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << (detail::to_u8(name_) + "-" + std::to_string(id)) << dst;
        return run_admin("lpmove", c);
    }
    result<void> move_all_jobs(const knst_c16string& dst) {
        if (dst.empty() || !knst_dev::is_safe_token(dst)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << name_ << dst;
        return run_admin("lpmove", c);
    }

    // Waits until the job left the queue; fails with BadState if it was canceled / aborted.
    result<void> wait_for_job(uint32_t id, uint32_t timeout_ms = 60000) {
        using R = result<void>;
        if (id == 0) return R::fail(status::InvalidArgument);
        const auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            auto p = jobs(false);
            if (!p && p.st == status::ToolMissing) return R::fail(p.st);
            if (p) {
                bool still = false;
                for (auto& j : p.value) if (j.id == id) { still = true; break; }
                if (!still) {
                    auto d = job_details(id);
                    if (d && (d.value.state == job_state::Aborted || d.value.state == job_state::Canceled))
                        return R::fail(status::BadState, static_cast<int>(d.value.state));
                    return R::success();
                }
            }
            if (elapsed_ms(t0) >= timeout_ms) return R::fail(status::Timeout);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    }
    result<void> wait_for_ready(uint32_t timeout_ms = 30000) {
        const auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            auto s = status();
            if (s && s.value.enabled && s.value.accepting &&
                (s.value.state == printer_state::Ready || s.value.state == printer_state::Busy ||
                 s.value.state == printer_state::TonerLow || s.value.state == printer_state::Unknown))
                return result<void>::success();
            if (elapsed_ms(t0) >= timeout_ms) return result<void>::fail(status::Timeout);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    }
    result<bool> is_job_done(uint32_t id) const {
        using R = result<bool>;
        if (id == 0) return R::fail(status::InvalidArgument);
        auto p = jobs(false);
        if (!p) return R::fail(p.st, p.native);
        for (auto& j : p.value) if (j.id == id) return R::success(false);
        return R::success(true);
    }
    result<uint32_t> reprint_job(uint32_t id) {
        if (id == 0) return result<uint32_t>::fail(status::InvalidArgument);
        knst_c16string cached = detail::cache_find(name_, id);
        if (cached.empty()) return result<uint32_t>::fail(status::NotFound);
        return print_file(cached);
    }

    // ---- administration (usually needs privileges) ------------------------
    result<void> pause()       { return admin1("cupsdisable"); }
    result<void> resume()      { return admin1("cupsenable"); }
    result<void> reject_jobs() { return admin1("cupsreject"); }
    result<void> accept_jobs() { return admin1("cupsaccept"); }
    result<void> set_default() { detail::cmd c; c << "-d" << name_; return run_admin("lpoptions", c); }

    result<knst_vector<printer_option>> options() const {
        using R = result<knst_vector<printer_option>>;
        detail::cmd c; c << "-p" << name_ << "-l";
        auto t = c.run("lpoptions", 6000);
        if (!t.launched) return R::fail(status::ToolMissing);
        R r;
        for (auto& line : detail::split_lines(t.out)) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string head = line.substr(0, colon), vals = line.substr(colon + 1);
            printer_option po;
            size_t slash = head.find('/');
            if (slash != std::string::npos) { po.text = detail::from_u8(head.substr(slash + 1)); head = head.substr(0, slash); }
            po.name = detail::from_u8(head);
            size_t i = 0;
            while (i < vals.size()) {
                while (i < vals.size() && std::isspace(static_cast<unsigned char>(vals[i]))) ++i;
                size_t j = i;
                while (j < vals.size() && !std::isspace(static_cast<unsigned char>(vals[j]))) ++j;
                if (j > i) {
                    std::string tok = vals.substr(i, j - i);
                    if (tok[0] == '*') { tok.erase(0, 1); po.current = detail::from_u8(tok); }
                    if (!tok.empty()) po.choices.push_back(detail::from_u8(tok));
                }
                i = j;
            }
            r.value.push_back(std::move(po));
        }
        return r;
    }
    result<void> set_default_option(const knst_c16string& k, const knst_c16string& v) {
        if (!detail::safe_value(k) || !detail::safe_value(v)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << "-p" << name_ << "-o" << (detail::to_u8(k) + "=" + detail::to_u8(v));
        return run_admin("lpoptions", c);
    }
    result<void> set_option_permanent(const knst_c16string& k, const knst_c16string& v) {
        if (!detail::safe_value(k) || !detail::safe_value(v)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << "-p" << name_ << "-o" << (detail::to_u8(k) + "=" + detail::to_u8(v));
        return run_admin("lpadmin", c, 15000);
    }
    result<void> remove_option(const knst_c16string& k) {
        if (!detail::safe_value(k)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << "-p" << name_ << "-r" << k;
        return run_admin("lpoptions", c);
    }
    result<void> set_description(const knst_c16string& d) { detail::cmd c; c << "-p" << name_ << "-D" << d; return run_admin("lpadmin", c, 15000); }
    result<void> set_location(const knst_c16string& l)    { detail::cmd c; c << "-p" << name_ << "-L" << l; return run_admin("lpadmin", c, 15000); }
    result<void> set_sharing(bool on) {
        detail::cmd c; c << "-p" << name_ << "-o" << (on ? "printer-is-shared=true" : "printer-is-shared=false");
        return run_admin("lpadmin", c, 15000);
    }

    // ---- static helpers ---------------------------------------------------
    static result<knst_c16string> get_default_name() {
        using R = result<knst_c16string>;
        detail::cmd c; c << "-d";
        auto t = c.run("lpstat", 5000);
        if (!t.launched) return R::fail(status::ToolMissing);
        size_t k = t.out.find(": ");
        if (t.code != 0 || k == std::string::npos) return R::fail(status::NotFound);
        size_t eol = t.out.find('\n', k);
        std::string n = detail::trim(t.out.substr(k + 2, eol == std::string::npos ? std::string::npos : eol - k - 2));
        if (n.empty()) return R::fail(status::NotFound);
        return R::success(detail::from_u8(n));
    }

    static result<knst_vector<printer_info_ex>> list_all() {
        using R = result<knst_vector<printer_info_ex>>;
        detail::cmd c; c << "-p";
        auto t = c.run("lpstat", 8000);
        if (!t.launched) return R::fail(status::ToolMissing);
        knst_c16string def;
        { auto d = get_default_name(); if (d) def = d.value; }
        R r;
        for (auto& line : detail::split_lines(t.out)) {                // "printer NAME is idle. ..." (word 1 is localized)
            if (line.empty() || std::isspace(static_cast<unsigned char>(line[0]))) continue;
            auto tok = detail::first_tokens(line, 2);
            if (tok.size() < 2) continue;
            printer_info_ex ex;
            printer tmp; tmp.name_ = detail::from_u8(tok[1]);
            detail::kvlist kv = tmp.dest_attrs();
            ex = info_from_attrs(tmp.name_, kv);
            if (ex.device_uri.empty()) ex.device_uri = device_uri_fallback(tmp.name_);
            ex.is_default = (ex.name == def);
            r.value.push_back(std::move(ex));
        }
        return r;
    }

    static result<knst_vector<device_info>> list_available_devices() {
        using R = result<knst_vector<device_info>>;
        detail::cmd c; c << "-v";
        auto t = c.run("lpinfo", 20000);
        if (!t.launched) return R::fail(status::ToolMissing);
        R r;
        for (auto& line : detail::split_lines(t.out)) {
            auto tok = detail::first_tokens(line, 2);
            if (tok.size() != 2) continue;
            device_info di; di.type = detail::from_u8(tok[0]); di.uri = detail::from_u8(tok[1]);
            r.value.push_back(std::move(di));
        }
        return r;
    }
    static result<knst_vector<driver_info>> list_drivers() {
        using R = result<knst_vector<driver_info>>;
        detail::cmd c; c << "-m";
        auto t = c.run("lpinfo", 30000);
        if (!t.launched) return R::fail(status::ToolMissing);
        R r;
        for (auto& line : detail::split_lines(t.out)) {
            auto tok = detail::first_tokens(line, 1);
            if (tok.empty()) continue;
            driver_info di; di.id = detail::from_u8(tok[0]);
            di.description = detail::from_u8(detail::trim(line.substr(line.find(tok[0]) + tok[0].size())));
            r.value.push_back(std::move(di));
        }
        return r;
    }

    static result<server_info> server_status() {
        using R = result<server_info>;
        detail::cmd c; c << "-r";
        auto t = c.run("lpstat", 5000);
        if (!t.launched) return R::fail(status::ToolMissing);
        R r;
        r.value.running = knst_file::path_exists(u"/run/cups/cups.sock") ||
                          (t.out.find("is running") != std::string::npos && t.out.find("not running") == std::string::npos);
        char host[256] = {0};
        if (::gethostname(host, sizeof(host) - 1) == 0) r.value.hostname = detail::from_u8(host);
        { auto d = get_default_name(); if (d) r.value.default_printer = d.value; }
        { auto a = list_all(); if (a) r.value.total_printers = static_cast<uint32_t>(a.value.size()); }
        return r;
    }

    static result<bool> exists(const knst_c16string& name) {
        using R = result<bool>;
        if (name.empty()) return R::fail(status::InvalidArgument);
        detail::cmd c; c << "-p" << name;
        auto t = c.run("lpstat", 5000);
        if (!t.launched) return R::fail(status::ToolMissing);
        return R::success(t.code == 0 && !t.out.empty());
    }

    static result<uint32_t> pdf_page_count(const knst_c16string& path) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        { detail::cmd c; c << abs; auto t = c.run("pdfinfo", 5000);
          size_t k = t.out.find("Pages:");
          if (t.launched && t.code == 0 && k != std::string::npos) { long n = detail::to_long(detail::trim(t.out.substr(k + 6, 12)), 0); if (n > 0) return R::success(static_cast<uint32_t>(n)); } }
        { detail::cmd c; c << "--show-npages" << abs; auto t = c.run("qpdf", 5000);
          if (t.launched && t.code == 0) { long n = detail::to_long(detail::trim(t.out), 0); if (n > 0) return R::success(static_cast<uint32_t>(n)); } }
        knst_file_error fe = knst_file_error::None;
        knst_byte_string b = knst_file::read_file_data<>(abs, &fe);
        if (fe != knst_file_error::None) return R::fail(status::IoError);
        uint32_t n = detail::count_pdf_pages_scan(b);
        return n ? R::success(n) : R::fail(status::ToolMissing);
    }

    static result<knst_byte_string> render_preview(const knst_c16string& path, uint32_t page = 1, uint32_t dpi = 100) {
        using R = result<knst_byte_string>;
        if (path.empty() || page == 0 || dpi < 36 || dpi > 600) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        detail::temp_set tmp;
        knst_c16string prefix = tmp.add(u"");
        knst_c16string png = prefix; png.append(u".png"); tmp.paths.push_back(png);
        detail::cmd c; c << "-png" << "-singlefile" << "-f" << std::to_string(page) << "-l" << std::to_string(page)
                         << "-r" << std::to_string(dpi) << abs << prefix;
        auto t = c.run("pdftoppm", 20000);
        if (!t.launched) return R::fail(status::ToolMissing);
        if (t.code != 0 || !knst_file::file_exists(png)) return R::fail(status::IoError, t.code);
        return R::success(knst_file::read_file_data<>(png));
    }

    static result<void> add_printer(const knst_c16string& name, const knst_c16string& uri, const knst_c16string& driver_id,
                                    const knst_c16string& desc = knst_c16string(), const knst_c16string& loc = knst_c16string()) {
        using R = result<void>;
        if (name.empty() || uri.empty() || driver_id.empty() || !knst_dev::is_safe_token(name)) return R::fail(status::InvalidArgument);
        bool ok_uri = uri.starts_with(u"ipp://") || uri.starts_with(u"ipps://") || uri.starts_with(u"socket://") ||
                      uri.starts_with(u"lpd://") || uri.starts_with(u"http://") || uri.starts_with(u"https://") ||
                      uri.starts_with(u"usb://") || uri.starts_with(u"hp:/") || uri.starts_with(u"dnssd://");
        if (!ok_uri) return R::fail(status::InvalidArgument);
        detail::cmd c; c << "-p" << name << "-E" << "-v" << uri << "-m" << driver_id;
        if (!desc.empty()) c << "-D" << desc;
        if (!loc.empty())  c << "-L" << loc;
        return run_admin_s("lpadmin", c, 30000);
    }
    static result<void> add_ipp_printer(const knst_c16string& name, const knst_c16string& uri) { return add_printer(name, uri, u"everywhere"); }
    static result<void> remove_printer(const knst_c16string& name) {
        if (name.empty() || !knst_dev::is_safe_token(name)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << "-x" << name; return run_admin_s("lpadmin", c, 15000);
    }

    static result<knst_vector<printer_class>> list_classes() {
        using R = result<knst_vector<printer_class>>;
        detail::cmd c; c << "-c";
        auto t = c.run("lpstat", 5000);
        if (!t.launched) return R::fail(status::ToolMissing);
        R r; printer_class cur; bool have = false;
        for (auto& line : detail::split_lines(t.out)) {
            if (line.empty()) continue;
            if (!std::isspace(static_cast<unsigned char>(line[0]))) {          // "members of class NAME:"
                if (have) r.value.push_back(cur);
                cur = printer_class(); have = true;
                std::string h = detail::trim(line);
                if (!h.empty() && h.back() == ':') h.pop_back();
                size_t sp = h.find_last_of(" \t");
                cur.name = detail::from_u8(sp == std::string::npos ? h : h.substr(sp + 1));
            } else if (have) {
                std::string m = detail::trim(line);
                if (!m.empty()) cur.members.push_back(detail::from_u8(m));
            }
        }
        if (have) r.value.push_back(cur);
        return r;
    }
    static result<void> create_class(const knst_c16string& cn, const knst_c16string& p) {
        if (!knst_dev::is_safe_token(cn) || !knst_dev::is_safe_token(p)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << "-p" << p << "-c" << cn; return run_admin_s("lpadmin", c, 15000);
    }
    static result<void> delete_class(const knst_c16string& cn) {
        if (!knst_dev::is_safe_token(cn)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << "-x" << cn; return run_admin_s("lpadmin", c, 15000);
    }
    static result<void> add_to_class(const knst_c16string& cn, const knst_c16string& p) { return create_class(cn, p); }
    static result<void> remove_from_class(const knst_c16string& cn, const knst_c16string& p) {
        if (!knst_dev::is_safe_token(cn) || !knst_dev::is_safe_token(p)) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << "-p" << p << "-r" << cn; return run_admin_s("lpadmin", c, 15000);
    }

    static result<void> set_cache(bool enabled, const knst_c16string& dir = knst_c16string(), uint32_t max_mb = 500) {
        return detail::cache_set(enabled, dir, max_mb);
    }
    static result<void> clear_cache() { return detail::cache_clear(); }
    static uint64_t cache_size_bytes() { return detail::cache_size(); }

    // ─── Direct IPP (bypasses CUPS) — works with ipps:// TLS ─────────
    // Use when: printer is not in CUPS, or you need direct IPPS with
    // options CUPS does not pass through (finishings, output-bin, ...).
#if KNST_PRN_HAS_NETWORK
    static result<uint32_t> print_ipp(const knst_c16string& pdf_path,
                                       const knst_c16string& host_port,
                                       uint16_t port = 631,
                                       const print_options& opts = print_options(),
                                       const knst_c16string& doc_format = u"application/pdf") {
        return detail::ipp::print_pdf(pdf_path, host_port, port, opts, doc_format);
    }
    static result<uint32_t> print_ipp_send_doc(const knst_c16string& pdf_path,
                                                const knst_c16string& host_port,
                                                uint16_t port = 631,
                                                const print_options& opts = print_options(),
                                                const knst_c16string& doc_format = u"application/pdf") {
        return detail::ipp::print_pdf_send_doc(pdf_path, host_port, port, opts, doc_format);
    }
    static result<uint32_t> print_jpeg_ipp(const knst_c16string& pdf_path,
                                            const knst_c16string& host_port,
                                            uint16_t = 631,
                                            const print_options& opts = print_options()) {
        detail::ipp::send_policy p; p.prefer = "jpeg";
        return print_ipp_smart(pdf_path, host_port, opts, p);
    }
#else
    static result<uint32_t> print_ipp(const knst_c16string&, const knst_c16string&,
                                       uint16_t = 631, const print_options& = print_options(),
                                       const knst_c16string& = u"application/pdf") {
        return result<uint32_t>::fail(status::NotSupported);
    }
    static result<uint32_t> print_ipp_send_doc(const knst_c16string&, const knst_c16string&,
                                                uint16_t = 631, const print_options& = print_options(),
                                                const knst_c16string& = u"application/pdf") {
        return result<uint32_t>::fail(status::NotSupported);
    }
    static result<uint32_t> print_jpeg_ipp(const knst_c16string&, const knst_c16string&,
                                            uint16_t = 631, const print_options& = print_options()) {
        return result<uint32_t>::fail(status::NotSupported);
    }
#endif

#if KNST_PRN_HAS_NETWORK
    // ─── Direct IPP job control (bypasses CUPS) ───
    static result<void> hold_job_ipp(const knst_c16string& host_port, uint32_t job_id) {
        std::string hpu; bool use_tls = false; uint16_t port = 631;
        if (!detail::ipp::split_uri(detail::to_u8(host_port), hpu, use_tls, port))
            return result<void>::fail(status::InvalidArgument);
        return detail::ipp::set_job_attrs(hpu, use_tls, job_id, {{"job-state", 4}});
    }
    static result<void> release_job_ipp(const knst_c16string& host_port, uint32_t job_id) {
        std::string hpu; bool use_tls = false; uint16_t port = 631;
        if (!detail::ipp::split_uri(detail::to_u8(host_port), hpu, use_tls, port))
            return result<void>::fail(status::InvalidArgument);
        return detail::ipp::set_job_attrs(hpu, use_tls, job_id, {{"job-state", 3}});
    }
    static result<void> set_job_priority_ipp(const knst_c16string& host_port,
                                              uint32_t job_id, uint32_t prio) {
        if (prio == 0 || prio > 100) return result<void>::fail(status::InvalidArgument);
        std::string hpu; bool use_tls = false; uint16_t port = 631;
        if (!detail::ipp::split_uri(detail::to_u8(host_port), hpu, use_tls, port))
            return result<void>::fail(status::InvalidArgument);
        return detail::ipp::set_job_attrs(hpu, use_tls, job_id, {{"job-priority", prio}});
    }
#else
    static result<void> hold_job_ipp(const knst_c16string&, uint32_t) { return result<void>::fail(status::NotSupported); }
    static result<void> release_job_ipp(const knst_c16string&, uint32_t) { return result<void>::fail(status::NotSupported); }
    static result<void> set_job_priority_ipp(const knst_c16string&, uint32_t, uint32_t) { return result<void>::fail(status::NotSupported); }
#endif

    // ─── Universal print: spooler first, direct IPP fallback ─────────
    // Linux: CUPS via `lp` is fastest (options cached in PPD), but if
    // CUPS is not configured or the option combination is rejected,
    // fall back to direct IPP → direct JPEG-IPP.
    //
    // host_port is optional: if empty, uses CUPS only.
    // If non-empty ("host:port", "ipp://host", "ipps://host"), tries
    // direct IPP first, then CUPS as final fallback.
    result<uint32_t> print_universal(const knst_c16string& path,
                                      const knst_c16string& host_port = knst_c16string(),
                                      const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);

        // 1) Smart IPP (probe → PDF/PWG/JPEG seçimi → Print-Job + bekleme)
#if KNST_PRN_HAS_NETWORK
        if (!host_port.empty() && detail::detect_kind(path) == detail::file_kind::Pdf) {
            auto r = print_ipp_smart(path, host_port, o);
            if (r) return r;
        }
#endif
        // 2) CUPS spooler (last resort)
        return print_file(path, o);
    }

private:
    knst_c16string name_;
    mutable bool qo_loaded_ = false;
    mutable std::vector<printer_option> qo_;

    static uint32_t elapsed_ms(std::chrono::steady_clock::time_point t0) {
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    }

    detail::kvlist dest_attrs() const {
        detail::cmd c; c << "-p" << name_;
        auto t = c.run("lpoptions", 6000);
        detail::kvlist kv;
        if (t.launched && t.code == 0) {
            detail::parse_kv_line(t.out, kv);
        }
        return kv;
    }
    static printer_info_ex info_from_attrs(const knst_c16string& name, const detail::kvlist& kv) {
        printer_info_ex ex;
        ex.name = name;
        ex.description    = detail::from_u8(detail::kv_get(kv, "printer-info"));
        ex.location       = detail::from_u8(detail::kv_get(kv, "printer-location"));
        ex.make_and_model = detail::from_u8(detail::kv_get(kv, "printer-make-and-model"));
        ex.model          = ex.make_and_model;
        ex.device_uri     = detail::from_u8(detail::kv_get(kv, "device-uri"));
        ex.is_shared      = detail::lower(detail::kv_get(kv, "printer-is-shared")) == "true";
        ex.is_accepting   = detail::lower(detail::kv_get(kv, "printer-is-accepting-jobs", "true")) != "false";
        ex.is_enabled     = detail::to_long(detail::kv_get(kv, "printer-state", "3"), 3) != 5;
        ex.state          = detail::state_from_attrs(kv);
        return ex;
    }
    static knst_c16string device_uri_fallback(const knst_c16string& name) {
        detail::cmd c; c << "-v" << name;
        auto t = c.run("lpstat", 5000);
        size_t k = t.out.find(": ");
        if (!t.launched || k == std::string::npos) return knst_c16string();
        return detail::from_u8(detail::trim(t.out.substr(k + 2)));
    }

    const std::vector<printer_option>& qopts() const {
        if (!qo_loaded_) {
            auto r = options();
            if (r) for (auto& o : r.value) qo_.push_back(o);
            qo_loaded_ = true;
        }
        return qo_;
    }
    const printer_option* qfind(const char* n) const {
        for (auto& o : qopts()) if (detail::to_u8(o.name) == n) return &o;
        return nullptr;
    }

    // page size of generated documents: custom > paper > queue default > A4
    void page_dims(const print_options& o, float& w, float& h) const {
        w = 210.0f; h = 297.0f;
        if (o.custom_w_mm && o.custom_h_mm) { w = static_cast<float>(o.custom_w_mm); h = static_cast<float>(o.custom_h_mm); }
        else if (!detail::paper_mm(o.paper, w, h)) {
            if (const printer_option* q = qfind("PageSize"))
                detail::paper_mm(detail::paper_from_name(detail::to_u8(q->current)), w, h);
        }
        if (o.orient == orientation::Landscape || o.orient == orientation::ReverseLandscape) std::swap(w, h);
    }

    void build_args(knst_dev::argv& a, const print_options& o, bool generated) const {
        using namespace detail;
        auto O = [&](const std::string& kv) { a.push_back("-o"); a.push_back(kv); };
        auto ppd = [&](const char* key, std::initializer_list<const char*> want) {
            const printer_option* q = qfind(key);
            if (!q) return;
            for (const char* w : want)
                for (uint32_t i = 0; i < q->choices.size(); ++i)
                    if (ieq(to_u8(q->choices[i]), w)) { O(std::string(key) + "=" + to_u8(q->choices[i])); return; }
        };
        if (o.copies > 1) { a.push_back("-n"); a.push_back(std::to_string(o.copies)); }
        if (!o.title.empty()) { a.push_back("-t"); a.push_back(to_u8(o.title)); }
        if (!o.page_ranges.empty()) { a.push_back("-P"); a.push_back(to_u8(o.page_ranges)); }
        if (o.priority != 50) { a.push_back("-q"); a.push_back(std::to_string(o.priority)); }

        switch (o.duplex) {
            case duplex_mode::Simplex:   O("sides=one-sided");            ppd("Duplex", {"None"}); break;
            case duplex_mode::LongEdge:  O("sides=two-sided-long-edge");  ppd("Duplex", {"DuplexNoTumble"}); break;
            case duplex_mode::ShortEdge: O("sides=two-sided-short-edge"); ppd("Duplex", {"DuplexTumble"}); break;
            default: break;
        }
        switch (o.color) {
            case color_mode::Color: O("print-color-mode=color"); ppd("ColorModel", {"RGB", "Color", "CMYK"}); break;
            case color_mode::Mono:
            case color_mode::Gray:  O("print-color-mode=monochrome"); ppd("ColorModel", {"Gray", "KGray", "Mono", "Grayscale", "Monochrome"}); break;
            case color_mode::Auto:  O("print-color-mode=auto"); break;
            default: break;
        }
        if (!generated) {                       // generated documents already have the right page orientation
            switch (o.orient) {
                case orientation::Portrait:         O("orientation-requested=3"); break;
                case orientation::Landscape:        O("orientation-requested=4"); break;
                case orientation::ReverseLandscape: O("orientation-requested=5"); break;
                case orientation::ReversePortrait:  O("orientation-requested=6"); break;
                default: break;
            }
        }
        print_quality q = o.quality;
        if (q == print_quality::Default && o.toner_save) q = print_quality::Draft;
        switch (q) {
            case print_quality::Draft:  O("print-quality=3"); ppd("cupsPrintQuality", {"Draft", "Fast"}); break;
            case print_quality::Normal: O("print-quality=4"); ppd("cupsPrintQuality", {"Normal"}); break;
            case print_quality::High:   O("print-quality=5"); ppd("cupsPrintQuality", {"High", "Best"}); break;
            default: break;
        }
        std::string media;
        if (!o.media.empty()) media = to_u8(o.media);
        else if (o.custom_w_mm && o.custom_h_mm) media = "Custom." + std::to_string(o.custom_w_mm) + "x" + std::to_string(o.custom_h_mm) + "mm";
        else if (const char16_t* m = media_str(o.paper)) media = to_u8(knst_c16string(m));
        if (!media.empty()) {
            O("media=" + media);
            if (o.media.empty() && o.paper != media_size::Default) if (const char* pn = ppd_page_name(o.paper)) ppd("PageSize", {pn});
        }
        if (const char* pt = paper_type_str(o.ptype)) O(std::string("media-type=") + pt);
        if (o.media_weight_gsm > 0) O("media-weight-metric=" + std::to_string(o.media_weight_gsm));
        if (o.borderless && !o.fit_to_page) O("print-scaling=fill");
        if (!o.resolution.empty()) { O("printer-resolution=" + to_u8(o.resolution)); ppd("Resolution", {to_u8(o.resolution).c_str()}); }
        if (o.auto_center) O("position=center");
        if (o.brightness != 0) O("brightness=" + std::to_string(100 + o.brightness));
        if (o.saturation != 0) O("saturation=" + std::to_string(100 + o.saturation));
        if (o.mirror_horizontal || o.mirror_vertical) O("mirror=true");
        if (o.n_up > 1) O("number-up=" + std::to_string(o.n_up));
        if (o.booklet) O("booklet=true");
        if (o.reverse) O("outputorder=reverse");
        auto pt = [](int mm) { return std::to_string(static_cast<int>(mm * 72.0 / 25.4 + 0.5)); };
        if (o.margin_top_mm > 0)    O("page-top=" + pt(o.margin_top_mm));
        if (o.margin_bottom_mm > 0) O("page-bottom=" + pt(o.margin_bottom_mm));
        if (o.margin_left_mm > 0)   O("page-left=" + pt(o.margin_left_mm));
        if (o.margin_right_mm > 0)  O("page-right=" + pt(o.margin_right_mm));
        if (o.fit_to_page) O("fit-to-page=true");
        else if (o.scaling != 100) O("scaling=" + std::to_string(o.scaling));
        if (o.collate) O("collate=true");
        if (!o.media_source.empty()) { O("media-source=" + to_u8(o.media_source)); ppd("InputSlot", {to_u8(o.media_source).c_str()}); }
        std::string bin = o.output_bin.empty() ? (o.face_down ? std::string("face-down") : std::string()) : to_u8(o.output_bin);
        if (!bin.empty()) { O("output-bin=" + bin); ppd("OutputBin", {bin.c_str()}); }
        if (o.staple || o.punch || o.fold) {
            std::string f;
            if (o.staple) f += "4,";
            if (o.punch)  f += "5,";
            if (o.fold)   f += "10,";
            f.pop_back(); O("finishings=" + f);
        }
        const char* js1 = job_sheet_str(o.job_sheet_start);
        const char* js2 = job_sheet_str(o.job_sheet_end);
        if (js1 || js2) O(std::string("job-sheets=") + (js1 ? js1 : "none") + "," + (js2 ? js2 : "none"));
        if (o.raw) O("raw");
        for (uint32_t i = 0; i < o.extra_options.size(); ++i) O(to_u8(o.extra_options[i]));
    }

    result<uint32_t> submit(const std::vector<knst_c16string>& files, const print_options& o, bool generated,
                            bool force_raw = false, const knst_vector<knst_c16string>& extra = knst_vector<knst_c16string>()) {
        using R = result<uint32_t>;
        if (files.empty() || !detail::valid_options(o)) return R::fail(status::InvalidArgument);

        detail::cmd c;
        c << "-d" << name_;

        knst_dev::argv built;
        build_args(built, o, generated);
        c << built;

        if (force_raw && !o.raw) { c << "-o" << "raw"; }
        for (uint32_t i = 0; i < extra.size(); ++i) { c << "-o" << extra[i]; }
        for (auto& f : files) c << f;

        auto t = c.run("lp", 60000);
        if (!t.launched) return R::fail(status::ToolMissing);
        if (t.code != 0) return R::fail(status::IoError, t.code);

        const std::string out_str = t.str();
        uint32_t id = detail::parse_job_id(out_str, detail::to_u8(name_));
        if (!id) return R::fail(status::Unknown);
        if (o.cache_for_reprint) detail::cache_store(name_, id, files[0]);
        return R::success(id);
    }

    result<uint32_t> print_document(const detail::document& doc, const print_options& o) {
        detail::temp_set tmp; knst_c16string pdf = tmp.add(u".pdf");
        auto st = detail::write_pdf(doc, pdf);
        if (st != status::Ok) return result<uint32_t>::fail(st);
        return submit({pdf}, o, true);
    }

    knst_prn::status compose_images(const std::vector<knst_c16string>& paths, const image_batch_options& bo,
                          const print_options& o, detail::temp_set& tmp, knst_c16string& out_pdf) {
        float w, h; page_dims(o, w, h);
        detail::document doc;
        knst_prn::status st = detail::layout_images(paths, bo, w, h, doc);
        if (st != status::Ok) return st;
        out_pdf = tmp.add(u".pdf");
        return detail::write_pdf(doc, out_pdf);
    }

    result<void> admin1(const char* tool) { detail::cmd c; c << name_; return run_admin(tool, c); }
    result<void> run_admin(const char* tool, const detail::cmd& c, uint32_t timeout_ms = 8000) { return run_admin_s(tool, c, timeout_ms); }
    static result<void> run_admin_s(const char* tool, const detail::cmd& c, uint32_t timeout_ms = 8000) {
        auto t = c.run(tool, timeout_ms);
        if (!t.launched) return result<void>::fail(status::ToolMissing);
        if (t.code != 0) return result<void>::fail(status::IoError, t.code);
        return result<void>::success();
    }
    result<void> job_hold(uint32_t id, const char* mode) {
        if (id == 0) return result<void>::fail(status::InvalidArgument);
        detail::cmd c; c << "-i" << std::to_string(id) << "-H" << mode;
        return run_admin("lp", c);
    }
    result<void> job_all(const char* mode) {
        auto p = jobs(false);
        if (!p) return result<void>::fail(p.st, p.native);
        bool any = false;
        for (auto& j : p.value) if (job_hold(j.id, mode)) any = true;
        return any ? result<void>::success() : result<void>::fail(status::NotFound);
    }
};

#endif // __linux__

// ===========================================================================
//                         Scanner class - Linux/SANE
// ===========================================================================
#if defined(__linux__) && !defined(__ANDROID__)

namespace detail {

inline void parse_sane_options(const std::string& out, scanner_capabilities& caps) {
    auto after_opt = [](const std::string& s, size_t skip) {
        std::string r = trim(s.substr(skip));
        size_t b = r.find('[');
        if (b != std::string::npos) r = trim(r.substr(0, b));
        return r;
    };
    bool saw_source = false;
    for (auto& line : split_lines(out)) {
        std::string s = trim(line);
        if (starts_with(s, "--resolution")) {
            std::string r = after_opt(s, 12);
            size_t d = lower(r).find("dpi");
            if (d != std::string::npos) r.erase(d, 3);
            r = trim(r);
            size_t dd = r.find("..");
            if (dd != std::string::npos) {
                double lo = c_atof(r.substr(0, dd)), hi = c_atof(r.substr(dd + 2));
                static const uint32_t std_dpi[] = {75, 100, 150, 200, 300, 400, 600, 1200, 2400, 4800};
                for (uint32_t v : std_dpi) if (v >= lo && v <= hi) caps.resolutions.push_back(v);
            } else {
                for (auto& t : split(r, '|')) { long v = to_long(trim(t), 0); if (v > 0) caps.resolutions.push_back(static_cast<uint32_t>(v)); }
            }
        } else if (starts_with(s, "--mode")) {
            for (auto& t0 : split(after_opt(s, 6), '|')) {
                std::string t = trim(t0), lt = lower(t);
                if (t.empty()) continue;
                caps.mode_names.push_back(from_u8(t));
                if (lt.find("color") != std::string::npos) caps.modes.push_back(color_mode::Color);
                else if (lt.find("gray") != std::string::npos || lt.find("grey") != std::string::npos) caps.modes.push_back(color_mode::Gray);
                else caps.modes.push_back(color_mode::Mono);
            }
        } else if (starts_with(s, "--source")) {
            saw_source = true;
            for (auto& t0 : split(after_opt(s, 8), '|')) {
                std::string t = trim(t0), lt = lower(t);
                if (t.empty()) continue;
                caps.sources.push_back(from_u8(t));
                if (lt.find("flatbed") != std::string::npos || lt.find("platen") != std::string::npos) caps.has_flatbed = true;
                if (lt.find("adf") != std::string::npos || lt.find("feeder") != std::string::npos) caps.has_adf = true;
                if (lt.find("duplex") != std::string::npos) caps.has_duplex_adf = true;
            }
        } else if (starts_with(s, "-x ") || starts_with(s, "-y ")) {
            std::string r = after_opt(s, 2);
            size_t dd = r.find("..");
            if (dd != std::string::npos && lower(r).find("mm") != std::string::npos) {
                uint32_t mm = static_cast<uint32_t>(c_atof(r.substr(dd + 2)) + 0.5);
                (s[1] == 'x' ? caps.max_width_mm : caps.max_height_mm) = mm;
            }
        }
    }
    if (saw_source && caps.sources.size() > 0) {
        bool fb = false;
        for (auto& n : caps.sources) { std::string l = lower(to_u8(n)); if (l.find("flatbed") != std::string::npos || l.find("platen") != std::string::npos) fb = true; }
        caps.has_flatbed = fb;
    }
}

struct scan_dir {
    knst_c16string path;
    scan_dir() {
        static std::atomic<uint32_t> ctr{0};
        std::string n = "knst_scan_" + std::to_string(static_cast<uint32_t>(::getpid())) + "_" + std::to_string(ctr++);
        path = knst_file::join_path(knst_file::get_temp_dir(), from_u8(n));
        knst_file::create_directories(path);
    }
    scan_dir(const scan_dir&) = delete;
    scan_dir& operator=(const scan_dir&) = delete;
    ~scan_dir() { knst_file::remove_all(path); }
};

} // namespace detail

class scanner {
public:
    static result<knst_vector<scanner_info>> list_all() {
        using R = result<knst_vector<scanner_info>>;
        detail::cmd c; c << "-L";
        auto t = c.run("scanimage", 25000);
        if (!t.launched) return R::fail(status::ToolMissing);
        R r;
        for (auto& line : detail::split_lines(t.out)) {
            size_t p = line.find("device ");
            if (p == std::string::npos || p + 8 >= line.size()) continue;
            char qc = line[p + 7];
            if (qc != '`' && qc != '\'') continue;
            size_t st = p + 8, e = line.find("' is ", st);
            std::string rest;
            if (e != std::string::npos) rest = line.substr(e + 5);
            else { e = line.rfind('\''); if (e == std::string::npos || e <= st) continue; }
            std::string id = line.substr(st, e - st);
            if (detail::starts_with(rest, "a ")) rest = rest.substr(2);
            else if (detail::starts_with(rest, "an ")) rest = rest.substr(3);
            std::string lr = detail::lower(rest);
            size_t cut = std::string::npos;
            for (const char* k : {" platen", " flatbed", " adf", " feeder", " ip=", " scanner"}) {
                size_t x = lr.find(k);
                if (x != std::string::npos && x < cut) cut = x;
            }
            scanner_info si;
            si.device_id = detail::from_u8(id);
            si.name = detail::from_u8(detail::trim(cut == std::string::npos ? rest : rest.substr(0, cut)));
            if (si.name.empty()) si.name = si.device_id;
            size_t colon = id.find(':');
            std::string be = colon == std::string::npos ? id : id.substr(0, colon);
            si.backend = detail::from_u8(be);
            si.adf = lr.find("adf") != std::string::npos || lr.find("feeder") != std::string::npos;
            si.duplex_adf = lr.find("duplex") != std::string::npos;
            si.flatbed = true;
            si.is_network = be == "escl" || be == "airscan" || be == "net" || id.find("http") != std::string::npos ||
                            lr.find("ip=") != std::string::npos;
                        r.value.push_back(std::move(si));
        }

        // Sort: prefer `airscan` over `escl`.
        // The `escl` backend on HP printers (eSCL over HTTPS) ignores
        // --source=Flatbed and returns HTTP 409 conflict when the ADF
        // is empty. `airscan` (sane-airscan) handles --source correctly.
        // AirScan first, then everything else in original order.
        for (uint32_t i = 1; i < r.value.size(); ++i) {
            scanner_info key = r.value[i];
            int j = static_cast<int>(i) - 1;
            bool key_airscan = (detail::to_u8(key.backend) == "airscan");
            while (j >= 0) {
                bool cur_airscan = (detail::to_u8(r.value[j].backend) == "airscan");
                if (key_airscan && !cur_airscan) { r.value[j + 1] = r.value[j]; --j; }
                else break;
            }
            r.value[j + 1] = key;
        }
        return r;
    }

    // `device_id` is the id returned by list_all() (scanner_info::device_id)
    static result<scanner> open(const knst_c16string& device_id) {
        using R = result<scanner>;
        if (device_id.empty()) return R::fail(status::InvalidArgument);
        scanner s; s.name_ = device_id; s.device_id_ = device_id;
        return R::success(std::move(s));
    }

    const knst_c16string& name() const noexcept { return name_; }

    result<scanner_capabilities> capabilities() const {
        using R = result<scanner_capabilities>;
        knst_prn::status st = load_caps();
        if (st != status::Ok) return R::fail(st);
        scanner_capabilities c = caps_;
        if (c.modes.empty()) { c.modes.push_back(color_mode::Color); c.mode_names.push_back(u"Color"); }
        if (c.resolutions.empty()) for (uint32_t v : {100u, 200u, 300u, 600u}) c.resolutions.push_back(v);
        return R::success(c);
    }

    // Scans ONE page and returns the encoded image (format = scan_options::output_format).
    result<knst_byte_string> scan_page(const scan_options& opt = scan_options()) {
        using R = result<knst_byte_string>;
        scan_options o = opt; o.page_count = 1;
        detail::scan_dir dir;
        std::string fmt = format_of(o);
        auto pages = scan_pages(o, dir.path, fmt);
        if (!pages) return R::fail(pages.st, pages.native);
        knst_file_error fe = knst_file_error::None;
        knst_byte_string b = knst_file::read_file_data<>(pages.value[0], &fe);
        if (fe != knst_file_error::None || b.empty()) return R::fail(status::IoError);
        return R::success(std::move(b));
    }

    // png / jpeg / tiff.  With the feeder (use_adf) every page is written as <name>_001.ext, <name>_002.ext ...
    // and the first file is returned.  output_format "pdf" is forwarded to scan_to_pdf().
    result<knst_c16string> scan_to_file(const knst_c16string& path, const scan_options& opt = scan_options()) {
        using R = result<knst_c16string>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        std::string fmt = format_of(opt);
        if (detail::lower(detail::to_u8(opt.output_format)) == "pdf") return scan_to_pdf(path, opt);
        knst_c16string abs = knst_file::resolve_path(path);
        detail::scan_dir dir;
        auto pages = scan_pages(opt, dir.path, fmt);
        if (!pages) return R::fail(pages.st, pages.native);
        const bool multi = opt.use_adf && pages.value.size() > 1;
        if (!multi) {
            if (!knst_file::move_path(pages.value[0], abs, true)) return R::fail(status::IoError);
            return R::success(abs);
        }
        knst_c16string parent = knst_file::get_parent_dir(abs);
        std::string stem = detail::to_u8(knst_file::get_stem(abs));
        std::string ext = detail::path_ext(pages.value[0]);
        knst_c16string first;
        for (size_t i = 0; i < pages.value.size(); ++i) {
            char num[16]; std::snprintf(num, sizeof(num), "_%03u", static_cast<unsigned>(i + 1));
            knst_c16string dst = knst_file::join_path(parent, detail::from_u8(stem + num + ext));
            if (!knst_file::move_path(pages.value[i], dst, true)) return R::fail(status::IoError);
            if (i == 0) first = dst;
        }
        return R::success(first);
    }

    // Multi page PDF (feeder) - the PDF is written by the library (JPEG pages are embedded as they are).
    result<knst_c16string> scan_to_pdf(const knst_c16string& path, const scan_options& opt = scan_options()) {
        using R = result<knst_c16string>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        detail::scan_dir dir;
        auto pages = scan_chain(opt, dir.path);
        if (!pages) return R::fail(pages.st, pages.native);
        float w = 210.0f, h = 297.0f;
        if (!detail::paper_mm(opt.paper, w, h)) { w = 210.0f; h = 297.0f; }
        std::vector<knst_c16string> v;
        for (auto& p : pages.value) v.push_back(p);
        image_batch_options bo; bo.margin_mm = 0.0f; bo.fit = fit_mode::Fit; bo.apply_exif = false;
        bo.skip_unreadable = false; bo.max_dpi = std::max<uint32_t>(150, opt.dpi);
        detail::document doc;
        auto st = detail::layout_images(v, bo, w, h, doc);
        if (st != status::Ok) return R::fail(st);
        knst_c16string tmp = abs; tmp.append(u".knsttmp");
        st = detail::write_pdf(doc, tmp);
        if (st != status::Ok) { knst_file::remove_file(tmp); return R::fail(st); }
                if (!knst_file::move_path(tmp, abs, true)) { knst_file::remove_file(tmp); return R::fail(status::IoError); }
        return R::success(abs);
    }

    // Multi-page ADF scan → one file per page (prefix_001.png, prefix_002.png, ...).
    // Returns every path; output_format "pdf" is rejected (use scan_to_pdf).
    result<knst_vector<knst_c16string>> scan_to_files(const knst_c16string& prefix_path,
                                                       const scan_options& o = scan_options()) {
        using R = result<knst_vector<knst_c16string>>;
        if (prefix_path.empty()) return R::fail(status::InvalidArgument);
        std::string fmt = detail::lower(detail::to_u8(o.output_format));
        if (fmt == "pdf") return R::fail(status::InvalidArgument);

        knst_c16string abs = knst_file::resolve_path(prefix_path);
        knst_c16string parent = knst_file::get_parent_dir(abs);
        if (parent.empty()) parent = knst_file::get_temp_dir();
        std::string stem = detail::to_u8(knst_file::get_stem(abs));
        if (stem.empty()) stem = "scan";

        detail::scan_dir dir;
        auto pages = scan_pages(o, dir.path, format_of(o));
        if (!pages) return R::fail(pages.st, pages.native);

        const std::string ext = detail::path_ext(pages.value[0]);  // ".jpg" / ".png" / ".tif"
        R out;
        for (size_t i = 0; i < pages.value.size(); ++i) {
            char num[16];
            std::snprintf(num, sizeof(num), "_%03u", static_cast<unsigned>(i + 1));
            knst_c16string dst = knst_file::join_path(
                parent, detail::from_u8(stem + num + ext));
            if (!knst_file::move_path(pages.value[i], dst, true))
                return R::fail(status::IoError);
            out.value.push_back(dst);
        }
        return out;
    }

    // Scans and prints on the default (or first) printer as ONE job.
    result<uint32_t> scan_and_print(const print_options& popt = print_options(), const scan_options& sopt = scan_options()) {
        auto pr = printer::open_any();
        if (!pr) return result<uint32_t>::fail(pr.st, pr.native);
        return scan_and_print_to(pr.value, popt, sopt);
    }
    result<uint32_t> scan_and_print_to(printer& pr, const print_options& popt = print_options(),
                                       const scan_options& sopt = scan_options()) {
        using R = result<uint32_t>;
        detail::scan_dir dir;
        auto pages = scan_chain(sopt, dir.path);
        if (!pages) return R::fail(pages.st, pages.native);
        std::vector<knst_c16string> v;
        for (auto& p : pages.value) v.push_back(p);
        image_batch_options bo; bo.margin_mm = popt.borderless ? 0.0f : 5.0f; bo.apply_exif = false;
        bo.skip_unreadable = false;
        return pr.print_images(v, bo, popt);        // the spooler has its own copy when lp returns
    }

private:
    knst_c16string name_;
    knst_c16string device_id_;
    mutable bool caps_loaded_ = false;
    mutable scanner_capabilities caps_;

    // Process-wide capabilities cache, keyed by device_id.
    // `scanimage -A` can take 2-5 s on slow eSCL devices; caching makes
    // subsequent scanner::open() + capabilities() calls instant.
    struct caps_entry {
        scanner_capabilities caps;
        std::chrono::steady_clock::time_point at;
    };
    static std::mutex& _caps_mu() {
        static std::mutex m; return m;
    }
    static knst_vector<std::pair<knst_c16string, caps_entry>>& _caps_map() {
        static knst_vector<std::pair<knst_c16string, caps_entry>> m; return m;
    }
    static constexpr int _caps_ttl_sec = 3600;  // 1 hour — caps are static
    static void _caps_put(const knst_c16string& id, const scanner_capabilities& c) {
        std::lock_guard<std::mutex> lk(_caps_mu());
        auto& m = _caps_map();
        for (auto& p : m) if (p.first == id) {
            p.second.caps = c;
            p.second.at = std::chrono::steady_clock::now();
            return;
        }
        m.push_back({id, {c, std::chrono::steady_clock::now()}});
    }
    static bool _caps_get(const knst_c16string& id, scanner_capabilities& out) {
        std::lock_guard<std::mutex> lk(_caps_mu());
        auto& m = _caps_map();
        auto now = std::chrono::steady_clock::now();
        for (auto& p : m) {
            if (p.first != id) continue;
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - p.second.at).count();
            if (age > _caps_ttl_sec) return false;
            out = p.second.caps;
            return true;
        }
        return false;
    }

    static std::string format_of(const scan_options& o) {
        std::string f = detail::lower(detail::to_u8(o.output_format));
        if (f == "jpg" || f == "jpeg") return "jpeg";
        if (f == "tif" || f == "tiff") return "tiff";
        if (f == "pnm" || f == "ppm") return "pnm";
        return "png";
    }

    knst_prn::status load_caps() const {
        if (caps_loaded_) return status::Ok;

        // Fast path: process-wide cache.
        {
            scanner_capabilities cached;
            if (_caps_get(device_id_, cached)) {
                caps_ = std::move(cached);
                caps_loaded_ = true;
                return status::Ok;
            }
        }

        detail::cmd c; c << "-d" << device_id_ << "-A";
        auto t = c.run("scanimage", 25000);
        if (!t.launched) return status::ToolMissing;
        if (t.out.empty()) return status::IoError;          // device not reachable
        detail::parse_sane_options(t.out, caps_);
        caps_loaded_ = true;
        _caps_put(device_id_, caps_);
        return status::Ok;
    }

    std::string pick_mode(color_mode m) const {
        std::vector<const char*> want;
        if (m == color_mode::Gray)      want = {"Gray", "Grayscale", "Grey"};
        else if (m == color_mode::Mono) want = {"Lineart", "Binary", "Black & White", "Halftone"};
        else                            want = {"Color"};
        for (const char* w : want)
            for (uint32_t i = 0; i < caps_.mode_names.size(); ++i)
                if (detail::ieq(detail::to_u8(caps_.mode_names[i]), w)) return detail::to_u8(caps_.mode_names[i]);
        if (caps_.mode_names.empty()) return want[0];
        return detail::to_u8(caps_.mode_names[0]);
    }

    void base_args(detail::cmd& c, const scan_options& o, bool geometry) const {
        load_caps();                                       // best effort, caps_ stays empty on failure
        c << "-d" << device_id_;
        std::string src = detail::to_u8(o.source);

        if (src.empty()) {
            if (o.use_adf) {
                // User asked for ADF: pick a feeder source from capabilities
                for (uint32_t i = 0; i < caps_.sources.size() && src.empty(); ++i) {
                    std::string s = detail::lower(detail::to_u8(caps_.sources[i]));
                    bool adf = s.find("adf") != std::string::npos || s.find("feeder") != std::string::npos;
                    bool dup = s.find("duplex") != std::string::npos;
                    if (adf && dup == o.duplex) src = detail::to_u8(caps_.sources[i]);
                }
                if (src.empty()) src = o.duplex ? "ADF Duplex" : "ADF";
            } else {
                // Flatbed: EXPLICITLY select it. Without this, the `escl`
                // backend on HP printers defaults to ADF and returns HTTP 409.
                for (uint32_t i = 0; i < caps_.sources.size() && src.empty(); ++i) {
                    std::string s = detail::lower(detail::to_u8(caps_.sources[i]));
                    if (s.find("flatbed") != std::string::npos ||
                        s.find("platen")  != std::string::npos) {
                        src = detail::to_u8(caps_.sources[i]);
                    }
                }
                if (src.empty()) src = "Flatbed";
            }
        }
        if (!src.empty()) c << ("--source=" + src);
        uint32_t dpi = o.dpi ? o.dpi : 300;
        if (!caps_.resolutions.empty()) {                   // snap to the nearest supported value
            uint32_t best = caps_.resolutions[0];
            for (uint32_t i = 0; i < caps_.resolutions.size(); ++i) {
                uint32_t r = caps_.resolutions[i];
                if (std::abs(static_cast<int>(r) - static_cast<int>(dpi)) < std::abs(static_cast<int>(best) - static_cast<int>(dpi))) best = r;
            }
            dpi = best;
        }
        c << ("--resolution=" + std::to_string(dpi));
        c << ("--mode=" + pick_mode(o.mode));
        if (geometry) {
            float w, h;
            if (detail::paper_mm(o.paper, w, h)) {
                if (caps_.max_width_mm && w > caps_.max_width_mm) w = static_cast<float>(caps_.max_width_mm);
                if (caps_.max_height_mm && h > caps_.max_height_mm) h = static_cast<float>(caps_.max_height_mm);
                c << "-l" << "0" << "-t" << "0" << "-x" << std::to_string(static_cast<int>(w)) << "-y" << std::to_string(static_cast<int>(h));
            }
        }
        if (o.brightness != 0) c << ("--brightness=" + std::to_string(o.brightness));
        if (o.contrast != 0)   c << ("--contrast=" + std::to_string(o.contrast));
    }

    // Writes page_001.ext, page_002.ext ... into `dir` and returns them in order.
    result<knst_vector<knst_c16string>> scan_pages(const scan_options& o, const knst_c16string& dir, const std::string& fmt) {
        using R = result<knst_vector<knst_c16string>>;
        const std::string ext = fmt == "jpeg" ? "jpg" : fmt == "tiff" ? "tif" : fmt == "pnm" ? "pnm" : "png";
        const bool multi = o.use_adf;
        int code = 0; bool launched = true;
        for (int attempt = 0; attempt < 2; ++attempt) {
            const bool geometry = (attempt == 0);
            float tw, th;
            if (!geometry && !detail::paper_mm(o.paper, tw, th)) break;
            detail::cmd c; base_args(c, o, geometry);
            c << ("--format=" + fmt);
            if (multi) {
                knst_c16string pat = knst_file::join_path(dir, detail::from_u8("page_%03d." + ext));
                c << ("--batch=" + detail::to_u8(pat)) << "--batch-start=1";
                if (o.page_count > 0) c << ("--batch-count=" + std::to_string(o.page_count));
            } else {
                knst_c16string one = knst_file::join_path(dir, detail::from_u8("page_001." + ext));
                c << ("--output-file=" + detail::to_u8(one));
            }
                        auto t = c.run("scanimage", (o.timeout_s ? o.timeout_s : 120) * 1000u);
            launched = t.launched; code = t.code;
            if (!launched) return R::fail(status::ToolMissing);
            R r;
            knst_vector<knst_file> entries;
            knst_file::list_directory(dir, entries);
            std::vector<std::pair<std::string, knst_c16string>> found;
            for (auto& e : entries) {
                std::string nm = detail::to_u8(e.name);
                if (e.is_file && e.size > 0 && detail::starts_with(nm, "page_") && detail::path_ext(e.name) == "." + ext)
                    found.emplace_back(nm, e.path);
            }
            std::sort(found.begin(), found.end(), [](const std::pair<std::string, knst_c16string>& a,
                                                     const std::pair<std::string, knst_c16string>& b) { return a.first < b.first; });
            // Pages produced → return them (feeder "empty" ends with non-zero,
            // but pages are already saved).
            if (!found.empty() && (code == 0 || multi)) {
                for (auto& f : found) r.value.push_back(f.second);
                return r;
            }

            // scanimage said "success" (code == 0) but produced no file.
            // This is either an empty ADF or an unreachable device — retrying
            // with different args will not help. Skip the second attempt.
            if (code == 0 && found.empty()) {
                for (auto& e : entries) knst_file::remove_file(e.path);
                return R::fail(status::NotFound, 0);
            }

            // Real error (code != 0, no pages). Clean up and retry once
            // without geometry — some HP firmware rejects -x/-y.
            for (auto& e : entries) knst_file::remove_file(e.path);
        }
        return R::fail(status::IoError, code);
    }

       result<knst_vector<knst_c16string>> scan_chain(const scan_options& o, const knst_c16string& dir) {
        // ADF: DO NOT retry with another format. WIA advances the feeder on
        // every Download() attempt; a failed first attempt would have already
        // consumed sheets and retrying with png/bmp starts from a partial or
        // empty feeder → pages get lost. Use JPEG (universally supported) as
        // the only format for ADF.
        if (o.use_adf) {
            return scan_pages(o, dir, "jpeg");
        }
        // Flatbed: try jpeg, then png, then bmp (page doesn't move between
        // attempts so retry is safe).
        result<knst_vector<knst_c16string>> last =
            result<knst_vector<knst_c16string>>::fail(status::IoError);
        for (const char* f : {"jpeg", "png", "bmp"}) {
            auto r = scan_pages(o, dir, f);
            if (r) return r;
            last = r;
        }
        return last;
    }
};

#endif // __linux__

// ===========================================================================
//                    Windows helpers (winspool + GDI)
// ===========================================================================
#if defined(_WIN32)

namespace detail {

inline std::wstring wide(const knst_c16string& s) {
    return std::wstring(reinterpret_cast<const wchar_t*>(s.data()), s.length());
}
inline knst_c16string from_wide(const wchar_t* w) {
    return (w && *w) ? knst_c16string(reinterpret_cast<const char16_t*>(w)) : knst_c16string();
}

struct hprinter {
    HANDLE h = nullptr;
    explicit hprinter(const knst_c16string& n, DWORD access = 0) {
        std::wstring w = wide(n);
        PRINTER_DEFAULTSW pd{nullptr, nullptr, access};
        if (!OpenPrinterW(&w[0], &h, access ? &pd : nullptr)) h = nullptr;
    }
    ~hprinter() { if (h) ClosePrinter(h); }
    hprinter(const hprinter&) = delete;
    hprinter& operator=(const hprinter&) = delete;
    bool ok() const { return h != nullptr; }
};

inline knst_prn::status win_status(DWORD err) {
    switch (err) {
        case ERROR_ACCESS_DENIED:        return status::Permission;
        case ERROR_INVALID_PRINTER_NAME:
        case ERROR_FILE_NOT_FOUND:
        case ERROR_INVALID_PARAMETER:    return status::NotFound;
        case ERROR_OUTOFMEMORY:
        case ERROR_NOT_ENOUGH_MEMORY:    return status::OutOfMemory;
        default:                         return status::IoError;
    }
}

inline printer_state win_state(DWORD st) {
    if (st & PRINTER_STATUS_PAPER_JAM) return printer_state::PaperJam;
    if (st & PRINTER_STATUS_PAPER_OUT) return printer_state::PaperOut;
    if (st & PRINTER_STATUS_DOOR_OPEN) return printer_state::DoorOpen;
    if (st & (PRINTER_STATUS_OFFLINE | PRINTER_STATUS_NOT_AVAILABLE)) return printer_state::Offline;
    if (st & (PRINTER_STATUS_ERROR | PRINTER_STATUS_USER_INTERVENTION | PRINTER_STATUS_PAPER_PROBLEM)) return printer_state::Error;
    if (st & PRINTER_STATUS_PAUSED) return printer_state::Paused;
    if (st & (PRINTER_STATUS_TONER_LOW | PRINTER_STATUS_NO_TONER)) return printer_state::TonerLow;
    if (st & PRINTER_STATUS_WARMING_UP) return printer_state::WarmingUp;
    if (st & (PRINTER_STATUS_BUSY | PRINTER_STATUS_PRINTING | PRINTER_STATUS_PROCESSING |
              PRINTER_STATUS_WAITING | PRINTER_STATUS_INITIALIZING)) return printer_state::Busy;
    return printer_state::Ready;
}

#ifndef JOB_STATUS_RENDERING_LOCALLY
// Older SDKs (e.g. MinGW-w64 / older Windows SDK) lack this constant.
// Value from winspool.h: 0x00000400.
#  define JOB_STATUS_RENDERING_LOCALLY 0x00000400
#endif

inline job_state win_job_state(DWORD st) {
    if (st & (JOB_STATUS_DELETING | JOB_STATUS_DELETED)) return job_state::Canceled;
    if (st & JOB_STATUS_ERROR) return job_state::Aborted;
    if (st & (JOB_STATUS_COMPLETE | JOB_STATUS_PRINTED)) return job_state::Completed;
    if (st & JOB_STATUS_PAUSED) return job_state::Held;
    if (st & JOB_STATUS_BLOCKED_DEVQ) return job_state::Blocked;
    if (st & JOB_STATUS_SPOOLING) return job_state::Spooling;
    if (st & (JOB_STATUS_PRINTING | JOB_STATUS_RENDERING_LOCALLY)) return job_state::Processing;
    return job_state::Pending;
}

inline printer_info_ex win_info(const PRINTER_INFO_2W& p, const wchar_t* def) {
    printer_info_ex ex;
    ex.name = from_wide(p.pPrinterName); ex.description = from_wide(p.pComment);
    ex.location = from_wide(p.pLocation); ex.model = from_wide(p.pDriverName);
    ex.make_and_model = ex.model; ex.device_uri = from_wide(p.pPortName);
    ex.state = win_state(p.Status);
    ex.is_default = def && p.pPrinterName && std::wcscmp(def, p.pPrinterName) == 0;
    ex.is_shared = (p.Attributes & PRINTER_ATTRIBUTE_SHARED) != 0;
    ex.is_enabled = !(p.Status & PRINTER_STATUS_PAUSED);
    ex.is_accepting = !(p.Attributes & PRINTER_ATTRIBUTE_WORK_OFFLINE);
    ex.pending_jobs = p.cJobs;
    return ex;
}

inline bool get_printer2(HANDLE h, std::vector<BYTE>& buf) {
    DWORD need = 0;
    GetPrinterW(h, 2, nullptr, 0, &need);
    if (!need) return false;
    buf.assign(need, 0);
    return GetPrinterW(h, 2, buf.data(), need, &need) != FALSE;
}

inline uint64_t systime_to_unix(const SYSTEMTIME& st) {
    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return t > 116444736000000000ull ? (t - 116444736000000000ull) / 10000000ull : 0;
}

inline job_info win_job(const JOB_INFO_2W& j) {
    job_info o;
    o.id = j.JobId; o.printer = from_wide(j.pPrinterName); o.user = from_wide(j.pUserName);
    o.title = from_wide(j.pDocument); o.state_text = from_wide(j.pStatus);
    o.state = win_job_state(j.Status); o.size_bytes = j.Size; o.pages = j.TotalPages;
    o.priority = j.Priority; o.submitted_unix = systime_to_unix(j.Submitted);
    return o;
}

inline bool enum_jobs(HANDLE h, std::vector<job_info>& out) {
    DWORD need = 0, cnt = 0;
    EnumJobsW(h, 0, 0xFFFFFFFF, 2, nullptr, 0, &need, &cnt);
    if (!need) return true;
    std::vector<BYTE> buf(need);
    if (!EnumJobsW(h, 0, 0xFFFFFFFF, 2, buf.data(), need, &need, &cnt)) return false;
    auto* ji = reinterpret_cast<JOB_INFO_2W*>(buf.data());
    for (DWORD i = 0; i < cnt; ++i) out.push_back(win_job(ji[i]));
    return true;
}

// ---- device context for a print job ---------------------------------------
struct win_dc {
    HDC dc = nullptr;
    std::vector<BYTE> dm;
    win_dc() = default;
    win_dc(const win_dc&) = delete;
    win_dc& operator=(const win_dc&) = delete;
    ~win_dc() { if (dc) DeleteDC(dc); }
};

inline short dm_paper(media_size m) {
    switch (m) {
        case media_size::A3: return DMPAPER_A3;       case media_size::A4: return DMPAPER_A4;
        case media_size::A5: return DMPAPER_A5;       case media_size::A6: return 70;   // DMPAPER_A6
        case media_size::Letter: return DMPAPER_LETTER; case media_size::Legal: return DMPAPER_LEGAL;
        case media_size::Tabloid: return DMPAPER_11X17; case media_size::Executive: return DMPAPER_EXECUTIVE;
        default: return 0;
    }
}

inline bool make_dc(const knst_c16string& name, const print_options& o, win_dc& out) {
    std::wstring wn = wide(name);
    hprinter hp(name);
    if (!hp.ok()) return false;
    LONG sz = DocumentPropertiesW(nullptr, hp.h, &wn[0], nullptr, nullptr, 0);
    if (sz <= 0) return false;
    out.dm.assign(static_cast<size_t>(sz), 0);
    DEVMODEW* dm = reinterpret_cast<DEVMODEW*>(out.dm.data());
    if (DocumentPropertiesW(nullptr, hp.h, &wn[0], dm, nullptr, DM_OUT_BUFFER) != IDOK) return false;
    if (o.copies > 1) { dm->dmFields |= DM_COPIES; dm->dmCopies = static_cast<short>(std::min<uint32_t>(o.copies, 9999)); }
    if (o.collate)    { dm->dmFields |= DM_COLLATE; dm->dmCollate = DMCOLLATE_TRUE; }
    if (o.duplex != duplex_mode::Default) {
        dm->dmFields |= DM_DUPLEX;
        dm->dmDuplex = o.duplex == duplex_mode::Simplex ? DMDUP_SIMPLEX
                     : o.duplex == duplex_mode::LongEdge ? DMDUP_VERTICAL : DMDUP_HORIZONTAL;
    }
    if (o.color == color_mode::Color || o.color == color_mode::Mono || o.color == color_mode::Gray) {
        dm->dmFields |= DM_COLOR; dm->dmColor = (o.color == color_mode::Color) ? DMCOLOR_COLOR : DMCOLOR_MONOCHROME;
    }
    if (o.orient != orientation::Default) {
        dm->dmFields |= DM_ORIENTATION;
        dm->dmOrientation = (o.orient == orientation::Landscape || o.orient == orientation::ReverseLandscape)
                            ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    }
    if (o.custom_w_mm && o.custom_h_mm) {
        dm->dmFields |= DM_PAPERSIZE | DM_PAPERWIDTH | DM_PAPERLENGTH;
        dm->dmPaperSize = DMPAPER_USER;
        dm->dmPaperWidth = static_cast<short>(o.custom_w_mm * 10); dm->dmPaperLength = static_cast<short>(o.custom_h_mm * 10);
    } else if (short p = dm_paper(o.paper)) { dm->dmFields |= DM_PAPERSIZE; dm->dmPaperSize = p; }
    if (o.quality != print_quality::Default) {
        dm->dmFields |= DM_PRINTQUALITY;
        dm->dmPrintQuality = o.quality == print_quality::Draft ? DMRES_DRAFT
                           : o.quality == print_quality::Normal ? DMRES_MEDIUM : DMRES_HIGH;
    }
    if (o.ptype == paper_type::Photo || o.ptype == paper_type::Glossy || o.ptype == paper_type::Matte) {
        dm->dmFields |= DM_MEDIATYPE; dm->dmMediaType = o.ptype == paper_type::Glossy ? DMMEDIA_GLOSSY : DMMEDIA_STANDARD;
    } else if (o.ptype == paper_type::Transparency) { dm->dmFields |= DM_MEDIATYPE; dm->dmMediaType = DMMEDIA_TRANSPARENCY; }
    if (DocumentPropertiesW(nullptr, hp.h, &wn[0], dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER) != IDOK) return false;
    out.dc = CreateDCW(L"WINSPOOL", wn.c_str(), nullptr, dm);
    return out.dc != nullptr;
}

inline void dc_page_mm(HDC dc, float& w, float& h) {
    float dx = static_cast<float>(GetDeviceCaps(dc, LOGPIXELSX)), dy = static_cast<float>(GetDeviceCaps(dc, LOGPIXELSY));
    if (dx < 1) dx = 300; if (dy < 1) dy = 300;
    w = GetDeviceCaps(dc, PHYSICALWIDTH) * 25.4f / dx;
    h = GetDeviceCaps(dc, PHYSICALHEIGHT) * 25.4f / dy;
}

inline void append_doc(document& dst, const document& src) {
    size_t off = dst.images.size();
    if (dst.pages.empty()) { dst.w_pt = src.w_pt; dst.h_pt = src.h_pt; dst.max_dpi = src.max_dpi; }
    dst.images.insert(dst.images.end(), src.images.begin(), src.images.end());
    for (auto p : src.pages) {
        for (auto& it : p.items) if (it.kind == item::Image) it.image += static_cast<uint32_t>(off);
        dst.pages.push_back(std::move(p));
    }
}

// Renders the document model through GDI as ONE spooler job; returns the job id.
inline knst_prn::status render_document(HDC dc, const document& d, const knst_c16string& title, uint32_t& job) {
    const int dpx = GetDeviceCaps(dc, LOGPIXELSX), dpy = GetDeviceCaps(dc, LOGPIXELSY);
    const int offx = GetDeviceCaps(dc, PHYSICALOFFSETX), offy = GetDeviceCaps(dc, PHYSICALOFFSETY);
    auto X = [&](float pt) { return static_cast<int>(std::lround(pt * dpx / 72.0f)) - offx; };
    auto Y = [&](float pt) { return static_cast<int>(std::lround(pt * dpy / 72.0f)) - offy; };

    std::wstring wt = title.empty() ? std::wstring(L"KNST document") : wide(title);
    DOCINFOW di{}; di.cbSize = sizeof(di); di.lpszDocName = wt.c_str();
    int jid = StartDocW(dc, &di);
    if (jid <= 0) return status::IoError;

    int cache_idx = -1; knst_byte_string cache_rgb; uint32_t cache_w = 0, cache_h = 0;
    for (auto& pg : d.pages) {
        if (StartPage(dc) <= 0) { AbortDoc(dc); return status::IoError; }
        SetBkMode(dc, TRANSPARENT);
        SetTextAlign(dc, TA_LEFT | TA_BASELINE);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        for (auto& it : pg.items) {
            switch (it.kind) {
            case item::Text: {
                if (it.text.empty()) break;
                const wchar_t* face = it.fam == font_family::Courier ? L"Courier New"
                                    : it.fam == font_family::Times ? L"Times New Roman" : L"Arial";
                HFONT f = CreateFontW(-static_cast<int>(std::lround(it.size * dpy / 72.0f)), 0, 0, 0,
                                      it.bold ? FW_BOLD : FW_NORMAL, it.italic ? TRUE : FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, face);
                HGDIOBJ old = SelectObject(dc, f);
                SetTextColor(dc, RGB(it.r, it.g, it.b));
                TextOutW(dc, X(it.x1), Y(it.y1), reinterpret_cast<const wchar_t*>(it.text.data()), static_cast<int>(it.text.size()));
                SelectObject(dc, old); DeleteObject(f);
                break; }
            case item::Line: {
                HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>(std::lround(it.lw * dpx / 72.0f))), RGB(it.r, it.g, it.b));
                HGDIOBJ old = SelectObject(dc, pen);
                MoveToEx(dc, X(it.x1), Y(it.y1), nullptr); LineTo(dc, X(it.x2), Y(it.y2));
                SelectObject(dc, old); DeleteObject(pen);
                break; }
            case item::Rect: {
                HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>(std::lround(it.lw * dpx / 72.0f))), RGB(it.r, it.g, it.b));
                HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, X(std::min(it.x1, it.x2)), Y(std::min(it.y1, it.y2)), X(std::max(it.x1, it.x2)), Y(std::max(it.y1, it.y2)));
                SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(pen);
                break; }
            case item::FillRect: {
                HBRUSH br = CreateSolidBrush(RGB(it.r, it.g, it.b));
                RECT rc = { X(std::min(it.x1, it.x2)), Y(std::min(it.y1, it.y2)), X(std::max(it.x1, it.x2)), Y(std::max(it.y1, it.y2)) };
                FillRect(dc, &rc, br); DeleteObject(br);
                break; }
            case item::Image: {
                if (it.image >= d.images.size()) break;
                if (cache_idx != static_cast<int>(it.image)) {
                    knst_file_error fe = knst_file_error::None;
                    knst_byte_string bytes = knst_file::read_file_data<>(d.images[it.image].path, &fe);
                    if (fe != knst_file_error::None || !decode_rgb(bytes, cache_rgb, cache_w, cache_h)) {
                        // Skip this image only; aborting the whole doc would
                        // lose all other photos in the batch.
                        cache_idx = static_cast<int>(it.image);
                        cache_w = cache_h = 0;
                    } else {
                        cache_idx = static_cast<int>(it.image);
                    }
                }
                if (cache_w == 0 || cache_h == 0) break;  // failed image → skip
                const int dx = X(it.x1), dy = Y(it.y1), dw = X(it.x2) - X(it.x1), dh = Y(it.y2) - Y(it.y1);
                if (dw <= 0 || dh <= 0) break;
                knst_byte_string disp; uint32_t pw, ph;
                transform_rgb(cache_rgb, cache_w, cache_h, it.flip, it.rot, disp, pw, ph);
                double need_w = (it.x2 - it.x1) * d.max_dpi / 72.0;
                if (pw > need_w && need_w >= 1.0) {
                    uint32_t nw = static_cast<uint32_t>(std::ceil(need_w));
                    uint32_t nh = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(static_cast<double>(ph) * nw / pw)));
                    knst_byte_string small_buf; downscale_rgb(disp, pw, ph, nw, nh, small_buf);
                    disp = small_buf; pw = nw; ph = nh;
                }
                const size_t stride = (static_cast<size_t>(pw) * 3 + 3) & ~static_cast<size_t>(3);
                std::vector<BYTE> bgr(stride * ph, 0);
                const uint8_t* s = disp.data();
                for (uint32_t y = 0; y < ph; ++y) {
                    BYTE* o = bgr.data() + y * stride;
                    for (uint32_t x = 0; x < pw; ++x, s += 3) { o[x * 3] = s[2]; o[x * 3 + 1] = s[1]; o[x * 3 + 2] = s[0]; }
                }
                BITMAPINFO bmi{}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = static_cast<LONG>(pw); bmi.bmiHeader.biHeight = -static_cast<LONG>(ph);
                bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 24; bmi.bmiHeader.biCompression = BI_RGB;
                int saved = 0;
                if (it.clip) { saved = SaveDC(dc); IntersectClipRect(dc, X(it.cx1), Y(it.cy1), X(it.cx2), Y(it.cy2)); }
                StretchDIBits(dc, dx, dy, dw, dh, 0, 0, static_cast<int>(pw), static_cast<int>(ph), bgr.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
                if (it.clip) RestoreDC(dc, saved);
                break; }
            }
        }
        if (EndPage(dc) <= 0) { AbortDoc(dc); return status::IoError; }
    }
    if (EndDoc(dc) <= 0) return status::IoError;
    job = static_cast<uint32_t>(jid);
    return status::Ok;
}

} // namespace detail

// ===========================================================================
//                           Printer class - Windows
// ===========================================================================
class printer {
public:
    static result<printer> open(const knst_c16string& name) {
        using R = result<printer>;
        if (name.empty() || name.length() > 220) return R::fail(status::InvalidArgument);
        detail::hprinter hp(name);
        if (!hp.ok()) return R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
        printer p; p.name_ = name;
        return R::success(std::move(p));
    }
    static result<printer> open_default() {
        auto n = get_default_name();
        if (!n) return result<printer>::fail(n.st, n.native);
        return open(n.value);
    }
    static result<printer> open_any() {
        auto d = open_default();
        if (d) return d;
        auto all = list_all();
        if (!all) return result<printer>::fail(all.st, all.native);
        for (auto& p : all.value) if (p.is_enabled && p.is_accepting) return open(p.name);
        if (!all.value.empty()) return open(all.value[0].name);
        return result<printer>::fail(status::NotFound);
    }
    const knst_c16string& name() const noexcept { return name_; }

    result<queue_status> status() const {
        using R = result<queue_status>;
        detail::hprinter hp(name_); std::vector<BYTE> b;
        if (!hp.ok() || !detail::get_printer2(hp.h, b)) return R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
        auto* p = reinterpret_cast<PRINTER_INFO_2W*>(b.data());
        R r;
        r.value.enabled = !(p->Status & PRINTER_STATUS_PAUSED);
        r.value.accepting = !(p->Attributes & PRINTER_ATTRIBUTE_WORK_OFFLINE);
        r.value.job_count = p->cJobs; r.value.state = detail::win_state(p->Status);
        return r;
    }
    result<printer_info_ex> info() const {
        using R = result<printer_info_ex>;
        detail::hprinter hp(name_); std::vector<BYTE> b;
        if (!hp.ok() || !detail::get_printer2(hp.h, b)) return R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
        wchar_t def[256]; DWORD dl = 256; bool has = GetDefaultPrinterW(def, &dl) != FALSE;
        return R::success(detail::win_info(*reinterpret_cast<PRINTER_INFO_2W*>(b.data()), has ? def : nullptr));
    }
    result<knst_c16string> make_and_model() const {
        auto i = info();
        if (!i) return result<knst_c16string>::fail(i.st, i.native);
        return result<knst_c16string>::success(i.value.make_and_model);
    }

    result<printer_capabilities> capabilities() const {
        using R = result<printer_capabilities>;
        std::wstring n = detail::wide(name_);
        auto cap = [&](WORD c, void* out) { return DeviceCapabilitiesW(n.c_str(), nullptr, c, static_cast<LPWSTR>(out), nullptr); };
        R r;
        if (cap(DC_COLORDEVICE, nullptr) < 0 && GetLastError() != 0 && cap(DC_DUPLEX, nullptr) < 0) return R::fail(status::NotFound);
        r.value.color = cap(DC_COLORDEVICE, nullptr) == 1;
        r.value.duplex_long_edge = r.value.duplex_short_edge = cap(DC_DUPLEX, nullptr) == 1;
        r.value.stapling = cap(DC_STAPLE, nullptr) == 1;
        int np = cap(DC_PAPERNAMES, nullptr);
        if (np > 0) { std::vector<wchar_t> b(static_cast<size_t>(np) * 64); cap(DC_PAPERNAMES, b.data());
                      for (int i = 0; i < np; ++i) r.value.supported_media.push_back(detail::from_wide(&b[static_cast<size_t>(i) * 64])); }
        int nb = cap(DC_BINNAMES, nullptr);
        if (nb > 0) { std::vector<wchar_t> b(static_cast<size_t>(nb) * 24); cap(DC_BINNAMES, b.data());
                      for (int i = 0; i < nb; ++i) r.value.supported_sources.push_back(detail::from_wide(&b[static_cast<size_t>(i) * 24])); }
        int nr = cap(DC_ENUMRESOLUTIONS, nullptr);
        if (nr > 0) { std::vector<LONG> b(static_cast<size_t>(nr) * 2); cap(DC_ENUMRESOLUTIONS, b.data());
                      for (int i = 0; i < nr; ++i) { std::string s = std::to_string(b[i * 2]); if (b[i * 2] != b[i * 2 + 1]) s += "x" + std::to_string(b[i * 2 + 1]); s += "dpi";
                                                      r.value.supported_resolutions.push_back(detail::from_u8(s)); } }
        return r;
    }
    result<bool> supports_duplex(duplex_mode m) const {
        auto c = capabilities();
        if (!c) return result<bool>::fail(c.st, c.native);
        return result<bool>::success(m == duplex_mode::Default || m == duplex_mode::Simplex || c.value.duplex_long_edge);
    }
    result<bool> supports_media(media_size m) const {
        std::wstring n = detail::wide(name_);
        short id = detail::dm_paper(m);
        if (!id) return result<bool>::success(true);
        int cnt = DeviceCapabilitiesW(n.c_str(), nullptr, DC_PAPERS, nullptr, nullptr);
        if (cnt <= 0) return result<bool>::fail(status::NotSupported);
        std::vector<WORD> v(static_cast<size_t>(cnt));
        DeviceCapabilitiesW(n.c_str(), nullptr, DC_PAPERS, reinterpret_cast<LPWSTR>(v.data()), nullptr);
        return result<bool>::success(std::find(v.begin(), v.end(), static_cast<WORD>(id)) != v.end());
    }
    result<knst_vector<supply_level>> supply_levels() const { return result<knst_vector<supply_level>>::fail(status::NotSupported); }
    result<knst_vector<printer_alert>> alerts() const {
        using R = result<knst_vector<printer_alert>>;
        auto s = status();
        if (!s) return R::fail(s.st, s.native);
        R r;
        const char16_t* txt = nullptr; uint32_t sev = 2;
        switch (s.value.state) {
            case printer_state::PaperJam: txt = u"paper jam"; break;   case printer_state::PaperOut: txt = u"out of paper"; break;
            case printer_state::DoorOpen: txt = u"door open"; break;   case printer_state::Offline: txt = u"offline"; break;
            case printer_state::Error: txt = u"error"; break;         case printer_state::TonerLow: txt = u"toner low"; sev = 1; break;
            default: break;
        }
        if (txt) { printer_alert a; a.severity = sev; a.code = txt; a.text = txt; r.value.push_back(a); }
        return r;
    }
    result<usage_stats> usage() const {
        result<usage_stats> r;
        auto p = jobs(false);
        if (p) for (auto& j : p.value) { r.value.pending_jobs++; r.value.total_jobs++; r.value.total_bytes += j.size_bytes; r.value.total_pages += j.pages; }
        return r;
    }

    // ---- printing (GDI) ---------------------------------------------------
    // Raw bytes → spooler.
    // If the bytes are a known document format (PDF/PS/JPEG/PNG/...),
    // they go through the spooler's driver chain (CUPS-equivalent), NOT raw.
    // Otherwise they go via RAW datatype (printer-language bytes).
    result<uint32_t> print_data(const unsigned char* d, uint32_t len, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (!d || !len) return R::fail(status::InvalidArgument);
        auto is = [&](const char* m, uint32_t n) { return len >= n && std::memcmp(d, m, n) == 0; };
        const char16_t* ext = nullptr;
        if      (is("%PDF-", 5))       ext = u".pdf";
        else if (is("%!", 2))          ext = u".ps";
        else if (is("\xFF\xD8\xFF", 3)) ext = u".jpg";
        else if (is("\x89PNG", 4))     ext = u".png";
        else if (is("GIF8", 4))        ext = u".gif";
        else if (is("BM", 2))          ext = u".bmp";
        else if (is("P6", 2) || is("P5", 2)) ext = u".pnm";
        else if (is("II*\0", 4) || is("MM\0*", 4)) ext = u".tif";
        else if (is("RIFF", 4) && len >= 12 && std::memcmp(d + 8, "WEBP", 4) == 0) ext = u".webp";

        if (ext) {
            detail::temp_set tmp;
            knst_c16string t = tmp.add(ext);
            if (!knst_file::write_file_atomic(t, d, len)) return R::fail(status::IoError);
            auto r = print_file(t, o);
            // shell_print returns success(id=0) on 15s timeout — the PDF reader
            // may still be loading the file asynchronously. Keep the temp file
            // alive and let cleanup_stale_temp_files() remove it later.
            if (r && r.value == 0) {
                tmp.paths.clear();                // keep t
                cleanup_stale_temp_files();       // sweep >1h old files
            }
            return r;
        }
        return send_raw(knst_byte_string(d, len));
    }
    result<uint32_t> print_text(const knst_c16string& text, const print_options& o = print_options()) {
        if (text.empty()) return result<uint32_t>::fail(status::InvalidArgument);
        return print_doc(o, [&](float w, float h, detail::document& d) { detail::layout_plain_text(text, w, h, d); return status::Ok; });
    }
    result<uint32_t> print_rich(const rich_document& rd, const print_options& o = print_options()) {
        if (rd.elements.empty() && rd.shapes.empty()) return result<uint32_t>::fail(status::InvalidArgument);
        return print_doc(o, [&](float, float, detail::document& d) { detail::layout_rich(rd, d); return status::Ok; });
    }
    result<uint32_t> print_image(const knst_c16string& path, const print_options& o = print_options()) {
        image_batch_options bo;
        float m = static_cast<float>(std::max(std::max(o.margin_left_mm, o.margin_right_mm), std::max(o.margin_top_mm, o.margin_bottom_mm)));
        bo.margin_mm = m > 0 ? m : (o.borderless ? 0.0f : 5.0f);
        bo.fit = o.borderless ? fit_mode::Fill : fit_mode::Fit;
        std::vector<knst_c16string> one; one.push_back(path);
        return print_images(one, bo, o);
    }
    // MANY pictures -> ONE spooler job (printed back to back).
    result<uint32_t> print_images(const knst_vector<knst_c16string>& paths, const image_batch_options& bo = image_batch_options(),
                                  const print_options& o = print_options()) {
        std::vector<knst_c16string> v; for (uint32_t i = 0; i < paths.size(); ++i) v.push_back(paths[i]);
        return print_images(v, bo, o);
    }
    result<uint32_t> print_images(const std::vector<knst_c16string>& paths, const image_batch_options& bo, const print_options& o) {
        if (paths.empty()) return result<uint32_t>::fail(status::InvalidArgument);
        return print_doc(o, [&](float w, float h, detail::document& d) { return detail::layout_images(paths, bo, w, h, d); });
    }
    // Photos + text files -> ONE job (order kept).  PDFs / other files are sent afterwards as own jobs.
    result<uint32_t> print_batch(const knst_vector<knst_c16string>& paths, const print_options& o = print_options(),
                                 const image_batch_options& bo = image_batch_options()) {
        using R = result<uint32_t>;
        if (paths.empty()) return R::fail(status::InvalidArgument);
        std::vector<knst_c16string> others;
        std::vector<std::pair<bool, knst_c16string>> seq;       // true = image
        for (uint32_t i = 0; i < paths.size(); ++i) {
            auto k = detail::detect_kind(paths[i]);
            if (k == detail::file_kind::Image) seq.emplace_back(true, paths[i]);
            else if (k == detail::file_kind::Text) seq.emplace_back(false, paths[i]);
            else others.push_back(paths[i]);
        }
        uint32_t first = 0;
        if (!seq.empty()) {
            auto r = print_doc(o, [&](float w, float h, detail::document& all) {
                size_t i = 0;
                while (i < seq.size()) {
                    detail::document part;
                    if (seq[i].first) {
                        std::vector<knst_c16string> run;
                        while (i < seq.size() && seq[i].first) run.push_back(seq[i++].second);
                        knst_prn::status st = detail::layout_images(run, bo, w, h, part);
                        if (st != status::Ok) return st;
                    } else {
                        detail::layout_plain_text(knst_file::read_file_text(knst_file::resolve_path(seq[i++].second)), w, h, part);
                    }
                    detail::append_doc(all, part);
                }
                return status::Ok;
            });
            if (!r) return r;
            first = r.value;
        }
        for (auto& p : others) { auto r = print_file(p, o); if (!r) return r; if (!first) first = r.value; }
        return R::success(first);
    }
    // ⚠️ On Windows, PDF printing via winspool requires a PDF reader
    // registered for "printto" (Acrobat, SumatraPDF, Foxit, Edge...).
    // Without one, shell_print returns NotSupported (SE_ERR_NOASSOC).
    // Use print_universal(path, "printer_host:631") to bypass this via IPP.
    result<uint32_t> print_pdf(const knst_c16string& path, const print_options& o = print_options()) {
        return print_file(path, o);
    }

    // Raw printer-language files are sent as RAW, text/images are rendered, everything else uses the
    // application registered for "printto" (PDF needs a reader that supports it, e.g. Acrobat/SumatraPDF).
    result<uint32_t> print_file(const knst_c16string& path, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        switch (detail::detect_kind(abs)) {
            case detail::file_kind::Text:  return print_text(knst_file::read_file_text(abs), o);
            case detail::file_kind::Image: return print_image(abs, o);
            case detail::file_kind::PostScript: case detail::file_kind::Pcl:
            case detail::file_kind::Zpl: case detail::file_kind::EscPos: {
                knst_file_error fe = knst_file_error::None;
                knst_byte_string b = knst_file::read_file_data<>(abs, &fe);
                if (fe != knst_file_error::None || b.empty()) return R::fail(status::IoError);
                return send_raw(b, o.title.empty() ? knst_c16string(u"knst file") : o.title);
            }
            default: return shell_print(abs);
        }
    }
    result<uint32_t> print_auto(const knst_c16string& path, const print_options& o = print_options()) { return print_file(path, o); }

    result<uint32_t> send_raw(const knst_byte_string& data, const knst_c16string& title = u"knst raw job") {
        using R = result<uint32_t>;
        if (data.empty()) return R::fail(status::InvalidArgument);
        detail::hprinter hp(name_);
        if (!hp.ok()) return R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
        std::wstring dn = title.empty() ? std::wstring(L"knst raw job") : detail::wide(title);
        DOC_INFO_1W di{}; di.pDocName = &dn[0]; di.pDatatype = const_cast<LPWSTR>(L"RAW");
        DWORD id = StartDocPrinterW(hp.h, 1, reinterpret_cast<LPBYTE>(&di));
        if (!id) return R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
        bool ok = StartPagePrinter(hp.h) != FALSE;
        const unsigned char* p = data.data(); size_t left = data.length();
        while (ok && left) {
            DWORD chunk = static_cast<DWORD>(std::min<size_t>(left, 1u << 16)), wr = 0;
            ok = WritePrinter(hp.h, const_cast<unsigned char*>(p), chunk, &wr) && wr > 0;
            p += wr; left -= wr;
        }
        if (ok) {
            EndPagePrinter(hp.h);
            EndDocPrinter(hp.h);
            return R::success(static_cast<uint32_t>(id));
        }
        // WritePrinter failed mid-stream — abort to discard the partial doc
        // instead of committing garbage to the spool.
        AbortPrinter(hp.h);
        return R::fail(status::IoError, static_cast<int>(GetLastError()));
    }
    result<uint32_t> send_escpos(const knst_byte_string& d) { return send_raw(d, u"knst ESC/POS"); }
    result<uint32_t> send_zpl(const knst_byte_string& d)    { return send_raw(d, u"knst ZPL"); }
        result<uint32_t> send_pcl(const knst_byte_string& d)    { return send_raw(d, u"knst PCL"); }

    // ---- Network raw (TCP 9100) — ESC/POS, PCL, ZPL over network ----------
    // Bypasses winspool entirely. For thermal / label printers not installed
    // in the local spooler.
    result<uint32_t> print_network(const knst_byte_string& data, const knst_c16string& host,
                                   uint16_t port = 9100, uint32_t timeout_ms = 5000) {
        using R = result<uint32_t>;
        if (data.empty() || host.empty()) return R::fail(status::InvalidArgument);
        if (timeout_ms < 100) timeout_ms = 100;

        WSADATA wsa;
        static bool wsa_ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
        if (!wsa_ok) return R::fail(status::IoError);

        std::string host_str = detail::to_u8(host);
        if (host_str.empty()) return R::fail(status::InvalidArgument);

        struct addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        std::string port_str = std::to_string(port ? port : 9100);
        struct addrinfo* res = nullptr;
        if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res) != 0 || !res)
            return R::fail(status::NotFound);

        SOCKET sock = INVALID_SOCKET;
        for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
            SOCKET s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (s == INVALID_SOCKET) continue;
            u_long nb = 1;
            ::ioctlsocket(s, FIONBIO, &nb);
            int rc = ::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
            bool ok_conn = (rc == 0);
            if (!ok_conn) {
                int e = ::WSAGetLastError();
                if (e == WSAEWOULDBLOCK) {
                    WSAPOLLFD pfd{}; pfd.fd = s; pfd.events = POLLWRNORM;
                    int pr = ::WSAPoll(&pfd, 1, static_cast<INT>(timeout_ms));
                    if (pr > 0) {
                        int so = 0, sl = sizeof(so);
                        ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so), &sl);
                        ok_conn = (so == 0);
                    }
                }
            }
            if (!ok_conn) { ::closesocket(s); continue; }
            nb = 0;
            ::ioctlsocket(s, FIONBIO, &nb);
            DWORD tv = timeout_ms;
            ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
            sock = s;
            break;
        }
        ::freeaddrinfo(res);
        if (sock == INVALID_SOCKET) return R::fail(status::IoError);

        const char* p = reinterpret_cast<const char*>(data.data());
        size_t left = data.length();
        bool ok = true;
        while (left > 0) {
            int n = ::send(sock, p, static_cast<int>(left), 0);
            if (n <= 0) { ok = false; break; }
            p += n;
            left -= static_cast<size_t>(n);
        }
        ::closesocket(sock);
        return ok ? R::success(0) : R::fail(status::IoError);
    }

    result<uint32_t> print_secure(const knst_c16string&, const secure_print_options&, const print_options& = print_options()) { return result<uint32_t>::fail(status::NotSupported); }
    result<uint32_t> print_held(const knst_c16string&, const knst_c16string&, const print_options& = print_options()) { return result<uint32_t>::fail(status::NotSupported); }

    result<uint32_t> print_test_page(test_page_type type = test_page_type::Diagnostic, const print_options& o = print_options()) {
        print_options oo = o; if (oo.title.empty()) oo.title = u"knst test page";
        oo.copies = 1;
        std::string nm = detail::to_u8(name_);
        return print_doc(oo, [&](float w, float h, detail::document& d) { detail::make_test_doc(type, nm, w, h, d); return status::Ok; });
    }

    result<knst_vector<uint32_t>> print_files(const knst_vector<knst_c16string>& paths, const print_options& o = print_options(), bool cont = false) {
        using R = result<knst_vector<uint32_t>>;
        R r;
        for (uint32_t i = 0; i < paths.size(); ++i) {
            auto one = print_auto(paths[i], o);
            if (one) { r.value.push_back(one.value); continue; }
            if (!cont) { if (r.value.empty()) return R::fail(one.st, one.native); break; }
        }
        if (r.value.empty() && paths.size() > 0) return R::fail(status::IoError);
        return r;
    }
    static result<knst_vector<uint32_t>> print_to_multiple(const knst_vector<knst_c16string>& names, const knst_c16string& path,
                                                           const print_options& o = print_options()) {
        using R = result<knst_vector<uint32_t>>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        R r; knst_prn::status last = status::NotFound; int nat = 0;
        for (uint32_t i = 0; i < names.size(); ++i) {
            auto pr = open(names[i]);
            if (!pr) { last = pr.st; nat = pr.native; continue; }
            auto one = pr.value.print_auto(path, o);
            if (one) r.value.push_back(one.value); else { last = one.st; nat = one.native; }
        }
        if (r.value.empty()) return R::fail(last, nat);
        return r;
    }

    // ---- jobs -------------------------------------------------------------
    result<knst_vector<job_info>> jobs(bool = false) const {
        using R = result<knst_vector<job_info>>;
        detail::hprinter hp(name_); std::vector<job_info> v;
        if (!hp.ok() || !detail::enum_jobs(hp.h, v)) return R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
        R r; for (auto& j : v) r.value.push_back(std::move(j));
        return r;
    }
    result<job_info> job_details(uint32_t id) const {
        using R = result<job_info>;
        if (!id) return R::fail(status::InvalidArgument);
        detail::hprinter hp(name_); DWORD need = 0;
        if (!hp.ok()) return R::fail(detail::win_status(GetLastError()));
        GetJobW(hp.h, id, 2, nullptr, 0, &need);
        if (!need) return R::fail(status::NotFound);
        std::vector<BYTE> b(need);
        if (!GetJobW(hp.h, id, 2, b.data(), need, &need)) return R::fail(status::NotFound, static_cast<int>(GetLastError()));
        return R::success(detail::win_job(*reinterpret_cast<JOB_INFO_2W*>(b.data())));
    }
    result<knst_vector<job_info>> jobs_by_user(const knst_c16string& user, bool = false) const {
        using R = result<knst_vector<job_info>>;
        auto all = jobs(false);
        if (!all) return R::fail(all.st, all.native);
        R r; for (auto& j : all.value) if (j.user == user) r.value.push_back(j);
        return r;
    }
    result<knst_vector<job_info>> completed_jobs(uint32_t = 20) const { return result<knst_vector<job_info>>::success(knst_vector<job_info>()); }  // Windows does not keep them
    result<void> cancel_job(uint32_t id) { return job_cmd(id, JOB_CONTROL_DELETE); }
    result<void> cancel_all() { return all_jobs(JOB_CONTROL_DELETE, nullptr); }
    result<void> cancel_by_user(const knst_c16string& u) { return all_jobs(JOB_CONTROL_DELETE, &u); }
    result<void> hold_job(uint32_t id)    { return job_cmd(id, JOB_CONTROL_PAUSE); }
    result<void> release_job(uint32_t id) { return job_cmd(id, JOB_CONTROL_RESUME); }
    result<void> hold_all()    { return all_jobs(JOB_CONTROL_PAUSE, nullptr); }
    result<void> release_all() { return all_jobs(JOB_CONTROL_RESUME, nullptr); }
    result<void> set_job_priority(uint32_t id, uint32_t prio) {
        using R = result<void>;
        if (!id || !prio || prio > 100) return R::fail(status::InvalidArgument);
        detail::hprinter hp(name_); DWORD need = 0;
        if (!hp.ok()) return R::fail(detail::win_status(GetLastError()));
        GetJobW(hp.h, id, 2, nullptr, 0, &need);
        if (!need) return R::fail(status::NotFound);
        std::vector<BYTE> b(need);
        if (!GetJobW(hp.h, id, 2, b.data(), need, &need)) return R::fail(status::NotFound);
        auto* j = reinterpret_cast<JOB_INFO_2W*>(b.data());
        j->Priority = std::min<DWORD>(99, prio); j->pSecurityDescriptor = nullptr;
        return SetJobW(hp.h, id, 2, b.data(), 0) ? R::success() : R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
    }
    result<void> move_job(uint32_t, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> move_all_jobs(const knst_c16string&) { return result<void>::fail(status::NotSupported); }

    result<void> wait_for_job(uint32_t id, uint32_t timeout_ms = 60000) {
        using R = result<void>;
        if (!id) return R::fail(status::InvalidArgument);
        const auto t0 = std::chrono::steady_clock::now();
        bool deleting = false;
        for (;;) {
            auto d = job_details(id);
            if (!d) { if (d.st == status::NotFound) return deleting ? R::fail(status::BadState) : R::success(); }
            else if (d.value.state == job_state::Canceled || d.value.state == job_state::Aborted) deleting = true;
            if (elapsed_ms(t0) >= timeout_ms) return R::fail(status::Timeout);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    }
    result<void> wait_for_ready(uint32_t timeout_ms = 30000) {
        const auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            auto s = status();
            if (s && s.value.enabled && s.value.accepting && s.value.state != printer_state::Offline &&
                s.value.state != printer_state::Error) return result<void>::success();
            if (elapsed_ms(t0) >= timeout_ms) return result<void>::fail(status::Timeout);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    }
    result<bool> is_job_done(uint32_t id) const {
        if (!id) return result<bool>::fail(status::InvalidArgument);
        auto d = job_details(id);
        if (!d) return d.st == status::NotFound ? result<bool>::success(true) : result<bool>::fail(d.st, d.native);
        return result<bool>::success(false);
    }
    result<uint32_t> reprint_job(uint32_t id) {
        if (!id) return result<uint32_t>::fail(status::InvalidArgument);
        knst_c16string c = detail::cache_find(name_, id);
        if (c.empty()) return result<uint32_t>::fail(status::NotFound);
        return print_file(c);
    }

    // ---- administration ---------------------------------------------------
    result<void> pause()  { return printer_ctl(PRINTER_CONTROL_PAUSE); }
    result<void> resume() { return printer_ctl(PRINTER_CONTROL_RESUME); }
    result<void> reject_jobs() { return result<void>::fail(status::NotSupported); }
    result<void> accept_jobs() { return result<void>::fail(status::NotSupported); }
    result<void> set_default() {
        std::wstring w = detail::wide(name_);
        return SetDefaultPrinterW(w.c_str()) ? result<void>::success() : result<void>::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
    }
    result<knst_vector<printer_option>> options() const { return result<knst_vector<printer_option>>::fail(status::NotSupported); }
    result<void> set_default_option(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_option_permanent(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> remove_option(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_description(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_location(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_sharing(bool) { return result<void>::fail(status::NotSupported); }

    static result<knst_c16string> get_default_name() {
        wchar_t b[256]; DWORD n = 256;
        if (!GetDefaultPrinterW(b, &n) || !b[0]) return result<knst_c16string>::fail(status::NotFound);
        return result<knst_c16string>::success(detail::from_wide(b));
    }
    static result<knst_vector<printer_info_ex>> list_all() {
        using R = result<knst_vector<printer_info_ex>>;
        const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
        DWORD need = 0, cnt = 0;
        EnumPrintersW(flags, nullptr, 2, nullptr, 0, &need, &cnt);
        R r;
        if (!need) return r;
        std::vector<BYTE> b(need);
        if (!EnumPrintersW(flags, nullptr, 2, b.data(), need, &need, &cnt)) return R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
        wchar_t def[256]; DWORD dl = 256; bool has = GetDefaultPrinterW(def, &dl) != FALSE;
        auto* pi = reinterpret_cast<PRINTER_INFO_2W*>(b.data());
        for (DWORD i = 0; i < cnt; ++i) r.value.push_back(detail::win_info(pi[i], has ? def : nullptr));
        return r;
    }
    static result<knst_vector<device_info>> list_available_devices() { return result<knst_vector<device_info>>::fail(status::NotSupported); }
    static result<knst_vector<driver_info>> list_drivers() { return result<knst_vector<driver_info>>::fail(status::NotSupported); }
    static result<server_info> server_status() {
        result<server_info> r;
        auto a = list_all();
        r.value.running = static_cast<bool>(a);
        wchar_t h[MAX_COMPUTERNAME_LENGTH + 1]; DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
        if (GetComputerNameW(h, &n)) r.value.hostname = detail::from_wide(h);
        if (a) r.value.total_printers = static_cast<uint32_t>(a.value.size());
        auto d = get_default_name(); if (d) r.value.default_printer = d.value;
        return r;
    }
    static result<bool> exists(const knst_c16string& name) {
        if (name.empty()) return result<bool>::fail(status::InvalidArgument);
        detail::hprinter hp(name);
        return result<bool>::success(hp.ok());
    }
    static result<uint32_t> pdf_page_count(const knst_c16string& path) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_file_error fe = knst_file_error::None;
        knst_byte_string b = knst_file::read_file_data<>(knst_file::resolve_path(path), &fe);
        if (fe != knst_file_error::None) return R::fail(status::IoError);
        uint32_t n = detail::count_pdf_pages_scan(b);
        return n ? R::success(n) : R::fail(status::NotSupported);
    }
    static result<knst_byte_string> render_preview(const knst_c16string&, uint32_t = 1, uint32_t = 100) { return result<knst_byte_string>::fail(status::NotSupported); }
    static result<void> add_printer(const knst_c16string&, const knst_c16string&, const knst_c16string&, const knst_c16string& = knst_c16string(), const knst_c16string& = knst_c16string()) { return result<void>::fail(status::NotSupported); }
    static result<void> add_ipp_printer(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> remove_printer(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<knst_vector<printer_class>> list_classes() { return result<knst_vector<printer_class>>::fail(status::NotSupported); }
    static result<void> create_class(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> delete_class(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> add_to_class(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> remove_from_class(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> set_cache(bool enabled, const knst_c16string& dir = knst_c16string(), uint32_t max_mb = 500) { return detail::cache_set(enabled, dir, max_mb); }
        // Removes temp files >1h old left by print_data(PDF) async shell_print.
    // Called after every print_data(PDF) that timed out (job id 0).
    static void cleanup_stale_temp_files() noexcept {
        knst_c16string dir = knst_file::get_temp_dir();
        if (dir.empty()) return;
        knst_vector<knst_file> entries;
        if (!knst_file::list_directory(dir, entries)) return;
        auto now = std::chrono::system_clock::now();
        for (auto& f : entries) {
            if (!f.is_file) continue;
            std::string nm = detail::to_u8(f.name);
            if (nm.compare(0, 10, "knst_print") != 0) continue;
            std::string e = detail::path_ext(f.name);
            if (e != ".pdf" && e != ".ps" && e != ".jpg" && e != ".png" &&
                e != ".gif" && e != ".bmp" && e != ".pnm" && e != ".tif" && e != ".webp")
                continue;
            auto age = std::chrono::duration_cast<std::chrono::hours>(
                now - std::chrono::system_clock::from_time_t((std::time_t)f.modified_time));
            if (age.count() >= 1) knst_file::remove_file(f.path);
        }
    }
    static result<void> clear_cache() { return detail::cache_clear(); }
    static uint64_t cache_size_bytes() { return detail::cache_size(); }

    // ─── Direct IPP (bypasses winspool) — works with ipps:// TLS ─────
    // Bypasses Windows' "PDF needs a reader" limitation: PDF goes
    // straight to the printer via IPP, no Acrobat/SumatraPDF needed.
#if KNST_PRN_HAS_NETWORK
    static result<uint32_t> print_ipp(const knst_c16string& pdf_path,
                                       const knst_c16string& host_port,
                                       uint16_t port = 631,
                                       const print_options& opts = print_options(),
                                       const knst_c16string& doc_format = u"application/pdf") {
        return detail::ipp::print_pdf(pdf_path, host_port, port, opts, doc_format);
    }
    static result<uint32_t> print_jpeg_ipp(const knst_c16string& pdf_path,
                                            const knst_c16string& host_port,
                                            uint16_t = 631,
                                            const print_options& opts = print_options()) {
        detail::ipp::send_policy p; p.prefer = "jpeg";
        return print_ipp_smart(pdf_path, host_port, opts, p);
    }
#else
    static result<uint32_t> print_ipp(const knst_c16string&, const knst_c16string&,
                                       uint16_t = 631, const print_options& = print_options(),
                                       const knst_c16string& = u"application/pdf") {
        return result<uint32_t>::fail(status::NotSupported);
    }
    static result<uint32_t> print_jpeg_ipp(const knst_c16string&, const knst_c16string&,
                                            uint16_t = 631, const print_options& = print_options()) {
        return result<uint32_t>::fail(status::NotSupported);
    }
#endif

#if KNST_PRN_HAS_NETWORK
    // ─── Direct IPP job control (bypasses winspool) ───
    // For printers on the network that were never added to the local spooler.
    static result<void> hold_job_ipp(const knst_c16string& host_port, uint32_t job_id) {
        std::string hpu; bool use_tls = false; uint16_t port = 631;
        if (!detail::ipp::split_uri(detail::to_u8(host_port), hpu, use_tls, port))
            return result<void>::fail(status::InvalidArgument);
        return detail::ipp::set_job_attrs(hpu, use_tls, job_id, {{"job-state", 4}});
    }
    static result<void> release_job_ipp(const knst_c16string& host_port, uint32_t job_id) {
        std::string hpu; bool use_tls = false; uint16_t port = 631;
        if (!detail::ipp::split_uri(detail::to_u8(host_port), hpu, use_tls, port))
            return result<void>::fail(status::InvalidArgument);
        return detail::ipp::set_job_attrs(hpu, use_tls, job_id, {{"job-state", 3}});
    }
    static result<void> set_job_priority_ipp(const knst_c16string& host_port,
                                              uint32_t job_id, uint32_t prio) {
        if (prio == 0 || prio > 100) return result<void>::fail(status::InvalidArgument);
        std::string hpu; bool use_tls = false; uint16_t port = 631;
        if (!detail::ipp::split_uri(detail::to_u8(host_port), hpu, use_tls, port))
            return result<void>::fail(status::InvalidArgument);
        return detail::ipp::set_job_attrs(hpu, use_tls, job_id, {{"job-priority", prio}});
    }
#else
    static result<void> hold_job_ipp(const knst_c16string&, uint32_t) { return result<void>::fail(status::NotSupported); }
    static result<void> release_job_ipp(const knst_c16string&, uint32_t) { return result<void>::fail(status::NotSupported); }
    static result<void> set_job_priority_ipp(const knst_c16string&, uint32_t, uint32_t) { return result<void>::fail(status::NotSupported); }
#endif

    // ─── Universal print: winspool first, direct IPP fallback ────────
    // Windows: winspool driver chain is used for local printer setup,
    // but PDF requires a reader (Acrobat/Sumatra) registered for
    // "printto". Direct IPP bypasses this — no driver, no reader.
    //
    // host_port is optional: if empty, uses winspool only.
    // If non-empty ("host:port", "ipp://host", "ipps://host"), tries
    // direct IPP first, then winspool as final fallback.
    result<uint32_t> print_universal(const knst_c16string& path,
                                      const knst_c16string& host_port = knst_c16string(),
                                      const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);

#if KNST_PRN_HAS_NETWORK
        if (!host_port.empty() && detail::detect_kind(path) == detail::file_kind::Pdf) {
            auto r = print_ipp_smart(path, host_port, o);
            if (r) return r;
        }
#endif
        return print_file(path, o);
    }
private:
    knst_c16string name_;

    static uint32_t elapsed_ms(std::chrono::steady_clock::time_point t0) {
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    }

    result<uint32_t> print_doc(const print_options& o, const std::function<knst_prn::status(float, float, detail::document&)>& build) {
        using R = result<uint32_t>;
        if (!detail::valid_options(o)) return R::fail(status::InvalidArgument);
        detail::win_dc wd;
        if (!detail::make_dc(name_, o, wd)) return R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
        float w, h; detail::dc_page_mm(wd.dc, w, h);
        detail::document doc;
        knst_prn::status st = build(w, h, doc);
        if (st != status::Ok) return R::fail(st);
        uint32_t job = 0;
        st = detail::render_document(wd.dc, doc, o.title, job);
        if (st != status::Ok) return R::fail(st, static_cast<int>(GetLastError()));
        return R::success(job);
    }

    result<uint32_t> shell_print(const knst_c16string& abs) {
        using R = result<uint32_t>;
        auto ids = [&]() { std::vector<uint32_t> v; detail::hprinter hp(name_); std::vector<job_info> j;
                           if (hp.ok() && detail::enum_jobs(hp.h, j)) for (auto& x : j) v.push_back(x.id); return v; };
        std::vector<uint32_t> before = ids();
        std::wstring file = detail::wide(abs), params = L"\"" + detail::wide(name_) + L"\"";
        HINSTANCE h = ShellExecuteW(nullptr, L"printto", file.c_str(), params.c_str(), nullptr, SW_HIDE);
        if (reinterpret_cast<INT_PTR>(h) <= 32) return R::fail(status::NotSupported, static_cast<int>(reinterpret_cast<INT_PTR>(h)));
        for (int i = 0; i < 60; ++i) {                    // wait up to 15 s for the spooler job to appear
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            for (uint32_t id : ids()) if (std::find(before.begin(), before.end(), id) == before.end()) return R::success(id);
        }
        return R::success(0);                              // sent, job id unknown
    }

    result<void> job_cmd(uint32_t id, DWORD command) {
        using R = result<void>;
        if (!id) return R::fail(status::InvalidArgument);
        detail::hprinter hp(name_);
        if (!hp.ok()) return R::fail(detail::win_status(GetLastError()));
        return SetJobW(hp.h, id, 0, nullptr, command) ? R::success() : R::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
    }
    result<void> all_jobs(DWORD command, const knst_c16string* user) {
        auto p = jobs(false);
        if (!p) return result<void>::fail(p.st, p.native);
        bool any = false;
        for (auto& j : p.value) if ((!user || j.user == *user) && job_cmd(j.id, command)) any = true;
        return any ? result<void>::success() : result<void>::fail(status::NotFound);
    }
    result<void> printer_ctl(DWORD command) {
        detail::hprinter hp(name_, PRINTER_ACCESS_ADMINISTER);
        if (!hp.ok()) return result<void>::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
        return SetPrinterW(hp.h, 0, nullptr, command) ? result<void>::success()
               : result<void>::fail(detail::win_status(GetLastError()), static_cast<int>(GetLastError()));
    }
};

// ===========================================================================
//                          Scanner class - Windows (WIA 2.0)
// ===========================================================================
#if defined(_MSC_VER)

namespace detail {

struct com_init {
    bool ok = false;
    com_init() { ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~com_init() { if (ok) CoUninitialize(); }
};
template <class T> struct com_ptr {
    T* p = nullptr;
    com_ptr() = default;
    com_ptr(const com_ptr&) = delete;
    com_ptr& operator=(const com_ptr&) = delete;
    ~com_ptr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct scan_dir {
    knst_c16string path;
    scan_dir() {
        path = make_temp(u"");
        knst_file::create_directories(path);
    }
    scan_dir(const scan_dir&) = delete;
    scan_dir& operator=(const scan_dir&) = delete;
    ~scan_dir() { knst_file::remove_all(path); }
};

class wia_callback : public IWiaTransferCallback {
public:
    wia_callback(const knst_c16string& dir, const std::string& ext) : dir_(dir), ext_(ext) {}
    std::vector<knst_c16string> files;
    STDMETHODIMP QueryInterface(REFIID iid, void** ppv) override {
        if (iid == IID_IUnknown || iid == IID_IWiaTransferCallback) { *ppv = static_cast<IWiaTransferCallback*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&ref_); if (!r) delete this; return static_cast<ULONG>(r); }
    STDMETHODIMP TransferCallback(LONG, WiaTransferParams*) override { return S_OK; }
    STDMETHODIMP GetNextStream(LONG, BSTR, BSTR, IStream** pp) override {
        char num[16]; std::snprintf(num, sizeof(num), "page_%03d.", ++count_);
        knst_c16string p = knst_file::join_path(dir_, from_u8(std::string(num) + ext_));
        files.push_back(p);
        std::wstring w = wide(p);
        return SHCreateStreamOnFileW(w.c_str(), STGM_CREATE | STGM_WRITE, pp);
    }
private:
    LONG ref_ = 1; int count_ = 0; knst_c16string dir_; std::string ext_;
};

inline void wia_set(IWiaPropertyStorage* ps, PROPID id, LONG v) {
    PROPSPEC spec; spec.ulKind = PRSPEC_PROPID; spec.propid = id;
    PROPVARIANT pv; PropVariantInit(&pv); pv.vt = VT_I4; pv.lVal = v;
    ps->WriteMultiple(1, &spec, &pv, WIA_IPA_FIRST);
}

} // namespace detail

class scanner {
public:
    static result<knst_vector<scanner_info>> list_all() {
        using R = result<knst_vector<scanner_info>>;
        detail::com_init ci;
        detail::com_ptr<IWiaDevMgr2> mgr;
        if (FAILED(CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr2, reinterpret_cast<void**>(&mgr))))
            return R::fail(status::NotSupported);
        detail::com_ptr<IEnumWIA_DEV_INFO> en;
        if (FAILED(mgr->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL, &en))) return R::fail(status::IoError);
        R r;
        IWiaPropertyStorage* ps = nullptr; ULONG got = 0;
        while (en->Next(1, &ps, &got) == S_OK && ps) {
            PROPSPEC spec[2]; spec[0].ulKind = spec[1].ulKind = PRSPEC_PROPID;
            spec[0].propid = WIA_DIP_DEV_ID; spec[1].propid = WIA_DIP_DEV_NAME;
            PROPVARIANT pv[2]; PropVariantInit(&pv[0]); PropVariantInit(&pv[1]);
            if (SUCCEEDED(ps->ReadMultiple(2, spec, pv)) && pv[0].vt == VT_BSTR) {
                scanner_info si;
                si.device_id = detail::from_wide(pv[0].bstrVal);
                si.name = (pv[1].vt == VT_BSTR) ? detail::from_wide(pv[1].bstrVal) : si.device_id;
                si.backend = u"wia"; si.flatbed = true;
                r.value.push_back(std::move(si));
            }
            PropVariantClear(&pv[0]); PropVariantClear(&pv[1]);
            ps->Release(); ps = nullptr;
        }
        return r;
    }
    static result<scanner> open(const knst_c16string& device_id) {
        if (device_id.empty()) return result<scanner>::fail(status::InvalidArgument);
        scanner s; s.name_ = device_id; s.device_id_ = device_id;
        return result<scanner>::success(std::move(s));
    }
    const knst_c16string& name() const noexcept { return name_; }

    result<scanner_capabilities> capabilities() const {
        using R = result<scanner_capabilities>;
        detail::com_init ci;
        detail::com_ptr<IWiaItem2> root;
        if (!connect(root)) return R::fail(status::NotFound);
        R r;
        detail::com_ptr<IWiaPropertyStorage> ps;
        if (SUCCEEDED(root->QueryInterface(IID_IWiaPropertyStorage, reinterpret_cast<void**>(&ps)))) {
            PROPSPEC spec; spec.ulKind = PRSPEC_PROPID; spec.propid = WIA_DPS_DOCUMENT_HANDLING_CAPABILITIES;
            PROPVARIANT pv; PropVariantInit(&pv);
            if (SUCCEEDED(ps->ReadMultiple(1, &spec, &pv)) && pv.vt == VT_I4) {
                r.value.has_adf = (pv.lVal & FEEDER) != 0; r.value.has_flatbed = (pv.lVal & FLATBED) != 0;
                r.value.has_duplex_adf = (pv.lVal & DUPLEX) != 0;
            }
            PropVariantClear(&pv);
        }
        for (uint32_t v : {100u, 200u, 300u, 600u}) r.value.resolutions.push_back(v);
        r.value.modes.push_back(color_mode::Color); r.value.modes.push_back(color_mode::Gray); r.value.modes.push_back(color_mode::Mono);
        return r;
    }

    result<knst_byte_string> scan_page(const scan_options& opt = scan_options()) {
        using R = result<knst_byte_string>;
        scan_options o = opt; o.page_count = 1;
        detail::scan_dir dir;
        auto pages = scan_pages(o, dir.path, format_of(o));
        if (!pages) return R::fail(pages.st, pages.native);
        knst_file_error fe = knst_file_error::None;
        knst_byte_string b = knst_file::read_file_data<>(pages.value[0], &fe);
        if (fe != knst_file_error::None || b.empty()) return R::fail(status::IoError);
        return R::success(std::move(b));
    }
    result<knst_c16string> scan_to_file(const knst_c16string& path, const scan_options& opt = scan_options()) {
        using R = result<knst_c16string>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        if (detail::lower(detail::to_u8(opt.output_format)) == "pdf") return scan_to_pdf(path, opt);
        knst_c16string abs = knst_file::resolve_path(path);
        detail::scan_dir dir;
        auto pages = scan_pages(opt, dir.path, format_of(opt));
        if (!pages) return R::fail(pages.st, pages.native);
        if (!(opt.use_adf && pages.value.size() > 1)) {
            if (!knst_file::move_path(pages.value[0], abs, true)) return R::fail(status::IoError);
            return R::success(abs);
        }
        knst_c16string parent = knst_file::get_parent_dir(abs), first;
        std::string stem = detail::to_u8(knst_file::get_stem(abs)), ext = detail::path_ext(pages.value[0]);
        for (size_t i = 0; i < pages.value.size(); ++i) {
            char num[16]; std::snprintf(num, sizeof(num), "_%03u", static_cast<unsigned>(i + 1));
            knst_c16string dst = knst_file::join_path(parent, detail::from_u8(stem + num + ext));
            if (!knst_file::move_path(pages.value[i], dst, true)) return R::fail(status::IoError);
            if (i == 0) first = dst;
        }
        return R::success(first);
    }
    result<knst_c16string> scan_to_pdf(const knst_c16string& path, const scan_options& opt = scan_options()) {
        using R = result<knst_c16string>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        detail::scan_dir dir;
        auto pages = scan_chain(opt, dir.path);
        if (!pages) return R::fail(pages.st, pages.native);
        float w = 210.0f, h = 297.0f;
        if (!detail::paper_mm(opt.paper, w, h)) { w = 210.0f; h = 297.0f; }
        std::vector<knst_c16string> v; for (auto& p : pages.value) v.push_back(p);
        image_batch_options bo; bo.margin_mm = 0.0f; bo.apply_exif = false; bo.skip_unreadable = false;
        bo.max_dpi = std::max<uint32_t>(150, opt.dpi);
        detail::document doc;
        auto st = detail::layout_images(v, bo, w, h, doc);
        if (st != status::Ok) return R::fail(st);
        knst_c16string tmp = abs; tmp.append(u".knsttmp");
        st = detail::write_pdf(doc, tmp);
        if (st != status::Ok) { knst_file::remove_file(tmp); return R::fail(st); }
                if (!knst_file::move_path(tmp, abs, true)) { knst_file::remove_file(tmp); return R::fail(status::IoError); }
        return R::success(abs);
    }
    result<knst_vector<knst_c16string>> scan_to_files(const knst_c16string& prefix_path,
                                                       const scan_options& o = scan_options()) {
        using R = result<knst_vector<knst_c16string>>;
        if (prefix_path.empty()) return R::fail(status::InvalidArgument);
        std::string fmt = detail::lower(detail::to_u8(o.output_format));
        if (fmt == "pdf") return R::fail(status::InvalidArgument);

        knst_c16string abs = knst_file::resolve_path(prefix_path);
        knst_c16string parent = knst_file::get_parent_dir(abs);
        if (parent.empty()) parent = knst_file::get_temp_dir();
        std::string stem = detail::to_u8(knst_file::get_stem(abs));
        if (stem.empty()) stem = "scan";

        detail::scan_dir dir;
        auto pages = scan_pages(o, dir.path, format_of(o));
        if (!pages) return R::fail(pages.st, pages.native);

        const std::string ext = detail::path_ext(pages.value[0]);
        R out;
        for (size_t i = 0; i < pages.value.size(); ++i) {
            char num[16];
            std::snprintf(num, sizeof(num), "_%03u", static_cast<unsigned>(i + 1));
            knst_c16string dst = knst_file::join_path(
                parent, detail::from_u8(stem + num + ext));
            if (!knst_file::move_path(pages.value[i], dst, true))
                return R::fail(status::IoError);
            out.value.push_back(dst);
        }
        return out;
    }
    result<uint32_t> scan_and_print(const print_options& popt = print_options(), const scan_options& sopt = scan_options()) {
        auto pr = printer::open_any();
        if (!pr) return result<uint32_t>::fail(pr.st, pr.native);
        return scan_and_print_to(pr.value, popt, sopt);
    }
    result<uint32_t> scan_and_print_to(printer& pr, const print_options& popt = print_options(), const scan_options& sopt = scan_options()) {
        using R = result<uint32_t>;
        detail::scan_dir dir;
        auto pages = scan_chain(sopt, dir.path);
        if (!pages) return R::fail(pages.st, pages.native);
        std::vector<knst_c16string> v; for (auto& p : pages.value) v.push_back(p);
        image_batch_options bo; bo.margin_mm = popt.borderless ? 0.0f : 5.0f; bo.apply_exif = false; bo.skip_unreadable = false;
        return pr.print_images(v, bo, popt);
    }

private:
    knst_c16string name_, device_id_;

    static std::string format_of(const scan_options& o) {
        std::string f = detail::lower(detail::to_u8(o.output_format));
        if (f == "jpg" || f == "jpeg") return "jpeg";
        if (f == "tif" || f == "tiff") return "tiff";
        if (f == "bmp") return "bmp";
        return "png";
    }
    bool connect(detail::com_ptr<IWiaItem2>& root) const {
        detail::com_ptr<IWiaDevMgr2> mgr;
        if (FAILED(CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr2, reinterpret_cast<void**>(&mgr)))) return false;
        std::wstring w = detail::wide(device_id_);
        BSTR id = SysAllocString(w.c_str());
        HRESULT hr = mgr->CreateDevice(0, id, &root);
        SysFreeString(id);
        return SUCCEEDED(hr) && root;
    }

    result<knst_vector<knst_c16string>> scan_pages(const scan_options& o, const knst_c16string& dir, const std::string& fmt) {
        using R = result<knst_vector<knst_c16string>>;
        detail::com_init ci;
        detail::com_ptr<IWiaItem2> root;
        if (!connect(root)) return R::fail(status::NotFound);
        const GUID want = o.use_adf ? WIA_CATEGORY_FEEDER : WIA_CATEGORY_FLATBED;
        detail::com_ptr<IEnumWiaItem2> en;
        if (FAILED(root->EnumChildItems(nullptr, &en))) return R::fail(status::IoError);
        detail::com_ptr<IWiaItem2> item;
        for (;;) {
            IWiaItem2* c = nullptr; ULONG got = 0;
            if (en->Next(1, &c, &got) != S_OK || !c) break;
            GUID cat = GUID_NULL;
            if (SUCCEEDED(c->GetItemCategory(&cat)) && cat == want) { item.p = c; break; }
            c->Release();
        }
        if (!item) return R::fail(status::NotFound);

        if (o.use_adf) {                                   // feeder selection lives on the root item
            detail::com_ptr<IWiaPropertyStorage> rp;
            if (SUCCEEDED(root->QueryInterface(IID_IWiaPropertyStorage, reinterpret_cast<void**>(&rp)))) {
                detail::wia_set(rp.p, WIA_DPS_DOCUMENT_HANDLING_SELECT, FEEDER | (o.duplex ? DUPLEX : 0));
                detail::wia_set(rp.p, WIA_DPS_PAGES, static_cast<LONG>(o.page_count));
            }
        }
        detail::com_ptr<IWiaPropertyStorage> ps;
        if (SUCCEEDED(item->QueryInterface(IID_IWiaPropertyStorage, reinterpret_cast<void**>(&ps)))) {
            const LONG dpi = static_cast<LONG>(o.dpi ? o.dpi : 300);
            detail::wia_set(ps.p, WIA_IPS_XRES, dpi); detail::wia_set(ps.p, WIA_IPS_YRES, dpi);
            detail::wia_set(ps.p, WIA_IPA_DATATYPE, o.mode == color_mode::Gray ? WIA_DATA_GRAYSCALE
                                                   : o.mode == color_mode::Mono ? WIA_DATA_THRESHOLD : WIA_DATA_COLOR);
            float wmm, hmm;
            if (detail::paper_mm(o.paper, wmm, hmm)) {
                detail::wia_set(ps.p, WIA_IPS_XPOS, 0); detail::wia_set(ps.p, WIA_IPS_YPOS, 0);
                detail::wia_set(ps.p, WIA_IPS_XEXTENT, static_cast<LONG>(wmm / 25.4f * dpi));
                detail::wia_set(ps.p, WIA_IPS_YEXTENT, static_cast<LONG>(hmm / 25.4f * dpi));
            }
            detail::wia_set(ps.p, WIA_IPA_TYMED, TYMED_FILE);
            PROPSPEC spec; spec.ulKind = PRSPEC_PROPID; spec.propid = WIA_IPA_FORMAT;
            PROPVARIANT pv; PropVariantInit(&pv); pv.vt = VT_CLSID;
            GUID g = fmt == "jpeg" ? WiaImgFmt_JPEG : fmt == "tiff" ? WiaImgFmt_TIFF : fmt == "bmp" ? WiaImgFmt_BMP : WiaImgFmt_PNG;
            pv.puuid = &g;
            ps->WriteMultiple(1, &spec, &pv, WIA_IPA_FIRST);
        }
        detail::com_ptr<IWiaTransfer> tr;
        if (FAILED(item->QueryInterface(IID_IWiaTransfer, reinterpret_cast<void**>(&tr)))) return R::fail(status::NotSupported);
        const std::string ext = fmt == "jpeg" ? "jpg" : fmt == "tiff" ? "tif" : fmt;
        detail::wia_callback* cb = new detail::wia_callback(dir, ext);
        HRESULT hr = tr->Download(0, cb);
        R r;
        for (auto& f : cb->files) {
            knst_file_error fe = knst_file_error::None;
            if (knst_file::get_file_size(f) > 0) r.value.push_back(f);
            (void)fe;
        }
        cb->Release();
        if (r.value.empty()) return R::fail(hr == static_cast<HRESULT>(0x80210003) ? status::NotFound : status::IoError, static_cast<int>(hr));
        return r;                                          // "paper empty" after >= 1 page is the normal feeder end
    }
    result<knst_vector<knst_c16string>> scan_chain(const scan_options& o, const knst_c16string& dir) {
        // ADF: DO NOT retry with another format. WIA advances the feeder on
        // every Download() attempt; a failed first attempt would have already
        // consumed sheets and retrying with png/bmp starts from a partial or
        // empty feeder. Use JPEG (universally supported) as the only format.
        if (o.use_adf) {
            return scan_pages(o, dir, "jpeg");
        }
        // Flatbed: try jpeg, then png, then bmp (page doesn't move between
        // attempts so retry is safe).
        result<knst_vector<knst_c16string>> last =
            result<knst_vector<knst_c16string>>::fail(status::IoError);
        for (const char* f : {"jpeg", "png", "bmp"}) {
            auto r = scan_pages(o, dir, f);
            if (r) return r;
            last = r;
        }
        return last;
    }
};

#else
// ===========================================================================
//  MinGW: eSCL (AirScan) scanner — WIA 2.0 is unavailable on MinGW builds.
//  Uses HTTP+XML eSCL for modern printers (HP OfficeJet Pro 7740,
//  Canon MF, Epson WF, ...).
// ===========================================================================

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// ============================================================================
//  Minimal mDNS resolver — Windows getaddrinfo does not resolve ".local"
//  (Windows only supports mDNS via DnsServiceBrowse, not getaddrinfo).
//  Queries A records over UDP multicast 224.0.0.251:5353.
//  Not needed on Linux/Android — they use avahi/NsdManager.
// ============================================================================
namespace knst_mdns {

inline void encode_name(const std::string& name, std::vector<uint8_t>& out) {
    size_t i = 0, n = name.size();
    while (i < n) {
        size_t dot = name.find('.', i);
        if (dot == std::string::npos) dot = n;
        uint8_t ll = (uint8_t)(dot - i);
        if (ll > 0 && ll < 64) {
            out.push_back(ll);
            for (size_t j = i; j < dot; ++j) out.push_back((uint8_t)name[j]);
        }
        i = dot + 1;
    }
    out.push_back(0);
}

inline bool decode_name(const uint8_t* b, size_t len, size_t& pos, std::string& out) {
    out.clear();
    int jumps = 0;
    size_t cur = pos;
    bool used_ptr = false;
    while (cur < len && jumps++ < 30) {
        uint8_t l = b[cur];
        if (l == 0) { if (!used_ptr) pos = cur + 1; return true; }
        if ((l & 0xC0) == 0xC0) {
            if (cur + 1 >= len) return false;
            uint16_t off = ((uint16_t)(l & 0x3F) << 8) | b[cur + 1];
            if (!used_ptr) { pos = cur + 2; used_ptr = true; }
            if (off >= cur) return false;
            cur = off;
            continue;
        }
        if (l > 63 || cur + 1 + l > len) return false;
        if (!out.empty()) out.push_back('.');
        for (uint8_t k = 0; k < l; ++k) out.push_back((char)b[cur + 1 + k]);
        cur += 1 + l;
    }
    return false;
}

inline std::vector<uint8_t> build_a_query(const std::string& name) {
    std::vector<uint8_t> p;
    p.push_back(0); p.push_back(0);  // id
    p.push_back(0); p.push_back(0);  // flags
    p.push_back(0); p.push_back(1);  // qd
    p.push_back(0); p.push_back(0);
    p.push_back(0); p.push_back(0);
    p.push_back(0); p.push_back(0);
    encode_name(name, p);
    p.push_back(0); p.push_back(1);   // type A
    p.push_back(0x80); p.push_back(0x01);  // class IN + QU bit (unicast resp)
    return p;
}

inline void parse_a(const uint8_t* b, size_t len, const std::string& target,
                    std::vector<std::string>& out) {
    if (len < 12) return;
    uint16_t qd = (b[4] << 8) | b[5];
    uint16_t an = (b[6] << 8) | b[7];
    size_t pos = 12;
    for (uint16_t q = 0; q < qd && pos < len; ++q) {
        std::string tmp;
        if (!decode_name(b, len, pos, tmp)) return;
        pos += 4;
    }
    for (uint16_t a = 0; a < an && pos < len; ++a) {
        std::string rn;
        if (!decode_name(b, len, pos, rn)) return;
        if (pos + 10 > len) return;
        uint16_t rtype = (b[pos] << 8) | b[pos + 1];
        uint16_t rclass = (b[pos + 2] << 8) | b[pos + 3];
        uint16_t rdlen = (b[pos + 8] << 8) | b[pos + 9];
        pos += 10;
        if (pos + rdlen > len) return;
        if (rtype == 1 && rdlen == 4 && (rclass & 0x7FFF) == 1) {
            bool match = target.empty() || (rn.size() == target.size() &&
                std::equal(rn.begin(), rn.end(), target.begin(),
                    [](char x, char y){ return ::tolower((unsigned char)x) == ::tolower((unsigned char)y); }));
            if (match) {
                char ip[24];
                ::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", b[pos], b[pos+1], b[pos+2], b[pos+3]);
                out.push_back(ip);
            }
        }
        pos += rdlen;
    }
}

// Resolves a ".local" hostname via A record lookup.
inline bool resolve_ipv4(const std::string& hostname, std::vector<std::string>& ips,
                         uint32_t timeout_ms = 1500) {
    ips.clear();
    if (hostname.empty()) return false;

    SOCKET fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == INVALID_SOCKET) return false;

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    // Try binding to 5353 to catch multicast responses.
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(5353);
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
                addr.sin_port = 0;   // fallback: ephemeral port (QU-only mode)
        if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
            ::closesocket(fd); return false;
        }
    }
    struct ip_mreq mreq{};
    ::inet_pton(AF_INET, "224.0.0.251", &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    ::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));

    int ttl = 255;
    ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));

    std::vector<uint8_t> q = build_a_query(hostname);
    struct sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(5353);
    ::inet_pton(AF_INET, "224.0.0.251", &dst.sin_addr);

    auto t0 = std::chrono::steady_clock::now();
    bool sent = false;

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (elapsed >= (long long)timeout_ms) break;

                // Resend every 200 ms to survive packet loss.
        if (!sent || elapsed >= 200) {
            ::sendto(fd, (char*)q.data(), (int)q.size(), 0, (sockaddr*)&dst, sizeof(dst));
            sent = true;
        }

        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
        struct timeval tv{};
        int rem = (int)(timeout_ms - elapsed);
        tv.tv_sec = rem / 1000;
        tv.tv_usec = (rem % 1000) * 1000;
        int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        char buf[4096];
        struct sockaddr_in from{};
        int fromlen = sizeof(from);
        int n = ::recvfrom(fd, buf, sizeof(buf), 0, (sockaddr*)&from, &fromlen);
        if (n > 0) parse_a((uint8_t*)buf, (size_t)n, hostname, ips);
        if (!ips.empty()) break;
    }
    ::closesocket(fd);
    return !ips.empty();
}

// Resolves a ".local" hostname via A record. If the full name fails,
// retries with the first label (HPB664C4.modem.local → HPB664C4.local).
inline bool resolve_local(const std::string& host, std::vector<std::string>& ips,
                          uint32_t timeout_ms = 1500) {
    ips.clear();
    if (resolve_ipv4(host, ips, timeout_ms)) return true;
        // Custom suffix like ".modem.local" → try first label + ".local"
    size_t dot = host.find('.');
    if (dot != std::string::npos) {
        std::string alt = host.substr(0, dot) + ".local";
        if (alt != host && resolve_ipv4(alt, ips, timeout_ms)) return true;
    }
    return false;
}

// ============================================================================
//  PTR browse — queries like "_uscan._tcp.local"
//  The only way to find all scanners on Windows without DnsServiceBrowse.
// ============================================================================

// Collect every PTR record in the DNS message.
inline void parse_ptr(const uint8_t* b, size_t len, std::vector<std::string>& out) {
    if (len < 12) return;
    uint16_t qd = (b[4] << 8) | b[5];
    uint16_t an = (b[6] << 8) | b[7];
    size_t pos = 12;
    for (uint16_t q = 0; q < qd && pos < len; ++q) {
        std::string tmp;
        if (!decode_name(b, len, pos, tmp)) return;
        pos += 4;
    }
    for (uint16_t a = 0; a < an && pos < len; ++a) {
        std::string rn;
        if (!decode_name(b, len, pos, rn)) return;
        if (pos + 10 > len) return;
        uint16_t rtype = (b[pos] << 8) | b[pos + 1];
        uint16_t rdlen = (b[pos + 8] << 8) | b[pos + 9];
        pos += 10;
        if (pos + rdlen > len) return;
        if (rtype == 12) {   // PTR
            size_t rp = pos;
            std::string target;
            if (decode_name(b, len, rp, target) && !target.empty())
                out.push_back(target);
        }
        pos += rdlen;
    }
}

// Extract the first SRV record: host and port.
inline void parse_srv(const uint8_t* b, size_t len, std::string& host, uint16_t& port) {
    host.clear(); port = 0;
    if (len < 12) return;
    uint16_t qd = (b[4] << 8) | b[5];
    uint16_t an = (b[6] << 8) | b[7];
    size_t pos = 12;
    for (uint16_t q = 0; q < qd && pos < len; ++q) {
        std::string tmp;
        if (!decode_name(b, len, pos, tmp)) return;
        pos += 4;
    }
    for (uint16_t a = 0; a < an && pos < len; ++a) {
        std::string rn;
        if (!decode_name(b, len, pos, rn)) return;
        if (pos + 10 > len) return;
        uint16_t rtype = (b[pos] << 8) | b[pos + 1];
        uint16_t rdlen = (b[pos + 8] << 8) | b[pos + 9];
        pos += 10;
        if (pos + rdlen > len) return;
        if (rtype == 33 && rdlen >= 6) {     // SRV
            port = (uint16_t)((b[pos + 4] << 8) | b[pos + 5]);
            size_t rp = pos + 6;
            decode_name(b, len, rp, host);
            return;
        }
        pos += rdlen;
    }
}

// Send a single query for `name` and collect the response.
inline bool mdns_query_once(const std::string& name, uint16_t qtype,
                            std::vector<uint8_t>& response, uint32_t timeout_ms) {
    SOCKET fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == INVALID_SOCKET) return false;

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(5353);
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        addr.sin_port = 0;
        if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
            ::closesocket(fd); return false;
        }
    }
    struct ip_mreq mreq{};
    ::inet_pton(AF_INET, "224.0.0.251", &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    ::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));

    // Query packet
    std::vector<uint8_t> q;
    q.push_back(0); q.push_back(0);   // id
    q.push_back(0); q.push_back(0);   // flags
    q.push_back(0); q.push_back(1);   // qd
    q.push_back(0); q.push_back(0);
    q.push_back(0); q.push_back(0);
    q.push_back(0); q.push_back(0);
    encode_name(name, q);
    q.push_back((uint8_t)(qtype >> 8));
    q.push_back((uint8_t)(qtype & 0xFF));
    q.push_back(0x00); q.push_back(0x01);   // class IN (multicast, no QU)

    struct sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(5353);
    ::inet_pton(AF_INET, "224.0.0.251", &dst.sin_addr);

    auto t0 = std::chrono::steady_clock::now();
    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (elapsed >= (long long)timeout_ms) break;

        if (elapsed < 100 || (elapsed % 300) < 10) {
            ::sendto(fd, (char*)q.data(), (int)q.size(), 0, (sockaddr*)&dst, sizeof(dst));
        }

        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
        struct timeval tv{};
        int rem = (int)(timeout_ms - elapsed);
        tv.tv_sec = rem / 1000; tv.tv_usec = (rem % 1000) * 1000;
        int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        char buf[4096];
        int n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            response.assign(buf, buf + n);
            ::closesocket(fd);
            return true;
        }
    }
    ::closesocket(fd);
    return false;
}

// mDNS service browse for types like "_uscan._tcp.local".
// Returns a list of (instance_name, host, port).
struct mdns_service {
    std::string instance;
    std::string host;
    uint16_t port = 0;
};

inline std::vector<mdns_service> browse_service(const std::string& service_type,
                                                uint32_t timeout_ms = 2500) {
    std::vector<mdns_service> results;

    // 1) PTR query → instance names.
    std::vector<uint8_t> resp;
    if (!mdns_query_once(service_type, 12, resp, timeout_ms)) return results;

    std::vector<std::string> instances;
    parse_ptr(resp.data(), resp.size(), instances);
    if (instances.empty()) return results;

    // 2) SRV + A for each instance.
    for (const auto& inst : instances) {
        mdns_service svc;
        svc.instance = inst;

        std::vector<uint8_t> srv_resp;
        if (!mdns_query_once(inst, 33, srv_resp, 1500)) continue;

        std::string srv_host;
        uint16_t srv_port = 0;
        parse_srv(srv_resp.data(), srv_resp.size(), srv_host, srv_port);
        if (srv_host.empty() || srv_port == 0) continue;
        svc.host = srv_host;
        svc.port = srv_port;

        // If the host is not already an IP, resolve it with an A query.
        struct in_addr a4{};
        if (::inet_pton(AF_INET, srv_host.c_str(), &a4) != 1) {
            std::vector<std::string> ips;
            resolve_ipv4(srv_host, ips, 1000);
            if (!ips.empty()) svc.host = ips[0];   // replace with the resolved IP
        }
        results.push_back(std::move(svc));
    }
    return results;
}
} // namespace knst_mdns

namespace detail {

inline bool wsa_ensure_started() {
    static bool started = []() {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return started;
}

// Windows socket → int wrapper (Winsock SOCKET is UINT_PTR).
inline int ws_open(int af, int type, int proto) {
    SOCKET s = ::socket(af, type, proto);
    return (s == INVALID_SOCKET) ? -1 : (int)s;
}
inline int ws_connect(int fd, const struct sockaddr* addr, int addrlen) {
    return ::connect((SOCKET)fd, addr, addrlen);
}
inline int ws_send(int fd, const char* buf, int len) {
    return ::send((SOCKET)fd, buf, len, 0);  // no MSG_NOSIGNAL on Windows, no SIGPIPE either
}
inline int ws_recv(int fd, char* buf, int len) {
    return ::recv((SOCKET)fd, buf, len, 0);
}
inline void ws_close(int fd) { ::closesocket((SOCKET)fd); }

} // namespace detail

class scanner {
public:
    // eSCL mDNS discovery is unavailable on Windows — hosts come from
    // the KNST_SCANNER_HOSTS environment variable.
    //   $env:KNST_SCANNER_HOSTS = "HPB664C4.modem.local:80"
    //   Multiple: "host1:80,host2:443"
         static result<knst_vector<scanner_info>> list_all() {
        using R = result<knst_vector<scanner_info>>;
        if (!detail::wsa_ensure_started()) return R::fail(status::IoError);

        R r;
        const char* hosts = std::getenv("KNST_SCANNER_HOSTS");

        // 1) Env var present → manual list (override). Never cached.
        if (hosts && *hosts) {
            std::string hs(hosts);
            size_t pos = 0;
            while (pos < hs.size()) {
                size_t comma = hs.find(',', pos);
                std::string one = hs.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                pos = (comma == std::string::npos) ? hs.size() : comma + 1;
                if (one.empty()) continue;
                scanner_info si;
                si.name = detail::from_u8(one);
                si.device_id = detail::from_u8(one);
                si.backend = u"escl";
                si.is_network = true;
                si.flatbed = true;
                si.adf = false;
                r.value.push_back(std::move(si));
            }
            return r;
        }

        // 2) Cache check (only when env var is absent).
        static std::mutex cache_m;
        static knst_vector<scanner_info> cached;
        static std::chrono::steady_clock::time_point cached_at{};
        {
            std::lock_guard<std::mutex> lk(cache_m);
            auto now = std::chrono::steady_clock::now();
            if (!cached.empty() && (now - cached_at) < std::chrono::minutes(5)) {
                return R::success(cached);
            }
        }

               auto mdns_scanners = knst_mdns::browse_service("_uscan._tcp.local", 4000);
        for (const auto& svc : mdns_scanners) {
            std::string device_id = svc.host + ":" + std::to_string(svc.port);
            scanner_info si;
            si.name = detail::from_u8(svc.instance);
            si.device_id = detail::from_u8(device_id);
            si.backend = u"escl";
            si.is_network = true;
            si.flatbed = true;
            si.adf = false;
            r.value.push_back(std::move(si));

        }

        // Also try _scanner._tcp.local (some vendors use it).
        if (r.value.empty()) {
                        auto mdns_scanners2 = knst_mdns::browse_service("_scanner._tcp.local", 4000);
            for (const auto& svc : mdns_scanners2) {
                std::string device_id = svc.host + ":" + std::to_string(svc.port);
                scanner_info si;
                si.name = detail::from_u8(svc.instance);
                si.device_id = detail::from_u8(device_id);
                si.backend = u"escl";
                si.is_network = true;
                si.flatbed = true;
                si.adf = false;
                r.value.push_back(std::move(si));

            }
        }

                // ═══════ RETRY ON EMPTY (mDNS SRV/A race fix) ═══════
        // Some printers (e.g. HP 7740) send the PTR reply immediately but
        // the SRV/A records 300-500 ms later in separate packets. The
        // first 4 s window can miss the SRV/A records → empty result.
        //
        // IMPORTANT: the retry must re-run mDNS browse, NOT a subnet scan.
        // The HP 7740 eSCL service lives on port 8080 (per its SRV reply),
        // but scan_local_subnet_() only probes port 80 → it can never find
        // it. Retrying mDNS is the only correct path.
        //
        // This retry also runs BEFORE the subnet scan:
        //   • mDNS succeeds → 8.5 s total (4+4+0.5)
        //   • Subnet scan runs only if mDNS truly failed (~5-10 s)
        //   • Old ordering wasted 18 s (4+4+5+5) before falling back
        if (r.value.empty()) {

            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Retry _uscan._tcp.local
            auto retry1 = knst_mdns::browse_service("_uscan._tcp.local", 4000);
            for (const auto& svc : retry1) {
                std::string device_id = svc.host + ":" + std::to_string(svc.port);
                scanner_info si;
                si.name = detail::from_u8(svc.instance);
                si.device_id = detail::from_u8(device_id);
                si.backend = u"escl";
                si.is_network = true;
                si.flatbed = true;
                si.adf = false;
                r.value.push_back(std::move(si));

            }

            // Still empty → try _scanner._tcp.local
            if (r.value.empty()) {
                auto retry2 = knst_mdns::browse_service("_scanner._tcp.local", 4000);
                for (const auto& svc : retry2) {
                    std::string device_id = svc.host + ":" + std::to_string(svc.port);
                    scanner_info si;
                    si.name = detail::from_u8(svc.instance);
                    si.device_id = detail::from_u8(device_id);
                    si.backend = u"escl";
                    si.is_network = true;
                    si.flatbed = true;
                    si.adf = false;
                    r.value.push_back(std::move(si));

                }
            }


        }

        // ═══════ SUBNET SCAN FALLBACK (last resort) ═══════
        // Fires only when mDNS returned empty twice → scan subnet (port 80 probe).
        // NOTE: devices that serve eSCL on port 8080 (like HP 7740) will NOT
        // be found here. This fallback covers port-80 eSCL devices only.
        if (r.value.empty()) {
            r = scan_local_subnet_();
        }

        // Cache successful results.
        if (r && !r.value.empty()) {
            std::lock_guard<std::mutex> lk(cache_m);
            cached = r.value;
            cached_at = std::chrono::steady_clock::now();
            
        }
        return r;
    }

    static result<scanner> open(const knst_c16string& device_id) {
        using R = result<scanner>;
        if (device_id.empty()) return R::fail(status::InvalidArgument);
        if (!detail::wsa_ensure_started()) return R::fail(status::IoError);
        scanner s;
        s.name_ = device_id;
        s.device_id_ = device_id;
        return R::success(std::move(s));
    }

    const knst_c16string& name() const noexcept { return name_; }

    // ─── GET /eSCL/ScannerStatus ────────────────────────────────────
    result<bool> scanner_status() const {
        using R = result<bool>;
        auto resp = http_get(u"/eSCL/ScannerStatus");
        if (!resp) return R::fail(resp.st, resp.native);
        if (resp.value.status != 200) return R::fail(status::IoError, resp.value.status);
        std::string xml(reinterpret_cast<const char*>(resp.value.body.data()),
                        resp.value.body.length());
        return R::success(xml.find("Idle") != std::string::npos);
    }

    // ─── GET /eSCL/ScannerCapabilities ──────────────────────────────
    result<scanner_capabilities> capabilities() const {
        using R = result<scanner_capabilities>;
        auto resp = http_get(u"/eSCL/ScannerCapabilities");
        if (!resp) return R::fail(resp.st, resp.native);
        if (resp.value.status != 200) return R::fail(status::IoError, resp.value.status);

        R r;
        std::string xml(reinterpret_cast<const char*>(resp.value.body.data()),
                        resp.value.body.length());

        auto find_ints = [&](const char* tag) {
            std::vector<uint32_t> vals;
            std::string open_tag = std::string("<") + tag;
            std::string close_tag = std::string("</") + tag + ">";
            size_t pos = 0;
            while ((pos = xml.find(open_tag, pos)) != std::string::npos) {
                size_t gt = xml.find('>', pos);
                if (gt == std::string::npos) break;
                size_t end = xml.find(close_tag, gt);
                if (end == std::string::npos) break;
                vals.push_back((uint32_t)std::atoi(xml.substr(gt + 1, end - gt - 1).c_str()));
                pos = end + close_tag.size();
            }
            return vals;
        };
        auto res_x = find_ints("scan:XResolution");
        auto res_y = find_ints("scan:YResolution");
        for (size_t i = 0; i < res_x.size() && i < res_y.size(); ++i)
            if (res_x[i] > 0 && res_x[i] == res_y[i])
                r.value.resolutions.push_back(res_x[i]);
        // Remove duplicates.
        std::sort(r.value.resolutions.begin(), r.value.resolutions.end());
        r.value.resolutions.erase(
            std::unique(r.value.resolutions.begin(), r.value.resolutions.end()),
            r.value.resolutions.end());
        if (r.value.resolutions.empty()) r.value.resolutions = {100, 200, 300, 600};

        auto find_str = [&](const char* tag) {
            std::vector<std::string> vals;
            std::string open_tag = std::string("<") + tag + ">";
            std::string close_tag = std::string("</") + tag + ">";
            size_t pos = 0;
            while ((pos = xml.find(open_tag, pos)) != std::string::npos) {
                size_t gt = xml.find('>', pos);
                size_t end = xml.find(close_tag, gt);
                if (gt == std::string::npos || end == std::string::npos) break;
                vals.push_back(xml.substr(gt + 1, end - gt - 1));
                pos = end + close_tag.size();
            }
            return vals;
        };

                for (auto& v : find_str("pwg:InputSource")) {
            if (v == "Platen")       { r.value.has_flatbed = true; r.value.sources.push_back(u"Platen"); }
            else if (v == "Feeder")  { r.value.has_adf = true; r.value.sources.push_back(u"Feeder"); }
            else if (v == "FeederDuplex") { r.value.has_duplex_adf = true; r.value.sources.push_back(u"FeederDuplex"); }
        }

                // Some printers (e.g. HP 7740) report the flatbed via <scan:Platen>.
        if (xml.find("<scan:Platen>") != std::string::npos ||
            xml.find("<scan:PlatenInputCaps>") != std::string::npos) {
            r.value.has_flatbed = true;
            if (std::find(r.value.sources.begin(), r.value.sources.end(), u"Platen")
                    == r.value.sources.end()) {
                r.value.sources.push_back(u"Platen");
            }
        }
        if (!find_str("scan:AdfSimplexInputCaps").empty() ||
            !find_str("scan:AdfDuplexInputCaps").empty()) {
            r.value.has_adf = true;
            if (std::find(r.value.sources.begin(), r.value.sources.end(), u"Feeder") == r.value.sources.end())
                r.value.sources.push_back(u"Feeder");
        }
        for (auto& v : find_str("scan:ColorMode")) {
            if (v == "RGB24")            r.value.modes.push_back(color_mode::Color);
            else if (v == "Grayscale8")  r.value.modes.push_back(color_mode::Gray);
            else if (v == "BlackAndWhite1") r.value.modes.push_back(color_mode::Mono);
        }
        if (r.value.modes.empty())
            r.value.modes = {color_mode::Color, color_mode::Gray, color_mode::Mono};

        return r;
    }

        // ══════════════════════════════════════════════════════════════════
    //  scan_jpegs — internal helper (eSCL native JPEG output)
    // ══════════════════════════════════════════════════════════════════
    result<knst_vector<knst_c16string>> scan_jpegs(detail::temp_set& tmp,
                                                    const scan_options& o = scan_options()) const {
        using R = result<knst_vector<knst_c16string>>;
        std::string settings = build_scan_settings(o);
        result<http_response> post;
        bool post_ok = false;
        for (int attempt = 0; attempt < 5; ++attempt) {
            post = http_post(u"/eSCL/ScanJobs", "text/xml", settings);
            if (!post) return R::fail(post.st, post.native);
            if (post.value.status == 201 || post.value.status == 200) { post_ok = true; break; }
            if (post.value.status == 503) {

                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            return R::fail(status::IoError, post.value.status);
        }
        if (!post_ok) {

            return R::fail(status::IoError, post.value.status);
        }

        knst_c16string loc = post.value.header_value("Location");
        if (loc.empty()) return R::fail(status::IoError);
        std::string loc_path = detail::to_u8(loc);
        size_t scheme = loc_path.find("://");
        if (scheme != std::string::npos) {
            size_t slash = loc_path.find('/', scheme + 3);
            if (slash != std::string::npos) loc_path = loc_path.substr(slash);
        }

        R out;
        // page_count == 0 means "until the feeder is empty". Use a generous
        // cap (500) — most ADFs hold 50-100 sheets; 500 covers high-volume
        // units and is safe against runaway loops if a printer misbehaves.
        const uint32_t max_pages = o.page_count > 0 ? o.page_count : 500;
        for (uint32_t page = 0; page < max_pages; ++page) {
            bool got_doc = false;
            for (int attempt = 0; attempt < 120; ++attempt) {   // 120 s per page max
                auto doc = http_get(loc_path + "/NextDocument");
                if (!doc) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
                if (doc.value.status == 503) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
                if (doc.value.status == 404 || doc.value.status == 410) break;  // feeder empty
                if (doc.value.status != 200 || doc.value.body.empty()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1)); continue;
                }
                knst_c16string jp = tmp.add(u".jpg");
                if (!knst_file::write_file_atomic(jp, doc.value.body.data(), doc.value.body.length()))
                    break;
                out.value.push_back(jp);
                got_doc = true;
                
                break;
            }
            if (!got_doc) break;
            if (!o.use_adf) break;
        }

        if (out.value.empty()) {
            return R::fail(status::IoError);
        }
        return out;
    }

    result<knst_c16string> scan_to_pdf(const knst_c16string& path,
                                       const scan_options& o = scan_options()) {
        using R = result<knst_c16string>;
        if (path.empty()) return R::fail(status::InvalidArgument);

        detail::temp_set tmp;
        auto jpegs = scan_jpegs(tmp, o);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);

        float w = 210.0f, h = 297.0f;
        detail::paper_mm(o.paper, w, h);
        std::vector<knst_c16string> v(jpegs.value.begin(), jpegs.value.end());
        image_batch_options bo;
        bo.margin_mm = 0.0f;
        bo.fit = fit_mode::Fit;
        bo.apply_exif = false;
        bo.skip_unreadable = false;
        bo.max_dpi = std::max<uint32_t>(150, o.dpi);
        detail::document doc;
        auto st = detail::layout_images(v, bo, w, h, doc);
        if (st != status::Ok) return R::fail(st);
        knst_c16string abs = knst_file::resolve_path(path);
        st = detail::write_pdf(doc, abs);
        if (st != status::Ok) return R::fail(st);
      
        return R::success(abs);
    }

    result<knst_byte_string> scan_page(const scan_options& o = scan_options()) {
        using R = result<knst_byte_string>;
        detail::temp_set tmp;
        scan_options oo = o; oo.page_count = 1;
        auto jpegs = scan_jpegs(tmp, oo);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);
        knst_file_error fe = knst_file_error::None;
        knst_byte_string b = knst_file::read_file_data<>(jpegs.value[0], &fe);
        if (fe != knst_file_error::None) return R::fail(status::IoError);
        return R::success(std::move(b));
    }
    result<knst_c16string> scan_to_file(const knst_c16string& path,
                                        const scan_options& o = scan_options()) {
        using R = result<knst_c16string>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        std::string fmt = detail::lower(detail::to_u8(o.output_format));
        if (fmt == "pdf") return scan_to_pdf(path, o);

        detail::temp_set tmp;
        auto jpegs = scan_jpegs(tmp, o);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);

                                                    knst_c16string abs = knst_file::resolve_path(path);
        const bool want_png = (fmt == "png");
        const std::string actual_ext = want_png ? ".png" : ".jpg";
        if (jpegs.value.size() == 1) {
            if (want_png) {
                // Real PNG: JPEG → decode → write PNG
                if (!detail::decode_to_png(jpegs.value[0], abs))
                    return R::fail(status::DecodeFailed);
                        } else {
                if (!knst_file::move_path(jpegs.value[0], abs, true))
                    return R::fail(status::IoError);
            }
            return R::success(abs);
        }

        // Multi-page (ADF): prefix_001.png/.jpg, prefix_002.png/.jpg ...
        knst_c16string parent = knst_file::get_parent_dir(abs);
        std::string stem = detail::to_u8(knst_file::get_stem(abs));
        if (stem.empty()) stem = "scan";
        knst_c16string first;
        for (size_t i = 0; i < jpegs.value.size(); ++i) {
            char suffix[20];
            std::snprintf(suffix, sizeof(suffix), "_%03u%s", (unsigned)(i + 1), actual_ext.c_str());
            knst_c16string dst = knst_file::join_path(parent, detail::from_u8(stem + suffix));
            if (want_png) {
                if (!detail::decode_to_png(jpegs.value[i], dst))
                    return R::fail(status::DecodeFailed);
            } else {
                if (!knst_file::move_path(jpegs.value[i], dst, true))
                    return R::fail(status::IoError);
            }
            if (i == 0) first = dst;
        }
        return R::success(first);
    }
    result<knst_vector<knst_c16string>> scan_to_files(const knst_c16string& prefix_path,
                                                       const scan_options& o = scan_options()) {
        using R = result<knst_vector<knst_c16string>>;
        if (prefix_path.empty()) return R::fail(status::InvalidArgument);
        std::string fmt = detail::lower(detail::to_u8(o.output_format));
        if (fmt == "pdf") return R::fail(status::InvalidArgument);

        detail::temp_set tmp;
        auto jpegs = scan_jpegs(tmp, o);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);

        knst_c16string abs = knst_file::resolve_path(prefix_path);
        knst_c16string parent = knst_file::get_parent_dir(abs);
        if (parent.empty()) parent = knst_file::get_temp_dir();
        std::string stem = detail::to_u8(knst_file::get_stem(abs));
        if (stem.empty()) stem = "scan";

        const bool want_png = (fmt == "png");
        const std::string actual_ext = want_png ? ".png" : ".jpg";

        R out;
        for (size_t i = 0; i < jpegs.value.size(); ++i) {
            char suffix[20];
            std::snprintf(suffix, sizeof(suffix), "_%03u%s", (unsigned)(i + 1), actual_ext.c_str());
            knst_c16string dst = knst_file::join_path(parent, detail::from_u8(stem + suffix));
            if (want_png) {
                if (!detail::decode_to_png(jpegs.value[i], dst))
                    return R::fail(status::DecodeFailed);
            } else {
                if (!knst_file::move_path(jpegs.value[i], dst, true))
                    return R::fail(status::IoError);
            }
            out.value.push_back(dst);
        }
        return out;
    }

    result<uint32_t> scan_and_print(const print_options& popt = print_options(),
                                     const scan_options& sopt = scan_options()) {
        auto pr = printer::open_any();
        if (!pr) return result<uint32_t>::fail(pr.st, pr.native);
        return scan_and_print_to(pr.value, popt, sopt);
    }
        result<uint32_t> scan_and_print_to(printer& pr, const print_options& popt = print_options(),
                                        const scan_options& sopt = scan_options()) {
        using R = result<uint32_t>;
        detail::temp_set tmp;
        auto jpegs = scan_jpegs(tmp, sopt);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);

        std::vector<knst_c16string> v(jpegs.value.begin(), jpegs.value.end());
        image_batch_options bo;
        bo.margin_mm = popt.borderless ? 0.0f : 5.0f;
        bo.fit = fit_mode::Fit;
        bo.apply_exif = false;
        bo.skip_unreadable = false;

       
        return pr.print_images(v, bo, popt);
    }
private:
    knst_c16string name_, device_id_;

    struct http_response {
        int status = 0;
        std::string headers;
        knst_byte_string body;
        knst_c16string header_value(const char* name) const {
            std::string n(name);
            for (size_t i = 0; i + n.size() <= headers.size(); ++i) {
                bool match = true;
                for (size_t j = 0; j < n.size(); ++j) {
                    char a = (char)std::tolower((unsigned char)headers[i + j]);
                    char b = (char)std::tolower((unsigned char)n[j]);
                    if (a != b) { match = false; break; }
                }
                if (match && i + n.size() < headers.size() && headers[i + n.size()] == ':') {
                    size_t v = i + n.size() + 1;
                    while (v < headers.size() && (headers[v] == ' ' || headers[v] == '\t')) ++v;
                    size_t e = v;
                    while (e < headers.size() && headers[e] != '\r' && headers[e] != '\n') ++e;
                    return knst_c16string(headers.substr(v, e - v).c_str());
                }
            }
            return knst_c16string();
        }
    };

        // Scans the local /24 subnet and returns hosts that answer
    // /eSCL/ScannerStatus. Runs 32 parallel threads with a 400 ms
    // connect timeout — roughly 5-10 s total.
       static result<knst_vector<scanner_info>> scan_local_subnet_() {
        using R = result<knst_vector<scanner_info>>;
        R out;

                // 1) Find our local IP (8.8.8.8 probe + gethostname fallback).
        sockaddr_in local{};
        SOCKET probe = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (probe != INVALID_SOCKET) {
            sockaddr_in dst{}; dst.sin_family = AF_INET;
            dst.sin_port = htons(80);
            dst.sin_addr.s_addr = inet_addr("8.8.8.8");
            if (::connect(probe, (sockaddr*)&dst, sizeof(dst)) == 0) {
                int len = sizeof(local);
                ::getsockname(probe, (sockaddr*)&local, &len);
            }
            ::closesocket(probe);
        }
        // If the 8.8.8.8 probe fails (offline VPN route, ...), fall back to gethostname.
        if (local.sin_addr.s_addr == 0) {
            char hostname[256] = {0};
            if (::gethostname(hostname, sizeof(hostname) - 1) == 0 && hostname[0]) {
                struct addrinfo hints{};
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                struct addrinfo* res = nullptr;
                if (::getaddrinfo(hostname, nullptr, &hints, &res) == 0 && res) {
                    uint32_t best = 0; int best_prio = 999;
                    for (auto* ai = res; ai; ai = ai->ai_next) {
                        auto* sa = reinterpret_cast<sockaddr_in*>(ai->ai_addr);
                        uint32_t ip = ntohl(sa->sin_addr.s_addr);
                        uint8_t a = (ip >> 24) & 0xFF, b = (ip >> 16) & 0xFF;
                        int prio = 999;
                        if (a == 192 && b == 168) prio = 0;                // home LAN
                        else if (a == 10) prio = 1;                        // corporate LAN
                        else if (a == 172 && b >= 16 && b <= 31) prio = 2; // Docker/private
                        if (prio < best_prio) { best_prio = prio; best = ip; }
                    }
                    ::freeaddrinfo(res);
                    if (best) local.sin_addr.s_addr = htonl(best);
                }
            }
        }
        if (local.sin_addr.s_addr == 0) {
           
            return out;
        }

        uint32_t base = ntohl(local.sin_addr.s_addr) & 0xFFFFFF00u;
      

        std::mutex m;
        const uint32_t total = 254;
        const uint32_t threads = 32;   // 32 parallel → ~4-5 seconds
        std::atomic<uint32_t> next_host{1};

        auto worker = [&]() {
            for (;;) {
                uint32_t h = next_host.fetch_add(1);
                if (h > total) break;
                uint32_t ip = base | h;
                char ipbuf[32];
                ::snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u",
                           (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF);

                // ─── Phase 1: quick TCP-80 probe (non-blocking, 400 ms) ───
                int fd = detail::ws_open(AF_INET, SOCK_STREAM, 0);
                if (fd < 0) continue;
                u_long nb = 1;
                ::ioctlsocket((SOCKET)fd, FIONBIO, &nb);
                struct sockaddr_in sa{};
                sa.sin_family = AF_INET;
                sa.sin_port = htons(80);
                sa.sin_addr.s_addr = htonl(ip);

                int rc = ::connect((SOCKET)fd, (sockaddr*)&sa, sizeof(sa));
                bool tcp_ok = (rc == 0);
                if (!tcp_ok) {
                    int e = ::WSAGetLastError();
                    if (e == WSAEWOULDBLOCK) {
                        WSAPOLLFD pfd{};
                        pfd.fd = (SOCKET)fd;
                        pfd.events = POLLWRNORM;
                        int pr = ::WSAPoll(&pfd, 1, 400);
                        if (pr > 0) {
                            int so = 0; int sl = sizeof(so);
                            ::getsockopt((SOCKET)fd, SOL_SOCKET, SO_ERROR, (char*)&so, &sl);
                            tcp_ok = (so == 0);
                        }
                    }
                }
                detail::ws_close(fd);
                if (!tcp_ok) continue;   // port 80 closed → skip

                // ─── Phase 2: eSCL ScannerStatus probe ───
                std::string hostport = std::string(ipbuf) + ":80";
                scanner s; s.device_id_ = detail::from_u8(hostport);
                auto r = s.http_get(u"/eSCL/ScannerStatus");
                if (!r || r.value.status != 200) continue;

                // ─── Phase 3: check the body for eSCL XML ───
                // Routers/modems can return 200 with HTML instead of 404
                // (captive portal, SPA). A real eSCL reply is XML:
                // <scan:ScannerStatus or <pwg:ScannerStatus.
                std::string body(reinterpret_cast<const char*>(r.value.body.data()),
                                 r.value.body.length());
                bool is_escl = (body.find("ScannerStatus") != std::string::npos) ||
                               (body.find("<scan:")        != std::string::npos) ||
                               (body.find("<pwg:")         != std::string::npos) ||
                               (body.find("Idle")          != std::string::npos);
                if (!is_escl) {
                    continue;
                }

                scanner_info si;
                si.name = detail::from_u8(hostport);
                si.device_id = detail::from_u8(hostport);
                si.backend = u"escl";
                si.is_network = true; si.flatbed = true;
                std::lock_guard<std::mutex> lk(m);
                out.value.push_back(std::move(si));
               
            }
        };
        std::vector<std::thread> ts;
        for (uint32_t i = 0; i < threads; ++i) ts.emplace_back(worker);
        for (auto& t : ts) t.join();
      
        return out;
    }

    result<http_response> http_request(const knst_c16string& method,
                                       const knst_c16string& path,
                                       const std::string& content_type,
                                       const std::string& body) const {
        using R = result<http_response>;
        std::string hp = detail::to_u8(device_id_);
        size_t colon = hp.rfind(':');
        if (colon == std::string::npos) return R::fail(status::InvalidArgument);
        std::string host = hp.substr(0, colon);
        int port = std::atoi(hp.c_str() + colon + 1);
        if (port <= 0) port = 80;

        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = nullptr;
        std::string port_str = std::to_string(port);
                int gai_rc = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
        int fd = -1;

        // Fallback 1: LLMNR — getaddrinfo cannot resolve suffixes like
        // ".modem.local"; try the first label ("HPB664C4").
        if (gai_rc != 0 || !res) {
            res = nullptr;
            size_t dot = host.find('.');
            if (dot != std::string::npos) {
                std::string llmnr_host = host.substr(0, dot);
                gai_rc = ::getaddrinfo(llmnr_host.c_str(), port_str.c_str(), &hints, &res);
               
            }
        }

        // If getaddrinfo (full or LLMNR) returned a result, connect normally.
        if (gai_rc == 0 && res) {
            for (auto* ai = res; ai; ai = ai->ai_next) {
                fd = detail::ws_open(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
                if (fd < 0) continue;
                DWORD tv_ms = 60000;
                ::setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_ms, sizeof(tv_ms));
                ::setsockopt((SOCKET)fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv_ms, sizeof(tv_ms));
                if (detail::ws_connect(fd, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
                detail::ws_close(fd); fd = -1;
            }
            ::freeaddrinfo(res);
        }

        // Fallback 2: mDNS — A-record query for ".local" hostnames.
        // Windows getaddrinfo does not do mDNS; Linux/Android use
        // avahi/NsdManager instead.
        if (fd < 0 && host.size() >= 6 &&
            host.compare(host.size() - 6, 6, ".local") == 0) {
           
            std::vector<std::string> mdns_ips;
            if (knst_mdns::resolve_local(host, mdns_ips, 2000)) {
                
                for (size_t mi = 0; mi < mdns_ips.size(); ++mi) {
                    struct sockaddr_in sa{};
                    sa.sin_family = AF_INET;
                    sa.sin_port = htons((uint16_t)port);
                    ::inet_pton(AF_INET, mdns_ips[mi].c_str(), &sa.sin_addr);
                    fd = detail::ws_open(AF_INET, SOCK_STREAM, 0);
                    if (fd < 0) continue;
                    DWORD tv_ms = 60000;
                    ::setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_ms, sizeof(tv_ms));
                    ::setsockopt((SOCKET)fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv_ms, sizeof(tv_ms));
                    if (detail::ws_connect(fd, (sockaddr*)&sa, sizeof(sa)) == 0) {
                       
                        break;
                    }
                    detail::ws_close(fd); fd = -1;
                }
            }
        }
        if (fd < 0) {
            return R::fail(status::NotFound);
        }

        std::string method_s = detail::to_u8(method);
        std::string path_s = detail::to_u8(path);
        std::string req = method_s + " " + path_s + " HTTP/1.1\r\n";
        req += "Host: " + host + ":" + port_str + "\r\n";
        req += "Connection: close\r\n";
        req += "User-Agent: KernelNucleusT/1.0\r\n";
        req += "Accept: */*\r\n";
        if (!content_type.empty()) req += "Content-Type: " + content_type + "\r\n";
        if (!body.empty()) req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        else if (method_s == "POST") req += "Content-Length: 0\r\n";
        req += "\r\n";
        req += body;

        const char* p = req.c_str();
        size_t left = req.size();
        while (left > 0) {
            int n = detail::ws_send(fd, p, (int)left);
            if (n <= 0) { detail::ws_close(fd); return R::fail(status::IoError); }
            p += n; left -= (size_t)n;
        }

        std::string raw;
        char buf[16384];
        size_t header_end = std::string::npos;
        size_t content_length = std::string::npos;
        bool chunked = false;

        for (;;) {
            int n = detail::ws_recv(fd, buf, sizeof(buf));
            if (n <= 0) break;
            raw.append(buf, (size_t)n);

            if (header_end == std::string::npos) {
                header_end = raw.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    std::string headers = raw.substr(0, header_end);
                    std::string low = headers;
                    for (auto& c : low) c = (char)std::tolower((unsigned char)c);
                    size_t cl = low.find("content-length:");
                    if (cl != std::string::npos) {
                        size_t vstart = cl + 15;
                        while (vstart < headers.size() &&
                               (headers[vstart] == ' ' || headers[vstart] == '\t')) ++vstart;
                        content_length = std::strtoul(headers.c_str() + vstart, nullptr, 10);
                    }
                    size_t te = low.find("transfer-encoding:");
                    if (te != std::string::npos && low.find("chunked", te) != std::string::npos)
                        chunked = true;
                }
            }

            if (header_end != std::string::npos) {
                size_t body_have = raw.size() - (header_end + 4);
                if (!chunked && content_length != std::string::npos && body_have >= content_length) break;
                if (chunked && body_have >= 5) {
                    const char* tail = raw.data() + raw.size() - 5;
                    if (std::memcmp(tail, "0\r\n\r\n", 5) == 0) break;
                }
            }
            if (raw.size() > 64 * 1024 * 1024) break;
        }
        detail::ws_close(fd);

        if (header_end == std::string::npos) return R::fail(status::IoError);

        http_response r;
        r.headers = raw.substr(0, header_end);
        std::string body_raw = raw.substr(header_end + 4);
        if (chunked) {
            std::string decoded;
            size_t pos = 0;
            while (pos < body_raw.size()) {
                size_t eol = body_raw.find("\r\n", pos);
                if (eol == std::string::npos) break;
                std::string szstr = body_raw.substr(pos, eol - pos);
                size_t semi = szstr.find(';');
                if (semi != std::string::npos) szstr = szstr.substr(0, semi);
                unsigned long sz = std::strtoul(szstr.c_str(), nullptr, 16);
                pos = eol + 2;
                if (sz == 0) break;
                if (pos + sz > body_raw.size()) { decoded.append(body_raw, pos, body_raw.size() - pos); break; }
                decoded.append(body_raw, pos, sz);
                pos += sz;
                if (pos + 2 <= body_raw.size() && body_raw[pos] == '\r' && body_raw[pos + 1] == '\n') pos += 2;
            }
            body_raw = std::move(decoded);
        } else if (content_length != std::string::npos && body_raw.size() > content_length) {
            body_raw.resize(content_length);
        }

        r.body = knst_byte_string(reinterpret_cast<const unsigned char*>(body_raw.data()),
                                  (uint32_t)body_raw.size());
        size_t sp = r.headers.find(' ');
        if (sp != std::string::npos) r.status = std::atoi(r.headers.c_str() + sp + 1);
        return R::success(std::move(r));
    }

    result<http_response> http_get(const knst_c16string& path) const {
        return http_request(u"GET", path, std::string(), std::string());
    }
    result<http_response> http_post(const knst_c16string& path,
                                     const std::string& ct,
                                     const std::string& body) const {
        return http_request(u"POST", path, ct, body);
    }

    std::string build_scan_settings(const scan_options& o) const {
        float w_mm = 210.0f, h_mm = 297.0f;
        detail::paper_mm(o.paper, w_mm, h_mm);
        uint32_t w_region = (uint32_t)(w_mm / 25.4f * 300.0f + 0.5f);
        uint32_t h_region = (uint32_t)(h_mm / 25.4f * 300.0f + 0.5f);
        uint32_t dpi = o.dpi ? o.dpi : 300;

        const char* source = "Platen";
        if (o.use_adf) source = o.duplex ? "FeederDuplex" : "Feeder";
        if (!o.source.empty()) {
            std::string s = detail::to_u8(o.source);
            if (s.find("Duplex") != std::string::npos) source = "FeederDuplex";
            else if (s == "ADF" || s == "Feeder")       source = "Feeder";
            else if (s == "Platen" || s == "Flatbed")   source = "Platen";
        }
        if (o.use_adf && source == std::string("ADF")) source = "Feeder";

        const char* cm = "RGB24";
        if (o.mode == color_mode::Gray)      cm = "Grayscale8";
        else if (o.mode == color_mode::Mono) cm = "BlackAndWhite1";

        std::string xml;
        xml.reserve(600);
        xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        xml += "<scan:ScanSettings xmlns:pwg=\"http://www.pwg.org/schemas/2010/12/sm\"";
        xml += " xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\">";
        xml += "<pwg:Version>2.0</pwg:Version>";
        xml += "<pwg:ScanRegions><pwg:ScanRegion>";
        xml += "<pwg:ContentRegionUnits>escl:ThreeHundredthsOfInches</pwg:ContentRegionUnits>";
        xml += "<pwg:XOffset>0</pwg:XOffset><pwg:YOffset>0</pwg:YOffset>";
        xml += "<pwg:Width>" + std::to_string(w_region) + "</pwg:Width>";
        xml += "<pwg:Height>" + std::to_string(h_region) + "</pwg:Height>";
        xml += "</pwg:ScanRegion></pwg:ScanRegions>";
        xml += std::string("<pwg:InputSource>") + source + "</pwg:InputSource>";
        xml += "<pwg:DocumentFormat>image/jpeg</pwg:DocumentFormat>";
        xml += "<scan:DocumentFormatExt>image/jpeg</scan:DocumentFormatExt>";
        xml += std::string("<scan:ColorMode>") + cm + "</scan:ColorMode>";
        xml += "<scan:XResolution>" + std::to_string(dpi) + "</scan:XResolution>";
        xml += "<scan:YResolution>" + std::to_string(dpi) + "</scan:YResolution>";
        xml += "</scan:ScanSettings>";
        return xml;
    }
};

#endif  // _MSC_VER
#endif  // _WIN32

#if defined(__ANDROID__)
// ===========================================================================
//                         Android implementation
// ===========================================================================

namespace detail {

struct android_jni {
    JavaVM* vm = nullptr;
    jobject activity = nullptr;

    static android_jni& get() { static android_jni s; return s; }

        void set(JavaVM* v, jobject a) {
        vm = v;
        if (!a) return;
        JNIEnv* e = nullptr;
        bool attached = false;
        if (v->GetEnv((void**)&e, JNI_VERSION_1_6) == JNI_EDETACHED) {
            v->AttachCurrentThread(&e, nullptr);
            attached = true;
        }
        if (!e) { if (attached) v->DetachCurrentThread(); return; }
        // Idempotent: same Java object → no-op.
        if (activity && e->IsSameObject(activity, a)) {
            if (attached) v->DetachCurrentThread();
            return;
        }
        if (activity) { e->DeleteGlobalRef(activity); activity = nullptr; }
        activity = e->NewGlobalRef(a);
        if (attached) v->DetachCurrentThread();
    }

    JNIEnv* env() {
        if (!vm) return nullptr;
        JNIEnv* e = nullptr;
        if (vm->GetEnv((void**)&e, JNI_VERSION_1_6) == JNI_EDETACHED)
            vm->AttachCurrentThread(&e, nullptr);
        return e;
    }
};

inline android_jni& jni() { return android_jni::get(); }

inline void set_jni_context(JavaVM* vm, jobject activity) { jni().set(vm, activity); }

inline knst_c16string jstr(JNIEnv* e, jstring s) {
    if (!s) return knst_c16string();
    const char* u = e->GetStringUTFChars(s, nullptr);
    knst_c16string r(u);
    e->ReleaseStringUTFChars(s, u);
    return r;
}

// ─── Rasterize a PDF into JPEG pages (Android PdfRenderer) ─────────────
// Returns the created JPEG file paths, or an empty list on failure.
inline std::vector<knst_c16string> rasterize_pdf_to_jpegs(
        const knst_c16string& pdf_path,
        const knst_c16string& out_dir,
        uint32_t target_dpi = 300,
        uint32_t jpeg_quality = 85) {
    std::vector<knst_c16string> result;
    JNIEnv* e = jni().env();
    if (!e || !jni().activity) return result;

    jclass helper = nullptr;
    {
        jclass act_cls = e->GetObjectClass(jni().activity);
        if (act_cls) {
            jmethodID get_cl = e->GetMethodID(act_cls, "getClassLoader",
                                               "()Ljava/lang/ClassLoader;");
            if (get_cl) {
                jobject cl = e->CallObjectMethod(jni().activity, get_cl);
                if (cl) {
                    jclass cl_cls = e->GetObjectClass(cl);
                    jmethodID load = e->GetMethodID(cl_cls, "loadClass",
                                                    "(Ljava/lang/String;)Ljava/lang/Class;");
                    if (load) {
                        jstring jn = e->NewStringUTF("com.knst.print.KnstPdfRasterizer");
                        helper = (jclass)e->CallObjectMethod(cl, load, jn);
                        e->DeleteLocalRef(jn);
                    }
                    e->DeleteLocalRef(cl_cls); e->DeleteLocalRef(cl);
                }
            }
            e->DeleteLocalRef(act_cls);
        }
        if (e->ExceptionCheck()) e->ExceptionClear();
    }
    if (!helper) {
        return result;
    }

    jmethodID m = e->GetStaticMethodID(helper, "rasterize",
        "(Ljava/lang/String;Ljava/lang/String;II)[Ljava/lang/String;");
    if (!m) {
        e->ExceptionClear();
        e->DeleteLocalRef(helper);
        return result;
    }

    knst_byte_string pdf_u8 = knst_dev::to_utf8(knst_file::resolve_path(pdf_path));
    knst_byte_string out_u8 = knst_dev::to_utf8(knst_file::resolve_path(out_dir));
    jstring jpdf  = e->NewStringUTF(reinterpret_cast<const char*>(pdf_u8.data()));
    jstring jout  = e->NewStringUTF(reinterpret_cast<const char*>(out_u8.data()));

    jobjectArray arr = (jobjectArray)e->CallStaticObjectMethod(
        helper, m, jpdf, jout, (jint)target_dpi, (jint)jpeg_quality);

    e->DeleteLocalRef(jpdf);
    e->DeleteLocalRef(jout);

    if (e->ExceptionCheck()) {
        e->ExceptionClear();
        e->DeleteLocalRef(helper);
        return result;
    }

    if (arr) {
        jsize n = e->GetArrayLength(arr);
        for (jsize i = 0; i < n; ++i) {
            jstring js = (jstring)e->GetObjectArrayElement(arr, i);
            if (js) {
                const char* u = e->GetStringUTFChars(js, nullptr);
                if (u) {
                    result.push_back(knst_c16string(u));
                    e->ReleaseStringUTFChars(js, u);
                }
                e->DeleteLocalRef(js);
            }
        }
        e->DeleteLocalRef(arr);
    }

    e->DeleteLocalRef(helper);

    return result;
}

// PrintManager dialog (PDF) — applies print_options: paper / color / duplex / dpi
inline bool print_pdf_dialog(const knst_c16string& pdf_path,
                             const print_options& opts) {
    JNIEnv* e = jni().env();
    if (!e || !jni().activity) return false;

    jclass ctx_cls = e->GetObjectClass(jni().activity);
    jmethodID get_service = e->GetMethodID(ctx_cls, "getSystemService",
                                           "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring svc_name = e->NewStringUTF("print");
    jobject pm = e->CallObjectMethod(jni().activity, get_service, svc_name);
    e->DeleteLocalRef(svc_name);
    if (!pm) { e->DeleteLocalRef(ctx_cls); return false; }

    // Resolve the KnstPrintAdapter class via the activity classloader.
    jclass adapter_cls = nullptr;
    {
        jclass act_cls = e->GetObjectClass(jni().activity);
        if (act_cls) {
            jmethodID get_cl = e->GetMethodID(act_cls, "getClassLoader",
                                              "()Ljava/lang/ClassLoader;");
            if (get_cl) {
                jobject cl = e->CallObjectMethod(jni().activity, get_cl);
                if (cl) {
                    jclass cl_cls = e->GetObjectClass(cl);
                    jmethodID load = e->GetMethodID(cl_cls, "loadClass",
                                                    "(Ljava/lang/String;)Ljava/lang/Class;");
                    if (load) {
                        jstring jn = e->NewStringUTF("com.knst.print.KnstPrintAdapter");
                        adapter_cls = (jclass)e->CallObjectMethod(cl, load, jn);
                        e->DeleteLocalRef(jn);
                    }
                    e->DeleteLocalRef(cl_cls); e->DeleteLocalRef(cl);
                }
            }
            e->DeleteLocalRef(act_cls);
        }
        if (e->ExceptionCheck()) e->ExceptionClear();
    }
    if (!adapter_cls) {
        e->DeleteLocalRef(pm); e->DeleteLocalRef(ctx_cls);
        return false;
    }

    // Adapter instance
    jmethodID ctor = e->GetMethodID(adapter_cls, "<init>",
                                    "(Ljava/lang/String;Ljava/lang/String;)V");
    knst_byte_string path_u8 = knst_dev::to_utf8(pdf_path);
    knst_c16string title = opts.title.empty() ? u"KNST print" : opts.title;
    knst_byte_string title_u8 = knst_dev::to_utf8(title);
    jstring jpath = e->NewStringUTF(reinterpret_cast<const char*>(path_u8.data()));
    jstring jname = e->NewStringUTF(reinterpret_cast<const char*>(title_u8.data()));
    jobject adapter = e->NewObject(adapter_cls, ctor, jpath, jname);
    e->DeleteLocalRef(jpath);
    if (!adapter) {
        e->DeleteLocalRef(jname);
        e->DeleteLocalRef(adapter_cls); e->DeleteLocalRef(pm); e->DeleteLocalRef(ctx_cls);
        return false;
    }

    // ── PrintAttributes.Builder ──
    jclass builder_cls = e->FindClass("android/print/PrintAttributes$Builder");
    if (!builder_cls) {
        e->ExceptionClear();
        e->DeleteLocalRef(jname); e->DeleteLocalRef(adapter);
        e->DeleteLocalRef(adapter_cls); e->DeleteLocalRef(pm); e->DeleteLocalRef(ctx_cls);
        return false;
    }
    jmethodID builder_ctor = e->GetMethodID(builder_cls, "<init>", "()V");
    jobject builder = e->NewObject(builder_cls, builder_ctor);

    // MediaSize
    {
        jclass ms_cls = e->FindClass("android/print/PrintAttributes$MediaSize");
        if (ms_cls) {
            const char* field = nullptr;
            switch (opts.paper) {
                case media_size::A3:        field = "ISO_A3";         break;
                case media_size::A4:        field = "ISO_A4";         break;
                case media_size::A5:        field = "ISO_A5";         break;
                case media_size::A6:        field = "ISO_A6";         break;
                case media_size::Letter:    field = "NA_LETTER";      break;
                case media_size::Legal:     field = "NA_LEGAL";       break;
                case media_size::Tabloid:   field = "NA_LEDGER";      break;
                case media_size::Executive: field = "NA_EXECUTIVE";   break;
                default:                    field = nullptr;          break;
            }
            if (field) {
                jfieldID fid = e->GetStaticFieldID(ms_cls, field,
                    "Landroid/print/PrintAttributes$MediaSize;");
                if (fid) {
                    jobject ms = e->GetStaticObjectField(ms_cls, fid);
                    if (ms) {
                        jmethodID set_media = e->GetMethodID(builder_cls, "setMediaSize",
                            "(Landroid/print/PrintAttributes$MediaSize;)Landroid/print/PrintAttributes$Builder;");
                        if (set_media) {
                            jobject r = e->CallObjectMethod(builder, set_media, ms);
                            if (r) e->DeleteLocalRef(r);
                        }
                        e->DeleteLocalRef(ms);
                    }
                }
            }
            e->DeleteLocalRef(ms_cls);
        }
        if (e->ExceptionCheck()) e->ExceptionClear();
    }

    // ColorMode
    {
        int mode = -1;
        if (opts.color == color_mode::Color) mode = 2;               // COLOR_MODE_COLOR
        else if (opts.color == color_mode::Mono ||
                 opts.color == color_mode::Gray) mode = 1;           // COLOR_MODE_MONOCHROME
        if (mode >= 0) {
            jmethodID set_color = e->GetMethodID(builder_cls, "setColorMode",
                "(I)Landroid/print/PrintAttributes$Builder;");
            if (set_color) {
                jobject r = e->CallObjectMethod(builder, set_color, (jint)mode);
                if (r) e->DeleteLocalRef(r);
            }
        }
        if (e->ExceptionCheck()) e->ExceptionClear();
    }

    // DuplexMode
    {
        int mode = 0;   // DUPLEX_MODE_NONE
        if (opts.duplex == duplex_mode::LongEdge)  mode = 1;
        else if (opts.duplex == duplex_mode::ShortEdge) mode = 2;
        jmethodID set_duplex = e->GetMethodID(builder_cls, "setDuplexMode",
            "(I)Landroid/print/PrintAttributes$Builder;");
        if (set_duplex) {
            jobject r = e->CallObjectMethod(builder, set_duplex, (jint)mode);
            if (r) e->DeleteLocalRef(r);
        }
        if (e->ExceptionCheck()) e->ExceptionClear();
    }


    if (!opts.resolution.empty()) {
        std::string res = to_u8(opts.resolution);
        int dpi = std::atoi(res.c_str());
        if (dpi <= 0) dpi = 300;
        jclass res_cls = e->FindClass("android/print/PrintAttributes$Resolution");
        if (res_cls) {
            jmethodID res_ctor = e->GetMethodID(res_cls, "<init>",
                "(Ljava/lang/String;Ljava/lang/String;II)V");
            if (res_ctor) {
                jstring rid    = e->NewStringUTF("knst_res");
                jstring rlabel = e->NewStringUTF("Custom");
                jobject res_obj = e->NewObject(res_cls, res_ctor,
                    rid, rlabel, (jint)dpi, (jint)dpi);
                e->DeleteLocalRef(rid); e->DeleteLocalRef(rlabel);
                if (res_obj) {
                    jmethodID set_res = e->GetMethodID(builder_cls, "setResolution",
                        "(Landroid/print/PrintAttributes$Resolution;)Landroid/print/PrintAttributes$Builder;");
                    if (set_res) {
                        jobject r = e->CallObjectMethod(builder, set_res, res_obj);
                        if (r) e->DeleteLocalRef(r);
                    }
                    e->DeleteLocalRef(res_obj);
                }
            }
            e->DeleteLocalRef(res_cls);
        }
        if (e->ExceptionCheck()) e->ExceptionClear();
    }

    // build()
    jmethodID build_attr = e->GetMethodID(builder_cls, "build",
        "()Landroid/print/PrintAttributes;");
    jobject attrs = build_attr ? e->CallObjectMethod(builder, build_attr) : nullptr;

    // PrintManager.print(jobName, adapter, attrs)
    jclass pm_cls = e->GetObjectClass(pm);
    jmethodID print_m = e->GetMethodID(pm_cls, "print",
        "(Ljava/lang/String;Landroid/print/PrintDocumentAdapter;Landroid/print/PrintAttributes;)Landroid/print/PrintJob;");
    jobject job = print_m ? e->CallObjectMethod(pm, print_m, jname, adapter, attrs) : nullptr;

    if (attrs)         e->DeleteLocalRef(attrs);
    e->DeleteLocalRef(builder); e->DeleteLocalRef(builder_cls);
    e->DeleteLocalRef(jname);
    e->DeleteLocalRef(adapter); e->DeleteLocalRef(adapter_cls);
    e->DeleteLocalRef(pm_cls);  e->DeleteLocalRef(pm);
    e->DeleteLocalRef(ctx_cls);
    return job != nullptr;
}

// USB raw print (bulk transfer, ESC/POS, PCL, ZPL)
// USB raw print (bulk transfer, ESC/POS, PCL, ZPL)
// Hardened version:
//   • Looks for CLASS=7 (Printer) or CLASS=0xFF (Vendor) instead of assuming interface 0
//   • Looks for a BULK-OUT (direction=OUT, type=BULK) endpoint instead of endpoint 0
//   • Splits large PDFs into 8 KB chunks for transmission
//   • No JNI ref leak in claimInterface/close
inline bool print_usb_raw(const knst_byte_string& data) {
    JNIEnv* e = jni().env();
    if (!e || !jni().activity) return false;
    if (data.empty()) return false;

    jclass ctx_cls = e->GetObjectClass(jni().activity);
    jmethodID get_service = e->GetMethodID(ctx_cls, "getSystemService",
                                           "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring svc_name = e->NewStringUTF("usb");
    jobject usb_mgr = e->CallObjectMethod(jni().activity, get_service, svc_name);
    e->DeleteLocalRef(svc_name);
    if (!usb_mgr) { e->DeleteLocalRef(ctx_cls); return false; }

    jclass mgr_cls = e->GetObjectClass(usb_mgr);
    jmethodID get_list = e->GetMethodID(mgr_cls, "getDeviceList", "()Ljava/util/HashMap;");
    jobject map = e->CallObjectMethod(usb_mgr, get_list);
    if (!map) { e->DeleteLocalRef(mgr_cls); e->DeleteLocalRef(usb_mgr); e->DeleteLocalRef(ctx_cls); return false; }

    jclass map_cls = e->GetObjectClass(map);
    jmethodID values = e->GetMethodID(map_cls, "values", "()Ljava/util/Collection;");
    jobject coll = e->CallObjectMethod(map, values);
    if (!coll) { e->DeleteLocalRef(map_cls); e->DeleteLocalRef(map); e->DeleteLocalRef(mgr_cls); e->DeleteLocalRef(usb_mgr); e->DeleteLocalRef(ctx_cls); return false; }

    jclass coll_cls = e->GetObjectClass(coll);
    jmethodID to_array = e->GetMethodID(coll_cls, "toArray", "()[Ljava/lang/Object;");
    jobjectArray arr = (jobjectArray)e->CallObjectMethod(coll, to_array);

    bool sent = false;
    if (arr) {
        jsize n = e->GetArrayLength(arr);
        for (jsize i = 0; i < n && !sent; ++i) {
            jobject dev = e->GetObjectArrayElement(arr, i);
            if (!dev) continue;
            jclass dev_cls = e->GetObjectClass(dev);

            // 1) Open the device
            jmethodID open = e->GetMethodID(mgr_cls, "openDevice",
                "(Landroid/hardware/usb/UsbDevice;)Landroid/hardware/usb/UsbDeviceConnection;");
            jobject conn = open ? e->CallObjectMethod(usb_mgr, open, dev) : nullptr;
            if (!conn) {
                e->DeleteLocalRef(dev_cls); e->DeleteLocalRef(dev);
                continue;
            }
            jclass conn_cls = e->GetObjectClass(conn);

            // 2) Scan interfaces — class 7 (Printer) or class 0xFF (Vendor)
            jmethodID get_iface_count = e->GetMethodID(dev_cls, "getInterfaceCount", "()I");
            jmethodID get_iface       = e->GetMethodID(dev_cls, "getInterface",
                                                        "(I)Landroid/hardware/usb/UsbInterface;");
                            jobject   chosen_iface    = nullptr;
            [[maybe_unused]] jint chosen_iface_id = -1;

            if (get_iface_count && get_iface) {
                jint iface_count = e->CallIntMethod(dev, get_iface_count);
                for (jint ii = 0; ii < iface_count; ++ii) {
                    jobject iface = e->CallObjectMethod(dev, get_iface, ii);
                    if (!iface) continue;
                    jclass iface_cls  = e->GetObjectClass(iface);
                    jmethodID get_cls = e->GetMethodID(iface_cls, "getInterfaceClass", "()I");
                    jint iface_class  = get_cls ? e->CallIntMethod(iface, get_cls) : -1;

                    if (iface_class == 7 || iface_class == 0xFF) {
                        chosen_iface    = iface;
                        chosen_iface_id = ii;
                        e->DeleteLocalRef(iface_cls);
                        break;                      // found it, stop
                    }
                    e->DeleteLocalRef(iface_cls);
                    e->DeleteLocalRef(iface);
                }
            }

            if (chosen_iface) {
                // 3) Find the BULK-OUT endpoint on this interface
                jclass    iface_cls    = e->GetObjectClass(chosen_iface);
                jmethodID get_ep_count = e->GetMethodID(iface_cls, "getEndpointCount", "()I");
                jmethodID get_ep       = e->GetMethodID(iface_cls, "getEndpoint",
                                            "(I)Landroid/hardware/usb/UsbEndpoint;");
                jobject   chosen_ep    = nullptr;

                if (get_ep_count && get_ep) {
                    jint ep_count = e->CallIntMethod(chosen_iface, get_ep_count);
                    for (jint ei = 0; ei < ep_count; ++ei) {
                        jobject ep = e->CallObjectMethod(chosen_iface, get_ep, ei);
                        if (!ep) continue;
                        jclass ep_cls    = e->GetObjectClass(ep);
                        jmethodID ep_dir  = e->GetMethodID(ep_cls, "getDirection", "()I");
                        jmethodID ep_type = e->GetMethodID(ep_cls, "getType", "()I");
                        jint dir  = ep_dir  ? e->CallIntMethod(ep, ep_dir)  : -1;
                        jint type = ep_type ? e->CallIntMethod(ep, ep_type) : -1;
                        // 0 = OUT, 2 = BULK
                        if (dir == 0 && type == 2) {
                            chosen_ep = ep;
                            e->DeleteLocalRef(ep_cls);
                            break;
                        }
                        e->DeleteLocalRef(ep_cls);
                        e->DeleteLocalRef(ep);
                    }
                }
                e->DeleteLocalRef(iface_cls);

                if (chosen_ep) {
                    // 4) claimInterface
                    jmethodID claim = e->GetMethodID(conn_cls, "claimInterface", "(Landroid/hardware/usb/UsbInterface;Z)Z");
                    jboolean claimed = claim ? e->CallBooleanMethod(conn, claim, chosen_iface, JNI_TRUE) : JNI_FALSE;
                    if (claimed) {
                        // 5) bulkTransfer — send in 8 KB chunks
                        jmethodID bulk = e->GetMethodID(conn_cls, "bulkTransfer",
                            "(Landroid/hardware/usb/UsbEndpoint;[BII)I");
                        if (bulk) {
                            const size_t kChunk = 8192;
                            sent = true;
                            for (size_t off = 0; off < data.length(); off += kChunk) {
                                size_t nbytes = std::min(kChunk, data.length() - off);
                                jbyteArray jdata = e->NewByteArray((jsize)nbytes);
                                e->SetByteArrayRegion(jdata, 0, (jsize)nbytes,
                                    reinterpret_cast<const jbyte*>(data.data() + off));
                                jint r = e->CallIntMethod(conn, bulk, chosen_ep, jdata,
                                                          (jint)nbytes, 5000);
                                e->DeleteLocalRef(jdata);
                                if (r != (jint)nbytes) {
                                  
                                    sent = false;
                                    break;
                                }
                            }
                            
                        }

                        // 6) releaseInterface
                        jmethodID release = e->GetMethodID(conn_cls, "releaseInterface",
                            "(Landroid/hardware/usb/UsbInterface;)Z");
                        if (release) e->CallBooleanMethod(conn, release, chosen_iface);
                    }

                    e->DeleteLocalRef(chosen_ep);
                } 
                e->DeleteLocalRef(chosen_iface);
            }

            // 7) close()
            jmethodID close_conn = e->GetMethodID(conn_cls, "close", "()V");
            if (close_conn) e->CallVoidMethod(conn, close_conn);

            e->DeleteLocalRef(conn_cls);
            e->DeleteLocalRef(conn);
            e->DeleteLocalRef(dev_cls);
            e->DeleteLocalRef(dev);
        }
        e->DeleteLocalRef(arr);
    }
    e->DeleteLocalRef(coll_cls); e->DeleteLocalRef(coll);
    e->DeleteLocalRef(map_cls); e->DeleteLocalRef(map);
    e->DeleteLocalRef(mgr_cls); e->DeleteLocalRef(usb_mgr);
    e->DeleteLocalRef(ctx_cls);
    return sent;
}

// Bluetooth SPP raw print
inline bool print_bluetooth_raw(const knst_byte_string& data, const knst_c16string& mac) {
    JNIEnv* e = jni().env();
    if (!e || !jni().activity) return false;

    jclass ctx_cls = e->GetObjectClass(jni().activity);
    jmethodID get_service = e->GetMethodID(ctx_cls, "getSystemService",
                                           "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring svc_name = e->NewStringUTF("bluetooth");
    jobject bt_mgr = e->CallObjectMethod(jni().activity, get_service, svc_name);
    e->DeleteLocalRef(svc_name);
    if (!bt_mgr) { e->DeleteLocalRef(ctx_cls); return false; }

    jclass mgr_cls = e->GetObjectClass(bt_mgr);
    jmethodID get_adapter = e->GetMethodID(mgr_cls, "getAdapter", "()Landroid/bluetooth/BluetoothAdapter;");
    jobject adapter = get_adapter ? e->CallObjectMethod(bt_mgr, get_adapter) : nullptr;
    if (!adapter) { e->DeleteLocalRef(mgr_cls); e->DeleteLocalRef(bt_mgr); e->DeleteLocalRef(ctx_cls); return false; }

    jclass ad_cls = e->GetObjectClass(adapter);
    jmethodID get_remote = e->GetMethodID(ad_cls, "getRemoteDevice",
        "(Ljava/lang/String;)Landroid/bluetooth/BluetoothDevice;");
    knst_byte_string mac_u8 = knst_dev::to_utf8(mac);
    jstring jmac = e->NewStringUTF(reinterpret_cast<const char*>(mac_u8.data()));
    jobject remote = get_remote ? e->CallObjectMethod(adapter, get_remote, jmac) : nullptr;
    e->DeleteLocalRef(jmac);

    bool sent = false;
    if (remote) {
        jclass rem_cls = e->GetObjectClass(remote);
        // UUID SPP: 00001101-0000-1000-8000-00805F9B34FB
        jclass uuid_cls = e->FindClass("java/util/UUID");
        jmethodID from_str = e->GetStaticMethodID(uuid_cls, "fromString",
            "(Ljava/lang/String;)Ljava/util/UUID;");
        jstring uuid_s = e->NewStringUTF("00001101-0000-1000-8000-00805F9B34FB");
        jobject uuid = e->CallStaticObjectMethod(uuid_cls, from_str, uuid_s);
        e->DeleteLocalRef(uuid_s);

        jmethodID create_sock = e->GetMethodID(rem_cls, "createRfcommSocketToServiceRecord",
            "(Ljava/util/UUID;)Landroid/bluetooth/BluetoothSocket;");
        jobject sock = create_sock ? e->CallObjectMethod(remote, create_sock, uuid) : nullptr;
        e->DeleteLocalRef(uuid); e->DeleteLocalRef(uuid_cls);

        if (sock) {
            jclass sock_cls = e->GetObjectClass(sock);
            jmethodID connect = e->GetMethodID(sock_cls, "connect", "()V");
            e->CallVoidMethod(sock, connect);
            if (!e->ExceptionCheck()) {
                jmethodID get_out = e->GetMethodID(sock_cls, "getOutputStream",
                    "()Ljava/io/OutputStream;");
                jobject out = get_out ? e->CallObjectMethod(sock, get_out) : nullptr;
                if (out) {
                    jclass out_cls = e->GetObjectClass(out);
                    jmethodID write = e->GetMethodID(out_cls, "write", "([B)V");
                    if (write) {
                        jbyteArray jdata = e->NewByteArray((jsize)data.length());
                        e->SetByteArrayRegion(jdata, 0, (jsize)data.length(),
                            reinterpret_cast<const jbyte*>(data.data()));
                        e->CallVoidMethod(out, write, jdata);
                        sent = !e->ExceptionCheck();
                        e->DeleteLocalRef(jdata);
                    }
                    jmethodID flush = e->GetMethodID(out_cls, "flush", "()V");
                    if (flush) e->CallVoidMethod(out, flush);
                    e->DeleteLocalRef(out_cls);
                    e->DeleteLocalRef(out);
                }
                jmethodID close_s = e->GetMethodID(sock_cls, "close", "()V");
                if (close_s) e->CallVoidMethod(sock, close_s);
            }
            e->ExceptionClear();
            e->DeleteLocalRef(sock_cls);
            e->DeleteLocalRef(sock);
        }
        e->DeleteLocalRef(rem_cls);
        e->DeleteLocalRef(remote);
    }
    e->DeleteLocalRef(ad_cls); e->DeleteLocalRef(adapter);
    e->DeleteLocalRef(mgr_cls); e->DeleteLocalRef(bt_mgr);
    e->DeleteLocalRef(ctx_cls);
    return sent;
}

} // namespace detail

class printer {
public:
    // ─── Open by URI or "host:port" ────────────────────────────────
    // Accepts: "192.168.1.5:631", "ipp://192.168.1.5/ipp/print",
    //          "ipps://192.168.1.5:631/ipp/print", "192.168.1.5"
    static result<printer> open(const knst_c16string& device_id) {
        using R = result<printer>;
        if (device_id.empty()) return R::fail(status::InvalidArgument);
        printer p;
        p.name_ = device_id;
        p.dev_id_ = device_id;
        if (!p._init_uri()) return R::fail(status::InvalidArgument);
        return R::success(std::move(p));
    }
        static result<printer> open_default() {
        using R = result<printer>;
        return R::fail(status::NotFound);
    }
    static result<printer> open_any() {
        auto all = list_all();
        if (!all) return result<printer>::fail(all.st, all.native);
        if (all.value.empty()) return result<printer>::fail(status::NotFound);
        return open(all.value[0].device_uri);
    }
    const knst_c16string& name() const noexcept { return name_; }

    // ─── status / info / capabilities / supply / alerts / usage ───
#if KNST_PRN_HAS_NETWORK
    result<queue_status> status() const {
        using R = result<queue_status>;
        auto r = _get_attrs({"printer-state", "printer-is-accepting-jobs",
                             "printer-state-reasons", "queued-job-count"});
        if (!r) return R::fail(r.st, r.native);
        R out;
        uint32_t ps = (uint32_t)::strtoul(
            detail::ipp::attr_get(r.value, "printer-state", "3").c_str(), nullptr, 10);
        switch (ps) {
            case 3: out.value.state = printer_state::Ready;  break;
            case 4: out.value.state = printer_state::Busy;   break;
            case 5: out.value.state = printer_state::Paused; break;
            default: out.value.state = printer_state::Unknown; break;
        }
        std::string acc = detail::ipp::attr_get(r.value, "printer-is-accepting-jobs", "true");
        out.value.accepting = (acc != "false" && acc != "0");
        out.value.enabled   = (ps != 5);
        out.value.job_count = (uint32_t)::strtoul(
            detail::ipp::attr_get(r.value, "queued-job-count", "0").c_str(), nullptr, 10);
        std::string reasons = detail::ipp::attr_get(r.value, "printer-state-reasons", "none");
        if (!reasons.empty() && reasons != "none") out.value.text = detail::from_u8(reasons);
        return out;
    }

    result<printer_info_ex> info() const {
        using R = result<printer_info_ex>;
        auto r = _get_attrs({"printer-name", "printer-info", "printer-location",
                             "printer-make-and-model", "printer-uri-supported",
                             "printer-state", "printer-state-reasons"});
        if (!r) return R::fail(r.st, r.native);
        R out;
        out.value.name = detail::from_u8(detail::ipp::attr_get(r.value, "printer-name"));
        if (out.value.name.empty()) out.value.name = name_;
        out.value.description    = detail::from_u8(detail::ipp::attr_get(r.value, "printer-info"));
        out.value.location       = detail::from_u8(detail::ipp::attr_get(r.value, "printer-location"));
        out.value.make_and_model = detail::from_u8(detail::ipp::attr_get(r.value, "printer-make-and-model"));
        out.value.model          = out.value.make_and_model;
        out.value.device_uri     = detail::from_u8(detail::ipp::attr_get(r.value, "printer-uri-supported"));
        if (out.value.device_uri.empty()) out.value.device_uri = name_;
        uint32_t ps = (uint32_t)::strtoul(
            detail::ipp::attr_get(r.value, "printer-state", "3").c_str(), nullptr, 10);
        out.value.state = (ps == 3) ? printer_state::Ready
                        : (ps == 4) ? printer_state::Busy
                        : (ps == 5) ? printer_state::Paused
                        : printer_state::Unknown;
        out.value.is_enabled   = (ps != 5);
        out.value.is_accepting = true;
        return out;
    }

    result<knst_c16string> make_and_model() const {
        auto i = info();
        if (!i) return result<knst_c16string>::fail(i.st, i.native);
        return result<knst_c16string>::success(i.value.make_and_model);
    }

    result<printer_capabilities> capabilities() const {
        using R = result<printer_capabilities>;
        auto r = _get_attrs({"printer-resolution-supported", "media-supported",
                             "sides-supported", "print-color-mode-supported",
                             "finishings-supported", "media-source-supported",
                             "output-bin-supported"});
        if (!r) return R::fail(r.st, r.native);
        R out;
        auto has = [&](const char* key, const char* needle) {
            return detail::ipp::attr_get(r.value, key).find(needle) != std::string::npos;
        };
        out.value.duplex_long_edge  = has("sides-supported", "two-sided-long-edge");
        out.value.duplex_short_edge = has("sides-supported", "two-sided-short-edge");
        out.value.color             = has("print-color-mode-supported", "color");
        out.value.stapling          = has("finishings-supported", "3");
        out.value.punching          = has("finishings-supported", "4");
        out.value.booklet           = has("finishings-supported", "10");
        auto split_kw = [&](const char* key, knst_vector<knst_c16string>& outv) {
            std::string s = detail::ipp::attr_get(r.value, key);
            size_t p = 0, n = s.size();
            while (p < n) {
                size_t comma = s.find(',', p);
                std::string tok = s.substr(p, comma == std::string::npos ? n - p : comma - p);
                p = comma == std::string::npos ? n : comma + 1;
                if (!tok.empty()) outv.push_back(detail::from_u8(tok));
            }
        };
        split_kw("media-supported",              out.value.supported_media);
        split_kw("printer-resolution-supported", out.value.supported_resolutions);
        split_kw("media-source-supported",       out.value.supported_sources);
        split_kw("output-bin-supported",         out.value.supported_output_bins);
        return out;
    }

    result<knst_vector<supply_level>> supply_levels() const {
        using R = result<knst_vector<supply_level>>;
        auto r = _get_attrs({"marker-levels", "marker-names",
                             "marker-types", "marker-low-levels"});
        if (!r) return R::fail(r.st, r.native);
        R out;
        std::string lv = detail::ipp::attr_get(r.value, "marker-levels");
        if (lv.empty()) return out;
        auto split = [](const std::string& s) {
            std::vector<std::string> v;
            size_t p = 0, n = s.size();
            while (p < n) {
                size_t comma = s.find(',', p);
                v.push_back(s.substr(p, comma == std::string::npos ? n - p : comma - p));
                p = comma == std::string::npos ? n : comma + 1;
            }
            return v;
        };
        auto levels = split(lv);
        auto names  = split(detail::ipp::attr_get(r.value, "marker-names"));
        auto types  = split(detail::ipp::attr_get(r.value, "marker-types"));
        auto lows   = split(detail::ipp::attr_get(r.value, "marker-low-levels"));
        for (size_t i = 0; i < levels.size(); ++i) {
            supply_level s;
            long pct = ::atol(levels[i].c_str());
            s.known = (pct >= 0 && pct <= 100);
            s.percent = s.known ? (uint32_t)pct : 0;
            if (i < names.size()) s.name = detail::from_u8(detail::trim(names[i]));
            if (i < types.size()) {
                s.raw_type = detail::from_u8(types[i]);
                std::string t = detail::lower(types[i]);
                if (t.find("waste-toner") != std::string::npos) s.type = supply_kind::WasteToner;
                else if (t.find("waste-ink") != std::string::npos) s.type = supply_kind::WasteInk;
                else if (t.find("toner") != std::string::npos) s.type = supply_kind::Toner;
                else if (t.find("ink") != std::string::npos) s.type = supply_kind::Ink;
                else if (t.find("drum") != std::string::npos) s.type = supply_kind::Drum;
                else if (t.find("fuser") != std::string::npos) s.type = supply_kind::Fuser;
                else if (t.find("ribbon") != std::string::npos) s.type = supply_kind::Ribbon;
                else s.type = supply_kind::Other;
            }
            long low = (i < lows.size()) ? ::atol(lows[i].c_str()) : -1;
            s.low = s.known && (low >= 0 ? (long)s.percent <= low : s.percent < 15);
            if (s.type == supply_kind::WasteToner || s.type == supply_kind::WasteInk) s.low = false;
            out.value.push_back(std::move(s));
        }
        return out;
    }

    result<knst_vector<printer_alert>> alerts() const {
        using R = result<knst_vector<printer_alert>>;
        auto r = _get_attrs({"printer-state-reasons", "marker-message"});
        if (!r) return R::fail(r.st, r.native);
        R out;
        std::string reasons = detail::ipp::attr_get(r.value, "printer-state-reasons");
        if (!reasons.empty() && reasons != "none") {
            size_t p = 0, n = reasons.size();
            while (p < n) {
                size_t comma = reasons.find(',', p);
                std::string tok = reasons.substr(p, comma == std::string::npos ? n - p : comma - p);
                p = comma == std::string::npos ? n : comma + 1;
                if (tok.empty() || tok == "none") continue;
                printer_alert a;
                a.code = detail::from_u8(tok);
                a.text = a.code;
                a.severity = (tok.size() > 6 && tok.compare(tok.size() - 6, 6, "-error") == 0) ? 2
                           : (tok.size() > 8 && tok.compare(tok.size() - 8, 8, "-warning") == 0) ? 1 : 0;
                out.value.push_back(std::move(a));
            }
        }
        std::string msg = detail::ipp::attr_get(r.value, "marker-message");
        if (!msg.empty()) {
            printer_alert a;
            a.code = u"marker-message";
            a.text = detail::from_u8(msg);
            out.value.push_back(std::move(a));
        }
        return out;
    }

    result<usage_stats> usage() const {
        result<usage_stats> out;
        auto p = jobs(false);
        if (p) for (auto& j : p.value) {
            out.value.pending_jobs++;
            out.value.total_jobs++;
            out.value.total_bytes += j.size_bytes;
            out.value.total_pages += j.pages;
        }
        return out;
    }

    // ─── jobs / job_details / cancel / wait ──────────────────────
    result<knst_vector<job_info>> jobs(bool include_completed) const {
        using R = result<knst_vector<job_info>>;
        if (hpu_.empty()) return R::fail(status::BadState);

        detail::ipp::request req;
        req.begin(detail::ipp::OP_GET_JOBS);
        req.add_string(detail::ipp::TAG_CHARSET,          "attributes-charset",         "utf-8");
        req.add_string(detail::ipp::TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
        req.add_string(detail::ipp::TAG_URI,              "printer-uri",                _ipp_uri());
        req.add_string(detail::ipp::TAG_NAME,             "requesting-user-name",       "knst");
        req.add_bool("my-jobs", false);
        req.add_string(detail::ipp::TAG_KEYWORD, "which-jobs",
                       include_completed ? "completed" : "not-completed");
        bool first = true;
        for (const char* a : {"job-id", "job-name", "job-state", "job-state-reasons",
                              "job-originating-user-name", "job-k-octets",
                              "job-media-sheets-completed", "job-priority",
                              "time-at-creation"}) {
            req.add_string(detail::ipp::TAG_KEYWORD,
                           first ? "requested-attributes" : std::string(), a);
            first = false;
        }
        req.end_of_attrs();

        detail::ipp::response resp;
        knst_network_error err = knst_network_error::None;
        if (!detail::ipp::send(hpu_, use_tls_, req.buf, 8000, resp, err))
            return R::fail(status::IoError);
        if (resp.status != 0x0000)
            return R::fail(status::IoError, resp.status);

        R out;
        job_info cur;
        bool have_cur = false;
        for (auto& kv : resp.attrs) {
            if (kv.first == "job-id") {
                if (have_cur) out.value.push_back(std::move(cur));
                cur = job_info{};
                cur.id = (uint32_t)::strtoul(kv.second.c_str(), nullptr, 10);
                cur.printer = name_;
                have_cur = true;
            } else if (have_cur && kv.first == "job-name") {
                cur.title = detail::from_u8(kv.second);
            } else if (have_cur && kv.first == "job-state") {
                cur.state = detail::ipp::job_state_from_uint(
                    (uint32_t)::strtoul(kv.second.c_str(), nullptr, 10));
            } else if (have_cur && kv.first == "job-state-reasons") {
                cur.message = detail::from_u8(kv.second);
            } else if (have_cur && kv.first == "job-originating-user-name") {
                cur.user = detail::from_u8(kv.second);
            } else if (have_cur && kv.first == "job-k-octets") {
                cur.size_bytes = (uint64_t)::strtoull(kv.second.c_str(), nullptr, 10) * 1024ull;
            } else if (have_cur && kv.first == "job-media-sheets-completed") {
                cur.pages = (uint32_t)::strtoul(kv.second.c_str(), nullptr, 10);
            } else if (have_cur && kv.first == "job-priority") {
                cur.priority = (uint32_t)::strtoul(kv.second.c_str(), nullptr, 10);
            } else if (have_cur && kv.first == "time-at-creation") {
                cur.submitted_unix = (uint64_t)::strtoull(kv.second.c_str(), nullptr, 10);
            }
        }
        if (have_cur) out.value.push_back(std::move(cur));
        return out;
    }

    result<job_info> job_details(uint32_t id) const {
        using R = result<job_info>;
        if (hpu_.empty() || id == 0) return R::fail(status::InvalidArgument);

        detail::ipp::request req;
        req.begin(detail::ipp::OP_GET_JOB_ATTRIBUTES);
        req.add_string(detail::ipp::TAG_CHARSET,          "attributes-charset",         "utf-8");
        req.add_string(detail::ipp::TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
        req.add_string(detail::ipp::TAG_URI,              "printer-uri",                _ipp_uri());
        req.add_int   (detail::ipp::TAG_INTEGER,          "job-id",                     id);
        req.add_string(detail::ipp::TAG_NAME,             "requesting-user-name",       "knst");
        req.add_string(detail::ipp::TAG_KEYWORD,          "requested-attributes",       "all");
        req.end_of_attrs();

        detail::ipp::response resp;
        knst_network_error err = knst_network_error::None;
        if (!detail::ipp::send(hpu_, use_tls_, req.buf, 8000, resp, err))
            return R::fail(status::IoError);
        if (resp.status != 0x0000)
            return R::fail(status::NotFound, resp.status);

        R out;
        out.value.id = id;
        out.value.printer = name_;
        out.value.title = detail::from_u8(detail::ipp::attr_get(resp, "job-name"));
        out.value.user  = detail::from_u8(detail::ipp::attr_get(resp, "job-originating-user-name"));
        out.value.state = detail::ipp::job_state_from_uint(
            (uint32_t)::strtoul(detail::ipp::attr_get(resp, "job-state", "0").c_str(), nullptr, 10));
        out.value.message = detail::from_u8(detail::ipp::attr_get(resp, "job-state-reasons"));
        out.value.size_bytes = (uint64_t)::strtoul(
            detail::ipp::attr_get(resp, "job-k-octets", "0").c_str(), nullptr, 10) * 1024ull;
        out.value.pages = (uint32_t)::strtoul(
            detail::ipp::attr_get(resp, "job-media-sheets-completed", "0").c_str(), nullptr, 10);
        out.value.submitted_unix = (uint64_t)::strtoul(
            detail::ipp::attr_get(resp, "time-at-creation", "0").c_str(), nullptr, 10);
        out.value.priority = (uint32_t)::strtoul(
            detail::ipp::attr_get(resp, "job-priority", "50").c_str(), nullptr, 10);
        return out;
    }

    result<void> cancel_job(uint32_t id) {
        using R = result<void>;
        if (hpu_.empty() || id == 0) return R::fail(status::InvalidArgument);

        detail::ipp::request req;
        req.begin(detail::ipp::OP_CANCEL_JOB);
        req.add_string(detail::ipp::TAG_CHARSET,          "attributes-charset",         "utf-8");
        req.add_string(detail::ipp::TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
        req.add_string(detail::ipp::TAG_URI,              "printer-uri",                _ipp_uri());
        req.add_int   (detail::ipp::TAG_INTEGER,          "job-id",                     id);
        req.add_string(detail::ipp::TAG_NAME,             "requesting-user-name",       "knst");
        req.end_of_attrs();

        detail::ipp::response resp;
        knst_network_error err = knst_network_error::None;
        if (!detail::ipp::send(hpu_, use_tls_, req.buf, 8000, resp, err))
            return R::fail(status::IoError);
        // RFC 8011: 0x0406 = not-possible (job already finished)
        if (resp.status == 0x0406) return R::fail(status::BadState, 0x0406);
    if (resp.status != 0x0000) return R::fail(status::IoError, resp.status);
    return R::success();
}



    result<void> cancel_all() {
        auto j = jobs(false);
        if (!j) return result<void>::fail(j.st, j.native);
        bool any = false;
        for (auto& jb : j.value) if (cancel_job(jb.id)) any = true;
        return any ? result<void>::success() : result<void>::fail(status::NotFound);
    }

    result<knst_vector<job_info>> jobs_by_user(const knst_c16string& user, bool incl = false) const {
        auto all = jobs(incl);
        if (!all) return result<knst_vector<job_info>>::fail(all.st, all.native);
        result<knst_vector<job_info>> out;
        for (auto& j : all.value) if (j.user == user) out.value.push_back(j);
        return out;
    }
    result<knst_vector<job_info>> completed_jobs(uint32_t limit = 20) const {
        auto all = jobs(true);
        if (!all) return result<knst_vector<job_info>>::fail(all.st, all.native);
        result<knst_vector<job_info>> out;
        size_t from = all.value.size() > limit ? all.value.size() - limit : 0;
        for (size_t i = from; i < all.value.size(); ++i) out.value.push_back(all.value[i]);
        return out;
    }
    result<void> hold_job(uint32_t id) {
        if (hpu_.empty() || id == 0) return result<void>::fail(status::InvalidArgument);
        // RFC 8011 §5.3.3 job-state: 4 = held. Best-effort via Set-Job-Attributes.
        return detail::ipp::set_job_attrs(hpu_, use_tls_, id, {{"job-state", 4}});
    }
    result<void> release_job(uint32_t id) {
        if (hpu_.empty() || id == 0) return result<void>::fail(status::InvalidArgument);
        // job-state: 3 = pending (released from held)
        return detail::ipp::set_job_attrs(hpu_, use_tls_, id, {{"job-state", 3}});
    }
    result<void> hold_all() {
        auto j = jobs(false);
        if (!j) return result<void>::fail(j.st, j.native);
        bool any = false;
        for (auto& jb : j.value) if (hold_job(jb.id)) any = true;
        return any ? result<void>::success() : result<void>::fail(status::NotFound);
    }
    result<void> release_all() {
        auto j = jobs(false);
        if (!j) return result<void>::fail(j.st, j.native);
        bool any = false;
        for (auto& jb : j.value) if (release_job(jb.id)) any = true;
        return any ? result<void>::success() : result<void>::fail(status::NotFound);
    }
    result<void> set_job_priority(uint32_t id, uint32_t prio) {
        if (hpu_.empty() || id == 0) return result<void>::fail(status::InvalidArgument);
        if (prio == 0 || prio > 100) return result<void>::fail(status::InvalidArgument);
        return detail::ipp::set_job_attrs(hpu_, use_tls_, id, {{"job-priority", prio}});
    }
    result<void> move_job(uint32_t, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> move_all_jobs(const knst_c16string&) { return result<void>::fail(status::NotSupported); }

    result<void> wait_for_job(uint32_t id, uint32_t timeout_ms = 60000) {
        using R = result<void>;
        if (hpu_.empty() || id == 0) return R::fail(status::InvalidArgument);
        auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            auto d = job_details(id);
            if (d) {
                if (d.value.state == job_state::Completed) return R::success();
                if (d.value.state == job_state::Canceled || d.value.state == job_state::Aborted)
                    return R::fail(status::BadState, static_cast<int>(d.value.state));
            } else if (d.st == status::NotFound) {
                return R::success();     // job aged out → treat as done
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            if ((uint32_t)elapsed >= timeout_ms) return R::fail(status::Timeout);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    result<bool> is_job_done(uint32_t id) const {
        using R = result<bool>;
        auto d = job_details(id);
        if (!d) {
            if (d.st == status::NotFound) return R::success(true);
            return R::fail(d.st, d.native);
        }
        return R::success(d.value.state == job_state::Completed ||
                          d.value.state == job_state::Canceled  ||
                          d.value.state == job_state::Aborted);
    }

    result<void> wait_for_ready(uint32_t timeout_ms = 30000) {
        auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            auto s = status();
            if (s && s.value.enabled && s.value.accepting &&
                (s.value.state == printer_state::Ready ||
                 s.value.state == printer_state::Busy  ||
                 s.value.state == printer_state::TonerLow))
                return result<void>::success();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            if ((uint32_t)elapsed >= timeout_ms) return result<void>::fail(status::Timeout);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    result<uint32_t> reprint_job(uint32_t id) {
        if (id == 0) return result<uint32_t>::fail(status::InvalidArgument);
        knst_c16string cached = detail::cache_find(name_, id);
        if (cached.empty()) return result<uint32_t>::fail(status::NotFound);
        return print_pdf_dialog(cached, print_options());
    }

    // Admin — not applicable to a single IPP printer.
    result<void> pause()    { return result<void>::fail(status::NotSupported); }
    result<void> resume()   { return result<void>::fail(status::NotSupported); }
    result<void> reject_jobs() { return result<void>::fail(status::NotSupported); }
    result<void> accept_jobs() { return result<void>::fail(status::NotSupported); }
    result<void> set_default() { return result<void>::fail(status::NotSupported); }
    result<knst_vector<printer_option>> options() const {
        return result<knst_vector<printer_option>>::fail(status::NotSupported);
    }
    result<void> set_default_option(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_option_permanent(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> remove_option(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_description(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_location(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_sharing(bool) { return result<void>::fail(status::NotSupported); }

    result<void> cancel_by_user(const knst_c16string& u) {
        auto j = jobs(false);
        if (!j) return result<void>::fail(j.st, j.native);
        bool any = false;
        for (auto& jb : j.value) if (jb.user == u && cancel_job(jb.id)) any = true;
        return any ? result<void>::success() : result<void>::fail(status::NotFound);
    }

private:
    // Send a Get-Printer-Attributes request.
    result<detail::ipp::response> _get_attrs(
            std::initializer_list<const char*> requested_attrs = {}) const {
        using R = result<detail::ipp::response>;
        if (hpu_.empty()) return R::fail(status::BadState);

        detail::ipp::request req;
        req.begin(detail::ipp::OP_GET_PRINTER_ATTRIBUTES);
        req.add_string(detail::ipp::TAG_CHARSET,          "attributes-charset",         "utf-8");
        req.add_string(detail::ipp::TAG_NATURAL_LANGUAGE, "attributes-natural-language", "en");
        req.add_string(detail::ipp::TAG_URI,              "printer-uri",                _ipp_uri());

        if (requested_attrs.size() > 0) {
            bool first = true;
            for (const char* a : requested_attrs) {
                req.add_string(detail::ipp::TAG_KEYWORD,
                               first ? "requested-attributes" : std::string(), a);
                first = false;
            }
        } else {
            req.add_string(detail::ipp::TAG_KEYWORD, "requested-attributes", "all");
        }
        req.end_of_attrs();

        detail::ipp::response resp;
        knst_network_error err = knst_network_error::None;
        if (!detail::ipp::send(hpu_, use_tls_, req.buf, 8000, resp, err))
            return R::fail(status::IoError);
        if (resp.status != 0x0000)
            return R::fail(status::IoError, resp.status);
        return R::success(std::move(resp));
    }

    std::string _ipp_uri() const {
        return std::string(use_tls_ ? "ipps" : "ipp") + "://" + hpu_ + "/ipp/print";
    }

    bool _init_uri() {
        hpu_.clear();
        use_tls_ = false;
        port_ = 631;
        if (!detail::ipp::split_uri(detail::to_u8(dev_id_), hpu_, use_tls_, port_))
            return false;
        return true;
    }

    knst_c16string name_;      // original device_id (for display)
    knst_c16string dev_id_;    // same
    std::string    hpu_;       // "host:port"
    bool           use_tls_ = false;
    uint16_t       port_ = 631;
#else
    // No knst_network — management is not possible.
    result<queue_status> status() const { return result<queue_status>::fail(status::NotSupported); }
    result<printer_info_ex> info() const { return result<printer_info_ex>::fail(status::NotSupported); }
    result<knst_c16string> make_and_model() const { return result<knst_c16string>::fail(status::NotSupported); }
    result<printer_capabilities> capabilities() const { return result<printer_capabilities>::fail(status::NotSupported); }
    result<knst_vector<supply_level>> supply_levels() const { return result<knst_vector<supply_level>>::fail(status::NotSupported); }
    result<knst_vector<printer_alert>> alerts() const { return result<knst_vector<printer_alert>>::fail(status::NotSupported); }
    result<usage_stats> usage() const { return result<usage_stats>::fail(status::NotSupported); }
    result<knst_vector<job_info>> jobs(bool = false) const { return result<knst_vector<job_info>>::fail(status::NotSupported); }
    result<job_info> job_details(uint32_t) const { return result<job_info>::fail(status::NotSupported); }
    result<void> cancel_job(uint32_t) { return result<void>::fail(status::NotSupported); }
    result<void> cancel_all() { return result<void>::fail(status::NotSupported); }
    result<void> cancel_by_user(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> wait_for_job(uint32_t, uint32_t = 60000) { return result<void>::fail(status::NotSupported); }
    result<bool> is_job_done(uint32_t) const { return result<bool>::fail(status::NotSupported); }
    result<void> wait_for_ready(uint32_t = 30000) { return result<void>::fail(status::NotSupported); }
    result<void> hold_job(uint32_t) { return result<void>::fail(status::NotSupported); }
    result<void> release_job(uint32_t) { return result<void>::fail(status::NotSupported); }
    result<void> hold_all() { return result<void>::fail(status::NotSupported); }
    result<void> release_all() { return result<void>::fail(status::NotSupported); }
    result<void> set_job_priority(uint32_t, uint32_t) { return result<void>::fail(status::NotSupported); }
    result<void> move_job(uint32_t, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> move_all_jobs(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<uint32_t> reprint_job(uint32_t) { return result<uint32_t>::fail(status::NotSupported); }
    result<void> pause() { return result<void>::fail(status::NotSupported); }
    result<void> resume() { return result<void>::fail(status::NotSupported); }
    result<void> reject_jobs() { return result<void>::fail(status::NotSupported); }
    result<void> accept_jobs() { return result<void>::fail(status::NotSupported); }
    result<void> set_default() { return result<void>::fail(status::NotSupported); }
    result<knst_vector<printer_option>> options() const { return result<knst_vector<printer_option>>::fail(status::NotSupported); }
    result<void> set_default_option(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_option_permanent(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> remove_option(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_description(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_location(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    result<void> set_sharing(bool) { return result<void>::fail(status::NotSupported); }

    knst_c16string name_;
    knst_c16string dev_id_;   // kept in sync with name_ — used by open() / _init_uri()

    bool _init_uri() { return true; }   // no knst_network — URI parse is a no-op
#endif

public:
    // ─── Existing print functions (unchanged) ──────────────────────
    result<uint32_t> print_pdf_dialog(const knst_c16string& path, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        if (!detail::print_pdf_dialog(abs, o)) return R::fail(status::IoError);
        static std::atomic<uint32_t> s_next_id{1};
        uint32_t id = s_next_id.fetch_add(1, std::memory_order_relaxed);
        if (o.cache_for_reprint) detail::cache_store(name_, id, abs);
        // Opportunistically clean stale temp PDFs left over from previous
        // print_pdf_dialog calls (PrintManager reads them asynchronously;
        // we cannot know when it's done — sweep anything older than 1 h).
        cleanup_stale_dialog_pdfs();
        return R::success(id);
    }

    // Removes temp PDFs left over from PrintManager dialogs (>1 h old).
    // Call this from onResume() or an app timer to keep storage bounded.
    static void cleanup_stale_dialog_pdfs() noexcept {
        knst_c16string dir = knst_file::get_temp_dir();
        if (dir.empty()) return;
        knst_vector<knst_file> entries;
        if (!knst_file::list_directory(dir, entries)) return;
        auto now = std::chrono::system_clock::now();
        for (auto& f : entries) {
            if (!f.is_file) continue;
            std::string nm = detail::to_u8(f.name);
            if (nm.compare(0, 10, "knst_print") != 0) continue;
            std::string e = detail::path_ext(f.name);
            if (e != ".pdf" && e != ".png" && e != ".jpg") continue;
            auto age = std::chrono::duration_cast<std::chrono::hours>(
                now - std::chrono::system_clock::from_time_t((std::time_t)f.modified_time));
            if (age.count() >= 1) knst_file::remove_file(f.path);
        }
    }
    // ─── IPP Print-Job (RFC 8010/8011) — universal network path ─────────
    // Supports: ipp:// (plain) and ipps:// (TLS) — knst_http_client handles
    // the TLS handshake, proxy CONNECT, DNS cache and mDNS ".local" lookups.
    // Attribute coverage: copies, sides (duplex), print-color-mode, media,
    // print-quality, orientation-requested, page-ranges, number-up,
    // finishings (staple/punch/fold), media-source, output-bin,
    // print-scaling / print-scaling-percent, multiple-document-handling,
    // job-sheets.
#if KNST_PRN_HAS_NETWORK
    static result<uint32_t> print_ipp(const knst_c16string& pdf_path,
                                       const knst_c16string& host_port,
                                       uint16_t port = 631,
                                       const print_options& opts = print_options(),
                                       const knst_c16string& doc_format = u"application/pdf") {
        return detail::ipp::print_pdf(pdf_path, host_port, port, opts, doc_format);
    }
#else
    static result<uint32_t> print_ipp(const knst_c16string& pdf_path,
                                       const knst_c16string& host_port,
                                       uint16_t port = 631,
                                       const print_options& opts = print_options(),
                                       const knst_c16string& doc_format = u"application/pdf") {
        (void)pdf_path; (void)host_port; (void)port; (void)opts; (void)doc_format;
        return result<uint32_t>::fail(status::NotSupported);
    }
#endif

        // ─── Send-Document helper (HP 7740: Print-Job inline PDF fails, needs 2-step) ───
    static result<uint32_t> print_ipp_send_doc(const knst_c16string& pdf_path,
                                                const knst_c16string& host_port,
                                                uint16_t port = 631,
                                                const print_options& opts = print_options(),
                                                const knst_c16string& doc_format = u"application/pdf") {
        return detail::ipp::print_pdf_send_doc(pdf_path, host_port, port, opts, doc_format);
    }

    // ─── Universal print: fast path → smart fallback ─────────────────────
    // 1. IPP (631/443) PDF — fast and silent (modern printers).
    // 2. Fallback: PDF → JPEG → IPP per page.
    // 3. Last resort: USB OTG / Bluetooth SPP.
    // Raw 9100 is deliberately NOT tried — PDF bytes print as blank pages.
           result<uint32_t> print_universal(const knst_c16string& path,
                                  const knst_c16string& host_port = knst_c16string(),
                                  const print_options& o = print_options()) {
    using R = result<uint32_t>;
    if (path.empty()) return R::fail(status::InvalidArgument);

    // ── Resolve input file ──
    knst_c16string abs = knst_file::resolve_path(path);
    if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);

    // ── Split host:port (may come as URI) ──
    std::string hp = detail::to_u8(host_port);
    size_t scheme = hp.find("://");
    if (scheme != std::string::npos) hp = hp.substr(scheme + 3);
    size_t slash = hp.find('/');
    if (slash != std::string::npos) hp = hp.substr(0, slash);

    std::string host = hp;
    uint16_t port = 631;
    size_t colon = hp.rfind(':');
    if (colon != std::string::npos) {
        host = hp.substr(0, colon);
        int p = std::atoi(hp.c_str() + colon + 1);
        if (p > 0) port = (uint16_t)p;
    }
    std::string hpu = host + ":" + std::to_string(port);

    // MAC-format host → Bluetooth printer. IPP cannot talk to Bluetooth SPP,
    // so skip the IPP path entirely (avoids 30s PDF rasterization before
    // falling back to the PrintManager dialog).
    // MAC detection must check `hp` (the ORIGINAL pre-split string), not
    // `host` — the last `:FF` would otherwise be eaten as "port FF" and
    // truncate host to 14 chars, making the MAC check fail.
    const bool is_bt_mac = (hp.size() == 17 &&
                            hp[2] == ':' && hp[5] == ':' &&
                            hp[8] == ':' && hp[11] == ':' && hp[14] == ':');

    // ── Convert to PDF if needed (all IPP paths expect PDF) ──
    auto kind = detail::detect_kind(abs);
    detail::temp_set tmp;
    knst_c16string pdf_path = abs;
    if (kind != detail::file_kind::Pdf) {
        if (kind != detail::file_kind::Image && kind != detail::file_kind::Text) {
            // Raw printer languages (PCL/ZPL/EscPos) → send raw over USB/BT,
            // not through IPP (raw bytes over IPP as application/pdf = garbage).
            if (kind == detail::file_kind::Pcl || kind == detail::file_kind::Zpl ||
                kind == detail::file_kind::EscPos || kind == detail::file_kind::PostScript) {
                knst_file_error fe = knst_file_error::None;
                knst_byte_string data = knst_file::read_file_data<>(abs, &fe);
                if (fe == knst_file_error::None && !data.empty()) {
                    // USB first
                    if (print_usb(data)) return R::success(0);
                    // BT if the ORIGINAL host string is a MAC (not the truncated
                    // `host` variable — that's had the last `:XX` eaten as a port).
                    if (is_bt_mac && print_bluetooth(data, detail::from_u8(hp)))
                        return R::success(0);
                }
            }
            return R::fail(status::NotSupported);
        }
        // Image / Text → build PDF
        float w, h;
        if (!detail::paper_mm(o.paper, w, h)) { w = 210.0f; h = 297.0f; }
        detail::document doc;
        if (kind == detail::file_kind::Image) {
            image_batch_options bo;
            bo.margin_mm = o.borderless ? 0.0f : 5.0f;
            bo.fit       = o.borderless ? fit_mode::Fill : fit_mode::Fit;
            std::vector<knst_c16string> one{ abs };
            auto st = detail::layout_images(one, bo, w, h, doc);
            if (st != status::Ok) return R::fail(st);
        } else {
            detail::layout_plain_text(knst_file::read_file_text(abs), w, h, doc);
        }
        pdf_path = tmp.add(u".pdf");
        auto st = detail::write_pdf(doc, pdf_path);
        if (st != status::Ok) return R::fail(st);
    }

    // ══ 1) + 2) Smart IPP path — only for network hosts, not Bluetooth MACs ═══
    if (!is_bt_mac) {
#if KNST_PRN_HAS_NETWORK
        auto r_ipp = print_ipp_smart(pdf_path, host_port, o);
        if (r_ipp) return r_ipp;
#endif
    }

    // ══ 3) Final fallback: Android PrintManager dialog with the PDF ═══
    // Cleanup old (>1h) dialog PDFs to bound storage; keep the current one.
    tmp.paths.clear();  // keep pdf_path alive while the dialog reads it
    auto r_dlg = print_pdf_dialog(pdf_path, o);
    if (r_dlg) return r_dlg;

    return R::fail(status::IoError);
}

    // ---- USB OTG raw (ESC/POS, PCL, ZPL) ---------------------------------
    result<uint32_t> print_usb(const knst_byte_string& data) {
        using R = result<uint32_t>;
        if (data.empty()) return R::fail(status::InvalidArgument);
        if (!detail::print_usb_raw(data)) return R::fail(status::IoError);
        return R::success(0);
    }

    // ---- Bluetooth SPP raw -----------------------------------------------
    result<uint32_t> print_bluetooth(const knst_byte_string& data, const knst_c16string& mac) {
        using R = result<uint32_t>;
        if (data.empty() || mac.empty()) return R::fail(status::InvalidArgument);
        if (!detail::print_bluetooth_raw(data, mac)) return R::fail(status::IoError);
        return R::success(0);
    }

    // ---- Network raw (TCP 9100) — ESC/POS, PCL, ZPL over network ----------
    // Raw BSD sockets (getaddrinfo + socket + connect + send).
    // Works even when knst_printer.hpp is included before knst_network.hpp.
    result<uint32_t> print_network(const knst_byte_string& data, const knst_c16string& host,
                                   uint16_t port = 9100, uint32_t timeout_ms = 5000) {
        using R = result<uint32_t>;
        if (data.empty() || host.empty()) return R::fail(status::InvalidArgument);
        if (timeout_ms < 100) timeout_ms = 100;

        // Convert the host to a UTF-8 C string.
        knst_byte_string host_u8 = knst_dev::to_utf8(host);
        std::string host_str(reinterpret_cast<const char*>(host_u8.data()), host_u8.length());
        if (host_str.empty()) return R::fail(status::InvalidArgument);

        // DNS + address resolution.
        struct addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        std::string port_str = std::to_string(port ? port : 9100);
        struct addrinfo* res = nullptr;
        int gai = ::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res);
        if (gai != 0 || !res) return R::fail(status::NotFound);

        // Try every address (non-blocking connect + poll).
        int sock_fd = -1;
        for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
            int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK, ai->ai_protocol);
            if (fd < 0) continue;

            int rc = ::connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
            if (rc < 0 && errno != EINPROGRESS) {
                ::close(fd);
                continue;
            }

            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int pr = ::poll(&pfd, 1, static_cast<int>(timeout_ms));
            if (pr <= 0) {
                ::close(fd);
                continue;
            }

            int soerr = 0;
            socklen_t solen = sizeof(soerr);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &solen) != 0 || soerr != 0) {
                ::close(fd);
                continue;
            }

            // Switch back to blocking mode and apply a send timeout.
            int flags = ::fcntl(fd, F_GETFL, 0);
            ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            struct timeval tv{};
            tv.tv_sec  = static_cast<time_t>(timeout_ms / 1000);
            tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            sock_fd = fd;
            break;
        }
        ::freeaddrinfo(res);

        if (sock_fd < 0) return R::fail(status::IoError);

        // Send the data (MSG_NOSIGNAL prevents SIGPIPE on peer close).
        const uint8_t* p = data.data();
        size_t left = data.length();
        bool ok = true;
        while (left > 0) {
            ssize_t n = ::send(sock_fd, p, left, MSG_NOSIGNAL);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                ok = false;
                break;
            }
            p    += n;
            left -= static_cast<size_t>(n);
        }

        ::close(sock_fd);
        return ok ? R::success(0) : R::fail(status::IoError);
    }


    // ─── PDF → JPEG rasterize → Create-Job + Send-Document × N (one IPP job) ─
    // Uses RFC 8011 §4.2.1 Create-Job + §4.3.1 Send-Document with
    // last-document=true so N pages form ONE IPP job. Duplex / collate /
    // finishings apply to the whole job. No Mopria / external service.
    // Required for printers that reject PDF (e.g. HP 7740).
#if KNST_PRN_HAS_NETWORK
    static result<uint32_t> print_jpeg_ipp(const knst_c16string& pdf_path,
                                            const knst_c16string& host_port,
                                            uint16_t = 631,
                                            const print_options& opts = print_options()) {
        detail::ipp::send_policy p; p.prefer = "jpeg";
        return print_ipp_smart(pdf_path, host_port, opts, p);
    }
#else
    static result<uint32_t> print_jpeg_ipp(const knst_c16string&,
                                            const knst_c16string&,
                                            uint16_t = 631,
                                            const print_options& = print_options()) {
        return result<uint32_t>::fail(status::NotSupported);
    }
#endif

        // ---- mDNS printer discovery (_ipp._tcp + _ipps._tcp) ---------------
    // JNI bridge to NsdManager.discoverServices(). Calls the Java side's
    // KnstPrintDiscoveryHelper.discover(timeout_ms), which collects IPP
    // printers on the network and returns them as JSON.
    static result<knst_vector<printer_info_ex>> list_all() {
        using R = result<knst_vector<printer_info_ex>>;
        JNIEnv* e = detail::jni().env();
        if (!e || !detail::jni().activity) return R::fail(status::BadState);

        jclass helper = nullptr;
        // Resolve the app class via the activity's classloader.
        jclass act_cls = e->GetObjectClass(detail::jni().activity);
        if (act_cls) {
            jmethodID get_cl = e->GetMethodID(act_cls, "getClassLoader",
                                              "()Ljava/lang/ClassLoader;");
            if (get_cl) {
                jobject cl = e->CallObjectMethod(detail::jni().activity, get_cl);
                if (cl) {
                    jclass cl_cls = e->GetObjectClass(cl);
                    jmethodID load = e->GetMethodID(cl_cls, "loadClass",
                                                    "(Ljava/lang/String;)Ljava/lang/Class;");
                    if (load) {
                        jstring jn = e->NewStringUTF("com.knst.print.KnstPrintDiscoveryHelper");
                        helper = (jclass)e->CallObjectMethod(cl, load, jn);
                        e->DeleteLocalRef(jn);
                    }
                    e->DeleteLocalRef(cl_cls); e->DeleteLocalRef(cl);
                }
            }
            e->DeleteLocalRef(act_cls);
        }
        if (e->ExceptionCheck()) { e->ExceptionClear(); }
        if (!helper) return R::fail(status::NotSupported);

        jmethodID discover = e->GetStaticMethodID(helper, "discover",
            "(I)[Ljava/lang/String;");
        if (!discover) {
            if (e->ExceptionCheck()) e->ExceptionClear();
            e->DeleteLocalRef(helper);
            return R::fail(status::NotSupported);
        }

        jobjectArray arr = (jobjectArray)e->CallStaticObjectMethod(helper, discover, 4000);
        R r;
        if (arr) {
            jsize n = e->GetArrayLength(arr);
            for (jsize i = 0; i < n; ++i) {
                jstring js = (jstring)e->GetObjectArrayElement(arr, i);
                if (!js) continue;
                const char* u = e->GetStringUTFChars(js, nullptr);
                if (u) {
                    // Format: "name|host|port|uri"
                    std::string line(u);
                    e->ReleaseStringUTFChars(js, u);
                    size_t p1 = line.find('|');
                    size_t p2 = (p1 != std::string::npos) ? line.find('|', p1 + 1) : std::string::npos;
                    size_t p3 = (p2 != std::string::npos) ? line.find('|', p2 + 1) : std::string::npos;
                    if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
                        printer_info_ex ex;
                        ex.name = detail::from_u8(line.substr(0, p1));
                        ex.device_uri = detail::from_u8(line.substr(p3 + 1));
                        ex.make_and_model = ex.name;
                        ex.model = ex.name;
                        ex.is_enabled = true;
                        ex.is_accepting = true;
                        ex.state = printer_state::Ready;
                        r.value.push_back(std::move(ex));
                    }
                }
                e->DeleteLocalRef(js);
            }
            e->DeleteLocalRef(arr);
        }
        if (e->ExceptionCheck()) e->ExceptionClear();
        e->DeleteLocalRef(helper);
        return r;
    }

    // ─── Generate PDF + PrintManager dialog (same document model as PC) ──────
    result<uint32_t> print_text(const knst_c16string& text, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (text.empty()) return R::fail(status::InvalidArgument);
        float w, h;
        if (!detail::paper_mm(o.paper, w, h)) { w = 210.0f; h = 297.0f; }
        detail::document doc;
        detail::layout_plain_text(text, w, h, doc);
        detail::temp_set tmp;
        knst_c16string pdf = tmp.add(u".pdf");
        auto st = detail::write_pdf(doc, pdf);
        if (st != status::Ok) return R::fail(st);
        tmp.paths.clear();  // keep the PDF — the dialog reads it while open
        return print_pdf_dialog(pdf, o);
    }

    result<uint32_t> print_rich(const rich_document& rd, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (rd.elements.empty() && rd.shapes.empty()) return R::fail(status::InvalidArgument);
        detail::document doc;
        detail::layout_rich(rd, doc);
        detail::temp_set tmp;
        knst_c16string pdf = tmp.add(u".pdf");
        auto st = detail::write_pdf(doc, pdf);
        if (st != status::Ok) return R::fail(st);
        tmp.paths.clear();
        return print_pdf_dialog(pdf, o);
    }

    result<uint32_t> print_image(const knst_c16string& path, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        float w, h;
        if (!detail::paper_mm(o.paper, w, h)) { w = 210.0f; h = 297.0f; }
        image_batch_options bo;
        bo.margin_mm = o.borderless ? 0.0f : 5.0f;
        bo.fit = o.borderless ? fit_mode::Fill : fit_mode::Fit;
        std::vector<knst_c16string> v{path};
        detail::document doc;
        auto st = detail::layout_images(v, bo, w, h, doc);
        if (st != status::Ok) return R::fail(st);
        detail::temp_set tmp;
        knst_c16string pdf = tmp.add(u".pdf");
        st = detail::write_pdf(doc, pdf);
        if (st != status::Ok) return R::fail(st);
        tmp.paths.clear();
        return print_pdf_dialog(pdf, o);
    }

        // ── knst_vector overload (canonical implementation) ──────────────────
    result<uint32_t> print_images(const knst_vector<knst_c16string>& paths,
                                  const image_batch_options& bo = image_batch_options(),
                                  const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (paths.empty()) return R::fail(status::InvalidArgument);
        float w, h;
        if (!detail::paper_mm(o.paper, w, h)) { w = 210.0f; h = 297.0f; }
        std::vector<knst_c16string> v;
        for (uint32_t i = 0; i < paths.size(); ++i) v.push_back(paths[i]);
        detail::document doc;
        auto st = detail::layout_images(v, bo, w, h, doc);
        if (st != status::Ok) return R::fail(st);
        detail::temp_set tmp;
        knst_c16string pdf = tmp.add(u".pdf");
        st = detail::write_pdf(doc, pdf);
        if (st != status::Ok) return R::fail(st);
        tmp.paths.clear();
        return print_pdf_dialog(pdf, o);
    }

    // ── std::vector overload (same pattern as Linux/Windows) ─────────────
    // scan_and_print_to() and user code can pass std::vector.
    // One-way wrapper that forwards to the knst_vector overload.
    // No default args — matches Linux exactly, no ambiguity.
    result<uint32_t> print_images(const std::vector<knst_c16string>& paths,
                                  const image_batch_options& bo,
                                  const print_options& o) {
        if (paths.empty()) return result<uint32_t>::fail(status::InvalidArgument);
        knst_vector<knst_c16string> v;
        for (const auto& p : paths) v.push_back(p);
        return print_images(v, bo, o);
    }

    // ── knst_vector overload (canonical implementation) ──────────────────
        // ── knst_vector overload (canonical implementation) ──────────────────
    // Mixed content (images + text + PDFs) → ONE PDF → ONE PrintManager dialog.
    // Unreadable files are skipped if bo.skip_unreadable is true.
    result<uint32_t> print_batch(const knst_vector<knst_c16string>& paths,
                                 const print_options& o = print_options(),
                                 const image_batch_options& bo = image_batch_options()) {
        using R = result<uint32_t>;
        if (paths.empty()) return R::fail(status::InvalidArgument);

        float w, h;
        if (!detail::paper_mm(o.paper, w, h)) { w = 210.0f; h = 297.0f; }

        detail::document combined;
        std::vector<knst_c16string> img_run;

        // Per-call raster temp dirs; the destructor guarantees cleanup
        // on every return path (earlier code leaked them; concurrent calls
        // also raced on the fixed "knst_batch_raster" name).
        std::vector<knst_c16string> raster_dirs;
        struct dir_cleanup {
            std::vector<knst_c16string>& dirs;
            ~dir_cleanup() { for (auto& d : dirs) knst_file::remove_all(d); }
        } cleanup_guard{raster_dirs};

        auto flush_run = [&]() -> knst_prn::status {
            if (img_run.empty()) return status::Ok;
            detail::document part;
            knst_prn::status st = detail::layout_images(img_run, bo, w, h, part);
            img_run.clear();
            if (st != status::Ok) return st;
            size_t off = combined.images.size();
            if (combined.pages.empty()) { combined.w_pt = part.w_pt; combined.h_pt = part.h_pt; combined.max_dpi = part.max_dpi; }
            for (auto& im : part.images) combined.images.push_back(std::move(im));
            for (auto& pg : part.pages) {
                for (auto& it : pg.items) if (it.kind == detail::item::Image) it.image += (uint32_t)off;
                combined.pages.push_back(std::move(pg));
            }
            return status::Ok;
        };

        auto append_doc = [&](detail::document& part) {
            size_t off = combined.images.size();
            if (combined.pages.empty()) { combined.w_pt = part.w_pt; combined.h_pt = part.h_pt; combined.max_dpi = part.max_dpi; }
            for (auto& im : part.images) combined.images.push_back(std::move(im));
            for (auto& pg : part.pages) {
                for (auto& it : pg.items) if (it.kind == detail::item::Image) it.image += (uint32_t)off;
                combined.pages.push_back(std::move(pg));
            }
        };

        for (uint32_t i = 0; i < paths.size(); ++i) {
            const knst_c16string& p = paths[i];
            knst_c16string abs = knst_file::resolve_path(p);
            detail::file_kind k = detail::detect_kind(p);

            if (k == detail::file_kind::Image) {
                if (!knst_file::file_exists(abs)) {
                    if (bo.skip_unreadable) { if (bo.skipped) bo.skipped->push_back(p); continue; }
                    return R::fail(status::NotFound);
                }
                img_run.push_back(abs);
                continue;
            }

            auto st = flush_run();
            if (st != status::Ok) return R::fail(st);

            if (!knst_file::file_exists(abs)) {
                if (bo.skip_unreadable) { if (bo.skipped) bo.skipped->push_back(p); continue; }
                return R::fail(status::NotFound);
            }

            if (k == detail::file_kind::Text) {
                detail::document part;
                detail::layout_plain_text(knst_file::read_file_text(abs), w, h, part);
                append_doc(part);
            } else if (k == detail::file_kind::Pdf) {
                // Rasterize PDF → JPEGs → add as image run (flushed on next iter or at end)
                uint64_t stamp = static_cast<uint64_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count());
                knst_c16string raster_dir = knst_file::join_path(
                    knst_file::get_temp_dir(),
                    detail::from_u8("knst_batch_raster_" + std::to_string(stamp & 0xFFFFFFu)));
                knst_file::create_directories(raster_dir);
                raster_dirs.push_back(raster_dir);   // tracked — cleanup_guard removes on exit
                auto jpegs = detail::rasterize_pdf_to_jpegs(abs, raster_dir, 200, 85);
                if (jpegs.empty()) {
                    if (bo.skip_unreadable) { if (bo.skipped) bo.skipped->push_back(p); continue; }
                    return R::fail(status::DecodeFailed);
                }
                for (auto& j : jpegs) img_run.push_back(j);
            } else {
                if (bo.skip_unreadable) { if (bo.skipped) bo.skipped->push_back(p); continue; }
                return R::fail(status::NotSupported);
            }
        }

        auto st = flush_run();
        if (st != status::Ok) return R::fail(st);
        if (combined.pages.empty()) return R::fail(status::NotFound);

        detail::temp_set tmp;
        knst_c16string pdf = tmp.add(u".pdf");
        st = detail::write_pdf(combined, pdf);
        if (st != status::Ok) return R::fail(st);
        tmp.paths.clear();  // keep the PDF — the dialog reads it while open
        return print_pdf_dialog(pdf, o);
    }

    // ── std::vector overload (same pattern as Linux/Windows) ─────────────
    result<uint32_t> print_batch(const std::vector<knst_c16string>& paths,
                                 const print_options& o,
                                 const image_batch_options& bo) {
        if (paths.empty()) return result<uint32_t>::fail(status::InvalidArgument);
        knst_vector<knst_c16string> v;
        for (const auto& p : paths) v.push_back(p);
        return print_batch(v, o, bo);
    }

    result<uint32_t> print_file(const knst_c16string& path, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        switch (detail::detect_kind(abs)) {
            case detail::file_kind::Text: {
                knst_c16string c = knst_file::read_file_text(abs);
                if (c.empty()) return R::fail(status::IoError);
                return print_text(c, o);
            }
            case detail::file_kind::Image: return print_image(abs, o);
            case detail::file_kind::Pdf:   return print_pdf(abs, o);
            case detail::file_kind::PostScript:
            case detail::file_kind::Pcl:
            case detail::file_kind::Zpl:
            case detail::file_kind::EscPos: {
                knst_file_error fe = knst_file_error::None;
                knst_byte_string b = knst_file::read_file_data<>(abs, &fe);
                if (fe != knst_file_error::None || b.empty()) return R::fail(status::IoError);
                return print_usb(b);  // raw to a USB OTG printer
            }
            default: return R::fail(status::NotSupported);
        }
    }

    result<uint32_t> print_pdf(const knst_c16string& path, const print_options& o = print_options()) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        unsigned char m[5];
        if (!detail::read_magic(abs, m, 5)) return R::fail(status::IoError);
        if (std::memcmp(m, "%PDF-", 5) != 0) return R::fail(status::InvalidArgument);
        return print_pdf_dialog(abs, o);   // PDF already exists; hand it to the dialog
    }

    result<uint32_t> print_auto(const knst_c16string& path, const print_options& o = print_options()) {
        return print_file(path, o);
    }

    result<uint32_t> print_test_page(test_page_type type = test_page_type::Diagnostic,
                                     const print_options& o = print_options()) {
        using R = result<uint32_t>;
        float w, h;
        if (!detail::paper_mm(o.paper, w, h)) { w = 210.0f; h = 297.0f; }
        detail::document doc;
        detail::make_test_doc(type, "android", w, h, doc);
        detail::temp_set tmp;
        knst_c16string pdf = tmp.add(u".pdf");
        auto st = detail::write_pdf(doc, pdf);
        if (st != status::Ok) return R::fail(st);
        tmp.paths.clear();
        print_options oo = o;
        if (oo.title.empty()) oo.title = u"KNST test page";
        oo.copies = 1;
        return print_pdf_dialog(pdf, oo);
    }

    result<uint32_t> send_raw(const knst_byte_string& data, const knst_c16string& = u"") {
        return print_usb(data);  // On Android, "raw" means USB OTG bulk transfer.
    }
    result<uint32_t> send_escpos(const knst_byte_string& d) { return print_usb(d); }
    result<uint32_t> send_zpl(const knst_byte_string& d) { return print_usb(d); }
    result<uint32_t> send_pcl(const knst_byte_string& d) { return print_usb(d); }
    result<uint32_t> print_secure(const knst_c16string&, const secure_print_options&, const print_options& = print_options()) { return result<uint32_t>::fail(status::NotSupported); }
    result<uint32_t> print_held(const knst_c16string&, const knst_c16string&, const print_options& = print_options()) { return result<uint32_t>::fail(status::NotSupported); }
        result<knst_vector<uint32_t>> print_files(const knst_vector<knst_c16string>& paths,
                                              const print_options& o = print_options(),
                                              bool continue_on_error = false) {
        using R = result<knst_vector<uint32_t>>;
        if (paths.empty()) return R::fail(status::InvalidArgument);
        R r;
        knst_prn::status last = status::IoError;
        int nat = 0;
        for (uint32_t i = 0; i < paths.size(); ++i) {
            // Each file → print_auto → one PrintManager dialog
            // (Android's PrintManager is one-job-at-a-time; N files = N dialogs).
            auto one = print_auto(paths[i], o);
            if (one) { r.value.push_back(one.value); continue; }
            last = one.st;
            nat  = one.native;
            if (!continue_on_error) {
                if (r.value.empty()) return R::fail(one.st, one.native);
                break;
            }
        }
        if (r.value.empty()) return R::fail(last, nat);
        return r;
    }
        static result<knst_vector<uint32_t>> print_to_multiple(const knst_vector<knst_c16string>& targets,
                                                           const knst_c16string& path,
                                                           const print_options& o = print_options()) {
        using R = result<knst_vector<uint32_t>>;
        if (path.empty() || targets.empty()) return R::fail(status::InvalidArgument);

        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);

        // Convert non-PDF to PDF first (print_ipp only handles PDF directly).
        knst_c16string pdf_path = abs;
        detail::temp_set tmp;
        if (detail::detect_kind(abs) != detail::file_kind::Pdf) {
            float w, h;
            if (!detail::paper_mm(o.paper, w, h)) { w = 210.0f; h = 297.0f; }
            detail::document doc;
            auto kind = detail::detect_kind(abs);
            if (kind == detail::file_kind::Image) {
                image_batch_options bo;
                bo.margin_mm = o.borderless ? 0.0f : 5.0f;
                bo.fit       = o.borderless ? fit_mode::Fill : fit_mode::Fit;
                std::vector<knst_c16string> one{ abs };
                auto st = detail::layout_images(one, bo, w, h, doc);
                if (st != status::Ok) return R::fail(st);
            } else if (kind == detail::file_kind::Text) {
                detail::layout_plain_text(knst_file::read_file_text(abs), w, h, doc);
            } else {
                return R::fail(status::NotSupported);
            }
            pdf_path = tmp.add(u".pdf");
            auto st = detail::write_pdf(doc, pdf_path);
            if (st != status::Ok) return R::fail(st);
        }

        R r;
        knst_prn::status last = status::NotFound;
        int nat = 0;
        for (uint32_t i = 0; i < targets.size(); ++i) {
            // 1) Direct PDF over IPP/IPPS
            auto r1 = print_ipp(pdf_path, targets[i], 631, o);
            if (r1) { r.value.push_back(r1.value); continue; }
            // 2) JPEG-per-page fallback (HP 7740-class printers)
            auto r2 = print_jpeg_ipp(pdf_path, targets[i], 631, o);
            if (r2) { r.value.push_back(r2.value); continue; }
            last = r2.st;
            nat  = r2.native;
        }
        if (r.value.empty()) return R::fail(last, nat);
        return r;
    }

    static result<uint32_t> pdf_page_count(const knst_c16string& path) {
        using R = result<uint32_t>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);
        knst_file_error fe = knst_file_error::None;
        knst_byte_string b = knst_file::read_file_data<>(abs, &fe);
        if (fe != knst_file_error::None) return R::fail(status::IoError);
        uint32_t n = detail::count_pdf_pages_scan(b);
        return n ? R::success(n) : R::fail(status::ToolMissing);
    }
    static result<knst_byte_string> render_preview(const knst_c16string& path, uint32_t page = 1, uint32_t dpi = 100) {
        using R = result<knst_byte_string>;
        if (path.empty() || page == 0 || dpi < 36 || dpi > 600)
            return R::fail(status::InvalidArgument);
        knst_c16string abs = knst_file::resolve_path(path);
        if (!knst_file::file_exists(abs)) return R::fail(status::NotFound);

        // Temp dir for rasterized JPEGs (unique name from steady_clock)
        uint64_t stamp = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        knst_c16string out_dir = knst_file::join_path(
            knst_file::get_temp_dir(),
            detail::from_u8("knst_prev_" + std::to_string(stamp & 0xFFFFFFu)));
        knst_file::create_directories(out_dir);

        auto jpegs = detail::rasterize_pdf_to_jpegs(abs, out_dir, dpi, 90);
        if (jpegs.empty() || page > jpegs.size()) {
            knst_file::remove_all(out_dir);
            return R::fail(status::NotFound);
        }

        // Convert requested page JPEG → PNG (consistent with Linux/Windows API)
        knst_c16string png_path = knst_file::join_path(out_dir, u"preview.png");
        if (!detail::decode_to_png(jpegs[page - 1], png_path)) {
            knst_file::remove_all(out_dir);
            return R::fail(status::DecodeFailed);
        }

        knst_file_error fe = knst_file_error::None;
        knst_byte_string png = knst_file::read_file_data<>(png_path, &fe);
        knst_file::remove_all(out_dir);
        if (fe != knst_file_error::None || png.empty())
            return R::fail(status::IoError);
        return R::success(std::move(png));
    }
    static result<void> add_printer(const knst_c16string&, const knst_c16string&, const knst_c16string&, const knst_c16string& = knst_c16string(), const knst_c16string& = knst_c16string()) { return result<void>::fail(status::NotSupported); }
    static result<void> add_ipp_printer(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> remove_printer(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<knst_vector<printer_class>> list_classes() { return result<knst_vector<printer_class>>::fail(status::NotSupported); }
    static result<void> create_class(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> delete_class(const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> add_to_class(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> remove_from_class(const knst_c16string&, const knst_c16string&) { return result<void>::fail(status::NotSupported); }
    static result<void> set_cache(bool enabled, const knst_c16string& dir = knst_c16string(), uint32_t max_mb = 500) {
        return detail::cache_set(enabled, dir, max_mb);
    }
    static result<void> clear_cache() { return detail::cache_clear(); }
    static uint64_t cache_size_bytes() { return detail::cache_size(); }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Android scanner — eSCL (AirScan) protocol, no ML Kit dependency.
//
//  Modern printers (e.g. HP OfficeJet Pro 7740) support the HTTP+XML
//  eSCL protocol. Flow:
//
//    1) GET  /eSCL/ScannerCapabilities       → XML (supported modes)
//    2) POST /eSCL/ScanJobs (ScanSettings)   → 201 + Location header
//    3) GET  <Location>/NextDocument         → JPEG pages
//    For ADF, loop until NextDocument returns 404.
//
//  ADVANTAGE: zero extra dependencies, cross-platform (same code runs
//  on Linux; only mDNS discovery is platform-specific).
// ═══════════════════════════════════════════════════════════════════════════
class scanner {
public:
            // ---- mDNS discovery (JNI → KnstPrintDiscoveryHelper.discoverScanners) ----
    static result<knst_vector<scanner_info>> list_all() {
        using R = result<knst_vector<scanner_info>>;

        // Process-lifetime cache: when the HP 7740 mDNS cache fills up it
        // answers with PTR only on the second call and skips SRV/A → 0
        // results. Cache the first successful discovery for 5 minutes.
        static std::mutex cache_m;
        static knst_vector<scanner_info> cached;
        static std::chrono::steady_clock::time_point cached_at{};
        static constexpr auto kCacheTtl = std::chrono::minutes(5);

        {
            std::lock_guard<std::mutex> lk(cache_m);
            auto now = std::chrono::steady_clock::now();
            if (!cached.empty() && (now - cached_at) < kCacheTtl) {

                return R::success(cached);
            }
        }

        // ─── One full JNI discovery cycle (retried by the caller) ──────
        // Some printers (e.g. HP 7740) send the PTR reply immediately but
        // the SRV/A records 300-500 ms later in separate packets. The
        // first 4 s window can miss them and return an empty list.
        // This lambda runs once; on empty result list_all() sleeps 500 ms
        // and retries, resolving the race.
        auto discover_once = []() -> R {
            R r;
            JNIEnv* e = detail::jni().env();
            if (!e || !detail::jni().activity) return R::fail(status::BadState);

            jclass helper = nullptr;
            jclass act_cls = e->GetObjectClass(detail::jni().activity);
            if (act_cls) {
                jmethodID get_cl = e->GetMethodID(act_cls, "getClassLoader", "()Ljava/lang/ClassLoader;");
                if (get_cl) {
                    jobject cl = e->CallObjectMethod(detail::jni().activity, get_cl);
                    if (cl) {
                        jclass cl_cls = e->GetObjectClass(cl);
                        jmethodID load = e->GetMethodID(cl_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                        if (load) {
                            jstring jn = e->NewStringUTF("com.knst.print.KnstPrintDiscoveryHelper");
                            helper = (jclass)e->CallObjectMethod(cl, load, jn);
                            e->DeleteLocalRef(jn);
                        }
                        e->DeleteLocalRef(cl_cls); e->DeleteLocalRef(cl);
                    }
                }
                e->DeleteLocalRef(act_cls);
            }
            if (e->ExceptionCheck()) e->ExceptionClear();
            if (!helper) return R::fail(status::NotSupported);

            jmethodID discover = e->GetStaticMethodID(helper, "discoverScanners", "(I)[Ljava/lang/String;");
            if (!discover) {
                if (e->ExceptionCheck()) e->ExceptionClear();
                e->DeleteLocalRef(helper);
                return R::fail(status::NotSupported);
            }

            jobjectArray arr = (jobjectArray)e->CallStaticObjectMethod(helper, discover, 4000);
            if (arr) {
                jsize n = e->GetArrayLength(arr);
                for (jsize i = 0; i < n; ++i) {
                    jstring js = (jstring)e->GetObjectArrayElement(arr, i);
                    if (!js) continue;
                    const char* u = e->GetStringUTFChars(js, nullptr);
                    if (u) {
                        // Format: "name|host|port"
                        std::string line(u);
                        e->ReleaseStringUTFChars(js, u);
                        size_t p1 = line.find('|');
                        size_t p2 = (p1 != std::string::npos) ? line.find('|', p1 + 1) : std::string::npos;
                        if (p1 != std::string::npos && p2 != std::string::npos) {
                            // 3-part: name|host|port
                            scanner_info si;
                            si.name = detail::from_u8(line.substr(0, p1));
                            si.device_id = detail::from_u8(line.substr(p1 + 1, p2 - p1 - 1) + ":" + line.substr(p2 + 1));
                            si.backend = u"escl";
                            si.is_network = true;
                            si.flatbed = true;
                            r.value.push_back(std::move(si));
                        } else if (!line.empty() && line.find(':') != std::string::npos) {
                            // 2-part: host:port (Java discoverScannersRaw returns this)
                            scanner_info si;
                            si.name = detail::from_u8(line);
                            si.device_id = detail::from_u8(line);
                            si.backend = u"escl";
                            si.is_network = true;
                            si.flatbed = true;
                            r.value.push_back(std::move(si));
                        }
                    }
                    e->DeleteLocalRef(js);
                }
                e->DeleteLocalRef(arr);
            }
            if (e->ExceptionCheck()) e->ExceptionClear();
            e->DeleteLocalRef(helper);
            return r;
        };

        auto r = discover_once();

        // ═══════ RETRY ON EMPTY (mDNS SRV/A race fix) ═══════
        // Some printers (e.g. HP 7740) send the PTR reply immediately but
        // the SRV/A records 300-500 ms later in separate packets. The
        // first 4 s window can miss them and return an empty list.
        // Fix: on empty, sleep 500 ms and retry. Cache successful results.
        // On repeated failure the old behaviour (0 scanners) is preserved.
        if (r && r.value.empty()) {

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            r = discover_once();

        }

        if (r && !r.value.empty()) {
            std::lock_guard<std::mutex> lk(cache_m);
            cached = r.value;
            cached_at = std::chrono::steady_clock::now();

        }
        return r;
    }

    // ---- open("host:port") ----
    static result<scanner> open(const knst_c16string& device_id) {
        using R = result<scanner>;
        if (device_id.empty()) return R::fail(status::InvalidArgument);
        scanner s;
        s.name_ = device_id;
        s.device_id_ = device_id;   // "host:port"
        return R::success(std::move(s));
    }

    const knst_c16string& name() const noexcept { return name_; }

    // ── GET /eSCL/ScannerStatus → "Idle"? ────────────────────────────
    // Optional pre-check before submitting a scan job.
    result<bool> scanner_status() const {
        using R = result<bool>;
        auto resp = http_get(u"/eSCL/ScannerStatus");
        if (!resp) return R::fail(resp.st, resp.native);
        if (resp.value.status != 200) return R::fail(status::IoError, resp.value.status);
        std::string xml(reinterpret_cast<const char*>(resp.value.body.data()),
                        resp.value.body.length());
        return R::success(xml.find("Idle") != std::string::npos);
    }

        // ---- GET /eSCL/ScannerCapabilities → parse (3 retries) ----
    result<scanner_capabilities> capabilities() const {
        using R = result<scanner_capabilities>;
        auto resp = http_get(u"/eSCL/ScannerCapabilities");
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (attempt > 0) {

                std::this_thread::sleep_for(std::chrono::seconds(2));
                resp = http_get(u"/eSCL/ScannerCapabilities");
            }
            if (resp && resp.value.status == 200) break;
        }
        if (!resp) {

            return R::fail(resp.st, resp.native);
        }
        if (resp.value.status != 200) return R::fail(status::IoError, resp.value.status);

        R r;

        std::string xml(reinterpret_cast<const char*>(resp.value.body.data()),
                        resp.value.body.length());

        auto find_ints = [&](const char* tag) {
            std::vector<uint32_t> vals;
            std::string open_tag = std::string("<") + tag;
            std::string close_tag = std::string("</") + tag + ">";
            size_t pos = 0;
            while ((pos = xml.find(open_tag, pos)) != std::string::npos) {
                size_t gt = xml.find('>', pos);
                if (gt == std::string::npos) break;
                size_t end = xml.find(close_tag, gt);
                if (end == std::string::npos) break;
                vals.push_back((uint32_t)std::atoi(xml.substr(gt + 1, end - gt - 1).c_str()));
                pos = end + close_tag.size();
            }
            return vals;
        };
        auto res_x = find_ints("scan:XResolution");
        auto res_y = find_ints("scan:YResolution");
        for (size_t i = 0; i < res_x.size(); ++i)
            if (i < res_y.size() && res_x[i] > 0 && res_x[i] == res_y[i])
                r.value.resolutions.push_back(res_x[i]);
        // Remove duplicates.
        std::sort(r.value.resolutions.begin(), r.value.resolutions.end());
        r.value.resolutions.erase(
            std::unique(r.value.resolutions.begin(), r.value.resolutions.end()),
            r.value.resolutions.end());
        if (r.value.resolutions.empty()) r.value.resolutions = {100, 200, 300, 600};

        auto find_str = [&](const char* tag) {
            std::vector<std::string> vals;
            std::string open_tag = std::string("<") + tag + ">";
            std::string close_tag = std::string("</") + tag + ">";
            size_t pos = 0;
            while ((pos = xml.find(open_tag, pos)) != std::string::npos) {
                size_t gt = xml.find('>', pos);
                size_t end = xml.find(close_tag, gt);
                if (gt == std::string::npos || end == std::string::npos) break;
                vals.push_back(xml.substr(gt + 1, end - gt - 1));
                pos = end + close_tag.size();
            }
            return vals;
        };

        for (auto& v : find_str("scan:ColorMode")) {
            if (v == "RGB24") r.value.modes.push_back(color_mode::Color);
            else if (v == "Grayscale8") r.value.modes.push_back(color_mode::Gray);
            else if (v == "BlackAndWhite1") r.value.modes.push_back(color_mode::Mono);
        }
        if (r.value.modes.empty()) {
            r.value.modes = {color_mode::Color, color_mode::Gray, color_mode::Mono};
        }
                // ── pwg:InputSource (some printers) ──────────────────────────
        for (auto& v : find_str("pwg:InputSource")) {
            if (v == "Platen") { r.value.has_flatbed = true; r.value.sources.push_back(u"Platen"); }
            else if (v == "Feeder") { r.value.has_adf = true; r.value.sources.push_back(u"Feeder"); }
            else if (v == "FeederDuplex") { r.value.has_duplex_adf = true; r.value.sources.push_back(u"FeederDuplex"); }
        }

        // ── <scan:Platen> — some printers (e.g. HP 7740) report the
        //     flatbed here instead of pwg:InputSource.
        if (xml.find("<scan:Platen>") != std::string::npos ||
            xml.find("<scan:PlatenInputCaps>") != std::string::npos) {
            r.value.has_flatbed = true;
            if (std::find(r.value.sources.begin(), r.value.sources.end(), u"Platen")
                    == r.value.sources.end()) {
                r.value.sources.push_back(u"Platen");
            }
        }

        // ── scan:AdfSimplexInputCaps / AdfDuplexInputCaps ─────────────
        // HP (e.g. 7740) reports ADF only via these tags. Without this
        // block has_adf stays false and ADF is never visible.
        bool adf_simplex = !find_str("scan:AdfSimplexInputCaps").empty();
        bool adf_duplex  = !find_str("scan:AdfDuplexInputCaps").empty();

        // Verify physical ADF via <scan:AdfState> (Empty/Loaded).
        auto adf_state_xml = find_str("scan:AdfState");
        bool adf_attached = false;
        if (!adf_state_xml.empty()) {
            std::string s = detail::lower(adf_state_xml[0]);
            adf_attached = (s.find("empty") != std::string::npos ||
                            s.find("loaded") != std::string::npos);
        }

        if (adf_simplex || adf_duplex || adf_attached) {
            r.value.has_adf = true;
            if (std::find(r.value.sources.begin(), r.value.sources.end(), u"Feeder") == r.value.sources.end())
                r.value.sources.push_back(u"Feeder");
        }
        if (adf_duplex) {
            r.value.has_duplex_adf = true;
            if (std::find(r.value.sources.begin(), r.value.sources.end(), u"FeederDuplex") == r.value.sources.end())
                r.value.sources.push_back(u"FeederDuplex");
        }

        return r;
    }

    // ══════════════════════════════════════════════════════════════════
    //  scan_jpegs — internal helper (eSCL native JPEG output)
    //  Every high-level API (scan_to_pdf / scan_page / scan_to_file /
    //  scan_to_files / scan_and_print_to) goes through this.
    // ══════════════════════════════════════════════════════════════════
    result<knst_vector<knst_c16string>> scan_jpegs(detail::temp_set& tmp,
                                                    const scan_options& o = scan_options()) const {
        using R = result<knst_vector<knst_c16string>>;
        std::string settings = build_scan_settings(o);
        auto post = http_post(u"/eSCL/ScanJobs", "text/xml", settings);
        if (!post) return R::fail(post.st, post.native);
        if (post.value.status != 201 && post.value.status != 200) {

            return R::fail(status::IoError, post.value.status);
        }

        knst_c16string loc = post.value.header_value("Location");
        if (loc.empty()) return R::fail(status::IoError);
        std::string loc_path = detail::to_u8(loc);
        size_t scheme = loc_path.find("://");
        if (scheme != std::string::npos) {
            size_t slash = loc_path.find('/', scheme + 3);
            if (slash != std::string::npos) loc_path = loc_path.substr(slash);
        }

        R out;
        uint32_t max_pages = o.page_count > 0 ? o.page_count : 100;
        for (uint32_t page = 0; page < max_pages; ++page) {
            bool got_doc = false;
            for (int attempt = 0; attempt < 60; ++attempt) {
                auto doc = http_get(loc_path + "/NextDocument");
                if (!doc) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
                if (doc.value.status == 503) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
                if (doc.value.status == 404 || doc.value.status == 410) break;
                if (doc.value.status != 200 || doc.value.body.empty()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                knst_c16string jp = tmp.add(u".jpg");
                if (!knst_file::write_file_atomic(jp, doc.value.body.data(), doc.value.body.length()))
                    break;
                out.value.push_back(jp);
                got_doc = true;

                break;
            }
            if (!got_doc) break;
            if (!o.use_adf) break;
        }

        if (out.value.empty()) {

            return R::fail(status::IoError);
        }
        return out;
    }

    // ---- POST ScanJobs → GET NextDocument → PDF ----
    result<knst_c16string> scan_to_pdf(const knst_c16string& path,
                                       const scan_options& o = scan_options()) {
        using R = result<knst_c16string>;
        if (path.empty()) return R::fail(status::InvalidArgument);

        detail::temp_set tmp;
        auto jpegs = scan_jpegs(tmp, o);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);

        float w = 210.0f, h = 297.0f;
        detail::paper_mm(o.paper, w, h);
        std::vector<knst_c16string> v(jpegs.value.begin(), jpegs.value.end());
        image_batch_options bo;
        bo.margin_mm = 0.0f;
        bo.fit = fit_mode::Fit;
        bo.apply_exif = false;
        bo.skip_unreadable = false;
        bo.max_dpi = std::max<uint32_t>(150, o.dpi);
        detail::document doc;
        auto st = detail::layout_images(v, bo, w, h, doc);
        if (st != status::Ok) return R::fail(st);
        knst_c16string abs = knst_file::resolve_path(path);
        st = detail::write_pdf(doc, abs);
        if (st != status::Ok) return R::fail(st);

        return R::success(abs);
    }

    result<knst_byte_string> scan_page(const scan_options& o = scan_options()) {
        using R = result<knst_byte_string>;
        detail::temp_set tmp;
        scan_options oo = o; oo.page_count = 1;
        auto jpegs = scan_jpegs(tmp, oo);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);
        knst_file_error fe = knst_file_error::None;
        knst_byte_string b = knst_file::read_file_data<>(jpegs.value[0], &fe);
        if (fe != knst_file_error::None) return R::fail(status::IoError);
        return R::success(std::move(b));
    }

    result<knst_c16string> scan_to_file(const knst_c16string& path,
                                        const scan_options& o = scan_options()) {
        using R = result<knst_c16string>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        std::string fmt = detail::lower(detail::to_u8(o.output_format));
        if (fmt == "pdf") return scan_to_pdf(path, o);

        detail::temp_set tmp;
        auto jpegs = scan_jpegs(tmp, o);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);

        knst_c16string abs = knst_file::resolve_path(path);
        const bool want_png = (fmt == "png");
        const std::string actual_ext = want_png ? ".png" : ".jpg";
        if (jpegs.value.size() == 1) {
            if (want_png) {
                // Real PNG: JPEG → decode → write PNG
                if (!detail::decode_to_png(jpegs.value[0], abs))
                    return R::fail(status::DecodeFailed);
            } else {
                if (!knst_file::move_path(jpegs.value[0], abs, true))
                    return R::fail(status::IoError);
            }
            return R::success(abs);
        }

        // Multi-page (ADF): prefix_001.png/.jpg, prefix_002.png/.jpg ...
        knst_c16string parent = knst_file::get_parent_dir(abs);
        std::string stem = detail::to_u8(knst_file::get_stem(abs));
        if (stem.empty()) stem = "scan";
        knst_c16string first;
        for (size_t i = 0; i < jpegs.value.size(); ++i) {
            char suffix[20];
            std::snprintf(suffix, sizeof(suffix), "_%03u%s", (unsigned)(i + 1), actual_ext.c_str());
            knst_c16string dst = knst_file::join_path(parent, detail::from_u8(stem + suffix));
            if (want_png) {
                if (!detail::decode_to_png(jpegs.value[i], dst))
                    return R::fail(status::DecodeFailed);
            } else {
                if (!knst_file::move_path(jpegs.value[i], dst, true))
                    return R::fail(status::IoError);
            }
            if (i == 0) first = dst;
        }
        return R::success(first);
    }

    // ─── Multi-page ADF scan → one file per page ─────────────────────────
    // Pulls NextDocument one by one from ADF printers (e.g. HP 7740),
    // saves each as "prefix_001.jpg", "prefix_002.jpg", ... and returns
    // every path. Use scan_to_pdf when output_format=pdf.
       result<knst_vector<knst_c16string>> scan_to_files(const knst_c16string& path_prefix,
                                                       const scan_options& o = scan_options()) {
        using R = result<knst_vector<knst_c16string>>;
        if (path_prefix.empty()) return R::fail(status::InvalidArgument);
        std::string fmt = detail::lower(detail::to_u8(o.output_format));
        if (fmt == "pdf") return R::fail(status::InvalidArgument);

        detail::temp_set tmp;
        auto jpegs = scan_jpegs(tmp, o);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);

        knst_c16string abs = knst_file::resolve_path(path_prefix);
        knst_c16string parent = knst_file::get_parent_dir(abs);
        if (parent.empty()) parent = knst_file::get_temp_dir();
        std::string stem = detail::to_u8(knst_file::get_stem(abs));
        if (stem.empty()) stem = "scan";

        const bool want_png = (fmt == "png");
        const std::string actual_ext = want_png ? ".png" : ".jpg";

        R out;
        for (size_t i = 0; i < jpegs.value.size(); ++i) {
            char suffix[20];
            std::snprintf(suffix, sizeof(suffix), "_%03u%s", (unsigned)(i + 1), actual_ext.c_str());
            knst_c16string dst = knst_file::join_path(parent, detail::from_u8(stem + suffix));
            if (want_png) {
                if (!detail::decode_to_png(jpegs.value[i], dst))
                    return R::fail(status::DecodeFailed);
            } else {
                if (!knst_file::move_path(jpegs.value[i], dst, true))
                    return R::fail(status::IoError);
            }
            out.value.push_back(dst);
        }
        return out;
    }

    result<uint32_t> scan_and_print(const print_options& popt = print_options(),
                                     const scan_options& sopt = scan_options()) {
        // On Android the printer object opens a dialog; name_ is unused.
        printer p;
        return scan_and_print_to(p, popt, sopt);
    }
    result<uint32_t> scan_and_print_to(printer& pr, const print_options& popt = print_options(),
                                        const scan_options& sopt = scan_options()) {
        using R = result<uint32_t>;
        detail::temp_set tmp;
        auto jpegs = scan_jpegs(tmp, sopt);
        if (!jpegs) return R::fail(jpegs.st, jpegs.native);

        std::vector<knst_c16string> v(jpegs.value.begin(), jpegs.value.end());
        image_batch_options bo;
        bo.margin_mm = popt.borderless ? 0.0f : 5.0f;
        bo.fit = fit_mode::Fit;
        bo.apply_exif = false;
        bo.skip_unreadable = false;


        return pr.print_images(v, bo, popt);
    }

private:
    knst_c16string name_;
    knst_c16string device_id_;   // "host:port"

    // ─── Minimal HTTP/1.1 client over raw socket ─────────────────────────
    struct http_response {
        int status = 0;
        std::string headers;
        knst_byte_string body;

        knst_c16string header_value(const char* name) const {
            std::string n(name);
            for (size_t i = 0; i + n.size() <= headers.size(); ++i) {
                bool match = true;
                for (size_t j = 0; j < n.size(); ++j) {
                    char a = (char)std::tolower((unsigned char)headers[i + j]);
                    char b = (char)std::tolower((unsigned char)n[j]);
                    if (a != b) { match = false; break; }
                }
                if (match && i + n.size() < headers.size() && headers[i + n.size()] == ':') {
                    size_t v = i + n.size() + 1;
                    while (v < headers.size() && (headers[v] == ' ' || headers[v] == '\t')) ++v;
                    size_t e = v;
                    while (e < headers.size() && headers[e] != '\r' && headers[e] != '\n') ++e;
                    return knst_c16string(headers.substr(v, e - v).c_str());
                }
            }
            return knst_c16string();
        }
    };

       result<http_response> http_request(const knst_c16string& method,
                                       const knst_c16string& path,
                                       const std::string& content_type,
                                       const std::string& body) const {
        using R = result<http_response>;

        std::string hp = detail::to_u8(device_id_);
        size_t colon = hp.rfind(':');
        if (colon == std::string::npos) return R::fail(status::InvalidArgument);
        std::string host = hp.substr(0, colon);
        int port = std::atoi(hp.c_str() + colon + 1);
        if (port <= 0) port = 80;

        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = nullptr;
        std::string port_str = std::to_string(port);
        if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res)
            return R::fail(status::NotFound);

        int fd = -1;
        int last_errno = 0;
        for (auto* ai = res; ai; ai = ai->ai_next) {
            fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0) { last_errno = errno; continue; }
            struct timeval tv{};
            tv.tv_sec = 60; tv.tv_usec = 0;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
            last_errno = errno;

            ::close(fd); fd = -1;
        }
        ::freeaddrinfo(res);
        if (fd < 0) {

            return R::fail(status::IoError, last_errno);
        }

        std::string method_s = detail::to_u8(method);
        std::string path_s   = detail::to_u8(path);
        std::string req = method_s + " " + path_s + " HTTP/1.1\r\n";
        req += "Host: " + host + ":" + port_str + "\r\n";
        req += "Connection: close\r\n";
        req += "User-Agent: KernelNucleusT/1.0\r\n";
        req += "Accept: */*\r\n";
        if (!content_type.empty()) req += "Content-Type: " + content_type + "\r\n";
        if (!body.empty())        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        else if (method_s == "POST") req += "Content-Length: 0\r\n";
        req += "\r\n";
        req += body;

        const char* p = req.c_str();
        size_t left = req.size();
        while (left > 0) {
            ssize_t n = ::send(fd, p, left, MSG_NOSIGNAL);
            if (n <= 0) { ::close(fd); return R::fail(status::IoError); }
            p += n; left -= (size_t)n;
        }

        std::string raw;
        char buf[16384];
        size_t header_end     = std::string::npos;
        size_t content_length = std::string::npos; 
        bool   chunked        = false;
        bool   timed_out      = false;

        for (;;) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) timed_out = true;
                break;
            }
            if (n == 0) break;
            raw.append(buf, (size_t)n);

            // Check whether the headers have fully arrived.
            if (header_end == std::string::npos) {
                header_end = raw.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    std::string headers = raw.substr(0, header_end);
                    std::string low = headers;
                    for (auto& c : low) c = (char)std::tolower((unsigned char)c);

                    // Content-Length
                    size_t cl = low.find("content-length:");
                    if (cl != std::string::npos) {
                        size_t vstart = cl + 15;
                        while (vstart < headers.size() &&
                               (headers[vstart] == ' ' || headers[vstart] == '\t')) ++vstart;
                        content_length = std::strtoul(headers.c_str() + vstart, nullptr, 10);
                    }

                    // Transfer-Encoding: chunked
                    size_t te = low.find("transfer-encoding:");
                    if (te != std::string::npos && low.find("chunked", te) != std::string::npos)
                        chunked = true;
                }
            }

            if (header_end != std::string::npos) {
                size_t body_have = raw.size() - (header_end + 4);

                // Content-Length satisfied → stop reading early.
                if (!chunked && content_length != std::string::npos &&
                    body_have >= content_length) break;

                // Chunked: stop once the terminating "0\r\n\r\n" is seen.
                if (chunked && body_have >= 5) {
                    const char* tail = raw.data() + raw.size() - 5;
                    if (std::memcmp(tail, "0\r\n\r\n", 5) == 0) break;
                }
            }

            if (raw.size() > 64 * 1024 * 1024) break;
        }
        ::close(fd);

        if (header_end == std::string::npos) {

            return R::fail(timed_out ? status::Timeout : status::IoError);
        }

        http_response r;
        r.headers = raw.substr(0, header_end);
        std::string body_raw = raw.substr(header_end + 4);

        // Decode if chunked.
        if (chunked) {
            std::string decoded;
            size_t pos = 0;
            while (pos < body_raw.size()) {
                size_t eol = body_raw.find("\r\n", pos);
                if (eol == std::string::npos) break;
                std::string szstr = body_raw.substr(pos, eol - pos);
                size_t semi = szstr.find(';');
                if (semi != std::string::npos) szstr = szstr.substr(0, semi);
                unsigned long chunk_sz = std::strtoul(szstr.c_str(), nullptr, 16);
                pos = eol + 2;
                if (chunk_sz == 0) break;
                if (pos + chunk_sz > body_raw.size()) {
                    decoded.append(body_raw, pos, body_raw.size() - pos);
                    break;
                }
                decoded.append(body_raw, pos, chunk_sz);
                pos += chunk_sz;
                if (pos + 2 <= body_raw.size() &&
                    body_raw[pos] == '\r' && body_raw[pos + 1] == '\n') pos += 2;
            }
            body_raw = std::move(decoded);
        } else if (content_length != std::string::npos && body_raw.size() > content_length) {
            body_raw.resize(content_length);
        }

        r.body = knst_byte_string(
            reinterpret_cast<const unsigned char*>(body_raw.data()),
            (uint32_t)body_raw.size());

        size_t sp = r.headers.find(' ');
        if (sp != std::string::npos) r.status = std::atoi(r.headers.c_str() + sp + 1);


        return R::success(std::move(r));
    }

    result<http_response> http_get(const knst_c16string& path) const {
        return http_request(u"GET", path, std::string(), std::string());
    }
    result<http_response> http_post(const knst_c16string& path,
                                     const std::string& ct,
                                     const std::string& body) const {
        return http_request(u"POST", path, ct, body);
    }

    // ─── ScanSettings XML (HP/PWG compatible) ────────────────────────────
    std::string build_scan_settings(const scan_options& o) const {
        float w_mm = 210.0f, h_mm = 297.0f;
        detail::paper_mm(o.paper, w_mm, h_mm);
        uint32_t w_region = (uint32_t)(w_mm / 25.4f * 300.0f + 0.5f);
        uint32_t h_region = (uint32_t)(h_mm / 25.4f * 300.0f + 0.5f);
        uint32_t dpi = o.dpi ? o.dpi : 300;

        const char* source = "Platen";
        if (o.use_adf) source = o.duplex ? "FeederDuplex" : "Feeder";
        if (!o.source.empty()) {
            std::string s = detail::to_u8(o.source);
            if (s.find("Duplex") != std::string::npos) source = "FeederDuplex";
            else if (s == "ADF" || s == "Feeder")         source = "Feeder";
            else if (s == "Platen" || s == "Flatbed")     source = "Platen";
        }

        // eSCL expects "Feeder" (simplex) or "FeederDuplex" as the ADF
        // source. Some HP firmware rejects "ADF" → use "Feeder".
        if (o.use_adf && source == std::string("ADF")) source = "Feeder";

        const char* cm = "RGB24";
        if (o.mode == color_mode::Gray) cm = "Grayscale8";
        else if (o.mode == color_mode::Mono) cm = "BlackAndWhite1";

        std::string xml;
        xml.reserve(600);
        xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        xml += "<scan:ScanSettings xmlns:pwg=\"http://www.pwg.org/schemas/2010/12/sm\"";
        xml += " xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\">";
        xml += "<pwg:Version>2.0</pwg:Version>";
        xml += "<pwg:ScanRegions><pwg:ScanRegion>";
        xml += "<pwg:ContentRegionUnits>escl:ThreeHundredthsOfInches</pwg:ContentRegionUnits>";
        xml += "<pwg:XOffset>0</pwg:XOffset><pwg:YOffset>0</pwg:YOffset>";
        xml += "<pwg:Width>" + std::to_string(w_region) + "</pwg:Width>";
        xml += "<pwg:Height>" + std::to_string(h_region) + "</pwg:Height>";
        xml += "</pwg:ScanRegion></pwg:ScanRegions>";
        xml += std::string("<pwg:InputSource>") + source + "</pwg:InputSource>";
        xml += "<pwg:DocumentFormat>image/jpeg</pwg:DocumentFormat>";
        xml += "<scan:DocumentFormatExt>image/jpeg</scan:DocumentFormatExt>";
        xml += std::string("<scan:ColorMode>") + cm + "</scan:ColorMode>";
        xml += "<scan:XResolution>" + std::to_string(dpi) + "</scan:XResolution>";
        xml += "<scan:YResolution>" + std::to_string(dpi) + "</scan:YResolution>";
        if (o.brightness != 0) {
            int b = 500 + o.brightness * 5;
            if (b < 0) b = 0; if (b > 1000) b = 1000;
            xml += "<scan:Brightness>" + std::to_string(b) + "</scan:Brightness>";
        }
        if (o.contrast != 0) {
            int c = 500 + o.contrast * 5;
            if (c < 0) c = 0; if (c > 1000) c = 1000;
            xml += "<scan:Contrast>" + std::to_string(c) + "</scan:Contrast>";
        }
        xml += "</scan:ScanSettings>";
        return xml;
    }
};

#endif // __ANDROID__

#if KNST_PRN_HAS_NETWORK
inline result<uint32_t> print_ipp_smart(const knst_c16string& pdf_path, const knst_c16string& host_port,
                                        const print_options& o, const detail::ipp::send_policy& pol,
                                        std::string* info) {
    using R = result<uint32_t>;
    namespace ip = detail::ipp;
    ip::last_diag().clear();
    if (pdf_path.empty() || host_port.empty()) return R::fail(status::InvalidArgument);

    std::string hp0; bool tls0 = false; uint16_t port0 = 631;
    if (!ip::split_uri(detail::to_u8(host_port), hp0, tls0, port0))
        return R::fail(status::InvalidArgument);
    const std::string host = hp0.substr(0, hp0.rfind(':'));

    struct cand { std::string hp; bool tls; };
    std::vector<cand> cands{ {hp0, tls0}, tls0 ? cand{host + ":631", false}
                                                : cand{host + ":443", true} };
    ip::printer_caps caps; std::string hp; bool tls = false, found = false;
    for (auto& c : cands) {
        auto pr = ip::probe(c.hp, c.tls);
        if (pr) { caps = pr.value; hp = c.hp; tls = c.tls; found = true; break; }
    }
    if (!found) { ip::last_diag() = "printer unreachable: " + hp0; return R::fail(status::IoError); }
    if (knst_prn::status st = ip::preflight(caps); st != status::Ok) return R::fail(st);

    const std::string uri = std::string(tls ? "ipps" : "ipp") + "://" + hp + "/ipp/print";
    char tok[16];
    std::snprintf(tok, sizeof(tok), "%08x",
                  static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu));
    const std::string name = (o.title.empty() ? std::string("KNST document")
                                              : detail::to_u8(o.title)) + " #" + tok;

    auto finish = [&](const std::vector<uint8_t>& body, const std::string& nm) -> R {
        auto j = ip::submit_job(hp, tls, body, nm, pol);
        if (!j) return j;
        if (pol.wait_for_completion) {
            auto w = ip::wait_job(hp, tls, j.value, pol);
            if (!w) return R::fail(w.st, w.native);
        }
        return j;
    };

    // 1) Printer accepts PDF directly
    if (caps.fmt("application/pdf") && (pol.prefer.empty() || pol.prefer == "pdf")) {
        knst_file_error fe = knst_file_error::None;
        knst_byte_string pdf = knst_file::read_file_data<>(pdf_path, &fe);
        if (fe != knst_file_error::None || pdf.empty()) return R::fail(status::IoError);
        ip::request req; ip::begin_print_job(req, uri, name, "application/pdf", o);
        req.append_doc(pdf.data(), pdf.length());
        if (info) *info = "pdf";
        return finish(req.buf, name);
    }

    const bool can_pwg = caps.fmt("image/pwg-raster"), can_jpg = caps.fmt("image/jpeg");
    if (!can_pwg && !can_jpg) {
        ip::last_diag() = "printer formats: " + caps.formats;
        return R::fail(status::NotSupported);
    }
    bool use_pwg = can_pwg && pol.prefer != "jpeg";
    if (pol.prefer == "pwg" && !can_pwg) return R::fail(status::NotSupported);

    float wmm = 210.0f, hmm = 297.0f;
    if (!detail::paper_mm(o.paper, wmm, hmm) &&
        !detail::paper_mm(detail::paper_from_name(caps.media_default), wmm, hmm)) {
        wmm = 210.0f; hmm = 297.0f;
    }

    const bool gray_only = !caps.pwg_types.empty() &&
                           caps.pwg_types.find("srgb_8") == std::string::npos &&
                           caps.pwg_types.find("sgray_8") != std::string::npos;
    const uint32_t ch  = gray_only ? 1u : 3u;
    const uint32_t dpi = use_pwg ? ip::pick_dpi(caps.pwg_res) : 300u;
    const uint32_t W   = static_cast<uint32_t>(std::lround(wmm / 25.4f * dpi));
    const uint32_t H   = static_cast<uint32_t>(std::lround(hmm / 25.4f * dpi));

    knst_c16string dir = detail::make_temp(u"");
    knst_file::create_directories(dir);
    struct guard { knst_c16string p; ~guard() { knst_file::remove_all(p); } } g{dir};
    auto jpegs = detail::rasterize_pdf_to_jpegs(pdf_path, dir, dpi, use_pwg ? 92u : 90u);
    if (jpegs.empty()) {
        ip::last_diag() = "no rasterizer (install poppler-utils or ImageMagick)";
        return R::fail(status::ToolMissing);
    }

    if (use_pwg && can_jpg &&
        static_cast<uint64_t>(jpegs.size()) * W * H * ch > (static_cast<uint64_t>(pol.max_body_mb) << 20))
        use_pwg = false;

    // 2) PWG raster: ALL pages in ONE job
    if (use_pwg) {
        auto run_pwg = [&]() -> R {
            ip::request req; ip::begin_print_job(req, uri, name, "image/pwg-raster", o);
            static const uint8_t sync[4] = { 'R', 'a', 'S', '2' };
            req.buf.insert(req.buf.end(), sync, sync + 4);
            for (size_t i = 0; i < jpegs.size(); ++i) {
                knst_file_error fe = knst_file_error::None;
                knst_byte_string bytes = knst_file::read_file_data<>(jpegs[i], &fe);
                knst_byte_string rgb; uint32_t w = 0, h = 0;
                if (fe != knst_file_error::None || !detail::decode_rgb(bytes, rgb, w, h)) {
                    ip::last_diag() = "page " + std::to_string(i + 1) + " decode failed";
                    return R::fail(status::DecodeFailed);
                }
                std::vector<uint8_t> canvas(static_cast<size_t>(W) * H * ch, 255);
                const uint32_t cw = std::min(w, W), chh = std::min(h, H);
                const uint32_t sx = (w - cw) / 2, sy = (h - chh) / 2;
                const uint32_t dx = (W - cw) / 2, dy = (H - chh) / 2;
                for (uint32_t y = 0; y < chh; ++y) {
                    const uint8_t* s = rgb.data() + (static_cast<size_t>(sy + y) * w + sx) * 3;
                    uint8_t* d = &canvas[(static_cast<size_t>(dy + y) * W + dx) * ch];
                    if (ch == 3) std::memcpy(d, s, static_cast<size_t>(cw) * 3);
                    else for (uint32_t x = 0; x < cw; ++x, s += 3)
                        d[x] = static_cast<uint8_t>((s[0] * 77 + s[1] * 151 + s[2] * 28) >> 8);
                }
                ip::pwg_add_page(req.buf, canvas.data(), W, H, ch, dpi,
                                 static_cast<uint32_t>(std::lround(wmm * detail::kMm2Pt)),
                                 static_cast<uint32_t>(std::lround(hmm * detail::kMm2Pt)));
            }
            return finish(req.buf, name);
        };
        R r = run_pwg();
        if (r || !can_jpg || r.st == status::Timeout) {
            if (r && info) *info = "pwg-raster " + std::to_string(dpi) + "dpi " +
                                   (ch == 1 ? "gray" : "rgb") + ", " +
                                   std::to_string(jpegs.size()) + " pages, 1 job";
            return r;
        }
        ip::last_diag() += " (pwg failed -> jpeg fallback)";
    }

    // 3) JPEG: one Print-Job per page, sequential, each finished before next
    uint32_t first = 0;
    for (size_t i = 0; i < jpegs.size(); ++i) {
        knst_file_error fe = knst_file_error::None;
        knst_byte_string b = knst_file::read_file_data<>(jpegs[i], &fe);
        if (fe != knst_file_error::None || b.empty()) return R::fail(status::IoError);
        const std::string nm = name + " p" + std::to_string(i + 1);
        ip::request req; ip::begin_print_job(req, uri, nm, "image/jpeg", o);
        req.append_doc(b.data(), b.length());
        R r = finish(req.buf, nm);
        if (!r) {
            ip::last_diag() = "page " + std::to_string(i + 1) + "/" +
                              std::to_string(jpegs.size()) + " failed (" +
                              std::to_string(i) + " page(s) printed): " + ip::last_diag();
            return r;
        }
        if (!first) first = r.value;
    }
    if (info) *info = "jpeg, " + std::to_string(jpegs.size()) + " jobs";
    return R::success(first);
}
#endif

} // namespace knst_prn