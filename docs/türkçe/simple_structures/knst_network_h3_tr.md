# knst_network_h3 — HTTP/3 (QUIC) Client

Selamlar! 👋 `knst_network_h3.hpp`, KernelNucleusT'nin **HTTP/3 / QUIC** client modülü. UDP/443 üzerinden TLS 1.3 + QUIC + QPACK + HTTP/3 framing'i tek header'da sarar. `knst_network_h2.hpp`'ın `knst_web_*` katmanını **ortak** kullanır — yani HTTP/2 ve HTTP/3 aynı request/response/pool altyapısını paylaşır. Üstüne `knst_http_auto_client` ile **h3 → h2 → h1** otomatik fallback geliyor.

**Kısaca:** Bu header **stok OpenSSL 3.0 ile derlenmez**. QUIC destekli bir TLS (`quictls` / OpenSSL QUIC fork) + `ngtcp2 >= 1.0` + `nghttp3 >= 1.1` gerekir. CMake `H3=ON` bunları sağlar. Sadece `https://` çalışır, proxy desteklenmez, 0-RTT yok.

---

## 🎯 Honesty Matrix — Ne Var, Ne Yok?

### Platform matrisi

| Özellik | Windows | Linux | Android |
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

### Kütüphane bağımlılıkları — bu en kritik kısım

| Bağımlılık | Neden gerekli | Versiyon |
|---|---|---|
| **`knst_network_h2.hpp`** | `knst_web_*` katmanı (stream, pool, options) | dahili |
| **`KNST_NETWORK_ENABLE_TLS`** | TLS tanımlı olmalı, yoksa `#error` | define |
| **ngtcp2** | QUIC protokol motoru | **>= 1.0** |
| **ngtcp2_crypto_quictls** | QUIC + TLS entegrasyon katmanı | >= 1.0 |
| **nghttp3** | HTTP/3 framing + QPACK | **>= 1.1** |
| **quictls / OpenSSL QUIC fork** | QUIC handshake'i yapacak TLS | 3.1+quic önerilir |
| **nghttp2** (transitif) | `knst_network_h2.hpp` üzerinden | >= 1.58 (CMake) |

**Kritik uyarılar:**
- **Stok OpenSSL 3.0 (Ubuntu 22.04 / Pop!_OS) yeterli değil.** `quictls` gerekli.
- CMake'de `KNST_QUICTLS_ROOT=/path/to/quictls` gösterilmelidir.
- Android'de CMake bunu **FetchContent** ile indirir (ilk seferde 10-15 dk).
- **`apple` (macOS)** — KernelNucleusT desteklemiyor (`FATAL_ERROR` CMake'de).

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **Tek QUIC bağlantı, çoklu stream** | Aynı origin'e N request → 1 UDP socket |
| **QPACK** | Header compression (4096 dinamik tablo, 100 blocked stream) |
| **Session resumption** | `h3\|host:port` anahtarıyla ticket cache |
| **PMTUD** | IPv4/IPv6 path MTU keşfi |
| **Multiplexing** | Stream başına deadline, RST_STREAM, STOP_SENDING |
| **GOAWAY handling** | İşlenmemiş stream'ler güvenle tekrar denenebilir |
| **`knst_http_auto_client`** | h3 → h2 → h1 fallback, origin başına 5 dk backoff |
| **Per-origin backoff** | UDP bloklu ağda 1 timeout, N timeout değil |
| **Replay guard** | Non-idempotent / streaming isteklerde sadece connect-phase fallback |
| **Pool interface** | `knst_h2_session` ile aynı kontrat — `try_reserve`/`release` |
| **Driver election** | Tek thread poll eder, diğerleri mutex bekler |
| **Soft net errors** | Handshake öncesi `ConnectionRefused` geçici, sonrası fatal |

---

## 🚀 Hızlı Başlangıç

### Basit HTTP/3 GET

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
knst_http_auto_client ac;   // h3 → h2 → h1 otomatik

knst_web_options o;
o.timeout_ms       = 3000;     // h3 UDP bloklu olabilir, hızlı düşsün
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

### h2/h3 karşılaştırma

```cpp
// Elle kontrol — fallback yok
knst_http3_client c3;
if (!c3.get(url, o, r, err)) {
    // h3 başarısız, h2'ye geç
    knst_http2_client c2;
    c2.get(url, o, r, err);
}

// Otomatik — h3 hata verirse h2, o da olmazsa h1
knst_http_auto_client ac;
ac.get(url, o, r, err);
```

---

## 📋 Genel API

| Kategori | Sınıf / Fonksiyon |
|---|---|
| **QUIC/UDP katmanı** | `knst_h3_detail::udp_conn`, `knst_h3_detail::tls_ctx_h3()` |
| **QUIC oturumu** | `knst_h3_session` |
| **HTTP/3 client** | `knst_http3_client` |
| **Auto fallback client** | `knst_http_auto_client` |
| **H3 error kodları** | `H3_NO_ERROR`, `H3_INTERNAL_ERROR`, `H3_REQUEST_REJECTED`, `H3_REQUEST_CANCELLED` |

---

## 🔌 1) `knst_h3_detail` — Dahili Katman

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

**`open` ne yapar?**
1. `AF_INET`/`AF_INET6` + `SOCK_DGRAM` socket açar
2. `SO_RCVBUF = 2 MB`, `SO_SNDBUF = 1 MB` — QUIC window için
3. **PMTUD açılır:** IPv4'te `IP_PMTUDISC_DO`, IPv6'da `IPV6_PMTUDISC_DO`
4. Opsiyonel `bind_ip` ile kaynak IP sabitleme
5. `connect()` — peer'a bağlanır (kernel routing/ICMP hataları için)
6. `getsockname()` — local addr/port

**`send` davranışı:**
- `MessageTooLarge` → **`r_t::Ok`** döner (paket düşürüldü, PMTUD halledecək). Yani sessiz drop.
- `WouldBlock` → `Again`
- Diğer hatalar → `Fail` + error

**`recv` davranışı:**
- `MessageTooLarge` → `r_t::Ok` + `n = 0` (truncated datagram dropped)
- ICMP-derived hatalar → `Fail` olarak döner ama **handshake sonrası fatal değil** (spoofable/transient)

### 1.2) `tls_ctx_h3()` — Process-wide SSL_CTX

```cpp
inline SSL_CTX* tls_ctx_h3() noexcept;
```

**Ne yapar?** `once_flag` ile bir kez oluşturulur:
- `knst_web_detail::build_ssl_ctx(alpn="h3", sizeof, true /*tls13_only*/, false /*not h2*/)`
- `ngtcp2_crypto_quictls_configure_client_context()` — QUIC TLS method'unu yükler
- Session cache client-side (`SSL_SESS_CACHE_NO_INTERNAL_STORE`)
- `knst_net_detail::tls_new_session_cb` — session ticket cache

**Not:** `tls_new_session_cb` `knst_network.hpp`'dan gelir — h2 ile paylaşılır.

### 1.3) H3 error kodları

```cpp
static constexpr uint64_t H3_NO_ERROR          = 0x100;
static constexpr uint64_t H3_INTERNAL_ERROR    = 0x102;
static constexpr uint64_t H3_REQUEST_REJECTED  = 0x10b;   // güvenle retry
static constexpr uint64_t H3_REQUEST_CANCELLED = 0x10c;
```

`H3_REQUEST_REJECTED` özel anlamda: istek **sunucu tarafından işlenmedi** → hangi HTTP method olursa olsun **retry güvenli**.

### 1.4) `soft_net_error` — ICMP-based hatalar

```cpp
inline bool soft_net_error(knst_network_error e) noexcept;
```

`ConnectionRefused`, `HostUnreachable`, `NetworkUnreachable`, `ConnectionReset` dönerse **handshake sırasında** fatal değil — UDP ICMP spoofable. Handshake tamamlandıysa fatal.

### 1.5) Time helpers

```cpp
inline uint64_t now_ns() noexcept;              // steady_clock nanosecond
inline time_pt  tp_from_ns(uint64_t ns) noexcept;   // nanosecond → time_point
```

---

## 🧠 2) `knst_h3_session` — QUIC Bağlantısı

Bir QUIC connection'ı temsil eder. Aynı origin'e N request → hepsi bu session'ın farklı stream'lerinde multiplexed.

### 2.1) Fabrika + pool arayüzü

```cpp
class knst_h3_session {
public:
    static std::shared_ptr<knst_h3_session> create(
        const knst_url& u,
        const knst_web_detail::proxy_cfg& pc,
        const knst_web_options& o,
        const deadline_t& total,
        knst_network_error& err) noexcept;

    // Pool interface — knst_h2_session ile aynı kontrat
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

**`create` ne yapar?**
1. **Proxy kontrolü** — `pc.enabled()` ise `NotSupported`
2. **Deadline birleştir** — `o.timeout_ms` ve `total` arasından minimum
3. **bind_local_ip parse** — varsa `knst_ip_address::from_string`
4. **DNS resolve** — `knst_web_detail::resolve_endpoints`
5. **Her endpoint için `attempt()`** — biri başarılıysa o
6. **`TlsCertificateInvalid`** gelirse **diğer adresleri denemez** (hostname check fail tüm adreslerde fail demek)

### 2.2) `execute` — Request submit + block

```cpp
bool execute(
    const knst_web_request& r,
    const knst_web_options& o,
    const deadline_t& total,
    knst_http_response& out,
    knst_web_stream& st,
    knst_network_error& err) noexcept;
```

**Ne yapar?**
1. **Slot guard** — `release()` dönerken çağrılır (pool slot serbest bırakılır)
2. **Stream setup** — `st.setup(&out, r, o, total, o.timeout_ms, u"HTTP/3")`
3. **Expect-Continue** — `r.body_len > 0 && o.expect_continue` ise `continue_ok = false`
4. **Pending queue'ya ekle** — sonra `m_pending.push_back(h)` + `m_requests++`
5. **Eğer driver yoksa** → `m_wake.signal()` + kendin driver ol
6. **`run(st, lk)`** — blocking poll loop, `st.finished()` olana kadar
7. **Tamamlanınca** — cleanup, `ngtcp2_conn_shutdown_stream` ile RST_STREAM (resp_end değilse)
8. **`st.err`'i `err`'e aktar**

**Kritik not:** `h->st = nullptr; h->req = nullptr;` — callback'ler sonrasında no-op olur. Fakat ctx `stream_close` callback'ine kadar `m_streams`'te kalır.

### 2.3) Locking modeli — çok önemli

```
m_mtx guard'lar:
  - ngtcp2_conn*
  - nghttp3_conn*
  - m_streams (stream_id → ctx)
  - m_pending (submit edilmemiş)
  - tüm stream state (st->err, st->got_final, ...)

Callback'ler:
  - ngtcp2/nghttp3 call'ları içinde m_mtx HELD ile çağrılır
  - Yani callback'ler ASLA lock almaz (deadlock yok)

Driver:
  - m_driving flag ile işaretli
  - Tek thread poll eder
  - m_mtx'i sadece poll() sırasında bırakır
  - poll döner → m_mtx tekrar alınır

Uyuyan thread'ler:
  - m_cv.wait_until(lk, expiry veya 1s)
  - Driver bitince veya stream bitince uyanır
```

**Sonuç:** Tek session'a **N thread paralel request atabilir** — sadece biri driver olur, diğerleri `m_cv` bekler.

### 2.4) QUIC handshake akışı

```
attempt(ep, ...):
  1. udp_conn.open(ep)                    # UDP/443 (veya ne ise)
  2. init_quic(u, o)                      # ngtcp2_conn_client_new
  3. m_driving = true                     # bu thread driver
  4. loop until (m_hs_done && m_http):
     a. step(lk, dl.end)                  # bir event loop iterasyonu
        - ngtcp2_conn_get_expiry → handle_expiry (timeout/fatal)
        - scan() → per-stream deadline kontrolü
        - write_packets() → outbound UDP
        - poll_sock_wake() → readable'a kadar bekle (m_mtx serbest)
        - read_packets() → inbound UDP
  5. m_driving = false

init_quic içinde:
  - SSL_new(ctx_h3)
  - tls_prepare_peer → SNI/hostname verify
  - SSL_set_session (cache'den)
  - ngtcp2_conn_client_new(tls_native_handle = ssl)
```

### 2.5) `setup_http()` — 1-RTT key gelince

```cpp
// recv_rx_key callback (NGTCP2_ENCRYPTION_LEVEL_1RTT):
//   → setup_http() çağrılır
//   → nghttp3_conn_client_new + 3 unidirectional stream:
//       - control stream (bind_control_stream)
//       - QPACK encoder stream (bind_qpack_streams)
//       - QPACK decoder stream (bind_qpack_streams)
```

**Kritik kontrol:** `ngtcp2_conn_get_streams_uni_left(m_conn) < 3` ise `ProtocolError` — server QUIC param'larını kısıtlamış demektir.

### 2.6) HTTP/3 stream submit akışı

```cpp
// open_pending() içinde:
int64_t sid = -1;
ngtcp2_conn_open_bidi_stream(m_conn, &sid, nullptr);

// Pseudo-header'lar
:method    = r.method
:scheme    = "https"            # hard-coded! http:// yok
:authority = r.authority
:path      = r.path

// Custom header'lar
for h in r.headers:
    nva.push_back({h.name, h.value})

// data_reader (body varsa)
nghttp3_data_reader dr;
dr.read_data = &knst_h3_session::h_read_body;

nghttp3_conn_submit_request(m_http, sid, nva, dr);
```

**`h_read_body`** — nghttp3 çağırır, business logic **caller'ın buffer'ından** okur (zero-copy):
- `st->expect && !st->continue_ok` → `NGHTTP3_ERR_WOULDBLOCK` (Expect-Continue bekleniyor)
- Body → `vec[0].base = const_cast<uint8_t*>(st->body + st->body_off)`
- `body_len` tamamen tükendiyse → `NGHTTP3_DATA_FLAG_EOF`
- Trailer varsa → `NGHTTP3_DATA_FLAG_NO_END_STREAM` + `nghttp3_conn_submit_trailers`

### 2.7) `scan()` — per-stream deadline watchdog

```cpp
time_pt scan(time_pt now) noexcept;
```

Her step'te çağrılır:
- **`st->err`** set ise → `cancel_locked(h, H3_REQUEST_CANCELLED)`
- **Expect deadline** geçtiyse → `continue_ok = true` (retry deferred stream)
- **`st->expiry()`** geçtiyse → `st->fail(Timeout)` + cancel
- **Pending queue**'daki stream'ler için de aynı kontrol
- Return: en yakın deadline (step() poll timeout olarak kullanır)

### 2.8) `step()` — tek event loop iterasyonu

```cpp
void step(std::unique_lock<std::mutex>& lk, time_pt limit) noexcept;
```

Sırayla:
1. **`ngtcp2_conn_get_expiry`** → expired ise `ngtcp2_conn_handle_expiry` (timeout → `Timeout`/`ConnectionClosed`)
2. **`scan(now)`** → stream deadline kontrolü
3. **`write_packets(ts)`** → outbound, `more` döner
4. **`next` deadline hesapla:** scan + limit + conn expiry
5. **`more` false ise:**
   - `poll_sock_wake(sock, write_blocked, wake_handle, timeout_ms, s_read, sw, woke, pe)`
   - `m_mtx` **serbest bırakılır** (poll sırasında)
   - `pe` set ise → fatal
6. **`s_read` true ise** → `read_packets()`
7. `m_cv.notify_all()` — stream finish'leri bildir

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
      m_cv.notify_all()   # başka waiter'a geç
      continue
  # ben driver değilim, başka thread sürüyor
  m_cv.wait_until(lk, min(st.expiry, now+1s))
```

**Neden bu model?** Tek QUIC connection → tek UDP socket → tek poll. Multiplexing için driver election.

### 2.10) `shutdown_()` — destructor

```cpp
~knst_h3_session() noexcept { shutdown_(); }
```

1. `ngtcp2_conn_write_connection_close` → CONNECTION_CLOSE frame (H3_NO_ERROR)
2. UDP'den gönder
3. `nghttp3_conn_del` + `ngtcp2_conn_del` + `SSL_free`
4. UDP close

**Hata olursa sessiz** — destructor'da exception atılmaz.

---

## 🌐 3) `knst_http3_client` — Public API

`knst_web_client_base<knst_http3_client>` kalıtır — yani `get`, `post`, `put`, `del`, `head`, `request`, `get_json`, `post_json`, `cookies`, `set_header`, `clear_headers` **hepsi hazır** (h2 ile aynı).

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

### 3.1) `make_key` — pool anahtarı

```
"h3|{O|S}|host:port[|b:bind_ip]"
```

- `O` = `allow_private_networks=true` (unguarded pool)
- `S` = `allow_private_networks=false` (guarded pool)

**Neden ayrı?** Guard'lı ve guard'sız bağlantı karışmamalı — DNS rebinding SSRF'ye karşı.

### 3.2) `exchange` — request + retry logic

```cpp
bool exchange(const knst_url& u, const knst_web_detail::proxy_cfg& pc,
              const knst_web_options& o, const knst_web_request& r,
              knst_http_response& out,
              const knst_web_detail::deadline_t& total,
              knst_network_error& err) noexcept;
```

**Akış:**
1. **Proxy varsa** → `NotSupported` (HTTP/3 needs CONNECT-UDP)
2. **Pool'dan acquire** — yoksa `create()` ile yeni
3. **`s->execute(r, o, total, out, st, err)`** — blocking
4. **`!o.use_keepalive`** → `s->retire()` (pool'a geri dönmesin)
5. **Başarısızsa retry kararı:**
   - `st.refused` (GOAWAY veya H3_REQUEST_REJECTED) → **her method için güvenli retry**
   - `!st.response_started && reused && idem && is_conn_error(err)` → **stale reused connection**, idempotent method için retry
   - Aksi halde → **başarısız**

**Max 3 attempt.**

### 3.3) Ne yapmaz?

- **h3 başarısız olursa h2/h1'e DÜŞMEZ.** Bu client sadece h3 konuşur. Fallback için `knst_http_auto_client` kullan.
- **`http://` URL** → `NotSupported` (`:scheme` hard-coded "https")
- **`use_chunked_upload`** → `InvalidArgument` (H3'te framed protokol, CL/chunked yok)

---

## 🎛️ 4) `knst_http_auto_client` — Otomatik Fallback

h3 → h2 → h1 sırasıyla dener. Başarısız origin'leri **belirli bir süre atlar** (default 5 dk).

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

### 4.1) Karar ağacı

```
run(method, url, body, o, out, err):

  1. URL parse → https mi?
     - DEĞİL veya use_chunked_upload → h1 (doğrudan)
  2. strict = !idempotent(method) || o.on_body != nullptr
  3. proxied = o.proxy_url || o.use_env_proxy

  4. if m_h3 && !proxied && !recently_failed(origin):
        if h3.request(...) → OK
        fb = strict ? connect_phase(err) : replayable(err)
        if !fb → return false  (gerçek hata: redirect loop, size limit, vb.)
        mark_failed(origin)
        reset err/out

  5. if m_h2:
        if h2.request(...) → OK
        fb = (err == NotSupported || err == TlsNotSupported)
        if !fb → return false
        reset err/out

  6. h1.request(...) → OK veya hata
```

### 4.2) `connect_phase` vs `replayable`

**`connect_phase(e)`** — istek **kesinlikle sunucuya ulaşmadı**:
- `ConnectionRefused`, `HostUnreachable`, `NetworkUnreachable`, `NetworkDown`
- `TlsHandshakeFailed`, `NotSupported`, `TlsNotSupported`

**`replayable(e)`** — istek **güvenle tekrar denenebilir** (sadece idempotent):
- `connect_phase` + `Timeout`, `ConnectionClosed`, `HttpConnectionClosed`, `ConnectionReset`, `ConnectionAborted`, `ProtocolError`, `BrokenPipe`

**Neden bu ayrım?**
- **Non-idempotent POST** → sadece `connect_phase` (istek sunucuya ulaştıysa **tekrar gönderme**)
- **Streaming `on_body`** → callback iki kez tetiklenmesin diye sadece `connect_phase`
- **Idempotent GET/HEAD/PUT/DELETE** → `replayable` (timeout/close sonrası tekrar dene)

### 4.3) Backoff mekanizması

```cpp
std::unordered_map<std::string, knst_web_detail::time_pt> m_bad;
std::mutex m_mtx;
uint32_t m_backoff_ms = 300000;   // 5 dk
```

- **Key:** `host:port` (string)
- **Value:** `now + backoff_ms` (expiry time)
- **`recently_failed(key)`** → `now < expiry` ise true
- **`mark_failed(key)`** → `m_bad[key] = now + backoff_ms`
- **Cap:** 512 entry (dolarsa hepsi temizlenir)

**Senaryo:** UDP bloklu ağ (kurumsal firewall). İlk istek:
1. h3 → 3 sn timeout
2. `connect_phase(Timeout)` → **false** (Timeout connect_phase değil, replayable)
3. `strict = !idempotent(POST)` → **true** olduğu için `connect_phase(err)` → **false**
4. **POST başarısız** döner. GET olsa `replayable` üzerinden geçerdi.

Sonraki 5 dakika boyunca aynı origin için h3 hiç denenmez → h2/h1 doğrudan.

### 4.4) `strict` semantiği

| Durum | `strict` | Fallback koşulu |
|---|---|---|
| **Idempotent GET** | false | `replayable(err)` |
| **Non-idempotent POST** | true | `connect_phase(err)` |
| **Streaming `on_body`** | true | `connect_phase(err)` |

**Sonuç:** POST + timeout → **fallback yok, hata döner**. Ama POST + connection refused → **h1'e düşer**.

### 4.5) Her protokolün **kendi** state'i var

```cpp
knst_http_client   m_c1;
knst_http2_client  m_c2;
knst_http3_client  m_c3;
```

- **Cookie jar'lar ayrı** — h3'te alınan cookie h1'e geçmez (browser de böyle)
- **Connection pool'lar ayrı**
- **Header'lar ayrı** — `h3().set_header()` sadece h3'ü etkiler

**Kullanım:** `ac.h1().set_header(...)`, `ac.h2().cookies()`, `ac.h3().idle_sessions()` — direkt erişim.

---

## 🔄 5) `knst_web_stream` — Ortak Katman (h2'den)

Bu header doğrudan `knst_web_stream` tanımlamaz — h2'deki ortak katmanı kullanır. Ama burada kullanılan kısımlar:

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

    // Hata
    knst_network_error err = knst_network_error::None;

    // Metodlar
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

**`block_t` değerleri:**
- `Continue100` — `100 Continue` geldi, body'i gönder
- `Interim` — `1xx` (100 hariç)
- `Final` — `2xx`/`3xx`/`4xx`/`5xx` — header'lar bitti
- `Trailer` — trailer header'ları
- `Error` — `st->err` set

---

## 🔍 6) Callback Zinciri — Detaylı

### 6.1) ngtcp2 callback'leri

| Callback | Ne yapar |
|---|---|
| **`client_initial`** | QUIC Initial packet için (ngtcp2 crypto) |
| **`recv_crypto_data`** | TLS handshake byte'ları (ngtcp2 crypto → SSL) |
| **`encrypt` / `decrypt`** | QUIC packet şifreleme/çözme (ngtcp2 crypto) |
| **`hp_mask`** | Header protection mask (ngtcp2 crypto) |
| **`recv_retry`** | Retry paketi işleme (ngtcp2 crypto) |
| **`update_key`** | Key rotation (ngtcp2 crypto) |
| **`handshake_completed`** | `m_hs_done = true` (bizim) |
| **`recv_rx_key`** | 1-RTT key açıldı → `setup_http()` çağır |
| **`recv_stream_data`** | QUIC stream data → `nghttp3_conn_read_stream` |
| **`acked_stream_data_offset`** | ACK → `nghttp3_conn_add_ack_offset` |
| **`stream_close`** | Stream kapandı → `nghttp3_conn_close_stream` |
| **`stream_reset`** | RST_STREAM → `nghttp3_conn_shutdown_stream_read` |
| **`stream_stop_sending`** | STOP_SENDING → `nghttp3_conn_shutdown_stream_read` |
| **`extend_max_local_streams_bidi`** | Server MAX_STREAMS arttı → no-op (pending her step retry) |
| **`extend_max_stream_data`** | Stream window arttı → `nghttp3_conn_unblock_stream` |
| **`rand`** | `RAND_bytes` |
| **`get_new_connection_id`** | Yeni CID üret (RAND + reset token) |

### 6.2) nghttp3 callback'leri

| Callback | Ne yapar |
|---|---|
| **`acked_stream_data`** | no-op (bilgi) |
| **`stream_close`** | H3 error kodundan `st->on_close()` |
| **`recv_data`** | Body chunk → `st->on_data()` |
| **`deferred_consume`** | `ngtcp2_conn_extend_max_stream_offset` |
| **`begin_headers`** | `st->begin_block()` |
| **`recv_header`** | `st->add_header()` |
| **`end_headers`** | `st->end_block()` → `Continue100` ise `resume_stream` |
| **`begin_trailers` / `recv_trailer` / `end_trailers`** | Aynı handler'lar |
| **`end_stream`** | `st->resp_end = true` |
| **`stop_sending`** | `ngtcp2_conn_shutdown_stream_read` |
| **`reset_stream`** | `ngtcp2_conn_shutdown_stream_write` |
| **`shutdown`** | **GOAWAY** — stream id >= `id` → `st->refused = true` + `st->fail(ConnectionAborted)` |

**GOAWAY semantiği:** `id`'nin üstündeki stream'ler **işlenmedi** → güvenle retry. Altındaki stream'ler **işlendi** (belki cevap verilmedi ama sunucu gördü) → retry **tehlikeli** (non-idempotent için).

---

## ⚠️ 7) Sık Tuzaklar

### 1) Stok OpenSSL 3.0 ile derlenmez

```
/usr/include/openssl/ssl.h:  stock
ngtcp2_crypto_quictls:        requires QUIC-capable SSL
```

**Çözüm:** `-DKNST_QUICTLS_ROOT=/path/to/quictls` veya Android'de otomatik FetchContent.

### 2) `http://` URL → `NotSupported`

`:scheme` pseudo-header **hard-coded `https`**. HTTP/3 için encryption zorunlu (QUIC TLS 1.3 üstünde). `http://` gönderirsen `knst_http3_client` hata verir.

### 3) Proxy → `NotSupported`

HTTP proxy CONNECT-UDP desteklemez. SOCKS5 UDP ASSOCIATE da desteklenmez (bu header'da yok). **UDP-üstü proxy için ayrı implementasyon gerekir.**

### 4) `use_chunked_upload = true` → `InvalidArgument`

HTTP/3'te Transfer-Encoding yok. Body framing DATA frame'leriyle yapılır. `use_chunked_upload` h1'e özgü.

### 5) `lenient_close_delimited` **yok sayılır**

H3 framed protokol — connection close body'yi delimitlemez. `st->resp_end` DATA frame'inden gelir.

### 6) `on_body` **driver thread'inde** çalışır

Hangi thread `run()` çağırıp driver olduysa `on_body` onun üstünde çalışır. **Başka thread olabilir!** Client'a **re-entry etmeyin** — deadlock.

### 7) Alt-Svc discovery yok

`Alt-Svc: h3=":443"` header'ı **okunmaz**. Doğrudan UDP/443 dener. H2 sunucusu h3 destekliyorsa ama Alt-Svc göndermiyorsa bu client **bilmez**.

**Sonuç:** Önceden bilmen gerekir ki origin h3 konuşur. Ya da `knst_http_auto_client` kullanıp hata alınca backoff'la düş.

### 8) `ngtcp2 >= 1.26` farkı

```cpp
// Yorum satırı (execute içinde):
// (idle-expiry check removed — ngtcp2 >= 1.26 no longer exposes
//  ngtcp2_conn_get_idle_expiry; QUIC idle timeout is handled by
//  ngtcp2 internally via ngtcp2_conn_get_expiry + handle_expiry)
```

**Anlamı:** Eski ngtcp2 sürümlerinde `get_idle_expiry` vardı, yeni sürümlerde kaldırıldı. Kod her iki sürümü de destekler — `get_expiry` + `handle_expiry` üzerinden.

### 9) Handshake öncesi ICMP hataları **yumuşak**

```cpp
// soft_net_error:
ConnectionRefused, HostUnreachable, NetworkUnreachable, ConnectionReset
```

Handshake **tamamlanmadıysa** bunlar **fatal değil** — spoofable ICMP. Tamamlandıysa fatal.

### 10) Handshake sonrası `ConnectionRefused` fatal

Neden? Artık UDP connected socket, ICMP port-unreachable **gerçek** bir durum. Peer ölmüş demek. Session ölür.

### 11) `MAX_STREAMS` blocked

```cpp
rv = ngtcp2_conn_open_bidi_stream(m_conn, &sid, nullptr);
if (rv == NGTCP2_ERR_STREAM_ID_BLOCKED) break;   // wait for MAX_STREAMS
```

`extend_max_local_streams_bidi` callback **no-op** — pending her step retry. Yani blocked stream'ler bekler, sonraki adımda tekrar denenir.

### 12) `ngtcp2_conn_get_streams_bidi_left` — peer limit

Handshake sonrası:
```cpp
uint64_t left = ngtcp2_conn_get_streams_bidi_left(m_conn);
self->m_peer_max.store(left == 0 ? 1u : (left > 1000 ? 1000u : static_cast<uint32_t>(left)));
```

`m_peer_max` = pool `try_reserve` üst limiti. Peer 100 stream dedi → aynı anda 100 request.

### 13) Session resumption key `h3|host:port`

TLS session ticket cache h2 ile **paylaşılır** (`knst_net_detail::tls_sess()`), ama key prefix `h3|`. Yani h2 session h3'te kullanılmaz.

### 14) `native_app_glue` benzeri — Android NDK

`ngtcp2` Android'de build edilirken `-DNGTCP2_STATICLIB` gibi define'lar gerekir. CMake halleder (`H3=ON`).

### 15) `m_wblocked` — kernel UDP buffer dolu

```cpp
if (r == udp_t::r_t::Again) {
    m_pend.assign(m_wbuf.begin(), m_wbuf.begin() + nw);
    m_wblocked = true;
    break;
}
```

`m_pend` **driver-only**. Sonraki `write_packets()` önce `m_pend`'i flush eder. `m_wblocked` true iken `poll()` **POLLOUT** bekler.

### 16) `flush_locked` — son RST gönderme çabası

```cpp
// execute() sonunda:
if (need_flush) {
    if (m_driving) m_wake.signal(); else flush_locked(o.cancel_grace_ms);
}
```

Eğer kimse driver değilse ama RST_STREAM kuyrukta kaldıysa — `flush_locked` **kendisi driver olur**, best-effort gönderir. Max 4 iterasyon + grace_ms.

### 17) `m_retired` — pool'a dönmesin

```cpp
if (!o.use_keepalive) s->retire();
```

`use_keepalive=false` → `m_retired=true`. `is_usable()` false döner. Session pool'dan çıkarılır, bir sonraki request yeni session açar.

### 18) `h3_backoff` per-origin ama **global**

`m_bad` map paylaşılır. Yani thread A origin X için backoff başlattıysa thread B de origin X için atlar. **Request-spesifik değil.**

### 19) `strict` = POST + on_body

POST + `on_body` set ise strict **her ikisinden** biri true olur. Bu durumda timeout'ta fallback **yok**.

### 20) `knst_http_auto_client` **cookie paylaşımı yok**

h3'te `Set-Cookie` aldıysan h1'e düşerse o cookie **gönderilmez**. Her protokolün jar'ı ayrı. Bu **tarayıcı davranışıyla** uyumlu (origin başına).

**İhtiyaç varsa:** `ac.h1().set_header(u"Cookie", ...)` manuel.

---

## 📊 8) Performans

| İşlem | Maliyet | Not |
|---|---|---|
| **Session create (ilk)** | 50-200 ms | DNS + UDP + QUIC handshake |
| **Session create (resume)** | 10-30 ms | TLS ticket + 0-RTT yok ama 1-RTT hızlı |
| **`execute` (multiplexed)** | 1-10 ms | Aynı session, yeni stream |
| **Stream submit** | O(headers) | QPACK encode |
| **Response parse** | O(headers + body) | nghttp3 |
| **`write_packets`** | O(packets) | Max 64/iterasyon |
| **`read_packets`** | O(packets) | Max 64/iterasyon |
| **`scan`** | O(streams) | Her step |
| **UDP RTT** | 1-100 ms | Ağ bağlı |

**Tipik gecikmeler:**
- **İlk HTTP/3 GET**: 100-500 ms (DNS + handshake + 1 RTT)
- **Keep-alive multiplexed GET**: 10-50 ms
- **Aynı session N stream paralel**: RTT'ler üst üste biner

**Optimizasyon:**
- **`o.use_keepalive = true`** (default) — QUIC handshake maliyeti amortize
- **`o.max_sessions_per_origin`** — birden fazla QUIC connection paralel
- **`o.max_concurrent_streams`** — peer limit'i geçme
- **`o.connection_window`** ve **`o.initial_stream_window`** — buffer tuning
- **`o.total_timeout_ms`** — slow-drip koruması

---

## ✅ 9) Do / ❌ Don't

### ✅ Do

```cpp
// 1. auto_client kullan (fallback ile)
knst_http_auto_client ac;
ac.get(u"https://api.example.com/data", o, r, err);

// 2. h3 için agresif timeout (UDP bloklu olabilir)
o.timeout_ms = 3000;            // h3 hızlı düşsün
o.total_timeout_ms = 15000;

// 3. Streaming için FILE* user
o.on_body      = on_chunk;
o.on_body_user = file_ptr;

// 4. mTLS main'in başında
knst_tls_set_client_cert("client.pem", "client.key");

// 5. Her protokolün state'i ayrı — manuel kontrol
ac.h2().set_header(u"X-Test", u"1");   // sadece h2
ac.h1().set_header(u"X-Test", u"1");   // sadece h1

// 6. Backoff'u uzat (kurumsal ağda UDP engelli)
ac.set_h3_backoff(600000);   // 10 dk

// 7. Debug için idle_sessions()
std::cout << "h3 sessions: " << ac.h3().idle_sessions() << "\n";
```

### ❌ Yapma

```cpp
// 1. http:// URL — NotSupported
c3.get(u"http://example.com", o, r, err);   // ❌

// 2. use_chunked_upload — InvalidArgument
o.use_chunked_upload = true;   // ❌ h3 için yok
c3.post(url, body, o, r, err);

// 3. Proxy ile h3 — NotSupported
o.proxy_url = u"http://proxy:8080";   // ❌ h3 CONNECT-UDP desteklemez
ac.get(url, o, r, err);   // auto_client h3'ü atlar, h2/h1'e geçer

// 4. on_body içinde client'a re-entry
static bool on_chunk(...) {
    c3.get(...);   // ❌ driver thread'de deadlock
    return true;
}

// 5. Stok OpenSSL 3.0 ile
// ❌ ngtcp2_crypto_quictls include fail
// ✅ quictls kullan

// 6. Strict POST + timeout
o.timeout_ms = 1000;   // ❌ POST timeout → fallback yok, hata döner
ac.post(url, body, o, r, err);

// 7. Alt-Svc beklemek
// ❌ Alt-Svc: h3 header'ı OKUNMAZ
// ✅ Ya prior knowledge ya auto_client

// 8. Session'ı elle yönetmek
knst_h3_session::create(...);   // pool dışında
// ✅ knst_http3_client kullan, pool idare eder
```

---

## 🔥 10) Gerçek Kullanım Örnekleri

### Örnek 1: Basit h3 fallback

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

### Örnek 2: h3-only, kontrolü bende

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

### Örnek 3: Streaming download

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

### Örnek 4: POST + JSON (JSON opsiyonel)

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

### Örnek 5: h3 + SSRF guard

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

### Örnek 6: Birden fazla origin paralel

```cpp
knst_http_auto_client ac;   // thread-safe (session pool mutex'li)

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

### Örnek 7: mTLS + h3

```cpp
// main() başında
knst_tls_set_client_cert("/etc/certs/client.pem", "/etc/certs/client.key");

// Sonra normal h3
knst_http3_client c3;
knst_web_options o;
o.timeout_ms       = 10000;
o.total_timeout_ms = 30000;

knst_http_response r;
knst_network_error err;
c3.get(u"https://mtls.example.com/api", o, r, err);
```

### Örnek 8: Debug — hangi protokol?

```cpp
knst_http_auto_client ac;
knst_web_options o;
o.timeout_ms       = 3000;
o.total_timeout_ms = 15000;

knst_http_response r;
knst_network_error err;

const uint32_t h3_before = ac.h3().idle_sessions();
const uint32_t h2_before = ac.h2().idle_sessions();   // h2() metodu da var

if (ac.get(u"https://example.com", o, r, err)) {
    const uint32_t h3_after = ac.h3().idle_sessions();
    const uint32_t h2_after = ac.h2().idle_sessions();

    if (h3_after > h3_before) std::cout << "h3 kullanıldı\n";
    else if (h2_after > h2_before) std::cout << "h2'ye fallback\n";
    else std::cout << "h1 kullanıldı\n";
}
```

---

## 📌 11) API Kontrat Özeti

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

## 🎯 Size Getireceği Kazanç

`knst_network_h3` şu durumlarda ciddi avantaj sağlar:

- ✅ **HTTP/3 desteği** — modern CDN/cloud (Cloudflare, Google, AWS) h3'e hazır
- ✅ **Multiplexing** — tek QUIC bağlantı, N paralel stream (HOL blocking yok)
- ✅ **Hızlı handshake** — TLS 1.3 + QUIC birleşik handshake (1 RTT)
- ✅ **Session resumption** — `h3|host:port` anahtarıyla ticket cache
- ✅ **QPACK** — header compression, dinamik tablo
- ✅ **PMTUD** — path MTU discovery, large packet optimization
- ✅ **Otomatik fallback** — `knst_http_auto_client` ile h3 → h2 → h1
- ✅ **Per-origin backoff** — UDP bloklu ağda 1 timeout, N timeout değil
- ✅ **Replay guard** — non-idempotent POST'lar için güvenli fallback
- ✅ **SSRF guard** — `allow_private_networks=false` h3'te de çalışır
- ✅ **mTLS** — client cert desteği (h2 ile paylaşılır)
- ✅ **`knst_network_h2.hpp` uyumu** — aynı `knst_web_stream`, pool kontratı
- ✅ **KernelNucleusT entegrasyonu** — `knst_c16string`, `knst_byte_string`, `knst_json`
- ✅ **HTTP/1.1 fallback** — `knst_http_auto_client` üzerinden otomatik

---