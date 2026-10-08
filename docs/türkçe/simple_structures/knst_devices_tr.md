# knst_devices — Donanım Tarama Kütüphanesi

Selamlar! 👋 Bu doküman `knst_devices` kütüphanesinin ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **18 kategori altında tüm sistem donanımını (CPU, RAM, disk, ağ, USB, PCI, monitör, sensör, kamera, yazıcı, batarya, Bluetooth...) standart bilgi yapılarına okuyan bir donanım envanteri kütüphanesi.** Linux'ta `/proc`, `/sys` ve harici araçlarla, Windows'ta WMI + native API'lerle çalışır.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **18 kategori** | CPU'dan yazıcıya kadar her şey tek bir namespace altında |
| **Tam cross-platform** | Linux ve Windows'ta **her kategori** çalışır |
| **Standart yapılar** | Her sorgu `valid` bayrağı olan bir `struct` döner |
| **Eksik veri = varsayılan değer** | OS sağlamıyorsa alan default kalır — `valid` ile kontrol et |
| **Lazy yükleme** | DMI cache gibi pahalı işlemler `call_once` ile bir kez çalışır |
| **Hızlı yol kontrolleri** | Bluetooth/CUPS yoksa araç çağrısı hiç yapılmaz |
| **UTF-16 string** | Tüm metin alanları `knst_c16string` — Windows uyumlu |
| **Vector dönüşler** | USB, PCI, storage, network gibi kategoriler `knst_vector<T>` döner |
| **Hata yönetimi** | `knst_device_error` enum'ı ile standart hata kodları |

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
        std::cout << "Çekirdek: " << cpu.physical_cores  << " fiziksel / " << cpu.logical_cores << " mantıksal\n";
        std::cout << "Maks frekans: " << cpu.max_mhz << " MHz\n";
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

    return 0;
}
```

---

## 📋 18 Kategori

| # | Kategori | Fonksiyon | Dönüş Tipi |
|---|---|---|---|
| 1 | **CPU** | `knst_devices::cpu()` | `knst_cpu_info` |
| 2 | **Anakart + BIOS** | `knst_devices::motherboard()` | `knst_motherboard_info` |
| 3 | **Bellek** | `knst_devices::memory()` | `knst_memory_info` |
| 4 | **Depolama** | `knst_devices::storage()` | `knst_vector<knst_storage_info>` |
| 5 | **Ağ** | `knst_devices::network()` | `knst_vector<knst_network_info>` |
| 6 | **USB** | `knst_devices::usb()` | `knst_vector<knst_usb_info>` |
| 7 | **PCI** | `knst_devices::pci()` | `knst_vector<knst_pci_info>` |
| 8 | **Monitörler** | `knst_devices::monitors()` | `knst_vector<knst_monitor_info>` |
| 9 | **Giriş cihazları** | `knst_devices::input()` | `knst_vector<knst_input_info>` |
| 10 | **Ses** | `knst_devices::audio()` | `knst_audio_info` |
| 11 | **Güç/Batarya** | `knst_devices::power()` | `knst_power_info` |
| 12 | **Sensörler** | `knst_devices::sensors()` | `knst_vector<knst_sensor_info>` |
| 13 | **Bluetooth** | `knst_devices::bluetooth()` | `knst_bluetooth_info` |
| 14 | **Kameralar** | `knst_devices::cameras()` | `knst_vector<knst_camera_info>` |
| 15 | **Yazıcılar** | `knst_devices::printers()` | `knst_vector<knst_printer_info>` |
| 16 | **Sistem** | `knst_devices::system()` | `knst_system_info` |
| 17 | **İşletim Sistemi** | `knst_devices::os()` | `knst_os_info` |
| 18 | **Sürücüler** | `knst_devices::drivers()` | `knst_vector<knst_driver_info>` |
| ➕ | **GPU (köprü)** | `knst_devices::gpus()` | `knst_vector<knst_gpu_info>` |

---

## 🌍 Platform Desteği

**Tüm kategoriler hem Linux hem Windows'ta çalışır.**

| Kategori | Linux | Windows |
|---|---|---|
| CPU | ✅ `/proc/cpuinfo`, `/sys/...cpufreq`, `/sys/class/hwmon` | ✅ WMI (`Win32_Processor`) |
| Anakart | ✅ `dmidecode` + `/sys/class/dmi/id` | ✅ WMI (`Win32_BaseBoard`, `Win32_BIOS`) |
| Bellek | ✅ `/proc/meminfo` + `dmidecode -t memory` | ✅ WMI (`Win32_PhysicalMemory`) |
| Depolama | ✅ `/sys/block` + `/proc/mounts` | ✅ WMI (`Win32_DiskDrive`) |
| Ağ | ✅ `getifaddrs` + `/sys/class/net` | ✅ WMI + `netsh wlan` |
| USB | ✅ `/sys/bus/usb/devices` | ✅ WMI (`Win32_PnPEntity`) |
| PCI | ✅ `/sys/bus/pci/devices` + `lspci` | ✅ WMI (`Win32_PnPEntity`) |
| **Monitör** | ✅ `/sys/class/drm` + EDID | ✅ `EnumDisplayDevices` + registry EDID + `WmiMonitorID` (isim/üretici/seri) + `WmiMonitorBrightness` |
| **Giriş** | ✅ `/sys/class/input` | ✅ `GetRawInputDeviceList` + HID API |
| **Ses** | ✅ `pactl` (PulseAudio/PipeWire) | ✅ `IMMDeviceEnumerator` (COM) |
| Güç | ✅ `/sys/class/power_supply` | ✅ `GetSystemPowerStatus` |
| **Sensörler** | ✅ `/sys/class/hwmon` | ✅ WMI `root\WMI` + `Win32_Fan` + `Win32_VoltageProbe` |
| **Bluetooth** | ✅ `bluetoothctl` | ✅ `BluetoothFindFirstRadio` + `BluetoothFindFirstDevice` |
| **Kameralar** | ✅ `/sys/class/video4linux` | ✅ WMI (`PNPClass='Camera'`) |
| Yazıcılar | ✅ `lpstat` | ✅ WMI (`Win32_Printer`) |
| Sistem | ✅ DMI | ✅ WMI (`Win32_ComputerSystem`) |
| OS | ✅ `/etc/os-release` + `uname` | ✅ `RtlGetVersion` + `GetComputerNameW` |
| Sürücüler | ✅ `/proc/modules` | ✅ WMI (`Win32_SystemDriver`) |

✅ = Tam destekli.

---

## 🖥️ CPU — `knst_cpu_info`

```cpp
knst_cpu_info c = knst_devices::cpu();

c.vendor;              // "GenuineIntel" / "AuthenticAMD"
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
c.temperature_c;       // °C (coretemp/k10temp)
c.usage_percent;       // anlık kullanım %
c.load_avg_1;          // /proc/loadavg (Linux)
c.load_avg_5;
c.load_avg_15;

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

**Örnek:**

```cpp
knst_cpu_info c = knst_devices::cpu();
if (c.valid) {
    std::cout << c.model_name << "\n"
              << "  " << c.physical_cores << "C/" << c.logical_cores << "T @ "
              << c.max_mhz << " MHz\n"
              << "  AVX2: " << (c.has_avx2 ? "var" : "yok") << "\n";
}
```

---

## 💾 Bellek — `knst_memory_info`

```cpp
knst_memory_info m = knst_devices::memory();

m.total_bytes;         // Toplam fiziksel RAM
m.used_bytes;          // Kullanılan
m.free_bytes;          // Boş
m.available_bytes;     // Kullanılabilir (cache hariç)
m.cached_bytes;        // Sayfa cache
m.buffers_bytes;       // Kernel buffer'ları

m.swap_total;          // Swap toplam
m.swap_used;           // Swap kullanılan
m.swap_free;           // Swap boş
m.has_swap;

m.slot_count;          // Toplam RAM slotu
m.slots_used;          // Kullanılan slot sayısı

// Bellek modülleri
for (const auto& mod : m.modules) {
    mod.size_bytes;    // Modül boyutu
    mod.speed_mhz;     // Hız
    mod.manufacturer;  // "Samsung" / "Kingston"
    mod.part_number;   // "KHX3200C16D4/8G"
    mod.serial;
    mod.locator;       // "DIMM_A1"
    mod.type;          // "DDR4" / "DDR5"
    mod.form_factor;   // "DIMM" / "SODIMM"
    mod.ecc;
}

m.has_modules;         // Modül bilgisi okunabildi mi?
```

**GB cinsinden gösterim:**

```cpp
auto gb = [](uint64_t b) { return b / (1024ULL * 1024 * 1024); };

std::cout << "RAM: " << gb(m.used_bytes) << "/" << gb(m.total_bytes) << " GB\n";
```

---

## 💽 Depolama — `knst_vector<knst_storage_info>`

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
    s.rotational_rpm;      // HDD için 5400/7200

    // SMART verileri (varsa)
    s.has_smart;
    s.smart_passed;
    s.smart_power_on_hours;
    s.smart_bytes_written;
    s.smart_reallocated_sectors;
    s.smart_pending_sectors;
    s.smart_percent_used;

    // Bölümler
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

**LVM/LUKS desteği:** Linux'ta kütüphane `/dev/mapper/*` ve `/dev/dm-*` aygıtlarını otomatik olarak fiziksel disklerine çözer. Pop!_OS, Ubuntu, Fedora gibi LUKS+LVM kullanan sistemlerde doğru çalışır.


**Windows'ta:**
- **SSD/HDD tespiti:** `MSFT_PhysicalDisk` (root\Microsoft\Windows\Storage) — `MediaType` alanı 3=HDD, 4=SSD
- **System disk tespiti:** `Win32_DiskPartition` üzerinden boot partition'ın hangi `PHYSICALDRIVE`'da olduğu bulunur ve `is_system_disk = true` işaretlenir

---

## 🌐 Ağ — `knst_vector<knst_network_info>`

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

    // WiFi bilgileri
    n.ssid;             // "EvWifi_5G"
    n.signal_dbm;       // -47
    n.frequency_mhz;    // 5180
    n.channel;          // 36
    n.security;         // "WPA2-PSK"

    n.has_ip;
    n.has_stats;
    n.has_wifi;
}
```

**WiFi örneği:**

```cpp
for (const auto& n : nets) {
    if (n.has_wifi) {
        std::cout << "SSID: " << n.ssid  << "  Signal: " << n.signal_dbm << " dBm\n";
    }
}
```

**Not:** Windows'ta WiFi bilgileri `netsh wlan show interfaces` üzerinden okunur — SSID, kanal ve güvenlik tipi otomatik olarak doldurulur.

---

## 🔌 USB — `knst_vector<knst_usb_info>`

```cpp
for (const auto& u : knst_devices::usb()) {
    u.bus;              // USB bus numarası
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

## 🖼️ Monitörler — `knst_vector<knst_monitor_info>`

```cpp
for (const auto& m : knst_devices::monitors()) {
    m.name;
    m.manufacturer;     // EDID'den: "Dell", "LG"
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
    m.physical_width_mm;   // Fiziksel genişlik (mm)
    m.physical_height_mm;
    m.diagonal_inch;    // 27.0
    m.gamma;
    m.hdr;
    m.is_primary;
    m.brightness;       // 0-100 (%)
}
```

**Linux:** EDID verisi `/sys/class/drm/card*/card*-*/edid` dosyasından okunur.

**Windows:** `EnumDisplayDevicesW` ile monitör listesi alınır; EDID registry'den (`HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\...\Device Parameters\EDID`) okunur. Monitör ismi, üreticisi ve seri numarası `WmiMonitorID` (root\WMI namespace) üzerinden alınır. Parlaklık `WmiMonitorBrightness` üzerinden gelir.

---

## ⌨️ Giriş Cihazları — `knst_vector<knst_input_info>`

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

**Linux:** `/sys/class/input/event*` üzerinden tip belirlenir. Sınıflandırma **iki aşamalıdır**:

1. **İsim bazlı (öncelikli):** Cihaz isminde `keyboard`, `mouse`, `touchpad`, `gamepad`, `xbox`, `logitech k/m`, `wmi hotkeys` gibi anahtar kelimeler aranır.
2. **Capability bazlı (fallback):** İsim eşleşmezse `capabilities/key`, `capabilities/rel`, `capabilities/abs` dosyaları okunur. Abs bit sayısı ile klavye/gamepad ayrımı yapılır (≤2 bit = ses tekerlekli klavye, >4 bit = gamepad).

**Filtreler:**
- Ses jak'ları (`HD-Audio`, `Rear Mic`, `Line Out`, `HDMI/DP`) filtrelenir
- Sanal event'ler (`Power Button`, `Sleep Button`, `Video Bus`, `Lid Switch`) filtrelenir

Bu sayede gerçek klavye/fare/gamepad dışındaki gürültü elenir.

**Windows:** `GetRawInputDeviceList` + `GetRawInputDeviceInfoW` ile HID cihazları listelenir. Ürün adı `HidD_GetProductString` ile alınır. Usage page bazlı tip sınıflandırması yapılır (Mouse, Keyboard, Gamepad, Touchpad, Remote).

---

## 🔊 Ses — `knst_audio_info`

```cpp
knst_audio_info a = knst_devices::audio();

a.default_sink;         // Varsayılan çıkış
a.default_source;       // Varsayılan giriş (mikrofon)

for (const auto& d : a.devices) {
    d.index;
    d.name;             // "alsa_output.pci-0000_00_1f.3.analog-stereo"
    d.description;
    d.is_input;         // Mikrofon mu?
    d.is_output;        // Hoparlör mü?
    d.is_default;
    d.volume_percent;
    d.muted;
    d.sample_rate;
    d.channels;
}
```

**Linux:** PulseAudio/PipeWire gerekir (`pactl` üzerinden). Socket yoksa kütüphane hızlı döner.

**Windows:** `IMMDeviceEnumerator` COM API'si kullanılır. `eRender` ve `eCapture` endpoint'leri ayrı ayrı listelenir, ürün adı `PKEY_Device_FriendlyName` property'sinden alınır.

---

## 🔋 Güç — `knst_power_info`

```cpp
knst_power_info p = knst_devices::power();

p.has_battery;
p.ac_connected;         // Şarj takılı mı?
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

## 🌡️ Sensörler — `knst_vector<knst_sensor_info>`

```cpp
for (const auto& s : knst_devices::sensors()) {
    s.chip;             // "coretemp" / "k10temp" / "nct6775"
    s.name;             // "Package id 0" / "Core 0"
    s.type;             // Temperature, Fan, Voltage, Current, Power
    s.value;            // 45.0 (°C), 1200 (RPM), 12.05 (V)
    s.unit;             // "°C", "RPM", "V", "A", "W"
    s.min_value;
    s.max_value;
    s.critical;         // Kritik sıcaklık
}
```

**Linux:** `/sys/class/hwmon/*/` — `lm-sensors` paketiyle birlikte çalışır.

**Windows:** WMI `root\WMI` namespace'indeki `MSAcpi_ThermalZoneTemperature` (ACPI termal bölgeleri) + `Win32_Fan` + `Win32_VoltageProbe` + `Win32_TemperatureProbe` sınıfları kullanılır.

---

## 📱 Bluetooth — `knst_bluetooth_info`

```cpp
knst_bluetooth_info b = knst_devices::bluetooth();

b.has_adapter;
b.adapter_name;         // "Intel AX200"
b.adapter_address;      // "AA:BB:CC:DD:EE:FF"
b.powered;              // Açık mı?
b.discoverable;

for (const auto& d : b.devices) {
    d.address;
    d.name;             // "Sony WH-1000XM4"
    d.connected;
    d.paired;
    d.trusted;
    d.rssi;             // Sinyal gücü
    d.type;             // "Headphones" / "Keyboard"
}
```

**Linux:** `bluetoothctl` aracı gerekir. Adaptör yoksa kütüphane hızlı döner (D-Bus timeout'unu beklemez).

**Windows:** `BluetoothFindFirstRadio` + `BluetoothGetRadioInfo` ile adaptör; `BluetoothFindFirstDevice` + `BluetoothFindNextDevice` ile eşleştirilmiş cihazlar listelenir.

---

## 📷 Kameralar — `knst_vector<knst_camera_info>`

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

**Linux:** `/sys/class/video4linux/` taranır.

**Windows:** WMI `Win32_PnPEntity` (`PNPClass='Camera'` veya `PNPClass='Image'`) üzerinden kamera aygıtları listelenir; VID/PID device ID'den çıkarılır.

---

## 🖨️ Yazıcılar — `knst_vector<knst_printer_info>`

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

**Linux:** CUPS socket'i yoksa kütüphane hiç `lpstat` çağırmaz.

**Windows:** WMI `Win32_Printer` sınıfı kullanılır.

---

## 💻 Sistem — `knst_system_info`

```cpp
knst_system_info s = knst_devices::system();

s.manufacturer;         // "Dell Inc."
s.product_name;         // "XPS 15 9520"
s.version;
s.serial;               // Servis etiketi
s.uuid;
s.sku;
s.family;               // "XPS"
s.chassis_type;         // "Laptop" / "Desktop" / "Tower" / ...
s.chassis_serial;
s.asset_tag;
s.is_virtual_machine;   // QEMU/VMware/VirtualBox
s.is_laptop;
```

**Linux:** Öncelikle `dmidecode` (root gerekir), yoksa `/sys/class/dmi/id/*` dosyaları (world-readable).

**Windows:** WMI `Win32_ComputerSystem`.

---

## 🐧 İşletim Sistemi — `knst_os_info`

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
o.uptime_seconds;       // Sistem açık kalma süresi
o.page_size;            // 4096
o.boot_id;
```

---

## 🔧 Sürücüler — `knst_vector<knst_driver_info>`

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

**Linux:** `/proc/modules` dosyası okunur.

**Windows:** WMI `Win32_SystemDriver`.

---

## 🎮 GPU Köprüsü — `knst_gpu`

`knst_devices` namespace'i GPU sorgularını `knst_gpu` (knst_process.hpp içinde) üzerine yönlendirir:

```cpp
knst_devices::has_gpu();                    // GPU var mı?
knst_devices::gpu_count();                  // Kaç GPU?
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
auto procs0 = knst_devices::gpu_processes(0);   // Sadece GPU 0
```

**Gereksinim:**
- **Linux:** NVIDIA/AMD sürücüleri + `nvidia-smi` / `rocm-smi` araçları
- **Windows:** Sadece sürücüler yeterli (NVML + DXGI + PDH native)

---

## 🛡️ Hata Yönetimi

Her kategori `valid` bayrağı olan bir yapı döner. **Veriyi kullanmadan önce `valid` kontrol et.**

```cpp
knst_cpu_info c = knst_devices::cpu();
if (!c.valid) {
    std::cout << "CPU bilgisi okunamadı\n";
    return 1;
}
// Şimdi güvenle kullan
std::cout << c.model_name << "\n";
```

### Hata Enum'u

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

### Alt `has_*` Bayrakları

Bazı yapılarda alt bayraklar var. Örneğin `knst_cpu_info`:

- `valid` → genel olarak veri var mı?
- `has_cores` → çekirdek sayısı okunabildi mi?
- `has_temperature` → sıcaklık okunabildi mi?
- `has_cache` → cache bilgisi var mı?
- `has_usage` → kullanım yüzdesi hesaplanabildi mi?

Bunlar **kısmi veri** durumunu belirtir. `valid=true` ama `has_temperature=false` olabilir — yani CPU bilgisi var ama sıcaklık okunamıyor.

---

## ⚡ Performans Notları

### Lazy Yükleme

Kütüphane pahalı işlemleri **bir kez** yapar:

- **DMI cache** — `dmidecode` çıktısı `std::call_once` ile yalnızca ilk çağrıda okunur. Sonraki `motherboard()` / `system()` çağrıları cache'den okur.
- **Hızlı yol kontrolleri** — Bluetooth adaptörü yoksa `bluetoothctl` hiç çağrılmaz; CUPS socket'i yoksa `lpstat` çağrılmaz. Bu sayede araç timeout'ları beklenmez.

### CPU Kullanım Ölçümü

`knst_devices::cpu()` çağrısı Linux'ta **20 ms** `nanosleep` yapar (iki `/proc/stat` okuması arasında). Bu, kullanım yüzdesinin doğru hesaplanması için gereklidir. Eğer sadece model adı istiyorsan bu 20 ms boşa gider.

**Optimizasyon ipucu:** Uygulama başında bir kez çağır, sonucu cache'le.

### Harici Araçlar (Linux)

| Araç | Gerekli mi? | Ne için? |
|---|---|---|
| `dmidecode` | Opsiyonel | Detaylı DMI (root) |
| `lspci` | Opsiyonel | PCI isimleri |
| `iw` | Opsiyonel | WiFi SSID/sinyal |
| `pactl` | Opsiyonel | Ses cihazları |
| `bluetoothctl` | Opsiyonel | Bluetooth |
| `lpstat` | Opsiyonel | Yazıcılar |
| `nvidia-smi` | Opsiyonel | NVIDIA GPU |

Eksik araçlar **hata vermez**, sadece ilgili alanlar boş kalır.

### Windows API Notları

- **COM başlatma** — `audio()` fonksiyonu `CoInitializeEx` çağırır. Uygulaman zaten COM kullanıyorsa `RPC_E_CHANGED_MODE` dönebilir; bu durum graceful olarak ele alınır.
- **Session 0** — Servis olarak çalışıyorsan (Session 0), `EnumDisplayDevices` gibi bazı API'ler boş dönebilir. Bu normaldir.
- **WMI root\WMI** — Sensörler `root\WMI` namespace'inde olduğu için ayrı bir sorgu mekanizması kullanılır.

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

    // İşletim sistemi
    knst_os_info o = knst_devices::os();
    std::cout << "OS: " << o.os_name << " " << o.os_version << "\n";
    std::cout << "Uptime: " << o.uptime_seconds / 3600 << " saat\n";

    return 0;
}
```

### Örnek Çıktı (Linux)

```
CPU: AMD Ryzen 7 5800X 8-Core Processor (16 threads)
RAM: 32 GB

Diskler:
  /dev/nvme0n1 - Samsung SSD 970 EVO Plus 1TB (931 GB)
    └─ /boot/efi (vfat)
    └─ / (ext4)
  /dev/sda - WDC WD20EZBX-00AYRA0 (1863 GB)
    └─ /mnt/data (ext4)

Ağ arayüzleri:
  enp5s0 - 192.168.1.42 (Ethernet)
  wlp4s0 - 192.168.1.100 (WiFi)

Monitörler:
  Dell 3840x2160 @ 60 Hz

Sistem: ASUS PRIME B550M-A (Desktop)
OS: Pop!_OS 22.04
Uptime: 4 saat
```

### Örnek Çıktı (Windows)

```
CPU: Intel(R) Core(TM) i9-13900K (32 threads)
RAM: 64 GB

Diskler:
  \\.\PHYSICALDRIVE0 - Samsung SSD 990 PRO 2TB (1863 GB)
  \\.\PHYSICALDRIVE1 - WDC WD40EZAZ-00SF3B0 (3726 GB)

Ağ arayüzleri:
  Ethernet - 192.168.1.42 (Ethernet)
  Wi-Fi    - 192.168.1.100 (WiFi)

Monitörler:
  Dell 3840x2160 @ 144 Hz

Sistem: ASUS ROG STRIX Z790-E (Desktop)
OS: Windows 10.0.22631
Uptime: 8 saat
```

---

## 💡 Kullanım İpuçları

### ✅ Yapılması Gerekenler

```cpp
// 1. Her zaman valid kontrol et
knst_cpu_info c = knst_devices::cpu();
if (!c.valid) return;   // ✅ erken çık

// 2. Alt bayrakları kontrol et
if (c.has_temperature) {
    std::cout << c.temperature_c << " °C\n";   // ✅ güvenli
}

// 3. Sonuçları cache'le
static const auto cached_cpu = knst_devices::cpu();   // ✅ 20ms'lik sleep bir kez

// 4. Ağ arayüzlerini filtrele
for (const auto& n : knst_devices::network()) {
    if (n.is_loopback || !n.is_up) continue;   // ✅ loopback'i atla
    // ...
}
```

### ❌ Kaçınılması Gerekenler

```cpp
// 1. Sürekli CPU sorgulamak
for (int i = 0; i < 1000; ++i) {
    auto c = knst_devices::cpu();   // ❌ her çağrı 20ms sleep!
}
// 2. valid kontrolü yapmadan okumak
std::cout << c.model_name;   // ❌ c.valid=false olabilir

// 3. Tüm kategorileri uygulama başında çağırmak
auto cpu = knst_devices::cpu();
auto mem = knst_devices::memory();
auto usb = knst_devices::usb();
auto pci = knst_devices::pci();       // ❌ hepsi harici araç çağırır
auto bt = knst_devices::bluetooth();  // ❌ yavaş başlangıç
// ✅ Çözüm: Sadece ihtiyaç duyduğunda çağır (lazy)

// 4. Root gerektiren bilgiyi zorlamak
// dmidecode root olmadan çalışmaz — /sys/class/dmi/id fallback'i kullan
```

---

## 🎯 Size Getireceği Kazanç

`knst_devices` şu durumlarda ciddi avantaj sağlar:

- ✅ **Sistem tanılama aracı yazıyorsan** — tüm donanım tek API ile
- ✅ **Oyun motoru / uygulama başlangıç ayarları** — sistem profiline göre kalite seç
- ✅ **Benchmark / telemetri** — donanım bilgisiyle birlikte sonuç kaydet
- ✅ **Uyumluluk kontrolü** — "AVX2 var mı?" gibi hızlı sorgu
- ✅ **Pil yönetimi** — batarya durumuna göre davranış
- ✅ **Envanter / stok yazılımı** — tüm donanımı raporla
- ✅ **Cross-platform build** — aynı kod Linux ve Windows'ta çalışır

