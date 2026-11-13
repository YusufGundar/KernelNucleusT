// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/* ============================================================================
 * knst_network_h2.hpp — HTTP/2 client for KernelNucleusT (single header).
 * ----------------------------------------------------------------------------
 *  Requires : knst_network.hpp, libnghttp2 >= 1.40, OpenSSL >= 1.1.0,
 *             KNST_NETWORK_ENABLE_TLS (without it every call fails with
 *             TlsNotSupported). Link: -lnghttp2 -lssl -lcrypto
 *
 *  Layers in this file
 *    knst_web_detail      shared by h2 AND h3: proxy, cookie jar, header
 *                         validation, resolver helpers, response assembler,
 *                         TLS ctx builder, poll helpers
 *    knst_web_options     options (= knst_http_client::options + h2/h3 extras)
 *    knst_web_client_base CRTP base: redirects, cookies, auth, decoding
 *    knst_web_session_pool<S> per-origin session pool (shared_ptr based)
 *    knst_h2_socket / knst_h2_session / knst_http2_client
 *
 *  Behaviour notes
 *    - https only (no h2c). If the server does not negotiate "h2" the call
 *      fails with NotSupported (caller may fall back to knst_http_client).
 *    - options::lenient_close_delimited : IGNORED (HTTP/2 is framed).
 *    - options::use_chunked_upload      : InvalidArgument (DATA is framed).
 *    - options::max_per_host            : unused (see max_sessions_per_origin).
 *    - options::use_keepalive = false   : session is retired after the request.
 *    - options::on_body may be invoked from ANOTHER thread than the caller's
 *      (the thread currently driving the shared connection). Calls are
 *      serialized per session; the callback must not re-enter the client.
 *    - Proxies: HTTP CONNECT and SOCKS5/SOCKS5h (https targets only).
 *    - Call knst_tls_set_ca_file/path/client_cert BEFORE the first connection
 *      of either client (each builds its own SSL_CTX lazily).
 *
 *  Thread safety
 *    One knst_http2_client may be shared by many threads. All requests to one
 *    origin multiplex over one TLS connection; one thread at a time drives the
 *    socket (driver election), nghttp2 is only touched under m_mtx.
 *
 *  Containers: knst_vector is used for public/response types; std::vector /
 *  std::string are used internally where nghttp2's contiguous API needs them.
 * ============================================================================ */

#pragma once


#include <nghttp2/nghttp2.h>
#if NGHTTP2_VERSION_NUM < 0x012800
    #error "knst_network_h2.hpp: nghttp2 >= 1.40 required"
#endif

#if defined(KNST_NETWORK_ENABLE_TLS)
    #include <openssl/ssl.h>
    #include <openssl/err.h>
    #include <openssl/x509.h>
    #include <openssl/x509v3.h>
    #if OPENSSL_VERSION_NUMBER < 0x10100000L
        #error "knst_network_h2.hpp: OpenSSL >= 1.1.0 required"
    #endif
#endif

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <utility>

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4996)
#endif

#if KNST_NET_HAS_EXCEPTIONS
    #define KNST_WEB_GUARD_BEGIN try {
    #define KNST_WEB_GUARD_END(errvar) } catch (...) { errvar = knst_network_error::OutOfMemory; return false; }
#else
    #define KNST_WEB_GUARD_BEGIN {
    #define KNST_WEB_GUARD_END(errvar) }
#endif


/* ============================================================================
 * knst_web_detail
 * ============================================================================ */

// Protocol-neutral request description (built by knst_web_client_base).
struct knst_web_header {
    knst_byte_string name;
    knst_byte_string value;
};

struct knst_web_request {
    knst_byte_string method, authority, path;
    knst_vector<knst_web_header> headers;   // lowercase names
    knst_vector<knst_web_header> trailers;  // lowercase names
    const uint8_t* body = nullptr;
    size_t         body_len = 0;
};

// Options shared by the h2 / h3 clients.
struct knst_web_options : public knst_http_client::options {
    knst_vector<knst_http_header> request_trailers;       // sent after the body
    bool     expect_continue            = false;          // defer body until "100 Continue"
    uint32_t expect_continue_timeout_ms = 1000;           // ...or this long
    uint32_t max_concurrent_streams     = 100;            // local cap per session
    uint32_t initial_stream_window      = 1u << 20;       // 1 MiB
    uint32_t connection_window          = 16u << 20;      // 16 MiB
    uint32_t max_sessions_per_origin    = 2;              // extra sessions only when saturated
    uint32_t cancel_grace_ms            = 2000;           // wait for RST to flush before killing the session

    knst_web_options() = default;
    knst_web_options(const knst_http_client::options& b) : knst_http_client::options(b) {}
};

namespace knst_web_detail {

    typedef knst_net_detail::deadline                 deadline_t;
    typedef std::chrono::steady_clock::time_point     time_pt;

    // ms budget for one I/O step under `dl`; ms == 0 means "infinite".
    inline bool io_budget(const deadline_t& dl, uint32_t& ms, knst_network_error& err) noexcept {
        bool expired = false;
        uint32_t rem = dl.remaining_ms(expired);
        if (expired) { err = knst_network_error::Timeout; return false; }
        ms = (rem == knst_net_detail::KNST_NET_INFINITE) ? 0 : rem;
        return true;
    }

    /* ---------------------------------------------------------------- proxy */

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
        bool enabled() const noexcept { return kind != kind_t::None; }
    };

    inline bool host_in_no_proxy(const knst_c16string& host, const knst_c16string& list) {
        uint32_t start = 0;
        for (uint32_t k = 0; k <= list.length(); ++k) {
            if (k < list.length() && list[k] != u',' && list[k] != u' ' && list[k] != u'\t') continue;
            knst_c16string tok = knst_net_detail::lower16(knst_net_detail::trim16(list.substr(start, k - start)));
            start = k + 1;
            if (tok.empty()) continue;
            if (tok.length() == 1 && tok[0] == u'*') return true;
            uint32_t colons = 0, last = 0;
            for (uint32_t j = 0; j < tok.length(); ++j) if (tok[j] == u':') { ++colons; last = j; }
            if (colons == 1) tok = tok.substr(0, last);
            while (tok.length() && (tok[0] == u'*' || tok[0] == u'.')) tok = tok.substr(1, tok.length() - 1);
            if (tok.empty()) continue;
            if (host == tok) return true;
            if (host.length() > tok.length()) {
                uint32_t off = host.length() - tok.length();
                if (host[off - 1] == u'.') {
                    bool same = true;
                    for (uint32_t j = 0; j < tok.length(); ++j) if (host[off + j] != tok[j]) { same = false; break; }
                    if (same) return true;
                }
            }
        }
        return false;
    }

    inline bool resolve_proxy(const knst_url& target, const knst_http_client::options& o,
                              proxy_cfg& pc, knst_network_error& err) noexcept {
        pc = proxy_cfg{};
        err = knst_network_error::None;
        knst_c16string purl = o.proxy_url;
        char buf[2048];
        if (purl.empty() && o.use_env_proxy) {
            // Only lowercase http_proxy is honoured for plain http (httpoxy); here target is https.
            const char* names[2] = { "https_proxy", "HTTPS_PROXY" };
            for (int i = 0; i < 2; ++i)
                if (knst_net_detail::get_env(names[i], buf, sizeof(buf))) { purl = knst_net_detail::ascii_to_c16(buf); break; }
        }
        if (purl.empty()) return true;

        knst_c16string np = o.no_proxy;
        if (np.empty()) {
            if (knst_net_detail::get_env("no_proxy", buf, sizeof(buf)) || knst_net_detail::get_env("NO_PROXY", buf, sizeof(buf)))
                np = knst_net_detail::ascii_to_c16(buf);
        }
        if (!np.empty() && host_in_no_proxy(target.host, np)) return true;

        if (!knst_net_detail::has_scheme(purl)) { knst_c16string t = u"http://"; t.append(purl); purl = t; }
        knst_url pu;
        if (!knst_url::parse(purl, pu, err)) { err = knst_network_error::ProxyFailed; return false; }
        if (pu.scheme == u"http")        pc.kind = proxy_cfg::kind_t::Http;
        else if (pu.scheme == u"socks5") { pc.kind = proxy_cfg::kind_t::Socks5; pc.socks5_remote_dns = false; }
        else if (pu.scheme == u"socks5h"){ pc.kind = proxy_cfg::kind_t::Socks5; pc.socks5_remote_dns = true; }
        else { err = knst_network_error::NotSupported; return false; }
        pc.host = pu.host;
        pc.port = pu.effective_port();
        if (!pu.userinfo.empty()) {
            knst_byte_string ui = knst_net_detail::to_utf8(pu.userinfo);
            uint32_t colon = ui.length();
            for (uint32_t i = 0; i < ui.length(); ++i) if (ui[i] == ':') { colon = i; break; }
            knst_byte_string user, pass;
            for (uint32_t i = 0; i < colon; ++i) user.push_back(ui[i]);
            for (uint32_t i = colon + 1; i < ui.length(); ++i) pass.push_back(ui[i]);
            knst_byte_string up = knst_net_detail::percent_decode_bytes(user);
            knst_byte_string pw = knst_net_detail::percent_decode_bytes(pass);
            if (pc.kind == proxy_cfg::kind_t::Http) {
                knst_byte_string uc = up; uc.push_back(':');
                for (uint32_t i = 0; i < pw.length(); ++i) uc.push_back(pw[i]);
                pc.auth = u"Basic ";
                pc.auth.append(knst_net_detail::base64_encode_bytes(uc));
            } else { pc.user_raw = up; pc.pass_raw = pw; }
            pc.has_auth = true;
        }
        return true;
    }

    // HTTP CONNECT tunnel (deadline-bounded, response head capped at 16 KiB).
    inline bool proxy_connect_tunnel(knst_tcp_socket& sock, const knst_url& u, const proxy_cfg& pc,
                                     const knst_http_client::options& o, const deadline_t& dl,
                                     knst_network_error& err) noexcept {
        err = knst_network_error::None;
        knst_c16string hp = u.host_bracketed();
        hp.append(u':');
        knst_net_detail::append_uint(hp, u.effective_port());
        knst_http_request r;
        r.method = u"CONNECT"; r.path = hp;
        r.set_header_internal(u"Host", hp);
        if (!o.user_agent.empty()) r.set_header_internal(u"User-Agent", o.user_agent);
        r.set_header_internal(u"Proxy-Connection", u"keep-alive");
        if (pc.has_auth) r.set_header_internal(u"Proxy-Authorization", pc.auth);
        knst_byte_string bytes = r.serialize();
        uint32_t ms = 0;
        if (!io_budget(dl, ms, err)) return false;
        if (!sock.send_all(bytes, ms, err)) return false;

        knst_byte_string head;
        uint8_t buf[2048];
        uint32_t scan = 0; bool found = false; uint32_t hend = 0;
        while (!found) {
            if (!io_budget(dl, ms, err)) return false;
            size_t got = 0;
            if (!sock.recv(buf, sizeof(buf), got, ms, err)) return false;
            if (got == 0) { err = knst_network_error::ConnectionClosed; return false; }
            if (head.length() + got > 16384) { err = knst_network_error::ProxyFailed; return false; }
            if (!knst_net_detail::append_bytes(head, buf, got)) { err = knst_network_error::OutOfMemory; return false; }
            const uint8_t* p = head.data(); uint32_t L = head.length();
            for (uint32_t i = scan; i + 3 < L; ++i)
                if (p[i] == '\r' && p[i+1] == '\n' && p[i+2] == '\r' && p[i+3] == '\n') { hend = i + 4; found = true; break; }
            scan = L > 3 ? L - 3 : 0;
        }
        bool extra = hend < head.length();
        head.resize(hend);
        knst_http_response pr;
        if (!knst_http_response::parse(head, pr, err)) { err = knst_network_error::ProxyFailed; return false; }
        if (pr.status < 200 || pr.status >= 300 || extra) { err = knst_network_error::ProxyFailed; return false; }
        return true;
    }

    // RFC 1928 SOCKS5 CONNECT (name is always sent to the proxy unless it is an IP literal).
    inline bool socks5_handshake(knst_tcp_socket& sock, const knst_c16string& target_host, uint16_t target_port,
                                 const proxy_cfg& pc, const deadline_t& dl, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        auto budget = [&](bool& ok) -> uint32_t {
            uint32_t ms = 0; ok = io_budget(dl, ms, err); return ms;
        };
        {
            uint8_t greet[4]; int n = 0;
            greet[n++] = 0x05;
            if (pc.has_auth) { greet[n++] = 0x02; greet[n++] = 0x00; greet[n++] = 0x02; }
            else             { greet[n++] = 0x01; greet[n++] = 0x00; }
            bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.send_all(greet, static_cast<size_t>(n), t, err)) return false;
        }
        uint8_t method = 0;
        {
            uint8_t resp[2]; bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.recv_exact(resp, 2, t, err)) return false;
            if (resp[0] != 0x05 || resp[1] == 0xFF) { err = knst_network_error::ProxyFailed; return false; }
            method = resp[1];
        }
        if (method == 0x02) {
            if (!pc.has_auth || pc.user_raw.length() > 255 || pc.pass_raw.length() > 255) { err = knst_network_error::ProxyFailed; return false; }
            uint8_t buf[513]; int n = 0;
            buf[n++] = 0x01;
            buf[n++] = static_cast<uint8_t>(pc.user_raw.length());
            for (uint32_t i = 0; i < pc.user_raw.length(); ++i) buf[n++] = pc.user_raw[i];
            buf[n++] = static_cast<uint8_t>(pc.pass_raw.length());
            for (uint32_t i = 0; i < pc.pass_raw.length(); ++i) buf[n++] = pc.pass_raw[i];
            bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.send_all(buf, static_cast<size_t>(n), t, err)) return false;
            uint8_t resp[2]; t = budget(ok); if (!ok) return false;
            if (!sock.recv_exact(resp, 2, t, err)) return false;
            if (resp[0] != 0x01 || resp[1] != 0x00) { err = knst_network_error::ProxyFailed; return false; }
        } else if (method != 0x00) { err = knst_network_error::ProxyFailed; return false; }
        {
            uint8_t addr[1 + 1 + 255]; int addr_total = 0;
            knst_network_error ip_err = knst_network_error::None;
            knst_ip_address ip = knst_ip_address::from_string(target_host, ip_err);
            if (ip_err == knst_network_error::None && ip.is_valid()) {
                if (ip.is_ipv4()) { addr[0] = 0x01; uint8_t r4[4] = {}; ip.to_raw_ipv4(r4); ::memcpy(addr + 1, r4, 4); addr_total = 5; }
                else              { addr[0] = 0x04; uint8_t r6[16] = {}; ip.to_raw_ipv6(r6); ::memcpy(addr + 1, r6, 16); addr_total = 17; }
            } else {
                knst_byte_string h8 = knst_net_detail::to_utf8(target_host);
                if (h8.length() == 0 || h8.length() > 255) { err = knst_network_error::ProxyFailed; return false; }
                addr[0] = 0x03; addr[1] = static_cast<uint8_t>(h8.length());
                ::memcpy(addr + 2, h8.data(), h8.length());
                addr_total = 2 + static_cast<int>(h8.length());
            }
            uint8_t req[3 + 258 + 2]; int n = 0;
            req[n++] = 0x05; req[n++] = 0x01; req[n++] = 0x00;
            for (int i = 0; i < addr_total; ++i) req[n++] = addr[i];
            req[n++] = static_cast<uint8_t>((target_port >> 8) & 0xFF);
            req[n++] = static_cast<uint8_t>(target_port & 0xFF);
            bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.send_all(req, static_cast<size_t>(n), t, err)) return false;
        }
        {
            uint8_t resp[4]; bool ok; uint32_t t = budget(ok); if (!ok) return false;
            if (!sock.recv_exact(resp, 4, t, err)) return false;
            if (resp[0] != 0x05) { err = knst_network_error::ProxyFailed; return false; }
            if (resp[1] != 0x00) {
                err = (resp[1] == 0x05) ? knst_network_error::ConnectionRefused : knst_network_error::ProxyFailed;
                return false;
            }
            int consume = 0;
            if (resp[3] == 0x01) consume = 4;
            else if (resp[3] == 0x04) consume = 16;
            else if (resp[3] == 0x03) {
                uint8_t dlen = 0; t = budget(ok); if (!ok) return false;
                if (!sock.recv_exact(&dlen, 1, t, err)) return false;
                consume = static_cast<int>(dlen);
            } else { err = knst_network_error::ProxyFailed; return false; }
            consume += 2;
            uint8_t dummy[64];
            while (consume > 0) {
                int c = consume > 64 ? 64 : consume;
                t = budget(ok); if (!ok) return false;
                if (!sock.recv_exact(dummy, static_cast<size_t>(c), t, err)) return false;
                consume -= c;
            }
        }
        return true;
    }

    /* ------------------------------------------------- DNS / SSRF helpers */

    // Cache + async DNS (bounded by `dl`) + optional SSRF filtering.
    inline bool resolve_endpoints(const knst_c16string& host, uint16_t port, const knst_http_client::options& o,
                                  uint32_t dns_to, const deadline_t& dl, bool filter,
                                  knst_vector<knst_endpoint>& out, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        out.clear();
        bool hit = o.dns_cache && knst_dns_cache::global().lookup(host, port, out);
        if (!hit) {
            if (!knst_dns_resolver::resolve_async(host, port, out, err, knst_net_detail::dns_budget(dns_to, dl))) return false;
            if (o.dns_cache) knst_dns_cache::global().store(host, out, o.dns_cache_ttl_ms);
        }
        if (filter) {
            knst_vector<knst_endpoint> keep;
            for (uint32_t i = 0; i < out.size(); ++i) {
                if (out[i].address.is_non_public()) continue;
                if (!keep.push_back(out[i])) { err = knst_network_error::OutOfMemory; return false; }
            }
            if (keep.size() == 0) { err = knst_network_error::ForbiddenAddress; return false; }
            out = std::move(keep);
        }
        return true;
    }

    // SSRF guard for IP-literal targets behind a proxy (proxy resolves names itself).
    inline bool literal_target_allowed(const knst_c16string& host, knst_network_error& err) noexcept {
        if (!knst_net_detail::looks_like_ip_literal(host)) return true;
        knst_network_error e2 = knst_network_error::None;
        knst_ip_address ip = knst_ip_address::from_string(host, e2);
        if (e2 != knst_network_error::None || ip.is_non_public()) { err = knst_network_error::ForbiddenAddress; return false; }
        return true;
    }

    /* ---------------------------------------------------------- cookie jar */

    class knst_cookie_jar_impl {
    public:
        void clear() { std::lock_guard<std::mutex> lk(m_mtx); m_c.clear(); }
        knst_vector<knst_cookie> snapshot() const { std::lock_guard<std::mutex> lk(m_mtx); return m_c; }

        knst_c16string header_for(const knst_url& u) const {
            std::lock_guard<std::mutex> lk(m_mtx);
            std::vector<knst_cookie> hit;
            int64_t now = now_s();
            for (uint32_t i = 0; i < m_c.size(); ++i) {
                const knst_cookie& c = m_c[i];
                if (c.expires_unix > 0 && c.expires_unix < now) continue;
                bool host_ok = c.host_only ? (c.domain == u.host) : domain_match(u.host, c.domain);
                if (!host_ok) continue;
                if (c.secure && u.scheme != u"https") continue;
                if (!path_match(u.path, c.path)) continue;
                hit.push_back(c);
            }
            std::stable_sort(hit.begin(), hit.end(), [](const knst_cookie& a, const knst_cookie& b) {
                return a.path.length() > b.path.length();      // RFC 6265 §5.4: longer paths first
            });
            knst_c16string h;
            for (size_t i = 0; i < hit.size(); ++i) {
                if (h.length()) h.append(u"; ");
                h.append(hit[i].name); h.append(u'='); h.append(hit[i].value);
            }
            return h;
        }

        void collect(const knst_http_response& resp, const knst_url& origin) {
            std::lock_guard<std::mutex> lk(m_mtx);
            int64_t now = now_s();
            for (uint32_t i = 0; i < m_c.size(); )
                if (m_c[i].expires_unix > 0 && m_c[i].expires_unix <= now) m_c.erase(i); else ++i;
            const bool origin_is_ip = knst_net_detail::looks_like_ip_literal(origin.host);
            for (uint32_t i = 0; i < resp.headers.size(); ++i) {
                if (!knst_net_detail::ieq(resp.headers[i].name, u"set-cookie")) continue;
                const knst_c16string& v = resp.headers[i].value;
                if (v.empty()) continue;
                uint32_t eq = 0; bool has_eq = false;
                for (uint32_t k = 0; k < v.length(); ++k) if (v[k] == u'=') { eq = k; has_eq = true; break; }
                if (!has_eq || eq == 0) continue;
                knst_cookie c;
                uint32_t ne = eq;
                while (ne > 0 && (v[ne - 1] == u' ' || v[ne - 1] == u'\t')) --ne;
                c.name = v.substr(0, ne);
                uint32_t semi = v.length();
                for (uint32_t k = eq + 1; k < v.length(); ++k) if (v[k] == u';') { semi = k; break; }
                uint32_t vs = eq + 1, ve = semi;
                while (vs < ve && (v[vs] == u' ' || v[vs] == u'\t')) ++vs;
                while (ve > vs && (v[ve - 1] == u' ' || v[ve - 1] == u'\t')) --ve;
                c.value = v.substr(vs, ve - vs);
                if (c.name.empty() || !knst_net_detail::valid_header_name(c.name)) continue;
                {
                    bool bad = false;
                    for (uint32_t q = 0; q < c.value.length(); ++q) {
                        char16_t ch = c.value[q];
                        if ((ch < 0x20 && ch != u'\t') || ch == 0x7F || ch >= 0x80) { bad = true; break; }
                    }
                    if (bad) continue;
                }
                c.domain = origin.host; c.path = default_path(origin.path); c.host_only = true;
                int64_t max_age = -2, expires_abs = 0; bool reject = false;
                uint32_t k = semi;
                while (k < v.length() && !reject) {
                    while (k < v.length() && (v[k] == u';' || v[k] == u' ' || v[k] == u'\t')) ++k;
                    uint32_t ks = k;
                    while (k < v.length() && v[k] != u'=' && v[k] != u';') ++k;
                    uint32_t ke = k;
                    while (ke > ks && (v[ke - 1] == u' ' || v[ke - 1] == u'\t')) --ke;
                    knst_c16string key = v.substr(ks, ke - ks), val;
                    if (k < v.length() && v[k] == u'=') {
                        ++k; uint32_t a = k;
                        while (k < v.length() && v[k] != u';') ++k;
                        uint32_t b = k;
                        while (b > a && (v[b - 1] == u' ' || v[b - 1] == u'\t')) --b;
                        while (a < b && (v[a] == u' ' || v[a] == u'\t')) ++a;
                        val = v.substr(a, b - a);
                    }
                    if (knst_net_detail::ieq(key, u"Domain")) {
                        knst_c16string dom = val;
                        if (dom.length() && dom[0] == u'.') dom = dom.substr(1, dom.length() - 1);
                        dom = knst_net_detail::lower16(dom);
                        uint32_t dots = 0;
                        for (uint32_t j = 0; j < dom.length(); ++j) if (dom[j] == u'.') ++dots;
                        if (dom.empty() || !domain_match(origin.host, dom) || dots == 0) { reject = true; break; }
                        if (origin_is_ip && dom != origin.host) { reject = true; break; }
                        c.domain = dom; c.host_only = false;
                    } else if (knst_net_detail::ieq(key, u"Path")) {
                        if (val.length() && val[0] == u'/') c.path = val;
                    } else if (knst_net_detail::ieq(key, u"Secure")) {
                        c.secure = true;
                    } else if (knst_net_detail::ieq(key, u"Max-Age")) {
                        int64_t ma = 0; bool neg = false; uint32_t j = 0;
                        if (val.length() && val[0] == u'-') { neg = true; j = 1; }
                        for (; j < val.length(); ++j) {
                            char16_t cc = val[j];
                            if (cc < u'0' || cc > u'9') break;
                            if (ma < 100000000000LL) ma = ma * 10 + (cc - u'0');
                        }
                        max_age = neg ? -ma : ma;
                    } else if (knst_net_detail::ieq(key, u"Expires")) {
                        expires_abs = parse_date(val);
                    }
                }
                if (reject) continue;
                if (c.secure && origin.scheme != u"https") continue;
                if (max_age != -2)        c.expires_unix = (max_age <= 0) ? (now - 1) : (now + max_age);
                else if (expires_abs > 0) c.expires_unix = expires_abs;
                else                      c.expires_unix = 0;
                if (c.name.length() + c.value.length() > 4096) continue;
                if (m_c.size() >= 3000) continue;
                bool replaced = false;
                for (uint32_t j = 0; j < m_c.size(); ++j) {
                    if (m_c[j].name == c.name && m_c[j].domain == c.domain && m_c[j].path == c.path) {
                        if (c.expires_unix != 0 && c.expires_unix < now) m_c.erase(j); else m_c[j] = c;
                        replaced = true; break;
                    }
                }
                if (!replaced && (c.expires_unix == 0 || c.expires_unix > now)) m_c.push_back(c);
            }
        }

    private:
        static int64_t now_s() {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }
        static bool domain_match(const knst_c16string& host, const knst_c16string& domain) {
            if (host.length() < domain.length()) return false;
            uint32_t off = host.length() - domain.length();
            if (off > 0 && host[off - 1] != u'.') return false;
            for (uint32_t i = 0; i < domain.length(); ++i) {
                char16_t a = host[off + i], b = domain[i];
                if (a >= u'A' && a <= u'Z') a = static_cast<char16_t>(a - u'A' + u'a');
                if (b >= u'A' && b <= u'Z') b = static_cast<char16_t>(b - u'A' + u'a');
                if (a != b) return false;
            }
            return true;
        }
        static bool path_match(const knst_c16string& req, const knst_c16string& cp) {
            if (req.length() < cp.length()) return false;
            for (uint32_t i = 0; i < cp.length(); ++i) if (req[i] != cp[i]) return false;
            if (req.length() == cp.length()) return true;
            if (cp.length() > 0 && cp[cp.length() - 1] == u'/') return true;
            return req[cp.length()] == u'/';
        }
        static knst_c16string default_path(const knst_c16string& p) {
            if (p.empty() || p[0] != u'/') return knst_c16string(u"/");
            uint32_t last = 0;
            for (uint32_t i = 0; i < p.length(); ++i) if (p[i] == u'/') last = i;
            if (last == 0) return knst_c16string(u"/");
            return p.substr(0, last);
        }
        static int64_t parse_date(const knst_c16string& s) {     // IMF-fixdate only
            uint32_t i = 0;
            while (i < s.length() && s[i] != u' ') ++i;
            while (i < s.length() && s[i] == u' ') ++i;
            if (i >= s.length()) return 0;
            int day = 0;
            while (i < s.length() && s[i] >= u'0' && s[i] <= u'9') { day = day * 10 + (s[i] - u'0'); ++i; }
            while (i < s.length() && s[i] == u' ') ++i;
            static const char* months[] = {"jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec"};
            int month = -1;
            for (int m = 0; m < 12; ++m) {
                bool ok = true;
                for (uint32_t j = 0; j < 3; ++j) {
                    if (i + j >= s.length()) { ok = false; break; }
                    char16_t c = s[i + j];
                    if (c >= u'A' && c <= u'Z') c = static_cast<char16_t>(c - u'A' + u'a');
                    if (c != static_cast<char16_t>(months[m][j])) { ok = false; break; }
                }
                if (ok) { month = m; break; }
            }
            if (month < 0) return 0;
            i += 3;
            while (i < s.length() && s[i] == u' ') ++i;
            int year = 0;
            while (i < s.length() && s[i] >= u'0' && s[i] <= u'9') { year = year * 10 + (s[i] - u'0'); ++i; }
            while (i < s.length() && s[i] == u' ') ++i;
            int hh = 0, mm = 0, ss = 0;
            while (i < s.length() && s[i] >= u'0' && s[i] <= u'9') { hh = hh * 10 + (s[i] - u'0'); ++i; }
            if (i < s.length() && s[i] == u':') ++i;
            while (i < s.length() && s[i] >= u'0' && s[i] <= u'9') { mm = mm * 10 + (s[i] - u'0'); ++i; }
            if (i < s.length() && s[i] == u':') ++i;
            while (i < s.length() && s[i] >= u'0' && s[i] <= u'9') { ss = ss * 10 + (s[i] - u'0'); ++i; }
            if (year < 1970 || year > 9999 || day < 1 || day > 31) return 0;
            int y = year - (month <= 1 ? 1 : 0);
            int era = (y >= 0 ? y : y - 399) / 400;
            int yoe = y - era * 400;
            int doy = (153 * (month + (month > 1 ? -3 : 9)) + 2) / 5 + day - 1;
            int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
            return days * 86400 + hh * 3600 + mm * 60 + ss;
        }

        mutable std::mutex       m_mtx;
        knst_vector<knst_cookie> m_c;
    };

    /* --------------------------------------------------- header validation */

    inline std::string c16_utf8(const knst_c16string& s) {
        knst_byte_string b = knst_net_detail::to_utf8(s);
        if (b.length() == 0) return std::string();
        return std::string(reinterpret_cast<const char*>(b.data()), b.length());
    }
    inline std::string c16_lower_ascii(const knst_c16string& s) {
        std::string r;
        r.reserve(s.length());
        for (uint32_t i = 0; i < s.length(); ++i) {
            char16_t c = s[i];
            if (c >= 128) continue;
            if (c >= u'A' && c <= u'Z') c = static_cast<char16_t>(c - u'A' + u'a');
            r.push_back(static_cast<char>(c));
        }
        return r;
    }
    // RFC 9113 §8.2.2 connection-specific headers + headers the library owns.
        inline bool is_reserved_header(const knst_byte_string& n, const knst_byte_string& v) noexcept {
        return n == "host" || n == "content-length" || n == "transfer-encoding" || n == "connection" ||
               n == "keep-alive" || n == "proxy-connection" || n == "upgrade" || n == "expect" ||
               n == "proxy-authorization" || n == "accept-encoding" || n == "trailer" ||
               (n == "te" && v != "trailers");
    }
    inline bool is_sensitive_name(const knst_byte_string& n) noexcept {
        return n == "authorization" || n == "cookie" || n == "x-api-key" || n == "x-auth-token" || n == "proxy-authorization";
    }
    inline void set_pair(knst_vector<knst_web_header>& v, knst_byte_string n, knst_byte_string val) noexcept {
        for (uint32_t i = 0; i < v.size(); ++i) {
            if (v[i].name == n) { v[i].value = std::move(val); return; }
        }
        knst_web_header h;
        h.name = std::move(n);
        h.value = std::move(val);
        v.push_back(std::move(h));
    }
    inline bool has_pair(const knst_vector<knst_web_header>& v, const knst_byte_string& n) noexcept {
        for (uint32_t i = 0; i < v.size(); ++i) if (v[i].name == n) return true;
        return false;
    }
    inline bool check_user_header(const knst_http_header& h, knst_byte_string& n, knst_byte_string& v, knst_network_error& err) {
        if (h.name.length() > 0 && h.name[0] == u':') { err = knst_network_error::HttpInvalidHeader; return false; }
        if (!knst_net_detail::valid_header_name(h.name) || !knst_net_detail::valid_header_value(h.value)) {
            err = knst_network_error::HttpInvalidHeader; return false;
        }
        n = knst_byte_string(c16_lower_ascii(h.name));
        v = knst_byte_string(c16_utf8(h.value));
        if (n.empty() || is_reserved_header(n, v)) { err = knst_network_error::HttpInvalidHeader; return false; }
        return true;
    }
    inline bool is_idempotent_method(const knst_byte_string& m) noexcept {
        return m == "GET" || m == "HEAD" || m == "PUT" || m == "DELETE" || m == "OPTIONS" || m == "TRACE";
    }

    /* ----------------------------------------------- wake event (poll breaker) */

    // Loopback UDP socket used to interrupt a thread blocked in poll().
    class wake_event {
    public:
        wake_event() noexcept = default;
        ~wake_event() noexcept { close(); }
        wake_event(const wake_event&) = delete;
        wake_event& operator=(const wake_event&) = delete;

        bool init() noexcept {
            close();
            m_h = knst_net_detail::create_socket(AF_INET, SOCK_DGRAM, 0);
            if (m_h == KNST_INVALID_SOCKET) return false;
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = 0;
            if (::bind(m_h, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { close(); return false; }
            socklen_t sl = sizeof(m_addr);
            if (::getsockname(m_h, reinterpret_cast<sockaddr*>(&m_addr), &sl) != 0) { close(); return false; }
            return true;
        }
        void signal() noexcept {
            if (m_h == KNST_INVALID_SOCKET) return;
            char c = 1;
            ::sendto(m_h, &c, 1, 0, reinterpret_cast<const sockaddr*>(&m_addr), sizeof(m_addr));
        }
        void drain() noexcept {
            if (m_h == KNST_INVALID_SOCKET) return;
            char b[64];
            for (int i = 0; i < 64; ++i) {
            #if KNST_USING_PLATFORM_WINDOWS
                int n = ::recvfrom(m_h, b, sizeof(b), 0, nullptr, nullptr);
            #else
                ssize_t n = ::recvfrom(m_h, b, sizeof(b), 0, nullptr, nullptr);
            #endif
                if (n < 0) break;
            }
        }
        knst_socket_handle_t handle() const noexcept { return m_h; }
        bool ok() const noexcept { return m_h != KNST_INVALID_SOCKET; }
        void close() noexcept { knst_net_detail::close_handle(m_h); m_h = KNST_INVALID_SOCKET; }
    private:
        knst_socket_handle_t m_h = KNST_INVALID_SOCKET;
        sockaddr_in          m_addr{};
    };

    // Waits for: socket readable (always), socket writable (if want_write), wake fd.
    // Errors/hangups on the socket are reported as "readable" so the next read
    // surfaces the real error.
    inline void poll_sock_wake(knst_socket_handle_t s, bool want_write, knst_socket_handle_t w,
                               uint32_t timeout_ms, bool& s_read, bool& s_write, bool& woke,
                               knst_network_error& err) noexcept {
        s_read = s_write = woke = false;
        err = knst_network_error::None;
        int tmo = timeout_ms > static_cast<uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(timeout_ms);
    #if KNST_USING_PLATFORM_WINDOWS
        WSAPOLLFD p[2]{};
        p[0].fd = s; p[0].events = POLLRDNORM | (want_write ? POLLWRNORM : 0);
        ULONG n = 1;
        if (w != KNST_INVALID_SOCKET) { p[1].fd = w; p[1].events = POLLRDNORM; n = 2; }
        int rc = ::WSAPoll(p, n, tmo);
        if (rc < 0) { int se = ::WSAGetLastError(); if (se != WSAEINTR) err = knst_net_detail::map_errno(se); return; }
        s_read  = (p[0].revents & (POLLRDNORM | POLLERR | POLLHUP | POLLNVAL)) != 0;
        s_write = (p[0].revents & POLLWRNORM) != 0;
        woke    = n == 2 && (p[1].revents & POLLRDNORM) != 0;
    #else
        struct pollfd p[2]{};
        p[0].fd = s; p[0].events = static_cast<short>(POLLIN | (want_write ? POLLOUT : 0));
        nfds_t n = 1;
        if (w != KNST_INVALID_SOCKET) { p[1].fd = w; p[1].events = POLLIN; n = 2; }
        int rc = ::poll(p, n, tmo);
        if (rc < 0) { if (errno != EINTR) err = knst_net_detail::map_errno(errno); return; }
        s_read  = (p[0].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0;
        s_write = (p[0].revents & POLLOUT) != 0;
        woke    = n == 2 && (p[1].revents & POLLIN) != 0;
    #endif
    }

    /* ------------------------------------------------------------- TLS ctx */

#if defined(KNST_NETWORK_ENABLE_TLS)
    inline bool host_to_cstr(const knst_c16string& host, char* buf, size_t cap) noexcept {
        if (host.empty() || host.length() >= cap) return false;
        for (uint32_t i = 0; i < host.length(); ++i) {
            char16_t c = host[i];
            if (c <= 0x20 || c >= 0x7F) return false;
            buf[i] = static_cast<char>(c);
        }
        buf[host.length()] = '\0';
        return true;
    }

    // IP literal => iPAddress SAN check, no SNI. Name => SNI + hostname check.
    inline bool tls_prepare_peer(SSL* ssl, const char* hbuf) noexcept {
        unsigned char tmp[sizeof(struct in6_addr)];
        bool is_ip = ::inet_pton(AF_INET, hbuf, tmp) == 1 || ::inet_pton(AF_INET6, hbuf, tmp) == 1;
        if (is_ip) {
            X509_VERIFY_PARAM* vp = SSL_get0_param(ssl);
            return vp && X509_VERIFY_PARAM_set1_ip_asc(vp, hbuf) == 1;
        }
        SSL_set_tlsext_host_name(ssl, hbuf);
        SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        return SSL_set1_host(ssl, hbuf) == 1;
    }

    // Builds a client SSL_CTX (CA/mTLS settings are read from knst_net_detail::tls_st()).
    inline SSL_CTX* build_ssl_ctx(const unsigned char* alpn, unsigned alpn_len,
                                  bool tls13_only, bool session_cache) noexcept {
        OPENSSL_init_ssl(0, nullptr);
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) return nullptr;
        SSL_CTX_set_min_proto_version(ctx, tls13_only ? TLS1_3_VERSION : TLS1_2_VERSION);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY | SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        {
            uint64_t opts = 0;
        #ifdef SSL_OP_NO_COMPRESSION
            opts |= SSL_OP_NO_COMPRESSION;
        #endif
        #ifdef SSL_OP_NO_RENEGOTIATION
            opts |= SSL_OP_NO_RENEGOTIATION;
        #endif
        #ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
            opts |= SSL_OP_IGNORE_UNEXPECTED_EOF;     // framed protocols tolerate a missing close_notify
        #endif
            if (opts) SSL_CTX_set_options(ctx, opts);
        }
        if (alpn && alpn_len) SSL_CTX_set_alpn_protos(ctx, alpn, alpn_len);
        if (session_cache) {
            SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
            SSL_CTX_sess_set_new_cb(ctx, knst_net_detail::tls_new_session_cb);
        }
                auto& st = knst_net_detail::tls_st();
        std::lock_guard<std::mutex> lk(st.m);
        const bool custom = st.ca_file[0] != '\0' || st.ca_path[0] != '\0';
        if (custom) {
            if (SSL_CTX_load_verify_locations(ctx, st.ca_file[0] ? st.ca_file : nullptr,
                                              st.ca_path[0] ? st.ca_path : nullptr) != 1) {
                SSL_CTX_free(ctx); ERR_clear_error(); return nullptr;
            }
        } else {
            SSL_CTX_set_default_verify_paths(ctx);
        #if defined(__ANDROID__)

            {
                X509_STORE* store = SSL_CTX_get_cert_store(ctx);
                const char* dirs[] = {
                    "/apex/com.android.conscrypt/cacerts",
                    "/system/etc/security/cacerts", 
                    nullptr
                };
                for (int di = 0; dirs[di]; ++di) {
                    DIR* d = ::opendir(dirs[di]);
                    if (!d) continue;
                    struct dirent* ent;
                    while ((ent = ::readdir(d)) != nullptr) {
                        if (ent->d_name[0] == '.') continue;
                        char path[512];
                        int n = ::snprintf(path, sizeof(path), "%s/%s", dirs[di], ent->d_name);
                        if (n <= 0 || (size_t)n >= sizeof(path)) continue;
                        X509* x = nullptr;
                        FILE* fp = ::fopen(path, "rb");
                        if (!fp) continue;
                        x = ::PEM_read_X509(fp, nullptr, nullptr, nullptr);
                        if (!x) {
                            ::rewind(fp);
                            x = ::d2i_X509_fp(fp, nullptr);
                        }
                        ::fclose(fp);
                        if (x) {
                            if (!X509_STORE_add_cert(store, x)) {
                                unsigned long e = ERR_peek_last_error();
                                if (!(ERR_GET_LIB(e) == ERR_LIB_X509 &&
                                      ERR_GET_REASON(e) == X509_R_CERT_ALREADY_IN_HASH_TABLE)) {
                                    
                                }
                            }
                            X509_free(x);
                        }
                    }
                    ::closedir(d);
                }
                ERR_clear_error();
            }
        #endif
        #if !KNST_USING_PLATFORM_WINDOWS
            // quictls's baked-in openssldir is empty on a fresh install.
            // Load the system CA bundle explicitly so TLS verification works.
            SSL_CTX_load_verify_locations(ctx,
                "/etc/ssl/certs/ca-certificates.crt", "/etc/ssl/certs");   // Debian/Ubuntu
            SSL_CTX_load_verify_locations(ctx,
                "/etc/pki/tls/certs/ca-bundle.crt", "/etc/pki/tls/certs"); // RHEL/Fedora
            SSL_CTX_load_verify_locations(ctx, "/etc/ssl/cert.pem", nullptr); // Arch/Alpine
        #endif
        #if KNST_NETWORK_USE_WIN_CERTSTORE
            HCERTSTORE store = ::CertOpenSystemStoreA(0, "ROOT");
            if (store) {
                X509_STORE* xs = SSL_CTX_get_cert_store(ctx);
                PCCERT_CONTEXT c = nullptr;
                while ((c = ::CertEnumCertificatesInStore(store, c)) != nullptr) {
                    const unsigned char* p = c->pbCertEncoded;
                    X509* x = d2i_X509(nullptr, &p, static_cast<long>(c->cbCertEncoded));
                    if (x) { X509_STORE_add_cert(xs, x); X509_free(x); }
                }
                ::CertCloseStore(store, 0);
            }
        #endif
        }
        if (st.client_cert[0] && st.client_key[0]) {
            if (SSL_CTX_use_certificate_chain_file(ctx, st.client_cert) != 1 ||
                SSL_CTX_use_PrivateKey_file(ctx, st.client_key, SSL_FILETYPE_PEM) != 1 ||
                SSL_CTX_check_private_key(ctx) != 1) {
                SSL_CTX_free(ctx); ERR_clear_error(); return nullptr;
            }
        }
        ERR_clear_error();
        return ctx;
    }
#endif // KNST_NETWORK_ENABLE_TLS

} // namespace knst_web_detail

typedef knst_web_detail::knst_cookie_jar_impl knst_cookie_jar;


/* ============================================================================
 * knst_web_stream — protocol-neutral response assembler + stream lifecycle
 * (used by both the h2 and the h3 session; all access under the session mutex)
 * ============================================================================ */

struct knst_web_stream {
    typedef knst_web_detail::time_pt time_pt;
    enum class block_t { Error, Interim, Continue100, Final, Trailer };

    // request side
    const uint8_t* body = nullptr;
    size_t         body_len = 0, body_off = 0;
        knst_vector<knst_web_header> trailers;
    bool           body_done = false;
    bool           expect = false, continue_ok = true, deferred = false;
    time_pt        expect_deadline{};

    // response side
    knst_http_response* out = nullptr;
    knst_http_client::body_callback_t on_body = nullptr;
    void*          on_body_user = nullptr;
    uint64_t       max_body = 0, received = 0;
    uint32_t       max_header_bytes = 65536, hdr_bytes = 0;
    bool           is_head = false, follow = true, allow_enc = true;
    bool           got_final = false, no_body = false, discard = false;
    bool           response_started = false, delivered = false;
    const char16_t* version = u"HTTP/2";
    knst_vector<knst_http_header> tmp;
    int            tmp_status = 0;
    std::unique_ptr<knst_net_detail::byte_sink> sink;

    // lifecycle
    bool           closed = false, cancel_req = false, rst_sent = false, abandoned = false, refused = false;
    knst_network_error err = knst_network_error::None;
    time_pt        last_activity{}, cancel_at{}, total_end{};
    bool           has_total = false;
    uint32_t       idle_ms = 0;

    bool setup(knst_http_response* o, const knst_web_request& r, const knst_web_options& opt,
               const knst_web_detail::deadline_t& total, uint32_t idle, const char16_t* ver) noexcept {
        out = o; version = ver;
        on_body = opt.on_body; on_body_user = opt.on_body_user;
        max_body = opt.max_response_bytes; max_header_bytes = opt.max_header_bytes;
        is_head = (r.method == "HEAD"); follow = opt.follow_redirects; allow_enc = opt.allow_content_encoding;
        body = r.body; body_len = r.body_len; trailers = r.trailers;
        has_total = !total.infinite; total_end = total.end; idle_ms = idle;
        last_activity = std::chrono::steady_clock::now();
        if (!on_body) {
            size_t lim = max_body > knst_net_detail::KNST_NET_MAX_LEN ? knst_net_detail::KNST_NET_MAX_LEN
                                                                      : static_cast<size_t>(max_body);
            sink.reset(new (std::nothrow) knst_net_detail::byte_sink(o->body, lim));
            if (!sink) return false;
        }
        return true;
    }

    void touch() noexcept { last_activity = std::chrono::steady_clock::now(); }

    time_pt expiry() const noexcept {
        time_pt e = time_pt::max();
        if (has_total) e = total_end;
        if (idle_ms) { time_pt i = last_activity + std::chrono::milliseconds(idle_ms); if (i < e) e = i; }
        return e;
    }

    void begin_block() noexcept { tmp.clear(); tmp_status = 0; hdr_bytes = 0; response_started = true; }

    knst_network_error add_header(const uint8_t* n, size_t nl, const uint8_t* v, size_t vl) noexcept {
        if (err != knst_network_error::None) return err;
        if (!out || abandoned) return knst_network_error::None;
        hdr_bytes += static_cast<uint32_t>(nl + vl + 32);
        if (hdr_bytes > max_header_bytes) { err = knst_network_error::HttpMalformedHeaders; return err; }
        if (nl > 0 && n[0] == ':') {
            if (nl == 7 && ::memcmp(n, ":status", 7) == 0) {
                int s = 0;
                if (vl != 3) { err = knst_network_error::HttpMalformedStatus; return err; }
                for (size_t i = 0; i < 3; ++i) {
                    if (v[i] < '0' || v[i] > '9') { err = knst_network_error::HttpMalformedStatus; return err; }
                    s = s * 10 + (v[i] - '0');
                }
                if (s < 100) { err = knst_network_error::HttpMalformedStatus; return err; }
                tmp_status = s;
            }
            return knst_network_error::None;
        }
        knst_http_header h;
        for (size_t i = 0; i < nl; ++i) h.name.append(static_cast<char16_t>(n[i]));
        for (size_t i = 0; i < vl; ++i) h.value.append(static_cast<char16_t>(v[i]));
        if (!tmp.push_back(h)) { err = knst_network_error::OutOfMemory; return err; }
        return knst_network_error::None;
    }

        block_t end_block(knst_network_error& e) noexcept {
        e = knst_network_error::None;
        if (err != knst_network_error::None) { e = err; return block_t::Error; }
        if (!out) { tmp.clear(); return block_t::Interim; }

        if (tmp_status == 0) {                                   // no :status => trailer block
            if (!got_final) { err = knst_network_error::HttpMalformedStatus; e = err; return block_t::Error; }
            for (uint32_t i = 0; i < tmp.size(); ++i)
                if (!out->headers.push_back(tmp[i])) { err = knst_network_error::OutOfMemory; e = err; return block_t::Error; }
            tmp.clear();
            return block_t::Trailer;
        }
        if (got_final) { err = knst_network_error::HttpMalformedStatus; e = err; return block_t::Error; }

        if (tmp_status < 200) {                                  // 1xx: never stored (RFC 9113 §8.1)
            tmp.clear();
            if (tmp_status == 101) { err = knst_network_error::HttpMalformedStatus; e = err; return block_t::Error; }
            return tmp_status == 100 ? block_t::Continue100 : block_t::Interim;
        }

        got_final = true;
        out->status  = static_cast<uint16_t>(tmp_status);
        out->version = knst_c16string(version);
        for (uint32_t i = 0; i < tmp.size(); ++i)
            if (!out->headers.push_back(tmp[i])) { err = knst_network_error::OutOfMemory; e = err; return block_t::Error; }
        tmp.clear();

        const bool will_redirect = out->is_redirect() && follow && !out->get_header(u"location").empty();
        no_body = is_head || tmp_status == 204 || tmp_status == 304;
        discard = will_redirect;

        if (!no_body && !discard) {
            knst_c16string ce = out->get_header(u"content-encoding");
            const bool enc = !ce.empty() && !knst_net_detail::ieq(knst_net_detail::trim16(ce), u"identity");
            if (enc && (!allow_enc || on_body)) {
                err = knst_network_error::HttpUnsupportedEncoding; e = err; return block_t::Error;
            }
            knst_c16string cl = out->get_header(u"content-length");
            if (!cl.empty()) {
                uint64_t want = 0; bool ok = true;
                for (uint32_t k = 0; k < cl.length(); ++k) {
                    if (cl[k] < u'0' || cl[k] > u'9') { ok = false; break; }
                    uint64_t d = static_cast<uint64_t>(cl[k] - u'0');
                    if (want > (UINT64_MAX - d) / 10) { want = UINT64_MAX; break; }
                    want = want * 10 + d;
                }
                if (ok) {
                    if (want > max_body) { err = knst_network_error::ResponseTooLarge; e = err; return block_t::Error; }
                    if (sink && want > 0) sink->room(static_cast<size_t>(want < (64ull << 20) ? want : (64ull << 20)));
                }
            }
        }
        return block_t::Final;
    }

    // true when the response is complete from the caller's point of view.
    bool complete() const noexcept { return resp_end || (discard && got_final); }
    bool finished() const noexcept { return closed || err != knst_network_error::None || complete(); }
    void fail(knst_network_error e) noexcept { if (err == knst_network_error::None) err = e; }

    // Body bytes. Returns false => stream must be reset (err is set).
    bool on_data(const uint8_t* d, size_t n, knst_network_error& e) noexcept {
        e = knst_network_error::None;
        if (!out) return true;
        if (!got_final) { err = knst_network_error::HttpMalformedBody; e = err; return false; }
        if (no_body || discard || n == 0) return true;           // HEAD/204/304/redirect bodies are swallowed
        if (received + n > max_body) { err = knst_network_error::ResponseTooLarge; e = err; return false; }
        if (on_body) {
            received += n; delivered = true;
            if (!on_body(d, n, on_body_user)) { err = knst_network_error::CallbackAborted; e = err; return false; }
            return true;
        }
        if (!sink) { err = knst_network_error::OutOfMemory; e = err; return false; }
        if (!sink->room(n)) { err = sink->error(); e = err; return false; }
        ::memcpy(sink->at(sink->len), d, n);
        sink->len += n; received += n;
        return true;
    }

    void on_close(knst_network_error mapped, bool was_refused) noexcept {
        closed = true;
        if (err != knst_network_error::None || complete()) return;
        refused = was_refused;
        err = (mapped != knst_network_error::None) ? mapped : knst_network_error::HttpConnectionClosed;
    }

    void finalize() noexcept { if (sink) sink->finish(); }

    // additional state (declared after use; members may appear anywhere in the class)
    bool    resp_end  = false;          // END_STREAM seen on the response
    int32_t stream_id = -1;
};


/* ============================================================================
 * knst_web_detail (part 2): helpers used by the sessions
 * ============================================================================ */

#if KNST_NET_HAS_EXCEPTIONS
    #define KNST_WEB_TRY try {
    #define KNST_WEB_CATCH(errvar, ret) } catch (...) { errvar = knst_network_error::OutOfMemory; return ret; }
#else
    #define KNST_WEB_TRY {
    #define KNST_WEB_CATCH(errvar, ret) }
#endif

namespace knst_web_detail {

    // Blocks SIGPIPE on this thread (POSIX, non-macOS) for the scope's lifetime.
    struct sigpipe_scope {
    #if defined(KNST_NETWORK_ENABLE_TLS)
        knst_net_detail::sigpipe_guard g;
    #endif
        sigpipe_scope() noexcept {}
    };

    inline deadline_t min_deadline(const deadline_t& a, const deadline_t& b) noexcept {
        if (a.infinite) return b;
        if (b.infinite) return a;
        return a.end <= b.end ? a : b;
    }
    inline time_pt now_tp() noexcept { return std::chrono::steady_clock::now(); }

    inline void clear_sock_err() noexcept {
    #if KNST_USING_PLATFORM_WINDOWS
        ::WSASetLastError(0);
    #else
        errno = 0;
    #endif
    }

    inline bool is_idempotent_method(const std::string& m) noexcept {
        return m == "GET" || m == "HEAD" || m == "PUT" || m == "DELETE" || m == "OPTIONS" || m == "TRACE";
    }
    inline bool is_conn_error(knst_network_error e) noexcept {
        return e == knst_network_error::ConnectionClosed || e == knst_network_error::HttpConnectionClosed ||
               e == knst_network_error::ConnectionReset  || e == knst_network_error::ConnectionAborted ||
               e == knst_network_error::BrokenPipe;
    }

#if defined(KNST_NETWORK_ENABLE_TLS)
    // Process-wide SSL_CTX for HTTP/2 (ALPN "h2", client session cache).
    inline SSL_CTX* tls_ctx_h2() noexcept {
        static std::once_flag once;
        static SSL_CTX* ctx = nullptr;
        std::call_once(once, []() noexcept {
            static const unsigned char alpn[] = { 2, 'h', '2' };
            ctx = build_ssl_ctx(alpn, sizeof(alpn), false, true);
        });
        return ctx;
    }
#endif

} // namespace knst_web_detail


/* ============================================================================
 * knst_h2_socket — TCP + TLS(ALPN h2), NON-BLOCKING I/O primitives
 * ============================================================================ */

class knst_h2_socket : public knst_tcp_socket {
public:
    enum class io_t { Ok, WantRead, WantWrite, Eof, Fail };

    using knst_tcp_socket::recv;
    knst_h2_socket() noexcept = default;
    ~knst_h2_socket() noexcept override { knst_h2_socket::close(); }
    knst_h2_socket(const knst_h2_socket&) = delete;
    knst_h2_socket& operator=(const knst_h2_socket&) = delete;

    bool start_tls_h2(const knst_c16string& host, uint16_t port,
                      const knst_web_detail::deadline_t& dl, knst_network_error& err) noexcept {
#if !defined(KNST_NETWORK_ENABLE_TLS)
        (void)host; (void)port; (void)dl;
        err = knst_network_error::TlsNotSupported;
        return false;
#else
        err = knst_network_error::None;
        if (!is_open()) { err = knst_network_error::NotOpen; return false; }
        if (m_ssl) { SSL_free(m_ssl); m_ssl = nullptr; }
        m_noshut = false;

        char hbuf[256];
        if (!knst_web_detail::host_to_cstr(host, hbuf, sizeof(hbuf))) {
            err = knst_network_error::InvalidArgument; m_noshut = true; close(); return false;
        }
        SSL_CTX* ctx = knst_web_detail::tls_ctx_h2();
        if (!ctx) { err = knst_network_error::TlsHandshakeFailed; m_noshut = true; close(); return false; }

        knst_net_detail::set_blocking(native_handle(), false);
        m_ssl = SSL_new(ctx);
        if (!m_ssl) { err = knst_network_error::TlsHandshakeFailed; ERR_clear_error(); m_noshut = true; close(); return false; }
        if (!knst_web_detail::tls_prepare_peer(m_ssl, hbuf) ||
            SSL_set_fd(m_ssl, static_cast<int>(native_handle())) != 1) {
            err = knst_network_error::TlsHandshakeFailed; ERR_clear_error(); m_noshut = true; close(); return false;
        }

        {   // session resumption (shared cache, own key namespace "h2|")
            char* k = m_sess_key; size_t n = 0;
            k[n++] = 'h'; k[n++] = '2'; k[n++] = '|';
            for (const char* h = hbuf; *h && n + 8 < sizeof(m_sess_key); ++h)
                k[n++] = static_cast<char>((*h >= 'A' && *h <= 'Z') ? (*h - 'A' + 'a') : *h);
            ::snprintf(k + n, sizeof(m_sess_key) - n, ":%u", static_cast<unsigned>(port));
            SSL_set_ex_data(m_ssl, knst_net_detail::tls_sess_idx(), m_sess_key);
            if (SSL_SESSION* sess = knst_net_detail::tls_sess().get(m_sess_key)) {
                SSL_set_session(m_ssl, sess);
                SSL_SESSION_free(sess);
            }
        }

        {
            knst_web_detail::sigpipe_scope sp;
            for (;;) {
                knst_web_detail::clear_sock_err();
                ERR_clear_error();
                int rc = SSL_connect(m_ssl);
                if (rc == 1) break;
                int se  = SSL_get_error(m_ssl, rc);
                int sys = knst_net_detail::last_socket_error();
                if (se == SSL_ERROR_WANT_READ || se == SSL_ERROR_WANT_WRITE) {
                    knst_network_error perr = knst_network_error::None;
                    if (knst_net_detail::poll_wait(native_handle(), se == SSL_ERROR_WANT_READ, dl, perr)) continue;
                    err = perr;
                } else if (SSL_get_verify_result(m_ssl) != X509_V_OK) {
                    err = knst_network_error::TlsCertificateInvalid;
                } else if (se == SSL_ERROR_SYSCALL) {
                    err = knst_net_detail::map_errno(sys);
                    if (err == knst_network_error::None) err = knst_network_error::ConnectionClosed;
                    if (err == knst_network_error::ConnectionTimedOut || err == knst_network_error::WouldBlock)
                        err = knst_network_error::Timeout;
                } else {
                    err = knst_network_error::TlsHandshakeFailed;
                }
                m_last_error = err;
                ERR_clear_error();
                m_noshut = true; close();
                return false;
            }
        }
        const unsigned char* proto = nullptr;
        unsigned int proto_len = 0;
        SSL_get0_alpn_selected(m_ssl, &proto, &proto_len);
        if (proto_len != 2 || proto[0] != 'h' || proto[1] != '2') {
            err = knst_network_error::NotSupported;           // server did not pick h2
            m_noshut = false; close(); return false;
        }
        m_last_error = knst_network_error::None;              // socket stays NON-BLOCKING
        return true;
#endif
    }

    bool has_pending_input() const noexcept override {
#if defined(KNST_NETWORK_ENABLE_TLS)
        return m_ssl && SSL_pending(m_ssl) > 0;
#else
        return false;
#endif
    }

    // Plain send/recv are only valid before TLS starts (proxy handshakes).
    bool send(const void* d, size_t l, size_t& s, uint32_t t, knst_network_error& e) noexcept override {
#if defined(KNST_NETWORK_ENABLE_TLS)
        if (m_ssl) { s = 0; e = knst_network_error::NotSupported; return false; }
#endif
        return knst_tcp_socket::send(d, l, s, t, e);
    }
    bool recv(void* b, size_t c, size_t& g, uint32_t t, knst_network_error& e) noexcept override {
#if defined(KNST_NETWORK_ENABLE_TLS)
        if (m_ssl) { g = 0; e = knst_network_error::NotSupported; return false; }
#endif
        return knst_tcp_socket::recv(b, c, g, t, e);
    }

    io_t read_some(void* buf, size_t cap, size_t& got, knst_network_error& err) noexcept {
        got = 0; err = knst_network_error::None;
#if !defined(KNST_NETWORK_ENABLE_TLS)
        (void)buf; (void)cap; err = knst_network_error::TlsNotSupported; return io_t::Fail;
#else
        if (!m_ssl || !buf || cap == 0) { err = knst_network_error::NotOpen; return io_t::Fail; }
        const int c = static_cast<int>(cap > static_cast<size_t>(INT_MAX) ? INT_MAX : cap);
        for (;;) {
            knst_web_detail::clear_sock_err();                 // SYSCALL + errno==0 => unexpected EOF
            ERR_clear_error();
            int n = SSL_read(m_ssl, buf, c);
            if (n > 0) { got = static_cast<size_t>(n); return io_t::Ok; }
            int se = SSL_get_error(m_ssl, n);
            int sys = knst_net_detail::last_socket_error();
            ERR_clear_error();
            switch (se) {
                case SSL_ERROR_ZERO_RETURN: return io_t::Eof;
                case SSL_ERROR_WANT_READ:   return io_t::WantRead;
                case SSL_ERROR_WANT_WRITE:  return io_t::WantWrite;
                case SSL_ERROR_SYSCALL: {
                    knst_network_error e = knst_net_detail::map_errno(sys);
                    if (e == knst_network_error::Interrupted) continue;
                    if (e == knst_network_error::None) return io_t::Eof;
                    if (e == knst_network_error::WouldBlock) return io_t::WantRead;
                    err = e; m_noshut = true; return io_t::Fail;
                }
                default:
                    err = knst_network_error::ProtocolError; m_noshut = true; return io_t::Fail;
            }
        }
#endif
    }

    io_t write_some(const void* data, size_t len, size_t& sent, knst_network_error& err) noexcept {
        sent = 0; err = knst_network_error::None;
#if !defined(KNST_NETWORK_ENABLE_TLS)
        (void)data; (void)len; err = knst_network_error::TlsNotSupported; return io_t::Fail;
#else
        if (!m_ssl || (!data && len)) { err = knst_network_error::NotOpen; return io_t::Fail; }
        if (len == 0) return io_t::Ok;
        const size_t MAX_CHUNK = size_t(256) * 1024;
        const int c = static_cast<int>(len > MAX_CHUNK ? MAX_CHUNK : len);
        for (;;) {
            knst_web_detail::clear_sock_err();
            ERR_clear_error();
            int n = SSL_write(m_ssl, data, c);
            if (n > 0) { sent = static_cast<size_t>(n); return io_t::Ok; }
            int se = SSL_get_error(m_ssl, n);
            int sys = knst_net_detail::last_socket_error();
            ERR_clear_error();
            switch (se) {
                case SSL_ERROR_WANT_WRITE: return io_t::WantWrite;
                case SSL_ERROR_WANT_READ:  return io_t::WantRead;
                case SSL_ERROR_SYSCALL: {
                    knst_network_error e = knst_net_detail::map_errno(sys);
                    if (e == knst_network_error::Interrupted) continue;
                    if (e == knst_network_error::WouldBlock) return io_t::WantWrite;
                    if (e == knst_network_error::None) e = knst_network_error::ConnectionClosed;
                    err = e; m_noshut = true; return io_t::Fail;
                }
                default:
                    err = knst_network_error::ConnectionClosed; m_noshut = true; return io_t::Fail;
            }
        }
#endif
    }

    void close() noexcept override {
#if defined(KNST_NETWORK_ENABLE_TLS)
        if (m_ssl) {
            if (native_handle() != KNST_INVALID_SOCKET && !m_noshut) {
                knst_net_detail::set_blocking(native_handle(), false);   // never hang on close_notify
                knst_web_detail::sigpipe_scope sp;
                ERR_clear_error();
                SSL_shutdown(m_ssl);
                ERR_clear_error();
            }
            SSL_free(m_ssl);
            m_ssl = nullptr;
        }
        m_noshut = false;
#endif
        knst_tcp_socket::close();
    }

private:
#if defined(KNST_NETWORK_ENABLE_TLS)
    SSL* m_ssl = nullptr;
    char m_sess_key[320] = {};
#endif
    bool m_noshut = false;      // skip SSL_shutdown on broken connections
};


/* ============================================================================
 * knst_h2_session — one TCP+TLS connection, many multiplexed streams
 *
 *  Locking model
 *    m_mtx guards nghttp2, m_streams, m_srcs and all stream state. nghttp2
 *    callbacks run inside nghttp2_session_mem_send/mem_recv which are only
 *    called with m_mtx held, so callbacks never lock.
 *    ONE thread at a time is the "driver" (m_driving): it releases m_mtx only
 *    around socket syscalls and poll(). m_wbuf/m_rbuf/socket I/O belong to the
 *    driver exclusively.
 * ============================================================================ */

class knst_h2_session {
public:
    typedef knst_web_detail::time_pt     time_pt;
    typedef knst_web_detail::deadline_t  deadline_t;
    typedef knst_h2_socket::io_t         io_t;

    knst_h2_session(const knst_h2_session&) = delete;
    knst_h2_session& operator=(const knst_h2_session&) = delete;
    ~knst_h2_session() noexcept { shutdown_(); }

    static std::shared_ptr<knst_h2_session> create(
        const knst_url& u, const knst_web_detail::proxy_cfg& pc,
        const knst_web_options& o, const deadline_t& total, knst_network_error& err) noexcept
    {
        err = knst_network_error::None;
        std::shared_ptr<knst_h2_session> self(new (std::nothrow) knst_h2_session());
        if (!self) { err = knst_network_error::OutOfMemory; return nullptr; }
        self->m_sock.reset(new (std::nothrow) knst_h2_socket());
        if (!self->m_sock) { err = knst_network_error::OutOfMemory; return nullptr; }
        KNST_WEB_TRY
            self->m_rbuf.resize(16384);
        KNST_WEB_CATCH(err, nullptr)

        deadline_t cd = deadline_t::after(o.timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : o.timeout_ms);
        cd = knst_web_detail::min_deadline(cd, total);       // DNS + connect + proxy + TLS under ONE deadline

        if (!o.bind_local_ip.empty()) {
            knst_network_error e2 = knst_network_error::None;
            knst_ip_address bip = knst_ip_address::from_string(o.bind_local_ip, e2);
            if (e2 != knst_network_error::None || !bip.is_valid()) { err = knst_network_error::InvalidArgument; return nullptr; }
            if (!self->m_sock->bind_local(knst_endpoint(bip, 0), err)) return nullptr;
        }
        if (!o.allow_private_networks && pc.enabled() &&
            !knst_web_detail::literal_target_allowed(u.host, err)) return nullptr;

        const knst_c16string& chost = pc.enabled() ? pc.host : u.host;
        const uint16_t cport = pc.enabled() ? pc.port : u.effective_port();
        const uint32_t dns_to = o.dns_timeout_ms ? o.dns_timeout_ms : o.timeout_ms;
        knst_vector<knst_endpoint> eps;
        if (!knst_web_detail::resolve_endpoints(chost, cport, o, dns_to, cd,
                                                !o.allow_private_networks && !pc.enabled(), eps, err))
            return nullptr;

        uint32_t ms = 0;
        if (!knst_web_detail::io_budget(cd, ms, err)) return nullptr;
        if (!self->m_sock->connect_any(eps, ms, err)) return nullptr;

        if (pc.enabled()) {
            const bool ok = (pc.kind == knst_web_detail::proxy_cfg::kind_t::Http)
                ? knst_web_detail::proxy_connect_tunnel(*self->m_sock, u, pc, o, cd, err)
                : knst_web_detail::socks5_handshake(*self->m_sock, u.host, u.effective_port(), pc, cd, err);
            if (!ok) return nullptr;
        }
        if (!self->m_sock->start_tls_h2(u.host, u.effective_port(), cd, err)) return nullptr;
        if (!self->init_ng(o, err)) return nullptr;
        self->m_wake.init();            // failure tolerated: poll is then capped at 50 ms
        self->touch_idle();
        return self;
    }

    /* ---- pool interface ---- */
    bool is_usable() const noexcept {
        return !m_fatal.load(std::memory_order_acquire) && !m_goaway.load(std::memory_order_acquire) &&
               !m_retired.load(std::memory_order_acquire) && m_sock && m_sock->is_open();
    }
    bool try_reserve(uint32_t local_cap) noexcept {
        if (!is_usable()) return false;
        uint32_t lim = std::min<uint32_t>(local_cap, m_peer_max.load(std::memory_order_relaxed));
        if (lim == 0) lim = 1;
        uint32_t cur = m_reserved.load(std::memory_order_relaxed);
        while (cur < lim)
            if (m_reserved.compare_exchange_weak(cur, cur + 1, std::memory_order_acq_rel)) return true;
        return false;
    }
    void force_reserve() noexcept { m_reserved.fetch_add(1, std::memory_order_acq_rel); }
    void release() noexcept { m_reserved.fetch_sub(1, std::memory_order_acq_rel); touch_idle(); }
    uint32_t load() const noexcept { return m_reserved.load(std::memory_order_relaxed); }
    time_pt idle_since() const noexcept {
        return time_pt(std::chrono::steady_clock::duration(m_idle_ns.load(std::memory_order_relaxed)));
    }
    uint64_t requests_started() const noexcept { return m_requests.load(std::memory_order_relaxed); }
    void retire() noexcept { m_retired.store(true, std::memory_order_release); }

    /* Submit one request on this connection and block until it completes.
       The pool-reserved slot is released before returning. `st` is owned by
       the caller (so the caller can inspect refused/response_started). */
    bool execute(const knst_web_request& r, const knst_web_options& o, const deadline_t& total,
                 knst_http_response& out, knst_web_stream& st, knst_network_error& err) noexcept
    {
        struct slot_guard { knst_h2_session* s; ~slot_guard() noexcept { s->release(); } } sg{ this };
        KNST_WEB_GUARD_BEGIN
        err = knst_network_error::None;
        if (!m_ng) { err = knst_network_error::NotOpen; return false; }

        // ---- header list (nghttp2 copies names/values at submit time) ----
        knst_byte_string scheme_s("https");
        std::vector<nghttp2_nv> nva;
        nva.reserve(8 + r.headers.size());
        auto push = [&](const char* name_cstr, const knst_byte_string& value) {
            nghttp2_nv nv;
            nv.name     = reinterpret_cast<uint8_t*>(const_cast<char*>(name_cstr));
            nv.value    = const_cast<uint8_t*>(value.data());
            nv.namelen  = std::strlen(name_cstr);
            nv.valuelen = value.length();
            nv.flags    = NGHTTP2_NV_FLAG_NONE;
            nva.push_back(nv);
        };
        push(":method", r.method);
        push(":scheme", scheme_s);
        push(":authority", r.authority);
        push(":path", r.path);
        for (uint32_t i = 0; i < r.headers.size(); ++i) {
            nghttp2_nv nv;
            nv.name     = const_cast<uint8_t*>(r.headers[i].name.data());
            nv.value    = const_cast<uint8_t*>(r.headers[i].value.data());
            nv.namelen  = r.headers[i].name.length();
            nv.valuelen = r.headers[i].value.length();
            nv.flags    = NGHTTP2_NV_FLAG_NONE;
            nva.push_back(nv);
        }

        if (!st.setup(&out, r, o, total, o.timeout_ms, u"HTTP/2")) { err = knst_network_error::OutOfMemory; return false; }
        st.expect = o.expect_continue && r.body_len > 0;
        st.continue_ok = !st.expect;
        st.expect_deadline = knst_web_detail::now_tp() + std::chrono::milliseconds(o.expect_continue_timeout_ms);
        const bool use_provider = r.body_len > 0 || !r.trailers.empty();

        std::unique_lock<std::mutex> lk(m_mtx);

        // Idle connection: observe a pending GOAWAY / EOF BEFORE sending anything.
        if (!m_driving && m_reserved.load() <= 1) probe_idle(lk);
        if (!is_usable()) { err = knst_network_error::ConnectionClosed; st.refused = true; return false; }
        if (nghttp2_session_check_request_allowed(m_ng.get()) == 0) {
            m_retired.store(true); err = knst_network_error::ConnectionClosed; st.refused = true; return false;
        }

        std::unique_ptr<src_t> src;
        nghttp2_data_provider dp{};
        const nghttp2_data_provider* dpp = nullptr;
        if (use_provider) {
            src.reset(new (std::nothrow) src_t());
            if (!src) { err = knst_network_error::OutOfMemory; return false; }
            src->st = &st;
            dp.source.ptr = src.get();
            dp.read_callback = cb_body;
            dpp = &dp;
        }
        int32_t sid = nghttp2_submit_request(m_ng.get(), nullptr, nva.data(), nva.size(), dpp, nullptr);
        if (sid < 0) {
            if (sid == NGHTTP2_ERR_STREAM_ID_NOT_AVAILABLE) { m_retired.store(true); st.refused = true; err = knst_network_error::ConnectionClosed; }
            else err = knst_network_error::ProtocolError;
            return false;
        }
        st.stream_id = sid;
        m_streams[sid] = &st;
        if (src) m_srcs[sid] = std::move(src);
        m_requests.fetch_add(1, std::memory_order_relaxed);
        if (m_driving) m_wake.signal();

        run(st, lk);

        // ---- completion / cleanup (m_mtx held) ----
        bool ok = st.err == knst_network_error::None && st.got_final && (st.resp_end || st.discard);
        if (!ok && st.err == knst_network_error::None) st.err = knst_network_error::HttpConnectionClosed;
        if (ok) st.finalize();
        bool need_flush = false;
        if (!st.closed && !st.rst_sent) {
            nghttp2_submit_rst_stream(m_ng.get(), NGHTTP2_FLAG_NONE, sid,
                                      st.resp_end ? NGHTTP2_NO_ERROR : NGHTTP2_CANCEL);
            st.rst_sent = true; need_flush = true;
        }
        auto it = m_srcs.find(sid);
        if (it != m_srcs.end()) it->second->st = nullptr;       // provider must never touch `st` again
        m_streams.erase(sid);
        err = st.err;
        if (need_flush) {
            if (m_driving) m_wake.signal();
            else flush_locked(lk, o.cancel_grace_ms);
        }
        return ok;
        KNST_WEB_GUARD_END(err)
    }

private:
    knst_h2_session() noexcept = default;

    struct ng_del { void operator()(nghttp2_session* p) const noexcept { if (p) nghttp2_session_del(p); } };
    struct src_t { knst_web_stream* st = nullptr; };          // per-stream upload source (Bug 1 fix)

    /* ------------------------------------------------------------ init */
    bool init_ng(const knst_web_options& o, knst_network_error& err) noexcept {
        nghttp2_session_callbacks* cbs = nullptr;
        if (nghttp2_session_callbacks_new(&cbs) != 0) { err = knst_network_error::OutOfMemory; return false; }
        nghttp2_session_callbacks_set_on_begin_headers_callback(cbs, cb_begin_headers);
        nghttp2_session_callbacks_set_on_header_callback(cbs, cb_header);
        nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, cb_frame);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, cb_data);
        nghttp2_session_callbacks_set_on_stream_close_callback(cbs, cb_close);
        nghttp2_session* ng = nullptr;
        int rv = nghttp2_session_client_new(&ng, cbs, this);
        nghttp2_session_callbacks_del(cbs);
        if (rv != 0 || !ng) { err = knst_network_error::OutOfMemory; return false; }
        m_ng.reset(ng);

        const uint32_t iw = std::min<uint32_t>(o.initial_stream_window ? o.initial_stream_window : 65535u, 0x7FFFFFFFu);
        nghttp2_settings_entry iv[3];
        iv[0].settings_id = NGHTTP2_SETTINGS_ENABLE_PUSH;          iv[0].value = 0;
        iv[1].settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;  iv[1].value = iw;
        iv[2].settings_id = NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE; iv[2].value = o.max_header_bytes;
        if (nghttp2_submit_settings(ng, NGHTTP2_FLAG_NONE, iv, 3) != 0) { err = knst_network_error::ProtocolError; return false; }
        if (o.connection_window > 65535u) {
            const int32_t cw = static_cast<int32_t>(std::min<uint32_t>(o.connection_window, 0x7FFFFFFFu));
            nghttp2_session_set_local_window_size(ng, NGHTTP2_FLAG_NONE, 0, cw);
        }
        return true;
    }

    void touch_idle() noexcept {
        m_idle_ns.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    }

    void shutdown_() noexcept {
        if (m_ng && m_sock && m_sock->is_open() && !m_fatal.load()) {
            nghttp2_session_terminate_session(m_ng.get(), NGHTTP2_NO_ERROR);
            knst_web_detail::sigpipe_scope sp;
            const uint8_t* p = nullptr;
            ssize_t n = nghttp2_session_mem_send(m_ng.get(), &p);
            if (n > 0) { size_t sent = 0; knst_network_error e; m_sock->write_some(p, static_cast<size_t>(n), sent, e); }
        }
        m_ng.reset();
        if (m_sock) m_sock->close();
    }

    /* ------------------------------------------------ nghttp2 callbacks */
    knst_web_stream* find(int32_t sid) noexcept {
        auto it = m_streams.find(sid);
        return it == m_streams.end() ? nullptr : it->second;
    }

    static int cb_begin_headers(nghttp2_session*, const nghttp2_frame* f, void* ud) noexcept {
        auto* self = static_cast<knst_h2_session*>(ud);
        if (f->hd.type != NGHTTP2_HEADERS) return 0;
        if (knst_web_stream* st = self->find(f->hd.stream_id)) { st->touch(); st->begin_block(); }
        return 0;
    }

    static int cb_header(nghttp2_session*, const nghttp2_frame* f, const uint8_t* name, size_t nl,
                         const uint8_t* val, size_t vl, uint8_t, void* ud) noexcept {
        auto* self = static_cast<knst_h2_session*>(ud);
        if (f->hd.type != NGHTTP2_HEADERS) return 0;           // push promises are refused anyway
        knst_web_stream* st = self->find(f->hd.stream_id);
        if (!st) return 0;
        st->touch();
        if (st->add_header(name, nl, val, vl) != knst_network_error::None)
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;      // RST_STREAM this stream only
        return 0;
    }

    static int cb_frame(nghttp2_session* s, const nghttp2_frame* f, void* ud) noexcept {
        auto* self = static_cast<knst_h2_session*>(ud);
        switch (f->hd.type) {
        case NGHTTP2_HEADERS: {
            if (f->headers.cat != NGHTTP2_HCAT_RESPONSE && f->headers.cat != NGHTTP2_HCAT_HEADERS) return 0;
            knst_web_stream* st = self->find(f->hd.stream_id);
            if (!st) return 0;
            st->touch();
            knst_network_error e = knst_network_error::None;
            switch (st->end_block(e)) {
                case knst_web_stream::block_t::Error:
                    if (!st->rst_sent) {
                        nghttp2_submit_rst_stream(s, NGHTTP2_FLAG_NONE, f->hd.stream_id, NGHTTP2_PROTOCOL_ERROR);
                        st->rst_sent = true;
                    }
                    break;
                case knst_web_stream::block_t::Continue100:       // release a body held back by Expect
                    st->continue_ok = true;
                    if (st->deferred) { st->deferred = false; nghttp2_session_resume_data(s, f->hd.stream_id); }
                    break;
                case knst_web_stream::block_t::Final:
                    if (st->discard && !(f->hd.flags & NGHTTP2_FLAG_END_STREAM) && !st->rst_sent) {
                        nghttp2_submit_rst_stream(s, NGHTTP2_FLAG_NONE, f->hd.stream_id, NGHTTP2_CANCEL);
                        st->rst_sent = true;                      // redirect body is not worth downloading
                    }
                    break;
                default: break;                                   // Interim (103 Early Hints etc.), Trailer
            }
            if (f->hd.flags & NGHTTP2_FLAG_END_STREAM) st->resp_end = true;
            break;
        }
        case NGHTTP2_DATA: {
            if (f->hd.flags & NGHTTP2_FLAG_END_STREAM)
                if (knst_web_stream* st = self->find(f->hd.stream_id)) st->resp_end = true;
            break;
        }
        case NGHTTP2_GOAWAY:
            // nghttp2 itself closes every stream above last_stream_id with REFUSED_STREAM
            // (-> cb_close -> st.refused => safe automatic retry on a fresh session).
            self->m_goaway.store(true, std::memory_order_release);
            break;
        case NGHTTP2_SETTINGS:
            if (!(f->hd.flags & NGHTTP2_FLAG_ACK)) {
                uint32_t v = nghttp2_session_get_remote_settings(s, NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
                self->m_peer_max.store(v, std::memory_order_relaxed);
            }
            break;
        case NGHTTP2_PUSH_PROMISE:
            nghttp2_submit_rst_stream(s, NGHTTP2_FLAG_NONE, f->push_promise.promised_stream_id, NGHTTP2_REFUSED_STREAM);
            break;
        default: break;
        }
        return 0;
    }

    static int cb_data(nghttp2_session*, uint8_t, int32_t sid, const uint8_t* d, size_t n, void* ud) noexcept {
        auto* self = static_cast<knst_h2_session*>(ud);
        knst_web_stream* st = self->find(sid);
        if (!st) return 0;                                         // abandoned stream: data is consumed and dropped
        st->touch();
        knst_network_error e = knst_network_error::None;
        if (!st->on_data(d, n, e)) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        return 0;
    }

    static int cb_close(nghttp2_session*, int32_t sid, uint32_t code, void* ud) noexcept {
        auto* self = static_cast<knst_h2_session*>(ud);
        knst_network_error m = knst_network_error::None;
        bool refused = false;
        switch (code) {
            case NGHTTP2_NO_ERROR: break;
            case NGHTTP2_REFUSED_STREAM:     m = knst_network_error::ConnectionAborted; refused = true; break;
            case NGHTTP2_HTTP_1_1_REQUIRED:  m = knst_network_error::NotSupported; break;
            case NGHTTP2_CANCEL:             m = knst_network_error::ConnectionReset; break;
            default:                         m = knst_network_error::ProtocolError; break;
        }
        if (knst_web_stream* st = self->find(sid)) st->on_close(m, refused);
        self->m_srcs.erase(sid);
        return 0;
    }

    // Request-body provider. Runs inside nghttp2_session_mem_send (m_mtx held).
    static ssize_t cb_body(nghttp2_session* s, int32_t sid, uint8_t* buf, size_t length,
                           uint32_t* flags, nghttp2_data_source* source, void*) noexcept {
        auto* src = static_cast<src_t*>(source->ptr);
        knst_web_stream* st = src->st;
        if (!st) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;    // owner went away
        if (st->expect && !st->continue_ok) {
            if (knst_web_detail::now_tp() >= st->expect_deadline) st->continue_ok = true;
            else { st->deferred = true; return NGHTTP2_ERR_DEFERRED; }
        }
        const size_t rem  = st->body_len - st->body_off;
        const size_t take = length < rem ? length : rem;
        if (take) { ::memcpy(buf, st->body + st->body_off, take); st->body_off += take; st->touch(); }
        if (st->body_off >= st->body_len) {
            st->body_done = true;
            *flags |= NGHTTP2_DATA_FLAG_EOF;
                        if (!st->trailers.empty()) {
                *flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
                std::vector<nghttp2_nv> tv;
                tv.reserve(st->trailers.size());
                for (uint32_t i = 0; i < st->trailers.size(); ++i) {
                    nghttp2_nv nv;
                    nv.name     = const_cast<uint8_t*>(st->trailers[i].name.data());
                    nv.value    = const_cast<uint8_t*>(st->trailers[i].value.data());
                    nv.namelen  = st->trailers[i].name.length();
                    nv.valuelen = st->trailers[i].value.length();
                    nv.flags    = NGHTTP2_NV_FLAG_NONE;
                    tv.push_back(nv);
                }
                if (nghttp2_submit_trailer(s, sid, tv.data(), tv.size()) != 0)
                    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
            }
        }
        return static_cast<ssize_t>(take);
    }

    /* ------------------------------------------------ fatal / timers */
    void set_fatal_locked(knst_network_error e) noexcept {
        if (m_fatal.load()) return;
        m_fatal_err = e;
        m_fatal.store(true, std::memory_order_release);
        for (auto& kv : m_streams) if (!kv.second->finished()) kv.second->fail(e);
        m_cv.notify_all();
    }

    void cancel_locked(int32_t sid, knst_web_stream& st, uint32_t code) noexcept {
        if (st.closed || st.rst_sent) return;
        nghttp2_submit_rst_stream(m_ng.get(), NGHTTP2_FLAG_NONE, sid, code);
        st.rst_sent = true;
    }

    // Per-stream deadlines (idle + total), Expect-continue timers. Returns next wake time.
    time_pt scan_timers(time_pt now) noexcept {
        time_pt next = now + std::chrono::seconds(30);
        for (auto& kv : m_streams) {
            knst_web_stream* st = kv.second;
            if (st->finished()) continue;
            if (st->expect && !st->continue_ok) {
                if (now >= st->expect_deadline) {
                    st->continue_ok = true;
                    if (st->deferred) { st->deferred = false; nghttp2_session_resume_data(m_ng.get(), kv.first); }
                } else if (st->expect_deadline < next) next = st->expect_deadline;
            }
            const time_pt ex = st->expiry();
            if (now >= ex) { st->fail(knst_network_error::Timeout); cancel_locked(kv.first, *st, NGHTTP2_CANCEL); }
            else if (ex < next) next = ex;
        }
        return next;
    }

    /* ------------------------------------------------ driver election */
    void run(knst_web_stream& st, std::unique_lock<std::mutex>& lk) noexcept {
        for (;;) {
            if (st.finished()) return;
            if (m_fatal.load()) { st.fail(m_fatal_err); return; }
            if (!m_driving) {
                m_driving = true;
                drive(lk, st);
                m_driving = false;
                m_cv.notify_all();                                  // hand the socket to a waiter
                continue;
            }
            const time_pt now = knst_web_detail::now_tp();
            const time_pt ex  = st.expiry();
            if (now >= ex) { st.fail(knst_network_error::Timeout); return; }
            time_pt wk = now + std::chrono::seconds(1);
            if (ex < wk) wk = ex;
            m_cv.wait_until(lk, wk);
        }
    }

    // Appends nghttp2's pending output to m_wbuf (bounded: write backpressure).
        bool pull_output() noexcept {
        [[maybe_unused]] knst_network_error dummy = knst_network_error::None;
        KNST_WEB_TRY
            while (m_wbuf.size() - m_woff < (256u * 1024u)) {
                const uint8_t* p = nullptr;
                ssize_t n = nghttp2_session_mem_send(m_ng.get(), &p);
                if (n < 0) return false;
                if (n == 0) break;
                if (m_woff == m_wbuf.size()) { m_wbuf.clear(); m_woff = 0; }
                m_wbuf.insert(m_wbuf.end(), p, p + n);
            }
            return true;
        KNST_WEB_CATCH(dummy, false)
        return false;
    }

    // Lock held on entry and exit; released around the syscall.
    void write_burst(std::unique_lock<std::mutex>& lk, bool& progress, bool& wants_read) noexcept {
        while (m_woff < m_wbuf.size()) {
            const uint8_t* p = m_wbuf.data() + m_woff;
            const size_t   n = m_wbuf.size() - m_woff;
            size_t sent = 0; knst_network_error e = knst_network_error::None;
            lk.unlock();
            io_t r = m_sock->write_some(p, n, sent, e);
            lk.lock();
            if (r == io_t::Ok) { m_woff += sent; progress = true; continue; }
            if (r == io_t::WantWrite) break;
            if (r == io_t::WantRead) { wants_read = true; break; }
            set_fatal_locked(e != knst_network_error::None ? e : knst_network_error::ConnectionClosed);
            return;
        }
        if (m_woff >= m_wbuf.size()) { m_wbuf.clear(); m_woff = 0; }
    }

    void read_burst(std::unique_lock<std::mutex>& lk, int max_iter) noexcept {
        for (int i = 0; i < max_iter; ++i) {
            size_t got = 0; knst_network_error e = knst_network_error::None;
            lk.unlock();
            io_t r = m_sock->read_some(m_rbuf.data(), m_rbuf.size(), got, e);
            lk.lock();
            if (r == io_t::Ok) {
                ssize_t used = nghttp2_session_mem_recv(m_ng.get(), m_rbuf.data(), got);
                if (used < 0 || static_cast<size_t>(used) != got) { set_fatal_locked(knst_network_error::ProtocolError); return; }
                if (m_fatal.load()) return;
                continue;
            }
            if (r == io_t::WantRead || r == io_t::WantWrite) return;
            set_fatal_locked(r == io_t::Eof ? knst_network_error::ConnectionClosed
                                            : (e != knst_network_error::None ? e : knst_network_error::ConnectionClosed));
            return;
        }
    }

    // Non-blocking look at an idle connection (GOAWAY / EOF / alerts) before reuse.
    void probe_idle(std::unique_lock<std::mutex>& lk) noexcept {
        m_driving = true;
        read_burst(lk, 8);
        m_driving = false;
        m_cv.notify_all();
    }

    // Best-effort flush of queued frames (RST_STREAM / GOAWAY) when nobody drives.
    void flush_locked(std::unique_lock<std::mutex>& lk, uint32_t grace_ms) noexcept {
        if (m_driving || m_fatal.load()) return;
        m_driving = true;
        knst_web_detail::sigpipe_scope sp;
        const deadline_t dl = deadline_t::after(grace_ms ? grace_ms : 1);
        while (!m_fatal.load()) {
            if (!pull_output()) break;
            if (m_woff >= m_wbuf.size()) break;
            bool prog = false, wrd = false;
            write_burst(lk, prog, wrd);
            if (m_fatal.load() || m_woff >= m_wbuf.size()) continue;
            bool expired = false; dl.remaining_ms(expired);
            if (expired) break;
            knst_network_error pe = knst_network_error::None;
            lk.unlock();
            knst_net_detail::poll_wait(m_sock->native_handle(), wrd, dl, pe);
            lk.lock();
            if (pe != knst_network_error::None) break;
        }
        m_driving = false;
        m_cv.notify_all();
    }

    // The event loop (lock held on entry/exit). Returns when `mine` finished or the session died.
    void drive(std::unique_lock<std::mutex>& lk, knst_web_stream& mine) noexcept {
        knst_web_detail::sigpipe_scope sp;
        for (;;) {
            if (mine.finished() || m_fatal.load()) return;
            const time_pt now = knst_web_detail::now_tp();
            const time_pt next = scan_timers(now);
            if (mine.finished()) { m_cv.notify_all(); return; }

            if (!pull_output()) { set_fatal_locked(knst_network_error::ProtocolError); continue; }
            const bool more_out = nghttp2_session_want_write(m_ng.get()) != 0;
            if (!nghttp2_session_want_read(m_ng.get()) && !more_out && m_woff >= m_wbuf.size()) {
                set_fatal_locked(knst_network_error::ConnectionClosed);   // session fully terminated
                continue;
            }

            bool progress = false, wr_wants_read = false;
            if (m_woff < m_wbuf.size()) write_burst(lk, progress, wr_wants_read);
            if (m_fatal.load()) continue;

            bool s_read = false;
            if (m_sock->has_pending_input()) {
                s_read = true;                                      // decrypted bytes already buffered
            } else {
                const bool want_w = m_woff < m_wbuf.size() && !wr_wants_read;
                int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count();
                const int64_t cap = m_wake.ok() ? 30000 : 50;
                if (ms < 1) ms = 1;
                if (ms > cap) ms = cap;
                const uint32_t tmo = (progress && more_out) ? 0u : static_cast<uint32_t>(ms);
                bool sw = false, woke = false;
                knst_network_error pe = knst_network_error::None;
                lk.unlock();
                knst_web_detail::poll_sock_wake(m_sock->native_handle(), want_w, m_wake.handle(),
                                                tmo, s_read, sw, woke, pe);
                if (woke) m_wake.drain();
                lk.lock();
                if (pe != knst_network_error::None) { set_fatal_locked(pe); continue; }
            }
            if (s_read) read_burst(lk, 16);
            m_cv.notify_all();                                      // streams may have finished
        }
    }

    /* ------------------------------------------------------ members */
    std::unique_ptr<knst_h2_socket>                    m_sock;
    std::unique_ptr<nghttp2_session, ng_del>           m_ng;
    knst_web_detail::wake_event                        m_wake;

    std::mutex                                         m_mtx;
    std::condition_variable                            m_cv;
    bool                                               m_driving = false;      // guarded by m_mtx
    std::unordered_map<int32_t, knst_web_stream*>      m_streams;              // guarded by m_mtx
    std::unordered_map<int32_t, std::unique_ptr<src_t>> m_srcs;                // guarded by m_mtx

    std::vector<uint8_t>                               m_wbuf, m_rbuf;         // driver-only
    size_t                                             m_woff = 0;             // driver-only

    knst_network_error                                 m_fatal_err = knst_network_error::None;
    std::atomic<bool>                                  m_fatal{false}, m_goaway{false}, m_retired{false};
    std::atomic<uint32_t>                              m_reserved{0};
    std::atomic<uint32_t>                              m_peer_max{100};
    std::atomic<uint64_t>                              m_requests{0};
    std::atomic<int64_t>                               m_idle_ns{0};
};


/* ============================================================================
 * knst_web_session_pool<S> — per-origin session pool (shared_ptr: no UAF)
 * ============================================================================ */

template<class S>
class knst_web_session_pool {
public:
    typedef std::shared_ptr<S> ptr;

    // Returns a session with one slot RESERVED for the caller (released by S::execute).
    // Concurrent callers for the same key funnel through one creation lock, so a burst
    // of N threads opens ONE connection and multiplexes (up to local_cap streams).
    template<class F>
    ptr acquire(const std::string& key, uint32_t local_cap, uint32_t max_sessions, uint32_t idle_ms,
                const knst_web_detail::deadline_t& dl, F&& make, knst_network_error& err) noexcept {
        err = knst_network_error::None;
        KNST_WEB_TRY
            uint32_t cnt = 0;
            if (ptr s = find(key, local_cap, idle_ms, false, cnt)) return s;
            std::shared_ptr<std::timed_mutex> cm = creation_mutex(key);
            if (!cm) { err = knst_network_error::OutOfMemory; return nullptr; }
            if (dl.infinite) cm->lock();
            else if (!cm->try_lock_until(dl.end)) { err = knst_network_error::Timeout; return nullptr; }
            std::lock_guard<std::timed_mutex> lk(*cm, std::adopt_lock);

            if (ptr s = find(key, local_cap, idle_ms, false, cnt)) return s;
            if (cnt > 0 && cnt >= (max_sessions ? max_sessions : 1))
                if (ptr s = find(key, local_cap, idle_ms, true, cnt)) return s;   // saturated: overcommit, nghttp2 queues

            ptr n = make(err);
            if (!n) return nullptr;
            n->force_reserve();
            add(key, n);
            return n;
        KNST_WEB_CATCH(err, nullptr)
        return nullptr;
    }

    void clear() noexcept {
        std::vector<ptr> dead;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            for (auto& e : m_e) { e.s->retire(); dead.push_back(std::move(e.s)); }
            m_e.clear();
        }
    }
    uint32_t size() const noexcept {
        std::lock_guard<std::mutex> lk(m_mtx);
        uint32_t n = 0;
        for (auto& e : m_e) if (e.s && e.s->is_usable()) ++n;
        return n;
    }

private:
    struct entry { std::string key; ptr s; };

    ptr find(const std::string& key, uint32_t cap, uint32_t idle_ms, bool force, uint32_t& cnt) noexcept {
        std::vector<ptr> dead;                                  // destroyed after the lock is released
        ptr result;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            const auto now = std::chrono::steady_clock::now();
            for (size_t i = 0; i < m_e.size(); ) {
                S* s = m_e[i].s.get();
                const bool stale = idle_ms && s->load() == 0 &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - s->idle_since()).count() > static_cast<long long>(idle_ms);
                if (!s->is_usable() || stale) { dead.push_back(std::move(m_e[i].s)); m_e.erase(m_e.begin() + static_cast<long>(i)); }
                else ++i;
            }
            cnt = 0;
            ptr best;
            for (auto& e : m_e) {
                if (e.key != key) continue;
                ++cnt;
                if (!force && e.s->try_reserve(cap)) { result = e.s; break; }
                if (!best || e.s->load() < best->load()) best = e.s;
            }
            if (!result && force && best) { best->force_reserve(); result = best; }
        }
        return result;
    }

    void add(const std::string& key, const ptr& s) {
        std::vector<ptr> dead;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_e.push_back(entry{ key, s });
            while (m_e.size() > MAX_TOTAL) {                    // evict the oldest idle entry
                size_t victim = 0; bool found = false;
                for (size_t i = 0; i < m_e.size(); ++i) if (m_e[i].s->load() == 0) { victim = i; found = true; break; }
                if (!found) break;
                dead.push_back(std::move(m_e[victim].s));
                m_e.erase(m_e.begin() + static_cast<long>(victim));
            }
        }
    }

    std::shared_ptr<std::timed_mutex> creation_mutex(const std::string& key) {
        std::lock_guard<std::mutex> lk(m_cmtx);
        auto it = m_cm.find(key);
        if (it != m_cm.end()) return it->second;
        if (m_cm.size() >= 256) {
            for (auto i2 = m_cm.begin(); i2 != m_cm.end(); ) {
                if (i2->second.use_count() == 1) i2 = m_cm.erase(i2);
                else ++i2;
            }
        }
        std::shared_ptr<std::timed_mutex> m(new (std::nothrow) std::timed_mutex());
        if (m) m_cm[key] = m;
        return m;
    }

    static constexpr size_t MAX_TOTAL = 64;
    mutable std::mutex                                           m_mtx;
    std::vector<entry>                                           m_e;
    std::mutex                                                   m_cmtx;
    std::unordered_map<std::string, std::shared_ptr<std::timed_mutex>> m_cm;
};


/* ============================================================================
 * knst_web_client_base<Derived> — redirects, cookies, auth, headers, decoding
 *   Derived must provide:
 *     bool exchange(const knst_url&, const proxy_cfg&, const knst_web_options&,
 *                   const knst_web_request&, knst_http_response&,
 *                   const deadline_t& total, knst_network_error&) noexcept;
 * ============================================================================ */

template<class Derived>
class knst_web_client_base {
public:
    typedef knst_web_options                    options;
    typedef knst_http_client::multipart_part    multipart_part;

    knst_web_client_base() noexcept = default;
    knst_web_client_base(const knst_web_client_base&) = delete;
    knst_web_client_base& operator=(const knst_web_client_base&) = delete;

    bool get(const knst_c16string& url, const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"GET", url, nullptr, 0, o, out, err);
    }
    bool post(const knst_c16string& url, const knst_byte_string& body, const options& o,
              knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"POST", url, body.data(), body.length(), o, out, err);
    }
    bool put(const knst_c16string& url, const knst_byte_string& body, const options& o,
             knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"PUT", url, body.data(), body.length(), o, out, err);
    }
    bool del(const knst_c16string& url, const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"DELETE", url, nullptr, 0, o, out, err);
    }
    bool head(const knst_c16string& url, const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(u"HEAD", url, nullptr, 0, o, out, err);
    }
    bool request(const knst_c16string& method, const knst_c16string& url, const knst_byte_string& body,
                 const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return do_request(method, url, body.data(), body.length(), o, out, err);
    }

    void clear_cookies() { m_jar.clear(); }
    knst_vector<knst_cookie> cookies() const { return m_jar.snapshot(); }

    // Client-level default header (validated like per-request headers).
    bool set_header(const knst_c16string& name, const knst_c16string& value) {
        knst_byte_string n, v; knst_network_error e = knst_network_error::None;
        if (!knst_web_detail::check_user_header(knst_http_header{ name, value }, n, v, e)) return false;
        std::lock_guard<std::mutex> lk(m_hdr_mtx);
        knst_web_detail::set_pair(m_extra, std::move(n), std::move(v));
        return true;
    }
    void clear_headers() { std::lock_guard<std::mutex> lk(m_hdr_mtx); m_extra.clear(); }

    // multipart helpers (forward to the h1 implementation)
    static void build_multipart(const knst_vector<multipart_part>& parts, knst_byte_string& out, knst_c16string& boundary) {
        knst_http_client::build_multipart(parts, out, boundary);
    }
    static knst_c16string multipart_content_type(const knst_c16string& boundary) {
        return knst_http_client::multipart_content_type(boundary);
    }
    static bool parse_multipart(const knst_byte_string& body, const knst_c16string& content_type,
                                knst_vector<multipart_part>& out, knst_network_error& err) noexcept {
        return knst_http_client::parse_multipart(body, content_type, out, err);
    }

#if defined(KNST_NETWORK_ENABLE_JSON)
    bool get_json(const knst_c16string& url, const options& o,
                  knst_json& out, knst_network_error& err) noexcept {
        knst_http_response r;
        if (!get(url, o, r, err)) return false;
        out = r.json();
        if (out.is_null() && r.body.length() > 0) {
            err = knst_network_error::HttpMalformedBody;
            return false;
        }
        return true;
    }

    bool post_json(const knst_c16string& url, const knst_json& payload,
                   const options& o, knst_json& out,
                   knst_network_error& err) noexcept {
        knst_json_buffer buf;
        if (!payload.dump(buf)) {
            err = knst_network_error::OutOfMemory;
            return false;
        }
                knst_byte_string raw;
        raw.resize(static_cast<uint32_t>(buf.size()));
        if (raw.length() != buf.size()) {
            err = knst_network_error::OutOfMemory;
            return false;
        }
        if (buf.size() > 0) ::memcpy(&raw[0], buf.data(), buf.size());

        options o2 = o;
        knst_http_header h; h.name = u"Content-Type"; h.value = u"application/json";
        o2.headers.push_back(h);

        knst_http_response resp;
        if (!post(url, raw, o2, resp, err)) return false;
        out = resp.json();
        if (out.is_null() && resp.body.length() > 0) {
            err = knst_network_error::HttpMalformedBody;
            return false;
        }
        return true;
    }
#endif

protected:
    static bool same_origin(const knst_url& a, const knst_url& b) {
        return a.scheme == b.scheme && a.host == b.host && a.effective_port() == b.effective_port();
    }

    bool do_request(const knst_c16string& method, const knst_c16string& url, const void* body, size_t body_len,
                    const options& o, knst_http_response& out, knst_network_error& err) noexcept
    {
        KNST_WEB_GUARD_BEGIN
        typedef knst_vector<knst_web_header> hv_t;
        err = knst_network_error::None;
        out = knst_http_response{};

        if (o.use_chunked_upload) { err = knst_network_error::InvalidArgument; return false; }
        if (!knst_net_detail::valid_header_name(method)) { err = knst_network_error::InvalidArgument; return false; }
        if (body_len > 0 && !body) { err = knst_network_error::InvalidArgument; return false; }

        hv_t user_h, user_t, client_h;
        for (uint32_t i = 0; i < o.headers.size(); ++i) {
            knst_byte_string n, v;
            if (!knst_web_detail::check_user_header(o.headers[i], n, v, err)) return false;
            knst_web_detail::set_pair(user_h, std::move(n), std::move(v));
        }
        for (uint32_t i = 0; i < o.request_trailers.size(); ++i) {
            knst_byte_string n, v;
            if (!knst_web_detail::check_user_header(o.request_trailers[i], n, v, err)) return false;
            knst_web_header h;
            h.name = std::move(n);
            h.value = std::move(v);
            user_t.push_back(std::move(h));
        }
        { std::lock_guard<std::mutex> lk(m_hdr_mtx); client_h = m_extra; }

        const uint8_t* body_ptr = static_cast<const uint8_t*>(body);       // caller memory, never copied
        size_t body_n = body_ptr ? body_len : 0;
        bool dropped = false;                                              // body removed by a 301/302/303 rewrite
        knst_c16string cur_url = url, cur_method = method;
        int hops = 0;
        knst_url origin_url; bool origin_set = false;
        const knst_web_detail::deadline_t total_dl = knst_web_detail::deadline_t::after(
            o.total_timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : o.total_timeout_ms);

        for (;;) {
            knst_url u;
            if (!knst_url::parse(cur_url, u, err)) { if (err == knst_network_error::None) err = knst_network_error::HttpParseUrl; return false; }
            if (!origin_set) { origin_url = u; origin_set = true; }
            u.userinfo.clear();                                            // auth only via options
            if (u.scheme != u"https") { err = knst_network_error::NotSupported; return false; }
            { bool expired = false; total_dl.remaining_ms(expired); if (expired) { err = knst_network_error::Timeout; return false; } }

            knst_web_detail::proxy_cfg pc;
            if (!knst_web_detail::resolve_proxy(u, o, pc, err)) return false;

            knst_web_request r;
            r.method    = knst_web_detail::c16_utf8(cur_method);
            r.authority = knst_web_detail::c16_utf8(u.host_header_value());
            r.path      = knst_web_detail::c16_utf8(u.request_target());
            if (!o.user_agent.empty()) knst_web_detail::set_pair(r.headers, "user-agent", knst_web_detail::c16_utf8(o.user_agent));
            knst_web_detail::set_pair(r.headers, "accept", "*/*");
            knst_web_detail::set_pair(r.headers, "accept-encoding",
                (o.allow_content_encoding && !o.on_body) ? "gzip, deflate" : "identity");

            const bool cross = !same_origin(u, origin_url);
            auto add_user = [&](const hv_t& src) {
                for (uint32_t i = 0; i < src.size(); ++i) {
                    if (cross && knst_web_detail::is_sensitive_name(src[i].name)) continue;
                    if (dropped && src[i].name == "content-type") continue;
                    knst_web_detail::set_pair(r.headers, src[i].name, src[i].value);
                }
            };
            add_user(client_h);
            add_user(user_h);

            if (!o.basic_user.empty() && !cross && !knst_web_detail::has_pair(r.headers, "authorization")) {
                knst_byte_string up = knst_net_detail::to_utf8(o.basic_user);
                up.push_back(':');
                knst_byte_string pw = knst_net_detail::to_utf8(o.basic_pass);
                knst_net_detail::append_bytes(up, pw.data(), pw.length());
                knst_c16string a = u"Basic ";
                a.append(knst_net_detail::base64_encode_bytes(up));
                knst_web_detail::set_pair(r.headers, "authorization", knst_web_detail::c16_utf8(a));
            }
            if (o.auto_cookie && !knst_web_detail::has_pair(r.headers, "cookie")) {
                knst_c16string ch = m_jar.header_for(u);
                if (!ch.empty()) knst_web_detail::set_pair(r.headers, "cookie", knst_web_detail::c16_utf8(ch));
            }

            const bool body_method = (cur_method == u"POST" || cur_method == u"PUT" || cur_method == u"PATCH");
            if (body_n > 0) {
                knst_web_detail::set_pair(r.headers, "content-length", std::to_string(body_n));
                if (!knst_web_detail::has_pair(r.headers, "content-type"))
                    knst_web_detail::set_pair(r.headers, "content-type", knst_web_detail::c16_utf8(o.default_content_type));
                r.body = body_ptr; r.body_len = body_n;
            } else if (body_method) {
                knst_web_detail::set_pair(r.headers, "content-length", "0");
            }
            if (!dropped) r.trailers = user_t;                              // 307/308 resend body AND trailers

            knst_http_response resp;
            if (!static_cast<Derived*>(this)->exchange(u, pc, o, r, resp, total_dl, err)) {
                if (err == knst_network_error::None) err = knst_network_error::Unknown;
                return false;
            }
            if (o.auto_cookie) m_jar.collect(resp, u);

            if (resp.is_redirect() && o.follow_redirects) {
                knst_c16string loc = resp.get_header(u"location");
                if (!loc.empty()) {
                    if (++hops > o.max_redirects) { err = knst_network_error::HttpTooManyRedirects; return false; }
                    if (resp.status == 303) {
                        if (cur_method != u"HEAD") cur_method = u"GET";
                        body_n = 0; dropped = true;
                    } else if ((resp.status == 301 || resp.status == 302) && cur_method == u"POST") {
                        cur_method = u"GET"; body_n = 0; dropped = true;
                    }
                    knst_url next;
                    if (!knst_url::resolve(u, loc, next, err)) { if (err == knst_network_error::None) err = knst_network_error::HttpParseUrl; return false; }
                    cur_url = next.to_string(false);
                    continue;
                }
            }

            // Content-Encoding decode (buffered mode only)
            knst_c16string ce = resp.get_header(u"content-encoding");
            const bool has_enc = !ce.empty() && !knst_net_detail::ieq(knst_net_detail::trim16(ce), u"identity");
            if (has_enc && o.allow_content_encoding && !o.on_body && resp.body.length() > 0) {
                size_t lim = o.max_decompressed_bytes > knst_net_detail::KNST_NET_MAX_LEN
                           ? knst_net_detail::KNST_NET_MAX_LEN : static_cast<size_t>(o.max_decompressed_bytes);
                if (!knst_net_detail::decode_content_encoding(ce, resp.body, lim, err)) return false;
                for (uint32_t i = 0; i < resp.headers.size(); ) {
                    if (knst_net_detail::ieq(resp.headers[i].name, u"content-encoding")) resp.headers.erase(i); else ++i;
                }
                for (uint32_t i = 0; i < resp.headers.size(); ++i) {
                    if (knst_net_detail::ieq(resp.headers[i].name, u"content-length")) {
                        knst_c16string nv; knst_net_detail::append_uint(nv, resp.body.length());
                        resp.headers[i].value = nv;
                    }
                }
            }
            out = std::move(resp);
            return true;
        }
        KNST_WEB_GUARD_END(err)
    }

    knst_cookie_jar                                          m_jar;
    std::mutex                                               m_hdr_mtx;
        knst_vector<knst_web_header>                             m_extra;
};


/* ============================================================================
 * knst_http2_client — public HTTP/2 client
 * ============================================================================ */

class knst_http2_client : public knst_web_client_base<knst_http2_client> {
public:
    knst_http2_client() noexcept = default;

    void     clear_connection_pool() noexcept { m_pool.clear(); }
    uint32_t idle_sessions() const noexcept   { return m_pool.size(); }   // live pooled sessions

private:
    friend class knst_web_client_base<knst_http2_client>;

    static std::string make_key(const knst_url& u, const knst_web_detail::proxy_cfg& pc, const knst_web_options& o) {
        std::string k = "h2|";
        k += o.allow_private_networks ? "O|" : "S|";                         // guarded and unguarded never share
        k += knst_web_detail::c16_utf8(u.host); k += ':'; k += std::to_string(u.effective_port());
        if (pc.enabled()) {
            k += "|p:"; k += knst_web_detail::c16_utf8(pc.host); k += ':'; k += std::to_string(pc.port);
            k += (pc.kind == knst_web_detail::proxy_cfg::kind_t::Http) ? 'h' : (pc.socks5_remote_dns ? 'H' : 's');
            if (pc.has_auth) {
                std::string cred = knst_web_detail::c16_utf8(pc.auth);
                cred.append(reinterpret_cast<const char*>(pc.user_raw.data()), pc.user_raw.length());
                cred.push_back(0);
                cred.append(reinterpret_cast<const char*>(pc.pass_raw.data()), pc.pass_raw.length());
                k += "|a:"; k += std::to_string(std::hash<std::string>()(cred));
            }
        }
        if (!o.bind_local_ip.empty()) { k += "|b:"; k += knst_web_detail::c16_utf8(o.bind_local_ip); }
        return k;
    }

    bool exchange(const knst_url& u, const knst_web_detail::proxy_cfg& pc, const knst_web_options& o,
                  const knst_web_request& r, knst_http_response& out,
                  const knst_web_detail::deadline_t& total, knst_network_error& err) noexcept
    {
        KNST_WEB_GUARD_BEGIN
        const bool idem = knst_web_detail::is_idempotent_method(r.method);
        const std::string key = make_key(u, pc, o);
        for (int attempt = 0; attempt < 3; ++attempt) {
            std::shared_ptr<knst_h2_session> s = m_pool.acquire(
                key, o.max_concurrent_streams ? o.max_concurrent_streams : 100,
                o.max_sessions_per_origin, o.keepalive_idle_ms, total,
                [&](knst_network_error& e) { return knst_h2_session::create(u, pc, o, total, e); }, err);
            if (!s) return false;
            const bool reused = s->requests_started() > 0;
            out = knst_http_response{};
            knst_web_stream st;
            const bool ok = s->execute(r, o, total, out, st, err);
            if (!o.use_keepalive) s->retire();
            if (ok) return true;
            // REFUSED_STREAM / GOAWAY guarantee "not processed": retry any method.
            // A stale reused connection that died before the first response byte: idempotent only.
            const bool safe = st.refused || (!st.response_started && reused && idem && knst_web_detail::is_conn_error(err));
            if (safe && attempt < 2) { err = knst_network_error::None; continue; }
            return false;
        }
        if (err == knst_network_error::None) err = knst_network_error::Unknown;
        return false;
        KNST_WEB_GUARD_END(err)
    }

    knst_web_session_pool<knst_h2_session> m_pool;
};

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif