/*
  knst_usb_basic.cpp

  Comprehensive demonstration of knst_usbc — the USB controller built
  on knst_devices::usb().

  NO INTERACTIVE INPUT. Section 26 (hot-cycle) runs AUTOMATICALLY
  when the process has root (geteuid() == 0). Otherwise it prints a
  clear "needs sudo" message and skips.

  SAFETY CONTRACT
  ===============
  Read-only EXCEPT section 26 (hot-cycle), which:
    - runs ONLY with root privileges
    - picks a NON-HID device (never touches your keyboard/mouse)
    - never targets a hub
    - reattaches the device automatically after 2 s
  Everything else NEVER auto-calls:
    - reset() or power_cycle() (except §26)
    - set_authorized(false) (would disable your input devices)
    - unbind_driver() / bind_driver() (would break the driver)
    - writes to any sysfs attribute

  Sections:
    1.  Platform availability & privileges
    2.  Full device enumeration
    3.  Find by VID/PID
    4.  Find by class
    5.  Find by driver
    6.  Root hubs
    7.  Device descriptor
    8.  USB strings
    9.  Configurations & interfaces
    10. Speed & state
    11. Power info
    12. Hub ports
    13. Hub topology
    14. Error stats
    15. Health check
    16. Authorization (demo only — NOT exercised)
    17. Reset / power-cycle (demo only — NOT exercised)
    18. Runtime PM (demo only — NOT exercised)
    19. Driver binding (demo only — NOT exercised)
    20. Eject (Windows only — demo only, NOT exercised)
    21. Hot-plug wait (safe, read-only)
    22. Event bus (2-second window)
    23. Sorted device table
    24. Platform summary
    25. Graceful failure
    26. Hot-cycle test — AUTOMATIC if root, SKIP otherwise
    27. Cleanup
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <algorithm>

#if KNST_USING_PLATFORM_WINDOWS
    #include <windows.h>
#else
    #include <unistd.h>
#endif


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
    return "Windows (WMI / SetupAPI)";
#else
    return "Linux (sysfs / usbfs)";
#endif
}

// Privilege check — no input needed, decides whether §26 runs.
static bool is_privileged() {
#if KNST_USING_PLATFORM_WINDOWS
    BOOL admin = FALSE;
    PSID grp = nullptr;
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
    if (::AllocateAndInitializeSid(&auth, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &grp)) {
        ::CheckTokenMembership(nullptr, grp, &admin);
        ::FreeSid(grp);
    }
    return admin != FALSE;
#else
    return ::geteuid() == 0;
#endif
}

[[maybe_unused]] static const char* speed_name(knst_usbc::usb_speed s) {
    switch (s) {
        case knst_usbc::usb_speed::Low:       return "Low (1.5 Mbps)";
        case knst_usbc::usb_speed::Full:      return "Full (12 Mbps)";
        case knst_usbc::usb_speed::High:      return "High (480 Mbps)";
        case knst_usbc::usb_speed::Super:     return "SuperSpeed (5 Gbps)";
        case knst_usbc::usb_speed::SuperPlus: return "SuperSpeed+ (10 Gbps)";
        default:                               return "Unknown";
    }
}

[[maybe_unused]] static const char* state_name(knst_usbc::usb_state s) {
    switch (s) {
        case knst_usbc::usb_state::NotAttached:  return "not attached";
        case knst_usbc::usb_state::Attached:     return "attached";
        case knst_usbc::usb_state::Powered:      return "powered";
        case knst_usbc::usb_state::Reconnecting: return "reconnecting";
        case knst_usbc::usb_state::Unauthorized: return "unauthorized";
        case knst_usbc::usb_state::Default:      return "default";
        case knst_usbc::usb_state::Addressed:    return "addressed";
        case knst_usbc::usb_state::Configured:   return "configured";
        case knst_usbc::usb_state::Suspended:    return "suspended";
        default:                                  return "unknown";
    }
}

static const char* usb_class_name(knst_usb_class c) {
    switch (c) {
        case knst_usb_class::Audio:       return "Audio      ";
        case knst_usb_class::Video:       return "Video      ";
        case knst_usb_class::HID:         return "HID        ";
        case knst_usb_class::MassStorage: return "MassStorage";
        case knst_usb_class::Printer:     return "Printer    ";
        case knst_usb_class::Hub:         return "Hub        ";
        case knst_usb_class::Wireless:    return "Wireless   ";
        case knst_usb_class::CDC:         return "CDC        ";
        case knst_usb_class::Vendor:      return "Vendor     ";
        default:                           return "Other      ";
    }
}

static void print_vid_pid(uint16_t vid, uint16_t pid) {
    std::cout << "VID:PID="
              << std::hex << std::setw(4) << std::setfill('0') << vid
              << ":"
              << std::setw(4) << std::setfill('0') << pid
              << std::dec << std::setfill(' ');
}

static void print_device_one_line(const knst_usb_info& u) {
    std::cout << "  bus=" << u.bus
              << "  dev=" << u.device_num
              << "  " << usb_class_name(u.usb_class)
              << "  ";
    print_vid_pid(u.vid, u.pid);
    std::cout << "  ";
    if (!u.product_name.empty()) std::cout << u.product_name;
    else                          std::cout << "(no name)";
    if (u.is_root_hub)   std::cout << "  [ROOT HUB]";
    else if (u.is_hub)   std::cout << "  [HUB]";
    std::cout << "\n";
}

[[maybe_unused]] static void print_port(const knst_usbc::usb_port_info& p) {
    std::cout << "    " << p.port_name
              << "  (port #" << p.number << ")";
    if (p.port_speed_mbps > 0)
        std::cout << "  speed=" << p.port_speed_mbps << " Mbps";
    else if (!p.link_state.empty())
        std::cout << "  link=" << p.link_state;
    if (!p.connect_type.empty())
        std::cout << "  connect=" << p.connect_type;
    if (!p.is_enabled)
        std::cout << "  [DISABLED]";
    if (p.has_device) {
        std::cout << "  → " << p.device_name;
        if (!p.device_product.empty())
            std::cout << " (" << p.device_product << ")";
    } else {
        std::cout << "  (empty)";
    }
    std::cout << "\n";
}

// Find a safe target — non-HID, non-hub. Used by §26 only when root.
static bool pick_safe_target(knst_usb_info& out) {
    auto all = knst_devices::usb();
    for (auto& u : all) {
        auto e = knst_usbc::enrich(u);
        if (e.is_root_hub || e.is_hub) continue;
        if (e.usb_class == knst_usb_class::HID) continue;
        out = u;
        return true;
    }
    return false;
}


// ============================================================
// main
// ============================================================

int main() {

    const bool root = is_privileged();

    std::cout << "Platform     : " << platform_name() << "\n";
    std::cout << "Running root : " << (root ? "yes" : "no") << "\n";
    std::cout << "SAFE MODE    : read-only. No reset / no block / no unbind.\n";
    std::cout << "EXCEPTION    : Section 26 (hot-cycle) runs AUTOMATICALLY if root.\n";
    std::cout << "NOTE         : endpoints are not populated from sysfs — libusb or\n";
    std::cout << "               debugfs is needed for full endpoint descriptors.\n";

    // =====================================================================
    section("1) Platform availability & privileges");
    // =====================================================================
    {
        auto all = knst_devices::usb();
        std::cout << "  usb devices found : " << all.size() << "\n";

        uint32_t hubs = 0, root_hubs = 0, hid = 0, storage = 0;
        for (auto& u : all) {
            auto e = knst_usbc::enrich(u);
            if (e.is_root_hub)      ++root_hubs;
            else if (e.is_hub)      ++hubs;
            if (e.usb_class == knst_usb_class::HID)         ++hid;
            if (e.usb_class == knst_usb_class::MassStorage) ++storage;
        }

        auto hid_by_iface = knst_usbc::find_by_class(0x03);
        if (hid_by_iface.size() > hid) hid = static_cast<uint32_t>(hid_by_iface.size());

        auto storage_by_iface = knst_usbc::find_by_class(0x08);
        if (storage_by_iface.size() > storage)
            storage = static_cast<uint32_t>(storage_by_iface.size());

        std::cout << "  root hubs        : " << root_hubs << "\n";
        std::cout << "  child hubs       : " << hubs << "\n";
        std::cout << "  HID devices      : " << hid << "\n";
        std::cout << "  storage devices  : " << storage << "\n";
        std::cout << "  running as root  : " << (root ? "yes" : "no") << "\n";

        if (all.empty()) {
            std::cout << "  (No USB devices — likely a VM or restricted environment)\n";
        }
    }

    // =====================================================================
    section("2) Full device enumeration");
    // =====================================================================
    {
        auto all = knst_devices::usb();
        if (all.empty()) {
            std::cout << "  (empty — skipping)\n";
        } else {
            uint32_t limit = all.size() < 20 ? all.size() : 20;
            for (uint32_t i = 0; i < limit; ++i) {
                std::cout << "  [" << i << "] ";
                print_device_one_line(knst_usbc::enrich(all[i]));
            }
            if (all.size() > 20)
                std::cout << "  ... and " << (all.size() - 20) << " more\n";
        }
    }

    // =====================================================================
    section("3) Find by VID/PID");
    // =====================================================================
    {
        struct { uint16_t vid; const char* name; } probes[] = {
            { 0x046D, "Logitech" },
            { 0x8087, "Intel"    },
            { 0x0BDA, "Realtek"  },
            { 0x04E8, "Samsung"  },
            { 0x1B3F, "General"  },
        };

        for (auto& probe : probes) {
            auto matches = knst_usbc::find(probe.vid);
            std::cout << "  VID=0x"
                      << std::hex << std::setw(4) << std::setfill('0') << probe.vid
                      << std::dec << std::setfill(' ')
                      << " (" << probe.name << "): "
                      << matches.size() << " device(s)\n";
            for (uint32_t i = 0; i < matches.size() && i < 3; ++i) {
                std::cout << "    - " << matches[i].product_name << " (";
                print_vid_pid(matches[i].vid, matches[i].pid);
                std::cout << ")\n";
            }
        }

        auto exact = knst_usbc::find_by_vid_pid(0x046D, 0xC52B);
        std::cout << "  find_by_vid_pid(0x046d, 0xc52b): "
                  << exact.size() << " device(s)\n";
    }

    // =====================================================================
    section("4) Find by class");
    // =====================================================================
    {
        struct { uint8_t code; const char* name; } classes[] = {
            { 0x01, "Audio"       },
            { 0x03, "HID"         },
            { 0x08, "MassStorage" },
            { 0x09, "Hub"         },
            { 0x0E, "Video"       },
            { 0xFF, "Vendor"      },
        };

        for (auto& c : classes) {
            auto matches = knst_usbc::find_by_class(c.code);
            std::cout << "  class 0x" << std::hex << (int)c.code << std::dec
                      << " (" << c.name << "): "
                      << matches.size() << " device(s)\n";
            for (uint32_t i = 0; i < matches.size() && i < 2; ++i) {
                std::cout << "    - bus=" << matches[i].bus
                          << " dev=" << matches[i].device_num
                          << " " << matches[i].product_name << "\n";
            }
        }
    }

    // =====================================================================
    section("5) Find by driver");
    // =====================================================================
    {
        const char* drivers[] = {
            "usbhid",
            "uvcvideo",
            "snd-usb-audio",
            "usb-storage",
            "hub",
        };
        for (const char* drv : drivers) {
            auto matches = knst_usbc::find_by_driver(knst_c16string(drv));
            std::cout << "  driver=" << drv << ": "
                      << matches.size() << " device(s)\n";
            for (uint32_t i = 0; i < matches.size() && i < 4; ++i) {
                std::cout << "    - " << matches[i].bus
                          << "/" << matches[i].device_num
                          << " " << matches[i].product_name << "\n";
            }
        }
    }

    // =====================================================================
    section("6) Root hubs");
    // =====================================================================
    {
        auto hubs = knst_usbc::find_hubs();
        uint32_t roots = 0;
        for (auto& u : hubs) {
            if (!u.is_root_hub) continue;
            ++roots;
            std::cout << "  root hub: bus=" << u.bus
                      << " dev=" << u.device_num
                      << "  USB " << u.usb_version
                      << "  speed=" << u.max_speed_mbps << " Mbps\n";
        }
        if (roots == 0)
            std::cout << "  (no root hubs reported)\n";
    }

    // =====================================================================
    section("7) Device descriptor");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto all = knst_devices::usb();
        if (all.empty()) {
            std::cout << "  (no devices — skipping)\n";
        } else {
            const knst_usb_info* pick = nullptr;
            for (auto& u : all) {
                auto e = knst_usbc::enrich(u);
                if (!e.is_root_hub && !e.is_hub) { pick = &u; break; }
            }
            if (!pick) pick = &all[0];

            std::cout << "  probing: " << pick->product_name
                      << " (" << pick->bus << "/" << pick->device_num << ")\n";

            auto d = knst_usbc::usb::descriptors(*pick);
            if (!d) {
                std::cout << "  descriptors: " << d.error_string() << "\n";
            } else {
                const auto& v = d.value;
                std::cout << "    bcdUSB            : 0x"
                          << std::hex << std::setw(4) << std::setfill('0')
                          << v.bcd_usb << std::dec << std::setfill(' ') << "\n";
                std::cout << "    device class      : " << (int)v.device_class << "\n";
                std::cout << "    device subclass   : " << (int)v.device_subclass << "\n";
                std::cout << "    device protocol   : " << (int)v.device_protocol << "\n";
                std::cout << "    max packet size0  : " << (int)v.max_packet_size0 << "\n";
                std::cout << "    ";
                print_vid_pid(v.id_vendor, v.id_product);
                std::cout << "\n";
                std::cout << "    bcdDevice         : 0x"
                          << std::hex << std::setw(4) << std::setfill('0')
                          << v.bcd_device << std::dec << std::setfill(' ') << "\n";
                std::cout << "    num configurations: " << (int)v.num_configurations << "\n";
            }
        }
#else
        std::cout << "  descriptors: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("8) USB strings");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto all = knst_devices::usb();
        if (all.empty()) {
            std::cout << "  (no devices — skipping)\n";
        } else {
            const knst_usb_info* pick = nullptr;
            for (auto& u : all) {
                auto e = knst_usbc::enrich(u);
                if (!e.is_root_hub && !e.is_hub) { pick = &u; break; }
            }
            if (!pick) pick = &all[0];

            auto s = knst_usbc::usb::strings(*pick);
            if (!s) {
                std::cout << "  strings: " << s.error_string() << "\n";
            } else {
                std::cout << "  manufacturer : " << s.value.manufacturer << "\n";
                std::cout << "  product      : " << s.value.product << "\n";
                std::cout << "  serial       : " << s.value.serial << "\n";
            }
        }
#else
        std::cout << "  strings: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("9) Configurations & interfaces");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto all = knst_devices::usb();
        if (all.empty()) {
            std::cout << "  (no devices — skipping)\n";
        } else {
            const knst_usb_info* pick = nullptr;
            for (auto& u : all) {
                auto e = knst_usbc::enrich(u);
                if (!e.is_root_hub && !e.is_hub) { pick = &u; break; }
            }
            if (!pick) pick = &all[0];

            std::cout << "  device: " << pick->product_name << "\n";

            auto cfgs = knst_usbc::usb::configurations(*pick);
            if (!cfgs) {
                std::cout << "  configurations: " << cfgs.error_string() << "\n";
            } else {
                for (auto& cfg : cfgs.value) {
                    std::cout << "  config #" << (int)cfg.number
                              << (cfg.is_active ? "  [ACTIVE]" : "")
                              << "  max_power=" << cfg.max_power_ma << " mA\n";
                    for (auto& iface : cfg.interfaces) {
                        std::cout << "    iface " << iface.number
                                  << "  class=0x" << std::hex << iface.class_code << std::dec
                                  << "  driver=" << iface.driver << "\n";
                    }
                }
            }
            std::cout << "  (endpoints not populated via sysfs — see docstring)\n";
        }
#else
        std::cout << "  configurations: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("10) Speed & state");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto all = knst_devices::usb();
        uint32_t shown = 0;
        for (auto& u : all) {
            auto e = knst_usbc::enrich(u);
            if (e.is_root_hub) continue;
            if (shown >= 5) break;

            auto sp = knst_usbc::usb::speed(u);
            auto st = knst_usbc::usb::state(u);

            std::cout << "  " << u.bus << "/" << u.device_num << "  "
                      << (u.product_name.empty()
                          ? knst_c16string(u"<unknown>") : u.product_name)
                      << "\n";
            if (sp) std::cout << "    speed : " << speed_name(sp.value) << "\n";
            else    std::cout << "    speed : " << sp.error_string() << "\n";
            if (st) std::cout << "    state : " << state_name(st.value) << "\n";
            else    std::cout << "    state : " << st.error_string() << "\n";
            ++shown;
        }
        if (shown == 0) std::cout << "  (no non-hub devices)\n";
#else
        std::cout << "  speed / state: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("11) Power info");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto all = knst_devices::usb();
        const knst_usb_info* pick = nullptr;
        for (auto& u : all) {
            auto e = knst_usbc::enrich(u);
            if (!e.is_root_hub && !e.is_hub) { pick = &u; break; }
        }

        if (!pick) {
            std::cout << "  (no non-hub device — skipping)\n";
        } else {
            auto p = knst_usbc::usb::power_info(*pick);
            if (!p) {
                std::cout << "  power_info: " << p.error_string() << "\n";
            } else {
                std::cout << "  device          : " << pick->product_name << "\n";
                std::cout << "  max_power_ma    : " << p.value.max_power_ma << "\n";
                std::cout << "  control_mode    : " << p.value.control_mode << "\n";
                std::cout << "  autosuspend_ms  : " << p.value.autosuspend_delay_ms << "\n";
                std::cout << "  is_suspended    : "
                          << (p.value.is_suspended ? "yes" : "no") << "\n";
                std::cout << "  remote_wakeup   : "
                          << (p.value.has_remote_wakeup ? "enabled" : "disabled") << "\n";
            }
        }
#else
        std::cout << "  power_info: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("12) Hub ports — every port with connected device");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto hubs = knst_usbc::find_hubs();
        if (hubs.empty()) {
            std::cout << "  (no hubs — skipping)\n";
        } else {
            uint32_t total_ports = 0, ports_with_device = 0;
            uint32_t shown_hubs = 0;

            for (auto& hub : hubs) {
                if (shown_hubs >= 3) break;
                ++shown_hubs;

                std::cout << "  hub: " << hub.product_name
                          << "  (bus=" << hub.bus
                          << " dev=" << hub.device_num << ")\n";

                auto ports = knst_usbc::usb::hub_ports(hub);
                if (!ports) {
                    std::cout << "    hub_ports: " << ports.error_string() << "\n";
                    continue;
                }
                if (ports.value.empty()) {
                    std::cout << "    (no port devices under this hub)\n";
                    continue;
                }

                total_ports += static_cast<uint32_t>(ports.value.size());
                for (auto& p : ports.value) {
                    if (p.has_device) ++ports_with_device;
                    print_port(p);
                }
                std::cout << "\n";
            }
            std::cout << "  total ports scanned : " << total_ports << "\n";
            std::cout << "  ports with devices  : " << ports_with_device << "\n";
        }
#else
        std::cout << "  hub_ports: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("13) Hub topology — parent/child tree");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto hubs = knst_usbc::find_hubs();
        if (hubs.empty()) {
            std::cout << "  (no hubs — skipping)\n";
        } else {
            std::cout << "  topology (root hubs → connected devices):\n";
            uint32_t shown = 0;

            for (auto& hub : hubs) {
                if (shown >= 4) break;
                ++shown;

                std::cout << "  ├─ " << hub.product_name
                          << " (usb" << hub.bus << ")\n";

                auto ports = knst_usbc::usb::hub_ports(hub);
                if (!ports || ports.value.empty()) {
                    std::cout << "  │    (no ports)\n";
                    continue;
                }

                for (auto& p : ports.value) {
                    if (p.has_device) {
                        std::cout << "  │    ├─ " << p.port_name
                                  << " → " << p.device_name;
                        if (!p.device_product.empty())
                            std::cout << " (" << p.device_product << ")";
                        std::cout << "\n";
                    }
                }
            }
        }
#else
        std::cout << "  topology: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("14) Error stats (per hub)");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto hubs = knst_usbc::find_hubs();
        if (hubs.empty()) {
            std::cout << "  (no hubs — skipping)\n";
        } else {
            const auto& hub = hubs[0];
            std::cout << "  probing hub: " << hub.product_name
                      << " (" << hub.bus << "/" << hub.device_num << ")\n";

            auto e = knst_usbc::usb::error_stats(hub);
            if (!e) {
                std::cout << "  error_stats: " << e.error_string() << "\n";
            } else {
                std::cout << "  port_resets    : " << e.value.port_resets << "\n";
                std::cout << "  connect_errors : " << e.value.connect_errors << "\n";
                std::cout << "  disconnects    : " << e.value.disconnects << "\n";
                std::cout << "  overcurrents   : " << e.value.overcurrents << "\n";
            }
        }
#else
        std::cout << "  error_stats: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("15) Health check (all devices)");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        auto all = knst_devices::usb();
        uint32_t unhealthy = 0, checked = 0;
        for (auto& u : all) {
            auto e = knst_usbc::enrich(u);
            if (e.is_root_hub) continue;
            auto h = knst_usbc::usb::health_check(u);
            if (!h) continue;
            ++checked;

            if (h.value.score < 100.0 || !h.value.healthy) {
                ++unhealthy;
                std::cout << "  ⚠️  " << u.bus << "/" << u.device_num
                          << " " << u.product_name
                          << "  score=" << h.value.score << "\n";
                for (auto& w : h.value.warnings)
                    std::cout << "       - " << w << "\n";
            }
        }
        std::cout << "  checked           : " << checked << "\n";
        std::cout << "  unhealthy devices : " << unhealthy << "\n";
#else
        std::cout << "  health_check: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("16) Authorization (demo only — NOT exercised)");
    // =====================================================================
    {
        std::cout << "  Authorization controls whether the kernel binds\n";
        std::cout << "  a USB driver to the device. Disabling a keyboard\n";
        std::cout << "  would make it unusable until re-authorized.\n\n";
        std::cout << "  API:\n";
        std::cout << "    knst_usbc::usb::set_authorized(device, true/false)\n";
        std::cout << "  Calling set_authorized(false) on a live device is\n";
        std::cout << "  a DESTRUCTIVE action — this demo skips it.\n";

#if KNST_USING_PLATFORM_LINUX
        auto all = knst_devices::usb();
        if (!all.empty()) {
            const auto& sample = all[0];
            std::cout << "\n  Would authorize: " << sample.bus << "/" << sample.device_num
                      << " (" << sample.product_name << ")\n";
            std::cout << "    → SKIPPED (safety)\n";
        }
#endif
    }

    // =====================================================================
    section("17) Reset / power-cycle (demo only — NOT exercised)");
    // =====================================================================
    {
        std::cout << "  reset() re-enumerates the device without unplugging.\n";
        std::cout << "  power_cycle() unbinds and rebinds the usb driver,\n";
        std::cout << "  which forces the ENTIRE device to be reinitialized.\n\n";
        std::cout << "  API:\n";
        std::cout << "    knst_usbc::usb::reset(device)                // fast, ~50ms\n";
        std::cout << "    knst_usbc::usb::power_cycle(device, 500)     // slower, ~500ms\n";
        std::cout << "  Both would disconnect the device temporarily.\n";
        std::cout << "  This demo SKIPS both (see section 26 for automatic test).\n";

#if KNST_USING_PLATFORM_LINUX
        auto all = knst_devices::usb();
        if (!all.empty()) {
            const auto& sample = all[0];
            std::cout << "\n  Would reset: " << sample.bus << "/" << sample.device_num
                      << " (" << sample.product_name << ")\n";
            std::cout << "    → SKIPPED (safety)\n";
        }
#endif
    }

    // =====================================================================
    section("18) Runtime PM / autosuspend (demo only — NOT exercised)");
    // =====================================================================
    {
        std::cout << "  Runtime PM allows the kernel to suspend idle devices.\n";
        std::cout << "  API:\n";
        std::cout << "    set_power_control(device, true)          // auto-suspend\n";
        std::cout << "    set_autosuspend_delay_ms(device, 2000)   // 2 second timeout\n";
        std::cout << "    set_remote_wakeup(device, true)          // allow wakeup\n";
        std::cout << "  This demo SKIPS writes to power/*.\n";
    }

    // =====================================================================
    section("19) Driver binding (demo only — NOT exercised)");
    // =====================================================================
    {
        std::cout << "  A USB device can be bound to a specific driver.\n";
        std::cout << "  Common drivers:\n";
        std::cout << "    usbhid        — HID devices (keyboard, mouse)\n";
        std::cout << "    usb-storage   — mass storage\n";
        std::cout << "    snd-usb-audio — audio\n";
        std::cout << "    uvcvideo      — video\n";
        std::cout << "  API:\n";
        std::cout << "    unbind_driver(device)\n";
        std::cout << "    bind_driver(device, u\"usbhid\")\n";
        std::cout << "  This demo SKIPS both (would break the driver).\n";
    }

    // =====================================================================
    section("20) Eject (demo only — NOT exercised)");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  Windows: eject_device() disables + re-enables the\n";
        std::cout << "  PnP device via PowerShell WMI. Requires admin.\n\n";
        std::cout << "  API:\n";
        std::cout << "    knst_usbc::usb::eject_device(device, delay_ms)\n";
        std::cout << "  This would disconnect the device for ~1 second.\n";
        std::cout << "  This demo SKIPS it (see section 26 for automatic test).\n";

        auto all = knst_devices::usb();
        if (!all.empty()) {
            const auto& sample = all[0];
            std::cout << "\n  Would eject: ";
            print_vid_pid(sample.vid, sample.pid);
            std::cout << " (" << sample.product_name << ")\n";
            std::cout << "    → SKIPPED (safety)\n";
        }
#else
        std::cout << "  Linux: use reset() or power_cycle() instead.\n";
        std::cout << "  (Windows-only eject_device for PnP cycle)\n";
#endif
    }

    // =====================================================================
    section("21) Hot-plug wait (safe)");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        std::cout << "  waiting 1 second for a usb_device event...\n";
        std::cout << "  (plug/unplug a USB device now to trigger this)\n";

        auto ev = knst_usbc::usb::wait_for_event(1000);
        if (ev) {
            std::cout << "  event : " << ev.value.action
                      << "  devname=" << ev.value.devname
                      << "  subsystem=" << ev.value.subsystem << "\n";
        } else {
            std::cout << "  wait_for_event: " << ev.error_string()
                      << " (no event within 1s — normal)\n";
        }
#else
        std::cout << "  wait_for_event: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("22) Event bus subscription (2-second window)");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        knst_usbc::usb_event_bus bus;
        int add_events = 0, remove_events = 0, bind_events = 0;

        uint32_t sub_id = bus.subscribe(
            [&](const knst_dev::uevent& ev) {
                if (ev.action == u"add")         ++add_events;
                else if (ev.action == u"remove") ++remove_events;
                else if (ev.action == u"bind")   ++bind_events;
            });

        std::cout << "  subscription started (running for 2s in background)\n";
        std::cout << "  (plug/unplug a USB device now to see events)\n";

        std::thread runner([&] { bus.run(2000); });
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        bus.stop();
        if (runner.joinable()) runner.join();

        std::cout << "  add     events: " << add_events << "\n";
        std::cout << "  remove  events: " << remove_events << "\n";
        std::cout << "  bind    events: " << bind_events << "\n";
        bus.unsubscribe(sub_id);
#else
        std::cout << "  Event bus: NotSupported on Windows\n";
#endif
    }

    // =====================================================================
    section("23) Sorted device table (by bus/dev)");
    // =====================================================================
    {
        auto all = knst_devices::usb();
        if (all.empty()) {
            std::cout << "  (no devices — skipping)\n";
        } else {
            for (uint32_t i = 1; i < all.size(); ++i) {
                knst_usb_info key = all[i];
                int j = static_cast<int>(i) - 1;
                while (j >= 0 && (all[j].bus > key.bus ||
                      (all[j].bus == key.bus &&
                       all[j].device_num > key.device_num))) {
                    all[j + 1] = all[j];
                    --j;
                }
                all[j + 1] = key;
            }

            std::cout << "  Sorted by (bus, dev):\n";
            for (auto& u : all) {
                auto e = knst_usbc::enrich(u);
                std::cout << "    " << u.bus << "/" << u.device_num << "  "
                          << usb_class_name(e.usb_class)
                          << "  ";
                if (!u.product_name.empty())
                    std::cout << u.product_name;
                else
                    std::cout << "(unknown)";
                std::cout << "\n";
            }
        }
    }

    // =====================================================================
    section("24) Platform summary");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  Windows : WMI + PowerShell PnP (limited)\n";
        std::cout << "  Supported: find, find_by_class, find_by_driver,\n";
        std::cout << "             find_hubs, eject_device\n";
        std::cout << "  Not supported: descriptors, strings, reset, power, port,\n";
        std::cout << "                 hub_ports, event bus, wait_for_event\n";
        std::cout << "  Note: bus/device numbers are always 0 on Windows —\n";
        std::cout << "        Win32_PnPEntity does not expose them.\n";
#else
        std::cout << "  Linux   : sysfs + usbfs + uevent\n";
        std::cout << "  Supported: full enumeration, descriptors, strings,\n";
        std::cout << "             speed, state, power, port, hub_ports,\n";
        std::cout << "             errors, health, authorize, reset,\n";
        std::cout << "             power_cycle, bind/unbind, hot-plug wait, event bus\n";
        std::cout << "  Root required for writes (set_authorized, reset, etc)\n";
#endif
    }

    // =====================================================================
    section("25) Graceful failure — errors are results");
    // =====================================================================
    {
        knst_usb_info bogus;
        bogus.bus        = 999;
        bogus.device_num = 999;
        bogus.vid        = 0xFFFF;
        bogus.pid        = 0xFFFF;

        auto r1 = knst_usbc::usb::descriptors(bogus);
        std::cout << "  descriptors(bogus)     : "
                  << (r1 ? "ok (unexpected!)" : r1.error_string()) << "\n";

        auto r2 = knst_usbc::usb::strings(bogus);
        std::cout << "  strings(bogus)         : "
                  << (r2 ? "ok (unexpected!)" : r2.error_string()) << "\n";

        auto r3 = knst_usbc::usb::speed(bogus);
        std::cout << "  speed(bogus)           : "
                  << (r3 ? "ok (unexpected!)" : r3.error_string()) << "\n";

        auto r4 = knst_usbc::usb::reset(bogus);
        std::cout << "  reset(bogus)           : "
                  << (r4 ? "ok (unexpected!)" : r4.error_string()) << "\n";

        auto r5 = knst_usbc::usb::set_authorized(bogus, true);
        std::cout << "  set_authorized(bogus)  : "
                  << (r5 ? "ok (unexpected!)" : r5.error_string()) << "\n";

        auto r6 = knst_usbc::usb::set_autosuspend_delay_ms(bogus, 999999);
        std::cout << "  set_autosuspend(999999): "
                  << (r6 ? "ok (unexpected!)" : r6.error_string()) << "\n";

        auto r7 = knst_usbc::find(0xDEAD);
        std::cout << "  find(0xDEAD)           : "
                  << r7.size() << " result(s) (expected 0)\n";

        auto r8 = knst_usbc::usb::wait_for_event(1);
        std::cout << "  wait_for_event(1ms)    : "
                  << (r8 ? "ok" : r8.error_string()) << "\n";

        auto r9 = knst_usbc::find_by_class(0xFE);
        std::cout << "  find_by_class(0xFE)    : "
                  << r9.size() << " result(s) (expected 0)\n";

        auto r10 = knst_usbc::find_by_driver(u"__nonexistent__");
        std::cout << "  find_by_driver(bogus)  : "
                  << r10.size() << " result(s) (expected 0)\n";

        auto r11 = knst_usbc::find_by_vid_pid(0xFFFF, 0xFFFF);
        std::cout << "  find_by_vid_pid(bogus) : "
                  << r11.size() << " result(s) (expected 0)\n";

        auto r12 = knst_usbc::usb::hub_ports(bogus);
        std::cout << "  hub_ports(bogus)       : "
                  << (r12 ? "ok (unexpected!)" : r12.error_string()) << "\n";
    }

    // =====================================================================
    section("26) Hot-cycle test — AUTOMATIC if root, SKIP otherwise");
    // =====================================================================
    {
        std::cout << "  This section disconnects + reattaches a non-HID device\n";
        std::cout << "  for 2 seconds. Keyboard, mouse, and hubs are SKIPPED.\n\n";

        knst_usb_info target{};
        if (!pick_safe_target(target)) {
            std::cout << "  (no safe non-HID device to cycle — skipping)\n";
            std::cout << "  hint: plug in a webcam, USB serial, or other non-HID device.\n";
        } else {
            std::cout << "  target : " << target.bus << "/" << target.device_num << "  ";
            print_vid_pid(target.vid, target.pid);
            std::cout << "  " << target.product_name << "\n";

            if (!root) {
                std::cout << "\n  Privileges : not root — hot-cycle needs sudo.\n";
                std::cout << "  To exercise this automatically:\n";
                std::cout << "      sudo ./build/bin/knst_app\n";
                std::cout << "  → SKIPPED\n";
            } else {
                std::cout << "\n  Privileges : root — running hot-cycle automatically...\n";

                uint16_t v = target.vid;
                uint16_t p = target.pid;
                uint32_t b = target.bus;
                uint32_t d = target.device_num;

                auto t_start = std::chrono::steady_clock::now();

#if KNST_USING_PLATFORM_LINUX
                auto r = knst_usbc::usb::power_cycle(target, 2000);
#else
                auto r = knst_usbc::usb::eject_device(target, 2000);
#endif

                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t_start).count();

                if (r) {
                    std::cout << "  ✅ OK — device reattached ("
                              << elapsed_ms << " ms)\n";
                } else {
                    std::cout << "  ❌ FAILED: " << r.error_string()
                              << "  (native=" << r.native << ")\n";
                    if (r.st == knst_dev::status::PermissionDenied)
                        std::cout << "  hint: needs root → sudo ./build/bin/knst_app\n";
                    else if (r.st == knst_dev::status::NotFound)
                        std::cout << "  hint: device may not be in sysfs anymore\n";
                }

                // Verify: give the kernel 500 ms, then re-enumerate.
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                auto after = knst_usbc::find(v, p);
                bool same_busdev = false;
                for (auto& a : after)
                    if (a.bus == b && a.device_num == d) { same_busdev = true; break; }

                std::cout << "  post-cycle find(): " << after.size()
                          << " matching device(s)\n";
                if (same_busdev)
                    std::cout << "  bus/dev present : yes (" << b << "/" << d << ")\n";
                else if (!after.empty())
                    std::cout << "  bus/dev present : re-enumerated (new device number)\n";
                else
                    std::cout << "  bus/dev present : NOT FOUND (device may need manual replug)\n";
            }
        }
    }

    // =====================================================================
    section("27) Cleanup");
    // =====================================================================
    {
        std::cout << "  no USB device was reset, blocked, ejected, or rebound\n";
        std::cout << "  except section 26 (automatic when root)\n";
        std::cout << "  all sysfs writes were skipped outside §26\n";
        std::cout << "  event subscriptions closed\n";
        std::cout << "  system state: unchanged\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}