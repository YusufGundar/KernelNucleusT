// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
----------------------------
knst_process.hpp
----------------------------

    knst_process.hpp is a header-only process management library written for C++. It handles tasks such as launching programs, capturing output, terminating processes, and gathering information on threads, files, and memory.
    It also features GPU monitoring capabilities, allowing it to read data—such as GPU usage, VRAM, temperature, and power—via NVIDIA (NVML), AMD (sysfs), and Windows (WMI). It is designed to run on both Linux and Windows.

*/

#pragma once

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cerrno>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

#if KNST_USING_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
        #include <tlhelp32.h>
    #include <psapi.h>
    #include <processthreadsapi.h>
    #include <dxgi.h>
    #include <dxgi1_4.h>
    #include <pdh.h>
    #pragma comment(lib, "psapi.lib")
    #pragma comment(lib, "dxgi.lib")
    #pragma comment(lib, "pdh.lib")
    #pragma comment(lib, "advapi32.lib")
#else
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <sys/stat.h>
    #include <sys/resource.h>
    #include <sys/time.h>
    #include <sched.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <signal.h>
    #include <poll.h>
    #include <dirent.h>
    #include <pwd.h>
    #include <dlfcn.h>
    extern char** environ;
#endif


enum class knst_process_error : uint8_t {
    None = 0, NotFound, PermissionDenied, InvalidArgument, Timeout,
    PipeFailed, ForkFailed, ExecFailed, WaitFailed, AlreadyRunning,
    NotRunning, Crashed, OutOfMemory, ShellNotFound, FileRedirectFailed,
    AccessDenied, NoSuchProcess, SignalFailed, NotSupported,
    ReadFailed, ParseFailed, SystemCallFailed, Unknown
};

inline const char* knst_process_error_string(knst_process_error e) noexcept {
    switch (e) {
        case knst_process_error::None:                return "No error";
        case knst_process_error::NotFound:            return "Executable not found";
        case knst_process_error::PermissionDenied:    return "Permission denied";
        case knst_process_error::InvalidArgument:     return "Invalid argument";
        case knst_process_error::Timeout:             return "Timeout";
        case knst_process_error::PipeFailed:          return "Pipe creation failed";
        case knst_process_error::ForkFailed:          return "Fork failed";
        case knst_process_error::ExecFailed:          return "Exec failed";
        case knst_process_error::WaitFailed:          return "Wait failed";
        case knst_process_error::AlreadyRunning:      return "Process already running";
        case knst_process_error::NotRunning:          return "Process is not running";
        case knst_process_error::Crashed:             return "Process crashed";
        case knst_process_error::OutOfMemory:         return "Out of memory";
        case knst_process_error::ShellNotFound:       return "Shell executable not found";
        case knst_process_error::FileRedirectFailed:  return "stdio file redirect failed";
        case knst_process_error::AccessDenied:        return "Access denied";
        case knst_process_error::NoSuchProcess:       return "No such process";
        case knst_process_error::SignalFailed:        return "Signal delivery failed";
        case knst_process_error::NotSupported:        return "Not supported";
        case knst_process_error::ReadFailed:          return "Read failed";
        case knst_process_error::ParseFailed:         return "Parse failed";
        case knst_process_error::SystemCallFailed:    return "System call failed";
        case knst_process_error::Unknown:             return "Unknown error";
    }
    return "Unknown error";
}



enum class knst_stdio_mode : uint8_t { Inherit = 0, Pipe, Null, File };

enum class knst_shell_type : uint8_t {
    Auto = 0, Sh, Bash, Zsh, Fish, Cmd, PowerShell, PowerShellCore, Custom
};

enum class knst_window_mode : uint8_t { Hidden = 0, Minimized, Maximized, Normal };

enum class knst_process_state : uint8_t {
    Unknown = 0, Running, Sleeping, DiskSleep, Zombie, Stopped,
    Tracing, Dead, Idle, Waiting
};

inline const char* knst_process_state_string(knst_process_state s) noexcept {
    switch (s) {
        case knst_process_state::Running:   return "running";
        case knst_process_state::Sleeping:  return "sleeping";
        case knst_process_state::DiskSleep: return "disk-sleep";
        case knst_process_state::Zombie:    return "zombie";
        case knst_process_state::Stopped:   return "stopped";
        case knst_process_state::Tracing:   return "tracing";
        case knst_process_state::Dead:      return "dead";
        case knst_process_state::Idle:      return "idle";
        case knst_process_state::Waiting:   return "waiting";
        case knst_process_state::Unknown:   return "unknown";
    }
    return "unknown";
}


enum class knst_gpu_vendor : uint8_t {
    Unknown = 0, NVIDIA, AMD, Intel, Apple, Other
};

inline const char* knst_gpu_vendor_string(knst_gpu_vendor v) noexcept {
    switch (v) {
        case knst_gpu_vendor::NVIDIA:  return "NVIDIA";
        case knst_gpu_vendor::AMD:     return "AMD";
        case knst_gpu_vendor::Intel:   return "Intel";
        case knst_gpu_vendor::Apple:   return "Apple";
        case knst_gpu_vendor::Other:   return "Other";
        case knst_gpu_vendor::Unknown: return "Unknown";
    }
    return "Unknown";
}

struct knst_gpu_usage {
    bool valid = false;
    uint32_t gpu_index = 0;
    uint64_t vram_bytes = 0;
    knst_gpu_vendor vendor = knst_gpu_vendor::Unknown;
};

struct knst_process_info {
    uint32_t pid  = 0;
    uint32_t ppid = 0;
    uint32_t pgid = 0;
    uint32_t sid  = 0;

    knst_c16string name;
    knst_c16string exe_path;
    knst_c16string cmdline;
    knst_c16string cwd;
    knst_c16string user;
    uint32_t uid = 0;
    uint32_t gid = 0;

    uint64_t rss_bytes = 0;
    uint64_t vsz_bytes = 0;
    uint64_t shared_bytes = 0;
    uint64_t text_bytes = 0;

    uint64_t cpu_user_ms = 0;
    uint64_t cpu_system_ms = 0;
    uint64_t start_time_ms = 0;

    int32_t  nice = 0;
    int32_t  priority = 0;
    uint32_t thread_count = 0;

    knst_process_state state = knst_process_state::Unknown;
    int32_t exit_code = -1;
    bool is_kernel_thread = false;
    bool is_zombie = false;

    uint32_t fd_count = 0;

    
    knst_gpu_usage gpu;

    bool uses_gpu() const noexcept { return gpu.valid; }

    bool has_exe = false;
    bool has_cmdline = false;
    bool has_cwd = false;
    bool has_user = false;
    bool has_memory = false;
    bool has_cpu = false;
    bool has_threads = false;
};

struct knst_thread_info {
    uint32_t tid = 0;
    knst_c16string name;
    knst_process_state state = knst_process_state::Unknown;
    uint64_t cpu_user_ms = 0;
    uint64_t cpu_system_ms = 0;
};

struct knst_fd_info {
    int32_t fd = -1;
    knst_c16string target;
    bool is_socket = false;
    bool is_pipe = false;
    bool is_file = false;
};



struct knst_env_var {
    knst_c16string name;
    knst_c16string value;
};

struct knst_process_options {
    knst_c16string cwd;
    knst_vector<knst_env_var> env;
    bool inherit_env = true;

    knst_stdio_mode stdin_mode  = knst_stdio_mode::Inherit;
    knst_stdio_mode stdout_mode = knst_stdio_mode::Pipe;
    knst_stdio_mode stderr_mode = knst_stdio_mode::Pipe;

    knst_c16string stdin_file;
    knst_c16string stdout_file;
    knst_c16string stderr_file;

    bool capture_stdout = true;
    bool capture_stderr = true;
    bool inherit_stdin  = true;

    knst_window_mode window_mode = knst_window_mode::Hidden;
    bool new_session = false;
    bool new_console = false;
    bool new_process_group = false;
    bool detached = false;
   
    int priority = 0;

    knst_shell_type shell    = knst_shell_type::Auto;
    knst_c16string shell_path;

    bool close_extra_fds = true;
    bool win_unicode = true;

   
    int32_t  nice_value = 0;
    uint64_t affinity_mask = 0;


    bool suspend_after_spawn = false;
};

struct knst_process_result {
    knst_byte_string out_data;
    knst_byte_string err_data;
    int  exit_code = -1;
    bool timed_out = false;
    knst_process_error error = knst_process_error::None;
};



namespace knst_process_detail {

inline void ResolveShell(knst_shell_type shell,const knst_c16string& custom_path,knst_c16string& out_path,knst_c16string& out_flag) noexcept {
                          
#if KNST_USING_PLATFORM_WINDOWS

    

    switch (shell) {
        case knst_shell_type::Cmd:            out_path = u"C:\\Windows\\System32\\cmd.exe"; out_flag = u"/c"; return;
        case knst_shell_type::PowerShell:     out_path = u"powershell.exe"; out_flag = u"-Command"; return;
        case knst_shell_type::PowerShellCore: out_path = u"pwsh.exe";       out_flag = u"-Command"; return;
        case knst_shell_type::Custom:         out_path = custom_path;       out_flag = u"/c"; return;
        default:                              out_path = u"C:\\Windows\\System32\\cmd.exe"; out_flag = u"/c"; return;
    }
#else
    switch (shell) {
        case knst_shell_type::Sh:     out_path = u"/bin/sh";       out_flag = u"-c"; return;
        case knst_shell_type::Bash:   out_path = u"/bin/bash";     out_flag = u"-c"; return;
        case knst_shell_type::Zsh:    out_path = u"/bin/zsh";      out_flag = u"-c"; return;
        case knst_shell_type::Fish:   out_path = u"/usr/bin/fish"; out_flag = u"-c"; return;
        case knst_shell_type::Custom: out_path = custom_path;      out_flag = u"-c"; return;
        default:                      out_path = u"/bin/sh";       out_flag = u"-c"; return;
    }
#endif
}

inline bool iequals_ascii(const knst_c16string& a, const knst_c16string& b) {
    if (a.length() != b.length()) return false;
    for (uint32_t i = 0; i < a.length(); ++i) {
        char16_t ca = a[i], cb = b[i];


        if (ca >= u'A' && ca <= u'Z') ca = (char16_t)(ca + 32);
        if (cb >= u'A' && cb <= u'Z') cb = (char16_t)(cb + 32);
        if (ca != cb) return false;
    }
    return true;
}

inline knst_c16string base_name_of(const knst_c16string& path) noexcept {
    uint32_t start = 0;
    for (uint32_t k = 0; k < path.length(); ++k)
        if (path[k] == u'/' || path[k] == u'\\') start = k + 1;
    return path.substr(start, path.length() - start);
}


struct gpu_proc_entry {
    uint32_t pid = 0;
    uint32_t gpu_index = 0;
    uint64_t vram_bytes = 0;
    knst_gpu_vendor vendor = knst_gpu_vendor::Unknown;
};


knst_vector<gpu_proc_entry> collect_gpu_processes() noexcept;

} // namespace knst_process_detail



struct knst_process {

#if KNST_USING_PLATFORM_WINDOWS
    void* m_process_handle = nullptr;
    void* m_thread_handle  = nullptr;
    void* m_stdin_write = nullptr;
    void* m_stdout_read  = nullptr;
    void* m_stderr_read = nullptr;
    uint32_t m_pid  = 0;
#else
    int  m_pid = -1;
    int  m_stdin_fd = -1;
    int  m_stdout_fd = -1;
    int  m_stderr_fd  = -1;
    bool m_signaled = false;
    int  m_term_signal = 0;
#endif

    knst_byte_string m_line_buf;
    uint32_t  m_line_pos = 0;
    bool  m_line_eof = false;

    knst_c16string m_program_name;
    std::atomic<int>  m_exit_code{ -1 };
    std::atomic<bool> m_running{ false };
    bool m_detached = false;

    knst_process_error error = knst_process_error::None;
    int32_t error_code_native = 0;

    knst_process() noexcept = default;
    ~knst_process() noexcept { cleanup(); }
    knst_process(const knst_process&) = delete;
    knst_process& operator=(const knst_process&) = delete;
    knst_process(knst_process&& other) noexcept { move_from(other); }
    knst_process& operator=(knst_process&& other) noexcept {
        if (this != &other) { cleanup(); move_from(other); }
        return *this;
    }

    static knst_process run(const knst_c16string& program,const knst_vector<knst_c16string>& args = {},const knst_process_options& opts = {}) noexcept;

    static knst_process run_shell(const knst_c16string& command,const knst_process_options& opts = {}) noexcept;

    static knst_process run_with_shell(knst_shell_type shell,const knst_c16string& command,const knst_process_options& opts = {}) noexcept;

    static knst_process run_detached(const knst_c16string& program,const knst_vector<knst_c16string>& args = {},const knst_process_options& opts = {}) noexcept;

    static knst_process_result run_capture(const knst_c16string& program,const knst_vector<knst_c16string>& args = {},const knst_process_options& opts = {},const void* stdin_data = nullptr,size_t stdin_size = 0,uint32_t timeout_ms = 0) noexcept;

    bool wait(uint32_t timeout_ms = 0) noexcept;
    bool kill(int signal = 15) noexcept;
    bool force_kill() noexcept;
    bool kill_tree() noexcept;
    bool is_running() noexcept;
    int  exit_code() noexcept;
    uint32_t pid() const noexcept;
    bool wait_or_kill(uint32_t timeout_ms) noexcept;
    bool was_signaled() const noexcept;
    int  term_signal() const noexcept;

    bool send_signal(int signal) noexcept;
    bool suspend() noexcept;
    bool resume() noexcept;
    bool set_priority(int nice_or_class) noexcept;
    bool set_affinity(uint64_t mask) noexcept;
    knst_process_info get_info() noexcept;

    int32_t native_error_code() const noexcept { return error_code_native; }

    knst_byte_string read_stdout() noexcept;
    knst_byte_string read_stderr() noexcept;
    knst_c16string read_stdout_text() noexcept;
    knst_c16string read_stderr_text() noexcept;
    bool read_line(knst_c16string& out) noexcept;
    bool try_read_stdout(uint32_t timeout_ms, knst_byte_string& out) noexcept;
    bool try_read_stderr(uint32_t timeout_ms, knst_byte_string& out) noexcept;
    bool write_stdin(const void* data, size_t size) noexcept;
    bool write_stdin(const knst_c16string& text) noexcept;
    bool close_stdin() noexcept;
    knst_process_result communicate(const void* stdin_data = nullptr,size_t stdin_size = 0,uint32_t timeout_ms = 0) noexcept;
    static knst_vector<uint32_t> list_pids() noexcept;

    static knst_vector<knst_process_info> list_all(bool include_gpu = false) noexcept;

    static knst_vector<knst_process_info> find_by_name(const knst_c16string& name,bool exact = false) noexcept;
    static knst_vector<uint32_t> children_of(uint32_t ppid) noexcept;
    static knst_vector<knst_process_info> descendants_of(uint32_t pid) noexcept;

    static bool exists(uint32_t pid) noexcept;
    static bool is_zombie(uint32_t pid) noexcept;
    static bool is_alive(uint32_t pid) noexcept;

    static knst_process_info get_info(uint32_t pid) noexcept;
    static knst_c16string get_exe_path(uint32_t pid) noexcept;
    static knst_c16string get_cmdline(uint32_t pid) noexcept;
    static knst_c16string get_cwd(uint32_t pid) noexcept;
    static knst_c16string get_user(uint32_t pid) noexcept;
    static uint32_t get_uid(uint32_t pid) noexcept;
    static uint32_t get_ppid(uint32_t pid) noexcept;
    static int32_t  get_nice(uint32_t pid) noexcept;
    static knst_process_state get_state(uint32_t pid) noexcept;
    static uint64_t get_rss_bytes(uint32_t pid) noexcept;
    static uint64_t get_vsz_bytes(uint32_t pid) noexcept;
    static bool get_cpu_times(uint32_t pid, uint64_t& user_ms, uint64_t& system_ms) noexcept;
    static bool get_memory(uint32_t pid, uint64_t& rss, uint64_t& vsz) noexcept;
    static knst_vector<knst_thread_info> get_threads(uint32_t pid) noexcept;
    static knst_vector<knst_fd_info> get_fds(uint32_t pid) noexcept;
    static knst_vector<knst_env_var> get_env(uint32_t pid) noexcept;
    static knst_vector<uint32_t> get_thread_ids(uint32_t pid) noexcept;

    static bool kill_by_pid(uint32_t pid, int signal = 15) noexcept;
    static bool kill_by_name(const knst_c16string& name,int signal = 15,bool exact = false) noexcept;
    static bool kill_tree(uint32_t pid, int signal = 9) noexcept;
    static bool kill_children(uint32_t pid, int signal = 15) noexcept;
    static bool terminate_graceful(uint32_t pid, uint32_t timeout_ms = 3000) noexcept;
    static bool suspend_pid(uint32_t pid) noexcept;
    static bool resume_pid(uint32_t pid) noexcept;
    static bool set_priority(uint32_t pid, int nice_or_class) noexcept;
    static bool set_affinity(uint32_t pid, uint64_t mask) noexcept;

    static uint32_t current_pid() noexcept;
    static uint32_t parent_pid() noexcept;
    static knst_c16string current_user() noexcept;
    static uint32_t current_uid() noexcept;
    static knst_c16string current_exe_path() noexcept;

    struct system_info {
        uint64_t total_memory = 0;
        uint64_t free_memory = 0;
        uint64_t used_memory = 0;
        uint64_t uptime_ms = 0;
        uint32_t cpu_count = 0;
        uint32_t page_size = 0;
    };
    static system_info get_system_info() noexcept;

    const knst_c16string& program_name() const noexcept { return m_program_name; }
    const char* error_string() const noexcept { return knst_process_error_string(error); }

private:
    void cleanup() noexcept;
    void move_from(knst_process& other) noexcept;

#if KNST_USING_PLATFORM_WINDOWS
    static knst_process spawn_win(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts) noexcept;
    bool fill_line_buffer() noexcept;
#else
    static knst_process spawn_posix(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts) noexcept;
    bool fill_line_buffer() noexcept;
#endif
};



#if !KNST_USING_PLATFORM_WINDOWS

namespace knst_process_detail {

inline void set_cloexec(int fd) noexcept {
    if (fd < 0) return;

    int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags != -1) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}


inline void trim_trailing_newlines(knst_byte_string& s) noexcept {
    while (s.length() > 0) {
        unsigned char c = s[s.length() - 1];
        if (c == '\n' || c == '\r') s.resize(s.length() - 1);
        else break;
    }
}

inline void trim_trailing_newlines(knst_c16string& s) noexcept {
    while (s.length() > 0) {
        char16_t c = s[s.length() - 1];
        if (c == u'\n' || c == u'\r') s.resize(s.length() - 1);
        else break;
    }
}

inline void bump_fd_above_stdio(int& fd) noexcept {
    if (fd >= 0 && fd <= 2) {
        int nf = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (nf >= 0) { ::close(fd); fd = nf; }
    }
}

inline knst_process_error MapErrnoToProcessError(int e) noexcept {
    switch (e) {
        case ENOENT:
        case ENOTDIR: return knst_process_error::NotFound;
        case EACCES:
        case EPERM: return knst_process_error::PermissionDenied;
        case ENOMEM: return knst_process_error::OutOfMemory;
        case EINVAL: return knst_process_error::InvalidArgument;
        case ESRCH: return knst_process_error::NoSuchProcess;
        default: return knst_process_error::ExecFailed;
    }
}

inline void ExecvpeCompat(const char* file, char* const argv[], char* const envp[]) noexcept {
    if (::strchr(file, '/') != nullptr) { ::execve(file, argv, envp); return; }


    const char* path_env = nullptr;
    for (int i = 0; envp && envp[i] != nullptr; ++i)
        if (::strncmp(envp[i], "PATH=", 5) == 0) { path_env = envp[i] + 5; break; }
    const char* search_path = (path_env && *path_env) ? path_env : "/usr/local/bin:/usr/bin:/bin";
    size_t file_len = ::strlen(file);
    const char* p = search_path;
    while (*p) {
        const char* colon = ::strchr(p, ':');
        size_t seg_len = colon ? (size_t)(colon - p) : ::strlen(p);
        char full[4096];
        size_t needed = seg_len + 1 + file_len + 1;
        if (needed <= sizeof(full)) {
            size_t pos = 0;
            if (seg_len > 0) { ::memcpy(full, p, seg_len); pos = seg_len; full[pos++] = '/'; }
            ::memcpy(full + pos, file, file_len);
            full[pos + file_len] = '\0';
            ::execve(full, argv, envp);
        }
        if (!colon) break;
        p = colon + 1;
    }
}

inline int OpenDevNull(bool for_read) noexcept {
    return ::open("/dev/null", for_read ? O_RDONLY : O_WRONLY);
}

inline int OpenRedirectFile(const knst_c16string& path, bool for_read) noexcept {
    if (path.empty()) return -1;
    knst_byte_string p(path);
    if (p.empty()) return -1;
    return ::open(reinterpret_cast<const char*>(p.data()),for_read ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC), 0644);
}

inline knst_stdio_mode EffectiveMode(bool capture_flag,knst_stdio_mode explicit_mode,bool is_stdin) noexcept {
    if (explicit_mode == knst_stdio_mode::File ||
        explicit_mode == knst_stdio_mode::Null) return explicit_mode;
    if (!is_stdin && capture_flag) return knst_stdio_mode::Pipe;
    if (!is_stdin && !capture_flag) return knst_stdio_mode::Inherit;

    
    return capture_flag ? knst_stdio_mode::Pipe : knst_stdio_mode::Inherit;
}

inline bool read_all_fd(int fd, knst_byte_string& out) {
    out.clear();
    unsigned char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) out.append(buf, (uint32_t)n);
        else if (n == 0) break;
        else if (errno == EINTR) continue;
        else return false;
    }
    return true;
}

inline bool read_proc_file(const knst_c16string& path, knst_byte_string& out) {
    knst_byte_string p(path);
    int fd = ::open(reinterpret_cast<const char*>(p.data()), O_RDONLY);
    if (fd < 0) return false;
    bool ok = read_all_fd(fd, out);
    ::close(fd);
    return ok;
}

inline uint64_t get_boot_epoch_ms_cached() noexcept {
    static std::atomic<uint64_t> cached{0};
    uint64_t v = cached.load(std::memory_order_acquire);
    if (v != 0) return v;
    knst_byte_string up;
    uint64_t boot = 0;
    if (read_proc_file(u"/proc/uptime", up) && !up.empty()) {
        double up_sec = 0.0;
        ::sscanf((const char*)up.data(), "%lf", &up_sec);
        uint64_t uptime_ms = (uint64_t)(up_sec * 1000.0);
        struct timespec ts;
        if (::clock_gettime(CLOCK_REALTIME, &ts) == 0) {
            uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
            boot = now_ms > uptime_ms ? now_ms - uptime_ms : 0;
        }
    }
    cached.store(boot, std::memory_order_release);
    return boot;
}

inline bool parse_proc_stat(uint32_t pid, knst_process_info& info) {
    knst_c16string path = u"/proc/";
    path.append((long long)pid);
    path.append(u"/stat");
    
    knst_byte_string b;
    if (!read_proc_file(path, b)) return false;
    const char* s = (const char*)b.data();
    uint32_t n = b.length();
    if (!s || n == 0) return false;

    const char* lp = (const char*)::memchr(s, '(', n);
    if (!lp) return false;

    const char* rp = nullptr;
    for (const char* q = s + n; q > lp; ) {
        if (*--q == ')') { rp = q; break; }
    }

    if (!rp) return false;

    info.name = knst_c16string(lp + 1, (uint32_t)(rp - lp - 1));

    const char* p = rp + 2;
    auto skip = [&]() { while (*p == ' ') ++p; };
    auto read_i64 = [&]() -> int64_t {
        skip();
        bool neg = false;
        if (*p == '-') { neg = true; ++p; }
        int64_t v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
        return neg ? -v : v;
    };
    auto read_u64 = [&]() -> uint64_t { return (uint64_t)read_i64(); };

    skip();
    char state_ch = *p ? *p++ : '?';
    switch (state_ch) {
        case 'R': info.state = knst_process_state::Running;   break;
        case 'S': info.state = knst_process_state::Sleeping;  break;
        case 'D': info.state = knst_process_state::DiskSleep; break;
        case 'Z': info.state = knst_process_state::Zombie; info.is_zombie = true; break;
        case 'T': info.state = knst_process_state::Stopped;   break;
        case 't': info.state = knst_process_state::Tracing;   break;
        case 'X':
        case 'x': info.state = knst_process_state::Dead;      break;
        case 'I': info.state = knst_process_state::Idle;      break;
        case 'W': info.state = knst_process_state::Waiting;   break;
        default:  info.state = knst_process_state::Unknown;   break;
    }

    info.ppid = (uint32_t)read_i64();
    info.pgid = (uint32_t)read_i64();
    info.sid  = (uint32_t)read_i64();
    for (int i = 0; i < 3; ++i) (void)read_i64();
    for (int i = 0; i < 4; ++i) (void)read_u64();
    uint64_t utime = read_u64();
    uint64_t stime = read_u64();
    (void)read_i64(); (void)read_i64();
    info.priority = (int32_t)read_i64();
    info.nice = (int32_t)read_i64();
    info.thread_count = (uint32_t)read_u64();


    (void)read_u64();
    uint64_t starttime = read_u64();
    info.vsz_bytes = read_u64();

    long hz = ::sysconf(_SC_CLK_TCK);
    if (hz <= 0) hz = 100;
    info.cpu_user_ms   = utime * 1000ULL / (uint64_t)hz;
    info.cpu_system_ms = stime * 1000ULL / (uint64_t)hz;

    if (starttime > 0) {
        uint64_t boot_ms = get_boot_epoch_ms_cached();
        info.start_time_ms = boot_ms + (starttime * 1000ULL) / (uint64_t)hz;
    }

    info.pid = pid;
    info.has_cpu = true;
    info.has_threads = true;
    return true;
}


inline bool parse_proc_stat_ppid_only(uint32_t pid, uint32_t& ppid_out) noexcept {
    char path[64];
    ::snprintf(path, sizeof(path), "/proc/%u/stat", pid);
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return false;
    char buf[512];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';

    const char* rp = nullptr;
    for (const char* q = buf + n; q > buf; ) {
        if (*--q == ')') { rp = q; break; }
    }
    if (!rp) return false;

    const char* p = rp + 2;
    while (*p == ' ') ++p;
    if (*p) ++p;
    while (*p == ' ') ++p;

    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    long v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
    ppid_out = (uint32_t)(neg ? -v : v);


    return true;
}


inline bool get_user_name_safe(uint32_t uid, knst_c16string& out) noexcept {
    struct passwd pwd;
    struct passwd* result = nullptr;
    char buf[4096];
    if (::getpwuid_r((uid_t)uid, &pwd, buf, sizeof(buf), &result) == 0 &&
        result && result->pw_name) {
        out = knst_c16string(result->pw_name);
        return true;
    }
    return false;
}

inline bool parse_proc_status(uint32_t pid, knst_process_info& info) {
    knst_c16string path = u"/proc/";
    path.append((long long)pid);
    path.append(u"/status");
    knst_byte_string b;
    if (!read_proc_file(path, b)) return false;
    const char* s = (const char*)b.data();
    uint32_t n = b.length();
    if (!s || !n) return false;

    const char* p = s;
    const char* end = s + n;
    while (p < end) {
        const char* eol = (const char*)::memchr(p, '\n', (size_t)(end - p));
        if (!eol) eol = end;
        const char* colon = (const char*)::memchr(p, ':', (size_t)(eol - p));
        if (colon) {
            const char* v = colon + 1;
            while (v < eol && (*v == ' ' || *v == '\t')) ++v;
            auto key_eq = [&](const char* k) {
                size_t kl = ::strlen(k);
                return (size_t)(colon - p) == kl && ::strncmp(p, k, kl) == 0;
            };
            if (key_eq("Uid")) {
                unsigned u=0,g=0,e=0,s2=0;
                ::sscanf(v, "%u %u %u %u", &u, &g, &e, &s2);
                info.uid = u; info.gid = g;
                info.has_user = true;
            } else if (key_eq("VmRSS")) {
                unsigned long kb = 0; ::sscanf(v, "%lu", &kb);
                info.rss_bytes = (uint64_t)kb * 1024ULL;
                info.has_memory = true;
            } else if (key_eq("VmSize")) {
                unsigned long kb = 0; ::sscanf(v, "%lu", &kb);
                info.vsz_bytes = (uint64_t)kb * 1024ULL;
            } else if (key_eq("VmShared") || key_eq("RssShmem")) {
                unsigned long kb = 0; ::sscanf(v, "%lu", &kb);
                info.shared_bytes = (uint64_t)kb * 1024ULL;
            } else if (key_eq("VmExe")) {
                unsigned long kb = 0; ::sscanf(v, "%lu", &kb);
                info.text_bytes = (uint64_t)kb * 1024ULL;
            } else if (key_eq("Threads")) {
                unsigned t = 0; ::sscanf(v, "%u", &t);
                info.thread_count = t;
            }
        }
        p = (eol < end) ? eol + 1 : end;
    }
    return true;
}

inline bool read_proc_link(uint32_t pid, const char* sub, knst_c16string& out) {
    knst_c16string path = u"/proc/";
    path.append((long long)pid);
    path.append(u"/");
    path.append(sub);
    knst_byte_string p(path);
    char buf[4096];
    ssize_t r = ::readlink(reinterpret_cast<const char*>(p.data()), buf, sizeof(buf) - 1);
    if (r <= 0) return false;
    buf[r] = '\0';
    out = knst_c16string((const char*)buf, (uint32_t)r);
    return true;
}

inline bool read_proc_cmdline(uint32_t pid, knst_c16string& out) {
    knst_c16string path = u"/proc/";
    path.append((long long)pid);
    path.append(u"/cmdline");


    knst_byte_string b;
    if (!read_proc_file(path, b)) return false;
    if (b.empty()) { out.clear(); return true; }
    for (uint32_t i = 0; i < b.length(); ++i) if (b[i] == 0) b[i] = ' ';
    if (b.length() > 0 && b[b.length()-1] == ' ') b.resize(b.length()-1);
    out = knst_c16string((const char*)b.data(), b.length());
    return true;
}

inline knst_c16string process_name_fast(uint32_t pid) noexcept {
    char path[64];
    ::snprintf(path, sizeof(path), "/proc/%u/stat", pid);
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return knst_c16string();

    char buf[512];

    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);

    if (n <= 0) return knst_c16string();
    buf[n] = '\0';
    const char* lp = (const char*)::memchr(buf, '(', n);
    if (!lp) return knst_c16string();
    const char* rp = nullptr;
    for (const char* q = buf + n; q > lp; ) {
        if (*--q == ')') { rp = q; break; }
    }
    if (!rp) return knst_c16string();
    return knst_c16string(lp + 1, (uint32_t)(rp - lp - 1));
}

inline bool pid_exists_raw(int pid) noexcept {
    return ::kill(pid, 0) == 0 || errno == EPERM;
}

} // namespace knst_process_detail



inline knst_process knst_process::run(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts) noexcept {
                                       
    return spawn_posix(program, args, opts);
}

inline knst_process knst_process::run_shell(const knst_c16string& command,const knst_process_options& opts) noexcept {
    knst_c16string shell_path, flag;
    knst_process_detail::ResolveShell(opts.shell, opts.shell_path, shell_path, flag);
    knst_vector<knst_c16string> args;
    args.push_back(flag);
    args.push_back(command);
    return spawn_posix(shell_path, args, opts);
}

inline knst_process knst_process::run_with_shell(knst_shell_type shell,const knst_c16string& command,const knst_process_options& opts) noexcept {
    knst_process_options o = opts;
    o.shell = shell;
    return run_shell(command, o);
}

inline knst_process knst_process::run_detached(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts) noexcept {
    knst_process_options o = opts;
    o.detached = true;
    return run(program, args, o);
}

namespace knst_process_detail {

inline knst_process spawn_posix_detached(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts) noexcept {
    knst_process p;
    p.m_program_name = program;
    if (program.empty()) { p.error = knst_process_error::InvalidArgument; return p; }

    int err_pipe[2] = { -1, -1 };
    if (::pipe(err_pipe) != 0) { p.error = knst_process_error::PipeFailed; return p; }
    set_cloexec(err_pipe[0]); set_cloexec(err_pipe[1]);

    pid_t mid = ::fork();
    if (mid < 0) {
        ::close(err_pipe[0]); ::close(err_pipe[1]);
        p.error = knst_process_error::ForkFailed;
        p.error_code_native = errno;
        return p;
    }

    if (mid == 0) {
        ::close(err_pipe[0]);
        pid_t real = ::fork();
        if (real < 0) {
            int e = errno; int tag = 2; ssize_t w;
            w = ::write(err_pipe[1], &tag, sizeof(tag)); (void)w;
            w = ::write(err_pipe[1], &e, sizeof(e)); (void)w;
            ::_exit(127);
        }
        if (real > 0) {
            int tag = 1; int rp = (int)real; ssize_t w;
            w = ::write(err_pipe[1], &tag, sizeof(tag)); (void)w;
            w = ::write(err_pipe[1], &rp, sizeof(rp)); (void)w;
            ::_exit(0);
        }
        ::setsid();
        int dn = ::open("/dev/null", O_RDWR);
        if (dn >= 0) {
            bump_fd_above_stdio(dn);
            ::dup2(dn, STDIN_FILENO);
            ::dup2(dn, STDOUT_FILENO);
            ::dup2(dn, STDERR_FILENO);
            if (dn > 2) ::close(dn);
        } else {
            int r = OpenDevNull(true);  if (r >= 0) { bump_fd_above_stdio(r); ::dup2(r, STDIN_FILENO); if (r > 2) ::close(r); }
            int w = OpenDevNull(false); if (w >= 0) { bump_fd_above_stdio(w); ::dup2(w, STDOUT_FILENO); if (w > 2) ::close(w); }
            int e = OpenDevNull(false); if (e >= 0) { bump_fd_above_stdio(e); ::dup2(e, STDERR_FILENO); if (e > 2) ::close(e); }
        }
        bump_fd_above_stdio(err_pipe[1]);
        if (!opts.cwd.empty()) {
            knst_byte_string cwd_utf8(opts.cwd);
            if (::chdir(reinterpret_cast<const char*>(cwd_utf8.data())) != 0) {
                int e = errno, tag = 2; ssize_t w;
                w = ::write(err_pipe[1], &tag, sizeof(tag)); (void)w;
                w = ::write(err_pipe[1], &e, sizeof(e)); (void)w;
                ::_exit(127);
            }
        }
        if (opts.inherit_env) {
            for (uint32_t i = 0; i < opts.env.size(); ++i) {
                knst_byte_string n(opts.env[i].name);
                knst_byte_string v(opts.env[i].value);
                ::setenv(reinterpret_cast<const char*>(n.data()),reinterpret_cast<const char*>(v.data()), 1);
            }
        }
        knst_byte_string prog_utf8(program);
        knst_vector<knst_byte_string> args_utf8;
        args_utf8.reserve(args.size());
        for (uint32_t i = 0; i < args.size(); ++i)
            args_utf8.push_back(knst_byte_string(args[i]));
        knst_vector<char*> argv_ptrs;
        argv_ptrs.reserve(args_utf8.size() + 2);
        argv_ptrs.push_back(const_cast<char*>(reinterpret_cast<const char*>(prog_utf8.data())));
        for (uint32_t i = 0; i < args_utf8.size(); ++i)
            argv_ptrs.push_back(const_cast<char*>(reinterpret_cast<const char*>(args_utf8[i].data())));
        argv_ptrs.push_back(nullptr);
        char** envp = environ;
        knst_vector<knst_byte_string> env_storage;
        knst_vector<char*> envp_ptrs;
        if (!opts.inherit_env) {
            env_storage.reserve(opts.env.size());
            envp_ptrs.reserve(opts.env.size() + 1);
            for (uint32_t i = 0; i < opts.env.size(); ++i) {
                knst_c16string pair = opts.env[i].name;
                pair.append(u"=");
                pair.append(opts.env[i].value);
                env_storage.push_back(knst_byte_string(pair));
                envp_ptrs.push_back(const_cast<char*>(reinterpret_cast<const char*>(env_storage.back().data())));
            }
            envp_ptrs.push_back(nullptr);
            envp = envp_ptrs.data();
        }

        {
            int nice_delta = (opts.nice_value != 0) ? opts.nice_value : opts.priority;
            if (nice_delta != 0) ::nice(nice_delta);
        }
        ExecvpeCompat(reinterpret_cast<const char*>(prog_utf8.data()), argv_ptrs.data(), envp);
        int e = errno, tag = 2; ssize_t w;
        w = ::write(err_pipe[1], &tag, sizeof(tag)); (void)w;
        w = ::write(err_pipe[1], &e, sizeof(e)); (void)w;
        ::_exit(127);
    }
    ::close(err_pipe[1]);
    int tag = 0; size_t got = 0; char* dst = reinterpret_cast<char*>(&tag);
    while (got < sizeof(tag)) {
        ssize_t rn = ::read(err_pipe[0], dst + got, sizeof(tag) - got);
        if (rn > 0) { got += (size_t)rn; continue; }
        if (rn < 0 && errno == EINTR) continue;
        break;
    }
    if (got == sizeof(tag) && tag == 1) {
        int real_pid = 0; got = 0; dst = reinterpret_cast<char*>(&real_pid);
        while (got < sizeof(real_pid)) {
            ssize_t rn = ::read(err_pipe[0], dst + got, sizeof(real_pid) - got);
            if (rn > 0) { got += (size_t)rn; continue; }
            if (rn < 0 && errno == EINTR) continue;
            break;
        }
        ::close(err_pipe[0]);
        int st = 0; ::waitpid(mid, &st, 0);
        if (got == sizeof(real_pid) && real_pid > 0) {
            p.m_pid = real_pid;
            p.m_running.store(true);
            p.m_detached = true;
        } else p.error = knst_process_error::ExecFailed;
        return p;
    }
    int child_errno = 0;
    if (got == sizeof(tag) && tag == 2) {
        got = 0; dst = reinterpret_cast<char*>(&child_errno);
        while (got < sizeof(child_errno)) {
            ssize_t rn = ::read(err_pipe[0], dst + got, sizeof(child_errno) - got);
            if (rn > 0) { got += (size_t)rn; continue; }
            if (rn < 0 && errno == EINTR) continue;
            break;
        }
    }
    ::close(err_pipe[0]);
    int st = 0; ::waitpid(mid, &st, 0);
    if (got == sizeof(child_errno) && child_errno != 0) {
        p.error = MapErrnoToProcessError(child_errno);
        p.error_code_native = child_errno;
    } else p.error = knst_process_error::ExecFailed;
    return p;
}

} // namespace knst_process_detail

inline knst_process knst_process::spawn_posix(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts) noexcept {
    using namespace knst_process_detail;
    if (opts.detached) return spawn_posix_detached(program, args, opts);

    knst_process p;
    p.m_program_name = program;
    if (program.empty()) { p.error = knst_process_error::InvalidArgument; return p; }

    knst_stdio_mode stdin_mode  = EffectiveMode(opts.inherit_stdin,  opts.stdin_mode,  true);
    knst_stdio_mode stdout_mode = EffectiveMode(opts.capture_stdout, opts.stdout_mode, false);
    knst_stdio_mode stderr_mode = EffectiveMode(opts.capture_stderr, opts.stderr_mode, false);

    int stdin_pipe[2]  = { -1, -1 };
    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };
    int err_pipe[2]    = { -1, -1 };
    int redir_fds[3]   = { -1, -1, -1 };

    auto cleanup_all = [&]() {
        if (stdin_pipe[0]  >= 0) { ::close(stdin_pipe[0]);  ::close(stdin_pipe[1]);  }
        if (stdout_pipe[0] >= 0) { ::close(stdout_pipe[0]); ::close(stdout_pipe[1]); }
        if (stderr_pipe[0] >= 0) { ::close(stderr_pipe[0]); ::close(stderr_pipe[1]); }
        if (err_pipe[0]    >= 0) { ::close(err_pipe[0]);    ::close(err_pipe[1]);    }
        for (int i = 0; i < 3; ++i) if (redir_fds[i] >= 0) ::close(redir_fds[i]);
    };

    if (stdin_mode == knst_stdio_mode::Pipe) {
        if (::pipe(stdin_pipe) != 0) { p.error = knst_process_error::PipeFailed; return p; }
        set_cloexec(stdin_pipe[0]); set_cloexec(stdin_pipe[1]);
    } else if (stdin_mode == knst_stdio_mode::Null) {
        redir_fds[0] = OpenDevNull(true);
        if (redir_fds[0] < 0) { p.error = knst_process_error::FileRedirectFailed; return p; }
        set_cloexec(redir_fds[0]);
    } else if (stdin_mode == knst_stdio_mode::File) {
        redir_fds[0] = OpenRedirectFile(opts.stdin_file, true);
        if (redir_fds[0] < 0) { p.error = knst_process_error::FileRedirectFailed; return p; }
        set_cloexec(redir_fds[0]);
    }
    if (stdout_mode == knst_stdio_mode::Pipe) {
        if (::pipe(stdout_pipe) != 0) { cleanup_all(); p.error = knst_process_error::PipeFailed; return p; }
        set_cloexec(stdout_pipe[0]); set_cloexec(stdout_pipe[1]);
    } else if (stdout_mode == knst_stdio_mode::Null) {
        redir_fds[1] = OpenDevNull(false);
        if (redir_fds[1] < 0) { cleanup_all(); p.error = knst_process_error::FileRedirectFailed; return p; }
        set_cloexec(redir_fds[1]);
    } else if (stdout_mode == knst_stdio_mode::File) {
        redir_fds[1] = OpenRedirectFile(opts.stdout_file, false);
        if (redir_fds[1] < 0) { cleanup_all(); p.error = knst_process_error::FileRedirectFailed; return p; }
        set_cloexec(redir_fds[1]);
    }
    if (stderr_mode == knst_stdio_mode::Pipe) {
        if (::pipe(stderr_pipe) != 0) { cleanup_all(); p.error = knst_process_error::PipeFailed; return p; }
        set_cloexec(stderr_pipe[0]); set_cloexec(stderr_pipe[1]);
    } else if (stderr_mode == knst_stdio_mode::Null) {
        redir_fds[2] = OpenDevNull(false);
        if (redir_fds[2] < 0) { cleanup_all(); p.error = knst_process_error::FileRedirectFailed; return p; }
        set_cloexec(redir_fds[2]);
    } else if (stderr_mode == knst_stdio_mode::File) {
        redir_fds[2] = OpenRedirectFile(opts.stderr_file, false);
        if (redir_fds[2] < 0) { cleanup_all(); p.error = knst_process_error::FileRedirectFailed; return p; }
        set_cloexec(redir_fds[2]);
    }

    if (::pipe(err_pipe) != 0) { cleanup_all(); p.error = knst_process_error::PipeFailed; return p; }
    set_cloexec(err_pipe[0]); set_cloexec(err_pipe[1]);

    knst_byte_string prog_utf8(program);
    knst_vector<knst_byte_string> args_utf8;
    args_utf8.reserve(args.size());
    for (uint32_t i = 0; i < args.size(); ++i)
        args_utf8.push_back(knst_byte_string(args[i]));

    knst_vector<char*> argv_ptrs;
    argv_ptrs.reserve(args_utf8.size() + 2);
    argv_ptrs.push_back(const_cast<char*>(reinterpret_cast<const char*>(prog_utf8.data())));
    for (uint32_t i = 0; i < args_utf8.size(); ++i)
        argv_ptrs.push_back(const_cast<char*>(reinterpret_cast<const char*>(args_utf8[i].data())));
    argv_ptrs.push_back(nullptr);

    pid_t child = ::fork();
    if (child < 0) {
        cleanup_all();
        p.error = knst_process_error::ForkFailed;
        p.error_code_native = errno;
        return p;
    }

    if (child == 0) {
        ::close(err_pipe[0]);
        bump_fd_above_stdio(err_pipe[1]);
        bump_fd_above_stdio(stdin_pipe[0]);
        bump_fd_above_stdio(stdout_pipe[1]);
        bump_fd_above_stdio(stderr_pipe[1]);
        bump_fd_above_stdio(redir_fds[0]);
        bump_fd_above_stdio(redir_fds[1]);
        bump_fd_above_stdio(redir_fds[2]);
        if (opts.new_session) ::setsid();
        else if (opts.new_process_group) ::setpgid(0, 0);

        if (stdin_pipe[0] >= 0) {
            ::dup2(stdin_pipe[0], STDIN_FILENO);
            ::close(stdin_pipe[0]);
            if (stdin_pipe[1] > 2) ::close(stdin_pipe[1]);
        } else if (redir_fds[0] >= 0) {
            ::dup2(redir_fds[0], STDIN_FILENO);
            if (redir_fds[0] > 2) ::close(redir_fds[0]);
        }
        if (stdout_pipe[1] >= 0) {
            ::dup2(stdout_pipe[1], STDOUT_FILENO);
            if (stdout_pipe[0] > 2) ::close(stdout_pipe[0]);
            ::close(stdout_pipe[1]);
        } else if (redir_fds[1] >= 0) {
            ::dup2(redir_fds[1], STDOUT_FILENO);
            if (redir_fds[1] > 2) ::close(redir_fds[1]);
        }
        if (stderr_pipe[1] >= 0) {
            ::dup2(stderr_pipe[1], STDERR_FILENO);
            if (stderr_pipe[0] > 2) ::close(stderr_pipe[0]);
            ::close(stderr_pipe[1]);
        } else if (redir_fds[2] >= 0) {
            ::dup2(redir_fds[2], STDERR_FILENO);
            if (redir_fds[2] > 2) ::close(redir_fds[2]);
        }
        if (!opts.cwd.empty()) {
            knst_byte_string cwd_utf8(opts.cwd);
            if (::chdir(reinterpret_cast<const char*>(cwd_utf8.data())) != 0) {
                int e = errno;
                ssize_t w = ::write(err_pipe[1], &e, sizeof(e)); (void)w;
                ::_exit(127);
            }
        }
        char** envp = environ;
        knst_vector<knst_byte_string> env_storage;
        knst_vector<char*> envp_ptrs;
        if (opts.inherit_env) {
            for (uint32_t i = 0; i < opts.env.size(); ++i) {
                knst_byte_string n(opts.env[i].name);
                knst_byte_string v(opts.env[i].value);
                ::setenv(reinterpret_cast<const char*>(n.data()),reinterpret_cast<const char*>(v.data()), 1);
                         
            }
            envp = environ;
        } else {
            env_storage.reserve(opts.env.size());
            envp_ptrs.reserve(opts.env.size() + 1);
            for (uint32_t i = 0; i < opts.env.size(); ++i) {
                knst_c16string pair = opts.env[i].name;
                pair.append(u"=");
                pair.append(opts.env[i].value);
                env_storage.push_back(knst_byte_string(pair));
                envp_ptrs.push_back(const_cast<char*>(reinterpret_cast<const char*>(env_storage.back().data())));
            }
            envp_ptrs.push_back(nullptr);
            envp = envp_ptrs.data();
        }

        {
            int nice_delta = (opts.nice_value != 0) ? opts.nice_value : opts.priority;
            if (nice_delta != 0) ::nice(nice_delta);
        }
        if (opts.suspend_after_spawn) ::raise(SIGSTOP);
        ExecvpeCompat(reinterpret_cast<const char*>(prog_utf8.data()), argv_ptrs.data(), envp);
        int e = errno;
        ssize_t w = ::write(err_pipe[1], &e, sizeof(e)); (void)w;
        ::_exit(127);
    }

    ::close(err_pipe[1]);

    if (opts.suspend_after_spawn) {
        ::close(err_pipe[0]);
        p.m_pid = (int)child;
        p.m_running.store(true);
        p.m_detached = false;

        if (stdin_pipe[0]  >= 0) ::close(stdin_pipe[0]);
        if (stdout_pipe[1] >= 0) ::close(stdout_pipe[1]);
        if (stderr_pipe[1] >= 0) ::close(stderr_pipe[1]);
        for (int i = 0; i < 3; ++i) if (redir_fds[i] >= 0) ::close(redir_fds[i]);

        p.m_stdin_fd  = stdin_pipe[1];
        p.m_stdout_fd = stdout_pipe[0];
        p.m_stderr_fd = stderr_pipe[0];

        if (opts.affinity_mask != 0) p.set_affinity(opts.affinity_mask);
        return p;
    }

    int child_errno = 0; size_t got = 0;
    for (;;) {
        ssize_t rn = ::read(err_pipe[0],reinterpret_cast<char*>(&child_errno) + got,sizeof(child_errno) - got);
        if (rn > 0) { got += (size_t)rn; if (got == sizeof(child_errno)) break; continue; }
        if (rn < 0 && errno == EINTR) continue;
        break;
    }
    ::close(err_pipe[0]);

    if (got == sizeof(child_errno)) {
        int status = 0; ::waitpid(child, &status, 0);
        p.m_pid = (int)child;
        p.m_running.store(false);
        p.m_exit_code.store(127);
        p.error = MapErrnoToProcessError(child_errno);
        p.error_code_native = child_errno;
        err_pipe[0] = -1;
        err_pipe[1] = -1;
        cleanup_all();
        return p;
    }

    p.m_pid = (int)child;
    p.m_running.store(true);
    p.m_detached = false;

    if (stdin_pipe[0]  >= 0) ::close(stdin_pipe[0]);
    if (stdout_pipe[1] >= 0) ::close(stdout_pipe[1]);
    if (stderr_pipe[1] >= 0) ::close(stderr_pipe[1]);
    for (int i = 0; i < 3; ++i) if (redir_fds[i] >= 0) ::close(redir_fds[i]);

    p.m_stdin_fd  = stdin_pipe[1];
    p.m_stdout_fd = stdout_pipe[0];
    p.m_stderr_fd = stderr_pipe[0];

    if (opts.affinity_mask != 0) p.set_affinity(opts.affinity_mask);
    return p;
}


inline bool knst_process::wait(uint32_t timeout_ms) noexcept {
    if (m_pid <= 0) return false;
    if (!m_running.load()) return true;

    if (m_detached) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms ? timeout_ms : 0xFFFFFFFFu / 2);
        for (;;) {
            if (!knst_process_detail::pid_exists_raw(m_pid)) {
                m_running.store(false);
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                error = knst_process_error::Timeout;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    auto reap = [&](int status) {
        if (WIFEXITED(status)) {
            m_exit_code.store(WEXITSTATUS(status));
            m_signaled = false; m_term_signal = 0;
        } else if (WIFSIGNALED(status)) {
            m_exit_code.store(128 + WTERMSIG(status));
            m_signaled = true; m_term_signal = WTERMSIG(status);
        } else m_exit_code.store(-1);
        m_running.store(false);
    };

    if (timeout_ms == 0) {
        int status = 0; pid_t r;
        do { r = ::waitpid(m_pid, &status, 0); } while (r < 0 && errno == EINTR);
        if (r == m_pid) { reap(status); return true; }
        error = knst_process_error::WaitFailed;
        error_code_native = errno;
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        int status = 0;
        pid_t r = ::waitpid(m_pid, &status, WNOHANG);
        if (r == m_pid) { reap(status); return true; }
        if (r == 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                error = knst_process_error::Timeout;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (errno == EINTR) continue;
        error = knst_process_error::WaitFailed;
        error_code_native = errno;
        return false;
    }
}

inline bool knst_process::is_running() noexcept {
    if (!m_running.load() || m_pid <= 0) return false;
    if (m_detached) {
        if (knst_process_detail::pid_exists_raw(m_pid)) return true;
        m_running.store(false);
        return false;
    }
    int status = 0;
    pid_t r = ::waitpid(m_pid, &status, WNOHANG);
    if (r == m_pid) {
        if (WIFEXITED(status)) { m_exit_code.store(WEXITSTATUS(status)); m_signaled = false; m_term_signal = 0; }
        else if (WIFSIGNALED(status)) { m_exit_code.store(128 + WTERMSIG(status)); m_signaled = true; m_term_signal = WTERMSIG(status); }
        else m_exit_code.store(-1);
        m_running.store(false);
        return false;
    }
    return (r == 0);
}

inline bool knst_process::kill(int signal) noexcept {
    if (m_pid <= 0 || !m_running.load()) return false;
    if (::kill(m_pid, signal) == 0) return true;
    error = (errno == ESRCH) ? knst_process_error::NoSuchProcess : (errno == EPERM) ? knst_process_error::PermissionDenied : knst_process_error::SignalFailed;
    error_code_native = errno;
    return false;
}
inline bool knst_process::force_kill() noexcept { return kill(9); }
inline bool knst_process::kill_tree() noexcept {
    if (m_pid <= 0 || !m_running.load()) return false;
    return knst_process::kill_tree((uint32_t)m_pid, 9);
}
inline int knst_process::exit_code() noexcept {
    if (m_running.load()) is_running();
    return m_exit_code.load();
}
inline uint32_t knst_process::pid() const noexcept {
    return (m_pid > 0) ? (uint32_t)m_pid : 0;
}
inline bool knst_process::was_signaled() const noexcept { return m_signaled; }
inline int  knst_process::term_signal() const noexcept { return m_term_signal; }

inline bool knst_process::wait_or_kill(uint32_t timeout_ms) noexcept {
    if (wait(timeout_ms)) return true;
    if (error == knst_process_error::Timeout) { force_kill(); wait(0); }
    return false;
}

inline bool knst_process::send_signal(int signal) noexcept {
    if (m_pid <= 0) return false;
    if (::kill(m_pid, signal) == 0) return true;
    error = (errno == ESRCH) ? knst_process_error::NoSuchProcess  : (errno == EPERM) ? knst_process_error::PermissionDenied : knst_process_error::SignalFailed;
    error_code_native = errno;
    return false;
}
inline bool knst_process::suspend() noexcept { return send_signal(SIGSTOP); }
inline bool knst_process::resume()  noexcept { return send_signal(SIGCONT); }

inline bool knst_process::set_priority(int nice_or_class) noexcept {
    if (m_pid <= 0) return false;
    if (::setpriority(PRIO_PROCESS, (id_t)m_pid, nice_or_class) == 0) return true;
    error = (errno == EPERM) ? knst_process_error::PermissionDenied : (errno == ESRCH) ? knst_process_error::NoSuchProcess : knst_process_error::SystemCallFailed;
    error_code_native = errno;
    return false;
}

inline bool knst_process::set_affinity(uint64_t mask) noexcept {
#if defined(__linux__)
    if (m_pid <= 0) return false;
    cpu_set_t set; CPU_ZERO(&set);
    for (int i = 0; i < 64; ++i) if (mask & (1ULL << i)) CPU_SET(i, &set);
    if (::sched_setaffinity((pid_t)m_pid, sizeof(set), &set) == 0) return true;
    error = (errno == EPERM) ? knst_process_error::PermissionDenied : knst_process_error::SystemCallFailed;
                             
    error_code_native = errno;
    return false;
#else
    (void)mask;
    error = knst_process_error::NotSupported;
    return false;
#endif
}

inline knst_process_info knst_process::get_info() noexcept {
    return knst_process::get_info((uint32_t)m_pid);
}

inline bool knst_process::fill_line_buffer() noexcept {
    m_line_buf.clear();
    m_line_pos = 0;
    if (m_stdout_fd < 0) return false;
    unsigned char tmp[4096];


    for (;;) {
        ssize_t n = ::read(m_stdout_fd, tmp, sizeof(tmp));
        if (n > 0) { m_line_buf.append(tmp, (uint32_t)n); return true; }
        if (n == 0) { m_line_eof = true; return false; }
        if (errno == EINTR) continue;
        m_line_eof = true;
        return false;
    }
}

inline knst_byte_string knst_process::read_stdout() noexcept {
    knst_byte_string result;
    if (m_line_pos < m_line_buf.length()) {
        result.append(m_line_buf.data() + m_line_pos, m_line_buf.length() - m_line_pos);
        m_line_buf.clear(); m_line_pos = 0;
    }
    if (m_stdout_fd < 0) return result;
    unsigned char buf[65536];

    for (;;) {
        ssize_t n = ::read(m_stdout_fd, buf, sizeof(buf));
        if (n > 0) result.append(buf, (uint32_t)n);
        else if (n == 0) break;
        else if (errno == EINTR) continue;
        else break;
    }
    ::close(m_stdout_fd); m_stdout_fd = -1;
    m_line_eof = true;
    return result;
}

inline knst_byte_string knst_process::read_stderr() noexcept {
    knst_byte_string result;
    if (m_stderr_fd < 0) return result;
    unsigned char buf[65536];
    for (;;) {
        ssize_t n = ::read(m_stderr_fd, buf, sizeof(buf));
        if (n > 0) result.append(buf, (uint32_t)n);
        else if (n == 0) break;
        else if (errno == EINTR) continue;
        else break;
    }
    ::close(m_stderr_fd); m_stderr_fd = -1;
    return result;
}

inline knst_c16string knst_process::read_stdout_text() noexcept {
    knst_byte_string d = read_stdout();
    if (d.empty() || d.data() == nullptr) return knst_c16string();
    knst_c16string result(reinterpret_cast<const char*>(d.data()), d.length());
    knst_process_detail::trim_trailing_newlines(result);
    return result;
}
inline knst_c16string knst_process::read_stderr_text() noexcept {
    knst_byte_string d = read_stderr();
    if (d.empty() || d.data() == nullptr) return knst_c16string();
    knst_c16string result(reinterpret_cast<const char*>(d.data()), d.length());
    knst_process_detail::trim_trailing_newlines(result);
    return result;
}

inline bool knst_process::read_line(knst_c16string& out) noexcept {
    out.clear();
    knst_byte_string line;
    for (;;) {
        uint32_t n = m_line_buf.length();
        while (m_line_pos < n) {
            unsigned char c = m_line_buf[m_line_pos++];
            if (c == '\n') {
                out = knst_c16string(reinterpret_cast<const char*>(line.data()), line.length());
                return true;
            }
            if (c != '\r') line.push_back(c);
        }
        if (m_line_eof) {
            if (line.empty()) return false;
            out = knst_c16string(reinterpret_cast<const char*>(line.data()), line.length());
            return true;
        }
        fill_line_buffer();
    }
}

inline bool knst_process::try_read_stdout(uint32_t timeout_ms, knst_byte_string& out) noexcept {
    out.clear();
    if (m_line_pos < m_line_buf.length()) {
        out.append(m_line_buf.data() + m_line_pos, m_line_buf.length() - m_line_pos);
        m_line_buf.clear(); m_line_pos = 0;
        return true;
    }
    if (m_stdout_fd < 0) return false;
    struct pollfd pfd;
    pfd.fd = m_stdout_fd; pfd.events = POLLIN; pfd.revents = 0;
    int r = ::poll(&pfd, 1, (int)timeout_ms);
    if (r <= 0) return false;
    if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
        unsigned char buf[65536];
        ssize_t n = ::read(m_stdout_fd, buf, sizeof(buf));
        if (n > 0) { out.append(buf, (uint32_t)n); return true; }
        if (n == 0) { ::close(m_stdout_fd); m_stdout_fd = -1; m_line_eof = true; }
    }
    return false;
}

inline bool knst_process::try_read_stderr(uint32_t timeout_ms, knst_byte_string& out) noexcept {
    out.clear();
    if (m_stderr_fd < 0) return false;
    struct pollfd pfd;
    pfd.fd = m_stderr_fd; pfd.events = POLLIN; pfd.revents = 0;
    int r = ::poll(&pfd, 1, (int)timeout_ms);
    if (r <= 0) return false;
    if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
        unsigned char buf[65536];
        ssize_t n = ::read(m_stderr_fd, buf, sizeof(buf));
        if (n > 0) { out.append(buf, (uint32_t)n); return true; }
        if (n == 0) { ::close(m_stderr_fd); m_stderr_fd = -1; }
    }
    return false;
}

inline bool knst_process::write_stdin(const void* data, size_t size) noexcept {
    if (m_stdin_fd < 0) return false;
    if (size == 0) return true;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    size_t total = 0;
    while (total < size) {
        ssize_t n = ::write(m_stdin_fd, p + total, size - total);
        if (n > 0) total += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}
inline bool knst_process::write_stdin(const knst_c16string& text) noexcept {
    knst_byte_string utf8(text);
    if (utf8.empty()) return true;
    return write_stdin(utf8.data(), utf8.length());
}
inline bool knst_process::close_stdin() noexcept {
    if (m_stdin_fd < 0) return true;
    bool ok = (::close(m_stdin_fd) == 0);
    m_stdin_fd = -1;
    return ok;
}

inline knst_process_result knst_process::communicate(const void* stdin_data,size_t stdin_size,uint32_t timeout_ms) noexcept {
    using clock = std::chrono::steady_clock;
    knst_process_result result;


    const unsigned char* in_ptr = static_cast<const unsigned char*>(stdin_data);
    size_t in_remaining = (stdin_data && stdin_size > 0) ? stdin_size : 0;

    if (m_line_pos < m_line_buf.length()) {
        result.out_data.append(m_line_buf.data() + m_line_pos, m_line_buf.length() - m_line_pos);
        m_line_buf.clear(); m_line_pos = 0;
    }

    bool in_open = (in_remaining > 0 && m_stdin_fd >= 0);
    bool out_open = (m_stdout_fd >= 0);
    bool err_open = (m_stderr_fd >= 0);

    if (!in_open) {
        close_stdin();
    } else {
        int flags = ::fcntl(m_stdin_fd, F_GETFL, 0);
        if (flags != -1) ::fcntl(m_stdin_fd, F_SETFL, flags | O_NONBLOCK);
    }

    unsigned char buf[65536];
    const bool has_deadline = (timeout_ms != 0);
    auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    bool timed_out = false;

    while (in_open || out_open || err_open) {
        if (has_deadline && clock::now() >= deadline) { timed_out = true; break; }

        struct pollfd fds[3];
        int nfds = 0, in_idx = -1, out_idx = -1, err_idx = -1;
        if (in_open)  { fds[nfds].fd = m_stdin_fd;  fds[nfds].events = POLLOUT; fds[nfds].revents = 0; in_idx  = nfds; ++nfds; }
        if (out_open) { fds[nfds].fd = m_stdout_fd; fds[nfds].events = POLLIN;  fds[nfds].revents = 0; out_idx = nfds; ++nfds; }
        if (err_open) { fds[nfds].fd = m_stderr_fd; fds[nfds].events = POLLIN;  fds[nfds].revents = 0; err_idx = nfds; ++nfds; }

        int wait_ms = -1;
        if (has_deadline) {
            auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
            wait_ms = rem > 0 ? (int)(rem > 0x7FFFFFFF ? 0x7FFFFFFF : rem) : 0;
        }
        int r = ::poll(fds, nfds, wait_ms);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) continue;

        if (in_idx >= 0 && (fds[in_idx].revents & (POLLOUT | POLLERR | POLLHUP))) {
            if (fds[in_idx].revents & POLLOUT) {
                size_t chunk = in_remaining > 65536 ? 65536 : in_remaining;
                ssize_t n = ::write(m_stdin_fd, in_ptr, chunk);
                if (n > 0) {
                    in_ptr += n;
                    in_remaining -= (size_t)n;
                    if (in_remaining == 0) { close_stdin(); in_open = false; }
                } else if (n < 0 && errno != EINTR && errno != EAGAIN) {
                    close_stdin(); in_open = false;
                }
                else if (n == 0) {
                    close_stdin(); in_open = false;
                }
            } else {
                close_stdin(); in_open = false;
            }
        }
        if (out_idx >= 0 && (fds[out_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = ::read(m_stdout_fd, buf, sizeof(buf));
            if (n > 0) result.out_data.append(buf, (uint32_t)n);
            else { ::close(m_stdout_fd); m_stdout_fd = -1; out_open = false; m_line_eof = true; }
        }
        if (err_idx >= 0 && (fds[err_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = ::read(m_stderr_fd, buf, sizeof(buf));
            if (n > 0) result.err_data.append(buf, (uint32_t)n);
            else { ::close(m_stderr_fd); m_stderr_fd = -1; err_open = false; }
        }
    }
    if (timed_out) {
        force_kill(); wait(0);
        auto drain_deadline = clock::now() + std::chrono::milliseconds(500);
        while (out_open) {
            auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(drain_deadline - clock::now()).count();
            if (rem <= 0) break;
            struct pollfd pfd; pfd.fd = m_stdout_fd; pfd.events = POLLIN; pfd.revents = 0;
            int pr = ::poll(&pfd, 1, (int)rem);
            if (pr <= 0) break;
            ssize_t n = ::read(m_stdout_fd, buf, sizeof(buf));
            if (n > 0) { result.out_data.append(buf, (uint32_t)n); continue; }
            ::close(m_stdout_fd); m_stdout_fd = -1; out_open = false; break;
        }
        while (err_open) {
            auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(drain_deadline - clock::now()).count();
            if (rem <= 0) break;
            struct pollfd pfd; pfd.fd = m_stderr_fd; pfd.events = POLLIN; pfd.revents = 0;
            int pr = ::poll(&pfd, 1, (int)rem);
            if (pr <= 0) break;
            ssize_t n = ::read(m_stderr_fd, buf, sizeof(buf));
            if (n > 0) { result.err_data.append(buf, (uint32_t)n); continue; }
            ::close(m_stderr_fd); m_stderr_fd = -1; err_open = false; break;
        }
        if (out_open) { ::close(m_stdout_fd); m_stdout_fd = -1; }
        if (err_open) { ::close(m_stderr_fd); m_stderr_fd = -1; }
        result.timed_out = true;
        result.error = knst_process_error::Timeout;
    } else {
        if (out_open) { ::close(m_stdout_fd); m_stdout_fd = -1; }
        if (err_open) { ::close(m_stderr_fd); m_stderr_fd = -1; }
        uint32_t wait_ms = 0;
        if (has_deadline) {
            auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
            wait_ms = rem > 0 ? (uint32_t)rem : 1;
        }
        if (!wait(wait_ms)) {
            if (error == knst_process_error::Timeout) {
                force_kill(); wait(0);
                result.timed_out = true;
                result.error = knst_process_error::Timeout;
            }
        }
    }
    result.exit_code = m_exit_code.load();
    if (!result.timed_out) result.error = error;

    knst_process_detail::trim_trailing_newlines(result.out_data);
    knst_process_detail::trim_trailing_newlines(result.err_data);

    return result;
}

inline void knst_process::cleanup() noexcept {
    if (m_stdin_fd  >= 0) { ::close(m_stdin_fd);  m_stdin_fd  = -1; }
    if (m_stdout_fd >= 0) { ::close(m_stdout_fd); m_stdout_fd = -1; }
    if (m_stderr_fd >= 0) { ::close(m_stderr_fd); m_stderr_fd = -1; }
    m_line_buf.clear(); m_line_pos = 0; m_line_eof = false;
    if (m_pid <= 0) return;
    if (m_detached) { m_pid = -1; m_running.store(false); return; }
    if (m_running.load()) {


        ::kill(m_pid, SIGTERM);
        for (int i = 0; i < 50; ++i) {
            int status = 0;
            pid_t r = ::waitpid(m_pid, &status, WNOHANG);
            if (r == m_pid) { m_running.store(false); m_pid = -1; return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ::kill(m_pid, SIGKILL);
        int status = 0; ::waitpid(m_pid, &status, 0);
        m_running.store(false);
    }

    m_pid = -1;
}

inline void knst_process::move_from(knst_process& other) noexcept {
    m_pid = other.m_pid;
    m_stdin_fd  = other.m_stdin_fd;
    m_stdout_fd = other.m_stdout_fd;
    m_stderr_fd = other.m_stderr_fd;
    m_signaled = other.m_signaled;
    m_term_signal = other.m_term_signal;
    m_detached = other.m_detached;
    m_line_buf = std::move(other.m_line_buf);
    m_line_pos = other.m_line_pos;
    m_line_eof = other.m_line_eof;
    other.m_pid = -1;
    other.m_stdin_fd = -1; other.m_stdout_fd = -1; other.m_stderr_fd = -1;
    other.m_signaled = false; other.m_term_signal = 0; other.m_detached = false;
    other.m_line_pos = 0; other.m_line_eof = false;
    m_program_name = std::move(other.m_program_name);
    m_exit_code.store(other.m_exit_code.load());
    m_running.store(other.m_running.load());
    error = other.error;
    error_code_native = other.error_code_native;
    other.m_running.store(false);
    other.m_exit_code.store(-1);
    other.error = knst_process_error::None;
    other.error_code_native = 0;
}

inline knst_vector<uint32_t> knst_process::list_pids() noexcept {
    knst_vector<uint32_t> out;
#if defined(__linux__)
    DIR* d = ::opendir("/proc");
    if (!d) return out;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        const char* n = e->d_name;
        bool numeric = true;
        for (const char* p = n; *p; ++p) if (*p < '0' || *p > '9') { numeric = false; break; }
        if (!numeric || !*n) continue;
        uint32_t pid = (uint32_t)::strtoul(n, nullptr, 10);
        if (pid > 0) out.push_back(pid);
    }
    ::closedir(d);
#endif
    return out;
}


inline knst_vector<knst_process_info> knst_process::list_all(bool include_gpu) noexcept {
    knst_vector<knst_process_info> out;
    knst_vector<uint32_t> pids = list_pids();
    out.reserve(pids.size());

    knst_vector<knst_process_detail::gpu_proc_entry> gpu_entries;
    if (include_gpu) gpu_entries = knst_process_detail::collect_gpu_processes();

    for (uint32_t i = 0; i < pids.size(); ++i) {
        knst_process_info info = get_info(pids[i]);
        if (info.pid == 0) continue;

        if (include_gpu && !gpu_entries.empty()) {
            for (uint32_t j = 0; j < gpu_entries.size(); ++j) {
                if (gpu_entries[j].pid == info.pid) {
                    info.gpu.valid       = true;
                    info.gpu.gpu_index   = gpu_entries[j].gpu_index;
                    info.gpu.vram_bytes  = gpu_entries[j].vram_bytes;
                    info.gpu.vendor      = gpu_entries[j].vendor;
                    break;
                }
            }
        }
        out.push_back(info);
    }
    return out;
}

inline knst_vector<knst_process_info> knst_process::find_by_name(const knst_c16string& name,
                                                                  bool exact) noexcept {
    knst_vector<knst_process_info> out;
    knst_vector<knst_process_info> all = list_all(false);
    for (uint32_t i = 0; i < all.size(); ++i) {
        bool match = exact ? (all[i].name == name)
                           : knst_process_detail::iequals_ascii(all[i].name, name);
        if (!match && !all[i].exe_path.empty()) {
            knst_c16string base = knst_process_detail::base_name_of(all[i].exe_path);
            match = exact ? (base == name) : knst_process_detail::iequals_ascii(base, name);
        }
        if (match) out.push_back(all[i]);
    }
    return out;
}

inline knst_vector<uint32_t> knst_process::children_of(uint32_t ppid) noexcept {
    knst_vector<uint32_t> out;
    knst_vector<uint32_t> pids = list_pids();
    for (uint32_t i = 0; i < pids.size(); ++i) {
        uint32_t this_ppid = 0;
        if (knst_process_detail::parse_proc_stat_ppid_only(pids[i], this_ppid) &&
            this_ppid == ppid) {
            out.push_back(pids[i]);
        }
    }
    return out;
}

inline knst_vector<knst_process_info> knst_process::descendants_of(uint32_t pid) noexcept {
    knst_vector<knst_process_info> out;
    knst_vector<uint32_t> stack; stack.push_back(pid);
    while (stack.size() > 0) {
        uint32_t cur = stack[stack.size() - 1]; stack.pop_back();
        knst_vector<uint32_t> kids = children_of(cur);
        for (uint32_t i = 0; i < kids.size(); ++i) {
            knst_process_info info = get_info(kids[i]);
            if (info.pid != 0) { out.push_back(info); stack.push_back(kids[i]); }
        }
    }
    return out;
}

inline bool knst_process::exists(uint32_t pid) noexcept {
    if (pid == 0) return false;
#if defined(__linux__)
    knst_c16string p = u"/proc/"; p.append((long long)pid);
    knst_byte_string b(p);
    struct stat st;
    return ::stat(reinterpret_cast<const char*>(b.data()), &st) == 0;
#else
    return ::kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}
inline bool knst_process::is_zombie(uint32_t pid) noexcept {
    return get_state(pid) == knst_process_state::Zombie;
}
inline bool knst_process::is_alive(uint32_t pid) noexcept {
    knst_process_state s = get_state(pid);
    return s != knst_process_state::Zombie && s != knst_process_state::Dead && s != knst_process_state::Unknown;
}

inline knst_process_info knst_process::get_info(uint32_t pid) noexcept {
    knst_process_info info;
    info.pid = pid;
#if defined(__linux__)
    if (!knst_process_detail::parse_proc_stat(pid, info)) {

        info.pid = 0;
        return info;
    }
    (void)knst_process_detail::parse_proc_status(pid, info);
    if (knst_process_detail::read_proc_link(pid, "exe", info.exe_path)) info.has_exe = true;
    if (knst_process_detail::read_proc_cmdline(pid, info.cmdline)) info.has_cmdline = true;
    if (knst_process_detail::read_proc_link(pid, "cwd", info.cwd)) info.has_cwd = true;
    if (info.has_user) {
        knst_process_detail::get_user_name_safe(info.uid, info.user);
    }
    {
        knst_c16string path = u"/proc/"; path.append((long long)pid); path.append(u"/fd");
        knst_byte_string p(path);
        DIR* d = ::opendir(reinterpret_cast<const char*>(p.data()));
        if (d) {
            uint32_t n = 0;
            while (::readdir(d) != nullptr) ++n;
            ::closedir(d);
            info.fd_count = (n >= 2) ? n - 2 : 0;
        }
    }
#endif
    return info;
}

inline knst_c16string knst_process::get_exe_path(uint32_t pid) noexcept {
#if defined(__linux__)
    knst_c16string out; knst_process_detail::read_proc_link(pid, "exe", out); return out;
#else
    (void)pid; return knst_c16string();
#endif
}
inline knst_c16string knst_process::get_cmdline(uint32_t pid) noexcept {
#if defined(__linux__)
    knst_c16string out; knst_process_detail::read_proc_cmdline(pid, out); return out;
#else
    (void)pid; return knst_c16string();
#endif
}
inline knst_c16string knst_process::get_cwd(uint32_t pid) noexcept {
#if defined(__linux__)
    knst_c16string out; knst_process_detail::read_proc_link(pid, "cwd", out); return out;
#else
    (void)pid; return knst_c16string();
#endif
}
inline knst_c16string knst_process::get_user(uint32_t pid) noexcept { return get_info(pid).user; }
inline uint32_t knst_process::get_uid(uint32_t pid) noexcept { return get_info(pid).uid; }
inline uint32_t knst_process::get_ppid(uint32_t pid) noexcept { return get_info(pid).ppid; }
inline int32_t  knst_process::get_nice(uint32_t pid) noexcept { return get_info(pid).nice; }
inline knst_process_state knst_process::get_state(uint32_t pid) noexcept { return get_info(pid).state; }
inline uint64_t knst_process::get_rss_bytes(uint32_t pid) noexcept { return get_info(pid).rss_bytes; }
inline uint64_t knst_process::get_vsz_bytes(uint32_t pid) noexcept { return get_info(pid).vsz_bytes; }

inline bool knst_process::get_cpu_times(uint32_t pid, uint64_t& u, uint64_t& s) noexcept {
    knst_process_info i = get_info(pid);
    u = i.cpu_user_ms; s = i.cpu_system_ms;
    return i.has_cpu;
}
inline bool knst_process::get_memory(uint32_t pid, uint64_t& rss, uint64_t& vsz) noexcept {
    knst_process_info i = get_info(pid);
    rss = i.rss_bytes; vsz = i.vsz_bytes;
    return i.has_memory;
}

inline knst_vector<uint32_t> knst_process::get_thread_ids(uint32_t pid) noexcept {
    knst_vector<uint32_t> out;
#if defined(__linux__)
    knst_c16string path = u"/proc/"; path.append((long long)pid); path.append(u"/task");
    knst_byte_string p(path);
    DIR* d = ::opendir(reinterpret_cast<const char*>(p.data()));
    if (!d) return out;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        const char* n = e->d_name;
        bool numeric = true;
        for (const char* q = n; *q; ++q) if (*q < '0' || *q > '9') { numeric = false; break; }
        if (!numeric || !*n) continue;
        out.push_back((uint32_t)::strtoul(n, nullptr, 10));
    }
    ::closedir(d);
#endif
    return out;
}

inline knst_vector<knst_thread_info> knst_process::get_threads(uint32_t pid) noexcept {
    knst_vector<knst_thread_info> out;
#if defined(__linux__)
    knst_vector<uint32_t> tids = get_thread_ids(pid);
    out.reserve(tids.size());
    for (uint32_t i = 0; i < tids.size(); ++i) {
        knst_thread_info ti; ti.tid = tids[i];
        knst_c16string p = u"/proc/";
        p.append((long long)pid); p.append(u"/task/"); p.append((long long)tids[i]); p.append(u"/comm");
        knst_byte_string b;
        if (knst_process_detail::read_proc_file(p, b)) {
            while (b.length() > 0 && (b[b.length()-1] == '\n' || b[b.length()-1] == '\r'))
                b.resize(b.length() - 1);
            if (b.length() > 0) ti.name = knst_c16string((const char*)b.data(), b.length());
        }
        out.push_back(ti);
    }
#endif
    return out;
}

inline knst_vector<knst_fd_info> knst_process::get_fds(uint32_t pid) noexcept {
    knst_vector<knst_fd_info> out;
#if defined(__linux__)
    knst_c16string path = u"/proc/"; path.append((long long)pid); path.append(u"/fd");
    knst_byte_string p(path);
    DIR* d = ::opendir(reinterpret_cast<const char*>(p.data()));
    if (!d) return out;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        const char* n = e->d_name;
        bool numeric = true;
        for (const char* q = n; *q; ++q) if (*q < '0' || *q > '9') { numeric = false; break; }
        if (!numeric || !*n) continue;
        knst_fd_info fi; fi.fd = (int32_t)::strtol(n, nullptr, 10);
        knst_c16string link = u"/proc/";
        link.append((long long)pid); link.append(u"/fd/"); link.append((long long)fi.fd);
        knst_byte_string lp(link);
        char buf[4096];
        ssize_t r = ::readlink(reinterpret_cast<const char*>(lp.data()), buf, sizeof(buf)-1);
        if (r > 0) {
            buf[r] = '\0';
            fi.target = knst_c16string(buf, (uint32_t)r);
            if (fi.target.starts_with(u"socket:")) fi.is_socket = true;
            else if (fi.target.starts_with(u"pipe:")) fi.is_pipe = true;
            else if (fi.target.starts_with(u"/")) fi.is_file = true;
        }
        out.push_back(fi);
    }
    ::closedir(d);
#endif
    return out;
}

inline knst_vector<knst_env_var> knst_process::get_env(uint32_t pid) noexcept {
    knst_vector<knst_env_var> out;
#if defined(__linux__)
    knst_c16string path = u"/proc/"; path.append((long long)pid); path.append(u"/environ");
    knst_byte_string b;
    if (!knst_process_detail::read_proc_file(path, b)) return out;
    uint32_t i = 0, n = b.length();
    while (i < n) {
        uint32_t start = i;
        while (i < n && b[i] != 0) ++i;
        if (i > start) {
            uint32_t eq = start;
            while (eq < i && b[eq] != '=') ++eq;
            if (eq < i) {
                knst_env_var ev;
                ev.name  = knst_c16string((const char*)b.data() + start, eq - start);
                ev.value = knst_c16string((const char*)b.data() + eq + 1, i - eq - 1);
                out.push_back(ev);
            }
        }
        ++i;
    }
#endif
    return out;
}

inline bool knst_process::kill_by_pid(uint32_t pid, int signal) noexcept {
    if (pid == 0) return false;
    return ::kill((pid_t)pid, signal) == 0;
}
inline bool knst_process::kill_by_name(const knst_c16string& name, int signal, bool exact) noexcept {
    knst_vector<knst_process_info> found = find_by_name(name, exact);
    bool any = false;
    for (uint32_t i = 0; i < found.size(); ++i)
        if (kill_by_pid(found[i].pid, signal)) any = true;
    return any;
}
inline bool knst_process::kill_tree(uint32_t pid, int signal) noexcept {
    knst_vector<knst_process_info> desc = descendants_of(pid);
    for (uint32_t i = 0; i < desc.size(); ++i)
        (void)::kill((pid_t)desc[i].pid, signal);
    (void)::kill(-(pid_t)pid, signal);
    return ::kill((pid_t)pid, signal) == 0;
}
inline bool knst_process::kill_children(uint32_t pid, int signal) noexcept {
    knst_vector<uint32_t> kids = children_of(pid);
    bool any = false;
    for (uint32_t i = 0; i < kids.size(); ++i)
        if (::kill((pid_t)kids[i], signal) == 0) any = true;
    return any;
}
inline bool knst_process::terminate_graceful(uint32_t pid, uint32_t timeout_ms) noexcept {
    if (!exists(pid)) return false;
    if (::kill((pid_t)pid, SIGTERM) != 0) return false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!exists(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return kill_by_pid(pid, SIGKILL);
}
inline bool knst_process::suspend_pid(uint32_t pid) noexcept { return ::kill((pid_t)pid, SIGSTOP) == 0; }
inline bool knst_process::resume_pid(uint32_t pid) noexcept { return ::kill((pid_t)pid, SIGCONT) == 0; }
inline bool knst_process::set_priority(uint32_t pid, int nice_or_class) noexcept {
    return ::setpriority(PRIO_PROCESS, (id_t)pid, nice_or_class) == 0;
}
inline bool knst_process::set_affinity(uint32_t pid, uint64_t mask) noexcept {
#if defined(__linux__)
    cpu_set_t set; CPU_ZERO(&set);
    for (int i = 0; i < 64; ++i) if (mask & (1ULL << i)) CPU_SET(i, &set);
    return ::sched_setaffinity((pid_t)pid, sizeof(set), &set) == 0;
#else
    (void)pid; (void)mask; return false;
#endif
}

inline uint32_t knst_process::current_pid() noexcept { return (uint32_t)::getpid(); }
inline uint32_t knst_process::parent_pid()  noexcept { return (uint32_t)::getppid(); }
inline uint32_t knst_process::current_uid() noexcept { return (uint32_t)::getuid(); }
inline knst_c16string knst_process::current_user() noexcept {
    knst_c16string out;
    knst_process_detail::get_user_name_safe((uint32_t)::getuid(), out);
    return out;
}
inline knst_c16string knst_process::current_exe_path() noexcept {
#if defined(__linux__)
    char buf[4096];
    ssize_t r = ::readlink("/proc/self/exe", buf, sizeof(buf)-1);
    if (r > 0) { buf[r] = '\0'; return knst_c16string(buf, (uint32_t)r); }
#endif
    return knst_c16string();
}

inline knst_process::system_info knst_process::get_system_info() noexcept {
    system_info si;
    long ps = ::sysconf(_SC_PAGESIZE);
    si.page_size = ps > 0 ? (uint32_t)ps : 4096;
    long np = ::sysconf(_SC_NPROCESSORS_ONLN);
    si.cpu_count = np > 0 ? (uint32_t)np : 1;
#if defined(__linux__)
    knst_byte_string b;
    if (knst_process_detail::read_proc_file(u"/proc/meminfo", b)) {
        const char* s = (const char*)b.data();
        unsigned long long kb = 0;
        if (::sscanf(s, "MemTotal: %llu kB", &kb) == 1) si.total_memory = kb * 1024ULL;
        const char* p = ::strstr(s, "MemAvailable:");
        if (p && ::sscanf(p, "MemAvailable: %llu kB", &kb) == 1) si.free_memory = kb * 1024ULL;
        else { p = ::strstr(s, "MemFree:"); if (p && ::sscanf(p, "MemFree: %llu kB", &kb) == 1) si.free_memory = kb * 1024ULL; }
        si.used_memory = si.total_memory > si.free_memory ? si.total_memory - si.free_memory : 0;
    }
    knst_byte_string up;
    if (knst_process_detail::read_proc_file(u"/proc/uptime", up) && !up.empty()) {
        double sec = 0.0; ::sscanf((const char*)up.data(), "%lf", &sec);
        si.uptime_ms = (uint64_t)(sec * 1000.0);
    }
#endif
    return si;
}

#endif // !KNST_USING_PLATFORM_WINDOWS



#if KNST_USING_PLATFORM_WINDOWS

namespace knst_process_detail {



inline knst_c16string process_name_fast(uint32_t pid) noexcept {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return knst_c16string();
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    knst_c16string name;
    if (Process32FirstW(snap, &pe)) {
        do {
            if ((uint32_t)pe.th32ProcessID == pid) {
                name = knst_c16string(reinterpret_cast<const char16_t*>(pe.szExeFile));
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return name;
}


inline void trim_trailing_newlines(knst_byte_string& s) noexcept {
    while (s.length() > 0) {
        unsigned char c = s[s.length() - 1];
        if (c == '\n' || c == '\r') s.resize(s.length() - 1);
        else break;
    }
}

inline void trim_trailing_newlines(knst_c16string& s) noexcept {
    while (s.length() > 0) {
        char16_t c = s[s.length() - 1];
        if (c == u'\n' || c == u'\r') s.resize(s.length() - 1);
        else break;
    }
}

inline void AppendQuotedArgWin(knst_c16string& cmdline, const knst_c16string& arg) noexcept {
    bool needs_quotes = arg.empty();
    if (!needs_quotes) {
        for (uint32_t i = 0; i < arg.length(); ++i) {
            char16_t c = arg[i];
            if (c == u' ' || c == u'\t' || c == u'\n' || c == u'\v' || c == u'"') { needs_quotes = true; break; }
        }
    }
    if (!needs_quotes) { cmdline.append(arg); return; }
    cmdline.append(u"\"");
    uint32_t len = arg.length(); uint32_t i = 0;
    while (i < len) {
        uint32_t backslashes = 0;
        while (i < len && arg[i] == u'\\') { ++backslashes; ++i; }
        if (i == len) { for (uint32_t k = 0; k < backslashes * 2; ++k) cmdline.append(u"\\"); }
        else if (arg[i] == u'"') {
            for (uint32_t k = 0; k < backslashes * 2 + 1; ++k) cmdline.append(u"\\");
            cmdline.append(u"\""); ++i;
        } else {
            for (uint32_t k = 0; k < backslashes; ++k) cmdline.append(u"\\");
            char16_t one[2] = { arg[i], u'\0' };
            cmdline.append(one); ++i;
        }
    }
    cmdline.append(u"\"");
}

inline void BuildWindowsEnvBlock(const knst_process_options& opts, knst_vector<wchar_t>& block) noexcept {
    block.clear();
    knst_vector<knst_c16string> lines;
    if (opts.inherit_env) {
        LPWCH es = GetEnvironmentStringsW();
        if (es) {
            const wchar_t* p = es;
            while (*p) {
                size_t len = wcslen(p);
                lines.push_back(knst_c16string(reinterpret_cast<const char16_t*>(p), (uint32_t)len));
                p += len + 1;
            }
            FreeEnvironmentStringsW(es);
        }
    }
    for (uint32_t i = 0; i < opts.env.size(); ++i) {
        const knst_c16string& name = opts.env[i].name;
        for (uint32_t j = 0; j < lines.size(); ) {
            const knst_c16string& line = lines[j];
            uint32_t eq = UINT32_MAX;
            for (uint32_t k = 0; k < line.length(); ++k) if (line[k] == u'=') { eq = k; break; }
            bool same = (eq != UINT32_MAX && eq == name.length());
            if (same) {
                for (uint32_t k = 0; k < eq; ++k) {
                    char16_t a = line[k], b = name[k];
                    if (a >= u'a' && a <= u'z') a = (char16_t)(a - 32);
                    if (b >= u'a' && b <= u'z') b = (char16_t)(b - 32);
                    if (a != b) { same = false; break; }
                }
            }
            if (same) lines.erase(j);
            else ++j;
        }
    }
    for (uint32_t i = 0; i < opts.env.size(); ++i) {
        knst_c16string entry = opts.env[i].name;
        entry.append(u"="); entry.append(opts.env[i].value);
        lines.push_back(std::move(entry));
    }
    for (uint32_t i = 0; i < lines.size(); ++i) {
        const char16_t* d = lines[i].data();
        uint32_t len = lines[i].length();
        for (uint32_t k = 0; k < len; ++k) block.push_back((wchar_t)d[k]);
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
}

inline DWORD PriorityToWinClass(int priority) noexcept {
    if (priority <= -15) return HIGH_PRIORITY_CLASS;
    if (priority <= -5)  return ABOVE_NORMAL_PRIORITY_CLASS;
    if (priority <  15)  return NORMAL_PRIORITY_CLASS;
    if (priority <  20)  return BELOW_NORMAL_PRIORITY_CLASS;
    return IDLE_PRIORITY_CLASS;
}

inline HANDLE PickHandle(knst_stdio_mode mode, HANDLE pipe_end, HANDLE redir, DWORD std_id) noexcept {
    switch (mode) {
        case knst_stdio_mode::Pipe: return pipe_end;
        case knst_stdio_mode::Null: return redir;
        case knst_stdio_mode::File: return redir;
        default:                    return GetStdHandle(std_id);
    }
}

inline knst_stdio_mode EffectiveMode(bool capture_flag, knst_stdio_mode explicit_mode, bool is_stdin) noexcept {
    if (explicit_mode == knst_stdio_mode::File || explicit_mode == knst_stdio_mode::Null) return explicit_mode;
    if (!is_stdin && capture_flag) return knst_stdio_mode::Pipe;
    if (!is_stdin && !capture_flag) return knst_stdio_mode::Inherit;
    return capture_flag ? knst_stdio_mode::Pipe : knst_stdio_mode::Inherit;
}

typedef LONG (NTAPI *NtQueryInformationProcess_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef LONG (NTAPI *NtSuspendProcess_t)(HANDLE);
typedef LONG (NTAPI *NtResumeProcess_t)(HANDLE);


inline NtSuspendProcess_t GetNtSuspendProcess() noexcept {
    static NtSuspendProcess_t fn = []() -> NtSuspendProcess_t {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        return nt ? (NtSuspendProcess_t)GetProcAddress(nt, "NtSuspendProcess") : nullptr;
    }();
    return fn;
}
inline NtResumeProcess_t GetNtResumeProcess() noexcept {
    static NtResumeProcess_t fn = []() -> NtResumeProcess_t {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        return nt ? (NtResumeProcess_t)GetProcAddress(nt, "NtResumeProcess") : nullptr;
    }();
    return fn;
}

typedef struct _KNST_UNICODE_STRING { USHORT Length; USHORT MaximumLength; PVOID Buffer; } KNST_UNICODE_STRING;
typedef struct _KNST_UNICODE_STRING32 { USHORT Length; USHORT MaximumLength; ULONG Buffer; } KNST_UNICODE_STRING32;
typedef struct _KNST_PROCESS_BASIC_INFORMATION {
    PVOID Reserved1; PVOID PebBaseAddress; PVOID Reserved2[2];
    ULONG_PTR UniqueProcessId; PVOID Reserved3;
} KNST_PROCESS_BASIC_INFORMATION;

#ifndef KNST_PROCESS_BASIC_INFORMATION_CLASS
    #define KNST_PROCESS_BASIC_INFORMATION_CLASS 0
#endif
#ifndef KNST_PROCESS_WOW64_INFORMATION_CLASS
    #define KNST_PROCESS_WOW64_INFORMATION_CLASS 26
#endif


inline bool GetProcessUserName(HANDLE hProcess, knst_c16string& out_user) noexcept {
    if (!hProcess) return false;

    HANDLE token = nullptr;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &token)) return false;

    DWORD sz = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &sz);
    if (sz == 0 || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(token);
        return false;
    }

    knst_vector<unsigned char> buf;
    if (!buf.resize((uint32_t)sz)) { CloseHandle(token); return false; }

    BOOL ok = ::GetTokenInformation(token, TokenUser, buf.data(), sz, &sz);
    CloseHandle(token);
    if (!ok) return false;

    TOKEN_USER* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
    if (!tu->User.Sid) return false;

    wchar_t name[256] = {};
    wchar_t domain[256] = {};
    DWORD name_len   = (DWORD)(sizeof(name)   / sizeof(name[0]));
    DWORD domain_len = (DWORD)(sizeof(domain) / sizeof(domain[0]));
    SID_NAME_USE sid_type = SidTypeUnknown;
    if (!::LookupAccountSidW(nullptr, tu->User.Sid, name,   &name_len, domain, &domain_len, &sid_type)) {
                             
        return false;
    }
    if (name_len == 0) return false;


    out_user = knst_c16string();
    if (domain_len > 0) {
        out_user.append(knst_c16string(reinterpret_cast<const char16_t*>(domain),(uint32_t)domain_len));
            
        out_user.append(u"\\");
    }
    out_user.append(knst_c16string(
        reinterpret_cast<const char16_t*>(name),
        (uint32_t)name_len));
    return true;
}


inline knst_c16string ReadCmdlineFromPEB(HANDLE hProcess, bool target_wow64) noexcept {
    knst_c16string out;
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (!nt) return out;
    auto NtQIP = (NtQueryInformationProcess_t)GetProcAddress(nt, "NtQueryInformationProcess");
    if (!NtQIP) return out;

#if defined(_WIN64)
    if (target_wow64) {
        ULONG_PTR peb32 = 0;
        if (NtQIP(hProcess, KNST_PROCESS_WOW64_INFORMATION_CLASS,
                  &peb32, sizeof(peb32), nullptr) != 0 || peb32 == 0) return out;
        ULONG params32 = 0;
        if (!ReadProcessMemory(hProcess, (LPCVOID)(peb32 + 0x10), &params32, sizeof(params32), nullptr) || params32 == 0) return out;
                               
        KNST_UNICODE_STRING32 us;
        if (!ReadProcessMemory(hProcess, (LPCVOID)((ULONG_PTR)params32 + 0x40), &us, sizeof(us), nullptr) || us.Length == 0 || us.Buffer == 0) return out;
                               
        uint32_t chars = us.Length / 2;
        knst_vector<char16_t> tmp;
        if (!tmp.reserve(chars)) return out;
        for (uint32_t i = 0; i < chars; ++i) tmp.push_back(u'\0');
        if (!ReadProcessMemory(hProcess, (LPCVOID)(ULONG_PTR)us.Buffer,tmp.data(), chars * sizeof(char16_t), nullptr)) return out;
        out = knst_c16string(tmp.data(), chars);
        return out;
    } else {
        KNST_PROCESS_BASIC_INFORMATION pbi = {};
        if (NtQIP(hProcess, KNST_PROCESS_BASIC_INFORMATION_CLASS, &pbi, sizeof(pbi), nullptr) != 0 || pbi.PebBaseAddress == nullptr) return out;
        ULONG_PTR params = 0;
        if (!ReadProcessMemory(hProcess, (LPCVOID)((ULONG_PTR)pbi.PebBaseAddress + 0x20),  &params, sizeof(params), nullptr) || params == 0) return out;
        KNST_UNICODE_STRING us = {};
        if (!ReadProcessMemory(hProcess, (LPCVOID)(params + 0x70), &us,  sizeof(us), nullptr) || us.Length == 0 || us.Buffer == nullptr) return out;
        uint32_t chars = us.Length / 2;
        knst_vector<char16_t> tmp;
        if (!tmp.reserve(chars)) return out;
        for (uint32_t i = 0; i < chars; ++i) tmp.push_back(u'\0');

        if (!ReadProcessMemory(hProcess, us.Buffer, tmp.data(), chars * sizeof(char16_t), nullptr)) return out;
                               
        out = knst_c16string(tmp.data(), chars);


        return out;
    }
#else
    (void)target_wow64;
    KNST_PROCESS_BASIC_INFORMATION pbi = {};
    if (NtQIP(hProcess, KNST_PROCESS_BASIC_INFORMATION_CLASS,&pbi, sizeof(pbi), nullptr) != 0 || pbi.PebBaseAddress == nullptr) return out;
              
    ULONG params = 0;
    if (!ReadProcessMemory(hProcess, (LPCVOID)((ULONG_PTR)pbi.PebBaseAddress + 0x10),&params, sizeof(params), nullptr) || params == 0) return out;
    KNST_UNICODE_STRING32 us = {};
    if (!ReadProcessMemory(hProcess, (LPCVOID)(ULONG_PTR)(params + 0x40), &us, sizeof(us), nullptr) || us.Length == 0 || us.Buffer == 0) return out;
    uint32_t chars = us.Length / 2;
    knst_vector<char16_t> tmp;
    if (!tmp.reserve(chars)) return out;
    for (uint32_t i = 0; i < chars; ++i) tmp.push_back(u'\0');
    if (!ReadProcessMemory(hProcess, (LPCVOID)(ULONG_PTR)us.Buffer, tmp.data(),  chars * sizeof(char16_t), nullptr)) return out;
                          
    out = knst_c16string(tmp.data(), chars);
    return out;
#endif
}

}

inline knst_process knst_process::run(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts) noexcept {
    return spawn_win(program, args, opts);
}

inline knst_process knst_process::run_shell(const knst_c16string& command,const knst_process_options& opts) noexcept {
    knst_c16string sp, fl;
    knst_process_detail::ResolveShell(opts.shell, opts.shell_path, sp, fl);
    knst_vector<knst_c16string> a; a.push_back(fl); a.push_back(command);
    return spawn_win(sp, a, opts);
}

inline knst_process knst_process::run_with_shell(knst_shell_type shell,const knst_c16string& command,const knst_process_options& opts) noexcept {
    knst_process_options o = opts; o.shell = shell; return run_shell(command, o);
}

inline knst_process knst_process::run_detached(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts) noexcept {
    knst_process_options o = opts; o.detached = true; return run(program, args, o);
}

inline knst_process knst_process::spawn_win(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts) noexcept {
    using namespace knst_process_detail;

    knst_process p;
    p.m_program_name = program;
    if (program.empty()) { p.error = knst_process_error::InvalidArgument; return p; }

    knst_stdio_mode stdin_mode  = EffectiveMode(opts.inherit_stdin,  opts.stdin_mode,  true);
    knst_stdio_mode stdout_mode = EffectiveMode(opts.capture_stdout, opts.stdout_mode, false);
    knst_stdio_mode stderr_mode = EffectiveMode(opts.capture_stderr, opts.stderr_mode, false);

    if (opts.detached) {
        if (stdin_mode  == knst_stdio_mode::Pipe) stdin_mode  = knst_stdio_mode::Null;
        if (stdout_mode == knst_stdio_mode::Pipe) stdout_mode = knst_stdio_mode::Null;
        if (stderr_mode == knst_stdio_mode::Pipe) stderr_mode = knst_stdio_mode::Null;
    }

    SECURITY_ATTRIBUTES sa = {}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;

    HANDLE sr=nullptr, sw=nullptr, or_=nullptr, ow=nullptr, er=nullptr, ew=nullptr;
    HANDLE nul_in = INVALID_HANDLE_VALUE, nul_out = INVALID_HANDLE_VALUE, nul_err = INVALID_HANDLE_VALUE;
    HANDLE file_in = INVALID_HANDLE_VALUE, file_out = INVALID_HANDLE_VALUE, file_err = INVALID_HANDLE_VALUE;

    auto fail = [&]() {
        if (sr) CloseHandle(sr); if (sw) CloseHandle(sw);
        if (or_) CloseHandle(or_); if (ow) CloseHandle(ow);
        if (er) CloseHandle(er); if (ew) CloseHandle(ew);
        if (nul_in  != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
        if (nul_out != INVALID_HANDLE_VALUE) CloseHandle(nul_out);
        if (nul_err != INVALID_HANDLE_VALUE) CloseHandle(nul_err);
        if (file_in  != INVALID_HANDLE_VALUE) CloseHandle(file_in);
        if (file_out != INVALID_HANDLE_VALUE) CloseHandle(file_out);
        if (file_err != INVALID_HANDLE_VALUE) CloseHandle(file_err);
    };

    if (stdin_mode == knst_stdio_mode::Pipe) {
        if (!CreatePipe(&sr, &sw, &sa, 0)) { p.error = knst_process_error::PipeFailed; return p; }
        SetHandleInformation(sw, HANDLE_FLAG_INHERIT, 0);
    } else if (stdin_mode == knst_stdio_mode::Null) {
        nul_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        if (nul_in == INVALID_HANDLE_VALUE) { fail(); p.error = knst_process_error::FileRedirectFailed; return p; }
    } else if (stdin_mode == knst_stdio_mode::File) {
        file_in = CreateFileW(reinterpret_cast<const wchar_t*>(opts.stdin_file.data()),
                              GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, nullptr);
        if (file_in == INVALID_HANDLE_VALUE) { fail(); p.error = knst_process_error::FileRedirectFailed; return p; }
    }
    if (stdout_mode == knst_stdio_mode::Pipe) {
        if (!CreatePipe(&or_, &ow, &sa, 0)) { fail(); p.error = knst_process_error::PipeFailed; return p; }
        SetHandleInformation(or_, HANDLE_FLAG_INHERIT, 0);
    } else if (stdout_mode == knst_stdio_mode::Null) {
        nul_out = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        if (nul_out == INVALID_HANDLE_VALUE) { fail(); p.error = knst_process_error::FileRedirectFailed; return p; }
    } else if (stdout_mode == knst_stdio_mode::File) {
        file_out = CreateFileW(reinterpret_cast<const wchar_t*>(opts.stdout_file.data()),
                               GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
        if (file_out == INVALID_HANDLE_VALUE) { fail(); p.error = knst_process_error::FileRedirectFailed; return p; }
    }
    if (stderr_mode == knst_stdio_mode::Pipe) {
        if (!CreatePipe(&er, &ew, &sa, 0)) { fail(); p.error = knst_process_error::PipeFailed; return p; }
        SetHandleInformation(er, HANDLE_FLAG_INHERIT, 0);
    } else if (stderr_mode == knst_stdio_mode::Null) {
        nul_err = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        if (nul_err == INVALID_HANDLE_VALUE) { fail(); p.error = knst_process_error::FileRedirectFailed; return p; }
    } else if (stderr_mode == knst_stdio_mode::File) {
        file_err = CreateFileW(reinterpret_cast<const wchar_t*>(opts.stderr_file.data()),GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
        if (file_err == INVALID_HANDLE_VALUE) { fail(); p.error = knst_process_error::FileRedirectFailed; return p; }
    }

    knst_c16string cmdline;
    AppendQuotedArgWin(cmdline, program);
    for (uint32_t i = 0; i < args.size(); ++i) {
        cmdline.append(u" ");
        AppendQuotedArgWin(cmdline, args[i]);
    }

    knst_c16string prog_z = program;
    knst_vector<wchar_t> cmd_wbuf;
    cmd_wbuf.reserve(cmdline.length() + 1);
    for (uint32_t i = 0; i < cmdline.length(); ++i)
        cmd_wbuf.push_back((wchar_t)cmdline[i]);
    cmd_wbuf.push_back(L'\0');

    const wchar_t* cwd_ptr = nullptr;
    knst_c16string cwd_z;
    if (!opts.cwd.empty()) { cwd_z = opts.cwd; cwd_ptr = reinterpret_cast<const wchar_t*>(cwd_z.data()); }

    knst_vector<wchar_t> env_block;
    LPVOID env_ptr = nullptr;
    DWORD extra = 0;
    if (!opts.inherit_env || !opts.env.empty()) {
        BuildWindowsEnvBlock(opts, env_block);
        env_ptr = env_block.data();
        extra |= CREATE_UNICODE_ENVIRONMENT;
    }

    HANDLE hStdIn  = PickHandle(stdin_mode,  sr, (nul_in  != INVALID_HANDLE_VALUE ? nul_in  : file_in),  STD_INPUT_HANDLE);
    HANDLE hStdOut = PickHandle(stdout_mode, ow, (nul_out != INVALID_HANDLE_VALUE ? nul_out : file_out), STD_OUTPUT_HANDLE);
    HANDLE hStdErr = PickHandle(stderr_mode, ew, (nul_err != INVALID_HANDLE_VALUE ? nul_err : file_err), STD_ERROR_HANDLE);


    int effective_priority = (opts.nice_value != 0) ? opts.nice_value : opts.priority;

    DWORD base_flags = extra | PriorityToWinClass(effective_priority);
    if (opts.window_mode == knst_window_mode::Hidden) base_flags |= CREATE_NO_WINDOW;
    if (opts.new_console) base_flags |= CREATE_NEW_CONSOLE;
    if (opts.new_process_group || opts.detached || opts.new_session) base_flags |= CREATE_NEW_PROCESS_GROUP;
    if (opts.detached) base_flags |= DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB;
    if (opts.suspend_after_spawn) base_flags |= CREATE_SUSPENDED;

    PROCESS_INFORMATION pi = {};

    auto do_create = [&](DWORD flags, STARTUPINFOW* psi) -> BOOL {
        return CreateProcessW(
            reinterpret_cast<const wchar_t*>(prog_z.data()),
            cmd_wbuf.data(), nullptr, nullptr, TRUE, flags, env_ptr, cwd_ptr, psi, &pi);
    };

    auto fill_startup = [&](STARTUPINFOW& si) {
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hStdIn; si.hStdOutput = hStdOut; si.hStdError = hStdErr;
        switch (opts.window_mode) {
            case knst_window_mode::Minimized: si.dwFlags |= STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWMINNOACTIVE; break;
            case knst_window_mode::Maximized: si.dwFlags |= STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWMAXIMIZED;  break;
            case knst_window_mode::Normal:    si.dwFlags |= STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWNORMAL;     break;
            default: break;
        }
    };

    BOOL ok = FALSE;

#ifdef PROC_THREAD_ATTRIBUTE_HANDLE_LIST
    STARTUPINFOEXW siex = {};
    fill_startup(siex.StartupInfo);
    siex.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    HANDLE inherit_list[3]; int n_inherit = 0;
    if (hStdIn)  inherit_list[n_inherit++] = hStdIn;
    if (hStdOut) inherit_list[n_inherit++] = hStdOut;
    if (hStdErr) inherit_list[n_inherit++] = hStdErr;

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    if (attr_size > 0) {
        void* attr_mem = HeapAlloc(GetProcessHeap(), 0, attr_size);
        if (attr_mem) {
            if (InitializeProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)attr_mem, 1, 0, &attr_size)) {
                if (UpdateProcThreadAttribute((LPPROC_THREAD_ATTRIBUTE_LIST)attr_mem, 0,
                                              PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit_list,
                                              (SIZE_T)(n_inherit * sizeof(HANDLE)), nullptr, nullptr)) {
                    siex.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attr_mem;
                    ok = do_create(base_flags | EXTENDED_STARTUPINFO_PRESENT, &siex.StartupInfo);
                    if (!ok && (base_flags & CREATE_BREAKAWAY_FROM_JOB) &&
                        GetLastError() == ERROR_ACCESS_DENIED) {
                        DWORD retry_flags = (base_flags & ~CREATE_BREAKAWAY_FROM_JOB) | EXTENDED_STARTUPINFO_PRESENT;
                        ok = do_create(retry_flags, &siex.StartupInfo);
                    }
                }
                DeleteProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)attr_mem);
            }
            HeapFree(GetProcessHeap(), 0, attr_mem);
        }
    }
#endif

    if (!ok) {
        STARTUPINFOW si; fill_startup(si);
        ok = do_create(base_flags, &si);
        if (!ok && (base_flags & CREATE_BREAKAWAY_FROM_JOB) &&
            GetLastError() == ERROR_ACCESS_DENIED) {
            ok = do_create(base_flags & ~CREATE_BREAKAWAY_FROM_JOB, &si);
        }
    }

    if (sr) CloseHandle(sr); if (ow) CloseHandle(ow); if (ew) CloseHandle(ew);
    if (nul_in  != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
    if (nul_out != INVALID_HANDLE_VALUE) CloseHandle(nul_out);
    if (nul_err != INVALID_HANDLE_VALUE) CloseHandle(nul_err);
    if (file_in  != INVALID_HANDLE_VALUE) CloseHandle(file_in);
    if (file_out != INVALID_HANDLE_VALUE) CloseHandle(file_out);
    if (file_err != INVALID_HANDLE_VALUE) CloseHandle(file_err);

    if (!ok) {
        DWORD e = GetLastError();
        p.error_code_native = (int32_t)e;
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) p.error = knst_process_error::NotFound;
        else if (e == ERROR_ACCESS_DENIED) p.error = knst_process_error::PermissionDenied;
        else p.error = knst_process_error::ExecFailed;
        if (sw) CloseHandle(sw); if (or_) CloseHandle(or_); if (er) CloseHandle(er);
        return p;
    }

    p.m_process_handle = pi.hProcess;
    p.m_thread_handle  = pi.hThread;
    p.m_pid            = pi.dwProcessId;
    p.m_stdin_write    = sw;
    p.m_stdout_read    = or_;
    p.m_stderr_read    = er;
    p.m_running.store(true);
    p.m_detached = opts.detached;


    if (opts.affinity_mask != 0) SetProcessAffinityMask(pi.hProcess, (DWORD_PTR)opts.affinity_mask);

    return p;
}

inline bool knst_process::wait(uint32_t timeout_ms) noexcept {
    HANDLE h = static_cast<HANDLE>(m_process_handle);
    if (!h) return false;
    if (!m_running.load()) return true;
    DWORD ms = (timeout_ms == 0) ? INFINITE : timeout_ms;
    DWORD r = WaitForSingleObject(h, ms);
    if (r == WAIT_OBJECT_0) {
        DWORD code = 0; GetExitCodeProcess(h, &code);
        m_exit_code.store((int)code); m_running.store(false); return true;
    }
    if (r == WAIT_TIMEOUT) { error = knst_process_error::Timeout; return false; }
    error = knst_process_error::WaitFailed;
    error_code_native = (int32_t)GetLastError();
    return false;
}

inline bool knst_process::is_running() noexcept {
    HANDLE h = static_cast<HANDLE>(m_process_handle);
    if (!h || !m_running.load()) return false;
    DWORD r = WaitForSingleObject(h, 0);
    if (r == WAIT_TIMEOUT) return true;
    if (r == WAIT_OBJECT_0) {
        DWORD code = 0; GetExitCodeProcess(h, &code);
        m_exit_code.store((int)code); m_running.store(false); return false;
    }
    return false;
}

inline bool knst_process::kill(int) noexcept {
    HANDLE h = static_cast<HANDLE>(m_process_handle);
    if (!h || !m_running.load()) return false;
    if (TerminateProcess(h, 1)) return true;
    DWORD e = GetLastError();
    error_code_native = (int32_t)e;
    error = (e == ERROR_ACCESS_DENIED) ? knst_process_error::PermissionDenied : knst_process_error::SignalFailed;
    return false;
}
inline bool knst_process::force_kill() noexcept { return kill(9); }

inline bool knst_process::kill_tree() noexcept {
    HANDLE h = static_cast<HANDLE>(m_process_handle);
    if (!h || !m_running.load()) return false;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        if (AssignProcessToJobObject(job, h)) { TerminateJobObject(job, 1); CloseHandle(job); return true; }
        CloseHandle(job);
    }
    return TerminateProcess(h, 1) != 0;
}

inline int knst_process::exit_code() noexcept { if (m_running.load()) is_running(); return m_exit_code.load(); }
inline uint32_t knst_process::pid() const noexcept { return m_pid; }
inline bool knst_process::was_signaled() const noexcept { return false; }
inline int  knst_process::term_signal() const noexcept { return 0; }

inline bool knst_process::wait_or_kill(uint32_t timeout_ms) noexcept {
    if (wait(timeout_ms)) return true;
    if (error == knst_process_error::Timeout) { force_kill(); wait(0); }
    return false;
}
inline bool knst_process::send_signal(int) noexcept { return kill(9); }

inline bool knst_process::suspend() noexcept {
    HANDLE h = static_cast<HANDLE>(m_process_handle);
    if (!h) return false;
    auto fn = knst_process_detail::GetNtSuspendProcess();
    if (!fn) { error = knst_process_error::NotSupported; return false; }
    return fn(h) == 0;
}
inline bool knst_process::resume() noexcept {
    HANDLE h = static_cast<HANDLE>(m_process_handle);
    if (!h) return false;
    bool any = false;

    HANDLE th = static_cast<HANDLE>(m_thread_handle);
    if (th && ResumeThread(th) != (DWORD)-1) any = true;

    auto fn = knst_process_detail::GetNtResumeProcess();
    if (fn && fn(h) == 0) any = true;
    if (!any) error = knst_process_error::NotSupported;
    return any;
}
inline bool knst_process::set_priority(int nice_or_class) noexcept {
    HANDLE h = static_cast<HANDLE>(m_process_handle);
    if (!h) return false;
    return SetPriorityClass(h, knst_process_detail::PriorityToWinClass(nice_or_class)) != 0;
}
inline bool knst_process::set_affinity(uint64_t mask) noexcept {
    HANDLE h = static_cast<HANDLE>(m_process_handle);
    if (!h) return false;
    return SetProcessAffinityMask(h, (DWORD_PTR)mask) != 0;
}
inline knst_process_info knst_process::get_info() noexcept { return knst_process::get_info(m_pid); }

inline bool knst_process::fill_line_buffer() noexcept {
    m_line_buf.clear(); m_line_pos = 0;
    HANDLE h = static_cast<HANDLE>(m_stdout_read);
    if (!h) return false;
    unsigned char tmp[4096]; DWORD n = 0;
    if (ReadFile(h, tmp, sizeof(tmp), &n, nullptr) && n > 0) {
        m_line_buf.append(tmp, (uint32_t)n); return true;
    }
    m_line_eof = true; return false;
}

inline knst_byte_string knst_process::read_stdout() noexcept {
    knst_byte_string result;
    if (m_line_pos < m_line_buf.length()) {
        result.append(m_line_buf.data() + m_line_pos, m_line_buf.length() - m_line_pos);
        m_line_buf.clear(); m_line_pos = 0;
    }
    HANDLE h = static_cast<HANDLE>(m_stdout_read);
    if (!h) return result;
    unsigned char buf[65536]; DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0)
        result.append(buf, (uint32_t)n);
    CloseHandle(h); m_stdout_read = nullptr; m_line_eof = true;
    return result;
}
inline knst_byte_string knst_process::read_stderr() noexcept {
    knst_byte_string result;
    HANDLE h = static_cast<HANDLE>(m_stderr_read);
    if (!h) return result;
    unsigned char buf[65536]; DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0)
        result.append(buf, (uint32_t)n);
    CloseHandle(h); m_stderr_read = nullptr;
    return result;
}
inline knst_c16string knst_process::read_stdout_text() noexcept {
    knst_byte_string d = read_stdout();
    if (d.empty() || d.data() == nullptr) return knst_c16string();
    knst_c16string result(reinterpret_cast<const char*>(d.data()), d.length());
    knst_process_detail::trim_trailing_newlines(result);
    return result;
}
inline knst_c16string knst_process::read_stderr_text() noexcept {
    knst_byte_string d = read_stderr();
    if (d.empty() || d.data() == nullptr) return knst_c16string();
    knst_c16string result(reinterpret_cast<const char*>(d.data()), d.length());
    knst_process_detail::trim_trailing_newlines(result);
    return result;
}

inline bool knst_process::read_line(knst_c16string& out) noexcept {
    out.clear();
    knst_byte_string line;
    for (;;) {
        uint32_t n = m_line_buf.length();
        while (m_line_pos < n) {
            unsigned char c = m_line_buf[m_line_pos++];
            if (c == '\n') {
                out = knst_c16string(reinterpret_cast<const char*>(line.data()), line.length());
                return true;
            }
            if (c != '\r') line.push_back(c);
        }
        if (m_line_eof) {
            if (line.empty()) return false;
            out = knst_c16string(reinterpret_cast<const char*>(line.data()), line.length());
            return true;
        }
        fill_line_buffer();
    }
}

inline bool knst_process::try_read_stdout(uint32_t timeout_ms, knst_byte_string& out) noexcept {
    out.clear();
    if (m_line_pos < m_line_buf.length()) {
        out.append(m_line_buf.data() + m_line_pos, m_line_buf.length() - m_line_pos);
        m_line_buf.clear(); m_line_pos = 0;
        return true;
    }
    HANDLE h = static_cast<HANDLE>(m_stdout_read);
    if (!h) return false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {
            CloseHandle(h); m_stdout_read = nullptr; m_line_eof = true; return false;
        }
        if (avail > 0) {
            unsigned char buf[65536];
            DWORD to_read = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail;
            DWORD n = 0;
            if (ReadFile(h, buf, to_read, &n, nullptr) && n > 0) { out.append(buf, (uint32_t)n); return true; }
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
inline bool knst_process::try_read_stderr(uint32_t timeout_ms, knst_byte_string& out) noexcept {
    out.clear();
    HANDLE h = static_cast<HANDLE>(m_stderr_read);
    if (!h) return false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {
            CloseHandle(h); m_stderr_read = nullptr; return false;
        }
        if (avail > 0) {
            unsigned char buf[65536];
            DWORD to_read = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail;
            DWORD n = 0;
            if (ReadFile(h, buf, to_read, &n, nullptr) && n > 0) { out.append(buf, (uint32_t)n); return true; }
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

inline bool knst_process::write_stdin(const void* data, size_t size) noexcept {
    HANDLE h = static_cast<HANDLE>(m_stdin_write);
    if (!h) return false;
    if (size == 0) return true;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    size_t total = 0;
    while (total < size) {
        DWORD to_write = (DWORD)((size - total) > (1u << 20) ? (1u << 20) : (size - total));
        DWORD written = 0;
        if (!WriteFile(h, p + total, to_write, &written, nullptr) || written == 0) return false;
        total += written;
    }
    return true;
}
inline bool knst_process::write_stdin(const knst_c16string& text) noexcept {
    knst_byte_string utf8(text);
    if (utf8.empty()) return true;
    return write_stdin(utf8.data(), utf8.length());
}
inline bool knst_process::close_stdin() noexcept {
    HANDLE h = static_cast<HANDLE>(m_stdin_write);
    if (!h) return true;
    bool ok = CloseHandle(h) != 0;
    m_stdin_write = nullptr;
    return ok;
}

inline knst_process_result knst_process::communicate(const void* stdin_data,size_t stdin_size,uint32_t timeout_ms) noexcept {
    using clock = std::chrono::steady_clock;
    knst_process_result result;


    std::thread stdin_writer;
    bool writer_started = false;
    if (stdin_data && stdin_size > 0) {
        writer_started = true;
        stdin_writer = std::thread([this, stdin_data, stdin_size]() {
            write_stdin(stdin_data, stdin_size);
            close_stdin();
        });
    } else {
        close_stdin();
    }
    if (m_line_pos < m_line_buf.length()) {
        result.out_data.append(m_line_buf.data() + m_line_pos, m_line_buf.length() - m_line_pos);
        m_line_buf.clear(); m_line_pos = 0;
    }
    HANDLE ho = static_cast<HANDLE>(m_stdout_read);
    HANDLE he = static_cast<HANDLE>(m_stderr_read);
    unsigned char buf[65536];
    const bool has_deadline = (timeout_ms != 0);
    auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    bool timed_out = false;
    while (ho || he) {
        if (has_deadline && clock::now() >= deadline) { timed_out = true; break; }
        bool progressed = false;
        if (ho) {
            DWORD avail = 0;
            if (!PeekNamedPipe(ho, nullptr, 0, nullptr, &avail, nullptr)) {
                CloseHandle(ho); ho = nullptr; m_stdout_read = nullptr; m_line_eof = true;
            } else if (avail > 0) {
                DWORD to_read = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail;
                DWORD n = 0;
                if (ReadFile(ho, buf, to_read, &n, nullptr) && n > 0) { result.out_data.append(buf, (uint32_t)n); progressed = true; }
                else { CloseHandle(ho); ho = nullptr; m_stdout_read = nullptr; m_line_eof = true; }
            }
        }
        if (he) {
            DWORD avail = 0;
            if (!PeekNamedPipe(he, nullptr, 0, nullptr, &avail, nullptr)) {
                CloseHandle(he); he = nullptr; m_stderr_read = nullptr;
            } else if (avail > 0) {
                DWORD to_read = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail;
                DWORD n = 0;
                if (ReadFile(he, buf, to_read, &n, nullptr) && n > 0) { result.err_data.append(buf, (uint32_t)n); progressed = true; }
                else { CloseHandle(he); he = nullptr; m_stderr_read = nullptr; }
            }
        }
        if (!progressed && (ho || he)) {
            if (has_deadline && clock::now() >= deadline) { timed_out = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (timed_out) {
        force_kill(); wait(0);
        auto drain_deadline = clock::now() + std::chrono::milliseconds(500);
        while (ho && clock::now() < drain_deadline) {
            DWORD avail = 0;
            if (!PeekNamedPipe(ho, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            DWORD n = 0;
            if (ReadFile(ho, buf, sizeof(buf), &n, nullptr) && n > 0) result.out_data.append(buf, (uint32_t)n);
            else break;
        }
        while (he && clock::now() < drain_deadline) {
            DWORD avail = 0;
            if (!PeekNamedPipe(he, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            DWORD n = 0;
            if (ReadFile(he, buf, sizeof(buf), &n, nullptr) && n > 0) result.err_data.append(buf, (uint32_t)n);
            else break;
        }
        if (ho) { CloseHandle(ho); m_stdout_read = nullptr; }
        if (he) { CloseHandle(he); m_stderr_read = nullptr; }
        result.timed_out = true;
        result.error = knst_process_error::Timeout;
    } else {
        uint32_t wait_ms = 0;
        if (has_deadline) {
            auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
            wait_ms = rem > 0 ? (uint32_t)rem : 1;
        }
        if (!wait(wait_ms)) {
            if (error == knst_process_error::Timeout) {
                force_kill(); wait(0);
                result.timed_out = true;
                result.error = knst_process_error::Timeout;
            }
        }
    }
    
    result.exit_code = m_exit_code.load();
    if (!result.timed_out) result.error = error;

    knst_process_detail::trim_trailing_newlines(result.out_data);
    knst_process_detail::trim_trailing_newlines(result.err_data);

    if (writer_started && stdin_writer.joinable()) {
        stdin_writer.join();
    }

    return result;
}

inline void knst_process::cleanup() noexcept {
    if (m_stdin_write)  { CloseHandle(static_cast<HANDLE>(m_stdin_write));  m_stdin_write  = nullptr; }
    if (m_stdout_read)  { CloseHandle(static_cast<HANDLE>(m_stdout_read));  m_stdout_read  = nullptr; }
    if (m_stderr_read)  { CloseHandle(static_cast<HANDLE>(m_stderr_read));  m_stderr_read  = nullptr; }
    m_line_buf.clear(); m_line_pos = 0; m_line_eof = false;
    HANDLE h = static_cast<HANDLE>(m_process_handle);
    if (h) {
        if (m_running.load() && !m_detached) {
            TerminateProcess(h, 1);
            WaitForSingleObject(h, 1000);
            m_running.store(false);
        }
        CloseHandle(h); m_process_handle = nullptr;
    }
    if (m_thread_handle) { CloseHandle(static_cast<HANDLE>(m_thread_handle)); m_thread_handle = nullptr; }
    m_pid = 0;
}

inline void knst_process::move_from(knst_process& other) noexcept {
    m_process_handle = other.m_process_handle;
    m_thread_handle  = other.m_thread_handle;
    m_stdin_write    = other.m_stdin_write;
    m_stdout_read    = other.m_stdout_read;
    m_stderr_read    = other.m_stderr_read;
    m_pid            = other.m_pid;
    m_detached       = other.m_detached;
    m_line_buf       = std::move(other.m_line_buf);
    m_line_pos       = other.m_line_pos;
    m_line_eof       = other.m_line_eof;
    other.m_process_handle = nullptr; other.m_thread_handle = nullptr;
    other.m_stdin_write = nullptr; other.m_stdout_read = nullptr; other.m_stderr_read = nullptr;
    other.m_pid = 0; other.m_detached = false;
    other.m_line_pos = 0; other.m_line_eof = false;
    m_program_name = std::move(other.m_program_name);
    m_exit_code.store(other.m_exit_code.load());
    m_running.store(other.m_running.load());
    error = other.error;
    error_code_native = other.error_code_native;
    other.m_running.store(false);
    other.m_exit_code.store(-1);
    other.error = knst_process_error::None;
    other.error_code_native = 0;
}

inline knst_vector<uint32_t> knst_process::list_pids() noexcept {
    knst_vector<uint32_t> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do { out.push_back((uint32_t)pe.th32ProcessID); } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}


inline knst_vector<knst_process_info> knst_process::list_all(bool include_gpu) noexcept {
    knst_vector<knst_process_info> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    knst_vector<knst_process_detail::gpu_proc_entry> gpu_entries;
    if (include_gpu) gpu_entries = knst_process_detail::collect_gpu_processes();

    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
                       knst_process_info info = knst_process::get_info((uint32_t)pe.th32ProcessID);
            if (info.pid == 0 || info.name.empty()) {
                info.pid  = (uint32_t)pe.th32ProcessID;
                info.ppid = (uint32_t)pe.th32ParentProcessID;
                if (info.name.empty())
                    info.name = knst_c16string(reinterpret_cast<const char16_t*>(pe.szExeFile));
                if (info.thread_count == 0) info.thread_count = pe.cntThreads;
            }
            if (include_gpu && !gpu_entries.empty()) {
                for (uint32_t j = 0; j < gpu_entries.size(); ++j) {
                    if (gpu_entries[j].pid == info.pid) {
                        info.gpu.valid       = true;
                        info.gpu.gpu_index   = gpu_entries[j].gpu_index;
                        info.gpu.vram_bytes  = gpu_entries[j].vram_bytes;
                        info.gpu.vendor      = gpu_entries[j].vendor;
                        break;
                    }
                }
            }
            out.push_back(info);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

inline knst_vector<knst_process_info> knst_process::find_by_name(const knst_c16string& name,
                                                                  bool exact) noexcept {
    knst_vector<knst_process_info> out;
    knst_vector<knst_process_info> all = list_all(false);
    for (uint32_t i = 0; i < all.size(); ++i) {
        bool m = exact ? (all[i].name == name) : knst_process_detail::iequals_ascii(all[i].name, name);
        if (!m && !all[i].exe_path.empty()) {
            knst_c16string base = knst_process_detail::base_name_of(all[i].exe_path);
            m = exact ? (base == name) : knst_process_detail::iequals_ascii(base, name);
        }
        if (m) out.push_back(all[i]);
    }
    return out;
}

inline knst_vector<uint32_t> knst_process::children_of(uint32_t ppid) noexcept {
    knst_vector<uint32_t> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if ((uint32_t)pe.th32ParentProcessID == ppid)
                out.push_back((uint32_t)pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

inline knst_vector<knst_process_info> knst_process::descendants_of(uint32_t pid) noexcept {
    knst_vector<knst_process_info> out;
    knst_vector<uint32_t> stack; stack.push_back(pid);
    while (stack.size() > 0) {
        uint32_t cur = stack[stack.size() - 1]; stack.pop_back();
        knst_vector<uint32_t> kids = children_of(cur);
        for (uint32_t i = 0; i < kids.size(); ++i) {
            knst_process_info info = get_info(kids[i]);
            if (info.pid != 0) { out.push_back(info); stack.push_back(kids[i]); }
        }
    }
    return out;
}

inline bool knst_process::exists(uint32_t pid) noexcept {
    if (pid == 0) return false;
   
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (h) { CloseHandle(h); return true; }
    
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;


    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);


    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if ((uint32_t)pe.th32ProcessID == pid) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}
inline bool knst_process::is_zombie(uint32_t) noexcept { return false; }
inline bool knst_process::is_alive(uint32_t pid) noexcept { return exists(pid); }

inline knst_process_info knst_process::get_info(uint32_t pid) noexcept {
    knst_process_info info;
    info.pid = pid;
    if (pid == 0) return info;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

    wchar_t path[MAX_PATH * 4];
    DWORD sz = sizeof(path) / sizeof(path[0]);
    if (QueryFullProcessImageNameW(h, 0, path, &sz)) {
        info.exe_path = knst_c16string(reinterpret_cast<const char16_t*>(path), (uint32_t)sz);
        info.has_exe = true;
        info.name = knst_process_detail::base_name_of(info.exe_path);
    }
    {
        BOOL wow = FALSE;
        bool wow_ok = IsWow64Process(h, &wow) != 0;
#if defined(_WIN64)
        bool target_wow64 = wow_ok && wow;
#else
        bool target_wow64 = false;
#endif
        knst_c16string cmd = knst_process_detail::ReadCmdlineFromPEB(h, target_wow64);
        if (!cmd.empty()) { info.cmdline = std::move(cmd); info.has_cmdline = true; }
        else if (info.has_exe) info.cmdline = info.exe_path;
    }
    PROCESS_MEMORY_COUNTERS pmc = {}; pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
        info.rss_bytes = (uint64_t)pmc.WorkingSetSize;
        info.vsz_bytes = (uint64_t)pmc.PagefileUsage;
        info.has_memory = true;
    }
    FILETIME ct, et, kt, ut;
    if (GetProcessTimes(h, &ct, &et, &kt, &ut)) {
        ULARGE_INTEGER u;
        u.LowPart = ut.dwLowDateTime; u.HighPart = ut.dwHighDateTime;
        info.cpu_user_ms = u.QuadPart / 10000ULL;
        u.LowPart = kt.dwLowDateTime; u.HighPart = kt.dwHighDateTime;
        info.cpu_system_ms = u.QuadPart / 10000ULL;
        ULARGE_INTEGER c;
        c.LowPart = ct.dwLowDateTime; c.HighPart = ct.dwHighDateTime;
        uint64_t epoch = c.QuadPart / 10000ULL;
        const uint64_t EPOCH_DIFF = 11644473600000ULL;
        info.start_time_ms = epoch > EPOCH_DIFF ? epoch - EPOCH_DIFF : 0;
        info.has_cpu = true;
    }
    DWORD prio = GetPriorityClass(h);
    switch (prio) {
        case IDLE_PRIORITY_CLASS:         info.nice = 19;  break;
        case BELOW_NORMAL_PRIORITY_CLASS: info.nice = 10;  break;
        case NORMAL_PRIORITY_CLASS:       info.nice = 0;   break;
        case ABOVE_NORMAL_PRIORITY_CLASS: info.nice = -5;  break;
        case HIGH_PRIORITY_CLASS:         info.nice = -15; break;
        case REALTIME_PRIORITY_CLASS:     info.nice = -20; break;
        default: break;
    }

    {
        knst_c16string uname;
        if (knst_process_detail::GetProcessUserName(h, uname)) {
            info.user = uname;
            info.has_user = true;
        }
    }

    CloseHandle(h);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ProcessID == pid) {
                    info.ppid = (uint32_t)pe.th32ParentProcessID;
                    info.thread_count = pe.cntThreads;
                    if (info.name.empty())
                        info.name = knst_c16string(reinterpret_cast<const char16_t*>(pe.szExeFile));
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    info.state = knst_process_state::Running;
    return info;
}

inline knst_c16string knst_process::get_exe_path(uint32_t pid) noexcept { return get_info(pid).exe_path; }
inline knst_c16string knst_process::get_cmdline(uint32_t pid) noexcept { return get_info(pid).cmdline; }
inline knst_c16string knst_process::get_cwd(uint32_t) noexcept { return knst_c16string(); }
inline knst_c16string knst_process::get_user(uint32_t) noexcept { return knst_c16string(); }
inline uint32_t knst_process::get_uid(uint32_t) noexcept { return 0; }
inline uint32_t knst_process::get_ppid(uint32_t pid) noexcept { return get_info(pid).ppid; }
inline int32_t  knst_process::get_nice(uint32_t pid) noexcept { return get_info(pid).nice; }
inline knst_process_state knst_process::get_state(uint32_t pid) noexcept {
    return exists(pid) ? knst_process_state::Running : knst_process_state::Dead;
}
inline uint64_t knst_process::get_rss_bytes(uint32_t pid) noexcept { return get_info(pid).rss_bytes; }
inline uint64_t knst_process::get_vsz_bytes(uint32_t pid) noexcept { return get_info(pid).vsz_bytes; }

inline bool knst_process::get_cpu_times(uint32_t pid, uint64_t& u, uint64_t& s) noexcept {
    knst_process_info i = get_info(pid); u = i.cpu_user_ms; s = i.cpu_system_ms; return i.has_cpu;
}
inline bool knst_process::get_memory(uint32_t pid, uint64_t& rss, uint64_t& vsz) noexcept {
    knst_process_info i = get_info(pid); rss = i.rss_bytes; vsz = i.vsz_bytes; return i.has_memory;
}

inline knst_vector<uint32_t> knst_process::get_thread_ids(uint32_t pid) noexcept {
    knst_vector<uint32_t> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    THREADENTRY32 te = {}; te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid)
                out.push_back((uint32_t)te.th32ThreadID);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return out;
}
inline knst_vector<knst_thread_info> knst_process::get_threads(uint32_t pid) noexcept {
    knst_vector<knst_thread_info> out;
    knst_vector<uint32_t> ids = get_thread_ids(pid);
    out.reserve(ids.size());
    for (uint32_t i = 0; i < ids.size(); ++i) {
        knst_thread_info ti; ti.tid = ids[i];
        out.push_back(ti);
    }
    return out;
}
inline knst_vector<knst_fd_info> knst_process::get_fds(uint32_t) noexcept { return {}; }
inline knst_vector<knst_env_var> knst_process::get_env(uint32_t) noexcept { return {}; }

inline bool knst_process::kill_by_pid(uint32_t pid, int) noexcept {
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) return false;
    bool ok = TerminateProcess(h, 1) != 0;
    CloseHandle(h);
    return ok;
}
inline bool knst_process::kill_by_name(const knst_c16string& name, int sig, bool exact) noexcept {
    knst_vector<knst_process_info> found = find_by_name(name, exact);
    bool any = false;
    for (uint32_t i = 0; i < found.size(); ++i)
        if (kill_by_pid(found[i].pid, sig)) any = true;
    return any;
}
inline bool knst_process::kill_tree(uint32_t pid, int) noexcept {
    knst_vector<knst_process_info> desc = descendants_of(pid);
    for (uint32_t i = desc.size(); i-- > 0; ) kill_by_pid(desc[i].pid, 9);
    return kill_by_pid(pid, 9);
}
inline bool knst_process::kill_children(uint32_t pid, int sig) noexcept {
    knst_vector<uint32_t> kids = children_of(pid);
    bool any = false;
    for (uint32_t i = 0; i < kids.size(); ++i)
        if (kill_by_pid(kids[i], sig)) any = true;
    return any;
}
inline bool knst_process::terminate_graceful(uint32_t pid, uint32_t timeout_ms) noexcept {
    if (!exists(pid)) return false;
    kill_by_pid(pid, 15);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!exists(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return kill_by_pid(pid, 9);
}
inline bool knst_process::suspend_pid(uint32_t pid) noexcept {
    HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (!h) return false;
    auto fn = knst_process_detail::GetNtSuspendProcess();
    bool ok = fn ? (fn(h) == 0) : false;
    CloseHandle(h);
    return ok;
}
inline bool knst_process::resume_pid(uint32_t pid) noexcept {
    HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (!h) return false;
    auto fn = knst_process_detail::GetNtResumeProcess();
    bool ok = fn ? (fn(h) == 0) : false;
    CloseHandle(h);
    return ok;
}
inline bool knst_process::set_priority(uint32_t pid, int nice_or_class) noexcept {
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return false;
    bool ok = SetPriorityClass(h, knst_process_detail::PriorityToWinClass(nice_or_class)) != 0;
    CloseHandle(h);
    return ok;
}
inline bool knst_process::set_affinity(uint32_t pid, uint64_t mask) noexcept {
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return false;
    bool ok = SetProcessAffinityMask(h, (DWORD_PTR)mask) != 0;
    CloseHandle(h);
    return ok;
}

inline uint32_t knst_process::current_pid() noexcept { return (uint32_t)GetCurrentProcessId(); }
inline uint32_t knst_process::parent_pid() noexcept {
    uint32_t my_pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    uint32_t result = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == my_pid) { result = (uint32_t)pe.th32ParentProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return result;
}
inline uint32_t knst_process::current_uid() noexcept { return 0; }
inline knst_c16string knst_process::current_user() noexcept {
    wchar_t buf[256]; DWORD sz = sizeof(buf) / sizeof(buf[0]);
    if (GetUserNameW(buf, &sz))
        return knst_c16string(reinterpret_cast<const char16_t*>(buf), (uint32_t)(sz - 1));
    return knst_c16string();
}
inline knst_c16string knst_process::current_exe_path() noexcept {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, sizeof(buf) / sizeof(buf[0]));
    if (n > 0) return knst_c16string(reinterpret_cast<const char16_t*>(buf), (uint32_t)n);
    return knst_c16string();
}

inline knst_process::system_info knst_process::get_system_info() noexcept {
    system_info si;
    MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        si.total_memory = ms.ullTotalPhys;
        si.free_memory  = ms.ullAvailPhys;
        si.used_memory  = si.total_memory - si.free_memory;
    }
    SYSTEM_INFO si2; GetSystemInfo(&si2);
    si.cpu_count = si2.dwNumberOfProcessors;
    si.page_size = si2.dwPageSize;
    si.uptime_ms = GetTickCount64();
    return si;
}

#endif // KNST_USING_PLATFORM_WINDOWS



inline knst_process_result knst_process::run_capture(const knst_c16string& program,const knst_vector<knst_c16string>& args,const knst_process_options& opts,const void* stdin_data,size_t stdin_size,uint32_t timeout_ms) noexcept {
                                                      
    knst_process p = run(program, args, opts);
    if (p.error != knst_process_error::None) {
        knst_process_result r;
        r.exit_code = p.m_exit_code.load();
        r.error = p.error;
        return r;
    }
    return p.communicate(stdin_data, stdin_size, timeout_ms);
}



namespace knst_gpu_detail {

#if KNST_USING_PLATFORM_WINDOWS
    using gpu_lib_t = HMODULE;
    inline gpu_lib_t gpu_load_lib(const char* name) noexcept { return ::LoadLibraryA(name); }
    inline void* gpu_sym(gpu_lib_t h, const char* name) noexcept {
        return h ? (void*)::GetProcAddress(h, name) : nullptr;
    }
    inline void gpu_unload_lib(gpu_lib_t h) noexcept { if (h) ::FreeLibrary(h); }
#else
    using gpu_lib_t = void*;
    inline gpu_lib_t gpu_load_lib(const char* name) noexcept {
        return ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
    }
    inline void* gpu_sym(gpu_lib_t h, const char* name) noexcept { return h ? ::dlsym(h, name) : nullptr; }
    inline void gpu_unload_lib(gpu_lib_t h) noexcept { if (h) ::dlclose(h); }
#endif

struct nvml_memory_t { unsigned long long total, free, used; };
struct nvml_utilization_t { unsigned int gpu, memory; };
struct nvml_proc_v1_t { unsigned int pid; unsigned long long usedGpuMemory; };
struct nvml_proc_v2_t {
    unsigned int pid; unsigned long long usedGpuMemory;
    unsigned int gpuInstanceId; unsigned int computeInstanceId;
};

struct knst_nvml_api {
    gpu_lib_t lib = nullptr;
    typedef int (*init_t)();
    typedef int (*shutdown_t)();
    typedef int (*get_count_t)(unsigned int*);
    typedef int (*get_handle_t)(unsigned int, void**);
    typedef int (*get_name_t)(void*, char*, unsigned int);
    typedef int (*get_uuid_t)(void*, char*, unsigned int);
    typedef int (*get_driver_t)(char*, unsigned int);
    typedef int (*get_mem_t)(void*, void*);
    typedef int (*get_util_t)(void*, void*);
    typedef int (*get_temp_t)(void*, int, unsigned int*);
    typedef int (*get_power_t)(void*, unsigned int*);
    typedef int (*get_power_lim_t)(void*, unsigned int*);
    typedef int (*get_clock_t)(void*, int, unsigned int*);
    typedef int (*get_fan_t)(void*, unsigned int*);
    typedef int (*get_pcie_gen_t)(void*, unsigned int*);
    typedef int (*get_pcie_w_t)(void*, unsigned int*);
    typedef int (*get_vbios_t)(void*, char*, unsigned int);
    typedef int (*get_cuda_t)(int*);
    typedef int (*get_procs_v1_t)(void*, unsigned int*, void*);
    typedef int (*get_procs_v2_t)(void*, unsigned int*, void*);

    init_t init = nullptr;
    shutdown_t shutdown = nullptr;
    get_count_t get_count = nullptr;
    get_handle_t get_handle = nullptr;
    get_name_t get_name = nullptr;
    get_uuid_t get_uuid = nullptr;
    get_driver_t get_driver = nullptr;
    get_mem_t get_mem = nullptr;
    get_util_t get_util = nullptr;
    get_temp_t get_temp = nullptr;
    get_power_t get_power = nullptr;
    get_power_lim_t get_power_lim = nullptr;
    get_clock_t get_clock = nullptr;
    get_fan_t get_fan = nullptr;
    get_pcie_gen_t get_pcie_gen = nullptr;
    get_pcie_w_t get_pcie_w = nullptr;
    get_vbios_t get_vbios = nullptr;
    get_cuda_t get_cuda = nullptr;
    get_procs_v1_t get_comp_v1 = nullptr;
    get_procs_v1_t get_gfx_v1 = nullptr;
    get_procs_v2_t get_comp_v2 = nullptr;
    get_procs_v2_t get_gfx_v2 = nullptr;
    bool ready_flag = false;

    bool load_impl() noexcept {
#if KNST_USING_PLATFORM_WINDOWS
        lib = gpu_load_lib("nvml.dll");
        if (!lib) lib = gpu_load_lib("C:\\Windows\\System32\\nvml.dll");
        if (!lib) lib = gpu_load_lib("C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
#else
        lib = gpu_load_lib("libnvidia-ml.so.1");
        if (!lib) lib = gpu_load_lib("libnvidia-ml.so");
#endif
        if (!lib) return false;

        init = (init_t)gpu_sym(lib, "nvmlInit_v2");
        if (!init) init = (init_t)gpu_sym(lib, "nvmlInit");
        shutdown = (shutdown_t)gpu_sym(lib, "nvmlShutdown");
        get_count = (get_count_t)gpu_sym(lib, "nvmlDeviceGetCount_v2");
        if (!get_count) get_count = (get_count_t)gpu_sym(lib, "nvmlDeviceGetCount");
        get_handle = (get_handle_t)gpu_sym(lib, "nvmlDeviceGetHandleByIndex_v2");
        if (!get_handle) get_handle = (get_handle_t)gpu_sym(lib, "nvmlDeviceGetHandleByIndex");
        get_name = (get_name_t)gpu_sym(lib, "nvmlDeviceGetName");
        get_uuid = (get_uuid_t)gpu_sym(lib, "nvmlDeviceGetUUID");
        get_driver = (get_driver_t)gpu_sym(lib, "nvmlSystemGetDriverVersion");
        get_mem = (get_mem_t)gpu_sym(lib, "nvmlDeviceGetMemoryInfo");
        get_util = (get_util_t)gpu_sym(lib, "nvmlDeviceGetUtilizationRates");
        get_temp = (get_temp_t)gpu_sym(lib, "nvmlDeviceGetTemperature");
        get_power = (get_power_t)gpu_sym(lib, "nvmlDeviceGetPowerUsage");
        get_power_lim = (get_power_lim_t)gpu_sym(lib, "nvmlDeviceGetPowerManagementLimit");
        get_clock = (get_clock_t)gpu_sym(lib, "nvmlDeviceGetClockInfo");
        get_fan = (get_fan_t)gpu_sym(lib, "nvmlDeviceGetFanSpeed");
        get_pcie_gen = (get_pcie_gen_t)gpu_sym(lib, "nvmlDeviceGetCurrPcieLinkGeneration");
        get_pcie_w = (get_pcie_w_t)gpu_sym(lib, "nvmlDeviceGetCurrPcieLinkWidth");
        get_vbios = (get_vbios_t)gpu_sym(lib, "nvmlDeviceGetVbiosVersion");
        get_cuda = (get_cuda_t)gpu_sym(lib, "nvmlSystemGetCudaDriverVersion_v2");
        if (!get_cuda) get_cuda = (get_cuda_t)gpu_sym(lib, "nvmlSystemGetCudaDriverVersion");
        get_comp_v2 = (get_procs_v2_t)gpu_sym(lib, "nvmlDeviceGetComputeRunningProcesses_v2");
        get_gfx_v2 = (get_procs_v2_t)gpu_sym(lib, "nvmlDeviceGetGraphicsRunningProcesses_v2");
        if (!get_comp_v2) get_comp_v1 = (get_procs_v1_t)gpu_sym(lib, "nvmlDeviceGetComputeRunningProcesses");
        if (!get_gfx_v2) get_gfx_v1 = (get_procs_v1_t)gpu_sym(lib, "nvmlDeviceGetGraphicsRunningProcesses");

        if (!init || !get_count || !get_handle || !get_mem) {
            gpu_unload_lib(lib); lib = nullptr; return false;
        }
        if (init() != 0) { gpu_unload_lib(lib); lib = nullptr; return false; }
        return true;
    }

    ~knst_nvml_api() {
        if (ready_flag && shutdown) shutdown();
        if (lib) gpu_unload_lib(lib);
    }
};

inline knst_nvml_api& nvml() noexcept { static knst_nvml_api api; return api; }

inline bool nvml_ready() noexcept {
    static std::once_flag flag;
    static bool ready = false;
    std::call_once(flag, []() {
        ready = nvml().load_impl();
        nvml().ready_flag = ready;
    });
    return ready;
}


#if defined(__linux__) && !KNST_USING_PLATFORM_WINDOWS
inline bool sysfs_read_str(const char* path, char* buf, size_t sz) noexcept {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = ::read(fd, buf, sz - 1);
    ::close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' ')) buf[--n] = '\0';
    return true;
}
inline bool sysfs_read_u64(const char* path, uint64_t& out) noexcept {
    char buf[64];
    if (!sysfs_read_str(path, buf, sizeof(buf))) return false;
    out = ::strtoull(buf, nullptr, 10);
    return true;
}
inline bool sysfs_read_i32(const char* path, int32_t& out) noexcept {
    char buf[64];
    if (!sysfs_read_str(path, buf, sizeof(buf))) return false;
    out = (int32_t)::strtol(buf, nullptr, 10);
    return true;
}
inline knst_gpu_vendor vendor_from_pci_id(uint32_t v) noexcept {
    switch (v) {
        case 0x10DE: return knst_gpu_vendor::NVIDIA;
        case 0x1002:
        case 0x1022: return knst_gpu_vendor::AMD;
        case 0x8086: return knst_gpu_vendor::Intel;
        case 0x106B: return knst_gpu_vendor::Apple;
        default:     return knst_gpu_vendor::Other;
    }
}
#endif

} // namespace knst_gpu_detail



namespace knst_process_detail {

    

inline knst_vector<gpu_proc_entry> collect_gpu_processes() noexcept {
    knst_vector<gpu_proc_entry> out;



    if (knst_gpu_detail::nvml_ready()) {
        auto& a = knst_gpu_detail::nvml();
        unsigned int gpu_count = 0;
        if (a.get_count(&gpu_count) == 0) {
            for (unsigned int gi = 0; gi < gpu_count; ++gi) {
                void* dev = nullptr;
                if (a.get_handle(gi, &dev) != 0 || !dev) continue;

                const unsigned int MAXP = 128;

                 
                constexpr uint64_t NVML_SENTINEL_MIN = 0xFFFFFFFFFFFFULL;
                auto safe_vram = [](uint64_t raw) -> uint64_t {
                    return (raw >= NVML_SENTINEL_MIN) ? 0ULL : raw;
                };
                auto emit_v2 = [&](const knst_gpu_detail::nvml_proc_v2_t& p) {
                    gpu_proc_entry entry;
                    entry.pid = p.pid;
                    entry.gpu_index = gi;
                    entry.vram_bytes = safe_vram(p.usedGpuMemory);
                    entry.vendor = knst_gpu_vendor::NVIDIA;
                    out.push_back(entry);
                };
                auto emit_v1 = [&](const knst_gpu_detail::nvml_proc_v1_t& p) {
                    gpu_proc_entry entry;
                    entry.pid = p.pid;
                    entry.gpu_index = gi;
                    entry.vram_bytes = safe_vram(p.usedGpuMemory);
                    entry.vendor = knst_gpu_vendor::NVIDIA;
                    out.push_back(entry);
                };

                if (a.get_comp_v2) {
                    knst_gpu_detail::nvml_proc_v2_t arr[MAXP] = {};
                    unsigned int cnt = MAXP;
                    if (a.get_comp_v2(dev, &cnt, arr) == 0) {
                        if (cnt > MAXP) cnt = MAXP;
                        for (unsigned int k = 0; k < cnt; ++k) emit_v2(arr[k]);
                    }
                } else if (a.get_comp_v1) {
                    knst_gpu_detail::nvml_proc_v1_t arr[MAXP] = {};
                    unsigned int cnt = MAXP;
                    if (a.get_comp_v1(dev, &cnt, arr) == 0) {
                        if (cnt > MAXP) cnt = MAXP;
                        for (unsigned int k = 0; k < cnt; ++k) emit_v1(arr[k]);
                    }
                }
                if (a.get_gfx_v2) {
                    knst_gpu_detail::nvml_proc_v2_t arr[MAXP] = {};
                    unsigned int cnt = MAXP;
                    if (a.get_gfx_v2(dev, &cnt, arr) == 0) {
                        if (cnt > MAXP) cnt = MAXP;
                        for (unsigned int k = 0; k < cnt; ++k) emit_v2(arr[k]);
                    }
                } else if (a.get_gfx_v1) {
                    knst_gpu_detail::nvml_proc_v1_t arr[MAXP] = {};
                    unsigned int cnt = MAXP;
                    if (a.get_gfx_v1(dev, &cnt, arr) == 0) {
                        if (cnt > MAXP) cnt = MAXP;
                        for (unsigned int k = 0; k < cnt; ++k) emit_v1(arr[k]);
                    }
                }
            }
        }
    }

#if defined(__linux__) && !KNST_USING_PLATFORM_WINDOWS

    {
        DIR* d = ::opendir("/proc");
        if (d) {
            struct dirent* e;
            while ((e = ::readdir(d)) != nullptr) {
                const char* n = e->d_name;
                bool numeric = true;
                for (const char* q = n; *q; ++q) if (*q < '0' || *q > '9') { numeric = false; break; }
                if (!numeric || !*n) continue;
                uint32_t pid = (uint32_t)::strtoul(n, nullptr, 10);
                if (pid == 0) continue;


                bool already = false;
                for (uint32_t k = 0; k < out.size(); ++k) {
                    if (out[k].pid == pid && out[k].vendor == knst_gpu_vendor::NVIDIA) {
                        already = true; break;
                    }
                }
                if (already) continue;

                char dir[64];
                ::snprintf(dir, sizeof(dir), "/proc/%u/fdinfo", pid);
                DIR* df = ::opendir(dir);
                if (!df) continue;
                uint64_t vram_total = 0;
                struct dirent* fe;
                while ((fe = ::readdir(df)) != nullptr) {
                    if (fe->d_name[0] == '.') continue;
                    char fpath[128];
                    ::snprintf(fpath, sizeof(fpath), "%s/%s", dir, fe->d_name);
                    int fd = ::open(fpath, O_RDONLY);
                    if (fd < 0) continue;
                    char buf[512];
                    ssize_t rn = ::read(fd, buf, sizeof(buf) - 1);
                    ::close(fd);
                    if (rn <= 0) continue;
                    buf[rn] = '\0';
                    const char* p = ::strstr(buf, "drm-memory-vram:");
                    if (p) {
                        p += 17;
                        while (*p == ' ' || *p == '\t') ++p;
                        unsigned long long kb = ::strtoull(p, nullptr, 10);
                        vram_total += kb * 1024ULL;
                    }
                }
                ::closedir(df);
                if (vram_total > 0) {
                                        gpu_proc_entry entry;
                    entry.pid = pid;
                    entry.vram_bytes = vram_total;
                    entry.vendor = knst_gpu_vendor::AMD;
                    out.push_back(entry);
                }
            }
            ::closedir(d);
        }
    }
#endif

    return out;
}

} // namespace knst_process_detail



struct knst_gpu_info {
    uint32_t index = 0;
    knst_c16string name;
    knst_c16string vendor_name;
    knst_c16string driver_version;
    knst_c16string uuid;
    knst_c16string vbios_version;
    knst_gpu_vendor vendor = knst_gpu_vendor::Unknown;

    uint64_t memory_total = 0;
    uint64_t memory_used  = 0;
    uint64_t memory_free  = 0;

    uint32_t utilization_gpu = 0;
    uint32_t utilization_mem = 0;

    int32_t  temperature_core = 0;

    uint32_t power_usage_mw = 0;
    uint32_t power_limit_mw = 0;

    uint32_t clock_core_mhz = 0;
    uint32_t clock_mem_mhz  = 0;

    uint32_t fan_percent = 0;

    uint32_t pcie_gen = 0;
    uint32_t pcie_width = 0;

    bool valid           = false;
    bool has_memory      = false;
    bool has_utilization = false;
    bool has_temperature = false;
    bool has_power       = false;
    bool has_clocks      = false;
    bool has_fan         = false;
    bool has_pcie        = false;
    bool has_driver_info = false;
    bool has_vbios       = false;
};

struct knst_gpu_process {
    uint32_t pid = 0;
    uint32_t gpu_index = 0;
    knst_c16string name;
    uint64_t vram_bytes = 0;
    knst_gpu_vendor vendor = knst_gpu_vendor::Unknown;
};

namespace knst_gpu_detail {

inline bool fill_from_nvml(uint32_t idx, knst_gpu_info& info) noexcept {
    knst_nvml_api& a = nvml();
    if (!nvml_ready()) return false;
    void* dev = nullptr;
    if (a.get_handle(idx, &dev) != 0 || !dev) return false;

    info.index = idx;
    info.vendor = knst_gpu_vendor::NVIDIA;
    info.vendor_name = u"NVIDIA";

    char buf[256];
    if (a.get_name && a.get_name(dev, buf, sizeof(buf)) == 0) info.name = knst_c16string(buf);
    if (a.get_uuid && a.get_uuid(dev, buf, sizeof(buf)) == 0) info.uuid = knst_c16string(buf);
    if (a.get_vbios && a.get_vbios(dev, buf, sizeof(buf)) == 0) {
        info.vbios_version = knst_c16string(buf);
        info.has_vbios = true;
    }
    if (a.get_driver) {
        char dv[80];
        if (a.get_driver(dv, sizeof(dv)) == 0) {
            info.driver_version = knst_c16string(dv);
            info.has_driver_info = true;
        }
    }
    nvml_memory_t mem = {};
    if (a.get_mem(dev, &mem) == 0) {
        info.memory_total = mem.total; info.memory_used = mem.used; info.memory_free = mem.free;
        info.has_memory = true;
    }
    nvml_utilization_t ut = {};
    if (a.get_util && a.get_util(dev, &ut) == 0) {
        info.utilization_gpu = ut.gpu; info.utilization_mem = ut.memory;
        info.has_utilization = true;
    }
    if (a.get_temp) {
        unsigned int t = 0;
        if (a.get_temp(dev, 0, &t) == 0) { info.temperature_core = (int32_t)t; info.has_temperature = true; }
    }
    if (a.get_power) {
        unsigned int p = 0;
        if (a.get_power(dev, &p) == 0) { info.power_usage_mw = p; info.has_power = true; }
    }
    if (a.get_power_lim) { unsigned int p = 0; if (a.get_power_lim(dev, &p) == 0) info.power_limit_mw = p; }
    if (a.get_clock) {
        unsigned int c = 0; bool any = false;
        if (a.get_clock(dev, 0, &c) == 0) { info.clock_core_mhz = c; any = true; }
        if (a.get_clock(dev, 2, &c) == 0) { info.clock_mem_mhz  = c; any = true; }
        info.has_clocks = any;
    }
    if (a.get_fan) {
        unsigned int f = 0;
        if (a.get_fan(dev, &f) == 0) { info.fan_percent = f; info.has_fan = true; }
    }
    if (a.get_pcie_gen && a.get_pcie_w) {
        unsigned int g = 0, w = 0;
        if (a.get_pcie_gen(dev, &g) == 0 && a.get_pcie_w(dev, &w) == 0) {
            info.pcie_gen = g; info.pcie_width = w; info.has_pcie = true;
        }
    }
    info.valid = true;
    return true;
}

#if defined(__linux__) && !KNST_USING_PLATFORM_WINDOWS
inline bool fill_from_sysfs(uint32_t idx, const char* card_dir, knst_gpu_info& info) noexcept {
    char path[512]; char tmp[128]; uint64_t v = 0; int32_t iv = 0;

    ::snprintf(path, sizeof(path), "%s/device/vendor", card_dir);
    if (sysfs_read_str(path, tmp, sizeof(tmp))) {
        info.vendor = vendor_from_pci_id((uint32_t)::strtoul(tmp, nullptr, 0));
        info.vendor_name = knst_c16string(knst_gpu_vendor_string(info.vendor));
    }

    ::snprintf(path, sizeof(path), "%s/device/product_name", card_dir);
    if (sysfs_read_str(path, tmp, sizeof(tmp))) info.name = knst_c16string(tmp);


    ::snprintf(path, sizeof(path), "%s/device/mem_info_vram_total", card_dir);
    if (sysfs_read_u64(path, v)) { info.memory_total = v; info.has_memory = true; }
    ::snprintf(path, sizeof(path), "%s/device/mem_info_vram_used", card_dir);
    if (sysfs_read_u64(path, v)) { info.memory_used = v; info.memory_free = info.memory_total > v ? info.memory_total - v : 0; }

    ::snprintf(path, sizeof(path), "%s/device/gpu_busy_percent", card_dir);
    if (sysfs_read_u64(path, v)) { info.utilization_gpu = (uint32_t)v; info.has_utilization = true; }


    ::snprintf(path, sizeof(path), "%s/device/current_link_width", card_dir);
    if (sysfs_read_u64(path, v)) info.pcie_width = (uint32_t)v;

    ::snprintf(path, sizeof(path), "%s/device/current_link_speed", card_dir);
    {
        char spd[64];
        if (sysfs_read_str(path, spd, sizeof(spd))) {
            double gts = ::atof(spd);
            if      (gts >= 32.0) info.pcie_gen = 5;
            else if (gts >= 16.0) info.pcie_gen = 4;
            else if (gts >= 8.0)  info.pcie_gen = 3;
            else if (gts >= 5.0)  info.pcie_gen = 2;
            else if (gts > 0.0)   info.pcie_gen = 1;
        }
    }
    info.has_pcie = (info.pcie_gen > 0 || info.pcie_width > 0);

    ::snprintf(path, sizeof(path), "%s/device/pp_dpm_sclk", card_dir);
    {
        int fd = ::open(path, O_RDONLY);
        if (fd >= 0) {
            char buf[512];
            ssize_t rn = ::read(fd, buf, sizeof(buf) - 1);
            ::close(fd);
            if (rn > 0) {
                buf[rn] = '\0';
                const char* p = buf;
                while (*p) {
                    const char* eol = ::strchr(p, '\n');
                    if (!eol) eol = p + ::strlen(p);
                    if (::memchr(p, '*', (size_t)(eol - p))) {
                        const char* colon = (const char*)::memchr(p, ':', (size_t)(eol - p));
                        if (colon) {
                            const char* q = colon + 1;
                            while (q < eol && (*q == ' ' || *q == '\t')) ++q;
                            unsigned long mhz = ::strtoul(q, nullptr, 10);
                            if (mhz > 0) { info.clock_core_mhz = (uint32_t)mhz; info.has_clocks = true; }
                        }
                        break;
                    }
                    if (*eol == '\0') break;
                    p = eol + 1;
                }
            }
        }
    }

    ::snprintf(path, sizeof(path), "%s/device/hwmon", card_dir);
    DIR* dh = ::opendir(path);
    if (dh) {
        struct dirent* e;
        while ((e = ::readdir(dh)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            char base[600];
            ::snprintf(base, sizeof(base), "%s/%s", path, e->d_name);
            char fpath[700];

            ::snprintf(fpath, sizeof(fpath), "%s/temp1_input", base);
            if (sysfs_read_i32(fpath, iv)) { info.temperature_core = iv / 1000; info.has_temperature = true; }
            ::snprintf(fpath, sizeof(fpath), "%s/power1_average", base);
            if (sysfs_read_u64(fpath, v)) { info.power_usage_mw = (uint32_t)(v / 1000); info.has_power = true; }
            ::snprintf(fpath, sizeof(fpath), "%s/power1_cap", base);
            if (sysfs_read_u64(fpath, v)) info.power_limit_mw = (uint32_t)(v / 1000);


            ::snprintf(fpath, sizeof(fpath), "%s/freq1_input", base);
            if (sysfs_read_u64(fpath, v)) {
                if (info.clock_core_mhz == 0) info.clock_core_mhz = (uint32_t)(v / 1000000);
                info.has_clocks = true;
            }
            ::snprintf(fpath, sizeof(fpath), "%s/freq2_input", base);
            if (sysfs_read_u64(fpath, v)) { info.clock_mem_mhz = (uint32_t)(v / 1000000); info.has_clocks = true; }

            ::snprintf(fpath, sizeof(fpath), "%s/pwm1", base);
            uint64_t pwm = 0, pwm_max = 255;
            if (sysfs_read_u64(fpath, pwm)) {
                ::snprintf(fpath, sizeof(fpath), "%s/pwm1_max", base);
                (void)sysfs_read_u64(fpath, pwm_max);
                if (pwm_max > 0) { info.fan_percent = (uint32_t)((pwm * 100ULL) / pwm_max); info.has_fan = true; }
            }
            break;
        }
        ::closedir(dh);
    }



    ::snprintf(path, sizeof(path), "%s/gt_act_freq_mhz", card_dir);
    if (sysfs_read_u64(path, v)) {
        info.clock_core_mhz = (uint32_t)v;
        info.has_clocks = true;
    } else {
        ::snprintf(path, sizeof(path), "%s/gt_cur_freq_mhz", card_dir);
        if (sysfs_read_u64(path, v)) {
            info.clock_core_mhz = (uint32_t)v;
            info.has_clocks = true;
        }
    }
    ::snprintf(path, sizeof(path), "%s/gt_RP0_freq_mhz", card_dir);
    if (sysfs_read_u64(path, v) && info.clock_core_mhz == 0) {
        info.clock_core_mhz = (uint32_t)v;
        info.has_clocks = true;
    }

    ::snprintf(path, sizeof(path), "%s/gt_busy_percent", card_dir);
    if (sysfs_read_u64(path, v)) {
        info.utilization_gpu = (uint32_t)v;
        info.has_utilization = true;
    }

    if (info.name.empty() && !info.vendor_name.empty()) info.name = info.vendor_name;
    info.index = idx;
    info.valid = true;
    return true;
}

inline bool find_sysfs_cards(knst_vector<uint32_t>& out) noexcept {
    out.clear();
    DIR* d = ::opendir("/sys/class/drm");
    if (!d) return false;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        const char* n = e->d_name;
        if (::strncmp(n, "card", 4) != 0) continue;
        if (!(n[4] >= '0' && n[4] <= '9')) continue;
        const char* p = n + 4;
        while (*p >= '0' && *p <= '9') ++p;
        if (*p != '\0') continue;
        out.push_back((uint32_t)::strtoul(n + 4, nullptr, 10));
    }
    ::closedir(d);
    return !out.empty();
}
#endif

#if KNST_USING_PLATFORM_WINDOWS


struct knst_luid_key { uint32_t high; uint32_t low; };

inline knst_vector<knst_gpu_info> pdh_collect_gpu_utils(knst_vector<knst_luid_key>& out_luids) noexcept {
    knst_vector<knst_gpu_info> dummy;
    out_luids.clear();

    PDH_HQUERY query = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return dummy;

    PDH_HCOUNTER counter = nullptr;
    if (PdhAddCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return dummy;
    }

   
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) { PdhCloseQuery(query); return dummy; }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) { PdhCloseQuery(query); return dummy; }

    DWORD buf_size = 0, item_count = 0;
    PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buf_size, &item_count, nullptr);
    if (buf_size == 0) { PdhCloseQuery(query); return dummy; }

    knst_vector<unsigned char> buf;
    if (!buf.resize(buf_size)) { PdhCloseQuery(query); return dummy; }
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buf_size, &item_count, items) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return dummy;
    }

   
    struct luid_sum { uint32_t high; uint32_t low; double total; };
    knst_vector<luid_sum> sums;

    for (DWORD i = 0; i < item_count; ++i) {
        
        if (items[i].FmtValue.doubleValue < 0.0) continue;

       
        const wchar_t* name = items[i].szName;
        const wchar_t* p = ::wcsstr(name, L"_luid_");
        if (!p) continue;
        p += 6;
        unsigned long hi = 0, lo = 0;
        if (::swscanf(p, L"0x%lx_0x%lx", &hi, &lo) != 2) continue;

        double v = items[i].FmtValue.doubleValue;
        if (v < 0.0) v = 0.0;

        bool found = false;
        for (uint32_t k = 0; k < sums.size(); ++k) {
            if (sums[k].high == (uint32_t)hi && sums[k].low == (uint32_t)lo) {
                sums[k].total += v;
                found = true;
                break;
            }
        }
        if (!found) {
            luid_sum s; s.high = (uint32_t)hi; s.low = (uint32_t)lo; s.total = v;
            sums.push_back(s);
        }
    }
    PdhCloseQuery(query);

    for (uint32_t i = 0; i < sums.size(); ++i) {
        knst_luid_key k; k.high = sums[i].high; k.low = sums[i].low;
        out_luids.push_back(k);

        knst_gpu_info g;
        double cap = sums[i].total > 100.0 ? 100.0 : sums[i].total;
        g.utilization_gpu = (uint32_t)(cap + 0.5);
        g.has_utilization = (g.utilization_gpu > 0);
        dummy.push_back(g);
    }
    return dummy;
}

inline bool fill_from_dxgi_impl(knst_vector<knst_gpu_info>& out) noexcept {
    knst_vector<knst_luid_key> pdh_luids;
    knst_vector<knst_gpu_info> pdh_utils = pdh_collect_gpu_utils(pdh_luids);

   
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = ::CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr) || !factory) return false;

    IDXGIAdapter1* adapter = nullptr;
    uint32_t idx = 0;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc = {};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter->Release();
                continue;
            }
            if (::wcsstr(desc.Description, L"Basic Render") ||
                ::wcsstr(desc.Description, L"Microsoft Basic")) {
                adapter->Release();
                continue;
            }

            knst_gpu_info g;
            g.index = idx++;
            g.name = knst_c16string(reinterpret_cast<const char16_t*>(desc.Description));

            switch (desc.VendorId) {
                case 0x10DE: g.vendor = knst_gpu_vendor::NVIDIA; break;
                case 0x1002:
                case 0x1022: g.vendor = knst_gpu_vendor::AMD;    break;
                case 0x8086: g.vendor = knst_gpu_vendor::Intel;  break;
                case 0x106B: g.vendor = knst_gpu_vendor::Apple;  break;
                default:     g.vendor = knst_gpu_vendor::Other;  break;
            }
            g.vendor_name = knst_c16string(knst_gpu_vendor_string(g.vendor));

          
            g.memory_total = (uint64_t)desc.DedicatedVideoMemory;
            g.has_memory   = (g.memory_total > 0);


            IDXGIAdapter3* adapter3 = nullptr;
            if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3),(void**)&adapter3)) && adapter3) {
                                                  
                DXGI_QUERY_VIDEO_MEMORY_INFO vmi = {};
                if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(
                        0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmi))) {
                    if (vmi.Budget > g.memory_total)
                        g.memory_total = (uint64_t)vmi.Budget;
                    g.memory_used = (uint64_t)vmi.CurrentUsage;
                    g.memory_free = (g.memory_total > g.memory_used) ? (g.memory_total - g.memory_used) : 0;
                    if (g.memory_total > 0) g.has_memory = true;
                }
                adapter3->Release();
            }
            if (!g.has_memory && desc.DedicatedVideoMemory > 0) {
                g.memory_total = (uint64_t)desc.DedicatedVideoMemory;
                g.memory_free  = g.memory_total;
                g.has_memory   = true;
            }

            
            for (uint32_t k = 0; k < pdh_luids.size(); ++k) {
                if (pdh_luids[k].high == (uint32_t)desc.AdapterLuid.HighPart &&
                    pdh_luids[k].low  == (uint32_t)desc.AdapterLuid.LowPart) {
                    if (k < pdh_utils.size()) {
                        g.utilization_gpu = pdh_utils[k].utilization_gpu;
                        g.has_utilization = pdh_utils[k].has_utilization;
                    }
                    break;
                }
            }

            g.valid = true;
            out.push_back(g);
        }
        adapter->Release();
    }
    factory->Release();
    return !out.empty();
}

inline bool fill_from_wmi_impl(knst_vector<knst_gpu_info>& out) noexcept {
    auto r = knst_process::run_capture(
        u"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
        { u"-NoProfile", u"-NonInteractive", u"-Command",
          u"$OutputEncoding = [Console]::OutputEncoding = [System.Text.Encoding]::UTF8; "
          u"Get-CimInstance Win32_VideoController | "
          u"Select-Object Name,AdapterCompatibility,DriverVersion | "
          u"ConvertTo-Csv -NoTypeInformation" },
        {}, nullptr, 0, 8000);
    if (r.error != knst_process_error::None || r.exit_code != 0) return false;

    const char* s = (const char*)r.out_data.data();
    uint32_t n = r.out_data.length();
    if (!s || n < 10) return false;

    uint32_t i = 0;
    if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) i = 3;
    while (i < n && s[i] != '\n') ++i;
    if (i < n) ++i;

    uint32_t idx = 0;
    while (i < n) {
        uint32_t start = i;
        while (i < n && s[i] != '\n' && s[i] != '\r') ++i;
        uint32_t len = i - start;
        while (i < n && (s[i] == '\n' || s[i] == '\r')) ++i;
        if (len < 5) continue;

        knst_vector<knst_c16string> cols;
        knst_c16string cur;
        bool in_q = false;
        for (uint32_t k = 0; k < len; ++k) {
            char c = s[start + k];
            if (in_q) {
                if (c == '"') {
                                        if (k + 1 < len && s[start + k + 1] == '"') { cur.append(u"\""); ++k; }
                    else in_q = false;
                                } else { char16_t _t[2] = { (char16_t)(unsigned char)c, 0 }; cur.append(_t); }
            } else {
                if (c == '"') in_q = true;
                else if (c == ',') { cols.push_back(cur); cur.clear(); }
                                else { char16_t _t[2] = { (char16_t)(unsigned char)c, 0 }; cur.append(_t); }
            }
        }
        cols.push_back(cur);
        if (cols.size() < 3) continue;

        knst_gpu_info info;
        info.index = idx++;
        info.name = cols[0];
        info.vendor_name = cols[1];
        info.driver_version = cols[2];
        info.has_driver_info = true;

        if (info.vendor_name.contains(u"NVIDIA")) info.vendor = knst_gpu_vendor::NVIDIA;
        else if (info.vendor_name.contains(u"AMD") || info.vendor_name.contains(u"Advanced Micro")) info.vendor = knst_gpu_vendor::AMD;
        else if (info.vendor_name.contains(u"Intel")) info.vendor = knst_gpu_vendor::Intel;

        info.valid = true;
        out.push_back(info);
    }
    return !out.empty();
}

inline const knst_vector<knst_gpu_info>& wmi_cache() noexcept {
    static knst_vector<knst_gpu_info> cache;
    static std::once_flag flag;
    std::call_once(flag, []{ fill_from_wmi_impl(cache); });
    return cache;
}
#endif

} // namespace knst_gpu_detail



class knst_gpu {
public:
    static bool has_gpu() noexcept { return count() > 0; }

    static uint32_t count() noexcept {
        return (uint32_t)list_all().size();
    }


    static knst_vector<knst_gpu_info> list_all() noexcept {
        knst_vector<knst_gpu_info> out;

        bool has_nvidia_from_nvml = false;


        if (knst_gpu_detail::nvml_ready()) {
            unsigned int n = 0;
            if (knst_gpu_detail::nvml().get_count(&n) == 0 && n > 0) {
                out.reserve(n);
                for (unsigned int i = 0; i < n; ++i) {
                    knst_gpu_info info;

                    if (knst_gpu_detail::fill_from_nvml((uint32_t)i, info)) {
                        out.push_back(info);
                        has_nvidia_from_nvml = true;
                    }
                }
            }
        }

#if defined(__linux__) && !KNST_USING_PLATFORM_WINDOWS

        knst_vector<uint32_t> cards;
        if (knst_gpu_detail::find_sysfs_cards(cards)) {
            for (uint32_t i = 0; i < cards.size(); ++i) {
                char card_dir[64];
                ::snprintf(card_dir, sizeof(card_dir), "/sys/class/drm/card%u", cards[i]);


                char vendor_path[256], vendor_buf[32];
                ::snprintf(vendor_path, sizeof(vendor_path), "%s/device/vendor", card_dir);
                knst_gpu_vendor v = knst_gpu_vendor::Unknown;
                if (knst_gpu_detail::sysfs_read_str(vendor_path, vendor_buf, sizeof(vendor_buf)))
                    v = knst_gpu_detail::vendor_from_pci_id((uint32_t)::strtoul(vendor_buf, nullptr, 0));


                if (v == knst_gpu_vendor::NVIDIA && has_nvidia_from_nvml) continue;


                char probe_path[256];
                uint64_t probe = 0;
                ::snprintf(probe_path, sizeof(probe_path), "%s/device/mem_info_vram_total", card_dir);
                bool has_mem = knst_gpu_detail::sysfs_read_u64(probe_path, probe);

                char product_path[256], product_buf[128];
                ::snprintf(product_path, sizeof(product_path), "%s/device/product_name", card_dir);
                bool has_product = knst_gpu_detail::sysfs_read_str(product_path, product_buf, sizeof(product_buf));

               
                if (!has_mem && !has_product && v == knst_gpu_vendor::Unknown) continue;

                knst_gpu_info info;
                if (knst_gpu_detail::fill_from_sysfs((uint32_t)out.size(), card_dir, info))
                    out.push_back(info);
            }
        }
#endif

#if KNST_USING_PLATFORM_WINDOWS
        
        {
            knst_vector<knst_gpu_info> dxgi_gpus;
            if (knst_gpu_detail::fill_from_dxgi_impl(dxgi_gpus)) {
                for (auto& dg : dxgi_gpus) {
                    if (dg.vendor == knst_gpu_vendor::NVIDIA && has_nvidia_from_nvml) continue;
                    bool dup = false;
                    for (uint32_t j = 0; j < out.size(); ++j) {
                        if (out[j].name == dg.name) { dup = true; break; }
                    }
                    if (dup) continue;
                    dg.index = (uint32_t)out.size();
                    out.push_back(dg);
                }
            }
        }

      
        const auto& cache = knst_gpu_detail::wmi_cache();
        for (uint32_t i = 0; i < cache.size(); ++i) {
            if (cache[i].vendor == knst_gpu_vendor::NVIDIA && has_nvidia_from_nvml) continue;
            bool dup = false;
            for (uint32_t j = 0; j < out.size(); ++j) {
                if (out[j].name == cache[i].name) { dup = true; break; }
            }
            if (dup) continue;
            knst_gpu_info info = cache[i];
            info.index = (uint32_t)out.size();
            out.push_back(info);
        }
#endif

        return out;
    }

    static knst_gpu_info get(uint32_t index) noexcept {
        auto all = list_all();
        if (index < all.size()) return all[index];
        return knst_gpu_info{};
    }

  
    static knst_vector<knst_gpu_process> get_processes() noexcept {
        knst_vector<knst_gpu_process> out;
        auto entries = knst_process_detail::collect_gpu_processes();
        out.reserve(entries.size());
        for (uint32_t i = 0; i < entries.size(); ++i) {
            knst_gpu_process gp;
            gp.pid = entries[i].pid;
            gp.gpu_index = entries[i].gpu_index;
            gp.vram_bytes = entries[i].vram_bytes;
            gp.vendor = entries[i].vendor;
            gp.name = knst_process_detail::process_name_fast(entries[i].pid);
            out.push_back(gp);
        }
        return out;
    }

    static knst_vector<knst_gpu_process> get_processes(uint32_t gpu_index) noexcept {
        auto all = get_processes();
        knst_vector<knst_gpu_process> out;
        for (uint32_t i = 0; i < all.size(); ++i)
            if (all[i].gpu_index == gpu_index) out.push_back(all[i]);
        return out;
    }

    static knst_c16string nvidia_driver_version() noexcept {
        if (!knst_gpu_detail::nvml_ready()) return knst_c16string();
        auto& a = knst_gpu_detail::nvml();
        if (!a.get_driver) return knst_c16string();
        char buf[80];
        if (a.get_driver(buf, sizeof(buf)) != 0) return knst_c16string();
        return knst_c16string(buf);
    }

    static knst_c16string cuda_version() noexcept {
        if (!knst_gpu_detail::nvml_ready()) return knst_c16string();
        auto& a = knst_gpu_detail::nvml();
        if (!a.get_cuda) return knst_c16string();
        int v = 0;
        if (a.get_cuda(&v) != 0) return knst_c16string();
        int major = v / 1000;
        int minor = (v % 1000) / 10;
        knst_c16string s;
        s.append((long long)major);
        s.append(u".");
        s.append((long long)minor);
        return s;
    }

    static bool nvml_available() noexcept { return knst_gpu_detail::nvml_ready(); }
};

