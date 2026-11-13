# knst_network — Cross-Platform Network Library

Selamlar! 👋 `knst_network.hpp`, KernelNucleusT'nin **network modülü**. Tek header'da: hata modeli, IP/endpoint, DNS (sync/async/cache/DoH), TCP/UDP/Unix socket'ler, TLS (OpenSSL), URL parser (RFC 3986), ve full **HTTP/1.1 client** (redirect + cookie + gzip + proxy + keep-alive + SSRF guard).

**Kısaca:** `curl`'a bağımlılık yok, header-only. `-lssl -lcrypto` linklediysen HTTPS çalışır. **HTTP/2 ve HTTP/3 bu header'da değil** — onlar CMake'deki `H2`/`H3` tier'larında ayrı header'larla (`knst_network_h2.hpp`, `knst_network_h3.hpp`) devreye girer.

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

| Özellik | Windows | Linux | Android |
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
| **HTTP/2** | ❌ bu header'da yok | ❌ | ❌ |
| **HTTP/3 / QUIC** | ❌ bu header'da yok | ❌ | ❌ |
| **WebSocket** | ⚠️ sadece `open_raw_socket()` | ⚠️ | ⚠️ |
| **ICMP / ping** | ❌ | ❌ | ❌ |
| **FTP / SMTP / raw prot.** | ❌ | ❌ | ❌ |

**Kritik notlar:**
- **HTTP/2 ve HTTP/3 bu header'da yok.** ALPN sadece `http/1.1` bildiriyor. CMake'deki `H2`/`H3` tier'ları ayrı header'ları aktif eder; bu header onları kullanmaz.
- **WebSocket** için ayrı `knst_websocket.hpp` var. Bu header sadece `open_raw_socket()` verir — upgrade handshake sonrası byte stream sizin.
- **TLS olmadan derlersen** (`KNST_NETWORK_ENABLE_TLS` yok) `https://` → `TlsNotSupported`. Plain `http://` çalışır.
- **Hostname ASCII olmalı.** IDN (Türkçe alan adı vs.) gönderecekseniz **kendiniz punycode'a çevirin** — library çevirmez.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **RFC 3986 URL parser** | `remove_dot_segments`, redirect resolution, request-target encoding |
| **RFC 6265 cookie jar** | Domain/Path/Secure/Expires/Max-Age, IP-literal guard |
| **HTTP smuggling guard** | Çakışan `Content-Length`, `CL + TE` birlikte → reject |
| **gzip / deflate / multi-member** | Streaming DEFLATE, allocation-safe, bomb guard (1000× ratio) |
| **Keep-alive pool** | LRU, per-host limit, stale socket retry (idempotent) |
| **Proxy (HTTP + SOCKS5)** | CONNECT tunnel, absolute-URI, NO_PROXY |
| **SSRF guard** | `allow_private_networks=false` → loopback/RFC1918/CGNAT/ULA/link-local reddedilir |
| **Streaming download** | `on_body` callback, RAM'de buffering yok |
| **Chunked upload** | `use_chunked_upload = true` |
| **DNS async** | Deadline-bounded, 32 thread cap, IP literal fast-path |
| **DNS cache** | Thread-safe LRU, TTL, entry cap |
| **DoH** | RFC 8484, `application/dns-message` |
| **TLS session cache** | 64 entry LRU, `lower(host):port` anahtarı |
| **SIGPIPE guard** | POSIX'te `SSL_write` için thread-local maskeleme |
| **Android system proxy** | JNI `System.getProperty("http.proxyHost")` |
| **Cookie Secure guard** | Plaintext bağlantıda `Set-Cookie: Secure` → reject |
| **Cross-origin sensitive strip** | `Authorization`, `Cookie`, `X-Api-Key` redirect'te düşer |
| **httpoxy guard** | `http://` için sadece `http_proxy` (küçük harf), `HTTP_PROXY` yok sayılır |

---

## 🚀 Hızlı Başlangıç

### Basit GET

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
    return true;   // false dönersen request CallbackAborted ile biter
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

## 📋 Genel API

| Kategori | Sınıf / Fonksiyon |
|---|---|
| **Hata** | `knst_network_error`, `knst_network_error_string()` |
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

## 🎯 1) `knst_network_error` — Hata Enum'u

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

**`ForbiddenAddress`** — SSRF guard reddetti (`allow_private_networks = false` + private/loopback/link-local adres).

---

## 🌐 2) `knst_ip_address` — IPv4 + IPv6

```cpp
class knst_ip_address {
public:
    // Fabrika
    static knst_ip_address from_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) noexcept;
    static knst_ip_address from_ipv6(const uint8_t raw[16]) noexcept;
    static knst_ip_address from_raw(const sockaddr* sa) noexcept;
    static knst_ip_address from_string(const knst_c16string& s, knst_network_error& err) noexcept;

    static knst_ip_address loopback_v4() noexcept;   // 127.0.0.1
    static knst_ip_address any_v4()      noexcept;   // 0.0.0.0
    static knst_ip_address loopback_v6() noexcept;   // ::1
    static knst_ip_address any_v6()      noexcept;   // ::

    // Sorgu
    bool is_valid()     const noexcept;
    bool is_ipv4()      const noexcept;
    bool is_ipv6()      const noexcept;
    bool is_loopback()  const noexcept;
    bool is_any()       const noexcept;
    bool is_multicast() const noexcept;
    bool is_non_public() const noexcept;   // SSRF guard için

    // Dönüşüm
    knst_c16string       to_string() const;
    bool                 to_raw_ipv4(uint8_t out[4])  const noexcept;
    bool                 to_raw_ipv6(uint8_t out[16]) const noexcept;
    const sockaddr_storage& native() const noexcept;

    bool operator==(const knst_ip_address&) const noexcept;
    bool operator!=(const knst_ip_address&) const noexcept;
};
```

### `is_non_public()` ne yapar?

SSRF guard bu method'u kullanır. `true` döndüğü aralıklar:

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
- `64:ff9b::/96` (NAT64) — gömülü IPv4 kontrol edilir
- `64:ff9b:1::/48`
- `2001::/32` (Teredo)
- `2002::/16` (6to4) — gömülü IPv4 kontrol edilir
- `::/96` (IPv4-compatible, deprecated)

**IPv4-mapped IPv6** (`::ffff:a.b.c.d`) → gömülü IPv4 değerlendirilir.

### IPv6 zone ID

`from_string(u"fe80::1%eth0")` çalışır:
- **Linux:** `if_nametoindex()` ile çözülür
- **Windows:** Sadece sayısal zone (`%3`) kabul edilir

---

## 🔌 3) `knst_endpoint` — Adres + Port

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

### `parse()` formatları

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

### `resolve_async` davranışı

- **IP literal** → thread yok, direkt endpoint döner (fast path)
- **`timeout_ms == 0`** → sync fallback
- **Timeout aşılırsa** → worker thread `detach()` edilir (getaddrinfo iptal edilemez), `Timeout` döner. Worker arka planda biter.
- **Thread cap:** Aynı anda **32 worker** (`MAX_WORKERS`). Aşılırsa `DnsTemporaryFailure` döner — thread exhaustion engeli.
- **AI_ADDRCONFIG** → offline/loopback-only ortamda `EAI_NONAME` alınırsa **AI_ADDRCONFIG olmadan retry**. Android'de her zaman kapalı.

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
- **LRU:** `lookup` başarılı olan entry head'e taşınır
- **TTL:** `steady_clock` ile, `now >= expiry` → delete
- **Port:** Cache entry'de saklanmaz, `lookup` sırasında parametre port uygulanır
- **Key:** host lowercase ASCII (128 byte cap)

---

## 🔌 6) Socket sınıfları

### 6.1) `knst_socket_base` — Ortak yaşam döngüsü

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

    void adopt(knst_socket_handle_t h) noexcept;   // sahiplen

    knst_endpoint local_endpoint()  const noexcept;
    knst_endpoint remote_endpoint() const noexcept;

    // Timeout (0 = infinite)
    void set_read_timeout(uint32_t ms) noexcept;
    void set_write_timeout(uint32_t ms) noexcept;
    uint32_t read_timeout()  const noexcept;
    uint32_t write_timeout() const noexcept;

    // Socket seçenekleri
    bool set_nodelay(bool enable) noexcept;   // TCP_NODELAY (AF_UNIX'te skip)
    bool set_keepalive(bool enable) noexcept;
    bool set_reuse_addr(bool enable) noexcept;

    // Kapanış
    void shutdown_read()  noexcept;
    void shutdown_write() noexcept;
    void shutdown_both()  noexcept;

    // Source IP binding (connect öncesi)
    bool bind_local(const knst_endpoint& local, knst_network_error& err) noexcept;
    void clear_bind_local() noexcept;
    bool has_local_bind() const noexcept;
};
```

**`bind_local` ne yapar?** Çoklu ağ kartı / VPN split routing senaryolarında **çıkış kaynak IP**'sini sabitler. `do_connect()` içinde otomatik uygulanır, `connect_any()` / `connect_async()` de çalışır. Family mismatch (v4 endpoint + v6 bind) sessizce atlanır.

### 6.2) `knst_stream_socket_base` — Virtual I/O

```cpp
class knst_stream_socket_base : public knst_socket_base {
public:
    virtual bool has_pending_input() const noexcept;   // TLS buffer'da byte var mı?

    // Tek write denemesi (>= 1 byte başarıda)
    virtual bool send(const void* data, size_t len, size_t& sent,
                      uint32_t timeout_ms, knst_network_error& err) noexcept;

    // Deadline altında tümünü yaz
    virtual bool send_all(const void* data, size_t len,
                          uint32_t timeout_ms, knst_network_error& err) noexcept;
    bool send_all(const knst_byte_string& data,
                  uint32_t timeout_ms, knst_network_error& err) noexcept;

    // Tek read: got == 0 => clean EOF
    virtual bool recv(void* buf, size_t cap, size_t& got,
                      uint32_t timeout_ms, knst_network_error& err) noexcept;
    bool recv(knst_byte_string& out, uint32_t timeout_ms,
              knst_network_error& err, size_t max_bytes = 4096) noexcept;

    // Deadline altında tam N byte
    virtual bool recv_exact(void* buf, size_t n,
                            uint32_t timeout_ms, knst_network_error& err) noexcept;
};
```

**`virtual` sebebi:** `knst_ssl_socket` TLS handshake sonrası aynı interface üzerinden şifreli I/O sağlar. `knst_http_client` bunun üzerinden çalışır — plain ve TLS aynı kod yolu.

### 6.3) `knst_tcp_socket` — Client

```cpp
class knst_tcp_socket : public knst_stream_socket_base {
public:
    // Belirli bir endpoint'e bağlan
    bool connect(const knst_endpoint& ep, uint32_t timeout_ms,
                 knst_network_error& err) noexcept;

    // Birden fazla endpoint, tek deadline
    bool connect_any(const knst_vector<knst_endpoint>& list, uint32_t timeout_ms,
                     knst_network_error& err) noexcept;

    // Sync DNS + connect
    bool connect(const knst_c16string& host, uint16_t port, uint32_t timeout_ms,
                 knst_network_error& err) noexcept;

    // Async DNS + connect, TEK deadline
    bool connect_async(const knst_c16string& host, uint16_t port,
                       uint32_t dns_timeout_ms, uint32_t connect_timeout_ms,
                       knst_network_error& err) noexcept;
};
```

**`connect_any` stratejisi:** Toplam deadline altında her adres için **yarım kalan bütçe**. İlk adres timeout olursa ikinciye geçer. Bu IPv6/IPv4 dual-stack fallback'in temelidir.

**`connect_async`:** DNS + connect **tek deadline** altında. `dns_budget()` DNS kısmına toplam bütçeden pay ayırır.

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

**Davranış:**
- **Windows:** `SO_EXCLUSIVEADDRUSE` + `WSA_FLAG_NO_HANDLE_INHERIT`
- **POSIX:** `SO_REUSEADDR` + `SOCK_CLOEXEC` + `SO_NOSIGPIPE` (macOS)
- **`accept` sonrası:** `adopt()` → non-blocking + `TCP_NODELAY` + close-on-exec

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

**Windows özel:** `SIO_UDP_CONNRESET` kapatılır — ICMP port-unreachable alsa bile `recv_from` sessizce devam eder (Linux davranışı gibi).

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

**Windows:** `afunix.h` varsa kullanılabilir (`KNST_NETWORK_HAS_AF_UNIX_WIN`). **Linux/macOS:** tam destek.

**`listen` davranışı (POSIX):**
1. `lstat` ile dosya var mı?
2. Varsa ve socket değilse → `AddressInUse`
3. Socket ise `connect` dener → canlıysa `AddressInUse`, değilse (stale) `unlink`
4. `umask(0177)` ile dinleme süresince 0700 izin
5. `bind` + `listen` → hata olursa umask eski haline döner
6. `close()` çağrısında socket dosyası `unlink` edilir

---

## 🔐 7) TLS — `knst_ssl_socket` + global ayarlar

> **Koşullu derleme:** `KNST_NETWORK_ENABLE_TLS` tanımlıysa aktif. OpenSSL >= 1.1.0 gerekli (LibreSSL 2.7+).

### Global ayarlar (ilk bağlantıdan ÖNCE çağır)

```cpp
// Custom CA dosyası
bool knst_tls_set_ca_file(const char* path) noexcept;

// Custom CA dizini
bool knst_tls_set_ca_path(const char* dir) noexcept;

// Client certificate (mTLS)
bool knst_tls_set_client_cert(const char* cert_pem_path, const char* key_pem_path) noexcept;

// Sertifika doğrulamasını kapat (SADECE güvendiğin LAN cihazları için — self-signed IPP)
bool knst_tls_set_skip_verify(bool skip) noexcept;
```

**Dönüş değeri:** İlk TLS handshake'ten sonra çağırırsanız `false` — ayar değişmez. **`knst_tls_set_skip_verify` istisnadır:** mevcut `ctx` varsa `SSL_CTX_set_verify` ile anında uygular (test esnasında pratik).

### `knst_ssl_socket` — TLS client

```cpp
class knst_ssl_socket : public knst_tcp_socket {
public:
    // DNS + TCP + handshake (tek deadline)
    bool connect_tls(const knst_c16string& host, uint16_t port,
                     uint32_t dns_timeout_ms, uint32_t timeout_ms,
                     knst_network_error& err) noexcept;
    bool connect_tls(const knst_c16string& host, uint16_t port,
                     uint32_t timeout_ms, knst_network_error& err) noexcept;

    // Birden fazla endpoint, ilk handshake başarılı olanı al
    bool connect_tls_any(const knst_vector<knst_endpoint>& eps,
                         const knst_c16string& sni_host,
                         uint32_t timeout_ms, knst_network_error& err) noexcept;

    // Bağlı TCP üstünde handshake (proxy CONNECT sonrası)
    bool tls_handshake(const knst_c16string& sni_host, uint32_t timeout_ms,
                       knst_network_error& err, uint16_t port_hint = 0) noexcept;

    // Durum
    bool is_tls_active() const noexcept;
    bool session_reused() const noexcept;
    bool has_pending_input() const noexcept override;
};
```

**Davranış:**
- **Handshake non-blocking:** Tek deadline altında `poll()` döngüsü
- **Handshake sonrası blocking:** `SO_RCVTIMEO` + `SO_SNDTIMEO` ile per-syscall timeout
- **Verification:** `SSL_VERIFY_PEER` + hostname (or `iPAddress` SAN for IP literal)
- **Min TLS:** 1.2
- **ALPN:** Sadece `http/1.1` (H2/H3 ayrı header)
- **Compression:** `SSL_OP_NO_COMPRESSION` (CRIME)
- **Renegotiation:** `SSL_OP_NO_RENEGOTIATION` (1.1.1+)
- **Session resumption:** 64-entry LRU, key = `lower(host):port`
- **SIGPIPE:** POSIX'te `pthread_sigmask` ile thread-local koruma

### CA kaynakları (sırayla)

1. `ca_file` / `ca_path` set edildiyse → sadece onlar
2. Aksi halde:
   - **Windows:** `CertOpenSystemStoreA("ROOT")` ile sistem cert store import
   - **Android:** `/apex/com.android.conscrypt/cacerts` + `/system/etc/security/cacerts`
   - **Linux:** `/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`, `/etc/ssl/cert.pem`

### mTLS kullanımı

```cpp
// main() başında, ilk TLS bağlantısından önce
knst_tls_set_client_cert("/etc/certs/client.pem", "/etc/certs/client.key");

// Sonra normal HTTPS client
knst_http_client c;
knst_http_client::options o;
o.timeout_ms = 10000;

knst_http_response r;
knst_network_error err;
c.get(u"https://mtls.example.com/api", o, r, err);
```

### Self-signed printer (IPP)

```cpp
// Sadece belirli bir printer'a güveniyorsan
knst_tls_set_skip_verify(true);

knst_http_client c;
knst_http_client::options o;
o.timeout_ms = 5000;
o.total_timeout_ms = 10000;
o.allow_private_networks = true;   // LAN printer için

knst_http_response r;
knst_network_error err;
c.get(u"https://192.168.1.50:631/ipp/print", o, r, err);
```

> ⚠️ `skip_verify` **process-wide**. Bir kere açtın mı, tüm TLS bağlantılarında doğrulama kapanır. Prod'a çıkarken kaldır.

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
    knst_c16string host_bracketed() const;       // [v6] veya host
    knst_c16string host_header_value() const;    // Host header value

    static knst_c16string remove_dot_segments(const knst_c16string& input);
};
```

### Parse örneği

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
// Absolute URL — tamamen yeni
```

**RFC 3986 §5.2.2 davranışı:** Tek `?` ref → path aynı, query boş. `""` ref → base kopyası.

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

    // Güvenli header (reserved reddedilir: Host, Content-Length, Transfer-Encoding,
    // Connection, TE, Trailer, Upgrade, Expect, Proxy-Connection, Proxy-Authorization,
    // Accept-Encoding)
    bool set_header(const knst_c16string& name, const knst_c16string& value);

    // Dahili (library kendi set eder)
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
    bool    host_only = true;   // Domain attribute yoksa
};
```

### 9.4) `knst_http_client` — Ana API

```cpp
class knst_http_client {
public:
    typedef bool (*body_callback_t)(const uint8_t* data, size_t len, void* user);

    struct options {
        // Zamanlama
        uint32_t timeout_ms       = 10000;      // per-op
        uint32_t total_timeout_ms = 120000;     // toplam (redirects dahil)
        uint32_t dns_timeout_ms   = 0;          // 0 = timeout_ms

        // Redirect
        int      max_redirects    = 5;
        bool     follow_redirects = true;

        // Auth (sadece ORIGINAL origin'e gider)
        knst_c16string basic_user;
        knst_c16string basic_pass;

        // Header
        knst_c16string user_agent = u"KernelNucleusT/1.0";
        knst_c16string default_content_type = u"application/octet-stream";
        knst_vector<knst_http_header> headers;   // per-request extra

        // Boyut limitleri
        uint64_t max_response_bytes     = 64ULL * 1024 * 1024;   // wire
        uint64_t max_decompressed_bytes = 256ULL * 1024 * 1024;  // decode sonrası
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

        // Cookie
        bool     auto_cookie        = true;

        // Proxy
        knst_c16string proxy_url;                 // "http://[user:pass@]host:port"
        knst_c16string no_proxy;                  // empty → env
        bool     use_env_proxy      = false;      // HTTP(S)_PROXY + NO_PROXY

        // SSRF guard
        bool     allow_private_networks = true;   // false → private/loopback reddedilir

        // Source IP
        knst_c16string bind_local_ip;             // "192.168.1.5" veya "fe80::1"

        // Streaming / chunked
        body_callback_t on_body      = nullptr;
        void*           on_body_user = nullptr;
        bool            use_chunked_upload = false;

        // Legacy
        bool     lenient_close_delimited = false;
    };

    // HTTP metodları
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

    // Cookie / header yönetimi
    void clear_cookies();
    knst_vector<knst_cookie> cookies() const;
    bool set_header(const knst_c16string& name, const knst_c16string& value);
    void clear_headers();

    // Connection pool
    void     clear_connection_pool() noexcept;
    uint32_t idle_connections()      const noexcept;
    uint64_t connections_opened()    const noexcept;

    // Raw socket (WebSocket upgrade vb. için)
    knst_tcp_socket* open_raw_socket(const knst_url& u, const options& o,
                                     knst_network_error& err) noexcept;

    // Multipart
    struct multipart_part {
        knst_c16string name, filename, content_type;
        knst_byte_string data;
        knst_vector<knst_http_header> headers;   // parse tarafından doldurulur
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

## ⚙️ 10) `options` alanlarının detaylı anlamı

### `total_timeout_ms` ne yapar?

`timeout_ms` **per-syscall/per-read** bütçesi. `total_timeout_ms` **tüm request** (DNS + TCP + TLS + tüm redirect'ler + tüm body) için absolute cap. **Slow-drip** saldırılarına karşı: sunucu 10 sn'de 1 byte gönderirse `timeout_ms = 10000` yine geçer ama `total_timeout_ms = 120000` toplamda 2 dakikada keser.

**0** → sadece per-op kontrolü. **Uyarı:** 0 yapmak slow-drip korumasını kapatır.

### `allow_content_encoding`

- `true` → `Accept-Encoding: gzip, deflate, identity` gönderilir + response decode edilir
- `false` → `Accept-Encoding: identity` gönderilir, decode yok
- **Streaming modda** (`on_body` set): her zaman `identity` — decode callback'e verilmiş olsaydı stream bozulurdu

**Bomb guard:** `effective_inflate_limit(in_len, max_out)` → `min(max_out, max(1 MiB, in_len × 1000))`. Yani 1 KB gzip → en fazla 1 MB açılır.

### `use_chunked_upload`

`true` → `Transfer-Encoding: chunked` ile tek parça gönderilir (`0\r\n\r\n` sonlandırıcı). Bilinmeyen boyutlu streaming upload için. `false` (default) → `Content-Length`.

### `on_body` streaming

`on_body` set edildiyse **redirect değilse** body RAM'de tutulmaz; her chunk callback'e gelir:
- `response.body` boş kalır
- Content-Length hâlâ `max_response_bytes` cap'ine tabi
- Content-Encoding decode **yapılmaz** (`identity` gönderilir)
- `callback` `false` dönerse → `CallbackAborted`

### `lenient_close_delimited`

Content-Length / chunked yoksa body **connection close** ile sınırlanır. Bu durumda reset / TLS truncation normalde **hata** (`ConnectionClosed` / `ConnectionReset`). `true` yaparsan **başarı** sayılır. Legacy sunucu uyumluluğu — default `false`.

### `bind_local_ip`

Çıkış kaynak IP'sini sabitler (VPN split routing). **IPv4 ve IPv6 mixed endpoint'lerde:** family mismatch olanlar için sessizce atlanır, sadece uyumlu family'de bind uygulanır. Geçersiz IP → `InvalidArgument`.

### `dns_cache` + `dns_cache_ttl_ms`

`true` ise `knst_dns_cache::global()` kullanılır. `false` her seferinde `getaddrinfo`. Cache **global singleton** — process genelinde paylaşılır. **TTL ayrı**, cache entry kendi TTL'iyle saklanır.

### `dns_timeout_ms`

`0` ise `timeout_ms` kullanılır. Aksi halde DNS için ayrı bütçe (toplam deadline'dan tüketilir).

### `use_env_proxy` + `proxy_url` + `no_proxy`

`proxy_url` boşsa ve `use_env_proxy` true:
- **Android:** JNI `System.getProperty("http.proxyHost")` + `http.proxyPort` → `host:port`
- **POSIX/Windows:**
  - `https://` → `https_proxy` env, boşsa `HTTPS_PROXY`
  - `http://` → **sadece** `http_proxy` (httpoxy koruması — `HTTP_PROXY` yok sayılır)
- `no_proxy` boşsa `no_proxy` / `NO_PROXY` env okunur

### `allow_private_networks` — SSRF guard

`false` ise:
- DNS sonucu `is_non_public()` olan adresler **filtrelenir**
- Hiç public adres kalmazsa → `ForbiddenAddress`
- **Proxy kullanılıyorsa:** Sadece IP literal hedefler kontrol edilir (proxy kendi çözer)
- **Pool key'de işaretli:** `strict=true` farklı pool slot'u — guard'lı bağlantı guard'sız tarafından reuse edilemez

**Neden?** Web app içinden kullanıcı URL'si çekiyorsan (SSRF riski) → `allow_private_networks = false`. İç ağ erişimi serbestçe gerekiyorsa → `true` (default).

---

## 🔀 11) Proxy stratejisi

### Desteklenen protokoller

| Scheme | Davranış |
|---|---|
| `http://host:port` | **HTTP proxy:** plain HTTP için absolute-URI, HTTPS için CONNECT tunnel |
| `socks5://host:port` | **SOCKS5:** Target hostname **lokal çözülür**, IP proxy'ye verilir |
| `socks5h://host:port` | **SOCKS5h:** Target hostname **proxy'de çözülür** (DNS leak yok) |

### httpoxy koruması

`http://` hedefi için **`HTTP_PROXY` env okunmaz**, sadece `http_proxy` (küçük harf). Sebep: CGI ortamında `Proxy:` request header'ı `HTTP_PROXY` env'i olarak yansır → SSRF. httpoxy açığı (CVE-2016-5385).

`https://` için `HTTPS_PROXY` / `https_proxy` — ikisi de okunur (HTTPS'te CGI `Proxy:` yansımaz).

### CONNECT tunnel

```
Client → Proxy: CONNECT target.com:443 HTTP/1.1
                Host: target.com:443
                Proxy-Authorization: Basic ...

Proxy  → Client: HTTP/1.1 200 Connection established

Client → Proxy: <TLS handshake>
Client → Proxy: <HTTP request>
```

`open_raw_socket(force_tunnel=true)` — WebSocket için `ws://` bile tunnel üzerinden gider (proxy relative-URI'yi route edemez).

### `NO_PROXY` eşleştirme

- Virgülle ayrılmış liste
- `*` → her şeyi bypass
- `.example.com` → `example.com` ve alt domain'ler
- `example.com:8080` → sadece o port
- `host.equals(token)` veya `.suffix` match

---

## 🔄 12) Redirect davranışı

| Status | Method | Body |
|---|---|---|
| **301 / 302** | `POST` → `GET` | Temizlenir |
| **303** | `HEAD` hariç her şey → `GET` | Temizlenir |
| **307 / 308** | Method korunur | Body korunur |
| **300 / 304** | Follow yok | — |

### Güvenlik

- **Cross-origin** (`scheme / host / port` farklı): `Authorization`, `Proxy-Authorization`, `Cookie`, `X-Api-Key`, `X-Auth-Token` **header'ları düşer**. `options::basic_user/pass` da gönderilmez.
- **Redirect body cap:** 64 KB (`will_redirect` durumunda `max_response_bytes` yerine)
- **Redirect sınırı:** `max_redirects` (default 5)

---

## 🍪 13) Cookie davranışı (RFC 6265)

### Parse

`Set-Cookie: name=value; Domain=.example.com; Path=/api; Secure; Expires=...; Max-Age=3600`

| Attribute | Davranış |
|---|---|
| **Domain** | `.` prefix strip, alt domain match. IP literal origin'de sadece exact host eşleşmeli |
| **Path** | Verilmezse RFC 6265 §5.1.4 default-path (son `/`'a kadar) |
| **Secure** | **HTTPS değilse** Set-Cookie reddedilir |
| **Expires** | HTTP-date parse (IMF-fixdate format) |
| **Max-Age** | Önceliklidir, `Expires`'i override eder |

### Gönderme

`Cookie: n1=v1; n2=v2` — sırayla:
- Expiry kontrol (`expires_unix > 0 && < now` → skip)
- Host match (`host_only` ise exact, değilse `domain_match`)
- Path match (`path_match(req_path, cookie_path)`)
- `Secure` ise `https://` şart

### Limitler

- **Name + Value** ≤ 4096
- **Total cookie** ≤ 3000
- **Value:** Control char / DEL / > 0x7F reddedilir (Cookie header injection guard)

### IP literal origin

`Domain=.example.com` yok sayılır — **IP adresine domain atanamaz**. Sadece `host_only` cookie'ler kabul edilir.

---

## 🗜️ 14) gzip / deflate (internal)

`Content-Encoding` chain'inde `gzip`, `x-gzip`, `deflate`, `identity` desteklenir. **En fazla 4 katman** (`gzip, gzip` gibi). Decode sırası **tersine** (applied order → decode reverse).

### Bomb guard

`effective_inflate_limit(in_len, max_out) = min(max_out, max(1 MiB, in_len × 1000))`

- **1 MB gzip** → en fazla **1 GB** açılır
- **10 KB gzip** → en fazla **10 MB** açılır
- **Boş gzip** → en fazla **1 MB** açılır

Aşılırsa → `ResponseTooLarge`.

### Multi-member gzip

RFC 1952 desteklenir — arka arkaya gzip member'ları decode edilir. Trailing garbage → `HttpMalformedBody`.

### Raw deflate fallback

`deflate` **RFC 1950 (zlib header'lı)** beklenir. Ama bazı sunucular raw deflate gönderir → header + Adler32 doğrulaması başarısızsa **raw deflate** olarak retry.

---

## 🏊 15) `knst_connection_pool` — Keep-alive

```cpp
class knst_connection_pool {
public:
    // Idle, sağlıklı socket al (ownership caller'a)
    knst_tcp_socket* take(const knst_c16string& key, uint32_t idle_ms) noexcept;

    // Ownership geçer (başarıda true)
    bool put(const knst_c16string& key, knst_tcp_socket* s, uint32_t max_per_host) noexcept;

    void clear() noexcept;
    uint32_t idle_count() const noexcept;
};
```

- **Global cap:** 64 connection
- **Per-host limit:** `options::max_per_host` (default 8)
- **Sağlıklı mı?**
  - `socket_looks_alive()` → `poll(POLLIN, 0)` idle olmalı (EOF/error/data yok)
  - **`!has_pending_input()`** → TLS buffer'da byte yok (desync kontrolü)
- **Stale socket retry:** İlk exchange başarısız ve `!got_byte` + idempotent method → retry

### Pool key formatı

```
"strict|bind|scheme://host:port|proxy_host:port"
```

`strict` = SSRF guard durumu (guard'lı/guard'sız pool karışmaz).
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
o.total_timeout_ms = 3600000;   // 1 saat absolute cap
o.on_body         = on_chunk;
o.on_body_user    = f;

knst_http_response r;
knst_network_error err;
c.get(u"https://example.com/large-file.bin", o, r, err);
```

### Kurallar

- `on_body` set → **streaming mod**, `response.body` **boş** kalır
- **Redirect** durumunda streaming devre dışı (Location header okunmalı)
- **Content-Encoding** decode yapılmaz (her zaman `identity`)
- Callback `false` dönerse → `CallbackAborted`
- Chunk boyutları: internal **32 KB** staging buffer (heap'te, stack'te değil)

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

### Boundary üretimi

`----knst<hex>%08x%08x` formatı — counter + steady_clock nanos XOR. Thread-safe (`std::atomic`).

### Header param extraction

`extract_header_param(value, "boundary")` — `;` ayrılmış parametreleri parse eder, quoted/unquoted value destekler. `content-type: multipart/form-data; boundary=----xyz; charset=utf-8` → `boundary` = `----xyz`.

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

**Davranış:**
- IP literal → thread yok, direkt
- `AF_UNSPEC` → önce `A`, sonra `AAAA` dener
- Sadece `A` istiyorsan `family = AF_INET`, sadece `AAAA` için `AF_INET6`

**Örnek:**

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

**TLS gerekli:** `KNST_NETWORK_ENABLE_TLS` tanımlı değilse → `TlsNotSupported`.

---

## 🌍 19) Public IP keşfi

```cpp
namespace knst_net {
    knst_c16string public_ipv4(knst_network_error& err, uint32_t timeout_ms = 5000) noexcept;
    knst_c16string public_ipv6(knst_network_error& err, uint32_t timeout_ms = 5000) noexcept;
}
```

- `public_ipv4()` → `https://api.ipify.org` (plain text IPv4)
- `public_ipv6()` → `https://api64.ipify.org` (IPv6 beklenir, `:` yoksa boş döner)
- **TLS yoksa** → `TlsNotSupported` + boş string

---

## 🎯 20) Platform farkları

| Konu | Windows | Linux | Android |
|---|---|---|---|
| **Socket handle** | `SOCKET` (unsigned intptr) | `int` | `int` |
| **Winsock init** | `WSAStartup` (once_flag) | — | — |
| **AF_UNIX** | Win10 1803+ | ✅ | ✅ |
| **TLS cert store** | `CertOpenSystemStoreA("ROOT")` | CA files | `/apex` + `/system/etc` |
| **`SOCK_CLOEXEC`** | `WSA_FLAG_NO_HANDLE_INHERIT` | doğrudan | doğrudan |
| **SIGPIPE** | ❌ yok | `pthread_sigmask` | `pthread_sigmask` |
| **SO_NOSIGPIPE** | ❌ | macOS | ❌ |
| **`SO_EXCLUSIVEADDRUSE`** | server listen | ❌ | ❌ |
| **`SO_REUSEADDR`** | ❌ | ✅ | ✅ |
| **UDP `SIO_UDP_CONNRESET`** | kapatılır | ❌ | ❌ |
| **System proxy** | env | env | JNI `System.getProperty` |
| **IPv6 zone name** | sadece sayısal | `if_nametoindex` | `if_nametoindex` |
| **DNS `AI_ADDRCONFIG`** | kullanılır | kullanılır | **her zaman kapalı** |
| **`afunix.h`** | opsiyonel | — | — |

---

## ⚠️ 21) Sık tuzaklar

### 1) `HTTPS_PROXY` vs `http_proxy` — httpoxy

Plain `http://` isteğinde **`HTTP_PROXY` env okunmaz**. Sadece `http_proxy`. Bu bilinçli. CGI container'da çalışıyorsan `HTTP_PROXY` set etmeyin — kullanılmaz.

### 2) `total_timeout_ms = 0` yapmayın

Slow-drip saldırısına açık kalırsınız. Sunucu 10 sn'de 1 byte gönderirse `timeout_ms` geçer ama toplam sonsuz olur. **Default 120 sn tutun.**

### 3) `allow_private_networks = false` DNS rebinding koruması

DNS sonucundaki **adreslere** bakar. Host **tekrar** resolve edilirse (redirect ile) yeni adres yine kontrol edilir. Ama **proxy kullanıyorsanız** SSRF guard sadece IP literal target'lar için çalışır — proxy kendi DNS'iyle private IP'ye gidebilir. **Proxy'ye güvenin şart.**

### 4) `on_body` redirect'te çalışmaz

Streaming mod redirect response'unda **devre dışı**. Redirect body'si normal buffered okunur (64 KB cap) — Location okunabilsin.

### 5) `Secure` cookie over HTTP → reddedilir

RFC 6265bis §5.7. `Set-Cookie: x=y; Secure` HTTP üzerinden gelirse **jar'a eklenmez**.

### 6) `Host` header override edilemez

`set_header("Host", ...)` → `false` döner. `Host` header'ı URL'den otomatik oluşur. Aynı şey `Content-Length`, `Transfer-Encoding`, `Connection`, `Proxy-Authorization`.

### 7) `basic_user/pass` cross-origin redirect'te gönderilmez

Sadece **original origin**'e (`scheme + host + port` ilk URL ile aynı) gider. Farklı host'a redirect olursa Authorization header düşer. Bilinçli güvenlik.

### 8) TLS session cache — `host:port` key

Session reuse `lower(host):port` anahtarıyla çalışır. Aynı host farklı port'ta farklı session. Doğru davranış.

### 9) IPv6 zone ID Windows'ta sadece sayısal

`fe80::1%eth0` Windows'ta → `InvalidArgument`. `fe80::1%3` çalışır. Linux'ta `if_nametoindex("eth0")` çalışır.

### 10) `get_json` body boşsa null döner

`r.body.empty()` → `json() = null`, hata dönmez. **Body boş ama hata değil** — 204 No Content durumunda normal. `r.status` kontrolü yapın.

### 11) `parse()` çakışan Content-Length'i reddeder

`Content-Length: 5` + `Content-Length: 10` → `HttpMalformedHeaders`. `Content-Length: 5` + `Transfer-Encoding: chunked` → **aynı şekilde reddedilir** (HTTP request smuggling guard).

### 12) `chunked` + `use_chunked_upload`

`Transfer-Encoding: chunked` header'ı siz set etmeyin — `use_chunked_upload = true` yapın. Library doğru serialize eder (single-chunk). `set_header("Transfer-Encoding", ...)` → `false` döner.

### 13) DNS worker thread detach

Async DNS timeout olunca worker **detach** edilir. `getaddrinfo` iptal edilemez — OS seviyesinde bloklar. 32 worker cap'i bu yüzden var.

### 14) TLS handshake non-blocking under deadline

Handshake **poll() döngüsü** altında çalışır. Handshake **tamamlandıktan sonra** socket **blocking** olur + `SO_RCVTIMEO/SO_SNDTIMEO`. Yani read/write timeout per-syscall, ama handshake deadline toplam.

### 15) `SO_RCVTIMEO` 0 = sonsuz

`set_read_timeout(0)` → `SO_RCVTIMEO = 0` → **sonsuz bloklama**. Read timeout olmadan `recv` bloklarsa cevap gelene kadar bekler. `0` kullanmak istemiyorsan explicit bir değer ver.

### 16) `open_raw_socket()` force_tunnel

WebSocket için `ws://` (plain) bile **CONNECT tunnel** üzerinden gider. Sebep: HTTP proxy relative-URI'yi route edemez. `open_raw_socket()` bu yüzden `force_tunnel=true` çağırıyor.

### 17) `bind_local` family mismatch

Endpoint `192.168.1.5` (v4), `bind_local_ip = "fe80::1"` (v6) → **sessizce atlanır**. Bind uygulanmaz, normal routing. Bu bilinçli — mixed endpoint listelerinde kırılmasın.

### 18) `extract_header_param` quoted değer

`boundary="--xyz"` → value = `--xyz` (tırnak atılmış). Boşluklu değerler desteklenir.

### 19) Multipart boundary collision

Boundary **rastgele + counter**. Payload içinde geçmesi **olasılıksız** ama imkansız değil. Prod'da büyük binary upload'larda collision olursa multipart parse fail eder. Library'ye custom boundary veremezsiniz — üretimi değiştirmek gerekir.

### 20) Cookie value'de >0x7F karakter reddedilir

`Set-Cookie: name=ünïcödé` → **jar'a eklenmez**. RFC 6265 cookie değerinde non-ASCII **yasak**. Library silently reddeder.

---

## 📊 22) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| `knst_url::parse` | O(n) | Bir kez redirect başına |
| `knst_dns_resolver::resolve` | 5-100 ms | DNS cache hit ≈ 0 |
| `knst_dns_cache::lookup` | O(128) | LRU linear scan |
| `getaddrinfo` sync | bloklar | Async önerilir |
| TCP connect | 1-100 ms | RTT'ye bağlı |
| TLS handshake | 20-200 ms | Session reuse 5-30 ms |
| `SSL_write` | O(n) | Kernel buffer |
| gzip decode | O(n) | Slicing-by-8 CRC32 |
| HTTP parse | O(headers + body) | Chunked için streaming |
| Cookie match | O(cookies × headers) | Jar genellikle <100 |
| Pool take/put | O(pool size) | Max 64 |

**Tipik gecikmeler:**
- **Simple GET** (DNS + TCP + TLS + 1 round-trip): 50-150 ms LAN'da
- **Keep-alive reuse**: 5-20 ms
- **TLS session resumption**: handshake 5-30 ms (full 20-200 ms yerine)
- **DNS cache hit**: <1 ms

**Optimizasyon:**
- **`total_timeout_ms` açık tut** — slow-drip guard
- **DNS cache açık tut** (`dns_cache = true`)
- **Keep-alive açık tut** (`use_keepalive = true`)
- **Streaming download** için `on_body` kullan — RAM tasarrufu
- **`max_decompressed_bytes` düşür** — bomb guard daha agresif

---

## ✅ 23) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Total + per-op timeout birlikte
options o;
o.timeout_ms       = 10000;
o.total_timeout_ms = 30000;

// 2. Kullanıcı URL'si → SSRF guard
options o;
o.allow_private_networks = false;

// 3. Streaming download
o.on_body      = my_callback;
o.on_body_user = my_user;

// 4. TLS mTLS
knst_tls_set_client_cert("client.pem", "client.key");
// (main başında, ilk bağlantıdan ÖNCE)

// 5. Cookie'yi incele
auto jar = c.cookies();
for (auto& ck : jar) { /* ... */ }

// 6. Multipart upload
knst_byte_string body;
knst_c16string   boundary;
knst_http_client::build_multipart(parts, body, boundary);

// 7. VPN split routing için bind
options o;
o.bind_local_ip = u"10.8.0.2";   // VPN adapter IP
```

### ❌ Yapma

```cpp
// 1. Total timeout'unu 0 yapma
options o;
o.total_timeout_ms = 0;   // ❌ slow-drip açığı

// 2. on_body + Content-Encoding
options o;
o.on_body = my_callback;   // ✅ decode otomatik off, sorun değil
// ama zlib'i kendin çözmeye çalışma — stream identity gelir

// 3. Host header'ı override etme
req.set_header(u"Host", u"other.com");   // ❌ false döner

// 4. Kullanıcı URL'sini doğrudan kullanma
c.get(user_input_url, o, r, err);   // ❌ SSRF riski
// ✅ o.allow_private_networks = false;

// 5. skip_verify'ı prod'da bırakma
knst_tls_set_skip_verify(true);   // ❌ prod'da kaldır

// 6. HTTP/2 bekleme
// Bu header HTTP/1.1 — ALPN h2 istemez

// 7. Proxy DNS'i kendi çözer sanmak
options o;
o.proxy_url = u"socks5://proxy:1080";   // socks5 → LOKAL çözüm
// socks5h:// kullan → proxy çözer

// 8. Multipart'a custom boundary vermek
// Library üretir, siz değiştiremezsiniz
```

---

## 🔥 24) Gerçek kullanım örnekleri

### Örnek 1: JSON REST client

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

### Örnek 2: Büyük dosya download + progress

```cpp
struct progress_t {
    uint64_t total_received = 0;
    uint64_t start_time     = knst_steady_ms();
};

static bool on_chunk(const uint8_t* data, size_t len, void* user) {
    auto* p = static_cast<progress_t*>(user);
    p->total_received += len;
    // her 1 MB'da progress yazdır
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

### Örnek 3: SOCKS5 proxy + timeout

```cpp
knst_http_client c;
knst_http_client::options o;
o.timeout_ms       = 15000;
o.total_timeout_ms = 60000;
o.proxy_url        = u"socks5h://127.0.0.1:1080";   // DNS proxy'de

knst_http_response r;
knst_network_error err;

if (c.get(u"https://check.torproject.org/api/ip", o, r, err)) {
    std::cout << r.body.length() << " bytes\n";
}
```

### Örnek 4: SSRF guard (web app içinden kullanıcı URL fetch)

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

### Örnek 5: Multipart upload

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
o.use_chunked_upload = true;   // bilinmiyorsa boyut → chunked

knst_http_response r;
knst_network_error err;
c.post(u"https://upload.example.com/api", body, o, r, err);
```

### Örnek 6: SSRF-safe + DoH resolve + HTTP

```cpp
// 1. DNS over HTTPS ile resolve
knst_vector<knst_endpoint> eps;
knst_network_error err;

if (!knst_doh_resolver::resolve(
        u"https://cloudflare-dns.com/dns-query",
        u"example.com", 443, eps, err)) {
    return;
}

// 2. Doğrudan IP'ye git (TLS SNI host)
knst_ssl_socket ssl;
if (!ssl.connect_tls_any(eps, u"example.com", 10000, err)) {
    return;
}

// 3. HTTP request (Host header manual)
knst_http_request req;
req.method = u"GET";
req.path   = u"/";
req.set_header_internal(u"Host", u"example.com");

knst_byte_string bytes = req.serialize();
if (!ssl.send_all(bytes, 10000, err)) return;

knst_byte_string out;
if (!ssl.recv(out, 10000, err, 8192)) return;
```

### Örnek 7: UDP DNS manual

```cpp
knst_udp_socket udp;
knst_network_error err;

knst_endpoint dns_server = knst_endpoint::parse(u"8.8.8.8:53", err);
if (err != knst_network_error::None) return;

// DNS query inşa (özel library var: knst_net_detail::dns_build_query, dahili)
// (bu sadece örnek — public API değil)
```

### Örnek 8: WebSocket raw upgrade

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

// şimdi WebSocket upgrade request gönder (RFC 6455)
// ... Sec-WebSocket-Key, Upgrade: websocket, vb.
// response 101 kontrolü
// sonra framing — knst_websocket.hpp yapar

delete sock;
```

---

## 📌 25) API Kontrat Özeti

```cpp
namespace knst_net {

enum class knst_network_error : uint16_t { /* 50+ değer */ };
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
    struct options { /* 30+ alan */ };
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

## 🎯 Size Getireceği Kazanç

`knst_network` şu durumlarda ciddi avantaj sağlar:

- ✅ **Header-only HTTP client** — curl/libcurl bağımlılığı yok
- ✅ **Cross-platform** — Windows + Linux + Android aynı API
- ✅ **TLS** — OpenSSL, mTLS, session resumption, cert store import
- ✅ **DoH** — DNS over HTTPS, RFC 8484
- ✅ **SSRF guard** — Kullanıcı URL'si çeken servisler için
- ✅ **Streaming** — büyük download'lar RAM'e sığmaz
- ✅ **Chunked upload** — bilinmeyen boyutlu POST
- ✅ **Cookie jar** — RFC 6265 uyumlu
- ✅ **Multipart** — builder + parser
- ✅ **Proxy** — HTTP + SOCKS5 + SOCKS5h
- ✅ **Keep-alive pool** — latency tasarrufu
- ✅ **gzip/deflate** — bomb guard'lı
- ✅ **Async DNS** — thread cap'li, deadline'lı
- ✅ **`knst_process`**, **`knst_file`**, **`knst_prn`** ile tam entegre (IPP, eSCL, DoH)
- ✅ **KernelNucleusT uyumu** — `knst_c16string`, `knst_byte_string`, `knst_vector`, `knst_json` (opsiyonel)

---