# knst_storage — Comprehensive Storage Controller

Hey! 👋 The `knst_sto` namespace is a storage framework that unifies disks, partitions, filesystems, LUKS/RAID/LVM/BitLocker layers, and performance benchmarks under a single API. Full control on Linux (udisksctl + smartctl + sysfs + fstrim), partial control on Windows (eject + BitLocker WMI), read-only + benchmark on Android.

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

| Operation | Linux | Windows | Android |
|---|---|---|---|
| **Free space query** | ✅ statvfs | ✅ | ✅ |
| **Directory size** | ✅ recurse | ✅ | ✅ |
| **Largest files** | ✅ | ✅ | ✅ |
| **Mount list** | ✅ /proc/mounts | ✅ drive letters | ✅ |
| **Path → mount** | ✅ | ✅ | ✅ |
| **Filesystem usage** | ✅ | ✅ | ✅ |
| **mount/unmount** | ✅ udisksctl | ❌ | ❌ |
| **eject** | ✅ udisksctl | ✅ drive letter | ❌ |
| **SMART** | ✅ ATA + NVMe | ⚠️ WMI health | ⚠️ NVMe temp only |
| **Health score** | ✅ | ⚠️ | ⚠️ |
| **identity** | ✅ full | ⚠️ WMI | ⚠️ sysfs |
| **io_stats** | ✅ /sys/block | ❌ | ✅ |
| **benchmark write** | ✅ fsync | ✅ | ✅ |
| **benchmark read** | ✅ O_DIRECT | ✅ buffered | ✅ |
| **LUKS** | ✅ cryptsetup | ❌ | ❌ |
| **RAID** | ✅ /proc/mdstat | ❌ | ❌ |
| **LVM** | ✅ lvs + sysfs | ❌ | ❌ |
| **BitLocker** | ❌ | ✅ PowerShell | ❌ |
| **TRIM** | ✅ fstrim | ❌ | ❌ |
| **Hotplug** | ✅ uevent | ❌ | ❌ |
| **directory_watcher** | ✅ polling | ✅ polling | ✅ polling |
| **JSON report** | ✅ | ✅ | ✅ |

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Cross-platform free space** | `free_space()` on every platform |
| **Directory size** | Recursive walk, max depth 64 |
| **Largest files** | Insertion sort, top-N |
| **SMART** | ATA + NVMe, zero library dependencies |
| **Health score** | 0-100 + warning list |
| **identity** | Partition table, UUID, TRIM, WWID |
| **Benchmark** | O_DIRECT read, fsync write |
| **LUKS** | /dev/mapper + cryptsetup + crypttab |
| **RAID** | /proc/mdstat parser |
| **LVM** | lvs command + sysfs fallback |
| **BitLocker** | PowerShell Get-BitLockerVolume |
| **TRIM** | `fstrim -v` wrapper |
| **Hotplug** | block device events via `uevent` |
| **directory_watcher** | Polling, cross-platform |
| **JSON report** | Export via `KNST_JSON_HPP` |

---

## 🚀 Quick Start

### Query free space

```cpp
auto r = knst_sto::free_space(u"/home");
if (r) {
    std::cout << "Free: " << r.value.free_bytes / (1024*1024) << " MB\n";
    std::cout << "Used: " << r.value.used_percent << "%\n";
}
```

### Mount list

```cpp
auto m = knst_sto::list_mounts();
for (auto& e : m.value) {
    std::cout << e.device << " → " << e.mount_point
              << " (" << e.fs_type << ")\n";
}
```

### SMART report

```cpp
auto disks = knst_devices::storage();
for (auto& disk : disks) {
    auto s = knst_sto::storage::smart(disk);
    if (s) {
        std::cout << disk.model
                  << "  Health: " << (s.value.passed ? "OK" : "FAIL")
                  << "  Temp: " << s.value.temperature_c << "°C\n";
    }
}
```

### Benchmark

```cpp
auto w = knst_sto::storage::benchmark_write(u"/tmp", 100);   // 100 MB
if (w) {
    std::cout << "Write: " << w.value.mb_per_sec << " MB/s\n";
}
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Cross-platform** | `free_space`, `directory_size`, `largest_files`, `list_mounts`, `find_mount_for_path`, `filesystem_usage` |
| **Linux storage** | `storage::mount`, `unmount`, `eject`, `smart`, `health_check`, `identity`, `io_statistics`, `benchmark_write`, `benchmark_read`, `list_luks`, `list_raid`, `list_lvm`, `trim`, `wait_for_change` |
| **Windows storage** | `storage::eject_volume`, `health_check`, `identity`, `benchmark_write`, `benchmark_read`, `list_bitlocker` |
| **Android storage** | `storage::smart` (partial), `identity` (partial), `io_statistics`, `benchmark_write`, `benchmark_read` |
| **Watcher** | `directory_watcher::open`, `wait`, `stop` |
| **JSON** | `export_report_json`, `save_report_json` |

---

## 📦 1) Common Types

All inside the `knst_sto` namespace:

### `space_info`

```cpp
struct space_info {
    uint64_t free_bytes   = 0;
    uint64_t total_bytes  = 0;
    uint64_t used_bytes   = 0;
    double   used_percent = 0.0;
};
```

### `smart_report`

```cpp
struct smart_report {
    bool available = false;
    bool passed = false;              // SMART PASSED?
    uint32_t power_on_hours = 0;      // Operating hours
    uint32_t reallocated = 0;         // Reallocated sector count
    uint32_t pending = 0;             // Pending sector count
    uint32_t percent_used = 0;        // SSD wear (0-100)
    int32_t  temperature_c = 0;       // Temperature
    uint64_t bytes_written = 0;
    uint64_t bytes_read = 0;
    knst_c16string model;
    knst_c16string firmware;
};
```

### `disk_identity`

```cpp
struct disk_identity {
    knst_c16string serial, wwid, model, vendor, firmware;
    knst_c16string partition_table;   // "MBR" / "GPT" / "RAW"
    knst_c16string fs_type;           // "ext4" / "NTFS" / ...
    knst_c16string fs_uuid;
    knst_c16string fs_label;
    knst_c16string iface;             // "usb" / "nvme" / ...
    uint32_t rotational_rpm = 0;      // 0 = SSD
    uint64_t size_bytes = 0;
    uint64_t sector_size = 512;
    uint64_t physical_sector_size = 4096;
    bool is_ssd = false;
    bool is_nvme = false;
    bool is_encrypted = false;
    bool is_boot_disk = false;
    bool has_trim_support = false;
    bool valid = false;
};
```

### `health_snapshot`

```cpp
struct health_snapshot {
    bool healthy = false;              // score >= 70
    bool temperature_warning = false;  // >55°C
    bool pending_sector_warning = false;
    bool reallocated_warning = false;
    bool wear_warning = false;         // >80%
    double score = 100.0;              // 0-100
    knst_vector<knst_c16string> warnings;
};
```

### `mount_entry`

```cpp
struct mount_entry {
    knst_c16string device;
    knst_c16string mount_point;
    knst_c16string fs_type;
    knst_c16string options;
    bool is_read_only = false;
    bool is_network = false;   // NFS, CIFS, // UNC
    bool is_bind = false;
    bool is_virtual = false;   // tmpfs, proc, sysfs ...
    bool valid = false;
};
```

### `fs_usage`

```cpp
struct fs_usage {
    knst_c16string device, mount_point, fs_type;
    uint64_t total_bytes, used_bytes, free_bytes;
    double used_percent;
};
```

### `dir_size`

```cpp
struct dir_size {
    uint64_t total_bytes = 0;
    uint64_t file_count = 0;
    uint64_t dir_count = 0;
    uint32_t max_depth = 0;
    bool valid = false;
};
```

### `file_entry`

```cpp
struct file_entry {
    knst_c16string path;
    uint64_t size = 0;
    uint64_t modified_time = 0;
};
```

### `io_stats`

```cpp
struct io_stats {
    uint64_t reads_completed = 0;
    uint64_t reads_merged = 0;
    uint64_t sectors_read = 0;
    uint64_t read_ticks_ms = 0;
    uint64_t writes_completed = 0;
    uint64_t writes_merged = 0;
    uint64_t sectors_written = 0;
    uint64_t write_ticks_ms = 0;
    uint64_t io_in_progress = 0;
    uint64_t io_ticks_ms = 0;
    uint64_t weighted_io_ms = 0;
    bool valid = false;
};
```

### `speed_result`

```cpp
struct speed_result {
    double mb_per_sec = 0.0;
    double iops = 0.0;
    double latency_ms = 0.0;
    uint64_t bytes = 0;
    uint32_t duration_ms = 0;
    bool cache_bypassed = false;   // O_DIRECT used?
    bool valid = false;
};
```

### `luks_container`

```cpp
struct luks_container {
    knst_c16string device;       // real underlying like /dev/sda3
    knst_c16string mapper_name;  // cryptswap
    knst_c16string cipher;       // aes-xts-plain64
    knst_c16string uuid;
    uint32_t key_size = 0;
    bool is_unlocked = false;
    bool valid = false;
};
```

### `raid_array`

```cpp
struct raid_array {
    knst_c16string name, level, state;
    uint32_t total_devices = 0;
    uint32_t active_devices = 0;
    uint32_t failed_devices = 0;
    knst_vector<knst_c16string> members;
    bool valid = false;
};
```

### `lvm_volume`

```cpp
struct lvm_volume {
    knst_c16string name, vg_name, type;
    uint64_t size_bytes = 0;
    bool is_active = false;
    bool valid = false;
};
```

### `bitlocker_status`

```cpp
struct bitlocker_status {
    knst_c16string drive;       // "C:"
    knst_c16string method;      // "XtsAes128"
    bool is_encrypted = false;
    bool is_protected = false;
    bool is_locked = false;
    uint32_t percentage = 0;
    bool valid = false;
};
```

---

## 🔧 2) Helpers

### `current_pid()`, `now_ms()`

```cpp
uint32_t pid = knst_sto::current_pid();
uint64_t  t   = knst_sto::now_ms();
```

Platform-independent PID and epoch ms.

### `build_health(smart_report, health_snapshot&)`

```cpp
knst_sto::smart_report s = /* ... */;
knst_sto::health_snapshot h;
knst_sto::build_health(s, h);
// h.score, h.healthy, h.warnings populated
```

**Score logic (starts at 100):**
- `!available` → `healthy = false` (score not computed)
- `!passed` → **-50** ("SMART self-assessment FAILED")
- `temperature_c > 55` → **-15** ("High temperature")
- `reallocated > 0` → **-20** ("Reallocated sectors detected")
- `pending > 0` → **-30** ("Pending sectors — possible failure")
- `percent_used > 80` → **-10** ("SSD wear >80%")

`healthy = score >= 70.0`

---

## 🌍 3) Cross-Platform Functions

### `free_space(path)`

```cpp
auto r = knst_sto::free_space(u"/home");
if (r) {
    std::cout << r.value.free_bytes << " / " << r.value.total_bytes << " bytes\n";
    std::cout << r.value.used_percent << "% used\n";
}
```

Internally calls `knst_file::get_disk_space()`. Works on every platform.

### `directory_size(path)`

```cpp
auto r = knst_sto::directory_size(u"/home/user/project");
if (r) {
    std::cout << "Total : " << r.value.total_bytes << " bytes\n";
    std::cout << "Files : " << r.value.file_count << "\n";
    std::cout << "Dirs  : " << r.value.dir_count << "\n";
    std::cout << "Depth : " << r.value.max_depth << "\n";
}
```

**Recursive walk:**
- Max depth **64** (infinite loop protection)
- Files/dirs starting with `.` are **skipped** (hidden)
- Symlinks **not followed** (uses lstat)
- Linux: `opendir` + `readdir`
- Windows: `FindFirstFileW` + `FindNextFileW`

### `largest_files(path, count = 20)`

```cpp
auto r = knst_sto::largest_files(u"/home/user", 50);
for (auto& f : r.value) {
    std::cout << f.size << "  " << f.path << "\n";
}
```

**How it works?**
1. Collect all files via `walk_collect_files`
2. **Insertion sort** by descending size (stable, fast for small N)
3. Return first `count`

**Limits:**
- `count == 0` → `InvalidArgument`
- `count > 10000` → `InvalidArgument`

---

## 🐧 4) Linux — `knst_sto::storage` (Full Control)

### 4.1) mount / unmount / eject

```cpp
// Mount — returns mount point
auto mp = knst_sto::storage::mount(u"/dev/sdb1");
if (mp) std::cout << "Mount point: " << mp.value << "\n";

// Unmount
knst_sto::storage::unmount(u"/dev/sdb1");

// Eject (remove USB disk)
knst_sto::storage::eject(disk_info);
```

**How it works?**
- `mount` → `udisksctl mount -b <dev> --no-user-interaction`, parses "at /path" from output
- `unmount` → `udisksctl unmount -b <dev>`
- `eject` → unmounts all partitions first, then `udisksctl power-off`

**Safety:**
- Boot disk cannot be ejected → `BadState`
- Non-removable disk cannot be ejected → `InvalidArgument`
- Device path validated with `knst_dev::is_dev_path()`

> ⚠️ **Polkit required.** udisksctl runs as user but prompts via polkit agent for some operations.

> ⚠️ **Boot partition blocks eject.** Code checks this — safety.

### 4.2) SMART

```cpp
auto s = knst_sto::storage::smart(disk);
if (s) {
    std::cout << "Health  : " << (s.value.passed ? "PASSED" : "FAILED") << "\n";
    std::cout << "Temp    : " << s.value.temperature_c << " °C\n";
    std::cout << "Hours   : " << s.value.power_on_hours << " h\n";
    std::cout << "Written : " << s.value.bytes_written / (1024ULL*1024*1024) << " GB\n";
    std::cout << "Wear    : " << s.value.percent_used << "%\n";
}
```

**What it reads?**

**General:**
- `SMART overall-health self-assessment test result: PASSED` → `passed`
- `Device Model` / `Model Number` → `model`
- `Firmware Version` → `firmware`
- `Power On Hours` → `power_on_hours` (commas stripped)

**NVMe (`-d nvme`):**
- `Percentage Used` → `percent_used`
- `Temperature` → `temperature_c`
- `Data Units Written` → `bytes_written` (× 512,000 — spec says 1 unit = 512,000 bytes)
- `Data Units Read` → `bytes_read`

**ATA (attribute table):**
- ID 5 → `reallocated`
- ID 9 → `power_on_hours`
- ID 194 → `temperature_c` (raw & 0xFF)
- ID 197 → `pending`
- ID 241 → `bytes_written` (× 512)

**NVMe temperature fallback:**
- If `smartctl` doesn't report temp → `/sys/block/<dev>/device/hwmon/hwmonN/temp1_input` (millidegree)

**Exit code behavior:**
- `0` → OK
- `1` (bit 0) → slightly used but working → still returns report
- `2` (bit 1) → **`PermissionDenied`** (root required)
- `3` (bit 0+1) → OK + PermissionDenied

> ⚠️ **`smartctl` requires root.** Regular user → `PermissionDenied`.

> ⚠️ **`smartctl` missing → `ToolMissing`.** `apt install smartmontools`.

### `apply_smart(disk_info&)` — shortcut

```cpp
knst_storage_info disk = /* ... */;
knst_sto::storage::apply_smart(disk);
// disk.has_smart, disk.smart_passed, disk.temperature_c, ... populated
```

Calls `smart()` and writes result into `knst_storage_info` fields. If `smartctl` missing → `NotSupported`.

### 4.3) health_check

```cpp
auto h = knst_sto::storage::health_check(disk);
if (h) {
    std::cout << "Score: " << h.value.score << "/100\n";
    std::cout << "Healthy: " << h.value.healthy << "\n";
    for (auto& w : h.value.warnings) {
        std::cout << "  ⚠ " << w << "\n";
    }
}
```

`smart()` + `build_health()` combination. Details above.

### 4.4) identity

```cpp
auto id = knst_sto::storage::identity(disk);
if (id) {
    auto& i = id.value;
    std::cout << "Model  : " << i.model << "\n";
    std::cout << "Serial : " << i.serial << "\n";
    std::cout << "Part.  : " << i.partition_table << "\n";  // GPT/MBR
    std::cout << "FS     : " << i.fs_type << "  UUID: " << i.fs_uuid << "\n";
    std::cout << "SSD    : " << i.is_ssd << "  NVMe: " << i.is_nvme << "\n";
    std::cout << "TRIM   : " << i.has_trim_support << "\n";
    std::cout << "Sector : " << i.sector_size << "/" << i.physical_sector_size << "\n";
}
```

**What it reads?**

**sysfs:**
- `/sys/block/<dev>/queue/rotational` → `rotational_rpm` (0 = SSD, 1 = assume 7200)
- `/sys/block/<dev>/queue/logical_block_size` → `sector_size`
- `/sys/block/<dev>/queue/physical_block_size` → `physical_sector_size`
- `/sys/block/<dev>/queue/discard_max_bytes` → `has_trim_support`
- `/sys/block/<dev>/device/wwid` → `wwid`

**Partition table (fallback chain):**
1. `blkid -p -o value -s PTTYPE <dev>` → authoritative, but needs root/disk grp
2. Else → `lsblk -dno PTTYPE <dev>` → udev cache (world-readable)

**fs_type / fs_uuid (fallback chain):**
1. `blkid -o value -s TYPE <first_partition>`
2. `blkid -o value -s UUID <first_partition>`
3. Else → `lsblk -Pno NAME,FSTYPE,UUID,LABEL <dev>` → all partitions
4. Else → try 16 partitions in sequence with `blkid` (NVMe gets `p` prefix)

> ⚠️ **NVMe partition names** are `/dev/nvme0n1p1` (with p), SATA is `/dev/sda1` (no p). Code detects this automatically.

> ⚠️ **`is_encrypted`** only checks `/dev/mapper/` — i.e. shows **unlocked** LUKS/LVM. For locked LUKS use `list_luks()`.

### 4.5) io_statistics

```cpp
auto s = knst_sto::storage::io_statistics(disk);
if (s) {
    std::cout << "Read  : " << s.value.sectors_read * 512 << " bytes\n";
    std::cout << "Write : " << s.value.sectors_written * 512 << " bytes\n";
    std::cout << "IO time: " << s.value.io_ticks_ms << " ms\n";
}
```

Parses `/sys/block/<dev>/stat`. 17 columns, first 11 populated.

**Columns:**
1. reads completed
2. reads merged
3. sectors read
4. read ticks (ms)
5. writes completed
6. writes merged
7. sectors written
8. write ticks (ms)
9. io in progress
10. io ticks (ms)
11. weighted io (ms)

> ⚠️ **`sectors_read` × 512 = bytes read.** Kernel always reports 512-byte sectors — by convention.

### 4.6) benchmark_write

```cpp
auto r = knst_sto::storage::benchmark_write(u"/tmp", 100);   // 100 MB
if (r) {
    std::cout << "Write : " << r.value.mb_per_sec << " MB/s\n";
    std::cout << "IOPS  : " << r.value.iops << "\n";
    std::cout << "Time  : " << r.value.duration_ms << " ms\n";
}
```

**What it does?**
1. Opens temp file `<dir>/.knst_bench_<pid>_<ms>.tmp`
2. Writes `size_mb` bytes in 1 MB chunks
3. `fsync()` to flush to disk (cache bypass)
4. Deletes the file

**Limits:** `size_mb` 1..8192 (8 GB).

> ⚠️ **`fsync()` is mandatory.** Without cache bypass, SSDs show 10 GB/s — misleading.

> ⚠️ **Temp file deleted.** Even on failure, `unlink` is called.

> ⚠️ **Misleading on compressed FS.** ZFS/btrfs compress the constant `0xAB` pattern → shows higher than actual.

### 4.7) benchmark_read

```cpp
auto r = knst_sto::storage::benchmark_read(u"/path/to/file", 100, /*try_direct=*/true);
if (r) {
    std::cout << "Read : " << r.value.mb_per_sec << " MB/s\n";
    std::cout << "Cache bypass: " << r.value.cache_bypassed << "\n";
}
```

**What it does?**
1. Tries to open with `O_DIRECT` → cache bypass
2. Else falls back to `O_RDONLY` + `posix_fadvise(POSIX_FADV_DONTNEED)` → cache clear
3. Reads `size_mb` bytes in 1 MB chunks
4. Buffer is `posix_memalign(4096)` — O_DIRECT alignment requirement

**Limits:** `size_mb` 1..8192, file must exist.

> ⚠️ **`O_DIRECT` not supported on all filesystems.** Usually fine on ext4/xfs; falls back on ZFS/btrfs → `cache_bypassed = false`.

> ⚠️ **`size_mb` > file size** → stops at EOF, `bytes` reflects actual read.

### 4.8) list_luks

```cpp
auto r = knst_sto::storage::list_luks();
for (auto& c : r.value) {
    std::cout << c.mapper_name << " → " << c.device
              << "  unlocked: " << c.is_unlocked << "\n";
    if (!c.cipher.empty())
        std::cout << "  cipher: " << c.cipher << "  keysize: " << c.key_size << "\n";
}
```

**How it works? (3 sources)**

**1) /dev/mapper scan:**
- Follow symlink per entry (`readlink`)
- Check `/sys/block/dm-N/dm/uuid`: `CRYPT-` prefix → LUKS, `LVM-` → LVM, `mpath-` → multipath
- **Only `CRYPT-` prefix passes** — LVM/mpath filtered out
- First entry of `/sys/block/dm-N/slaves/` → real underlying device (`/dev/sda3`)

**2) `cryptsetup status <mapper>`** (root required):
- `cipher:` → cipher
- `keysize:` → key_size
- `device:` → device
- `UUID:` → uuid

**3) /etc/crypttab fallback** (for locked LUKS):
- `name source keyfile options` format
- Already-added mappers skipped
- `is_unlocked = false`

> ⚠️ **`cryptsetup` requires root.** Without root, cipher/keysize stay empty but `is_unlocked = true` (since `/dev/mapper/<name>` only exists for active dm-crypt).

> ⚠️ **LUKS vs LVM confusion.** Both live under `/dev/mapper/`. The `dm/uuid` prefix check is critical — non-`CRYPT-` is skipped.

### 4.9) list_raid

```cpp
auto r = knst_sto::storage::list_raid();
for (auto& a : r.value) {
    std::cout << a.name << " (" << a.level << ")  "
              << a.state << "  " << a.active_devices << "/" << a.total_devices << "\n";
    for (auto& m : a.members) {
        std::cout << "  [" << m << "]\n";
    }
}
```

`/proc/mdstat` parser:
```
md0 : active raid1 sda1[0] sdb1[1]
      976630464 blocks super 1.2 [2/2] [UU]
```

- Sees `active` line → starts new array
- `[0]`, `[1]` → member list
- `[UU]` → all OK

> ⚠️ **Only `active` arrays.** Inactive/broken arrays not listed.

> ⚠️ **`active_devices` = `total_devices`.** Code doesn't parse `[UU]` pattern — only counts `[N]`. Failed member still shows `active = total`. Known limitation.

### 4.10) list_lvm

```cpp
auto r = knst_sto::storage::list_lvm();
for (auto& v : r.value) {
    std::cout << v.vg_name << "/" << v.name
              << "  " << v.size_bytes / (1024*1024) << " MB"
              << "  active: " << v.is_active << "\n";
}
```

**How it works? (2 sources)**

**1) `lvs` command:**
- `lvs --noheadings --separator "|" -o lv_name,vg_name,lv_size,lv_attr`
- `|` separator (not space — no alignment issue)
- Size "1.5G" → 1.5 × 1024³ bytes
- `lv_attr[4] == 'a'` → active

**2) sysfs fallback** (if lvs absent):
- `/sys/block/dm-N/dm/uuid` → `LVM-` prefix
- `/sys/block/dm-N/dm/name` → LV name
- `/sys/block/dm-N/size` × 512 → size
- **`vg_name` stays empty** — not in sysfs

> ⚠️ **`lvs` requires root** (metadata cache access). Without root, fallback runs but `vg_name` is empty.

### 4.11) trim

```cpp
knst_sto::storage::trim(u"/");
// or
knst_sto::storage::trim(u"/home");
```

Calls `fstrim -v <resolved_path>`.

**When to use?**
- Periodically (weekly) on SSD → prevents performance degradation
- Usually `systemd-fstrim.timer` is preferred over manual

> ⚠️ **Requires root.** `fstrim` won't run as regular user.

> ⚠️ **Slow.** 600-second timeout — large FS may take a while.

> ⚠️ **Path validation.** `dir_exists` + length ≤ 400 check.

### 4.12) wait_for_change

```cpp
auto ev = knst_sto::storage::wait_for_change(60000);   // 60 sec
if (ev) {
    std::cout << ev.value.action << "  " << ev.value.devpath << "\n";
}
```

Waits for `uevent` on block subsystem. `add` when USB plugged, `remove` when unplugged.

> ⚠️ **Returns `Timeout` after `timeout_ms`.** Call again per event.

---

## 🤖 5) Android — `knst_sto::storage` (Read-Only)

Android's storage stack is very restricted. Code explicitly separates what works from what doesn't.

### ✅ Works

| Method | What |
|---|---|
| `smart()` | **Partial** — only NVMe temp (sysfs hwmon) |
| `identity()` | **Partial** — sysfs `queue/*`, wwid |
| `io_statistics()` | **Full** — `/sys/block/<dev>/stat` |
| `benchmark_write/read()` | **Full** — in app-writable dirs |
| `free_space`, `directory_size`, `largest_files` | **Full** — POSIX APIs |
| `list_mounts`, `filesystem_usage` | **Full** — `/proc/mounts` |

### ❌ Doesn't work

| Method | Why |
|---|---|
| `mount` / `unmount` / `eject` | No udisksctl |
| `trim` | No fstrim |
| `list_luks` | No cryptsetup, `/dev/mapper` restricted |
| `list_raid` | `/proc/mdstat` restricted |
| `list_lvm` | No LVM on Android |
| `wait_for_change` | uevent blocked |
| `health_check` | No `smartctl` |

**In practice:** use `find_*` + `io_statistics` + `benchmark_*`, don't rely on the rest.

### Example

```cpp
auto disks = knst_devices::storage();
for (auto& d : disks) {
    // IO stats work
    auto s = knst_sto::storage::io_statistics(d);
    if (s) {
        std::cout << d.device << "  " << s.value.sectors_read * 512 << " B read\n";
    }

    // Benchmark works in app-private dir
    auto b = knst_sto::storage::benchmark_write(u"/data/local/tmp", 10);
    if (b) std::cout << "Write: " << b.value.mb_per_sec << " MB/s\n";
}
```

---

## 🪟 6) Windows — `knst_sto::storage` (Partial)

### 6.1) eject_volume

```cpp
auto r = knst_sto::storage::eject_volume(u"E:");
if (!r) {
    // status::InvalidArgument → wrong drive letter or not removable
    // status::PermissionDenied → not admin
    // status::Busy → device busy
}
```

**How it works?**
1. Drive type check → must be `DRIVE_REMOVABLE` or `DRIVE_CDROM`
2. Open `\\.\X:` (GENERIC_READ | GENERIC_WRITE)
3. `FSCTL_LOCK_VOLUME` → lock volume
4. `FSCTL_DISMOUNT_VOLUME` → detach filesystem
5. `IOCTL_STORAGE_MEDIA_REMOVAL` → mark media removable
6. `IOCTL_STORAGE_EJECT_MEDIA` → actually eject

> ⚠️ **Must hold the lock.** 3-4-5-6 in sequence, otherwise eject fails.

> ⚠️ **CD-ROM supported too** — `DRIVE_CDROM` accepted.

### 6.2) health_check

```cpp
auto h = knst_sto::storage::health_check(disk);
if (h) {
    std::cout << "Healthy: " << h.value.healthy << "\n";
}
```

WMI query `root\Microsoft\Windows\Storage` → `MSFT_PhysicalDisk`. Returns only basic health — no temperature/wear.

> ⚠️ **Limited info.** Windows WMI doesn't expose SMART detail. For detail, use Windows build of `smartmontools`.

### 6.3) identity

```cpp
auto id = knst_sto::storage::identity(disk);
if (id) {
    std::cout << id.value.fs_type << "  " << id.value.partition_table << "\n";
}
```

**What it reads?**
- `model`/`serial`/`firmware`/`size` → `knst_devices` cache
- `fs_type`/`fs_uuid`/`fs_label` → `GetVolumeInformationW` (first mounted partition)
- `partition_table` → WMI `MSFT_Partition.PartitionStyle` (0=MBR, 1=GPT, 2=RAW)
- `has_trim_support` → assume true if SSD (Vista+ NTFS always has TRIM active)
- `is_encrypted` → **not filled** (Windows has no dm-crypt equivalent — for BitLocker use `list_bitlocker()`)

### 6.4) benchmark_write / benchmark_read

```cpp
auto w = knst_sto::storage::benchmark_write(u"C:\\temp", 100);
auto r = knst_sto::storage::benchmark_read(u"C:\\path\\file.bin", 100);
```

On Windows, **buffered I/O** — no O_DIRECT. So cache can't be bypassed (`cache_bypassed = false`).

> ⚠️ **`FlushFileBuffers`** used for write (fsync equivalent).

> ⚠️ **No read cache bypass.** Windows has `FILE_FLAG_NO_BUFFERING` but alignment complexity. Code preferred buffered.

### 6.5) list_bitlocker

```cpp
auto r = knst_sto::storage::list_bitlocker();
for (auto& b : r.value) {
    std::cout << b.drive << "  "
              << (b.is_encrypted ? "encrypted" : "open") << "  "
              << b.percentage << "%\n";
}
```

**How it works?** PowerShell:
```powershell
Get-BitLockerVolume | Select-Object MountPoint,VolumeStatus,EncryptionPercentage,ProtectionStatus,EncryptionMethod | ConvertTo-Csv -NoTypeInformation
```

**Exit code behavior:**
- `ToolMissing` → PowerShell not found
- `NotSupported` → Windows Home edition (no BitLocker cmdlets — "not recognized" in stderr)
- `None` → normal, parse below
- Other → `IoError`

> ⚠️ **`NotSupported` on Windows Home.** Home edition lacks BitLocker cmdlets. Code looks for "not recognized" / "BitLocker" / "CommandNotFound" in stderr to catch this case.

> ⚠️ **CSV parsing careful.** Naive `strtok` breaks — quoted fields, trailing `\r`, embedded commas. Code uses a manual quote-aware splitter.

> ⚠️ **`VolumeStatus` containing "Locked"** → `is_locked = true`. "FullyEncrypted" → `is_encrypted = true`.

---

## 🔗 7) Cross-Platform Mounts

### `list_mounts()`

```cpp
auto r = knst_sto::list_mounts();
for (auto& m : r.value) {
    std::cout << m.device << " → " << m.mount_point;
    if (m.is_read_only) std::cout << " [ro]";
    if (m.is_network)   std::cout << " [net]";
    if (m.is_virtual)   std::cout << " [virt]";
    std::cout << "\n";
}
```

**Linux:** Parses `/proc/mounts`.
**Windows:** `GetLogicalDrives` + `GetVolumeInformationW`.

**Linux fields:**
- `is_read_only` → searches for `"ro"` as a **token** (not substring — so `remount-ro` isn't caught)
- `is_network` → `//` prefix, contains `:/`, `nfs`/`cifs` fs
- `is_virtual` → `tmpfs`, `proc`, `sysfs`, `cgroup`, `devtmpfs`, `debugfs`, `tracefs`, `configfs`, `fusectl`, `autofs`, `mqueue`, `hugetlbfs`, `pstore`, `bpf`, `securityfs`

**Windows fields:**
- `is_read_only` → `FILE_READ_ONLY_VOLUME`
- `is_network` → `DRIVE_REMOTE`
- `is_virtual` → always `false` (no such concept on Windows)

> ⚠️ **`is_read_only` token check.** `remount-ro` contains `ro` but it's not a token. Code uses `,` splitter for this reason.

### `find_mount_for_path(path)`

```cpp
auto m = knst_sto::find_mount_for_path(u"/home/user/file.txt");
if (m) std::cout << "This path is on " << m.value.mount_point << "\n";
```

**Longest prefix match:** Walks all mounts, returns the one with the longest matching prefix.

**Case-insensitive Windows:** `\\` and `/` normalized, chars lowercased before compare. **Case-sensitive on Linux.**

### `filesystem_usage()`

```cpp
auto u = knst_sto::filesystem_usage();
for (auto& f : u.value) {
    std::cout << f.mount_point << "  "
              << f.used_percent << "% (" << f.free_bytes / (1024*1024*1024) << " GB free)\n";
}
```

Calls `free_space()` per mount. Skips those without free-space data.

---

## 📡 8) `directory_watcher` (Cross-Platform Polling)

```cpp
auto w = knst_sto::directory_watcher::open(u"/home/user/watch");
if (!w) return;

while (true) {
    auto e = w.value.wait(1000);   // 1 second timeout
    if (e) {
        switch (e.value.kind) {
            case knst_sto::directory_watcher::event_kind::Created:
                std::cout << "Created: " << e.value.path << "\n"; break;
            case knst_sto::directory_watcher::event_kind::Modified:
                std::cout << "Modified: " << e.value.path << "\n"; break;
            case knst_sto::directory_watcher::event_kind::Deleted:
                std::cout << "Deleted: " << e.value.path << "\n"; break;
        }
    }
}
```

**How it works?**
- Recursive scan every **200 ms**
- Keeps a `std::map<path, mtime>` snapshot
- New path → `Created`, changed mtime → `Modified`, missing → `Deleted`
- `pending_` queue — each `wait()` returns one event

**Limits:**
- **Polling** — no inotify/FSEvents (cross-platform requirement)
- **Max depth 32**
- **`.` prefix skipped**
- **`wait()` in a loop** — careful with busy loop

**When to use?**
- Small directories (<10k files)
- Low-change areas
- Cross-platform needed

**When not to use?**
- Large directories (polling expensive)
- Truly realtime needed (use inotify)

> ⚠️ **`wait()` does periodic polling.** Every call **re-scans the entire directory**. Too many events → CPU spikes.

> ⚠️ **Mtime granularity.** Some FS have 1-second mtime granularity — fast changes can be missed.

> ⚠️ **No move events.** A → B move appears as `Deleted + Created`.

> ⚠️ **Call `stop()`.** To exit the loop, `stop()` + `wait` returns `BadState`.

---

## 📄 9) JSON Export (Conditional)

Works if `KNST_JSON_HPP` is included.

### `export_report_json(out)`

```cpp
knst_json j;
if (knst_sto::export_report_json(j)) {
    // j → all disks, partitions, mounts
}
```

**Contains:**
- `disks[]` → per disk `device`, `model`, `serial`, `type`, `total_bytes`, `is_removable`, `is_solid_state`, `is_system_disk`, `partitions[]`
- `mounts[]` → `device`, `mount_point`, `fs_type`, `is_read_only`, `is_network`

### `save_report_json(path, opt)`

```cpp
knst_sto::save_report_json(u"storage_report.json");
```

Writes to file. `opt` for pretty print etc. (see `knst_json` docs).

> ⚠️ **Won't compile without JSON.** These functions require `KNST_JSON_HPP` to be included. `#if defined(KNST_JSON_HPP)` guard is present.

---

## ⚠️ 10) Common Gotchas

### 1) `smart()` requires root

`smartctl` as regular user → `PermissionDenied`. Use sudo or udev rule.

### 2) `smartctl` missing → `ToolMissing`

`apt install smartmontools` (Debian/Ubuntu), `pacman -S smartmontools` (Arch).

### 3) `eject()` refuses boot disk

If boot partition exists → `BadState` — safety.

### 4) `mount()` requires udisksctl

Missing → `ToolMissing`. If polkit agent isn't running, mount is denied.

### 5) `benchmark_write` does fsync

Cache bypass. **Slow** but realistic.

### 6) `benchmark_read` O_DIRECT optional

`try_direct=false` skips cache clearing too → misleading high speed.

### 7) `list_luks` limited without root

If `cryptsetup status` fails, cipher/keysize empty. But `is_unlocked` **is correct** (`/dev/mapper` presence suffices).

### 8) `list_raid` only active arrays

Broken/inactive arrays appear differently in `/proc/mdstat`. Code only sees `active` line.

### 9) `list_lvm` fallback without root

If `lvs` fails, reads sysfs but `vg_name` stays empty.

### 10) `identity()` blkid permission

`blkid -p` needs root/disk grp. Fallback `lsblk` is world-readable.

### 11) Windows `eject_volume` requires admin

`FSCTL_LOCK_VOLUME` as regular user → `PermissionDenied`.

### 12) Windows `list_bitlocker` on Home → `NotSupported`

Home edition lacks BitLocker cmdlets.

### 13) `directory_watcher` polling

Uses CPU. Expensive on large directories. Don't forget to call `stop()`.

### 14) `directory_watcher` recursive

Max depth 32. Deeper trees not fully covered.

### 15) `filesystem_usage` per-mount `statvfs`

Slow with hundreds of mounts. Use `list_mounts()` + filter if needed.

### 16) JSON export conditional

Without `KNST_JSON_HPP`, `export_report_json` **won't compile**.

### 17) `used_percent` can be 0

If `total_bytes == 0`, returns 0 instead of division by zero — code handles this.

### 18) Android `smart()` only temperature

No `smartctl`. NVMe temp read from sysfs hwmon, other fields empty.

### 19) `is_encrypted` may be misleading

Only checks `/dev/mapper/`. **Locked LUKS** or **BitLocker** → false negative.

### 20) `identity().rotational_rpm` heuristic

sysfs returns only 0/1 — code assumes **7200** for 1. Actual RPM unknown.

---

## 📊 11) Performance

| Operation | Cost | Note |
|---|---|---|
| `free_space` | O(1) | statvfs syscall |
| `directory_size` | O(files) | Recursive walk, max depth 64 |
| `largest_files` | O(N log N) | Insertion sort — OK for small N |
| `list_mounts` | O(lines) | /proc/mounts parse |
| `find_mount_for_path` | O(mounts × len) | Longest prefix |
| `storage::smart` | ~1-3 sec | smartctl spawn |
| `storage::identity` | ~500 ms | blkid/lsblk spawn ×N |
| `storage::io_statistics` | O(1) | 1 sysfs read |
| `benchmark_write` | O(size) + fsync | fsync = slowest part |
| `benchmark_read` | O(size) | Fast with O_DIRECT |
| `list_luks` | O(mappers × cryptsetup) | Spawn per mapper |
| `list_raid` | O(lines) | 1 file read |
| `list_lvm` | O(LVs × lvs) | 1 spawn + parse |
| `wait_for_change` | O(1) | netlink recv |
| `directory_watcher::wait` | O(directory) | Full scan per call |

**Tips:**
- `smart()` and `identity()` are **slow** (tool spawn). Cache them.
- `benchmark_*` is **destructive** — use `/tmp`, don't run on live filesystem.
- `directory_watcher` uses **CPU**. Don't open too many.
- `list_mounts` returns a new vector each call — cache if called frequently.

---

## ✅ 12) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Cache smart() result (tool spawn expensive)
static knst_sto::smart_report cached;
static uint64_t cached_at = 0;
if (knst_sto::now_ms() - cached_at > 60000) {
    auto s = knst_sto::storage::smart(disk);
    if (s) { cached = s.value; cached_at = knst_sto::now_ms(); }
}

// 2. Run benchmark in /tmp
knst_sto::storage::benchmark_write(u"/tmp", 100);

// 3. Unmount before eject
knst_sto::storage::unmount(partition);
knst_sto::storage::eject(disk);

// 4. Stop() the directory_watcher
w.stop();
w.value.wait(100);   // until BadState returns

// 5. Handle ToolMissing when smartctl absent
auto s = knst_sto::storage::smart(disk);
if (!s && s.st == knst_dev::status::ToolMissing) {
    std::cerr << "install smartmontools\n";
}
```

### ❌ Don't

```cpp
// 1. Don't eject the boot disk
knst_sto::storage::eject(boot_disk);   // ❌ BadState

// 2. Don't expect smart() without root
auto s = knst_sto::storage::smart(disk);   // ❌ PermissionDenied

// 3. Don't run benchmark on live FS
knst_sto::storage::benchmark_write(u"/home", 8192);   // ❌ writes 8 GB, fills disk

// 4. Don't assume O_DIRECT works everywhere
auto r = knst_sto::storage::benchmark_read(u"/zfs/file", 100);   // ⚠️ fallback

// 5. Don't expect full list_luks without root
auto l = knst_sto::storage::list_luks();   // ⚠️ cipher/keysize empty

// 6. Don't expect mount on Android
knst_sto::storage::mount(u"/dev/block/sda1");   // ❌ NotSupported

// 7. Don't expect trim on Windows
knst_sto::storage::trim(u"C:\\");   // ❌ NotSupported

// 8. Don't use directory_watcher on huge dirs
auto w = knst_sto::directory_watcher::open(u"/usr");   // ⚠️ 500ms+ per scan

// 9. Don't leave directory_watcher without stop()
// thread joining while loop spins → CPU waste

// 10. Don't expect JSON export without KNST_JSON_HPP
// won't compile
```

---

## 🎯 13) When to Use?

### ✅ Use

- **Disk health monitoring** — SMART + health score
- **Pre-backup check** — free space + disk health
- **Benchmark tool** — measure new disk speed
- **USB disk management** — plug, mount, eject
- **LUKS/RAID/LVM inventory** — system introspection
- **BitLocker status** — Windows enterprise
- **Directory size analysis** — `du` alternative
- **Largest files** — disk cleanup
- **Directory watcher** — simple polling, cross-platform
- **JSON export** — feed to monitoring system

### ❌ Don't use

- **USB transfers** — this is storage, use `knst_usbc`
- **File I/O** — use `knst_file`
- **Partition create/delete** — not in this library
- **Format** — not in this library
- **Real-time file watch** — separate module for `inotify`/`FSEvents`
- **Windows partition management** — use `diskpart`/WMI

---

## 📌 14) API Contract Summary

```cpp
namespace knst_sto {

// Types
struct space_info, smart_report, disk_identity, health_snapshot;
struct mount_entry, fs_usage, dir_size, file_entry;
struct io_stats, speed_result, luks_container, raid_array;
struct lvm_volume, bitlocker_status;

// Helpers
inline uint32_t current_pid() noexcept;
inline uint64_t now_ms() noexcept;
inline void build_health(const smart_report&, health_snapshot&) noexcept;

// Cross-platform
inline result<space_info> free_space(const knst_c16string& path) noexcept;
inline result<dir_size> directory_size(const knst_c16string& path) noexcept;
inline result<knst_vector<file_entry>> largest_files(
    const knst_c16string& path, uint32_t count = 20) noexcept;

inline result<knst_vector<mount_entry>> list_mounts() noexcept;
inline result<mount_entry> find_mount_for_path(const knst_c16string& path) noexcept;
inline result<knst_vector<fs_usage>> filesystem_usage() noexcept;

// Linux (not Android) / Windows / Android
class storage {
public:
    // mount / eject / smart / health / identity / io_stats / benchmark
    // list_luks / list_raid / list_lvm / trim / wait_for_change
    // Some are NotSupported depending on platform
};

// Watcher
class directory_watcher {
public:
    enum class event_kind : uint8_t { Created, Modified, Deleted };
    struct file_event { event_kind kind; knst_c16string path; uint64_t timestamp_ms; };

    static result<directory_watcher> open(const knst_c16string& path) noexcept;
    result<file_event> wait(uint32_t timeout_ms) noexcept;
    void stop() noexcept;
};

// JSON (KNST_JSON_HPP conditional)
#if defined(KNST_JSON_HPP)
inline result<void> export_report_json(knst_json& out) noexcept;
inline result<void> save_report_json(const knst_c16string& path,
    const knst_json_dump_options& opt = {}) noexcept;
#endif

} // namespace knst_sto
```

---

## 🎯 What It Brings You

`knst_sto` gives you serious advantages in these situations:

- ✅ **Cross-platform storage** — Linux + Windows + Android single API
- ✅ **SMART report** — ATA + NVMe, parsed from scratch
- ✅ **Health score** — 0-100 + warning list
- ✅ **Disk identity** — partition table, fs UUID, TRIM, WWID
- ✅ **Benchmark** — O_DIRECT read, fsync write
- ✅ **LUKS inventory** — mapper + crypttab combined
- ✅ **RAID status** — mdstat parser
- ✅ **LVM list** — lvs + sysfs fallback
- ✅ **BitLocker** — PowerShell Get-BitLockerVolume
- ✅ **Directory analysis** — `du` alternative, largest files
- ✅ **Mount list** — /proc/mounts and GetLogicalDrives combined
- ✅ **Directory watcher** — simple cross-platform polling
- ✅ **JSON export** — feed to monitoring
- ✅ **KernelNucleusT compatibility** — fully integrated with `knst_devices`, `knst_file`, `knst_dev::uevent`
- ✅ **Android-aware** — read-only nature explicit, won't break compilation
- ✅ **Windows-aware** — exposes only what works, rest is `NotSupported`

---