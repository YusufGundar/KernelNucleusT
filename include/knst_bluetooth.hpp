// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_bluetooth.hpp
----------------------------
    Comprehensive Bluetooth controller.

    Features:
      - Adapter enumeration and info (name, address, powered/discoverable/
        pairable state)
      - Power on/off, discoverable, pairable, adapter rename, rfkill
      - Discovery: timed scan, scan on/off, list all/paired/connected
      - Device info: basic (name, paired, trusted, connected), extended
        (battery, RSSI, class, manufacturer, UUIDs, blocked)
      - Pair: Just Works (via built-in agent) and PIN (best-effort via
        script on Linux, NotSupported on Windows)
      - Trust/untrust, connect/disconnect, remove, block, pair_and_connect,
        reconnect
      - Wait helpers: wait_for_device, wait_for_connection
      - Cross-platform graceful failure via result<T>
*/

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <thread>
#include <utility>



#if KNST_USING_PLATFORM_LINUX
    #include <unistd.h>
    #include <dirent.h>
#endif

#if KNST_USING_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
   #include <bluetoothapis.h>
    #pragma comment(lib, "bthprops.lib")
#endif
namespace knst_bt {

using knst_dev::status;
using knst_dev::result;

// ============================================================
// Common types
// ============================================================

struct adapter_info {
    knst_c16string address;
    knst_c16string name;
    knst_c16string alias;
    bool powered      = false;
    bool discoverable = false;
    bool pairable     = false;
    bool discovering  = false;
    bool valid        = false;
};

struct device_detail {
    knst_bluetooth_device basic;
    int32_t battery_percent = -1;       // -1 = unknown
    int32_t rssi_dbm        = 0;        //  0 = unknown (RSSI is only reported while discovering)
    knst_c16string manufacturer;        // first advertised company id, e.g. "0x004c"
    knst_c16string device_class;        // "audio-headset", "input-keyboard", ... or major class
    knst_vector<knst_c16string> uuids;  // advertised service UUIDs
    bool blocked = false;
    bool valid   = false;
};

inline knst_c16string class_major(uint32_t cod) noexcept {
    switch ((cod >> 8) & 0x1F) {
        case 0x01: return u"Computer";
        case 0x02: return u"Phone";
        case 0x03: return u"LAN/Network";
        case 0x04: return u"Audio/Video";
        case 0x05: return u"Peripheral";
        case 0x06: return u"Imaging";
        case 0x07: return u"Wearable";
        case 0x08: return u"Toy";
        case 0x09: return u"Health";
        default:   return u"Uncategorized";
    }
}

// ============================================================
// Shared helpers (platform independent)
// ============================================================

namespace impl {

inline uint64_t now_ms() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline void sleep_ms(uint32_t ms) noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Validates "AA:BB:CC:DD:EE:FF" (any case) and returns it upper-cased.
inline bool normalize_mac(const knst_c16string& in, knst_c16string& out) noexcept {
    if (!knst_dev::is_mac(in)) return false;
    knst_c16string o;
    for (uint32_t i = 0; i < 17; ++i) {
        char16_t c = in[i];
        if (c >= u'a' && c <= u'f') c = static_cast<char16_t>(c - 32);
        o.append(c);
    }
    out = std::move(o);
    return true;
}

// Statuses that can never be fixed by waiting longer.
inline bool is_fatal(status s) noexcept {
    return s == status::InvalidArgument || s == status::ToolMissing ||
           s == status::NotSupported    || s == status::PermissionDenied;
}

// Polls `cond` until it returns true or `max_ms` elapsed. Checks once
// immediately. Returns whether the condition was reached.
template<typename Cond>
inline bool settle(Cond&& cond, uint32_t max_ms, uint32_t interval_ms = 150) noexcept {
    const uint64_t deadline = now_ms() + max_ms;
    for (;;) {
        if (cond()) return true;
        const uint64_t now = now_ms();
        if (now >= deadline) return false;
        const uint64_t left = deadline - now;
        sleep_ms(left < interval_ms ? static_cast<uint32_t>(left) : interval_ms);
    }
}

template<typename Probe>
inline result<void> wait_until(Probe&& probe, uint32_t timeout_ms,
                                uint32_t interval_ms = 200) noexcept {
    using R = result<void>;
    const uint64_t deadline = now_ms() + timeout_ms;
    for (;;) {
        R r = probe();
        if (r) return R::success();
        if (is_fatal(r.st)) return r;
        const uint64_t now = now_ms();
        if (now >= deadline) return R::fail(status::Timeout);
        const uint64_t left = deadline - now;
        sleep_ms(left < interval_ms ? static_cast<uint32_t>(left) : interval_ms);
    }
}

// f() must return result<knst_bluetooth_device>.
template<typename InfoFn>
inline result<void> wait_connection(InfoFn&& f, bool want_connected,
                                     uint32_t timeout_ms) noexcept {
    using R = result<void>;
    return wait_until([&]() -> R {
        auto i = f();
        if (!i) {
            // Device vanished while we wait for a disconnect -> it is disconnected.
            if (i.st == status::NotFound && !want_connected) return R::success();
            return R::fail(i.st, i.native);
        }
        return (i.value.connected == want_connected) ? R::success()
                                                      : R::fail(status::BadState);
    }, timeout_ms);
}

} // namespace impl


// ============================================================
// Linux (BlueZ)
// ============================================================

#if KNST_USING_PLATFORM_LINUX && !defined(KNST_USING_PLATFORM_ANDROID)

namespace impl {

using knst_devices::detail::split_kv;

inline bool yes(const char* v) noexcept {
    return v && (::strncmp(v, "yes", 3) == 0 || ::strncmp(v, "true", 4) == 0);
}

// Parses "0xffffffc4 (-60)" and "-60" style values.
inline bool parse_paren_int(const char* v, int32_t& out) noexcept {
    if (!v || !*v) return false;
    const char* paren = ::strchr(v, '(');
    char* end = nullptr;
    long x;
    if (paren) {
        x = ::strtol(paren + 1, &end, 10);
        if (end == paren + 1) return false;
    } else {
        x = ::strtol(v, &end, 0);
        if (end == v) return false;
    }
    out = static_cast<int32_t>(x);
    return true;
}

inline bool tool_in_path(const char* prog) noexcept {
    if (!prog || !*prog) return false;
    if (::strchr(prog, '/')) return ::access(prog, X_OK) == 0;
    const char* path = ::getenv("PATH");
    if (!path || !*path)
        path = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    const char* p = path;
    while (*p) {
        const char* e = ::strchr(p, ':');
        const size_t len = e ? static_cast<size_t>(e - p) : ::strlen(p);
        if (len > 0 && len < 400) {
            char full[512];
            ::snprintf(full, sizeof(full), "%.*s/%s", static_cast<int>(len), p, prog);
            if (::access(full, X_OK) == 0) return true;
        }
        if (!e) break;
        p = e + 1;
    }
    return false;
}

// Removes ANSI escape sequences, carriage returns and other control chars
// (bluetoothctl wraps colours in readline's \001 \002 markers).
inline knst_byte_string sanitize(const knst_byte_string& raw) noexcept {
    knst_byte_string out;
    const unsigned char* p = raw.data();
    const uint32_t n = raw.length();
    if (!p || n == 0) return out;
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        const unsigned char c = p[i];
        if (c == 0x1B) {                                   // ESC
            if (i + 1 < n && p[i + 1] == '[') {
                i += 2;
                while (i < n && !(p[i] >= 0x40 && p[i] <= 0x7E)) ++i;   // final byte
            }
            continue;
        }
        if (c == '\n' || c == '\t' || (c >= 0x20 && c != 0x7F)) out.push_back(c);
    }
    return out;
}

// Copies one line into `buf`, strips leading blanks and an optional
// "[bluetooth]# " prompt prefix. Returns a pointer into `buf`.
inline char* prep_line(const char* line, size_t len, char* buf, size_t sz) noexcept {
    if (len >= sz) len = sz - 1;
    ::memcpy(buf, line, len);
    buf[len] = '\0';
    char* c = buf;
    while (*c == '\t' || *c == ' ') ++c;
    if (*c == '[') {
        char* h = ::strstr(c, "]# ");
        if (h && (h - c) < 48) {
            c = h + 3;
            while (*c == '\t' || *c == ' ') ++c;
        }
    }
    return c;
}

// Maps BlueZ / bluetoothctl error text to a status.
inline status map_output_error(const char* t, status fallback) noexcept {
    if (!t || !*t) return fallback;
    if (::strstr(t, "No default controller") || ::strstr(t, "not available") ||
        ::strstr(t, "org.bluez.Error.DoesNotExist") || ::strstr(t, "Does Not Exist"))
        return status::NotFound;
    if (::strstr(t, "org.bluez.Error.InProgress") || ::strstr(t, "org.bluez.Error.Busy"))
        return status::Busy;
    if (::strstr(t, "org.bluez.Error.NotReady") || ::strstr(t, "org.bluez.Error.Blocked") ||
        ::strstr(t, "Blocked through rfkill"))
        return status::BadState;
    if (::strstr(t, "org.bluez.Error.NotPermitted") || ::strstr(t, "org.bluez.Error.Rejected") ||
        ::strstr(t, "AuthenticationRejected") || ::strstr(t, "org.freedesktop.DBus.Error.AccessDenied"))
        return status::PermissionDenied;
    if (::strstr(t, "org.bluez.Error.NotSupported"))
        return status::NotSupported;
    if (::strstr(t, "AuthenticationTimeout") || ::strstr(t, "page-timeout") ||
        ::strstr(t, "Timed out"))
        return status::Timeout;
    return fallback;
}

struct btctl_out {
    status st       = status::Unknown;   // Ok = process ran and produced output/exit code
    int    exit_code = -1;
    knst_byte_string text;               // sanitised stdout
    bool ok() const noexcept { return st == status::Ok; }
};

inline btctl_out run_btctl(const knst_dev::argv& a, uint32_t timeout_ms) noexcept {
    btctl_out r;
    auto t = knst_dev::run_tool_raw("bluetoothctl", a, timeout_ms);
    if (!t.launched) {
        // Not launched = either not installed or killed by our timeout
        // (bluetoothctl blocks forever when bluetoothd is not running).
        r.st = tool_in_path("bluetoothctl") ? status::Timeout : status::ToolMissing;
        return r;
    }
    r.st = status::Ok;
    r.exit_code = t.exit_code;
    r.text = sanitize(t.out);
    return r;
}

inline result<adapter_info> parse_adapter(const knst_byte_string& raw) noexcept {
    using R = result<adapter_info>;
    adapter_info a;
    bool have_ctrl = false;

    knst_dev::for_each_line(knst_dev::text(raw), [&](const char* line, size_t len) {
        char buf[512];
        char* c = prep_line(line, len, buf, sizeof(buf));
        const char* v = nullptr;

        if (::strncmp(c, "Controller ", 11) == 0) {
            if (!have_ctrl) {
                char mac[32] = {0};
                if (::sscanf(c + 11, "%31s", mac) == 1) {
                    knst_c16string m;
                    if (normalize_mac(knst_c16string(mac), m)) {
                        a.address = std::move(m);
                        have_ctrl = true;
                    }
                }
            }
        }
        else if (split_kv(c, "Name", v))         a.name         = knst_c16string(v);
        else if (split_kv(c, "Alias", v))        a.alias        = knst_c16string(v);
        else if (split_kv(c, "Powered", v))      a.powered      = yes(v);
        else if (split_kv(c, "Discoverable", v)) a.discoverable = yes(v);
        else if (split_kv(c, "Pairable", v))     a.pairable     = yes(v);
        else if (split_kv(c, "Discovering", v))  a.discovering  = yes(v);
    });

    if (!have_ctrl) return R::fail(status::NotFound);
    if (a.alias.empty()) a.alias = a.name;
    a.valid = true;
    return R::success(std::move(a));
}

// Parses `bluetoothctl info <mac>` output. Returns false when the device is
// unknown ("Device XX not available") or the output has no device header.
inline bool parse_device(const knst_c16string& addr, const knst_byte_string& raw,
                         device_detail& dd) noexcept {
    dd = device_detail();
    dd.basic.address = addr;

    bool header = false, missing = false, have_cod = false;
    uint32_t cod = 0;
    knst_c16string name, alias;

    knst_dev::for_each_line(knst_dev::text(raw), [&](const char* line, size_t len) {
        char buf[512];
        char* c = prep_line(line, len, buf, sizeof(buf));
        const char* v = nullptr;

        if (::strncmp(c, "Device ", 7) == 0) {
            if (!header) {
                header = true;
                if (::strstr(c, "not available")) missing = true;
            }
            return;
        }

        if      (split_kv(c, "Name", v))       name = knst_c16string(v);
        else if (split_kv(c, "Alias", v))      alias = knst_c16string(v);
        else if (split_kv(c, "Paired", v))     dd.basic.paired = yes(v);
        else if (split_kv(c, "Trusted", v))    dd.basic.trusted = yes(v);
        else if (split_kv(c, "Blocked", v))    dd.blocked = yes(v);
        else if (split_kv(c, "Connected", v))  dd.basic.connected = yes(v);
        else if (split_kv(c, "RSSI", v)) {
            int32_t x = 0;
            if (parse_paren_int(v, x)) dd.rssi_dbm = x;
        }
        else if (split_kv(c, "Icon", v))       dd.device_class = knst_c16string(v);
        else if (split_kv(c, "Class", v)) {
            char* end = nullptr;
            unsigned long x = ::strtoul(v, &end, 0);
            if (end != v) { cod = static_cast<uint32_t>(x); have_cod = true; }
        }
        else if (split_kv(c, "Battery Percentage", v)) {
            int32_t x = 0;
            if (parse_paren_int(v, x) && x >= 0 && x <= 100) dd.battery_percent = x;
        }
        else if (::strncmp(c, "ManufacturerData Key:", 21) == 0) {
            if (dd.manufacturer.empty()) {
                const char* k = c + 21;
                while (*k == ' ' || *k == '\t') ++k;
                char tok[32];
                size_t n = 0;
                while (k[n] && k[n] != ' ' && k[n] != '\t' && n < sizeof(tok) - 1) { tok[n] = k[n]; ++n; }
                tok[n] = '\0';
                if (n > 0) dd.manufacturer = knst_c16string(tok);
            }
        }
        else if (::strncmp(c, "UUID:", 5) == 0) {
            const char* open  = ::strrchr(c, '(');
            const char* close = open ? ::strchr(open, ')') : nullptr;
            if (open && close && close > open + 1 && (close - open - 1) < 64
                && dd.uuids.size() < 64) {
                char u[64];
                const size_t n = static_cast<size_t>(close - open - 1);
                ::memcpy(u, open + 1, n);
                u[n] = '\0';
                dd.uuids.push_back(knst_c16string(u));
            }
        }
    });

    if (!header || missing) return false;

    dd.basic.name = !name.empty() ? name : alias;
    if (dd.device_class.empty() && have_cod) dd.device_class = class_major(cod);
    dd.basic.type = dd.device_class;
    dd.basic.rssi = dd.rssi_dbm;
    dd.basic.valid = true;
    dd.valid = true;
    return true;
}

} // namespace impl


class bluetooth {
public:
    using device_list = knst_vector<knst_bluetooth_device>;

    // ---- runtime ------------------------------------------------------------

    // True when bluetoothctl is installed.
    static bool is_supported() noexcept {
        return impl::tool_in_path("bluetoothctl");
    }

    // Fast path: sysfs answers in microseconds. `bluetoothctl list` can hang
    // when no hardware is present.
    static bool has_adapter() noexcept {
        DIR* d = ::opendir("/sys/class/bluetooth");
        if (!d) return false;
        bool found = false;
        struct dirent* e;
        while ((e = ::readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            // "hci0" is an adapter; "hci0:256" is a connection handle.
            if (::strncmp(e->d_name, "hci", 3) == 0 && !::strchr(e->d_name, ':')) {
                found = true;
                break;
            }
        }
        ::closedir(d);
        return found;
    }

    // ---- adapter ------------------------------------------------------------

    static result<knst_vector<adapter_info>> adapters() noexcept {
        using R = result<knst_vector<adapter_info>>;
        if (!has_adapter()) return R::success(knst_vector<adapter_info>());

        knst_dev::argv a; a << "list";
        auto t = impl::run_btctl(a, 8000);
        if (!t.ok()) return R::fail(t.st);

        knst_vector<knst_c16string> macs;
        knst_dev::for_each_line(knst_dev::text(t.text), [&](const char* line, size_t len) {
            char buf[512];
            char* c = impl::prep_line(line, len, buf, sizeof(buf));
            if (::strncmp(c, "Controller ", 11) != 0) return;
            char mac[32] = {0};
            if (::sscanf(c + 11, "%31s", mac) != 1) return;
            knst_c16string m;
            if (impl::normalize_mac(knst_c16string(mac), m)) macs.push_back(std::move(m));
        });

        knst_vector<adapter_info> out;
        for (auto& m : macs) {
            auto ai = adapter(m);
            if (ai) {
                out.push_back(std::move(ai.value));
            } else {
                adapter_info fb;
                fb.address = m;
                fb.valid = true;
                out.push_back(std::move(fb));
            }
        }
        return R::success(std::move(out));
    }

    // Default adapter.
    static result<adapter_info> adapter() noexcept {
        if (!has_adapter()) return result<adapter_info>::fail(status::NotFound);
        knst_dev::argv a; a << "show";
        return query_adapter(a);
    }

    // Specific adapter by address.
    static result<adapter_info> adapter(const knst_c16string& addr) noexcept {
        knst_c16string mac;
        if (!impl::normalize_mac(addr, mac))
            return result<adapter_info>::fail(status::InvalidArgument);
        knst_dev::argv a; a << "show" << mac;
        return query_adapter(a);
    }

    static result<void> set_power(bool on) noexcept {
        auto get = [](const adapter_info& a) { return a.powered; };
        auto r = set_adapter_flag("power", on, get, 8000);
        if (!r && on && r.st != status::ToolMissing) {
            // Most common reason for a failed power-on: soft-blocked by rfkill.
            knst_dev::argv k; k << "unblock" << "bluetooth";
            knst_dev::run("rfkill", k, 3000);
            r = set_adapter_flag("power", on, get, 8000);
        }
        return r;
    }

    static result<void> set_discoverable(bool on) noexcept {
        return set_adapter_flag("discoverable", on,
                                [](const adapter_info& a) { return a.discoverable; }, 5000);
    }

    static result<void> set_pairable(bool on) noexcept {
        return set_adapter_flag("pairable", on,
                                [](const adapter_info& a) { return a.pairable; }, 5000);
    }

    static result<void> set_adapter_name(const knst_c16string& name) noexcept {
        using R = result<void>;
        if (name.empty()) return R::fail(status::InvalidArgument);

        // BlueZ limit is 248 *bytes* of UTF-8. A leading '-' would be parsed
        // as an option by bluetoothctl; control chars break the command line.
        knst_byte_string u8 = knst_dev::to_utf8(name);
        const unsigned char* p = u8.data();
        const uint32_t n = u8.length();
        if (!p || n == 0 || n > 248 || p[0] == '-') return R::fail(status::InvalidArgument);
        for (uint32_t i = 0; i < n; ++i)
            if (p[i] < 0x20 || p[i] == 0x7F) return R::fail(status::InvalidArgument);

        if (!has_adapter()) return R::fail(status::NotFound);

        knst_dev::argv a; a << "system-alias" << name;
        auto t = impl::run_btctl(a, 5000);
        if (t.st == status::ToolMissing) return R::fail(t.st);

        status last = status::IoError;
        const bool reached = impl::settle([&]() -> bool {
            auto ad = adapter();
            if (!ad) { last = ad.st; return false; }
            return ad.value.alias == name;
        }, 2500);
        if (reached) return R::success();
        return R::fail(impl::map_output_error(knst_dev::text(t.text),
                                              t.st != status::Ok ? t.st : last), t.exit_code);
    }

    static result<void> rfkill_block(bool block) noexcept {
        knst_dev::argv a; a << (block ? "block" : "unblock") << "bluetooth";
        return knst_dev::drop(knst_dev::run("rfkill", a, 3000));
    }

    // ---- discovery ----------------------------------------------------------

    // Linux discovery only lives as long as the bluetoothctl process, so this
    // is a timed scan that blocks for `seconds` (1..60). Use scan() if you
    // also want the resulting device list.
    static result<void> scan_start(uint32_t seconds = 1) noexcept {
        using R = result<void>;
        if (seconds == 0 || seconds > 60) return R::fail(status::InvalidArgument);
        if (!has_adapter()) return R::fail(status::NotFound);

        knst_dev::argv a;
        a << knst_dev::format("--timeout=%u", seconds) << "scan" << "on";
        auto t = impl::run_btctl(a, seconds * 1000u + 5000u);
        if (t.st == status::ToolMissing) return R::fail(t.st);
        if (::strstr(knst_dev::text(t.text), "Failed to start discovery"))
            return R::fail(impl::map_output_error(knst_dev::text(t.text), status::IoError));
        return t.ok() ? R::success() : R::fail(t.st);
    }

    static result<void> scan_stop() noexcept {
        using R = result<void>;
        if (!has_adapter()) return R::fail(status::NotFound);
        knst_dev::argv a; a << "scan" << "off";
        auto t = impl::run_btctl(a, 3000);
        if (t.st == status::ToolMissing) return R::fail(t.st);

        // `scan off` without an owning session errors out; what matters is
        // that discovery is not running afterwards.
        auto ad = adapter();
        if (!ad) return R::fail(ad.st, ad.native);
        return ad.value.discovering ? R::fail(status::IoError, t.exit_code) : R::success();
    }

    static result<device_list> scan(uint32_t seconds = 8, bool detailed = true) noexcept {
        using R = result<device_list>;
        if (seconds == 0 || seconds > 60) return R::fail(status::InvalidArgument);
        if (!has_adapter()) return R::success(device_list());

        auto ad = adapter();
        if (!ad) return R::fail(ad.st, ad.native);
        if (!ad.value.powered) return R::fail(status::BadState);   // adapter is off

        knst_dev::argv a;
        a << knst_dev::format("--timeout=%u", seconds) << "scan" << "on";
        auto t = impl::run_btctl(a, seconds * 1000u + 5000u);
        if (t.st == status::ToolMissing) return R::fail(t.st);
        if (::strstr(knst_dev::text(t.text), "Failed to start discovery"))
            return R::fail(impl::map_output_error(knst_dev::text(t.text), status::IoError));
        // A Timeout here only means the process had to be killed; the device
        // cache is still valid, so fall through to list().
        return list(detailed);
    }

    static result<device_list> list(bool detailed = true) noexcept {
        if (!has_adapter()) return result<device_list>::success(device_list());
        knst_dev::argv a; a << "devices";
        return collect(a, detailed, false, false);
    }

    static result<device_list> list_paired() noexcept {
        using R = result<device_list>;
        if (!has_adapter()) return R::success(device_list());
        // "devices Paired" (BlueZ >= 5.65). Older versions ignore the filter,
        // so the real flag is always checked via info().
        knst_dev::argv a; a << "devices" << "Paired";
        auto all = collect(a, true, true, false);
        if (!all) return all;
        device_list out;
        for (auto& d : all.value) if (d.paired) out.push_back(std::move(d));
        return R::success(std::move(out));
    }

    static result<device_list> list_connected() noexcept {
        using R = result<device_list>;
        if (!has_adapter()) return R::success(device_list());
        knst_dev::argv a; a << "devices" << "Connected";
        auto all = collect(a, true, false, true);
        if (!all) return all;
        device_list out;
        for (auto& d : all.value) if (d.connected) out.push_back(std::move(d));
        return R::success(std::move(out));
    }

    // ---- info ---------------------------------------------------------------

    static result<knst_bluetooth_device> info(const knst_c16string& addr) noexcept {
        using R = result<knst_bluetooth_device>;
        auto d = query_device(addr);
        if (!d) return R::fail(d.st, d.native);
        return R::success(std::move(d.value.basic));
    }

    static result<device_detail> detail(const knst_c16string& addr) noexcept {
        return query_device(addr);
    }

    static result<int32_t> battery(const knst_c16string& addr) noexcept {
        using R = result<int32_t>;
        auto d = query_device(addr);
        if (!d) return R::fail(d.st, d.native);
        if (d.value.battery_percent < 0) return R::fail(status::NotFound);
        return R::success(d.value.battery_percent);
    }

    // RSSI is only reported by BlueZ while the adapter is discovering;
    // otherwise NotFound is returned.
    static result<int32_t> rssi(const knst_c16string& addr) noexcept {
        using R = result<int32_t>;
        auto d = query_device(addr);
        if (!d) return R::fail(d.st, d.native);
        if (d.value.rssi_dbm == 0) return R::fail(status::NotFound);
        return R::success(d.value.rssi_dbm);
    }

    // ---- wait helpers -------------------------------------------------------

    // Does not start a scan; call scan() first for devices that are not cached yet.
    static result<void> wait_for_device(const knst_c16string& addr,
                                         uint32_t timeout_ms = 10000) noexcept {
        return impl::wait_until([&]() -> result<void> {
            auto r = info(addr);
            if (!r) return result<void>::fail(r.st, r.native);
            return result<void>::success();
        }, timeout_ms);
    }

    static result<void> wait_for_connection(const knst_c16string& addr,
                                             bool want_connected = true,
                                             uint32_t timeout_ms = 15000) noexcept {
        return impl::wait_connection([&]() { return info(addr); }, want_connected, timeout_ms);
    }

    // ---- device operations --------------------------------------------------
    // All of them require the device to be known to BlueZ (scan first) and
    // are idempotent.

    static result<void> pair(const knst_c16string& addr, uint32_t timeout_s = 30) noexcept {
        return op_verified(addr, "pair", timeout_s, 3000,
                           [](const device_detail& d) { return d.basic.paired; });
    }

    static result<void> trust(const knst_c16string& addr) noexcept {
        return op_verified(addr, "trust", 8, 2000,
                           [](const device_detail& d) { return d.basic.trusted; });
    }

    static result<void> untrust(const knst_c16string& addr) noexcept {
        return op_verified(addr, "untrust", 8, 2000,
                           [](const device_detail& d) { return !d.basic.trusted; });
    }

    static result<void> connect(const knst_c16string& addr, uint32_t timeout_s = 20) noexcept {
        return op_verified(addr, "connect", timeout_s, 3000,
                           [](const device_detail& d) { return d.basic.connected; });
    }

    static result<void> disconnect(const knst_c16string& addr) noexcept {
        return op_verified(addr, "disconnect", 10, 3000,
                           [](const device_detail& d) { return !d.basic.connected; });
    }

    static result<void> block(const knst_c16string& addr, bool blocked) noexcept {
        if (blocked)
            return op_verified(addr, "block", 5, 2000,
                               [](const device_detail& d) { return d.blocked; });
        return op_verified(addr, "unblock", 5, 2000,
                           [](const device_detail& d) { return !d.blocked; });
    }

    // Unknown device == already removed -> success.
    static result<void> remove(const knst_c16string& addr) noexcept {
        using R = result<void>;
        knst_c16string mac;
        if (!impl::normalize_mac(addr, mac)) return R::fail(status::InvalidArgument);
        if (!has_adapter()) return R::fail(status::NotFound);

        auto pre = query_device(mac);
        if (!pre) {
            if (pre.st == status::NotFound) return R::success();
            return R::fail(pre.st, pre.native);
        }

        knst_dev::argv a; a << "remove" << mac;
        auto t = impl::run_btctl(a, 10000);
        if (t.st == status::ToolMissing) return R::fail(t.st);

        const bool gone = impl::settle([&]() -> bool {
            auto d = query_device(mac);
            return !d && d.st == status::NotFound;
        }, 3000);
        if (gone) return R::success();
        return R::fail(impl::map_output_error(knst_dev::text(t.text),
                                              t.st != status::Ok ? t.st : status::IoError),
                       t.exit_code);
    }

    static result<void> pair_and_connect(const knst_c16string& addr) noexcept {
        auto p = pair(addr);
        if (!p) {
            auto i = info(addr);
            if (!(i && i.value.paired)) return p;
        }
        // Trust is best-effort: a device can work without it, it just will
        // not reconnect automatically.
        auto tr = trust(addr);
        (void)tr;
        return connect(addr);
    }

    static result<void> reconnect(const knst_c16string& addr) noexcept {
        // Best-effort disconnect first — already-disconnected is fine.
        auto d = disconnect(addr);
        (void)d;
        // Give the driver a moment to actually drop the link before
        // asking it to reconnect.
        wait_for_connection(addr, false, 5000);
        return connect(addr, 20);
    }

    // PIN pairing: best-effort. Just Works and already-trusted devices
    // work reliably; devices that need a real user-side confirmation
    // (e.g. some keyboards showing a passkey) may need manual bluetoothctl.
    //
    // bluetoothctl has no "pincode" command: the PIN must be typed when its
    // agent prompts for it. The script therefore registers an agent, starts
    // pairing, feeds the PIN (and a "yes" for passkey confirmations) after
    // a short delay and polls until the device reports Paired.
    // mac / pin / wait are passed as positional parameters, never
    // interpolated into the script text.
    static result<void> pair_with_pin(const knst_c16string& addr,
                                      const knst_c16string& pin,
                                      uint32_t timeout_s = 30) noexcept {
        using R = result<void>;
        knst_c16string mac;
        if (!impl::normalize_mac(addr, mac) || !knst_dev::is_safe_token(pin, 16) ||
            timeout_s < 5 || timeout_s > 120)
            return R::fail(status::InvalidArgument);
        if (!has_adapter()) return R::fail(status::NotFound);

        auto pre = query_device(mac);
        if (!pre) return R::fail(pre.st, pre.native);
        if (pre.value.basic.paired) return R::success();

        static const char kScript[] = R"BTS(
mac="$1"; pin="$2"; wait_s="$3"
{
  printf 'agent KeyboardDisplay\n'
  printf 'default-agent\n'
  printf 'pair %s\n' "$mac"
  sleep 4
  printf '%s\n' "$pin"
  sleep 1
  printf 'yes\n'
  i=0
  while [ "$i" -lt "$wait_s" ]; do
    sleep 1
    i=$((i+1))
    bluetoothctl info "$mac" 2>/dev/null | grep -q 'Paired: yes' && break
  done
  printf 'quit\n'
} | bluetoothctl
)BTS";

        knst_dev::argv a;
        a << "-c" << kScript << "sh" << mac << pin << static_cast<unsigned int>(timeout_s);
        auto t = knst_dev::run_tool_raw("sh", a, timeout_s * 1000u + 8000u);
        if (!t.launched) return R::fail(impl::tool_in_path("sh") ? status::Timeout
                                                                  : status::ToolMissing);

        status last = status::IoError;
        const bool paired = impl::settle([&]() -> bool {
            auto d = query_device(mac);
            if (!d) { last = d.st; return false; }
            return d.value.basic.paired;
        }, 2000);
        return paired ? R::success() : R::fail(last, t.exit_code);
    }

private:
    static result<adapter_info> query_adapter(const knst_dev::argv& a) noexcept {
        using R = result<adapter_info>;
        auto t = impl::run_btctl(a, 8000);
        if (!t.ok()) return R::fail(t.st);
        auto r = impl::parse_adapter(t.text);
        if (!r) return R::fail(impl::map_output_error(knst_dev::text(t.text), r.st), t.exit_code);
        return r;
    }

    static result<device_detail> query_device(const knst_c16string& addr) noexcept {
        using R = result<device_detail>;
        knst_c16string mac;
        if (!impl::normalize_mac(addr, mac)) return R::fail(status::InvalidArgument);
        if (!has_adapter()) return R::fail(status::NotFound);   // no adapter -> no device

        knst_dev::argv a; a << "info" << mac;
        auto t = impl::run_btctl(a, 5000);
        if (!t.ok()) return R::fail(t.st);

        device_detail dd;
        if (!impl::parse_device(mac, t.text, dd))
            return R::fail(impl::map_output_error(knst_dev::text(t.text), status::NotFound),
                           t.exit_code);
        return R::success(std::move(dd));
    }

    // Runs a "devices ..." style command. When `detailed`, each entry is
    // enriched with an info() call; entries whose info() fails fall back to
    // the name from the listing (and the mark_* flags implied by the filter).
    static result<device_list> collect(const knst_dev::argv& a, bool detailed,
                                       bool mark_paired, bool mark_connected) noexcept {
        using R = result<device_list>;
        auto t = impl::run_btctl(a, 5000);
        if (!t.ok()) return R::fail(t.st);

        device_list devices;
        knst_dev::for_each_line(knst_dev::text(t.text), [&](const char* line, size_t len) {
            char buf[512];
            char* c = impl::prep_line(line, len, buf, sizeof(buf));
            if (::strncmp(c, "Device ", 7) != 0) return;     // also skips [NEW]/[CHG] noise

            char mac[32] = {0};
            if (::sscanf(c + 7, "%31s", mac) != 1) return;
            knst_c16string ad;
            if (!impl::normalize_mac(knst_c16string(mac), ad)) return;

            if (detailed) {
                auto i = info(ad);
                if (i) { devices.push_back(std::move(i.value)); return; }
            }

            knst_bluetooth_device d;
            d.address   = ad;
            d.paired    = mark_paired;
            d.connected = mark_connected;
            d.valid     = true;
            const char* nm = ::strchr(c + 7, ' ');
            if (nm) {
                ++nm;
                while (*nm == ' ') ++nm;
                if (*nm) d.name = knst_c16string(nm);
            }
            devices.push_back(std::move(d));
        });
        return R::success(std::move(devices));
    }

    // Sets an on/off adapter property and verifies it on the adapter.
    template<typename Getter>
    static result<void> set_adapter_flag(const char* cmd, bool on, Getter get,
                                          uint32_t timeout_ms) noexcept {
        using R = result<void>;
        if (!has_adapter()) return R::fail(status::NotFound);

        knst_dev::argv a; a << cmd << (on ? "on" : "off");
        auto t = impl::run_btctl(a, timeout_ms);
        if (t.st == status::ToolMissing) return R::fail(t.st);

        status last = status::IoError;
        const bool reached = impl::settle([&]() -> bool {
            auto ad = adapter();
            if (!ad) { last = ad.st; return false; }
            return get(ad.value) == on;
        }, 2500);
        if (reached) return R::success();
        return R::fail(impl::map_output_error(knst_dev::text(t.text),
                                              t.st != status::Ok ? t.st : last), t.exit_code);
    }

    // Runs `bluetoothctl <cmd> <mac>` and verifies the resulting device state.
    template<typename Pred>
    static result<void> op_verified(const knst_c16string& addr, const char* cmd,
                                     uint32_t timeout_s, uint32_t settle_ms, Pred ok) noexcept {
        using R = result<void>;
        knst_c16string mac;
        if (!impl::normalize_mac(addr, mac) || timeout_s == 0 || timeout_s > 120)
            return R::fail(status::InvalidArgument);
        if (!has_adapter()) return R::fail(status::NotFound);

        // The device must be known to BlueZ; this also avoids a long timeout
        // for a device that is simply not there.
        auto pre = query_device(mac);
        if (!pre) return R::fail(pre.st, pre.native);
        if (ok(pre.value)) return R::success();                  // idempotent

        knst_dev::argv a; a << cmd << mac;
        auto t = impl::run_btctl(a, timeout_s * 1000u);
        if (t.st == status::ToolMissing) return R::fail(t.st);

        status last = status::IoError;
        const bool reached = impl::settle([&]() -> bool {
            auto d = query_device(mac);
            if (!d) { last = d.st; return false; }
            return ok(d.value);
        }, settle_ms);
        if (reached) return R::success();
        return R::fail(impl::map_output_error(knst_dev::text(t.text),
                                              t.st != status::Ok ? t.st : last), t.exit_code);
    }
};

// ============================================================
// Windows (Win32 Bluetooth)
// ============================================================

#elif KNST_USING_PLATFORM_WINDOWS

namespace impl {

inline bool parse_addr(const knst_c16string& s, uint64_t& out) noexcept {
    if (!knst_dev::is_mac(s)) return false;
    uint64_t v = 0;
    for (uint32_t i = 0; i < 17; i += 3) {
        unsigned b = 0;
        for (uint32_t k = 0; k < 2; ++k) {
            const char16_t c = s[i + k];
            const unsigned x = (c <= u'9') ? static_cast<unsigned>(c - u'0')
                                           : static_cast<unsigned>((c | 0x20) - u'a' + 10);
            b = b * 16 + x;
        }
        v = (v << 8) | b;
    }
    out = v;
    return true;
}

inline knst_c16string addr_to_string(const BLUETOOTH_ADDRESS& a) noexcept {
    return knst_dev::format("%02X:%02X:%02X:%02X:%02X:%02X",
        a.rgBytes[5], a.rgBytes[4], a.rgBytes[3],
        a.rgBytes[2], a.rgBytes[1], a.rgBytes[0]);
}

// Caller owns (and must CloseHandle) `h` on success.
inline bool first_radio(HANDLE& h) noexcept {
    BLUETOOTH_FIND_RADIO_PARAMS p; p.dwSize = sizeof(p);
    HBLUETOOTH_RADIO_FIND f = ::BluetoothFindFirstRadio(&p, &h);
    if (!f) return false;
    ::BluetoothFindRadioClose(f);
    return true;
}

constexpr GUID svc(uint32_t short_uuid) noexcept {
    return GUID{ short_uuid, 0x0000, 0x1000,
                 { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
}

inline status win_status(DWORD e) noexcept {
    switch (e) {
        case ERROR_SUCCESS:                return status::Ok;
        case ERROR_ACCESS_DENIED:          return status::PermissionDenied;
        case ERROR_NOT_FOUND:              return status::NotFound;
        case ERROR_NOT_SUPPORTED:
        case ERROR_SERVICE_DOES_NOT_EXIST: return status::NotSupported;
        case ERROR_INVALID_PARAMETER:      return status::InvalidArgument;
        case ERROR_TIMEOUT:
        case WAIT_TIMEOUT:                 return status::Timeout;
        case ERROR_CANCELLED:              return status::IoError;
        case ERROR_DEVICE_NOT_CONNECTED:   return status::BadState;
        default:                           return status::Unknown;
    }
}

template<typename T>
inline result<T> win_fail(DWORD e) noexcept {
    return result<T>::fail(win_status(e), static_cast<int>(e));
}

inline knst_bluetooth_device to_dev(const BLUETOOTH_DEVICE_INFO& di) noexcept {
    knst_bluetooth_device d;
    d.name      = knst_c16string(reinterpret_cast<const char16_t*>(di.szName));
    d.address   = addr_to_string(di.Address);
    d.connected = di.fConnected     != FALSE;
    d.paired    = di.fAuthenticated != FALSE;
    d.trusted   = di.fRemembered    != FALSE;
    d.type      = class_major(di.ulClassofDevice);
    d.valid     = true;
    return d;
}

} // namespace impl


class bluetooth {
public:
    using device_list = knst_vector<knst_bluetooth_device>;

    static bool is_supported() noexcept {
        HANDLE h = nullptr;
        if (!impl::first_radio(h)) return false;
        ::CloseHandle(h);
        return true;
    }

    static bool has_adapter() noexcept { return is_supported(); }

    // ---- adapter ------------------------------------------------------------

    static result<knst_vector<adapter_info>> adapters() noexcept {
        using R = result<knst_vector<adapter_info>>;
        auto a = adapter();
        if (!a) {
            if (a.st == status::NotFound) return R::success(knst_vector<adapter_info>());
            return R::fail(a.st, a.native);
        }
        knst_vector<adapter_info> out;
        out.push_back(std::move(a.value));
        return R::success(std::move(out));
    }

    static result<adapter_info> adapter() noexcept {
        using R = result<adapter_info>;
        HANDLE h = nullptr;
        if (!impl::first_radio(h)) return R::fail(status::NotFound);

        BLUETOOTH_RADIO_INFO ri = {};
        ri.dwSize = sizeof(ri);
        const DWORD e = ::BluetoothGetRadioInfo(h, &ri);
        const bool disc = ::BluetoothIsDiscoverable(h) != FALSE;
        const bool conn = ::BluetoothIsConnectable(h)  != FALSE;
        ::CloseHandle(h);
        if (e != ERROR_SUCCESS) return impl::win_fail<adapter_info>(e);

        adapter_info a;
        a.address      = impl::addr_to_string(ri.address);
        a.name         = knst_c16string(reinterpret_cast<const char16_t*>(ri.szName));
        a.alias        = a.name;
        a.powered      = true;     // if we can query the radio, it is on
        a.discoverable = disc;
        a.pairable     = conn;     // "connectable" is the closest Win32 notion
        a.valid        = true;
        return R::success(std::move(a));
    }

    static result<void> set_power(bool) noexcept {
        // Win32 bthprops.h has no true radio enable/disable. Only these are
        // available: BluetoothEnableDiscovery, BluetoothEnableIncomingConnections.
        // Those stop NEW connections but do NOT power off the radio — the
        // device still transmits. Real power control needs one of:
        //   - WinRT: Windows.Devices.Radios (C++/WinRT, extra dependency)
        //   - SetupAPI: disable the BT device via DIF_PROPERTYCHANGE (admin)
        // Neither is compatible with a header-only library, so return
        // NotSupported honestly. Callers wanting a "soft" off should call
        // set_discoverable(false) + set_pairable(false) themselves.
        return result<void>::fail(status::NotSupported);
    }

    static result<void> set_discoverable(bool on) noexcept {
        HANDLE h = nullptr;
        if (!impl::first_radio(h)) return result<void>::fail(status::NotFound);
        const BOOL r = ::BluetoothEnableDiscovery(h, on ? TRUE : FALSE);
        const DWORD e = r ? 0 : ::GetLastError();
        ::CloseHandle(h);
        if (r) return result<void>::success();
        return impl::win_fail<void>(e ? e : ERROR_ACCESS_DENIED);
    }

    static result<void> set_pairable(bool on) noexcept {
        HANDLE h = nullptr;
        if (!impl::first_radio(h)) return result<void>::fail(status::NotFound);
        // Win32 has no "pairable" flag — BluetoothEnableIncomingConnections
        // is the closest: it toggles whether the radio accepts new pairings.
        BOOL ok = ::BluetoothEnableIncomingConnections(h, on ? TRUE : FALSE);
        DWORD e = ok ? ERROR_SUCCESS : ::GetLastError();
        ::CloseHandle(h);
        return ok ? result<void>::success() : impl::win_fail<void>(e);
    }
    static result<void> set_adapter_name(const knst_c16string&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> rfkill_block(bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }

    // ---- discovery ----------------------------------------------------------

    static result<void> scan_start(uint32_t = 1) noexcept { return result<void>::success(); }
    static result<void> scan_stop()  noexcept { return result<void>::success(); }

    static result<device_list> scan(uint32_t seconds = 8, bool = true) noexcept {
        return enumerate(true, seconds);
    }

    static result<device_list> list(bool = true) noexcept {
        return enumerate(false, 0);
    }

    static result<device_list> list_paired() noexcept {
        using R = result<device_list>;
        auto all = list();
        if (!all) return all;
        device_list out;
        for (auto& d : all.value) if (d.paired) out.push_back(std::move(d));
        return R::success(std::move(out));
    }

    static result<device_list> list_connected() noexcept {
        using R = result<device_list>;
        auto all = list();
        if (!all) return all;
        device_list out;
        for (auto& d : all.value) if (d.connected) out.push_back(std::move(d));
        return R::success(std::move(out));
    }

    // ---- info ---------------------------------------------------------------

    static result<knst_bluetooth_device> info(const knst_c16string& addr) noexcept {
        using R = result<knst_bluetooth_device>;
        uint64_t a = 0;
        if (!impl::parse_addr(addr, a)) return R::fail(status::InvalidArgument);
        HANDLE h = nullptr;
        if (!impl::first_radio(h)) return R::fail(status::NotFound);

        BLUETOOTH_DEVICE_INFO di = {};
        di.dwSize = sizeof(di);
        di.Address.ullLong = a;
        const DWORD e = ::BluetoothGetDeviceInfo(h, &di);
        ::CloseHandle(h);
        if (e != ERROR_SUCCESS) return impl::win_fail<knst_bluetooth_device>(e);
        return R::success(impl::to_dev(di));
    }

    static result<device_detail> detail(const knst_c16string& addr) noexcept {
        using R = result<device_detail>;
        auto bi = info(addr);
        if (!bi) return R::fail(bi.st, bi.native);
        device_detail dd;
        dd.basic = std::move(bi.value);
        dd.device_class = dd.basic.type;
        // Windows does not expose battery/RSSI via the basic Win32 BT API.
        dd.battery_percent = -1;
        dd.valid = true;
        return R::success(std::move(dd));
    }

    static result<int32_t> battery(const knst_c16string&) noexcept {
        return result<int32_t>::fail(status::NotSupported);
    }
    static result<int32_t> rssi(const knst_c16string&) noexcept {
        return result<int32_t>::fail(status::NotSupported);
    }

    // ---- wait helpers -------------------------------------------------------

    static result<void> wait_for_device(const knst_c16string& addr,
                                         uint32_t timeout_ms = 10000) noexcept {
        return impl::wait_until([&]() -> result<void> {
            auto r = info(addr);
            if (!r) return result<void>::fail(r.st, r.native);
            return result<void>::success();
        }, timeout_ms);
    }

    static result<void> wait_for_connection(const knst_c16string& addr,
                                             bool want_connected = true,
                                             uint32_t timeout_ms = 15000) noexcept {
        return impl::wait_connection([&]() { return info(addr); }, want_connected, timeout_ms);
    }

    // ---- device operations --------------------------------------------------
    // Shows the system pairing dialog. Already-paired devices succeed at once.
    static result<void> pair(const knst_c16string& addr, uint32_t = 30) noexcept {
        return with_device(addr, [](HANDLE h, BLUETOOTH_DEVICE_INFO& di) -> DWORD {
            if (di.fAuthenticated) return ERROR_SUCCESS;
            const DWORD r = ::BluetoothAuthenticateDeviceEx(
                nullptr, h, &di, nullptr, MITMProtectionNotRequired);
            // ERROR_NO_MORE_ITEMS: device is already authenticated.
            return r == ERROR_NO_MORE_ITEMS ? ERROR_SUCCESS : r;
        });
    }

private:
    // PIN-based authentication uses Windows' Bluetooth authentication callback.
    // The callback is invoked by the driver when the remote device requests a
    // PIN; we fill the response with the caller-supplied PIN.
    struct pin_auth_ctx {
        volatile LONG done   = 0;
        DWORD         result = ERROR_SUCCESS;
        knst_byte_string pin;
    };

    static BOOL CALLBACK pin_auth_cb(LPVOID param, PBLUETOOTH_AUTHENTICATION_CALLBACK_PARAMS p) {
        auto* s = static_cast<pin_auth_ctx*>(param);
        if (!s || !p) return FALSE;

        BLUETOOTH_AUTHENTICATE_RESPONSE r = {};
        r.bthAddressRemote = p->deviceInfo.Address;
        r.authMethod       = p->authenticationMethod;

                switch (static_cast<DWORD>(p->authenticationMethod)) {
            case 0x01: {  // BLUETOOTH_AUTHENTICATION_METHOD_LEGACY
                if (s->pin.length() > BTH_MAX_PIN_SIZE) {
                    s->result = ERROR_INVALID_PARAMETER;
                    InterlockedExchange(&s->done, 1);
                    return FALSE;
                }
                r.negativeResponse = FALSE;
                r.pinInfo.pinLength = static_cast<DWORD>(s->pin.length());
                if (s->pin.length() > 0)
                    ::memcpy(r.pinInfo.pin, s->pin.data(), s->pin.length());
                break;
            }
            case 0x02:  // BLUETOOTH_AUTHENTICATION_METHOD_NUMERIC_COMPARISON
            case 0x04:  // BLUETOOTH_AUTHENTICATION_METHOD_JUST_WORKS (= PASSKEY per MSDN)
            case 0x06:  // BLUETOOTH_AUTHENTICATION_METHOD_JUST_WORKS_NO_MITM
                r.negativeResponse = FALSE;
                break;
            default:
                r.negativeResponse = TRUE;
                break;
        }

        DWORD e = ::BluetoothSendAuthenticationResponseEx(nullptr, &r);
        s->result = e;
        InterlockedExchange(&s->done, 1);
        return e == ERROR_SUCCESS ? TRUE : FALSE;
    }

public:
    static result<void> pair_with_pin(const knst_c16string& addr, const knst_c16string& pin,
                                       uint32_t timeout_s = 30) noexcept {
        using R = result<void>;
        if (timeout_s < 5 || timeout_s > 120) return R::fail(status::InvalidArgument);

        knst_byte_string pin8 = knst_dev::to_utf8(pin);
        if (pin8.empty() || pin8.length() > BTH_MAX_PIN_SIZE)
            return R::fail(status::InvalidArgument);

        uint64_t a = 0;
        if (!impl::parse_addr(addr, a)) return R::fail(status::InvalidArgument);

        HANDLE radio = nullptr;
        if (!impl::first_radio(radio)) return R::fail(status::NotFound);

        BLUETOOTH_DEVICE_INFO di = {};
        di.dwSize = sizeof(di);
        di.Address.ullLong = a;

        DWORD e = ::BluetoothGetDeviceInfo(radio, &di);
        if (e != ERROR_SUCCESS) { ::CloseHandle(radio); return impl::win_fail<void>(e); }
        if (di.fAuthenticated) { ::CloseHandle(radio); return R::success(); }

        pin_auth_ctx ctx;
        ctx.pin = pin8;

                HBLUETOOTH_AUTHENTICATION_REGISTRATION reg = nullptr;
        e = ::BluetoothRegisterForAuthenticationEx(&di, &reg, &pin_auth_cb, &ctx);
        if (e != ERROR_SUCCESS) { ::CloseHandle(radio); return impl::win_fail<void>(e); }

        e = ::BluetoothAuthenticateDeviceEx(nullptr, radio, &di, nullptr,
                                             MITMProtectionNotRequired);
        // Not OK is not fatal; the callback may still complete the pairing.

        const uint64_t deadline = impl::now_ms() + timeout_s * 1000ull;
        while (impl::now_ms() < deadline) {
            if (InterlockedCompareExchange(&ctx.done, 1, 1) == 1) break;
            impl::sleep_ms(100);
            BLUETOOTH_DEVICE_INFO cur = di;
            if (::BluetoothGetDeviceInfo(radio, &cur) == ERROR_SUCCESS && cur.fAuthenticated) {
                ctx.result = ERROR_SUCCESS;
                break;
            }
        }

        ::BluetoothUnregisterAuthentication(reg);
        ::CloseHandle(radio);

        if (ctx.result == ERROR_SUCCESS) {
            auto chk = info(addr);
            if (chk && chk.value.paired) return R::success();
        }
        return R::fail(ctx.result == ERROR_SUCCESS ? status::Timeout
                                                   : impl::win_status(ctx.result),
                       static_cast<int>(ctx.result));
    }
    static result<void> trust(const knst_c16string&) noexcept {
        return result<void>::success();
    }
    static result<void> untrust(const knst_c16string&) noexcept {
        return result<void>::success();
    }

    // Unknown device == already removed -> success.
    static result<void> remove(const knst_c16string& addr) noexcept {
        uint64_t a = 0;
        if (!impl::parse_addr(addr, a))
            return result<void>::fail(status::InvalidArgument);
        BLUETOOTH_ADDRESS ba; ba.ullLong = a;
        const DWORD e = ::BluetoothRemoveDevice(&ba);
        if (e == ERROR_SUCCESS || e == ERROR_NOT_FOUND) return result<void>::success();
        return impl::win_fail<void>(e);
    }

    static result<void> connect(const knst_c16string& addr, uint32_t timeout_s = 20) noexcept {
        if (timeout_s == 0 || timeout_s > 120) return result<void>::fail(status::InvalidArgument);
        auto r = services(addr, BLUETOOTH_SERVICE_ENABLE);
        if (!r) return r;
        return wait_for_connection(addr, true, timeout_s * 1000u);
    }

    static result<void> disconnect(const knst_c16string& addr) noexcept {
        auto r = services(addr, BLUETOOTH_SERVICE_DISABLE);
        if (!r) return r;
        return wait_for_connection(addr, false, 5000);
    }

    static result<void> block(const knst_c16string&, bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }

    static result<void> pair_and_connect(const knst_c16string& addr) noexcept {
        auto r = pair(addr);
        if (!r) return r;
        return connect(addr);
    }

    static result<void> reconnect(const knst_c16string& addr) noexcept {
        auto d = disconnect(addr);
        (void)d;
        wait_for_connection(addr, false, 5000);
        return connect(addr, 20);
    }

private:
        static result<device_list> enumerate(bool inquiry, uint32_t seconds) noexcept {
        using R = result<device_list>;
        if (inquiry && (seconds == 0 || seconds > 60))
            return R::fail(status::InvalidArgument);

        HANDLE h = nullptr;
       
        if (!impl::first_radio(h))
            return R::success(device_list());

        // cTimeoutMultiplier is in units of 1.28 s (max 48).
        uint32_t mult = 1;
        if (inquiry) {
            mult = (seconds * 100u / 128u) + 1u;
            if (mult > 48u) mult = 48u;
        }

        BLUETOOTH_DEVICE_SEARCH_PARAMS sp = {};
        sp.dwSize = sizeof(sp);
        sp.fReturnAuthenticated = TRUE;
        sp.fReturnRemembered = TRUE;
        sp.fReturnConnected = TRUE;
        sp.fReturnUnknown = inquiry ? TRUE : FALSE;
        sp.fIssueInquiry = inquiry ? TRUE : FALSE;
        sp.cTimeoutMultiplier = static_cast<UCHAR>(mult);
        sp.hRadio = h;

        BLUETOOTH_DEVICE_INFO di = {};
        di.dwSize = sizeof(di);

        device_list devices;
        HBLUETOOTH_DEVICE_FIND f = ::BluetoothFindFirstDevice(&sp, &di);
        if (f) {
            do { devices.push_back(impl::to_dev(di)); }
            while (::BluetoothFindNextDevice(f, &di));
            ::BluetoothFindDeviceClose(f);
        }
        ::CloseHandle(h);
        return R::success(std::move(devices));
    }

    // fn(HANDLE, BLUETOOTH_DEVICE_INFO&) must return a Win32 error code.
    template<typename F>
    static result<void> with_device(const knst_c16string& addr, F&& fn) noexcept {
        uint64_t a = 0;
        if (!impl::parse_addr(addr, a))
            return result<void>::fail(status::InvalidArgument);
        HANDLE h = nullptr;
        if (!impl::first_radio(h)) return result<void>::fail(status::NotFound);

        BLUETOOTH_DEVICE_INFO di = {};
        di.dwSize = sizeof(di);
        di.Address.ullLong = a;

        DWORD e = ::BluetoothGetDeviceInfo(h, &di);
        if (e == ERROR_SUCCESS) e = fn(h, di);
        ::CloseHandle(h);
        return e == ERROR_SUCCESS ? result<void>::success() : impl::win_fail<void>(e);
    }

    static result<void> services(const knst_c16string& addr, DWORD flag) noexcept {
        return with_device(addr, [flag](HANDLE h, BLUETOOTH_DEVICE_INFO& di) -> DWORD {
            // Coverage:
            //   Audio    — A2DP sink/source, AVRCP, Headset, Handsfree
            //   Input    — HID
            //   Data     — SPP, OBEX Object Push
            //   Network  — PANU, NAP, GN
            //   Phone    — PBAP (client + server), SIM Access
            static const GUID ids[] = {
                impl::svc(0x110B),  // AudioSink (A2DP sink)
                impl::svc(0x110A),  // AudioSource (A2DP source)
                impl::svc(0x110C),  // AVRCP Target
                impl::svc(0x110E),  // AVRCP Controller
                impl::svc(0x1108),  // Headset
                impl::svc(0x1112),  // Headset Audio Gateway
                impl::svc(0x111E),  // Handsfree
                impl::svc(0x111F),  // Handsfree Audio Gateway
                impl::svc(0x1124),  // HID
                impl::svc(0x1101),  // Serial Port (SPP)
                impl::svc(0x1105),  // OBEX Object Push
                impl::svc(0x1115),  // PANU
                impl::svc(0x1116),  // NAP
                impl::svc(0x1117),  // GN
                impl::svc(0x112F),  // PBAP Client
                impl::svc(0x1130),  // PBAP Server
                impl::svc(0x112D),  // SIM Access
            };
            DWORD last = ERROR_SERVICE_DOES_NOT_EXIST;
            bool any = false;
            for (const GUID& g : ids) {
                const DWORD r = ::BluetoothSetServiceState(h, &di, &g, flag);
                if (r == ERROR_SUCCESS) any = true;
                else last = r;
            }
            return any ? ERROR_SUCCESS : last;
        });
    }
};

// ============================================================
// Android (JNI BluetoothAdapter)
// ============================================================

#elif defined(KNST_USING_PLATFORM_ANDROID)

namespace impl_android {

inline JNIEnv* jenv() {
    return knst_devices::detail_android::get_env();
}

inline void clear_exc(JNIEnv* e) {
    if (e && e->ExceptionCheck()) e->ExceptionClear();
}

inline knst_c16string to_jstr_val(JNIEnv* e, jstring s) {
    if (!s) return knst_c16string();
    const char* u = e->GetStringUTFChars(s, nullptr);
    knst_c16string r(u ? u : "");
    if (u) e->ReleaseStringUTFChars(s, u);
    return r;
}

// Load a class through the activity's classloader.
inline jclass find_class(JNIEnv* e, const char* dotted) {
    if (!e || !knst_devices::detail_android::g_activity_obj) return nullptr;
    jclass act = e->GetObjectClass(knst_devices::detail_android::g_activity_obj);
    if (!act) { clear_exc(e); return nullptr; }
    jmethodID gc = e->GetMethodID(act, "getClassLoader",
                                   "()Ljava/lang/ClassLoader;");
    if (!gc) { clear_exc(e); e->DeleteLocalRef(act); return nullptr; }
    jobject cl = e->CallObjectMethod(knst_devices::detail_android::g_activity_obj, gc);
    e->DeleteLocalRef(act);
    if (!cl) { clear_exc(e); return nullptr; }
    jclass clc = e->GetObjectClass(cl);
    jmethodID lc = e->GetMethodID(clc, "loadClass",
                                   "(Ljava/lang/String;)Ljava/lang/Class;");
    jclass r = nullptr;
    if (lc) {
        jstring jn = e->NewStringUTF(dotted);
        r = (jclass)e->CallObjectMethod(cl, lc, jn);
        e->DeleteLocalRef(jn);
    }
    e->DeleteLocalRef(clc);
    e->DeleteLocalRef(cl);
    clear_exc(e);
    return r;
}

inline jclass get_helper(JNIEnv* e) {
    return find_class(e, "com.knst.devices.KnstBluetoothHelper");
}

inline int32_t arr_int(JNIEnv* e, jobjectArray a, int i) {
    jobject o = e->GetObjectArrayElement(a, i);
    if (!o) return 0;
    jclass c = e->GetObjectClass(o);
    jmethodID m = e->GetMethodID(c, "intValue", "()I");
    int32_t v = m ? e->CallIntMethod(o, m) : 0;
    e->DeleteLocalRef(c);
    e->DeleteLocalRef(o);
    clear_exc(e);
    return v;
}

inline knst_c16string arr_str(JNIEnv* e, jobjectArray a, int i) {
    jobject o = e->GetObjectArrayElement(a, i);
    if (!o) return knst_c16string();
    knst_c16string r = to_jstr_val(e, (jstring)o);
    e->DeleteLocalRef(o);
    return r;
}

inline knst_c16string type_str(int t) {
    switch (t) {
        case 1: return u"Classic";
        case 2: return u"LE";
        case 3: return u"Dual";
        default: return u"Unknown";
    }
}

inline void read_device(JNIEnv* e, jobjectArray a, int base,
                        knst_bluetooth_device& d) {
    d.name      = arr_str(e, a, base + 0);
    d.address   = arr_str(e, a, base + 1);
    int type_i  = arr_int(e, a, base + 2);
    int bond_i  = arr_int(e, a, base + 4);
    int conn_i  = arr_int(e, a, base + 5);
    d.type      = type_str(type_i);
    d.paired    = (bond_i == 12);
    d.connected = (conn_i == 1);
    d.trusted   = d.paired;
    d.valid     = true;
}

inline jstring new_addr_str(JNIEnv* e, const knst_c16string& addr) {
    knst_byte_string u = knst_dev::to_utf8(addr);
    return e->NewStringUTF(knst_dev::text(u));
}

} // namespace impl_android


class bluetooth {
public:
    using device_list = knst_vector<knst_bluetooth_device>;

    static bool is_supported() noexcept { return has_adapter(); }
    static bool has_adapter()   noexcept { return static_cast<bool>(adapter()); }

    static result<knst_vector<adapter_info>> adapters() noexcept {
        using R = result<knst_vector<adapter_info>>;
        auto a = adapter();
        if (!a) {
            if (a.st == status::NotFound) return R::success(knst_vector<adapter_info>());
            return R::fail(a.st, a.native);
        }
        knst_vector<adapter_info> out;
        out.push_back(std::move(a.value));
        return R::success(std::move(out));
    }

    static result<adapter_info> adapter() noexcept {
        using R = result<adapter_info>;
        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);
        jclass h = impl_android::get_helper(e);
        if (!h) return R::fail(status::NotSupported);
        jmethodID m = e->GetStaticMethodID(h, "getAdapterInfo",
                                            "()[Ljava/lang/Object;");
        if (!m) { e->DeleteLocalRef(h); impl_android::clear_exc(e);
                  return R::fail(status::NotSupported); }
        jobjectArray a = (jobjectArray)e->CallStaticObjectMethod(h, m);
        e->DeleteLocalRef(h);
        impl_android::clear_exc(e);
        if (!a) return R::fail(status::NotFound);

        adapter_info ai;
        ai.name        = impl_android::arr_str(e, a, 0);
        ai.address     = impl_android::arr_str(e, a, 1);
        ai.alias       = ai.name;
        ai.powered     = impl_android::arr_int(e, a, 2) != 0;
        int sm         = impl_android::arr_int(e, a, 3);
        ai.discoverable = (sm == 23);
        ai.pairable    = ai.discoverable;
        ai.valid       = true;
        e->DeleteLocalRef(a);
        return R::success(std::move(ai));
    }

    static result<adapter_info> adapter(const knst_c16string&) noexcept {
        return adapter();
    }

    static result<void> set_power(bool on) noexcept {
        using R = result<void>;
        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);

        // BluetoothAdapter.enable()/disable() are deprecated in API 33+
        // but still functional on API 24–32. On 33+ they return false;
        // we surface that as NotSupported.
        jclass ba = e->FindClass("android/bluetooth/BluetoothAdapter");
        if (!ba) { impl_android::clear_exc(e); return R::fail(status::NotSupported); }
        jmethodID get = e->GetStaticMethodID(ba, "getDefaultAdapter",
                                             "()Landroid/bluetooth/BluetoothAdapter;");
        if (!get) { e->DeleteLocalRef(ba); impl_android::clear_exc(e);
                    return R::fail(status::NotSupported); }
        jobject adapter = e->CallStaticObjectMethod(ba, get);
        e->DeleteLocalRef(ba);
        impl_android::clear_exc(e);
        if (!adapter) return R::fail(status::NotFound);

        jclass cls = e->GetObjectClass(adapter);
        jmethodID m = e->GetMethodID(cls, on ? "enable" : "disable", "()Z");
        bool ok = false;
        if (m) {
            jboolean r = e->CallBooleanMethod(adapter, m);
            if (e->ExceptionCheck()) { e->ExceptionClear(); r = JNI_FALSE; }
            ok = (r == JNI_TRUE);
        }
        e->DeleteLocalRef(cls);
        e->DeleteLocalRef(adapter);
        impl_android::clear_exc(e);

        if (!ok) return R::fail(status::NotSupported);
        // Poll the adapter state until it settles.
        status last = status::Timeout;
        bool reached = impl::settle([&]() -> bool {
            auto a = adapter_info_result();
            if (!a) { last = a.st; return false; }
            return a.value.powered == on;
        }, 5000);
        return reached ? R::success() : R::fail(last);
    }

private:
    // Helper — returns the adapter_info as result<adapter_info>; used by
    // set_power to poll the current state. Named differently from the public
    // adapter() to avoid ambiguity inside the settle() lambda.
    static result<adapter_info> adapter_info_result() noexcept {
        return adapter();
    }

public:
    static result<void> set_discoverable(bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> set_pairable(bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> set_adapter_name(const knst_c16string& name) noexcept {
        using R = result<void>;
        if (name.empty()) return R::fail(status::InvalidArgument);
        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);

        jclass ba = e->FindClass("android/bluetooth/BluetoothAdapter");
        if (!ba) { impl_android::clear_exc(e); return R::fail(status::NotSupported); }
        jmethodID get = e->GetStaticMethodID(ba, "getDefaultAdapter",
                                             "()Landroid/bluetooth/BluetoothAdapter;");
        if (!get) { e->DeleteLocalRef(ba); impl_android::clear_exc(e);
                    return R::fail(status::NotSupported); }
        jobject adapter = e->CallStaticObjectMethod(ba, get);
        e->DeleteLocalRef(ba);
        impl_android::clear_exc(e);
        if (!adapter) return R::fail(status::NotFound);

        jclass cls = e->GetObjectClass(adapter);
        jmethodID setn = e->GetMethodID(cls, "setName", "(Ljava/lang/String;)Z");
        bool ok = false;
        if (setn) {
            knst_byte_string n8 = knst_dev::to_utf8(name);
            jstring js = e->NewStringUTF(knst_dev::text(n8));
            jboolean r = e->CallBooleanMethod(adapter, setn, js);
            if (e->ExceptionCheck()) { e->ExceptionClear(); r = JNI_FALSE; }
            e->DeleteLocalRef(js);
            ok = (r == JNI_TRUE);
        }
        e->DeleteLocalRef(cls);
        e->DeleteLocalRef(adapter);
        impl_android::clear_exc(e);
        return ok ? R::success() : R::fail(status::IoError);
    }
    static result<void> rfkill_block(bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }

    // ---- scan ----------------------------------------------------------

    static result<void> scan_start(uint32_t = 1) noexcept {
        using R = result<void>;
        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);
        jclass h = impl_android::get_helper(e);
        if (!h) return R::fail(status::NotSupported);
        jmethodID m = e->GetStaticMethodID(h, "startDiscovery", "()I");
        if (!m) { e->DeleteLocalRef(h); impl_android::clear_exc(e);
                  return R::fail(status::NotSupported); }
        jint r = e->CallStaticIntMethod(h, m);
        e->DeleteLocalRef(h);
        impl_android::clear_exc(e);
        return r != 0 ? R::success() : R::fail(status::BadState);
    }

    static result<void> scan_stop() noexcept {
        JNIEnv* e = impl_android::jenv();
        if (!e) return result<void>::fail(status::BadState);
        jclass h = impl_android::get_helper(e);
        if (!h) return result<void>::fail(status::NotSupported);
        jmethodID m = e->GetStaticMethodID(h, "stopDiscovery", "()V");
        if (!m) { e->DeleteLocalRef(h); impl_android::clear_exc(e);
                  return result<void>::fail(status::NotSupported); }
        e->CallStaticVoidMethod(h, m);
        e->DeleteLocalRef(h);
        impl_android::clear_exc(e);
        return result<void>::success();
    }

    static result<device_list> scan(uint32_t seconds = 8, bool = true) noexcept {
        using R = result<device_list>;
        if (seconds == 0 || seconds > 60) return R::fail(status::InvalidArgument);

        auto start = scan_start(seconds);
        if (!start) return R::fail(start.st, start.native);

        impl::sleep_ms(seconds * 1000);

        auto stop = scan_stop();
        (void)stop;

        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);
        jclass h = impl_android::get_helper(e);
        if (!h) return R::fail(status::NotSupported);
        jmethodID m = e->GetStaticMethodID(h, "getDiscoveredDevices",
                                            "()[Ljava/lang/Object;");
        if (!m) { e->DeleteLocalRef(h); impl_android::clear_exc(e);
                  return R::fail(status::NotSupported); }
        jobjectArray a = (jobjectArray)e->CallStaticObjectMethod(h, m);
        e->DeleteLocalRef(h);
        impl_android::clear_exc(e);
        if (!a) return R::fail(status::BadState);

        jsize total = e->GetArrayLength(a);
        jsize n = total / 6;
        device_list out;
        for (jsize i = 0; i < n; ++i) {
            knst_bluetooth_device d;
            impl_android::read_device(e, a, i * 6, d);
            out.push_back(std::move(d));
        }
        e->DeleteLocalRef(a);
        return R::success(std::move(out));
    }

    // ---- list ----------------------------------------------------------

    static result<device_list> list(bool = true) noexcept {
        // Android has no "all devices" cache — only paired and discovered.
        return list_paired();
    }

    static result<device_list> list_paired() noexcept {
        using R = result<device_list>;
        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);
        jclass h = impl_android::get_helper(e);
        if (!h) return R::fail(status::NotSupported);
        jmethodID m = e->GetStaticMethodID(h, "getBondedDevices",
                                            "()[Ljava/lang/Object;");
        if (!m) { e->DeleteLocalRef(h); impl_android::clear_exc(e);
                  return R::fail(status::NotSupported); }
        jobjectArray a = (jobjectArray)e->CallStaticObjectMethod(h, m);
        e->DeleteLocalRef(h);
        impl_android::clear_exc(e);
        if (!a) return R::fail(status::BadState);

        jsize total = e->GetArrayLength(a);
        jsize n = total / 6;
        device_list out;
        for (jsize i = 0; i < n; ++i) {
            knst_bluetooth_device d;
            impl_android::read_device(e, a, i * 6, d);
            out.push_back(std::move(d));
        }
        e->DeleteLocalRef(a);
        return R::success(std::move(out));
    }

    static result<device_list> list_connected() noexcept {
        auto all = list_paired();
        if (!all) return all;
        device_list out;
        for (auto& d : all.value) if (d.connected) out.push_back(std::move(d));
        return result<device_list>::success(std::move(out));
    }

    // ---- info ----------------------------------------------------------

    static result<knst_bluetooth_device> info(const knst_c16string& addr) noexcept {
        using R = result<knst_bluetooth_device>;
        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);
        jclass h = impl_android::get_helper(e);
        if (!h) return R::fail(status::NotSupported);
        jmethodID m = e->GetStaticMethodID(h, "getDeviceInfo",
            "(Ljava/lang/String;)[Ljava/lang/Object;");
        if (!m) { e->DeleteLocalRef(h); impl_android::clear_exc(e);
                  return R::fail(status::NotSupported); }
        jstring js = impl_android::new_addr_str(e, addr);
        jobjectArray a = (jobjectArray)e->CallStaticObjectMethod(h, m, js);
        e->DeleteLocalRef(js);
        e->DeleteLocalRef(h);
        impl_android::clear_exc(e);
        if (!a) return R::fail(status::NotFound);

        knst_bluetooth_device d;
        impl_android::read_device(e, a, 0, d);
        e->DeleteLocalRef(a);
        return R::success(std::move(d));
    }

    static result<device_detail> detail(const knst_c16string& addr) noexcept {
        using R = result<device_detail>;
        auto bi = info(addr);
        if (!bi) return R::fail(bi.st, bi.native);
        device_detail dd;
        dd.basic = std::move(bi.value);
        dd.device_class = dd.basic.type;

        auto bat = battery(addr);
        if (bat) dd.battery_percent = bat.value;

        dd.valid = true;
        return R::success(std::move(dd));
    }

    static result<int32_t> battery(const knst_c16string& addr) noexcept {
        using R = result<int32_t>;
        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);
        jclass h = impl_android::get_helper(e);
        if (!h) return R::fail(status::NotSupported);
        jmethodID m = e->GetStaticMethodID(h, "getBatteryLevel",
                                            "(Ljava/lang/String;)I");
        if (!m) { e->DeleteLocalRef(h); impl_android::clear_exc(e);
                  return R::fail(status::NotSupported); }
        jstring js = impl_android::new_addr_str(e, addr);
        jint bl = e->CallStaticIntMethod(h, m, js);
        e->DeleteLocalRef(js);
        e->DeleteLocalRef(h);
        impl_android::clear_exc(e);
        if (bl < 0) return R::fail(status::NotFound);
        return R::success(static_cast<int32_t>(bl));
    }

    static result<int32_t> rssi(const knst_c16string&) noexcept {
        return result<int32_t>::fail(status::NotSupported);
    }

    // ---- wait ----------------------------------------------------------

    static result<void> wait_for_device(const knst_c16string& addr,
                                         uint32_t timeout_ms = 10000) noexcept {
        return impl::wait_until([&]() -> result<void> {
            auto r = info(addr);
            if (!r) return result<void>::fail(r.st, r.native);
            return result<void>::success();
        }, timeout_ms);
    }

    static result<void> wait_for_connection(const knst_c16string& addr,
                                             bool want_connected = true,
                                             uint32_t timeout_ms = 15000) noexcept {
        return impl::wait_connection([&]() { return info(addr); }, want_connected, timeout_ms);
    }

    // ---- pair / remove -------------------------------------------------

    static result<void> pair(const knst_c16string& addr, uint32_t timeout_s = 30) noexcept {
        using R = result<void>;

        // Idempotent
        auto pre = info(addr);
        if (pre && pre.value.paired) return R::success();

        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);
        jclass h = impl_android::get_helper(e);
        if (!h) return R::fail(status::NotSupported);
        jmethodID m = e->GetStaticMethodID(h, "createBond",
                                            "(Ljava/lang/String;)I");
        if (!m) { e->DeleteLocalRef(h); impl_android::clear_exc(e);
                  return R::fail(status::NotSupported); }
        jstring js = impl_android::new_addr_str(e, addr);
        jint r = e->CallStaticIntMethod(h, m, js);
        e->DeleteLocalRef(js);
        e->DeleteLocalRef(h);
        impl_android::clear_exc(e);
        if (r == 0) return R::fail(status::IoError);

        status last = status::Timeout;
        const bool ok = impl::settle([&]() -> bool {
            auto i = info(addr);
            if (!i) { last = i.st; return false; }
            return i.value.paired;
        }, timeout_s * 1000);
        return ok ? R::success() : R::fail(last);
    }

    static result<void> pair_with_pin(const knst_c16string& addr,
                                       const knst_c16string& pin,
                                       uint32_t timeout_s = 30) noexcept {
        using R = result<void>;
        if (timeout_s < 5 || timeout_s > 120) return R::fail(status::InvalidArgument);
        if (pin.empty() || pin.length() > 16) return R::fail(status::InvalidArgument);

        // Idempotent
        auto pre = info(addr);
        if (pre && pre.value.paired) return R::success();

        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);

        jclass ba = e->FindClass("android/bluetooth/BluetoothAdapter");
        if (!ba) { impl_android::clear_exc(e); return R::fail(status::NotSupported); }
        jmethodID get = e->GetStaticMethodID(ba, "getDefaultAdapter",
                                             "()Landroid/bluetooth/BluetoothAdapter;");
        if (!get) { e->DeleteLocalRef(ba); impl_android::clear_exc(e);
                    return R::fail(status::NotSupported); }
        jobject adapter = e->CallStaticObjectMethod(ba, get);
        e->DeleteLocalRef(ba);
        impl_android::clear_exc(e);
        if (!adapter) return R::fail(status::NotFound);

        jclass acls = e->GetObjectClass(adapter);
        jmethodID getdev = e->GetMethodID(acls, "getRemoteDevice",
            "(Ljava/lang/String;)Landroid/bluetooth/BluetoothDevice;");
        if (!getdev) { e->DeleteLocalRef(acls); e->DeleteLocalRef(adapter);
                       impl_android::clear_exc(e); return R::fail(status::NotSupported); }

        jstring jaddr = impl_android::new_addr_str(e, addr);
        jobject device = e->CallObjectMethod(adapter, getdev, jaddr);
        e->DeleteLocalRef(jaddr);
        e->DeleteLocalRef(acls);
        e->DeleteLocalRef(adapter);
        impl_android::clear_exc(e);
        if (!device) return R::fail(status::NotFound);

        // BluetoothDevice.setPin(byte[]) — deprecated in API 33+ but still
        // functional for classic BT legacy PIN pairing on 24–32.
        jclass dcls = e->GetObjectClass(device);
        jmethodID setpin = e->GetMethodID(dcls, "setPin", "([B)Z");
        bool ok = false;
        if (setpin) {
            knst_byte_string pin8 = knst_dev::to_utf8(pin);
            jbyteArray arr = e->NewByteArray((jsize)pin8.length());
            if (arr) {
                e->SetByteArrayRegion(arr, 0, (jsize)pin8.length(),
                                       reinterpret_cast<const jbyte*>(pin8.data()));
                jboolean r = e->CallBooleanMethod(device, setpin, arr);
                if (e->ExceptionCheck()) { e->ExceptionClear(); r = JNI_FALSE; }
                e->DeleteLocalRef(arr);
                ok = (r == JNI_TRUE);
            }
        }
        e->DeleteLocalRef(dcls);
        e->DeleteLocalRef(device);
        impl_android::clear_exc(e);
        if (!ok) return R::fail(status::NotSupported);

        // Trigger createBond and poll for completion.
        auto bond = pair(addr, timeout_s);
        return bond;
    }

    static result<void> trust(const knst_c16string&) noexcept {
        return result<void>::success();   // Bond == trust on Android
    }
    static result<void> untrust(const knst_c16string&) noexcept {
        return result<void>::success();
    }

    static result<void> remove(const knst_c16string& addr) noexcept {
        using R = result<void>;
        JNIEnv* e = impl_android::jenv();
        if (!e) return R::fail(status::BadState);
        jclass h = impl_android::get_helper(e);
        if (!h) return R::fail(status::NotSupported);
        jmethodID m = e->GetStaticMethodID(h, "removeBond",
                                            "(Ljava/lang/String;)I");
        if (!m) { e->DeleteLocalRef(h); impl_android::clear_exc(e);
                  return R::fail(status::NotSupported); }
        jstring js = impl_android::new_addr_str(e, addr);
        jint r = e->CallStaticIntMethod(h, m, js);
        e->DeleteLocalRef(js);
        e->DeleteLocalRef(h);
        impl_android::clear_exc(e);
        return r != 0 ? R::success() : R::fail(status::IoError);
    }

    static result<void> block(const knst_c16string&, bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }

    static result<void> pair_and_connect(const knst_c16string& addr) noexcept {
        return pair(addr);
    }
    static result<void> reconnect(const knst_c16string&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> connect(const knst_c16string& addr, uint32_t timeout_s = 20) noexcept {
        using R = result<void>;
        if (timeout_s == 0 || timeout_s > 120) return R::fail(status::InvalidArgument);
        // If already connected, succeed.
        auto i = info(addr);
        if (!i) return R::fail(i.st, i.native);
        if (i.value.connected) return R::success();
        // Android 12+ blocks apps from triggering A2DP/HFP profile connects
        // without a profile proxy (needs Java-side BluetoothProfile.ServiceListener).
        // The system auto-connects profiles after bonding; for app-level
        // control, call the platform APIs directly. We poll briefly in case
        // the connection is in-flight from pair().
        status last = status::Timeout;
        bool reached = impl::settle([&]() -> bool {
            auto r = info(addr);
            if (!r) { last = r.st; return false; }
            return r.value.connected;
        }, (timeout_s > 5 ? 5 : timeout_s) * 1000u);
        if (reached) return R::success();
        return R::fail(status::NotSupported);
    }
    static result<void> disconnect(const knst_c16string& addr) noexcept {
        using R = result<void>;
        auto i = info(addr);
        if (!i) return R::fail(i.st, i.native);
        if (!i.value.connected) return R::success();
        // Same limitation as connect — profile-based disconnect requires
        // Java-side profile proxies, which the library does not own.
        return R::fail(status::NotSupported);
    }
};


// ============================================================
// Other platforms: everything reports NotSupported
// ============================================================

#else



class bluetooth {
public:
    using device_list = knst_vector<knst_bluetooth_device>;

    static bool is_supported() noexcept { return false; }
    static bool has_adapter()  noexcept { return false; }

    static result<knst_vector<adapter_info>> adapters() noexcept {
        return result<knst_vector<adapter_info>>::success(knst_vector<adapter_info>());
    }
    static result<adapter_info> adapter() noexcept {
        return result<adapter_info>::fail(status::NotSupported);
    }
    static result<adapter_info> adapter(const knst_c16string&) noexcept {
        return result<adapter_info>::fail(status::NotSupported);
    }

    static result<void> set_power(bool) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> set_discoverable(bool) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> set_pairable(bool) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> set_adapter_name(const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> rfkill_block(bool) noexcept { return result<void>::fail(status::NotSupported); }

    static result<void> scan_start(uint32_t = 1) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> scan_stop() noexcept { return result<void>::fail(status::NotSupported); }
    static result<device_list> scan(uint32_t = 8, bool = true) noexcept { return result<device_list>::fail(status::NotSupported); }
    static result<device_list> list(bool = true) noexcept { return result<device_list>::fail(status::NotSupported); }
    static result<device_list> list_paired() noexcept { return result<device_list>::fail(status::NotSupported); }
    static result<device_list> list_connected() noexcept { return result<device_list>::fail(status::NotSupported); }

    static result<knst_bluetooth_device> info(const knst_c16string&) noexcept { return result<knst_bluetooth_device>::fail(status::NotSupported); }
    static result<device_detail> detail(const knst_c16string&) noexcept { return result<device_detail>::fail(status::NotSupported); }
    static result<int32_t> battery(const knst_c16string&) noexcept { return result<int32_t>::fail(status::NotSupported); }
    static result<int32_t> rssi(const knst_c16string&) noexcept { return result<int32_t>::fail(status::NotSupported); }

    static result<void> wait_for_device(const knst_c16string&, uint32_t = 10000) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> wait_for_connection(const knst_c16string&, bool = true, uint32_t = 15000) noexcept { return result<void>::fail(status::NotSupported); }

    static result<void> pair(const knst_c16string&, uint32_t = 30) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> pair_with_pin(const knst_c16string&, const knst_c16string&, uint32_t = 30) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> trust(const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> untrust(const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> connect(const knst_c16string&, uint32_t = 20) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> disconnect(const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> remove(const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> block(const knst_c16string&, bool) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> pair_and_connect(const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
    static result<void> reconnect(const knst_c16string&) noexcept { return result<void>::fail(status::NotSupported); }
};

#endif

} // namespace knst_bt