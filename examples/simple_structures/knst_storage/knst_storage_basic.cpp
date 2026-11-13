/*
  knst_storage_basic.cpp

  Comprehensive demonstration of knst_sto — the non-destructive storage
  controller built on knst_devices::storage().

  Sections:
    1.  Platform availability
    2.  Disk enumeration
    3.  Free space
    4.  Mount listing
    5.  Find mount for path
    6.  Filesystem usage
    7.  Directory size
    8.  Largest files
    9.  Disk identity (serial, fs, partition table, encryption)
    10. SMART health report (ATA + NVMe)
    11. Health snapshot
    12. IO statistics
    13. Benchmark — write
    14. Benchmark — read (O_DIRECT)
    15. Mount / unmount (safe demo)
    16. Eject (safe demo)
    17. LUKS discovery
    18. RAID discovery
    19. LVM discovery
    20. BitLocker (Windows only)
    21. Directory watcher
    22. Hot-plug wait (Linux only)
    23. Platform summary
    24. Graceful failure
    25. Cleanup

*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <thread>
#include <chrono>


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
    return "Windows (WMI / IOCTL)";
#else
    return "Linux (sysfs / udisks / smartctl)";
#endif
}

static const char* storage_type_name(knst_storage_type t) {
    switch (t) {
        case knst_storage_type::HDD:     return "HDD ";
        case knst_storage_type::SSD:     return "SSD ";
        case knst_storage_type::NVMe:    return "NVMe";
        case knst_storage_type::USB:     return "USB ";
        case knst_storage_type::eMMC:    return "eMMC";
        case knst_storage_type::SD_Card: return "SD  ";
        case knst_storage_type::Optical: return "Opt ";
        case knst_storage_type::Floppy:  return "FD  ";
        case knst_storage_type::RAID:    return "RAID";
        case knst_storage_type::Virtual: return "Virt";
        default:                          return "?   ";
    }
}

static void print_size(uint64_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int u = 0;
    double v = static_cast<double>(bytes);
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    if (u == 0) std::cout << static_cast<uint64_t>(v) << " " << units[u];
    else        std::cout << v << " " << units[u];
}

static void print_disk(const knst_storage_info& d) {
    std::cout << d.device;
    if (!d.model.empty()) std::cout << "  " << d.model;
    std::cout << "  (";
    print_size(d.total_bytes);
    std::cout << ")";
    if (d.is_system_disk) std::cout << "  [SYSTEM]";
    if (d.is_removable)   std::cout << "  [REMOVABLE]";
}


// ============================================================
// main
// ============================================================

int main() {

    std::cout << "Platform: " << platform_name() << "\n";
    std::cout << "SAFE MODE: no destructive operations will be performed.\n";

    // =====================================================================
    section("1) Platform availability");
    // =====================================================================
    {
        auto disks = knst_devices::storage();
        std::cout << "  disk count  : " << disks.size() << "\n";

        uint32_t system_disks = 0, removable = 0;
        for (auto& d : disks) {
            if (d.is_system_disk) ++system_disks;
            if (d.is_removable)   ++removable;
        }
        std::cout << "  system disks: " << system_disks << "\n";
        std::cout << "  removable   : " << removable << "\n";

        auto mounts = knst_sto::list_mounts();
        if (mounts) std::cout << "  mount points: " << mounts.value.size() << "\n";
        else        std::cout << "  mount points: " << mounts.error_string() << "\n";
    }

    // =====================================================================
    section("2) Disk enumeration");
    // =====================================================================
    {
        auto disks = knst_devices::storage();
        if (disks.empty()) {
            std::cout << "  (no disks — skipping)\n";
        } else {
            uint32_t limit = disks.size() < 10 ? disks.size() : 10;
            for (uint32_t i = 0; i < limit; ++i) {
                std::cout << "  [" << i << "] "
                          << storage_type_name(disks[i].type) << "  ";
                print_disk(disks[i]);
                std::cout << "\n";
            }
            if (disks.size() > 10)
                std::cout << "  ... and " << (disks.size() - 10) << " more\n";
        }
    }

    // =====================================================================
    section("3) Free space");
    // =====================================================================
    {
        knst_c16string home = knst_file::get_home_dir();
        auto s = knst_sto::free_space(home);
        if (s) {
            std::cout << "  path         : " << home << "\n";
            std::cout << "  total        : "; print_size(s.value.total_bytes);  std::cout << "\n";
            std::cout << "  used         : "; print_size(s.value.used_bytes);   std::cout << "\n";
            std::cout << "  free         : "; print_size(s.value.free_bytes);   std::cout << "\n";
            std::cout << "  used_percent : " << s.value.used_percent << " %\n";
        } else {
            std::cout << "  free_space: " << s.error_string() << "\n";
        }
    }

    // =====================================================================
    section("4) Mount listing");
    // =====================================================================
    {
        auto mounts = knst_sto::list_mounts();
        if (!mounts) {
            std::cout << "  list_mounts: " << mounts.error_string() << "\n";
        } else {
            std::cout << "  total mounts: " << mounts.value.size() << "\n";
            uint32_t shown = 0;
            for (uint32_t i = 0; i < mounts.value.size() && shown < 15; ++i) {
                const auto& m = mounts.value[i];
                // Skip virtual pseudo-filesystems in display
                if (m.is_virtual) continue;
                std::cout << "  [" << shown << "] ";
                std::cout << m.mount_point << "  (" << m.fs_type << ")";
                if (m.is_read_only) std::cout << "  [RO]";
                if (m.is_network)   std::cout << "  [NET]";
                std::cout << "\n";
                ++shown;
            }
            if (shown == 0)
                std::cout << "  (only pseudo-filesystems mounted)\n";
        }
    }

    // =====================================================================
    section("5) Find mount for path");
    // =====================================================================
    {
        knst_c16string paths[] = {
            knst_file::get_home_dir(),
            knst_file::get_temp_dir(),
#if KNST_USING_PLATFORM_LINUX
            u"/proc",
#else
            u"C:\\Windows",
#endif
        };

        for (auto& p : paths) {
            auto m = knst_sto::find_mount_for_path(p);
            if (m) {
                std::cout << "  " << p << "\n    → "
                          << m.value.mount_point << " ("
                          << m.value.fs_type << ")\n";
            } else {
                std::cout << "  " << p << "  → " << m.error_string() << "\n";
            }
        }
    }

    // =====================================================================
    section("6) Filesystem usage");
    // =====================================================================
    {
        auto usage = knst_sto::filesystem_usage();
        if (!usage) {
            std::cout << "  filesystem_usage: " << usage.error_string() << "\n";
        } else {
            uint32_t shown = 0;
            for (uint32_t i = 0; i < usage.value.size() && shown < 10; ++i) {
                const auto& u = usage.value[i];
                if (u.total_bytes == 0) continue;
                // Skip tiny pseudo-fs
                if (u.total_bytes < 1024 * 1024) continue;
                std::cout << "  " << u.mount_point << "  ("
                          << u.fs_type << ")\n";
                std::cout << "    total: "; print_size(u.total_bytes); std::cout << "\n";
                std::cout << "    used : "; print_size(u.used_bytes);  std::cout
                          << "  (" << u.used_percent << " %)\n";
                ++shown;
            }
        }
    }

    // =====================================================================
    section("7) Directory size");
    // =====================================================================
    {
        knst_c16string dir = knst_file::get_temp_dir();
        auto s = knst_sto::directory_size(dir);
        if (s) {
            std::cout << "  path       : " << dir << "\n";
            std::cout << "  files      : " << s.value.file_count << "\n";
            std::cout << "  dirs       : " << s.value.dir_count  << "\n";
            std::cout << "  total size : "; print_size(s.value.total_bytes); std::cout << "\n";
            std::cout << "  max depth  : " << s.value.max_depth  << "\n";
        } else {
            std::cout << "  directory_size: " << s.error_string() << "\n";
        }
    }

    // =====================================================================
    section("8) Largest files");
    // =====================================================================
    {
        knst_c16string dir = knst_file::get_temp_dir();
        auto files = knst_sto::largest_files(dir, 10);
        if (!files) {
            std::cout << "  largest_files: " << files.error_string() << "\n";
        } else if (files.value.empty()) {
            std::cout << "  (no files in " << dir << ")\n";
        } else {
            std::cout << "  top " << files.value.size() << " files in "
                      << dir << ":\n";
            for (uint32_t i = 0; i < files.value.size(); ++i) {
                std::cout << "  [" << i << "] ";
                print_size(files.value[i].size);
                std::cout << "  " << files.value[i].path << "\n";
            }
        }
    }

    // =====================================================================
    section("9) Disk identity");
    // =====================================================================
    {
        auto disks = knst_devices::storage();
        if (disks.empty()) {
            std::cout << "  (no disks — skipping)\n";
        } else {
            // Identity for each disk (up to 3)
            uint32_t limit = disks.size() < 3 ? disks.size() : 3;
            for (uint32_t i = 0; i < limit; ++i) {
                const auto& d = disks[i];
                std::cout << "  --- " << d.device << " ---\n";

                auto id = knst_sto::storage::identity(d);
                if (!id) {
                    std::cout << "  identity: " << id.error_string() << "\n";
                    continue;
                }

                const auto& v = id.value;
                std::cout << "    serial           : " << v.serial << "\n";
                std::cout << "    model            : " << v.model << "\n";
                if (!v.vendor.empty())
                    std::cout << "    vendor           : " << v.vendor << "\n";
                if (!v.firmware.empty())
                    std::cout << "    firmware         : " << v.firmware << "\n";
                std::cout << "    partition table  : " << v.partition_table << "\n";
                std::cout << "    fs type          : " << v.fs_type << "\n";
                std::cout << "    fs uuid          : " << v.fs_uuid << "\n";
                if (!v.fs_label.empty())
                    std::cout << "    fs label         : " << v.fs_label << "\n";
                if (!v.wwid.empty())
                    std::cout << "    wwid             : " << v.wwid << "\n";
                std::cout << "    sector size      : " << v.sector_size << " B\n";
                std::cout << "    physical sector  : " << v.physical_sector_size << " B\n";
                std::cout << "    is_ssd           : " << (v.is_ssd ? "yes" : "no") << "\n";
                std::cout << "    is_nvme          : " << (v.is_nvme ? "yes" : "no") << "\n";
                std::cout << "    has_trim         : " << (v.has_trim_support ? "yes" : "no") << "\n";
                std::cout << "    is_encrypted     : " << (v.is_encrypted ? "yes" : "no") << "\n";
                std::cout << "    is_boot_disk     : " << (v.is_boot_disk ? "yes" : "no") << "\n";
                std::cout << "\n";
            }
        }
    }

    // =====================================================================
    section("10) SMART health report");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto disks = knst_devices::storage();
        if (disks.empty()) {
            std::cout << "  (no disks — skipping)\n";
        } else {
            for (uint32_t i = 0; i < disks.size() && i < 2; ++i) {
                std::cout << "  --- " << disks[i].device << " ---\n";
                auto s = knst_sto::storage::smart(disks[i]);
                if (!s) {
                    std::cout << "  smart: " << s.error_string() << "\n";
                    std::cout << "  (may need root: sudo)\n";
                    continue;
                }
                const auto& r = s.value;
                std::cout << "  available       : " << (r.available ? "yes" : "no") << "\n";
                std::cout << "  passed          : " << (r.passed    ? "yes" : "no") << "\n";
                std::cout << "  model           : " << r.model << "\n";
                std::cout << "  firmware        : " << r.firmware << "\n";
                std::cout << "  power-on hours  : " << r.power_on_hours << " h\n";
                std::cout << "  temperature     : " << r.temperature_c << " °C\n";
                std::cout << "  percent used    : " << r.percent_used << " %\n";
                std::cout << "  reallocated     : " << r.reallocated << " sectors\n";
                std::cout << "  pending         : " << r.pending << " sectors\n";
                std::cout << "  bytes written   : "; print_size(r.bytes_written); std::cout << "\n";
                if (r.bytes_read)
                    { std::cout << "  bytes read      : "; print_size(r.bytes_read); std::cout << "\n"; }
                std::cout << "\n";
            }
        }
#else
        std::cout << "  SMART: NotSupported on Windows without smartmontools\n";
#endif
    }

    // =====================================================================
    section("11) Health snapshot");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto disks = knst_devices::storage();
        if (disks.empty()) {
            std::cout << "  (no disks — skipping)\n";
        } else {
            for (uint32_t i = 0; i < disks.size() && i < 2; ++i) {
                std::cout << "  --- " << disks[i].device << " ---\n";
                auto h = knst_sto::storage::health_check(disks[i]);
                if (!h) {
                    std::cout << "  health_check: " << h.error_string() << "\n";
                    continue;
                }
                std::cout << "  healthy            : " << (h.value.healthy ? "yes" : "no") << "\n";
                std::cout << "  score              : " << h.value.score << " / 100\n";
                std::cout << "  temperature_warn   : " << (h.value.temperature_warning ? "yes" : "no") << "\n";
                std::cout << "  reallocated_warn   : " << (h.value.reallocated_warning ? "yes" : "no") << "\n";
                std::cout << "  pending_warn       : " << (h.value.pending_sector_warning ? "yes" : "no") << "\n";
                std::cout << "  wear_warn          : " << (h.value.wear_warning ? "yes" : "no") << "\n";
                for (auto& w : h.value.warnings)
                    std::cout << "    ⚠️  " << w << "\n";
                std::cout << "\n";
            }
        }
#else
        auto disks = knst_devices::storage();
        if (!disks.empty()) {
            auto h = knst_sto::storage::health_check(disks[0]);
            if (h) {
                std::cout << "  Windows health_check: basic (WMI)\n";
                std::cout << "  healthy: " << (h.value.healthy ? "yes" : "no") << "\n";
            }
        }
#endif
    }

    // =====================================================================
    section("12) IO statistics");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto disks = knst_devices::storage();
        if (disks.empty()) {
            std::cout << "  (no disks — skipping)\n";
        } else {
            for (uint32_t i = 0; i < disks.size() && i < 2; ++i) {
                std::cout << "  --- " << disks[i].device << " ---\n";
                auto s = knst_sto::storage::io_statistics(disks[i]);
                if (!s) {
                    std::cout << "  io_statistics: " << s.error_string() << "\n";
                    continue;
                }
                std::cout << "  reads_completed  : " << s.value.reads_completed  << "\n";
                std::cout << "  writes_completed : " << s.value.writes_completed << "\n";
                std::cout << "  sectors_read     : " << s.value.sectors_read     << "\n";
                std::cout << "  sectors_written  : " << s.value.sectors_written  << "\n";
                std::cout << "  io_in_progress   : " << s.value.io_in_progress   << "\n";
                std::cout << "  io_ticks_ms      : " << s.value.io_ticks_ms      << " ms\n";
                std::cout << "  weighted_io_ms   : " << s.value.weighted_io_ms   << " ms\n";
                std::cout << "\n";
            }
        }
#else
        std::cout << "  io_statistics: NotSupported on Windows\n";
#endif
    }

        section("13) Benchmark — write");
    {
        // Windows and Linux both support benchmark_write now (the Windows
        // backend uses buffered Win32 I/O; Linux tries O_DIRECT).
        knst_c16string dir = knst_file::get_temp_dir();
        std::cout << "  writing 32 MB to " << dir << "...\n";
        auto r = knst_sto::storage::benchmark_write(dir, 32);
        if (!r) {
            std::cout << "  benchmark_write: " << r.error_string() << "\n";
        } else {
            std::cout << "  bytes     : "; print_size(r.value.bytes); std::cout << "\n";
            std::cout << "  duration  : " << r.value.duration_ms << " ms\n";
            std::cout << "  speed     : " << r.value.mb_per_sec << " MB/s\n";
            std::cout << "  cache     : "
                      << (r.value.cache_bypassed ? "bypassed (O_DIRECT)" : "buffered")
                      << "\n";
            std::cout << "  temp file : deleted automatically\n";
        }
    }

        section("14) Benchmark — read");
    {
        knst_c16string dir = knst_file::get_temp_dir();
        knst_c16string test_file = knst_file::join_path(dir, u"knst_storage_bench.bin");
        knst_byte_string payload;
        payload.resize(16 * 1024 * 1024, 0xCD);
        if (!knst_file::write_file_data(test_file, payload.data(), payload.length())) {
            std::cout << "  could not create test file\n";
        } else {
            std::cout << "  reading 16 MB from " << test_file << "...\n";
            auto r = knst_sto::storage::benchmark_read(test_file, 16);
            if (!r) {
                std::cout << "  benchmark_read: " << r.error_string() << "\n";
            } else {
                std::cout << "  bytes          : "; print_size(r.value.bytes); std::cout << "\n";
                std::cout << "  duration       : " << r.value.duration_ms << " ms\n";
                std::cout << "  speed          : " << r.value.mb_per_sec << " MB/s\n";
                std::cout << "  cache_bypassed : "
                          << (r.value.cache_bypassed ? "yes (O_DIRECT)" : "no (buffered)")
                          << "\n";
            }
            knst_file::remove_file(test_file);
            std::cout << "  test file : deleted\n";
        }
    }

    // =====================================================================
    section("15) Mount / unmount (safe demo)");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto disks = knst_devices::storage();
        bool demoed = false;

        for (auto& d : disks) {
            if (!d.is_removable) continue;
            if (d.partitions.empty()) continue;

            const auto& p = d.partitions[0];
            if (p.mount_point.empty()) {
                std::cout << "  attempting mount: " << p.device << "\n";
                auto mp = knst_sto::storage::mount(p.device);
                if (mp) {
                    std::cout << "  mounted at: " << mp.value << "\n";
                    std::cout << "  unmounting again...\n";
                    auto u = knst_sto::storage::unmount(p.device);
                    std::cout << "  unmount: "
                              << (u ? "ok" : u.error_string()) << "\n";
                } else {
                    std::cout << "  mount: " << mp.error_string() << "\n";
                }
                demoed = true;
            } else {
                std::cout << "  already mounted: " << p.device
                          << " → " << p.mount_point << "\n";
                std::cout << "  (skipping mount demo — safer)\n";
                demoed = true;
            }
            break;
        }
        if (!demoed)
            std::cout << "  (no unmounted removable disk — skipping)\n";
        std::cout << "  SAFETY: fixed disks are never mounted/unmounted here\n";
#else
        std::cout << "  mount / unmount: NotSupported on Windows\n";
        std::cout << "  (drive letters are always mounted — no mount concept)\n";
#endif
    }

    // =====================================================================
    section("16) Eject — safe demo");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto disks = knst_devices::storage();
        bool demoed = false;

        for (auto& d : disks) {
            if (!d.is_removable) continue;
            if (!d.partitions.empty() && d.partitions[0].is_boot) continue;

            std::cout << "  removable disk found: " << d.device << "\n";
            std::cout << "  (not actually ejecting — demo only)\n";
            std::cout << "  to actually eject:\n";
            std::cout << "    knst_sto::storage::eject(disk)\n";
            demoed = true;
            break;
        }
        if (!demoed)
            std::cout << "  (no removable disk — skipping)\n";
#else
        std::cout << "  eject_volume(drive) available\n";
        std::cout << "  Example: knst_sto::storage::eject_volume(u\"E\")\n";
        std::cout << "  (not actually ejecting — demo only)\n";
#endif
    }

       section("17) LUKS discovery");
    {
#if KNST_USING_PLATFORM_LINUX
        auto luks = knst_sto::storage::list_luks();
        if (!luks) {
            std::cout << "  list_luks: " << luks.error_string() << "\n";
            std::cout << "  (may need root: sudo)\n";
        } else if (luks.value.empty()) {
            std::cout << "  no LUKS containers configured\n";
        } else {
            uint32_t unlocked = 0, locked = 0;
            for (auto& c : luks.value) {
                if (c.is_unlocked) ++unlocked;
                else               ++locked;
            }
            std::cout << "  active (unlocked): " << unlocked << "\n";
            std::cout << "  configured (locked): " << locked << "\n\n";

            for (auto& c : luks.value) {
                std::cout << "  [" << (c.is_unlocked ? "ACTIVE" : "LOCKED") << "] "
                          << c.mapper_name;
                if (!c.device.empty()) std::cout << "  ← " << c.device;
                std::cout << "\n";

                if (!c.cipher.empty())
                    std::cout << "    cipher  : " << c.cipher << "\n";
                if (c.key_size > 0)
                    std::cout << "    keysize : " << c.key_size << " bits\n";
                if (!c.uuid.empty())
                    std::cout << "    uuid    : " << c.uuid << "\n";
            }
        }
#else
        std::cout << "  LUKS: Linux only (Windows uses BitLocker)\n";
#endif
    }

    // =====================================================================
    section("18) RAID discovery");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto ra = knst_sto::storage::list_raid();
        if (!ra) {
            std::cout << "  list_raid: " << ra.error_string() << "\n";
        } else if (ra.value.empty()) {
            std::cout << "  no md RAID arrays found\n";
        } else {
            for (auto& a : ra.value) {
                std::cout << "  " << a.name << "  level=" << a.level
                          << "  state=" << a.state << "\n";
                std::cout << "    members: " << a.active_devices
                          << "/" << a.total_devices << "\n";
            }
        }
#else
        std::cout << "  RAID: Linux md only in this demo\n";
#endif
    }

    // =====================================================================
    section("19) LVM discovery");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto lv = knst_sto::storage::list_lvm();
        if (!lv) {
            std::cout << "  list_lvm: " << lv.error_string() << "\n";
            std::cout << "  (LVM may not be installed — apt install lvm2)\n";
        } else if (lv.value.empty()) {
            std::cout << "  no LVM volumes found\n";
        } else {
            for (auto& v : lv.value) {
                std::cout << "  " << v.vg_name << "/" << v.name
                          << "  " << (v.is_active ? "active" : "inactive") << "\n";
            }
        }
#else
        std::cout << "  LVM: Linux only\n";
#endif
    }

    // =====================================================================
    section("20) BitLocker");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_WINDOWS
        auto bl = knst_sto::storage::list_bitlocker();
        if (!bl) {
            std::cout << "  list_bitlocker: " << bl.error_string() << "\n";
        } else if (bl.value.empty()) {
            std::cout << "  no BitLocker volumes\n";
        } else {
            for (auto& b : bl.value) {
                std::cout << "  " << b.drive << "  encrypted="
                          << (b.is_encrypted ? "yes" : "no")
                          << "  method=" << b.method
                          << "  " << b.percentage << "%\n";
            }
        }
#else
        std::cout << "  BitLocker: Windows only\n";
        std::cout << "  (Linux equivalent: LUKS — see section 17)\n";
#endif
    }

    // =====================================================================
    section("21) Directory watcher");
    // =====================================================================
    {
        knst_c16string watch_dir = knst_file::get_temp_dir();
        std::cout << "  watching: " << watch_dir << "\n";
        std::cout << "  polling for 2 seconds (200 ms interval)...\n";

        auto w = knst_sto::directory_watcher::open(watch_dir);
        if (!w) {
            std::cout << "  open: " << w.error_string() << "\n";
        } else {
            knst_c16string test_file = knst_file::join_path(watch_dir, u"knst_watch_test.txt");
            std::thread creator([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                knst_file::write_file_text(test_file, u"hello watcher");
            });

            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::seconds(2);
            int events_seen = 0;
            while (std::chrono::steady_clock::now() < deadline) {
                auto ev = w.value.wait(500);
                if (!ev) continue;
                events_seen++;
                const char* kind = "?";
                switch (ev.value.kind) {
                    case knst_sto::directory_watcher::event_kind::Created:  kind = "NEW "; break;
                    case knst_sto::directory_watcher::event_kind::Modified: kind = "MOD "; break;
                    case knst_sto::directory_watcher::event_kind::Deleted:  kind = "DEL "; break;
                }
                std::cout << "    [" << kind << "] " << ev.value.path << "\n";
                if (events_seen >= 3) break;
            }

            creator.join();
            knst_file::remove_file(test_file);
            w.value.stop();
            std::cout << "  events seen: " << events_seen << "\n";
        }
    }

    // =====================================================================
    section("22) Hot-plug wait");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        std::cout << "  waiting 500 ms for a block device change...\n";
        std::cout << "  (plug in a USB drive to trigger this)\n";
        auto ev = knst_sto::storage::wait_for_change(500);
        if (ev) {
            std::cout << "  event: " << ev.value.action
                      << "  " << ev.value.devname
                      << "  subsystem=" << ev.value.subsystem << "\n";
        } else {
            std::cout << "  wait_for_change: " << ev.error_string()
                      << " (no event within 500 ms — normal)\n";
        }
#else
        std::cout << "  Hot-plug wait: Linux only\n";
#endif
    }

    // =====================================================================
    section("23) Platform summary");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  Windows : WMI + IOCTL\n";
        std::cout << "  Supported: free_space, list_mounts, identity,\n";
        std::cout << "             eject_volume, BitLocker, dir_size, bench\n";
        std::cout << "  Not supported: mount/unmount, LUKS, RAID, LVM, hot-plug\n";
#else
        std::cout << "  Linux   : sysfs + udisks + smartctl\n";
        std::cout << "  Supported: free_space, list_mounts, identity,\n";
        std::cout << "             smart, health, io_stats, bench,\n";
        std::cout << "             mount/unmount/eject, LUKS, RAID, LVM, hot-plug\n";
#endif
    }

    // =====================================================================
    section("24) Graceful failure — errors are results");
    // =====================================================================
    {
        auto r1 = knst_sto::free_space(u"/__no_such_path__");
        std::cout << "  free_space(nonexistent)       : "
                  << (r1 ? "ok" : r1.error_string()) << "\n";

        auto r2 = knst_sto::directory_size(u"/__no_such_dir__");
        std::cout << "  directory_size(nonexistent)   : "
                  << (r2 ? "ok" : r2.error_string()) << "\n";

        auto r3 = knst_sto::storage::benchmark_read(u"/__no_such_file__");
        std::cout << "  benchmark_read(nonexistent)   : "
                  << (r3 ? "ok" : r3.error_string()) << "\n";

        auto r4 = knst_sto::find_mount_for_path(u"");
        std::cout << "  find_mount_for_path(\"\")       : "
                  << (r4 ? "ok" : r4.error_string()) << "\n";

        auto r5 = knst_sto::storage::benchmark_write(knst_file::get_temp_dir(), 0);
        std::cout << "  benchmark_write(size=0)       : "
                  << (r5 ? "ok" : r5.error_string()) << "\n";

        knst_storage_info bogus;
        bogus.device = u"/not/a/device";
        auto r6 = knst_sto::storage::smart(bogus);
        std::cout << "  smart(invalid_device)         : "
                  << (r6 ? "ok" : r6.error_string()) << "\n";
    }

    // =====================================================================
    section("25) Cleanup");
    // =====================================================================
    {
        std::cout << "  all temp files cleaned up\n";
        std::cout << "  no disks were modified\n";
        std::cout << "  benchmarks self-cleaned\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}