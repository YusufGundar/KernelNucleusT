// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_devices_basic.cpp

  Basic usage of knst_devices — a cross-platform hardware inventory.

  Shows: CPU, motherboard, memory, storage, network, USB, PCI,
  monitors, input, audio, power, sensors, bluetooth, cameras, printers,
  system, OS, drivers, and the GPU bridge.

  Every function returns a struct (or a vector) with a `valid` flag.
  Fields the OS didn't provide keep their default values — check the
  flag before trusting the data.

  Platform coverage is now complete on both Linux and Windows —
  monitors, input devices, audio, sensors, bluetooth and cameras all
  have native Windows implementations.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>


static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


int main() {

    // =====================================================================
    section("1) CPU");
    // =====================================================================
    {
        knst_cpu_info c = knst_devices::cpu();

        if (!c.valid) {
            std::cout << "cpu() : not available\n";
        } else {
            std::cout << "vendor        : " << c.vendor      << "\n";
            std::cout << "model         : " << c.model_name  << "\n";
            std::cout << "architecture  : " << c.architecture<< "\n";
            std::cout << "physical cores: " << c.physical_cores << "\n";
            std::cout << "logical cores : " << c.logical_cores  << "\n";
            std::cout << "sockets       : " << c.socket_count   << "\n";
            std::cout << "base MHz      : " << c.base_mhz       << "\n";
            std::cout << "max MHz       : " << c.max_mhz        << "\n";
            std::cout << "current MHz   : " << c.current_mhz    << "\n";
            std::cout << "L3 cache KB   : " << c.cache_l3_kb    << "\n";
            std::cout << "load 1/5/15   : " << c.load_avg_1 << " / " << c.load_avg_5 << " / " << c.load_avg_15 << "\n";
            std::cout << "flags         : "
            << (c.has_sse4 ? "SSE4 " : "")
            << (c.has_avx  ? "AVX "  : "")
            << (c.has_avx2 ? "AVX2 " : "")
            << (c.has_aes  ? "AES "  : "")
            << (c.has_vmx  ? "VMX "  : "")
            << (c.has_hyperthreading ? "HT" : "") << "\n";
        }
    }

    // =====================================================================
    section("2) Motherboard & BIOS");
    // =====================================================================
    {
        knst_motherboard_info m = knst_devices::motherboard();

        std::cout << "manufacturer  : " << m.manufacturer  << "\n";
        std::cout << "product       : " << m.product       << "\n";
        std::cout << "version       : " << m.version       << "\n";
        std::cout << "bios vendor   : " << m.bios_vendor   << "\n";
        std::cout << "bios version  : " << m.bios_version  << "\n";
        std::cout << "bios date     : " << m.bios_date     << "\n";
        std::cout << "UEFI boot     : " << (m.uefi_boot   ? "yes" : "no") << "\n";
        std::cout << "Secure Boot   : " << (m.secure_boot ? "yes" : "no") << "\n";
        std::cout << "TPM present   : " << (m.has_tpm     ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("3) Memory");
    // =====================================================================
    {
        knst_memory_info m = knst_devices::memory();

        auto gb = [](uint64_t b) { return b / (1024ULL * 1024 * 1024); };

        std::cout << "total : " << gb(m.total_bytes)     << " GB\n";
        std::cout << "used : " << gb(m.used_bytes)      << " GB\n";
        std::cout << "free : " << gb(m.free_bytes)      << " GB\n";
        std::cout << "available :" << gb(m.available_bytes) << " GB\n";

        if (m.has_swap) {
            std::cout << "swap : " << gb(m.swap_used) << " / " << gb(m.swap_total) << " GB\n";
        }

        if (m.has_modules) {
            std::cout << "\nmemory modules : " << m.modules.size() << "\n";
            for (uint32_t i = 0; i < m.modules.size() && i < 4; ++i) {
                const auto& mod = m.modules[i];
                std::cout << "  [" << i << "] " << gb(mod.size_bytes) << " GB @ "
                          << mod.speed_mhz << " MHz  "
                          << mod.manufacturer << "  " << mod.part_number << "\n";
            }
        }
    }

    // =====================================================================
    section("4) Storage");
    // =====================================================================
    {
        knst_vector<knst_storage_info> disks = knst_devices::storage();

        std::cout << "disks found : " << disks.size() << "\n";
        for (uint32_t i = 0; i < disks.size(); ++i) {
            const auto& s = disks[i];
            std::cout << "\n[" << i << "] " << s.device << "\n";
            std::cout << "    model        : " << s.model  << "\n";
            std::cout << "    serial       : " << s.serial << "\n";
            std::cout << "    size (GB)    : " << s.total_bytes / (1024ULL * 1024 * 1024) << "\n";
            std::cout << "    solid state  : " << (s.is_solid_state ? "yes" : "no") << "\n";
            std::cout << "    removable    : " << (s.is_removable   ? "yes" : "no") << "\n";
            std::cout << "    system disk  : " << (s.is_system_disk ? "yes" : "no") << "\n";

            if (s.has_partitions) {
                std::cout << "    partitions   : " << s.partitions.size() << "\n";
                for (uint32_t j = 0; j < s.partitions.size() && j < 3; ++j) {
                    const auto& p = s.partitions[j];
                    std::cout << "      " << p.device << " -> " << p.mount_point
                              << " (" << p.fs_type << ")\n";
                }
            }
        }
    }

    // =====================================================================
    section("5) Network");
    // =====================================================================
    {
        knst_vector<knst_network_info> nets = knst_devices::network();

        std::cout << "interfaces found : " << nets.size() << "\n";
        for (uint32_t i = 0; i < nets.size(); ++i) {
            const auto& n = nets[i];
            std::cout << "\n[" << i << "] " << n.name << "\n";
            std::cout << "    MAC      : " << n.mac    << "\n";
            std::cout << "    IPv4     : " << n.ipv4   << "\n";
            std::cout << "    IPv6     : " << n.ipv6   << "\n";
            std::cout << "    up       : " << (n.is_up      ? "yes" : "no") << "\n";
            std::cout << "    loopback : " << (n.is_loopback? "yes" : "no") << "\n";
            std::cout << "    wireless : " << (n.is_wireless? "yes" : "no") << "\n";
            std::cout << "    rx bytes : " << n.rx_bytes << "\n";
            std::cout << "    tx bytes : " << n.tx_bytes << "\n";
            if (n.has_wifi) {
                std::cout << "    SSID     : " << n.ssid << "\n";
                std::cout << "    signal   : " << n.signal_dbm << " dBm\n";
                std::cout << "    channel  : " << n.channel << "\n";
                std::cout << "    security : " << n.security << "\n";
            }
        }
    }

    // =====================================================================
    section("6) USB");
    // =====================================================================
    {
        knst_vector<knst_usb_info> devs = knst_devices::usb();

        std::cout << "USB devices found : " << devs.size() << "\n";
        for (uint32_t i = 0; i < devs.size() && i < 6; ++i) {
            const auto& u = devs[i];
            std::cout << "  [" << i << "] VID=" << std::hex << u.vid
                      << " PID=" << u.pid << std::dec << "  "
                      << u.manufacturer << "  " << u.product_name << "\n";
        }
        if (devs.size() > 6)
            std::cout << "  ... " << (devs.size() - 6) << " more\n";
    }

    // =====================================================================
    section("7) PCI");
    // =====================================================================
    {
        knst_vector<knst_pci_info> devs = knst_devices::pci();

        std::cout << "PCI devices found : " << devs.size() << "\n";
        for (uint32_t i = 0; i < devs.size() && i < 6; ++i) {
            const auto& p = devs[i];
            std::cout << "  [" << i << "] " << std::hex << p.vendor_id
                      << ":" << p.device_id << std::dec << "  "
                      << p.vendor_name << "  " << p.device_name
                      << "  (driver: " << p.driver << ")\n";
        }
        if (devs.size() > 6)
            std::cout << "  ... " << (devs.size() - 6) << " more\n";
    }

    // =====================================================================
    section("8) Monitors");
    // =====================================================================
    // Linux: EDID from /sys/class/drm/*/edid
    // Windows: EnumDisplayDevices + registry EDID + WMI brightness
    {
        knst_vector<knst_monitor_info> mons = knst_devices::monitors();

        std::cout << "monitors found : " << mons.size() << "\n";
        for (uint32_t i = 0; i < mons.size(); ++i) {
            const auto& m = mons[i];
            std::cout << "\n[" << i << "] " << m.name << "\n";
            std::cout << "    manufacturer : " << m.manufacturer
                      << " (" << m.year << " week " << m.week << ")\n";
            std::cout << "    serial       : " << m.serial << "\n";

            std::cout << "    connection   : ";
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

            std::cout << "    native       : " << m.native_width  << "x" << m.native_height  << "\n";
            std::cout << "    current      : " << m.current_width << "x" << m.current_height << "\n";
            std::cout << "    refresh      : " << m.refresh_hz << " Hz\n";
            std::cout << "    size         : " << m.diagonal_inch << " inch";
            if (m.physical_width_mm > 0)
                std::cout << "  (" << m.physical_width_mm << "x" << m.physical_height_mm << " mm)";
            std::cout << "\n";

            if (m.gamma > 0)
                std::cout << "    gamma        : " << m.gamma << "\n";
            std::cout << "    primary      : " << (m.is_primary ? "yes" : "no") << "\n";
            if (m.brightness > 0)
                std::cout << "    brightness   : " << m.brightness << "%\n";
        }
    }

    // =====================================================================
    section("9) Input devices");
    // =====================================================================
    // Linux: /sys/class/input/event*
    // Windows: GetRawInputDeviceList + HID
    {
        knst_vector<knst_input_info> devs = knst_devices::input();

        std::cout << "input devices found : " << devs.size() << "\n";
        for (uint32_t i = 0; i < devs.size() && i < 8; ++i) {
            const auto& d = devs[i];

            std::cout << "  [" << i << "] ";
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
            if (d.num_buttons > 0)
                std::cout << "        buttons = " << d.num_buttons << "\n";
            if (!d.device_path.empty())
                std::cout << "        path    = " << d.device_path << "\n";
        }
        if (devs.size() > 8)
            std::cout << "  ... " << (devs.size() - 8) << " more\n";
    }

    // =====================================================================
    section("10) Audio");
    // =====================================================================
    // Linux: pactl (PulseAudio/PipeWire)
    // Windows: IMMDeviceEnumerator (COM)
    {
        knst_audio_info a = knst_devices::audio();

        std::cout << "default sink   : " << a.default_sink   << "\n";
        std::cout << "default source : " << a.default_source << "\n";
        std::cout << "devices        : " << a.devices.size() << "\n";
        for (uint32_t i = 0; i < a.devices.size() && i < 8; ++i) {
            const auto& d = a.devices[i];
            std::cout << "  [" << i << "] "
                      << (d.is_input ? "IN  " : "OUT ")
                      << d.description;
            if (d.is_default) std::cout << "  [default]";
            std::cout << "\n";
            if (!d.name.empty())
                std::cout << "        id = " << d.name << "\n";
        }
    }

    // =====================================================================
    section("11) Power / Battery");
    // =====================================================================
    {
        knst_power_info p = knst_devices::power();

        std::cout << "has battery: " << (p.has_battery ? "yes" : "no") << "\n";
        std::cout << "AC online: " << (p.ac_connected ? "yes" : "no") << "\n";

        for (uint32_t i = 0; i < p.batteries.size(); ++i) {
            const auto& b = p.batteries[i];
            std::cout << "  [" << i << "] " << b.name << "  " << b.percent << "%"
                      << "  health " << b.health_percent << "%"
                      << "  state=" << static_cast<int>(b.state) << "\n";
        }
    }

    // =====================================================================
    section("12) Sensors");
    // =====================================================================
    // Linux: /sys/class/hwmon (lm-sensors)
    // Windows: WMI root\WMI (thermal zones) + Win32_Fan/VoltageProbe
    {
        knst_vector<knst_sensor_info> s = knst_devices::sensors();

        std::cout << "sensors found : " << s.size() << "\n";
        for (uint32_t i = 0; i < s.size() && i < 10; ++i) {
            const auto& x = s[i];

            std::cout << "  [";
            switch (x.type) {
                case knst_sensor_type::Temperature: std::cout << "TEMP "; break;
                case knst_sensor_type::Fan:         std::cout << "FAN  "; break;
                case knst_sensor_type::Voltage:     std::cout << "VOLT "; break;
                case knst_sensor_type::Current:     std::cout << "CURR "; break;
                case knst_sensor_type::Power:       std::cout << "PWR  "; break;
                default:                             std::cout << "     "; break;
            }
            std::cout << "] " << x.chip;
            if (!x.name.empty()) std::cout << " / " << x.name;
            std::cout << " : " << x.value << " " << x.unit << "\n";
        }
        if (s.size() > 10)
            std::cout << "  ... " << (s.size() - 10) << " more\n";
    }

    // =====================================================================
    section("13) Bluetooth");
    // =====================================================================
    // Linux: bluetoothctl (D-Bus)
    // Windows: BluetoothFindFirstRadio + BluetoothFindFirstDevice
    {
        knst_bluetooth_info b = knst_devices::bluetooth();

        std::cout << "adapter   : " << (b.has_adapter ? "yes" : "no") << "\n";
        if (b.has_adapter) {
            std::cout << "name      : " << b.adapter_name    << "\n";
            std::cout << "address   : " << b.adapter_address << "\n";
            std::cout << "powered   : " << (b.powered      ? "yes" : "no") << "\n";
            std::cout << "discover. : " << (b.discoverable ? "yes" : "no") << "\n";
        }
        std::cout << "devices   : " << b.devices.size() << "\n";
        for (uint32_t i = 0; i < b.devices.size() && i < 6; ++i) {
            const auto& d = b.devices[i];
            std::cout << "  [" << i << "] " << d.name << "  " << d.address;
            if (d.connected) std::cout << "  [connected]";
            else if (d.paired) std::cout << "  [paired]";
            if (d.rssi != 0) std::cout << "  rssi=" << d.rssi << " dBm";
            std::cout << "\n";
        }
        if (b.devices.size() > 6)
            std::cout << "  ... " << (b.devices.size() - 6) << " more\n";
    }

    // =====================================================================
    section("14) Cameras");
    // =====================================================================
    // Linux: /sys/class/video4linux
    // Windows: WMI Win32_PnPEntity (PNPClass='Camera' / 'Image')
    {
        knst_vector<knst_camera_info> cams = knst_devices::cameras();

        std::cout << "cameras found : " << cams.size() << "\n";
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
    section("15) Printers");
    // =====================================================================
    {
        knst_vector<knst_printer_info> pr = knst_devices::printers();

        std::cout << "printers found : " << pr.size() << "\n";
        for (uint32_t i = 0; i < pr.size(); ++i) {
            std::cout << "  [" << i << "] " << pr[i].name
                      << "  status=" << pr[i].status
                      << (pr[i].is_default ? "  [default]" : "")
                      << (pr[i].is_shared  ? "  [shared]"  : "")
                      << "\n";
        }
    }

    // =====================================================================
    section("16) System");
    // =====================================================================
    {
        knst_system_info s = knst_devices::system();

        std::cout << "manufacturer : " << s.manufacturer << "\n";
        std::cout << "product      : " << s.product_name << "\n";
        std::cout << "version      : " << s.version      << "\n";
        std::cout << "serial       : " << s.serial       << "\n";
        std::cout << "chassis      : " << s.chassis_type << "\n";
        std::cout << "is VM        : " << (s.is_virtual_machine ? "yes" : "no") << "\n";
        std::cout << "is laptop    : " << (s.is_laptop          ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("17) OS");
    // =====================================================================
    {
        knst_os_info o = knst_devices::os();

        std::cout << "os           : " << o.os_name        << " " << o.os_version << "\n";
        std::cout << "kernel       : " << o.kernel_name    << " " << o.kernel_version << "\n";
        std::cout << "arch         : " << o.kernel_arch    << "\n";
        std::cout << "hostname     : " << o.hostname       << "\n";
        std::cout << "user         : " << o.user           << "\n";
        std::cout << "home         : " << o.home_dir       << "\n";
        std::cout << "shell        : " << o.shell          << "\n";
        std::cout << "timezone     : " << o.timezone       << "\n";
        std::cout << "uptime       : " << o.uptime_seconds << " s\n";
        std::cout << "page size    : " << o.page_size      << " bytes\n";
    }

    // =====================================================================
    section("18) Kernel drivers (first 8)");
    // =====================================================================
    {
        knst_vector<knst_driver_info> d = knst_devices::drivers();

        std::cout << "drivers loaded : " << d.size() << "\n";
        for (uint32_t i = 0; i < d.size() && i < 8; ++i) {
            const auto& drv = d[i];
            std::cout << "  " << drv.name
                      << "  size=" << drv.size_bytes
                      << "  used_by=" << drv.used_by_count << "\n";
        }
        if (d.size() > 8)
            std::cout << "  ... " << (d.size() - 8) << " more\n";
    }

        // =====================================================================
    section("19) GPU bridge (knst_gpu)");
    // =====================================================================
    // The devices namespace forwards GPU queries to knst_gpu (in
    // knst_process.hpp). Requires NVIDIA/AMD drivers + tools.
    {
        std::cout << "has_gpu()    : " << (knst_devices::has_gpu() ? "yes" : "no") << "\n";
        std::cout << "gpu_count()  : " << knst_devices::gpu_count()  << "\n";

        knst_c16string drv = knst_devices::nvidia_driver_version();
        if (!drv.empty()) std::cout << "nvidia drv   : " << drv << "\n";

        knst_c16string cuda = knst_devices::cuda_version();
        if (!cuda.empty()) std::cout << "cuda version : " << cuda << "\n";

        knst_vector<knst_gpu_info> gpus = knst_devices::gpus();
        std::cout << "gpus()       : " << gpus.size() << " entries\n";

        for (uint32_t i = 0; i < gpus.size(); ++i) {
            const auto& g = gpus[i];
            std::cout << "\n[" << i << "] " << g.name
                      << "  (" << knst_gpu_vendor_string(g.vendor) << ")\n";

            if (!g.driver_version.empty())
                std::cout << "    driver     : " << g.driver_version << "\n";
            if (!g.uuid.empty())
                std::cout << "    uuid       : " << g.uuid << "\n";
            if (!g.vbios_version.empty())
                std::cout << "    vbios      : " << g.vbios_version << "\n";

            if (g.has_memory) {
                std::cout << "    VRAM total : " << (g.memory_total / (1024ULL * 1024)) << " MB\n";
                std::cout << "    VRAM used  : " << (g.memory_used  / (1024ULL * 1024)) << " MB\n";
                std::cout << "    VRAM free  : " << (g.memory_free  / (1024ULL * 1024)) << " MB\n";
            }
            if (g.has_utilization) {
                std::cout << "    GPU load   : " << g.utilization_gpu << " %\n";
                if (g.utilization_mem > 0)
                    std::cout << "    Mem load   : " << g.utilization_mem << " %\n";
            }
            if (g.has_temperature)
                std::cout << "    temp       : " << g.temperature_core << " C\n";
            if (g.has_power)
                std::cout << "    power      : " << (g.power_usage_mw / 1000)
                          << " W (limit " << (g.power_limit_mw / 1000) << " W)\n";
                        if (g.has_clocks) {
                std::cout << "    core clock : " << g.clock_core_mhz << " MHz\n";
                if (g.clock_mem_mhz > 0)
                    std::cout << "    mem clock  : " << g.clock_mem_mhz  << " MHz\n";
            }
            if (g.has_fan)
                std::cout << "    fan        : " << g.fan_percent << " %\n";
            if (g.has_pcie)
                std::cout << "    PCIe       : Gen" << g.pcie_gen
                          << " x" << g.pcie_width << "\n";
        }


        knst_vector<knst_gpu_process> gp = knst_devices::gpu_processes();
        std::cout << "\nGPU-using processes : " << gp.size() << "\n";
        for (uint32_t i = 0; i < gp.size() && i < 6; ++i) {
            const auto& p = gp[i];
            std::cout << "  pid=" << p.pid
                      << "  gpu=" << p.gpu_index
                      << "  vram=";
            if (p.vram_bytes == 0) std::cout << "N/A";
            else                   std::cout << (p.vram_bytes / (1024ULL * 1024)) << " MB";
            std::cout << "  " << p.name
                      << "  (" << knst_gpu_vendor_string(p.vendor) << ")\n";
        }
        if (gp.size() > 6)
            std::cout << "  ... " << (gp.size() - 6) << " more\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}