// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_sensors.hpp
----------------------------
    thermal management on top of knst_devices::sensors().

    SAFETY DESIGN — READ THIS FIRST
    ================================
    Fan control can permanently stop a fan if misused. This library
    follows strict safety rules:

    1. set_pwm(value) REJECTS value < min_safe_pwm (default 40/255).
       - Including value=0. There is no allow_stop flag.
       - min_safe_pwm itself can never be configured below
         KNST_SENS_ABSOLUTE_MIN_PWM (default 20). open() rejects such
         values, controllers/zones clamp them.
       - set_pwm_unsafe() is the only bypass and is [[deprecated]].

    2. restore_auto() is SAFE:
       - Never lowers the fan below its present speed
         (writes max(min_safe_pwm, 128, current_pwm)).
       - Switches pwmN_enable back to the mode the fan had when it was
         opened (2..5, e.g. 5 = SmartFan IV on nct67xx); falls back to 2.
       - Verifies the mode (retries once) and reports failure honestly.

    3. fan_control only touches hardware when you ask it to. Its destructor
       restores the fan ONLY if this object changed it ("dirty").
       Moved-from objects never restore (no double / premature restore).

    4. fan_curve_controller / thermal_orchestrator / thermal_manager restore
       on destruction.

    5. fan_watchdog restores all attached fans if the heartbeat stops, and
       re-arms itself when the heartbeat comes back.

    6. Fail-safe behaviour:
       - Sensor missing / stale / out of range for 3 ticks -> zone runs at
         failsafe_pwm (default 200) and a SensorLost event is emitted.
       - Critical temperature -> all zones forced to 255 until the temp
         drops below (critical - hysteresis).
       - NaN temperature -> 255.
       - Empty fan curve -> 255.

    7. Crash safety (opt-in): install_crash_handlers() installs
       async-signal-safe handlers (SIGINT/SIGTERM/SIGHUP/SIGSEGV/SIGABRT/...)
       that put every fan this process touched back to auto before the
       process dies. SIGKILL and power loss cannot be caught by any
       library — for those, run under systemd with an ExecStopPost= that
       writes the fans back to auto.

    Curve behaviour:
       - Temperature hysteresis only delays COOLING DOWN. Rising
         temperature is always followed immediately.
       - ramp_up_step (default 255 = immediate) limits speeding up,
         ramp_step limits slowing down. 0 means "no limit".
       - Fan keeps ramping toward the target on every tick (the old
         "skip when temperature barely changed" logic could leave a fan
         stuck halfway during a ramp).

    Threading: event_bus handlers run on the sampling / manager thread —
    keep them short and never call stop() on the owner from a handler.

    Layers:
      sensor_filter  outlier rejection
      async_sensor_monitor      thread-safe periodic sampling + history
      fan_control   PWM/RPM/manual — Windows stub
      fan_curve / controller interpolation + hysteresis + ramp
      thermal_orchestrator      zone-based coordination
      fan_watchdog       crash-safe auto-restore
      event_bus / emergency     reactive alarms
      thermal_manager      all-in-one facade (Linux)
      discover()    chip/fan enumeration
*/

#pragma once


#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <functional>
#include <initializer_list>
#include <utility>

#if KNST_USING_PLATFORM_LINUX
    #include <dirent.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <sys/stat.h>
    #include <unistd.h>

#endif



// macros
#ifndef KNST_SENS_DEFAULT_INTERVAL_MS
#define KNST_SENS_DEFAULT_INTERVAL_MS 1000
#endif


#ifndef KNST_SENS_DEFAULT_HISTORY_DEPTH
#define KNST_SENS_DEFAULT_HISTORY_DEPTH 300
#endif


#ifndef KNST_SENS_DEFAULT_MIN_SAFE_PWM
#define KNST_SENS_DEFAULT_MIN_SAFE_PWM 40
#endif


#ifndef KNST_SENS_ABSOLUTE_MIN_PWM
#define KNST_SENS_ABSOLUTE_MIN_PWM 20
#endif


#ifndef KNST_SENS_RESTORE_PWM
#define KNST_SENS_RESTORE_PWM 128
#endif

namespace knst_sens {

using knst_dev::status;
using knst_dev::result;

// ============================================================
// Basic types
// ============================================================

struct sensor_stats {
    double min = 0.0;
    double max = 0.0;
    double avg = 0.0;
    double last = 0.0;
    uint32_t count = 0;
};

struct alarm {
    knst_c16string chip;
    knst_c16string name;
    double value = 0.0;
    double limit = 0.0;
};

struct thermal_event {
    enum class kind : uint8_t {
        Warn, Critical, Shutdown, SensorLost, FanStall
    } type = kind::Warn;

    knst_c16string chip;
    knst_c16string name;
    double value = 0.0;
    double threshold = 0.0;
    uint64_t timestamp_ms = 0;
};

inline uint64_t now_ms() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

inline uint64_t steady_ms() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()) + 1;
}

// Clamp a user supplied minimum PWM into [ABSOLUTE_MIN, 255].
inline uint32_t sanitize_min_pwm(uint32_t v) noexcept {
    if (v < static_cast<uint32_t>(KNST_SENS_ABSOLUTE_MIN_PWM))
        v = static_cast<uint32_t>(KNST_SENS_ABSOLUTE_MIN_PWM);
    if (v > 255) v = 255;
    return v;
}

// ============================================================
// Component classification
// ============================================================

enum class component : uint8_t {
    CPU, GPU, NVMe, Chipset, Motherboard, Battery, PSU, Network, Unknown
};

inline knst_c16string component_name(component c) noexcept {
    switch (c) {
        case component::CPU: return knst_c16string(u"CPU");
        case component::GPU: return knst_c16string(u"GPU");
        case component::NVMe: return knst_c16string(u"NVMe");
        case component::Chipset: return knst_c16string(u"Chipset");
        case component::Motherboard: return knst_c16string(u"Motherboard");
        case component::Battery: return knst_c16string(u"Battery");
        case component::PSU: return knst_c16string(u"PSU");
        case component::Network: return knst_c16string(u"Network");
        default: return knst_c16string(u"Unknown");
    }
}

inline component classify_chip(const knst_c16string& chip) noexcept {
    if (chip == u"coretemp" || chip == u"k10temp" || chip == u"zenpower"
     || chip == u"cpu_thermal" || chip == u"cpu-thermal" || chip == u"k8temp")
        return component::CPU;

    if (chip == u"amdgpu" || chip == u"nouveau" || chip == u"nvidia"
     || chip == u"radeon" || chip == u"i915" || chip == u"xe")
        return component::GPU;

    if (chip.starts_with(u"nvme"))
        return component::NVMe;

    if (chip.starts_with(u"nct") || chip.starts_with(u"it")
     || chip.starts_with(u"f71") || chip.starts_with(u"w83")
     || chip == u"asus" || chip == u"gigabyte")
        return component::Motherboard;
    if (chip.starts_with(u"pch"))

        return component::Chipset;
    if (chip.starts_with(u"bat") || chip == u"acpi")
        return component::Battery;
    if (chip.starts_with(u"iwl") || chip.starts_with(u"ath")
     || chip.starts_with(u"rtw") || chip.starts_with(u"mt"))
        return component::Network;
    return component::Unknown;
}

// ============================================================
// sensor_filter — outlier rejection
// ============================================================

class sensor_filter {
public:
    struct config {
        double temp_min  = -40.0,  temp_max  = 150.0;
        double fan_min   = 0.0,    fan_max   = 20000.0;
        double volt_min  = -30.0,  volt_max  = 30.0;
        double curr_min  = -100.0, curr_max  = 100.0;
        double power_min = 0.0,   power_max = 2000.0;


        uint32_t window  = 5;

        double   deviation_ratio = 0.5;
        uint32_t min_for_median  = 3;
        // A value rejected this many times in a row is a real change, not
        // an outlier: accept it and restart the history. Without this a
        // sensor could lock itself out forever after a genuine jump.
        uint32_t max_consecutive_rejects = 3;
    };

    sensor_filter() noexcept = default;
    explicit sensor_filter(config c) noexcept : cfg_(c) {}

    bool accept(const knst_sensor_info& s) noexcept {
        const double v = s.value;
        if (!std::isfinite(v)) return false;

        switch (s.type) {
            case knst_sensor_type::Temperature:
                if (v < cfg_.temp_min || v > cfg_.temp_max) return false;
                break;

            case knst_sensor_type::Fan:
                if (v < cfg_.fan_min || v > cfg_.fan_max) return false;
                break;

            case knst_sensor_type::Voltage:
                if (v < cfg_.volt_min || v > cfg_.volt_max) return false;
                break;

            case knst_sensor_type::Current:
                if (v < cfg_.curr_min || v > cfg_.curr_max) return false;
                break;

            case knst_sensor_type::Power:
                if (v < cfg_.power_min || v > cfg_.power_max) return false;
                break;
                
            default: break;
        }

        hist_entry* h = find_or_create(s.chip, s.name);
        const uint32_t need = cfg_.min_for_median < 1 ? 1u : cfg_.min_for_median;
        if (h->values.size() >= need) {
            const double med = median(h->values);
            const double dev = std::fabs(v - med);
            const double tol = std::max(2.0, std::fabs(med) * cfg_.deviation_ratio);
            if (dev > tol) {
                if (++h->rejects < cfg_.max_consecutive_rejects) return false;
                h->values.clear();          // persistent change: re-baseline
            }
        }
        h->rejects = 0;

        h->values.push_back(v);
        const uint32_t win = cfg_.window < 1 ? 1u : cfg_.window;
        while (h->values.size() > win) h->values.erase(0);
        return true;
    }

    void reset() noexcept { hist_.clear(); }

private:
    struct hist_entry {
        knst_c16string chip, name;
        knst_vector<double> values;
        uint32_t rejects = 0;
    };

    hist_entry* find_or_create(const knst_c16string& chip,
                               const knst_c16string& name) {
        for (auto& h : hist_)
            if (h.chip == chip && h.name == name) return &h;
        hist_entry e;
        e.chip = chip;
        e.name = name;
        hist_.push_back(std::move(e));
        return &hist_.back();
    }

    static double median(knst_vector<double> v) {
        for (uint32_t i = 1; i < v.size(); ++i) {
            double key = v[i];
            int j = static_cast<int>(i) - 1;
            while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; --j; }
            v[j + 1] = key;
        }
        return v[v.size() / 2];
    }

    config cfg_;
    knst_vector<hist_entry> hist_;
};

// ============================================================
// async_sensor_monitor
// ============================================================

class async_sensor_monitor {
public:
    struct config {
        uint32_t interval_ms   = KNST_SENS_DEFAULT_INTERVAL_MS;
        uint32_t history_depth = KNST_SENS_DEFAULT_HISTORY_DEPTH;
        bool enable_filter     = true;
    };

    async_sensor_monitor() noexcept = default;
    explicit async_sensor_monitor(config c) noexcept : cfg_(sanitize(c)) {}
    ~async_sensor_monitor() { stop(); }

    async_sensor_monitor(const async_sensor_monitor&) = delete;
    async_sensor_monitor& operator=(const async_sensor_monitor&) = delete;
    async_sensor_monitor(async_sensor_monitor&&) = delete;
    async_sensor_monitor& operator=(async_sensor_monitor&&) = delete;

    // Ignored while running.
    void set_config(config c) noexcept {
        std::lock_guard<std::mutex> lk(life_mu_);
        if (!running_.load()) cfg_ = sanitize(c);
    }
    const config& get_config() const noexcept { return cfg_; }

    result<void> start() noexcept {
        std::lock_guard<std::mutex> lk(life_mu_);
        if (running_.load()) return result<void>::success();
        stop_.store(false);
        thread_ = knst_thread();
        running_.store(true);
        if (!thread_.start([this] { loop(); })) {
            running_.store(false);
            return result<void>::fail(status::Unknown);
        }
        return result<void>::success();
    }

    void stop() noexcept {
        std::lock_guard<std::mutex> lk(life_mu_);
        if (!running_.load()) return;
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
        running_.store(false);
    }

    bool running() const noexcept { return running_.load(); }

    result<void> sample_once() noexcept {
        auto list = knst_devices::sensors();
        if (list.empty()) return result<void>::fail(status::NotFound);
        std::lock_guard<std::mutex> lk(mu_);
        ingest(list);
        return result<void>::success();
    }

    // Milliseconds since the last successful sample (UINT64_MAX = never).
    uint64_t age_ms() const noexcept {
        const uint64_t last = last_ok_ms_.load();
        if (last == 0) return UINT64_MAX;
        const uint64_t now = steady_ms();
        return now > last ? now - last : 0;
    }

    knst_vector<knst_sensor_info> snapshot() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return last_;
    }

    result<sensor_stats> stats(const knst_c16string& chip,
                               const knst_c16string& name) const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& e : entries_)
            if (e.chip == chip && e.name == name)
                return result<sensor_stats>::success(e.st);
        return result<sensor_stats>::fail(status::NotFound);
    }

    knst_vector<double> history(const knst_c16string& chip,
                                const knst_c16string& name) const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& e : entries_)
            if (e.chip == chip && e.name == name)
                return e.history.to_ordered();
        return {};
    }

    result<knst_sensor_info> find(knst_sensor_type type,
                                  const char16_t* needle = u"") const noexcept {
        if (!needle) needle = u"";
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& s : last_)
            if (s.type == type &&
                (!needle[0] || s.name.contains(needle) || s.chip.contains(needle)))
                return result<knst_sensor_info>::success(s);
        return result<knst_sensor_info>::fail(status::NotFound);
    }

    result<knst_sensor_info> hottest() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        bool any = false;
        knst_sensor_info best;
        for (auto& s : last_) {
            if (s.type != knst_sensor_type::Temperature) continue;
            if (!std::isfinite(s.value)) continue;
            if (s.value < -40.0 || s.value > 150.0) continue;
            if (!any || s.value > best.value) { best = s; any = true; }
        }
        return any ? result<knst_sensor_info>::success(best)
                   : result<knst_sensor_info>::fail(status::NotFound);
    }

    knst_vector<alarm> over_limit(knst_sensor_type type, double limit) const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        knst_vector<alarm> out;
        for (auto& s : last_) {
            if (s.type != type) continue;
            if (!std::isfinite(s.value)) continue;
            if (s.value <= limit || s.value >= 1e6) continue;
            alarm a;
            a.chip = s.chip; a.name = s.name;
            a.value = s.value; a.limit = limit;
            out.push_back(a);
        }
        return out;
    }

    // Hottest valid temperature of a component class.
    result<double> component_temp(component c) const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        bool any = false;
        double best = 0.0;
        for (auto& s : last_) {
            if (s.type != knst_sensor_type::Temperature) continue;
            if (classify_chip(s.chip) != c) continue;
            if (!std::isfinite(s.value) || s.value < -40.0 || s.value > 150.0) continue;
            if (!any || s.value > best) { best = s.value; any = true; }
        }
        return any ? result<double>::success(best)
                   : result<double>::fail(status::NotFound);
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        entries_.clear();
        last_.clear();
        filter_.reset();
        last_ok_ms_.store(0);
    }

private:
    static config sanitize(config c) noexcept {
        if (c.interval_ms < 50) c.interval_ms = 50;
        if (c.history_depth > 100000) c.history_depth = 100000;
        return c;
    }

    struct ring {
        knst_vector<double> buf;
        uint32_t head = 0, count = 0, cap = 0;

        void init(uint32_t c) {
            cap = c;
            if (cap > 0) buf.resize(cap, 0.0);
            head = 0; count = 0;
        }
        void push(double v) {
            if (cap == 0) return;
            buf[head] = v;
            head = (head + 1) % cap;
            if (count < cap) ++count;
        }
        knst_vector<double> to_ordered() const {
            knst_vector<double> out;
            if (cap == 0 || count == 0) return out;
            out.reserve(count);
            const uint32_t start = (head + cap - count) % cap;
            for (uint32_t i = 0; i < count; ++i)
                out.push_back(buf[(start + i) % cap]);
            return out;
        }
    };

    struct entry {
        knst_c16string chip, name;
        knst_sensor_type type = knst_sensor_type::Unknown;
        sensor_stats st;
        double sum = 0.0;
        ring history;
    };

    entry* find_entry(const knst_c16string& chip, const knst_c16string& name) noexcept {
        for (auto& e : entries_)
            if (e.chip == chip && e.name == name) return &e;
        return nullptr;
    }

    // mu_ must be held.
    void ingest(const knst_vector<knst_sensor_info>& list) noexcept {
        last_ = list;
        last_ok_ms_.store(steady_ms());
        for (auto& s : list) {
            if (cfg_.enable_filter && !filter_.accept(s)) continue;
            if (!std::isfinite(s.value)) continue;

            entry* e = find_entry(s.chip, s.name);
            if (!e) {
                entry n;
                n.chip = s.chip; n.name = s.name; n.type = s.type;
                n.st.min = n.st.max = s.value;
                n.st.last = s.value;
                n.history.init(cfg_.history_depth);
                entries_.push_back(std::move(n));
                e = &entries_.back();
            }

            if (s.value < e->st.min) e->st.min = s.value;
            if (s.value > e->st.max) e->st.max = s.value;
            e->sum += s.value;
            ++e->st.count;
            e->st.last = s.value;
            e->st.avg  = e->sum / e->st.count;
            e->history.push(s.value);
        }
    }

    void loop() noexcept {
        while (!stop_.load()) {
            auto list = knst_devices::sensors();
            if (!list.empty()) {
                std::lock_guard<std::mutex> lk(mu_);
                ingest(list);
            }
            uint32_t slept = 0;
            const uint32_t interval = cfg_.interval_ms;
            while (slept < interval && !stop_.load()) {
                uint32_t chunk = 50;
                if (chunk > interval - slept) chunk = interval - slept;
                std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
                slept += chunk;
            }
        }
    }

    config cfg_;
    sensor_filter filter_;
    mutable std::mutex mu_;
    std::mutex life_mu_;
    knst_vector<knst_sensor_info> last_;
    knst_vector<entry> entries_;
    knst_thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> last_ok_ms_{0};
};

// ============================================================
// Crash-safe restore registry (Linux)
//
// Every fan that this process switched to manual registers a pre-formatted
// restore recipe here. The recipe can be executed from a signal handler
// using only open()/write()/close().
// ============================================================

#if KNST_USING_PLATFORM_LINUX

namespace detail {

struct restore_slot {
    std::atomic<int> state{0};        // 0 free, 1 being filled, 2 armed
    char enable_path[260];
    char pwm_path[260];
    char pwm_val[8];
    char enable_val[4];
};

inline restore_slot g_restore_slots[64];

// Not signal-safe (uses snprintf); called from normal context only.
inline int claim_restore_slot(const char* enable_path, const char* pwm_path,
                              uint32_t pwm, uint32_t enable_mode) noexcept {
    for (int i = 0; i < 64; ++i) {
        int expected = 0;
        if (g_restore_slots[i].state.compare_exchange_strong(expected, 1)) {
            restore_slot& s = g_restore_slots[i];
            ::snprintf(s.enable_path, sizeof(s.enable_path), "%s", enable_path ? enable_path : "");
            ::snprintf(s.pwm_path,    sizeof(s.pwm_path),    "%s", pwm_path);
            ::snprintf(s.pwm_val,     sizeof(s.pwm_val),     "%u", pwm);
            ::snprintf(s.enable_val,  sizeof(s.enable_val),  "%u", enable_mode);
            s.state.store(2);
            return i;
        }
    }
    return -1;
}

inline void release_restore_slot(int i) noexcept {
    if (i >= 0 && i < 64) g_restore_slots[i].state.store(0);
}

// Async-signal-safe.
inline void write_sysfs_raw(const char* path, const char* val) noexcept {
    int fd = ::open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;
    size_t n = ::strlen(val);
    while (n > 0) {
        ssize_t w = ::write(fd, val, n);
        if (w < 0) { if (errno == EINTR) continue; break; }
        if (w == 0) break;
        val += w;
        n -= static_cast<size_t>(w);
    }
    ::close(fd);
}

// Async-signal-safe.
inline void restore_all_signal_safe() noexcept {
    const int saved_errno = errno;
    for (int i = 0; i < 64; ++i) {
        restore_slot& s = g_restore_slots[i];
        if (s.state.load() != 2) continue;
        const bool has_enable = s.enable_path[0] != '\0';
        if (has_enable) write_sysfs_raw(s.enable_path, "1");
        write_sysfs_raw(s.pwm_path, s.pwm_val);
        if (has_enable) write_sysfs_raw(s.enable_path, s.enable_val);
    }
    errno = saved_errno;
}

inline void crash_signal_handler(int sig) {
    restore_all_signal_safe();
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

} // namespace detail

// Put every touched fan back to auto, async-signal-safe. Can also be
// called manually from your own signal handler.
inline void restore_all_fans_signal_safe() noexcept {
    detail::restore_all_signal_safe();
}

// Opt-in. Replaces the handlers of the usual fatal / termination signals:
// fans are restored, then the default action runs (process still dies /
// dumps core as usual). Call once at startup, BEFORE installing your own
// handlers if you have any.
inline bool install_crash_handlers() noexcept {
    struct sigaction sa;
    ::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &detail::crash_signal_handler;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND | SA_NODEFER;
    const int sigs[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT,
                         SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
    bool ok = true;
    for (int s : sigs)
        if (::sigaction(s, &sa, nullptr) != 0) ok = false;
    return ok;
}

#endif // KNST_USING_PLATFORM_LINUX

// ============================================================
// fan_control — Linux
// ============================================================

#if KNST_USING_PLATFORM_LINUX && !defined(KNST_USING_PLATFORM_ANDROID)

class fan_control {
public:
    fan_control() noexcept = default;

    // Custom move: the source gives up ownership completely so its
    // destructor can never restore (or touch) the fan.
    fan_control(fan_control&& o) noexcept { take(o); }
    fan_control& operator=(fan_control&& o) noexcept {
        if (this != &o) { finalize(); take(o); }
        return *this;
    }
    fan_control(const fan_control&) = delete;
    fan_control& operator=(const fan_control&) = delete;

    ~fan_control() noexcept { finalize(); }

    static result<fan_control> open(const knst_c16string& chip,
                                    uint32_t index = 1,
                                    uint32_t min_safe_pwm = KNST_SENS_DEFAULT_MIN_SAFE_PWM) noexcept {
        using R = result<fan_control>;
        if (index == 0 || index > 16 ||
            min_safe_pwm < static_cast<uint32_t>(KNST_SENS_ABSOLUTE_MIN_PWM) ||
            min_safe_pwm > 255 ||
            !knst_dev::is_safe_token(chip, 32))
            return R::fail(status::InvalidArgument);

        knst_byte_string cb = knst_dev::to_utf8(chip);
        const char* want = reinterpret_cast<const char*>(cb.data());
        if (!want || !*want) return R::fail(status::InvalidArgument);

        DIR* d = ::opendir("/sys/class/hwmon");
        if (!d) { const int err = errno; return R::fail(knst_dev::map_errno(err), err); }

        fan_control f;
        bool found = false;
        struct dirent* e;
        while ((e = ::readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            char p[320], nm[64];
            int n = ::snprintf(p, sizeof(p), "/sys/class/hwmon/%s/name", e->d_name);
            if (n < 0 || static_cast<size_t>(n) >= sizeof(p)) continue;
            if (!knst_devices::detail::read_file(p, nm, sizeof(nm)) ||
                ::strcmp(nm, want) != 0)
                continue;
            n = ::snprintf(p, sizeof(p), "/sys/class/hwmon/%s/pwm%u", e->d_name, index);
            if (n < 0 || static_cast<size_t>(n) >= sizeof(p)) continue;
            struct stat st;
            if (::stat(p, &st) != 0) continue;
            n = ::snprintf(f.base_, sizeof(f.base_), "/sys/class/hwmon/%s", e->d_name);
            if (n < 0 || static_cast<size_t>(n) >= sizeof(f.base_)) { f.base_[0] = '\0'; continue; }
            ::snprintf(f.chip_name_, sizeof(f.chip_name_), "%s", nm);
            found = true;
            break;
        }
        ::closedir(d);
        if (!found) return R::fail(status::NotFound);

        f.index_    = index;
        f.min_safe_ = min_safe_pwm;

        // Remember the mode the fan was in so restore_auto() can put it back.
        char ep[260];
        f.path(ep, sizeof(ep), "pwm", "_enable");
        struct stat st2;
        f.has_enable_ = (::stat(ep, &st2) == 0);
        if (f.has_enable_) {
            uint64_t v = 0;
            if (knst_devices::detail::read_file_u64(ep, v))
                f.orig_enable_ = static_cast<uint32_t>(v);
        }
        return R::success(std::move(f));
    }

    bool valid() const noexcept { return base_[0] != '\0'; }
    uint32_t index() const noexcept { return index_; }
    uint32_t min_safe_pwm() const noexcept { return min_safe_; }
    knst_c16string chip_name() const noexcept { return knst_c16string(chip_name_); }

    // True if this object changed the fan and has not restored it yet.
    bool needs_restore() const noexcept { return dirty_; }

    result<uint32_t> get_pwm() const noexcept        { return read_u32("pwm", ""); }
    result<uint32_t> get_rpm() const noexcept        { return read_u32("fan", "_input"); }
    result<uint32_t> get_pwm_enable() const noexcept { return read_u32("pwm", "_enable"); }

    // SAFE — rejects value < min_safe_pwm (including 0).
    result<void> set_pwm(uint32_t value) noexcept {
        if (!valid()) return result<void>::fail(status::BadState);
        if (value > 255) return result<void>::fail(status::InvalidArgument);
        if (value < min_safe_) return result<void>::fail(status::InvalidArgument);
        return write_pwm(value);
    }

    result<void> set_percent(uint32_t percent) noexcept {
        if (percent > 100) return result<void>::fail(status::InvalidArgument);
        const uint32_t pwm = (percent * 255 + 50) / 100;
        if (pwm < min_safe_) return result<void>::fail(status::InvalidArgument);
        return set_pwm(pwm);
    }

    // DANGEROUS — no min_safe check. May stop the fan.
    [[deprecated("Use set_pwm() — this bypasses min_safe_pwm and may stop the fan")]]
    result<void> set_pwm_unsafe(uint32_t value) noexcept {
        if (!valid()) return result<void>::fail(status::BadState);
        if (value > 255) return result<void>::fail(status::InvalidArgument);
        return write_pwm(value);
    }

    // true  -> manual mode (1)
    // false -> automatic mode (the mode the fan had at open(), or 2)
    result<void> set_manual(bool manual) noexcept {
        if (!valid()) return result<void>::fail(status::BadState);
        if (manual) {
            mark_dirty();
            return write_enable(1);
        }
        return write_enable(restore_target_enable());
    }

    // SAFE RESTORE (always performs the write, even if this object never
    // changed anything — the watchdog relies on that):
    //  1. manual mode + PWM >= max(min_safe, 128, current speed)
    //  2. back to the original automatic mode
    //  3. verify (retry once)
    result<void> restore_auto() noexcept {
        if (!valid()) return result<void>::fail(status::BadState);

        uint32_t safe = safe_restore_pwm();
        auto cur = get_pwm();
        if (cur && cur.value > safe && cur.value <= 255) safe = cur.value;

        (void)write_enable(1);
        char p[260], v[16];
        path(p, sizeof(p), "pwm", "");
        ::snprintf(v, sizeof(v), "%u", safe);
        (void)knst_dev::sysfs_write(p, v);

        const uint32_t target = restore_target_enable();
        auto r = write_enable(target);
        if (has_enable_ && r) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto vr = get_pwm_enable();
            if (vr && vr.value != target) {
                r = write_enable(target);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                auto vr2 = get_pwm_enable();
                if (vr2 && vr2.value != target)
                    r = result<void>::fail(status::BadState);
            }
        }
        if (r) {
            dirty_ = false;
            if (slot_ >= 0) { detail::release_restore_slot(slot_); slot_ = -1; }
        }
        return r;
    }

private:
    char base_[192]      = {0};
    char chip_name_[64]  = {0};
    uint32_t index_      = 1;
    uint32_t min_safe_   = KNST_SENS_DEFAULT_MIN_SAFE_PWM;
    uint32_t orig_enable_ = 0;
    bool     has_enable_ = false;
    bool     dirty_      = false;
    int      slot_       = -1;

    void take(fan_control& o) noexcept {
        ::memcpy(base_, o.base_, sizeof(base_));
        ::memcpy(chip_name_, o.chip_name_, sizeof(chip_name_));
        index_       = o.index_;
        min_safe_    = o.min_safe_;
        orig_enable_ = o.orig_enable_;
        has_enable_  = o.has_enable_;
        dirty_       = o.dirty_;
        slot_        = o.slot_;
        o.base_[0]   = '\0';
        o.dirty_     = false;
        o.slot_      = -1;
    }

    void finalize() noexcept {
        if (!valid()) return;
        if (dirty_) restore_auto();
        if (slot_ >= 0) { detail::release_restore_slot(slot_); slot_ = -1; }
    }

    void path(char* out, size_t sz, const char* prefix, const char* suffix) const noexcept {
        ::snprintf(out, sz, "%s/%s%u%s", base_, prefix, index_, suffix);
    }

    uint32_t safe_restore_pwm() const noexcept {
        uint32_t s = min_safe_ > static_cast<uint32_t>(KNST_SENS_RESTORE_PWM)
                   ? min_safe_ : static_cast<uint32_t>(KNST_SENS_RESTORE_PWM);
        return s > 255 ? 255u : s;
    }

    uint32_t restore_target_enable() const noexcept {
        return (orig_enable_ >= 2 && orig_enable_ <= 5) ? orig_enable_ : 2u;
    }

    void mark_dirty() noexcept {
        if (dirty_) return;
        dirty_ = true;
        char ep[260], pp[260];
        path(ep, sizeof(ep), "pwm", "_enable");
        path(pp, sizeof(pp), "pwm", "");
        slot_ = detail::claim_restore_slot(has_enable_ ? ep : "", pp,
                                           safe_restore_pwm(), restore_target_enable());
    }

    result<void> write_enable(uint32_t mode) noexcept {
        if (!has_enable_) return result<void>::success();
        char p[260], v[8];
        path(p, sizeof(p), "pwm", "_enable");
        ::snprintf(v, sizeof(v), "%u", mode);
        return knst_dev::sysfs_write(p, v);
    }

    result<void> write_pwm(uint32_t value) noexcept {
        mark_dirty();
        auto m = write_enable(1);
        if (!m) return m;
        char p[260], v[16];
        path(p, sizeof(p), "pwm", "");
        ::snprintf(v, sizeof(v), "%u", value);
        auto w = knst_dev::sysfs_write(p, v);
        if (!w) restore_auto();     // never leave a fan in manual with unknown speed
        return w;
    }

    result<uint32_t> read_u32(const char* prefix, const char* suffix) const noexcept {
        if (!valid()) return result<uint32_t>::fail(status::BadState);
        char p[260];
        uint64_t v = 0;
        path(p, sizeof(p), prefix, suffix);
        if (!knst_devices::detail::read_file_u64(p, v))
            return result<uint32_t>::fail(status::IoError);
        return result<uint32_t>::success(static_cast<uint32_t>(v));
    }
};

#else // Windows stub

class fan_control {
public:
    fan_control() noexcept = default;
    fan_control(fan_control&&) noexcept = default;
    fan_control& operator=(fan_control&&) noexcept = default;
    fan_control(const fan_control&) = delete;
    fan_control& operator=(const fan_control&) = delete;
    ~fan_control() noexcept = default;

    static result<fan_control> open(const knst_c16string&, uint32_t = 1,
                                    uint32_t = KNST_SENS_DEFAULT_MIN_SAFE_PWM) noexcept {
        return result<fan_control>::fail(status::NotSupported);
    }

    bool valid() const noexcept { return false; }
    bool needs_restore() const noexcept { return false; }
    uint32_t index() const noexcept { return 0; }
    uint32_t min_safe_pwm() const noexcept { return 0; }
    knst_c16string chip_name() const noexcept { return knst_c16string(); }

    result<uint32_t> get_pwm() const noexcept        { return result<uint32_t>::fail(status::NotSupported); }
    result<uint32_t> get_rpm() const noexcept        { return result<uint32_t>::fail(status::NotSupported); }
    result<uint32_t> get_pwm_enable() const noexcept { return result<uint32_t>::fail(status::NotSupported); }

    result<void> set_pwm(uint32_t) noexcept                { return result<void>::fail(status::NotSupported); }
    result<void> set_percent(uint32_t) noexcept            { return result<void>::fail(status::NotSupported); }
    result<void> set_pwm_unsafe(uint32_t) noexcept         { return result<void>::fail(status::NotSupported); }
    result<void> set_manual(bool) noexcept                 { return result<void>::fail(status::NotSupported); }
    result<void> restore_auto() noexcept                   { return result<void>::fail(status::NotSupported); }
};

#endif

// ============================================================
// fan_curve + shared stepping logic
// ============================================================

struct curve_point {
    double   temp_c = 0.0;
    uint32_t pwm    = 0;
};

struct fan_curve {
    knst_vector<curve_point> points;
    double   hysteresis_c = 2.0;   // cool-down deadband (rising is never delayed)
    uint32_t ramp_step    = 8;     // max PWM decrease per tick (0 = unlimited)
    uint32_t ramp_up_step = 255;   // max PWM increase per tick (0 = unlimited)

    fan_curve() noexcept = default;
    fan_curve(std::initializer_list<curve_point> pts) : points(pts) { normalize(); }

    // Fail-safe: empty curve or non-finite temperature => full speed.
    uint32_t pwm_for(double temp_c) const noexcept {
        if (points.empty() || !std::isfinite(temp_c)) return 255;
        auto clampp = [](double p) -> uint32_t {
            if (p < 0.0) return 0;
            if (p > 255.0) return 255;
            return static_cast<uint32_t>(p);
        };
        if (temp_c <= points[0].temp_c) return clampp(points[0].pwm);
        if (temp_c >= points[points.size() - 1].temp_c)
            return clampp(points[points.size() - 1].pwm);
        for (uint32_t i = 0; i + 1 < points.size(); ++i) {
            if (temp_c <= points[i + 1].temp_c) {
                double t0 = points[i].temp_c, t1 = points[i + 1].temp_c;
                double p0 = points[i].pwm,    p1 = points[i + 1].pwm;
                if (t1 <= t0) return clampp(p1);
                double ratio = (temp_c - t0) / (t1 - t0);
                return clampp(p0 + (p1 - p0) * ratio);
            }
        }
        return clampp(points[points.size() - 1].pwm);
    }

    void normalize() noexcept {
        for (uint32_t i = 1; i < points.size(); ++i) {
            curve_point key = points[i];
            int j = static_cast<int>(i) - 1;
            while (j >= 0 && points[j].temp_c > key.temp_c) {
                points[j + 1] = points[j];
                --j;
            }
            points[j + 1] = key;
        }
    }

    static fan_curve silent() noexcept {
        fan_curve c;
        c.points = {{40.0, 50}, {55.0, 80}, {70.0, 130},
                    {85.0, 200}, {95.0, 255}};
        c.hysteresis_c = 3.0;
        c.ramp_step    = 5;
        return c;
    }
    static fan_curve balanced() noexcept {
        fan_curve c;
        c.points = {{40.0, 60}, {50.0, 100}, {65.0, 160},
                    {80.0, 220}, {95.0, 255}};
        c.hysteresis_c = 2.0;
        c.ramp_step    = 10;
        return c;
    }
    static fan_curve performance() noexcept {
        fan_curve c;
        c.points = {{35.0, 100}, {45.0, 150}, {55.0, 200},
                    {70.0, 240}, {85.0, 255}};
        c.hysteresis_c = 1.5;
        c.ramp_step    = 20;
        return c;
    }
};

struct curve_state {
    double eff_temp = 0.0;
    bool   valid    = false;
};

// One control step. Rising temperature is followed immediately; falling
// temperature must drop by hysteresis_c before the curve input follows.
// The result is clamped to [min_safe,255] and rate-limited versus `cur`.
inline uint32_t curve_next_pwm(const fan_curve& c, curve_state& st, double temp,
                               uint32_t cur, uint32_t min_safe) noexcept {
    if (!std::isfinite(temp)) { st.valid = false; return 255; }
    const double hyst = c.hysteresis_c > 0.0 ? c.hysteresis_c : 0.0;
    if (!st.valid || temp > st.eff_temp || (st.eff_temp - temp) >= hyst) {
        st.eff_temp = temp;
        st.valid = true;
    }
    uint32_t target = c.pwm_for(st.eff_temp);
    if (target < min_safe) target = min_safe;
    if (target > 255) target = 255;

    if (target > cur) {
        if (c.ramp_up_step != 0 && target - cur > c.ramp_up_step)
            target = cur + c.ramp_up_step;
    } else if (target < cur) {
        if (c.ramp_step != 0 && cur - target > c.ramp_step)
            target = cur - c.ramp_step;
    }
    if (target < min_safe) target = min_safe;
    if (target > 255) target = 255;
    return target;
}

// ============================================================
// fan_curve_controller
// ============================================================

class fan_curve_controller {
public:
    fan_curve_controller() noexcept = default;

    fan_curve_controller(fan_control fan, fan_curve curve,
                         uint32_t min_safe_pwm = KNST_SENS_DEFAULT_MIN_SAFE_PWM) noexcept
        : fan_(std::move(fan)), curve_(std::move(curve)),
          min_safe_(sanitize_min_pwm(min_safe_pwm)) {
        curve_.normalize();
        if (fan_.valid() && fan_.min_safe_pwm() > min_safe_)
            min_safe_ = fan_.min_safe_pwm();
        current_pwm_ = min_safe_;
    }

    ~fan_curve_controller() noexcept = default;   // fan_control restores itself

    fan_curve_controller(fan_curve_controller&&) noexcept = default;
    fan_curve_controller& operator=(fan_curve_controller&&) noexcept = default;
    fan_curve_controller(const fan_curve_controller&) = delete;
    fan_curve_controller& operator=(const fan_curve_controller&) = delete;

    result<void> tick(double temp_c) noexcept {
        if (!fan_.valid()) return result<void>::fail(status::BadState);

        if (!initialized_) {
            auto p = fan_.get_pwm();
            if (p && p.value >= min_safe_ && p.value <= 255) current_pwm_ = p.value;
            initialized_ = true;
        }

        const uint32_t target = curve_next_pwm(curve_, state_, temp_c,
                                               current_pwm_, min_safe_);
        if (target == current_pwm_) return result<void>::success();

        auto r = fan_.set_pwm(target);
        if (r) current_pwm_ = target;
        return r;
    }

    result<void> restore_auto() noexcept {
        initialized_ = false;
        state_ = curve_state();
        return fan_.restore_auto();
    }
    uint32_t current_pwm() const noexcept { return current_pwm_; }
    const fan_curve& get_curve() const noexcept { return curve_; }
    fan_curve& get_curve() noexcept { return curve_; }
    void set_curve(fan_curve c) noexcept { c.normalize(); curve_ = std::move(c); state_ = curve_state(); }
    fan_control& fan() noexcept { return fan_; }

private:
    fan_control fan_;
    fan_curve   curve_;
    curve_state state_;
    uint32_t    min_safe_    = KNST_SENS_DEFAULT_MIN_SAFE_PWM;
    uint32_t    current_pwm_ = KNST_SENS_DEFAULT_MIN_SAFE_PWM;
    bool        initialized_ = false;
};

// ============================================================
// event_bus
// ============================================================

class event_bus {
public:
    using handler = knst_function_shared<const thermal_event&>;

    uint32_t subscribe(handler h) noexcept {
        uint32_t id = next_id_.fetch_add(1);
        std::lock_guard<std::mutex> lk(mu_);
        subs_.push_back({id, std::move(h)});
        return id;
    }

    void unsubscribe(uint32_t id) noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        for (uint32_t i = 0; i < subs_.size(); ++i) {
            if (subs_[i].id == id) { subs_.erase(i); return; }
        }
    }

    void emit(const thermal_event& e) noexcept {
        knst_vector<handler> local;
        {
            std::lock_guard<std::mutex> lk(mu_);
            local.reserve(subs_.size());
            for (auto& s : subs_) if (s.h) local.push_back(s.h);
        }
        for (auto& h : local) {
            try { h(e); } catch (...) {}
        }
    }

private:
    struct sub { uint32_t id; handler h; };
    knst_vector<sub>       subs_;
    mutable std::mutex     mu_;
    std::atomic<uint32_t>  next_id_{1};
};

// ============================================================
// emergency_responder
//   Not thread-safe: call check() from one thread (the manager thread).
// ============================================================

class emergency_responder {
public:
    enum class level : uint8_t { Normal = 0, Warn, Critical, Shutdown };

    struct config {
        double warn_c      = 85.0;
        double critical_c  = 95.0;
        double shutdown_c  = 105.0;
        bool   full_blast_on_critical = true;
        double hysteresis_c = 5.0;      // must drop this far below a threshold to de-escalate
        uint32_t repeat_ms  = 10000;    // re-emit / re-blast interval while a level persists

        bool valid() const noexcept {
            return std::isfinite(warn_c) && std::isfinite(critical_c)
                && std::isfinite(shutdown_c) && std::isfinite(hysteresis_c)
                && warn_c < critical_c && critical_c < shutdown_c
                && hysteresis_c >= 0.0;
        }
    };

    emergency_responder() noexcept = default;
    explicit emergency_responder(event_bus& events) noexcept
        : events_(&events) {}
    emergency_responder(event_bus& events, const config& c) noexcept
        : events_(&events), cfg_(c) {}

    void set_event_bus(event_bus* e) noexcept { events_ = e; }
    void set_blast_callback(std::function<void()> fn) noexcept { blast_ = std::move(fn); }
    void set_release_callback(std::function<void()> fn) noexcept { release_ = std::move(fn); }
    void set_config(config c) noexcept { cfg_ = c; }
    const config& get_config() const noexcept { return cfg_; }
    level current_level() const noexcept { return level_; }
    void reset() noexcept { level_ = level::Normal; last_emit_ms_ = 0; }

    void check(const knst_sensor_info& hottest) noexcept {
        const double v = hottest.value;
        if (!std::isfinite(v)) return;

        const level raw = classify(v);
        level nl = level_;
        if (raw >= level_) {
            nl = raw;
        } else {
            while (nl > raw && v < threshold(nl) - cfg_.hysteresis_c)
                nl = static_cast<level>(static_cast<uint8_t>(nl) - 1);
        }

        const uint64_t t = now_ms();
        const bool escalated = nl > level_;
        const bool repeat = (nl == level_) && nl != level::Normal
                         && (t - last_emit_ms_ >= cfg_.repeat_ms);
        const bool was_critical = level_ >= level::Critical;
        level_ = nl;

        if (nl >= level::Critical && (escalated || repeat)
            && cfg_.full_blast_on_critical && blast_)
            blast_();
        if (was_critical && nl < level::Critical && release_)
            release_();

        if (escalated || repeat) {
            last_emit_ms_ = t;
            emit_event(kind_for(nl), hottest, threshold(nl));
        }
    }

private:
    level classify(double v) const noexcept {
        if (v >= cfg_.shutdown_c) return level::Shutdown;
        if (v >= cfg_.critical_c) return level::Critical;
        if (v >= cfg_.warn_c)     return level::Warn;
        return level::Normal;
    }
    double threshold(level l) const noexcept {
        switch (l) {
            case level::Warn:     return cfg_.warn_c;
            case level::Critical: return cfg_.critical_c;
            case level::Shutdown: return cfg_.shutdown_c;
            default:              return 0.0;
        }
    }
    static thermal_event::kind kind_for(level l) noexcept {
        switch (l) {
            case level::Shutdown: return thermal_event::kind::Shutdown;
            case level::Critical: return thermal_event::kind::Critical;
            default:              return thermal_event::kind::Warn;
        }
    }

    void emit_event(thermal_event::kind k,
                    const knst_sensor_info& s, double thr) noexcept {
        if (!events_) return;
        thermal_event e;
        e.type = k;
        e.chip = s.chip; e.name = s.name;
        e.value = s.value; e.threshold = thr;
        e.timestamp_ms = now_ms();
        events_->emit(e);
    }

    event_bus*            events_ = nullptr;
    config                cfg_;
    level                 level_ = level::Normal;
    uint64_t              last_emit_ms_ = 0;
    std::function<void()> blast_;
    std::function<void()> release_;
};

// ============================================================
// thermal_zones
// ============================================================

struct thermal_zone {
    knst_c16string type;
    double temp_c = 0.0;
};

#if KNST_USING_PLATFORM_LINUX
inline result<knst_vector<thermal_zone>> thermal_zones() noexcept {
    using R = result<knst_vector<thermal_zone>>;
    DIR* d = ::opendir("/sys/class/thermal");
    if (!d) { const int err = errno; return R::fail(knst_dev::map_errno(err), err); }
    R r;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (::strncmp(e->d_name, "thermal_zone", 12) != 0) continue;
        char p[320];
        int64_t t = 0;
        char ty[64] = {0};
        ::snprintf(p, sizeof(p), "/sys/class/thermal/%s/temp", e->d_name);
        if (!knst_devices::detail::read_file_i64(p, t)) continue;
        ::snprintf(p, sizeof(p), "/sys/class/thermal/%s/type", e->d_name);
        knst_devices::detail::read_file(p, ty, sizeof(ty));
        thermal_zone z;
        z.type   = knst_c16string(ty);
        z.temp_c = static_cast<double>(t) / 1000.0;

        // Reject physically impossible values. Qualcomm thermal HAL
        // returns sentinel values (e.g. 867000 = INT_MAX/1000) for
        // zones that are not actually wired to a sensor on this device.
        if (!std::isfinite(z.temp_c) || z.temp_c < -40.0 || z.temp_c > 150.0)
            continue;

        r.value.push_back(z);
    }
    ::closedir(d);
    return r;
}
#else
inline result<knst_vector<thermal_zone>> thermal_zones() noexcept {
    return result<knst_vector<thermal_zone>>::fail(status::NotSupported);
}
#endif

// ============================================================
// fan discovery
// ============================================================

struct fan_descriptor {
    knst_c16string chip;
    uint32_t index        = 1;
    bool     has_pwm      = false;
    bool     has_rpm      = false;
    bool     has_enable   = false;
    uint32_t current_pwm  = 0;
    uint32_t current_rpm  = 0;
    uint32_t current_enable = 0;
};

#if KNST_USING_PLATFORM_LINUX
inline knst_vector<fan_descriptor> enumerate_fans() noexcept {
    knst_vector<fan_descriptor> out;
    DIR* d = ::opendir("/sys/class/hwmon");
    if (!d) return out;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        char base[320], namepath[360];
        char chip_name[64] = {0};
        ::snprintf(base, sizeof(base), "/sys/class/hwmon/%s", e->d_name);
        ::snprintf(namepath, sizeof(namepath), "%s/name", base);
        if (!knst_devices::detail::read_file(namepath,
                                             chip_name, sizeof(chip_name)))
            continue;

        for (uint32_t idx = 1; idx <= 16; ++idx) {
            char path[380];
            struct stat st;
            ::snprintf(path, sizeof(path), "%s/pwm%u", base, idx);
            if (::stat(path, &st) != 0) continue;

            fan_descriptor fd;
            fd.chip    = knst_c16string(chip_name);
            fd.index   = idx;
            fd.has_pwm = true;

            uint64_t v = 0;
            if (knst_devices::detail::read_file_u64(path, v))
                fd.current_pwm = static_cast<uint32_t>(v);

            ::snprintf(path, sizeof(path), "%s/fan%u_input", base, idx);
            if (knst_devices::detail::read_file_u64(path, v)) {
                fd.has_rpm     = true;
                fd.current_rpm = static_cast<uint32_t>(v);
            }

            ::snprintf(path, sizeof(path), "%s/pwm%u_enable", base, idx);
            if (knst_devices::detail::read_file_u64(path, v)) {
                fd.has_enable     = true;
                fd.current_enable = static_cast<uint32_t>(v);
            }

            out.push_back(std::move(fd));
        }
    }
    ::closedir(d);
    return out;
}
#else
inline knst_vector<fan_descriptor> enumerate_fans() noexcept { return {}; }
#endif

// ============================================================
// discover
// ============================================================

struct discovery_report {
    knst_vector<knst_c16string> sensor_chips;
    knst_vector<fan_descriptor> fans;
    knst_vector<knst_c16string> suggestions;
};

inline discovery_report discover() noexcept {
    discovery_report r;

    auto sensors = knst_devices::sensors();
    for (auto& s : sensors) {
        bool found = false;
        for (auto& c : r.sensor_chips)
            if (c == s.chip) { found = true; break; }
        if (!found) r.sensor_chips.push_back(s.chip);
    }

    r.fans = enumerate_fans();
    for (auto& f : r.fans) {
        knst_c16string line = u"Fan: chip=";
        line.append(f.chip);
        line.append(u" pwm=");
        line.append(static_cast<unsigned int>(f.index));
        if (f.has_rpm) {
            line.append(u" rpm=");
            line.append(static_cast<unsigned int>(f.current_rpm));
        }
        r.suggestions.push_back(std::move(line));
    }

    if (r.fans.empty() && !r.sensor_chips.empty()) {
        r.suggestions.push_back(knst_c16string(
            u"No pwm channels found. If your motherboard has an ITE Super I/O "
            u"(e.g. IT8665E), you may need the 'it87' driver fork with force_id "
            u"support. See https://github.com/frankcrawford/it87"));
    }
    return r;
}

// ============================================================
// fan_zone + orchestrator
// ============================================================

struct fan_state {
    uint32_t stall    = 0;
    bool     spinning = false;
    bool     reported = false;
};

struct fan_zone {
    knst_c16string name;
    knst_c16string temp_chip;
    knst_c16string temp_name;      // empty = hottest sensor of temp_chip
    fan_curve curve;
    knst_vector<knst_c16string> fan_chips;
    knst_vector<uint32_t>       fan_indices;
    uint32_t min_safe_pwm = KNST_SENS_DEFAULT_MIN_SAFE_PWM;
    uint32_t failsafe_pwm = 200;   // used while the temperature sensor is lost

    // ---- runtime state (managed by the orchestrator) ----
    knst_vector<fan_control> fans;
    knst_vector<fan_state>   fan_states;
    curve_state cstate;
    uint32_t current_pwm   = KNST_SENS_DEFAULT_MIN_SAFE_PWM;
    uint32_t lost_ticks    = 0;
    uint32_t retry_cooldown = 0;
    bool     initialized   = false;
    bool     lost_reported = false;
};

class thermal_orchestrator {
public:
    thermal_orchestrator() noexcept = default;
    ~thermal_orchestrator() { restore_all_auto(); }

    thermal_orchestrator(const thermal_orchestrator&) = delete;
    thermal_orchestrator& operator=(const thermal_orchestrator&) = delete;

    result<void> add_zone(fan_zone z) noexcept {
        if (z.temp_chip.empty() || z.fan_chips.empty() || z.curve.points.empty())
            return result<void>::fail(status::InvalidArgument);
        for (uint32_t i = 0; i < z.fan_indices.size(); ++i)
            if (z.fan_indices[i] == 0 || z.fan_indices[i] > 16)
                return result<void>::fail(status::InvalidArgument);

        z.min_safe_pwm = sanitize_min_pwm(z.min_safe_pwm);
        if (z.failsafe_pwm < z.min_safe_pwm) z.failsafe_pwm = z.min_safe_pwm;
        if (z.failsafe_pwm > 255) z.failsafe_pwm = 255;
        z.curve.normalize();
        z.current_pwm = z.min_safe_pwm;
        z.initialized = false;
        z.cstate = curve_state();

        std::lock_guard<std::mutex> lk(mu_);
        zones_.push_back(std::move(z));
        return result<void>::success();
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        restore_locked();
        zones_.clear();
        emergency_.store(false);
    }

    uint32_t zone_count() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return static_cast<uint32_t>(zones_.size());
    }

    // One control step for all zones. Events are emitted AFTER the internal
    // lock is released, so handlers may call back into the orchestrator.
    void tick(async_sensor_monitor& mon, event_bus* bus = nullptr,
              uint32_t stale_ms = 5000) noexcept {
        auto sensors = mon.snapshot();
        const bool stale = mon.age_ms() > stale_ms;
        knst_vector<thermal_event> pending;
        {
            std::lock_guard<std::mutex> lk(mu_);
            const bool emerg = emergency_.load();
            for (auto& z : zones_) {
                ensure_fans(z);
                init_current(z);

                if (emerg) {
                    apply(z, 255);
                    check_stall(z, pending);
                    continue;
                }

                double temp = 0.0;
                const bool found = !stale && read_zone_temp(z, sensors, temp);
                if (!found) {
                    if (z.lost_ticks < 1000000u) ++z.lost_ticks;
                    if (z.lost_ticks >= kLostTicks) {
                        z.cstate.valid = false;
                        apply(z, z.failsafe_pwm);
                        if (!z.lost_reported) {
                            z.lost_reported = true;
                            thermal_event e;
                            e.type = thermal_event::kind::SensorLost;
                            e.chip = z.temp_chip;
                            e.name = z.temp_name.empty() ? z.name : z.temp_name;
                            e.value = 0.0;
                            e.threshold = static_cast<double>(z.failsafe_pwm);
                            e.timestamp_ms = now_ms();
                            pending.push_back(std::move(e));
                        }
                    }
                    check_stall(z, pending);
                    continue;
                }

                z.lost_ticks = 0;
                z.lost_reported = false;

                const uint32_t target = curve_next_pwm(z.curve, z.cstate, temp,
                                                       z.current_pwm, z.min_safe_pwm);
                if (target != z.current_pwm) apply(z, target);
                check_stall(z, pending);
            }
        }
        if (bus)
            for (auto& e : pending) bus->emit(e);
    }

    // Force every zone to 255 and keep it there until clear_emergency().
    void emergency_full_blast() noexcept {
        emergency_.store(true);
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& z : zones_) {
            ensure_fans(z);
            apply(z, 255);
        }
    }

    void clear_emergency() noexcept { emergency_.store(false); }
    bool in_emergency() const noexcept { return emergency_.load(); }

    void restore_all_auto() noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        restore_locked();
    }

private:
    static constexpr uint32_t kLostTicks    = 3;
    static constexpr uint32_t kStallTicks   = 3;
    static constexpr uint32_t kStallMinPwm  = 80;

    // mu_ must be held
    void restore_locked() noexcept {
        for (auto& z : zones_) {
            for (auto& f : z.fans)
                if (f.needs_restore()) f.restore_auto();
            z.initialized = false;
            z.cstate = curve_state();
        }
    }

    static bool read_zone_temp(const fan_zone& z,
                               const knst_vector<knst_sensor_info>& sensors,
                               double& out) noexcept {
        bool found = false;
        double best = 0.0;
        for (auto& s : sensors) {
            if (s.type != knst_sensor_type::Temperature) continue;
            if (s.chip != z.temp_chip) continue;
            if (!z.temp_name.empty() && s.name != z.temp_name) continue;
            if (!std::isfinite(s.value) || s.value < -40.0 || s.value > 150.0) continue;
            if (!found || s.value > best) { best = s.value; found = true; }
        }
        out = best;
        return found;
    }

    void init_current(fan_zone& z) noexcept {
        if (z.initialized || z.fans.empty()) return;
        auto p = z.fans[0].get_pwm();
        if (p && p.value >= z.min_safe_pwm && p.value <= 255) z.current_pwm = p.value;
        z.initialized = true;
    }

    bool apply(fan_zone& z, uint32_t target) noexcept {
        ensure_fans(z);
        bool any = false;
        for (auto& f : z.fans)
            if (f.set_pwm(target)) any = true;
        if (any) z.current_pwm = target;
        return any;
    }

    // Opens only the fans that are missing; never destroys working ones
    // (destroying a dirty fan_control would restore it to auto).
    void ensure_fans(fan_zone& z) noexcept {
        if (z.fans.size() >= z.fan_chips.size()) return;
        if (z.retry_cooldown > 0) { --z.retry_cooldown; return; }
        for (uint32_t i = 0; i < z.fan_chips.size(); ++i) {
            const uint32_t idx = i < z.fan_indices.size() ? z.fan_indices[i] : 1;
            bool have = false;
            for (auto& f : z.fans)
                if (f.index() == idx && f.chip_name() == z.fan_chips[i]) { have = true; break; }
            if (have) continue;
            auto f = fan_control::open(z.fan_chips[i], idx, z.min_safe_pwm);
            if (f) z.fans.push_back(std::move(f.value));
        }
        if (z.fans.size() < z.fan_chips.size()) z.retry_cooldown = 5;
    }

    void check_stall(fan_zone& z, knst_vector<thermal_event>& pending) noexcept {
        while (z.fan_states.size() < z.fans.size()) z.fan_states.push_back(fan_state());
        if (z.current_pwm < kStallMinPwm) return;
        for (uint32_t i = 0; i < z.fans.size(); ++i) {
            auto rpm = z.fans[i].get_rpm();
            if (!rpm) continue;
            fan_state& s = z.fan_states[i];
            if (rpm.value > 0) {
                s.spinning = true; s.stall = 0; s.reported = false;
            } else if (s.spinning) {
                if (++s.stall >= kStallTicks && !s.reported) {
                    s.reported = true;
                    thermal_event e;
                    e.type = thermal_event::kind::FanStall;
                    e.chip = z.fans[i].chip_name();
                    e.name = z.name;
                    e.value = 0.0;
                    e.threshold = static_cast<double>(z.current_pwm);
                    e.timestamp_ms = now_ms();
                    pending.push_back(std::move(e));
                }
            }
        }
    }

    mutable std::mutex mu_;
    std::atomic<bool>  emergency_{false};
    knst_vector<fan_zone> zones_;
};

// ============================================================
// fan_watchdog
// ============================================================

class fan_watchdog {
public:
    explicit fan_watchdog(uint32_t timeout_ms = 3000) noexcept
        : timeout_ms_(timeout_ms < 500 ? 500u : timeout_ms) {}
    ~fan_watchdog() { stop(); }

    fan_watchdog(const fan_watchdog&) = delete;
    fan_watchdog& operator=(const fan_watchdog&) = delete;

    void set_timeout_ms(uint32_t ms) noexcept { timeout_ms_.store(ms < 500 ? 500u : ms); }

    void attach(fan_control fan) noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        fans_.push_back(std::move(fan));
    }

    void heartbeat() noexcept {
        last_hb_ns_.store(static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()) + 1);
    }

    bool start() noexcept {
        std::lock_guard<std::mutex> lk(life_mu_);
        if (running_.load()) return true;
        heartbeat();                       // start the clock now, not at the first beat
        tripped_.store(false);
        stop_.store(false);
        thread_ = knst_thread();
        running_.store(true);
        if (!thread_.start([this] { loop(); })) {
            running_.store(false);
            return false;
        }
        return true;
    }

    void stop() noexcept {
        std::lock_guard<std::mutex> lk(life_mu_);
        if (!running_.load()) return;
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
        running_.store(false);
    }

    bool running() const noexcept { return running_.load(); }
    bool tripped() const noexcept { return tripped_.load(); }

    // Restores ALL attached fans (forced, even if untouched by these objects).
    void emergency_restore() noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& f : fans_) f.restore_auto();
    }

private:
    void loop() noexcept {
        while (!stop_.load()) {
            const int64_t now_ns = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            const int64_t last = last_hb_ns_.load();
            const int64_t diff_ms = last > 0 ? (now_ns - last) / 1000000 : 0;

            if (diff_ms > static_cast<int64_t>(timeout_ms_.load())) {
                if (!tripped_.load()) {
                    emergency_restore();
                    tripped_.store(true);
                }
            } else {
                tripped_.store(false);     // heartbeat is back: re-arm
            }
            for (int i = 0; i < 4 && !stop_.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    std::mutex               mu_;
    std::mutex               life_mu_;
    knst_vector<fan_control> fans_;
    knst_thread              thread_;
    std::atomic<bool>        stop_{false};
    std::atomic<bool>        running_{false};
    std::atomic<bool>        tripped_{false};
    std::atomic<int64_t>     last_hb_ns_{0};
    std::atomic<uint32_t>    timeout_ms_;
};

// ============================================================
// thermal_manager — all-in-one facade (Linux)
// ============================================================

#if KNST_USING_PLATFORM_LINUX && !defined(KNST_USING_PLATFORM_ANDROID)

class thermal_manager {
public:
    struct config {
        uint32_t tick_interval_ms    = 1000;
        uint32_t sample_interval_ms  = KNST_SENS_DEFAULT_INTERVAL_MS;
        uint32_t history_depth       = KNST_SENS_DEFAULT_HISTORY_DEPTH;
        uint32_t watchdog_timeout_ms = 3000;   // raised automatically to >= 3*tick+1s
        emergency_responder::config emergency;
        // Chips whose temperatures must NOT trigger the emergency responder
        // (e.g. a sensor that is known to read absurdly high).
        knst_vector<knst_c16string> emergency_ignore_chips;
    };

    thermal_manager() noexcept : watchdog_(3000) {}
    explicit thermal_manager(config c) noexcept
        : cfg_(std::move(c)), watchdog_(cfg_.watchdog_timeout_ms) {}
    ~thermal_manager() { stop(); }

    thermal_manager(const thermal_manager&) = delete;
    thermal_manager& operator=(const thermal_manager&) = delete;

    result<void> start() noexcept {
        std::lock_guard<std::mutex> lk(life_mu_);
        if (running_.load()) return result<void>::success();

        if (cfg_.tick_interval_ms < 100 || cfg_.tick_interval_ms > 60000
         || cfg_.sample_interval_ms < 50 || !cfg_.emergency.valid())
            return result<void>::fail(status::InvalidArgument);

        const uint32_t slow = std::max(cfg_.sample_interval_ms, cfg_.tick_interval_ms);
        stale_ms_ = std::max(5000u, slow * 5u);

        async_sensor_monitor::config mc;
        mc.interval_ms   = cfg_.sample_interval_ms;
        mc.history_depth = cfg_.history_depth;
        monitor_.set_config(mc);

        // Have real data before the first control tick.
        auto s = monitor_.sample_once();
        if (!s) return s;

        auto r = monitor_.start();
        if (!r) return r;

        watchdog_.set_timeout_ms(std::max(cfg_.watchdog_timeout_ms,
                                          cfg_.tick_interval_ms * 3u + 1000u));
        if (!watchdog_.start()) {
            monitor_.stop();
            return result<void>::fail(status::Unknown);
        }

        emergency_.set_event_bus(&events_);
        emergency_.set_config(cfg_.emergency);
        emergency_.set_blast_callback([this] { orchestrator_.emergency_full_blast(); });
        emergency_.set_release_callback([this] { orchestrator_.clear_emergency(); });
        emergency_.reset();
        orchestrator_.clear_emergency();

        stop_.store(false);
        thread_ = knst_thread();
        running_.store(true);
        if (!thread_.start([this] { loop(); })) {
            running_.store(false);
            monitor_.stop();
            watchdog_.stop();
            return result<void>::fail(status::Unknown);
        }
        return result<void>::success();
    }

    void stop() noexcept {
        std::lock_guard<std::mutex> lk(life_mu_);
        if (running_.load()) {
            stop_.store(true);
            if (thread_.joinable()) thread_.join();
            running_.store(false);
            monitor_.stop();
            watchdog_.stop();
        }
        orchestrator_.clear_emergency();
        orchestrator_.restore_all_auto();   // also covers "never started" state
    }

    bool running() const noexcept { return running_.load(); }

    // All fans of the zone must be openable now; they are then also
    // covered by the watchdog.
    result<void> add_zone(fan_zone z) noexcept {
        if (z.fan_chips.empty())
            return result<void>::fail(status::InvalidArgument);
        const uint32_t min_pwm = sanitize_min_pwm(z.min_safe_pwm);

        knst_vector<fan_control> probes;
        for (uint32_t i = 0; i < z.fan_chips.size(); ++i) {
            const uint32_t idx = i < z.fan_indices.size() ? z.fan_indices[i] : 1;
            auto f = fan_control::open(z.fan_chips[i], idx, min_pwm);
            if (!f) return result<void>::fail(f.st, f.native);
            probes.push_back(std::move(f.value));
        }

        auto r = orchestrator_.add_zone(std::move(z));
        if (!r) return r;
        for (uint32_t i = 0; i < probes.size(); ++i)
            watchdog_.attach(std::move(probes[i]));
        return result<void>::success();
    }

    async_sensor_monitor&       monitor()       noexcept { return monitor_; }
    const async_sensor_monitor& monitor() const noexcept { return monitor_; }
    event_bus&                  events()        noexcept { return events_; }
    thermal_orchestrator&       orchestrator()  noexcept { return orchestrator_; }
    fan_watchdog&               watchdog()      noexcept { return watchdog_; }

private:
    void check_emergency() noexcept {
        if (monitor_.age_ms() > stale_ms_) return;     // zones are already failing safe
        auto snap = monitor_.snapshot();
        bool any = false;
        knst_sensor_info best;
        for (auto& s : snap) {
            if (s.type != knst_sensor_type::Temperature) continue;
            if (!std::isfinite(s.value) || s.value < -40.0 || s.value > 150.0) continue;
            bool ignored = false;
            for (auto& c : cfg_.emergency_ignore_chips)
                if (c == s.chip) { ignored = true; break; }
            if (ignored) continue;
            if (!any || s.value > best.value) { best = s; any = true; }
        }
        if (any) emergency_.check(best);
    }

    void loop() noexcept {
        while (!stop_.load()) {
            watchdog_.heartbeat();
            orchestrator_.tick(monitor_, &events_, stale_ms_);
            check_emergency();
            watchdog_.heartbeat();

            uint32_t slept = 0;
            const uint32_t interval = cfg_.tick_interval_ms;
            while (slept < interval && !stop_.load()) {
                uint32_t chunk = 50;
                if (chunk > interval - slept) chunk = interval - slept;
                std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
                slept += chunk;
            }
        }
    }

    config                  cfg_;
    async_sensor_monitor    monitor_;
    fan_watchdog            watchdog_;
    thermal_orchestrator    orchestrator_;
    event_bus               events_;
    emergency_responder     emergency_;
    knst_thread             thread_;
    std::mutex              life_mu_;
    uint32_t                stale_ms_ = 5000;
    std::atomic<bool>       stop_{false};
    std::atomic<bool>       running_{false};
};

#endif // KNST_USING_PLATFORM_LINUX

#if defined(KNST_JSON_HPP)

namespace knst_sens {

inline knst_json snapshot_to_json(const knst_vector<knst_sensor_info>& sensors) noexcept {
    knst_json arr = knst_json::make_array();
    if (!arr.is_array()) return knst_json();
    for (uint32_t i = 0; i < sensors.size(); ++i) {
        knst_json s = knst_json::make_object();
        if (!s.is_object()) continue;
        s.set("chip",  knst_json(sensors[i].chip));
        s.set("name",  knst_json(sensors[i].name));
        s.set("type",  knst_json(static_cast<int64_t>(sensors[i].type)));
        s.set("value", knst_json(sensors[i].value));
        s.set("unit",  knst_json(sensors[i].unit));
        arr.push_back(std::move(s));
    }
    return arr;
}

} // namespace knst_sens

#endif // KNST_JSON_HPP

} // namespace knst_sens