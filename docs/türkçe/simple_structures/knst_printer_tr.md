# knst_prn — Yazıcı ve Tarayıcı Kontrol Kütüphanesi

Selamlar! 👋 `knst_prn` namespace'i, yazıcı ve tarayıcı işlemlerini **tek bir API altında** toplayan cross-platform bir framework. Programatik yazdırma, PDF oluşturma, IPP/IPPS, scanner scan, job kontrolü, test page, görsel batch, supply seviyeleri, modern printer'lar için özel düzeltmeler — hepsi burada.

**Kısaca:** Linux'ta CUPS + kendi IPP client'ı, Windows'ta winspool + GDI + opsiyonel IPP, Android'de PrintManager dialog + USB OTG + Bluetooth SPP + eSCL. **FFmpeg yok, external PDF kütüphanesi yok** — PDF yazıcısı ve PWG Raster encoder'ı bu kütüphanenin içinde.

---

## 🎯 Honesty Matrix — Platform Başına Ne Var?

| Özellik | Linux (CUPS) | Windows | Android |
|---|---|---|---|
| **`print_text` / `print_rich`** | ✅ CUPS → `lp` | ✅ GDI → spooler | ✅ PrintManager dialog |
| **`print_image` / `print_images`** | ✅ CUPS | ✅ GDI | ✅ PrintManager dialog |
| **`print_file` / `print_auto`** | ✅ CUPS | ⚠️ PDF needs reader | ✅ PrintManager |
| **`print_pdf`** | ✅ CUPS | ⚠️ PDF reader gerekir | ✅ PrintManager |
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
| **Job management** | ✅ tam | ✅ spooler | ✅ IPP |
| **Admin (pause/resume/options)** | ✅ CUPS | ⚠️ sınırlı | ❌ |
| **Cache / reprint** | ✅ | ✅ | ❌ |
| **PDF preview (`render_preview`)** | ✅ pdftoppm | ❌ | ⚠️ JNI helper |
| **Scanner `list_all`** | ✅ SANE | ⚠️ WIA (MSVC) / eSCL (MinGW) | ✅ mDNS |
| **Scanner `scan_page` / `scan_to_*`** | ✅ SANE | ✅ WIA / eSCL | ✅ eSCL |
| **Scanner ADF / duplex** | ✅ | ⚠️ device | ✅ eSCL |

**Kritik notlar:**
- **Windows PDF:** winspool PDF'yi yazdırmak için **Acrobat/SumatraPDF** gibi "printto" handler'ı kayıtlı bir reader **şart**. Yoksa `NotSupported`. Bypass için `print_universal(path, "printer:631")` (direct IPP) kullan.
- **MinGW build'de WIA yok.** Scanner kütüphanesi eSCL (HTTP+XML) kullanır.
- **Android'de N dosya = N PrintManager dialog.** PrintManager bir seferde tek job alır.
- **`print_network`** ile **PDF gönderme** — PDF byte'ları 9100 portuna raw giderse **boş sayfa** basar. Sadece ESC/POS, PCL, ZPL gibi raw printer dilleri için kullan.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Kendi PDF writer** | TTF subsetting, DEFLATE, WinAnsi + Türkçe codepoint map |
| **PWG Raster encoder** | RFC 5102.4, PackBits, `RaS2` sync |
| **IPP client** | RFC 8010/8011, `ipp://` + `ipps://` (TLS) |
| **Smart IPP** | PDF/PWG/JPEG seçimi, busy retry, wait-for-completion |
| **30+ print option** | duplex, color, media, n-up, booklet, staple, punch, fold, margin, fit-to-page, job-sheets |
| **Rich document** | text runs, bold/italic, color, alignment, header/footer, page numbers, draw commands |
| **TTF embed** | Unicode desteği (Kiril, Arapça, CJK, ...) — sistemden font auto-resolve |
| **Image batch** | N fotoğraf → 1 job, grid layout, EXIF, fit/fill/stretch, max_dpi downscale |
| **Test page** | 6 tip: Diagnostic, ColorBar, Alignment, Grid, Gradient, NozzleCheck |
| **Supply / alerts** | marker-levels, low warning, durum uyarıları |
| **Scanner (SANE / WIA / eSCL)** | Linux SANE, Windows WIA veya eSCL, Android eSCL |
| **USB OTG / Bluetooth SPP** | Android'de raw yazıcı (termal, label) desteği |
| **Reprint cache** | Job → temp dosya eşleştir, sonra yeniden bas |
| **HP 7740 fix'leri** | airscan önce, Send-Document flow, geometry retry, ADF tek format |

---

## 🚀 Hızlı Başlangıç

### Basit metin yazdır

```cpp
#include "KernelNucleusT.hpp"

auto pr = knst_prn::printer::open_any();
if (!pr) return 1;

knst_prn::print_options opts;
opts.title = u"Merhaba test";
opts.copies = 2;
opts.duplex = knst_prn::duplex_mode::LongEdge;

auto job = pr.value.print_text(u"Merhaba dünya!", opts);
if (job) {
    std::cout << "Job ID: " << job.value << "\n";
}
```

### Direct IPP (CUPS'u atla)

```cpp
auto r = knst_prn::printer::print_ipp(
    u"/path/to/file.pdf",
    u"192.168.1.50:631",           // veya "ipps://printer.local"
    631,
    opts);

if (!r) std::cerr << knst_prn::status_string(r.st) << "\n";
```

### Smart IPP (PDF/PWG/JPEG otomatik)

```cpp
auto r = knst_prn::print_ipp_smart(
    u"/path/file.pdf",
    u"192.168.1.50");               // port otomatik (631 veya 443)

if (r) std::cout << "Job: " << r.value << "\n";
```

### Görsel batch (10 fotoğraf → 1 job)

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

### Tarayıcı

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

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Printer açma** | `open`, `open_default`, `open_any` |
| **Printer durum** | `status`, `info`, `make_and_model`, `capabilities`, `supply_levels`, `alerts`, `usage` |
| **Yazdırma** | `print_text`, `print_rich`, `print_image`, `print_images`, `print_batch`, `print_file`, `print_auto`, `print_pdf` |
| **Raw** | `send_raw`, `send_escpos`, `send_zpl`, `send_pcl`, `print_network` |
| **IPP** | `print_ipp`, `print_ipp_send_doc`, `print_jpeg_ipp`, `print_universal` |
| **Job kontrol** | `jobs`, `job_details`, `cancel_job`, `cancel_all`, `cancel_by_user`, `hold_job`, `release_job`, `set_job_priority`, `move_job`, `wait_for_job`, `is_job_done` |
| **Admin** | `pause`, `resume`, `set_default`, `options`, `set_default_option`, `set_option_permanent`, `add_printer`, `remove_printer`, `list_classes` |
| **Cache** | `set_cache`, `clear_cache`, `cache_size_bytes`, `reprint_job` |
| **Yardımcı** | `pdf_page_count`, `render_preview` |
| **Static** | `list_all`, `list_available_devices`, `list_drivers`, `server_status` |
| **Scanner** | `list_all`, `open`, `capabilities`, `scan_page`, `scan_to_file`, `scan_to_files`, `scan_to_pdf`, `scan_and_print` |

---

## 🎯 1) Enum'lar ve Tipler

### `status` — Genel hata enum'u

```cpp
enum class status : uint8_t {
    Ok = 0, InvalidArgument, NotFound, IoError, Timeout, BadState,
    OutOfMemory, ToolMissing, NotSupported, Permission, DecodeFailed, Unknown
};
```

`status_string()` ile okunabilir string alınır.

### `printer_state`

```cpp
enum class printer_state : uint8_t {
    Unknown = 0, Ready, Paused, Error, PaperJam, PaperOut,
    TonerLow, DoorOpen, Offline, Busy, WarmingUp
};
```

**Linux:** `printer-state-reasons` üzerinden çıkarılır — **sadece `-error` severity blocking**. `media-empty-report`, `marker-supply-low-warning` bilgi amaçlı, `TonerLow` döner.

**Windows:** `PRINTER_STATUS_*` flag'leri.

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

**`fit_mode` anlamları:**
- **`Fit`** — fotoğrafın tamamı görünür, aspect korunur, hücreye ortalanır
- **`Fill`** — hücre tamamen kaplanır, aspect korunur, taşan kısım **kırpılır**
- **`Stretch`** — hücre tamamen kaplanır, aspect **korunmaz** (distorsiyon)

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

`result<void>` özelleştirilmiş versiyonu var.

---

## 🖨️ 2) `printer` Sınıfı

### 2.1) Açma

```cpp
static result<printer> open(const knst_c16string& name);
static result<printer> open_default();
static result<printer> open_any();
```

**`open`** — isimle aç. Linux'ta `/etc/cups/printers.conf`'daki isim, Windows'ta `PRINTER_INFO_2W::pPrinterName`, Android'de `host:port`.

**`open_default`** — sistem default printer'ı.

**`open_any`** — default varsa onu, yoksa ilk enabled+accepting, yoksa ilk printer.

### 2.2) Durum ve bilgi

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

**Linux:** `lpoptions -l` çıktısından (PPD options) çıkarılır.
**Windows:** `DeviceCapabilitiesW` (`DC_COLORDEVICE`, `DC_DUPLEX`, `DC_PAPERS`, `DC_ENUMRESOLUTIONS`).
**Android:** IPP `printer-resolution-supported`, `media-supported`, `sides-supported`, `print-color-mode-supported`, `finishings-supported`.

### `supply_level`

```cpp
struct supply_level {
    knst_c16string name;        // "Black Cartridge HP 952"
    knst_c16string raw_type;    // "toner-cartridge"
    supply_kind type = supply_kind::Unknown;
    uint32_t percent = 0;
    bool known = true;          // false: printer seviye vermiyor
    bool low   = false;         // < marker-low-levels veya %15
};
```

**Linux:** `lpoptions` `marker-levels`, `marker-names`, `marker-types`, `marker-low-levels` çıktısı. Yoksa IPP `Get-Printer-Attributes` fallback.

**Windows:** `NotSupported`.

**Android:** IPP `Get-Printer-Attributes` üzerinden.

### 2.3) Yazdırma — Yüksek seviye

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

**`print_text`** — text'i PDF olarak render eder (Courier 10pt, sayfa başına). CUPS/winspool/PrintManager'a PDF submit eder.

**`print_rich`** — `rich_document` (styled text runs + shapes) → PDF → submit.

**`print_image`** — tek fotoğraf sayfa başına (fit mode = `Fit`, margin 5mm).

**`print_images`** — N fotoğraf, `image_batch_options` grid yapısı (varsayılan 1×1, yani her sayfada 1 foto). Hepsini **tek job** olarak gönderir.

**`print_batch`** — fotoğraf + text + PDF karışık liste. Ardışık fotoğraflar birleştirilir, PDF'ler passthrough, text'ler PDF olarak render edilir. Hepsi tek job'a.

**`print_file`** — dosya tipini algılar: text → `print_text`, image → `print_image`, PDF → `print_pdf`, PCL/ZPL/ESC-POS/PS → `send_raw`.

**`print_auto`** — `print_file` ile aynı.

### 2.4) `print_options` — 30+ alan

```cpp
struct print_options {
    // Sayfa
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
    knst_c16string media;              // raw CUPS media adı, `paper`'ı override eder
    knst_c16string page_ranges;        // "1-3,5"
    knst_c16string title;
    uint32_t       priority = 50;
    bool           fit_to_page = false;
    bool           raw = false;         // CUPS'a -o raw
    bool           collate = false;
    uint32_t       scaling = 100;
    knst_c16string resolution;          // "600dpi"
    bool           toner_save = false;
    bool           auto_rotate = false;
    bool           auto_center = false;
    // Görüntü
    int32_t        brightness = 0;      // -100..100
    int32_t        contrast = 0;        // -100..100 (Linux'ta ignored)
    int32_t        saturation = 0;
    bool           mirror_horizontal = false;
    bool           mirror_vertical = false;
    bool           invert_colors = false;   // CUPS filter'ları desteklemez
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
    // Ekstra — `key=value` (safe_value kontrolünden geçer)
    knst_vector<knst_c16string> extra_options;
};
```

**Doğrulama (`valid_options`):**
- `copies` 1..9999
- `priority` 1..100
- `n_up` 1..16
- `scaling` 10..400
- `media_weight_gsm` ≤ 500
- `brightness`, `saturation` -100..100
- `media`, `page_ranges`, `resolution`, `media_source`, `output_bin` — `safe_value` kontrolü
- `extra_options[i]` — `=` içermeli, `safe_value` kontrolü

**`safe_value` karakter seti:** `[A-Za-z0-9._:/,+*#-=@]` — space, tırnak, `&`, `|`, `<`, `>` **yasak**.

### 2.5) `rich_document` — Zengin belge

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
    float      x1_mm, y1_mm, x2_mm, y2_mm;   // sol üst köşe origin
    float      line_width = 0.5f;            // pt
    uint8_t    r, g, b;
    uint32_t   page = 1;                     // 1-based, 0 = her sayfa
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

    knst_vector<text_run>     elements;    // otomatik wrap + paginate
    knst_vector<draw_command> shapes;      // metnin altında çizilir

    // Custom font (Unicode):
    //   Boş (varsayılan)  → WinAnsi + 12 Type1 font (Latin-1 + Türkçe)
    //   Dolu (path)       → TTF/OTF dosyası Type0/CIDFontType2/Identity-H olarak embed edilir
    knst_c16string embed_font_path;
    bool auto_fallback = false;
};
```

**Font desteği (`embed_font_path`):**

Üç şekilde verilebilir:
1. **Tam dosya yolu:** `u"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"`
2. **Sistem font adı:** `u"NotoSans"`, `u"Arial"` — `knst_find_font()` sistemde arar (Windows: `C:\Windows\Fonts`, Linux: `/usr/share/fonts`...).
3. **Boş + `auto_fallback=true`:** PDF writer metni tarar, non-WinAnsi codepoint varsa sistemden Unicode font bulur (NotoSans → DejaVuSans → Arial → LiberationSans).

**Not:** TTF subset yapılır — sadece kullanılan glyph'ler embed edilir. PDF boyutu ~50-200 KB artar (300-800 KB değil).

**RTL / complex script:** per-character mapping yapılır, **RTL shaping yok**. Arapça, İbranice için harfler doğru sırada olmayabilir. CJK, Kiril, Yunan çalışır.

### 2.6) `image_batch_options` — Görsel batch

```cpp
struct image_batch_options {
    uint32_t rows = 1;                  // grid: rows × cols
    uint32_t cols = 1;
    fit_mode fit = fit_mode::Fit;
    float    margin_mm = 8.0f;          // sayfa kenarı
    float    gap_mm    = 4.0f;          // hücreler arası
    float    border_mm = 0.0f;          // >0 ince çerçeve
    uint32_t copies_each = 1;           // her fotoğraf N kez
    bool     rotate_to_fit = true;      // 90° çevir hücreye daha iyi sığıyorsa
    bool     apply_exif    = true;
    bool     show_filename = false;
    bool     skip_unreadable = true;    // okunamayan dosyalar fatal değil
    uint32_t max_dpi       = 300;       // downscale eşiği
    knst_vector<knst_c16string>* skipped = nullptr;   // atlananlar
};
```

**Limitler:** `rows` ve `cols` 1..8. Hücre boyutu 20 pt'den küçükse `InvalidArgument`.

**`max_dpi` ne yapar?** Fotoğrafın gerçek DPI'sı bu değerin üstündeyse `downscale_rgb` ile küçültülür. PDF boyutu 10 MB → 500 KB düşer.

### 2.7) `secure_print_options` — PIN'li yazdırma (sadece Linux)

```cpp
struct secure_print_options {
    knst_c16string job_password;    // [A-Za-z0-9._:/,+*#-=@], 1..32
    knst_c16string job_user;
};
```

**Linux:** `-o job-password=<pwd> -o job-password-encryption=none`.
**Windows/Android:** `NotSupported`.

### 2.8) `print_options` — örnek

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

## 🔌 3) Direct IPP — CUPS bypass

`knst_prn::printer` static metodları IPP'yi doğrudan destekler. `knst_network.hpp` bulunduysa TLS (`ipps://`) de çalışır.

### `print_ipp`

```cpp
static result<uint32_t> print_ipp(
    const knst_c16string& pdf_path,
    const knst_c16string& host_port,     // "192.168.1.5:631", "ipps://printer.local"
    uint16_t port = 631,
    const print_options& opts = {},
    const knst_c16string& doc_format = u"application/pdf");
```

**Ne yapar?** `Print-Job` IPP operasyonu. PDF inline body olarak gönderilir.

**Uygun olduğu yer:** Printer PDF kabul ediyorsa.

### `print_ipp_send_doc` — HP 7740 gibi printer'lar için

```cpp
static result<uint32_t> print_ipp_send_doc(
    const knst_c16string& pdf_path,
    const knst_c16string& host_port,
    uint16_t port = 631,
    const print_options& opts = {},
    const knst_c16string& doc_format = u"application/pdf");
```

**Ne yapar?** İki adım:
1. `Create-Job` (RFC 8011 §4.2.1)
2. `Send-Document` (RFC 8011 §4.3.1) — PDF body, `last-document=true`

**Ne zaman kullan?** Bazı HP printer'lar (7740 gibi) `Print-Job` içinde PDF'i reddeder (`0x0400 bad-request`). İki adımlı akış bunları çözer.

### `print_ipp_smart` — akıllı seçim

```cpp
result<uint32_t> print_ipp_smart(
    const knst_c16string& pdf_path,
    const knst_c16string& host_port,
    const print_options& o = {},
    const detail::ipp::send_policy& pol = {},
    std::string* info = nullptr);
```

**Ne yapar? (5 aşama)**

**1. Probe:**
- `Get-Printer-Attributes` → `document-format-supported`, `printer-state`, `printer-state-reasons`, `pwg-raster-document-resolution-supported`, `pwg-raster-document-type-supported`, `media-default`
- Önce `host:631` dener, olmazsa `host:443` (TLS)

**2. Preflight:**
- `printer-state == 5` (stopped) → `BadState`
- `printer-is-accepting-jobs == false` → `BadState`
- `printer-state-reasons` `-error` içeriyorsa → `BadState`
- `media-empty-report`, `marker-supply-low-warning` **blocking değil**

**3. Format seçimi (öncelik sırası):**
- Printer `application/pdf` destekliyor → PDF inline
- Değilse `image/pwg-raster` → PDF → PWG Raster encode → 1 job
- Değilse `image/jpeg` → PDF → JPEG per page → N job

**4. Submit:**
- `Print-Job`
- Busy (`0x0507`) → exponential backoff (500ms → 8s, toplam `busy_total_ms=180s`)
- Transport fail → job-name ile bul (duplicate protection)

**5. Wait:**
- `Get-Job-Attributes` polling
- Progress-based stall: 180s değişim yoksa → `Timeout`
- Job canceled/aborted → `BadState`
- Job not-found → OK (yaşlandı, tamamlanmış say)

### `send_policy` — akıllı IPP ayarları

```cpp
struct send_policy {
    uint32_t    io_idle_ms          = 60000;      // socket silence
    uint32_t    busy_total_ms       = 180000;     // busy retry toplam
    uint32_t    stall_ms            = 180000;     // ilerleme yok → Timeout
    uint32_t    total_ms            = 3600000;    // 1 saat absolute cap
    uint32_t    max_body_mb         = 160;        // PWG body cap
    bool        cancel_on_stall     = false;      // stalled job'ı iptal etme
    bool        wait_for_completion = true;
    std::string prefer;                           // "", "pdf", "pwg", "jpeg"
};
```

**`prefer` ne yapar?**
- `"pdf"` → sadece PDF dene
- `"pwg"` → sadece PWG
- `"jpeg"` → sadece JPEG per page
- `""` (varsayılan) → otomatik seç

### `print_universal` — her platformda aynı isim

```cpp
result<uint32_t> print_universal(
    const knst_c16string& path,
    const knst_c16string& host_port = {},     // boş → CUPS/winspool
    const print_options& o = {});
```

**Sırası:**
1. **Linux:** IPP (`print_ipp_smart`) → yoksa CUPS `print_file`
2. **Windows:** IPP (`print_ipp_smart`) → yoksa winspool `print_file`
3. **Android:** IPP (`print_ipp_smart`) → yoksa PrintManager dialog

**Öneri:** PDF + network printer'da **her zaman `print_universal`** kullan. Platform-spesifik tuzakları gizler.

---

## 📡 4) Job Yönetimi

### Job listesi

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

**Linux:** `lpstat -W not-completed -o` + `Get-Job-Attributes` (fast path) veya ipptool fallback.
**Windows:** `EnumJobsW` (JOB_INFO_2).
**Android:** IPP `Get-Jobs`.

### İptal

```cpp
result<void> cancel_job(uint32_t id);
result<void> cancel_all();
result<void> cancel_by_user(const knst_c16string& user);
```

**Linux:** IPP `Cancel-Job` → başarısızsa `cancel` CLI.
**Windows:** `SetJob(..., JOB_CONTROL_DELETE)`.
**Android:** IPP `Cancel-Job`.

**Not:** zaten bitmiş job iptal edilemez → `BadState` (`0x0404`/`0x0406`).

### Hold / release

```cpp
result<void> hold_job(uint32_t id);
result<void> release_job(uint32_t id);
result<void> hold_all();
result<void> release_all();
```

**Linux:** IPP `Set-Job-Attributes` `job-state=4` (held) veya `3` (pending). Bazı printer'lar reddeder → `NotSupported`, o zaman `lp -i <id> -H hold/resume` fallback.
**Windows:** `SetJob(..., JOB_CONTROL_PAUSE/RESUME)`.
**Android:** IPP `Set-Job-Attributes`.

### Priority / wait

```cpp
result<void> set_job_priority(uint32_t id, uint32_t prio);   // 1..100
result<void> wait_for_job(uint32_t id, uint32_t timeout_ms = 60000);
result<bool> is_job_done(uint32_t id) const;
```

**`wait_for_job` davranışı:**
- Job listeden kaybolduysa → **başarı** (yaşlandı, muhtemelen tamam)
- Job canceled/aborted → **`BadState`**
- Timeout → **`Timeout`**

### Move (sadece Linux)

```cpp
result<void> move_job(uint32_t id, const knst_c16string& dst);
result<void> move_all_jobs(const knst_c16string& dst);
```

`lpmove` CLI. Başka printer'a taşır.

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

`pause`/`resume` `SetPrinter(PRINTER_CONTROL_PAUSE/RESUME)`, diğerleri çoğunlukla `NotSupported`.

### Printer ekleme/çıkarma (Linux)

```cpp
static result<void> add_printer(name, uri, driver_id, desc = {}, loc = {});
static result<void> add_ipp_printer(name, uri);           // "everywhere" driver
static result<void> remove_printer(name);
```

**URI scheme kontrolü:** `ipp://`, `ipps://`, `socket://`, `lpd://`, `http://`, `https://`, `usb://`, `hp:/`, `dnssd://`.

### Printer class'ları (Linux)

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

Altı tip:

| Tip | Ne içerir |
|---|---|
| **Diagnostic** | Tüm font örnekleri, Türkçe karakterler, semboller, renk bar, gri ton rampası |
| **ColorBar** | 7 renk bloğu (C, M, Y, K, R, G, B) + gri rampa |
| **Alignment** | Köşe işaretleri, merkez artı, cetvel (10mm, 50mm bold) |
| **Grid** | 10mm'lik kareli grid, 50mm'de bold |
| **Gradient** | 7 gradient (beyaz → renk), her biri ayrı satır |
| **NozzleCheck** | 4 renk için dikey + yatay çizgiler + solid blok (nozzle tıkanıklığı tespiti) |

Kullanım:

```cpp
pr.value.print_test_page(knst_prn::test_page_type::NozzleCheck, opts);
```

---

## 🖼️ 7) PDF Writer (internal)

Kütüphane kendi PDF 1.4 yazıcısına sahip. `detail::write_pdf(doc, path)`:

- **Sayfa boyutu:** `document::w_pt`, `h_pt` (pt = 1/72")
- **Font:** 12 built-in Type1 (Helvetica, Courier, Times + Bold/Oblique/BoldOblique). Türkçe codepoint'ler (Ğ, ğ, İ, ı, Ş, ş) WinAnsi slot'larına remap edilir.
- **Custom TTF:** `embed_font_path` + `auto_fallback` ile Unicode font subset embed.
- **DEFLATE:** Kendi fixed Huffman + LZ77 encoder'ı.
- **Görsel:** JPEG passthrough (kalite yakınsa) veya decode → downscale → DEFLATE RGB.

**PNG writer** (`write_png_simple`): RGB → Sub filter → fixed Huffman DEFLATE → PNG. `decode_to_png(src, dst)` herhangi bir görseli PNG'ye çevirir.

**Sayfa sayısı** (`count_pdf_pages_scan`): `/Type /Page` string tarar (compressed object stream'lerde başarısız, o yüzden `pdf_page_count` ilk `pdfinfo`/`qpdf` dener).

**Preview** (`render_preview`):
- Linux: `pdftoppm -png -singlefile -f N -l N -r DPI` → PNG bytes
- Windows: `NotSupported`
- Android: JNI `KnstPdfRasterizer.rasterize()` → JPEG → `decode_to_png` → PNG

---

## 🖨️ 8) Windows'a Özel

### GDI render

`print_text`, `print_rich`, `print_image`, `print_images`, `print_test_page` **doğrudan GDI** kullanır (`CreateDCW` + `StartDocW` + `TextOutW`/`Rectangle`/`StretchDIBits`).

**Ne kazandırır?** PDF reader'a bağımlılık **yok** — text, image, rich doc native spooler üzerinden gider.

**DEVMODE** üzerinden `copies`, `duplex`, `color`, `orientation`, `paper size`, `quality`, `media type` kontrol edilir.

### PDF yazdırma — kritik tuzak

`print_pdf` → `ShellExecuteW(L"printto", file, printer)` çağırır. **PDF için "printto" handler kayıtlı olması şart** (Acrobat, SumatraPDF, Foxit, Edge...).

Yoksa: `status::NotSupported` + `ShellExecuteW` dönüş kodu (`SE_ERR_NOASSOC = 31`).

**Bypass:** `print_universal(path, "printer:631")` — direct IPP PDF'i reader'sız gönderir.

### RAW datatype

`send_raw(data)` → `StartDocPrinterW(RAW)` + `WritePrinter`. Chunk 64 KB. `WritePrinter` ortada başarısız olursa `AbortPrinter` — yarım job spool'a yazılmaz.

### `send_raw` byte sniff

`print_data(d, len)` magic byte'lara bakar:
- `%PDF-` → PDF
- `%!` → PostScript
- `\xFF\xD8\xFF` → JPEG
- `\x89PNG` → PNG
- `GIF8` → GIF
- `BM` → BMP
- `P6`/`P5` → PNM
- `II*\0` / `MM\0*` → TIFF
- `RIFF...WEBP` → WEBP

**Eşleşme varsa** → temp dosyaya yaz, `print_file` (spooler driver chain üzerinden).
**Eşleşme yoksa** → `send_raw` (RAW datatype).

### Temp cleanup

Windows'ta `shell_print` 15 saniye içinde job görünmezse `R::success(0)` döner (sent, id unknown). Temp PDF **silinmez** — reader hâlâ okuyabilir. Sonraki `print_data` çağrıları `cleanup_stale_temp_files()` (1+ saat eski `knst_print*`) süpürür.

---

## 🤖 9) Android'e Özel

### PrintManager dialog

`print_text`, `print_rich`, `print_image`, `print_images`, `print_batch`, `print_test_page` → PDF oluştur → **JNI ile `PrintManager.print()`** dialog açar.

**PrintAttributes** `PrintManager`'a aktarılır:
- `MediaSize` (A3..Executive)
- `ColorMode` (2=Color, 1=Mono)
- `DuplexMode` (0=None, 1=LongEdge, 2=ShortEdge)
- `Resolution` (custom, opts.resolution'dan)

**`document_id`** `opts.cache_for_reprint` açıksa cache'e yazılır.

**Not:** PrintManager **bir job** kabul eder. `print_files(N)` = N dialog.

### USB OTG raw (bulk)

`print_usb(data)`:
1. `UsbManager.getDeviceList` → her device için `openDevice`
2. Interface tara: **class 7 (Printer)** veya **class 0xFF (Vendor)**
3. Bu interface'te **direction=OUT, type=BULK** endpoint bul
4. `claimInterface` → `bulkTransfer` 8 KB chunk'lar
5. `releaseInterface` + `close`

**Kullanım:** termal (ESC/POS), label (ZPL), PCL yazıcılar.

### Bluetooth SPP raw

`print_bluetooth(data, "AA:BB:CC:DD:EE:FF")`:
1. `BluetoothAdapter.getRemoteDevice`
2. `createRfcommSocketToServiceRecord(UUID 00001101-0000-1000-8000-00805F9B34FB)`
3. `connect` + `getOutputStream` → write

**Kullanım:** mobil termal printer'lar (Zebra, Epson TM).

### mDNS printer discovery

`printer::list_all` → JNI `KnstPrintDiscoveryHelper.discover(4000)` → Java `NsdManager.discoverServices("_ipp._tcp")` + `_ipps._tcp`. Her printer `name|host|port|uri` formatında döner.

`scanner::list_all` → `discoverScanners(4000)` → `_uscan._tcp` → `name|host|port` formatı.

**mDNS retry:** HP 7740 gibi bazı printer'lar PTR'i hemen gönderir, SRV/A 300-500 ms sonra ayrı pakette. Kod empty sonuç üzerine 500 ms bekleyip **tekrar dener**, sonra 5 dakika cache'ler.

### PDF rasterize (JNI)

`KnstPdfRasterizer.rasterize(path, outDir, dpi, quality)` → Java `PdfRenderer` ile her sayfayı JPEG'e çevirir, `[Ljava/lang/String;` döner.

**Kullanım:** `print_batch` PDF'i görsel gibi işlemesi gerektiğinde, `render_preview` her zaman.

### Scanner — sadece eSCL

Android'de **SANE yok, WIA yok**. Scanner eSCL (AirScan) protokolünü kullanır:
1. `GET /eSCL/ScannerCapabilities` → XML parse
2. `POST /eSCL/ScanJobs` (`ScanSettings` XML) → 201 + Location
3. `GET <Location>/NextDocument` → JPEG sayfa
4. ADF modunda 404/410'a kadar tekrar

**Format:** `image/jpeg` (native eSCL). PDF veya PNG isteniyorsa içeride çevirir.

---

## 🐧 10) Linux'a Özel

### CUPS CLI

`printer::print_*` → `lp -d <printer> -o <opts>` çağırır. `build_args` her `print_options` alanını CUPS'un `-o key=value` formatına çevirir.

**`ppd()` yardımcısı:** Bir PPD option var mı diye `lpoptions -l` çıktısına bakar. Varsa PPD adını da geçirir (örneğin `sides=two-sided-long-edge` yanında `Duplex=DuplexNoTumble`).

### Scanner SANE

`scanimage -d <device> --format=<fmt> --mode=<mode> --resolution=<dpi> ...`

**Capabilities cache:** `scanimage -A` 2-5 saniye sürebilir (özellikle eSCL üzerinde). `scanner::capabilities()` sonucu **process-lifetime** cache'lenir (1 saat TTL).

**Retry:**
- **Flatbed:** jpeg → png → bmp (sayfa hareket etmez, güvenli)
- **ADF:** **sadece jpeg** (her Download feeder'ı ilerletir, retry sheet kaybı)

### airscan vs escl sıralaması

`list_all` sonucu **airscan** backend'ini önce döner. Neden? HP'nin `escl` backend'i `--source=Flatbed`'i yok sayar, ADF boşken HTTP 409 döner. `airscan` (sane-airscan) bunu doğru işler.

### HP 7740 geometry retry

`scan_pages` ilk denemede `-x <w> -y <h>` gönderir. HP firmware bazen bunu reddeder → **geometry'siz retry**.

### `scanimage` exit code

`0` = başarı. `!= 0` + sayfa yok = gerçek hata. **`code == 0` + sayfa yok** = ADF boş veya cihaz ulaşılamaz — **retry anlamsız**, direkt `NotFound`.

---

## 🎥 11) `print_options` matris — ne nerede çalışır?

| Alan | Linux (CUPS) | Windows (GDI) | Android (PrintManager) |
|---|---|---|---|
| `copies` | ✅ `-n` | ✅ `DM_COPIES` | ⚠️ dialog'da |
| `duplex` | ✅ `sides` | ✅ `DM_DUPLEX` | ✅ `setDuplexMode` |
| `color` | ✅ `print-color-mode` | ✅ `DM_COLOR` | ✅ `setColorMode` |
| `orient` | ✅ `orientation-requested` | ✅ `DM_ORIENTATION` | ❌ (PDF'de) |
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
| `raw` | ✅ `-o raw` | ⚠️ (sayfa başına RAW) | ❌ |
| `toner_save` | ✅ `print-quality=3` | ❌ | ❌ |
| `job_sheet_*` | ✅ `job-sheets` | ❌ | ❌ |

`⚠️` = printer driver'ına bağlı.

---

## ⚠️ 12) Sık Tuzaklar

### 1) Windows'ta PDF için reader yok

`print_pdf` → `NotSupported`. Çözüm: `print_universal(path, "host:631")` veya `print_ipp(path, "host:631")`.

### 2) `print_network` PDF göndermek

**Yapma.** 9100 portu raw ESC/POS/PCL/ZPL alır, PDF değil. PDF byte'ları raw giderse boş sayfa basar.

### 3) HP 7740 geometry reddeder

Kod otomatik geometry'siz retry yapar. Ama **ADF'de retry tehlikeli** — feeder sheet'i tüketmiş olabilir. ADF her zaman tek format (JPEG) dener.

### 4) HP 7740 Print-Job reddeder

Kod `print_ipp_smart` içinde önce `print_ipp` dener. Başarısızsa **manuel `print_ipp_send_doc`** çağır.

### 5) `escl` vs `airscan` (Linux scanner)

`escl` backend HP'de `--source=Flatbed` yoksayar. `list_all` airscan'i önce sıralar. **Zorla `escl` kullanma.**

### 6) Android PrintManager tek job

N dosya = N dialog. Batch tek dialog için `print_batch` (fotoğraf/text tek PDF).

### 7) `print_secure` sadece Linux

Windows/Android → `NotSupported`. Windows'ta workaround yok (spooler PIN API'si yok).

### 8) TTF embed PDF'i şişirir

Full file embed (subset yok) → ~300-800 KB. Kod **subset** yapar — sadece kullanılan glyph'ler, ~30-80 KB.

### 9) RTL shaping yok

Arapça/İbranice per-character map çalışır ama **harfler bağlanmaz, RTL sıralama yok**. CJK/Kiril/Yunan tam çalışır.

### 10) `pdf_page_count` compressed object stream'de başarısız

Scan fallback `/Type /Page` string'i arar — object streams içine gizlenmiş sayfalar görülmez. Linux'ta `pdfinfo`/`qpdf` önce dener, ikisi de yoksa `ToolMissing`.

### 11) `wait_for_job` "not found" = success

Job listeden kaybolduysa **tamamlanmış** sayılır. Windows'ta bitmiş job listeden çıkar (EnumJobs). Linux'ta `completed` filter'da döner — ama `cancel_job` sonrası da listeden çıkar, o yüzden `wait_for_job` öncesi `cancel` yapmadığından emin ol.

### 12) `safe_value` karakter seti dar

`-o key=value` formatında `=` sonrası **sadece** `[A-Za-z0-9._:/,+*#-=@]`. Kullanıcı `"my document"` verirse → **reddedilir**. Boşluk gerekirse CUPS'a özel escape (`%20`) — **kod bunu yapmıyor**, `InvalidArgument`.

### 13) Printer adı 32 karakter

`is_safe_token(name, 32)` — daha uzun printer adı `InvalidArgument`. Linux/Windows/Android hepsinde aynı.

### 14) Windows `pdf_page_count` compressed PDF'te `NotSupported`

Scan-based, object streams içinde `ToolMissing`/`NotSupported`.

### 15) `render_preview` sadece Linux + Android

Windows'ta `NotSupported`. Alternatif: `pdf_page_count` + external tool.

### 16) Scanner capabilities cache 1 saat

`scanner::capabilities()` sonucu cache'lenir. Scanner değişirse program restart gerek.

### 17) ADF `page_count=0` → 500 sayfa cap

`scan_pages` `o.page_count == 0` durumunda max 500 sayfa dener. ADF 50-100 sayfa tutar, güvenli cap.

### 18) `print_images` empty `paths` → `InvalidArgument`

Tek fotoğraf için `print_image` kullan.

### 19) Windows `print_data` PDF temp cleanup gecikmeli

`print_data(pdf)` → `shell_print` → 15 s timeout olursa temp dosya **silinmez** (reader hâlâ okuyabilir). Sonraki çağrılarda `cleanup_stale_temp_files()` (>1h) süpürür.

### 20) `print_batch` Android'de N fotoğraf → 1 dialog

Ama **PDF içerik** varsa `rasterize_pdf_to_jpegs` (JNI) çağrılır — `KnstPdfRasterizer` yoksa `DecodeFailed`.

---

## 📊 13) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| `open` | O(1) | CUPS/winspool/IPP probe |
| `status` / `info` | ~50-200 ms | CLI spawn (Linux) / WMI (Windows) |
| `capabilities` | ~100-500 ms | `lpoptions -l` / `DeviceCapabilities` / IPP |
| `supply_levels` | ~100-300 ms | IPP Get-Printer-Attributes |
| `print_text` | O(text) + PDF render | CUPS submit |
| `print_images` | O(N × decode + resize) | 10 foto = ~1-3 s |
| `print_ipp` | ~1-2 s + transfer | Network |
| `print_ipp_smart` | ~2-5 s (probe + submit) | PWG encode ekstra |
| `print_universal` | ~1-10 s | Path seçimine bağlı |
| `wait_for_job` | O(job süresi) | 500 ms poll |
| `scanner::list_all` | ~2-5 s | SANE/mDNS |
| `scanner::capabilities` | ~2-5 s (cache'li 0) | İlk çağrı yavaş |
| `scan_page` | O(dpi² × alan) + I/O | 300 dpi A4 = ~10-30 s |
| `scan_to_pdf` | + PDF render | ~1-3 s ekstra |

**İpuçları:**
- `printer::capabilities()` **sonucu cache'le** — her printte çağırma
- `print_universal` → `print_file` yerine (IPP fast path)
- `image_batch_options::max_dpi` düşür → PDF boyutu küçülür
- `scanner::open` + `capabilities` bir kez, sonra scan loop

---

## ✅ 14) Yap / ❌ Yapma

### ✅ Yap

```cpp
// 1. Universal print (platform tuzakları gizli)
knst_prn::printer::print_universal(path, u"192.168.1.50:631", opts);

// 2. Windows PDF için
pr.print_universal(pdf, u"printer.local:631", opts);   // IPP bypass

// 3. HP 7740 için explicit Send-Document
knst_prn::printer::print_ipp_send_doc(pdf, u"printer:631", 631, opts);

// 4. N fotoğraf tek job
knst_prn::image_batch_options bo;
bo.rows = 2; bo.cols = 2;
bo.max_dpi = 200;              // PDF küçük
pr.print_images(paths, bo, opts);

// 5. Scanner: capabilities bir kez
auto sc = knst_prn::scanner::open(device);
auto caps = sc.value.capabilities();   // cache'lendi
for (int i = 0; i < 10; ++i) sc.value.scan_page(sopts);

// 6. Android print: single dialog
pr.print_batch({a, b, c}, opts);   // N fotoğraf + text → 1 dialog

// 7. Raw için magic byte kontrolü
pr.print_data(data, len, opts);    // PDF/PNG ise spooler, değilse RAW
```

### ❌ Yapma

```cpp
// 1. PDF'yi 9100'e raw gönderme
pr.print_network(pdf_bytes, u"192.168.1.5", 9100);   // ❌ boş sayfa

// 2. Windows'ta PDF için print_pdf
pr.print_pdf(pdf, opts);   // ❌ PDF reader yoksa NotSupported
// ✅ print_universal kullan

// 3. capabilities'i her printte
for (int i = 0; i < 100; ++i) {
    pr.value.capabilities();   // ❌ CLI spawn 100×
}

// 4. Android'de N fotoğraf için N dialog
for (auto& p : paths) pr.value.print_image(p);   // ❌ N dialog
// ✅ print_batch

// 5. ADF'de retry
// ❌ feeder sheet kaybı — kod bunu otomatik yapmıyor ama manuel denemeyin

// 6. escl backend zorla (Linux HP)
// ❌ listenin başındaki airscan'i kullan

// 7. `-o key=value` içine space
opts.extra_options.push_back(u"my option=value");   // ❌ safe_value reddeder

// 8. `print_secure` Windows/Android'de
pr.print_secure(pdf, {u"1234", u"user"}, opts);   // ❌ NotSupported
```

---

## 🔥 15) Gerçek Kullanım Örnekleri

### Örnek 1: Rapor yazdır (rich document)

```cpp
knst_prn::rich_document doc;
doc.page_w_mm = 210; doc.page_h_mm = 297;
doc.show_page_number = true;
doc.show_date = true;
doc.header_text = u"Şirket Raporu";

knst_prn::text_run title;
title.text = u"2025 Q4 Raporu";
title.style.font = u"Helvetica-Bold";
title.style.size_pt = 20;
title.style.align = knst_prn::alignment::Center;
doc.elements.push_back(title);

knst_prn::text_run body;
body.text = u"Bu rapor 2025 Q4 dönemini kapsamaktadır. " /* çok satır */;
body.style.size_pt = 11;
body.style.line_spacing = 1.4f;
doc.elements.push_back(body);

knst_prn::draw_command line;
line.type = knst_prn::shape_type::Line;
line.x1_mm = 20; line.y1_mm = 40; line.x2_mm = 190; line.y2_mm = 40;
line.line_width = 1.0f;
doc.shapes.push_back(line);

// Türkçe için TTF embed
doc.embed_font_path = u"NotoSans";

auto pr = knst_prn::printer::open_any();
knst_prn::print_options opts;
opts.duplex = knst_prn::duplex_mode::LongEdge;
opts.copies = 1;

pr.value.print_rich(doc, opts);
```

### Örnek 2: Fotoğraf albümü (grid)

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

### Örnek 3: HP 7740 scanner ADF duplex

```cpp
auto scanners = knst_prn::scanner::list_all();
// list_all airscan'i önce sıralar
auto sc = knst_prn::scanner::open(scanners.value[0].device_id);

knst_prn::scan_options so;
so.dpi = 300;
so.mode = knst_prn::color_mode::Color;
so.paper = knst_prn::media_size::A4;
so.use_adf = true;
so.duplex = true;
so.page_count = 0;   // feeder boşalana kadar
so.output_format = u"pdf";
so.timeout_s = 300;

sc.value.scan_to_pdf(u"/tmp/scan.pdf", so);
```

### Örnek 4: Termal yazıcı (Android USB OTG)

```cpp
knst_prn::printer pr;
knst_byte_string escpos = /* ESC/POS bytes */;

auto r = pr.print_usb(escpos);
if (r) std::cout << "USB yazdırma OK\n";

// Bluetooth SPP
knst_byte_string escpos2 = /* ... */;
pr.print_bluetooth(escpos2, u"DC:0D:30:AB:CD:EF");
```

### Örnek 5: Batch job monitoring

```cpp
auto pr = knst_prn::printer::open_any();

knst_prn::print_options opts;
opts.cache_for_reprint = true;

auto job = pr.value.print_pdf(u"big_report.pdf", opts);
if (job) {
    std::cout << "Job ID: " << job.value << "\n";
    
    if (pr.value.wait_for_job(job.value, 600000)) {
        std::cout << "Tamamlandı\n";
    } else {
        std::cerr << "Timeout veya iptal\n";
        // Job detayını al
        auto d = pr.value.job_details(job.value);
        if (d) std::cout << "State: " << (int)d.value.state << "\n";
    }
}
```

### Örnek 6: Windows PDF → IPP bypass

```cpp
// Windows'ta reader yok, direct IPP kullan
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

### Örnek 7: Job queue izleme

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

### Örnek 8: Scanner → print (kopyalama)

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

## 📌 16) API Kontrat Özeti

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

## 🎯 Size Getireceği Kazanç

`knst_prn` şu durumlarda ciddi avantaj sağlar:

- ✅ **Cross-platform yazdırma** — aynı kod Linux/Windows/Android
- ✅ **Kendi PDF yazıcısı** — external PDF kütüphanesi yok
- ✅ **TTF subsetting** — Unicode desteği, küçük PDF boyutu
- ✅ **IPP client (ipp + ipps)** — TLS, RFC 8010/8011
- ✅ **PWG Raster** — modern printer'lar için optimal format
- ✅ **Smart IPP** — probe → format seçimi → submit → wait
- ✅ **HP 7740 fix'leri** — airscan önceliği, Send-Document, geometry retry
- ✅ **30+ print option** — duplex, color, n-up, booklet, finishings
- ✅ **Rich document** — text runs + shapes + header/footer
- ✅ **Image batch** — N fotoğraf → 1 job, grid layout
- ✅ **Test page** — 6 tip, diagnostic/colorbar/alignment/grid/gradient/nozzle
- ✅ **Job management** — cancel, hold, release, priority, move, wait
- ✅ **Supply / alerts** — marker-levels, low warning
- ✅ **Scanner (SANE / WIA / eSCL)** — hepsini tek API'de
- ✅ **USB OTG + Bluetooth SPP** — Android termal/label yazıcı
- ✅ **Reprint cache** — job → temp dosya eşleştir, yeniden bas
- ✅ **KernelNucleusT uyumu** — `knst_file`, `knst_process`, `knst_network`, `knst_c16string`, `knst_image_loader` ile tam entegre

---