# knst_websocket — WebSocket Client Library

Hey there! 👋 This document explains what the `knst_websocket` class is, how to use it, and when it comes in handy.

**In short:** An RFC 6455-compliant, client-side WebSocket implementation. Supports `ws://` and `wss://`. It delegates all network-layer work — proxy, DNS cache, SSRF guard, TLS — to `knst_http_client`. You just connect, send, and receive. Under the hood there's a **full RFC 6455 frame codec**: text, binary, ping, pong, close, fragmentation, masking — all there.

---

## ✨ Highlights

| Feature | What it gives you |
|---|---|
| **ws:// and wss://** | TLS automatic (with `KNST_NETWORK_ENABLE_TLS`) |
| **HTTP proxy (CONNECT)** | Via `knst_http_client::options` |
| **SOCKS5 / SOCKS5h** | Selectable proxy type |
| **NO_PROXY support** | Proxy bypass list |
| **DNS cache** | Comes from `knst_http_client` |
| **bind_local_ip** | Pin the outgoing IP |
| **SSRF guard** | Protection against internal-network requests |
| **RFC 6455 frame codec** | Text/Binary/Ping/Pong/Close + fragmentation |
| **Client masking** | Mandatory, automatic (16 KiB chunks) |
| **Auto-pong** | Automatic Pong on incoming Ping (configurable) |
| **Graceful close** | Automatic echo of server close |
| **Fragment assembly** | Fragmented messages come back as one message |
| **Size limits** | Separate limits per frame and per message |
| **Sec-WebSocket-Accept** | Verified with SHA-1 |
| **Handshake buffer** | Frames arriving with 101 aren't lost |
| **JSON helpers** | Optional `send_json` / `recv_json` |
| **Move-only** | Copy forbidden, move allowed |

---

## 🚀 Quick Start

```cpp
#include "KernelNucleusT.hpp"
#include <iostream>

int main() {
    knst_websocket ws;
    knst_websocket::options opt;

    knst_network_error err = knst_network_error::None;

    if (!ws.connect(u"wss://echo.websocket.org/", opt, err)) {
        std::cerr << "Connection failed\n";
        return 1;
    }

    if (!ws.send_text(u"hello world", err)) {
        std::cerr << "Send failed\n";
        return 1;
    }

    knst_websocket::message msg;
    if (ws.recv(msg, /*timeout_ms=*/5000, err)) {
        std::cout << "Received: " << msg.data.data() << "\n";
    }

    ws.close(1000, u"bye");
}
```

That's it! `connect → send → recv → close`, four steps. The rest is detail.

---

## 📋 General API

| Category | Methods |
|---|---|
| **Connection** | `connect(url, options, err)`, `close(code, reason)`, `is_open()` |
| **Sending** | `send_text`, `send_binary`, `send_ping`, `send_pong`, `send_json`* |
| **Receiving** | `recv(message&, timeout_ms, err)`, `recv_json`* |
| **Status** | `close_code()`, `close_reason()`, `negotiated_subprotocol()`, `negotiated_extensions()`, `last_message_was_fragmented()` |
| **Types** | `opcode`, `options`, `message` |

> `*` JSON helpers are only available if `KNST_NETWORK_ENABLE_JSON` is defined and `knst_json.hpp` is included.

---

## 🔧 1) The `options` Struct — All Settings Here

```cpp
struct knst_websocket::options {
    knst_http_client::options http;      // all network-layer settings here
    knst_c16string subprotocols;         // "chat, superchat" etc.
    knst_c16string origin;               // Origin header
    bool auto_pong = true;               // auto-Pong on incoming Ping
    uint32_t max_message_bytes = 16 * 1024 * 1024;  // 16 MiB
    uint32_t max_frame_bytes   = 4  * 1024 * 1024;  // 4 MiB
};
```

### Network settings — via `options.http`

Settings coming from `knst_http_client::options` (all apply to WebSocket as well):

- `timeout_ms` → connection, read, write timeouts (default 10 sec)
- `user_agent` → `User-Agent` header
- `headers` → extra HTTP headers (auth tokens etc.)
- `max_header_bytes` → HTTP response header limit
- Proxy: `proxy_url`, `proxy_type`, `no_proxy`
- TLS: `verify_peer`, `ca_file`, `client_cert`
- DNS: `dns_cache_ttl`, `bind_local_ip`
- SSRF: `ssrf_guard`, `allow_private_networks`

See the `knst_http_client` docs for details.

### `subprotocols`

Comma-separated list. The server reports which one it chose via `Sec-WebSocket-Protocol`. Leave empty to skip the header entirely.

```cpp
opt.subprotocols = u"graphql-ws";

// or multiple
opt.subprotocols = u"chat, superchat";
```

You read the server's answer with `negotiated_subprotocol()`.

### `origin`

The `Origin` header browsers send. Some servers (especially ones doing CORS/CSRF checks) require it. Empty means it's never sent.

```cpp
opt.origin = u"https://app.example.com";
```

### `auto_pong`

`true` (default): automatically send PONG when a PING frame arrives. `false`: you handle PING manually.

### `max_message_bytes`

Total size of an incoming message **after reassembly**. If exceeded, a **1009 (Message Too Big)** close frame is sent and `MessageTooLarge` is returned. Default 16 MiB.

### `max_frame_bytes`

**Single frame** size for both outgoing and incoming frames. Default 4 MiB.

- Outgoing: `send_data()` auto-splits payloads larger than this.
- Incoming: frames larger than this → `MessageTooLarge`.

---

## 🔌 2) Connecting

### Basic connection

```cpp
knst_websocket ws;
knst_websocket::options opt;
knst_network_error err;

if (!ws.connect(u"wss://api.example.com/ws", opt, err)) {
    // err: NotSupported, ProtocolError, HttpMalformedHeaders, etc.
}
```

### With subprotocol

```cpp
opt.subprotocols = u"graphql-ws";
ws.connect(u"wss://api.example.com/graphql", opt, err);

// Which subprotocol did the server pick?
knst_c16string chosen = ws.negotiated_subprotocol();
```

### With Origin (browser-like)

```cpp
opt.origin = u"https://app.example.com";
```

### Through a proxy

```cpp
opt.http.proxy_url  = u"socks5://127.0.0.1:1080";
opt.http.proxy_type = knst_proxy_type::Socks5;

// or SOCKS5h (resolves DNS at the proxy — more secure)
opt.http.proxy_url  = u"socks5h://proxy.local:9050";
opt.http.proxy_type = knst_proxy_type::Socks5h;

// Bypass list
opt.http.no_proxy = u"localhost,127.0.0.1,*.internal";
```

### Auth header + custom headers

```cpp
opt.http.headers.push_back({u"Authorization", u"Bearer eyJhbGci..."});
opt.http.headers.push_back({u"X-Client-Version", u"1.2.3"});
opt.http.user_agent = u"MyApp/1.0";
```

### Timeouts

```cpp
opt.http.timeout_ms = 15000;   // 15 sec
```

> ⚠️ This timeout applies to **every socket operation**. It's also used during `connect()` for the handshake.

### What happens inside?

1. **URL is validated.** Parsed with `knst_url::parse()`. Scheme **must be `ws` or `wss`**, otherwise `NotSupported` is returned.
2. **Fragment is stripped.** Any `#fragment` in the URL is removed (RFC 6455 §3 — fragments aren't sent to servers).
3. **Scheme is rewritten.** For the underlying layer (`knst_http_client`): `ws://` → `http://`, `wss://` → `https://`.
4. **Raw TCP.** `knst_http_client::open_raw_socket()` is called — proxy/TLS/DNS are all automatic.
5. **Key is generated.** 16 random bytes for `Sec-WebSocket-Key`.
6. **HTTP upgrade request sent.** `GET ... HTTP/1.1` + `Upgrade: websocket` + `Connection: Upgrade` + `Sec-WebSocket-Key` + `Sec-WebSocket-Version: 13` + optional `Sec-WebSocket-Protocol` / `Origin` / `User-Agent` + custom headers.
7. **Response read.** Up to `\r\n\r\n` (`read_http_head`).
8. **101 check.** Otherwise `ProtocolError`.
9. **Upgrade + Connection check.** Missing → `ProtocolError`.
10. **Sec-WebSocket-Accept verified.** Must equal `base64(SHA-1(key + magic))`. Magic = `"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"`. Mismatch → `ProtocolError`.
11. **Subprotocol / Extensions stored.** If the server returned them, they're saved to `m_subprotocol` / `m_extensions`.
12. **Handshake buffer.** Any extra bytes arriving with the 101 (some servers push the first frame in the same segment) go into `m_prebuf` — **not lost**.

On error, the socket is closed and `err` is set.

### 🎁 Why the handshake buffer matters

RFC 6455 §4.2.2 says: after writing the 101, the server **may immediately** start sending WebSocket frames — even in the same TCP segment.

```
[HTTP/1.1 101 Switching Protocols\r\n...\r\n\r\n][0x81 0x05 "hello"]  ← single segment
```

A naive implementation throws away everything after `\r\n\r\n`. `knst_websocket` doesn't: `read_http_head` captures the extra bytes as `leftover`, stashes them in `m_prebuf`, and the first `recv()` reads from there. No matter how fast the server is, frames aren't lost. 👌

---

## 📤 3) Sending

Every send method takes `knst_network_error&` and returns `bool`.

### `send_text` — UTF-16 → UTF-8 → Text frame

```cpp
ws.send_text(u"hello world", err);
```

Inside:
1. `knst_net_detail::to_utf8()` is called (`knst_c16string` → `knst_byte_string`)
2. `send_data(0x1, ...)` is called (opcode 0x1 = Text)

### `send_binary` — Raw bytes → Binary frame

```cpp
knst_byte_string blob = /* ... */;
ws.send_binary(blob, err);
```

Opcode 0x2 (Binary). Raw bytes go on the wire — **no** UTF-8 conversion.

### `send_ping` / `send_pong` — Control frames

```cpp
ws.send_ping(knst_byte_string(), err);          // empty payload
ws.send_pong(knst_byte_string(), err);          // empty payload

// or with payload
knst_byte_string p = /* ≤ 125 bytes */;
ws.send_ping(p, err);
```

> ⚠️ Control frame payload can be **at most 125 bytes** (RFC §5.5.2). Exceeding this returns `MessageTooLarge`.

### Automatic fragmentation

Inside `send_text` / `send_binary`, `send_data()` kicks in:

- Payload smaller than `max_frame_bytes` → **single frame** (FIN=1)
- Larger → split into `max_frame_bytes` chunks:
  - First chunk: original opcode (0x1/0x2), FIN=0
  - Middle chunks: opcode 0x0 (Continuation), FIN=0
  - Last chunk: opcode 0x0, FIN=1

Example: 10 MB binary, `max_frame_bytes=4 MiB` → 3 frames:

```
[Binary FIN=0, 4 MiB] [Continuation FIN=0, 4 MiB] [Continuation FIN=1, 2 MiB]
```

You don't do anything — a single `send_binary` call is enough at the API level. RFC §5.4 rules are correctly applied.

### Client masking (RFC §5.3 — Mandatory)

**Every outgoing frame** from client to server is masked:

- 4 random mask key bytes (`knst_ws_detail::random_bytes`)
- Payload XOR'd in 16 KiB chunks

Nothing for you to do, it's automatic. 🔒

### `send_json` (if JSON module is enabled)

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
    knst_json j = /* ... */;

    // Compact
    ws.send_json(j, err, /*indent=*/0);

    // Pretty
    ws.send_json(j, err, /*indent=*/2);
#endif
```

Inside:
1. `j.dump()` is called → UTF-8 buffer
2. UTF-8 buffer → `knst_c16string`
3. `send_text()` is called

So JSON messages go out as **Text frames**.

### `can_send()` pre-check

Every `send_*` method first calls `can_send()`:

- No socket → `NotOpen`
- CLOSE sent → `ConnectionClosed`
- CLOSE received → `ConnectionClosed`

So writing to a closed connection returns a clear error.

---

## 📥 4) Receiving

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
    // raw bytes
}
```

### The `message` struct

```cpp
struct knst_websocket::message {
    opcode type = opcode::Text;   // Text or Binary
    knst_byte_string data;         // Raw bytes
};
```

> ⚠️ **`msg.type` can only be `Text` or `Binary`**. Ping/Pong/Close never land here — see below.

### What `recv` does (step by step)

Returns a single **message** (not a frame). Inside:

**If Ping arrives:**
- If `auto_pong` is enabled → sends Pong automatically
- **Not shown to you**, continues the loop

**If Pong arrives:**
- Silently swallowed
- **Not shown to you**, continues the loop

**If Close arrives:**
- `close_code` / `close_reason` are set
- If we haven't sent CLOSE yet, the close is echoed back (same code, or empty if no code)
- Socket is closed
- `ConnectionClosed` error returned

**If Text/Binary arrives:**
- `FIN=1` → single frame, returned directly (`last_message_was_fragmented = false`)
- `FIN=0` → stored in `out.data`, `assembling=true`, starts collecting `Continuation` frames

**If Continuation arrives:**
- Not `assembling` → **1002 ProtocolError**
- Appended to `out.data`
- Returns when `FIN=1` (`last_message_was_fragmented = true`)

**If another data frame interrupts** (Text/Binary instead of Continuation):
- **1002 ProtocolError** (RFC §5.4 violation)

**If total size exceeds `max_message_bytes`:**
- **1009 (Message Too Big)** close frame sent
- `MessageTooLarge` returned

**On protocol error:**
- **1002 (Protocol Error)** close frame sent
- `ProtocolError` returned

### ⚠️ BIG GOTCHA: You can't see Ping/Pong

As shown above, `recv()` **doesn't surface PING and PONG** — it handles them silently. This is a design choice.

**Consequence:** you can't call `send_ping()` and wait for the PONG through `recv()`. Latency measurement, heartbeat verification are **not possible** through this API.

```cpp
// ❌ THIS DOESN'T WORK:
ws.send_ping(payload, err);
ws.recv(msg, 3000, err);   // not PONG — next Text/Binary arrives

// ✅ If you want manual PING/PONG:
opt.auto_pong = false;
ws.connect(url, opt, err);

ws.send_ping(payload, err);

// But recv() still swallows PONG — pointless
// Heartbeat/latency is not done through this API.
```

### Fragmented info

```cpp
if (ws.last_message_was_fragmented()) {
    // This message was assembled from multiple frames
}
```

### Timeout behavior

`recv(msg, timeout_ms, err)`:

- `timeout_ms == 0` → uses `m_recv_timeout` (default 10 seconds)
- `timeout_ms > 0` → timeout for **every socket read**

> ⚠️ **Important:** The timeout applies to **each read operation**, not the whole message. So if you pass `timeout_ms=3000` and the server sends a message over 3 seconds, `recv()` can still complete a message that takes longer than 3 seconds. Slow large messages can exceed `timeout_ms`.

### `recv_json` (if JSON module is enabled)

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
    knst_json j;
    if (ws.recv_json(j, 5000, err)) {
        // j is ready
    }
#endif
```

Inside:
1. Calls `recv()`
2. `msg.type` must be Text or Binary (otherwise `ProtocolError`)
3. Parses with `knst_json::parse()`
4. Parse error → `ProtocolError`

---

## 🔒 5) Closing

### What `close()` does

```cpp
ws.close(1000, u"normal closure");
// 1) Sends Close frame (code + reason, max 123 bytes)
// 2) Immediately calls close_socket()
```

If already closed, does nothing (idempotent).

Reason is UTF-8 encoded, max 123 bytes (2 bytes code + 123 bytes reason = 125 bytes = control frame limit).

> ⚠️ **IMPORTANT:** `close()` **does not wait** for the server's CLOSE echo. It writes the CLOSE frame and immediately closes the socket. On the server side this may look like an "abort". RFC 6455 ideally says to wait for the echo, but this implementation closes fast. If you want a graceful close, call `recv()` once after `close()` to wait briefly for the echo.

### If the server sends CLOSE

Handled automatically inside `recv()`:

1. `m_close_received = true`
2. `m_close_code`, `m_close_reason` are populated
3. If payload ≥ 2 bytes, code is read; otherwise `1005` (no code)
4. If we haven't sent CLOSE yet, an echo CLOSE is sent
5. `close_socket()` is called
6. `ConnectionClosed` is returned

> ⚠️ **1005 is never written on the wire** (RFC §7.4.1). If the server sent an empty CLOSE, we send an empty CLOSE too.

### Close codes (RFC §7.4)

| Code | Meaning |
|---|---|
| **1000** | Normal closure (default) |
| **1001** | Going away (page closing etc.) |
| **1002** | Protocol error (RFC violation) |
| **1003** | Unsupported data |
| **1005** | No code (API may return it via `close_code()`, never written on wire) |
| **1009** | Message too big |

### `is_open()` state

`true` only when:

- Socket open **AND**
- Close not sent **AND**
- Close not received

### Destructor

```cpp
~knst_websocket() { close_socket(); }
```

The destructor **does not send a CLOSE frame** — it just closes the socket. For a graceful close, call `close()`.

### `close()` twice is safe

```cpp
ws.close(1000);
ws.close(1000);  // no-op, second call doesn't send another frame
```

---

## 🧠 6) State Accessors

```cpp
ws.is_open();                       // can we still talk?
ws.close_code();                    // 1000, 1001, 1005, ...
ws.close_reason();                  // "normal" etc.
ws.negotiated_subprotocol();        // which subprotocol the server chose
ws.negotiated_extensions();         // which extension the server accepted
ws.last_message_was_fragmented();   // was the last message fragmented?
```

All are `const noexcept` — safe to call anytime.

---

## ⚠️ 7) Things to Know (Common Gotchas)

### 1) `recv()` **blocks**

It waits until a message arrives. Timeout is `timeout_ms` (0 → `options.http.timeout_ms`, and if that's 0 too, 10 sec).

### 2) Copy forbidden, move allowed

```cpp
knst_websocket a;
knst_websocket b = std::move(a);   // ✅
knst_websocket c = a;              // ❌ won't compile
```

Reason: socket FD ownership is clear; copying gives two objects the same FD → double-close risk.

Move transfers all state (socket, close info, timeouts); source `a` is reset.

### 3) **No** thread-safety

Don't use the same `knst_websocket` from multiple threads. Either a separate instance per thread, or mutex + single instance.

### 4) Size limits

- **Per frame:** `max_frame_bytes` (default 4 MiB) — exceeding → `MessageTooLarge`
- **Per message:** `max_message_bytes` (default 16 MiB) — exceeding after reassembly → **1009 close**

If the server sends a frame larger than 4 MiB, it's **immediately rejected** (not reassembled into a message). This is intentional: DoS protection.

### 5) Server masking forbidden

Server → client frames **must be unmasked** (RFC 6455). Masked → `ProtocolError`.

### 6) `random_bytes` is **not** cryptographic

```cpp
static std::mt19937_64 rng = /* seed: random_device + steady_clock + thread_id + time */;
```

`std::mt19937_64` is **not a CSPRNG**. RFC 6455 accepts it for masking keys (crypto quality isn't required, only unpredictability is). But:

- **Sec-WebSocket-Key** also uses the same `random_bytes` — acceptable in practice.
- **Do not use it for critical cryptography** (encryption, token generation). Use a dedicated module for that.

### 7) No pool allocator

Every frame calls `knst_byte_string::resize()` → default allocator. If you're receiving lots of small messages, this could be performance-critical. Custom pool isn't exposed by the API.

### 8) `close()` doesn't wait for echo

Detailed above. May look like "abort" on the server side.

### 9) Ping/Pong not visible

`recv()` handles them silently. This API isn't for heartbeat.

### 10) Timeout is per-read, not per-message

Timeout applies to each socket read separately. Slow large messages can exceed `timeout_ms`.

---

## 📡 8) Usage Examples

### Example 1: Echo test

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

### Example 2: Sending a binary file chunk

```cpp
knst_byte_string chunk = /* 10 MB binary */;
ws.send_binary(chunk, err);
// send_data() auto-splits into 3 frames with max_frame_bytes (4 MiB)
```

### Example 3: Connecting behind a proxy

```cpp
knst_websocket::options opt;
opt.http.proxy_url  = u"socks5h://proxy.local:9050";
opt.http.proxy_type = knst_proxy_type::Socks5h;
opt.http.no_proxy   = u"localhost,127.0.0.1";

ws.connect(u"wss://private-api.example.com/ws", opt, err);
```

### Example 4: Auth header + subprotocol

```cpp
knst_websocket::options opt;
opt.subprotocols = u"graphql-ws";
opt.http.headers.push_back({u"Authorization", u"Bearer eyJhbGci..."});
opt.http.headers.push_back({u"X-Client-Version", u"1.2.3"});

ws.connect(u"wss://api.example.com/graphql", opt, err);

knst_c16string chosen = ws.negotiated_subprotocol();
// "graphql-ws" or empty
```

### Example 5: Sending a large message (fragmentation)

```cpp
knst_websocket::options opt;
opt.max_frame_bytes   = 64 * 1024;             // each frame 64 KB
opt.max_message_bytes = 100 * 1024 * 1024;     // accept up to 100 MB

ws.connect(u"wss://upload.example.com/stream", opt, err);

knst_byte_string big_blob = /* 50 MB */;
ws.send_binary(big_blob, err);   // split into ~800 frames, you don't deal with it
```

### Example 6: JSON chat

```cpp
#if defined(KNST_NETWORK_ENABLE_JSON)
    knst_json msg;
    knst_json::object obj;
    obj[u"type"]    = knst_json(u"message");
    obj[u"content"] = knst_json(u"hello");
    msg = knst_json(obj);

    ws.send_json(msg, err);

    knst_json incoming;
    if (ws.recv_json(incoming, 5000, err)) {
        // incoming is ready
    }
#endif
```

### Example 7: Receiving in a loop

```cpp
knst_websocket::message msg;

while (ws.is_open()) {
    if (!ws.recv(msg, 1000, err)) {
        if (err == knst_network_error::Timeout) {
            // 1 second without a message, keep going
            continue;
        }
        if (err == knst_network_error::ConnectionClosed) {
            std::cout << "Closed: " << ws.close_code()
                      << " (" << ws.close_reason() << ")\n";
            break;
        }
        // other errors
        break;
    }

    if (msg.type == knst_websocket::opcode::Text) {
        std::cout << "Text: " << msg.data.data() << "\n";
    } else if (msg.type == knst_websocket::opcode::Binary) {
        std::cout << "Binary: " << msg.data.length() << " bytes\n";
    }
}
```

### Example 8: Full usage with minimal error checking

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

    // Send first message
    if (!ws.send_text(u"hello", err)) {
        std::cerr << "Send: " << knst_network_error_string(err) << "\n";
        ws.close();
        return 1;
    }

    // Receive 10 messages
    for (int i = 0; i < 10; ++i) {
        knst_websocket::message msg;
        if (!ws.recv(msg, 5000, err)) {
            if (err == knst_network_error::Timeout) {
                std::cout << "Timeout, next iteration\n";
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

## 🐛 9) Error Handling

Through the `knst_network_error` enum:

| Error | When? |
|---|---|
| `None` | Success |
| `NotSupported` | Not `ws://`/`wss://` |
| `ProtocolError` | Handshake or frame format broken |
| `ConnectionClosed` | Server closed / connection dropped |
| `NotOpen` | Send attempt while not connected |
| `MessageTooLarge` | Frame or message limit exceeded |
| `OutOfMemory` | Out of memory |
| `HttpMalformedHeaders` | 101 response malformed |
| `Timeout` | Read didn't complete within timeout |
| `DnsError` / `ConnectError` / `TlsError` | From `knst_http_client` |

```cpp
if (!ws.recv(msg, 1000, err)) {
    if (err == knst_network_error::ConnectionClosed) {
        std::cout << "Server closed: " << ws.close_code()
                  << " (" << ws.close_reason() << ")\n";
    }
}
```

---

## 📊 10) Performance Notes

| Operation | Complexity | Note |
|---|---|---|
| `connect` | O(1) + handshake RTT | 1 round-trip + SHA-1 |
| `send_text` / `send_binary` (small) | O(n) | No fragmentation, masking included |
| `send_text` / `send_binary` (large) | O(n) + k×RTT | k = number of fragments |
| `recv` (single frame) | O(n) | Frame decode |
| `recv` (fragmented) | O(total) | Reassembly |
| Masking | O(n) | 16 KiB chunks |
| SHA-1 verify | O(1) | Handshake only |

### Tips

- Don't **lower** `max_frame_bytes` for small frequent messages (default 4 MiB is fine).
- When sending big blobs, set `max_frame_bytes` to whatever the server accepts.
- `send_binary` **blocks** — don't freeze the UI thread on big sends, use a worker thread.
- `recv()` also **blocks** — pass a timeout to check periodically.

---

## 🌍 11) Platform Differences

`knst_websocket` is **portable** — works on Linux / Windows / Android / macOS. Differences come from the underlying `knst_http_client`:

| Concern | Linux/macOS | Windows |
|---|---|---|
| Socket API | POSIX `socket()` | Winsock2 |
| TLS | OpenSSL | OpenSSL (same API) |
| DNS | `getaddrinfo` | `getaddrinfo` (same) |
| Proxy | Same | Same |

So **no platform differences** at the WebSocket layer — it's all handled by `knst_http_client`.

---

## 🎯 12) When to Use?

`knst_websocket` shines especially in:

- ✅ **Real-time chat / messaging** — text frames, fragmentation ready
- ✅ **Live data streams** — stock/finance/sensor feeds
- ✅ **Multiplayer games** — binary frame support
- ✅ **Remote terminal / SSH-over-WS** — big binary blobs auto-fragment
- ✅ **Corporate networks with proxies** — SOCKS5/HTTP CONNECT automatic
- ✅ **Secure connection with wss://** — TLS layer transparent
- ✅ **GraphQL subscriptions** — subprotocol header
- ✅ **KernelNucleusT compatibility** — fully integrated with `knst_c16string`, `knst_byte_string`, `knst_network_error`

### When **not** to use?

- ❌ **HTTP polling is enough** — WebSocket is overkill, use `knst_http_client`
- ❌ **You need a server-side WebSocket** — this library is **client-only**
- ❌ **Heartbeat/latency measurement is critical** — `recv()` doesn't surface PING/PONG
- ❌ **Thread-safety required** — don't use the same instance from multiple threads

---

## 🔥 13) RFC 6455 Compliance

| Rule | Status |
|---|---|
| §4.2.2 — 101 + first frame in same segment | ✅ Supported (`m_prebuf`) |
| §5.3 — Client MASK mandatory | ✅ Every frame masked |
| §5.3 — Server does not MASK | ✅ Checked, violation → `ProtocolError` |
| §5.4 — Fragmentation (Text/Binary → Continuation → FIN) | ✅ Automatic |
| §5.5.2 — Control frame FIN=1, payload ≤ 125 bytes | ✅ Enforced |
| §7.4 — Close codes (1000, 1002, 1005, 1009) | ✅ Supported |
| §7.4.1 — 1005 never on wire | ✅ Checked |
| §8.1 — UTF-8 text mandatory | ✅ `send_text` sends UTF-8 |
| §4.1 — Sec-WebSocket-Accept = base64(SHA-1(key + magic)) | ✅ Verified |

**Non-conformance:** `close()` doesn't wait for the server echo (fast-close preference). RFC says "MAY", not "MUST" — acceptable.

---

## ✅ 14) Do's / ❌ Don'ts

### ✅ Do

```cpp
// 1. Set a timeout before connecting
knst_websocket::options opt;
opt.http.timeout_ms = 5000;

// 2. Always call recv() with a timeout
knst_websocket::message msg;
ws.recv(msg, 3000, err);

// 3. Check the close code
if (ws.close_code() == 1009) {
    // message too big
}

// 4. Use move semantics
knst_websocket ws2 = std::move(ws);

// 5. Know about the handshake buffer — the server may send the first frame immediately
ws.connect(url, opt, err);
ws.recv(msg, 1000, err);

// 6. Use send_binary for binary data
ws.send_binary(blob, err);
```

### ❌ Don't

```cpp
// 1. Wait for PING/PONG through recv()
ws.send_ping(payload, err);
ws.recv(msg, 3000, err);  // ❌ not PONG — next Text/Binary arrives

// 2. recv() without timeout
ws.recv(msg, 0, err);     // ⚠️ waits 10 seconds (default)

// 3. send after close()
ws.close();
ws.send_text(u"x", err);  // ❌ NotOpen

// 4. Copy constructor
knst_websocket a;
knst_websocket b = a;     // ❌ won't compile — move-only

// 5. send_text for binary data
ws.send_text(u"\x01\x02\x03", err);   // ❌ embedded \0 in UTF-16 string → cut at UTF-8

// 6. Use the same instance from multiple threads
// ❌ No thread-safety
```

---

## 📌 15) API Contract Summary (Quick Reference)

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

## 🎯 What It Brings You

`knst_websocket` gives you serious advantages in these situations:

- ✅ **Real-time chat / messaging** — text frames, fragmentation ready
- ✅ **Live data streams** — stock/finance/sensor feeds, persistent connection
- ✅ **Multiplayer games** — binary frame support, low latency
- ✅ **Remote terminal / SSH-over-WS** — big binary blobs auto-fragment
- ✅ **Corporate networks with proxies** — SOCKS5/HTTP CONNECT automatic
- ✅ **Secure connection with wss://** — TLS layer transparent
- ✅ **GraphQL subscriptions** — subprotocol header ready
- ✅ **KernelNucleusT compatibility** — fully integrated with `knst_c16string`, `knst_byte_string`, `knst_network_error`
- ✅ **Full RFC 6455 compliance** — text, binary, ping, pong, close, fragmentation, masking
- ✅ **Handshake buffer** — frames arriving with 101 aren't lost
- ✅ **Proxy + DNS + SSRF on the house** — from the `knst_http_client` layer
- ✅ **Automatic fragmentation** — one line of code for big messages
- ✅ **Move-only design** — clean socket ownership, no double-close
- ✅ **JSON integration** — `send_json` / `recv_json` optional, ready when enabled
- ✅ **Error handling** — `knst_network_error` enum, clean, no exceptions

---