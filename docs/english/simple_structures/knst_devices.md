# knst_devices — Hardware Inventory Library

Hey! 👋 This document explains what the `knst_devices` library is, how to use it, and why it's the preferred choice in KernelNucleusT.

**In short:** A **hardware inventory library** that reads all system hardware (CPU, RAM, disk, network, USB, PCI, monitor, sensors, camera, printer, battery, Bluetooth...) into standardized info structures across 18 categories + 1 GPU bridge. Works via `/proc`, `/sys`, and external tools on Linux; via WMI + native APIs on Windows; via JNI + NDK APIs on Android.

> ⚠️ **BREAKING CHANGE (v2):** The old `knst_device_error` enum has been **removed**. Replaced by `knst_dev::status` + `status_string()`. User code must be updated.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **18 categories + GPU bridge** | Everything from CPU to printer under one namespace |
| **3 platforms** | Linux + Windows + **Android** (new) |
| **Standard structures** | Every query returns a `struct` with a `valid` flag |
| **Partial-data flags** | `has_cores`, `has_temperature`, ... — missing data distinguishable |
| **Lazy loading** | DMI cache, `pactl`/`bluetoothctl` fast-path checks |
| **UTF-16 strings** | All text fields are `knst_c16string` — Windows compatible |
| **Vector returns** | USB, PCI, storage, network categories return `knst_vector<T>` |
| **`knst_dev::status`** | New error model — `result<T>` + `status_string()` |
| **`knst_dev::argv`** | No shell injection — argv vector |
| **`knst_dev::run_tool`** | Safe tool execution (`ToolMissing`, exit code handling) |
| **`knst_dev::sysfs_write/read`** | `/sys/` path guard + error mapping |
| **`knst_dev::uevent_monitor`** | Kernel hotplug (Linux) — USB/disk/input plug events |
| **`knst_dev::unique_fd`** | RAII fd wrapper (POSIX) |
| **Chassis type normalization** | DMI numeric code → "Laptop"/"Tower"/"Tablet" |
| **JSON bridge** | `full_inventory_json()`, `save_inventory_json()` |
| **Full Android support** | JNI + NDK ASensorManager + ACameraManager + UsbManager |

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
        std::cout << "Cores: " << cpu.physical_cores  << " physical / "
                  << cpu.logical_cores << " logical\n";
        std::cout << "Max freq: " << cpu.max_mhz << " MHz\n";
        std::cout << "Stepping: 0x" << std::hex << cpu.stepping
                  << "  Microcode: 0x" << cpu.microcode_ver << std::dec << "\n";
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

    // JSON inventory
#if defined(KNST_JSON_HPP)
    knst_json inventory = knst_devices::full_inventory_json();
    knst_json_buffer out;
    inventory.dump(out, knst_json_dump_options::pretty(2));
    std::cout << out.data() << "\n";
#endif

    return 0;
}
```

---

## 📋 18 Categories + GPU Bridge

| # | Category | Function | Return Type | Linux | Windows | Android |
|---|---|---|---|---|---|---|
| 1 | **CPU** | `cpu()` | `knst_cpu_info` | ✅ | ✅ | ✅ |
| 2 | **Motherboard + BIOS** | `motherboard()` | `knst_motherboard_info` | ✅ | ✅ | ✅ (props) |
| 3 | **Memory** | `memory()` | `knst_memory_info` | ✅ | ✅ | ✅ |
| 4 | **Storage** | `storage()` | `knst_vector<knst_storage_info>` | ✅ | ✅ | ✅ (JNI) |
| 5 | **Network** | `network()` | `knst_vector<knst_network_info>` | ✅ | ✅ | ✅ + WiFi enrichment |
| 6 | **USB** | `usb()` | `knst_vector<knst_usb_info>` | ✅ | ✅ | ✅ (JNI UsbManager) |
| 7 | **PCI** | `pci()` | `knst_vector<knst_pci_info>` | ✅ | ✅ | ⚠️ sysfs restricted |
| 8 | **Monitors** | `monitors()` | `knst_vector<knst_monitor_info>` | ✅ | ✅ | ⚠️ mostly empty |
| 9 | **Input devices** | `input()` | `knst_vector<knst_input_info>` | ✅ | ✅ | ✅ |
| 10 | **Audio** | `audio()` | `knst_audio_info` | ✅ | ✅ | ✅ (JNI AudioManager) |
| 11 | **Power/Battery** | `power()` | `knst_power_info` | ✅ | ✅ | ✅ (sysfs + JNI) |
| 12 | **Sensors** | `sensors()` | `knst_vector<knst_sensor_info>` | ✅ | ✅ | ✅ (NDK ASensorManager) |
| 13 | **Bluetooth** | `bluetooth()` | `knst_bluetooth_info` | ✅ | ✅ | ✅ (JNI Adapter) |
| 14 | **Cameras** | `cameras()` | `knst_vector<knst_camera_info>` | ✅ | ✅ | ✅ (NDK ACameraManager) |
| 15 | **Printers** | `printers()` | `knst_vector<knst_printer_info>` | ✅ | ✅ | ✅ (mDNS helper) |
| 16 | **System** | `system()` | `knst_system_info` | ✅ | ✅ | ✅ (props) |
| 17 | **Operating System** | `os()` | `knst_os_info` | ✅ | ✅ | ✅ |
| 18 | **Drivers** | `drivers()` | `knst_vector<knst_driver_info>` | ✅ | ✅ | ✅ (/proc/modules) |
| ➕ | **GPU bridge** | `gpus()` | `knst_vector<knst_gpu_info>` | ✅ | ✅ | ✅ |

**Symbol key:** ✅ = full support · ⚠️ = partial/conditional · ❌ = none

---

## 🖥️ CPU — `knst_cpu_info`

```cpp
knst_cpu_info c = knst_devices::cpu();

// Basic
c.vendor;              // "GenuineIntel" / "AuthenticAMD" / "ARM"
c.model_name;          // "Intel(R) Core(TM) i7-9700K"
c.architecture;        // "x86_64" / "aarch64"
c.physical_cores;      // 8
c.logical_cores;       // 8 (equal if HT disabled)
c.socket_count;        // 1
c.base_mhz;            // 3600
c.max_mhz;             // 4900
c.current_mhz;         // current freq
c.cache_l1_kb;         // in KB
c.cache_l2_kb;
c.cache_l3_kb;
c.temperature_c;       // °C (coretemp/k10temp/zenpower)
c.usage_percent;       // instantaneous usage %
c.load_avg_1;          // /proc/loadavg (Linux)
c.load_avg_5;
c.load_avg_15;
c.stepping;            // CPU stepping (e.g. 0x0A)
c.microcode_ver;       // microcode revision (e.g. 0xEA)

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
c.valid;               // overall validity
c.has_cores;           // core info read?
c.has_frequency;       // freq info available?
c.has_cache;           // cache info available?
c.has_temperature;     // temperature readable?
c.has_usage;           // usage percent measurable?
```

### CPU usage measurement — 200 ms cache

`cpu()` on Linux reads `/proc/stat` twice (20 ms apart) to compute usage %. This is **cached for 200 ms**. Calling again within the same 200 ms window does no disk I/O.

### Partial data

```cpp
knst_cpu_info c = knst_devices::cpu();
if (!c.valid) return;

std::cout << c.model_name << "\n";
if (c.has_temperature)
    std::cout << "Temp: " << c.temperature_c << " °C\n";
if (c.has_cache)
    std::cout << "L3: " << c.cache_l3_kb << " KB\n";
```

---

## 🔧 Motherboard + BIOS — `knst_motherboard_info`

```cpp
knst_motherboard_info m = knst_devices::motherboard();

// Motherboard
m.manufacturer;        // "ASUSTeK COMPUTER INC."
m.product;             // "PRIME B550M-A"
m.version;             // "Rev X.0x"
m.serial;              // serial number
m.uuid;                // system UUID

// BIOS
m.bios_vendor;         // "American Megatrends Inc."
m.bios_version;        // "3202"
m.bios_date;           // "08/13/2024"

// Boot / security
m.uefi_boot;           // UEFI?
m.secure_boot;         // Secure Boot enabled?
m.has_tpm;             // TPM 2.0 present?
m.tpm_version;         // "2.0"

// Chassis
m.chassis_type;        // "Desktop" / "Laptop" / "Tower" / "Tablet"
m.chassis_serial;

// Flags
m.valid;
m.has_bios;
m.has_board;
m.has_chassis;
```

### Chassis type normalization (new)

DMI chassis type returns a numeric code (`9` = Laptop). The library converts it via `chassis_type_name()`:

```cpp
// internal:
knst_devices::detail::chassis_type_name(9);   // "Laptop"
knst_devices::detail::chassis_type_name(3);   // "Desktop"
knst_devices::detail::chassis_type_name(30);  // "Tablet"

// automatic on user side:
knst_motherboard_info m = knst_devices::motherboard();
// m.chassis_type is already "Laptop" (not numeric)
```

**Supported codes (DMTF SMBIOS):** chassis types 1-36.

### DMI cache

On the first `motherboard()` or `system()` call, DMI info is read (`dmidecode -q` → `/sys/class/dmi/id/*` fallback). **`std::call_once`** runs it only once.

---

## 💾 Memory — `knst_memory_info`

```cpp
knst_memory_info m = knst_devices::memory();

// System memory
m.total_bytes;
m.used_bytes;
m.free_bytes;
m.available_bytes;
m.cached_bytes;
m.buffers_bytes;

// Swap
m.swap_total;
m.swap_used;
m.swap_free;
m.has_swap;

// Slot info
m.slot_count;          // total slots
m.slots_used;          // used slots

// Memory modules
for (const auto& mod : m.modules) {
    mod.slot_index;
    mod.locator;       // "DIMM_A1"
    mod.size_bytes;
    mod.speed_mhz;
    mod.configured_mhz;
    mod.type;          // "DDR4" / "DDR5" / "LPDDR5"
    mod.form_factor;   // "DIMM" / "SODIMM"
    mod.manufacturer;  // "Samsung"
    mod.serial;
    mod.part_number;   // "KHX3200C16D4/8G"
    mod.rank;
    mod.voltage_mv;
    mod.ecc;
    mod.valid;
}

// Flags
m.valid;
m.has_modules;
m.has_slot_count;
```

**Linux:** `/proc/meminfo` + `dmidecode -t memory` (root required; otherwise `modules` stays empty).

**Windows:** `GlobalMemoryStatusEx` + WMI `Win32_PhysicalMemory`. MemoryType code (20=DDR, 24=DDR3, 26=DDR4, 34=DDR5, ...) is translated into `mod.type`.

---

## 💽 Storage — `knst_vector<knst_storage_info>`

```cpp
auto disks = knst_devices::storage();

for (const auto& s : disks) {
    s.device;              // "/dev/nvme0n1" / "\\\\.\\PHYSICALDRIVE0"
    s.model;               // "Samsung SSD 970 EVO Plus 1TB"
    s.serial;
    s.firmware;
    s.vendor;
    s.type;                // HDD, SSD, NVMe, USB, eMMC, SD_Card, Optical, Floppy, RAID, Virtual
    s.total_bytes;
    s.free_bytes;
    s.used_bytes;
    s.temperature_c;       // for NVMe
    s.is_removable;
    s.is_system_disk;
    s.is_solid_state;
    s.rotational_rpm;      // 5400/7200 for HDD

    // SMART (if available)
    s.has_smart;
    s.smart_passed;
    s.smart_power_on_hours;
    s.smart_bytes_written;
    s.smart_reallocated_sectors;
    s.smart_pending_sectors;
    s.smart_percent_used;

    // Partitions
    for (const auto& p : s.partitions) {
        p.device;
        p.mount_point;
        p.fs_type;         // "ext4" / "btrfs" / "vfat" / "ntfs"
        p.total_bytes;
        p.used_bytes;
        p.free_bytes;
        p.uuid;
        p.label;
        p.is_boot;
        p.is_removable;
    }
    s.has_partitions;
    s.has_temperature;
    s.has_model;
}
```

### LVM/LUKS support (Linux)

`/dev/mapper/*` and `/dev/dm-*` devices are **automatically resolved to their physical disks**. This works correctly on systems using LUKS + LVM (Pop!_OS, Ubuntu, Fedora, RHEL).

```cpp
// /dev/mapper/cryptroot → /dev/nvme0n1p3 → /dev/nvme0n1
// The library finds /dev/nvme0n1 at the top of the chain
// and attaches all partitions there.
```

Recursive resolution up to 8 levels deep. A `dm-*` slave may contain another `dm-*` (nested LVM).

### Disk type override (Linux)

The rotational heuristic is unreliable. The library **overrides by name prefix**:

| Prefix | Type |
|---|---|
| `sr*` | Optical (is_removable=true) |
| `md*` | RAID |
| `fd*` | Floppy (is_removable=true) |
| `mmcblk*` | SD_Card (removable) or eMMC |
| `nvme*` | NVMe |
| Removable `sd*` | USB |

### Windows SSD/HDD detection

`MSFT_PhysicalDisk` (`root\Microsoft\Windows\Storage`) → `MediaType`:
- `3` = HDD
- `4` = SSD
- `BusType = 17` = NVMe
- `BusType = 7` = USB

**System disk detection:** `Win32_DiskPartition` → find the `DiskIndex` where `BootPartition=true` → matches `\\.\PHYSICALDRIVE0` → `is_system_disk = true`.

---

## 🌐 Network — `knst_vector<knst_network_info>`

```cpp
auto nets = knst_devices::network();

for (const auto& n : nets) {
    n.name;             // "wlan0", "eth0", "lo", "rmnet0"
    n.description;
    n.mac;
    n.ipv4;
    n.ipv6;
    n.netmask;
    n.gateway;
    n.dns;

    n.type;             // Ethernet, WiFi, Loopback, Bridge, Virtual, Cellular, Bluetooth, Other
    n.mtu;
    n.speed_mbps;
    n.is_up;
    n.is_running;
    n.is_loopback;
    n.is_wireless;
    n.is_virtual;
    n.duplex;

    n.rx_bytes;
    n.tx_bytes;
    n.rx_packets;
    n.tx_packets;
    n.rx_errors;
    n.tx_errors;
    n.rx_dropped;
    n.tx_dropped;

    // WiFi
    n.ssid;
    n.signal_dbm;
    n.frequency_mhz;
    n.channel;
    n.security;         // "WPA2-PSK" / "WPA3-SAE"

    n.valid;
    n.has_ip;
    n.has_stats;
    n.has_wifi;
}
```

### Linux WiFi

Via `iw dev <iface> link`:
- SSID
- freq (2407-2484 → 2.4 GHz, 5000+ → 5 GHz)
- signal (dBm)
- channel (computed from freq)

### Windows WiFi

**WLAN API** (`WlanOpenHandle` + `WlanEnumInterfaces` + `WlanQueryInterface`) — **language-independent**, no localized `netsh` output:
- SSID (binary → string)
- signal quality % → dBm (`q/2 - 100`)
- channel (opcode query)
- security (auth algorithm → "WPA2-PSK" etc.)
- frequency (computed from channel)

### Android WiFi enrichment

Sysfs is restricted by SELinux. The library **calls a JNI helper** to obtain SSID/BSSID/RSSI/link speed/frequency from `WifiManager.getConnectionInfo()` and applies them to interfaces named `wlan*`/`p2p*`.

If sysfs can't be read, **name-based type inference**:
- `wlan*`, `wifi*`, `p2p*`, `ap0*` → WiFi
- `rmnet*`, `ccmni*`, `wwan*`, `pdp_ip*` → Cellular
- `bt-pan*` → Bluetooth
- `eth*` → Ethernet
- `r_*`, `dummy*` → Virtual

---

## 🔌 USB — `knst_vector<knst_usb_info>`

```cpp
for (const auto& u : knst_devices::usb()) {
    u.bus;
    u.device_num;
    u.port_num;
    u.vid;              // 0x046D (Logitech)
    u.pid;
    u.vendor_name;
    u.product_name;
    u.serial;
    u.manufacturer;
    u.usb_class;        // Audio, Video, HID, MassStorage, Printer, Hub, Wireless, CDC, Vendor, Other
    u.usb_version;      // "2.10" / "3.20"
    u.max_speed_mbps;   // 480 / 5000 / 10000
    u.max_power_ma;     // 500 mA
    u.is_hub;
    u.is_root_hub;
    u.valid;
}
```

**Linux:** `/sys/bus/usb/devices/*` — interface nodes (`1-1:1.0`) skipped, only devices returned.

**Windows:** WMI `Win32_PnPEntity WHERE DeviceID LIKE 'USB%'` + class inference from `Service` column:
- `usbhub` / `USBHUB` → Hub
- `usbhid` / `HidUsb` / `kbdhid` / `mouhid` → HID
- `usbvideo` / `uvcvideo` → Video
- `usbaudio` / `snd-usb-audio` → Audio
- `USBSTOR` → MassStorage
- `usbprint` → Printer
- `usbser` → CDC

`usbccgp` (composite parent) → inferred from product_name.

**Android:** JNI `UsbManager.getDeviceList()` — OTG devices listed.

---

## 🎛️ PCI — `knst_vector<knst_pci_info>`

```cpp
for (const auto& p : knst_devices::pci()) {
    p.domain;
    p.bus;
    p.slot;
    p.function;
    p.vendor_id;        // 0x10DE (NVIDIA)
    p.device_id;
    p.vendor_name;
    p.device_name;
    p.class_name;       // "VGA compatible controller"
    p.subclass;
    p.driver;           // "nvidia" / "amdgpu" / "i915"
    p.pcie_gen;         // 1, 2, 3, 4, 5
    p.pcie_width;       // 1, 4, 8, 16
    p.power_state;      // D0, D1, D2, D3hot, D3cold
    p.iommu_group;
    p.numa_node;
    p.valid;
}
```

**Linux PCIe speed/width:** `max_link_speed` / `max_link_width` preferred (`current_*` is **misleading** — ASPM drops to Gen1 when idle).

**Linux device names:** `lspci -vmm -nn` parse. Multi-function slots, domains, long names all parse correctly.

**Windows:** WMI `Win32_PnPEntity WHERE DeviceID LIKE 'PCI%'` — class inferred from `PNPClass` (Display, Net, MEDIA, SCSIAdapter, HDC, USB, System, MEMORY, BLUETOOTH, HIDClass, SecurityDevices).

**Note:** PCIe gen/width not available on Windows via WMI. Read from sysfs on Linux.

---

## 🖼️ Monitors — `knst_vector<knst_monitor_info>`

```cpp
for (const auto& m : knst_devices::monitors()) {
    m.index;
    m.name;
    m.manufacturer;     // from EDID: "DEL", "GSM"
    m.serial;
    m.product_code;
    m.year;             // 1990 + edid[17]
    m.week;
    m.connection;       // HDMI, DisplayPort, DVI, VGA, USB_C, LVDS, eDP, Internal, Wireless, Other
    m.native_width;     // 3840
    m.native_height;    // 2160
    m.current_width;
    m.current_height;
    m.refresh_hz;       // pixel_clock / (htotal * vtotal)
    m.physical_width_mm;
    m.physical_height_mm;
    m.diagonal_inch;    // sqrt(w_inch² + h_inch²)
    m.gamma;
    m.hdr;
    m.is_primary;
    m.brightness;       // 0-100 (backlight)
    m.valid;
}
```

### Linux EDID parsing

`/sys/class/drm/card*-*/edid` binary read:
- Header check (`00 FF FF FF`)
- Manufacturer ID (3× 5-bit)
- Product code, serial, week, year
- Physical size (edid[21] × 10 mm, edid[22] × 10 mm)
- Detailed Timing Descriptor (edid[54-61]): pixel clock, h/v active/blank → refresh
- Gamma (edid[23])

### Windows EDID + WMI

`EnumDisplayDevicesW` gets the monitor device path. EDID read from registry:
```
HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\<VENDOR>\<INSTANCE>\Device Parameters\EDID
```

`WmiMonitorID` (`root\WMI`) → friendly name, manufacturer name, serial, year, week. `WmiMonitorBrightness` → brightness.

### Connection type detection (Windows)

No direct API in Win32. Keywords searched in adapter and monitor `DeviceString`:
- `HDMI` → HDMI
- `DisplayPort` / `DP` → DisplayPort
- `DVI` → DVI
- `VGA` → VGA
- `USB-C` / `USB Type-C` → USB_C
- `eDP` → eDP
- `LVDS` → LVDS

---

## ⌨️ Input Devices — `knst_vector<knst_input_info>`

```cpp
for (const auto& d : knst_devices::input()) {
    d.name;             // "Logitech USB Receiver"
    d.vendor;
    d.device_path;      // "/dev/input/event3" / HID path
    d.vid;
    d.pid;
    d.type;             // Keyboard, Mouse, Touchpad, Touchscreen, Joystick, Gamepad, Tablet, Remote, Other
    d.num_buttons;
    d.num_axes;
    d.has_force_feedback;
    d.is_connected;
    d.valid;
}
```

### Linux classification (two-stage)

**1. Name-based (priority):**
- `keyboard`, `keys` → Keyboard
- `mouse`, `trackball`, `trackpoint` → Mouse
- `touchpad`, `trackpad`, `synaptics`, `elan` → Touchpad
- `touchscreen`, `touch screen` → Touchscreen
- `gamepad`, `joystick`, `controller`, `xbox`, `playstation`, `dualshock`, `dualsense`, `8bitdo` → Gamepad
- `logitech k*` → Keyboard (no HID descriptor)
- `logitech m*`, `logitech mx*` → Mouse
- `wmi hotkeys`, `hotkey`, `acpi` → Other

**2. Capability-based (fallback):**
- `capabilities/key` + no `rel` + no `abs` → Keyboard
- `key` + `abs` (≤2 bits) → Keyboard (multimedia)
- `rel` + no `abs` → Mouse
- `abs` (≤4 bits) → Touchpad
- `key` + `abs` (>4 bits) → Gamepad
- `abs` (no `rel`) → Touchpad / Gamepad

**Filters:** Audio jacks (`hd-audio`, `rear mic`, `line out`), virtual events (`power button`, `sleep button`, `video bus`, `lid switch`, `pc speaker`) filtered out.

### Windows Raw Input

`GetRawInputDeviceList` + `GetRawInputDeviceInfoW` (RIDI_DEVICENAME + RIDI_DEVICEINFO):
- `RIM_TYPEMOUSE` → Mouse
- `RIM_TYPEKEYBOARD` → Keyboard
- `RIM_TYPEHID` → HID usage page analysis:
  - `0x01` (Generic Desktop) + usage:
    - `0x02` → Mouse
    - `0x04` / `0x05` / `0x07` / `0x08` → Gamepad
    - `0x06` → Keyboard
    - `0x0D` → Tablet
  - `0x0D` (Digitizer) → Touchscreen
  - `0x0C` (Consumer Control) → Remote

Product name via `HidD_GetProductString`.

**VID/PID:** From `RIDI_DEVICEINFO` if available, else `VID_`/`PID_` regex on device_path.

**Filters:** `ROOT\` devices (RDP mouse/keyboard) skipped. VID/PID+type+name duplicates skipped.

---

## 🔊 Audio — `knst_audio_info`

```cpp
knst_audio_info a = knst_devices::audio();

a.default_sink;         // default output (description or name)
a.default_source;       // default input

for (const auto& d : a.devices) {
    d.index;
    d.name;             // "alsa_output.pci-0000_00_1f.3.analog-stereo"
    d.description;      // "Built-in Audio Analog Stereo"
    d.is_input;         // microphone?
    d.is_output;        // speaker?
    d.is_default;
    d.volume_percent;
    d.muted;
    d.sample_rate;
    d.channels;
    d.valid;
}

a.valid;
```

### Linux audio fast-path

Socket check — proceed if present, **fast return** otherwise:
- `/run/user/<uid>/pulse/native` (PulseAudio)
- `/run/user/<uid>/pipewire-0` (PipeWire)

If neither → `pactl` is never called → app starts fast.

Parse: `pactl list sinks` and `pactl list sources` — device start (`Sink #` / `Source #`) with index, `Name:` and `Description:` lines. Default marking via `pactl get-default-sink` / `get-default-source`.

### Windows audio

`IMMDeviceEnumerator` (COM):
- `CoInitializeEx(COINIT_MULTITHREADED)`
- `EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, ...)` → output
- `EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, ...)` → input
- `PKEY_Device_FriendlyName` property → description
- `GetDefaultAudioEndpoint` → default marking

`RPC_E_CHANGED_MODE` (another thread already did COM init) tolerated.

### Android audio

JNI `AudioManager.getDevices(GET_DEVICES_ALL)` → `KnstDevicesHelper.getAudioDevicesInfo()`:
- Type (AudioDeviceInfo.TYPE_*) → name mapping (EARPIECE, SPEAKER, WIRED_HEADSET, ...)
- Product name (compared against `Build.MODEL` — `(built-in)` if equal)
- isSink → is_output
- isSource → is_input

---

## 🔋 Power — `knst_power_info`

```cpp
knst_power_info p = knst_devices::power();

p.has_battery;
p.ac_connected;
p.power_profile;        // "performance" / "balanced" / "powersave" (Linux)

for (const auto& b : p.batteries) {
    b.name;             // "BAT0" / "BAT1"
    b.manufacturer;
    b.model;
    b.serial;
    b.technology;       // "Li-ion" / "Li-poly"
    b.state;            // Charging, Discharging, Full, NotCharging, Empty, Unknown
    b.percent;
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
    b.valid;
}

p.valid;
```

### Linux peripherals filter

`/sys/class/power_supply/*/type` = `Battery` listed. **However**, if a `scope` file exists and isn't `System` (wireless mouse/keyboard battery), **skipped**.

### AC propagation

`readdir` order isn't guaranteed — AC adapter may be read after BAT0. The library **first collects all batteries**, then propagates `ac_seen && ac_on` to all batteries.

### Windows power

`GetSystemPowerStatus` — `BatteryFlag & 128` = no battery. `ACLineStatus` = AC. `BatteryLifePercent`, `BatteryLifeTime`, `BatteryFlag & 0x0F` → state.

### Android power (3 paths)

**1. sysfs fallback** (fast, works on most devices):
- `/sys/class/power_supply/battery/capacity`
- `/sys/class/power_supply/battery/status`
- `/sys/class/power_supply/battery/technology`
- `/sys/class/power_supply/battery/charge_full`

**2. ACTION_BATTERY_CHANGED sticky broadcast** (reliable for state + plugged):
- `registerReceiver(null, IntentFilter(ACTION_BATTERY_CHANGED))`
- `getIntExtra("status")`, `getIntExtra("plugged")`, `getIntExtra("level")`

**3. BatteryManager** (last resort):
- `getIntProperty(BATTERY_PROPERTY_CAPACITY)` = 4

---

## 🌡️ Sensors — `knst_vector<knst_sensor_info>`

```cpp
for (const auto& s : knst_devices::sensors()) {
    s.chip;             // "coretemp" / "k10temp" / "nct6775" / "ACPI"
    s.name;             // "Package id 0" / "Core 0"
    s.type;             // Temperature, Fan, Voltage, Current, Power, ...
    s.value;
    s.unit;             // "°C", "RPM", "V", "A", "W"
    s.min_value;
    s.max_value;
    s.critical;
    s.valid;
}
```

### Sensor types (enum updated)

```cpp
enum class knst_sensor_type : uint8_t {
    Unknown = 0, Temperature, Fan, Voltage, Current, Power, Humidity, Proximity, Light,
    // Android / mobile:
    Accelerometer, Gyroscope, Magnetometer, MagneticField,
    Pressure, Gravity, LinearAcceleration, RotationVector,
    StepCounter, StepDetector, HeartRate,
    AmbientTemperature, RelativeHumidity
};
```

### Linux hwmon

`/sys/class/hwmon/*/`:
- `temp*_input` → Temperature, value/1000 °C
- `fan*_input` → Fan, RPM
- `in*_input` → Voltage, value/1000 V
- `curr*_input` → Current, value/1000 A
- `power*_input` → Power, value/1_000_000 W
- `*_label` → readable name (falls back to filename)

### Windows WMI

`root\WMI`:
- **`MSAcpi_ThermalZoneTemperature`** — `CurrentTemperature` tenths-Kelvin → °C
- **`MSAcpi_ThermalZoneInformation`** — **fallback** (if primary empty). Some AMD Ryzen BIOSes return empty from the first class.

`root\CIMV2`:
- `Win32_Fan` — `DesiredSpeed` RPM
- `Win32_VoltageProbe` — `CurrentReading` mV → V
- `Win32_TemperatureProbe` — `CurrentReading` tenths-Kelvin → °C

### Android NDK

`ASensorManager_getInstance()` + `ASensorManager_getSensorList()`:
- `ASENSOR_TYPE_ACCELEROMETER` (1) → Accelerometer, m/s²
- `ASENSOR_TYPE_MAGNETIC_FIELD` (2) → Magnetometer, µT
- `ASENSOR_TYPE_GYROSCOPE` (4) → Gyroscope, rad/s
- `ASENSOR_TYPE_LIGHT` (5) → Light, lx
- `ASENSOR_TYPE_PROXIMITY` (8) → Proximity, cm
- `ASENSOR_TYPE_PRESSURE` (6) → Pressure, hPa
- `ASENSOR_TYPE_RELATIVE_HUMIDITY` (12) → RelativeHumidity, %
- `ASENSOR_TYPE_AMBIENT_TEMPERATURE` (13) → AmbientTemperature, °C

**Note:** `ASensor*` API provides passive listing. Real-time reading requires `ASensorEventQueue` (not in this library — device listing only).

---

## 📱 Bluetooth — `knst_bluetooth_info`

```cpp
knst_bluetooth_info b = knst_devices::bluetooth();

b.has_adapter;
b.adapter_name;         // "Intel AX200"
b.adapter_address;      // "AA:BB:CC:DD:EE:FF"
b.powered;
b.discoverable;
b.valid;

for (const auto& d : b.devices) {
    d.address;
    d.name;
    d.connected;
    d.paired;
    d.trusted;
    d.rssi;
    d.type;
    d.valid;
}
```

### Linux fast-path

If `/sys/class/bluetooth/` is empty or absent, `bluetoothctl` is **never called** — no D-Bus timeout wait.

### Windows

`BluetoothFindFirstRadio` + `BluetoothGetRadioInfo` (adapter). `BluetoothFindFirstDevice` + `BluetoothFindNextDevice` (devices).

**Note:** `powered` not exposed by Windows API — always `true` (if adapter present). `discoverable` likewise.

---

## 📷 Cameras — `knst_vector<knst_camera_info>`

```cpp
for (const auto& c : knst_devices::cameras()) {
    c.name;             // "HD Webcam C920" / "camera.id 0 (Back)"
    c.device_path;      // "/dev/video0" / camera ID
    c.vid;
    c.pid;
    c.max_width;
    c.max_height;
    c.formats;
    c.valid;
}
```

### Linux V4L2 capability check

`/dev/video*` devices checked via `VIDIOC_QUERYCAP`. **Only devices with `V4L2_CAP_VIDEO_CAPTURE`** flag count as cameras. UVC webcam metadata nodes (`videoN+1`) skipped.

### Windows WMI

`Win32_PnPEntity WHERE PNPClass='Camera' OR Service='usbvideo'`. **Scanner filter:**
- Name or path containing `scan`, `escl`, `print`, `officejet`, `laserjet`, `deskjet` → skipped
- Path containing `escl` or `usbscan` → skipped

### Android NDK

`ACameraManager_create()` + `ACameraManager_getCameraIdList()`:
- `ACAMERA_LENS_FACING` → "(Front)" / "(Back)" / "(External)"
- `ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE` → max_width / max_height

---

## 🖨️ Printers — `knst_vector<knst_printer_info>`

```cpp
for (const auto& p : knst_devices::printers()) {
    p.name;
    p.model;
    p.status;           // "idle" / "printing" / "disabled"
    p.uri;
    p.is_default;
    p.is_shared;
    p.accepts_jobs;
    p.valid;
}
```

### Linux fast-path

If `/run/cups/cups.sock` or `/var/run/cups/cups.sock` absent, `lpstat` is **never called**.

`lpstat -p -d` output parsed:
- `printer <name> is idle|now printing|disabled` → status
- `system default destination: <name>` → is_default

### Windows

WMI `Win32_Printer` — `Name`, `DriverName`, `PrinterStatus`, `PortName`, `Default`, `Shared`, `WorkOffline`.

`PrinterStatus` numeric:
- `3` = idle
- `4` = printing
- `5` = warmup

`accepts_jobs = (WorkOffline != "True")`

### Android

`KnstPrintDiscoveryHelper.discover(4000)` — mDNS `_ipp._tcp` / `_ipps._tcp` scan. Format `name|host|port|uri` parsed.

---

## 💻 System — `knst_system_info`

```cpp
knst_system_info s = knst_devices::system();

s.manufacturer;         // "Dell Inc."
s.product_name;         // "XPS 15 9520"
s.version;
s.serial;
s.uuid;
s.sku;
s.family;
s.chassis_type;         // "Laptop" / "Desktop" / "Tablet" (normalized)
s.chassis_serial;
s.asset_tag;
s.is_virtual_machine;   // QEMU / VMware / VirtualBox / Hyper-V / Xen / ... detection
s.is_laptop;            // heuristic from chassis_type
s.valid;
```

### Virtual machine detection

These keywords searched in DMI manufacturer/product_name:
- `QEMU`, `VMware, Inc.`, `innotek GmbH`, `Xen`, `Amazon EC2`, `Google`, `OpenStack Foundation`, `Oracle Corporation`, `Parallels Software International Inc.`, `DigitalOcean`
- `VirtualBox`, `VMware`, `Virtual Machine`, `KVM`, `Bochs`, `Xen HVM`

### OEM placeholder filtering

These strings treated as empty:
- `"Not Specified"`, `"Not Present"`
- `"To be filled by O.E.M."`, `"To Be Filled By O.E.M."`
- `"Default string"`, `"System Product Name"` (ASUS/Gigabyte default)
- `"System Version"`, `"System Serial Number"`, `"SKU"`, `"Base Board Product Name"`

### Windows chassis type

First int from `Win32_SystemEnclosure.ChassisTypes` array. SMBIOS codes match Linux (3=Desktop, 9=Laptop, 30=Tablet, ...) — Windows has a separate lookup table.

### Android

From `ro.product.manufacturer`, `ro.product.model`, `ro.serialno`, `ro.product.device`, `ro.hardware`. Serial tried in order:
1. `ro.serialno`
2. `ro.boot.serialno`
3. `ril.serialnumber`
4. `persist.radio.serialno`
5. `Build.getSerial()` (JNI helper)
6. `"unknown"`

Emulator detection: `ro.hardware` = `goldfish` / `ranchu` or model contains `sdk`/`Emulator`.

---

## 🐧 Operating System — `knst_os_info`

```cpp
knst_os_info o = knst_devices::os();

o.os_name;              // "Pop!_OS" / "Windows" / "Android"
o.os_version;           // "22.04" / "10.0.19045" / "14"
o.os_id;                // "pop" / "ubuntu"
o.kernel_name;          // "Linux" / "Windows NT"
o.kernel_version;       // "6.8.0-...-generic"
o.kernel_arch;          // "x86_64" / "aarch64"
o.hostname;
o.domain;
o.timezone;
o.user;                 // "knst"
o.home_dir;             // "/home/knst"
o.shell;                // "/bin/bash"
o.boot_time_ms;
o.uptime_seconds;
o.page_size;            // 4096
o.boot_id;
o.valid;
```

### Linux

- `/etc/os-release` — `NAME`, `VERSION_ID`, `ID`
- `uname()` — kernel name/version/arch/hostname
- `/proc/uptime` — uptime
- `CLOCK_REALTIME` - uptime × 1000 = boot_time_ms
- `getpwuid(getuid())` — user, home
- `getenv("SHELL")` — shell
- `/etc/timezone` or `TZ` env — timezone
- `/proc/sys/kernel/random/boot_id` — boot_id (hashed)

### Windows

- `RtlGetVersion` (ntdll) — accurate version (GetVersionEx deprecated)
- `GetNativeSystemInfo` — arch
- `GetComputerNameW` / `GetUserNameW`
- `USERPROFILE` → home_dir
- `COMSPEC` → shell
- `GetDynamicTimeZoneInformation` → timezone + UTC offset
- `GetTickCount64` → uptime
- `GetSystemInfo` → page_size

### Android

- `ro.build.version.release` — Android version
- `ro.build.id` — build id
- `ro.kernel.version`, `ro.product.cpu.abi`, `net.hostname`
- `uname()` fallback
- `CLOCK_BOOTTIME` → uptime (for denied `/proc` cases)
- `/proc/uptime` fallback
- `/proc/stat btime` last resort

---

## 🔧 Drivers — `knst_vector<knst_driver_info>`

```cpp
for (const auto& d : knst_devices::drivers()) {
    d.name;
    d.version;
    d.vendor;
    d.license;
    d.used_by_count;
    d.size_bytes;
    d.used_by;          // vector<string>
    d.is_builtin;
    d.valid;
}
```

**Linux:** `/proc/modules` — name, size, used_by_count, used_by (comma-separated), state.

**Windows:** WMI `Win32_SystemDriver` — `Name`, `DisplayName`, `State`, `StartMode`, `PathName`. If `StartMode = "Boot" / "System"`, `is_builtin = true`.

**Android:** `/proc/modules` (same as Linux — mostly works).

---

## 🎮 GPU Bridge — `knst_devices::gpu_*`

`knst_devices` forwards GPU queries to `knst_gpu` (in knst_process.hpp):

```cpp
knst_devices::has_gpu();                    // any GPU?
knst_devices::gpu_count();                  // how many GPUs?
knst_devices::nvidia_driver_version();      // "535.183.01"
knst_devices::cuda_version();               // "12.2"

auto gpus = knst_devices::gpus();           // knst_vector<knst_gpu_info>
auto procs = knst_devices::gpu_processes(); // knst_vector<knst_gpu_process>
auto procs0 = knst_devices::gpu_processes(0);   // GPU 0 only
```

**Type definitions `knst_gpu_info` and `knst_gpu_process` come from `knst_process.hpp`.**

**Platform:**
- **Linux:** NVIDIA (NVML) + AMD (sysfs) + Intel (i915 sysfs)
- **Windows:** NVIDIA (NVML) + DXGI + PDH
- **Android:** Adreno (kgsl sysfs) + Mali (mali0 sysfs)

---

## 🛠️ `knst_dev` Utility Infrastructure (NEW)

`knst_devices.hpp` provides **important helper infrastructure under the `knst_dev` namespace**. User code can use these directly.

### 1) `knst_dev::status` — New error enum

```cpp
namespace knst_dev {
enum class status : uint8_t {
    Ok = 0,
    NotFound,
    PermissionDenied,
    Busy,
    NotSupported,
    InvalidArgument,
    IoError,
    Timeout,
    ToolMissing,
    BadState,
    OutOfMemory,
    Unknown
};
inline const char* status_string(status s) noexcept;
}
```

**`status_string()` returns:**
- `Ok` → `"OK"`
- `NotFound` → `"Not found"`
- `PermissionDenied` → `"Permission denied"`
- `Busy` → `"Device busy"`
- `NotSupported` → `"Not supported on this platform/device"`
- `InvalidArgument` → `"Invalid argument"`
- `IoError` → `"I/O error"`
- `Timeout` → `"Timeout"`
- `ToolMissing` → `"Required tool missing"`
- `BadState` → `"Invalid state for this operation"`
- `OutOfMemory` → `"Out of memory"`
- `Unknown` → `"Unknown error"`

> ⚠️ **The old `knst_device_error` was REMOVED.** Use `knst_dev::status` in new code.

### 2) `knst_dev::result<T>` — Standard result type

```cpp
template<typename T>
struct result {
    T      value{};
    status st = status::Ok;
    int    native = 0;                 // errno / GetLastError / HRESULT / exit code

    bool ok() const noexcept;
    explicit operator bool() const noexcept;
    const char* error_string() const noexcept;

    static result success(T v) noexcept;
    static result fail(status s, int native_code = 0) noexcept;
};

// Void specialization:
template<> struct result<void> {
    status st;
    int    native;
    bool ok() const noexcept;
    static result success() noexcept;
    static result fail(status s, int native_code = 0) noexcept;
};
```

**`forward_error<To>(from)`** — forwards an error type.

### 3) `knst_dev::argv` — Argument builder

```cpp
struct argv {
    knst_vector<knst_c16string> v;

    argv& operator<<(const char*);
    argv& operator<<(const knst_c16string&);
    argv& operator<<(unsigned int);
    argv& operator<<(int);             // handles negative

    argv& push_back(const char*);
    argv& push_back(const std::string&);
    argv& push_back(const knst_c16string&);
};
```

**Usage:**
```cpp
knst_dev::argv a;
a << "-s" << "TERM" << "2.5" << "parec"
  << "--format=s16le" << "--rate=16000" << "--channels=1";

auto r = knst_dev::run("timeout", a, 5000);
```

**No shell injection** — argv passed directly to `execve`/`CreateProcessW`.

### 4) `knst_dev::run_tool` / `run` / `run_codes` / `run_tool_raw`

```cpp
// Simple run (exit 0 = success)
result<knst_byte_string> run_tool(const char* prog,
                                   const knst_vector<knst_c16string>& args,
                                   uint32_t timeout_ms = 5000) noexcept;

// With argv builder
result<knst_byte_string> run(const char* prog, const argv& a,
                             uint32_t timeout_ms = 5000) noexcept;

// Accept specific exit codes (coreutils `timeout` = 124 etc.)
result<knst_byte_string> run_codes(const char* prog, const argv& a,
                                    uint32_t timeout_ms,
                                    std::initializer_list<int> ok_codes) noexcept;

// Raw result (launched, exit_code, out)
struct tool_result { bool launched; int exit_code; knst_byte_string out; };
tool_result run_tool_raw(const char* prog, const argv& a,
                         uint32_t timeout_ms = 5000) noexcept;

// result<byte> → result<void>
result<void> drop(const result<knst_byte_string>& r) noexcept;
```

**Error behavior:**
- `knst_process::run_capture` fails → `ToolMissing`
- exit_code != 0 → `IoError` + native exit code
- Success → `success(out_data)`

### 5) Validators

```cpp
bool is_safe_token(const knst_c16string& s, uint32_t max_len = 127) noexcept;
// [a-zA-Z0-9_-@.:] + must not start with '-'

bool is_page_range(const knst_c16string& s) noexcept;
// "1-3,5,7" — only digits, ',' and '-'

bool is_mac(const knst_c16string& s) noexcept;
// "AA:BB:CC:DD:EE:FF"

bool is_pci_addr(const knst_c16string& s) noexcept;
// "0000:00:1f.3"

bool is_dev_path(const knst_c16string& s) noexcept;
// "/dev/sda1", "/dev/nvme0n1p2" — traversal guard

bool is_ipv4(const knst_c16string& s) noexcept;
bool is_ipv4_cidr(const knst_c16string& s) noexcept;
// "192.168.1.10/24"
```

**Usage:** Validate user input or external data before calling tools.

### 6) `knst_dev::format` / `text`

```cpp
knst_c16string format(const char* fmt, ...) noexcept;
// sprintf-like → knst_c16string

const char* text(const knst_byte_string& b) noexcept;
// byte string → const char* (NUL-terminated guarantee)
```

### 7) `knst_dev::unique_fd` — RAII fd (POSIX)

```cpp
class unique_fd {
public:
    unique_fd() noexcept;
    explicit unique_fd(int fd) noexcept;
    // Move-only
    int  get() const noexcept;
    bool valid() const noexcept;
    int  release() noexcept;
    void reset(int fd = -1) noexcept;
};
```

### 8) `knst_dev::sysfs_write` / `sysfs_read` (Linux)

```cpp
// /sys/ path guard + error mapping
bool is_sysfs_path(const char* p) noexcept;
// "/sys/" prefix + no ".."

result<void> sysfs_write(const char* path, const char* value) noexcept;
result<knst_c16string> sysfs_read(const char* path) noexcept;
```

**Security:** Path traversal (`..`) blocked. `O_CLOEXEC` used.

### 9) `knst_dev::uevent_monitor` (Linux)

```cpp
struct uevent {
    knst_c16string action, subsystem, devtype, devpath, devname;
};

class uevent_monitor {
public:
    static result<uevent_monitor> open() noexcept;
    result<uevent> wait(uint32_t timeout_ms,
                        const char* subsystem = nullptr) noexcept;
};
```

**Usage:** Watch USB/disk/input plug events.

```cpp
auto mon = knst_dev::uevent_monitor::open();
if (mon) {
    auto ev = mon.value.wait(5000, "usb");
    if (ev && ev.value.action == u"add") {
        std::cout << "USB added: " << ev.value.devname << "\n";
    }
}
```

**Security:** `nl_pid == 0` check — only **kernel** messages accepted (user-space spoofing blocked).

### 10) `knst_dev::map_errno` / `map_win_error`

```cpp
#if Linux/Android
status map_errno(int e) noexcept;
// ENOENT/ENODEV → NotFound, EACCES/EPERM → PermissionDenied, EBUSY → Busy, ...

#elif Windows
status map_win_error(unsigned long e) noexcept;
// 2/3/1801 → NotFound, 5 → PermissionDenied, 32/170 → Busy, ...
#endif
```

---

## 📊 JSON Bridge

If `KNST_JSON_HPP` is defined (`knst_json.hpp` included):

```cpp
namespace knst_devices {

// 6 categories: cpu, memory, system, os, storage (array), network (array)
knst_json full_inventory_json() noexcept;

// Atomic file write
bool save_inventory_json(const knst_c16string& path,
                         const knst_json_dump_options& opt = {}) noexcept;

}
```

**JSON schema:**
```json
{
  "cpu": {
    "vendor": "GenuineIntel",
    "model_name": "Intel(R) Core(TM) i7-9700K",
    "architecture": "x86_64",
    "physical_cores": 8,
    "logical_cores": 8,
    "socket_count": 1,
    "base_mhz": 3600,
    "max_mhz": 4900,
    "current_mhz": 4700,
    "cache_l3_kb": 12288,
    "has_avx2": true,
    "has_aes": true,
    "has_vmx": true
  },
  "memory": {
    "total_bytes": 34359738368,
    "used_bytes": 12884901888,
    "free_bytes": 8589934592,
    "available_bytes": 21474836480,
    "swap_total": 8589934592,
    "swap_used": 0
  },
  "system": {
    "manufacturer": "ASUSTeK COMPUTER INC.",
    "product_name": "PRIME B550M-A",
    "serial": "...",
    "is_laptop": false,
    "is_virtual_machine": false
  },
  "os": {
    "name": "Pop!_OS",
    "version": "22.04",
    "kernel": "6.8.0-...-generic",
    "arch": "x86_64",
    "hostname": "knst-desktop",
    "uptime_s": 14400
  },
  "storage": [
    {
      "device": "/dev/nvme0n1",
      "model": "Samsung SSD 970 EVO Plus 1TB",
      "total_bytes": 1000204886016,
      "is_removable": false,
      "is_system": true
    }
  ],
  "network": [
    {
      "name": "enp5s0",
      "mac": "aa:bb:cc:dd:ee:ff",
      "ipv4": "192.168.1.42",
      "ipv6": "fe80::...",
      "is_up": true,
      "wireless": false,
      "rx_bytes": 123456789,
      "tx_bytes": 98765432
    }
  ]
}
```

**Note:** JSON bridge **doesn't cover** all 18 categories — only 6. For other categories, use the direct API.

**Usage:**
```cpp
#if defined(KNST_JSON_HPP)
    knst_json inv = knst_devices::full_inventory_json();
    inv.save_file(u"/var/lib/myapp/inventory.json",
                  knst_json_dump_options::pretty(2),
                  /*atomic=*/true);
#endif
```

---

## 📱 Android Integration

On Android, a JNI env is required. Call **once** at `JNI_OnLoad` or `ANativeActivity_onCreate`:

```cpp
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(&env, JNI_VERSION_1_6) != JNI_OK) return -1;
    // ... find activity ...
    jobject activity = /* ... */;
    knst_devices::detail_android::set_java_vm(vm, activity);
    return JNI_VERSION_1_6;
}
```

**What `set_java_vm` does:**
1. Stores `g_vm` + `g_activity_obj`
2. Calls `KnstDevicesHelper.setContext(activity)` (for audio, wifi enrichment, file paths)
3. Calls `KnstBluetoothHelper.setContext(activity)`
4. Calls `knst_file::set_android_dirs(cache_dir, home_dir, ext_dir)` — from the Java helper

**Without JNI**, Android categories return `valid=true` but **empty** (except audio, power fast path). Categories that read from sysfs/proc (CPU, memory, network, drivers) still work without JNI.

**Java helper classes:**
- `com.knst.devices.KnstDevicesHelper` — audio, wifi, system serial, file paths
- `com.knst.devices.KnstBluetoothHelper` — bluetooth (optional)
- `com.knst.print.KnstPrintDiscoveryHelper` — printer mDNS (shared with knst_prn)

---

## ⚡ Performance Notes

### Lazy loading

| Operation | When? |
|---|---|
| **DMI cache** | First `motherboard()` or `system()` |
| **Bluetooth fast-path** | Skip if `/sys/class/bluetooth` empty |
| **CUPS fast-path** | Skip if no socket |
| **Audio fast-path** | Skip if no Pulse/PipeWire socket |
| **CPU usage** | Cached 200 ms (Linux 20 ms `nanosleep` + two `/proc/stat`) |

### CPU usage — 20 ms cost

Linux `cpu()` on the **first** call performs 20 ms `nanosleep` (between two `/proc/stat` reads). Repeated calls within **200 ms** use the cache. Call once at app start, cache the result.

### External tools (Linux)

| Tool | Purpose | Required? |
|---|---|---|
| `dmidecode` | DMI (root) | Optional (sysfs fallback) |
| `lspci` | PCI names | Optional |
| `iw` | WiFi SSID/signal | Optional |
| `pactl` | Audio devices | Optional |
| `bluetoothctl` | Bluetooth | Optional |
| `lpstat` | Printers | Optional |
| `nvidia-smi` | NVIDIA GPU | Optional |

Missing tools **do not error**, only their fields stay empty. Fast-path checks (socket, dir) skip tool invocation.

### Windows notes

- **COM:** `audio()` calls `CoInitializeEx(COINIT_MULTITHREADED)`. `RPC_E_CHANGED_MODE` gracefully handled.
- **Session 0:** Running as a service, `EnumDisplayDevices` can return empty — normal.
- **WMI `root\WMI`:** Sensors use a separate namespace — separate query mechanism.
- **PowerShell:** WMI queries via `powershell.exe` `Get-CimInstance`. UTF-8 forced.

### Windows WMI query output

Every WMI query is parsed via `ConvertTo-Csv -NoTypeInformation -Delimiter ','`. UTF-16LE → UTF-8 normalized.

### Android notes

- **sysfs SELinux:** Paths like `/sys/class/net/*/wireless` are restricted. Name-based type inference kicks in.
- **JNI Attach:** `get_env()` tries `AttachCurrentThread` on every call (idempotent).
- **KnstDevicesHelper classloader:** `FindClass` doesn't see app classloader. Uses `activity.getClassLoader().loadClass()` (`find_app_class`).

---

## 📊 Performance Summary

| Operation | Linux | Windows | Android |
|---|---|---|---|
| `cpu()` | 20-50 ms (first), ~0 ms (cache) | 100-300 ms (PowerShell WMI) | 20-50 ms |
| `memory()` | 1-2 ms | 100-300 ms | 1-2 ms |
| `storage()` | 20-100 ms (if LVM) | 100-300 ms | 10-50 ms (JNI) |
| `network()` | 5-20 ms | 20-100 ms | 10-30 ms (JNI) |
| `usb()` | 5-20 ms | 100-300 ms | 50-100 ms (JNI) |
| `pci()` | 20-100 ms (lspci) | 100-300 ms | 20-100 ms |
| `monitors()` | 10-50 ms (EDID) | 50-200 ms | ~0 ms |
| `input()` | 10-50 ms | 20-100 ms | 10-50 ms |
| `audio()` | 100-500 ms (pactl) | 50-200 ms (COM) | 50-200 ms (JNI) |
| `power()` | 5-20 ms | 1-5 ms | 20-100 ms |
| `sensors()` | 5-20 ms | 100-300 ms | 5-20 ms |
| `bluetooth()` | 100-1500 ms (bluetoothctl) | 50-200 ms | 20-100 ms |
| `cameras()` | 10-50 ms | 100-300 ms | 20-100 ms |
| `printers()` | 50-200 ms (lpstat) | 100-300 ms | 500-4000 ms (mDNS) |
| `system()` | 0-5 ms (cache) | 100-300 ms | 0-5 ms (props) |
| `os()` | 1-5 ms | 10-50 ms | 1-5 ms |
| `drivers()` | 5-20 ms | 100-300 ms | 5-20 ms |

**Note:** Most Windows latency is **PowerShell startup** (100-200 ms per WMI query). Cache results or use a single batched `Get-CimInstance` call instead of multiple.

---

## ✅ Do / ❌ Don't

### ✅ Do

```cpp
// 1. Check valid
knst_cpu_info c = knst_devices::cpu();
if (!c.valid) return;

// 2. Check partial-data flags
if (c.has_temperature)
    std::cout << c.temperature_c << " °C\n";

// 3. Cache results
static const auto cached_cpu = knst_devices::cpu();

// 4. Filter loopback / down interfaces
for (const auto& n : knst_devices::network()) {
    if (n.is_loopback || !n.is_up) continue;
    // ...
}

// 5. Use validators for user input
if (!knst_dev::is_dev_path(user_input)) return;
auto r = knst_dev::sysfs_read(knst_dev::text(knst_dev::to_utf8(user_input)));

// 6. New status enum
knst_dev::status st = knst_dev::map_errno(EACCES);
if (st == knst_dev::status::PermissionDenied) { /* ... */ }

// 7. Initialize JNI on Android
#if defined(KNST_USING_PLATFORM_ANDROID)
knst_devices::detail_android::set_java_vm(vm, activity);
#endif

// 8. JSON inventory
#if defined(KNST_JSON_HPP)
knst_devices::save_inventory_json(u"/var/inventory.json");
#endif
```

### ❌ Don't

```cpp
// 1. Repeatedly query CPU (20 ms sleep every 200 ms)
for (int i = 0; i < 10000; ++i) {
    auto c = knst_devices::cpu();   // ❌ 20 ms sleep every 200 ms
}

// 2. Skip valid check
std::cout << c.model_name;   // ❌ c.valid=false possible

// 3. Expect the old error enum
// ❌ knst_device_error no longer exists
// ✅ use knst_dev::status

// 4. Call all categories at app start
auto cpu = knst_devices::cpu();
auto usb = knst_devices::usb();      // ❌ PowerShell slow
auto pci = knst_devices::pci();      // ❌
auto bt = knst_devices::bluetooth(); // ❌

// 5. Skip JNI init on Android
// ❌ audio/wifi/system.serial return empty
// ✅ call set_java_vm in JNI_OnLoad

// 6. Run tools with user input
knst_dev::argv a;
a << "--option" << user_input;   // ❌
// ✅ check knst_dev::is_safe_token(user_input) first

// 7. Bypass sysfs path guard
knst_dev::sysfs_read("/sys/class/../../etc/passwd");   // ❌ is_sysfs_path false
```

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
        if (c.has_temperature)
            std::cout << "  " << c.temperature_c << " °C\n";
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

    // JSON inventory
#if defined(KNST_JSON_HPP)
    knst_devices::save_inventory_json(u"/tmp/inventory.json");
    std::cout << "\nInventory written to /tmp/inventory.json\n";
#endif

    return 0;
}
```

---

## 📌 API Contract Summary

```cpp
namespace knst_devices {

// ─── 18 categories + GPU bridge ───
knst_cpu_info                  cpu()          noexcept;
knst_motherboard_info          motherboard()  noexcept;
knst_memory_info               memory()       noexcept;
knst_vector<knst_storage_info> storage()      noexcept;
knst_vector<knst_network_info> network()      noexcept;
knst_vector<knst_usb_info>     usb()          noexcept;
knst_vector<knst_pci_info>     pci()          noexcept;
knst_vector<knst_monitor_info> monitors()     noexcept;
knst_vector<knst_input_info>   input()        noexcept;
knst_audio_info                audio()        noexcept;
knst_power_info                power()        noexcept;
knst_vector<knst_sensor_info>  sensors()      noexcept;
knst_bluetooth_info            bluetooth()    noexcept;
knst_vector<knst_camera_info>  cameras()      noexcept;
knst_vector<knst_printer_info> printers()     noexcept;
knst_system_info               system()       noexcept;
knst_os_info                   os()           noexcept;
knst_vector<knst_driver_info>  drivers()      noexcept;

// GPU bridge (from knst_gpu)
knst_vector<knst_gpu_info>    gpus()                     noexcept;
knst_vector<knst_gpu_process> gpu_processes()            noexcept;
knst_vector<knst_gpu_process> gpu_processes(uint32_t)    noexcept;
uint32_t                      gpu_count()                noexcept;
bool                          has_gpu()                  noexcept;
knst_c16string                nvidia_driver_version()    noexcept;
knst_c16string                cuda_version()             noexcept;

// JSON bridge (KNST_JSON_HPP)
#if defined(KNST_JSON_HPP)
knst_json full_inventory_json() noexcept;
bool      save_inventory_json(const knst_c16string& path,
                              const knst_json_dump_options& = {}) noexcept;
#endif

// Android setup
namespace detail_android {
    void set_java_vm(JavaVM* vm, jobject activity);
}

} // namespace knst_devices


namespace knst_dev {

// ─── Status / result ───
enum class status : uint8_t { Ok, NotFound, PermissionDenied, Busy,
    NotSupported, InvalidArgument, IoError, Timeout, ToolMissing,
    BadState, OutOfMemory, Unknown };
inline const char* status_string(status s) noexcept;

template<typename T> struct result { T value; status st; int native; ... };
template<> struct result<void> { status st; int native; ... };
template<typename To, typename From>
result<To> forward_error(const result<From>& r) noexcept;

// ─── Error mapping ───
#if Linux/Android
status map_errno(int e) noexcept;
class unique_fd { ... };
bool write_all(int fd, const void* data, size_t len) noexcept;
#elif Windows
status map_win_error(unsigned long e) noexcept;
struct uevent { knst_c16string action, subsystem, devtype, devpath, devname; };
#endif

// ─── String helpers ───
knst_byte_string to_utf8(const knst_c16string& s) noexcept;
bool is_safe_token(const knst_c16string& s, uint32_t max_len = 127) noexcept;
bool is_page_range(const knst_c16string& s) noexcept;
knst_c16string u32_to_string(uint32_t v) noexcept;

// ─── Tool runners ───
result<knst_byte_string> run_tool(const char* prog,
                                   const knst_vector<knst_c16string>& args,
                                   uint32_t timeout_ms = 5000) noexcept;
struct argv { ... };
knst_c16string format(const char* fmt, ...) noexcept;
const char* text(const knst_byte_string& b) noexcept;
struct tool_result { bool launched; int exit_code; knst_byte_string out; };
tool_result run_tool_raw(const char* prog, const argv& a, uint32_t = 5000) noexcept;
result<knst_byte_string> run(const char* prog, const argv& a, uint32_t = 5000) noexcept;
result<knst_byte_string> run_codes(const char* prog, const argv& a, uint32_t,
                                    std::initializer_list<int> ok_codes) noexcept;
result<void> drop(const result<knst_byte_string>& r) noexcept;

// ─── Validators ───
bool is_hex(char16_t c) noexcept;
bool is_mac(const knst_c16string& s) noexcept;
bool is_pci_addr(const knst_c16string& s) noexcept;
bool is_dev_path(const knst_c16string& s) noexcept;
bool is_ipv4_cidr(const knst_c16string& s) noexcept;
bool is_ipv4(const knst_c16string& s) noexcept;

// ─── Linux sysfs + uevent ───
#if Linux
bool is_sysfs_path(const char* p) noexcept;
result<void> sysfs_write(const char* path, const char* value) noexcept;
result<knst_c16string> sysfs_read(const char* path) noexcept;

struct uevent { ... };
class uevent_monitor {
    static result<uevent_monitor> open() noexcept;
    result<uevent> wait(uint32_t timeout_ms, const char* = nullptr) noexcept;
};
#endif

} // namespace knst_dev
```

---

## 🎯 What It Brings You

`knst_devices` gives you serious advantages in these situations:

- ✅ **System diagnostics** — 18 categories in one API
- ✅ **Game engines / adaptive quality** — settings based on system profile
- ✅ **Benchmark / telemetry** — hardware info + results
- ✅ **Compatibility checks** — fast query like "AVX2 available?"
- ✅ **Battery management** — behavior based on battery state
- ✅ **Inventory software** — full hardware reported as JSON
- ✅ **Cross-platform** — Linux + Windows + **Android**
- ✅ **No shell injection** — argv builder + validators
- ✅ **Kernel hotplug** — uevent_monitor for USB/disk event tracking
- ✅ **`knst_gpu` integration** — GPU queries ready
- ✅ **`knst_json` integration** — atomic file save
- ✅ **`knst_file` integration** — path handling, Android dir setup
- ✅ **`knst_process` integration** — tool runner infrastructure

---