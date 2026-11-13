# knst_sensors — Thermal Management and Fan Control Library

Hey! 👋 The `knst_sens` namespace is a **thermal management + fan control** framework built on top of `knst_devices::sensors()`. You can periodically read temperature sensors, drive fans via PWM, define fan curves, blast everything to max on emergencies, and restore fans to a safe mode automatically on crash.

---

## 🚨 SAFETY DESIGN — READ THIS FIRST

Fan control **can permanently stop a fan if misused**. This library enforces strict safety rules:

### 1. `set_pwm(value)` **REJECTS values below `min_safe_pwm`** (default 40/255)

- **Including `value=0`.** There is **no `allow_stop` flag**.
- `min_safe_pwm` itself cannot go below `KNST_SENS_ABSOLUTE_MIN_PWM` (default 20). `open()` rejects it, controllers/zones clamp it.
- **Only bypass**: `set_pwm_unsafe()` — `[[deprecated]]`, don't use.

### 2. `restore_auto()` is **SAFE**:
- **Never lowers the fan below its present speed** (writes `max(min_safe_pwm, 128, current_pwm)`).
- Switches `pwmN_enable` back to **whatever mode the fan was in when it was opened** (2..5, e.g. 5 = SmartFan IV on nct67xx). Falls back to **2** if unknown.
- **Verifies** the mode (retries once), reports failure honestly.

### 3. `fan_control` only touches hardware when **you ask it to**

- Destructor only restores if **"dirty"** (this object changed it).
- **Moved-from** objects never restore (no double / premature restore).

### 4. `fan_curve_controller` / `thermal_orchestrator` / `thermal_manager` restore on destruction

### 5. `fan_watchdog` — restores all attached fans if the heartbeat stops, **re-arms itself** when the heartbeat comes back

### 6. Fail-safe behavior:
- **Sensor lost / stale / out of range for 3 ticks** → zone runs at `failsafe_pwm` (default 200), `SensorLost` event emitted.
- **Critical temperature** → all zones forced to 255 until temp drops below `critical - hysteresis`.
- **NaN temperature** → 255.
- **Empty fan curve** → 255.

### 7. Crash safety (opt-in): `install_crash_handlers()`
Installs async-signal-safe handlers for `SIGINT/SIGTERM/SIGHUP/SIGSEGV/SIGABRT/...` → puts every fan back to auto before the process dies. **SIGKILL and power loss cannot be caught** — for those, use systemd + `ExecStopPost=`.

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

| Feature | Linux | Windows | Android |
|---|---|---|---|
| **Sensor read** | ✅ `knst_devices::sensors()` | ✅ | ✅ |
| **Thermal zones** | ✅ `/sys/class/thermal` | ❌ | ⚠️ Partial |
| **Fan PWM** | ✅ `/sys/class/hwmon` | ❌ | ❌ |
| **Fan RPM** | ✅ | ❌ | ❌ |
| **Fan enumeration** | ✅ | ❌ | ❌ |
| **Crash handlers** | ✅ POSIX signal | ❌ | ⚠️ Partial |
| **Fan control** | ✅ Full | ⚠️ Stub | ❌ |
| **Watchdog** | ✅ | ⚠️ Stub | ❌ |
| **Orchestrator** | ✅ | ⚠️ Stub | ❌ |
| **Thermal manager** | ✅ | ❌ | ❌ |
| **Sensor filter** | ✅ | ✅ | ✅ |
| **Async monitor** | ✅ | ✅ | ✅ |
| **Event bus** | ✅ | ✅ | ✅ |

**Critical:** `fan_control` on Windows is a **stub** — `open()` always returns `NotSupported`. Fan control requires Linux.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Safe PWM** | Never below `min_safe_pwm`, fan can't be stopped |
| **Safe restore** | Never slows the fan, returns to original mode |
| **Crash-safe** | Signal handler restores fans to auto |
| **Sensor filter** | Median + outlier rejection, bad reads ignored |
| **Async monitor** | Thread-safe periodic sampling + history |
| **Fan curve** | Interpolation + hysteresis + ramp limiting |
| **Zone orchestration** | Drive multiple fans with one temperature |
| **Watchdog** | Fans restored if heartbeat stops |
| **Emergency** | 255 on all fans at critical temperature |
| **Discovery** | Chip + fan enumeration + suggestions |
| **Component classification** | Chip name → CPU/GPU/NVMe etc. |
| **JSON export** | Snapshot → JSON (`KNST_JSON_HPP`) |

---

## 🚀 Quick Start

### Simplest: run fan at 60%

```cpp
#include "KernelNucleusT.hpp"

auto f = knst_sens::fan_control::open(u"nct6798", 1);   // chip + index
if (f) {
    f.value.set_percent(60);   // 60%
}
// scope exits → automatic restore_auto() — safe
```

### Read temperature

```cpp
knst_sens::async_sensor_monitor mon;
mon.sample_once();

auto hottest = mon.hottest();
if (hottest) {
    std::cout << "Hottest: " << hottest.value.chip << "/" << hottest.value.name
              << " = " << hottest.value.value << "°C\n";
}
```

### Balanced fan curve

```cpp
knst_sens::fan_curve curve = knst_sens::fan_curve::balanced();

// or define your own
knst_sens::fan_curve custom;
custom.points = {{40.0, 80}, {60.0, 150}, {80.0, 230}, {95.0, 255}};
custom.hysteresis_c = 3.0;
custom.ramp_step    = 10;
```

### Full thermal manager

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
// ... run ...
mgr.stop();   // all fans restored to auto
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Sensor filter** | `sensor_filter::accept`, `reset` |
| **Async monitor** | `start`, `stop`, `sample_once`, `age_ms`, `snapshot`, `stats`, `history`, `find`, `hottest`, `over_limit`, `component_temp`, `reset` |
| **Fan control** | `open`, `set_pwm`, `set_percent`, `set_manual`, `restore_auto`, `get_pwm`, `get_rpm`, `get_pwm_enable` |
| **Fan curve** | `pwm_for`, `normalize`, `silent`, `balanced`, `performance` |
| **Curve controller** | `tick`, `restore_auto`, `current_pwm`, `get_curve`, `set_curve`, `fan` |
| **Event bus** | `subscribe`, `unsubscribe`, `emit` |
| **Emergency** | `check`, `current_level`, `set_blast_callback`, `set_release_callback`, `reset` |
| **Orchestrator** | `add_zone`, `clear`, `zone_count`, `tick`, `emergency_full_blast`, `clear_emergency`, `in_emergency`, `restore_all_auto` |
| **Watchdog** | `attach`, `heartbeat`, `start`, `stop`, `running`, `tripped`, `emergency_restore` |
| **Thermal manager** | `start`, `stop`, `running`, `add_zone`, `monitor`, `events`, `orchestrator`, `watchdog` |
| **Discovery** | `discover`, `enumerate_fans`, `thermal_zones` |
| **Crash safety** | `install_crash_handlers`, `restore_all_fans_signal_safe` |
| **JSON** | `snapshot_to_json` |

---

## 🔧 1) Config Macros

Define **before** including the header:

```cpp
#define KNST_SENS_DEFAULT_INTERVAL_MS    1000    // sampling interval (ms)
#define KNST_SENS_DEFAULT_HISTORY_DEPTH  300     // history depth (samples)
#define KNST_SENS_DEFAULT_MIN_SAFE_PWM   40      // min safe PWM (0-255)
#define KNST_SENS_ABSOLUTE_MIN_PWM       20      // absolute floor (0-255)
#define KNST_SENS_RESTORE_PWM            128     // restore minimum target
```

**`KNST_SENS_ABSOLUTE_MIN_PWM = 20`** — `min_safe_pwm` cannot go below this. `open()` rejects smaller values.

**`KNST_SENS_RESTORE_PWM = 128`** — the **minimum** speed the fan is restored to when `restore_auto()` is called. So during restore the fan never drops below 50%.

---

## 📦 2) Basic Types

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

| Kind | When |
|---|---|
| **Warn** | Temperature crossed `warn_c` |
| **Critical** | Temperature crossed `critical_c` |
| **Shutdown** | Temperature crossed `shutdown_c` |
| **SensorLost** | Zone sensor unreadable for 3 ticks |
| **FanStall** | Fan RPM=0 for 3 ticks despite PWM above `kStallMinPwm` |

### `component`

```cpp
enum class component : uint8_t {
    CPU, GPU, NVMe, Chipset, Motherboard, Battery, PSU, Network, Unknown
};
```

### `classify_chip(chip_name)` — Chip name → component

```cpp
knst_sens::classify_chip(u"coretemp");   // → CPU
knst_sens::classify_chip(u"amdgpu");     // → GPU
knst_sens::classify_chip(u"nvme0");      // → NVMe
knst_sens::classify_chip(u"nct6798");    // → Motherboard
knst_sens::classify_chip(u"pch_cannonlake"); // → Chipset
knst_sens::classify_chip(u"iwlwifi_1");  // → Network
```

Unknown chip → `Unknown`.

### `sanitize_min_pwm(v)`

```cpp
knst_sens::sanitize_min_pwm(10);    // → 20 (ABSOLUTE_MIN_PWM)
knst_sens::sanitize_min_pwm(300);   // → 255
knst_sens::sanitize_min_pwm(50);    // → 50
```

Clamps a user-supplied `min_safe_pwm` into the safe range.

### `now_ms()` / `steady_ms()`

```cpp
uint64_t wall  = knst_sens::now_ms();      // epoch ms (event timestamp)
uint64_t steady = knst_sens::steady_ms();  // monotonic +1 (not 0 sentinel)
```

`steady_ms()` is **monotonic** and **never returns 0** — so `age_ms() == 0` means "never sampled", not "just sampled".

---

## 🔍 3) `sensor_filter` — Outlier Rejection

```cpp
struct sensor_filter::config {
    // Physical limits (out of range → reject immediately)
    double temp_min  = -40.0,  temp_max  = 150.0;
    double fan_min   = 0.0,    fan_max   = 20000.0;
    double volt_min  = -30.0,  volt_max  = 30.0;
    double curr_min  = -100.0, curr_max  = 100.0;
    double power_min = 0.0,    power_max = 2000.0;

    // Median window
    uint32_t window  = 5;

    // Deviation check
    double   deviation_ratio = 0.5;      // 50% of median
    uint32_t min_for_median  = 3;        // accept first 3 reads
    uint32_t max_consecutive_rejects = 3; // after 3 rejects, treat as real change
};
```

**How it works?**

1. **Physical range check**: value outside `[min, max]` → reject
2. **NaN/Inf check**: not `isfinite` → reject
3. **Median deviation**: compare with median of last `window` (5) reads
   - `deviation > max(2, |median| × ratio)` → reject
   - But if rejected `max_consecutive_rejects` (3) times **in a row** → treat as real change, reset history
4. **If accepted** → add to window (FIFO, `window` size)

> 💡 **Why `max_consecutive_rejects`?** If a sensor genuinely jumps (e.g. thermal throttling → temp goes from 30°C to 85°C at once), the filter could reject forever. This setting treats 3 consecutive rejects as "real change" and resets the window.

### Usage

```cpp
knst_sens::sensor_filter filter;
for (auto& s : sensors) {
    if (filter.accept(s)) {
        // valid read, use it
    }
}
```

`async_sensor_monitor` applies this filter **automatically** (`enable_filter = true`).

---

## 📊 4) `async_sensor_monitor` — Thread-Safe Sampling

```cpp
struct async_sensor_monitor::config {
    uint32_t interval_ms   = KNST_SENS_DEFAULT_INTERVAL_MS;   // 1000
    uint32_t history_depth = KNST_SENS_DEFAULT_HISTORY_DEPTH; // 300
    bool enable_filter     = true;
};
```

### Basic usage

```cpp
knst_sens::async_sensor_monitor mon;
knst_sens::async_sensor_monitor::config cfg;
cfg.interval_ms = 500;      // sample every 500ms
cfg.history_depth = 600;    // 5 minutes of history

mon.set_config(cfg);
mon.start();

// ... run ...

mon.stop();
```

### `sample_once()` — One-shot

```cpp
if (!mon.sample_once()) {
    std::cerr << "No sensors\n";
}
```

Samples once without starting a thread. **Required** before `start()` (so the monitor has initial data).

### `snapshot()` — Full list of last sample

```cpp
auto list = mon.snapshot();
for (auto& s : list) {
    std::cout << s.chip << "/" << s.name << " = " << s.value << s.unit << "\n";
}
```

### `stats(chip, name)` — Statistics

```cpp
auto st = mon.stats(u"coretemp", u"Package id 0");
if (st) {
    std::cout << "Min: " << st.value.min << "\n";
    std::cout << "Max: " << st.value.max << "\n";
    std::cout << "Avg: " << st.value.avg << "\n";
    std::cout << "Last: " << st.value.last << "\n";
    std::cout << "Count: " << st.value.count << "\n";
}
```

### `history(chip, name)` — Ordered history

```cpp
auto hist = mon.history(u"coretemp", u"Package id 0");
// hist[0] = oldest, hist[last] = newest
```

Up to `history_depth` samples. Newest at the end.

### `find(type, needle)` — First match

```cpp
auto s = mon.find(knst_sensor_type::Temperature, u"Package");
if (s) std::cout << s.value.chip << "/" << s.value.name << "\n";

// empty needle → any Temperature
auto any = mon.find(knst_sensor_type::Temperature);
```

### `hottest()` — Hottest sensor

```cpp
auto s = mon.hottest();
if (s) {
    std::cout << "Hottest: " << s.value.chip << "/" << s.value.name
              << " = " << s.value.value << "°C\n";
}
```

Only `Temperature` type, physically valid (`-40..150`).

### `over_limit(type, limit)` — Sensors above limit

```cpp
auto alarms = mon.over_limit(knst_sensor_type::Temperature, 85.0);
for (auto& a : alarms) {
    std::cout << "⚠ " << a.chip << "/" << a.name
              << " = " << a.value << " > " << a.limit << "\n";
}
```

### `component_temp(component)` — Per-component

```cpp
auto cpu = mon.component_temp(knst_sens::component::CPU);
if (cpu) std::cout << "CPU: " << cpu.value << "°C\n";

auto gpu = mon.component_temp(knst_sens::component::GPU);
if (gpu) std::cout << "GPU: " << gpu.value << "°C\n";
```

Classifies chips via `classify_chip` into CPU/GPU/NVMe/... and returns the **hottest** in that category.

### `age_ms()` — Time since last sample

```cpp
uint64_t age = mon.age_ms();
if (age > 5000) {
    std::cerr << "Sensor data older than 5 seconds!\n";
}
```

`UINT64_MAX` = never sampled. `steady_ms()` is monotonic, so this is sane.

### `reset()`

```cpp
mon.reset();   // clears stats and history
```

---

## 🌀 5) `fan_control` — PWM Control (Linux)

### Opening

```cpp
auto f = knst_sens::fan_control::open(
    u"nct6798",                              // chip name
    1,                                       // fan index (1..16)
    KNST_SENS_DEFAULT_MIN_SAFE_PWM           // min_safe_pwm (default 40)
);
if (!f) {
    // NotFound → chip not found
    // InvalidArgument → index 0/17, min_safe_pwm < 20, invalid chip name
}
```

**Validation:**
- `index` must be 1..16
- `min_safe_pwm` must be `[ABSOLUTE_MIN_PWM=20, 255]`
- `chip` must pass `is_safe_token(_, 32)` — path traversal (`../`) blocked

**Chip discovery:**
- Finds the directory where `/sys/class/hwmon/hwmonN/name` matches the chip name
- Succeeds if `/sys/class/hwmon/hwmonN/pwm{index}` exists

### PWM read/write

```cpp
// Read
auto pwm = f.value.get_pwm();     // 0..255
auto rpm = f.value.get_rpm();     // 0..20000
auto en  = f.value.get_pwm_enable();   // 0..5 (mode)

// SAFE write — below min_safe_pwm REJECTED
f.value.set_pwm(120);             // OK (above 40)
f.value.set_pwm(0);               // ❌ InvalidArgument
f.value.set_pwm(30);              // ❌ InvalidArgument (min=40)
f.value.set_pwm(300);             // ❌ InvalidArgument (>255)

// Percent
f.value.set_percent(50);          // → PWM 128 (50 × 255 / 100)
f.value.set_percent(0);           // ❌ InvalidArgument (min=40 → pwm=0 < 40)

// Manual mode
f.value.set_manual(true);         // pwm_enable = 1 (manual)
f.value.set_manual(false);        // pwm_enable = original mode (or 2)
```

### ⚠️ `set_pwm_unsafe()` — DEPRECATED

```cpp
[[deprecated]]
f.value.set_pwm_unsafe(0);    // ❌ Compiles but warns
                              // ⚠️ Can STOP the fan!
```

**Only bypass.** Use only if you *really* need to stop the fan. Normally **don't**.

### `restore_auto()` — Safe restore

```cpp
auto r = f.value.restore_auto();
if (!r) {
    std::cerr << "Restore failed — check manually!\n";
}
```

**What it does?**
1. **Switch to manual** (`pwm_enable = 1`)
2. **Write PWM**: `max(min_safe, KNST_SENS_RESTORE_PWM=128, current_pwm)`
   - So the fan is **never slowed down**
   - Currently 200 → stays 200
   - Currently 50 → raised to 128
3. **Switch back to original mode** (`orig_enable_`, 2..5; else 2)
4. **Verify** (retries once, still wrong → `BadState`)

### `needs_restore()`

```cpp
if (f.value.needs_restore()) {
    // this object changed the fan and hasn't restored it
}
```

The `dirty_` flag. Destructor checks it.

### Move-only

```cpp
knst_sens::fan_control a = std::move(f.value);   // ✅ OK
knst_sens::fan_control b = f.value;              // ❌ No copy
```

Moved-from source gets `base_[0] = '\0'`, `dirty_ = false`, `slot_ = -1`. So **source never restores** — no double restore.

### On Windows

```cpp
auto f = knst_sens::fan_control::open(u"nct6798", 1);
// f → NotSupported
```

Fan control is a **stub** on Windows. All methods return `NotSupported`.

---

## 📈 6) `fan_curve` — Temperature → PWM Curve

```cpp
struct curve_point {
    double   temp_c = 0.0;
    uint32_t pwm    = 0;      // 0..255
};

struct fan_curve {
    knst_vector<curve_point> points;
    double   hysteresis_c = 2.0;
    uint32_t ramp_step    = 8;
    uint32_t ramp_up_step = 255;   // 255 = instant
};
```

### Built-in curves

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

`silent` → `hysteresis_c=3, ramp_step=5` (gentle)
`balanced` → `hysteresis_c=2, ramp_step=10`
`performance` → `hysteresis_c=1.5, ramp_step=20` (aggressive)

### Your own curve

```cpp
knst_sens::fan_curve custom;
custom.points = {
    {40.0, 80},
    {55.0, 130},
    {70.0, 200},
    {85.0, 255}
};
custom.hysteresis_c = 2.5;
custom.ramp_step    = 10;    // descent: max 10/tick
custom.ramp_up_step = 40;    // climb: max 40/tick
```

`points` must be **sorted** or call `normalize()`:

```cpp
custom.normalize();   // sort by temp_c
```

### `pwm_for(temp_c)` — Interpolation

```cpp
uint32_t target = custom.pwm_for(62.0);
```

- Temp below first point → first point's PWM
- Temp above last point → last point's PWM
- In between → **linear interpolation** (integer rounding)
- **Empty curve** → **255** (fail-safe)
- **NaN temp** → **255** (fail-safe)

### Hysteresis — Only **delays cooling down**

> ⚠️ **IMPORTANT:** Rising temp is always followed **immediately**. Hysteresis only kicks in on the **way down**.

```cpp
// eff_temp = 65°C (previous)
// new temp = 63°C
// hysteresis = 3°C
// → eff_temp stays 65, PWM doesn't drop (waiting for cooling)

// new temp = 61°C (65 - 3 - 1 = 61 < hyst)
// → eff_temp drops to 61, PWM drops
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
2. Apply hysteresis → `eff_temp`
3. `pwm_for(eff_temp)` → target
4. **Clamp below `min_safe`**
5. **Rate limiting**:
   - `target > current`: `ramp_up_step` applied (0 = unlimited)
   - `target < current`: `ramp_step` applied (0 = unlimited)
6. Clamp again to `min_safe..255`

**Why ramp?** An instant PWM change can damage the fan motor and create noise. With `ramp_step = 10`, at most 10 PWM changes per tick → smooth transition from 0 to 255 over 25 ticks.

> 💡 **`ramp_up_step = 255`** (default) → **climb instant**. Safe: no delay when temp rises.
> `ramp_step = 8` (default) → **descent slow**. Also safe: fan never stops immediately.

> 💡 **Old code:** "skip if temp barely changed" logic existed → fan could be stuck halfway. New code keeps ramping every tick.

---

## 🎮 7) `fan_curve_controller` — Curve + Fan

```cpp
knst_sens::fan_control fan = /* ... */;
knst_sens::fan_curve curve = knst_sens::fan_curve::balanced();

knst_sens::fan_curve_controller ctrl(std::move(fan), curve);

// every tick
auto r = ctrl.tick(65.0);   // 65°C → appropriate PWM
```

### Startup behavior

```cpp
// On the first tick it reads the current PWM
auto p = ctrl.fan().get_pwm();
// current_pwm_ = p (if within min_safe..255)
```

This matters: if the fan is already at 150, it **won't drop to 40 instantly** on the first tick. It ramps from the current PWM.

### Rules

- `min_safe_` = `max(sanitize_min_pwm(user), fan.min_safe_pwm())`
- `tick(temp)` → `curve_next_pwm` → `fan.set_pwm(target)` (if changed)
- `set_curve(new)` → state reset
- `restore_auto()` → `fan.restore_auto()`

### Destructor

Holds the `fan_control` internally. On destruction the `fan_control` destructor runs → **restores if dirty**. So if `fan_curve_controller` leaves scope, the fan is safe.

---

## 📢 8) `event_bus`

```cpp
knst_sens::event_bus bus;

auto id = bus.subscribe([](const knst_sens::thermal_event& e) {
    if (e.type == knst_sens::thermal_event::kind::Critical) {
        std::cerr << "CRITICAL! " << e.chip << " = " << e.value << "°C\n";
    }
});

// ... run ...

bus.unsubscribe(id);
```

### `subscribe` / `unsubscribe`

- Thread-safe (mutex)
- `handler` = `knst_function_shared<const thermal_event&>`
- `subscribe` → returns `uint32_t id`
- `unsubscribe(id)` → removes that handler

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

**Out-of-mutex dispatch:** Handler list is copied, then called outside the mutex. So:
- If a handler runs long, new `subscribe`s aren't blocked
- Calling `subscribe`/`unsubscribe` inside a handler is safe

**Exception handling:** `try { h(e); } catch (...) {}` — if one handler throws, others aren't affected. **But exception info is lost** — catch inside the handler if you want to log.

> ⚠️ **Thread note:** Handlers run on the **manager thread**. Don't call `stop()` inside them (deadlock).

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

    bool valid() const noexcept;   // warn < critical < shutdown, all finite
};
```

### Levels

| Level | Threshold |
|---|---|
| **Normal** | `temp < warn_c` |
| **Warn** | `warn_c <= temp < critical_c` |
| **Critical** | `critical_c <= temp < shutdown_c` |
| **Shutdown** | `temp >= shutdown_c` |

### Usage

```cpp
knst_sens::event_bus bus;
knst_sens::emergency_responder emergency(bus);

emergency.set_blast_callback([] {
    std::cerr << "EMERGENCY! Fans to max\n";
});

emergency.set_release_callback([] {
    std::cerr << "Temperature back to normal\n";
});

// every tick
auto hottest = mon.hottest();
if (hottest) {
    emergency.check(hottest.value);
}
```

### Behavior

**Rising** (`raw >= level_`):
- Moves to new level instantly

**Falling** (`raw < level_`):
- Doesn't drop until `temp < threshold(nl) - hysteresis_c`
- So temp must fall **5°C below** Warn to leave Warn → Normal

**Event emit:**
- Level changed → emit
- Same level for `repeat_ms` (10 sec) → emit (re-remind)

**Blast/release callback:**
- `>= Critical` → `blast_()` (only if `full_blast_on_critical`)
- Leaving Critical → `release_()`

### `current_level()`

```cpp
auto lvl = emergency.current_level();
// Normal / Warn / Critical / Shutdown
```

### Thread-safety

**None.** `check()` must be called from one thread (the manager thread).

---

## 🎯 10) `fan_zone` and `thermal_orchestrator`

### `fan_zone`

```cpp
struct fan_zone {
    knst_c16string name;                          // "cpu", "gpu"
    knst_c16string temp_chip;                     // "coretemp"
    knst_c16string temp_name;                     // empty = chip's hottest
    fan_curve curve;
    knst_vector<knst_c16string> fan_chips;        // {"nct6798", "nct6798"}
    knst_vector<uint32_t> fan_indices;            // {1, 2}
    uint32_t min_safe_pwm = KNST_SENS_DEFAULT_MIN_SAFE_PWM;
    uint32_t failsafe_pwm = 200;

    // runtime state (managed by orchestrator)
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

**`temp_name` empty** → uses the **hottest** sensor of `temp_chip`.
**`temp_name` set** → only that name matches.

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

// every tick
orch.tick(monitor, &bus);   // mon = async_sensor_monitor, bus optional
```

### `tick()` — what it does

Per zone:

1. **`ensure_fans(z)`** — open missing fans (retry cooldown 5 ticks)
2. **`init_current(z)`** — read current PWM
3. **If in emergency**: `apply(z, 255)` → all max
4. **Read temperature**:
   - `stale` (> `stale_ms`) → treat as lost
   - No sensor → `lost_ticks++`
   - `lost_ticks >= kLostTicks(3)` → `failsafe_pwm` + `SensorLost` event (once)
   - Sensor back → `lost_ticks=0`, `lost_reported=false`
5. **`curve_next_pwm`** → target PWM
6. **If changed, `apply(z, target)`**
7. **`check_stall(z, pending)`** — fan stall check

**Events emitted outside the mutex** → handlers can call back into the orchestrator.

### `emergency_full_blast()` / `clear_emergency()`

```cpp
orch.emergency_full_blast();   // all zones 255
orch.clear_emergency();         // back to normal curve
orch.in_emergency();            // true/false
```

### `restore_all_auto()`

```cpp
orch.restore_all_auto();   // restore all fans to auto
```

Destructor calls this automatically.

### Stall detection

```cpp
static constexpr uint32_t kLostTicks   = 3;
static constexpr uint32_t kStallTicks  = 3;
static constexpr uint32_t kStallMinPwm = 80;
```

**Stall check** only makes sense while `current_pwm >= 80`. `rpm=0` for 3 ticks → `FanStall` event.

> ⚠️ **Fan stall is information, not prevention.** Emits an event, **doesn't stop or restart** the fan. You handle that in the handler.

---

## 🐕 11) `fan_watchdog`

```cpp
knst_sens::fan_watchdog wd(3000);   // 3-second timeout

// Attach fans
wd.attach(std::move(fan1));
wd.attach(std::move(fan2));

wd.start();

// Manager thread every tick:
wd.heartbeat();

// ... run ...

wd.stop();
```

### What it does

1. **`start()`** → starts the heartbeat, spawns the thread
2. **`heartbeat()`** → updates `last_hb_ns_`
3. **Watchdog thread** checks every 50ms:
   - `now - last_hb > timeout_ms` → **`emergency_restore()`** (restores all fans) + `tripped_=true`
   - Heartbeat back → `tripped_=false` (re-armed)

### Timeout limits

Minimum **500ms**. Smaller values are clamped.

### Inside `thermal_manager`

```cpp
watchdog_.set_timeout_ms(std::max(
    cfg_.watchdog_timeout_ms,
    cfg_.tick_interval_ms * 3u + 1000u
));
```

So if `tick_interval = 1000`, timeout is at least **4000ms**. Prevents false positives on missed ticks.

### Crash safety

Together with `restore_all_fans_signal_safe()` (below):
- **Normal run**: watchdog heartbeat check
- **Crash**: signal handler

They complement each other.

---

## 🖥️ 12) `thermal_manager` — All-in-One Facade (Linux)

Top level. Contains:
- `async_sensor_monitor` (sensor sampling)
- `fan_watchdog` (heartbeat)
- `thermal_orchestrator` (zone control)
- `event_bus` (events)
- `emergency_responder` (critical temperature)

### Config

```cpp
struct thermal_manager::config {
    uint32_t tick_interval_ms    = 1000;   // control tick
    uint32_t sample_interval_ms  = 1000;   // sensor sampling
    uint32_t history_depth       = 300;
    uint32_t watchdog_timeout_ms = 3000;
    emergency_responder::config emergency;
    knst_vector<knst_c16string> emergency_ignore_chips;
};
```

**`emergency_ignore_chips`** — e.g. if "nct6798" chip always reports 200°C (broken sensor), exempt it from emergency.

### Full usage

```cpp
knst_sens::thermal_manager mgr;

// 1) Add zone (fans must be openable)
knst_sens::fan_zone cpu_zone;
cpu_zone.name        = u"cpu";
cpu_zone.temp_chip   = u"coretemp";
cpu_zone.temp_name   = u"Package id 0";   // optional
cpu_zone.curve       = knst_sens::fan_curve::balanced();
cpu_zone.fan_chips   = {u"nct6798"};
cpu_zone.fan_indices = {1};
cpu_zone.min_safe_pwm = 40;
cpu_zone.failsafe_pwm = 220;

auto r = mgr.add_zone(std::move(cpu_zone));
if (!r) {
    std::cerr << "Zone add failed\n";
    return 1;
}

// 2) Subscribe to events
auto sub = mgr.events().subscribe([](const knst_sens::thermal_event& e) {
    switch (e.type) {
        case knst_sens::thermal_event::kind::Warn:
            std::cout << "Warn: " << e.value << "°C\n"; break;
        case knst_sens::thermal_event::kind::Critical:
            std::cerr << "CRITICAL: " << e.value << "°C\n"; break;
        case knst_sens::thermal_event::kind::FanStall:
            std::cerr << "Fan stalled: " << e.name << "\n"; break;
        case knst_sens::thermal_event::kind::SensorLost:
            std::cerr << "Sensor lost: " << e.chip << "\n"; break;
        default: break;
    }
});

// 3) Start
r = mgr.start();
if (!r) {
    std::cerr << "Manager start failed\n";
    return 1;
}

// 4) Run...
std::this_thread::sleep_for(std::chrono::seconds(60));

// 5) Stop (all fans restore_auto)
mgr.stop();
```

### `start()` — what it does

1. **`cfg_.emergency.valid()`** check (warn < critical < shutdown)
2. `sample_interval >= 50`, `tick_interval ∈ [100, 60000]` check
3. `stale_ms_ = max(5000, max(sample, tick) × 5)`
4. `monitor_.set_config()` + `sample_once()` (**mandatory** first data)
5. `monitor_.start()`
6. `watchdog_.set_timeout_ms(max(given, tick×3 + 1000))` + `start()`
7. `emergency_.set_blast_callback()` → `orchestrator_.emergency_full_blast()`
8. `emergency_.set_release_callback()` → `orchestrator_.clear_emergency()`
9. Spawns manager thread → each tick: `heartbeat` + `tick` + `check_emergency`

### `stop()` — what it does

1. Stop thread
2. `monitor_.stop()`
3. `watchdog_.stop()`
4. `orchestrator_.clear_emergency()`
5. `orchestrator_.restore_all_auto()` (even if never started)

### `add_zone(z)` — opens fans first

```cpp
// Open fans
knst_vector<fan_control> probes;
for (chip, index) {
    auto f = fan_control::open(chip, index, min_pwm);
    if (!f) return f;   // if any fails, zone rejected
    probes.push_back(std::move(f));
}

// Add zone
auto r = orchestrator_.add_zone(std::move(z));

// Attach fans to watchdog
for (auto& p : probes) watchdog_.attach(std::move(p));
```

So **all fans must open** — if one fails, `add_zone` fails (no partial zone).

### Accessors

```cpp
mgr.monitor();       // async_sensor_monitor&
mgr.events();        // event_bus&
mgr.orchestrator();  // thermal_orchestrator&
mgr.watchdog();      // fan_watchdog&
```

Direct access if you want.

---

## 🛡️ 13) Crash Safety (Linux)

### `install_crash_handlers()`

```cpp
int main() {
    knst_sens::install_crash_handlers();   // ONCE, at startup
    
    // ... use the library ...
}
```

**Signals:** `SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT`

**Behavior:**
1. Writes every `restore_slot` in async-signal-safe fashion (`open`+`write`+`close`)
2. Sets handler to `SIG_DFL` (`SA_RESETHAND`)
3. Re-raises the signal (process still dies, but fans already restored)

**Cannot catch:**
- **`SIGKILL`** — no library can catch
- **Power loss** — OS isn't even shutting down

For these, use systemd:

```ini
[Service]
ExecStart=/usr/bin/my_app
ExecStopPost=/usr/bin/knst-fan-restore.sh
```

`ExecStopPost` runs after process death → script can call `restore_all_fans_signal_safe()`.

### `restore_all_fans_signal_safe()`

```cpp
void my_signal_handler(int sig) {
    knst_sens::restore_all_fans_signal_safe();
    // ... other work ...
}
```

Call from your own handler. Async-signal-safe (uses `open`+`write`+`close`).

### Restore Slot Mechanism

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

- **On each `set_pwm`** (first dirty), a slot is claimed
- The slot pre-stores which path gets what value (`snprintf` in normal context)
- Signal handler only does `open`+`write`+`close` → async-signal-safe
- On successful `restore_auto()`, slot freed (`state = 0`)

**Max 64 slots** — if you control more than 64 fans, some won't be covered by crash restore.

---

## 🔎 14) Discovery — `discover()`

```cpp
auto r = knst_sens::discover();

// Found chips
for (auto& c : r.sensor_chips) {
    std::cout << "Chip: " << c << "\n";
}

// Found fans
for (auto& f : r.fans) {
    std::cout << f.chip << " pwm" << f.index
              << " = " << f.current_pwm
              << "  rpm = " << f.current_rpm
              << "  enable = " << f.current_enable << "\n";
}

// Suggestion texts
for (auto& s : r.suggestions) {
    std::cout << s << "\n";
}
```

**What it does:**
1. Collects all chip names via `knst_devices::sensors()`
2. Scans `/sys/class/hwmon/*/pwmN` files
3. For each pwm, reads `fanN_input` and `pwmN_enable` if present
4. If no fans → **suggestion text** (e.g. `it87` fork for IT8665E)

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

Reads `/sys/class/thermal/thermal_zone*`. **Filters sentinel values:**
- Qualcomm HAL sometimes returns `867000` (= `INT_MAX/1000`) → `-40..150` filter rejects these.

---

## 📄 15) JSON Export (Conditional)

If `KNST_JSON_HPP` is included:

```cpp
auto snapshot = mon.snapshot();
knst_json j = knst_sens::snapshot_to_json(snapshot);
// j → [{ chip, name, type, value, unit }, ...]
```

---

## ⚠️ 16) Common Gotchas

### 1) `set_pwm(0)` CANNOT be called

```cpp
f.value.set_pwm(0);   // ❌ InvalidArgument (min_safe=40)
f.value.set_percent(0); // ❌ InvalidArgument (pwm=0 < 40)
```

**Intentional design.** Fan cannot be stopped. `set_pwm_unsafe` is deprecated.

### 2) `min_safe_pwm` cannot go below 20

```cpp
auto f = fan_control::open(u"nct6798", 1, 10);   // ❌ InvalidArgument
auto f = fan_control::open(u"nct6798", 1, 20);   // ✅ OK
auto f = fan_control::open(u"nct6798", 1, 15);   // ❌ InvalidArgument
```

### 3) Moved fan never restores

```cpp
knst_sens::fan_control a = knst_sens::fan_control::open(...).value;
knst_sens::fan_control b = std::move(a);
// a → base_[0]='\0', dirty_=false, slot_=-1
// a's destructor does nothing
```

**Desired behavior** — no double restore.

### 4) `restore_auto()` never slows down the fan

```cpp
// Fan is currently at 200
f.value.restore_auto();
// → PWM = max(40, 128, 200) = 200
// Fan doesn't speed up, just switches to auto mode
```

### 5) On Windows `fan_control::open` always returns `NotSupported`

Fan control needs **Linux**.

### 6) `thermal_manager::add_zone` opens fans first

```cpp
// If a fan can't be opened → zone rejected
auto r = mgr.add_zone(zone);
if (!r) {
    // fan couldn't be opened
}
```

No partial zone — all or nothing.

### 7) `emergency_responder` is not thread-safe

`check()` must be called from one thread. The manager already does this.

### 8) Call `install_crash_handlers()` **once**

After calling it, `SIGINT` handling **overwrites your handler**. If you have your own handlers, install them **first**, then `install_crash_handlers()`.

### 9) `SA_RESETHAND` — fires once

Signal handler reverts to default after receiving a signal. So the **second** time the signal arrives, default behavior (crash) runs. This is **intentional** — prevents infinite loops in the handler.

### 10) `kLostTicks = 3` — slow but safe

Sensor unreadable for 3 ticks → failsafe. With `tick_interval = 1000`, that's **3 seconds**. Want faster? Lower `tick_interval`.

### 11) No stall check below `kStallMinPwm = 80`

```cpp
if (z.current_pwm < kStallMinPwm) return;
```

If PWM is low, RPM=0 can be normal (fan not spinning at low speed). Prevent false alarms.

### 12) `failsafe_pwm` vs `min_safe_pwm` — be careful

`failsafe_pwm` used when sensor is lost → should be **high** (200 recommended). `min_safe_pwm` is the normal working floor (40).

### 13) `fan_curve::pwm_for` empty curve → 255

```cpp
knst_sens::fan_curve empty;
empty.pwm_for(50.0);   // → 255
```

**Fail-safe.** Empty curve = undefined = full speed.

### 14) `curve_next_pwm` NaN → 255

```cpp
knst_sens::curve_state st;
knst_sens::curve_next_pwm(curve, st, NAN, 100, 40);   // → 255
```

**Fail-safe.** NaN temp = sensor broken = full speed.

### 15) `ramp_up_step = 255` (default) → instant climb

```cpp
// Default: temp goes from 60 to 90 → target PWM instantly
// ramp_step (descent) = 8 → target from 255 to 40 takes 27 ticks
```

Safe design: **climb fast, descend slow**.

### 16) `thermal_manager::start` requires first sample

```cpp
auto r = mgr.start();
if (!r) {
    // monitor_.sample_once() failed → no sensors
}
```

Manager won't start without **mandatory** sensor data first. If no sensors, it doesn't start.

### 17) `watchdog_timeout_ms` minimum

```cpp
mgr.set_timeout_ms(100);   // → clamped to 500
```

### 18) `add_zone` rejects empty `fan_chips`

```cpp
zone.fan_chips = {};   // ❌ InvalidArgument
```

### 19) `restore_auto()` can fail

```cpp
auto r = f.value.restore_auto();
if (!r) {
    // ⚠️ Fan may still be in manual mode!
    // Manually set pwm_enable = 2
}
```

Sysfs write can fail. Check `result`.

### 20) `discover()` limited in containers

If `/sys/class/hwmon` isn't mounted (Docker/snap/flatpak), returns empty.

---

## 📊 17) Performance

| Operation | Cost | Note |
|---|---|---|
| `sample_once()` | O(sensors) | ~10-100 sysfs reads |
| `snapshot()` | O(sensors) | Mutex + copy |
| `stats()` / `history()` | O(sensors) | Linear search |
| `hottest()` | O(sensors) | Temp filter |
| `component_temp()` | O(sensors) | `classify_chip` + filter |
| `fan_control::open` | O(hwmon) | Directory scan + file stat |
| `set_pwm` | O(1) | 1-2 sysfs writes |
| `restore_auto` | O(1) + 50ms | Verify wait |
| `curve_next_pwm` | O(points) | Curve size (typically 4-5) |
| `tick` (zone) | O(fans + sensors) | `set_pwm` per fan |
| `emergency.check` | O(1) | Just number comparison |
| `watchdog` | O(1) / 50ms | Fixed interval |

**Typical latencies:**
- `sample_once`: 10-50ms (depends on sensor count)
- `tick` (one zone): 5-20ms (sysfs write + verify)
- `restore_auto`: 50-100ms (verify wait)

---

## 🌍 18) Platform Differences

| Feature | Linux | Windows | Android |
|---|---|---|---|
| **Sensor read** | sysfs | WMI | JNI |
| **Fan PWM** | sysfs | ❌ | ❌ |
| **Crash restore** | signal | ❌ | ❌ |
| **Watchdog** | ✅ | ⚠️ stub | ⚠️ stub |
| **Thermal manager** | ✅ | ❌ | ❌ |
| **install_crash_handlers** | ✅ | ❌ | ❌ |

**On Windows `fan_control` is a stub** — `open()` always returns `NotSupported`. Other layers (sensor_filter, monitor, event_bus, emergency) work.

**On Android** sensors via JNI (`knst_devices`), but no fan control.

---

## ✅ 19) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Respect min_safe_pwm
auto f = fan_control::open(u"nct6798", 1, 40);
f.value.set_pwm(120);   // safe

// 2. Let scope exit handle it (automatic restore)
{
    auto temp_fan = fan_control::open(u"nct6798", 2, 60);
    temp_fan.value.set_percent(80);
}   // destructor calls restore_auto

// 3. Call install_crash_handlers once in main
int main() {
    knst_sens::install_crash_handlers();
    // ...
}

// 4. Use thermal_manager (safest layer)
knst_sens::thermal_manager mgr;
mgr.add_zone(cpu_zone);
mgr.start();
// ...
mgr.stop();   // all restore automatic

// 5. Keep sensor filter on
knst_sens::async_sensor_monitor::config cfg;
cfg.enable_filter = true;   // default

// 6. Use emergency_ignore_chips for known broken sensors
cfg.emergency_ignore_chips = {u"nct6798"};
```

### ❌ Don't

```cpp
// 1. Don't call set_pwm(0)
f.value.set_pwm(0);   // ❌ InvalidArgument

// 2. Don't use set_pwm_unsafe
f.value.set_pwm_unsafe(0);   // ❌ deprecated + fan may stop

// 3. Don't go below min_safe_pwm 20
auto f = fan_control::open(u"nct6798", 1, 10);   // ❌ InvalidArgument

// 4. Don't leave in manual mode
f.value.set_manual(true);
// ... something else ...
// f destructor calls restore_auto ✅
// but if you don't call restore_auto, fan stays in manual mode (until next reboot)

// 5. Don't call install_crash_handlers multiple times
install_crash_handlers();
install_crash_handlers();   // ⚠️ harmless but redundant

// 6. Don't call stop() from a handler
bus.subscribe([&](auto& e) {
    mgr.stop();   // ❌ deadlock (called on manager thread)
});

// 7. Don't call emergency from two threads
emergency.check(a);   // thread A
emergency.check(b);   // ❌ thread B — race

// 8. Don't use an empty curve
knst_sens::fan_curve empty;
// ... pwm_for always returns 255
```

---

## 🎯 20) When to Use?

### ✅ Use

- **CPU/GPU fan control** — PWM based on temperature
- **Silent systems** — `silent` curve + hysteresis
- **Performance mode** — `performance` curve + aggressive ramp
- **Server/kiosk** — emergency responder + failsafe
- **Embedded/industrial** — crash handler + watchdog
- **Fan health monitoring** — FanStall event
- **Temperature logging** — async monitor + history

### ❌ Don't use

- **Windows fan control** — stub, `NotSupported`
- **Laptop fans** — usually EC controlled, `/sys/class/hwmon` empty
- **Liquid cooling pump** — `pwm*_enable` semantics may differ
- **RGB fan control** — this library doesn't do color
- **Voltage/frequency tuning** — undervolt/overclock, not this library

---

## 📌 21) API Contract Summary

```cpp
namespace knst_sens {

// Basic types
struct sensor_stats, alarm, thermal_event;
enum class component : uint8_t { CPU, GPU, NVMe, Chipset, Motherboard, Battery, PSU, Network, Unknown };
enum class thermal_event::kind : uint8_t { Warn, Critical, Shutdown, SensorLost, FanStall };

// Helpers
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
    result<void> set_pwm(uint32_t value) noexcept;   // ⚠️ below min_safe_pwm REJECTED
    result<void> set_percent(uint32_t percent) noexcept;
    [[deprecated]] result<void> set_pwm_unsafe(uint32_t value) noexcept;
    result<void> set_manual(bool manual) noexcept;
    result<void> restore_auto() noexcept;   // ⚠️ SAFE: never slows fan
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

## 🎯 What It Brings You

`knst_sens` gives you serious advantages in these situations:

- ✅ **Safe fan control** — never below `min_safe_pwm`, fan can't be stopped
- ✅ **Safe restore** — `restore_auto()` never slows the fan
- ✅ **Crash-safe** — signal handler restores fans to auto
- ✅ **Watchdog** — fans restored if heartbeat stops
- ✅ **Sensor filtering** — median + outlier rejection
- ✅ **Async monitor** — thread-safe, periodic, keeps history
- ✅ **Fan curve** — hysteresis + ramp limiting
- ✅ **Zone orchestration** — drive multiple fans with one temperature
- ✅ **Emergency responder** — 255 at critical temperature
- ✅ **Fail-safe** — sensor lost, NaN, empty curve → 255
- ✅ **Discovery** — chip + fan enumeration
- ✅ **Component classification** — CPU/GPU/NVMe/...
- ✅ **JSON export** — snapshot to JSON
- ✅ **KernelNucleusT compatibility** — fully integrated with `knst_devices`, `knst_sensor_info`, `knst_dev::status`
- ✅ **Windows-aware** — fan control `NotSupported`, other layers work
- ✅ **Zero dependency** — only POSIX sysfs

---