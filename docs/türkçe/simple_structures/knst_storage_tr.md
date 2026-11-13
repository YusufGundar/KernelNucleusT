# knst_storage — Kapsamlı Depolama Kontrolcüsü

Selamlar! 👋 `knst_sto` namespace'i, diskleri, bölümleri, dosya sistemlerini, LUKS/RAID/LVM/BitLocker katmanlarını ve performans ölçümlerini tek API'de toplayan bir depolama framework'ü. Linux'ta tam kontrol (udisksctl + smartctl + sysfs + fstrim), Windows'ta kısmi kontrol (eject + BitLocker WMI), Android'de read-only + benchmark.

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

| İşlem | Linux | Windows | Android |
|---|---|---|---|
| **Boş alan sorgu** | ✅ statvfs | ✅ | ✅ |
| **Dizin boyutu** | ✅ recurse | ✅ | ✅ |
| **En büyük dosyalar** | ✅ | ✅ | ✅ |
| **Mount listesi** | ✅ /proc/mounts | ✅ drive letters | ✅ |
| **Path → mount** | ✅ | ✅ | ✅ |
| **Filesystem usage** | ✅ | ✅ | ✅ |
| **mount/unmount** | ✅ udisksctl | ❌ | ❌ |
| **eject** | ✅ udisksctl | ✅ drive letter | ❌ |
| **SMART** | ✅ ATA + NVMe | ⚠️ WMI health | ⚠️ NVMe temp only |
| **Health score** | ✅ | ⚠️ | ⚠️ |
| **identity** | ✅ tam | ⚠️ WMI | ⚠️ sysfs |
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

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Cross-platform boş alan** | `free_space()` her platformda |
| **Dizin boyutu** | Recursive walk, max derinlik 64 |
| **En büyük dosyalar** | Insertion sort ile top-N |
| **SMART** | ATA + NVMe, sıfır kütüphane bağımlılığı |
| **Health score** | 0-100 + uyarı listesi |
| **identity** | Partition table, UUID, TRIM, WWID |
| **Benchmark** | O_DIRECT read, fsync write |
| **LUKS** | /dev/mapper + cryptsetup + crypttab |
| **RAID** | /proc/mdstat parser |
| **LVM** | lvs command + sysfs fallback |
| **BitLocker** | PowerShell Get-BitLockerVolume |
| **TRIM** | `fstrim -v` wrapper |
| **Hotplug** | `uevent` ile block cihaz event |
| **directory_watcher** | Polling, cross-platform |
| **JSON report** | `KNST_JSON_HPP` ile export |

---

## 🚀 Hızlı Başlangıç

### Boş alan sorgula

```cpp
auto r = knst_sto::free_space(u"/home");
if (r) {
    std::cout << "Boş: " << r.value.free_bytes / (1024*1024) << " MB\n";
    std::cout << "Kullanılan: " << r.value.used_percent << "%\n";
}
```

### Mount listesi

```cpp
auto m = knst_sto::list_mounts();
for (auto& e : m.value) {
    std::cout << e.device << " → " << e.mount_point
              << " (" << e.fs_type << ")\n";
}
```

### SMART rapor

```cpp
auto disks = knst_devices::storage();
for (auto& disk : disks) {
    auto s = knst_sto::storage::smart(disk);
    if (s) {
        std::cout << disk.model
                  << "  Sağlık: " << (s.value.passed ? "OK" : "FAIL")
                  << "  Sıcaklık: " << s.value.temperature_c << "°C\n";
    }
}
```

### Benchmark

```cpp
auto w = knst_sto::storage::benchmark_write(u"/tmp", 100);   // 100 MB
if (w) {
    std::cout << "Yazma: " << w.value.mb_per_sec << " MB/s\n";
}
```

---

## 📋 Genel API

| Kategori | Metotlar |
|---|---|
| **Cross-platform** | `free_space`, `directory_size`, `largest_files`, `list_mounts`, `find_mount_for_path`, `filesystem_usage` |
| **Linux storage** | `storage::mount`, `unmount`, `eject`, `smart`, `health_check`, `identity`, `io_statistics`, `benchmark_write`, `benchmark_read`, `list_luks`, `list_raid`, `list_lvm`, `trim`, `wait_for_change` |
| **Windows storage** | `storage::eject_volume`, `health_check`, `identity`, `benchmark_write`, `benchmark_read`, `list_bitlocker` |
| **Android storage** | `storage::smart` (partial), `identity` (partial), `io_statistics`, `benchmark_write`, `benchmark_read` |
| **Watcher** | `directory_watcher::open`, `wait`, `stop` |
| **JSON** | `export_report_json`, `save_report_json` |

---

## 📦 1) Ortak Tipler

Hepsi `knst_sto` namespace'i içinde:

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
    bool passed = false;              // SMART PASSED mı
    uint32_t power_on_hours = 0;      // Çalışma saati
    uint32_t reallocated = 0;         // Reallocated sector sayısı
    uint32_t pending = 0;             // Pending sector sayısı
    uint32_t percent_used = 0;        // SSD wear (0-100)
    int32_t  temperature_c = 0;       // Sıcaklık
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
    bool cache_bypassed = false;   // O_DIRECT kullanıldı mı
    bool valid = false;
};
```

### `luks_container`

```cpp
struct luks_container {
    knst_c16string device;       // /dev/sda3 gibi gerçek underlying
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

## 🔧 2) Yardımcılar

### `current_pid()`, `now_ms()`

```cpp
uint32_t pid = knst_sto::current_pid();
uint64_t  t   = knst_sto::now_ms();
```

Platformdan bağımsız PID ve epoch ms döner.

### `build_health(smart_report, health_snapshot&)`

```cpp
knst_sto::smart_report s = /* ... */;
knst_sto::health_snapshot h;
knst_sto::build_health(s, h);
// h.score, h.healthy, h.warnings dolu
```

**Skor mantığı (100'den başlar):**
- `!available` → `healthy = false` (skor hesaplanmaz)
- `!passed` → **-50** ("SMART self-assessment FAILED")
- `temperature_c > 55` → **-15** ("High temperature")
- `reallocated > 0` → **-20** ("Reallocated sectors detected")
- `pending > 0` → **-30** ("Pending sectors — possible failure")
- `percent_used > 80` → **-10** ("SSD wear >80%")

`healthy = score >= 70.0`

---

## 🌍 3) Cross-Platform Fonksiyonlar

### `free_space(path)`

```cpp
auto r = knst_sto::free_space(u"/home");
if (r) {
    std::cout << r.value.free_bytes << " / " << r.value.total_bytes << " bytes\n";
    std::cout << r.value.used_percent << "% dolu\n";
}
```

İçeride `knst_file::get_disk_space()` çağırır. Her platformda çalışır.

### `directory_size(path)`

```cpp
auto r = knst_sto::directory_size(u"/home/user/project");
if (r) {
    std::cout << "Toplam : " << r.value.total_bytes << " bytes\n";
    std::cout << "Dosya : " << r.value.file_count << "\n";
    std::cout << "Dizin : " << r.value.dir_count << "\n";
    std::cout << "Derinlik: " << r.value.max_depth << "\n";
}
```

**Recursive walk:**
- Max derinlik **64** (sonsuz döngü koruması)
- `.` ile başlayan dosya/dizin **atlanır** (hidden)
- Symlink **takip edilmez** (lstat kullanır)
- Linux: `opendir` + `readdir`
- Windows: `FindFirstFileW` + `FindNextFileW`

### `largest_files(path, count = 20)`

```cpp
auto r = knst_sto::largest_files(u"/home/user", 50);
for (auto& f : r.value) {
    std::cout << f.size << "  " << f.path << "\n";
}
```

**Nasıl çalışır?**
1. Tüm dosyaları `walk_collect_files` ile topla
2. **Insertion sort** ile azalan sırayla diz (stable, küçük N için hızlı)
3. İlk `count` tanesini döndür

**Limit:**
- `count == 0` → `InvalidArgument`
- `count > 10000` → `InvalidArgument`

---

## 🐧 4) Linux — `knst_sto::storage` (Tam Kontrol)

### 4.1) mount / unmount / eject

```cpp
// Mount et — mount point döner
auto mp = knst_sto::storage::mount(u"/dev/sdb1");
if (mp) std::cout << "Mount point: " << mp.value << "\n";

// Unmount
knst_sto::storage::unmount(u"/dev/sdb1");

// Eject (USB disk çıkar)
knst_sto::storage::eject(disk_info);
```

**Nasıl çalışır?**
- `mount` → `udisksctl mount -b <dev> --no-user-interaction`, çıktıdan "at /path" parse edilir
- `unmount` → `udisksctl unmount -b <dev>`
- `eject` → önce tüm partition'ları unmount eder, sonra `udisksctl power-off`

**Güvenlik:**
- Boot disk eject edilemez → `BadState`
- Removable olmayan disk eject edilemez → `InvalidArgument`
- Device path `knst_dev::is_dev_path()` ile doğrulanır

> ⚠️ **Polkit gerekli.** udisksctl normal kullanıcı olarak çalışır ama bazı işlemler için polkit agent'a soru sorar.

> ⚠️ **Boot partition varsa eject reddedilir.** Kod bunu kontrol eder — güvenlik.

### 4.2) SMART

```cpp
auto s = knst_sto::storage::smart(disk);
if (s) {
    std::cout << "Sağlık   : " << (s.value.passed ? "PASSED" : "FAILED") << "\n";
    std::cout << "Sıcaklık : " << s.value.temperature_c << " °C\n";
    std::cout << "Saat     : " << s.value.power_on_hours << " h\n";
    std::cout << "Yazılan  : " << s.value.bytes_written / (1024ULL*1024*1024) << " GB\n";
    std::cout << "Wear     : " << s.value.percent_used << "%\n";
}
```

**Ne okur?**

**Genel:**
- `SMART overall-health self-assessment test result: PASSED` → `passed`
- `Device Model` / `Model Number` → `model`
- `Firmware Version` → `firmware`
- `Power On Hours` → `power_on_hours` (virgül kaldırılır)

**NVMe (`-d nvme`):**
- `Percentage Used` → `percent_used`
- `Temperature` → `temperature_c`
- `Data Units Written` → `bytes_written` (× 512,000 — spec'e göre 1 unit = 512,000 bytes)
- `Data Units Read` → `bytes_read`

**ATA (attribute tablo):**
- ID 5 → `reallocated`
- ID 9 → `power_on_hours`
- ID 194 → `temperature_c` (raw & 0xFF)
- ID 197 → `pending`
- ID 241 → `bytes_written` (× 512)

**NVMe temperature fallback:**
- `smartctl` sıcaklık vermezse → `/sys/block/<dev>/device/hwmon/hwmonN/temp1_input` (millidegree)

**Exit code davranışı:**
- `0` → OK
- `1` (bit 0) → biraz kullanılmış ama çalışıyor → yine de rapor döner
- `2` (bit 1) → **`PermissionDenied`** (root gerekli)
- `3` (bit 0+1) → OK + PermissionDenied

> ⚠️ **`smartctl` root ister.** Normal kullanıcı → `PermissionDenied`.

> ⚠️ **`smartctl` yoksa → `ToolMissing`.** `apt install smartmontools`.

### `apply_smart(disk_info&)` — kısayol

```cpp
knst_storage_info disk = /* ... */;
knst_sto::storage::apply_smart(disk);
// disk.has_smart, disk.smart_passed, disk.temperature_c, ... dolu
```

`smart()` çağırıp sonucu `knst_storage_info` alanlarına yazar. `smartctl` yoksa `NotSupported`.

### 4.3) health_check

```cpp
auto h = knst_sto::storage::health_check(disk);
if (h) {
    std::cout << "Skor: " << h.value.score << "/100\n";
    std::cout << "Sağlıklı: " << h.value.healthy << "\n";
    for (auto& w : h.value.warnings) {
        std::cout << "  ⚠ " << w << "\n";
    }
}
```

`smart()` + `build_health()` kombinasyonu. Detaylar yukarıda.

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
    std::cout << "Sektör : " << i.sector_size << "/" << i.physical_sector_size << "\n";
}
```

**Ne okur?**

**sysfs:**
- `/sys/block/<dev>/queue/rotational` → `rotational_rpm` (0 = SSD, 1 = 7200 varsayımı)
- `/sys/block/<dev>/queue/logical_block_size` → `sector_size`
- `/sys/block/<dev>/queue/physical_block_size` → `physical_sector_size`
- `/sys/block/<dev>/queue/discard_max_bytes` → `has_trim_support`
- `/sys/block/<dev>/device/wwid` → `wwid`

**Partition table (fallback zinciri):**
1. `blkid -p -o value -s PTTYPE <dev>` → doğru, ama root/disk grp gerekir
2. Yoksa → `lsblk -dno PTTYPE <dev>` → udev cache (world-readable)

**fs_type / fs_uuid (fallback zinciri):**
1. `blkid -o value -s TYPE <first_partition>`
2. `blkid -o value -s UUID <first_partition>`
3. Yoksa → `lsblk -Pno NAME,FSTYPE,UUID,LABEL <dev>` → tüm partition'lar
4. Yoksa → 16 partition'ı sırayla `blkid` ile dene (NVMe'de `p` prefix eklenir)

> ⚠️ **NVMe partition isimleri** `/dev/nvme0n1p1` (p'li), SATA `/dev/sda1` (p'siz). Kod bunu otomatik algılar.

> ⚠️ **`is_encrypted`** sadece `/dev/mapper/` kontrol eder — yani **unlocked** LUKS/LVM gösterir. Locked LUKS için `list_luks()` bak.

### 4.5) io_statistics

```cpp
auto s = knst_sto::storage::io_statistics(disk);
if (s) {
    std::cout << "Okuma : " << s.value.sectors_read * 512 << " bytes\n";
    std::cout << "Yazma : " << s.value.sectors_written * 512 << " bytes\n";
    std::cout << "IO time: " << s.value.io_ticks_ms << " ms\n";
}
```

`/sys/block/<dev>/stat` dosyasını parse eder. 17 sütun, ilk 11'i dolu.

**Sütunlar:**
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

> ⚠️ **`sectors_read` × 512 = byte cinsinden okuma.** Sektör = 512 byte varsayımı — kernel her zaman 512 raporlar.

### 4.6) benchmark_write

```cpp
auto r = knst_sto::storage::benchmark_write(u"/tmp", 100);   // 100 MB
if (r) {
    std::cout << "Yazma : " << r.value.mb_per_sec << " MB/s\n";
    std::cout << "IOPS  : " << r.value.iops << "\n";
    std::cout << "Süre  : " << r.value.duration_ms << " ms\n";
}
```

**Ne yapar?**
1. `<dir>/.knst_bench_<pid>_<ms>.tmp` geçici dosya açar
2. 1 MB chunk'larla `size_mb` kadar yazar
3. `fsync()` ile diske flush eder (cache bypass)
4. Dosyayı siler

**Sınır:** `size_mb` 1..8192 (8 GB).

> ⚠️ **`fsync()` zorunlu.** Cache bypass olmadan SSD'ler 10 GB/s gösterir — yanıltıcı.

> ⚠️ **Geçici dosya silinir.** Başarısız olsa bile `unlink` çağrılır.

> ⚠️ **Compress'li FS'te yanıltıcı.** ZFS/btrfs'te `0xAB` sabit pattern compress edilir → gerçek hızdan yüksek gösterir.

### 4.7) benchmark_read

```cpp
auto r = knst_sto::storage::benchmark_read(u"/path/to/file", 100, /*try_direct=*/true);
if (r) {
    std::cout << "Okuma : " << r.value.mb_per_sec << " MB/s\n";
    std::cout << "Cache bypass: " << r.value.cache_bypassed << "\n";
}
```

**Ne yapar?**
1. `O_DIRECT` ile açmayı dener → cache bypass
2. Başarısızsa normal `O_RDONLY` + `posix_fadvise(POSIX_FADV_DONTNEED)` → cache temizle
3. 1 MB chunk'larla `size_mb` kadar okur
4. Buffer `posix_memalign(4096)` — O_DIRECT alignment şartı

**Limit:** `size_mb` 1..8192, dosya var olmalı.

> ⚠️ **`O_DIRECT` her FS'te çalışmaz.** ext4/xfs'te genelde OK, ama ZFS/btrfs'te fallback'e düşer → `cache_bypassed = false`.

> ⚠️ **`size_mb` > dosya boyutu olursa** okuma EOF'ta durur, `bytes` gerçek okunan kadar döner.

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

**Nasıl çalışır? (3 kaynak)**

**1) /dev/mapper tarama:**
- Her entry için symlink takip (`readlink`)
- `/sys/block/dm-N/dm/uuid` kontrol: `CRYPT-` prefix → LUKS, `LVM-` → LVM, `mpath-` → multipath
- **Sadece `CRYPT-` prefix geçer** — LVM/mpath filtrelenir
- `/sys/block/dm-N/slaves/` ilk entry → gerçek underlying device (`/dev/sda3`)

**2) `cryptsetup status <mapper>`** (root gerekli):
- `cipher:` → cipher
- `keysize:` → key_size
- `device:` → device
- `UUID:` → uuid

**3) /etc/crypttab fallback** (locked LUKS için):
- `name source keyfile options` formatı
- Daha önce eklenmiş mapper'lar atlanır
- `is_unlocked = false`

> ⚠️ **`cryptsetup` root ister.** Root değilse cipher/keysize boş kalır ama `is_unlocked = true` (çünkü `/dev/mapper/<name>` sadece aktif dm-crypt için var).

> ⚠️ **LUKS vs LVM karışıklığı.** `/dev/mapper/` altında her ikisi de var. `dm/uuid` prefix kontrolü bu yüzden kritik — `CRYPT-` değilse atlanır.

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

- `active` satırı görürse yeni array başlatır
- `[0]`, `[1]` → member listesi
- `[UU]` → hepsi OK

> ⚠️ **Sadece `active` array'ler.** Inactive/broken array'ler listelenmez.

> ⚠️ **`active_devices` = `total_devices`.** Kod `[UU]` pattern'ini parse etmiyor — sadece `[N]` sayısına bakıyor. Fail olmuş member olsa bile `active = total` gösterir. Bu bilinen eksiklik.

### 4.10) list_lvm

```cpp
auto r = knst_sto::storage::list_lvm();
for (auto& v : r.value) {
    std::cout << v.vg_name << "/" << v.name
              << "  " << v.size_bytes / (1024*1024) << " MB"
              << "  active: " << v.is_active << "\n";
}
```

**Nasıl çalışır? (2 kaynak)**

**1) `lvs` komutu:**
- `lvs --noheadings --separator "|" -o lv_name,vg_name,lv_size,lv_attr`
- `|` ayırıcı (space değil — alignment sorunu yok)
- Size "1.5G" → 1.5 × 1024³ byte
- `lv_attr[4] == 'a'` → active

**2) sysfs fallback** (lvs yoksa):
- `/sys/block/dm-N/dm/uuid` → `LVM-` prefix
- `/sys/block/dm-N/dm/name` → LV adı
- `/sys/block/dm-N/size` × 512 → boyut
- **`vg_name` boş kalır** — sysfs'te yok

> ⚠️ **`lvs` root ister** (metadata cache erişimi). Root değilse fallback çalışır ama `vg_name` boş.

### 4.11) trim

```cpp
knst_sto::storage::trim(u"/");
// veya
knst_sto::storage::trim(u"/home");
```

`fstrim -v <resolved_path>` çağırır.

**Ne zaman kullan?**
- SSD'de periyodik (haftada 1) → performans düşmesini engeller
- Manuel yerine `systemd-fstrim.timer` kullan genelde

> ⚠️ **Root gerekir.** `fstrim` normal kullanıcı olarak çalışmaz.

> ⚠️ **Yavaş.** 600 saniye timeout var — büyük FS'te uzun sürebilir.

> ⚠️ **Path doğrulama.** `dir_exists` + uzunluk ≤ 400 kontrolü var.

### 4.12) wait_for_change

```cpp
auto ev = knst_sto::storage::wait_for_change(60000);   // 60 sn
if (ev) {
    std::cout << ev.value.action << "  " << ev.value.devpath << "\n";
}
```

Block subsystem üzerinden `uevent` bekler. USB takıldığında `add`, çıktığında `remove`.

> ⚠️ **`timeout_ms` sonrası `Timeout` döner.** Tek event için yeniden çağır.

---

## 🤖 5) Android — `knst_sto::storage` (Read-Only)

Android'de USB/storage stack'i çok kısıtlı. Kod neyin çalıştığını, neyin çalışmadığını net ayırıyor.

### ✅ Çalışır

| Metot | Ne |
|---|---|
| `smart()` | **Kısmi** — sadece NVMe sıcaklık (sysfs hwmon) |
| `identity()` | **Kısmi** — sysfs `queue/*`, wwid |
| `io_statistics()` | **Tam** — `/sys/block/<dev>/stat` |
| `benchmark_write/read()` | **Tam** — app-writable dizinlerde |
| `free_space`, `directory_size`, `largest_files` | **Tam** — POSIX API |
| `list_mounts`, `filesystem_usage` | **Tam** — `/proc/mounts` |

### ❌ Çalışmaz

| Metot | Neden |
|---|---|
| `mount` / `unmount` / `eject` | udisksctl yok |
| `trim` | fstrim yok |
| `list_luks` | cryptsetup yok, `/dev/mapper` kısıtlı |
| `list_raid` | `/proc/mdstat` kısıtlı |
| `list_lvm` | LVM Android'de yok |
| `wait_for_change` | uevent bloklu |
| `health_check` | `smartctl` yok |

**Pratikte:** `find_*` + `io_statistics` + `benchmark_*` kullan, gerisine güvenme.

### Örnek

```cpp
auto disks = knst_devices::storage();
for (auto& d : disks) {
    // IO stats çalışır
    auto s = knst_sto::storage::io_statistics(d);
    if (s) {
        std::cout << d.device << "  " << s.value.sectors_read * 512 << " B okundu\n";
    }

    // Benchmark app-private dizinde çalışır
    auto b = knst_sto::storage::benchmark_write(u"/data/local/tmp", 10);
    if (b) std::cout << "Yazma: " << b.value.mb_per_sec << " MB/s\n";
}
```

---

## 🪟 6) Windows — `knst_sto::storage` (Kısmi)

### 6.1) eject_volume

```cpp
auto r = knst_sto::storage::eject_volume(u"E:");
if (!r) {
    // status::InvalidArgument → drive letter yanlış veya removable değil
    // status::PermissionDenied → admin değilsin
    // status::Busy → cihaz meşgul
}
```

**Nasıl çalışır?**
1. Drive type kontrol → `DRIVE_REMOVABLE` veya `DRIVE_CDROM` olmalı
2. `\\.\X:` open (GENERIC_READ | GENERIC_WRITE)
3. `FSCTL_LOCK_VOLUME` → volume kilitle
4. `FSCTL_DISMOUNT_VOLUME` → dosya sistemi ayır
5. `IOCTL_STORAGE_MEDIA_REMOVAL` → medya çıkarılabilir işaretle
6. `IOCTL_STORAGE_EJECT_MEDIA` → fiilen eject

> ⚠️ **Kilit tutulmalı.** 3-4-5-6 sırayla yapılmalı, aksi halde eject başarısız.

> ⚠️ **CD-ROM de destekli** — `DRIVE_CDROM` kabul.

### 6.2) health_check

```cpp
auto h = knst_sto::storage::health_check(disk);
if (h) {
    std::cout << "Sağlıklı: " << h.value.healthy << "\n";
}
```

WMI `root\Microsoft\Windows\Storage` → `MSFT_PhysicalDisk` sorgusu. Sadece basic health döner, sıcaklık/wear **yok**.

> ⚠️ **Limited bilgi.** Windows WMI SMART detayları sağlamıyor. Detay için `smartmontools` Windows build kullan.

### 6.3) identity

```cpp
auto id = knst_sto::storage::identity(disk);
if (id) {
    std::cout << id.value.fs_type << "  " << id.value.partition_table << "\n";
}
```

**Ne okur?**
- `model`/`serial`/`firmware`/`size` → `knst_devices` cache
- `fs_type`/`fs_uuid`/`fs_label` → `GetVolumeInformationW` (ilk mounted partition)
- `partition_table` → WMI `MSFT_Partition.PartitionStyle` (0=MBR, 1=GPT, 2=RAW)
- `has_trim_support` → SSD ise true varsayım (Vista+ NTFS her zaman TRIM aktif)
- `is_encrypted` → **dolmuyor** (Windows'ta dm-crypt karşılığı yok — BitLocker için `list_bitlocker()`)

### 6.4) benchmark_write / benchmark_read

```cpp
auto w = knst_sto::storage::benchmark_write(u"C:\\temp", 100);
auto r = knst_sto::storage::benchmark_read(u"C:\\path\\file.bin", 100);
```

Windows'ta **buffered I/O** — O_DIRECT yok. Bu yüzden cache bypass edilemez (`cache_bypassed = false`).

> ⚠️ **`FlushFileBuffers`** write için kullanılır (fsync karşılığı).

> ⚠️ **Read cache bypass yok.** Windows'ta `FILE_FLAG_NO_BUFFERING` var ama alignment/alignment karmaşık. Kod buffered tercih etti.

### 6.5) list_bitlocker

```cpp
auto r = knst_sto::storage::list_bitlocker();
for (auto& b : r.value) {
    std::cout << b.drive << "  "
              << (b.is_encrypted ? "şifreli" : "açık") << "  "
              << b.percentage << "%\n";
}
```

**Nasıl çalışır?** PowerShell:
```powershell
Get-BitLockerVolume | Select-Object MountPoint,VolumeStatus,EncryptionPercentage,ProtectionStatus,EncryptionMethod | ConvertTo-Csv -NoTypeInformation
```

**Exit code davranışı:**
- `ToolMissing` → PowerShell bulunamadı
- `NotSupported` → Windows Home edition (BitLocker cmdlets yok — "not recognized" stderr'de)
- `None` → normal, parse aşağıda
- Diğer → `IoError`

> ⚠️ **Windows Home'da `NotSupported`.** Home edition'da BitLocker cmdlet'leri yok. Kod stderr'de "not recognized" / "BitLocker" / "CommandNotFound" ararsa bu durumu yakalar.

> ⚠️ **CSV parse dikkatli.** Naive `strtok` bozar — quoted field'lar, trailing `\r`, embedded comma. Kod elle quote-aware splitter kullanıyor.

> ⚠️ **`VolumeStatus` "Locked" içerirse** `is_locked = true`. "FullyEncrypted" içerirse `is_encrypted = true`.

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

**Linux:** `/proc/mounts` parse edilir.
**Windows:** `GetLogicalDrives` + `GetVolumeInformationW`.

**Linux alanları:**
- `is_read_only` → `"ro"` **token** olarak aranır (substring değil — `remount-ro`'yu yakalamasın)
- `is_network` → `//` prefix, `:/` içerir, `nfs`/`cifs` fs
- `is_virtual` → `tmpfs`, `proc`, `sysfs`, `cgroup`, `devtmpfs`, `debugfs`, `tracefs`, `configfs`, `fusectl`, `autofs`, `mqueue`, `hugetlbfs`, `pstore`, `bpf`, `securityfs`

**Windows alanları:**
- `is_read_only` → `FILE_READ_ONLY_VOLUME`
- `is_network` → `DRIVE_REMOTE`
- `is_virtual` → her zaman `false` (Windows'ta bu kavram yok)

> ⚠️ **`is_read_only` token kontrolü.** `remount-ro` içinde `ro` var ama token değil. Kod bu yüzden `,` splitter kullanıyor.

### `find_mount_for_path(path)`

```cpp
auto m = knst_sto::find_mount_for_path(u"/home/user/file.txt");
if (m) std::cout << "Bu path " << m.value.mount_point << " üzerinde\n";
```

**Longest prefix match:** Tüm mount'ları gezer, en uzun path prefix'i eşleşen döner.

**Case-insensitive Windows:** `\\` ve `/` normalize edilir, char'lar compare'dan önce lower yapılır. **Linux'ta case-sensitive.**

### `filesystem_usage()`

```cpp
auto u = knst_sto::filesystem_usage();
for (auto& f : u.value) {
    std::cout << f.mount_point << "  "
              << f.used_percent << "% (" << f.free_bytes / (1024*1024*1024) << " GB boş)\n";
}
```

Her mount için `free_space()` çağırır. Boş alan alamayanlar atlanır.

---

## 📡 8) `directory_watcher` (Cross-Platform Polling)

```cpp
auto w = knst_sto::directory_watcher::open(u"/home/user/watch");
if (!w) return;

while (true) {
    auto e = w.value.wait(1000);   // 1 saniye timeout
    if (e) {
        switch (e.value.kind) {
            case knst_sto::directory_watcher::event_kind::Created:
                std::cout << "Yeni: " << e.value.path << "\n"; break;
            case knst_sto::directory_watcher::event_kind::Modified:
                std::cout << "Değişti: " << e.value.path << "\n"; break;
            case knst_sto::directory_watcher::event_kind::Deleted:
                std::cout << "Silindi: " << e.value.path << "\n"; break;
        }
    }
}
```

**Nasıl çalışır?**
- **200 ms** aralıkla recursive scan
- `std::map<path, mtime>` snapshot tutar
- Yeni path → `Created`, farklı mtime → `Modified`, yok olan → `Deleted`
- `pending_` kuyruğu — her `wait()` bir event döner

**Limitler:**
- **Polling** — inotify/FSEvents yok (cross-platform şartı)
- **Max derinlik 32**
- **`. ` ile başlayanlar atlanır**
- **`wait()` döngü içinde** — busy loop'a dikkat

**Ne zaman kullan?**
- Küçük dizinler (<10k dosya)
- Az değişim olan yerler
- Cross-platform lazım

**Ne zaman kullanma?**
- Büyük dizin (polling pahalı)
- Gerçekten realtime lazım (inotify kullan)

> ⚠️ **`wait()` periyodik polling.** Her çağrı **tüm dizini** yeniden tarar. Çok event geliyorsa CPU artar.

> ⚠️ **Mtime granularity.** Bazı FS'lerde mtime 1 saniye granularity — hızlı değişim kaçabilir.

> ⚠️ **Move event yok.** A → B taşıma `Deleted + Created` olarak görünür.

> ⚠️ **`stop()` çağır.** Loop'tan çıkmak için `stop()` + `wait`'in `BadState` dönmesi.

---

## 📄 9) JSON Export (Koşullu)

`KNST_JSON_HPP` include edildiyse çalışır.

### `export_report_json(out)`

```cpp
knst_json j;
if (knst_sto::export_report_json(j)) {
    // j → tüm diskler, partition'lar, mount'lar
}
```

**Ne içerir?**
- `disks[]` → her disk için `device`, `model`, `serial`, `type`, `total_bytes`, `is_removable`, `is_solid_state`, `is_system_disk`, `partitions[]`
- `mounts[]` → `device`, `mount_point`, `fs_type`, `is_read_only`, `is_network`

### `save_report_json(path, opt)`

```cpp
knst_sto::save_report_json(u"storage_report.json");
```

Dosyaya yazar. `opt` pretty print vs. ayarlar (bkz. `knst_json` doc'u).

> ⚠️ **JSON yoksa derlenmez.** `KNST_JSON_HPP` include edilmeden bu fonksiyonlar **derleme hatası** verir. `#if defined(KNST_JSON_HPP)` guard'ı var.

---

## ⚠️ 10) Sık Tuzaklar

### 1) `smart()` root ister

`smartctl` normal kullanıcı olarak `PermissionDenied` döner. Sudo veya udev kuralı gerek.

### 2) `smartctl` yoksa `ToolMissing`

`apt install smartmontools` (Debian/Ubuntu), `pacman -S smartmontools` (Arch).

### 3) `eject()` boot disk reddeder

Boot partition varsa `BadState` döner — güvenlik.

### 4) `mount()` udisksctl gerekir

Yoksa `ToolMissing`. `polkit` agent çalışmıyorsa mount reddedilir.

### 5) `benchmark_write` fsync yapar

Cache bypass için. **Yavaş** ama gerçekçi.

### 6) `benchmark_read` O_DIRECT opsiyonel

`try_direct=false` verirsen cache temizleme bile yapılmaz → yanıltıcı yüksek hız.

### 7) `list_luks` root olmadan sınırlı

`cryptsetup status` çalışmazsa cipher/keysize boş kalır. Ama `is_unlocked` **doğru** (`/dev/mapper` varlığı yeterli).

### 8) `list_raid` sadece active array

Broken/inactive array'ler `/proc/mdstat`'ta farklı satırda. Kod sadece `active` satırı görür.

### 9) `list_lvm` root olmadan fallback

`lvs` çalışmazsa sysfs'ten okur ama `vg_name` boş kalır.

### 10) `identity()` blkid permission

`blkid -p` root/disk grp ister. Fallback `lsblk` world-readable.

### 11) Windows `eject_volume` admin ister

`FSCTL_LOCK_VOLUME` normal kullanıcı olarak `PermissionDenied`.

### 12) Windows `list_bitlocker` Home'da `NotSupported`

Home edition'da BitLocker cmdlet yok.

### 13) `directory_watcher` polling

CPU kullanır. Büyük dizinlerde pahalı. `stop()` çağırmayı unutma.

### 14) `directory_watcher` recursive

Max derinlik 32. Daha derin ağaçta yetersiz.

### 15) `filesystem_usage` mount başına `statvfs`

Yüzlerce mount varsa yavaş. Gerekirse `list_mounts()` + filtre.

### 16) JSON export conditional

`KNST_JSON_HPP` yoksa `export_report_json` **derlenmez**.

### 17) `used_percent` 0 olabilir

`total_bytes == 0` ise division by zero yerine 0 döner — kod bunu handle eder.

### 18) Android `smart()` sadece sıcaklık

`smartctl` yok. NVMe temp sysfs hwmon'dan okunur, diğer alanlar boş.

### 19) `is_encrypted` yanıltıcı olabilir

Sadece `/dev/mapper/` kontrol eder. **Locked LUKS** veya **BitLocker** için yanlış negatif.

### 20) `identity().rotational_rpm` heuristik

sysfs sadece 0/1 döner — kod 1 için **7200 varsayımı** yapıyor. Gerçek RPM bilinmiyor.

---

## 📊 11) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| `free_space` | O(1) | statvfs syscall |
| `directory_size` | O(dosya) | Recursive walk, max derinlik 64 |
| `largest_files` | O(N log N) | Insertion sort — küçük N için OK |
| `list_mounts` | O(satır) | /proc/mounts parse |
| `find_mount_for_path` | O(mount × len) | Longest prefix |
| `storage::smart` | ~1-3 sn | smartctl spawn |
| `storage::identity` | ~500 ms | blkid/lsblk spawn ×N |
| `storage::io_statistics` | O(1) | 1 sysfs read |
| `benchmark_write` | O(size) + fsync | fsync = en yavaş kısım |
| `benchmark_read` | O(size) | O_DIRECT varsa hızlı |
| `list_luks` | O(mapper × cryptsetup) | Her mapper için spawn |
| `list_raid` | O(satır) | 1 dosya okuma |
| `list_lvm` | O(lv × lvs) | 1 spawn + parse |
| `wait_for_change` | O(1) | netlink recv |
| `directory_watcher::wait` | O(dizin) | Her çağrı full scan |

**İpuçları:**
- `smart()` ve `identity()` **yavaş** (tool spawn). Cache'le.
- `benchmark_*` **yıkıcı** — `/tmp` kullan, canlı FS'te çalıştırma.
- `directory_watcher` **CPU** kullanır. Çok sayıda watcher açma.
- `list_mounts` her çağrıda yeni vektör — sık çağırıyorsan cache'le.

---

## ✅ 12) Yap / ❌ Yapma

### ✅ Yap

```cpp
// 1. smart() sonucunu cache'le (tool spawn pahalı)
static knst_sto::smart_report cached;
static uint64_t cached_at = 0;
if (knst_sto::now_ms() - cached_at > 60000) {
    auto s = knst_sto::storage::smart(disk);
    if (s) { cached = s.value; cached_at = knst_sto::now_ms(); }
}

// 2. benchmark'ı /tmp'de yap
knst_sto::storage::benchmark_write(u"/tmp", 100);

// 3. eject öncesi unmount
knst_sto::storage::unmount(partition);
knst_sto::storage::eject(disk);

// 4. directory_watcher'ı stop() et
w.stop();
w.value.wait(100);   // BadState dönene kadar

// 5. smartctl yoksa ToolMissing handle et
auto s = knst_sto::storage::smart(disk);
if (!s && s.st == knst_dev::status::ToolMissing) {
    std::cerr << "smartmontools kur\n";
}
```

### ❌ Yapma

```cpp
// 1. Boot disk eject etme
knst_sto::storage::eject(boot_disk);   // ❌ BadState

// 2. Root olmadan smart() bekleme
auto s = knst_sto::storage::smart(disk);   // ❌ PermissionDenied

// 3. benchmark'ı canlı FS'te yapma
knst_sto::storage::benchmark_write(u"/home", 8192);   // ❌ 8 GB yaz, disk dolar

// 4. O_DIRECT her FS'te çalışmaz sanma
auto r = knst_sto::storage::benchmark_read(u"/zfs/file", 100);   // ⚠️ fallback

// 5. list_luks'u root olmadan tam bekleme
auto l = knst_sto::storage::list_luks();   // ⚠️ cipher/keysize boş kalır

// 6. Android'de mount bekleme
knst_sto::storage::mount(u"/dev/block/sda1");   // ❌ NotSupported

// 7. Windows'ta reset bekleme
knst_sto::storage::trim(u"C:\\");   // ❌ NotSupported

// 8. directory_watcher'ı büyük dizinde kullanma
auto w = knst_sto::directory_watcher::open(u"/usr");   // ⚠️ Her scan 500ms+

// 9. directory_watcher'ı stop() etmeden bırakma
// thread join ederken loop döner → CPU israfı

// 10. JSON export'u KNST_JSON_HPP olmadan bekleme
// Derlenmez
```

---

## 🎯 13) Ne Zaman Kullanmalı?

### ✅ Kullan

- **Disk sağlığı izleme** — SMART + health score
- **Backup öncesi kontrol** — boş alan + disk sağlığı
- **Benchmark tool** — yeni disk hızını ölç
- **USB disk yönetimi** — tak, mount, eject
- **LUKS/RAID/LVM envanteri** — sistem introspection
- **BitLocker durumu** — Windows enterprise
- **Dizin boyutu analizi** — `du` alternatifi
- **En büyük dosyalar** — disk temizliği için
- **Directory watcher** — basit polling, cross-platform
- **JSON export** — monitoring sistemine besle

### ❌ Kullanma

- **USB transfer** — bu depolama, `knst_usbc` kullan
- **Dosya I/O** — `knst_file` kullan
- **Partition oluşturma/silme** — bu kütüphane yapmıyor
- **Format** — bu kütüphane yapmıyor
- **Real-time file watch** — `inotify`/`FSEvents` için ayrı modül
- **Windows partition yönetimi** — `diskpart`/WMI kullan

---

## 📌 14) API Kontrat Özeti

```cpp
namespace knst_sto {

// Tipler
struct space_info, smart_report, disk_identity, health_snapshot;
struct mount_entry, fs_usage, dir_size, file_entry;
struct io_stats, speed_result, luks_container, raid_array;
struct lvm_volume, bitlocker_status;

// Yardımcılar
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
    // Platforma göre bazıları NotSupported
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

## 🎯 Size Getireceği Kazanç

`knst_sto` şu durumlarda ciddi avantaj sağlar:

- ✅ **Cross-platform depolama** — Linux + Windows + Android tek API
- ✅ **SMART raporu** — ATA + NVMe, sıfırdan parse
- ✅ **Health score** — 0-100 + uyarı listesi
- ✅ **Disk identity** — partition table, fs UUID, TRIM, WWID
- ✅ **Benchmark** — O_DIRECT read, fsync write
- ✅ **LUKS envanteri** — mapper + crypttab birleşik
- ✅ **RAID durumu** — mdstat parser
- ✅ **LVM listesi** — lvs + sysfs fallback
- ✅ **BitLocker** — PowerShell Get-BitLockerVolume
- ✅ **Dizin analizi** — du alternatifi, en büyük dosyalar
- ✅ **Mount listesi** — /proc/mounts ve GetLogicalDrives birleşik
- ✅ **Directory watcher** — basit cross-platform polling
- ✅ **JSON export** — monitoring'e besleme
- ✅ **KernelNucleusT uyumu** — `knst_devices`, `knst_file`, `knst_dev::uevent` ile tam entegre
- ✅ **Android-aware** — read-only olduğu net, derleme patlamaz
- ✅ **Windows-aware** — sadece çalışanı sunar, gerisi `NotSupported`

---