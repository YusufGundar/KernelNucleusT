// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_usb.hpp
----------------------------
    Comprehensive USB controller

    PORTABILITY NOTES
    =================
    Tested on kernel 6.x and 7.x. Should work on 5.x+.
    Handles these sysfs layout variations:
      - "1-2", "1-2.3"          (regular USB device paths)
      - "usbN" (N=1..99)        (root hub names)
      - Interface names "1-2:1.0"
      - Port names "usbN-portM", "1-2-portM"
      - Container environments without /sys/bus/usb/devices → empty results

*/

#pragma once

#include <thread>
#include <functional>
#include <mutex>
#include <atomic>

#if KNST_USING_PLATFORM_LINUX
    #include <dirent.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <linux/usbdevice_fs.h>
#endif

namespace knst_usbc {

using knst_dev::status;
using knst_dev::result;


// Types

enum class usb_speed : uint8_t {
    Unknown = 0, Low = 1, Full = 2, High = 3, Super = 4, SuperPlus = 5
};

enum class usb_state : uint8_t {
    Unknown = 0, NotAttached, Attached, Powered, Reconnecting,
    Unauthorized, Default, Addressed, Configured, Suspended
};

struct usb_endpoint_info {
    uint8_t address = 0;
    uint8_t number = 0;
    bool is_in = false;
    uint8_t transfer_type = 0;
    uint16_t max_packet_size = 0;
    uint8_t interval = 0;
    uint8_t attributes = 0;
};

struct usb_interface_info {
    uint32_t number = 0;
    uint32_t class_code = 0;
    uint32_t subclass = 0;
    uint32_t protocol = 0;
    uint32_t alt_setting = 0;
    knst_c16string driver;
    knst_vector<usb_endpoint_info> endpoints;   // empty on sysfs-only read
    bool valid = false;
};

struct usb_config_info {
    uint8_t number = 0;
    uint8_t attributes = 0;
    uint16_t max_power_ma = 0;
    bool is_active = false;
    knst_vector<usb_interface_info> interfaces;
    bool valid = false;
};

struct usb_device_descriptor {
    uint16_t bcd_usb = 0;
    uint8_t  device_class = 0;
    uint8_t  device_subclass = 0;
    uint8_t  device_protocol = 0;
    uint8_t  max_packet_size0 = 0;
    uint16_t id_vendor = 0;
    uint16_t id_product = 0;
    uint16_t bcd_device = 0;
    uint8_t  manufacturer_idx = 0;
    uint8_t  product_idx = 0;
    uint8_t  serial_idx = 0;
    uint8_t  num_configurations = 0;


    bool valid = false;
};

struct usb_strings {
    knst_c16string manufacturer;
    knst_c16string product;
    knst_c16string serial;
    knst_c16string configuration;
    bool valid = false;
};

struct usb_port_info {

    uint32_t number   = 0;

    bool is_connectable  = false;
    bool is_enabled = false;
    bool is_suspended = false;
    bool is_high_speed = false;
    bool is_super_speed  = false;



    uint32_t port_speed_mbps = 0;
    knst_c16string link_state;
    knst_c16string connect_type;


    knst_c16string port_name;         // "usb3-port1", "1-2-port2"
    bool has_device = false;
    knst_c16string device_name;       // "3-1", "1-2.3"
    knst_c16string device_product;    // "USB Receiver"

    bool valid = false;
};

struct usb_error_stats {
    uint64_t port_resets     = 0;
    uint64_t disconnects     = 0;
    uint64_t connect_errors  = 0;
    uint64_t overcurrents    = 0;
    uint64_t babble_errors   = 0;
    uint64_t timeout_errors  = 0;
    uint64_t crc_errors      = 0;
    bool valid = false;
};

struct usb_health_snapshot {
    bool healthy = false;
    double score = 100.0;
    bool has_overcurrent = false;
    bool has_errors = false;
    bool is_unauthorized = false;
    knst_vector<knst_c16string> warnings;
};

struct usb_power_info {
    uint32_t max_power_ma          = 0;

    bool is_self_powered = false;
    bool has_remote_wakeup = false;
    bool is_suspended = false;
    knst_c16string control_mode;
    uint32_t autosuspend_delay_ms = 0;
    bool valid = false;
};


inline knst_usb_info enrich(knst_usb_info u) noexcept {
    if (u.is_root_hub && u.usb_class == knst_usb_class::Unknown) {

        u.usb_class = knst_usb_class::Hub;
        u.is_hub  = true;

    }
    return u;
}

// ============================================================
// Enumeration — free functions
// ============================================================

inline knst_vector<knst_usb_info> find(uint16_t vid, uint16_t pid = 0) noexcept {
    knst_vector<knst_usb_info> out;
    auto all = knst_devices::usb();
    for (auto& u : all) {
        if (u.vid == vid && (pid == 0 || u.pid == pid))
            out.push_back(enrich(u));
    }
    return out;
}

// Alias — clearer name for callers
inline knst_vector<knst_usb_info> find_by_vid_pid(uint16_t vid,uint16_t pid = 0) noexcept {
    return find(vid, pid);
}

#if KNST_USING_PLATFORM_LINUX && !defined(KNST_USING_PLATFORM_ANDROID)

namespace detail_usb {

// Returns "1-2", "usb1", etc. for a device. Empty on failure.
inline knst_c16string sysfs_name(const knst_usb_info& u) noexcept {
    DIR* d = ::opendir("/sys/bus/usb/devices");
    if (!d) return knst_c16string();
    knst_c16string found;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.' || ::strchr(e->d_name, ':')) continue;
        char p[300]; uint64_t b = 0, n = 0;
        ::snprintf(p, sizeof(p), "/sys/bus/usb/devices/%s/busnum", e->d_name);
        if (!knst_devices::detail::read_file_u64(p, b)) continue;
        ::snprintf(p, sizeof(p), "/sys/bus/usb/devices/%s/devnum", e->d_name);
        if (!knst_devices::detail::read_file_u64(p, n)) continue;
        if (b == u.bus && n == u.device_num) { found = knst_c16string(e->d_name); break; }
    }
    ::closedir(d);
    return found;
}

// Accepts: "1-2", "1-2.3", "usb1".."usb99" (root hubs). Rejects '/', "..".
inline bool ok_sysname(const knst_c16string& s) noexcept {
    if (s.empty() || s.length() > 32) return false;

    uint32_t start = 0;
    if (s.length() >= 4 &&
        s[0] == u'u' && s[1] == u's' && s[2] == u'b' &&
        s[3] >= u'0' && s[3] <= u'9') {
        start = 3;
    }

    for (uint32_t i = start; i < s.length(); ++i) {
        char16_t c = s[i];
        if (!((c >= u'0' && c <= u'9') || c == u'-' || c == u'.'))
            return false;
    }
    return true;
}

inline bool read_attr_str(const knst_c16string& devname,
                          const char* attr,
                          char* buf, size_t buf_sz) noexcept {
    knst_byte_string nb = knst_dev::to_utf8(devname);
    char path[300];
    ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/%s",
               reinterpret_cast<const char*>(nb.data()), attr);
    return knst_devices::detail::read_file(path, buf, buf_sz);
}

inline bool read_attr_u64(const knst_c16string& devname,
                          const char* attr,
                          uint64_t& out) noexcept {
    knst_byte_string nb = knst_dev::to_utf8(devname);
    char path[300];
    ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/%s",
               reinterpret_cast<const char*>(nb.data()), attr);
    return knst_devices::detail::read_file_u64(path, out);
}

// USB class/subclass/protocol fields in sysfs are HEX strings
inline bool read_attr_hex(const knst_c16string& devname,
                          const char* attr,
                          uint64_t& out) noexcept {
    knst_byte_string nb = knst_dev::to_utf8(devname);
    char path[300], buf[64];
    ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/%s",
               reinterpret_cast<const char*>(nb.data()), attr);
    if (!knst_devices::detail::read_file(path, buf, sizeof(buf)))
        return false;
    out = ::strtoull(buf, nullptr, 16);
    return true;
}

// Scan interfaces of a device. Returns true if any interface matches class.
inline bool has_interface_class(const knst_c16string& devname,uint8_t class_code) noexcept {
    knst_byte_string nb = knst_dev::to_utf8(devname);
    const char* base = reinterpret_cast<const char*>(nb.data());
    size_t pl = ::strlen(base);

    DIR* d = ::opendir("/sys/bus/usb/devices");
    if (!d) return false;

    bool found = false;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (::strncmp(e->d_name, base, pl) != 0 || e->d_name[pl] != ':')
            continue;
        char p[320], buf[16];
        ::snprintf(p, sizeof(p),"/sys/bus/usb/devices/%s/bInterfaceClass", e->d_name);
                   
        if (knst_devices::detail::read_file(p, buf, sizeof(buf))) {
            if (::strtoul(buf, nullptr, 16) == class_code) {
                found = true;
                break;
            }
        }
    }
    ::closedir(d);
    return found;
}

inline knst_c16string driver_of_interface(const char* iface_name) noexcept {
    char p[320], link[128];
    ::snprintf(p, sizeof(p), "/sys/bus/usb/devices/%s/driver", iface_name);
    if (knst_devices::detail::read_symlink(p, link, sizeof(link))) {
        char* base = ::strrchr(link, '/');
        return knst_c16string(base ? base + 1 : link);
    }
    return knst_c16string();
}

// Checks every interface of a composite device for a given driver.
inline bool has_driver_on_any_interface(const knst_c16string& devname,const knst_c16string& driver_name) noexcept {
    knst_byte_string nb = knst_dev::to_utf8(devname);
    const char* base = reinterpret_cast<const char*>(nb.data());
    size_t pl = ::strlen(base);

    // 1) Scan interfaces
    {
        DIR* d = ::opendir("/sys/bus/usb/devices");
        if (d) {
            struct dirent* e;
            while ((e = ::readdir(d)) != nullptr) {
                if (::strncmp(e->d_name, base, pl) != 0 || e->d_name[pl] != ':')
                    continue;
                knst_c16string drv = driver_of_interface(e->d_name);
                if (drv == driver_name) {
                    ::closedir(d);
                    return true;
                }
            }
            ::closedir(d);
        }
    }

    // 2) Device-level driver symlink fallback

    char p[320], link[128];


    ::snprintf(p, sizeof(p), "/sys/bus/usb/devices/%s/driver", base);
    if (knst_devices::detail::read_symlink(p, link, sizeof(link))) {
        char* s = ::strrchr(link, '/');
        knst_c16string drv(s ? s + 1 : link);
        if (drv == driver_name) return true;
    }
    return false;
}

} // namespace detail_usb

// ============================================================
// Enumeration — class/driver aware
// ============================================================

inline knst_vector<knst_usb_info> find_by_class(uint8_t class_code) noexcept {
    knst_vector<knst_usb_info> out;
    auto all = knst_devices::usb();
    for (auto& u : all) {
        knst_usb_info e = enrich(u);

        if (class_code == 0x09) {
            if (e.is_hub || e.is_root_hub) out.push_back(e);
            continue;
        }

        // Map USB class code → enum value
        knst_usb_class want = knst_usb_class::Unknown;
        switch (class_code) {


            case 0x01: want = knst_usb_class::Audio;       break;
            case 0x02: want = knst_usb_class::CDC;         break;
            case 0x03: want = knst_usb_class::HID;         break;
            case 0x07: want = knst_usb_class::Printer;     break;
            case 0x08: want = knst_usb_class::MassStorage; break;
            case 0x0E: want = knst_usb_class::Video;       break;
            case 0xE0: want = knst_usb_class::Wireless;    break;
            case 0xFF: want = knst_usb_class::Vendor;      break;

            default: break;

        }
        if (want != knst_usb_class::Unknown && e.usb_class == want) {
            out.push_back(e);
            continue;
        }

        // Interface class fallback (composite devices)
        auto name = detail_usb::sysfs_name(e);
        if (!detail_usb::ok_sysname(name)) continue;
        if (detail_usb::has_interface_class(name, class_code))
            out.push_back(e);
    }
    return out;
}

// Finds every device with `driver_name` on AT LEAST ONE interface.
inline knst_vector<knst_usb_info> find_by_driver(const knst_c16string& driver_name) noexcept {
    knst_vector<knst_usb_info> out;
    if (!knst_dev::is_safe_token(driver_name, 32)) return out;

    auto all = knst_devices::usb();

    // Special case: root hubs are driven by "hub" but their sysfs name
    // is "usbN" and the driver symlink may vary across kernels.
    if (driver_name == u"hub") {
        for (auto& u : all) {
            knst_usb_info e = enrich(u);
            if (e.is_hub || e.is_root_hub) out.push_back(e);
        }
        return out;
    }

    for (auto& u : all) {
        knst_usb_info e = enrich(u);
        auto name = detail_usb::sysfs_name(e);
        if (!detail_usb::ok_sysname(name)) continue;
        if (detail_usb::has_driver_on_any_interface(name, driver_name))
            out.push_back(e);
    }
    return out;
}

inline knst_vector<knst_usb_info> find_hubs() noexcept {
    return find_by_class(0x09);
}

template<typename Predicate>
inline knst_vector<knst_usb_info> find_all(Predicate&& pred) noexcept {
    knst_vector<knst_usb_info> out;
    auto all = knst_devices::usb();
    for (auto& u : all) {
        knst_usb_info e = enrich(u);
        if (pred(e)) out.push_back(e);
    }
    return out;
}

// ============================================================
// Linux USB class
// ============================================================

class usb {
public:
    // ---------- Authorization ----------
    static result<void> set_authorized(const knst_usb_info& u, bool allow) noexcept {
        return write_attr(u, "authorized", allow ? "1" : "0");
    }

    // ---------- Runtime PM ----------
    static result<void> set_power_control(const knst_usb_info& u, bool auto_suspend) noexcept {
        return write_attr(u, "power/control", auto_suspend ? "auto" : "on");
    }

    static result<void> set_autosuspend_delay_ms(const knst_usb_info& u, uint32_t ms) noexcept {
        if (ms > 600000) return result<void>::fail(status::InvalidArgument);
        char v[16]; ::snprintf(v, sizeof(v), "%u", ms);
        return write_attr(u, "power/autosuspend_delay_ms", v);
    }

    static result<void> set_remote_wakeup(const knst_usb_info& u, bool enable) noexcept {
        return write_attr(u, "power/wakeup", enable ? "enabled" : "disabled");
    }

    // ---------- Reset / Recovery ----------
    static result<void> reset(const knst_usb_info& u) noexcept {
        if (u.bus == 0 || u.device_num == 0 ||
            u.bus > 999 || u.device_num > 999)
            return result<void>::fail(status::InvalidArgument);
        char p[64];
        ::snprintf(p, sizeof(p), "/dev/bus/usb/%03u/%03u", u.bus, u.device_num);
        knst_dev::unique_fd fd(::open(p, O_WRONLY | O_CLOEXEC));
        if (!fd.valid()) return result<void>::fail(knst_dev::map_errno(errno), errno);
        if (::ioctl(fd.get(), USBDEVFS_RESET, 0) < 0)
            return result<void>::fail(knst_dev::map_errno(errno), errno);
        return result<void>::success();
    }

    static result<void> power_cycle(const knst_usb_info& u,
                                     uint32_t delay_ms = 500) noexcept {
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return result<void>::fail(status::NotFound);
        knst_byte_string b = knst_dev::to_utf8(n);
        const char* name = reinterpret_cast<const char*>(b.data());
        auto r = knst_dev::sysfs_write("/sys/bus/usb/drivers/usb/unbind", name);
        if (!r) return r;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(delay_ms > 5000 ? 5000 : delay_ms));
        return knst_dev::sysfs_write("/sys/bus/usb/drivers/usb/bind", name);
    }

    // ---------- Driver binding ----------
    static result<void> unbind_driver(const knst_usb_info& u) noexcept {
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return result<void>::fail(status::NotFound);
        knst_byte_string b = knst_dev::to_utf8(n);
        return knst_dev::sysfs_write("/sys/bus/usb/drivers/usb/unbind",reinterpret_cast<const char*>(b.data()));
    }

    static result<void> bind_driver(const knst_usb_info& u,const knst_c16string& driver_name) noexcept {
        if (!knst_dev::is_safe_token(driver_name, 32))
            return result<void>::fail(status::InvalidArgument);
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return result<void>::fail(status::NotFound);
        knst_byte_string nb = knst_dev::to_utf8(n);
        knst_byte_string db = knst_dev::to_utf8(driver_name);
        char path[300];
        ::snprintf(path, sizeof(path),
                   "/sys/bus/usb/drivers/%s/bind",
                   reinterpret_cast<const char*>(db.data()));
        return knst_dev::sysfs_write(path, reinterpret_cast<const char*>(nb.data()));
    }

    // ---------- Descriptors ----------
    static result<usb_device_descriptor> descriptors(const knst_usb_info& u) noexcept {
        using R = result<usb_device_descriptor>;
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return R::fail(status::NotFound);

        usb_device_descriptor d;
        d.id_vendor  = u.vid;
        d.id_product = u.pid;

        uint64_t v = 0;

        // bcdUSB: sysfs has "version" not "bcdUSB". Format: " 2.00" or " 3.10"
        char ver_buf[32] = {0};
        if (detail_usb::read_attr_str(n, "version", ver_buf, sizeof(ver_buf))) {
            int major = 0, minor = 0;
            if (::sscanf(ver_buf, "%d.%d", &major, &minor) == 2) {
                // "3.10" → 0x0310 (BCD: 3, 1, 0)
                int minor_bcd = ((minor / 10) << 4) | (minor % 10);
                d.bcd_usb = static_cast<uint16_t>((major << 8) | minor_bcd);
            }
        }

        // USB class fields are hex strings in sysfs
        if (detail_usb::read_attr_hex(n, "bDeviceClass", v))
            d.device_class = static_cast<uint8_t>(v);
        if (detail_usb::read_attr_hex(n, "bDeviceSubClass", v))
            d.device_subclass = static_cast<uint8_t>(v);
        if (detail_usb::read_attr_hex(n, "bDeviceProtocol", v))
            d.device_protocol = static_cast<uint8_t>(v);
        if (detail_usb::read_attr_u64(n, "bMaxPacketSize0", v))
            d.max_packet_size0 = static_cast<uint8_t>(v);
        if (detail_usb::read_attr_hex(n, "bcdDevice", v))
            d.bcd_device = static_cast<uint16_t>(v);
        if (detail_usb::read_attr_u64(n, "bNumConfigurations", v))
            d.num_configurations = static_cast<uint8_t>(v);

        d.valid = true;
        return R::success(std::move(d));
    }

    static result<usb_strings> strings(const knst_usb_info& u) noexcept {
        using R = result<usb_strings>;
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return R::fail(status::NotFound);

        usb_strings s;
        char buf[256];
        if (detail_usb::read_attr_str(n, "manufacturer", buf, sizeof(buf)))
            s.manufacturer = knst_c16string(buf);
        if (detail_usb::read_attr_str(n, "product", buf, sizeof(buf)))
            s.product = knst_c16string(buf);
        if (detail_usb::read_attr_str(n, "serial", buf, sizeof(buf)))
            s.serial = knst_c16string(buf);
        s.valid = true;
        return R::success(std::move(s));
    }

    static result<knst_vector<usb_config_info>> configurations(
        const knst_usb_info& u) noexcept {
        using R = result<knst_vector<usb_config_info>>;
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return R::fail(status::NotFound);

        R r;
        uint64_t active_cfg = 0;
        detail_usb::read_attr_u64(n, "bConfigurationValue", active_cfg);

        uint64_t max_power = 0;
        detail_usb::read_attr_u64(n, "bMaxPower", max_power);

        knst_byte_string nb = knst_dev::to_utf8(n);
        const char* base = reinterpret_cast<const char*>(nb.data());
        size_t pl = ::strlen(base);

        DIR* d = ::opendir("/sys/bus/usb/devices");
        if (!d) return R::fail(knst_dev::map_errno(errno), errno);

        usb_config_info cfg;
        cfg.number = static_cast<uint8_t>(active_cfg);
        // sysfs bMaxPower already reports mA (kernel multiplies the 2mA
        // descriptor units); do NOT multiply again.
        cfg.max_power_ma = static_cast<uint16_t>(max_power);
        cfg.is_active = true;

        struct dirent* e;
        while ((e = ::readdir(d)) != nullptr) {
            if (::strncmp(e->d_name, base, pl) != 0 || e->d_name[pl] != ':')
                continue;

            usb_interface_info iface;
            knst_c16string iface_name(e->d_name);
            uint64_t v = 0;

            if (detail_usb::read_attr_hex(iface_name, "bInterfaceNumber", v))
                iface.number = static_cast<uint32_t>(v);

            if (detail_usb::read_attr_hex(iface_name, "bInterfaceClass", v))
                iface.class_code = static_cast<uint32_t>(v);

            if (detail_usb::read_attr_hex(iface_name, "bInterfaceSubClass", v))
                iface.subclass = static_cast<uint32_t>(v);


            if (detail_usb::read_attr_hex(iface_name, "bInterfaceProtocol", v))
                iface.protocol = static_cast<uint32_t>(v);


            iface.driver = detail_usb::driver_of_interface(e->d_name);
            iface.valid  = true;
            cfg.interfaces.push_back(std::move(iface));
        }
        ::closedir(d);

        cfg.valid = true;
        r.value.push_back(std::move(cfg));
        return r;
    }

    // ---------- Speed & state ----------
    static result<usb_speed> speed(const knst_usb_info& u) noexcept {
        using R = result<usb_speed>;
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return R::fail(status::NotFound);

        char buf[64] = {0};
        if (!detail_usb::read_attr_str(n, "speed", buf, sizeof(buf)))
            return R::fail(status::NotFound);

        // sysfs may contain "unknown" for some hubs
        if (::strstr(buf, "unknown")) return R::success(usb_speed::Unknown);

        double mbps = ::atof(buf);
        if (mbps >= 10000.0) return R::success(usb_speed::SuperPlus);
        if (mbps >= 5000.0) return R::success(usb_speed::Super);
        if (mbps >= 480.0) return R::success(usb_speed::High);
        if (mbps >= 12.0) return R::success(usb_speed::Full);
        if (mbps >= 1.5) return R::success(usb_speed::Low);
        return R::success(usb_speed::Unknown);
    }

    static result<usb_state> state(const knst_usb_info& u) noexcept {
        using R = result<usb_state>;
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return R::fail(status::NotFound);

        uint64_t auth = 1;
        if (detail_usb::read_attr_u64(n, "authorized", auth) && auth == 0)
            return R::success(usb_state::Unauthorized);

        uint64_t cfg = 0;
        if (detail_usb::read_attr_u64(n, "bConfigurationValue", cfg) && cfg > 0)
            return R::success(usb_state::Configured);

        return R::success(usb_state::Attached);
    }

    // ---------- Power ----------
    static result<usb_power_info> power_info(const knst_usb_info& u) noexcept {
        using R = result<usb_power_info>;
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return R::fail(status::NotFound);

        usb_power_info p;
        uint64_t v = 0;
        if (detail_usb::read_attr_u64(n, "bMaxPower", v))
            p.max_power_ma = static_cast<uint32_t>(v);   // sysfs = mA, no *2

        char buf[64];
        if (detail_usb::read_attr_str(n, "power/control", buf, sizeof(buf)))
            p.control_mode = knst_c16string(buf);
        if (detail_usb::read_attr_str(n, "power/wakeup", buf, sizeof(buf)))
            p.has_remote_wakeup = (::strcmp(buf, "enabled") == 0);
        if (detail_usb::read_attr_u64(n, "power/autosuspend_delay_ms", v))
            p.autosuspend_delay_ms = static_cast<uint32_t>(v);

char runtime_buf[64] = {0};
if (detail_usb::read_attr_str(n, "power/runtime_status", runtime_buf, sizeof(runtime_buf)))
    p.is_suspended = (::strcmp(runtime_buf, "suspended") == 0);

        p.valid = true;
        return R::success(std::move(p));
    }

    // ---------- Port (per-device) ----------
    // NOTE: port/* attributes live on PORT devices (e.g. "usb3-port1"),
    // not on the hub/device itself. Use hub_ports() for hub enumeration.
    // This per-device helper reads whatever port/* attrs exist (usually none).
    static result<usb_port_info> port_info(const knst_usb_info& u) noexcept {
        using R = result<usb_port_info>;
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return R::fail(status::NotFound);

        usb_port_info p;
        read_port_attrs(n, p);
        p.valid = true;
        return R::success(std::move(p));
    }

    // ---------- Hub ports ----------
    // Enumerates all "<hub>-portN" port devices for a hub and returns
    // their real attributes plus attached-device info.
    // Enumerates hub ports. Works on ALL kernel layouts:
    //   - Old kernels with real <hub>-portN devices (port/* attrs read)
    //   - New kernels without them (derived from connected device paths)
    //   - Containers/restricted env (returns what it can)
    static result<knst_vector<usb_port_info>> hub_ports(const knst_usb_info& hub) noexcept {
        using R = result<knst_vector<usb_port_info>>;
        knst_c16string n = detail_usb::sysfs_name(hub);
        if (!detail_usb::ok_sysname(n))
            return R::fail(status::NotFound);

        knst_byte_string nb = knst_dev::to_utf8(n);
        const char* base = reinterpret_cast<const char*>(nb.data());
        bool is_root_hub = (::strncmp(base, "usb", 3) == 0);

        R r;

        // ---- 1) Walk devices, find direct children of this hub ----
        auto all_devices = knst_devices::usb();
        for (auto& dev : all_devices) {
            auto devname = detail_usb::sysfs_name(dev);
            if (devname.empty()) continue;
            knst_byte_string devb = knst_dev::to_utf8(devname);
            const char* dname = reinterpret_cast<const char*>(devb.data());

            uint32_t port_num = 0;
            bool is_child = false;

                if (is_root_hub) {
                    const char* bus_str = base + 3;
                    char prefix[32];
                    ::snprintf(prefix, sizeof(prefix), "%s-", bus_str);
                    size_t plen = ::strlen(prefix);
                    if (::strncmp(dname, prefix, plen) != 0) continue;
                    const char* rest = dname + plen;
                    char num[16] = {0};
                    size_t i = 0;
                    while (rest[i] && rest[i] != '.' && i < sizeof(num) - 1) {
                        if (rest[i] < '0' || rest[i] > '9') break;
                        num[i] = rest[i];
                        ++i;
                    }
                    if (i == 0) continue;
                    // Reject grandchildren: "3-1.2" is NOT a direct child of usb3.
                    // A direct child's path is exactly "3-N" (no further ".<sub>").
                    if (rest[i] != '\0') continue;
                    port_num = static_cast<uint32_t>(::strtoul(num, nullptr, 10));
                    is_child = true;
                } else {
                // Hub = "N-M", child = "N-M.P"
                char prefix[96];
                ::snprintf(prefix, sizeof(prefix), "%s.", base);
                size_t plen = ::strlen(prefix);
                if (::strncmp(dname, prefix, plen) != 0) continue;
                const char* rest = dname + plen;
                char num[16] = {0};
                size_t i = 0;
                while (rest[i] && rest[i] != '.' && i < sizeof(num) - 1) {
                    if (rest[i] < '0' || rest[i] > '9') break;
                    num[i] = rest[i];
                    ++i;
                }
                if (i == 0) continue;
                // Reject grandchildren: "1-2.1.3" is NOT a direct child of "1-2".
                if (rest[i] != '\0') continue;
                port_num = static_cast<uint32_t>(::strtoul(num, nullptr, 10));
                    is_child = true;
            }

            if (!is_child) continue;

            // Skip duplicates (multi-interface device → one port entry)
            bool dup = false;
            for (auto& e : r.value)
                if (e.number == port_num) { dup = true; break; }
            if (dup) continue;

                        usb_port_info pi;
            pi.number         = port_num;
            pi.has_device     = true;
            pi.device_name    = devname;
            pi.device_product = dev.product_name;
            pi.is_enabled     = true;
            pi.valid          = true;

            // Synthetic port name: "usb3-port4", "1-4-port2", ...
            char port_name_buf[128];
            ::snprintf(port_name_buf, sizeof(port_name_buf),
                       "%s-port%u", base, port_num);
            pi.port_name = knst_c16string(port_name_buf);

            // Try real port/* attrs (present on older kernels)
            char port_sysname[128];
            ::snprintf(port_sysname, sizeof(port_sysname), "%s-port%u", base, port_num);
            read_port_attrs(knst_c16string(port_sysname), pi);

            r.value.push_back(std::move(pi));
        }

        // ---- 2) Add empty ports based on maxchild ----
        uint64_t maxchild = 0;
        detail_usb::read_attr_u64(n, "maxchild", maxchild);
        for (uint32_t p = 1; p <= static_cast<uint32_t>(maxchild) && p <= 64; ++p) {
            bool exists = false;
            for (auto& e : r.value) if (e.number == p) { exists = true; break; }
            if (exists) continue;

            usb_port_info pi;
            pi.number     = p;
            pi.has_device = false;
            pi.is_enabled = true;
            pi.valid      = true;

            // Synthetic port name for empty ports too
            char port_sysname[128];
            ::snprintf(port_sysname, sizeof(port_sysname), "%s-port%u", base, p);
            pi.port_name = knst_c16string(port_sysname);

            read_port_attrs(knst_c16string(port_sysname), pi);

            r.value.push_back(std::move(pi));
        }

        // ---- 3) Sort by port number ----
        for (uint32_t i = 1; i < r.value.size(); ++i) {
            usb_port_info key = r.value[i];
            int j = static_cast<int>(i) - 1;
            while (j >= 0 && r.value[j].number > key.number) {
                r.value[j + 1] = r.value[j];
                --j;
            }
            r.value[j + 1] = key;
        }

        return r;
    }

    // ---------- Error stats ----------
    static result<usb_error_stats> error_stats(const knst_usb_info& u) noexcept {
        using R = result<usb_error_stats>;
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return R::fail(status::NotFound);

        usb_error_stats e;
        uint64_t v = 0;
        if (detail_usb::read_attr_u64(n, "port/reset_count", v))
            e.port_resets = v;
        if (detail_usb::read_attr_u64(n, "port/connect_errors", v))
            e.connect_errors = v;

        e.valid = true;
        return R::success(std::move(e));
    }

    // ---------- Health ----------
    static result<usb_health_snapshot> health_check(const knst_usb_info& u) noexcept {
        using R = result<usb_health_snapshot>;
        usb_health_snapshot h;
        h.score = 100.0;

        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n)) {
            h.warnings.push_back(u"Device not found in sysfs");
            return R::success(std::move(h));
        }

        uint64_t auth = 1;
        if (detail_usb::read_attr_u64(n, "authorized", auth) && auth == 0) {
            h.is_unauthorized = true;
            h.score -= 20.0;
            h.warnings.push_back(u"Device is unauthorized");
        }

        uint64_t errors = 0;
        if (detail_usb::read_attr_u64(n, "port/connect_errors", errors) && errors > 0) {
            h.has_errors = true;
            h.score -= 15.0;
            h.warnings.push_back(u"Port connect errors detected");
        }

        uint64_t resets = 0;
        if (detail_usb::read_attr_u64(n, "port/reset_count", resets) && resets > 5) {
            h.score -= 10.0;
            h.warnings.push_back(u"Frequent port resets");
        }

        if (h.score < 0.0) h.score = 0.0;
        h.healthy = (h.score >= 70.0);
        return R::success(std::move(h));
    }

    // ---------- Hot-plug ----------
    static result<knst_dev::uevent> wait_for_event(uint32_t timeout_ms) noexcept {
        auto m = knst_dev::uevent_monitor::open();
        if (!m) return result<knst_dev::uevent>::fail(m.st, m.native);
        const auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            auto el = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count());
            if (el >= timeout_ms)
                return result<knst_dev::uevent>::fail(status::Timeout);
            auto ev = m.value.wait(timeout_ms - el, "usb");
            if (!ev) return ev;
            if (ev.value.devtype == knst_c16string("usb_device"))
                return ev;
        }
    }

    static result<knst_dev::uevent> wait_for_action(uint32_t timeout_ms,
                                                      const char16_t* action) noexcept {
        auto m = knst_dev::uevent_monitor::open();
        if (!m) return result<knst_dev::uevent>::fail(m.st, m.native);
        const auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            auto el = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count());
            if (el >= timeout_ms)
                return result<knst_dev::uevent>::fail(status::Timeout);
            auto ev = m.value.wait(timeout_ms - el, "usb");
            if (!ev) return ev;
            if (ev.value.devtype != knst_c16string("usb_device"))
                continue;
            if (ev.value.action == knst_c16string(action))
                return ev;
        }
    }

private:
    static result<void> write_attr(const knst_usb_info& u, const char* attr,
                                    const char* value) noexcept {
        knst_c16string n = detail_usb::sysfs_name(u);
        if (!detail_usb::ok_sysname(n))
            return result<void>::fail(status::NotFound);
        knst_byte_string b = knst_dev::to_utf8(n);
        char path[300];
        ::snprintf(path, sizeof(path),
                   "/sys/bus/usb/devices/%s/%s",
                   reinterpret_cast<const char*>(b.data()), attr);
        return knst_dev::sysfs_write(path, value);
    }

    // Shared helper for port_info and hub_ports
    static void read_port_attrs(const knst_c16string& sysname,
                                usb_port_info& pi) noexcept {
        char buf[64] = {0};

        if (detail_usb::read_attr_str(sysname, "port/connect_type",
                                       buf, sizeof(buf))) {
            pi.connect_type = knst_c16string(buf);
            pi.is_connectable = (::strstr(buf, "hotplug") != nullptr ||
                                 ::strstr(buf, "hardwired") != nullptr);
        }

        uint64_t v = 0;
        if (detail_usb::read_attr_u64(sysname, "port/disable", v))
            pi.is_enabled = (v == 0);

        if (detail_usb::read_attr_str(sysname, "port/link_state",
                                       buf, sizeof(buf)))
            pi.link_state = knst_c16string(buf);

        char speedbuf[32] = {0};
        if (detail_usb::read_attr_str(sysname, "port/speed",
                                       speedbuf, sizeof(speedbuf))) {
            if (!::strstr(speedbuf, "unknown"))
                pi.port_speed_mbps = static_cast<uint32_t>(::atoi(speedbuf));
        }
    }
};

// ============================================================
// Event subscription (UsbGuard-style daemon support)
// ============================================================

class usb_event_bus {
public:
    using handler = knst_function_shared<const knst_dev::uevent&>;

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

    result<void> run(uint32_t timeout_ms = 0) noexcept {
        auto m = knst_dev::uevent_monitor::open();
        if (!m) return result<void>::fail(m.st, m.native);
        running_.store(true);
        while (running_.load()) {
            auto ev = m.value.wait(timeout_ms == 0 ? 60000 : timeout_ms, "usb");
            if (!ev) continue;
            if (ev.value.devtype != knst_c16string("usb_device"))
                continue;
            knst_vector<handler> local;
            {
                std::lock_guard<std::mutex> lk(mu_);
                for (auto& s : subs_) if (s.h) local.push_back(s.h);
            }
            for (auto& h : local) {
                try { h(ev.value); } catch (...) {}
            }
        }
        return result<void>::success();
    }

    void stop() noexcept { running_.store(false); }

private:
    struct sub { uint32_t id; handler h; };
    knst_vector<sub>      subs_;
    mutable std::mutex    mu_;
    std::atomic<uint32_t> next_id_{1};
    std::atomic<bool>     running_{false};
};

#elif KNST_USING_PLATFORM_WINDOWS

// ============================================================
// Windows — enumeration + safe control (PowerShell PnP)
// ============================================================

inline knst_vector<knst_usb_info> find_by_class(uint8_t class_code) noexcept {
    knst_vector<knst_usb_info> out;
    auto all = knst_devices::usb();
    for (auto& u : all) {
        knst_usb_info e = enrich(u);

        if (class_code == 0x09) {
            if (e.is_hub || e.is_root_hub) out.push_back(e);
            continue;
        }

        // Map USB class code → enum value
        knst_usb_class want = knst_usb_class::Unknown;
        switch (class_code) {
            case 0x01: want = knst_usb_class::Audio;       break;
            case 0x02: want = knst_usb_class::CDC;         break;
            case 0x03: want = knst_usb_class::HID;         break;
            case 0x07: want = knst_usb_class::Printer;     break;
            case 0x08: want = knst_usb_class::MassStorage; break;
            case 0x0E: want = knst_usb_class::Video;       break;
            case 0xE0: want = knst_usb_class::Wireless;    break;
            case 0xFF: want = knst_usb_class::Vendor;      break;
            default: break;
        }
        if (want != knst_usb_class::Unknown && e.usb_class == want) {
            out.push_back(e);
        }
    }
    return out;
}

inline knst_vector<knst_usb_info> find_by_driver(const knst_c16string& driver_name) noexcept {
    knst_vector<knst_usb_info> out;
    if (driver_name.empty()) return out;

    knst_c16string ps = u"Get-PnpDevice -PresentOnly | "
                       u"Where-Object { $_.Service -ne $null -and "
                       u"$_.Service -ieq '";
    ps.append(driver_name);
    ps.append(u"' } | Select-Object -Property DeviceID,Service,PNPClass");

    auto res = knst_process::run_capture(
        u"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
        { u"-NoProfile", u"-NonInteractive", u"-Command", ps },
        {}, nullptr, 0, 5000);
    if (res.error != knst_process_error::None) return out;

    knst_byte_string data = knst_devices::detail::wmi_normalize_encoding(res.out_data);
    if (data.empty()) return out;

    const char* p = knst_dev::text(data);
    while (p && *p) {
        const char* e = ::strchr(p, '\n');
        size_t len = e ? static_cast<size_t>(e - p) : ::strlen(p);
        char line[512];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        ::memcpy(line, p, len); line[len] = 0;

        const char* vid_ptr = ::strstr(line, "VID_");
        const char* pid_ptr = ::strstr(line, "PID_");
        if (vid_ptr && pid_ptr) {
            unsigned vid = 0, pid = 0;
            if (::sscanf(vid_ptr, "VID_%04X", &vid) == 1 &&
                ::sscanf(pid_ptr, "PID_%04X", &pid) == 1) {
                auto matches = knst_usbc::find(static_cast<uint16_t>(vid),
                                                static_cast<uint16_t>(pid));
                for (auto& m : matches) out.push_back(m);
            }
        }

        if (!e) break;
        p = e + 1;
    }
    return out;
}

inline knst_vector<knst_usb_info> find_hubs() noexcept {
    return find_by_class(0x09);
}

template<typename Predicate>
inline knst_vector<knst_usb_info> find_all(Predicate&& pred) noexcept {
    knst_vector<knst_usb_info> out;
    auto all = knst_devices::usb();
    for (auto& u : all) {
        knst_usb_info e = enrich(u);
        if (pred(e)) out.push_back(e);
    }
    return out;
}

class usb {
public:
    static result<void> set_authorized(const knst_usb_info&, bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> set_power_control(const knst_usb_info&, bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> set_autosuspend_delay_ms(const knst_usb_info&, uint32_t) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> set_remote_wakeup(const knst_usb_info&, bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> reset(const knst_usb_info&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> power_cycle(const knst_usb_info&, uint32_t = 500) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> bind_driver(const knst_usb_info&, const knst_c16string&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> unbind_driver(const knst_usb_info&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<usb_device_descriptor> descriptors(const knst_usb_info&) noexcept {
        return result<usb_device_descriptor>::fail(status::NotSupported);
    }
    static result<usb_strings> strings(const knst_usb_info&) noexcept {
        return result<usb_strings>::fail(status::NotSupported);
    }
    static result<knst_vector<usb_config_info>> configurations(const knst_usb_info&) noexcept {
        return result<knst_vector<usb_config_info>>::fail(status::NotSupported);
    }
    static result<usb_speed> speed(const knst_usb_info&) noexcept {
        return result<usb_speed>::fail(status::NotSupported);
    }
    static result<usb_state> state(const knst_usb_info&) noexcept {
        return result<usb_state>::fail(status::NotSupported);
    }
    static result<usb_power_info> power_info(const knst_usb_info&) noexcept {
        return result<usb_power_info>::fail(status::NotSupported);
    }
    static result<usb_port_info> port_info(const knst_usb_info&) noexcept {
        return result<usb_port_info>::fail(status::NotSupported);
    }
    static result<knst_vector<usb_port_info>> hub_ports(const knst_usb_info&) noexcept {
        return result<knst_vector<usb_port_info>>::fail(status::NotSupported);
    }
    static result<usb_error_stats> error_stats(const knst_usb_info&) noexcept {
        return result<usb_error_stats>::fail(status::NotSupported);
    }
    static result<usb_health_snapshot> health_check(const knst_usb_info&) noexcept {
        return result<usb_health_snapshot>::fail(status::NotSupported);
    }
    static result<knst_dev::uevent> wait_for_event(uint32_t) noexcept {
        return result<knst_dev::uevent>::fail(status::NotSupported);
    }

        // Windows eject (PnP disable + enable) via PowerShell WMI.
    // Requires admin privileges. `delay_ms` is the time the device
    // stays disabled before being re-enabled (default 500 ms).
    static result<void> eject_device(const knst_usb_info& u,
                                      uint32_t delay_ms = 500) noexcept {
        using R = result<void>;

        knst_c16string ps = u"$dev = Get-PnpDevice -PresentOnly | "
                            u"Where-Object { $_.InstanceId -like '*VID_";
        char vid_hex[8], pid_hex[8];
        ::snprintf(vid_hex, sizeof(vid_hex), "%04X", u.vid);
        ::snprintf(pid_hex, sizeof(pid_hex), "%04X", u.pid);
        ps.append(knst_c16string(vid_hex));
        ps.append(u"&PID_");
        ps.append(knst_c16string(pid_hex));
        ps.append(u"*' } | Select-Object -First 1; "
                  u"if (-not $dev) { Write-Output 'NOT_FOUND'; exit 1 }; "
                  u"Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction Stop; "
                  u"Start-Sleep -Milliseconds ");
                    ps.append(knst_dev::u32_to_string(delay_ms));
        ps.append(u"; "
                  u"Enable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction Stop; "
                  u"Write-Output 'OK'");

        auto res = knst_process::run_capture(
            u"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
            { u"-NoProfile", u"-NonInteractive", u"-Command", ps },
            {}, nullptr, 0, 15000);

        if (res.error != knst_process_error::None)
            return R::fail(status::ToolMissing);

        if (res.exit_code != 0) {
            knst_byte_string data = knst_devices::detail::wmi_normalize_encoding(res.out_data);
            knst_c16string txt(knst_dev::text(data));
            if (txt.contains(u"NOT_FOUND"))
                return R::fail(status::NotFound);
            return R::fail(status::PermissionDenied, res.exit_code);
        }

        return R::success();
    }
};

class usb_event_bus {
public:
    using handler = knst_function_shared<const knst_dev::uevent&>;
    uint32_t subscribe(handler) noexcept { return 0; }
    void unsubscribe(uint32_t) noexcept {}
    result<void> run(uint32_t = 0) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    void stop() noexcept {}
};

// ============================================================
// Android — read-only enumeration via JNI UsbManager
// ============================================================
//
// Android's USB stack is heavily restricted for non-system apps:
//   @ Enumeration (UsbManager.getDeviceList via JNI)
//   @ Basic strings (product/manufacturer/serial) — cached
//   @ VID/PID/class — cached
//   * sysfs reset, power control, bind/unbind (SELinux)
//   * Hub topology (hidden)
//   * Hotplug events (needs BroadcastReceiver + permission)
//   * Descriptor read (needs openDevice + permission)
//   * Transfer speed (not exposed by UsbManager)
//
// Everything mutating or querying low-level state returns
// NotSupported. Enumeration mirrors knst_devices::usb().
//
// There is no `detail_usb` namespace on Android because sysfs
// access is unreliable. `find()` / `find_by_vid_pid()` (declared
// above the platform guard) work everywhere through
// knst_devices::usb().

#elif defined(KNST_USING_PLATFORM_ANDROID)

inline knst_vector<knst_usb_info> find_by_class(uint8_t class_code) noexcept {
    knst_vector<knst_usb_info> out;
    auto all = knst_devices::usb();
    for (auto& u : all) {
        knst_usb_info e = enrich(u);
        if (class_code == 0x09) {
            if (e.is_hub || e.is_root_hub) out.push_back(e);
            continue;
        }
        // Enum mapping
        knst_usb_class want = knst_usb_class::Unknown;
        switch (class_code) {
            case 0x01: want = knst_usb_class::Audio;       break;
            case 0x02: want = knst_usb_class::CDC;         break;
            case 0x03: want = knst_usb_class::HID;         break;
            case 0x07: want = knst_usb_class::Printer;     break;
            case 0x08: want = knst_usb_class::MassStorage; break;
            case 0x0E: want = knst_usb_class::Video;       break;
            case 0xE0: want = knst_usb_class::Wireless;    break;
            case 0xFF: want = knst_usb_class::Vendor;      break;
            default: break;
        }
        if (want != knst_usb_class::Unknown && e.usb_class == want)
            out.push_back(e);
    }
    return out;
}

inline knst_vector<knst_usb_info> find_by_driver(const knst_c16string&) noexcept {
    // UsbManager does not expose kernel driver bindings.
    return knst_vector<knst_usb_info>();
}

inline knst_vector<knst_usb_info> find_hubs() noexcept {
    return find_by_class(0x09);
}

template<typename Predicate>
inline knst_vector<knst_usb_info> find_all(Predicate&& pred) noexcept {
    knst_vector<knst_usb_info> out;
    auto all = knst_devices::usb();
    for (auto& u : all) {
        knst_usb_info e = enrich(u);
        if (pred(e)) out.push_back(e);
    }
    return out;
}

class usb {
public:
    // ---------- Mutation: all NotSupported on Android ----------
    static result<void> set_authorized(const knst_usb_info&, bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> set_power_control(const knst_usb_info&, bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> set_autosuspend_delay_ms(const knst_usb_info&, uint32_t) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> set_remote_wakeup(const knst_usb_info&, bool) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> reset(const knst_usb_info&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> power_cycle(const knst_usb_info&, uint32_t = 500) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> bind_driver(const knst_usb_info&, const knst_c16string&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> unbind_driver(const knst_usb_info&) noexcept {
        return result<void>::fail(status::NotSupported);
    }

    // ---------- Descriptors: best-effort from cached info ----------
    static result<usb_device_descriptor> descriptors(const knst_usb_info& u) noexcept {
        using R = result<usb_device_descriptor>;
        if (!u.valid) return R::fail(status::NotFound);

        usb_device_descriptor d;
        d.id_vendor    = u.vid;
        d.id_product   = u.pid;
        d.device_class = static_cast<uint8_t>(u.usb_class);

        // bcdUSB from cached usb_version (" 2.10" / " 3.10")
        if (!u.usb_version.empty()) {
            knst_byte_string v = knst_dev::to_utf8(u.usb_version);
            int major = 0, minor = 0;
            if (::sscanf(knst_dev::text(v), "%d.%d", &major, &minor) == 2) {
                int minor_bcd = ((minor / 10) << 4) | (minor % 10);
                d.bcd_usb = static_cast<uint16_t>((major << 8) | minor_bcd);
            }
        }
        d.valid = true;
        return R::success(std::move(d));
    }

    static result<usb_strings> strings(const knst_usb_info& u) noexcept {
        using R = result<usb_strings>;
        if (!u.valid) return R::fail(status::NotFound);
        usb_strings s;
        s.manufacturer = u.manufacturer;
        s.product      = u.product_name;
        s.serial       = u.serial;
        s.valid        = true;
        return R::success(std::move(s));
    }

    // ---------- Queries: NotSupported on Android ----------
    static result<knst_vector<usb_config_info>> configurations(const knst_usb_info&) noexcept {
        return result<knst_vector<usb_config_info>>::fail(status::NotSupported);
    }
    static result<usb_speed> speed(const knst_usb_info&) noexcept {
        // UsbDevice does not expose the negotiated speed.
        return result<usb_speed>::fail(status::NotSupported);
    }
    static result<usb_state> state(const knst_usb_info& u) noexcept {
        using R = result<usb_state>;
        if (!u.valid) return R::fail(status::NotFound);
        return R::success(usb_state::Configured);
    }
    static result<usb_power_info> power_info(const knst_usb_info&) noexcept {
        return result<usb_power_info>::fail(status::NotSupported);
    }
    static result<usb_port_info> port_info(const knst_usb_info&) noexcept {
        return result<usb_port_info>::fail(status::NotSupported);
    }
    static result<knst_vector<usb_port_info>> hub_ports(const knst_usb_info&) noexcept {
        return result<knst_vector<usb_port_info>>::fail(status::NotSupported);
    }
    static result<usb_error_stats> error_stats(const knst_usb_info&) noexcept {
        return result<usb_error_stats>::fail(status::NotSupported);
    }
    static result<usb_health_snapshot> health_check(const knst_usb_info&) noexcept {
        return result<usb_health_snapshot>::fail(status::NotSupported);
    }
    static result<knst_dev::uevent> wait_for_event(uint32_t) noexcept {
        return result<knst_dev::uevent>::fail(status::NotSupported);
    }
};

class usb_event_bus {
public:
    using handler = knst_function_shared<const knst_dev::uevent&>;
    uint32_t subscribe(handler) noexcept { return 0; }
    void unsubscribe(uint32_t) noexcept {}
    result<void> run(uint32_t = 0) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    void stop() noexcept {}
};

#endif // platform

} // namespace knst_usbc