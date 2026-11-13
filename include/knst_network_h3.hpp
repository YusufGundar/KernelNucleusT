// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/* ============================================================================
 * knst_network_h3.hpp — HTTP/3 (QUIC) client for KernelNucleusT 
 * ----------------------------------------------------------------------------
 *  Requires : knst_network_h2.hpp (shared knst_web_* layer, therefore nghttp2
 *             headers too), knst_network.hpp, KNST_NETWORK_ENABLE_TLS,
 *             ngtcp2 >= 1.0 + ngtcp2_crypto_quictls, nghttp3 >= 1.1,
 *             a QUIC-capable TLS library (quictls / OpenSSL fork).
 *             Stock OpenSSL 3.0 (Ubuntu 22.04 / Pop!_OS) is NOT enough.
 *
 *  Behaviour notes
 *    - https only, UDP/443-style prior knowledge (no Alt-Svc discovery here).
 *      If UDP is blocked the connect fails with Timeout / ConnectionRefused;
 *      the caller may fall back to knst_http2_client.
 *    - Proxies are not supported (NotSupported): HTTP/3 needs CONNECT-UDP.
 *    - No 0-RTT, no connection migration, no server push, no DATAGRAM.
 *    - TLS session tickets are cached under the key "h3|host:port".
 *    - options::lenient_close_delimited : ignored (framed protocol).
 *    - options::use_chunked_upload      : InvalidArgument.
 *    - options::on_body may run on another thread (the one driving the
 *      connection); it must not re-enter the client.
 *    - Call knst_tls_set_ca_file/path/client_cert BEFORE the first connection.
 *
 *  Thread safety: one client may be shared by many threads; requests to one
 *  origin multiplex over one QUIC connection.
 * ============================================================================ */

#pragma once

#include "knst_network_h2.hpp"

#if !defined(KNST_NETWORK_ENABLE_TLS)
    #error "knst_network_h3.hpp requires KNST_NETWORK_ENABLE_TLS"
#endif

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_quictls.h>
#include <nghttp3/nghttp3.h>
#include <openssl/rand.h>

#if NGTCP2_VERSION_NUM < 0x010000
    #error "knst_network_h3.hpp: ngtcp2 >= 1.0 required"
#endif
#if NGHTTP3_VERSION_NUM < 0x010100
    #error "knst_network_h3.hpp: nghttp3 >= 1.1 required"
#endif

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4996)
#endif


/* ============================================================================
 * knst_h3_detail — UDP socket, QUIC TLS context, time helpers
 * ============================================================================ */

namespace knst_h3_detail {

    inline uint64_t now_ns() noexcept {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    inline knst_web_detail::time_pt tp_from_ns(uint64_t ns) noexcept {
        return knst_web_detail::time_pt(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::nanoseconds(ns)));
    }

    // ICMP-derived errors on a connected UDP socket are spoofable and transient:
    // they are only fatal while the handshake is still running.
    inline bool soft_net_error(knst_network_error e) noexcept {
        return e == knst_network_error::ConnectionRefused || e == knst_network_error::HostUnreachable ||
               e == knst_network_error::NetworkUnreachable || e == knst_network_error::ConnectionReset;
    }

    // Non-blocking, connected UDP socket.
    class udp_conn {
    public:
        enum class r_t { Ok, Again, Fail };

        udp_conn() noexcept = default;
        ~udp_conn() noexcept { close(); }
        udp_conn(const udp_conn&) = delete;
        udp_conn& operator=(const udp_conn&) = delete;

        bool open(const knst_endpoint& peer, const knst_ip_address* bind_ip, knst_network_error& err) noexcept {
            close();
            err = knst_network_error::None;
            const int fam = peer.address.is_ipv4() ? AF_INET : AF_INET6;
            m_h = knst_net_detail::create_socket(fam, SOCK_DGRAM, 0);
            if (m_h == KNST_INVALID_SOCKET) { err = knst_net_detail::map_errno(knst_net_detail::last_socket_error()); return false; }
            int sz = 2 << 20;
            ::setsockopt(m_h, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&sz), sizeof(sz));
            sz = 1 << 20;
            ::setsockopt(m_h, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sz), sizeof(sz));
        #if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DO)
            if (fam == AF_INET) { int v = IP_PMTUDISC_DO; ::setsockopt(m_h, IPPROTO_IP, IP_MTU_DISCOVER, reinterpret_cast<const char*>(&v), sizeof(v)); }
        #endif
        #if defined(IPV6_MTU_DISCOVER) && defined(IPV6_PMTUDISC_DO)
            if (fam == AF_INET6) { int v = IPV6_PMTUDISC_DO; ::setsockopt(m_h, IPPROTO_IPV6, IPV6_MTU_DISCOVER, reinterpret_cast<const char*>(&v), sizeof(v)); }
        #endif
            if (bind_ip && bind_ip->is_valid() && bind_ip->family() == fam) {
                knst_endpoint le(*bind_ip, 0);
                sockaddr_storage b = le.to_native();
                if (::bind(m_h, reinterpret_cast<const sockaddr*>(&b), le.native_length()) != 0) {
                    err = knst_net_detail::map_errno(knst_net_detail::last_socket_error()); close(); return false;
                }
            }
            m_rs = peer.to_native();
            m_rl = peer.native_length();
            if (::connect(m_h, reinterpret_cast<const sockaddr*>(&m_rs), m_rl) != 0) {
                err = knst_net_detail::map_errno(knst_net_detail::last_socket_error()); close(); return false;
            }
            m_ls = sockaddr_storage{};
            m_ll = sizeof(m_ls);
            if (::getsockname(m_h, reinterpret_cast<sockaddr*>(&m_ls), &m_ll) != 0) {
                err = knst_net_detail::map_errno(knst_net_detail::last_socket_error()); close(); return false;
            }
            return true;
        }

        r_t send(const uint8_t* p, size_t n, knst_network_error& e) noexcept {
            for (;;) {
            #if KNST_USING_PLATFORM_WINDOWS
                int k = ::send(m_h, reinterpret_cast<const char*>(p), static_cast<int>(n), 0);
                if (k >= 0) return r_t::Ok;
                int se = ::WSAGetLastError();
            #else
                ssize_t k = ::send(m_h, p, n, KNST_MSG_NOSIGNAL);
                if (k >= 0) return r_t::Ok;
                int se = errno;
            #endif
                knst_network_error me = knst_net_detail::map_errno(se);
                if (me == knst_network_error::Interrupted) continue;
                if (me == knst_network_error::WouldBlock) return r_t::Again;
                if (me == knst_network_error::MessageTooLarge) return r_t::Ok;   // dropped; PMTUD copes
                e = me; return r_t::Fail;
            }
        }

        r_t recv(uint8_t* p, size_t cap, size_t& n, knst_network_error& e) noexcept {
            n = 0;
            for (;;) {
            #if KNST_USING_PLATFORM_WINDOWS
                int k = ::recv(m_h, reinterpret_cast<char*>(p), static_cast<int>(cap), 0);
                if (k >= 0) { n = static_cast<size_t>(k); return r_t::Ok; }
                int se = ::WSAGetLastError();
            #else
                ssize_t k = ::recv(m_h, p, cap, 0);
                if (k >= 0) { n = static_cast<size_t>(k); return r_t::Ok; }
                int se = errno;
            #endif
                knst_network_error me = knst_net_detail::map_errno(se);
                if (me == knst_network_error::Interrupted) continue;
                if (me == knst_network_error::WouldBlock) return r_t::Again;
                if (me == knst_network_error::MessageTooLarge) return r_t::Ok;   // truncated datagram: drop
                e = me; return r_t::Fail;
            }
        }

        bool is_open() const noexcept { return m_h != KNST_INVALID_SOCKET; }
        knst_socket_handle_t handle() const noexcept { return m_h; }
        const sockaddr_storage& local()  const noexcept { return m_ls; }
        socklen_t local_len()  const noexcept { return m_ll; }
        const sockaddr_storage& remote() const noexcept { return m_rs; }
        socklen_t remote_len() const noexcept { return m_rl; }
        void close() noexcept { knst_net_detail::close_handle(m_h); m_h = KNST_INVALID_SOCKET; }

    private:
        knst_socket_handle_t m_h = KNST_INVALID_SOCKET;
        sockaddr_storage m_ls{}, m_rs{};
        socklen_t m_ll = 0, m_rl = 0;
    };

    // Process-wide SSL_CTX for QUIC (TLS 1.3 only, ALPN "h3").
    inline SSL_CTX* tls_ctx_h3() noexcept {
        static std::once_flag once;
        static SSL_CTX* ctx = nullptr;
        std::call_once(once, []() noexcept {
            static const unsigned char alpn[] = { 2, 'h', '3' };
            SSL_CTX* c = knst_web_detail::build_ssl_ctx(alpn, sizeof(alpn), true, false);
            if (!c) return;
            if (ngtcp2_crypto_quictls_configure_client_context(c) != 0) { SSL_CTX_free(c); ERR_clear_error(); return; }
            // session cache AFTER the ngtcp2 helper (it installs the QUIC method)
            SSL_CTX_set_session_cache_mode(c, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
            SSL_CTX_sess_set_new_cb(c, knst_net_detail::tls_new_session_cb);
            ctx = c;
        });
        return ctx;
    }

    static constexpr uint64_t H3_NO_ERROR         = 0x100;
    static constexpr uint64_t H3_INTERNAL_ERROR   = 0x102;
    static constexpr uint64_t H3_REQUEST_REJECTED = 0x10b;
    static constexpr uint64_t H3_REQUEST_CANCELLED= 0x10c;

} // namespace knst_h3_detail


/* ============================================================================
 * knst_h3_session — one QUIC connection, many multiplexed request streams
 *
 *  Locking: m_mtx guards ngtcp2, nghttp3, the stream maps and all stream
 *  state. Every callback runs inside an ngtcp2/nghttp3 call that is made with
 *  m_mtx held, so callbacks never lock. One thread at a time drives the UDP
 *  socket ("driver", m_driving); it releases m_mtx only around poll().
 * ============================================================================ */

class knst_h3_session {
public:
    typedef knst_web_detail::time_pt    time_pt;
    typedef knst_web_detail::deadline_t deadline_t;
    typedef knst_h3_detail::udp_conn    udp_t;

    knst_h3_session(const knst_h3_session&) = delete;
    knst_h3_session& operator=(const knst_h3_session&) = delete;
    ~knst_h3_session() noexcept { shutdown_(); }

    static std::shared_ptr<knst_h3_session> create(
        const knst_url& u, const knst_web_detail::proxy_cfg& pc,
        const knst_web_options& o, const deadline_t& total, knst_network_error& err) noexcept
    {
        err = knst_network_error::None;
        if (pc.enabled()) { err = knst_network_error::NotSupported; return nullptr; }

        deadline_t cd = deadline_t::after(o.timeout_ms == 0 ? knst_net_detail::KNST_NET_INFINITE : o.timeout_ms);
        cd = knst_web_detail::min_deadline(cd, total);

        knst_ip_address bip; bool have_bind = false;
        if (!o.bind_local_ip.empty()) {
            knst_network_error e2 = knst_network_error::None;
            bip = knst_ip_address::from_string(o.bind_local_ip, e2);
            if (e2 != knst_network_error::None || !bip.is_valid()) { err = knst_network_error::InvalidArgument; return nullptr; }
            have_bind = true;
        }
        const uint32_t dns_to = o.dns_timeout_ms ? o.dns_timeout_ms : o.timeout_ms;
        knst_vector<knst_endpoint> eps;
        if (!knst_web_detail::resolve_endpoints(u.host, u.effective_port(), o, dns_to, cd,
                                                !o.allow_private_networks, eps, err)) return nullptr;
        for (uint32_t i = 0; i < eps.size(); ++i) {
            bool expired = false;
            uint32_t rem = cd.remaining_ms(expired);
            if (expired) { err = knst_network_error::Timeout; return nullptr; }
            uint32_t budget = knst_net_detail::attempt_budget(rem, eps.size() - i);
            deadline_t ad = deadline_t::after(budget);
            std::shared_ptr<knst_h3_session> s = attempt(eps[i], u, o, have_bind ? &bip : nullptr, ad, err);
            if (s) return s;
            if (err == knst_network_error::TlsCertificateInvalid) return nullptr;   // another address will not help
        }
        if (err == knst_network_error::None) err = knst_network_error::ConnectionRefused;
        return nullptr;
    }

    /* ---- pool interface (same contract as knst_h2_session) ---- */
    bool is_usable() const noexcept {
        return !m_fatal.load(std::memory_order_acquire) && !m_goaway.load(std::memory_order_acquire) &&
               !m_retired.load(std::memory_order_acquire) && m_udp.is_open();
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

    /* Submit one request and block until it completes. The pool slot is
       released before returning. `st` is owned by the caller. */
    bool execute(const knst_web_request& r, const knst_web_options& o, const deadline_t& total,
                 knst_http_response& out, knst_web_stream& st, knst_network_error& err) noexcept
    {
        struct slot_guard { knst_h3_session* s; ~slot_guard() noexcept { s->release(); } } sg{ this };
        KNST_WEB_GUARD_BEGIN
        err = knst_network_error::None;
        if (!m_conn) { err = knst_network_error::NotOpen; return false; }
        if (!st.setup(&out, r, o, total, o.timeout_ms, u"HTTP/3")) { err = knst_network_error::OutOfMemory; return false; }
        st.expect = o.expect_continue && r.body_len > 0;
        st.continue_ok = !st.expect;
        st.expect_deadline = knst_web_detail::now_tp() + std::chrono::milliseconds(o.expect_continue_timeout_ms);

        std::shared_ptr<ctx_t> h(new (std::nothrow) ctx_t());
        if (!h) { err = knst_network_error::OutOfMemory; return false; }
        h->st = &st; h->req = &r;

        std::unique_lock<std::mutex> lk(m_mtx);
        if (!is_usable()) { err = knst_network_error::ConnectionClosed; st.refused = true; return false; }
                // (idle-expiry check removed — ngtcp2 >= 1.26 no longer exposes
        //  ngtcp2_conn_get_idle_expiry; QUIC idle timeout is handled by
        //  ngtcp2 internally via ngtcp2_conn_get_expiry + handle_expiry)
        m_pending.push_back(h);
        m_requests.fetch_add(1, std::memory_order_relaxed);
        if (m_driving) m_wake.signal();

        run(st, lk);

        // ---- completion / cleanup (m_mtx held) ----
        bool ok = st.err == knst_network_error::None && st.got_final && (st.resp_end || st.discard);
        if (!ok && st.err == knst_network_error::None) st.err = knst_network_error::HttpConnectionClosed;
        if (ok) st.finalize();
        for (size_t i = 0; i < m_pending.size(); ++i)
            if (m_pending[i] == h) { m_pending.erase(m_pending.begin() + static_cast<long>(i)); break; }
        bool need_flush = false;
        if (h->sid >= 0 && !st.closed && !st.rst_sent) {
            // Stops ALL further reads of the caller's body memory for this stream.
            ngtcp2_conn_shutdown_stream(m_conn, 0, h->sid,
                st.resp_end ? knst_h3_detail::H3_NO_ERROR : knst_h3_detail::H3_REQUEST_CANCELLED);
            st.rst_sent = true; need_flush = true;
        }
        h->st = nullptr; h->req = nullptr;      // callbacks become no-ops; ctx stays until stream_close
        err = st.err;
        if (need_flush) {
            if (m_driving) m_wake.signal(); else flush_locked(o.cancel_grace_ms);
        }
        return ok;
        KNST_WEB_GUARD_END(err)
    }

private:
    knst_h3_session() noexcept = default;

    struct ctx_t {                              // per-request context, shared with the stream map
        knst_web_stream*        st  = nullptr;
        const knst_web_request* req = nullptr;
        int64_t                 sid = -1;
    };

    /* ------------------------------------------------------------ setup */
    static std::shared_ptr<knst_h3_session> attempt(const knst_endpoint& ep, const knst_url& u,
            const knst_web_options& o, const knst_ip_address* bind_ip,
            const deadline_t& dl, knst_network_error& err) noexcept
    {
        std::shared_ptr<knst_h3_session> self(new (std::nothrow) knst_h3_session());
        if (!self) { err = knst_network_error::OutOfMemory; return nullptr; }
        KNST_WEB_TRY
            self->m_rbuf.resize(65536);
            self->m_wbuf.resize(1500);
        KNST_WEB_CATCH(err, nullptr)
        self->m_max_hdr = o.max_header_bytes;
        if (!self->m_udp.open(ep, bind_ip, err)) return nullptr;
        if (!self->init_quic(u, o, err)) return nullptr;
        self->m_wake.init();                    // failure tolerated: poll is then capped at 50 ms

        std::unique_lock<std::mutex> lk(self->m_mtx);
        self->m_driving = true;
        while (!(self->m_hs_done && self->m_http) && !self->m_fatal.load()) {
            bool expired = false; dl.remaining_ms(expired);
            if (expired) { self->set_fatal_locked(knst_network_error::Timeout); break; }
            self->step(lk, dl.infinite ? knst_web_detail::now_tp() + std::chrono::seconds(30) : dl.end);
        }
        self->m_driving = false;
        if (self->m_fatal.load()) { err = self->m_fatal_err; return nullptr; }
        // learn the peer stream budget (QUIC counts streams cumulatively; use it as a concurrency hint)
        uint64_t left = ngtcp2_conn_get_streams_bidi_left(self->m_conn);
        self->m_peer_max.store(left == 0 ? 1u : (left > 1000 ? 1000u : static_cast<uint32_t>(left)));
        self->touch_idle();
        return self;
    }

    bool init_quic(const knst_url& u, const knst_web_options& o, knst_network_error& err) noexcept {
        SSL_CTX* ctx = knst_h3_detail::tls_ctx_h3();
        if (!ctx) { err = knst_network_error::TlsHandshakeFailed; return false; }
        char hbuf[256];
        if (!knst_web_detail::host_to_cstr(u.host, hbuf, sizeof(hbuf))) { err = knst_network_error::InvalidArgument; return false; }
        m_ssl = SSL_new(ctx);
        if (!m_ssl) { err = knst_network_error::TlsHandshakeFailed; ERR_clear_error(); return false; }
        if (!knst_web_detail::tls_prepare_peer(m_ssl, hbuf)) { err = knst_network_error::TlsHandshakeFailed; ERR_clear_error(); return false; }
        m_conn_ref.get_conn  = &knst_h3_session::get_conn;
        m_conn_ref.user_data = this;
        SSL_set_app_data(m_ssl, &m_conn_ref);
        SSL_set_connect_state(m_ssl);
        {   // session resumption key
            char* k = m_sess_key; size_t n = 0;
            k[n++] = 'h'; k[n++] = '3'; k[n++] = '|';
            for (const char* h = hbuf; *h && n + 8 < sizeof(m_sess_key); ++h)
                k[n++] = static_cast<char>((*h >= 'A' && *h <= 'Z') ? (*h - 'A' + 'a') : *h);
            ::snprintf(k + n, sizeof(m_sess_key) - n, ":%u", static_cast<unsigned>(u.effective_port()));
            SSL_set_ex_data(m_ssl, knst_net_detail::tls_sess_idx(), m_sess_key);
            if (SSL_SESSION* sess = knst_net_detail::tls_sess().get(m_sess_key)) { SSL_set_session(m_ssl, sess); SSL_SESSION_free(sess); }
        }

        ngtcp2_cid dcid, scid;
        uint8_t raw[16];
        if (RAND_bytes(raw, sizeof(raw)) != 1) { err = knst_network_error::Unknown; return false; }
        ngtcp2_cid_init(&dcid, raw, sizeof(raw));
        if (RAND_bytes(raw, sizeof(raw)) != 1) { err = knst_network_error::Unknown; return false; }
        ngtcp2_cid_init(&scid, raw, sizeof(raw));

        ngtcp2_callbacks cb{};
        cb.client_initial            = ngtcp2_crypto_client_initial_cb;
        cb.recv_crypto_data          = ngtcp2_crypto_recv_crypto_data_cb;
        cb.encrypt                   = ngtcp2_crypto_encrypt_cb;
        cb.decrypt                   = ngtcp2_crypto_decrypt_cb;
        cb.hp_mask                   = ngtcp2_crypto_hp_mask_cb;
        cb.recv_retry                = ngtcp2_crypto_recv_retry_cb;
        cb.update_key                = ngtcp2_crypto_update_key_cb;
        cb.delete_crypto_aead_ctx    = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
        cb.delete_crypto_cipher_ctx  = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
        cb.get_path_challenge_data   = ngtcp2_crypto_get_path_challenge_data_cb;
        cb.version_negotiation       = ngtcp2_crypto_version_negotiation_cb;
        cb.handshake_completed       = &knst_h3_session::q_handshake_completed;
        cb.recv_rx_key               = &knst_h3_session::q_recv_rx_key;
        cb.recv_stream_data          = &knst_h3_session::q_recv_stream_data;
        cb.acked_stream_data_offset  = &knst_h3_session::q_acked;
        cb.stream_close              = &knst_h3_session::q_stream_close;
        cb.stream_reset              = &knst_h3_session::q_stream_reset;
        cb.stream_stop_sending       = &knst_h3_session::q_stop_sending;
        cb.extend_max_local_streams_bidi = &knst_h3_session::q_max_streams;
        cb.extend_max_stream_data    = &knst_h3_session::q_max_stream_data;
        cb.rand                      = &knst_h3_session::q_rand;
        cb.get_new_connection_id     = &knst_h3_session::q_new_cid;

        ngtcp2_settings settings;
        ngtcp2_settings_default(&settings);
        settings.initial_ts = knst_h3_detail::now_ns();
        settings.max_tx_udp_payload_size = 1452;               // PMTUD upper bound; starts at 1200

        ngtcp2_transport_params tp;
        ngtcp2_transport_params_default(&tp);
        tp.initial_max_streams_bidi           = 0;
        tp.initial_max_streams_uni            = 100;
        tp.initial_max_data                   = o.connection_window ? o.connection_window : (16u << 20);
        tp.initial_max_stream_data_bidi_local = o.initial_stream_window ? o.initial_stream_window : (1u << 20);
        tp.initial_max_stream_data_uni        = 256 * 1024;
        tp.max_idle_timeout                   = 30 * NGTCP2_SECONDS;

        m_path.local.addr    = reinterpret_cast<ngtcp2_sockaddr*>(const_cast<sockaddr_storage*>(&m_udp.local()));
        m_path.local.addrlen = static_cast<ngtcp2_socklen>(m_udp.local_len());
        m_path.remote.addr   = reinterpret_cast<ngtcp2_sockaddr*>(const_cast<sockaddr_storage*>(&m_udp.remote()));
        m_path.remote.addrlen= static_cast<ngtcp2_socklen>(m_udp.remote_len());
        m_path.user_data     = nullptr;

        int rv = ngtcp2_conn_client_new(&m_conn, &dcid, &scid, &m_path, NGTCP2_PROTO_VER_V1,
                                        &cb, &settings, &tp, nullptr, this);
        if (rv != 0 || !m_conn) { err = knst_network_error::ProtocolError; m_conn = nullptr; return false; }
        ngtcp2_conn_set_tls_native_handle(m_conn, m_ssl);
        return true;
    }

    // Called from recv_rx_key(1-RTT): peer transport parameters are known now.
    bool setup_http() noexcept {
        if (m_http) return true;
        if (ngtcp2_conn_get_streams_uni_left(m_conn) < 3) { m_setup_err = knst_network_error::ProtocolError; return false; }
        nghttp3_callbacks cb{};
        cb.acked_stream_data = &knst_h3_session::h_acked;
        cb.stream_close      = &knst_h3_session::h_stream_close;
        cb.recv_data         = &knst_h3_session::h_recv_data;
        cb.deferred_consume  = &knst_h3_session::h_deferred_consume;
        cb.begin_headers     = &knst_h3_session::h_begin_headers;
        cb.recv_header       = &knst_h3_session::h_recv_header;
        cb.end_headers       = &knst_h3_session::h_end_headers;
        cb.begin_trailers    = &knst_h3_session::h_begin_headers;
        cb.recv_trailer      = &knst_h3_session::h_recv_header;
        cb.end_trailers      = &knst_h3_session::h_end_headers;
        cb.stop_sending      = &knst_h3_session::h_stop_sending;
                cb.end_stream        = &knst_h3_session::h_end_stream;
        cb.reset_stream      = &knst_h3_session::h_reset_stream;
        cb.shutdown          = &knst_h3_session::h_shutdown;
        nghttp3_settings hs;
        nghttp3_settings_default(&hs);
        hs.qpack_max_dtable_capacity = 4096;
        hs.qpack_blocked_streams     = 100;
        hs.max_field_section_size    = m_max_hdr;
        if (nghttp3_conn_client_new(&m_http, &cb, &hs, nghttp3_mem_default(), this) != 0) {
            m_http = nullptr; m_setup_err = knst_network_error::OutOfMemory; return false;
        }
        int64_t ctrl = -1, qenc = -1, qdec = -1;
        if (ngtcp2_conn_open_uni_stream(m_conn, &ctrl, nullptr) != 0 ||
            nghttp3_conn_bind_control_stream(m_http, ctrl) != 0 ||
            ngtcp2_conn_open_uni_stream(m_conn, &qenc, nullptr) != 0 ||
            ngtcp2_conn_open_uni_stream(m_conn, &qdec, nullptr) != 0 ||
            nghttp3_conn_bind_qpack_streams(m_http, qenc, qdec) != 0) {
            m_setup_err = knst_network_error::ProtocolError; return false;
        }
        return true;
    }

    void touch_idle() noexcept {
        m_idle_ns.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    }

    void shutdown_() noexcept {
        if (m_conn && m_udp.is_open() && !m_fatal.load()) {
            ngtcp2_ccerr ce;
            ngtcp2_ccerr_default(&ce);
            if (m_hs_done) ngtcp2_ccerr_set_application_error(&ce, knst_h3_detail::H3_NO_ERROR, nullptr, 0);
            ngtcp2_pkt_info pi{};
            ngtcp2_ssize n = ngtcp2_conn_write_connection_close(m_conn, nullptr, &pi, m_wbuf.data(), m_wbuf.size(), &ce,
                                                                knst_h3_detail::now_ns());
            if (n > 0) { knst_network_error e = knst_network_error::None; m_udp.send(m_wbuf.data(), static_cast<size_t>(n), e); }
        }
        if (m_http) { nghttp3_conn_del(m_http); m_http = nullptr; }
        if (m_conn) { ngtcp2_conn_del(m_conn); m_conn = nullptr; }
        if (m_ssl)  { SSL_free(m_ssl); m_ssl = nullptr; }
        m_udp.close();
    }

    /* -------------------------------------------------- ngtcp2 callbacks */
    static ngtcp2_conn* get_conn(ngtcp2_crypto_conn_ref* ref) noexcept {
        return static_cast<knst_h3_session*>(ref->user_data)->m_conn;
    }
    static int q_handshake_completed(ngtcp2_conn*, void* ud) noexcept {
        static_cast<knst_h3_session*>(ud)->m_hs_done = true;
        return 0;
    }
    static int q_recv_rx_key(ngtcp2_conn*, ngtcp2_encryption_level level, void* ud) noexcept {
        if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT) return 0;
        auto* self = static_cast<knst_h3_session*>(ud);
        return self->setup_http() ? 0 : NGTCP2_ERR_CALLBACK_FAILURE;
    }
    static int q_recv_stream_data(ngtcp2_conn* c, uint32_t flags, int64_t sid, uint64_t, const uint8_t* d, size_t n,
                                  void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (!self->m_http) return NGTCP2_ERR_CALLBACK_FAILURE;
        nghttp3_ssize used = nghttp3_conn_read_stream(self->m_http, sid, d, n, (flags & NGTCP2_STREAM_DATA_FLAG_FIN) ? 1 : 0);
        if (used < 0) return NGTCP2_ERR_CALLBACK_FAILURE;
        ngtcp2_conn_extend_max_stream_offset(c, sid, static_cast<uint64_t>(used));
        ngtcp2_conn_extend_max_offset(c, static_cast<uint64_t>(used));
        return 0;
    }
    static int q_acked(ngtcp2_conn*, int64_t sid, uint64_t, uint64_t datalen, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (!self->m_http) return 0;
        if (nghttp3_conn_add_ack_offset(self->m_http, sid, datalen) != 0) return NGTCP2_ERR_CALLBACK_FAILURE;
        if (ctx_t* h = self->find(sid)) if (h->st) h->st->touch();
        return 0;
    }
    static int q_stream_close(ngtcp2_conn*, uint32_t flags, int64_t sid, uint64_t code, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (!self->m_http) return 0;
        if (!(flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)) code = knst_h3_detail::H3_NO_ERROR;
        int rv = nghttp3_conn_close_stream(self->m_http, sid, code);
        if (rv != 0 && rv != NGHTTP3_ERR_STREAM_NOT_FOUND) return NGTCP2_ERR_CALLBACK_FAILURE;
        return 0;
    }
    static int q_stream_reset(ngtcp2_conn*, int64_t sid, uint64_t, uint64_t, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (self->m_http && nghttp3_conn_shutdown_stream_read(self->m_http, sid) != 0) return NGTCP2_ERR_CALLBACK_FAILURE;
        return 0;
    }
    static int q_stop_sending(ngtcp2_conn*, int64_t sid, uint64_t, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (self->m_http && nghttp3_conn_shutdown_stream_read(self->m_http, sid) != 0) return NGTCP2_ERR_CALLBACK_FAILURE;
        return 0;
    }
    static int q_max_streams(ngtcp2_conn*, uint64_t, void*) noexcept { return 0; }   // pending opens retried every step
    static int q_max_stream_data(ngtcp2_conn*, int64_t sid, uint64_t, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (self->m_http) {
            int rv = nghttp3_conn_unblock_stream(self->m_http, sid);
            if (rv != 0 && rv != NGHTTP3_ERR_STREAM_NOT_FOUND) return NGTCP2_ERR_CALLBACK_FAILURE;
        }
        return 0;
    }
    static void q_rand(uint8_t* dest, size_t len, const ngtcp2_rand_ctx*) noexcept {
        if (RAND_bytes(dest, static_cast<int>(len)) != 1) ::memset(dest, 0x5a, len);
    }
    static int q_new_cid(ngtcp2_conn*, ngtcp2_cid* cid, uint8_t* token, size_t cidlen, void*) noexcept {
        uint8_t buf[NGTCP2_MAX_CIDLEN];
        if (cidlen > sizeof(buf) || RAND_bytes(buf, static_cast<int>(cidlen)) != 1) return NGTCP2_ERR_CALLBACK_FAILURE;
        ngtcp2_cid_init(cid, buf, cidlen);
        if (RAND_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN) != 1) return NGTCP2_ERR_CALLBACK_FAILURE;
        return 0;
    }

    /* -------------------------------------------------- nghttp3 callbacks */
    ctx_t* find(int64_t sid) noexcept {
        auto it = m_streams.find(sid);
        return it == m_streams.end() ? nullptr : it->second.get();
    }
    static int h_acked(nghttp3_conn*, int64_t, uint64_t, void*, void*) noexcept { return 0; }
    static int h_stream_close(nghttp3_conn*, int64_t sid, uint64_t code, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        knst_network_error m = knst_network_error::None; bool refused = false;
        if (code == knst_h3_detail::H3_NO_ERROR) {}
        else if (code == knst_h3_detail::H3_REQUEST_REJECTED) { m = knst_network_error::ConnectionAborted; refused = true; }
        else if (code == knst_h3_detail::H3_REQUEST_CANCELLED) m = knst_network_error::ConnectionReset;
        else m = knst_network_error::ProtocolError;
        if (ctx_t* h = self->find(sid)) if (h->st) h->st->on_close(m, refused);
        self->m_streams.erase(sid);
        return 0;
    }
    static int h_recv_data(nghttp3_conn*, int64_t sid, const uint8_t* d, size_t n, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        ctx_t* h = self->find(sid);
        if (!h || !h->st) return 0;                              // abandoned stream: consumed and dropped
        h->st->touch();
        knst_network_error e = knst_network_error::None;
        h->st->on_data(d, n, e);                                 // failure sets st->err; scan() cancels the stream
        return 0;
    }
    static int h_deferred_consume(nghttp3_conn*, int64_t sid, size_t consumed, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        ngtcp2_conn_extend_max_stream_offset(self->m_conn, sid, consumed);
        ngtcp2_conn_extend_max_offset(self->m_conn, consumed);
        return 0;
    }
    static int h_begin_headers(nghttp3_conn*, int64_t sid, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (ctx_t* h = self->find(sid)) if (h->st) { h->st->touch(); h->st->begin_block(); }
        return 0;
    }
    static int h_recv_header(nghttp3_conn*, int64_t sid, int32_t, nghttp3_rcbuf* n, nghttp3_rcbuf* v, uint8_t,
                             void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        ctx_t* h = self->find(sid);
        if (!h || !h->st) return 0;
        nghttp3_vec nv = nghttp3_rcbuf_get_buf(n), vv = nghttp3_rcbuf_get_buf(v);
        h->st->touch();
        h->st->add_header(nv.base, nv.len, vv.base, vv.len);     // error stored in st->err
        return 0;
    }
    static int h_end_headers(nghttp3_conn*, int64_t sid, int fin, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        ctx_t* h = self->find(sid);
        if (!h || !h->st) return 0;
        knst_web_stream* st = h->st;
        st->touch();
        knst_network_error e = knst_network_error::None;
        switch (st->end_block(e)) {
            case knst_web_stream::block_t::Continue100:            // release a body held back by Expect
                st->continue_ok = true;
                if (st->deferred) { st->deferred = false; nghttp3_conn_resume_stream(self->m_http, sid); }
                break;
            default: break;                                        // Error (st->err set), Interim, Final, Trailer
        }
        if (fin) st->resp_end = true;
        return 0;
    }
    static int h_end_stream(nghttp3_conn*, int64_t sid, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (ctx_t* h = self->find(sid)) if (h->st) h->st->resp_end = true;
        return 0;
    }
    static int h_stop_sending(nghttp3_conn*, int64_t sid, uint64_t code, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (ngtcp2_conn_shutdown_stream_read(self->m_conn, 0, sid, code) != 0) return NGHTTP3_ERR_CALLBACK_FAILURE;
        return 0;
    }
    static int h_reset_stream(nghttp3_conn*, int64_t sid, uint64_t code, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        if (ngtcp2_conn_shutdown_stream_write(self->m_conn, 0, sid, code) != 0) return NGHTTP3_ERR_CALLBACK_FAILURE;
        return 0;
    }
    // GOAWAY: requests with stream id >= `id` were NOT processed => safe retry.
    static int h_shutdown(nghttp3_conn*, int64_t id, void* ud) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        self->m_goaway.store(true, std::memory_order_release);
        for (auto& kv : self->m_streams) {
            ctx_t* h = kv.second.get();
            if (kv.first >= id && h->st && !h->st->finished()) {
                h->st->refused = true;
                h->st->fail(knst_network_error::ConnectionAborted);
            }
        }
        return 0;
    }
    [[maybe_unused]] static void h_rand(uint8_t* dest, size_t len) noexcept {
        if (RAND_bytes(dest, static_cast<int>(len)) != 1) ::memset(dest, 0x5a, len);
    }

    // Request-body provider (runs inside nghttp3_conn_writev_stream, m_mtx held).
    static nghttp3_ssize h_read_body(nghttp3_conn* conn, int64_t sid, nghttp3_vec* vec, size_t veccnt,
                                     uint32_t* flags, void* ud, void*) noexcept {
        auto* self = static_cast<knst_h3_session*>(ud);
        ctx_t* h = self->find(sid);
        if (!h || !h->st || veccnt == 0) { *flags |= NGHTTP3_DATA_FLAG_EOF; return 0; }
        knst_web_stream* st = h->st;
        if (st->expect && !st->continue_ok) {
            if (knst_web_detail::now_tp() >= st->expect_deadline) st->continue_ok = true;
            else { st->deferred = true; return NGHTTP3_ERR_WOULDBLOCK; }
        }
        nghttp3_ssize cnt = 0;
        const size_t rem = st->body_len - st->body_off;
        if (rem > 0) {                                           // zero-copy: points into the caller's buffer
            vec[0].base = const_cast<uint8_t*>(st->body + st->body_off);
            vec[0].len  = rem;
            st->body_off = st->body_len;
            cnt = 1;
        }
        st->body_done = true;
        *flags |= NGHTTP3_DATA_FLAG_EOF;
        if (!st->trailers.empty()) {
            *flags |= NGHTTP3_DATA_FLAG_NO_END_STREAM;
            std::vector<nghttp3_nv> tv;
            tv.reserve(st->trailers.size());
            for (uint32_t i = 0; i < st->trailers.size(); ++i) {
                nghttp3_nv nv;
                nv.name     = const_cast<uint8_t*>(st->trailers[i].name.data());
                nv.value    = const_cast<uint8_t*>(st->trailers[i].value.data());
                nv.namelen  = st->trailers[i].name.length();
                nv.valuelen = st->trailers[i].value.length();
                nv.flags    = NGHTTP3_NV_FLAG_NONE;
                tv.push_back(nv);
            }
            if (nghttp3_conn_submit_trailers(conn, sid, tv.data(), tv.size()) != 0) return NGHTTP3_ERR_CALLBACK_FAILURE;
        }
        return cnt;
    }

    /* -------------------------------------------- stream open / timers */
    void set_fatal_locked(knst_network_error e) noexcept {
        if (m_fatal.load()) return;
        m_fatal_err = e;
        m_fatal.store(true, std::memory_order_release);
        for (auto& kv : m_streams) if (kv.second->st && !kv.second->st->finished()) kv.second->st->fail(e);
        for (auto& p : m_pending)  if (p->st && !p->st->finished()) { p->st->refused = true; p->st->fail(e); }
        m_cv.notify_all();
    }

    void cancel_locked(ctx_t* h, uint64_t code) noexcept {
        if (!h->st || h->sid < 0 || h->st->closed || h->st->rst_sent) return;
        ngtcp2_conn_shutdown_stream(m_conn, 0, h->sid, code);
        h->st->rst_sent = true;
    }

    void open_pending() noexcept {
        if (!m_http) return;
        for (size_t i = 0; i < m_pending.size(); ) {
            std::shared_ptr<ctx_t> h = m_pending[i];
            if (!h->st || h->st->finished()) { m_pending.erase(m_pending.begin() + static_cast<long>(i)); continue; }
            if (m_goaway.load()) {
                h->st->refused = true; h->st->fail(knst_network_error::ConnectionAborted);
                m_pending.erase(m_pending.begin() + static_cast<long>(i)); continue;
            }
            int64_t sid = -1;
            int rv = ngtcp2_conn_open_bidi_stream(m_conn, &sid, nullptr);
            if (rv == NGTCP2_ERR_STREAM_ID_BLOCKED) break;             // wait for MAX_STREAMS
            if (rv != 0) { h->st->fail(knst_network_error::ProtocolError); m_pending.erase(m_pending.begin() + static_cast<long>(i)); continue; }

                        const knst_web_request& r = *h->req;
            knst_byte_string scheme("https");
            std::vector<nghttp3_nv> nva;
            nva.reserve(4 + r.headers.size());
            auto push = [&](const char* name_cstr, const knst_byte_string& value) {
                nghttp3_nv nv;
                nv.name     = reinterpret_cast<uint8_t*>(const_cast<char*>(name_cstr));
                nv.value    = const_cast<uint8_t*>(value.data());
                nv.namelen  = std::strlen(name_cstr);
                nv.valuelen = value.length();
                nv.flags    = NGHTTP3_NV_FLAG_NONE;
                nva.push_back(nv);
            };
            push(":method", r.method);
            push(":scheme", scheme);
            push(":authority", r.authority);
            push(":path", r.path);
            for (uint32_t k = 0; k < r.headers.size(); ++k) {
                nghttp3_nv nv;
                nv.name     = const_cast<uint8_t*>(r.headers[k].name.data());
                nv.value    = const_cast<uint8_t*>(r.headers[k].value.data());
                nv.namelen  = r.headers[k].name.length();
                nv.valuelen = r.headers[k].value.length();
                nv.flags    = NGHTTP3_NV_FLAG_NONE;
                nva.push_back(nv);
            }

            nghttp3_data_reader dr{};
            dr.read_data = &knst_h3_session::h_read_body;
            const bool use_dr = r.body_len > 0 || !r.trailers.empty();
            rv = nghttp3_conn_submit_request(m_http, sid, nva.data(), nva.size(), use_dr ? &dr : nullptr, h.get());
            if (rv != 0) {
                ngtcp2_conn_shutdown_stream(m_conn, 0, sid, knst_h3_detail::H3_INTERNAL_ERROR);
                h->st->fail(knst_network_error::ProtocolError);
                m_pending.erase(m_pending.begin() + static_cast<long>(i)); continue;
            }
            h->sid = sid; h->st->stream_id = sid;
            m_streams[sid] = h;
            m_pending.erase(m_pending.begin() + static_cast<long>(i));
        }
    }

    // Per-stream deadlines, Expect timers, error-driven cancellation. Returns next wake time.
    time_pt scan(time_pt now) noexcept {
        time_pt next = now + std::chrono::seconds(30);
        open_pending();
        auto chk = [&](ctx_t* h) {
            knst_web_stream* st = h->st;
            if (!st) return;
            if (st->err != knst_network_error::None) { cancel_locked(h, knst_h3_detail::H3_REQUEST_CANCELLED); return; }
            if (st->finished()) return;
            if (st->expect && !st->continue_ok) {
                if (now >= st->expect_deadline) {
                    st->continue_ok = true;
                    if (st->deferred && h->sid >= 0 && m_http) { st->deferred = false; nghttp3_conn_resume_stream(m_http, h->sid); }
                } else if (st->expect_deadline < next) next = st->expect_deadline;
            }
            const time_pt ex = st->expiry();
            if (now >= ex) { st->fail(knst_network_error::Timeout); cancel_locked(h, knst_h3_detail::H3_REQUEST_CANCELLED); }
            else if (ex < next) next = ex;
        };
        for (auto& kv : m_streams) chk(kv.second.get());
        for (auto& p : m_pending)  chk(p.get());
        return next;
    }

    /* ----------------------------------------------------- packet I/O */
    // Writes up to 64 packets. Returns true when more output is likely pending.
        bool write_packets(uint64_t ts) noexcept {
        [[maybe_unused]] knst_network_error dummy = knst_network_error::None;
        KNST_WEB_TRY
            knst_network_error e = knst_network_error::None;
            if (!m_pend.empty()) {                                   // flush a packet the kernel refused earlier
                auto r = m_udp.send(m_pend.data(), m_pend.size(), e);
                if (r == udp_t::r_t::Again) { m_wblocked = true; return false; }
                if (r == udp_t::r_t::Fail && (!m_hs_done || !knst_h3_detail::soft_net_error(e))) { set_fatal_locked(e); return false; }
                m_pend.clear();
            }
            m_wblocked = false;
            size_t cnt = 0;
            while (cnt < 64) {
                int64_t sid = -1; int fin = 0;
                nghttp3_vec vec[16];
                nghttp3_ssize sv = 0;
                if (m_http && ngtcp2_conn_get_max_data_left(m_conn)) {
                    sv = nghttp3_conn_writev_stream(m_http, &sid, &fin, vec, 16);
                    if (sv < 0) { set_fatal_locked(knst_network_error::ProtocolError); return false; }
                }
                uint32_t fl = NGTCP2_WRITE_STREAM_FLAG_MORE;
                if (fin) fl |= NGTCP2_WRITE_STREAM_FLAG_FIN;
                ngtcp2_pkt_info pi{};
                ngtcp2_ssize nd = -1;
                ngtcp2_ssize nw = ngtcp2_conn_writev_stream(m_conn, nullptr, &pi, m_wbuf.data(), m_wbuf.size(), &nd, fl, sid,
                                                            reinterpret_cast<const ngtcp2_vec*>(vec), static_cast<size_t>(sv), ts);
                if (nw < 0) {
                    switch (nw) {
                        case NGTCP2_ERR_STREAM_DATA_BLOCKED:
                            if (sid >= 0) nghttp3_conn_block_stream(m_http, sid);
                            continue;
                        case NGTCP2_ERR_STREAM_SHUT_WR:
                            if (sid >= 0) nghttp3_conn_shutdown_stream_write(m_http, sid);
                            continue;
                        case NGTCP2_ERR_WRITE_MORE:
                            if (sid >= 0 && nd >= 0) nghttp3_conn_add_write_offset(m_http, sid, static_cast<size_t>(nd));
                            continue;
                        default:
                            set_fatal_locked(knst_network_error::ProtocolError); return false;
                    }
                }
                if (sid >= 0 && nd >= 0) nghttp3_conn_add_write_offset(m_http, sid, static_cast<size_t>(nd));
                if (nw == 0) break;                                  // congestion / pacing limited
                auto r = m_udp.send(m_wbuf.data(), static_cast<size_t>(nw), e);
                if (r == udp_t::r_t::Again) {
                    m_pend.assign(m_wbuf.begin(), m_wbuf.begin() + nw);
                    m_wblocked = true;
                    break;
                }
                if (r == udp_t::r_t::Fail && (!m_hs_done || !knst_h3_detail::soft_net_error(e))) { set_fatal_locked(e); return false; }
                ++cnt;
            }
            ngtcp2_conn_update_pkt_tx_time(m_conn, ts);
            return cnt >= 64;
        KNST_WEB_CATCH(dummy, false)
        set_fatal_locked(knst_network_error::OutOfMemory);
        return false;
    }

    void read_packets(uint64_t ts) noexcept {
        for (int i = 0; i < 64; ++i) {
            size_t n = 0; knst_network_error e = knst_network_error::None;
            auto r = m_udp.recv(m_rbuf.data(), m_rbuf.size(), n, e);
            if (r == udp_t::r_t::Again) return;
            if (r == udp_t::r_t::Fail) {
                if (m_hs_done && knst_h3_detail::soft_net_error(e)) continue;
                set_fatal_locked(e != knst_network_error::None ? e : knst_network_error::ConnectionClosed); return;
            }
            if (n == 0) continue;
            ngtcp2_pkt_info pi{};
            int rv = ngtcp2_conn_read_pkt(m_conn, &m_path, &pi, m_rbuf.data(), n, ts);
            if (rv == 0) continue;
            if (rv == NGTCP2_ERR_DRAINING || rv == NGTCP2_ERR_CLOSING) { set_fatal_locked(knst_network_error::ConnectionClosed); return; }
            if (rv == NGTCP2_ERR_DROP_CONN) continue;
            if (m_setup_err != knst_network_error::None) { set_fatal_locked(m_setup_err); return; }
            if (!m_hs_done) {
                set_fatal_locked((m_ssl && SSL_get_verify_result(m_ssl) != X509_V_OK)
                                 ? knst_network_error::TlsCertificateInvalid : knst_network_error::TlsHandshakeFailed);
            } else set_fatal_locked(knst_network_error::ProtocolError);
            return;
        }
    }

    // One event-loop iteration (m_mtx held on entry and exit; released only around poll()).
    void step(std::unique_lock<std::mutex>& lk, time_pt limit) noexcept {
        uint64_t ts = knst_h3_detail::now_ns();
        const uint64_t exp = ngtcp2_conn_get_expiry(m_conn);
        if (exp != UINT64_MAX && ts >= exp) {
            int rv = ngtcp2_conn_handle_expiry(m_conn, ts);
            if (rv != 0) {
                set_fatal_locked(rv == NGTCP2_ERR_HANDSHAKE_TIMEOUT ? knst_network_error::Timeout
                                                                    : knst_network_error::ConnectionClosed);
                return;
            }
        }
        time_pt next = scan(knst_web_detail::now_tp());
        const bool more = write_packets(ts);
        if (m_fatal.load()) return;

        if (limit < next) next = limit;
        const uint64_t e2 = ngtcp2_conn_get_expiry(m_conn);
        if (e2 != UINT64_MAX) { time_pt t = knst_h3_detail::tp_from_ns(e2); if (t < next) next = t; }

        bool s_read = more;                                         // more output pending: do not sleep
        if (!more) {
            int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(next - knst_web_detail::now_tp()).count();
            const int64_t cap = m_wake.ok() ? 30000 : 50;
            if (ms < 0) ms = 0;
            if (ms > cap) ms = cap;
            bool sw = false, woke = false;
            knst_network_error pe = knst_network_error::None;
            lk.unlock();
            knst_web_detail::poll_sock_wake(m_udp.handle(), m_wblocked, m_wake.handle(),
                                            static_cast<uint32_t>(ms), s_read, sw, woke, pe);
            if (woke) m_wake.drain();
            lk.lock();
            if (pe != knst_network_error::None) { set_fatal_locked(pe); return; }
        }
        if (s_read) read_packets(knst_h3_detail::now_ns());
        m_cv.notify_all();                                          // streams may have finished
    }

    /* ------------------------------------------------ driver election */
    void run(knst_web_stream& st, std::unique_lock<std::mutex>& lk) noexcept {
        for (;;) {
            if (st.finished()) return;
            if (m_fatal.load()) { st.fail(m_fatal_err); return; }
            if (!m_driving) {
                m_driving = true;
                while (!st.finished() && !m_fatal.load()) step(lk, st.expiry());
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

    // Best-effort flush (RESET_STREAM etc.) when nobody drives. m_mtx held.
    void flush_locked(uint32_t grace_ms) noexcept {
        if (m_driving || m_fatal.load()) return;
        m_driving = true;
        const deadline_t dl = deadline_t::after(grace_ms ? grace_ms : 1);
        for (int i = 0; i < 4 && !m_fatal.load(); ++i) {
            const bool more = write_packets(knst_h3_detail::now_ns());
            if (!m_wblocked && !more) break;
            if (m_wblocked) {
                knst_network_error pe = knst_network_error::None;
                knst_net_detail::poll_wait(m_udp.handle(), false, dl, pe);
                if (pe != knst_network_error::None) break;
            }
        }
        m_driving = false;
        m_cv.notify_all();
    }

    /* ------------------------------------------------------ members */
    udp_t                                               m_udp;
    knst_web_detail::wake_event                         m_wake;
    SSL*                                                m_ssl  = nullptr;
    ngtcp2_conn*                                        m_conn = nullptr;
    nghttp3_conn*                                       m_http = nullptr;
    ngtcp2_crypto_conn_ref                              m_conn_ref{};
    ngtcp2_path                                         m_path{};
    char                                                m_sess_key[320] = {};
    uint32_t                                            m_max_hdr = 65536;

    std::mutex                                          m_mtx;
    std::condition_variable                             m_cv;
    bool                                                m_driving = false;      // guarded by m_mtx
    bool                                                m_hs_done = false;      // guarded by m_mtx
    bool                                                m_wblocked = false;     // driver-only
    std::unordered_map<int64_t, std::shared_ptr<ctx_t>> m_streams;              // guarded by m_mtx
    std::vector<std::shared_ptr<ctx_t>>                 m_pending;              // guarded by m_mtx
    std::vector<uint8_t>                                m_rbuf, m_wbuf, m_pend; // driver-only

    knst_network_error                                  m_fatal_err = knst_network_error::None;
    knst_network_error                                  m_setup_err = knst_network_error::None;
    std::atomic<bool>                                   m_fatal{false}, m_goaway{false}, m_retired{false};
    std::atomic<uint32_t>                               m_reserved{0};
    std::atomic<uint32_t>                               m_peer_max{100};
    std::atomic<uint64_t>                               m_requests{0};
    std::atomic<int64_t>                                m_idle_ns{0};
};

/* ===== PART 2 (knst_http3_client, usage, test plan) CONTINUES HERE ===== */

/* ============================================================================
 * knst_http3_client — public HTTP/3 client
 * ============================================================================ */

class knst_http3_client : public knst_web_client_base<knst_http3_client> {
public:
    knst_http3_client() noexcept = default;

    void     clear_connection_pool() noexcept { m_pool.clear(); }
    uint32_t idle_sessions() const noexcept   { return m_pool.size(); }   // live pooled QUIC connections

private:
    friend class knst_web_client_base<knst_http3_client>;

    static std::string make_key(const knst_url& u, const knst_web_options& o) {
        std::string k = "h3|";
        k += o.allow_private_networks ? "O|" : "S|";                      // guarded and unguarded never share
        k += knst_web_detail::c16_utf8(u.host); k += ':'; k += std::to_string(u.effective_port());
        if (!o.bind_local_ip.empty()) { k += "|b:"; k += knst_web_detail::c16_utf8(o.bind_local_ip); }
        return k;
    }

    bool exchange(const knst_url& u, const knst_web_detail::proxy_cfg& pc, const knst_web_options& o,
                  const knst_web_request& r, knst_http_response& out,
                  const knst_web_detail::deadline_t& total, knst_network_error& err) noexcept
    {
        KNST_WEB_GUARD_BEGIN
        if (pc.enabled()) { err = knst_network_error::NotSupported; return false; }   // needs CONNECT-UDP
        const bool idem = knst_web_detail::is_idempotent_method(r.method);
        const std::string key = make_key(u, o);
        for (int attempt = 0; attempt < 3; ++attempt) {
            std::shared_ptr<knst_h3_session> s = m_pool.acquire(
                key, o.max_concurrent_streams ? o.max_concurrent_streams : 100,
                o.max_sessions_per_origin, o.keepalive_idle_ms, total,
                [&](knst_network_error& e) { return knst_h3_session::create(u, pc, o, total, e); }, err);
            if (!s) return false;
            const bool reused = s->requests_started() > 0;
            out = knst_http_response{};
            knst_web_stream st;
            const bool ok = s->execute(r, o, total, out, st, err);
            if (!o.use_keepalive) s->retire();
            if (ok) return true;
            // H3_REQUEST_REJECTED / GOAWAY guarantee "not processed": retry any method.
            // A stale reused connection that died before the first response byte: idempotent only.
            const bool safe = st.refused || (!st.response_started && reused && idem && knst_web_detail::is_conn_error(err));
            if (safe && attempt < 2) { err = knst_network_error::None; continue; }
            return false;
        }
        if (err == knst_network_error::None) err = knst_network_error::Unknown;
        return false;
        KNST_WEB_GUARD_END(err)
    }

    knst_web_session_pool<knst_h3_session> m_pool;
};


/* ============================================================================
 * knst_http_auto_client — h3 -> h2 -> h1 with per-origin fallback memory
 *
 *  - http:// URLs and options::use_chunked_upload go straight to HTTP/1.1.
 *  - With a proxy configured (proxy_url / use_env_proxy) h3 is skipped.
 *  - A failed h3 origin is not retried for backoff_ms (default 5 min), so a
 *    network that drops UDP/443 costs ONE timeout, not one per request.
 *  - Fallback never replays a request that may already have reached the server:
 *    non-idempotent methods and streaming (on_body) requests fall back only on
 *    connect-phase errors (refused / unreachable / handshake / not supported).
 *  - Each protocol keeps its OWN cookie jar and connection pool.
 *    Use h1() / h2() / h3() to reach the underlying clients.
 * ============================================================================ */

class knst_http_auto_client {
public:
    typedef knst_web_options options;
    typedef knst_http_client::multipart_part multipart_part;

    knst_http_auto_client() = default;
    knst_http_auto_client(const knst_http_auto_client&) = delete;
    knst_http_auto_client& operator=(const knst_http_auto_client&) = delete;

    void enable_h3(bool v) noexcept        { m_h3 = v; }
    void enable_h2(bool v) noexcept        { m_h2 = v; }
    void set_h3_backoff(uint32_t ms) noexcept { m_backoff_ms = ms; }

    knst_http_client&  h1() noexcept { return m_c1; }
    knst_http2_client& h2() noexcept { return m_c2; }
    knst_http3_client& h3() noexcept { return m_c3; }

    bool get(const knst_c16string& url, const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return run(u"GET", url, nullptr, o, out, err);
    }
    bool post(const knst_c16string& url, const knst_byte_string& body, const options& o,
              knst_http_response& out, knst_network_error& err) noexcept {
        return run(u"POST", url, &body, o, out, err);
    }
    bool put(const knst_c16string& url, const knst_byte_string& body, const options& o,
             knst_http_response& out, knst_network_error& err) noexcept {
        return run(u"PUT", url, &body, o, out, err);
    }
    bool del(const knst_c16string& url, const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return run(u"DELETE", url, nullptr, o, out, err);
    }
    bool head(const knst_c16string& url, const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return run(u"HEAD", url, nullptr, o, out, err);
    }
    bool request(const knst_c16string& method, const knst_c16string& url, const knst_byte_string& body,
                 const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        return run(method, url, &body, o, out, err);
    }

    void clear_cookies() { m_c1.clear_cookies(); m_c2.clear_cookies(); m_c3.clear_cookies(); }
    void clear_connection_pool() noexcept { m_c1.clear_connection_pool(); m_c2.clear_connection_pool(); m_c3.clear_connection_pool(); }

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

private:
    // Errors that prove the request never reached the server application.
    static bool connect_phase(knst_network_error e) noexcept {
        return e == knst_network_error::ConnectionRefused || e == knst_network_error::HostUnreachable ||
               e == knst_network_error::NetworkUnreachable || e == knst_network_error::NetworkDown ||
               e == knst_network_error::TlsHandshakeFailed || e == knst_network_error::NotSupported ||
               e == knst_network_error::TlsNotSupported;
    }
    // Errors worth a fallback for requests that are safe to replay.
    static bool replayable(knst_network_error e) noexcept {
        return connect_phase(e) || e == knst_network_error::Timeout || e == knst_network_error::ConnectionClosed ||
               e == knst_network_error::HttpConnectionClosed || e == knst_network_error::ConnectionReset ||
               e == knst_network_error::ConnectionAborted || e == knst_network_error::ProtocolError ||
               e == knst_network_error::BrokenPipe;
    }

    bool recently_failed(const std::string& key) {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_bad.find(key);
        if (it == m_bad.end()) return false;
        if (knst_web_detail::now_tp() >= it->second) { m_bad.erase(it); return false; }
        return true;
    }
    void mark_failed(const std::string& key) {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_bad.size() >= 512) m_bad.clear();
        m_bad[key] = knst_web_detail::now_tp() + std::chrono::milliseconds(m_backoff_ms);
    }

    bool run(const knst_c16string& method, const knst_c16string& url, const knst_byte_string* body,
             const options& o, knst_http_response& out, knst_network_error& err) noexcept {
        KNST_WEB_GUARD_BEGIN
        static const knst_byte_string kEmpty;
        const knst_byte_string& b = body ? *body : kEmpty;
        err = knst_network_error::None;
        out = knst_http_response{};

        knst_url u; knst_network_error pe = knst_network_error::None;
        const bool https = knst_url::parse(url, u, pe) && u.scheme == u"https";
        if (!https || o.use_chunked_upload) return m_c1.request(method, url, b, o, out, err);   // h1 reports its own errors

        const bool idem = knst_web_detail::is_idempotent_method(knst_web_detail::c16_utf8(method));
        const bool strict = !idem || o.on_body != nullptr;      // replay unsafe => only connect-phase fallback
        const bool proxied = !o.proxy_url.empty() || o.use_env_proxy;
        const std::string key = knst_web_detail::c16_utf8(u.host) + ":" + std::to_string(u.effective_port());

        if (m_h3 && !proxied && !recently_failed(key)) {
            if (m_c3.request(method, url, b, o, out, err)) return true;
            const bool fb = strict ? connect_phase(err) : replayable(err);
            if (!fb) return false;                               // real answer-level error (redirect loop, size limit, ...)
            mark_failed(key);
            out = knst_http_response{}; err = knst_network_error::None;
        }
        if (m_h2) {
            if (m_c2.request(method, url, b, o, out, err)) return true;
            // Only "server cannot do h2" justifies h1; other failures are real.
            const bool fb = (err == knst_network_error::NotSupported || err == knst_network_error::TlsNotSupported);
            if (!fb) return false;
            out = knst_http_response{}; err = knst_network_error::None;
        }
        return m_c1.request(method, url, b, o, out, err);
        KNST_WEB_GUARD_END(err)
    }

    knst_http_client   m_c1;
    knst_http2_client  m_c2;
    knst_http3_client  m_c3;
    bool               m_h3 = true, m_h2 = true;
    uint32_t           m_backoff_ms = 300000;
    std::mutex         m_mtx;
    std::unordered_map<std::string, knst_web_detail::time_pt> m_bad;
};

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif