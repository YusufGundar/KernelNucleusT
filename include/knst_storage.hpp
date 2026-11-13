// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_storage.hpp
----------------------------
    Comprehensive storage controller. Production-ready.
*/

#pragma once

#include <chrono>
#include <map>
#include <algorithm>

#if KNST_USING_PLATFORM_WINDOWS
    #include <winioctl.h>
#elif KNST_USING_PLATFORM_LINUX
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/statvfs.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <stdlib.h>
#endif

namespace knst_sto {

using knst_dev::status;
using knst_dev::result;

// ============================================================
// Common types
// ============================================================

struct space_info {
    uint64_t free_bytes   = 0;
    uint64_t total_bytes  = 0;
    uint64_t used_bytes   = 0;
    double   used_percent = 0.0;
};

struct smart_report {
    bool available = false;
    bool passed = false;
    uint32_t power_on_hours  = 0;
    uint32_t reallocated     = 0;
    uint32_t pending         = 0;
    uint32_t percent_used    = 0;
    int32_t  temperature_c   = 0;
    uint64_t bytes_written   = 0;
    uint64_t bytes_read      = 0;
    knst_c16string model;
    knst_c16string firmware;
};

struct disk_identity {
    knst_c16string serial;
    knst_c16string wwid;
    knst_c16string model;
    knst_c16string vendor;
    knst_c16string firmware;
    knst_c16string partition_table;
    knst_c16string fs_type;
    knst_c16string fs_uuid;
    knst_c16string fs_label;
    knst_c16string iface;
    uint32_t rotational_rpm = 0;
    uint64_t size_bytes = 0;
    uint64_t sector_size = 512;
    uint64_t physical_sector_size = 4096;
    bool is_ssd = false;
    bool is_nvme = false;
    bool is_encrypted = false;
    bool is_boot_disk = false;
    bool has_trim_support = false;
    bool valid = false;
};

struct health_snapshot {
    bool healthy = false;
    bool temperature_warning = false;
    bool pending_sector_warning = false;
    bool reallocated_warning  = false;
    bool wear_warning = false;
    double score  = 100.0;
    knst_vector<knst_c16string> warnings;
};

struct mount_entry {
    knst_c16string device;
    knst_c16string mount_point;
    knst_c16string fs_type;
    knst_c16string options;
    bool is_read_only = false;
    bool is_network = false;
    bool is_bind  = false;
    bool is_virtual = false;
    bool valid  = false;
};

struct fs_usage {
    knst_c16string device;
    knst_c16string mount_point;
    knst_c16string fs_type;
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
    uint64_t free_bytes = 0;
    double  used_percent = 0.0;
};

struct dir_size {
    uint64_t total_bytes = 0;
    uint64_t file_count = 0;
    uint64_t dir_count = 0;
    uint32_t max_depth = 0;
    bool valid  = false;
};

struct file_entry {
    knst_c16string path;
    uint64_t size = 0;
    uint64_t modified_time = 0;
};

struct io_stats {
    uint64_t reads_completed  = 0;
    uint64_t reads_merged = 0;
    uint64_t sectors_read = 0;
    uint64_t read_ticks_ms = 0;
    uint64_t writes_completed = 0;
    uint64_t writes_merged = 0;
    uint64_t sectors_written  = 0;
    uint64_t write_ticks_ms = 0;
    uint64_t io_in_progress = 0;
    uint64_t io_ticks_ms = 0;
    uint64_t weighted_io_ms = 0;
    bool valid = false;
};

struct speed_result {
    double   mb_per_sec     = 0.0;
    double   iops           = 0.0;
    double   latency_ms     = 0.0;
    uint64_t bytes          = 0;
    uint32_t duration_ms    = 0;
    bool     cache_bypassed = false;
    bool     valid          = false;
};

struct luks_container {
    knst_c16string device;
    knst_c16string mapper_name;
    knst_c16string cipher;
    knst_c16string uuid;
    uint32_t key_size = 0;
    bool is_unlocked  = false;
    bool valid        = false;
};

struct raid_array {
    knst_c16string name;
    knst_c16string level;
    knst_c16string state;
    uint32_t total_devices  = 0;
    uint32_t active_devices = 0;
    uint32_t failed_devices = 0;
    knst_vector<knst_c16string> members;
    bool valid = false;
};

struct lvm_volume {
    knst_c16string name;
    knst_c16string vg_name;
    knst_c16string type;
    uint64_t size_bytes = 0;
    bool is_active = false;
    bool valid = false;
};

struct bitlocker_status {
    knst_c16string drive;
    knst_c16string method;
    bool is_encrypted  = false;
    bool is_protected  = false;
    bool is_locked     = false;
    uint32_t percentage = 0;
    bool valid = false;
};

// ============================================================
// Helpers
// ============================================================

inline uint32_t current_pid() noexcept {
#if KNST_USING_PLATFORM_WINDOWS
    return static_cast<uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<uint32_t>(::getpid());
#endif
}

inline uint64_t now_ms() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

inline void build_health(const smart_report& s, health_snapshot& h) noexcept {
    h.score = 100.0;

    if (!s.available) {
        h.warnings.push_back(u"SMART not available");
        h.healthy = false;
        return;
    }
    if (!s.passed) {
        h.score -= 50.0;
        h.warnings.push_back(u"SMART self-assessment FAILED");
    }
    if (s.temperature_c > 55) {
        h.score -= 15.0;
        h.temperature_warning = true;
        h.warnings.push_back(u"High temperature: >55 °C");
    }
    if (s.reallocated > 0) {
        h.score -= 20.0;
        h.reallocated_warning = true;
        h.warnings.push_back(u"Reallocated sectors detected");
    }
    if (s.pending > 0) {
        h.score -= 30.0;
        h.pending_sector_warning = true;
        h.warnings.push_back(u"Pending sectors — possible failure");
    }
    if (s.percent_used > 80) {
        h.score -= 10.0;
        h.wear_warning = true;
        h.warnings.push_back(u"SSD wear >80%");
    }

    if (h.score < 0.0) h.score = 0.0;
    h.healthy = h.score >= 70.0;
}

// ============================================================
// Cross-platform: free space
// ============================================================

inline result<space_info> free_space(const knst_c16string& path) noexcept {
    using R = result<space_info>;
    R r;
    if (!knst_file::get_disk_space(path, r.value.free_bytes, r.value.total_bytes))
        return R::fail(status::NotFound);
    r.value.used_bytes = r.value.total_bytes - r.value.free_bytes;
    r.value.used_percent = r.value.total_bytes
        ? 100.0 * static_cast<double>(r.value.used_bytes) / static_cast<double>(r.value.total_bytes)
        : 0.0;
    return r;
}

// ============================================================
// Cross-platform: directory_size / largest_files
// ============================================================

namespace detail {

inline void walk_dir_size(const knst_c16string& path, dir_size& ds, uint32_t depth) noexcept {
    if (depth > 64) return;

#if KNST_USING_PLATFORM_LINUX
    knst_byte_string path_u8 = knst_dev::to_utf8(path);
    const char* dir_path = knst_dev::text(path_u8);
    DIR* d = ::opendir(dir_path);
    if (!d) return;

    struct dirent* e;
    char full[4096];
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        int n = ::snprintf(full, sizeof(full), "%s/%s", dir_path, e->d_name);
        if (n <= 0 || n >= static_cast<int>(sizeof(full))) continue;

        struct stat st;
        if (::lstat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            ds.dir_count++;
            if (depth + 1 > ds.max_depth) ds.max_depth = depth + 1;
            walk_dir_size(knst_c16string(full), ds, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            ds.file_count++;
            ds.total_bytes += static_cast<uint64_t>(st.st_size);
        }
    }
    ::closedir(d);
#else
    knst_c16string search = path;
    search.append(u"\\*");
    wchar_t wpath[1024];
    uint32_t len = search.length();
    if (len == 0 || len >= 1024) return;
    for (uint32_t i = 0; i < len; ++i) wpath[i] = static_cast<wchar_t>(search[i]);
    wpath[len] = L'\0';

    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW(wpath, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == L'.') continue;
        knst_c16string full = path;
        full.append(u"\\");
        for (int i = 0; fd.cFileName[i]; ++i)
            full += static_cast<char16_t>(fd.cFileName[i]);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ds.dir_count++;
            if (depth + 1 > ds.max_depth) ds.max_depth = depth + 1;
            walk_dir_size(full, ds, depth + 1);
        } else {
            ds.file_count++;
            uint64_t sz = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32)
                        | static_cast<uint64_t>(fd.nFileSizeLow);
            ds.total_bytes += sz;
        }
    } while (::FindNextFileW(h, &fd));

    ::FindClose(h);
#endif
}

inline void walk_collect_files(const knst_c16string& path,
                                knst_vector<file_entry>& out,
                                uint32_t depth) noexcept {
    if (depth > 64) return;

#if KNST_USING_PLATFORM_LINUX
    knst_byte_string path_u8 = knst_dev::to_utf8(path);
    const char* dir_path = knst_dev::text(path_u8);
    DIR* d = ::opendir(dir_path);
    if (!d) return;

    struct dirent* e;
    char full[4096];
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        int n = ::snprintf(full, sizeof(full), "%s/%s", dir_path, e->d_name);
        if (n <= 0 || n >= static_cast<int>(sizeof(full))) continue;

        struct stat st;
        if (::lstat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            walk_collect_files(knst_c16string(full), out, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            file_entry fe;
            fe.path = knst_c16string(full);
            fe.size = static_cast<uint64_t>(st.st_size);
            fe.modified_time = static_cast<uint64_t>(st.st_mtime);
            out.push_back(std::move(fe));
        }
    }
    ::closedir(d);
#else
    knst_c16string search = path;
    search.append(u"\\*");
    wchar_t wpath[1024];
    uint32_t len = search.length();
    if (len == 0 || len >= 1024) return;
    for (uint32_t i = 0; i < len; ++i) wpath[i] = static_cast<wchar_t>(search[i]);
    wpath[len] = L'\0';

    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW(wpath, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == L'.') continue;
        knst_c16string full = path;
        full.append(u"\\");
        for (int i = 0; fd.cFileName[i]; ++i)
            full += static_cast<char16_t>(fd.cFileName[i]);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            walk_collect_files(full, out, depth + 1);
        } else {
            file_entry fe;
            fe.path = full;
            fe.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32)
                    | static_cast<uint64_t>(fd.nFileSizeLow);
            uint64_t ft = (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32)
                        | static_cast<uint64_t>(fd.ftLastWriteTime.dwLowDateTime);
            fe.modified_time = (ft / 10000000ULL) - 11644473600ULL;
            out.push_back(std::move(fe));
        }
    } while (::FindNextFileW(h, &fd));

    ::FindClose(h);
#endif
}

} // namespace detail

inline result<dir_size> directory_size(const knst_c16string& path) noexcept {
    using R = result<dir_size>;
    if (!knst_file::dir_exists(path)) return R::fail(status::NotFound);
    dir_size ds;
    detail::walk_dir_size(path, ds, 0);
    ds.valid = true;
    return R::success(std::move(ds));
}

inline result<knst_vector<file_entry>> largest_files(const knst_c16string& path,
                                                      uint32_t count = 20) noexcept {
    using R = result<knst_vector<file_entry>>;
    if (!knst_file::dir_exists(path)) return R::fail(status::NotFound);
    if (count == 0 || count > 10000) return R::fail(status::InvalidArgument);

    knst_vector<file_entry> all;
    detail::walk_collect_files(path, all, 0);

    for (uint32_t i = 1; i < all.size(); ++i) {
        file_entry key = all[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && all[j].size < key.size) {
            all[j + 1] = all[j];
            --j;
        }
        all[j + 1] = key;
    }

    knst_vector<file_entry> out;
    uint32_t limit = all.size() < count ? all.size() : count;
    out.reserve(limit);
    for (uint32_t i = 0; i < limit; ++i)
        out.push_back(std::move(all[i]));

    return R::success(std::move(out));
}

// ============================================================
// Linux implementation
// ============================================================

#if KNST_USING_PLATFORM_LINUX && !defined(KNST_USING_PLATFORM_ANDROID)

namespace detail_linux {

inline result<knst_c16string> read_small_file(const char* path) noexcept {
    using R = result<knst_c16string>;
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return R::fail(knst_dev::map_errno(errno), errno);
    char buf[1024];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n < 0) return R::fail(knst_dev::map_errno(errno), errno);
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
        buf[--n] = 0;
    return R::success(knst_c16string(buf));
}

// NVMe temperature via /sys/block/<dev>/device/hwmon/hwmonN/temp1_input
inline int32_t nvme_temperature_fallback(const char* devname) noexcept {
    char p[256];
    ::snprintf(p, sizeof(p), "/sys/block/%s/device/hwmon", devname);
    DIR* d = ::opendir(p);
    if (!d) return 0;
    int32_t result = 0;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        char tp[320];
        int64_t tv = 0;
        ::snprintf(tp, sizeof(tp), "%s/%s/temp1_input", p, e->d_name);
        if (knst_devices::detail::read_file_i64(tp, tv) && tv > 0) {
            result = static_cast<int32_t>(tv / 1000);
            break;
        }
    }
    ::closedir(d);
    return result;
}

// Parse KEY="VALUE" from lsblk -P output
inline void extract_pairs_kv(const char* src, const char* key,
                              char* dst, size_t dst_sz) noexcept {
    dst[0] = 0;
    char pattern[32];
    ::snprintf(pattern, sizeof(pattern), "%s=\"", key);
    const char* p = ::strstr(src, pattern);
    if (!p) return;
    p += ::strlen(pattern);
    const char* end = ::strchr(p, '"');
    if (!end) return;
    size_t n = static_cast<size_t>(end - p);
    if (n >= dst_sz) n = dst_sz - 1;
    ::memcpy(dst, p, n);
    dst[n] = 0;
}

// lsblk -P fallback for fs_type/fs_uuid when partitions aren't mounted
inline void lsblk_fs_info(const char* devname, disk_identity& id) noexcept {
    knst_dev::argv a;
    a << "-Pno" << "NAME,FSTYPE,UUID,LABEL" << devname;
    auto t = knst_dev::run_tool_raw("lsblk", a, 3000);
    if (!t.launched || t.exit_code != 0) return;

    knst_dev::for_each_line(knst_dev::text(t.out), [&](const char* line, size_t len) {
        char b[512];
        if (len >= sizeof(b)) len = sizeof(b) - 1;
        ::memcpy(b, line, len); b[len] = 0;

        char fstype[64] = {0};
        char uuid[128]  = {0};
        char label[128] = {0};

        extract_pairs_kv(b, "FSTYPE", fstype, sizeof(fstype));
        if (fstype[0] == '\0') return;   // no filesystem → skip

        extract_pairs_kv(b, "UUID",  uuid,  sizeof(uuid));
        extract_pairs_kv(b, "LABEL", label, sizeof(label));

        if (id.fs_type.empty())
            id.fs_type = knst_c16string(fstype);
        if (id.fs_uuid.empty() && uuid[0] != '\0')
            id.fs_uuid = knst_c16string(uuid);
        if (id.fs_label.empty() && label[0] != '\0')
            id.fs_label = knst_c16string(label);
    });
}

// Last-resort: blkid per-partition (used only if lsblk fails)
inline bool blkid_partition(const knst_c16string& part_dev,
                             knst_c16string& fs_type, knst_c16string& fs_uuid) noexcept {
    knst_byte_string pdev = knst_dev::to_utf8(part_dev);
    if (!knst_dev::is_dev_path(part_dev)) return false;

    knst_dev::argv a;
    a << "-o" << "value" << "-s" << "TYPE" << part_dev;
    auto t = knst_dev::run_tool_raw("blkid", a, 1000);
    if (!t.launched || t.exit_code != 0 || t.out.empty()) return false;

    knst_byte_string s = t.out;
    while (!s.empty() && (s[s.length() - 1] == '\n' || s[s.length() - 1] == '\r'))
        s.resize(s.length() - 1);
    fs_type = knst_c16string(knst_dev::text(s));

    a = knst_dev::argv();
    a << "-o" << "value" << "-s" << "UUID" << part_dev;
    t = knst_dev::run_tool_raw("blkid", a, 1000);
    if (t.launched && t.exit_code == 0 && !t.out.empty()) {
        knst_byte_string su = t.out;
        while (!su.empty() && (su[su.length() - 1] == '\n' || su[su.length() - 1] == '\r'))
            su.resize(su.length() - 1);
        fs_uuid = knst_c16string(knst_dev::text(su));
    }
    return true;
}

// Parse NVMe Data Units (1 unit = 512,000 bytes per spec)
inline bool parse_nvme_data_units(const char* line, uint64_t& bytes) noexcept {
    const char* colon = ::strchr(line, ':');
    if (!colon) return false;
    ++colon;
    while (*colon == ' ' || *colon == '\t') ++colon;

    char digits[32] = {0};
    size_t j = 0;
    for (const char* p = colon; *p && j < 31; ++p) {
        if (*p >= '0' && *p <= '9') {
            digits[j++] = *p;
        } else if (*p == ',' || *p == ' ' || *p == '\t') {
            continue;
        } else {
            break;
        }
    }
    digits[j] = 0;
    if (j == 0) return false;

    uint64_t units = ::strtoull(digits, nullptr, 10);
    bytes = units * 512000ULL;
    return true;
}

} // namespace detail_linux

class storage {
public:
    // ---------- mount / unmount / eject ----------

    static result<knst_c16string> mount(const knst_c16string& block_dev) noexcept {
        using R = result<knst_c16string>;
        if (!knst_dev::is_dev_path(block_dev)) return R::fail(status::InvalidArgument);
        knst_dev::argv a; a << "mount" << "-b" << block_dev << "--no-user-interaction";
        auto out = knst_dev::run("udisksctl", a, 20000);
        if (!out) return R::fail(out.st, out.native);
        const char* at = ::strstr(knst_dev::text(out.value), " at ");
        if (!at) return R::fail(status::Unknown);
        at += 4;
        char mp[512]; size_t n = 0;
        while (at[n] && at[n] != '\n' && at[n] != '\r' && n < sizeof(mp) - 1) ++n;
        if (n > 0 && at[n - 1] == '.') --n;
        ::memcpy(mp, at, n); mp[n] = 0;
        return R::success(knst_c16string(mp));
    }

    static result<void> unmount(const knst_c16string& block_dev) noexcept {
        if (!knst_dev::is_dev_path(block_dev)) return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a; a << "unmount" << "-b" << block_dev << "--no-user-interaction";
        return knst_dev::drop(knst_dev::run("udisksctl", a, 20000));
    }

    static result<void> eject(const knst_storage_info& disk) noexcept {
        if (!disk.is_removable || !knst_dev::is_dev_path(disk.device))
            return result<void>::fail(status::InvalidArgument);
        for (uint32_t i = 0; i < disk.partitions.size(); ++i) {
            const auto& p = disk.partitions[i];
            if (p.is_boot) return result<void>::fail(status::BadState);
            if (knst_dev::is_dev_path(p.device)) {
                auto u = unmount(p.device);
                if (!u && u.st != status::IoError) return u;
            }
        }
        knst_dev::argv a; a << "power-off" << "-b" << disk.device << "--no-user-interaction";
        return knst_dev::drop(knst_dev::run("udisksctl", a, 20000));
    }

    // ---------- SMART (ATA + NVMe) ----------

    static result<smart_report> smart(const knst_storage_info& disk) noexcept {
        using R = result<smart_report>;
        if (!knst_dev::is_dev_path(disk.device)) return R::fail(status::InvalidArgument);

        bool is_nvme = (disk.type == knst_storage_type::NVMe);
        knst_dev::argv a; a << "-H" << "-A" << "-i";
        if (is_nvme) a << "-d" << "nvme";
        a << disk.device;
        auto t = knst_dev::run_tool_raw("smartctl", a, 15000);
        if (!t.launched) return R::fail(status::ToolMissing);
        if (t.exit_code & 0x03)
            return R::fail((t.exit_code & 0x02) ? status::PermissionDenied : status::IoError, t.exit_code);

        R r;
        r.value.available = true;

        knst_dev::for_each_line(knst_dev::text(t.out), [&](const char* line, size_t len) {
            char b[512];
            if (len >= sizeof(b)) len = sizeof(b) - 1;
            ::memcpy(b, line, len); b[len] = 0;

            const char* v = nullptr;

            if (::strstr(b, "overall-health self-assessment") && ::strstr(b, "PASSED"))
                r.value.passed = true;
            else if (::strstr(b, "SMART Health Status: OK"))
                r.value.passed = true;
            else if (::strstr(b, "SMART overall-health self-assessment test result: PASSED"))
                r.value.passed = true;

            // NVMe-specific data-unit fields
            else if (::strncmp(b, "Data Units Written:", 19) == 0) {
                uint64_t bytes = 0;
                if (detail_linux::parse_nvme_data_units(b, bytes))
                    r.value.bytes_written = bytes;
            }
            else if (::strncmp(b, "Data Units Read:", 16) == 0) {
                uint64_t bytes = 0;
                if (detail_linux::parse_nvme_data_units(b, bytes))
                    r.value.bytes_read = bytes;
            }

            // Generic fields
            else if (knst_devices::detail::split_kv(b, "Device Model", v) ||
                     knst_devices::detail::split_kv(b, "Model Number", v))
                r.value.model = knst_c16string(v);
            else if (knst_devices::detail::split_kv(b, "Firmware Version", v))
                r.value.firmware = knst_c16string(v);
            else if (knst_devices::detail::split_kv(b, "Percentage Used", v))
                r.value.percent_used = static_cast<uint32_t>(::atoi(v));
            else if (knst_devices::detail::split_kv(b, "Power On Hours", v)) {
                char tmp[32]; size_t j = 0;
                for (size_t i = 0; v[i] && j < 31; ++i)
                    if (v[i] != ',' && v[i] != ' ') tmp[j++] = v[i];
                tmp[j] = 0;
                r.value.power_on_hours = static_cast<uint32_t>(::strtoul(tmp, nullptr, 10));
            }
            else if (knst_devices::detail::split_kv(b, "Temperature", v)) {
                int32_t temp = static_cast<int32_t>(::atoi(v));
                if (temp > 0 && temp < 150 && r.value.temperature_c == 0)
                    r.value.temperature_c = temp;
            }
            else {
                // ATA attribute table row: ID NAME FLAG VALUE WORST THRESH TYPE UPDATED WHEN_FAILED RAW_VALUE
                unsigned id = 0; char name[64] = {0}; char rest[256] = {0};
                if (::sscanf(b, " %u %63s %255[^\n]", &id, name, rest) == 3) {
                    const char* tok = rest; int skip = 0;
                    while (*tok && skip < 7) {
                        while (*tok == ' ') ++tok;
                        while (*tok && *tok != ' ') ++tok;
                        ++skip;
                    }
                    while (*tok == ' ') ++tok;
                    unsigned long long raw = ::strtoull(tok, nullptr, 10);
                    if (id == 5)         r.value.reallocated    = static_cast<uint32_t>(raw);
                    else if (id == 9)    r.value.power_on_hours = static_cast<uint32_t>(raw);
                    else if (id == 197)  r.value.pending        = static_cast<uint32_t>(raw);
                    else if (id == 194 && r.value.temperature_c == 0)
                        r.value.temperature_c = static_cast<int32_t>(raw & 0xFF);
                    else if (id == 241)  r.value.bytes_written  = raw * 512ull;
                }
            }
        });

        // NVMe temperature fallback via sysfs hwmon
        if (r.value.temperature_c == 0) {
            knst_byte_string u8 = knst_dev::to_utf8(disk.device);
            const char* base = ::strrchr(knst_dev::text(u8), '/');
            if (base) {
                int32_t nvme_temp = detail_linux::nvme_temperature_fallback(base + 1);
                if (nvme_temp > 0) r.value.temperature_c = nvme_temp;
            }
        }

        return r;
    }

    static result<void> apply_smart(knst_storage_info& disk) noexcept {
        auto s = smart(disk);
        if (!s) return result<void>::fail(s.st, s.native);
        disk.has_smart        = true;
        disk.smart_passed     = s.value.passed;
        disk.smart_power_on_hours      = s.value.power_on_hours;
        disk.smart_reallocated_sectors = s.value.reallocated;
        disk.smart_pending_sectors     = s.value.pending;
        disk.smart_percent_used        = s.value.percent_used;
        disk.smart_bytes_written       = s.value.bytes_written;
        if (s.value.temperature_c) {
            disk.temperature_c = s.value.temperature_c;
            disk.has_temperature = true;
        }
        if (!s.value.firmware.empty()) disk.firmware = s.value.firmware;
        return result<void>::success();
    }

    // ---------- health ----------

    static result<health_snapshot> health_check(const knst_storage_info& disk) noexcept {
        using R = result<health_snapshot>;
        auto s = smart(disk);
        if (!s) return R::fail(s.st, s.native);
        health_snapshot h;
        build_health(s.value, h);
        return R::success(std::move(h));
    }

    // ---------- identity ----------

    static result<disk_identity> identity(const knst_storage_info& disk) noexcept {
        using R = result<disk_identity>;
        if (!knst_dev::is_dev_path(disk.device)) return R::fail(status::InvalidArgument);

        disk_identity id;
        id.model    = disk.model;
        id.vendor   = disk.vendor;
        id.firmware = disk.firmware;
        id.serial   = disk.serial;
        id.size_bytes = disk.total_bytes;
        id.is_ssd     = disk.is_solid_state;
        id.is_nvme    = (disk.type == knst_storage_type::NVMe);

        // /sys/block/<dev>/...
        knst_byte_string dev_u8 = knst_dev::to_utf8(disk.device);
        const char* devname = ::strrchr(knst_dev::text(dev_u8), '/');
        if (devname) {
            ++devname;
            char p[256];
            ::snprintf(p, sizeof(p), "/sys/block/%s/queue/rotational", devname);
            uint64_t v = 0;
            if (knst_devices::detail::read_file_u64(p, v))
                id.rotational_rpm = v ? 7200 : 0;

            ::snprintf(p, sizeof(p), "/sys/block/%s/queue/logical_block_size", devname);
            if (knst_devices::detail::read_file_u64(p, v)) id.sector_size = v;

            ::snprintf(p, sizeof(p), "/sys/block/%s/queue/physical_block_size", devname);
            if (knst_devices::detail::read_file_u64(p, v)) id.physical_sector_size = v;

            ::snprintf(p, sizeof(p), "/sys/block/%s/queue/discard_max_bytes", devname);
            if (knst_devices::detail::read_file_u64(p, v)) id.has_trim_support = (v > 0);

            ::snprintf(p, sizeof(p), "/sys/block/%s/device/wwid", devname);
            char wwid[128] = {0};
            if (knst_devices::detail::read_file(p, wwid, sizeof(wwid)))
                id.wwid = knst_c16string(wwid);
        }

        // Partition table:
        //   1) blkid -p does a RAW probe (authoritative, but needs root/disk grp)
        //   2) lsblk -dno PTTYPE reads the udev cache (world-readable, works as user)
        // Only fall back to lsblk when blkid produced nothing.
        {
            knst_dev::argv a;
            a << "-p" << "-o" << "value" << "-s" << "PTTYPE" << disk.device;
            auto t = knst_dev::run_tool_raw("blkid", a, 2000);
            if (t.launched && t.exit_code == 0 && !t.out.empty()) {
                knst_byte_string s = t.out;
                while (!s.empty() && (s[s.length() - 1] == '\n' || s[s.length() - 1] == '\r'))
                    s.resize(s.length() - 1);
                id.partition_table = knst_c16string(knst_dev::text(s));
            }

            if (id.partition_table.empty()) {
                knst_byte_string dev_u8b = knst_dev::to_utf8(disk.device);
                const char* base = ::strrchr(knst_dev::text(dev_u8b), '/');
                if (base) {
                    knst_dev::argv a2;
                    a2 << "-dno" << "PTTYPE" << knst_c16string(base + 1);
                    auto t2 = knst_dev::run_tool_raw("lsblk", a2, 2000);
                    if (t2.launched && t2.exit_code == 0 && !t2.out.empty()) {
                        knst_byte_string s2 = t2.out;
                        while (!s2.empty() &&
                               (s2[s2.length() - 1] == '\n' || s2[s2.length() - 1] == '\r'))
                            s2.resize(s2.length() - 1);
                        const char* pt = knst_dev::text(s2);
                        if (pt && *pt)
                            id.partition_table = knst_c16string(pt);
                    }
                }
            }
        }

        // blkid on the first mounted partition (if any)
        if (!disk.partitions.empty() && knst_dev::is_dev_path(disk.partitions[0].device)) {
            knst_dev::argv a;
            a << "-o" << "value" << "-s" << "TYPE" << disk.partitions[0].device;
            auto t = knst_dev::run_tool_raw("blkid", a, 2000);
            if (t.launched && t.exit_code == 0 && !t.out.empty()) {
                knst_byte_string s = t.out;
                while (!s.empty() && (s[s.length() - 1] == '\n' || s[s.length() - 1] == '\r'))
                    s.resize(s.length() - 1);
                id.fs_type = knst_c16string(knst_dev::text(s));
            }
            a = knst_dev::argv(); a << "-o" << "value" << "-s" << "UUID" << disk.partitions[0].device;
            t = knst_dev::run_tool_raw("blkid", a, 2000);
            if (t.launched && t.exit_code == 0 && !t.out.empty()) {
                knst_byte_string s = t.out;
                while (!s.empty() && (s[s.length() - 1] == '\n' || s[s.length() - 1] == '\r'))
                    s.resize(s.length() - 1);
                id.fs_uuid = knst_c16string(knst_dev::text(s));
            }
        }

        // lsblk -P fallback (unmounted partitions like NTFS on NVMe)
        if (id.fs_type.empty() || id.fs_uuid.empty()) {
            knst_byte_string dev_u8b = knst_dev::to_utf8(disk.device);
            const char* base = ::strrchr(knst_dev::text(dev_u8b), '/');
            if (base) detail_linux::lsblk_fs_info(base + 1, id);
        }

        // Last-resort blkid per-partition (only if lsblk also failed)
if (id.fs_type.empty()) {
    // NVMe and mmcblk use /dev/nvme0n1p1, /dev/mmcblk0p1;
    // SATA/USB use /dev/sda1 (no 'p').
        const bool needs_p = (disk.type == knst_storage_type::NVMe)
                      || disk.device.contains(u"mmcblk")
                      || disk.device.contains(u"nvme");
    for (uint32_t p = 1; p <= 16; ++p) {
        knst_c16string part_dev = disk.device;
        if (needs_p) part_dev.append(u"p");
        part_dev.append(static_cast<unsigned int>(p));

                knst_c16string ft, fu;
                if (detail_linux::blkid_partition(part_dev, ft, fu)) {
                    id.fs_type = std::move(ft);
                    if (!fu.empty()) id.fs_uuid = std::move(fu);
                    break;
                }
            }
        }

        // Encryption: dm-crypt detection
        for (auto& p : disk.partitions) {
            knst_byte_string pb = knst_dev::to_utf8(p.device);
            const char* pc = knst_dev::text(pb);
            if (pc && ::strstr(pc, "/dev/mapper/")) {
                id.is_encrypted = true;
                break;
            }
        }

        // Boot disk?
        for (auto& p : disk.partitions) {
            if (p.is_boot) { id.is_boot_disk = true; break; }
        }

        id.valid = true;
        return R::success(std::move(id));
    }

    // ---------- io stats ----------

    static result<io_stats> io_statistics(const knst_storage_info& disk) noexcept {
        using R = result<io_stats>;
        if (!knst_dev::is_dev_path(disk.device)) return R::fail(status::InvalidArgument);

        knst_byte_string u8 = knst_dev::to_utf8(disk.device);
        const char* devname = ::strrchr(knst_dev::text(u8), '/');
        if (!devname) return R::fail(status::InvalidArgument);
        ++devname;

        char p[256];
        ::snprintf(p, sizeof(p), "/sys/block/%s/stat", devname);
        char buf[512];
        if (!knst_devices::detail::read_file(p, buf, sizeof(buf)))
            return R::fail(status::NotFound);

        io_stats s;
        unsigned long long a[17] = {0};
        int n = ::sscanf(buf,
            "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
            &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7],
            &a[8], &a[9], &a[10], &a[11], &a[12], &a[13], &a[14], &a[15], &a[16]);
        if (n < 11) return R::fail(status::IoError);

        s.reads_completed  = a[0];
        s.reads_merged     = a[1];
        s.sectors_read     = a[2];
        s.read_ticks_ms    = a[3];
        s.writes_completed = a[4];
        s.writes_merged    = a[5];
        s.sectors_written  = a[6];
        s.write_ticks_ms   = a[7];
        s.io_in_progress   = a[8];
        s.io_ticks_ms      = a[9];
        s.weighted_io_ms   = a[10];
        s.valid = true;
        return R::success(std::move(s));
    }

    // ---------- benchmarks ----------

    static result<speed_result> benchmark_write(const knst_c16string& dir_path,
                                                 uint64_t size_mb = 100) noexcept {
        using R = result<speed_result>;
        if (!knst_file::dir_exists(dir_path)) return R::fail(status::NotFound);
        if (size_mb == 0 || size_mb > 8192) return R::fail(status::InvalidArgument);

        knst_c16string temp = dir_path;
        temp.append(u"/.knst_bench_");
        temp.append(static_cast<unsigned int>(current_pid()));
        temp.append(u"_");
        temp.append(static_cast<unsigned int>(now_ms()));
        temp.append(u".tmp");

        knst_byte_string tp = knst_dev::to_utf8(temp);
        const char* tmp_path = knst_dev::text(tp);

        int fd = ::open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return R::fail(knst_dev::map_errno(errno), errno);

        const uint64_t total = size_mb * 1024ULL * 1024ULL;
        const uint32_t chunk = 1024 * 1024;
        knst_byte_string buf;
        buf.resize(chunk, 0xAB);

        auto t0 = std::chrono::steady_clock::now();
        uint64_t written = 0;
        bool ok = true;
        while (written < total) {
            uint32_t want = static_cast<uint32_t>(
                (total - written) < chunk ? (total - written) : chunk);
            ssize_t w = ::write(fd, buf.data(), want);
            if (w <= 0) { ok = false; break; }
            written += static_cast<uint64_t>(w);
        }
        if (ok) ::fsync(fd);
        ::close(fd);
        auto t1 = std::chrono::steady_clock::now();
        ::unlink(tmp_path);

        if (!ok) return R::fail(status::IoError);

        speed_result r;
        r.bytes = written;
        r.duration_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        if (r.duration_ms > 0) {
            r.mb_per_sec = static_cast<double>(written) / (1024.0 * 1024.0)
                         / (static_cast<double>(r.duration_ms) / 1000.0);
            r.iops = static_cast<double>(written / chunk)
                   / (static_cast<double>(r.duration_ms) / 1000.0);
        }
        r.cache_bypassed = false;
        r.valid = true;
        return R::success(std::move(r));
    }

    static result<speed_result> benchmark_read(const knst_c16string& file_path,
                                                uint64_t size_mb = 100,
                                                bool try_direct = true) noexcept {
        using R = result<speed_result>;
        if (!knst_file::file_exists(file_path)) return R::fail(status::NotFound);
        if (size_mb == 0 || size_mb > 8192) return R::fail(status::InvalidArgument);

        knst_byte_string fp = knst_dev::to_utf8(file_path);
        const char* path = knst_dev::text(fp);

        const uint64_t max_bytes = size_mb * 1024ULL * 1024ULL;
        const uint32_t chunk = 1024 * 1024;
        const uint32_t align = 4096;

        int fd = -1;
        void* raw = nullptr;
        bool direct = false;

        if (try_direct) {
            fd = ::open(path, O_RDONLY | O_DIRECT);
            if (fd >= 0) {
                if (posix_memalign(&raw, align, chunk) == 0 && raw) {
                    direct = true;
                } else {
                    ::close(fd);
                    fd = -1;
                }
            }
        }

        if (fd < 0) {
            fd = ::open(path, O_RDONLY);
            if (fd < 0) return R::fail(knst_dev::map_errno(errno), errno);
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        }

        knst_byte_string buf;
        unsigned char* buf_ptr = nullptr;
        if (!direct) {
            buf.resize(chunk, 0);
            buf_ptr = const_cast<unsigned char*>(buf.data());
        } else {
            buf_ptr = static_cast<unsigned char*>(raw);
        }

        auto t0 = std::chrono::steady_clock::now();
        uint64_t total_read = 0;
        while (total_read < max_bytes) {
            uint32_t want = static_cast<uint32_t>(
                (max_bytes - total_read) < chunk ? (max_bytes - total_read) : chunk);
            ssize_t n = ::read(fd, buf_ptr, want);
            if (n <= 0) break;
            total_read += static_cast<uint64_t>(n);
        }
        auto t1 = std::chrono::steady_clock::now();

        ::close(fd);
        if (raw) ::free(raw);

        if (total_read == 0) return R::fail(status::IoError);

        speed_result r;
        r.bytes = total_read;
        r.duration_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        if (r.duration_ms > 0) {
            r.mb_per_sec = static_cast<double>(total_read) / (1024.0 * 1024.0)
                         / (static_cast<double>(r.duration_ms) / 1000.0);
            r.iops = static_cast<double>(total_read / chunk)
                   / (static_cast<double>(r.duration_ms) / 1000.0);
        }
        r.cache_bypassed = direct;
        r.valid = true;
        return R::success(std::move(r));
    }

    // ---------- LUKS ----------

        static result<knst_vector<luks_container>> list_luks() noexcept {
        using R = result<knst_vector<luks_container>>;
        R r;

        // Approach: scan /dev/mapper for active dm-crypt devices,
        // use cryptsetup status to fill details, and fall back to
        // /etc/crypttab for configured-but-locked LUKS containers.

        DIR* d = ::opendir("/dev/mapper");
        if (!d) return R::fail(knst_dev::map_errno(errno), errno);

        struct dirent* e;
        while ((e = ::readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            if (::strcmp(e->d_name, "control") == 0) continue;

                        char p[512];
            ::snprintf(p, sizeof(p), "/dev/mapper/%s", e->d_name);
                        // Confirm this is really a dm-crypt device, not an LVM LV
            // or multipath map that also lives under /dev/mapper/.
            // The kernel exposes the true type in /sys/block/dm-N/dm/uuid:
            //   CRYPT-LUKS2-<uuid>-<name>   = LUKS2 container
            //   CRYPT-LUKS1-<uuid>-<name>   = LUKS1 container
            //   CRYPT-PLAIN-<name>          = plain dm-crypt
            //   LVM-<uuid>                  = LVM LV (NOT LUKS)
            //   mpath-<uuid>                = multipath (NOT LUKS)
            //
            // While we're here, also read the underlying slave device
            // from /sys/block/dm-N/slaves/. That directory is
            // world-readable (no root needed), so c.device can show
            // /dev/sda3 even when `cryptsetup status` is denied.
            knst_c16string underlying_device;
            {
                char link_buf[256] = {0};
                ssize_t lk = ::readlink(p, link_buf, sizeof(link_buf) - 1);
                if (lk > 0) {
                    link_buf[lk] = 0;
                    const char* base = ::strrchr(link_buf, '/');
                    if (!base) base = link_buf; else ++base;

                    char uuid_path[512];
                    ::snprintf(uuid_path, sizeof(uuid_path),
                               "/sys/block/%s/dm/uuid", base);
                    char uuid_buf[256] = {0};
                    if (knst_devices::detail::read_file(uuid_path, uuid_buf, sizeof(uuid_buf))) {
                        if (::strncmp(uuid_buf, "CRYPT-", 6) != 0) {
                            continue;   // LVM LV, mpath, etc. — not LUKS
                        }
                    }

                    // First (and usually only) slave = underlying block
                    // device. e.g. dm-2 → sda4, dm-0 → sda3.
                    char slaves_dir[512];
                    ::snprintf(slaves_dir, sizeof(slaves_dir),
                               "/sys/block/%s/slaves", base);
                    DIR* sd = ::opendir(slaves_dir);
                    if (sd) {
                        struct dirent* se;
                        while ((se = ::readdir(sd)) != nullptr) {
                            if (se->d_name[0] == '.') continue;
                            underlying_device = u"/dev/";
                            underlying_device.append(knst_c16string(se->d_name));
                            break;
                        }
                        ::closedir(sd);
                    }
                }
            }

                        // We already know this dm-* entry has a CRYPT- uuid via
            // the filter above. That alone proves it's an unlocked
            // LUKS container — an inactive dm-crypt device would not
            // appear under /dev/mapper/ at all. Try cryptsetup for
            // cipher/keysize detail (sudo-only), but never mark the
            // entry "LOCKED" just because cryptsetup couldn't run.
            knst_dev::argv a;
            a << "status" << knst_c16string(p);
            auto t = knst_dev::run_tool_raw("cryptsetup", a, 3000);
            bool have_details = (t.launched && t.exit_code == 0);

            luks_container c;
            c.mapper_name = knst_c16string(e->d_name);
            // Prefer the real underlying device (e.g. /dev/sda3) read
            // from /sys/block/dm-N/slaves/. Fall back to the mapper
            // path (/dev/mapper/cryptswap) only if sysfs is missing.
            // cryptsetup status (root only) can still override below.
            c.device      = underlying_device.empty()
                          ? knst_c16string(p)
                          : underlying_device;
            c.is_unlocked = true;
            c.valid       = true;

            // Parse cryptsetup status output:
            //   /dev/mapper/cryptswap is active and is in use.
            //   type:    LUKS2
            //   cipher:  aes-xts-plain64
            //   keysize: 512 bits
            //   device:  /dev/sda2
                        if (!have_details) {
                // cryptsetup status unavailable (no root). Still push
                // the container as unlocked — /dev/mapper/<name> only
                // exists when the dm-crypt device is active.
                r.value.push_back(std::move(c));
                continue;
            }
            knst_dev::for_each_line(knst_dev::text(t.out), [&](const char* line, size_t len) {
                char b[512];
                if (len >= sizeof(b)) len = sizeof(b) - 1;
                ::memcpy(b, line, len); b[len] = 0;

                char* content = b;
                while (*content == ' ' || *content == '\t') ++content;

                const char* v = nullptr;
                if (knst_devices::detail::split_kv(content, "cipher", v) ||
                    knst_devices::detail::split_kv(content, "Cipher", v)) {
                    char cipher[64] = {0};
                    ::sscanf(v, "%63s", cipher);
                    if (cipher[0]) c.cipher = knst_c16string(cipher);
                }
                else if (knst_devices::detail::split_kv(content, "keysize", v) ||
                         knst_devices::detail::split_kv(content, "Key size", v)) {
                    c.key_size = static_cast<uint32_t>(::atoi(v));
                }
                else if (knst_devices::detail::split_kv(content, "device", v) ||
                         knst_devices::detail::split_kv(content, "Device", v)) {
                    char dev[256] = {0};
                    ::sscanf(v, "%255s", dev);
                    if (dev[0]) c.device = knst_c16string(dev);
                }
                else if (knst_devices::detail::split_kv(content, "UUID", v)) {
                    char uuid[128] = {0};
                    ::sscanf(v, "%127s", uuid);
                    if (uuid[0]) c.uuid = knst_c16string(uuid);
                }
            });

            r.value.push_back(std::move(c));
        }
        ::closedir(d);

        // Fallback: /etc/crypttab for configured (possibly locked) LUKS
        char buf[4096];
        if (knst_devices::detail::read_file("/etc/crypttab", buf, sizeof(buf))) {
            knst_dev::for_each_line(buf, [&](const char* line, size_t len) {
                char b[512];
                if (len >= sizeof(b)) len = sizeof(b) - 1;
                ::memcpy(b, line, len); b[len] = 0;

                if (b[0] == '#' || b[0] == '\0' || b[0] == '\n') return;

                char name[128] = {0}, source[256] = {0}, keyfile[64] = {0}, opts[128] = {0};
                if (::sscanf(b, "%127s %255s %63s %127s", name, source, keyfile, opts) < 2) return;

                for (auto& c : r.value) {
                    if (c.mapper_name == knst_c16string(name)) return;
                }

                luks_container c;
                c.mapper_name = knst_c16string(name);
                c.device      = knst_c16string(source);
                c.is_unlocked = false;
                c.valid       = true;
                r.value.push_back(std::move(c));
            });
        }

        return r;
    }

    // ---------- RAID ----------

    static result<knst_vector<raid_array>> list_raid() noexcept {
        using R = result<knst_vector<raid_array>>;
        R r;
        char buf[4096];
        if (!knst_devices::detail::read_file("/proc/mdstat", buf, sizeof(buf)))
            return r;

        raid_array cur;
        knst_dev::for_each_line(buf, [&](const char* line, size_t len) {
            char b[512];
            if (len >= sizeof(b)) len = sizeof(b) - 1;
            ::memcpy(b, line, len); b[len] = 0;

char name[32], state[32], level[32];
if (::sscanf(b, "%31s : %31s %31s", name, state, level) == 3 &&
    ::strcmp(state, "active") == 0) {
                if (cur.valid) r.value.push_back(std::move(cur));
                cur = raid_array();
                cur.name  = knst_c16string(name);
                cur.level = knst_c16string(level);
                cur.valid = true;
            }
            else if (cur.valid && ::strstr(b, "[") && ::strstr(b, "blocks")) {
                const char* p = b;
                while ((p = ::strchr(p, '[')) != nullptr) {
                    const char* end = ::strchr(p, ']');
                    if (!end) break;
                    cur.members.push_back(knst_c16string(p + 1, static_cast<uint32_t>(end - p - 1)));
                    p = end + 1;
                }
                cur.total_devices = static_cast<uint32_t>(cur.members.size());
                cur.active_devices = cur.total_devices;
                cur.state = u"clean";
            }
        });
        if (cur.valid) r.value.push_back(std::move(cur));
        return r;
    }

    // ---------- LVM ----------

      static result<knst_vector<lvm_volume>> list_lvm() noexcept {
        using R = result<knst_vector<lvm_volume>>;
        R r;

        // 1) Try `lvs` with explicit separator. `--noheadings` alone is
        //    fragile because output alignment pads with spaces and any
        //    slight variance in column widths breaks the parser.
        {
            knst_dev::argv a;
            a << "lvs" << "--noheadings" << "--separator" << "|" << "-o"
              << "lv_name,vg_name,lv_size,lv_attr";
            auto t = knst_dev::run_tool_raw("lvs", a, 5000);

            if (t.launched && t.exit_code == 0 && !t.out.empty()) {
                knst_dev::for_each_line(knst_dev::text(t.out),
                    [&](const char* line, size_t len) {
                        char buf[512];
                        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
                        ::memcpy(buf, line, len); buf[len] = 0;

                        char* p = buf;
                        while (*p == ' ' || *p == '\t') ++p;
                        if (!*p) return;

                        // Split on '|'
                        char* fields[4] = {nullptr, nullptr, nullptr, nullptr};
                        int nf = 0;
                        char* seg = p;
                        while (*p && nf < 4) {
                            if (*p == '|') { *p = 0; fields[nf++] = seg; seg = p + 1; }
                            ++p;
                        }
                        if (nf < 4) fields[nf++] = seg;

                        if (nf < 3 || !fields[0] || !fields[0][0]) return;

                        lvm_volume v;
                        v.name    = knst_c16string(fields[0]);
                        v.vg_name = knst_c16string(fields[1]);

                        if (fields[2]) {
                            double val = 0.0;
                            char unit = 0;
                            if (::sscanf(fields[2], "%lf%c", &val, &unit) == 2) {
                                double mult = 1.0;
                                switch (unit) {
                                    case 'k': case 'K': mult = 1024.0; break;
                                    case 'm': case 'M': mult = 1024.0 * 1024.0; break;
                                    case 'g': case 'G': mult = 1024.0 * 1024.0 * 1024.0; break;
                                    case 't': case 'T': mult = 1024.0 * 1024.0 * 1024.0 * 1024.0; break;
                                    default: mult = 0.0; break;
                                }
                                v.size_bytes = static_cast<uint64_t>(val * mult);
                            }
                        }

                        if (nf >= 4 && fields[3] && ::strlen(fields[3]) > 4)
                            v.is_active = (fields[3][4] == 'a');
                        v.valid = true;
                        r.value.push_back(std::move(v));
                    });
            }
        }

        // 2) Fallback: if `lvs` returned nothing (PATH missing, metadata
        //    cache inaccessible, sudo edge cases), scan sysfs directly.
        //    /sys/block/dm-N/dm/uuid = "LVM-<uuid>" is the authoritative
        //    marker of an LVM LV. VG name isn't available in sysfs, so
        //    it stays empty — but name + size are enough for
        //    introspection, GUI display, and error messages.
        if (r.value.empty()) {
            DIR* d = ::opendir("/sys/block");
            if (d) {
                struct dirent* e;
                while ((e = ::readdir(d)) != nullptr) {
                    if (::strncmp(e->d_name, "dm-", 3) != 0) continue;

                    char uuid_path[256];
                    ::snprintf(uuid_path, sizeof(uuid_path),
                               "/sys/block/%s/dm/uuid", e->d_name);
                    char uuid_buf[256] = {0};
                    if (!knst_devices::detail::read_file(uuid_path, uuid_buf,
                                                         sizeof(uuid_buf)))
                        continue;
                    if (::strncmp(uuid_buf, "LVM-", 4) != 0) continue;

                    char name_path[256];
                    ::snprintf(name_path, sizeof(name_path),
                               "/sys/block/%s/dm/name", e->d_name);
                    char name_buf[128] = {0};
                    if (!knst_devices::detail::read_file(name_path, name_buf,
                                                         sizeof(name_buf)))
                        continue;

                    lvm_volume v;
                    v.name      = knst_c16string(name_buf);
                    v.is_active = true;

                    char size_path[256];
                    ::snprintf(size_path, sizeof(size_path),
                               "/sys/block/%s/size", e->d_name);
                    uint64_t sectors = 0;
                    if (knst_devices::detail::read_file_u64(size_path, sectors))
                        v.size_bytes = sectors * 512;

                    v.valid = true;
                    r.value.push_back(std::move(v));
                }
                ::closedir(d);
            }
        }

        return r;
    }
    // ---------- trim / wait ----------

    static result<void> trim(const knst_c16string& mount_point) noexcept {
        if (!knst_file::dir_exists(mount_point) || mount_point.length() > 400)
            return result<void>::fail(status::InvalidArgument);
        knst_dev::argv a; a << "-v" << knst_file::resolve_path(mount_point);
        return knst_dev::drop(knst_dev::run("fstrim", a, 600000));
    }

    static result<knst_dev::uevent> wait_for_change(uint32_t timeout_ms) noexcept {
        auto m = knst_dev::uevent_monitor::open();
        if (!m) return result<knst_dev::uevent>::fail(m.st, m.native);
        return m.value.wait(timeout_ms, "block");
    }
};

// ============================================================
// Android implementation
// ============================================================
//
// Read-only operations that work via sysfs are kept.
// Everything that needs a userspace tool or root privilege
// returns NotSupported / empty list.
//
// What actually works:
//   ✓ io_statistics()      /sys/block/<dev>/stat
//   ✓ benchmark_write/read POSIX, scoped to app-writable dirs
//   ✓ identity()           partial: /sys/block/<dev>/queue/*
//   ✓ smart()              partial: NVMe temp via sysfs hwmon
//
// What does NOT work (returns NotSupported / empty):
//   ✗ mount / unmount / eject    (udisksctl)
//   ✗ trim                       (fstrim)
//   ✗ list_luks                  (cryptsetup, /dev/mapper locked)
//   ✗ list_raid                  (/proc/mdstat restricted)
//   ✗ list_lvm                   (lvs, no LVM on Android)
//   ✗ wait_for_change            (uevent blocked)
//   ✗ full SMART health          (no smartctl)
//
// Note: knst_sto::list_mounts() / filesystem_usage() /
// directory_watcher() all work on Android because they only
// use POSIX APIs.

#elif defined(KNST_USING_PLATFORM_ANDROID)

class storage {
public:
    // ---------- mount / unmount / eject: not available ----------
    static result<knst_c16string> mount(const knst_c16string&) noexcept {
        return result<knst_c16string>::fail(status::NotSupported);
    }
    static result<void> unmount(const knst_c16string&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> eject(const knst_storage_info&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> trim(const knst_c16string&) noexcept {
        return result<void>::fail(status::NotSupported);
    }

    // ---------- SMART: partial (temperature via sysfs only) ----------
    static result<smart_report> smart(const knst_storage_info& disk) noexcept {
        using R = result<smart_report>;
        if (!knst_dev::is_dev_path(disk.device))
            return R::fail(status::InvalidArgument);

        smart_report r;

        // NVMe temperature via sysfs hwmon — this path is usually
        // readable on Android even though smartctl is not.
        knst_byte_string dev_u8 = knst_dev::to_utf8(disk.device);
        const char* devname = ::strrchr(knst_dev::text(dev_u8), '/');
        if (devname) {
            ++devname;
            char p[256];
            ::snprintf(p, sizeof(p), "/sys/block/%s/device/hwmon", devname);
            DIR* d = ::opendir(p);
            if (d) {
                struct dirent* e;
                while ((e = ::readdir(d)) != nullptr) {
                    if (e->d_name[0] == '.') continue;
                    char tp[320];
                    int64_t tv = 0;
                    ::snprintf(tp, sizeof(tp), "%s/%s/temp1_input", p, e->d_name);
                    if (knst_devices::detail::read_file_i64(tp, tv) && tv > 0) {
                        r.temperature_c = static_cast<int32_t>(tv / 1000);
                        r.available = true;
                        break;
                    }
                }
                ::closedir(d);
            }
        }

        r.model = disk.model;
        r.firmware = disk.firmware;

        if (!r.available)
            return R::fail(status::NotSupported);
        return R::success(std::move(r));
    }

    static result<void> apply_smart(knst_storage_info& disk) noexcept {
        auto s = smart(disk);
        if (!s) return result<void>::fail(s.st, s.native);
        disk.has_smart = true;
        disk.smart_passed = true;   // partial only
        if (s.value.temperature_c) {
            disk.temperature_c = s.value.temperature_c;
            disk.has_temperature = true;
        }
        return result<void>::success();
    }

    // ---------- health: not supported without SMART ----------
    static result<health_snapshot> health_check(const knst_storage_info& disk) noexcept {
        using R = result<health_snapshot>;
        auto s = smart(disk);
        if (!s) return R::fail(s.st, s.native);
        health_snapshot h;
        h.score = 100.0;
        build_health(s.value, h);
        return R::success(std::move(h));
    }

    // ---------- identity: partial from sysfs ----------
    static result<disk_identity> identity(const knst_storage_info& disk) noexcept {
        using R = result<disk_identity>;
        if (!knst_dev::is_dev_path(disk.device))
            return R::fail(status::InvalidArgument);

        disk_identity id;
        id.model       = disk.model;
        id.vendor      = disk.vendor;
        id.firmware    = disk.firmware;
        id.serial      = disk.serial;
        id.size_bytes  = disk.total_bytes;
        id.is_ssd      = disk.is_solid_state;
        id.is_nvme     = (disk.type == knst_storage_type::NVMe);

        // /sys/block/<dev>/queue/* — usually readable on Android
        knst_byte_string dev_u8 = knst_dev::to_utf8(disk.device);
        const char* devname = ::strrchr(knst_dev::text(dev_u8), '/');
        if (devname) {
            ++devname;
            char p[256];
            uint64_t v = 0;

            ::snprintf(p, sizeof(p), "/sys/block/%s/queue/logical_block_size", devname);
            if (knst_devices::detail::read_file_u64(p, v)) id.sector_size = v;

            ::snprintf(p, sizeof(p), "/sys/block/%s/queue/physical_block_size", devname);
            if (knst_devices::detail::read_file_u64(p, v)) id.physical_sector_size = v;

            ::snprintf(p, sizeof(p), "/sys/block/%s/queue/discard_max_bytes", devname);
            if (knst_devices::detail::read_file_u64(p, v)) id.has_trim_support = (v > 0);

            ::snprintf(p, sizeof(p), "/sys/block/%s/device/wwid", devname);
            char wwid[128] = {0};
            if (knst_devices::detail::read_file(p, wwid, sizeof(wwid)))
                id.wwid = knst_c16string(wwid);
        }

        // Partition table / fs type: not available without blkid/lsblk
        // (blkid library would need root, lsblk not installed)

        // Boot disk — check if any partition has is_boot
        for (auto& p : disk.partitions) {
            if (p.is_boot) { id.is_boot_disk = true; break; }
        }

        // Encryption — check for dm-crypt naming
        for (auto& p : disk.partitions) {
            knst_byte_string pb = knst_dev::to_utf8(p.device);
            const char* pc = knst_dev::text(pb);
            if (pc && ::strstr(pc, "/dev/mapper/")) {
                id.is_encrypted = true;
                break;
            }
        }

        id.valid = true;
        return R::success(std::move(id));
    }

    // ---------- io stats: /sys/block/<dev>/stat works ----------
    static result<io_stats> io_statistics(const knst_storage_info& disk) noexcept {
        using R = result<io_stats>;
        if (!knst_dev::is_dev_path(disk.device))
            return R::fail(status::InvalidArgument);

        knst_byte_string u8 = knst_dev::to_utf8(disk.device);
        const char* devname = ::strrchr(knst_dev::text(u8), '/');
        if (!devname) return R::fail(status::InvalidArgument);
        ++devname;

        char p[256];
        ::snprintf(p, sizeof(p), "/sys/block/%s/stat", devname);
        char buf[512];
        if (!knst_devices::detail::read_file(p, buf, sizeof(buf)))
            return R::fail(status::NotFound);

        io_stats s;
        unsigned long long a[17] = {0};
        int n = ::sscanf(buf,
            "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
            &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7],
            &a[8], &a[9], &a[10], &a[11], &a[12], &a[13], &a[14], &a[15], &a[16]);
        if (n < 11) return R::fail(status::IoError);

        s.reads_completed  = a[0];
        s.reads_merged     = a[1];
        s.sectors_read     = a[2];
        s.read_ticks_ms    = a[3];
        s.writes_completed = a[4];
        s.writes_merged    = a[5];
        s.sectors_written  = a[6];
        s.write_ticks_ms   = a[7];
        s.io_in_progress   = a[8];
        s.io_ticks_ms      = a[9];
        s.weighted_io_ms   = a[10];
        s.valid = true;
        return R::success(std::move(s));
    }

    // ---------- benchmarks: work (scoped to writable dirs) ----------
    static result<speed_result> benchmark_write(const knst_c16string& dir_path,
                                                 uint64_t size_mb = 100) noexcept {
        using R = result<speed_result>;
        if (!knst_file::dir_exists(dir_path)) return R::fail(status::NotFound);
        if (size_mb == 0 || size_mb > 8192) return R::fail(status::InvalidArgument);

        knst_c16string temp = dir_path;
        temp.append(u"/.knst_bench_");
        temp.append(static_cast<unsigned int>(current_pid()));
        temp.append(u"_");
        temp.append(static_cast<unsigned int>(now_ms()));
        temp.append(u".tmp");

        knst_byte_string tp = knst_dev::to_utf8(temp);
        const char* tmp_path = knst_dev::text(tp);

        int fd = ::open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return R::fail(knst_dev::map_errno(errno), errno);

        const uint64_t total = size_mb * 1024ULL * 1024ULL;
        const uint32_t chunk = 1024 * 1024;
        knst_byte_string buf;
        buf.resize(chunk, 0xAB);

        auto t0 = std::chrono::steady_clock::now();
        uint64_t written = 0;
        bool ok = true;
        while (written < total) {
            uint32_t want = static_cast<uint32_t>(
                (total - written) < chunk ? (total - written) : chunk);
            ssize_t w = ::write(fd, buf.data(), want);
            if (w <= 0) { ok = false; break; }
            written += static_cast<uint64_t>(w);
        }
        if (ok) ::fsync(fd);
        ::close(fd);
        auto t1 = std::chrono::steady_clock::now();
        ::unlink(tmp_path);

        if (!ok) return R::fail(status::IoError);

        speed_result r;
        r.bytes = written;
        r.duration_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        if (r.duration_ms > 0) {
            r.mb_per_sec = static_cast<double>(written) / (1024.0 * 1024.0)
                         / (static_cast<double>(r.duration_ms) / 1000.0);
            r.iops = static_cast<double>(written / chunk)
                   / (static_cast<double>(r.duration_ms) / 1000.0);
        }
        r.cache_bypassed = false;
        r.valid = true;
        return R::success(std::move(r));
    }

    static result<speed_result> benchmark_read(const knst_c16string& file_path,
                                                uint64_t size_mb = 100,
                                                bool try_direct = true) noexcept {
        using R = result<speed_result>;
        if (!knst_file::file_exists(file_path)) return R::fail(status::NotFound);
        if (size_mb == 0 || size_mb > 8192) return R::fail(status::InvalidArgument);

        knst_byte_string fp = knst_dev::to_utf8(file_path);
        const char* path = knst_dev::text(fp);

        const uint64_t max_bytes = size_mb * 1024ULL * 1024ULL;
        const uint32_t chunk = 1024 * 1024;
        const uint32_t align = 4096;

        int fd = -1;
        void* raw = nullptr;
        bool direct = false;

        if (try_direct) {
            fd = ::open(path, O_RDONLY | O_DIRECT);
            if (fd >= 0) {
                if (posix_memalign(&raw, align, chunk) == 0 && raw) {
                    direct = true;
                } else {
                    ::close(fd);
                    fd = -1;
                }
            }
        }

        if (fd < 0) {
            fd = ::open(path, O_RDONLY);
            if (fd < 0) return R::fail(knst_dev::map_errno(errno), errno);
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        }

        knst_byte_string buf;
        unsigned char* buf_ptr = nullptr;
        if (!direct) {
            buf.resize(chunk, 0);
            buf_ptr = const_cast<unsigned char*>(buf.data());
        } else {
            buf_ptr = static_cast<unsigned char*>(raw);
        }

        auto t0 = std::chrono::steady_clock::now();
        uint64_t total_read = 0;
        while (total_read < max_bytes) {
            uint32_t want = static_cast<uint32_t>(
                (max_bytes - total_read) < chunk ? (max_bytes - total_read) : chunk);
            ssize_t n = ::read(fd, buf_ptr, want);
            if (n <= 0) break;
            total_read += static_cast<uint64_t>(n);
        }
        auto t1 = std::chrono::steady_clock::now();

        ::close(fd);
        if (raw) ::free(raw);

        if (total_read == 0) return R::fail(status::IoError);

        speed_result r;
        r.bytes = total_read;
        r.duration_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        if (r.duration_ms > 0) {
            r.mb_per_sec = static_cast<double>(total_read) / (1024.0 * 1024.0)
                         / (static_cast<double>(r.duration_ms) / 1000.0);
            r.iops = static_cast<double>(total_read / chunk)
                   / (static_cast<double>(r.duration_ms) / 1000.0);
        }
        r.cache_bypassed = direct;
        r.valid = true;
        return R::success(std::move(r));
    }

    // ---------- LUKS / RAID / LVM: empty on Android ----------
    static result<knst_vector<luks_container>> list_luks() noexcept {
        return result<knst_vector<luks_container>>::success(
            knst_vector<luks_container>());
    }
    static result<knst_vector<raid_array>> list_raid() noexcept {
        return result<knst_vector<raid_array>>::success(
            knst_vector<raid_array>());
    }
    static result<knst_vector<lvm_volume>> list_lvm() noexcept {
        return result<knst_vector<lvm_volume>>::success(
            knst_vector<lvm_volume>());
    }

    // ---------- hotplug: not available to non-system apps ----------
    static result<knst_dev::uevent> wait_for_change(uint32_t) noexcept {
        return result<knst_dev::uevent>::fail(status::NotSupported);
    }
};

#endif // KNST_USING_PLATFORM_LINUX / ANDROID

// ============================================================
// Windows implementation
// ============================================================

#if KNST_USING_PLATFORM_WINDOWS

class storage {
public:
    static result<knst_c16string> mount(const knst_c16string&) noexcept {
        return result<knst_c16string>::fail(status::NotSupported);
    }
    static result<void> unmount(const knst_c16string&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<smart_report> smart(const knst_storage_info&) noexcept {
        return result<smart_report>::fail(status::NotSupported);
    }
    static result<void> apply_smart(knst_storage_info&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> trim(const knst_c16string&) noexcept {
        return result<void>::fail(status::NotSupported);
    }
    static result<void> eject(const knst_storage_info&) noexcept {
        return result<void>::fail(status::NotSupported);
    }

    static result<void> eject_volume(const knst_c16string& drive) noexcept {
        using R = result<void>;
        if (drive.empty()) return R::fail(status::InvalidArgument);
        char16_t letter = drive[0];
        if (!((letter >= u'A' && letter <= u'Z') || (letter >= u'a' && letter <= u'z')))
            return R::fail(status::InvalidArgument);

        wchar_t root[4] = { static_cast<wchar_t>(letter), L':', L'\\', 0 };
        if (::GetDriveTypeW(root) != DRIVE_REMOVABLE && ::GetDriveTypeW(root) != DRIVE_CDROM)
            return R::fail(status::InvalidArgument);

        wchar_t path[8] = { L'\\', L'\\', L'.', L'\\',
                            static_cast<wchar_t>(letter), L':', 0 };
        HANDLE h = ::CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return R::fail(knst_dev::map_win_error(::GetLastError()),
                           static_cast<int>(::GetLastError()));

        DWORD br = 0;
        bool ok = true;
        ok = ok && ::DeviceIoControl(h, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &br, nullptr);
        ok = ok && ::DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &br, nullptr);
        PREVENT_MEDIA_REMOVAL pmr{};
        pmr.PreventMediaRemoval = FALSE;
        if (ok) ::DeviceIoControl(h, IOCTL_STORAGE_MEDIA_REMOVAL, &pmr, sizeof(pmr), nullptr, 0, &br, nullptr);
        ok = ok && ::DeviceIoControl(h, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &br, nullptr);
        DWORD err = ::GetLastError();
        ::CloseHandle(h);
        return ok ? R::success()
                  : R::fail(err == ERROR_ACCESS_DENIED ? status::PermissionDenied : status::Busy,
                            static_cast<int>(err));
    }

    static result<health_snapshot> health_check(const knst_storage_info& disk) noexcept {
        using R = result<health_snapshot>;
        health_snapshot h;
        h.score = 100.0;

        uint32_t num = 0; bool got = false;
        for (uint32_t i = 0; i < disk.device.length(); ++i) {
            char16_t c = disk.device[i];
            if (c >= u'0' && c <= u'9') { num = num * 10 + (c - u'0'); got = true; }
            else if (got) break;
        }
        if (!got) {
            h.warnings.push_back(u"SMART not available");
            h.healthy = false;
            return R::success(std::move(h));
        }

        auto r = knst_devices::detail::wmi_query_ns("root\\Microsoft\\Windows\\Storage",
            "MSFT_PhysicalDisk", "MediaType,HealthStatus", 3000);
        if (r.empty()) {
            h.warnings.push_back(u"SMART unavailable via WMI");
            return R::success(std::move(h));
        }
        h.healthy = true;
        return R::success(std::move(h));
    }

        static result<disk_identity> identity(const knst_storage_info& disk) noexcept {
        using R = result<disk_identity>;
        disk_identity id;
        id.model    = disk.model;
        id.serial   = disk.serial;
        id.firmware = disk.firmware;
        id.size_bytes = disk.total_bytes;
        id.is_ssd = disk.is_solid_state;
        id.is_nvme = (disk.type == knst_storage_type::NVMe);
        id.is_boot_disk = disk.is_system_disk;
        // Windows NTFS on SSD always has TRIM active since Vista.
        // Assume it when the disk is a solid state drive.
        id.has_trim_support = disk.is_solid_state;

        // ── Filesystem info: for the first partition with a drive letter,
        //    GetVolumeInformationW gives fs_type, fs_uuid (serial), label.
        //    knst_devices::storage() already exposes partitions with their
        //    mount_point in the form "C:\" on Windows.
        for (const auto& p : disk.partitions) {
            if (p.mount_point.empty()) continue;
            // mount_point should look like "C:\" — need at least 3 chars
            if (p.mount_point.length() < 2) continue;
            wchar_t root[4] = {
                static_cast<wchar_t>(p.mount_point[0]), L':', L'\\', 0
            };
            wchar_t fs_name[64]   = {0};
            wchar_t vol_name[256] = {0};
            DWORD serial = 0, max_comp = 0, flags = 0;
            if (::GetVolumeInformationW(root, vol_name, 256,
                                        &serial, &max_comp, &flags,
                                        fs_name, 64)) {
                if (id.fs_type.empty())
                    id.fs_type = knst_c16string(
                        reinterpret_cast<const char16_t*>(fs_name));
                                if (id.fs_uuid.empty()) {
                    char buf[16];
                    ::snprintf(buf, sizeof(buf), "%08lX",
                               static_cast<unsigned long>(serial));
                    id.fs_uuid = knst_c16string(buf);
                }
                if (id.fs_label.empty() && vol_name[0])
                    id.fs_label = knst_c16string(
                        reinterpret_cast<const char16_t*>(vol_name));
                break;
            }
        }

        // ── Partition table: WMI MSFT_Partition exposes PartitionStyle
        //    (MBR / GPT) for the disk. Parse the disk index out of
        //    \\.\PHYSICALDRIVE<N> and query the matching partition.
        {
            uint32_t disk_num = 0; bool got_num = false;
            for (uint32_t i = 0; i < disk.device.length(); ++i) {
                char16_t c = disk.device[i];
                if (c >= u'0' && c <= u'9') {
                    disk_num = disk_num * 10 + (c - u'0');
                    got_num = true;
                } else if (got_num) {
                    break;
                }
            }
            if (got_num) {
                knst_c16string filter = u"DiskNumber=";
                filter.append(static_cast<unsigned int>(disk_num));
                auto wmi = knst_devices::detail::wmi_query_ns(
                    "root\\Microsoft\\Windows\\Storage",
                    "MSFT_Partition",
                    "DiskNumber,PartitionNumber,PartitionStyle",
                    3000);
                if (!wmi.empty()) {
                    const char* q = (const char*)wmi.data();
                    const char* e1 = ::strchr(q, '\n');
                    if (e1) q = e1 + 1;
                    while (q && *q) {
                        const char* e = ::strchr(q, '\n');
                        size_t len = e ? (size_t)(e - q) : ::strlen(q);
                        char line[512];
                        if (len >= sizeof(line)) len = sizeof(line) - 1;
                        ::memcpy(line, q, len); line[len] = '\0';

                        auto row = knst_devices::detail::parse_csv(line);
                        if (row.count >= 3) {
                            uint32_t dn = (uint32_t)::atoi(row.cols[0]);
                            if (dn != disk_num) { if (!e) break; q = e + 1; continue; }
                            uint32_t style = (uint32_t)::atoi(row.cols[2]);
                            // MSFT_Partition.PartitionStyle:
                            //   0 = MBR, 1 = GPT, 2 = RAW
                            if (style == 0)      id.partition_table = u"MBR";
                            else if (style == 1) id.partition_table = u"GPT";
                            else if (style == 2) id.partition_table = u"RAW";
                            break;
                        }
                        if (!e) break;
                        q = e + 1;
                    }
                }
            }
        }

        id.valid = true;
        return R::success(std::move(id));
    }

        static result<io_stats> io_statistics(const knst_storage_info&) noexcept {
        return result<io_stats>::fail(status::NotSupported);
    }

    // ---------- benchmarks (Windows: buffered, no O_DIRECT) ----------
    static result<speed_result> benchmark_write(const knst_c16string& dir_path,
                                                 uint64_t size_mb = 100) noexcept {
        using R = result<speed_result>;
        if (!knst_file::dir_exists(dir_path)) return R::fail(status::NotFound);
        if (size_mb == 0 || size_mb > 8192) return R::fail(status::InvalidArgument);

        knst_c16string temp = dir_path;
        temp.append(u"/.knst_bench_");
        temp.append(static_cast<unsigned int>(current_pid()));
        temp.append(u"_");
        temp.append(static_cast<unsigned int>(now_ms()));
        temp.append(u".tmp");

        HANDLE h = ::CreateFileW(reinterpret_cast<const wchar_t*>(temp.data()),
                                 GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return R::fail(status::IoError, static_cast<int>(::GetLastError()));

        const uint64_t total = size_mb * 1024ULL * 1024ULL;
        const uint32_t chunk = 1024 * 1024;
        knst_byte_string buf;
        buf.resize(chunk, 0xAB);

        auto t0 = std::chrono::steady_clock::now();
        uint64_t written = 0;
        bool ok = true;
        while (written < total) {
            uint32_t want = static_cast<uint32_t>(
                (total - written) < chunk ? (total - written) : chunk);
            DWORD wrote = 0;
            if (!::WriteFile(h, buf.data(), want, &wrote, nullptr) || wrote == 0) {
                ok = false; break;
            }
            written += wrote;
        }
        if (ok) ::FlushFileBuffers(h);
        ::CloseHandle(h);
        auto t1 = std::chrono::steady_clock::now();
        ::DeleteFileW(reinterpret_cast<const wchar_t*>(temp.data()));

        if (!ok) return R::fail(status::IoError);

        speed_result r;
        r.bytes = written;
        r.duration_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        if (r.duration_ms > 0) {
            r.mb_per_sec = static_cast<double>(written) / (1024.0 * 1024.0)
                         / (static_cast<double>(r.duration_ms) / 1000.0);
            r.iops = static_cast<double>(written / chunk)
                   / (static_cast<double>(r.duration_ms) / 1000.0);
        }
        r.cache_bypassed = false;
        r.valid = true;
        return R::success(std::move(r));
    }

    static result<speed_result> benchmark_read(const knst_c16string& file_path,
                                                uint64_t size_mb = 100,
                                                bool /*try_direct*/ = true) noexcept {
        using R = result<speed_result>;
        if (!knst_file::file_exists(file_path)) return R::fail(status::NotFound);
        if (size_mb == 0 || size_mb > 8192) return R::fail(status::InvalidArgument);

        HANDLE h = ::CreateFileW(reinterpret_cast<const wchar_t*>(file_path.data()),
                                 GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                 nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return R::fail(status::IoError, static_cast<int>(::GetLastError()));

        const uint64_t max_bytes = size_mb * 1024ULL * 1024ULL;
        const uint32_t chunk = 1024 * 1024;
        knst_byte_string buf;
        buf.resize(chunk, 0);

        auto t0 = std::chrono::steady_clock::now();
        uint64_t total_read = 0;
        while (total_read < max_bytes) {
            uint32_t want = static_cast<uint32_t>(
                (max_bytes - total_read) < chunk ? (max_bytes - total_read) : chunk);
            DWORD got = 0;
            if (!::ReadFile(h, const_cast<unsigned char*>(buf.data()), want, &got, nullptr)
                || got == 0)
                break;
            total_read += got;
        }
        auto t1 = std::chrono::steady_clock::now();
        ::CloseHandle(h);

        if (total_read == 0) return R::fail(status::IoError);

        speed_result r;
        r.bytes = total_read;
        r.duration_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        if (r.duration_ms > 0) {
            r.mb_per_sec = static_cast<double>(total_read) / (1024.0 * 1024.0)
                         / (static_cast<double>(r.duration_ms) / 1000.0);
            r.iops = static_cast<double>(total_read / chunk)
                   / (static_cast<double>(r.duration_ms) / 1000.0);
        }
        r.cache_bypassed = false;   // Windows path uses buffered I/O
        r.valid = true;
        return R::success(std::move(r));
    }

    static result<knst_vector<bitlocker_status>> list_bitlocker() noexcept {
        using R = result<knst_vector<bitlocker_status>>;
        R r;

        knst_c16string ps =
            u"Get-BitLockerVolume | Select-Object MountPoint,VolumeStatus,"
            u"EncryptionPercentage,ProtectionStatus,EncryptionMethod "
            u"| ConvertTo-Csv -NoTypeInformation";

                auto res = knst_process::run_capture(
            u"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
            { u"-NoProfile", u"-NonInteractive", u"-Command", ps },
            {}, nullptr, 0, 5000);

        // Distinguish "PowerShell not found" (ToolMissing) from
        // "PowerShell ran but Get-BitLockerVolume doesn't exist"
        // (Windows Home edition — BitLocker cmdlets absent → NotSupported).
        // Exit code != 0 with stderr mentioning "not recognized" /
        // "CommandNotFoundException" is the Home-edition case.
        if (res.error == knst_process_error::NotFound ||
            res.error == knst_process_error::NotSupported) {
            return R::fail(status::ToolMissing);
        }
                if (res.error == knst_process_error::None && res.exit_code == 0) {
            // normal path — parse below
        } else {
            // PowerShell writes UTF-16 stderr on Windows, so the raw
            // bytes contain NUL characters between letters. Normalize
            // before matching, otherwise "Get-BitLockerVolume" doesn't
            // match "G\0e\0t\0...".
            knst_byte_string norm =
                knst_devices::detail::wmi_normalize_encoding(res.err_data);
            knst_c16string err_txt(
                reinterpret_cast<const char*>(norm.data()), norm.length());

            if (err_txt.contains(u"BitLocker") ||
                err_txt.contains(u"not recognized") ||
                err_txt.contains(u"CommandNotFound") ||
                err_txt.contains(u"desc") ||   
                err_txt.contains(u"nicht erkannt") ||
                norm.empty()) {
                return R::fail(status::NotSupported);
            }
            return R::fail(status::IoError, res.exit_code);
        }
        knst_byte_string data = knst_devices::detail::wmi_normalize_encoding(res.out_data);
        if (data.empty()) return r;

        const char* p = knst_dev::text(data);
        const char* e1 = ::strchr(p, '\n');
        if (!e1) return r;
        p = e1 + 1;

        while (p && *p) {
            const char* e = ::strchr(p, '\n');
            size_t len = e ? static_cast<size_t>(e - p) : ::strlen(p);
            if (len == 0) break;

            char line[512];
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            ::memcpy(line, p, len); line[len] = 0;

                        // CSV is `"C:","FullyDecrypted",0,"Off","None"\r\n` — the naive
            // comma split leaves quotes in the values and a stray \r at the
            // end of the last column, which printf renders as a line reset.
            char cols[5][128] = {{0}};
            int ci = 0; size_t ii = 0;
            bool in_quotes = false;
            for (size_t k = 0; line[k] && ci < 5; ++k) {
                char c = line[k];
                if (c == '"') { in_quotes = !in_quotes; continue; }
                if (c == ',' && !in_quotes) { cols[ci][ii] = 0; ++ci; ii = 0; continue; }
                if (ii < 127) cols[ci][ii++] = c;
            }
            cols[ci][ii] = 0;
            // Trim trailing CR/whitespace from every column.
            for (int q = 0; q <= ci; ++q) {
                size_t n = ::strlen(cols[q]);
                while (n > 0 && (cols[q][n-1] == '\r' || cols[q][n-1] == '\n' || cols[q][n-1] == ' '))
                    cols[q][--n] = 0;
            }

            if (ci >= 3) {
                bitlocker_status b;
                b.drive = knst_c16string(cols[0]);
                b.method = knst_c16string(cols[4]);
                b.is_encrypted = (::strstr(cols[1], "FullyEncrypted") != nullptr);
                b.is_locked = (::strstr(cols[1], "Locked") != nullptr);
                b.percentage = static_cast<uint32_t>(::atoi(cols[2]));
                b.valid = true;
                r.value.push_back(std::move(b));
            }
            if (!e) break;
            p = e + 1;
        }
        return r;
    }
};

#endif // KNST_USING_PLATFORM_WINDOWS

// ============================================================
// Cross-platform: list_mounts
// ============================================================

inline result<knst_vector<mount_entry>> list_mounts() noexcept {
    using R = result<knst_vector<mount_entry>>;
    R r;

#if KNST_USING_PLATFORM_LINUX
    char buf[65536];
    if (!knst_devices::detail::read_file("/proc/mounts", buf, sizeof(buf)))
        return R::fail(status::IoError);

    knst_dev::for_each_line(buf, [&](const char* line, size_t len) {
        char b[1024];
        if (len >= sizeof(b)) len = sizeof(b) - 1;
        ::memcpy(b, line, len); b[len] = 0;

        char dev[256] = {0}, mnt[512] = {0}, fs[64] = {0}, opts[256] = {0};
        if (::sscanf(b, "%255s %511s %63s %255s", dev, mnt, fs, opts) < 3) return;

        mount_entry e;
        e.device      = knst_c16string(dev);
        e.mount_point = knst_c16string(mnt);
        e.fs_type     = knst_c16string(fs);
        e.options     = knst_c16string(opts);
        // Check if "ro" is a comma-separated token, not a substring of "remount-ro".
e.is_read_only = false;
{
    const char* p = opts;
    while (*p) {
        while (*p == ',') ++p;
        const char* end = p;
        while (*end && *end != ',') ++end;
        if (end - p == 2 && p[0] == 'r' && p[1] == 'o') {
            e.is_read_only = true;
            break;
        }
        p = end;
    }
}
        e.is_network   = (::strncmp(dev, "//", 2) == 0
                       || ::strstr(dev, ":/") != nullptr
                       || ::strcmp(fs, "nfs") == 0
                       || ::strcmp(fs, "nfs4") == 0
                       || ::strcmp(fs, "cifs") == 0);
        e.is_virtual   = (::strcmp(fs, "tmpfs") == 0
                       || ::strcmp(fs, "sysfs") == 0
                       || ::strcmp(fs, "proc") == 0
                       || ::strcmp(fs, "devtmpfs") == 0
                       || ::strcmp(fs, "devpts") == 0
                       || ::strcmp(fs, "cgroup") == 0
                       || ::strcmp(fs, "cgroup2") == 0
                       || ::strcmp(fs, "securityfs") == 0
                       || ::strcmp(fs, "pstore") == 0
                       || ::strcmp(fs, "bpf") == 0
                       || ::strcmp(fs, "autofs") == 0
                       || ::strcmp(fs, "hugetlbfs") == 0
                       || ::strcmp(fs, "mqueue") == 0
                       || ::strcmp(fs, "debugfs") == 0
                       || ::strcmp(fs, "tracefs") == 0
                       || ::strcmp(fs, "configfs") == 0
                       || ::strcmp(fs, "fusectl") == 0);
        e.valid = true;
        r.value.push_back(std::move(e));
    });
#else
    DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[4] = { static_cast<wchar_t>(L'A' + i), L':', L'\\', 0 };
        UINT type = ::GetDriveTypeW(root);
        if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN) continue;

        wchar_t fs_name[64]   = {0};
        wchar_t vol_name[256] = {0};
        DWORD serial = 0, max_comp = 0, flags = 0;
        if (!::GetVolumeInformationW(root, vol_name, 256,
                                      &serial, &max_comp, &flags,
                                      fs_name, 64))
            continue;

        mount_entry e;
        knst_c16string dev_root;
        dev_root += static_cast<char16_t>(L'A' + i);
        dev_root += u':';
        dev_root += u'\\';
        e.device      = dev_root;
        e.mount_point = dev_root;
        e.fs_type     = knst_c16string(reinterpret_cast<const char16_t*>(fs_name));
        e.is_read_only = (flags & FILE_READ_ONLY_VOLUME) != 0;
        e.is_network   = (type == DRIVE_REMOTE);
        e.valid = true;
        r.value.push_back(std::move(e));
    }
#endif
    return r;
}

inline result<mount_entry> find_mount_for_path(const knst_c16string& path) noexcept {
    using R = result<mount_entry>;
    if (path.empty()) return R::fail(status::InvalidArgument);

    auto mounts = list_mounts();
    if (!mounts) return R::fail(mounts.st, mounts.native);

    mount_entry best;
    uint32_t best_len = 0;
    for (auto& m : mounts.value) {
        uint32_t len = m.mount_point.length();
        if (len == 0 || len > path.length()) continue;
        bool match = true;
        for (uint32_t i = 0; i < len; ++i) {
            char16_t a = m.mount_point[i], b = path[i];
            if (a == u'\\') a = u'/';
            if (b == u'\\') b = u'/';
            if (a != b) { match = false; break; }
        }
        if (match && len > best_len) {
            best = m;
            best_len = len;
        }
    }
    if (best_len == 0) return R::fail(status::NotFound);
    return R::success(std::move(best));
}

inline result<knst_vector<fs_usage>> filesystem_usage() noexcept {
    using R = result<knst_vector<fs_usage>>;
    auto mounts = list_mounts();
    if (!mounts) return R::fail(mounts.st, mounts.native);

    knst_vector<fs_usage> out;
    for (auto& m : mounts.value) {
        auto sp = free_space(m.mount_point);
        if (!sp) continue;
        fs_usage u;
        u.device       = m.device;
        u.mount_point  = m.mount_point;
        u.fs_type      = m.fs_type;
        u.total_bytes  = sp.value.total_bytes;
        u.used_bytes   = sp.value.used_bytes;
        u.free_bytes   = sp.value.free_bytes;
        u.used_percent = sp.value.used_percent;
        out.push_back(std::move(u));
    }
    return R::success(std::move(out));
}

// ============================================================
// Directory watcher (polling, cross-platform)
// ============================================================

// ============================================================================
//  JSON report export — full storage inventory
// ----------------------------------------------------------------------------
//  Available only when knst_json.hpp is included before knst_storage.hpp.
//  In KernelNucleusT.hpp the order is already correct.
// ============================================================================
#if defined(KNST_JSON_HPP)

inline result<void> export_report_json(knst_json& out) noexcept {
    using R = result<void>;
    knst_json j = knst_json::make_object();
    if (!j.is_object()) return R::fail(status::OutOfMemory);

    // ---- Disks ----
    knst_json disks = knst_json::make_array();
    if (!disks.is_array()) return R::fail(status::OutOfMemory);

    auto all = knst_devices::storage();
    for (uint32_t i = 0; i < all.size(); ++i) {
        knst_json d = knst_json::make_object();
        if (!d.is_object()) return R::fail(status::OutOfMemory);

        d.set("device",         knst_json(all[i].device));
        d.set("model",          knst_json(all[i].model));
        d.set("serial",         knst_json(all[i].serial));
        d.set("type",           knst_json(static_cast<int64_t>(all[i].type)));
        d.set("total_bytes",    knst_json(static_cast<int64_t>(all[i].total_bytes)));
        d.set("is_removable",   knst_json(all[i].is_removable));
        d.set("is_solid_state", knst_json(all[i].is_solid_state));
        d.set("is_system_disk", knst_json(all[i].is_system_disk));

        knst_json parts = knst_json::make_array();
        if (!parts.is_array()) return R::fail(status::OutOfMemory);
        for (uint32_t p = 0; p < all[i].partitions.size(); ++p) {
            const auto& part = all[i].partitions[p];
            knst_json pt = knst_json::make_object();
            if (!pt.is_object()) return R::fail(status::OutOfMemory);
            pt.set("device",      knst_json(part.device));
            pt.set("mount_point", knst_json(part.mount_point));
            pt.set("fs_type",     knst_json(part.fs_type));
            pt.set("total_bytes", knst_json(static_cast<int64_t>(part.total_bytes)));
            pt.set("used_bytes",  knst_json(static_cast<int64_t>(part.used_bytes)));
            pt.set("free_bytes",  knst_json(static_cast<int64_t>(part.free_bytes)));
            pt.set("is_boot",     knst_json(part.is_boot));
            if (!parts.push_back(std::move(pt))) return R::fail(status::OutOfMemory);
        }
        d.set("partitions", std::move(parts));
        if (!disks.push_back(std::move(d))) return R::fail(status::OutOfMemory);
    }
    j.set("disks", std::move(disks));

    // ---- Mounts ----
    auto mounts = list_mounts();
    if (mounts) {
        knst_json m = knst_json::make_array();
        if (!m.is_array()) return R::fail(status::OutOfMemory);
        for (uint32_t i = 0; i < mounts.value.size(); ++i) {
            knst_json e = knst_json::make_object();
            if (!e.is_object()) return R::fail(status::OutOfMemory);
            e.set("device",       knst_json(mounts.value[i].device));
            e.set("mount_point",  knst_json(mounts.value[i].mount_point));
            e.set("fs_type",      knst_json(mounts.value[i].fs_type));
            e.set("is_read_only", knst_json(mounts.value[i].is_read_only));
            e.set("is_network",   knst_json(mounts.value[i].is_network));
            if (!m.push_back(std::move(e))) return R::fail(status::OutOfMemory);
        }
        j.set("mounts", std::move(m));
    }

    out = std::move(j);
    return R::success();
}

inline result<void> save_report_json(const knst_c16string& path,
                                     const knst_json_dump_options& opt = knst_json_dump_options()) noexcept {
    using R = result<void>;
    knst_json j;
    auto r = export_report_json(j);
    if (!r) return r;
    knst_json_error e;
    if (!j.save_file(path, opt, true, &e))
        return R::fail(status::IoError, static_cast<int>(e.code));
    return R::success();
}

#endif // KNST_JSON_HPP

class directory_watcher {
public:
    enum class event_kind : uint8_t { Created, Modified, Deleted };

        struct file_event {
        event_kind kind = event_kind::Created;
        knst_c16string path;
        uint64_t timestamp_ms = 0;
    };

    // result<directory_watcher> requires move construction AND move
    // assignment for its value member. std::atomic<bool> blocks both —
    // provide explicit versions that transfer the stop flag via load()/store().
    directory_watcher() noexcept = default;
    directory_watcher(directory_watcher&& o) noexcept
        : path_(std::move(o.path_)),
          snapshot_(std::move(o.snapshot_)),
          pending_(std::move(o.pending_)),
          stop_(o.stop_.load()) {}
    directory_watcher& operator=(directory_watcher&& o) noexcept {
        if (this != &o) {
            path_     = std::move(o.path_);
            snapshot_ = std::move(o.snapshot_);
            pending_  = std::move(o.pending_);
            stop_.store(o.stop_.load());
        }
        return *this;
    }

    static result<directory_watcher> open(const knst_c16string& path) noexcept {
        using R = result<directory_watcher>;
        if (!knst_file::dir_exists(path)) return R::fail(status::NotFound);
        directory_watcher w;
        w.path_ = path;
        w.refresh();
        return R::success(std::move(w));
    }

    result<file_event> wait(uint32_t timeout_ms) noexcept {
        using R = result<file_event>;
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(timeout_ms);

        while (!stop_) {
            refresh();
            if (!pending_.empty()) {
                file_event e = pending_[0];
                pending_.erase(0);
                return R::success(std::move(e));
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return R::fail(status::Timeout);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return R::fail(status::BadState);
    }

    void stop() noexcept { stop_ = true; }

private:
    void refresh() noexcept {
        std::map<knst_c16string, uint64_t> current;
        scan(current, path_, 0);

        uint64_t t = now_ms();

        for (auto& kv : current) {
            auto it = snapshot_.find(kv.first);
            if (it == snapshot_.end()) {
                file_event e;
                e.kind = event_kind::Created;
                e.path = kv.first;
                e.timestamp_ms = t;
                pending_.push_back(std::move(e));
            } else if (it->second != kv.second) {
                file_event e;
                e.kind = event_kind::Modified;
                e.path = kv.first;
                e.timestamp_ms = t;
                pending_.push_back(std::move(e));
            }
        }
        for (auto& kv : snapshot_) {
            if (current.find(kv.first) == current.end()) {
                file_event e;
                e.kind = event_kind::Deleted;
                e.path = kv.first;
                e.timestamp_ms = t;
                pending_.push_back(std::move(e));
            }
        }
        snapshot_ = std::move(current);
    }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    void scan(std::map<knst_c16string, uint64_t>& out,
              const knst_c16string& path, uint32_t depth) noexcept {
        if (depth > 32) return;
#if KNST_USING_PLATFORM_LINUX
        knst_byte_string p8 = knst_dev::to_utf8(path);
        const char* p = knst_dev::text(p8);
        DIR* d = ::opendir(p);
        if (!d) return;
        struct dirent* e;
        char full[4096];
        while ((e = ::readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            int n = ::snprintf(full, sizeof(full), "%s/%s", p, e->d_name);
            if (n <= 0 || n >= static_cast<int>(sizeof(full))) continue;
            struct stat st;
            if (::lstat(full, &st) != 0) continue;
            uint64_t mt = static_cast<uint64_t>(st.st_mtime);
            knst_c16string key(full);
            out.emplace(std::move(key), mt);
            if (S_ISDIR(st.st_mode))
                scan(out, knst_c16string(full), depth + 1);
        }
        ::closedir(d);
#else
        knst_c16string search = path;
        search.append(u"\\*");
        wchar_t wpath[1024];
        uint32_t len = search.length();
        if (len == 0 || len >= 1024) return;
        for (uint32_t i = 0; i < len; ++i) wpath[i] = static_cast<wchar_t>(search[i]);
        wpath[len] = L'\0';
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileW(wpath, &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.cFileName[0] == L'.') continue;
            knst_c16string full = path;
            full.append(u"\\");
            for (int i = 0; fd.cFileName[i]; ++i)
                full += static_cast<char16_t>(fd.cFileName[i]);
            uint64_t ft = (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32)
                        | static_cast<uint64_t>(fd.ftLastWriteTime.dwLowDateTime);
            knst_c16string key(full);
            out.emplace(std::move(key), (ft / 10000000ULL) - 11644473600ULL);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                scan(out, full, depth + 1);
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
#endif
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    knst_c16string path_;
    std::map<knst_c16string, uint64_t> snapshot_;
    knst_vector<file_event> pending_;
    std::atomic<bool> stop_{false};
};

} // namespace knst_sto