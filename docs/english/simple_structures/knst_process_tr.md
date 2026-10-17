# knst_process — Cross-Platform Process Management

Hello! 👋 This document explains what the `knst_process` class is, how to use it, and why it exists in KernelNucleusT.

In short: **a cross-platform process management + GPU monitoring library.** Using the same API on Linux and Windows, it launches programs, captures their output, writes to stdin, applies timeouts, lists PIDs, reads thread/FD/env information, and **monitors NVIDIA/AMD GPUs**.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Cross-platform** | Same API on Linux and Windows |
| **Program launching** | `run`, `run_shell`, `run_capture`, `run_detached` |
| **Output capture** | stdout + stderr simultaneously, binary-safe |
| **stdin piping** | Send data to the child process |
| **Timeout** | Auto-kill if it does not finish in time |
| **Detached process** | Fire-and-forget, parent does not wait |
| **Process listing** | PID list, name search, children, descendants |
| **Process inspection** | RSS/VSZ, CPU time, thread count, FD count |
| **Thread / FD / Env** | `/proc/[pid]/task`, `/proc/[pid]/fd`, `/proc/[pid]/environ` |
| **Suspend / Resume** | Pause and resume a process |
| **Priority / Affinity** | nice value and CPU core assignment |
| **GPU monitoring** | NVIDIA (NVML), AMD (Linux: sysfs / Windows: DXGI+PDH), Intel (DXGI) |
| **GPU process list** | Which process uses which GPU, VRAM |
| **Shell selection** | sh, bash, zsh, fish, cmd, powershell |
| **UTF-16 friendly** | Unicode paths with `knst_c16string` |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Run a shell command and get its output
    knst_process p = knst_process::run_shell(u"echo hello");

    knst_process_result r = p.communicate(nullptr, 0, 5000);

    std::cout << "Output  : " << r.out_data << "\n";
    std::cout << "Exit    : " << r.exit_code << "\n";
    return 0;
}
```

---

## 📋 Overview API

| Category | Methods |
|---|---|
| **Program launching** | `run`, `run_shell`, `run_with_shell`, `run_detached`, `run_capture` |
| **Communication** | `communicate`, `read_stdout`, `read_stderr`, `write_stdin`, `close_stdin` |
| **Line reading** | `read_line`, `try_read_stdout`, `try_read_stderr` |
| **Waiting** | `wait`, `wait_or_kill`, `is_running`, `exit_code` |
| **Kill / Signal** | `kill`, `force_kill`, `kill_tree`, `send_signal` |
| **Suspend / Resume** | `suspend`, `resume` |
| **Priority / Affinity** | `set_priority`, `set_affinity` |
| **PID listing** | `list_pids`, `list_all`, `find_by_name`, `children_of`, `descendants_of` |
| **Control** | `exists`, `is_alive`, `is_zombie` |
| **Info** | `get_info`, `get_exe_path`, `get_cmdline`, `get_cwd`, `get_user` |
| **Memory / CPU** | `get_rss_bytes`, `get_vsz_bytes`, `get_cpu_times` |
| **Thread / FD / Env** | `get_threads`, `get_fds`, `get_env` |
| **Kill helpers** | `kill_by_pid`, `kill_by_name`, `kill_tree`, `kill_children`, `terminate_graceful` |
| **System** | `current_pid`, `parent_pid`, `current_user`, `get_system_info` |
| **GPU** | `knst_gpu::list_all`, `get_processes`, `nvidia_driver_version` |

---

## 🎯 1) Program Launching

### `run` — Direct Execution (No Shell)

Invokes the program directly; no shell intervenes:

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

**Advantage:** No shell injection risk. Arguments go directly to `execve`.

### `run_shell` — Shell Command

Uses the platform's default shell:

```cpp
// POSIX:   /bin/sh -c "..."
// Windows: cmd.exe /c "..."
knst_process p = knst_process::run_shell(u"ls -la | grep .txt");
auto r = p.communicate(nullptr, 0, 5000);
```

### `run_with_shell` — Choose a Shell Type

```cpp
knst_process p = knst_process::run_with_shell(
    knst_shell_type::Bash, u"echo $BASH_VERSION");
```

Supported shells:

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

### `run_capture` — Run + Capture in One Call

The most practical API — launches, waits, returns output:

```cpp
auto r = knst_process::run_capture(
    u"ls",
    { u"-la" },
    {},
    nullptr,  // stdin data
    0,        // stdin size
    5000);    // timeout ms

std::cout << r.out_data << "\n";
std::cout << "Exit: " << r.exit_code << "\n";
```

### `run_detached` — Fire-and-Forget

Runs in the background; parent does not wait:

```cpp
knst_process p = knst_process::run_detached(
    u"/usr/bin/notify-send",
    { u"Title", u"Message" });

std::cout << "PID: " << p.pid() << "\n";
// The program continues running in the background
```

**On Windows:** Uses `DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB`.

---

## 💬 2) Communication — `communicate()`

The most powerful API — writes to stdin, reads stdout+stderr, applies timeout:

```cpp
knst_process p = knst_process::run(u"/bin/cat");

const char* input = "line 1\nline 2\nline 3\n";
auto r = p.communicate(input, std::strlen(input), 5000);

std::cout << "Echo:\n" << r.out_data << "\n";
std::cout << "Timeout: " << (r.timed_out ? "yes" : "no") << "\n";
```

### `knst_process_result` Structure

```cpp
struct knst_process_result {
    knst_byte_string out_data;   // stdout
    knst_byte_string err_data;   // stderr
    int  exit_code = -1;
    bool timed_out = false;
    knst_process_error error = knst_process_error::None;
};
```

### Timeout Behavior

If the timeout is **exceeded**, the process is automatically killed:

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"5" });
auto r = p.communicate(nullptr, 0, 300);   // 300 ms timeout

std::cout << "timed_out: " << (r.timed_out ? "yes" : "no") << "\n";
// "yes" — sleep 5 was killed
```

### Alternative Reading Methods

```cpp
// Read stdout
knst_byte_string data = p.read_stdout();
knst_c16string text = p.read_stdout_text();   // converts to UTF-16

// Read stderr
knst_byte_string err = p.read_stderr();

// Read line by line
knst_c16string line;
while (p.read_line(line)) {
    std::cout << "> " << line << "\n";
}

// Read with timeout
knst_byte_string chunk;
if (p.try_read_stdout(100, chunk)) {
    std::cout << "Incoming data: " << chunk.length() << " bytes\n";
}

// Write to stdin
p.write_stdin("data\n", 5);
p.write_stdin(knst_c16string(u"UTF-16 text\n"));
p.close_stdin();   // EOF signal
```

---

## ⏱️ 3) Timeout and Waiting

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"1" });

// Wait (no timeout — blocks until process ends)
p.wait();

// Wait 500 ms
if (p.wait(500)) {
    std::cout << "Finished in 1 second\n";
} else {
    std::cout << "Still running\n";
}

// If still running: wait 500 ms, then kill
p.wait_or_kill(500);

// Is it running?
if (p.is_running()) { /* ... */ }

// Exit code
int code = p.exit_code();
```

---

## 🔪 4) Kill and Signal

### Instance Methods

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"100" });

// SIGTERM (gentle)
p.kill();

// SIGKILL (forceful)
p.force_kill();

// Kill with its children
p.kill_tree();

// Send a specific signal
p.send_signal(2);   // SIGINT
```

### Static Helpers

```cpp
// Kill by PID
knst_process::kill_by_pid(12345, 15);   // SIGTERM

// Kill by name
knst_process::kill_by_name(u"firefox", 15);

// Recursive — process + all descendants
knst_process::kill_tree(12345, 9);

// Terminate gracefully — SIGTERM first, SIGKILL on timeout
knst_process::terminate_graceful(12345, 3000);
```

### Kill Signals (POSIX)

| Signal | Value | Meaning |
|---|---|---|
| `SIGTERM` | 15 | Graceful termination (default) |
| `SIGKILL` | 9 | Force kill (uncatchable) |
| `SIGINT` | 2 | Like Ctrl+C |
| `SIGSTOP` | 19 | Pause (uncatchable) |
| `SIGCONT` | 18 | Resume |

**On Windows:** Signals are not supported; all of them call `TerminateProcess`.

---

## ⏸️ 5) Suspend / Resume

Pause and resume a process:

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"10" });

p.suspend();   // Pause
std::this_thread::sleep_for(std::chrono::seconds(1));
p.resume();    // Resume

// Static versions
knst_process::suspend_pid(12345);
knst_process::resume_pid(12345);
```

**Linux:** `SIGSTOP` / `SIGCONT`
**Windows:** `NtSuspendProcess` / `NtResumeProcess` (from ntdll)

---

## ⚡ 6) Priority and Affinity

### Priority

```cpp
knst_process p = knst_process::run(u"/bin/some_program");

// POSIX: nice value (-20 to 19)
// Windows: mapped to priority class
p.set_priority(15);   // low priority
p.set_priority(-5);   // high priority

// Static
knst_process::set_priority(12345, 10);
```

**nice → Windows priority class mapping:**

| nice | Windows Priority Class |
|---|---|
| -20 .. -15 | `HIGH_PRIORITY_CLASS` |
| -15 .. -5 | `ABOVE_NORMAL_PRIORITY_CLASS` |
| -5 .. 15 | `NORMAL_PRIORITY_CLASS` |
| 15 .. 20 | `BELOW_NORMAL_PRIORITY_CLASS` |
| 20+ | `IDLE_PRIORITY_CLASS` |

### Affinity (CPU Core Assignment)

```cpp
// Run only on cores 0 and 2
p.set_affinity(0b0101);   // bit 0 and bit 2

// Static
knst_process::set_affinity(12345, 0xFF);   // first 8 cores
```

**Linux:** `sched_setaffinity()`
**Windows:** `SetProcessAffinityMask()`

---

## 📋 7) Process Listing

### All PIDs

```cpp
knst_vector<uint32_t> pids = knst_process::list_pids();
std::cout << "Total " << pids.size() << " processes\n";
```

### Detailed List

```cpp
knst_vector<knst_process_info> procs = knst_process::list_all();

for (const auto& p : procs) {
    std::cout << p.pid << "  " << p.name 
              << "  rss=" << p.rss_bytes / 1024 << " KB\n";
}

// With GPU info
knst_vector<knst_process_info> with_gpu = knst_process::list_all(true);
for (const auto& p : with_gpu) {
    if (p.uses_gpu()) {
        std::cout << p.name << "  GPU" << p.gpu.gpu_index 
                  << "  VRAM=" << p.gpu.vram_bytes / (1024*1024) << " MB\n";
    }
}
```

### Search by Name

```cpp
// Case-insensitive substring search
auto found = knst_process::find_by_name(u"chrome", false);

// Exact match
auto exact = knst_process::find_by_name(u"chrome", true);
```

### Children and Descendants

```cpp
// Direct children
knst_vector<uint32_t> kids = knst_process::children_of(12345);

// All descendants (recursive)
knst_vector<knst_process_info> descendants = 
    knst_process::descendants_of(12345);
```

---

## 🔍 8) Process Inspection

### `knst_process_info` Structure

```cpp
struct knst_process_info {
    // Identity
    uint32_t pid  = 0;
    uint32_t ppid = 0;
    uint32_t pgid = 0;
    uint32_t sid  = 0;

    // Text information
    knst_c16string name;
    knst_c16string exe_path;
    knst_c16string cmdline;
    knst_c16string cwd;
    knst_c16string user;
    uint32_t uid = 0;
    uint32_t gid = 0;

    // Memory
    uint64_t rss_bytes    = 0;   // Resident set size
    uint64_t vsz_bytes    = 0;   // Virtual size
    uint64_t shared_bytes = 0;
    uint64_t text_bytes   = 0;

    // CPU
    uint64_t cpu_user_ms   = 0;
    uint64_t cpu_system_ms = 0;
    uint64_t start_time_ms = 0;

    // Other
    int32_t  nice         = 0;
    int32_t  priority     = 0;
    uint32_t thread_count = 0;
    uint32_t fd_count     = 0;

    knst_process_state state = knst_process_state::Unknown;
    int32_t exit_code = -1;
    bool is_kernel_thread = false;
    bool is_zombie        = false;

    // GPU (optional)
    knst_gpu_usage gpu;
    bool uses_gpu() const noexcept { return gpu.valid; }

    // Partial data flags
    bool has_exe     = false;
    bool has_cmdline = false;
    bool has_cwd     = false;
    bool has_user    = false;
    bool has_memory  = false;
    bool has_cpu     = false;
    bool has_threads = false;
};
```

### Usage

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

### Process States

| State | Meaning |
|---|---|
| `Running` | Running |
| `Sleeping` | Sleeping (interruptible) |
| `DiskSleep` | Waiting for disk I/O |
| `Zombie` | Dead but not reaped by parent |
| `Stopped` | Paused |
| `Tracing` | Traced by debugger |
| `Dead` | Dead |
| `Idle` | Idle |
| `Waiting` | Waiting |

### Static Accessors

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

## 🧵 9) Thread Information

```cpp
knst_vector<knst_thread_info> threads = knst_process::get_threads(pid);

for (const auto& t : threads) {
    std::cout << "TID: " << t.tid << "  Name: " << t.name << "\n";
}

// Just TID list
knst_vector<uint32_t> tids = knst_process::get_thread_ids(pid);
```

**Linux:** `/proc/[pid]/task/`
**Windows:** `CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD)`

---

## 🔗 10) File Descriptor Information (POSIX)

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

**Example output:**

```
FD 0 -> /dev/pts/2 (file)
FD 1 -> /dev/pts/2 (file)
FD 2 -> /dev/pts/2 (file)
FD 3 -> socket:[12345] (socket)
FD 5 -> /home/user/data.txt (file)
```

**Note:** Only supported on Linux.

---

## 🌍 11) Environment Variables (POSIX)

```cpp
knst_vector<knst_env_var> env = knst_process::get_env(pid);

for (const auto& e : env) {
    std::cout << e.name << "=" << e.value << "\n";
}
```

**Source:** `/proc/[pid]/environ`

---

## 🛠️ 12) `knst_process_options`

Spawn options — every field has a sensible default.

### Working Directory and Environment

```cpp
knst_process_options o;
o.cwd = u"/home/user/project";

// Add environment variables
knst_env_var env;
env.name  = u"MY_VAR";
env.value = u"value";
o.env.push_back(env);

o.inherit_env = true;   // use parent env
```

### Stdio Control

```cpp
o.stdin_mode  = knst_stdio_mode::Inherit;  // Pipe, Null, File
o.stdout_mode = knst_stdio_mode::Pipe;
o.stderr_mode = knst_stdio_mode::Pipe;

// Redirect to file
o.stdout_file = u"/tmp/out.log";
o.stderr_file = u"/tmp/err.log";

// Convenience flags
o.capture_stdout = true;
o.capture_stderr = true;
o.inherit_stdin  = true;
```

### Window and Session

```cpp
o.window_mode = knst_window_mode::Hidden;   // Hidden, Minimized, Maximized, Normal
o.new_session        = true;   // POSIX: setsid()
o.new_console        = true;   // Windows: CREATE_NEW_CONSOLE
o.new_process_group  = true;
o.detached           = true;
```

### Shell Selection

```cpp
o.shell = knst_shell_type::Bash;
o.shell_path = u"/usr/bin/bash";   // For Custom
```

### Priority and Affinity

```cpp
o.nice_value    = 10;    // POSIX nice
o.priority      = 10;    // Windows priority class
o.affinity_mask = 0b0011;  // first 2 cores
```

### Other

```cpp
o.suspend_after_spawn = true;   // Start with SIGSTOP/CREATE_SUSPENDED
o.close_extra_fds     = true;
o.win_unicode         = true;
```

---

## 🎮 13) GPU Monitoring

### `knst_gpu` API

```cpp
knst_gpu::has_gpu();                    // Is a GPU present?
knst_gpu::count();                      // How many GPUs?
knst_gpu::nvml_available();             // Is NVML loaded?

knst_c16string drv  = knst_gpu::nvidia_driver_version();
knst_c16string cuda = knst_gpu::cuda_version();

auto gpus = knst_gpu::list_all();
auto procs = knst_gpu::get_processes();
auto procs0 = knst_gpu::get_processes(0);   // Only GPU 0
```

### `knst_gpu_info` Structure

```cpp
struct knst_gpu_info {
    uint32_t index;
    knst_c16string name;            // "NVIDIA GeForce RTX 4090"
    knst_c16string vendor_name;     // "NVIDIA"
    knst_c16string driver_version;
    knst_c16string uuid;
    knst_c16string vbios_version;
    knst_gpu_vendor vendor;         // NVIDIA, AMD, Intel, Apple, Other

    // Memory
    uint64_t memory_total;
    uint64_t memory_used;
    uint64_t memory_free;

    // Utilization
    uint32_t utilization_gpu;       // %
    uint32_t utilization_mem;       // %

    // Temperature
    int32_t  temperature_core;      // °C

    // Power
    uint32_t power_usage_mw;        // mW
    uint32_t power_limit_mw;

    // Clocks
    uint32_t clock_core_mhz;
    uint32_t clock_mem_mhz;

    // Fan
    uint32_t fan_percent;           // %

    // PCIe
    uint32_t pcie_gen;
    uint32_t pcie_width;

    // Flags
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

### Usage

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
        std::cout << "    Power: " << g.power_usage_mw / 1000 << " W\n";
    }
}
```

### GPU Processes

```cpp
auto procs = knst_gpu::get_processes();

for (const auto& p : procs) {
    std::cout << "PID " << p.pid 
              << "  GPU" << p.gpu_index
              << "  VRAM=" << p.vram_bytes / (1024*1024) << " MB"
              << "  " << p.name << "\n";
}
```

**Output:**

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
- **NVIDIA:** NVML (nvml.dll) — full data
- **All vendors:** DXGI (`IDXGIFactory1` + `IDXGIAdapter3`) — name, VRAM total/used
- **GPU utilization %:** PDH (`\GPU Engine(*)\Utilization Percentage`) — LUID-based
- **Fallback:** WMI `Win32_VideoController` — last resort for old drivers

### Process + GPU Info

```cpp
// list_all(true) → includes GPU info
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

## 🌐 14) System Information

```cpp
knst_process::system_info si = knst_process::get_system_info();

std::cout << "CPU count  : " << si.cpu_count << "\n";
std::cout << "Page size  : " << si.page_size << " bytes\n";
std::cout << "Total RAM  : " << si.total_memory / (1024*1024*1024) << " GB\n";
std::cout << "Free RAM   : " << si.free_memory / (1024*1024*1024) << " GB\n";
std::cout << "Uptime     : " << si.uptime_ms / 1000 / 3600 << " hours\n";
```

### Active Process Info

```cpp
uint32_t pid   = knst_process::current_pid();
uint32_t ppid  = knst_process::parent_pid();
uint32_t uid   = knst_process::current_uid();
knst_c16string user = knst_process::current_user();
knst_c16string exe  = knst_process::current_exe_path();
```

---

## 🛡️ 15) Error Handling

### `knst_process_error` Enum

```cpp
enum class knst_process_error : uint8_t {
    None = 0,
    NotFound,             // executable not found
    PermissionDenied,     // permission denied
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

### Error String

```cpp
knst_process p = knst_process::run(u"/nonexistent");
if (p.error != knst_process_error::None) {
    std::cerr << "Error: " << p.error_string() << "\n";
    // "Executable not found"
}
```

### Native Error Code

```cpp
int32_t native = p.native_error_code();
// POSIX: errno
// Windows: GetLastError()
```

---

## 🌍 16) Platform Differences

| Topic | Linux (POSIX) | Windows |
|---|---|---|
| **Spawn** | `fork()` + `execve()` | `CreateProcessW()` |
| **Shell** | `/bin/sh` | `cmd.exe` |
| **Signal** | Full support (SIGTERM, SIGKILL, ...) | Kill only |
| **Suspend** | `SIGSTOP` | `NtSuspendProcess` |
| **Resume** | `SIGCONT` | `NtResumeProcess` |
| **Priority** | nice (-20..19) | Priority class |
| **Affinity** | `sched_setaffinity` | `SetProcessAffinityMask` |
| **Process list** | `/proc` scan | `CreateToolhelp32Snapshot` |
| **Cmdline** | `/proc/[pid]/cmdline` | PEB read |
| **Threads** | `/proc/[pid]/task` | `TH32CS_SNAPTHREAD` |
| **FDs** | `/proc/[pid]/fd` | None |
| **Environment** | `/proc/[pid]/environ` | None |
| **GPU (NVIDIA)** | NVML | NVML |
| **GPU (AMD)** | sysfs (full) | DXGI (VRAM+name) + PDH (usage %) |
| **GPU (Intel)** | sysfs i915 | DXGI + PDH |
| **Uptime** | `/proc/uptime` | `GetTickCount64` |

---

## 🔥 17) Real-World Examples

### Example 1: Simple Command Execution

```cpp
void run_and_print(const knst_c16string& cmd) {
    auto r = knst_process::run_capture(
        u"/bin/sh", { u"-c", cmd }, {}, nullptr, 0, 5000);

    if (r.error != knst_process_error::None) {
        std::cerr << "Error: " << knst_process_error_string(r.error) << "\n";
        return;
    }
    std::cout << r.out_data << "\n";
}

run_and_print(u"df -h");
```

### Example 2: Build with Timeout

```cpp
bool run_build_with_timeout(const knst_c16string& cmd, uint32_t secs) {
    knst_process p = knst_process::run_shell(cmd);
    auto r = p.communicate(nullptr, 0, secs * 1000);

    if (r.timed_out) {
        std::cerr << "Build did not finish in " << secs << " seconds, killed\n";
        return false;
    }
    if (r.exit_code != 0) {
        std::cerr << "Build error:\n" << r.err_data << "\n";
        return false;
    }
    return true;
}
```

### Example 3: Process Monitor Tool

```cpp
void monitor_process(const knst_c16string& name) {
    auto found = knst_process::find_by_name(name, false);

    if (found.empty()) {
        std::cout << "Not found: " << name << "\n";
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

### Example 4: GPU Monitoring

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
            std::cout << "    Power : " << g.power_usage_mw / 1000 << " W\n";
        }
    }
}
```

### Example 5: Kill Child Processes

```cpp
void cleanup_children(uint32_t parent_pid) {
    auto kids = knst_process::children_of(parent_pid);

    for (uint32_t pid : kids) {
        std::cout << "Killing: " << pid 
                  << " (" << knst_process::get_info(pid).name << ")\n";
        knst_process::terminate_graceful(pid, 2000);
    }
}
```

### Example 6: Data Streaming with Pipes

```cpp
void process_text_lines() {
    knst_process p = knst_process::run(u"/bin/grep", { u"error" });

    // Write data to stdin (in a parallel thread)
    std::thread writer([&p]() {
        p.write_stdin("line 1: info\n", 13);
        p.write_stdin("line 2: error\n", 14);
        p.write_stdin("line 3: info\n", 13);
        p.close_stdin();
    });

    // Read line by line
    knst_c16string line;
    while (p.read_line(line)) {
        std::cout << "Match: " << line << "\n";
    }

    writer.join();
    p.wait();
}
```

### Example 7: Suspend/Resume in Batch

```cpp
void pause_and_resume(uint32_t pid) {
    std::cout << "Pausing: " << pid << "\n";
    knst_process::suspend_pid(pid);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "Resuming: " << pid << "\n";
    knst_process::resume_pid(pid);
}
```

---

## 📊 18) Performance Notes

| Operation | Complexity | Note |
|---|---|---|
| `run` | O(1) | fork + exec |
| `communicate` | O(n) | Full output in memory |
| `wait` | O(1) | waitpid |
| `list_pids` | O(num pids) | `/proc` scan |
| `list_all` | O(num pids × files) | ~5 files per PID |
| `find_by_name` | O(num pids) | via `list_all` |
| `get_info` | O(files) | Reads `/proc/[pid]/*` |
| `children_of` | O(num pids) | Reads every `/proc/*/stat` |
| `knst_gpu::list_all` | O(num gpus) | NVML/sysfs |

### When Is It Slow?

- **`list_all`** — can take 1-2 seconds with 300+ processes
- **`descendants_of`** — recursive; slow with a big process tree
- **`get_env`** — slow if `/proc/[pid]/environ` is large
- **GPU cache** — the first call loads NVML + does WMI queries (slow); subsequent calls use cache

### Optimization Tips

```cpp
// ❌ Slow: scans all processes every time
for (int i = 0; i < 100; ++i) {
    auto all = knst_process::list_all();
}

// ✅ Fast: scan once, keep in memory
auto all = knst_process::list_all();
for (int i = 0; i < 100; ++i) {
    // work with `all`
}
```

---

## 🎯 What It Buys You

`knst_process` provides serious advantage in these situations:

- ✅ **Build / CI systems** — run commands, capture output, apply timeouts
- ✅ **System monitoring tools** — process list, threads, memory
- ✅ **GPU telemetry** — mining, gaming, render monitoring
- ✅ **Automation scripts** — write in C++ instead of Python/Node.js
- ✅ **Cross-platform tooling** — same code on Windows and Linux
- ✅ **Safe command execution** — pass arguments without shell injection