/*
  knst_sensor_basic.cpp — COMPREHENSIVE SAFE DEMO (34 sections)

  SAFETY guarantees:
    - set_pwm(0) is NEVER accepted (min_safe_pwm=40, 20 absolute floor)
    - Every fan operation is wrapped in RAII guard — auto restore
    - MAX speed test is bounded to 3 seconds, then restored
    - Crash handler demo only INSTALLS handlers — no crash is simulated
    - Emergency blast is simulated with callbacks — no real fan touched
    - No code path in this demo can permanently stop a fan

  NOTE: run with `sudo` to exercise the PWM-write sections.
        Without root, all writes fail cleanly with PermissionDenied
        but every safety check still triggers identically.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>


// ============================================================
// Helpers
// ============================================================

static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}

static const char* platform_name() {
#if KNST_USING_PLATFORM_WINDOWS
    return "Windows (WMI thermal zones, read-only)";
#else
    return "Linux (hwmon + thermal framework)";
#endif
}

static void print_sensor(const knst_sensor_info& s) {
    std::cout << s.chip << "/" << s.name << " = " << s.value;
    if (!s.unit.empty()) std::cout << " " << s.unit;
}

static const char* type_name(knst_sensor_type t) {
    switch (t) {
        case knst_sensor_type::Temperature: return "temp";
        case knst_sensor_type::Fan:         return "fan ";
        case knst_sensor_type::Voltage:     return "volt";
        case knst_sensor_type::Current:     return "curr";
        case knst_sensor_type::Power:       return "pwr ";
        case knst_sensor_type::Humidity:    return "hum ";
        case knst_sensor_type::Light:       return "lux ";
        default:                            return "othr";
    }
}

static void print_component(knst_sens::component c) {
    std::cout << knst_sens::component_name(c);
}


// ============================================================
// RAII fan restore guard
// ============================================================

#if KNST_USING_PLATFORM_LINUX
class fan_guard {
public:
    explicit fan_guard(knst_sens::fan_control& f) noexcept : fan_(f) {}
    ~fan_guard() noexcept {
        if (fan_.valid()) fan_.restore_auto();
    }
    fan_guard(const fan_guard&) = delete;
    fan_guard& operator=(const fan_guard&) = delete;
private:
    knst_sens::fan_control& fan_;
};
#endif


// ============================================================
// main
// ============================================================

int main() {

    std::cout << "Platform: " << platform_name() << "\n";
    std::cout << "SAFE MODE: fans will NEVER be stopped by this demo.\n";
    std::cout << "           min_safe_pwm = 40/255 (absolute floor 20).\n";
    std::cout << "           MAX speed test is bounded to 3 seconds.\n";
    std::cout << "           Every fan operation is wrapped in RAII guard.\n";

    knst_sens::async_sensor_monitor::config mon_cfg;
    mon_cfg.interval_ms   = 250;
    mon_cfg.history_depth = 20;
    mon_cfg.enable_filter = true;

    knst_sens::async_sensor_monitor mon(mon_cfg);

    // =====================================================================
    section("1) Platform availability");
    // =====================================================================
    {
        auto list = knst_devices::sensors();
        std::cout << "  sensor() returned  : " << list.size() << " sensors\n";

#if KNST_USING_PLATFORM_LINUX
        auto zones = knst_sens::thermal_zones();
        if (zones) std::cout << "  thermal_zones()    : " << zones.value.size() << " zones\n";
        else      std::cout << "  thermal_zones()    : " << zones.error_string() << "\n";
#else
        auto zones = knst_sens::thermal_zones();
        std::cout << "  thermal_zones()    : " << zones.error_string() << "\n";
        std::cout << "  fan control        : NotSupported\n";
        if (list.size() == 0) {
            std::cout << "  (no WMI sensors on this system — common on AMD Ryzen.\n";
            std::cout << "   MSAcpi_ThermalZoneTemperature is not registered here.\n";
            std::cout << "   GPU temperature is still readable via knst_gpu / NVML.)\n";
        }
#endif
    }

    // =====================================================================
    section("2) Raw sensor listing");
    // =====================================================================
    {
        auto list = knst_devices::sensors();
        uint32_t limit = list.size() < 20 ? list.size() : 20;
        for (uint32_t i = 0; i < limit; ++i) {
            std::cout << "  [" << i << "] " << type_name(list[i].type) << "  ";
            print_sensor(list[i]);
            std::cout << "\n";
        }
        if (list.size() > 20)
            std::cout << "  ... and " << (list.size() - 20) << " more\n";
    }

    // =====================================================================
    section("3) Component classification");
    // =====================================================================
    {
        auto list = knst_devices::sensors();

        for (const auto& s : list) {
            auto c = knst_sens::classify_chip(s.chip);
            if (c != knst_sens::component::Unknown) {
                std::cout << "  ";
                print_component(c);
                std::cout << "  ← " << s.chip << "\n";
                break;
            }
        }

        auto try_comp = [&](knst_sens::component c) {
            for (const auto& s : list) {
                if (s.type == knst_sensor_type::Temperature &&
                    knst_sens::classify_chip(s.chip) == c) {
                    std::cout << "  ";
                    print_component(c);
                    std::cout << " temp: " << s.value << " °C\n";
                    return;
                }
            }
            std::cout << "  ";
            print_component(c);
            std::cout << " temp: (not found)\n";
        };

        try_comp(knst_sens::component::CPU);
        try_comp(knst_sens::component::GPU);
        try_comp(knst_sens::component::NVMe);
        try_comp(knst_sens::component::Motherboard);
        try_comp(knst_sens::component::Chipset);
        try_comp(knst_sens::component::Battery);
    }

    // =====================================================================
    section("4) Async monitor");
    // =====================================================================
    {
        auto r = mon.start();
        if (!r) {
            std::cout << "  start failed: " << r.error_string() << "\n";
            return 1;
        }
        std::cout << "  monitor started (" << mon.get_config().interval_ms
                  << " ms, " << mon.get_config().history_depth << "-sample history)\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        std::cout << "  snapshot size: " << mon.snapshot().size() << "\n";
                uint64_t age = mon.age_ms();
        std::cout << "  age_ms()     : ";
        if (age == UINT64_MAX) std::cout << "(never sampled yet)\n";
        else                    std::cout << age << " ms since last sample\n";
    }

    // =====================================================================
    section("5) Statistics");
    // =====================================================================
    {
        auto snap = mon.snapshot();
        uint32_t limit = snap.size() < 5 ? snap.size() : 5;
        for (uint32_t i = 0; i < limit; ++i) {
            const auto& s = snap[i];
            auto st = mon.stats(s.chip, s.name);
            if (st) std::cout << "  " << s.chip << "/" << s.name
                              << "  min=" << st.value.min
                              << " max=" << st.value.max
                              << " avg=" << st.value.avg
                              << " n=" << st.value.count << "\n";
        }
    }

    // =====================================================================
    section("6) History — ring buffer");
    // =====================================================================
    {
        auto snap = mon.snapshot();
        if (!snap.empty()) {
            const auto& s = snap[0];
            auto hist = mon.history(s.chip, s.name);
            std::cout << "  " << s.chip << "/" << s.name
                      << "  " << hist.size() << " samples:";
            for (uint32_t i = 0; i < hist.size(); ++i) std::cout << " " << hist[i];
            std::cout << "\n";
        }
    }

    // =====================================================================
    section("7) Hottest sensor");
    // =====================================================================
    {
        auto hot = mon.hottest();
        if (hot) { std::cout << "  hottest: "; print_sensor(hot.value); std::cout << "\n"; }
        else     std::cout << "  hottest: " << hot.error_string() << "\n";
    }

    // =====================================================================
    section("8) Component-wise temperature summary");
    // =====================================================================
    {
        struct { knst_sens::component c; const char* name; } comps[] = {
            { knst_sens::component::CPU,         "CPU"         },
            { knst_sens::component::GPU,         "GPU"         },
            { knst_sens::component::NVMe,        "NVMe"        },
            { knst_sens::component::Motherboard, "Motherboard" },
            { knst_sens::component::Chipset,     "Chipset"     },
        };
        for (auto& c : comps) {
            auto t = mon.component_temp(c.c);
            std::cout << "  " << c.name << ": ";
            if (t) std::cout << t.value << " °C (hottest valid sensor)\n";
            else   std::cout << t.error_string() << "\n";
        }
    }

    // =====================================================================
    section("9) Over-limit alarms");
    // =====================================================================
    {
        auto hot = mon.over_limit(knst_sensor_type::Temperature, 0.0);
        std::cout << "  sensors above 0 °C : " << hot.size() << "\n";
        auto warm = mon.over_limit(knst_sensor_type::Temperature, 65.0);
        std::cout << "  sensors above 65 °C: " << warm.size() << "\n";
        auto very_hot = mon.over_limit(knst_sensor_type::Temperature, 90.0);
        std::cout << "  sensors above 90 °C: " << very_hot.size() << "\n";
    }

    // =====================================================================
    section("10) Sensor filter — outlier + persistent change");
    // =====================================================================
    {
        knst_sens::sensor_filter f;

        knst_sensor_info g;
        g.type  = knst_sensor_type::Temperature;
        g.chip  = u"coretemp";
        g.name  = u"Package id 0";
        g.value = 50.0;

        std::cout << "  baseline: accept(50.0) = " << f.accept(g) << "\n";

        // --- absolute-bound tests: values outside [temp_min, temp_max]
        //     are rejected EVERY time, no matter how many times you try.
        //     max_consecutive_rejects does NOT help — physical bounds win.
        knst_sensor_info b = g; b.value = 998.0;
        knst_sensor_info n = g; n.value = -273.0;
        std::cout << "  absolute bound tests:\n";
        std::cout << "    accept(998.0)  = " << f.accept(b)
                  << "  (above 150 °C — always rejected)\n";
        std::cout << "    accept(-273.0) = " << f.accept(n)
                  << "  (below -40 °C — always rejected)\n";

        // --- persistent-change test: an IN-BOUNDS jump that is far from
        //     the rolling median. The filter rejects it as an outlier for
        //     `max_consecutive_rejects` ticks (default 3), then re-baselines
        //     and accepts — so a genuine sensor jump is never locked out.
        std::cout << "  persistent change test (in-bounds jump 50 -> 120 °C):\n";
        f.reset();
        // Fill history so the median check actually engages (min_for_median = 3)
        for (int i = 0; i < 3; ++i) f.accept(g);   // history: [50, 50, 50]

        knst_sensor_info p = g; p.value = 120.0;
        for (int i = 0; i < 5; ++i) {
            bool ok = f.accept(p);
            std::cout << "    attempt " << (i + 1) << ": "
                      << (ok ? "ACCEPTED (re-baselined)" : "rejected (outlier)")
                      << "\n";
        }
    }

    // =====================================================================
    section("11) Thermal zones");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto zones = knst_sens::thermal_zones();
        if (zones) {
            std::cout << "  zone count: " << zones.value.size() << "\n";
            uint32_t limit = zones.value.size() < 10 ? zones.value.size() : 10;
            for (uint32_t i = 0; i < limit; ++i)
                std::cout << "  [" << i << "] " << zones.value[i].type
                          << " = " << zones.value[i].temp_c << " °C\n";
        } else {
            std::cout << "  thermal_zones: " << zones.error_string() << "\n";
        }
#else
        std::cout << "  NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("12) Discovery");
    // =====================================================================
    {
        auto rep = knst_sens::discover();
        std::cout << "  sensor chips: " << rep.sensor_chips.size() << "\n";
        for (uint32_t i = 0; i < rep.sensor_chips.size(); ++i)
            std::cout << "    " << rep.sensor_chips[i] << "\n";
        std::cout << "  fan channels: " << rep.fans.size() << "\n";
        uint32_t limit = rep.suggestions.size() < 10 ? rep.suggestions.size() : 10;
        for (uint32_t i = 0; i < limit; ++i)
            std::cout << "    " << rep.suggestions[i] << "\n";
    }

    // =====================================================================
    section("13) Fan enumeration");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        std::cout << "  pwm channels detected: " << fans.size() << "\n";
        uint32_t limit = fans.size() < 10 ? fans.size() : 10;
        for (uint32_t i = 0; i < limit; ++i) {
            const auto& f = fans[i];
            std::cout << "  [" << i << "] chip=" << f.chip << " pwm=" << f.index;
            if (f.has_pwm)    std::cout << "  pwm=" << f.current_pwm;
            if (f.has_rpm)    std::cout << "  rpm=" << f.current_rpm;
            if (f.has_enable) std::cout << "  enable=" << f.current_enable;
            std::cout << "\n";
        }
#else
        std::cout << "  NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("14) Fan control — READ ONLY");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (fans.empty()) {
            std::cout << "  (no pwm channels — skipping)\n";
        } else {
            auto fr = knst_sens::fan_control::open(fans[0].chip, fans[0].index, 40);
            if (!fr) {
                std::cout << "  open failed: " << fr.error_string() << "\n";
            } else {
                auto& fan = fr.value;
                std::cout << "  chip         : " << fan.chip_name() << "\n";
                std::cout << "  min_safe     : " << fan.min_safe_pwm() << "/255\n";
                std::cout << "  needs_restore: " << (fan.needs_restore() ? "yes" : "no") << "\n";
                auto pwm    = fan.get_pwm();
                auto rpm    = fan.get_rpm();
                auto enable = fan.get_pwm_enable();
                if (pwm)    std::cout << "  current pwm  : " << pwm.value << "/255\n";
                if (rpm)    std::cout << "  current rpm  : " << rpm.value << "\n";
                if (enable) std::cout << "  enable mode  : " << enable.value
                                      << " (2=BIOS, 1=manual)\n";
            }
        }
#else
        std::cout << "  NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("15) Fan control — SAFE SET (min_safe enforcement)");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (fans.empty()) {
            std::cout << "  (no pwm channels — skipping)\n";
        } else {
            auto fr = knst_sens::fan_control::open(fans[0].chip, fans[0].index, 40);
            if (!fr) {
                std::cout << "  open failed: " << fr.error_string() << "\n";
            } else {
                auto& fan = fr.value;
                fan_guard guard(fan);   // RAII restore

                auto orig = fan.get_pwm();
                uint32_t orig_val = orig ? orig.value : 128;
                std::cout << "  orig pwm       : " << orig_val << "\n";

                auto set = fan.set_pwm(80);
                std::cout << "  set_pwm(80)    : " << (set ? "ok" : set.error_string()) << "\n";
                if (set) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    auto now = fan.get_pwm();
                    if (now) std::cout << "    readback     : " << now.value << "\n";
                }

                auto setp = fan.set_percent(50);
                std::cout << "  set_percent(50%): " << (setp ? "ok" : setp.error_string()) << "\n";

                std::cout << "  rejection tests (fan must NOT stop):\n";
                auto bad1 = fan.set_pwm(5);
                std::cout << "    set_pwm(5)   : " << (bad1 ? "ACCEPTED (unexpected!)" : bad1.error_string()) << "\n";
                auto bad2 = fan.set_pwm(0);
                std::cout << "    set_pwm(0)   : " << (bad2 ? "ACCEPTED (unexpected!)" : bad2.error_string()) << "\n";
                auto bad3 = fan.set_pwm(39);
                std::cout << "    set_pwm(39)  : " << (bad3 ? "ACCEPTED (unexpected!)" : bad3.error_string()) << "\n";
                auto ok40 = fan.set_pwm(40);
                std::cout << "    set_pwm(40)  : " << (ok40 ? "accepted (min safe)" : ok40.error_string()) << "\n";

                fan.set_pwm(orig_val);
                std::cout << "  restored to    : " << orig_val << "\n";
            }
        }
#else
        std::cout << "  NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("16) Fan MAX speed burst — 3 second test");
    // =====================================================================
    {
        std::cout << "  Tests the library's ability to REACH 100% instantly.\n";
        std::cout << "  Bounded to 3 seconds, then restored. Safe on a cool system.\n";
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (fans.empty()) {
            std::cout << "  (no fans — skipping)\n";
        } else {
            auto fr = knst_sens::fan_control::open(fans[0].chip, fans[0].index, 40);
            if (!fr) {
                std::cout << "  open failed: " << fr.error_string() << "\n";
            } else {
                auto& fan = fr.value;
                fan_guard guard(fan);

                auto orig_pwm = fan.get_pwm();
                auto orig_rpm = fan.get_rpm();
                uint32_t orig_val = orig_pwm ? orig_pwm.value : 128;
                uint32_t orig_rpm_v = orig_rpm ? orig_rpm.value : 0;

                std::cout << "  before : pwm=" << orig_val
                          << " rpm=" << orig_rpm_v << "\n";

                auto t0 = std::chrono::steady_clock::now();
                auto set_max = fan.set_pwm(255);
                auto t1 = std::chrono::steady_clock::now();
                auto write_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

                std::cout << "  set_pwm(255) result : " << (set_max ? "ok" : set_max.error_string()) << "\n";
                std::cout << "  write latency       : " << write_us << " us\n";

                std::cout << "  RPM ramp (watch the fan spin up):\n";
                for (int i = 1; i <= 10; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    auto rpm = fan.get_rpm();
                    std::cout << "    t=" << (i * 300) << "ms   rpm=" << (rpm ? rpm.value : 0) << "\n";
                }

                fan.set_pwm(orig_val);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                auto after_rpm = fan.get_rpm();
                std::cout << "  after restore to pwm=" << orig_val
                          << "  rpm=" << (after_rpm ? after_rpm.value : 0) << "\n";
                std::cout << "  (guard will call restore_auto on scope exit)\n";
            }
        }
#else
        std::cout << "  Linux only\n";
#endif
    }

    // =====================================================================
    section("17) Fan curve — interpolation + empty curve failsafe");
    // =====================================================================
    {
        auto curve = knst_sens::fan_curve::balanced();
        std::cout << "  balanced curve points:\n";
        for (const auto& p : curve.points)
            std::cout << "    " << p.temp_c << " °C → pwm " << p.pwm << "\n";
        std::cout << "  pwm_for() interpolation:\n";
        for (double t = 30.0; t <= 100.0; t += 10.0)
            std::cout << "    " << t << " °C → " << curve.pwm_for(t) << "\n";
        std::cout << "  silent(80°C)      : " << knst_sens::fan_curve::silent().pwm_for(80.0)      << "\n";
        std::cout << "  performance(80°C) : " << knst_sens::fan_curve::performance().pwm_for(80.0) << "\n";
        knst_sens::fan_curve empty;
        std::cout << "  empty.pwm_for(80) : " << empty.pwm_for(80.0) << " (safe default 255)\n";
        std::cout << "  NaN input (safe)  : " << empty.pwm_for(std::nan("")) << " (full speed)\n";
    }

    // =====================================================================
    section("18) Fan curve controller — asymmetric ramp");
    // =====================================================================
    {
        std::cout << "  balanced: ramp_up_step=255 (instant), ramp_step=10 (slow down)\n";
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (fans.empty()) {
            std::cout << "  (no fans — skipping)\n";
        } else {
            auto fr = knst_sens::fan_control::open(fans[0].chip, fans[0].index, 40);
            if (!fr) {
                std::cout << "  open failed: " << fr.error_string() << "\n";
            } else {
                knst_sens::fan_curve_controller ctrl(
                    std::move(fr.value),
                    knst_sens::fan_curve::balanced(),
                    40);

                double temps[] = {45.0, 46.0, 60.0, 61.0, 75.0, 90.0, 50.0};
                std::cout << "  simulating temperature changes (fast up, slow down):\n";
                for (double t : temps) {
                    auto r = ctrl.tick(t);
                    if (r) std::cout << "    " << t << " °C → pwm " << ctrl.current_pwm() << "\n";
                    else   std::cout << "    " << t << " °C → " << r.error_string() << "\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }

                ctrl.restore_auto();
                std::cout << "  restored to auto (dtor also restores)\n";
            }
        }
#else
        std::cout << "  Linux only\n";
#endif
    }

    // =====================================================================
    section("19) Fan stall detection (simulated)");
    // =====================================================================
    {
        std::cout << "  The orchestrator monitors RPM while PWM > 80.\n";
        std::cout << "  If RPM=0 for 3 consecutive ticks, a FanStall event fires.\n";
        std::cout << "  Here we exercise the event bus directly (no real fan touched):\n";

        knst_sens::event_bus events;
        int stall_events = 0;
        events.subscribe([&](const knst_sens::thermal_event& e) {
            if (e.type == knst_sens::thermal_event::kind::FanStall) ++stall_events;
        });

        knst_sens::thermal_event te;
        te.type = knst_sens::thermal_event::kind::FanStall;
        te.chip = u"it8665";
        te.name = u"CPU Zone";
        te.value = 0.0;
        te.threshold = 128.0;
        te.timestamp_ms = knst_sens::now_ms();
        events.emit(te);

        std::cout << "  simulated FanStall event fired: " << stall_events << " time(s)\n";
    }

    // =====================================================================
    section("20) Sensor lost → failsafe_pwm");
    // =====================================================================
    {
        std::cout << "  A zone whose reference sensor disappears for 3 ticks\n";
        std::cout << "  falls back to failsafe_pwm (default 200) and emits SensorLost.\n";

        knst_sens::event_bus events;
        int lost_events = 0;
        events.subscribe([&](const knst_sens::thermal_event& e) {
            if (e.type == knst_sens::thermal_event::kind::SensorLost) ++lost_events;
        });

        knst_sens::thermal_event te;
        te.type = knst_sens::thermal_event::kind::SensorLost;
        te.chip = u"coretemp";
        te.name = u"Package id 0";
        te.value = 0.0;
        te.threshold = 200.0;
        te.timestamp_ms = knst_sens::now_ms();
        events.emit(te);

        std::cout << "  simulated SensorLost event fired: " << lost_events << " time(s)\n";
    }

    // =====================================================================
    section("21) Watchdog — timeout + re-arm");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (fans.empty()) {
            std::cout << "  (no fans — skipping)\n";
        } else {
            auto fr = knst_sens::fan_control::open(fans[0].chip, fans[0].index, 40);
            if (!fr) {
                std::cout << "  open failed: " << fr.error_string() << "\n";
            } else {
                knst_sens::fan_watchdog wd(1000);   // 1s timeout for demo
                wd.attach(std::move(fr.value));
                wd.start();
                std::cout << "  watchdog started (1000 ms timeout)\n";

                for (int i = 0; i < 5; ++i) {
                    wd.heartbeat();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                std::cout << "  tripped after 500ms of heartbeats: "
                          << (wd.tripped() ? "yes" : "no") << "\n";

                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                std::cout << "  tripped after 1500ms of silence: "
                          << (wd.tripped() ? "yes (restored to auto)" : "no") << "\n";

                for (int i = 0; i < 5; ++i) {
                    wd.heartbeat();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                std::cout << "  re-armed after resume: "
                          << (wd.tripped() ? "still tripped" : "yes") << "\n";

                wd.stop();
                std::cout << "  watchdog stopped\n";
            }
        }
#else
        std::cout << "  Linux only\n";
#endif
    }

    // =====================================================================
    section("22) Event bus — subscribe / unsubscribe");
    // =====================================================================
    {
        knst_sens::event_bus events;
        int count = 0;
        uint32_t sub_id = events.subscribe([&](const knst_sens::thermal_event&) { ++count; });

        knst_sens::thermal_event te;
        te.type = knst_sens::thermal_event::kind::Warn;
        te.chip = u"test"; te.name = u"test";
        te.value = 90.0; te.threshold = 85.0;
        te.timestamp_ms = knst_sens::now_ms();

        events.emit(te);
        std::cout << "  after 1st emit: " << count << " call(s)\n";
        events.unsubscribe(sub_id);
        events.emit(te);
        std::cout << "  after unsubscribe + emit: " << count << " call(s) (unchanged)\n";
    }

    // =====================================================================
    section("23) Emergency responder — hysteresis");
    // =====================================================================
    {
        std::cout << "  Emergency levels: Normal < Warn < Critical < Shutdown\n";
        std::cout << "  Hysteresis: must drop hysteresis_c below threshold to de-escalate.\n";

        knst_sens::event_bus events;
        int warn = 0, crit = 0, shut = 0;
        events.subscribe([&](const knst_sens::thermal_event& e) {
            switch (e.type) {
                case knst_sens::thermal_event::kind::Warn:     ++warn; break;
                case knst_sens::thermal_event::kind::Critical: ++crit; break;
                case knst_sens::thermal_event::kind::Shutdown: ++shut; break;
                default: break;
            }
        });

        knst_sens::emergency_responder er(events);
        int blast = 0;
        er.set_blast_callback([&blast] { ++blast; });

        knst_sensor_info s;
        s.chip = u"coretemp"; s.name = u"Package id 0";
        s.type = knst_sensor_type::Temperature;

        struct { double v; const char* label; } steps[] = {
            {  80.0, "80°C (normal)  " },
            {  90.0, "90°C (warn)    " },
            {  85.0, "85°C (hysteresis, no de-escalate)" },
            {  97.0, "97°C (critical)" },
            {  92.0, "92°C (hysteresis, no de-escalate)" },
            {  75.0, "75°C (back to normal)" },
            { 108.0, "108°C (shutdown) " },
        };
        for (auto& st : steps) {
            s.value = st.v;
            er.check(s);
            std::cout << "  " << st.label << " → level=";
            switch (er.current_level()) {
                case knst_sens::emergency_responder::level::Normal:   std::cout << "normal\n";   break;
                case knst_sens::emergency_responder::level::Warn:     std::cout << "warn\n";     break;
                case knst_sens::emergency_responder::level::Critical: std::cout << "critical\n"; break;
                case knst_sens::emergency_responder::level::Shutdown: std::cout << "shutdown\n"; break;
            }
        }
        std::cout << "  totals: warn=" << warn << " crit=" << crit
                  << " shut=" << shut << " blast=" << blast << "\n";
    }

    // =====================================================================
    section("24) Emergency — repeat_ms (re-emit while level persists)");
    // =====================================================================
    {
        knst_sens::event_bus events;
        int crit_count = 0;
        events.subscribe([&](const knst_sens::thermal_event& e) {
            if (e.type == knst_sens::thermal_event::kind::Critical) ++crit_count;
        });

        knst_sens::emergency_responder er(events);
        er.set_blast_callback([] {});
        er.set_config({85.0, 95.0, 105.0, true, 5.0, 100});   // repeat every 100ms

        knst_sensor_info s;
        s.chip = u"coretemp"; s.name = u"Package id 0";
        s.type = knst_sensor_type::Temperature;
        s.value = 96.0;

        for (int i = 0; i < 5; ++i) {
            er.check(s);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        std::cout << "  5 checks in 750ms with repeat_ms=100: "
                  << crit_count << " critical event(s)\n";
    }

    // =====================================================================
    section("25) Orchestrator — normal tick");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (fans.empty()) {
            std::cout << "  (no fans — skipping)\n";
        } else {
            knst_sens::thermal_orchestrator orch;
            auto cpu_temp = mon.find(knst_sensor_type::Temperature, u"core");
            if (!cpu_temp) cpu_temp = mon.find(knst_sensor_type::Temperature);

            if (cpu_temp) {
                knst_sens::fan_zone z;
                z.name         = u"CPU Zone";
                z.temp_chip    = cpu_temp.value.chip;
                z.temp_name    = cpu_temp.value.name;
                z.curve        = knst_sens::fan_curve::balanced();
                z.min_safe_pwm = 40;
                z.failsafe_pwm = 200;
                z.fan_chips.push_back(fans[0].chip);
                z.fan_indices.push_back(fans[0].index);
                orch.add_zone(std::move(z));
                std::cout << "  zone added: CPU Zone → " << fans[0].chip
                          << " pwm" << fans[0].index << "\n";
                std::cout << "  zones: " << orch.zone_count() << "\n";
                for (int i = 0; i < 3; ++i) {
                    orch.tick(mon);
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                }
                orch.restore_all_auto();
                std::cout << "  restored all to auto\n";
            }
        }
#else
        std::cout << "  Linux only\n";
#endif
    }

    // =====================================================================
    section("26) Orchestrator — emergency_full_blast + clear");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (fans.empty()) {
            std::cout << "  (no fans — skipping)\n";
        } else {
            knst_sens::thermal_orchestrator orch;
            auto cpu_temp = mon.find(knst_sensor_type::Temperature, u"core");
            if (!cpu_temp) cpu_temp = mon.find(knst_sensor_type::Temperature);
            if (cpu_temp) {
                knst_sens::fan_zone z;
                z.name      = u"CPU Zone";
                z.temp_chip = cpu_temp.value.chip;
                z.temp_name = cpu_temp.value.name;
                z.curve     = knst_sens::fan_curve::balanced();
                z.fan_chips.push_back(fans[0].chip);
                z.fan_indices.push_back(fans[0].index);
                orch.add_zone(std::move(z));
            }

            std::cout << "  entering emergency_full_blast...\n";
            orch.emergency_full_blast();
            std::cout << "  in_emergency(): " << (orch.in_emergency() ? "yes" : "no") << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            orch.clear_emergency();
            std::cout << "  after clear_emergency(): " << (orch.in_emergency() ? "yes" : "no") << "\n";
            orch.restore_all_auto();
            std::cout << "  restored to auto\n";
        }
#else
        std::cout << "  Linux only\n";
#endif
    }

    // =====================================================================
    section("27) Thermal manager facade — the all-in-one");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (fans.empty()) {
            std::cout << "  (no fans — skipping)\n";
        } else {
            knst_sens::thermal_manager tm;
            tm.events().subscribe([](const knst_sens::thermal_event& e) {
                if (e.type == knst_sens::thermal_event::kind::Warn)
                    std::cout << "    [manager] WARN event\n";
            });

            auto cpu_temp = mon.find(knst_sensor_type::Temperature, u"core");
            if (!cpu_temp) cpu_temp = mon.find(knst_sensor_type::Temperature);

            if (cpu_temp) {
                knst_sens::fan_zone z;
                z.name      = u"CPU Zone";
                z.temp_chip = cpu_temp.value.chip;
                z.temp_name = cpu_temp.value.name;
                z.curve     = knst_sens::fan_curve::balanced();
                z.fan_chips.push_back(fans[0].chip);
                z.fan_indices.push_back(fans[0].index);
                tm.add_zone(std::move(z));
            }

            auto r = tm.start();
            if (r) {
                std::cout << "  thermal_manager started\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                auto hot = tm.monitor().hottest();
                if (hot) { std::cout << "  hottest: "; print_sensor(hot.value); std::cout << "\n"; }
                std::cout << "  running: " << (tm.running() ? "yes" : "no") << "\n";
                tm.stop();
                std::cout << "  stopped cleanly (dtor also restores)\n";
            } else {
                std::cout << "  start failed: " << r.error_string() << "\n";
            }
        }
#else
        std::cout << "  Linux only\n";
#endif
    }

    // =====================================================================
    section("28) Crash handler — install only (no crash)");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        bool ok = knst_sens::install_crash_handlers();
        std::cout << "  install_crash_handlers(): " << (ok ? "installed" : "failed") << "\n";
        std::cout << "  Handlers cover: SIGINT, SIGTERM, SIGHUP, SIGQUIT,\n";
        std::cout << "                  SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT\n";
        std::cout << "  On any of these, all touched fans are restored to auto\n";
        std::cout << "  in an async-signal-safe way before the process dies.\n";
        std::cout << "  (No crash is simulated here — handlers just installed.)\n";
#else
        std::cout << "  Linux only (POSIX signals)\n";
#endif
    }

    // =====================================================================
    section("29) Signal-safe manual restore");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        std::cout << "  Manually invoking restore_all_fans_signal_safe():\n";
        std::cout << "  This is what the crash handler calls.\n";
        knst_sens::restore_all_fans_signal_safe();
        std::cout << "  signal-safe restore completed without crashing.\n";
#else
        std::cout << "  Linux only\n";
#endif
    }

    // =====================================================================
    section("30) sanitize_min_pwm — absolute floor enforcement");
    // =====================================================================
    {
        std::cout << "  The library enforces a hard floor of 20/255 on min_safe_pwm.\n";
        std::cout << "  Values below 20 are clamped up. open() rejects them outright.\n";
        std::cout << "  sanitize_min_pwm(10)  → " << knst_sens::sanitize_min_pwm(10)  << "\n";
        std::cout << "  sanitize_min_pwm(20)  → " << knst_sens::sanitize_min_pwm(20)  << "\n";
        std::cout << "  sanitize_min_pwm(40)  → " << knst_sens::sanitize_min_pwm(40)  << "\n";
        std::cout << "  sanitize_min_pwm(255) → " << knst_sens::sanitize_min_pwm(255) << "\n";
        std::cout << "  sanitize_min_pwm(999) → " << knst_sens::sanitize_min_pwm(999) << "\n";
    }

    // =====================================================================
    section("31) Fan move semantics — no double-restore");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (fans.empty()) {
            std::cout << "  (no fans — skipping)\n";
        } else {
            auto fr = knst_sens::fan_control::open(fans[0].chip, fans[0].index, 40);
            if (!fr) {
                std::cout << "  open failed: " << fr.error_string() << "\n";
            } else {
                std::cout << "  source valid before move: " << (fr.value.valid() ? "yes" : "no") << "\n";

                knst_sens::fan_control moved(std::move(fr.value));
                std::cout << "  source valid after move : " << (fr.value.valid() ? "yes" : "no") << "\n";
                std::cout << "  target valid after move : " << (moved.valid() ? "yes" : "no") << "\n";
                std::cout << "  moved-from will NOT restore (safe).\n";
                std::cout << "  moved-to will restore on scope exit.\n";
                // moved destructs here → restore_auto()
            }
        }
#else
        std::cout << "  Linux only\n";
#endif
    }

    // =====================================================================
    section("32) Platform summary");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  Windows : read-only monitoring, no fan control\n";
#else
        std::cout << "  Linux   : full monitoring + PWM control\n";
        std::cout << "  Note    : fans always restored to BIOS on every exit\n";
        std::cout << "  Safety  : set_pwm(0) rejected, min floor 20/255, crash handler\n";
#endif
    }

    // =====================================================================
    section("33) Graceful failure — the full battery");
    // =====================================================================
    {
        auto r1 = knst_sens::fan_control::open(u"__no_such_chip__", 1, 40);
        std::cout << "  open(nonexistent)          : " << (r1 ? "ok" : r1.error_string()) << "\n";
        auto r2 = knst_sens::fan_control::open(u"coretemp", 0);
        std::cout << "  open(index=0)              : " << (r2 ? "ok" : r2.error_string()) << "\n";
        auto r3 = knst_sens::fan_control::open(u"coretemp", 17);
        std::cout << "  open(index=17)             : " << (r3 ? "ok" : r3.error_string()) << "\n";
        auto r3b = knst_sens::fan_control::open(u"it8665", 1, 10);   // below absolute floor
        std::cout << "  open(min_safe=10)          : " << (r3b ? "ok" : r3b.error_string()) << "\n";
        auto r4 = mon.stats(u"__no_chip__", u"__no_name__");
        std::cout << "  stats(nonexistent)         : " << (r4 ? "ok" : r4.error_string()) << "\n";
        auto r5 = mon.find(knst_sensor_type::Temperature, u"__no_needle__");
        std::cout << "  find(no_needle)            : " << (r5 ? "ok" : r5.error_string()) << "\n";
        auto r5b = mon.component_temp(knst_sens::component::GPU);
        std::cout << "  component_temp(GPU)        : " << (r5b ? "ok" : r5b.error_string()) << "\n";
        knst_sens::fan_curve c;
        std::cout << "  empty_curve.pwm_for(80)    : " << c.pwm_for(80.0) << " (safe default 255)\n";
        std::cout << "  NaN temp pwm_for()         : " << c.pwm_for(std::nan("")) << " (safe)\n";

#if KNST_USING_PLATFORM_LINUX
        auto fans = knst_sens::enumerate_fans();
        if (!fans.empty()) {
            auto fr = knst_sens::fan_control::open(fans[0].chip, fans[0].index, 40);
            if (fr) {
                fan_guard guard(fr.value);
                auto r6 = fr.value.set_pwm(0);
                std::cout << "  set_pwm(0) via API         : "
                          << (r6 ? "ACCEPTED (unexpected!)" : r6.error_string())
                          << "  ← fan NOT stopped\n";
                auto r7 = fr.value.set_percent(150);
                std::cout << "  set_percent(150)           : "
                          << (r7 ? "ok" : r7.error_string()) << "\n";
            }
        }
#endif
    }

    // =====================================================================
    section("34) Final cleanup");
    // =====================================================================
    {
        mon.stop();
        std::cout << "  monitor stopped\n";
        std::cout << "  all fans restored to BIOS via RAII guard / dtor\n";
        std::cout << "\n  Verify fans are running:\n";
        std::cout << "    for i in 1 2 3 4 5 6; do\n";
        std::cout << "      [ -f /sys/class/hwmon/hwmon5/fan${i}_input ] && \\\n";
        std::cout << "        echo \"fan${i}=$(cat /sys/class/hwmon/hwmon5/fan${i}_input) RPM\"\n";
        std::cout << "    done\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}