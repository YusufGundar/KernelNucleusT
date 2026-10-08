# knst_process — Cross-Platform Process Management

Selamlar! 👋 Bu doküman `knst_process` sınıfının ne olduğunu, nasıl kullanıldığını ve neden KernelNucleusT'de tercih edildiğini açıklar.

Kısaca: **Cross-platform process yönetimi + GPU izleme kütüphanesi.** Linux ve Windows'ta aynı API'yi kullanarak program başlatır, çıktısını yakalar, stdin'e yazar, timeout uygular, PID listeler, thread/FD/env bilgisi okur ve **NVIDIA/AMD GPU**'ları izler.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Cross-platform** | Linux ve Windows'ta aynı API |
| **Program başlatma** | `run`, `run_shell`, `run_capture`, `run_detached` |
| **Output capture** | stdout + stderr aynı anda, binary-safe |
| **stdin piping** | Çocuk process'e veri gönder |
| **Timeout** | Belirtilen sürede bitmezse otomatik kill |
| **Detached process** | Fire-and-forget, parent beklemez |
| **Process listeleme** | PID listesi, isim arama, children, descendants |
| **Process inspection** | RSS/VSZ, CPU süresi, thread sayısı, FD sayısı |
| **Thread / FD / Env** | `/proc/[pid]/task`, `/proc/[pid]/fd`, `/proc/[pid]/environ` |
| **Suspend / Resume** | Process'i duraklat ve devam ettir |
| **Priority / Affinity** | nice değeri ve CPU çekirdek ataması |
| **GPU izleme** | NVIDIA (NVML), AMD (Linux: sysfs / Windows: DXGI+PDH), Intel (DXGI) |
| **GPU process listesi** | Hangi process hangi GPU'yu kullanıyor, VRAM |
| **Shell seçimi** | sh, bash, zsh, fish, cmd, powershell |
| **UTF-16 dostu** | `knst_c16string` ile Unicode yollar |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Shell komutu çalıştır ve çıktısını al
    knst_process p = knst_process::run_shell(u"echo merhaba");

    knst_process_result r = p.communicate(nullptr, 0, 5000);

    std::cout << "Çıktı   : " << r.out_data << "\n";
    std::cout << "Exit    : " << r.exit_code << "\n";
    return 0;
}
```

---

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Program başlatma** | `run`, `run_shell`, `run_with_shell`, `run_detached`, `run_capture` |
| **İletişim** | `communicate`, `read_stdout`, `read_stderr`, `write_stdin`, `close_stdin` |
| **Satır okuma** | `read_line`, `try_read_stdout`, `try_read_stderr` |
| **Bekleme** | `wait`, `wait_or_kill`, `is_running`, `exit_code` |
| **Kill / Signal** | `kill`, `force_kill`, `kill_tree`, `send_signal` |
| **Suspend / Resume** | `suspend`, `resume` |
| **Priority / Affinity** | `set_priority`, `set_affinity` |
| **PID listesi** | `list_pids`, `list_all`, `find_by_name`, `children_of`, `descendants_of` |
| **Kontrol** | `exists`, `is_alive`, `is_zombie` |
| **Info** | `get_info`, `get_exe_path`, `get_cmdline`, `get_cwd`, `get_user` |
| **Memory / CPU** | `get_rss_bytes`, `get_vsz_bytes`, `get_cpu_times` |
| **Thread / FD / Env** | `get_threads`, `get_fds`, `get_env` |
| **Kill helpers** | `kill_by_pid`, `kill_by_name`, `kill_tree`, `kill_children`, `terminate_graceful` |
| **System** | `current_pid`, `parent_pid`, `current_user`, `get_system_info` |
| **GPU** | `knst_gpu::list_all`, `get_processes`, `nvidia_driver_version` |

---

## 🎯 1) Program Başlatma

### `run` — Doğrudan Çalıştırma (Shell Yok)

Programı doğrudan invoke eder, shell araya girmez:

```cpp
knst_vector<knst_c16string> args;
args.push_back(u"-la");
args.push_back(u"/home/user");

knst_process p = knst_process::run(u"/bin/ls", args);
if (p.error == knst_process_error::None) {
    auto r = p.communicate(nullptr, 0, 5000);
    std::cout << r.out_data << "\n";
}
```

**Avantajı:** Shell injection riski yok. Argümanlar doğrudan `execve`'ye gider.

### `run_shell` — Shell Komutu

Platformun varsayılan shell'ini kullanır:

```cpp
// POSIX:   /bin/sh -c "..."
// Windows: cmd.exe /c "..."
knst_process p = knst_process::run_shell(u"ls -la | grep .txt");
auto r = p.communicate(nullptr, 0, 5000);
```

### `run_with_shell` — Shell Tipi Seç

```cpp
knst_process p = knst_process::run_with_shell(
    knst_shell_type::Bash, u"echo $BASH_VERSION");
```

Desteklenen shell'ler:

| Shell | POSIX | Windows |
|---|---|---|
| `Sh` | `/bin/sh` | — |
| `Bash` | `/bin/bash` | — |
| `Zsh` | `/bin/zsh` | — |
| `Fish` | `/usr/bin/fish` | — |
| `Cmd` | — | `cmd.exe` |
| `PowerShell` | — | `powershell.exe` |
| `PowerShellCore` | — | `pwsh.exe` |
| `Custom` | `opts.shell_path` | `opts.shell_path` |

### `run_capture` — Tek Çağrıda Çalıştır + Yakala

En pratik API — başlatır, bekler, output döner:

```cpp
auto r = knst_process::run_capture(
    u"ls",
    { u"-la" },
    {},
    nullptr,  // stdin verisi
    0,        // stdin boyutu
    5000);    // timeout ms

std::cout << r.out_data << "\n";
std::cout << "Exit: " << r.exit_code << "\n";
```

### `run_detached` — Fire-and-Forget

Arka planda çalışır, parent beklemez:

```cpp
knst_process p = knst_process::run_detached(
    u"/usr/bin/notify-send",
    { u"Başlık", u"Mesaj" });

std::cout << "PID: " << p.pid() << "\n";
// Program devam ederken bu process arka planda çalışır
```

**Windows'ta:** `DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB` kullanılır.

---

## 💬 2) İletişim — `communicate()`

En güçlü API — stdin'e yazar, stdout+stderr okur, timeout uygular:

```cpp
knst_process p = knst_process::run(u"/bin/cat");

const char* input = "satır 1\nsatır 2\nsatır 3\n";
auto r = p.communicate(input, std::strlen(input), 5000);

std::cout << "Echo:\n" << r.out_data << "\n";
std::cout << "Timeout: " << (r.timed_out ? "evet" : "hayır") << "\n";
```

### `knst_process_result` Yapısı

```cpp
struct knst_process_result {
    knst_byte_string out_data;   // stdout
    knst_byte_string err_data;   // stderr
    int  exit_code = -1;
    bool timed_out = false;
    knst_process_error error = knst_process_error::None;
};
```

### Timeout Davranışı

Timeout **aşılırsa** process otomatik kill edilir:

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"5" });
auto r = p.communicate(nullptr, 0, 300);   // 300 ms timeout

std::cout << "timed_out: " << (r.timed_out ? "evet" : "hayır") << "\n";
// "evet" — sleep 5 kill edildi
```

### Alternatif Okuma Metodları

```cpp
// stdout'u oku
knst_byte_string data = p.read_stdout();
knst_c16string text = p.read_stdout_text();   // UTF-16'ya çevirir

// stderr'u oku
knst_byte_string err = p.read_stderr();

// Satır satır oku
knst_c16string line;
while (p.read_line(line)) {
    std::cout << "> " << line << "\n";
}

// Timeout ile oku
knst_byte_string chunk;
if (p.try_read_stdout(100, chunk)) {
    std::cout << "Gelen veri: " << chunk.length() << " byte\n";
}

// stdin'e yaz
p.write_stdin("veri\n", 6);
p.write_stdin(knst_c16string(u"UTF-16 metin\n"));
p.close_stdin();   // EOF sinyali
```

---

## ⏱️ 3) Timeout ve Bekleme

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"1" });

// Bekle (timeout'suz — process bitene kadar bloklar)
p.wait();

// 500 ms bekle
if (p.wait(500)) {
    std::cout << "1 saniyede bitti\n";
} else {
    std::cout << "hala çalışıyor\n";
}

// Hala çalışıyorsa: 500 ms bekle, sonra kill
p.wait_or_kill(500);

// Çalışıyor mu?
if (p.is_running()) { /* ... */ }

// Exit code
int code = p.exit_code();
```

---

## 🔪 4) Kill ve Signal

### Instance Metodları

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"100" });

// SIGTERM (nazik)
p.kill();

// SIGKILL (zorla)
p.force_kill();

// Çocuklarıyla birlikte öldür
p.kill_tree();

// Belirli bir sinyal gönder
p.send_signal(2);   // SIGINT
```

### Static Yardımcılar

```cpp
// PID ile öldür
knst_process::kill_by_pid(12345, 15);   // SIGTERM

// İsimle öldür
knst_process::kill_by_name(u"firefox", 15);

// Recursive — process + tüm descendants
knst_process::kill_tree(12345, 9);

// Nazikçe sonlandır — önce SIGTERM, timeout'ta SIGKILL
knst_process::terminate_graceful(12345, 3000);
```

### Kill Sinyalleri (POSIX)

| Sinyal | Değer | Anlamı |
|---|---|---|
| `SIGTERM` | 15 | Nazik sonlandırma (varsayılan) |
| `SIGKILL` | 9 | Zorla öldür (yakalanamaz) |
| `SIGINT` | 2 | Ctrl+C gibi |
| `SIGSTOP` | 19 | Duraklat (yakalanamaz) |
| `SIGCONT` | 18 | Devam ettir |

**Windows'ta:** Sinyaller desteklenmez, hepsi `TerminateProcess` çağırır.

---

## ⏸️ 5) Suspend / Resume

Process'i duraklat ve devam ettir:

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"10" });

p.suspend();   // Duraklat
std::this_thread::sleep_for(std::chrono::seconds(1));
p.resume();    // Devam ettir

// Static versiyonlar
knst_process::suspend_pid(12345);
knst_process::resume_pid(12345);
```

**Linux:** `SIGSTOP` / `SIGCONT`
**Windows:** `NtSuspendProcess` / `NtResumeProcess` (ntdll'den)

---

## ⚡ 6) Priority ve Affinity

### Priority

```cpp
knst_process p = knst_process::run(u"/bin/some_program");

// POSIX: nice değeri (-20 ile 19 arası)
// Windows: priority class'a eşlenir
p.set_priority(15);   // düşük öncelik
p.set_priority(-5);   // yüksek öncelik

// Static
knst_process::set_priority(12345, 10);
```

**nice → Windows priority class eşlemesi:**

| nice | Windows Priority Class |
|---|---|
| -20 .. -15 | `HIGH_PRIORITY_CLASS` |
| -15 .. -5 | `ABOVE_NORMAL_PRIORITY_CLASS` |
| -5 .. 15 | `NORMAL_PRIORITY_CLASS` |
| 15 .. 20 | `BELOW_NORMAL_PRIORITY_CLASS` |
| 20+ | `IDLE_PRIORITY_CLASS` |

### Affinity (CPU Çekirdek Ataması)

```cpp
// Sadece 0. ve 2. çekirdeklerde çalışsın
p.set_affinity(0b0101);   // bit 0 ve bit 2

// Static
knst_process::set_affinity(12345, 0xFF);   // ilk 8 çekirdek
```

**Linux:** `sched_setaffinity()`
**Windows:** `SetProcessAffinityMask()`

---

## 📋 7) Process Listeleme

### Tüm PID'ler

```cpp
knst_vector<uint32_t> pids = knst_process::list_pids();
std::cout << "Toplam " << pids.size() << " process\n";
```

### Detaylı Liste

```cpp
knst_vector<knst_process_info> procs = knst_process::list_all();

for (const auto& p : procs) {
    std::cout << p.pid << "  " << p.name 
              << "  rss=" << p.rss_bytes / 1024 << " KB\n";
}

// GPU bilgisiyle birlikte
knst_vector<knst_process_info> with_gpu = knst_process::list_all(true);
for (const auto& p : with_gpu) {
    if (p.uses_gpu()) {
        std::cout << p.name << "  GPU" << p.gpu.gpu_index 
                  << "  VRAM=" << p.gpu.vram_bytes / (1024*1024) << " MB\n";
    }
}
```

### İsimle Arama

```cpp
// Case-insensitive substring arama
auto found = knst_process::find_by_name(u"chrome", false);

// Tam eşleşme
auto exact = knst_process::find_by_name(u"chrome", true);
```

### Çocuk ve Torunlar

```cpp
// Doğrudan çocuklar
knst_vector<uint32_t> kids = knst_process::children_of(12345);

// Tüm torunlar (recursive)
knst_vector<knst_process_info> descendants = 
    knst_process::descendants_of(12345);
```

---

## 🔍 8) Process Inspection

### `knst_process_info` Yapısı

```cpp
struct knst_process_info {
    // Kimlik
    uint32_t pid  = 0;
    uint32_t ppid = 0;
    uint32_t pgid = 0;
    uint32_t sid  = 0;

    // Metin bilgileri
    knst_c16string name;
    knst_c16string exe_path;
    knst_c16string cmdline;
    knst_c16string cwd;
    knst_c16string user;
    uint32_t uid = 0;
    uint32_t gid = 0;

    // Bellek
    uint64_t rss_bytes    = 0;   // Resident set size
    uint64_t vsz_bytes    = 0;   // Virtual size
    uint64_t shared_bytes = 0;
    uint64_t text_bytes   = 0;

    // CPU
    uint64_t cpu_user_ms   = 0;
    uint64_t cpu_system_ms = 0;
    uint64_t start_time_ms = 0;

    // Diğer
    int32_t  nice         = 0;
    int32_t  priority     = 0;
    uint32_t thread_count = 0;
    uint32_t fd_count     = 0;

    knst_process_state state = knst_process_state::Unknown;
    int32_t exit_code = -1;
    bool is_kernel_thread = false;
    bool is_zombie        = false;

    // GPU (opsiyonel)
    knst_gpu_usage gpu;
    bool uses_gpu() const noexcept { return gpu.valid; }

    // Kısmi veri bayrakları
    bool has_exe     = false;
    bool has_cmdline = false;
    bool has_cwd     = false;
    bool has_user    = false;
    bool has_memory  = false;
    bool has_cpu     = false;
    bool has_threads = false;
};
```

### Kullanım

```cpp
uint32_t pid = knst_process::current_pid();
knst_process_info info = knst_process::get_info(pid);

std::cout << "PID       : " << info.pid  << "\n";
std::cout << "PPID      : " << info.ppid << "\n";
std::cout << "Name      : " << info.name << "\n";
std::cout << "Exe       : " << info.exe_path << "\n";
std::cout << "Cmdline   : " << info.cmdline << "\n";
std::cout << "State     : " << knst_process_state_string(info.state) << "\n";
std::cout << "RSS       : " << info.rss_bytes / 1024 << " KB\n";
std::cout << "Threads   : " << info.thread_count << "\n";
std::cout << "FDs       : " << info.fd_count << "\n";
```

### Process State'leri

| State | Anlamı |
|---|---|
| `Running` | Çalışıyor |
| `Sleeping` | Uyuyor (interruptible) |
| `DiskSleep` | Disk I/O bekliyor |
| `Zombie` | Ölmüş ama parent reap etmemiş |
| `Stopped` | Duraklatılmış |
| `Tracing` | Debugger tarafından izleniyor |
| `Dead` | Ölü |
| `Idle` | Boş |
| `Waiting` | Bekliyor |

### Static Erişimciler

```cpp
knst_c16string exe = knst_process::get_exe_path(pid);
knst_c16string cmd = knst_process::get_cmdline(pid);
knst_c16string cwd = knst_process::get_cwd(pid);
knst_c16string usr = knst_process::get_user(pid);
uint32_t ppid = knst_process::get_ppid(pid);
int32_t nice = knst_process::get_nice(pid);
uint64_t rss = knst_process::get_rss_bytes(pid);
uint64_t vsz = knst_process::get_vsz_bytes(pid);
knst_process_state s = knst_process::get_state(pid);
```

---

## 🧵 9) Thread Bilgisi

```cpp
knst_vector<knst_thread_info> threads = knst_process::get_threads(pid);

for (const auto& t : threads) {
    std::cout << "TID: " << t.tid << "  Name: " << t.name << "\n";
}

// Sadece TID listesi
knst_vector<uint32_t> tids = knst_process::get_thread_ids(pid);
```

**Linux:** `/proc/[pid]/task/`
**Windows:** `CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD)`

---

## 🔗 10) File Descriptor Bilgisi (POSIX)

```cpp
knst_vector<knst_fd_info> fds = knst_process::get_fds(pid);

for (const auto& fd : fds) {
    std::cout << "FD " << fd.fd << " -> " << fd.target;
    if (fd.is_socket) std::cout << " (socket)";
    if (fd.is_pipe)   std::cout << " (pipe)";
    if (fd.is_file)   std::cout << " (file)";
    std::cout << "\n";
}
```

**Örnek çıktı:**

```
FD 0 -> /dev/pts/2 (file)
FD 1 -> /dev/pts/2 (file)
FD 2 -> /dev/pts/2 (file)
FD 3 -> socket:[12345] (socket)
FD 5 -> /home/user/data.txt (file)
```

**Not:** Sadece Linux'ta desteklenir.

---

## 🌍 11) Environment Variables (POSIX)

```cpp
knst_vector<knst_env_var> env = knst_process::get_env(pid);

for (const auto& e : env) {
    std::cout << e.name << "=" << e.value << "\n";
}
```

**Kaynak:** `/proc/[pid]/environ`

---

## 🛠️ 12) `knst_process_options`

Spawn seçenekleri — her alanın makul bir varsayılanı var.

### Çalışma Dizini ve Ortam

```cpp
knst_process_options o;
o.cwd = u"/home/user/project";

// Environment değişkenleri ekle
knst_env_var env;
env.name  = u"MY_VAR";
env.value = u"value";
o.env.push_back(env);

o.inherit_env = true;   // parent env'i kullan
```

### Stdio Kontrolü

```cpp
o.stdin_mode  = knst_stdio_mode::Inherit;  // Pipe, Null, File
o.stdout_mode = knst_stdio_mode::Pipe;
o.stderr_mode = knst_stdio_mode::Pipe;

// Dosyaya yönlendir
o.stdout_file = u"/tmp/out.log";
o.stderr_file = u"/tmp/err.log";

// Kolaylık bayrakları
o.capture_stdout = true;
o.capture_stderr = true;
o.inherit_stdin  = true;
```

### Pencere ve Session

```cpp
o.window_mode = knst_window_mode::Hidden;   // Hidden, Minimized, Maximized, Normal
o.new_session        = true;   // POSIX: setsid()
o.new_console        = true;   // Windows: CREATE_NEW_CONSOLE
o.new_process_group  = true;
o.detached           = true;
```

### Shell Seçimi

```cpp
o.shell = knst_shell_type::Bash;
o.shell_path = u"/usr/bin/bash";   // Custom için
```

### Priority ve Affinity

```cpp
o.nice_value    = 10;    // POSIX nice
o.priority      = 10;    // Windows priority class
o.affinity_mask = 0b0011;  // ilk 2 çekirdek
```

### Diğer

```cpp
o.suspend_after_spawn = true;   // SIGSTOP/CREATE_SUSPENDED ile başlat
o.close_extra_fds     = true;
o.win_unicode         = true;
```

---

## 🎮 13) GPU İzleme

### `knst_gpu` API'si

```cpp
knst_gpu::has_gpu();                    // GPU var mı?
knst_gpu::count();                      // Kaç GPU?
knst_gpu::nvml_available();             // NVML yüklü mü?

knst_c16string drv  = knst_gpu::nvidia_driver_version();
knst_c16string cuda = knst_gpu::cuda_version();

auto gpus = knst_gpu::list_all();
auto procs = knst_gpu::get_processes();
auto procs0 = knst_gpu::get_processes(0);   // Sadece GPU 0
```

### `knst_gpu_info` Yapısı

```cpp
struct knst_gpu_info {
    uint32_t index;
    knst_c16string name;            // "NVIDIA GeForce RTX 4090"
    knst_c16string vendor_name;     // "NVIDIA"
    knst_c16string driver_version;
    knst_c16string uuid;
    knst_c16string vbios_version;
    knst_gpu_vendor vendor;         // NVIDIA, AMD, Intel, Apple, Other

    // Bellek
    uint64_t memory_total;
    uint64_t memory_used;
    uint64_t memory_free;

    // Kullanım
    uint32_t utilization_gpu;       // %
    uint32_t utilization_mem;       // %

    // Sıcaklık
    int32_t  temperature_core;      // °C

    // Güç
    uint32_t power_usage_mw;        // mW
    uint32_t power_limit_mw;

    // Frekanslar
    uint32_t clock_core_mhz;
    uint32_t clock_mem_mhz;

    // Fan
    uint32_t fan_percent;           // %

    // PCIe
    uint32_t pcie_gen;
    uint32_t pcie_width;

    // Bayraklar
    bool valid;
    bool has_memory;
    bool has_utilization;
    bool has_temperature;
    bool has_power;
    bool has_clocks;
    bool has_fan;
    bool has_pcie;
};
```

### Kullanım

```cpp
auto gpus = knst_gpu::list_all();

for (const auto& g : gpus) {
    std::cout << "[" << g.index << "] " << g.name 
              << "  (" << knst_gpu_vendor_string(g.vendor) << ")\n";

    if (g.has_memory) {
        std::cout << "    VRAM: " << g.memory_used / (1024*1024) 
                  << " / " << g.memory_total / (1024*1024) << " MB\n";
    }
    if (g.has_utilization) {
        std::cout << "    GPU : " << g.utilization_gpu << "%\n";
    }
    if (g.has_temperature) {
        std::cout << "    Temp: " << g.temperature_core << " °C\n";
    }
    if (g.has_power) {
        std::cout << "    Güç : " << g.power_usage_mw / 1000 << " W\n";
    }
}
```

### GPU Process'leri

```cpp
auto procs = knst_gpu::get_processes();

for (const auto& p : procs) {
    std::cout << "PID " << p.pid 
              << "  GPU" << p.gpu_index
              << "  VRAM=" << p.vram_bytes / (1024*1024) << " MB"
              << "  " << p.name << "\n";
}
```

**Çıktı:**

```
PID 12345  GPU0  VRAM=2048 MB  blender
PID 23456  GPU0  VRAM=512 MB   firefox
```

### GPU Vendor Detection

**Linux:**
- **NVIDIA:** NVML (libnvidia-ml.so)
- **AMD:** `/sys/class/drm/card*/device/mem_info_vram_*`
- **Intel:** `/sys/class/drm/card*/device/vendor` (0x8086)

**Windows:**
- **NVIDIA:** NVML (nvml.dll) — tam veri
- **Tüm markalar:** DXGI (`IDXGIFactory1` + `IDXGIAdapter3`) — isim, VRAM total/used
- **GPU kullanım %:** PDH (`\GPU Engine(*)\Utilization Percentage`) — LUID bazlı
- **Fallback:** WMI `Win32_VideoController` — eski sürücüler için son çare

### Process + GPU Bilgisi

```cpp
// list_all(true) → GPU bilgisini de içerir
auto procs = knst_process::list_all(true);

for (const auto& p : procs) {
    if (p.uses_gpu()) {
        std::cout << p.name 
                  << "  GPU" << p.gpu.gpu_index
                  << "  VRAM=" << p.gpu.vram_bytes / (1024*1024) << " MB\n";
    }
}
```

---

## 🌐 14) Sistem Bilgisi

```cpp
knst_process::system_info si = knst_process::get_system_info();

std::cout << "CPU sayısı : " << si.cpu_count << "\n";
std::cout << "Sayfa boyutu: " << si.page_size << " byte\n";
std::cout << "RAM toplam : " << si.total_memory / (1024*1024*1024) << " GB\n";
std::cout << "RAM boş    : " << si.free_memory / (1024*1024*1024) << " GB\n";
std::cout << "Uptime     : " << si.uptime_ms / 1000 / 3600 << " saat\n";
```

### Aktif Process Bilgisi

```cpp
uint32_t pid   = knst_process::current_pid();
uint32_t ppid  = knst_process::parent_pid();
uint32_t uid   = knst_process::current_uid();
knst_c16string user = knst_process::current_user();
knst_c16string exe  = knst_process::current_exe_path();
```

---

## 🛡️ 15) Hata Yönetimi

### `knst_process_error` Enum

```cpp
enum class knst_process_error : uint8_t {
    None = 0,
    NotFound,             // yürütülebilir bulunamadı
    PermissionDenied,     // izin reddedildi
    InvalidArgument,
    Timeout,
    PipeFailed,
    ForkFailed,
    ExecFailed,
    WaitFailed,
    AlreadyRunning,
    NotRunning,
    Crashed,
    OutOfMemory,
    ShellNotFound,
    FileRedirectFailed,
    AccessDenied,
    NoSuchProcess,
    SignalFailed,
    NotSupported,
    ReadFailed,
    ParseFailed,
    SystemCallFailed,
    Unknown
};
```

### Hata String'i

```cpp
knst_process p = knst_process::run(u"/nonexistent");
if (p.error != knst_process_error::None) {
    std::cerr << "Hata: " << p.error_string() << "\n";
    // "Executable not found"
}
```

### Native Hata Kodu

```cpp
int32_t native = p.native_error_code();
// POSIX: errno
// Windows: GetLastError()
```

---

## 🌍 16) Platform Farkları

| Konu | Linux (POSIX) | Windows |
|---|---|---|
| **Spawn** | `fork()` + `execve()` | `CreateProcessW()` |
| **Shell** | `/bin/sh` | `cmd.exe` |
| **Signal** | Tam destek (SIGTERM, SIGKILL, ...) | Sadece kill |
| **Suspend** | `SIGSTOP` | `NtSuspendProcess` |
| **Resume** | `SIGCONT` | `NtResumeProcess` |
| **Priority** | nice (-20..19) | Priority class |
| **Affinity** | `sched_setaffinity` | `SetProcessAffinityMask` |
| **Process list** | `/proc` tarama | `CreateToolhelp32Snapshot` |
| **Cmdline** | `/proc/[pid]/cmdline` | PEB okuma |
| **Threads** | `/proc/[pid]/task` | `TH32CS_SNAPTHREAD` |
| **FDs** | `/proc/[pid]/fd` | Yok |
| **Environment** | `/proc/[pid]/environ` | Yok |
| **GPU (NVIDIA)** | NVML | NVML |
| **GPU (AMD)** | sysfs (tam) | DXGI (VRAM+isim) + PDH (kullanım %) |
| **GPU (Intel)** | sysfs i915 | DXGI + PDH |
| **Uptime** | `/proc/uptime` | `GetTickCount64` |

---

## 🔥 17) Gerçek Kullanım Örnekleri

### Örnek 1: Basit Komut Çalıştırma

```cpp
void run_and_print(const knst_c16string& cmd) {
    auto r = knst_process::run_capture(
        u"/bin/sh", { u"-c", cmd }, {}, nullptr, 0, 5000);

    if (r.error != knst_process_error::None) {
        std::cerr << "Hata: " << knst_process_error_string(r.error) << "\n";
        return;
    }
    std::cout << r.out_data << "\n";
}

run_and_print(u"df -h");
```

### Örnek 2: Timeout'lu Build

```cpp
bool run_build_with_timeout(const knst_c16string& cmd, uint32_t secs) {
    knst_process p = knst_process::run_shell(cmd);
    auto r = p.communicate(nullptr, 0, secs * 1000);

    if (r.timed_out) {
        std::cerr << "Build " << secs << " saniyede bitmedi, kill edildi\n";
        return false;
    }
    if (r.exit_code != 0) {
        std::cerr << "Build hatası:\n" << r.err_data << "\n";
        return false;
    }
    return true;
}
```

### Örnek 3: Process İzleme Aracı

```cpp
void monitor_process(const knst_c16string& name) {
    auto found = knst_process::find_by_name(name, false);

    if (found.empty()) {
        std::cout << "Bulunamadı: " << name << "\n";
        return;
    }

    for (const auto& p : found) {
        std::cout << "\nPID " << p.pid << "  " << p.name << "\n";
        std::cout << "  RSS      : " << p.rss_bytes / 1024 << " KB\n";
        std::cout << "  Threads  : " << p.thread_count << "\n";
        std::cout << "  CPU user : " << p.cpu_user_ms << " ms\n";
        std::cout << "  State    : " 
                  << knst_process_state_string(p.state) << "\n";

        if (p.uses_gpu()) {
            std::cout << "  GPU      : " << p.gpu.gpu_index << "\n";
            std::cout << "  VRAM     : " << p.gpu.vram_bytes / (1024*1024) << " MB\n";
        }
    }
}

monitor_process(u"blender");
```

### Örnek 4: GPU İzleme

```cpp
void print_gpu_dashboard() {
    auto gpus = knst_gpu::list_all();

    for (const auto& g : gpus) {
        std::cout << "[" << g.index << "] " << g.name << "\n";

        if (g.has_memory) {
            double used_pct = 100.0 * g.memory_used / g.memory_total;
            std::cout << "    VRAM  : "
                      << g.memory_used  / (1024*1024) << " / "
                      << g.memory_total / (1024*1024) << " MB ("
                      << (int)used_pct << "%)\n";
        }
        if (g.has_utilization) {
            std::cout << "    GPU   : " << g.utilization_gpu << "%\n";
        }
        if (g.has_temperature) {
            std::cout << "    Temp  : " << g.temperature_core << " °C\n";
        }
        if (g.has_power) {
            std::cout << "    Güç   : " << g.power_usage_mw / 1000 << " W\n";
        }
    }
}
```

### Örnek 5: Child Process Öldür

```cpp
void cleanup_children(uint32_t parent_pid) {
    auto kids = knst_process::children_of(parent_pid);

    for (uint32_t pid : kids) {
        std::cout << "Öldürülüyor: " << pid 
                  << " (" << knst_process::get_info(pid).name << ")\n";
        knst_process::terminate_graceful(pid, 2000);
    }
}
```

### Örnek 6: Pipe ile Veri Akışı

```cpp
void process_text_lines() {
    knst_process p = knst_process::run(u"/bin/grep", { u"error" });

    // stdin'e veri yaz (paralel thread'de)
    std::thread writer([&p]() {
        p.write_stdin("line 1: info\n", 14);
        p.write_stdin("line 2: error\n", 15);
        p.write_stdin("line 3: info\n", 14);
        p.close_stdin();
    });

    // Satır satır oku
    knst_c16string line;
    while (p.read_line(line)) {
        std::cout << "Match: " << line << "\n";
    }

    writer.join();
    p.wait();
}
```

### Örnek 7: Suspend/Resume ile Batch

```cpp
void pause_and_resume(uint32_t pid) {
    std::cout << "Duraklat: " << pid << "\n";
    knst_process::suspend_pid(pid);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "Devam ettir: " << pid << "\n";
    knst_process::resume_pid(pid);
}
```

---

## 📊 18) Performans Notları

| İşlem | Karmaşıklık | Not |
|---|---|---|
| `run` | O(1) | fork + exec |
| `communicate` | O(n) | Tüm çıktı bellekte |
| `wait` | O(1) | waitpid |
| `list_pids` | O(pid sayısı) | `/proc` tarama |
| `list_all` | O(pid sayısı × dosya) | Her PID için ~5 dosya |
| `find_by_name` | O(pid sayısı) | `list_all` üzerinden |
| `get_info` | O(dosya) | `/proc/[pid]/*` okuma |
| `children_of` | O(pid sayısı) | Her `/proc/*/stat` okuma |
| `knst_gpu::list_all` | O(GPU sayısı) | NVML/sysfs |

### Ne Zaman Yavaş?

- **`list_all`** — 300+ process varsa 1-2 saniye sürebilir
- **`descendants_of`** — Recursive olduğu için process tree büyükse yavaş
- **`get_env`** — `/proc/[pid]/environ` büyükse yavaş
- **GPU cache** — İlk çağrı NVML yükleme + WMI sorgu yapar (yavaş), sonrası cache

### Optimizasyon İpuçları

```cpp
// ❌ Yavaş: Her seferinde tüm process'leri tara
for (int i = 0; i < 100; ++i) {
    auto all = knst_process::list_all();
}

// ✅ Hızlı: Bir kez tara, bellekte tut
auto all = knst_process::list_all();
for (int i = 0; i < 100; ++i) {
    // all üzerinden çalış
}
```

---

## 🎯 Size Getireceği Kazanç

`knst_process` şu durumlarda ciddi avantaj sağlar:

- ✅ **Build / CI sistemleri** — komut çalıştır, output yakala, timeout uygula
- ✅ **Sistem izleme araçları** — process listesi, thread, memory
- ✅ **GPU telemetri** — mining, oyun, render izleme
- ✅ **Otomasyon scriptleri** — Python/Node.js yerine C++ ile
- ✅ **Cross-platform tooling** — Windows ve Linux'ta aynı kod
- ✅ **Güvenli komut çalıştırma** — shell injection olmadan argüman geçirme

