/*
  knst_bluetooth_basic.cpp

  Comprehensive demonstration of knst_bluetooth
  Sections:
    1.  Runtime check — BT stack, adapter present?
    2.  Adapter list
    3.  Adapter info (name, address, state)
    4.  Toggle discoverable (safe — restored)
    5.  Scan for nearby devices (8 seconds)
    6.  List known devices
    7.  List paired devices
    8.  List connected devices
    9.  Device info (basic)
    10. Device detail (battery, RSSI, class, UUIDs)
    11. Wait helpers (device / connection)
    12. Pair (API shape only)
    13. Trust / untrust
    14. Connect / disconnect (API shape only)
    15. Pair and connect
    16. Reconnect
    17. Remove (destructive — not executed)
    18. Block / unblock
    19. Platform differences summary
    20. NotSupported / failure demonstration

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
    return "Windows (Win32 Bluetooth)";
#else
    return "POSIX (BlueZ / bluetoothctl)";
#endif
}


static void print_dev(const knst_bluetooth_device& d) {
    std::cout << "  " << d.address;
    if (!d.name.empty()) std::cout << "  " << d.name;
    if (d.paired)    std::cout << "  [PAIRED]";
    if (d.connected) std::cout << "  [CONNECTED]";
    if (d.trusted)   std::cout << "  [TRUSTED]";
    if (d.rssi != 0) std::cout << "  rssi=" << d.rssi << " dBm";
}


int main() {

    std::cout << "Platform: " << platform_name() << "\n";
    std::cout << "NOTE: Destructive operations (pair/connect/remove) are\n";
    std::cout << "      demonstrated as API shape only.\n";

    // =====================================================================
    section("1) Runtime check");
    // =====================================================================
    bool have_adapter = false;
    {
        bool supported = knst_bt::bluetooth::is_supported();
        have_adapter   = knst_bt::bluetooth::has_adapter();
        std::cout << "  supported : " << (supported ? "yes" : "no") << "\n";
        std::cout << "  adapter   : " << (have_adapter ? "yes" : "no") << "\n";
                if (!supported) {
#if KNST_USING_PLATFORM_WINDOWS
            std::cout << "  (no Bluetooth radio found — remaining sections will\n";
            std::cout << "   show graceful failures)\n";
#else
            std::cout << "  (bluetoothctl not found — remaining sections will\n";
            std::cout << "   show graceful failures)\n";
#endif
        } else if (!have_adapter) {
            std::cout << "  (no Bluetooth hardware present — scan and\n";
            std::cout << "   adapter ops will be skipped)\n";
        }
    }

    // =====================================================================
    section("2) Adapter list");
    // =====================================================================
    {
        auto all = knst_bt::bluetooth::adapters();
        if (!all) {
            std::cout << "  adapters() failed: " << all.error_string() << "\n";
        } else {
            std::cout << "  count: " << all.value.size() << "\n";
            for (uint32_t i = 0; i < all.value.size(); ++i) {
                const auto& a = all.value[i];
                std::cout << "  [" << i << "] " << a.address
                          << "  " << (a.name.empty() ? a.alias : a.name) << "\n";
            }
        }
    }

    // =====================================================================
    section("3) Adapter info");
    // =====================================================================
    {
        if (!have_adapter) {
            std::cout << "  (no adapter — skipping)\n";
        } else {
            auto a = knst_bt::bluetooth::adapter();
            if (a) {
                std::cout << "  address      : " << a.value.address << "\n";
                std::cout << "  name         : " << a.value.name << "\n";
                std::cout << "  alias        : " << a.value.alias << "\n";
                std::cout << "  powered      : " << (a.value.powered ? "yes" : "no") << "\n";
                std::cout << "  discoverable : " << (a.value.discoverable ? "yes" : "no") << "\n";
                std::cout << "  pairable     : " << (a.value.pairable ? "yes" : "no") << "\n";
            } else {
                std::cout << "  adapter(): " << a.error_string() << "\n";
            }
        }
    }

    // =====================================================================
    section("4) Toggle discoverable (safe — restored)");
    // =====================================================================
    {
        if (!have_adapter) {
            std::cout << "  (no adapter — skipping)\n";
        } else {
            auto a = knst_bt::bluetooth::adapter();
            if (a) {
                bool orig = a.value.discoverable;
                std::cout << "  original: " << (orig ? "on" : "off") << "\n";

                auto r1 = knst_bt::bluetooth::set_discoverable(true);
                std::cout << "  set_discoverable(true)  : "
                          << (r1 ? "ok" : r1.error_string()) << "\n";

                auto r2 = knst_bt::bluetooth::set_discoverable(orig);
                std::cout << "  set_discoverable(" << (orig ? "true" : "false")
                          << ") : " << (r2 ? "ok" : r2.error_string()) << "\n";
            }
        }
    }

    // =====================================================================
    section("5) Scan for nearby devices (8 seconds)");
    // =====================================================================
    {
        if (!have_adapter) {
            std::cout << "  (no adapter — skipping scan)\n";
        } else {
            std::cout << "  scanning (this takes 8 seconds)...\n";
            auto r = knst_bt::bluetooth::scan(8, true);
            if (r) {
                std::cout << "  found: " << r.value.size() << " devices\n";
                for (uint32_t i = 0; i < r.value.size() && i < 10; ++i) {
                    print_dev(r.value[i]);
                    std::cout << "\n";
                }
                if (r.value.size() > 10)
                    std::cout << "  ... and " << (r.value.size() - 10) << " more\n";
            } else {
                std::cout << "  scan(): " << r.error_string() << "\n";
            }
        }
    }

    // =====================================================================
    section("6) List known devices");
    // =====================================================================
    {
        auto r = knst_bt::bluetooth::list(true);
        if (r) {
            std::cout << "  known: " << r.value.size() << "\n";
            for (uint32_t i = 0; i < r.value.size(); ++i) {
                print_dev(r.value[i]);
                std::cout << "\n";
            }
        } else {
            std::cout << "  list(): " << r.error_string() << "\n";
        }
    }

    // =====================================================================
    section("7) List paired devices");
    // =====================================================================
    {
        auto r = knst_bt::bluetooth::list_paired();
        if (r) {
            std::cout << "  paired: " << r.value.size() << "\n";
            for (uint32_t i = 0; i < r.value.size(); ++i) {
                print_dev(r.value[i]);
                std::cout << "\n";
            }
        } else {
            std::cout << "  list_paired(): " << r.error_string() << "\n";
        }
    }

    // =====================================================================
    section("8) List connected devices");
    // =====================================================================
    {
        auto r = knst_bt::bluetooth::list_connected();
        if (r) {
            std::cout << "  connected: " << r.value.size() << "\n";
            for (uint32_t i = 0; i < r.value.size(); ++i) {
                print_dev(r.value[i]);
                std::cout << "\n";
            }
        } else {
            std::cout << "  list_connected(): " << r.error_string() << "\n";
        }
    }

    // =====================================================================
    section("9) Device info (basic)");
    // =====================================================================
    {
        auto all = knst_bt::bluetooth::list(false);
        if (all && !all.value.empty()) {
            const auto& d0 = all.value[0];
            std::cout << "  inspecting: " << d0.address << "\n";

            auto i = knst_bt::bluetooth::info(d0.address);
            if (i) { print_dev(i.value); std::cout << "\n"; }
            else   std::cout << "  info(): " << i.error_string() << "\n";
        } else {
            std::cout << "  (no known devices — scan or pair first)\n";
        }
    }

    // =====================================================================
    section("10) Device detail (battery, RSSI, class, UUIDs)");
    // =====================================================================
    {
        auto all = knst_bt::bluetooth::list(false);
        if (all && !all.value.empty()) {
            const auto& d0 = all.value[0];
            std::cout << "  inspecting: " << d0.address << "\n";

            auto d = knst_bt::bluetooth::detail(d0.address);
            if (d) {
                std::cout << "  name         : " << d.value.basic.name << "\n";
                std::cout << "  paired       : " << (d.value.basic.paired ? "yes" : "no") << "\n";
                std::cout << "  connected    : " << (d.value.basic.connected ? "yes" : "no") << "\n";
                std::cout << "  battery      : ";
                if (d.value.battery_percent >= 0)
                    std::cout << d.value.battery_percent << "%\n";
                else
                    std::cout << "(not reported)\n";
                std::cout << "  RSSI         : " << d.value.rssi_dbm << " dBm\n";
                if (!d.value.device_class.empty())
                    std::cout << "  class        : " << d.value.device_class << "\n";
                if (!d.value.manufacturer.empty())
                    std::cout << "  manufacturer : " << d.value.manufacturer << "\n";
                std::cout << "  UUIDs        : " << d.value.uuids.size() << "\n";
                for (uint32_t u = 0; u < d.value.uuids.size() && u < 5; ++u)
                    std::cout << "    " << d.value.uuids[u] << "\n";
            } else {
                std::cout << "  detail(): " << d.error_string() << "\n";
            }
        } else {
            std::cout << "  (no known devices to inspect)\n";
        }
    }

    // =====================================================================
    section("11) Wait helpers (safe — 1 s timeout)");
    // =====================================================================
    {
        auto all = knst_bt::bluetooth::list(false);
        if (all && !all.value.empty()) {
            const auto& d0 = all.value[0];
            std::cout << "  wait_for_device(" << d0.address << ", 1000 ms)...\n";
            auto r = knst_bt::bluetooth::wait_for_device(d0.address, 1000);
            std::cout << "  → " << (r ? "found" : r.error_string()) << "\n";
        } else {
            std::cout << "  (no known devices to wait for)\n";
        }
    }

    // =====================================================================
    section("12) Pair (API shape only)");
    // =====================================================================
    {
        std::cout << "  pair(addr) — shows Windows pairing dialog on Windows,\n";
        std::cout << "  pairs via agent on Linux. Not executed to avoid UI pop-ups.\n";
    }

    // =====================================================================
    section("13) Trust / untrust");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        std::cout << "  trust(addr) / untrust(addr) — verified state change.\n";
        std::cout << "  Only trust an already-paired device in prod.\n";
#else
        std::cout << "  NotSupported on Windows (no separate trust flag).\n";
        std::cout << "  Windows trust() is a no-op returning success.\n";
#endif
    }

    // =====================================================================
    section("14) Connect / disconnect (API shape only)");
    // =====================================================================
    {
        std::cout << "  connect(addr) / disconnect(addr) — verified state change.\n";
        std::cout << "  Not executed to avoid disturbing paired devices.\n";
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  Windows connect() enables A2DP/HFP/HID service GUIDs.\n";
#endif
    }

    // =====================================================================
    section("15) Pair and connect (combined flow)");
    // =====================================================================
    {
        std::cout << "  pair_and_connect(addr) = pair → trust → connect.\n";
        std::cout << "  Safe to call on already-paired devices.\n";
    }

    // =====================================================================
    section("16) Reconnect");
    // =====================================================================
    {
        std::cout << "  reconnect(addr) = disconnect → wait → connect.\n";
        std::cout << "  Useful for flaky connections.\n";
    }

    // =====================================================================
    section("17) Remove (destructive — NOT executed)");
    // =====================================================================
    {
        std::cout << "  remove(addr) removes pairing + trust.\n";
        std::cout << "  This is destructive; skipped in demo.\n";
    }

    // =====================================================================
    section("18) Block / unblock");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_LINUX
        std::cout << "  block(addr, true) / block(addr, false) — Linux only.\n";
        std::cout << "  Prevents the device from connecting; block(addr, false) undoes it.\n";
#else
        std::cout << "  NotSupported on Windows (no block flag in Win32 API).\n";
#endif
    }

    // =====================================================================
    section("19) Platform differences summary");
    // =====================================================================
    {
#if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  Backend           : Win32 Bluetooth API (bthprops)\n";
        std::cout << "  Adapter power     : NotSupported (Win10+ Settings app)\n";
        std::cout << "  Pairable toggle   : NotSupported\n";
        std::cout << "  Adapter rename    : NotSupported\n";
        std::cout << "  Trust flag        : No-op (always success)\n";
        std::cout << "  Block             : NotSupported\n";
        std::cout << "  Pair with PIN     : NotSupported\n";
        std::cout << "  Battery / RSSI    : NotSupported\n";
        std::cout << "  Pair              : Shows system pairing dialog\n";
        std::cout << "  Connect           : Via A2DP/HFP/HID service state\n";
#else
        std::cout << "  Backend           : BlueZ (bluetoothctl + rfkill)\n";
        std::cout << "  Adapter power     : Yes (rfkill fallback)\n";
        std::cout << "  Pairable toggle   : Yes\n";
        std::cout << "  Adapter rename    : Yes (system-alias)\n";
        std::cout << "  Trust flag        : Yes\n";
        std::cout << "  Block             : Yes\n";
        std::cout << "  Pair with PIN     : Best-effort (agent script)\n";
        std::cout << "  Battery / RSSI    : Yes (from bluetoothctl info)\n";
        std::cout << "  Pair              : Just Works; PIN via script\n";
        std::cout << "  Connect           : Via BlueZ profile\n";
#endif
    }

    // =====================================================================
    section("20) NotSupported / failure demonstration");
    // =====================================================================
    {
        auto r1 = knst_bt::bluetooth::info(u"not-a-mac");
        std::cout << "  info(\"not-a-mac\")        : "
                  << (r1 ? "ok" : r1.error_string()) << "\n";

        auto r2 = knst_bt::bluetooth::info(u"FF:FF:FF:FF:FF:FF");
        std::cout << "  info(FF:FF:FF:FF:FF:FF) : "
                  << (r2 ? "ok" : r2.error_string()) << "\n";

        auto r3 = knst_bt::bluetooth::pair_with_pin(u"FF:FF:FF:FF:FF:FF", u"", 10);
        std::cout << "  pair_with_pin(empty PIN): "
                  << (r3 ? "ok" : r3.error_string()) << "\n";

        std::cout << "\n  Every function returns result<T> and never throws.\n";
        std::cout << "  Callers can check .ok() or .error_string() to handle.\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}