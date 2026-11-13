# knst_network_h2 — HTTP/2 Client

Hey! 👋 `knst_network_h2.hpp` is KernelNucleusT's **HTTP/2 client** module. It runs HTTPS requests over TLS 1.3 + ALPN `h2` + nghttp2 as **multiplexed streams** on a single TLS connection. The `knst_web_*` layer is **shared with HTTP/3** — meaning `knst_network_h3.hpp` builds on top of this header.

**In short:** Requires `nghttp2 >= 1.40` + `OpenSSL >= 1.1.0` + the `KNST_NETWORK_ENABLE_TLS` define. **h2c (plaintext HTTP/2) is not supported.** If ALPN `h2` isn't selected → `NotSupported`. Proxies: HTTP CONNECT + SOCKS5/SOCKS5h — only for `https://` targets (h2 is always under TLS).

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

### Platform matrix

| Feature | Windows | Linux | Android |
|---|---|---|---|
| **HTTP/2 client (over TLS)** | ✅ | ✅ | ✅ |
| **ALPN `h2`** | ✅ | ✅ | ✅ |
| **Multiplexed streams** | ✅ | ✅ | ✅ |
| **HPACK (not QPACK)** | ✅ | ✅ | ✅ |
| **h2c (plaintext HTTP/2)** | ❌ `NotSupported` | ❌ | ❌ |
| **`http://` URL** | ❌ `NotSupported` | ❌ | ❌ |
| **HTTP CONNECT proxy** | ✅ | ✅ | ✅ |
| **SOCKS5 / SOCKS5h** | ✅ | ✅ | ✅ |
| **NO_PROXY env** | ✅ | ✅ | ✅ |
| **Session reuse (keep-alive)** | ✅ | ✅ | ✅ |
| **TLS session resumption** | ✅ | ✅ | ✅ |
| **mTLS** | ✅ | ✅ | ✅ |
| **Cookie jar** | ✅ | ✅ | ✅ |
| **Redirect** | ✅ | ✅ | ✅ |
| **gzip/deflate decode** | ✅ | ✅ | ✅ |
| **Streaming `on_body`** | ✅ driver thread | ✅ | ✅ |
| **Request trailers** | ✅ | ✅ | ✅ |
| **Expect-Continue (100)** | ✅ | ✅ | ✅ |
| **SSRF guard** | ✅ | ✅ | ✅ |
| **`use_chunked_upload`** | ❌ `InvalidArgument` | ❌ | ❌ |
| **`lenient_close_delimited`** | ⚠️ ignored (framed) | ⚠️ | ⚠️ |
| **`max_per_host`** | ⚠️ unused (see `max_sessions_per_origin`) | ⚠️ | ⚠️ |
| **Server push** | ❌ refused | ❌ | ❌ |

### Library dependencies

| Dependency | Why it's needed | Version |
|---|---|---|
| **`knst_network.hpp`** | TLS infra, socket, DNS, HTTP/1.1 helpers | internal |
| **nghttp2** | HTTP/2 protocol engine | **>= 1.40** |
| **OpenSSL** | TLS 1.3 + ALPN | **>= 1.1.0** |
| **`KNST_NETWORK_ENABLE_TLS`** | TLS must be defined, else `TlsNotSupported` | define |

**Link:** `-lnghttp2 -lssl -lcrypto`.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **One TLS, many streams** | N requests to same origin → 1 TLS socket, multiplexed |
| **CRTP base** | `knst_web_client_base<Derived>` — redirect + cookie + auth + decode shared |
| **Session pool** | Per-origin pool, saturation control via `try_reserve` |
| **Driver election** | One thread polls, others wait on mutex |
| **Non-blocking TLS handshake** | `poll()` loop under a single deadline |
| **`has_pending_input()`** | TLS buffered plaintext present? — desync guard |
| **Idle session probe** | GOAWAY/EOF check before reuse |
| **REFUSED_STREAM replay** | Streams above GOAWAY safely retryable |
| **Cookie jar (RFC 6265)** | Path-sorted sending, Secure/SameSite guard |
| **HPACK** | Managed by nghttp2 |
| **`wake_event`** | Loopback UDP self-pipe — lifts driver out of poll |
| **`sigpipe_scope`** | Thread-local masking for `SSL_write` on POSIX |
| **SSRF guard** | `allow_private_networks=false` — marked `S` in pool key |
| **Expect-Continue** | Deferred body via `expect_continue_timeout_ms` |

---

## 🚀 Quick Start

### Simple HTTP/2 GET

```cpp
#include "KernelNucleusT.hpp"

knst_http2_client c2;
knst_web_options o;
o.timeout_ms       = 5000;
o.total_timeout_ms = 15000;

knst_http_response r;
knst_network_error err;

if (c2.get(u"https://example.com/", o, r, err)) {
    std::cout << "Status: " << r.status << "\n";
    std::cout << "Body:   " << r.body.length() << " bytes\n";
} else {
    std::cerr << "Error: " << knst_network_error_string(err) << "\n";
}
```

### Streaming download

```cpp
static bool on_chunk(const uint8_t* data, size_t len, void* user) {
    return std::fwrite(data, 1, len, static_cast<FILE*>(user)) == len;
}

FILE* f = std::fopen("/tmp/big.iso", "wb");

knst_http2_client c2;
knst_web_options o;
o.timeout_ms       = 60000;
o.total_timeout_ms = 3600000;
o.on_body          = on_chunk;
o.on_body_user     = f;

knst_http_response r;
knst_network_error err;
c2.get(u"https://example.com/big.iso", o, r, err);

std::fclose(f);
```

### SOCKS5h proxy + timeout

```cpp
knst_http2_client c2;
knst_web_options o;
o.timeout_ms       = 15000;
o.total_timeout_ms = 60000;
o.proxy_url        = u"socks5h://127.0.0.1:1080";

knst_http_response r;
knst_network_error err;

if (c2.get(u"https://check.torproject.org/api/ip", o, r, err)) {
    std::cout << r.body.length() << " bytes\n";
}
```

### Expect-Continue

```cpp
knst_web_options o;
o.timeout_ms       = 30000;
o.total_timeout_ms = 60000;
o.expect_continue = true;                    // wait for 100 Continue before sending body
o.expect_continue_timeout_ms = 2000;         // but give up after 2s

knst_http_response r;
knst_network_error err;
c2.post(u"https://api.example.com/upload", big_body, o, r, err);
```

---

## 📋 General API

| Category | Class / Function |
|---|---|
| **Shared layer** | `knst_web_detail::*` (h2 + h3) |
| **Request descriptor** | `knst_web_header`, `knst_web_request` |
| **Options** | `knst_web_options` |
| **Response assembler** | `knst_web_stream` |
| **Socket** | `knst_h2_socket` |
| **Session** | `knst_h2_session` |
| **Pool** | `knst_web_session_pool<S>` |
| **CRTP base** | `knst_web_client_base<Derived>` |
| **Client** | `knst_http2_client` |
| **Cookie jar** | `knst_cookie_jar` (`knst_web_detail::knst_cookie_jar_impl`) |

---

## 🔌 1) `knst_web_detail` — Shared Layer

This namespace is **shared between HTTP/2 and HTTP/3**. Proxies, cookie jar, header validation, resolver helpers, TLS ctx builder — all here.

### 1.1) `proxy_cfg` — Proxy configuration

```cpp
struct proxy_cfg {
    enum class kind_t : uint8_t { None, Http, Socks5 };
    kind_t           kind = kind_t::None;
    bool             socks5_remote_dns = false;
    knst_c16string   host;
    uint16_t         port = 0;
    bool             has_auth = false;
    knst_c16string   auth;          // HTTP: "Basic <b64>"
    knst_byte_string user_raw;      // SOCKS5
    knst_byte_string pass_raw;
    bool enabled() const noexcept;
};
```

- **`Http`** — CONNECT tunnel (https target mandatory)
- **`Socks5`** — `socks5://` — hostname **resolved locally**, IP handed to proxy
- **`Socks5` + `socks5_remote_dns=true`** — `socks5h://` — hostname **resolved by proxy**

### 1.2) `host_in_no_proxy` + `resolve_proxy`

```cpp
bool host_in_no_proxy(const knst_c16string& host, const knst_c16string& list);
bool resolve_proxy(const knst_url& target, const knst_http_client::options& o,
                   proxy_cfg& pc, knst_network_error& err) noexcept;
```

**`resolve_proxy` sources:**
- `o.proxy_url` — if set, uses it
- `o.use_env_proxy` → `https_proxy` / `HTTPS_PROXY` (target is https, so both)
- `no_proxy` / `NO_PROXY` env — bypass list

**Bypass matching:** `*` everything, `.example.com` subdomain, `host:port` specific port.

**httpoxy note:** `resolve_proxy` reads **only `https_proxy`**, because h2 accepts only `https://` targets. `http_proxy` (lowercase) relates to CGI `Proxy:` header reflection — plain `http://` is `NotSupported` in this header anyway.

### 1.3) `proxy_connect_tunnel` — HTTP CONNECT

```cpp
bool proxy_connect_tunnel(knst_tcp_socket& sock, const knst_url& u,
                          const proxy_cfg& pc, const knst_http_client::options& o,
                          const deadline_t& dl, knst_network_error& err) noexcept;
```

**What it does?**
1. Sends `CONNECT host:port HTTP/1.1` (adds `Proxy-Authorization` if present)
2. Reads response head (16 KiB cap)
3. Expects `2xx` — else `ProxyFailed`

**Extra byte check:** Even on `2xx`, if there are **extra bytes** after the head → `ProxyFailed` (breaks h2 framing).

### 1.4) `socks5_handshake` — RFC 1928

```cpp
bool socks5_handshake(knst_tcp_socket& sock, const knst_c16string& target_host,
                      uint16_t target_port, const proxy_cfg& pc,
                      const deadline_t& dl, knst_network_error& err) noexcept;
```

**Flow:**
1. Greeting — `0x05 [0x00] [0x02]` (if auth)
2. Method selection — server `0x00` (no-auth) or `0x02` (user/pass)
3. Auth — `0x01 user_len user pass_len pass`
4. CONNECT — `0x05 0x01 0x00 [addr_type] [addr] [port]`
5. Response — `0x05 rep rsv atyp [bind_addr] [bind_port]`

**addr_type:**
- `0x01` — IPv4 (4 bytes)
- `0x04` — IPv6 (16 bytes)
- `0x03` — Hostname (len + bytes)

**Note:** Even with `socks5h` (remote DNS), `target_host` is passed to the proxy — the proxy does the DNS. With `socks5` (local DNS), the caller must have already converted to IP.

### 1.5) `resolve_endpoints` — DNS + SSRF

```cpp
bool resolve_endpoints(const knst_c16string& host, uint16_t port,
                       const knst_http_client::options& o,
                       uint32_t dns_to, const deadline_t& dl, bool filter,
                       knst_vector<knst_endpoint>& out, knst_network_error& err) noexcept;
```

**What it does?**
1. **Cache lookup** — if `o.dns_cache`, `knst_dns_cache::global().lookup`
2. **Async resolve** — cache miss → `knst_dns_resolver::resolve_async` (deadline-bound)
3. **Cache store** — on success with `o.dns_cache_ttl_ms`
4. **SSRF filter** — if `filter=true`, drops `is_non_public()`; if all dropped → `ForbiddenAddress`

### 1.6) `literal_target_allowed` — SSRF behind proxy

```cpp
bool literal_target_allowed(const knst_c16string& host, knst_network_error& err) noexcept;
```

When a proxy is used, DNS is done by the proxy — so the SSRF guard only checks **IP literal** targets. If the host isn't an IP, returns `true` (proxy resolves itself). If IP, checks `is_non_public()`.

### 1.7) `knst_cookie_jar_impl` — RFC 6265

```cpp
class knst_cookie_jar_impl {
public:
    void clear();
    knst_vector<knst_cookie> snapshot() const;
    knst_c16string header_for(const knst_url& u) const;
    void collect(const knst_http_response& resp, const knst_url& origin);
};
typedef knst_cookie_jar_impl knst_cookie_jar;
```

**`header_for` path ordering:** RFC 6265 §5.4 — **longer paths first**. Equal paths keep insertion order (`std::stable_sort`).

**`collect` — Set-Cookie parse:**
- **Domain** — `.` prefix strip, subdomain match, exact host for IP-literal origins
- **Path** — if absent, §5.1.4 default-path
- **Secure** — rejected if not HTTPS
- **Max-Age** — overrides `Expires`
- **Value** — Control chars / DEL / > 0x7F rejected (injection guard)
- **Name + Value** ≤ 4096
- **Total** ≤ 3000
- **Domain `.com`-style TLD** — rejected via `dots == 0` check (at least one dot required)

### 1.8) Header validation

```cpp
bool is_reserved_header(const knst_byte_string& n, const knst_byte_string& v) noexcept;
bool is_sensitive_name(const knst_byte_string& n) noexcept;
bool check_user_header(const knst_http_header& h, knst_byte_string& n,
                       knst_byte_string& v, knst_network_error& err);
```

**`is_reserved_header` — user cannot set:**
- `host`, `content-length`, `transfer-encoding`, `connection`, `keep-alive`
- `proxy-connection`, `upgrade`, `expect`, `proxy-authorization`
- `accept-encoding`, `trailer`
- `te` — **only accepts `trailers` value**, otherwise reserved

**`is_sensitive_name` — dropped on cross-origin redirect:**
- `authorization`, `cookie`, `x-api-key`, `x-auth-token`, `proxy-authorization`

**`check_user_header` — pseudo-header check:** Headers starting with `:` rejected (user cannot set `:method`).

### 1.9) `is_idempotent_method`

```cpp
bool is_idempotent_method(const knst_byte_string& m) noexcept;
```

`GET`, `HEAD`, `PUT`, `DELETE`, `OPTIONS`, `TRACE` — eligible for stale-connection retry.

### 1.10) `wake_event` — Loopback UDP self-pipe

```cpp
class wake_event {
public:
    bool init() noexcept;
    void signal() noexcept;
    void drain() noexcept;
    knst_socket_handle_t handle() const noexcept;
    bool ok() const noexcept;
    void close() noexcept;
};
```

**What it does?** Binds a UDP socket to `127.0.0.1:0`. `signal()` → sends 1 byte to self. `poll()` sees it readable.

**Why?** To wake the driver thread blocked in `poll()` when another thread adds a request to the same session. Windows lacks `WSAEventSelect` for this purpose; this is a portable solution.

**Fallback:** If `init()` fails, poll timeout is **capped at 50 ms** — still works, just more polls.

### 1.11) `poll_sock_wake`

```cpp
void poll_sock_wake(knst_socket_handle_t s, bool want_write, knst_socket_handle_t w,
                    uint32_t timeout_ms, bool& s_read, bool& s_write, bool& woke,
                    knst_network_error& err) noexcept;
```

**Behavior:** Waits for socket readable **or** writable (if requested) **or** wake fd readable. If the socket has error/hangup, reported as `s_read = true` — the next `recv` surfaces the real error.

### 1.12) TLS ctx

```cpp
bool host_to_cstr(const knst_c16string& host, char* buf, size_t cap) noexcept;
bool tls_prepare_peer(SSL* ssl, const char* hbuf) noexcept;
SSL_CTX* build_ssl_ctx(const unsigned char* alpn, unsigned alpn_len,
                       bool tls13_only, bool session_cache) noexcept;
SSL_CTX* tls_ctx_h2() noexcept;
```

**`build_ssl_ctx` — for h2:**
- TLS 1.2 minimum (RFC 9113 allows TLS 1.2+ for h2)
- `SSL_OP_NO_COMPRESSION`, `SSL_OP_NO_RENEGOTIATION`, `SSL_OP_IGNORE_UNEXPECTED_EOF` (framed-protocol tolerance)
- ALPN `h2` set
- Session cache (`SSL_SESS_CACHE_NO_INTERNAL_STORE`)
- CA: `ca_file`/`ca_path` if set; else default paths + **Android `/apex` + `/system/etc` + Linux CA bundles + Windows cert store** — all via `knst_net_detail::tls_st()`
- mTLS: `client_cert` + `client_key` if present, chain + key + check

**`tls_prepare_peer`:**
- **IP literal** → `X509_VERIFY_PARAM_set1_ip_asc` (no SNI, iPAddress SAN)
- **Hostname** → SNI + `SSL_set1_host` + `X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS`

**`tls_ctx_h2` — process-wide singleton:** one `build_ssl_ctx(alpn="h2", ..., tls13_only=false, session_cache=true)` per `once_flag`.

### 1.13) `sigpipe_scope` + `min_deadline` + `is_conn_error`

```cpp
struct sigpipe_scope { /* sigpipe_guard in ctor, auto in dtor */ };
deadline_t min_deadline(const deadline_t& a, const deadline_t& b) noexcept;
time_pt now_tp() noexcept;
bool is_conn_error(knst_network_error e) noexcept;
```

**`sigpipe_scope`** — On POSIX, masks SIGPIPE on the current thread during `SSL_write`. Wraps `knst_net_detail::sigpipe_guard`.

**`is_conn_error`** — `ConnectionClosed`, `HttpConnectionClosed`, `ConnectionReset`, `ConnectionAborted`, `BrokenPipe` — retryable errors.

---

## 📝 2) `knst_web_header` / `knst_web_request` / `knst_web_options`

### 2.1) `knst_web_header` / `knst_web_request`

```cpp
struct knst_web_header {
    knst_byte_string name;   // lowercase
    knst_byte_string value;
};

struct knst_web_request {
    knst_byte_string method, authority, path;
    knst_vector<knst_web_header> headers;   // lowercase
    knst_vector<knst_web_header> trailers;
    const uint8_t* body = nullptr;
    size_t         body_len = 0;
};
```

**Note:** `name` is always **lowercase**. `path` is **percent-encoded** (from `u.request_target()`).

### 2.2) `knst_web_options`

**Inherits** `knst_http_client::options` and adds h2/h3 fields:

```cpp
struct knst_web_options : public knst_http_client::options {
    // Trailers (sent after body)
    knst_vector<knst_http_header> request_trailers;

    // Expect-Continue
    bool     expect_continue            = false;
    uint32_t expect_continue_timeout_ms = 1000;

    // Stream limits
    uint32_t max_concurrent_streams  = 100;      // local cap per session
    uint32_t initial_stream_window   = 1u << 20; // 1 MiB
    uint32_t connection_window       = 16u << 20;// 16 MiB
    uint32_t max_sessions_per_origin = 2;        // extra sessions after saturation
    uint32_t cancel_grace_ms         = 2000;     // wait for RST_STREAM flush
};
```

**What does `expect_continue` do?** Adds `Expect: 100-continue` header before sending the body. If server returns `100 Continue`, body is sent. If not within `expect_continue_timeout_ms`, **gives up and sends the body anyway**. In h2 framing this is managed with `NGHTTP2_ERR_DEFERRED`.

---

## 🌊 3) `knst_web_stream` — Response Assembler

Shared by h2 and h3. One instance per active stream.

```cpp
struct knst_web_stream {
    // Request side
    const uint8_t* body;
    size_t         body_len, body_off;
    knst_vector<knst_web_header> trailers;
    bool           body_done;
    bool           expect, continue_ok, deferred;
    time_pt        expect_deadline;

    // Response side
    knst_http_response* out;
    knst_http_client::body_callback_t on_body;
    void*          on_body_user;
    uint64_t       max_body, received;
    uint32_t       max_header_bytes, hdr_bytes;
    bool           is_head, follow, allow_enc;
    bool           got_final, no_body, discard;
    bool           response_started, delivered;
    const char16_t* version;                     // u"HTTP/2" or u"HTTP/3"
    knst_vector<knst_http_header> tmp;           // header accumulation
    int            tmp_status;
    std::unique_ptr<knst_net_detail::byte_sink> sink;

    // Lifecycle
    bool           closed, cancel_req, rst_sent, abandoned, refused;
    knst_network_error err;
    time_pt        last_activity, cancel_at, total_end;
    bool           has_total;
    uint32_t       idle_ms;

    bool    resp_end;            // END_STREAM seen
    int32_t stream_id;

    // Methods
    bool setup(knst_http_response*, const knst_web_request&, const knst_web_options&,
               const deadline_t&, uint32_t idle_ms, const char16_t* version) noexcept;
    void touch() noexcept;
    time_pt expiry() const noexcept;
    void begin_block() noexcept;
    knst_network_error add_header(const uint8_t*, size_t, const uint8_t*, size_t) noexcept;
    block_t end_block(knst_network_error&) noexcept;
    bool complete() const noexcept;
    bool finished() const noexcept;
    void fail(knst_network_error) noexcept;
    bool on_data(const uint8_t*, size_t, knst_network_error&) noexcept;
    void on_close(knst_network_error, bool refused) noexcept;
    void finalize() noexcept;
};
```

### `block_t` values

| Value | Meaning |
|---|---|
| **`Error`** | `st->err` set — stream must be RST |
| **`Interim`** | `1xx` (except 100, e.g. 103 Early Hints) — not stored |
| **`Continue100`** | `100 Continue` — resume deferred body |
| **`Final`** | `2xx`/`3xx`/`4xx`/`5xx` — headers complete |
| **`Trailer`** | Trailer block — appended to `out->headers` |

### `expiry()` — deadline computation

```cpp
time_pt expiry() const noexcept;
```

**Returns:** `min(total_end, last_activity + idle_ms)`. Whichever comes first. If `total_end` absent (infinite deadline), just idle. If both absent, `time_pt::max()`.

### `on_data` — body chunk

```cpp
bool on_data(const uint8_t* d, size_t n, knst_network_error& e) noexcept;
```

**Flow:**
1. `!got_final` → `HttpMalformedBody` (data can't arrive before headers)
2. `no_body || discard || n == 0` → **swallow** (HEAD/204/304/redirect body swallowed)
3. `received + n > max_body` → `ResponseTooLarge`
4. **Streaming mode** (`on_body`): invoke callback, false → `CallbackAborted`
5. **Buffered mode:** memcpy into `byte_sink`

---

## 🔌 4) `knst_h2_socket` — TCP + TLS (ALPN h2)

Inherits `knst_tcp_socket`. Non-blocking I/O + TLS.

```cpp
class knst_h2_socket : public knst_tcp_socket {
public:
    enum class io_t { Ok, WantRead, WantWrite, Eof, Fail };

    bool start_tls_h2(const knst_c16string& host, uint16_t port,
                      const deadline_t& dl, knst_network_error& err) noexcept;

    bool has_pending_input() const noexcept override;

    // Plain send/recv (before proxy handshake)
    bool send(...) noexcept override;
    bool recv(...) noexcept override;

    // TLS-aware
    io_t read_some(void* buf, size_t cap, size_t& got, knst_network_error& err) noexcept;
    io_t write_some(const void* data, size_t len, size_t& sent, knst_network_error& err) noexcept;

    void close() noexcept override;
};
```

### 4.1) `start_tls_h2`

**What it does?**
1. `SSL_new(tls_ctx_h2())`
2. **Hostname**: IP literal → `X509_VERIFY_PARAM_set1_ip_asc` (no SNI); else SNI + `SSL_set1_host`
3. Session resumption key: `"h2|lower(host):port"` (cache shared with h3 but prefix differs)
4. Handshake **non-blocking** — `poll()` loop under a single `deadline`
5. **ALPN check** — if `h2` not selected → `NotSupported` (server doesn't support h2)
6. **On success, socket stays NON-BLOCKING** (unlike plain `knst_ssl_socket` which goes blocking)

**Error paths:**
- `TlsCertificateInvalid` — hostname/CA mismatch
- `TlsHandshakeFailed` — other TLS failures
- `NotSupported` — ALPN not `h2`
- `Timeout` — deadline exceeded

### 4.2) `read_some` / `write_some`

**`SSL_ERROR_WANT_READ` / `WANT_WRITE`** → returns `io_t::WantRead` / `WantWrite`. Caller `poll()`s and retries.

**`SSL_ERROR_ZERO_RETURN`** → `io_t::Eof` (clean close_notify).

**`SSL_ERROR_SYSCALL` + errno==0** → `io_t::Eof` (unexpected EOF — framed protocol tolerates this via `SSL_OP_IGNORE_UNEXPECTED_EOF`).

**`EINTR`** → retry (in-loop).

### 4.3) `close()` — silent shutdown

The `m_noshut` flag skips `SSL_shutdown`:
- Handshake failure
- `read_some`/`write_some` protocol failure
- `read_some` SYSCALL failure

In these cases `SSL_shutdown` can **deadlock/hang**.

**On successful connection:** non-blocking `SSL_shutdown` runs, no hang.

### 4.4) `has_pending_input()`

Any **decrypted plaintext** buffered in TLS? `SSL_pending(m_ssl) > 0`.

**Why important?** The socket may not be readable yet TLS buffer has bytes. `poll()` won't see this — check before reading.

### 4.5) Plain `send`/`recv` — only before proxy handshake

If TLS is active, returns `NotSupported`. The proxy CONNECT/SOCKS5 handshake happens **before TLS**, so it uses plain socket.

---

## 🧠 5) `knst_h2_session` — Not QUIC, TLS/TCP Session

One TLS connection, many multiplexed streams.

### 5.1) Factory + pool interface

```cpp
class knst_h2_session {
public:
    static std::shared_ptr<knst_h2_session> create(
        const knst_url& u, const knst_web_detail::proxy_cfg& pc,
        const knst_web_options& o, const deadline_t& total,
        knst_network_error& err) noexcept;

    // Pool interface
    bool     is_usable() const noexcept;
    bool     try_reserve(uint32_t local_cap) noexcept;
    void     force_reserve() noexcept;
    void     release() noexcept;
    uint32_t load() const noexcept;
    time_pt  idle_since() const noexcept;
    uint64_t requests_started() const noexcept;
    void     retire() noexcept;

    bool execute(const knst_web_request& r, const knst_web_options& o,
                 const deadline_t& total, knst_http_response& out,
                 knst_web_stream& st, knst_network_error& err) noexcept;
};
```

### 5.2) `create` — connection flow

```
1. Parse bind_local_ip
2. SSRF guard (IP-literal check if proxy)
3. DNS resolve (target or proxy host)
4. TCP connect (under conn deadline)
5. Proxy handshake (CONNECT or SOCKS5) — if present
6. TLS handshake (ALPN h2) — mandatory
7. nghttp2 session init (settings: ENABLE_PUSH=0, window, max_header)
8. wake_event init (failure tolerated)
```

**Deadline merge:** `cd = min(o.timeout_ms, total)`. So DNS + connect + proxy + TLS under a single deadline.

**`TlsCertificateInvalid` special:** doesn't try other addresses — hostname check fails for all.

### 5.3) `execute` — submit request + block

**Flow:**
1. **Slot guard** — `release()` on return
2. **Header list build** — pseudo (`:method`, `:scheme="https"`, `:authority`, `:path`) + user headers
3. **`st.setup()`** — response assembler
4. **Expect-Continue** — `continue_ok = false` (until 100 arrives)
5. **Body provider** — if `r.body_len > 0 || trailers`
6. **`nghttp2_submit_request`** — creates stream, `st.stream_id = sid`
7. **Idle probe** — if `!m_driving && load <= 1`, check GOAWAY/EOF (`probe_idle`)
8. **`is_usable()`** — retire/goaway/fatal check
9. **`nghttp2_session_check_request_allowed`** — session closed?
10. **`run(st, lk)`** — driver election + event loop
11. **After completion** — `RST_STREAM` (if not resp_end), `m_srcs` cleanup

### 5.4) Locking model

```
m_mtx guards:
  - nghttp2_session*
  - m_streams (sid → st*)
  - m_srcs (sid → body provider)
  - all stream state

nghttp2 callbacks:
  - Invoked WITH m_mtx HELD
  - Callbacks NEVER take locks

Driver:
  - Marked by m_driving flag
  - One thread does socket I/O
  - m_mtx released only around socket syscall
  - m_wbuf, m_rbuf, m_woff driver-only

Sleeping threads:
  - m_cv.wait_until(lk, expiry or 1s)
```

### 5.5) `run()` — driver election

```
Loop:
  if st.finished() → return
  if m_fatal → st.fail(m_fatal_err)
  if !m_driving:
      m_driving = true
      drive(lk, st)                       # event loop
      m_driving = false
      m_cv.notify_all()                   # hand off to waiter
      continue
  # I'm not the driver
  m_cv.wait_until(lk, min(st.expiry, now+1s))
```

### 5.6) `drive()` — event loop

1. **`scan_timers(now)`** — idle + total deadline check
2. **`pull_output()`** — `nghttp2_session_mem_send` → `m_wbuf` (bounded 256 KB)
3. **`want_write`/`want_read` check** — if session done → fatal
4. **`write_burst(lk, ...)`** — non-blocking `write_some`, `WantWrite` → break
5. **`has_pending_input()`** — TLS buffer has bytes?
6. **`poll_sock_wake`** — socket readable/writable or wake
7. **`read_burst(lk, 16)`** — `read_some` → `nghttp2_session_mem_recv`
8. **`m_cv.notify_all()`** — streams may have finished

### 5.7) nghttp2 callbacks

| Callback | What it does |
|---|---|
| **`cb_begin_headers`** | `st->touch()` + `st->begin_block()` |
| **`cb_header`** | `st->add_header()` — error → `NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE` (RST this stream) |
| **`cb_frame` HEADERS** | `st->end_block()` — `Continue100` → resume, `Final` + redirect → RST (don't download body) |
| **`cb_frame` DATA** | `END_STREAM` → `resp_end = true` |
| **`cb_frame` GOAWAY** | `m_goaway = true` — nghttp2 closes streams with REFUSED_STREAM |
| **`cb_frame` SETTINGS** | `m_peer_max = MAX_CONCURRENT_STREAMS` (if not ACK) |
| **`cb_frame` PUSH_PROMISE** | `RST_STREAM(REFUSED_STREAM)` — push refused |
| **`cb_data`** | `st->on_data()` — error → `NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE` |
| **`cb_close`** | Error code → `st->on_close()`, `m_srcs.erase()` |
| **`cb_body`** | Request body provider — zero-copy from caller buffer |

### 5.8) `cb_close` — error mapping

| nghttp2 code | knst_network_error | refused? |
|---|---|---|
| `NGHTTP2_NO_ERROR` | `None` | no |
| `NGHTTP2_REFUSED_STREAM` | `ConnectionAborted` | **yes** (retry safe) |
| `NGHTTP2_HTTP_1_1_REQUIRED` | `NotSupported` | no |
| `NGHTTP2_CANCEL` | `ConnectionReset` | no |
| Others | `ProtocolError` | no |

### 5.9) `cb_body` — body provider

**Flow:**
1. `st->expect && !st->continue_ok` → `NGHTTP2_ERR_DEFERRED` (waiting for 100) or on timeout → `continue_ok = true`
2. `body_off < body_len` → memcpy from caller buffer
3. `body_off >= body_len` → `NGHTTP2_DATA_FLAG_EOF`
4. **Trailers present** → `NGHTTP2_DATA_FLAG_NO_END_STREAM` + `nghttp2_submit_trailer`

**Note:** `src->st = nullptr` on cleanup — if provider accesses `st` afterwards it fails.

### 5.10) `probe_idle` — pre-reuse check

```cpp
void probe_idle(std::unique_lock<std::mutex>& lk) noexcept;
```

Before reusing an idle connection, does `read_burst(lk, 8)`. Purpose: catch pending GOAWAY or EOF bytes. This way a stale connection is retired **before** submitting a request.

**Why important?** If server sent GOAWAY and the session isn't retired, submit returns `STREAM_ID_NOT_AVAILABLE`.

### 5.11) `flush_locked` — last RST effort

If RST_STREAM is queued at end of execute but no driver exists, `flush_locked` **becomes the driver itself**, tries to send under `cancel_grace_ms`.

### 5.12) `pull_output` — bounded

While `m_wbuf - m_woff < 256 KB`, call `nghttp2_session_mem_send`. Backpressure: stops if buffer full.

### 5.13) `set_fatal_locked` — fail all streams

```cpp
void set_fatal_locked(knst_network_error e) noexcept;
```

Sets `m_fatal` + stores `m_fatal_err` + **`st->fail(e)` on all active streams** + `m_cv.notify_all()`.

---

## 🌐 6) `knst_web_session_pool<S>` — Session Pool

```cpp
template<class S>
class knst_web_session_pool {
public:
    typedef std::shared_ptr<S> ptr;

    template<class F>
    ptr acquire(const std::string& key, uint32_t local_cap, uint32_t max_sessions,
                uint32_t idle_ms, const deadline_t& dl, F&& make,
                knst_network_error& err) noexcept;

    void clear() noexcept;
    uint32_t size() const noexcept;
};
```

### 6.1) `acquire` flow

1. **Cache lookup** — suitable session exists?
2. **Creation lock** — `timed_mutex` per key
3. **Re-lookup** — for race condition
4. **Saturation check** — `cnt >= max_sessions` → `force_reserve` the least-loaded session (overcommit, nghttp2 queues)
5. **New session** — call `make(err)`
6. **Slot reserve** — `force_reserve()`

**Why creation mutex?** If N threads hammer the same origin, avoid opening N connections. Second lookup under lock finds the winner.

**`force` semantics:** On saturation — `try_reserve` fails → `force_reserve` (over limit, nghttp2 queues).

### 6.2) Stale session cleanup

Every `find()`:
- `!is_usable()` → retire
- `idle_ms` exceeded → retire

If `add()` exceeds `MAX_TOTAL=64`, oldest idle retired.

---

## 🎛️ 7) `knst_web_client_base<Derived>` — CRTP Base

Shared layer for h2 and h3: redirect, cookie, auth, decode, header validation.

```cpp
template<class Derived>
class knst_web_client_base {
public:
    typedef knst_web_options                 options;
    typedef knst_http_client::multipart_part multipart_part;

    // HTTP methods
    bool get/post/put/del/head/request(...) noexcept;

    // Cookie / header
    void clear_cookies();
    knst_vector<knst_cookie> cookies() const;
    bool set_header(const knst_c16string& name, const knst_c16string& value);
    void clear_headers();

    // JSON (KNST_NETWORK_ENABLE_JSON)
    bool get_json/post_json(...) noexcept;

    // Multipart forward
    static void build_multipart(...);
    static knst_c16string multipart_content_type(...);
    static bool parse_multipart(...) noexcept;
};
```

**Derived provides:** `exchange(url, proxy, options, request, response, total_deadline, err)`.

### 7.1) `do_request` — main flow

```
1. use_chunked_upload check (InvalidArgument)
2. Method validation
3. User headers + trailers + client headers validation
4. Build total deadline
5. Loop (redirect):
   a. Parse URL
   b. Store origin_url (first)
   c. Strip userinfo
   d. scheme https check (else NotSupported)
   e. Resolve proxy_cfg
   f. Build request:
      - :method, :authority, :path
      - user-agent, accept, accept-encoding
      - client headers + user headers (strip sensitive on cross-origin)
      - Authorization (not cross-origin, not if user-supplied)
      - Cookie (auto_cookie)
      - Content-Length / Content-Type (if body)
   g. exchange() → Derived
   h. Cookie collect
   i. Redirect?
      - 303 / 301+POST → GET, drop body
      - 307/308 → method + body preserved (incl. trailers)
      - Location resolve (RFC 3986)
      - max_redirects exceeded → HttpTooManyRedirects
   j. Content-Encoding decode (buffered, non-on_body)
   k. Return
```

**Cross-origin** — if `scheme + host + port` differ: `authorization`, `cookie`, `x-api-key`, `x-auth-token`, `proxy-authorization` user headers dropped.

**`dropped` flag** — if POST was rewritten to GET, content-type cleared, trailers not sent.

### 7.2) `set_header` — client-level default

User adds default via `set_header("X-My", "val")`. `check_user_header` validates — pseudo/reserved rejected.

---

## 🌐 8) `knst_http2_client` — Public API

```cpp
class knst_http2_client : public knst_web_client_base<knst_http2_client> {
public:
    void     clear_connection_pool() noexcept;
    uint32_t idle_sessions() const noexcept;

private:
    friend class knst_web_client_base<knst_http2_client>;

    static std::string make_key(const knst_url& u, const proxy_cfg& pc, const options& o);
    bool exchange(...) noexcept;

    knst_web_session_pool<knst_h2_session> m_pool;
};
```

### 8.1) `make_key` — pool key

```
"h2|{O|S}|host:port[|p:proxy_host:port[|a:hash]][|b:bind_ip]"
```

- `O` = `allow_private_networks=true`
- `S` = `allow_private_networks=false` (SSRF guard)
- `p:` — proxy host:port + type (`h` HTTP, `H` socks5h, `s` socks5) + auth hash
- `b:` — bind local IP

**Why auth hash?** Different proxy credentials → different pool → no re-auth needed.

### 8.2) `exchange` — request + retry

```
for attempt in 0..2:
    s = pool.acquire(key, max_streams, max_sessions, idle_ms, total, make, err)
    if !s → false
    reused = s.requests_started() > 0
    out.reset, st.reset
    ok = s.execute(r, o, total, out, st, err)
    if !o.use_keepalive → s.retire()
    if ok → true
    
    # Retry decision
    safe = st.refused                                              # GOAWAY/REFUSED_STREAM
        OR (!st.response_started && reused && idem && is_conn_error(err))
    if safe && attempt < 2 → retry
    return false
```

**Two retry types:**
1. **`st.refused`** — `REFUSED_STREAM` or stream above GOAWAY → **safe for any method** (server didn't process)
2. **`!response_started && reused && idem`** — stale reused connection died **before first byte** → **idempotent only** retry

**Max 3 attempts.**

### 8.3) `use_keepalive = false`

`retire()`s the session — doesn't return to pool, next request opens new session.

### 8.4) `idle_sessions()`

Number of live sessions in pool.

---

## ⚠️ 9) Common Gotchas

### 1) h2c not supported

`http://` URL → `NotSupported`. h2 is always under TLS (RFC 7540 §3.3, RFC 9113 §3.3). If ALPN `h2` not selected → `NotSupported`.

### 2) Proxy only for https targets

`resolve_proxy` reads `https_proxy` because the target is `https`. `http_proxy` is **not read** — plain HTTP is `NotSupported` here anyway.

### 3) `use_chunked_upload` → `InvalidArgument`

H2 is framed with DATA — no `Transfer-Encoding: chunked`. This field is h1-only.

### 4) `lenient_close_delimited` **ignored**

H2 is a framed protocol — connection close doesn't delimit the body. `st->resp_end` comes from END_STREAM flag.

### 5) `max_per_host` **unused**

That's for h1. In H2 there's `max_sessions_per_origin` (default 2) and `max_concurrent_streams` (default 100).

### 6) `on_body` runs on the **driver thread**

Whichever thread called `run()` and became driver will run `on_body`. **Don't re-enter the client** — deadlock.

### 7) TLS settings **before first connection**

`knst_tls_set_ca_file`, `knst_tls_set_client_cert` — in `main()`, **before** the first h2/h3 connection. Calling them later returns `false`.

**`knst_tls_set_skip_verify` exception** — applied to `ctx` immediately.

### 8) `skip_verify` is process-wide

Once enabled, **all** h2/h3/h1 TLS connections affected. Remove before shipping to prod.

### 9) IP literal — no SNI

If host is IP, no SNI is sent, `iPAddress` SAN is checked. If hostname, SNI + hostname check.

### 10) Hostname must be ASCII

`host_to_cstr` rejects non-ASCII. IDN (Turkish etc.) must be **converted to punycode**.

### 11) Cross-origin sensitive header strip

`Authorization`, `Cookie`, `X-Api-Key`, `X-Auth-Token`, `Proxy-Authorization` — **not sent** to a different origin on redirect. Intentional security.

### 12) `is_reserved_header` list is narrow

User cannot set `Host`, `Content-Length`, `Connection`, `TE` (except `trailers`), `Trailer`, `Proxy-Connection`, `Upgrade`, `Expect`, `Proxy-Authorization`, `Accept-Encoding`. `check_user_header` rejects with `HttpInvalidHeader`.

### 13) `check_user_header` rejects pseudo-headers

Headers like `:method`, `:path` cannot be set by the user.

### 14) `trailers` — only with `NGHTTP2_DATA_FLAG_NO_END_STREAM`

Trailers are sent inside `cb_body` when `body_off >= body_len`. Not before.

### 15) `expect_continue` timeout

If `100 Continue` doesn't arrive within `expect_continue_timeout_ms`, **gives up and sends body**. Even if server rejects later, the body has been sent.

### 16) GOAWAY stream id semantics

`GOAWAY last_stream_id` — streams **above** this id are closed with `REFUSED_STREAM` → **not processed**, safe to retry. Below may have been processed — non-idempotent retry dangerous.

### 17) `probe_idle` — pre-reuse byte check

Before reusing an idle session, `read_burst(8)` is called. Catches pending GOAWAY or EOF bytes → retire decision.

**Effect:** Doesn't submit to a stale reused connection.

### 18) `nghttp2_session_check_request_allowed`

Called at the start of `execute` — if session is closed → `ConnectionClosed` + `retire`.

### 19) `wake_event` init fail → 50 ms poll cap

If `init()` fails (rarely), poll timeout is capped at 50 ms. Works correctly, just polls more.

### 20) Session resumption key `h2|host:port`

Cache shared with h3 (`knst_net_detail::tls_sess()`), but prefix `h2|`. h3 session isn't used for h2.

### 21) `knst_http2_client` cookie/jar separate

Each client has its own `knst_cookie_jar_impl`. `cookies()` returns a snapshot (copy under mutex).

### 22) `get_json` empty body → `null`

`r.body.empty()` → `json() = null`, no error. **204 No Content** is normal. Check `r.status`.

### 23) `cancel_grace_ms` — 2 seconds default

If RST_STREAM is queued at end of execute and there's no driver, `flush_locked` tries to send for 2 seconds. Increase for very slow networks, decrease for faster shutdown.

### 24) `nghttp2 >= 1.40`

Older versions may have different `nghttp2_session_mem_send` API. `#error` check exists.

### 25) `nghttp2_submit_trailer` failure → `NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE`

If trailer submit fails inside `cb_body`, the stream is RST'd.

---

## 📊 10) Performance

| Operation | Cost | Note |
|---|---|---|
| **Session create (first)** | 50-200 ms | DNS + TCP + TLS + ALPN |
| **Session create (resume)** | 10-30 ms | TLS ticket |
| **`execute` (multiplexed)** | 1-10 ms | Same session, new stream |
| **`probe_idle`** | 1-5 ms | Non-blocking read |
| **`pull_output`** | O(frames) | Bounded 256 KB |
| **`write_burst`** | O(bytes) | Non-blocking `SSL_write` |
| **`read_burst`** | O(packets) × 16 | `SSL_read` + `mem_recv` |
| **Cookie lookup** | O(cookies) | Path sort |

**Typical latencies:**
- **First HTTP/2 GET**: 100-300 ms (DNS + handshake + 1 RTT)
- **Keep-alive multiplexed GET**: 10-50 ms
- **N parallel streams on same session**: RTTs overlap

**Optimizations:**
- **`o.use_keepalive = true`** (default) — amortizes TLS handshake
- **`o.max_sessions_per_origin`** (default 2) — parallel after saturation
- **`o.max_concurrent_streams`** (default 100) — respect peer limit
- **`o.initial_stream_window` / `connection_window`** — buffer tuning
- **`o.total_timeout_ms`** — slow-drip protection
- **`on_body` for streaming download** — RAM savings

---

## ✅ 11) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Combine total + per-op timeout
knst_web_options o;
o.timeout_ms       = 10000;
o.total_timeout_ms = 30000;

// 2. User-supplied URL → SSRF guard
o.allow_private_networks = false;

// 3. Streaming download
o.on_body      = my_callback;
o.on_body_user = my_user;

// 4. mTLS at top of main
knst_tls_set_client_cert("client.pem", "client.key");

// 5. Expect-Continue for large POST
o.expect_continue = true;
o.expect_continue_timeout_ms = 2000;
c2.post(url, huge_body, o, r, err);

// 6. Request trailer
knst_http_header tr;
tr.name = u"X-Checksum"; tr.value = u"abc123";
o.request_trailers.push_back(tr);

// 7. Auto-fallback including h2
knst_http_auto_client ac;
ac.get(url, o, r, err);
```

### ❌ Don't

```cpp
// 1. http:// URL — NotSupported
c2.get(u"http://example.com", o, r, err);   // ❌

// 2. use_chunked_upload — InvalidArgument
o.use_chunked_upload = true;   // ❌ not for h2

// 3. Re-enter client from on_body
static bool on_chunk(...) {
    c2.get(...);   // ❌ deadlock on driver thread
    return true;
}

// 4. Override Host header
c2.set_header(u"Host", u"other.com");   // ❌ returns false

// 5. Expect cross-origin auth
o.basic_user = u"me";
o.basic_pass = u"pw";
// ❌ if redirect to different host, header dropped

// 6. skip_verify in prod
knst_tls_set_skip_verify(true);   // ❌ remove before prod

// 7. Alt-Svc h2 discovery
// ❌ this header doesn't parse Alt-Svc — h2 negotiated via TLS ALPN anyway

// 8. Rely on `max_per_host`
o.max_per_host = 4;   // ⚠️ ignored — use max_sessions_per_origin
```

---

## 🔥 12) Real-World Examples

### Example 1: REST client (JSON)

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
class ApiClient {
    knst_http2_client c2;
    knst_web_options o;

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
        return c2.get_json(base + u"/items", o, out, last_err);
    }

    knst_network_error last_err = knst_network_error::None;
    knst_c16string base;
};
#endif
```

### Example 2: Large file + progress

```cpp
struct progress_t {
    uint64_t total = 0;
    uint64_t start_ns = 0;
};

static bool on_chunk(const uint8_t* data, size_t len, void* user) {
    auto* p = static_cast<progress_t*>(user);
    p->total += len;
    if (p->total % (1024 * 1024) < len) {
        std::cout << p->total / (1024*1024) << " MB\n";
    }
    return true;
}

knst_http2_client c2;
knst_web_options o;
o.timeout_ms       = 60000;
o.total_timeout_ms = 3600000;
o.on_body          = on_chunk;

progress_t p;
o.on_body_user = &p;

knst_http_response r;
knst_network_error err;
c2.get(u"https://example.com/large.iso", o, r, err);
```

### Example 3: SOCKS5h proxy

```cpp
knst_http2_client c2;
knst_web_options o;
o.timeout_ms       = 15000;
o.total_timeout_ms = 60000;
o.proxy_url        = u"socks5h://127.0.0.1:1080";

knst_http_response r;
knst_network_error err;

if (c2.get(u"https://check.torproject.org/api/ip", o, r, err)) {
    std::cout << r.body.length() << " bytes\n";
}
```

### Example 4: SSRF-safe fetch

```cpp
bool safe_fetch(const knst_c16string& user_url, knst_byte_string& out) {
    knst_url parsed;
    knst_network_error err;
    if (!knst_url::parse(user_url, parsed, err)) return false;
    if (parsed.scheme != u"https") return false;

    knst_http2_client c2;
    knst_web_options o;
    o.timeout_ms       = 5000;
    o.total_timeout_ms = 10000;
    o.max_response_bytes     = 1024 * 1024;
    o.max_decompressed_bytes = 4 * 1024 * 1024;
    o.allow_private_networks = false;
    o.follow_redirects = true;
    o.max_redirects    = 3;

    knst_http_response r;
    if (!c2.get(user_url, o, r, err)) return false;
    if (!r.is_success()) return false;

    out = std::move(r.body);
    return true;
}
```

### Example 5: Multipart upload

```cpp
knst_http_client::multipart_part p1;
p1.name = u"title";
p1.data = /* bytes */;

knst_http_client::multipart_part p2;
p2.name         = u"file";
p2.filename     = u"data.bin";
p2.content_type = u"application/octet-stream";
p2.data         = /* raw bytes */;

knst_vector<knst_http_client::multipart_part> parts;
parts.push_back(p1);
parts.push_back(p2);

knst_byte_string body;
knst_c16string   boundary;
knst_http2_client::build_multipart(parts, body, boundary);

knst_http2_client c2;
knst_web_options o;
o.timeout_ms       = 60000;
o.total_timeout_ms = 300000;
o.default_content_type = knst_http2_client::multipart_content_type(boundary);

knst_http_response r;
knst_network_error err;
c2.post(u"https://upload.example.com/api", body, o, r, err);
```

### Example 6: Request trailers

```cpp
knst_http2_client c2;
knst_web_options o;
o.timeout_ms       = 30000;
o.total_timeout_ms = 60000;

knst_http_header tr;
tr.name = u"X-Checksum";
tr.value = u"sha256:abcd...";
o.request_trailers.push_back(tr);

knst_http_response r;
knst_network_error err;
c2.post(u"https://api.example.com/upload", body, o, r, err);
```

### Example 7: HTTP CONNECT proxy

```cpp
knst_http2_client c2;
knst_web_options o;
o.timeout_ms       = 15000;
o.total_timeout_ms = 60000;
o.proxy_url        = u"http://user:pass@proxy.corp:8080";

knst_http_response r;
knst_network_error err;

if (c2.get(u"https://api.example.com/data", o, r, err)) {
    std::cout << r.status << "\n";
}
// Proxy CONNECT tunnel: TLS handshake over the proxy
```

### Example 8: VPN split routing (bind_local_ip)

```cpp
knst_http2_client c2;
knst_web_options o;
o.timeout_ms       = 10000;
o.total_timeout_ms = 30000;
o.bind_local_ip    = u"10.8.0.2";   // VPN adapter IP

knst_http_response r;
knst_network_error err;

if (c2.get(u"https://internal.corp.local/api", o, r, err)) {
    std::cout << "Via VPN\n";
}
```

---

## 📌 13) API Contract Summary

```cpp
// knst_network_h2.hpp — requires knst_network.hpp + nghttp2 >= 1.40

struct knst_web_header {
    knst_byte_string name;   // lowercase
    knst_byte_string value;
};

struct knst_web_request {
    knst_byte_string method, authority, path;
    knst_vector<knst_web_header> headers, trailers;
    const uint8_t* body;
    size_t         body_len;
};

struct knst_web_options : public knst_http_client::options {
    knst_vector<knst_http_header> request_trailers;
    bool     expect_continue;
    uint32_t expect_continue_timeout_ms;
    uint32_t max_concurrent_streams;
    uint32_t initial_stream_window;
    uint32_t connection_window;
    uint32_t max_sessions_per_origin;
    uint32_t cancel_grace_ms;
};

namespace knst_web_detail {
    typedef knst_net_detail::deadline                deadline_t;
    typedef std::chrono::steady_clock::time_point    time_pt;

    bool io_budget(const deadline_t&, uint32_t& ms, knst_network_error&) noexcept;

    struct proxy_cfg {
        enum class kind_t : uint8_t { None, Http, Socks5 };
        kind_t kind;
        bool   socks5_remote_dns;
        knst_c16string host;
        uint16_t port;
        bool has_auth;
        knst_c16string auth;
        knst_byte_string user_raw, pass_raw;
        bool enabled() const noexcept;
    };

    bool host_in_no_proxy(const knst_c16string&, const knst_c16string&);
    bool resolve_proxy(const knst_url&, const knst_http_client::options&,
                       proxy_cfg&, knst_network_error&) noexcept;
    bool proxy_connect_tunnel(knst_tcp_socket&, const knst_url&, const proxy_cfg&,
                              const knst_http_client::options&, const deadline_t&,
                              knst_network_error&) noexcept;
    bool socks5_handshake(knst_tcp_socket&, const knst_c16string&, uint16_t,
                          const proxy_cfg&, const deadline_t&, knst_network_error&) noexcept;

    bool resolve_endpoints(const knst_c16string&, uint16_t, const knst_http_client::options&,
                           uint32_t, const deadline_t&, bool, knst_vector<knst_endpoint>&,
                           knst_network_error&) noexcept;
    bool literal_target_allowed(const knst_c16string&, knst_network_error&) noexcept;

    class knst_cookie_jar_impl {
    public:
        void clear();
        knst_vector<knst_cookie> snapshot() const;
        knst_c16string header_for(const knst_url&) const;
        void collect(const knst_http_response&, const knst_url&);
    };

    bool is_reserved_header(const knst_byte_string&, const knst_byte_string&) noexcept;
    bool is_sensitive_name(const knst_byte_string&) noexcept;
    bool check_user_header(const knst_http_header&, knst_byte_string&, knst_byte_string&,
                           knst_network_error&);
    bool is_idempotent_method(const knst_byte_string&) noexcept;
    bool is_idempotent_method(const std::string&) noexcept;

    class wake_event {
    public:
        bool init() noexcept;
        void signal() noexcept;
        void drain() noexcept;
        knst_socket_handle_t handle() const noexcept;
        bool ok() const noexcept;
        void close() noexcept;
    };
    void poll_sock_wake(knst_socket_handle_t, bool, knst_socket_handle_t,
                        uint32_t, bool&, bool&, bool&, knst_network_error&) noexcept;

#if defined(KNST_NETWORK_ENABLE_TLS)
    bool host_to_cstr(const knst_c16string&, char*, size_t) noexcept;
    bool tls_prepare_peer(SSL*, const char*) noexcept;
    SSL_CTX* build_ssl_ctx(const unsigned char*, unsigned, bool, bool) noexcept;
    SSL_CTX* tls_ctx_h2() noexcept;
#endif

    struct sigpipe_scope {};
    deadline_t min_deadline(const deadline_t&, const deadline_t&) noexcept;
    time_pt    now_tp() noexcept;
    bool       is_conn_error(knst_network_error) noexcept;
}

typedef knst_web_detail::knst_cookie_jar_impl knst_cookie_jar;

struct knst_web_stream {
    enum class block_t { Error, Interim, Continue100, Final, Trailer };
    // ... (see section 3)
};

class knst_h2_socket : public knst_tcp_socket {
public:
    enum class io_t { Ok, WantRead, WantWrite, Eof, Fail };
    bool start_tls_h2(const knst_c16string&, uint16_t, const deadline_t&, knst_network_error&) noexcept;
    bool has_pending_input() const noexcept override;
    io_t read_some(void*, size_t, size_t&, knst_network_error&) noexcept;
    io_t write_some(const void*, size_t, size_t&, knst_network_error&) noexcept;
    void close() noexcept override;
};

class knst_h2_session {
public:
    static std::shared_ptr<knst_h2_session> create(
        const knst_url&, const knst_web_detail::proxy_cfg&, const knst_web_options&,
        const knst_web_detail::deadline_t&, knst_network_error&) noexcept;

    bool     is_usable() const noexcept;
    bool     try_reserve(uint32_t) noexcept;
    void     force_reserve() noexcept;
    void     release() noexcept;
    uint32_t load() const noexcept;
    knst_web_detail::time_pt idle_since() const noexcept;
    uint64_t requests_started() const noexcept;
    void     retire() noexcept;

    bool execute(const knst_web_request&, const knst_web_options&,
                 const knst_web_detail::deadline_t&, knst_http_response&,
                 knst_web_stream&, knst_network_error&) noexcept;
};

template<class S> class knst_web_session_pool {
public:
    typedef std::shared_ptr<S> ptr;
    template<class F> ptr acquire(const std::string&, uint32_t, uint32_t, uint32_t,
                                  const knst_web_detail::deadline_t&, F&&,
                                  knst_network_error&) noexcept;
    void     clear() noexcept;
    uint32_t size() const noexcept;
};

template<class Derived> class knst_web_client_base {
public:
    typedef knst_web_options                 options;
    typedef knst_http_client::multipart_part multipart_part;
    bool get/post/put/del/head/request(...) noexcept;
    void clear_cookies();
    knst_vector<knst_cookie> cookies() const;
    bool set_header(const knst_c16string&, const knst_c16string&);
    void clear_headers();
#if defined(KNST_NETWORK_ENABLE_JSON)
    bool get_json/post_json(...) noexcept;
#endif
    static void build_multipart(...);
    static knst_c16string multipart_content_type(...);
    static bool parse_multipart(...) noexcept;
protected:
    bool do_request(...) noexcept;
};

class knst_http2_client : public knst_web_client_base<knst_http2_client> {
public:
    void     clear_connection_pool() noexcept;
    uint32_t idle_sessions() const noexcept;
};
```

---

## 🎯 What It Brings You

`knst_network_h2` gives you serious advantages in these situations:

- ✅ **Multiplexing** — one TLS connection, N parallel streams (reduces HOL blocking)
- ✅ **Header compression** — HPACK, bandwidth savings
- ✅ **Fast handshake** — TLS 1.3 + ALPN `h2` (1 RTT)
- ✅ **Session resumption** — ticket cache keyed `h2|host:port`
- ✅ **Session pool** — per-origin, `try_reserve` for saturation
- ✅ **Driver election** — one thread polls, N threads issue requests in parallel
- ✅ **Idle probe** — stale connection reuse guarded by GOAWAY/EOF check
- ✅ **REFUSED_STREAM replay** — safe retry above GOAWAY
- ✅ **Proxy** — HTTP CONNECT + SOCKS5 + SOCKS5h
- ✅ **Cookie jar** — RFC 6265 compliant, path-sorted
- ✅ **SSRF guard** — `allow_private_networks=false` marked in pool key
- ✅ **Expect-Continue** — timeout-deferred body for large POSTs
- ✅ **Request trailers** — checksum, signature, etc.
- ✅ **`knst_network.hpp` compatible** — TLS, DNS, socket infra shared
- ✅ **`knst_network_h3.hpp` compatible** — `knst_web_*` layer shared
- ✅ **KernelNucleusT integration** — `knst_c16string`, `knst_byte_string`, `knst_json`

---