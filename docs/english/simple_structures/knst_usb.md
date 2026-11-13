# knst_usbc — Comprehensive USB Controller

Hey! 👋 This document explains what the `knst_usbc` namespace is, how to use it, and when it comes in handy.

**In short:** A comprehensive framework for **finding**, **querying**, **controlling**, and **subscribing to events** from USB devices. On Linux, full control through sysfs (authorization, reset, power cycle, driver bind/unbind, hub port enumeration, error stats, health check, hotplug). On Windows, enumeration + safe eject via PowerShell. On Android, read-only enumeration only.

Tested on kernel **6.x and 7.x**, should work on **5.x+**. Automatically tolerates these sysfs layout variations:

- `1-2`, `1-2.3` (regular USB device paths)
- `usbN` (N=1..99) root hub names
- Interface names `1-2:1.0`
- Port names `usbN-portM`, `1-2-portM`
- Container environments without `/sys/bus/usb/devices` → empty results

---

## 🎯 Honesty Matrix — What's Available Per Platform?

| Operation | Linux | Windows | Android |
|---|---|---|---|
| **Device enumeration** | ✅ | ✅ | ✅ |
| **VID/PID search** | ✅ | ✅ | ✅ |
| **Class search** | ✅ | ✅ | ✅ |
| **Driver search** | ✅ | ✅ PowerShell | ⚠️ Read-only |
| **Hub listing** | ✅ | ✅ | ✅ |
| **Reset** | ✅ ioctl | ❌ | ❌ |
| **Power cycle** | ✅ unbind/bind | ❌ | ❌ |
| **Authorize/deauthorize** | ✅ | ❌ | ❌ |
| **Runtime PM** | ✅ | ❌ | ❌ |
| **Driver bind/unbind** | ✅ | ❌ | ❌ |
| **Descriptor read** | ✅ sysfs | ❌ | ⚠️ From cache |
| **String read** | ✅ sysfs | ❌ | ⚠️ From cache |
| **Configuration/interface** | ✅ | ❌ | ❌ |
| **Speed/state** | ✅ | ❌ | ⚠️ Partial |
| **Power info** | ✅ | ❌ | ❌ |
| **Port info** | ✅ | ❌ | ❌ |
| **Hub ports** | ✅ | ❌ | ❌ |
| **Error stats** | ✅ | ❌ | ❌ |
| **Health check** | ✅ | ❌ | ❌ |
| **Hotplug event** | ✅ uevent | ⚠️ No event bus | ❌ |
| **Event bus** | ✅ | ❌ | ❌ |
| **Eject** | ✅ power_cycle | ✅ PowerShell | ❌ |

**Note:** On Windows, `find_*` functions work, but most methods of the `usb` class return `NotSupported` — only `eject_device()` is available. On Android, everything is read-only — you can enumerate but can't control devices.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Enumeration** | Search by VID/PID/class/driver/predicate |
| **`enrich()`** | Auto-marks root hubs as Hub |
| **Authorization** | USBGuard-style approve/reject |
| **Reset** | `USBDEVFS_RESET` ioctl |
| **Power cycle** | Software power-cut via unbind/bind |
| **Runtime PM** | auto-suspend, remote wakeup, delay |
| **Driver bind/unbind** | Change any interface's driver |
| **Descriptor read** | VID/PID/class/bcd/serial idx all |
| **Configurations** | Interface list + driver + endpoints |
| **Hub ports** | Empty/occupied ports + speed + link state |
| **Error stats** | Port reset, connect error, overcurrent |
| **Health check** | Score (0-100) + warning list |
| **Hotplug** | `wait_for_event` / `wait_for_action` via `uevent` |
| **Event bus** | UsbGuard-style daemon support |
| **Windows eject** | PowerShell `Disable-PnpDevice`/`Enable-PnpDevice` |
| **Kernel layout tolerance** | Automatic from 5.x to 7.x |

---

## 🚀 Quick Start

### Simple: List all USB devices

```cpp
#include "KernelNucleusT.hpp"

auto all = knst_usbc::find_all([](const knst_usb_info& u) { return true; });
for (auto& u : all) {
    std::cout << u.vid << ":" << u.pid
              << "  " << u.product_name
              << "  (" << u.device_num << ")\n";
}
```

### Find by VID/PID

```cpp
auto r = knst_usbc::find(0x046D, 0xC52B);   // Logitech Unifying Receiver
for (auto& u : r) {
    std::cout << "Found: " << u.product_name << "\n";
}
```

### Reset

```cpp
auto r = knst_usbc::usb::reset(u);
if (!r) std::cerr << "Reset failed\n";
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Enumeration** | `find`, `find_by_vid_pid`, `find_by_class`, `find_by_driver`, `find_hubs`, `find_all`, `enrich` |
| **Device control** | `usb::set_authorized`, `set_power_control`, `set_autosuspend_delay_ms`, `set_remote_wakeup` |
| **Reset/Recovery** | `usb::reset`, `usb::power_cycle` |
| **Driver** | `usb::bind_driver`, `usb::unbind_driver` |
| **Descriptor** | `usb::descriptors`, `usb::strings`, `usb::configurations` |
| **Status** | `usb::speed`, `usb::state`, `usb::power_info` |
| **Port** | `usb::port_info`, `usb::hub_ports` |
| **Diagnostics** | `usb::error_stats`, `usb::health_check` |
| **Hotplug** | `usb::wait_for_event`, `usb::wait_for_action` |
| **Event bus** | `usb_event_bus::subscribe`, `run`, `stop` |
| **Windows** | `usb::eject_device` (PowerShell) |

---

## 🔍 1) Enumeration

All find functions work through **`knst_devices::usb()`** — i.e. they use `knst_dev`'s USB list. `enrich()` automatically marks root hubs as `Hub` class and `is_hub=true`.

### `find(vid, pid = 0)`

```cpp
auto receivers = knst_usbc::find(0x046D);          // all Logitech devices
auto specific  = knst_usbc::find(0x046D, 0xC52B);   // specific model
```

`pid = 0` returns **all VIDs**.

### `find_by_vid_pid` — alias

```cpp
// Same as find(), just clarifies intent
auto r = knst_usbc::find_by_vid_pid(0x0781, 0x5567);
```

### `find_by_class(class_code)`

```cpp
auto audio   = knst_usbc::find_by_class(0x01);   // Audio
auto hid     = knst_usbc::find_by_class(0x03);   // HID (keyboard, mouse)
auto storage = knst_usbc::find_by_class(0x08);   // Mass Storage
auto hubs    = knst_usbc::find_by_class(0x09);   // Hub
auto video   = knst_usbc::find_by_class(0x0E);   // Video (webcam)
auto vendor  = knst_usbc::find_by_class(0xFF);   // Vendor-specific
```

**Class mapping in code:**

| Code | Enum |
|---|---|
| 0x01 | Audio |
| 0x02 | CDC |
| 0x03 | HID |
| 0x07 | Printer |
| 0x08 | MassStorage |
| 0x0E | Video |
| 0xE0 | Wireless |
| 0xFF | Vendor |

**Class 0x09 (Hub):** The code first checks `is_hub || is_root_hub` — root hubs appear as `usbN` in sysfs but may not return **0x09** as `bDeviceClass`. That's why there's a special branch.

**Composite device fallback:** If the device's own class doesn't match, the code scans the **interfaces'** `bInterfaceClass`. So an audio+hid composite device appears in both Audio and HID. This is **very important** because `bDeviceClass` alone is typically `0x00` (device class defined in interfaces).

### `find_by_driver(driver_name)`

```cpp
auto usbhid   = knst_usbc::find_by_driver(u"usbhid");
auto uas      = knst_usbc::find_by_driver(u"uas");
auto usb_stor = knst_usbc::find_by_driver(u"usb-storage");

// Special for root hubs:
auto hubs = knst_usbc::find_by_driver(u"hub");   // → same as find_hubs()
```

**How it works:** Matches if **any interface** of the device has this driver. So a composite device with `usbhid` on one interface and `usb-storage` on another matches both drivers.

**Root hub special case:** If you pass `driver_name == u"hub"`, the code goes directly to `find_hubs()` — because the root hub's sysfs symlink varies across kernel versions.

**Safety:** `knst_dev::is_safe_token(driver_name, 32)` check prevents path traversal (`../` etc.).

### `find_hubs()`

```cpp
auto hubs = knst_usbc::find_hubs();
for (auto& h : hubs) {
    std::cout << (h.is_root_hub ? "Root hub: " : "Hub: ")
              << h.product_name << "\n";
}
```

Same as `find_by_class(0x09)`.

### `find_all(predicate)`

```cpp
// Devices 5 Gbps or faster
auto fast = knst_usbc::find_all([](const knst_usb_info& u) {
    return u.speed_mbps >= 5000;
});

// A specific manufacturer
auto logitech = knst_usbc::find_all([](const knst_usb_info& u) {
    return u.manufacturer == u"Logitech";
});
```

Predicate receives the `enrich()`'d device.

### `enrich(knst_usb_info)` — helper

```cpp
knst_usb_info u = /* ... */;
u = knst_usbc::enrich(u);
// if root hub → usb_class = Hub, is_hub = true
```

**Why is it needed?** `knst_devices::usb()` doesn't fill `usb_class` correctly for root hubs (kernel sometimes returns 0x09, sometimes `Unknown`). `enrich()` normalizes this.

---

## 🛡️ 2) Device Control — Authorization

USBGuard-style **approve/reject** mechanism. A deauthorized device **is never talked to by the kernel** — so when a dangerous USB is plugged in, you can immediately reject it.

```cpp
// Authorize the device
knst_usbc::usb::set_authorized(u, true);

// Revoke authorization (device enumerates but no driver loads)
knst_usbc::usb::set_authorized(u, false);
```

**How it works:** Writes `1` or `0` to `/sys/bus/usb/devices/<name>/authorized`.

> ⚠️ When `authorized=0`, the device looks like it "dropped" from the kernel but remains physically plugged into the USB hub. Writing `1` again re-enumerates it. This is the core USBGuard mechanism.

---

## ⚡ 3) Runtime PM — Power Management

```cpp
// Enable auto-suspend
knst_usbc::usb::set_power_control(u, true);   // "auto"

// Disable (always on)
knst_usbc::usb::set_power_control(u, false);  // "on"

// Autosuspend delay (ms)
knst_usbc::usb::set_autosuspend_delay_ms(u, 2000);   // 2 seconds

// Remote wakeup
knst_usbc::usb::set_remote_wakeup(u, true);   // "enabled"
knst_usbc::usb::set_remote_wakeup(u, false);  // "disabled"
```

**How it works:**
- `set_power_control` → `/sys/bus/usb/devices/<name>/power/control` (auto/on)
- `set_autosuspend_delay_ms` → `power/autosuspend_delay_ms` (0-600000, above → `InvalidArgument`)
- `set_remote_wakeup` → `power/wakeup` (enabled/disabled)

> ⚠️ Above 600000 ms (10 minutes) delay returns **`InvalidArgument`**. Don't exceed the kernel limit.

---

## 🔄 4) Reset and Power Cycle

### `reset(u)` — USB port reset

```cpp
auto r = knst_usbc::usb::reset(u);
if (!r) {
    std::cerr << "Reset error\n";
    // On Linux this usually gives EPERM → root/sudo required
}
```

**How it works:** Issues `USBDEVFS_RESET` ioctl to `/dev/bus/usb/<bus>/<devnum>`. Kernel re-enumerates the device.

> ⚠️ **Root/sudo required.** As a regular user you'll get `PermissionDenied` (`EPERM`). Resetting a USB device needs `CAP_SYS_ADMIN` or a udev rule.

### `power_cycle(u, delay_ms = 500)` — Software power cut

```cpp
auto r = knst_usbc::usb::power_cycle(u, 1000);   // 1 second power cut
```

**How it works:**
1. Write device name to `/sys/bus/usb/drivers/usb/unbind` → device "electrically" drops
2. Wait `delay_ms` (max 5000 ms — anything more is clamped to 5000)
3. Write to `/sys/bus/usb/drivers/usb/bind` → device re-enumerates

**Difference from reset:** `reset()` just resets the port; `power_cycle()` fully detaches the driver — stronger. For a stuck device, `power_cycle` is more reliable.

> ⚠️ **Delay max 5000 ms.** The code automatically clamps anything higher to 5000.

---

## 🔌 5) Driver Bind/Unbind

```cpp
// Fully detach device from the USB driver
knst_usbc::usb::unbind_driver(u);

// Bind to a specific driver
knst_usbc::usb::bind_driver(u, u"usbhid");

// Example: detach HID device from usbhid, bind to your driver
knst_usbc::usb::unbind_driver(u);
knst_usbc::usb::bind_driver(u, u"my_custom_driver");
```

**How it works:**
- `unbind_driver` → `/sys/bus/usb/drivers/usb/unbind`
- `bind_driver` → `/sys/bus/usb/drivers/<driver_name>/bind`

**Safety:** `driver_name` is checked with `is_safe_token(_, 32)` — no `../`.

> ⚠️ Binding to the wrong driver can leave the device **unusable**. Be careful while testing.

> ⚠️ You must `unbind` first, then `bind`. Calling only `bind` on an already-bound device may give `IoError`.

---

## 📋 6) Descriptor and String Reading

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

**What it reads?** These sysfs files:
- `version` → `bcd_usb` (converted to BCD: "3.10" → 0x0310)
- `bDeviceClass` / `bDeviceSubClass` / `bDeviceProtocol` → read as **hex string**
- `bMaxPacketSize0` → decimal
- `bcdDevice` → hex
- `bNumConfigurations` → decimal

**VID/PID are written to `usb_device_descriptor` from `u.vid`/`u.pid`** — not separately in sysfs.

### `strings(u)` — Manufacturer/Product/Serial

```cpp
auto r = knst_usbc::usb::strings(u);
if (r) {
    std::cout << "Manufacturer: " << r.value.manufacturer << "\n";
    std::cout << "Product     : " << r.value.product << "\n";
    std::cout << "Serial      : " << r.value.serial << "\n";
}
```

Reads `manufacturer`, `product`, `serial` from sysfs. **May be empty** — some devices don't provide them.

### `configurations(u)` — Interface list

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

**What it reads?**
- `bConfigurationValue`, `bMaxPower` (from device)
- From each interface folder `1-2:1.0`, `1-2:1.1`: `bInterfaceNumber`, `bInterfaceClass`, `bInterfaceSubClass`, `bInterfaceProtocol`, `driver` symlink

> ⚠️ **`bMaxPower` is already mA.** Kernel multiplies the 2mA descriptor unit and writes mA to sysfs. **The code doesn't multiply again** (there's a comment in code: "sysfs bMaxPower already reports mA; do NOT multiply again").

---

## 📊 7) Speed and State

### `speed(u)` — USB speed

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

Reads the Mbps value from the `speed` file, converts to enum. Returns `Unknown` for "unknown" string (some hubs).

### `state(u)` — Device state

```cpp
auto r = knst_usbc::usb::state(u);
// Can return Unauthorized, Configured, Attached
```

**Simple logic:**
1. `authorized == 0` → `Unauthorized`
2. `bConfigurationValue > 0` → `Configured`
3. Otherwise → `Attached`

> ⚠️ The `usb_state` enum has values like `Default`, `Addressed`, `Suspended` but **the code doesn't fill them**. Only 3 are actually used.

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

**What it reads?**
- `bMaxPower` → `max_power_ma` (already mA)
- `power/control` → `control_mode` string
- `power/wakeup` → `has_remote_wakeup` (true if enabled)
- `power/autosuspend_delay_ms` → decimal
- `power/runtime_status` → `is_suspended` (true if suspended)

---

## 🔗 9) Port Info and Hub Ports

### `port_info(u)` — For a single device

```cpp
auto r = knst_usbc::usb::port_info(u);
```

> ⚠️ **This method usually returns empty.** In sysfs, `port/*` attributes live on **port devices** (`usb3-port1`, `1-4-port2`), not on the device itself. Use `hub_ports()`.

### `hub_ports(hub)` — All ports of a hub

```cpp
auto r = knst_usbc::usb::hub_ports(hub);
if (r) {
    for (auto& p : r.value) {
        std::cout << "Port " << p.number
                  << " [" << p.port_name << "]"
                  << "  " << (p.has_device ? "occupied" : "empty");
        if (p.has_device) {
            std::cout << "  device: " << p.device_name
                      << " (" << p.device_product << ")";
        }
        std::cout << "  link=" << p.link_state
                  << "  speed=" << p.port_speed_mbps << " Mbps\n";
    }
}
```

**What it does (3 phases)**

**1) Find occupied ports:** Walks all USB devices, collects those that are **direct children** of this hub.
- If root hub (`usb3`): `3-N` format, `3-N.M` **rejected** (grandchild)
- If normal hub (`1-2`): `1-2.P` format, `1-2.P.Q` **rejected** (grandchild)

**2) Add empty ports:** Reads the hub's `maxchild`, adds missing ports 1..`maxchild` with `has_device=false` (max 64).

**3) Sort:** By port number.

**For each port, what it reads?**
- `port/connect_type` → `is_connectable` (hotplug or hardwired)
- `port/disable` → `is_enabled` (0 = enabled)
- `port/link_state` → string
- `port/speed` → `port_speed_mbps` (number)

> ⚠️ On older kernels, `<hub>-portN` exists as a **real device**; on newer kernels it may not. `hub_ports()` tolerates both — tries a synthetic name if the port device is missing, and `read_port_attrs` silently skips on error.

> ⚠️ **Duplicate filter:** Multi-interface devices (`1-2.1` + `1-2:1.0` + `1-2:1.1`) are shown as one port.

---

## 📉 10) Error Stats and Health Check

### `error_stats(u)` — Port error counters

```cpp
auto r = knst_usbc::usb::error_stats(u);
if (r) {
    std::cout << "Port resets     : " << r.value.port_resets << "\n";
    std::cout << "Connect errors  : " << r.value.connect_errors << "\n";
}
```

**Note:** The `usb_error_stats` struct has 7 fields (`port_resets`, `disconnects`, `connect_errors`, `overcurrents`, `babble_errors`, `timeout_errors`, `crc_errors`) but **the code only fills 2**: `port/reset_count` and `port/connect_errors`. The others aren't common in sysfs.

### `health_check(u)` — Score + warnings

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

**Score logic (starts at 100):**
- `authorized == 0` → **-20** ("Device is unauthorized")
- `port/connect_errors > 0` → **-15** ("Port connect errors detected")
- `port/reset_count > 5` → **-10** ("Frequent port resets")

Result clamped at 0. **`healthy` = score ≥ 70**.

> ⚠️ This is a simple heuristic — for precise diagnostics use `dmesg` + `usbmon`.

---

## 📡 11) Hot-plug — `wait_for_event` / `wait_for_action`

```cpp
// Wait for any USB event (60-second timeout)
auto r = knst_usbc::usb::wait_for_event(60000);
if (r) {
    std::cout << "Event: " << r.value.action
              << "  device: " << r.value.devpath << "\n";
}

// Wait for a specific action
auto r2 = knst_usbc::usb::wait_for_action(60000, u"add");
```

**How it works?**
1. Opens a netlink socket via `knst_dev::uevent_monitor::open()`
2. Listens on the `"usb"` subsystem
3. Filters events with `devtype == "usb_device"`
4. Waits up to timeout

**Action values:** `"add"`, `"remove"`, `"change"`, `"bind"`, `"unbind"`

> ⚠️ `wait_for_action` must be called **again per action**. If it returns `Timeout`, no event arrived.

> ⚠️ `usb_event_bus::run()` **runs in a loop** — call it once and it listens until `stop()` is called.

---

## 📢 12) Event Bus — UsbGuard-Style Daemon

```cpp
class knst_usbc::usb_event_bus {
public:
    using handler = knst_function_shared<const knst_dev::uevent&>;

    uint32_t subscribe(handler h);           // returns the handler ID
    void unsubscribe(uint32_t id);
    result<void> run(uint32_t timeout_ms = 0);  // blocking loop
    void stop();                              // stops the loop
};
```

### Usage

```cpp
knst_usbc::usb_event_bus bus;

auto id = bus.subscribe([](const knst_dev::uevent& ev) {
    if (ev.action == u"add") {
        std::cout << "New device: " << ev.devpath << "\n";
    } else if (ev.action == u"remove") {
        std::cout << "Device left: " << ev.devpath << "\n";
    }
});

// Blocking loop — listens until stop() is called
std::thread t([&] { bus.run(); });

// ... later ...
bus.stop();
t.join();
```

**Thread-safe?** `subscribe`/`unsubscribe` are **mutex-locked**. Handler calls inside `run()` happen **outside the mutex** — so a slow handler won't block new subscriptions.

**Handler error case:** `try { h(ev); } catch (...) {}` — if one handler throws, the others aren't affected. **But the exception info is lost** — use try/catch inside the handler if you want to log.

**`timeout_ms = 0`:** `run()` uses a 60-second `wait` internally — returns to the uevent monitor every 60 seconds.

> ⚠️ **`usb_event_bus` doesn't work on Windows or Android.** `subscribe` always returns 0, `run` returns `NotSupported`.

---

## 🪟 13) Windows — Enumeration + Eject

### Functions that work

- `find(vid, pid)`, `find_by_vid_pid(vid, pid)`
- `find_by_class(class_code)`
- `find_by_driver(driver_name)` — via PowerShell `Get-PnpDevice`
- `find_hubs()`
- `find_all(predicate)`

### Functions that don't work

Almost everything in the `knst_usbc::usb` class returns `NotSupported`. **Exception: `eject_device`.**

### `eject_device(u, delay_ms = 500)`

```cpp
auto r = knst_usbc::usb::eject_device(u, 1000);
if (!r) {
    // status::PermissionDenied → not admin
    // status::NotFound → device not plugged in
    // status::ToolMissing → PowerShell not found
    std::cerr << "Eject failed\n";
}
```

**How it works?** This PowerShell script:
```powershell
$dev = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_XXXX&PID_YYYY*' } | Select-Object -First 1
if (-not $dev) { Write-Output 'NOT_FOUND'; exit 1 }
Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction Stop
Start-Sleep -Milliseconds <delay>
Enable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction Stop
Write-Output 'OK'
```

**In order:**
1. Find the device by VID/PID
2. `Disable-PnpDevice` → device disabled (as if unplugged)
3. Wait `delay_ms`
4. `Enable-PnpDevice` → re-enable

> ⚠️ **Admin / User Administrator required.** `Disable-PnpDevice` won't work as a regular user → `PermissionDenied`.

> ⚠️ **PowerShell required.** Auto-detects the `powershell.exe` path. If missing → `ToolMissing`.

> ⚠️ **15-second timeout.** Script can be slow; after 15 sec → `Timeout`.

---

## 🤖 14) Android — Enumeration Only

Android's USB stack is severely restricted:

- ✅ **Enumeration:** `UsbManager.getDeviceList` via JNI
- ✅ **VID/PID/class/manufacturer/product/serial:** From cache
- ⚠️ **Descriptor:** Only partial from cache
- ❌ **Reset, power cycle, bind/unbind:** SELinux blocks
- ❌ **Hub topology:** Hidden
- ❌ **Hotplug event:** BroadcastReceiver + permission needed (separate API)
- ❌ **Transfer speed:** Not exposed by UsbManager

**In practice:** `find()`, `find_by_class()`, `find_all()` work. All `usb::*` methods return `NotSupported`.

**`descriptors()`:** Only works if `u.usb_version` is cached, `bcd_usb` calculated. Other fields remain zero.

**`strings()`:** From cache (`u.manufacturer`, `u.product_name`, `u.serial`).

**`state()`:** Always returns `Configured` (UsbDevice's state is already configured).

---

## ⚠️ 15) Common Gotchas

### 1) `find_by_class` may show composite devices twice

An audio+HID webcam appears in both `find_by_class(0x01)` and `find_by_class(0x03)`. **Correct behavior** — different interfaces of the same device.

### 2) `hub_ports()` can't read `port/*` on older kernels

If the `<hub>-portN` device is missing, code tries a synthetic name; on error it silently skips. `has_device` and `device_name` are still correct (derived from device).

### 3) Grandchild reject

`1-2.3` is **child of `1-2`**. But `1-2.3.4` is a **grandchild** — not a direct child of `1-2`. `hub_ports(1-2)` returns only `1-2.1`, `1-2.2`, `1-2.3` — `1-2.3.4` is **filtered out**.

### 4) `bMaxPower` is not multiplied

Kernel multiplies the descriptor unit (2 mA) and **already writes mA** to sysfs. The code **doesn't multiply again** — there's a comment.

### 5) `reset()` requires root

Regular user gets `EPERM`. Need sudo or udev rule.

### 6) `power_cycle()` delay max 5000 ms

Anything higher is clamped to 5000.

### 7) `set_autosuspend_delay_ms()` above 600000 ms → `InvalidArgument`

Don't exceed the kernel limit.

### 8) Windows `eject_device()` needs admin

`Disable-PnpDevice` as a regular user → `PermissionDenied`.

### 9) `usb_event_bus::subscribe` returns 0 on Windows/Android

Not `NotSupported` — silently 0. You have to check.

### 10) `state()` returns only 3 values

`Unauthorized`, `Configured`, `Attached`. Other enum values (`Default`, `Addressed`, `Suspended`) **never return** — code doesn't fill them.

### 11) `error_stats()` fills only 2 fields

The `usb_error_stats` struct contains 7 fields but only `port_resets` and `connect_errors` are filled. The rest stay 0.

### 12) `hub_ports()` max 64 ports

Caps at 64 even if `maxchild > 64` — protection for virtual hubs.

### 13) Container environments without `/sys/bus/usb/devices` return empty

Docker/Flatpak/Snap without sysfs mount → `find_*` returns empty list. Not a bug, by design.

### 14) `unbind_driver` + `bind_driver` order matters

`unbind` first, then `bind`. Calling only `bind` on an already-bound device may give `IoError`.

### 15) `eject_device` 15-second timeout

If PowerShell is slow, `Timeout` can occur. Try lowering `delay_ms`.

---

## 📊 16) Performance Notes

| Operation | Cost | Note |
|---|---|---|
| `find(vid, pid)` | O(N) | N = total USB device count |
| `find_by_class()` | O(N × I) | I = average interface count |
| `find_by_driver()` | O(N × I) | Reads a sysfs symlink per interface |
| `descriptors()` | O(1) | 6-7 sysfs files |
| `configurations()` | O(N)` | Walks all interface folders |
| `hub_ports()` | O(N × P) | N = devices, P = ports (fallback) |
| `error_stats()` | O(1) | 2 sysfs files |
| `health_check()` | O(1) | 3 sysfs reads |
| `reset()` | O(1) + kernel work | ioctl |
| `power_cycle()` | O(1) + delay | unbind/bind |
| `eject_device()` (Win) | **~1-3 seconds** | PowerShell spawn + sleep |

**Tips:**
- **Cache** the result if calling `find_by_class()` frequently — sysfs is scanned each time
- Keep the `find_all()` predicate **light** — it's called per device
- `hub_ports()` is **expensive** — walks all USB devices per hub. Don't call it in a loop
- Windows `eject_device()` is **slow** — separate PowerShell spawn per device

---

## 🌍 17) Platform Differences — Summary

| Concern | Linux | Windows | Android |
|---|---|---|---|
| **Backend** | sysfs + ioctl | PowerShell PnP | JNI UsbManager |
| **Root required** | Reset/power cycle | Eject | — |
| **Enumeration** | ✅ Full | ✅ Full | ✅ Full |
| **Control** | ✅ Full | ⚠️ Eject only | ❌ None |
| **Hotplug** | ✅ netlink uevent | ❌ | ❌ |
| **Event bus** | ✅ | ❌ | ❌ |
| **No sysfs** | Empty results | N/A | N/A |

---

## 🎯 18) When to Use?

### ✅ Use

- **USBGuard-style security** — authorize/deauthorize new devices
- **Camera/microphone control** — find and disable by class 0x0E/0x01
- **Storage management** — trigger auto mount/unmount when USB disk is plugged
- **Hotplug daemon** — react to events via `usb_event_bus`
- **Debug tool** — list device details (descriptor, config, strings)
- **Kiosk application** — warn when unauthorized USB is plugged
- **Test automation** — restart device with reset/power_cycle
- **Hub monitoring** — what's on which port, link state
- **Windows eject** — safe removal via PowerShell

### ❌ Don't use

- **USB transfers** (bulk/interrupt/control) — this library is control-only; use `libusb` for data transfer
- **USB device mode (gadget)** — this is host side
- **Device control on Android** — SELinux blocks; enumeration only
- **Reset/power cycle on Windows** — `NotSupported`; use eject or a separate tool
- **USB in a container** — empty results if `/sys` isn't mounted

---

## ✅ 19) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Mark root hubs as Hub (enrich does it automatically, but you can manually too)
auto u = knst_usbc::enrich(raw_info);

// 2. For class 0x09 check is_hub (bDeviceClass unreliable)
if (u.is_hub || u.is_root_hub) { /* ... */ }

// 3. Know the interface fallback in find_by_class for composite devices
// (audio+hid webcam appears in both classes)

// 4. Check root before reset
if (geteuid() != 0) std::cerr << "sudo required\n";

// 5. When changing drivers: unbind first, then bind
knst_usbc::usb::unbind_driver(u);
knst_usbc::usb::bind_driver(u, u"my_driver");

// 6. For hub ports, get the whole list once and cache
auto ports = knst_usbc::usb::hub_ports(hub);
for (auto& p : ports.value) { /* ... */ }

// 7. Catch exceptions in event bus handler
bus.subscribe([](const knst_dev::uevent& ev) {
    try { /* ... */ } catch (...) { /* log */ }
});

// 8. Be admin for Windows eject
// (manifest requireAdministrator or UAC)
```

### ❌ Don't

```cpp
// 1. Don't call hub_ports() in a hot loop
for (int i = 0; i < 100; ++i) {
    auto r = knst_usbc::usb::hub_ports(hub);   // ❌ full USB scan every time
}

// 2. Don't multiply bMaxPower (already mA)
uint32_t wrong = r.value.max_power_ma * 2;   // ❌ kernel already multiplied

// 3. Don't pass >600000 to set_autosuspend_delay_ms
knst_usbc::usb::set_autosuspend_delay_ms(u, 1000000);   // ❌ InvalidArgument

// 4. Don't pass >5000 to power_cycle delay
knst_usbc::usb::power_cycle(u, 10000);   // ⚠️ auto-clamped to 5000

// 5. Don't call reset() without root (EPERM)
knst_usbc::usb::reset(u);   // ❌ PermissionDenied

// 6. Don't expect all state() enum values
auto s = knst_usbc::usb::state(u);   // only Unauthorized/Configured/Attached
// (Default, Addressed, Suspended never return)

// 7. Don't expect all error_stats fields to be filled
// (only port_resets and connect_errors filled)

// 8. Don't expect reset/power_cycle on Windows
knst_usbc::usb::reset(u);   // ❌ NotSupported

// 9. Don't expect enumeration in containers without sysfs
// empty list if /sys not mounted

// 10. Don't expect device control on Android
knst_usbc::usb::set_authorized(u, false);   // ❌ NotSupported
```

---

## 🔥 20) Full Example — USBGuard-Style Daemon

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    knst_usbc::usb_event_bus bus;

    // Check when a new device is plugged in
    bus.subscribe([](const knst_dev::uevent& ev) {
        if (ev.action != u"add") return;

        // Find the device from the path
        auto matches = knst_usbc::find_all([](const knst_usb_info& u) {
            return u.valid;
        });

        for (auto& u : matches) {
            // Allow HID keyboards/mice
            if (u.usb_class == knst_usb_class::HID) {
                std::cout << "✅ HID device: " << u.product_name << "\n";
                continue;
            }

            // Deauthorize mass storage
            if (u.usb_class == knst_usb_class::MassStorage) {
                std::cout << "⚠️  Storage device blocked: "
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

### Example 2: Health Dashboard

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

        // Auto reset
        if (h.value.score < 50.0) {
            std::cout << "    → attempting reset...\n";
            knst_usbc::usb::reset(u);
        }
    }
}
```

### Example 3: Hub Port Map

```cpp
void dump_hub(size_t hub_index) {
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
            std::cout << "empty";
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

## 📌 21) API Contract Summary

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

// Control
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

    // Status
    static result<usb_speed> speed(const knst_usb_info&) noexcept;
    static result<usb_state> state(const knst_usb_info&) noexcept;
    static result<usb_power_info> power_info(const knst_usb_info&) noexcept;

    // Port
    static result<usb_port_info> port_info(const knst_usb_info&) noexcept;
    static result<knst_vector<usb_port_info>> hub_ports(const knst_usb_info&) noexcept;

    // Diagnostics
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

## 🎯 What It Brings You

`knst_usbc` gives you serious advantages in these situations:

- ✅ **USBGuard-style security** — auto-approve/reject new devices
- ✅ **Camera/microphone control** — class-based find + authorize
- ✅ **Storage management** — trigger mount on plug
- ✅ **Hotplug daemon** — react to events via `usb_event_bus`
- ✅ **Debug tool** — detailed descriptor + config + strings
- ✅ **Kiosk application** — warn + deauthorize on unauthorized USB
- ✅ **Test automation** — restart device with reset/power_cycle
- ✅ **Hub monitoring** — port map + link state + speed
- ✅ **Health check** — score + warnings
- ✅ **Windows eject** — safe removal via PowerShell
- ✅ **Kernel layout tolerance** — 5.x to 7.x, root hub vs. external hub irrelevant
- ✅ **Container-aware** — empty results if sysfs missing, never crashes
- ✅ **KernelNucleusT compatibility** — fully integrated with `knst_devices`, `knst_dev::uevent`, `knst_c16string`

---