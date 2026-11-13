# knst_devices — Donanım Tarama Kütüphanesi

Selamlar! 👋 Bu doküman `knst_devices` kütüphanesinin ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **18 kategori + 1 GPU köprüsü altında tüm sistem donanımını (CPU, RAM, disk, ağ, USB, PCI, monitör, sensör, kamera, yazıcı, batarya, Bluetooth...) standart bilgi yapılarına okuyan bir donanım envanteri kütüphanesi.** Linux'ta `/proc`, `/sys` ve harici araçlarla, Windows'ta WMI + native API'lerle, Android'de JNI + NDK API'leriyle çalışır.

> ⚠️ **BREAKING CHANGE (v2):** Eski `knst_device_error` enum'ı **kaldırıldı**. Yerine `knst_dev::status` + `status_string()` geldi. Kullanıcı kodu güncellenmeli.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **18 kategori + GPU köprüsü** | CPU'dan yazıcıya kadar her şey tek namespace altında |
| **3 platform** | Linux + Windows + **Android** (yeni) |
| **Standart yapılar** | Her sorgu `valid` bayrağı olan bir `struct` döner |
| **Kısmi veri bayrakları** | `has_cores`, `has_temperature`, ... — eksik veri ayırt edilir |
| **Lazy yükleme** | DMI cache, `pactl`/`bluetoothctl` fast-path kontrolleri |
| **UTF-16 string** | Tüm metin alanları `knst_c16string` — Windows uyumlu |
| **Vector dönüşler** | USB, PCI, storage, network gibi kategoriler `knst_vector<T>` döner |
| **`knst_dev::status`** | Yeni hata modeli — `result<T>` + `status_string()` |
| **`knst_dev::argv`** | Shell injection yok — argv vektörü |
| **`knst_dev::run_tool`** | Güvenli araç çalıştırma (`ToolMissing`, exit code handling) |
| **`knst_dev::sysfs_write/read`** | `/sys/` path guard + error mapping |
| **`knst_dev::uevent_monitor`** | Kernel hotplug (Linux) — USB/disk/input takıp çıkarma |
| **`knst_dev::unique_fd`** | RAII fd wrapper (POSIX) |
| **Chassis type normalize** | DMI numeric kod → "Laptop"/"Tower"/"Tablet" |
| **JSON bridge** | `full_inventory_json()`, `save_inventory_json()` |
| **Android tam destek** | JNI + NDK ASensorManager + ACameraManager + UsbManager |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // CPU bilgisi
    knst_cpu_info cpu = knst_devices::cpu();
    if (cpu.valid) {
        std::cout << "CPU: " << cpu.model_name << "\n";
        std::cout << "Çekirdek: " << cpu.physical_cores  << " fiziksel / "
                  << cpu.logical_cores << " mantıksal\n";
        std::cout << "Maks frekans: " << cpu.max_mhz << " MHz\n";
        std::cout << "Stepping: 0x" << std::hex << cpu.stepping
                  << "  Microcode: 0x" << cpu.microcode_ver << std::dec << "\n";
    }

    // RAM bilgisi
    knst_memory_info ram = knst_devices::memory();
    if (ram.valid) {
        std::cout << "Toplam RAM: " << ram.total_bytes / (1024 * 1024 * 1024) << " GB\n";
    }

    // Tüm diskleri listele
    auto disks = knst_devices::storage();
    for (const auto& d : disks) {
        std::cout << "Disk: " << d.device << " (" << d.model << ")\n";
    }

    // JSON envanter
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

## 📋 18 Kategori + GPU Köprüsü

| # | Kategori | Fonksiyon | Dönüş Tipi | Linux | Windows | Android |
|---|---|---|---|---|---|---|
| 1 | **CPU** | `cpu()` | `knst_cpu_info` | ✅ | ✅ | ✅ |
| 2 | **Anakart + BIOS** | `motherboard()` | `knst_motherboard_info` | ✅ | ✅ | ✅ (props) |
| 3 | **Bellek** | `memory()` | `knst_memory_info` | ✅ | ✅ | ✅ |
| 4 | **Depolama** | `storage()` | `knst_vector<knst_storage_info>` | ✅ | ✅ | ✅ (JNI) |
| 5 | **Ağ** | `network()` | `knst_vector<knst_network_info>` | ✅ | ✅ | ✅ + WiFi zenginleştirme |
| 6 | **USB** | `usb()` | `knst_vector<knst_usb_info>` | ✅ | ✅ | ✅ (JNI UsbManager) |
| 7 | **PCI** | `pci()` | `knst_vector<knst_pci_info>` | ✅ | ✅ | ⚠️ sysfs kısıtlı |
| 8 | **Monitörler** | `monitors()` | `knst_vector<knst_monitor_info>` | ✅ | ✅ | ⚠️ çoğunlukla boş |
| 9 | **Giriş cihazları** | `input()` | `knst_vector<knst_input_info>` | ✅ | ✅ | ✅ |
| 10 | **Ses** | `audio()` | `knst_audio_info` | ✅ | ✅ | ✅ (JNI AudioManager) |
| 11 | **Güç/Batarya** | `power()` | `knst_power_info` | ✅ | ✅ | ✅ (sysfs + JNI) |
| 12 | **Sensörler** | `sensors()` | `knst_vector<knst_sensor_info>` | ✅ | ✅ | ✅ (NDK ASensorManager) |
| 13 | **Bluetooth** | `bluetooth()` | `knst_bluetooth_info` | ✅ | ✅ | ✅ (JNI Adapter) |
| 14 | **Kameralar** | `cameras()` | `knst_vector<knst_camera_info>` | ✅ | ✅ | ✅ (NDK ACameraManager) |
| 15 | **Yazıcılar** | `printers()` | `knst_vector<knst_printer_info>` | ✅ | ✅ | ✅ (mDNS helper) |
| 16 | **Sistem** | `system()` | `knst_system_info` | ✅ | ✅ | ✅ (props) |
| 17 | **İşletim Sistemi** | `os()` | `knst_os_info` | ✅ | ✅ | ✅ |
| 18 | **Sürücüler** | `drivers()` | `knst_vector<knst_driver_info>` | ✅ | ✅ | ✅ (/proc/modules) |
| ➕ | **GPU köprüsü** | `gpus()` | `knst_vector<knst_gpu_info>` | ✅ | ✅ | ✅ |

**Sembol açıklaması:** ✅ = tam destek · ⚠️ = kısmi/koşullu · ❌ = yok

---

## 🖥️ CPU — `knst_cpu_info`

```cpp
knst_cpu_info c = knst_devices::cpu();

// Temel bilgiler
c.vendor;              // "GenuineIntel" / "AuthenticAMD" / "ARM"
c.model_name;          // "Intel(R) Core(TM) i7-9700K"
c.architecture;        // "x86_64" / "aarch64"
c.physical_cores;      // 8
c.logical_cores;       // 8 (HT kapalıysa eşit)
c.socket_count;        // 1
c.base_mhz;            // 3600
c.max_mhz;             // 4900
c.current_mhz;         // anlık frekans
c.cache_l1_kb;         // KB cinsinden
c.cache_l2_kb;
c.cache_l3_kb;
c.temperature_c;       // °C (coretemp/k10temp/zenpower)
c.usage_percent;       // anlık kullanım %
c.load_avg_1;          // /proc/loadavg (Linux)
c.load_avg_5;
c.load_avg_15;
c.stepping;            // CPU stepping (0x0A gibi)
c.microcode_ver;       // microcode revision (0xEA gibi)

// CPU özellikleri
c.has_sse4;            // SSE4.2
c.has_avx;             // AVX
c.has_avx2;            // AVX2
c.has_avx512;          // AVX-512
c.has_aes;             // AES-NI
c.has_vmx;             // Intel VT-x / AMD-V
c.has_iommu;           // IOMMU
c.has_hyperthreading;  // HT / SMT

// Bayraklar
c.valid;               // Genel geçerlilik
c.has_cores;           // Çekirdek bilgisi okundu mu?
c.has_frequency;       // Frekans bilgisi var mı?
c.has_cache;           // Cache bilgisi var mı?
c.has_temperature;     // Sıcaklık okunabildi mi?
c.has_usage;           // Kullanım yüzdesi ölçülebildi mi?
```

### CPU kullanımı ölçümü — 200 ms cache

`cpu()` çağrısı Linux'ta `/proc/stat` iki kez okur (20 ms aralıkla) — CPU kullanım yüzdesi için. Bu **200 ms boyunca cached**'lenir. Aynı 200 ms içinde tekrar çağırırsan disk I/O yok.

### Kısmi veri

```cpp
knst_cpu_info c = knst_devices::cpu();
if (!c.valid) return;

std::cout << c.model_name << "\n";
if (c.has_temperature)
    std::cout << "Sıcaklık: " << c.temperature_c << " °C\n";
if (c.has_cache)
    std::cout << "L3: " << c.cache_l3_kb << " KB\n";
```

---

## 🔧 Anakart + BIOS — `knst_motherboard_info`

```cpp
knst_motherboard_info m = knst_devices::motherboard();

// Anakart
m.manufacturer;        // "ASUSTeK COMPUTER INC."
m.product;             // "PRIME B550M-A"
m.version;             // "Rev X.0x"
m.serial;              // Seri numarası
m.uuid;                // Sistem UUID

// BIOS
m.bios_vendor;         // "American Megatrends Inc."
m.bios_version;        // "3202"
m.bios_date;           // "08/13/2024"

// Boot / güvenlik
m.uefi_boot;           // UEFI mi?
m.secure_boot;         // Secure Boot açık mı?
m.has_tpm;             // TPM 2.0 var mı?
m.tpm_version;         // "2.0"

// Kasa
m.chassis_type;        // "Desktop" / "Laptop" / "Tower" / "Tablet"
m.chassis_serial;

// Bayraklar
m.valid;
m.has_bios;
m.has_board;
m.has_chassis;
```

### Chassis type normalizasyonu (yeni)

DMI chassis type numeric kod döner (`9` = Laptop). Kütüphane bunu `chassis_type_name()` ile okunabilir hale getirir:

```cpp
// internal:
knst_devices::detail::chassis_type_name(9);   // "Laptop"
knst_devices::detail::chassis_type_name(3);   // "Desktop"
knst_devices::detail::chassis_type_name(30);  // "Tablet"

// kullanıcı tarafında otomatik yapılır:
knst_motherboard_info m = knst_devices::motherboard();
// m.chassis_type zaten "Laptop" (numeric değil)
```

**Desteklenen kodlar (DMTF SMBIOS):** 1-36 arası chassis type.

### DMI cache

İlk `motherboard()` veya `system()` çağrısında DMI bilgisi okunur (`dmidecode -q` → fail ise `/sys/class/dmi/id/*` fallback). **`std::call_once`** ile sadece bir kez.

---

## 💾 Bellek — `knst_memory_info`

```cpp
knst_memory_info m = knst_devices::memory();

// Sistem belleği
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

// Slot bilgisi
m.slot_count;          // Toplam slot
m.slots_used;          // Kullanılan slot

// Bellek modülleri
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

// Bayraklar
m.valid;
m.has_modules;
m.has_slot_count;
```

**Linux:** `/proc/meminfo` + `dmidecode -t memory` (root gerekir; yoksa `modules` boş kalır).

**Windows:** `GlobalMemoryStatusEx` + WMI `Win32_PhysicalMemory`. MemoryType kodu (20=DDR, 24=DDR3, 26=DDR4, 34=DDR5, ...) `mod.type`'a çevrilir.

---

## 💽 Depolama — `knst_vector<knst_storage_info>`

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
    s.temperature_c;       // NVMe için
    s.is_removable;
    s.is_system_disk;
    s.is_solid_state;
    s.rotational_rpm;      // HDD için 5400/7200

    // SMART (varsa)
    s.has_smart;
    s.smart_passed;
    s.smart_power_on_hours;
    s.smart_bytes_written;
    s.smart_reallocated_sectors;
    s.smart_pending_sectors;
    s.smart_percent_used;

    // Bölümler
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

### LVM/LUKS desteği (Linux)

`/dev/mapper/*` ve `/dev/dm-*` aygıtları **otomatik olarak fiziksel disklerine çözülür**. Pop!_OS, Ubuntu, Fedora, RHEL gibi LUKS+LVM kullanan sistemlerde doğru çalışır.

```cpp
// /dev/mapper/cryptroot → /dev/nvme0n1p3 → /dev/nvme0n1
// Kütüphane bu zincirin en üstündeki /dev/nvme0n1'i bulur,
// tüm partition'ları oraya iliştirir.
```

Recursive çözümleme max 8 seviye derinlik. `dm-*` slave içinde `dm-*` olabilir (nested LVM).

### Disk tipi override (Linux)

Rotational heuristic yetersizdir. Kütüphane **isim prefix'ine** göre override eder:

| Prefix | Type |
|---|---|
| `sr*` | Optical (is_removable=true) |
| `md*` | RAID |
| `fd*` | Floppy (is_removable=true) |
| `mmcblk*` | SD_Card (removable) veya eMMC |
| `nvme*` | NVMe |
| Removable `sd*` | USB |

### Windows SSD/HDD tespiti

`MSFT_PhysicalDisk` (`root\Microsoft\Windows\Storage`) → `MediaType`:
- `3` = HDD
- `4` = SSD
- `BusType = 17` = NVMe
- `BusType = 7` = USB

**System disk tespiti:** `Win32_DiskPartition` → `BootPartition=true` olan hangi `DiskIndex`'te → `\\.\PHYSICALDRIVE0` eşleşirse `is_system_disk = true`.

---

## 🌐 Ağ — `knst_vector<knst_network_info>`

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

`iw dev <iface> link` üzerinden:
- SSID
- freq (2407-2484 → 2.4 GHz, 5000+ → 5 GHz)
- signal (dBm)
- channel (freq'ten hesaplanır)

### Windows WiFi

**WLAN API** (`WlanOpenHandle` + `WlanEnumInterfaces` + `WlanQueryInterface`) — **dil-bağımsız**, `netsh` gibi lokalize çıktı yok:
- SSID (binary → string)
- signal quality % → dBm dönüşümü (`q/2 - 100`)
- channel (opcode query)
- security (auth algorithm → "WPA2-PSK" vb.)
- frequency (channel'dan hesaplanır)

### Android WiFi zenginleştirme

Sysfs SELinux tarafından kısıtlı. Kütüphane **JNI helper** üzerinden `WifiManager.getConnectionInfo()`'dan SSID/BSSID/RSSI/link speed/frequency alır ve ismi `wlan*`/`p2p*` olan arayüze uygular.

Ayrıca sysfs okuyamazsa **isim bazlı tip tahmini**:
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

**Linux:** `/sys/bus/usb/devices/*` — interface node'ları (`1-1:1.0`) atlanır, sadece device'lar alınır.

**Windows:** WMI `Win32_PnPEntity WHERE DeviceID LIKE 'USB%'` + `Service` sütunundan class tahmini:
- `usbhub` / `USBHUB` → Hub
- `usbhid` / `HidUsb` / `kbdhid` / `mouhid` → HID
- `usbvideo` / `uvcvideo` → Video
- `usbaudio` / `snd-usb-audio` → Audio
- `USBSTOR` → MassStorage
- `usbprint` → Printer
- `usbser` → CDC

`usbccgp` (composite parent) ise product_name'den tahmin.

**Android:** JNI `UsbManager.getDeviceList()` — OTG cihazları listelenir.

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

**Linux PCIe speed/width:** `max_link_speed` / `max_link_width` tercih edilir (`current_*` **yanıltıcıdır** — ASPM idle'da Gen1'e düşürür).

**Linux device names:** `lspci -vmm -nn` parse edilir. Multi-function slot'lar, domain'ler, uzun isimler doğru parse edilir.

**Windows:** WMI `Win32_PnPEntity WHERE DeviceID LIKE 'PCI%'` — `PNPClass`'tan class tahmini (Display, Net, MEDIA, SCSIAdapter, HDC, USB, System, MEMORY, BLUETOOTH, HIDClass, SecurityDevices).

**Not:** Windows'ta PCIe gen/width bilgisi WMI'da yok. Linux'ta sysfs'ten okunur.

---

## 🖼️ Monitörler — `knst_vector<knst_monitor_info>`

```cpp
for (const auto& m : knst_devices::monitors()) {
    m.index;
    m.name;
    m.manufacturer;     // EDID'den: "DEL", "GSM"
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

`/sys/class/drm/card*-*/edid` binary okunur:
- Header kontrolü (`00 FF FF FF`)
- Manufacturer ID (3× 5-bit)
- Product code, serial, week, year
- Physical size (edid[21] × 10 mm, edid[22] × 10 mm)
- Detailed Timing Descriptor (edid[54-61]): pixel clock, h/v active/blank → refresh
- Gamma (edid[23])

### Windows EDID + WMI

`EnumDisplayDevicesW` ile monitör device path alınır. EDID registry'den okunur:
```
HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\<VENDOR>\<INSTANCE>\Device Parameters\EDID
```

`WmiMonitorID` (`root\WMI`) → friendly name, manufacturer name, serial, year, week. `WmiMonitorBrightness` → parlaklık.

### Connection type tespiti (Windows)

Win32'de doğrudan API yok. Adapter ve monitor `DeviceString`'inde keyword aranır:
- `HDMI` → HDMI
- `DisplayPort` / `DP` → DisplayPort
- `DVI` → DVI
- `VGA` → VGA
- `USB-C` / `USB Type-C` → USB_C
- `eDP` → eDP
- `LVDS` → LVDS

---

## ⌨️ Giriş Cihazları — `knst_vector<knst_input_info>`

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

### Linux sınıflandırma (iki aşamalı)

**1. İsim bazlı (öncelikli):**
- `keyboard`, `keys` → Keyboard
- `mouse`, `trackball`, `trackpoint` → Mouse
- `touchpad`, `trackpad`, `synaptics`, `elan` → Touchpad
- `touchscreen`, `touch screen` → Touchscreen
- `gamepad`, `joystick`, `controller`, `xbox`, `playstation`, `dualshock`, `dualsense`, `8bitdo` → Gamepad
- `logitech k*` → Keyboard (HID descriptor'sız)
- `logitech m*`, `logitech mx*` → Mouse
- `wmi hotkeys`, `hotkey`, `acpi` → Other

**2. Capability bazlı (fallback):**
- `capabilities/key` + `rel` yok + `abs` yok → Keyboard
- `key` + `abs` (≤2 bit) → Keyboard (multimedia)
- `rel` + `abs` yok → Mouse
- `abs` (≤4 bit) → Touchpad
- `key` + `abs` (>4 bit) → Gamepad
- `abs` (rel yok) → Touchpad / Gamepad

**Filtreler:** Audio jack'ları (`hd-audio`, `rear mic`, `line out`), sanal event'ler (`power button`, `sleep button`, `video bus`, `lid switch`, `pc speaker`) filtrelenir.

### Windows Raw Input

`GetRawInputDeviceList` + `GetRawInputDeviceInfoW` (RIDI_DEVICENAME + RIDI_DEVICEINFO):
- `RIM_TYPEMOUSE` → Mouse
- `RIM_TYPEKEYBOARD` → Keyboard
- `RIM_TYPEHID` → HID usage page analizi:
  - `0x01` (Generic Desktop) + usage:
    - `0x02` → Mouse
    - `0x04` / `0x05` / `0x07` / `0x08` → Gamepad
    - `0x06` → Keyboard
    - `0x0D` → Tablet
  - `0x0D` (Digitizer) → Touchscreen
  - `0x0C` (Consumer Control) → Remote

Ürün adı `HidD_GetProductString` ile alınır.

**VID/PID:** `RIDI_DEVICEINFO` sağlarsa oradan, yoksa device_path'ten `VID_`/`PID_` regex.

**Filtreler:** `ROOT\` device'lar (RDP mouse/keyboard) atlanır. VID/PID+type+name duplicate'leri atlanır.

---

## 🔊 Ses — `knst_audio_info`

```cpp
knst_audio_info a = knst_devices::audio();

a.default_sink;         // Varsayılan çıkış (description veya name)
a.default_source;       // Varsayılan giriş

for (const auto& d : a.devices) {
    d.index;
    d.name;             // "alsa_output.pci-0000_00_1f.3.analog-stereo"
    d.description;      // "Built-in Audio Analog Stereo"
    d.is_input;         // Mikrofon mu?
    d.is_output;        // Hoparlör mü?
    d.is_default;
    d.volume_percent;
    d.muted;
    d.sample_rate;
    d.channels;
    d.valid;
}

a.valid;
```

### Linux ses fast-path

Socket kontrolü — varsa devam, yoksa **hızlı dönüş**:
- `/run/user/<uid>/pulse/native` (PulseAudio)
- `/run/user/<uid>/pipewire-0` (PipeWire)

İkisi de yoksa `pactl` hiç çağrılmaz → uygulama başlangıcı hızlı.

Parse: `pactl list sinks` ve `pactl list sources` — device başlangıcı (`Sink #` / `Source #`) ile index, `Name:` ve `Description:` satırları. Default işaretleme `pactl get-default-sink` / `get-default-source` ile.

### Windows ses

`IMMDeviceEnumerator` (COM):
- `CoInitializeEx(COINIT_MULTITHREADED)`
- `EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, ...)` → output
- `EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, ...)` → input
- `PKEY_Device_FriendlyName` property → description
- `GetDefaultAudioEndpoint` → default işaretleme

`RPC_E_CHANGED_MODE` (başka thread önceden COM init etmiş) tolere edilir.

### Android ses

JNI `AudioManager.getDevices(GET_DEVICES_ALL)` → `KnstDevicesHelper.getAudioDevicesInfo()`:
- Type (AudioDeviceInfo.TYPE_*) → isim eşleşmesi (EARPIECE, SPEAKER, WIRED_HEADSET, ...)
- Product name (`Build.MODEL` ile karşılaştırılır — built-in ise `(built-in)`)
- isSink → is_output
- isSource → is_input

---

## 🔋 Güç — `knst_power_info`

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

`/sys/class/power_supply/*/type` = `Battery` olan cihazlar listelenir. **Ancak** `scope` dosyası varsa ve `System` değilse (wireless mouse/keyboard battery) **atlanır**.

### AC propagation

`readdir` sırası garanti değil — AC adapter BAT0'tan sonra okunabilir. Kütüphane **önce tüm battery'leri toplar**, sonra `ac_seen && ac_on` durumunu tüm battery'lere propagate eder.

### Windows güç

`GetSystemPowerStatus` — `BatteryFlag & 128` = no battery. `ACLineStatus` = AC. `BatteryLifePercent`, `BatteryLifeTime`, `BatteryFlag & 0x0F` → state.

### Android güç (3 path)

**1. sysfs fallback** (hızlı, çoğu cihazda çalışır):
- `/sys/class/power_supply/battery/capacity`
- `/sys/class/power_supply/battery/status`
- `/sys/class/power_supply/battery/technology`
- `/sys/class/power_supply/battery/charge_full`

**2. ACTION_BATTERY_CHANGED sticky broadcast** (state + plugged için güvenilir):
- `registerReceiver(null, IntentFilter(ACTION_BATTERY_CHANGED))`
- `getIntExtra("status")`, `getIntExtra("plugged")`, `getIntExtra("level")`

**3. BatteryManager** (son çare):
- `getIntProperty(BATTERY_PROPERTY_CAPACITY)` = 4

---

## 🌡️ Sensörler — `knst_vector<knst_sensor_info>`

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

### Sensör tipleri (enum güncellendi)

```cpp
enum class knst_sensor_type : uint8_t {
    Unknown = 0, Temperature, Fan, Voltage, Current, Power, Humidity, Proximity, Light,
    // Android / mobil:
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
- `*_label` → okunabilir isim (yoksa dosya adı)

### Windows WMI

`root\WMI`:
- **`MSAcpi_ThermalZoneTemperature`** — `CurrentTemperature` tenths-Kelvin → °C
- **`MSAcpi_ThermalZoneInformation`** — **fallback** (birincil boşsa). AMD Ryzen bazı BIOS'larda birinci class boş döner.

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

**Not:** `ASensor*` API pasif listeleme sağlar. Gerçek zamanlı okuma için `ASensorEventQueue` gerekir (bu kütüphanede yok — sadece cihaz listesi).

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

`/sys/class/bluetooth/` boşsa veya yoksa `bluetoothctl` **hiç çağrılmaz** — D-Bus timeout'unu beklemez.

### Windows

`BluetoothFindFirstRadio` + `BluetoothGetRadioInfo` (adapter). `BluetoothFindFirstDevice` + `BluetoothFindNextDevice` (cihazlar).

**Not:** `powered` alanı Windows API'de yok — her zaman `true` döner (adapter varsa). `discoverable` de aynı şekilde.

---

## 📷 Kameralar — `knst_vector<knst_camera_info>`

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

`/dev/video*` cihazları `VIDIOC_QUERYCAP` ile kontrol edilir. **Sadece `V4L2_CAP_VIDEO_CAPTURE`** flag'li cihazlar kamera sayılır. UVC webcam'lerin metadata node'ları (`videoN+1`) atlanır.

### Windows WMI

`Win32_PnPEntity WHERE PNPClass='Camera' OR Service='usbvideo'`. **Scanner filtresi:**
- Name veya path'te `scan`, `escl`, `print`, `officejet`, `laserjet`, `deskjet` → atlanır
- Path'te `escl` veya `usbscan` → atlanır

### Android NDK

`ACameraManager_create()` + `ACameraManager_getCameraIdList()`:
- `ACAMERA_LENS_FACING` → "(Front)" / "(Back)" / "(External)"
- `ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE` → max_width / max_height

---

## 🖨️ Yazıcılar — `knst_vector<knst_printer_info>`

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

`/run/cups/cups.sock` veya `/var/run/cups/cups.sock` yoksa `lpstat` **hiç çağrılmaz**.

`lpstat -p -d` çıktısı parse edilir:
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

`KnstPrintDiscoveryHelper.discover(4000)` — mDNS `_ipp._tcp` / `_ipps._tcp` scan. Format `name|host|port|uri` parse edilir.

---

## 💻 Sistem — `knst_system_info`

```cpp
knst_system_info s = knst_devices::system();

s.manufacturer;         // "Dell Inc."
s.product_name;         // "XPS 15 9520"
s.version;
s.serial;
s.uuid;
s.sku;
s.family;
s.chassis_type;         // "Laptop" / "Desktop" / "Tablet" (normalize edilmiş)
s.chassis_serial;
s.asset_tag;
s.is_virtual_machine;   // QEMU / VMware / VirtualBox / Hyper-V / Xen / ... tespiti
s.is_laptop;            // chassis_type'tan heuristic
s.valid;
```

### Virtual machine tespiti

DMI manufacturer/product_name'de bu anahtar kelimeler aranır:
- `QEMU`, `VMware, Inc.`, `innotek GmbH`, `Xen`, `Amazon EC2`, `Google`, `OpenStack Foundation`, `Oracle Corporation`, `Parallels Software International Inc.`, `DigitalOcean`
- `VirtualBox`, `VMware`, `Virtual Machine`, `KVM`, `Bochs`, `Xen HVM`

### OEM placeholder filtreleme

Bu string'ler boş sayılır:
- `"Not Specified"`, `"Not Present"`
- `"To be filled by O.E.M."`, `"To Be Filled By O.E.M."`
- `"Default string"`, `"System Product Name"` (ASUS/Gigabyte default)
- `"System Version"`, `"System Serial Number"`, `"SKU"`, `"Base Board Product Name"`

### Windows chassis type

`Win32_SystemEnclosure.ChassisTypes` array'inden ilk int alınır. SMBIOS kodları Linux'takiyle aynı (3=Desktop, 9=Laptop, 30=Tablet, ...) — Windows için ayrı bir lookup tablosu var.

### Android

`ro.product.manufacturer`, `ro.product.model`, `ro.serialno`, `ro.product.device`, `ro.hardware` üzerinden. Serial için sırasıyla denenir:
1. `ro.serialno`
2. `ro.boot.serialno`
3. `ril.serialnumber`
4. `persist.radio.serialno`
5. `Build.getSerial()` (JNI helper)
6. `"unknown"`

Emulator tespiti: `ro.hardware` = `goldfish` / `ranchu` veya model `sdk`/`Emulator` içeriyorsa.

---

## 🐧 İşletim Sistemi — `knst_os_info`

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
- `/etc/timezone` veya `TZ` env — timezone
- `/proc/sys/kernel/random/boot_id` — boot_id (hash edilir)

### Windows

- `RtlGetVersion` (ntdll) — doğru sürüm (GetVersionEx deprecated)
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
- `CLOCK_BOOTTIME` → uptime (denied `/proc` durumları için)
- `/proc/uptime` fallback
- `/proc/stat btime` son çare

---

## 🔧 Sürücüler — `knst_vector<knst_driver_info>`

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

**Windows:** WMI `Win32_SystemDriver` — `Name`, `DisplayName`, `State`, `StartMode`, `PathName`. `StartMode = "Boot" / "System"` ise `is_builtin = true`.

**Android:** `/proc/modules` (Linux ile aynı — çoğunlukla çalışır).

---

## 🎮 GPU Köprüsü — `knst_devices::gpu_*`

`knst_devices` GPU sorgularını `knst_gpu` (knst_process.hpp içinde) üzerine yönlendirir:

```cpp
knst_devices::has_gpu();                    // GPU var mı?
knst_devices::gpu_count();                  // Kaç GPU?
knst_devices::nvidia_driver_version();      // "535.183.01"
knst_devices::cuda_version();               // "12.2"

auto gpus = knst_devices::gpus();           // knst_vector<knst_gpu_info>
auto procs = knst_devices::gpu_processes(); // knst_vector<knst_gpu_process>
auto procs0 = knst_devices::gpu_processes(0);   // Sadece GPU 0
```

**Tip tanımları `knst_gpu_info` ve `knst_gpu_process` `knst_process.hpp`'den gelir.**

**Platform:**
- **Linux:** NVIDIA (NVML) + AMD (sysfs) + Intel (i915 sysfs)
- **Windows:** NVIDIA (NVML) + DXGI + PDH
- **Android:** Adreno (kgsl sysfs) + Mali (mali0 sysfs)

---

## 🛠️ `knst_dev` Yardımcı Altyapısı (YENİ)

`knst_devices.hpp` **`knst_dev` namespace'inde önemli yardımcı altyapı** sağlar. Kullanıcı kodu bunları doğrudan kullanabilir.

### 1) `knst_dev::status` — Yeni hata enum'u

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

**`status_string()` dönüşleri:**
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

> ⚠️ **ESKİ `knst_device_error` KALDIRILDI.** Yeni kodda `knst_dev::status` kullan.

### 2) `knst_dev::result<T>` — Standart sonuç tipi

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

**`forward_error<To>(from)`** — hata tipini aktarır.

### 3) `knst_dev::argv` — Argüman builder

```cpp
struct argv {
    knst_vector<knst_c16string> v;

    argv& operator<<(const char*);
    argv& operator<<(const knst_c16string&);
    argv& operator<<(unsigned int);
    argv& operator<<(int);             // negative handling dahil

    argv& push_back(const char*);
    argv& push_back(const std::string&);
    argv& push_back(const knst_c16string&);
};
```

**Kullanım:**
```cpp
knst_dev::argv a;
a << "-s" << "TERM" << "2.5" << "parec"
  << "--format=s16le" << "--rate=16000" << "--channels=1";

auto r = knst_dev::run("timeout", a, 5000);
```

**Shell injection yok** — argv doğrudan `execve`/`CreateProcessW`'ye geçer.

### 4) `knst_dev::run_tool` / `run` / `run_codes` / `run_tool_raw`

```cpp
// Basit çalıştırma (exit 0 = success)
result<knst_byte_string> run_tool(const char* prog,
                                   const knst_vector<knst_c16string>& args,
                                   uint32_t timeout_ms = 5000) noexcept;

// argv builder ile
result<knst_byte_string> run(const char* prog, const argv& a,
                             uint32_t timeout_ms = 5000) noexcept;

// Belirli exit code'ları kabul et (coreutils `timeout` = 124 gibi)
result<knst_byte_string> run_codes(const char* prog, const argv& a,
                                    uint32_t timeout_ms,
                                    std::initializer_list<int> ok_codes) noexcept;

// Ham sonuç (launched, exit_code, out)
struct tool_result { bool launched; int exit_code; knst_byte_string out; };
tool_result run_tool_raw(const char* prog, const argv& a,
                         uint32_t timeout_ms = 5000) noexcept;

// result<byte> → result<void>
result<void> drop(const result<knst_byte_string>& r) noexcept;
```

**Hata davranışı:**
- `knst_process::run_capture` fail → `ToolMissing`
- exit_code != 0 → `IoError` + native exit code
- Başarı → `success(out_data)`

### 5) Validator'lar

```cpp
bool is_safe_token(const knst_c16string& s, uint32_t max_len = 127) noexcept;
// [a-zA-Z0-9_-@.:]  + '-' ile başlayamaz

bool is_page_range(const knst_c16string& s) noexcept;
// "1-3,5,7" — sadece digit, ',' ve '-'

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

**Kullanım:** Kullanıcı girdisi veya harici veri ile araç çağırmadan önce doğrula.

### 6) `knst_dev::format` / `text`

```cpp
knst_c16string format(const char* fmt, ...) noexcept;
// sprintf benzeri → knst_c16string

const char* text(const knst_byte_string& b) noexcept;
// byte string → const char* (NUL-terminated garantisi)
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
// "/sys/" prefix + ".." yok

result<void> sysfs_write(const char* path, const char* value) noexcept;
result<knst_c16string> sysfs_read(const char* path) noexcept;
```

**Güvenlik:** Path traversal (`..`) engellenir. `O_CLOEXEC` kullanılır.

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

**Kullanım:** USB/disk/input takıp çıkarma olaylarını dinle.

```cpp
auto mon = knst_dev::uevent_monitor::open();
if (mon) {
    auto ev = mon.value.wait(5000, "usb");
    if (ev && ev.value.action == u"add") {
        std::cout << "USB eklendi: " << ev.value.devname << "\n";
    }
}
```

**Güvenlik:** `nl_pid == 0` kontrolü — sadece **kernel**'den gelen mesajlar kabul edilir (user-space spoofing engellenir).

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

`KNST_JSON_HPP` define'lıysa (`knst_json.hpp` include edildiyse):

```cpp
namespace knst_devices {

// 6 kategori: cpu, memory, system, os, storage (array), network (array)
knst_json full_inventory_json() noexcept;

// Atomic file write
bool save_inventory_json(const knst_c16string& path,
                         const knst_json_dump_options& opt = {}) noexcept;

}
```

**JSON şeması:**
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

**Not:** JSON bridge tüm 18 kategoriyi **kapsamaz** — sadece 6 tanesini. Diğer kategoriler için direkt API kullan.

**Kullanım:**
```cpp
#if defined(KNST_JSON_HPP)
    knst_json inv = knst_devices::full_inventory_json();
    inv.save_file(u"/var/lib/myapp/inventory.json",
                  knst_json_dump_options::pretty(2),
                  /*atomic=*/true);
#endif
```

---

## 📱 Android Entegrasyonu

Android'de JNI env gerekir. `JNI_OnLoad` veya `ANativeActivity_onCreate` başında **bir kez** çağır:

```cpp
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(&env, JNI_VERSION_1_6) != JNI_OK) return -1;
    // ... activity bul ...
    jobject activity = /* ... */;
    knst_devices::detail_android::set_java_vm(vm, activity);
    return JNI_VERSION_1_6;
}
```

**`set_java_vm` ne yapar?**
1. `g_vm` + `g_activity_obj` saklar
2. `KnstDevicesHelper.setContext(activity)` çağırır (audio, wifi enrichment, file paths için)
3. `KnstBluetoothHelper.setContext(activity)` çağırır
4. `knst_file::set_android_dirs(cache_dir, home_dir, ext_dir)` — Java helper'dan path'leri alır

**JNI olmadan** Android kategorileri `valid=true` ama **boş** döner (audio, power fast path hariç). CPU, memory, network, drivers gibi sysfs/procüzerinden çalışan kategoriler JNI olmadan da çalışır.

**Java helper sınıfları:**
- `com.knst.devices.KnstDevicesHelper` — audio, wifi, system serial, file paths
- `com.knst.devices.KnstBluetoothHelper` — bluetooth (opsiyonel)
- `com.knst.print.KnstPrintDiscoveryHelper` — printer mDNS (knst_prn ile ortak)

---

## ⚡ Performans Notları

### Lazy yükleme

| İşlem | Ne zaman? |
|---|---|
| **DMI cache** | İlk `motherboard()` veya `system()` |
| **Bluetooth fast-path** | `/sys/class/bluetooth` boşsa skip |
| **CUPS fast-path** | Socket yoksa skip |
| **Audio fast-path** | Pulse/PipeWire socket yoksa skip |
| **CPU usage** | 200 ms cached (Linux'ta 20 ms `nanosleep` + iki `/proc/stat`) |

### CPU kullanımı — 20 ms maliyet

Linux'ta `cpu()` **ilk** çağrıda 20 ms `nanosleep` yapar (iki `/proc/stat` okuması arasında). Sonraki **200 ms içinde tekrar çağrılırsa cached**. Uygulama başında bir kez çağır, cache'le.

### Harici araçlar (Linux)

| Araç | Ne için? | Gerekli mi? |
|---|---|---|
| `dmidecode` | DMI (root) | Opsiyonel (sysfs fallback) |
| `lspci` | PCI isimleri | Opsiyonel |
| `iw` | WiFi SSID/sinyal | Opsiyonel |
| `pactl` | Ses cihazları | Opsiyonel |
| `bluetoothctl` | Bluetooth | Opsiyonel |
| `lpstat` | Yazıcılar | Opsiyonel |
| `nvidia-smi` | NVIDIA GPU | Opsiyonel |

Eksik araçlar **hata vermez**, sadece ilgili alanlar boş kalır. Fast-path kontrolleri (socket, dir) araç çağrısını atlar.

### Windows notları

- **COM:** `audio()` `CoInitializeEx(COINIT_MULTITHREADED)` çağırır. `RPC_E_CHANGED_MODE` gracefully handle edilir.
- **Session 0:** Servis olarak çalışıyorsan `EnumDisplayDevices` boş dönebilir — normaldir.
- **WMI `root\WMI`:** Sensörler ayrı namespace — ayrı sorgu mekanizması.
- **PowerShell:** WMI sorguları `powershell.exe` üzerinden `Get-CimInstance` ile. UTF-8 zorlanır.

### Windows WMI query output

Her WMI sorgusu `ConvertTo-Csv -NoTypeInformation -Delimiter ','` ile parse edilir. UTF-16LE → UTF-8 normalize edilir.

### Android notları

- **sysfs SELinux:** `/sys/class/net/*/wireless` gibi yollar kısıtlı. İsim bazlı tip tahmini devreye girer.
- **JNI Attach:** `get_env()` her çağrıda `AttachCurrentThread` dener (idempotent).
- **KnstDevicesHelper classloader:** `FindClass` app classloader'ı görmez. `activity.getClassLoader().loadClass()` kullanılır (`find_app_class`).

---

## 📊 Performans Özeti

| İşlem | Linux | Windows | Android |
|---|---|---|---|
| `cpu()` | 20-50 ms (ilk), ~0 ms (cache) | 100-300 ms (PowerShell WMI) | 20-50 ms |
| `memory()` | 1-2 ms | 100-300 ms | 1-2 ms |
| `storage()` | 20-100 ms (LVM varsa) | 100-300 ms | 10-50 ms (JNI) |
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

**Not:** Windows latency'sinin çoğu **PowerShell startup** (100-200 ms per WMI query). Batch query gerekirse cache kullan veya `wmic` (deprecated) yerine tek `Get-CimInstance` çağrısı yap.

---

## ✅ Yap / ❌ Yapma

### ✅ Yap

```cpp
// 1. valid kontrolü
knst_cpu_info c = knst_devices::cpu();
if (!c.valid) return;

// 2. Kısmi veri bayraklarını kontrol et
if (c.has_temperature)
    std::cout << c.temperature_c << " °C\n";

// 3. Sonuçları cache'le
static const auto cached_cpu = knst_devices::cpu();

// 4. Loopback / down arayüzleri filtrele
for (const auto& n : knst_devices::network()) {
    if (n.is_loopback || !n.is_up) continue;
    // ...
}

// 5. Kullanıcı girdisi için validator kullan
if (!knst_dev::is_dev_path(user_input)) return;
auto r = knst_dev::sysfs_read(knst_dev::text(knst_dev::to_utf8(user_input)));

// 6. Yeni status enum'u
knst_dev::status st = knst_dev::map_errno(EACCES);
if (st == knst_dev::status::PermissionDenied) { /* ... */ }

// 7. Android'de JNI'yi başlat
#if defined(KNST_USING_PLATFORM_ANDROID)
knst_devices::detail_android::set_java_vm(vm, activity);
#endif

// 8. JSON envanter
#if defined(KNST_JSON_HPP)
knst_devices::save_inventory_json(u"/var/inventory.json");
#endif
```

### ❌ Yapma

```cpp
// 1. Sürekli CPU sorgulamak (20 ms sleep her 200 ms'de)
for (int i = 0; i < 10000; ++i) {
    auto c = knst_devices::cpu();   // ❌ her 200 ms'de 20 ms sleep
}

// 2. valid kontrolü atlamak
std::cout << c.model_name;   // ❌ c.valid=false olabilir

// 3. Eski error enum'u beklemek
// ❌ knst_device_error artık yok
// ✅ knst_dev::status kullan

// 4. Uygulama başında tüm kategorileri çağırmak
auto cpu = knst_devices::cpu();
auto usb = knst_devices::usb();      // ❌ PowerShell yavaş
auto pci = knst_devices::pci();      // ❌
auto bt = knst_devices::bluetooth(); // ❌

// 5. Android'de JNI init atlamak
// ❌ audio/wifi/system.serial boş döner
// ✅ JNI_OnLoad'ta set_java_vm çağır

// 6. Kullanıcı girdisi ile araç çalıştırmak
knst_dev::argv a;
a << "--option" << user_input;   // ❌
// ✅ knst_dev::is_safe_token(user_input) önce kontrol et

// 7. sysfs path guard bypass
knst_dev::sysfs_read("/sys/class/../../etc/passwd");   // ❌ is_sysfs_path false
```

---

## 📚 Tam Örnek

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

    // Diskler
    auto disks = knst_devices::storage();
    std::cout << "\nDiskler:\n";
    for (const auto& d : disks) {
        std::cout << "  " << d.device << " - " << d.model
                  << " (" << d.total_bytes / (1024ULL * 1024 * 1024) << " GB)\n";
        for (const auto& p : d.partitions) {
            std::cout << "    └─ " << p.mount_point
                      << " (" << p.fs_type << ")\n";
        }
    }

    // Ağ
    auto nets = knst_devices::network();
    std::cout << "\nAğ arayüzleri:\n";
    for (const auto& n : nets) {
        if (n.is_loopback) continue;
        std::cout << "  " << n.name << " - " << n.ipv4
                  << " (" << (n.is_wireless ? "WiFi" : "Ethernet") << ")\n";
    }

    // Monitörler
    auto mons = knst_devices::monitors();
    std::cout << "\nMonitörler:\n";
    for (const auto& mon : mons) {
        std::cout << "  " << mon.manufacturer
                  << " " << mon.current_width << "x" << mon.current_height
                  << " @ " << mon.refresh_hz << " Hz\n";
    }

    // Sistem
    knst_system_info s = knst_devices::system();
    std::cout << "\nSistem: " << s.manufacturer << " " << s.product_name
              << " (" << s.chassis_type << ")\n";

    // OS
    knst_os_info o = knst_devices::os();
    std::cout << "OS: " << o.os_name << " " << o.os_version << "\n";
    std::cout << "Uptime: " << o.uptime_seconds / 3600 << " saat\n";

    // JSON envanter
#if defined(KNST_JSON_HPP)
    knst_devices::save_inventory_json(u"/tmp/inventory.json");
    std::cout << "\nEnvanter /tmp/inventory.json dosyasına yazıldı\n";
#endif

    return 0;
}
```

---

## 📌 API Kontrat Özeti

```cpp
namespace knst_devices {

// ─── 18 kategori + GPU köprüsü ───
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

// GPU köprüsü (knst_gpu'dan)
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

## 🎯 Size Getireceği Kazanç

`knst_devices` şu durumlarda ciddi avantaj sağlar:

- ✅ **Sistem tanılama** — 18 kategori tek API
- ✅ **Oyun motoru / uyarlanabilir kalite** — sistem profiline göre ayar
- ✅ **Benchmark / telemetri** — donanım bilgisi + sonuç
- ✅ **Uyumluluk kontrolü** — "AVX2 var mı?" hızlı sorgu
- ✅ **Pil yönetimi** — batarya durumuna göre davranış
- ✅ **Envanter yazılımı** — tüm donanım JSON olarak raporlanır
- ✅ **Cross-platform** — Linux + Windows + **Android**
- ✅ **Shell injection yok** — argv builder + validators
- ✅ **Kernel hotplug** — uevent_monitor ile USB/disk olay takibi
- ✅ **`knst_gpu` entegrasyonu** — GPU sorguları hazır
- ✅ **`knst_json` entegrasyonu** — atomik dosya kaydı
- ✅ **`knst_file` entegrasyonu** — path handling, Android dir setup
- ✅ **`knst_process` entegrasyonu** — tool runner altyapısı

---