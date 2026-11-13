# knst_network_h2 — HTTP/2 Client

Selamlar! 👋 `knst_network_h2.hpp`, KernelNucleusT'nin **HTTP/2 client** modülü. TLS 1.3 + ALPN `h2` + nghttp2 ile HTTPS isteklerini tek TLS bağlantısında **multiplexed stream**'ler olarak çalıştırır. `knst_web_*` katmanı **HTTP/3 ile paylaşılır** — yani `knst_network_h3.hpp` bu header'ın üzerine kurulur.

**Kısaca:** `nghttp2 >= 1.40` + `OpenSSL >= 1.1.0` + `KNST_NETWORK_ENABLE_TLS` define'ı gerekir. **h2c (TLS'siz HTTP/2) desteklenmez.** ALPN `h2` seçilmezse `NotSupported` döner. Proxies: HTTP CONNECT + SOCKS5/SOCKS5h — sadece `https://` hedef için (h2 her zaman TLS altında).

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

### Platform matrisi

| Özellik | Windows | Linux | Android |
|---|---|---|---|
| **HTTP/2 client (TLS üstünde)** | ✅ | ✅ | ✅ |
| **ALPN `h2`** | ✅ | ✅ | ✅ |
| **Multiplexed streams** | ✅ | ✅ | ✅ |
| **QPACK yerine HPACK** | ✅ | ✅ | ✅ |
| **h2c (plain HTTP/2)** | ❌ `NotSupported` | ❌ | ❌ |
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
| **`max_per_host`** | ⚠️ unused (bkz. `max_sessions_per_origin`) | ⚠️ | ⚠️ |
| **Server push** | ❌ refused | ❌ | ❌ |

### Kütüphane bağımlılıkları

| Bağımlılık | Neden gerekli | Versiyon |
|---|---|---|
| **`knst_network.hpp`** | TLS altyapısı, socket, DNS, HTTP/1.1 helpers | dahili |
| **nghttp2** | HTTP/2 protokol motoru | **>= 1.40** |
| **OpenSSL** | TLS 1.3 + ALPN | **>= 1.1.0** |
| **`KNST_NETWORK_ENABLE_TLS`** | TLS tanımlı olmalı, yoksa `TlsNotSupported` | define |

**Link:** `-lnghttp2 -lssl -lcrypto`.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Tek TLS, çoklu stream** | Aynı origin'e N request → 1 TLS socket, multiplexed |
| **CRTP base** | `knst_web_client_base<Derived>` — redirect + cookie + auth + decode ortak |
| **Session pool** | Per-origin pool, `try_reserve` ile saturasyon kontrolü |
| **Driver election** | Tek thread poll eder, diğerleri mutex bekler |
| **Non-blocking TLS handshake** | Tek deadline altında `poll()` döngüsü |
| **`has_pending_input()`** | TLS buffered plaintext var mı — desync guard |
| **Idle session probe** | Reuse öncesi GOAWAY/EOF kontrolü |
| **REFUSED_STREAM replay** | GOAWAY üstündeki stream'ler güvenle retry |
| **Cookie jar (RFC 6265)** | Path sıralı gönderim, Secure/SameSite guard |
| **HPACK** | nghttp2 tarafından yönetilir |
| **`wake_event`** | Loopback UDP self-pipe — driver'ı poll'dan kaldırma |
| **`sigpipe_scope`** | POSIX'te `SSL_write` için thread-local maskeleme |
| **SSRF guard** | `allow_private_networks=false` — pool key'de `S` işaretli |
| **Expect-Continue** | `expect_continue_timeout_ms` ile gecikmeli body |

---

## 🚀 Hızlı Başlangıç

### Basit HTTP/2 GET

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
o.expect_continue = true;                    // 100 Continue gelene kadar body bekler
o.expect_continue_timeout_ms = 2000;         // ama 2 sn sonra pes eder

knst_http_response r;
knst_network_error err;
c2.post(u"https://api.example.com/upload", big_body, o, r, err);
```

---

## 📋 Genel API

| Kategori | Sınıf / Fonksiyon |
|---|---|
| **Ortak katman** | `knst_web_detail::*` (h2 + h3) |
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

## 🔌 1) `knst_web_detail` — Ortak Katman

Bu namespace **HTTP/2 ve HTTP/3 tarafından paylaşılır**. Proxies, cookie jar, header validation, resolver helpers, TLS ctx builder — hep burada.

### 1.1) `proxy_cfg` — Proxy konfigürasyonu

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

- **`Http`** — CONNECT tunnel (https hedef zorunlu)
- **`Socks5`** — `socks5://` — hostname **lokal çözülür**, IP proxy'ye verilir
- **`Socks5` + `socks5_remote_dns=true`** — `socks5h://` — hostname **proxy'de çözülür**

### 1.2) `host_in_no_proxy` + `resolve_proxy`

```cpp
bool host_in_no_proxy(const knst_c16string& host, const knst_c16string& list);
bool resolve_proxy(const knst_url& target, const knst_http_client::options& o,
                   proxy_cfg& pc, knst_network_error& err) noexcept;
```

**`resolve_proxy` kaynakları:**
- `o.proxy_url` — set edildiyse onu
- `o.use_env_proxy` → `https_proxy` / `HTTPS_PROXY` (hedef https olduğu için ikisi de)
- `no_proxy` / `NO_PROXY` env — bypass listesi

**Bypass eşleştirme:** `*` her şey, `.example.com` subdomain, `host:port` belirli port.

**httpoxy notu:** `resolve_proxy` sadece **`https_proxy`** okur, çünkü h2 sadece `https://` hedef kabul eder. `http_proxy` (küçük harf) CGI `Proxy:` header yansımasıyla ilgili — bu header'da plain `http://` zaten `NotSupported`.

### 1.3) `proxy_connect_tunnel` — HTTP CONNECT

```cpp
bool proxy_connect_tunnel(knst_tcp_socket& sock, const knst_url& u,
                          const proxy_cfg& pc, const knst_http_client::options& o,
                          const deadline_t& dl, knst_network_error& err) noexcept;
```

**Ne yapar?**
1. `CONNECT host:port HTTP/1.1` gönderir (`Proxy-Authorization` varsa ekler)
2. Response head okur (16 KiB cap)
3. `2xx` bekler — değilse `ProxyFailed`

**Extra byte kontrolü:** `2xx` olsa bile head sonrası **ek byte varsa** `ProxyFailed` döner (h2 framing'i karışır).

### 1.4) `socks5_handshake` — RFC 1928

```cpp
bool socks5_handshake(knst_tcp_socket& sock, const knst_c16string& target_host,
                      uint16_t target_port, const proxy_cfg& pc,
                      const deadline_t& dl, knst_network_error& err) noexcept;
```

**Akış:**
1. Greeting — `0x05 [0x00] [0x02]` (auth varsa)
2. Method selection — server `0x00` (no-auth) veya `0x02` (user/pass)
3. Auth — `0x01 user_len user pass_len pass`
4. CONNECT — `0x05 0x01 0x00 [addr_type] [addr] [port]`
5. Response — `0x05 rep rsv atyp [bind_addr] [bind_port]`

**addr_type:**
- `0x01` — IPv4 (4 byte)
- `0x04` — IPv6 (16 byte)
- `0x03` — Hostname (len + bytes)

**Not:** `socks5h` (remote DNS) durumunda bile `target_host` proxy'ye geçer — DNS'i proxy yapar. `socks5` (local DNS) durumunda caller IP'ye çevirmiş olmalı.

### 1.5) `resolve_endpoints` — DNS + SSRF

```cpp
bool resolve_endpoints(const knst_c16string& host, uint16_t port,
                       const knst_http_client::options& o,
                       uint32_t dns_to, const deadline_t& dl, bool filter,
                       knst_vector<knst_endpoint>& out, knst_network_error& err) noexcept;
```

**Ne yapar?**
1. **Cache lookup** — `o.dns_cache` ise `knst_dns_cache::global().lookup`
2. **Async resolve** — cache miss → `knst_dns_resolver::resolve_async` (deadline-bound)
3. **Cache store** — başarıysa `o.dns_cache_ttl_ms` ile sakla
4. **SSRF filter** — `filter=true` ise `is_non_public()` olanlar atılır, hepsi atılırsa `ForbiddenAddress`

### 1.6) `literal_target_allowed` — Proxy arkası SSRF

```cpp
bool literal_target_allowed(const knst_c16string& host, knst_network_error& err) noexcept;
```

Proxy kullanıldığında DNS'i proxy yapar — bu yüzden SSRF guard sadece **IP literal** hedefler için çalışır. Host IP değilse `true` döner (proxy kendi çözer). IP ise `is_non_public()` kontrol edilir.

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

**`header_for` path sıralaması:** RFC 6265 §5.4 — **daha uzun path önce**. Eşit path'te eklenme sırası korunur (`std::stable_sort`).

**`collect` — Set-Cookie parse:**
- **Domain** — `.` prefix strip, alt domain match, IP literal origin'de exact host
- **Path** — yoksa §5.1.4 default-path
- **Secure** — HTTPS değilse reddedilir
- **Max-Age** — `Expires`'i override eder
- **Value** — Control char / DEL / > 0x7F reddedilir (injection guard)
- **Name + Value** ≤ 4096
- **Total** ≤ 3000
- **Domain `.com` gibi TLD** — `dots == 0` kontrolüyle reddedilir (en az 1 nokta olmalı)

### 1.8) Header validation

```cpp
bool is_reserved_header(const knst_byte_string& n, const knst_byte_string& v) noexcept;
bool is_sensitive_name(const knst_byte_string& n) noexcept;
bool check_user_header(const knst_http_header& h, knst_byte_string& n,
                       knst_byte_string& v, knst_network_error& err);
```

**`is_reserved_header` — kullanıcı set edemez:**
- `host`, `content-length`, `transfer-encoding`, `connection`, `keep-alive`
- `proxy-connection`, `upgrade`, `expect`, `proxy-authorization`
- `accept-encoding`, `trailer`
- `te` — **sadece `trailers` değeri kabul**, aksi halde reserved

**`is_sensitive_name` — cross-origin redirect'te düşer:**
- `authorization`, `cookie`, `x-api-key`, `x-auth-token`, `proxy-authorization`

**`check_user_header` — pseudo-header kontrolü:** `:` ile başlayan header reddedilir (kullanıcı `:method` set edemez).

### 1.9) `is_idempotent_method`

```cpp
bool is_idempotent_method(const knst_byte_string& m) noexcept;
```

`GET`, `HEAD`, `PUT`, `DELETE`, `OPTIONS`, `TRACE` — bunlar stale connection retry'a uygun.

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

**Ne yapar?** `127.0.0.1:0`'a bind edilmiş UDP socket açar. `signal()` → 1 byte self-send. `poll()` buna readable olarak döner.

**Neden?** Aynı session'a başka thread request eklediğinde driver'ı `poll()`'dan kaldırmak için. Windows'ta `WSAEventSelect` yok; bu portable çözüm.

**Fallback:** `init()` başarısız olursa poll timeout **50 ms** ile cap'lenir (wake olmadan da çalışır, sadece daha çok poll yapar).

### 1.11) `poll_sock_wake`

```cpp
void poll_sock_wake(knst_socket_handle_t s, bool want_write, knst_socket_handle_t w,
                    uint32_t timeout_ms, bool& s_read, bool& s_write, bool& woke,
                    knst_network_error& err) noexcept;
```

**Davranış:** Socket readable **veya** writable (istenirse) **veya** wake fd readable'a kadar bekler. Socket'te hata/hangup varsa `s_read = true` olarak raporlanır — sonraki `recv` gerçek hatayı yüzeye çıkarır.

### 1.12) TLS ctx

```cpp
bool host_to_cstr(const knst_c16string& host, char* buf, size_t cap) noexcept;
bool tls_prepare_peer(SSL* ssl, const char* hbuf) noexcept;
SSL_CTX* build_ssl_ctx(const unsigned char* alpn, unsigned alpn_len,
                       bool tls13_only, bool session_cache) noexcept;
SSL_CTX* tls_ctx_h2() noexcept;
```

**`build_ssl_ctx` — h2 için:**
- TLS 1.2 minimum (h2 için TLS 1.3 şart değil, RFC 9113 TLS 1.2+)
- `SSL_OP_NO_COMPRESSION`, `SSL_OP_NO_RENEGOTIATION`, `SSL_OP_IGNORE_UNEXPECTED_EOF` (framed protokol toleransı)
- ALPN `h2` set
- Session cache (`SSL_SESS_CACHE_NO_INTERNAL_STORE`)
- CA: `ca_file`/`ca_path` set ise onları, değilse default paths + **Android `/apex`+`/system/etc` + Linux CA bundle'ları + Windows cert store** — hepsi `knst_net_detail::tls_st()` üzerinden
- mTLS: `client_cert` + `client_key` varsa chain + key + check

**`tls_prepare_peer`:**
- **IP literal** → `X509_VERIFY_PARAM_set1_ip_asc` (SNI yok, iPAddress SAN)
- **Hostname** → SNI + `SSL_set1_host` + `X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS`

**`tls_ctx_h2` — process-wide singleton:** `once_flag` ile bir kez `build_ssl_ctx(alpn="h2", ..., tls13_only=false, session_cache=true)`.

### 1.13) `sigpipe_scope` + `min_deadline` + `is_conn_error`

```cpp
struct sigpipe_scope { /* ctor'da sigpipe_guard, dtor'da otomatik */ };
deadline_t min_deadline(const deadline_t& a, const deadline_t& b) noexcept;
time_pt now_tp() noexcept;
bool is_conn_error(knst_network_error e) noexcept;
```

**`sigpipe_scope`** — POSIX'te `SSL_write` sırasında SIGPIPE'ı bu thread için maskeler. `knst_net_detail::sigpipe_guard`'ı sarar.

**`is_conn_error`** — `ConnectionClosed`, `HttpConnectionClosed`, `ConnectionReset`, `ConnectionAborted`, `BrokenPipe` — retry uygun hatalar.

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

**Not:** `name` her zaman **lowercase**. `path` **percent-encoded** (`u.request_target()` sonucu).

### 2.2) `knst_web_options`

`knst_http_client::options`'ı **miras alır** ve h2/h3 için ek alanlar ekler:

```cpp
struct knst_web_options : public knst_http_client::options {
    // Trailer (body sonrası gönderilir)
    knst_vector<knst_http_header> request_trailers;

    // Expect-Continue
    bool     expect_continue            = false;
    uint32_t expect_continue_timeout_ms = 1000;

    // Stream limitleri
    uint32_t max_concurrent_streams  = 100;      // session başına lokal cap
    uint32_t initial_stream_window   = 1u << 20; // 1 MiB
    uint32_t connection_window       = 16u << 20;// 16 MiB
    uint32_t max_sessions_per_origin = 2;        // saturasyon sonrası extra session
    uint32_t cancel_grace_ms         = 2000;     // RST_STREAM flush için bekle
};
```

**`expect_continue` ne yapar?** Body gönderilmeden `Expect: 100-continue` header'ı eklenir. Server `100 Continue` dönerse body gönderilir. Timeout (`expect_continue_timeout_ms`) içinde gelmezse **pess edip body'yi gönderir**. Bu h2 framing'inde `NGHTTP2_ERR_DEFERRED` ile yönetilir.

---

## 🌊 3) `knst_web_stream` — Response Assembler

h2 ve h3 tarafından paylaşılır. Her aktif stream için bir instance.

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
    const char16_t* version;                     // u"HTTP/2" veya u"HTTP/3"
    knst_vector<knst_http_header> tmp;           // header birikimi
    int            tmp_status;
    std::unique_ptr<knst_net_detail::byte_sink> sink;

    // Lifecycle
    bool           closed, cancel_req, rst_sent, abandoned, refused;
    knst_network_error err;
    time_pt        last_activity, cancel_at, total_end;
    bool           has_total;
    uint32_t       idle_ms;

    bool    resp_end;            // END_STREAM gördük
    int32_t stream_id;

    // Metodlar
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

### `block_t` değerleri

| Değer | Anlamı |
|---|---|
| **`Error`** | `st->err` set — stream RST edilmeli |
| **`Interim`** | `1xx` (100 hariç, ör. 103 Early Hints) — stored değil |
| **`Continue100`** | `100 Continue` — deferred body'yi resume et |
| **`Final`** | `2xx`/`3xx`/`4xx`/`5xx` — header'lar bitti |
| **`Trailer`** | Trailer block — `out->headers`'a eklenir |

### `expiry()` — deadline hesabı

```cpp
time_pt expiry() const noexcept;
```

**Dönüş:** `min(total_end, last_activity + idle_ms)`. Hangisi daha önce. `total_end` yoksa (infinite deadline) sadece idle. İkisi de yoksa `time_pt::max()`.

### `on_data` — body chunk

```cpp
bool on_data(const uint8_t* d, size_t n, knst_network_error& e) noexcept;
```

**Akış:**
1. `!got_final` → `HttpMalformedBody` (data header'dan önce gelemez)
2. `no_body || discard || n == 0` → **swallow** (HEAD/204/304/redirect body'si yutulur)
3. `received + n > max_body` → `ResponseTooLarge`
4. **Streaming mode** (`on_body`): callback çağır, false → `CallbackAborted`
5. **Buffered mode:** `byte_sink`'e memcpy

---

## 🔌 4) `knst_h2_socket` — TCP + TLS (ALPN h2)

`knst_tcp_socket`'i miras alır. Non-blocking I/O + TLS.

```cpp
class knst_h2_socket : public knst_tcp_socket {
public:
    enum class io_t { Ok, WantRead, WantWrite, Eof, Fail };

    bool start_tls_h2(const knst_c16string& host, uint16_t port,
                      const deadline_t& dl, knst_network_error& err) noexcept;

    bool has_pending_input() const noexcept override;

    // Plain send/recv (proxy handshake öncesi)
    bool send(...) noexcept override;
    bool recv(...) noexcept override;

    // TLS-aware
    io_t read_some(void* buf, size_t cap, size_t& got, knst_network_error& err) noexcept;
    io_t write_some(const void* data, size_t len, size_t& sent, knst_network_error& err) noexcept;

    void close() noexcept override;
};
```

### 4.1) `start_tls_h2`

**Ne yapar?**
1. `SSL_new(tls_ctx_h2())`
2. **Hostname**: IP literal ise `X509_VERIFY_PARAM_set1_ip_asc` (SNI yok), değilse SNI + `SSL_set1_host`
3. Session resumption key: `"h2|lower(host):port"` (h3 ile cache paylaşılır ama prefix farklı)
4. Handshake **non-blocking** — `poll()` döngüsü, tek `deadline` altında
5. **ALPN check** — `h2` seçilmediyse `NotSupported` (server h2 desteklemiyor)
6. **Başarıda socket NON-BLOCKING kalır** (plain `knst_ssl_socket`'in aksine — bu blocking olur)

**Hata yolları:**
- `TlsCertificateInvalid` — hostname/CA mismatch
- `TlsHandshakeFailed` — diğer TLS hataları
- `NotSupported` — ALPN `h2` değil
- `Timeout` — deadline aştı

### 4.2) `read_some` / `write_some`

**`SSL_ERROR_WANT_READ` / `WANT_WRITE`** → `io_t::WantRead` / `WantWrite` döner. Caller `poll()` yapıp tekrar çağırır.

**`SSL_ERROR_ZERO_RETURN`** → `io_t::Eof` (clean close_notify).

**`SSL_ERROR_SYSCALL` + errno==0** → `io_t::Eof` (unexpected EOF — `SSL_OP_IGNORE_UNEXPECTED_EOF` sayesinde framed protokol bunu tolere eder).

**`EINTR`** → retry (döngü içinde).

### 4.3) `close()` — sessiz shutdown

`m_noshut` flag'i `SSL_shutdown`'ı atlar:
- Handshake fail
- `read_some`/`write_some` protokol hatası
- `read_some` SYSCALL fail

Bu durumlarda `SSL_shutdown` **deadlock/hang** yapabilir.

**Başarılı bağlantıda:** non-blocking mode'da `SSL_shutdown` çağrılır, hang etmez.

### 4.4) `has_pending_input()`

TLS buffered **decrypted plaintext** var mı? `SSL_pending(m_ssl) > 0`.

**Neden önemli?** Socket readable olmasa bile TLS buffer'ında byte olabilir. `poll()` bunu görmez → read yapmadan önce bu kontrol.

### 4.5) Plain `send`/`recv` — sadece proxy handshake öncesi

TLS aktifse `NotSupported` döner. Proxy CONNECT/SOCKS5 handshake'i **TLS öncesi** yapılır, o yüzden plain socket kullanılır.

---

## 🧠 5) `knst_h2_session` — QUIC Değil, TLS/TCP Session

Bir TLS bağlantısı, çoklu multiplexed stream.

### 5.1) Fabrika + pool arayüzü

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

### 5.2) `create` — bağlantı akışı

```
1. bind_local_ip parse
2. SSRF guard (proxy varsa IP literal kontrolü)
3. DNS resolve (target veya proxy host)
4. TCP connect (conn deadline altında)
5. Proxy handshake (CONNECT veya SOCKS5) — varsa
6. TLS handshake (ALPN h2) — zorunlu
7. nghttp2 session init (settings: ENABLE_PUSH=0, window, max_header)
8. wake_event init (fail tolere edilir)
```

**Deadline birleştirme:** `cd = min(o.timeout_ms, total)`. Yani DNS + connect + proxy + TLS tek deadline altında.

**`TlsCertificateInvalid` özel:** Diğer adresleri denemez — hostname check fail hepsinde fail.

### 5.3) `execute` — request submit + block

**Akış:**
1. **Slot guard** — `release()` dönüşte
2. **Header list build** — pseudo (`:method`, `:scheme="https"`, `:authority`, `:path`) + user headers
3. **`st.setup()`** — response assembler
4. **Expect-Continue** — `continue_ok = false` (100 gelene kadar)
5. **Body provider** — `r.body_len > 0 || trailers` varsa
6. **`nghttp2_submit_request`** — stream oluştur, `st.stream_id = sid`
7. **Idle probe** — `!m_driving && load <= 1` ise GOAWAY/EOF kontrolü (`probe_idle`)
8. **`is_usable()`** — retire/goaway/fatal kontrolü
9. **`nghttp2_session_check_request_allowed`** — session kapanmış mı
10. **`run(st, lk)`** — driver election + event loop
11. **Tamamlandıktan sonra** — `RST_STREAM` (resp_end değilse), `m_srcs` temizlik

### 5.4) Locking modeli

```
m_mtx guard'lar:
  - nghttp2_session*
  - m_streams (sid → st*)
  - m_srcs (sid → body provider)
  - tüm stream state

nghttp2 callbacks:
  - m_mtx HELD iken çağrılır
  - Callback'ler ASLA lock almaz

Driver:
  - m_driving flag ile işaretli
  - Tek thread socket I/O yapar
  - m_mtx sadece socket syscall sırasında bırakılır
  - m_wbuf, m_rbuf, m_woff driver-only

Uyuyan thread'ler:
  - m_cv.wait_until(lk, expiry veya 1s)
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
      m_cv.notify_all()                   # başka waiter'a geç
      continue
  # ben driver değilim
  m_cv.wait_until(lk, min(st.expiry, now+1s))
```

### 5.6) `drive()` — event loop

1. **`scan_timers(now)`** — idle + total deadline kontrolü
2. **`pull_output()`** — `nghttp2_session_mem_send` → `m_wbuf` (bounded 256 KB)
3. **`want_write`/`want_read` kontrol** — session bittiyse fatal
4. **`write_burst(lk, ...)`** — non-blocking `write_some`, `WantWrite` ise çık
5. **`has_pending_input()`** — TLS buffer'da byte var mı?
6. **`poll_sock_wake`** — socket readable/writable veya wake
7. **`read_burst(lk, 16)`** — `read_some` → `nghttp2_session_mem_recv`
8. **`m_cv.notify_all()`** — stream'ler bitmiş olabilir

### 5.7) nghttp2 callback'leri

| Callback | Ne yapar |
|---|---|
| **`cb_begin_headers`** | `st->touch()` + `st->begin_block()` |
| **`cb_header`** | `st->add_header()` — hata → `NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE` (bu stream RST) |
| **`cb_frame` HEADERS** | `st->end_block()` — `Continue100` → resume, `Final` + redirect → RST (body'yi indirme) |
| **`cb_frame` DATA** | `END_STREAM` → `resp_end = true` |
| **`cb_frame` GOAWAY** | `m_goaway = true` — nghttp2 stream'leri REFUSED_STREAM ile kapatır |
| **`cb_frame` SETTINGS** | `m_peer_max = MAX_CONCURRENT_STREAMS` (ACK değilse) |
| **`cb_frame` PUSH_PROMISE** | `RST_STREAM(REFUSED_STREAM)` — push reddedilir |
| **`cb_data`** | `st->on_data()` — hata → `NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE` |
| **`cb_close`** | Hata kodu → `st->on_close()`, `m_srcs.erase()` |
| **`cb_body`** | Request body provider — zero-copy caller buffer'dan |

### 5.8) `cb_close` — hata eşlemesi

| nghttp2 kodu | knst_network_error | refused? |
|---|---|---|
| `NGHTTP2_NO_ERROR` | `None` | hayır |
| `NGHTTP2_REFUSED_STREAM` | `ConnectionAborted` | **evet** (retry güvenli) |
| `NGHTTP2_HTTP_1_1_REQUIRED` | `NotSupported` | hayır |
| `NGHTTP2_CANCEL` | `ConnectionReset` | hayır |
| Diğerleri | `ProtocolError` | hayır |

### 5.9) `cb_body` — body provider

**Akış:**
1. `st->expect && !st->continue_ok` → `NGHTTP2_ERR_DEFERRED` (100 bekleniyor) ya da timeout → `continue_ok = true`
2. `body_off < body_len` → `memcpy` caller buffer'dan
3. `body_off >= body_len` → `NGHTTP2_DATA_FLAG_EOF`
4. **Trailers varsa** → `NGHTTP2_DATA_FLAG_NO_END_STREAM` + `nghttp2_submit_trailer`

**Not:** `src->st = nullptr` yapılır cleanup'ta — provider sonrasında `st`'ye erişirse fail.

### 5.10) `probe_idle` — reuse öncesi kontrol

```cpp
void probe_idle(std::unique_lock<std::mutex>& lk) noexcept;
```

Idle connection reuse edilmeden **önce** `read_burst(lk, 8)` yapar. Amaç: GOAWAY veya EOF bekleyen byte'ları yakalamak. Bu sayede stale connection'a istek submit etmeden önce retire edilir.

**Neden önemli?** Server GOAWAY göndermişse session retire olmalı, aksi halde submit `STREAM_ID_NOT_AVAILABLE` verir.

### 5.11) `flush_locked` — son RST çabası

Execute sonunda RST_STREAM kuyrukta kaldıysa ama driver yoksa `flush_locked` **kendisi driver olur**, `cancel_grace_ms` altında göndermeye çalışır.

### 5.12) `pull_output` — bounded

`m_wbuf - m_woff < 256 KB` iken `nghttp2_session_mem_send` çağır. Backpressure: buffer dolduysa durur.

### 5.13) `set_fatal_locked` — tüm stream'leri fail et

```cpp
void set_fatal_locked(knst_network_error e) noexcept;
```

`m_fatal` set + `m_fatal_err` sakla + **tüm aktif stream'lere `st->fail(e)`** + `m_cv.notify_all()`.

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

### 6.1) `acquire` akışı

1. **Cache lookup** — uygun session var mı?
2. **Creation lock** — aynı key için `timed_mutex`
3. **Tekrar lookup** — yarış koşulu için
4. **Saturasyon kontrolü** — `cnt >= max_sessions` → `force_reserve` ile en az yüklü session'a overcommit (nghttp2 queue'ya alır)
5. **Yeni session** — `make(err)` çağır
6. **Slot reserve** — `force_reserve()`

**Neden creation mutex?** Aynı origin'e N thread patlarsa N connection açmasın diye. Lock altında ikinci lookup bulur.

**`force` semantics:** Saturasyon → `try_reserve` başarısız → `force_reserve` (limit aşımı, nghttp2 queue'ya alır).

### 6.2) Stale session temizliği

`find()` her seferinde:
- `!is_usable()` → retire
- `idle_ms` aşılmış → retire

`add()` MAX_TOTAL=64'ü aşarsa en eski idle retire edilir.

---

## 🎛️ 7) `knst_web_client_base<Derived>` — CRTP Base

h2 ve h3'ün ortak katmanı: redirect, cookie, auth, decode, header validation.

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

**Derived sınıf sağlar:** `exchange(url, proxy, options, request, response, total_deadline, err)`.

### 7.1) `do_request` — ana akış

```
1. use_chunked_upload kontrolü (InvalidArgument)
2. Method validation
3. User headers + trailers + client headers validate
4. Total deadline oluştur
5. Loop (redirect):
   a. URL parse
   b. origin_url sakla (ilk)
   c. userinfo strip
   d. scheme https kontrolü (değilse NotSupported)
   e. proxy_cfg resolve
   f. Request oluştur:
      - :method, :authority, :path
      - user-agent, accept, accept-encoding
      - client headers + user headers (cross-origin'de sensitive strip)
      - Authorization (cross-origin'de yok, user-supplied varsa yok)
      - Cookie (auto_cookie)
      - Content-Length / Content-Type (body varsa)
   g. exchange() → Derived
   h. Cookie collect
   i. Redirect?
      - 303 / 301+POST → GET, body drop
      - 307/308 → method + body korunur (trailer'lar dahil)
      - Location resolve (RFC 3986)
      - max_redirects aşımı → HttpTooManyRedirects
   j. Content-Encoding decode (buffered, non-on_body)
   k. Return
```

**Cross-origin** — `scheme + host + port` farklıysa: `authorization`, `cookie`, `x-api-key`, `x-auth-token`, `proxy-authorization` user headers düşer.

**`dropped` flag** — POST → GET rewrite edildiyse content-type temizlenir, trailer'lar yollanmaz.

### 7.2) `set_header` — client-level default

Kullanıcı `set_header("X-My", "val")` ile default ekler. `check_user_header` validate — pseudo/reserved reddedilir.

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

### 8.1) `make_key` — pool anahtarı

```
"h2|{O|S}|host:port[|p:proxy_host:port[|a:hash]][|b:bind_ip]"
```

- `O` = `allow_private_networks=true`
- `S` = `allow_private_networks=false` (SSRF guard)
- `p:` — proxy host:port + türü (`h` HTTP, `H` socks5h, `s` socks5) + auth hash
- `b:` — bind local IP

**Neden auth hash?** Farklı proxy credentials farklı pool → re-auth gerekmez.

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
    
    # Retry kararı
    safe = st.refused                                              # GOAWAY/REFUSED_STREAM
        OR (!st.response_started && reused && idem && is_conn_error(err))
    if safe && attempt < 2 → retry
    return false
```

**İki retry türü:**
1. **`st.refused`** — `REFUSED_STREAM` veya GOAWAY üstünde kalan stream → **her method güvenli** (server işlemedi)
2. **`!response_started && reused && idem`** — stale reused connection **ilk byte öncesi** öldü → **sadece idempotent** retry

**Max 3 attempt.**

### 8.3) `use_keepalive = false`

Session'ı `retire()` eder — pool'a dönmez, sonraki request yeni session açar.

### 8.4) `idle_sessions()`

Pool'daki live session sayısı.

---

## ⚠️ 9) Sık Tuzaklar

### 1) h2c desteklenmez

`http://` URL → `NotSupported`. h2 her zaman TLS altında (RFC 7540 §3.3, RFC 9113 §3.3). ALPN `h2` seçilmediyse `NotSupported`.

### 2) Proxy sadece https hedef

`resolve_proxy` hedef `https` olduğu için `https_proxy` okunur. `http_proxy` **okunmaz** — plain HTTP zaten `NotSupported`.

### 3) `use_chunked_upload` → `InvalidArgument`

H2 DATA frame'leri ile framed — `Transfer-Encoding: chunked` yok. Bu alan h1'e özgü.

### 4) `lenient_close_delimited` **yok sayılır**

H2 framed protokol — connection close body'yi delimitlemez. `st->resp_end` END_STREAM flag'inden gelir.

### 5) `max_per_host` **kullanılmaz**

Bu h1 için. H2'de `max_sessions_per_origin` (default 2) ve `max_concurrent_streams` (default 100) var.

### 6) `on_body` **driver thread'inde** çalışır

Hangi thread `run()` çağırıp driver olduysa `on_body` onun üstünde. Client'a **re-entry etmeyin** — deadlock.

### 7) TLS ayarları **ilk bağlantıdan önce**

`knst_tls_set_ca_file`, `knst_tls_set_client_cert` — `main()`'de, ilk h2/h3 bağlantısından **önce**. Sonra çağırırsan `false` döner.

**`knst_tls_set_skip_verify` istisna** — anında `ctx`'e uygulanır.

### 8) `skip_verify` process-wide

Bir kere açıldı mı **tüm** h2/h3/h1 TLS bağlantıları etkilenir. Prod'a çıkmadan kaldır.

### 9) IP literal — SNI yok

Hostname IP ise SNI gönderilmez, `iPAddress` SAN kontrol edilir. Hostname ise SNI + hostname check.

### 10) Hostname ASCII

`host_to_cstr` non-ASCII reddeder. IDN (Türkçe vs.) **punycode'a çevir**.

### 11) Cross-origin sensitive header strip

`Authorization`, `Cookie`, `X-Api-Key`, `X-Auth-Token`, `Proxy-Authorization` — redirect'te farklı origin'e **gitmez**. Bilinçli güvenlik.

### 12) `is_reserved_header` listesi dar

Kullanıcı `Host`, `Content-Length`, `Connection`, `TE` (trailers hariç), `Trailer`, `Proxy-Connection`, `Upgrade`, `Expect`, `Proxy-Authorization`, `Accept-Encoding` set edemez. `check_user_header` bunları `HttpInvalidHeader` ile reddeder.

### 13) `check_user_header` pseudo-header reddeder

`:method`, `:path` gibi header'lar kullanıcı tarafından set edilemez.

### 14) `trailers` — sadece `NGHTTP2_DATA_FLAG_NO_END_STREAM` ile

Trailer'lar `cb_body` içinde `body_off >= body_len` olduğunda gönderilir. Öncesinde değil.

### 15) `expect_continue` timeout

`100 Continue` `expect_continue_timeout_ms` içinde gelmezse **pess edip body gönderilir**. Server sonradan reddederse bile body gitmiş olur.

### 16) GOAWAY stream id semantiği

`GOAWAY last_stream_id` — bu id'nin **üstündeki** stream'ler `REFUSED_STREAM` ile kapatılır → **işlenmediler**, güvenle retry. Altındakiler işlenmiş olabilir — non-idempotent için retry tehlikeli.

### 17) `probe_idle` — reuse öncesi byte check

Session idle reuse edilmeden **önce** `read_burst(8)` çağrılır. GOAWAY veya EOF bekleyen byte'ları yakalar → retire kararı.

**Etkisi:** Reuse stale connection'a istek submit etmez.

### 18) `nghttp2_session_check_request_allowed`

`execute` başında çağrılır — session kapanmışsa `ConnectionClosed` + `retire`.

### 19) `wake_event` init fail → 50 ms poll cap

`init()` fail ederse (nadiren), poll timeout 50 ms'ye cap'lenir. Doğru çalışır, sadece daha çok poll yapar.

### 20) Session resumption key `h2|host:port`

Cache h3 ile paylaşılır (`knst_net_detail::tls_sess()`), ama prefix `h2|`. h3 session h2'de kullanılmaz.

### 21) `knst_http2_client` cookie/jar ayrı

Her client kendi `knst_cookie_jar_impl`'ine sahip. `cookies()` snapshot döner (mutex altında copy).

### 22) `get_json` empty body → `null`

`r.body.empty()` → `json() = null`, hata dönmez. **204 No Content** durumunda normal. `r.status` kontrolü yapın.

### 23) `cancel_grace_ms` — 2 saniye default

Execute sonunda RST_STREAM kuyrukta kaldıysa driver yoksa `flush_locked` 2 saniye boyunca göndermeye çalışır. Çok yavaş ağda arttır, çok hızlı kapatma istiyorsan azalt.

### 24) `nghttp2 >= 1.40`

Daha eski sürümler `nghttp2_session_mem_send` API farklı olabilir. `#error` ile kontrol var.

### 25) `nghttp2_submit_trailer` hata durumunda `NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE`

`cb_body` içinde trailer submit fail olursa stream RST edilir.

---

## 📊 10) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| **Session create (ilk)** | 50-200 ms | DNS + TCP + TLS + ALPN |
| **Session create (resume)** | 10-30 ms | TLS ticket |
| **`execute` (multiplexed)** | 1-10 ms | Aynı session, yeni stream |
| **`probe_idle`** | 1-5 ms | Non-blocking read |
| **`pull_output`** | O(frames) | Bounded 256 KB |
| **`write_burst`** | O(bytes) | Non-blocking `SSL_write` |
| **`read_burst`** | O(packets) × 16 | `SSL_read` + `mem_recv` |
| **Cookie lookup** | O(cookies) | Path sort |

**Tipik gecikmeler:**
- **İlk HTTP/2 GET**: 100-300 ms (DNS + handshake + 1 RTT)
- **Keep-alive multiplexed GET**: 10-50 ms
- **Aynı session N stream paralel**: RTT'ler üst üste biner

**Optimizasyon:**
- **`o.use_keepalive = true`** (default) — TLS handshake amortize
- **`o.max_sessions_per_origin`** (default 2) — saturasyon üstü paralel
- **`o.max_concurrent_streams`** (default 100) — peer limit'e uy
- **`o.initial_stream_window` / `connection_window`** — buffer tuning
- **`o.total_timeout_ms`** — slow-drip koruması
- **Streaming download** için `on_body` — RAM tasarrufu

---

## ✅ 11) Do / ❌ Don't

### ✅ Do

```cpp
// 1. Total + per-op timeout birlikte
knst_web_options o;
o.timeout_ms       = 10000;
o.total_timeout_ms = 30000;

// 2. Kullanıcı URL'si → SSRF guard
o.allow_private_networks = false;

// 3. Streaming download
o.on_body      = my_callback;
o.on_body_user = my_user;

// 4. mTLS main'in başında
knst_tls_set_client_cert("client.pem", "client.key");

// 5. Expect-Continue büyük POST için
o.expect_continue = true;
o.expect_continue_timeout_ms = 2000;
c2.post(url, huge_body, o, r, err);

// 6. Request trailer
knst_http_header tr;
tr.name = u"X-Checksum"; tr.value = u"abc123";
o.request_trailers.push_back(tr);

// 7. h3 fallback için auto_client
knst_http_auto_client ac;   // h2'yi de içerir
ac.get(url, o, r, err);
```

### ❌ Yapma

```cpp
// 1. http:// URL — NotSupported
c2.get(u"http://example.com", o, r, err);   // ❌

// 2. use_chunked_upload — InvalidArgument
o.use_chunked_upload = true;   // ❌ h2 için yok

// 3. on_body içinde client'a re-entry
static bool on_chunk(...) {
    c2.get(...);   // ❌ driver thread'de deadlock
    return true;
}

// 4. Host header override
c2.set_header(u"Host", u"other.com");   // ❌ false döner

// 5. Cross-origin auth bekleme
o.basic_user = u"me";
o.basic_pass = u"pw";
// ❌ farklı host'a redirect olursa header düşer

// 6. skip_verify prod'da
knst_tls_set_skip_verify(true);   // ❌ prod'a çıkmadan kaldır

// 7. Alt-Svc ile h2 keşfi
// ❌ bu header Alt-Svc parse etmez — h2 zaten TLS ALPN ile negotiate

// 8. `max_per_host` beklemek
o.max_per_host = 4;   // ⚠️ ignored — max_sessions_per_origin kullan
```

---

## 🔥 12) Gerçek Kullanım Örnekleri

### Örnek 1: REST client (JSON)

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

### Örnek 2: Büyük dosya + progress

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

### Örnek 3: SOCKS5h proxy

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

### Örnek 4: SSRF-safe fetch

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

### Örnek 5: Multipart upload

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

### Örnek 6: Request trailers

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

### Örnek 7: HTTP CONNECT proxy

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
// Proxy CONNECT tunnel: TLS handshake proxy üzerinden
```

### Örnek 8: VPN split routing (bind_local_ip)

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

## 📌 13) API Kontrat Özeti

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
    // ... (bkz. bölüm 3)
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

## 🎯 Size Getireceği Kazanç

`knst_network_h2` şu durumlarda ciddi avantaj sağlar:

- ✅ **Multiplexing** — tek TLS bağlantı, N paralel stream (HOL blocking azaltır)
- ✅ **Header compression** — HPACK, bant genişliği tasarrufu
- ✅ **Fast handshake** — TLS 1.3 + ALPN `h2` (1 RTT)
- ✅ **Session resumption** — `h2|host:port` anahtarıyla ticket cache
- ✅ **Session pool** — per-origin, `try_reserve` ile saturasyon
- ✅ **Driver election** — tek thread poll, N thread paralel request
- ✅ **Idle probe** — stale connection reuse öncesi GOAWAY/EOF kontrol
- ✅ **REFUSED_STREAM replay** — GOAWAY üstünde güvenle retry
- ✅ **Proxy** — HTTP CONNECT + SOCKS5 + SOCKS5h
- ✅ **Cookie jar** — RFC 6265 uyumlu, path sıralı
- ✅ **SSRF guard** — `allow_private_networks=false` pool key'de işaretli
- ✅ **Expect-Continue** — büyük POST için timeout'lu defer
- ✅ **Request trailers** — checksum, signature vb.
- ✅ **`knst_network.hpp` uyumu** — TLS, DNS, socket altyapısı paylaşılır
- ✅ **`knst_network_h3.hpp` uyumu** — `knst_web_*` katmanı paylaşılır
- ✅ **KernelNucleusT entegrasyonu** — `knst_c16string`, `knst_byte_string`, `knst_json`

---