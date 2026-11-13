# knst_websocket — WebSocket İstemcisi Kütüphanesi

Selamlar! 👋 Bu doküman `knst_websocket` sınıfının ne olduğunu, nasıl kullanıldığını ve hangi durumlarda işine yarayacağını anlatır.

**Kısaca:** RFC 6455 uyumlu, istemci tarafı bir WebSocket implementasyonu. `ws://` ve `wss://` destekler. Proxy, DNS cache, SSRF guard, TLS gibi ağ katmanı işlerini `knst_http_client`'a devreder — sen sadece bağlan, mesaj gönder/al. Alt seviyede **tam RFC 6455 frame codec** var: text, binary, ping, pong, close, fragmentation, masking — hepsi.

---

## ✨ Öne Çıkan Özellikler

| Özellik | Ne kazandırır? |
|---|---|
| **ws:// ve wss://** | TLS otomatik (`KNST_NETWORK_ENABLE_TLS` ile) |
| **HTTP proxy (CONNECT)** | `knst_http_client::options` üzerinden |
| **SOCKS5 / SOCKS5h** | Proxy türü seçilebilir |
| **NO_PROXY desteği** | Proxy bypass listesi |
| **DNS cache** | `knst_http_client`'tan gelir |
| **bind_local_ip** | Çıkış IP'si sabitlenebilir |
| **SSRF guard** | İç ağa istek koruması |
| **RFC 6455 frame codec** | Text/Binary/Ping/Pong/Close + fragmentation |
| **Client masking** | Zorunlu, otomatik (16 KiB chunk) |
| **Auto-pong** | Gelen Ping'e otomatik Pong (kapatılabilir) |
| **Graceful close** | Sunucu close'una otomatik echo |
| **Fragment assembly** | Parçalı mesajlar tek mesaj olarak döner |
| **Boyut limitleri** | Frame ve mesaj başına ayrı limit |
| **Sec-WebSocket-Accept** | SHA-1 ile doğrulanır |
| **Handshake buffer** | 101 ile aynı segmentte gelen frame kaybolmaz |
| **JSON yardımcıları** | Opsiyonel `send_json` / `recv_json` |
| **Move-only** | Copy yasak, move serbest |

---

## 🚀 Hızlı Başlangıç

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    knst_websocket ws;
    knst_websocket::options opt;

    knst_network_error err = knst_network_error::None;

    if (!ws.connect(u"wss://echo.websocket.org/", opt, err)) {
        std::cerr << "Bağlantı hatası\n";
        return 1;
    }

    if (!ws.send_text(u"merhaba dünya", err)) {
        std::cerr << "Gönderim hatası\n";
        return 1;
    }

    knst_websocket::message msg;
    if (ws.recv(msg, /*timeout_ms=*/5000, err)) {
        std::cout << "Gelen: " << msg.data.data() << "\n";
    }

    ws.close(1000, u"bye");
}
```

Bu kadar! `connect → send → recv → close` dört adım. Gerisi detay.

---

## 📋 Genel API

| Kategori | Metodlar |
|---|---|
| **Bağlantı** | `connect(url, options, err)`, `close(code, reason)`, `is_open()` |
| **Gönderme** | `send_text`, `send_binary`, `send_ping`, `send_pong`, `send_json`* |
| **Alma** | `recv(message&, timeout_ms, err)`, `recv_json`* |
| **Durum** | `close_code()`, `close_reason()`, `negotiated_subprotocol()`, `negotiated_extensions()`, `last_message_was_fragmented()` |
| **Tipler** | `opcode`, `options`, `message` |

> `*` JSON yardımcıları sadece `KNST_NETWORK_ENABLE_JSON` tanımlıysa ve `knst_json.hpp` include edildiyse çalışır.

---

## 🔧 1) `options` Yapısı — Tüm Ayarlar Burada

```cpp
struct knst_websocket::options {
    knst_http_client::options http;      // tüm ağ katmanı burada
    knst_c16string subprotocols;         // "chat, superchat" gibi
    knst_c16string origin;               // Origin header
    bool auto_pong = true;               // gelen Ping'e otomatik Pong
    uint32_t max_message_bytes = 16 * 1024 * 1024;  // 16 MiB
    uint32_t max_frame_bytes   = 4  * 1024 * 1024;  // 4 MiB
};
```

### Ağ ayarları — `options.http` içinden

`knst_http_client::options` üzerinden gelen ayarlar (hepsi WebSocket için de geçerli):

- `timeout_ms` → bağlantı, okuma, yazma timeout'ları (varsayılan 10 sn)
- `user_agent` → `User-Agent` header
- `headers` → ek HTTP header'lar (auth token vs.)
- `max_header_bytes` → HTTP response header limiti
- Proxy: `proxy_url`, `proxy_type`, `no_proxy`
- TLS: `verify_peer`, `ca_file`, `client_cert`
- DNS: `dns_cache_ttl`, `bind_local_ip`
- SSRF: `ssrf_guard`, `allow_private_networks`

Detaylar için `knst_http_client` dokümanına bak.

### `subprotocols`

Virgülle ayrılmış liste. Sunucu hangisini seçtiğini `Sec-WebSocket-Protocol` ile döner. Boş bırakırsan header hiç gönderilmez.

```cpp
opt.subprotocols = u"graphql-ws";

// veya birden fazla
opt.subprotocols = u"chat, superchat";
```

Sunucunun cevabını `negotiated_subprotocol()` ile alırsın.

### `origin`

Tarayıcıların gönderdiği `Origin` header. Bazı sunucular (özellikle CORS/CSRF kontrolü yapanlar) bunu zorunlu tutar. Boş bırakırsan hiç gönderilmez.

```cpp
opt.origin = u"https://app.example.com";
```

### `auto_pong`

`true` (varsayılan): gelen PING frame'ine otomatik PONG gönderir. `false`: PING'i manuel yönetirsin.

### `max_message_bytes`

Gelen mesajın **reassembly sonrası** toplam boyutu. Aşılırsa **1009 (Message Too Big)** close frame'i gönderilir ve `MessageTooLarge` döner. Varsayılan 16 MiB.

### `max_frame_bytes`

Hem giden frame'lerin hem gelen frame'lerin **tek frame** boyutu. Varsayılan 4 MiB.

- Giden: `send_data()` bu limitten büyük payload'ları otomatik böler.
- Gelen: bu limitten büyük frame → `MessageTooLarge`.

---

## 🔌 2) Bağlanma

### Basit bağlantı

```cpp
knst_websocket ws;
knst_websocket::options opt;
knst_network_error err;

if (!ws.connect(u"wss://api.example.com/ws", opt, err)) {
    // err: NotSupported, ProtocolError, HttpMalformedHeaders vs.
}
```

### Subprotocol ile

```cpp
opt.subprotocols = u"graphql-ws";
ws.connect(u"wss://api.example.com/graphql", opt, err);

// Sunucu hangi subprotocol'ü seçti?
knst_c16string chosen = ws.negotiated_subprotocol();
```

### Origin ile (tarayıcı benzeri)

```cpp
opt.origin = u"https://app.example.com";
```

### Proxy üzerinden

```cpp
opt.http.proxy_url  = u"socks5://127.0.0.1:1080";
opt.http.proxy_type = knst_proxy_type::Socks5;

// veya SOCKS5h (DNS'i proxy'de çözer — daha güvenli)
opt.http.proxy_url  = u"socks5h://proxy.local:9050";
opt.http.proxy_type = knst_proxy_type::Socks5h;

// Bypass listesi
opt.http.no_proxy = u"localhost,127.0.0.1,*.internal";
```

### Auth header + custom header

```cpp
opt.http.headers.push_back({u"Authorization", u"Bearer eyJhbGci..."});
opt.http.headers.push_back({u"X-Client-Version", u"1.2.3"});
opt.http.user_agent = u"MyApp/1.0";
```

### Zaman aşımları

```cpp
opt.http.timeout_ms = 15000;   // 15 sn
```

> ⚠️ Bu timeout **her socket işlemi** için geçerli. `connect()` sırasında handshake için de kullanılır.

### Ne oluyor içeride?

1. **URL doğrulanır.** `knst_url::parse()` ile ayrıştırılır. Scheme **sadece `ws` veya `wss`** olabilir, aksi halde `NotSupported` döner.
2. **Fragment silinir.** URL'de `#fragment` varsa silinir (RFC 6455 §3 — fragment sunucuya gönderilmez).
3. **Scheme çevrilir.** Alt katman (knst_http_client) için: `ws://` → `http://`, `wss://` → `https://`.
4. **Ham TCP.** `knst_http_client::open_raw_socket()` çağrılır — proxy/TLS/DNS hepsi otomatik.
5. **Key üretilir.** 16 byte rastgele `Sec-WebSocket-Key`.
6. **HTTP upgrade isteği gönderilir.** `GET ... HTTP/1.1` + `Upgrade: websocket` + `Connection: Upgrade` + `Sec-WebSocket-Key` + `Sec-WebSocket-Version: 13` + opsiyonel `Sec-WebSocket-Protocol` / `Origin` / `User-Agent` + custom headers.
7. **Yanıt okunur.** `\r\n\r\n`'e kadar (`read_http_head`).
8. **101 kontrol edilir.** Değilse `ProtocolError`.
9. **Upgrade + Connection kontrol edilir.** Yoksa `ProtocolError`.
10. **Sec-WebSocket-Accept doğrulanır.** `base64(SHA-1(key + magic))` hesabıyla eşleşmeli. Magic = `"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"`. Uymazsa `ProtocolError`.
11. **Subprotocol / Extensions kaydedilir.** Sunucu döndürdüyse `m_subprotocol` / `m_extensions`'a yazılır.
12. **Handshake buffer.** 101 yanıtıyla birlikte gelen fazladan byte'lar (bazı sunucular ilk frame'i aynı segmentte gönderir) `m_prebuf`'a alınır — **kaybolmaz**.

Hata durumunda socket kapatılır, `err` set edilir.

### 🎁 Handshake buffer neden önemli?

RFC 6455 §4.2.2 der ki: sunucu 101'i yazdıktan **hemen sonra** WebSocket frame gönderebilir — hatta aynı TCP segmentinde bile.

```
[HTTP/1.1 101 Switching Protocols\r\n...\r\n\r\n][0x81 0x05 "hello"]  ← tek segment
```

Naif bir implementasyon `\r\n\r\n`'den sonrasını çöpe atar. `knst_websocket` atmaz: `read_http_head` `leftover` olarak yakalar, `m_prebuf`'a koyar, ilk `recv()` çağrısı oradan okur. Yani sunucu ne kadar hızlı olursa olsun frame kaybolmaz. 👌

---

## 📤 3) Gönderme

Tüm gönderme metodları `knst_network_error&` alır ve `bool` döner.

### `send_text` — UTF-16 → UTF-8 → Text frame

```cpp
ws.send_text(u"merhaba dünya", err);
```

İçeride:
1. `knst_net_detail::to_utf8()` çağrılır (`knst_c16string` → `knst_byte_string`)
2. `send_data(0x1, ...)` çağrılır (opcode 0x1 = Text)

### `send_binary` — Ham byte → Binary frame

```cpp
knst_byte_string blob = /* ... */;
ws.send_binary(blob, err);
```

Opcode 0x2 (Binary). Ham byte gider — UTF-8 dönüşümü **yok**.

### `send_ping` / `send_pong` — Control frame

```cpp
ws.send_ping(knst_byte_string(), err);          // boş payload
ws.send_pong(knst_byte_string(), err);          // boş payload

// veya payload ile
knst_byte_string p = /* ≤ 125 byte */;
ws.send_ping(p, err);
```

> ⚠️ Control frame payload **en fazla 125 byte** olabilir (RFC §5.5.2). Aşarsan `MessageTooLarge` döner.

### Otomatik fragmentasyon

`send_text` / `send_binary` içinde `send_data()` devreye girer:

- Payload `max_frame_bytes`'tan küçükse → **tek frame** (FIN=1)
- Büyükse → `max_frame_bytes`'lık parçalara bölünür:
  - İlk parça: asıl opcode (0x1/0x2), FIN=0
  - Ara parçalar: opcode 0x0 (Continuation), FIN=0
  - Son parça: opcode 0x0, FIN=1

Örnek: 10 MB binary, `max_frame_bytes=4 MiB` → 3 frame:

```
[Binary FIN=0, 4 MiB] [Continuation FIN=0, 4 MiB] [Continuation FIN=1, 2 MiB]
```

Sen hiçbir şey yapmıyorsun — API seviyesinde tek `send_binary` çağrısı yeterli. RFC §5.4 kuralları doğru uygulanır.

### Client masking (RFC §5.3 — Zorunlu)

Client'tan sunucuya giden **her frame** maskelenir:

- 4 byte rastgele mask key (`knst_ws_detail::random_bytes`)
- Payload 16 KiB chunk'lar halinde XOR'lanır

Sen hiçbir şey yapmıyorsun, otomatik. 🔒

### `send_json` (JSON modülü açıksa)

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
    knst_json j = /* ... */;

    // Kompakt
    ws.send_json(j, err, /*indent=*/0);

    // Pretty
    ws.send_json(j, err, /*indent=*/2);
#endif
```

İçeride:
1. `j.dump()` çağrılır → UTF-8 buffer
2. UTF-8 buffer → `knst_c16string`
3. `send_text()` çağrılır

Yani JSON mesajı **Text frame** olarak gider.

### `can_send()` ön kontrolü

Tüm `send_*` metotları önce `can_send()` çağırır:

- Soket yoksa → `NotOpen`
- CLOSE gönderilmişse → `ConnectionClosed`
- CLOSE alınmışsa → `ConnectionClosed`

Bu sayede kapalı bağlantıya yazma denemesi net hata döner.

---

## 📥 4) Alma

```cpp
knst_websocket::message msg;
if (!ws.recv(msg, /*timeout_ms=*/5000, err)) {
    // err: NotOpen, ConnectionClosed, ProtocolError, MessageTooLarge, OutOfMemory, Timeout
    return;
}

if (msg.type == knst_websocket::opcode::Text) {
    knst_c16string text(reinterpret_cast<const char*>(msg.data.data()), msg.data.length());
    std::cout << text << "\n";
} else if (msg.type == knst_websocket::opcode::Binary) {
    // ham byte
}
```

### `message` yapısı

```cpp
struct knst_websocket::message {
    opcode type = opcode::Text;   // Text veya Binary
    knst_byte_string data;         // Ham byte
};
```

> ⚠️ **`msg.type` sadece `Text` veya `Binary`** olabilir. Ping/Pong/Close asla buraya gelmez — aşağıda görüyorsun.

### `recv` ne yapar? (Adım adım)

Tek bir **mesaj** döner (frame değil). İçeride:

**Ping geldiyse:**
- `auto_pong` açıksa → otomatik Pong gönderir
- Sana **göstermez**, döngüye devam eder

**Pong geldiyse:**
- Sessizce yutar
- Sana **göstermez**, döngüye devam eder

**Close geldiyse:**
- `close_code` / `close_reason` set edilir
- Eğer biz henüz CLOSE göndermediysek, karşı tarafa close echo'lanır (aynı kod, kod yoksa boş)
- Socket kapatılır
- `ConnectionClosed` hatası döner

**Text/Binary geldiyse:**
- `FIN=1` → tek frame, direkt döner (`last_message_was_fragmented = false`)
- `FIN=0` → `out.data`'ya koy, `assembling=true`, `Continuation` frame'lerini toplamaya başla

**Continuation geldiyse:**
- `assembling` değilse → **1002 ProtocolError**
- `out.data`'ya ekler
- `FIN=1`'de döner (`last_message_was_fragmented = true`)

**Başka bir data frame araya girerse** (Continuation yerine Text/Binary):
- **1002 ProtocolError** (RFC §5.4 ihlali)

**Toplam boyut `max_message_bytes`'ı aşarsa:**
- **1009 (Message Too Big)** close frame'i gönderilir
- `MessageTooLarge` döner

**Protokol hatası:**
- **1002 (Protocol Error)** close frame'i gönderilir
- `ProtocolError` döner

### ⚠️ BÜYÜK TUZAK: Ping/Pong'u göremezsin

Yukarıda gördüğün gibi `recv()` **PING ve PONG'u göstermiyor** — sessizce yönetiyor. Bu bir tasarım tercihi.

**Sonuç:** `send_ping()` çağırıp PONG cevabını `recv()` üzerinden **bekleyemezsin**. Latency ölçümü, heartbeat doğrulaması bu API ile **mümkün değil**.

```cpp
// ❌ BU İŞE YARAMAZ:
ws.send_ping(payload, err);
ws.recv(msg, 3000, err);   // PONG değil, sonraki Text/Binary gelir

// ✅ PING/PONG'u manuel yönetmek istiyorsan:
opt.auto_pong = false;
ws.connect(url, opt, err);

ws.send_ping(payload, err);

// Ama recv() hâlâ PONG'u yutuyor — bir işe yaramıyor
// Heartbeat/latency bu API ile yapılmaz.
```

### Fragmented bilgisi

```cpp
if (ws.last_message_was_fragmented()) {
    // Bu mesaj birden fazla frame'den birleştirildi
}
```

### Timeout davranışı

`recv(msg, timeout_ms, err)`:

- `timeout_ms == 0` → `m_recv_timeout` kullanılır (varsayılan 10 saniye)
- `timeout_ms > 0` → **her socket read** için timeout

> ⚠️ **Önemli:** Timeout **her read işlemi** için geçerli, mesajın tamamı için değil. Yani `timeout_ms=3000` verip server 3 saniyede bir mesaj gönderirse, `recv()` **3 saniyeyi aşan bir mesajı** yine de tamamlayabilir. Yavaş büyük mesajlar `timeout_ms`'den uzun sürebilir.

### `recv_json` (JSON modülü açıksa)

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
    knst_json j;
    if (ws.recv_json(j, 5000, err)) {
        // j hazır
    }
#endif
```

İçeride:
1. `recv()` çağırır
2. `msg.type` Text veya Binary olmalı (değilse `ProtocolError`)
3. `knst_json::parse()` ile parse eder
4. Parse hatası → `ProtocolError`

---

## 🔒 5) Kapatma

### `close()` ne yapar?

```cpp
ws.close(1000, u"normal kapanış");
// 1) Close frame gönderir (code + reason, max 123 byte)
// 2) Hemen close_socket() çağırır
```

Zaten kapalıysa hiçbir şey yapmaz (idempotent).

Reason UTF-8 encode edilir, max 123 byte (2 byte code + 123 byte reason = 125 byte = control frame limiti).

> ⚠️ **ÖNEMLİ:** `close()` sunucunun CLOSE echo'sunu **beklemez**. CLOSE frame'i yazar, hemen socket'i kapatır. Sunucu tarafında "abort" gibi görünebilir. RFC 6455 ideal olarak echo beklenmesini söyler ama bu implementasyon hızlı kapatıyor. Zarifçe kapatmak istiyorsan `close()` sonrası `recv()` bir kez çağırıp echo'yu bir süre bekle.

### Sunucudan CLOSE gelirse

`recv()` içinde otomatik:

1. `m_close_received = true`
2. `m_close_code`, `m_close_reason` doldurulur
3. Payload ≥ 2 byte ise code okunur, else `1005` (kod yok)
4. Biz henüz CLOSE göndermediysek, echo CLOSE gönderilir
5. `close_socket()` çağrılır
6. `ConnectionClosed` döner

> ⚠️ **1005 wire'a yazılmaz** (RFC §7.4.1). Sunucu boş CLOSE gönderdiyse biz de boş CLOSE yollarız.

### Close code'ları (RFC §7.4)

| Kod | Anlam |
|---|---|
| **1000** | Normal kapanış (varsayılan) |
| **1001** | Going away (sayfa kapanıyor vs.) |
| **1002** | Protocol error (RFC ihlali) |
| **1003** | Unsupported data |
| **1005** | Kod yok (API'de `close_code()` dönebilir, wire'a yazılmaz) |
| **1009** | Message too big |

### `is_open()` durumu

`true` sadece:

- Socket açık **ve**
- Close gönderilmemiş **ve**
- Close alınmamış

### Yıkıcı (Destructor)

```cpp
~knst_websocket() { close_socket(); }
```

Yıkıcı **CLOSE frame göndermez** — sadece socket'i kapatır. Zarifçe kapatmak için `close()` çağırman gerek.

### `close()` iki kere güvenli

```cpp
ws.close(1000);
ws.close(1000);  // no-op, ikinci kez frame göndermez
```

---

## 🧠 6) Durum Erişicileri

```cpp
ws.is_open();                       // hâlâ konuşabilir miyiz?
ws.close_code();                    // 1000, 1001, 1005, ...
ws.close_reason();                  // "normal" vs.
ws.negotiated_subprotocol();        // sunucu hangi subprotocol'ü seçti
ws.negotiated_extensions();         // sunucu hangi extension'ı kabul etti
ws.last_message_was_fragmented();   // son mesaj parçalı mıydı?
```

Hepsi `const noexcept` — güvenle çağırabilirsin.

---

## ⚠️ 7) Bilinmesi Gerekenler (Sık Tuzaklar)

### 1) `recv()` **bloklama**dır

Bir mesaj gelene kadar bekler. Timeout `timeout_ms` (0 verirsen `options.http.timeout_ms`, o da 0 ise 10 sn).

### 2) Copy yasak, move serbest

```cpp
knst_websocket a;
knst_websocket b = std::move(a);   // ✅
knst_websocket c = a;              // ❌ derlenmez
```

Sebep: Soket FD sahipliği net, kopyalama iki nesneye aynı FD'yi verir → double-close tehlikesi.

Move sırasında tüm state (socket, close bilgisi, timeout'lar) taşınır; kaynak `a` sıfırlanır.

### 3) Thread-safety **yok**

Aynı `knst_websocket`'ı birden fazla thread'den kullanma. Her thread için ayrı instance (veya mutex + tek instance).

### 4) Boyut limitleri

- **Frame** başına: `max_frame_bytes` (varsayılan 4 MiB) — aşarsa `MessageTooLarge`
- **Mesaj** başına: `max_message_bytes` (varsayılan 16 MiB) — reassembly sonrası aşarsa **1009 close**

Sunucu 4 MiB'den büyük frame gönderirse **hemen reddedilir** (mesaj olarak birleştirilmez). Bu bilinçli bir tasarım: DoS koruması.

### 5) Server masking yasak

Server → client frame'leri **mask'siz** olmalı (RFC 6455). Maskeli gelirse `ProtocolError`.

### 6) `random_bytes` kriptografik **değil**

```cpp
static std::mt19937_64 rng = /* seed: random_device + steady_clock + thread_id + time */;
```

`std::mt19937_64` **CSPRNG değil**. Masking key için RFC 6455 bunu kabul ediyor (kriptografik olması şart değil, tahmin edilemez olması yeterli). Ama:

- **Sec-WebSocket-Key** için de aynı `random_bytes` kullanılıyor — pratikte yeterli.
- **Kritik kriptografi** için kullanma (şifreleme, token üretimi). Onun için ayrı bir modül kullan.

### 7) Pool allocator yok

Her frame için `knst_byte_string::resize()` çağrılır → default allocator. Çok sayıda küçük mesaj alıyorsan performans kritik olabilir. Custom pool için API açık değil.

### 8) `close()` echo beklemez

Yukarıda detaylı anlattık. Sunucu tarafında "abort" gibi görünebilir.

### 9) Ping/Pong görünmez

`recv()` sessizce yönetiyor. Heartbeat için bu API uygun değil.

### 10) Timeout per-read, per-message değil

Timeout her `socket read` için ayrı ayrı uygulanır. Yavaş büyük mesajlar `timeout_ms`'i aşabilir.

---

## 📡 8) Kullanım Örnekleri

### Örnek 1: Echo testi

```cpp
knst_websocket ws;
knst_websocket::options opt;
knst_network_error err;

if (!ws.connect(u"wss://echo.websocket.org/", opt, err)) return;

ws.send_text(u"ping test", err);

knst_websocket::message msg;
if (ws.recv(msg, 3000, err)) {
    std::cout << "echo: " << msg.data.data() << "\n";
}
ws.close();
```

### Örnek 2: Binary dosya parçası gönderme

```cpp
knst_byte_string chunk = /* 10 MB binary */;
ws.send_binary(chunk, err);
// send_data() max_frame_bytes (4 MiB) ile otomatik 3 frame'e böler
```

### Örnek 3: Proxy arkasından bağlanma

```cpp
knst_websocket::options opt;
opt.http.proxy_url  = u"socks5h://proxy.local:9050";
opt.http.proxy_type = knst_proxy_type::Socks5h;
opt.http.no_proxy   = u"localhost,127.0.0.1";

ws.connect(u"wss://private-api.example.com/ws", opt, err);
```

### Örnek 4: Auth header + subprotocol

```cpp
knst_websocket::options opt;
opt.subprotocols = u"graphql-ws";
opt.http.headers.push_back({u"Authorization", u"Bearer eyJhbGci..."});
opt.http.headers.push_back({u"X-Client-Version", u"1.2.3"});

ws.connect(u"wss://api.example.com/graphql", opt, err);

knst_c16string chosen = ws.negotiated_subprotocol();
// "graphql-ws" veya boş
```

### Örnek 5: Büyük mesaj gönderme (fragmentation)

```cpp
knst_websocket::options opt;
opt.max_frame_bytes   = 64 * 1024;             // her frame 64 KB
opt.max_message_bytes = 100 * 1024 * 1024;     // 100 MB'a kadar al

ws.connect(u"wss://upload.example.com/stream", opt, err);

knst_byte_string big_blob = /* 50 MB */;
ws.send_binary(big_blob, err);   // ~800 frame'e bölünür, sen uğraşmıyorsun
```

### Örnek 6: JSON chat

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
    knst_json msg;
    knst_json::object obj;
    obj[u"type"]    = knst_json(u"message");
    obj[u"content"] = knst_json(u"selam");
    msg = knst_json(obj);

    ws.send_json(msg, err);

    knst_json incoming;
    if (ws.recv_json(incoming, 5000, err)) {
        // incoming hazır
    }
#endif
```

### Örnek 7: Loop içinde mesaj alma

```cpp
knst_websocket::message msg;

while (ws.is_open()) {
    if (!ws.recv(msg, 1000, err)) {
        if (err == knst_network_error::Timeout) {
            // 1 saniye mesaj gelmedi, devam et
            continue;
        }
        if (err == knst_network_error::ConnectionClosed) {
            std::cout << "Kapandı: " << ws.close_code()
                      << " (" << ws.close_reason() << ")\n";
            break;
        }
        // diğer hatalar
        break;
    }

    if (msg.type == knst_websocket::opcode::Text) {
        std::cout << "Text: " << msg.data.data() << "\n";
    } else if (msg.type == knst_websocket::opcode::Binary) {
        std::cout << "Binary: " << msg.data.length() << " byte\n";
    }
}
```

### Örnek 8: En az hata kontrolü ile tam kullanım

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    knst_websocket ws;
    knst_network_error err;

    knst_websocket::options opt;
    opt.http.timeout_ms = 5000;
    opt.auto_pong = true;
    opt.subprotocols = u"chat";

    if (!ws.connect(u"wss://chat.example.com/ws", opt, err)) {
        std::cerr << "Connect: " << knst_network_error_string(err) << "\n";
        return 1;
    }

    // İlk mesajı gönder
    if (!ws.send_text(u"merhaba", err)) {
        std::cerr << "Send: " << knst_network_error_string(err) << "\n";
        ws.close();
        return 1;
    }

    // 10 mesaj al
    for (int i = 0; i < 10; ++i) {
        knst_websocket::message msg;
        if (!ws.recv(msg, 5000, err)) {
            if (err == knst_network_error::Timeout) {
                std::cout << "Timeout, sonraki tur\n";
                continue;
            }
            std::cerr << "Recv: " << knst_network_error_string(err) << "\n";
            break;
        }

        std::cout << "[" << i << "] " << msg.data.data() << "\n";
    }

    ws.close(1000, u"done");
    return 0;
}
```

---

## 🐛 9) Hata Yönetimi

`knst_network_error` enum'u üzerinden:

| Hata | Ne zaman? |
|---|---|
| `None` | Başarılı |
| `NotSupported` | `ws://`/`wss://` değil |
| `ProtocolError` | Handshake veya frame formatı bozuk |
| `ConnectionClosed` | Sunucu close gönderdi / bağlantı koptu |
| `NotOpen` | Bağlı değilken gönderme denemesi |
| `MessageTooLarge` | Frame veya mesaj limiti aşıldı |
| `OutOfMemory` | Bellek yetmedi |
| `HttpMalformedHeaders` | 101 yanıtı bozuk |
| `Timeout` | Belirtilen süre içinde okuma tamamlanmadı |
| `DnsError` / `ConnectError` / `TlsError` | `knst_http_client`'tan gelen |

```cpp
if (!ws.recv(msg, 1000, err)) {
    if (err == knst_network_error::ConnectionClosed) {
        std::cout << "Sunucu kapattı: " << ws.close_code()
                  << " (" << ws.close_reason() << ")\n";
    }
}
```

---

## 📊 10) Performans Notları

| İşlem | Karmaşıklık | Not |
|---|---|---|
| `connect` | O(1) + handshake RTT | 1 round-trip + SHA-1 |
| `send_text` / `send_binary` (küçük) | O(n) | Frame bölme yok, maskeleme var |
| `send_text` / `send_binary` (büyük) | O(n) + k×RTT | k = parça sayısı |
| `recv` (tek frame) | O(n) | Frame decode |
| `recv` (fragmented) | O(total) | Reassembly |
| Masking | O(n) | 16 KiB chunk |
| SHA-1 verify | O(1) | Sadece handshake'te |

### İpuçları

- Küçük sık mesajlar için `max_frame_bytes`'ı **düşürme** (default 4 MiB iyi).
- Büyük blob'ları gönderirken `max_frame_bytes`'ı sunucunun kabul ettiği kadar ayarla.
- `send_binary` çağrısı **bloklar** — büyük gönderimde UI thread'i dondurma, worker thread kullan.
- `recv()` de **bloklar** — timeout vererek periyodik kontrol yap.

---

## 🌍 11) Platform Farkları

`knst_websocket` **taşınabilir** — Linux / Windows / Android / macOS hepsinde çalışır. Farklar alt katman `knst_http_client`'tan geliyor:

| Konu | Linux/macOS | Windows |
|---|---|---|
| Socket API | POSIX `socket()` | Winsock2 |
| TLS | OpenSSL | OpenSSL (aynı API) |
| DNS | `getaddrinfo` | `getaddrinfo` (aynı) |
| Proxy | Aynı | Aynı |

Yani WebSocket katmanı için **platform farkı yok** — hepsi `knst_http_client`'ta hallediliyor.

---

## 🎯 12) Ne Zaman Kullanmalı?

`knst_websocket` özellikle şu senaryolarda işine yarar:

- ✅ **Gerçek zamanlı chat / mesajlaşma** — metin frame'leri, fragmentation hazır
- ✅ **Canlı veri akışı** — borsa/fiyat/sensör akışı
- ✅ **Multiplayer oyun** — binary frame desteği
- ✅ **Uzak terminal / SSH-over-WS** — büyük binary blob'lar fragment edilir
- ✅ **Proxy'li kurumsal ağ** — SOCKS5/HTTP CONNECT otomatik
- ✅ **wss:// ile güvenli bağlantı** — TLS katmanı şeffaf
- ✅ **GraphQL subscription** — subprotocol header
- ✅ **KernelNucleusT uyumu** — `knst_c16string`, `knst_byte_string`, `knst_network_error` ile tam entegre

### Ne zaman **kullanmamalı**?

- ❌ **HTTP polling yeterliyse** — WebSocket fazla, `knst_http_client` kullan
- ❌ **Server-side WebSocket yazacaksan** — bu kütüphane **client-only**
- ❌ **Heartbeat/latency ölçümü kritikse** — `recv()` PING/PONG göstermiyor
- ❌ **Thread-safety şart ise** — aynı instance'ı birden fazla thread'den kullanma

---

## 🔥 13) RFC 6455 Uyumluluk

| Kural | Durum |
|---|---|
| §4.2.2 — 101 + ilk frame aynı segmentte olabilir | ✅ Destekli (`m_prebuf`) |
| §5.3 — Client MASK zorunlu | ✅ Her frame maskelenir |
| §5.3 — Server MASK yapmaz | ✅ Kontrol edilir, ihlal → `ProtocolError` |
| §5.4 — Fragmentation (Text/Binary → Continuation → FIN) | ✅ Otomatik |
| §5.5.2 — Control frame FIN=1, payload ≤ 125 byte | ✅ Zorlanır |
| §7.4 — Close codes (1000, 1002, 1005, 1009) | ✅ Destekli |
| §7.4.1 — 1005 wire'a yazılmaz | ✅ Kontrol edilir |
| §8.1 — UTF-8 text zorunlu | ✅ `send_text` UTF-8 gönderir |
| §4.1 — Sec-WebSocket-Accept = base64(SHA-1(key + magic)) | ✅ Kontrol edilir |

**Uyumsuzluk:** `close()` sunucu echo'sunu beklemez (hızlı kapanma tercihi). RFC "MAY" der, "MUST" değil — kabul edilebilir.

---

## ✅ 14) Yapılması Gerekenler / ❌ Kaçınılması Gerekenler

### ✅ Yap

```cpp
// 1. Bağlantı öncesi timeout ayarla
knst_websocket::options opt;
opt.http.timeout_ms = 5000;

// 2. recv() her zaman timeout ile çağır
knst_websocket::message msg;
ws.recv(msg, 3000, err);

// 3. Close code'u kontrol et
if (ws.close_code() == 1009) {
    // message too big
}

// 4. Move semantics kullan
knst_websocket ws2 = std::move(ws);

// 5. Handshake buffer'ı bil — server ilk frame'i hemen gönderebilir
ws.connect(url, opt, err);
ws.recv(msg, 1000, err);

// 6. Binary veri için send_binary kullan
ws.send_binary(blob, err);
```

### ❌ Yapma

```cpp
// 1. recv() üzerinden PING/PONG bekleme
ws.send_ping(payload, err);
ws.recv(msg, 3000, err);  // ❌ PONG değil, sonraki Text/Binary gelir

// 2. Timeout'suz recv
ws.recv(msg, 0, err);     // ⚠️ 10 saniye bekler (default)

// 3. close() sonrası send
ws.close();
ws.send_text(u"x", err);  // ❌ NotOpen

// 4. Copy constructor
knst_websocket a;
knst_websocket b = a;     // ❌ derlenmez — move-only

// 5. Binary veri için send_text
ws.send_text(u"\x01\x02\x03", err);   // ❌ UTF-16 string'e gömülü \0 → UTF-8'de kopar

// 6. Aynı instance'ı birden fazla thread'de kullanma
// ❌ Thread-safety yok
```

---

## 📌 15) API Kontrat Özeti (Hızlı Referans)

```cpp
class knst_websocket {
public:
    enum class opcode : uint8_t {
        Continuation = 0x0,
        Text         = 0x1,
        Binary       = 0x2,
        Close        = 0x8,
        Ping         = 0x9,
        Pong         = 0xA,
    };

    struct options {
        knst_http_client::options http;
        knst_c16string subprotocols;
        knst_c16string origin;
        bool auto_pong = true;
        uint32_t max_message_bytes = 16u * 1024 * 1024;
        uint32_t max_frame_bytes   = 4u  * 1024 * 1024;
    };

    struct message {
        opcode type = opcode::Text;
        knst_byte_string data;
    };

    knst_websocket() noexcept = default;
    ~knst_websocket() noexcept;
    knst_websocket(const knst_websocket&) = delete;
    knst_websocket& operator=(const knst_websocket&) = delete;
    knst_websocket(knst_websocket&&) noexcept;
    knst_websocket& operator=(knst_websocket&&) noexcept;

    bool connect(const knst_c16string& url, const options& o,
                 knst_network_error& err) noexcept;
    void close(uint16_t code = 1000,
               const knst_c16string& reason = {}) noexcept;
    bool is_open() const noexcept;

    bool send_text  (const knst_c16string& s, knst_network_error& err) noexcept;
    bool send_binary(const knst_byte_string& b, knst_network_error& err) noexcept;
    bool send_ping  (const knst_byte_string& p, knst_network_error& err) noexcept;
    bool send_pong  (const knst_byte_string& p, knst_network_error& err) noexcept;

    bool recv(message& out, uint32_t timeout_ms, knst_network_error& err) noexcept;

    uint16_t close_code() const noexcept;
    const knst_c16string& close_reason() const noexcept;
    const knst_c16string& negotiated_subprotocol() const noexcept;
    const knst_c16string& negotiated_extensions() const noexcept;
    bool last_message_was_fragmented() const noexcept;

#if defined(KNST_NETWORK_ENABLE_JSON)
    bool send_json(const knst_json& j, knst_network_error& err,
                   uint32_t indent = 0) noexcept;
    bool recv_json(knst_json& out, uint32_t timeout_ms,
                   knst_network_error& err) noexcept;
#endif
};
```

---

## 🎯 Size Getireceği Kazanç

`knst_websocket` şu durumlarda ciddi avantaj sağlar:

- ✅ **Gerçek zamanlı chat / mesajlaşma** — metin frame'leri, fragmentation hazır
- ✅ **Canlı veri akışı** — borsa/fiyat/sensör akışı, sürekli bağlantı
- ✅ **Multiplayer oyun** — binary frame desteği, düşük latency
- ✅ **Uzak terminal / SSH-over-WS** — büyük binary blob'lar otomatik fragment edilir
- ✅ **Proxy'li kurumsal ağ** — SOCKS5/HTTP CONNECT otomatik
- ✅ **wss:// ile güvenli bağlantı** — TLS katmanı şeffaf
- ✅ **GraphQL subscription** — subprotocol header hazır
- ✅ **KernelNucleusT uyumu** — `knst_c16string`, `knst_byte_string`, `knst_network_error` ile tam entegre
- ✅ **RFC 6455 tam uyum** — text, binary, ping, pong, close, fragmentation, masking
- ✅ **Handshake buffer** — 101 ile aynı segmentte gelen frame kaybolmaz
- ✅ **Proxy + DNS + SSRF hediye** — `knst_http_client` katmanından
- ✅ **Fragmentasyon otomatik** — büyük mesajlar için tek satır kod
- ✅ **Move-only tasarım** — soket sahipliği net, double-close yok
- ✅ **JSON entegrasyonu** — `send_json` / `recv_json` opsiyonel, açtığında hazır
- ✅ **Hata yönetimi** — `knst_network_error` enum'u ile net, exception yok

---