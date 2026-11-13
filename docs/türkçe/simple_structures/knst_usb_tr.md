# knst_usbc — Kapsamlı USB Kontrolcüsü

Selamlar! 👋 Bu doküman `knst_usbc` namespace'inin ne olduğunu, nasıl kullanıldığını ve hangi durumlarda işine yarayacağını anlatır.

**Kısaca:** USB cihazlarını **bulma**, **sorgulama**, **kontrol etme** ve **olaylarını dinleme** için kapsamlı bir framework. Linux'ta sysfs üzerinden tam kontrol (authorization, reset, power cycle, driver bind/unbind, hub port enumeration, error stats, health check, hotplug). Windows'ta enumeration + PowerShell üzerinden güvenli eject. Android'de sadece read-only enumeration.

Kernel **6.x ve 7.x** üzerinde test edildi, **5.x+** üzerinde çalışmalı. Şu sysfs layout varyasyonlarını otomatik tolere eder:

- `1-2`, `1-2.3` (normal USB cihaz yolları)
- `usbN` (N=1..99) root hub isimleri
- Interface isimleri `1-2:1.0`
- Port isimleri `usbN-portM`, `1-2-portM`
- `/sys/bus/usb/devices` olmayan container'larda → boş sonuç

---

## 🎯 Honesty Matrix — Platform Başına Ne Var?

| İşlem | Linux | Windows | Android |
|---|---|---|---|
| **Cihaz listeleme** | ✅ | ✅ | ✅ |
| **VID/PID ile arama** | ✅ | ✅ | ✅ |
| **Class ile arama** | ✅ | ✅ | ✅ |
| **Driver ile arama** | ✅ | ✅ PowerShell | ⚠️ Read-only |
| **Hub listeleme** | ✅ | ✅ | ✅ |
| **Reset** | ✅ ioctl | ❌ | ❌ |
| **Power cycle** | ✅ unbind/bind | ❌ | ❌ |
| **Authorize/Deauthorize** | ✅ | ❌ | ❌ |
| **Runtime PM** | ✅ | ❌ | ❌ |
| **Driver bind/unbind** | ✅ | ❌ | ❌ |
| **Descriptor okuma** | ✅ sysfs | ❌ | ⚠️ Cache'ten |
| **String okuma** | ✅ sysfs | ❌ | ⚠️ Cache'ten |
| **Configuration/interface** | ✅ | ❌ | ❌ |
| **Speed/state** | ✅ | ❌ | ⚠️ Kısmi |
| **Power info** | ✅ | ❌ | ❌ |
| **Port info** | ✅ | ❌ | ❌ |
| **Hub ports** | ✅ | ❌ | ❌ |
| **Error stats** | ✅ | ❌ | ❌ |
| **Health check** | ✅ | ❌ | ❌ |
| **Hotplug event** | ✅ uevent | ⚠️ Olay bus yok | ❌ |
| **Event bus** | ✅ | ❌ | ❌ |
| **Eject** | ✅ power_cycle | ✅ PowerShell | ❌ |

**Not:** Windows'ta `find_*` fonksiyonları çalışır, ama `usb` sınıfının çoğu metodu `NotSupported` döner — sadece `eject_device()` var. Android'de her şey read-only — enumeration yapabilirsin ama cihazı kontrol edemezsin.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Enumeration** | VID/PID/class/driver/predicate ile bul |
| **`enrich()`** | Root hub'ları otomatik Hub olarak işaretler |
| **Authorization** | USBGuard tarzı cihaz onay/red |
| **Reset** | `USBDEVFS_RESET` ioctl |
| **Power cycle** | unbind/bind ile yazılımsal güç kes |
| **Runtime PM** | auto-suspend, remote wakeup, delay |
| **Driver bind/unbind** | Herhangi bir interface'in driver'ını değiştir |
| **Descriptor okuma** | VID/PID/class/bcd/serial idx hepsi |
| **Configurations** | Interface listesi + driver + endpoints |
| **Hub ports** | Boş/dolu portlar + hız + link state |
| **Error stats** | Port reset, connect error, overcurrent |
| **Health check** | Skor (0-100) + uyarı listesi |
| **Hotplug** | `uevent` ile `wait_for_event` / `wait_for_action` |
| **Event bus** | UsbGuard tarzı daemon desteği |
| **Windows eject** | PowerShell `Disable-PnpDevice`/`Enable-PnpDevice` |
| **Kernel layout toleransı** | 5.x'ten 7.x'e kadar otomatik |

---

## 🚀 Hızlı Başlangıç

### Basit: Tüm USB cihazlarını listele

```cpp
#include "KernelNucleusT.hpp"

auto all = knst_usbc::find_all([](const knst_usb_info& u) { return true; });
for (auto& u : all) {
    std::cout << u.vid << ":" << u.pid
              << "  " << u.product_name
              << "  (" << u.device_num << ")\n";
}
```

### VID/PID ile bul

```cpp
auto r = knst_usbc::find(0x046D, 0xC52B);   // Logitech Unifying Receiver
for (auto& u : r) {
    std::cout << "Bulundu: " << u.product_name << "\n";
}
```

### Reset

```cpp
auto r = knst_usbc::usb::reset(u);
if (!r) std::cerr << "Reset başarısız\n";
```

---

## 📋 Genel API

| Kategori | Metotlar |
|---|---|
| **Enumeration** | `find`, `find_by_vid_pid`, `find_by_class`, `find_by_driver`, `find_hubs`, `find_all`, `enrich` |
| **Cihaz kontrol** | `usb::set_authorized`, `set_power_control`, `set_autosuspend_delay_ms`, `set_remote_wakeup` |
| **Reset/Recovery** | `usb::reset`, `usb::power_cycle` |
| **Driver** | `usb::bind_driver`, `usb::unbind_driver` |
| **Descriptor** | `usb::descriptors`, `usb::strings`, `usb::configurations` |
| **Durum** | `usb::speed`, `usb::state`, `usb::power_info` |
| **Port** | `usb::port_info`, `usb::hub_ports` |
| **Teşhis** | `usb::error_stats`, `usb::health_check` |
| **Hotplug** | `usb::wait_for_event`, `usb::wait_for_action` |
| **Event bus** | `usb_event_bus::subscribe`, `run`, `stop` |
| **Windows** | `usb::eject_device` (PowerShell) |

---

## 🔍 1) Enumeration

Cihaz bulma fonksiyonları hepsi **`knst_devices::usb()`** üzerinden çalışır — yani `knst_dev` modülünün USB listesini kullanıyor. `enrich()` otomatik olarak root hub'ları `Hub` class'ı ve `is_hub=true` olarak işaretliyor.

### `find(vid, pid = 0)`

```cpp
auto receivers = knst_usbc::find(0x046D);          // tüm Logitech cihazları
auto specific  = knst_usbc::find(0x046D, 0xC52B);   // belirli bir model
```

`pid = 0` verirsen **tüm VID'leri** döner.

### `find_by_vid_pid` — alias

```cpp
// find() ile aynı, sadece niyeti netleştirir
auto r = knst_usbc::find_by_vid_pid(0x0781, 0x5567);
```

### `find_by_class(class_code)`

```cpp
auto audio   = knst_usbc::find_by_class(0x01);   // Audio
auto hid     = knst_usbc::find_by_class(0x03);   // HID (klavye, mouse)
auto storage = knst_usbc::find_by_class(0x08);   // Mass Storage
auto hubs    = knst_usbc::find_by_class(0x09);   // Hub
auto video   = knst_usbc::find_by_class(0x0E);   // Video (webcam)
auto vendor  = knst_usbc::find_by_class(0xFF);   // Vendor-specific
```

**Kod içinde class mapping:**

| Kod | Enum |
|---|---|
| 0x01 | Audio |
| 0x02 | CDC |
| 0x03 | HID |
| 0x07 | Printer |
| 0x08 | MassStorage |
| 0x0E | Video |
| 0xE0 | Wireless |
| 0xFF | Vendor |

**Class 0x09 (Hub):** Kod önce `is_hub || is_root_hub` bakıyor — root hub'lar `usbN` formatında sysfs'te ama `bDeviceClass` **0x09** dönmüyor olabilir. Bu yüzden özel bir dal var.

**Composite cihaz fallback:** Eğer cihazın kendi class'ı uymuyorsa **interface'lerin** `bInterfaceClass`'ını tarar. Yani bir audio+hid composite cihaz hem Audio hem HID için döner. Bu **çok önemli** çünkü tek başına `bDeviceClass` genelde `0x00` (device class interface'lerde tanımlı) döner.

### `find_by_driver(driver_name)`

```cpp
auto usbhid   = knst_usbc::find_by_driver(u"usbhid");
auto uas      = knst_usbc::find_by_driver(u"uas");
auto usb_stor = knst_usbc::find_by_driver(u"usb-storage");

// Root hub'lar için özel:
auto hubs = knst_usbc::find_by_driver(u"hub");   // → find_hubs() ile aynı
```

**Nasıl çalışır?** Cihazın **herhangi bir interface'inde** bu driver varsa eşleşir. Yani composite bir cihazın bir interface'i `usbhid`, başka bir interface'i `usb-storage` ise iki driver için de döner.

**Root hub özel durumu:** `driver_name == u"hub"` verirsen kod doğrudan `find_hubs()`'a gider — çünkü root hub'ların sysfs symlink'i kernel versiyonuna göre değişebiliyor.

**Güvenlik:** `knst_dev::is_safe_token(driver_name, 32)` kontrolü var — `../` gibi path traversal engellenir.

### `find_hubs()`

```cpp
auto hubs = knst_usbc::find_hubs();
for (auto& h : hubs) {
    std::cout << (h.is_root_hub ? "Root hub: " : "Hub: ")
              << h.product_name << "\n";
}
```

`find_by_class(0x09)` ile aynı.

### `find_all(predicate)`

```cpp
// 5 Gbps veya üstü cihazlar
auto fast = knst_usbc::find_all([](const knst_usb_info& u) {
    return u.speed_mbps >= 5000;
});

// Belirli bir manufacturer
auto logitech = knst_usbc::find_all([](const knst_usb_info& u) {
    return u.manufacturer == u"Logitech";
});
```

Predicate `enrich()` edilmiş cihazı alır.

### `enrich(knst_usb_info)` — yardımcı

```cpp
knst_usb_info u = /* ... */;
u = knst_usbc::enrich(u);
// root hub ise usb_class = Hub, is_hub = true
```

**Neden gerekli?** `knst_devices::usb()` root hub'lar için `usb_class`'ı doğru doldurmuyor (kernel bazen 0x09 döner, bazen `Unknown`). `enrich()` bunu normalize eder.

---

## 🛡️ 2) Cihaz Kontrol — Authorization

USBGuard tarzı **onay/red** mekanizması. Deauthorize edilmiş cihaz **kernel tarafından hiç konuşulmaz** — yani tehlikeli bir USB takıldığında hemen reddedebilirsin.

```cpp
// Cihazı yetkilendir
knst_usbc::usb::set_authorized(u, true);

// Yetkiyi geri al (cihaz enumerated olur ama hiçbir driver yüklenmez)
knst_usbc::usb::set_authorized(u, false);
```

**Nasıl çalışır?** `/sys/bus/usb/devices/<name>/authorized` dosyasına `1` veya `0` yazar.

> ⚠️ `authorized=0` yaptığında cihaz **kernel'den düşer** gibi görünür ama USB hub'da fiziksel olarak takılı kalır. Tekrar `1` yazarsan cihaz yeniden enumerated olur. Bu, USBGuard'ın temel mekanizması.

---

## ⚡ 3) Runtime PM — Güç Yönetimi

```cpp
// Otomatik askıya almayı aç
knst_usbc::usb::set_power_control(u, true);   // "auto"

// Kapat (her zaman açık)
knst_usbc::usb::set_power_control(u, false);  // "on"

// Askıya alma gecikmesi (ms)
knst_usbc::usb::set_autosuspend_delay_ms(u, 2000);   // 2 saniye

// Remote wakeup
knst_usbc::usb::set_remote_wakeup(u, true);   // "enabled"
knst_usbc::usb::set_remote_wakeup(u, false);  // "disabled"
```

**Nasıl çalışır?**
- `set_power_control` → `/sys/bus/usb/devices/<name>/power/control` (auto/on)
- `set_autosuspend_delay_ms` → `power/autosuspend_delay_ms` (0-600000 arası, üstü `InvalidArgument`)
- `set_remote_wakeup` → `power/wakeup` (enabled/disabled)

> ⚠️ 600000 ms (10 dakika) üstü delay **`InvalidArgument`** döner. Kernel limitini aşmayasın.

---

## 🔄 4) Reset ve Power Cycle

### `reset(u)` — USB port reset

```cpp
auto r = knst_usbc::usb::reset(u);
if (!r) {
    std::cerr << "Reset hatası\n";
    // Linux'ta genelde EPERM çıkar → root/sudo gerekiyor
}
```

**Nasıl çalışır?** `/dev/bus/usb/<bus>/<devnum>` cihazına `USBDEVFS_RESET` ioctl atar. Kernel cihazı enumerated olarak yeniden başlatır.

> ⚠️ **Root/sudo gerekli.** Normal kullanıcı olarak çalıştırırsan `PermissionDenied` (`EPERM`) alırsın. USB cihaz reset'lemek için `CAP_SYS_ADMIN` veya udev kuralı gerekir.

### `power_cycle(u, delay_ms = 500)` — Yazılımsal güç kes

```cpp
auto r = knst_usbc::usb::power_cycle(u, 1000);   // 1 saniye güç kes
```

**Nasıl çalışır?**
1. `/sys/bus/usb/drivers/usb/unbind`'e cihaz adı yaz → cihaz "elektriksel olarak" çıkar
2. `delay_ms` kadar bekle (max 5000 ms — fazlası 5000'e çekilir)
3. `/sys/bus/usb/drivers/usb/bind`'e yaz → cihaz yeniden enumerated olur

**Reset'ten farkı:** `reset()` sadece port'u sıfırlar, `power_cycle()` driver'ı tamamen söker — daha kuvvetli. Takılı kalmış bir cihazı kurtarmak için `power_cycle` daha güvenilir.

> ⚠️ **Delay max 5000 ms.** Daha fazla istiyorsan kod otomatik 5000'e çekiyor.

---

## 🔌 5) Driver Bind/Unbind

```cpp
// Cihazı USB driver'ından tamamen ayır
knst_usbc::usb::unbind_driver(u);

// Belirli bir driver'a bağla
knst_usbc::usb::bind_driver(u, u"usbhid");

// Örnek: bir HID cihazını usbhid'den kurtarıp kendi driver'ına bağla
knst_usbc::usb::unbind_driver(u);
knst_usbc::usb::bind_driver(u, u"my_custom_driver");
```

**Nasıl çalışır?**
- `unbind_driver` → `/sys/bus/usb/drivers/usb/unbind`
- `bind_driver` → `/sys/bus/usb/drivers/<driver_name>/bind`

**Güvenlik:** `driver_name` `is_safe_token(_, 32)` ile kontrol edilir — `../` yok.

> ⚠️ Yanlış driver'a bind etmek cihazı **kullanılamaz** hale getirebilir. Test ederken dikkat.

> ⚠️ Önce `unbind`, sonra `bind` yapmalısın. Sadece `bind` çağırırsan cihaz zaten bağlıysa `IoError` alabilirsin.

---

## 📋 6) Descriptor ve String Okuma

### `descriptors(u)` — USB device descriptor

```cpp
auto r = knst_usbc::usb::descriptors(u);
if (r) {
    auto& d = r.value;
    std::cout << "bcdUSB     : 0x" << std::hex << d.bcd_usb << "\n";
    std::cout << "VID:PID    : " << d.id_vendor << ":" << d.id_product << "\n";
    std::cout << "bcdDevice  : 0x" << d.bcd_device << "\n";
    std::cout << "MaxPacket0 : " << (int)d.max_packet_size0 << "\n";
    std::cout << "Class      : 0x" << (int)d.device_class << "\n";
    std::cout << "NumConfig  : " << (int)d.num_configurations << "\n";
}
```

**Ne okur?** sysfs'ten şu dosyalar:
- `version` → `bcd_usb` (BCD'e çevrilir: "3.10" → 0x0310)
- `bDeviceClass` / `bDeviceSubClass` / `bDeviceProtocol` → **hex string** olarak okunur
- `bMaxPacketSize0` → decimal
- `bcdDevice` → hex
- `bNumConfigurations` → decimal

**VID/PID `usb_device_descriptor`'a `u.vid`/`u.pid`'den yazılır** — sysfs'te ayrıca yok.

### `strings(u)` — Manufacturer/Product/Serial

```cpp
auto r = knst_usbc::usb::strings(u);
if (r) {
    std::cout << "Manufacturer: " << r.value.manufacturer << "\n";
    std::cout << "Product     : " << r.value.product << "\n";
    std::cout << "Serial      : " << r.value.serial << "\n";
}
```

sysfs'teki `manufacturer`, `product`, `serial` dosyalarından okur. **Boş olabilir** — bazı cihazlar bunları sağlamaz.

### `configurations(u)` — Interface listesi

```cpp
auto r = knst_usbc::usb::configurations(u);
if (r) {
    for (auto& cfg : r.value) {
        std::cout << "Configuration #" << (int)cfg.number
                  << "  Power: " << cfg.max_power_ma << " mA\n";
        for (auto& iface : cfg.interfaces) {
            std::cout << "  Interface " << iface.number
                      << "  class=0x" << std::hex << iface.class_code
                      << "  driver=" << iface.driver << "\n";
        }
    }
}
```

**Ne okur?**
- `bConfigurationValue`, `bMaxPower` (device'tan)
- `1-2:1.0`, `1-2:1.1` gibi tüm interface klasörlerinden: `bInterfaceNumber`, `bInterfaceClass`, `bInterfaceSubClass`, `bInterfaceProtocol`, `driver` symlink

> ⚠️ **`bMaxPower` zaten mA.** Kernel USB descriptor'daki 2mA birimini çarpıp sysfs'e yazıyor. **Kod da tekrar çarpmıyor** (yorum satırı var koda: "sysfs bMaxPower already reports mA; do NOT multiply again").

---

## 📊 7) Speed ve State

### `speed(u)` — USB hızı

```cpp
auto r = knst_usbc::usb::speed(u);
if (r) {
    switch (r.value) {
        case knst_usbc::usb_speed::Low:       std::cout << "1.5 Mbps\n"; break;
        case knst_usbc::usb_speed::Full:      std::cout << "12 Mbps\n"; break;
        case knst_usbc::usb_speed::High:      std::cout << "480 Mbps\n"; break;
        case knst_usbc::usb_speed::Super:     std::cout << "5 Gbps\n"; break;
        case knst_usbc::usb_speed::SuperPlus: std::cout << "10+ Gbps\n"; break;
        default: std::cout << "Unknown\n";
    }
}
```

`speed` dosyasından Mbps değerini okur, enum'a çevirir. "unknown" string görürse `Unknown` döner (bazı hub'lar böyle).

### `state(u)` — Cihaz durumu

```cpp
auto r = knst_usbc::usb::state(u);
// Unauthorized, Configured, Attached dönebilir
```

**Basit mantık:**
1. `authorized == 0` → `Unauthorized`
2. `bConfigurationValue > 0` → `Configured`
3. Aksi halde → `Attached`

> ⚠️ `usb_state` enum'unda `Default`, `Addressed`, `Suspended` gibi değerler var ama **kod bunları doldurmuyor**. Sadece 3 tanesi gerçekte kullanılıyor.

---

## 💾 8) Power Info

```cpp
auto r = knst_usbc::usb::power_info(u);
if (r) {
    auto& p = r.value;
    std::cout << "Max power  : " << p.max_power_ma << " mA\n";
    std::cout << "Control    : " << p.control_mode << "\n";       // "auto" / "on"
    std::cout << "Wakeup     : " << p.has_remote_wakeup << "\n";
    std::cout << "Autosuspend: " << p.autosuspend_delay_ms << " ms\n";
    std::cout << "Suspended  : " << p.is_suspended << "\n";
}
```

**Ne okur?**
- `bMaxPower` → `max_power_ma` (zaten mA)
- `power/control` → `control_mode` string
- `power/wakeup` → `has_remote_wakeup` (enabled ise true)
- `power/autosuspend_delay_ms` → decimal
- `power/runtime_status` → `is_suspended` (suspended ise true)

---

## 🔗 9) Port Info ve Hub Ports

### `port_info(u)` — Tek cihaz için

```cpp
auto r = knst_usbc::usb::port_info(u);
```

> ⚠️ **Bu metot genelde boş döner.** Sysfs'te `port/*` attribute'ları **port device**'larında (`usb3-port1`, `1-4-port2`) yaşar, cihazın kendisinde değil. `hub_ports()` kullan.

### `hub_ports(hub)` — Hub'ın tüm portları

```cpp
auto r = knst_usbc::usb::hub_ports(hub);
if (r) {
    for (auto& p : r.value) {
        std::cout << "Port " << p.number
                  << " [" << p.port_name << "]"
                  << "  " << (p.has_device ? "dolu" : "boş");
        if (p.has_device) {
            std::cout << "  cihaz: " << p.device_name
                      << " (" << p.device_product << ")";
        }
        std::cout << "  link=" << p.link_state
                  << "  speed=" << p.port_speed_mbps << " Mbps\n";
    }
}
```

**Ne yapar? (3 aşamalı)**

**1) Dolu portları bul:** Tüm USB cihazlarını gezer, bu hub'ın **doğrudan çocukları** olanları toplar.
- Root hub (`usb3`) ise: `3-N` formatı, `3-N.M` **reddedilir** (grandchild)
- Normal hub (`1-2`) ise: `1-2.P` formatı, `1-2.P.Q` **reddedilir** (grandchild)

**2) Boş portları ekle:** Hub'ın `maxchild` değerini okur, 1'den `maxchild`'a kadar eksik portları `has_device=false` ile ekler (max 64).

**3) Sırala:** Port numarasına göre.

**Her port için ne okur?**
- `port/connect_type` → `is_connectable` (hotplug veya hardwired)
- `port/disable` → `is_enabled` (0 = enabled)
- `port/link_state` → string
- `port/speed` → `port_speed_mbps` (sayı)

> ⚠️ Eski kernel'lerde `<hub>-portN` **gerçek device** olarak var, yeni kernel'lerde olmayabilir. `hub_ports()` her iki durumu da tolere eder — port device yoksa sentetik isimle dener, hata olursa `read_port_attrs` sessizce atlar.

> ⚠️ **Duplicate filtresi:** Çoklu-interface cihazlar (`1-2.1` + `1-2:1.0` + `1-2:1.1`) tek port olarak gösterilir.

---

## 📉 10) Error Stats ve Health Check

### `error_stats(u)` — Port hata sayaçları

```cpp
auto r = knst_usbc::usb::error_stats(u);
if (r) {
    std::cout << "Port resets     : " << r.value.port_resets << "\n";
    std::cout << "Connect errors  : " << r.value.connect_errors << "\n";
}
```

**Not:** `usb_error_stats` struct'ında 7 alan var (`port_resets`, `disconnects`, `connect_errors`, `overcurrents`, `babble_errors`, `timeout_errors`, `crc_errors`) ama **kod sadece 2 tanesini dolduruyor**: `port/reset_count` ve `port/connect_errors`. Diğerleri sysfs'te yaygın değil.

### `health_check(u)` — Skor + uyarılar

```cpp
auto r = knst_usbc::usb::health_check(u);
if (r) {
    auto& h = r.value;
    std::cout << "Healthy : " << h.healthy << "\n";
    std::cout << "Score   : " << h.score << " / 100\n";
    for (auto& w : h.warnings) {
        std::cout << "  ⚠ " << w << "\n";
    }
}
```

**Skor mantığı (100'den başlar):**
- `authorized == 0` → **-20** ("Device is unauthorized")
- `port/connect_errors > 0` → **-15** ("Port connect errors detected")
- `port/reset_count > 5` → **-10** ("Frequent port resets")

Sonuç 0'ın altına inmez. **`healthy` = skor ≥ 70**.

> ⚠️ Bu basit bir sezgisel — kesin teşhis için `dmesg` + `usbmon` kullan.

---

## 📡 11) Hot-plug — `wait_for_event` / `wait_for_action`

```cpp
// Herhangi bir USB olayı bekle (60 saniye timeout)
auto r = knst_usbc::usb::wait_for_event(60000);
if (r) {
    std::cout << "Olay: " << r.value.action
              << "  device: " << r.value.devpath << "\n";
}

// Belirli bir action bekle
auto r2 = knst_usbc::usb::wait_for_action(60000, u"add");
```

**Nasıl çalışır?**
1. `knst_dev::uevent_monitor::open()` ile netlink socket açar
2. `"usb"` subsystem'ini dinler
3. `devtype == "usb_device"` olan olayları filtreler
4. Timeout'a kadar bekler

**Action değerleri:** `"add"`, `"remove"`, `"change"`, `"bind"`, `"unbind"`

> ⚠️ `wait_for_action` **her action'ı ayrı ayrı** beklemek için yeniden çağrılmalı. `Timeout` dönerse hiç olay gelmemiştir.

> ⚠️ `usb_event_bus::run()` **loop içinde** çalışır — bir kere çağırırsan `stop()` çağırana kadar dinler.

---

## 📢 12) Event Bus — UsbGuard Tarzı Daemon

```cpp
class knst_usbc::usb_event_bus {
public:
    using handler = knst_function_shared<const knst_dev::uevent&>;

    uint32_t subscribe(handler h);           // handler'ın ID'sini döner
    void unsubscribe(uint32_t id);
    result<void> run(uint32_t timeout_ms = 0);  // blocking loop
    void stop();                              // döngüyü durdur
};
```

### Kullanım

```cpp
knst_usbc::usb_event_bus bus;

auto id = bus.subscribe([](const knst_dev::uevent& ev) {
    if (ev.action == u"add") {
        std::cout << "Yeni cihaz: " << ev.devpath << "\n";
    } else if (ev.action == u"remove") {
        std::cout << "Cihaz çıktı: " << ev.devpath << "\n";
    }
});

// Blocking loop — stop() çağırılana kadar dinler
std::thread t([&] { bus.run(); });

// ... bir süre sonra ...
bus.stop();
t.join();
```

**Thread-safe mi?** `subscribe`/`unsubscribe` **mutex'li**. `run()` loop içindeki handler çağrıları **mutex dışında** — yani handler uzun iş yaparsa yeni subscribe'lar bloklanmaz.

**Handler hata durumu:** `try { h(ev); } catch (...) {}` — bir handler throw ederse diğerleri etkilenmez. **Ama exception bilgisi kaybolur** — loglamak istiyorsan handler içinde try/catch kullan.

**`timeout_ms = 0`:** `run()` içinde 60 saniyelik `wait` kullanır — yani her 60 saniyede bir uevent monitor'a döner.

> ⚠️ **Windows ve Android'de `usb_event_bus` çalışmaz.** `subscribe` her zaman 0 döner, `run` `NotSupported` verir.

---

## 🪟 13) Windows — Enumeration + Eject

### Çalışan fonksiyonlar

- `find(vid, pid)`, `find_by_vid_pid(vid, pid)`
- `find_by_class(class_code)`
- `find_by_driver(driver_name)` — PowerShell `Get-PnpDevice` üzerinden
- `find_hubs()`
- `find_all(predicate)`

### Çalışmayanlar

`knst_usbc::usb` sınıfının neredeyse hepsi `NotSupported` döner. **İstisna: `eject_device`.**

### `eject_device(u, delay_ms = 500)`

```cpp
auto r = knst_usbc::usb::eject_device(u, 1000);
if (!r) {
    // status::PermissionDenied → admin değilsin
    // status::NotFound → cihaz takılı değil
    // status::ToolMissing → PowerShell bulunamadı
    std::cerr << "Eject başarısız\n";
}
```

**Nasıl çalışır?** PowerShell'de şu script:
```powershell
$dev = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_XXXX&PID_YYYY*' } | Select-Object -First 1
if (-not $dev) { Write-Output 'NOT_FOUND'; exit 1 }
Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction Stop
Start-Sleep -Milliseconds <delay>
Enable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction Stop
Write-Output 'OK'
```

**Sırasıyla:**
1. Cihazı VID/PID ile bul
2. `Disable-PnpDevice` → cihazı devre dışı bırak (sanki çıkmış gibi)
3. `delay_ms` bekle
4. `Enable-PnpDevice` → yeniden etkinleştir

> ⚠️ **Admin/User Administrator gerekli.** `Disable-PnpDevice` normal kullanıcı olarak çalışmaz → `PermissionDenied`.

> ⚠️ **PowerShell gerekli.** `powershell.exe` yolunu otomatik bulur. Bulamazsa `ToolMissing`.

> ⚠️ **15 saniye timeout.** Script uzun sürebilir, 15 sn sonra `Timeout` döner.

---

## 🤖 14) Android — Sadece Enumeration

Android'de USB stack'i aşırı kısıtlı:

- ✅ **Enumeration:** `UsbManager.getDeviceList` JNI ile
- ✅ **VID/PID/class/manufacturer/product/serial:** Cache'ten
- ⚠️ **Descriptor:** Sadece cache'ten kısmi
- ❌ **Reset, power cycle, bind/unbind:** SELinux engeller
- ❌ **Hub topology:** Gizli
- ❌ **Hotplug event:** BroadcastReceiver + izin gerekli (ayrı API)
- ❌ **Transfer speed:** UsbManager açığa çıkarmıyor

**Pratikte:** `find()`, `find_by_class()`, `find_all()` çalışır. `usb::*` metodlarının hepsi `NotSupported`.

**`descriptors()`:** Sadece cached `u.usb_version` varsa çalışır, `bcd_usb` hesaplanır. Diğer alanlar sıfır kalır.

**`strings()`:** Cache'ten (`u.manufacturer`, `u.product_name`, `u.serial`).

**`state()`:** Her zaman `Configured` döner (UsbDevice'nın state'i zaten configured).

---

## ⚠️ 15) Sık Tuzaklar

### 1) `find_by_class` composite cihazları iki kere gösterebilir

Audio+HID bir webcam `find_by_class(0x01)` ve `find_by_class(0x03)` ikisinde de döner. **Bu doğru davranış** — çünkü aynı cihazın farklı interface'leri.

### 2) `hub_ports()` eski kernel'de `port/*` okuyamaz

`<hub>-portN` device'ı yoksa sentetik isim dener, hata olursa sessizce geçer. `has_device` ve `device_name` yine doğru dolu olur (device'tan türetilir).

### 3) Grandchild reject

`1-2.3` **child of `1-2`** kabul edilir. Ama `1-2.3.4` **grandchild** — `1-2`'nin doğrudan child'ı değil. `hub_ports(1-2)` sadece `1-2.1`, `1-2.2`, `1-2.3` döner — `1-2.3.4` **filtrelenir**.

### 4) `bMaxPower` çarpılmaz

Kernel `bMaxPower` descriptor birimini (2 mA) çarpıp sysfs'e **zaten mA olarak** yazıyor. Kod da **tekrar çarpmıyor** — yorum satırı var.

### 5) `reset()` root gerekli

Normal kullanıcı `EPERM` alır. sudo veya udev kuralı gerek.

### 6) `power_cycle()` delay max 5000 ms

Fazlası otomatik 5000'e çekilir.

### 7) `set_autosuspend_delay_ms()` 600000 ms üstü `InvalidArgument`

Kernel limitini aşmayasın.

### 8) Windows `eject_device()` admin gerekli

`Disable-PnpDevice` normal kullanıcı olarak `PermissionDenied`.

### 9) `usb_event_bus::subscribe` Windows/Android'de 0 döner

`NotSupported` değil, sessizce 0 — kontrol etmen gerek.

### 10) `state()` sadece 3 durum döner

`Unauthorized`, `Configured`, `Attached`. Diğer enum değerleri (`Default`, `Addressed`, `Suspended`) **hiç dönmez** — kod doldurmuyor.

### 11) `error_stats()` sadece 2 alan doldurulur

`usb_error_stats` struct'ı 7 alan içerir ama sadece `port_resets` ve `connect_errors` doldurulur. Diğerleri 0 kalır.

### 12) `hub_ports()` max 64 port

Hub `maxchild > 64` dönerse bile kod 64'te keser — sanal hub'lar için koruma.

### 13) Container'da `/sys/bus/usb/devices` yoksa boş döner

Docker/Flatpak/Snap gibi ortamlarda sysfs mount edilmemişse `find_*` boş liste verir. Bu bir hata değil, tasarım.

### 14) `unbind_driver` + `bind_driver` sıra önemli

Önce `unbind`, sonra `bind`. Sadece `bind` çağırırsan cihaz zaten bağlıysa `IoError`.

### 15) `eject_device` 15 saniye timeout

PowerShell yavaş çalışırsa `Timeout` alabilirsin. `delay_ms` değerini küçültmeyi dene.

---

## 📊 16) Performans Notları

| İşlem | Maliyet | Not |
|---|---|---|
| `find(vid, pid)` | O(N) | N = toplam USB cihaz sayısı |
| `find_by_class()` | O(N × I) | I = ortalama interface sayısı |
| `find_by_driver()` | O(N × I) | Her interface için sysfs symlink okur |
| `descriptors()` | O(1) | 6-7 sysfs dosyası |
| `configurations()` | O(N)` | Tüm interface klasörlerini gezer |
| `hub_ports()` | O(N × P) | N = cihaz, P = port (fallback) |
| `error_stats()` | O(1) | 2 sysfs dosyası |
| `health_check()` | O(1) | 3 sysfs okuma |
| `reset()` | O(1) + kernel işi | ioctl |
| `power_cycle()` | O(1) + delay | unbind/bind |
| `eject_device()` (Win) | **~1-3 saniye** | PowerShell spawn + sleep |

**İpuçları:**
- Sık `find_by_class()` çağırıyorsan **sonucu cache'le** — sysfs her seferinde taranıyor
- `find_all()` predicate'i **hafif tut** — her cihaz için çağrılıyor
- `hub_ports()` **pahalı** — bir hub için tüm USB cihazlarını gezer. Loop içinde çağırma
- Windows `eject_device()` **yavaş** — her cihaz için ayrı PowerShell spawn

---

## 🌍 17) Platform Farkları — Özet

| Konu | Linux | Windows | Android |
|---|---|---|---|
| **Backend** | sysfs + ioctl | PowerShell PnP | JNI UsbManager |
| **Root gerekli** | Reset/power cycle için | Eject için | — |
| **Enumeration** | ✅ Tam | ✅ Tam | ✅ Tam |
| **Kontrol** | ✅ Tam | ⚠️ Sadece eject | ❌ Yok |
| **Hotplug** | ✅ netlink uevent | ❌ | ❌ |
| **Event bus** | ✅ | ❌ | ❌ |
| **Sysfs yok** | Boş sonuç | N/A | N/A |

---

## 🎯 18) Ne Zaman Kullanmalı?

### ✅ Kullan

- **USBGuard tarzı güvenlik** — yeni cihazları authorize/deauthorize et
- **Kamera/mikrofon kontrolü** — class 0x0E/0x01 ile bul, disable et
- **Storage yönetimi** — USB disk takıldığında otomatik mount/unmount tetikle
- **Hotplug daemon** — `usb_event_bus` ile olaylara tepki ver
- **Debug tool** — cihaz bilgilerini detaylı listele (descriptor, config, strings)
- **Kiosk uygulaması** — yetkisiz USB takıldığında uyarı ver
- **Test otomasyonu** — cihazı reset/power_cycle ile yeniden başlat
- **Hub monitoring** — hangi portta ne var, link state ne
- **Windows eject** — PowerShell ile güvenli çıkarma

### ❌ Kullanma

- **USB transfer yapmak** (bulk/interrupt/control) — bu kütüphane sadece kontrol, data transfer için `libusb` gerek
- **USB device mode (gadget)** — bu host tarafı
- **Android'de cihaz kontrol** — SELinux izin vermez, sadece enumeration
- **Windows'ta reset/power cycle** — `NotSupported`, eject kullan veya ayrı araç
- **Container'da USB çalıştırmak** — `/sys` mount edilmemişse boş sonuç

---

## ✅ 19) Yap / ❌ Yapma

### ✅ Yap

```cpp
// 1. Root hub'ları Hub olarak işaretle (enrich otomatik yapar ama manuel de yapabilirsin)
auto u = knst_usbc::enrich(raw_info);

// 2. Class 0x09 için is_hub bak (bDeviceClass güvenilmez)
if (u.is_hub || u.is_root_hub) { /* ... */ }

// 3. Composite cihazlarda find_by_class interface fallback'ini bil
// (audio+hid webcam iki class'ta da döner)

// 4. Reset öncesi root kontrolü yap
if (geteuid() != 0) std::cerr << "sudo gerekli\n";

// 5. Driver değiştirirken önce unbind sonra bind
knst_usbc::usb::unbind_driver(u);
knst_usbc::usb::bind_driver(u, u"my_driver");

// 6. Hub ports için tüm listeyi bir kere al, cache'le
auto ports = knst_usbc::usb::hub_ports(hub);
for (auto& p : ports.value) { /* ... */ }

// 7. Event bus handler'ında exception yakala
bus.subscribe([](const knst_dev::uevent& ev) {
    try { /* ... */ } catch (...) { /* log */ }
});

// 8. Windows eject için admin ol
// (manifest'te requireAdministrator veya UAC ile)
```

### ❌ Yapma

```cpp
// 1. hub_ports()'u sıcak loop'ta çağırma
for (int i = 0; i < 100; ++i) {
    auto r = knst_usbc::usb::hub_ports(hub);   // ❌ her seferinde tüm USB taraması
}

// 2. bMaxPower'ı çarpma (zaten mA)
uint32_t wrong = r.value.max_power_ma * 2;   // ❌ kernel zaten çarpmış

// 3. set_autosuspend_delay_ms'ye 600000'den büyük değer verme
knst_usbc::usb::set_autosuspend_delay_ms(u, 1000000);   // ❌ InvalidArgument

// 4. power_cycle delay'ini 5000'den büyük verme
knst_usbc::usb::power_cycle(u, 10000);   // ⚠️ otomatik 5000'e çekilir

// 5. reset'i root olmadan çağırma (EPERM)
knst_usbc::usb::reset(u);   // ❌ PermissionDenied

// 6. state() enum'unun tüm değerlerini bekleyeme
auto s = knst_usbc::usb::state(u);   // sadece Unauthorized/Configured/Attached
// (Default, Addressed, Suspended hiç dönmez)

// 7. error_stats() tüm alanlarının dolu olduğunu bekleme
// (sadece port_resets ve connect_errors dolu)

// 8. Windows'ta reset/power_cycle bekleme
knst_usbc::usb::reset(u);   // ❌ NotSupported

// 9. Container'da sysfs olmadan enumeration bekleme
// /sys mount edilmemişse boş liste döner

// 10. Android'de cihaz kontrolü bekleme
knst_usbc::usb::set_authorized(u, false);   // ❌ NotSupported
```

---

## 🔥 20) Tam Örnek — USBGuard Tarzı Daemon

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    knst_usbc::usb_event_bus bus;

    // Yeni cihaz takıldığında kontrol et
    bus.subscribe([](const knst_dev::uevent& ev) {
        if (ev.action != u"add") return;

        // device path'ten cihazı bul
        auto matches = knst_usbc::find_all([](const knst_usb_info& u) {
            return u.valid;
        });

        for (auto& u : matches) {
            // HID klavye/mouse ise izin ver
            if (u.usb_class == knst_usb_class::HID) {
                std::cout << "✅ HID cihaz: " << u.product_name << "\n";
                continue;
            }

            // Mass storage ise deauthorize et
            if (u.usb_class == knst_usb_class::MassStorage) {
                std::cout << "⚠️  Storage cihazı engellendi: "
                          << u.product_name << "\n";
                knst_usbc::usb::set_authorized(u, false);
            }
        }
    });

    // Blocking loop
    bus.run();
    return 0;
}
```

### Örnek 2: Health Dashboard

```cpp
void usb_health_report() {
    auto all = knst_usbc::find_all([](const knst_usb_info& u) { return u.valid; });

    for (auto& u : all) {
        auto h = knst_usbc::usb::health_check(u);
        if (!h || h.value.healthy) continue;

        std::cout << "⚠️  " << u.product_name
                  << "  score=" << h.value.score << "/100\n";
        for (auto& w : h.value.warnings) {
            std::cout << "    " << w << "\n";
        }

        // Otomatik reset
        if (h.value.score < 50.0) {
            std::cout << "    → reset deneniyor...\n";
            knst_usbc::usb::reset(u);
        }
    }
}
```

### Örnek 3: Hub Port Haritası

```cpp
void dump_hub(hub_index) {
    auto hubs = knst_usbc::find_hubs();
    if (hub_index >= hubs.size()) return;

    auto& hub = hubs[hub_index];
    std::cout << "Hub: " << hub.product_name
              << " (" << (hub.is_root_hub ? "root" : "external") << ")\n";

    auto ports = knst_usbc::usb::hub_ports(hub);
    if (!ports) return;

    for (auto& p : ports.value) {
        std::cout << "  Port " << p.number << "  ";
        if (p.has_device) {
            std::cout << "▶ " << p.device_product
                      << " (" << p.device_name << ")";
        } else {
            std::cout << "boş";
        }
        if (!p.link_state.empty()) {
            std::cout << "  [" << p.link_state << "]";
        }
        if (p.port_speed_mbps > 0) {
            std::cout << "  " << p.port_speed_mbps << " Mbps";
        }
        std::cout << "\n";
    }
}
```

---

## 📌 21) API Kontrat Özeti

```cpp
namespace knst_usbc {

enum class usb_speed : uint8_t { Unknown, Low, Full, High, Super, SuperPlus };
enum class usb_state : uint8_t { Unknown, NotAttached, Attached, Powered,
    Reconnecting, Unauthorized, Default, Addressed, Configured, Suspended };

struct usb_endpoint_info { /* address, number, is_in, transfer_type, ... */ };
struct usb_interface_info { /* number, class_code, subclass, protocol, driver, ... */ };
struct usb_config_info { /* number, attributes, max_power_ma, interfaces, ... */ };
struct usb_device_descriptor { /* bcd_usb, id_vendor, id_product, bcd_device, ... */ };
struct usb_strings { /* manufacturer, product, serial, configuration */ };
struct usb_port_info { /* number, is_connectable, port_speed_mbps, link_state, ... */ };
struct usb_error_stats { /* port_resets, disconnects, connect_errors, ... */ };
struct usb_health_snapshot { /* healthy, score, warnings */ };
struct usb_power_info { /* max_power_ma, control_mode, ... */ };

// Enumeration
inline knst_vector<knst_usb_info> find(uint16_t vid, uint16_t pid = 0) noexcept;
inline knst_vector<knst_usb_info> find_by_vid_pid(uint16_t vid, uint16_t pid = 0) noexcept;
inline knst_vector<knst_usb_info> find_by_class(uint8_t class_code) noexcept;
inline knst_vector<knst_usb_info> find_by_driver(const knst_c16string& driver_name) noexcept;
inline knst_vector<knst_usb_info> find_hubs() noexcept;
template<typename Predicate>
inline knst_vector<knst_usb_info> find_all(Predicate&& pred) noexcept;

inline knst_usb_info enrich(knst_usb_info u) noexcept;

// Kontrol
class usb {
public:
    // Authorization
    static result<void> set_authorized(const knst_usb_info&, bool allow) noexcept;

    // Runtime PM
    static result<void> set_power_control(const knst_usb_info&, bool auto_suspend) noexcept;
    static result<void> set_autosuspend_delay_ms(const knst_usb_info&, uint32_t ms) noexcept;
    static result<void> set_remote_wakeup(const knst_usb_info&, bool enable) noexcept;

    // Reset
    static result<void> reset(const knst_usb_info&) noexcept;
    static result<void> power_cycle(const knst_usb_info&, uint32_t delay_ms = 500) noexcept;

    // Driver
    static result<void> bind_driver(const knst_usb_info&, const knst_c16string&) noexcept;
    static result<void> unbind_driver(const knst_usb_info&) noexcept;

    // Descriptor
    static result<usb_device_descriptor> descriptors(const knst_usb_info&) noexcept;
    static result<usb_strings> strings(const knst_usb_info&) noexcept;
    static result<knst_vector<usb_config_info>> configurations(const knst_usb_info&) noexcept;

    // Durum
    static result<usb_speed> speed(const knst_usb_info&) noexcept;
    static result<usb_state> state(const knst_usb_info&) noexcept;
    static result<usb_power_info> power_info(const knst_usb_info&) noexcept;

    // Port
    static result<usb_port_info> port_info(const knst_usb_info&) noexcept;
    static result<knst_vector<usb_port_info>> hub_ports(const knst_usb_info&) noexcept;

    // Teşhis
    static result<usb_error_stats> error_stats(const knst_usb_info&) noexcept;
    static result<usb_health_snapshot> health_check(const knst_usb_info&) noexcept;

    // Hotplug
    static result<knst_dev::uevent> wait_for_event(uint32_t timeout_ms) noexcept;
    static result<knst_dev::uevent> wait_for_action(uint32_t timeout_ms, const char16_t*) noexcept;

    // Windows only
    static result<void> eject_device(const knst_usb_info&, uint32_t delay_ms = 500) noexcept;
};

class usb_event_bus {
public:
    using handler = knst_function_shared<const knst_dev::uevent&>;
    uint32_t subscribe(handler h) noexcept;
    void unsubscribe(uint32_t id) noexcept;
    result<void> run(uint32_t timeout_ms = 0) noexcept;
    void stop() noexcept;
};

} // namespace knst_usbc
```

---

## 🎯 Size Getireceği Kazanç

`knst_usbc` şu durumlarda ciddi avantaj sağlar:

- ✅ **USBGuard tarzı güvenlik** — yeni cihazları otomatik onayla/reddet
- ✅ **Kamera/mikrofon kontrolü** — class-based bulma + authorize
- ✅ **Storage yönetimi** — takıldığında mount tetikle
- ✅ **Hotplug daemon** — `usb_event_bus` ile olaylara tepki ver
- ✅ **Debug tool** — descriptor + config + strings detaylı
- ✅ **Kiosk uygulaması** — yetkisiz USB'de uyarı + deauthorize
- ✅ **Test otomasyonu** — reset/power_cycle ile cihazı yeniden başlat
- ✅ **Hub monitoring** — port haritası + link state + hız
- ✅ **Health check** — skor + uyarılar
- ✅ **Windows eject** — PowerShell ile güvenli çıkarma
- ✅ **Kernel layout toleransı** — 5.x'ten 7.x'e, root hub vs. external hub farkı yok
- ✅ **Container-aware** — sysfs yoksa boş döner, çökmez
- ✅ **KernelNucleusT uyumu** — `knst_devices`, `knst_dev::uevent`, `knst_c16string` ile tam entegre

---