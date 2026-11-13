/*
  knst_printer_basic.cpp — safe demo of knst_prn (printer + scanner)

  RUNTIME MODES
  =============
    ./build/bin/knst_app                          → inspect only (no print/scan)
    KNST_PRINT_AND_SCAN=1 ./build/bin/knst_app    → print 3 pages as ONE job + scan 1 page

  Compile once, run either way. No rebuild to switch modes.

  PRINT JOB GROUP
  ===============
    The 3 pages (image + rich text + diagnostic test page) are combined
    into ONE PDF document (detail::document) and submitted as ONE CUPS
    job. This is how the printer produces them back-to-back without gaps.

    If you don't have a PDF reader—for instance, when using Windows—try printing as a JPEG, similar to the approach shown in the android examples.

  SAFETY
  ======
    * Every wait is capped (READY_WAIT_MS / JOB_WAIT_MS).
    * Offline printers / missing scanners are skipped with a clear message.
    * Errors surface as result<> failures — never as hangs.
    * No disk is modified except a temp sandbox that is removed at exit.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>



// ============================================================
// Runtime mode detection
// ============================================================

// Returns true when KNST_PRINT_AND_SCAN is set to a non-empty, non-zero value.
static bool do_print_and_scan() {
#if defined(KNST_PRINT_AND_SCAN)
    return true;
#else
    const char* e = std::getenv("KNST_PRINT_AND_SCAN");
    return e && e[0] != '\0' && !(e[0] == '0' && e[1] == '\0');
#endif
}


// ============================================================
// Timing caps
// ============================================================

static constexpr uint32_t DISCOVERY_WAIT_MS = 5000;
static constexpr uint32_t READY_WAIT_MS     = 3000;
static constexpr uint32_t JOB_WAIT_MS       = 60000;
static constexpr uint32_t JOB_POLL_MS       = 500;


// ============================================================
// Console helpers
// ============================================================

static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}

static void banner(const char* title) {
    std::cout << "\n"
              << "==================================================\n"
              << "  " << title << "\n"
              << "==================================================\n";
}

static const char* platform_name() {
#if KNST_USING_PLATFORM_WINDOWS
    return "Windows (winspool + GDI, WIA if MSVC)";
#elif KNST_USING_PLATFORM_LINUX
    return "Linux (CUPS + SANE)";
#else
    return "Other";
#endif
}

static const char* state_name(knst_prn::printer_state s) {
    switch (s) {
        case knst_prn::printer_state::Ready:     return "ready";
        case knst_prn::printer_state::Paused:    return "paused";
        case knst_prn::printer_state::Error:     return "error";
        case knst_prn::printer_state::PaperJam:  return "paper jam";
        case knst_prn::printer_state::PaperOut:  return "paper out";
        case knst_prn::printer_state::TonerLow:  return "toner low";
        case knst_prn::printer_state::DoorOpen:  return "door open";
        case knst_prn::printer_state::Offline:   return "offline";
        case knst_prn::printer_state::Busy:      return "busy";
        case knst_prn::printer_state::WarmingUp: return "warming up";
        default:                                  return "unknown";
    }
}

static const char* job_state_name(knst_prn::job_state s) {
    switch (s) {
        case knst_prn::job_state::Pending:    return "pending";
        case knst_prn::job_state::Held:       return "held";
        case knst_prn::job_state::Processing: return "processing";
        case knst_prn::job_state::Spooling:   return "spooling";
        case knst_prn::job_state::Stopped:    return "stopped";
        case knst_prn::job_state::Blocked:    return "blocked";
        case knst_prn::job_state::Canceled:   return "canceled";
        case knst_prn::job_state::Aborted:    return "aborted";
        case knst_prn::job_state::Completed:  return "completed";
        default:                               return "unknown";
    }
}


// ============================================================
// Test image generator
// ============================================================

static void generate_test_image(uint32_t w, uint32_t h, knst_byte_string& rgb) {
    rgb.resize(w * h * 3);
    if (rgb.length() != w * h * 3) return;
    uint8_t* p = &rgb[0];
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* d = p + (static_cast<uint64_t>(y) * w + x) * 3;
            uint8_t r, g, b;
            if (y < h / 2) {
                float t = static_cast<float>(x) / static_cast<float>(w);
                r = static_cast<uint8_t>(255 * t);
                g = static_cast<uint8_t>(255 * (1.0f - t));
                b = 128;
            } else {
                uint32_t band = (x * 6) / w;
                switch (band) {
                    case 0: r=255; g=0;   b=0;   break;
                    case 1: r=0;   g=255; b=0;   break;
                    case 2: r=0;   g=0;   b=255; break;
                    case 3: r=255; g=255; b=0;   break;
                    case 4: r=0;   g=255; b=255; break;
                    case 5: r=255; g=0;   b=255; break;
                    default: r=g=b=128; break;
                }
            }
            uint32_t cx = w * 3 / 4, cy = h * 3 / 4;
            if (x >= cx && y >= cy) {
                bool black = ((x/8 + y/8) % 2) == 0;
                r = g = b = black ? 0 : 255;
            }
            d[0]=r; d[1]=g; d[2]=b;
        }
    }
}

static knst_byte_string encode_ppm(const knst_byte_string& rgb, uint32_t w, uint32_t h) {
    knst_byte_string out;
    char hdr[64];
    int hl = ::snprintf(hdr, sizeof(hdr), "P6\n%u %u\n255\n", w, h);
    if (hl <= 0) return out;
    out.append(reinterpret_cast<const unsigned char*>(hdr), (uint32_t)hl);
    out.append(rgb.data(), rgb.length());
    return out;
}


// ============================================================
// Robust printer discovery — never blocks, never hangs
// ============================================================

static bool printer_is_ready(knst_prn::printer& p) {
    auto st = p.status();
    if (!st) return false;
    if (!st.value.enabled || !st.value.accepting) return false;
    switch (st.value.state) {
        case knst_prn::printer_state::Ready:
        case knst_prn::printer_state::Busy:
        case knst_prn::printer_state::TonerLow:
        case knst_prn::printer_state::WarmingUp:
        case knst_prn::printer_state::Unknown:
            return true;
        default:
            return false;
    }
}

static bool open_ready_printer(knst_prn::printer& out) {
    auto all = knst_prn::printer::list_all();
    if (!all) {
        std::cout << "  list_all(): " << all.error_string() << "\n";
        return false;
    }
    for (auto& ex : all.value) {
        if (!ex.is_enabled || !ex.is_accepting) continue;
        if (ex.state != knst_prn::printer_state::Ready &&
            ex.state != knst_prn::printer_state::Busy &&
            ex.state != knst_prn::printer_state::TonerLow) continue;
        auto pr = knst_prn::printer::open(ex.name);
        if (pr) {
            out = std::move(pr.value);
            return true;
        }
    }
    auto def = knst_prn::printer::open_default();
    if (def) {
        if (def.value.wait_for_ready(READY_WAIT_MS)) {
            out = std::move(def.value);
            return true;
        }
        std::cout << "  default printer not ready within "
                  << READY_WAIT_MS << " ms — skipping print steps\n";
    }
    return false;
}

static bool wait_job_group(knst_prn::printer& p,
                            const std::vector<uint32_t>& ids,
                            uint32_t cap_ms) {
    if (ids.empty()) return true;

    if (!printer_is_ready(p)) {
        std::cout << "  printer went offline — jobs will stay in the queue\n";
        return false;
    }

    auto t0 = std::chrono::steady_clock::now();
    int last_done = -1;
    while (true) {
        auto pending = p.jobs(false);
        if (!pending) return false;

        int done = 0;
        for (auto id : ids) {
            bool still = false;
            for (auto& j : pending.value) if (j.id == id) { still = true; break; }
            if (!still) ++done;
        }
        if (done != last_done) {
            std::cout << "    [" << done << "/" << ids.size() << " done]\n";
            last_done = done;
        }
        if (done == static_cast<int>(ids.size())) return true;

        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (static_cast<uint32_t>(el) >= cap_ms) {
            std::cout << "    cap reached at " << done
                      << "/" << ids.size() << " done\n";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(JOB_POLL_MS));
    }
}


// ============================================================
// Combine 3 different page types into ONE PDF document
// ============================================================

// Appends `part` to `combined`, fixing up image indices so the merged
// document references the right image objects.
static void append_doc(knst_prn::detail::document& combined,
                       knst_prn::detail::document& part) {
    using namespace knst_prn::detail;
    uint32_t off = static_cast<uint32_t>(combined.images.size());
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
}

// Builds a 3-page document:
//   page 1 — test image (PPM) fit on A4
//   page 2 — rich text with Turkish chars + shapes + header/footer
//   page 3 — diagnostic test page
static knst_prn::status build_three_page_document(const knst_c16string& ppm_path,
                                                    const knst_c16string& printer_name,
                                                    knst_prn::detail::document& combined) {
    using namespace knst_prn::detail;

    float w_mm = 210.0f, h_mm = 297.0f;

    // ── Page 1: image ────────────────────────────────────────────────
    {
        document part;
        knst_prn::image_batch_options bo;
        bo.margin_mm = 5.0f;
        bo.fit       = knst_prn::fit_mode::Fit;
        std::vector<knst_c16string> one{ ppm_path };
        auto st = layout_images(one, bo, w_mm, h_mm, part);
        if (st != knst_prn::status::Ok) return st;
        append_doc(combined, part);
    }

    // ── Page 2: rich text ────────────────────────────────────────────
    {
        knst_prn::rich_document rd;
        rd.header_text = u"KNST PRINTER DEMO";
        rd.footer_text = u"Page 2 of 3";
        rd.show_page_number = true;
        rd.show_date        = true;

        knst_prn::text_run title;
        title.text = u"KNST PRINTER DEMO";
        title.style.size_pt = 28.0f;
        title.style.bold    = true;
        title.style.r = 0; title.style.g = 60; title.style.b = 160;
        title.style.align = knst_prn::alignment::Center;
        rd.elements.push_back(title);

        knst_prn::text_run sub;
        sub.text = u"\n"
                   u"This is page 2 of a 3-page demo.\n\n"
                   u"It demonstrates:\n"
                   u"  \u2022 Automatic text wrapping\n"
                   u"  \u2022 Turkish characters: \u00c7\u011e\u0130\u00d6\u015e\u00dc "
                   u"\u00e7\u011f\u0131\u00f6\u015f\u00fc\n"
                   u"  \u2022 Shape drawing (line, box, filled rect)\n"
                   u"  \u2022 Header / footer / page numbers\n";
        sub.style.size_pt = 12.0f;
        sub.style.line_spacing = 1.3f;
        rd.elements.push_back(sub);

        knst_prn::draw_command line;
        line.type = knst_prn::shape_type::Line;
        line.x1_mm = 20; line.y1_mm = 130;
        line.x2_mm = 190; line.y2_mm = 130;
        line.line_width = 0.8f;
        line.r = 100; line.g = 100; line.b = 100;
        rd.shapes.push_back(line);

        knst_prn::draw_command box;
        box.type = knst_prn::shape_type::Rect;
        box.x1_mm = 20; box.y1_mm = 150;
        box.x2_mm = 90; box.y2_mm = 200;
        box.line_width = 1.2f;
        box.r = 0; box.g = 100; box.b = 200;
        rd.shapes.push_back(box);

        knst_prn::draw_command filled;
        filled.type = knst_prn::shape_type::FilledRect;
        filled.x1_mm = 110; filled.y1_mm = 150;
        filled.x2_mm = 190; filled.y2_mm = 200;
        filled.r = 255; filled.g = 200; filled.b = 100;
        rd.shapes.push_back(filled);

        document part;
        layout_rich(rd, part);
        append_doc(combined, part);
    }

    // ── Page 3: diagnostic test page ─────────────────────────────────
    {
        document part;
        make_test_doc(knst_prn::test_page_type::Diagnostic,
                      to_u8(printer_name), w_mm, h_mm, part);
        append_doc(combined, part);
    }

    return knst_prn::status::Ok;
}


// ============================================================
// main
// ============================================================

int main() {
    const bool full_mode = do_print_and_scan();

    std::cout << "Platform : " << platform_name() << "\n";
    if (full_mode)
        std::cout << "MODE     : print + scan (KNST_PRINT_AND_SCAN set)\n";
    else
        std::cout << "MODE     : inspect only (run with KNST_PRINT_AND_SCAN=1 for print+scan)\n";
    std::cout << "SAFE     : all waits are capped ("
              << READY_WAIT_MS << " ms ready, "
              << JOB_WAIT_MS << " ms jobs).\n";

    knst_c16string sandbox = knst_file::join_path(
        knst_file::get_temp_dir(), u"knst_printer_demo");
    knst_file::create_directories(sandbox);

    // =====================================================================
    section("1) Server status");
    // =====================================================================
    {
        auto srv = knst_prn::printer::server_status();
        if (srv) {
            std::cout << "  running         : " << (srv.value.running ? "yes" : "no") << "\n";
            std::cout << "  hostname        : " << srv.value.hostname << "\n";
            std::cout << "  default printer : "
                      << (srv.value.default_printer.empty() ? "(none)" : srv.value.default_printer) << "\n";
            std::cout << "  total printers  : " << srv.value.total_printers << "\n";
        } else {
            std::cout << "  server_status(): " << srv.error_string() << "\n";
        }
    }

    // =====================================================================
    section("2) List printers");
    // =====================================================================
    knst_vector<knst_prn::printer_info_ex> printers;
    {
        auto all = knst_prn::printer::list_all();
        if (!all) std::cout << "  list_all(): " << all.error_string() << "\n";
        else {
            printers = std::move(all.value);
            std::cout << "  count: " << printers.size() << "\n";
            for (uint32_t i = 0; i < printers.size(); ++i) {
                const auto& p = printers[i];
                std::cout << "  [" << i << "] " << p.name;
                if (p.is_default) std::cout << "  [DEFAULT]";
                std::cout << "  state=" << state_name(p.state)
                          << "  enabled=" << (p.is_enabled ? "yes" : "no")
                          << "  accepting=" << (p.is_accepting ? "yes" : "no");
                if (p.pending_jobs > 0) std::cout << "  pending=" << p.pending_jobs;
                std::cout << "\n";
            }
            if (printers.empty()) std::cout << "  (no printers installed)\n";
        }
    }

    // =====================================================================
    section("3) Printer details + capabilities");
    // =====================================================================
    for (uint32_t i = 0; i < printers.size(); ++i) {
        auto pr = knst_prn::printer::open(printers[i].name);
        if (!pr) {
            std::cout << "  [" << printers[i].name << "] open: "
                      << pr.error_string() << "\n";
            continue;
        }
        std::cout << "  [" << printers[i].name << "]\n";

        auto ex = pr.value.info();
        if (ex) {
            std::cout << "    model   : " << ex.value.make_and_model << "\n";
            std::cout << "    uri     : " << ex.value.device_uri << "\n";
        }

        auto caps = pr.value.capabilities();
        if (caps) {
            std::cout << "    color   : " << (caps.value.color ? "yes" : "no") << "\n";
            std::cout << "    duplex  : ";
            if (caps.value.duplex_long_edge && caps.value.duplex_short_edge)
                std::cout << "both\n";
            else if (caps.value.duplex_long_edge)  std::cout << "long-edge only\n";
            else if (caps.value.duplex_short_edge) std::cout << "short-edge only\n";
            else                                    std::cout << "no\n";
            if (caps.value.stapling) std::cout << "    stapling: yes\n";
            if (!caps.value.supported_media.empty())
                std::cout << "    media   : " << caps.value.supported_media.size() << " supported\n";
        }
    }
    if (printers.empty()) std::cout << "  (no printers)\n";

    // =====================================================================
    section("4) Supply levels + alerts + usage");
    // =====================================================================
    if (!printers.empty()) {
        auto pr = knst_prn::printer::open(printers[0].name);
        if (pr) {
            auto s = pr.value.supply_levels();
            if (s && !s.value.empty()) {
                for (auto& sl : s.value) {
                    std::cout << "  " << sl.name;
                    if (sl.known) std::cout << ": %" << sl.percent;
                    else          std::cout << ": (no level)";
                    if (sl.low)   std::cout << "  [LOW]";
                    std::cout << "\n";
                }
            } else std::cout << "  (no supply info)\n";

            auto al = pr.value.alerts();
            if (al && !al.value.empty())
                for (auto& a : al.value)
                    std::cout << "  ALERT(" << a.severity << "): " << a.text << "\n";
            else std::cout << "  (no alerts)\n";

            auto u = pr.value.usage();
            if (u) {
                std::cout << "  usage  : total=" << u.value.total_jobs
                          << "  done=" << u.value.done_jobs
                          << "  pending=" << u.value.pending_jobs << "\n";
            }
        }
    }

    // =====================================================================
    section("5) Open ready printer (non-blocking)");
    // =====================================================================
    knst_prn::printer active;
    bool have_printer = false;
    {
        if (open_ready_printer(active)) {
            have_printer = true;
            std::cout << "  opened: " << active.name() << "\n";
            auto st = active.status();
            if (st) {
                std::cout << "  state    : " << state_name(st.value.state) << "\n";
                std::cout << "  enabled  : " << (st.value.enabled ? "yes" : "no") << "\n";
                std::cout << "  accepting: " << (st.value.accepting ? "yes" : "no") << "\n";
            }
        } else {
            std::cout << "  no ready printer found — print steps will be skipped\n";
        }
    }

    // =====================================================================
    section("6) Printer options");
    // =====================================================================
    if (have_printer) {
        auto opts = active.options();
        if (opts && !opts.value.empty()) {
            std::cout << "  count: " << opts.value.size() << "\n";
            for (uint32_t i = 0; i < opts.value.size() && i < 6; ++i) {
                const auto& po = opts.value[i];
                std::cout << "    " << po.name << " = " << po.current
                          << "  [" << po.choices.size() << " choices]\n";
            }
        } else {
            std::cout << "  (no options)\n";
        }
    }

    // =====================================================================
    section("7) Jobs (pending / completed)");
    // =====================================================================
    if (have_printer) {
        auto pending = active.jobs(false);
        if (pending) {
            std::cout << "  pending: " << pending.value.size() << "\n";
            for (uint32_t i = 0; i < pending.value.size() && i < 5; ++i) {
                const auto& j = pending.value[i];
                std::cout << "    #" << j.id
                          << "  " << job_state_name(j.state)
                          << "  " << j.title
                          << "  (" << j.size_bytes << " bytes)\n";
            }
        }
        auto done = active.completed_jobs(10);
        if (done) {
            std::cout << "  completed: " << done.value.size() << "\n";
            for (uint32_t i = 0; i < done.value.size() && i < 3; ++i) {
                const auto& j = done.value[i];
                std::cout << "    #" << j.id
                          << "  " << job_state_name(j.state)
                          << "  " << j.title << "\n";
            }
        }
    }

    // =====================================================================
    section("8) Test image generation");
    // =====================================================================
    knst_c16string ppm_path;
    {
        const uint32_t W = 320, H = 320;
        knst_byte_string rgb;
        generate_test_image(W, H, rgb);
        knst_byte_string ppm = encode_ppm(rgb, W, H);
        ppm_path = knst_file::join_path(sandbox, u"test.ppm");
        if (knst_file::write_file_data(ppm_path, ppm.data(), ppm.length()))
            std::cout << "  wrote " << ppm_path << " (" << ppm.length() << " bytes)\n";
        else
            std::cout << "  write failed\n";
    }

    // =====================================================================
    //  PRINT JOB GROUP + SCAN (runtime env check)
    // =====================================================================
    if (full_mode) {

        banner("PRINT JOB GROUP — 3 pages as ONE PDF → ONE CUPS job");

        if (!have_printer) {
            std::cout << "  (no ready printer — skipping job group)\n";
        } else {
            std::cout << "  target : " << active.name() << "\n\n";

            // ── 1) Build ONE combined 3-page PDF ──────────────────────
            knst_prn::detail::document combined;
            auto st = build_three_page_document(ppm_path, active.name(), combined);
            if (st != knst_prn::status::Ok) {
                std::cout << "  build_three_page_document(): "
                          << knst_prn::status_string(st) << "\n";
            } else {
                std::cout << "  combined document : " << combined.pages.size()
                          << " page(s), " << combined.images.size() << " image(s)\n";

                knst_c16string combined_pdf = knst_file::join_path(sandbox, u"combined.pdf");
                st = knst_prn::detail::write_pdf(combined, combined_pdf);
                if (st != knst_prn::status::Ok) {
                    std::cout << "  write_pdf(): " << knst_prn::status_string(st) << "\n";
                } else {
                    uint64_t sz = knst_file::get_file_size(combined_pdf);
                    std::cout << "  wrote combined PDF : " << sz << " bytes\n\n";

                    // ── 2) Submit ONE CUPS job for the whole group ────
                    knst_prn::print_options opt;
                    opt.title = u"knst demo group — 3 pages";
                    opt.paper = knst_prn::media_size::A4;

                    std::vector<uint32_t> job_group;
                    auto r = active.print_file(combined_pdf, opt);
                    if (r) {
                        job_group.push_back(r.value);
                        std::cout << "  submitted : ONE job (#" << r.value << ") for 3 pages\n";
                        std::cout << "\n  waiting for the job (cap " << JOB_WAIT_MS << " ms)...\n";

                        bool ok = wait_job_group(active, job_group, JOB_WAIT_MS);
                        std::cout << "  "
                                  << (ok ? "\u2713 job completed"
                                         : "\u23f3 timed out or printer offline") << "\n";
                    } else {
                        std::cout << "  print_file(): " << r.error_string() << "\n";
                    }
                }
            }
        }

        // =================================================================
        banner("SCANNER — 1 page (flatbed, 200 DPI, PNG)");
        // =================================================================
        {
            auto list = knst_prn::scanner::list_all();
            if (!list) {
                std::cout << "  scanner list: " << list.error_string() << "\n";
            } else if (list.value.empty()) {
                std::cout << "  no scanner found\n";
#if KNST_USING_PLATFORM_LINUX
                std::cout << "  hint: sudo apt install sane-utils sane-airscan\n";
#endif
            } else {
                std::cout << "  found " << list.value.size() << " scanner(s)\n";
                std::cout << "  using: " << list.value[0].name
                          << " (" << list.value[0].device_id << ")\n";

                auto sc = knst_prn::scanner::open(list.value[0].device_id);
                if (!sc) {
                    std::cout << "  open failed: " << sc.error_string() << "\n";
                } else {
                    knst_prn::scan_options so;
                    so.dpi           = 200;
                    so.mode          = knst_prn::color_mode::Color;
                    so.use_adf       = false;
                    so.source        = u"Flatbed";
                    so.output_format = u"png";

                    knst_c16string out = knst_file::join_path(sandbox, u"scan.png");
                    std::cout << "  scanning to " << out << "...\n";

                    auto r = sc.value.scan_to_file(out, so);
                    if (r) {
                        uint64_t sz = knst_file::get_file_size(r.value);
                        std::cout << "  \u2713 scanned \u2192 " << r.value
                                  << " (" << sz << " bytes)\n";
                    } else {
                        std::cout << "  scan failed: " << r.error_string() << "\n";
                    }
                }
            }
        }

    } else {
        // =================================================================
        section("9) Print + scan (skipped — run with KNST_PRINT_AND_SCAN=1)");
        // =================================================================
        {
            std::cout << "  To enable the 3-page job group + scan, run:\n";
            std::cout << "\n";
            std::cout << "    KNST_PRINT_AND_SCAN=1 ./build/bin/knst_app\n";
            std::cout << "\n";
            std::cout << "  No rebuild needed — the mode is runtime.\n";
            std::cout << "\n";
            std::cout << "  The 3 pages are combined into ONE PDF and submitted\n";
            std::cout << "  as ONE CUPS job, so the printer produces them back-to-back.\n";
        }
    }

    // =====================================================================
    section("15) PDF helpers (API demo)");
    // =====================================================================
    {
        knst_c16string test_pdf = knst_file::join_path(sandbox, u"test.pdf");
        knst_file::write_file_data(test_pdf, nullptr, 0);
        auto n = knst_prn::printer::pdf_page_count(test_pdf);
        std::cout << "  pdf_page_count(empty file) : "
                  << (n ? std::to_string(n.value) + " page(s)" : n.error_string())
                  << "  (ToolMissing = no pdfinfo, expected)\n";

        auto preview = knst_prn::printer::render_preview(test_pdf, 1, 100);
        std::cout << "  render_preview(empty file) : "
                  << (preview ? "ok" : preview.error_string()) << "\n";

        std::cout << "  (both APIs are ready — provide a real PDF to test)\n";
    }

    // =====================================================================
    section("16) Graceful failures — errors are results");
    // =====================================================================
    {
        auto r1 = knst_prn::printer::open(u"__no_such_printer__");
        std::cout << "  open(nonexistent)          : "
                  << (r1 ? "ok (unexpected!)" : r1.error_string()) << "\n";

        if (have_printer) {
            knst_prn::print_options bad;
            bad.copies = 0;
            auto r2 = active.print_text(u"x", bad);
            std::cout << "  print_text(copies=0)       : "
                      << (r2 ? "UNEXPECTED OK" : r2.error_string()) << "\n";

            auto r3 = active.job_details(99999);
            std::cout << "  job_details(bogus id)      : "
                      << (r3 ? "ok (unexpected!)" : r3.error_string()) << "\n";

            auto r4 = active.print_image(u"");
            std::cout << "  print_image(\"\")            : "
                      << (r4 ? "UNEXPECTED OK" : r4.error_string()) << "\n";

            auto r5 = active.print_file(u"/no/such/file.pdf");
            std::cout << "  print_file(nonexistent)    : "
                      << (r5 ? "UNEXPECTED OK" : r5.error_string()) << "\n";
        }
    }

    // =====================================================================
    section("17) Platform summary");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  print : winspool.drv + GDI\n";
        std::cout << "  scan  : WIA 2.0 (MSVC only)\n";
#else
        std::cout << "  print : CUPS (lp / lpstat / lpadmin)\n";
        std::cout << "  scan  : SANE (scanimage) + AirScan (sane-airscan)\n";
#endif
        if (full_mode)
            std::cout << "  mode  : print + scan (env var set)\n";
        else
            std::cout << "  mode  : inspect only\n";
    }

    // =====================================================================
    section("18) Cleanup");
    // =====================================================================
    {
        knst_prn::printer::clear_cache();
        knst_file::remove_all(sandbox);
        std::cout << "  cache cleared\n";
        std::cout << "  sandbox removed: "
                  << (knst_file::path_exists(sandbox) ? "no" : "yes") << "\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}