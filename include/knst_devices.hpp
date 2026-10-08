// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
knst_devices.hpp
----------------------------

    A hardware inventory library spanning 18 categories that reads all system hardware
    (CPU, RAM, disk, network, USB, PCI, monitor, sensor, camera, etc.) into standardized info structures, operating via `/proc`, `/sys`, and external tools on Linux, and via WMI on Windows.

*/

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cerrno>
#include <mutex>
#include <initializer_list>
#include <cctype>

#if !KNST_USING_PLATFORM_WINDOWS
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/statvfs.h>
    #include <sys/utsname.h>
    #include <sys/types.h>
    #include <pwd.h>
    #include <fcntl.h>
    #include <dlfcn.h>
    #include <net/if.h>
    #include <ifaddrs.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #if defined(__linux__)
        #include <linux/ethtool.h>
        #include <linux/sockios.h>
    #endif
#else
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <winternl.h>
    #include <shellapi.h>
    #include <dbt.h>
    #include <hidsdi.h>
    #include <mmdeviceapi.h>
    #include <functiondiscoverykeys_devpkey.h>
    #include <bluetoothapis.h>
    #include <setupapi.h>
    #include <devguid.h>
    #include <initguid.h>
    #include <dshow.h>
    #include <wbemidl.h>

    #pragma comment(lib, "setupapi.lib")
    #pragma comment(lib, "bthprops.lib")
    #pragma comment(lib, "ole32.lib")
    #pragma comment(lib, "strmiids.lib")
    #pragma comment(lib, "hid.lib")
    #pragma comment(lib, "iphlpapi.lib")

    #pragma comment(lib, "oleaut32.lib")

    #pragma comment(lib, "wbemuuid.lib")
#endif



enum class knst_device_error : uint8_t {
    None = 0,
    NotFound,
    PermissionDenied,
    NotSupported,
    ReadFailed,
    ParseFailed,
    ToolMissing,
    Timeout,
    Unknown
};

inline const char* knst_device_error_string(knst_device_error e) noexcept {
    switch (e) {
        case knst_device_error::None:             return "No error";
        case knst_device_error::NotFound:         return "Not found";
        case knst_device_error::PermissionDenied: return "Permission denied";
        case knst_device_error::NotSupported:     return "Not supported";
        case knst_device_error::ReadFailed:       return "Read failed";
        case knst_device_error::ParseFailed:      return "Parse failed";
        case knst_device_error::ToolMissing:      return "Required tool missing";
        case knst_device_error::Timeout:          return "Timeout";
        case knst_device_error::Unknown:          return "Unknown error";
    }
    return "Unknown error";
}



enum class knst_storage_type : uint8_t {
    Unknown = 0, HDD, SSD, NVMe, USB, eMMC, SD_Card, Optical, Floppy, RAID, Virtual
};

enum class knst_net_type : uint8_t {
    Unknown = 0, Ethernet, WiFi, Loopback, Bridge, Virtual, Cellular, Bluetooth, Other
};

enum class knst_input_type : uint8_t {
    Unknown = 0, Keyboard, Mouse, Touchpad, Touchscreen, Joystick, Gamepad, Tablet, Remote, Other
};

enum class knst_monitor_conn : uint8_t {
    Unknown = 0, HDMI, DisplayPort, DVI, VGA, USB_C, LVDS, eDP, Internal, Wireless, Other
};

enum class knst_battery_state : uint8_t {
    Unknown = 0, Charging, Discharging, Full, NotCharging, Empty
};

enum class knst_sensor_type : uint8_t {
    Unknown = 0, Temperature, Fan, Voltage, Current, Power, Humidity, Proximity, Light
};

enum class knst_usb_class : uint8_t {
    Unknown = 0, Audio, Video, HID, MassStorage, Printer, Hub, Wireless, CDC, Vendor, Other
};

enum class knst_device_power_state : uint8_t {
    Unknown = 0, D0, D1, D2, D3hot, D3cold
};



struct knst_cpu_info {
    knst_c16string vendor;
    knst_c16string model_name;
    knst_c16string architecture;
    uint32_t physical_cores = 0;
    uint32_t logical_cores = 0;
    uint32_t socket_count = 0;
    uint32_t base_mhz = 0;
    uint32_t max_mhz = 0;
    uint32_t current_mhz = 0;
    uint32_t cache_l1_kb = 0;
    uint32_t cache_l2_kb = 0;
    uint32_t cache_l3_kb = 0;
    int32_t  temperature_c = 0;
    uint32_t usage_percent = 0;
    double   load_avg_1 = 0.0;
    double   load_avg_5 = 0.0;
    double   load_avg_15 = 0.0;
    uint32_t stepping = 0;
    uint32_t microcode_ver  = 0;

    bool has_sse4 = false;
    bool has_avx = false;
    bool has_avx2 = false;
    bool has_avx512 = false;
    bool has_aes = false;
    bool has_vmx = false;
    bool has_iommu = false;
    bool has_hyperthreading = false;

    bool valid = false;
    bool has_cores  = false;
    bool has_frequency = false;
    bool has_cache = false;
    bool has_temperature = false;
    bool has_usage = false;
};


struct knst_motherboard_info {
    knst_c16string manufacturer;
    knst_c16string product;
    knst_c16string version;
    knst_c16string serial;
    knst_c16string uuid;

    knst_c16string bios_vendor;
    knst_c16string bios_version;
    knst_c16string bios_date;

    bool uefi_boot = false;
    bool secure_boot = false;
    bool has_tpm = false;
    knst_c16string tpm_version;

    knst_c16string chassis_type;
    knst_c16string chassis_serial;

    bool valid = false;
    bool has_bios = false;
    bool has_board  = false;
    bool has_chassis = false;
};


struct knst_memory_module {
    uint32_t slot_index = 0;
    knst_c16string locator;
    uint64_t size_bytes = 0;
    uint32_t speed_mhz  = 0;
    uint32_t configured_mhz = 0;
    knst_c16string type;
    knst_c16string form_factor;
    knst_c16string manufacturer;
    knst_c16string serial;
    knst_c16string part_number;
    uint32_t rank = 0;
    uint32_t voltage_mv = 0;
    bool ecc = false;
    bool valid = false;
};

struct knst_memory_info {
    uint64_t total_bytes     = 0;
    uint64_t used_bytes      = 0;
    uint64_t free_bytes      = 0;
    uint64_t available_bytes = 0;
    uint64_t cached_bytes    = 0;
    uint64_t buffers_bytes   = 0;
    uint64_t swap_total      = 0;
    uint64_t swap_used       = 0;
    uint64_t swap_free       = 0;

    uint32_t slot_count = 0;
    uint32_t slots_used = 0;

    knst_vector<knst_memory_module> modules;

    bool valid          = false;
    bool has_modules    = false;
    bool has_swap       = false;
    bool has_slot_count = false;
};


struct knst_storage_partition {
    knst_c16string device;
    knst_c16string mount_point;
    knst_c16string fs_type;
    uint64_t total_bytes = 0;
    uint64_t used_bytes  = 0;
    uint64_t free_bytes  = 0;
    knst_c16string uuid;
    knst_c16string label;
    bool is_boot      = false;
    bool is_removable = false;
    bool valid        = false;
};

struct knst_storage_info {
    knst_c16string device;
    knst_c16string model;
    knst_c16string serial;
    knst_c16string firmware;
    knst_c16string vendor;
    knst_storage_type type = knst_storage_type::Unknown;
    uint64_t total_bytes = 0;
    uint64_t free_bytes  = 0;
    uint64_t used_bytes  = 0;
    int32_t  temperature_c = 0;
    bool is_removable   = false;
    bool is_system_disk = false;
    bool is_solid_state = false;
    uint32_t rotational_rpm = 0;

    bool has_smart      = false;
    bool smart_passed   = false;
    uint32_t smart_power_on_hours       = 0;
    uint64_t smart_bytes_written        = 0;
    uint32_t smart_reallocated_sectors  = 0;
    uint32_t smart_pending_sectors      = 0;
    uint32_t smart_percent_used         = 0;

    knst_vector<knst_storage_partition> partitions;

    bool valid           = false;
    bool has_partitions  = false;
    bool has_temperature = false;
    bool has_model       = false;
};


struct knst_network_info {
    knst_c16string name;
    knst_c16string description;
    knst_c16string mac;
    knst_c16string ipv4;
    knst_c16string ipv6;
    knst_c16string netmask;
    knst_c16string gateway;
    knst_c16string dns;

    knst_net_type type = knst_net_type::Unknown;
    uint32_t mtu = 0;
    uint32_t speed_mbps = 0;
    bool is_up       = false;
    bool is_running  = false;
    bool is_loopback = false;
    bool is_wireless = false;
    bool is_virtual  = false;
    uint8_t duplex   = 0;

    uint64_t rx_bytes = 0, tx_bytes = 0;
    uint64_t rx_packets = 0, tx_packets = 0;
    uint64_t rx_errors = 0, tx_errors = 0;
    uint64_t rx_dropped = 0, tx_dropped = 0;

    knst_c16string ssid;
    int32_t  signal_dbm    = 0;
    uint32_t frequency_mhz = 0;
    uint32_t channel       = 0;
    knst_c16string security;

    bool valid = false;
    bool has_ip = false;
    bool has_stats = false;
    bool has_wifi = false;
};


struct knst_usb_info {
    uint32_t bus = 0;
    uint32_t device_num = 0;
    uint32_t port_num   = 0;
    uint16_t vid = 0;
    uint16_t pid = 0;
    knst_c16string vendor_name;
    knst_c16string product_name;
    knst_c16string serial;
    knst_c16string manufacturer;
    knst_usb_class usb_class = knst_usb_class::Unknown;
    knst_c16string usb_version;
    uint32_t max_speed_mbps = 0;
    uint32_t max_power_ma   = 0;
    bool is_hub = false;
    bool is_root_hub = false;
    bool valid = false;
};


struct knst_pci_info {
    uint32_t domain = 0;
    uint32_t bus = 0;
    uint32_t slot = 0;
    uint32_t function = 0;
    uint16_t vendor_id = 0;
    uint16_t device_id = 0;
    knst_c16string vendor_name;
    knst_c16string device_name;
    knst_c16string class_name;
    knst_c16string subclass;
    knst_c16string driver;
    uint32_t pcie_gen = 0;
    uint32_t pcie_width = 0;
    knst_device_power_state power_state = knst_device_power_state::Unknown;
    int32_t iommu_group = -1;
    int32_t numa_node = -1;
    bool valid = false;
};


struct knst_monitor_info {
    uint32_t index = 0;
    knst_c16string name;
    knst_c16string manufacturer;
    knst_c16string serial;
    uint32_t product_code = 0;
    uint32_t year = 0, week = 0;
    knst_monitor_conn connection = knst_monitor_conn::Unknown;
    uint32_t native_width  = 0;
    uint32_t native_height = 0;
    uint32_t current_width = 0;
    uint32_t current_height = 0;
    uint32_t refresh_hz = 0;
    uint32_t physical_width_mm  = 0;
    uint32_t physical_height_mm = 0;
    float diagonal_inch = 0.0f;
    float gamma = 0.0f;
    bool hdr = false;
    bool is_primary = false;
    uint32_t brightness = 0;
    bool valid = false;
};


struct knst_input_info {
    knst_c16string name;
    knst_c16string vendor;
    knst_c16string device_path;
    uint16_t vid = 0, pid = 0;
    knst_input_type type = knst_input_type::Unknown;
    uint32_t num_buttons = 0;
    uint32_t num_axes = 0;
    bool has_force_feedback = false;
    bool is_connected = true;
    bool valid = false;
};


struct knst_audio_device {
    uint32_t index = 0;
    knst_c16string name;
    knst_c16string description;
    bool is_input   = false;
    bool is_output  = false;
    bool is_default = false;
    uint32_t volume_percent = 0;
    bool muted = false;
    uint32_t sample_rate = 0;
    uint32_t channels    = 0;
    bool valid = false;
};

struct knst_audio_info {
    knst_c16string default_sink;
    knst_c16string default_source;
    knst_vector<knst_audio_device> devices;
    bool valid = false;
};


struct knst_battery_info {
    knst_c16string name;
    knst_c16string manufacturer;
    knst_c16string model;
    knst_c16string serial;
    knst_c16string technology;
    knst_battery_state state = knst_battery_state::Unknown;
    uint32_t percent = 0;
    uint32_t capacity_mah  = 0;
    uint32_t capacity_full_mah = 0;
    uint32_t capacity_design_mah = 0;
    uint32_t voltage_mv = 0;
    int32_t  current_ma = 0;
    uint32_t power_now_mw = 0;
    uint32_t seconds_left = 0;
    uint32_t cycle_count  = 0;
    uint32_t health_percent = 0;
    bool ac_online = false;
    bool valid = false;
};

struct knst_power_info {
    bool has_battery = false;
    knst_vector<knst_battery_info> batteries;
    bool ac_connected = false;
    knst_c16string power_profile;
    bool valid = false;
};


struct knst_sensor_info {
    knst_c16string name;
    knst_c16string chip;
    knst_sensor_type type = knst_sensor_type::Unknown;
    double value = 0.0;
    knst_c16string unit;
    double min_value = 0.0;
    double max_value = 0.0;
    double critical  = 0.0;
    bool valid = false;
};

struct knst_bluetooth_device {
    knst_c16string address;
    knst_c16string name;
    bool connected = false;
    bool paired = false;
    bool trusted = false;
    int32_t rssi = 0;
    knst_c16string type;
    bool valid = false;
};

struct knst_bluetooth_info {
    bool has_adapter = false;
    knst_c16string adapter_name;
    knst_c16string adapter_address;
    bool powered      = false;
    bool discoverable = false;
    knst_vector<knst_bluetooth_device> devices;
    bool valid = false;
};


struct knst_camera_info {
    knst_c16string name;
    knst_c16string device_path;
    uint16_t vid = 0, pid = 0;
    uint32_t max_width  = 0;
    uint32_t max_height = 0;
    knst_vector<knst_c16string> formats;
    bool valid = false;
};


struct knst_printer_info {
    knst_c16string name;
    knst_c16string model;
    knst_c16string status;
    knst_c16string uri;
    bool is_default  = false;
    bool is_shared   = false;
    bool accepts_jobs = true;
    bool valid = false;
};

struct knst_system_info {
    knst_c16string manufacturer;
    knst_c16string product_name;
    knst_c16string version;
    knst_c16string serial;
    knst_c16string uuid;
    knst_c16string sku;
    knst_c16string family;
    knst_c16string chassis_type;
    knst_c16string chassis_serial;
    knst_c16string asset_tag;
    bool is_virtual_machine = false;
    bool is_laptop          = false;
    bool valid = false;
};

struct knst_os_info {
    knst_c16string os_name;
    knst_c16string os_version;
    knst_c16string os_id;
    knst_c16string kernel_name;
    knst_c16string kernel_version;
    knst_c16string kernel_arch;
    knst_c16string hostname;
    knst_c16string domain;
    knst_c16string timezone;
    knst_c16string user;
    knst_c16string home_dir;
    knst_c16string shell;
    uint64_t boot_time_ms    = 0;
    uint64_t uptime_seconds  = 0;
    uint32_t page_size       = 0;
    uint64_t boot_id         = 0;
    bool valid = false;
};


struct knst_driver_info {
    knst_c16string name;
    knst_c16string version;
    knst_c16string vendor;
    knst_c16string license;
    uint32_t used_by_count = 0;
    uint64_t size_bytes    = 0;
    knst_vector<knst_c16string> used_by;
    bool is_builtin = false;
    bool valid = false;
};



namespace knst_devices {

knst_cpu_info                  cpu()         noexcept;
knst_motherboard_info          motherboard() noexcept;
knst_memory_info               memory()      noexcept;
knst_vector<knst_storage_info> storage()     noexcept;
knst_vector<knst_network_info> network()     noexcept;
knst_vector<knst_usb_info>     usb()         noexcept;
knst_vector<knst_pci_info>     pci()         noexcept;
knst_vector<knst_monitor_info> monitors()    noexcept;
knst_vector<knst_input_info>   input()       noexcept;
knst_audio_info                audio()       noexcept;
knst_power_info                power()       noexcept;
knst_vector<knst_sensor_info>  sensors()     noexcept;
knst_bluetooth_info            bluetooth()   noexcept;
knst_vector<knst_camera_info>  cameras()     noexcept;
knst_vector<knst_printer_info> printers()    noexcept;
knst_system_info               system()      noexcept;
knst_os_info                   os()          noexcept;
knst_vector<knst_driver_info>  drivers()     noexcept;

inline knst_device_error last_error() noexcept;

knst_vector<knst_gpu_info>    gpus()                     noexcept;
knst_vector<knst_gpu_process> gpu_processes()            noexcept;
knst_vector<knst_gpu_process> gpu_processes(uint32_t idx) noexcept;
uint32_t                      gpu_count()                noexcept;
bool                          has_gpu()                  noexcept;
knst_c16string                nvidia_driver_version()    noexcept;
knst_c16string                cuda_version()             noexcept;

} // namespace knst_devices



#if !KNST_USING_PLATFORM_WINDOWS

namespace knst_devices {
namespace detail {

inline bool read_file(const char* path, char* buf, size_t sz) noexcept {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = ::read(fd, buf, sz - 1);
    ::close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return true;
}

inline bool read_file_u64(const char* path, uint64_t& out) noexcept {
    char buf[64];
    if (!read_file(path, buf, sizeof(buf))) return false;
    out = (uint64_t)::strtoull(buf, nullptr, 10);
    return true;
}

inline bool read_file_i64(const char* path, int64_t& out) noexcept {
    char buf[64];
    if (!read_file(path, buf, sizeof(buf))) return false;
    out = (int64_t)::strtoll(buf, nullptr, 10);
    return true;
}

inline bool read_symlink(const char* path, char* buf, size_t sz) noexcept {
    ssize_t n = ::readlink(path, buf, sz - 1);
    if (n <= 0) return false;
    buf[n] = '\0';
    return true;
}

inline bool starts_with(const char* s, const char* prefix) noexcept {
    return ::strncmp(s, prefix, ::strlen(prefix)) == 0;
}

inline bool contains_ci(const char* haystack, const char* needle) noexcept {
    if (!haystack || !needle || !*needle) return false;
    size_t hn = ::strlen(haystack), nn = ::strlen(needle);
    if (nn > hn) return false;
    for (size_t i = 0; i + nn <= hn; ++i) {
        size_t j = 0;
        for (; j < nn; ++j) {
            char a = haystack[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (j == nn) return true;
    }
    return false;
}


inline bool device_matches_disk(const knst_c16string& mounted_dev,const knst_c16string& disk_dev) noexcept {
                                
    uint32_t dl = disk_dev.length();
    if (mounted_dev.length() < dl) return false;
    for (uint32_t i = 0; i < dl; ++i) {
        if (mounted_dev[i] != disk_dev[i]) return false;
    }
    if (mounted_dev.length() == dl) return true;

    char16_t last_disk_char = dl > 0 ? disk_dev[dl - 1] : u'\0';
    char16_t next = mounted_dev[dl];
    bool disk_ends_digit = (last_disk_char >= u'0' && last_disk_char <= u'9');

    if (disk_ends_digit) {
        if (next != u'p') return false;
        if (mounted_dev.length() <= dl + 1) return false;
        char16_t after_p = mounted_dev[dl + 1];
        return (after_p >= u'0' && after_p <= u'9');
    }
    return (next >= u'0' && next <= u'9');
}



inline knst_byte_string resolve_dm_slave_impl(const char* name, int depth) noexcept {
    knst_byte_string result;
    if (depth > 8) return result;
    if (!name || !*name) return result;

    // Find the dm-N device whose /dm/name matches `name`.
    char target_dm[32] = {0};
    DIR* d = ::opendir("/sys/block");
    if (!d) return result;

    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (::strncmp(e->d_name, "dm-", 3) != 0) continue;

        char namepath[256];
        char namebuf[128] = {0};
        ::snprintf(namepath, sizeof(namepath), "/sys/block/%s/dm/name", e->d_name);
        if (!read_file(namepath, namebuf, sizeof(namebuf))) continue;

        if (::strcmp(namebuf, name) == 0) {
            const char* src = e->d_name;
            size_t sl = ::strlen(src);
            if (sl >= sizeof(target_dm)) sl = sizeof(target_dm) - 1;
            ::memcpy(target_dm, src, sl);
            target_dm[sl] = '\0';
            break;
        }
    }
    ::closedir(d);

    if (target_dm[0] == '\0') return result;

    // Read the first slave.
    char slaves_dir[256];
    ::snprintf(slaves_dir, sizeof(slaves_dir), "/sys/block/%s/slaves", target_dm);

    DIR* sd = ::opendir(slaves_dir);
    if (!sd) return result;

    struct dirent* se;
    while ((se = ::readdir(sd)) != nullptr) {
        if (se->d_name[0] == '.') continue;
        const char* slave = se->d_name;

        // Another dm device? Recurse — this is the LUKS/LVM layering case.
        if (::strncmp(slave, "dm-", 3) == 0) {
            char subnamepath[256];
            char subname[128] = {0};
            ::snprintf(subnamepath, sizeof(subnamepath),
                       "/sys/block/%s/dm/name", slave);
            if (read_file(subnamepath, subname, sizeof(subname))) {
                ::closedir(sd);
                return resolve_dm_slave_impl(subname, depth + 1);
            }
            continue;
        }

        // Physical partition — strip the partition suffix.
        const char* disk_end = slave;
        if (::strncmp(slave, "nvme", 4) == 0 || ::strncmp(slave, "mmcblk", 6) == 0) {
            const char* p = ::strchr(slave, 'p');
            if (p && p != slave && ::isdigit((unsigned char)p[1])) disk_end = p;
            else disk_end = slave + ::strlen(slave);
        } else {
            const char* p = slave;
            while (*p && !::isdigit((unsigned char)*p)) ++p;
            disk_end = p;
        }

        size_t disk_len = (size_t)(disk_end - slave);
        if (disk_len == 0) continue;

        char disk[64] = {0};
        if (disk_len >= sizeof(disk)) disk_len = sizeof(disk) - 1;
        ::memcpy(disk, slave, disk_len);
        disk[disk_len] = '\0';

        result = knst_byte_string(disk);
        break;
    }
    ::closedir(sd);
    return result;
}

inline knst_byte_string resolve_dm_slave(const char* mapper_name) noexcept {
    return resolve_dm_slave_impl(mapper_name, 0);
}

inline void trim_right(char* s) noexcept {
    size_t n = ::strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r'|| s[n-1] == '\n')) s[--n] = '\0';
                     
}

inline bool split_kv(const char* line, const char* key, const char*& value_out) noexcept {
    size_t kl = ::strlen(key);
    if (::strncmp(line, key, kl) != 0) return false;
    const char* p = line + kl;
    if (*p != ' ' && *p != '\t' && *p != ':' && *p != '=') return false;
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=') ++p;
    value_out = p;
    return true;
}


inline void copy_bounded(char* dst, size_t dst_sz, const char* src) noexcept {
    size_t vl = ::strlen(src);
    if (vl >= dst_sz) vl = dst_sz - 1;
    ::memcpy(dst, src, vl);
    dst[vl] = '\0';
}

inline bool dir_has_prefix(const char* dir, const char* prefix) noexcept {
    DIR* d = ::opendir(dir);
    if (!d) return false;
    struct dirent* e;
    bool found = false;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        if (starts_with(e->d_name, prefix)) { found = true; break; }
    }
    ::closedir(d);
    return found;
}

struct tool_output {
    knst_byte_string data;
    bool ok = false;
    explicit operator bool() const noexcept { return ok; }
    const char* c_str() const noexcept { return ok ? (const char*)data.data() : ""; }
};

inline tool_output tool(const char* prog,std::initializer_list<const char*> args,uint32_t timeout_ms = 3000) noexcept {
    tool_output r;
    knst_vector<knst_c16string> a;
    for (auto s : args) a.push_back(knst_c16string(s));
    auto res = knst_process::run_capture(knst_c16string(prog), a, {}, nullptr, 0, timeout_ms);
    if (res.error != knst_process_error::None || res.exit_code != 0) return r;
    r.data = std::move(res.out_data);
    r.ok = true;
    return r;
}


struct dmi_cache_t {
    bool loaded = false;
    char manufacturer[256]   = {0};
    char product[256]        = {0};
    char version[256]        = {0};
    char serial[256]         = {0};
    char uuid[256]           = {0};
    char sku[256]            = {0};
    char family[256]         = {0};
    char bios_vendor[256]    = {0};
    char bios_version[256]   = {0};
    char bios_date[256]      = {0};
    char chassis_type[256]   = {0};
    char chassis_serial[256] = {0};
    char asset_tag[256]      = {0};
    char board_mfg[256]      = {0};
    char board_product[256]  = {0};
    char board_version[256]  = {0};
    char board_serial[256]   = {0};
};


inline const dmi_cache_t& dmi_cache() noexcept {
    static dmi_cache_t cache;
    static std::once_flag once;

        std::call_once(once, []() {
        auto r = tool("dmidecode", { "-q" }, 3000);

        // dmidecode requires root on most systems. If it's unavailable or
        // permission-denied, fall back to /sys/class/dmi/id/* — those files
        // are world-readable on Linux and cover the common cases.
        if (!r) {
            auto load = [](const char* path, char* dst, size_t dst_sz) {
                if (dst_sz == 0) return;
                char tmp[256] = {0};
                if (!read_file(path, tmp, sizeof(tmp))) return;
                // Skip sentinel values that mean "not set" in DMI.
                if (::strcmp(tmp, "Not Specified") == 0) return;
                if (::strcmp(tmp, "Not Present") == 0) return;
                if (::strcmp(tmp, "To be filled by O.E.M.") == 0) return;
                if (::strcmp(tmp, "Default string") == 0) return;
                copy_bounded(dst, dst_sz, tmp);
            };

            load("/sys/class/dmi/id/sys_vendor",          cache.manufacturer,   sizeof(cache.manufacturer));
            load("/sys/class/dmi/id/product_name",        cache.product,        sizeof(cache.product));
            load("/sys/class/dmi/id/product_version",     cache.version,        sizeof(cache.version));
            load("/sys/class/dmi/id/product_serial",      cache.serial,         sizeof(cache.serial));
            load("/sys/class/dmi/id/product_uuid",        cache.uuid,           sizeof(cache.uuid));
            load("/sys/class/dmi/id/product_sku",         cache.sku,            sizeof(cache.sku));
            load("/sys/class/dmi/id/product_family",      cache.family,         sizeof(cache.family));

            load("/sys/class/dmi/id/board_vendor",        cache.board_mfg,      sizeof(cache.board_mfg));
            load("/sys/class/dmi/id/board_name",          cache.board_product,  sizeof(cache.board_product));
            load("/sys/class/dmi/id/board_version",       cache.board_version,  sizeof(cache.board_version));
            load("/sys/class/dmi/id/board_serial",        cache.board_serial,   sizeof(cache.board_serial));

            load("/sys/class/dmi/id/bios_vendor",         cache.bios_vendor,    sizeof(cache.bios_vendor));
            load("/sys/class/dmi/id/bios_version",        cache.bios_version,   sizeof(cache.bios_version));
            load("/sys/class/dmi/id/bios_date",           cache.bios_date,      sizeof(cache.bios_date));

            load("/sys/class/dmi/id/chassis_type",        cache.chassis_type,   sizeof(cache.chassis_type));
            load("/sys/class/dmi/id/chassis_serial",      cache.chassis_serial, sizeof(cache.chassis_serial));
            load("/sys/class/dmi/id/chassis_asset_tag",   cache.asset_tag,      sizeof(cache.asset_tag));

            cache.loaded = true;
            return;
        }

        enum class Section { None, System, Bios, BaseBoard, Chassis };
        Section section = Section::None;

        const char* p = r.c_str();
        while (*p) {
            const char* e = ::strchr(p, '\n');
            size_t linelen = e ? (size_t)(e - p) : ::strlen(p);
            char line[256];
            if (linelen >= sizeof(line)) linelen = sizeof(line) - 1;
            ::memcpy(line, p, linelen); line[linelen] = '\0';

            bool indented = (line[0] == '\t' || line[0] == ' ');

            if (!indented) {

                if (::strcmp(line, "System Information") == 0)          section = Section::System;
                else if (::strcmp(line, "BIOS Information") == 0)       section = Section::Bios;
                else if (::strcmp(line, "Base Board Information") == 0) section = Section::BaseBoard;
                else if (::strcmp(line, "Chassis Information") == 0)    section = Section::Chassis;
                else                                                    section = Section::None;
            } else {
                char* content = line;
                while (*content == '\t' || *content == ' ') ++content;
                trim_right(content);

                const char* v;
                switch (section) {
                    case Section::System:
                        if (split_kv(content, "Manufacturer", v))        copy_bounded(cache.manufacturer, sizeof(cache.manufacturer), v);
                        else if (split_kv(content, "Product Name", v))   copy_bounded(cache.product, sizeof(cache.product), v);
                        else if (split_kv(content, "Version", v))        copy_bounded(cache.version, sizeof(cache.version), v);
                        else if (split_kv(content, "Serial Number", v))  copy_bounded(cache.serial, sizeof(cache.serial), v);
                        else if (split_kv(content, "UUID", v))           copy_bounded(cache.uuid, sizeof(cache.uuid), v);
                        else if (split_kv(content, "SKU Number", v))     copy_bounded(cache.sku, sizeof(cache.sku), v);
                        else if (split_kv(content, "Family", v))         copy_bounded(cache.family, sizeof(cache.family), v);
                        break;
                    case Section::Bios:
                        if (split_kv(content, "Vendor", v))              copy_bounded(cache.bios_vendor, sizeof(cache.bios_vendor), v);
                        else if (split_kv(content, "Version", v))        copy_bounded(cache.bios_version, sizeof(cache.bios_version), v);
                        else if (split_kv(content, "Release Date", v))   copy_bounded(cache.bios_date, sizeof(cache.bios_date), v);
                        break;
                    case Section::BaseBoard:
                        if (split_kv(content, "Manufacturer", v))        copy_bounded(cache.board_mfg, sizeof(cache.board_mfg), v);
                        else if (split_kv(content, "Product Name", v))   copy_bounded(cache.board_product, sizeof(cache.board_product), v);
                        else if (split_kv(content, "Version", v))        copy_bounded(cache.board_version, sizeof(cache.board_version), v);
                        else if (split_kv(content, "Serial Number", v))  copy_bounded(cache.board_serial, sizeof(cache.board_serial), v);
                        break;
                    case Section::Chassis:
                        if (split_kv(content, "Type", v))                copy_bounded(cache.chassis_type, sizeof(cache.chassis_type), v);
                        else if (split_kv(content, "Serial Number", v))  copy_bounded(cache.chassis_serial, sizeof(cache.chassis_serial), v);
                        else if (split_kv(content, "Asset Tag", v))      copy_bounded(cache.asset_tag, sizeof(cache.asset_tag), v);
                        break;
                    default: break;
                }
            }

            if (!e) break;
            p = e + 1;
        }

        // Even when dmidecode succeeds, some fields may still be empty
        // (older dmidecode versions, VMs, restricted kernels). Fill any
        // gaps from /sys/class/dmi/id/* — that file set is authoritative
        // and world-readable.
        auto fill_if_empty = [](char* dst, size_t dst_sz, const char* path) {
            if (dst_sz == 0 || dst[0] != '\0') return;
            char tmp[256] = {0};
            if (!read_file(path, tmp, sizeof(tmp))) return;
            if (::strcmp(tmp, "Not Specified") == 0) return;
            if (::strcmp(tmp, "Not Present") == 0) return;
            if (::strcmp(tmp, "To be filled by O.E.M.") == 0) return;
            if (::strcmp(tmp, "Default string") == 0) return;
            copy_bounded(dst, dst_sz, tmp);
        };

        fill_if_empty(cache.manufacturer,   sizeof(cache.manufacturer),   "/sys/class/dmi/id/sys_vendor");
        fill_if_empty(cache.product,        sizeof(cache.product),        "/sys/class/dmi/id/product_name");
        fill_if_empty(cache.version,        sizeof(cache.version),        "/sys/class/dmi/id/product_version");
        fill_if_empty(cache.serial,         sizeof(cache.serial),         "/sys/class/dmi/id/product_serial");
        fill_if_empty(cache.uuid,           sizeof(cache.uuid),           "/sys/class/dmi/id/product_uuid");
        fill_if_empty(cache.sku,            sizeof(cache.sku),            "/sys/class/dmi/id/product_sku");
        fill_if_empty(cache.family,         sizeof(cache.family),         "/sys/class/dmi/id/product_family");
        fill_if_empty(cache.board_mfg,      sizeof(cache.board_mfg),      "/sys/class/dmi/id/board_vendor");
        fill_if_empty(cache.board_product,  sizeof(cache.board_product),  "/sys/class/dmi/id/board_name");
        fill_if_empty(cache.board_version,  sizeof(cache.board_version),  "/sys/class/dmi/id/board_version");
        fill_if_empty(cache.board_serial,   sizeof(cache.board_serial),   "/sys/class/dmi/id/board_serial");
        fill_if_empty(cache.bios_vendor,    sizeof(cache.bios_vendor),    "/sys/class/dmi/id/bios_vendor");
        fill_if_empty(cache.bios_version,   sizeof(cache.bios_version),   "/sys/class/dmi/id/bios_version");
        fill_if_empty(cache.bios_date,      sizeof(cache.bios_date),      "/sys/class/dmi/id/bios_date");
        fill_if_empty(cache.chassis_type,   sizeof(cache.chassis_type),   "/sys/class/dmi/id/chassis_type");
        fill_if_empty(cache.chassis_serial, sizeof(cache.chassis_serial), "/sys/class/dmi/id/chassis_serial");
        fill_if_empty(cache.asset_tag,      sizeof(cache.asset_tag),      "/sys/class/dmi/id/chassis_asset_tag");

        cache.loaded = true;
    });

    return cache;
}

} // namespace detail



inline knst_cpu_info cpu() noexcept {
    knst_cpu_info c;

    char buf[16384];
    if (!detail::read_file("/proc/cpuinfo", buf, sizeof(buf))) return c;


    bool got_flags = false, got_stepping = false, got_microcode = false, got_vendor = false;
    int processor_markers_seen = 0;

    char* line = buf;
    while (*line) {
        char* eol = ::strchr(line, '\n');
        if (eol) *eol = '\0';

        const char* v = nullptr;
        if (detail::split_kv(line, "processor", v)) {
            ++processor_markers_seen;
            if (processor_markers_seen >= 2) break;
        }
        else if (!got_vendor && detail::split_kv(line, "vendor_id", v)) {
            c.vendor = knst_c16string(v);
            got_vendor = true;
        }
        else if (detail::split_kv(line, "model name", v)) {
            if (c.model_name.empty()) c.model_name = knst_c16string(v);
        }
        else if (detail::split_kv(line, "cpu MHz", v)) {
            if (c.current_mhz == 0) c.current_mhz = (uint32_t)::atof(v);
        }
        else if (detail::split_kv(line, "cache size", v)) {
            if (c.cache_l3_kb == 0) { c.cache_l3_kb = (uint32_t)::atoi(v); c.has_cache = true; }
        }
        else if (detail::split_kv(line, "cpu cores", v)) {
            if (c.physical_cores == 0) c.physical_cores = (uint32_t)::atoi(v);
        }
        else if (!got_flags && detail::split_kv(line, "flags", v)) {
            if (::strstr(v, " sse4"))   c.has_sse4 = true;
            if (::strstr(v, " avx"))    c.has_avx = true;
            if (::strstr(v, " avx2"))   c.has_avx2 = true;
            if (::strstr(v, " avx512")) c.has_avx512 = true;
            if (::strstr(v, " aes"))    c.has_aes = true;
            if (::strstr(v, " vmx") || ::strstr(v, " svm")) c.has_vmx = true;
            got_flags = true;
        }
        else if (!got_flags && detail::split_kv(line, "Features", v)) {
            if (::strstr(v, " sse4"))   c.has_sse4 = true;
            if (::strstr(v, " avx"))    c.has_avx = true;
            if (::strstr(v, " avx2"))   c.has_avx2 = true;
            if (::strstr(v, " avx512")) c.has_avx512 = true;
            if (::strstr(v, " aes"))    c.has_aes = true;
            if (::strstr(v, " vmx") || ::strstr(v, " svm")) c.has_vmx = true;
            got_flags = true;
        }
        else if (!got_stepping && detail::split_kv(line, "stepping", v)) {
            c.stepping = (uint32_t)::atoi(v);
            got_stepping = true;
        }
        else if (!got_microcode && detail::split_kv(line, "microcode", v)) {
            c.microcode_ver = (uint32_t)::strtoul(v, nullptr, 16);
            got_microcode = true;
        }

        if (!eol) break;
        line = eol + 1;
    }


    long nproc = ::sysconf(_SC_NPROCESSORS_ONLN);
    c.logical_cores = nproc > 0 ? (uint32_t)nproc: (processor_markers_seen > 0 ? (uint32_t)processor_markers_seen : 1);
                     

    if (c.logical_cores > 0 && c.physical_cores > 0 && c.logical_cores > c.physical_cores)
        c.has_hyperthreading = true;

    c.has_cores = (c.logical_cores > 0);
    c.has_frequency = true;

    {
        uint64_t v = 0;
        if (detail::read_file_u64("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", v))
            c.max_mhz = (uint32_t)(v / 1000);
        else if (detail::read_file_u64("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq", v))
            c.max_mhz = (uint32_t)(v / 1000);
        uint64_t b = 0;
        if (detail::read_file_u64("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency", b))
            c.base_mhz = (uint32_t)(b / 1000);
        uint64_t cur = 0;
        if (detail::read_file_u64("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", cur))
            c.current_mhz = (uint32_t)(cur / 1000);
    }


    {
        DIR* d = ::opendir("/sys/class/hwmon");
        if (d) {
            struct dirent* e;
            while ((e = ::readdir(d)) != nullptr) {
                if (e->d_name[0] == '.') continue;
                char npath[256], nbuf[64];
                ::snprintf(npath, sizeof(npath), "/sys/class/hwmon/%s/name", e->d_name);
                if (!detail::read_file(npath, nbuf, sizeof(nbuf))) continue;
                if (::strcmp(nbuf, "coretemp") != 0
                    && ::strcmp(nbuf, "k10temp") != 0
                    && ::strcmp(nbuf, "zenpower") != 0) continue;

                char tpath[256];
                int64_t t = 0;
                ::snprintf(tpath, sizeof(tpath), "/sys/class/hwmon/%s/temp1_input", e->d_name);
                if (detail::read_file_i64(tpath, t)) {
                    c.temperature_c = (int32_t)(t / 1000);
                    c.has_temperature = true;
                    break;
                }
            }
            ::closedir(d);
        }
    }


    {
        struct stat_t { uint64_t user, nice, sys, idle, iowait, irq, softirq, steal; };
        auto snap = [](stat_t& s) -> bool {
            char sb[512];
            if (!detail::read_file("/proc/stat", sb, sizeof(sb))) return false;
            unsigned long long u,n,sy,i,io,ir,si,st;
            if (::sscanf(sb, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                         &u,&n,&sy,&i,&io,&ir,&si,&st) < 4) return false;
            s.user=u; s.nice=n; s.sys=sy; s.idle=i; s.iowait=io;
            s.irq=ir; s.softirq=si; s.steal=st;
            return true;
        };
                stat_t s1, s2;
        if (snap(s1)) {

            struct timespec ts { 0, 20 * 1000 * 1000 };
            ::nanosleep(&ts, nullptr);
            if (snap(s2)) {
                uint64_t idle1 = s1.idle + s1.iowait;
                uint64_t idle2 = s2.idle + s2.iowait;
                uint64_t tot1 = s1.user+s1.nice+s1.sys+s1.idle+s1.iowait+s1.irq+s1.softirq+s1.steal;
                uint64_t tot2 = s2.user+s2.nice+s2.sys+s2.idle+s2.iowait+s2.irq+s2.softirq+s2.steal;
                uint64_t dt = tot2 - tot1, di = idle2 - idle1;
                if (dt > 0) {
                    c.usage_percent = (uint32_t)(100ULL * (dt - di) / dt);
                    c.has_usage = true;
                }
            }
        }
    }

    {
        char lb[64];
        if (detail::read_file("/proc/loadavg", lb, sizeof(lb))) {
            double a, b, cc;
            if (::sscanf(lb, "%lf %lf %lf", &a, &b, &cc) == 3) {
                c.load_avg_1 = a; c.load_avg_5 = b; c.load_avg_15 = cc;
            }
        }
    }

    {
        struct utsname u;
        if (::uname(&u) == 0) c.architecture = knst_c16string(u.machine);
    }

    if (detail::dir_has_prefix("/sys/class/iommu", "")) c.has_iommu = true;


    {
        bool seen[256] = { false };
        int max_id = -1;
        DIR* cd = ::opendir("/sys/devices/system/cpu");
        if (cd) {
            struct dirent* ce;
            while ((ce = ::readdir(cd)) != nullptr) {
                if (::strncmp(ce->d_name, "cpu", 3) != 0) continue;
                const char* numpart = ce->d_name + 3;
                if (*numpart == '\0') continue;
                bool all_digit = true;
                for (const char* q = numpart; *q; ++q) {
                    if (*q < '0' || *q > '9') { all_digit = false; break; }
                }
                if (!all_digit) continue;

                char ppath[128];
                ::snprintf(ppath, sizeof(ppath),
                          "/sys/devices/system/cpu/%s/topology/physical_package_id",
                          ce->d_name);
                int64_t pkg = -1;
                if (detail::read_file_i64(ppath, pkg) && pkg >= 0 && pkg < 256) {
                    seen[pkg] = true;
                    if ((int)pkg > max_id) max_id = (int)pkg;
                }
            }
            ::closedir(cd);
        }
        int cnt = 0;
        for (int i = 0; i <= max_id && i < 256; ++i) if (seen[i]) cnt++;
        c.socket_count = cnt > 0 ? (uint32_t)cnt : 1;
    }

    c.valid = true;
    return c;
}



inline knst_motherboard_info motherboard() noexcept {
    knst_motherboard_info m;
    const auto& d = detail::dmi_cache();

    auto put = [](knst_c16string& dst, const char* src) {
        if (src && *src && ::strcmp(src, "Not Specified") != 0
            && ::strcmp(src, "Not Present") != 0
            && ::strcmp(src, "To be filled by O.E.M.") != 0)
            dst = knst_c16string(src);
    };

    put(m.manufacturer,   d.board_mfg);
    put(m.product,        d.board_product);
    put(m.version,        d.board_version);
    put(m.serial,         d.board_serial);
    put(m.bios_vendor,    d.bios_vendor);
    put(m.bios_version,   d.bios_version);
    put(m.bios_date,      d.bios_date);
    put(m.chassis_type,   d.chassis_type);
    put(m.chassis_serial, d.chassis_serial);
    put(m.uuid,           d.uuid);

    m.has_board   = !m.product.empty();
    m.has_bios    = !m.bios_version.empty();
    m.has_chassis = !m.chassis_type.empty();

    struct stat st;
    m.uefi_boot = (::stat("/sys/firmware/efi", &st) == 0);

    if (m.uefi_boot) {
        char sb[16];
        if (detail::read_file(
                "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c",
                sb, sizeof(sb))) {
            m.secure_boot = (sb[4] != 0);
        }
    }

    if (detail::dir_has_prefix("/sys/class/tpm", "tpm")) {
        m.has_tpm = true;
        char ver[16];
        if (detail::read_file("/sys/class/tpm/tpm0/tpm_version_major", ver, sizeof(ver)))
            m.tpm_version = knst_c16string(ver);
    }

    m.valid = true;
    return m;
}



inline knst_memory_info memory() noexcept {
    knst_memory_info m;

    char buf[4096];
    if (!detail::read_file("/proc/meminfo", buf, sizeof(buf))) return m;

    // Parses one key from /proc/meminfo. Copies each line into a local
    // buffer so that `buf` is never mutated — otherwise a second call
    // would skip lines whose '\n' was replaced by '\0' in the first call.
    auto parse_kb = [&](const char* key) -> uint64_t {
        char* line = buf;
        while (*line) {
            char* eol = ::strchr(line, '\n');
            size_t len = eol ? (size_t)(eol - line) : ::strlen(line);

            char lbuf[128];
            if (len >= sizeof(lbuf)) len = sizeof(lbuf) - 1;
            ::memcpy(lbuf, line, len);
            lbuf[len] = '\0';

            const char* v = nullptr;
            if (detail::split_kv(lbuf, key, v)) {
                unsigned long long kb = ::strtoull(v, nullptr, 10);
                return kb * 1024ULL;
            }

            if (!eol) break;
            line = eol + 1;
        }
        return 0;
    
    };

    m.total_bytes     = parse_kb("MemTotal");
    m.free_bytes      = parse_kb("MemFree");
    m.available_bytes = parse_kb("MemAvailable");
    m.cached_bytes    = parse_kb("Cached");
    m.buffers_bytes   = parse_kb("Buffers");
    m.swap_total      = parse_kb("SwapTotal");
    m.swap_free       = parse_kb("SwapFree");
    if (m.swap_total > 0) {
        m.swap_used = m.swap_total - m.swap_free;
        m.has_swap = true;
    }
    m.used_bytes = m.total_bytes > m.available_bytes? m.total_bytes - m.available_bytes: m.total_bytes - m.free_bytes;
                   
                   


    auto dm = detail::tool("dmidecode", { "-t", "memory" }, 3000);
    if (dm) {
        const char* p = dm.c_str();
        knst_memory_module cur;
        bool in_device = false;

        auto commit = [&]() {
            if (cur.size_bytes > 0) {
                cur.valid = true;
                cur.slot_index = (uint32_t)m.modules.size();
                m.modules.push_back(cur);
            }
            cur = knst_memory_module();
        };

        while (*p) {
            const char* eol = ::strchr(p, '\n');
            size_t linelen = eol ? (size_t)(eol - p) : ::strlen(p);
            char line[256];
            if (linelen >= sizeof(line)) linelen = sizeof(line) - 1;
            ::memcpy(line, p, linelen); line[linelen] = '\0';
            detail::trim_right(line);

            const char* v = nullptr;
            if (detail::split_kv(line, "Memory Device", v)) {
                commit();
                in_device = true;
            }
            if (in_device) {
                if (detail::split_kv(line, "Size", v)) {
                    if (!::strstr(v, "No Module")) {
                        char* endp;
                        unsigned long long val = ::strtoull(v, &endp, 10);
                        if (::strstr(v, "GB")) cur.size_bytes = val * 1024ULL * 1024 * 1024;
                        else if (::strstr(v, "MB")) cur.size_bytes = val * 1024ULL * 1024;
                        else if (::strstr(v, "KB")) cur.size_bytes = val * 1024ULL;
                    }
                }
                else if (detail::split_kv(line, "Locator", v) && !::strstr(v, "Bank"))
                    cur.locator = knst_c16string(v);
                else if (detail::split_kv(line, "Speed", v)) {
                    uint32_t sp = (uint32_t)::atoi(v);
                    if (sp > 0) cur.speed_mhz = sp;
                }
                else if (detail::split_kv(line, "Configured Memory Speed", v)) {
                    uint32_t sp = (uint32_t)::atoi(v);
                    if (sp > 0) cur.configured_mhz = sp;
                }
                else if (detail::split_kv(line, "Type", v)
                         && !::strstr(v, "Unknown") && !::strstr(v, "Other"))
                    cur.type = knst_c16string(v);
                else if (detail::split_kv(line, "Form Factor", v) && !::strstr(v, "Unknown"))
                    cur.form_factor = knst_c16string(v);
                else if (detail::split_kv(line, "Manufacturer", v) && !::strstr(v, "Unknown"))
                    cur.manufacturer = knst_c16string(v);
                else if (detail::split_kv(line, "Serial Number", v) && !::strstr(v, "Unknown"))
                    cur.serial = knst_c16string(v);
                else if (detail::split_kv(line, "Part Number", v) && !::strstr(v, "Unknown"))
                    cur.part_number = knst_c16string(v);
                else if (detail::split_kv(line, "Rank", v) && !::strstr(v, "Unknown"))
                    cur.rank = (uint32_t)::atoi(v);
            }
            if (!eol) break;
            p = eol + 1;
        }
        commit();

        if (!m.modules.empty()) {
            m.has_modules = true;
            m.slot_count = (uint32_t)m.modules.size();
            m.slots_used = m.slot_count;
        }
    }

    m.valid = true;
    return m;
}



inline knst_vector<knst_storage_info> storage() noexcept {
    knst_vector<knst_storage_info> out;

    DIR* d = ::opendir("/sys/block");
    if (d) {
        struct dirent* e;
        while ((e = ::readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            if (detail::starts_with(e->d_name, "loop")
                || detail::starts_with(e->d_name, "ram")
                || detail::starts_with(e->d_name, "zram")
                || detail::starts_with(e->d_name, "dm-")
                || detail::starts_with(e->d_name, "sr")) continue;

            knst_storage_info s;
            s.device = knst_c16string("/dev/");
            s.device.append((const char*)e->d_name);

            char path[256], buf[256];
            ::snprintf(path, sizeof(path), "/sys/block/%s/device/model", e->d_name);
            if (detail::read_file(path, buf, sizeof(buf))) s.model = knst_c16string(buf);

            ::snprintf(path, sizeof(path), "/sys/block/%s/device/serial", e->d_name);
            if (detail::read_file(path, buf, sizeof(buf))) s.serial = knst_c16string(buf);

            ::snprintf(path, sizeof(path), "/sys/block/%s/device/vendor", e->d_name);
            if (detail::read_file(path, buf, sizeof(buf))) s.vendor = knst_c16string(buf);

            ::snprintf(path, sizeof(path), "/sys/block/%s/size", e->d_name);
            uint64_t sectors = 0;
            if (detail::read_file_u64(path, sectors)) s.total_bytes = sectors * 512;

            ::snprintf(path, sizeof(path), "/sys/block/%s/queue/rotational", e->d_name);
            int64_t rot = 1;
            if (detail::read_file_i64(path, rot)) {
                s.is_solid_state = (rot == 0);
                s.type = s.is_solid_state ? knst_storage_type::SSD : knst_storage_type::HDD;
                if (s.is_solid_state && detail::starts_with(e->d_name, "nvme"))
                    s.type = knst_storage_type::NVMe;
            }

            ::snprintf(path, sizeof(path), "/sys/block/%s/removable", e->d_name);
            int64_t rm = 0;
            if (detail::read_file_i64(path, rm)) s.is_removable = (rm != 0);

  
            if (s.type == knst_storage_type::NVMe) {
                char p2[256];
                ::snprintf(p2, sizeof(p2), "/sys/block/%s/device/hwmon", e->d_name);
                DIR* dh = ::opendir(p2);
                if (dh) {
                    struct dirent* he;
                    while ((he = ::readdir(dh)) != nullptr) {
                        if (he->d_name[0] == '.') continue;
                        char tpath[400];
                        int64_t t = 0;
                        ::snprintf(tpath, sizeof(tpath), "%s/%s/temp1_input", p2, he->d_name);
                        if (detail::read_file_i64(tpath, t)) {
                            s.temperature_c = (int32_t)(t / 1000);
                            s.has_temperature = true;
                            break;
                        }
                    }
                    ::closedir(dh);
                }
            }

            s.has_model = !s.model.empty();
            s.valid = true;
            out.push_back(s);
        }
        ::closedir(d);
    }


    char mbuf[16384];
    if (detail::read_file("/proc/mounts", mbuf, sizeof(mbuf))) {
        char* line = mbuf;
        while (*line) {
            char* eol = ::strchr(line, '\n');
            if (eol) *eol = '\0';

                        char dev[256] = {0}, mnt[512] = {0}, fs[64] = {0};
            if (::sscanf(line, "%255s %511s %63s", dev, mnt, fs) == 3) {
                knst_c16string devc(dev);

                // Normalise /dev/mapper/NAME and /dev/dm-N to their
                // underlying physical disk. Required for LVM, LUKS and
                // RAID — used by Pop!_OS, Ubuntu, Fedora, etc.
                const char* mapped_name = nullptr;
                if (::strncmp(dev, "/dev/mapper/", 12) == 0) {
                    mapped_name = dev + 12;
                } else if (::strncmp(dev, "/dev/dm-", 8) == 0) {
                    char namepath[256];
                    char namebuf[128] = {0};
                    ::snprintf(namepath, sizeof(namepath),
                               "/sys/block/%s/dm/name", dev + 5);
                    if (detail::read_file(namepath, namebuf, sizeof(namebuf))) {
                        mapped_name = namebuf;
                    }
                }

                auto attach = [&](knst_storage_info& s) {
                    knst_storage_partition pt;
                    pt.device = devc;                 // keep original for display
                    pt.mount_point = knst_c16string(mnt);
                    pt.fs_type = knst_c16string(fs);
                    struct statvfs st;
                    if (::statvfs(mnt, &st) == 0) {
                        pt.total_bytes = (uint64_t)st.f_blocks * st.f_frsize;
                        pt.free_bytes  = (uint64_t)st.f_bfree  * st.f_frsize;
                        pt.used_bytes  = pt.total_bytes - pt.free_bytes;
                    }
                    if (::strcmp(mnt, "/") == 0 || ::strcmp(mnt, "/boot") == 0)
                        pt.is_boot = true;
                    pt.valid = true;
                    s.partitions.push_back(pt);
                    s.has_partitions = true;
                    if (pt.is_boot) s.is_system_disk = true;
                };

                if (mapped_name) {
                    knst_byte_string slave = detail::resolve_dm_slave(mapped_name);
                    if (!slave.empty()) {
                        knst_c16string phys_full = knst_c16string("/dev/");
                        phys_full.append(knst_c16string(
                            reinterpret_cast<const char*>(slave.data()),
                            static_cast<uint32_t>(slave.length())));
                        for (auto& s : out) {
                            if (detail::device_matches_disk(phys_full, s.device)) {
                                attach(s);
                                break;
                            }
                        }
                    }
                } else {
                    for (auto& s : out) {
                        if (detail::device_matches_disk(devc, s.device)) {
                            attach(s);
                        }
                    }
                }
            }
            if (!eol) break;
            line = eol + 1;
        }
    }

        for (auto& s : out) {
        s.used_bytes = 0; s.free_bytes = 0;
        for (auto& pt : s.partitions) {
            s.used_bytes += pt.used_bytes;
            s.free_bytes += pt.free_bytes;
        }
    }

    return out;
}

inline knst_vector<knst_network_info> network() noexcept {
    knst_vector<knst_network_info> out;

    struct ifaddrs* ifs = nullptr;
    if (::getifaddrs(&ifs) == 0) {
        for (struct ifaddrs* i = ifs; i != nullptr; i = i->ifa_next) {
            if (!i->ifa_name) continue;

            knst_network_info* cur = nullptr;
            for (auto& n : out) if (n.name == knst_c16string(i->ifa_name)) { cur = &n; break; }
            if (!cur) {
                knst_network_info ni;
                ni.name = knst_c16string(i->ifa_name);
                out.push_back(ni);
                cur = &out[out.size() - 1];
            }

            if (i->ifa_flags & IFF_LOOPBACK) cur->is_loopback = true;
            if (i->ifa_flags & IFF_UP)       cur->is_up = true;
            if (i->ifa_flags & IFF_RUNNING)  cur->is_running = true;

            if (i->ifa_addr) {
                char ip[64] = {0};
                if (i->ifa_addr->sa_family == AF_INET) {
                    auto* sin = (struct sockaddr_in*)i->ifa_addr;
                    ::inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                    if (cur->ipv4.empty()) cur->ipv4 = knst_c16string(ip);
                    cur->has_ip = true;
                } else if (i->ifa_addr->sa_family == AF_INET6) {
                    auto* sin6 = (struct sockaddr_in6*)i->ifa_addr;
                    ::inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
                    if (cur->ipv6.empty()) cur->ipv6 = knst_c16string(ip);
                    cur->has_ip = true;
                }
            }
        }
        ::freeifaddrs(ifs);
    }

    for (auto& n : out) {
        char path[128], buf[64];
        knst_byte_string name_b(n.name);
        const char* name_c = (const char*)name_b.data();

        ::snprintf(path, sizeof(path), "/sys/class/net/%s/address", name_c);
        if (detail::read_file(path, buf, sizeof(buf))) n.mac = knst_c16string(buf);

        ::snprintf(path, sizeof(path), "/sys/class/net/%s/type", name_c);
        int64_t type = -1;
        if (detail::read_file_i64(path, type)) {
            if (type == 1)         n.type = knst_net_type::Ethernet;
            else if (type == 772)  n.type = knst_net_type::Loopback;
            else if (type == 801)  { n.type = knst_net_type::WiFi; n.is_wireless = true; }
            else if (type == 65534) n.type = knst_net_type::Virtual;
        }

        {
            char wpath[300];
            struct stat wst;
            ::snprintf(wpath, sizeof(wpath), "/sys/class/net/%s/wireless", name_c);
            if (::stat(wpath, &wst) == 0) {
                n.is_wireless = true; n.type = knst_net_type::WiFi;
            } else {
                ::snprintf(wpath, sizeof(wpath), "/sys/class/net/%s/phy80211", name_c);
                if (::stat(wpath, &wst) == 0) {
                    n.is_wireless = true; n.type = knst_net_type::WiFi;
                }
            }
        }

        ::snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", name_c);
        if (detail::read_file(path, buf, sizeof(buf))) {
            n.is_up = (::strcmp(buf, "up") == 0 || ::strcmp(buf, "unknown") == 0);
        }

        ::snprintf(path, sizeof(path), "/sys/class/net/%s/mtu", name_c);
        uint64_t mtu = 0;
        if (detail::read_file_u64(path, mtu)) n.mtu = (uint32_t)mtu;

        ::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes", name_c);
        detail::read_file_u64(path, n.rx_bytes);
        ::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes", name_c);
        detail::read_file_u64(path, n.tx_bytes);
        ::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_packets", name_c);
        detail::read_file_u64(path, n.rx_packets);
        ::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_packets", name_c);
        detail::read_file_u64(path, n.tx_packets);
        ::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_errors", name_c);
        detail::read_file_u64(path, n.rx_errors);
        ::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_errors", name_c);
        detail::read_file_u64(path, n.tx_errors);
        ::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_dropped", name_c);
        detail::read_file_u64(path, n.rx_dropped);
        ::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_dropped", name_c);
        detail::read_file_u64(path, n.tx_dropped);
        n.has_stats = true;

        if (n.is_wireless) {
            auto iw = detail::tool("iw", { "dev", name_c, "link" }, 500);
            if (iw) {
                const char* p = iw.c_str();
                while (*p) {
                    const char* e = ::strchr(p, '\n');
                    if (!e) break;
                    char line[256];
                    size_t l = (size_t)(e - p);
                    if (l >= sizeof(line)) l = sizeof(line) - 1;
                    ::memcpy(line, p, l); line[l] = '\0';
                    const char* v;
                    if (detail::split_kv(line, "SSID", v)) n.ssid = knst_c16string(v);
                    else if (detail::split_kv(line, "freq", v)) {
                        double f = ::atof(v);
                        n.frequency_mhz = (uint32_t)f;
                        n.channel = (uint32_t)((f - 2407) / 5);
                    }
                    else if (detail::split_kv(line, "signal", v)) {
                        n.signal_dbm = (int32_t)::atoi(v);
                    }
                    p = e + 1;
                }
                n.has_wifi = true;
            }
        }

        n.valid = true;
    }

    return out;
}



inline knst_vector<knst_usb_info> usb() noexcept {
    knst_vector<knst_usb_info> out;

    DIR* d = ::opendir("/sys/bus/usb/devices");
    if (!d) return out;

    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        if (!::strchr(e->d_name, '-') && ::strncmp(e->d_name, "usb", 3) != 0) continue;

        char path[256], buf[256];
        knst_usb_info u;

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/busnum", e->d_name);
        uint64_t busnum = 0;
        if (detail::read_file_u64(path, busnum)) u.bus = (uint32_t)busnum;

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/devnum", e->d_name);
        uint64_t devnum = 0;
        if (detail::read_file_u64(path, devnum)) u.device_num = (uint32_t)devnum;
        if (u.device_num == 0) continue;

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idVendor", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) u.vid = (uint16_t)::strtoul(buf, nullptr, 16);

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idProduct", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) u.pid = (uint16_t)::strtoul(buf, nullptr, 16);

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/manufacturer", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) u.manufacturer = knst_c16string(buf);

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/product", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) u.product_name = knst_c16string(buf);

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/serial", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) u.serial = knst_c16string(buf);

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/version", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) u.usb_version = knst_c16string(buf);

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/speed", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) {
            double s = ::atof(buf);
            if (s > 0) u.max_speed_mbps = (uint32_t)s;
        }

        ::snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/bDeviceClass", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) {
            unsigned bc = (unsigned)::strtoul(buf, nullptr, 16);
            switch (bc) {
                case 0x01: u.usb_class = knst_usb_class::Audio; break;
                case 0x02: u.usb_class = knst_usb_class::CDC; break;
                case 0x03: u.usb_class = knst_usb_class::HID; break;
                case 0x07: u.usb_class = knst_usb_class::Printer; break;
                case 0x08: u.usb_class = knst_usb_class::MassStorage; break;
                case 0x09: u.usb_class = knst_usb_class::Hub; u.is_hub = true; break;
                case 0x0E: u.usb_class = knst_usb_class::Video; break;
                case 0xE0: u.usb_class = knst_usb_class::Wireless; break;
                case 0xFF: u.usb_class = knst_usb_class::Vendor; break;
                default: u.usb_class = knst_usb_class::Other; break;
            }
        }

        if (::strncmp(e->d_name, "usb", 3) == 0) u.is_root_hub = true;

        u.valid = true;
        out.push_back(u);
    }
    ::closedir(d);
    return out;
}



inline knst_vector<knst_pci_info> pci() noexcept {
    knst_vector<knst_pci_info> out;

    DIR* d = ::opendir("/sys/bus/pci/devices");
    if (!d) return out;

    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        unsigned dom, bus, slot, func;
        if (::sscanf(e->d_name, "%x:%x:%x.%x", &dom, &bus, &slot, &func) != 4) continue;

        knst_pci_info info;
        info.domain = dom; info.bus = bus; info.slot = slot; info.function = func;

        char path[256], buf[256];
        ::snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/vendor", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf)))
            info.vendor_id = (uint16_t)::strtoul(buf, nullptr, 16);

        ::snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/device", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf)))
            info.device_id = (uint16_t)::strtoul(buf, nullptr, 16);

        ::snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/driver", e->d_name);
        char driver[64];
        if (detail::read_symlink(path, driver, sizeof(driver))) {
            char* base = ::strrchr(driver, '/');
            info.driver = knst_c16string(base ? base + 1 : driver);
        }

        ::snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/iommu_group", e->d_name);
        char iommu[64];
        if (detail::read_symlink(path, iommu, sizeof(iommu))) {
            char* base = ::strrchr(iommu, '/');
            info.iommu_group = ::atoi(base ? base + 1 : iommu);
        }

        ::snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/numa_node", e->d_name);
        int64_t numa = -1;
        if (detail::read_file_i64(path, numa)) info.numa_node = (int32_t)numa;

        ::snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/current_link_speed", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) {
            double gts = ::atof(buf);
            if (gts >= 31.0)      info.pcie_gen = 5;
            else if (gts >= 15.0) info.pcie_gen = 4;
            else if (gts >= 7.0)  info.pcie_gen = 3;
            else if (gts >= 4.5)  info.pcie_gen = 2;
            else if (gts > 0.0)   info.pcie_gen = 1;
        }

        ::snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/current_link_width", e->d_name);
        uint64_t width = 0;
        if (detail::read_file_u64(path, width)) info.pcie_width = (uint32_t)width;

        ::snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/power_state", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) {
            if (::strcmp(buf, "D0") == 0)          info.power_state = knst_device_power_state::D0;
            else if (::strcmp(buf, "D1") == 0)     info.power_state = knst_device_power_state::D1;
            else if (::strcmp(buf, "D2") == 0)     info.power_state = knst_device_power_state::D2;
            else if (::strcmp(buf, "D3hot") == 0)  info.power_state = knst_device_power_state::D3hot;
            else if (::strcmp(buf, "D3cold") == 0) info.power_state = knst_device_power_state::D3cold;
        }

        info.valid = true;
        out.push_back(info);
    }
    ::closedir(d);


    auto r = detail::tool("lspci", { "-mm", "-nn" }, 3000);
    if (r) {
        const char* p = r.c_str();
        while (*p) {
            const char* e2 = ::strchr(p, '\n');
            if (!e2) break;
            char line[512];
            size_t l = (size_t)(e2 - p);
            if (l >= sizeof(line)) l = sizeof(line) - 1;
            ::memcpy(line, p, l); line[l] = '\0';

            unsigned bus, slot, func;
            if (::sscanf(line, "%x:%x.%x", &bus, &slot, &func) == 3) {
                char classbuf[256] = {0}, vendorbuf[256] = {0}, devbuf[256] = {0};
                const char* q = line;
                int idx = 0;
                while ((q = ::strchr(q, '"')) != nullptr && idx < 3) {
                    q++;
                    const char* qe = ::strchr(q, '"');
                    if (!qe) break;
                    size_t fl = (size_t)(qe - q);
                    if (fl > 255) fl = 255;
                    char* dst = (idx == 0) ? classbuf : (idx == 1) ? vendorbuf : devbuf;
                    ::memcpy(dst, q, fl); dst[fl] = '\0';
                    idx++;
                    q = qe + 1;
                }
                for (auto& info : out) {
                    if (info.bus == bus && info.slot == slot && info.function == func) {
                        if (classbuf[0]) info.class_name = knst_c16string(classbuf);
                        if (vendorbuf[0]) info.vendor_name = knst_c16string(vendorbuf);
                        if (devbuf[0]) info.device_name = knst_c16string(devbuf);
                        break;
                    }
                }
            }
            p = e2 + 1;
        }
    }

    return out;
}



namespace detail {

inline char16_t edid_letter(uint8_t five_bits) noexcept {
    if (five_bits < 1 || five_bits > 26) return u'?';
    return static_cast<char16_t>(u'A' + five_bits - 1);
}
} // namespace detail

inline knst_vector<knst_monitor_info> monitors() noexcept {
    knst_vector<knst_monitor_info> out;

    DIR* d = ::opendir("/sys/class/drm");
    if (!d) return out;

    struct dirent* e;
    uint32_t idx = 0;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        const char* dash = ::strchr(e->d_name, '-');
        if (!dash) continue;
        if (::strncmp(e->d_name, "card", 4) != 0) continue;

        char path[256], buf[512];
        ::snprintf(path, sizeof(path), "/sys/class/drm/%s/status", e->d_name);
        if (!detail::read_file(path, buf, sizeof(buf))) continue;
        if (::strcmp(buf, "connected") != 0) continue;

        knst_monitor_info m;
        m.index = idx++;

        const char* conn = dash + 1;
        if (::strncmp(conn, "HDMI", 4) == 0)         m.connection = knst_monitor_conn::HDMI;
        else if (::strncmp(conn, "DP", 2) == 0)      m.connection = knst_monitor_conn::DisplayPort;
        else if (::strncmp(conn, "eDP", 3) == 0)     m.connection = knst_monitor_conn::eDP;
        else if (::strncmp(conn, "LVDS", 4) == 0)    m.connection = knst_monitor_conn::LVDS;
        else if (::strncmp(conn, "DVI", 3) == 0)     m.connection = knst_monitor_conn::DVI;
        else if (::strncmp(conn, "VGA", 3) == 0)     m.connection = knst_monitor_conn::VGA;
        else if (::strncmp(conn, "USB-C", 5) == 0)   m.connection = knst_monitor_conn::USB_C;

        ::snprintf(path, sizeof(path), "/sys/class/drm/%s/edid", e->d_name);
        int fd = ::open(path, O_RDONLY);
        if (fd >= 0) {
            unsigned char edid[256] = {0};
            ssize_t rn = ::read(fd, edid, sizeof(edid));
            ::close(fd);
            if (rn >= 128 && edid[0] == 0x00 && edid[1] == 0xFF
                && edid[2] == 0xFF && edid[3] == 0xFF) {
                uint16_t mid = ((uint16_t)edid[8] << 8) | edid[9];
                char16_t mfg[4] = {0};
                mfg[0] = detail::edid_letter((mid >> 10) & 0x1F);
                mfg[1] = detail::edid_letter((mid >> 5)  & 0x1F);
                mfg[2] = detail::edid_letter(mid & 0x1F);
                m.manufacturer = knst_c16string(mfg);

                m.product_code = (uint32_t)edid[10] | ((uint32_t)edid[11] << 8);
                uint32_t serial = (uint32_t)edid[12] | ((uint32_t)edid[13] << 8)
                                | ((uint32_t)edid[14] << 16) | ((uint32_t)edid[15] << 24);
                char serbuf[16]; ::snprintf(serbuf, sizeof(serbuf), "%u", serial);
                m.serial = knst_c16string(serbuf);

                m.week = edid[16];
                m.year = 1990 + edid[17];
                m.physical_width_mm  = (uint32_t)edid[21] * 10;
                m.physical_height_mm = (uint32_t)edid[22] * 10;


                if (edid[54] != 0) {
                    uint32_t pixel_clock_hz = (((uint32_t)edid[55] << 8) | edid[54]) * 10000u;
                    uint32_t hactive = edid[56] | ((uint32_t)(edid[58] & 0xF0) << 4);
                    uint32_t hblank  = edid[57] | ((uint32_t)(edid[58] & 0x0F) << 8);
                    uint32_t vactive = edid[59] | ((uint32_t)(edid[61] & 0xF0) << 4);
                    uint32_t vblank  = edid[60] | ((uint32_t)(edid[61] & 0x0F) << 8);

                    m.native_width = hactive;
                    m.native_height = vactive;

                    uint32_t htotal = hactive + hblank;
                    uint32_t vtotal = vactive + vblank;
                    if (htotal > 0 && vtotal > 0 && pixel_clock_hz > 0) {
                        m.refresh_hz = pixel_clock_hz / (htotal * vtotal);
                    }
                }

                if (edid[23] != 0xFF) m.gamma = (float)(edid[23] + 100) / 100.0f;
                m.valid = true;
            }
        }

        ::snprintf(path, sizeof(path), "/sys/class/drm/%s/modes", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) {
            char* p = buf;
            while (*p && *p != '\n') ++p;
            *p = '\0';
            unsigned w, h;
            if (::sscanf(buf, "%ux%u", &w, &h) == 2) {
                m.current_width = w;
                m.current_height = h;
                if (m.native_width == 0) { m.native_width = w; m.native_height = h; }
            }
        }

        out.push_back(m);
    }
    ::closedir(d);

    for (auto& m : out) {
        if (m.physical_width_mm > 0 && m.physical_height_mm > 0) {
            double w_inch = m.physical_width_mm / 25.4;
            double h_inch = m.physical_height_mm / 25.4;
            m.diagonal_inch = (float)::sqrt(w_inch * w_inch + h_inch * h_inch);
        }
    }


    {
        DIR* bd = ::opendir("/sys/class/backlight");
        if (bd) {
            struct dirent* be;
            char bl_name[128] = {0};
            bool found_one = false;
            while ((be = ::readdir(bd)) != nullptr) {
                if (be->d_name[0] == '.') continue;
                ::snprintf(bl_name, sizeof(bl_name), "%s", be->d_name);
                found_one = true;
                break;
            }
            ::closedir(bd);
            if (found_one) {
                char bpath[256];
                uint64_t brightness = 0, max_brightness = 1;
                ::snprintf(bpath, sizeof(bpath), "/sys/class/backlight/%s/brightness", bl_name);
                detail::read_file_u64(bpath, brightness);
                ::snprintf(bpath, sizeof(bpath), "/sys/class/backlight/%s/max_brightness", bl_name);
                detail::read_file_u64(bpath, max_brightness);
                if (max_brightness == 0) max_brightness = 1;
                uint32_t percent = (uint32_t)(100ULL * brightness / max_brightness);
                for (auto& m : out) {
                    if (m.connection == knst_monitor_conn::eDP
                        || m.connection == knst_monitor_conn::LVDS) {
                        m.brightness = percent;
                    }
                }
            }
        }
    }

    return out;
}


inline knst_vector<knst_input_info> input() noexcept {
    knst_vector<knst_input_info> out;

    DIR* d = ::opendir("/sys/class/input");
    if (!d) return out;

    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (::strncmp(e->d_name, "event", 5) != 0) continue;

        knst_input_info i;
        i.device_path = knst_c16string("/dev/input/");
        i.device_path.append((const char*)e->d_name);

        char path[256], buf[256];
        ::snprintf(path, sizeof(path), "/sys/class/input/%s/device/name", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) i.name = knst_c16string(buf);

        ::snprintf(path, sizeof(path), "/sys/class/input/%s/device/id/vendor", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf)))
            i.vid = (uint16_t)::strtoul(buf, nullptr, 16);

        ::snprintf(path, sizeof(path), "/sys/class/input/%s/device/id/product", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf)))
            i.pid = (uint16_t)::strtoul(buf, nullptr, 16);

        ::snprintf(path, sizeof(path), "/sys/class/input/%s/device/capabilities/key", e->d_name);
        bool has_keys = detail::read_file(path, buf, sizeof(buf));

        ::snprintf(path, sizeof(path), "/sys/class/input/%s/device/capabilities/rel", e->d_name);
        bool has_rel = detail::read_file(path, buf, sizeof(buf));

                ::snprintf(path, sizeof(path), "/sys/class/input/%s/device/capabilities/abs", e->d_name);
        bool has_abs = detail::read_file(path, buf, sizeof(buf));
        int abs_bit_count = 0;
        if (has_abs) {

            for (const char* p = buf; *p; ++p) {
                char c = *p;
                int v;
                if (c >= '0' && c <= '9')      v = c - '0';
                else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                else continue;
                while (v) { abs_bit_count += (v & 1); v >>= 1; }
            }
        }


        bool classified_by_name = false;

        if (!i.name.empty()) {
            knst_byte_string name_b(i.name);
            const char* name_c = (const char*)name_b.data();

            if (name_c) {

                if (detail::contains_ci(name_c, "wmi hotkeys")
                 || detail::contains_ci(name_c, "hotkey")
                 || detail::contains_ci(name_c, "hotkeys")
                 || detail::contains_ci(name_c, "acpi")) {
                    i.type = knst_input_type::Other;
                    classified_by_name = true;
                }

                else if (detail::contains_ci(name_c, "touchscreen")
                      || detail::contains_ci(name_c, "touch screen")) {
                    i.type = knst_input_type::Touchscreen;
                    classified_by_name = true;
                }
                else if (detail::contains_ci(name_c, "touchpad")
                      || detail::contains_ci(name_c, "trackpad")
                      || detail::contains_ci(name_c, "synaptics")
                      || detail::contains_ci(name_c, "elan")) {
                    i.type = knst_input_type::Touchpad;
                    classified_by_name = true;
                }

                else if (detail::contains_ci(name_c, "gamepad")
                      || detail::contains_ci(name_c, "game pad")
                      || detail::contains_ci(name_c, "joystick")
                      || detail::contains_ci(name_c, "controller")
                      || detail::contains_ci(name_c, "xbox")
                      || detail::contains_ci(name_c, "playstation")
                      || detail::contains_ci(name_c, "dualshock")
                      || detail::contains_ci(name_c, "dualsense")
                      || detail::contains_ci(name_c, "8bitdo")) {
                    i.type = knst_input_type::Gamepad;
                    classified_by_name = true;
                }

                else if (detail::contains_ci(name_c, "mouse")
                      || detail::contains_ci(name_c, "trackball")
                      || detail::contains_ci(name_c, "trackpoint")) {
                    i.type = knst_input_type::Mouse;
                    classified_by_name = true;
                }

                else if (detail::contains_ci(name_c, "keyboard")
                      || detail::contains_ci(name_c, "keys")) {
                    i.type = knst_input_type::Keyboard;
                    classified_by_name = true;
                }
                // yea my keybloard is logitech and my mouse :)
                else if (detail::contains_ci(name_c, "logitech k")
                      || detail::contains_ci(name_c, "logitech k5")
                      || detail::contains_ci(name_c, "logitech k3")
                      || detail::contains_ci(name_c, "logitech k4")) {
                    i.type = knst_input_type::Keyboard;
                    classified_by_name = true;
                }

                else if (detail::contains_ci(name_c, "logitech m")
                      || detail::contains_ci(name_c, "logitech mx")) {
                    i.type = knst_input_type::Mouse;
                    classified_by_name = true;
                }
            }
        }

                if (!classified_by_name) {
            if (has_keys && !has_rel && !has_abs) {
                i.type = knst_input_type::Keyboard;
            }
            else if (has_keys && has_abs && abs_bit_count <= 2) {
                i.type = knst_input_type::Keyboard;
            }
            else if (has_rel && !has_abs) {
                i.type = knst_input_type::Mouse;
            }
            else if (has_abs && !has_rel && abs_bit_count <= 4) {
                i.type = knst_input_type::Touchpad;
            }
            else if (has_keys && has_abs && abs_bit_count > 4) {
                i.type = knst_input_type::Gamepad;
            }
            else if (has_abs && !has_rel) {
                i.type = knst_input_type::Touchpad;
            }
            else if (has_abs) {
                i.type = knst_input_type::Gamepad;
            }
            else {
                i.type = knst_input_type::Other;
            }
        }

     
        if (!i.name.empty()) {
            knst_byte_string name_b(i.name);
            const char* name_c = (const char*)name_b.data();
            if (name_c && (detail::contains_ci(name_c, "hd-audio")
                        || detail::contains_ci(name_c, "hda nvidia")
                        || detail::contains_ci(name_c, "rear mic")
                        || detail::contains_ci(name_c, "front mic")
                        || detail::contains_ci(name_c, "front headphone")
                        || detail::contains_ci(name_c, "rear headphone")
                        || detail::contains_ci(name_c, "line out")
                        || detail::contains_ci(name_c, "line in")
                        || detail::contains_ci(name_c, "hdmi/dp")
                        || detail::contains_ci(name_c, "hdmi,dp"))) {
                continue;
            }
        }

       
        if (!i.name.empty()) {
            knst_byte_string name_b(i.name);
            const char* name_c = (const char*)name_b.data();
            if (name_c && (detail::contains_ci(name_c, "power button")
                        || detail::contains_ci(name_c, "sleep button")
                        || detail::contains_ci(name_c, "video bus")
                        || detail::contains_ci(name_c, "lid switch")
                        || detail::contains_ci(name_c, "pc speaker"))) {
                continue; 
            }
        }

       
        if (i.type == knst_input_type::Touchpad && !i.name.empty()) {
            knst_byte_string name_b(i.name);
            const char* name_c = (const char*)name_b.data();
            if (name_c && (detail::contains_ci(name_c, "touchscreen")
                          || detail::contains_ci(name_c, "touch screen"))) {
                i.type = knst_input_type::Touchscreen;
            }
        }

        i.valid = true;
        out.push_back(i);
    }
    ::closedir(d);

    return out;
}



inline knst_audio_info audio() noexcept {
    knst_audio_info a;

    // Fast path: no PulseAudio socket → nothing to query.
    {
        char pulse_sock[128];
        ::snprintf(pulse_sock, sizeof(pulse_sock),
                   "/run/user/%u/pulse/native", (unsigned)::getuid());
        struct stat st;
        if (::stat(pulse_sock, &st) != 0) { a.valid = true; return a; }
    }

    auto parse = [&](const char* kind, bool is_input) {
        auto r = detail::tool("pactl", { "list", "short", kind }, 2000);
        if (!r) return;
        const char* p = r.c_str();
        while (*p) {
            const char* e = ::strchr(p, '\n');
            if (!e) break;
            char line[512];
            size_t l = (size_t)(e - p);
            if (l >= sizeof(line)) l = sizeof(line) - 1;
            ::memcpy(line, p, l); line[l] = '\0';

            unsigned idx;
            char name[256] = {0};
            if (::sscanf(line, "%u\t%255s", &idx, name) == 2) {
                knst_audio_device dv;
                dv.index = idx;
                dv.name = knst_c16string(name);
                dv.is_input = is_input;
                dv.is_output = !is_input;
                dv.valid = true;
                a.devices.push_back(dv);
            }
            p = e + 1;
        }
    };

    parse("sinks", false);
    parse("sources", true);

    auto ds = detail::tool("pactl", { "get-default-sink" }, 1000);
    if (ds) a.default_sink = knst_c16string(ds.c_str());
    auto dsrc = detail::tool("pactl", { "get-default-source" }, 1000);
    if (dsrc) a.default_source = knst_c16string(dsrc.c_str());

    a.valid = true;
    return a;
}



inline knst_power_info power() noexcept {
    knst_power_info p;

    DIR* d = ::opendir("/sys/class/power_supply");
    if (!d) return p;

    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;

        char path[256], buf[64];
        knst_byte_string name_b(e->d_name);
        const char* name_c = (const char*)name_b.data();

        ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", name_c);
        if (!detail::read_file(path, buf, sizeof(buf))) continue;

        if (::strcmp(buf, "Battery") == 0) {
            knst_battery_info b;
            b.name = knst_c16string(e->d_name);
            p.has_battery = true;

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", name_c);
            uint64_t cap = 0;
            if (detail::read_file_u64(path, cap)) b.percent = (uint32_t)cap;

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", name_c);
            if (detail::read_file(path, buf, sizeof(buf))) {
                if (::strcmp(buf, "Charging") == 0)          b.state = knst_battery_state::Charging;
                else if (::strcmp(buf, "Discharging") == 0)  b.state = knst_battery_state::Discharging;
                else if (::strcmp(buf, "Full") == 0)         b.state = knst_battery_state::Full;
                else if (::strcmp(buf, "Not charging") == 0) b.state = knst_battery_state::NotCharging;
            }

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/voltage_now", name_c);
            uint64_t v = 0;
            if (detail::read_file_u64(path, v)) b.voltage_mv = (uint32_t)(v / 1000);

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/current_now", name_c);
            int64_t c = 0;
            if (detail::read_file_i64(path, c)) b.current_ma = (int32_t)(c / 1000);

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/power_now", name_c);
            uint64_t pw = 0;
            if (detail::read_file_u64(path, pw)) b.power_now_mw = (uint32_t)(pw / 1000);

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/charge_full", name_c);
            uint64_t cf = 0;
            if (detail::read_file_u64(path, cf)) b.capacity_full_mah = (uint32_t)(cf / 1000);

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/charge_full_design", name_c);
            uint64_t cfd = 0;
            if (detail::read_file_u64(path, cfd)) {
                b.capacity_design_mah = (uint32_t)(cfd / 1000);
                if (b.capacity_design_mah > 0 && b.capacity_full_mah > 0)
                    b.health_percent = (uint32_t)(100ULL * b.capacity_full_mah / b.capacity_design_mah);
            }

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/cycle_count", name_c);
            uint64_t cyc = 0;
            if (detail::read_file_u64(path, cyc)) b.cycle_count = (uint32_t)cyc;

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/manufacturer", name_c);
            if (detail::read_file(path, buf, sizeof(buf))) b.manufacturer = knst_c16string(buf);

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/model_name", name_c);
            if (detail::read_file(path, buf, sizeof(buf))) b.model = knst_c16string(buf);

            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/technology", name_c);
            if (detail::read_file(path, buf, sizeof(buf))) b.technology = knst_c16string(buf);

            b.valid = true;
            p.batteries.push_back(b);
        }
        else if (::strcmp(buf, "Mains") == 0 || ::strcmp(buf, "USB") == 0) {
            ::snprintf(path, sizeof(path), "/sys/class/power_supply/%s/online", name_c);
            uint64_t on = 0;
            if (detail::read_file_u64(path, on)) {
                if (on) p.ac_connected = true;
                for (auto& b : p.batteries) b.ac_online = (on != 0);
            }
        }
    }
    ::closedir(d);

    p.valid = true;
    return p;
}



inline knst_vector<knst_sensor_info> sensors() noexcept {
    knst_vector<knst_sensor_info> out;

    DIR* d = ::opendir("/sys/class/hwmon");
    if (!d) return out;

    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;

        char hwmon_dir[256], chip[64];
        ::snprintf(hwmon_dir, sizeof(hwmon_dir), "/sys/class/hwmon/%s", e->d_name);
        char npath[400];
        ::snprintf(npath, sizeof(npath), "%s/name", hwmon_dir);
        if (!detail::read_file(npath, chip, sizeof(chip))) continue;

        DIR* dd = ::opendir(hwmon_dir);
        if (!dd) continue;
        struct dirent* f;
        while ((f = ::readdir(dd)) != nullptr) {
            const char* nm = f->d_name;

            knst_sensor_type st = knst_sensor_type::Unknown;
            const char* unit = "";
            const char* sub = nullptr;

            if (::strncmp(nm, "temp", 4) == 0)      { st = knst_sensor_type::Temperature; unit = "°C"; sub = "_input"; }
            else if (::strncmp(nm, "fan", 3) == 0)  { st = knst_sensor_type::Fan;         unit = "RPM"; sub = "_input"; }
            else if (::strncmp(nm, "in", 2) == 0)   { st = knst_sensor_type::Voltage;     unit = "V";  sub = "_input"; }
            else if (::strncmp(nm, "curr", 4) == 0) { st = knst_sensor_type::Current;     unit = "A";  sub = "_input"; }
            else if (::strncmp(nm, "power", 5) == 0){ st = knst_sensor_type::Power;       unit = "W";  sub = "_input"; }
            else continue;

            const char* sfx = ::strstr(nm, sub);
            if (!sfx) continue;
            if (sfx[::strlen(sub)] != '\0') continue;

            char fpath[512];
            ::snprintf(fpath, sizeof(fpath), "%s/%s", hwmon_dir, nm);
            int64_t raw = 0;
            if (!detail::read_file_i64(fpath, raw)) continue;

            knst_sensor_info s;
            s.chip = knst_c16string(chip);
            s.type = st;
            s.unit = knst_c16string(unit);

                        if (st == knst_sensor_type::Temperature) s.value = static_cast<double>(raw) / 1000.0;
            else if (st == knst_sensor_type::Voltage) s.value = static_cast<double>(raw) / 1000.0;
            else if (st == knst_sensor_type::Current) s.value = static_cast<double>(raw) / 1000.0;
            else if (st == knst_sensor_type::Power)   s.value = static_cast<double>(raw) / 1000000.0;
            else if (st == knst_sensor_type::Fan)     s.value = (double)raw;

            char label_base[64] = {0};
            size_t base_len = (size_t)(sfx - nm);
            if (base_len < sizeof(label_base)) {
                ::memcpy(label_base, nm, base_len);
                char label_path[512];
                ::snprintf(label_path, sizeof(label_path), "%s/%s_label", hwmon_dir, label_base);
                char lbl[128];
                if (detail::read_file(label_path, lbl, sizeof(lbl))) s.name = knst_c16string(lbl);
                else s.name = knst_c16string(label_base);
            }

            s.valid = true;
            out.push_back(s);
        }
        ::closedir(dd);
    }
    ::closedir(d);

    return out;
}



inline knst_bluetooth_info bluetooth() noexcept {
    knst_bluetooth_info b;

    // Fast path: no Bluetooth adapter at all → skip the slow tool calls.
    // /sys/class/bluetooth has one entry per adapter (hci0, hci1, ...).
    // If the directory is missing or empty, bluetoothctl would just
    // wait for a D-Bus reply that never comes.
    {
        DIR* bd = ::opendir("/sys/class/bluetooth");
        if (!bd) { b.valid = true; return b; }
        struct dirent* be;
        bool has_adapter = false;
        while ((be = ::readdir(bd)) != nullptr) {
            if (be->d_name[0] == '.') continue;
            has_adapter = true;
            break;
        }
        ::closedir(bd);
        if (!has_adapter) { b.valid = true; return b; }
    }

    auto r = detail::tool("bluetoothctl", { "show" }, 1500);
    if (r) {
        const char* p = r.c_str();
        while (*p) {
            const char* e = ::strchr(p, '\n');
            if (!e) break;
            char line[256];
            size_t l = (size_t)(e - p);
            if (l >= sizeof(line)) l = sizeof(line) - 1;
            ::memcpy(line, p, l); line[l] = '\0';

            const char* v;
            if (detail::split_kv(line, "Controller", v)) {
                b.has_adapter = true;
                char addr[64] = {0};
                if (::sscanf(v, "%63s", addr) == 1) b.adapter_address = knst_c16string(addr);
            }
            else if (detail::split_kv(line, "Name", v))          b.adapter_name = knst_c16string(v);
            else if (detail::split_kv(line, "Powered", v))       b.powered = (::strcmp(v, "yes") == 0);
            else if (detail::split_kv(line, "Discoverable", v))  b.discoverable = (::strcmp(v, "yes") == 0);

            p = e + 1;
        }
    }

    auto devs = detail::tool("bluetoothctl", { "devices", "Connected" }, 1500);
    if (devs) {
        const char* q = devs.c_str();
        while (*q) {
            const char* e = ::strchr(q, '\n');
            if (!e) break;
            char line[256];
            size_t l = (size_t)(e - q);
            if (l >= sizeof(line)) l = sizeof(line) - 1;
            ::memcpy(line, q, l); line[l] = '\0';

            const char* v;
            if (detail::split_kv(line, "Device", v)) {
                char addr[64] = {0};
                if (::sscanf(v, "%63s", addr) == 1) {
                    knst_bluetooth_device dv;
                    dv.address = knst_c16string(addr);
                    const char* nm = ::strchr(v, ' ');
                    if (nm) dv.name = knst_c16string(nm + 1);
                    dv.connected = true;
                    dv.valid = true;
                    b.devices.push_back(dv);
                }
            }
            q = e + 1;
        }
    }

    b.valid = true;
    return b;
}



inline knst_vector<knst_camera_info> cameras() noexcept {
    knst_vector<knst_camera_info> out;

    DIR* d = ::opendir("/sys/class/video4linux");
    if (!d) return out;

    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;

        knst_camera_info c;
        c.device_path = knst_c16string("/dev/");
        c.device_path.append((const char*)e->d_name);

        char path[256], buf[256];
        ::snprintf(path, sizeof(path), "/sys/class/video4linux/%s/name", e->d_name);
        if (detail::read_file(path, buf, sizeof(buf))) c.name = knst_c16string(buf);

        c.valid = true;
        out.push_back(c);
    }
    ::closedir(d);
    return out;
}



inline knst_vector<knst_printer_info> printers() noexcept {
    knst_vector<knst_printer_info> out;

    // Fast path: no CUPS socket → nothing to query.
    {
        struct stat st;
        if (::stat("/run/cups/cups.sock", &st) != 0 &&
            ::stat("/var/run/cups/cups.sock", &st) != 0) {
            return out;
        }
    }

    auto r = detail::tool("lpstat", { "-p", "-d" }, 2000);
    if (!r) return out;

    const char* p = r.c_str();
    while (*p) {
        const char* e = ::strchr(p, '\n');
        if (!e) break;
        char line[512];
        size_t l = (size_t)(e - p);
        if (l >= sizeof(line)) l = sizeof(line) - 1;
        ::memcpy(line, p, l); line[l] = '\0';

        if (::strncmp(line, "printer ", 8) == 0) {
            knst_printer_info pr;
            char name[128] = {0};
            if (::sscanf(line + 8, "%127s", name) == 1) {
                pr.name = knst_c16string(name);
                if (::strstr(line, "is idle")) pr.status = u"idle";
                else if (::strstr(line, "now printing")) pr.status = u"printing";
                else if (::strstr(line, "disabled")) pr.status = u"disabled";
                else pr.status = u"unknown";
                pr.valid = true;
                out.push_back(pr);
            }
        }
        else if (::strncmp(line, "system default destination:", 28) == 0) {
            const char* defname = line + 28;
            while (*defname == ' ') ++defname;
            for (auto& pr : out) {
                if (pr.name == knst_c16string(defname)) pr.is_default = true;
            }
        }
        p = e + 1;
    }

    return out;
}



inline knst_system_info system() noexcept {
    knst_system_info s;
    const auto& d = detail::dmi_cache();

    auto put = [](knst_c16string& dst, const char* src) {
        if (src && *src && ::strcmp(src, "Not Specified") != 0
            && ::strcmp(src, "Not Present") != 0
            && ::strcmp(src, "To be filled by O.E.M.") != 0)
            dst = knst_c16string(src);
    };

        put(s.manufacturer,   d.manufacturer);
    put(s.product_name,   d.product);
    put(s.version,        d.version);
    put(s.serial,         d.serial);
    put(s.uuid,           d.uuid);
    put(s.sku,            d.sku);
    put(s.family,         d.family);
    put(s.chassis_type,   d.chassis_type);
    put(s.chassis_serial, d.chassis_serial);
    put(s.asset_tag,      d.asset_tag);

    // DMI chassis_type is numeric on the sysfs fallback path. Map it to
    // readable text so both sources (dmidecode / sysfs) produce the same
    // string. If dmidecode already returned text, the first char won't
    // be a digit and this block is skipped.
    if (!s.chassis_type.empty() && s.chassis_type[0] >= u'0' && s.chassis_type[0] <= u'9') {
        int code = 0;
        for (uint32_t i = 0; i < s.chassis_type.length(); ++i) {
            char16_t c = s.chassis_type[i];
            if (c < u'0' || c > u'9') break;
            code = code * 10 + (c - u'0');
        }
        switch (code) {
            case 1:  s.chassis_type = u"Other";                break;
            case 2:  s.chassis_type = u"Unknown";              break;
            case 3:  s.chassis_type = u"Desktop";              break;
            case 4:  s.chassis_type = u"Low Profile Desktop";  break;
            case 5:  s.chassis_type = u"Pizza Box";            break;
            case 6:  s.chassis_type = u"Mini Tower";           break;
            case 7:  s.chassis_type = u"Tower";                break;
            case 8:  s.chassis_type = u"Portable";             break;
            case 9:  s.chassis_type = u"Laptop";               break;
            case 10: s.chassis_type = u"Notebook";             break;
            case 11: s.chassis_type = u"Hand Held";            break;
            case 12: s.chassis_type = u"Docking Station";      break;
            case 13: s.chassis_type = u"All In One";           break;
            case 14: s.chassis_type = u"Sub Notebook";         break;
            case 15: s.chassis_type = u"Space-saving";         break;
            case 16: s.chassis_type = u"Lunch Box";            break;
            case 17: s.chassis_type = u"Main Server Chassis";  break;
            case 18: s.chassis_type = u"Expansion Chassis";    break;
            case 19: s.chassis_type = u"Sub Chassis";          break;
            case 20: s.chassis_type = u"Bus Expansion Chassis";break;
            case 21: s.chassis_type = u"Peripheral Chassis";   break;
            case 22: s.chassis_type = u"RAID Chassis";         break;
            case 23: s.chassis_type = u"Rack Mount Chassis";   break;
            case 24: s.chassis_type = u"Sealed-case PC";       break;
            default: break;   // leave the raw code
        }
    }

       if (s.manufacturer == knst_c16string("QEMU")
        || s.manufacturer == knst_c16string("VMware, Inc.")
        || s.manufacturer == knst_c16string("innotek GmbH")
        || s.manufacturer == knst_c16string("Microsoft Corporation")
        || s.product_name.contains(u"VirtualBox")
        || s.product_name.contains(u"VMware"))
        s.is_virtual_machine = true;

    if (s.chassis_type == u"Portable"
        || s.chassis_type == u"Laptop"
        || s.chassis_type == u"Notebook"
        || s.chassis_type == u"Sub Notebook"
        || s.chassis_type == u"Hand Held"
        || s.chassis_type == u"Convertible") {
        s.is_laptop = true;
    }

    s.valid = true;
    return s;
}



inline knst_os_info os() noexcept {
    knst_os_info o;

    char buf[1024];
    if (detail::read_file("/etc/os-release", buf, sizeof(buf))) {
        char* line = buf;
        while (*line) {
            char* e = ::strchr(line, '\n');
            if (e) *e = '\0';

            const char* v;
            if (detail::split_kv(line, "NAME", v)) {
                if (*v == '"') v++;
                o.os_name = knst_c16string(v);
                if (o.os_name.length() > 0 && o.os_name[o.os_name.length() - 1] == u'"')
                    o.os_name.resize(o.os_name.length() - 1);
            }
            else if (detail::split_kv(line, "VERSION_ID", v)) {
                if (*v == '"') v++;
                o.os_version = knst_c16string(v);
                if (o.os_version.length() > 0 && o.os_version[o.os_version.length() - 1] == u'"')
                    o.os_version.resize(o.os_version.length() - 1);
            }
            else if (detail::split_kv(line, "ID", v)) {
                if (*v == '"') v++;
                o.os_id = knst_c16string(v);
                if (o.os_id.length() > 0 && o.os_id[o.os_id.length() - 1] == u'"')
                    o.os_id.resize(o.os_id.length() - 1);
            }

            if (!e) break;
            line = e + 1;
        }
    }

    struct utsname u;
    if (::uname(&u) == 0) {
        o.kernel_name = knst_c16string(u.sysname);
        o.kernel_version = knst_c16string(u.release);
        o.kernel_arch = knst_c16string(u.machine);
        o.hostname = knst_c16string(u.nodename);
    }

    char ub[64];
    if (detail::read_file("/proc/uptime", ub, sizeof(ub))) {
        double up = ::atof(ub);
        o.uptime_seconds = (uint64_t)up;
    }

    {
        struct timespec ts;
        if (::clock_gettime(CLOCK_REALTIME, &ts) == 0) {
            uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
            o.boot_time_ms = now_ms > o.uptime_seconds * 1000ULL
                             ? now_ms - o.uptime_seconds * 1000ULL : 0;
        }
    }

    long ps = ::sysconf(_SC_PAGESIZE);
    o.page_size = ps > 0 ? (uint32_t)ps : 4096;

    struct passwd* pw = ::getpwuid(::getuid());
    if (pw) {
        o.user = knst_c16string(pw->pw_name);
        o.home_dir = knst_c16string(pw->pw_dir);
    }
    const char* sh = ::getenv("SHELL");
    if (sh) o.shell = knst_c16string(sh);

    if (detail::read_file("/etc/timezone", buf, sizeof(buf))) o.timezone = knst_c16string(buf);
    else {
        const char* tz = ::getenv("TZ");
        if (tz) o.timezone = knst_c16string(tz);
    }

    if (detail::read_file("/proc/sys/kernel/random/boot_id", buf, sizeof(buf))) {
        uint64_t h = 0;
        for (char* p = buf; *p && *p != '\n'; ++p) h = h * 131 + (unsigned char)*p;
        o.boot_id = h;
    }

    o.valid = true;
    return o;
}



inline knst_vector<knst_driver_info> drivers() noexcept {
    knst_vector<knst_driver_info> out;

    char buf[32768];
    if (!detail::read_file("/proc/modules", buf, sizeof(buf))) return out;

    char* line = buf;
    while (*line) {
        char* e = ::strchr(line, '\n');
        if (e) *e = '\0';

        char name[128] = {0};
        unsigned long size = 0;
        unsigned used_by_count = 0;
        char used_by_str[256] = {0};
        char state[32] = {0};

        if (::sscanf(line, "%127s %lu %u %255s %31s",
                     name, &size, &used_by_count, used_by_str, state) >= 3) {
            knst_driver_info d;
            d.name = knst_c16string(name);
            d.size_bytes = size;
            d.used_by_count = used_by_count;

            if (::strcmp(used_by_str, "-") != 0) {
                char* tok = used_by_str;
                while (*tok) {
                    char* comma = ::strchr(tok, ',');
                    if (comma) *comma = '\0';
                    d.used_by.push_back(knst_c16string(tok));
                    if (!comma) break;
                    tok = comma + 1;
                }
            }

            d.valid = true;
            out.push_back(d);
        }

        if (!e) break;
        line = e + 1;
    }

    return out;
}

} // namespace knst_devices

#endif // !KNST_USING_PLATFORM_WINDOWS



#if KNST_USING_PLATFORM_WINDOWS

namespace knst_devices {
namespace detail {


inline knst_byte_string wmi_normalize_encoding(const knst_byte_string& raw) noexcept {
    const unsigned char* p = (const unsigned char*)raw.data();
    uint32_t n = raw.length();
    if (n < 2) return raw;

    bool is_utf16le = false;
    uint32_t offset = 0;

    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        is_utf16le = true; offset = 2;
    } else if (n >= 2 && p[0] != 0x00 && p[1] == 0x00) {
        is_utf16le = true;
    }
    if (!is_utf16le) return raw;

    knst_byte_string utf8;
    utf8.reserve(n / 2);
    for (uint32_t i = offset; i + 1 < n; i += 2) {
        uint16_t cp = (uint16_t)p[i] | ((uint16_t)p[i + 1] << 8);
        if (cp == 0) continue;
        if (cp < 0x80) {
            utf8.push_back((unsigned char)cp);
        } else if (cp < 0x800) {
            utf8.push_back((unsigned char)(0xC0 | (cp >> 6)));
            utf8.push_back((unsigned char)(0x80 | (cp & 0x3F)));
        } else {
            utf8.push_back((unsigned char)(0xE0 | (cp >> 12)));
            utf8.push_back((unsigned char)(0x80 | ((cp >> 6) & 0x3F)));
            utf8.push_back((unsigned char)(0x80 | (cp & 0x3F)));
        }
    }
    return utf8;
}



inline knst_byte_string wmi_query(const char* cls,
                                   const char* props,
                                   const char* where_filter = nullptr,
                                   uint32_t timeout_ms = 5000) noexcept {
    knst_c16string cmd = u"$OutputEncoding=[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; ";
    cmd.append(u"Get-CimInstance -ClassName ");
    cmd.append(cls);
    if (where_filter && *where_filter) {
        cmd.append(u" -Filter \"");
        cmd.append(knst_c16string(where_filter));
        cmd.append(u"\"");
    }
    cmd.append(u" | Select-Object ");
    cmd.append(props);
    cmd.append(u" | ConvertTo-Csv -NoTypeInformation");
        auto r = knst_process::run_capture(
        u"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
        { u"-NoProfile", u"-NonInteractive", u"-Command", cmd },
        {}, nullptr, 0, timeout_ms);



    if (r.error != knst_process_error::None || r.exit_code != 0) return knst_byte_string();
    return wmi_normalize_encoding(r.out_data);
}
struct csv_row {
    char cols[32][512];
    int count = 0;
};

inline csv_row parse_csv(const char* line) noexcept {
    csv_row r;
    const char* p = line;
    while (*p && r.count < 32) {
        while (*p == ' ') ++p;
        bool in_q = false;
        if (*p == '"') { in_q = true; ++p; }
        size_t i = 0;
        while (*p && i < sizeof(r.cols[0]) - 1) {
            if (in_q) {
                if (*p == '"') {
                    if (p[1] == '"') { r.cols[r.count][i++] = '"'; p += 2; continue; }
                    in_q = false; ++p; break;
                }
                r.cols[r.count][i++] = *p++;
            } else {
                if (*p == ',') break;
                r.cols[r.count][i++] = *p++;
            }
        }
        r.cols[r.count][i] = '\0';
        r.count++;
        while (*p && *p != ',') ++p;
        if (*p == ',') ++p;
        else break;
    }
    return r;
}


inline bool first_data_row(const knst_byte_string& data, char* line, size_t line_sz) noexcept {
    if (data.empty()) return false;
    const char* p = (const char*)data.data();
    const char* e1 = ::strchr(p, '\n');
    if (!e1) return false;
    p = e1 + 1;
    const char* e2 = ::strchr(p, '\n');
    size_t len = e2 ? (size_t)(e2 - p) : ::strlen(p);
    if (len == 0) return false;
    if (len >= line_sz) len = line_sz - 1;
    ::memcpy(line, p, len); line[len] = '\0';
    return true;
}


inline void enrich_wireless(knst_vector<knst_network_info>& out) noexcept {
    auto r = knst_process::run_capture(u"netsh.exe",
        { u"wlan", u"show", u"interfaces" }, {}, nullptr, 0, 3000);
    if (r.error != knst_process_error::None || r.exit_code != 0) return;
    if (r.out_data.empty()) return;

    const char* p = (const char*)r.out_data.data();
    knst_network_info* cur = nullptr;

    while (p && *p) {
        const char* e = ::strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : ::strlen(p);
        char line[512];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        ::memcpy(line, p, len); line[len] = '\0';

        size_t ll = ::strlen(line);
        while (ll > 0 && (line[ll-1] == '\r' || line[ll-1] == ' ')) line[--ll] = '\0';

        char* content = line;
        while (*content == ' ') ++content;

        const char* colon = ::strchr(content, ':');
        if (colon) {
            char key[64] = {0};
            size_t kl = (size_t)(colon - content);
            while (kl > 0 && content[kl-1] == ' ') --kl;
            if (kl >= sizeof(key)) kl = sizeof(key) - 1;
            ::memcpy(key, content, kl); key[kl] = '\0';

            const char* v = colon + 1;
            while (*v == ' ') ++v;

            if (::strcmp(key, "Name") == 0) {
                cur = nullptr;
                for (auto& n : out) {
                    if (n.name == knst_c16string(v)) { cur = &n; break; }
                }
                if (cur) { cur->is_wireless = true; cur->type = knst_net_type::WiFi; }
            }
            else if (cur && ::strcmp(key, "SSID") == 0) {
                cur->ssid = knst_c16string(v);
                cur->has_wifi = true;
            }
            else if (cur && ::strcmp(key, "Channel") == 0) {
                cur->channel = (uint32_t)::atoi(v);
            }
            else if (cur && ::strcmp(key, "Authentication") == 0) {
                cur->security = knst_c16string(v);
            }
        }

        if (!e) break;
        p = e + 1;
    }
}

// ============================================================
// WMI with custom namespace (root\WMI, root\CIMV2, ...)
// ============================================================
inline knst_byte_string wmi_query_ns(const char* ns, const char* cls,const char* props, uint32_t timeout_ms = 5000) noexcept {
    knst_c16string cmd = u"$OutputEncoding=[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; ";
    cmd.append(u"Get-CimInstance -Namespace '");
    cmd.append(knst_c16string(ns));
    cmd.append(u"' -ClassName ");
    cmd.append(knst_c16string(cls));
    cmd.append(u" | Select-Object ");
    cmd.append(knst_c16string(props));
    cmd.append(u" | ConvertTo-Csv -NoTypeInformation");
    auto r = knst_process::run_capture(
        u"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
        { u"-NoProfile", u"-NonInteractive", u"-Command", cmd },
        {}, nullptr, 0, timeout_ms);
    if (r.error != knst_process_error::None || r.exit_code != 0) return knst_byte_string();
    return wmi_normalize_encoding(r.out_data);
}



inline knst_byte_string wmi_monitor_id_query() noexcept {
    knst_c16string cmd = u"$OutputEncoding=[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; ";
    cmd.append(u"Get-CimInstance -Namespace 'root\\WMI' -ClassName WmiMonitorID | ");
    cmd.append(u"Select-Object InstanceName,");
    cmd.append(u"@{N='UserFriendlyName';E={-join ($_.UserFriendlyName | Where-Object {$_ -ne 0} | ForEach-Object {[char]$_})}},");
    cmd.append(u"@{N='ManufacturerName';E={-join ($_.ManufacturerName | Where-Object {$_ -ne 0} | ForEach-Object {[char]$_})}},");
    cmd.append(u"ProductCodeID,");
    cmd.append(u"@{N='SerialNumberID';E={-join ($_.SerialNumberID | Where-Object {$_ -ne 0} | ForEach-Object {[char]$_})}},");
    cmd.append(u"YearOfManufacture,WeekOfManufacture | ");
    cmd.append(u"ConvertTo-Csv -NoTypeInformation");
    auto r = knst_process::run_capture(
        u"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
        { u"-NoProfile", u"-NonInteractive", u"-Command", cmd },
        {}, nullptr, 0, 5000);
    if (r.error != knst_process_error::None || r.exit_code != 0) return knst_byte_string();
    return wmi_normalize_encoding(r.out_data);
}

// ============================================================
// EDID parsing helpers (monitor)
// ============================================================
inline char16_t edid_letter_win(uint8_t five_bits) noexcept {
    if (five_bits < 1 || five_bits > 26) return u'?';
    return static_cast<char16_t>(u'A' + five_bits - 1);
}

inline void parse_edid_win(const unsigned char* edid, knst_monitor_info& m) noexcept {
    if (!edid) return;
    if (edid[0] != 0x00 || edid[1] != 0xFF || edid[2] != 0xFF || edid[3] != 0xFF) return;

    uint16_t mid = ((uint16_t)edid[8] << 8) | edid[9];
    char16_t mfg[4] = {0};
    mfg[0] = edid_letter_win((mid >> 10) & 0x1F);
    mfg[1] = edid_letter_win((mid >> 5)  & 0x1F);
    mfg[2] = edid_letter_win(mid & 0x1F);
    m.manufacturer = knst_c16string(mfg);

    m.product_code = (uint32_t)edid[10] | ((uint32_t)edid[11] << 8);
    uint32_t serial = (uint32_t)edid[12] | ((uint32_t)edid[13] << 8)
                    | ((uint32_t)edid[14] << 16) | ((uint32_t)edid[15] << 24);
    char serbuf[16]; ::snprintf(serbuf, sizeof(serbuf), "%u", serial);
    m.serial = knst_c16string(serbuf);

    m.week = edid[16];
    m.year = 1990 + edid[17];
    m.physical_width_mm  = (uint32_t)edid[21] * 10;
    m.physical_height_mm = (uint32_t)edid[22] * 10;

    if (edid[54] != 0) {
        uint32_t pixel_clock_hz = (((uint32_t)edid[55] << 8) | edid[54]) * 10000u;
        uint32_t hactive = edid[56] | ((uint32_t)(edid[58] & 0xF0) << 4);
        uint32_t hblank  = edid[57] | ((uint32_t)(edid[58] & 0x0F) << 8);
        uint32_t vactive = edid[59] | ((uint32_t)(edid[61] & 0xF0) << 4);
        uint32_t vblank  = edid[60] | ((uint32_t)(edid[61] & 0x0F) << 8);
        m.native_width = hactive;
        m.native_height = vactive;
        uint32_t htotal = hactive + hblank;
        uint32_t vtotal = vactive + vblank;
        if (htotal > 0 && vtotal > 0 && pixel_clock_hz > 0)
            m.refresh_hz = pixel_clock_hz / (htotal * vtotal);
    }
    if (edid[23] != 0xFF) m.gamma = (float)(edid[23] + 100) / 100.0f;
    m.valid = true;
}

// ============================================================
// Read EDID bytes from registry for a monitor device
// ============================================================
inline bool read_monitor_edid(const wchar_t* device_id, unsigned char* out, DWORD out_sz) noexcept {
    if (!device_id || !out || out_sz < 128) return false;

    // device_id format: \\?\DISPLAY#<VENDOR>#<INSTANCE>#{GUID}
    // Registry path:    HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\<VENDOR>\<INSTANCE>
    const wchar_t* p = device_id;
    if (p[0] == L'\\' && p[1] == L'\\' && p[2] == L'?' && p[3] == L'\\') p += 4;
    if (::_wcsnicmp(p, L"DISPLAY#", 8) != 0) return false;
    p += 8;

    // vendor
    const wchar_t* vstart = p;
    while (*p && *p != L'#') ++p;
    if (*p != L'#') return false;
    size_t vlen = (size_t)(p - vstart);
    p++;

    // instance
    const wchar_t* istart = p;
    while (*p && *p != L'#') ++p;
    size_t ilen = (size_t)(p - istart);

    wchar_t vendor[64] = {0};
    wchar_t instance[128] = {0};
    if (vlen >= 64 || ilen >= 128) return false;
    ::wcsncpy_s(vendor, vstart, vlen);
    ::wcsncpy_s(instance, istart, ilen);

    wchar_t regpath[512];
    ::swprintf_s(regpath, L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\%s\\%s\\Device Parameters",
                 vendor, instance);

    HKEY hKey = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, regpath, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD type = 0, sz = out_sz;
    LONG rc = ::RegQueryValueExW(hKey, L"EDID", nullptr, &type, out, &sz);
    ::RegCloseKey(hKey);

    if (rc != ERROR_SUCCESS || type != REG_BINARY || sz < 128) return false;
    return true;
}

// ============================================================
// HID helpers for input enumeration
// ============================================================
inline knst_c16string hid_product_name(const wchar_t* device_path) noexcept {
    knst_c16string result;
    HANDLE h = ::CreateFileW(device_path, GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return result;

    wchar_t product[256] = {0};
    if (HidD_GetProductString(h, product, sizeof(product))) {
        result = knst_c16string(reinterpret_cast<const char16_t*>(product));
    }

    HIDD_ATTRIBUTES attr = {}; attr.Size = sizeof(attr);
    if (HidD_GetAttributes(h, &attr)) {
        // attr.VendorID / attr.ProductID
    }

    ::CloseHandle(h);
    return result;
}

} // namespace detail


inline knst_cpu_info cpu() noexcept {
    knst_cpu_info c;
    auto data = detail::wmi_query("Win32_Processor",
        "Name,Manufacturer,NumberOfCores,NumberOfLogicalProcessors,"
        "MaxClockSpeed,CurrentClockSpeed,L2CacheSize,L3CacheSize,"
        "AddressWidth,DataWidth,Architecture,Family,Stepping,"
        "SocketDesignation");

    char line[2048];
    if (!detail::first_data_row(data, line, sizeof(line))) return c;

    auto row = detail::parse_csv(line);
    if (row.count > 0) c.model_name = knst_c16string(row.cols[0]);
    if (row.count > 1) c.vendor = knst_c16string(row.cols[1]);
    if (row.count > 2) c.physical_cores = (uint32_t)::atoi(row.cols[2]);
    if (row.count > 3) c.logical_cores = (uint32_t)::atoi(row.cols[3]);
    if (row.count > 4) {
        c.max_mhz = (uint32_t)::atoi(row.cols[4]);

        if (c.max_mhz > 0) c.base_mhz = c.max_mhz;
    }
    if (row.count > 5) c.current_mhz = (uint32_t)::atoi(row.cols[5]);
    if (row.count > 6) c.cache_l2_kb = (uint32_t)::atoi(row.cols[6]);
    if (row.count > 7) c.cache_l3_kb = (uint32_t)::atoi(row.cols[7]);

    // ─── Architecture ───
    // WMI "Architecture" numeric (0=x86, 5=ARM, 9=x64, 12=ARM64)
    if (row.count > 10) {
        uint32_t arch = (uint32_t)::atoi(row.cols[10]);
        switch (arch) {
            case 0:  c.architecture = u"x86";    break;
            case 5:  c.architecture = u"ARM";    break;
            case 9:  c.architecture = u"x86_64"; break;
            case 12: c.architecture = u"ARM64";  break;
            default: break;
        }
    }
    // Fallback: AddressWidth
    if (c.architecture.empty() && row.count > 8) {
        uint32_t addr = (uint32_t)::atoi(row.cols[8]);
        if (addr == 64) c.architecture = u"x86_64";
        else if (addr == 32) c.architecture = u"x86";
    }


    {
       
        const char* p = (const char*)data.data();
        const char* e1 = ::strchr(p, '\n');
        if (e1) p = e1 + 1;

        uint32_t sockets = 0;
        while (p && *p) {
            const char* e = ::strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : ::strlen(p);
            if (len > 5) ++sockets;
            if (!e) break;
            p = e + 1;
        }
        c.socket_count = sockets > 0 ? sockets : 1;
    }

    // ─── Stepping ───
    if (row.count > 13) c.stepping = (uint32_t)::atoi(row.cols[13]);

    c.has_cores = true;
    c.has_frequency = true;
    c.has_cache = (c.cache_l2_kb > 0 || c.cache_l3_kb > 0);
    c.has_hyperthreading = (c.logical_cores > c.physical_cores);
    c.valid = true;
    return c;
}

inline knst_memory_info memory() noexcept {
    knst_memory_info m;
    MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        m.total_bytes = ms.ullTotalPhys;
        m.free_bytes  = ms.ullAvailPhys;
        m.used_bytes  = m.total_bytes - m.free_bytes;
        m.available_bytes = ms.ullAvailPhys;
        m.valid = true;
    }

    auto data = detail::wmi_query("Win32_PhysicalMemory",
        "Capacity,Speed,Manufacturer,PartNumber,SerialNumber,MemoryType,FormFactor,DeviceLocator");
    if (!data.empty()) {
        const char* p = (const char*)data.data();
        const char* e1 = ::strchr(p, '\n');
        if (e1) p = e1 + 1;
        uint32_t slot = 0;
        while (p && *p) {
            const char* e = ::strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : ::strlen(p);
            char line[2048];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';

            auto row = detail::parse_csv(line);
            if (row.count >= 8) {
                knst_memory_module mod;
                mod.size_bytes   = (uint64_t)::strtoull(row.cols[0], nullptr, 10);
                mod.speed_mhz    = (uint32_t)::atoi(row.cols[1]);
                mod.manufacturer = knst_c16string(row.cols[2]);
                mod.part_number  = knst_c16string(row.cols[3]);
                mod.serial       = knst_c16string(row.cols[4]);
                mod.locator      = knst_c16string(row.cols[7]);
                mod.slot_index   = slot++;
                mod.valid = true;
                m.modules.push_back(mod);
            }

            if (!e) break;
            p = e + 1;
        }
        if (!m.modules.empty()) {
            m.has_modules = true;
            m.slot_count = (uint32_t)m.modules.size();
            m.slots_used = (uint32_t)m.modules.size();
        }
    }

    return m;
}


inline knst_vector<knst_storage_info> storage() noexcept {
    knst_vector<knst_storage_info> out;

    auto data = detail::wmi_query("Win32_DiskDrive",
        "DeviceID,Model,SerialNumber,Size,InterfaceType,MediaType,FirmwareRevision");
    if (data.empty()) return out;

    const char* p = (const char*)data.data();
    const char* e1 = ::strchr(p, '\n');
    if (e1) p = e1 + 1;
    while (p && *p) {
        const char* e = ::strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : ::strlen(p);
        char line[2048];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        ::memcpy(line, p, len); line[len] = '\0';

        auto row = detail::parse_csv(line);
        if (row.count >= 7) {
            knst_storage_info s;
            s.device = knst_c16string(row.cols[0]);
            s.model = knst_c16string(row.cols[1]);
            s.serial = knst_c16string(row.cols[2]);
            s.total_bytes = (uint64_t)::strtoull(row.cols[3], nullptr, 10);
            s.firmware = knst_c16string(row.cols[6]);

            if (::strstr(row.cols[4], "NVMe") || ::strstr(row.cols[4], "SCSI"))
                s.type = knst_storage_type::SSD;
            else if (::strstr(row.cols[4], "USB")) {
                s.type = knst_storage_type::USB;
                s.is_removable = true;
            } else s.type = knst_storage_type::HDD;

            s.has_model = !s.model.empty();
            s.valid = true;
            out.push_back(s);
        }
        if (!e) break;
        p = e + 1;
    }


    {
      
        auto part = detail::wmi_query("Win32_DiskPartition",
            "DeviceID,DiskIndex,Index,BootPartition,SystemPartition");
        if (!part.empty()) {
            
            uint32_t sys_disk_index = UINT32_MAX;
            const char* pp = (const char*)part.data();
            const char* pe1 = ::strchr(pp, '\n');
            if (pe1) pp = pe1 + 1;
            while (pp && *pp) {
                const char* pe = ::strchr(pp, '\n');
                size_t plen = pe ? (size_t)(pe - pp) : ::strlen(pp);
                char pline[1024];
                if (plen >= sizeof(pline)) plen = sizeof(pline) - 1;
                ::memcpy(pline, pp, plen); pline[plen] = '\0';

                auto prow = detail::parse_csv(pline);
                if (prow.count >= 5) {
                    bool is_boot = (::strcmp(prow.cols[2], "TRUE") == 0
                                 || ::strcmp(prow.cols[2], "True") == 0);
                    bool is_sys  = (::strcmp(prow.cols[3], "TRUE") == 0
                                 || ::strcmp(prow.cols[3], "True") == 0);
                    if (is_boot || is_sys) {
                        uint32_t di = (uint32_t)::atoi(prow.cols[1]);
                        sys_disk_index = di;
                        break;
                    }
                }
                if (!pe) break;
                pp = pe + 1;
            }

            if (sys_disk_index != UINT32_MAX) {
                
                knst_c16string target = u"\\\\.\\PHYSICALDRIVE";
                target.append((long long)sys_disk_index);
                for (auto& s : out) {
                    if (s.device == target) {
                        s.is_system_disk = true;
                        break;
                    }
                }
            }
        }
    }

   
    auto pd = detail::wmi_query_ns("root\\Microsoft\\Windows\\Storage",
        "MSFT_PhysicalDisk", "DeviceId,MediaType,FriendlyName");
    if (!pd.empty()) {
        const char* pdp = (const char*)pd.data();
        const char* pde1 = ::strchr(pdp, '\n');
        if (pde1) pdp = pde1 + 1;
        while (pdp && *pdp) {
            const char* pde = ::strchr(pdp, '\n');
            size_t pdlen = pde ? (size_t)(pde - pdp) : ::strlen(pdp);
            char pdline[512];
            if (pdlen >= sizeof(pdline)) pdlen = sizeof(pdline) - 1;
            ::memcpy(pdline, pdp, pdlen); pdline[pdlen] = '\0';

            auto pdrow = detail::parse_csv(pdline);
            if (pdrow.count >= 2) {
                uint32_t did = (uint32_t)::atoi(pdrow.cols[0]);
                uint32_t mt  = (uint32_t)::atoi(pdrow.cols[1]);
                for (auto& s : out) {
                    if (!s.device.contains(u"PHYSICALDRIVE")) continue;
                    uint32_t n = 0;
                    bool got_num = false;
                    for (uint32_t i = 0; i < s.device.length(); ++i) {
                        char16_t c = s.device[i];
                        if (c >= u'0' && c <= u'9') {
                            n = n * 10 + (c - u'0');
                            got_num = true;
                        } else if (got_num) {
                            break;
                        }
                    }
                    if (got_num && n == did) {
                        if (mt == 4) {           // 4 = SSD
                            s.is_solid_state = true;
                            s.type = knst_storage_type::SSD;
                        } else if (mt == 3) {    // 3 = HDD
                            s.is_solid_state = false;
                            s.type = knst_storage_type::HDD;
                        }
                    }
                }
            }
            if (!pde) break;
            pdp = pde + 1;
        }
    }

    return out;
}

inline knst_vector<knst_network_info> network() noexcept {
    knst_vector<knst_network_info> out;


    alignas(8) static unsigned char buffer[32768];
    ULONG bufSize = sizeof(buffer);
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;

    ULONG ret = ::GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer), &bufSize);
    if (ret != NO_ERROR) return out;

    IP_ADAPTER_ADDRESSES* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer);
    while (a) {


        knst_network_info n;
        n.name = knst_c16string(reinterpret_cast<const char16_t*>(a->FriendlyName));
        n.description = knst_c16string(reinterpret_cast<const char16_t*>(a->Description));

        if (a->PhysicalAddressLength == 6) {
            char mac[32];
            ::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                a->PhysicalAddress[0], a->PhysicalAddress[1],
                a->PhysicalAddress[2], a->PhysicalAddress[3],
                a->PhysicalAddress[4], a->PhysicalAddress[5]);
            n.mac = knst_c16string(mac);
        }

        n.is_up = (a->OperStatus == IfOperStatusUp);

        IP_ADAPTER_UNICAST_ADDRESS* ua = a->FirstUnicastAddress;
        while (ua) {
            if (ua->Address.lpSockaddr) {
                char ip[64] = {0};
                if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                    auto* sin = (struct sockaddr_in*)ua->Address.lpSockaddr;
                    ::inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                    if (n.ipv4.empty()) n.ipv4 = knst_c16string(ip);
                    n.has_ip = true;
                } else if (ua->Address.lpSockaddr->sa_family == AF_INET6) {
                    auto* sin6 = (struct sockaddr_in6*)ua->Address.lpSockaddr;
                    ::inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
                    if (n.ipv6.empty()) n.ipv6 = knst_c16string(ip);
                    n.has_ip = true;
                }
            }
            ua = ua->Next;
        }

        IP_ADAPTER_GATEWAY_ADDRESS_LH* gw = a->FirstGatewayAddress;
        if (gw && gw->Address.lpSockaddr
            && gw->Address.lpSockaddr->sa_family == AF_INET) {
            char ip[64] = {0};
            auto* sin = (struct sockaddr_in*)gw->Address.lpSockaddr;
            ::inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            n.gateway = knst_c16string(ip);
        }

        IP_ADAPTER_DNS_SERVER_ADDRESS_XP* dns = a->FirstDnsServerAddress;
        if (dns && dns->Address.lpSockaddr
            && dns->Address.lpSockaddr->sa_family == AF_INET) {
            char ip[64] = {0};
            auto* sin = (struct sockaddr_in*)dns->Address.lpSockaddr;
            ::inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            n.dns = knst_c16string(ip);
        }

        n.type = knst_net_type::Ethernet;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            n.type = knst_net_type::Loopback;
            n.is_loopback = true;
        } else if (a->IfType == IF_TYPE_IEEE80211) {
            n.type = knst_net_type::WiFi;
            n.is_wireless = true;
        } else if (a->IfType == IF_TYPE_TUNNEL
                || a->IfType == IF_TYPE_PPP) {
            n.is_virtual = true;
        }

        // ─── rx/tx bytes: MIB_IF_ROW2 (GetIfEntry2) ───
        {
            MIB_IF_ROW2 row = {};
            row.InterfaceIndex = a->IfIndex;
            row.InterfaceLuid  = a->Luid;
            if (::GetIfEntry2(&row) == NO_ERROR) {
                n.rx_bytes   = row.InOctets;
                n.tx_bytes   = row.OutOctets;
                n.rx_packets = row.InUcastPkts + row.InNUcastPkts;
                n.tx_packets = row.OutUcastPkts + row.OutNUcastPkts;
                n.rx_errors  = row.InErrors;
                n.tx_errors  = row.OutErrors;
                n.rx_dropped = row.InDiscards;
                n.tx_dropped = row.OutDiscards;
                n.has_stats  = true;

                if (row.Mtu > 0 && n.mtu == 0) n.mtu = row.Mtu;
                if (row.TransmitLinkSpeed > 0 && n.speed_mbps == 0)
                    n.speed_mbps = (uint32_t)(row.TransmitLinkSpeed / 1000000ULL);
            }
        }


        if (n.is_up || n.has_ip || n.is_loopback) {
            n.valid = true;
            out.push_back(n);
        }

        a = a->Next;
    }

    detail::enrich_wireless(out);
    return out;
}


inline knst_vector<knst_usb_info> usb() noexcept {
    knst_vector<knst_usb_info> out;
    auto data = detail::wmi_query("Win32_PnPEntity",
        "Name,DeviceID,Manufacturer,Service",
        "DeviceID LIKE 'USB%'");
    if (data.empty()) return out;

    const char* p = (const char*)data.data();
    const char* e1 = ::strchr(p, '\n');
    if (e1) p = e1 + 1;
    while (p && *p) {
        const char* e = ::strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : ::strlen(p);
        char line[2048];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        ::memcpy(line, p, len); line[len] = '\0';

        knst_usb_info u;
        const char* vid = ::strstr(line, "VID_");
        if (vid) {
            unsigned v; if (::sscanf(vid, "VID_%04X", &v) == 1) u.vid = (uint16_t)v;
        }
        const char* pid = ::strstr(line, "PID_");
        if (pid) {
            unsigned v; if (::sscanf(pid, "PID_%04X", &v) == 1) u.pid = (uint16_t)v;
        }

        auto row = detail::parse_csv(line);
        if (row.count > 0) u.product_name = knst_c16string(row.cols[0]);
        if (row.count > 2) u.manufacturer = knst_c16string(row.cols[2]);

        u.valid = true;
        out.push_back(u);

        if (!e) break;
        p = e + 1;
    }

    return out;
}



inline knst_vector<knst_pci_info> pci() noexcept {
    knst_vector<knst_pci_info> out;
    auto data = detail::wmi_query("Win32_PnPEntity",
        "Name,DeviceID,Manufacturer,Service",
        "DeviceID LIKE 'PCI%'");
    if (data.empty()) return out;

    const char* p = (const char*)data.data();
    const char* e1 = ::strchr(p, '\n');
    if (e1) p = e1 + 1;
    while (p && *p) {
        const char* e = ::strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : ::strlen(p);
        char line[2048];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        ::memcpy(line, p, len); line[len] = '\0';

        knst_pci_info info;
        const char* ven = ::strstr(line, "VEN_");
        if (ven) {
            unsigned v; if (::sscanf(ven, "VEN_%04X", &v) == 1) info.vendor_id = (uint16_t)v;
        }
        const char* dev = ::strstr(line, "DEV_");
        if (dev) {
            unsigned v; if (::sscanf(dev, "DEV_%04X", &v) == 1) info.device_id = (uint16_t)v;
        }

        auto row = detail::parse_csv(line);
        if (row.count > 0) info.device_name = knst_c16string(row.cols[0]);
        if (row.count > 2) info.vendor_name  = knst_c16string(row.cols[2]);
               if (row.count > 3) {
            info.driver = knst_c16string(row.cols[3]);

            while (info.driver.length() > 0) {
                char16_t last = info.driver[info.driver.length() - 1];
                if (last == u'\n' || last == u'\r' || last == u' ' || last == u'\t') {
                    info.driver.resize(info.driver.length() - 1);
                } else break;
            }
        }

        info.valid = true;
        out.push_back(info);

        if (!e) break;
        p = e + 1;
    }

    return out;
}


inline knst_motherboard_info motherboard() noexcept {
    knst_motherboard_info m;

    {
        auto bb = detail::wmi_query("Win32_BaseBoard", "Manufacturer,Product,Version,SerialNumber");
        char line[2048];
        if (detail::first_data_row(bb, line, sizeof(line))) {
            auto row = detail::parse_csv(line);
            if (row.count > 0) m.manufacturer = knst_c16string(row.cols[0]);
            if (row.count > 1) m.product      = knst_c16string(row.cols[1]);
            if (row.count > 2) m.version      = knst_c16string(row.cols[2]);
            if (row.count > 3) m.serial       = knst_c16string(row.cols[3]);
            m.has_board = !m.product.empty();
        }
    }

    {
        auto bios = detail::wmi_query("Win32_BIOS", "Manufacturer,SMBIOSBIOSVersion,ReleaseDate");
        char line[2048];
        if (detail::first_data_row(bios, line, sizeof(line))) {
            auto row = detail::parse_csv(line);
            if (row.count > 0) m.bios_vendor  = knst_c16string(row.cols[0]);
            if (row.count > 1) m.bios_version = knst_c16string(row.cols[1]);
            if (row.count > 2) m.bios_date    = knst_c16string(row.cols[2]);
            m.has_bios = !m.bios_version.empty();
        }
    }

    {
        FIRMWARE_TYPE ft = FirmwareTypeUnknown;
        if (GetFirmwareType(&ft)) {
            m.uefi_boot = (ft == FirmwareTypeUefi);
        }
    }



    m.valid = true;
    return m;
}


inline knst_vector<knst_printer_info> printers() noexcept {
    knst_vector<knst_printer_info> out;
    auto data = detail::wmi_query("Win32_Printer",
        "Name,DriverName,PrinterStatus,PortName,Default,Shared,WorkOffline");
    if (data.empty()) return out;

    const char* p = (const char*)data.data();
    const char* e1 = ::strchr(p, '\n');
    if (e1) p = e1 + 1;
    while (p && *p) {
        const char* e = ::strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : ::strlen(p);
        char line[2048];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        ::memcpy(line, p, len); line[len] = '\0';

        auto row = detail::parse_csv(line);
        if (row.count >= 7) {
            knst_printer_info pr;
            pr.name  = knst_c16string(row.cols[0]);
            pr.model = knst_c16string(row.cols[1]);

            int status = ::atoi(row.cols[2]);
            switch (status) {
                case 3: pr.status = u"idle"; break;
                case 4: pr.status = u"printing"; break;
                case 5: pr.status = u"warmup"; break;
                default: pr.status = u"unknown"; break;
            }

            pr.uri = knst_c16string(row.cols[3]);
            pr.is_default   = (::strcmp(row.cols[4], "True") == 0);
            pr.is_shared    = (::strcmp(row.cols[5], "True") == 0);
            pr.accepts_jobs = (::strcmp(row.cols[6], "True") != 0);
            pr.valid = true;
            out.push_back(pr);
        }
        if (!e) break;
        p = e + 1;
    }

    return out;
}


inline knst_vector<knst_driver_info> drivers() noexcept {
    knst_vector<knst_driver_info> out;
    auto data = detail::wmi_query("Win32_SystemDriver", "Name,DisplayName,State,StartMode,PathName");
    if (data.empty()) return out;

    const char* p = (const char*)data.data();
    const char* e1 = ::strchr(p, '\n');
    if (e1) p = e1 + 1;
    while (p && *p) {
        const char* e = ::strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : ::strlen(p);
        char line[2048];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        ::memcpy(line, p, len); line[len] = '\0';

        auto row = detail::parse_csv(line);
        if (row.count >= 5) {
            knst_driver_info d;
            d.name = knst_c16string(row.cols[0]);
            d.vendor = knst_c16string(row.cols[1]);
            d.is_builtin = (::strcmp(row.cols[2], "Running") == 0);
            d.valid = true;
            out.push_back(d);
        }
        if (!e) break;
        p = e + 1;
    }

    return out;
}


inline knst_system_info system() noexcept {
    knst_system_info s;
    auto data = detail::wmi_query("Win32_ComputerSystem",
        "Manufacturer,Model,TotalPhysicalMemory,SystemFamily,SystemSKUNumber");

    char line[2048];
    if (!detail::first_data_row(data, line, sizeof(line))) return s;

    auto row = detail::parse_csv(line);
    if (row.count > 0) s.manufacturer = knst_c16string(row.cols[0]);
    if (row.count > 1) s.product_name = knst_c16string(row.cols[1]);
    if (row.count > 3) s.family = knst_c16string(row.cols[3]);
    if (row.count > 4) s.sku = knst_c16string(row.cols[4]);

    s.valid = true;
    return s;
}

inline knst_os_info os() noexcept {
    knst_os_info o;

    typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (nt) {
        auto fn = (RtlGetVersionPtr)GetProcAddress(nt, "RtlGetVersion");
        if (fn) {
            RTL_OSVERSIONINFOW vi = {}; vi.dwOSVersionInfoSize = sizeof(vi);
            if (fn(&vi) == 0) {
                char buf[64];
                ::snprintf(buf, sizeof(buf), "%lu.%lu.%lu",
                    vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
                o.os_version = knst_c16string(buf);


                ::snprintf(buf, sizeof(buf), "%lu.%lu.%lu",
                    vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
                o.kernel_version = knst_c16string(buf);

                o.os_name = u"Windows";
                o.kernel_name = u"Windows NT";
            }
        }
    }


    // ─── Architecture ───
    {
        SYSTEM_INFO si = {};
        GetNativeSystemInfo(&si);
        switch (si.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64: o.kernel_arch = u"x86_64"; break;
            case PROCESSOR_ARCHITECTURE_INTEL: o.kernel_arch = u"x86";    break;
            case PROCESSOR_ARCHITECTURE_ARM:   o.kernel_arch = u"ARM";    break;
            case PROCESSOR_ARCHITECTURE_ARM64: o.kernel_arch = u"ARM64";  break;
            case PROCESSOR_ARCHITECTURE_IA64:  o.kernel_arch = u"IA64";   break;
            default:                            o.kernel_arch = u"unknown"; break;
        }
    }

    wchar_t hostname[256]; DWORD sz = sizeof(hostname)/sizeof(hostname[0]);
    if (GetComputerNameW(hostname, &sz))
        o.hostname = knst_c16string(reinterpret_cast<const char16_t*>(hostname), sz);
    sz = sizeof(hostname)/sizeof(hostname[0]);
    if (GetUserNameW(hostname, &sz))
        o.user = knst_c16string(reinterpret_cast<const char16_t*>(hostname), sz - 1);

    // ─── Home directory ───
    {
        wchar_t home_buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home_buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            o.home_dir = knst_c16string(reinterpret_cast<const char16_t*>(home_buf), n);
        }
    }

  
    {
        wchar_t sh_buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"COMSPEC", sh_buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            o.shell = knst_c16string(reinterpret_cast<const char16_t*>(sh_buf), n);
        }
    }

    // ─── Timezone ───
    {

        DYNAMIC_TIME_ZONE_INFORMATION dtzi = {};
        DWORD tz_result = GetDynamicTimeZoneInformation(&dtzi);
        if (tz_result != TIME_ZONE_ID_INVALID) {
            o.timezone = knst_c16string(reinterpret_cast<const char16_t*>(dtzi.TimeZoneKeyName));
 
            if (!o.timezone.empty()) {
                LONG bias_min = dtzi.Bias + dtzi.StandardBias;
                int offset_h = -(int)(bias_min / 60);
                char off_buf[16];
                ::snprintf(off_buf, sizeof(off_buf), " (UTC%+d)", offset_h);
                o.timezone.append(knst_c16string(off_buf));
            }
        }
    }

    o.uptime_seconds = GetTickCount64() / 1000;
    {
        SYSTEM_INFO si = {};
        GetSystemInfo(&si);
        o.page_size = si.dwPageSize;
    }

    o.valid = true;
    return o;
}

inline knst_power_info power() noexcept {
    knst_power_info p;
    SYSTEM_POWER_STATUS sps = {};
    if (GetSystemPowerStatus(&sps)) {
        p.has_battery = (sps.BatteryFlag & 128) == 0;
        p.ac_connected = (sps.ACLineStatus == 1);

        if (p.has_battery) {
            knst_battery_info b;
            b.percent = (uint32_t)sps.BatteryLifePercent;
            b.ac_online = p.ac_connected;
            switch (sps.BatteryFlag & 0x0F) {
                case 1: b.state = knst_battery_state::Discharging; break;
                case 2: b.state = knst_battery_state::Charging;    break;
                case 3: b.state = knst_battery_state::Full;        break;
                default: b.state = knst_battery_state::Unknown;    break;
            }
            if (sps.BatteryLifeTime != (DWORD)-1) b.seconds_left = sps.BatteryLifeTime;
            b.valid = true;
            p.batteries.push_back(b);
        }
        p.valid = true;
    }
    return p;
}



inline knst_vector<knst_monitor_info> monitors() noexcept {
    knst_vector<knst_monitor_info> out;

    // Enumerate adapter devices, then their child monitor devices.
    DISPLAY_DEVICEW adapter = {};
    adapter.cb = sizeof(adapter);
    DWORD adapter_idx = 0;
    uint32_t monitor_index = 0;

    while (EnumDisplayDevicesW(nullptr, adapter_idx, &adapter, 0)) {
        if (adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            DISPLAY_DEVICEW mon = {};
            mon.cb = sizeof(mon);
            DWORD mon_idx = 0;

            while (EnumDisplayDevicesW(adapter.DeviceName, mon_idx, &mon, 0)) {
                knst_monitor_info m;
                m.index = monitor_index++;
                m.name = knst_c16string(reinterpret_cast<const char16_t*>(mon.DeviceString));

                // Physical size / EDID via registry
                unsigned char edid[256] = {0};
                if (detail::read_monitor_edid(mon.DeviceID, edid, sizeof(edid))) {
                    detail::parse_edid_win(edid, m);
                }

                // Current resolution / refresh rate
                DEVMODEW dm = {};
                dm.dmSize = sizeof(dm);
                if (EnumDisplaySettingsW(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
                    m.current_width  = dm.dmPelsWidth;
                    m.current_height = dm.dmPelsHeight;
                    m.refresh_hz     = dm.dmDisplayFrequency > 1 ? dm.dmDisplayFrequency : 60;

                    // Native = current unless EDID says otherwise
                    if (m.native_width == 0) {
                        m.native_width = dm.dmPelsWidth;
                        m.native_height = dm.dmPelsHeight;
                    }

                    m.is_primary = (adapter.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

                    // Determine connection type from adapter name
                    const wchar_t* ad = adapter.DeviceString;
                    if (::wcsstr(ad, L"HDMI"))          m.connection = knst_monitor_conn::HDMI;
                    else if (::wcsstr(ad, L"DisplayPort")) m.connection = knst_monitor_conn::DisplayPort;
                    else if (::wcsstr(ad, L"DVI"))         m.connection = knst_monitor_conn::DVI;
                    else if (::wcsstr(ad, L"VGA"))         m.connection = knst_monitor_conn::VGA;
                    else if (::wcsstr(ad, L"USB-C")
                          || ::wcsstr(ad, L"USB Type-C")) m.connection = knst_monitor_conn::USB_C;
                    else if (::wcsstr(ad, L"eDP"))         m.connection = knst_monitor_conn::eDP;
                    else if (::wcsstr(ad, L"LVDS"))        m.connection = knst_monitor_conn::LVDS;
                }

                // Diagonal in inches
                if (m.physical_width_mm > 0 && m.physical_height_mm > 0) {
                    double w_inch = m.physical_width_mm / 25.4;
                    double h_inch = m.physical_height_mm / 25.4;
                    m.diagonal_inch = (float)::sqrt(w_inch * w_inch + h_inch * h_inch);
                }

                m.valid = true;
                out.push_back(m);
                mon_idx++;
            }
        }
        adapter_idx++;
    }

        // Backlight brightness (laptop internal panel)
    {
        // Use WMI: WmiMonitorBrightness
        auto bdata = detail::wmi_query_ns("root\\WMI", "WmiMonitorBrightness",
            "CurrentBrightness", 2000);
        if (!bdata.empty()) {
            char line[512];
            if (detail::first_data_row(bdata, line, sizeof(line))) {
                auto row = detail::parse_csv(line);
                if (row.count > 0 && !out.empty()) {
                    uint32_t b = (uint32_t)::atoi(row.cols[0]);
                    for (auto& m : out) {
                        if (m.connection == knst_monitor_conn::eDP
                            || m.connection == knst_monitor_conn::LVDS) {
                            m.brightness = b;
                        }
                    }
                }
            }
        }
    }


    {
        auto mid = detail::wmi_monitor_id_query();
        if (!mid.empty()) {
            const char* p = (const char*)mid.data();
            const char* e1 = ::strchr(p, '\n');
            if (e1) p = e1 + 1;
            uint32_t idx = 0;
            while (p && *p && idx < out.size()) {
                const char* e = ::strchr(p, '\n');
                size_t len = e ? (size_t)(e - p) : ::strlen(p);
                char line[2048];
                if (len >= sizeof(line)) len = sizeof(line) - 1;
                ::memcpy(line, p, len); line[len] = '\0';

                auto row = detail::parse_csv(line);
                if (row.count >= 7) {
                    knst_monitor_info& m = out[idx];

                    // CSV: "InstanceName","UserFriendlyName","ManufacturerName","ProductCodeID","SerialNumberID","YearOfManufacture","WeekOfManufacture"
                    knst_c16string name   = knst_c16string(row.cols[1]);
                    knst_c16string mfg    = knst_c16string(row.cols[2]);
                    knst_c16string serial = knst_c16string(row.cols[4]);

                    if (!name.empty())   m.name         = name;
                    if (!mfg.empty())    m.manufacturer = mfg;
                    if (!serial.empty()) m.serial       = serial;

                    uint32_t year = (uint32_t)::atoi(row.cols[5]);
                    uint32_t week = (uint32_t)::atoi(row.cols[6]);
                    if (year > 0) m.year = year;
                    if (week > 0) m.week = week;

                    m.valid = true;
                    idx++;
                }
                if (!e) break;
                p = e + 1;
            }
        }
    }

    return out;
}
inline knst_vector<knst_input_info> input() noexcept {
    knst_vector<knst_input_info> out;

    UINT num = 0;
    if (GetRawInputDeviceList(nullptr, &num, sizeof(RAWINPUTDEVICELIST)) != 0) return out;
    if (num == 0) return out;

    knst_vector<RAWINPUTDEVICELIST> list(num);
    if (GetRawInputDeviceList(list.data(), &num, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1)
        return out;

    for (UINT i = 0; i < num; ++i) {
        const RAWINPUTDEVICELIST& rd = list[i];

        knst_input_info info;
        info.device_path = u"(unknown)";

        // Device path
        UINT path_len = 0;
        GetRawInputDeviceInfoW(rd.hDevice, RIDI_DEVICENAME, nullptr, &path_len);
        if (path_len > 0) {
            knst_vector<wchar_t> pathbuf(path_len + 1);
            path_len = (UINT)pathbuf.size();
            if (GetRawInputDeviceInfoW(rd.hDevice, RIDI_DEVICENAME, pathbuf.data(), &path_len) > 0) {
                info.device_path = knst_c16string(reinterpret_cast<const char16_t*>(pathbuf.data()));
            }
        }

        // Device info
        RID_DEVICE_INFO di = {};
        di.cbSize = sizeof(di);
        UINT di_sz = sizeof(di);
        if (GetRawInputDeviceInfoW(rd.hDevice, RIDI_DEVICEINFO, &di, &di_sz) == (UINT)-1)
            continue;

        switch (di.dwType) {
            case RIM_TYPEMOUSE: {
                info.type = knst_input_type::Mouse;
                info.num_buttons = (uint32_t)di.mouse.dwNumberOfButtons;
                info.name = u"Mouse";

                break;
            }
            case RIM_TYPEKEYBOARD: {
                info.type = knst_input_type::Keyboard;
                info.num_buttons = (uint32_t)di.keyboard.dwNumberOfKeysTotal;
                info.name = u"Keyboard";
                break;
            }
            case RIM_TYPEHID: {
               
                USHORT up = di.hid.usUsagePage;
                USHORT ud = di.hid.usUsage;
                info.vid = (uint16_t)di.hid.dwVendorId;
                info.pid = (uint16_t)di.hid.dwProductId;

                if (up == 0x01) {   // Generic Desktop
                    if (ud == 0x02)       info.type = knst_input_type::Mouse;
                    else if (ud == 0x06)  info.type = knst_input_type::Keyboard;
                    else if (ud == 0x04)  info.type = knst_input_type::Joystick;
                    else if (ud == 0x05)  info.type = knst_input_type::Gamepad;
                    else if (ud == 0x07)  info.type = knst_input_type::Gamepad;
                    else if (ud == 0x08)  info.type = knst_input_type::Gamepad;
                    else if (ud == 0x0D)  info.type = knst_input_type::Tablet;
                    else                  info.type = knst_input_type::Other;
                } else if (up == 0x0D) {   // Digitizer
                    info.type = knst_input_type::Touchscreen;
                } else if (up == 0x0C) {   // Consumer Control
                    info.type = knst_input_type::Remote;
                } else if (up == 0x08) {   // LED
                    info.type = knst_input_type::Other;
                } else {
                    info.type = knst_input_type::Other;
                }

                // Friendly product name (HID)
                knst_vector<wchar_t> wpath(info.device_path.length() + 1);
                for (uint32_t k = 0; k < info.device_path.length(); ++k)
                    wpath[k] = (wchar_t)info.device_path[k];
                wpath[info.device_path.length()] = 0;

                knst_c16string prod = detail::hid_product_name(wpath.data());
                if (!prod.empty()) {
                    info.name = prod;
                } else {
                    // Fallback isim
                    switch (info.type) {
                        case knst_input_type::Mouse:      info.name = u"Mouse";      break;
                        case knst_input_type::Keyboard:   info.name = u"Keyboard";   break;
                        case knst_input_type::Touchscreen:info.name = u"Touchscreen";break;
                        case knst_input_type::Gamepad:    info.name = u"Gamepad";    break;
                        case knst_input_type::Joystick:   info.name = u"Joystick";   break;
                        case knst_input_type::Tablet:     info.name = u"Tablet";     break;
                        case knst_input_type::Remote:     info.name = u"Remote";     break;
                        default:                          info.name = u"HID Device"; break;
                    }
                }
                break;
            }
            default:
                continue;
        }


        if (info.vid == 0 && info.pid == 0) {
            
            knst_byte_string path_utf8(info.device_path);
            const char* p = (const char*)path_utf8.data();
            if (p) {
                const char* vid = ::strstr(p, "VID_");
                if (vid) {
                    unsigned v = 0;
                    if (::sscanf(vid, "VID_%04X", &v) == 1) info.vid = (uint16_t)v;
                }
                const char* pid = ::strstr(p, "PID_");
                if (pid) {
                    unsigned v = 0;
                    if (::sscanf(pid, "PID_%04X", &v) == 1) info.pid = (uint16_t)v;
                }
            }
        }

        // Skip root-enumerated system devices (ROOT\RDP_MOU vs.)
        if (info.device_path.contains(u"ROOT\\")) continue;


        bool dup = false;
        for (uint32_t k = 0; k < out.size(); ++k) {
            if (out[k].device_path == info.device_path) { dup = true; break; }
        }
        if (dup) continue;


        if (info.vid != 0 && info.pid != 0) {
            for (uint32_t k = 0; k < out.size(); ++k) {
                const auto& e = out[k];
                if (e.vid == info.vid && e.pid == info.pid
                    && e.type == info.type
                    && e.name == info.name) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
        }

        info.is_connected = true;
        info.valid = true;
        out.push_back(info);
    }

    return out;
}
inline knst_audio_info audio() noexcept {
    knst_audio_info a;

    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool need_uninit = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) need_uninit = false;

    IMMDeviceEnumerator* enumr = nullptr;
    hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                            __uuidof(IMMDeviceEnumerator), (void**)&enumr);
    if (FAILED(hr)) {
        if (need_uninit) ::CoUninitialize();
        a.valid = true;
        return a;
    }

    auto enum_endpoints = [&](EDataFlow flow, bool is_input) {
        IMMDeviceCollection* coll = nullptr;
        if (FAILED(enumr->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll))) return;

        UINT count = 0;
        coll->GetCount(&count);

        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            if (FAILED(coll->Item(i, &dev))) continue;

            knst_audio_device d;
            d.index = (uint32_t)i;
            d.is_input = is_input;
            d.is_output = !is_input;
            d.valid = true;

            LPWSTR id = nullptr;
            if (SUCCEEDED(dev->GetId(&id))) {
                d.name = knst_c16string(reinterpret_cast<const char16_t*>(id));
                ::CoTaskMemFree(id);
            }

            IPropertyStore* props = nullptr;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
                PROPVARIANT pv;
                ::PropVariantInit(&pv);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv))) {
                    if (pv.vt == VT_LPWSTR && pv.pwszVal) {
                        d.description = knst_c16string(
                            reinterpret_cast<const char16_t*>(pv.pwszVal));
                    }
                }
                ::PropVariantClear(&pv);
                props->Release();
            }

            // Default check
            IMMDevice* defDev = nullptr;
            ERole role = eConsole;
            if (SUCCEEDED(enumr->GetDefaultAudioEndpoint(flow, role, &defDev))) {
                LPWSTR defId = nullptr;
                if (SUCCEEDED(defDev->GetId(&defId))) {
                    if (d.name == knst_c16string(reinterpret_cast<const char16_t*>(defId))) {
                        d.is_default = true;
                        if (is_input) a.default_source = d.description.empty() ? d.name : d.description;
                        else          a.default_sink   = d.description.empty() ? d.name : d.description;
                    }
                    ::CoTaskMemFree(defId);
                }
                defDev->Release();
            }

            a.devices.push_back(d);
            dev->Release();
        }
        coll->Release();
    };

    enum_endpoints(eRender, false);
    enum_endpoints(eCapture, true);

    enumr->Release();
    if (need_uninit) ::CoUninitialize();

    a.valid = true;
    return a;
}
inline knst_vector<knst_sensor_info> sensors() noexcept {
    knst_vector<knst_sensor_info> out;

    // --- Thermal zones (ACPI) ---
    auto therm = detail::wmi_query_ns("root\\WMI", "MSAcpi_ThermalZoneTemperature",
        "CurrentTemperature,InstanceName", 3000);
    if (!therm.empty()) {
        const char* p = (const char*)therm.data();
        const char* e1 = ::strchr(p, '\n');
        if (e1) p = e1 + 1;
        while (p && *p) {
            const char* e = ::strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : ::strlen(p);
            char line[1024];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';

            auto row = detail::parse_csv(line);
            if (row.count >= 2) {
                knst_sensor_info s;
                // CurrentTemperature is in tenths of Kelvin
                double tenths_k = ::atof(row.cols[0]);
                s.value = tenths_k / 10.0 - 273.15;   // Celsius
                s.unit = u"°C";
                s.type = knst_sensor_type::Temperature;
                s.chip = u"ACPI";
                s.name = knst_c16string(row.cols[1]);
                s.valid = true;
                out.push_back(s);
            }
            if (!e) break;
            p = e + 1;
        }
    }

    // --- Fans ---
        auto fans = detail::wmi_query("Win32_Fan", "Name,DesiredSpeed,ActiveCooling", nullptr, 3000);
    if (!fans.empty()) {
        const char* p = (const char*)fans.data();
        const char* e1 = ::strchr(p, '\n');
        if (e1) p = e1 + 1;
        while (p && *p) {
            const char* e = ::strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : ::strlen(p);
            char line[1024];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';

            auto row = detail::parse_csv(line);
            if (row.count >= 2) {
                knst_sensor_info s;
                s.name = knst_c16string(row.cols[0]);
                s.value = ::atof(row.cols[1]);
                s.unit = u"RPM";
                s.type = knst_sensor_type::Fan;
                s.chip = u"Win32_Fan";
                s.valid = true;
                out.push_back(s);
            }
            if (!e) break;
            p = e + 1;
        }
    }

    // --- Voltage probes ---
        auto volts = detail::wmi_query("Win32_VoltageProbe", "Name,CurrentReading", nullptr, 3000);
    if (!volts.empty()) {
        const char* p = (const char*)volts.data();
        const char* e1 = ::strchr(p, '\n');
        if (e1) p = e1 + 1;
        while (p && *p) {
            const char* e = ::strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : ::strlen(p);
            char line[1024];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';

            auto row = detail::parse_csv(line);
            if (row.count >= 2) {
                knst_sensor_info s;
                s.name = knst_c16string(row.cols[0]);
                s.value = ::atof(row.cols[1]) / 1000.0;   // mV -> V
                s.unit = u"V";
                s.type = knst_sensor_type::Voltage;
                s.chip = u"Win32_VoltageProbe";
                s.valid = true;
                out.push_back(s);
            }
            if (!e) break;
            p = e + 1;
        }
    }

    // --- Temperature probes (motherboard) ---
        auto temps = detail::wmi_query("Win32_TemperatureProbe", "Name,CurrentReading", nullptr, 3000);
    if (!temps.empty()) {
        const char* p = (const char*)temps.data();
        const char* e1 = ::strchr(p, '\n');
        if (e1) p = e1 + 1;
        while (p && *p) {
            const char* e = ::strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : ::strlen(p);
            char line[1024];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';

            auto row = detail::parse_csv(line);
            if (row.count >= 2) {
                knst_sensor_info s;
                s.name = knst_c16string(row.cols[0]);
                double v = ::atof(row.cols[1]);
                if (v > 0) {
                    s.value = v / 10.0 - 273.15;
                    s.unit = u"°C";
                    s.type = knst_sensor_type::Temperature;
                    s.chip = u"Win32_TemperatureProbe";
                    s.valid = true;
                    out.push_back(s);
                }
            }
            if (!e) break;
            p = e + 1;
        }
    }

    return out;
}
inline knst_bluetooth_info bluetooth() noexcept {
    knst_bluetooth_info b;

    BLUETOOTH_FIND_RADIO_PARAMS params;
    params.dwSize = sizeof(params);
    HANDLE hRadio = nullptr;
    HBLUETOOTH_RADIO_FIND hFind = ::BluetoothFindFirstRadio(&params, &hRadio);
    if (!hFind) {
        b.valid = true;
        return b;
    }

    b.has_adapter = true;

    do {
        BLUETOOTH_RADIO_INFO ri = {};
        ri.dwSize = sizeof(ri);
        if (::BluetoothGetRadioInfo(hRadio, &ri) == ERROR_SUCCESS) {
            b.adapter_name = knst_c16string(reinterpret_cast<const char16_t*>(ri.szName));
            char addr[32];
            ::snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X",
                ri.address.rgBytes[5], ri.address.rgBytes[4], ri.address.rgBytes[3],
                ri.address.rgBytes[2], ri.address.rgBytes[1], ri.address.rgBytes[0]);
            b.adapter_address = knst_c16string(addr);
            b.powered = true;   // API doesn't expose this; assume on
        }

        BLUETOOTH_DEVICE_SEARCH_PARAMS sp = {};
        sp.dwSize = sizeof(sp);
        sp.fReturnAuthenticated = TRUE;
        sp.fReturnRemembered   = TRUE;
        sp.fReturnConnected    = TRUE;
        sp.fReturnUnknown      = FALSE;
        sp.fIssueInquiry       = FALSE;
        sp.cTimeoutMultiplier  = 1;
        sp.hRadio              = hRadio;

        BLUETOOTH_DEVICE_INFO di = {};
        di.dwSize = sizeof(di);
        HBLUETOOTH_DEVICE_FIND hDev = ::BluetoothFindFirstDevice(&sp, &di);

        if (hDev) {
            do {
                knst_bluetooth_device d;
                d.name = knst_c16string(reinterpret_cast<const char16_t*>(di.szName));

                char addr[32];
                ::snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X",
                    di.Address.rgBytes[5], di.Address.rgBytes[4], di.Address.rgBytes[3],
                    di.Address.rgBytes[2], di.Address.rgBytes[1], di.Address.rgBytes[0]);
                d.address = knst_c16string(addr);

                d.connected = di.fConnected     != FALSE;
                d.paired    = di.fAuthenticated != FALSE;
                d.trusted   = di.fRemembered    != FALSE;
                d.valid     = true;

                b.devices.push_back(d);
            } while (::BluetoothFindNextDevice(hDev, &di));
            ::BluetoothFindDeviceClose(hDev);
        }

        ::CloseHandle(hRadio);
    } while (::BluetoothFindNextRadio(hFind, &hRadio));

    ::BluetoothFindRadioClose(hFind);

    b.valid = true;
    return b;
}
inline knst_vector<knst_camera_info> cameras() noexcept {
    knst_vector<knst_camera_info> out;


    auto data = detail::wmi_query("Win32_PnPEntity",
        "Name,DeviceID,Manufacturer,PNPClass,Service",
        "PNPClass='Camera' OR Service='usbvideo'",
        3000);

    if (!data.empty()) {
        const char* p = (const char*)data.data();
        const char* e1 = ::strchr(p, '\n');
        if (e1) p = e1 + 1;
        while (p && *p) {
            const char* e = ::strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : ::strlen(p);
            char line[2048];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = '\0';

                        auto row = detail::parse_csv(line);
            if (row.count >= 3) {

                knst_c16string cam_name(row.cols[0]);
                knst_c16string cam_path(row.cols[1]);

  
                auto contains_ci = [](const knst_c16string& hay, const char16_t* needle) -> bool {
                    if (!needle) return false;
                    uint32_t nlen = 0;
                    while (needle[nlen]) ++nlen;
                    if (nlen == 0 || hay.length() < nlen) return false;

                    const char16_t* h = hay.data();
                    uint32_t hlen = hay.length();
                    for (uint32_t i = 0; i + nlen <= hlen; ++i) {
                        bool match = true;
                        for (uint32_t j = 0; j < nlen; ++j) {
                            char16_t a = h[i + j];
                            char16_t b = needle[j];
                            if (a >= u'A' && a <= u'Z') a = (char16_t)(a + 32);
                            if (b >= u'A' && b <= u'Z') b = (char16_t)(b + 32);
                            if (a != b) { match = false; break; }
                        }
                        if (match) return true;
                    }
                    return false;
                };

                bool is_scanner = false;
                if (contains_ci(cam_name, u"scan")
                 || contains_ci(cam_name, u"escl")
                 || contains_ci(cam_name, u"print")
                 || contains_ci(cam_name, u"officejet")
                 || contains_ci(cam_name, u"laserjet")
                 || contains_ci(cam_name, u"deskjet")) {
                    is_scanner = true;
                }
                if (contains_ci(cam_path, u"escl")
                 || contains_ci(cam_path, u"usbscan")) {
                    is_scanner = true;
                }
                if (is_scanner) {
                    if (!e) break;
                    p = e + 1;
                    continue;
                }

                knst_camera_info c;
                c.name = cam_name;
                c.device_path = cam_path;
                c.valid = true;


                const char* vid_ptr = ::strstr(row.cols[1], "VID_");
                if (vid_ptr) {
                    unsigned v;
                    if (::sscanf(vid_ptr, "VID_%04X", &v) == 1) c.vid = (uint16_t)v;
                }
                const char* pid_ptr = ::strstr(row.cols[1], "PID_");
                if (pid_ptr) {
                    unsigned v;
                    if (::sscanf(pid_ptr, "PID_%04X", &v) == 1) c.pid = (uint16_t)v;
                }


                out.push_back(c);
            }
            if (!e) break;
            p = e + 1;
        }
    }

    return out;
}

} // namespace knst_devices

#endif // KNST_USING_PLATFORM_WINDOWS



namespace knst_devices {

inline knst_device_error last_error() noexcept {
    return knst_device_error::None;
}



inline knst_vector<knst_gpu_info> gpus() noexcept {
    return knst_gpu::list_all();
}

inline knst_vector<knst_gpu_process> gpu_processes() noexcept {
    return knst_gpu::get_processes();
}

inline knst_vector<knst_gpu_process> gpu_processes(uint32_t idx) noexcept {
    return knst_gpu::get_processes(idx);
}

inline uint32_t gpu_count() noexcept {
    return knst_gpu::count();
}

inline bool has_gpu() noexcept {
    return knst_gpu::has_gpu();
}

inline knst_c16string nvidia_driver_version() noexcept {
    return knst_gpu::nvidia_driver_version();
}

inline knst_c16string cuda_version() noexcept {
    return knst_gpu::cuda_version();
}

} // namespace knst_devices