# knst_process — Cross-Platform Process Management

Hey! 👋 This document explains what the `knst_process` class is, how to use it, and why it's in KernelNucleusT.

**In short:** A **cross-platform process management + GPU monitoring library.** Using the same API on Linux and Windows, you can launch programs, capture output, write to stdin, apply timeouts, list PIDs, read thread/FD/env info, and monitor **NVIDIA/AMD/Intel GPUs**. On Android, sensor/device mode is limited but detection works.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **Cross-platform** | Same API on Linux + Windows + Android |
| **Program launch** | `run`, `run_shell`, `run_capture`, `run_detached` |
| **Output capture** | stdout + stderr in parallel, binary-safe |
| **stdin piping** | Write data to child process (threaded, non-blocking) |
| **Timeout** | Auto-kill if not done in given time |
| **Detached process** | POSIX: double-fork, Windows: `DETACHED_PROCESS` |
| **Process listing** | PID list, name search, children, descendants |
| **Process inspection** | RSS/VSZ, CPU time, thread count, FD count |
| **Thread / FD / Env** | Works on both platforms (FD: handle enumeration, Env: PEB) |
| **Suspend / Resume** | Pause and resume processes |
| **Priority / Affinity** | nice value and CPU core pinning |
| **GPU monitoring** | NVIDIA (NVML), AMD/Intel (Linux: sysfs / Windows: DXGI+PDH) |
| **GPU process list** | Which process uses which GPU, VRAM |
| **Shell selection** | sh, bash, zsh, fish, cmd, powershell, pwsh |
| **UTF-16 friendly** | `knst_c16string` for Unicode paths |
| **Suspend after spawn** | Launch + immediately pause (for debuggers) |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    // Run a shell command and capture output
    knst_process p = knst_process::run_shell(u"echo hello");

    knst_process_result r = p.communicate(nullptr, 0, 5000);

    std::cout << "Output  : " << r.out_data << "\n";
    std::cout << "Exit    : " << r.exit_code << "\n";
    return 0;
}
```

---

## 📋 General API

| Category | Methods |
|---|---|
| **Program launch** | `run`, `run_shell`, `run_with_shell`, `run_detached`, `run_capture` |
| **Communication** | `communicate`, `read_stdout`, `read_stderr`, `write_stdin`, `close_stdin` |
| **Line reading** | `read_line`, `try_read_stdout`, `try_read_stderr` |
| **Waiting** | `wait`, `wait_or_kill`, `is_running`, `exit_code` |
| **Kill / Signal** | `kill`, `force_kill`, `kill_tree`, `send_signal` |
| **Suspend / Resume** | `suspend`, `resume` |
| **Priority / Affinity** | `set_priority`, `set_affinity` |
| **PID list** | `list_pids`, `list_all`, `find_by_name`, `children_of`, `descendants_of` |
| **Check** | `exists`, `is_alive`, `is_zombie` |
| **Info** | `get_info`, `get_exe_path`, `get_cmdline`, `get_cwd`, `get_user` |
| **Memory / CPU** | `get_rss_bytes`, `get_vsz_bytes`, `get_cpu_times` |
| **Thread / FD / Env** | `get_threads`, `get_fds`, `get_env` |
| **Kill helpers** | `kill_by_pid`, `kill_by_name`, `kill_tree`, `kill_children`, `terminate_graceful` |
| **System** | `current_pid`, `parent_pid`, `current_user`, `get_system_info` |
| **GPU** | `knst_gpu::list_all`, `get_processes`, `nvidia_driver_version` |

---

## 🎯 1) Program Launch

### `run` — Direct Execution (No Shell)

Invokes the program directly, no shell in between:

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

**Advantage:** No shell injection risk. Args go directly to `execve`/`CreateProcessW`.

**POSIX exec path:** If program has no `/`, it's searched via `PATH` (`ExecvpeCompat`). Otherwise direct `execve`.

### `run_shell` — Shell Command

Uses the platform's default shell:

```cpp
// POSIX:   /bin/sh -c "..."
// Windows: cmd.exe /c "..."
knst_process p = knst_process::run_shell(u"ls -la | grep .txt");
auto r = p.communicate(nullptr, 0, 5000);
```

### `run_with_shell` — Choose Shell Type

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

**Android special:** `Sh`, `Bash`, `Zsh`, `Fish` all map to `/system/bin/sh`.

**Windows default (`Auto`):** `cmd.exe /c`.

### `run_capture` — Run + Capture in One Call

Most practical API — launches, waits, returns output:

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

**Inside:** `run()` + `communicate()`.

### `run_detached` — Fire-and-Forget

Runs in background, parent doesn't wait:

```cpp
knst_process p = knst_process::run_detached(
    u"/usr/bin/notify-send",
    { u"Title", u"Message" });

std::cout << "PID: " << p.pid() << "\n";
```

**POSIX:** Double-fork + `setsid()` + `/dev/null` stdio. Real PID returned via pipe to parent.

**Windows:** `DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB`. If job access denied, retries without `CREATE_BREAKAWAY_FROM_JOB`.

---

## 💬 2) Communication — `communicate()`

Most powerful API — writes to stdin, reads stdout+stderr, applies timeout:

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

### POSIX vs Windows Behavior

**POSIX:** stdin/stdout/stderr multiplexed in one `poll()` loop. Efficient, single thread.

**Windows:** stdin writer runs in a separate `knst_thread` (parallel write), main thread runs `PeekNamedPipe` + `ReadFile` loop for output. Writer thread joined at end of `communicate()`.

### Timeout Behavior

If timeout **exceeded**, process is auto-killed and a **500 ms drain** is performed:

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"5" });
auto r = p.communicate(nullptr, 0, 300);   // 300 ms timeout

// After timeout:
//   - force_kill() called
//   - wait(0) blocked on
//   - extra 500 ms drain to empty pipes
std::cout << "timed_out: " << (r.timed_out ? "yes" : "no") << "\n";
```

### Alternative Read Methods

```cpp
// Read stdout
knst_byte_string data = p.read_stdout();
knst_c16string text = p.read_stdout_text();   // converts to UTF-16, trims trailing newline

// Read stderr
knst_byte_string err = p.read_stderr();

// Read line by line
knst_c16string line;
while (p.read_line(line)) {
    std::cout << "> " << line << "\n";
}

// Read with timeout (non-blocking)
knst_byte_string chunk;
if (p.try_read_stdout(100, chunk)) {
    std::cout << "Received: " << chunk.length() << " bytes\n";
}

// Write to stdin
p.write_stdin("data\n", 6);
p.write_stdin(knst_c16string(u"UTF-16 text\n"));   // auto-converted to UTF-8
p.close_stdin();   // EOF signal
```

> ⚠️ **`read_line` is not non-blocking.** Blocks if no line arrives. Use `try_read_stdout` to avoid blocking.

---

## ⏱️ 3) Timeout and Waiting

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"1" });

// Wait (no timeout — blocks until process ends)
p.wait();

// Wait 500 ms
if (p.wait(500)) {
    std::cout << "Finished within 1 second\n";
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

**Detached process:** `is_running()` and `wait()` check via `kill(pid, 0)` (child can't be reaped).

---

## 🔪 4) Kill and Signal

### Instance Methods

```cpp
knst_process p = knst_process::run(u"/bin/sleep", { u"100" });

// SIGTERM (gentle)
p.kill();

// SIGKILL (forced)
p.force_kill();

// Kill with children
p.kill_tree();

// Send specific signal
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

// Graceful — SIGTERM first, SIGKILL on timeout
knst_process::terminate_graceful(12345, 3000);
```

### Kill Signals (POSIX)

| Signal | Value | Meaning |
|---|---|---|
| `SIGTERM` | 15 | Graceful termination (default) |
| `SIGKILL` | 9 | Forced kill (uncatchable) |
| `SIGINT` | 2 | Like Ctrl+C |
| `SIGSTOP` | 19 | Pause (uncatchable) |
| `SIGCONT` | 18 | Resume |

**Windows behavior:**
- **`kill(sig)`:** `TerminateProcess(h, sig)` — no signal concept
- **`send_signal(15)`** on detached process → `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT)`
- **`kill_tree()`:** `CreateJobObject` + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` + `TerminateJobObject` — catches children
- If job can't be assigned → fallback to `TerminateProcess`

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

**Windows:** `NtSuspendProcess` / `NtResumeProcess` (loaded at runtime from `ntdll.dll`). `resume()` tries both `ResumeThread` (main thread) and `NtResumeProcess`.

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

**Note:** `knst_process_options` has both `nice_value` and `priority`. `nice_value` takes precedence — used if non-zero, else falls back to `priority`.

### Affinity (CPU Core Pinning)

```cpp
// Run only on cores 0 and 2
p.set_affinity(0b0101);   // bit 0 and bit 2

// Static
knst_process::set_affinity(12345, 0xFF);   // first 8 cores
```

**Linux:** `sched_setaffinity()` — 64-bit mask supported.

**Windows:** `SetProcessAffinityMask()`.

---

## 📋 7) Process Listing

### All PIDs

```cpp
knst_vector<uint32_t> pids = knst_process::list_pids();
std::cout << "Total " << pids.size() << " processes\n";
```

**POSIX:** `/proc/*` scan (numeric directories).

**Windows:** `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)`.

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

Both `name` (process name) and `exe_path` basename are checked.

### Children and Descendants

```cpp
// Direct children
knst_vector<uint32_t> kids = knst_process::children_of(12345);

// All descendants (recursive)
knst_vector<knst_process_info> descendants = 
    knst_process::descendants_of(12345);
```

**POSIX:** `/proc/[pid]/stat` read per PID (to extract ppid).

**Windows:** `CreateToolhelp32Snapshot` + `pe.th32ParentProcessID`.

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

    // Text info
    knst_c16string name;
    knst_c16string exe_path;
    knst_c16string cmdline;
    knst_c16string cwd;
    knst_c16string user;
    uint32_t uid = 0;
    uint32_t gid = 0;

    // Memory
    uint64_t rss_bytes    = 0;
    uint64_t vsz_bytes    = 0;
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
| `DiskSleep` | Waiting on disk I/O |
| `Zombie` | Dead but not reaped by parent |
| `Stopped` | Paused |
| `Tracing` | Traced by debugger |
| `Dead` | Dead |
| `Idle` | Idle (kernel thread) |
| `Waiting` | Waiting (Windows WrWaiting) |

**POSIX:** Read from `/proc/[pid]/stat` — `R`, `S`, `D`, `Z`, `T`, `t`, `X`, `I`, `W`.

**Windows:** Synthesized from thread states via `NtQuerySystemInformation(SystemProcessInformation)`:
- All threads terminated → `Dead`
- Any thread running → `Running`
- All suspended → `Stopped`
- Any disk wait → `DiskSleep`
- Any wait → `Sleeping`

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

### Platform Behaviors

**POSIX `get_info`:** `/proc/[pid]/stat` + `/proc/[pid]/status` + `readlink("/proc/[pid]/exe")` + `readlink("/proc/[pid]/cwd")` + `/proc/[pid]/cmdline` + `/proc/[pid]/fd` count.

**Android `get_info`:** `exe`, `cmdline`, `cwd` **only read for self (own PID)**. For other processes, these fields stay empty (SELinux blocks).

**Windows `get_info`:**
- `exe_path` → `QueryFullProcessImageNameW`
- `cmdline` → PEB read (WOW64 support — PEB32 read for 32-bit processes)
- `cwd` → PEB read (CurrentDirectory.DosPath)
- `user` → `OpenProcessToken` + `LookupAccountSid`
- `uid` → FNV-1a hash of the SID (no POSIX uid equivalent)
- `nice` → `GetPriorityClass` converted to nice
- `thread_count` → `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)`
- `state` → see above
- `is_zombie` → **always `false`** (no zombie concept on Windows)

### `exists`, `is_alive`, `is_zombie`

```cpp
bool e = knst_process::exists(12345);    // exists in any state?
bool a = knst_process::is_alive(12345);  // not zombie/dead?
bool z = knst_process::is_zombie(12345); // zombie only?
```

- **POSIX `exists`:** Does `/proc/[pid]` exist?
- **Windows `exists`:** Tries `OpenProcess`, falls back to snapshot scan
- **`is_zombie`:** POSIX state check, Windows always `false`

---

## 🧵 9) Thread Info

```cpp
knst_vector<knst_thread_info> threads = knst_process::get_threads(pid);

for (const auto& t : threads) {
    std::cout << "TID: " << t.tid << "  Name: " << t.name << "\n";
}

// TID list only
knst_vector<uint32_t> tids = knst_process::get_thread_ids(pid);
```

**`knst_thread_info`:**

```cpp
struct knst_thread_info {
    uint32_t tid = 0;
    knst_c16string name;                    // populated only on POSIX
    knst_process_state state = knst_process_state::Unknown;   // (currently not filled)
    uint64_t cpu_user_ms = 0;
    uint64_t cpu_system_ms = 0;
};
```

**POSIX:** `/proc/[pid]/task/*` + `/proc/[pid]/task/[tid]/comm` → name populated.

**Windows:** `TH32CS_SNAPTHREAD` → TID list only. **Thread name stays empty** (needs extra API on Windows).

---

## 🔗 10) File Descriptor Info

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

**`knst_fd_info`:**

```cpp
struct knst_fd_info {
    int32_t fd = -1;
    knst_c16string target;
    bool is_socket = false;
    bool is_pipe = false;
    bool is_file = false;
};
```

**POSIX:** `/proc/[pid]/fd/*` — target read via `readlink`. Distinguished by `socket:`, `pipe:`, `/` prefixes.

**Windows:** `NtQuerySystemInformation(SystemExtendedHandleInformation)` + `NtQueryObject` + `DuplicateHandle` + `GetFinalPathNameByHandleW`. Only **`File`** and **`Pipe`** types returned — other handles (event, mutex, section) skipped.

> ⚠️ **"fd" on Windows is different.** Handle number reported as `fd`, not a POSIX FD number.

---

## 🌍 11) Environment Variables

```cpp
knst_vector<knst_env_var> env = knst_process::get_env(pid);

for (const auto& e : env) {
    std::cout << e.name << "=" << e.value << "\n";
}
```

**`knst_env_var`:**

```cpp
struct knst_env_var {
    knst_c16string name;
    knst_c16string value;
};
```

**POSIX:** `/proc/[pid]/environ` — read with `\0` separator.

**Windows:** PEB read (`ProcessParameters.Environment`). WOW64 supported. Has `kMaxEnv = 256 KB` limit; for larger, chunk increments from 32 KB to 256 KB are tried.

---

## 🛠️ 12) `knst_process_options`

Spawn options — each field has a reasonable default.

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

**`inherit_env` + `env` combined:** If `inherit_env=true`, entries in `env` are **added** to parent env (overriding same-name vars). If `false`, only `env` is used.

### Stdio Control

```cpp
o.stdin_mode  = knst_stdio_mode::Inherit;  // Pipe, Null, File
o.stdout_mode = knst_stdio_mode::Pipe;
o.stderr_mode = knst_stdio_mode::Pipe;

// Redirect to file
o.stdout_file = u"/tmp/out.log";
o.stderr_file = u"/tmp/err.log";

// Convenience flags (override modes)
o.capture_stdout = true;
o.capture_stderr = true;
o.inherit_stdin  = true;
```

**`EffectiveMode` logic:**
- `File` or `Null` explicit mode → use it
- `capture_*=true` → `Pipe`
- `capture_*=false` → `Inherit`

### Window and Session

```cpp
o.window_mode = knst_window_mode::Hidden;   // Hidden, Minimized, Maximized, Normal
o.new_session        = true;   // POSIX: setsid()
o.new_console        = true;   // Windows: CREATE_NEW_CONSOLE
o.new_process_group  = true;   // POSIX: setpgid(0,0) / Windows: CREATE_NEW_PROCESS_GROUP
o.detached           = true;   // POSIX: double-fork / Windows: DETACHED_PROCESS
```

### Shell Selection

```cpp
o.shell = knst_shell_type::Bash;
o.shell_path = u"/usr/bin/bash";   // for Custom
```

### Priority and Affinity

```cpp
o.nice_value    = 10;    // POSIX nice (takes precedence)
o.priority      = 10;    // used if nice_value=0
o.affinity_mask = 0b0011;  // first 2 cores
```

### Other

```cpp
o.suspend_after_spawn = true;   // start with SIGSTOP/CREATE_SUSPENDED
o.close_extra_fds     = true;
o.win_unicode         = true;
```

**What is `suspend_after_spawn`?** Process launched with `SIGSTOP`/`CREATE_SUSPENDED` → immediately suspended. Useful for attaching debuggers or doing early operations like `set_affinity`. Resumed via `resume()`.

---

## 🎮 13) GPU Monitoring

### `knst_gpu` API

```cpp
knst_gpu::has_gpu();                    // any GPU present?
knst_gpu::count();                      // how many GPUs?
knst_gpu::nvml_available();             // NVML loaded?

knst_c16string drv  = knst_gpu::nvidia_driver_version();
knst_c16string cuda = knst_gpu::cuda_version();

auto gpus = knst_gpu::list_all();
auto procs = knst_gpu::get_processes();
auto procs0 = knst_gpu::get_processes(0);   // GPU 0 only
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

    uint64_t memory_total;
    uint64_t memory_used;
    uint64_t memory_free;

    uint32_t utilization_gpu;       // %
    uint32_t utilization_mem;       // %

    int32_t  temperature_core;      // °C

    uint32_t power_usage_mw;        // mW
    uint32_t power_limit_mw;

    uint32_t clock_core_mhz;
    uint32_t clock_mem_mhz;

    uint32_t fan_percent;           // %

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

**Sources:**
- **NVIDIA:** NVML `nvmlDeviceGetComputeRunningProcesses_v2` + `nvmlDeviceGetGraphicsRunningProcesses_v2` (MIG support). Falls back to v1 if v2 unavailable.
- **AMD:** `/proc/[pid]/fdinfo/*` accumulates `drm-memory-vram:` lines. Multiple fds from same PID summed.

### GPU Vendor Detection

**Linux:**
- **NVIDIA:** NVML (libnvidia-ml.so)
- **AMD:** `/sys/class/drm/card*/device/mem_info_vram_*`, `gpu_busy_percent`, `pp_dpm_sclk`, `hwmon/temp1_input`, `power1_average`
- **Intel:** `/sys/class/drm/card*/device/vendor` (0x8086), `gt_act_freq_mhz`, `gt_cur_freq_mhz`, `gt_busy_percent`

**Windows:**
- **NVIDIA:** NVML (nvml.dll) — full data
- **All brands:** DXGI (`IDXGIFactory1` + `IDXGIAdapter3`) — name, VRAM total/used (QueryVideoMemoryInfo)
- **GPU utilization %:** PDH (`\GPU Engine(*)\Utilization Percentage`) — summed by LUID
- **Fallback:** WMI `Win32_VideoController` — last resort for old drivers (cached)

**Android:**
- **Qualcomm Adreno:** `/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage`, `gpuclk`
- **ARM Mali:** `/sys/class/misc/mali0/device/utilisation`, `clock`

### GPU Cache Details

- **PDH cache:** 2 seconds (thread_local). So `list_all()` doesn't PDH-query every call.
- **WMI cache:** `std::once_flag` — one query per program.
- **NVML cache:** `std::once_flag` — `nvmlInit` once.

### NVML Sentinel Values

Some drivers return **`0xFFFFFFFFFFFF` and above as sentinel** when VRAM info unavailable. Code auto-clamps these to **0** (marked as unknown):

```cpp
constexpr uint64_t NVML_SENTINEL_MIN = 0xFFFFFFFFFFFFULL;
auto safe_vram = [](uint64_t raw) -> uint64_t {
    return (raw >= NVML_SENTINEL_MIN) ? 0ULL : raw;
};
```

### Duplicate Filtering

If **NVIDIA GPUs** already came from NVML, same-named GPUs in sysfs/DXGI/WMI lists are **not added again** (`has_nvidia_from_nvml` flag).

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

## 🌐 14) System Info

```cpp
knst_process::system_info si = knst_process::get_system_info();

std::cout << "CPU count   : " << si.cpu_count << "\n";
std::cout << "Page size   : " << si.page_size << " bytes\n";
std::cout << "RAM total   : " << si.total_memory / (1024*1024*1024) << " GB\n";
std::cout << "RAM free    : " << si.free_memory / (1024*1024*1024) << " GB\n";
std::cout << "Uptime      : " << si.uptime_ms / 1000 / 3600 << " hours\n";
```

**POSIX:** `/proc/meminfo` + `/proc/uptime` (falls back to `CLOCK_BOOTTIME` if uptime unavailable on Android).

**Windows:** `GlobalMemoryStatusEx` + `GetSystemInfo` + `GetTickCount64`.

### Current Process Info

```cpp
uint32_t pid   = knst_process::current_pid();
uint32_t ppid  = knst_process::parent_pid();
uint32_t uid   = knst_process::current_uid();
knst_c16string user = knst_process::current_user();
knst_c16string exe  = knst_process::current_exe_path();
```

**Windows note:** `current_uid()` **always returns 0** — no POSIX uid concept. `current_user()` uses `GetUserNameW`.

---

## 🛡️ 15) Error Handling

### `knst_process_error` Enum (23 values)

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

### POSIX errno → process_error mapping

| errno | process_error |
|---|---|
| `ENOENT`, `ENOTDIR` | `NotFound` |
| `EACCES`, `EPERM` | `PermissionDenied` |
| `ENOMEM` | `OutOfMemory` |
| `EINVAL` | `InvalidArgument` |
| `ESRCH` | `NoSuchProcess` |
| Others | `ExecFailed` |

### Windows GetLastError → process_error mapping

| GetLastError | process_error |
|---|---|
| `ERROR_FILE_NOT_FOUND`, `ERROR_PATH_NOT_FOUND` | `NotFound` |
| `ERROR_ACCESS_DENIED` | `PermissionDenied` |
| Others | `ExecFailed` |

---

## 🌍 16) Platform Differences

| Concern | Linux (POSIX) | Windows | Android |
|---|---|---|---|
| **Spawn** | `fork()` + `execve()` | `CreateProcessW()` | `fork()` + `execve()` |
| **Shell** | `/bin/sh` | `cmd.exe` | `/system/bin/sh` |
| **Signal** | Full (SIGTERM, SIGKILL, ...) | `TerminateProcess` only | Full |
| **Suspend** | `SIGSTOP` | `NtSuspendProcess` | `SIGSTOP` |
| **Resume** | `SIGCONT` | `NtResumeProcess` | `SIGCONT` |
| **Priority** | nice (-20..19) | Priority class | nice |
| **Affinity** | `sched_setaffinity` (64-bit) | `SetProcessAffinityMask` | `sched_setaffinity` |
| **Process list** | `/proc` scan | `CreateToolhelp32Snapshot` | `/proc` scan |
| **Cmdline** | `/proc/[pid]/cmdline` | PEB read (WOW64) | **Self only** |
| **CWD** | `/proc/[pid]/cwd` | PEB read (WOW64) | **Self only** |
| **Exe path** | `/proc/[pid]/exe` | `QueryFullProcessImageNameW` | **Self only** |
| **Threads** | `/proc/[pid]/task` + name | `TH32CS_SNAPTHREAD` (no name) | `/proc/[pid]/task` |
| **FDs** | `/proc/[pid]/fd` | Handle enumeration (File+Pipe) | `/proc/[pid]/fd` |
| **Environment** | `/proc/[pid]/environ` | PEB read | `/proc/[pid]/environ` |
| **`is_zombie`** | state check | **always false** | state check |
| **`current_uid`** | `getuid()` | **always 0** | `getuid()` |
| **GPU (NVIDIA)** | NVML | NVML | — |
| **GPU (AMD)** | sysfs (full) | DXGI (VRAM+name) + PDH (%) | — |
| **GPU (Intel)** | sysfs i915 | DXGI + PDH | — |
| **GPU (Adreno)** | — | — | kgsl sysfs |
| **GPU (Mali)** | — | — | mali0 sysfs |
| **Uptime** | `/proc/uptime` | `GetTickCount64` | `/proc/uptime` or `CLOCK_BOOTTIME` |

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
        std::cerr << "Build didn't finish in " << secs << "s, killed\n";
        return false;
    }
    if (r.exit_code != 0) {
        std::cerr << "Build error:\n" << r.err_data << "\n";
        return false;
    }
    return true;
}
```

### Example 3: Process Monitor

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

### Example 5: Kill Child Process

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

### Example 6: Pipe Data Flow

```cpp
void process_text_lines() {
    knst_process p = knst_process::run(u"/bin/grep", { u"error" });

    // Write to stdin (in parallel thread)
    std::thread writer([&p]() {
        p.write_stdin("line 1: info\n", 14);
        p.write_stdin("line 2: error\n", 15);
        p.write_stdin("line 3: info\n", 14);
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

### Example 7: Suspend/Resume for Batch

```cpp
void pause_and_resume(uint32_t pid) {
    std::cout << "Pause: " << pid << "\n";
    knst_process::suspend_pid(pid);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "Resume: " << pid << "\n";
    knst_process::resume_pid(pid);
}
```

### Example 8: Suspend-After-Spawn (for Debugger)

```cpp
void attach_debugger(const knst_c16string& program) {
    knst_process_options o;
    o.suspend_after_spawn = true;

    knst_process p = knst_process::run(program, {}, o);
    if (p.error != knst_process_error::None) return;

    std::cout << "PID " << p.pid() << " suspended, debugger can attach\n";

    // Attach debugger...
    // std::this_thread::sleep_for(std::chrono::seconds(5));

    p.resume();
}
```

---

## 📊 18) Performance Notes

| Operation | Complexity | Note |
|---|---|---|
| `run` | O(1) | fork + exec (~1-5 ms) |
| `run_shell` | O(1) | Extra shell spawn cost |
| `communicate` | O(n) | All output in memory |
| `wait` | O(1) | waitpid |
| `list_pids` | O(pid count) | `/proc` scan |
| `list_all` | O(pid count × file) | ~5 files per PID |
| `find_by_name` | O(pid count) | Via `list_all` |
| `get_info` | O(files) | `/proc/[pid]/*` reads |
| `children_of` | O(pid count) | Each `/proc/*/stat` read |
| `get_fds` (POSIX) | O(fd count) | readlink per fd |
| `get_fds` (Win) | O(handle count) | NtQueryObject + DuplicateHandle |
| `get_env` (Win) | O(env size) | PEB read, chunk increment |
| `knst_gpu::list_all` | O(gpu count) | NVML/sysfs |
| `knst_gpu::get_processes` | O(fd × process) | fdinfo scan on POSIX |

### When is it slow?

- **`list_all`** — 300+ processes takes 1-2 seconds (~5 file reads per PID)
- **`descendants_of`** — Recursive, slow with big trees
- **`get_env`** — Slow if `/proc/[pid]/environ` is large
- **`get_fds`** (Windows) — With hundreds of handles, `DuplicateHandle` + `NtQueryObject` per handle → ~100ms-1s
- **GPU cache** — First call does NVML load + WMI query + PDH 250ms sample (slow), then cached:
  - PDH: 2 seconds
  - WMI: once per program
  - NVML: once per program

### Optimization Tips

```cpp
// ❌ Slow: Rescan all processes every time
for (int i = 0; i < 100; ++i) {
    auto all = knst_process::list_all();
}

// ✅ Fast: Scan once, hold in memory
auto all = knst_process::list_all();
for (int i = 0; i < 100; ++i) {
    // work with `all`
}
```

```cpp
// ❌ Slow: List GPUs every frame (PDH cache 2s but first call slow)
for (int i = 0; i < 60; ++i) {
    auto gpus = knst_gpu::list_all();
}

// ✅ Fast: Get once, refresh occasionally
auto gpus = knst_gpu::list_all();
```

---

## ✅ 19) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Use communicate to capture output
auto r = p.communicate(stdin_data, size, 5000);

// 2. Always give timeout
p.communicate(nullptr, 0, 5000);   // 5 seconds

// 3. Use run() to avoid shell injection
knst_process::run(u"/bin/ls", { u"-la" });

// 4. Don't wait() on detached processes
auto p = knst_process::run_detached(u"notify-send", { u"hi" });
// DON'T call p.wait() — let it run

// 5. Cache list_all result
auto procs = knst_process::list_all();
// ... use across multiple operations

// 6. Don't expect non-File+Pipe handles on Windows (fds)
// (event, mutex, section handles not returned)
```

### ❌ Don't

```cpp
// 1. Don't call read_stdout() + read_stderr() separately (interleave issue)
p.read_stdout();
p.read_stderr();   // ❌ stderr may be left

// ✅ Use communicate()

// 2. Don't expect is_zombie() to be true on Windows
knst_process::is_zombie(12345);   // ❌ always false

// 3. Don't expect another process's exe_path on Android
knst_process::get_exe_path(1);   // ❌ returns empty (not self)

// 4. Don't use run_shell to avoid shell injection
knst_process::run_shell(u"rm -rf " + user_input);   // ❌ dangerous

// ✅ knst_process::run(u"rm", { u"-rf", user_input });

// 5. Don't wait with timeout on suspended process
p.suspend();
p.communicate(nullptr, 0, 1000);   // ❌ timeout (process suspended)

// 6. Don't expect every field in get_info to be filled
knst_process::get_info(pid);   // Windows: is_zombie=false, uid=SID hash
```

---

## 🎯 What It Brings You

`knst_process` gives you serious advantages in these situations:

- ✅ **Build / CI systems** — run commands, capture output, apply timeouts
- ✅ **System monitoring tools** — process list, thread, memory, FDs, env
- ✅ **GPU telemetry** — mining, gaming, render monitoring, NVIDIA/AMD/Intel
- ✅ **Automation scripts** — C++ instead of Python/Node.js
- ✅ **Cross-platform tooling** — Windows + Linux + Android same code
- ✅ **Safe command execution** — pass arguments without shell injection
- ✅ **Process tree analysis** — children, descendants, kill_tree
- ✅ **Suspend/Resume** — pause batch operations, resume them
- ✅ **Priority/Affinity** — CPU core pinning, nice adjustment
- ✅ **Debugger-friendly** — catch early with `suspend_after_spawn`