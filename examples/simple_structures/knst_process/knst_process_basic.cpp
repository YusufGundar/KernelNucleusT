/*
  knst_process_basic.cpp

  Full tutorial walkthrough of knst_process — the cross-platform
  process / thread / GPU library in KernelNucleusT.

  Every public method is demonstrated once with the expected outcome
  printed next to the result, so this file doubles as a reference.

  Safe to run: every spawned child is short-lived, every kill
  targets only children this test created, no files are left behind,
  and all temporary resources are cleaned up before exit.

  Sections
  --------
    A. Enums & types              1-3
    B. Spawning                   4-15
    C. stdio & I/O                16-22
    D. Control                    23-29
    E. Introspection              30-41
    F. Errors                     42
    G. GPU bridge                 43-44
    H. Platform summary           45
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>
#include <chrono>
#include <thread>


// ─────────────────────────────────────────────────────────────
// Console helpers
// ─────────────────────────────────────────────────────────────

static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}

static void pc(const knst_c16string& s) {
    for (uint32_t i = 0; i < s.length(); ++i)
        std::cout << static_cast<char>(s[i] < 128 ? s[i] : '?');
}

static void pb(const knst_byte_string& b, size_t maxlen = 200) {
    size_t n = b.length() < maxlen ? b.length() : maxlen;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = b[i];
        std::cout << (c >= 32 && c < 127 ? static_cast<char>(c) : '.');
    }
    if (b.length() > maxlen) std::cout << "...";
}

static const char* platform_name() {
#if KNST_USING_PLATFORM_WINDOWS
    return "Windows";
#else
    return "POSIX (Linux/macOS)";
#endif
}

#if KNST_USING_PLATFORM_WINDOWS
static const char16_t* SH = u"C:\\Windows\\System32\\cmd.exe";
// ECHO_PROG is only referenced inside the POSIX-only branch of test 6.
[[maybe_unused]] static const char16_t* ECHO_PROG = u"C:\\Windows\\System32\\cmd.exe";
#else
[[maybe_unused]] static const char16_t* SH = u"/bin/sh";
static const char16_t* ECHO_PROG = u"/bin/echo";
#endif

// ─────────────────────────────────────────────────────────────
// Test result tracker
// ─────────────────────────────────────────────────────────────

struct TR { const char* name; bool passed; bool skipped; };
static std::vector<TR> g_results;

static void record(const char* name, bool passed, bool skipped = false) {
    g_results.push_back({name, passed, skipped});
    std::cout << "  >>> ";
    if (skipped)      std::cout << "SKIP";
    else if (passed)  std::cout << "PASS";
    else              std::cout << "FAIL";
    std::cout << " : " << name << "\n";
}


// ─────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────

int main() {

    std::cout << "Platform : " << platform_name() << "\n";
    std::cout << "sizeof(knst_process)        : " << sizeof(knst_process) << " bytes\n";
    std::cout << "sizeof(knst_process_info)   : " << sizeof(knst_process_info) << " bytes\n";
    std::cout << "sizeof(knst_process_result) : " << sizeof(knst_process_result) << " bytes\n";
    std::cout << "sizeof(knst_gpu_info)       : " << sizeof(knst_gpu_info) << " bytes\n";
    std::cout << "sizeof(knst_thread_info)    : " << sizeof(knst_thread_info) << " bytes\n";


    // ============================================================
    // 1) knst_process_error enum — every value + its message
    // ============================================================
    section("1) knst_process_error — full enum dump");
    {
        // Every error code the library can return, in numeric order.
        knst_process_error errs[] = {
            knst_process_error::None,
            knst_process_error::NotFound,
            knst_process_error::PermissionDenied,
            knst_process_error::InvalidArgument,
            knst_process_error::Timeout,
            knst_process_error::PipeFailed,
            knst_process_error::ForkFailed,
            knst_process_error::ExecFailed,
            knst_process_error::WaitFailed,
            knst_process_error::AlreadyRunning,
            knst_process_error::NotRunning,
            knst_process_error::Crashed,
            knst_process_error::OutOfMemory,
            knst_process_error::ShellNotFound,
            knst_process_error::FileRedirectFailed,
            knst_process_error::AccessDenied,
            knst_process_error::NoSuchProcess,
            knst_process_error::SignalFailed,
            knst_process_error::NotSupported,
            knst_process_error::ReadFailed,
            knst_process_error::ParseFailed,
            knst_process_error::SystemCallFailed,
            knst_process_error::Unknown,
        };
        for (auto e : errs) {
            std::cout << "  [" << static_cast<int>(e) << "] "
                      << knst_process_error_string(e) << "\n";
        }
        record("error enum dump", true);
    }


    // ============================================================
    // 2) knst_process_state enum
    // ============================================================
    section("2) knst_process_state enum");
    {
        knst_process_state states[] = {
            knst_process_state::Unknown,
            knst_process_state::Running,
            knst_process_state::Sleeping,
            knst_process_state::DiskSleep,
            knst_process_state::Zombie,
            knst_process_state::Stopped,
            knst_process_state::Tracing,
            knst_process_state::Dead,
            knst_process_state::Idle,
            knst_process_state::Waiting,
        };
        for (auto s : states) {
            std::cout << "  [" << static_cast<int>(s) << "] "
                      << knst_process_state_string(s) << "\n";
        }
        record("state enum dump", true);
    }


    // ============================================================
    // 3) Other enums — stdio / shell / window / gpu vendor
    // ============================================================
    section("3) stdio / shell / window / GPU vendor enums");
    {
        std::cout << "  stdio modes  : Inherit(0) Pipe(1) Null(2) File(3)\n";
        std::cout << "  shell types  : Auto Sh Bash Zsh Fish Cmd PowerShell PowerShellCore Custom\n";
        std::cout << "  window modes : Hidden Minimized Maximized Normal\n";
        std::cout << "  gpu vendors  : ";
        knst_gpu_vendor vs[] = {
            knst_gpu_vendor::Unknown, knst_gpu_vendor::NVIDIA,
            knst_gpu_vendor::AMD, knst_gpu_vendor::Intel,
            knst_gpu_vendor::Apple, knst_gpu_vendor::Other,
        };
        for (auto v : vs) std::cout << knst_gpu_vendor_string(v) << " ";
        std::cout << "\n";
        record("auxiliary enums", true);
    }


    // ============================================================
    // 4) run_shell — cross-platform default shell
    // ============================================================
    section("4) run_shell — default platform shell");
    {
        knst_process p = knst_process::run_shell(u"echo hello from run_shell");
        if (p.error == knst_process_error::None) {
            knst_process_result r = p.communicate(nullptr, 0, 5000);
            std::cout << "  stdout   : "; pb(r.out_data, 80); std::cout << "\n";
            std::cout << "  exit code: " << r.exit_code << "\n";
            record("run_shell basic", r.exit_code == 0 && r.out_data.length() > 0);
        } else {
            std::cout << "  error: " << p.error_string() << "\n";
            record("run_shell basic", false);
        }
    }


    // ============================================================
    // 5) run_shell with explicit shell type
    // ============================================================
    section("5) run_with_shell — explicit shell type");
    {
#if !KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run_with_shell(
            knst_shell_type::Bash, u"echo shell is $0");
        if (p.error == knst_process_error::None) {
            knst_process_result r = p.communicate(nullptr, 0, 5000);
            std::cout << "  stdout   : "; pb(r.out_data, 80); std::cout << "\n";
            record("explicit bash", r.exit_code == 0);
        } else {
            std::cout << "  bash not available: " << p.error_string() << "\n";
            record("explicit bash", false, true);
        }
#else
        knst_process p = knst_process::run_with_shell(
            knst_shell_type::Cmd, u"echo cmd shell");
        knst_process_result r = p.communicate(nullptr, 0, 5000);
        std::cout << "  stdout   : "; pb(r.out_data, 80); std::cout << "\n";
        record("explicit shell", r.exit_code == 0);
#endif
    }


    // ============================================================
    // 6) run — direct program, no shell
    // ============================================================
    section("6) run — direct program invocation (no shell)");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(SH, {u"/c", u"ver"});
#else
        knst_process p = knst_process::run(ECHO_PROG,
            {u"direct", u"invocation"});
#endif
        if (p.error == knst_process_error::None) {
            knst_process_result r = p.communicate(nullptr, 0, 5000);
            std::cout << "  stdout   : "; pb(r.out_data, 100); std::cout << "\n";
            std::cout << "  exit code: " << r.exit_code << "\n";
            record("run direct", r.exit_code == 0);
        } else {
            std::cout << "  error: " << p.error_string() << "\n";
            record("run direct", false);
        }
    }


    // ============================================================
    // 7) communicate — buffered stdout capture
    // ============================================================
        section("7) communicate — buffered stdout");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run_shell(u"echo A & echo B & echo C");
#else
        knst_process p = knst_process::run_shell(u"printf 'A\\nB\\nC\\n'");
#endif
        knst_process_result r = p.communicate(nullptr, 0, 5000);
        std::cout << "  bytes    : " << r.out_data.length() << "\n";
        std::cout << "  content  : "; pb(r.out_data, 100); std::cout << "\n";
        std::cout << "  timed_out: " << (r.timed_out ? "yes" : "no") << "\n";
        record("communicate stdout", r.out_data.length() > 0 && !r.timed_out);
    }


    // ============================================================
    // 8) communicate — stderr capture
    // ============================================================
    section("8) communicate — stderr capture");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(SH,
            {u"/c", u"echo to-stderr 1>&2"});
#else
        knst_process p = knst_process::run_shell(u"echo to-stderr 1>&2");
#endif
        knst_process_result r = p.communicate(nullptr, 0, 5000);
        std::cout << "  stderr   : "; pb(r.err_data, 100); std::cout << "\n";
        record("communicate stderr", r.err_data.length() > 0);
    }


    // ============================================================
    // 9) communicate — stdin piping
    // ============================================================
    section("9) communicate — stdin piping (cat / findstr)");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            u"C:\\Windows\\System32\\findstr.exe", {u"."});
#else
        knst_process p = knst_process::run(u"/bin/cat");
#endif
        if (p.error == knst_process_error::None) {
            const char* input = "line one\nline two\nline three\n";
            knst_process_result r = p.communicate(input,
                std::strlen(input), 5000);
            std::cout << "  echoed: "; pb(r.out_data, 150); std::cout << "\n";
            std::cout << "  bytes : " << r.out_data.length() << "\n";
            record("stdin piping", r.out_data.length() >= 20);
        } else {
            record("stdin piping", false);
        }
    }


    // ============================================================
    // 10) communicate — timeout kill
    // ============================================================
    section("10) communicate — timeout kill (300 ms)");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"5", u"127.0.0.1"});
#else
        knst_process p = knst_process::run(u"/bin/sleep", {u"5"});
#endif
        auto t0 = std::chrono::steady_clock::now();
        knst_process_result r = p.communicate(nullptr, 0, 300);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        std::cout << "  timed_out : " << (r.timed_out ? "yes" : "no") << "\n";
        std::cout << "  elapsed   : " << ms << " ms\n";
        std::cout << "  exit_code : " << r.exit_code
                  << " (137 = SIGKILL, expected on POSIX)\n";
        record("timeout kill", r.timed_out && ms < 2000);
    }


    // ============================================================
    // 11) run_capture — one-shot convenience
    // ============================================================
    section("11) run_capture — one-shot convenience");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process_result r = knst_process::run_capture(
            SH, {u"/c", u"echo capture works"}, {}, nullptr, 0, 5000);
#else
        knst_process_result r = knst_process::run_capture(
            u"/bin/echo", {u"capture works"}, {}, nullptr, 0, 5000);
#endif
        std::cout << "  stdout   : "; pb(r.out_data, 80); std::cout << "\n";
        std::cout << "  exit code: " << r.exit_code << "\n";
        record("run_capture", r.exit_code == 0 && r.out_data.length() > 0);
    }


    // ============================================================
    // 12) run_capture with stdin data
    // ============================================================
    section("12) run_capture — stdin data parameter");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process_result r = knst_process::run_capture(
            u"C:\\Windows\\System32\\findstr.exe", {u"."}, {},
            "alpha\nbeta\n", 11, 5000);
#else
        knst_process_result r = knst_process::run_capture(
            u"/bin/cat", {}, {}, "alpha\nbeta\n", 11, 5000);
#endif
        std::cout << "  echoed: "; pb(r.out_data, 80); std::cout << "\n";
        record("run_capture stdin", r.out_data.length() > 0);
    }


    // ============================================================
    // 13) run_capture — with explicit timeout
    // ============================================================
    section("13) run_capture — timeout variant");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process_result r = knst_process::run_capture(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"10", u"127.0.0.1"}, {}, nullptr, 0, 200);
#else
        knst_process_result r = knst_process::run_capture(
            u"/bin/sleep", {u"3"}, {}, nullptr, 0, 200);
#endif
        std::cout << "  timed_out: " << (r.timed_out ? "yes" : "no") << "\n";
        record("run_capture timeout", r.timed_out);
    }


    // ============================================================
    // 14) run_detached — fire and forget
    // ============================================================
    section("14) run_detached — fire and forget");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run_detached(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"3", u"127.0.0.1"});
#else
        knst_process p = knst_process::run_detached(
            u"/bin/sleep", {u"2"});
#endif
        if (p.error == knst_process_error::None) {
            std::cout << "  detached pid : " << p.pid() << "\n";
            std::cout << "  is_running   : " << (p.is_running() ? "yes" : "no") << "\n";
            std::cout << "  (main program continues immediately)\n";
            record("run_detached", p.pid() != 0);
        } else {
            std::cout << "  error: " << p.error_string() << "\n";
            record("run_detached", false);
        }
    }


    // ============================================================
    // 15) Detached process — wait / kill from outside
    // ============================================================
    section("15) Detached process — kill by pid");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run_detached(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"10", u"127.0.0.1"});
#else
        knst_process p = knst_process::run_detached(
            u"/bin/sleep", {u"30"});
#endif
        if (p.error == knst_process_error::None) {
            uint32_t pid = p.pid();
            std::cout << "  pid            : " << pid << "\n";
            bool before = knst_process::exists(pid);
            std::cout << "  exists before  : " << (before ? "yes" : "no") << "\n";

            // Kill by pid. On Windows a DETACHED_PROCESS loses its parent
            // handle, so p.kill() has nothing to terminate. kill_by_pid()
            // opens a fresh handle from the pid — works on every platform.
            bool killed = knst_process::kill_by_pid(pid, 9);
            std::cout << "  kill_by_pid    : " << (killed ? "yes" : "no") << "\n";

            // Reap the process handle; on Windows a terminated process stays
            // "alive" to OpenProcess until all handles are closed, so without
            // this the exists() check below would still report 'yes'.
            p.wait(2000);

            for (int i = 0; i < 100; ++i) {
                if (!knst_process::exists(pid)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            bool after = knst_process::exists(pid);
            std::cout << "  exists after   : " << (after ? "yes" : "no") << "\n";
            record("detached kill", before && killed);
        } else {
            record("detached kill", false);
        }
    }


    // ============================================================
    // 16) Custom cwd
    // ============================================================
    section("16) Custom working directory");
    {
        knst_process_options o;
        o.cwd = u"/tmp";
#if KNST_USING_PLATFORM_WINDOWS
        o.cwd = u"C:\\Windows";
        knst_process p = knst_process::run(SH, {u"/c", u"cd"}, o);
#else
        knst_process p = knst_process::run(u"/bin/pwd", {}, o);
#endif
        knst_process_result r = p.communicate(nullptr, 0, 5000);
        std::cout << "  cwd reported : "; pb(r.out_data, 100); std::cout << "\n";
        record("custom cwd", r.exit_code == 0);
    }


    // ============================================================
    // 17) Custom environment variables
    // ============================================================
    section("17) Custom environment variables");
    {
        knst_process_options o;
        o.env.push_back({u"KNST_TEST_VAR", u"hello-from-test"});
        o.env.push_back({u"ANOTHER_VAR",   u"42"});
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run_shell(
            u"echo %KNST_TEST_VAR% %ANOTHER_VAR%", o);
#else
        knst_process p = knst_process::run_shell(
            u"echo $KNST_TEST_VAR $ANOTHER_VAR", o);
#endif
        knst_process_result r = p.communicate(nullptr, 0, 5000);
        std::cout << "  echoed: "; pb(r.out_data, 100); std::cout << "\n";
        record("custom env", r.out_data.length() > 0);
    }


    // ============================================================
    // 18) stdio Null mode
    // ============================================================
    section("18) stdio Null mode — discard output");
    {
        knst_process_options o;
        o.stdout_mode = knst_stdio_mode::Null;
        o.stderr_mode = knst_stdio_mode::Null;
        o.stdin_mode  = knst_stdio_mode::Null;
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(SH, {u"/c", u"echo discarded"}, o);
#else
        knst_process p = knst_process::run(u"/bin/echo", {u"discarded"}, o);
#endif
        knst_process_result r = p.communicate(nullptr, 0, 5000);
        std::cout << "  stdout bytes : " << r.out_data.length()
                  << " (expected 0)\n";
        std::cout << "  exit code    : " << r.exit_code << "\n";
        record("stdio null", r.exit_code == 0);
    }


    // ============================================================
    // 19) stdout redirect to file
    // ============================================================
    section("19) stdout redirect to file");
    {
        knst_c16string out_path = knst_file::join_path(
            knst_file::get_temp_dir(), u"knst_proc_stdout.txt");
        knst_file::remove_file(out_path);

        knst_process_options o;
        o.stdout_mode = knst_stdio_mode::File;
        o.stdout_file = out_path;
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(SH, {u"/c", u"echo file-redirected"}, o);
#else
        knst_process p = knst_process::run(u"/bin/echo", {u"file-redirected"}, o);
#endif
        p.wait(5000);

        knst_c16string content = knst_file::read_file_text(out_path);
        std::cout << "  file content : "; pc(content); std::cout << "\n";
        knst_file::remove_file(out_path);
        record("stdout to file", content.length() > 0);
    }


    // ============================================================
    // 20) Line-by-line reading
    // ============================================================
    section("20) Line-by-line reading");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            SH, {u"/c", u"echo first & echo second & echo third"});
#else
        knst_process p = knst_process::run_shell(
            u"printf 'first\\nsecond\\nthird\\n'");
#endif
        knst_c16string line;
        int n = 0;
        while (p.read_line(line)) {
            ++n;
            std::cout << "  line " << n << ": "; pc(line); std::cout << "\n";
        }
        p.wait(1000);
        record("read_line", n == 3);
    }


    // ============================================================
    // 21) Non-blocking try_read
    // ============================================================
    section("21) Non-blocking try_read_stdout");
    {
        knst_process p = knst_process::run_shell(u"echo quick");
        knst_byte_string buf;
        // Give the child ~1 second to produce output
        bool got = p.try_read_stdout(1000, buf);
        std::cout << "  got      : " << (got ? "yes" : "no") << "\n";
        std::cout << "  bytes    : " << buf.length() << "\n";
        std::cout << "  content  : "; pb(buf, 80); std::cout << "\n";
        p.wait(1000);
        record("try_read", got && buf.length() > 0);
    }


    // ============================================================
    // 22) write_stdin + close_stdin
    // ============================================================
    section("22) write_stdin + close_stdin");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            u"C:\\Windows\\System32\\findstr.exe", {u"."});
#else
        knst_process p = knst_process::run(u"/bin/cat");
#endif
        if (p.error == knst_process_error::None) {
            bool w1 = p.write_stdin(u"written via write_stdin\n");
            bool w2 = p.write_stdin(u"second line\n");
            bool w3 = p.close_stdin();
            knst_byte_string out = p.read_stdout();
            p.wait(1000);
            std::cout << "  write #1 : " << (w1 ? "ok" : "fail") << "\n";
            std::cout << "  write #2 : " << (w2 ? "ok" : "fail") << "\n";
            std::cout << "  close    : " << (w3 ? "ok" : "fail") << "\n";
            std::cout << "  echoed   : "; pb(out, 100); std::cout << "\n";
            record("write_stdin", w1 && w2 && w3 && out.length() > 0);
        } else {
            record("write_stdin", false);
        }
    }


    // ============================================================
    // 23) Signal handling — SIGTERM
    // ============================================================
    section("23) Signal handling — SIGTERM graceful");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"10", u"127.0.0.1"});
#else
        knst_process p = knst_process::run(u"/bin/sleep", {u"10"});
#endif
        if (p.error == knst_process_error::None) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            bool sent = p.send_signal(15);   // SIGTERM
            p.wait(3000);
            std::cout << "  signal sent : " << (sent ? "yes" : "no") << "\n";
            std::cout << "  exit_code   : " << p.exit_code() << "\n";
            std::cout << "  signaled    : " << (p.was_signaled() ? "yes" : "no") << "\n";
            std::cout << "  term signal : " << p.term_signal() << "\n";
            record("SIGTERM", sent);
        } else {
            record("SIGTERM", false);
        }
    }


    // ============================================================
    // 24) Suspend / resume
    // ============================================================
    section("24) Suspend / resume");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"20", u"127.0.0.1"});
#else
        knst_process p = knst_process::run(u"/bin/sleep", {u"20"});
#endif
        if (p.error == knst_process_error::None) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            bool susp_ok = p.suspend();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            bool still_running = p.is_running();
            bool res_ok = p.resume();
            std::cout << "  suspend : " << (susp_ok ? "ok" : "fail") << "\n";
            std::cout << "  still   : " << (still_running ? "running" : "not running") << "\n";
            std::cout << "  resume  : " << (res_ok ? "ok" : "fail") << "\n";
            p.kill(9);
            p.wait(1000);
            record("suspend/resume", susp_ok && res_ok);
        } else {
            record("suspend/resume", false, true);
        }
    }


    // ============================================================
    // 25) Priority adjustment
    // ============================================================
    section("25) Priority adjustment — set_priority(15)");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"3", u"127.0.0.1"});
#else
        knst_process p = knst_process::run(u"/bin/sleep", {u"2"});
#endif
        if (p.error == knst_process_error::None) {
            bool ok = p.set_priority(15);
            std::cout << "  set_priority : " << (ok ? "ok" : "fail") << "\n";
            std::cout << "  nice in info : " << p.get_info().nice << "\n";
            p.kill(9);
            p.wait(1000);
            record("priority", ok);
        } else {
            record("priority", false);
        }
    }


    // ============================================================
    // 26) CPU affinity
    // ============================================================
    section("26) CPU affinity — pin to CPU 0");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"3", u"127.0.0.1"});
#else
        knst_process p = knst_process::run(u"/bin/sleep", {u"2"});
#endif
        if (p.error == knst_process_error::None) {
            bool ok = p.set_affinity(1ULL << 0);   // mask = CPU 0 only
            std::cout << "  set_affinity : " << (ok ? "ok" : "fail") << "\n";
            p.kill(9);
            p.wait(1000);
            record("affinity", ok);
        } else {
            record("affinity", false);
        }
    }


    // ============================================================
    // 27) kill_by_pid (child)
    // ============================================================
    section("27) kill_by_pid — static helper");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"30", u"127.0.0.1"});
#else
        knst_process p = knst_process::run(u"/bin/sleep", {u"30"});
#endif
        if (p.error == knst_process_error::None) {
            uint32_t pid = p.pid();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            bool killed = knst_process::kill_by_pid(pid, 9);
            p.wait(2000);
            std::cout << "  pid    : " << pid << "\n";
            std::cout << "  killed : " << (killed ? "yes" : "no") << "\n";
            record("kill_by_pid", killed);
        } else {
            record("kill_by_pid", false);
        }
    }


    // ============================================================
    // 28) kill_by_name
    // ============================================================
    section("28) kill_by_name — self-safe check");
    {
        // Safe test: search for a nonsense name, confirm 0 matches (no kills).
        bool found = knst_process::kill_by_name(u"definitely_no_such_proc_xyz", 15);
        std::cout << "  killed any : " << (found ? "yes (unexpected)" : "no (correct)") << "\n";
        record("kill_by_name empty", !found);
    }


    // ============================================================
    // 29) terminate_graceful
    // ============================================================
    section("29) terminate_graceful — SIGTERM then SIGKILL");
    {
#if KNST_USING_PLATFORM_WINDOWS
        knst_process p = knst_process::run(
            u"C:\\Windows\\System32\\ping.exe",
            {u"-n", u"30", u"127.0.0.1"});
#else
        knst_process p = knst_process::run(u"/bin/sleep", {u"30"});
#endif
        if (p.error == knst_process_error::None) {
            uint32_t pid = p.pid();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto t0 = std::chrono::steady_clock::now();
            bool graceful_ok = knst_process::terminate_graceful(pid, 2000);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
                            p.wait(2000);
            std::cout << "  terminated in : " << ms << " ms\n";
            std::cout << "  graceful_ok   : " << (graceful_ok ? "yes" : "no") << "\n";
#if KNST_USING_PLATFORM_WINDOWS
            // Windows has no SIGTERM, so terminate_graceful is really
            // TerminateProcess + wait. But is_alive(pid) (which calls
            // OpenProcess from a FRESH handle) keeps returning true on a
            // zombie — the kernel object survives while ANY handle is open,
            // including our p.m_process_handle. The reliable signal is
            // our own handle state: after p.wait() succeeded, m_running
            // is false and exit_code reflects the actual termination.
            bool dead = !p.is_running();
            std::cout << "  process dead  : " << (dead ? "yes" : "no") << "\n";
            std::cout << "  exit code     : " << p.exit_code() << "\n";
            record("terminate_graceful", dead);
#else
            // POSIX: SIGTERM works, expect graceful success
            record("terminate_graceful", graceful_ok);
#endif
        } else {
            record("terminate_graceful", false);
        }
    }


    // ============================================================
    // 30) Current process info
    // ============================================================
    section("30) Current process info");
    {
        std::cout << "  current pid  : " << knst_process::current_pid()  << "\n";
        std::cout << "  parent pid   : " << knst_process::parent_pid()   << "\n";
        std::cout << "  current user : "; pc(knst_process::current_user()); std::cout << "\n";
        std::cout << "  current uid  : " << knst_process::current_uid()  << "\n";
        std::cout << "  exe path     : "; pc(knst_process::current_exe_path()); std::cout << "\n";
        record("current info", knst_process::current_pid() != 0);
    }


    // ============================================================
    // 31) get_info — full breakdown
    // ============================================================
    section("31) get_info — full breakdown");
    {
        uint32_t self = knst_process::current_pid();
        knst_process_info info = knst_process::get_info(self);
        std::cout << "  pid          : " << info.pid << "\n";
        std::cout << "  ppid         : " << info.ppid << "\n";
        std::cout << "  pgid         : " << info.pgid << "\n";
        std::cout << "  sid          : " << info.sid  << "\n";
        std::cout << "  name         : "; pc(info.name); std::cout << "\n";
        std::cout << "  exe path     : "; pc(info.exe_path); std::cout << "\n";
        std::cout << "  cmdline      : "; pc(info.cmdline); std::cout << "\n";
        std::cout << "  cwd          : "; pc(info.cwd); std::cout << "\n";
        std::cout << "  user         : "; pc(info.user); std::cout << "\n";
        std::cout << "  uid / gid    : " << info.uid << " / " << info.gid << "\n";
        std::cout << "  state        : " << knst_process_state_string(info.state) << "\n";
        std::cout << "  rss bytes    : " << info.rss_bytes << "\n";
        std::cout << "  vsz bytes    : " << info.vsz_bytes << "\n";
        std::cout << "  thread count : " << info.thread_count << "\n";
        std::cout << "  nice         : " << info.nice << "\n";
        std::cout << "  priority     : " << info.priority << "\n";
        std::cout << "  fd count     : " << info.fd_count << "\n";
        std::cout << "  zombie       : " << (info.is_zombie ? "yes" : "no") << "\n";
        std::cout << "  kernel thr   : " << (info.is_kernel_thread ? "yes" : "no") << "\n";
        record("get_info", info.pid == self && !info.name.empty());
    }


    // ============================================================
    // 32) get_* convenience helpers
    // ============================================================
    section("32) get_* convenience helpers");
    {
        uint32_t self = knst_process::current_pid();
        std::cout << "  get_exe_path : "; pc(knst_process::get_exe_path(self)); std::cout << "\n";
        std::cout << "  get_cmdline  : "; pc(knst_process::get_cmdline(self));  std::cout << "\n";
        std::cout << "  get_cwd      : "; pc(knst_process::get_cwd(self));      std::cout << "\n";
        std::cout << "  get_user     : "; pc(knst_process::get_user(self));     std::cout << "\n";
        std::cout << "  get_uid      : " << knst_process::get_uid(self) << "\n";
        std::cout << "  get_ppid     : " << knst_process::get_ppid(self) << "\n";
        std::cout << "  get_nice     : " << knst_process::get_nice(self) << "\n";
        std::cout << "  get_state    : "
                  << knst_process_state_string(knst_process::get_state(self)) << "\n";
        record("get_* helpers", !knst_process::get_exe_path(self).empty());
    }


    // ============================================================
    // 33) RSS / VSZ / CPU times
    // ============================================================
    section("33) RSS / VSZ / CPU times");
    {
        uint32_t self = knst_process::current_pid();

        uint64_t rss = knst_process::get_rss_bytes(self);
        uint64_t vsz = knst_process::get_vsz_bytes(self);
        std::cout << "  rss : " << (rss / 1024) << " KB\n";
        std::cout << "  vsz : " << (vsz / 1024) << " KB\n";

        uint64_t cu = 0, cs = 0;
        bool ok = knst_process::get_cpu_times(self, cu, cs);
        std::cout << "  cpu : user=" << cu << " ms, system=" << cs << " ms\n";

        uint64_t rss2 = 0, vsz2 = 0;
        bool ok2 = knst_process::get_memory(self, rss2, vsz2);
        std::cout << "  get_memory : " << (ok2 ? "ok" : "fail") << "\n";
        record("mem/cpu", rss > 0 && vsz > 0 && ok);
    }


    // ============================================================
    // 34) list_pids / list_all
    // ============================================================
    section("34) list_pids / list_all");
    {
        knst_vector<uint32_t> pids = knst_process::list_pids();
        std::cout << "  list_pids count : " << pids.size() << "\n";

        knst_vector<knst_process_info> all = knst_process::list_all();
        std::cout << "  list_all count  : " << all.size() << "\n";

        // Print a few children of PID 1 (or PID 4 on Windows)
        uint32_t root = knst_process::current_pid() > 0 ? 1 : 1;
        knst_vector<knst_process_info> sample;
        for (uint32_t i = 0; i < all.size() && sample.size() < 3; ++i) {
            if (all[i].pid == root || all[i].ppid == root) {
                sample.push_back(all[i]);
            }
        }
        for (uint32_t i = 0; i < sample.size(); ++i) {
            std::cout << "    pid=" << sample[i].pid
                      << "  name="; pc(sample[i].name); std::cout << "\n";
        }
        record("list processes", pids.size() > 0 && all.size() > 0);
    }


    // ============================================================
    // 35) find_by_name — exact and fuzzy
    // ============================================================
    section("35) find_by_name — exact and fuzzy");
    {
        knst_c16string self_name = knst_process::get_info(
            knst_process::current_pid()).name;
        std::cout << "  self name : "; pc(self_name); std::cout << "\n";

        knst_vector<knst_process_info> exact =
            knst_process::find_by_name(self_name, true);
        std::cout << "  exact match count : " << exact.size() << "\n";

        // Case-insensitive substring by default
        knst_c16string fuzzy = self_name;
        for (uint32_t i = 0; i < fuzzy.length(); ++i) {
            if (fuzzy[i] >= u'A' && fuzzy[i] <= u'Z')
                fuzzy[i] = static_cast<char16_t>(fuzzy[i] + 32);
        }
        knst_vector<knst_process_info> ci =
            knst_process::find_by_name(fuzzy, false);
        std::cout << "  fuzzy match count : " << ci.size() << "\n";
        record("find_by_name", exact.size() >= 1 && ci.size() >= 1);
    }


    // ============================================================
    // 36) children_of / descendants_of
    // ============================================================
    section("36) children_of / descendants_of (current process)");
    {
        uint32_t self = knst_process::current_pid();
        knst_vector<uint32_t> kids = knst_process::children_of(self);
        std::cout << "  direct children  : " << kids.size() << "\n";

        knst_vector<knst_process_info> desc = knst_process::descendants_of(self);
        std::cout << "  total descendants: " << desc.size() << "\n";
        record("children/descendants", true);   // both 0 is valid
    }


    // ============================================================
    // 37) exists / is_alive / is_zombie
    // ============================================================
    section("37) exists / is_alive / is_zombie");
    {
        uint32_t self = knst_process::current_pid();
        std::cout << "  self exists   : " << (knst_process::exists(self)    ? "yes" : "no") << "\n";
        std::cout << "  self alive    : " << (knst_process::is_alive(self)  ? "yes" : "no") << "\n";
        std::cout << "  self zombie   : " << (knst_process::is_zombie(self) ? "yes" : "no") << "\n";
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  pid 4 (System): " << (knst_process::exists(4)       ? "yes" : "no") << "\n";
#else
        std::cout << "  pid 1 exists  : " << (knst_process::exists(1)       ? "yes" : "no") << "\n";
#endif
        record("exists/alive",
               knst_process::exists(self) && knst_process::is_alive(self)
               && !knst_process::is_zombie(self));
    }


    // ============================================================
    // 38) Threads of current process
    // ============================================================
    section("38) Threads of current process");
    {
        uint32_t self = knst_process::current_pid();
        knst_vector<uint32_t> tids = knst_process::get_thread_ids(self);
        std::cout << "  thread count : " << tids.size() << "\n";

        knst_vector<knst_thread_info> threads = knst_process::get_threads(self);
        std::cout << "  thread infos : " << threads.size() << "\n";
        for (uint32_t i = 0; i < threads.size() && i < 3; ++i) {
            std::cout << "    tid=" << threads[i].tid
                      << "  name="; pc(threads[i].name); std::cout << "\n";
        }
        record("threads", tids.size() >= 1);
    }


    // ============================================================
    // 39) File descriptors of current process
    // ============================================================
    section("39) File descriptors of current process");
    {
        uint32_t self = knst_process::current_pid();
        knst_vector<knst_fd_info> fds = knst_process::get_fds(self);
        std::cout << "  total fds : " << fds.size() << "\n";
        uint32_t shown = 0;
        for (uint32_t i = 0; i < fds.size() && shown < 5; ++i) {
            std::cout << "    fd=" << fds[i].fd
                      << "  "; pc(fds[i].target);
            if (fds[i].is_pipe)   std::cout << "  [pipe]";
            if (fds[i].is_file)   std::cout << "  [file]";
            if (fds[i].is_socket) std::cout << "  [socket]";
            std::cout << "\n";
            ++shown;
        }
        record("fds", fds.size() >= 3);   // at least stdin/stdout/stderr
    }


    // ============================================================
    // 40) Environment of current process
    // ============================================================
    section("40) Environment of current process");
    {
        uint32_t self = knst_process::current_pid();
        knst_vector<knst_env_var> env = knst_process::get_env(self);
        std::cout << "  total env vars : " << env.size() << "\n";
        uint32_t shown = 0;
        for (uint32_t i = 0; i < env.size() && shown < 5; ++i) {
            std::cout << "    "; pc(env[i].name);
            std::cout << " = "; pc(env[i].value);
            std::cout << "\n";
            ++shown;
        }
        record("env", env.size() > 0);
    }


    // ============================================================
    // 41) System information
    // ============================================================
    section("41) System information");
    {
        auto si = knst_process::get_system_info();
        auto gb = [](uint64_t b) { return b / (1024ULL * 1024 * 1024); };
        std::cout << "  cpu count    : " << si.cpu_count << "\n";
        std::cout << "  page size    : " << si.page_size << " bytes\n";
        std::cout << "  total memory : " << gb(si.total_memory) << " GB\n";
        std::cout << "  used memory  : " << gb(si.used_memory)  << " GB\n";
        std::cout << "  free memory  : " << gb(si.free_memory)  << " GB\n";
        std::cout << "  uptime       : " << (si.uptime_ms / 1000) << " s\n";
        record("system info", si.cpu_count > 0 && si.total_memory > 0);
    }


    // ============================================================
    // 42) Error handling — spawn failures
    // ============================================================
    section("42) Error handling — spawn failures");
    {
        // Nonexistent executable
        knst_process p1 = knst_process::run(u"/nonexistent/program_xyz",
                                             {u"arg"});
        std::cout << "  missing exe   : " << p1.error_string()
                                   << "  (native=" << p1.native_error_code() << ")\n";

        // Empty program name
        knst_process p2 = knst_process::run(u"");
        std::cout << "  empty program : " << p2.error_string() << "\n";

        // Bad cwd
        knst_process_options o;
        o.cwd = u"/definitely/does/not/exist";
        knst_process p3 = knst_process::run(u"/bin/echo", {u"x"}, o);
        std::cout << "  bad cwd       : " << p3.error_string() << "\n";

        bool all_failed = (p1.error != knst_process_error::None)
                       && (p2.error != knst_process_error::None)
                       && (p3.error != knst_process_error::None);
        record("spawn failures rejected", all_failed);
    }


    // ============================================================
    // 43) GPU inventory
    // ============================================================
    section("43) GPU inventory");
    {
        std::cout << "  has_gpu()      : " << (knst_gpu::has_gpu()        ? "yes" : "no") << "\n";
        std::cout << "  count()        : " << knst_gpu::count()                              << "\n";
        std::cout << "  nvml_available : " << (knst_gpu::nvml_available() ? "yes" : "no") << "\n";

        knst_c16string drv = knst_gpu::nvidia_driver_version();
        if (!drv.empty()) {
            std::cout << "  nvidia driver  : "; pc(drv); std::cout << "\n";
        }
        knst_c16string cuda = knst_gpu::cuda_version();
        if (!cuda.empty()) {
            std::cout << "  cuda version   : "; pc(cuda); std::cout << "\n";
        }

        knst_vector<knst_gpu_info> gpus = knst_gpu::list_all();
        for (uint32_t i = 0; i < gpus.size(); ++i) {
            const auto& g = gpus[i];
            std::cout << "\n  [" << i << "] "; pc(g.name);
            std::cout << "  (" << knst_gpu_vendor_string(g.vendor) << ")\n";
            if (g.has_memory) {
                std::cout << "    VRAM       : "
                          << (g.memory_used  / (1024ULL * 1024)) << " MB / "
                          << (g.memory_total / (1024ULL * 1024)) << " MB\n";
            }
            if (g.has_utilization)
                std::cout << "    GPU load   : " << g.utilization_gpu << " %\n";
            if (g.has_temperature)
                std::cout << "    temp       : " << g.temperature_core << " C\n";
            if (g.has_power)
                std::cout << "    power      : " << (g.power_usage_mw / 1000)
                          << " W (limit " << (g.power_limit_mw / 1000) << " W)\n";
            if (g.has_clocks) {
                std::cout << "    core clock : " << g.clock_core_mhz << " MHz\n";
                if (g.clock_mem_mhz > 0)
                    std::cout << "    mem clock  : " << g.clock_mem_mhz  << " MHz\n";
            }
            if (g.has_pcie)
                std::cout << "    PCIe       : Gen" << g.pcie_gen
                          << " x" << g.pcie_width << "\n";
            if (g.has_fan)
                std::cout << "    fan        : " << g.fan_percent << " %\n";
        }
        record("gpu inventory", !gpus.empty());
    }


    // ============================================================
    // 44) GPU processes
    // ============================================================
    section("44) GPU processes");
    {
        knst_vector<knst_gpu_process> procs = knst_gpu::get_processes();
        std::cout << "  process count : " << procs.size() << "\n";
        uint64_t total_vram = 0;
        for (uint32_t i = 0; i < procs.size() && i < 8; ++i) {
            const auto& p = procs[i];
            total_vram += p.vram_bytes;
            std::cout << "    pid=" << p.pid
                      << "  gpu=" << p.gpu_index
                      << "  vram=";
            if (p.vram_bytes == 0) std::cout << "N/A";
            else std::cout << (p.vram_bytes / (1024ULL * 1024)) << " MB";
            std::cout << "  "; pc(p.name);
            std::cout << "  (" << knst_gpu_vendor_string(p.vendor) << ")\n";
        }
        std::cout << "  total VRAM    : "
                  << (total_vram / (1024ULL * 1024)) << " MB\n";

        // Per-GPU query
        if (knst_gpu::count() > 0) {
            auto gpu0 = knst_gpu::get_processes(0);
            std::cout << "  gpu 0 procs   : " << gpu0.size() << "\n";
        }
        record("gpu processes", true);
    }


    // ============================================================
    // 45) Platform summary
    // ============================================================
    section("45) Platform-specific behaviour summary");
    {
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  Spawn       : CreateProcessW + PROC_THREAD_ATTRIBUTE_HANDLE_LIST\n";
        std::cout << "  stdio       : 3 anonymous pipes (stdin/stdout/stderr)\n";
        std::cout << "  Timeout     : TerminateProcess + drain remaining pipe\n";
        std::cout << "  Signals     : TerminateProcess(exit_code) / Ctrl-Break for detached\n";
        std::cout << "  Suspend     : NtSuspendProcess (ntdll)\n";
        std::cout << "  Process info: Toolhelp32 + PEB read (cmdline, cwd, env)\n";
        std::cout << "  FDs         : NtQuerySystemInformation(64) + NtQueryObject(2)\n";
        std::cout << "  Priority    : SetPriorityClass(IDLE/NORMAL/HIGH/REALTIME)\n";
        std::cout << "  GPU         : NVML + DXGI (memory) + PDH (utilization) + WMI\n";
#else
        std::cout << "  Spawn       : fork + execvpe\n";
        std::cout << "  stdio       : pipe() + dup2()\n";
        std::cout << "  Timeout     : SIGKILL + poll-drain of remaining bytes\n";
        std::cout << "  Signals     : POSIX kill(2), SIGSTOP/SIGCONT/SIGTERM\n";
        std::cout << "  Suspend     : kill(pid, SIGSTOP)\n";
        std::cout << "  Process info: /proc/<pid>/{stat,status,cmdline,environ,fd}\n";
        std::cout << "  FDs         : /proc/<pid>/fd/*\n";
        std::cout << "  Priority    : nice(2) + sched_setaffinity\n";
        std::cout << "  GPU         : NVML (NVIDIA) + sysfs (AMD) + kgsl/mali (Android)\n";
#endif
        record("platform summary", true);
    }


    // ─────────────────────────────────────────────────────────
    // Summary
    // ─────────────────────────────────────────────────────────
    int pass = 0, fail = 0, skip = 0;
    for (auto& r : g_results) {
        if      (r.skipped) ++skip;
        else if (r.passed)  ++pass;
        else                ++fail;
    }
    std::cout << "\n==================================================\n";
    std::cout << "  RESULT: " << pass << " PASS / " << fail << " FAIL";
    if (skip) std::cout << " / " << skip << " SKIP";
    std::cout << "  (total " << g_results.size() << ")\n";
    std::cout << "==================================================\n";

    if (fail > 0) {
        std::cout << "\nFailed tests:\n";
        for (auto& r : g_results)
            if (!r.passed && !r.skipped)
                std::cout << "  - " << r.name << "\n";
    }

    std::cout << "\nDone.\n";
    return fail == 0 ? 0 : 1;
}