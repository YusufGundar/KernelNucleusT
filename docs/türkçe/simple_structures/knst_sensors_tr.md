# knst_sensors — Termal Yönetim ve Fan Kontrol Kütüphanesi

Selamlar! 👋 `knst_sens` namespace'i, `knst_devices::sensors()` üzerine kurulmuş bir **termal yönetim + fan kontrol** framework'ü. Sıcaklık sensörlerini periyodik okuyabilir, fanları PWM ile sürebilir, fan curve'leri tanımlayabilir, acil durumlarda hepsini son hıza alabilir ve crash durumunda fanları otomatik olarak güvenli moda döndürebilirsin.

---

## 🚨 GÜVENLİK TASARIMI — ÖNCE BUNU OKU

Fan kontrolü **yanlış kullanıldığında fanı kalıcı olarak durdurabilir**. Bu kütüphane katı güvenlik kuralları uygular:

### 1. `set_pwm(value)` **`min_safe_pwm` altındaki değerleri REDDEDER** (varsayılan 40/255)

- **`value=0` dahil.** `allow_stop` bayrağı **yok**.
- `min_safe_pwm` bile `KNST_SENS_ABSOLUTE_MIN_PWM` (varsayılan 20) altına inemez. `open()` bunu reddeder, controller/zone'lar clamp eder.
- **Tek bypass**: `set_pwm_unsafe()` — `[[deprecated]]` işaretli, kullanma.

### 2. `restore_auto()` **GÜVENLİ**:
- Fanı **mevcut hızının altına asla düşürmez** (`max(min_safe_pwm, 128, current_pwm)` yazar).
- `pwmN_enable`'ı **fan açıldığında hangi moddaysa ona** geri çevirir (2..5, örneğin nct67xx'te 5 = SmartFan IV). Bilinmiyorsa **2**'ye düşer.
- Modu **doğrular** (bir kez retry), başarısızlığı dürüstçe raporlar.

### 3. `fan_control` sadece **sen istediğinde** donanıma dokunur

- Yıkıcı (destructor) sadece **"dirty"** ise (bu nesne değiştirdiyse) fanı restore eder.
- **Move edilmiş** nesneler asla restore etmez (double / premature restore yok).

### 4. `fan_curve_controller` / `thermal_orchestrator` / `thermal_manager` yıkımda restore eder

### 5. `fan_watchdog` — heartbeat durursa tüm bağlı fanları restore eder, heartbeat geri gelirse **yeniden kurulur**

### 6. Fail-safe davranış:
- **Sensör 3 tick boyunca kayıp / eski / aralık dışı** → zone `failsafe_pwm` (varsayılan 200) ile çalışır, `SensorLost` event fırlatılır.
- **Kritik sıcaklık** → tüm zone'lar 255'e zorlanır, sıcaklık `critical - hysteresis` altına düşene kadar.
- **NaN sıcaklık** → 255.
- **Boş fan curve** → 255.

### 7. Crash safety (opt-in): `install_crash_handlers()`
`SIGINT/SIGTERM/SIGHUP/SIGSEGV/SIGABRT/...` için async-signal-safe handler kurar → proses ölmeden önce her fanı auto'ya döndürür. **SIGKILL ve güç kesintisi yakalanamaz** — onun için systemd + `ExecStopPost=` kullan.

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

| Özellik | Linux | Windows | Android |
|---|---|---|---|
| **Sensör okuma** | ✅ `knst_devices::sensors()` | ✅ | ✅ |
| **Thermal zone** | ✅ `/sys/class/thermal` | ❌ | ⚠️ Kısmi |
| **Fan PWM** | ✅ `/sys/class/hwmon` | ❌ | ❌ |
| **Fan RPM** | ✅ | ❌ | ❌ |
| **Fan enumeration** | ✅ | ❌ | ❌ |
| **Crash handlers** | ✅ POSIX signal | ❌ | ⚠️ Kısmi |
| **Fan control** | ✅ Tam | ⚠️ Stub | ❌ |
| **Watchdog** | ✅ | ⚠️ Stub | ❌ |
| **Orchestrator** | ✅ | ⚠️ Stub | ❌ |
| **Thermal manager** | ✅ | ❌ | ❌ |
| **Sensor filter** | ✅ | ✅ | ✅ |
| **Async monitor** | ✅ | ✅ | ✅ |
| **Event bus** | ✅ | ✅ | ✅ |

**Kritik:** `fan_control` Windows'ta **stub** — `open()` her zaman `NotSupported` döner. Fan kontrolü için Linux gerekli.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Safe PWM** | `min_safe_pwm` altına inmez, fan stop ettirilemez |
| **Safe restore** | Fanı asla yavaşlatmaz, orijinal moda döner |
| **Crash-safe** | Signal handler fanları auto'ya döndürür |
| **Sensor filter** | Median + outlier rejection, bozuk okuma yoksayılır |
| **Async monitor** | Thread-safe periyodik örnekleme + geçmiş |
| **Fan curve** | Interpolation + hysteresis + ramp limiting |
| **Zone orchestration** | Birden fazla fanı tek sıcaklıkla yönet |
| **Watchdog** | Heartbeat durursa fanlar restore edilir |
| **Emergency** | Kritik sıcaklıkta hepsi 255 |
| **Discovery** | Chip + fan enumeration + öneri |
| **Component classification** | Chip adı → CPU/GPU/NVMe vs. |
| **JSON export** | Snapshot'ı JSON'a çevir (`KNST_JSON_HPP`) |

---

## 🚀 Hızlı Başlangıç

### En basit: Fanı %60'ta çalıştır

```cpp
#include "KernelNucleusT.hpp"

auto f = knst_sens::fan_control::open(u"nct6798", 1);   // chip + index
if (f) {
    f.value.set_percent(60);   // %60
}
// scope çıkınca otomatik restore_auto() — güvenli
```

### Sıcaklık oku

```cpp
knst_sens::async_sensor_monitor mon;
mon.sample_once();

auto hottest = mon.hottest();
if (hottest) {
    std::cout << "En sıcak: " << hottest.value.chip << "/" << hottest.value.name
              << " = " << hottest.value.value << "°C\n";
}
```

### Balanced fan curve

```cpp
knst_sens::fan_curve curve = knst_sens::fan_curve::balanced();

// veya kendi curve'ünü tanımla
knst_sens::fan_curve custom;
custom.points = {{40.0, 80}, {60.0, 150}, {80.0, 230}, {95.0, 255}};
custom.hysteresis_c = 3.0;
custom.ramp_step    = 10;
```

### Tam thermal manager

```cpp
knst_sens::thermal_manager mgr;

knst_sens::fan_zone cpu_zone;
cpu_zone.name      = u"cpu";
cpu_zone.temp_chip = u"coretemp";
cpu_zone.curve     = knst_sens::fan_curve::balanced();
cpu_zone.fan_chips = {u"nct6798"};
cpu_zone.fan_indices = {1};
cpu_zone.min_safe_pwm = 40;

mgr.add_zone(std::move(cpu_zone));
mgr.start();
// ... çalış ...
mgr.stop();   // tüm fanları auto'ya döndürür
```

---

## 📋 Genel API

| Kategori | Metotlar |
|---|---|
| **Sensör filtresi** | `sensor_filter::accept`, `reset` |
| **Async monitor** | `start`, `stop`, `sample_once`, `age_ms`, `snapshot`, `stats`, `history`, `find`, `hottest`, `over_limit`, `component_temp`, `reset` |
| **Fan control** | `open`, `set_pwm`, `set_percent`, `set_manual`, `restore_auto`, `get_pwm`, `get_rpm`, `get_pwm_enable` |
| **Fan curve** | `pwm_for`, `normalize`, `silent`, `balanced`, `performance` |
| **Curve controller** | `tick`, `restore_auto`, `current_pwm`, `get_curve`, `set_curve`, `fan` |
| **Event bus** | `subscribe`, `unsubscribe`, `emit` |
| **Emergency** | `check`, `current_level`, `set_blast_callback`, `set_release_callback`, `reset` |
| **Orchestrator** | `add_zone`, `clear`, `zone_count`, `tick`, `emergency_full_blast`, `clear_emergency`, `in_emergency`, `restore_all_auto` |
| **Watchdog** | `attach`, `heartbeat`, `start`, `stop`, `running`, `tripped`, `emergency_restore` |
| **Thermal manager** | `start`, `stop`, `running`, `add_zone`, `monitor`, `events`, `orchestrator`, `watchdog` |
| **Keşif** | `discover`, `enumerate_fans`, `thermal_zones` |
| **Crash safety** | `install_crash_handlers`, `restore_all_fans_signal_safe` |
| **JSON** | `snapshot_to_json` |

---

## 🔧 1) Config Macro'ları

Header'ı include etmeden **önce** tanımla:

```cpp
#define KNST_SENS_DEFAULT_INTERVAL_MS    1000    // örnekleme aralığı (ms)
#define KNST_SENS_DEFAULT_HISTORY_DEPTH  300     // geçmiş derinliği (örnek)
#define KNST_SENS_DEFAULT_MIN_SAFE_PWM   40      // min güvenli PWM (0-255)
#define KNST_SENS_ABSOLUTE_MIN_PWM       20      // mutlak taban (0-255)
#define KNST_SENS_RESTORE_PWM            128     // restore sırasında min hedef
```

**`KNST_SENS_ABSOLUTE_MIN_PWM = 20`** — `min_safe_pwm` bundan aşağı inemez. `open()` daha küçük bir değer verilirse reddeder.

**`KNST_SENS_RESTORE_PWM = 128`** — `restore_auto()` çağrıldığında fanın çıkacağı **minimum** hız. Yani restore sırasında fan %50'nin altına inmez.

---

## 📦 2) Temel Tipler

### `sensor_stats`

```cpp
struct sensor_stats {
    double min = 0.0;
    double max = 0.0;
    double avg = 0.0;
    double last = 0.0;
    uint32_t count = 0;
};
```

### `alarm`

```cpp
struct alarm {
    knst_c16string chip;
    knst_c16string name;
    double value = 0.0;
    double limit = 0.0;
};
```

### `thermal_event`

```cpp
struct thermal_event {
    enum class kind : uint8_t {
        Warn, Critical, Shutdown, SensorLost, FanStall
    } type = kind::Warn;

    knst_c16string chip;
    knst_c16string name;
    double value = 0.0;
    double threshold = 0.0;
    uint64_t timestamp_ms = 0;
};
```

| Kind | Ne zaman |
|---|---|
| **Warn** | Sıcaklık `warn_c` eşiğini geçti |
| **Critical** | Sıcaklık `critical_c` eşiğini geçti |
| **Shutdown** | Sıcaklık `shutdown_c` eşiğini geçti |
| **SensorLost** | Zone sensörü 3 tick boyunca okunamadı |
| **FanStall** | Fan `kStallMinPwm`'in üstünde PWM'e rağmen 3 tick boyunca RPM=0 |

### `component`

```cpp
enum class component : uint8_t {
    CPU, GPU, NVMe, Chipset, Motherboard, Battery, PSU, Network, Unknown
};
```

### `classify_chip(chip_name)` — Chip adı → component

```cpp
knst_sens::classify_chip(u"coretemp");   // → CPU
knst_sens::classify_chip(u"amdgpu");     // → GPU
knst_sens::classify_chip(u"nvme0");      // → NVMe
knst_sens::classify_chip(u"nct6798");    // → Motherboard
knst_sens::classify_chip(u"pch_cannonlake"); // → Chipset
knst_sens::classify_chip(u"iwlwifi_1");  // → Network
```

Tanınmayan chip → `Unknown`.

### `sanitize_min_pwm(v)`

```cpp
knst_sens::sanitize_min_pwm(10);    // → 20 (ABSOLUTE_MIN_PWM)
knst_sens::sanitize_min_pwm(300);   // → 255
knst_sens::sanitize_min_pwm(50);    // → 50
```

Kullanıcıdan gelen `min_safe_pwm`'i güvenli aralığa çeker.

### `now_ms()` / `steady_ms()`

```cpp
uint64_t wall  = knst_sens::now_ms();      // epoch ms (event timestamp)
uint64_t steady = knst_sens::steady_ms();  // monotonic +1 (0 sentinel değil)
```

`steady_ms()` **monotonik** ve **asla 0 dönmez** — bu yüzden `age_ms() == 0` "hiç örneklenmemiş" anlamına gelir, "şimdi örneklendi" değil.

---

## 🔍 3) `sensor_filter` — Outlier Rejection

```cpp
struct sensor_filter::config {
    // Fiziksel limitler (aralık dışı → direkt reject)
    double temp_min  = -40.0,  temp_max  = 150.0;
    double fan_min   = 0.0,    fan_max   = 20000.0;
    double volt_min  = -30.0,  volt_max  = 30.0;
    double curr_min  = -100.0, curr_max  = 100.0;
    double power_min = 0.0,    power_max = 2000.0;

    // Median window
    uint32_t window  = 5;

    // Deviation kontrolü
    double   deviation_ratio = 0.5;      // median'ın %50'si
    uint32_t min_for_median  = 3;        // ilk 3 okuma kabul
    uint32_t max_consecutive_rejects = 3; // 3 reject sonrası gerçek değişim say
};
```

**Nasıl çalışır?**

1. **Fiziksel aralık kontrolü**: Değer `[min, max]` dışındaysa direkt reject
2. **NaN/Inf kontrolü**: `isfinite` değilse reject
3. **Median deviation**: Son `window` (5) okumanın median'ı ile karşılaştır
   - `deviation > max(2, |median| × ratio)` → reject
   - Ama `max_consecutive_rejects` (3) kez üst üste reddedilirse **gerçek değişim** kabul edilir, geçmiş sıfırlanır
4. **Kabul edilirse** → pencereye ekle (FIFO, `window` boyutunda)

> 💡 **`max_consecutive_rejects` neden gerekli?** Bir sensör gerçekten büyük bir sıçrama yaparsa (örn. termal throttling → sıcaklık 30°C birden 85°C), filter sonsuza kadar reddedebilir. Bu ayar, 3 ardışık reddi "gerçek değişim" olarak kabul eder ve pencereyi yeniden başlatır.

### Kullanım

```cpp
knst_sens::sensor_filter filter;
for (auto& s : sensors) {
    if (filter.accept(s)) {
        // geçerli okuma, kullan
    }
}
```

`async_sensor_monitor` bu filter'ı **otomatik** uygular (`enable_filter = true`).

---

## 📊 4) `async_sensor_monitor` — Thread-Safe Örnekleme

```cpp
struct async_sensor_monitor::config {
    uint32_t interval_ms   = KNST_SENS_DEFAULT_INTERVAL_MS;   // 1000
    uint32_t history_depth = KNST_SENS_DEFAULT_HISTORY_DEPTH; // 300
    bool enable_filter     = true;
};
```

### Temel kullanım

```cpp
knst_sens::async_sensor_monitor mon;
knst_sens::async_sensor_monitor::config cfg;
cfg.interval_ms = 500;      // 500ms'de bir örnekle
cfg.history_depth = 600;    // 5 dakikalık geçmiş

mon.set_config(cfg);
mon.start();

// ... çalış ...

mon.stop();
```

### `sample_once()` — Tek seferlik

```cpp
if (!mon.sample_once()) {
    std::cerr << "Sensör yok\n";
}
```

Thread başlatmadan tek seferlik örnekleme. `start()` öncesi **zorunlu** (monitor'ın ilk veriyi toplaması için).

### `snapshot()` — Son örneklemenin tam listesi

```cpp
auto list = mon.snapshot();
for (auto& s : list) {
    std::cout << s.chip << "/" << s.name << " = " << s.value << s.unit << "\n";
}
```

### `stats(chip, name)` — İstatistik

```cpp
auto st = mon.stats(u"coretemp", u"Package id 0");
if (st) {
    std::cout << "Min: " << st.value.min << "\n";
    std::cout << "Max: " << st.value.max << "\n";
    std::cout << "Ort: " << st.value.avg << "\n";
    std::cout << "Son: " << st.value.last << "\n";
    std::cout << "Adet: " << st.value.count << "\n";
}
```

### `history(chip, name)` — Sıralı geçmiş

```cpp
auto hist = mon.history(u"coretemp", u"Package id 0");
// hist[0] = en eski, hist[last] = en yeni
```

`history_depth` kadar örnek. En yeni son.

### `find(type, needle)` — İlk eşleşen

```cpp
auto s = mon.find(knst_sensor_type::Temperature, u"Package");
if (s) std::cout << s.value.chip << "/" << s.value.name << "\n";

// needle boş verirsen herhangi bir Temperature döner
auto any = mon.find(knst_sensor_type::Temperature);
```

### `hottest()` — En sıcak sensör

```cpp
auto s = mon.hottest();
if (s) {
    std::cout << "En sıcak: " << s.value.chip << "/" << s.value.name
              << " = " << s.value.value << "°C\n";
}
```

Sadece `Temperature` tipinde, fiziksel aralıkta (`-40..150`) olan sensörler.

### `over_limit(type, limit)` — Limit aşanlar

```cpp
auto alarms = mon.over_limit(knst_sensor_type::Temperature, 85.0);
for (auto& a : alarms) {
    std::cout << "⚠ " << a.chip << "/" << a.name
              << " = " << a.value << " > " << a.limit << "\n";
}
```

### `component_temp(component)` — Component bazlı

```cpp
auto cpu = mon.component_temp(knst_sens::component::CPU);
if (cpu) std::cout << "CPU: " << cpu.value << "°C\n";

auto gpu = mon.component_temp(knst_sens::component::GPU);
if (gpu) std::cout << "GPU: " << gpu.value << "°C\n";
```

`classify_chip` ile chip'i CPU/GPU/NVMe/... olarak sınıflandırır, o kategoriden **en sıcak** olanı döner.

### `age_ms()` — Son örneklemeden bu yana

```cpp
uint64_t age = mon.age_ms();
if (age > 5000) {
    std::cerr << "Sensör verisi 5 saniyeden eski!\n";
}
```

`UINT64_MAX` = hiç örneklenmemiş. `steady_ms()` monotonik olduğu için mantıklı.

### `reset()`

```cpp
mon.reset();   // istatistikleri ve geçmişi sıfırlar
```

---

## 🌀 5) `fan_control` — PWM Kontrolü (Linux)

### Açma

```cpp
auto f = knst_sens::fan_control::open(
    u"nct6798",                              // chip adı
    1,                                       // fan index (1..16)
    KNST_SENS_DEFAULT_MIN_SAFE_PWM           // min_safe_pwm (default 40)
);
if (!f) {
    // NotFound → chip yok
    // InvalidArgument → index 0/17, min_safe_pwm < 20, chip adı geçersiz
}
```

**Doğrulama:**
- `index` 1..16 arası olmalı
- `min_safe_pwm` `[ABSOLUTE_MIN_PWM=20, 255]` arası olmalı
- `chip` `is_safe_token(_, 32)` — path traversal (`../`) engellenir

**Chip bulma:**
- `/sys/class/hwmon/hwmonN/name` = chip adı ile eşleşen dizini bulur
- `/sys/class/hwmon/hwmonN/pwm{index}` varsa başarılı

### PWM okuma/yazma

```cpp
// Oku
auto pwm = f.value.get_pwm();     // 0..255
auto rpm = f.value.get_rpm();     // 0..20000
auto en  = f.value.get_pwm_enable();   // 0..5 (mode)

// SAFE yaz — min_safe_pwm altı REDDEDİLİR
f.value.set_pwm(120);             // OK (40'tan büyük)
f.value.set_pwm(0);               // ❌ InvalidArgument
f.value.set_pwm(30);              // ❌ InvalidArgument (min=40)
f.value.set_pwm(300);             // ❌ InvalidArgument (>255)

// Yüzde
f.value.set_percent(50);          // → PWM 128 (50 × 255 / 100)
f.value.set_percent(0);           // ❌ InvalidArgument (min=40 → pwm=0 < 40)

// Manuel mod
f.value.set_manual(true);         // pwm_enable = 1 (manual)
f.value.set_manual(false);        // pwm_enable = orijinal mod (veya 2)
```

### ⚠️ `set_pwm_unsafe()` — DEPRECATED

```cpp
[[deprecated]]
f.value.set_pwm_unsafe(0);    // ❌ Derler ama warning verir
                              // ⚠️ Fanı DURDURABİLİR!
```

**Tek bypass.** Sadece "fanı gerçekten durdurmam gerekiyor" dersen kullan. Normalde **kullanma**.

### `restore_auto()` — Güvenli restore

```cpp
auto r = f.value.restore_auto();
if (!r) {
    std::cerr << "Restore başarısız — manuel kontrol et!\n";
}
```

**Ne yapar?**
1. **Manuel moda al** (`pwm_enable = 1`)
2. **PWM yaz**: `max(min_safe, KNST_SENS_RESTORE_PWM=128, current_pwm)`
   - Yani fanı **asla yavaşlatmaz**
   - Şu an 200 ise → 200 kalır
   - Şu an 50 ise → 128'e çıkar
3. **Orijinal moda geri al** (`orig_enable_`, 2..5 arası; yoksa 2)
4. **Doğrula** (bir kez retry, hâlâ yanlışsa `BadState`)

### `needs_restore()`

```cpp
if (f.value.needs_restore()) {
    // bu nesne fanı değiştirdi, restore etmedi
}
```

`dirty_` bayrağı. Destructor bunu kontrol eder.

### Move-only

```cpp
knst_sens::fan_control a = std::move(f.value);   // ✅ OK
knst_sens::fan_control b = f.value;              // ❌ Copy yok
```

Move edilen kaynak `base_[0] = '\0'`, `dirty_ = false`, `slot_ = -1`. Yani **kaynak asla restore etmez** — çifte restore olmaz.

### Windows'ta

```cpp
auto f = knst_sens::fan_control::open(u"nct6798", 1);
// f → NotSupported
```

Fan kontrolü **Windows'ta stub**. Tüm metotlar `NotSupported` döner.

---

## 📈 6) `fan_curve` — Sıcaklık → PWM Eğrisi

```cpp
struct curve_point {
    double   temp_c = 0.0;
    uint32_t pwm    = 0;      // 0..255
};

struct fan_curve {
    knst_vector<curve_point> points;
    double   hysteresis_c = 2.0;
    uint32_t ramp_step    = 8;
    uint32_t ramp_up_step = 255;   // 255 = anında
};
```

### Hazır curve'ler

```cpp
auto silent      = knst_sens::fan_curve::silent();
auto balanced    = knst_sens::fan_curve::balanced();
auto performance = knst_sens::fan_curve::performance();
```

| Curve | 40°C | 50°C | 65°C | 80°C | 95°C |
|---|---|---|---|---|---|
| **silent** | 50 | 80 | 130 | 200 | 255 |
| **balanced** | 60 | 100 | 160 | 220 | 255 |
| **performance** | — | 150 | 200 | 240 | 255 |

`silent` → `hysteresis_c=3, ramp_step=5` (yumuşak)
`balanced` → `hysteresis_c=2, ramp_step=10`
`performance` → `hysteresis_c=1.5, ramp_step=20` (agresif)

### Kendi curve'ün

```cpp
knst_sens::fan_curve custom;
custom.points = {
    {40.0, 80},
    {55.0, 130},
    {70.0, 200},
    {85.0, 255}
};
custom.hysteresis_c = 2.5;
custom.ramp_step    = 10;    // iniş: max 10/tick
custom.ramp_up_step = 40;    // çıkış: max 40/tick
```

`points` **sıralı olmalı** veya `normalize()` çağrılmalı:

```cpp
custom.normalize();   // temp_c'ye göre sırala
```

### `pwm_for(temp_c)` — Interpolasyon

```cpp
uint32_t target = custom.pwm_for(62.0);
```

- Sıcaklık ilk noktadan küçükse → ilk noktanın PWM'i
- Son noktadan büyükse → son noktanın PWM'i
- Aradaysa → **lineer interpolasyon** (tam sayı yuvarlama)
- **Boş curve** → **255** (fail-safe)
- **NaN sıcaklık** → **255** (fail-safe)

### Hysteresis — Sadece **soğumayı** geciktirir

> ⚠️ **ÖNEMLİ:** Rising sıcaklık her zaman **anında** takip edilir. Hysteresis sadece **düşerken** devreye girer.

```cpp
// eff_temp = 65°C (önceki)
// yeni temp = 63°C
// hysteresis = 3°C
// → eff_temp 65 kalır, PWM düşmez (soğumayı bekliyor)

// yeni temp = 61°C (65 - 3 - 1 = 61 < hyst)
// → eff_temp 61'e iner, PWM düşer
```

### Ramp limiting

```cpp
struct curve_state {
    double eff_temp = 0.0;
    bool   valid    = false;
};
```

`curve_next_pwm(curve, state, temp, current_pwm, min_safe)`:

1. NaN temp → 255
2. Hysteresis uygula → `eff_temp`
3. `pwm_for(eff_temp)` → target
4. **`min_safe` altına clamp** et
5. **Rate limiting**:
   - `target > current`: `ramp_up_step` uygulanır (0 = limitsiz)
   - `target < current`: `ramp_step` uygulanır (0 = limitsiz)
6. Tekrar `min_safe..255` clamp

**Ramp neden önemli?** Ani PWM değişimi fan motoruna zarar verebilir, gürültü yapabilir. `ramp_step = 10` ise bir tick'te en fazla 10 PWM değişir → 25 tick'te 0'dan 255'e yumuşak geçiş.

> 💡 **`ramp_up_step = 255`** (varsayılan) → **çıkış anında**. Bu güvenli: sıcaklık artarken gecikme olmaz.
> `ramp_step = 8` (varsayılan) → **iniş yavaş**. Bu da güvenli: fan hemen durmaz.

> 💡 **Eski kod:** "sıcaklık az değiştiyse atla" mantığı vardı → fan yarıda takılı kalabiliyordu. Yeni kod her tick'te ramp'lemeye devam eder.

---

## 🎮 7) `fan_curve_controller` — Curve + Fan

```cpp
knst_sens::fan_control fan = /* ... */;
knst_sens::fan_curve curve = knst_sens::fan_curve::balanced();

knst_sens::fan_curve_controller ctrl(std::move(fan), curve);

// her tick'te
auto r = ctrl.tick(65.0);   // 65°C → uygun PWM
```

### Başlangıç davranışı

```cpp
// İlk tick'te mevcut PWM'i okur
auto p = ctrl.fan().get_pwm();
// current_pwm_ = p (min_safe..255 aralığındaysa)
```

Bu önemli: eğer fan zaten 150'de çalışıyorsa, ilk tick'te **aniden 40'a düşmez**. Mevcut PWM'den ramplemeye başlar.

### Kurallar

- `min_safe_` = `max(sanitize_min_pwm(user), fan.min_safe_pwm())`
- `tick(temp)` → `curve_next_pwm` → `fan.set_pwm(target)` (değiştiyse)
- `set_curve(yeni)` → state sıfırlanır
- `restore_auto()` → `fan.restore_auto()`

### Yıkıcı (Destructor)

`fan_control` nesnesini içinde tutar. Yıkıcı çalıştığında `fan_control`'ün yıkıcısı da çalışır → **dirty ise restore eder**. Yani `fan_curve_controller` kapsamdan çıkarsa fan güvenli.

---

## 📢 8) `event_bus`

```cpp
knst_sens::event_bus bus;

auto id = bus.subscribe([](const knst_sens::thermal_event& e) {
    if (e.type == knst_sens::thermal_event::kind::Critical) {
        std::cerr << "KRİTİK! " << e.chip << " = " << e.value << "°C\n";
    }
});

// ... çalış ...

bus.unsubscribe(id);
```

### `subscribe` / `unsubscribe`

- Thread-safe (mutex)
- `handler` = `knst_function_shared<const thermal_event&>`
- `subscribe` → `uint32_t id` döner
- `unsubscribe(id)` → o handler'ı çıkarır

### `emit`

```cpp
knst_sens::thermal_event e;
e.type = knst_sens::thermal_event::kind::Warn;
e.chip = u"coretemp";
e.name = u"Package id 0";
e.value = 88.0;
e.threshold = 85.0;
e.timestamp_ms = knst_sens::now_ms();

bus.emit(e);
```

**Mutex dışı dispatch:** Handler listesi kopyalanır, sonra mutex dışında çağrılır. Yani:
- Handler uzun çalışırsa yeni `subscribe`'lar bloklanmaz
- Handler içinde `subscribe`/`unsubscribe` çağırmak güvenli

**Exception handling:** `try { h(e); } catch (...) {}` — bir handler throw ederse diğerleri etkilenmez. **Ama exception bilgisi kaybolur** — loglamak istiyorsan handler içinde yakala.

> ⚠️ **Thread notu:** Handler'lar **manager thread**'inde çalışır. İçinde `stop()` çağırma (deadlock olur).

---

## 🚨 9) `emergency_responder`

```cpp
struct emergency_responder::config {
    double warn_c      = 85.0;
    double critical_c  = 95.0;
    double shutdown_c  = 105.0;
    bool   full_blast_on_critical = true;
    double hysteresis_c = 5.0;
    uint32_t repeat_ms  = 10000;

    bool valid() const noexcept;   // warn < critical < shutdown, hepsi finite
};
```

### Seviyeler

| Level | Eşik |
|---|---|
| **Normal** | `temp < warn_c` |
| **Warn** | `warn_c <= temp < critical_c` |
| **Critical** | `critical_c <= temp < shutdown_c` |
| **Shutdown** | `temp >= shutdown_c` |

### Kullanım

```cpp
knst_sens::event_bus bus;
knst_sens::emergency_responder emergency(bus);

emergency.set_blast_callback([] {
    std::cerr << "ACİL! Fanlar son hıza alınıyor\n";
});

emergency.set_release_callback([] {
    std::cerr << "Sıcaklık normale döndü\n";
});

// her tick'te
auto hottest = mon.hottest();
if (hottest) {
    emergency.check(hottest.value);
}
```

### Davranış

**Yükselirken** (`raw >= level_`):
- Anında yeni seviyeye geçer

**Düşerken** (`raw < level_`):
- `temp < threshold(nl) - hysteresis_c` olana kadar düşürmez
- Yani sıcaklık **5°C altına** düşmeden Warn'dan Normal'e geçmez

**Event emit:**
- Seviye değiştiyse → emit
- Aynı seviyede `repeat_ms` (10 sn) geçtiyse → emit (yeniden hatırlat)

**Blast/release callback:**
- `>= Critical` olunca → `blast_()` (sadece `full_blast_on_critical` açıksa)
- Critical'dan çıkınca → `release_()`

### `current_level()`

```cpp
auto lvl = emergency.current_level();
// Normal / Warn / Critical / Shutdown
```

### Thread-safety

**Yok.** `check()` tek thread'den çağrılmalı (manager thread).

---

## 🎯 10) `fan_zone` ve `thermal_orchestrator`

### `fan_zone`

```cpp
struct fan_zone {
    knst_c16string name;                          // "cpu", "gpu"
    knst_c16string temp_chip;                     // "coretemp"
    knst_c16string temp_name;                     // boş = chip'in en sıcağı
    fan_curve curve;
    knst_vector<knst_c16string> fan_chips;        // {"nct6798", "nct6798"}
    knst_vector<uint32_t> fan_indices;            // {1, 2}
    uint32_t min_safe_pwm = KNST_SENS_DEFAULT_MIN_SAFE_PWM;
    uint32_t failsafe_pwm = 200;

    // runtime state (orchestrator yönetir)
    knst_vector<fan_control> fans;
    knst_vector<fan_state>   fan_states;
    curve_state cstate;
    uint32_t current_pwm;
    uint32_t lost_ticks;
    uint32_t retry_cooldown;
    bool     initialized;
    bool     lost_reported;
};
```

**`temp_name` boşsa** → `temp_chip`'in **en sıcak** sensörü kullanılır.
**`temp_name` doluysa** → sadece o isim eşleşir.

### `thermal_orchestrator`

```cpp
knst_sens::thermal_orchestrator orch;

knst_sens::fan_zone zone;
zone.name      = u"cpu";
zone.temp_chip = u"coretemp";
zone.curve     = knst_sens::fan_curve::balanced();
zone.fan_chips = {u"nct6798"};
zone.fan_indices = {1};
orch.add_zone(std::move(zone));

// Her tick'te
orch.tick(monitor, &bus);   // mon = async_sensor_monitor, bus opsiyonel
```

### `tick()` — Ne yapar?

Her zone için:

1. **`ensure_fans(z)`** — eksik fanları aç (retry cooldown 5 tick)
2. **`init_current(z)`** — mevcut PWM'i oku
3. **Emergency modundaysa**: `apply(z, 255)` → hepsi son hız
4. **Sıcaklık oku**:
   - `stale` (> `stale_ms`) → kayıp sayılır
   - Sensör yoksa → `lost_ticks++`
   - `lost_ticks >= kLostTicks(3)` → `failsafe_pwm` + `SensorLost` event (bir kez)
   - Sensör geldi → `lost_ticks=0`, `lost_reported=false`
5. **`curve_next_pwm`** → hedef PWM
6. **Değiştiyse `apply(z, target)`**
7. **`check_stall(z, pending)`** — fan stall kontrolü

**Event'ler mutex dışında** emit edilir → handler orchestrator'a geri çağırabilir.

### `emergency_full_blast()` / `clear_emergency()`

```cpp
orch.emergency_full_blast();   // tüm zone'lar 255
orch.clear_emergency();         // normal curve'e dön
orch.in_emergency();            // true/false
```

### `restore_all_auto()`

```cpp
orch.restore_all_auto();   // tüm fanları auto'ya döndür
```

Yıkıcı otomatik çağırır.

### Stall tespiti

```cpp
static constexpr uint32_t kLostTicks   = 3;
static constexpr uint32_t kStallTicks  = 3;
static constexpr uint32_t kStallMinPwm = 80;
```

**Stall kontrolü** sadece `current_pwm >= 80` iken anlamlı. `rpm=0` ve 3 tick boyunca sürerse → `FanStall` event.

> ⚠️ **Fan stall önlem değil, bilgi.** Event emit eder, **fanı durdurmaz veya tekrar çalıştırmaz**. Bunu handler'da yapmalısın.

---

## 🐕 11) `fan_watchdog`

```cpp
knst_sens::fan_watchdog wd(3000);   // 3 saniye timeout

// Fanları ekle
wd.attach(std::move(fan1));
wd.attach(std::move(fan2));

wd.start();

// Manager thread her tick'te:
wd.heartbeat();

// ... çalış ...

wd.stop();
```

### Ne yapar?

1. **`start()`** → heartbeat'i başlatır, thread açar
2. **`heartbeat()`** → `last_hb_ns_` günceller
3. **Watchdog thread** her 50ms'de bir kontrol:
   - `now - last_hb > timeout_ms` → **`emergency_restore()`** (tüm fanları restore) + `tripped_=true`
   - Heartbeat geri gelirse → `tripped_=false` (yeniden kurulur)

### Timeout sınırı

Minimum **500ms**. Daha küçük verirsen clamp edilir.

### `thermal_manager` içinde

```cpp
watchdog_.set_timeout_ms(std::max(
    cfg_.watchdog_timeout_ms,
    cfg_.tick_interval_ms * 3u + 1000u
));
```

Yani `tick_interval = 1000` ise timeout **en az 4000ms**. Tick kaçırırsa false-positive olmasın.

### Crash safety

`restore_all_fans_signal_safe()` (aşağıda) ile birlikte:
- **Normal çalışma**: watchdog heartbeat kontrolü
- **Crash**: signal handler

İkisi birbirini tamamlar.

---

## 🖥️ 12) `thermal_manager` — All-in-One Facade (Linux)

En yüksek seviye. İçeride şunları barındırır:
- `async_sensor_monitor` (sensör örnekleme)
- `fan_watchdog` (heartbeat)
- `thermal_orchestrator` (zone kontrolü)
- `event_bus` (event'ler)
- `emergency_responder` (kritik sıcaklık)

### Config

```cpp
struct thermal_manager::config {
    uint32_t tick_interval_ms    = 1000;   // kontrol tick'i
    uint32_t sample_interval_ms  = 1000;   // sensör örnekleme
    uint32_t history_depth       = 300;
    uint32_t watchdog_timeout_ms = 3000;
    emergency_responder::config emergency;
    knst_vector<knst_c16string> emergency_ignore_chips;
};
```

**`emergency_ignore_chips`** — Örneğin "nct6798" chip'i her zaman 200°C gösteriyorsa (bozuk sensör), onu emergency'den muaf tut.

### Tam kullanım

```cpp
knst_sens::thermal_manager mgr;

// 1) Zone ekle (fan'lar zorunlu açılabilir olmalı)
knst_sens::fan_zone cpu_zone;
cpu_zone.name        = u"cpu";
cpu_zone.temp_chip   = u"coretemp";
cpu_zone.temp_name   = u"Package id 0";   // opsiyonel
cpu_zone.curve       = knst_sens::fan_curve::balanced();
cpu_zone.fan_chips   = {u"nct6798"};
cpu_zone.fan_indices = {1};
cpu_zone.min_safe_pwm = 40;
cpu_zone.failsafe_pwm = 220;

auto r = mgr.add_zone(std::move(cpu_zone));
if (!r) {
    std::cerr << "Zone eklenemedi\n";
    return 1;
}

// 2) Event dinle
auto sub = mgr.events().subscribe([](const knst_sens::thermal_event& e) {
    switch (e.type) {
        case knst_sens::thermal_event::kind::Warn:
            std::cout << "Uyarı: " << e.value << "°C\n"; break;
        case knst_sens::thermal_event::kind::Critical:
            std::cerr << "KRİTİK: " << e.value << "°C\n"; break;
        case knst_sens::thermal_event::kind::FanStall:
            std::cerr << "Fan durdu: " << e.name << "\n"; break;
        case knst_sens::thermal_event::kind::SensorLost:
            std::cerr << "Sensör kayıp: " << e.chip << "\n"; break;
        default: break;
    }
});

// 3) Başlat
r = mgr.start();
if (!r) {
    std::cerr << "Manager başlatılamadı\n";
    return 1;
}

// 4) Çalış...
std::this_thread::sleep_for(std::chrono::seconds(60));

// 5) Durdur (tüm fanlar restore_auto)
mgr.stop();
```

### `start()` — Ne yapar?

1. **`cfg_.emergency.valid()`** kontrolü (warn < critical < shutdown)
2. `sample_interval >= 50`, `tick_interval ∈ [100, 60000]` kontrolü
3. `stale_ms_ = max(5000, max(sample, tick) × 5)`
4. `monitor_.set_config()` + `sample_once()` (ilk veri **zorunlu**)
5. `monitor_.start()`
6. `watchdog_.set_timeout_ms(max(given, tick×3 + 1000))` + `start()`
7. `emergency_.set_blast_callback()` → `orchestrator_.emergency_full_blast()`
8. `emergency_.set_release_callback()` → `orchestrator_.clear_emergency()`
9. Manager thread açar → her tick'te `heartbeat` + `tick` + `check_emergency`

### `stop()` — Ne yapar?

1. Thread durdur
2. `monitor_.stop()`
3. `watchdog_.stop()`
4. `orchestrator_.clear_emergency()`
5. `orchestrator_.restore_all_auto()` (hiç başlamamış olsa bile)

### `add_zone(z)` — Fan'ları önce açar

```cpp
// Fan'ları aç
knst_vector<fan_control> probes;
for (chip, index) {
    auto f = fan_control::open(chip, index, min_pwm);
    if (!f) return f;   // hepsi açılamazsa zone reddedilir
    probes.push_back(std::move(f));
}

// Zone ekle
auto r = orchestrator_.add_zone(std::move(z));

// Fan'ları watchdog'a bağla
for (auto& p : probes) watchdog_.attach(std::move(p));
```

Yani **tüm fan'lar açılabilmeli** — biri açılamazsa `add_zone` başarısız olur (kısmi zone yok).

### Accessor'lar

```cpp
mgr.monitor();       // async_sensor_monitor&
mgr.events();        // event_bus&
mgr.orchestrator();  // thermal_orchestrator&
mgr.watchdog();      // fan_watchdog&
```

İstersen içlerine direkt erişebilirsin.

---

## 🛡️ 13) Crash Safety (Linux)

### `install_crash_handlers()`

```cpp
int main() {
    knst_sens::install_crash_handlers();   // BİR KEZ, startup'ta
    
    // ... kütüphaneyi kullan ...
}
```

**Sinyaller:** `SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT`

**Davranış:**
1. Tüm `restore_slot`'ları async-signal-safe şekilde yazar (`open`+`write`+`close`)
2. Handler'ı `SIG_DFL`'e alır (`SA_RESETHAND`)
3. Sinyali yeniden fırlatır (proses yine ölür, ama fanlar zaten restore edildi)

**Yakalanamayan:**
- **`SIGKILL`** — hiçbir kütüphane yakalayamaz
- **Güç kesintisi** — OS zaten kapanmıyor

Bu durumlar için systemd kullan:

```ini
[Service]
ExecStart=/usr/bin/my_app
ExecStopPost=/usr/bin/knst-fan-restore.sh
```

`ExecStopPost` proses öldükten sonra çalışır → `restore_all_fans_signal_safe()` script'ten çağırılabilir.

### `restore_all_fans_signal_safe()`

```cpp
void my_signal_handler(int sig) {
    knst_sens::restore_all_fans_signal_safe();
    // ... diğer işler ...
}
```

Kendi handler'ından çağırabilirsin. Async-signal-safe (`open`+`write`+`close` kullanır).

### Restore Slot Mekanizması

```cpp
struct restore_slot {
    std::atomic<int> state;    // 0 free, 1 filling, 2 armed
    char enable_path[260];
    char pwm_path[260];
    char pwm_val[8];
    char enable_val[4];
};
inline restore_slot g_restore_slots[64];
```

- **Her `set_pwm` çağrısında** (ilk dirty olduğunda) bir slot talep edilir
- Slot, hangi path'e ne yazılacağını **önceden** depolar (snprintf normal context'te)
- Signal handler sadece `open`+`write`+`close` yapar → async-signal-safe
- `restore_auto()` başarılı olunca slot serbest bırakılır (`state = 0`)

**Maksimum 64 slot** — 64'ten fazla fan kontrol ediyorsan bazıları crash restore kapsamı dışında kalır.

---

## 🔎 14) Keşif — `discover()`

```cpp
auto r = knst_sens::discover();

// Bulunan chip'ler
for (auto& c : r.sensor_chips) {
    std::cout << "Chip: " << c << "\n";
}

// Bulunan fan'lar
for (auto& f : r.fans) {
    std::cout << f.chip << " pwm" << f.index
              << " = " << f.current_pwm
              << "  rpm = " << f.current_rpm
              << "  enable = " << f.current_enable << "\n";
}

// Öneri metinleri
for (auto& s : r.suggestions) {
    std::cout << s << "\n";
}
```

**Ne yapar?**
1. `knst_devices::sensors()` üzerinden tüm chip adlarını toplar
2. `/sys/class/hwmon/*/pwmN` dosyalarını tarar
3. Her pwm için `fanN_input` ve `pwmN_enable` varsa onları da okur
4. Fan yoksa **öneri metni** ekler (IT8665E için `it87` fork vs.)

### `enumerate_fans()` — Linux only

```cpp
auto fans = knst_sens::enumerate_fans();
for (auto& f : fans) {
    std::cout << f.chip << " pwm" << f.index << "\n";
}
```

### `thermal_zones()` — Linux only

```cpp
auto zones = knst_sens::thermal_zones();
for (auto& z : zones) {
    std::cout << z.type << " = " << z.temp_c << "°C\n";
}
```

**`/sys/class/thermal/thermal_zone*`** okur. **Sentinel değerleri filtreler:**
- Qualcomm HAL bazen `867000` (= `INT_MAX/1000`) döner → `-40..150` filtresi bunları atar.

---

## 📄 15) JSON Export (Koşullu)

`KNST_JSON_HPP` include edildiyse:

```cpp
auto snapshot = mon.snapshot();
knst_json j = knst_sens::snapshot_to_json(snapshot);
// j → [{ chip, name, type, value, unit }, ...]
```

---

## ⚠️ 16) Sık Tuzaklar

### 1) `set_pwm(0)` ÇAĞIRILAMAZ

```cpp
f.value.set_pwm(0);   // ❌ InvalidArgument (min_safe=40)
f.value.set_percent(0); // ❌ InvalidArgument (pwm=0 < 40)
```

**Bilinçli tasarım.** Fan stop ettirilemez. `set_pwm_unsafe` deprecated.

### 2) `min_safe_pwm` 20 altına inemez

```cpp
auto f = fan_control::open(u"nct6798", 1, 10);   // ❌ InvalidArgument
auto f = fan_control::open(u"nct6798", 1, 20);   // ✅ OK
auto f = fan_control::open(u"nct6798", 1, 15);   // ❌ InvalidArgument
```

### 3) Move edilen fan restore etmez

```cpp
knst_sens::fan_control a = knst_sens::fan_control::open(...).value;
knst_sens::fan_control b = std::move(a);
// a → base_[0]='\0', dirty_=false, slot_=-1
// a'nın destructor'ı hiçbir şey yapmaz
```

**İstenen davranış** — çifte restore yok.

### 4) `restore_auto()` fanı asla yavaşlatmaz

```cpp
// Fan şu an 200'de
f.value.restore_auto();
// → PWM = max(40, 128, 200) = 200
// Fan hızlanmaz, sadece mod auto'ya döner
```

### 5) Windows'ta `fan_control::open` her zaman `NotSupported`

Fan kontrolü için **Linux** gerekli.

### 6) `thermal_manager::add_zone` fan'ları önce açar

```cpp
// Fan açılamazsa → zone reddedilir
auto r = mgr.add_zone(zone);
if (!r) {
    // fan açılamadı
}
```

Kısmi zone **yok** — hepsi veya hiçbiri.

### 7) `emergency_responder` thread-safe değil

`check()` tek thread'den çağrılmalı. Manager zaten bunu yapıyor.

### 8) `install_crash_handlers()` **bir kez** çağır

Çağırdıktan sonra `SIGINT` handler'ın **senin handler'ını ezer**. Kendi handler'ların varsa **önce** onları kur, sonra `install_crash_handlers()`.

### 9) `SA_RESETHAND` — bir kere çalışır

Signal handler bir sinyal aldıktan sonra default'a döner. Yani aynı sinyal **ikinci** geldiğinde default davranış (crash) çalışır. Bu **bilinçli** — handler'da sonsuz döngü olmasın.

### 10) `kLostTicks = 3` — yavaş ama güvenli

Sensör 3 tick boyunca okunamazsa failsafe. `tick_interval = 1000` ise **3 saniye** sonra devreye girer. Daha hızlı istiyorsan `tick_interval`'ı düşür.

### 11) `kStallMinPwm = 80` altında stall kontrolü **yok**

```cpp
if (z.current_pwm < kStallMinPwm) return;
```

PWM düşükse fan RPM=0 normal olabilir (düşük hızda dönmüyor). Yanlış alarm olmasın.

### 12) `failsafe_pwm` yerine `min_safe_pwm` dikkat

`failsafe_pwm` sensör kayıp olduğunda kullanılır → **yüksek** olmalı (200 önerilen). `min_safe_pwm` normal çalışma alt sınırı (40).

### 13) `fan_curve::pwm_for` boş curve → 255

```cpp
knst_sens::fan_curve empty;
empty.pwm_for(50.0);   // → 255
```

**Fail-safe.** Boş curve = tanımsız = tam hız.

### 14) `curve_next_pwm` NaN → 255

```cpp
knst_sens::curve_state st;
knst_sens::curve_next_pwm(curve, st, NAN, 100, 40);   // → 255
```

**Fail-safe.** NaN sıcaklık = sensör bozuk = tam hız.

### 15) `ramp_up_step = 255` (varsayılan) → çıkış anında

```cpp
// Varsayılan: sıcaklık 60'tan 90'a çıktıysa → hedef PWM anında
// ramp_step (iniş) = 8 → hedef 255'ten 40'a inerse 27 tick sürer
```

Güvenli tasarım: **çıkış hızlı, iniş yavaş**.

### 16) `thermal_manager::start` ilk sample **zorunlu**

```cpp
auto r = mgr.start();
if (!r) {
    // monitor_.sample_once() başarısız → sensör yok
}
```

Manager başlamadan **mutlaka** sensör verisi lazım. Sensör yoksa başlamaz.

### 17) `watchdog_timeout_ms` minimumu

```cpp
mgr.set_timeout_ms(100);   // → 500'e clamp edilir
```

### 18) `add_zone` boş `fan_chips` reddeder

```cpp
zone.fan_chips = {};   // ❌ InvalidArgument
```

### 19) `restore_auto()` başarısız olabilir

```cpp
auto r = f.value.restore_auto();
if (!r) {
    // ⚠️ Fan hâlâ manual modda olabilir!
    // Manuel olarak pwm_enable = 2 yap
}
```

Sysfs yazma başarısız olabilir. `result` kontrol et.

### 20) `discover()` container'da sınırlı

Docker/snap/flatpak'ta `/sys/class/hwmon` mount edilmemişse boş döner.

---

## 📊 17) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| `sample_once()` | O(sensör) | ~10-100 sysfs okuma |
| `snapshot()` | O(sensör) | Mutex + kopya |
| `stats()` / `history()` | O(sensör) | Lineer arama |
| `hottest()` | O(sensör) | Sıcaklık filtresi |
| `component_temp()` | O(sensör) | `classify_chip` + filtre |
| `fan_control::open` | O(hwmon) | Dizin tarama + dosya stat |
| `set_pwm` | O(1) | 1-2 sysfs yazma |
| `restore_auto` | O(1) + 50ms | Doğrulama bekleme |
| `curve_next_pwm` | O(nokta) | Curve boyutu kadar (genelde 4-5) |
| `tick` (zone) | O(fan + sensör) | Fan başına set_pwm |
| `emergency.check` | O(1) | Sadece sayı karşılaştırma |
| `watchdog` | O(1) / 50ms | Sabit aralık |

**Tipik gecikmeler:**
- `sample_once`: 10-50ms (sensör sayısına göre)
- `tick` (bir zone): 5-20ms (sysfs yazma + doğrulama)
- `restore_auto`: 50-100ms (doğrulama bekleme)

---

## 🌍 18) Platform Farkları

| Özellik | Linux | Windows | Android |
|---|---|---|---|
| **Sensör okuma** | sysfs | WMI | JNI |
| **Fan PWM** | sysfs | ❌ | ❌ |
| **Crash restore** | signal | ❌ | ❌ |
| **Watchdog** | ✅ | ⚠️ stub | ⚠️ stub |
| **Thermal manager** | ✅ | ❌ | ❌ |
| **install_crash_handlers** | ✅ | ❌ | ❌ |

**Windows'ta `fan_control` stub** — `open()` her zaman `NotSupported`. Diğer katmanlar (sensor_filter, monitor, event_bus, emergency) çalışır.

**Android'de** sensör okuma JNI üzerinden (`knst_devices`), ama fan kontrolü yok.

---

## ✅ 19) Yap / ❌ Yapma

### ✅ Yap

```cpp
// 1. min_safe_pwm'i saygıyla kullan
auto f = fan_control::open(u"nct6798", 1, 40);
f.value.set_pwm(120);   // güvenli

// 2. Scope'tan çıkmasına izin ver (otomatik restore)
{
    auto temp_fan = fan_control::open(u"nct6798", 2, 60);
    temp_fan.value.set_percent(80);
}   // destructor restore_auto çağırır

// 3. install_crash_handlers'ı main'de bir kez çağır
int main() {
    knst_sens::install_crash_handlers();
    // ...
}

// 4. thermal_manager kullan (en güvenli katman)
knst_sens::thermal_manager mgr;
mgr.add_zone(cpu_zone);
mgr.start();
// ...
mgr.stop();   // tüm restore otomatik

// 5. Sensor filter'ı açık bırak
knst_sens::async_sensor_monitor::config cfg;
cfg.enable_filter = true;   // varsayılan

// 6. emergency_ignore_chips ile bilinen bozuk sensörleri filtrele
cfg.emergency_ignore_chips = {u"nct6798"};
```

### ❌ Yapma

```cpp
// 1. set_pwm(0) çağırma
f.value.set_pwm(0);   // ❌ InvalidArgument

// 2. set_pwm_unsafe kullanma
f.value.set_pwm_unsafe(0);   // ❌ deprecated + fan durabilir

// 3. min_safe_pwm'i 20 altına çekme
auto f = fan_control::open(u"nct6798", 1, 10);   // ❌ InvalidArgument

// 4. Manuel modda unut
f.value.set_manual(true);
// ... başka bir şey ...
// f destructor restore_auto çağırır ✅
// ama restore_auto çağırmadıysan fan manual modda kalır (sonraki reboot'a kadar)

// 5. install_crash_handlers'ı birden fazla çağırma
install_crash_handlers();
install_crash_handlers();   // ⚠️ zararsız ama gereksiz

// 6. Handler'dan stop() çağırma
bus.subscribe([&](auto& e) {
    mgr.stop();   // ❌ deadlock (manager thread'de çağrılıyor)
});

// 7. Emergency'i iki thread'den çağırma
emergency.check(a);   // thread A
emergency.check(b);   // ❌ thread B — yarış

// 8. Boş curve kullanma
knst_sens::fan_curve empty;
// ... pwm_for her zaman 255 döner
```

---

## 🎯 20) Ne Zaman Kullanmalı?

### ✅ Kullan

- **CPU/GPU fan kontrolü** — sıcaklığa göre PWM
- **Sessiz sistem** — `silent` curve + hysteresis
- **Performance modu** — `performance` curve + agresif ramp
- **Server/kiosk** — emergency responder + failsafe
- **Embedded/industrial** — crash handler + watchdog
- **Fan sağlık izleme** — FanStall event
- **Sıcaklık loglama** — async monitor + history

### ❌ Kullanma

- **Windows fan kontrolü** — stub, `NotSupported`
- **Laptop fan** — genelde EC kontrolü, `/sys/class/hwmon` boş
- **Sıvı soğutma pompa** — `pwm*_enable` semantiği farklı olabilir
- **RGB fan kontrolü** — bu kütüphane renk yapmıyor
- **Voltaj/frekans ayarı** — undervolt/overclock, bu kütüphane yapmıyor

---

## 📌 21) API Kontrat Özeti

```cpp
namespace knst_sens {

// Temel tipler
struct sensor_stats, alarm, thermal_event;
enum class component : uint8_t { CPU, GPU, NVMe, Chipset, Motherboard, Battery, PSU, Network, Unknown };
enum class thermal_event::kind : uint8_t { Warn, Critical, Shutdown, SensorLost, FanStall };

// Yardımcılar
inline uint64_t now_ms() noexcept;
inline uint64_t steady_ms() noexcept;
inline uint32_t sanitize_min_pwm(uint32_t v) noexcept;
inline component classify_chip(const knst_c16string& chip) noexcept;
inline knst_c16string component_name(component c) noexcept;

// Sensor filter
class sensor_filter {
public:
    struct config;
    sensor_filter() noexcept;
    explicit sensor_filter(config c) noexcept;
    bool accept(const knst_sensor_info& s) noexcept;
    void reset() noexcept;
};

// Async monitor
class async_sensor_monitor {
public:
    struct config { uint32_t interval_ms, history_depth; bool enable_filter; };
    void set_config(config) noexcept;
    result<void> start() noexcept;
    void stop() noexcept;
    result<void> sample_once() noexcept;
    uint64_t age_ms() const noexcept;
    knst_vector<knst_sensor_info> snapshot() const noexcept;
    result<sensor_stats> stats(const knst_c16string&, const knst_c16string&) const noexcept;
    knst_vector<double> history(const knst_c16string&, const knst_c16string&) const noexcept;
    result<knst_sensor_info> find(knst_sensor_type, const char16_t* = u"") const noexcept;
    result<knst_sensor_info> hottest() const noexcept;
    knst_vector<alarm> over_limit(knst_sensor_type, double) const noexcept;
    result<double> component_temp(component) const noexcept;
    void reset() noexcept;
};

// Fan control (Linux only, Windows stub)
class fan_control {
public:
    static result<fan_control> open(const knst_c16string& chip, uint32_t index = 1,
                                     uint32_t min_safe_pwm = KNST_SENS_DEFAULT_MIN_SAFE_PWM) noexcept;
    bool valid() const noexcept;
    bool needs_restore() const noexcept;
    result<uint32_t> get_pwm() const noexcept;
    result<uint32_t> get_rpm() const noexcept;
    result<uint32_t> get_pwm_enable() const noexcept;
    result<void> set_pwm(uint32_t value) noexcept;   // ⚠️ min_safe_pwm altı REDDEDİLİR
    result<void> set_percent(uint32_t percent) noexcept;
    [[deprecated]] result<void> set_pwm_unsafe(uint32_t value) noexcept;
    result<void> set_manual(bool manual) noexcept;
    result<void> restore_auto() noexcept;   // ⚠️ GÜVENLİ: fanı asla yavaşlatmaz
    // move-only
};

// Fan curve
struct curve_point { double temp_c; uint32_t pwm; };
struct fan_curve {
    knst_vector<curve_point> points;
    double   hysteresis_c = 2.0;
    uint32_t ramp_step    = 8;
    uint32_t ramp_up_step = 255;
    uint32_t pwm_for(double) const noexcept;
    void normalize() noexcept;
    static fan_curve silent(), balanced(), performance();
};
struct curve_state { double eff_temp; bool valid; };
inline uint32_t curve_next_pwm(const fan_curve&, curve_state&, double temp,
                                uint32_t cur, uint32_t min_safe) noexcept;

// Fan curve controller
class fan_curve_controller {
public:
    fan_curve_controller(fan_control, fan_curve, uint32_t = KNST_SENS_DEFAULT_MIN_SAFE_PWM) noexcept;
    result<void> tick(double temp_c) noexcept;
    result<void> restore_auto() noexcept;
    uint32_t current_pwm() const noexcept;
    fan_curve& get_curve() noexcept;
    void set_curve(fan_curve) noexcept;
    fan_control& fan() noexcept;
};

// Event bus
class event_bus {
public:
    using handler = knst_function_shared<const thermal_event&>;
    uint32_t subscribe(handler) noexcept;
    void unsubscribe(uint32_t) noexcept;
    void emit(const thermal_event&) noexcept;
};

// Emergency responder
class emergency_responder {
public:
    enum class level : uint8_t { Normal, Warn, Critical, Shutdown };
    struct config;
    emergency_responder(event_bus&, const config& = {}) noexcept;
    void set_blast_callback(std::function<void()>) noexcept;
    void set_release_callback(std::function<void()>) noexcept;
    void check(const knst_sensor_info& hottest) noexcept;
    level current_level() const noexcept;
    void reset() noexcept;
};

// Orchestrator
struct fan_state { uint32_t stall; bool spinning; bool reported; };
struct fan_zone { /* ... */ };
class thermal_orchestrator {
public:
    result<void> add_zone(fan_zone) noexcept;
    void clear() noexcept;
    void tick(async_sensor_monitor&, event_bus* = nullptr, uint32_t stale_ms = 5000) noexcept;
    void emergency_full_blast() noexcept;
    void clear_emergency() noexcept;
    void restore_all_auto() noexcept;
};

// Watchdog
class fan_watchdog {
public:
    explicit fan_watchdog(uint32_t timeout_ms = 3000) noexcept;
    void attach(fan_control) noexcept;
    void heartbeat() noexcept;
    bool start() noexcept;
    void stop() noexcept;
    void emergency_restore() noexcept;
};

// Thermal manager (Linux only)
class thermal_manager {
public:
    struct config;
    result<void> start() noexcept;
    void stop() noexcept;
    result<void> add_zone(fan_zone) noexcept;
    async_sensor_monitor& monitor() noexcept;
    event_bus& events() noexcept;
    thermal_orchestrator& orchestrator() noexcept;
    fan_watchdog& watchdog() noexcept;
};

// Discovery
struct fan_descriptor { knst_c16string chip; uint32_t index; /* ... */ };
struct discovery_report { /* ... */ };
knst_vector<fan_descriptor> enumerate_fans() noexcept;
result<knst_vector<thermal_zone>> thermal_zones() noexcept;
discovery_report discover() noexcept;

// Crash safety (Linux only)
bool install_crash_handlers() noexcept;
void restore_all_fans_signal_safe() noexcept;

} // namespace knst_sens
```

---

## 🎯 Size Getireceği Kazanç

`knst_sens` şu durumlarda ciddi avantaj sağlar:

- ✅ **Güvenli fan kontrolü** — `min_safe_pwm` altına inmez, fan stop ettirilemez
- ✅ **Güvenli restore** — `restore_auto()` fanı asla yavaşlatmaz
- ✅ **Crash-safe** — signal handler fanları auto'ya döndürür
- ✅ **Watchdog** — heartbeat durursa fanlar restore edilir
- ✅ **Sensor filtering** — median + outlier rejection
- ✅ **Async monitor** — thread-safe, periyodik, geçmiş tutan
- ✅ **Fan curve** — hysteresis + ramp limiting
- ✅ **Zone orchestration** — birden fazla fanı tek sıcaklıkla yönet
- ✅ **Emergency responder** — kritik sıcaklıkta 255
- ✅ **Fail-safe** — sensör kayıp, NaN, boş curve → 255
- ✅ **Discovery** — chip + fan enumeration
- ✅ **Component classification** — CPU/GPU/NVMe/...
- ✅ **JSON export** — snapshot'ı JSON'a
- ✅ **KernelNucleusT uyumu** — `knst_devices`, `knst_sensor_info`, `knst_dev::status` ile tam entegre
- ✅ **Windows-aware** — fan kontrolü `NotSupported`, diğer katmanlar çalışır
- ✅ **Sıfır bağımlılık** — sadece POSIX sysfs

---