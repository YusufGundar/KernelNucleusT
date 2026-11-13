# knst_network — Cross-Platform Network Library

Hey! 👋 `knst_network.hpp` is KernelNucleusT's **network module**. All in one header: error model, IP/endpoint, DNS (sync/async/cache/DoH), TCP/UDP/Unix sockets, TLS (OpenSSL), URL parser (RFC 3986), and a full **HTTP/1.1 client** (redirect + cookies + gzip + proxy + keep-alive + SSRF guard).

**In short:** No `curl` dependency, header-only. Link `-lssl -lcrypto` and HTTPS works. **HTTP/2 and HTTP/3 are not in this header** — they're separate headers (`knst_network_h2.hpp`, `knst_network_h3.hpp`) activated by the CMake `H2`/`H3` tiers.

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

| Feature | Windows | Linux | Android |
|---|---|---|---|
| **TCP client / server** | ✅ | ✅ | ✅ |
| **UDP + multicast** | ✅ | ✅ | ✅ |
| **Unix domain socket** | ✅ (Win10+) | ✅ | ✅ |
| **DNS sync (`getaddrinfo`)** | ✅ | ✅ | ✅ |
| **DNS async (thread)** | ✅ | ✅ | ✅ |
| **DNS cache (LRU + TTL)** | ✅ | ✅ | ✅ |
| **TLS (OpenSSL 1.1+)** | ✅ + cert store | ✅ + CA dirs | ✅ + CA dirs |
| **mTLS (client cert)** | ✅ | ✅ | ✅ |
| **TLS session resumption** | ✅ | ✅ | ✅ |
| **HTTP/1.1 client** | ✅ | ✅ | ✅ |
| **HTTP proxy + CONNECT** | ✅ | ✅ | ✅ |
| **SOCKS5 / SOCKS5h** | ✅ | ✅ | ✅ |
| **NO_PROXY env** | ✅ | ✅ | ✅ (+ JNI system proxy) |
| **Redirect (RFC 3986)** | ✅ | ✅ | ✅ |
| **Cookie (RFC 6265)** | ✅ | ✅ | ✅ |
| **gzip / deflate** | ✅ | ✅ | ✅ |
| **Keep-alive pool** | ✅ | ✅ | ✅ |
| **Streaming response** | ✅ callback | ✅ | ✅ |
| **SSRF guard** | ✅ | ✅ | ✅ |
| **DoH (DNS over HTTPS)** | ✅ | ✅ | ✅ |
| **Public IP discovery** | ✅ (ipify) | ✅ | ✅ |
| **HTTP/2** | ❌ not in this header | ❌ | ❌ |
| **HTTP/3 / QUIC** | ❌ not in this header | ❌ | ❌ |
| **WebSocket** | ⚠️ only `open_raw_socket()` | ⚠️ | ⚠️ |
| **ICMP / ping** | ❌ | ❌ | ❌ |
| **FTP / SMTP / raw prot.** | ❌ | ❌ | ❌ |

**Critical notes:**
- **HTTP/2 and HTTP/3 are not in this header.** ALPN only advertises `http/1.1`. The CMake `H2`/`H3` tiers activate separate headers; this header does not use them.
- **WebSocket** has its own `knst_websocket.hpp`. This header only provides `open_raw_socket()` — the byte stream is yours after the upgrade handshake.
- **Without TLS** (no `KNST_NETWORK_ENABLE_TLS`) `https://` → `TlsNotSupported`. Plain `http://` still works.
- **Hostnames must be ASCII.** If you need IDN (internationalized domain names), **convert to punycode yourself** — the library does not do it.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **RFC 3986 URL parser** | `remove_dot_segments`, redirect resolution, request-target encoding |
| **RFC 6265 cookie jar** | Domain/Path/Secure/Expires/Max-Age, IP-literal guard |
| **HTTP smuggling guard** | Conflicting `Content-Length`, `CL + TE` together → reject |
| **gzip / deflate / multi-member** | Streaming DEFLATE, allocation-safe, bomb guard (1000× ratio) |
| **Keep-alive pool** | LRU, per-host limit, stale socket retry (idempotent) |
| **Proxy (HTTP + SOCKS5)** | CONNECT tunnel, absolute-URI, NO_PROXY |
| **SSRF guard** | `allow_private_networks=false` → loopback/RFC1918/CGNAT/ULA/link-local refused |
| **Streaming download** | `on_body` callback, no RAM buffering |
| **Chunked upload** | `use_chunked_upload = true` |
| **Async DNS** | Deadline-bounded, 32 thread cap, IP literal fast-path |
| **DNS cache** | Thread-safe LRU, TTL, entry cap |
| **DoH** | RFC 8484, `application/dns-message` |
| **TLS session cache** | 64-entry LRU, `lower(host):port` key |
| **SIGPIPE guard** | Thread-local masking for `SSL_write` on POSIX |
| **Android system proxy** | JNI `System.getProperty("http.proxyHost")` |
| **Cookie Secure guard** | `Set-Cookie: Secure` over plaintext → reject |
| **Cross-origin sensitive strip** | `Authorization`, `Cookie`, `X-Api-Key` dropped on redirect |
| **httpoxy guard** | For `http://` only lowercase `http_proxy`, `HTTP_PROXY` ignored |

---

## 🚀 Quick Start

### Simple GET

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    knst_http_client c;
    knst_http_client::options o;
    o.timeout_ms = 5000;

    knst_http_response r;
    knst_network_error err;

    if (c.get(u"http://example.com", o, r, err)) {
        std::cout << "Status: " << r.status << "\n";
        std::cout << "Body:   " << r.body.length() << " bytes\n";
    } else {
        std::cerr << "Error: " << knst_network_error_string(err) << "\n";
    }
}
```

### HTTPS + JSON POST

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
    knst_http_client c;
    knst_http_client::options o;
    o.timeout_ms       = 10000;
    o.total_timeout_ms = 30000;
    o.basic_user       = u"user";
    o.basic_pass       = u"pass";

    knst_json payload;
    payload["name"] = "knst";
    payload["ver"]  = 1;

    knst_json result;
    knst_network_error err;

    if (c.post_json(u"https://api.example.com/items", payload, o, result, err)) {
        std::cout << result.dump_pretty() << "\n";
    }
#endif
```

### Streaming download

```cpp
static bool on_chunk(const uint8_t* data, size_t len, void* user) {
    auto* f = static_cast<FILE*>(user);
    std::fwrite(data, 1, len, f);
    return true;   // returning false aborts the request with CallbackAborted
}

FILE* f = std::fopen("/tmp/big.iso", "wb");

knst_http_client c;
knst_http_client::options o;
o.timeout_ms = 60000;
o.on_body      = on_chunk;
o.on_body_user = f;

knst_http_response r;
knst_network_error err;
bool ok = c.get(u"https://example.com/big.iso", o, r, err);

std::fclose(f);
```

### IP + port parse

```cpp
knst_network_error err;
knst_endpoint ep = knst_endpoint::parse(u"192.168.1.5:8080", err);
if (err == knst_network_error::None) {
    std::cout << ep.to_string() << "\n";   // "192.168.1.5:8080"
}
```

---

## 📋 General API

| Category | Class / Function |
|---|---|
| **Errors** | `knst_network_error`, `knst_network_error_string()` |
| **IP** | `knst_ip_address` |
| **Endpoint** | `knst_endpoint` |
| **DNS** | `knst_dns_resolver`, `knst_dns_cache`, `knst_doh_resolver` |
| **TCP** | `knst_tcp_socket`, `knst_tcp_server` |
| **UDP** | `knst_udp_socket` |
| **Unix** | `knst_unix_socket`, `knst_unix_server` |
| **TLS** | `knst_ssl_socket`, `knst_tls_set_ca_file/path`, `knst_tls_set_client_cert`, `knst_tls_set_skip_verify` |
| **URL** | `knst_url` |
| **HTTP** | `knst_http_request`, `knst_http_response`, `knst_http_client`, `knst_cookie` |
| **Pool** | `knst_connection_pool` |
| **Helpers** | `knst_net::public_ipv4()`, `knst_net::public_ipv6()` |

---

## 🎯 1) `knst_network_error` — Error Enum

```cpp
enum class knst_network_error : uint16_t {
    None = 0,
    Unknown, InvalidArgument, NotSupported, Interrupted,
    NotConnected, AlreadyConnected, AlreadyOpen, NotOpen,
    AddressInUse, AddressNotAvailable, PermissionDenied, WouldBlock, InProgress,
    ConnectionRefused, ConnectionReset, ConnectionAborted, ConnectionTimedOut,
    HostUnreachable, NetworkUnreachable, NetworkDown, BrokenPipe, NotSocket,
    MessageTooLarge, MessageTooSmall, PartialWrite, BufferEmpty,
    Timeout,
    DnsFailure, DnsNoData, DnsTemporaryFailure, DnsHostNotFound,
    TlsHandshakeFailed, TlsCertificateInvalid, TlsNotSupported,
    HttpMalformedStatus, HttpMalformedHeaders, HttpMalformedBody,
    HttpTooManyRedirects, HttpUnsupportedEncoding, HttpUnsupportedTransfer,
    HttpParseUrl, HttpConnectionClosed,
    OutOfMemory, TooManyOpenFiles, ConnectionClosed, ResponseTooLarge,
    HttpInvalidHeader, ProtocolError,
    ProxyFailed, CallbackAborted,
    ForbiddenAddress
};
```

```cpp
const char* knst_network_error_string(knst_network_error e) noexcept;
```

**`ForbiddenAddress`** — refused by the SSRF guard (`allow_private_networks = false` + private/loopback/link-local address).

---

## 🌐 2) `knst_ip_address` — IPv4 + IPv6

```cpp
class knst_ip_address {
public:
    // Factory
    static knst_ip_address from_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) noexcept;
    static knst_ip_address from_ipv6(const uint8_t raw[16]) noexcept;
    static knst_ip_address from_raw(const sockaddr* sa) noexcept;
    static knst_ip_address from_string(const knst_c16string& s, knst_network_error& err) noexcept;

    static knst_ip_address loopback_v4() noexcept;   // 127.0.0.1
    static knst_ip_address any_v4()      noexcept;   // 0.0.0.0
    static knst_ip_address loopback_v6() noexcept;   // ::1
    static knst_ip_address any_v6()      noexcept;   // ::

    // Queries
    bool is_valid()     const noexcept;
    bool is_ipv4()      const noexcept;
    bool is_ipv6()      const noexcept;
    bool is_loopback()  const noexcept;
    bool is_any()       const noexcept;
    bool is_multicast() const noexcept;
    bool is_non_public() const noexcept;   // for SSRF guard

    // Conversions
    knst_c16string       to_string() const;
    bool                 to_raw_ipv4(uint8_t out[4])  const noexcept;
    bool                 to_raw_ipv6(uint8_t out[16]) const noexcept;
    const sockaddr_storage& native() const noexcept;

    bool operator==(const knst_ip_address&) const noexcept;
    bool operator!=(const knst_ip_address&) const noexcept;
};
```

### What does `is_non_public()` do?

The SSRF guard uses this method. Ranges that return `true`:

**IPv4:**
- `0.0.0.0/8`, `10.0.0.0/8`, `127.0.0.0/8`
- `100.64.0.0/10` (CGNAT)
- `169.254.0.0/16` (link-local)
- `172.16.0.0/12`, `192.168.0.0/16`
- `192.0.0.0/24` (IETF), `192.0.2.0/24` (TEST-NET-1)
- `192.88.99.0/24`, `198.18.0.0/15`, `198.51.100.0/24` (TEST-NET-2), `203.0.113.0/24` (TEST-NET-3)
- `224.0.0.0/4` (multicast + reserved + broadcast)

**IPv6:**
- loopback, unspecified, multicast
- `fc00::/7` (ULA), `fe80::/10` (link-local), `fec0::/10` (site-local deprecated)
- `2001:db8::/32` (doc)
- `64:ff9b::/96` (NAT64) — embedded IPv4 checked
- `64:ff9b:1::/48`
- `2001::/32` (Teredo)
- `2002::/16` (6to4) — embedded IPv4 checked
- `::/96` (IPv4-compatible, deprecated)

**IPv4-mapped IPv6** (`::ffff:a.b.c.d`) → embedded IPv4 evaluated.

### IPv6 zone ID

`from_string(u"fe80::1%eth0")` works:
- **Linux:** resolved via `if_nametoindex()`
- **Windows:** only numeric zone (`%3`) accepted

---

## 🔌 3) `knst_endpoint` — Address + Port

```cpp
struct knst_endpoint {
    knst_ip_address address;
    uint16_t        port = 0;

    static knst_endpoint from_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t p) noexcept;
    static knst_endpoint from_native(const sockaddr* sa) noexcept;
    static knst_endpoint parse(const knst_c16string& s, knst_network_error& err) noexcept;

    sockaddr_storage to_native() const noexcept;
    socklen_t        native_length() const noexcept;
    knst_c16string   to_string() const;
    bool             is_valid() const noexcept;
};
```

### `parse()` formats

```cpp
knst_network_error err;
knst_endpoint::parse(u"192.168.1.5:8080", err);    // v4 + port
knst_endpoint::parse(u"[::1]:443", err);            // v6 + port
knst_endpoint::parse(u"1.2.3.4", err);              // v4, port=0
knst_endpoint::parse(u"::1", err);                  // v6, port=0
knst_endpoint::parse(u"[fe80::1%eth0]:80", err);    // v6 + zone + port
```

---

## 🔍 4) DNS — `knst_dns_resolver`

```cpp
class knst_dns_resolver {
public:
    static bool resolve(const knst_c16string& host, uint16_t port,
                        knst_vector<knst_endpoint>& out, knst_network_error& err,
                        int family = AF_UNSPEC) noexcept;   // sync (blocking)

    static bool resolve_async(const knst_c16string& host, uint16_t port,
                              knst_vector<knst_endpoint>& out, knst_network_error& err,
                              uint32_t timeout_ms, int family = AF_UNSPEC) noexcept;

    static bool resolve_first(const knst_c16string& host, uint16_t port,
                              knst_endpoint& out, knst_network_error& err,
                              int family = AF_UNSPEC) noexcept;

    static bool reverse(const knst_ip_address& addr, knst_c16string& out,
                        knst_network_error& err) noexcept;
};
```

### `resolve_async` behavior

- **IP literal** → no thread, returns endpoint directly (fast path)
- **`timeout_ms == 0`** → sync fallback
- **On timeout** → worker thread `detach()`ed (`getaddrinfo` not cancelable), `Timeout` returned. Worker completes in background.
- **Thread cap:** At most **32 workers** concurrently (`MAX_WORKERS`). Exceeded → `DnsTemporaryFailure` — thread exhaustion guard.
- **AI_ADDRCONFIG** → on offline/loopback-only environments, if `EAI_NONAME` is returned, **retry without AI_ADDRCONFIG**. On Android always disabled.

---

## 💾 5) `knst_dns_cache` — Thread-safe LRU + TTL

```cpp
class knst_dns_cache {
public:
    static knst_dns_cache& global() noexcept;

    bool lookup(const knst_c16string& host, uint16_t port,
                knst_vector<knst_endpoint>& out) noexcept;

    void store(const knst_c16string& host,
               const knst_vector<knst_endpoint>& eps,
               uint32_t ttl_ms) noexcept;

    void clear() noexcept;
    void set_max_entries(uint32_t n) noexcept;
};
```

- **Default max entries:** 128
- **LRU:** `lookup` hit promotes entry to head
- **TTL:** via `steady_clock`, `now >= expiry` → delete
- **Port:** Not stored in cache entry; parameter port applied on lookup
- **Key:** host lowercase ASCII (128-byte cap)

---

## 🔌 6) Socket Classes

### 6.1) `knst_socket_base` — Common lifecycle

```cpp
class knst_socket_base {
public:
    virtual ~knst_socket_base() noexcept;

    // Move-only
    knst_socket_base(knst_socket_base&&) noexcept;
    knst_socket_base& operator=(knst_socket_base&&) noexcept;

    bool is_open()      const noexcept;
    bool is_connected() const noexcept;
    knst_network_error last_error() const noexcept;
    knst_socket_handle_t native_handle() const noexcept;
    virtual void close() noexcept;

    void adopt(knst_socket_handle_t h) noexcept;   // take ownership

    knst_endpoint local_endpoint()  const noexcept;
    knst_endpoint remote_endpoint() const noexcept;

    // Timeout (0 = infinite)
    void set_read_timeout(uint32_t ms) noexcept;
    void set_write_timeout(uint32_t ms) noexcept;
    uint32_t read_timeout()  const noexcept;
    uint32_t write_timeout() const noexcept;

    // Socket options
    bool set_nodelay(bool enable) noexcept;   // TCP_NODELAY (skipped for AF_UNIX)
    bool set_keepalive(bool enable) noexcept;
    bool set_reuse_addr(bool enable) noexcept;

    // Shutdown
    void shutdown_read()  noexcept;
    void shutdown_write() noexcept;
    void shutdown_both()  noexcept;

    // Source IP binding (before connect)
    bool bind_local(const knst_endpoint& local, knst_network_error& err) noexcept;
    void clear_bind_local() noexcept;
    bool has_local_bind() const noexcept;
};
```

**What does `bind_local` do?** On multi-homed / VPN split-routing scenarios, it pins the **outgoing source IP**. Applied automatically inside `do_connect()` — works with `connect_any()` and `connect_async()` too. Family mismatch (v4 endpoint + v6 bind) silently skipped.

### 6.2) `knst_stream_socket_base` — Virtual I/O

```cpp
class knst_stream_socket_base : public knst_socket_base {
public:
    virtual bool has_pending_input() const noexcept;   // any bytes in TLS buffer?

    // Single write attempt (>= 1 byte on success)
    virtual bool send(const void* data, size_t len, size_t& sent,
                      uint32_t timeout_ms, knst_network_error& err) noexcept;

    // Write everything under a deadline
    virtual bool send_all(const void* data, size_t len,
                          uint32_t timeout_ms, knst_network_error& err) noexcept;
    bool send_all(const knst_byte_string& data,
                  uint32_t timeout_ms, knst_network_error& err) noexcept;

    // Single read: got == 0 => clean EOF
    virtual bool recv(void* buf, size_t cap, size_t& got,
                      uint32_t timeout_ms, knst_network_error& err) noexcept;
    bool recv(knst_byte_string& out, uint32_t timeout_ms,
              knst_network_error& err, size_t max_bytes = 4096) noexcept;

    // Exactly N bytes under a deadline
    virtual bool recv_exact(void* buf, size_t n,
                            uint32_t timeout_ms, knst_network_error& err) noexcept;
};
```

**Why `virtual`?** `knst_ssl_socket` provides encrypted I/O through the same interface after the TLS handshake. `knst_http_client` runs on top of this — plain and TLS share the same code path.

### 6.3) `knst_tcp_socket` — Client

```cpp
class knst_tcp_socket : public knst_stream_socket_base {
public:
    // Connect to a specific endpoint
    bool connect(const knst_endpoint& ep, uint32_t timeout_ms,
                 knst_network_error& err) noexcept;

    // Multiple endpoints, single deadline
    bool connect_any(const knst_vector<knst_endpoint>& list, uint32_t timeout_ms,
                     knst_network_error& err) noexcept;

    // Sync DNS + connect
    bool connect(const knst_c16string& host, uint16_t port, uint32_t timeout_ms,
                 knst_network_error& err) noexcept;

    // Async DNS + connect, ONE deadline
    bool connect_async(const knst_c16string& host, uint16_t port,
                       uint32_t dns_timeout_ms, uint32_t connect_timeout_ms,
                       knst_network_error& err) noexcept;
};
```

**`connect_any` strategy:** Under the total deadline, each address gets **half of the remaining budget**. If the first times out, moves on to the next. This is the foundation of IPv6/IPv4 dual-stack fallback.

**`connect_async`:** DNS + connect under **a single deadline**. `dns_budget()` allocates the DNS portion from the total budget.

### 6.4) `knst_tcp_server` — Server

```cpp
class knst_tcp_server {
public:
    bool listen(const knst_endpoint& local, int backlog, knst_network_error& err) noexcept;

    bool accept(knst_tcp_socket& out, uint32_t timeout_ms, knst_network_error& err) noexcept;
    bool accept(knst_tcp_socket& out, knst_endpoint& peer, uint32_t timeout_ms,
                knst_network_error& err) noexcept;

    knst_endpoint local_endpoint() const noexcept;
    void close() noexcept;
};
```

**Behavior:**
- **Windows:** `SO_EXCLUSIVEADDRUSE` + `WSA_FLAG_NO_HANDLE_INHERIT`
- **POSIX:** `SO_REUSEADDR` + `SOCK_CLOEXEC` + `SO_NOSIGPIPE` (macOS)
- **After `accept`:** `adopt()` → non-blocking + `TCP_NODELAY` + close-on-exec

### 6.5) `knst_udp_socket` — UDP + multicast

```cpp
class knst_udp_socket : public knst_socket_base {
public:
    bool bind(const knst_endpoint& local, knst_network_error& err) noexcept;

    bool send_to(const void* data, size_t len, const knst_endpoint& dest,
                 size_t& bytes_sent, uint32_t timeout_ms, knst_network_error& err) noexcept;

    bool recv_from(void* buf, size_t cap, size_t& bytes_read, knst_endpoint& from,
                   uint32_t timeout_ms, knst_network_error& err) noexcept;
    bool recv_from(knst_byte_string& out, knst_endpoint& from, uint32_t timeout_ms,
                   knst_network_error& err, size_t max_bytes = 65536) noexcept;

    bool set_broadcast(bool enable) noexcept;
    bool set_multicast_ttl(int ttl) noexcept;
    bool join_multicast(const knst_ip_address& group,
                        const knst_ip_address& iface = {}) noexcept;
    bool leave_multicast(const knst_ip_address& group,
                         const knst_ip_address& iface = {}) noexcept;
};
```

**Windows-specific:** `SIO_UDP_CONNRESET` disabled — even if ICMP port-unreachable arrives, `recv_from` continues silently (like Linux behavior).

### 6.6) `knst_unix_socket` + `knst_unix_server`

```cpp
class knst_unix_socket : public knst_stream_socket_base {
public:
    bool connect(const knst_c16string& path, uint32_t timeout_ms,
                 knst_network_error& err) noexcept;
};

class knst_unix_server {
public:
    bool listen(const knst_c16string& path, int backlog, knst_network_error& err) noexcept;
    bool accept(knst_unix_socket& out, uint32_t timeout_ms,
                knst_network_error& err) noexcept;
    void close() noexcept;
};
```

**Windows:** usable if `afunix.h` is available (`KNST_NETWORK_HAS_AF_UNIX_WIN`). **Linux/macOS:** full support.

**`listen` behavior (POSIX):**
1. `lstat` the file
2. If exists and not a socket → `AddressInUse`
3. If socket, `connect` probe → alive means `AddressInUse`, else (stale) `unlink`
4. `umask(0177)` for 0700 perms during listen
5. `bind` + `listen` → restore umask on any failure path
6. `close()` unlinks the socket file

---

## 🔐 7) TLS — `knst_ssl_socket` + global settings

> **Conditional compilation:** active if `KNST_NETWORK_ENABLE_TLS` is defined. Requires OpenSSL >= 1.1.0 (or LibreSSL 2.7+).

### Global settings (call BEFORE the first connection)

```cpp
// Custom CA file
bool knst_tls_set_ca_file(const char* path) noexcept;

// Custom CA directory
bool knst_tls_set_ca_path(const char* dir) noexcept;

// Client certificate (mTLS)
bool knst_tls_set_client_cert(const char* cert_pem_path, const char* key_pem_path) noexcept;

// Disable certificate verification (ONLY for trusted LAN devices — self-signed IPP)
bool knst_tls_set_skip_verify(bool skip) noexcept;
```

**Return value:** `false` if called after the first TLS handshake — setting not applied. **`knst_tls_set_skip_verify` is the exception:** if `ctx` already exists, it applies immediately via `SSL_CTX_set_verify` (handy during testing).

### `knst_ssl_socket` — TLS client

```cpp
class knst_ssl_socket : public knst_tcp_socket {
public:
    // DNS + TCP + handshake (single deadline)
    bool connect_tls(const knst_c16string& host, uint16_t port,
                     uint32_t dns_timeout_ms, uint32_t timeout_ms,
                     knst_network_error& err) noexcept;
    bool connect_tls(const knst_c16string& host, uint16_t port,
                     uint32_t timeout_ms, knst_network_error& err) noexcept;

    // Multiple endpoints; use the first that handshakes successfully
    bool connect_tls_any(const knst_vector<knst_endpoint>& eps,
                         const knst_c16string& sni_host,
                         uint32_t timeout_ms, knst_network_error& err) noexcept;

    // Handshake over an already-connected TCP (after proxy CONNECT)
    bool tls_handshake(const knst_c16string& sni_host, uint32_t timeout_ms,
                       knst_network_error& err, uint16_t port_hint = 0) noexcept;

    // Status
    bool is_tls_active() const noexcept;
    bool session_reused() const noexcept;
    bool has_pending_input() const noexcept override;
};
```

**Behavior:**
- **Handshake non-blocking:** runs under a single deadline via `poll()` loop
- **Blocking after handshake:** with per-syscall timeout via `SO_RCVTIMEO` + `SO_SNDTIMEO`
- **Verification:** `SSL_VERIFY_PEER` + hostname check (or `iPAddress` SAN for IP literal)
- **Min TLS:** 1.2
- **ALPN:** only `http/1.1` (H2/H3 in separate headers)
- **Compression:** `SSL_OP_NO_COMPRESSION` (CRIME)
- **Renegotiation:** `SSL_OP_NO_RENEGOTIATION` (1.1.1+)
- **Session resumption:** 64-entry LRU, key = `lower(host):port`
- **SIGPIPE:** POSIX thread-local protection via `pthread_sigmask`

### CA sources (in order)

1. If `ca_file` / `ca_path` set → only those
2. Otherwise:
   - **Windows:** import system cert store via `CertOpenSystemStoreA("ROOT")`
   - **Android:** `/apex/com.android.conscrypt/cacerts` + `/system/etc/security/cacerts`
   - **Linux:** `/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`, `/etc/ssl/cert.pem`

### mTLS usage

```cpp
// At the top of main(), BEFORE the first TLS connection
knst_tls_set_client_cert("/etc/certs/client.pem", "/etc/certs/client.key");

// Then use a normal HTTPS client
knst_http_client c;
knst_http_client::options o;
o.timeout_ms = 10000;

knst_http_response r;
knst_network_error err;
c.get(u"https://mtls.example.com/api", o, r, err);
```

### Self-signed printer (IPP)

```cpp
// Only trust this specific printer
knst_tls_set_skip_verify(true);

knst_http_client c;
knst_http_client::options o;
o.timeout_ms = 5000;
o.total_timeout_ms = 10000;
o.allow_private_networks = true;   // for LAN printer

knst_http_response r;
knst_network_error err;
c.get(u"https://192.168.1.50:631/ipp/print", o, r, err);
```

> ⚠️ `skip_verify` is **process-wide**. Once enabled, verification is off for **all** TLS connections. Remove before shipping to prod.

---

## 🌐 8) `knst_url` — RFC 3986 URL parser

```cpp
class knst_url {
public:
    knst_c16string scheme, userinfo, host, path, query, fragment;
    uint16_t       port = 0;

    bool is_valid() const noexcept;
    uint16_t effective_port() const noexcept;

    static bool parse(const knst_c16string& s, knst_url& out,
                      knst_network_error& err) noexcept;

    // RFC 3986 §5.2 redirect resolution
    static bool resolve(const knst_url& base, const knst_c16string& ref,
                        knst_url& out, knst_network_error& err) noexcept;

    knst_c16string to_string(bool include_fragment = true) const;
    knst_c16string request_target() const;       // path?query, percent-encoded
    knst_c16string host_bracketed() const;       // [v6] or host
    knst_c16string host_header_value() const;    // Host header value

    static knst_c16string remove_dot_segments(const knst_c16string& input);
};
```

### Parse example

```cpp
knst_url u;
knst_network_error err;
knst_url::parse(u"https://user:pass@example.com:8443/api/v1?x=1#frag", u, err);
// u.scheme   = "https"
// u.userinfo = "user:pass"
// u.host     = "example.com"
// u.port     = 8443
// u.path     = "/api/v1"
// u.query    = "x=1"
// u.fragment = "frag"
```

### Redirect resolution

```cpp
knst_url base, next;
knst_url::parse(u"https://example.com/dir/page.html", base, err);

knst_url::resolve(base, u"../other.html", next, err);
// next.path = "/other.html"

knst_url::resolve(base, u"?q=1", next, err);
// next.path = "/dir/page.html", next.query = "q=1"

knst_url::resolve(base, u"https://other.com/x", next, err);
// Absolute URL — completely new
```

**RFC 3986 §5.2.2 behavior:** single `?` ref → path unchanged, query empty. `""` ref → base copy.

---

## 📨 9) HTTP — Request / Response / Client

### 9.1) `knst_http_request`

```cpp
class knst_http_request {
public:
    knst_c16string method = u"GET";
    knst_c16string path   = u"/";
    knst_vector<knst_http_header> headers;
    knst_byte_string body;
    knst_vector<knst_http_header> trailers;   // chunked upload

    // Safe header (reserved rejected: Host, Content-Length, Transfer-Encoding,
    // Connection, TE, Trailer, Upgrade, Expect, Proxy-Connection, Proxy-Authorization,
    // Accept-Encoding)
    bool set_header(const knst_c16string& name, const knst_c16string& value);

    // Internal (library sets them itself)
    bool set_header_internal(const knst_c16string& name, const knst_c16string& value);

#if defined(KNST_NETWORK_ENABLE_JSON)
    bool set_json(const knst_json& j, uint32_t indent = 0) noexcept;
#endif

    bool has_header(const knst_c16string& name) const;
    knst_c16string get_header(const knst_c16string& name) const;

    knst_byte_string serialize_head() const;   // request line + headers + CRLF
    knst_byte_string serialize() const;        // head + body (chunked-aware)
};
```

### 9.2) `knst_http_response`

```cpp
class knst_http_response {
public:
    uint16_t status = 0;
    knst_c16string reason;
    knst_c16string version;
    knst_vector<knst_http_header> headers;
    knst_byte_string body;

    bool is_success()    const noexcept;   // 2xx
    bool is_redirect()   const noexcept;   // 301/302/303/307/308
    bool is_client_err() const noexcept;   // 4xx
    bool is_server_err() const noexcept;   // 5xx

    knst_c16string get_header(const knst_c16string& name) const;
    bool header_has_token(const knst_c16string& name, const knst_c16string& token) const;

#if defined(KNST_NETWORK_ENABLE_JSON)
    knst_json json() const noexcept;
#endif

    static bool parse(const knst_byte_string& raw, knst_http_response& out,
                      knst_network_error& err) noexcept;
};
```

### 9.3) `knst_cookie` — RFC 6265

```cpp
struct knst_cookie {
    knst_c16string name, value, domain, path;
    int64_t expires_unix = 0;   // 0 = session; >0 = unix seconds
    bool    secure = false;
    bool    host_only = true;   // true when no Domain attribute
};
```

### 9.4) `knst_http_client` — Main API

```cpp
class knst_http_client {
public:
    typedef bool (*body_callback_t)(const uint8_t* data, size_t len, void* user);

    struct options {
        // Timing
        uint32_t timeout_ms       = 10000;      // per-op
        uint32_t total_timeout_ms = 120000;     // total (incl. redirects)
        uint32_t dns_timeout_ms   = 0;          // 0 = timeout_ms

        // Redirect
        int      max_redirects    = 5;
        bool     follow_redirects = true;

        // Auth (sent to ORIGINAL origin only)
        knst_c16string basic_user;
        knst_c16string basic_pass;

        // Headers
        knst_c16string user_agent = u"KernelNucleusT/1.0";
        knst_c16string default_content_type = u"application/octet-stream";
        knst_vector<knst_http_header> headers;   // per-request extra

        // Size limits
        uint64_t max_response_bytes     = 64ULL * 1024 * 1024;   // wire
        uint64_t max_decompressed_bytes = 256ULL * 1024 * 1024;  // after decode
        uint32_t max_header_bytes       = 64 * 1024;

        // Content-Encoding
        bool     allow_content_encoding = true;   // Accept-Encoding + decode

        // Keep-alive
        bool     use_keepalive      = true;
        uint32_t keepalive_idle_ms  = 60000;
        uint32_t max_per_host       = 8;

        // DNS cache
        bool     dns_cache          = true;
        uint32_t dns_cache_ttl_ms   = 60000;

        // Cookies
        bool     auto_cookie        = true;

        // Proxy
        knst_c16string proxy_url;                 // "http://[user:pass@]host:port"
        knst_c16string no_proxy;                  // empty → env
        bool     use_env_proxy      = false;      // HTTP(S)_PROXY + NO_PROXY

        // SSRF guard
        bool     allow_private_networks = true;   // false → private/loopback refused

        // Source IP
        knst_c16string bind_local_ip;             // "192.168.1.5" or "fe80::1"

        // Streaming / chunked
        body_callback_t on_body      = nullptr;
        void*           on_body_user = nullptr;
        bool            use_chunked_upload = false;

        // Legacy
        bool     lenient_close_delimited = false;
    };

    // HTTP methods
    bool get    (const knst_c16string& url, const options& o,
                 knst_http_response& out, knst_network_error& err) noexcept;
    bool post   (const knst_c16string& url, const knst_byte_string& body,
                 const options& o, knst_http_response& out, knst_network_error& err) noexcept;
    bool put    (const knst_c16string& url, const knst_byte_string& body,
                 const options& o, knst_http_response& out, knst_network_error& err) noexcept;
    bool del    (const knst_c16string& url, const options& o,
                 knst_http_response& out, knst_network_error& err) noexcept;
    bool head   (const knst_c16string& url, const options& o,
                 knst_http_response& out, knst_network_error& err) noexcept;
    bool request(const knst_c16string& method, const knst_c16string& url,
                 const knst_byte_string& body, const options& o,
                 knst_http_response& out, knst_network_error& err) noexcept;

#if defined(KNST_NETWORK_ENABLE_JSON)
    bool get_json (const knst_c16string& url, const options& o,
                   knst_json& out, knst_network_error& err) noexcept;
    bool post_json(const knst_c16string& url, const knst_json& payload,
                   const options& o, knst_json& out, knst_network_error& err) noexcept;
#endif

    // Cookie / header management
    void clear_cookies();
    knst_vector<knst_cookie> cookies() const;
    bool set_header(const knst_c16string& name, const knst_c16string& value);
    void clear_headers();

    // Connection pool
    void     clear_connection_pool() noexcept;
    uint32_t idle_connections()      const noexcept;
    uint64_t connections_opened()    const noexcept;

    // Raw socket (for WebSocket upgrade etc.)
    knst_tcp_socket* open_raw_socket(const knst_url& u, const options& o,
                                     knst_network_error& err) noexcept;

    // Multipart
    struct multipart_part {
        knst_c16string name, filename, content_type;
        knst_byte_string data;
        knst_vector<knst_http_header> headers;   // populated by parser
    };

    static void build_multipart(const knst_vector<multipart_part>& parts,
                                knst_byte_string& out,
                                knst_c16string& boundary_out);

    static knst_c16string multipart_content_type(const knst_c16string& boundary);

    static knst_c16string extract_header_param(const knst_c16string& value,
                                                const knst_c16string& key);

    static bool parse_multipart(const knst_byte_string& body,
                                const knst_c16string& content_type_header,
                                knst_vector<multipart_part>& out,
                                knst_network_error& err) noexcept;
};
```

---

## ⚙️ 10) `options` field details

### What does `total_timeout_ms` do?

`timeout_ms` is the **per-syscall/per-read** budget. `total_timeout_ms` is the **absolute cap** for the whole request (DNS + TCP + TLS + all redirects + all body). **Slow-drip** guard: if a server sends 1 byte per 10s, `timeout_ms = 10000` still passes but `total_timeout_ms = 120000` cuts off after 2 minutes.

**0** → only per-op checks. **Warning:** setting to 0 disables slow-drip protection.

### `allow_content_encoding`

- `true` → sends `Accept-Encoding: gzip, deflate, identity` + decodes response
- `false` → sends `Accept-Encoding: identity`, no decode
- **In streaming mode** (`on_body` set): always `identity` — decoding the stream would break it

**Bomb guard:** `effective_inflate_limit(in_len, max_out)` → `min(max_out, max(1 MiB, in_len × 1000))`. So 1 KB gzip → at most 1 MB expanded.

### `use_chunked_upload`

`true` → `Transfer-Encoding: chunked` as a single chunk (`0\r\n\r\n` terminator). For streaming uploads of unknown length. `false` (default) → `Content-Length`.

### `on_body` streaming

If `on_body` is set and **not a redirect**, the body is not buffered in RAM; chunks are delivered to the callback:
- `response.body` stays empty
- Content-Length still capped by `max_response_bytes`
- Content-Encoding decode is **not performed** (`identity` sent)
- Callback returns `false` → `CallbackAborted`

### `lenient_close_delimited`

If no Content-Length / chunked, body is **delimited by connection close**. In that case a reset / TLS truncation normally becomes an **error** (`ConnectionClosed` / `ConnectionReset`). Setting `true` treats it as **success**. Legacy server compat — default `false`.

### `bind_local_ip`

Pins the outgoing source IP (VPN split routing). **With mixed IPv4/IPv6 endpoint lists:** families that mismatch are silently skipped; bind only applied on the matching family. Invalid IP → `InvalidArgument`.

### `dns_cache` + `dns_cache_ttl_ms`

`true` → uses `knst_dns_cache::global()`. `false` → `getaddrinfo` every time. Cache is a **global singleton**, shared process-wide. **TTL is separate** — cache entries stored with their own TTL.

### `dns_timeout_ms`

`0` → `timeout_ms` used. Otherwise, a separate DNS budget (consumed from total deadline).

### `use_env_proxy` + `proxy_url` + `no_proxy`

If `proxy_url` is empty and `use_env_proxy` is true:
- **Android:** JNI `System.getProperty("http.proxyHost")` + `http.proxyPort` → `host:port`
- **POSIX/Windows:**
  - `https://` → `https_proxy` env, else `HTTPS_PROXY`
  - `http://` → **only** `http_proxy` (httpoxy guard — `HTTP_PROXY` ignored)
- `no_proxy` empty → `no_proxy` / `NO_PROXY` env read

### `allow_private_networks` — SSRF guard

When `false`:
- DNS results with `is_non_public()` addresses are **filtered out**
- If no public address remains → `ForbiddenAddress`
- **With proxy:** only IP-literal targets checked (proxy resolves names itself)
- **Marked in pool key:** `strict=true` gets a different pool slot — a guarded connection cannot be reused by an unguarded one

**Why?** If you fetch user-supplied URLs (SSRF risk) → `allow_private_networks = false`. If you freely need internal network access → `true` (default).

---

## 🔀 11) Proxy strategy

### Supported protocols

| Scheme | Behavior |
|---|---|
| `http://host:port` | **HTTP proxy:** absolute-URI for plain HTTP, CONNECT tunnel for HTTPS |
| `socks5://host:port` | **SOCKS5:** target hostname **resolved locally**, IP passed to proxy |
| `socks5h://host:port` | **SOCKS5h:** target hostname **resolved by proxy** (no DNS leak) |

### httpoxy guard

For `http://` targets, **`HTTP_PROXY` env is not read**, only `http_proxy` (lowercase). Reason: in a CGI environment, a `Proxy:` request header can be reflected as the `HTTP_PROXY` env var → SSRF. httpoxy vulnerability (CVE-2016-5385).

For `https://` both `HTTPS_PROXY` / `https_proxy` are read (HTTPS doesn't reflect CGI `Proxy:`).

### CONNECT tunnel

```
Client → Proxy: CONNECT target.com:443 HTTP/1.1
                Host: target.com:443
                Proxy-Authorization: Basic ...

Proxy  → Client: HTTP/1.1 200 Connection established

Client → Proxy: <TLS handshake>
Client → Proxy: <HTTP request>
```

`open_raw_socket(force_tunnel=true)` — for WebSocket even `ws://` goes through the tunnel (the proxy cannot route a relative-URI request).

### `NO_PROXY` matching

- Comma-separated list
- `*` → bypass everything
- `.example.com` → `example.com` and subdomains
- `example.com:8080` → that port only
- `host.equals(token)` or `.suffix` match

---

## 🔄 12) Redirect behavior

| Status | Method | Body |
|---|---|---|
| **301 / 302** | `POST` → `GET` | Cleared |
| **303** | anything except `HEAD` → `GET` | Cleared |
| **307 / 308** | Method preserved | Body preserved |
| **300 / 304** | Not followed | — |

### Security

- **Cross-origin** (`scheme / host / port` differ): `Authorization`, `Proxy-Authorization`, `Cookie`, `X-Api-Key`, `X-Auth-Token` **headers are dropped**. `options::basic_user/pass` also not sent.
- **Redirect body cap:** 64 KB (in `will_redirect` state, instead of `max_response_bytes`)
- **Redirect limit:** `max_redirects` (default 5)

---

## 🍪 13) Cookie behavior (RFC 6265)

### Parse

`Set-Cookie: name=value; Domain=.example.com; Path=/api; Secure; Expires=...; Max-Age=3600`

| Attribute | Behavior |
|---|---|
| **Domain** | `.` prefix stripped, subdomain match. For IP-literal origins, only exact host matches |
| **Path** | If absent, RFC 6265 §5.1.4 default-path (up to last `/`) |
| **Secure** | If not HTTPS → Set-Cookie rejected |
| **Expires** | HTTP-date parse (IMF-fixdate format) |
| **Max-Age** | Takes precedence, overrides `Expires` |

### Sending

`Cookie: n1=v1; n2=v2` — in order:
- Expiry check (`expires_unix > 0 && < now` → skip)
- Host match (`host_only` → exact, else `domain_match`)
- Path match (`path_match(req_path, cookie_path)`)
- `Secure` → requires `https://`

### Limits

- **Name + Value** ≤ 4096
- **Total cookies** ≤ 3000
- **Value:** Control chars / DEL / > 0x7F rejected (Cookie header injection guard)

### IP-literal origins

`Domain=.example.com` is ignored — **IP addresses cannot be assigned a domain**. Only `host_only` cookies accepted.

---

## 🗜️ 14) gzip / deflate (internal)

`Content-Encoding` chain supports `gzip`, `x-gzip`, `deflate`, `identity`. **At most 4 layers** (e.g. `gzip, gzip`). Decode order is **reverse** (applied order → decode reverse).

### Bomb guard

`effective_inflate_limit(in_len, max_out) = min(max_out, max(1 MiB, in_len × 1000))`

- **1 MB gzip** → at most **1 GB** expanded
- **10 KB gzip** → at most **10 MB** expanded
- **Empty gzip** → at most **1 MB** expanded

Exceeded → `ResponseTooLarge`.

### Multi-member gzip

RFC 1952 supported — consecutive gzip members decoded. Trailing garbage → `HttpMalformedBody`.

### Raw deflate fallback

`deflate` is expected to be **RFC 1950 (with zlib header)**. But some servers send raw deflate → if header + Adler32 verification fails, **retry as raw deflate**.

---

## 🏊 15) `knst_connection_pool` — Keep-alive

```cpp
class knst_connection_pool {
public:
    // Take an idle healthy socket (ownership goes to caller)
    knst_tcp_socket* take(const knst_c16string& key, uint32_t idle_ms) noexcept;

    // Ownership transferred (true on success)
    bool put(const knst_c16string& key, knst_tcp_socket* s, uint32_t max_per_host) noexcept;

    void clear() noexcept;
    uint32_t idle_count() const noexcept;
};
```

- **Global cap:** 64 connections
- **Per-host limit:** `options::max_per_host` (default 8)
- **Healthy check:**
  - `socket_looks_alive()` → `poll(POLLIN, 0)` must be idle (no EOF/error/data)
  - **`!has_pending_input()`** → no bytes in TLS buffer (desync guard)
- **Stale socket retry:** first exchange fails + `!got_byte` + idempotent method → retry

### Pool key format

```
"strict|bind|scheme://host:port|proxy_host:port"
```

`strict` = SSRF guard state (guarded/unguarded pools don't mix).
`bind` = source IP.

---

## 📥 16) Streaming body (`on_body`)

```cpp
static bool on_chunk(const uint8_t* data, size_t len, void* user) {
    auto* f = static_cast<FILE*>(user);
    return std::fwrite(data, 1, len, f) == len;
}

knst_http_client c;
knst_http_client::options o;
o.timeout_ms      = 60000;
o.total_timeout_ms = 3600000;   // 1-hour absolute cap
o.on_body         = on_chunk;
o.on_body_user    = f;

knst_http_response r;
knst_network_error err;
c.get(u"https://example.com/large-file.bin", o, r, err);
```

### Rules

- `on_body` set → **streaming mode**, `response.body` stays **empty**
- **On redirect** streaming is disabled (Location header must be readable)
- **Content-Encoding** decode not performed (always `identity`)
- Callback returns `false` → `CallbackAborted`
- Chunk sizes: internal **32 KB** staging buffer (on heap, not stack)

---

## 🧩 17) Multipart/form-data

### Builder

```cpp
knst_vector<knst_http_client::multipart_part> parts;

knst_http_client::multipart_part p1;
p1.name = u"title";
p1.data = /* bytes */;
parts.push_back(p1);

knst_http_client::multipart_part p2;
p2.name         = u"file";
p2.filename     = u"report.pdf";
p2.content_type = u"application/pdf";
p2.data         = /* PDF bytes */;
parts.push_back(p2);

knst_byte_string body;
knst_c16string   boundary;
knst_http_client::build_multipart(parts, body, boundary);

knst_http_client c;
knst_http_client::options o;
o.timeout_ms = 30000;
o.default_content_type = knst_http_client::multipart_content_type(boundary);

knst_http_response r;
knst_network_error err;
c.post(u"https://upload.example.com/api", body, o, r, err);
```

### Parser

```cpp
knst_c16string ct = r.get_header(u"Content-Type");   // "multipart/form-data; boundary=xyz"

knst_vector<knst_http_client::multipart_part> parts;
knst_network_error err;
if (knst_http_client::parse_multipart(r.body, ct, parts, err)) {
    for (auto& p : parts) {
        std::cout << "name=" << p.name
                  << " file=" << p.filename
                  << " size=" << p.data.length() << "\n";
    }
}
```

### Boundary generation

`----knst<hex>%08x%08x` format — counter XOR steady_clock nanos. Thread-safe (`std::atomic`).

### Header param extraction

`extract_header_param(value, "boundary")` — parses `;`-separated parameters, supports quoted/unquoted values. `content-type: multipart/form-data; boundary=----xyz; charset=utf-8` → `boundary` = `----xyz`.

---

## 🚀 18) DoH — `knst_doh_resolver`

```cpp
class knst_doh_resolver {
public:
    static bool resolve(const knst_c16string& doh_endpoint,   // "https://cloudflare-dns.com/dns-query"
                        const knst_c16string& host,
                        uint16_t port,
                        knst_vector<knst_endpoint>& out,
                        knst_network_error& err,
                        uint32_t timeout_ms = 5000,
                        int family = AF_UNSPEC) noexcept;
};
```

**RFC 8484** — `POST` + `Content-Type: application/dns-message` + binary DNS query body. Response `application/dns-message`.

**Behavior:**
- IP literal → no thread, direct
- `AF_UNSPEC` → tries `A` then `AAAA`
- `family = AF_INET` for A-only, `AF_INET6` for AAAA-only

**Example:**

```cpp
knst_vector<knst_endpoint> endpoints;
knst_network_error err;

if (knst_doh_resolver::resolve(
        u"https://cloudflare-dns.com/dns-query",
        u"example.com", 443, endpoints, err)) {
    for (auto& ep : endpoints) {
        std::cout << ep.to_string() << "\n";
    }
}
```

**Requires TLS:** if `KNST_NETWORK_ENABLE_TLS` is not defined → `TlsNotSupported`.

---

## 🌍 19) Public IP discovery

```cpp
namespace knst_net {
    knst_c16string public_ipv4(knst_network_error& err, uint32_t timeout_ms = 5000) noexcept;
    knst_c16string public_ipv6(knst_network_error& err, uint32_t timeout_ms = 5000) noexcept;
}
```

- `public_ipv4()` → `https://api.ipify.org` (plain-text IPv4)
- `public_ipv6()` → `https://api64.ipify.org` (expects IPv6, empty if no `:`)
- **Without TLS** → `TlsNotSupported` + empty string

---

## 🎯 20) Platform differences

| Concern | Windows | Linux | Android |
|---|---|---|---|
| **Socket handle** | `SOCKET` (unsigned intptr) | `int` | `int` |
| **Winsock init** | `WSAStartup` (once_flag) | — | — |
| **AF_UNIX** | Win10 1803+ | ✅ | ✅ |
| **TLS cert store** | `CertOpenSystemStoreA("ROOT")` | CA files | `/apex` + `/system/etc` |
| **`SOCK_CLOEXEC`** | `WSA_FLAG_NO_HANDLE_INHERIT` | direct | direct |
| **SIGPIPE** | ❌ absent | `pthread_sigmask` | `pthread_sigmask` |
| **SO_NOSIGPIPE** | ❌ | macOS | ❌ |
| **`SO_EXCLUSIVEADDRUSE`** | server listen | ❌ | ❌ |
| **`SO_REUSEADDR`** | ❌ | ✅ | ✅ |
| **UDP `SIO_UDP_CONNRESET`** | disabled | ❌ | ❌ |
| **System proxy** | env | env | JNI `System.getProperty` |
| **IPv6 zone name** | numeric only | `if_nametoindex` | `if_nametoindex` |
| **DNS `AI_ADDRCONFIG`** | used | used | **always disabled** |
| **`afunix.h`** | optional | — | — |

---

## ⚠️ 21) Common gotchas

### 1) `HTTPS_PROXY` vs `http_proxy` — httpoxy

For plain `http://` requests, **`HTTP_PROXY` env is not read**. Only `http_proxy`. This is deliberate. In CGI containers don't set `HTTP_PROXY` — it's ignored.

### 2) Don't set `total_timeout_ms = 0`

You'll be exposed to slow-drip attacks. If a server sends 1 byte per 10s, `timeout_ms` passes but the total becomes unbounded. **Keep the default 120s.**

### 3) `allow_private_networks = false` — DNS rebinding guard

Checks the **addresses** in the DNS result. If the host is **re-resolved** (redirect), new addresses are checked again. **But with a proxy,** the SSRF guard only checks IP-literal targets — the proxy can resolve private IPs itself. **Trust your proxy.**

### 4) `on_body` doesn't run on redirects

Streaming mode is **disabled** for redirect responses. Redirect bodies are read normally (64 KB cap) so Location is readable.

### 5) `Secure` cookie over HTTP → rejected

RFC 6265bis §5.7. `Set-Cookie: x=y; Secure` over HTTP → **not added to the jar**.

### 6) `Host` header cannot be overridden

`set_header("Host", ...)` → `false`. `Host` is auto-derived from the URL. Same for `Content-Length`, `Transfer-Encoding`, `Connection`, `Proxy-Authorization`.

### 7) `basic_user/pass` not sent on cross-origin redirect

Only sent to the **original origin** (`scheme + host + port` same as first URL). On redirect to a different host, Authorization is dropped. Intentional security.

### 8) TLS session cache — `host:port` key

Session reuse is keyed on `lower(host):port`. Different ports on the same host are different sessions. Correct behavior.

### 9) IPv6 zone ID on Windows is numeric only

`fe80::1%eth0` on Windows → `InvalidArgument`. `fe80::1%3` works. On Linux `if_nametoindex("eth0")` works.

### 10) `get_json` returns null for empty bodies

`r.body.empty()` → `json() = null`, no error. **Empty body is not an error** — normal for 204 No Content. Check `r.status`.

### 11) `parse()` rejects conflicting Content-Length

`Content-Length: 5` + `Content-Length: 10` → `HttpMalformedHeaders`. `Content-Length: 5` + `Transfer-Encoding: chunked` → **also rejected** (HTTP request smuggling guard).

### 12) `chunked` + `use_chunked_upload`

Don't set `Transfer-Encoding: chunked` yourself — use `use_chunked_upload = true`. The library serializes correctly (single chunk). `set_header("Transfer-Encoding", ...)` → `false`.

### 13) DNS worker thread detach

On async DNS timeout, the worker is **detached**. `getaddrinfo` isn't cancelable — it blocks at OS level. That's why the 32-worker cap exists.

### 14) TLS handshake non-blocking under deadline

Handshake runs under a **poll() loop**. After handshake **completes**, the socket becomes **blocking** + `SO_RCVTIMEO/SO_SNDTIMEO`. So read/write timeout is per-syscall; handshake deadline is total.

### 15) `SO_RCVTIMEO` 0 = infinite

`set_read_timeout(0)` → `SO_RCVTIMEO = 0` → **infinite blocking**. If `recv` blocks with no read timeout, it waits forever. Give an explicit value if you don't want `0`.

### 16) `open_raw_socket()` force_tunnel

For WebSocket even `ws://` (plain) goes through a **CONNECT tunnel**. Reason: an HTTP proxy cannot route relative-URI. `open_raw_socket()` calls with `force_tunnel=true` for that reason.

### 17) `bind_local` family mismatch

Endpoint `192.168.1.5` (v4), `bind_local_ip = "fe80::1"` (v6) → **silently skipped**. Bind not applied, normal routing. Deliberate — mixed endpoint lists shouldn't break.

### 18) `extract_header_param` with quoted values

`boundary="--xyz"` → value = `--xyz` (quotes stripped). Values with spaces supported.

### 19) Multipart boundary collision

Boundary is **random + counter**. Collision with payload is **improbable** but not impossible. If it happens with large binary uploads in prod, multipart parse fails. You cannot supply a custom boundary — changing the generator would be required.

### 20) Cookie value > 0x7F rejected

`Set-Cookie: name=ünïcödé` → **not added to jar**. RFC 6265 forbids non-ASCII in cookie values. The library silently rejects.

---

## 📊 22) Performance

| Operation | Cost | Note |
|---|---|---|
| `knst_url::parse` | O(n) | Once per redirect |
| `knst_dns_resolver::resolve` | 5-100 ms | DNS cache hit ≈ 0 |
| `knst_dns_cache::lookup` | O(128) | LRU linear scan |
| `getaddrinfo` sync | blocks | Async recommended |
| TCP connect | 1-100 ms | RTT-dependent |
| TLS handshake | 20-200 ms | Session reuse 5-30 ms |
| `SSL_write` | O(n) | Kernel buffer |
| gzip decode | O(n) | Slicing-by-8 CRC32 |
| HTTP parse | O(headers + body) | Streaming for chunked |
| Cookie match | O(cookies × headers) | Jar usually < 100 |
| Pool take/put | O(pool size) | Max 64 |

**Typical latencies:**
- **Simple GET** (DNS + TCP + TLS + 1 RTT): 50-150 ms on LAN
- **Keep-alive reuse**: 5-20 ms
- **TLS session resumption**: handshake 5-30 ms (vs 20-200 ms full)
- **DNS cache hit**: < 1 ms

**Optimizations:**
- **Keep `total_timeout_ms` set** — slow-drip guard
- **Keep DNS cache on** (`dns_cache = true`)
- **Keep keep-alive on** (`use_keepalive = true`)
- **Use `on_body` for streaming downloads** — RAM savings
- **Lower `max_decompressed_bytes`** — more aggressive bomb guard

---

## ✅ 23) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Combine total + per-op timeout
options o;
o.timeout_ms       = 10000;
o.total_timeout_ms = 30000;

// 2. User-supplied URL → SSRF guard
options o;
o.allow_private_networks = false;

// 3. Streaming download
o.on_body      = my_callback;
o.on_body_user = my_user;

// 4. TLS mTLS
knst_tls_set_client_cert("client.pem", "client.key");
// (at the top of main, BEFORE the first connection)

// 5. Inspect cookies
auto jar = c.cookies();
for (auto& ck : jar) { /* ... */ }

// 6. Multipart upload
knst_byte_string body;
knst_c16string   boundary;
knst_http_client::build_multipart(parts, body, boundary);

// 7. Bind for VPN split routing
options o;
o.bind_local_ip = u"10.8.0.2";   // VPN adapter IP
```

### ❌ Don't

```cpp
// 1. Don't set total_timeout_ms to 0
options o;
o.total_timeout_ms = 0;   // ❌ slow-drip vulnerability

// 2. on_body + Content-Encoding
options o;
o.on_body = my_callback;   // ✅ decode automatically off, no problem
// but don't try to zlib-decode yourself — stream arrives as identity

// 3. Don't override Host header
req.set_header(u"Host", u"other.com");   // ❌ returns false

// 4. Don't use user URLs blindly
c.get(user_input_url, o, r, err);   // ❌ SSRF risk
// ✅ o.allow_private_networks = false;

// 5. Don't leave skip_verify on in prod
knst_tls_set_skip_verify(true);   // ❌ remove before prod

// 6. Don't expect HTTP/2
// This header is HTTP/1.1 — ALPN won't ask for h2

// 7. Don't assume proxy resolves names
options o;
o.proxy_url = u"socks5://proxy:1080";   // socks5 → LOCALLY resolved
// use socks5h:// to have the proxy resolve

// 8. Don't try to give a custom boundary to multipart
// Library generates it; you can't override
```

---

## 🔥 24) Real-world examples

### Example 1: JSON REST client

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
class ApiClient {
    knst_http_client c;
    knst_http_client::options o;

public:
    ApiClient(const knst_c16string& base_url, const knst_c16string& token)
        : base(base_url)
    {
        o.timeout_ms       = 10000;
        o.total_timeout_ms = 30000;
        o.basic_user       = token;
        o.follow_redirects = true;
    }

    bool get_items(knst_json& out) {
        return c.get_json(base + u"/items", o, out, last_err);
    }

    knst_network_error last_err = knst_network_error::None;
    knst_c16string base;
};
#endif
```

### Example 2: Large file download + progress

```cpp
struct progress_t {
    uint64_t total_received = 0;
    uint64_t start_time     = knst_steady_ms();
};

static bool on_chunk(const uint8_t* data, size_t len, void* user) {
    auto* p = static_cast<progress_t*>(user);
    p->total_received += len;
    // print progress every 1 MB
    if (p->total_received % (1024 * 1024) < len) {
        std::cout << p->total_received / (1024*1024) << " MB\n";
    }
    return true;
}

knst_http_client c;
knst_http_client::options o;
o.timeout_ms       = 60000;
o.total_timeout_ms = 3600000;
o.on_body          = on_chunk;

progress_t p;
o.on_body_user = &p;

knst_http_response r;
knst_network_error err;
c.get(u"https://example.com/large-file.iso", o, r, err);
```

### Example 3: SOCKS5 proxy + timeout

```cpp
knst_http_client c;
knst_http_client::options o;
o.timeout_ms       = 15000;
o.total_timeout_ms = 60000;
o.proxy_url        = u"socks5h://127.0.0.1:1080";   // DNS at proxy

knst_http_response r;
knst_network_error err;

if (c.get(u"https://check.torproject.org/api/ip", o, r, err)) {
    std::cout << r.body.length() << " bytes\n";
}
```

### Example 4: SSRF guard (fetching user URLs from a web app)

```cpp
bool safe_fetch(const knst_c16string& user_url, knst_byte_string& out) {
    knst_url parsed;
    knst_network_error err;

    if (!knst_url::parse(user_url, parsed, err)) return false;
    if (parsed.scheme != u"http" && parsed.scheme != u"https") return false;

    knst_http_client c;
    knst_http_client::options o;
    o.timeout_ms       = 5000;
    o.total_timeout_ms = 10000;
    o.max_response_bytes     = 1024 * 1024;    // 1 MB cap
    o.max_decompressed_bytes = 4 * 1024 * 1024;
    o.allow_private_networks = false;          // SSRF guard ON
    o.follow_redirects = true;
    o.max_redirects    = 3;

    knst_http_response r;
    if (!c.get(user_url, o, r, err)) return false;
    if (!r.is_success()) return false;

    out = std::move(r.body);
    return true;
}
```

### Example 5: Multipart upload

```cpp
knst_http_client::multipart_part title_part;
title_part.name = u"title";
title_part.data = /* knst_byte_string("Hello") */;

knst_http_client::multipart_part file_part;
file_part.name         = u"file";
file_part.filename     = u"data.bin";
file_part.content_type = u"application/octet-stream";
file_part.data         = /* raw bytes */;

knst_vector<knst_http_client::multipart_part> parts;
parts.push_back(title_part);
parts.push_back(file_part);

knst_byte_string body;
knst_c16string   boundary;
knst_http_client::build_multipart(parts, body, boundary);

knst_http_client c;
knst_http_client::options o;
o.timeout_ms       = 60000;
o.total_timeout_ms = 300000;
o.default_content_type = knst_http_client::multipart_content_type(boundary);
o.use_chunked_upload = true;   // unknown size → chunked

knst_http_response r;
knst_network_error err;
c.post(u"https://upload.example.com/api", body, o, r, err);
```

### Example 6: SSRF-safe + DoH resolve + HTTP

```cpp
// 1. Resolve with DNS over HTTPS
knst_vector<knst_endpoint> eps;
knst_network_error err;

if (!knst_doh_resolver::resolve(
        u"https://cloudflare-dns.com/dns-query",
        u"example.com", 443, eps, err)) {
    return;
}

// 2. Connect directly to IP (with SNI host)
knst_ssl_socket ssl;
if (!ssl.connect_tls_any(eps, u"example.com", 10000, err)) {
    return;
}

// 3. HTTP request (manual Host header)
knst_http_request req;
req.method = u"GET";
req.path   = u"/";
req.set_header_internal(u"Host", u"example.com");

knst_byte_string bytes = req.serialize();
if (!ssl.send_all(bytes, 10000, err)) return;

knst_byte_string out;
if (!ssl.recv(out, 10000, err, 8192)) return;
```

### Example 7: Manual UDP DNS

```cpp
knst_udp_socket udp;
knst_network_error err;

knst_endpoint dns_server = knst_endpoint::parse(u"8.8.8.8:53", err);
if (err != knst_network_error::None) return;

// Build DNS query (special library: knst_net_detail::dns_build_query, internal)
// (this is just an example — not public API)
```

### Example 8: WebSocket raw upgrade

```cpp
knst_url ws_url;
knst_network_error err;
knst_url::parse(u"wss://echo.websocket.org/", ws_url, err);

knst_http_client c;
knst_http_client::options o;
o.timeout_ms = 10000;
o.total_timeout_ms = 30000;

knst_tcp_socket* sock = c.open_raw_socket(ws_url, o, err);
if (!sock) return;

// now send the WebSocket upgrade request (RFC 6455)
// ... Sec-WebSocket-Key, Upgrade: websocket, etc.
// check for 101 response
// then framing — knst_websocket.hpp does this

delete sock;
```

---

## 📌 25) API Contract Summary

```cpp
namespace knst_net {

enum class knst_network_error : uint16_t { /* 50+ values */ };
const char* knst_network_error_string(knst_network_error e) noexcept;

class knst_ip_address {
    static knst_ip_address from_ipv4(uint8_t, uint8_t, uint8_t, uint8_t) noexcept;
    static knst_ip_address from_ipv6(const uint8_t[16]) noexcept;
    static knst_ip_address from_raw(const sockaddr*) noexcept;
    static knst_ip_address from_string(const knst_c16string&, knst_network_error&) noexcept;
    static knst_ip_address loopback_v4() noexcept;
    static knst_ip_address any_v4()      noexcept;
    static knst_ip_address loopback_v6() noexcept;
    static knst_ip_address any_v6()      noexcept;
    bool is_valid() / is_ipv4() / is_ipv6() / is_loopback() / is_any() / is_multicast() const noexcept;
    bool is_non_public() const noexcept;
    knst_c16string to_string() const;
};

struct knst_endpoint {
    knst_ip_address address;
    uint16_t        port = 0;
    static knst_endpoint parse(const knst_c16string&, knst_network_error&) noexcept;
    sockaddr_storage to_native() const noexcept;
    knst_c16string   to_string() const;
    bool             is_valid() const noexcept;
};

class knst_dns_resolver {
    static bool resolve(...);
    static bool resolve_async(..., uint32_t timeout_ms, ...);
    static bool resolve_first(...);
    static bool reverse(...);
};

class knst_dns_cache {
    static knst_dns_cache& global() noexcept;
    bool lookup(...);
    void store(...);
    void clear() noexcept;
    void set_max_entries(uint32_t) noexcept;
};

class knst_socket_base { /* open, close, timeouts, bind_local, shutdown_* */ };
class knst_stream_socket_base : public knst_socket_base { /* virtual send/recv */ };
class knst_tcp_socket  : public knst_stream_socket_base { /* connect / connect_any / connect_async */ };
class knst_tcp_server  { /* listen / accept */ };
class knst_udp_socket  : public knst_socket_base { /* send_to / recv_from / multicast */ };
class knst_unix_socket : public knst_stream_socket_base { /* connect */ };
class knst_unix_server { /* listen / accept */ };

#if KNST_NETWORK_ENABLE_TLS
class knst_ssl_socket : public knst_tcp_socket { /* connect_tls / connect_tls_any / tls_handshake */ };
bool knst_tls_set_ca_file(const char*);
bool knst_tls_set_ca_path(const char*);
bool knst_tls_set_client_cert(const char*, const char*);
bool knst_tls_set_skip_verify(bool);
#endif

class knst_url {
    knst_c16string scheme, userinfo, host, path, query, fragment;
    uint16_t       port = 0;
    static bool parse(...);
    static bool resolve(...);
    static knst_c16string remove_dot_segments(...);
    knst_c16string to_string(bool = true) const;
    knst_c16string request_target() const;
    knst_c16string host_header_value() const;
};

struct knst_http_header { knst_c16string name, value; };
class  knst_http_request { /* method, path, headers, body, set_header, serialize */ };
class  knst_http_response { /* status, headers, body, get_header, header_has_token, parse */ };
struct knst_cookie { knst_c16string name, value, domain, path; int64_t expires_unix; bool secure, host_only; };

class knst_connection_pool { /* take / put / clear / idle_count */ };

class knst_http_client {
    struct options { /* 30+ fields */ };
    bool get/post/put/del/head/request(...) noexcept;
    bool get_json/post_json(...) noexcept;   // KNST_NETWORK_ENABLE_JSON
    void clear_cookies();
    knst_vector<knst_cookie> cookies() const;
    bool set_header(...); void clear_headers();
    void clear_connection_pool(); uint32_t idle_connections() const; uint64_t connections_opened() const;
    knst_tcp_socket* open_raw_socket(...) noexcept;

    struct multipart_part { /* name, filename, content_type, data, headers */ };
    static void build_multipart(...);
    static knst_c16string multipart_content_type(...);
    static knst_c16string extract_header_param(...);
    static bool parse_multipart(...) noexcept;
};

class knst_doh_resolver {
    static bool resolve(...) noexcept;
};

// Helpers
knst_c16string public_ipv4(knst_network_error&, uint32_t timeout_ms = 5000) noexcept;
knst_c16string public_ipv6(knst_network_error&, uint32_t timeout_ms = 5000) noexcept;

} // namespace knst_net
```

---

## 🎯 What It Brings You

`knst_network` gives you serious advantages in these situations:

- ✅ **Header-only HTTP client** — no curl/libcurl dependency
- ✅ **Cross-platform** — Windows + Linux + Android, same API
- ✅ **TLS** — OpenSSL, mTLS, session resumption, cert store import
- ✅ **DoH** — DNS over HTTPS, RFC 8484
- ✅ **SSRF guard** — for services that fetch user URLs
- ✅ **Streaming** — large downloads don't fit in RAM
- ✅ **Chunked upload** — POST with unknown size
- ✅ **Cookie jar** — RFC 6265 compliant
- ✅ **Multipart** — builder + parser
- ✅ **Proxy** — HTTP + SOCKS5 + SOCKS5h
- ✅ **Keep-alive pool** — latency savings
- ✅ **gzip/deflate** — bomb-guarded
- ✅ **Async DNS** — thread cap, deadline-bounded
- ✅ **Integrated with `knst_process`, `knst_file`, `knst_prn`** (IPP, eSCL, DoH)
- ✅ **KernelNucleusT native** — `knst_c16string`, `knst_byte_string`, `knst_vector`, `knst_json` (optional)

---