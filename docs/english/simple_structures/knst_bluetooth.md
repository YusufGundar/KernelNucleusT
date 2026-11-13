# knst_bluetooth — Bluetooth Control Library

Hey! 👋 `knst_bluetooth.hpp` is KernelNucleusT's **Bluetooth control** module. All in one header: adapter management, discovery, device info (battery/RSSI/class/UUID), pairing (Just Works + PIN), connect/disconnect, trust/block, wait helpers. On Linux via BlueZ (`bluetoothctl`), on Windows via the Win32 Bluetooth API, on Android via JNI `BluetoothAdapter`.

**In short:** **Synchronous, exception-free, no RTTI.** Every public call returns `result<T>`. **Failures are reported honestly per platform** — Windows radio power off is `NotSupported`, Android profile connect/disconnect is `NotSupported`, etc. MAC normalization (uppercase), ANSI sanitization, idempotent ops, `wait_for_*` helpers are all ready.

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

### Platform matrix

| Feature | Linux (BlueZ) | Windows (Win32) | Android (JNI) |
|---|---|---|---|
| **Adapter list** | ✅ | ✅ | ✅ |
| **Adapter info** | ✅ | ✅ | ✅ |
| **Adapter power on/off** | ✅ (`bluetoothctl power on`) | ❌ `NotSupported` | ⚠️ `enable()/disable()` (deprecated) |
| **Discoverable** | ✅ | ✅ (`BluetoothEnableDiscovery`) | ❌ `NotSupported` |
| **Pairable** | ✅ | ✅ (`BluetoothEnableIncomingConnections`) | ❌ `NotSupported` |
| **Adapter rename** | ✅ | ❌ `NotSupported` | ✅ (`setName`) |
| **rfkill block/unblock** | ✅ | ❌ `NotSupported` | ❌ `NotSupported` |
| **Scan (timed)** | ✅ (blocking) | ✅ (inquiry) | ✅ (discovery + sleep) |
| **scan_start/stop** | ✅ | ⚠️ no-op success | ✅ |
| **List all devices** | ✅ | ✅ | ⚠️ paired only |
| **List paired** | ✅ | ✅ | ✅ |
| **List connected** | ✅ | ✅ | ✅ |
| **Device info** | ✅ | ✅ | ✅ |
| **Device detail** | ✅ (CoD, UUID, Mfr) | ⚠️ (basic only) | ⚠️ (basic + battery) |
| **Battery level** | ✅ (BlueZ ≥5.48) | ❌ `NotSupported` | ✅ |
| **RSSI** | ⚠️ (only during discovery) | ❌ `NotSupported` | ❌ `NotSupported` |
| **Pair (Just Works)** | ✅ | ✅ (system dialog) | ✅ |
| **Pair with PIN** | ⚠️ best-effort (script) | ✅ (auth callback) | ⚠️ `setPin` (deprecated) |
| **Trust / untrust** | ✅ | ⚠️ no-op success | ⚠️ no-op (bond=trust) |
| **Connect** | ✅ | ✅ (service enable) | ❌ `NotSupported` (needs profile proxy) |
| **Disconnect** | ✅ | ✅ | ❌ `NotSupported` |
| **Remove (unpair)** | ✅ | ✅ | ✅ |
| **Block / unblock** | ✅ | ❌ `NotSupported` | ❌ `NotSupported` |
| **`pair_and_connect`** | ✅ | ✅ | ⚠️ (pair only) |
| **`reconnect`** | ✅ | ✅ | ❌ `NotSupported` |
| **`wait_for_device`** | ✅ | ✅ | ✅ |
| **`wait_for_connection`** | ✅ | ✅ | ✅ |

**Symbol key:** ✅ = full support · ⚠️ = partial/conditional · ❌ = none

**Critical notes:**
- **No radio power off on Windows.** `BluetoothEnableDiscovery` and `BluetoothEnableIncomingConnections` exist but they **restrict new connections**, they don't **power off the radio** — the device still transmits. Real power off needs **WinRT `Windows.Devices.Radios`** or **SetupAPI** (`DIF_PROPERTYCHANGE`, admin) — **not possible with a header-only library**.
- **On Android 12+ apps cannot trigger profile connect/disconnect.** A `BluetoothProfile.ServiceListener` (Java-side proxy) is required. This library only does **short-time polling** and may return `NotSupported`.
- **No Discoverable/Pairable API on Android** — done via system settings.
- **No Block (blacklist) API on Android.**
- **No RSSI or battery level in Win32 API.**
- **No SetAdapterName on Windows** — done via system settings.
- **On Linux without `bluetoothctl` nothing works** → `ToolMissing`.

### Dependencies

| Dependency | For | Note |
|---|---|---|
| **`knst_dev`** | `status`, `result`, `argv`, `run_tool_raw`, `format` | internal |
| **`knst_devices`** | `knst_bluetooth_device` struct, `split_kv`, `for_each_line` | internal |
| **`bluetoothctl`** (Linux) | All operations | `bluez` package |
| **`rfkill`** (Linux) | Block/unblock | `util-linux` |
| **`bthprops.lib`** (Windows) | Win32 BT API | Windows SDK |
| **`KnstBluetoothHelper`** (Android) | JNI bridge | App-side |

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **3 platforms one API** | BlueZ / Win32 / JNI |
| **MAC normalization** | Accepts lowercase/uppercase, returns uppercase |
| **ANSI sanitization** | Cleans `bluetoothctl` color codes |
| **Fast-path `has_adapter`** | Linux sysfs read (microseconds) |
| **Idempotent ops** | `pair` succeeds if already paired |
| **Verified operation** | Set → poll → verify (settle) |
| **Wait helpers** | `wait_for_device`, `wait_for_connection` |
| **Timeout + `is_fatal`** | Errors that won't resolve by waiting (ToolMissing, NotSupported) return immediately |
| **`bluetoothctl` blocking fix** | Distinguishes `ToolMissing` / `Timeout` when `bluetoothd` is down |
| **Output error mapping** | Converts BlueZ error strings to `status` |
| **PIN pairing** | Linux (script), Windows (auth callback), Android (`setPin`) |
| **Class of Device** | "Computer", "Audio/Video", "Peripheral" |
| **UUID parsing** | BlueZ `UUID: ... (name)` → list |
| **ManufacturerData** | First `ManufacturerData Key` |
| **Battery Percentage** | BlueZ ≥5.48 |
| **RFKILL recovery** | `set_power(true)` fail → `rfkill unblock` retry |
| **Windows service-based connect** | 17 service UUIDs (A2DP, AVRCP, HID, SPP, PANU, PBAP, ...) |

---

## 🚀 Quick Start

### Adapter + scan + connect

```cpp
#include "KernelNucleusT.hpp"

// Any adapter?
if (!knst_bt::bluetooth::has_adapter()) {
    std::cerr << "No Bluetooth\n";
    return 1;
}

// Adapter info
auto ad = knst_bt::bluetooth::adapter();
if (ad) {
    std::cout << "Adapter: " << ad.value.name
              << " (" << ad.value.address << ")"
              << (ad.value.powered ? " [on]" : " [off]") << "\n";
}

// Power on
knst_bt::bluetooth::set_power(true);

// 8-second scan → device list
auto devices = knst_bt::bluetooth::scan(8);
if (devices) {
    for (const auto& d : devices.value) {
        std::cout << d.address << "  " << d.name
                  << (d.paired    ? " [paired]"    : "")
                  << (d.connected ? " [connected]" : "") << "\n";
    }
}

// Connect to a specific device
auto r = knst_bt::bluetooth::pair_and_connect(u"AA:BB:CC:DD:EE:FF");
if (!r) {
    std::cerr << knst_dev::status_string(r.st) << "\n";
}
```

### Details of a specific device

```cpp
auto dd = knst_bt::bluetooth::detail(u"AA:BB:CC:DD:EE:FF");
if (dd) {
    std::cout << "Name   : " << dd.value.basic.name << "\n";
    std::cout << "Class  : " << dd.value.device_class << "\n";
    std::cout << "Battery: " << dd.value.battery_percent << "%\n";
    std::cout << "RSSI   : " << dd.value.rssi_dbm << " dBm\n";
    std::cout << "Paired : " << dd.value.basic.paired << "\n";
    for (const auto& u : dd.value.uuids) {
        std::cout << "UUID   : " << u << "\n";
    }
}
```

### PIN pairing

```cpp
// Linux: best-effort (Just Works and trusted devices work)
// Windows: via auth callback
// Android: setPin (API 24-32)
auto r = knst_bt::bluetooth::pair_with_pin(u"AA:BB:CC:DD:EE:FF", u"0000", 30);
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Runtime** | `is_supported()`, `has_adapter()` |
| **Adapter** | `adapters()`, `adapter()`, `set_power()`, `set_discoverable()`, `set_pairable()`, `set_adapter_name()`, `rfkill_block()` |
| **Discovery** | `scan_start()`, `scan_stop()`, `scan()` |
| **List** | `list()`, `list_paired()`, `list_connected()` |
| **Info** | `info(addr)`, `detail(addr)`, `battery(addr)`, `rssi(addr)` |
| **Wait** | `wait_for_device()`, `wait_for_connection()` |
| **Device op** | `pair()`, `pair_with_pin()`, `trust()`, `untrust()`, `connect()`, `disconnect()`, `remove()`, `block()`, `pair_and_connect()`, `reconnect()` |
| **Free helper** | `class_major(cod)` (in namespace) |

---

## 🎯 1) Enums and Structs

### 1.1) `adapter_info`

```cpp
struct adapter_info {
    knst_c16string address;
    knst_c16string name;
    knst_c16string alias;
    bool powered      = false;
    bool discoverable = false;
    bool pairable     = false;
    bool discovering  = false;
    bool valid        = false;
};
```

### 1.2) `device_detail`

```cpp
struct device_detail {
    knst_bluetooth_device basic;
    int32_t battery_percent = -1;        // -1 = unknown
    int32_t rssi_dbm        = 0;         // 0 = unknown
    knst_c16string manufacturer;         // e.g. "0x004c"
    knst_c16string device_class;         // "audio-headset" / "input-keyboard"
    knst_vector<knst_c16string> uuids;   // Service UUIDs
    bool blocked = false;
    bool valid   = false;
};
```

**`basic` fields** (`knst_bluetooth_device` — from `knst_devices.hpp`):

```cpp
struct knst_bluetooth_device {
    knst_c16string address;      // "AA:BB:CC:DD:EE:FF"
    knst_c16string name;
    bool connected = false;
    bool paired    = false;
    bool trusted   = false;
    int32_t rssi   = 0;
    knst_c16string type;         // Class string
    bool valid     = false;
};
```

### 1.3) `class_major` — Class of Device decode

```cpp
knst_c16string class_major(uint32_t cod) noexcept;
```

**Returns:**
- `"Computer"` (0x01)
- `"Phone"` (0x02)
- `"LAN/Network"` (0x03)
- `"Audio/Video"` (0x04)
- `"Peripheral"` (0x05)
- `"Imaging"` (0x06)
- `"Wearable"` (0x07)
- `"Toy"` (0x08)
- `"Health"` (0x09)
- `"Uncategorized"` (default)

**Usage:** `knst_bt::class_major(0x240404)` → `"Audio/Video"` (headphones).

---

## 🔌 2) Runtime — `is_supported` / `has_adapter`

### 2.1) `is_supported()`

```cpp
static bool is_supported() noexcept;
```

**Linux:** Is `bluetoothctl` in PATH?
**Windows:** Does `BluetoothFindFirstRadio` succeed?
**Android:** Adapter present?

**Usage:** Check once at app start. If false, the rest of the calls will return `ToolMissing` / `NotSupported` / `NotFound` anyway.

### 2.2) `has_adapter()`

```cpp
static bool has_adapter() noexcept;
```

**Linux:** `/sys/class/bluetooth/hci*` (microseconds, works even without `bluetoothd`).
**Windows:** Same as `is_supported()`.
**Android:** Result of `adapter()`.

**Why fast-path?** On Linux, `bluetoothctl list` **can hang when `bluetoothd` is down** (no D-Bus timeout). Sysfs answers **instantly**.

---

## 🎛️ 3) Adapter Operations

### 3.1) `adapters()` — All adapters

```cpp
static result<knst_vector<adapter_info>> adapters() noexcept;
```

**Linux:** `bluetoothctl list` + `show` per MAC.
**Windows:** Single adapter (Win32 limitation).
**Android:** Single adapter.

**No adapter:** Empty vector (not a fail).

### 3.2) `adapter()` — Default

```cpp
static result<adapter_info> adapter() noexcept;
```

**Linux:** `bluetoothctl show` (default adapter).
**Windows/Android:** Single adapter.

### 3.3) `adapter(addr)` — Specific adapter

```cpp
static result<adapter_info> adapter(const knst_c16string& addr) noexcept;
```

**Only meaningful on Linux** (multiple adapters). Windows/Android fall back to the default.

### 3.4) `set_power(bool)`

```cpp
static result<void> set_power(bool on) noexcept;
```

**Linux:**
1. `bluetoothctl power on/off`
2. Verify (settle 2.5s)
3. Fail + `on=true` → `rfkill unblock bluetooth` retry (soft-block recovery)

**Windows:** `NotSupported`.
**Android:** `BluetoothAdapter.enable()` / `disable()` (deprecated in API 33+, returns false → `NotSupported`).

### 3.5) `set_discoverable(bool)`

```cpp
static result<void> set_discoverable(bool on) noexcept;
```

**Linux:** `bluetoothctl discoverable on/off` + verify.
**Windows:** `BluetoothEnableDiscovery(hRadio, on)`.
**Android:** `NotSupported` (done via system settings).

### 3.6) `set_pairable(bool)`

```cpp
static result<void> set_pairable(bool on) noexcept;
```

**Linux:** `bluetoothctl pairable on/off` + verify.
**Windows:** `BluetoothEnableIncomingConnections(hRadio, on)` — **not "pairable", "accept new connections"** (closest Win32 equivalent).
**Android:** `NotSupported`.

### 3.7) `set_adapter_name(name)`

```cpp
static result<void> set_adapter_name(const knst_c16string& name) noexcept;
```

**Constraints:**
- Not empty
- ≤ 248 **bytes** (UTF-8)
- Must not start with `-` (bluetoothctl interprets it as an option)
- No control chars (0x00-0x1F, 0x7F)

**Linux:** `bluetoothctl system-alias <name>` + verify.
**Windows:** `NotSupported`.
**Android:** `BluetoothAdapter.setName()`.

### 3.8) `rfkill_block(bool)`

```cpp
static result<void> rfkill_block(bool block) noexcept;
```

**Linux:** `rfkill block/unblock bluetooth`.
**Windows/Android:** `NotSupported`.

**Note:** On Linux, `set_power(true)` automatically tries `rfkill unblock` on fail. Manual call is optional.

---

## 📡 4) Discovery

### 4.1) `scan_start(seconds)`

```cpp
static result<void> scan_start(uint32_t seconds = 1) noexcept;
```

**Constraints:** `seconds ∈ [1, 60]`.

**Linux:** `bluetoothctl --timeout=N scan on` — **blocks for N seconds** (on Linux, discovery is tied to the process lifetime).
**Windows:** No-op success.
**Android:** `startDiscovery()` (async).

**Warning:** On Linux `scan_start` **blocks until `bluetoothctl` exits**. **It doesn't return a result list** — it only starts discovery. For an async-style API, use `scan()`.

### 4.2) `scan_stop()`

```cpp
static result<void> scan_stop() noexcept;
```

**Linux:** `bluetoothctl scan off` + verify (`Discovering = false`).
**Windows:** No-op success.
**Android:** `stopDiscovery()`.

**Note:** On Linux `scan off` **errors if there is no owning session** — but the result is verified by **whether discovery is actually stopped**.

### 4.3) `scan(seconds, detailed)`

```cpp
static result<device_list> scan(uint32_t seconds = 8, bool detailed = true) noexcept;
```

**What it does?**
1. Check adapter is on
2. `bluetoothctl --timeout=N scan on` — **blocks for N seconds**
3. Returns results via `list(detailed)`

**Constraints:** `seconds ∈ [1, 60]`.

**Notes:**
- **Blocking** — don't call from the UI thread.
- **If adapter is off:** `BadState`.
- **Timeout:** If the process gets killed → `Timeout` — **but the device cache is still valid**, `list()` works.
- **`detailed = true`:** Calls `info()` for each device (slower but complete).

**Windows:** `fIssueInquiry = TRUE` (Bluetooth inquiry).
**Android:** `startDiscovery` + `sleep(N)` + `stopDiscovery` + `getDiscoveredDevices`.

### 4.4) `list(detailed)`

```cpp
static result<device_list> list(bool detailed = true) noexcept;
```

**Linux:** `bluetoothctl devices` — every device in the cache (discovered + paired + connected).
**Windows:** `BluetoothFindFirstDevice` + `fReturnUnknown=TRUE`.
**Android:** `getBondedDevices()` (no cache on Android, only paired).

**`detailed = true`:** Calls `info()` for each device → full info.

### 4.5) `list_paired()`

```cpp
static result<device_list> list_paired() noexcept;
```

**Linux:** `bluetoothctl devices Paired` (BlueZ ≥5.65) — on older versions the filter is ignored, the `paired` flag is verified via `info()`.
**Windows:** `fReturnAuthenticated = TRUE`.
**Android:** `getBondedDevices()`.

### 4.6) `list_connected()`

```cpp
static result<device_list> list_connected() noexcept;
```

**Linux:** `bluetoothctl devices Connected` + `info()` verify.
**Windows:** `fReturnConnected = TRUE`.
**Android:** Filters `connected` from the paired list.

---

## 🔍 5) Device Info

### 5.1) `info(addr)`

```cpp
static result<knst_bluetooth_device> info(const knst_c16string& addr) noexcept;
```

**Returns:** Basic info (name, paired, connected, rssi, type).

**Error cases:**
- `InvalidArgument` — invalid MAC
- `NotFound` — device not in BlueZ cache
- `ToolMissing` — `bluetoothctl` missing (Linux)
- `Timeout` — `bluetoothd` down (Linux)

### 5.2) `detail(addr)`

```cpp
static result<device_detail> detail(const knst_c16string& addr) noexcept;
```

**Returns:** Basic + battery, rssi, manufacturer, device_class, uuids, blocked.

**Linux:** Parses `bluetoothctl info <mac>`:
- `Name`, `Alias`
- `Paired: yes`, `Trusted: yes`, `Blocked: no`, `Connected: yes`
- `RSSI: 0xffffffc4 (-60)` → `rssi_dbm = -60`
- `Icon: audio-card` → `device_class`
- `Class: 0x240404` → `class_major()`
- `Battery Percentage: 0x64 (100)` → `battery_percent = 100`
- `ManufacturerData Key: 0x004c` → `manufacturer`
- `UUID: ... (name)` → `uuids.push_back(name)`

**Windows:** Basic + type. Battery/RSSI/Class → **`NotSupported`**.
**Android:** Basic + battery (via `getBatteryLevel`).

### 5.3) `battery(addr)`

```cpp
static result<int32_t> battery(const knst_c16string& addr) noexcept;
```

**Linux:** BlueZ ≥5.48 — `Battery Percentage` field.
**Windows:** `NotSupported`.
**Android:** `getBatteryLevel()`.

**`NotFound`:** Device exists but no battery info.

### 5.4) `rssi(addr)`

```cpp
static result<int32_t> rssi(const knst_c16string& addr) noexcept;
```

**Linux:** RSSI is **only reported by BlueZ during discovery**. Once discovery ends, `rssi_dbm = 0` → `NotFound`.
**Windows/Android:** `NotSupported`.

**Usage:** Call `info()` **while discovery is active** for real RSSI.

---

## ⏳ 6) Wait Helpers

### 6.1) `wait_for_device(addr, timeout_ms)`

```cpp
static result<void> wait_for_device(const knst_c16string& addr,
                                     uint32_t timeout_ms = 10000) noexcept;
```

**What it does?** Polls until `info(addr)` returns `Ok`.

**Note:** **Doesn't start a scan**. If the device isn't cached, call `scan()` first.

**Timeout default:** 10 seconds.

### 6.2) `wait_for_connection(addr, want_connected, timeout_ms)`

```cpp
static result<void> wait_for_connection(const knst_c16string& addr,
                                         bool want_connected = true,
                                         uint32_t timeout_ms = 15000) noexcept;
```

**What it does?** Polls until `info(addr).connected == want_connected`.

**`want_connected = false`** — Waits for disconnect. If the device **disappears from the cache** (forgotten by bluetoothd) → **success** (it's already disconnected).

**Timeout default:** 15 seconds.

**`is_fatal` handling:** `ToolMissing`, `NotSupported`, `PermissionDenied`, `InvalidArgument` → **fail immediately** (waiting is pointless).

---

## 🎯 7) Device Operations

All ops are **idempotent** — success if already in the requested state.

### 7.1) `pair(addr, timeout_s)`

```cpp
static result<void> pair(const knst_c16string& addr, uint32_t timeout_s = 30) noexcept;
```

**Linux:** `bluetoothctl pair <mac>` + settle + verify (`paired == true`).
**Windows:** Opens the system **pairing dialog** (`BluetoothAuthenticateDeviceEx`). Already paired returns instantly (`ERROR_NO_MORE_ITEMS` is also success).
**Android:** `createBond()` + poll (bond state `12 = BONDED`).

**Constraints:** `timeout_s ∈ [1, 120]` (Windows/Android: `[1, 120]`, Linux: `[1, 120]`).

**If PIN is required:** Use `pair_with_pin`.

### 7.2) `pair_with_pin(addr, pin, timeout_s)`

```cpp
static result<void> pair_with_pin(const knst_c16string& addr,
                                   const knst_c16string& pin,
                                   uint32_t timeout_s = 30) noexcept;
```

**PIN validation:** `is_safe_token(pin, 16)` — `[a-zA-Z0-9_-@.:]`, must not start with `-`, ≤16 chars.

#### Linux (best-effort)

**How does it work?** `bluetoothctl` has **no `pincode` command** — the PIN must be typed at the **agent prompt**. Script:
1. `agent KeyboardDisplay` + `default-agent` register
2. Start `pair <mac>`
3. Wait 4 seconds
4. Feed the PIN
5. Feed `yes` (passkey confirm)
6. Poll until `Paired: yes`
7. `quit`

**Limitations:**
- **Just Works** and **already trusted** devices work reliably.
- Some **passkey-showing** keyboards/mice may need manual `bluetoothctl`.
- The script runs through **`sh`** (dependency).

#### Windows (auth callback)

`BluetoothRegisterForAuthenticationEx` + `BluetoothAuthenticateDeviceEx`:
- **Legacy pairing** (`0x01`) → PIN given to callback
- **Numeric comparison** (`0x02`) → `negativeResponse = FALSE` (passkey auto-confirm)
- **Just Works** (`0x04`, `0x06`) → auto-confirm
- Other methods → `negativeResponse = TRUE` (reject)

#### Android (deprecated `setPin`)

`BluetoothDevice.setPin(byte[])` — **deprecated in API 33+** but **works on API 24-32**. Then `createBond()` + poll.

### 7.3) `trust(addr)` / `untrust(addr)`

```cpp
static result<void> trust(const knst_c16string& addr) noexcept;
static result<void> untrust(const knst_c16string& addr) noexcept;
```

**Linux:** `bluetoothctl trust/untrust <mac>` + verify.
**Windows:** **No-op success** (no explicit trust flag in Win32 — paired = trusted).
**Android:** **No-op success** (bond = trust).

### 7.4) `connect(addr, timeout_s)`

```cpp
static result<void> connect(const knst_c16string& addr, uint32_t timeout_s = 20) noexcept;
```

**Linux:** `bluetoothctl connect <mac>` + settle + verify (`connected == true`).
**Windows:** Enables **17 service UUIDs**:
- Audio: AudioSink (0x110B), AudioSource (0x110A), AVRCP (0x110C/0x110E), Headset (0x1108/0x1112), Handsfree (0x111E/0x111F)
- Input: HID (0x1124)
- Data: SPP (0x1101), OBEX Object Push (0x1105)
- Network: PANU (0x1115), NAP (0x1116), GN (0x1117)
- Phone: PBAP (0x112F/0x1130), SIM Access (0x112D)
- **Any of them succeeds** → `ERROR_SUCCESS`
**Android:** **`NotSupported`** (needs Java-side profile proxy).

**Timeout default:** 20 seconds.

### 7.5) `disconnect(addr)`

```cpp
static result<void> disconnect(const knst_c16string& addr) noexcept;
```

**Linux:** `bluetoothctl disconnect <mac>` + verify.
**Windows:** Disables the same 17 service UUIDs + verify.
**Android:** `NotSupported` (needs profile proxy).

### 7.6) `remove(addr)` — Unpair

```cpp
static result<void> remove(const knst_c16string& addr) noexcept;
```

**Linux:** `bluetoothctl remove <mac>` + settle (until device is gone from cache).
**Windows:** `BluetoothRemoveDevice()`.
**Android:** `removeBond()` (helper).

**Idempotent:** If the device is already gone → **success**.

### 7.7) `block(addr, blocked)`

```cpp
static result<void> block(const knst_c16string& addr, bool blocked) noexcept;
```

**Linux:** `bluetoothctl block/unblock <mac>` + verify.
**Windows/Android:** `NotSupported`.

**Usage:** Tells BlueZ "never accept this device".

### 7.8) `pair_and_connect(addr)`

```cpp
static result<void> pair_and_connect(const knst_c16string& addr) noexcept;
```

**Order:**
1. `pair(addr)` — even if it fails, check `paired`
2. `trust(addr)` — best-effort (ignore fail)
3. `connect(addr)`

**Windows:** `pair` → `connect`.
**Android:** Only `pair` (connect is `NotSupported`).

### 7.9) `reconnect(addr)`

```cpp
static result<void> reconnect(const knst_c16string& addr) noexcept;
```

**Order:**
1. `disconnect(addr)` — best-effort (OK if already disconnected)
2. `wait_for_connection(addr, false, 5000)` — wait for disconnect to settle
3. `connect(addr, 20)`

**Usage:** "Drop and reconnect" when the link breaks.

---

## 🖥️ 8) Platform-Specific Backends

### 8.1) Linux (BlueZ)

**APIs used:**
- **`bluetoothctl`** — all operations (`show`, `list`, `devices`, `info`, `pair`, `connect`, `trust`, `power`, `discoverable`, `pairable`, `scan`, `system-alias`, `remove`, `block`, `unblock`)
- **`rfkill`** — block/unblock
- **sysfs** (`/sys/class/bluetooth/hci*`) — fast-path `has_adapter`

**Special handling:**

**ANSI sanitization:** `bluetoothctl` output contains color codes (`\033[...m`), carriage returns, and readline markers (`\001 \002`). `sanitize()` **cleans** them.

**Prompt strip:** The `[bluetooth]# ` prefix is removed by `prep_line()`.

**When `bluetoothd` is down:** `bluetoothctl` **can hang** (no D-Bus timeout). `run_tool_raw` is protected by a **timeout** (`ToolMissing` vs `Timeout` distinction made with `tool_in_path`).

**Error mapping:** BlueZ error strings:
- `"No default controller"` → `NotFound`
- `"org.bluez.Error.InProgress"` → `Busy`
- `"org.bluez.Error.NotReady"` / `"Blocked through rfkill"` → `BadState`
- `"org.bluez.Error.NotPermitted"` / `"AuthenticationRejected"` → `PermissionDenied`
- `"org.bluez.Error.NotSupported"` → `NotSupported`
- `"AuthenticationTimeout"` / `"page-timeout"` → `Timeout`
- Others → fallback

**`class_major()` decode:** BlueZ `Class:` field like `0x240404` (hex) → `(cod >> 8) & 0x1F` major class.

**UUID parsing:** `UUID: <uuid> (<name>)` — the name inside `(...)` is taken (e.g. `Audio Sink`).

**`ManufacturerData Key:`** — first key (hex) goes into `manufacturer`.

### 8.2) Windows (Win32 Bluetooth)

**APIs used:**
- `BluetoothFindFirstRadio` — find adapter
- `BluetoothGetRadioInfo` — adapter info
- `BluetoothIsDiscoverable` / `BluetoothIsConnectable` — state
- `BluetoothEnableDiscovery` — discoverable toggle
- `BluetoothEnableIncomingConnections` — pairable toggle
- `BluetoothFindFirstDevice` / `BluetoothFindNextDevice` — enumerate
- `BluetoothGetDeviceInfo` — device info
- `BluetoothAuthenticateDeviceEx` — pair dialog
- `BluetoothRegisterForAuthenticationEx` — PIN callback
- `BluetoothSendAuthenticationResponseEx` — callback response
- `BluetoothRemoveDevice` — unpair
- `BluetoothSetServiceState` — connect/disconnect (service-level)

**Limitations:**
- **Power off `NotSupported`** — no Win32 radio power API. Requires WinRT `Windows.Devices.Radios` or SetupAPI.
- **SetAdapterName `NotSupported`** — via system settings.
- **Block `NotSupported`** — no Win32 blacklist API.
- **RSSI/battery `NotSupported`** — not in Win32.
- **PIN pairing** — via auth callback (16-char limit `BTH_MAX_PIN_SIZE`).

**Service UUID list** (for connect):
- 17 UUIDs (Audio, Input, Data, Network, Phone)

**Error mapping:** `win_status(DWORD)`:
- `ERROR_ACCESS_DENIED` → `PermissionDenied`
- `ERROR_NOT_FOUND` → `NotFound`
- `ERROR_NOT_SUPPORTED` → `NotSupported`
- `ERROR_INVALID_PARAMETER` → `InvalidArgument`
- `WAIT_TIMEOUT` → `Timeout`
- `ERROR_DEVICE_NOT_CONNECTED` → `BadState`

### 8.3) Android (JNI BluetoothAdapter)

**APIs used (via JNI):**
- `BluetoothAdapter.getDefaultAdapter()` — adapter
- `BluetoothAdapter.isEnabled()` — power state
- `BluetoothAdapter.enable()` / `disable()` — power toggle (deprecated in API 33+)
- `BluetoothAdapter.getBondedDevices()` — paired list
- `BluetoothAdapter.startDiscovery()` / `stopDiscovery()` — scan
- `BluetoothAdapter.setName()` — adapter rename
- `BluetoothDevice.getName()` / `getAddress()` / `getType()` / `getBondState()` / `getBleState()` — device info
- `BluetoothDevice.setPin(byte[])` — PIN (deprecated)
- `BluetoothDevice.createBond()` — pair
- `BluetoothDevice.removeBond()` — unpair

**Helper class:** `com.knst.devices.KnstBluetoothHelper`
- `getAdapterInfo()` → `[String, String, int, int]`
- `getBondedDevices()` → `[name, address, type, ..., bondState, connectionState] × N` (6 slots per device)
- `getDeviceInfo(addr)` → same format (1 device)
- `getDiscoveredDevices()` → scan results
- `getBatteryLevel(addr)` → int (-1 = none)
- `startDiscovery()` / `stopDiscovery()`
- `createBond(addr)` / `removeBond(addr)`
- `setPin(addr, pin)` — PIN setting (API 24-32)

**JNI Context:** `KnstDevicesHelper.setContext(activity)` must have been called earlier.

**Limitations:**
- **Discoverable/Pairable `NotSupported`** — done via system settings.
- **Connect/Disconnect `NotSupported`** — needs Java-side profile proxy.
- **Block `NotSupported`** — no API.
- **RSSI `NotSupported`** — only available via `BluetoothGatt` (BLE), not for classic BT.
- **Power off** — `enable()/disable()` returns false on API 33+ → `NotSupported`.

---

## ⚠️ 9) Common Gotchas

### 1) Nothing works on Linux without `bluetoothctl`

`is_supported()` → `false`, all ops return `ToolMissing`.

**Fix:** `sudo apt install bluez` (Debian/Ubuntu) / `sudo dnf install bluez` (Fedora).

### 2) `Timeout` on Linux without `bluetoothd`

`bluetoothctl` **hangs without `bluetoothd`**. `run_btctl` is protected by timeout, but:
- `tool_in_path` → `Timeout`
- `!tool_in_path` → `ToolMissing`

**Fix:** `sudo systemctl enable --now bluetooth`.

### 3) Windows power off truly doesn't exist

```cpp
knst_bt::bluetooth::set_power(false);   // NotSupported
```

**No Win32 radio power-off API.** Only:
- `set_discoverable(false)` — turn discovery off
- `set_pairable(false)` — reject new connections

But the device **still transmits**. Real power off needs **WinRT** or **SetupAPI**.

### 4) No Android profile connect/disconnect

```cpp
knst_bt::bluetooth::connect(addr);   // NotSupported (API 12+)
```

**On Android 12+** apps **cannot trigger** profile connections like `A2DP`/`HFP`. `BluetoothProfile.ServiceListener` (Java-side) is required.

**Alternative:** After `pair()` the system auto-connects profiles. The library polls briefly.

### 5) RSSI only during discovery

```cpp
// After discovery ends:
knst_bt::bluetooth::rssi(addr);   // NotFound (rssi_dbm = 0)
```

**Fix:** Call `info()` during or immediately after `scan()`.

### 6) No battery on Windows

```cpp
knst_bt::bluetooth::battery(addr);   // NotSupported
```

**Win32 BT API doesn't report battery.** BLE GATT can read it, but not for classic BT.

### 7) MAC normalization

```cpp
knst_bt::bluetooth::pair(u"aa:bb:cc:dd:ee:ff");   // ✅ OK (auto uppercase)
knst_bt::bluetooth::pair(u"AA-BB-CC-DD-EE-FF");   // ❌ InvalidArgument
```

**Only `:` separator** accepted. `is_mac()` check.

### 8) `scan()` is blocking

```cpp
auto devices = knst_bt::bluetooth::scan(8);   // blocks 8 seconds
```

**Don't call from the UI thread.** For async, use `scan_start(seconds)` + timer + `list()`.

### 9) `pair_with_pin` is best-effort (Linux)

```cpp
knst_bt::bluetooth::pair_with_pin(addr, u"0000");
```

**Just Works** and **trusted** devices work. **Passkey-showing** devices may need **manual `bluetoothctl`**.

**Fix:** If it fails, try manual `bluetoothctl`.

### 10) Android `setPin` deprecated

**In API 33+** `BluetoothDevice.setPin` is **deprecated** but still works (on some devices). Newer API uses `setPairingConfirmation`.

**Fix:** Use `pair()` (Just Works) on API 33+.

### 11) `pair()` timeout (Linux)

```cpp
auto r = knst_bt::bluetooth::pair(addr, 30);
// After 30 seconds → Timeout
```

**PIN is awaited** — no confirmation on your side. Use `pair_with_pin` or manual `bluetoothctl`.

### 12) `wait_for_connection` `NotFound` handling

```cpp
wait_for_connection(addr, false, 5000);
// If device disappears from cache → success (already disconnected)
```

**Why?** If the device is forgotten during the wait, the disconnect wait is pointless — it's already disconnected.

### 13) `is_fatal` — no need to wait

```cpp
// ToolMissing, NotSupported, PermissionDenied, InvalidArgument → fail immediately
```

**`wait_until` fails immediately** on these — doesn't waste time waiting for timeout.

### 14) `bluetoothctl` ANSI codes

```cpp
// Sanitized output:
"[bluetooth]# Controller AA:BB:CC:DD:EE:FF My PC"
```

`sanitize()` cleans ESC codes, `\r`, and control chars. `prep_line()` strips the prompt prefix.

### 15) Scan with adapter off

```cpp
knst_bt::bluetooth::scan(8);   // BadState
```

**Fix:** `set_power(true)` first.

### 16) `set_adapter_name` UTF-8 byte limit

```cpp
knst_bt::bluetooth::set_adapter_name(u"日本語");   // 9 bytes — OK
knst_bt::bluetooth::set_adapter_name(u"-bad");     // InvalidArgument (starts with '-')
```

**BlueZ limit:** 248 **bytes** (UTF-8). Not `knst_c16string.length()`, `to_utf8().length()`.

### 17) `pair_and_connect` partial success

```cpp
knst_bt::bluetooth::pair_and_connect(addr);
// pair succeeds, connect fails → pair stays successful
```

**Trust:** Best-effort, ignored.
**Connect fail:** Even if `pair` succeeded, it **returns fail**.

### 18) `reconnect` disconnect wait

```cpp
knst_bt::bluetooth::reconnect(addr);
// 1. disconnect (best-effort)
// 2. wait_for_connection(addr, false, 5000) — max 5 seconds
// 3. connect(addr, 20)
```

**Total max:** ~25 seconds.

### 19) Android adapter rename `setName`

```cpp
knst_bt::bluetooth::set_adapter_name(u"My Phone");   // ✅
```

**Works on Android** (`BluetoothAdapter.setName`) — requires **system permissions** (`BLUETOOTH_CONNECT` on API 31+).

### 20) `detail()` RSSI = 0 (unknown)

```cpp
dd.rssi_dbm = 0;   // If not during discovery
```

**`0` = unknown**, not real RSSI. `rssi()` → `NotFound`.

### 21) `has_adapter()` Linux fast-path

```cpp
// /sys/class/bluetooth/hci* (microseconds)
// "hci0:256" (handle) skipped, only "hci0" (adapter)
```

**Why?** `bluetoothctl list` **hangs when bluetoothd is down**. Sysfs always responds.

### 22) `list()` returns empty even without an adapter

```cpp
knst_bt::bluetooth::list();   // Empty vector
```

**Not a fail** — no adapter means no devices. Empty vector.

### 23) `pair_with_pin` shell script injection

```cpp
// Mac / pin / wait as positional parameters
// Not interpolated into the script text
a << "-c" << kScript << "sh" << mac << pin << timeout_s;
```

**Safe.** `is_safe_token(pin, 16)` validation + positional args.

### 24) Windows `BluetoothEnableIncomingConnections` != powered

```cpp
set_pairable(false);   // Rejects new connections, radio not off
```

**Pairable and powered are separate.** No powered toggle on Windows.

### 25) Android `BluetoothAdapter.enable()` API 33+

```cpp
// API 33+:
bluetooth.set_power(true);   // NotSupported
```

**On Android 13+** `BluetoothAdapter.enable()` is **deprecated** and returns `false` on many devices. System settings are required.

### 26) UUID parsing `UUID: <uuid> (<name>)` — 64-char limit

```cpp
// Name inside parentheses max 63 chars
const size_t n = close - open - 1;
if (n < 64) { ... }
```

**Long names are skipped.**

### 27) `ManufacturerData Key:` first value

```cpp
dd.manufacturer = "0x004c";   // First key
```

**If multiple, only the first.**

### 28) Windows `pair()` dialog blocking

```cpp
knst_bt::bluetooth::pair(addr, 30);
// Dialog shown for 30 seconds, user confirms/rejects
```

**Dialog is modal** — doesn't return until the user acts. `timeout_s = 30` → dialog timeout.

### 29) Windows `pair_with_pin` register/unregister

```cpp
BluetoothRegisterForAuthenticationEx(...)
BluetoothAuthenticateDeviceEx(...)
// poll callback + device
BluetoothUnregisterAuthentication(reg)
```

**During the callback** `BluetoothGetDeviceInfo` is polled — did pairing complete?

### 30) `remove()` cache cleanup

```cpp
knst_bt::bluetooth::remove(addr);
// bluetoothctl remove → cache cleanup
// settle poll: device gone?
```

**`NotFound` = already removed → success.**

### 31) `scan()` device cache

```cpp
knst_bt::bluetooth::scan(8);
// 8-second scan → added to bluetoothd cache
// list() reads from this cache
```

**Bluetoothd restart** → cache cleared.

### 32) `wait_for_device` requires scan

```cpp
knst_bt::bluetooth::wait_for_device(addr, 10000);
// If device not in cache → Timeout after 10 seconds
```

**Fix:** `scan()` first → then `wait_for_device`.

### 33) `device_class` Icon vs Class

```cpp
// Two fields:
dd.device_class = "audio-headset";   // BlueZ Icon (semantic)
// or
dd.device_class = "Audio/Video";     // Class major
```

**If BlueZ `Icon:` exists** it's used, otherwise `class_major()` decode.

### 34) `paired` vs `bonded` (Android)

```cpp
d.paired = (bondState == 12);   // BOND_BONDED
```

**On Android "bond" = "paired"**. `bondState == 11` = BONDING (in progress).

### 35) `connected` (not Android `getBleState`)

```cpp
// For classic BT:
d.connected = (connectionState == 1);   // STATE_CONNECTED
```

**For BLE** `BluetoothGatt` is a separate API — not in this library.

### 36) Windows device `fRemembered` vs `fAuthenticated`

```cpp
d.paired  = di.fAuthenticated;   // Pair complete
d.trusted = di.fRemembered;      // System remembers
```

**They're different** — a paired device may not be trusted (has been untrusted).

### 37) `untrust` Windows no-op

```cpp
knst_bt::bluetooth::untrust(addr);   // success (no-op)
```

**No explicit untrust in Win32** — paired = trusted.

### 38) `set_power` Linux rfkill retry

```cpp
set_power(true);
// 1. bluetoothctl power on → fail (rfkill soft-block)
// 2. rfkill unblock bluetooth
// 3. bluetoothctl power on → retry
```

**Automatic** — even if the user didn't call `rfkill_block(false)`.

### 39) `class_major()` 5-bit major class

```cpp
(cod >> 8) & 0x1F   // Bits 8-12
```

**Bluetooth CoD spec:** Major class is bits 8-12. Example `0x240404` → `(0x240404 >> 8) & 0x1F = 0x04` = Audio/Video.

### 40) Timeout + `is_fatal` combination

```cpp
// wait_until:
// - ToolMissing / NotSupported / PermissionDenied / InvalidArgument → fail immediately
// - Others → poll until deadline
```

**Permission denied** waiting 10 seconds is pointless — returns immediately.

### 41) Adapter rename on Windows doesn't exist

```cpp
knst_bt::bluetooth::set_adapter_name(u"My PC");   // NotSupported
```

**Win32 BT API doesn't support adapter rename.** System settings required.

### 42) No Android block

```cpp
knst_bt::bluetooth::block(addr, true);   // NotSupported
```

**No "blacklist" API on Android.** Achieved via unpair + not re-pairing.

### 43) Linux `devices Paired` filter (BlueZ < 5.65)

```cpp
knst_bt::bluetooth::list_paired();
// Old BlueZ: filter ignored → all devices returned
// But the library verifies the `paired` flag via info()
```

**Result is correct** — just extra `info()` calls.

### 44) `wait_connection` cache miss (`want_connected = false`)

```cpp
// If the device disappears from cache (unpair, forget, ...)
// → success (already disconnected)
```

**Why?** Waiting for disconnect + no device = already disconnected.

### 45) `scan_start` blocking (Linux)

```cpp
auto r = knst_bt::bluetooth::scan_start(8);   // blocks 8 seconds
```

**Blocking like `scan()`.** No async API.

### 46) `--timeout=N` bluetoothctl flag

```cpp
knst_dev::argv a;
a << knst_dev::format("--timeout=%u", seconds) << "scan" << "on";
```

**`bluetoothctl --timeout=8 scan on`** — stops automatically after 8 seconds.

### 47) `adapter()` default (Linux)

```cpp
knst_bt::bluetooth::adapter();
// bluetoothctl show (default adapter)
```

**With multiple adapters** only the default is returned. Use `adapter(addr)` for a specific one.

### 48) `adapters()` fanout (Linux)

```cpp
// 1. bluetoothctl list → MAC list
// 2. adapter(mac) per MAC → full info
```

**N adapters** → N+1 calls. Slow.

### 49) `pair()` `ERROR_NO_MORE_ITEMS` Windows

```cpp
// Already paired → BluetoothAuthenticateDeviceEx returns ERROR_NO_MORE_ITEMS
// The library treats it as ERROR_SUCCESS
```

**Idempotent behavior.**

### 50) `pin_auth_cb` `MITMProtectionNotRequired`

```cpp
BluetoothAuthenticateDeviceEx(..., MITMProtectionNotRequired);
```

**MITM protection not required** — for legacy PIN. Can be changed for higher-security scenarios.

---

## 📊 10) Performance

| Operation | Cost | Note |
|---|---|---|
| **`is_supported`** | Linux: PATH check (~µs) | Windows/Android: adapter probe (~ms) |
| **`has_adapter`** | ~µs | Sysfs (Linux) / adapter probe |
| **`adapter()`** | 50-200 ms | `bluetoothctl show` |
| **`adapters()`** | 100-500 ms | N+1 calls |
| **`set_power`** | 500-8000 ms | Settle 2.5s |
| **`set_discoverable`** | 500-5000 ms | `bluetoothctl` spawn |
| **`scan(8)`** | 8000+ ms | 8-second blocking |
| **`scan_start(1)`** | 1000+ ms | Blocking |
| **`list()`** | 100-500 ms | `bluetoothctl devices` |
| **`list_paired()`** | 500-3000 ms | N × `info()` |
| **`info(addr)`** | 100-300 ms | `bluetoothctl info` |
| **`detail(addr)`** | 100-300 ms | Same |
| **`pair(addr, 30)`** | 500-30000 ms | Depends on timeout |
| **`pair_with_pin`** | 5000-30000 ms | Script + settle |
| **`connect`** | 2000-20000 ms | Depends on timeout |
| **`disconnect`** | 500-5000 ms | Settle |
| **`remove`** | 500-3000 ms | Settle |
| **`wait_for_device`** | Depends on timeout | Default 10s |
| **`wait_for_connection`** | Depends on timeout | Default 15s |

**Typical latencies:**
- **Adapter info:** 100-200 ms
- **Scan 8 seconds:** 8-9 seconds
- **Pair Just Works:** 5-15 seconds
- **Connect:** 3-10 seconds
- **Disconnect:** 1-3 seconds

**Optimizations:**
- **Fast-path `has_adapter`** (Linux sysfs) — no `bluetoothctl` spawn
- **`detailed = false`** — fast `list()` (no N × `info`)
- **`scan()` in one call** — instead of `scan_start` + `list`
- **`pair_and_connect`** — pair + trust + connect in one call

---

## ✅ 11) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Check support at startup
if (!knst_bt::bluetooth::is_supported()) return;

// 2. Any adapter?
if (!knst_bt::bluetooth::has_adapter()) return;

// 3. Power on
knst_bt::bluetooth::set_power(true);

// 4. Scan + list
auto devices = knst_bt::bluetooth::scan(8);
if (devices) {
    for (auto& d : devices.value) {
        std::cout << d.address << " " << d.name << "\n";
    }
}

// 5. Pair + connect
knst_bt::bluetooth::pair_and_connect(u"AA:BB:CC:DD:EE:FF");

// 6. Wait
knst_bt::bluetooth::wait_for_connection(u"AA:BB:CC:DD:EE:FF", true, 15000);

// 7. List paired
auto paired = knst_bt::bluetooth::list_paired();

// 8. Detail (battery, class, UUIDs)
auto dd = knst_bt::bluetooth::detail(addr);
if (dd && dd.value.battery_percent > 0) {
    std::cout << "Battery: " << dd.value.battery_percent << "%\n";
}

// 9. PIN pairing
knst_bt::bluetooth::pair_with_pin(addr, u"0000", 30);

// 10. Remove (unpair)
knst_bt::bluetooth::remove(addr);
```

### ❌ Don't

```cpp
// 1. Without bluetoothd on Linux
// ❌ Returns Timeout (bluetoothctl hangs)
// ✅ First: sudo systemctl enable --now bluetooth

// 2. Expect Windows power off
knst_bt::bluetooth::set_power(false);   // ❌ NotSupported
// ✅ set_discoverable(false) + set_pairable(false)

// 3. Expect Android connect
knst_bt::bluetooth::connect(addr);   // ❌ NotSupported (profile proxy)

// 4. Expect Windows battery
knst_bt::bluetooth::battery(addr);   // ❌ NotSupported

// 5. Android discoverable
knst_bt::bluetooth::set_discoverable(true);   // ❌ NotSupported

// 6. Wrong MAC format
knst_bt::bluetooth::pair(u"AA-BB-CC-DD-EE-FF");   // ❌ InvalidArgument
// ✅ u"AA:BB:CC:DD:EE:FF"

// 7. Scan from the UI thread
auto devices = knst_bt::bluetooth::scan(8);   // ❌ 8-second blocking

// 8. RSSI outside discovery
knst_bt::bluetooth::rssi(addr);   // ❌ NotFound (discovery ended)

// 9. Windows adapter rename
knst_bt::bluetooth::set_adapter_name(u"New");   // ❌ NotSupported

// 10. Android block
knst_bt::bluetooth::block(addr, true);   // ❌ NotSupported
```

---

## 🔥 12) Real-World Examples

### Example 1: Find the nearest device

```cpp
knst_bt::bluetooth::set_power(true);

auto devices = knst_bt::bluetooth::scan(10);
if (!devices) return;

// Find the strongest signal
int32_t best_rssi = -127;
knst_c16string best_addr;
for (auto& d : devices.value) {
    if (d.rssi > best_rssi) {
        best_rssi = d.rssi;
        best_addr = d.address;
    }
}

if (!best_addr.empty()) {
    std::cout << "Nearest: " << best_addr << " (" << best_rssi << " dBm)\n";
}
```

### Example 2: Auto-connect to audio device

```cpp
auto paired = knst_bt::bluetooth::list_paired();
if (paired) {
    for (auto& d : paired.value) {
        auto dd = knst_bt::bluetooth::detail(d.address);
        if (dd && dd.value.device_class.contains(u"audio")) {
            std::cout << "Connecting: " << d.name << "\n";
            knst_bt::bluetooth::connect(d.address);
            break;
        }
    }
}
```

### Example 3: PIN for keyboard

```cpp
knst_bt::bluetooth::pair_with_pin(
    u"AA:BB:CC:DD:EE:FF",
    u"000000",        // 6-digit PIN
    30
);
```

### Example 4: Battery level monitor

```cpp
knst_c16string target = u"AA:BB:CC:DD:EE:FF";

while (running) {
    auto bat = knst_bt::bluetooth::battery(target);
    if (bat) {
        std::cout << "Battery: " << bat.value << "%\n";
    }
    std::this_thread::sleep_for(std::chrono::minutes(5));
}
```

### Example 5: Adapter state monitor

```cpp
while (running) {
    auto ad = knst_bt::bluetooth::adapter();
    if (ad) {
        std::cout << "Adapter: "
                  << (ad.value.powered ? "on" : "off")
                  << "  Discoverable: "
                  << (ad.value.discoverable ? "yes" : "no")
                  << "\n";
    }
    std::this_thread::sleep_for(std::chrono::seconds(10));
}
```

### Example 6: Multi-device status

```cpp
auto all = knst_bt::bluetooth::list();
if (all) {
    for (auto& d : all.value) {
        std::cout << d.address << "  " << d.name
                  << "  paired=" << d.paired
                  << "  connected=" << d.connected
                  << "  rssi=" << d.rssi << "\n";
    }
}
```

### Example 7: Adapter rename

```cpp
#if !defined(KNST_USING_PLATFORM_WINDOWS)
auto r = knst_bt::bluetooth::set_adapter_name(u"My Computer");
if (r) {
    std::cout << "Adapter renamed\n";
}
#endif
```

### Example 8: Disconnect + reconnect

```cpp
auto disc = knst_bt::bluetooth::disconnect(addr);
if (disc) {
    std::cout << "Disconnected\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

knst_bt::bluetooth::reconnect(addr);
```

### Example 9: Wait + retry

```cpp
auto r = knst_bt::bluetooth::pair_and_connect(addr);
if (!r) {
    // Retry
    knst_bt::bluetooth::disconnect(addr);
    knst_bt::bluetooth::wait_for_connection(addr, false, 5000);
    r = knst_bt::bluetooth::connect(addr, 20);
}
```

### Example 10: Device enumerate + filter

```cpp
auto all = knst_bt::bluetooth::list(true);
if (all) {
    for (auto& d : all.value) {
        if (!d.name.empty() && d.rssi > -70) {
            std::cout << "Nearby: " << d.name << " (" << d.rssi << " dBm)\n";
        }
    }
}
```

---

## 📌 13) API Contract Summary

```cpp
namespace knst_bt {

using knst_dev::status;
using knst_dev::result;

// ─── Types ───
struct adapter_info { /* ... */ };
struct device_detail { /* ... */ };

knst_c16string class_major(uint32_t cod) noexcept;

// ─── Bluetooth class ───
class bluetooth {
public:
    using device_list = knst_vector<knst_bluetooth_device>;

    // Runtime
    static bool is_supported() noexcept;
    static bool has_adapter() noexcept;

    // Adapter
    static result<knst_vector<adapter_info>> adapters() noexcept;
    static result<adapter_info> adapter() noexcept;
    static result<adapter_info> adapter(const knst_c16string&) noexcept;
    static result<void> set_power(bool) noexcept;
    static result<void> set_discoverable(bool) noexcept;
    static result<void> set_pairable(bool) noexcept;
    static result<void> set_adapter_name(const knst_c16string&) noexcept;
    static result<void> rfkill_block(bool) noexcept;

    // Discovery
    static result<void> scan_start(uint32_t = 1) noexcept;
    static result<void> scan_stop() noexcept;
    static result<device_list> scan(uint32_t = 8, bool = true) noexcept;
    static result<device_list> list(bool = true) noexcept;
    static result<device_list> list_paired() noexcept;
    static result<device_list> list_connected() noexcept;

    // Info
    static result<knst_bluetooth_device> info(const knst_c16string&) noexcept;
    static result<device_detail> detail(const knst_c16string&) noexcept;
    static result<int32_t> battery(const knst_c16string&) noexcept;
    static result<int32_t> rssi(const knst_c16string&) noexcept;

    // Wait
    static result<void> wait_for_device(const knst_c16string&, uint32_t = 10000) noexcept;
    static result<void> wait_for_connection(const knst_c16string&, bool = true, uint32_t = 15000) noexcept;

    // Device operations
    static result<void> pair(const knst_c16string&, uint32_t = 30) noexcept;
    static result<void> pair_with_pin(const knst_c16string&, const knst_c16string&, uint32_t = 30) noexcept;
    static result<void> trust(const knst_c16string&) noexcept;
    static result<void> untrust(const knst_c16string&) noexcept;
    static result<void> connect(const knst_c16string&, uint32_t = 20) noexcept;
    static result<void> disconnect(const knst_c16string&) noexcept;
    static result<void> remove(const knst_c16string&) noexcept;
    static result<void> block(const knst_c16string&, bool) noexcept;
    static result<void> pair_and_connect(const knst_c16string&) noexcept;
    static result<void> reconnect(const knst_c16string&) noexcept;
};

} // namespace knst_bt
```

---

## 🎯 What It Brings You

`knst_bluetooth` gives you serious advantages in these situations:

- ✅ **3 platforms one API** — BlueZ / Win32 / JNI
- ✅ **Adapter management** — power, discoverable, pairable, rename
- ✅ **Discovery** — scan, list, filter
- ✅ **Device info** — battery, RSSI, class, UUID, manufacturer
- ✅ **Pairing** — Just Works + PIN (3 platforms)
- ✅ **Connect/disconnect** — Linux/Windows (17 service UUIDs)
- ✅ **Idempotent ops** — success if already in the requested state
- ✅ **Verified operation** — set → poll → verify
- ✅ **Wait helpers** — `wait_for_device`, `wait_for_connection`
- ✅ **MAC normalization** — uppercase, `:` separator
- ✅ **ANSI sanitization** — `bluetoothctl` color codes cleaned
- ✅ **Fast-path `has_adapter`** — Linux sysfs
- ✅ **BlueZ error mapping** — string → `status`
- ✅ **`ToolMissing` vs `Timeout` distinction** — `bluetoothctl` missing vs hang
- ✅ **RFKILL recovery** — `set_power` fail → `rfkill unblock` retry
- ✅ **CoD decode** — `class_major()`
- ✅ **`is_fatal`** — errors that won't resolve by waiting return immediately
- ✅ **`knst_devices` integration** — `knst_bluetooth_device` struct
- ✅ **`knst_dev` integration** — `result`, `argv`, `run_tool_raw`

---