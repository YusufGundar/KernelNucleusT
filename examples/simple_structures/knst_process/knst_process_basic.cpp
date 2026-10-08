// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_process_basic.cpp

  Basic usage of knst_process - a cross-platform process management
  library with GPU monitoring.

  Shows: running programs, capturing output, shell commands, stdin
  piping, timeouts, detached processes, process listing, process
  inspection, and the GPU bridge.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>



static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}


static const char* platform_name() {
#if KNST_USING_PLATFORM_WINDOWS
    return "Windows";
#else
    return "POSIX";
#endif
}


int main() {

    std::cout << "Platform: " << platform_name() << "\n";

    // =====================================================================
    section("1) run_shell - cross-platform shell command");
    // =====================================================================
    // run_shell() picks the platform default shell:
    //   POSIX   -> /bin/sh -c "command"
    //   Windows -> cmd.exe /c "command"
    {
        knst_process p = knst_process::run_shell(u"echo hello from the shell");
        if (p.error == knst_process_error::None) {
            knst_process_result r = p.communicate(nullptr, 0, 5000);
            std::cout << "stdout   : " << r.out_data << "\n";
            std::cout << "exit code: " << r.exit_code << "\n";
        } else {
            std::cout << "run_shell failed: " << p.error_string() << "\n";
        }
    }

    // =====================================================================
    section("2) communicate - one-shot capture with timeout");
    // =====================================================================
    {
        knst_process p = knst_process::run_shell(u"echo captured!");
        knst_process_result r = p.communicate(nullptr, 0, 5000);

        std::cout << "stdout   : " << r.out_data << "\n";
        std::cout << "exit code: " << r.exit_code << "  timed_out=" << (r.timed_out ? "yes" : "no") << "\n";
    }

    // =====================================================================
    section("3) run - direct program invocation (no shell)");
    // =====================================================================
    // Bypasses the shell; the executable is invoked directly.
    {
        #if KNST_USING_PLATFORM_WINDOWS
                knst_vector<knst_c16string> args;
                args.push_back(u"/c");
                args.push_back(u"ver");
                knst_process p = knst_process::run(u"C:\\Windows\\System32\\cmd.exe", args);
        #else
                knst_vector<knst_c16string> args;
                args.push_back(u"Hello from knst_process!");
                knst_process p = knst_process::run(u"/bin/echo", args);
        #endif
        if (p.error == knst_process_error::None) {
            knst_process_result r = p.communicate(nullptr, 0, 5000);
            std::cout << "stdout   : " << r.out_data << "\n";
            std::cout << "exit code: " << r.exit_code << "\n";
        } else {
            std::cout << "run failed: " << p.error_string() << "\n";
        }
    }

    // =====================================================================
    section("4) stdin piping");
    // =====================================================================
    // Write to the child's stdin, then read its stdout.
    // POSIX:   'cat' echoes stdin.
    // Windows: 'findstr .' echoes non-empty lines.
    {
        #if KNST_USING_PLATFORM_WINDOWS
                knst_process p = knst_process::run(u"C:\\Windows\\System32\\findstr.exe", { u"." });
        #else
                knst_process p = knst_process::run(u"/bin/cat");
        #endif
        if (p.error == knst_process_error::None) {
            const char* input = "line one\nline two\nline three\n";
            knst_process_result r = p.communicate(input, std::strlen(input), 5000);

            std::cout << "echoed back:\n";
            std::cout << r.out_data << "\n";
        } else {
            std::cout << "run failed: " << p.error_string() << "\n";
        }
    }

    // =====================================================================
    section("5) Timeout - kill a slow process");
    // =====================================================================
    // A ~5 second delay with a 300ms timeout gets killed.
    //   POSIX:   /bin/sleep 5
    //   Windows: ping.exe -n 5 127.0.0.1  (~4 seconds)
    {
        #if KNST_USING_PLATFORM_WINDOWS
                knst_process p = knst_process::run(
                    u"C:\\Windows\\System32\\ping.exe",
                    { u"-n", u"5", u"127.0.0.1" });
        #else
                knst_process p = knst_process::run(u"/bin/sleep", { u"5" });
        #endif
        knst_process_result r = p.communicate(nullptr, 0, 300);

        std::cout << "timed_out : " << (r.timed_out ? "yes" : "no") << "\n";
        std::cout << "exit_code : " << r.exit_code << "\n";
    }

    // =====================================================================
    section("6) Detached process");
    // =====================================================================
    // Fire-and-forget. Doesn't block, doesn't capture output.
    {
        #if KNST_USING_PLATFORM_WINDOWS
                knst_process p = knst_process::run_detached(
                    u"C:\\Windows\\System32\\ping.exe",
                    { u"-n", u"3", u"127.0.0.1" });
        #else
                knst_process p = knst_process::run_detached(u"/bin/sleep", { u"2" });
        #endif
        if (p.error == knst_process_error::None) {
            std::cout << "detached pid : " << p.pid() << "\n";
            std::cout << "is_running   : " << (p.is_running() ? "yes" : "no") << "\n";
            std::cout << "(main program continues immediately)\n";
        } else {
            std::cout << "detached failed: " << p.error_string() << "\n";
        }
    }

    // =====================================================================
    section("7) Current process info");
    // =====================================================================
    {
        std::cout << "current pid    : " << knst_process::current_pid()  << "\n";
        std::cout << "parent pid     : " << knst_process::parent_pid()   << "\n";
        std::cout << "current user   : " << knst_process::current_user() << "\n";
        std::cout << "executable     : " << knst_process::current_exe_path() << "\n";
        std::cout << "uid            : " << knst_process::current_uid()  << "\n";
    }

    // =====================================================================
    section("8) Process information - get_info()");
    // =====================================================================
    {
        uint32_t self = knst_process::current_pid();
        knst_process_info info = knst_process::get_info(self);

        std::cout << "pid          : " << info.pid     << "\n";
        std::cout << "ppid         : " << info.ppid    << "\n";
        std::cout << "name         : " << info.name    << "\n";
        std::cout << "exe path     : " << info.exe_path<< "\n";
        std::cout << "cmdline      : " << info.cmdline << "\n";
        std::cout << "state        : " << knst_process_state_string(info.state) << "\n";
        std::cout << "rss bytes    : " << info.rss_bytes << "\n";
        std::cout << "vsz bytes    : " << info.vsz_bytes << "\n";
        std::cout << "thread count : " << info.thread_count << "\n";
        std::cout << "user         : " << info.user    << "\n";
    }

    // =====================================================================
    section("9) System information");
    // =====================================================================
    {
        auto si = knst_process::get_system_info();

        auto gb = [](uint64_t b) { return b / (1024ULL * 1024 * 1024); };

        std::cout << "cpu count    : " << si.cpu_count   << "\n";
        std::cout << "page size    : " << si.page_size   << " bytes\n";
        std::cout << "total memory : " << gb(si.total_memory) << " GB\n";
        std::cout << "used memory  : " << gb(si.used_memory)  << " GB\n";
        std::cout << "free memory  : " << gb(si.free_memory)  << " GB\n";
        std::cout << "uptime       : " << (si.uptime_ms / 1000) << " s\n";
    }

    // =====================================================================
    section("10) List processes (first 10, sorted by pid)");
    // =====================================================================
    {
        knst_vector<knst_process_info> procs = knst_process::list_all();

        std::sort(procs.begin(), procs.end(),[](const knst_process_info& a, const knst_process_info& b) {
                      return a.pid < b.pid;
                  });

        std::cout << "total processes : " << procs.size() << "\n";
        for (uint32_t i = 0; i < procs.size() && i < 10; ++i) {
            const auto& p = procs[i];
            std::cout << "  pid=" << p.pid
                      << "  ppid=" << p.ppid
                      << "  name=" << p.name
                      << "  state=" << knst_process_state_string(p.state)
                      << "\n";
        }
    }

    // =====================================================================
    section("11) Find processes by name");
    // =====================================================================
    // Search for our own process name - guaranteed to find at least one
    // entry on both platforms. On Windows the name includes ".exe".
    {
        uint32_t self = knst_process::current_pid();
        knst_c16string self_name = knst_process::get_info(self).name;

        std::cout << "our own name : " << self_name << "\n";

        knst_vector<knst_process_info> found = knst_process::find_by_name(self_name, false);
            

        std::cout << "found        : " << found.size() << " match(es)\n";
        for (uint32_t i = 0; i < found.size() && i < 5; ++i) {
            std::cout << "  pid=" << found[i].pid << "  " << found[i].name << "\n";
                      
        }
    }

    // =====================================================================
    section("12) Children of the current process");
    // =====================================================================
    {
        uint32_t self = knst_process::current_pid();
        knst_vector<uint32_t> kids = knst_process::children_of(self);

        std::cout << "direct children of pid " << self << " : " << kids.size() << "\n";
        for (uint32_t i = 0; i < kids.size(); ++i) {
            knst_c16string name = knst_process::get_info(kids[i]).name;
            std::cout << "  " << kids[i] << "  " << name << "\n";
        }
    }

    // =====================================================================
    section("13) exists / is_alive");
    // =====================================================================
    {
        uint32_t self = knst_process::current_pid();

        std::cout << "self exists   : " << (knst_process::exists(self)     ? "yes" : "no") << "\n";
        std::cout << "self alive    : " << (knst_process::is_alive(self)   ? "yes" : "no") << "\n";
        std::cout << "self zombie   : " << (knst_process::is_zombie(self)  ? "yes" : "no") << "\n";

        // PID 1 exists on POSIX (init/systemd). On Windows the "System"
        // process is PID 4, and PID 1 may or may not exist depending on
        // the Windows version.
        #if KNST_USING_PLATFORM_WINDOWS
                uint32_t probe = 4;
                std::cout << "System (pid 4): "
                        << (knst_process::exists(probe) ? "yes" : "no") << "\n";
        #else
                uint32_t probe = 1;
                std::cout << "pid 1 exists  : "
                        << (knst_process::exists(probe) ? "yes" : "no") << "\n";
        #endif
    }

    // =====================================================================
    section("14) GPU inventory (NVIDIA via NVML, AMD via sysfs)");
    // =====================================================================
    {
        std::cout << "has_gpu()      : " << (knst_gpu::has_gpu() ? "yes" : "no") << "\n";
        std::cout << "gpu count      : " << knst_gpu::count() << "\n";
        std::cout << "nvml available : " << (knst_gpu::nvml_available() ? "yes" : "no") << "\n";
                  

        knst_c16string drv = knst_gpu::nvidia_driver_version();
        if (!drv.empty()) std::cout << "nvidia driver  : " << drv << "\n";

        knst_c16string cuda = knst_gpu::cuda_version();
        if (!cuda.empty()) std::cout << "cuda version   : " << cuda << "\n";

        knst_vector<knst_gpu_info> gpus = knst_gpu::list_all();
        for (uint32_t i = 0; i < gpus.size(); ++i) {
            const auto& g = gpus[i];
            std::cout << "\n[" << i << "] " << g.name
                      << "  (" << knst_gpu_vendor_string(g.vendor) << ")\n";
            if (g.has_memory) {
                std::cout << "    VRAM used  : " << (g.memory_used  / (1024ULL * 1024)) << " MB / " << (g.memory_total / (1024ULL * 1024)) << " MB\n";
            }
            if (g.has_utilization)
                std::cout << "    GPU load   : " << g.utilization_gpu << " %\n";
            if (g.has_temperature)
                std::cout << "    temp       : " << g.temperature_core << " C\n";
            if (g.has_power)
                std::cout << "    power      : " << (g.power_usage_mw / 1000) << " W (limit " << (g.power_limit_mw / 1000) << " W)\n";
                        if (g.has_clocks) {
                std::cout << "    core clock : " << g.clock_core_mhz << " MHz\n";
                if (g.clock_mem_mhz > 0)
                    std::cout << "    mem clock  : " << g.clock_mem_mhz  << " MHz\n";
            }
            if (g.utilization_mem > 0)
                std::cout << "    mem load   : " << g.utilization_mem << " %\n";
            if (!g.uuid.empty())
                std::cout << "    uuid       : " << g.uuid << "\n";
            if (!g.vbios_version.empty())
                std::cout << "    vbios      : " << g.vbios_version << "\n";
            if (g.has_pcie)
                std::cout << "    PCIe       : Gen" << g.pcie_gen
                          << " x" << g.pcie_width << "\n";
            if (g.has_fan)
                std::cout << "    fan        : " << g.fan_percent << " %\n";
        }
    }

    // =====================================================================
    section("15) Processes using the GPU");
    // =====================================================================
    {
        knst_vector<knst_gpu_process> procs = knst_gpu::get_processes();

        std::cout << "GPU-using processes : " << procs.size() << "\n";
        for (uint32_t i = 0; i < procs.size() && i < 8; ++i) {
            const auto& p = procs[i];
                        std::cout << "  pid=" << p.pid
                      << "  gpu=" << p.gpu_index
                      << "  vram=";
            if (p.vram_bytes == 0)
                std::cout << "N/A";
            else
                std::cout << (p.vram_bytes / (1024ULL * 1024)) << " MB";
            std::cout << "  " << p.name
                      << "  (" << knst_gpu_vendor_string(p.vendor) << ")\n";
        }
    }

    // =====================================================================
    section("16) Priority adjustment");
    // =====================================================================
    // Lower the priority of a short-lived process.
    //   POSIX:   nice(15)  -- positive lowers priority
    //   Windows: BELOW_NORMAL_PRIORITY_CLASS
    {
        #if KNST_USING_PLATFORM_WINDOWS
                knst_process p = knst_process::run(
                    u"C:\\Windows\\System32\\ping.exe",
                    { u"-n", u"2", u"127.0.0.1" });
        #else
                knst_process p = knst_process::run(u"/bin/sleep", { u"1" });
        #endif
        if (p.error == knst_process_error::None) {
            bool ok = p.set_priority(15);
            std::cout << "set_priority(15) : " << (ok ? "ok" : "failed") << "\n";
            p.wait();
        }
    }

    std::cout << "\nDone.\n";
    return 0;
}