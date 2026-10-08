# knst_devices — Hardware Scanning Library

Hello! 👋 This document explains what the `knst_devices` library is, how to use it, and why it exists in KernelNucleusT.

In short: **a hardware inventory library that reads the entire system hardware (CPU, RAM, disk, network, USB, PCI, monitors, sensors, cameras, printers, batteries, Bluetooth...) into standard information structures across 18 categories.** It works via `/proc`, `/sys`, and external tools on Linux, and via WMI + native APIs on Windows.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **18 categories** | Everything from CPU to printers under a single namespace |
| **Fully cross-platform** | **Every category** works on both Linux and Windows |
| **Standard structures** | Every query returns a `struct` with a `valid` flag |
| **Missing data = default value** | If the OS does not provide it, the field stays default — check via `valid` |
| **Lazy loading** | Expensive work like DMI caching runs once via `call_once` |
| **Fast path checks** | If Bluetooth/CUPS is absent, no tool call is made |
| **UTF-16 strings** | All text fields use `knst_c16string` — Windows-compatible |
| **Vector returns** | Categories like USB, PCI, storage, network return `knst_vector<T>` |
| **Error handling** | Standard error codes via the `knst_device_error` enum |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // CPU info
    knst_cpu_info cpu = knst_devices::cpu();
    if (cpu.valid) {
        std::cout << "CPU: " << cpu.model_name << "\n";
        std::cout << "Cores: " << cpu.physical_cores << " physical / " << cpu.logical_cores << " logical\n";
        std::cout << "Max frequency: " << cpu.max_mhz << " MHz\n";
    }

    // RAM info
    knst_memory_info ram = knst_devices::memory();
    if (ram.valid) {
        std::cout << "Total RAM: " << ram.total_bytes / (1024 * 1024 * 1024) << " GB\n";
    }

    // List all disks
    auto disks = knst_devices::storage();
    for (const auto& d : disks) {
        std::cout << "Disk: " << d.device << " (" << d.model << ")\n";
    }

    return 0;
}
```

---

## 📋 18 Categories

| # | Category | Function | Return Type |
|---|---|---|---|
| 1 | **CPU** | `knst_devices::cpu()` | `knst_cpu_info` |
| 2 | **Motherboard + BIOS** | `knst_devices::motherboard()` | `knst_motherboard_info` |
| 3 | **Memory** | `knst_devices::memory()` | `knst_memory_info` |
| 4 | **Storage** | `knst_devices::storage()` | `knst_vector<knst_storage_info>` |
| 5 | **Network** | `knst_devices::network()` | `knst_vector<knst_network_info>` |
| 6 | **USB** | `knst_devices::usb()` | `knst_vector<knst_usb_info>` |
| 7 | **PCI** | `knst_devices::pci()` | `knst_vector<knst_pci_info>` |
| 8 | **Monitors** | `knst_devices::monitors()` | `knst_vector<knst_monitor_info>` |
| 9 | **Input devices** | `knst_devices::input()` | `knst_vector<knst_input_info>` |
| 10 | **Audio** | `knst_devices::audio()` | `knst_audio_info` |
| 11 | **Power/Battery** | `knst_devices::power()` | `knst_power_info` |
| 12 | **Sensors** | `knst_devices::sensors()` | `knst_vector<knst_sensor_info>` |
| 13 | **Bluetooth** | `knst_devices::bluetooth()` | `knst_bluetooth_info` |
| 14 | **Cameras** | `knst_devices::cameras()` | `knst_vector<knst_camera_info>` |
| 15 | **Printers** | `knst_devices::printers()` | `knst_vector<knst_printer_info>` |
| 16 | **System** | `knst_devices::system()` | `knst_system_info` |
| 17 | **Operating System** | `knst_devices::os()` | `knst_os_info` |
| 18 | **Drivers** | `knst_devices::drivers()` | `knst_vector<knst_driver_info>` |
| ➕ | **GPU (bridge)** | `knst_devices::gpus()` | `knst_vector<knst_gpu_info>` |

---

## 🌍 Platform Support

**All categories work on both Linux and Windows.**

| Category | Linux | Windows |
|---|---|---|
| CPU | ✅ `/proc/cpuinfo`, `/sys/...cpufreq`, `/sys/class/hwmon` | ✅ WMI (`Win32_Processor`) |
| Motherboard | ✅ `dmidecode` + `/sys/class/dmi/id` | ✅ WMI (`Win32_BaseBoard`, `Win32_BIOS`) |
| Memory | ✅ `/proc/meminfo` + `dmidecode -t memory` | ✅ WMI (`Win32_PhysicalMemory`) |
| Storage | ✅ `/sys/block` + `/proc/mounts` | ✅ WMI (`Win32_DiskDrive`) |
| Network | ✅ `getifaddrs` + `/sys/class/net` | ✅ WMI + `netsh wlan` |
| USB | ✅ `/sys/bus/usb/devices` | ✅ WMI (`Win32_PnPEntity`) |
| PCI | ✅ `/sys/bus/pci/devices` + `lspci` | ✅ WMI (`Win32_PnPEntity`) |
| **Monitors** | ✅ `/sys/class/drm` + EDID | ✅ `EnumDisplayDevices` + registry EDID + `WmiMonitorID` (name/vendor/serial) + `WmiMonitorBrightness` |
| **Input** | ✅ `/sys/class/input` | ✅ `GetRawInputDeviceList` + HID API |
| **Audio** | ✅ `pactl` (PulseAudio/PipeWire) | ✅ `IMMDeviceEnumerator` (COM) |
| Power | ✅ `/sys/class/power_supply` | ✅ `GetSystemPowerStatus` |
| **Sensors** | ✅ `/sys/class/hwmon` | ✅ WMI `root\WMI` + `Win32_Fan` + `Win32_VoltageProbe` |
| **Bluetooth** | ✅ `bluetoothctl` | ✅ `BluetoothFindFirstRadio` + `BluetoothFindFirstDevice` |
| **Cameras** | ✅ `/sys/class/video4linux` | ✅ WMI (`PNPClass='Camera'`) |
| Printers | ✅ `lpstat` | ✅ WMI (`Win32_Printer`) |
| System | ✅ DMI | ✅ WMI (`Win32_ComputerSystem`) |
| OS | ✅ `/etc/os-release` + `uname` | ✅ `RtlGetVersion` + `GetComputerNameW` |
| Drivers | ✅ `/proc/modules` | ✅ WMI (`Win32_SystemDriver`) |

✅ = Fully supported.

---

## 🖥️ CPU — `knst_cpu_info`

```cpp
knst_cpu_info c = knst_devices::cpu();

c.vendor;              // "GenuineIntel" / "AuthenticAMD"
c.model_name;          // "Intel(R) Core(TM) i7-9700K"
c.architecture;        // "x86_64" / "aarch64"
c.physical_cores;      // 8
c.logical_cores;       // 8 (same if HT is disabled)
c.socket_count;        // 1
c.base_mhz;            // 3600
c.max_mhz;             // 4900
c.current_mhz;         // instant frequency
c.cache_l1_kb;         // in KB
c.cache_l2_kb;
c.cache_l3_kb;
c.temperature_c;       // °C (coretemp/k10temp)
c.usage_percent;       // instant usage %
c.load_avg_1;          // /proc/loadavg (Linux)
c.load_avg_5;
c.load_avg_15;

// CPU features
c.has_sse4;            // SSE4.2
c.has_avx;             // AVX
c.has_avx2;            // AVX2
c.has_avx512;          // AVX-512
c.has_aes;             // AES-NI
c.has_vmx;             // Intel VT-x / AMD-V
c.has_iommu;           // IOMMU
c.has_hyperthreading;  // HT / SMT

// Flags
c.valid;               // Overall validity
c.has_cores;           // Core info was read?
c.has_frequency;       // Frequency info present?
c.has_cache;           // Cache info present?
c.has_temperature;     // Temperature readable?
c.has_usage;           // Usage percentage measurable?
```

**Example:**

```cpp
knst_cpu_info c = knst_devices::cpu();
if (c.valid) {
    std::cout << c.model_name << "\n"
              << "  " << c.physical_cores << "C/" << c.logical_cores << "T @ "
              << c.max_mhz << " MHz\n"
              << "  AVX2: " << (c.has_avx2 ? "yes" : "no") << "\n";
}
```

---

## 💾 Memory — `knst_memory_info`

```cpp
knst_memory_info m = knst_devices::memory();

m.total_bytes;         // Total physical RAM
m.used_bytes;          // Used
m.free_bytes;          // Free
m.available_bytes;     // Available (excluding cache)
m.cached_bytes;        // Page cache
m.buffers_bytes;       // Kernel buffers

m.swap_total;          // Swap total
m.swap_used;           // Swap used
m.swap_free;           // Swap free
m.has_swap;

m.slot_count;          // Total RAM slots
m.slots_used;          // Used slots

// Memory modules
for (const auto& mod : m.modules) {
    mod.size_bytes;    // Module size
    mod.speed_mhz;     // Speed
    mod.manufacturer;  // "Samsung" / "Kingston"
    mod.part_number;   // "KHX3200C16D4/8G"
    mod.serial;
    mod.locator;       // "DIMM_A1"
    mod.type;          // "DDR4" / "DDR5"
    mod.form_factor;   // "DIMM" / "SODIMM"
    mod.ecc;
}

m.has_modules;         // Module info was read?
```

**Displaying in GB:**

```cpp
auto gb = [](uint64_t b) { return b / (1024ULL * 1024 * 1024); };

std::cout << "RAM: " << gb(m.used_bytes) << "/" << gb(m.total_bytes) << " GB\n";
```

---

## 💽 Storage — `knst_vector<knst_storage_info>`

```cpp
auto disks = knst_devices::storage();

for (const auto& s : disks) {
    s.device;              // "/dev/nvme0n1" (Linux) / "\\\\.\\PHYSICALDRIVE0" (Win)
    s.model;               // "Samsung SSD 970 EVO Plus 1TB"
    s.serial;
    s.firmware;
    s.vendor;
    s.type;                // HDD, SSD, NVMe, USB, eMMC, ...
    s.total_bytes;
    s.free_bytes;
    s.used_bytes;
    s.temperature_c;
    s.is_removable;
    s.is_system_disk;
    s.is_solid_state;
    s.rotational_rpm;      // 5400/7200 for HDD

    // SMART data (if available)
    s.has_smart;
    s.smart_passed;
    s.smart_power_on_hours;
    s.smart_bytes_written;
    s.smart_reallocated_sectors;
    s.smart_pending_sectors;
    s.smart_percent_used;

    // Partitions
    for (const auto& p : s.partitions) {
        p.device;          // "/dev/nvme0n1p2"
        p.mount_point;     // "/" / "/home" / "/boot"
        p.fs_type;         // "ext4" / "btrfs" / "vfat"
        p.total_bytes;
        p.used_bytes;
        p.free_bytes;
        p.uuid;
        p.label;
        p.is_boot;
        p.is_removable;
    }
    s.has_partitions;
}
```

**LVM/LUKS support:** On Linux the library automatically resolves `/dev/mapper/*` and `/dev/dm-*` devices to their physical disks. It works correctly on systems using LUKS+LVM, such as Pop!_OS, Ubuntu, Fedora.

**On Windows:**
- **SSD/HDD detection:** `MSFT_PhysicalDisk` (root\Microsoft\Windows\Storage) — `MediaType` field, 3=HDD, 4=SSD
- **System disk detection:** via `Win32_DiskPartition`, the physical drive holding the boot partition is found and marked `is_system_disk = true`

---

## 🌐 Network — `knst_vector<knst_network_info>`

```cpp
auto nets = knst_devices::network();

for (const auto& n : nets) {
    n.name;             // "wlan0", "eth0", "lo"
    n.description;
    n.mac;              // "aa:bb:cc:dd:ee:ff"
    n.ipv4;             // "192.168.1.42"
    n.ipv6;
    n.netmask;
    n.gateway;
    n.dns;

    n.type;             // Ethernet, WiFi, Loopback, Bridge, ...
    n.mtu;              // 1500
    n.speed_mbps;       // 1000
    n.is_up;
    n.is_running;
    n.is_loopback;
    n.is_wireless;
    n.is_virtual;
    n.duplex;           // 0=half, 1=full

    n.rx_bytes;
    n.tx_bytes;
    n.rx_packets;
    n.tx_packets;
    n.rx_errors;
    n.tx_errors;
    n.rx_dropped;
    n.tx_dropped;

    // WiFi info
    n.ssid;             // "HomeWifi_5G"
    n.signal_dbm;       // -47
    n.frequency_mhz;    // 5180
    n.channel;          // 36
    n.security;         // "WPA2-PSK"

    n.has_ip;
    n.has_stats;
    n.has_wifi;
}
```

**WiFi example:**

```cpp
for (const auto& n : nets) {
    if (n.has_wifi) {
        std::cout << "SSID: " << n.ssid  << "  Signal: " << n.signal_dbm << " dBm\n";
    }
}
```

**Note:** On Windows, WiFi info is read via `netsh wlan show interfaces` — SSID, channel, and security type are filled automatically.

---

## 🔌 USB — `knst_vector<knst_usb_info>`

```cpp
for (const auto& u : knst_devices::usb()) {
    u.bus;              // USB bus number
    u.device_num;
    u.port_num;
    u.vid;              // Vendor ID (0x046D = Logitech)
    u.pid;              // Product ID
    u.vendor_name;
    u.product_name;
    u.serial;
    u.manufacturer;
    u.usb_class;        // Audio, Video, HID, MassStorage, Printer, ...
    u.usb_version;      // "2.10" / "3.20"
    u.max_speed_mbps;   // 480 (USB 2.0) / 5000 (USB 3.0)
    u.max_power_ma;     // 500 mA
    u.is_hub;
    u.is_root_hub;
}
```

---

## 🎛️ PCI — `knst_vector<knst_pci_info>`

```cpp
for (const auto& p : knst_devices::pci()) {
    p.domain;
    p.bus;
    p.slot;
    p.function;
    p.vendor_id;        // 0x10DE = NVIDIA
    p.device_id;
    p.vendor_name;
    p.device_name;
    p.class_name;       // "VGA compatible controller"
    p.subclass;
    p.driver;           // "nvidia" / "amdgpu" / "i915"
    p.pcie_gen;         // 1, 2, 3, 4, 5
    p.pcie_width;       // x1, x4, x8, x16
    p.power_state;      // D0, D1, D2, D3hot, D3cold
    p.iommu_group;
    p.numa_node;
}
```

---

## 🖼️ Monitors — `knst_vector<knst_monitor_info>`

```cpp
for (const auto& m : knst_devices::monitors()) {
    m.name;
    m.manufacturer;     // from EDID: "Dell", "LG"
    m.serial;
    m.product_code;
    m.year;             // 2021
    m.week;             // 42
    m.connection;       // HDMI, DisplayPort, DVI, VGA, USB_C, eDP, LVDS
    m.native_width;     // 3840
    m.native_height;    // 2160
    m.current_width;
    m.current_height;
    m.refresh_hz;       // 60 / 144
    m.physical_width_mm;   // Physical width (mm)
    m.physical_height_mm;
    m.diagonal_inch;    // 27.0
    m.gamma;
    m.hdr;
    m.is_primary;
    m.brightness;       // 0-100 (%)
}
```

**Linux:** EDID data is read from `/sys/class/drm/card*/card*-*/edid`.

**Windows:** Monitor list is obtained via `EnumDisplayDevicesW`; EDID is read from the registry (`HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\...\Device Parameters\EDID`). Monitor name, vendor, and serial number come from `WmiMonitorID` (root\WMI namespace). Brightness comes from `WmiMonitorBrightness`.

---

## ⌨️ Input Devices — `knst_vector<knst_input_info>`

```cpp
for (const auto& d : knst_devices::input()) {
    d.name;             // "Logitech USB Receiver"
    d.vendor;
    d.device_path;      // "/dev/input/event3" (Linux) / HID path (Win)
    d.vid;
    d.pid;
    d.type;             // Keyboard, Mouse, Touchpad, Touchscreen, Gamepad
    d.num_buttons;
    d.num_axes;
    d.has_force_feedback;
    d.is_connected;
}
```

**Linux:** Type is determined via `/sys/class/input/event*`. Classification is **two-stage**:

1. **Name-based (priority):** Keywords like `keyboard`, `mouse`, `touchpad`, `gamepad`, `xbox`, `logitech k/m`, `wmi hotkeys` are searched in the device name.
2. **Capability-based (fallback):** If the name does not match, `capabilities/key`, `capabilities/rel`, `capabilities/abs` files are read. Abs bit count is used to distinguish keyboard vs gamepad (≤2 bits = keyboard with volume wheel, >4 bits = gamepad).

**Filters:**
- Audio jacks (`HD-Audio`, `Rear Mic`, `Line Out`, `HDMI/DP`) are filtered out
- Virtual events (`Power Button`, `Sleep Button`, `Video Bus`, `Lid Switch`) are filtered out

This removes noise from anything that is not a real keyboard/mouse/gamepad.

**Windows:** HID devices are enumerated via `GetRawInputDeviceList` + `GetRawInputDeviceInfoW`. Product name comes from `HidD_GetProductString`. Type classification is based on usage page (Mouse, Keyboard, Gamepad, Touchpad, Remote).

---

## 🔊 Audio — `knst_audio_info`

```cpp
knst_audio_info a = knst_devices::audio();

a.default_sink;         // Default output
a.default_source;       // Default input (microphone)

for (const auto& d : a.devices) {
    d.index;
    d.name;             // "alsa_output.pci-0000_00_1f.3.analog-stereo"
    d.description;
    d.is_input;         // Is it a microphone?
    d.is_output;        // Is it a speaker?
    d.is_default;
    d.volume_percent;
    d.muted;
    d.sample_rate;
    d.channels;
}
```

**Linux:** Requires PulseAudio/PipeWire (via `pactl`). If the socket is absent, the library returns quickly.

**Windows:** Uses the `IMMDeviceEnumerator` COM API. `eRender` and `eCapture` endpoints are listed separately; product name comes from the `PKEY_Device_FriendlyName` property.

---

## 🔋 Power — `knst_power_info`

```cpp
knst_power_info p = knst_devices::power();

p.has_battery;
p.ac_connected;         // Is the charger plugged in?
p.power_profile;        // "performance" / "balanced" / "powersave"

for (const auto& b : p.batteries) {
    b.name;             // "BAT0" / "BAT1"
    b.manufacturer;
    b.model;
    b.serial;
    b.technology;       // "Li-ion" / "Li-poly"
    b.state;            // Charging, Discharging, Full, NotCharging, Empty
    b.percent;          // 0-100
    b.capacity_mah;
    b.capacity_full_mah;
    b.capacity_design_mah;
    b.voltage_mv;
    b.current_ma;
    b.power_now_mw;
    b.seconds_left;
    b.cycle_count;
    b.health_percent;   // full / design * 100
    b.ac_online;
}
```

---

## 🌡️ Sensors — `knst_vector<knst_sensor_info>`

```cpp
for (const auto& s : knst_devices::sensors()) {
    s.chip;             // "coretemp" / "k10temp" / "nct6775"
    s.name;             // "Package id 0" / "Core 0"
    s.type;             // Temperature, Fan, Voltage, Current, Power
    s.value;            // 45.0 (°C), 1200 (RPM), 12.05 (V)
    s.unit;             // "°C", "RPM", "V", "A", "W"
    s.min_value;
    s.max_value;
    s.critical;         // Critical temperature
}
```

**Linux:** `/sys/class/hwmon/*/` — works together with the `lm-sensors` package.

**Windows:** Uses the `MSAcpi_ThermalZoneTemperature` (ACPI thermal zones) + `Win32_Fan` + `Win32_VoltageProbe` + `Win32_TemperatureProbe` classes in the WMI `root\WMI` namespace.

---

## 📱 Bluetooth — `knst_bluetooth_info`

```cpp
knst_bluetooth_info b = knst_devices::bluetooth();

b.has_adapter;
b.adapter_name;         // "Intel AX200"
b.adapter_address;      // "AA:BB:CC:DD:EE:FF"
b.powered;              // Is it on?
b.discoverable;

for (const auto& d : b.devices) {
    d.address;
    d.name;             // "Sony WH-1000XM4"
    d.connected;
    d.paired;
    d.trusted;
    d.rssi;             // Signal strength
    d.type;             // "Headphones" / "Keyboard"
}
```

**Linux:** Requires the `bluetoothctl` tool. If no adapter is present, the library returns quickly (does not wait for a D-Bus timeout).

**Windows:** Adapter is obtained via `BluetoothFindFirstRadio` + `BluetoothGetRadioInfo`; paired devices are listed via `BluetoothFindFirstDevice` + `BluetoothFindNextDevice`.

---

## 📷 Cameras — `knst_vector<knst_camera_info>`

```cpp
for (const auto& c : knst_devices::cameras()) {
    c.name;             // "HD Webcam C920"
    c.device_path;      // "/dev/video0" (Linux) / USB path (Win)
    c.vid;
    c.pid;
    c.max_width;        // 1920
    c.max_height;       // 1080
    c.formats;          // ["YUYV", "MJPG", "H264"]
}
```

**Linux:** Scans `/sys/class/video4linux/`.

**Windows:** Camera devices are listed via WMI `Win32_PnPEntity` (`PNPClass='Camera'` or `PNPClass='Image'`); VID/PID is extracted from the device ID.

---

## 🖨️ Printers — `knst_vector<knst_printer_info>`

```cpp
for (const auto& p : knst_devices::printers()) {
    p.name;             // "HP_LaserJet_M404"
    p.model;
    p.status;           // "idle" / "printing" / "disabled"
    p.uri;              // "ipp://..."
    p.is_default;
    p.is_shared;
    p.accepts_jobs;
}
```

**Linux:** If the CUPS socket is absent, the library never calls `lpstat`.

**Windows:** Uses the WMI `Win32_Printer` class.

---

## 💻 System — `knst_system_info`

```cpp
knst_system_info s = knst_devices::system();

s.manufacturer;         // "Dell Inc."
s.product_name;         // "XPS 15 9520"
s.version;
s.serial;               // Service tag
s.uuid;
s.sku;
s.family;               // "XPS"
s.chassis_type;         // "Laptop" / "Desktop" / "Tower" / ...
s.chassis_serial;
s.asset_tag;
s.is_virtual_machine;   // QEMU/VMware/VirtualBox
s.is_laptop;
```

**Linux:** First tries `dmidecode` (requires root), otherwise uses `/sys/class/dmi/id/*` files (world-readable).

**Windows:** WMI `Win32_ComputerSystem`.

---

## 🐧 Operating System — `knst_os_info`

```cpp
knst_os_info o = knst_devices::os();

o.os_name;              // "Pop!_OS" / "Windows"
o.os_version;           // "22.04" / "10.0.19045"
o.os_id;                // "pop" / "ubuntu"
o.kernel_name;          // "Linux" / "Windows NT"
o.kernel_version;       // "6.8.0-76060800-generic"
o.kernel_arch;          // "x86_64"
o.hostname;
o.domain;
o.timezone;
o.user;                 // "knst"
o.home_dir;             // "/home/knst"
o.shell;                // "/bin/bash"
o.boot_time_ms;
o.uptime_seconds;       // System uptime
o.page_size;            // 4096
o.boot_id;
```

---

## 🔧 Drivers — `knst_vector<knst_driver_info>`

```cpp
for (const auto& d : knst_devices::drivers()) {
    d.name;             // "nvidia" / "amdgpu" / "ext4"
    d.version;
    d.vendor;
    d.license;          // "GPL" / "Proprietary"
    d.used_by_count;
    d.size_bytes;
    d.used_by;          // ["drm", "i2c", ...]
    d.is_builtin;
}
```

**Linux:** Reads `/proc/modules`.

**Windows:** WMI `Win32_SystemDriver`.

---

## 🎮 GPU Bridge — `knst_gpu`

The `knst_devices` namespace redirects GPU queries to `knst_gpu` (inside `knst_process.hpp`):

```cpp
knst_devices::has_gpu();                    // Is a GPU present?
knst_devices::gpu_count();                  // How many GPUs?
knst_devices::nvidia_driver_version();      // "535.183.01"
knst_devices::cuda_version();               // "12.2"

auto gpus = knst_devices::gpus();
for (const auto& g : gpus) {
    g.name;
    g.vram_total_mb;
    g.temperature_c;
    g.usage_percent;
}

auto procs = knst_devices::gpu_processes();
auto procs0 = knst_devices::gpu_processes(0);   // Only GPU 0
```

**Requirements:**
- **Linux:** NVIDIA/AMD drivers + `nvidia-smi` / `rocm-smi` tools
- **Windows:** Drivers alone are enough (NVML + DXGI + PDH native)

---

## 🛡️ Error Handling

Each category returns a structure with a `valid` flag. **Check `valid` before using the data.**

```cpp
knst_cpu_info c = knst_devices::cpu();
if (!c.valid) {
    std::cout << "Could not read CPU info\n";
    return 1;
}
// Now use it safely
std::cout << c.model_name << "\n";
```

### Error Enum

```cpp
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

const char* s = knst_device_error_string(knst_device_error::PermissionDenied);
// s = "Permission denied"
```

### Sub `has_*` Flags

Some structures have sub-flags. For example `knst_cpu_info`:

- `valid` → is there data at all?
- `has_cores` → was the core count readable?
- `has_temperature` → was the temperature readable?
- `has_cache` → is cache info present?
- `has_usage` → could usage percentage be computed?

These indicate **partial data** state. `valid=true` but `has_temperature=false` is possible — meaning CPU info is present but temperature cannot be read.

---

## ⚡ Performance Notes

### Lazy Loading

The library performs expensive work **once**:

- **DMI cache** — `dmidecode` output is read only on the first call via `std::call_once`. Subsequent `motherboard()` / `system()` calls read from cache.
- **Fast path checks** — if no Bluetooth adapter is present, `bluetoothctl` is never called; if no CUPS socket, `lpstat` is never called. This avoids waiting for tool timeouts.

### CPU Usage Measurement

`knst_devices::cpu()` on Linux does a **20 ms** `nanosleep` (between two `/proc/stat` reads). This is required for accurate usage percentage. If you only want the model name, those 20 ms are wasted.

**Optimization tip:** Call it once at application start and cache the result.

### External Tools (Linux)

| Tool | Required? | For |
|---|---|---|
| `dmidecode` | Optional | Detailed DMI (root) |
| `lspci` | Optional | PCI names |
| `iw` | Optional | WiFi SSID/signal |
| `pactl` | Optional | Audio devices |
| `bluetoothctl` | Optional | Bluetooth |
| `lpstat` | Optional | Printers |
| `nvidia-smi` | Optional | NVIDIA GPU |

Missing tools **do not error**; the related fields simply stay empty.

### Windows API Notes

- **COM initialization** — `audio()` calls `CoInitializeEx`. If your application already uses COM, `RPC_E_CHANGED_MODE` may be returned; this is handled gracefully.
- **Session 0** — if you are running as a service (Session 0), some APIs like `EnumDisplayDevices` may return empty. This is normal.
- **WMI root\WMI** — sensors are in the `root\WMI` namespace, so a separate query mechanism is used.

---

## 📚 Full Example

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // CPU
    knst_cpu_info c = knst_devices::cpu();
    if (c.valid) {
        std::cout << "CPU: " << c.model_name 
                  << " (" << c.logical_cores << " threads)\n";
    }

    // RAM
    knst_memory_info m = knst_devices::memory();
    if (m.valid) {
        auto gb = [](uint64_t b) { return b / (1024ULL * 1024 * 1024); };
        std::cout << "RAM: " << gb(m.total_bytes) << " GB\n";
    }

    // Disks
    auto disks = knst_devices::storage();
    std::cout << "\nDisks:\n";
    for (const auto& d : disks) {
        std::cout << "  " << d.device << " - " << d.model 
                  << " (" << d.total_bytes / (1024ULL * 1024 * 1024) << " GB)\n";
        for (const auto& p : d.partitions) {
            std::cout << "    └─ " << p.mount_point 
                      << " (" << p.fs_type << ")\n";
        }
    }

    // Network
    auto nets = knst_devices::network();
    std::cout << "\nNetwork interfaces:\n";
    for (const auto& n : nets) {
        if (n.is_loopback) continue;
        std::cout << "  " << n.name << " - " << n.ipv4 
                  << " (" << (n.is_wireless ? "WiFi" : "Ethernet") << ")\n";
    }

    // Monitors
    auto mons = knst_devices::monitors();
    std::cout << "\nMonitors:\n";
    for (const auto& mon : mons) {
        std::cout << "  " << mon.manufacturer 
                  << " " << mon.current_width << "x" << mon.current_height 
                  << " @ " << mon.refresh_hz << " Hz\n";
    }

    // System
    knst_system_info s = knst_devices::system();
    std::cout << "\nSystem: " << s.manufacturer << " " << s.product_name 
              << " (" << s.chassis_type << ")\n";

    // OS
    knst_os_info o = knst_devices::os();
    std::cout << "OS: " << o.os_name << " " << o.os_version << "\n";
    std::cout << "Uptime: " << o.uptime_seconds / 3600 << " hours\n";

    return 0;
}
```

### Example Output (Linux)

```
CPU: AMD Ryzen 7 5800X 8-Core Processor (16 threads)
RAM: 32 GB

Disks:
  /dev/nvme0n1 - Samsung SSD 970 EVO Plus 1TB (931 GB)
    └─ /boot/efi (vfat)
    └─ / (ext4)
  /dev/sda - WDC WD20EZBX-00AYRA0 (1863 GB)
    └─ /mnt/data (ext4)

Network interfaces:
  enp5s0 - 192.168.1.42 (Ethernet)
  wlp4s0 - 192.168.1.100 (WiFi)

Monitors:
  Dell 3840x2160 @ 60 Hz

System: ASUS PRIME B550M-A (Desktop)
OS: Pop!_OS 22.04
Uptime: 4 hours
```

### Example Output (Windows)

```
CPU: Intel(R) Core(TM) i9-13900K (32 threads)
RAM: 64 GB

Disks:
  \\.\PHYSICALDRIVE0 - Samsung SSD 990 PRO 2TB (1863 GB)
  \\.\PHYSICALDRIVE1 - WDC WD40EZAZ-00SF3B0 (3726 GB)

Network interfaces:
  Ethernet - 192.168.1.42 (Ethernet)
  Wi-Fi    - 192.168.1.100 (WiFi)

Monitors:
  Dell 3840x2160 @ 144 Hz

System: ASUS ROG STRIX Z790-E (Desktop)
OS: Windows 10.0.22631
Uptime: 8 hours
```

---

## 💡 Usage Tips

### ✅ Do

```cpp
// 1. Always check valid
knst_cpu_info c = knst_devices::cpu();
if (!c.valid) return;   // ✅ early exit

// 2. Check sub-flags
if (c.has_temperature) {
    std::cout << c.temperature_c << " °C\n";   // ✅ safe
}

// 3. Cache results
static const auto cached_cpu = knst_devices::cpu();   // ✅ 20ms sleep runs once

// 4. Filter network interfaces
for (const auto& n : knst_devices::network()) {
    if (n.is_loopback || !n.is_up) continue;   // ✅ skip loopback
    // ...
}
```

### ❌ Avoid

```cpp
// 1. Polling CPU repeatedly
for (int i = 0; i < 1000; ++i) {
    auto c = knst_devices::cpu();   // ❌ every call sleeps 20ms!
}
// 2. Reading without checking valid
std::cout << c.model_name;   // ❌ c.valid might be false

// 3. Calling every category at startup
auto cpu = knst_devices::cpu();
auto mem = knst_devices::memory();
auto usb = knst_devices::usb();
auto pci = knst_devices::pci();       // ❌ all call external tools
auto bt = knst_devices::bluetooth();  // ❌ slow startup
// ✅ Fix: call only when needed (lazy)

// 4. Forcing root-only information
// dmidecode doesn't work without root — use the /sys/class/dmi/id fallback
```

---

## 🎯 What It Buys You

`knst_devices` provides serious advantage in these situations:

- ✅ **Writing a system diagnostics tool** — all hardware through one API
- ✅ **Game engine / application startup settings** — pick quality based on system profile
- ✅ **Benchmark / telemetry** — save results together with hardware info
- ✅ **Compatibility check** — quick query like "is AVX2 available?"
- ✅ **Battery management** — behavior based on battery state
- ✅ **Inventory / stock software** — report all hardware
- ✅ **Cross-platform build** — same code runs on Linux and Windows