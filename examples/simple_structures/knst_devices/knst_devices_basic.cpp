/*
  knst_devices_basic.cpp

  Comprehensive demonstration of knst_devices — cross-platform hardware
  inventory. Every category the library exposes is covered, with detail
  on where each field originates (sysfs/proc/tools on Linux, WMI/Win32
  on Windows).

  Two additional helper passes are done on the caller side:
    - CPU total L3 is computed by walking sysfs index3 slices
    - PCI devices missing a name get a "vendor:device" fallback
    - Monitors missing a name get "MFG 0xNNNN" from the EDID
    - Storage models are trimmed (sysfs pads them)

  Sections are grouped:
    A. Processor & platform     — CPU, motherboard, memory
    B. Storage                  — disks + partitions
    C. Interconnects            — network, USB, PCI
    D. User I/O                 — monitors, input, audio
    E. Power & environment      — battery, sensors (grouped)
    F. Wireless & peripherals   — bluetooth, cameras, printers
    G. System identity          — system, OS, drivers
    H. GPU bridge
    I. Diagnostics              — failure handling, platform summary

  Every function returns a struct (or a vector) with a `valid` flag.
  Fields the OS didn't provide keep their default values — check the
  flag before trusting the data.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <cstdio>
#include <cstring>


// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}

static void subsection(const char* title) {
    std::cout << "\n  ── " << title << " ──\n";
}

// Trim ASCII whitespace from both ends of a c16string.
static knst_c16string trim16(const knst_c16string& s) {
    uint32_t a = 0, b = s.length();
    while (a < b && (s[a] == u' ' || s[a] == u'\t')) ++a;
    while (b > a && (s[b - 1] == u' ' || s[b - 1] == u'\t')) --b;
    return s.substr(a, b - a);
}

// Total L3 across all slices. On AMD Zen the per-core L3 is one CCX slice
// (e.g. 16 MB); total is slices × per-slice. Uses shared_cpu_list to
// deduplicate. Linux-only — Windows WMI already reports total L3, so this
// helper is not needed there (and would trigger -Wunused-function).
#if KNST_USING_PLATFORM_LINUX
static uint32_t cpu_total_l3_kb() {
    knst_vector<knst_c16string> seen;
    uint32_t total = 0;
    for (int cpu = 0; cpu < 256; ++cpu) {
        char p[256], buf[128];
        ::snprintf(p, sizeof(p),
            "/sys/devices/system/cpu/cpu%d/cache/index3/shared_cpu_list", cpu);
        int fd = ::open(p, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
        ::close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
            buf[--n] = '\0';
        knst_c16string lst(buf);

        bool dup = false;
        for (auto& s : seen) if (s == lst) { dup = true; break; }
        if (dup) continue;
        seen.push_back(lst);

        ::snprintf(p, sizeof(p),
            "/sys/devices/system/cpu/cpu%d/cache/index3/size", cpu);
        fd = ::open(p, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        n = ::read(fd, buf, sizeof(buf) - 1);
        ::close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';
        char* end = nullptr;
        unsigned long kb = ::strtoul(buf, &end, 10);
        if (end && (*end == 'M' || *end == 'm')) kb *= 1024;
        else if (end && (*end == 'G' || *end == 'g')) kb *= 1024 * 1024;
        total += static_cast<uint32_t>(kb);
    }
    return total;
}
#endif // KNST_USING_PLATFORM_LINUX

// Human-readable byte size.
static knst_c16string human_gb(uint64_t b) {
    char tmp[64];
    ::snprintf(tmp, sizeof(tmp), "%.1f GB", b / (1024.0 * 1024.0 * 1024.0));
    return knst_c16string(tmp);
}

// Best-effort name for a PCI device when the driver doesn't expose one.
static knst_c16string pci_display_name(const knst_pci_info& p) {
    if (!p.device_name.empty()) return p.device_name;
    char tmp[64];
    ::snprintf(tmp, sizeof(tmp), "device %04x:%04x", p.vendor_id, p.device_id);
    return knst_c16string(tmp);
}

// Synthesize a monitor name from the EDID if the OS didn't provide one.
static knst_c16string monitor_display_name(const knst_monitor_info& m) {
    if (!m.name.empty()) return m.name;
    char tmp[64];
    if (!m.manufacturer.empty() && m.product_code)
        ::snprintf(tmp, sizeof(tmp), "%s 0x%04x",
                   reinterpret_cast<const char*>(knst_dev::to_utf8(m.manufacturer).data()),
                   m.product_code);
    else
        ::snprintf(tmp, sizeof(tmp), "Monitor %u", m.index);
    return knst_c16string(tmp);
}


int main() {

    // =====================================================================
    section("A1) CPU");
    // =====================================================================
    {
        knst_cpu_info c = knst_devices::cpu();

        if (!c.valid) {
            std::cout << "  cpu() : not available\n";
        } else {
            std::cout << "  vendor        : " << c.vendor        << "\n";
            std::cout << "  model         : " << c.model_name    << "\n";
            std::cout << "  architecture  : " << c.architecture  << "\n";
            std::cout << "  physical cores: " << c.physical_cores << "\n";
            std::cout << "  logical cores : " << c.logical_cores  << "\n";
            std::cout << "  sockets       : " << c.socket_count   << "\n";

            subsection("frequency");
            std::cout << "  base MHz      : " << c.base_mhz    << "\n";
            std::cout << "  max MHz       : " << c.max_mhz     << "\n";
            std::cout << "  current MHz   : " << c.current_mhz << "\n";

                        subsection("cache");
#if KNST_USING_PLATFORM_WINDOWS
            // Windows WMI (Win32_Processor) reports TOTAL L2/L3 sizes.
            // L1 is not exposed by WMI at all.
            std::cout << "  L1 (total)    : ";
            if (c.cache_l1_kb) std::cout << c.cache_l1_kb << " KB\n";
            else               std::cout << "(not reported by WMI)\n";
            std::cout << "  L2 (total)    : " << c.cache_l2_kb << " KB\n";
            std::cout << "  L3 (total)    : " << c.cache_l3_kb << " KB\n";
#else
            // Linux sysfs reports PER-CORE (cpu0 slice) sizes.
            uint32_t total_l3 = cpu_total_l3_kb();
            std::cout << "  L1 (per-core) : " << c.cache_l1_kb << " KB\n";
            std::cout << "  L2 (per-core) : " << c.cache_l2_kb << " KB\n";
            std::cout << "  L3 (per-core) : " << c.cache_l3_kb << " KB\n";
            if (total_l3 > c.cache_l3_kb)
                std::cout << "  L3 (total)    : " << total_l3
                          << " KB  (" << (total_l3 / 1024) << " MB, "
                          << (total_l3 / (c.cache_l3_kb ? c.cache_l3_kb : 1))
                          << " slices)\n";
#endif

            subsection("load & features");
            std::cout << "  load 1/5/15   : " << c.load_avg_1
                      << " / " << c.load_avg_5
                      << " / " << c.load_avg_15 << "\n";
            std::cout << "  flags         : "
                      << (c.has_sse4 ? "SSE4 " : "")
                      << (c.has_avx  ? "AVX "  : "")
                      << (c.has_avx2 ? "AVX2 " : "")
                      << (c.has_avx512 ? "AVX512 " : "")
                      << (c.has_aes  ? "AES "  : "")
                      << (c.has_vmx  ? "VMX "  : "")
                      << (c.has_iommu ? "IOMMU " : "")
                      << (c.has_hyperthreading ? "HT" : "") << "\n";
        }
    }

    // =====================================================================
    section("A2) Motherboard & BIOS");
    // =====================================================================
    {
        knst_motherboard_info m = knst_devices::motherboard();

        std::cout << "  manufacturer  : " << m.manufacturer  << "\n";
        std::cout << "  product       : " << m.product       << "\n";
        std::cout << "  version       : " << m.version       << "\n";

        subsection("BIOS / firmware");
        std::cout << "  bios vendor   : " << m.bios_vendor   << "\n";
        std::cout << "  bios version  : " << m.bios_version  << "\n";
        std::cout << "  bios date     : " << m.bios_date     << "\n";
        std::cout << "  UEFI boot     : " << (m.uefi_boot   ? "yes" : "no") << "\n";
        std::cout << "  Secure Boot   : " << (m.secure_boot ? "yes" : "no") << "\n";
        std::cout << "  TPM present   : " << (m.has_tpm     ? "yes" : "no") << "\n";
        if (!m.tpm_version.empty())
            std::cout << "  TPM version   : " << m.tpm_version << "\n";

        subsection("chassis");
        std::cout << "  chassis type  : " << m.chassis_type   << "\n";
        std::cout << "  chassis serial: " << m.chassis_serial << "\n";
    }

    // =====================================================================
    section("A3) Memory");
    // =====================================================================
    {
        knst_memory_info m = knst_devices::memory();

        std::cout << "  total     : " << human_gb(m.total_bytes)     << "\n";
        std::cout << "  used      : " << human_gb(m.used_bytes)      << "\n";
        std::cout << "  free      : " << human_gb(m.free_bytes)      << "\n";
        std::cout << "  available : " << human_gb(m.available_bytes) << "\n";
        std::cout << "  cached    : " << human_gb(m.cached_bytes)    << "\n";
        std::cout << "  buffers   : " << human_gb(m.buffers_bytes)   << "\n";

        if (m.has_swap) {
            std::cout << "  swap      : " << human_gb(m.swap_used)
                      << " / " << human_gb(m.swap_total) << "\n";
        }

        if (m.has_modules) {
            subsection("installed modules");
            std::cout << "  slots used : " << m.slots_used
                      << " / " << m.slot_count << "\n";
            for (uint32_t i = 0; i < m.modules.size() && i < 8; ++i) {
                const auto& mod = m.modules[i];
                std::cout << "  [" << i << "] " << human_gb(mod.size_bytes)
                          << "  @ " << mod.speed_mhz << " MHz";
                if (mod.configured_mhz && mod.configured_mhz != mod.speed_mhz)
                    std::cout << " (running " << mod.configured_mhz << ")";
                std::cout << "  " << mod.type
                          << "  " << mod.form_factor << "\n";
                if (!mod.manufacturer.empty())
                    std::cout << "       vendor : " << mod.manufacturer << "\n";
                if (!mod.part_number.empty())
                    std::cout << "       part   : " << mod.part_number  << "\n";
                if (!mod.serial.empty())
                    std::cout << "       serial : " << mod.serial       << "\n";
            }
        }
    }

    // =====================================================================
    section("B4) Storage devices");
    // =====================================================================
    {
        knst_vector<knst_storage_info> disks = knst_devices::storage();

        std::cout << "  disks found : " << disks.size() << "\n";
        for (uint32_t i = 0; i < disks.size(); ++i) {
            const auto& s = disks[i];
            knst_c16string model = trim16(s.model);

            std::cout << "\n  [" << i << "] " << s.device << "\n";
            std::cout << "      model        : " << model << "\n";
            if (!s.vendor.empty())
                std::cout << "      vendor       : " << trim16(s.vendor) << "\n";
            if (!s.serial.empty())
                std::cout << "      serial       : " << trim16(s.serial) << "\n";
            if (!s.firmware.empty())
                std::cout << "      firmware     : " << trim16(s.firmware) << "\n";
            std::cout << "      size         : " << human_gb(s.total_bytes) << "\n";
            std::cout << "      solid state  : " << (s.is_solid_state ? "yes" : "no") << "\n";
            std::cout << "      removable    : " << (s.is_removable   ? "yes" : "no") << "\n";
            std::cout << "      system disk  : " << (s.is_system_disk ? "yes" : "no") << "\n";
            if (s.has_temperature)
                std::cout << "      temp         : " << s.temperature_c << " C\n";

            if (s.has_partitions) {
                std::cout << "      partitions   : " << s.partitions.size() << "\n";
                for (uint32_t j = 0; j < s.partitions.size(); ++j) {
                    const auto& p = s.partitions[j];
                    std::cout << "        " << p.device
                              << "  " << p.mount_point
                              << "  (" << p.fs_type << ")";
                    if (p.total_bytes)
                        std::cout << "  " << human_gb(p.used_bytes)
                                  << " / " << human_gb(p.total_bytes);
                    if (p.is_boot) std::cout << "  [boot]";
                    std::cout << "\n";
                }
            }
        }
    }

    // =====================================================================
    section("C6) Network");
    // =====================================================================
    {
        knst_vector<knst_network_info> nets = knst_devices::network();

        std::cout << "  interfaces found : " << nets.size() << "\n";
        for (uint32_t i = 0; i < nets.size(); ++i) {
            const auto& n = nets[i];
            std::cout << "\n  [" << i << "] " << n.name;
            if (!n.description.empty()) std::cout << "  (" << n.description << ")";
            std::cout << "\n";
            std::cout << "      MAC      : " << n.mac    << "\n";
            if (!n.ipv4.empty())   std::cout << "      IPv4     : " << n.ipv4   << "\n";
            if (!n.ipv6.empty())   std::cout << "      IPv6     : " << n.ipv6   << "\n";
            if (!n.gateway.empty())std::cout << "      gateway  : " << n.gateway << "\n";
            if (!n.dns.empty())    std::cout << "      DNS      : " << n.dns << "\n";
            std::cout << "      up       : " << (n.is_up      ? "yes" : "no") << "\n";
            std::cout << "      loopback : " << (n.is_loopback? "yes" : "no") << "\n";
            std::cout << "      wireless : " << (n.is_wireless? "yes" : "no") << "\n";
            if (n.mtu)       std::cout << "      MTU      : " << n.mtu << "\n";
            if (n.speed_mbps)std::cout << "      speed    : " << n.speed_mbps << " Mbps\n";
            std::cout << "      rx bytes : " << n.rx_bytes << "\n";
            std::cout << "      tx bytes : " << n.tx_bytes << "\n";
            if (n.has_wifi) {
                std::cout << "      SSID     : " << n.ssid << "\n";
                std::cout << "      signal   : " << n.signal_dbm << " dBm\n";
                std::cout << "      channel  : " << n.channel
                          << " (" << n.frequency_mhz << " MHz)\n";
                std::cout << "      security : " << n.security << "\n";
            }
        }
    }

    // =====================================================================
    section("C7) USB");
    // =====================================================================
    {
        knst_vector<knst_usb_info> devs = knst_devices::usb();

        std::cout << "  USB devices found : " << devs.size() << "\n";
        for (uint32_t i = 0; i < devs.size(); ++i) {
            const auto& u = devs[i];
            std::cout << "  [" << i << "] " << std::hex << u.vid << ":" << u.pid << std::dec;
            if (!u.manufacturer.empty()) std::cout << "  " << u.manufacturer;
            if (!u.product_name.empty()) std::cout << "  " << u.product_name;
            if (u.is_root_hub)           std::cout << "  [root hub]";
            else if (u.is_hub)           std::cout << "  [hub]";
            std::cout << "\n";
            if (!u.serial.empty())       std::cout << "      serial  : " << u.serial << "\n";
            if (!u.usb_version.empty())  std::cout << "      version : " << u.usb_version << "\n";
            if (u.max_speed_mbps)        std::cout << "      speed   : " << u.max_speed_mbps << " Mbps\n";
        }
    }

    // =====================================================================
    section("C8) PCI");
    // =====================================================================
    {
        knst_vector<knst_pci_info> devs = knst_devices::pci();

        std::cout << "  PCI devices found : " << devs.size() << "\n";

        // Group by class (video, storage, network, audio, bridge, ...)
        struct class_summary { knst_c16string name; uint32_t count = 0; };
        knst_vector<class_summary> groups;

        for (uint32_t i = 0; i < devs.size(); ++i) {
            const auto& p = devs[i];
            knst_c16string cls = p.class_name.empty() ? u"Unclassified" : p.class_name;

            bool found = false;
            for (auto& g : groups) if (g.name == cls) { g.count++; found = true; break; }
            if (!found) {
                class_summary s;
                s.name = cls;
                s.count = 1;
                groups.push_back(std::move(s));
            }
        }

        std::cout << "\n  by class:\n";
        for (auto& g : groups)
            std::cout << "      " << g.name << "  (" << g.count << ")\n";

        std::cout << "\n  detail (first 10):\n";
        for (uint32_t i = 0; i < devs.size() && i < 10; ++i) {
            const auto& p = devs[i];
            std::cout << "  [" << i << "] " << std::hex << p.vendor_id
                      << ":" << p.device_id << std::dec << "  ";
            if (!p.vendor_name.empty()) std::cout << p.vendor_name << "  ";
            std::cout << pci_display_name(p) << "\n";
            if (!p.driver.empty())
                std::cout << "      driver : " << p.driver << "\n";
            if (p.pcie_gen)
                std::cout << "      PCIe   : Gen" << p.pcie_gen
                          << " x" << p.pcie_width << "\n";
            if (p.iommu_group >= 0)
                std::cout << "      IOMMU  : group " << p.iommu_group << "\n";
        }
        if (devs.size() > 10)
            std::cout << "  ... " << (devs.size() - 10) << " more\n";
    }

    // =====================================================================
    section("D9) Monitors");
    // =====================================================================
    {
        knst_vector<knst_monitor_info> mons = knst_devices::monitors();

        std::cout << "  monitors found : " << mons.size() << "\n";
        for (uint32_t i = 0; i < mons.size(); ++i) {
            const auto& m = mons[i];
            std::cout << "\n  [" << i << "] " << monitor_display_name(m) << "\n";
            std::cout << "      manufacturer : " << m.manufacturer
                      << " (" << m.year << " week " << m.week << ")\n";
            if (!m.serial.empty())
                std::cout << "      serial       : " << m.serial << "\n";

            std::cout << "      connection   : ";
            switch (m.connection) {
                case knst_monitor_conn::HDMI:        std::cout << "HDMI"; break;
                case knst_monitor_conn::DisplayPort: std::cout << "DisplayPort"; break;
                case knst_monitor_conn::DVI:         std::cout << "DVI"; break;
                case knst_monitor_conn::VGA:         std::cout << "VGA"; break;
                case knst_monitor_conn::USB_C:       std::cout << "USB-C"; break;
                case knst_monitor_conn::eDP:         std::cout << "eDP"; break;
                case knst_monitor_conn::LVDS:        std::cout << "LVDS"; break;
                default:                             std::cout << "Unknown"; break;
            }
            std::cout << "\n";

            std::cout << "      native       : " << m.native_width  << "x" << m.native_height  << "\n";
            std::cout << "      current      : " << m.current_width << "x" << m.current_height << "\n";
            std::cout << "      refresh      : " << m.refresh_hz << " Hz\n";
            std::cout << "      size         : " << m.diagonal_inch << " inch";
            if (m.physical_width_mm > 0)
                std::cout << "  (" << m.physical_width_mm << "x" << m.physical_height_mm << " mm)";
            std::cout << "\n";
            if (m.gamma > 0)
                std::cout << "      gamma        : " << m.gamma << "\n";
            std::cout << "      primary      : " << (m.is_primary ? "yes" : "no") << "\n";
            if (m.brightness > 0)
                std::cout << "      brightness   : " << m.brightness << "%\n";
        }
    }

    // =====================================================================
    section("D10) Input devices");
    // =====================================================================
    {
        knst_vector<knst_input_info> devs = knst_devices::input();

        std::cout << "  input devices found : " << devs.size() << "\n";

        // Group by type
        struct type_count { const char* label; uint32_t n = 0; };
        type_count counts[] = {
            {"Keyboard",     0},
            {"Mouse",        0},
            {"Touchpad",     0},
            {"Touchscreen",  0},
            {"Joystick",     0},
            {"Gamepad",      0},
            {"Tablet",       0},
            {"Remote",       0},
            {"Other",        0}
        };
        for (const auto& d : devs) {
            switch (d.type) {
                case knst_input_type::Keyboard:    counts[0].n++; break;
                case knst_input_type::Mouse:       counts[1].n++; break;
                case knst_input_type::Touchpad:    counts[2].n++; break;
                case knst_input_type::Touchscreen: counts[3].n++; break;
                case knst_input_type::Joystick:    counts[4].n++; break;
                case knst_input_type::Gamepad:     counts[5].n++; break;
                case knst_input_type::Tablet:      counts[6].n++; break;
                case knst_input_type::Remote:      counts[7].n++; break;
                default:                           counts[8].n++; break;
            }
        }
        std::cout << "  breakdown : ";
        bool first = true;
        for (auto& c : counts) {
            if (c.n == 0) continue;
            if (!first) std::cout << ", ";
            std::cout << c.label << " x" << c.n;
            first = false;
        }
        std::cout << "\n";

        for (uint32_t i = 0; i < devs.size(); ++i) {
            const auto& d = devs[i];
            std::cout << "\n  [" << i << "] ";
            switch (d.type) {
                case knst_input_type::Keyboard:    std::cout << "[Keyboard]   "; break;
                case knst_input_type::Mouse:       std::cout << "[Mouse]      "; break;
                case knst_input_type::Touchpad:    std::cout << "[Touchpad]   "; break;
                case knst_input_type::Touchscreen: std::cout << "[Touchscreen]"; break;
                case knst_input_type::Joystick:    std::cout << "[Joystick]   "; break;
                case knst_input_type::Gamepad:     std::cout << "[Gamepad]    "; break;
                case knst_input_type::Tablet:      std::cout << "[Tablet]     "; break;
                case knst_input_type::Remote:      std::cout << "[Remote]     "; break;
                default:                            std::cout << "[Other]      "; break;
            }
            std::cout << d.name << "\n";
            if (d.vid || d.pid)
                std::cout << "        VID:PID = " << std::hex << d.vid << ":" << d.pid
                          << std::dec << "\n";
            if (d.num_buttons > 0) std::cout << "        buttons = " << d.num_buttons << "\n";
            if (!d.device_path.empty()) std::cout << "        path    = " << d.device_path << "\n";
        }
    }

    // =====================================================================
    section("D11) Audio");
    // =====================================================================
    {
        knst_audio_info a = knst_devices::audio();

        std::cout << "  default sink   : " << a.default_sink   << "\n";
        std::cout << "  default source : " << a.default_source << "\n";
        std::cout << "  devices        : " << a.devices.size() << "\n";

        subsection("outputs (sinks)");
        uint32_t out_count = 0;
        for (const auto& d : a.devices) {
            if (!d.is_output) continue;
            out_count++;
            std::cout << "  " << d.description;
            if (d.is_default) std::cout << "  [default]";
            std::cout << "\n";
            if (!d.name.empty()) std::cout << "      id = " << d.name << "\n";
        }
        if (out_count == 0) std::cout << "  (none)\n";

        subsection("inputs (sources)");
        uint32_t in_count = 0;
        for (const auto& d : a.devices) {
            if (!d.is_input) continue;
            in_count++;
            std::cout << "  " << d.description;
            if (d.name.contains(u".monitor"))
                std::cout << "  [monitor]";
            if (d.is_default) std::cout << "  [default]";
            std::cout << "\n";
            if (!d.name.empty()) std::cout << "      id = " << d.name << "\n";
        }
        if (in_count == 0) std::cout << "  (none)\n";
    }

    // =====================================================================
    section("E12) Power / Battery");
    // =====================================================================
    {
        knst_power_info p = knst_devices::power();

        std::cout << "  has battery : " << (p.has_battery ? "yes" : "no") << "\n";
        std::cout << "  AC online   : " << (p.ac_connected ? "yes" : "no") << "\n";

        if (p.batteries.empty()) {
            std::cout << "  (desktop / no system battery)\n";
        } else {
            for (uint32_t i = 0; i < p.batteries.size(); ++i) {
                const auto& b = p.batteries[i];
                std::cout << "\n  [" << i << "] " << b.name << "\n";
                std::cout << "      charge   : " << b.percent << "%\n";
                if (b.health_percent)
                    std::cout << "      health   : " << b.health_percent << "%\n";
                std::cout << "      state    : " << static_cast<int>(b.state);
                switch (b.state) {
                    case knst_battery_state::Charging:    std::cout << " (charging)"; break;
                    case knst_battery_state::Discharging: std::cout << " (discharging)"; break;
                    case knst_battery_state::Full:        std::cout << " (full)"; break;
                    case knst_battery_state::NotCharging: std::cout << " (not charging)"; break;
                    default: break;
                }
                std::cout << "\n";
                if (b.voltage_mv)
                    std::cout << "      voltage  : " << b.voltage_mv << " mV\n";
                if (b.cycle_count)
                    std::cout << "      cycles   : " << b.cycle_count << "\n";
            }
        }
    }

    // =====================================================================
    section("E13) Sensors (grouped by type)");
    // =====================================================================
    {
        knst_vector<knst_sensor_info> s = knst_devices::sensors();

        std::cout << "  sensors found : " << s.size() << "\n";

        auto print_group = [&](knst_sensor_type type, const char* label) {
            uint32_t n = 0;
            for (const auto& x : s) if (x.type == type) n++;
            if (n == 0) return;
            subsection(label);
            for (const auto& x : s) {
                if (x.type != type) continue;
                std::cout << "  " << x.chip;
                if (!x.name.empty()) std::cout << " / " << x.name;
                std::cout << " : " << x.value << " " << x.unit << "\n";
            }
        };

        print_group(knst_sensor_type::Temperature, "temperature");
        print_group(knst_sensor_type::Fan,         "fans");
        print_group(knst_sensor_type::Voltage,     "voltages");
        print_group(knst_sensor_type::Current,     "current");
        print_group(knst_sensor_type::Power,       "power");
    }

    // =====================================================================
    section("F14) Bluetooth");
    // =====================================================================
    {
        knst_bluetooth_info b = knst_devices::bluetooth();

        std::cout << "  adapter   : " << (b.has_adapter ? "yes" : "no") << "\n";
        if (b.has_adapter) {
            std::cout << "  name      : " << b.adapter_name    << "\n";
            std::cout << "  address   : " << b.adapter_address << "\n";
            std::cout << "  powered   : " << (b.powered      ? "yes" : "no") << "\n";
            std::cout << "  discover. : " << (b.discoverable ? "yes" : "no") << "\n";
        } else {
            std::cout << "  (no Bluetooth hardware present)\n";
        }
        std::cout << "  devices   : " << b.devices.size() << "\n";
        for (uint32_t i = 0; i < b.devices.size() && i < 6; ++i) {
            const auto& d = b.devices[i];
            std::cout << "  [" << i << "] " << d.name << "  " << d.address;
            if (d.connected) std::cout << "  [connected]";
            else if (d.paired) std::cout << "  [paired]";
            if (d.rssi != 0) std::cout << "  rssi=" << d.rssi << " dBm";
            std::cout << "\n";
        }
    }

    // =====================================================================
    section("F15) Cameras");
    // =====================================================================
    {
        knst_vector<knst_camera_info> cams = knst_devices::cameras();

        std::cout << "  cameras found : " << cams.size() << "\n";
        for (uint32_t i = 0; i < cams.size(); ++i) {
            const auto& c = cams[i];
            std::cout << "  [" << i << "] " << c.name << "\n";
            std::cout << "        path = " << c.device_path << "\n";
            if (c.vid || c.pid)
                std::cout << "        VID:PID = " << std::hex << c.vid << ":" << c.pid
                          << std::dec << "\n";
            if (c.max_width > 0)
                std::cout << "        max = " << c.max_width << "x" << c.max_height << "\n";
        }
    }

    // =====================================================================
    section("F16) Printers");
    // =====================================================================
    {
        knst_vector<knst_printer_info> pr = knst_devices::printers();

        std::cout << "  printers found : " << pr.size() << "\n";
        for (uint32_t i = 0; i < pr.size(); ++i) {
            std::cout << "  [" << i << "] " << pr[i].name
                      << "  status=" << pr[i].status
                      << (pr[i].is_default ? "  [default]" : "")
                      << (pr[i].is_shared  ? "  [shared]"  : "")
                      << "\n";
        }
    }

    // =====================================================================
    section("G17) System");
    // =====================================================================
    {
        knst_system_info s = knst_devices::system();

        std::cout << "  manufacturer : " << s.manufacturer << "\n";
        std::cout << "  product      : " << s.product_name << "\n";
        std::cout << "  version      : " << s.version      << "\n";
        if (!s.serial.empty()) std::cout << "  serial       : " << s.serial << "\n";
        if (!s.sku.empty())    std::cout << "  SKU          : " << s.sku << "\n";
        if (!s.family.empty()) std::cout << "  family       : " << s.family << "\n";
        std::cout << "  chassis      : " << s.chassis_type << "\n";
        std::cout << "  is VM        : " << (s.is_virtual_machine ? "yes" : "no") << "\n";
        std::cout << "  is laptop    : " << (s.is_laptop          ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("G18) OS");
    // =====================================================================
    {
        knst_os_info o = knst_devices::os();

        std::cout << "  os           : " << o.os_name        << " " << o.os_version << "\n";
        std::cout << "  kernel       : " << o.kernel_name    << " " << o.kernel_version << "\n";
        std::cout << "  arch         : " << o.kernel_arch    << "\n";
        std::cout << "  hostname     : " << o.hostname       << "\n";
        std::cout << "  user         : " << o.user           << "\n";
        std::cout << "  home         : " << o.home_dir       << "\n";
        std::cout << "  shell        : " << o.shell          << "\n";
        std::cout << "  timezone     : " << o.timezone       << "\n";
        std::cout << "  uptime       : " << o.uptime_seconds << " s\n";
        if (o.boot_time_ms)
            std::cout << "  boot time    : " << o.boot_time_ms << " (unix ms)\n";
        std::cout << "  page size    : " << o.page_size      << " bytes\n";
    }

    // =====================================================================
    section("G19) Kernel drivers");
    // =====================================================================
    {
        knst_vector<knst_driver_info> d = knst_devices::drivers();

        std::cout << "  drivers loaded : " << d.size() << "\n";

        // Biggest drivers by size
        knst_vector<knst_driver_info> by_size = d;
        for (uint32_t i = 1; i < by_size.size(); ++i) {
            knst_driver_info key = by_size[i];
            int j = static_cast<int>(i) - 1;
            while (j >= 0 && by_size[j].size_bytes < key.size_bytes) {
                by_size[j + 1] = by_size[j];
                --j;
            }
            by_size[j + 1] = key;
        }

        std::cout << "\n  top 8 by size:\n";
        for (uint32_t i = 0; i < by_size.size() && i < 8; ++i) {
            const auto& drv = by_size[i];
            std::cout << "    " << drv.name
                      << "  " << (drv.size_bytes / 1024) << " KB"
                      << "  used_by=" << drv.used_by_count << "\n";
        }
    }

    // =====================================================================
    section("H20) GPU bridge (knst_gpu)");
    // =====================================================================
    {
        std::cout << "  has_gpu()    : " << (knst_devices::has_gpu() ? "yes" : "no") << "\n";
        std::cout << "  gpu_count()  : " << knst_devices::gpu_count()  << "\n";

        knst_c16string drv = knst_devices::nvidia_driver_version();
        if (!drv.empty())  std::cout << "  nvidia drv   : " << drv << "\n";
        knst_c16string cuda = knst_devices::cuda_version();
        if (!cuda.empty()) std::cout << "  cuda version : " << cuda << "\n";

        knst_vector<knst_gpu_info> gpus = knst_devices::gpus();
        std::cout << "  gpus()       : " << gpus.size() << " entries\n";

        for (uint32_t i = 0; i < gpus.size(); ++i) {
            const auto& g = gpus[i];
            std::cout << "\n  [" << i << "] " << g.name
                      << "  (" << knst_gpu_vendor_string(g.vendor) << ")\n";
            if (!g.driver_version.empty()) std::cout << "      driver     : " << g.driver_version << "\n";
            if (!g.uuid.empty())           std::cout << "      uuid       : " << g.uuid << "\n";
            if (!g.vbios_version.empty())  std::cout << "      vbios      : " << g.vbios_version << "\n";
            if (g.has_memory) {
                std::cout << "      VRAM total : " << (g.memory_total / (1024ULL * 1024)) << " MB\n";
                std::cout << "      VRAM used  : " << (g.memory_used  / (1024ULL * 1024)) << " MB\n";
                std::cout << "      VRAM free  : " << (g.memory_free  / (1024ULL * 1024)) << " MB\n";
            }
            if (g.has_utilization) {
                std::cout << "      GPU load   : " << g.utilization_gpu << " %\n";
                if (g.utilization_mem > 0)
                    std::cout << "      Mem load   : " << g.utilization_mem << " %\n";
            }
            if (g.has_temperature)
                std::cout << "      temp       : " << g.temperature_core << " C\n";
            if (g.has_power)
                std::cout << "      power      : " << (g.power_usage_mw / 1000)
                          << " W (limit " << (g.power_limit_mw / 1000) << " W)\n";
            if (g.has_clocks) {
                std::cout << "      core clock : " << g.clock_core_mhz << " MHz\n";
                if (g.clock_mem_mhz > 0)
                    std::cout << "      mem clock  : " << g.clock_mem_mhz  << " MHz\n";
            }
            if (g.has_fan)
                std::cout << "      fan        : " << g.fan_percent << " %\n";
            if (g.has_pcie)
                std::cout << "      PCIe       : Gen" << g.pcie_gen
                          << " x" << g.pcie_width << "\n";
        }

        knst_vector<knst_gpu_process> gp = knst_devices::gpu_processes();
        uint64_t total_vram = 0;
        for (const auto& p : gp) total_vram += p.vram_bytes;

        std::cout << "\n  GPU-using processes : " << gp.size()
                  << "  (total VRAM " << (total_vram / (1024ULL * 1024)) << " MB)\n";
        for (uint32_t i = 0; i < gp.size() && i < 8; ++i) {
            const auto& p = gp[i];
            std::cout << "    pid=" << p.pid
                      << "  gpu=" << p.gpu_index
                      << "  vram=";
            if (p.vram_bytes == 0) std::cout << "N/A";
            else                   std::cout << (p.vram_bytes / (1024ULL * 1024)) << " MB";
            std::cout << "  " << p.name << "\n";
        }
    }

    // =====================================================================
    section("I21) Failure handling");
    // =====================================================================
    {
        // These are NOT errors — they exercise the "not available" path
        // that all knst_devices functions use when a category is missing.

        knst_c16string fake_dev = u"/dev/does_not_exist_999";
        bool exists = knst_file::path_exists(fake_dev);
        std::cout << "  path_exists(\"" << fake_dev << "\") : "
                  << (exists ? "yes" : "no") << "  (expected: no)\n";

        knst_file empty_file;
        std::cout << "  default knst_file.valid            : "
                  << (empty_file.exists ? "yes" : "no") << "  (expected: no)\n";

        std::cout << "\n  All knst_devices functions:\n";
        std::cout << "    - never throw\n";
        std::cout << "    - return structs with `valid = false` on failure\n";
        std::cout << "    - return empty vectors when a category has no items\n";
        std::cout << "    - fall back to empty strings for missing text fields\n";
    }

    // =====================================================================
    section("I22) Platform differences summary");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  CPU           : WMI Win32_Processor\n";
        std::cout << "  Motherboard   : WMI Win32_BaseBoard + Win32_BIOS\n";
        std::cout << "  Memory        : WMI Win32_PhysicalMemory + GlobalMemoryStatusEx\n";
        std::cout << "  Storage       : WMI Win32_DiskDrive + MSFT_PhysicalDisk\n";
        std::cout << "  Network       : GetAdaptersAddresses + WLAN API\n";
        std::cout << "  USB           : WMI Win32_PnPEntity (USB%)\n";
        std::cout << "  PCI           : WMI Win32_PnPEntity (PCI%)\n";
        std::cout << "  Monitors      : EnumDisplayDevices + registry EDID\n";
        std::cout << "  Input         : GetRawInputDeviceList + HID API\n";
        std::cout << "  Audio         : IMMDeviceEnumerator (COM)\n";
        std::cout << "  Power         : GetSystemPowerStatus\n";
        std::cout << "  Sensors       : WMI MSAcpi_ThermalZoneTemperature + Win32_Fan/Probe\n";
        std::cout << "  Bluetooth     : BluetoothFindFirstRadio\n";
        std::cout << "  Cameras       : WMI Win32_PnPEntity (PNPClass='Camera')\n";
        std::cout << "  Printers      : WMI Win32_Printer\n";
        std::cout << "  Drivers       : WMI Win32_SystemDriver\n";
#else
        std::cout << "  CPU           : /proc/cpuinfo + /sys/devices/system/cpu\n";
        std::cout << "  Motherboard   : dmidecode + /sys/class/dmi/id/*\n";
        std::cout << "  Memory        : /proc/meminfo + dmidecode -t memory\n";
        std::cout << "  Storage       : /sys/block + /proc/mounts (LVM/LUKS aware)\n";
        std::cout << "  Network       : getifaddrs + /sys/class/net + iw\n";
        std::cout << "  USB           : /sys/bus/usb/devices\n";
        std::cout << "  PCI           : /sys/bus/pci/devices + lspci -mm\n";
        std::cout << "  Monitors      : /sys/class/drm/*/edid\n";
        std::cout << "  Input         : /sys/class/input/event*\n";
        std::cout << "  Audio         : pactl (PulseAudio / PipeWire-pulse)\n";
        std::cout << "  Power         : /sys/class/power_supply (System scope only)\n";
        std::cout << "  Sensors       : /sys/class/hwmon (lm-sensors)\n";
        std::cout << "  Bluetooth     : bluetoothctl (BlueZ)\n";
        std::cout << "  Cameras       : /sys/class/video4linux (V4L2 caps checked)\n";
        std::cout << "  Printers      : lpstat (CUPS)\n";
        std::cout << "  Drivers       : /proc/modules\n";
        std::cout << "\n  Helper tools used:\n";
        std::cout << "    dmidecode, lspci, pactl, lpstat, iw, rfkill\n";
        std::cout << "    (each is optional — the library falls back gracefully)\n";
#endif
    }

    std::cout << "\nDone.\n";
    return 0;
}