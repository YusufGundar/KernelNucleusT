# knst_network_h3 — HTTP/3 (QUIC) Client

Hey! 👋 `knst_network_h3.hpp` is KernelNucleusT's **HTTP/3 / QUIC** client module. It wraps TLS 1.3 + QUIC + QPACK + HTTP/3 framing over UDP/443 in a single header. It **shares** the `knst_web_*` layer of `knst_network_h2.hpp` — meaning HTTP/2 and HTTP/3 share the same request/response/pool infrastructure. On top of that, `knst_http_auto_client` gives you **h3 → h2 → h1** automatic fallback.

**In short:** This header **will not build with stock OpenSSL 3.0**. It needs a QUIC-capable TLS (`quictls` / OpenSSL QUIC fork) + `ngtcp2 >= 1.0` + `nghttp3 >= 1.1`. CMake `H3=ON` provisions all of those. Only `https://` works — no proxy, no 0-RTT.

---

## 🎯 Honesty Matrix — What Exists, What Doesn't?

### Platform matrix

| Feature | Windows | Linux | Android |
|---|---|---|---|
| **HTTP/3 client** | ✅ | ✅ | ✅ |
| **QUIC (UDP/443)** | ✅ | ✅ | ✅ |
| **TLS 1.3** | ✅ (quictls) | ✅ (quictls) | ✅ (quictls) |
| **0-RTT** | ❌ | ❌ | ❌ |
| **Connection migration** | ❌ | ❌ | ❌ |
| **Server push** | ❌ | ❌ | ❌ |
| **DATAGRAM** | ❌ | ❌ | ❌ |
| **Alt-Svc discovery** | ❌ (prior knowledge) | ❌ | ❌ |
| **HTTP/3 proxy (CONNECT-UDP)** | ❌ `NotSupported` | ❌ | ❌ |
| **`http://` URL** | ❌ `NotSupported` | ❌ | ❌ |
| **`use_chunked_upload`** | ❌ `InvalidArgument` | ❌ | ❌ |
| **`lenient_close_delimited`** | ⚠️ ignored | ⚠️ | ⚠️ |
| **`on_body` streaming** | ✅ (driver thread) | ✅ | ✅ |
| **mTLS** | ✅ | ✅ | ✅ |
| **Session resumption** | ✅ (`h3\|host:port`) | ✅ | ✅ |
| **SSRF guard** | ✅ | ✅ | ✅ |
| **Auto fallback (h3→h2→h1)** | ✅ | ✅ | ✅ |

### Library dependencies — the most critical part

| Dependency | Why it's needed | Version |
|---|---|---|
| **`knst_network_h2.hpp`** | `knst_web_*` layer (stream, pool, options) | internal |
| **`KNST_NETWORK_ENABLE_TLS`** | TLS must be defined, else `#error` | define |
| **ngtcp2** | QUIC protocol engine | **>= 1.0** |
| **ngtcp2_crypto_quictls** | QUIC + TLS integration layer | >= 1.0 |
| **nghttp3** | HTTP/3 framing + QPACK | **>= 1.1** |
| **quictls / OpenSSL QUIC fork** | TLS to perform the QUIC handshake | 3.1+quic recommended |
| **nghttp2** (transitive) | via `knst_network_h2.hpp` | >= 1.58 (CMake) |

**Critical warnings:**
- **Stock OpenSSL 3.0 (Ubuntu 22.04 / Pop!_OS) is not enough.** `quictls` is required.
- In CMake you must point `KNST_QUICTLS_ROOT=/path/to/quictls`.
- On Android CMake fetches it via **FetchContent** (10-15 min on first build).
- **`apple` (macOS)** — KernelNucleusT does not support it (`FATAL_ERROR` in CMake).

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **One QUIC connection, many streams** | N requests to same origin → 1 UDP socket |
| **QPACK** | Header compression (4096 dynamic table, 100 blocked streams) |
| **Session resumption** | Ticket cache keyed on `h3\|host:port` |
| **PMTUD** | IPv4/IPv6 path MTU discovery |
| **Multiplexing** | Per-stream deadline, RST_STREAM, STOP_SENDING |
| **GOAWAY handling** | Unprocessed streams safely retryable |
| **`knst_http_auto_client`** | h3 → h2 → h1 fallback, per-origin 5-min backoff |
| **Per-origin backoff** | On UDP-blocked networks: 1 timeout, not N |
| **Replay guard** | Non-idempotent / streaming: connect-phase fallback only |
| **Pool interface** | Same contract as `knst_h2_session` — `try_reserve`/`release` |
| **Driver election** | One thread polls, others wait on mutex |
| **Soft net errors** | `ConnectionRefused` transient before handshake, fatal after |

---

## 🚀 Quick Start

### Simple HTTP/3 GET

```cpp
#include "KernelNucleusT.hpp"

knst_http3_client c3;
knst_web_options o;
o.timeout_ms       = 5000;
o.total_timeout_ms = 10000;

knst_http_response r;
knst_network_error err;

if (c3.get(u"https://cloudflare.com/cdn-cgi/trace", o, r, err)) {
    std::cout << "Status: " << r.status << "\n";
    std::cout << "Body:   " << r.body.length() << " bytes\n";
} else {
    std::cerr << "Error: " << knst_network_error_string(err) << "\n";
}
```

### Auto fallback client

```cpp
knst_http_auto_client ac;   // h3 → h2 → h1 automatic

knst_web_options o;
o.timeout_ms       = 3000;     // h3 might have blocked UDP, fail fast
o.total_timeout_ms = 15000;

knst_http_response r;
knst_network_error err;

if (ac.get(u"https://api.example.com/data", o, r, err)) {
    std::cout << r.status << " via "
              << (ac.h3().idle_sessions() ? "maybe h3" : "fallback")
              << "\n";
}
```

### Streaming download

```cpp
static bool on_chunk(const uint8_t* data, size_t len, void* user) {
    auto* f = static_cast<FILE*>(user);
    return std::fwrite(data, 1, len, f) == len;
}

FILE* f = std::fopen("/tmp/big.iso", "wb");

knst_http3_client c3;
knst_web_options o;
o.timeout_ms       = 60000;
o.total_timeout_ms = 600000;
o.on_body          = on_chunk;
o.on_body_user     = f;

knst_http_response r;
knst_network_error err;
c3.get(u"https://example.com/big.iso", o, r, err);

std::fclose(f);
```

### h2/h3 comparison

```cpp
// Manual control — no fallback
knst_http3_client c3;
if (!c3.get(url, o, r, err)) {
    // h3 failed, switch to h2
    knst_http2_client c2;
    c2.get(url, o, r, err);
}

// Automatic — if h3 fails, h2; if that fails too, h1
knst_http_auto_client ac;
ac.get(url, o, r, err);
```

---

## 📋 General API

| Category | Class / Function |
|---|---|
| **QUIC/UDP layer** | `knst_h3_detail::udp_conn`, `knst_h3_detail::tls_ctx_h3()` |
| **QUIC session** | `knst_h3_session` |
| **HTTP/3 client** | `knst_http3_client` |
| **Auto fallback client** | `knst_http_auto_client` |
| **H3 error codes** | `H3_NO_ERROR`, `H3_INTERNAL_ERROR`, `H3_REQUEST_REJECTED`, `H3_REQUEST_CANCELLED` |

---

## 🔌 1) `knst_h3_detail` — Internal Layer

### 1.1) `udp_conn` — Non-blocking connected UDP socket

```cpp
class udp_conn {
public:
    enum class r_t { Ok, Again, Fail };

    bool open(const knst_endpoint& peer, const knst_ip_address* bind_ip,
              knst_network_error& err) noexcept;

    r_t send(const uint8_t* p, size_t n, knst_network_error& e) noexcept;
    r_t recv(uint8_t* p, size_t cap, size_t& n, knst_network_error& e) noexcept;

    bool is_open() const noexcept;
    knst_socket_handle_t handle() const noexcept;
    const sockaddr_storage& local()  const noexcept;
    const sockaddr_storage& remote() const noexcept;
    void close() noexcept;
};
```

**What does `open` do?**
1. Opens `AF_INET`/`AF_INET6` + `SOCK_DGRAM` socket
2. `SO_RCVBUF = 2 MB`, `SO_SNDBUF = 1 MB` — for QUIC windows
3. **PMTUD on:** IPv4 uses `IP_PMTUDISC_DO`, IPv6 uses `IPV6_PMTUDISC_DO`
4. Optional `bind_ip` pins the source IP
5. `connect()` — binds to peer (for kernel routing/ICMP errors)
6. `getsockname()` — local addr/port

**`send` behavior:**
- `MessageTooLarge` → returns **`r_t::Ok`** (packet dropped, PMTUD will handle). Silent drop.
- `WouldBlock` → `Again`
- Other errors → `Fail` + error

**`recv` behavior:**
- `MessageTooLarge` → `r_t::Ok` + `n = 0` (truncated datagram dropped)
- ICMP-derived errors → returned as `Fail`, but **not fatal after handshake** (spoofable/transient)

### 1.2) `tls_ctx_h3()` — Process-wide SSL_CTX

```cpp
inline SSL_CTX* tls_ctx_h3() noexcept;
```

**What it does?** Built once via `once_flag`:
- `knst_web_detail::build_ssl_ctx(alpn="h3", sizeof, true /*tls13_only*/, false /*not h2*/)`
- `ngtcp2_crypto_quictls_configure_client_context()` — installs the QUIC TLS method
- Session cache client-side (`SSL_SESS_CACHE_NO_INTERNAL_STORE`)
- `knst_net_detail::tls_new_session_cb` — session ticket cache

**Note:** `tls_new_session_cb` comes from `knst_network.hpp` — shared with h2.

### 1.3) H3 error codes

```cpp
static constexpr uint64_t H3_NO_ERROR          = 0x100;
static constexpr uint64_t H3_INTERNAL_ERROR    = 0x102;
static constexpr uint64_t H3_REQUEST_REJECTED  = 0x10b;   // safe to retry
static constexpr uint64_t H3_REQUEST_CANCELLED = 0x10c;
```

`H3_REQUEST_REJECTED` is special: the request was **not processed by the server** → **retry is safe** for any HTTP method.

### 1.4) `soft_net_error` — ICMP-based errors

```cpp
inline bool soft_net_error(knst_network_error e) noexcept;
```

If `ConnectionRefused`, `HostUnreachable`, `NetworkUnreachable`, `ConnectionReset` are returned **during the handshake**, they are not fatal — UDP ICMP is spoofable. After handshake completes, they are fatal.

### 1.5) Time helpers

```cpp
inline uint64_t now_ns() noexcept;              // steady_clock nanoseconds
inline time_pt  tp_from_ns(uint64_t ns) noexcept;   // nanoseconds → time_point
```

---

## 🧠 2) `knst_h3_session` — QUIC Connection

Represents one QUIC connection. N requests to the same origin are multiplexed over different streams on this session.

### 2.1) Factory + pool interface

```cpp
class knst_h3_session {
public:
    static std::shared_ptr<knst_h3_session> create(
        const knst_url& u,
        const knst_web_detail::proxy_cfg& pc,
        const knst_web_options& o,
        const deadline_t& total,
        knst_network_error& err) noexcept;

    // Pool interface — same contract as knst_h2_session
    bool     is_usable() const noexcept;
    bool     try_reserve(uint32_t local_cap) noexcept;
    void     force_reserve() noexcept;
    void     release() noexcept;
    uint32_t load() const noexcept;
    time_pt  idle_since() const noexcept;
    uint64_t requests_started() const noexcept;
    void     retire() noexcept;
};
```

**What does `create` do?**
1. **Proxy check** — `pc.enabled()` → `NotSupported`
2. **Merge deadlines** — minimum of `o.timeout_ms` and `total`
3. **bind_local_ip parse** — if present, `knst_ip_address::from_string`
4. **DNS resolve** — `knst_web_detail::resolve_endpoints`
5. **`attempt()` for each endpoint** — the one that succeeds wins
6. **`TlsCertificateInvalid`** → **doesn't try other addresses** (hostname check fails for all)

### 2.2) `execute` — Submit request + block

```cpp
bool execute(
    const knst_web_request& r,
    const knst_web_options& o,
    const deadline_t& total,
    knst_http_response& out,
    knst_web_stream& st,
    knst_network_error& err) noexcept;
```

**What it does?**
1. **Slot guard** — calls `release()` on return (pool slot freed)
2. **Stream setup** — `st.setup(&out, r, o, total, o.timeout_ms, u"HTTP/3")`
3. **Expect-Continue** — if `r.body_len > 0 && o.expect_continue` → `continue_ok = false`
4. **Add to pending queue** — `m_pending.push_back(h)` + `m_requests++`
5. **If no driver running** → `m_wake.signal()` + become the driver yourself
6. **`run(st, lk)`** — blocking poll loop until `st.finished()`
7. **On completion** — cleanup, `ngtcp2_conn_shutdown_stream` for RST_STREAM if not `resp_end`
8. **Copy `st.err` into `err`**

**Critical note:** `h->st = nullptr; h->req = nullptr;` — callbacks become no-ops afterwards. But the ctx stays in `m_streams` until `stream_close`.

### 2.3) Locking model — very important

```
m_mtx guards:
  - ngtcp2_conn*
  - nghttp3_conn*
  - m_streams (stream_id → ctx)
  - m_pending (not-yet-submitted)
  - all stream state (st->err, st->got_final, ...)

Callbacks:
  - Invoked WITH m_mtx HELD, inside ngtcp2/nghttp3 calls
  - So callbacks NEVER take locks (no deadlock)

Driver:
  - Marked by m_driving flag
  - One thread polls
  - Releases m_mtx only around poll()
  - On poll return → re-acquires m_mtx

Sleeping threads:
  - m_cv.wait_until(lk, expiry or 1s)
  - Woken up when driver finishes or stream finishes
```

**Result:** **N threads can issue parallel requests** on a single session — only one becomes the driver, others wait on `m_cv`.

### 2.4) QUIC handshake flow

```
attempt(ep, ...):
  1. udp_conn.open(ep)                    # UDP/443 (or whatever port)
  2. init_quic(u, o)                      # ngtcp2_conn_client_new
  3. m_driving = true                     # this thread is driver
  4. loop until (m_hs_done && m_http):
     a. step(lk, dl.end)                  # one event loop iteration
        - ngtcp2_conn_get_expiry → handle_expiry (timeout/fatal)
        - scan() → per-stream deadline check
        - write_packets() → outbound UDP
        - poll_sock_wake() → wait until readable (m_mtx released)
        - read_packets() → inbound UDP
  5. m_driving = false

init_quic does:
  - SSL_new(ctx_h3)
  - tls_prepare_peer → SNI/hostname verify
  - SSL_set_session (from cache)
  - ngtcp2_conn_client_new(tls_native_handle = ssl)
```

### 2.5) `setup_http()` — when 1-RTT key arrives

```cpp
// recv_rx_key callback (NGTCP2_ENCRYPTION_LEVEL_1RTT):
//   → setup_http() is called
//   → nghttp3_conn_client_new + 3 unidirectional streams:
//       - control stream (bind_control_stream)
//       - QPACK encoder stream (bind_qpack_streams)
//       - QPACK decoder stream (bind_qpack_streams)
```

**Critical check:** If `ngtcp2_conn_get_streams_uni_left(m_conn) < 3` → `ProtocolError` — the server's QUIC params are too restrictive.

### 2.6) HTTP/3 stream submit flow

```cpp
// inside open_pending():
int64_t sid = -1;
ngtcp2_conn_open_bidi_stream(m_conn, &sid, nullptr);

// Pseudo-headers
:method    = r.method
:scheme    = "https"            # hard-coded! no http://
:authority = r.authority
:path      = r.path

// Custom headers
for h in r.headers:
    nva.push_back({h.name, h.value})

// data_reader (if body present)
nghttp3_data_reader dr;
dr.read_data = &knst_h3_session::h_read_body;

nghttp3_conn_submit_request(m_http, sid, nva, dr);
```

**`h_read_body`** — called by nghttp3, reads business logic **from the caller's buffer** (zero-copy):
- `st->expect && !st->continue_ok` → `NGHTTP3_ERR_WOULDBLOCK` (waiting for Expect-Continue)
- Body → `vec[0].base = const_cast<uint8_t*>(st->body + st->body_off)`
- `body_len` fully consumed → `NGHTTP3_DATA_FLAG_EOF`
- Trailers present → `NGHTTP3_DATA_FLAG_NO_END_STREAM` + `nghttp3_conn_submit_trailers`

### 2.7) `scan()` — per-stream deadline watchdog

```cpp
time_pt scan(time_pt now) noexcept;
```

Called every step:
- **`st->err`** set → `cancel_locked(h, H3_REQUEST_CANCELLED)`
- **Expect deadline** expired → `continue_ok = true` (retry deferred stream)
- **`st->expiry()`** expired → `st->fail(Timeout)` + cancel
- Same check for pending-queue streams
- Return: nearest deadline (used by `step()` as poll timeout)

### 2.8) `step()` — one event loop iteration

```cpp
void step(std::unique_lock<std::mutex>& lk, time_pt limit) noexcept;
```

In order:
1. **`ngtcp2_conn_get_expiry`** → if expired, `ngtcp2_conn_handle_expiry` (timeout → `Timeout`/`ConnectionClosed`)
2. **`scan(now)`** → stream deadline check
3. **`write_packets(ts)`** → outbound, returns `more`
4. **Compute next deadline:** scan + limit + conn expiry
5. **If `more` is false:**
   - `poll_sock_wake(sock, write_blocked, wake_handle, timeout_ms, s_read, sw, woke, pe)`
   - **`m_mtx` released** (during poll)
   - If `pe` set → fatal
6. **If `s_read`** → `read_packets()`
7. `m_cv.notify_all()` — notify stream finishes

### 2.9) `run()` — driver election

```cpp
void run(knst_web_stream& st, std::unique_lock<std::mutex>& lk) noexcept;
```

```
Loop:
  if st.finished() → return
  if m_fatal → st.fail(m_fatal_err)
  if !m_driving:
      m_driving = true
      while !st.finished && !m_fatal:
          step(lk, st.expiry())
      m_driving = false
      m_cv.notify_all()   # hand off to another waiter
      continue
  # I'm not the driver, another thread is driving
  m_cv.wait_until(lk, min(st.expiry, now+1s))
```

**Why this model?** One QUIC connection → one UDP socket → one poll. Multiplexing requires driver election.

### 2.10) `shutdown_()` — destructor

```cpp
~knst_h3_session() noexcept { shutdown_(); }
```

1. `ngtcp2_conn_write_connection_close` → CONNECTION_CLOSE frame (H3_NO_ERROR)
2. Send via UDP
3. `nghttp3_conn_del` + `ngtcp2_conn_del` + `SSL_free`
4. UDP close

**Fails silently** — no exceptions from destructors.

---

## 🌐 3) `knst_http3_client` — Public API

Inherits `knst_web_client_base<knst_http3_client>` — so `get`, `post`, `put`, `del`, `head`, `request`, `get_json`, `post_json`, `cookies`, `set_header`, `clear_headers` are **all provided** (same as h2).

```cpp
class knst_http3_client : public knst_web_client_base<knst_http3_client> {
public:
    void     clear_connection_pool() noexcept;
    uint32_t idle_sessions() const noexcept;   // live pooled QUIC connections

private:
    static std::string make_key(const knst_url& u, const knst_web_options& o);
    bool exchange(...);
    knst_web_session_pool<knst_h3_session> m_pool;
};
```

### 3.1) `make_key` — pool key

```
"h3|{O|S}|host:port[|b:bind_ip]"
```

- `O` = `allow_private_networks=true` (unguarded pool)
- `S` = `allow_private_networks=false` (guarded pool)

**Why separate?** Guarded and unguarded connections must not mix — protects against DNS rebinding SSRF.

### 3.2) `exchange` — request + retry logic

```cpp
bool exchange(const knst_url& u, const knst_web_detail::proxy_cfg& pc,
              const knst_web_options& o, const knst_web_request& r,
              knst_http_response& out,
              const knst_web_detail::deadline_t& total,
              knst_network_error& err) noexcept;
```

**Flow:**
1. **If proxy configured** → `NotSupported` (HTTP/3 needs CONNECT-UDP)
2. **Acquire from pool** — if none, `create()` a new one
3. **`s->execute(r, o, total, out, st, err)`** — blocking
4. **`!o.use_keepalive`** → `s->retire()` (won't return to pool)
5. **On failure, retry decision:**
   - `st.refused` (GOAWAY or H3_REQUEST_REJECTED) → **safe retry for any method**
   - `!st.response_started && reused && idem && is_conn_error(err)` → **stale reused connection**, retry for idempotent methods
   - Otherwise → **fail**

**Max 3 attempts.**

### 3.3) What it doesn't do

- **Doesn't fall back to h2/h1 when h3 fails.** This client is h3-only. For fallback use `knst_http_auto_client`.
- **`http://` URL** → `NotSupported` (`:scheme` hard-coded "https")
- **`use_chunked_upload`** → `InvalidArgument` (H3 is framed; no CL/chunked)

---

## 🎛️ 4) `knst_http_auto_client` — Automatic Fallback

Tries h3 → h2 → h1 in order. **Skips failing origins for a period** (default 5 min).

```cpp
class knst_http_auto_client {
public:
    typedef knst_web_options options;
    typedef knst_http_client::multipart_part multipart_part;

    void enable_h3(bool v) noexcept;
    void enable_h2(bool v) noexcept;
    void set_h3_backoff(uint32_t ms) noexcept;

    knst_http_client&  h1() noexcept;
    knst_http2_client& h2() noexcept;
    knst_http3_client& h3() noexcept;

    // HTTP methods
    bool get/post/put/del/head/request(...) noexcept;

    void clear_cookies();
    void clear_connection_pool() noexcept;

#if defined(KNST_NETWORK_ENABLE_JSON)
    bool get_json/post_json(...) noexcept;
#endif
};
```

### 4.1) Decision tree

```
run(method, url, body, o, out, err):

  1. Parse URL → is it https?
     - NO, or use_chunked_upload → h1 (direct)
  2. strict = !idempotent(method) || o.on_body != nullptr
  3. proxied = o.proxy_url || o.use_env_proxy

  4. if m_h3 && !proxied && !recently_failed(origin):
        if h3.request(...) → OK
        fb = strict ? connect_phase(err) : replayable(err)
        if !fb → return false  (real error: redirect loop, size limit, etc.)
        mark_failed(origin)
        reset err/out

  5. if m_h2:
        if h2.request(...) → OK
        fb = (err == NotSupported || err == TlsNotSupported)
        if !fb → return false
        reset err/out

  6. h1.request(...) → OK or error
```

### 4.2) `connect_phase` vs `replayable`

**`connect_phase(e)`** — the request **definitely did not reach the server**:
- `ConnectionRefused`, `HostUnreachable`, `NetworkUnreachable`, `NetworkDown`
- `TlsHandshakeFailed`, `NotSupported`, `TlsNotSupported`

**`replayable(e)`** — the request **can safely be retried** (idempotent only):
- `connect_phase` + `Timeout`, `ConnectionClosed`, `HttpConnectionClosed`, `ConnectionReset`, `ConnectionAborted`, `ProtocolError`, `BrokenPipe`

**Why this split?**
- **Non-idempotent POST** → only `connect_phase` (if the request reached the server, **don't resend**)
- **Streaming `on_body`** → only `connect_phase`, so the callback isn't invoked twice
- **Idempotent GET/HEAD/PUT/DELETE** → `replayable` (retry after timeout/close)

### 4.3) Backoff mechanism

```cpp
std::unordered_map<std::string, knst_web_detail::time_pt> m_bad;
std::mutex m_mtx;
uint32_t m_backoff_ms = 300000;   // 5 minutes
```

- **Key:** `host:port` (string)
- **Value:** `now + backoff_ms` (expiry time)
- **`recently_failed(key)`** → true if `now < expiry`
- **`mark_failed(key)`** → `m_bad[key] = now + backoff_ms`
- **Cap:** 512 entries (cleared when full)

**Scenario:** UDP-blocked network (corporate firewall). First request:
1. h3 → 3s timeout
2. `connect_phase(Timeout)` → **false** (Timeout is not in connect_phase, it's replayable)
3. `strict = !idempotent(POST)` → **true**, so it checks `connect_phase(err)` → **false**
4. **POST returns failure**. A GET would have passed through `replayable` instead.

For the next 5 minutes h3 isn't tried for that origin → h2/h1 direct.

### 4.4) `strict` semantics

| Case | `strict` | Fallback condition |
|---|---|---|
| **Idempotent GET** | false | `replayable(err)` |
| **Non-idempotent POST** | true | `connect_phase(err)` |
| **Streaming `on_body`** | true | `connect_phase(err)` |

**Result:** POST + timeout → **no fallback, error returned**. But POST + connection refused → **falls back to h1**.

### 4.5) Each protocol has its **own** state

```cpp
knst_http_client   m_c1;
knst_http2_client  m_c2;
knst_http3_client  m_c3;
```

- **Cookie jars separate** — a cookie obtained on h3 isn't sent to h1 (browser-like)
- **Connection pools separate**
- **Headers separate** — `h3().set_header()` affects only h3

**Usage:** `ac.h1().set_header(...)`, `ac.h2().cookies()`, `ac.h3().idle_sessions()` — direct access.

---

## 🔄 5) `knst_web_stream` — Shared Layer (from h2)

This header doesn't define `knst_web_stream` directly — it uses the shared layer from h2. The parts used here:

```cpp
struct knst_web_stream {
    // Request
    const uint8_t* body = nullptr;
    size_t         body_len = 0;
    size_t         body_off = 0;
    bool           body_done = false;
    knst_vector<knst_http_header> trailers;

    // Expect-Continue
    bool     expect = false;
    bool     continue_ok = true;
    time_pt  expect_deadline;
    bool     deferred = false;

    // Response
    knst_http_response* out = nullptr;
    int64_t stream_id = -1;
    bool    got_final = false;
    bool    resp_end = false;
    bool    discard = false;
    bool    response_started = false;
    bool    closed = false;
    bool    rst_sent = false;
    bool    refused = false;
    bool    finished() const noexcept;

    // Error
    knst_network_error err = knst_network_error::None;

    // Methods
    bool setup(knst_http_response* out, const knst_web_request&,
               const knst_web_options&, const deadline_t&,
               uint32_t io_ms, const char16_t* proto_name);
    void touch();
    void begin_block();
    void add_header(const uint8_t* n, size_t nlen, const uint8_t* v, size_t vlen);
    block_t end_block(knst_network_error&);
    void on_data(const uint8_t* d, size_t n, knst_network_error&);
    void on_close(knst_network_error, bool refused);
    void fail(knst_network_error);
    void finalize();
    time_pt expiry() const;
};
```

**`block_t` values:**
- `Continue100` — `100 Continue` received, send body
- `Interim` — `1xx` (other than 100)
- `Final` — `2xx`/`3xx`/`4xx`/`5xx` — headers done
- `Trailer` — trailer headers
- `Error` — `st->err` set

---

## 🔍 6) Callback Chain — Detailed

### 6.1) ngtcp2 callbacks

| Callback | What it does |
|---|---|
| **`client_initial`** | For the QUIC Initial packet (ngtcp2 crypto) |
| **`recv_crypto_data`** | TLS handshake bytes (ngtcp2 crypto → SSL) |
| **`encrypt` / `decrypt`** | QUIC packet encrypt/decrypt (ngtcp2 crypto) |
| **`hp_mask`** | Header protection mask (ngtcp2 crypto) |
| **`recv_retry`** | Retry packet handling (ngtcp2 crypto) |
| **`update_key`** | Key rotation (ngtcp2 crypto) |
| **`handshake_completed`** | `m_hs_done = true` (ours) |
| **`recv_rx_key`** | 1-RTT key available → call `setup_http()` |
| **`recv_stream_data`** | QUIC stream data → `nghttp3_conn_read_stream` |
| **`acked_stream_data_offset`** | ACK → `nghttp3_conn_add_ack_offset` |
| **`stream_close`** | Stream closed → `nghttp3_conn_close_stream` |
| **`stream_reset`** | RST_STREAM → `nghttp3_conn_shutdown_stream_read` |
| **`stream_stop_sending`** | STOP_SENDING → `nghttp3_conn_shutdown_stream_read` |
| **`extend_max_local_streams_bidi`** | Server MAX_STREAMS bumped → no-op (pending retried each step) |
| **`extend_max_stream_data`** | Stream window bumped → `nghttp3_conn_unblock_stream` |
| **`rand`** | `RAND_bytes` |
| **`get_new_connection_id`** | New CID (RAND + reset token) |

### 6.2) nghttp3 callbacks

| Callback | What it does |
|---|---|
| **`acked_stream_data`** | no-op (informational) |
| **`stream_close`** | H3 error code → `st->on_close()` |
| **`recv_data`** | Body chunk → `st->on_data()` |
| **`deferred_consume`** | `ngtcp2_conn_extend_max_stream_offset` |
| **`begin_headers`** | `st->begin_block()` |
| **`recv_header`** | `st->add_header()` |
| **`end_headers`** | `st->end_block()` → if `Continue100`, `resume_stream` |
| **`begin_trailers` / `recv_trailer` / `end_trailers`** | Same handlers |
| **`end_stream`** | `st->resp_end = true` |
| **`stop_sending`** | `ngtcp2_conn_shutdown_stream_read` |
| **`reset_stream`** | `ngtcp2_conn_shutdown_stream_write` |
| **`shutdown`** | **GOAWAY** — stream id >= `id` → `st->refused = true` + `st->fail(ConnectionAborted)` |

**GOAWAY semantics:** Streams above `id` **were not processed** → safe to retry. Streams below `id` **were processed** (maybe no response but the server saw them) → retry is **dangerous** for non-idempotent requests.

---

## ⚠️ 7) Common Gotchas

### 1) Won't build with stock OpenSSL 3.0

```
/usr/include/openssl/ssl.h:  stock
ngtcp2_crypto_quictls:        requires QUIC-capable SSL
```

**Fix:** `-DKNST_QUICTLS_ROOT=/path/to/quictls` or Android's automatic FetchContent.

### 2) `http://` URL → `NotSupported`

`:scheme` pseudo-header is **hard-coded `https`**. HTTP/3 requires encryption (QUIC over TLS 1.3). Sending `http://` will make `knst_http3_client` return an error.

### 3) Proxy → `NotSupported`

HTTP proxy doesn't support CONNECT-UDP. SOCKS5 UDP ASSOCIATE also unsupported (not in this header). **A separate implementation is needed for UDP-over-proxy.**

### 4) `use_chunked_upload = true` → `InvalidArgument`

HTTP/3 has no Transfer-Encoding. Body framing uses DATA frames. `use_chunked_upload` is h1-only.

### 5) `lenient_close_delimited` **ignored**

H3 is a framed protocol — connection close doesn't delimit the body. `st->resp_end` is driven by DATA frames.

### 6) `on_body` runs on the **driver thread**

Whichever thread called `run()` and became driver will run `on_body`. **That may not be your thread!** Don't re-enter the client — deadlock.

### 7) No Alt-Svc discovery

The `Alt-Svc: h3=":443"` header is **not parsed**. Direct UDP/443 attempt. If an H2 server supports h3 but doesn't send Alt-Svc, this client **doesn't know**.

**Result:** You must know in advance that an origin speaks h3. Or use `knst_http_auto_client` and let backoff handle it on failure.

### 8) `ngtcp2 >= 1.26` difference

```cpp
// Comment inside execute():
// (idle-expiry check removed — ngtcp2 >= 1.26 no longer exposes
//  ngtcp2_conn_get_idle_expiry; QUIC idle timeout is handled by
//  ngtcp2 internally via ngtcp2_conn_get_expiry + handle_expiry)
```

**Meaning:** Older ngtcp2 had `get_idle_expiry`; newer versions removed it. Code supports both — via `get_expiry` + `handle_expiry`.

### 9) Pre-handshake ICMP errors are **soft**

```cpp
// soft_net_error:
ConnectionRefused, HostUnreachable, NetworkUnreachable, ConnectionReset
```

If handshake **not completed**, these are **not fatal** — spoofable ICMP. If completed, fatal.

### 10) Post-handshake `ConnectionRefused` is fatal

Why? UDP socket is now connected; ICMP port-unreachable is **real**. Peer is dead. Session dies.

### 11) `MAX_STREAMS` blocked

```cpp
rv = ngtcp2_conn_open_bidi_stream(m_conn, &sid, nullptr);
if (rv == NGTCP2_ERR_STREAM_ID_BLOCKED) break;   // wait for MAX_STREAMS
```

`extend_max_local_streams_bidi` callback is **no-op** — pending retried each step. Blocked streams wait, retried next iteration.

### 12) `ngtcp2_conn_get_streams_bidi_left` — peer limit

After handshake:
```cpp
uint64_t left = ngtcp2_conn_get_streams_bidi_left(m_conn);
self->m_peer_max.store(left == 0 ? 1u : (left > 1000 ? 1000u : static_cast<uint32_t>(left)));
```

`m_peer_max` = pool `try_reserve` upper bound. Peer said 100 streams → 100 concurrent requests max.

### 13) Session resumption key `h3|host:port`

TLS session ticket cache is **shared** with h2 (`knst_net_detail::tls_sess()`), but the key has prefix `h3|`. So an h2 session isn't used for h3.

### 14) `native_app_glue`-like — Android NDK

When building `ngtcp2` on Android, `-DNGTCP2_STATICLIB` etc. defines are needed. CMake handles it (`H3=ON`).

### 15) `m_wblocked` — kernel UDP buffer full

```cpp
if (r == udp_t::r_t::Again) {
    m_pend.assign(m_wbuf.begin(), m_wbuf.begin() + nw);
    m_wblocked = true;
    break;
}
```

`m_pend` is **driver-only**. Next `write_packets()` flushes `m_pend` first. `poll()` **waits for POLLOUT** when `m_wblocked`.

### 16) `flush_locked` — best-effort RST send

```cpp
// At end of execute():
if (need_flush) {
    if (m_driving) m_wake.signal(); else flush_locked(o.cancel_grace_ms);
}
```

If no one is driver but an RST_STREAM is queued — `flush_locked` **becomes the driver itself**, best-effort send. Max 4 iterations + grace_ms.

### 17) `m_retired` — don't return to pool

```cpp
if (!o.use_keepalive) s->retire();
```

`use_keepalive=false` → `m_retired=true`. `is_usable()` returns false. Session removed from pool; next request opens new.

### 18) `h3_backoff` per-origin but **global**

`m_bad` map is shared. Thread A starts backoff for origin X → thread B skips origin X too. **Not request-specific.**

### 19) `strict` = POST + on_body

POST + `on_body` set → `strict` is true (either one). In this case, **no fallback** on timeout.

### 20) `knst_http_auto_client` **no cookie sharing**

If you get `Set-Cookie` on h3, then fall back to h1, that cookie **isn't sent**. Each protocol has its own jar. This matches **browser behavior** (per-origin).

**If needed:** `ac.h1().set_header(u"Cookie", ...)` manually.

---

## 📊 8) Performance

| Operation | Cost | Note |
|---|---|---|
| **Session create (first)** | 50-200 ms | DNS + UDP + QUIC handshake |
| **Session create (resume)** | 10-30 ms | TLS ticket + no 0-RTT but fast 1-RTT |
| **`execute` (multiplexed)** | 1-10 ms | Same session, new stream |
| **Stream submit** | O(headers) | QPACK encode |
| **Response parse** | O(headers + body) | nghttp3 |
| **`write_packets`** | O(packets) | Max 64 per iteration |
| **`read_packets`** | O(packets) | Max 64 per iteration |
| **`scan`** | O(streams) | Every step |
| **UDP RTT** | 1-100 ms | Network-dependent |

**Typical latencies:**
- **First HTTP/3 GET**: 100-500 ms (DNS + handshake + 1 RTT)
- **Keep-alive multiplexed GET**: 10-50 ms
- **N parallel streams on same session**: RTTs overlap

**Optimizations:**
- **`o.use_keepalive = true`** (default) — amortizes QUIC handshake cost
- **`o.max_sessions_per_origin`** — multiple QUIC connections in parallel
- **`o.max_concurrent_streams`** — stay under peer limit
- **`o.connection_window`** and **`o.initial_stream_window`** — buffer tuning
- **`o.total_timeout_ms`** — slow-drip protection

---

## ✅ 9) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Use auto_client (with fallback)
knst_http_auto_client ac;
ac.get(u"https://api.example.com/data", o, r, err);

// 2. Aggressive timeout for h3 (UDP might be blocked)
o.timeout_ms = 3000;            // h3 fails fast
o.total_timeout_ms = 15000;

// 3. FILE* user for streaming
o.on_body      = on_chunk;
o.on_body_user = file_ptr;

// 4. mTLS at top of main
knst_tls_set_client_cert("client.pem", "client.key");

// 5. Each protocol's state is separate — manual control
ac.h2().set_header(u"X-Test", u"1");   // h2 only
ac.h1().set_header(u"X-Test", u"1");   // h1 only

// 6. Extend backoff (corporate network blocks UDP)
ac.set_h3_backoff(600000);   // 10 min

// 7. Debug via idle_sessions()
std::cout << "h3 sessions: " << ac.h3().idle_sessions() << "\n";
```

### ❌ Don't

```cpp
// 1. http:// URL — NotSupported
c3.get(u"http://example.com", o, r, err);   // ❌

// 2. use_chunked_upload — InvalidArgument
o.use_chunked_upload = true;   // ❌ not for h3
c3.post(url, body, o, r, err);

// 3. h3 with proxy — NotSupported
o.proxy_url = u"http://proxy:8080";   // ❌ h3 doesn't support CONNECT-UDP
ac.get(url, o, r, err);   // auto_client skips h3, uses h2/h1

// 4. Re-enter client from on_body
static bool on_chunk(...) {
    c3.get(...);   // ❌ deadlock on driver thread
    return true;
}

// 5. Build with stock OpenSSL 3.0
// ❌ ngtcp2_crypto_quictls include fails
// ✅ use quictls

// 6. Strict POST + timeout
o.timeout_ms = 1000;   // ❌ POST timeout → no fallback, returns error
ac.post(url, body, o, r, err);

// 7. Wait for Alt-Svc
// ❌ Alt-Svc: h3 header is NOT parsed
// ✅ Either prior knowledge or auto_client

// 8. Manage the session manually
knst_h3_session::create(...);   // outside pool
// ✅ Use knst_http3_client, pool manages it
```

---

## 🔥 10) Real-world examples

### Example 1: Simple h3 fallback

```cpp
bool fetch(const knst_c16string& url, knst_byte_string& out) {
    knst_http_auto_client ac;
    knst_web_options o;
    o.timeout_ms       = 3000;
    o.total_timeout_ms = 15000;

    knst_http_response r;
    knst_network_error err;

    if (!ac.get(url, o, r, err)) return false;
    if (r.status != 200) return false;

    out = std::move(r.body);
    return true;
}
```

### Example 2: h3-only, I keep control

```cpp
bool fetch_h3_only(const knst_c16string& url, knst_byte_string& out) {
    knst_http3_client c3;
    knst_web_options o;
    o.timeout_ms       = 5000;
    o.total_timeout_ms = 15000;

    knst_http_response r;
    knst_network_error err;

    if (!c3.get(url, o, r, err)) {
        std::cerr << "h3 failed: " << knst_network_error_string(err) << "\n";
        return false;
    }
    out = std::move(r.body);
    return true;
}
```

### Example 3: Streaming download

```cpp
static bool on_chunk(const uint8_t* data, size_t len, void* user) {
    return std::fwrite(data, 1, len, static_cast<FILE*>(user)) == len;
}

bool download(const knst_c16string& url, const char* path) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    knst_http_auto_client ac;
    knst_web_options o;
    o.timeout_ms       = 60000;
    o.total_timeout_ms = 3600000;
    o.on_body          = on_chunk;
    o.on_body_user     = f;

    knst_http_response r;
    knst_network_error err;
    const bool ok = ac.get(url, o, r, err);

    std::fclose(f);
    return ok && r.status == 200;
}
```

### Example 4: POST + JSON (JSON optional)

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
bool post_data(const knst_c16string& url,
               const knst_json& payload,
               knst_json& result) {
    knst_http_auto_client ac;
    knst_web_options o;
    o.timeout_ms       = 5000;
    o.total_timeout_ms = 30000;

    knst_network_error err;
    return ac.post_json(url, payload, o, result, err);
}
#endif
```

### Example 5: h3 + SSRF guard

```cpp
bool safe_fetch(const knst_c16string& user_url, knst_byte_string& out) {
    knst_url parsed;
    knst_network_error err;
    if (!knst_url::parse(user_url, parsed, err)) return false;
    if (parsed.scheme != u"https") return false;

    knst_http_auto_client ac;
    knst_web_options o;
    o.timeout_ms       = 3000;
    o.total_timeout_ms = 10000;
    o.allow_private_networks = false;
    o.max_response_bytes     = 1024 * 1024;

    knst_http_response r;
    if (!ac.get(user_url, o, r, err)) return false;
    if (!r.is_success()) return false;

    out = std::move(r.body);
    return true;
}
```

### Example 6: Multiple origins in parallel

```cpp
knst_http_auto_client ac;   // thread-safe (session pool has mutex)

std::vector<std::thread> threads;
for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&, i]() {
        knst_web_options o;
        o.timeout_ms       = 5000;
        o.total_timeout_ms = 15000;

        knst_http_response r;
        knst_network_error err;

        auto url = u"https://api" + knst_c16string(static_cast<long long>(i))
                 + u".example.com/data";
        ac.get(url, o, r, err);
    });
}
for (auto& t : threads) t.join();
```

### Example 7: mTLS + h3

```cpp
// At top of main
knst_tls_set_client_cert("/etc/certs/client.pem", "/etc/certs/client.key");

// Then normal h3
knst_http3_client c3;
knst_web_options o;
o.timeout_ms       = 10000;
o.total_timeout_ms = 30000;

knst_http_response r;
knst_network_error err;
c3.get(u"https://mtls.example.com/api", o, r, err);
```

### Example 8: Debug — which protocol?

```cpp
knst_http_auto_client ac;
knst_web_options o;
o.timeout_ms       = 3000;
o.total_timeout_ms = 15000;

knst_http_response r;
knst_network_error err;

const uint32_t h3_before = ac.h3().idle_sessions();
const uint32_t h2_before = ac.h2().idle_sessions();   // h2() also exists

if (ac.get(u"https://example.com", o, r, err)) {
    const uint32_t h3_after = ac.h3().idle_sessions();
    const uint32_t h2_after = ac.h2().idle_sessions();

    if (h3_after > h3_before) std::cout << "h3 used\n";
    else if (h2_after > h2_before) std::cout << "fell back to h2\n";
    else std::cout << "h1 used\n";
}
```

---

## 📌 11) API Contract Summary

```cpp
// knst_network_h3.hpp — requires knst_network_h2.hpp

namespace knst_h3_detail {

    inline uint64_t now_ns() noexcept;
    inline time_pt  tp_from_ns(uint64_t ns) noexcept;
    inline bool     soft_net_error(knst_network_error e) noexcept;

    class udp_conn {
        enum class r_t { Ok, Again, Fail };
        bool open(const knst_endpoint&, const knst_ip_address*, knst_network_error&) noexcept;
        r_t  send(const uint8_t*, size_t, knst_network_error&) noexcept;
        r_t  recv(uint8_t*, size_t, size_t&, knst_network_error&) noexcept;
        bool is_open() const noexcept;
        knst_socket_handle_t handle() const noexcept;
        void close() noexcept;
    };

    inline SSL_CTX* tls_ctx_h3() noexcept;

    static constexpr uint64_t H3_NO_ERROR          = 0x100;
    static constexpr uint64_t H3_INTERNAL_ERROR    = 0x102;
    static constexpr uint64_t H3_REQUEST_REJECTED  = 0x10b;
    static constexpr uint64_t H3_REQUEST_CANCELLED = 0x10c;

} // namespace knst_h3_detail


class knst_h3_session {
public:
    // Factory
    static std::shared_ptr<knst_h3_session> create(
        const knst_url&, const knst_web_detail::proxy_cfg&,
        const knst_web_options&, const deadline_t&, knst_network_error&) noexcept;

    // Pool interface
    bool     is_usable() const noexcept;
    bool     try_reserve(uint32_t local_cap) noexcept;
    void     force_reserve() noexcept;
    void     release() noexcept;
    uint32_t load() const noexcept;
    time_pt  idle_since() const noexcept;
    uint64_t requests_started() const noexcept;
    void     retire() noexcept;

    // Request execution (blocking)
    bool execute(const knst_web_request&, const knst_web_options&,
                 const deadline_t&, knst_http_response&,
                 knst_web_stream&, knst_network_error&) noexcept;
};


class knst_http3_client : public knst_web_client_base<knst_http3_client> {
public:
    void     clear_connection_pool() noexcept;
    uint32_t idle_sessions() const noexcept;

    // Inherited: get/post/put/del/head/request/get_json/post_json
    //            cookies/set_header/clear_headers
private:
    knst_web_session_pool<knst_h3_session> m_pool;
};


class knst_http_auto_client {
public:
    typedef knst_web_options options;
    typedef knst_http_client::multipart_part multipart_part;

    void enable_h3(bool v) noexcept;
    void enable_h2(bool v) noexcept;
    void set_h3_backoff(uint32_t ms) noexcept;

    knst_http_client&  h1() noexcept;
    knst_http2_client& h2() noexcept;
    knst_http3_client& h3() noexcept;

    bool get/post/put/del/head/request(...) noexcept;
    void clear_cookies();
    void clear_connection_pool() noexcept;

#if defined(KNST_NETWORK_ENABLE_JSON)
    bool get_json/post_json(...) noexcept;
#endif

private:
    knst_http_client         m_c1;
    knst_http2_client        m_c2;
    knst_http3_client        m_c3;
    bool                     m_h3 = true, m_h2 = true;
    uint32_t                 m_backoff_ms = 300000;
    std::mutex               m_mtx;
    std::unordered_map<std::string, knst_web_detail::time_pt> m_bad;
};
```

---

## 🎯 What It Brings You

`knst_network_h3` gives you serious advantages in these situations:

- ✅ **HTTP/3 support** — modern CDN/cloud (Cloudflare, Google, AWS) is h3-ready
- ✅ **Multiplexing** — one QUIC connection, N parallel streams (no HOL blocking)
- ✅ **Fast handshake** — TLS 1.3 + QUIC combined handshake (1 RTT)
- ✅ **Session resumption** — ticket cache keyed `h3|host:port`
- ✅ **QPACK** — header compression, dynamic table
- ✅ **PMTUD** — path MTU discovery, large packet optimization
- ✅ **Automatic fallback** — h3 → h2 → h1 via `knst_http_auto_client`
- ✅ **Per-origin backoff** — UDP-blocked network: 1 timeout, not N
- ✅ **Replay guard** — safe fallback for non-idempotent POSTs
- ✅ **SSRF guard** — `allow_private_networks=false` also works on h3
- ✅ **mTLS** — client cert support (shared with h2)
- ✅ **`knst_network_h2.hpp` compatible** — same `knst_web_stream`, pool contract
- ✅ **KernelNucleusT integration** — `knst_c16string`, `knst_byte_string`, `knst_json`
- ✅ **HTTP/1.1 fallback** — via `knst_http_auto_client`

---