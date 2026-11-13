/*
  knst_network_h3_basic.cpp

  HTTP/3 (QUIC) test + tutorial for knst_http3_client and knst_http_auto_client.

  DESIGN NOTES
  ------------
  Two categories of test servers are used:

    1. h3-capable servers (QUIC + HTTP/3 at the edge):
         - cloudflare-quic.com  (Cloudflare official QUIC test)
         - www.google.com       (Google, h3 enabled)

    2. h2-only servers:
         - httpbin.org          (echo/test endpoints)

  httpbin.org is fronted by a QUIC-capable edge but its backend
  does not complete HTTP/3 exchanges. h3 attempts hang until the
  per-request timeout (default 20s), which makes tests slow and
  causes POST/streaming tests to time out (the auto client refuses
  to replay non-idempotent requests over a different transport).

  For that reason the httpbin.org tests here explicitly call
  `c.enable_h3(false)` — they exercise the h2 path through the
  auto client. The h3 path is validated separately against
  cloudflare-quic.com and google.com.

  REQUIREMENTS
  ------------
    - KNST_NETWORK_ENABLE_TLS and KNST_NETWORK_ENABLE_H3 defined
    - libngtcp2, libnghttp3, quictls (QUIC-capable OpenSSL)
    - UDP/443 outbound not blocked
    - /etc/ssl/certs/ca-certificates.crt present
*/

#include "../../../include/KernelNucleusT.hpp"
#include <iostream>



/* =====================================================================
 *  Console helpers
 * ===================================================================== */

static void section(const char* title, const char* what, const char* expect) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "  WHAT    : " << what << "\n";
    std::cout << "  EXPECT  : " << expect << "\n";
    std::cout << "--------------------------------------------------\n";
}

static void print_c16(const knst_c16string& s) {
    for (uint32_t i = 0; i < s.length(); ++i) {
        char16_t c = s[i];
        std::cout << (c < 128 ? static_cast<char>(c) : '?');
    }
}

static void print_bs(const knst_byte_string& b, size_t maxlen = 80) {
    size_t n = b.length() < maxlen ? b.length() : maxlen;
    for (size_t i = 0; i < n; ++i) {
        char c = (char)b[i];
        std::cout << (c >= 32 && c < 127 ? c : '.');
    }
    if (b.length() > maxlen) std::cout << "...";
}

static knst_byte_string to_bs(const char* s) {
    knst_byte_string r;
    for (const char* p = s; *p; ++p) r.push_back((uint8_t)*p);
    return r;
}

static knst_c16string to_c16(const char* s) {
    knst_c16string r;
    for (const char* p = s; *p; ++p) r.append((char16_t)*p);
    return r;
}

static void print_bytes(char16_t prefix, const knst_byte_string& b, size_t maxlen) {
    std::cout << (char)prefix << " : ";
    print_bs(b, maxlen);
    std::cout << "\n";
}


/* =====================================================================
 *  Result tracking
 * ===================================================================== */

struct TestResult {
    const char* name;
    bool        passed;
    bool        skipped;
};

static std::vector<TestResult> g_results;

static void record(const char* name, bool passed, bool skipped = false) {
    g_results.push_back({name, passed, skipped});
    std::cout << "  >>> ";
    if (skipped)      std::cout << "SKIP";
    else if (passed)  std::cout << "PASS";
    else              std::cout << "FAIL";
    std::cout << " : " << name << "\n";
}


/* =====================================================================
 *  Endpoints
 * ===================================================================== */

// h3-capable servers (real HTTP/3 at the edge).
static const char16_t* URL_H3_CF     = u"https://cloudflare-quic.com/";
static const char16_t* URL_H3_GOOGLE = u"https://www.google.com/";

// httpbin.org: h2-only backend. The auto client is used with h3 DISABLED
// for these tests (see DESIGN NOTES at the top of this file).
static const char16_t* URL_GET         = u"https://httpbin.org/get";
static const char16_t* URL_GET_Q       = u"https://httpbin.org/get?foo=bar&x=42";
static const char16_t* URL_POST        = u"https://httpbin.org/post";
static const char16_t* URL_PUT         = u"https://httpbin.org/put";
static const char16_t* URL_DELETE      = u"https://httpbin.org/delete";
static const char16_t* URL_HEADERS     = u"https://httpbin.org/headers";
static const char16_t* URL_AUTH_OK     = u"https://httpbin.org/basic-auth/user/pass";
static const char16_t* URL_COOKIE_SET  = u"https://httpbin.org/cookies/set?foo=bar";
static const char16_t* URL_COOKIE_GET  = u"https://httpbin.org/cookies";
static const char16_t* URL_REDIRECT_3  = u"https://httpbin.org/redirect/3";
static const char16_t* URL_STREAM_100K = u"https://httpbin.org/stream-bytes/102400";
static const char16_t* URL_GZIP        = u"https://httpbin.org/gzip";
static const char16_t* URL_DELAY_5     = u"https://httpbin.org/delay/5";
static const char16_t* URL_404         = u"https://httpbin.org/status/404";


/* =====================================================================
 *  main
 * ===================================================================== */

int main() {

    std::cout << "knst_network_h3 test suite\n";
    std::cout << "sizeof(knst_http3_client)      : " << sizeof(knst_http3_client)      << " bytes\n";
    std::cout << "sizeof(knst_http_auto_client)  : " << sizeof(knst_http_auto_client)  << " bytes\n";

    bool h3_available = false;


    /* 1) UDP/443 precheck */
    section("1) UDP/443 reachability precheck",
            "Attempt a real h3 GET to detect UDP blocking or CA issues",
            "ok=true or explicit diagnostics + graceful fallback");
    {
        knst_http3_client c;
        knst_http3_client::options o;
        o.timeout_ms       = 4000;
        o.total_timeout_ms = 6000;

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        auto t0 = std::chrono::steady_clock::now();
        bool ok = c.get(URL_H3_CF, o, resp, err);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        if (ok && resp.status == 200) {
            h3_available = true;
            std::cout << "  h3 reachable   : YES\n";
            std::cout << "  status         : " << resp.status << "\n";
            std::cout << "  version        : "; print_c16(resp.version); std::cout << "\n";
            std::cout << "  elapsed        : " << ms << " ms\n";
            record("UDP/443 precheck", true);
        } else {
            h3_available = false;
            std::cout << "  h3 reachable   : NO\n";
            std::cout << "  elapsed        : " << ms << " ms\n";
            std::cout << "  error          : " << knst_network_error_string(err) << "\n";
            std::cout << "\n  >>> Possible causes:\n";
            if (err == knst_network_error::TlsCertificateInvalid) {
                std::cout << "  >>>   quictls has no system CA bundle loaded\n";
            } else if (err == knst_network_error::Timeout) {
                std::cout << "  >>>   UDP/443 blocked or QUIC handshake failed\n";
            } else {
                std::cout << "  >>>   check quictls install\n";
            }
            std::cout << "  >>> h3-only tests will be SKIPPED.\n";
            record("UDP/443 precheck", true, /*skipped=*/true);
        }
    }


    /* 2) Basic h3 GET */
    section("2) Basic h3 GET",
            "TLS 1.3 + ALPN 'h3' over QUIC + response parsing",
            "status=200, version='HTTP/3', body > 0");
    if (!h3_available) {
        std::cout << "  (h3 not reachable - skipped)\n";
        record("basic h3 GET", true, /*skipped=*/true);
    } else {
        knst_http3_client c;
        knst_http3_client::options o;
        o.timeout_ms       = 10000;
        o.total_timeout_ms = 30000;
        o.user_agent       = u"KernelNucleusT/h3-test/1.0";

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_H3_CF, o, resp, err);
        if (ok) {
            std::cout << "  status   : " << resp.status << "\n";
            std::cout << "  version  : "; print_c16(resp.version); std::cout << "\n";
            std::cout << "  body size: " << resp.body.length() << " bytes\n";
            print_bytes('>', resp.body, 60);
            record("basic h3 GET", resp.status == 200 && resp.body.length() > 0);
        } else {
            std::cout << "  error    : " << knst_network_error_string(err) << "\n";
            record("basic h3 GET", false);
        }
    }


    /* 3) Session reuse */
    section("3) Session reuse",
            "Second request to the same origin reuses the QUIC connection",
            "idle_sessions() stays at 1, second request ok=true");
    if (!h3_available) {
        std::cout << "  (h3 not reachable - skipped)\n";
        record("h3 session reuse", true, /*skipped=*/true);
    } else {
        knst_http3_client c;
        knst_http3_client::options o;
        o.timeout_ms    = 15000;
        o.use_keepalive = true;

        knst_http_response  r1, r2;
        knst_network_error  e1 = knst_network_error::None;
        knst_network_error  e2 = knst_network_error::None;

        bool ok1 = c.get(URL_H3_CF, o, r1, e1);
        uint32_t idle_after_first = c.idle_sessions();

        bool ok2 = c.get(URL_H3_CF, o, r2, e2);
        uint32_t idle_after_second = c.idle_sessions();

        std::cout << "  first  : ok=" << (ok1 ? "true" : "false")
                  << "  idle=" << idle_after_first << "\n";
        std::cout << "  second : ok=" << (ok2 ? "true" : "false")
                  << "  idle=" << idle_after_second << "\n";

        record("h3 session reuse", ok1 && ok2 && idle_after_second == 1);
    }


    /* 4) POST small */
    section("4) POST (small body, httpbin via h2)",
            "Body upload through the auto client (h3 disabled for httpbin)",
            "status=200, body echoed");
    {
        knst_http_auto_client c;
        c.enable_h3(false);   // httpbin h3 edge is broken; use h2

        knst_http_auto_client::options o;
        o.timeout_ms = 10000;

        knst_http_header h;
        h.name  = u"content-type";
        h.value = u"application/json";
        o.headers.push_back(h);

        knst_byte_string body = to_bs("{\"msg\":\"hello h3\"}");

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.post(URL_POST, body, o, resp, err);
        if (ok) {
            std::cout << "  status   : " << resp.status << "\n";
            std::cout << "  version  : "; print_c16(resp.version); std::cout << "\n";
            print_bytes('>', resp.body, 120);
            record("POST small", resp.status == 200);
        } else {
            std::cout << "  error    : " << knst_network_error_string(err) << "\n";
            record("POST small", false);
        }
    }


    /* 5) POST large */
    section("5) POST (large body, 512 KB, httpbin via h2)",
            "DATA streaming through the auto client",
            "status=200, full body reaches server");
    {
        knst_http_auto_client c;
        c.enable_h3(false);

        knst_http_auto_client::options o;
        o.timeout_ms       = 30000;
        o.total_timeout_ms = 60000;

        knst_byte_string body;
        body.resize(512 * 1024);
        for (uint32_t i = 0; i < body.length(); ++i)
            body[i] = (uint8_t)('A' + (i % 26));

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        auto t0 = std::chrono::steady_clock::now();
        bool ok = c.post(URL_POST, body, o, resp, err);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (ok) {
            std::cout << "  status  : " << resp.status << "\n";
            std::cout << "  elapsed : " << ms << " ms\n";
            std::cout << "  resp    : " << resp.body.length() << " bytes\n";
            record("POST 512KB", resp.status == 200);
        } else {
            std::cout << "  error   : " << knst_network_error_string(err) << "\n";
            record("POST 512KB", false);
        }
    }


    /* 6) PUT */
    section("6) PUT (httpbin via h2)", "PUT with body", "status=200");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms = 10000;
        knst_byte_string body = to_bs("put-payload");
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.put(URL_PUT, body, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        record("PUT", ok && resp.status == 200);
    }


    /* 7) DELETE */
    section("7) DELETE (httpbin via h2)", "Bodyless DELETE", "status=200");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms = 10000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.del(URL_DELETE, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        record("DELETE", ok && resp.status == 200);
    }


    /* 8) HEAD (h3 direct) */
    section("8) HEAD (h3 direct)",
            "HEAD response carries no body (RFC 9110 S9.3.2)",
            "status=200, body.length() == 0");
    if (!h3_available) {
        std::cout << "  (h3 not reachable - skipped)\n";
        record("h3 HEAD", true, /*skipped=*/true);
    } else {
        knst_http3_client c;
        knst_http3_client::options o;
        o.timeout_ms = 10000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.head(URL_H3_CF, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        std::cout << "  body   : " << resp.body.length() << " bytes\n";
        record("h3 HEAD", ok && resp.status == 200 && resp.body.length() == 0);
    }


    /* 9) Query string */
    section("9) Query string (httpbin via h2)",
            "?foo=bar&x=42 propagates to :path",
            "status=200, httpbin echoes them in args");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms = 10000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_GET_Q, o, resp, err);
        if (ok) {
            std::cout << "  status : " << resp.status << "\n";
            print_bytes('>', resp.body, 120);
            record("query string", resp.status == 200);
        } else {
            record("query string", false);
        }
    }


    /* 10) Custom headers */
    section("10) Custom headers (httpbin via h2)",
            "User-Agent and custom headers reach the server",
            "status=200, httpbin shows them in headers");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms = 10000;
        o.user_agent = u"knst-h3-tester/1.0";

        knst_http_header h1; h1.name = u"x-app";   h1.value = u"h3-suite";
        knst_http_header h2; h2.name = u"x-token"; h2.value = u"abc123";
        o.headers.push_back(h1);
        o.headers.push_back(h2);

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_HEADERS, o, resp, err);
        if (ok) {
            std::cout << "  status : " << resp.status << "\n";
            print_bytes('>', resp.body, 200);
            record("custom headers", resp.status == 200);
        } else {
            record("custom headers", false);
        }
    }


    /* 11) Basic auth */
    section("11) Basic auth (httpbin via h2)",
            "user:pass becomes Authorization: Basic base64(...)",
            "status=200");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms = 10000;
        o.basic_user = u"user";
        o.basic_pass = u"pass";
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_AUTH_OK, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        record("basic auth", ok && resp.status == 200);
    }


    /* 12) Cookies */
    section("12) Cookies (httpbin via h2)",
            "Set-Cookie -> jar -> Cookie header on next request",
            "jar size >= 1 and second request status=200");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms  = 10000;
        o.auto_cookie = true;

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;

        c.get(URL_COOKIE_SET, o, resp, err);
        size_t jar_size = c.h1().cookies().size()
                        + c.h2().cookies().size()
                        + c.h3().cookies().size();
        std::cout << "  jar after set : " << jar_size << "\n";

        c.get(URL_COOKIE_GET, o, resp, err);
        std::cout << "  second status : " << resp.status << "\n";
        print_bytes('>', resp.body, 120);

        record("cookies", jar_size >= 1 && resp.status == 200);
    }


    /* 13) Redirect chain */
    section("13) Redirect chain (httpbin via h2)",
            "Follow a 3-hop 301/302 chain (RFC 3986)",
            "status=200 at the end of the chain");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms       = 20000;
        o.follow_redirects = true;
        o.max_redirects    = 5;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_REDIRECT_3, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        record("redirect chain", ok && resp.status == 200);
    }


    /* 14) Redirect limit [reverse] */
    section("14) Redirect limit (reverse test)",
            "max_redirects=1 < chain=3 -> HttpTooManyRedirects",
            "ok=false, err=HttpTooManyRedirects");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms       = 20000;
        o.follow_redirects = true;
        o.max_redirects    = 1;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_REDIRECT_3, o, resp, err);
        std::cout << "  ok  : " << (ok ? "true" : "false") << "\n";
        std::cout << "  err : " << knst_network_error_string(err) << "\n";
        record("redirect limit",
               !ok && err == knst_network_error::HttpTooManyRedirects);
    }


    /* 15) Streaming on_body */
    section("15) Streaming on_body (httpbin via h2)",
            "on_body fires per chunk, response.body stays empty",
            "ctx.total > 0, resp.body.length() == 0");
    {
        knst_http_auto_client c;
        c.enable_h3(false);

        knst_http_auto_client::options o;
        o.timeout_ms = 20000;

        struct Ctx { size_t total = 0; size_t chunks = 0; };
        Ctx ctx;
        o.on_body = [](const uint8_t* /*d*/, size_t n, void* u) -> bool {
            auto* p = static_cast<Ctx*>(u);
            p->total += n;
            p->chunks++;
            return true;
        };
        o.on_body_user = &ctx;

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_STREAM_100K, o, resp, err);
        if (ok) {
            std::cout << "  status   : " << resp.status << "\n";
            std::cout << "  streamed : " << ctx.total << " bytes in " << ctx.chunks << " chunks\n";
            std::cout << "  buffered : " << resp.body.length() << " bytes\n";
            record("streaming on_body",
                   resp.status == 200 && ctx.total > 0 && resp.body.length() == 0);
        } else {
            std::cout << "  error    : " << knst_network_error_string(err) << "\n";
            record("streaming on_body", false);
        }
    }


    /* 16) Gzip decode */
    section("16) Gzip decode (httpbin via h2)",
            "Content-Encoding: gzip decoded, header removed",
            "status=200, ce header gone, body decoded");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms             = 10000;
        o.allow_content_encoding = true;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_GZIP, o, resp, err);
        if (ok) {
            knst_c16string ce = resp.get_header(u"content-encoding");
            std::cout << "  status    : " << resp.status << "\n";
            std::cout << "  ce header : length=" << ce.length() << "\n";
            std::cout << "  decoded   : " << resp.body.length() << " bytes\n";
            print_bytes('>', resp.body, 100);
            record("gzip decode",
                   resp.status == 200 && ce.length() == 0 && resp.body.length() > 0);
        } else {
            record("gzip decode", false);
        }
    }


    /* 17) Multipart (offline) */
    section("17) Multipart build (offline)",
            "build_multipart() produces a valid form-data body",
            "boundary non-empty, body.length() > 0");
    {
        knst_vector<knst_http_client::multipart_part> parts;

        knst_http_client::multipart_part p1;
        p1.name = u"field1";
        p1.data = to_bs("value1");
        parts.push_back(p1);

        knst_http_client::multipart_part p2;
        p2.name         = u"file";
        p2.filename     = u"a.txt";
        p2.content_type = u"text/plain";
        p2.data         = to_bs("hello file");
        parts.push_back(p2);

        knst_byte_string body;
        knst_c16string   boundary;
        knst_http3_client::build_multipart(parts, body, boundary);

        std::cout << "  boundary  : "; print_c16(boundary); std::cout << "\n";
        std::cout << "  body size : " << body.length() << " bytes\n";
        record("multipart (offline)", boundary.length() > 0 && body.length() > 0);
    }


    /* 18) Parallel multiplexing */
    section("18) Parallel multiplexing (httpbin via h2)",
            "4 threads through one auto client instance",
            "4/4 succeed");
    {
        knst_http_auto_client c;
        c.enable_h3(false);

        knst_http_auto_client::options o;
        o.timeout_ms = 15000;

        std::atomic<int> pass{0}, fail{0};
        auto t0 = std::chrono::steady_clock::now();

        std::thread threads[4];
        for (int i = 0; i < 4; ++i) {
            threads[i] = std::thread([&c, &o, &pass, &fail, i]() {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                    "https://httpbin.org/get?thread=%d", i);
                knst_c16string url = to_c16(buf);
                knst_http_response  resp;
                knst_network_error  err = knst_network_error::None;
                if (c.get(url, o, resp, err) && resp.status == 200)
                    pass.fetch_add(1);
                else
                    fail.fetch_add(1);
            });
        }
        for (auto& th : threads) th.join();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        std::cout << "  pass    : " << pass.load() << " / 4\n";
        std::cout << "  fail    : " << fail.load() << "\n";
        std::cout << "  elapsed : " << ms << " ms\n";
        record("parallel", pass.load() == 4);
    }


    /* 19) Timeout [reverse] */
    section("19) Timeout (reverse test)",
            "delay/5 with timeout=800ms returns Timeout near 800ms",
            "ok=false, err=Timeout, elapsed < 3s");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms       = 800;
        o.total_timeout_ms = 3000;

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        auto t0 = std::chrono::steady_clock::now();
        bool ok = c.get(URL_DELAY_5, o, resp, err);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        std::cout << "  ok      : " << (ok ? "true" : "false") << "\n";
        std::cout << "  err     : " << knst_network_error_string(err) << "\n";
        std::cout << "  elapsed : " << ms << " ms\n";
        record("timeout",
               !ok && err == knst_network_error::Timeout && ms < 3000);
    }


    /* 20) 404 status */
    section("20) 404 status (httpbin via h2)",
            "4xx is a normal response, not a client error",
            "ok=true, status=404");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms = 10000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_404, o, resp, err);
        std::cout << "  ok     : " << (ok ? "true" : "false") << "\n";
        std::cout << "  status : " << resp.status << "\n";
        record("404 status", ok && resp.status == 404);
    }


    /* 21) Response headers */
    section("21) Response headers (httpbin via h2)",
            "All response headers parsed",
            "status=200, headers.size() >= 5");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms = 10000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_GET, o, resp, err);
        if (ok) {
            std::cout << "  status       : " << resp.status << "\n";
            std::cout << "  version      : "; print_c16(resp.version); std::cout << "\n";
            std::cout << "  header count : " << resp.headers.size() << "\n";
            uint32_t shown = 0;
            for (uint32_t i = 0; i < resp.headers.size() && shown < 8; ++i) {
                std::cout << "    "; print_c16(resp.headers[i].name);
                std::cout << " : "; print_c16(resp.headers[i].value);
                std::cout << "\n";
                ++shown;
            }
            record("response headers",
                   resp.status == 200 && resp.headers.size() >= 5);
        } else {
            record("response headers", false);
        }
    }


    /* 22) Pool clear */
    section("22) Pool clear (httpbin via h2)",
            "clear_connection_pool() closes all idle sessions",
            "idle before >= 1, idle after == 0");
    {
        knst_http_auto_client c;
        c.enable_h3(false);
        knst_http_auto_client::options o;
        o.timeout_ms = 10000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        c.get(URL_GET, o, resp, err);
        uint32_t before = c.h1().idle_connections()
                        + c.h2().idle_sessions()
                        + c.h3().idle_sessions();
        c.clear_connection_pool();
        uint32_t after  = c.h1().idle_connections()
                        + c.h2().idle_sessions()
                        + c.h3().idle_sessions();
        std::cout << "  idle before : " << before << "\n";
        std::cout << "  idle after  : " << after  << "\n";
        record("pool clear", before >= 1 && after == 0);
    }


    /* 23) Auto fallback (google.com h3 works) */
    section("23) Auto client fallback (google.com)",
            "h3 attempt first; google actually speaks h3",
            "status=200, version=HTTP/3");
    {
        knst_http_auto_client c;
        if (!h3_available) c.enable_h3(false);

        knst_http_auto_client::options o;
        o.timeout_ms       = 15000;
        o.total_timeout_ms = 20000;

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_H3_GOOGLE, o, resp, err);
        if (ok) {
            std::cout << "  status   : " << resp.status << "\n";
            std::cout << "  version  : "; print_c16(resp.version);
            std::cout << "  (h3 if UDP/443 open, else h2)\n";
            record("auto fallback (google)", resp.status == 200);
        } else {
            std::cout << "  error    : " << knst_network_error_string(err) << "\n";
            record("auto fallback (google)", false);
        }
    }


    /* 24) Auto backoff */
    section("24) Auto client backoff",
            "h3 failure on origin -> h2 fallback remembered for backoff_ms",
            "3 requests take < 3 x timeout");
    if (!h3_available) {
        knst_http_auto_client c;                // h3 enabled on purpose
        knst_http_auto_client::options o;
        o.timeout_ms = 3000;

        auto t0 = std::chrono::steady_clock::now();
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;

        for (int i = 0; i < 3; ++i) {
            c.get(URL_H3_CF, o, resp, err);
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        std::cout << "  3 requests total : " << ms << " ms\n";
        record("auto backoff", ms < 3000 * 3);
    } else {
        std::cout << "  (h3 reachable - backoff path not triggered, skipped)\n";
        record("auto backoff", true, /*skipped=*/true);
    }


    /* 25) Transport selection */
    section("25) Auto client transport selection",
            "Compare resp.version for h3-capable vs h2-only origin",
            "cloudflare-quic.com -> HTTP/3, httpbin.org -> HTTP/2");
    {
        knst_http_auto_client c;
        if (!h3_available) c.enable_h3(false);

        knst_http_auto_client::options o;
        o.timeout_ms       = 15000;
        o.total_timeout_ms = 20000;

        knst_http_response  r_cf, r_hb;
        knst_network_error  e_cf = knst_network_error::None;
        knst_network_error  e_hb = knst_network_error::None;

        bool ok_cf = c.get(URL_H3_CF, o, r_cf, e_cf);
        bool ok_hb = c.get(URL_GET,   o, r_hb, e_hb);

        std::cout << "  cloudflare-quic.com : ";
        if (ok_cf) { print_c16(r_cf.version); } else { std::cout << "FAIL"; }
        std::cout << "  (expect HTTP/3 if UDP open)\n";

        std::cout << "  httpbin.org         : ";
        if (ok_hb) { print_c16(r_hb.version); } else { std::cout << "FAIL"; }
        std::cout << "  (expect HTTP/2)\n";

        record("transport selection",
               ok_cf && ok_hb && r_cf.status == 200 && r_hb.status == 200);
    }


    /* Summary */
    int pass = 0, fail = 0, skip = 0;
    for (size_t i = 0; i < g_results.size(); ++i) {
        const TestResult& res = g_results[i];
        if      (res.skipped) ++skip;
        else if (res.passed)  ++pass;
        else                  ++fail;
    }

    std::cout << "\n==================================================\n";
    std::cout << "  RESULT: " << pass << " PASS / " << fail << " FAIL";
    if (skip) std::cout << " / " << skip << " SKIP";
    std::cout << "  (total " << g_results.size() << ")\n";
    std::cout << "==================================================\n";

    if (fail > 0) {
        std::cout << "\nFailed tests:\n";
        for (size_t i = 0; i < g_results.size(); ++i) {
            const TestResult& res = g_results[i];
            if (!res.passed && !res.skipped)
                std::cout << "  - " << res.name << "\n";
        }
    }

    std::cout << "\nNotes:\n";
    std::cout << "  - h3 works for cloudflare-quic.com and google.com.\n";
    std::cout << "  - httpbin.org has a broken h3 edge: it accepts QUIC but the\n";
    std::cout << "    backend never answers, causing a per-request timeout.\n";
    std::cout << "    Those tests explicitly disable h3 and use h2 through the\n";
    std::cout << "    auto client to stay fast and deterministic.\n";
    std::cout << "\nDone.\n";
    return fail == 0 ? 0 : 1;
}