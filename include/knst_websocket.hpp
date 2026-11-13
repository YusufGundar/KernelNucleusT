// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/* ============================================================================
 * knst_websocket.hpp
 * ----------------------------------------------------------------------------
 *
 *  Features
 *    - ws:// and wss:// (TLS via OpenSSL, honors KNST_NETWORK_ENABLE_TLS)
 *    - HTTP proxy (CONNECT), SOCKS5 / SOCKS5h, NO_PROXY — via knst_http_client
 *    - DNS cache, bind_local_ip, SSRF guard — all reused from knst_http_client
 *    - Full RFC 6455 frame codec (text/binary/ping/pong/close, fragmentation)
 *    - Client-side masking (mandatory)
 *    - Auto-pong on incoming ping (configurable)
 *    - Graceful close handshake (both directions handled)
 *    - Fragmented message assembly
 *    - Message size limits (max_frame_bytes per outgoing frame; max_message_bytes
 *      per inbound message after reassembly)
 * ============================================================================ */

#pragma once

#include <random>
#include <memory>
#include <ctime>

#if defined(KNST_NETWORK_ENABLE_JSON) && !defined(KNST_JSON_HPP)
    #error "KNST_NETWORK_ENABLE_JSON requires knst_json.hpp to be included first"
#endif

/* ============================================================================
 * knst_ws_detail — SHA-1 + random + masking helpers (private to WebSocket)
 * ============================================================================ */

namespace knst_ws_detail {

    /* ---- SHA-1 (FIPS 180-1) — needed for Sec-WebSocket-Accept verification ---- */

    struct sha1_ctx {
        uint32_t h[5];
        uint64_t total_len;
        uint8_t  buf[64];
        uint32_t buf_len;

        sha1_ctx() noexcept { reset(); }

        void reset() noexcept {
            h[0] = 0x67452301u; h[1] = 0xEFCDAB89u; h[2] = 0x98BADCFEu;
            h[3] = 0x10325476u; h[4] = 0xC3D2E1F0u;
            total_len = 0; buf_len = 0;
        }
        static uint32_t rol(uint32_t x, int n) noexcept { return (x << n) | (x >> (32 - n)); }

        void process(const uint8_t* p) noexcept {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = (uint32_t(p[i*4]) << 24) | (uint32_t(p[i*4+1]) << 16) |
                       (uint32_t(p[i*4+2]) << 8) | uint32_t(p[i*4+3]);
            for (int i = 16; i < 80; ++i)
                w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; ++i) {
                uint32_t f, k;
                if      (i < 20) { f = (b & c) | ((~b) & d);            k = 0x5A827999u; }
                else if (i < 40) { f = b ^ c ^ d;                       k = 0x6ED9EBA1u; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d);     k = 0x8F1BBCDCu; }
                else             { f = b ^ c ^ d;                       k = 0xCA62C1D6u; }
                uint32_t t = rol(a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol(b, 30); b = a; a = t;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }

        void update(const uint8_t* p, size_t n) noexcept {
            total_len += n;
            while (n > 0) {
                uint32_t take = 64 - buf_len;
                if (take > n) take = static_cast<uint32_t>(n);
                ::memcpy(buf + buf_len, p, take);
                buf_len += take; p += take; n -= take;
                if (buf_len == 64) { process(buf); buf_len = 0; }
            }
        }

        void final(uint8_t out[20]) noexcept {
            uint64_t bits = total_len * 8;
            uint8_t pad = 0x80; update(&pad, 1);
            uint8_t zero = 0;
            while (buf_len != 56) update(&zero, 1);
            uint8_t len_buf[8];
            for (int i = 0; i < 8; ++i) len_buf[i] = uint8_t((bits >> (56 - i*8)) & 0xFF);
            update(len_buf, 8);
            for (int i = 0; i < 5; ++i) {
                out[i*4]     = uint8_t((h[i] >> 24) & 0xFF);
                out[i*4 + 1] = uint8_t((h[i] >> 16) & 0xFF);
                out[i*4 + 2] = uint8_t((h[i] >> 8)  & 0xFF);
                out[i*4 + 3] = uint8_t( h[i]        & 0xFF);
            }
        }
    };

    inline void sha1(const void* data, size_t n, uint8_t out[20]) noexcept {
        sha1_ctx c;
        c.update(static_cast<const uint8_t*>(data), n);
        c.final(out);
    }

    /* ---- Cryptographic-ish random (mask keys + Sec-WebSocket-Key) ---- */

    inline void random_bytes(uint8_t* out, size_t n) noexcept {
        static std::mutex mtx;
        static std::mt19937_64 rng = []() {
            std::random_device rd;
            uint64_t seed = (uint64_t(rd()) << 32) | uint64_t(rd());
            seed ^= uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
            seed ^= uint64_t(::time(nullptr)) * 0x9E3779B97F4A7C15ull;
            seed ^= uint64_t(std::hash<std::thread::id>{}(std::this_thread::get_id()));
            return std::mt19937_64(seed);
        }();
        std::lock_guard<std::mutex> lk(mtx);
        size_t i = 0;
        while (i < n) {
            uint64_t v = rng();
            size_t take = (n - i) < 8 ? (n - i) : 8;
            for (size_t k = 0; k < take; ++k) out[i + k] = uint8_t((v >> (k * 8)) & 0xFF);
            i += take;
        }
    }

} // namespace knst_ws_detail



class knst_websocket {
public:
    enum class opcode : uint8_t {
        Continuation = 0x0,
        Text = 0x1,
        Binary = 0x2,
        Close = 0x8,
        Ping = 0x9,
        Pong = 0xA,
    };

    struct options {
        // Everything networking (proxy, DNS, TLS, bind_local, SSRF, timeouts,
        // user-agent, extra headers) is delegated to knst_http_client::options.
        knst_http_client::options http;

        // Sec-WebSocket-Protocol (comma-separated; leave empty to skip).
        knst_c16string subprotocols;

        // Origin header (browsers send this; most servers don't require it).
        knst_c16string origin;

        // Auto-reply to incoming PING frames with a matching PONG. Default on.
        bool auto_pong = true;

        // Fail a message (close 1009) if the assembled body exceeds this.
        uint32_t max_message_bytes = 16u * 1024 * 1024;   // 16 MiB

        // Split outgoing messages into frames of at most this size.
        uint32_t max_frame_bytes   = 4u * 1024 * 1024;    // 4 MiB
    };

    struct message {
        opcode type = opcode::Text;
        knst_byte_string data;
    };

    knst_websocket() noexcept = default;
    ~knst_websocket() noexcept { close_socket(); }

    knst_websocket(const knst_websocket&) = delete;
    knst_websocket& operator=(const knst_websocket&) = delete;

    knst_websocket(knst_websocket&& o) noexcept { *this = std::move(o); }
    knst_websocket& operator=(knst_websocket&& o) noexcept {
        if (this != &o) {
            close_socket();
            m_sock             = std::move(o.m_sock);
            m_prebuf           = std::move(o.m_prebuf);
            m_subprotocol      = std::move(o.m_subprotocol);
            m_extensions       = std::move(o.m_extensions);
            m_close_code       = o.m_close_code;
            m_close_reason     = std::move(o.m_close_reason);
            m_close_sent       = o.m_close_sent;
            m_close_received   = o.m_close_received;
            m_auto_pong        = o.m_auto_pong;
            m_last_fragmented  = o.m_last_fragmented;
            m_send_timeout     = o.m_send_timeout;
            m_recv_timeout     = o.m_recv_timeout;
            m_max_message      = o.m_max_message;
            m_max_frame        = o.m_max_frame;
            o.m_sock.reset();
            o.m_close_sent = o.m_close_received = false;
        }
        return *this;
    }



    bool connect(const knst_c16string& url, const options& o,
                 knst_network_error& err) noexcept {
        err = knst_network_error::None;
        close_socket();

        knst_url u;
        if (!knst_url::parse(url, u, err)) return false;
        if (u.scheme != u"ws" && u.scheme != u"wss") { err = knst_network_error::NotSupported; return false; }
        u.fragment.clear(); 

  
        knst_url conn_url = u;
        if (conn_url.scheme == u"ws") conn_url.scheme = u"http";
        else if (conn_url.scheme == u"wss") conn_url.scheme = u"https";

      
        knst_http_client http;
        m_sock.reset(http.open_raw_socket(conn_url, o.http, err));
        if (!m_sock) return false;

        m_send_timeout = o.http.timeout_ms ? o.http.timeout_ms : 10000;
        m_recv_timeout = o.http.timeout_ms ? o.http.timeout_ms : 10000;
        m_sock->set_read_timeout(m_recv_timeout);
        m_sock->set_write_timeout(m_send_timeout);

        // Random 16-byte Sec-WebSocket-Key.
        uint8_t key_bytes[16];
        knst_ws_detail::random_bytes(key_bytes, 16);
        knst_byte_string key_bs;
        key_bs.resize(16);
        if (key_bs.length() != 16) { err = knst_network_error::OutOfMemory; close_socket(); return false; }
        ::memcpy(&key_bs[0], key_bytes, 16);
        knst_c16string key_b64 = knst_net_detail::base64_encode_bytes(key_bs);

        knst_c16string req;
        req.append(u"GET ");
        req.append(conn_url.request_target());
        req.append(u" HTTP/1.1\r\n");
        req.append(u"Host: ");
        req.append(conn_url.host_header_value());
        req.append(u"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ");
        req.append(key_b64);
        req.append(u"\r\nSec-WebSocket-Version: 13\r\n");
        if (!o.subprotocols.empty()) {
            req.append(u"Sec-WebSocket-Protocol: "); req.append(o.subprotocols); req.append(u"\r\n");
        }
        if (!o.origin.empty()) {
            req.append(u"Origin: "); req.append(o.origin); req.append(u"\r\n");
        }
        if (!o.http.user_agent.empty()) {
            req.append(u"User-Agent: "); req.append(o.http.user_agent); req.append(u"\r\n");
        }
        for (uint32_t i = 0; i < o.http.headers.size(); ++i) {
            req.append(o.http.headers[i].name); req.append(u": ");
            req.append(o.http.headers[i].value); req.append(u"\r\n");
        }
        req.append(u"\r\n");

        knst_byte_string req_bytes = knst_net_detail::to_utf8(req);
        if (!m_sock->send_all(req_bytes, m_send_timeout, err)) { close_socket(); return false; }

        // Read response head.
        knst_byte_string head, leftover;
        if (!read_http_head(*m_sock, m_recv_timeout, o.http.max_header_bytes, head, leftover, err)) {
            close_socket(); return false;
        }
        // Per RFC 6455 §4.2.2 the server MAY start sending WebSocket frames
        // immediately after writing the 101 response — sometimes in the same
        // TCP segment. Anything that arrived after the CRLFCRLF is the start
        // of the first frame; buffer it instead of failing the handshake.
        m_prebuf = std::move(leftover);

        knst_http_response resp;
        if (!knst_http_response::parse(head, resp, err)) { close_socket(); return false; }
        if (resp.status != 101) { err = knst_network_error::ProtocolError; close_socket(); return false; }

        knst_c16string upgrade = knst_net_detail::trim16(resp.get_header(u"Upgrade"));
        if (!knst_net_detail::ieq(upgrade, u"websocket")) {
            err = knst_network_error::ProtocolError; close_socket(); return false;
        }
        if (!resp.header_has_token(u"Connection", u"Upgrade")) {
            err = knst_network_error::ProtocolError; close_socket(); return false;
        }

        // Verify Sec-WebSocket-Accept = base64(sha1(key_b64 + magic)).
        {
            knst_byte_string to_hash;
            knst_byte_string kb = knst_net_detail::to_utf8(key_b64);
            if (!knst_net_detail::append_bytes(to_hash, kb.data(), kb.length())) {
                err = knst_network_error::OutOfMemory; close_socket(); return false;
            }
            static const char magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            for (const char* p = magic; *p; ++p) to_hash.push_back(static_cast<uint8_t>(*p));
            uint8_t sha1_out[20];
            knst_ws_detail::sha1(to_hash.data(), to_hash.length(), sha1_out);
            knst_byte_string sha_bs; sha_bs.resize(20);
            if (sha_bs.length() != 20) { err = knst_network_error::OutOfMemory; close_socket(); return false; }
            ::memcpy(&sha_bs[0], sha1_out, 20);
            knst_c16string expected = knst_net_detail::base64_encode_bytes(sha_bs);
            knst_c16string got = knst_net_detail::trim16(resp.get_header(u"Sec-WebSocket-Accept"));
            if (got != expected) { err = knst_network_error::ProtocolError; close_socket(); return false; }
        }

        knst_c16string sub = knst_net_detail::trim16(resp.get_header(u"Sec-WebSocket-Protocol"));
        if (!sub.empty()) m_subprotocol = sub;
        knst_c16string ext = knst_net_detail::trim16(resp.get_header(u"Sec-WebSocket-Extensions"));
        if (!ext.empty()) m_extensions = ext;

        m_auto_pong  = o.auto_pong;
        m_max_message = o.max_message_bytes;
        m_max_frame = o.max_frame_bytes ? o.max_frame_bytes : (4u * 1024 * 1024);
        m_close_sent = false;
        m_close_received = false;
        m_close_code = 0;
        m_close_reason.clear();
        return true;
    }

    void close(uint16_t code = 1000, const knst_c16string& reason = {}) noexcept {
        if (!m_sock) return;
        if (!m_close_sent && !m_close_received) {
            uint8_t buf[125];
            size_t len = 2;
            buf[0] = uint8_t((code >> 8) & 0xFF);
            buf[1] = uint8_t(code & 0xFF);
            knst_byte_string rb = knst_net_detail::to_utf8(reason);
            size_t take = rb.length() < 123 ? rb.length() : 123;
            for (size_t i = 0; i < take; ++i) buf[2 + i] = rb.data()[i];
            len = 2 + take;
            knst_network_error e = knst_network_error::None;
            send_frame(*m_sock, 0x8, true, buf, len, m_send_timeout, e);
            m_close_sent = true;
        }
        close_socket();
    }

    /* ---- state ---- */

    bool is_open() const noexcept { return m_sock != nullptr && !m_close_sent && !m_close_received; }

    uint16_t close_code() const noexcept { return m_close_code; }

    const knst_c16string& close_reason() const noexcept { return m_close_reason; }

    const knst_c16string& negotiated_subprotocol() const noexcept { return m_subprotocol; }

    const knst_c16string& negotiated_extensions() const noexcept { return m_extensions; }

    bool last_message_was_fragmented() const noexcept { return m_last_fragmented; }

    /* ---- send ---- */

    bool send_text(const knst_c16string& s, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (!can_send(err)) return false;
        knst_byte_string b = knst_net_detail::to_utf8(s);
        return send_data(0x1, b.data(), b.length(), err);
    }
    bool send_binary(const knst_byte_string& b, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (!can_send(err)) return false;
        return send_data(0x2, b.data(), b.length(), err);
    }
    bool send_ping(const knst_byte_string& payload, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (!can_send(err)) return false;
        if (payload.length() > 125) { err = knst_network_error::MessageTooLarge; return false; }
        return send_frame(*m_sock, 0x9, true, payload.data(), payload.length(), m_send_timeout, err);
    }
    bool send_pong(const knst_byte_string& payload, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        if (!can_send(err)) return false;
        if (payload.length() > 125) { err = knst_network_error::MessageTooLarge; return false; }
        return send_frame(*m_sock, 0xA, true, payload.data(), payload.length(), m_send_timeout, err);
    }

#if defined(KNST_NETWORK_ENABLE_JSON)
    bool send_json(const knst_json& j, knst_network_error& err,uint32_t indent = 0) noexcept {
        err = knst_network_error::None;
        if (!can_send(err)) return false;

        knst_json_buffer buf;
        knst_json_dump_options opt;
        opt.indent = indent;
        if (!j.dump(buf, opt)) {
            err = knst_network_error::OutOfMemory;
            return false;
        }

        const char* dp = reinterpret_cast<const char*>(buf.data());
        if (!dp) {
            err = knst_network_error::OutOfMemory;
            return false;
        }
        knst_c16string text(dp, static_cast<uint32_t>(buf.size()));
        return send_text(text, err);
    }

    bool recv_json(knst_json& out, uint32_t timeout_ms,
                   knst_network_error& err) noexcept {
        err = knst_network_error::None;
        message msg;
        if (!recv(msg, timeout_ms, err)) return false;
        if (msg.type != opcode::Text && msg.type != opcode::Binary) {
            err = knst_network_error::ProtocolError;
            return false;
        }
        knst_json_error e;
        if (!knst_json::parse(reinterpret_cast<const char*>(msg.data.data()), msg.data.length(), out, &e)) {
            err = knst_network_error::ProtocolError;
            return false;
        }
        return true;
    }
#endif

    /* ---- receive (blocking, one message) ---- */

    bool recv(message& out, uint32_t timeout_ms, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out.data.clear();
        out.type = opcode::Text;

        if (!m_sock) { err = knst_network_error::NotOpen; return false; }
        if (m_close_received) { err = knst_network_error::ConnectionClosed; return false; }

        uint32_t t = timeout_ms ? timeout_ms : m_recv_timeout;
        bool assembling = false;
        opcode asm_type = opcode::Text;

        for (;;) {
            opcode op;
            bool fin;
            knst_byte_string payload;
            // Consume any bytes that arrived together with the 101 handshake.
            if (!read_frame_buffered(op, fin, payload, t, m_max_message, err)) return false;

            if (op == opcode::Ping) {
                if (m_auto_pong) {
                    knst_network_error pe = knst_network_error::None;
                    send_frame(*m_sock, 0xA, true, payload.data(), payload.length(), m_send_timeout, pe);
                }
                continue;
            }
            if (op == opcode::Pong) continue;

            if (op == opcode::Close) {
                m_close_received = true;
                if (payload.length() >= 2) {
                    m_close_code = (uint16_t(payload[0]) << 8) | payload[1];
                    m_close_reason.clear();
                    for (uint32_t i = 2; i < payload.length(); ++i) {
                        uint8_t b = payload[i];
                        m_close_reason.append(b < 128 ? static_cast<char16_t>(b) : u'?');
                    }
                } else {
                    m_close_code = 1005;   // no status code
                }
                if (!m_close_sent) {
                    knst_network_error ce = knst_network_error::None;
                    if (payload.length() >= 2) {
                        uint8_t echo[2];
                        echo[0] = uint8_t((m_close_code >> 8) & 0xFF);
                        echo[1] = uint8_t(m_close_code & 0xFF);
                        send_frame(*m_sock, 0x8, true, echo, 2, m_send_timeout, ce);
                    } else {
                        // Server sent Close without a code; echo empty close
                        // (RFC 6455 §7.4.1: 1005 must never appear on the wire).
                        send_frame(*m_sock, 0x8, true, nullptr, 0, m_send_timeout, ce);
                    }
                    m_close_sent = true;
                }
                close_socket();
                err = knst_network_error::ConnectionClosed;
                return false;
            }

            if (op == opcode::Text || op == opcode::Binary) {
                if (assembling) { protocol_close(1002); err = knst_network_error::ProtocolError; return false; }
                if (fin) { m_last_fragmented = false; out.type = op; out.data = std::move(payload); return true; }
                out.type = op; out.data = std::move(payload);
                asm_type = op; assembling = true; continue;
            }
            if (op == opcode::Continuation) {
                if (!assembling) { protocol_close(1002); err = knst_network_error::ProtocolError; return false; }
                if (uint64_t(out.data.length()) + payload.length() > m_max_message) {
                    protocol_close(1009);
                    err = knst_network_error::MessageTooLarge; return false;
                }
                if (!knst_net_detail::append_bytes(out.data, payload.data(), payload.length())) {
                    err = knst_network_error::OutOfMemory; return false;
                }
                if (fin) { (void)asm_type; m_last_fragmented = true; return true; }
                continue;
            }

            protocol_close(1002);
            err = knst_network_error::ProtocolError;
            return false;
        }
    }

private:
    /* ---- private helpers ---- */

    bool can_send(knst_network_error& err) const noexcept {
        if (!m_sock) { err = knst_network_error::NotOpen; return false; }
        if (m_close_sent) { err = knst_network_error::ConnectionClosed; return false; }
        if (m_close_received){ err = knst_network_error::ConnectionClosed; return false; }
        return true;
    }

    void close_socket() noexcept { m_sock.reset(); m_prebuf.clear(); }

    void protocol_close(uint16_t code) noexcept {
        if (m_close_sent) return;
        uint8_t buf[2];
        buf[0] = uint8_t((code >> 8) & 0xFF);
        buf[1] = uint8_t(code & 0xFF);
        knst_network_error e = knst_network_error::None;
        send_frame(*m_sock, 0x8, true, buf, 2, m_send_timeout, e);
        m_close_sent = true;
    }

    // Send a data payload as one frame (if it fits) or fragmented.
    bool send_data(uint8_t initial_opcode, const uint8_t* data, size_t len, knst_network_error& err) noexcept {
                  
        if (len <= m_max_frame) {
            return send_frame(*m_sock, initial_opcode, true, data, len, m_send_timeout, err);
        }

        size_t off = 0;

        bool first = true;
        while (off < len) {
            size_t take = (len - off) < m_max_frame ? (len - off) : m_max_frame;
            bool fin = (off + take) >= len;
            uint8_t op = first ? initial_opcode : 0x0;
            if (!send_frame(*m_sock, op, fin, data + off, take, m_send_timeout, err)) return false;
            off += take; first = false;
        }
        return true;
    }

    // Buffered read_frame: honours any pre-buffered bytes from the 101 handshake.
    // Fallback path: when prebuf is empty we delegate straight to the fast
    // (unbuffered) read_frame. When prebuf has data we read the missing bytes
    // from the socket, concatenate, and parse.
    bool read_frame_buffered(opcode& out_op, bool& out_fin,
                             knst_byte_string& out_payload, uint32_t timeout_ms,
                             uint32_t max_frame, knst_network_error& err) noexcept {
        if (m_prebuf.length() == 0) {
            return read_frame(*m_sock, out_op, out_fin, out_payload, timeout_ms, max_frame, err);
        }
        // 1) Parse fixed 2-byte header from prebuf, fall through if short
        auto& pb = m_prebuf;
        auto need = [&](size_t n) -> bool {
            while (pb.length() < n) {
                uint8_t tmp[1024];
                size_t want = n - pb.length();
                size_t take = want < sizeof(tmp) ? want : sizeof(tmp);
                size_t got = 0;
                if (!m_sock->recv(tmp, take, got, timeout_ms, err)) return false;
                if (got == 0) { err = knst_network_error::ConnectionClosed; return false; }
                if (!knst_net_detail::append_bytes(pb, tmp, got)) {
                    err = knst_network_error::OutOfMemory; return false;
                }
            }
            return true;
        };
        if (!need(2)) return false;
        bool fin = (pb[0] & 0x80) != 0;
        uint8_t op = pb[0] & 0x0F;
        bool masked = (pb[1] & 0x80) != 0;
        uint64_t len = pb[1] & 0x7F;
        size_t hdr = 2;
        if (len == 126) { if (!need(4)) return false; len = ((uint64_t)pb[2] << 8) | pb[3]; hdr = 4; }
        else if (len == 127) {
            if (!need(10)) return false;
            len = 0; for (int i = 0; i < 8; ++i) len = (len << 8) | pb[2 + i];
            if (len & 0x8000000000000000ull) { err = knst_network_error::ProtocolError; return false; }
            hdr = 10;
        }
        if (op != 0x0 && op != 0x1 && op != 0x2 && op != 0x8 && op != 0x9 && op != 0xA) {
            err = knst_network_error::ProtocolError; return false;
        }
        if ((op & 0x08) && (!fin || len > 125)) { err = knst_network_error::ProtocolError; return false; }
        if (len > max_frame) { err = knst_network_error::MessageTooLarge; return false; }
        if (masked) { err = knst_network_error::ProtocolError; return false; }
        if (!need(hdr + (size_t)len)) return false;
        out_op = static_cast<opcode>(op);
        out_fin = fin;
        out_payload.clear();
        if (len > 0) {
            out_payload.resize((uint32_t)len);
            if (out_payload.length() != len) { err = knst_network_error::OutOfMemory; return false; }
            ::memcpy(&out_payload[0], pb.data() + hdr, (size_t)len);
        }
        const uint32_t consumed = static_cast<uint32_t>(hdr + (size_t)len);
        const uint32_t remain   = static_cast<uint32_t>(pb.length()) - consumed;
        if (remain > 0) {
                        ::memmove(const_cast<uint8_t*>(pb.data()), pb.data() + consumed, remain);
            pb.resize(remain);
        } else {
            pb.clear();
        }
        return true;
    }

    // Write one WebSocket frame (client → server; masked).
    static bool send_frame(knst_stream_socket_base& sock, uint8_t opcode, bool fin,
                           const uint8_t* payload, size_t len, uint32_t timeout_ms,
                           knst_network_error& err) noexcept {
        uint8_t hdr[14];
        int h = 0;
        hdr[h++] = static_cast<uint8_t>((fin ? 0x80 : 0x00) | (opcode & 0x0F));
        if (len < 126) {
            hdr[h++] = static_cast<uint8_t>(0x80 | len);
        } else if (len <= 0xFFFF) {
            hdr[h++] = 0x80 | 126;
            hdr[h++] = static_cast<uint8_t>((len >> 8) & 0xFF);
            hdr[h++] = static_cast<uint8_t>(len & 0xFF);
        } else {
            hdr[h++] = 0x80 | 127;
            for (int i = 7; i >= 0; --i) hdr[h++] = static_cast<uint8_t>((uint64_t(len) >> (i * 8)) & 0xFF);
        }
        uint8_t mask[4];
        knst_ws_detail::random_bytes(mask, 4);
        ::memcpy(hdr + h, mask, 4);
        h += 4;
        if (!sock.send_all(hdr, static_cast<size_t>(h), timeout_ms, err)) return false;

        if (len == 0) return true;

        // Mask payload in 16 KiB chunks.
        const size_t CHUNK = 16384;
        uint8_t buf[CHUNK];
        size_t off = 0;
        while (off < len) {
            size_t take = (len - off) < CHUNK ? (len - off) : CHUNK;
            for (size_t i = 0; i < take; ++i) buf[i] = payload[off + i] ^ mask[(off + i) & 3];
            if (!sock.send_all(buf, take, timeout_ms, err)) return false;
            off += take;
        }
        return true;
    }

    // Read one WebSocket frame (server → client; MUST be unmasked).
    static bool read_frame(knst_stream_socket_base& sock, opcode& out_op, bool& out_fin,
                           knst_byte_string& out_payload, uint32_t timeout_ms,
                           uint32_t max_frame, knst_network_error& err) noexcept {
        uint8_t hdr[2];
        if (!sock.recv_exact(hdr, 2, timeout_ms, err)) return false;
        out_fin = (hdr[0] & 0x80) != 0;
        uint8_t op = hdr[0] & 0x0F;
        bool masked = (hdr[1] & 0x80) != 0;
        uint64_t len = hdr[1] & 0x7F;

        if (op != 0x0 && op != 0x1 && op != 0x2 && op != 0x8 && op != 0x9 && op != 0xA) {
            err = knst_network_error::ProtocolError; return false;
        }
        if ((op & 0x08) && (!out_fin || len > 125)) { err = knst_network_error::ProtocolError; return false; }

        if (len == 126) {
            uint8_t ext[2];
            if (!sock.recv_exact(ext, 2, timeout_ms, err)) return false;
            len = (uint64_t(ext[0]) << 8) | ext[1];
        } else if (len == 127) {
            uint8_t ext[8];
            if (!sock.recv_exact(ext, 8, timeout_ms, err)) return false;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
            if (len & 0x8000000000000000ull) { err = knst_network_error::ProtocolError; return false; }
        }
        if (len > max_frame) { err = knst_network_error::MessageTooLarge; return false; }
        if (masked) { err = knst_network_error::ProtocolError; return false; }   // server MUST NOT mask

        out_op = static_cast<opcode>(op);
        out_payload.clear();
        if (len > 0) {
            out_payload.resize(static_cast<uint32_t>(len));
            if (out_payload.length() != len) { err = knst_network_error::OutOfMemory; return false; }
            if (!sock.recv_exact(&out_payload[0], static_cast<size_t>(len), timeout_ms, err)) return false;
        }
        return true;
    }

    // Read an HTTP response head (status line + headers, up to CRLFCRLF).
    static bool read_http_head(knst_stream_socket_base& sock, uint32_t timeout_ms,
                               uint32_t max_bytes, knst_byte_string& head,
                               knst_byte_string& leftover,
                               knst_network_error& err) noexcept {
        head.clear(); leftover.clear();
        uint8_t tmp[4096];
        for (;;) {
            if (head.length() > max_bytes) { err = knst_network_error::HttpMalformedHeaders; return false; }
            size_t n = 0;
            if (!sock.recv(tmp, sizeof(tmp), n, timeout_ms, err)) return false;
            if (n == 0) { err = knst_network_error::ConnectionClosed; return false; }
            if (!knst_net_detail::append_bytes(head, tmp, n)) { err = knst_network_error::OutOfMemory; return false; }
            const uint8_t* p = head.data();
            uint32_t L = head.length();
            for (uint32_t i = 0; i + 3 < L; ++i) {
                if (p[i] == '\r' && p[i+1] == '\n' && p[i+2] == '\r' && p[i+3] == '\n') {
                    uint32_t hend = i + 4;
                    if (hend < L && !knst_net_detail::append_bytes(leftover, p + hend, L - hend)) {
                        err = knst_network_error::OutOfMemory; return false;
                    }
                    head.resize(hend);
                    return true;
                }
            }
        }
    }


    std::unique_ptr<knst_tcp_socket> m_sock;
    knst_byte_string m_prebuf; // 101 handshake leftover (server pushed first frame in same segment)
    knst_c16string m_subprotocol;
    knst_c16string m_extensions;
    uint16_t m_close_code = 0;
    knst_c16string m_close_reason;


    bool m_close_sent = false;
    bool m_close_received = false;
    bool m_auto_pong = true;
    bool m_last_fragmented = false;

    uint32_t m_send_timeout = 10000;
    uint32_t m_recv_timeout = 10000;
    uint32_t m_max_message = 16u * 1024 * 1024;
    uint32_t m_max_frame = 4u * 1024 * 1024;
};