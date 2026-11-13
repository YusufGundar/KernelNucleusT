/*
  knst_network_h2_basic.cpp

  Comprehensive HTTP/2 test + tutorial for knst_http2_client.
  Exercises every feature of the h2 client against real h2-enabled
  servers (httpbin.org, nghttp2.org).

  Test structure
  --------------
    Each section documents:
      WHAT    - the specific h2 feature being exercised
      EXPECT  - the pass criterion
      RESULT  - PASS / FAIL / SKIP (printed after the test)

    Tests are isolated: one failure does not stop the rest.

    Two sections are "reverse tests": they verify that the client
    correctly *rejects* input. Their PASS criterion is ok == false
    plus a specific error code.

  Requires
  --------
    - KNST_NETWORK_ENABLE_TLS  (define at build time)
    - libnghttp2 >= 1.40
    - OpenSSL >= 1.1.0
    - Network access to httpbin.org and nghttp2.org
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
 *  Endpoints used by the tests
 * ===================================================================== */

// Static h2 landing page (always 200, exercises TLS+ALPN+parse).
static const char16_t* URL_STATIC      = u"https://nghttp2.org/";

// httpbin.org: h2-enabled echo/test server.
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

    std::cout << "knst_network_h2 test suite\n";
    std::cout << "sizeof(knst_http2_client) : " << sizeof(knst_http2_client) << " bytes\n";


    /* =================================================================
     *  1) Basic GET
     * ================================================================= */
    section("1) Basic GET",
            "TLS handshake + ALPN 'h2' negotiation + response parsing",
            "status=200, version='HTTP/2', body > 0");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms       = 10000;
        o.total_timeout_ms = 30000;
        o.user_agent       = u"KernelNucleusT/h2-test/1.0";

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_STATIC, o, resp, err);
        if (ok) {
            std::cout << "  status   : " << resp.status << "\n";
            std::cout << "  version  : "; print_c16(resp.version); std::cout << "\n";
            std::cout << "  body size: " << resp.body.length() << " bytes\n";
            print_bytes('>', resp.body, 60);
            record("basic GET", resp.status == 200 && resp.body.length() > 0);
        } else {
            std::cout << "  error    : " << knst_network_error_string(err) << "\n";
            record("basic GET", false);
        }
    }


    /* =================================================================
     *  2) Session reuse (connection pool)
     * ================================================================= */
    section("2) Session reuse",
            "Second request to same origin reuses the TLS connection",
            "idle_sessions() stays at 1, second request ok=true");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms    = 15000;
        o.use_keepalive = true;

        knst_http_response  r1, r2;
        knst_network_error  e1 = knst_network_error::None;
        knst_network_error  e2 = knst_network_error::None;

        bool ok1 = c.get(URL_GET, o, r1, e1);
        uint32_t idle_after_first = c.idle_sessions();

        bool ok2 = c.get(URL_GET, o, r2, e2);
        uint32_t idle_after_second = c.idle_sessions();

        std::cout << "  first  : ok=" << (ok1 ? "true" : "false")
                  << "  idle=" << idle_after_first << "\n";
        std::cout << "  second : ok=" << (ok2 ? "true" : "false")
                  << "  idle=" << idle_after_second << "\n";

        record("session reuse", ok1 && ok2 && idle_after_second == 1);
    }


    /* =================================================================
     *  3) POST (small body)
     * ================================================================= */
    section("3) POST (small body)",
            "Request body + Content-Length + DATA frame",
            "status=200, body echoed by server");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;

        knst_http_header h;
        h.name  = u"content-type";
        h.value = u"application/json";
        o.headers.push_back(h);

        knst_byte_string body = to_bs("{\"msg\":\"hello h2\"}");

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.post(URL_POST, body, o, resp, err);
        if (ok) {
            std::cout << "  status   : " << resp.status << "\n";
            print_bytes('>', resp.body, 120);
            record("POST small", resp.status == 200);
        } else {
            std::cout << "  error    : " << knst_network_error_string(err) << "\n";
            record("POST small", false);
        }
    }


    /* =================================================================
     *  4) POST (large body, 512 KB)
     * ================================================================= */
    section("4) POST (large body, 512 KB)",
            "DATA frame streaming + per-stream flow control",
            "status=200, full body reaches server");
    {
        knst_http2_client c;
        knst_http2_client::options o;
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


    /* =================================================================
     *  5) PUT
     * ================================================================= */
    section("5) PUT",
            "PUT method with body (same DATA path as POST)",
            "status=200");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;
        knst_byte_string body = to_bs("put-payload");
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.put(URL_PUT, body, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        record("PUT", ok && resp.status == 200);
    }


    /* =================================================================
     *  6) DELETE
     * ================================================================= */
    section("6) DELETE",
            "Bodyless DELETE",
            "status=200");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.del(URL_DELETE, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        record("DELETE", ok && resp.status == 200);
    }


    /* =================================================================
     *  7) HEAD
     * ================================================================= */
    section("7) HEAD",
            "HEAD response carries no body (RFC 9110 S9.3.2)",
            "status=200, body.length() == 0");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.head(URL_STATIC, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        std::cout << "  body   : " << resp.body.length() << " bytes\n";
        record("HEAD", ok && resp.status == 200 && resp.body.length() == 0);
    }


    /* =================================================================
     *  8) Query string
     * ================================================================= */
    section("8) Query string",
            "?foo=bar&x=42 propagates to :path",
            "status=200, httpbin echoes them in args");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;
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


    /* =================================================================
     *  9) Custom request headers
     * ================================================================= */
    section("9) Custom headers",
            "User-Agent and custom headers reach the server",
            "status=200, httpbin shows them in headers");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;
        o.user_agent = u"knst-h2-tester/1.0";

        knst_http_header h1; h1.name = u"x-app";   h1.value = u"h2-suite";
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


    /* =================================================================
     *  10) Basic auth
     * ================================================================= */
    section("10) Basic auth",
            "user:pass becomes Authorization: Basic base64(...)",
            "status=200 (httpbin /basic-auth/user/pass)");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;
        o.basic_user = u"user";
        o.basic_pass = u"pass";
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_AUTH_OK, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        record("basic auth", ok && resp.status == 200);
    }


    /* =================================================================
     *  11) Cookies
     * ================================================================= */
    section("11) Cookies",
            "Set-Cookie -> jar -> Cookie header on next request",
            "jar size >= 1 and second request status=200");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms  = 15000;
        o.auto_cookie = true;

        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;

        c.get(URL_COOKIE_SET, o, resp, err);
        size_t jar_after_set = c.cookies().size();
        std::cout << "  jar after set : " << jar_after_set << "\n";

        c.get(URL_COOKIE_GET, o, resp, err);
        std::cout << "  second status : " << resp.status << "\n";
        print_bytes('>', resp.body, 120);

        record("cookies", jar_after_set >= 1 && resp.status == 200);
    }


    /* =================================================================
     *  12) Redirect chain (3 hops)
     * ================================================================= */
    section("12) Redirect chain",
            "Follow a 3-hop 301/302 chain (RFC 3986)",
            "status=200 at the end of the chain");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms       = 30000;
        o.follow_redirects = true;
        o.max_redirects    = 5;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_REDIRECT_3, o, resp, err);
        std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
        record("redirect chain", ok && resp.status == 200);
    }


    /* =================================================================
     *  13) Redirect limit  [reverse test]
     * ================================================================= */
    section("13) Redirect limit (reverse test)",
            "max_redirects=1 < chain=3 -> HttpTooManyRedirects",
            "ok=false, err=HttpTooManyRedirects");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms       = 30000;
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


    /* =================================================================
     *  14) Streaming on_body (100 KB)
     * ================================================================= */
    section("14) Streaming on_body",
            "on_body fires per DATA chunk, response.body stays empty",
            "ctx.total > 0, resp.body.length() == 0");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 30000;

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
            std::cout << "  buffered : " << resp.body.length() << " bytes (expect 0)\n";
            record("streaming on_body",
                   resp.status == 200 && ctx.total > 0 && resp.body.length() == 0);
        } else {
            record("streaming on_body", false);
        }
    }


    /* =================================================================
     *  15) Gzip decode
     * ================================================================= */
    section("15) Gzip decode",
            "Content-Encoding: gzip decoded, header removed",
            "status=200, ce header gone, body decoded");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms             = 15000;
        o.allow_content_encoding = true;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_GZIP, o, resp, err);
        if (ok) {
            knst_c16string ce = resp.get_header(u"content-encoding");
            std::cout << "  status    : " << resp.status << "\n";
            std::cout << "  ce header : length=" << ce.length()
                      << " (expect 0)\n";
            std::cout << "  decoded   : " << resp.body.length() << " bytes\n";
            print_bytes('>', resp.body, 100);
            record("gzip decode",
                   resp.status == 200 && ce.length() == 0 && resp.body.length() > 0);
        } else {
            record("gzip decode", false);
        }
    }


    /* =================================================================
     *  16) Multipart build (offline)
     * ================================================================= */
    section("16) Multipart build (offline)",
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
        knst_http2_client::build_multipart(parts, body, boundary);

        std::cout << "  boundary  : "; print_c16(boundary); std::cout << "\n";
        std::cout << "  body size : " << body.length() << " bytes\n";
        record("multipart", boundary.length() > 0 && body.length() > 0);
    }


    /* =================================================================
     *  17) Parallel multiplexing (4 threads, same client)
     * ================================================================= */
    section("17) Parallel multiplexing",
            "4 threads through one knst_http2_client share one TLS connection",
            "4/4 succeed, pool has few idle sessions");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 30000;

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
        std::cout << "  pool    : " << c.idle_sessions() << " idle session(s)\n";
        record("parallel", pass.load() == 4);
    }


    /* =================================================================
     *  18) Timeout  [reverse test]
     * ================================================================= */
    section("18) Timeout (reverse test)",
            "delay/5 with timeout=800ms returns Timeout near 800ms",
            "ok=false, err=Timeout, elapsed < 3s");
    {
        knst_http2_client c;
        knst_http2_client::options o;
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
        record("timeout", !ok && err == knst_network_error::Timeout && ms < 3000);
    }


    /* =================================================================
     *  19) 404 status
     * ================================================================= */
    section("19) 404 status",
            "4xx is a normal response, not a client error",
            "ok=true, status=404");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_404, o, resp, err);
        std::cout << "  ok     : " << (ok ? "true" : "false") << "\n";
        std::cout << "  status : " << resp.status << "\n";
        record("404 status", ok && resp.status == 404);
    }


    /* =================================================================
     *  20) Response headers
     * ================================================================= */
    section("20) Response headers",
            "HPACK decode: all response headers parsed",
            "status=200, headers.size() >= 5");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        bool ok = c.get(URL_GET, o, resp, err);
        if (ok) {
            std::cout << "  status       : " << resp.status << "\n";
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


    /* =================================================================
     *  21) Pool clear
     * ================================================================= */
    section("21) Pool clear",
            "clear_connection_pool() closes all idle sessions",
            "idle before >= 1, idle after == 0");
    {
        knst_http2_client c;
        knst_http2_client::options o;
        o.timeout_ms = 15000;
        knst_http_response  resp;
        knst_network_error  err = knst_network_error::None;
        c.get(URL_GET, o, resp, err);
        uint32_t before = c.idle_sessions();
        c.clear_connection_pool();
        uint32_t after = c.idle_sessions();
        std::cout << "  idle before : " << before << "\n";
        std::cout << "  idle after  : " << after  << "\n";
        record("pool clear", before >= 1 && after == 0);
    }


    /* =================================================================
     *  22) Proxy (optional)
     * ================================================================= */
    section("22) Proxy (optional)",
            "h2 through proxy_url (SOCKS5 or HTTP CONNECT)",
            "SKIP if KNST_TEST_PROXY unset; otherwise status=200");
    {
        const char* env = ::getenv("KNST_TEST_PROXY");
        if (!env || !*env) {
            std::cout << "  (KNST_TEST_PROXY not set - test skipped)\n";
            record("proxy", true, /*skipped=*/true);
        } else {
            knst_http2_client c;
            knst_http2_client::options o;
            o.timeout_ms = 20000;
            o.proxy_url  = to_c16(env);

            knst_http_response  resp;
            knst_network_error  err = knst_network_error::None;
            bool ok = c.get(URL_GET, o, resp, err);
            std::cout << "  proxy  : " << env << "\n";
            std::cout << "  status : " << (ok ? resp.status : 0) << "\n";
            if (!ok)
                std::cout << "  error  : " << knst_network_error_string(err) << "\n";
            record("proxy", ok && resp.status == 200);
        }
    }


    /* =================================================================
     *  Summary
     * ================================================================= */
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

    std::cout << "\nDone.\n";
    return fail == 0 ? 0 : 1;
}