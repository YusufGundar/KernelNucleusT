# knst_bluetooth — Bluetooth Kontrol Kütüphanesi

Selamlar! 👋 `knst_bluetooth.hpp`, KernelNucleusT'nin **Bluetooth kontrol** modülü. Tek header'da: adapter yönetimi, discovery, device info (battery/RSSI/class/UUID), pairing (Just Works + PIN), connect/disconnect, trust/block, wait helpers. Linux'ta BlueZ (`bluetoothctl`), Windows'ta Win32 Bluetooth API, Android'de JNI `BluetoothAdapter`.

**Kısaca:** **Synchronous, exception-free, RTTI yok.** Her public çağrı `result<T>` döner. **Fail'ler platform-spesifik olarak dürüstçe raporlanır** — Windows'ta radio power off `NotSupported`, Android'de profile connect/disconnect `NotSupported` gibi. MAC normalizasyonu (uppercase), ANSI sanitizasyon, idempotent op'lar, `wait_for_*` helper'ları hazır.

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

### Platform matrisi

| Özellik | Linux (BlueZ) | Windows (Win32) | Android (JNI) |
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

**Sembol açıklaması:** ✅ = tam destek · ⚠️ = kısmi/koşullu · ❌ = yok

**Kritik notlar:**
- **Windows'ta radio power off YOK.** `BluetoothEnableDiscovery` ve `BluetoothEnableIncomingConnections` var ama bunlar **yeni bağlantıları kısıtlar**, radyoyu **kapatmaz** — cihaz hâlâ yayın yapar. Gerçek power off için **WinRT `Windows.Devices.Radios`** veya **SetupAPI** (`DIF_PROPERTYCHANGE`, admin) gerekir — **header-only library ile mümkün değil**.
- **Android 12+'ta profile connect/disconnect uygulama tarafından tetiklenemez.** `BluetoothProfile.ServiceListener` (Java-side proxy) gerekir. Bu kütüphane sadece **kısa süreli polling** yapar, `NotSupported` dönebilir.
- **Android'de Discoverable/Pairable API'si yok** — sistem ayarlarından yapılır.
- **Android'de Block (blacklist) API'si yok.**
- **Windows'ta RSSI ve battery level Win32 API'de yok.**
- **Windows'ta SetAdapterName yok** — sistem ayarlarından değiştirilir.
- **Linux'ta `bluetoothctl` yoksa hiçbir şey çalışmaz** → `ToolMissing`.

### Bağımlılıklar

| Bağımlılık | Ne için? | Not |
|---|---|---|
| **`knst_dev`** | `status`, `result`, `argv`, `run_tool_raw`, `format` | dahili |
| **`knst_devices`** | `knst_bluetooth_device` struct, `split_kv`, `for_each_line` | dahili |
| **`bluetoothctl`** (Linux) | Tüm operasyonlar | `bluez` paketi |
| **`rfkill`** (Linux) | Block/unblock | `util-linux` |
| **`bthprops.lib`** (Windows) | Win32 BT API | Windows SDK |
| **`KnstBluetoothHelper`** (Android) | JNI bridge | App tarafı |

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **3 platform tek API** | BlueZ / Win32 / JNI |
| **MAC normalizasyonu** | Lowercase/uppercase kabul, uppercase döner |
| **ANSI sanitizasyon** | `bluetoothctl` renk kodlarını temizler |
| **Fast-path `has_adapter`** | Linux'ta sysfs okuma (mikrosaniye) |
| **Idempotent op'lar** | `pair` zaten paired ise success |
| **Verified operation** | Set → poll → verify (settle) |
| **Wait helpers** | `wait_for_device`, `wait_for_connection` |
| **Timeout + `is_fatal`** | Bekleyerek düzelmeyecek hatalar (ToolMissing, NotSupported) anında döner |
| **`bluetoothctl` blocking fix** | `bluetoothd` yoksa `ToolMissing`/`Timeout` ayrımı |
| **Output error mapping** | BlueZ hata string'lerini `status`'a çevirir |
| **PIN pairing** | Linux (script), Windows (auth callback), Android (`setPin`) |
| **Class of Device** | "Computer", "Audio/Video", "Peripheral" |
| **UUID parsing** | BlueZ `UUID: ... (name)` → list |
| **ManufacturerData** | İlk `ManufacturerData Key` |
| **Battery Percentage** | BlueZ ≥5.48 |
| **RFKILL recovery** | `set_power(true)` fail → `rfkill unblock` retry |
| **Windows service-based connect** | 17 service UUID (A2DP, AVRCP, HID, SPP, PANU, PBAP, ...) |

---

## 🚀 Hızlı Başlangıç

### Adapter + scan + connect

```cpp
#include "KernelNucleusT.hpp"

// Adapter var mı?
if (!knst_bt::bluetooth::has_adapter()) {
    std::cerr << "Bluetooth yok\n";
    return 1;
}

// Adapter bilgisi
auto ad = knst_bt::bluetooth::adapter();
if (ad) {
    std::cout << "Adapter: " << ad.value.name
              << " (" << ad.value.address << ")"
              << (ad.value.powered ? " [on]" : " [off]") << "\n";
}

// Adapter'ı aç
knst_bt::bluetooth::set_power(true);

// 8 saniye scan → device listesi
auto devices = knst_bt::bluetooth::scan(8);
if (devices) {
    for (const auto& d : devices.value) {
        std::cout << d.address << "  " << d.name
                  << (d.paired    ? " [paired]"    : "")
                  << (d.connected ? " [connected]" : "") << "\n";
    }
}

// Belirli bir cihaza bağlan
auto r = knst_bt::bluetooth::pair_and_connect(u"AA:BB:CC:DD:EE:FF");
if (!r) {
    std::cerr << knst_dev::status_string(r.st) << "\n";
}
```

### Belirli bir cihazın detayları

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

### PIN ile pairing

```cpp
// Linux: best-effort (Just Works ve trusted cihazlar çalışır)
// Windows: auth callback ile
// Android: setPin (API 24-32)
auto r = knst_bt::bluetooth::pair_with_pin(u"AA:BB:CC:DD:EE:FF", u"0000", 30);
```

---

## 📋 Genel API

| Kategori | Metodlar |
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

## 🎯 1) Enum'lar ve Struct'lar

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
    int32_t battery_percent = -1;        // -1 = bilinmiyor
    int32_t rssi_dbm        = 0;         // 0 = bilinmiyor
    knst_c16string manufacturer;         // "0x004c" gibi
    knst_c16string device_class;         // "audio-headset" / "input-keyboard"
    knst_vector<knst_c16string> uuids;   // Service UUID'ler
    bool blocked = false;
    bool valid   = false;
};
```

**`basic` alanları** (`knst_bluetooth_device` — `knst_devices.hpp`'den):

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

**Döndürdüğü:**
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

**Kullanım:** `knst_bt::class_major(0x240404)` → `"Audio/Video"` (headphones).

---

## 🔌 2) Runtime — `is_supported` / `has_adapter`

### 2.1) `is_supported()`

```cpp
static bool is_supported() noexcept;
```

**Linux:** `bluetoothctl` PATH'te mi?
**Windows:** `BluetoothFindFirstRadio` başarılı mı?
**Android:** Adapter var mı?

**Kullanım:** Uygulama başlangıcında bir kez kontrol et. Yoksa geri kalan çağrılar zaten `ToolMissing` / `NotSupported` / `NotFound` döner.

### 2.2) `has_adapter()`

```cpp
static bool has_adapter() noexcept;
```

**Linux:** `/sys/class/bluetooth/hci*` (mikrosaniye, `bluetoothd` yoksa da çalışır).
**Windows:** `is_supported()` ile aynı.
**Android:** `adapter()` sonucu.

**Neden fast-path?** Linux'ta `bluetoothctl list` **bluetoothd yoksa hang edebilir** (D-Bus timeout yok). Sysfs **anlık** cevap verir.

---

## 🎛️ 3) Adapter İşlemleri

### 3.1) `adapters()` — Tüm adapter'lar

```cpp
static result<knst_vector<adapter_info>> adapters() noexcept;
```

**Linux:** `bluetoothctl list` + her MAC için `show`.
**Windows:** Tek adapter (Win32 kısıtı).
**Android:** Tek adapter.

**Adapter yoksa:** Boş vector (fail değil).

### 3.2) `adapter()` — Default

```cpp
static result<adapter_info> adapter() noexcept;
```

**Linux:** `bluetoothctl show` (default adapter).
**Windows/Android:** Tek adapter.

### 3.3) `adapter(addr)` — Belirli adapter

```cpp
static result<adapter_info> adapter(const knst_c16string& addr) noexcept;
```

**Sadece Linux'ta anlamlı** (çoklu adapter). Windows/Android default'a düşer.

### 3.4) `set_power(bool)`

```cpp
static result<void> set_power(bool on) noexcept;
```

**Linux:**
1. `bluetoothctl power on/off`
2. Verify (settle 2.5s)
3. Fail + `on=true` → `rfkill unblock bluetooth` retry (soft-block recovery)

**Windows:** `NotSupported`.
**Android:** `BluetoothAdapter.enable()` / `disable()` (API 33+'ta deprecated, false döner → `NotSupported`).

### 3.5) `set_discoverable(bool)`

```cpp
static result<void> set_discoverable(bool on) noexcept;
```

**Linux:** `bluetoothctl discoverable on/off` + verify.
**Windows:** `BluetoothEnableDiscovery(hRadio, on)`.
**Android:** `NotSupported` (sistem ayarlarından yapılır).

### 3.6) `set_pairable(bool)`

```cpp
static result<void> set_pairable(bool on) noexcept;
```

**Linux:** `bluetoothctl pairable on/off` + verify.
**Windows:** `BluetoothEnableIncomingConnections(hRadio, on)` — **"pairable" değil, "yeni bağlantı kabul et"** (en yakın Win32 eşdeğeri).
**Android:** `NotSupported`.

### 3.7) `set_adapter_name(name)`

```cpp
static result<void> set_adapter_name(const knst_c16string& name) noexcept;
```

**Kısıtlar:**
- Boş değil
- ≤ 248 **byte** (UTF-8)
- `-` ile başlayamaz (bluetoothctl option olarak yorumlar)
- Control char yok (0x00-0x1F, 0x7F)

**Linux:** `bluetoothctl system-alias <name>` + verify.
**Windows:** `NotSupported`.
**Android:** `BluetoothAdapter.setName()`.

### 3.8) `rfkill_block(bool)`

```cpp
static result<void> rfkill_block(bool block) noexcept;
```

**Linux:** `rfkill block/unblock bluetooth`.
**Windows/Android:** `NotSupported`.

**Not:** Linux'ta `set_power(true)` fail ederse **otomatik olarak** `rfkill unblock` denenir. Manuel çağrı opsiyonel.

---

## 📡 4) Discovery

### 4.1) `scan_start(seconds)`

```cpp
static result<void> scan_start(uint32_t seconds = 1) noexcept;
```

**Kısıtlar:** `seconds ∈ [1, 60]`.

**Linux:** `bluetoothctl --timeout=N scan on` — **N saniye bloklar** (Linux'ta discovery process ömrüne bağlı).
**Windows:** No-op success.
**Android:** `startDiscovery()` (async).

**Uyarı:** Linux'ta `scan_start` **`bluetoothctl` bitene kadar bloke eder**. **Sonuç listesi dönmez** — sadece discovery başlatır. Asenkron API istiyorsan `scan()` kullan.

### 4.2) `scan_stop()`

```cpp
static result<void> scan_stop() noexcept;
```

**Linux:** `bluetoothctl scan off` + verify (`Discovering = false`).
**Windows:** No-op success.
**Android:** `stopDiscovery()`.

**Not:** Linux'ta `scan off` **sahip session yoksa hata verir** — ama sonuç **discovery duruyor mu** üzerinden doğrulanır.

### 4.3) `scan(seconds, detailed)`

```cpp
static result<device_list> scan(uint32_t seconds = 8, bool detailed = true) noexcept;
```

**Ne yapar?**
1. Adapter açık mı kontrol et
2. `bluetoothctl --timeout=N scan on` — **N saniye bloklar**
3. `list(detailed)` ile sonuçları döner

**Kısıtlar:** `seconds ∈ [1, 60]`.

**Notlar:**
- **Bloke eder** — UI thread'inden çağırma.
- **Adapter kapalıysa:** `BadState`.
- **Timeout:** Process kill edilirse `Timeout` — **ama device cache hâlâ geçerli**, `list()` çalışır.
- **`detailed = true`:** Her cihaz için `info()` çağrılır (yavaş ama tam bilgi).

**Windows:** `fIssueInquiry = TRUE` (Bluetooth inquiry).
**Android:** `startDiscovery` + `sleep(N)` + `stopDiscovery` + `getDiscoveredDevices`.

### 4.4) `list(detailed)`

```cpp
static result<device_list> list(bool detailed = true) noexcept;
```

**Linux:** `bluetoothctl devices` — cache'teki tüm cihazlar (discovered + paired + connected).
**Windows:** `BluetoothFindFirstDevice` + `fReturnUnknown=TRUE`.
**Android:** `getBondedDevices()` (Android'de "cache" yok, sadece paired).

**`detailed = true`:** Her cihaz için `info()` → tam bilgi.

### 4.5) `list_paired()`

```cpp
static result<device_list> list_paired() noexcept;
```

**Linux:** `bluetoothctl devices Paired` (BlueZ ≥5.65) — eski sürümlerde filter ignore, `paired` flag `info()`'dan doğrulanır.
**Windows:** `fReturnAuthenticated = TRUE`.
**Android:** `getBondedDevices()`.

### 4.6) `list_connected()`

```cpp
static result<device_list> list_connected() noexcept;
```

**Linux:** `bluetoothctl devices Connected` + `info()` verify.
**Windows:** `fReturnConnected = TRUE`.
**Android:** Paired list'ten `connected` filtrelenir.

---

## 🔍 5) Device Info

### 5.1) `info(addr)`

```cpp
static result<knst_bluetooth_device> info(const knst_c16string& addr) noexcept;
```

**Döndürdüğü:** Basic bilgi (name, paired, connected, rssi, type).

**Hata durumları:**
- `InvalidArgument` — MAC geçersiz
- `NotFound` — cihaz BlueZ cache'inde yok
- `ToolMissing` — `bluetoothctl` yok (Linux)
- `Timeout` — bluetoothd yok (Linux)

### 5.2) `detail(addr)`

```cpp
static result<device_detail> detail(const knst_c16string& addr) noexcept;
```

**Döndürdüğü:** Basic + battery, rssi, manufacturer, device_class, uuids, blocked.

**Linux:** `bluetoothctl info <mac>` parse edilir:
- `Name`, `Alias`
- `Paired: yes`, `Trusted: yes`, `Blocked: no`, `Connected: yes`
- `RSSI: 0xffffffc4 (-60)` → `rssi_dbm = -60`
- `Icon: audio-card` → `device_class`
- `Class: 0x240404` → `class_major()`
- `Battery Percentage: 0x64 (100)` → `battery_percent = 100`
- `ManufacturerData Key: 0x004c` → `manufacturer`
- `UUID: ... (name)` → `uuids.push_back(name)`

**Windows:** Basic + type. Battery/RSSI/Class **`NotSupported`**.
**Android:** Basic + battery (via `getBatteryLevel`).

### 5.3) `battery(addr)`

```cpp
static result<int32_t> battery(const knst_c16string& addr) noexcept;
```

**Linux:** BlueZ ≥5.48 — `Battery Percentage` alanı.
**Windows:** `NotSupported`.
**Android:** `getBatteryLevel()`.

**`NotFound`:** Cihaz var ama battery bilgisi yok.

### 5.4) `rssi(addr)`

```cpp
static result<int32_t> rssi(const knst_c16string& addr) noexcept;
```

**Linux:** RSSI **sadece discovery sırasında** BlueZ tarafından raporlanır. Discovery bitince `rssi_dbm = 0` → `NotFound`.
**Windows/Android:** `NotSupported`.

**Kullanım:** Gerçek RSSI için **discovery aktifken** `info()` çağır.

---

## ⏳ 6) Wait Helpers

### 6.1) `wait_for_device(addr, timeout_ms)`

```cpp
static result<void> wait_for_device(const knst_c16string& addr,
                                     uint32_t timeout_ms = 10000) noexcept;
```

**Ne yapar?** `info(addr)` `Ok` dönene kadar poll eder.

**Not:** **Scan başlatmaz**. Cihaz cache'te değilse, önce `scan()` çağır.

**Timeout default:** 10 saniye.

### 6.2) `wait_for_connection(addr, want_connected, timeout_ms)`

```cpp
static result<void> wait_for_connection(const knst_c16string& addr,
                                         bool want_connected = true,
                                         uint32_t timeout_ms = 15000) noexcept;
```

**Ne yapar?** `info(addr).connected == want_connected` olana kadar poll eder.

**`want_connected = false`** — Disconnect bekler. Cihaz **cache'ten kaybolursa** (bluetoothd tarafından unutulursa) → **başarı** sayılır (o zaten disconnected).

**Timeout default:** 15 saniye.

**`is_fatal` handling:** `ToolMissing`, `NotSupported`, `PermissionDenied`, `InvalidArgument` → **hemen fail** (beklemek anlamsız).

---

## 🎯 7) Device Operations

Tüm op'lar **idempotent** — zaten istenen state'te ise success döner.

### 7.1) `pair(addr, timeout_s)`

```cpp
static result<void> pair(const knst_c16string& addr, uint32_t timeout_s = 30) noexcept;
```

**Linux:** `bluetoothctl pair <mac>` + settle + verify (`paired == true`).
**Windows:** Sistem **pairing dialog**'unu açar (`BluetoothAuthenticateDeviceEx`). Zaten paired ise anında success (`ERROR_NO_MORE_ITEMS` da success).
**Android:** `createBond()` + poll (bond state `12 = BONDED`).

**Kısıtlar:** `timeout_s ∈ [1, 120]` (Windows/Android: `[1, 120]`, Linux: `[1, 120]`).

**PIN gerekiyorsa:** `pair_with_pin` kullan.

### 7.2) `pair_with_pin(addr, pin, timeout_s)`

```cpp
static result<void> pair_with_pin(const knst_c16string& addr,
                                   const knst_c16string& pin,
                                   uint32_t timeout_s = 30) noexcept;
```

**PIN validasyonu:** `is_safe_token(pin, 16)` — `[a-zA-Z0-9_-@.:]`, `-` ile başlayamaz, ≤16 char.

#### Linux (best-effort)

**Nasıl çalışır?** `bluetoothctl` **`pincode` komutu yok** — PIN'i agent prompt'unda **yazmak** gerekir. Script:
1. `agent KeyboardDisplay` + `default-agent` register
2. `pair <mac>` başlat
3. 4 saniye bekle
4. PIN'i feed et
5. `yes` feed et (passkey confirm)
6. `Paired: yes` olana kadar poll
7. `quit`

**Sınırlamalar:**
- **Just Works** ve **zaten trusted** cihazlar güvenilir.
- **Passkey gösteren** bazı klavyeler/fareler manuel `bluetoothctl` gerektirebilir.
- Script **`sh` üzerinden** çalışır (bağımlılık).

#### Windows (auth callback)

`BluetoothRegisterForAuthenticationEx` + `BluetoothAuthenticateDeviceEx`:
- **Legacy pairing** (`0x01`) → PIN callback'e verilir
- **Numeric comparison** (`0x02`) → `negativeResponse = FALSE` (passkey auto-confirm)
- **Just Works** (`0x04`, `0x06`) → auto-confirm
- Diğer metodlar → `negativeResponse = TRUE` (reject)

#### Android (deprecated `setPin`)

`BluetoothDevice.setPin(byte[])` — **API 33+'ta deprecated** ama **API 24-32'de** çalışır. Sonrasında `createBond()` + poll.

### 7.3) `trust(addr)` / `untrust(addr)`

```cpp
static result<void> trust(const knst_c16string& addr) noexcept;
static result<void> untrust(const knst_c16string& addr) noexcept;
```

**Linux:** `bluetoothctl trust/untrust <mac>` + verify.
**Windows:** **No-op success** (Win32'de explicit trust flag yok — pair olmak = trusted).
**Android:** **No-op success** (bond = trust).

### 7.4) `connect(addr, timeout_s)`

```cpp
static result<void> connect(const knst_c16string& addr, uint32_t timeout_s = 20) noexcept;
```

**Linux:** `bluetoothctl connect <mac>` + settle + verify (`connected == true`).
**Windows:** **17 service UUID** enable edilir:
- Audio: AudioSink (0x110B), AudioSource (0x110A), AVRCP (0x110C/0x110E), Headset (0x1108/0x1112), Handsfree (0x111E/0x111F)
- Input: HID (0x1124)
- Data: SPP (0x1101), OBEX Object Push (0x1105)
- Network: PANU (0x1115), NAP (0x1116), GN (0x1117)
- Phone: PBAP (0x112F/0x1130), SIM Access (0x112D)
- **Herhangi biri başarılı** → `ERROR_SUCCESS`
**Android:** **`NotSupported`** (profile proxy Java-side gerekir).

**Timeout default:** 20 saniye.

### 7.5) `disconnect(addr)`

```cpp
static result<void> disconnect(const knst_c16string& addr) noexcept;
```

**Linux:** `bluetoothctl disconnect <mac>` + verify.
**Windows:** Aynı 17 service UUID **disable** + verify.
**Android:** `NotSupported` (profile proxy).

### 7.6) `remove(addr)` — Unpair

```cpp
static result<void> remove(const knst_c16string& addr) noexcept;
```

**Linux:** `bluetoothctl remove <mac>` + settle (device cache'ten kaybolana kadar).
**Windows:** `BluetoothRemoveDevice()`.
**Android:** `removeBond()` (helper).

**Idempotent:** Cihaz zaten yoksa → **success**.

### 7.7) `block(addr, blocked)`

```cpp
static result<void> block(const knst_c16string& addr, bool blocked) noexcept;
```

**Linux:** `bluetoothctl block/unblock <mac>` + verify.
**Windows/Android:** `NotSupported`.

**Kullanım:** BlueZ'e "bu cihazı asla kabul etme" der.

### 7.8) `pair_and_connect(addr)`

```cpp
static result<void> pair_and_connect(const knst_c16string& addr) noexcept;
```

**Sıra:**
1. `pair(addr)` — fail olsa bile `paired` kontrol et
2. `trust(addr)` — best-effort (ignore fail)
3. `connect(addr)`

**Windows:** `pair` → `connect`.
**Android:** Sadece `pair` (connect `NotSupported`).

### 7.9) `reconnect(addr)`

```cpp
static result<void> reconnect(const knst_c16string& addr) noexcept;
```

**Sıra:**
1. `disconnect(addr)` — best-effort (zaten disconnected ise OK)
2. `wait_for_connection(addr, false, 5000)` — disconnect settle
3. `connect(addr, 20)`

**Kullanım:** Bağlantı bozulduysa "sil ve tekrar bağlan".

---

## 🖥️ 8) Platform-Spesifik Backend'ler

### 8.1) Linux (BlueZ)

**Kullandığı API'ler:**
- **`bluetoothctl`** — tüm operasyonlar (`show`, `list`, `devices`, `info`, `pair`, `connect`, `trust`, `power`, `discoverable`, `pairable`, `scan`, `system-alias`, `remove`, `block`, `unblock`)
- **`rfkill`** — block/unblock
- **sysfs** (`/sys/class/bluetooth/hci*`) — fast-path `has_adapter`

**Özel durumlar:**

**ANSI sanitizasyon:** `bluetoothctl` renk kodları (`\033[...m`), carriage return, readline marker'ları (`\001 \002`) içerir. `sanitize()` **temizler**.

**Prompt strip:** `[bluetooth]# ` prefix'i `prep_line()` ile atılır.

**`bluetoothd` yoksa:** `bluetoothctl` **hangi edebilir** (D-Bus timeout yok). `run_tool_raw` **timeout** ile korunur (`ToolMissing` vs `Timeout` ayrımı `tool_in_path` ile).

**Error mapping:** BlueZ hata string'leri:
- `"No default controller"` → `NotFound`
- `"org.bluez.Error.InProgress"` → `Busy`
- `"org.bluez.Error.NotReady"` / `"Blocked through rfkill"` → `BadState`
- `"org.bluez.Error.NotPermitted"` / `"AuthenticationRejected"` → `PermissionDenied`
- `"org.bluez.Error.NotSupported"` → `NotSupported`
- `"AuthenticationTimeout"` / `"page-timeout"` → `Timeout`
- Diğerleri → fallback

**`class_major()` decode:** BlueZ `Class:` alanı `0x240404` gibi hex → `(cod >> 8) & 0x1F` major class.

**UUID parse:** `UUID: <uuid> (<name>)` — `(...)` içindeki name alınır (ör. `Audio Sink`).

**`ManufacturerData Key:`** — ilk key (hex) `manufacturer` alanına.

### 8.2) Windows (Win32 Bluetooth)

**Kullandığı API'ler:**
- `BluetoothFindFirstRadio` — adapter bul
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

**Sınırlamalar:**
- **Power off `NotSupported`** — Win32 radyo kapatma API'si yok. WinRT `Windows.Devices.Radios` veya SetupAPI gerekir.
- **SetAdapterName `NotSupported`** — sistem ayarlarından.
- **Block `NotSupported`** — Win32 blacklist API'si yok.
- **RSSI/battery `NotSupported`** — Win32'de yok.
- **PIN pairing** — auth callback ile (16 char limit `BTH_MAX_PIN_SIZE`).

**Service UUID listesi** (connect için):
- 17 UUID (Audio, Input, Data, Network, Phone)

**Error mapping:** `win_status(DWORD)`:
- `ERROR_ACCESS_DENIED` → `PermissionDenied`
- `ERROR_NOT_FOUND` → `NotFound`
- `ERROR_NOT_SUPPORTED` → `NotSupported`
- `ERROR_INVALID_PARAMETER` → `InvalidArgument`
- `WAIT_TIMEOUT` → `Timeout`
- `ERROR_DEVICE_NOT_CONNECTED` → `BadState`

### 8.3) Android (JNI BluetoothAdapter)

**Kullandığı API'ler (JNI üzerinden):**
- `BluetoothAdapter.getDefaultAdapter()` — adapter
- `BluetoothAdapter.isEnabled()` — power state
- `BluetoothAdapter.enable()` / `disable()` — power toggle (API 33+'ta deprecated)
- `BluetoothAdapter.getBondedDevices()` — paired list
- `BluetoothAdapter.startDiscovery()` / `stopDiscovery()` — scan
- `BluetoothAdapter.setName()` — adapter rename
- `BluetoothDevice.getName()` / `getAddress()` / `getType()` / `getBondState()` / `getBleState()` — device info
- `BluetoothDevice.setPin(byte[])` — PIN (deprecated)
- `BluetoothDevice.createBond()` — pair
- `BluetoothDevice.removeBond()` — unpair

**Helper class:** `com.knst.devices.KnstBluetoothHelper`
- `getAdapterInfo()` → `[String, String, int, int]`
- `getBondedDevices()` → `[name, address, type, ..., bondState, connectionState] × N` (6 slot per device)
- `getDeviceInfo(addr)` → aynı format (1 device)
- `getDiscoveredDevices()` → scan sonuçları
- `getBatteryLevel(addr)` → int (-1 = yok)
- `startDiscovery()` / `stopDiscovery()`
- `createBond(addr)` / `removeBond(addr)`
- `setPin(addr, pin)` — PIN setting (API 24-32)

**JNI Context:** `KnstDevicesHelper.setContext(activity)` daha önce çağrılmalı.

**Sınırlamalar:**
- **Discoverable/Pairable `NotSupported`** — sistem ayarları.
- **Connect/Disconnect `NotSupported`** — profile proxy Java-side.
- **Block `NotSupported`** — API yok.
- **RSSI `NotSupported`** — sadece `BluetoothGatt` üzerinden (BLE) alınabilir, classic BT için yok.
- **Power off** — API 33+'ta `enable()/disable()` false döner → `NotSupported`.

---

## ⚠️ 9) Sık Tuzaklar

### 1) Linux'ta `bluetoothctl` yoksa hiçbir şey çalışmaz

`is_supported()` → `false`, tüm op'lar `ToolMissing` döner.

**Fix:** `sudo apt install bluez` (Debian/Ubuntu) / `sudo dnf install bluez` (Fedora).

### 2) Linux'ta `bluetoothd` yoksa `Timeout`

`bluetoothctl` **bluetoothd olmadan hang eder**. `run_btctl` timeout ile korunur ama:
- `tool_in_path` → `Timeout`
- `!tool_in_path` → `ToolMissing`

**Fix:** `sudo systemctl enable --now bluetooth`.

### 3) Windows power off gerçekten yok

```cpp
knst_bt::bluetooth::set_power(false);   // NotSupported
```

**Win32 radyo kapatma API'si yok.** Sadece:
- `set_discoverable(false)` — discovery kapat
- `set_pairable(false)` — yeni bağlantı reddet

Ama cihaz **hâlâ yayın yapar**. Gerçek power off için **WinRT** veya **SetupAPI** gerekir.

### 4) Android profile connect/disconnect yok

```cpp
knst_bt::bluetooth::connect(addr);   // NotSupported (API 12+)
```

**Android 12+'ta** uygulama `A2DP`/`HFP` gibi profil bağlantılarını **tetikleyemez**. `BluetoothProfile.ServiceListener` (Java-side) gerekir.

**Alternatif:** `pair()` sonrası sistem otomatik bağlanır. Kütüphane kısa süreli polling yapar.

### 5) RSSI sadece discovery sırasında

```cpp
// Discovery bitmişse:
knst_bt::bluetooth::rssi(addr);   // NotFound (rssi_dbm = 0)
```

**Fix:** `scan()` sırasında veya hemen sonrasında `info()` çağır.

### 6) Windows'ta battery yok

```cpp
knst_bt::bluetooth::battery(addr);   // NotSupported
```

**Win32 BT API'si battery raporlamaz.** BLE GATT üzerinden okunabilir ama classic BT'de yok.

### 7) MAC normalizasyonu

```cpp
knst_bt::bluetooth::pair(u"aa:bb:cc:dd:ee:ff");   // ✅ OK (auto uppercase)
knst_bt::bluetooth::pair(u"AA-BB-CC-DD-EE-FF");   // ❌ InvalidArgument
```

**Sadece `:` separator** kabul edilir. `is_mac()` kontrolü.

### 8) `scan()` blocking

```cpp
auto devices = knst_bt::bluetooth::scan(8);   // 8 saniye bloke
```

**UI thread'inden çağırma.** Asenkron gerekirse `scan_start(seconds)` + timer + `list()`.

### 9) `pair_with_pin` best-effort (Linux)

```cpp
knst_bt::bluetooth::pair_with_pin(addr, u"0000");
```

**Just Works** ve **trusted** cihazlar çalışır. **Passkey** gösteren cihazlar **manuel `bluetoothctl`** gerektirebilir.

**Fix:** Başarısız olursa manuel `bluetoothctl` ile dene.

### 10) Android `setPin` deprecated

**API 33+'ta** `BluetoothDevice.setPin` **deprecated** ama hâlâ çalışır (bazı cihazlarda). Yeni API'de `setPairingConfirmation` var.

**Fix:** API 33+'ta `pair()` (Just Works) kullan.

### 11) `pair()` timeout (Linux)

```cpp
auto r = knst_bt::bluetooth::pair(addr, 30);
// 30 saniye sonra → Timeout
```

**PIN beklenir** — sizin tarafınızdan confirm yok. `pair_with_pin` kullan veya manuel `bluetoothctl` aç.

### 12) `wait_for_connection` `NotFound` handling

```cpp
wait_for_connection(addr, false, 5000);
// Cihaz cache'ten kaybolursa → success (zaten disconnected)
```

**Neden?** `pair` sırasında cihaz unutulursa, disconnect beklemesi anlamsız — zaten disconnected.

### 13) `is_fatal` — Beklemeye gerek yok

```cpp
// ToolMissing, NotSupported, PermissionDenied, InvalidArgument → hemen fail
```

**`wait_until` bunları görür görmez fail eder** — boşuna timeout beklemez.

### 14) `bluetoothctl` ANSI kodları

```cpp
// Sanitize edilmiş çıktı:
"[bluetooth]# Controller AA:BB:CC:DD:EE:FF My PC"
```

`sanitize()` ESC kodlarını, `\r` ve kontrol char'larını temizler. `prep_line()` prompt prefix'i atar.

### 15) Adapter kapalıysa scan

```cpp
knst_bt::bluetooth::scan(8);   // BadState
```

**Fix:** Önce `set_power(true)`.

### 16) `set_adapter_name` UTF-8 byte limiti

```cpp
knst_bt::bluetooth::set_adapter_name(u"日本語");   // 9 byte — OK
knst_bt::bluetooth::set_adapter_name(u"-bad");     // InvalidArgument ('-' ile başlar)
```

**BlueZ limit:** 248 **byte** (UTF-8). `knst_c16string.length()` değil, `to_utf8().length()`.

### 17) `pair_and_connect` partial success

```cpp
knst_bt::bluetooth::pair_and_connect(addr);
// pair başarılı, connect fail → pair success kalır
```

**Trust:** Best-effort, ignore edilir.
**Connect fail:** `pair` success olsa bile **fail döner**.

### 18) `reconnect` disconnect bekleme

```cpp
knst_bt::bluetooth::reconnect(addr);
// 1. disconnect (best-effort)
// 2. wait_for_connection(addr, false, 5000) — 5 saniye max
// 3. connect(addr, 20)
```

**Toplam max:** ~25 saniye.

### 19) Android adapter rename `setName`

```cpp
knst_bt::bluetooth::set_adapter_name(u"My Phone");   // ✅
```

**Android'de çalışır** (`BluetoothAdapter.setName`) — **sistem izinleri** gerekir (`BLUETOOTH_CONNECT` API 31+).

### 20) `detail()` RSSI = 0 (bilinmiyor)

```cpp
dd.rssi_dbm = 0;   // Discovery sırasında değilse
```

**`0` = bilinmiyor**, gerçek RSSI değil. `rssi()` → `NotFound` döner.

### 21) `has_adapter()` Linux fast-path

```cpp
// /sys/class/bluetooth/hci* (mikrosaniye)
// "hci0:256" (handle) skip, sadece "hci0" (adapter)
```

**Neden?** `bluetoothctl list` **bluetoothd yoksa hang eder**. Sysfs her zaman cevap verir.

### 22) Adapter olmasa bile `list()` boş döner

```cpp
knst_bt::bluetooth::list();   // Boş vector
```

**Not fail değil** — adapter yoksa hiç cihaz yok. Boş vector döner.

### 23) `pair_with_pin` shell script injection

```cpp
// Mac / pin / wait as positional parameters
// Script içine interpolate EDILMEZ
a << "-c" << kScript << "sh" << mac << pin << timeout_s;
```

**Güvenli.** `is_safe_token(pin, 16)` validasyonu + positional arg.

### 24) Windows `BluetoothEnableIncomingConnections` != powered

```cpp
set_pairable(false);   // Yeni bağlantı reddedilir, radyo kapanmaz
```

**Pairable ve powered ayrı kavramlar.** Windows'ta powered toggle yok.

### 25) Android `BluetoothAdapter.enable()` API 33+

```cpp
// API 33+:
bluetooth.set_power(true);   // NotSupported
```

**Android 13+**'ta `BluetoothAdapter.enable()` **deprecated** ve çoğu cihazda `false` döner. Sistem ayarları gerekir.

### 26) UUID parse `UUID: <uuid> (<name>)` — 64 char limit

```cpp
// Parentez içindeki name max 63 char
const size_t n = close - open - 1;
if (n < 64) { ... }
```

**Uzun isimler atlanır.**

### 27) `ManufacturerData Key:` ilk değer

```cpp
dd.manufacturer = "0x004c";   // İlk key
```

**Birden fazla varsa sadece ilk.**

### 28) Windows `pair()` dialog blocking

```cpp
knst_bt::bluetooth::pair(addr, 30);
// Dialog 30 saniye gösterilir, kullanıcı onay/red verir
```

**Dialog modal** — kullanıcı beklemeden dönmez. `timeout_s = 30` → dialog timeout.

### 29) Windows `pair_with_pin` register/unregister

```cpp
BluetoothRegisterForAuthenticationEx(...)
BluetoothAuthenticateDeviceEx(...)
// poll callback + device
BluetoothUnregisterAuthentication(reg)
```

**Callback sırasında** `BluetoothGetDeviceInfo` poll edilir — pair tamamlandı mı?

### 30) `remove()` cache temizliği

```cpp
knst_bt::bluetooth::remove(addr);
// bluetoothctl remove → cache'ten temizleme
// settle ile poll: device kayboldu mu?
```

**`NotFound` = zaten silinmiş → success.**

### 31) `scan()` device cache

```cpp
knst_bt::bluetooth::scan(8);
// 8 saniye scan → bluetoothd cache'ine eklenir
// list() bu cache'ten okur
```

**Bluetoothd restart** → cache temizlenir.

### 32) `wait_for_device` scan gerektirir

```cpp
knst_bt::bluetooth::wait_for_device(addr, 10000);
// Cihaz cache'te değilse → 10 saniye sonra Timeout
```

**Fix:** Önce `scan()` → sonra `wait_for_device`.

### 33) `device_class` Icon vs Class

```cpp
// İki alan:
dd.device_class = "audio-headset";   // BlueZ Icon (semantic)
// veya
dd.device_class = "Audio/Video";     // Class major
```

**BlueZ `Icon:` varsa** o kullanılır, yoksa `class_major()` decode.

### 34) `paired` vs `bonded` (Android)

```cpp
d.paired = (bondState == 12);   // BOND_BONDED
```

**Android'de "bond" = "paired"**. `bondState == 11` = BONDING (in progress).

### 35) `connected` (Android `getBleState` değil)

```cpp
// Classic BT için:
d.connected = (connectionState == 1);   // STATE_CONNECTED
```

**BLE için** `BluetoothGatt` ayrı API — bu kütüphanede yok.

### 36) Windows device `fRemembered` vs `fAuthenticated`

```cpp
d.paired  = di.fAuthenticated;   // Pair tamamlandı
d.trusted = di.fRemembered;      // Sistem hatırlıyor
```

**İkisi farklı** — paired cihaz trusted olmayabilir (untrust edilmiş).

### 37) `untrust` Windows no-op

```cpp
knst_bt::bluetooth::untrust(addr);   // success (no-op)
```

**Win32'de explicit untrust yok** — paired = trusted.

### 38) `set_power` Linux'ta rfkill retry

```cpp
set_power(true);
// 1. bluetoothctl power on → fail (rfkill soft-block)
// 2. rfkill unblock bluetooth
// 3. bluetoothctl power on → retry
```

**Otomatik** — kullanıcı `rfkill_block(false)` çağırmasa bile.

### 39) `class_major()` 5-bit major class

```cpp
(cod >> 8) & 0x1F   // Bit 8-12
```

**Bluetooth CoD spec:** Major class bits 8-12. Örnek `0x240404` → `(0x240404 >> 8) & 0x1F = 0x04` = Audio/Video.

### 40) Timeout + `is_fatal` kombinasyonu

```cpp
// wait_until:
// - ToolMissing / NotSupported / PermissionDenied / InvalidArgument → fail hemen
// - Diğer → deadline'a kadar poll
```

**Permission denied** için 10 saniye beklemek anlamsız — hemen döner.

### 41) Adapter rename Windows'ta yok

```cpp
knst_bt::bluetooth::set_adapter_name(u"My PC");   // NotSupported
```

**Win32 BT API'si adapter isim değiştirmeyi desteklemez.** Sistem ayarları gerekir.

### 42) Android block yok

```cpp
knst_bt::bluetooth::block(addr, true);   // NotSupported
```

**Android'de "blacklist" API'si yok.** Unpair + yeniden pair'lememe ile sağlanır.

### 43) Linux `devices Paired` filter (BlueZ < 5.65)

```cpp
knst_bt::bluetooth::list_paired();
// Eski BlueZ: filter ignore → tüm cihazlar döner
// Ama kütüphane `info()` ile `paired` flag'i doğrular
```

**Sonuç doğru** — sadece gereksiz `info()` çağrıları olur.

### 44) `wait_connection` cache miss (`want_connected = false`)

```cpp
// Cihaz cache'ten kaybolursa (unpair, forget, ...)
// → success (zaten disconnected)
```

**Neden?** Disconnect bekleniyor + cihaz yok = zaten disconnected.

### 45) `scan_start` blocking (Linux)

```cpp
auto r = knst_bt::bluetooth::scan_start(8);   // 8 saniye bloke
```

**`scan()` gibi blocking.** Asenkron API yok.

### 46) `--timeout=N` bluetoothctl flag

```cpp
knst_dev::argv a;
a << knst_dev::format("--timeout=%u", seconds) << "scan" << "on";
```

**`bluetoothctl --timeout=8 scan on`** — 8 saniye sonra otomatik durur.

### 47) `adapter()` default (Linux)

```cpp
knst_bt::bluetooth::adapter();
// bluetoothctl show (default adapter)
```

**Çoklu adapter'da** sadece default döner. Spesifik için `adapter(addr)`.

### 48) `adapters()` fanout (Linux)

```cpp
// 1. bluetoothctl list → MAC listesi
// 2. Her MAC için adapter(mac) → tam bilgi
```

**N adapter** → N+1 çağrı. Yavaş.

### 49) `pair()` `ERROR_NO_MORE_ITEMS` Windows

```cpp
// Zaten paired → BluetoothAuthenticateDeviceEx ERROR_NO_MORE_ITEMS döner
// Kütüphane bunu ERROR_SUCCESS olarak yorumlar
```

**Idempotent behavior.**

### 50) `pin_auth_cb` `MITMProtectionNotRequired`

```cpp
BluetoothAuthenticateDeviceEx(..., MITMProtectionNotRequired);
```

**MITM koruması gerekli değil** — legacy PIN için. Yüksek güvenlik senaryolarında değiştirilebilir.

---

## 📊 10) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| **`is_supported`** | Linux: PATH check (~µs) | Windows/Android: adapter probe (~ms) |
| **`has_adapter`** | ~µs | Sysfs (Linux) / adapter probe |
| **`adapter()`** | 50-200 ms | `bluetoothctl show` |
| **`adapters()`** | 100-500 ms | N+1 çağrı |
| **`set_power`** | 500-8000 ms | Settle 2.5s |
| **`set_discoverable`** | 500-5000 ms | `bluetoothctl` spawn |
| **`scan(8)`** | 8000+ ms | 8 saniye blocking |
| **`scan_start(1)`** | 1000+ ms | Blocking |
| **`list()`** | 100-500 ms | `bluetoothctl devices` |
| **`list_paired()`** | 500-3000 ms | N × `info()` |
| **`info(addr)`** | 100-300 ms | `bluetoothctl info` |
| **`detail(addr)`** | 100-300 ms | Aynı |
| **`pair(addr, 30)`** | 500-30000 ms | Timeout'a bağlı |
| **`pair_with_pin`** | 5000-30000 ms | Script + settle |
| **`connect`** | 2000-20000 ms | Timeout'a bağlı |
| **`disconnect`** | 500-5000 ms | Settle |
| **`remove`** | 500-3000 ms | Settle |
| **`wait_for_device`** | Timeout'a bağlı | Default 10s |
| **`wait_for_connection`** | Timeout'a bağlı | Default 15s |

**Tipik latencies:**
- **Adapter info:** 100-200 ms
- **Scan 8 saniye:** 8-9 saniye
- **Pair Just Works:** 5-15 saniye
- **Connect:** 3-10 saniye
- **Disconnect:** 1-3 saniye

**Optimizasyon:**
- **Fast-path `has_adapter`** (Linux sysfs) — `bluetoothctl` spawn yok
- **`detailed = false`** — `list()` hızlı (N × `info` yok)
- **`scan()` tek çağrı** — `scan_start` + `list` yerine
- **`pair_and_connect`** — tek çağrıda pair + trust + connect

---

## ✅ 11) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Başlangıçta destek kontrolü
if (!knst_bt::bluetooth::is_supported()) return;

// 2. Adapter var mı?
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

### ❌ Yapma

```cpp
// 1. Linux'ta bluetoothd olmadan
// ❌ Timeout döner (bluetoothctl hang)
// ✅ Önce: sudo systemctl enable --now bluetooth

// 2. Windows power off bekle
knst_bt::bluetooth::set_power(false);   // ❌ NotSupported
// ✅ set_discoverable(false) + set_pairable(false)

// 3. Android connect bekle
knst_bt::bluetooth::connect(addr);   // ❌ NotSupported (profile proxy)

// 4. Windows battery bekle
knst_bt::bluetooth::battery(addr);   // ❌ NotSupported

// 5. Android discoverable
knst_bt::bluetooth::set_discoverable(true);   // ❌ NotSupported

// 6. MAC formatı yanlış
knst_bt::bluetooth::pair(u"AA-BB-CC-DD-EE-FF");   // ❌ InvalidArgument
// ✅ u"AA:BB:CC:DD:EE:FF"

// 7. UI thread'inde scan
auto devices = knst_bt::bluetooth::scan(8);   // ❌ 8 saniye blocking

// 8. RSSI discovery dışında
knst_bt::bluetooth::rssi(addr);   // ❌ NotFound (discovery bitmiş)

// 9. Windows adapter rename
knst_bt::bluetooth::set_adapter_name(u"New");   // ❌ NotSupported

// 10. Android block
knst_bt::bluetooth::block(addr, true);   // ❌ NotSupported
```

---

## 🔥 12) Gerçek Kullanım Örnekleri

### Örnek 1: En yakın cihazı bul

```cpp
knst_bt::bluetooth::set_power(true);

auto devices = knst_bt::bluetooth::scan(10);
if (!devices) return;

// En güçlü sinyali bul
int32_t best_rssi = -127;
knst_c16string best_addr;
for (auto& d : devices.value) {
    if (d.rssi > best_rssi) {
        best_rssi = d.rssi;
        best_addr = d.address;
    }
}

if (!best_addr.empty()) {
    std::cout << "En yakın: " << best_addr << " (" << best_rssi << " dBm)\n";
}
```

### Örnek 2: Ses cihazına otomatik bağlan

```cpp
auto paired = knst_bt::bluetooth::list_paired();
if (paired) {
    for (auto& d : paired.value) {
        auto dd = knst_bt::bluetooth::detail(d.address);
        if (dd && dd.value.device_class.contains(u"audio")) {
            std::cout << "Bağlanıyor: " << d.name << "\n";
            knst_bt::bluetooth::connect(d.address);
            break;
        }
    }
}
```

### Örnek 3: Klavye için PIN

```cpp
knst_bt::bluetooth::pair_with_pin(
    u"AA:BB:CC:DD:EE:FF",
    u"000000",        // 6 haneli PIN
    30
);
```

### Örnek 4: Pil seviyesi monitörü

```cpp
knst_c16string target = u"AA:BB:CC:DD:EE:FF";

while (running) {
    auto bat = knst_bt::bluetooth::battery(target);
    if (bat) {
        std::cout << "Pil: " << bat.value << "%\n";
    }
    std::this_thread::sleep_for(std::chrono::minutes(5));
}
```

### Örnek 5: Adapter durum monitörü

```cpp
while (running) {
    auto ad = knst_bt::bluetooth::adapter();
    if (ad) {
        std::cout << "Adapter: "
                  << (ad.value.powered ? "açık" : "kapalı")
                  << "  Discoverable: "
                  << (ad.value.discoverable ? "evet" : "hayır")
                  << "\n";
    }
    std::this_thread::sleep_for(std::chrono::seconds(10));
}
```

### Örnek 6: Çoklu cihaz durum

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

### Örnek 7: Adapter rename

```cpp
#if !defined(KNST_USING_PLATFORM_WINDOWS)
auto r = knst_bt::bluetooth::set_adapter_name(u"My Computer");
if (r) {
    std::cout << "Adapter yeniden adlandırıldı\n";
}
#endif
```

### Örnek 8: Disconnect + reconnect

```cpp
auto disc = knst_bt::bluetooth::disconnect(addr);
if (disc) {
    std::cout << "Bağlantı kesildi\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

knst_bt::bluetooth::reconnect(addr);
```

### Örnek 9: Wait + retry

```cpp
auto r = knst_bt::bluetooth::pair_and_connect(addr);
if (!r) {
    // Retry
    knst_bt::bluetooth::disconnect(addr);
    knst_bt::bluetooth::wait_for_connection(addr, false, 5000);
    r = knst_bt::bluetooth::connect(addr, 20);
}
```

### Örnek 10: Cihaz enumerate + filter

```cpp
auto all = knst_bt::bluetooth::list(true);
if (all) {
    for (auto& d : all.value) {
        if (!d.name.empty() && d.rssi > -70) {
            std::cout << "Yakın: " << d.name << " (" << d.rssi << " dBm)\n";
        }
    }
}
```

---

## 📌 13) API Kontrat Özeti

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

## 🎯 Size Getireceği Kazanç

`knst_bluetooth` şu durumlarda ciddi avantaj sağlar:

- ✅ **3 platform tek API** — BlueZ / Win32 / JNI
- ✅ **Adapter yönetimi** — power, discoverable, pairable, rename
- ✅ **Discovery** — scan, list, filter
- ✅ **Device info** — battery, RSSI, class, UUID, manufacturer
- ✅ **Pairing** — Just Works + PIN (3 platform)
- ✅ **Connect/disconnect** — Linux/Windows (17 service UUID)
- ✅ **Idempotent op'lar** — zaten istenen state'te ise success
- ✅ **Verified operation** — set → poll → verify
- ✅ **Wait helpers** — `wait_for_device`, `wait_for_connection`
- ✅ **MAC normalizasyonu** — uppercase, `:` separator
- ✅ **ANSI sanitizasyon** — `bluetoothctl` renk kodları temizlenir
- ✅ **Fast-path `has_adapter`** — Linux sysfs
- ✅ **BlueZ error mapping** — string → `status`
- ✅ **`ToolMissing` vs `Timeout` ayrımı** — `bluetoothctl` yok vs hang
- ✅ **RFKILL recovery** — `set_power` fail → `rfkill unblock` retry
- ✅ **CoD decode** — `class_major()`
- ✅ **`is_fatal`** — beklemek anlamsız hatalar anında döner
- ✅ **`knst_devices` entegrasyonu** — `knst_bluetooth_device` struct
- ✅ **`knst_dev` entegrasyonu** — `result`, `argv`, `run_tool_raw`

---