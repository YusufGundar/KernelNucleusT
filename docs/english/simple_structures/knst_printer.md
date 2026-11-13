# knst_prn — Printer and Scanner Control Library

Hey! 👋 The `knst_prn` namespace is a cross-platform framework that brings printer and scanner operations under **one API**. Programmatic printing, PDF generation, IPP/IPPS, scanner scanning, job control, test pages, image batching, supply levels, and special fixes for modern printers — all here.

**In short:** On Linux, CUPS + its own IPP client. On Windows, winspool + GDI + optional IPP. On Android, PrintManager dialog + USB OTG + Bluetooth SPP + eSCL. **No FFmpeg, no external PDF library** — the PDF writer and PWG Raster encoder live inside this library.

---

## 🎯 Honesty Matrix — What's Available Per Platform?

| Feature | Linux (CUPS) | Windows | Android |
|---|---|---|---|
| **`print_text` / `print_rich`** | ✅ CUPS → `lp` | ✅ GDI → spooler | ✅ PrintManager dialog |
| **`print_image` / `print_images`** | ✅ CUPS | ✅ GDI | ✅ PrintManager dialog |
| **`print_file` / `print_auto`** | ✅ CUPS | ⚠️ PDF needs reader | ✅ PrintManager |
| **`print_pdf`** | ✅ CUPS | ⚠️ PDF reader required | ✅ PrintManager |
| **`send_raw` (ESC/POS, PCL, ZPL)** | ✅ raw mode | ✅ RAW datatype | ✅ USB OTG bulk |
| **`print_network` (TCP 9100)** | ✅ | ✅ | ✅ |
| **Direct IPP (`print_ipp`)** | ✅ (+ `ipps://` TLS) | ✅ (+ TLS) | ✅ (+ TLS) |
| **`print_ipp_send_doc`** | ✅ | ✅ | ✅ |
| **`print_ipp_smart`** | ✅ probe + PDF/PWG/JPEG | ✅ | ✅ |
| **`print_universal`** | ✅ IPP → CUPS | ✅ IPP → winspool | ✅ IPP → dialog |
| **`print_secure` (PIN)** | ✅ job-password | ❌ | ❌ |
| **`print_held`** | ✅ job-hold-until | ❌ | ❌ |
| **`print_test_page`** | ✅ | ✅ | ✅ |
| **`print_usb` (bulk)** | ❌ | ❌ | ✅ OTG |
| **`print_bluetooth`** | ❌ | ❌ | ✅ SPP |
| **Printer list (`list_all`)** | ✅ `lpstat -p` | ✅ `EnumPrintersW` | ✅ mDNS |
| **Printer info / capabilities** | ✅ | ⚠️ GDI DeviceCapabilities | ✅ IPP |
| **Supply levels** | ✅ | ❌ | ✅ IPP |
| **Alerts** | ✅ | ⚠️ state-based | ✅ IPP |
| **Job management** | ✅ full | ✅ spooler | ✅ IPP |
| **Admin (pause/resume/options)** | ✅ CUPS | ⚠️ limited | ❌ |
| **Cache / reprint** | ✅ | ✅ | ❌ |
| **PDF preview (`render_preview`)** | ✅ pdftoppm | ❌ | ⚠️ JNI helper |
| **Scanner `list_all`** | ✅ SANE | ⚠️ WIA (MSVC) / eSCL (MinGW) | ✅ mDNS |
| **Scanner `scan_page` / `scan_to_*`** | ✅ SANE | ✅ WIA / eSCL | ✅ eSCL |
| **Scanner ADF / duplex** | ✅ | ⚠️ device | ✅ eSCL |

**Critical notes:**
- **Windows PDF:** winspool needs a reader registered for "printto" (**Acrobat/SumatraPDF**) to print PDFs. Missing → `NotSupported`. To bypass, use `print_universal(path, "printer:631")` (direct IPP).
- **No WIA in MinGW builds.** Scanner uses eSCL (HTTP+XML).
- **On Android, N files = N PrintManager dialogs.** PrintManager accepts one job at a time.
- **`print_network`** with **PDF** — PDF bytes to port 9100 raw will print **blank pages**. Only use it for raw printer languages like ESC/POS, PCL, ZPL.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Own PDF writer** | TTF subsetting, DEFLATE, WinAnsi + Turkish codepoint map |
| **PWG Raster encoder** | RFC 5102.4, PackBits, `RaS2` sync |
| **IPP client** | RFC 8010/8011, `ipp://` + `ipps://` (TLS) |
| **Smart IPP** | PDF/PWG/JPEG selection, busy retry, wait-for-completion |
| **30+ print options** | duplex, color, media, n-up, booklet, staple, punch, fold, margin, fit-to-page, job-sheets |
| **Rich document** | text runs, bold/italic, color, alignment, header/footer, page numbers, draw commands |
| **TTF embed** | Unicode support (Cyrillic, Arabic, CJK, ...) — auto-resolve from system fonts |
| **Image batch** | N photos → 1 job, grid layout, EXIF, fit/fill/stretch, max_dpi downscale |
| **Test page** | 6 types: Diagnostic, ColorBar, Alignment, Grid, Gradient, NozzleCheck |
| **Supply / alerts** | marker-levels, low warning, state alerts |
| **Scanner (SANE / WIA / eSCL)** | Linux SANE, Windows WIA or eSCL, Android eSCL |
| **USB OTG / Bluetooth SPP** | Raw printer (thermal, label) support on Android |
| **Reprint cache** | Job → temp file mapping, reprint later |
| **HP 7740 fixes** | airscan first, Send-Document flow, geometry retry, ADF single-format |

---

## 🚀 Quick Start

### Basic text print

```cpp
#include "KernelNucleusT.hpp"

auto pr = knst_prn::printer::open_any();
if (!pr) return 1;

knst_prn::print_options opts;
opts.title = u"Hello test";
opts.copies = 2;
opts.duplex = knst_prn::duplex_mode::LongEdge;

auto job = pr.value.print_text(u"Hello world!", opts);
if (job) {
    std::cout << "Job ID: " << job.value << "\n";
}
```

### Direct IPP (skip CUPS)

```cpp
auto r = knst_prn::printer::print_ipp(
    u"/path/to/file.pdf",
    u"192.168.1.50:631",           // or "ipps://printer.local"
    631,
    opts);

if (!r) std::cerr << knst_prn::status_string(r.st) << "\n";
```

### Smart IPP (PDF/PWG/JPEG auto-selection)

```cpp
auto r = knst_prn::print_ipp_smart(
    u"/path/file.pdf",
    u"192.168.1.50");               // port auto (631 or 443)

if (r) std::cout << "Job: " << r.value << "\n";
```

### Image batch (10 photos → 1 job)

```cpp
knst_prn::image_batch_options batch;
batch.rows = 2;
batch.cols = 2;
batch.fit  = knst_prn::fit_mode::Fit;
batch.margin_mm = 8.0f;
batch.gap_mm    = 4.0f;
batch.border_mm = 0.5f;
batch.copies_each = 1;

knst_vector<knst_c16string> photos = {u"a.jpg", u"b.jpg", /* ... */};
pr.value.print_images(photos, batch, opts);
```

### Scanner

```cpp
auto scanners = knst_prn::scanner::list_all();
if (scanners && !scanners.value.empty()) {
    auto sc = knst_prn::scanner::open(scanners.value[0].device_id);
    if (sc) {
        knst_prn::scan_options so;
        so.dpi = 300;
        so.mode = knst_prn::color_mode::Color;
        so.paper = knst_prn::media_size::A4;
        so.output_format = u"pdf";

        sc.value.scan_to_pdf(u"/tmp/scan.pdf", so);
    }
}
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Open printer** | `open`, `open_default`, `open_any` |
| **Printer state** | `status`, `info`, `make_and_model`, `capabilities`, `supply_levels`, `alerts`, `usage` |
| **Printing** | `print_text`, `print_rich`, `print_image`, `print_images`, `print_batch`, `print_file`, `print_auto`, `print_pdf` |
| **Raw** | `send_raw`, `send_escpos`, `send_zpl`, `send_pcl`, `print_network` |
| **IPP** | `print_ipp`, `print_ipp_send_doc`, `print_jpeg_ipp`, `print_universal` |
| **Job control** | `jobs`, `job_details`, `cancel_job`, `cancel_all`, `cancel_by_user`, `hold_job`, `release_job`, `set_job_priority`, `move_job`, `wait_for_job`, `is_job_done` |
| **Admin** | `pause`, `resume`, `set_default`, `options`, `set_default_option`, `set_option_permanent`, `add_printer`, `remove_printer`, `list_classes` |
| **Cache** | `set_cache`, `clear_cache`, `cache_size_bytes`, `reprint_job` |
| **Helpers** | `pdf_page_count`, `render_preview` |
| **Static** | `list_all`, `list_available_devices`, `list_drivers`, `server_status` |
| **Scanner** | `list_all`, `open`, `capabilities`, `scan_page`, `scan_to_file`, `scan_to_files`, `scan_to_pdf`, `scan_and_print` |

---

## 🎯 1) Enums and Types

### `status` — General error enum

```cpp
enum class status : uint8_t {
    Ok = 0, InvalidArgument, NotFound, IoError, Timeout, BadState,
    OutOfMemory, ToolMissing, NotSupported, Permission, DecodeFailed, Unknown
};
```

Use `status_string()` for a readable string.

### `printer_state`

```cpp
enum class printer_state : uint8_t {
    Unknown = 0, Ready, Paused, Error, PaperJam, PaperOut,
    TonerLow, DoorOpen, Offline, Busy, WarmingUp
};
```

**Linux:** Derived from `printer-state-reasons` — **only `-error` severity is blocking**. `media-empty-report`, `marker-supply-low-warning` are informational, returns `TonerLow`.

**Windows:** `PRINTER_STATUS_*` flags.

**Android:** IPP `printer-state` + reasons.

### `job_state`

```cpp
enum class job_state : uint8_t {
    Unknown = 0, Pending, Held, Processing, Rendering, Spooling,
    Stopped, Blocked, Canceled, Aborted, Completed, Retrying
};
```

### `duplex_mode` / `color_mode` / `orientation` / `print_quality` / `media_size`

```cpp
enum class duplex_mode   : uint8_t { Default = 0, Simplex, LongEdge, ShortEdge };
enum class color_mode    : uint8_t { Default = 0, Color, Mono, Auto, Gray };
enum class orientation   : uint8_t { Default = 0, Portrait, Landscape, ReverseLandscape, ReversePortrait };
enum class print_quality : uint8_t { Default = 0, Draft, Normal, High };
enum class media_size    : uint8_t { Default = 0, A3, A4, A5, A6, Letter, Legal, Tabloid, Executive };
```

### `paper_type` / `job_sheet` / `test_page_type` / `supply_kind`

```cpp
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
```

### `alignment` / `shape_type` / `fit_mode`

```cpp
enum class alignment  : uint8_t { Left = 0, Center, Right };
enum class shape_type : uint8_t { Line = 0, Rect, FilledRect };
enum class fit_mode   : uint8_t { Fit = 0, Fill, Stretch };
```

**`fit_mode` meanings:**
- **`Fit`** — whole photo visible, aspect kept, centered in cell
- **`Fill`** — cell completely covered, aspect kept, overflow is **cropped**
- **`Stretch`** — cell completely covered, aspect **not** kept (distortion)

### `result<T>`

```cpp
template <typename T> struct result {
    T value{};
    status st = status::Ok;
    int native = 0;
    explicit operator bool() const noexcept;
    const char* error_string() const noexcept;
};
```

There's a specialized `result<void>` version.

---

## 🖨️ 2) `printer` Class

### 2.1) Opening

```cpp
static result<printer> open(const knst_c16string& name);
static result<printer> open_default();
static result<printer> open_any();
```

**`open`** — open by name. On Linux, name from `/etc/cups/printers.conf`; on Windows, `PRINTER_INFO_2W::pPrinterName`; on Android, `host:port`.

**`open_default`** — system default printer.

**`open_any`** — default if available, else first enabled+accepting, else first printer.

### 2.2) Status and info

```cpp
result<queue_status> status() const;
result<printer_info_ex> info() const;
result<knst_c16string> make_and_model() const;
result<printer_capabilities> capabilities() const;
result<knst_vector<supply_level>> supply_levels() const;
result<knst_vector<printer_alert>> alerts() const;
result<usage_stats> usage() const;
```

### `queue_status`

```cpp
struct queue_status {
    bool enabled = false;
    bool accepting = false;
    uint32_t job_count = 0;
    printer_state state = printer_state::Unknown;
    knst_c16string text;   // printer-state-reasons
};
```

### `printer_info_ex`

```cpp
struct printer_info_ex {
    knst_c16string name, description, location, model, make_and_model, device_uri;
    printer_state state = printer_state::Unknown;
    bool is_default, is_shared, is_enabled, is_accepting;
    uint32_t pending_jobs = 0;
};
```

### `printer_capabilities`

```cpp
struct printer_capabilities {
    bool color, duplex_long_edge, duplex_short_edge;
    bool stapling, punching, booklet;
    knst_vector<knst_c16string> supported_media;
    knst_vector<knst_c16string> supported_resolutions;
    knst_vector<knst_c16string> supported_sources;
    knst_vector<knst_c16string> supported_output_bins;
};
```

**Linux:** Extracted from `lpoptions -l` (PPD options).
**Windows:** `DeviceCapabilitiesW` (`DC_COLORDEVICE`, `DC_DUPLEX`, `DC_PAPERS`, `DC_ENUMRESOLUTIONS`).
**Android:** IPP `printer-resolution-supported`, `media-supported`, `sides-supported`, `print-color-mode-supported`, `finishings-supported`.

### `supply_level`

```cpp
struct supply_level {
    knst_c16string name;        // "Black Cartridge HP 952"
    knst_c16string raw_type;    // "toner-cartridge"
    supply_kind type = supply_kind::Unknown;
    uint32_t percent = 0;
    bool known = true;          // false: printer doesn't report a level
    bool low   = false;         // < marker-low-levels or 15%
};
```

**Linux:** `lpoptions` output for `marker-levels`, `marker-names`, `marker-types`, `marker-low-levels`. If absent, falls back to IPP `Get-Printer-Attributes`.

**Windows:** `NotSupported`.

**Android:** Via IPP `Get-Printer-Attributes`.

### 2.3) Printing — High level

```cpp
result<uint32_t> print_text(const knst_c16string& text, const print_options& o = {});
result<uint32_t> print_rich(const rich_document& rd, const print_options& o = {});
result<uint32_t> print_image(const knst_c16string& path, const print_options& o = {});
result<uint32_t> print_images(const knst_vector<knst_c16string>& paths,
                              const image_batch_options& bo = {},
                              const print_options& o = {});
result<uint32_t> print_images(const std::vector<knst_c16string>& paths,
                              const image_batch_options& bo,
                              const print_options& o);
result<uint32_t> print_batch(const knst_vector<knst_c16string>& paths,
                             const print_options& o = {},
                             const image_batch_options& bo = {});
result<uint32_t> print_file(const knst_c16string& path, const print_options& o = {});
result<uint32_t> print_pdf(const knst_c16string& path, const print_options& o = {});
result<uint32_t> print_auto(const knst_c16string& path, const print_options& o = {});
```

**`print_text`** — renders text to PDF (Courier 10pt, one page per line block). Submits PDF to CUPS/winspool/PrintManager.

**`print_rich`** — `rich_document` (styled text runs + shapes) → PDF → submit.

**`print_image`** — one photo per page (fit mode = `Fit`, 5mm margin).

**`print_images`** — N photos, `image_batch_options` grid layout (default 1×1, i.e. one photo per page). Sends all as **one job**.

**`print_batch`** — mixed list of photos + text + PDFs. Consecutive photos are combined, PDFs pass through, text becomes PDF. All in one job.

**`print_file`** — detects file type: text → `print_text`, image → `print_image`, PDF → `print_pdf`, PCL/ZPL/ESC-POS/PS → `send_raw`.

**`print_auto`** — same as `print_file`.

### 2.4) `print_options` — 30+ fields

```cpp
struct print_options {
    // Page
    uint32_t       copies = 1;
    duplex_mode    duplex = duplex_mode::Default;
    color_mode     color = color_mode::Default;
    orientation    orient = orientation::Default;
    print_quality  quality = print_quality::Default;
    media_size     paper = media_size::Default;
    paper_type     ptype = paper_type::Default;
    uint32_t       media_weight_gsm = 0;
    bool           borderless = false;
    bool           bleed = false;
    knst_c16string media;              // raw CUPS media name, overrides `paper`
    knst_c16string page_ranges;        // "1-3,5"
    knst_c16string title;
    uint32_t       priority = 50;
    bool           fit_to_page = false;
    bool           raw = false;         // CUPS -o raw
    bool           collate = false;
    uint32_t       scaling = 100;
    knst_c16string resolution;          // "600dpi"
    bool           toner_save = false;
    bool           auto_rotate = false;
    bool           auto_center = false;
    // Image
    int32_t        brightness = 0;      // -100..100
    int32_t        contrast = 0;        // -100..100 (ignored on Linux)
    int32_t        saturation = 0;
    bool           mirror_horizontal = false;
    bool           mirror_vertical = false;
    bool           invert_colors = false;   // unsupported by CUPS filters
    // Layout
    uint32_t       n_up = 1;            // 1..16
    bool           booklet = false;
    bool           reverse = false;
    uint32_t       custom_w_mm = 0;
    uint32_t       custom_h_mm = 0;
    int32_t        margin_top_mm, margin_bottom_mm, margin_left_mm, margin_right_mm;
    // Finishing
    knst_c16string media_source;
    knst_c16string output_bin;
    bool           staple = false;
    bool           punch = false;
    bool           fold = false;
    bool           face_down = false;
    uint32_t       job_offset_mm = 0;   // reserved
    // Job sheet
    job_sheet      job_sheet_start = job_sheet::None;
    job_sheet      job_sheet_end   = job_sheet::None;
    // Cache
    bool           cache_for_reprint = false;
    // Extra — `key=value` (validated by safe_value)
    knst_vector<knst_c16string> extra_options;
};
```

**Validation (`valid_options`):**
- `copies` 1..9999
- `priority` 1..100
- `n_up` 1..16
- `scaling` 10..400
- `media_weight_gsm` ≤ 500
- `brightness`, `saturation` -100..100
- `media`, `page_ranges`, `resolution`, `media_source`, `output_bin` — `safe_value` check
- `extra_options[i]` — must contain `=`, `safe_value` check

**`safe_value` character set:** `[A-Za-z0-9._:/,+*#-=@]` — spaces, quotes, `&`, `|`, `<`, `>` **forbidden**.

### 2.5) `rich_document` — Rich document

```cpp
struct text_style {
    knst_c16string font = u"Helvetica";   // Helvetica | Courier | Times (+ -Bold / -Oblique)
    float          size_pt = 12.0f;
    bool           bold = false;
    bool           italic = false;
    uint8_t        r = 0, g = 0, b = 0;
    float          line_spacing = 1.2f;
    alignment      align = alignment::Left;
};

struct text_run {
    knst_c16string text;
    text_style     style;
};

struct draw_command {
    shape_type type = shape_type::Line;
    float      x1_mm, y1_mm, x2_mm, y2_mm;   // origin top-left
    float      line_width = 0.5f;            // pt
    uint8_t    r, g, b;
    uint32_t   page = 1;                     // 1-based, 0 = every page
};

struct rich_document {
    float    page_w_mm = 210.0f;
    float    page_h_mm = 297.0f;
    float    margin_top_mm = 15.0f;
    float    margin_bottom_mm = 15.0f;
    float    margin_left_mm = 20.0f;
    float    margin_right_mm = 20.0f;

    knst_c16string header_text;
    knst_c16string footer_text;
    bool     show_page_number = false;
    bool     show_date = false;

    knst_vector<text_run>     elements;    // auto wrap + paginate
    knst_vector<draw_command> shapes;      // drawn below text

    // Custom font (Unicode):
    //   Empty (default)   → WinAnsi + 12 Type1 fonts (Latin-1 + Turkish)
    //   Non-empty (path)  → TTF/OTF file embedded as Type0/CIDFontType2/Identity-H
    knst_c16string embed_font_path;
    bool auto_fallback = false;
};
```

**Font support (`embed_font_path`):**

Can be given in three ways:
1. **Full file path:** `u"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"`
2. **System font name:** `u"NotoSans"`, `u"Arial"` — `knst_find_font()` searches the system (Windows: `C:\Windows\Fonts`, Linux: `/usr/share/fonts`...).
3. **Empty + `auto_fallback=true`:** PDF writer scans the text, finds a Unicode font from the system if non-WinAnsi codepoints exist (NotoSans → DejaVuSans → Arial → LiberationSans).

**Note:** TTF is subset — only used glyphs embedded. PDF size increases ~50-200 KB (not 300-800 KB).

**RTL / complex script:** Per-character mapping, **no RTL shaping**. Arabic and Hebrew may have letters in wrong order. CJK, Cyrillic, Greek work.

### 2.6) `image_batch_options` — Image batch

```cpp
struct image_batch_options {
    uint32_t rows = 1;                  // grid: rows × cols
    uint32_t cols = 1;
    fit_mode fit = fit_mode::Fit;
    float    margin_mm = 8.0f;          // page margin
    float    gap_mm    = 4.0f;          // gap between cells
    float    border_mm = 0.0f;          // >0 thin frame
    uint32_t copies_each = 1;           // each photo printed N times
    bool     rotate_to_fit = true;      // rotate 90° when it fits the cell better
    bool     apply_exif    = true;
    bool     show_filename = false;
    bool     skip_unreadable = true;    // unreadable files not fatal
    uint32_t max_dpi       = 300;       // downscale threshold
    knst_vector<knst_c16string>* skipped = nullptr;   // receives skipped paths
};
```

**Limits:** `rows` and `cols` 1..8. If cell size is less than 20 pt → `InvalidArgument`.

**What does `max_dpi` do?** If a photo's actual DPI exceeds this value, `downscale_rgb` reduces it. PDF size drops from 10 MB → 500 KB.

### 2.7) `secure_print_options` — PIN printing (Linux only)

```cpp
struct secure_print_options {
    knst_c16string job_password;    // [A-Za-z0-9._:/,+*#-=@], 1..32
    knst_c16string job_user;
};
```

**Linux:** `-o job-password=<pwd> -o job-password-encryption=none`.
**Windows/Android:** `NotSupported`.

### 2.8) `print_options` — example

```cpp
knst_prn::print_options opts;
opts.copies = 3;
opts.duplex = knst_prn::duplex_mode::LongEdge;
opts.color = knst_prn::color_mode::Color;
opts.paper = knst_prn::media_size::A4;
opts.quality = knst_prn::print_quality::High;
opts.n_up = 2;
opts.collate = true;
opts.staple = true;
opts.margin_top_mm = 10;
opts.margin_bottom_mm = 10;
opts.fit_to_page = true;
opts.title = u"My document";
opts.page_ranges = u"1-5,7,9";
opts.cache_for_reprint = true;
```

---

## 🔌 3) Direct IPP — bypass CUPS

`knst_prn::printer` static methods support IPP directly. If `knst_network.hpp` is present, TLS (`ipps://`) also works.

### `print_ipp`

```cpp
static result<uint32_t> print_ipp(
    const knst_c16string& pdf_path,
    const knst_c16string& host_port,     // "192.168.1.5:631", "ipps://printer.local"
    uint16_t port = 631,
    const print_options& opts = {},
    const knst_c16string& doc_format = u"application/pdf");
```

**What it does?** `Print-Job` IPP operation. PDF inline as body.

**When to use?** When the printer accepts PDF.

### `print_ipp_send_doc` — for HP 7740-class printers

```cpp
static result<uint32_t> print_ipp_send_doc(
    const knst_c16string& pdf_path,
    const knst_c16string& host_port,
    uint16_t port = 631,
    const print_options& opts = {},
    const knst_c16string& doc_format = u"application/pdf");
```

**What it does?** Two steps:
1. `Create-Job` (RFC 8011 §4.2.1)
2. `Send-Document` (RFC 8011 §4.3.1) — PDF body, `last-document=true`

**When to use?** Some HP printers (7740 etc.) reject PDF inline in `Print-Job` (`0x0400 bad-request`). Two-step flow fixes this.

### `print_ipp_smart` — smart selection

```cpp
result<uint32_t> print_ipp_smart(
    const knst_c16string& pdf_path,
    const knst_c16string& host_port,
    const print_options& o = {},
    const detail::ipp::send_policy& pol = {},
    std::string* info = nullptr);
```

**What it does? (5 phases)**

**1. Probe:**
- `Get-Printer-Attributes` → `document-format-supported`, `printer-state`, `printer-state-reasons`, `pwg-raster-document-resolution-supported`, `pwg-raster-document-type-supported`, `media-default`
- Tries `host:631` first, then `host:443` (TLS)

**2. Preflight:**
- `printer-state == 5` (stopped) → `BadState`
- `printer-is-accepting-jobs == false` → `BadState`
- `printer-state-reasons` containing `-error` → `BadState`
- `media-empty-report`, `marker-supply-low-warning` **not blocking**

**3. Format selection (priority order):**
- Printer supports `application/pdf` → inline PDF
- Else `image/pwg-raster` → PDF → PWG Raster encode → 1 job
- Else `image/jpeg` → PDF → JPEG per page → N jobs

**4. Submit:**
- `Print-Job`
- Busy (`0x0507`) → exponential backoff (500ms → 8s, total `busy_total_ms=180s`)
- Transport fail → look up by job-name (duplicate protection)

**5. Wait:**
- `Get-Job-Attributes` polling
- Progress-based stall: no change for 180s → `Timeout`
- Job canceled/aborted → `BadState`
- Job not-found → OK (aged out, treated as complete)

### `send_policy` — smart IPP tuning

```cpp
struct send_policy {
    uint32_t    io_idle_ms          = 60000;      // socket silence
    uint32_t    busy_total_ms       = 180000;     // busy retry total
    uint32_t    stall_ms            = 180000;     // no progress → Timeout
    uint32_t    total_ms            = 3600000;    // 1-hour absolute cap
    uint32_t    max_body_mb         = 160;        // PWG body cap
    bool        cancel_on_stall     = false;      // don't cancel stalled job
    bool        wait_for_completion = true;
    std::string prefer;                           // "", "pdf", "pwg", "jpeg"
};
```

**What does `prefer` do?**
- `"pdf"` → only try PDF
- `"pwg"` → only PWG
- `"jpeg"` → only JPEG per page
- `""` (default) → automatic selection

### `print_universal` — same name on every platform

```cpp
result<uint32_t> print_universal(
    const knst_c16string& path,
    const knst_c16string& host_port = {},     // empty → CUPS/winspool
    const print_options& o = {});
```

**Order:**
1. **Linux:** IPP (`print_ipp_smart`) → else CUPS `print_file`
2. **Windows:** IPP (`print_ipp_smart`) → else winspool `print_file`
3. **Android:** IPP (`print_ipp_smart`) → else PrintManager dialog

**Recommendation:** For PDF + network printer, **always use `print_universal`**. It hides platform-specific traps.

---

## 📡 4) Job Management

### Job list

```cpp
result<knst_vector<job_info>> jobs(bool include_completed = false) const;
result<job_info> job_details(uint32_t id) const;
```

**`job_info`:**

```cpp
struct job_info {
    uint32_t       id = 0;
    knst_c16string printer, user, title, state_text, message;
    job_state      state = job_state::Unknown;
    uint64_t       size_bytes = 0;
    uint32_t       pages = 0;
    uint64_t       submitted_unix = 0;
    uint32_t       priority = 50;
};
```

**Linux:** `lpstat -W not-completed -o` + `Get-Job-Attributes` (fast path) or ipptool fallback.
**Windows:** `EnumJobsW` (JOB_INFO_2).
**Android:** IPP `Get-Jobs`.

### Cancel

```cpp
result<void> cancel_job(uint32_t id);
result<void> cancel_all();
result<void> cancel_by_user(const knst_c16string& user);
```

**Linux:** IPP `Cancel-Job` → fallback `cancel` CLI on failure.
**Windows:** `SetJob(..., JOB_CONTROL_DELETE)`.
**Android:** IPP `Cancel-Job`.

**Note:** Already-finished jobs cannot be canceled → `BadState` (`0x0404`/`0x0406`).

### Hold / release

```cpp
result<void> hold_job(uint32_t id);
result<void> release_job(uint32_t id);
result<void> hold_all();
result<void> release_all();
```

**Linux:** IPP `Set-Job-Attributes` `job-state=4` (held) or `3` (pending). Some printers reject → `NotSupported`, then falls back to `lp -i <id> -H hold/resume`.
**Windows:** `SetJob(..., JOB_CONTROL_PAUSE/RESUME)`.
**Android:** IPP `Set-Job-Attributes`.

### Priority / wait

```cpp
result<void> set_job_priority(uint32_t id, uint32_t prio);   // 1..100
result<void> wait_for_job(uint32_t id, uint32_t timeout_ms = 60000);
result<bool> is_job_done(uint32_t id) const;
```

**`wait_for_job` behavior:**
- Job disappeared from list → **success** (aged out, likely complete)
- Job canceled/aborted → **`BadState`**
- Timeout → **`Timeout`**

### Move (Linux only)

```cpp
result<void> move_job(uint32_t id, const knst_c16string& dst);
result<void> move_all_jobs(const knst_c16string& dst);
```

`lpmove` CLI. Moves to another printer.

---

## 🔧 5) Admin

### Linux

```cpp
result<void> pause();           // cupsdisable
result<void> resume();          // cupsenable
result<void> reject_jobs();     // cupsreject
result<void> accept_jobs();     // cupsaccept
result<void> set_default();     // lpoptions -d
result<knst_vector<printer_option>> options() const;
result<void> set_default_option(const knst_c16string& k, const knst_c16string& v);
result<void> set_option_permanent(const knst_c16string& k, const knst_c16string& v);  // lpadmin
result<void> remove_option(const knst_c16string& k);
result<void> set_description(const knst_c16string& d);
result<void> set_location(const knst_c16string& l);
result<void> set_sharing(bool on);
```

**`printer_option`:**

```cpp
struct printer_option {
    knst_c16string name;                // "PageSize"
    knst_c16string text;                // "Media Size"
    knst_c16string current;             // "A4"
    knst_vector<knst_c16string> choices;
};
```

### Windows

`pause`/`resume` → `SetPrinter(PRINTER_CONTROL_PAUSE/RESUME)`, most others are `NotSupported`.

### Add/remove printer (Linux)

```cpp
static result<void> add_printer(name, uri, driver_id, desc = {}, loc = {});
static result<void> add_ipp_printer(name, uri);           // "everywhere" driver
static result<void> remove_printer(name);
```

**URI scheme check:** `ipp://`, `ipps://`, `socket://`, `lpd://`, `http://`, `https://`, `usb://`, `hp:/`, `dnssd://`.

### Printer classes (Linux)

```cpp
static result<knst_vector<printer_class>> list_classes();
static result<void> create_class(cn, printer);
static result<void> delete_class(cn);
static result<void> add_to_class(cn, printer);
static result<void> remove_from_class(cn, printer);
```

---

## 🎨 6) Test Page

```cpp
result<uint32_t> print_test_page(test_page_type type = test_page_type::Diagnostic,
                                 const print_options& o = {});
```

Six types:

| Type | Contents |
|---|---|
| **Diagnostic** | All font samples, Turkish characters, symbols, color bar, grayscale ramp |
| **ColorBar** | 7 color blocks (C, M, Y, K, R, G, B) + gray ramp |
| **Alignment** | Corner marks, center cross, ruler (10mm, 50mm bold) |
| **Grid** | 10mm grid squares, bold at 50mm |
| **Gradient** | 7 gradients (white → color), each on its own line |
| **NozzleCheck** | 4-color vertical + horizontal lines + solid blocks (nozzle clog detection) |

Usage:

```cpp
pr.value.print_test_page(knst_prn::test_page_type::NozzleCheck, opts);
```

---

## 🖼️ 7) PDF Writer (internal)

The library has its own PDF 1.4 writer. `detail::write_pdf(doc, path)`:

- **Page size:** `document::w_pt`, `h_pt` (pt = 1/72")
- **Fonts:** 12 built-in Type1 (Helvetica, Courier, Times + Bold/Oblique/BoldOblique). Turkish codepoints (Ğ, ğ, İ, ı, Ş, ş) remapped to WinAnsi slots.
- **Custom TTF:** With `embed_font_path` + `auto_fallback`, Unicode font subset embedded.
- **DEFLATE:** Own fixed Huffman + LZ77 encoder.
- **Image:** JPEG passthrough (when quality is adequate) or decode → downscale → DEFLATE RGB.

**PNG writer** (`write_png_simple`): RGB → Sub filter → fixed Huffman DEFLATE → PNG. `decode_to_png(src, dst)` converts any image to PNG.

**Page count** (`count_pdf_pages_scan`): Scans for `/Type /Page` strings (fails on compressed object streams, which is why `pdf_page_count` tries `pdfinfo`/`qpdf` first).

**Preview** (`render_preview`):
- Linux: `pdftoppm -png -singlefile -f N -l N -r DPI` → PNG bytes
- Windows: `NotSupported`
- Android: JNI `KnstPdfRasterizer.rasterize()` → JPEG → `decode_to_png` → PNG

---

## 🖨️ 8) Windows-Specific

### GDI render

`print_text`, `print_rich`, `print_image`, `print_images`, `print_test_page` use **GDI directly** (`CreateDCW` + `StartDocW` + `TextOutW`/`Rectangle`/`StretchDIBits`).

**Benefit:** No PDF reader dependency — text, image, rich doc go native through spooler.

**DEVMODE** controls `copies`, `duplex`, `color`, `orientation`, `paper size`, `quality`, `media type`.

### PDF printing — critical trap

`print_pdf` → `ShellExecuteW(L"printto", file, printer)`. **A "printto" handler must be registered for PDF** (Acrobat, SumatraPDF, Foxit, Edge...).

Otherwise: `status::NotSupported` + `ShellExecuteW` return code (`SE_ERR_NOASSOC = 31`).

**Bypass:** `print_universal(path, "printer:631")` — direct IPP sends PDF without reader.

### RAW datatype

`send_raw(data)` → `StartDocPrinterW(RAW)` + `WritePrinter`. Chunk size 64 KB. If `WritePrinter` fails midway, `AbortPrinter` — half job not written to spool.

### `send_raw` byte sniff

`print_data(d, len)` checks magic bytes:
- `%PDF-` → PDF
- `%!` → PostScript
- `\xFF\xD8\xFF` → JPEG
- `\x89PNG` → PNG
- `GIF8` → GIF
- `BM` → BMP
- `P6`/`P5` → PNM
- `II*\0` / `MM\0*` → TIFF
- `RIFF...WEBP` → WEBP

**Match** → write to temp file, `print_file` (spooler driver chain).
**No match** → `send_raw` (RAW datatype).

### Temp cleanup

On Windows, if `shell_print` doesn't see a job within 15 seconds, it returns `R::success(0)` (sent, id unknown). Temp PDF **not deleted** — the reader might still be reading it. Subsequent `print_data` calls sweep `cleanup_stale_temp_files()` (1+ hour-old `knst_print*`).

---

## 🤖 9) Android-Specific

### PrintManager dialog

`print_text`, `print_rich`, `print_image`, `print_images`, `print_batch`, `print_test_page` → create PDF → **JNI `PrintManager.print()`** dialog opens.

**PrintAttributes** passed to `PrintManager`:
- `MediaSize` (A3..Executive)
- `ColorMode` (2=Color, 1=Mono)
- `DuplexMode` (0=None, 1=LongEdge, 2=ShortEdge)
- `Resolution` (custom from `opts.resolution`)

**`document_id`** stored in cache if `opts.cache_for_reprint` set.

**Note:** PrintManager accepts **one job**. `print_files(N)` = N dialogs.

### USB OTG raw (bulk)

`print_usb(data)`:
1. `UsbManager.getDeviceList` → `openDevice` for each device
2. Scan interfaces: **class 7 (Printer)** or **class 0xFF (Vendor)**
3. On that interface, find a **direction=OUT, type=BULK** endpoint
4. `claimInterface` → `bulkTransfer` in 8 KB chunks
5. `releaseInterface` + `close`

**Use:** thermal (ESC/POS), label (ZPL), PCL printers.

### Bluetooth SPP raw

`print_bluetooth(data, "AA:BB:CC:DD:EE:FF")`:
1. `BluetoothAdapter.getRemoteDevice`
2. `createRfcommSocketToServiceRecord(UUID 00001101-0000-1000-8000-00805F9B34FB)`
3. `connect` + `getOutputStream` → write

**Use:** mobile thermal printers (Zebra, Epson TM).

### mDNS printer discovery

`printer::list_all` → JNI `KnstPrintDiscoveryHelper.discover(4000)` → Java `NsdManager.discoverServices("_ipp._tcp")` + `_ipps._tcp`. Each printer returned as `name|host|port|uri`.

`scanner::list_all` → `discoverScanners(4000)` → `_uscan._tcp` → `name|host|port` format.

**mDNS retry:** Some printers (like HP 7740) send PTR immediately, SRV/A 300-500 ms later in a separate packet. Code waits 500 ms on empty result and **retries**, then caches for 5 minutes.

### PDF rasterize (JNI)

`KnstPdfRasterizer.rasterize(path, outDir, dpi, quality)` → Java `PdfRenderer` converts each page to JPEG, returns `[Ljava/lang/String;`.

**Use:** When `print_batch` needs to treat PDF as image, `render_preview` always.

### Scanner — eSCL only

Android has **no SANE, no WIA**. Scanner uses eSCL (AirScan) protocol:
1. `GET /eSCL/ScannerCapabilities` → parse XML
2. `POST /eSCL/ScanJobs` (`ScanSettings` XML) → 201 + Location
3. `GET <Location>/NextDocument` → JPEG page
4. In ADF mode, repeat until 404/410

**Format:** `image/jpeg` (native eSCL). If PDF or PNG requested, converted internally.

---

## 🐧 10) Linux-Specific

### CUPS CLI

`printer::print_*` → calls `lp -d <printer> -o <opts>`. `build_args` converts every `print_options` field into CUPS `-o key=value` format.

**`ppd()` helper:** Checks if a PPD option exists via `lpoptions -l`. If so, also passes the PPD name (e.g. `Duplex=DuplexNoTumble` alongside `sides=two-sided-long-edge`).

### Scanner SANE

`scanimage -d <device> --format=<fmt> --mode=<mode> --resolution=<dpi> ...`

**Capabilities cache:** `scanimage -A` can take 2-5 seconds (especially over eSCL). `scanner::capabilities()` result is **process-lifetime** cached (1-hour TTL).

**Retry:**
- **Flatbed:** jpeg → png → bmp (page doesn't move, safe)
- **ADF:** **only jpeg** (each Download advances the feeder, retry loses sheets)

### airscan vs escl ordering

`list_all` returns **airscan** backend first. Why? HP's `escl` backend ignores `--source=Flatbed`, returns HTTP 409 when ADF is empty. `airscan` (sane-airscan) handles it correctly.

### HP 7740 geometry retry

`scan_pages` first attempt sends `-x <w> -y <h>`. HP firmware sometimes rejects → **retry without geometry**.

### `scanimage` exit code

`0` = success. `!= 0` + no pages = real error. **`code == 0` + no pages** = ADF empty or device unreachable — **retry pointless**, straight `NotFound`.

---

## 🎥 11) `print_options` matrix — what works where?

| Field | Linux (CUPS) | Windows (GDI) | Android (PrintManager) |
|---|---|---|---|
| `copies` | ✅ `-n` | ✅ `DM_COPIES` | ⚠️ in dialog |
| `duplex` | ✅ `sides` | ✅ `DM_DUPLEX` | ✅ `setDuplexMode` |
| `color` | ✅ `print-color-mode` | ✅ `DM_COLOR` | ✅ `setColorMode` |
| `orient` | ✅ `orientation-requested` | ✅ `DM_ORIENTATION` | ❌ (in PDF) |
| `quality` | ✅ `print-quality` | ✅ `DM_PRINTQUALITY` | ❌ |
| `paper` | ✅ `media` | ✅ `DM_PAPERSIZE` | ✅ `setMediaSize` |
| `custom_w_mm`+`custom_h_mm` | ✅ `Custom.WxH` | ✅ `DMPAPER_USER` | ❌ |
| `ptype` | ✅ `media-type` | ✅ `DM_MEDIATYPE` | ❌ |
| `borderless` | ✅ `print-scaling=fill` | ⚠️ driver | ❌ |
| `resolution` | ✅ `printer-resolution` | ⚠️ `DM_PRINTQUALITY` | ✅ `setResolution` |
| `n_up` | ✅ `number-up` | ❌ (driver) | ❌ |
| `booklet` | ✅ `booklet` | ❌ | ❌ |
| `margin_*_mm` | ✅ `page-top/bottom/left/right` (pt) | ❌ | ❌ |
| `staple` / `punch` / `fold` | ✅ `finishings=4,5,10` | ⚠️ driver | ❌ |
| `media_source` | ✅ `media-source` | ⚠️ `DM_DEFAULTSOURCE` | ❌ |
| `output_bin` | ✅ `output-bin` | ⚠️ `DM_DEFAULTSOURCE` | ❌ |
| `page_ranges` | ✅ `-P` | ❌ | ❌ |
| `collate` | ✅ `collate` | ✅ `DM_COLLATE` | ❌ |
| `fit_to_page` | ✅ `fit-to-page` | ❌ | ❌ |
| `scaling` | ✅ `scaling` | ❌ | ❌ |
| `raw` | ✅ `-o raw` | ⚠️ (RAW per page) | ❌ |
| `toner_save` | ✅ `print-quality=3` | ❌ | ❌ |
| `job_sheet_*` | ✅ `job-sheets` | ❌ | ❌ |

`⚠️` = depends on printer driver.

---

## ⚠️ 12) Common Gotchas

### 1) No PDF reader on Windows

`print_pdf` → `NotSupported`. Fix: `print_universal(path, "host:631")` or `print_ipp(path, "host:631")`.

### 2) `print_network` with PDF

**Don't do it.** Port 9100 accepts raw ESC/POS/PCL/ZPL, not PDF. Raw PDF bytes print blank pages.

### 3) HP 7740 rejects geometry

Code automatically retries without geometry. But **ADF retry is dangerous** — the feeder may have consumed sheets. ADF always tries only one format (JPEG).

### 4) HP 7740 rejects Print-Job

Code `print_ipp_smart` tries `print_ipp` first. On failure, **manually call `print_ipp_send_doc`**.

### 5) `escl` vs `airscan` (Linux scanner)

`escl` backend ignores `--source=Flatbed` on HP. `list_all` sorts airscan first. **Don't force `escl`.**

### 6) Android PrintManager single job

N files = N dialogs. For one-dialog batch use `print_batch` (photos/text as one PDF).

### 7) `print_secure` Linux only

Windows/Android → `NotSupported`. No Windows workaround (no spooler PIN API).

### 8) TTF embed inflates PDF

Full file embed (no subset) → ~300-800 KB. Code **does subset** — only used glyphs, ~30-80 KB.

### 9) No RTL shaping

Arabic/Hebrew per-character map works but **letters don't join, no RTL ordering**. CJK/Cyrillic/Greek work fully.

### 10) `pdf_page_count` fails on compressed object streams

Scan fallback looks for `/Type /Page` strings — pages hidden inside object streams won't be found. Linux tries `pdfinfo`/`qpdf` first; if neither is present → `ToolMissing`.

### 11) `wait_for_job` "not found" = success

Job disappearance from list → **treated as complete**. On Windows, finished jobs leave EnumJobs. On Linux, they move to "completed" filter — but `cancel_job` also removes them from list, so ensure you didn't `cancel` before `wait_for_job`.

### 12) `safe_value` charset is narrow

In `-o key=value`, the value after `=` must contain **only** `[A-Za-z0-9._:/,+*#-=@]`. User input `"my document"` → **rejected**. If you need spaces, CUPS-specific `%20` escape — **code doesn't do that**, returns `InvalidArgument`.

### 13) Printer name max 32 chars

`is_safe_token(name, 32)` — longer printer names → `InvalidArgument`. Same on Linux/Windows/Android.

### 14) Windows `pdf_page_count` on compressed PDF → `NotSupported`

Scan-based; inside object streams → `ToolMissing`/`NotSupported`.

### 15) `render_preview` Linux + Android only

Windows → `NotSupported`. Alternative: `pdf_page_count` + external tool.

### 16) Scanner capabilities cache 1 hour

`scanner::capabilities()` result cached. If scanner changes, program restart required.

### 17) ADF `page_count=0` → 500-page cap

`scan_pages` with `o.page_count == 0` tries up to 500 pages. ADF holds 50-100 sheets, safe cap.

### 18) `print_images` empty `paths` → `InvalidArgument`

For single photo use `print_image`.

### 19) Windows `print_data` PDF temp cleanup delayed

`print_data(pdf)` → `shell_print` → if timeout after 15s, temp file **not deleted** (reader may still read). Subsequent calls run `cleanup_stale_temp_files()` (>1h).

### 20) `print_batch` on Android → 1 dialog for N photos

But **PDF content** requires `rasterize_pdf_to_jpegs` (JNI) — if `KnstPdfRasterizer` missing → `DecodeFailed`.

---

## 📊 13) Performance

| Operation | Cost | Note |
|---|---|---|
| `open` | O(1) | CUPS/winspool/IPP probe |
| `status` / `info` | ~50-200 ms | CLI spawn (Linux) / WMI (Windows) |
| `capabilities` | ~100-500 ms | `lpoptions -l` / `DeviceCapabilities` / IPP |
| `supply_levels` | ~100-300 ms | IPP Get-Printer-Attributes |
| `print_text` | O(text) + PDF render | CUPS submit |
| `print_images` | O(N × decode + resize) | 10 photos ≈ 1-3 s |
| `print_ipp` | ~1-2 s + transfer | Network |
| `print_ipp_smart` | ~2-5 s (probe + submit) | + PWG encode |
| `print_universal` | ~1-10 s | Depends on path selection |
| `wait_for_job` | O(job duration) | 500 ms poll |
| `scanner::list_all` | ~2-5 s | SANE/mDNS |
| `scanner::capabilities` | ~2-5 s (cached 0) | First call slow |
| `scan_page` | O(dpi² × area) + I/O | 300 dpi A4 ≈ 10-30 s |
| `scan_to_pdf` | + PDF render | +1-3 s |

**Tips:**
- **Cache `printer::capabilities()` result** — don't call per print
- Prefer `print_universal` over `print_file` (IPP fast path)
- Lower `image_batch_options::max_dpi` → smaller PDF
- `scanner::open` + `capabilities` once, then loop scan

---

## ✅ 14) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Universal print (hides platform traps)
knst_prn::printer::print_universal(path, u"192.168.1.50:631", opts);

// 2. For Windows PDF
pr.print_universal(pdf, u"printer.local:631", opts);   // IPP bypass

// 3. For HP 7740 explicit Send-Document
knst_prn::printer::print_ipp_send_doc(pdf, u"printer:631", 631, opts);

// 4. N photos → one job
knst_prn::image_batch_options bo;
bo.rows = 2; bo.cols = 2;
bo.max_dpi = 200;              // small PDF
pr.print_images(paths, bo, opts);

// 5. Scanner: capabilities once
auto sc = knst_prn::scanner::open(device);
auto caps = sc.value.capabilities();   // cached
for (int i = 0; i < 10; ++i) sc.value.scan_page(sopts);

// 6. Android print: single dialog
pr.print_batch({a, b, c}, opts);   // N photos + text → 1 dialog

// 7. Magic byte sniff for raw
pr.print_data(data, len, opts);    // PDF/PNG → spooler, else → RAW
```

### ❌ Don't

```cpp
// 1. PDF raw to port 9100
pr.print_network(pdf_bytes, u"192.168.1.5", 9100);   // ❌ blank pages

// 2. Windows PDF via print_pdf
pr.print_pdf(pdf, opts);   // ❌ NotSupported without reader
// ✅ use print_universal

// 3. capabilities per print
for (int i = 0; i < 100; ++i) {
    pr.value.capabilities();   // ❌ CLI spawn × 100
}

// 4. Android N photos → N dialogs
for (auto& p : paths) pr.value.print_image(p);   // ❌ N dialogs
// ✅ print_batch

// 5. Retry on ADF
// ❌ feeder sheet loss — code avoids it but don't try manually

// 6. Force escl backend (Linux HP)
// ❌ use airscan first in the list

// 7. Space in `-o key=value`
opts.extra_options.push_back(u"my option=value");   // ❌ safe_value rejects

// 8. `print_secure` on Windows/Android
pr.print_secure(pdf, {u"1234", u"user"}, opts);   // ❌ NotSupported
```

---

## 🔥 15) Real-World Examples

### Example 1: Report print (rich document)

```cpp
knst_prn::rich_document doc;
doc.page_w_mm = 210; doc.page_h_mm = 297;
doc.show_page_number = true;
doc.show_date = true;
doc.header_text = u"Company Report";

knst_prn::text_run title;
title.text = u"2025 Q4 Report";
title.style.font = u"Helvetica-Bold";
title.style.size_pt = 20;
title.style.align = knst_prn::alignment::Center;
doc.elements.push_back(title);

knst_prn::text_run body;
body.text = u"This report covers Q4 2025." /* long text */;
body.style.size_pt = 11;
body.style.line_spacing = 1.4f;
doc.elements.push_back(body);

knst_prn::draw_command line;
line.type = knst_prn::shape_type::Line;
line.x1_mm = 20; line.y1_mm = 40; line.x2_mm = 190; line.y2_mm = 40;
line.line_width = 1.0f;
doc.shapes.push_back(line);

// Turkish Unicode
doc.embed_font_path = u"NotoSans";

auto pr = knst_prn::printer::open_any();
knst_prn::print_options opts;
opts.duplex = knst_prn::duplex_mode::LongEdge;
opts.copies = 1;

pr.value.print_rich(doc, opts);
```

### Example 2: Photo album (grid)

```cpp
knst_prn::image_batch_options bo;
bo.rows = 2;
bo.cols = 3;
bo.fit = knst_prn::fit_mode::Fill;
bo.margin_mm = 5;
bo.gap_mm = 3;
bo.border_mm = 0.3f;
bo.show_filename = true;
bo.apply_exif = true;
bo.max_dpi = 200;

knst_vector<knst_c16string> photos = /* ... */;

auto pr = knst_prn::printer::open_any();
knst_prn::print_options opts;
opts.paper = knst_prn::media_size::A4;
opts.color = knst_prn::color_mode::Color;
opts.quality = knst_prn::print_quality::High;

pr.value.print_images(photos, bo, opts);
```

### Example 3: HP 7740 scanner ADF duplex

```cpp
auto scanners = knst_prn::scanner::list_all();
// list_all sorts airscan first
auto sc = knst_prn::scanner::open(scanners.value[0].device_id);

knst_prn::scan_options so;
so.dpi = 300;
so.mode = knst_prn::color_mode::Color;
so.paper = knst_prn::media_size::A4;
so.use_adf = true;
so.duplex = true;
so.page_count = 0;   // until feeder empty
so.output_format = u"pdf";
so.timeout_s = 300;

sc.value.scan_to_pdf(u"/tmp/scan.pdf", so);
```

### Example 4: Thermal printer (Android USB OTG)

```cpp
knst_prn::printer pr;
knst_byte_string escpos = /* ESC/POS bytes */;

auto r = pr.print_usb(escpos);
if (r) std::cout << "USB print OK\n";

// Bluetooth SPP
knst_byte_string escpos2 = /* ... */;
pr.print_bluetooth(escpos2, u"DC:0D:30:AB:CD:EF");
```

### Example 5: Batch job monitoring

```cpp
auto pr = knst_prn::printer::open_any();

knst_prn::print_options opts;
opts.cache_for_reprint = true;

auto job = pr.value.print_pdf(u"big_report.pdf", opts);
if (job) {
    std::cout << "Job ID: " << job.value << "\n";
    
    if (pr.value.wait_for_job(job.value, 600000)) {
        std::cout << "Completed\n";
    } else {
        std::cerr << "Timeout or canceled\n";
        auto d = pr.value.job_details(job.value);
        if (d) std::cout << "State: " << (int)d.value.state << "\n";
    }
}
```

### Example 6: Windows PDF → IPP bypass

```cpp
// No reader on Windows; use direct IPP
knst_prn::print_options opts;
opts.duplex = knst_prn::duplex_mode::LongEdge;
opts.copies = 2;

auto r = knst_prn::printer::print_universal(
    u"C:\\path\\to\\report.pdf",
    u"office-printer.local:631",    // printer host:port
    opts);

if (r) std::cout << "Job: " << r.value << "\n";
else   std::cerr << knst_prn::status_string(r.st) << "\n";
```

### Example 7: Job queue monitoring

```cpp
auto jobs = pr.value.jobs(false);
if (jobs) {
    for (auto& j : jobs.value) {
        std::cout << "Job " << j.id
                  << "  " << j.user
                  << "  " << j.title
                  << "  state=" << (int)j.state
                  << "  pages=" << j.pages
                  << "\n";
    }
}

// Cancel by user
pr.value.cancel_by_user(u"guest");
```

### Example 8: Scanner → print (copy)

```cpp
auto sc = knst_prn::scanner::open(scanner_id);
auto pr = knst_prn::printer::open_any();

knst_prn::scan_options so;
so.dpi = 300;
so.mode = knst_prn::color_mode::Color;
so.use_adf = true;

knst_prn::print_options po;
po.paper = knst_prn::media_size::A4;
po.color = knst_prn::color_mode::Color;

sc.value.scan_and_print_to(pr.value, po, so);
```

---

## 📌 16) API Contract Summary

```cpp
namespace knst_prn {

enum class status, printer_state, job_state, duplex_mode, color_mode;
enum class orientation, print_quality, media_size, paper_type;
enum class job_sheet, test_page_type, supply_kind, alignment, shape_type, fit_mode;

template<typename T> struct result;
template<> struct result<void>;

struct printer_info_ex, queue_status, printer_capabilities;
struct supply_level, printer_alert, usage_stats;
struct device_info, driver_info, server_info, job_info;
struct printer_option, printer_class, cache_config;
struct print_options, secure_print_options;
struct image_batch_options;
struct text_style, text_run, draw_command, rich_document;
struct scanner_info, scan_options, scanner_capabilities;

class printer {
public:
    // Open
    static result<printer> open(const knst_c16string&);
    static result<printer> open_default();
    static result<printer> open_any();
    const knst_c16string& name() const noexcept;

    // Info
    result<queue_status> status() const;
    result<printer_info_ex> info() const;
    result<knst_c16string> make_and_model() const;
    result<printer_capabilities> capabilities() const;
    result<knst_vector<supply_level>> supply_levels() const;
    result<knst_vector<printer_alert>> alerts() const;
    result<usage_stats> usage() const;

    // Print
    result<uint32_t> print_text(const knst_c16string&, const print_options& = {});
    result<uint32_t> print_rich(const rich_document&, const print_options& = {});
    result<uint32_t> print_image(const knst_c16string&, const print_options& = {});
    result<uint32_t> print_images(const knst_vector<knst_c16string>&,
                                  const image_batch_options& = {},
                                  const print_options& = {});
    result<uint32_t> print_batch(const knst_vector<knst_c16string>&,
                                 const print_options& = {},
                                 const image_batch_options& = {});
    result<uint32_t> print_file(const knst_c16string&, const print_options& = {});
    result<uint32_t> print_pdf(const knst_c16string&, const print_options& = {});
    result<uint32_t> print_auto(const knst_c16string&, const print_options& = {});
    result<uint32_t> print_test_page(test_page_type, const print_options& = {});
    result<uint32_t> print_secure(const knst_c16string&, const secure_print_options&, const print_options& = {});
    result<uint32_t> print_held(const knst_c16string&, const knst_c16string&, const print_options& = {});
    result<knst_vector<uint32_t>> print_files(const knst_vector<knst_c16string>&, const print_options& = {}, bool = false);
    static result<knst_vector<uint32_t>> print_to_multiple(const knst_vector<knst_c16string>&, const knst_c16string&, const print_options& = {});

    // Raw
    result<uint32_t> send_raw(const knst_byte_string&, const knst_c16string& = {});
    result<uint32_t> send_escpos(const knst_byte_string&);
    result<uint32_t> send_zpl(const knst_byte_string&);
    result<uint32_t> send_pcl(const knst_byte_string&);
    result<uint32_t> print_network(const knst_byte_string&, const knst_c16string&, uint16_t = 9100, uint32_t = 5000);
    result<uint32_t> print_usb(const knst_byte_string&);              // Android only
    result<uint32_t> print_bluetooth(const knst_byte_string&, const knst_c16string&);  // Android only

    // IPP
    static result<uint32_t> print_ipp(const knst_c16string&, const knst_c16string&, uint16_t = 631, const print_options& = {}, const knst_c16string& = u"application/pdf");
    static result<uint32_t> print_ipp_send_doc(...);
    static result<uint32_t> print_jpeg_ipp(...);
    result<uint32_t> print_universal(const knst_c16string&, const knst_c16string& = {}, const print_options& = {});

    // Jobs
    result<knst_vector<job_info>> jobs(bool = false) const;
    result<job_info> job_details(uint32_t) const;
    result<knst_vector<job_info>> jobs_by_user(const knst_c16string&, bool = false) const;
    result<knst_vector<job_info>> completed_jobs(uint32_t = 20) const;
    result<void> cancel_job(uint32_t);
    result<void> cancel_all();
    result<void> cancel_by_user(const knst_c16string&);
    result<void> hold_job(uint32_t);
    result<void> release_job(uint32_t);
    result<void> hold_all();
    result<void> release_all();
    result<void> set_job_priority(uint32_t, uint32_t);
    result<void> move_job(uint32_t, const knst_c16string&);
    result<void> move_all_jobs(const knst_c16string&);
    result<void> wait_for_job(uint32_t, uint32_t = 60000);
    result<void> wait_for_ready(uint32_t = 30000);
    result<bool> is_job_done(uint32_t) const;
    result<uint32_t> reprint_job(uint32_t);

    // Admin
    result<void> pause(), resume(), reject_jobs(), accept_jobs(), set_default();
    result<knst_vector<printer_option>> options() const;
    result<void> set_default_option(const knst_c16string&, const knst_c16string&);
    result<void> set_option_permanent(...);
    result<void> remove_option(const knst_c16string&);
    result<void> set_description(const knst_c16string&);
    result<void> set_location(const knst_c16string&);
    result<void> set_sharing(bool);

    // Static helpers
    static result<knst_c16string> get_default_name();
    static result<knst_vector<printer_info_ex>> list_all();
    static result<knst_vector<device_info>> list_available_devices();
    static result<knst_vector<driver_info>> list_drivers();
    static result<server_info> server_status();
    static result<bool> exists(const knst_c16string&);
    static result<uint32_t> pdf_page_count(const knst_c16string&);
    static result<knst_byte_string> render_preview(const knst_c16string&, uint32_t = 1, uint32_t = 100);
    static result<void> add_printer(const knst_c16string&, const knst_c16string&, const knst_c16string&, ...);
    static result<void> add_ipp_printer(const knst_c16string&, const knst_c16string&);
    static result<void> remove_printer(const knst_c16string&);
    static result<knst_vector<printer_class>> list_classes();
    static result<void> create_class(...), delete_class(...);
    static result<void> add_to_class(...), remove_from_class(...);
    static result<void> set_cache(bool, const knst_c16string& = {}, uint32_t = 500);
    static result<void> clear_cache();
    static uint64_t cache_size_bytes();

    // IPP job control (Android/Windows native)
    static result<void> hold_job_ipp(const knst_c16string&, uint32_t);
    static result<void> release_job_ipp(const knst_c16string&, uint32_t);
    static result<void> set_job_priority_ipp(const knst_c16string&, uint32_t, uint32_t);
};

class scanner {
public:
    static result<knst_vector<scanner_info>> list_all();
    static result<scanner> open(const knst_c16string& device_id);
    const knst_c16string& name() const noexcept;

    result<bool> scanner_status() const;
    result<scanner_capabilities> capabilities() const;

    result<knst_byte_string> scan_page(const scan_options& = {});
    result<knst_c16string> scan_to_file(const knst_c16string&, const scan_options& = {});
    result<knst_vector<knst_c16string>> scan_to_files(const knst_c16string&, const scan_options& = {});
    result<knst_c16string> scan_to_pdf(const knst_c16string&, const scan_options& = {});
    result<uint32_t> scan_and_print(const print_options& = {}, const scan_options& = {});
    result<uint32_t> scan_and_print_to(printer&, const print_options& = {}, const scan_options& = {});
};

#if KNST_PRN_HAS_NETWORK
result<uint32_t> print_ipp_smart(
    const knst_c16string& pdf_path,
    const knst_c16string& host_port,
    const print_options& o = {},
    const detail::ipp::send_policy& pol = {},
    std::string* info = nullptr);
#endif

} // namespace knst_prn
```

---

## 🎯 What It Brings You

`knst_prn` gives you serious advantages in these situations:

- ✅ **Cross-platform printing** — same code on Linux/Windows/Android
- ✅ **Own PDF writer** — no external PDF library
- ✅ **TTF subsetting** — Unicode support, small PDF size
- ✅ **IPP client (ipp + ipps)** — TLS, RFC 8010/8011
- ✅ **PWG Raster** — optimal format for modern printers
- ✅ **Smart IPP** — probe → format select → submit → wait
- ✅ **HP 7740 fixes** — airscan first, Send-Document, geometry retry
- ✅ **30+ print options** — duplex, color, n-up, booklet, finishings
- ✅ **Rich document** — text runs + shapes + header/footer
- ✅ **Image batch** — N photos → 1 job, grid layout
- ✅ **Test page** — 6 types, diagnostic/colorbar/alignment/grid/gradient/nozzle
- ✅ **Job management** — cancel, hold, release, priority, move, wait
- ✅ **Supply / alerts** — marker-levels, low warning
- ✅ **Scanner (SANE / WIA / eSCL)** — all in one API
- ✅ **USB OTG + Bluetooth SPP** — Android thermal/label printer
- ✅ **Reprint cache** — job → temp file mapping, reprint
- ✅ **KernelNucleusT compatibility** — fully integrated with `knst_file`, `knst_process`, `knst_network`, `knst_c16string`, `knst_image_loader`

---