/*
  knst_network_basic.cpp

  Basic usage of knst_network - a cross-platform networking library
  with TCP, UDP, Unix sockets, HTTP, and DNS.

  Shows: error enum, IPv4/IPv6 parsing, endpoints, predicates,
  comparison, raw conversion, DNS resolution, TCP/UDP loopback,
  Unix socket IPC, URL parsing, HTTP request/response serialization,
  HTTP client (GET/POST/redirect/basic-auth/cookies), multipart
  form-data, TLS stub, and async composition via thread pool.
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>



static void section(const char* title) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "--------------------------------------------------\n";
}

static const char* platform_name() {
#if KNST_USING_PLATFORM_WINDOWS
    return "Windows (Winsock2)";
#else
    return "POSIX (BSD sockets)";
#endif
}

// ASCII-safe UTF-16 printer (non-ASCII → '?')
static void print_c16(const knst_c16string& s) {
    for (uint32_t i = 0; i < s.length(); ++i) {
        char16_t c = s[i];
        std::cout << (c < 128 ? static_cast<char>(c) : '?');
    }
}


int main() {

    std::cout << "Platform : " << platform_name() << "\n";
    std::cout << "sizeof(knst_ip_address) : " << sizeof(knst_ip_address) << " bytes\n";
    std::cout << "sizeof(knst_endpoint)   : " << sizeof(knst_endpoint)   << " bytes\n";

    /* =====================================================================
     * 1) knst_network_error — the unified error enumeration
     * ===================================================================== */
    section("1) knst_network_error");
    {
        knst_network_error es[] = {
            knst_network_error::None,
            knst_network_error::WouldBlock,
            knst_network_error::ConnectionRefused,
            knst_network_error::Timeout,
            knst_network_error::DnsHostNotFound,
            knst_network_error::HttpMalformedStatus,
            knst_network_error::TlsNotSupported,
            knst_network_error::Unknown,
        };
        for (auto e : es) {
            std::cout << "  [" << (unsigned)e << "] "
                      << knst_network_error_string(e) << "\n";
        }
    }

    /* =====================================================================
     * 2) IPv4 — construction and parsing
     * ===================================================================== */
    section("2) IPv4");
    {
        auto a = knst_ip_address::from_ipv4(192, 168, 1, 100);
        std::cout << "from_ipv4(192,168,1,100) : "; print_c16(a.to_string()); std::cout << "\n";

        knst_network_error err;
        auto b = knst_ip_address::from_string(u"10.0.0.1", err);
        std::cout << "from_string(\"10.0.0.1\")  : "; print_c16(b.to_string());
        std::cout << "  err=" << knst_network_error_string(err) << "\n";

        auto c = knst_ip_address::from_string(u"not.an.ip", err);
        std::cout << "from_string(\"not.an.ip\")  : err="
                  << knst_network_error_string(err)
                  << "  valid=" << (c.is_valid() ? "yes" : "no") << "\n";
    }

    /* =====================================================================
     * 3) IPv6 — construction and parsing
     * ===================================================================== */
    section("3) IPv6");
    {
        knst_network_error err;
        auto a = knst_ip_address::from_string(u"2001:db8::1", err);
        std::cout << "from_string(\"2001:db8::1\") : "; print_c16(a.to_string()); std::cout << "\n";

        auto b = knst_ip_address::from_string(u"::1", err);
        std::cout << "from_string(\"::1\")         : "; print_c16(b.to_string()); std::cout << "\n";
    }

    /* =====================================================================
     * 4) Predicates — loopback / any / multicast
     * ===================================================================== */
    section("4) Predicates");
    {
        auto print_row = [](const knst_ip_address& ip, const char* label) {
            std::cout << "  " << label
                      << "  loopback=" << (ip.is_loopback()  ? "yes" : "no ")
                      << "  any="      << (ip.is_any()       ? "yes" : "no ")
                      << "  mc="       << (ip.is_multicast() ? "yes" : "no ") << "\n";
        };
        print_row(knst_ip_address::loopback_v4(), "127.0.0.1");
        print_row(knst_ip_address::any_v4(),      "0.0.0.0  ");
        print_row(knst_ip_address::from_ipv4(224, 0, 0, 1), "224.0.0.1");
        print_row(knst_ip_address::loopback_v6(), "::1      ");
        print_row(knst_ip_address::any_v6(),      "::       ");
    }

    /* =====================================================================
     * 5) Comparison — ==, !=
     * ===================================================================== */
    section("5) Comparison");
    {
        auto a = knst_ip_address::from_ipv4(10, 20, 30, 40);
        auto b = knst_ip_address::from_ipv4(10, 20, 30, 40);
        auto c = knst_ip_address::from_ipv4(10, 20, 30, 41);
        std::cout << "a == b : " << (a == b ? "true" : "false") << "\n";
        std::cout << "a == c : " << (a == c ? "true" : "false") << "\n";
    }

    /* =====================================================================
     * 6) Raw conversion — to_raw_ipv4 / to_raw_ipv6
     * ===================================================================== */
    section("6) Raw conversion");
    {
        auto v4 = knst_ip_address::from_ipv4(203, 0, 113, 5);
        uint8_t raw4[4] = {};
        v4.to_raw_ipv4(raw4);
        std::cout << "IPv4 raw : "
                  << (unsigned)raw4[0] << "." << (unsigned)raw4[1] << "."
                  << (unsigned)raw4[2] << "." << (unsigned)raw4[3] << "\n";
    }

    /* =====================================================================
     * 7) from_raw — construct from native sockaddr
     * ===================================================================== */
    section("7) from_raw");
    {
        auto original = knst_ip_address::from_ipv4(172, 16, 254, 1);
        auto copy = knst_ip_address::from_raw(
            reinterpret_cast<const sockaddr*>(&original.native()));
        std::cout << "original : "; print_c16(original.to_string()); std::cout << "\n";
        std::cout << "copy     : "; print_c16(copy.to_string());     std::cout << "\n";
        std::cout << "equal    : " << (original == copy ? "true" : "false") << "\n";
    }

    /* =====================================================================
     * 8) to_string — canonical textual form
     * ===================================================================== */
    section("8) to_string");
    {
        const char16_t* samples[] = { u"192.168.0.1", u"::1", u"2001:db8::1", u"ff02::1" };
        knst_network_error err;
        for (auto s : samples) {
            knst_c16string in(s);
            auto ip = knst_ip_address::from_string(in, err);
            std::cout << "  "; print_c16(in); std::cout << "  ->  ";
            print_c16(ip.to_string()); std::cout << "\n";
        }
    }

    /* =====================================================================
     * 9) Endpoint — construction and to_string
     * ===================================================================== */
    section("9) Endpoint constructors");
    {
        knst_endpoint ep = knst_endpoint::from_ipv4(192, 168, 1, 100, 8080);
        std::cout << "  "; print_c16(ep.to_string()); std::cout << "\n";

        knst_endpoint ep2(knst_ip_address::loopback_v6(), 8443);
        std::cout << "  "; print_c16(ep2.to_string()); std::cout << "\n";
    }

    /* =====================================================================
     * 10) Endpoint parse — success cases
     * ===================================================================== */
    section("10) Endpoint parse (success)");
    {
        knst_network_error err;
        const char16_t* cases[] = {
            u"1.2.3.4:80", u"[::1]:8080", u"[2001:db8::1]:443", u"1.2.3.4",
        };
        for (auto s : cases) {
            knst_c16string in(s);
            knst_endpoint ep = knst_endpoint::parse(in, err);
            std::cout << "  "; print_c16(in); std::cout << "  ->  ";
            print_c16(ep.to_string());
            std::cout << "  valid=" << (ep.is_valid() ? "yes" : "no")
                      << "  err=" << knst_network_error_string(err) << "\n";
        }
    }

    /* =====================================================================
     * 11) Endpoint parse — error cases
     * ===================================================================== */
    section("11) Endpoint parse (errors)");
    {
        knst_network_error err;
        const char16_t* cases[] = { u"", u"not.an.ip:80", u"1.2.3.4:99999", u"[::1" };
        for (auto s : cases) {
            knst_c16string in(s);
            knst_endpoint ep = knst_endpoint::parse(in, err);
            std::cout << "  valid=" << (ep.is_valid() ? "yes" : "no")
                      << "  err=" << knst_network_error_string(err) << "\n";
        }
    }

    /* =====================================================================
     * 12) Endpoint comparison
     * ===================================================================== */
    section("12) Endpoint comparison");
    {
        knst_endpoint a = knst_endpoint::from_ipv4(10, 0, 0, 1, 80);
        knst_endpoint b = knst_endpoint::from_ipv4(10, 0, 0, 1, 80);
        knst_endpoint c = knst_endpoint::from_ipv4(10, 0, 0, 1, 81);
        std::cout << "  a == b : " << (a == b ? "true" : "false") << "\n";
        std::cout << "  a != c : " << (a != c ? "true" : "false") << "\n";
    }

    /* =====================================================================
     * 13) DNS — resolve_first(localhost)
     * ===================================================================== */
    section("13) DNS resolve_first(localhost)");
    {
        knst_network_error err;
        knst_endpoint ep;
        bool ok = knst_dns_resolver::resolve_first(u"localhost", 80, ep, err);
        std::cout << "  ok  : " << (ok ? "true" : "false") << "\n";
        if (ok) { std::cout << "  ep  : "; print_c16(ep.to_string()); std::cout << "\n"; }
    }

    /* =====================================================================
     * 14) DNS — resolve(localhost), all results
     * ===================================================================== */
    section("14) DNS resolve(localhost) - all");
    {
        knst_network_error err;
        knst_vector<knst_endpoint> list;
        knst_dns_resolver::resolve(u"localhost", 80, list, err);
        std::cout << "  count : " << list.size() << "\n";
        for (uint32_t i = 0; i < list.size(); ++i) {
            std::cout << "    [" << i << "] "; print_c16(list[i].to_string()); std::cout << "\n";
        }
    }

    /* =====================================================================
     * 15) DNS — resolve(localhost), IPv4 only
     * ===================================================================== */
    section("15) DNS resolve(localhost) - IPv4 only");
    {
        knst_network_error err;
        knst_vector<knst_endpoint> list;
        knst_dns_resolver::resolve(u"localhost", 80, list, err, AF_INET);
        std::cout << "  count : " << list.size() << "\n";
        for (uint32_t i = 0; i < list.size(); ++i) {
            std::cout << "    [" << i << "] "; print_c16(list[i].to_string()); std::cout << "\n";
        }
    }

    /* =====================================================================
     * 16) DNS — resolve(localhost), IPv6 only
     * ===================================================================== */
    section("16) DNS resolve(localhost) - IPv6 only");
    {
        knst_network_error err;
        knst_vector<knst_endpoint> list;
        knst_dns_resolver::resolve(u"localhost", 80, list, err, AF_INET6);
        std::cout << "  count : " << list.size() << "\n";
    }

    /* =====================================================================
     * 17) DNS — host not found
     * ===================================================================== */
    section("17) DNS - host not found");
    {
        knst_network_error err;
        knst_vector<knst_endpoint> list;
        knst_dns_resolver::resolve(u"nonexistent.invalid", 80, list, err);
        std::cout << "  err : " << knst_network_error_string(err) << "\n";
    }

    /* =====================================================================
     * 18) DNS — reverse lookup
     * ===================================================================== */
    section("18) DNS reverse lookup");
    {
        knst_network_error err;
        knst_c16string name;
        if (knst_dns_resolver::reverse(knst_ip_address::loopback_v4(), name, err)) {
            std::cout << "  127.0.0.1 -> "; print_c16(name); std::cout << "\n";
        }
    }

    /* =====================================================================
     * 19) TCP loopback — round-trip
     * ===================================================================== */
    section("19) TCP loopback round-trip");
    {
        knst_tcp_server server;
        knst_network_error err;
        knst_endpoint local = knst_endpoint::from_ipv4(127, 0, 0, 1, 1);
        local.port = 0;

        if (!server.listen(local, 4, err)) {
            std::cout << "  listen failed: " << knst_network_error_string(err) << "\n";
        } else {
            knst_endpoint bound = server.local_endpoint();
            std::cout << "  listening on "; print_c16(bound.to_string()); std::cout << "\n";

            knst_tcp_socket client;
            if (client.connect(bound, 1000, err)) {
                client.send_all("hello", 5, 1000, err);

                knst_tcp_socket peer;
                knst_endpoint peer_ep;
                if (server.accept(peer, peer_ep, 1000, err)) {
                    std::cout << "  accepted from "; print_c16(peer_ep.to_string()); std::cout << "\n";

                    knst_byte_string rx;
                    if (peer.recv(rx, 1000, err))
                        std::cout << "  server got " << rx.length() << " bytes\n";
                    peer.send_all("pong", 4, 1000, err);
                }

                knst_byte_string pong;
                if (client.recv(pong, 1000, err))
                    std::cout << "  client got " << pong.length() << " bytes\n";
            }
        }
    }

    /* =====================================================================
     * 20) TCP server — 3 sequential clients
     * ===================================================================== */
    section("20) TCP server - 3 sequential clients");
    {
        knst_tcp_server server;
        knst_network_error err;
        knst_endpoint local = knst_endpoint::from_ipv4(127, 0, 0, 1, 1); local.port = 0;
        server.listen(local, 4, err);
        knst_endpoint bound = server.local_endpoint();

        for (int i = 0; i < 3; ++i) {
            knst_tcp_socket client;
            if (client.connect(bound, 1000, err)) {
                char msg[32];
                int n = ::snprintf(msg, sizeof(msg), "msg-%d", i);
                client.send_all(msg, static_cast<size_t>(n), 1000, err);

                knst_tcp_socket peer;
                if (server.accept(peer, 1000, err)) {
                    knst_byte_string rx;
                    if (peer.recv(rx, 1000, err))
                        std::cout << "  round " << i << " got " << rx.length() << " bytes\n";
                }
            }
        }
    }

    /* =====================================================================
     * 21) UDP loopback — two sockets, one datagram
     * ===================================================================== */
    section("21) UDP loopback");
    {
        knst_network_error err;
        knst_udp_socket a, b;
        knst_endpoint ea = knst_endpoint::from_ipv4(127, 0, 0, 1, 1); ea.port = 0;
        knst_endpoint eb = knst_endpoint::from_ipv4(127, 0, 0, 1, 1); eb.port = 0;

        if (a.bind(ea, err) && b.bind(eb, err)) {
            knst_endpoint a_addr = a.local_endpoint();
            knst_endpoint b_addr = b.local_endpoint();
            std::cout << "  A: "; print_c16(a_addr.to_string()); std::cout << "\n";
            std::cout << "  B: "; print_c16(b_addr.to_string()); std::cout << "\n";

            size_t sent = 0;
            if (a.send_to("ping", 4, b_addr, sent, 1000, err))
                std::cout << "  A -> B sent " << sent << " bytes\n";

            knst_byte_string rx; knst_endpoint from;
            if (b.recv_from(rx, from, 1000, err)) {
                std::cout << "  B received " << rx.length() << " bytes from ";
                print_c16(from.to_string()); std::cout << "\n";
            }
        }
    }

    /* =====================================================================
     * 22) UDP options — broadcast / multicast TTL / join
     * ===================================================================== */
    section("22) UDP broadcast / multicast");
    {
        knst_udp_socket s;
        knst_network_error err;
        knst_endpoint local = knst_endpoint::from_ipv4(0, 0, 0, 0, 1); local.port = 0;

        if (s.bind(local, err)) {
            bool ok1 = s.set_broadcast(true);
            bool ok2 = s.set_multicast_ttl(4);
            bool ok3 = s.join_multicast(knst_ip_address::from_ipv4(224, 0, 0, 251));
            std::cout << "  set_broadcast       : " << (ok1 ? "ok" : "fail") << "\n";
            std::cout << "  set_multicast_ttl   : " << (ok2 ? "ok" : "fail") << "\n";
            std::cout << "  join_multicast(224) : " << (ok3 ? "ok" : "fail") << "\n";
        }
    }

    /* =====================================================================
     * 23) Unix domain socket — AF_UNIX round-trip
     * ===================================================================== */
    section("23) Unix domain socket");
    {
    #if !KNST_USING_PLATFORM_WINDOWS || KNST_NETWORK_HAS_AF_UNIX_WIN
        knst_network_error err;
        knst_c16string path = knst_file::join_path(
            knst_file::get_temp_dir(), u"knst_unix_test.sock");

        knst_unix_server server;
        if (server.listen(path, 4, err)) {
            std::cout << "  server listening\n";
            knst_unix_socket client;
            if (client.connect(path, 1000, err)) {
                client.send_all("unix-hello", 10, 1000, err);
                knst_unix_socket peer;
                if (server.accept(peer, 1000, err)) {
                    knst_byte_string rx;
                    if (peer.recv(rx, 1000, err))
                        std::cout << "  server got " << rx.length() << " bytes\n";
                }
            }
        } else {
            std::cout << "  listen failed: " << knst_network_error_string(err) << "\n";
        }
        server.close();
    #else
        std::cout << "  AF_UNIX not available\n";
    #endif
    }

    /* =====================================================================
     * 24) URL parse — basic http
     * ===================================================================== */
    section("24) URL parse - http");
    {
        knst_url u; knst_network_error err;
        if (knst_url::parse(u"http://example.com/index.html", u, err)) {
            std::cout << "  scheme : "; print_c16(u.scheme); std::cout << "\n";
            std::cout << "  host   : "; print_c16(u.host);   std::cout << "\n";
            std::cout << "  port   : " << u.effective_port() << "\n";
            std::cout << "  path   : "; print_c16(u.path);   std::cout << "\n";
        }
    }

    /* =====================================================================
     * 25) URL parse — full: userinfo, port, path, query, fragment
     * ===================================================================== */
    section("25) URL parse - full");
    {
        knst_url u; knst_network_error err;
        if (knst_url::parse(
                u"https://user:pass@example.com:8443/api/v1?x=1&y=2#frag", u, err)) {
            std::cout << "  scheme   : "; print_c16(u.scheme);   std::cout << "\n";
            std::cout << "  userinfo : "; print_c16(u.userinfo); std::cout << "\n";
            std::cout << "  host     : "; print_c16(u.host);     std::cout << "\n";
            std::cout << "  port     : " << u.port << "\n";
            std::cout << "  path     : "; print_c16(u.path);     std::cout << "\n";
            std::cout << "  query    : "; print_c16(u.query);    std::cout << "\n";
            std::cout << "  fragment : "; print_c16(u.fragment); std::cout << "\n";
        }
    }

    /* =====================================================================
     * 26) URL parse — IPv6 host
     * ===================================================================== */
    section("26) URL parse - IPv6 host");
    {
        knst_url u; knst_network_error err;
        if (knst_url::parse(u"http://[::1]:8080/path", u, err)) {
            std::cout << "  host : "; print_c16(u.host); std::cout << "\n";
            std::cout << "  port : " << u.port << "\n";
        }
    }

    /* =====================================================================
     * 27) URL to_string — round-trip
     * ===================================================================== */
    section("27) URL to_string round-trip");
    {
        knst_url u; knst_network_error err;
        if (knst_url::parse(u"https://example.com/a/b?q=1#f", u, err)) {
            std::cout << "  "; print_c16(u.to_string()); std::cout << "\n";
        }
    }

    /* =====================================================================
     * 28) HTTP request — serialization to raw bytes
     * ===================================================================== */
    section("28) HTTP request serialization");
    {
        knst_http_request req;
        req.method = u"POST";
        req.path   = u"/api/echo";
                // NOTE: set_header() rejects Host/Content-Length/etc. (library-managed).
        // Real requests get these injected automatically by do_request().
        req.set_header_internal(u"Host", u"example.com");
        req.set_header(u"Content-Type", u"text/plain");
        req.set_header_internal(u"Content-Length", u"5");

        req.body.resize(5);
        const char* hello = "hello";
        for (int i = 0; i < 5; ++i) req.body[i] = static_cast<uint8_t>(hello[i]);

        knst_byte_string raw = req.serialize();
        std::cout << "  bytes: " << raw.length() << "\n";
        std::cout << "  --- raw ---\n";
        for (uint32_t i = 0; i < raw.length(); ++i) std::cout << (char)raw[i];
        std::cout << "  --- end ---\n";
    }

    /* =====================================================================
     * 29) HTTP response — parse raw bytes
     * ===================================================================== */
    section("29) HTTP response parse (raw bytes)");
    {
        const char* raw_text =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "hello world";

        knst_byte_string raw;
        for (const char* p = raw_text; *p; ++p) raw.push_back(static_cast<uint8_t>(*p));

        knst_http_response resp; knst_network_error err;
        if (knst_http_response::parse(raw, resp, err)) {
            std::cout << "  status : " << resp.status << "\n";
            std::cout << "  reason : "; print_c16(resp.reason); std::cout << "\n";
            std::cout << "  body   : ";
            for (uint32_t i = 0; i < resp.body.length(); ++i) std::cout << (char)resp.body[i];
            std::cout << "\n";
            std::cout << "  ct     : "; print_c16(resp.get_header(u"Content-Type")); std::cout << "\n";
        }
    }

    /* =====================================================================
     * 30) HTTP client — GET (network required)
     * ===================================================================== */
    section("30) HTTP client GET (network required)");
    {
        knst_http_client client;
        knst_http_client::options opts;
        opts.timeout_ms = 5000;

        knst_http_response resp;
        knst_network_error err;
        bool ok = client.get(u"http://example.com/", opts, resp, err);

        std::cout << "  ok     : " << (ok ? "true" : "false") << "\n";
        if (ok) {
            std::cout << "  status : " << resp.status << "\n";
            std::cout << "  body   : " << resp.body.length() << " bytes\n";
        } else {
            std::cout << "  err    : " << knst_network_error_string(err)
                      << " (offline env is fine)\n";
        }
    }

    /* =====================================================================
     * 31) HTTP client — redirect following (network required)
     * ===================================================================== */
    section("31) HTTP client redirect (network required)");
    {
        knst_http_client client;
        knst_http_client::options opts;
        opts.timeout_ms = 5000;
        opts.follow_redirects = true;
        opts.max_redirects = 5;

        knst_http_response resp;
        knst_network_error err;
        bool ok = client.get(u"http://httpbin.org/redirect/1", opts, resp, err);

        std::cout << "  ok     : " << (ok ? "true" : "false") << "\n";
        if (ok) std::cout << "  status : " << resp.status << "\n";
        else    std::cout << "  err    : " << knst_network_error_string(err) << "\n";
    }

    /* =====================================================================
     * 32) HTTP client — POST (network required)
     * ===================================================================== */
    section("32) HTTP client POST (network required)");
    {
        knst_http_client client;
        knst_http_client::options opts;
        opts.timeout_ms = 5000;

        knst_byte_string body;
        for (const char* p = "hello=world"; *p; ++p) body.push_back(static_cast<uint8_t>(*p));

        knst_http_response resp; knst_network_error err;
        bool ok = client.post(u"http://httpbin.org/post", body, opts, resp, err);

        std::cout << "  ok     : " << (ok ? "true" : "false") << "\n";
        if (ok) std::cout << "  status : " << resp.status << "\n";
        else    std::cout << "  err    : " << knst_network_error_string(err) << "\n";
    }

    /* =====================================================================
     * 33) HTTP client — Basic authentication (network required)
     * ===================================================================== */
    section("33) HTTP client Basic auth (network required)");
    {
        knst_http_client client;
        knst_http_client::options opts;
        opts.timeout_ms = 5000;
        opts.basic_user = u"user";
        opts.basic_pass = u"pass";

        knst_http_response resp; knst_network_error err;
        bool ok = client.get(u"http://httpbin.org/basic-auth/user/pass", opts, resp, err);

        std::cout << "  ok     : " << (ok ? "true" : "false") << "\n";
        if (ok) std::cout << "  status : " << resp.status << "\n";
        else    std::cout << "  err    : " << knst_network_error_string(err) << "\n";
    }

    /* =====================================================================
     * 34) HTTP client — cookies (network required)
     * ===================================================================== */
    section("34) HTTP client cookies (network required)");
    {
        knst_http_client client;
        knst_http_client::options opts;
        opts.timeout_ms = 5000;
        opts.auto_cookie = true;

        knst_http_response resp;
        knst_network_error err;

        client.get(u"http://httpbin.org/cookies/set?foo=bar", opts, resp, err);
        std::cout << "  cookie jar size: " << client.cookies().size() << "\n";

        client.get(u"http://httpbin.org/cookies", opts, resp, err);
        std::cout << "  second response status: " << resp.status << "\n";
    }

    /* =====================================================================
     * 35) Multipart form-data builder
     * ===================================================================== */
    section("35) Multipart form-data");
    {
        knst_vector<knst_http_client::multipart_part> parts;

        knst_http_client::multipart_part p1;
        p1.name = u"field1";
        for (const char* p = "value1"; *p; ++p)
            p1.data.push_back(static_cast<uint8_t>(*p));
        parts.push_back(p1);

        knst_http_client::multipart_part p2;
        p2.name = u"file";
        p2.filename = u"test.txt";
        p2.content_type = u"text/plain";
        for (const char* p = "file content"; *p; ++p)
            p2.data.push_back(static_cast<uint8_t>(*p));
        parts.push_back(p2);

        knst_byte_string body; knst_c16string boundary;
        knst_http_client::build_multipart(parts, body, boundary);

        std::cout << "  boundary : "; print_c16(boundary); std::cout << "\n";
        std::cout << "  body size: " << body.length() << " bytes\n";
    }

    /* =====================================================================
     * 36) TLS status — opt-in via KNST_NETWORK_ENABLE_TLS
     * ===================================================================== */
    section("36) ssl_socket stub");
    {
    #ifdef KNST_NETWORK_ENABLE_TLS
        std::cout << "  TLS enabled - knst_ssl_socket available\n";
    #else
        std::cout << "  " << knst_network_error_string(knst_network_error::TlsNotSupported) << "\n";
    #endif
    }

    /* =====================================================================
     * 37) Async composition — via knst_thread_pool
     * ===================================================================== */
    section("37) Async via knst_thread_pool");
    {
        knst_thread_pool pool;
        pool.start();

        std::atomic<int> done{0};
        for (int i = 0; i < 4; ++i) {
            pool.submit([&done, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                done.fetch_add(1);
                std::cout << "  job " << i << " done\n";
            });
        }

        pool.wait_all();
        pool.shutdown();
        std::cout << "  total done: " << done.load() << "\n";
    }

    /* =====================================================================
     * 38) Platform summary
     * ===================================================================== */
    section("38) Platform summary");
    {
    #if KNST_USING_PLATFORM_WINDOWS
        std::cout << "  Platform : Windows (Winsock2)\n";
    #else
        std::cout << "  Platform : POSIX (BSD sockets)\n";
    #endif
        std::cout << "  Async    : knst_thread / knst_thread_pool (user-controlled)\n";
        std::cout << "  Allocator: knst_pool_allocator ready for HTTP bodies\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}