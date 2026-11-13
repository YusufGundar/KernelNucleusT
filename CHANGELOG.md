# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] — targeting 2.0.0

> ⚠️ **Breaking release.** Function renames and behavior changes ship in this
> version. See "Migration from 1.0.0" below.

### Added

#### MSVC compatibility — Networking stack

- Compilation issues on MSVC (warnings, data loss) — all fixed. The network stack now compiles cleanly with MSVC.

#### New simple_structures modules

- **`knst_audio.hpp`** — full audio subsystem (PCM buffers, WAV/AIFF/FLAC/OGG codecs, DSP primitives, mixer, device control, AudioFocus helper)
- **`knst_camera.hpp`** — camera capture (V4L2 on Linux, Media Foundation on Windows, Camera2 NDK on Android; streaming, snapshot, video recording)
- **`knst_microphone.hpp`** — capture, VAD, AGC, pitch detection (ALSA on Linux, WASAPI on Windows, AAudio on Android)
- **`knst_bluetooth.hpp`** — adapter enumeration, pairing, bond management, SPP/RFCOMM transports
- **`knst_usb.hpp`** — USB device inventory, descriptors, bulk/interrupt transfers
- **`knst_storage.hpp`** — mount points, filesystem usage, write/read benchmark
- **`knst_sensors.hpp`** — thermal zones, fan speeds, async sensor monitor
- **`knst_video.hpp`** — video writer (AVI/MKV/MP4 with MJPEG), video reader, RGB24 frame I/O
- **`knst_converter.hpp`** — audio/video format conversion, probe, sample-rate conversion
- **`knst_image_loader.hpp`** — PNG/JPEG/BMP/GIF/PNM/TIFF/WEBP decoder with format detection
- **`knst_printer.hpp`** — printing + scanning controller (see dedicated section below)

#### Networking stack — 4 new modules

- **`knst_network.hpp`** — HTTP/1.1 client and full network layer
  - Sync + async DNS resolver with thread-safe LRU/TTL cache
  - DNS-over-HTTPS (RFC 8484) via `knst_doh_resolver`
  - HTTP proxy (absolute-URI for `http`, `CONNECT` tunnel for `https`)
  - SOCKS5 / SOCKS5h support (`socks5://`, `socks5h://`)
  - `NO_PROXY` / `no_proxy` environment handling (httpoxy-safe: only lowercase for plain http)
  - Cookie jar (RFC 6265): domain/path matching, Secure, HttpOnly, Max-Age/Expires, capped at 3000 entries
  - Basic auth (sent to the original origin only, stripped on cross-origin redirects)
  - gzip / deflate decoding with decompression-bomb guards (1000:1 ratio + hard cap)
  - `Content-Encoding: gzip, gzip` chained decoding
  - Keep-alive connection pool with idempotent first-byte retry
  - SSRF guard via `options::allow_private_networks = false` — refuses loopback, RFC1918, CGNAT, link-local, ULA, multicast, reserved; also vets every redirect hop
  - `bind_local_ip` for multi-homed / VPN split routing
  - `options::lenient_close_delimited` for legacy close-delimited responses
  - `options::use_chunked_upload` for streaming uploads
  - `options::on_body` streaming download callback
  - TLS 1.2/1.3 via OpenSSL (defines `KNST_NETWORK_ENABLE_TLS`)
  - System CA bundle auto-loaded (Linux), WinCertStore imported (Windows), Android cacerts directory read (`/apex/com.android.conscrypt/cacerts` + `/system/etc/security/cacerts`)
  - Custom CA + mTLS: `knst_tls_set_ca_file()` / `knst_tls_set_ca_path()` / `knst_tls_set_client_cert()`
  - `knst_tls_set_skip_verify()` — only for LAN devices (self-signed IPP printers, etc.)

- **`knst_websocket.hpp`** — RFC 6455 WebSocket client
  - `ws://` and `wss://`
  - Text / binary / ping / pong / close frames
  - Incoming and outgoing fragmented messages (configurable frame size via `max_frame_bytes`)
  - Configurable message size limit (`max_message_bytes`)
  - Auto-pong on incoming ping (`auto_pong`)
  - Graceful close handshake in both directions
  - Full handshake verification: `Sec-WebSocket-Accept = base64(sha1(key + magic))`
  - Subprotocol negotiation (`Sec-WebSocket-Protocol`)
  - Reuses `knst_http_client` networking: proxy, TLS, DNS cache, SSRF, `bind_local_ip`

- **`knst_network_h2.hpp`** — HTTP/2 client (nghttp2-based)
  - ALPN `h2` negotiation (fails with `NotSupported` if server picks h1)
  - Multiplexed request streams over one TLS connection
  - Per-origin session pool (`knst_web_session_pool<S>`)
  - `Expect: 100-continue` with timeout fallback
  - Request trailers
  - `initial_stream_window` / `connection_window` tuning
  - Automatic session retirement when server sends GOAWAY
  - Safe retry on `REFUSED_STREAM` (guaranteed not processed) + idempotent stale-socket retry
  - Shares `knst_web_client_base<Derived>` with H3: cookie jar, redirects, sensitive header stripping on cross-origin

- **`knst_network_h3.hpp`** — HTTP/3 (QUIC) client
  - ngtcp2 ≥ 1.0 + nghttp3 ≥ 1.1 + `ngtcp2_crypto_quictls`
  - UDP with PMTUD (`IP_MTU_DISCOVER` / `IPV6_MTU_DISCOVER`)
  - TLS 1.3 only (ALPN `h3`, via quictls)
  - Multiplexed request streams over one QUIC connection
  - Session tickets cached under `"h3|host:port"`
  - GOAWAY-driven `H3_REQUEST_REJECTED` safe retry
  - No 0-RTT, no connection migration, no server push, no DATAGRAM
  - Proxies are not supported (`NotSupported`) — needs `CONNECT-UDP`

- **`knst_http_auto_client`** — protocol auto-negotiation
  - Tries h3 → h2 → h1 with per-origin backoff memory (default 5 min)
  - Falls back silently on UDP-blocked networks (only ONE timeout, not one per request)
  - Replay policy: non-idempotent methods and streaming requests fall back only on connect-phase errors
  - Per-protocol cookie jar + connection pool via `h1()` / `h2()` / `h3()`

- **CMake auto-detection** for the network stack
  - Configure-time detection — **no user-facing `ENABLE` flags**
  - Cascade: `find_package()` → `pkg_check_modules()` → `find_library()`
  - Prefix hints: `KNST_QUICTLS_ROOT`, `KNST_NGTCP2_ROOT`, `KNST_NGHTTP3_ROOT`, `KNST_NGHTTP2_ROOT`
  - Opt-outs: `KNST_NETWORK_DISABLE_TLS`, `_H2`, `_H3`
  - **H3 + OpenSSL ABI clash prevention** — when H3 is active, only quictls is linked; system OpenSSL is dropped, RPATH prepends the quictls lib dir
  - Android: quictls + nghttp2 + ngtcp2 + nghttp3 built from source via FetchContent, cached under `$HOME/.knst-cache/quictls-android-<ABI>/`
  - Summary line printed at configure end: `Network: TLS=TRUE H2=TRUE H3=FALSE`

- **Automatic audio codec detection** (CMake)
  - `libfaad2` present → AAC decode (`KNST_NETWORK_HAVE_FAAD2`)
  - `libvorbis` + `libvorbisenc` + `libogg` present → native OGG Vorbis encode (`KNST_NETWORK_HAVE_VORBIS`)

#### `knst_printer.hpp` — new module (printing + scanning)

- **IPP client (RFC 8010/8011)**
  - `Print-Job`, `Create-Job`, `Send-Document`, `Cancel-Job`, `Get-Job-Attributes`, `Get-Jobs`, `Get-Printer-Attributes`, `Set-Job-Attributes`
  - Plain `ipp://` and TLS `ipps://` (via `knst_http_client` — proxy, SSRF, DNS cache, mDNS reuse)
  - IPv6 link-local URIs handled; HTTP chunked responses decoded
  - `print_ipp_smart()` — protocol probe: PDF passthrough → PWG raster → JPEG fallback, all in ONE job when the printer supports it

- **Rich document model** — text runs (font/size/bold/italic/color/spacing/align), shapes (line/rect/fill), page numbers, header/footer, custom page size, custom margins

- **PDF writer** — WinAnsi (12 Type1 fonts, Turkish glyph remap) or full Unicode via TTF Type0/CIDFontType2/Identity-H embedding
  - `embed_font_path` for explicit font, `auto_fallback` for NotoSans/DejaVu/Arial auto-lookup
  - PNG writer with real DEFLATE + Sub filter (40-70% smaller than stored blocks)

- **Test pages** — `Diagnostic`, `ColorBar`, `Alignment`, `Grid`, `Gradient`, `NozzleCheck`

- **Photo batch** — grid layout (rows × cols), EXIF orientation, `Fit`/`Fill`/`Stretch`, per-image downscale to target DPI, filename captions, border, skip-unreadable

- **Mixed batch** — images + text + PDFs in one job, order preserved, `print_batch()`

- **Job control** — cancel / hold / release / priority / wait / reprint (cache), `print_secure` (PIN), `print_held` (deferred)

- **Linux** — CUPS backend via `lp`, `lpstat`, `lpadmin`, `lpoptions`, `cancel`, `lpmove`; PPD option fallback for `Duplex`, `ColorModel`, `InputSlot`, `OutputBin`

- **Windows** — winspool + GDI rendering via `DocumentProperties` + `CreateDC` + `StartDoc`; native `printto` shell fallback; job control via `SetJob`

- **Android** — `PrintManager` dialog via JNI (`com.knst.print.KnstPrintAdapter`); USB OTG raw (bulk transfer) with dynamic class/endpoint scan; Bluetooth SPP raw

- **Cross-platform raw path** — TCP 9100 (network raw), USB bulk (Linux + Android OTG), Bluetooth SPP (Android)

- **Scanner controller (eSCL / AirScan)**
  - `list_all()` via mDNS (`_uscan._tcp.local` + `_scanner._tcp.local`) — JNI on Android/Windows, avahi on Linux
  - `capabilities()` — resolutions, color modes, sources (Flatbed/ADF/ADF Duplex)
  - `scan_page()` / `scan_to_file()` / `scan_to_files()` / `scan_to_pdf()` — single-page or ADF batch
  - `scan_and_print()` — scan → print in one call
  - Output formats: JPEG (native eSCL), PNG (JPEG → decode → write), PDF (JPEG → embed), multi-page ADF auto-numbered

- **PDF rasterizer** — `rasterize_pdf_to_jpegs()` for PWG/JPEG IPP paths
  - Linux/Windows: `pdftoppm` (poppler-utils) → ImageMagick fallback
  - Android: JNI `KnstPdfRasterizer` (Android `PdfRenderer` API)

- **Reprint cache** — LRU-evicted on-disk cache of printed files, `cache_for_reprint` per job, `cache_find()` / `cache_clear()` / `cache_size_bytes()`

- **Safe token validation** — `is_safe_token`, `is_page_range`, `is_mac`, `is_pci_addr`, `is_dev_path`, `is_ipv4` — prevents shell injection into `lp`/`lpstat`/`lpadmin`

#### `knst_process.hpp` — Windows instrumentation + Android + GPU monitoring

- **Windows PEB readers** (was only cmdline in 1.0.0):
  - `ReadCwdFromPEB(hProcess, target_wow64, out)` — current working directory
  - `ReadEnvFromPEB(hProcess, target_wow64)` → `knst_vector<knst_env_var>` — full environment block (32 KB → 256 KB dynamic growth)
  - `ReadCmdlineFromPEB` — already present in 1.0.0
  - All three handle WOW64 targets (32-bit process on 64-bit host) via `ProcessWow64Information` + PEB32 offsets

- **Windows process state via `NtQuerySystemInformation(SystemProcessInformation)`**
  - `get_state_via_ntqsi(pid)` — Running / Sleeping / DiskSleep / Stopped / Dead
  - Aggregates per-thread states (WrQueue detection → DiskSleep vs Sleeping)
  - Automatic buffer growth on `STATUS_INFO_LENGTH_MISMATCH` (0xC0000004)

- **Windows handle-table enumeration** for `get_fds(pid)`
  - `NtQuerySystemInformation(SystemExtendedHandleInformation)` → filter by PID
  - `DuplicateHandle` + `NtQueryObject(ObjectTypeInformation)` for type detection
  - `GetFinalPathNameByHandleW` for file paths
  - Pipes recognized via `GetFileType(FILE_TYPE_PIPE)`

- **Windows SID → uid simulation**
  - `SidToUid(PSID)` — FNV-1a hash of identifier authority + sub-authorities (never 0)
  - `GetProcessUidByHandle(HANDLE)` — token → `TokenUser` → `SidToUid`

- **GPU monitoring (`knst_gpu` class)**
  - `knst_gpu_info` — name, vendor, driver version, uuid, vbios, memory total/used/free, utilization (gpu/mem), temperature, power usage/limit, clocks (core/mem), fan %, pcie gen/width
  - `knst_gpu_process` — per-process GPU usage: pid, gpu_index, vram_bytes, vendor
  - `knst_gpu::list_all()` / `get()` / `count()` / `has_gpu()`
  - `knst_gpu::get_processes()` / `get_processes(gpu_index)`
  - `knst_gpu::nvidia_driver_version()` / `cuda_version()` / `nvml_available()`
  - **NVIDIA (NVML)** — dlopen/LoadLibrary dynamic load; full API surface (init, mem, util, temp, power, clocks, fan, pcie, vbios, cuda, running processes v1 + v2)
  - **AMD (Linux sysfs)** — `/sys/class/drm/card*/device/{mem_info_vram_*, gpu_busy_percent, current_link_*, hwmon/*, pp_dpm_sclk}`; per-process VRAM via `/proc/PID/fdinfo` `drm-memory-vram`
  - **Intel (Linux sysfs)** — `gt_act_freq_mhz`, `gt_cur_freq_mhz`, `gt_RP0_freq_mhz`, `gt_busy_percent`
  - **Windows** — DXGI (`IDXGIFactory1` + `IDXGIAdapter3::QueryVideoMemoryInfo`) + PDH (`\GPU Engine(*)\Utilization Percentage` with 2s cache) + WMI `Win32_VideoController` fallback; DXGI + PDH LUID matching
  - **Android** — Qualcomm Adreno via `/sys/class/kgsl/kgsl-3d0/{gpu_busy_percentage, gpuclk, gpubusy}`; ARM Mali via `/sys/class/misc/mali0/device/{utilisation, clock}`
  - **Vendor deduplication** — NVIDIA from NVML takes priority; DXGI/sysfs skipped for NVIDIA when NVML is active
  - **Sentinel VRAM handling** — NVML's 0xFFFFFFFFFFFF "unknown" sentinel mapped to 0
  - `knst_gpu_vendor` enum — Unknown / NVIDIA / AMD / Intel / Apple / Other
  - `knst_process_info::gpu` field + `uses_gpu()` helper — per-process GPU info folded into `list_all(include_gpu=true)`

- **Android shell path** — `ResolveShell()` uses `/system/bin/sh` (was `/bin/sh` — nonexistent on Android)
- **Android process info** — `get_info(pid)` only reads `/proc/PID/{exe,cmdline,cwd}` for self process; other PIDs return `has_exe/has_cmdline/has_cwd = false` (SELinux blocks `/proc/PID/exe` for other processes)
- **Android uptime fallback** — `get_system_info().uptime_ms` falls back to `CLOCK_BOOTTIME` when `/proc/uptime` is unavailable
- **Windows `communicate()`** now uses `knst_thread` for the stdin writer (was raw `std::thread`) — matches library conventions

#### `knst_devices.hpp` — Android native backend + `knst_dev` utilities

- **`knst_dev` namespace** — new utility layer:
  - `status` enum + `status_string()` — Ok / NotFound / PermissionDenied / Busy / NotSupported / …
  - `result<T>` / `result<void>` — error-carrying value wrapper with `ok()` / `error_string()` / `native` code
  - `unique_fd` — move-only RAII fd wrapper
  - `argv` builder — `<<` operator (const char*, knst_c16string, unsigned, int) + `push_back` for legacy call sites
  - `run_tool(prog, args, timeout_ms)` → `result<knst_byte_string>`
  - `run_tool_raw(prog, argv, timeout)` → `{launched, exit_code, out}` (never early-returns on non-zero exit)
  - `run_codes(prog, argv, timeout, ok_codes)` — accepts extra exit codes (e.g. coreutils `timeout` → 124)
  - `drop(result<T>)` → `result<void>` — discards value
  - `format(fmt, ...)` — printf-into-knst_c16string
  - `text(knst_byte_string)` — safe `const char*` accessor (empty string on empty)
  - `for_each_line(text, fn)` — no-allocation line iterator with `\r\n` stripping
  - `map_errno(int)` / `map_win_error(unsigned long)` — error code translation
  - Validators: `is_safe_token`, `is_page_range`, `is_mac`, `is_pci_addr`, `is_dev_path`, `is_ipv4`, `is_ipv4_cidr`
  - `u32_to_string(uint32_t)` — no-locale conversion

- **`knst_dev::uevent_monitor`** — Linux-only kernel hotplug listener
  - `uevent_monitor::open()` — `NETLINK_KOBJECT_UEVENT` socket + `bind` on group 1
  - `wait(timeout_ms, subsystem)` → `result<uevent>`
  - **Kernel-only filter** — `sockaddr_nl.nl_pid == 0` (rejects user-space event spoofing)
  - `uevent` struct: `action`, `subsystem`, `devtype`, `devpath`, `devname`
  - Windows: `uevent` struct only (data-only stub; all uevent APIs return `NotSupported`)

- **`knst_dev::sysfs_read` / `sysfs_write`** — path-restricted helpers
  - Both require `/sys/` prefix, reject `..` in path (`is_sysfs_path`)
  - `sysfs_read(path)` → `result<knst_c16string>` (trailing whitespace stripped)
  - `sysfs_write(path, value)` → `result<void>` (`write_all` handles EINTR + short writes)

- **Android native backend (`knst_devices::detail_android`)** — replaces all no-op stubs:
  - `set_java_vm(vm, activity)` — JNI bridge init + idempotent re-init
  - `get_env()`, `clear_jni_exception()`, `getprop()`, `jstr()`, `get_service()`, `find_app_class()`
  - **`find_app_class()`** — routes through activity classloader (system classloader cannot see app classes on Android)
  - `motherboard()` — `ro.product.*`, `ro.boot.serialno`, `ro.build.*`
  - `storage()` — `Environment.getDataDirectory()` + `getExternalStorageDirectory()` + `StatFs` (Long API with `int` fallback for pre-API 18)
  - `usb()` — `UsbManager.getDeviceList()` via JNI
  - `audio()` — `AudioManager.getDevices()` via `KnstDevicesHelper.getAudioDevicesInfo()`
  - `power()` — three-tier: sysfs → `ACTION_BATTERY_CHANGED` sticky broadcast → `BatteryManager.getIntProperty(4)`
  - `sensors()` — NDK `ASensorManager` / `ASensorList`
  - `bluetooth()` — `BluetoothAdapter.getBondedDevices()`
  - `cameras()` — NDK `ACameraManager` + `ACameraMetadata` (lens facing, pixel array size)
  - `printers()` — `KnstPrintDiscoveryHelper.discover()` via JNI
  - `system()` — `ro.product.*` + `Build.getSerial()` fallback chain (`ro.serialno` → `ro.boot.serialno` → `ril.serialnumber` → `persist.radio.serialno` → JNI) + emulator detection (`goldfish`/`ranchu`/`sdk`/`Emulator`)
  - `os()` — `ro.build.*` + `uname()` + `CLOCK_BOOTTIME` uptime fallback + `/proc/stat btime` fallback
  - `enrich_network_wifi()` — `KnstDevicesHelper.getWifiInfo()` (SSID, RSSI, link_mbps, freq, bssid)
  - **`knst_file::set_android_dirs(cache, home, external)`** — called from `set_java_vm()` via `KnstDevicesHelper.getAppPaths()`

- **Android JNI helpers bound at startup** (via activity classloader):
  - `com.knst.devices.KnstDevicesHelper.setContext(activity)` — `getAudioDevicesInfo()`, `getWifiInfo()`, `getAppPaths()`, `getSystemSerial()`
  - `com.knst.devices.KnstBluetoothHelper.setContext(activity)` — adapter enumeration

- **JSON bridge** (guarded by `KNST_JSON_HPP`):
  - `knst_devices::full_inventory_json()` — CPU + memory + system + OS + storage + network as one JSON object
  - `knst_devices::save_inventory_json(path, options)` — file writer helper

- **DMI chassis type helpers** — extracted from `system()`:
  - `chassis_type_name(int code)` — 36 SMBIOS chassis codes → readable string
  - `normalize_chassis_type(knst_c16string&)` — idempotent numeric → text (safe on already-text input)

#### `knst_file.hpp` — Android JNI path bridge

- **`knst_file::set_android_dirs(temp, home, external)`** — JNI bridge entry point
  - Called once at app startup from `android_main` / `JNI_OnLoad`
  - `temp`     → `Context.getCacheDir()`         (e.g. `/data/user/0/<pkg>/cache`)
  - `home`     → `Context.getFilesDir()` parent  (e.g. `/data/user/0/<pkg>`)
  - `external` → `Context.getExternalFilesDir()` (e.g. `/storage/emulated/0/Android/data/<pkg>/files`)
- **`get_temp_dir()` / `get_home_dir()` on Android** use the JNI-provided paths
  - Fallback chain: JNI path → `/data/local/tmp` → empty string (no writable temp dir)
- **`get_executable_dir()` on Android** returns empty string (documented) — `/proc/self/exe` points to the zygote binary, not the app directory
- **`#ifndef PATH_MAX` fallback to 4096** — for Android toolchains where `<limits.h>` doesn't define it
- **`knst_file_detail::g_android_temp_dir()` / `g_android_home_dir()` / `g_android_external_dir()`** — static storage filled by `set_android_dirs()`

#### `knst_android_context.hpp` — new central JNI context

- Process-wide `JavaVM + Activity (NewGlobalRef) + ClassLoader` singleton
- `init()` / `update_activity()` / `shutdown()` lifecycle
- Idempotent `init()` via `IsSameObject` (not pointer equality — different handle-table slots can reference the same Java object)
- Attach guard — `shutdown()` only detaches threads it attached; a caller thread already attached stays attached
- `env()` — per-calling-thread `JNIEnv*`, auto-attach
- `find_class()` from any native thread (via cached ClassLoader — the system classloader cannot see app classes on Android)
- Rotation handling — `update_activity()` refreshes the cached ClassLoader (it can change across Activity recreations)
- **`session` RAII wrapper** — construct = init, destruct = `shutdown()` + `ANativeActivity_finish()`
- Replaces the three legacy per-module inits with one call (legacy functions still work internally)

#### Android backend per module

- **`knst_devices`** — AudioManager volume/mute, BluetoothAdapter, Camera2 NDK enumeration
- **`knst_audio`** — AAudio stream backend, AudioManager volume control, AudioFocus helper (`com.knst.print.KnstAudioFocus`)
- **`knst_camera`** — Camera2 NDK backend (streaming, snapshot, video recording)
- **`knst_bluetooth`** — BluetoothAdapter JNI (bond / connect / disconnect / setPin)
- **`knst_microphone`** — AAudio capture, VAD, AGC, pitch detection
- **`knst_window`** — `ANativeWindow` + `ANativeActivity` bindings
- **`knst_network`** — JNI system proxy detection via `java.lang.System.getProperty`
- **`knst_printer`** — JNI helper binding for `KnstPrintDiscoveryHelper` / `KnstMlKitScannerHelper`

#### Android context — compile-time and runtime checks

**Compile-time:**
- `KNST_HAVE_ANDROID_APP_STRUCT` guard with `__has_include` probing **three** different `native_app_glue.h` paths (`<android_native_app_glue.h>`, `<android/native_app_glue.h>`, `<native_app_glue.h>`) — Android NDK layouts vary across versions
- `session` RAII is only compiled when the struct is available; targets without `native_app_glue` still get a working `context`
- `knst_android_context.hpp` is a no-op on non-Android targets (safe to include everywhere)

**Runtime — helper check:**
- Each Java helper bound at startup is checked for presence
- Missing **required** helper prints to stderr:
  ```
  [KNST] missing required helper: com.knst.print.KnstPrintDiscoveryHelper
  ```
- Missing **optional** helper prints to stderr:
  ```
  [KNST] optional not linked helper: com.knst.print.KnstMlKitScannerHelper
  ```
- **No crash on missing helpers** — the affected feature returns `NotSupported` and the app continues

**Runtime — lifecycle check:**
- `init()` fast-path via `IsSameObject` — reinit with same Activity is a no-op, not a new global ref
- Attach guard in `init()` and `shutdown()` — only detaches if this call attached
- `onAppCmd` chain: `APP_CMD_INIT_WINDOW` / `APP_CMD_CONFIG_CHANGED` trigger `update_activity()`, then chain to any user handler

#### CPU — Linux improvements

- **L1 / L2 / L3 cache sizes** from `/sys/devices/system/cpu/cpu0/cache/index*/` — authoritative source
  - Handles `K` / `M` / `G` size suffix
  - Falls back to `/proc/cpuinfo` `cache size` only for L2 (per-core L2)
- **CPU usage** cached with **200ms TTL + mutex** — 1.0.0 sampled 20ms on every call
- **ARM vendor detection** from `CPU implementer` (`0x41`=ARM, `0x51`=Qualcomm, `0x4D`=Apple, `0x53`=Samsung, `0x61`=Apple)
- **ARM board names** — reads `Hardware` and `Processor` fields for SBCs (Raspberry Pi, etc.)

#### Windows — process info now actually populated

- `get_info(pid)` on Windows now fills `cwd` (PEB), `uid` (SID hash), `has_cwd`, `has_user` — 1.0.0 returned empty strings / `0` / `false`
- `get_state(pid)` on Windows uses `NtQuerySystemInformation` — 1.0.0 returned binary `Running` / `Dead`
- `get_fds(pid)` on Windows returns actual handles via `NtQuerySystemInformation(SystemExtendedHandleInformation)`
- `get_env(pid)` on Windows reads target PEB environment block
- `get_uid(pid)` on Windows returns an FNV-1a hash of the token user SID
- `send_signal(sig)` on Windows attempts `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT)` for SIGTERM/SIGINT on detached processes — 1.0.0 always called `TerminateProcess`

### Changed

- **Android init** — one call replaces three per-module legacy inits:
  - `knst_devices::detail_android::set_java_vm()` — still works
  - `knst_net_detail::android_jni::set_context()` — still works
  - `knst_prn::detail::set_jni_context()` — still works

  New code should call `knst_android::context::instance().init(vm, activity)` or use the `session` RAII wrapper. Legacy calls are internally chained by `context::init()` via `configure_modules()`.

- **CMake** — network tiers resolved at configure time. No user-facing `KNST_NETWORK_ENABLE_*` flags. See README §2.1 for details.

- **`/proc/cpuinfo` `cache size`** now maps to `cache_l2_kb` (was `cache_l3_kb` — the per-core field is L2, not L3)

- **`knst_devices::pci()`** prefers `max_link_speed` / `max_link_width` over `current_*` — 1.0.0 read `current_*` which reflects ASPM-downclocked (idle) values, making Gen4 cards appear as Gen1

- **`knst_devices::pci()` on Windows** now maps `PNPClass` to human-readable class name (`Display`, `Network`, `Audio`, `Storage`, `USB Controller`, `System`, `Memory`, `Bluetooth`, `HID`, `Security (TPM)`) — 1.0.0 returned empty `class_name`

- **`knst_devices::system()` VM detection** expanded — added Xen, Amazon EC2, Google, OpenStack, Oracle, Parallels, DigitalOcean as manufacturers; added Virtual Machine / KVM / Bochs / Xen HVM as product name markers; removed bare "Microsoft Corporation" (matched Surface bare-metal)

- **`knst_devices::system()` `is_laptop` detection** — `Tablet` and `Detachable` chassis types now included (Surface Pro, convertibles)

- **`knst_devices::memory()`** `used_bytes` fallback — if `MemAvailable` is absent (kernel < 3.14), uses `free + cached + buffers`

- **`knst_devices::storage()` on Windows** — NVMe drives detected via `MSFT_PhysicalDisk.BusType == 17` — 1.0.0 only checked `MediaType` (SSD/HDD); a NVMe drive would show as generic SSD

- **`knst_devices::monitors()` on Windows** — connection type checked on **both** `adapter.DeviceString` and `mon.DeviceString` (1.0.0 only checked adapter)

- **`knst_devices::motherboard()` / `system()` on Windows** — now query `Win32_SystemEnclosure` for `ChassisTypes` + `SerialNumber` (1.0.0 returned empty `chassis_type` / `chassis_serial`)

- **`knst_devices::audio()` on Linux** — checks both PulseAudio and PipeWire sockets (`/run/user/<uid>/pulse/native` + `/run/user/<uid>/pipewire-0`) — 1.0.0 only checked PulseAudio

- **`knst_devices::audio()` on Linux** — uses `pactl list sinks` (long format) instead of `pactl list short` — descriptions and default device names now available

- **`knst_file::join_path()`** now always produces native separators on Windows
  - Before: picked the separator from the first argument (`/` if `a` had `/`)
  - After: always `\` on Windows, matching `std::filesystem::path::operator/`
  - POSIX: no change

### Fixed

- **`knst_devices::memory()` `/proc/meminfo` parser** — `parse_kb()` no longer mutates the buffer across calls — 1.0.0 replaced `\n` with `\0` in-place, causing subsequent `parse_kb()` calls to skip lines

- **`knst_devices::power()`** — `ac_online` propagation fixed — `readdir()` order is not guaranteed; 1.0.0 set `ac_online` inline while iterating, so `BAT0` before `AC` got `ac_online = false` even with AC plugged in. Now two-pass: collect batteries → propagate `ac_seen` / `ac_on`

- **`knst_devices::power()`** — peripheral battery filter via `/sys/class/power_supply/*/scope` — HID mice/keyboards' batteries no longer mixed with system battery

- **`knst_devices::usb()`** — interface nodes (`1-1:1.0` style) skipped — 1.0.0 could produce duplicate entries

- **`knst_devices::storage()`** — `sr*` (optical drives) no longer filtered out; `mmcblk*` distinguishes eMMC from SD card by removable flag; `md*` and `fd*` get proper `RAID` / `Floppy` type; removable `sdX` inferred as `USB` type

- **`knst_devices::bluetooth()`** — fast-path when `/sys/class/bluetooth` is empty — 1.0.0 hung on `bluetoothctl` D-Bus timeout (~1.5s on systems without adapter)

- **`knst_devices::printers()`** — fast-path when CUPS socket is absent — 1.0.0 ran `lpstat` unconditionally (2s penalty on headless systems)

- **`knst_devices::cameras()`** — checks V4L2 `VIDIOC_QUERYCAP` + `V4L2_CAP_VIDEO_CAPTURE` — skips UVC metadata nodes that 1.0.0 listed as cameras

- **`knst_devices::motherboard()` / `system()` on Windows** — chassis type + serial now populated (was empty)

- **`knst_devices::pci()` on Windows** — `PNPClass` now mapped to human-readable names (was empty)

- **`knst_devices::storage()` on Windows** — NVMe detected via `BusType == 17` (was generic SSD)

- **`knst_devices::system()` VM detection on Windows** — `GetFirmwareType` now dynamically resolved via `GetProcAddress` — static import required Win8+, breaking Win7

- **`knst_file::create_symlink()` on Windows** — retry without `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE` on `ERROR_INVALID_PARAMETER`
  - Fixes symlink creation on **Windows 7 / 8.x** (which don't support the flag)
  - Fixes symlink creation on **Windows 10 without Developer Mode** + non-admin
  - Fallback requires admin rights, but at least works where old code always failed

### Removed

- `knst_devices::knst_device_error` enum — was declared but never populated (`last_error()` always returned `None`)
- `knst_devices::knst_device_error_string()` — helper for the above
- `knst_devices::last_error()` — was a stub always returning `knst_device_error::None`

### Breaking

- **Function renames** — several functions were renamed to reflect their changed purpose. Old names are **not** available as aliases. See Migration section below for examples and update call sites accordingly.
- **Behavior changes** — some functions with unchanged names now return different results (e.g. `knst_devices::cpu().cache_l3_kb` may report `0` where 1.0.0 returned the per-core L2 value; `knst_process::get_uid(pid)` on Windows returns an SID hash instead of `0`; `knst_file::join_path()` on Windows always emits `\`). See Migration section below.
- **Removed APIs** — `knst_device_error` enum, its `_string()` helper, and `last_error()` stub. See Migration section below.

### Migration from 1.0.0

**Android initialization** — replace three per-module calls with one central call:

```cpp
// Before (1.0.0) — three separate calls, three global refs, three classloader lookups
knst_devices::detail_android::set_java_vm(vm, activity);
knst_net_detail::android_jni::set_context(vm, activity);
knst_prn::detail::set_jni_context(vm, activity);

// After (2.0.0) — one call, fans out to all modules internally
knst_android::context::instance().init(vm, activity);

// Recommended — RAII wrapper that also finishes the activity on scope exit:
void android_main(struct android_app* app) {
    knst_android::session session(app);   // ← only line that knows `app`
    if (!knst_android::session::activity()) return;
    // ... everything else, zero JNI boilerplate
}
```

**`knst_devices::knst_device_error` removed** — replace with `knst_dev::status`:

```cpp
// Before (1.0.0)
auto err = knst_devices::last_error();   // always None, useless

// After (2.0.0) — use knst_dev::status directly
knst_dev::status s = knst_dev::status::Ok;
```

**`knst_devices::cpu()` cache level fix** — `cache_l3_kb` may now report `0` on systems where 1.0.0 reported the per-core L2 size (per-core `cache size` in `/proc/cpuinfo` is L2, not L3). Real L3 is now read from `/sys/devices/system/cpu/cpu0/cache/index3/size`:

```cpp
// 1.0.0: cache_l3_kb = /proc/cpuinfo "cache size" (actually L2)
// 2.0.0: cache_l2_kb = /proc/cpuinfo "cache size" (correct)
//        cache_l3_kb = /sys/.../index3/size (real L3)
```

**`knst_process::get_uid(pid)` on Windows** — now returns a SID hash instead of `0`. Code that checked `if (uid == 0)` to detect "unknown" should migrate to `has_user`:

```cpp
// Before (1.0.0)
if (info.uid != 0) { /* has user */ }

// After (2.0.0)
if (info.has_user) { /* has user */ }
```

**`knst_file::join_path()` on Windows** — now always produces native separators:

```cpp
// Before (1.0.0) on Windows
auto p = knst_file::join_path(u"C:/folder", u"file.txt");
// p == "C:/folder/file.txt"

// After (2.0.0) on Windows
auto p = knst_file::join_path(u"C:/folder", u"file.txt");
// p == "C:\\folder\\file.txt"
```

---


## v1.0.0 — Stable Release

The beta process is over. This is the **first stable** release of the library. Tested on all platforms (Windows, Linux X11, Linux Wayland, Linux Dual, Android) through leak tests and other tests, verified on real hardware. Many technical changes were made since the beta version. I've done my best, and along the way I realized how important comment lines and code layout really are, and how crucial it is to run every feature test and leak test on every platform.

Right now, on my machine, I have 2 different operating systems, an Android development environment, Linux (lots of desktop environments for X11/Wayland, specifically testing GNOME's title bar, etc.) — so comprehensive tests were done on every platform under every condition, fixed until the results were exactly what I wanted, and now here's `knst_window` — ready for you.